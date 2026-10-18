// lmvideohost: the DSL host for lmvideo (R12). The host owns the process, the output and the FRAME LOOP; the lmscript `_dsl_`
// chunk is a callback that describes one frame at a time. It implements schema/lmvideo.dsl.json against the lmv:: library.
//
//   lmvideohost --dsl FILE.dslb --schema schema/lmvideo.dsl.json [--output PATH]
//
// Run order: (1) register the schema, load and verify the bytecode (nothing runs yet); (2) SETUP pass: run the chunk once with
// video.setup() == true, where it declares size, fps, frame count and output file; (3) the host creates the surface (and the
// ffmpeg encoder for video output) and runs the chunk once per frame; (4) finish and report.
// Exit codes: 0 ok, 1 bytecode/schema refused (nothing ran), 2 usage, 3 run-time error, 4 the chunk called out.fail().
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <optional>
#include <set>
#include <unistd.h>

#include "dslvm.hpp"
#include "encoder.hpp"
#include "lmvideo.hpp"
#include "schema.hpp"

namespace {

using dslvm::Args;
using dslvm::Value;

// Everything a chunk creates lives here for one run of the chunk and dies after it (R11/R12: handles cannot be stored).
class Arena {
 public:
  template <class T, class... A>
  T* make(A&&... a) {
    auto p = std::make_shared<T>(std::forward<A>(a)...);
    items_.push_back(p);
    return p.get();
  }
  void clear() { items_.clear(); }

 private:
  std::deque<std::shared_ptr<void>> items_;
};

// A Canvas is a non-owning view; the slot is empty during the setup pass so drawing then is a clear error.
struct CanvasSlot {
  std::optional<lmv::Canvas> canvas;
  lmv::Canvas& get() {
    if (!canvas) throw dslvm::Error("Canvas is not available during the setup pass (draw in the else branch of video.setup())");
    return *canvas;
  }
};

struct SurfaceObj {
  SurfaceObj(int w, int h) : surface(w, h) { slot.canvas = surface.canvas(); }
  lmv::Surface surface;
  CanvasSlot slot;
};

struct VideoState {
  bool setup = true;
  int width = 0, height = 0, fps = 30, frames = 1, crf = 18, index = 0;
  std::string output;
  bool size_set = false;
  const lmv::Surface* surface = nullptr;  // the frame being drawn (for save_frame)
  double t() const { return frames <= 1 ? 0.0 : static_cast<double>(index) / (frames - 1); }
};

struct OutputState {
  std::string failure;
  bool failed = false;
};

bool has_suffix(const std::string& s, const char* suffix) {
  size_t n = strlen(suffix);
  if (s.size() < n) return false;
  std::string tail = s.substr(s.size() - n);
  std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return std::tolower(c); });
  return tail == suffix;
}

bool is_image_path(const std::string& p) { return has_suffix(p, ".png") || has_suffix(p, ".jpg") || has_suffix(p, ".jpeg"); }

// Writes through a temporary sibling and renames it into place after every write succeeded, so a failure (full disk, bad
// path) never reports success and never leaves a partial image under the requested name.
void write_atomic(const std::string& path, const std::string& bytes) {
  size_t slash = path.rfind('/');
  std::string tmp = (slash == std::string::npos ? "" : path.substr(0, slash + 1)) + ".lmv-" + std::to_string(getpid()) + "-" +
                    (slash == std::string::npos ? path : path.substr(slash + 1));
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) { f.close(); unlink(tmp.c_str()); throw lmv::Error("cannot write " + path); }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); throw lmv::Error("cannot write " + path); }
}

void write_image(const lmv::Surface& s, const std::string& path) {
  if (has_suffix(path, ".png")) write_atomic(path, s.encode_png());
  else if (has_suffix(path, ".jpg") || has_suffix(path, ".jpeg")) write_atomic(path, s.encode_jpeg(90));
  else throw lmv::Error("unsupported image extension (use .png, .jpg or .jpeg): " + path);
}

float F(const Value& v) { return static_cast<float>(v.f); }

