#include "lmvideo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#include "include/codec/SkCodec.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkBlurMaskFilter.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradientShader.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/ports/SkFontMgr_data.h"
#include "include/ports/SkFontMgr_empty.h"
#include "include/utils/SkParsePath.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"

// Embed bundled DejaVu Sans TTF into the binary so rendering never depends on system fonts.
__asm__(
  ".section .rodata\n"
  ".global dejavu_sans_start\n"
  ".global dejavu_sans_end\n"
  "dejavu_sans_start:\n"
  ".incbin \"assets/fonts/DejaVuSans.ttf\"\n"
  "dejavu_sans_end:\n"
  ".text\n"
);
extern "C" const unsigned char dejavu_sans_start[];
extern "C" const unsigned char dejavu_sans_end[];

namespace lmv {

namespace {

inline std::string trim_and_lower(const std::string& str) {
    size_t first = 0;
    while (first < str.size() && std::isspace(static_cast<unsigned char>(str[first]))) {
        first++;
    }
    size_t last = str.size();
    while (last > first && std::isspace(static_cast<unsigned char>(str[last - 1]))) {
        last--;
    }
    std::string res;
    res.reserve(last - first);
    for (size_t i = first; i < last; ++i) {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(str[i]))));
    }
    return res;
}

inline int parse_hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

sk_sp<SkFontMgr> get_font_mgr() {
    static sk_sp<SkFontMgr> s_mgr = []() {
        size_t font_size = dejavu_sans_end - dejavu_sans_start;
        sk_sp<SkData> fontData = SkData::MakeWithoutCopy(dejavu_sans_start, font_size);
        sk_sp<SkData> dataArray[] = { fontData };
        return SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(dataArray, 1));
    }();
    return s_mgr;
}

sk_sp<SkTypeface> get_bundled_typeface() {
    static sk_sp<SkTypeface> s_tf = []() {
        size_t font_size = dejavu_sans_end - dejavu_sans_start;
        sk_sp<SkData> fontData = SkData::MakeWithoutCopy(dejavu_sans_start, font_size);
        auto tf = get_font_mgr()->makeFromData(fontData);
        if (!tf) {
            throw Error("Failed to initialize bundled DejaVu Sans typeface");
        }
        return tf;
    }();
    return s_tf;
}

void configure_deterministic_font(SkFont& font) {
    font.setHinting(SkFontHinting::kNone);
    font.setEdging(SkFont::Edging::kAntiAlias);
    font.setSubpixel(false);
}

} // namespace

Color parse_color(const std::string& str) {
    std::string s = trim_and_lower(str);
    if (s.empty()) {
        throw Error("Color string is empty");
    }

    if (s[0] == '#') {
        if (s.size() == 4) { // #rgb
            int r = parse_hex_char(s[1]);
            int g = parse_hex_char(s[2]);
            int b = parse_hex_char(s[3]);
            if (r < 0 || g < 0 || b < 0) {
                throw Error("Invalid hex characters in color: " + str);
            }
            return Color(static_cast<uint8_t>(r * 17),
                         static_cast<uint8_t>(g * 17),
                         static_cast<uint8_t>(b * 17),
                         255);
        } else if (s.size() == 7) { // #rrggbb
            int r1 = parse_hex_char(s[1]), r2 = parse_hex_char(s[2]);
            int g1 = parse_hex_char(s[3]), g2 = parse_hex_char(s[4]);
            int b1 = parse_hex_char(s[5]), b2 = parse_hex_char(s[6]);
            if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
                throw Error("Invalid hex characters in color: " + str);
            }
            return Color(static_cast<uint8_t>((r1 << 4) | r2),
                         static_cast<uint8_t>((g1 << 4) | g2),
                         static_cast<uint8_t>((b1 << 4) | b2),
                         255);
        } else if (s.size() == 9) { // #rrggbbaa
            int r1 = parse_hex_char(s[1]), r2 = parse_hex_char(s[2]);
            int g1 = parse_hex_char(s[3]), g2 = parse_hex_char(s[4]);
            int b1 = parse_hex_char(s[5]), b2 = parse_hex_char(s[6]);
            int a1 = parse_hex_char(s[7]), a2 = parse_hex_char(s[8]);
            if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0 || a1 < 0 || a2 < 0) {
                throw Error("Invalid hex characters in color: " + str);
            }
            return Color(static_cast<uint8_t>((r1 << 4) | r2),
                         static_cast<uint8_t>((g1 << 4) | g2),
                         static_cast<uint8_t>((b1 << 4) | b2),
                         static_cast<uint8_t>((a1 << 4) | a2));
        } else {
            throw Error("Invalid hex color length in: " + str);
        }
    }

    if (s == "black")       return Color(0, 0, 0, 255);
    if (s == "white")       return Color(255, 255, 255, 255);
    if (s == "red")         return Color(255, 0, 0, 255);
    if (s == "green")       return Color(0, 255, 0, 255);
    if (s == "blue")        return Color(0, 0, 255, 255);
    if (s == "transparent") return Color(0, 0, 0, 0);
    if (s == "yellow")      return Color(255, 255, 0, 255);
    if (s == "cyan")        return Color(0, 255, 255, 255);
    if (s == "magenta")     return Color(255, 0, 255, 255);
    if (s == "gray" || s == "grey") return Color(128, 128, 128, 255);

    throw Error("Unknown color name or invalid format: " + str);
}

