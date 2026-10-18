// Phase 2 test: frames -> video through the ffmpeg CLI, verified with ffprobe and by decoding frames back. Offline, deterministic
// facts only (frame count, duration, size, codec, lossless pixel equality); encoded bytes are never compared.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "encoder.hpp"
#include "lmvideo.hpp"

static int checks = 0, failures = 0;
#define CHECK(cond, what)                                                          \
  do {                                                                             \
    ++checks;                                                                      \
    if (cond) printf("ok %d - %s\n", checks, what);                                \
    else { ++failures; printf("not ok %d - %s   [%s:%d]\n", checks, what, __FILE__, __LINE__); } \
  } while (0)

static const int W = 64, H = 48, N = 10;

// Frame i: white background, a red 16x16 square whose left edge is at 4*i, so every frame is distinct and checkable.
static lmv::Surface make_frame(int i) {
  lmv::Surface s(W, H);
  s.canvas().clear(lmv::parse_color("#ffffff"));
  lmv::Paint red;
  red.color(lmv::parse_color("#ff0000")).antialias(false);
  s.canvas().draw_rect(4 * i, 16, 16, 16, red);
  return s;
}

static bool throws(void (*fn)()) {
  try { fn(); } catch (const lmv::Error&) { return true; }
  return false;
}

static std::string tmp(const char* name) {
  const char* d = getenv("TMPDIR");
  return std::string(d ? d : "/tmp") + "/lmv-enc-" + std::to_string(getpid()) + "-" + name;
}

static bool px(const std::string& rgba, int x, int y, int r, int g, int b, int tol) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(rgba.data()) + (static_cast<size_t>(y) * W + x) * 4;
  return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol;
}

int main() {
  // Lossless FFV1 in Matroska: decoded frames must equal the source frames exactly.
  std::string mkv = tmp("lossless.mkv");
  {
    lmv::EncoderOptions o; o.fps = 10;
    lmv::Encoder enc(mkv, W, H, o);
    for (int i = 0; i < N; ++i) enc.add_frame(make_frame(i).rgba());
    enc.finish();
    CHECK(enc.frames() == N, "encoder counted 10 frames");
  }
  lmv::VideoInfo info = lmv::probe_video(mkv);
  CHECK(info.codec == "ffv1" && info.width == W && info.height == H, "mkv: codec ffv1, 64x48");
  CHECK(info.frames == N, "mkv: ffprobe counts 10 decoded frames");
  CHECK(info.duration > 0.99 && info.duration < 1.01 && info.fps == 10.0, "mkv: duration 1.0 s at 10 fps");
  bool exact = true;
  for (int i : {0, 5, 9}) exact = exact && lmv::decode_frame(mkv, i, W, H) == make_frame(i).rgba();
  CHECK(exact, "mkv: frames 0, 5 and 9 decode bit-identical to the drawn frames (lossless)");

  // H.264 in MP4: lossy, so probes with tolerance.
  std::string mp4 = tmp("video.mp4");
  {
    lmv::EncoderOptions o; o.fps = 10; o.crf = 10;
    lmv::Encoder enc(mp4, W, H, o);
    for (int i = 0; i < N; ++i) enc.add_frame(make_frame(i).rgba());
    enc.finish();
  }
  info = lmv::probe_video(mp4);
  CHECK(info.codec == "h264" && info.width == W && info.height == H, "mp4: codec h264, 64x48");
  CHECK(info.frames == N, "mp4: ffprobe counts 10 frames");
  CHECK(info.duration > 0.99 && info.duration < 1.05, "mp4: duration about 1.0 s");
  std::string f7 = lmv::decode_frame(mp4, 7, W, H);
  CHECK(px(f7, 28 + 8, 24, 255, 0, 0, 40) && px(f7, 2, 4, 255, 255, 255, 25) && px(f7, 20, 24, 255, 255, 255, 40),
        "mp4: frame 7 has the red square at x 28..44 and a white background (tolerance 40)");

  // Error paths.
  CHECK(throws([] { lmv::Encoder e(tmp("odd.mp4"), 63, 48); }), "odd width is refused");
  CHECK(throws([] { lmv::Encoder e(tmp("x.xyz"), 64, 48); }), "unknown extension is refused");
  CHECK(throws([] { lmv::Encoder e(tmp("o.mp4"), 64, 48); e.add_frame("short"); }), "wrong frame size is refused");
  CHECK(throws([] { lmv::Encoder e(tmp("e.mp4"), 64, 48); e.finish(); }), "finishing without frames is an error");
  CHECK(throws([] { lmv::probe_video("/nonexistent/video.mp4"); }), "probing a missing file is an error");
  lmv::EncoderOptions bad; bad.ffmpeg = "ffmpeg-does-not-exist";
  bool missing = false;
  try { lmv::Encoder e(tmp("m.mp4"), 64, 48, bad); e.add_frame(make_frame(0).rgba()); e.finish(); }
  catch (const lmv::Error& err) { missing = std::string(err.what()).find("not found") != std::string::npos || std::string(err.what()).find("stopped") != std::string::npos; }
  CHECK(missing, "a missing ffmpeg executable gives a clear error");

  unlink(mkv.c_str());
  unlink(mp4.c_str());
  printf("%s %d checks\n", failures ? "FAIL" : "PASS", checks);
  return failures ? 1 : 0;
}