// Wraps dslvm::Registry with what the VM does not do: every argument and result is checked against the schema's signature
// (the verifier checks names and arities only), library errors become DSL run-time errors that name the callee, and the
// registrations are remembered so the host can refuse to start when its registry and the schema disagree.
class Registrar {
 public:
  Registrar(dslvm::Registry& reg, const schema::Schema& schema) : reg_(reg), schema_(schema) {}

  void context(const std::string& name, const std::string& type) {
    reg_.context(name, type);
    contexts_[name] = type;
  }

  void method(const std::string& type, const std::string& name, size_t argc, dslvm::Method fn) {
    methods_.insert(type + "." + name);
    arity_[type + "." + name] = argc;
    const schema::Sig* sig = find(schema_.types, type, name);
    reg_.method(type, name, argc, [type, name, fn, sig](void* self, Args& a) {
      std::string what = type + "." + name;
      if (!sig) throw dslvm::Error(what + ": not in the schema");
      check(what, *sig, a);
      try {
        return checked_result(what, *sig, fn(self, a));
      } catch (const lmv::Error& e) {
        throw dslvm::Error(what + ": " + e.what());
      }
    });
  }

  void function(const std::string& name, size_t argc, dslvm::Function fn) {
    functions_.insert(name);
    arity_[name] = argc;
    auto it = schema_.functions.find(name);
    const schema::Sig* sig = it == schema_.functions.end() ? nullptr : &it->second;
    reg_.function(name, argc, [name, fn, sig](Args& a) {
      if (!sig) throw dslvm::Error(name + ": not in the schema");
      check(name, *sig, a);
      try {
        return checked_result(name, *sig, fn(a));
      } catch (const lmv::Error& e) {
        throw dslvm::Error(name + ": " + e.what());
      }
    });
  }

  // Startup self-check: the registry must be exactly the schema (names, arities, context). Throws with every difference.
  void verify() const {
    std::string diff;
    for (const auto& t : schema_.types)
      for (const auto& m : t.second) {
        std::string key = t.first + "." + m.first;
        if (!methods_.count(key)) diff += " missing method " + key + ";";
        else if (arity_.at(key) != m.second.params.size()) diff += " arity of " + key + ";";
      }
    for (const auto& f : schema_.functions) {
      if (!functions_.count(f.first)) diff += " missing function " + f.first + ";";
      else if (arity_.at(f.first) != f.second.params.size()) diff += " arity of " + f.first + ";";
    }
    for (const auto& m : methods_) {
      size_t dot = m.find('.');
      auto t = schema_.types.find(m.substr(0, dot));
      if (t == schema_.types.end() || !t->second.count(m.substr(dot + 1))) diff += " method " + m + " is not in the schema;";
    }
    for (const auto& f : functions_)
      if (!schema_.functions.count(f)) diff += " function " + f + " is not in the schema;";
    if (contexts_ != schema_.context) diff += " context names or types differ;";
    if (!diff.empty()) throw dslvm::Error("host registry does not match the schema:" + diff);
  }

 private:
  static const schema::Sig* find(const std::map<std::string, std::map<std::string, schema::Sig>>& types, const std::string& type, const std::string& name) {
    auto t = types.find(type);
    if (t == types.end()) return nullptr;
    auto m = t->second.find(name);
    return m == t->second.end() ? nullptr : &m->second;
  }
  static bool matches(const std::string& type, const Value& v) {
    if (type == "int") return v.kind == Value::INT;
    if (type == "float") return v.kind == Value::FLOAT;
    if (type == "string") return v.kind == Value::STR;
    if (type == "bool") return v.kind == Value::BOOL;
    if (type == "void") return v.kind == Value::VOID;
    return v.kind == Value::OBJ && v.type == type && v.obj != nullptr;
  }
  static std::string describe(const Value& v) { return v.kind == Value::OBJ ? v.type : std::string(v.kind_name()); }
  static void check(const std::string& what, const schema::Sig& sig, const Args& a) {
    if (a.size() != sig.params.size()) throw dslvm::Error(what + ": expected " + std::to_string(sig.params.size()) + " argument(s)");
    for (size_t i = 0; i < a.size(); ++i)
      if (!matches(sig.params[i], a[i]))
        throw dslvm::Error(what + ": argument " + std::to_string(i + 1) + " must be " + sig.params[i] + ", got " + describe(a[i]));
  }
  static Value checked_result(const std::string& what, const schema::Sig& sig, Value v) {
    if (!matches(sig.returns, v)) throw dslvm::Error(what + ": host bug: returned " + describe(v) + ", the schema says " + sig.returns);
    return v;
  }

