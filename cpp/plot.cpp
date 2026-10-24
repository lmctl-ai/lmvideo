#include "plot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <utility>

namespace lmvplot {
namespace {
constexpr size_t kMaxSeries = 32;
constexpr size_t kMaxSamples = 250000;

void finite(double value, const char* what) {
  if (!std::isfinite(value)) throw lmv::Error(std::string("Plot: ") + what + " must be finite");
}
lmv::Paint fill(const std::string& color) {
  return lmv::Paint().fill().color(lmv::parse_color(color)).antialias(true);
}
lmv::Paint stroke(const std::string& color, float width) {
  return lmv::Paint().stroke().stroke_width(width).color(lmv::parse_color(color)).antialias(true);
}
std::string number(double value, double step = 0) {
  if (std::fabs(value) < std::fabs(step) * 1e-10) value = 0;
  char text[64];
  if (std::fabs(value) >= 1e6 || (value != 0 && std::fabs(value) < 1e-4)) std::snprintf(text, sizeof(text), "%.3g", value);
  else {
    int decimals = 0;
    if (step > 0) {
      double s = step;
      while (decimals < 8 && std::fabs(s - std::round(s)) > 1e-8) { s *= 10; ++decimals; }
    }
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
  }
  return text;
}
double nice_step(double span) {
  const double raw = span / 5.0;
  const double power = std::pow(10.0, std::floor(std::log10(raw)));
  const double fraction = raw / power;
  const double nice = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 2.5 ? 2.5 : fraction <= 5.0 ? 5.0 : 10.0;
  return nice * power;
}
struct Bounds { double lo, hi; };
Bounds auto_bounds(const Plot& p, bool x_axis, bool right) {
  const bool logarithmic = x_axis ? p.x_scale == "log" : p.y_scale == "log";
  double lo = std::numeric_limits<double>::infinity();
  double hi = -lo;
  for (const Series* s : p.series) {
    if (!x_axis && s->right_axis != right) continue;
    for (const Sample& v : s->samples) {
      const double a = x_axis ? v.x : v.y;
      lo = std::min(lo, a); hi = std::max(hi, a);
      if (!x_axis && v.error) { lo = std::min(lo, v.low); hi = std::max(hi, v.high); }
    }
  }
  if (!std::isfinite(lo)) return logarithmic ? Bounds{1,10} : Bounds{0,1};
  if (lo == hi && logarithmic) {
    lo /= 1.2; hi *= 1.2;
  } else if (lo == hi) {
    const double pad = std::max(std::fabs(lo) * 0.05, 1.0);
    lo -= pad; hi += pad;
  } else if (logarithmic) {
    lo /= 1.1; hi *= 1.1;
  } else {
    const double pad = (hi - lo) * 0.05;
    lo -= pad; hi += pad;
  }
  return {lo, hi};
}
struct Area { double x, y, w, h; };
struct Axis {
  double lo, hi;
  bool log;
  double map(double value, double pixel0, double pixel1, bool reverse = false) const {
    const double a = log ? std::log10(lo) : lo;
    const double b = log ? std::log10(hi) : hi;
    const double v = log ? std::log10(value) : value;
    const double t = (v - a) / (b - a);
    return reverse ? pixel1 - t * (pixel1 - pixel0) : pixel0 + t * (pixel1 - pixel0);
  }
};
std::vector<double> linear_ticks(double lo, double hi, double& step) {
  step = nice_step(hi - lo);
  std::vector<double> out;
  const double first = std::ceil(lo / step - 1e-10) * step;
  for (double v = first; v <= hi + step * 1e-9 && out.size() < 64; v += step) out.push_back(v);
  return out;
}
std::vector<double> log_ticks(double lo, double hi, bool majors_only) {
  std::vector<double> out;
  int first = static_cast<int>(std::floor(std::log10(lo)));
  int last = static_cast<int>(std::ceil(std::log10(hi)));
  if (last - first > 20) throw lmv::Error("Plot: logarithmic range cannot span more than 20 decades");
  for (int exponent = first; exponent <= last; ++exponent) {
    const double decade = std::pow(10.0, exponent);
    for (int m = 1; m <= 9; ++m) {
      if (majors_only && m != 1) continue;
      const double v = decade * m;
      if (v >= lo * (1 - 1e-12) && v <= hi * (1 + 1e-12)) out.push_back(v);
    }
  }
  return out;
}
void draw_text(lmv::Canvas& c, const lmv::Font& font, const std::string& text, double x, double baseline, const std::string& color) {
  c.draw_text(text, static_cast<float>(x), static_cast<float>(baseline), font, fill(color));
}
void draw_marker(lmv::Canvas& c, const Series& s, double x, double y) {
  const auto color = lmv::parse_color(s.color);
  const float r = std::max(2.5f, s.width * 1.8f);
  if (s.marker == "none") return;
  if (s.marker == "circle") { c.draw_circle(x, y, r, lmv::Paint().fill().color(color).antialias(true)); return; }
  if (s.marker == "square") { c.draw_rect(x-r, y-r, r*2, r*2, lmv::Paint().fill().color(color).antialias(true)); return; }
  lmv::Path p;
  if (s.marker == "triangle") p.move_to(x, y-r).line_to(x+r, y+r).line_to(x-r, y+r).close();
  else if (s.marker == "plus") {
    c.draw_line(x-r, y, x+r, y, stroke(s.color, s.width)); c.draw_line(x, y-r, x, y+r, stroke(s.color, s.width)); return;
  }
  c.draw_path(p, lmv::Paint().fill().color(color).antialias(true));
}
void draw_dash_dot(lmv::Canvas& c, const std::vector<std::pair<double,double>>& points,
                   const std::string& color, float width) {
  constexpr double pattern[] = {8, 3, 2, 3};
  constexpr double cycle = 16;
  double distance = 0;
  const auto pen = stroke(color,width);
  for (size_t i=1;i<points.size();++i) {
    const double x0=points[i-1].first, y0=points[i-1].second;
    const double dx=points[i].first-x0, dy=points[i].second-y0;
    const double length=std::hypot(dx,dy);
    if (length==0) continue;
    double along=0;
    while(along<length) {
      double phase=std::fmod(distance+along,cycle);
      size_t part=0;
      while(part<3 && phase>=pattern[part]) { phase-=pattern[part]; ++part; }
      const double step=std::min(length-along,pattern[part]-phase);
      if(part%2==0) {
        const double a=along/length, b=(along+step)/length;
        c.draw_line(x0+dx*a,y0+dy*a,x0+dx*b,y0+dy*b,pen);
      }
      along+=step;
    }
    distance+=length;
  }
}
void check_data(const Plot& p, const Series& s) {
  for (const auto& v : s.samples) {
    finite(v.x, "sample x"); finite(v.y, "sample y");
    if (p.x_scale == "log" && v.x <= 0) throw lmv::Error("Plot: logarithmic x data must be positive");
    if (s.right_axis ? p.y_scale == "log" : p.y_scale == "log") {
      if (v.y <= 0 || (v.error && (v.low <= 0 || v.high <= 0))) throw lmv::Error("Plot: logarithmic y data and error bounds must be positive");
    }
    if (v.error) { finite(v.low, "error low"); finite(v.high, "error high"); if (v.low > v.y || v.high < v.y) throw lmv::Error("Plot: error bounds must surround y"); }
  }
}
void draw_series(lmv::Canvas& c, const Plot& p, const Series& s, const Area& a, const Axis& xa, const Axis& ya, double base) {
  if (s.samples.empty()) return;
  c.save(); c.clip_rect(a.x, a.y, a.w, a.h);
  auto px = [&](double v) { return xa.map(v, a.x, a.x + a.w); };
  auto py = [&](double v) { return ya.map(v, a.y, a.y + a.h, true); };
  lmv::Paint line = stroke(s.color, s.width);
  if (s.dash == "dashed") line.dash(8, 5);
  else if (s.dash == "dotted") line.dash(2, 4);
  std::vector<std::pair<double,double>> dash_dot_points;
  if (s.kind == "bar") {
    size_t bar_index = 0, bar_count = 0;
    for (auto* other : p.series) if (other->kind == "bar") { if (other == &s) bar_index = bar_count; ++bar_count; }
    for (size_t i = 0; i < s.samples.size(); ++i) {
      double gap = a.w / std::max<size_t>(s.samples.size(), 8);
      if (s.samples.size() > 1) {
        double neighbor = i == 0 ? std::fabs(px(s.samples[1].x)-px(s.samples[0].x)) :
                          i + 1 == s.samples.size() ? std::fabs(px(s.samples[i].x)-px(s.samples[i-1].x)) :
                          std::min(std::fabs(px(s.samples[i].x)-px(s.samples[i-1].x)), std::fabs(px(s.samples[i+1].x)-px(s.samples[i].x)));
        gap = std::min(gap, neighbor);
      }
      const double group = gap * 0.78, bw = group / std::max<size_t>(bar_count, 1);
      const double x0 = px(s.samples[i].x) - group/2 + bar_index*bw + bw*0.08;
      const double top = py(s.samples[i].y), bottom = py(base);
      c.draw_rect(x0, std::min(top,bottom), std::max(1.0,bw*0.84), std::fabs(bottom-top), lmv::Paint().fill().color(lmv::parse_color(s.color)).antialias(true));
    }
  } else {
    lmv::Path path;
    bool first = true;
    if (s.kind == "area") {
      const double ybase = py(base);
      path.move_to(px(s.samples.front().x), ybase).line_to(px(s.samples.front().x), py(s.samples.front().y));
    }
    for (size_t i = 0; i < s.samples.size(); ++i) {
      const auto& v = s.samples[i];
      const double x = px(v.x), y = py(v.y);
      if (s.kind == "step" && !first) path.line_to(x, py(s.samples[i - 1].y));
      if (s.kind == "area") { if (i) path.line_to(x,y); }
      else if (first) path.move_to(x,y); else path.line_to(x,y);
      if (s.kind == "step" && !first) dash_dot_points.emplace_back(x,py(s.samples[i-1].y));
      dash_dot_points.emplace_back(x,y);
      if (s.kind == "errorbar") {
        const double high = py(v.high), low = py(v.low);
        c.draw_line(x, high, x, low, line); c.draw_line(x-4, high, x+4, high, line); c.draw_line(x-4, low, x+4, low, line);
        Series marker=s; if(marker.marker=="none") marker.marker="circle";
        draw_marker(c,marker,x,y);
      }
      if (s.kind == "scatter") draw_marker(c, s, x, y);
      first = false;
    }
    if (s.kind == "area") {
      path.line_to(px(s.samples.back().x), py(base)).close();
      auto area = lmv::Paint().fill().color(lmv::parse_color(s.color)).alpha(0.22f).antialias(true);
      c.draw_path(path, area); if(s.dash!="dash_dot") c.draw_path(path, line);
    } else if (s.kind == "line" || s.kind == "step") { if(s.dash!="dash_dot") c.draw_path(path, line); }
    else if (s.kind == "scatter") {
      for (const auto& v : s.samples) {
        Series marker = s;
        if (marker.marker == "none") marker.marker = "circle";
        draw_marker(c, marker, px(v.x), py(v.y));
      }
    }
    if(s.dash=="dash_dot"&&(s.kind=="line"||s.kind=="step"||s.kind=="area")) draw_dash_dot(c,dash_dot_points,s.color,s.width);
    if ((s.kind == "line" || s.kind == "step" || s.kind == "area") && s.marker != "none")
      for (const auto& v : s.samples) draw_marker(c, s, px(v.x), py(v.y));
  }
  c.restore();
}
void draw_legend(lmv::Canvas& c, const Plot& p, const Area& a, const lmv::Font& font) {
  if (p.legend_position == "none") return;
  std::vector<const Series*> items;
  for (const auto* s : p.series) if (!s->name.empty()) items.push_back(s);
  if (items.empty()) return;
  double text_width = 0;
  for (auto* s : items) text_width = std::max<double>(text_width, font.measure(s->name));
  const double w = text_width + 54, h = items.size()*21 + 12;
  const bool left = p.legend_position == "top_left" || p.legend_position == "bottom_left";
  const bool bottom = p.legend_position == "bottom_left" || p.legend_position == "bottom_right";
  const double x = left ? a.x + 8 : a.x + a.w - w - 8;
  const double y = bottom ? a.y + a.h - h - 8 : a.y + 8;
  c.draw_round_rect(x,y,w,h,4,4,lmv::Paint().fill().color(lmv::Color(255,255,255,232)).antialias(true));
  c.draw_round_rect(x,y,w,h,4,4,lmv::Paint().stroke().stroke_width(1).color(lmv::Color(190,198,210,255)).antialias(true));
  for (size_t i=0;i<items.size();++i) {
    const Series& s=*items[i]; const double yy=y+18+i*21;
    c.draw_line(x+8,yy-4,x+29,yy-4,stroke(s.color,s.width));
    draw_text(c,font,s.name,x+36,yy, "#263244");
  }
}
}  // namespace

lmv::Canvas& CanvasSlot::get() {
  if (!canvas) throw lmv::Error("Canvas is not available before video.begin()");
  return *canvas;
}

Plot::Plot(CanvasSlot* slot, double px, double py, double w, double h) : canvas(slot), x(px), y(py), width(w), height(h) {
  set_bounds(px,py,w,h);
}

void Plot::set_bounds(double px, double py, double w, double h) {
  finite(px,"x"); finite(py,"y"); finite(w,"width"); finite(h,"height");
  finite(px+w,"right edge"); finite(py+h,"bottom edge");
  if (w <= 0 || h <= 0) throw lmv::Error("Canvas.plot: width and height must be positive");
  x=px; y=py; width=w; height=h;
}

void draw(Plot& p) {
  if (!p.canvas) throw lmv::Error("Plot: canvas is unavailable");
  lmv::Canvas& c=p.canvas->get();
  for (auto* s:p.series) {
    if (s->right_axis && !p.y2_enabled) throw lmv::Error("Plot: right-axis series requires y2_range");
    check_data(p,*s);
  }
  Bounds xb=p.x_fixed?Bounds{p.x_min,p.x_max}:auto_bounds(p,true,false);
  Bounds yb=p.y_fixed?Bounds{p.y_min,p.y_max}:auto_bounds(p,false,false);
  Bounds y2b=p.y2_fixed?Bounds{p.y2_min,p.y2_max}:auto_bounds(p,false,true);
  if (xb.lo>=xb.hi || yb.lo>=yb.hi || (p.y2_enabled && y2b.lo>=y2b.hi)) throw lmv::Error("Plot: axis ranges must be increasing");
  if ((p.x_scale=="log" && xb.lo<=0) || (p.y_scale=="log" && (yb.lo<=0 || (p.y2_enabled && y2b.lo<=0)))) throw lmv::Error("Plot: logarithmic ranges must be positive");
  if (p.x_scale=="log" && std::log10(xb.hi/xb.lo)>20) throw lmv::Error("Plot: logarithmic x range cannot span more than 20 decades");
  if (p.y_scale=="log" && std::log10(yb.hi/yb.lo)>20) throw lmv::Error("Plot: logarithmic y range cannot span more than 20 decades");
  for (const auto& ann : p.annotations) {
    if (ann.kind == "vline" && p.x_scale == "log" && ann.value <= 0) throw lmv::Error("Plot: logarithmic x reference line must be positive");
    if ((ann.kind == "hline" || ann.kind == "text") && p.y_scale == "log" && (ann.kind == "hline" ? ann.value : ann.y) <= 0)
      throw lmv::Error("Plot: logarithmic y annotation must be positive");
  }
  const bool right= p.y2_enabled || !p.y2_label.empty();
  const double left_margin=64, right_margin=right?58:18, top_margin=p.title_text.empty()?14:38, bottom_margin=p.x_label.empty()?34:54;
  Area a{p.x+left_margin,p.y+top_margin,p.width-left_margin-right_margin,p.height-top_margin-bottom_margin};
  if(a.w<24 || a.h<24) throw lmv::Error("Plot: plot rectangle is too small for labels and axes");
  Axis xa{xb.lo,xb.hi,p.x_scale=="log"}, ya{yb.lo,yb.hi,p.y_scale=="log"}, y2a{y2b.lo,y2b.hi,p.y_scale=="log"};
  lmv::Font small(11), label(13), titlefont(16); titlefont.embolden(true);
  const std::string ink="#344054", muted="#667085", grid="#e3e8ef";
  c.save();
  if(!p.background_color.empty()) c.draw_rect(p.x,p.y,p.width,p.height,fill(p.background_color));
  double sx=0,sy=0,sy2=0;
  auto xt=xa.log?log_ticks(xb.lo,xb.hi,false):linear_ticks(xb.lo,xb.hi,sx);
  auto yt=ya.log?log_ticks(yb.lo,yb.hi,false):linear_ticks(yb.lo,yb.hi,sy);
  auto y2t=y2a.log?log_ticks(y2b.lo,y2b.hi,false):linear_ticks(y2b.lo,y2b.hi,sy2);
  if(p.grid_visible) {
    for(double v:xt) if(!xa.log || std::fmod(std::log10(v),1.0)==0) c.draw_line(xa.map(v,a.x,a.x+a.w),a.y,xa.map(v,a.x,a.x+a.w),a.y+a.h,stroke(grid,1));
    for(double v:yt) if(!ya.log || std::fmod(std::log10(v),1.0)==0) c.draw_line(a.x,ya.map(v,a.y,a.y+a.h,true),a.x+a.w,ya.map(v,a.y,a.y+a.h,true),stroke(grid,1));
  }
  c.draw_rect(a.x,a.y,a.w,a.h,stroke(ink,1.2f));
  for(double v:xt) {
    const double x=xa.map(v,a.x,a.x+a.w); c.draw_line(x,a.y+a.h,x,a.y+a.h+4,stroke(ink,1));
    if(!xa.log || std::fabs(std::log10(v)-std::round(std::log10(v)))<1e-8) draw_text(c,small,xa.log?number(v):number(v,sx),x-small.measure(xa.log?number(v):number(v,sx))/2,a.y+a.h+17,muted);
  }
  for(double v:yt) {
    const double y=ya.map(v,a.y,a.y+a.h,true); c.draw_line(a.x-4,y,a.x,y,stroke(ink,1));
    if(!ya.log || std::fabs(std::log10(v)-std::round(std::log10(v)))<1e-8) { const auto t=ya.log?number(v):number(v,sy); draw_text(c,small,t,a.x-8-small.measure(t),y+4,muted); }
  }
  if(p.y2_enabled) for(double v:y2t) {
    const double y=y2a.map(v,a.y,a.y+a.h,true); c.draw_line(a.x+a.w,y,a.x+a.w+4,y,stroke(ink,1));
    if(!y2a.log || std::fabs(std::log10(v)-std::round(std::log10(v)))<1e-8) { const auto t=y2a.log?number(v):number(v,sy2); draw_text(c,small,t,a.x+a.w+7,y+4,muted); }
  }
  for(const auto* s:p.series) {
    const Bounds& bounds=s->right_axis?y2b:yb;
    const double base=bounds.lo<=0 && bounds.hi>=0?0:bounds.lo;
    draw_series(c,p,*s,a,xa,s->right_axis?y2a:ya,base);
  }
  c.save(); c.clip_rect(a.x,a.y,a.w,a.h);
  for(const auto& ann:p.annotations) {
    if(ann.kind=="vline") { const double x=xa.map(ann.value,a.x,a.x+a.w); auto pen=stroke(ann.color,ann.width); pen.dash(4,4); c.draw_line(x,a.y,x,a.y+a.h,pen); }
    else if(ann.kind=="hline") { const double y=ya.map(ann.value,a.y,a.y+a.h,true); auto pen=stroke(ann.color,ann.width); pen.dash(4,4); c.draw_line(a.x,y,a.x+a.w,y,pen); }
    else { const double x=xa.map(ann.x,a.x,a.x+a.w), y=ya.map(ann.y,a.y,a.y+a.h,true); draw_text(c,label,ann.text,x,y,ann.color); }
  }
  c.restore();
  if(!p.title_text.empty()) draw_text(c,titlefont,p.title_text,p.x+(p.width-titlefont.measure(p.title_text))/2,p.y+23,ink);
  if(!p.x_label.empty()) draw_text(c,label,p.x_label,a.x+(a.w-label.measure(p.x_label))/2,p.y+p.height-5,ink);
  if(!p.y_label.empty()) { c.save(); c.rotate(-90); draw_text(c,label,p.y_label,-(a.y+a.h/2+label.measure(p.y_label)/2),p.x+15,ink); c.restore(); }
  if(p.y2_enabled&&!p.y2_label.empty()) { c.save(); c.rotate(90); draw_text(c,label,p.y2_label,a.y+a.h/2-label.measure(p.y2_label)/2,-(p.x+p.width-5),ink); c.restore(); }
  draw_legend(c,p,a,small);
  c.restore();
}

}  // namespace lmvplot
