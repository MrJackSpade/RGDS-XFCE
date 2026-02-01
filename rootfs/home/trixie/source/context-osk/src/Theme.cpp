#include "Theme.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdint>

// Utils
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Base64 decoding table
static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static std::vector<uint8_t> base64_decode(const std::string& encoded) {
    std::vector<uint8_t> decoded;
    std::vector<uint8_t> buffer;
    buffer.reserve(4);

    for (char c : encoded) {
        if (c == '=') break;
        unsigned char val = base64_decode_table[(unsigned char)c];
        if (val == 64) continue; // Skip invalid characters
        buffer.push_back(val);

        if (buffer.size() == 4) {
            decoded.push_back((buffer[0] << 2) | (buffer[1] >> 4));
            decoded.push_back((buffer[1] << 4) | (buffer[2] >> 2));
            decoded.push_back((buffer[2] << 6) | buffer[3]);
            buffer.clear();
        }
    }

    if (buffer.size() >= 2) {
        decoded.push_back((buffer[0] << 2) | (buffer[1] >> 4));
        if (buffer.size() >= 3) {
            decoded.push_back((buffer[1] << 4) | (buffer[2] >> 2));
        }
    }

    return decoded;
}

// Load image from various sources and create a scaled surface
static cairo_surface_t* load_and_scale_image(const std::string& image_spec, int target_w, int target_h) {
    cairo_surface_t* original = nullptr;

    // Check if it's a base64 data URI
    if (image_spec.find("data:image/") == 0) {
        size_t comma_pos = image_spec.find(',');
        if (comma_pos != std::string::npos) {
            std::string base64_data = image_spec.substr(comma_pos + 1);
            std::vector<uint8_t> decoded = base64_decode(base64_data);

            if (!decoded.empty()) {
                // Create a read function with proper closure
                struct MemoryReader {
                    const std::vector<uint8_t>* buffer;
                    size_t offset;

                    static cairo_status_t read_func(void* closure, unsigned char* data, unsigned int length) {
                        MemoryReader* reader = static_cast<MemoryReader*>(closure);
                        if (reader->offset + length > reader->buffer->size()) {
                            return CAIRO_STATUS_READ_ERROR;
                        }
                        memcpy(data, reader->buffer->data() + reader->offset, length);
                        reader->offset += length;
                        return CAIRO_STATUS_SUCCESS;
                    }
                };

                MemoryReader reader = {&decoded, 0};
                original = cairo_image_surface_create_from_png_stream(MemoryReader::read_func, &reader);
            }
        }
    }
    // Check if it's a file:// URI
    else if (image_spec.find("file://") == 0) {
        std::string file_path = image_spec.substr(7); // Remove "file://"
        original = cairo_image_surface_create_from_png(file_path.c_str());
    }
    // Treat as regular file path
    else {
        original = cairo_image_surface_create_from_png(image_spec.c_str());
    }

    if (!original || cairo_surface_status(original) != CAIRO_STATUS_SUCCESS) {
        if (original) cairo_surface_destroy(original);
        return nullptr;
    }

    // Get original dimensions
    int orig_w = cairo_image_surface_get_width(original);
    int orig_h = cairo_image_surface_get_height(original);

    // Create target surface with transparent background
    cairo_surface_t* scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_w, target_h);
    cairo_t* cr = cairo_create(scaled);

    // Clear to transparent
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Calculate scale to fit within target while maintaining aspect ratio
    double scale_x = (double)target_w / orig_w;
    double scale_y = (double)target_h / orig_h;
    double scale = (scale_x < scale_y) ? scale_x : scale_y; // Use smaller scale to fit

    // Calculate scaled dimensions
    int scaled_w = (int)(orig_w * scale);
    int scaled_h = (int)(orig_h * scale);

    // Calculate centering offsets
    int offset_x = (target_w - scaled_w) / 2;
    int offset_y = (target_h - scaled_h) / 2;

    // Draw centered and scaled
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, original, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(original);

    return scaled;
}

