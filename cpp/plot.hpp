#pragma once

#include "lmvideo.hpp"

#include <optional>
#include <array>
#include <string>
#include <vector>

namespace lmvplot {

struct Plot;
struct Heatmap;
struct VectorField;

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
  std::vector<std::array<double,3>> bands;
  int bins = 0;
  std::string group_name, stack_name;
  double donut_hole=0.45;
  std::vector<std::array<double,5>> boxes;
};

struct Cell { int row=0, column=0; double value=0; };
struct Heatmap {
  Plot* owner=nullptr;
  std::string name, palette="viridis", contour_color="#263244";
  int rows=0, columns=0;
  bool show_colorbar=true, show_labels=false;
  int contour_count=0;
  float contour_width=1.0f;
  bool fixed_range=false;
  double value_min=0, value_max=1;
  std::vector<Cell> cells;
};
struct Vector { double x=0,y=0,u=0,v=0; };
struct VectorField {
  Plot* owner=nullptr;
  std::string name, color="#263244";
  double scale=1;
  bool streamlines=false;
  std::vector<Vector> vectors;
};
struct Inset { Plot* child=nullptr; double x=0,y=0,width=0,height=0; };

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
  std::vector<Heatmap*> heatmaps;
  std::vector<VectorField*> vector_fields;
  std::vector<Inset> insets;
  double x_symlog_threshold=1, y_symlog_threshold=1;
  std::string x_date_format;
  std::vector<Annotation> annotations;
};

void draw(Plot& plot);
void draw_heatmap(Heatmap& heatmap);
void draw_vector_field(VectorField& field);

}  // namespace lmvplot
