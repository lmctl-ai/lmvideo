#include "layout.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int checks=0, failures=0;
void check(bool ok,const char* label) {
  ++checks;
  if(ok) std::cout<<"ok "<<checks<<" - "<<label<<'\n';
  else { std::cerr<<"not ok "<<checks<<" - "<<label<<'\n';++failures; }
}
lmv::Color pixel(const lmv::Surface& surface,int x,int y) {
  const auto bytes=surface.rgba();const size_t p=(size_t(y)*surface.width()+x)*4;
  return {uint8_t(bytes[p]),uint8_t(bytes[p+1]),uint8_t(bytes[p+2]),uint8_t(bytes[p+3])};
}
lmvlayout::LayoutItem& shape_item(lmvlayout::Panel& panel,lmvlayout::Shape& shape,const std::string& color,lmvlayout::LayoutItem& item) {
  shape.fill=color;
  item.owner=&panel;item.kind=lmvlayout::ChildKind::Shape;item.child=&shape;
  panel.children.push_back(&item);return item;
}
void pack_expansion() {
  lmv::Surface surface(300,200);auto canvas=surface.canvas();canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot slot;slot.canvas=canvas;lmvlayout::Panel p(&slot,10,10,280,180);p.manager="pack";p.gap=5;
  lmvlayout::Shape header,body;lmvlayout::LayoutItem a,b;
  shape_item(p,header,"#ff0000",a);a.side="top";a.fill="both";a.preferred_h=30;a.constraint_set=true;
  shape_item(p,body,"#00aa00",b);b.side="top";b.fill="both";b.expand=true;b.preferred_h=20;b.constraint_set=true;
  lmvlayout::draw(p);
  check(pixel(surface,20,20)==lmv::Color(255,0,0,255),"pack keeps the fixed header at the top");
  check(pixel(surface,20,100)==lmv::Color(0,170,0,255),"pack expands the body into remaining space");
}
void weighted_grid() {
  lmv::Surface surface(300,200);auto canvas=surface.canvas();canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot slot;slot.canvas=canvas;lmvlayout::Panel p(&slot,10,10,280,180);p.manager="grid";p.columns=2;p.gap=10;p.column_weights[0]=1;p.column_weights[1]=2;p.row_weights[0]=1;p.row_weights[1]=2;
  lmvlayout::Shape left,right,span;lmvlayout::LayoutItem a,b,c;shape_item(p,left,"#ff0000",a);shape_item(p,right,"#0000ff",b);shape_item(p,span,"#ffff00",c);
  a.row=0;a.column=0;a.constraint_set=true;b.row=0;b.column=1;b.constraint_set=true;c.row=1;c.column=0;c.row_span=1;c.column_span=2;c.constraint_set=true;
  lmvlayout::draw(p);
  check(pixel(surface,40,40)==lmv::Color(255,0,0,255),"grid assigns the first weighted column");
  check(pixel(surface,170,40)==lmv::Color(0,0,255,255),"grid assigns the wider weighted column");
  check(pixel(surface,160,150)==lmv::Color(255,255,0,255),"grid spans one child across two weighted columns");
}
void border_regions() {
  lmv::Surface surface(300,200);auto canvas=surface.canvas();canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot slot;slot.canvas=canvas;lmvlayout::Panel p(&slot,10,10,280,180);p.manager="border";p.gap=5;
  lmvlayout::Shape top,left,center;lmvlayout::LayoutItem n,w,c;shape_item(p,top,"#ff0000",n);shape_item(p,left,"#00aa00",w);shape_item(p,center,"#0000ff",c);
  n.region="north";n.preferred_h=25;n.constraint_set=true;w.region="west";w.preferred_w=30;w.constraint_set=true;c.region="center";c.constraint_set=true;
  lmvlayout::draw(p);
  check(pixel(surface,20,20)==lmv::Color(255,0,0,255),"border manager reserves the north region");
  check(pixel(surface,20,50)==lmv::Color(0,170,0,255),"border manager reserves the west region");
  check(pixel(surface,100,80)==lmv::Color(0,0,255,255),"border manager fills the center remainder");
}
void place_and_flow() {
  lmv::Surface surface(240,120);auto canvas=surface.canvas();canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot slot;slot.canvas=canvas;lmvlayout::Panel p(&slot,0,0,240,120);p.manager="place";
  lmvlayout::Shape yellow;lmvlayout::LayoutItem item;shape_item(p,yellow,"#ffff00",item);item.place_x=25;item.place_y=30;item.place_w=30;item.place_h=25;item.placed=true;
  lmvlayout::draw(p);
  check(pixel(surface,35,40)==lmv::Color(255,255,0,255),"place uses explicit child coordinates");
  check(pixel(surface,10,10)==lmv::Color(255,255,255,255),"place leaves other canvas coordinates untouched");
}
void flow_and_box() {
  lmv::Surface flow_surface(220,100);auto flow_canvas=flow_surface.canvas();flow_canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot flow_slot;flow_slot.canvas=flow_canvas;lmvlayout::Panel flow(&flow_slot,0,0,220,100);flow.manager="flow_h";flow.gap=10;
  lmvlayout::Shape red,blue;lmvlayout::LayoutItem a,b;shape_item(flow,red,"#ff0000",a);shape_item(flow,blue,"#0000ff",b);
  a.preferred_w=20;a.preferred_h=20;b.preferred_w=30;b.preferred_h=20;lmvlayout::draw(flow);
  check(pixel(flow_surface,10,50)==lmv::Color(255,0,0,255),"flow_h places the first child in order");
  check(pixel(flow_surface,35,50)==lmv::Color(0,0,255,255),"flow_h advances by preferred size and gap");

  lmv::Surface box_surface(220,100);auto box_canvas=box_surface.canvas();box_canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot box_slot;box_slot.canvas=box_canvas;lmvlayout::Panel box(&box_slot,0,0,220,100);box.manager="box_h";
  lmvlayout::Shape first,last;lmvlayout::LayoutItem l,g,r;shape_item(box,first,"#ff0000",l);
  g.owner=&box;g.kind=lmvlayout::ChildKind::Glue;box.children.push_back(&g);shape_item(box,last,"#0000ff",r);
  l.preferred_w=20;l.preferred_h=20;r.preferred_w=20;r.preferred_h=20;g.weight=1;
  lmvlayout::draw(box);
  check(pixel(box_surface,10,50)==lmv::Color(255,0,0,255),"box_h keeps fixed children at the edges");
  check(pixel(box_surface,210,50)==lmv::Color(0,0,255,255),"box_h glue receives remaining space before the trailing child");

  lmv::Surface vertical_surface(100,220);auto vertical_canvas=vertical_surface.canvas();vertical_canvas.clear({255,255,255,255});
  lmvplot::CanvasSlot vertical_slot;vertical_slot.canvas=vertical_canvas;lmvlayout::Panel vertical(&vertical_slot,0,0,100,220);vertical.manager="box_v";
  lmvlayout::Shape upper,lower;lmvlayout::LayoutItem u,gap,bottom_item;shape_item(vertical,upper,"#ff0000",u);
  gap.owner=&vertical;gap.kind=lmvlayout::ChildKind::Glue;vertical.children.push_back(&gap);shape_item(vertical,lower,"#0000ff",bottom_item);
  u.preferred_w=20;u.preferred_h=20;bottom_item.preferred_w=20;bottom_item.preferred_h=20;gap.weight=1;lmvlayout::draw(vertical);
  check(pixel(vertical_surface,50,10)==lmv::Color(255,0,0,255),"box_v keeps the first child at the top");
  check(pixel(vertical_surface,50,210)==lmv::Color(0,0,255,255),"box_v glue pushes the final child to the bottom");
}
}
int main(){
  pack_expansion();weighted_grid();border_regions();place_and_flow();flow_and_box();
  std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
