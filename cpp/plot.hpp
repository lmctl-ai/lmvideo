#pragma once

#include "lmvideo.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lmvplot {

struct Plot;

// The host rebinds this stable slot to each video frame's Skia canvas.
struct CanvasSlot {
  std::optional<lmv::Canvas> canvas;
  lmv::Canvas& get();
};

struct Sample {
  double x = 0, y = 0, low = 0, high = 0;
  bool error = false;
};

struct Series {
  Plot* owner = nullptr;
  std::string name;
  std::string kind;
  std::string color = "#2878b5";
  std::string dash = "solid";
  std::string marker = "none";
  bool right_axis = false;
  float width = 2.0f;
  std::vector<Sample> samples;
};

struct Annotation {
  std::string kind, text, color;
  double value = 0;
  double x = 0, y = 0, width = 1;
};

struct Plot {
  explicit Plot(CanvasSlot* canvas, double x, double y, double width, double height);
  void set_bounds(double x, double y, double width, double height);
  CanvasSlot* canvas;
  double x, y, width, height;
  std::string title_text, x_label, y_label, y2_label;
  std::string x_scale = "linear", y_scale = "linear";
  std::string legend_position = "top_right";
  std::string background_color;
  bool grid_visible = true, y2_enabled = false;
  double x_min = 0, x_max = 1, y_min = 0, y_max = 1, y2_min = 0, y2_max = 1;
  bool x_fixed = false, y_fixed = false, y2_fixed = false;
  std::vector<Series*> series;
  std::vector<Annotation> annotations;
};

void draw(Plot& plot);

}  // namespace lmvplot