Theme::Theme() : height(200), bg_color("#333333"), r(0.2), g(0.2), b(0.2), window_title_pattern("") {}

Theme::~Theme() {
    for (auto& btn : buttons) {
        if (btn.image_surface) {
            cairo_surface_destroy(btn.image_surface);
            btn.image_surface = nullptr;
        }
    }
}

void Theme::parse_color() {
    if (bg_color.size() >= 7 && bg_color[0] == '#') {
        int ir, ig, ib;
        sscanf(bg_color.c_str(), "#%02x%02x%02x", &ir, &ig, &ib);
        r = ir / 255.0;
        g = ig / 255.0;
        b = ib / 255.0;
    }
}

bool Theme::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open theme: " << path << std::endl;
        return false;
    }
    printf("Loading theme from: %s\n", path.c_str());
    
    // Clear existing buttons only if we successfully opened new theme
    for (auto& btn : buttons) {
        if (btn.image_surface) cairo_surface_destroy(btn.image_surface);
    }
    buttons.clear();

    std::string line;
    std::string section = "none";
    Button current_btn;
    // Initialize defaults to avoid garbage
    current_btn.x = 0; current_btn.y = 0; current_btn.w = 0; current_btn.h = 0;
    current_btn.keycode = 0; current_btn.toggle = false; current_btn.is_pressed = false;
    current_btn.image_surface = nullptr;
    
    bool building_btn = false;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                if (building_btn) {
                    buttons.push_back(current_btn);
                    building_btn = false;
                }
                section = line.substr(1, end - 1);
                
                bool is_btn_section = (section == "button" || section.find("Key_") == 0);

                if (is_btn_section) {
                    current_btn = Button();
                    current_btn.x = 0; current_btn.y = 0; current_btn.w = 32; current_btn.h = 32;
                    current_btn.keycode = 0;
                    current_btn.image_surface = nullptr;
                    current_btn.toggle = false;
                    current_btn.is_pressed = false;
                    building_btn = true;
                }
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            
            printf("Debug: key='%s' val='%s' section='%s'\n", key.c_str(), val.c_str(), section.c_str());

            if (section == "general" || section == "General") {
                if (key == "height") height = std::stoi(val);
                if (key == "background_color") bg_color = val;
                if (key == "window_title") window_title_pattern = val;
            } else if (building_btn) {
                if (key == "x") current_btn.x = std::stoi(val);
                if (key == "y") current_btn.y = std::stoi(val);
                if (key == "width") current_btn.w = std::stoi(val);
                if (key == "height") current_btn.h = std::stoi(val);
                if (key == "image") current_btn.image_path = val;
                if (key == "keycode") current_btn.keycode = std::stoi(val);
                if (key == "toggle") {
                    current_btn.toggle = (val == "true");
                    printf("Parsed toggle: '%s' -> %d for keycode %d\n", val.c_str(), current_btn.toggle, current_btn.keycode);
                }
                if (key == "label") current_btn.label = val;
                if (key == "shift_label") current_btn.shift_label = val;
                if (key == "caps_label") current_btn.caps_label = val;
            }
        }
    }
    if (building_btn) {
        buttons.push_back(current_btn);
    }
    
    parse_color();

    // Load and scale images to button dimensions
    for (auto& btn : buttons) {
        if (!btn.image_path.empty()) {
            btn.image_surface = load_and_scale_image(btn.image_path, btn.w, btn.h);
            if (!btn.image_surface) {
                std::cerr << "Failed to load image: " << btn.image_path << std::endl;
            } else {
                printf("Loaded and scaled image for button: %s (%dx%d)\n",
                       btn.image_path.c_str(), btn.w, btn.h);
            }
        }
    }

    return true;
}

std::string Theme::peek_match_pattern(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::string line;
    std::string section = "none";

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                section = line.substr(1, end - 1);
            }
            continue;
        }

        if (section == "general" || section == "General") {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(line.substr(0, eq));
                if (key == "window_title") {
                    return trim(line.substr(eq + 1));
                }
            }
        }
    }
    return "";
}
