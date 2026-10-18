#include "lmvideo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_test_index = 0;
int g_pass_count = 0;
int g_fail_count = 0;

#define CHECK(cond, desc) do { \
    g_test_index++; \
    if (cond) { \
        std::cout << "ok " << g_test_index << " - " << desc << "\n"; \
        g_pass_count++; \
    } else { \
        std::cerr << "not ok " << g_test_index << " - " << desc << " (line " << __LINE__ << ")\n"; \
        g_fail_count++; \
    } \
} while (0)

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        throw lmv::Error("Failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool file_exists(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return in.good();
}

lmv::Color get_pixel(const std::string& rgba, int width, int height, int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        throw std::out_of_range("Coordinates (" + std::to_string(x) + "," +
                                std::to_string(y) + ") out of bounds");
    }
    size_t idx = (static_cast<size_t>(y) * width + x) * 4;
    return lmv::Color(static_cast<uint8_t>(rgba[idx]),
                      static_cast<uint8_t>(rgba[idx + 1]),
                      static_cast<uint8_t>(rgba[idx + 2]),
                      static_cast<uint8_t>(rgba[idx + 3]));
}

// ----------------------------------------------------------------------------
// Test 1: Fill rect with exact colors and assert exact RGBA at probe pixels
// ----------------------------------------------------------------------------
void test_rect_exact_rgba() {
    lmv::Surface surf(100, 100);
    auto canvas = surf.canvas();

    lmv::Color bg(10, 20, 30, 255);
    lmv::Color fg(200, 100, 50, 255);

    canvas.clear(bg);

    lmv::Paint p;
    p.fill().color(fg).antialias(false);
    canvas.draw_rect(20, 20, 60, 60, p);

    std::string bytes = surf.rgba();
    CHECK(bytes.size() == 100 * 100 * 4, "exact RGBA byte length is 100*100*4");

    // Probes in background
    CHECK(get_pixel(bytes, 100, 100, 5, 5) == bg, "background probe at (5, 5)");
    CHECK(get_pixel(bytes, 100, 100, 10, 50) == bg, "background probe at (10, 50)");
    CHECK(get_pixel(bytes, 100, 100, 90, 50) == bg, "background probe at (90, 50)");
    CHECK(get_pixel(bytes, 100, 100, 95, 95) == bg, "background probe at (95, 95)");

    // Probes in rect interior
    CHECK(get_pixel(bytes, 100, 100, 25, 25) == fg, "rect interior probe at (25, 25)");
    CHECK(get_pixel(bytes, 100, 100, 50, 50) == fg, "rect interior probe at (50, 50)");
    CHECK(get_pixel(bytes, 100, 100, 75, 75) == fg, "rect interior probe at (75, 75)");
}

