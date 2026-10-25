#include "diagram.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace lmvdiagram {
namespace {

void check_label(const std::string& label, const char* what) {
  if (label.size() > 4096) throw lmv::Error(std::string(what) + " exceeds 4096 bytes");
}

// Diagram fills and strokes accept lmvideo colour names plus lmdraw's "none".
lmv::Color shape_fill(const std::string& color) {
  if (color == "none") return lmv::Color(0, 0, 0, 0);
  return lmv::parse_color(color);
}
bool has_fill(const std::string& color) {
  return !color.empty() && color != "none" && color != "transparent";
}
bool has_stroke(const std::string& color, double width) {
  return !color.empty() && color != "none" && color != "transparent" && width > 0;
}

lmv::Paint fill_paint(const std::string& color, double opacity) {
  auto p = lmv::Paint().fill().color(shape_fill(color)).antialias(true);
  if (opacity < 1.0) p.alpha(static_cast<float>(std::max(0.0, opacity)));
  return p;
}
lmv::Paint stroke_paint(const std::string& color, double width, double opacity, bool round_cap = true) {
  auto p = lmv::Paint().stroke().stroke_width(static_cast<float>(width)).color(lmv::parse_color(color)).antialias(true);
  if (round_cap) { p.stroke_cap("round"); p.stroke_join("round"); }
  if (opacity < 1.0) p.alpha(static_cast<float>(std::max(0.0, opacity)));
  return p;
}

// Greedy word wrap like lmdraw's centered labels; at most 256 lines.
std::string wrap_text(const std::string& text, double width, float size) {
  std::istringstream paragraphs(text);
  std::string paragraph, result;
  size_t lines = 0;
  bool first_paragraph = true;
  while (std::getline(paragraphs, paragraph, '\n')) {
    if (!first_paragraph) result += '\n';
    first_paragraph = false;
    std::istringstream words(paragraph);
    std::string word, line;
    if (!(words >> word)) { ++lines; continue; }
    while (true) {
      std::string candidate = line.empty() ? word : line + " " + word;
      lmv::Font font(size);
      const double measured = font.measure(candidate);
      if (!line.empty() && measured > width) {
        result += line + '\n';
        if (++lines > 256) throw lmv::Error("wrapped label may contain at most 256 lines");
        line = word;
      } else {
        line = std::move(candidate);
      }
      if (!(words >> word)) break;
    }
    result += line;
    if (++lines > 256) throw lmv::Error("wrapped label may contain at most 256 lines");
  }
  return result;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line, '\n')) out.push_back(line);
  if (!text.empty() && text.back() == '\n') out.emplace_back();
  return out;
}

// Centered measured label: baseline a third of the size below center, a
// wrapped multi-line label shifts up 0.6em so it stays visually centered.
void centered_label(lmv::Canvas& c, double cx, double cy, double size, const std::string& label,
                    double wrap, double opacity) {
  if (label.empty()) return;
  check_label(label, "shape label");
  const std::string wrapped = wrap_text(label, wrap, static_cast<float>(size));
  double baseline = cy + 0.35 * size;
  const auto lines = split_lines(wrapped);
  if (lines.size() > 1) baseline -= 0.6 * size;
  lmv::Font font(static_cast<float>(size));
  const auto paint = fill_paint("#dbe7f3", opacity);
  for (size_t i = 0; i < lines.size(); ++i)
    c.draw_text(lines[i], static_cast<float>(cx - font.measure(lines[i]) / 2.0),
                static_cast<float>(baseline + 1.2 * size * double(i)), font, paint);
}