// ============================================================================
// Paint Implementation
// ============================================================================

struct Paint::Impl {
    SkPaint sk_paint;
    Impl() {
        sk_paint.setColor(SK_ColorBLACK);
        sk_paint.setAntiAlias(true);
        sk_paint.setStyle(SkPaint::kFill_Style);
    }
};

Paint::Paint() : pimpl_(std::make_unique<Impl>()) {}
Paint::~Paint() = default;

Paint::Paint(const Paint& other)
    : pimpl_(std::make_unique<Impl>(*other.pimpl_)) {}

Paint& Paint::operator=(const Paint& other) {
    if (this != &other) {
        pimpl_ = std::make_unique<Impl>(*other.pimpl_);
    }
    return *this;
}

Paint::Paint(Paint&& other) noexcept = default;
Paint& Paint::operator=(Paint&& other) noexcept = default;

Paint& Paint::color(Color c) {
    pimpl_->sk_paint.setShader(nullptr);
    pimpl_->sk_paint.setColor(SkColorSetARGB(c.a, c.r, c.g, c.b));
    return *this;
}

Paint& Paint::antialias(bool aa) {
    pimpl_->sk_paint.setAntiAlias(aa);
    return *this;
}

Paint& Paint::fill() {
    pimpl_->sk_paint.setStyle(SkPaint::kFill_Style);
    return *this;
}

Paint& Paint::stroke() {
    pimpl_->sk_paint.setStyle(SkPaint::kStroke_Style);
    return *this;
}

Paint& Paint::stroke_width(float w) {
    if (w < 0.0f || std::isnan(w)) {
        throw Error("Negative or invalid stroke width: " + std::to_string(w));
    }
    pimpl_->sk_paint.setStrokeWidth(w);
    return *this;
}

Paint& Paint::stroke_cap(const std::string& cap) {
    std::string c = trim_and_lower(cap);
    if (c == "butt") {
        pimpl_->sk_paint.setStrokeCap(SkPaint::kButt_Cap);
    } else if (c == "round") {
        pimpl_->sk_paint.setStrokeCap(SkPaint::kRound_Cap);
    } else if (c == "square") {
        pimpl_->sk_paint.setStrokeCap(SkPaint::kSquare_Cap);
    } else {
        throw Error("Unknown stroke cap: " + cap);
    }
    return *this;
}

Paint& Paint::stroke_join(const std::string& join) {
    std::string j = trim_and_lower(join);
    if (j == "miter") {
        pimpl_->sk_paint.setStrokeJoin(SkPaint::kMiter_Join);
    } else if (j == "round") {
        pimpl_->sk_paint.setStrokeJoin(SkPaint::kRound_Join);
    } else if (j == "bevel") {
        pimpl_->sk_paint.setStrokeJoin(SkPaint::kBevel_Join);
    } else {
        throw Error("Unknown stroke join: " + join);
    }
    return *this;
}

Paint& Paint::linear_gradient(float x0, float y0, float x1, float y1, Color c0, Color c1) {
    SkPoint pts[2] = {SkPoint::Make(x0, y0), SkPoint::Make(x1, y1)};
    SkColor colors[2] = {SkColorSetARGB(c0.a, c0.r, c0.g, c0.b), SkColorSetARGB(c1.a, c1.r, c1.g, c1.b)};
    pimpl_->sk_paint.setShader(SkGradientShader::MakeLinear(pts, colors, nullptr, 2, SkTileMode::kClamp));
    return *this;
}