// ----------------------------------------------------------------------------
// Test 2: Draw each primitive and assert probes inside
// ----------------------------------------------------------------------------
void test_primitives_probes() {
    lmv::Color bg(0, 0, 0, 255);

    // Round rect
    {
        lmv::Surface surf(100, 100);
        surf.canvas().clear(bg);
        lmv::Paint p;
        p.fill().color(lmv::Color(255, 0, 0, 255)).antialias(false);
        surf.canvas().draw_round_rect(10, 10, 80, 80, 15, 15, p);
        std::string bytes = surf.rgba();
        CHECK(get_pixel(bytes, 100, 100, 50, 50) == lmv::Color(255, 0, 0, 255),
              "round rect center probe");
        CHECK(get_pixel(bytes, 100, 100, 2, 2) == bg, "round rect exterior probe");
    }

    // Circle
    {
        lmv::Surface surf(100, 100);
        surf.canvas().clear(bg);
        lmv::Paint p;
        p.fill().color(lmv::Color(0, 255, 0, 255)).antialias(false);
        surf.canvas().draw_circle(50, 50, 30, p);
        std::string bytes = surf.rgba();
        CHECK(get_pixel(bytes, 100, 100, 50, 50) == lmv::Color(0, 255, 0, 255),
              "circle center probe");
        CHECK(get_pixel(bytes, 100, 100, 10, 10) == bg, "circle exterior probe");
    }

    // Oval
    {
        lmv::Surface surf(100, 100);
        surf.canvas().clear(bg);
        lmv::Paint p;
        p.fill().color(lmv::Color(0, 0, 255, 255)).antialias(false);
        surf.canvas().draw_oval(20, 30, 60, 40, p);
        std::string bytes = surf.rgba();
        CHECK(get_pixel(bytes, 100, 100, 50, 50) == lmv::Color(0, 0, 255, 255),
              "oval center probe");
        CHECK(get_pixel(bytes, 100, 100, 5, 5) == bg, "oval exterior probe");
    }

    // Line (stroked)
    {
        lmv::Surface surf(100, 100);
        surf.canvas().clear(bg);
        lmv::Paint p;
        p.stroke().stroke_width(12).color(lmv::Color(255, 255, 0, 255)).antialias(false);
        surf.canvas().draw_line(20, 50, 80, 50, p);
        std::string bytes = surf.rgba();
        CHECK(get_pixel(bytes, 100, 100, 50, 50) == lmv::Color(255, 255, 0, 255),
              "line center probe");
        CHECK(get_pixel(bytes, 100, 100, 50, 20) == bg, "line exterior probe");
    }

    // Path (polygon)
    {
        lmv::Surface surf(100, 100);
        surf.canvas().clear(bg);
        lmv::Path path;
        path.move_to(20, 20).line_to(80, 20).line_to(50, 80).close();
        lmv::Paint p;
        p.fill().color(lmv::Color(255, 0, 255, 255)).antialias(false);
        surf.canvas().draw_path(path, p);
        std::string bytes = surf.rgba();
        CHECK(get_pixel(bytes, 100, 100, 50, 35) == lmv::Color(255, 0, 255, 255),
              "path interior probe");
        CHECK(get_pixel(bytes, 100, 100, 5, 5) == bg, "path exterior probe");
    }

    // Image draw
    {
        lmv::Surface small_surf(20, 20);
        small_surf.canvas().clear(lmv::Color(0, 255, 255, 255));
        lmv::Image img = small_surf.snapshot();

        lmv::Surface surf(100, 100);
        surf.canvas().clear(bg);
        surf.canvas().draw_image(img, 30, 30);
        std::string bytes = surf.rgba();
        CHECK(get_pixel(bytes, 100, 100, 40, 40) == lmv::Color(0, 255, 255, 255),
              "draw_image interior probe");
        CHECK(get_pixel(bytes, 100, 100, 10, 10) == bg, "draw_image exterior probe");
    }
}

// ----------------------------------------------------------------------------
// Test 3: Text measurement & ink bounding box + embolden ink check
// ----------------------------------------------------------------------------
void test_text_measurement_and_ink() {
    lmv::Font font(24.0f);
    CHECK(font.size() == 24.0f, "font size reported correctly");

    float w1 = font.measure("Hello");
    float w2 = font.measure("Hello");
    CHECK(w1 == w2, "identical measure() across calls");
    CHECK(w1 > 35.0f && w1 < 85.0f, "measure(\"Hello\") within sane band");

    lmv::Surface surf(200, 100);
    surf.canvas().clear(lmv::Color(0, 0, 0, 255));

    lmv::Paint p;
    p.color(lmv::Color(255, 255, 255, 255)).antialias(true);
    // Base point at (50, 60)
    surf.canvas().draw_text("Hello", 50, 60, font, p);

    std::string bytes = surf.rgba();
    int min_x = 200, max_x = 0, min_y = 100, max_y = 0;
    int ink_pixels = 0;

    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            lmv::Color c = get_pixel(bytes, 200, 100, x, y);
            if (c.r > 20 || c.g > 20 || c.b > 20) {
                ink_pixels++;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }

    CHECK(ink_pixels > 80, "text ink exists inside expected bbox");
    CHECK(min_x >= 45 && max_x <= 135, "ink x-bounds within expected range");
    CHECK(min_y >= 35 && max_y <= 70, "ink y-bounds within expected range");

    // Assert no ink far outside
    CHECK(get_pixel(bytes, 200, 100, 10, 10) == lmv::Color(0, 0, 0, 255), "no ink at (10, 10)");
    CHECK(get_pixel(bytes, 200, 100, 190, 90) == lmv::Color(0, 0, 0, 255), "no ink at (190, 90)");
    CHECK(get_pixel(bytes, 200, 100, 10, 90) == lmv::Color(0, 0, 0, 255), "no ink at (10, 90)");
    CHECK(get_pixel(bytes, 200, 100, 190, 10) == lmv::Color(0, 0, 0, 255), "no ink at (190, 10)");

    // Embolden check: render with bold font and assert strictly more rendered ink
    lmv::Surface surf_bold(200, 100);
    surf_bold.canvas().clear(lmv::Color(0, 0, 0, 255));
    font.embolden(true);
    surf_bold.canvas().draw_text("Hello", 50, 60, font, p);

    std::string bold_bytes = surf_bold.rgba();
    int bold_ink_pixels = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            lmv::Color c = get_pixel(bold_bytes, 200, 100, x, y);
            if (c.r > 20 || c.g > 20 || c.b > 20) {
                bold_ink_pixels++;
            }
        }
    }
    CHECK(bold_ink_pixels > ink_pixels, "emboldened font renders strictly more ink pixels than normal font");
    CHECK(bold_bytes != bytes, "emboldened font produces distinct pixel bytes from normal font");
}

