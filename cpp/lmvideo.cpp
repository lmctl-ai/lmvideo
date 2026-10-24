#include "lmvideo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
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
#if defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#else
#include "include/ports/SkFontMgr_data.h"
#include "include/ports/SkFontMgr_empty.h"
#endif
#include "include/utils/SkParsePath.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"

// Embed bundled DejaVu Sans TTF into the binary so rendering never depends on system fonts.
#ifndef LMVIDEO_FONT_PATH
#define LMVIDEO_FONT_PATH "assets/fonts/DejaVuSans.ttf"
#endif
#if defined(__APPLE__)
// Mach-O: C symbols carry a leading underscore in assembly, and read-only data lives in __TEXT,__const.
__asm__(
  ".section __TEXT,__const\n"
  ".globl _dejavu_sans_start\n"
  ".globl _dejavu_sans_end\n"
  "_dejavu_sans_start:\n"
  ".incbin \"" LMVIDEO_FONT_PATH "\"\n"
  "_dejavu_sans_end:\n"
  ".text\n"
);
#else
__asm__(
  ".section .rodata\n"
  ".global dejavu_sans_start\n"
  ".global dejavu_sans_end\n"
  "dejavu_sans_start:\n"
  ".incbin \"" LMVIDEO_FONT_PATH "\"\n"
  "dejavu_sans_end:\n"
  ".text\n"
);
#endif
extern "C" const unsigned char dejavu_sans_start[];
extern "C" const unsigned char dejavu_sans_end[];

namespace lmv {

namespace {

inline void check_finite(float val, const char* func, const char* arg) {
    if (!std::isfinite(val)) {
        throw Error(std::string(func) + ": " + arg + " must be a finite number");
    }
}

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
#if defined(__APPLE__)
        // The macOS Skia pack has no FreeType (so no custom-data manager); CoreText loads the bundled face from memory via makeFromData.
        return SkFontMgr_New_CoreText(nullptr);
#else
        size_t font_size = dejavu_sans_end - dejavu_sans_start;
        sk_sp<SkData> fontData = SkData::MakeWithoutCopy(dejavu_sans_start, font_size);
        sk_sp<SkData> dataArray[] = { fontData };
        return SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(dataArray, 1));
#endif
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

sk_sp<SkImage> decode_image_data_eager(sk_sp<SkData> data, const std::string& func_name) {
    if (!data || data->isEmpty()) {
        throw Error(func_name + ": image data is empty");
    }
    auto codec = SkCodec::MakeFromData(data);
    if (!codec) {
        throw Error(func_name + ": unrecognized image format or invalid header");
    }
    SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
    if (info.alphaType() == kUnknown_SkAlphaType) {
        info = info.makeAlphaType(kPremul_SkAlphaType);
    }
    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(info)) {
        throw Error(func_name + ": memory allocation failed for decoded image");
    }
    SkCodec::Result res = codec->getPixels(info, bitmap.getPixels(), bitmap.rowBytes());
    if (res != SkCodec::kSuccess) {
        throw Error(func_name + ": image decode failed (" + SkCodec::ResultToString(res) + ")");
    }
    bitmap.setImmutable();
    auto img = bitmap.asImage();
    if (!img) {
        throw Error(func_name + ": failed to construct image from decoded pixels");
    }
    return img;
}

} // namespace

