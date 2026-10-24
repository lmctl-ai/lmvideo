// Phase 2 test: frames -> video through the ffmpeg CLI, verified with ffprobe and by decoding frames back. Offline, deterministic
// facts only (frame count, duration, size, codec, lossless pixel equality); encoded bytes are never compared.
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <signal.h>
#include <string>
#include <sys/stat.h>
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

  // Finding a: SIGPIPE disposition unchanged and pipe error surfaced as Error
  struct sigaction sa_before;
  sigaction(SIGPIPE, nullptr, &sa_before);
  {
    lmv::Encoder enc(tmp("sig_test.mp4"), W, H);
    enc.add_frame(make_frame(0).rgba());
    enc.finish();
    unlink(tmp("sig_test.mp4").c_str());
  }
  struct sigaction sa_after;
  sigaction(SIGPIPE, nullptr, &sa_after);
  CHECK(sa_before.sa_handler == sa_after.sa_handler, "SIGPIPE process disposition is not modified to SIG_IGN");

  bool sigpipe_error_caught = false;
  try {
    lmv::EncoderOptions fast_die;
    fast_die.ffmpeg = "false";
    lmv::Encoder enc(tmp("die.mp4"), W, H, fast_die);
    usleep(50000);
    for (int i = 0; i < 50; ++i) {
      enc.add_frame(make_frame(0).rgba());
    }
  } catch (const lmv::Error&) {
    sigpipe_error_caught = true;
  }
  CHECK(sigpipe_error_caught, "EPIPE becomes an Error with message without killing the process");

  // Finding b: atomic encode into temp sibling; no partial files on error; existing file untouched
  std::string zero_file = tmp("zero.mp4");
  unlink(zero_file.c_str());
  try {
    lmv::Encoder enc(zero_file, W, H);
    enc.finish();
  } catch (const lmv::Error&) {}
  CHECK(access(zero_file.c_str(), F_OK) != 0, "finish() with zero frames leaves no output file");

  std::string abort_file = tmp("abort.mp4");
  unlink(abort_file.c_str());
  {
    lmv::Encoder enc(abort_file, W, H);
    enc.add_frame(make_frame(0).rgba());
    // Destructor unlinks temporary sibling
  }
  CHECK(access(abort_file.c_str(), F_OK) != 0, "aborted encoder destructor leaves no output file");

  std::string exist_file = tmp("exist.mp4");
  {
    lmv::Encoder enc(exist_file, W, H);
    enc.add_frame(make_frame(0).rgba());
    enc.finish();
  }
  long original_size = 0;
  {
    std::ifstream in(exist_file, std::ios::binary | std::ios::ate);
    original_size = in.tellg();
  }
  CHECK(original_size > 0, "existing file created with non-zero size");
  try {
    lmv::Encoder enc(exist_file, W, H);
    enc.finish();
  } catch (const lmv::Error&) {}
  long after_zero_size = 0;
  {
    std::ifstream in(exist_file, std::ios::binary | std::ios::ate);
    after_zero_size = in.tellg();
  }
  CHECK(after_zero_size == original_size, "existing file untouched when new encode finishes with zero frames");

  {
    lmv::Encoder enc(exist_file, W, H);
    enc.add_frame(make_frame(1).rgba());
  }
  long after_abort_size = 0;
  {
    std::ifstream in(exist_file, std::ios::binary | std::ios::ate);
    after_abort_size = in.tellg();
  }
  CHECK(after_abort_size == original_size, "existing file untouched when new encode is aborted");
  unlink(exist_file.c_str());

  // Finding c: no fd leaks on failing constructions
  auto count_open_fds = []() -> int {
    int count = 0;
#if defined(__APPLE__)
    DIR* d = opendir("/dev/fd");
#else
    DIR* d = opendir("/proc/self/fd");
#endif
    if (!d) return -1;
    while (struct dirent* de = readdir(d)) {
      if (de->d_name[0] != '.') ++count;
    }
    closedir(d);
    return count;
  };
  int fds_before = count_open_fds();
  for (int i = 0; i < 20; ++i) {
    try { lmv::Encoder e("", W, H); } catch (...) {}
    try { lmv::Encoder e(tmp("odd.mp4"), 63, H); } catch (...) {}
    try { lmv::Encoder e(tmp("bad.xyz"), W, H); } catch (...) {}
    setenv("TMPDIR", "/nonexistent_dir_for_test", 1);
    try { lmv::Encoder e(tmp("fail.mp4"), W, H); } catch (...) {}
    unsetenv("TMPDIR");
  }
  int fds_after = count_open_fds();
  CHECK(fds_before > 0 && fds_before == fds_after, "no fd leaks across 20 failing constructor invocations");

  // Finding d: leading-dash paths
  std::string dash_file = "-dash_out.mp4";
  unlink(("./" + dash_file).c_str());
  {
    lmv::Encoder enc(dash_file, W, H);
    for (int i = 0; i < N; ++i) enc.add_frame(make_frame(i).rgba());
    enc.finish();
  }
  CHECK(access(("./" + dash_file).c_str(), F_OK) == 0, "video created with leading dash filename '-dash_out.mp4'");
  lmv::VideoInfo dash_info = lmv::probe_video(dash_file);
  CHECK(dash_info.frames == N, "probe_video succeeds with leading dash path");
  std::string dash_f0 = lmv::decode_frame(dash_file, 0, W, H);
  CHECK(dash_f0.size() == static_cast<size_t>(W) * H * 4, "decode_frame succeeds with leading dash path");
  unlink(("./" + dash_file).c_str());

  // Finding e: probe_video rejects malformed ffprobe output
  auto make_fake_probe = [](const std::string& script_name, const std::string& output) -> std::string {
    std::string path = tmp(script_name.c_str());
    std::ofstream out(path);
    out << "#!/bin/sh\ncat << 'EOF'\n" << output << "\nEOF\n";
    out.close();
    chmod(path.c_str(), 0755);
    return path;
  };
  auto throws_with = [](auto&& fn, const std::string& substr) -> bool {
    try {
      fn();
    } catch (const lmv::Error& e) {
      return std::string(e.what()).find(substr) != std::string::npos;
    }
    return false;
  };

  std::string p_w = make_fake_probe("p_w.sh", "codec_name=h264\nwidth=not-a-number\nheight=48\nnb_read_frames=10\nduration=1.0\navg_frame_rate=10/1\n");
  CHECK(throws_with([&] { lmv::probe_video(mkv, p_w); }, "width"), "probe_video rejects width=not-a-number naming field");
  unlink(p_w.c_str());

  std::string p_h = make_fake_probe("p_h.sh", "codec_name=h264\nwidth=64\nheight=-7\nnb_read_frames=10\nduration=1.0\navg_frame_rate=10/1\n");
  CHECK(throws_with([&] { lmv::probe_video(mkv, p_h); }, "height"), "probe_video rejects height=-7 naming field");
  unlink(p_h.c_str());

  std::string p_f = make_fake_probe("p_f.sh", "codec_name=h264\nwidth=64\nheight=48\nnb_read_frames=N/A\nduration=1.0\navg_frame_rate=10/1\n");
  CHECK(throws_with([&] { lmv::probe_video(mkv, p_f); }, "nb_read_frames"), "probe_video rejects nb_read_frames=N/A naming field");
  unlink(p_f.c_str());

  std::string p_d = make_fake_probe("p_d.sh", "codec_name=h264\nwidth=64\nheight=48\nnb_read_frames=10\nduration=invalid\navg_frame_rate=10/1\n");
  CHECK(throws_with([&] { lmv::probe_video(mkv, p_d); }, "duration"), "probe_video rejects invalid duration naming field");
  unlink(p_d.c_str());

  std::string p_r = make_fake_probe("p_r.sh", "codec_name=h264\nwidth=64\nheight=48\nnb_read_frames=10\nduration=1.0\navg_frame_rate=1/0\n");
  CHECK(throws_with([&] { lmv::probe_video(mkv, p_r); }, "avg_frame_rate"), "probe_video rejects avg_frame_rate=1/0 naming field");
  unlink(p_r.c_str());

  std::string p_c = make_fake_probe("p_c.sh", "width=64\nheight=48\nnb_read_frames=10\nduration=1.0\navg_frame_rate=10/1\n");
  CHECK(throws_with([&] { lmv::probe_video(mkv, p_c); }, "codec_name"), "probe_video rejects missing codec_name naming field");
  unlink(p_c.c_str());

  unlink(mkv.c_str());
  unlink(mp4.c_str());
  printf("%s %d checks\n", failures ? "FAIL" : "PASS", checks);
  return failures ? 1 : 0;
}
