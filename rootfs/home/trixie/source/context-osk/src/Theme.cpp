#include "Theme.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

// Utils
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

Theme::Theme() : height(200), bg_color("#333333"), r(0.2), g(0.2), b(0.2) {}

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
            }
        }
    }
    if (building_btn) {
        buttons.push_back(current_btn);
    }
    
    parse_color();

    // Load images
    for (auto& btn : buttons) {
        if (!btn.image_path.empty()) {
             // Assume path relative to theme or absolute? 
             // Requirement says: "Images for the buttons".
             // We can check local path first.
             // Note: cairo png loading is simple.
             btn.image_surface = cairo_image_surface_create_from_png(btn.image_path.c_str());
             if (cairo_surface_status(btn.image_surface) != CAIRO_STATUS_SUCCESS) {
                 std::cerr << "Failed to load image: " << btn.image_path << std::endl;
                 btn.image_surface = nullptr;
             }
        }
    }

    return true;
}
