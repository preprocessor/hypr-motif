#include "chrome.hpp"

#include <cairo/cairo.h>

#include <algorithm>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "config.hpp"

using namespace Hyprutils::Math;

namespace {

    struct SPalette {
        CHyprColor face, light, dark, title, hover;
    };

    SPalette currentPalette() {
        const auto c = CDE::config();
        return {c.face, c.light, c.dark, CHyprColor{c.title.r, c.title.g, c.title.b, 1}, CHyprColor{0.72, 0.72, 0.72, 1}};
    }

    void fill(cairo_t* cr, double x, double y, double w, double h, const CHyprColor& c) {
        if (w <= 0 || h <= 0)
            return;
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
    }

    // 1px dark over 1px light. This pairing is what makes the bar read as three
    // fields rather than one flat strip.
    void separator(cairo_t* cr, double x, double y, double h, const SPalette& p) {
        fill(cr, x, y, CDE::SIDE, h, p.dark);
        fill(cr, x + CDE::SIDE, y, CDE::SIDE, h, p.light);
    }

    // Motif draws its title-bar buttons as a bare glyph on the face — no button box.
    // The glyph itself is a raised shape: light top/left, dark bottom/right.
    void glyphFrame(cairo_t* cr, const CBox& b, const SPalette& p) {
        fill(cr, b.x, b.y, b.w, CDE::SIDE, p.light);
        fill(cr, b.x, b.y, CDE::SIDE, b.h, p.light);
        fill(cr, b.x, b.y + b.h - CDE::SIDE, b.w, CDE::SIDE, p.dark);
        fill(cr, b.x + b.w - CDE::SIDE, b.y, CDE::SIDE, b.h, p.dark);
    }

    void drawClose(cairo_t* cr, const CBox& b, const SPalette& p) {
        const double i = 3;
        cairo_set_line_width(cr, 1.5);
        cairo_set_source_rgb(cr, p.dark.r, p.dark.g, p.dark.b);
        cairo_move_to(cr, b.x + i, b.y + i);
        cairo_line_to(cr, b.x + b.w - i, b.y + b.h - i);
        cairo_move_to(cr, b.x + b.w - i, b.y + i);
        cairo_line_to(cr, b.x + i, b.y + b.h - i);
        cairo_stroke(cr);
    }

    void drawMin(cairo_t* cr, const CBox& b, const SPalette& p) {
        fill(cr, b.x + 2, b.y + b.h - 4, b.w - 4, 2, p.dark);
    }

    void drawMax(cairo_t* cr, const CBox& b, const SPalette& p) {
        fill(cr, b.x + 2, b.y + 2, b.w - 4, CDE::SIDE, p.dark);
        fill(cr, b.x + 2, b.y + 2, CDE::SIDE, b.h - 4, p.dark);
        fill(cr, b.x + b.w - 3, b.y + 2, CDE::SIDE, b.h - 4, p.dark);
        fill(cr, b.x + 2, b.y + b.h - 3, b.w - 4, CDE::SIDE, p.dark);
    }

} // namespace

namespace CDE {

    // Measured layout: 1px dark + 1px light outer edge, then an 18px left field.
    double leftFieldEnd(Vector2D size) {
        return SIDE * 2 + FIELD_W;
    }

    // Right pair: [18px field][sep][18px field][sep], then 2px of face.
    double rightFieldStart(Vector2D size) {
        return size.x - (2 * (FIELD_W + SIDE * 2) + SIDE * 2);
    }

    CBox titleField(Vector2D size) {
        const double x = leftFieldEnd(size) + SIDE * 2;
        return {x, 0, std::max(0.0, rightFieldStart(size) - x), size.y};
    }

    CBox buttonRect(Vector2D size, int btn, bool showButtons) {
        if (!showButtons || btn < 0 || btn >= BTN_COUNT)
            return {};

        // Content runs from below the groove to above the bottom edge; centre in that.
        const double top    = BEVEL + 2 + GROOVE * 2;
        const double avail  = size.y - top - SIDE;
        const double by     = top + std::max(0.0, (avail - BTN_SIZE) / 2);
        const double inset  = (FIELD_W - BTN_SIZE) / 2;

        if (btn == BTN_CLOSE)
            return {SIDE * 2 + inset, by, BTN_SIZE, BTN_SIZE};

        // Right pair, laid out from the right edge inward.
        const int    idx   = btn - BTN_MIN;
        const double right = size.x - SIDE * 2 - idx * (FIELD_W + SIDE * 2) - FIELD_W;
        return {right + inset, by, BTN_SIZE, BTN_SIZE};
    }

    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize) {
        const int      w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));
        const SPalette p = currentPalette();

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t*         cr   = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        fill(cr, 0, 0, size.x, size.y, p.face);

        // Top bevel, then the groove that separates it from the field row.
        fill(cr, 0, 0, size.x, BEVEL, p.light);
        fill(cr, 0, BEVEL + 2, size.x, GROOVE, p.dark);
        fill(cr, 0, BEVEL + 2 + GROOVE, size.x, GROOVE, p.light);

        // Left outer edge, bottom edge, and the field separators.
        fill(cr, 0, 0, SIDE, size.y, p.dark);
        fill(cr, SIDE, 0, SIDE, size.y, p.light);
        fill(cr, 0, size.y - SIDE, size.x, SIDE, p.dark);

        const double lfEnd = leftFieldEnd(size), rfStart = rightFieldStart(size);
        if (lfEnd < rfStart) {
            const double sepTop = BEVEL + 2 + GROOVE * 2;
            separator(cr, lfEnd, sepTop, size.y - sepTop - SIDE, p);
            separator(cr, rfStart, sepTop, size.y - sepTop - SIDE, p);
            separator(cr, rfStart + FIELD_W + SIDE * 2, sepTop, size.y - sepTop - SIDE, p);
        }

        if (config().showButtons) {
            for (int b = 0; b < BTN_COUNT; ++b) {
                const CBox r = buttonRect(size, b, true);
                if (!r.w)
                    continue;

                if (hovered == b)
                    fill(cr, r.x - 1, r.y - 1, r.w + 2, r.h + 2, p.hover);
                if (pressed == b) {
                    // Motif inverts the bevel when a button is held.
                    fill(cr, r.x, r.y, r.w, SIDE, p.dark);
                    fill(cr, r.x, r.y, SIDE, r.h, p.dark);
                    fill(cr, r.x, r.y + r.h - SIDE, r.w, SIDE, p.light);
                    fill(cr, r.x + r.w - SIDE, r.y, SIDE, r.h, p.light);
                } else {
                    glyphFrame(cr, r, p);
                }

                switch (b) {
                    case BTN_CLOSE: drawClose(cr, r, p); break;
                    case BTN_MIN: drawMin(cr, r, p); break;
                    case BTN_MAX: drawMax(cr, r, p); break;
                    default: break;
                }
            }
        }

        // Title, centred in the title field. Motif draws it in white on the face.
        if (!title.empty() && fontSize > 0) {
            const CBox field = titleField(size);

            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, fontSize);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, title.c_str(), &ext);

            const double avail = field.w - 8;
            if (ext.width > avail && ext.width > 0) {
                // Shrink to fit rather than overflow into the button fields.
                cairo_set_font_size(cr, fontSize * avail / ext.width);
                cairo_text_extents(cr, title.c_str(), &ext);
            }

            const double tx = field.x + (field.w - ext.width) / 2 - ext.x_bearing;
            const double ty = (size.y + ext.height) / 2;

            cairo_set_source_rgb(cr, p.title.r, p.title.g, p.title.b);
            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, title.c_str());
        }

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

} // namespace CDE
