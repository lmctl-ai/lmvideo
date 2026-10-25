#include "layout.hpp"

#include "diagram.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

namespace lmvlayout {
namespace {
constexpr double kEpsilon = 1e-9;
void finite(double v, const char* what) {
  if (!std::isfinite(v)) throw lmv::Error(std::string("Layout: ") + what + " must be finite");
}
lmv::Paint fill(const std::string& color) {
  return lmv::Paint().fill().color(lmv::parse_color(color)).antialias(true);
}
lmv::Paint stroke(const std::string& color, double width) {
  return lmv::Paint().stroke().stroke_width(static_cast<float>(width)).color(lmv::parse_color(color)).antialias(true);
}
double clamp_size(double value, double low, double high) {
  return std::max(low, high >= 0 ? std::min(value, high) : value);
}
void validate_bounds(double w, double h, const char* what) {
  finite(w, what); finite(h, what);
  if (w <= 0 || h <= 0) throw lmv::Error(std::string("Layout: ") + what + " must be positive");
}
std::pair<double,double> intrinsic(LayoutItem& item) {
  switch (item.kind) {
    case ChildKind::Panel: {
      auto& p=*static_cast<Panel*>(item.child);
      return {std::max(160.0,p.min_w),std::max(100.0,p.min_h)};
    }
    case ChildKind::Plot: return {320,200};
    case ChildKind::Text: {
      auto& t=*static_cast<Text*>(item.child);
      lmv::Font f(static_cast<float>(t.size)); if(t.bold) f.embolden(true);
      return {f.measure(t.value)+8,t.size*1.6};
    }
    case ChildKind::Shape: {
      auto& s=*static_cast<Shape*>(item.child);
      if((s.kind=="box"||s.kind=="cluster")&&!s.label.empty()) {
        lmv::Font f(static_cast<float>(s.label_size));
        return {std::max(64.0,f.measure(s.label)+32.0),std::max(32.0,s.label_size*2.0)};
      }
      return {64,32};
    }
    case ChildKind::Image: {
      auto* image=static_cast<ImageView*>(item.child)->source;
      if(!image) throw lmv::Error("Layout: image item has no Image");
      return {double(image->width()),double(image->height())};
    }
    case ChildKind::Glue: return {0,0};
  }
  return {0,0};
}
double pref_w(LayoutItem& item) { return item.preferred_w >= 0 ? item.preferred_w : intrinsic(item).first; }
double pref_h(LayoutItem& item) { return item.preferred_h >= 0 ? item.preferred_h : intrinsic(item).second; }
double min_w(LayoutItem& item) {
  double value=std::max(item.min_w,item.preferred_w>=0&&item.max_w>=0?std::min(item.preferred_w,item.max_w):0.0);
  if(item.kind==ChildKind::Panel) value=std::max(value,static_cast<Panel*>(item.child)->min_w);
  return value;
}
double min_h(LayoutItem& item) {
  double value=std::max(item.min_h,item.preferred_h>=0&&item.max_h>=0?std::min(item.preferred_h,item.max_h):0.0);
  if(item.kind==ChildKind::Panel) value=std::max(value,static_cast<Panel*>(item.child)->min_h);
  return value;
}
double max_w(LayoutItem& item) {
  double value=item.max_w;
  if(item.kind==ChildKind::Panel&&static_cast<Panel*>(item.child)->max_w>=0)
    value=value<0?static_cast<Panel*>(item.child)->max_w:std::min(value,static_cast<Panel*>(item.child)->max_w);
  return value;
}
double max_h(LayoutItem& item) {
  double value=item.max_h;
  if(item.kind==ChildKind::Panel&&static_cast<Panel*>(item.child)->max_h>=0)
    value=value<0?static_cast<Panel*>(item.child)->max_h:std::min(value,static_cast<Panel*>(item.child)->max_h);
  return value;
}
std::pair<double,double> size_for(LayoutItem& item, double available_w, double available_h,
                                  bool fill_x, bool fill_y) {
  double w=fill_x?available_w:pref_w(item), h=fill_y?available_h:pref_h(item);
  w=clamp_size(std::max(w,min_w(item)),min_w(item),max_w(item));
  h=clamp_size(std::max(h,min_h(item)),min_h(item),max_h(item));
  if(w>available_w+kEpsilon||h>available_h+kEpsilon)
    throw lmv::Error("Layout: panel is too small for a child's minimum size");
  return {w,h};
}
std::string align_x(const LayoutItem& item,const Panel& p) { return item.align_x.empty()?p.align_x:item.align_x; }
std::string align_y(const LayoutItem& item,const Panel& p) { return item.align_y.empty()?p.align_y:item.align_y; }
Rect fitted(Rect slot, double w, double h, const std::string& ax, const std::string& ay) {
  double x=slot.x,y=slot.y;
  if(ax=="center") x+=(slot.w-w)/2; else if(ax=="right") x+=slot.w-w;
  if(ay=="center") y+=(slot.h-h)/2; else if(ay=="bottom") y+=slot.h-h;
  return {x,y,w,h};
}
void apply_fill(LayoutItem& item, const Panel& panel, Rect slot) {
  const bool fx=item.fill=="x"||item.fill=="both";
  const bool fy=item.fill=="y"||item.fill=="both";
  auto size=size_for(item,slot.w,slot.h,fx,fy);
  item.bounds=fitted(slot,size.first,size.second,align_x(item,panel),align_y(item,panel));
}
void draw_text(lmv::Canvas& c, Text& t, Rect r) {
  lmv::Font font(static_cast<float>(t.size)); if(t.bold) font.embolden(true);
  const double width=font.measure(t.value);
  std::string a=t.align;
  double x=r.x;
  if(a=="center") x+=std::max(0.0,(r.w-width)/2);
  else if(a=="right") x+=std::max(0.0,r.w-width);
  const double y=r.y+(r.h+t.size)/2.0;
  c.draw_text(t.value,static_cast<float>(x),static_cast<float>(y),font,fill(t.color));
}
void draw_shape(lmv::Canvas& c, Shape& s, Rect r) {
  // Every kind records its current rect so diagram anchors and cx()/cy() read
  // valid geometry after the layout pass, whatever assigned it.
  s.x=r.x; s.y=r.y; s.w=r.w; s.h=r.h;
  const bool diagram_kind=s.kind=="box"||s.kind=="diamond"||s.kind=="cylinder"||
                          s.kind=="note"||s.kind=="cluster"||s.kind=="cell";
  if(diagram_kind) { lmvdiagram::render_shape(c,s,r.x,r.y,r.w,r.h); return; }
  const bool has_fill=s.fill!=""&&s.fill!="transparent";
  const bool has_stroke=s.stroke!=""&&s.stroke!="transparent"&&s.stroke_width>0;
  if(s.kind=="line") {
    if(has_stroke) c.draw_line(r.x,r.y,r.x+r.w,r.y+r.h,stroke(s.stroke,s.stroke_width));
    return;
  }
  if(has_fill) {
    auto p=fill(s.fill);
    if(s.kind=="ellipse") c.draw_oval(r.x,r.y,r.w,r.h,p);
    else if(s.kind=="round_rect") c.draw_round_rect(r.x,r.y,r.w,r.h,s.radius,s.radius,p);
    else c.draw_rect(r.x,r.y,r.w,r.h,p);
  }
  if(has_stroke) {
    auto p=stroke(s.stroke,s.stroke_width);
    if(s.kind=="ellipse") c.draw_oval(r.x,r.y,r.w,r.h,p);
    else if(s.kind=="round_rect") c.draw_round_rect(r.x,r.y,r.w,r.h,s.radius,s.radius,p);
    else c.draw_rect(r.x,r.y,r.w,r.h,p);
  }
}
void draw_image(lmv::Canvas& c, ImageView& v, Rect r) {
  if(!v.source) throw lmv::Error("Layout: image item has no Image");
  const double iw=v.source->width(),ih=v.source->height();
  if(v.fit=="stretch") { c.draw_image_rect(*v.source,r.x,r.y,r.w,r.h); return; }
  const double scale=v.fit=="cover"?std::max(r.w/iw,r.h/ih):std::min(r.w/iw,r.h/ih);
  const double w=iw*scale,h=ih*scale;
  c.save(); c.clip_rect(r.x,r.y,r.w,r.h);
  c.draw_image_rect(*v.source,r.x+(r.w-w)/2,r.y+(r.h-h)/2,w,h);
  c.restore();
}
Rect content_rect(const Panel& p, Rect b) {
  return {b.x+p.pad_left,b.y+p.pad_top,b.w-p.pad_left-p.pad_right,b.h-p.pad_top-p.pad_bottom};
}
void arrange_pack(Panel& p, Rect area) {
  Rect rem=area;
  for(size_t i=0;i<p.children.size();++i) {
    LayoutItem& item=*p.children[i];
    if(!item.constraint_set) throw lmv::Error("Layout.pack: each child needs an explicit pack constraint");
    const bool vertical=item.side=="top"||item.side=="bottom";
    const double available=vertical?rem.h:rem.w;
    double main=vertical?pref_h(item):pref_w(item);
    if(item.expand) {
      double fixed=0,expand_base=0,total_weight=0;size_t gaps=0;
      for(size_t j=i;j<p.children.size();++j) {
        auto& other=*p.children[j];
        const bool same=(other.side=="top"||other.side=="bottom")==vertical;
        if(!same) continue;
        if(other.expand) { total_weight+=other.weight>0?other.weight:1; expand_base+=vertical?pref_h(other):pref_w(other); }
        else fixed+=vertical?pref_h(other):pref_w(other);
        if(j+1<p.children.size()) ++gaps;
      }
      const double extra=std::max(0.0,available-fixed-expand_base-p.gap*gaps);
      const double weight=item.weight>0?item.weight:1;
      if(total_weight>0) main+=extra*weight/total_weight;
    }
    main=clamp_size(std::max(main,vertical?min_h(item):min_w(item)),vertical?min_h(item):min_w(item),vertical?max_h(item):max_w(item));
    if(main>available+kEpsilon) throw lmv::Error("Layout.pack: requested children exceed available space");
    Rect slot;
    if(item.side=="top") { slot={rem.x,rem.y,rem.w,main}; rem.y+=main+p.gap; rem.h-=main+p.gap; }
    else if(item.side=="bottom") { slot={rem.x,rem.y+rem.h-main,rem.w,main}; rem.h-=main+p.gap; }
    else if(item.side=="left") { slot={rem.x,rem.y,main,rem.h}; rem.x+=main+p.gap; rem.w-=main+p.gap; }
    else if(item.side=="right") { slot={rem.x+rem.w-main,rem.y,main,rem.h}; rem.w-=main+p.gap; }
    else throw lmv::Error("Layout.pack: side must be top, bottom, left, or right");
    apply_fill(item,p,slot);
  }
}
void arrange_linear(Panel& p,Rect area,bool horizontal,bool distribute) {
  const size_t n=p.children.size(); if(!n) return;
  double base=0,total_weight=0;
  for(auto* it:p.children) {
    const double main=horizontal?pref_w(*it):pref_h(*it);
    base+=clamp_size(std::max(main,horizontal?min_w(*it):min_h(*it)),horizontal?min_w(*it):min_h(*it),horizontal?it->max_w:it->max_h);
    if(distribute) total_weight+=it->weight;
  }
  base+=p.gap*(n-1);
  if(base>(horizontal?area.w:area.h)+kEpsilon) throw lmv::Error("Layout.flow/box: preferred child sizes exceed panel");
  const double extra=std::max(0.0,(horizontal?area.w:area.h)-base);
  double pos=horizontal?area.x:area.y;
  for(auto* it:p.children) {
    double main=horizontal?pref_w(*it):pref_h(*it);
    main=clamp_size(std::max(main,horizontal?min_w(*it):min_h(*it)),horizontal?min_w(*it):min_h(*it),horizontal?max_w(*it):max_h(*it));
    if(distribute&&it->weight>0&&total_weight>0) main+=extra*it->weight/total_weight;
    Rect slot=horizontal?Rect{pos,area.y,main,area.h}:Rect{area.x,pos,area.w,main};
    const bool weighted=distribute&&it->weight>0;
    const bool fill_x=horizontal?weighted:(it->fill=="x"||it->fill=="both");
    const bool fill_y=horizontal?(it->fill=="y"||it->fill=="both"):weighted;
    auto size=size_for(*it,slot.w,slot.h,fill_x,fill_y);
    it->bounds=fitted(slot,size.first,size.second,align_x(*it,p),align_y(*it,p));
    pos+=main+p.gap;
  }
}
void arrange_grid(Panel& p,Rect area) {
  if(p.columns<1||p.columns>64) throw lmv::Error("Layout.grid: columns must be 1..64");
  int rows=0;
  for(auto* it:p.children) {
    if(!it->constraint_set||it->row<0||it->column<0||it->row_span<1||it->column_span<1||it->column+it->column_span>p.columns)
      throw lmv::Error("Layout.grid: every item needs a valid row, column, and positive span");
    rows=std::max(rows,it->row+it->row_span);
  }
  if(rows==0) return;
  std::vector<double> cw(p.columns),rh(rows);
  for(int i=0;i<p.columns;++i) { auto it=p.column_weights.find(i); cw[i]=it==p.column_weights.end()?1.0:it->second; }
  for(int i=0;i<rows;++i) { auto it=p.row_weights.find(i); rh[i]=it==p.row_weights.end()?1.0:it->second; }
  auto distribute_min=[&](bool horizontal,std::vector<double>& sizes) {
    for(auto* it:p.children) {
      const int start=horizontal?it->column:it->row, span=horizontal?it->column_span:it->row_span;
      const double required=horizontal?min_w(*it):min_h(*it);
      double have=p.gap*(span-1),weight_sum=0;
      for(int j=start;j<start+span;++j) { have+=sizes[j]; weight_sum+=sizes[j]; }
      if(required>have&&weight_sum>0) for(int j=start;j<start+span;++j) sizes[j]+=(required-have)*sizes[j]/weight_sum;
    }
  };
  distribute_min(true,cw); distribute_min(false,rh);
  auto scale_to=[&](std::vector<double>& sizes,double available,const char* axis) {
    const double total=std::accumulate(sizes.begin(),sizes.end(),0.0)+p.gap*(sizes.size()-1);
    if(total>available+kEpsilon) throw lmv::Error(std::string("Layout.grid: ")+axis+" minimum sizes exceed panel");
    const double extra=available-total,weights=std::accumulate(sizes.begin(),sizes.end(),0.0);
    for(auto& size:sizes) size+=extra*(weights>0?size/weights:1.0/sizes.size());
  };
  scale_to(cw,area.w,"column"); scale_to(rh,area.h,"row");
  std::vector<double> xs(p.columns),ys(rows); double pos=area.x;
  for(int i=0;i<p.columns;++i) { xs[i]=pos; pos+=cw[i]+p.gap; }
  pos=area.y; for(int i=0;i<rows;++i) { ys[i]=pos; pos+=rh[i]+p.gap; }
  std::set<std::pair<int,int>> occupied;
  for(auto* it:p.children) {
    if(!it->constraint_set) throw lmv::Error("Layout.border: each child needs an explicit border region");
    for(int r=it->row;r<it->row+it->row_span;++r) for(int c=it->column;c<it->column+it->column_span;++c)
      if(!occupied.emplace(r,c).second) throw lmv::Error("Layout.grid: child cells may not overlap");
    double w=p.gap*(it->column_span-1),h=p.gap*(it->row_span-1);
    for(int c=it->column;c<it->column+it->column_span;++c) w+=cw[c];
    for(int r=it->row;r<it->row+it->row_span;++r) h+=rh[r];
    Rect cell{xs[it->column],ys[it->row],w,h};
    const bool fx=it->sticky.find('e')!=std::string::npos||it->sticky.find('w')!=std::string::npos;
    const bool fy=it->sticky.find('n')!=std::string::npos||it->sticky.find('s')!=std::string::npos;
    auto size=size_for(*it,cell.w,cell.h,fx,fy);
    const std::string ax=it->sticky.find('w')!=std::string::npos?"left":it->sticky.find('e')!=std::string::npos?"right":align_x(*it,p);
    const std::string ay=it->sticky.find('n')!=std::string::npos?"top":it->sticky.find('s')!=std::string::npos?"bottom":align_y(*it,p);
    it->bounds=fitted(cell,size.first,size.second,ax,ay);
  }
}
void arrange_border(Panel& p,Rect area) {
  Rect rem=area; LayoutItem* center=nullptr;
  std::map<std::string,LayoutItem*> regions;
  for(auto* it:p.children) {
    if(it->region!="north"&&it->region!="south"&&it->region!="east"&&it->region!="west"&&it->region!="center")
      throw lmv::Error("Layout.border: region must be north, south, east, west, or center");
    if(!regions.emplace(it->region,it).second) throw lmv::Error("Layout.border: only one child is allowed in each region");
    if(it->region=="center") center=it;
  }
  auto strip=[&](const char* name,bool vertical,bool end) {
    auto found=regions.find(name); if(found==regions.end()) return;
    LayoutItem& it=*found->second;
    const double want=clamp_size(vertical?pref_h(it):pref_w(it),vertical?min_h(it):min_w(it),vertical?it.max_h:it.max_w);
    const double available=vertical?rem.h:rem.w;
    if(want>available+kEpsilon) throw lmv::Error("Layout.border: edge child exceeds available space");
    Rect slot;
    if(name==std::string("north")) { slot={rem.x,rem.y,rem.w,want}; rem.y+=want+p.gap; rem.h-=want+p.gap; }
    else if(name==std::string("south")) { slot={rem.x,rem.y+rem.h-want,rem.w,want}; rem.h-=want+p.gap; }
    else if(name==std::string("west")) { slot={rem.x,rem.y,want,rem.h}; rem.x+=want+p.gap; rem.w-=want+p.gap; }
    else { slot={rem.x+rem.w-want,rem.y,want,rem.h}; rem.w-=want+p.gap; }
    (void)end; apply_fill(it,p,slot);
  };
  strip("north",true,false); strip("south",true,true); strip("west",false,false); strip("east",false,true);
  if(center) { center->bounds=rem; }
}
void arrange(Panel& p,Rect area) {
  if(area.w<=0||area.h<=0) throw lmv::Error("Layout: panel has no available content area");
  if(p.manager=="pack") arrange_pack(p,area);
  else if(p.manager=="grid") arrange_grid(p,area);
  else if(p.manager=="border") arrange_border(p,area);
  else if(p.manager=="place") {
    for(auto* it:p.children) {
      if(!it->placed) throw lmv::Error("Layout.place: every child needs place(x, y, width, height)");
      validate_bounds(it->place_w,it->place_h,"placed size");
      Rect slot{area.x+it->place_x,area.y+it->place_y,it->place_w,it->place_h};
      if(slot.x<area.x-kEpsilon||slot.y<area.y-kEpsilon||slot.x+slot.w>area.x+area.w+kEpsilon||slot.y+slot.h>area.y+area.h+kEpsilon)
        throw lmv::Error("Layout.place: child rectangle must fit inside panel");
      it->bounds=slot;
    }
  } else if(p.manager=="flow_h") arrange_linear(p,area,true,false);
  else if(p.manager=="flow_v") arrange_linear(p,area,false,false);
  else if(p.manager=="box_h") arrange_linear(p,area,true,true);
  else if(p.manager=="box_v") arrange_linear(p,area,false,true);
  else throw lmv::Error("Panel.manager: unsupported manager");
}
void draw_panel(Panel& p,Rect bounds) {
  validate_bounds(bounds.w,bounds.h,"panel size");
  if(bounds.w+ kEpsilon<p.min_w||bounds.h+kEpsilon<p.min_h||(p.max_w>=0&&bounds.w>p.max_w+kEpsilon)||(p.max_h>=0&&bounds.h>p.max_h+kEpsilon))
    throw lmv::Error("Layout: panel bounds violate its min/max size");
  p.bounds=bounds;
  auto& c=p.canvas->get();
  c.save(); c.clip_rect(bounds.x,bounds.y,bounds.w,bounds.h);
  if(!p.background.empty()&&p.background!="transparent") c.draw_rect(bounds.x,bounds.y,bounds.w,bounds.h,fill(p.background));
  if(!p.edge_color.empty()&&p.edge_width>0) c.draw_rect(bounds.x,bounds.y,bounds.w,bounds.h,stroke(p.edge_color,p.edge_width));
  Rect area=content_rect(p,bounds);
  arrange(p,area);
  for(auto* item:p.children) {
    const Rect r=item->bounds;
    switch(item->kind) {
      case ChildKind::Panel: draw_panel(*static_cast<Panel*>(item->child),r); break;
      case ChildKind::Plot: {
        auto& plot=*static_cast<lmvplot::Plot*>(item->child); plot.set_bounds(r.x,r.y,r.w,r.h); lmvplot::draw(plot); break;
      }
      case ChildKind::Text: draw_text(c,*static_cast<Text*>(item->child),r); break;
      case ChildKind::Shape: draw_shape(c,*static_cast<Shape*>(item->child),r); break;
      case ChildKind::Image: draw_image(c,*static_cast<ImageView*>(item->child),r); break;
      case ChildKind::Glue: break;
    }
  }
  c.restore();
}
}  // namespace

Panel::Panel(lmvplot::CanvasSlot* slot) : canvas(slot) {}
Panel::Panel(lmvplot::CanvasSlot* slot,double x,double y,double width,double height) : canvas(slot),bounds{x,y,width,height} {
  finite(x,"panel x"); finite(y,"panel y"); validate_bounds(width,height,"panel size");
}
void draw(Panel& panel) {
  if(!panel.canvas) throw lmv::Error("Layout: panel has no Canvas");
  draw_panel(panel,panel.bounds);
}

}  // namespace lmvlayout