void marker(lmv::Canvas& c, const std::string& kind, double px, double py, double dir_x, double dir_y,
            const std::string& color, double width, double opacity) {
  const double len = std::hypot(dir_x, dir_y);
  if (len == 0.0) return;
  const double ux = dir_x / len, uy = dir_y / len;
  const auto paint = fill_paint(color, opacity);
  if (kind == "arrow") {
    const double back = 4.0 * width, half = 2.0 * width;
    lmv::Path path;
    path.move_to(static_cast<float>(px), static_cast<float>(py));
    path.line_to(static_cast<float>(px - ux * back - uy * half), static_cast<float>(py - uy * back + ux * half));
    path.line_to(static_cast<float>(px - ux * back + uy * half), static_cast<float>(py - uy * back - ux * half));
    path.close();
    c.draw_path(path, paint);
  } else if (kind == "dot") {
    c.draw_circle(static_cast<float>(px), static_cast<float>(py), static_cast<float>(1.5 * width), paint);
  } else {  // diamond
    const double half = 2.0 * width;
    lmv::Path path;
    path.move_to(static_cast<float>(px), static_cast<float>(py - half));
    path.line_to(static_cast<float>(px + half), static_cast<float>(py));
    path.line_to(static_cast<float>(px), static_cast<float>(py + half));
    path.line_to(static_cast<float>(px - half), static_cast<float>(py));
    path.close();
    c.draw_path(path, paint);
  }
}

std::pair<double, double> polyline_point(const std::vector<std::pair<double, double>>& pts, double t) {
  std::vector<double> lens(pts.size(), 0.0);
  double total = 0.0;
  for (size_t i = 1; i < pts.size(); ++i) {
    total += std::hypot(pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
    lens[i] = total;
  }
  if (total == 0.0) return pts.front();
  const double target = t * total;
  for (size_t i = 1; i < pts.size(); ++i) {
    if (target <= lens[i] || i + 1 == pts.size()) {
      const double seg = lens[i] - lens[i - 1];
      const double u = seg > 0.0 ? (target - lens[i - 1]) / seg : 0.0;
      return {pts[i - 1].first + u * (pts[i].first - pts[i - 1].first),
              pts[i - 1].second + u * (pts[i].second - pts[i - 1].second)};
    }
  }
  return pts.back();
}

// Applies the shared presentation attributes ("visibility", "opacity").
bool common_attr(bool& visible, double& opacity, const std::string& name, const std::string& value) {
  if (name == "visibility") {
    if (value != "visible" && value != "hidden") throw lmv::Error("visibility must be visible or hidden");
    visible = value == "visible";
    return true;
  }
  if (name == "opacity") {
    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v) || v < 0 || v > 1) throw lmv::Error("opacity must be a number from 0 to 1");
    opacity = v;
    return true;
  }
  return false;
}

}  // namespace

// Applies an SVG-style presentation attribute to a shape (used by Shape.set_attr
// and ShapeItem); common names (visibility, opacity) live on the shape itself.
bool shape_set_attr(lmvlayout::Shape& s, const std::string& name, const std::string& value) {
  if (common_attr(s.visible, s.opacity, name, value)) return true;
  if (name == "fill") {
    if (!value.empty() && value != "none" && value != "transparent") (void)lmv::parse_color(value);
    s.fill = value;
    return true;
  }
  if (name == "stroke") {
    if (!value.empty() && value != "none" && value != "transparent") (void)lmv::parse_color(value);
    s.stroke = value;
    return true;
  }
  if (name == "stroke-width") {
    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v) || v < 0 || v > 256) throw lmv::Error("stroke-width must be in [0, 256]");
    s.stroke_width = v;
    return true;
  }
  return false;
}

namespace {

void validate_color_or_none(const std::string& color, const char* what) {
  if (color == "none") return;
  (void)lmv::parse_color(color);
  (void)what;
}

}  // namespace

bool is_anchor(const std::string& name) {
  return name == "n" || name == "s" || name == "e" || name == "w" || name == "ne" || name == "nw" ||
         name == "se" || name == "sw" || name == "c";
}

std::pair<double, double> anchor_point(const lmvlayout::Shape& d, const std::string& anchor) {
  const double cx = d.x + d.w / 2.0, cy = d.y + d.h / 2.0;
  if (anchor == "n") return {cx, d.y};
  if (anchor == "s") return {cx, d.y + d.h};
  if (anchor == "e") return {d.x + d.w, cy};
  if (anchor == "w") return {d.x, cy};
  if (anchor == "c") return {cx, cy};
  if (anchor == "ne") return {d.x + d.w, d.y};
  if (anchor == "nw") return {d.x, d.y};
  if (anchor == "se") return {d.x + d.w, d.y + d.h};
  if (anchor == "sw") return {d.x, d.y + d.h};
  throw lmv::Error("anchor must be one of n, s, e, w, ne, nw, se, sw, c");
}

