#include "lmvideo.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " render IN.svg OUT.png [--width W] [--height H] [--background COLOR]\n"
              << "  " << prog << " demo OUT.png\n";
}

std::string read_file_to_string(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        throw lmv::Error("Cannot open file for reading: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        throw lmv::Error("I/O error while reading file: " + path);
    }
    return ss.str();
}

int run_demo(const std::string& out_path) {
    lmv::Surface surface(500, 350);
    auto canvas = surface.canvas();

    // 1. Background gradient
    lmv::Paint bg_paint;
    bg_paint.linear_gradient(0, 0, 500, 350, lmv::parse_color("#1a1a2e"), lmv::parse_color("#16213e"));
    canvas.draw_rect(0, 0, 500, 350, bg_paint);

    // 2. Rect
    lmv::Paint rect_paint;
    rect_paint.color(lmv::parse_color("#0f3460")).antialias(true);
    canvas.draw_rect(30, 30, 140, 90, rect_paint);

    // 3. Round rect
    lmv::Paint rr_paint;
    rr_paint.color(lmv::parse_color("#e94560")).antialias(true);
    canvas.draw_round_rect(200, 30, 140, 90, 15, 15, rr_paint);

    // 4. Circle
    lmv::Paint circle_paint;
    circle_paint.color(lmv::parse_color("#f39c12")).antialias(true);
    canvas.draw_circle(410, 75, 45, circle_paint);

    // 5. Stroked path with dashes
    lmv::Paint dash_paint;
    dash_paint.stroke().stroke_width(4).dash(10, 5).color(lmv::parse_color("#00fff5")).antialias(true);
    lmv::Path path;
    path.move_to(30, 180).quad_to(150, 130, 250, 180).cubic_to(320, 220, 400, 140, 470, 180);
    canvas.draw_path(path, dash_paint);

    // 6. Oval
    lmv::Paint oval_paint;
    oval_paint.color(lmv::parse_color("#9b59b6")).antialias(true);
    canvas.draw_oval(60, 220, 120, 70, oval_paint);

    // 7. Text line "Hello lmvideo 123"
    lmv::Font font(28);
    lmv::Paint text_paint;
    text_paint.color(lmv::parse_color("#ffffff")).antialias(true);
    canvas.draw_text("Hello lmvideo 123", 210, 265, font, text_paint);

    surface.write_png(out_path);
    return 0;
}

int run_render(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    std::string in_svg = argv[2];
    std::string out_png = argv[3];
    int opt_width = 0;
    int opt_height = 0;
    std::string bg_color;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--width") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --width requires an argument\n";
                return 1;
            }
            try {
                opt_width = std::stoi(argv[++i]);
                if (opt_width <= 0) {
                    std::cerr << "Error: --width must be positive\n";
                    return 1;
                }
            } catch (...) {
                std::cerr << "Error: invalid integer for --width\n";
                return 1;
            }
        } else if (arg == "--height") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --height requires an argument\n";
                return 1;
            }
            try {
                opt_height = std::stoi(argv[++i]);
                if (opt_height <= 0) {
                    std::cerr << "Error: --height must be positive\n";
                    return 1;
                }
            } catch (...) {
                std::cerr << "Error: invalid integer for --height\n";
                return 1;
            }
        } else if (arg == "--background") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --background requires an argument\n";
                return 1;
            }
            bg_color = argv[++i];
        } else {
            std::cerr << "Error: unrecognized option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    std::string xml = read_file_to_string(in_svg);
    lmv::Svg svg = lmv::Svg::parse(xml);

    float orig_w = svg.width();
    float orig_h = svg.height();
    int w = opt_width;
    int h = opt_height;

    if (w == 0 && h == 0) {
        w = static_cast<int>(std::round(orig_w));
        h = static_cast<int>(std::round(orig_h));
    } else if (w > 0 && h == 0) {
        if (orig_w > 0.0f && orig_h > 0.0f) {
            h = static_cast<int>(std::round(orig_h * (static_cast<float>(w) / orig_w)));
        } else {
            h = w;
        }
    } else if (h > 0 && w == 0) {
        if (orig_w > 0.0f && orig_h > 0.0f) {
            w = static_cast<int>(std::round(orig_w * (static_cast<float>(h) / orig_h)));
        } else {
            w = h;
        }
    }

    if (w <= 0 || h <= 0) {
        throw lmv::Error("SVG has no intrinsic dimensions; specify --width and --height");
    }

    lmv::Surface surface(w, h);
    auto canvas = surface.canvas();

    if (!bg_color.empty()) {
        lmv::Color bg = lmv::parse_color(bg_color);
        canvas.clear(bg);
    } else {
        canvas.clear(lmv::Color(0, 0, 0, 0));
    }

    canvas.draw_svg(svg, 0, 0, static_cast<float>(w), static_cast<float>(h));
    surface.write_png(out_png);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    try {
        if (cmd == "demo") {
            if (argc != 3) {
                print_usage(argv[0]);
                return 1;
            }
            return run_demo(argv[2]);
        } else if (cmd == "render") {
            return run_render(argc, argv);
        } else {
            print_usage(argv[0]);
            return 1;
        }
    } catch (const lmv::Error& e) {
        std::cerr << "Runtime error: " << e.what() << "\n";
        return 3;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 3;
    } catch (...) {
        std::cerr << "Unknown error occurred\n";
        return 3;
    }
}