// ----------------------------------------------------------------------------
// Test 4: PNG and JPEG encode round trip
// ----------------------------------------------------------------------------
void test_png_jpeg_round_trip() {
    int w = 80, h = 60;
    lmv::Surface surf(w, h);
    auto canvas = surf.canvas();

    // 4 quadrants: red, green, blue, yellow
    lmv::Paint p_red; p_red.color(lmv::Color(255, 0, 0, 255)).antialias(false);
    lmv::Paint p_green; p_green.color(lmv::Color(0, 255, 0, 255)).antialias(false);
    lmv::Paint p_blue; p_blue.color(lmv::Color(0, 0, 255, 255)).antialias(false);
    lmv::Paint p_yellow; p_yellow.color(lmv::Color(255, 255, 0, 255)).antialias(false);

    canvas.draw_rect(0, 0, 40, 30, p_red);
    canvas.draw_rect(40, 0, 40, 30, p_green);
    canvas.draw_rect(0, 30, 40, 30, p_blue);
    canvas.draw_rect(40, 30, 40, 30, p_yellow);

    // PNG round trip
    std::string png_data = surf.encode_png();
    CHECK(png_data.size() > 8, "PNG data size > 8");
    CHECK(png_data.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0, "PNG signature matches");

    lmv::Image decoded_png = lmv::Image::from_bytes(png_data);
    CHECK(decoded_png.width() == w && decoded_png.height() == h, "PNG decoded dimensions match");

    lmv::Surface roundtrip_surf(w, h);
    roundtrip_surf.canvas().draw_image(decoded_png, 0, 0);
    std::string rt_bytes = roundtrip_surf.rgba();
    CHECK(get_pixel(rt_bytes, w, h, 20, 15) == lmv::Color(255, 0, 0, 255), "PNG roundtrip probe Q1");
    CHECK(get_pixel(rt_bytes, w, h, 60, 15) == lmv::Color(0, 255, 0, 255), "PNG roundtrip probe Q2");
    CHECK(get_pixel(rt_bytes, w, h, 20, 45) == lmv::Color(0, 0, 255, 255), "PNG roundtrip probe Q3");
    CHECK(get_pixel(rt_bytes, w, h, 60, 45) == lmv::Color(255, 255, 0, 255), "PNG roundtrip probe Q4");

    // JPEG round trip
    std::string jpeg_data = surf.encode_jpeg(90);
    CHECK(jpeg_data.size() > 2, "JPEG data size > 2");
    CHECK(static_cast<uint8_t>(jpeg_data[0]) == 0xFF && static_cast<uint8_t>(jpeg_data[1]) == 0xD8,
          "JPEG starts with JFIF SOI marker (0xFFD8)");

    lmv::Image decoded_jpeg = lmv::Image::from_bytes(jpeg_data);
    CHECK(decoded_jpeg.width() == w && decoded_jpeg.height() == h, "JPEG decoded dimensions match");
}