  dslvm::Registry& reg_;
  const schema::Schema& schema_;
  std::set<std::string> methods_, functions_;
  std::map<std::string, size_t> arity_;
  std::map<std::string, std::string> contexts_;
};

std::string hex_color(const lmv::Color& c) {
  char buf[16];
  snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
  return buf;
}

// The schema's implementation. `video`, `out` and `main_canvas` are the context objects the chunk sees.
void register_schema(Registrar& reg, Arena& arena, OutputState& output) {
  reg.context("canvas", "Canvas");
  reg.context("video", "Video");
  reg.context("out", "Output");

  auto paint = [](void* p) -> lmv::Paint& { return *static_cast<lmv::Paint*>(p); };
  auto path = [](void* p) -> lmv::Path& { return *static_cast<lmv::Path*>(p); };
  auto font = [](void* p) -> lmv::Font& { return *static_cast<lmv::Font*>(p); };
  auto self = [](const char* type, void* p) { return Value::object(type, p); };

  // ---- Paint (setters return self) ----
  reg.method("Paint", "color", 1, [=](void* p, Args& a) { paint(p).color(lmv::parse_color(a[0].s)); return self("Paint", p); });
  reg.method("Paint", "alpha", 1, [=](void* p, Args& a) { paint(p).alpha(F(a[0])); return self("Paint", p); });
  reg.method("Paint", "antialias", 1, [=](void* p, Args& a) { paint(p).antialias(a[0].b); return self("Paint", p); });
  reg.method("Paint", "fill", 0, [=](void* p, Args&) { paint(p).fill(); return self("Paint", p); });
  reg.method("Paint", "stroke", 0, [=](void* p, Args&) { paint(p).stroke(); return self("Paint", p); });
  reg.method("Paint", "stroke_width", 1, [=](void* p, Args& a) { paint(p).stroke_width(F(a[0])); return self("Paint", p); });
  reg.method("Paint", "stroke_cap", 1, [=](void* p, Args& a) { paint(p).stroke_cap(a[0].s); return self("Paint", p); });
  reg.method("Paint", "stroke_join", 1, [=](void* p, Args& a) { paint(p).stroke_join(a[0].s); return self("Paint", p); });
  reg.method("Paint", "gradient", 6, [=](void* p, Args& a) {
    paint(p).linear_gradient(F(a[0]), F(a[1]), F(a[2]), F(a[3]), lmv::parse_color(a[4].s), lmv::parse_color(a[5].s));
    return self("Paint", p);
  });
  reg.method("Paint", "dash", 2, [=](void* p, Args& a) { paint(p).dash(F(a[0]), F(a[1])); return self("Paint", p); });
  reg.method("Paint", "blur", 1, [=](void* p, Args& a) { paint(p).blur(F(a[0])); return self("Paint", p); });
  reg.function("paint", 0, [&arena](Args&) { return Value::object("Paint", arena.make<lmv::Paint>()); });

  // ---- Path ----
  reg.method("Path", "move_to", 2, [=](void* p, Args& a) { path(p).move_to(F(a[0]), F(a[1])); return self("Path", p); });
  reg.method("Path", "line_to", 2, [=](void* p, Args& a) { path(p).line_to(F(a[0]), F(a[1])); return self("Path", p); });
  reg.method("Path", "quad_to", 4, [=](void* p, Args& a) { path(p).quad_to(F(a[0]), F(a[1]), F(a[2]), F(a[3])); return self("Path", p); });
  reg.method("Path", "cubic_to", 6, [=](void* p, Args& a) {
    path(p).cubic_to(F(a[0]), F(a[1]), F(a[2]), F(a[3]), F(a[4]), F(a[5]));
    return self("Path", p);
  });
  reg.method("Path", "close", 0, [=](void* p, Args&) { path(p).close(); return self("Path", p); });
  reg.method("Path", "rect", 4, [=](void* p, Args& a) { path(p).add_rect(F(a[0]), F(a[1]), F(a[2]), F(a[3])); return self("Path", p); });
  reg.method("Path", "oval", 4, [=](void* p, Args& a) { path(p).add_oval(F(a[0]), F(a[1]), F(a[2]), F(a[3])); return self("Path", p); });
  reg.method("Path", "circle", 3, [=](void* p, Args& a) { path(p).add_circle(F(a[0]), F(a[1]), F(a[2])); return self("Path", p); });
  reg.method("Path", "round_rect", 6, [=](void* p, Args& a) {
    path(p).add_round_rect(F(a[0]), F(a[1]), F(a[2]), F(a[3]), F(a[4]), F(a[5]));
    return self("Path", p);
  });
  reg.function("path", 0, [&arena](Args&) { return Value::object("Path", arena.make<lmv::Path>()); });

  // ---- Font, Image, Svg ----
  reg.method("Font", "bold", 1, [=](void* p, Args& a) { font(p).embolden(a[0].b); return self("Font", p); });
  reg.method("Font", "measure", 1, [=](void* p, Args& a) { return Value::real(font(p).measure(a[0].s)); });
  reg.function("font", 1, [&arena](Args& a) { return Value::object("Font", arena.make<lmv::Font>(F(a[0]))); });
  reg.method("Image", "width", 0, [](void* p, Args&) { return Value::integer(static_cast<lmv::Image*>(p)->width()); });
  reg.method("Image", "height", 0, [](void* p, Args&) { return Value::integer(static_cast<lmv::Image*>(p)->height()); });
  reg.function("image_file", 1, [&arena](Args& a) { return Value::object("Image", arena.make<lmv::Image>(lmv::Image::from_file(a[0].s))); });
  reg.method("Svg", "width", 0, [](void* p, Args&) { return Value::real(static_cast<lmv::Svg*>(p)->width()); });
  reg.method("Svg", "height", 0, [](void* p, Args&) { return Value::real(static_cast<lmv::Svg*>(p)->height()); });
  reg.function("svg", 1, [&arena](Args& a) { return Value::object("Svg", arena.make<lmv::Svg>(lmv::Svg::parse(a[0].s))); });
  reg.function("svg_file", 1, [&arena](Args& a) {
    std::ifstream in(a[0].s, std::ios::binary);
    if (!in) throw lmv::Error("cannot read " + a[0].s);
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Value::object("Svg", arena.make<lmv::Svg>(lmv::Svg::parse(xml)));
  });

  // ---- Canvas ----
  auto cv = [](void* p) -> lmv::Canvas& { return static_cast<CanvasSlot*>(p)->get(); };
  auto P = [](const Value& v) -> const lmv::Paint& { return *static_cast<lmv::Paint*>(v.obj); };
  reg.method("Canvas", "clear", 1, [=](void* p, Args& a) { cv(p).clear(lmv::parse_color(a[0].s)); return Value::none(); });
  reg.method("Canvas", "rect", 5, [=](void* p, Args& a) { cv(p).draw_rect(F(a[0]), F(a[1]), F(a[2]), F(a[3]), P(a[4])); return Value::none(); });
  reg.method("Canvas", "round_rect", 7, [=](void* p, Args& a) {
    cv(p).draw_round_rect(F(a[0]), F(a[1]), F(a[2]), F(a[3]), F(a[4]), F(a[5]), P(a[6]));
    return Value::none();
  });
  reg.method("Canvas", "oval", 5, [=](void* p, Args& a) { cv(p).draw_oval(F(a[0]), F(a[1]), F(a[2]), F(a[3]), P(a[4])); return Value::none(); });
  reg.method("Canvas", "circle", 4, [=](void* p, Args& a) { cv(p).draw_circle(F(a[0]), F(a[1]), F(a[2]), P(a[3])); return Value::none(); });
  reg.method("Canvas", "line", 5, [=](void* p, Args& a) { cv(p).draw_line(F(a[0]), F(a[1]), F(a[2]), F(a[3]), P(a[4])); return Value::none(); });
  reg.method("Canvas", "path", 2, [=](void* p, Args& a) {
    cv(p).draw_path(*static_cast<lmv::Path*>(a[0].obj), P(a[1]));
    return Value::none();
  });
  reg.method("Canvas", "text", 5, [=](void* p, Args& a) {
    cv(p).draw_text(a[0].s, F(a[1]), F(a[2]), *static_cast<lmv::Font*>(a[3].obj), P(a[4]));
    return Value::none();
  });
  reg.method("Canvas", "image", 3, [=](void* p, Args& a) {
    cv(p).draw_image(*static_cast<lmv::Image*>(a[0].obj), F(a[1]), F(a[2]));
    return Value::none();
  });
  reg.method("Canvas", "image_rect", 5, [=](void* p, Args& a) {
    cv(p).draw_image_rect(*static_cast<lmv::Image*>(a[0].obj), F(a[1]), F(a[2]), F(a[3]), F(a[4]));
    return Value::none();
  });
  reg.method("Canvas", "svg", 5, [=](void* p, Args& a) {
    cv(p).draw_svg(*static_cast<lmv::Svg*>(a[0].obj), F(a[1]), F(a[2]), F(a[3]), F(a[4]));
    return Value::none();
  });
  reg.method("Canvas", "save", 0, [=](void* p, Args&) { cv(p).save(); return Value::none(); });
  reg.method("Canvas", "restore", 0, [=](void* p, Args&) { cv(p).restore(); return Value::none(); });
  reg.method("Canvas", "translate", 2, [=](void* p, Args& a) { cv(p).translate(F(a[0]), F(a[1])); return Value::none(); });
  reg.method("Canvas", "scale", 2, [=](void* p, Args& a) { cv(p).scale(F(a[0]), F(a[1])); return Value::none(); });
  reg.method("Canvas", "rotate", 1, [=](void* p, Args& a) { cv(p).rotate(F(a[0])); return Value::none(); });
  reg.method("Canvas", "clip_rect", 4, [=](void* p, Args& a) { cv(p).clip_rect(F(a[0]), F(a[1]), F(a[2]), F(a[3])); return Value::none(); });

  // ---- Surface (offscreen drawing) ----
  reg.function("surface", 2, [&arena](Args& a) {
    if (a[0].i < 1 || a[1].i < 1 || a[0].i > 8192 || a[1].i > 8192) throw lmv::Error("size must be 1..8192");
    return Value::object("Surface", arena.make<SurfaceObj>(a[0].i, a[1].i));
  });
  reg.method("Surface", "canvas", 0, [](void* p, Args&) { return Value::object("Canvas", &static_cast<SurfaceObj*>(p)->slot); });
  reg.method("Surface", "image", 0, [&arena](void* p, Args&) {
    return Value::object("Image", arena.make<lmv::Image>(static_cast<SurfaceObj*>(p)->surface.snapshot()));
  });

  // ---- Video: setup declarations and per-frame facts ----
  auto vs = [](void* p) -> VideoState& { return *static_cast<VideoState*>(p); };
  auto setup_only = [vs](void* p, const char* what) {
    if (!vs(p).setup) throw dslvm::Error(std::string("Video.") + what + " can only be called in the setup pass (inside if (video.setup()))");
  };
  reg.method("Video", "setup", 0, [=](void* p, Args&) { return Value::boolean(vs(p).setup); });
  reg.method("Video", "size", 2, [=](void* p, Args& a) {
    setup_only(p, "size");
    if (a[0].i < 2 || a[1].i < 2 || a[0].i > 8192 || a[1].i > 8192) throw lmv::Error("size must be 2..8192 per side");
    vs(p).width = a[0].i; vs(p).height = a[1].i; vs(p).size_set = true;
    return Value::none();
  });
  reg.method("Video", "fps", 1, [=](void* p, Args& a) {
    setup_only(p, "fps");
    if (a[0].i < 1 || a[0].i > 240) throw lmv::Error("fps must be 1..240");
    vs(p).fps = a[0].i;
    return Value::none();
  });
  reg.method("Video", "frames", 1, [=](void* p, Args& a) {
    setup_only(p, "frames");
    if (a[0].i < 1 || a[0].i > 100000) throw lmv::Error("frames must be 1..100000");
    vs(p).frames = a[0].i;
    return Value::none();
  });
  reg.method("Video", "crf", 1, [=](void* p, Args& a) {
    setup_only(p, "crf");
    if (a[0].i < 0 || a[0].i > 51) throw lmv::Error("crf must be 0..51");
    vs(p).crf = a[0].i;
    return Value::none();
  });
  reg.method("Video", "output", 1, [=](void* p, Args& a) {
    setup_only(p, "output");
    if (a[0].s.empty()) throw lmv::Error("output path is empty");
    vs(p).output = a[0].s;
    return Value::none();
  });
  reg.method("Video", "index", 0, [=](void* p, Args&) { return Value::integer(vs(p).index); });
  reg.method("Video", "count", 0, [=](void* p, Args&) { return Value::integer(vs(p).frames); });
  reg.method("Video", "t", 0, [=](void* p, Args&) { return Value::real(vs(p).t()); });
  reg.method("Video", "time", 0, [=](void* p, Args&) { return Value::real(static_cast<double>(vs(p).index) / vs(p).fps); });
  reg.method("Video", "width", 0, [=](void* p, Args&) { return Value::integer(vs(p).width); });
  reg.method("Video", "height", 0, [=](void* p, Args&) { return Value::integer(vs(p).height); });
  reg.method("Video", "save_frame", 1, [=](void* p, Args& a) {
    if (!vs(p).surface) throw lmv::Error("no frame is being drawn (call it in the else branch of video.setup())");
    write_image(*vs(p).surface, a[0].s);
    return Value::none();
  });

  // ---- Output ----
  reg.method("Output", "print", 1, [](void*, Args& a) { std::cout << a[0].s << "\n"; return Value::none(); });
  reg.method("Output", "fail", 1, [&output](void*, Args& a) {
    if (!output.failed) output.failure = a[0].s;
    output.failed = true;
    return Value::none();
  });

  // ---- Math and colour helpers (the DSL has only + - * / on its numbers) ----
  auto num = [](const char* name, size_t argc, std::function<double(Args&)> fn) {
    return std::make_pair(std::string(name), std::make_pair(argc, fn));
  };
  for (auto& [name, spec] : {
           num("sin", 1, [](Args& a) { return std::sin(a[0].f); }),
           num("cos", 1, [](Args& a) { return std::cos(a[0].f); }),
           num("sqrt", 1, [](Args& a) { if (a[0].f < 0) throw dslvm::Error("sqrt of a negative number"); return std::sqrt(a[0].f); }),
           num("abs", 1, [](Args& a) { return std::fabs(a[0].f); }),
           num("floor", 1, [](Args& a) { return std::floor(a[0].f); }),
           num("pow", 2, [](Args& a) { return std::pow(a[0].f, a[1].f); }),
           num("min", 2, [](Args& a) { return std::min(a[0].f, a[1].f); }),
           num("max", 2, [](Args& a) { return std::max(a[0].f, a[1].f); }),
           num("lerp", 3, [](Args& a) { return a[0].f + (a[1].f - a[0].f) * a[2].f; }),
           num("clamp", 3, [](Args& a) { return std::min(std::max(a[0].f, a[1].f), a[2].f); }),
           num("ease", 1, [](Args& a) { double t = std::min(std::max(a[0].f, 0.0), 1.0); return t * t * (3.0 - 2.0 * t); }),
       }) {
    auto fn = spec.second;
    reg.function(name, spec.first, [fn](Args& a) { return Value::real(fn(a)); });
  }
  reg.function("to_int", 1, [](Args& a) {
    double v = std::trunc(a[0].f);
    if (!(v >= -2147483648.0 && v <= 2147483647.0)) throw lmv::Error("value does not fit an int");
    return Value::integer(static_cast<int32_t>(v));
  });
  reg.function("mix", 3, [](Args& a) {
    lmv::Color c0 = lmv::parse_color(a[0].s), c1 = lmv::parse_color(a[1].s);
    if (!std::isfinite(a[2].f)) throw lmv::Error("the interpolation factor must be finite");
    double t = std::min(std::max(a[2].f, 0.0), 1.0);
    auto ch = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
    return Value::string(hex_color(lmv::Color(ch(c0.r, c1.r), ch(c0.g, c1.g), ch(c0.b, c1.b), ch(c0.a, c1.a))));
  });
}

}  // namespace

