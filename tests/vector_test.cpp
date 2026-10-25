#include "lmvideo.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
int tests=0,failures=0;
void check(bool ok,const char* name){++tests;if(ok)std::cout<<"ok "<<tests<<" - "<<name<<'\n';else{std::cerr<<"not ok "<<tests<<" - "<<name<<'\n';++failures;}}
size_t occurrences(const std::string& text,const std::string& needle){size_t n=0,pos=0;while((pos=text.find(needle,pos))!=std::string::npos){++n;pos+=needle.size();}return n;}
}

int main(){
  lmv::PictureRecorder recorder(160,100);
  auto canvas=recorder.canvas();
  canvas.clear({255,255,255,255});
  canvas.draw_rect(12,12,136,76,lmv::Paint().fill().color({31,91,145,255}));
  lmv::Surface swatch_surface(8,8);
  swatch_surface.canvas().clear({40,190,90,255});
  auto swatch=swatch_surface.snapshot();
  canvas.draw_image(swatch,138,82);
  lmv::Font font(15);
  canvas.draw_text("Skia vector",20,52,font,lmv::Paint().fill().color({255,255,255,255}));
  auto picture=recorder.finish();
  const auto png1=picture.encode_png(1),png2=picture.encode_png(2),png3=picture.encode_png(3);
  auto image1=lmv::Image::from_bytes(png1),image2=lmv::Image::from_bytes(png2),image3=lmv::Image::from_bytes(png3);
  check(image1.width()==160&&image1.height()==100,"recorded picture exports a 1x PNG");
  check(image2.width()==320&&image2.height()==200&&image3.width()==480&&image3.height()==300,"picture renders at 2x and 3x logical scale");
  check(png1==picture.encode_png(1),"raster picture output is byte-deterministic");
  lmv::Surface direct_pixels(160,100),decoded_pixels(160,100);
  auto direct_canvas=direct_pixels.canvas();
  picture.playback(direct_canvas);
  decoded_pixels.canvas().draw_image(image1,0,0);
  check(direct_pixels.rgba()==decoded_pixels.rgba(),"decoded PNG pixels exactly match direct Picture playback");
  lmv::Surface scaled_picture(320,200),scaled_png(320,200);
  auto scaled_canvas=scaled_picture.canvas();
  picture.draw(scaled_canvas,0,0,320,200);
  scaled_png.canvas().draw_image(image2,0,0);
  check(scaled_picture.rgba()==scaled_png.rgba(),"Picture.draw composes into an arbitrary Canvas rectangle");

  const auto svg=picture.encode_svg();
  check(svg.find("<svg")!=std::string::npos&&svg.find("<text")==std::string::npos,"SVG output includes paths with text outlined");
  check(svg==picture.encode_svg(),"SVG output is byte-deterministic");
  auto parsed=lmv::Svg::parse(svg);
  lmv::Surface roundtrip(160,100);
  roundtrip.canvas().clear({255,255,255,255});
  roundtrip.canvas().draw_svg(parsed,0,0,160,100);
  const auto rgba=roundtrip.rgba();
  const size_t sample=(50*160+50)*4;
  check(static_cast<uint8_t>(rgba[sample+2])>100,"host SVG parser renders recorded output back to pixels");

  const auto pdf=picture.encode_pdf();
  check(pdf.rfind("%PDF-",0)==0&&pdf.find("/FontFile")!=std::string::npos,"picture exports vector text with embedded PDF fonts");
  check(pdf==picture.encode_pdf(),"single-page PDF output is byte-deterministic");
  const auto skp=picture.serialize();
  check(skp==picture.serialize(),"serialized Skia picture output is byte-deterministic");
  auto restored=lmv::Picture::deserialize(skp);
  check(restored.width()==160&&restored.height()==100&&restored.encode_png()==png1,"SkPicture serialization round-trips the recorded drawing");

  lmv::PdfDocument report(160,100);
  report.add_page(picture);
  report.add_page(restored);
  check(report.pages()==2,"PDF document counts its recorded pages");
  const auto multipage=report.finish();
  check(multipage.rfind("%PDF-",0)==0&&occurrences(multipage,"<</Type /Page\n")>=2&&multipage.find("/Count 2")!=std::string::npos,"PDF document contains multiple vector pages");

  lmv::Surface raster(40,30);
  raster.canvas().clear({220,30,40,255});
  check(raster.encode_svg().find("<svg")!=std::string::npos,"raster Surface can be wrapped in an SVG image");
  check(raster.encode_pdf().rfind("%PDF-",0)==0,"raster Surface can be wrapped in a PDF page");
  std::cout<<tests<<" checks, "<<failures<<" failures\n";
  return failures?1:0;
}
