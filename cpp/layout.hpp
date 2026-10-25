#pragma once

#include "lmvideo.hpp"
#include "plot.hpp"

#include <map>
#include <string>
#include <vector>

namespace lmvlayout {

struct Rect { double x = 0, y = 0, w = 0, h = 0; };

enum class ChildKind { Panel, Plot, Text, Shape, Image, Glue };

struct Panel;

struct Text {
  std::string value;
  std::string color = "#263244";
  std::string align = "left";
  double size = 14;
  bool bold = false;
};

struct Shape {
  std::string kind = "rect";
  std::string fill = "transparent";
  std::string stroke = "";
  double stroke_width = 1;
  double radius = 0;
  // Diagram vocabulary (labeled shapes, grid cells): a shape carries its
  // current rect so connectors can resolve named anchors and cx()/cy() at
  // draw time. Canvas-created shapes set explicit geometry at construction;
  // shapes used as layout children receive theirs from the layout pass.
  std::string label;
  double label_size = 13;
  double x = 0, y = 0, w = 0, h = 0;
  double opacity = 1;
  bool visible = true;
};

struct ImageView {
  lmv::Image* source = nullptr;
  std::string fit = "contain";
};

struct LayoutItem {
  Panel* owner = nullptr;
  ChildKind kind = ChildKind::Glue;
  void* child = nullptr;
  Rect bounds;
  double preferred_w = -1, preferred_h = -1;
  double min_w = 0, min_h = 0;
  double max_w = -1, max_h = -1;
  double weight = 0;
  std::string side = "top", fill = "both", region = "center", sticky = "nsew";
  std::string align_x = "center", align_y = "center";
  int row = -1, column = -1, row_span = 1, column_span = 1;
  double place_x = 0, place_y = 0, place_w = 0, place_h = 0;
  bool expand = false, placed = false, constraint_set = false;
};

struct Panel {
  explicit Panel(lmvplot::CanvasSlot* canvas);
  Panel(lmvplot::CanvasSlot* canvas, double x, double y, double width, double height);

  lmvplot::CanvasSlot* canvas = nullptr;
  Rect bounds;
  std::string manager = "pack";
  std::string background = "";
  std::string edge_color = "";
  double edge_width = 0;
  double pad_top = 0, pad_right = 0, pad_bottom = 0, pad_left = 0, gap = 0;
  std::string align_x = "center", align_y = "center";
  double min_w = 0, min_h = 0, max_w = -1, max_h = -1;
  int columns = 1;
  std::map<int, double> row_weights, column_weights;
  std::vector<LayoutItem*> children;
};

void draw(Panel& panel);

}  // namespace lmvlayout