Color parse_color(const std::string& str) {
    std::string s = trim_and_lower(str);
    if (s.empty()) {
        throw Error("parse_color: color string is empty");
    }

    if (s[0] == '#') {
        if (s.size() == 4) { // #rgb
            int r = parse_hex_char(s[1]);
            int g = parse_hex_char(s[2]);
            int b = parse_hex_char(s[3]);
            if (r < 0 || g < 0 || b < 0) {
                throw Error("parse_color: invalid hex characters in: " + str);
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
                throw Error("parse_color: invalid hex characters in: " + str);
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
                throw Error("parse_color: invalid hex characters in: " + str);
            }
            return Color(static_cast<uint8_t>((r1 << 4) | r2),
                         static_cast<uint8_t>((g1 << 4) | g2),
                         static_cast<uint8_t>((b1 << 4) | b2),
                         static_cast<uint8_t>((a1 << 4) | a2));
        } else {
            throw Error("parse_color: invalid hex color length in: " + str);
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

    throw Error("parse_color: unknown color name or invalid format: " + str);
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
    check_finite(w, "Paint.stroke_width", "w");
    if (w < 0.0f) {
        throw Error("Paint.stroke_width: width must be non-negative, got: " + std::to_string(w));
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
        throw Error("Paint.stroke_cap: unknown stroke cap: " + cap);
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
        throw Error("Paint.stroke_join: unknown stroke join: " + join);
    }
    return *this;
}

Paint& Paint::linear_gradient(float x0, float y0, float x1, float y1, Color c0, Color c1) {
    check_finite(x0, "Paint.linear_gradient", "x0");
    check_finite(y0, "Paint.linear_gradient", "y0");
    check_finite(x1, "Paint.linear_gradient", "x1");
    check_finite(y1, "Paint.linear_gradient", "y1");
    SkPoint pts[2] = {SkPoint::Make(x0, y0), SkPoint::Make(x1, y1)};
    SkColor colors[2] = {SkColorSetARGB(c0.a, c0.r, c0.g, c0.b), SkColorSetARGB(c1.a, c1.r, c1.g, c1.b)};
    pimpl_->sk_paint.setShader(SkGradientShader::MakeLinear(pts, colors, nullptr, 2, SkTileMode::kClamp));
    return *this;
}

Paint& Paint::dash(float on, float off) {
    check_finite(on, "Paint.dash", "on");
    check_finite(off, "Paint.dash", "off");
    if (on < 0.0f || off < 0.0f) {
        throw Error("Paint.dash: dash intervals must be non-negative");
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
    check_finite(sigma, "Paint.blur", "sigma");
    if (sigma < 0.0f) {
        throw Error("Paint.blur: blur sigma must be non-negative, got: " + std::to_string(sigma));
    }
    if (sigma > 0.0f) {
        pimpl_->sk_paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    } else {
        pimpl_->sk_paint.setMaskFilter(nullptr);
    }
    return *this;
}

Paint& Paint::alpha(float a) {
    check_finite(a, "Paint.alpha", "alpha");
    if (a < 0.0f || a > 1.0f) {
        throw Error("Paint.alpha: alpha must be between 0.0 and 1.0, got: " + std::to_string(a));
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
    check_finite(x, "Path.move_to", "x");
    check_finite(y, "Path.move_to", "y");
    pimpl_->sk_path.moveTo(x, y);
    return *this;
}

Path& Path::line_to(float x, float y) {
    check_finite(x, "Path.line_to", "x");
    check_finite(y, "Path.line_to", "y");
    pimpl_->sk_path.lineTo(x, y);
    return *this;
}

Path& Path::quad_to(float x1, float y1, float x2, float y2) {
    check_finite(x1, "Path.quad_to", "x1");
    check_finite(y1, "Path.quad_to", "y1");
    check_finite(x2, "Path.quad_to", "x2");
    check_finite(y2, "Path.quad_to", "y2");
    pimpl_->sk_path.quadTo(x1, y1, x2, y2);
    return *this;
}

Path& Path::cubic_to(float x1, float y1, float x2, float y2, float x3, float y3) {
    check_finite(x1, "Path.cubic_to", "x1");
    check_finite(y1, "Path.cubic_to", "y1");
    check_finite(x2, "Path.cubic_to", "x2");
    check_finite(y2, "Path.cubic_to", "y2");
    check_finite(x3, "Path.cubic_to", "x3");
    check_finite(y3, "Path.cubic_to", "y3");
    pimpl_->sk_path.cubicTo(x1, y1, x2, y2, x3, y3);
    return *this;
}

Path& Path::close() {
    pimpl_->sk_path.close();
    return *this;
}

Path& Path::add_rect(float x, float y, float w, float h) {
    check_finite(x, "Path.add_rect", "x");
    check_finite(y, "Path.add_rect", "y");
    check_finite(w, "Path.add_rect", "w");
    check_finite(h, "Path.add_rect", "h");
    if (w < 0.0f || h < 0.0f) {
        throw Error("Path.add_rect: dimensions (w, h) must be non-negative");
    }
    pimpl_->sk_path.addRect(SkRect::MakeXYWH(x, y, w, h));
    return *this;
}

Path& Path::add_oval(float x, float y, float w, float h) {
    check_finite(x, "Path.add_oval", "x");
    check_finite(y, "Path.add_oval", "y");
    check_finite(w, "Path.add_oval", "w");
    check_finite(h, "Path.add_oval", "h");
    if (w < 0.0f || h < 0.0f) {
        throw Error("Path.add_oval: dimensions (w, h) must be non-negative");
    }
    pimpl_->sk_path.addOval(SkRect::MakeXYWH(x, y, w, h));
    return *this;
}

Path& Path::add_circle(float cx, float cy, float r) {
    check_finite(cx, "Path.add_circle", "cx");
    check_finite(cy, "Path.add_circle", "cy");
    check_finite(r, "Path.add_circle", "r");
    if (r < 0.0f) {
        throw Error("Path.add_circle: radius must be non-negative");
    }
    pimpl_->sk_path.addCircle(cx, cy, r);
    return *this;
}

Path& Path::add_round_rect(float x, float y, float w, float h, float rx, float ry) {
    check_finite(x, "Path.add_round_rect", "x");
    check_finite(y, "Path.add_round_rect", "y");
    check_finite(w, "Path.add_round_rect", "w");
    check_finite(h, "Path.add_round_rect", "h");
    check_finite(rx, "Path.add_round_rect", "rx");
    check_finite(ry, "Path.add_round_rect", "ry");
    if (w < 0.0f || h < 0.0f || rx < 0.0f || ry < 0.0f) {
        throw Error("Path.add_round_rect: dimensions and radii must be non-negative");
    }
    pimpl_->sk_path.addRoundRect(SkRect::MakeXYWH(x, y, w, h), rx, ry);
    return *this;
}

Path Path::from_svg(const std::string& svg_path_data) {
    bool all_ws = true;
    for (char c : svg_path_data) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            all_ws = false;
            break;
        }
    }
    if (all_ws) {
        throw Error("Path.from_svg: empty or whitespace-only SVG path data");
    }
    auto opt_path = SkParsePath::FromSVGString(svg_path_data.c_str());
    if (!opt_path.has_value() || opt_path->isEmpty()) {
        throw Error("Path.from_svg: failed to parse SVG path data: " + svg_path_data);
    }
    Path p;
    p.pimpl_->sk_path = *opt_path;
    return p;
}

Path Path::translate(float dx, float dy) const {
    check_finite(dx, "Path.translate", "dx");
    check_finite(dy, "Path.translate", "dy");
    Path copy = *this;
    copy.pimpl_->sk_path.transform(SkMatrix::Translate(dx, dy));
    return copy;
}

Path Path::scale(float sx, float sy) const {
    check_finite(sx, "Path.scale", "sx");
    check_finite(sy, "Path.scale", "sy");
    Path copy = *this;
    copy.pimpl_->sk_path.transform(SkMatrix::Scale(sx, sy));
    return copy;
}

Path Path::rotate(float degrees) const {
    check_finite(degrees, "Path.rotate", "degrees");
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
    Impl(const Impl& other) : sk_font(other.sk_font) {}
};

Font::Font(float size) {
    check_finite(size, "Font.Font", "size");
    if (size <= 0.0f || size > 16384.0f) {
        throw Error("Font.Font: size must be in (0, 16384], got: " + std::to_string(size));
    }
    pimpl_ = std::make_unique<Impl>(get_bundled_typeface(), size);
}

Font::~Font() = default;

Font::Font(const Font& other)
    : pimpl_(other.pimpl_ ? std::make_unique<Impl>(*other.pimpl_) : nullptr) {}

Font& Font::operator=(const Font& other) {
    if (this != &other) {
        pimpl_ = other.pimpl_ ? std::make_unique<Impl>(*other.pimpl_) : nullptr;
    }
    return *this;
}

Font::Font(Font&& other) noexcept = default;
Font& Font::operator=(Font&& other) noexcept = default;

Font Font::from_file(const std::string& path, float size) {
    check_finite(size, "Font.from_file", "size");
    if (size <= 0.0f || size > 16384.0f) {
        throw Error("Font.from_file: size must be in (0, 16384], got: " + std::to_string(size));
    }
    if (path.empty()) {
        throw Error("Font.from_file: font file path cannot be empty");
    }
    auto tf = get_font_mgr()->makeFromFile(path.c_str());
    if (!tf) {
        throw Error("Font.from_file: failed to load font from: " + path);
    }
    Font f(size);
    f.pimpl_ = std::make_unique<Impl>(tf, size);
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
        throw Error("Image.from_file: path cannot be empty");
    }
    auto data = SkData::MakeFromFileName(path.c_str());
    if (!data) {
        throw Error("Image.from_file: failed to read file: " + path);
    }
    auto img = decode_image_data_eager(data, "Image.from_file");
    return Image(std::make_shared<Impl>(img));
}

Image Image::from_bytes(const std::string& bytes) {
    if (bytes.empty()) {
        throw Error("Image.from_bytes: byte buffer is empty");
    }
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto img = decode_image_data_eager(data, "Image.from_bytes");
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
        throw Error("Svg.parse: empty SVG XML string");
    }
    SkMemoryStream stream(xml.data(), xml.size());
    SkSVGDOM::Builder builder;
    builder.setFontManager(get_font_mgr());
    builder.setTextShapingFactory(SkShapers::Primitive::Factory());
    auto dom = builder.make(stream);
    if (!dom) {
        throw Error("Svg.parse: failed to parse SVG XML");
    }
    auto root = dom->getRoot();
    if (!root) {
        throw Error("Svg.parse: missing root <svg> element");
    }

    bool has_w = (root->getWidth().unit() != SkSVGLength::Unit::kPercentage &&
                  root->getWidth().unit() != SkSVGLength::Unit::kUnknown &&
                  root->getWidth().value() > 0.0f);
    bool has_h = (root->getHeight().unit() != SkSVGLength::Unit::kPercentage &&
                  root->getHeight().unit() != SkSVGLength::Unit::kUnknown &&
                  root->getHeight().value() > 0.0f);

    auto vb = root->getViewBox();
    float w = 0.0f;
    float h = 0.0f;

    if (has_w && has_h) {
        w = root->getWidth().value();
        h = root->getHeight().value();
    } else if (has_w && !has_h) {
        w = root->getWidth().value();
        if (vb.has_value() && vb->width() > 0.0f && vb->height() > 0.0f) {
            h = w * (vb->height() / vb->width());
        } else {
            h = w;
        }
    } else if (has_h && !has_w) {
        h = root->getHeight().value();
        if (vb.has_value() && vb->width() > 0.0f && vb->height() > 0.0f) {
            w = h * (vb->width() / vb->height());
        } else {
            w = h;
        }
    } else if (vb.has_value() && vb->width() > 0.0f && vb->height() > 0.0f) {
        w = vb->width();
        h = vb->height();
    }

    // If viewBox was not provided, but we have intrinsic dimensions, synthesize viewBox
    // so that SkSVGDOM can scale and preserve aspect ratio according to preserveAspectRatio.
    if (!vb.has_value() && w > 0.0f && h > 0.0f) {
        root->setViewBox(SkRect::MakeWH(w, h));
    }

    // Set root width and height to 100% so that setContainerSize controls the viewport
    // and SkSVGDOM's native viewBox-to-viewport mapping handles preserveAspectRatio.
    root->setWidth(SkSVGLength(100, SkSVGLength::Unit::kPercentage));
    root->setHeight(SkSVGLength(100, SkSVGLength::Unit::kPercentage));

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
    check_finite(x, "Canvas.draw_rect", "x");
    check_finite(y, "Canvas.draw_rect", "y");
    check_finite(w, "Canvas.draw_rect", "w");
    check_finite(h, "Canvas.draw_rect", "h");
    if (w < 0.0f || h < 0.0f) {
        throw Error("Canvas.draw_rect: dimensions (w, h) must be non-negative");
    }
    pimpl_->canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), p.impl().sk_paint);
}