// ----------------------------------------------------------------------------
// Test 5: SVG parsing and error handling
// ----------------------------------------------------------------------------
void test_svg_parsing_and_errors() {
    std::string svg_xml =
        "<svg width=\"100\" height=\"100\" viewBox=\"0 0 100 100\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <rect x=\"5\" y=\"5\" width=\"40\" height=\"40\" fill=\"#ff0000\" />\n"
        "  <circle cx=\"75\" cy=\"25\" r=\"20\" fill=\"#0000ff\" />\n"
        "  <path d=\"M 10 80 L 90 80\" stroke=\"#00ff00\" stroke-width=\"8\" />\n"
        "  <text x=\"10\" y=\"60\" font-family=\"DejaVu Sans\" font-size=\"14\" fill=\"#ffffff\">SVG</text>\n"
        "</svg>";

    lmv::Svg svg = lmv::Svg::parse(svg_xml);
    CHECK(svg.width() == 100.0f && svg.height() == 100.0f, "SVG dimensions parsed");

    lmv::Surface surf(100, 100);
    surf.canvas().clear(lmv::Color(0, 0, 0, 255));
    surf.canvas().draw_svg(svg, 0, 0, 100, 100);

    std::string bytes = surf.rgba();
    lmv::Color c_rect = get_pixel(bytes, 100, 100, 20, 20);
    CHECK(c_rect.r > 200 && c_rect.g < 30 && c_rect.b < 30, "SVG rect probe interior is red");

    lmv::Color c_circle = get_pixel(bytes, 100, 100, 75, 25);
    CHECK(c_circle.b > 200 && c_circle.r < 30 && c_circle.g < 30, "SVG circle probe interior is blue");

    lmv::Color c_path = get_pixel(bytes, 100, 100, 50, 80);
    CHECK(c_path.g > 200 && c_path.r < 30 && c_path.b < 30, "SVG stroked line probe interior is green");

    // Check that text produced ink in [10..60] x [45..62]
    int text_ink = 0;
    for (int y = 45; y <= 62; ++y) {
        for (int x = 10; x <= 60; ++x) {
            lmv::Color c = get_pixel(bytes, 100, 100, x, y);
            if (c.r > 30 || c.g > 30 || c.b > 30) {
                text_ink++;
            }
        }
    }
    CHECK(text_ink > 20, "SVG text produced ink");

    // Error handling checks
    bool threw = false;
    try {
        lmv::Svg::parse("not an svg document <xml");
    } catch (const lmv::Error&) {
        threw = true;
    }
    CHECK(threw, "bad SVG throws lmv::Error");

    threw = false;
    try {
        lmv::parse_color("#invalid_hex");
    } catch (const lmv::Error&) {
        threw = true;
    }
    CHECK(threw, "bad hex color throws lmv::Error");

    threw = false;
    try {
        lmv::parse_color("not_a_color_name");
    } catch (const lmv::Error&) {
        threw = true;
    }
    CHECK(threw, "unknown color name throws lmv::Error");

    threw = false;
    try {
        lmv::Surface(-10, 100);
    } catch (const lmv::Error&) {
        threw = true;
    }
    CHECK(threw, "negative surface size throws lmv::Error");

    threw = false;
    try {
        lmv::Surface(20000, 100);
    } catch (const lmv::Error&) {
        threw = true;
    }
    CHECK(threw, "excessive surface size > 16384 throws lmv::Error");

    threw = false;
    try {
        lmv::Path::from_svg("");
    } catch (const lmv::Error&) {
        threw = true;
    }
    CHECK(threw, "empty SVG path throws lmv::Error");
}

