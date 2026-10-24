#include "plot.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
int tests = 0;
int failures = 0;
void check(bool condition, const char* name) {
  ++tests;
  if (condition) std::cout << "ok " << tests << " - " << name << '\n';
  else { std::cerr << "not ok " << tests << " - " << name << '\n'; ++failures; }
}
lmv::Color pixel(const lmv::Surface& surface, int x, int y) {
  const auto bytes = surface.rgba();
  const size_t at = (static_cast<size_t>(y) * surface.width() + x) * 4;
  return {static_cast<uint8_t>(bytes[at]), static_cast<uint8_t>(bytes[at + 1]),
          static_cast<uint8_t>(bytes[at + 2]), static_cast<uint8_t>(bytes[at + 3])};
}
bool near(lmv::Color a, lmv::Color b, int tolerance) {
  return std::abs(int(a.r)-int(b.r))<=tolerance && std::abs(int(a.g)-int(b.g))<=tolerance &&
         std::abs(int(a.b)-int(b.b))<=tolerance;
}
void add_line(lmvplot::Plot& plot, lmvplot::Series& line) {
  line.owner = &plot; line.name = "signal"; line.kind = "line"; line.color = "#2878b5"; line.width = 2;
  line.samples = {{0,0,0,0,false},{1,1,0,0,false},{2,0,0,0,false},{3,-1,0,0,false},{4,0,0,0,false}};
  plot.series.push_back(&line);
}
void linear_axes_and_legend() {
  lmv::Surface surface(400,260);
  auto canvas=surface.canvas();
  canvas.clear({24,32,48,255});
  lmvplot::CanvasSlot slot; slot.canvas=canvas;
  lmvplot::Plot plot(&slot,0,0,400,260);
  plot.title_text="Linear ticks"; plot.x_label="x"; plot.y_label="y";
  plot.x_fixed=true; plot.x_min=0; plot.x_max=4;
  plot.y_fixed=true; plot.y_min=-2; plot.y_max=2;
  lmvplot::Series line; add_line(plot,line); lmvplot::draw(plot);
  const auto axis=pixel(surface,64,100);
  const auto grid=pixel(surface,143,100);
  check(axis.r < 150 && axis.g < 160 && axis.b < 180, "linear y-axis golden pixel");
  check(near(grid,lmv::Color(227,232,239,255),10), "linear major tick grid golden pixel");
  const auto legend=pixel(surface,300,53);
  check(legend.r > 210 && legend.g > 210 && legend.b > 210 && legend != lmv::Color(24,32,48,255), "top-right legend occupies its expected layout box");
  const auto swatch=pixel(surface,310,60);
  check(swatch.b > swatch.r && swatch.b > swatch.g, "legend swatch uses the series color");
}
void logarithmic_ticks() {
  lmv::Surface surface(400,260);
  auto canvas=surface.canvas(); canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot slot; slot.canvas=canvas;
  lmvplot::Plot plot(&slot,0,0,400,260);
  plot.x_scale="log"; plot.x_fixed=true; plot.x_min=1; plot.x_max=1000;
  plot.y_fixed=true; plot.y_min=1; plot.y_max=100; plot.legend_position="none";
  lmvplot::Series line; line.owner=&plot; line.kind="line"; line.color="#2878b5";
  line.samples={{1,1,0,0,false},{10,10,0,0,false},{100,30,0,0,false},{1000,100,0,0,false}};
  plot.series.push_back(&line); lmvplot::draw(plot);
  check(near(pixel(surface,170,70),lmv::Color(241,243,247,255),10), "log decade tick at 10 is one-third across the axis");
  const auto minor=pixel(surface,202,227);
  check(near(minor,lmv::Color(172,177,185,255),12), "log minor tick for 20 is placed between decade ticks");
}
void heatmap_palette_and_symlog() {
  lmv::Surface surface(400,260);auto canvas=surface.canvas();canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot slot;slot.canvas=canvas;lmvplot::Plot plot(&slot,0,0,400,260);
  plot.x_fixed=true;plot.x_min=0;plot.x_max=2;plot.y_fixed=true;plot.y_min=0;plot.y_max=2;plot.legend_position="none";
  lmvplot::Heatmap heat;heat.owner=&plot;heat.rows=2;heat.columns=2;heat.palette="gray";heat.show_colorbar=true;
  heat.cells={{0,0,0},{0,1,1},{1,0,2},{1,1,3}};plot.heatmaps.push_back(&heat);lmvplot::draw(plot);
  check(near(pixel(surface,100,160),lmv::Color(0,0,0,255),3),"heatmap gray palette maps minimum to black");
  check(pixel(surface,364,100).r>pixel(surface,364,180).r,"heatmap colorbar follows the value ramp");
  lmv::Surface symlog_surface(400,260);auto cv=symlog_surface.canvas();cv.clear({255,255,255,255});lmvplot::CanvasSlot symlog_slot;symlog_slot.canvas=cv;
  lmvplot::Plot symlog(&symlog_slot,0,0,400,260);symlog.x_fixed=true;symlog.x_min=0;symlog.x_max=1;symlog.y_fixed=true;symlog.y_min=-1000;symlog.y_max=1000;symlog.y_scale="symlog";symlog.y_symlog_threshold=100;symlog.legend_position="none";
  lmvplot::Series dots;dots.owner=&symlog;dots.kind="scatter";dots.color="#d02020";dots.marker="circle";dots.samples={{.5,100,0,0,false}};symlog.series.push_back(&dots);lmvplot::draw(symlog);
  bool red_at_threshold=false;for(int y=85;y<=94;++y)for(int x=218;x<=228;++x){const auto marker=pixel(symlog_surface,x,y);red_at_threshold=red_at_threshold||(marker.r>marker.g*1.5&&marker.r>marker.b*1.5);}
  check(red_at_threshold,"symlog transform places the threshold marker at its expected pixel");
}
}
int main() {
  linear_axes_and_legend();
  logarithmic_ticks();
  heatmap_palette_and_symlog();
  std::cout << tests << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