std::vector<float> parse_dash(const std::string& value) {
  std::vector<float> out;
  if (value.empty() || value == "none") return out;
  std::string normalized = value;
  std::replace(normalized.begin(), normalized.end(), ',', ' ');
  std::istringstream in(normalized);
  std::string token;
  while (in >> token) {
    if (out.size() >= 32) throw lmv::Error("dash lists may contain at most 32 lengths");
    char* end = nullptr;
    const double v = std::strtod(token.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v) || v <= 0) throw lmv::Error("dash must be 'none' or a list of positive pixel lengths");
    out.push_back(static_cast<float>(v));
  }
  return out;
}

void render_shape(lmv::Canvas& c, const lmvlayout::Shape& s, double x, double y, double w, double h) {
  if (!s.visible) return;
  const double cx = x + w / 2.0, cy = y + h / 2.0;
  const bool stroke = has_stroke(s.stroke, s.stroke_width);
  if (s.kind == "cell") return;  // invisible anchor carrier until styled
  if (s.kind == "box" || s.kind == "cluster") {
    if (has_fill(s.fill)) c.draw_round_rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h),
                                            static_cast<float>(s.radius), static_cast<float>(s.radius), fill_paint(s.fill, s.opacity));
    if (stroke) c.draw_round_rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h),
                                  static_cast<float>(s.radius), static_cast<float>(s.radius),
                                  stroke_paint(s.stroke, s.stroke_width, s.opacity, false));
    if (s.kind == "cluster") {
      if (!s.label.empty()) {
        check_label(s.label, "shape label");
        lmv::Font font(static_cast<float>(s.label_size));
        font.embolden(true);
        const std::string color = has_stroke(s.stroke, s.stroke_width) ? s.stroke : "#dbe7f3";
        c.draw_text(s.label, static_cast<float>(cx - font.measure(s.label) / 2.0), static_cast<float>(y + 13.0 + s.label_size), font, fill_paint(color, s.opacity));
      }
    } else {
      centered_label(c, cx, cy, s.label_size, s.label, std::max(0.0, w - 16.0), s.opacity);
    }
    return;
  }
  if (s.kind == "diamond") {
    lmv::Path path;
    path.move_to(static_cast<float>(cx), static_cast<float>(y));
    path.line_to(static_cast<float>(x + w), static_cast<float>(cy));
    path.line_to(static_cast<float>(cx), static_cast<float>(y + h));
    path.line_to(static_cast<float>(x), static_cast<float>(cy));
    path.close();
    if (has_fill(s.fill)) c.draw_path(path, fill_paint(s.fill, s.opacity));
    if (stroke) c.draw_path(path, stroke_paint(s.stroke, s.stroke_width, s.opacity, false));
    centered_label(c, cx, cy, s.label_size, s.label, std::max(0.0, w - 24.0), s.opacity);
    return;
  }
  if (s.kind == "cylinder") {
    if (h <= 20.0) throw lmv::Error("cylinder height must be greater than 20 pixels");
    auto cap = [&](double cap_cy) {
      if (has_fill(s.fill)) c.draw_oval(static_cast<float>(x), static_cast<float>(cap_cy - 10.0), static_cast<float>(w), 20.0f, fill_paint(s.fill, s.opacity));
      if (stroke) c.draw_oval(static_cast<float>(x), static_cast<float>(cap_cy - 10.0), static_cast<float>(w), 20.0f, stroke_paint(s.stroke, s.stroke_width, s.opacity, false));
    };
    // Body first, then the caps over it, mirroring lmdraw's painter order.
    if (has_fill(s.fill)) c.draw_rect(static_cast<float>(x), static_cast<float>(y + 10.0), static_cast<float>(w), static_cast<float>(h - 20.0), fill_paint(s.fill, s.opacity));
    if (stroke) c.draw_rect(static_cast<float>(x), static_cast<float>(y + 10.0), static_cast<float>(w), static_cast<float>(h - 20.0), stroke_paint(s.stroke, s.stroke_width, s.opacity, false));
    cap(y + 10.0);
    cap(y + h - 10.0);
    centered_label(c, cx, cy, s.label_size, s.label, std::max(0.0, w - 16.0), s.opacity);
    return;
  }
  if (s.kind == "note") {
    const double fold = std::min(w, h) * 0.3;
    lmv::Path body;
    body.move_to(static_cast<float>(x), static_cast<float>(y));
    body.line_to(static_cast<float>(x + w - fold), static_cast<float>(y));
    body.line_to(static_cast<float>(x + w), static_cast<float>(y + fold));
    body.line_to(static_cast<float>(x + w), static_cast<float>(y + h));
    body.line_to(static_cast<float>(x), static_cast<float>(y + h));
    body.close();
    if (has_fill(s.fill)) c.draw_path(body, fill_paint(s.fill, s.opacity));
    if (stroke) c.draw_path(body, stroke_paint(s.stroke, s.stroke_width, s.opacity, false));
    if (stroke) {
      lmv::Path crease;
      crease.move_to(static_cast<float>(x + w - fold), static_cast<float>(y));
      crease.line_to(static_cast<float>(x + w - fold), static_cast<float>(y + fold));
      crease.line_to(static_cast<float>(x + w), static_cast<float>(y + fold));
      c.draw_path(crease, stroke_paint(s.stroke, s.stroke_width, s.opacity, false));
    }
    if (!s.label.empty()) {
      check_label(s.label, "shape label");
      const std::string wrapped = wrap_text(s.label, std::max(0.0, w - 20.0), static_cast<float>(s.label_size));
      const auto lines = split_lines(wrapped);
      lmv::Font font(static_cast<float>(s.label_size));
      const auto paint = fill_paint("#dbe7f3", s.opacity);
      for (size_t i = 0; i < lines.size(); ++i)
        c.draw_text(lines[i], static_cast<float>(x + 10.0), static_cast<float>(y + 6.0 + s.label_size + 1.2 * s.label_size * double(i)), font, paint);
    }
    return;
  }
}