// ----------------------------------------------------------------------------
// Test 6: Golden check
// ----------------------------------------------------------------------------
void test_golden_check(bool generate_golden) {
    const std::string svg_path = "tests/golden/scene.svg";
    const std::string png_path = "tests/golden/scene.png";

    std::string svg_content = read_file(svg_path);
    lmv::Svg svg = lmv::Svg::parse(svg_content);
    int w = static_cast<int>(std::round(svg.width()));
    int h = static_cast<int>(std::round(svg.height()));
    CHECK(w == 200 && h == 120, "golden SVG has expected size 200x120");

    lmv::Surface rendered(w, h);
    rendered.canvas().clear(lmv::Color(0, 0, 0, 0));
    rendered.canvas().draw_svg(svg, 0, 0, static_cast<float>(w), static_cast<float>(h));

    if (generate_golden) {
        rendered.write_png(png_path);
        std::cout << "# Generated golden PNG: " << png_path << "\n";
    }

    if (!file_exists(png_path)) {
        CHECK(false, "golden PNG " + png_path + " missing (use --generate-golden to generate)");
        return;
    }

    lmv::Image golden_img = lmv::Image::from_file(png_path);
    CHECK(golden_img.width() == w && golden_img.height() == h, "golden PNG size matches 200x120");

    lmv::Surface golden_surf(w, h);
    golden_surf.canvas().draw_image(golden_img, 0, 0);

    std::string rendered_rgba = rendered.rgba();
    std::string golden_rgba = golden_surf.rgba();

    int total_pixels = w * h;
    int differing_pixels = 0;
    int max_channel_diff = 0;

    for (int i = 0; i < total_pixels; ++i) {
        size_t offset = static_cast<size_t>(i) * 4;
        int dr = std::abs(static_cast<int>(static_cast<uint8_t>(rendered_rgba[offset])) -
                          static_cast<int>(static_cast<uint8_t>(golden_rgba[offset])));
        int dg = std::abs(static_cast<int>(static_cast<uint8_t>(rendered_rgba[offset + 1])) -
                          static_cast<int>(static_cast<uint8_t>(golden_rgba[offset + 1])));
        int db = std::abs(static_cast<int>(static_cast<uint8_t>(rendered_rgba[offset + 2])) -
                          static_cast<int>(static_cast<uint8_t>(golden_rgba[offset + 2])));
        int da = std::abs(static_cast<int>(static_cast<uint8_t>(rendered_rgba[offset + 3])) -
                          static_cast<int>(static_cast<uint8_t>(golden_rgba[offset + 3])));

        int pixel_max = std::max({dr, dg, db, da});
        if (pixel_max > max_channel_diff) {
            max_channel_diff = pixel_max;
        }
        if (pixel_max > 0) {
            differing_pixels++;
        }
    }

    double diff_pct = (100.0 * differing_pixels) / total_pixels;
    CHECK(max_channel_diff <= 3, "golden comparison max per-channel difference <= 3 (got " +
          std::to_string(max_channel_diff) + ")");
    CHECK(diff_pct <= 0.5, "golden comparison pixel diff <= 0.5% (got " +
          std::to_string(diff_pct) + "%, " + std::to_string(differing_pixels) + " pixels)");
}

// ----------------------------------------------------------------------------
// Test 7 (Finding 1): Semi-transparent exact readback in unpremultiplied RGBA
// ----------------------------------------------------------------------------
void test_unpremul_readback() {
    lmv::Surface surf1(1, 1);
    surf1.canvas().clear(lmv::Color(255, 0, 0, 128));
    std::string b1 = surf1.rgba();
    CHECK(get_pixel(b1, 1, 1, 0, 0) == lmv::Color(255, 0, 0, 128),
          "Surface.rgba() returns exact straight-alpha RGBA for Color(255, 0, 0, 128)");

    lmv::Surface surf2(1, 1);
    surf2.canvas().clear(lmv::Color(0, 0, 255, 128));
    std::string b2 = surf2.rgba();
    CHECK(get_pixel(b2, 1, 1, 0, 0) == lmv::Color(0, 0, 255, 128),
          "Surface.rgba() returns exact straight-alpha RGBA for Color(0, 0, 255, 128)");
}