void Canvas::draw_round_rect(float x, float y, float w, float h, float rx, float ry, const Paint& p) {
    check_finite(x, "Canvas.draw_round_rect", "x");
    check_finite(y, "Canvas.draw_round_rect", "y");
    check_finite(w, "Canvas.draw_round_rect", "w");
    check_finite(h, "Canvas.draw_round_rect", "h");
    check_finite(rx, "Canvas.draw_round_rect", "rx");
    check_finite(ry, "Canvas.draw_round_rect", "ry");
    if (w < 0.0f || h < 0.0f || rx < 0.0f || ry < 0.0f) {
        throw Error("Canvas.draw_round_rect: dimensions and radii must be non-negative");
    }
    pimpl_->canvas->drawRoundRect(SkRect::MakeXYWH(x, y, w, h), rx, ry, p.impl().sk_paint);
}

void Canvas::draw_oval(float x, float y, float w, float h, const Paint& p) {
    check_finite(x, "Canvas.draw_oval", "x");
    check_finite(y, "Canvas.draw_oval", "y");
    check_finite(w, "Canvas.draw_oval", "w");
    check_finite(h, "Canvas.draw_oval", "h");
    if (w < 0.0f || h < 0.0f) {
        throw Error("Canvas.draw_oval: dimensions (w, h) must be non-negative");
    }
    pimpl_->canvas->drawOval(SkRect::MakeXYWH(x, y, w, h), p.impl().sk_paint);
}

