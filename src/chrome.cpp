#include "chrome.hpp"

#include <cairo/cairo.h>

#include <algorithm>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/Renderer.hpp>

using namespace Hyprutils::Math;

namespace {

    // Motif "System" palette, sampled from example.png.
    struct SPalette {
        double face, light, dark, outline, glyph;
    };

    constexpr SPalette NORMAL  = {0.655, 0.855, 0.325, 0.10, 0.17};
    constexpr SPalette PRESSED = {0.470, 0.325, 0.855, 0.10, 0.17};

    void setGrey(cairo_t* cr, double v) {
        cairo_set_source_rgb(cr, v, v, v);
    }

    void fillRect(cairo_t* cr, double x, double y, double w, double h, double v) {
        setGrey(cr, v);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
    }

    // Raised: light top/left, dark bottom/right.
    void bevelOut(cairo_t* cr, double x, double y, double w, double h, double t, const SPalette& p) {
        fillRect(cr, x, y, w, t, p.light);
        fillRect(cr, x, y, t, h, p.light);
        fillRect(cr, x, y + h - t, w, t, p.dark);
        fillRect(cr, x + w - t, y, t, h, p.dark);
    }

    void glyphMin(cairo_t* cr, const CBox& b, double t, const SPalette& p) {
        fillRect(cr, b.x + t, b.y + b.h - 2 * t, b.w - 2 * t, t, p.glyph);
    }

    void glyphMax(cairo_t* cr, const CBox& b, double t, const SPalette& p) {
        fillRect(cr, b.x + t, b.y + t, b.w - 2 * t, t, p.glyph);
        fillRect(cr, b.x + t, b.y + t, t, b.h - 2 * t, p.glyph);
        fillRect(cr, b.x + b.w - 2 * t, b.y + t, t, b.h - 2 * t, p.glyph);
        fillRect(cr, b.x + t, b.y + b.h - 2 * t, b.w - 2 * t, t, p.glyph);
    }

    void glyphClose(cairo_t* cr, const CBox& b, const SPalette& p) {
        cairo_set_line_width(cr, 1.5);
        cairo_set_source_rgb(cr, p.glyph, p.glyph, p.glyph);
        const double i = 3;
        cairo_move_to(cr, b.x + i, b.y + i);
        cairo_line_to(cr, b.x + b.w - i, b.y + b.h - i);
        cairo_move_to(cr, b.x + b.w - i, b.y + i);
        cairo_line_to(cr, b.x + i, b.y + b.h - i);
        cairo_stroke(cr);
    }

    void drawButton(cairo_t* cr, const CBox& b, int btn, bool hover, bool press) {
        const SPalette& p = press ? PRESSED : NORMAL;

        // Hover lifts the face slightly; press sinks the bevel as Motif does.
        fillRect(cr, b.x, b.y, b.w, b.h, hover && !press ? 0.72 : p.face);
        if (press)
            bevelOut(cr, b.x, b.y, b.w, b.h, 1.0, SPalette{p.dark, p.dark, p.light, p.outline, p.glyph});
        else
            bevelOut(cr, b.x, b.y, b.w, b.h, 1.0, p);

        const CBox inner = {b.x + 2, b.y + 2, b.w - 4, b.h - 4};
        switch (btn) {
            case CDE::BTN_MIN: glyphMin(cr, inner, 2.0, p); break;
            case CDE::BTN_MAX: glyphMax(cr, inner, 2.0, p); break;
            case CDE::BTN_CLOSE: glyphClose(cr, inner, p); break;
            default: break;
        }
    }

} // namespace

namespace CDE {

    CBox buttonRect(Vector2D size, int btn) {
        if (btn < 0 || btn >= BTN_COUNT)
            return {};

        return {BEVEL * 2 + btn * (BTN_SIZE + 2), (size.y - BTN_SIZE) / 2, BTN_SIZE, BTN_SIZE};
    }

    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed) {
        const int w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t*         cr   = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        fillRect(cr, 0, 0, size.x, size.y, NORMAL.face);
        bevelOut(cr, 0, 0, size.x, size.y, BEVEL, NORMAL);

        cairo_set_line_width(cr, 1);
        setGrey(cr, NORMAL.dark);
        cairo_rectangle(cr, BEVEL + 0.5, BEVEL + 0.5, size.x - 2 * BEVEL - 1, size.y - 2 * BEVEL - 1);
        cairo_stroke(cr);

        for (int b = 0; b < BTN_COUNT; ++b)
            drawButton(cr, buttonRect(size, b), b, hovered == b, pressed == b);

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

    SP<Render::ITexture> renderTitle(const std::string& title, float scale) {
        if (title.empty())
            return nullptr;

        return g_pHyprRenderer->renderText(title, CHyprColor{0, 0, 0, 1}, sc<int>(11 * scale), false, "Sans", 0, 500);
    }

} // namespace CDE