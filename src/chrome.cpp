#include "chrome.hpp"

#include <cairo/cairo.h>

#include <algorithm>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "config.hpp"

using namespace Hyprutils::Math;

namespace {

    // Per-channel so config colours map straight through. Today this forces
    // R=G=B via setGrey, which is why nothing can be warm.
    struct SPalette {
        CHyprColor face, light, dark, highlight, glyph, outline;
    };

    SPalette currentPalette() {
        const auto c = CDE::config();
        return {c.face, c.light, c.dark, c.highlight, CHyprColor{0.10, 0.10, 0.10, 1}, CHyprColor{0.10, 0.10, 0.10, 1}};
    }

    void setColor(cairo_t* cr, const CHyprColor& c) {
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
    }

    void fill(cairo_t* cr, double x, double y, double w, double h, const CHyprColor& c) {
        if (w <= 0 || h <= 0)
            return;
        setColor(cr, c);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
    }

    // Raised: light top/left, dark bottom/right.
    void bevelOut(cairo_t* cr, double x, double y, double w, double h, double t, const SPalette& p) {
        fill(cr, x, y, w, t, p.light);
        fill(cr, x, y, t, h, p.light);
        fill(cr, x, y + h - t, w, t, p.dark);
        fill(cr, x + w - t, y, t, h, p.dark);
    }

    // A 1px dark line over a 1px light one. This pairing is what makes the
    // fields read as three panels rather than one flat bar.
    void separator(cairo_t* cr, double x, double y, double h, const SPalette& p) {
        fill(cr, x, y, CDE::SEPARATOR, h, p.dark);
        fill(cr, x + CDE::SEPARATOR, y, CDE::SEPARATOR, h, p.light);
    }

    void glyphMin(cairo_t* cr, const CBox& b, double t, const SPalette& p) {
        fill(cr, b.x + t, b.y + b.h - 2 * t, b.w - 2 * t, t, p.glyph);
    }

    void glyphMax(cairo_t* cr, const CBox& b, double t, const SPalette& p) {
        fill(cr, b.x + t, b.y + t, b.w - 2 * t, t, p.glyph);
        fill(cr, b.x + t, b.y + t, t, b.h - 2 * t, p.glyph);
        fill(cr, b.x + b.w - 2 * t, b.y + t, t, b.h - 2 * t, p.glyph);
        fill(cr, b.x + t, b.y + b.h - 2 * t, b.w - 2 * t, t, p.glyph);
    }

    void glyphClose(cairo_t* cr, const CBox& b, const SPalette& p) {
        cairo_set_line_width(cr, 1.5);
        setColor(cr, p.glyph);
        const double i = 3;
        cairo_move_to(cr, b.x + i, b.y + i);
        cairo_line_to(cr, b.x + b.w - i, b.y + b.h - i);
        cairo_move_to(cr, b.x + b.w - i, b.y + i);
        cairo_line_to(cr, b.x + i, b.y + b.h - i);
        cairo_stroke(cr);
    }

    void drawButton(cairo_t* cr, const CBox& b, int btn, bool hover, bool press, const SPalette& p) {
        fill(cr, b.x, b.y, b.w, b.h, hover && !press ? CHyprColor{0.72, 0.72, 0.72, 1} : p.face);

        if (press) // sink the bevel on press, as Motif does
            bevelOut(cr, b.x, b.y, b.w, b.h, 1.0, SPalette{p.face, p.dark, p.light, p.highlight, p.glyph, p.outline});
        else
            bevelOut(cr, b.x, b.y, b.w, b.h, 1.0, p);

        const CBox inner = {b.x + 2, b.y + 2, b.w - 4, b.h - 4};
        switch (btn) {
            case CDE::BTN_CLOSE: glyphClose(cr, inner, p); break;
            case CDE::BTN_MIN: glyphMin(cr, inner, 2.0, p); break;
            case CDE::BTN_MAX: glyphMax(cr, inner, 2.0, p); break;
            default: break;
        }
    }

} // namespace

namespace CDE {

    double leftFieldEnd(Vector2D size) {
        return BEVEL + FIELD_INSET + (LEFT_FIELD - FIELD_INSET * 2);
    }

    double rightFieldStart(Vector2D size) {
        return size.x - BEVEL - RIGHT_FIELD;
    }

    CBox titleField(Vector2D size) {
        const double x = leftFieldEnd(size) + SEPARATOR * 2;
        return {x, BEVEL, std::max(0.0, rightFieldStart(size) - x), size.y - BEVEL - HIGHLIGHT};
    }

    CBox buttonRect(Vector2D size, int btn, bool showButtons) {
        if (!showButtons || btn < 0 || btn >= BTN_COUNT)
            return {};

        const double y = (size.y - BTN_SIZE) / 2;

        if (btn == BTN_CLOSE)
            return {BEVEL + FIELD_INSET, y, BTN_SIZE, BTN_SIZE};

        // Right field holds min then max, laid out from the right edge inward
        // with a gap between them, matching the measured 38px field.
        const int    idx   = btn - BTN_MIN;
        const double right = size.x - BEVEL - FIELD_INSET;
        return {right - (idx + 1) * BTN_SIZE - idx * 2, y, BTN_SIZE, BTN_SIZE};
    }

    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed) {
        const int w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));
        const SPalette p = currentPalette();

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t*         cr   = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        // Face, then the raised outer bevel.
        fill(cr, 0, 0, size.x, size.y, p.face);
        bevelOut(cr, 0, 0, size.x, size.y, BEVEL, p);

        // Field separators. Only drawn where the bar is wide enough to hold them.
        const double lfEnd = leftFieldEnd(size), rfStart = rightFieldStart(size);
        if (lfEnd < rfStart) {
            separator(cr, lfEnd, BEVEL, size.y - BEVEL - HIGHLIGHT, p);
            separator(cr, rfStart, BEVEL, size.y - BEVEL - HIGHLIGHT, p);
        }

        // Bright band along the bottom edge.
        fill(cr, 0, size.y - HIGHLIGHT, size.x, HIGHLIGHT, p.highlight);

        if (config().showButtons) {
            for (int b = 0; b < BTN_COUNT; ++b) {
                const CBox r = buttonRect(size, b, true);
                if (r.w)
                    drawButton(cr, r, b, hovered == b, pressed == b, p);
            }
        }

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

    SP<Render::ITexture> renderTitle(const std::string& title, float scale, int fontSize) {
        if (title.empty())
            return nullptr;

        return g_pHyprRenderer->renderText(title, CHyprColor{0.10, 0.10, 0.10, 1}, sc<int>(fontSize * scale), false, "Sans", 0, 500);
    }

} // namespace CDE