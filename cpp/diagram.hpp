// Retained diagram vocabulary for the DSL host: labeled shapes (box, diamond,
// cylinder, note, cluster), routed connectors with markers, grids of anchor
// cells, and sequence-diagram helpers. The vocabulary mirrors lmdraw 0.2.6 so
// a diagram script ports between hosts by changing little; rendering uses Skia
// through the lmvideo value API. Every created item registers with the canvas's
// retained diagram layer and is (re)drawn in creation order when the chunk calls
// Canvas.draw_diagram(), which makes the nodes updatable per video frame.
#pragma once

#include "layout.hpp"
#include "plot.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lmvdiagram {

class Layer;  // the per-canvas retained item list; defined below

// Named anchors every shape exposes: n, s, e, w, ne, nw, se, sw, c.
bool is_anchor(const std::string& name);
// The anchor point on the shape's current rect. Throws lmv::Error on an
// unknown anchor name.
std::pair<double, double> anchor_point(const lmvlayout::Shape& shape, const std::string& anchor);

// Parses an SVG stroke-dasharray list ("6 5", "6,5"); "none" or "" -> empty.
// Throws lmv::Error on malformed or non-positive entries (at most 32).
std::vector<float> parse_dash(const std::string& value);

// Renders a labeled diagram shape body and its label inside (x, y, w, h).
// Kinds "cell" and plain layout kinds without a label draw their plain body.
void render_shape(lmv::Canvas& canvas, const lmvlayout::Shape& shape, double x, double y, double w, double h);

// Applies an SVG-style presentation attribute to a shape (fill, stroke,
// stroke-width, opacity, visibility); returns false for other names.
bool shape_set_attr(lmvlayout::Shape& shape, const std::string& name, const std::string& value);

// A retained diagram item the layer redraws on every frame.
class Item {
 public:
  virtual ~Item() = default;
  virtual void draw(lmv::Canvas& canvas) const = 0;
  // Applies an SVG-style presentation attribute; returns false when the name
  // is not supported by this item.
  virtual bool set_attr(const std::string& name, const std::string& value) = 0;
};

// Draws a labeled shape from its current rect each frame.
class ShapeItem final : public Item {
 public:
  explicit ShapeItem(lmvlayout::Shape* shape) : shape(shape) {}
  lmvlayout::Shape* shape;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
};

// An edge between two anchor endpoints (shape + anchor), two raw points, or a
// mixture. Endpoints given as (shape, anchor) resolve at draw time, so a
// connector may address a shape whose bounds a layout pass assigns earlier in
// the same frame.
class ConnectorItem final : public Item {
 public:
  bool has_a = false, has_b = false;
  const lmvlayout::Shape* a = nullptr;
  const lmvlayout::Shape* b = nullptr;
  std::string anchor_a, anchor_b;
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  std::string routing, start_marker, end_marker, dash, color, label;
  double width = 2, label_t = 0.5, label_dx = 0, label_dy = 0, dash_offset = 0;
  bool visible = true;
  double opacity = 1;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
  std::pair<double, double> from() const;
  std::pair<double, double> to() const;
};

// Sequence-diagram notation; each draws like its lmdraw counterpart.
class LifelineItem final : public Item {
 public:
  double x = 0, y0 = 0, y1 = 0;
  std::string color;
  bool visible = true;
  double opacity = 1;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
};

class MessageItem final : public Item {
 public:
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  std::string label, color;
  bool dashed = false;
  bool visible = true;
  double opacity = 1;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
};

class ActivationItem final : public Item {
 public:
  double x = 0, y0 = 0, y1 = 0;
  std::string fill, stroke;
  bool visible = true;
  double opacity = 1;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
};

class SelfMessageItem final : public Item {
 public:
  double x = 0, y = 0;
  std::string label, color;
  bool visible = true;
  double opacity = 1;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
};

// A group of items; set_attr forwards a presentation attribute to every child.
// `owner` is the layer the group was created on; Group.add adopts a root item
// of that layer into the group so it no longer draws at the top level.
class GroupItem final : public Item {
 public:
  std::vector<Item*> children;
  Layer* owner = nullptr;
  bool visible = true;
  double opacity = 1;
  void draw(lmv::Canvas& canvas) const override;
  bool set_attr(const std::string& name, const std::string& value) override;
};

// The per-canvas retained list of diagram items, drawn in creation order.
class Layer {
 public:
  std::vector<Item*> items;
  void append(Item* item) { items.push_back(item); }
  // Detaches an item from the root list (Group.add adopts it into the group).
  bool erase_root(Item* item);
  void draw(lmv::Canvas& canvas) const;
};

}  // namespace lmvdiagram