void Canvas::draw_circle(float cx, float cy, float r, const Paint& p) {
    check_finite(cx, "Canvas.draw_circle", "cx");
    check_finite(cy, "Canvas.draw_circle", "cy");
    check_finite(r, "Canvas.draw_circle", "r");
    if (r < 0.0f) {
        throw Error("Canvas.draw_circle: radius must be non-negative");
    }
    pimpl_->canvas->drawCircle(cx, cy, r, p.impl().sk_paint);
}

void Canvas::draw_line(float x0, float y0, float x1, float y1, const Paint& p) {
    check_finite(x0, "Canvas.draw_line", "x0");
    check_finite(y0, "Canvas.draw_line", "y0");
    check_finite(x1, "Canvas.draw_line", "x1");
    check_finite(y1, "Canvas.draw_line", "y1");
    pimpl_->canvas->drawLine(x0, y0, x1, y1, p.impl().sk_paint);
}

void Canvas::draw_path(const Path& path, const Paint& p) {
    pimpl_->canvas->drawPath(path.impl().sk_path, p.impl().sk_paint);
}

void Canvas::draw_text(const std::string& text, float x, float y, const Font& f, const Paint& p) {
    check_finite(x, "Canvas.draw_text", "x");
    check_finite(y, "Canvas.draw_text", "y");
    pimpl_->canvas->drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y,
                                  f.impl().sk_font, p.impl().sk_paint);
}

