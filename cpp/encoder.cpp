#include "encoder.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace lmv {
namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

std::string extension(const std::string& path) {
  size_t dot = path.rfind('.');
  if (dot == std::string::npos) return "";
  std::string e = path.substr(dot);
  for (char& c : e) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return e;
}

std::string protect_path(const std::string& path) {
  if (!path.empty() && path[0] == '-') {
    return "./" + path;
  }
  return path;
}

std::string make_temp_sibling(const std::string& path) {
  size_t slash = path.rfind('/');
  std::string dir = (slash == std::string::npos) ? "." : path.substr(0, slash);
  std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
  static std::atomic<uint64_t> counter{0};
  return dir + "/.lmv-" + std::to_string(getpid()) + "-" + std::to_string(++counter) + "-" + base;
}

class ScopedFd {
 public:
  ScopedFd() : fd_(-1) {}
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() { reset(); }
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  ScopedFd(ScopedFd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
  ScopedFd& operator=(ScopedFd&& o) noexcept {
    if (this != &o) {
      reset();
      fd_ = o.fd_;
      o.fd_ = -1;
    }
    return *this;
  }
  int get() const { return fd_; }
  int release() {
    int f = fd_;
    fd_ = -1;
    return f;
  }
  void reset(int fd = -1) {
    if (fd_ >= 0) close(fd_);
    fd_ = fd;
  }
 private:
  int fd_ = -1;
};

// fork/exec without a shell (no quoting problems). stdin is `in_fd` (or /dev/null), stdout is `out_fd` (or /dev/null),
// stderr goes to `err_path`. Returns the pid.
pid_t spawn(const std::vector<std::string>& argv, int in_fd, int out_fd, const std::string& err_path) {
  std::vector<char*> args;
  for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid = fork();
  if (pid < 0) throw Error(std::string("fork failed: ") + strerror(errno));
  if (pid == 0) {
    int devnull = open("/dev/null", O_RDWR);
    dup2(in_fd >= 0 ? in_fd : devnull, 0);
    dup2(out_fd >= 0 ? out_fd : devnull, 1);
    int err = open(err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    dup2(err >= 0 ? err : devnull, 2);
    for (int fd = 3; fd < 256; ++fd) close(fd);
    execvp(args[0], args.data());
    _exit(127);
  }
  return pid;
}

int wait_exit(pid_t pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

std::string temp_path(const char* stem) {
  std::string tmpl = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/" + stem + "-XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  int fd = mkstemp(buf.data());
  if (fd < 0) throw Error("cannot create a temporary file");
  close(fd);
  return buf.data();
}

// Runs a command, returns its stdout; throws with stderr text when the exit code is not 0.
std::string run_capture(const std::vector<std::string>& argv, const char* what) {
  std::string out_path = temp_path("lmv-out"), err_path = temp_path("lmv-err");
  ScopedFd out_fd(open(out_path.c_str(), O_WRONLY | O_TRUNC));
  pid_t pid = spawn(argv, -1, out_fd.get(), err_path);
  out_fd.reset();
  int code = wait_exit(pid);
  std::string out = slurp(out_path), err = trim(slurp(err_path));
  unlink(out_path.c_str());
  unlink(err_path.c_str());
  if (code == 127) throw Error(std::string(what) + " not found (is it installed and in PATH?): " + argv[0]);
  if (code != 0) throw Error(std::string(what) + " failed (exit " + std::to_string(code) + "): " + err);
  return out;
}

int parse_strict_int(const std::string& s, const std::string& field, bool positive) {
  if (s.empty()) throw Error("probe_video: empty " + field);
  size_t pos = 0;
  int val = 0;
  try {
    val = std::stoi(s, &pos);
  } catch (...) {
    throw Error("probe_video: invalid integer for " + field + ": '" + s + "'");
  }
  if (pos != s.size()) {
    throw Error("probe_video: trailing characters in " + field + ": '" + s + "'");
  }
  if (positive && val <= 0) {
    throw Error("probe_video: non-positive " + field + ": " + std::to_string(val));
  }
  if (!positive && val < 0) {
    throw Error("probe_video: negative " + field + ": " + std::to_string(val));
  }
  return val;
}

double parse_strict_double(const std::string& s, const std::string& field) {
  if (s.empty()) throw Error("probe_video: empty " + field);
  size_t pos = 0;
  double val = 0.0;
  try {
    val = std::stod(s, &pos);
  } catch (...) {
    throw Error("probe_video: invalid number for " + field + ": '" + s + "'");
  }
  if (pos != s.size()) {
    throw Error("probe_video: trailing characters in " + field + ": '" + s + "'");
  }
  if (!std::isfinite(val)) {
    throw Error("probe_video: non-finite " + field);
  }
  if (val < 0.0) {
    throw Error("probe_video: negative " + field + ": " + std::to_string(val));
  }
  return val;
}

}  // namespace

Encoder::Encoder(const std::string& path, int width, int height, const EncoderOptions& o)
    : width_(width), height_(height) {
  if (width < 2 || height < 2 || width > 16384 || height > 16384) throw Error("video size must be 2..16384 per side");
  if (width % 2 || height % 2) throw Error("video width and height must be even (yuv420p)");
  if (o.fps < 1 || o.fps > 240) throw Error("fps must be 1..240");
  if (o.crf < 0 || o.crf > 51) throw Error("crf must be 0..51");
  if (path.empty()) throw Error("output path is empty");

  path_ = protect_path(path);
  temp_out_path_ = make_temp_sibling(path_);

  std::string ext = extension(path_);
  std::string codec = o.codec;
  if (codec.empty()) {
    static const std::map<std::string, std::string> by_ext = {
        {".mp4", "libx264"}, {".webm", "libvpx-vp9"}, {".mkv", "ffv1"}, {".gif", "gif"}};
    auto it = by_ext.find(ext);
    if (it == by_ext.end()) throw Error("cannot choose a codec for extension '" + ext + "' (use .mp4, .webm, .mkv, .gif)");
    codec = it->second;
  }

  // Create log file first, with RAII cleanup on failure during construction.
  log_path_ = temp_path("lmv-ffmpeg");
  struct ConstructionCleanup {
    std::string& log;
    std::string& temp_out;
    bool active = true;
    ~ConstructionCleanup() {
      if (active) {
        if (!log.empty()) { unlink(log.c_str()); log.clear(); }
        if (!temp_out.empty()) { unlink(temp_out.c_str()); temp_out.clear(); }
      }
    }
  } cleanup{log_path_, temp_out_path_};

  std::vector<std::string> argv = {o.ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba",
                                   "-s", std::to_string(width) + "x" + std::to_string(height), "-framerate", std::to_string(o.fps),
                                   "-i", "-", "-c:v", codec};
  if (codec == "libx264" || codec == "libvpx-vp9") {
    argv.insert(argv.end(), {"-crf", std::to_string(o.crf)});
    if (codec == "libvpx-vp9") argv.insert(argv.end(), {"-b:v", "0"});
  }
  if (codec == "libx264" || codec == "libvpx-vp9") argv.insert(argv.end(), {"-pix_fmt", "yuv420p"});
  if (ext == ".mp4") argv.insert(argv.end(), {"-movflags", "+faststart"});
  argv.push_back(temp_out_path_);

  int fds[2];
  if (pipe(fds) != 0) throw Error(std::string("pipe failed: ") + strerror(errno));
  ScopedFd read_fd(fds[0]);
  ScopedFd write_fd(fds[1]);

  pid_ = spawn(argv, read_fd.get(), -1, log_path_);
  read_fd.reset();
  pipe_fd_ = write_fd.release();
  cleanup.active = false;
}

Encoder::~Encoder() {
  if (!finished_) abort_quietly();
}

void Encoder::abort_quietly() {
  if (pipe_fd_ >= 0) close(pipe_fd_);
  pipe_fd_ = -1;
  if (pid_ > 0) wait_exit(pid_);
  pid_ = -1;
  if (!log_path_.empty()) {
    unlink(log_path_.c_str());
    log_path_.clear();
  }
  if (!temp_out_path_.empty()) {
    unlink(temp_out_path_.c_str());
    temp_out_path_.clear();
  }
  finished_ = true;
}

void Encoder::add_frame(const std::string& rgba) {
  if (finished_) throw Error("encoder is already finished");
  size_t want = static_cast<size_t>(width_) * height_ * 4;
  if (rgba.size() != want) throw Error("frame has " + std::to_string(rgba.size()) + " bytes, expected " + std::to_string(want));
  const char* p = rgba.data();
  size_t left = want;
  while (left > 0) {
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &mask, &oldmask);

    sigset_t pending_before;
    sigpending(&pending_before);
    bool was_pending = sigismember(&pending_before, SIGPIPE);

    ssize_t n = write(pipe_fd_, p, left);
    int saved_errno = errno;

    if (!was_pending) {
      struct timespec ts = {0, 0};
      siginfo_t info;
      while (sigtimedwait(&mask, &info, &ts) == SIGPIPE) {
      }
    }

    pthread_sigmask(SIG_SETMASK, &oldmask, nullptr);
    errno = saved_errno;

    if (n < 0) {
      if (errno == EINTR) continue;
      std::string msg = trim(slurp(log_path_));
      abort_quietly();
      throw Error("ffmpeg stopped reading frames" + (msg.empty() ? std::string() : ": " + msg));
    }
    p += n;
    left -= static_cast<size_t>(n);
  }
  ++frames_;
}

void Encoder::finish() {
  if (finished_) return;
  close(pipe_fd_);
  pipe_fd_ = -1;
  int code = wait_exit(pid_);
  pid_ = -1;
  std::string msg = trim(slurp(log_path_));
  unlink(log_path_.c_str());
  log_path_.clear();
  finished_ = true;

  if (code == 127) {
    if (!temp_out_path_.empty()) {
      unlink(temp_out_path_.c_str());
      temp_out_path_.clear();
    }
    throw Error("ffmpeg not found (is it installed and in PATH?)");
  }
  if (code != 0) {
    if (!temp_out_path_.empty()) {
      unlink(temp_out_path_.c_str());
      temp_out_path_.clear();
    }
    throw Error("ffmpeg failed (exit " + std::to_string(code) + "): " + msg);
  }
  if (frames_ == 0) {
    if (!temp_out_path_.empty()) {
      unlink(temp_out_path_.c_str());
      temp_out_path_.clear();
    }
    throw Error("no frames were added");
  }

  if (rename(temp_out_path_.c_str(), path_.c_str()) != 0) {
    std::string err = strerror(errno);
    unlink(temp_out_path_.c_str());
    temp_out_path_.clear();
    throw Error("cannot rename temporary output file to " + path_ + ": " + err);
  }
  temp_out_path_.clear();
}

VideoInfo probe_video(const std::string& path, const std::string& ffprobe) {
  std::string safe_path = protect_path(path);
  if (access(safe_path.c_str(), R_OK) != 0) throw Error("cannot read video file: " + path);
  std::string out = run_capture({ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                                 "stream=codec_name,width,height,nb_read_frames,avg_frame_rate:format=duration", "-of",
                                 "default=noprint_wrappers=1", safe_path},
                                "ffprobe");
  std::map<std::string, std::string> kv;
  std::istringstream lines(out);
  for (std::string line; std::getline(lines, line);) {
    size_t eq = line.find('=');
    if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
  }

  auto it_codec = kv.find("codec_name");
  if (it_codec == kv.end() || it_codec->second.empty()) {
    throw Error("probe_video: missing or empty codec_name in " + path);
  }
  auto it_w = kv.find("width");
  if (it_w == kv.end()) throw Error("probe_video: missing width in " + path);
  auto it_h = kv.find("height");
  if (it_h == kv.end()) throw Error("probe_video: missing height in " + path);
  auto it_f = kv.find("nb_read_frames");
  if (it_f == kv.end()) throw Error("probe_video: missing nb_read_frames in " + path);
  auto it_d = kv.find("duration");
  if (it_d == kv.end()) throw Error("probe_video: missing duration in " + path);

  VideoInfo v;
  v.codec = it_codec->second;
  v.width = parse_strict_int(it_w->second, "width", /*positive=*/true);
  v.height = parse_strict_int(it_h->second, "height", /*positive=*/true);
  v.frames = parse_strict_int(it_f->second, "nb_read_frames", /*positive=*/false);
  v.duration = parse_strict_double(it_d->second, "duration");

  auto it_r = kv.find("avg_frame_rate");
  if (it_r != kv.end() && !it_r->second.empty()) {
    const std::string& r = it_r->second;
    size_t slash = r.find('/');
    if (slash == std::string::npos) {
      throw Error("probe_video: invalid avg_frame_rate format in " + path + ": " + r);
    }
    int num = parse_strict_int(r.substr(0, slash), "avg_frame_rate", /*positive=*/false);
    int den = parse_strict_int(r.substr(slash + 1), "avg_frame_rate", /*positive=*/true);
    v.fps = static_cast<double>(num) / den;
    if (!std::isfinite(v.fps)) {
      throw Error("probe_video: non-finite avg_frame_rate in " + path + ": " + r);
    }
  }
  return v;
}

std::string decode_frame(const std::string& path, int index, int width, int height, const std::string& ffmpeg) {
  std::string safe_path = protect_path(path);
  std::string out = run_capture({ffmpeg, "-v", "error", "-i", safe_path, "-vf", "select=eq(n\\," + std::to_string(index) + ")", "-vframes", "1",
                                 "-f", "rawvideo", "-pix_fmt", "rgba", "-"},
                                "ffmpeg");
  size_t want = static_cast<size_t>(width) * height * 4;
  if (out.size() != want) throw Error("decoded frame " + std::to_string(index) + " has " + std::to_string(out.size()) + " bytes, expected " + std::to_string(want));
  return out;
}

}  // namespace lmv