Paint& Paint::dash(float on, float off) {
    if (on < 0.0f || off < 0.0f || std::isnan(on) || std::isnan(off)) {
        throw Error("Negative or invalid dash length");
    }
    if (on > 0.0f && off > 0.0f) {
        SkScalar intervals[2] = {on, off};
        pimpl_->sk_paint.setPathEffect(SkDashPathEffect::Make(intervals, 0.0f));
    } else {
        pimpl_->sk_paint.setPathEffect(nullptr);
    }
    return *this;
}

Paint& Paint::blur(float sigma) {
    if (sigma < 0.0f || std::isnan(sigma)) {
        throw Error("Negative or invalid blur sigma");
    }
    if (sigma > 0.0f) {
        pimpl_->sk_paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    } else {
        pimpl_->sk_paint.setMaskFilter(nullptr);
    }
    return *this;
}

Paint& Paint::alpha(float a) {
    if (a < 0.0f || a > 1.0f || std::isnan(a)) {
        throw Error("Alpha must be between 0.0 and 1.0, got: " + std::to_string(a));
    }
    pimpl_->sk_paint.setAlphaf(a);
    return *this;
}

// ============================================================================
// Path Implementation
// ============================================================================

struct Path::Impl {
    SkPath sk_path;
    Impl() = default;
    Impl(const SkPath& p) : sk_path(p) {}
};

Path::Path() : pimpl_(std::make_unique<Impl>()) {}
Path::~Path() = default;

Path::Path(const Path& other)
    : pimpl_(std::make_unique<Impl>(*other.pimpl_)) {}

Path& Path::operator=(const Path& other) {
    if (this != &other) {
        pimpl_ = std::make_unique<Impl>(*other.pimpl_);
    }
    return *this;
}

Path::Path(Path&& other) noexcept = default;
Path& Path::operator=(Path&& other) noexcept = default;

Path& Path::move_to(float x, float y) {
    pimpl_->sk_path.moveTo(x, y);
    return *this;
}

Path& Path::line_to(float x, float y) {
    pimpl_->sk_path.lineTo(x, y);
    return *this;
}

Path& Path::quad_to(float x1, float y1, float x2, float y2) {
    pimpl_->sk_path.quadTo(x1, y1, x2, y2);
    return *this;
}

Path& Path::cubic_to(float x1, float y1, float x2, float y2, float x3, float y3) {
    pimpl_->sk_path.cubicTo(x1, y1, x2, y2, x3, y3);
    return *this;
}

Path& Path::close() {
    pimpl_->sk_path.close();
    return *this;
}

Path& Path::add_rect(float x, float y, float w, float h) {
    if (w < 0.0f || h < 0.0f) {
        throw Error("Negative rect dimension in add_rect");
    }
    pimpl_->sk_path.addRect(SkRect::MakeXYWH(x, y, w, h));
    return *this;
}

Path& Path::add_oval(float x, float y, float w, float h) {
    if (w < 0.0f || h < 0.0f) {
        throw Error("Negative oval dimension in add_oval");
    }
    pimpl_->sk_path.addOval(SkRect::MakeXYWH(x, y, w, h));
    return *this;
}

Path& Path::add_circle(float cx, float cy, float r) {
    if (r < 0.0f) {
        throw Error("Negative circle radius in add_circle");
    }
    pimpl_->sk_path.addCircle(cx, cy, r);
    return *this;
}

Path& Path::add_round_rect(float x, float y, float w, float h, float rx, float ry) {
    if (w < 0.0f || h < 0.0f || rx < 0.0f || ry < 0.0f) {
        throw Error("Negative dimension or radius in add_round_rect");
    }
    pimpl_->sk_path.addRoundRect(SkRect::MakeXYWH(x, y, w, h), rx, ry);
    return *this;
}

Path Path::from_svg(const std::string& svg_path_data) {
    if (svg_path_data.empty()) {
        throw Error("Empty SVG path data string");
    }
    auto opt_path = SkParsePath::FromSVGString(svg_path_data.c_str());
    if (!opt_path.has_value()) {
        throw Error("Failed to parse SVG path data: " + svg_path_data);
    }
    Path p;
    p.pimpl_->sk_path = *opt_path;
    return p;
}