// ---- ShapeItem ----

void ShapeItem::draw(lmv::Canvas& canvas) const { render_shape(canvas, *shape, shape->x, shape->y, shape->w, shape->h); }

bool ShapeItem::set_attr(const std::string& name, const std::string& value) {
  return shape_set_attr(*shape, name, value);
}

// ---- ConnectorItem ----

std::pair<double, double> ConnectorItem::from() const { return has_a ? anchor_point(*a, anchor_a) : std::make_pair(x1, y1); }
std::pair<double, double> ConnectorItem::to() const { return has_b ? anchor_point(*b, anchor_b) : std::make_pair(x2, y2); }

void ConnectorItem::draw(lmv::Canvas& c) const {
  if (!visible) return;
  const auto [ax, ay] = from();
  const auto [bx, by] = to();
  std::vector<float> dash_list = parse_dash(dash);
  auto paint = stroke_paint(color, width, opacity);
  if (!dash_list.empty()) paint.dash_list(dash_list, static_cast<float>(dash_offset));
  double end_dir_x = bx - ax, end_dir_y = by - ay;
  double start_dir_x = end_dir_x, start_dir_y = end_dir_y;
  std::pair<double, double> label_at{ax + (bx - ax) * label_t, ay + (by - ay) * label_t};
  if (routing == "straight" || (ax == bx || ay == by)) {
    c.draw_line(static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(bx), static_cast<float>(by), paint);
  } else if (routing == "elbow") {
    std::vector<std::pair<double, double>> pts;
    if (std::abs(bx - ax) >= std::abs(by - ay)) {
      const double mx = (ax + bx) / 2.0;
      pts = {{ax, ay}, {mx, ay}, {mx, by}, {bx, by}};
    } else {
      const double my = (ay + by) / 2.0;
      pts = {{ax, ay}, {ax, my}, {bx, my}, {bx, by}};
    }
    lmv::Path path;
    path.move_to(static_cast<float>(pts[0].first), static_cast<float>(pts[0].second));
    for (size_t i = 1; i < pts.size(); ++i) path.line_to(static_cast<float>(pts[i].first), static_cast<float>(pts[i].second));
    c.draw_path(path, paint);
    start_dir_x = pts[1].first - pts[0].first; start_dir_y = pts[1].second - pts[0].second;
    end_dir_x = pts.back().first - pts[pts.size() - 2].first; end_dir_y = pts.back().second - pts[pts.size() - 2].second;
    label_at = polyline_point(pts, label_t);
  } else {  // curve: cubic S-curve along the dominant axis
    double c1x, c1y, c2x, c2y;
    if (std::abs(bx - ax) >= std::abs(by - ay)) {
      c1x = ax + (bx - ax) / 2.0; c1y = ay;
      c2x = bx - (bx - ax) / 2.0; c2y = by;
    } else {
      c1x = ax; c1y = ay + (by - ay) / 2.0;
      c2x = bx; c2y = by - (by - ay) / 2.0;
    }
    lmv::Path path;
    path.move_to(static_cast<float>(ax), static_cast<float>(ay));
    path.cubic_to(static_cast<float>(c1x), static_cast<float>(c1y), static_cast<float>(c2x), static_cast<float>(c2y),
                  static_cast<float>(bx), static_cast<float>(by));
    c.draw_path(path, paint);
    start_dir_x = c1x - ax; start_dir_y = c1y - ay;
    end_dir_x = bx - c2x; end_dir_y = by - c2y;
    const double t = label_t, u = 1.0 - t;
    label_at = {u * u * u * ax + 3.0 * u * u * t * c1x + 3.0 * u * t * t * c2x + t * t * t * bx,
                u * u * u * ay + 3.0 * u * u * t * c1y + 3.0 * u * t * t * c2y + t * t * t * by};
  }
  if (end_marker != "none") marker(c, end_marker, bx, by, end_dir_x, end_dir_y, color, width, opacity);
  if (start_marker != "none") marker(c, start_marker, ax, ay, start_dir_x, start_dir_y, color, width, opacity);
  if (!label.empty()) {
    lmv::Font font(13.0f);
    c.draw_text(label, static_cast<float>(label_at.first + label_dx - font.measure(label) / 2.0),
                static_cast<float>(label_at.second + label_dy), font, fill_paint(color, opacity));
  }
}

