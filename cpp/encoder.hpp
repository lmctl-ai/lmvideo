// lmv::Encoder: assembles RGBA frames into a video by piping them to the ffmpeg CLI (no libav headers on the box, see
// docs/design.md section 3), and lmv::probe_video: reads back stream facts with ffprobe. Skia-free; depends only on Error.
#pragma once
#include <string>

#include "lmvideo.hpp"

namespace lmv {

struct EncoderOptions {
  int fps = 30;               // frames per second, 1..240
  std::string codec;          // ffmpeg encoder name; empty = chosen from the output extension (.mp4 libx264, .webm libvpx-vp9, .mkv ffv1, .gif gif)
  int crf = 18;               // quality for libx264/libvpx-vp9 (lower = better), 0..51
  std::string ffmpeg = "ffmpeg";  // executable (searched in PATH)
};

class Encoder {
 public:
  // Starts ffmpeg writing `path` (overwritten). yuv420p codecs need even width and height.
  Encoder(const std::string& path, int width, int height, const EncoderOptions& options = {});
  Encoder(const Encoder&) = delete;
  Encoder& operator=(const Encoder&) = delete;
  ~Encoder();  // aborts quietly if finish() was not called

  // Appends one frame: exactly width*height*4 bytes of RGBA (Surface::rgba()).
  void add_frame(const std::string& rgba);
  // Closes the pipe and waits for ffmpeg; throws Error with ffmpeg's message when it failed. Idempotent.
  void finish();
  int frames() const { return frames_; }

 private:
  void abort_quietly();
  int width_, height_, frames_ = 0;
  int pipe_fd_ = -1;      // ffmpeg's stdin
  int pid_ = -1;
  std::string log_path_;  // ffmpeg's stderr is collected in this temp file
  bool finished_ = false;
};

struct VideoInfo {
  std::string codec;
  int width = 0, height = 0;
  int frames = 0;         // counted by decoding (ffprobe -count_frames), not trusted from the header
  double duration = 0;    // seconds, container duration
  double fps = 0;         // average frame rate
};

// Throws Error when the file is missing or ffprobe cannot read a video stream.
VideoInfo probe_video(const std::string& path, const std::string& ffprobe = "ffprobe");

// Decodes frame `index` (0-based) to RGBA (width*height*4 bytes) with ffmpeg; used by tests to compare pixels.
std::string decode_frame(const std::string& path, int index, int width, int height, const std::string& ffmpeg = "ffmpeg");

}  // namespace lmv