Path Path::translate(float dx, float dy) const {
    Path copy = *this;
    copy.pimpl_->sk_path.transform(SkMatrix::Translate(dx, dy));
    return copy;
}

Path Path::scale(float sx, float sy) const {
    Path copy = *this;
    copy.pimpl_->sk_path.transform(SkMatrix::Scale(sx, sy));
    return copy;
}

Path Path::rotate(float degrees) const {
    Path copy = *this;
    copy.pimpl_->sk_path.transform(SkMatrix::RotateDeg(degrees));
    return copy;
}

// ============================================================================
// Font Implementation
// ============================================================================

struct Font::Impl {
    SkFont sk_font;
    Impl(sk_sp<SkTypeface> tf, float size) : sk_font(tf, size) {
        configure_deterministic_font(sk_font);
    }
};

Font::Font(float size) {
    if (size <= 0.0f || size > 16384.0f || std::isnan(size)) {
        throw Error("Invalid font size: " + std::to_string(size));
    }
    pimpl_ = std::make_shared<Impl>(get_bundled_typeface(), size);
}

Font::~Font() = default;
Font::Font(const Font& other) = default;
Font& Font::operator=(const Font& other) = default;
Font::Font(Font&& other) noexcept = default;
Font& Font::operator=(Font&& other) noexcept = default;

Font Font::from_file(const std::string& path, float size) {
    if (path.empty()) {
        throw Error("Empty font file path");
    }
    if (size <= 0.0f || size > 16384.0f || std::isnan(size)) {
        throw Error("Invalid font size: " + std::to_string(size));
    }
    auto tf = get_font_mgr()->makeFromFile(path.c_str());
    if (!tf) {
        throw Error("Failed to load font from file: " + path);
    }
    Font f(size);
    f.pimpl_ = std::make_shared<Impl>(tf, size);
    return f;
}

float Font::size() const {
    return pimpl_->sk_font.getSize();
}

float Font::measure(const std::string& text) const {
    return pimpl_->sk_font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8, nullptr, nullptr);
}

Font& Font::embolden(bool embolden) {
    pimpl_->sk_font.setEmbolden(embolden);
    return *this;
}

// ============================================================================
// Image Implementation
// ============================================================================

struct Image::Impl {
    sk_sp<SkImage> img;
    Impl() = default;
    explicit Impl(sk_sp<SkImage> i) : img(std::move(i)) {}
};

Image::Image() : pimpl_(std::make_shared<Impl>()) {}
Image::~Image() = default;
Image::Image(const Image& other) = default;
Image& Image::operator=(const Image& other) = default;
Image::Image(Image&& other) noexcept = default;
Image& Image::operator=(Image&& other) noexcept = default;

Image::Image(std::shared_ptr<Impl> impl) : pimpl_(std::move(impl)) {}

Image Image::from_file(const std::string& path) {
    if (path.empty()) {
        throw Error("Image path cannot be empty");
    }
    auto data = SkData::MakeFromFileName(path.c_str());
    if (!data) {
        throw Error("Failed to read image file: " + path);
    }
    auto img = SkImages::DeferredFromEncodedData(data);
    if (!img) {
        throw Error("Failed to decode image from: " + path);
    }
    return Image(std::make_shared<Impl>(img));
}

Image Image::from_bytes(const std::string& bytes) {
    if (bytes.empty()) {
        throw Error("Image bytes cannot be empty");
    }
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto img = SkImages::DeferredFromEncodedData(data);
    if (!img) {
        throw Error("Failed to decode image from bytes");
    }
    return Image(std::make_shared<Impl>(img));
}

int Image::width() const {
    return (pimpl_ && pimpl_->img) ? pimpl_->img->width() : 0;
}

int Image::height() const {
    return (pimpl_ && pimpl_->img) ? pimpl_->img->height() : 0;
}

// ============================================================================
// Svg Implementation
// ============================================================================

struct Svg::Impl {
    sk_sp<SkSVGDOM> dom;
    float width = 0.0f;
    float height = 0.0f;
    Impl() = default;
    Impl(sk_sp<SkSVGDOM> d, float w, float h) : dom(std::move(d)), width(w), height(h) {}
};