bool ConnectorItem::set_attr(const std::string& name, const std::string& value) {
  if (common_attr(visible, opacity, name, value)) return true;
  if (name == "stroke") { (void)lmv::parse_color(value); color = value; return true; }
  if (name == "stroke-width") {
    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v) || v <= 0 || v > 256) throw lmv::Error("stroke-width must be in (0, 256]");
    width = v;
    return true;
  }
  if (name == "stroke-dasharray") { (void)parse_dash(value); dash = value; return true; }
  if (name == "stroke-dashoffset") {
    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) throw lmv::Error("stroke-dashoffset must be a finite pixel length");
    dash_offset = v;
    return true;
  }
  if (name == "marker-start" || name == "marker-end") {
    if (value != "none" && value != "arrow" && value != "dot" && value != "diamond")
      throw lmv::Error("markers must be none, arrow, dot, or diamond");
    if (name == "marker-start") start_marker = value;
    else end_marker = value;
    return true;
  }
  if (name == "label") { check_label(value, "connector label"); label = value; return true; }
  if (name == "fill") return true;  // accepted for parity with group styling; edges do not fill
  return false;
}

// ---- sequence items ----

void LifelineItem::draw(lmv::Canvas& c) const {
  if (!visible) return;
  auto paint = stroke_paint(color, 1.5, opacity);
  paint.dash(5.0f, 6.0f);
  c.draw_line(static_cast<float>(x), static_cast<float>(y0), static_cast<float>(x), static_cast<float>(y1), paint);
}