int main(int argc, char** argv) {
  std::string dsl_path, schema_path, output_override;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&](const char* flag) {
      if (i + 1 >= argc) { std::cerr << "error: " << flag << " needs a value\n"; std::exit(2); }
      return std::string(argv[++i]);
    };
    if (a == "--dsl") dsl_path = value("--dsl");
    else if (a == "--schema") schema_path = value("--schema");
    else if (a == "--output") output_override = value("--output");
    else { std::cerr << "usage: lmvideohost --dsl FILE.dslb --schema FILE.json [--output PATH]\n"; return 2; }
  }
  if (dsl_path.empty() || schema_path.empty()) { std::cerr << "error: --dsl FILE and --schema FILE are required\n"; return 2; }

  // (1) Register the schema's implementation, then load and verify the bytecode; nothing has run yet.
  Arena arena;
  VideoState video;
  OutputState output;
  dslvm::Registry reg;
  dslvm::Program program;
  schema::Schema schema;  // outlives the run: the registered closures point into it
  std::optional<Registrar> registrar_slot;
  try {
    std::string schema_bytes = dslvm::read_file(schema_path);
    schema = schema::parse(schema_bytes);
    Registrar& registrar = registrar_slot.emplace(reg, schema);
    register_schema(registrar, arena, output);
    registrar.verify();  // the registry IS the schema, or nothing starts
    program = dslvm::Program::load(dslvm::read_file(dsl_path), schema_bytes, reg);
  } catch (const dslvm::Error& e) {
    std::cerr << "error: " << dsl_path << ": " << e.what() << "\n";
    return 1;
  }

  CanvasSlot main_canvas;  // empty during setup
  dslvm::Context ctx;
  ctx.set("canvas", &main_canvas);
  ctx.set("video", &video);
  ctx.set("out", &output);

  try {
    // (2) Setup pass: the chunk declares size, fps, frames and the output file.
    video.setup = true;
    program.run(ctx, reg);
    arena.clear();
    if (output.failed) { std::cerr << "fail: " << output.failure << "\n"; return 4; }
    if (!output_override.empty()) video.output = output_override;
    if (!video.size_set) throw dslvm::Error("the chunk did not call video.size(w, h) in the setup pass");
    if (video.output.empty()) throw dslvm::Error("the chunk did not call video.output(path) in the setup pass");
    bool image = is_image_path(video.output);
    if (image && video.frames != 1) throw dslvm::Error("an image output (.png/.jpg) needs video.frames(1), not " + std::to_string(video.frames));

    // (3) The frame loop belongs to the host.
    video.setup = false;
    lmv::Surface surface(video.width, video.height);
    video.surface = &surface;
    main_canvas.canvas = surface.canvas();
    std::unique_ptr<lmv::Encoder> encoder;
    if (!image) {
      lmv::EncoderOptions opt;
      opt.fps = video.fps;
      opt.crf = video.crf;
      encoder = std::make_unique<lmv::Encoder>(video.output, video.width, video.height, opt);
    }
    for (video.index = 0; video.index < video.frames; ++video.index) {
      main_canvas.get().clear(image ? lmv::Color(0, 0, 0, 0) : lmv::Color(0, 0, 0, 255));
      program.run(ctx, reg);
      arena.clear();
      if (output.failed) { std::cerr << "fail: " << output.failure << "\n"; return 4; }
      if (encoder) encoder->add_frame(surface.rgba());
    }
    if (encoder) encoder->finish();
    else write_image(surface, video.output);

    // (4) Report.
    if (image) std::cerr << "wrote " << video.output << " (" << video.width << "x" << video.height << ")\n";
    else std::cerr << "wrote " << video.output << " (" << video.width << "x" << video.height << ", " << video.frames << " frames at " << video.fps << " fps)\n";
  } catch (const dslvm::Error& e) {
    std::cout.flush();
    std::cerr << "dsl runtime error: " << e.what() << "\n";
    return 3;
  } catch (const lmv::Error& e) {
    std::cout.flush();
    std::cerr << "error: " << e.what() << "\n";
    return 3;
  }
  return 0;
}
