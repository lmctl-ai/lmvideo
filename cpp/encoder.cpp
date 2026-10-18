#include "encoder.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
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
  int out_fd = open(out_path.c_str(), O_WRONLY | O_TRUNC);
  pid_t pid = spawn(argv, -1, out_fd, err_path);
  close(out_fd);
  int code = wait_exit(pid);
  std::string out = slurp(out_path), err = trim(slurp(err_path));
  unlink(out_path.c_str());
  unlink(err_path.c_str());
  if (code == 127) throw Error(std::string(what) + " not found (is it installed and in PATH?): " + argv[0]);
  if (code != 0) throw Error(std::string(what) + " failed (exit " + std::to_string(code) + "): " + err);
  return out;
}

}  // namespace

Encoder::Encoder(const std::string& path, int width, int height, const EncoderOptions& o)
    : width_(width), height_(height) {
  if (width < 2 || height < 2 || width > 16384 || height > 16384) throw Error("video size must be 2..16384 per side");
  if (width % 2 || height % 2) throw Error("video width and height must be even (yuv420p)");
  if (o.fps < 1 || o.fps > 240) throw Error("fps must be 1..240");
  if (o.crf < 0 || o.crf > 51) throw Error("crf must be 0..51");
  if (path.empty()) throw Error("output path is empty");
  std::string ext = extension(path);
  std::string codec = o.codec;
  if (codec.empty()) {
    static const std::map<std::string, std::string> by_ext = {
        {".mp4", "libx264"}, {".webm", "libvpx-vp9"}, {".mkv", "ffv1"}, {".gif", "gif"}};
    auto it = by_ext.find(ext);
    if (it == by_ext.end()) throw Error("cannot choose a codec for extension '" + ext + "' (use .mp4, .webm, .mkv, .gif)");
    codec = it->second;
  }
  signal(SIGPIPE, SIG_IGN);  // a dying ffmpeg must surface as a write error with its message, not kill the host

  std::vector<std::string> argv = {o.ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba",
                                   "-s", std::to_string(width) + "x" + std::to_string(height), "-framerate", std::to_string(o.fps),
                                   "-i", "-", "-c:v", codec};
  if (codec == "libx264" || codec == "libvpx-vp9") {
    argv.insert(argv.end(), {"-crf", std::to_string(o.crf)});
    if (codec == "libvpx-vp9") argv.insert(argv.end(), {"-b:v", "0"});
  }
  if (codec == "libx264" || codec == "libvpx-vp9") argv.insert(argv.end(), {"-pix_fmt", "yuv420p"});
  if (ext == ".mp4") argv.insert(argv.end(), {"-movflags", "+faststart"});
  argv.push_back(path);

  int fds[2];
  if (pipe(fds) != 0) throw Error(std::string("pipe failed: ") + strerror(errno));
  log_path_ = temp_path("lmv-ffmpeg");
  try {
    pid_ = spawn(argv, fds[0], -1, log_path_);
  } catch (...) {
    close(fds[0]);
    close(fds[1]);
    unlink(log_path_.c_str());
    throw;
  }
  close(fds[0]);
  pipe_fd_ = fds[1];
}

Encoder::~Encoder() {
  if (!finished_) abort_quietly();
}

void Encoder::abort_quietly() {
  if (pipe_fd_ >= 0) close(pipe_fd_);
  pipe_fd_ = -1;
  if (pid_ > 0) wait_exit(pid_);
  pid_ = -1;
  if (!log_path_.empty()) unlink(log_path_.c_str());
  finished_ = true;
}

void Encoder::add_frame(const std::string& rgba) {
  if (finished_) throw Error("encoder is already finished");
  size_t want = static_cast<size_t>(width_) * height_ * 4;
  if (rgba.size() != want) throw Error("frame has " + std::to_string(rgba.size()) + " bytes, expected " + std::to_string(want));
  const char* p = rgba.data();
  size_t left = want;
  while (left > 0) {
    ssize_t n = write(pipe_fd_, p, left);
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
  finished_ = true;
  if (code == 127) throw Error("ffmpeg not found (is it installed and in PATH?)");
  if (code != 0) throw Error("ffmpeg failed (exit " + std::to_string(code) + "): " + msg);
  if (frames_ == 0) throw Error("no frames were added");
}

VideoInfo probe_video(const std::string& path, const std::string& ffprobe) {
  if (access(path.c_str(), R_OK) != 0) throw Error("cannot read video file: " + path);
  std::string out = run_capture({ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                                 "stream=codec_name,width,height,nb_read_frames,avg_frame_rate:format=duration", "-of",
                                 "default=noprint_wrappers=1", path},
                                "ffprobe");
  std::map<std::string, std::string> kv;
  std::istringstream lines(out);
  for (std::string line; std::getline(lines, line);) {
    size_t eq = line.find('=');
    if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  if (kv.count("codec_name") == 0) throw Error("no video stream in " + path);
  VideoInfo v;
  v.codec = kv["codec_name"];
  v.width = atoi(kv["width"].c_str());
  v.height = atoi(kv["height"].c_str());
  v.frames = atoi(kv["nb_read_frames"].c_str());
  v.duration = atof(kv["duration"].c_str());
  int num = 0, den = 1;
  if (sscanf(kv["avg_frame_rate"].c_str(), "%d/%d", &num, &den) == 2 && den > 0) v.fps = static_cast<double>(num) / den;
  return v;
}

std::string decode_frame(const std::string& path, int index, int width, int height, const std::string& ffmpeg) {
  std::string out = run_capture({ffmpeg, "-v", "error", "-i", path, "-vf", "select=eq(n\\," + std::to_string(index) + ")", "-vframes", "1",
                                 "-f", "rawvideo", "-pix_fmt", "rgba", "-"},
                                "ffmpeg");
  size_t want = static_cast<size_t>(width) * height * 4;
  if (out.size() != want) throw Error("decoded frame " + std::to_string(index) + " has " + std::to_string(out.size()) + " bytes, expected " + std::to_string(want));
  return out;
}

}  // namespace lmv
