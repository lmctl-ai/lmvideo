#include "plot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
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
  if (std::fabs(value) >= 1e6 || (value != 0 && std::fabs(value) < 1e-4) || (step==0 && std::fabs(value-std::round(value))>1e-8)) std::snprintf(text, sizeof(text), "%.3g", value);
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
    if(s->kind=="box"&&!s->boxes.empty()) {
      if(x_axis){lo=std::min(lo,0.5);hi=std::max(hi,static_cast<double>(s->boxes.size())+0.5);}
      else for(const auto& b:s->boxes){lo=std::min(lo,b[0]);hi=std::max(hi,b[4]);}
    }
    if(!x_axis && s->kind=="histogram" && !s->samples.empty() && s->bins>0) {
      double xlo=std::numeric_limits<double>::infinity(),xhi=-xlo;
      for(const auto& v:s->samples){xlo=std::min(xlo,v.x);xhi=std::max(xhi,v.x);}
      std::vector<int> counts(s->bins,0); double span=std::max(1e-12,xhi-xlo);
      for(const auto& v:s->samples){int b=std::min(s->bins-1,static_cast<int>((v.x-xlo)/span*s->bins));++counts[b];}
      hi=std::max(hi,static_cast<double>(*std::max_element(counts.begin(),counts.end())));lo=std::min(lo,0.0);
    }
    if (!x_axis) for(const auto& band:s->bands){lo=std::min(lo,band[1]);hi=std::max(hi,band[2]);}
  }
  if(!x_axis) for(const Series* s:p.series) if(s->kind=="stacked_bar") for(const auto& sample:s->samples) {
    double total=0;for(const Series* other:p.series)if(other->kind=="stacked_bar"&&other->stack_name==s->stack_name)for(const auto& v:other->samples)if(v.x==sample.x)total+=v.y;
    hi=std::max(hi,total);lo=std::min(lo,0.0);
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
  std::string scale;
  double threshold=1;
  double transform(double v) const {
    if(scale=="log") return std::log10(v);
    if(scale=="symlog") return std::copysign(std::log10(1.0+std::fabs(v)/threshold),v);
    return v;
  }
  double map(double value, double pixel0, double pixel1, bool reverse = false) const {
    const double a = transform(lo);
    const double b = transform(hi);
    const double v = transform(value);
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
double inverse_symlog(double v, double threshold) {
  return std::copysign(threshold*(std::pow(10.0,std::fabs(v))-1.0),v);
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
std::vector<double> date_ticks(double lo,double hi) {
  std::vector<double> out;const double days=(hi-lo)/86400.0;std::time_t t=static_cast<std::time_t>(std::max(0.0,lo));
  if(days<180) {
    int step=days<10?1:days<24?2:days<70?7:14;std::time_t interval=step*86400;std::time_t first=static_cast<std::time_t>(std::ceil(lo/interval))*interval;
    for(auto v=first;v<=hi&&out.size()<64;v+=interval)out.push_back(static_cast<double>(v));
  } else {
    std::tm tm{};gmtime_r(&t,&tm);tm.tm_mday=1;tm.tm_hour=0;tm.tm_min=0;tm.tm_sec=0;
    const int months=std::max(1,static_cast<int>(std::ceil(days/30.0/6.0)));const int step=months<=1?1:months<=2?2:months<=3?3:6;
    tm.tm_mon=((tm.tm_mon/step)+1)*step;tm.tm_year+=tm.tm_mon/12;tm.tm_mon%=12;std::time_t first=timegm(&tm);
    for(auto v=first;v<=hi&&out.size()<64;){if(v>=lo)out.push_back(static_cast<double>(v));std::tm next{};gmtime_r(&v,&next);next.tm_mon+=step;v=timegm(&next);}
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
  if(s.kind=="pie"||s.kind=="donut") {
    double total=0;for(const auto& v:s.samples)total+=std::max(0.0,v.y);if(total<=0)return;
    const double cx=a.x+a.w/2,cy=a.y+a.h/2,r=std::min(a.w,a.h)*0.39;double angle=-1.5707963267948966;
    const std::array<std::string,7> colors={"#2878b5","#df5b36","#38a169","#805ad5","#d69e2e","#0f766e","#d53f8c"};
    for(size_t i=0;i<s.samples.size();++i){double value=std::max(0.0,s.samples[i].y),next=angle+6.283185307179586*value/total;lmv::Path wedge;wedge.move_to(cx,cy);
      const int steps=std::max(2,static_cast<int>(std::ceil((next-angle)*24)));
      for(int k=0;k<=steps;++k){double q=angle+(next-angle)*k/steps;wedge.line_to(cx+r*std::cos(q),cy+r*std::sin(q));}wedge.close();
      c.draw_path(wedge,lmv::Paint().fill().color(lmv::parse_color(colors[i%colors.size()])).antialias(true));angle=next;}
    if(s.kind=="donut")c.draw_circle(cx,cy,r*s.donut_hole,lmv::Paint().fill().color(lmv::Color(255,255,255,255)).antialias(true));
    return;
  }
  if(s.kind=="box") {
    if(s.boxes.empty())return;const auto pen=stroke(s.color,s.width);const double step=a.w/s.boxes.size(),boxw=step*.5;
    for(size_t i=0;i<s.boxes.size();++i){const auto& b=s.boxes[i];double x=a.x+(i+.5)*step;double ymin=ya.map(b[0],a.y,a.y+a.h,true),yq1=ya.map(b[1],a.y,a.y+a.h,true),ym=ya.map(b[2],a.y,a.y+a.h,true),yq3=ya.map(b[3],a.y,a.y+a.h,true),ymax=ya.map(b[4],a.y,a.y+a.h,true);
      c.draw_line(x,ymin,x,ymax,pen);c.draw_line(x-boxw*.25,ymin,x+boxw*.25,ymin,pen);c.draw_line(x-boxw*.25,ymax,x+boxw*.25,ymax,pen);
      c.draw_rect(x-boxw/2,std::min(yq1,yq3),boxw,std::fabs(yq3-yq1),lmv::Paint().fill().color(lmv::parse_color(s.color)).alpha(.25f).antialias(true));c.draw_rect(x-boxw/2,std::min(yq1,yq3),boxw,std::fabs(yq3-yq1),pen);c.draw_line(x-boxw/2,ym,x+boxw/2,ym,pen);}
    return;
  }
  if (s.samples.empty()) return;
  c.save(); c.clip_rect(a.x, a.y, a.w, a.h);
  auto px = [&](double v) { return xa.map(v, a.x, a.x + a.w); };
  auto py = [&](double v) { return ya.map(v, a.y, a.y + a.h, true); };
  lmv::Paint line = stroke(s.color, s.width);
  if (s.dash == "dashed") line.dash(8, 5);
  else if (s.dash == "dotted") line.dash(2, 4);
  std::vector<std::pair<double,double>> dash_dot_points;
  if(!s.bands.empty()) {
    lmv::Path band; bool first=true;
    for(const auto& v:s.bands){if(first){band.move_to(px(v[0]),py(v[1]));first=false;}else band.line_to(px(v[0]),py(v[1]));}
    for(auto it=s.bands.rbegin();it!=s.bands.rend();++it) band.line_to(px((*it)[0]),py((*it)[2]));
    band.close(); c.draw_path(band,lmv::Paint().fill().color(lmv::parse_color(s.color)).alpha(0.18f).antialias(true));
  }
  if (s.kind == "histogram") {
    if(s.bins<=0)throw lmv::Error("histogram series requires .histogram(bin_count)");
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;for(const auto& v:s.samples){lo=std::min(lo,v.x);hi=std::max(hi,v.x);}if(lo==hi){lo-=.5;hi+=.5;}
    std::vector<int> counts(s.bins,0);for(const auto& v:s.samples){int b=std::min(s.bins-1,static_cast<int>((v.x-lo)/(hi-lo)*s.bins));++counts[b];}
    const double dx=(hi-lo)/s.bins,bw=a.w/s.bins*.86;for(int i=0;i<s.bins;++i){double x=lo+(i+.5)*dx;double y=counts[i];c.draw_rect(px(x)-bw/2,std::min(py(y),py(base)),bw,std::fabs(py(y)-py(base)),lmv::Paint().fill().color(lmv::parse_color(s.color)).antialias(true));}
  } else if (s.kind == "bar" || s.kind=="grouped_bar" || s.kind=="stacked_bar") {
    size_t bar_index = 0, bar_count = 0;
    for (auto* other : p.series) if (other->kind == "bar" || other->kind=="grouped_bar") { if (other == &s) bar_index = bar_count; ++bar_count; }
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
      double value=s.samples[i].y,bar_base=base;
      if(s.kind=="stacked_bar")for(auto* prior:p.series){if(prior==&s)break;if(prior->kind=="stacked_bar"&&prior->stack_name==s.stack_name)for(const auto& sample:prior->samples)if(sample.x==s.samples[i].x){bar_base+=sample.y;break;}}
      const double top = py(bar_base+value), bottom = py(bar_base);
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
lmv::Color palette_color(const std::string& palette,double t) {
  t=std::clamp(t,0.0,1.0);
  std::array<std::array<int,3>,5> stops{};
  if(palette=="magma") stops={{{0,0,4},{80,18,123},{182,54,121},{251,136,97},{252,253,191}}};
  else if(palette=="coolwarm") stops={{{59,76,192},{116,173,209},{221,221,221},{244,109,67},{180,4,38}}};
  else if(palette=="gray") stops={{{0,0,0},{64,64,64},{128,128,128},{192,192,192},{255,255,255}}};
  else stops={{{68,1,84},{59,82,139},{33,145,140},{94,201,98},{253,231,37}}};
  const double q=t*4; const size_t i=std::min<size_t>(3,static_cast<size_t>(q)); const double f=q-i;
  return lmv::Color(static_cast<uint8_t>(std::lround(stops[i][0]*(1-f)+stops[i+1][0]*f)),
                    static_cast<uint8_t>(std::lround(stops[i][1]*(1-f)+stops[i+1][1]*f)),
                    static_cast<uint8_t>(std::lround(stops[i][2]*(1-f)+stops[i+1][2]*f)),255);
}
double color_luma(const lmv::Color& c) { return 0.2126*c.r+0.7152*c.g+0.0722*c.b; }
void draw_heatmap_data(lmv::Canvas& c,const Heatmap& h,const Area& a,const Axis& xa,const Axis& ya) {
  if(h.cells.empty()) return;
  double lo=h.fixed_range?h.value_min:std::numeric_limits<double>::infinity(), hi=h.fixed_range?h.value_max:-lo;
  if(!h.fixed_range) for(const auto& cell:h.cells){lo=std::min(lo,cell.value);hi=std::max(hi,cell.value);}
  if(!(hi>lo)) {lo-=0.5;hi+=0.5;}
  const double cw=a.w/h.columns,ch=a.h/h.rows;
  c.save(); c.clip_rect(a.x,a.y,a.w,a.h);
  for(const auto& cell:h.cells) {
    const double x0=a.x+cell.column*cw, y0=a.y+a.h-(cell.row+1)*ch;
    auto col=palette_color(h.palette,(cell.value-lo)/(hi-lo));
    c.draw_rect(x0,y0,cw+0.5,ch+0.5,lmv::Paint().fill().color(col).antialias(false));
    if(h.show_labels && cw>=22 && ch>=15) { lmv::Font f(10); const auto t=number(cell.value); const auto ink=color_luma(col)>140?"#17202a":"#ffffff"; c.draw_text(t,x0+(cw-f.measure(t))/2,y0+ch/2+4,f,fill(ink)); }
  }
  if(h.contour_count>0 && h.rows>1 && h.columns>1) {
    auto pen=stroke(h.contour_color,h.contour_width);
    for(int level=1;level<=h.contour_count;++level) {
      const double threshold=lo+(hi-lo)*level/(h.contour_count+1.0);
      for(int r=0;r<h.rows-1;++r) for(int col=0;col<h.columns-1;++col) {
        const double v[4]={h.cells[r*h.columns+col].value,h.cells[r*h.columns+col+1].value,h.cells[(r+1)*h.columns+col+1].value,h.cells[(r+1)*h.columns+col].value};
        const double px[4]={a.x+(col+0.5)*cw,a.x+(col+1.5)*cw,a.x+(col+1.5)*cw,a.x+(col+0.5)*cw};
        const double py[4]={a.y+a.h-(r+0.5)*ch,a.y+a.h-(r+0.5)*ch,a.y+a.h-(r+1.5)*ch,a.y+a.h-(r+1.5)*ch};
        std::array<std::pair<double,double>,4> hits{}; int n=0;
        for(int e=0;e<4;++e){int j=(e+1)%4;if((v[e]<threshold)!=(v[j]<threshold)){double t=(threshold-v[e])/(v[j]-v[e]);hits[n++]={px[e]+t*(px[j]-px[e]),py[e]+t*(py[j]-py[e])};}}
        for(int i=1;i<n;i+=2)c.draw_line(hits[i-1].first,hits[i-1].second,hits[i].first,hits[i].second,pen);
      }
    }
  }
  c.restore();
  if(h.show_colorbar) {
    const double bx=a.x+a.w+24, bw=12;
    for(int y=0;y<static_cast<int>(a.h);++y) c.draw_line(bx,a.y+y,bx+bw,a.y+y,lmv::Paint().stroke().stroke_width(1).color(palette_color(h.palette,1.0-y/std::max(1.0,a.h-1))));
    c.draw_rect(bx,a.y,bw,a.h,stroke("#667085",0.8f)); lmv::Font f(9);
    draw_text(c,f,number(hi),bx+bw+3,a.y+8,"#667085"); draw_text(c,f,number(lo),bx+bw+3,a.y+a.h,"#667085");
  }
}
void draw_vectors(lmv::Canvas& c,const VectorField& field,const Area& a,const Axis& xa,const Axis& ya) {
  const auto pen=stroke(field.color,1.25f);
  if(field.streamlines) {
    const double sx=(xa.hi-xa.lo)/120.0,sy=(ya.hi-ya.lo)/120.0;
    for(const auto& seed:field.vectors) {double x=seed.x,y=seed.y;lmv::Path path;path.move_to(xa.map(x,a.x,a.x+a.w),ya.map(y,a.y,a.y+a.h,true));
      for(int step=0;step<48;++step){const Vector* nearest=nullptr;double best=std::numeric_limits<double>::infinity();for(const auto& v:field.vectors){double d=(v.x-x)*(v.x-x)+(v.y-y)*(v.y-y);if(d<best){best=d;nearest=&v;}}
        if(!nearest||std::hypot(nearest->u,nearest->v)<1e-12)break;x+=nearest->u*sx*field.scale;y+=nearest->v*sy*field.scale;if(x<xa.lo||x>xa.hi||y<ya.lo||y>ya.hi)break;path.line_to(xa.map(x,a.x,a.x+a.w),ya.map(y,a.y,a.y+a.h,true));}
      c.draw_path(path,pen);
    }return;
  }
  for(const auto& v:field.vectors) {
    const double x=xa.map(v.x,a.x,a.x+a.w),y=ya.map(v.y,a.y,a.y+a.h,true);
    double dx=v.u*field.scale*a.w/(xa.hi-xa.lo),dy=-v.v*field.scale*a.h/(ya.hi-ya.lo);
    double len=std::hypot(dx,dy); if(len>22){dx*=22/len;dy*=22/len;} if(len<1e-7)continue;
    c.draw_line(x-dx*.5,y-dy*.5,x+dx*.5,y+dy*.5,pen);
    const double ex=x+dx*.5,ey=y+dy*.5,al=std::atan2(dy,dx),head=4.5;
    c.draw_line(ex,ey,ex-head*std::cos(al-.55),ey-head*std::sin(al-.55),pen);
    c.draw_line(ex,ey,ex-head*std::cos(al+.55),ey-head*std::sin(al+.55),pen);
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
  const bool pie_only=!p.series.empty()&&p.heatmaps.empty()&&std::all_of(p.series.begin(),p.series.end(),[](const Series* s){return s->kind=="pie"||s->kind=="donut";});
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
  const bool colorbar=std::any_of(p.heatmaps.begin(),p.heatmaps.end(),[](const Heatmap* h){return h->show_colorbar;});
  const double left_margin=pie_only?16:64, right_margin=pie_only?16:(right?58:18)+(colorbar?42:0), top_margin=p.title_text.empty()?14:38, bottom_margin=pie_only?16:(p.x_label.empty()?34:54);
  Area a{p.x+left_margin,p.y+top_margin,p.width-left_margin-right_margin,p.height-top_margin-bottom_margin};
  if(a.w<24 || a.h<24) throw lmv::Error("Plot: plot rectangle is too small for labels and axes");
  Axis xa{xb.lo,xb.hi,p.x_scale,p.x_symlog_threshold}, ya{yb.lo,yb.hi,p.y_scale,p.y_symlog_threshold}, y2a{y2b.lo,y2b.hi,p.y_scale,p.y_symlog_threshold};
  lmv::Font small(11), label(13), titlefont(16); titlefont.embolden(true);
  const std::string ink="#344054", muted="#667085", grid="#e3e8ef";
  c.save();
  if(!p.background_color.empty()) c.draw_rect(p.x,p.y,p.width,p.height,fill(p.background_color));
  double sx=0,sy=0,sy2=0;
  auto xt=!p.x_date_format.empty()?date_ticks(xb.lo,xb.hi):p.x_scale=="log"?log_ticks(xb.lo,xb.hi,false):linear_ticks(p.x_scale=="symlog"?xa.transform(xb.lo):xb.lo,p.x_scale=="symlog"?xa.transform(xb.hi):xb.hi,sx);
  auto yt=p.y_scale=="log"?log_ticks(yb.lo,yb.hi,false):linear_ticks(p.y_scale=="symlog"?ya.transform(yb.lo):yb.lo,p.y_scale=="symlog"?ya.transform(yb.hi):yb.hi,sy);
  auto y2t=p.y_scale=="log"?log_ticks(y2b.lo,y2b.hi,false):linear_ticks(p.y_scale=="symlog"?y2a.transform(y2b.lo):y2b.lo,p.y_scale=="symlog"?y2a.transform(y2b.hi):y2b.hi,sy2);
  if(p.x_scale=="symlog") for(auto& v:xt) v=inverse_symlog(v,p.x_symlog_threshold);
  if(p.y_scale=="symlog") { for(auto& v:yt) v=inverse_symlog(v,p.y_symlog_threshold); for(auto& v:y2t) v=inverse_symlog(v,p.y_symlog_threshold); }
  if(!pie_only&&p.grid_visible) {
    for(double v:xt) if(p.x_scale!="log" || std::fmod(std::log10(v),1.0)==0) c.draw_line(xa.map(v,a.x,a.x+a.w),a.y,xa.map(v,a.x,a.x+a.w),a.y+a.h,stroke(grid,1));
    for(double v:yt) if(p.y_scale!="log" || std::fmod(std::log10(v),1.0)==0) c.draw_line(a.x,ya.map(v,a.y,a.y+a.h,true),a.x+a.w,ya.map(v,a.y,a.y+a.h,true),stroke(grid,1));
  }
  for(const auto* h:p.heatmaps) draw_heatmap_data(c,*h,a,xa,ya);
  if(!pie_only)c.draw_rect(a.x,a.y,a.w,a.h,stroke(ink,1.2f));
  if(!pie_only)for(double v:xt) {
    const double x=xa.map(v,a.x,a.x+a.w); c.draw_line(x,a.y+a.h,x,a.y+a.h+4,stroke(ink,1));
    if(p.x_date_format.size()) { std::time_t sec=static_cast<std::time_t>(v); std::tm tm{}; gmtime_r(&sec,&tm); char date[32]; std::strftime(date,sizeof(date),p.x_date_format.c_str(),&tm); const std::string t=date; draw_text(c,small,t,x-small.measure(t)/2,a.y+a.h+17,muted); }
    else if(p.x_scale!="log" || std::fabs(std::log10(v)-std::round(std::log10(v)))<1e-8) { const auto t=p.x_scale=="symlog"?number(v):p.x_scale=="log"?number(v):number(v,sx); draw_text(c,small,t,x-small.measure(t)/2,a.y+a.h+17,muted); }
  }
  if(!pie_only)for(double v:yt) {
    const double y=ya.map(v,a.y,a.y+a.h,true); c.draw_line(a.x-4,y,a.x,y,stroke(ink,1));
    if(p.y_scale!="log" || std::fabs(std::log10(v)-std::round(std::log10(v)))<1e-8) { const auto t=p.y_scale=="symlog"?number(v):p.y_scale=="log"?number(v):number(v,sy); draw_text(c,small,t,a.x-8-small.measure(t),y+4,muted); }
  }
  if(!pie_only&&p.y2_enabled) for(double v:y2t) {
    const double y=y2a.map(v,a.y,a.y+a.h,true); c.draw_line(a.x+a.w,y,a.x+a.w+4,y,stroke(ink,1));
    if(p.y_scale!="log" || std::fabs(std::log10(v)-std::round(std::log10(v)))<1e-8) { const auto t=p.y_scale=="symlog"?number(v):p.y_scale=="log"?number(v):number(v,sy2); draw_text(c,small,t,a.x+a.w+7,y+4,muted); }
  }
  for(const auto* s:p.series) {
    const Bounds& bounds=s->right_axis?y2b:yb;
    const double base=bounds.lo<=0 && bounds.hi>=0?0:bounds.lo;
    draw_series(c,p,*s,a,xa,s->right_axis?y2a:ya,base);
  }
  for(const auto* field:p.vector_fields) draw_vectors(c,*field,a,xa,ya);
  c.save(); c.clip_rect(a.x,a.y,a.w,a.h);
  for(const auto& ann:p.annotations) {
    if(ann.kind=="vline") { const double x=xa.map(ann.value,a.x,a.x+a.w); auto pen=stroke(ann.color,ann.width); pen.dash(4,4); c.draw_line(x,a.y,x,a.y+a.h,pen); }
    else if(ann.kind=="hline") { const double y=ya.map(ann.value,a.y,a.y+a.h,true); auto pen=stroke(ann.color,ann.width); pen.dash(4,4); c.draw_line(a.x,y,a.x+a.w,y,pen); }
    else { const double x=xa.map(ann.x,a.x,a.x+a.w), y=ya.map(ann.y,a.y,a.y+a.h,true); draw_text(c,label,ann.text,x,y,ann.color); }
  }
  c.restore();
  if(!p.title_text.empty()) draw_text(c,titlefont,p.title_text,p.x+(p.width-titlefont.measure(p.title_text))/2,p.y+23,ink);
  if(!pie_only&&!p.x_label.empty()) draw_text(c,label,p.x_label,a.x+(a.w-label.measure(p.x_label))/2,p.y+p.height-5,ink);
  if(!pie_only&&!p.y_label.empty()) { c.save(); c.rotate(-90); draw_text(c,label,p.y_label,-(a.y+a.h/2+label.measure(p.y_label)/2),p.x+15,ink); c.restore(); }
  if(p.y2_enabled&&!p.y2_label.empty()) { c.save(); c.rotate(90); draw_text(c,label,p.y2_label,a.y+a.h/2-label.measure(p.y2_label)/2,-(p.x+p.width-5),ink); c.restore(); }
  draw_legend(c,p,a,small);
  c.restore();
  for(auto& inset:p.insets) { inset.child->set_bounds(p.x+inset.x*p.width,p.y+inset.y*p.height,inset.width*p.width,inset.height*p.height); draw(*inset.child); }
}

}  // namespace lmvplot
