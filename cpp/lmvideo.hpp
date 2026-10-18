#ifndef LMVIDEO_HPP
#define LMVIDEO_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace lmv {

// Base exception for all lmvideo errors
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// RGBA 8-bit color representation
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr Color() = default;
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color& o) const {
        return !(*this == o);
    }
};

// Parses color string accepting "#rgb", "#rrggbb", "#rrggbbaa" and named colors:
// black, white, red, green, blue, transparent. Throws lmv::Error on invalid format.
Color parse_color(const std::string& str);

// Paint configuration for stroke, fill, gradients, and effects.
// Copyable value type with deep-copy semantics.
class Paint {
public:
    Paint();
    ~Paint();
    Paint(const Paint& other);
    Paint& operator=(const Paint& other);
    Paint(Paint&& other) noexcept;
    Paint& operator=(Paint&& other) noexcept;

    Paint& color(Color c);
    Paint& antialias(bool aa);
    Paint& fill();
    Paint& stroke();
    Paint& stroke_width(float w);
    Paint& stroke_cap(const std::string& cap);   // "butt", "round", "square"
    Paint& stroke_join(const std::string& join); // "miter", "round", "bevel"
    Paint& linear_gradient(float x0, float y0, float x1, float y1, Color c0, Color c1);
    Paint& dash(float on, float off);
    Paint& blur(float sigma);
    Paint& alpha(float a);                       // 0.0f .. 1.0f

    struct Impl;
    const Impl& impl() const { return *pimpl_; }
    Impl& impl() { return *pimpl_; }

private:
    std::unique_ptr<Impl> pimpl_;
};

// Vector path representation. Copyable value type with deep-copy semantics.
class Path {
public:
    Path();
    ~Path();
    Path(const Path& other);
    Path& operator=(const Path& other);
    Path(Path&& other) noexcept;
    Path& operator=(Path&& other) noexcept;

    Path& move_to(float x, float y);
    Path& line_to(float x, float y);
    Path& quad_to(float x1, float y1, float x2, float y2);
    Path& cubic_to(float x1, float y1, float x2, float y2, float x3, float y3);
    Path& close();
    Path& add_rect(float x, float y, float w, float h);
    Path& add_oval(float x, float y, float w, float h);
    Path& add_circle(float cx, float cy, float r);
    Path& add_round_rect(float x, float y, float w, float h, float rx, float ry);

    static Path from_svg(const std::string& svg_path_data);

    // Optional path transformations returning a transformed copy
    Path translate(float dx, float dy) const;
    Path scale(float sx, float sy) const;
    Path rotate(float degrees) const;

    struct Impl;
    const Impl& impl() const { return *pimpl_; }
    Impl& impl() { return *pimpl_; }

private:
    std::unique_ptr<Impl> pimpl_;
};

// Font handle wrapping a typeface at a given size with deterministic rendering options.
class Font {
public:
    explicit Font(float size); // uses bundled DejaVu Sans
    ~Font();
    Font(const Font& other);
    Font& operator=(const Font& other);
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;

    static Font from_file(const std::string& path, float size);
    float size() const;
    float measure(const std::string& text) const;
    Font& embolden(bool embolden);

    struct Impl;
    const Impl& impl() const { return *pimpl_; }
    Impl& impl() { return *pimpl_; }

private:
    std::unique_ptr<Impl> pimpl_;
};

// Immutable decoded image handle.
class Image {
public:
    Image();
    ~Image();
    Image(const Image& other);
    Image& operator=(const Image& other);
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    static Image from_file(const std::string& path);
    static Image from_bytes(const std::string& bytes);

    int width() const;
    int height() const;

    struct Impl;
    const Impl& impl() const { return *pimpl_; }
    Impl& impl() { return *pimpl_; }

    explicit Image(std::shared_ptr<Impl> impl);

private:
    std::shared_ptr<Impl> pimpl_;
};

// Parsed SVG document.
class Svg {
public:
    Svg();
    ~Svg();
    Svg(const Svg& other);
    Svg& operator=(const Svg& other);
    Svg(Svg&& other) noexcept;
    Svg& operator=(Svg&& other) noexcept;

    static Svg parse(const std::string& xml);
    float width() const;
    float height() const;

    struct Impl;
    const Impl& impl() const { return *pimpl_; }
    Impl& impl() { return *pimpl_; }

private:
    std::shared_ptr<Impl> pimpl_;
};

// Non-owning drawing canvas valid while its parent Surface lives.
class Canvas {
public:
    void clear(Color c);
    void draw_rect(float x, float y, float w, float h, const Paint& p);
    void draw_round_rect(float x, float y, float w, float h, float rx, float ry, const Paint& p);
    void draw_oval(float x, float y, float w, float h, const Paint& p);
    void draw_circle(float cx, float cy, float r, const Paint& p);
    void draw_line(float x0, float y0, float x1, float y1, const Paint& p);
    void draw_path(const Path& path, const Paint& p);
    void draw_text(const std::string& text, float x, float y, const Font& f, const Paint& p);
    void draw_image(const Image& img, float x, float y);
    void draw_image_rect(const Image& img, float x, float y, float w, float h);
    // Draws the SVG fit into the (x, y, w, h) rectangle, honoring the SVG's preserveAspectRatio attribute
    void draw_svg(const Svg& svg, float x, float y, float w, float h);
    void save();
    void restore();
    void translate(float dx, float dy);
    void scale(float sx, float sy);
    void rotate(float degrees);
    void clip_rect(float x, float y, float w, float h);

    struct Impl;
    explicit Canvas(Impl* impl);

private:
    Impl* pimpl_;
};

// Raster surface (premultiplied RGBA8888).
class Surface {
public:
    Surface(int w, int h);
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&& other) noexcept;
    Surface& operator=(Surface&& other) noexcept;

    int width() const;
    int height() const;
    Canvas canvas();
    std::string encode_png() const;
    std::string encode_jpeg(int quality = 80) const;
    void write_png(const std::string& path) const;
    std::string rgba() const; // exactly w*h*4 bytes, row-major
    Image snapshot() const;

    struct Impl;
    const Impl& impl() const { return *pimpl_; }
    Impl& impl() { return *pimpl_; }

private:
    std::unique_ptr<Impl> pimpl_;
};

} // namespace lmv

#endif // LMVIDEO_HPP