// ----------------------------------------------------------------------------
// Test 8 (Finding 2): Font copy independence
// ----------------------------------------------------------------------------
void test_font_copy_independence() {
    lmv::Font a(32.0f);
    lmv::Font b = a;
    a.embolden(true);

    lmv::Font c(32.0f); // Fresh unemboldened font

    lmv::Paint p;
    p.color(lmv::Color(255, 255, 255, 255)).antialias(true);

    lmv::Surface surf_b(160, 60);
    surf_b.canvas().clear(lmv::Color(0, 0, 0, 255));
    surf_b.canvas().draw_text("Test", 20, 40, b, p);

    lmv::Surface surf_c(160, 60);
    surf_c.canvas().clear(lmv::Color(0, 0, 0, 255));
    surf_c.canvas().draw_text("Test", 20, 40, c, p);

    CHECK(surf_b.rgba() == surf_c.rgba(), "copied font b remains unemboldened when a is emboldened");

    lmv::Surface surf_a(160, 60);
    surf_a.canvas().clear(lmv::Color(0, 0, 0, 255));
    surf_a.canvas().draw_text("Test", 20, 40, a, p);

    CHECK(surf_a.rgba() != surf_b.rgba(), "emboldened font a produces distinct output from copy b");
}

// ----------------------------------------------------------------------------
// Test 9 (Finding 3): Truncated image rejection in from_bytes and from_file
// ----------------------------------------------------------------------------
void test_truncated_image_rejection() {
    lmv::Surface surf(32, 32);
    surf.canvas().clear(lmv::Color(255, 0, 0, 255));
    std::string valid_png = surf.encode_png();
    CHECK(valid_png.size() > 20, "valid PNG created for truncation test");

    std::string trunc_png = valid_png.substr(0, valid_png.size() / 2);

    bool threw_bytes = false;
    try {
        lmv::Image::from_bytes(trunc_png);
    } catch (const lmv::Error&) {
        threw_bytes = true;
    }
    CHECK(threw_bytes, "Image::from_bytes throws lmv::Error on truncated PNG data");

    const std::string tmp_path = "/tmp/lmv_test_truncated.png";
    {
        std::ofstream out(tmp_path, std::ios::binary);
        out.write(trunc_png.data(), trunc_png.size());
    }

    bool threw_file = false;
    try {
        lmv::Image::from_file(tmp_path);
    } catch (const lmv::Error&) {
        threw_file = true;
    }
    CHECK(threw_file, "Image::from_file throws lmv::Error on truncated PNG file");
    std::remove(tmp_path.c_str());
}

// ----------------------------------------------------------------------------
// Test 10 (Finding 5): SVG preserveAspectRatio support
// ----------------------------------------------------------------------------
void test_svg_preserve_aspect_ratio() {
    // 100x100 SVG with default xMidYMid meet drawn into 200x100 -> centered at [50, 150]
    std::string svg_meet =
        "<svg width=\"100\" height=\"100\" viewBox=\"0 0 100 100\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <rect width=\"100\" height=\"100\" fill=\"#ff0000\" />\n"
        "</svg>";
    lmv::Svg svg1 = lmv::Svg::parse(svg_meet);
    lmv::Surface surf1(200, 100);
    surf1.canvas().clear(lmv::Color(0, 0, 0, 0));
    surf1.canvas().draw_svg(svg1, 0, 0, 200, 100);
    std::string b1 = surf1.rgba();

    // Sidebars should be transparent, center should be red
    CHECK(get_pixel(b1, 200, 100, 25, 50).a == 0,
          "preserveAspectRatio meet: left sidebar at (25, 50) is transparent");
    lmv::Color center_c = get_pixel(b1, 200, 100, 100, 50);
    CHECK(center_c.r > 200 && center_c.a == 255,
          "preserveAspectRatio meet: center at (100, 50) is red");
    CHECK(get_pixel(b1, 200, 100, 175, 50).a == 0,
          "preserveAspectRatio meet: right sidebar at (175, 50) is transparent");

    // preserveAspectRatio="none" -> stretched across all 200 pixels
    std::string svg_none =
        "<svg width=\"100\" height=\"100\" viewBox=\"0 0 100 100\" preserveAspectRatio=\"none\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <rect width=\"100\" height=\"100\" fill=\"#ff0000\" />\n"
        "</svg>";
    lmv::Svg svg2 = lmv::Svg::parse(svg_none);
    lmv::Surface surf2(200, 100);
    surf2.canvas().clear(lmv::Color(0, 0, 0, 0));
    surf2.canvas().draw_svg(svg2, 0, 0, 200, 100);
    std::string b2 = surf2.rgba();

    lmv::Color c_left = get_pixel(b2, 200, 100, 25, 50);
    CHECK(c_left.r > 200 && c_left.a == 255,
          "preserveAspectRatio none: left at (25, 50) is filled red");
}

