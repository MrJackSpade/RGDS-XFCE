#ifndef THEME_H
#define THEME_H

#include <string>
#include <vector>
#include <map>
#include <cairo/cairo.h>

struct Button {
    std::string label;
    int x, y, w, h;
    std::string image_path;
    int keycode;
    bool toggle;
    bool is_pressed;
    cairo_surface_t* image_surface; // Loaded at runtime
};

struct Theme {
    int height;
    std::string bg_color; // Hex string e.g. #RRGGBB
    std::vector<Button> buttons;
    
    // Parsed color values
    double r, g, b;

    // Pattern matching
    std::string window_title_pattern; 

    Theme();
    ~Theme();
    bool load(const std::string& path);
    void parse_color();
    
    // Static helper to peek at a theme file and return its window_title pattern
    static std::string peek_match_pattern(const std::string& path);
};

#endif