Svg::Svg() : pimpl_(std::make_shared<Impl>()) {}
Svg::~Svg() = default;
Svg::Svg(const Svg& other) = default;
Svg& Svg::operator=(const Svg& other) = default;
Svg::Svg(Svg&& other) noexcept = default;
Svg& Svg::operator=(Svg&& other) noexcept = default;

Svg Svg::parse(const std::string& xml) {
    if (xml.empty()) {
        throw Error("Empty SVG XML string");
    }
    SkMemoryStream stream(xml.data(), xml.size());
    SkSVGDOM::Builder builder;
    builder.setFontManager(get_font_mgr());
    builder.setTextShapingFactory(SkShapers::Primitive::Factory());
    auto dom = builder.make(stream);
    if (!dom) {
        throw Error("Failed to parse SVG XML");
    }

    float w = 0.0f;
    float h = 0.0f;
    SkSize cs = dom->containerSize();
    if (cs.width() > 0.0f && cs.height() > 0.0f) {
        w = cs.width();
        h = cs.height();
    } else if (dom->getRoot()) {
        auto vb = dom->getRoot()->getViewBox();
        if (vb.has_value()) {
            w = vb->width();
            h = vb->height();
        }
    }

    Svg res;
    res.pimpl_ = std::make_shared<Impl>(dom, w, h);
    return res;
}

float Svg::width() const {
    return pimpl_ ? pimpl_->width : 0.0f;
}

float Svg::height() const {
    return pimpl_ ? pimpl_->height : 0.0f;
}

// ============================================================================
// Canvas Implementation
// ============================================================================

struct Canvas::Impl {
    SkCanvas* canvas = nullptr;
    explicit Impl(SkCanvas* c) : canvas(c) {}
};

Canvas::Canvas(Impl* impl) : pimpl_(impl) {}

void Canvas::clear(Color c) {
    pimpl_->canvas->clear(SkColorSetARGB(c.a, c.r, c.g, c.b));
}

void Canvas::draw_rect(float x, float y, float w, float h, const Paint& p) {
    pimpl_->canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), p.impl().sk_paint);
}

void Canvas::draw_round_rect(float x, float y, float w, float h, float rx, float ry, const Paint& p) {
    pimpl_->canvas->drawRoundRect(SkRect::MakeXYWH(x, y, w, h), rx, ry, p.impl().sk_paint);
}

void Canvas::draw_oval(float x, float y, float w, float h, const Paint& p) {
    pimpl_->canvas->drawOval(SkRect::MakeXYWH(x, y, w, h), p.impl().sk_paint);
}

void Canvas::draw_circle(float cx, float cy, float r, const Paint& p) {
    pimpl_->canvas->drawCircle(cx, cy, r, p.impl().sk_paint);
}

void Canvas::draw_line(float x0, float y0, float x1, float y1, const Paint& p) {
    pimpl_->canvas->drawLine(x0, y0, x1, y1, p.impl().sk_paint);
}

void Canvas::draw_path(const Path& path, const Paint& p) {
    pimpl_->canvas->drawPath(path.impl().sk_path, p.impl().sk_paint);
}

void Canvas::draw_text(const std::string& text, float x, float y, const Font& f, const Paint& p) {
    pimpl_->canvas->drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y,
                                  f.impl().sk_font, p.impl().sk_paint);
}

void Canvas::draw_image(const Image& img, float x, float y) {
    if (!img.impl().img) {
        throw Error("Cannot draw null or invalid Image");
    }
    pimpl_->canvas->drawImage(img.impl().img.get(), x, y, SkSamplingOptions());
}

void Canvas::draw_image_rect(const Image& img, float x, float y, float w, float h) {
    if (!img.impl().img) {
        throw Error("Cannot draw null or invalid Image");
    }
    pimpl_->canvas->drawImageRect(img.impl().img.get(), SkRect::MakeXYWH(x, y, w, h), SkSamplingOptions());
}

