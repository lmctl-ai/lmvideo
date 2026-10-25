#include "diagram.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* label) {
  ++checks;
  if (ok) std::cout << "ok " << checks << " - " << label << '\n';
  else { std::cerr << "not ok " << checks << " - " << label << '\n'; ++failures; }
}
lmv::Color pixel(const lmv::Surface& surface, int x, int y) {
  const auto bytes = surface.rgba();
  const size_t p = (size_t(y) * surface.width() + x) * 4;
  return {uint8_t(bytes[p]), uint8_t(bytes[p + 1]), uint8_t(bytes[p + 2]), uint8_t(bytes[p + 3])};
}
bool throws_error(const char* label, void (*fn)()) {
  try { fn(); } catch (const lmv::Error&) { check(true, label); return true; }
  check(false, label);
  return false;
}

// Anchor geometry on a shape rect.
void anchors() {
  lmvlayout::Shape s;
  s.kind = "box"; s.x = 100; s.y = 50; s.w = 200; s.h = 100;
  const auto c = lmvdiagram::anchor_point(s, "c");
  const auto e = lmvdiagram::anchor_point(s, "e");
  const auto sw = lmvdiagram::anchor_point(s, "sw");
  check(c.first == 200 && c.second == 100, "anchor c is the rect center");
  check(e.first == 300 && e.second == 100, "anchor e is the right edge midpoint");
  check(sw.first == 100 && sw.second == 150, "anchor sw is the bottom-left corner");
  bool threw = false;
  try { (void)lmvdiagram::anchor_point(s, "middle"); } catch (const lmv::Error& e2) { threw = e2.what() == std::string("anchor must be one of n, s, e, w, ne, nw, se, sw, c"); }
  check(threw, "an unknown anchor name is refused");
}

// Dash list parsing mirrors lmdraw's stroke-dasharray acceptance.
void dash_parsing() {
  check(lmvdiagram::parse_dash("none").empty() && lmvdiagram::parse_dash("").empty(), "'none' and '' parse to a solid stroke");
  const auto a = lmvdiagram::parse_dash("6 5");
  check(a.size() == 2 && a[0] == 6.0f && a[1] == 5.0f, "a space-separated dash pair parses");
  const auto b = lmvdiagram::parse_dash("6,5");
  check(b.size() == 2 && b[1] == 5.0f, "a comma-separated dash pair parses");
  bool threw = false;
  try { (void)lmvdiagram::parse_dash("6 wide"); } catch (const lmv::Error&) { threw = true; }
  check(threw, "a malformed dash list is refused");
}

// A connector between shape anchors resolves the shapes' current rects at
// draw time, so a layout pass that runs first is honored.
void connector_resolves_after_layout() {
  lmv::Surface surface(300, 200);
  auto canvas = surface.canvas();
  canvas.clear({16, 24, 32, 255});
  lmvplot::CanvasSlot slot;
  slot.canvas = canvas;
  auto layer = std::make_shared<lmvdiagram::Layer>();
  slot.diagram = layer;

  lmvlayout::Shape a, b;
  a.kind = "box"; a.label = "a"; a.fill = "#1e3247"; a.stroke = "#6fbdb4"; a.stroke_width = 2;
  b.kind = "box"; b.label = "b"; b.fill = "#1e3247"; b.stroke = "#6fbdb4"; b.stroke_width = 2;
  // Geometry the way Panel.draw would write it back after arranging.
  a.x = 30; a.y = 40; a.w = 100; a.h = 44;
  b.x = 180; b.y = 120; b.w = 100; b.h = 44;
  layer->append(new lmvdiagram::ShapeItem(&a));
  layer->append(new lmvdiagram::ShapeItem(&b));

  auto* edge = new lmvdiagram::ConnectorItem();
  edge->has_a = true; edge->a = &a; edge->anchor_a = "e";
  edge->has_b = true; edge->b = &b; edge->anchor_b = "w";
  edge->routing = "straight"; edge->end_marker = "arrow"; edge->color = "#f5c16c"; edge->width = 2.0;
  layer->append(edge);

  layer->draw(canvas);
  check(pixel(surface, 155, 102) == lmv::Color(245, 193, 108, 255), "the connector runs between the resolved anchor midpoints");
  check(pixel(surface, 30, 40) == lmv::Color(111, 189, 180, 255), "the box stroke lands on the laid-out rect");
}

// Elbow routing picks one deterministic bend like lmdraw.
void elbow_routing() {
  lmv::Surface surface(200, 200);
  auto canvas = surface.canvas();
  canvas.clear({16, 24, 32, 255});
  lmvdiagram::ConnectorItem wide;
  wide.x1 = 20; wide.y1 = 100; wide.x2 = 180; wide.y2 = 140;
  wide.routing = "elbow"; wide.color = "#f5c16c"; wide.width = 2.0;
  wide.draw(canvas);
  check(pixel(surface, 100, 100) == lmv::Color(245, 193, 108, 255), "a wide elbow runs horizontally first");
  check(pixel(surface, 100, 140) == lmv::Color(245, 193, 108, 255), "a wide elbow finishes on the lower run");

  lmv::Surface tall_surface(200, 200);
  auto tall_canvas = tall_surface.canvas();
  tall_canvas.clear({16, 24, 32, 255});
  lmvdiagram::ConnectorItem tall;
  tall.x1 = 60; tall.y1 = 20; tall.x2 = 140; tall.y2 = 180;
  tall.routing = "elbow"; tall.color = "#f5c16c"; tall.width = 2.0;
  tall.draw(tall_canvas);
  check(pixel(tall_surface, 60, 100) == lmv::Color(245, 193, 108, 255), "a tall elbow runs vertically first");
  check(pixel(tall_surface, 140, 100) == lmv::Color(245, 193, 108, 255), "a tall elbow finishes on the vertical drop");
}

// Shape attributes validate like lmdraw's set_attr family.
void shape_attributes() {
  lmvlayout::Shape s;
  check(lmvdiagram::shape_set_attr(s, "fill", "#233d2e"), "fill is a shape attribute");
  check(lmvdiagram::shape_set_attr(s, "opacity", "0.5") && s.opacity == 0.5, "opacity is a shape attribute");
  check(lmvdiagram::shape_set_attr(s, "visibility", "hidden") && !s.visible, "visibility is a shape attribute");
  check(!lmvdiagram::shape_set_attr(s, "warp", "4"), "an unknown shape attribute is not accepted");
  bool threw = false;
  try { (void)lmvdiagram::shape_set_attr(s, "opacity", "2"); } catch (const lmv::Error&) { threw = true; }
  check(threw, "opacity outside 0..1 is refused");
}
}  // namespace

int main() {
  anchors();
  dash_parsing();
  connector_resolves_after_layout();
  elbow_routing();
  shape_attributes();
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