void Canvas::draw_image(const Image& img, float x, float y) {
    if (!img.impl().img) {
        throw Error("Canvas.draw_image: cannot draw null or invalid Image");
    }
    check_finite(x, "Canvas.draw_image", "x");
    check_finite(y, "Canvas.draw_image", "y");
    pimpl_->canvas->drawImage(img.impl().img.get(), x, y, SkSamplingOptions());
}

void Canvas::draw_image_rect(const Image& img, float x, float y, float w, float h) {
    if (!img.impl().img) {
        throw Error("Canvas.draw_image_rect: cannot draw null or invalid Image");
    }
    check_finite(x, "Canvas.draw_image_rect", "x");
    check_finite(y, "Canvas.draw_image_rect", "y");
    check_finite(w, "Canvas.draw_image_rect", "w");
    check_finite(h, "Canvas.draw_image_rect", "h");
    if (w < 0.0f || h < 0.0f) {
        throw Error("Canvas.draw_image_rect: dimensions (w, h) must be non-negative");
    }
    pimpl_->canvas->drawImageRect(img.impl().img.get(), SkRect::MakeXYWH(x, y, w, h), SkSamplingOptions());
}

void Canvas::draw_svg(const Svg& svg, float x, float y, float w, float h) {
    if (!svg.impl().dom) {
        throw Error("Canvas.draw_svg: cannot draw null or invalid Svg");
    }
    check_finite(x, "Canvas.draw_svg", "x");
    check_finite(y, "Canvas.draw_svg", "y");
    check_finite(w, "Canvas.draw_svg", "w");
    check_finite(h, "Canvas.draw_svg", "h");
    if (w <= 0.0f || h <= 0.0f) {
        throw Error("Canvas.draw_svg: dimensions (w, h) must be positive");
    }
    pimpl_->canvas->save();
    pimpl_->canvas->translate(x, y);
    pimpl_->canvas->clipRect(SkRect::MakeWH(w, h));
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
    check_finite(dx, "Canvas.translate", "dx");
    check_finite(dy, "Canvas.translate", "dy");
    pimpl_->canvas->translate(dx, dy);
}

