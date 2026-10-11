#include "chrome.hpp"

#include <cairo/cairo.h>

#include <algorithm>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "config.hpp"

using namespace Hyprutils::Math;

namespace {

    struct SPalette {
        CHyprColor face, light, dark, title, hover;
    };

    SPalette currentPalette() {
        const auto c = CDE::config();
        return {c.face, c.light, c.dark, c.title, CHyprColor{0.72, 0.72, 0.72, 1}};
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

    // Motif draws its title-bar buttons as a bare raised glyph on the face — no button
    // box. The glyph itself is a raised rect: light top/left, dark bottom/right.
    void glyphFrame(cairo_t* cr, const CBox& b, const SPalette& p) {
        fill(cr, b.x, b.y, b.w, CDE::SIDE, p.light);
        fill(cr, b.x, b.y, CDE::SIDE, b.h, p.light);
        fill(cr, b.x, b.y + b.h - CDE::SIDE, b.w, CDE::SIDE, p.dark);
        fill(cr, b.x + b.w - CDE::SIDE, b.y, CDE::SIDE, b.h, p.dark);
    }

} // namespace

namespace CDE {

    // Measured layout: 1px dark + 1px light outer edge, then an 18px left field.
    double leftFieldEnd(Vector2D size) {
        return SIDE * 2 + FIELD_W;
    }

    // Right side has THREE separators: title/right-pair, between the pair, and the
    // outer right edge. The pair therefore starts RIGHT_MARGIN + 2 units in.
    double rightFieldStart(Vector2D size) {
        return size.x - RIGHT_MARGIN - 2 * (FIELD_W + SIDE * 2);
    }

    CBox titleField(Vector2D size) {
        const double x = leftFieldEnd(size) + SIDE * 2;
        return {x, 0, std::max(0.0, rightFieldStart(size) - x), size.y};
    }

    // Motif's title-bar glyphs, measured from example.png at BAR_HEIGHT 25.
    // All three share one centre line — the centre of the field, which is the band
    // between the groove and the bottom edge (rows 6..23). That is 3px below the
    // bar's own centre, which is why centring on the bar looks visibly wrong.
    //   left (window menu)  12 x 4   rows 13-16
    //   minimize             4 x 4   rows 13-16
    //   maximize            12 x 12  rows  9-20
    CBox buttonRect(Vector2D size, int btn, bool showButtons) {
        if (!showButtons || btn < 0 || btn >= BTN_COUNT)
            return {};

        const double fieldTop = BEVEL + 2 + GROOVE * 2;
        const double centre   = (fieldTop + size.y - SIDE) / 2;

        double w = 4, h = 4;
        if (btn == BTN_MAX)
            w = h = 12;
        else if (btn == BTN_CLOSE)
            w = 12, h = 4;

        const double y = centre - h / 2;

        if (btn == BTN_CLOSE)
            return {SIDE * 2 + (FIELD_W - w) / 2, y, w, h};

        // Maximize occupies the rightmost field, minimize the one to its left.
        const int    k          = btn == BTN_MAX ? 0 : 1;
        const double fieldRight = size.x - RIGHT_MARGIN - 1 - k * (FIELD_W + SIDE * 2);
        return {fieldRight - FIELD_W + 1 + (FIELD_W - w) / 2, y, w, h};
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
            const double sepH   = size.y - sepTop - SIDE;
            separator(cr, lfEnd, sepTop, sepH, p);
            separator(cr, rfStart, sepTop, sepH, p);
            separator(cr, rfStart + FIELD_W + SIDE * 2, sepTop, sepH, p);
            separator(cr, size.x - RIGHT_MARGIN, sepTop, sepH, p);
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

                // All three are bare raised rects; Motif puts no icon in them.
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

SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale) {
        const int      w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));
        const SPalette p = currentPalette();

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t*         cr   = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        fill(cr, 0, 0, size.x, size.y, p.face);

        // Raised frame: the outer edge of the window gets the highlight (light on the
        // left/bottom, dark on the right), the inner edge stays face.
        const double t = BORDER / 2.0;
        switch (edge) {
            case DECORATION_EDGE_LEFT: fill(cr, 0, 0, t, size.y, p.light); break;
            case DECORATION_EDGE_RIGHT: fill(cr, size.x - t, 0, t, size.y, p.dark); break;
            case DECORATION_EDGE_BOTTOM: fill(cr, 0, size.y - t, size.x, t, p.dark); break;
            default: break;
        }

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

} // namespace CDE