// ----------------------------------------------------------------------------
// Test 11 (Finding 6): Lone explicit SVG dimension derived from viewBox
// ----------------------------------------------------------------------------
void test_svg_lone_explicit_dimension() {
    std::string svg_w100 = "<svg width=\"100\" viewBox=\"0 0 50 50\" xmlns=\"http://www.w3.org/2000/svg\"></svg>";
    lmv::Svg s1 = lmv::Svg::parse(svg_w100);
    CHECK(s1.width() == 100.0f && s1.height() == 100.0f,
          "<svg width=\"100\" viewBox=\"0 0 50 50\"> reports 100x100");

    std::string svg_h80 = "<svg height=\"80\" viewBox=\"0 0 40 20\" xmlns=\"http://www.w3.org/2000/svg\"></svg>";
    lmv::Svg s2 = lmv::Svg::parse(svg_h80);
    CHECK(s2.width() == 160.0f && s2.height() == 80.0f,
          "<svg height=\"80\" viewBox=\"0 0 40 20\"> reports 160x80 (aspect ratio 2:1)");
}

// ----------------------------------------------------------------------------
// Test 12 (Finding 7): Invalid numeric and path inputs rejection
// ----------------------------------------------------------------------------
void test_invalid_numeric_and_path_validation() {
    float inf = std::numeric_limits<float>::infinity();
    float nan = std::numeric_limits<float>::quiet_NaN();

    bool threw = false;
    try {
        lmv::Paint().stroke_width(inf);
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Paint().stroke_width(+inf) throws lmv::Error");

    threw = false;
    try {
        lmv::Paint().dash(inf, 1.0f);
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Paint().dash(+inf, 1) throws lmv::Error");

    threw = false;
    try {
        lmv::Paint().blur(inf);
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Paint().blur(+inf) throws lmv::Error");

    threw = false;
    try {
        lmv::Path().add_circle(10.0f, 10.0f, nan);
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Path().add_circle(..., NaN) throws lmv::Error");

    threw = false;
    try {
        lmv::Surface surf(50, 50);
        surf.canvas().draw_circle(10.0f, 10.0f, -1.0f, lmv::Paint());
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Canvas.draw_circle(..., -1) throws lmv::Error");

    threw = false;
    try {
        lmv::Svg s = lmv::Svg::parse("<svg width=\"10\" height=\"10\" xmlns=\"http://www.w3.org/2000/svg\"></svg>");
        lmv::Surface surf(50, 50);
        surf.canvas().draw_svg(s, 0.0f, 0.0f, nan, 10.0f);
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Canvas.draw_svg(..., NaN, 10) throws lmv::Error");

    threw = false;
    try {
        lmv::Path::from_svg("   ");
    } catch (const lmv::Error&) { threw = true; }
    CHECK(threw, "Path::from_svg(\"   \") throws lmv::Error");
}

} // namespace

int main(int argc, char* argv[]) {
    bool generate_golden = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--generate-golden") {
            generate_golden = true;
        }
    }

    test_rect_exact_rgba();
    test_primitives_probes();
    test_text_measurement_and_ink();
    test_png_jpeg_round_trip();
    test_svg_parsing_and_errors();
    test_golden_check(generate_golden);
    test_unpremul_readback();
    test_font_copy_independence();
    test_truncated_image_rejection();
    test_svg_preserve_aspect_ratio();
    test_svg_lone_explicit_dimension();
    test_invalid_numeric_and_path_validation();

    if (g_fail_count == 0) {
        std::cout << "PASS " << g_pass_count << " checks\n";
        return 0;
    } else {
        std::cerr << "FAIL " << g_fail_count << " out of " << g_test_index << " checks\n";
        return 1;
    }
}