void Canvas::scale(float sx, float sy) {
    check_finite(sx, "Canvas.scale", "sx");
    check_finite(sy, "Canvas.scale", "sy");
    pimpl_->canvas->scale(sx, sy);
}

void Canvas::rotate(float degrees) {
    check_finite(degrees, "Canvas.rotate", "degrees");
    pimpl_->canvas->rotate(degrees);
}

void Canvas::clip_rect(float x, float y, float w, float h) {
    check_finite(x, "Canvas.clip_rect", "x");
    check_finite(y, "Canvas.clip_rect", "y");
    check_finite(w, "Canvas.clip_rect", "w");
    check_finite(h, "Canvas.clip_rect", "h");
    if (w < 0.0f || h < 0.0f) {
        throw Error("Canvas.clip_rect: dimensions (w, h) must be non-negative");
    }
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
        throw Error("Surface.Surface: dimensions out of bounds [1, 16384]: " +
                    std::to_string(w) + "x" + std::to_string(h));
    }
    SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    auto s = SkSurfaces::Raster(info);
    if (!s) {
        throw Error("Surface.Surface: failed to allocate raster surface of size " +
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
        throw Error("Surface.encode_png: failed to peek pixels for PNG encode");
    }
    SkDynamicMemoryWStream stream;
    if (!SkPngEncoder::Encode(&stream, pm, {})) {
        throw Error("Surface.encode_png: failed to encode PNG");
    }
    auto data = stream.detachAsData();
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

std::string Surface::encode_jpeg(int quality) const {
    if (quality < 0 || quality > 100) {
        throw Error("Surface.encode_jpeg: quality must be between 0 and 100, got: " + std::to_string(quality));
    }
    SkPixmap pm;
    if (!pimpl_->sk_surface->peekPixels(&pm)) {
        throw Error("Surface.encode_jpeg: failed to peek pixels for JPEG encode");
    }
    SkDynamicMemoryWStream stream;
    SkJpegEncoder::Options opts;
    opts.fQuality = quality;
    if (!SkJpegEncoder::Encode(&stream, pm, opts)) {
        throw Error("Surface.encode_jpeg: failed to encode JPEG");
    }
    auto data = stream.detachAsData();
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

void Surface::write_png(const std::string& path) const {
    std::string bytes = encode_png();
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw Error("Surface.write_png: failed to open file for writing: " + path);
    }
    out.write(bytes.data(), bytes.size());
    if (!out) {
        throw Error("Surface.write_png: failed to complete write of PNG to: " + path);
    }
}

std::string Surface::rgba() const {
    int w = pimpl_->width;
    int h = pimpl_->height;
    std::string bytes(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, '\0');
    // Read back as unpremultiplied straight-alpha RGBA
    SkImageInfo info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    if (!pimpl_->sk_surface->readPixels(info, &bytes[0], w * 4, 0, 0)) {
        throw Error("Surface.rgba: failed to read raw RGBA pixels from surface");
    }
    return bytes;
}

Image Surface::snapshot() const {
    auto img = pimpl_->sk_surface->makeImageSnapshot();
    if (!img) {
        throw Error("Surface.snapshot: failed to create snapshot from surface");
    }
    return Image(std::make_shared<Image::Impl>(img));
}

} // namespace lmv