void Canvas::draw_svg(const Svg& svg, float x, float y, float w, float h) {
    if (!svg.impl().dom) {
        throw Error("Cannot draw null or invalid Svg");
    }
    if (w <= 0.0f || h <= 0.0f) {
        throw Error("Invalid SVG viewport dimensions in draw_svg");
    }
    pimpl_->canvas->save();
    pimpl_->canvas->translate(x, y);
    float orig_w = svg.width();
    float orig_h = svg.height();
    if (orig_w > 0.0f && orig_h > 0.0f && (w != orig_w || h != orig_h)) {
        pimpl_->canvas->scale(w / orig_w, h / orig_h);
    }
    svg.impl().dom->setContainerSize(SkSize::Make(w, h));
    svg.impl().dom->render(pimpl_->canvas);
    pimpl_->canvas->restore();
}

void Canvas::save() {
    pimpl_->canvas->save();
}

void Canvas::restore() {
    pimpl_->canvas->restore();
}

void Canvas::translate(float dx, float dy) {
    pimpl_->canvas->translate(dx, dy);
}

void Canvas::scale(float sx, float sy) {
    pimpl_->canvas->scale(sx, sy);
}

void Canvas::rotate(float degrees) {
    pimpl_->canvas->rotate(degrees);
}

void Canvas::clip_rect(float x, float y, float w, float h) {
    pimpl_->canvas->clipRect(SkRect::MakeXYWH(x, y, w, h));
}

// ============================================================================
// Surface Implementation
// ============================================================================

struct Surface::Impl {
    int width = 0;
    int height = 0;
    sk_sp<SkSurface> sk_surface;
    Canvas::Impl canvas_impl;

    Impl(int w, int h, sk_sp<SkSurface> s)
        : width(w), height(h), sk_surface(std::move(s)), canvas_impl(sk_surface->getCanvas()) {}
};

Surface::Surface(int w, int h) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        throw Error("Surface dimensions out of bounds (1..16384): " +
                    std::to_string(w) + "x" + std::to_string(h));
    }
    SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    auto s = SkSurfaces::Raster(info);
    if (!s) {
        throw Error("Failed to allocate raster surface of size " +
                    std::to_string(w) + "x" + std::to_string(h));
    }
    pimpl_ = std::make_unique<Impl>(w, h, std::move(s));
}

Surface::~Surface() = default;
Surface::Surface(Surface&& other) noexcept = default;
Surface& Surface::operator=(Surface&& other) noexcept = default;

int Surface::width() const {
    return pimpl_->width;
}

int Surface::height() const {
    return pimpl_->height;
}

Canvas Surface::canvas() {
    return Canvas(&pimpl_->canvas_impl);
}

std::string Surface::encode_png() const {
    SkPixmap pm;
    if (!pimpl_->sk_surface->peekPixels(&pm)) {
        throw Error("Failed to peek pixels for PNG encode");
    }
    SkDynamicMemoryWStream stream;
    if (!SkPngEncoder::Encode(&stream, pm, {})) {
        throw Error("Failed to encode PNG");
    }
    auto data = stream.detachAsData();
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

std::string Surface::encode_jpeg(int quality) const {
    if (quality < 0 || quality > 100) {
        throw Error("JPEG quality must be between 0 and 100, got: " + std::to_string(quality));
    }
    SkPixmap pm;
    if (!pimpl_->sk_surface->peekPixels(&pm)) {
        throw Error("Failed to peek pixels for JPEG encode");
    }
    SkDynamicMemoryWStream stream;
    SkJpegEncoder::Options opts;
    opts.fQuality = quality;
    if (!SkJpegEncoder::Encode(&stream, pm, opts)) {
        throw Error("Failed to encode JPEG");
    }
    auto data = stream.detachAsData();
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

void Surface::write_png(const std::string& path) const {
    std::string bytes = encode_png();
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw Error("Failed to open file for writing: " + path);
    }
    out.write(bytes.data(), bytes.size());
    if (!out) {
        throw Error("Failed to complete write of PNG to: " + path);
    }
}

std::string Surface::rgba() const {
    int w = pimpl_->width;
    int h = pimpl_->height;
    std::string bytes(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, '\0');
    SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    if (!pimpl_->sk_surface->readPixels(info, &bytes[0], w * 4, 0, 0)) {
        throw Error("Failed to read raw RGBA pixels from surface");
    }
    return bytes;
}

Image Surface::snapshot() const {
    auto img = pimpl_->sk_surface->makeImageSnapshot();
    if (!img) {
        throw Error("Failed to create snapshot from surface");
    }
    return Image(std::make_shared<Image::Impl>(img));
}

} // namespace lmv