bool LifelineItem::set_attr(const std::string& name, const std::string& value) {
  if (common_attr(visible, opacity, name, value)) return true;
  if (name == "stroke") { (void)lmv::parse_color(value); color = value; return true; }
  if (name == "fill") return true;
  return false;
}

void MessageItem::draw(lmv::Canvas& c) const {
  if (!visible) return;
  auto paint = stroke_paint(color, 2.0, opacity);
  if (dashed) paint.dash(6.0f, 5.0f);
  c.draw_line(static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2), paint);
  marker(c, "arrow", x2, y2, x2 - x1, y2 - y1, color, 2.0, opacity);
  if (!label.empty()) {
    lmv::Font font(13.0f);
    c.draw_text(label, static_cast<float>((x1 + x2) / 2.0 - font.measure(label) / 2.0),
                static_cast<float>((y1 + y2) / 2.0 - 7.0), font, fill_paint("#dbe7f3", opacity));
  }
}

bool MessageItem::set_attr(const std::string& name, const std::string& value) {
  if (common_attr(visible, opacity, name, value)) return true;
  if (name == "stroke") { (void)lmv::parse_color(value); color = value; return true; }
  if (name == "fill") return true;
  return false;
}

void ActivationItem::draw(lmv::Canvas& c) const {
  if (!visible) return;
  if (has_fill(fill)) c.draw_rect(static_cast<float>(x - 4.0), static_cast<float>(y0), 8.0f, static_cast<float>(y1 - y0), fill_paint(fill, opacity));
  if (has_stroke(stroke, 1.0)) c.draw_rect(static_cast<float>(x - 4.0), static_cast<float>(y0), 8.0f, static_cast<float>(y1 - y0), stroke_paint(stroke, 1.0, opacity, false));
}

bool ActivationItem::set_attr(const std::string& name, const std::string& value) {
  if (common_attr(visible, opacity, name, value)) return true;
  if (name == "fill") { (void)shape_fill(value); fill = value; return true; }
  if (name == "stroke") { validate_color_or_none(value, "stroke"); stroke = value; return true; }
  return false;
}

void SelfMessageItem::draw(lmv::Canvas& c) const {
  if (!visible) return;
  lmv::Path path;
  path.move_to(static_cast<float>(x), static_cast<float>(y));
  path.line_to(static_cast<float>(x + 60.0), static_cast<float>(y));
  path.line_to(static_cast<float>(x + 60.0), static_cast<float>(y + 26.0));
  path.line_to(static_cast<float>(x + 3.0), static_cast<float>(y + 26.0));
  auto paint = stroke_paint(color, 2.0, opacity);
  c.draw_path(path, paint);
  marker(c, "arrow", x + 3.0, y + 26.0, -1.0, 0.0, color, 2.0, opacity);
  if (!label.empty()) {
    lmv::Font font(13.0f);
    c.draw_text(label, static_cast<float>(x + 70.0), static_cast<float>(y + 18.0), font, fill_paint("#dbe7f3", opacity));
  }
}

bool SelfMessageItem::set_attr(const std::string& name, const std::string& value) {
  if (common_attr(visible, opacity, name, value)) return true;
  if (name == "stroke") { (void)lmv::parse_color(value); color = value; return true; }
  if (name == "fill") return true;
  return false;
}

// ---- GroupItem ----

void GroupItem::draw(lmv::Canvas& canvas) const {
  if (!visible) return;
  for (const auto* child : children) child->draw(canvas);
}

bool GroupItem::set_attr(const std::string& name, const std::string& value) {
  if (common_attr(visible, opacity, name, value)) return true;
  for (auto* child : children)
    if (child->set_attr(name, value)) return true;
  return false;
}

// ---- Layer ----

bool Layer::erase_root(Item* item) {
  const auto it = std::find(items.begin(), items.end(), item);
  if (it == items.end()) return false;
  items.erase(it);
  return true;
}

void Layer::draw(lmv::Canvas& canvas) const {
  for (const auto* item : items) item->draw(canvas);
}

}  // namespace lmvdiagram
