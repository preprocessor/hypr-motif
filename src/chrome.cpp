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

    // The one and only bevel painter. The title bar spans the full window width and the
    // side frames run the full height, so bar and frame OVERLAP in the corners — if each
    // drew its own bevel there the two would meet and the join would show. Both call
    // this instead, so the frame and the bar's outer bevel are the same bands by
    // construction.
    //
    // A strip is light, then the grey face band, then dark. The dark side is `outer`,
    // i.e. which end of the strip faces away from the client:
    //   left   outer=low  -> light(2) face(2) dark(1)
    //   right  outer=high -> light(1) face(2) dark(2)
    //   bottom outer=high -> light(1) face(2) dark(2)
    void bevelBands(cairo_t* cr, double a, double b, double len, bool vertical, bool outerIsHigh, const SPalette& p) {
        // The lit band is the WIDE one when it faces outward (left) and the NARROW one
        // when it faces inward (right, bottom); the dark band takes the other width.
        const double litW   = outerIsHigh ? CDE::SIDE : CDE::BORDER - CDE::FACE_BAND - CDE::SIDE;
        const double darkW  = CDE::BORDER - CDE::FACE_BAND - litW;
        const double faceAt = litW;
        const double darkAt = litW + CDE::FACE_BAND;

        if (vertical) {
            fill(cr, a, b, litW, len, p.light);
            fill(cr, a + faceAt, b, CDE::FACE_BAND, len, p.face);
            fill(cr, a + darkAt, b, darkW, len, p.dark);
        } else {
            // Horizontal: `len` runs along x and the band widths along y. Swapping these
            // draws the lit band as a 1px column, which leaves the bottom frame missing
            // its inner highlight entirely.
            fill(cr, a, b, len, litW, p.light);
            fill(cr, a, b + faceAt, len, CDE::FACE_BAND, p.face);
            fill(cr, a, b + darkAt, len, darkW, p.dark);
        }
    }

} // namespace

namespace CDE {

    // Reference: examples/CDE-7.png. The left field runs from bar-relative x6..x20,
    // so the separator that closes it sits at x21.
    double leftFieldEnd(Vector2D size) {
        return LEFT_SEP + SIDE * 2 + FIELD_W;
    }

    // The right side carries three separators — title/fieldA, between the pair, and the
    // outermost one — at bar-relative x (width-40), (width-23) and (width-6).
    double rightFieldStart(Vector2D size) {
        return size.x - RIGHT_MARGIN - 2 * (FIELD_W + SIDE * 2);
    }

    CBox titleField(Vector2D size) {
        const double x = leftFieldEnd(size) + SIDE * 2;
        return {x, 0, std::max(0.0, rightFieldStart(size) - x), size.y};
    }

    // Motif's title-bar glyphs, measured from CDE-7.png at BAR_HEIGHT 22. All three are
    // centred on the *field* (rows 6..20, centre 13.5), not on the bar — centring on the
    // bar puts every glyph ~1.5px high, which reads as a vertical alignment bug.
    //   left (window menu)  9 x 4   rows 11-14
    //   minimize             4 x 4   rows 11-14
    //   maximize             9 x 9   rows  9-17
    CBox buttonRect(Vector2D size, int btn, bool showButtons) {
        if (!showButtons || btn < 0 || btn >= BTN_COUNT)
            return {};

        const double fieldTop = BEVEL + 2 + GROOVE * 2;
        const double fieldH   = size.y - fieldTop - SIDE;
        const double centre   = fieldTop + fieldH / 2;

        double w = 4, h = 4;
        if (btn == BTN_MAX)
            w = h = 9;
        else if (btn == BTN_CLOSE)
            w = 9, h = 4;

        // Each field is FIELD_W wide; the glyph is centred in it.
        const double fieldW = FIELD_W;
        double       fieldX;
        if (btn == BTN_CLOSE)
            fieldX = LEFT_SEP + SIDE * 2;
        else if (btn == BTN_MAX)
            fieldX = rightFieldStart(size) + SIDE * 2 + fieldW + SIDE * 2;
        else
            fieldX = rightFieldStart(size) + SIDE * 2;

        // Floor the top: centre is x.5 for an odd field height, and handing cairo a
        // fractional origin makes it blend across two rows instead of hitting the
        // reference rows exactly.
        return {fieldX + (fieldW - w) / 2, std::floor(centre - h / 2), w, h};
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

        // The bar's left and right ends ARE the frame's left and right bevels — the bar
        // spans the full window width, so these columns are shared with the side frames
        // and must be painted by the same routine, or the join shows.
        bevelBands(cr, 0, BEVEL, size.y - BEVEL, true, false, p);                       // left
        bevelBands(cr, size.x - CDE::BORDER, BEVEL, size.y - BEVEL, true, true, p);     // right

        // Bottom edge of the bar.
        fill(cr, 0, size.y - SIDE, size.x, SIDE, p.dark);

        const double lfEnd = leftFieldEnd(size), rfStart = rightFieldStart(size);
        if (LEFT_SEP < rfStart) {
            const double sepTop = BEVEL + 2 + GROOVE * 2;
            const double sepH   = size.y - sepTop - SIDE;
            separator(cr, LEFT_SEP, sepTop, sepH, p);
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

// Same painter the title bar uses for its own ends, so the frame and the bar are the
        // same bands by construction. `outerIsHigh` says which end faces away from the
        // client: for the left strip that is the low end, for right and bottom the high
        // end — which is why the lit band swaps sides between them.
        switch (edge) {
            case DECORATION_EDGE_LEFT: bevelBands(cr, 0, 0, size.y, true, false, p); break;
            case DECORATION_EDGE_RIGHT: bevelBands(cr, 0, 0, size.y, true, true, p); break;
            case DECORATION_EDGE_BOTTOM: bevelBands(cr, 0, 0, size.x, false, true, p); break;
            default: break;
        }

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

} // namespace CDE
