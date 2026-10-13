#include "chrome.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "config.hpp"

using namespace Hyprutils::Math;

namespace {

    struct SPalette {
        CHyprColor face, light, dark, title, shadow;
    };

    // The active (focused) window gets its own palette; everything else uses the inactive
    // one. Both sets are configurable, so nothing here is hardcoded. The shadow is NOT
    // per-window: examples/title.png shows one black shadow under both, so a single
    // colour serves both palettes.
    SPalette currentPalette(bool active) {
        const auto c = CDE::config();
        return {active ? c.faceActive : c.face, active ? c.lightActive : c.light, active ? c.darkActive : c.dark, active ? c.titleActive : c.title, c.titleShadow};
    }

    // Swap light and dark, keep face, title and shadow. This is what makes a button read
    // as INSET: the lit edge moves to the bottom/right, which is the opposite of raised.
    // Motif does this for the held state, and hover borrows the same treatment.
    SPalette inverted(const SPalette& p) {
        return {p.face, p.dark, p.light, p.title, p.shadow};
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
    // box. The glyph is a raised rect: `lit` along the top/left, `shade` along the
    // bottom/right.
    //
    // The shade is laid down FIRST so that the lit colour wins wherever the two bevels
    // meet in a corner — that is what the reference does, and all three glyphs show it:
    // the top row is lit right across (so the top-right cell is lit) and the bottom-left
    // cell is lit too. Drawn the other way round, dark overwrites both and the glyph is
    // one pixel wrong at each corner.
    void glyphFrame(cairo_t* cr, const CBox& b, const CHyprColor& lit, const CHyprColor& shade) {
        fill(cr, b.x, b.y + b.h - CDE::SIDE, b.w, CDE::SIDE, shade);
        fill(cr, b.x + b.w - CDE::SIDE, b.y, CDE::SIDE, b.h, shade);

        fill(cr, b.x, b.y, b.w, CDE::SIDE, lit);
        fill(cr, b.x, b.y, CDE::SIDE, b.h, lit);
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

// The corners are MITRED, not overlapping rectangles. Measured 5x5 from CDE-7.png
    // (bottom pair) and examples/new-standard.png (top pair); 'L'/'F'/'D' are the palette,
    // rows run inner->outer. Index 0 is the OUTERMOST column, so the top patterns read
    // left-to-right from the window edge inward, and the bottom ones from inner outward.
    //
    // The top pair is why the bar's bevel appears to span the full window width: rows 0-1
    // of TL/TR are light across, which is the bevel, and rows 2-3 drop the frame's inner
    // band. Normal bands resume at row 5, below the mitre.
    constexpr const char* TL = "LLLLL"
                               "LLLLL"
                               "LLFFF"
                               "LLFFF"
                               "LLFFD";
    constexpr const char* TR = "LLLLL"
                               "LLLLD"
                               "FFFDD"
                               "FFFDD"
                               "DFFDD";
    constexpr const char* BL = "LLLFD"
                               "LLFFF"
                               "LLFFF"
                               "LDDDD"
                               "DDDDD";
    constexpr const char* BR = "LFFDD"
                               "FFFDD"
                               "FFFDD"
                               "DDDDD"
                               "DDDDD";

    void drawCorner(cairo_t* cr, double x, double y, const char* pat, const SPalette& p) {
        for (int r = 0; r < CDE::BORDER; ++r)
            for (int c = 0; c < CDE::BORDER; ++c) {
                const char     ch = pat[r * CDE::BORDER + c];
                const CHyprColor col = ch == 'L' ? p.light : ch == 'F' ? p.face : p.dark;
                fill(cr, x + c, y + r, 1, 1, col);
            }
    }

    // The float-toggle glyph for the left button of the right pair, measured from
    // examples/float-toggle.png (15x15, 3px margin trimmed to the 9x9 core). Two
    // overlapping 6x6 outlined squares (4x4 hollow), the back one offset up-right by
    // THREE — the second revision of the image; the first had offset two. The front
    // square's interior is opaque and occludes the back one's bottom-left corner. Light
    // on top/left edges, dark on bottom/right — the same raised-bevel convention as the
    // other glyphs. Replaces the bare 4x4 rect Motif puts there; the reference is
    // user-supplied, so it outranks CDE-7.png for this button.
    constexpr const char* FLOAT_TOGGLE = "...LLLLLL"
                                         "...L....D"
                                         "...L....D"
                                         "LLLLLL..D"
                                         "LFFFFD..D"
                                         "LFFFFDDDD"
                                         "LFFFFD..."
                                         "LFFFFD..."
                                         "LDDDDD...";

    void drawFloatToggle(cairo_t* cr, double x, double y, const SPalette& p) {
        for (int r = 0; r < CDE::FLOAT_GLYPH; ++r)
            for (int c = 0; c < CDE::FLOAT_GLYPH; ++c) {
                const char ch = FLOAT_TOGGLE[r * CDE::FLOAT_GLYPH + c];
                if (ch == '.')
                    continue; // transparent — the bar face shows through
                const CHyprColor col = ch == 'L' ? p.light : ch == 'D' ? p.dark : p.face;
                fill(cr, x + c, y + r, 1, 1, col);
            }
    }

    // A separator marking off the corner grab square. The lit pixel goes on local x=0 in
    // both directions — for the left strip that is the outer edge, for the right strip the
    // inner one, which is what the reference does. On a dark-outer strip the outermost
    // pixel of the second row keeps dark.
    void grabSeparator(cairo_t* cr, double a, double b, double len, bool horizontal, bool darkOuter, const SPalette& p) {
        if (horizontal) {
            fill(cr, a, b, 1, len, p.light);
            fill(cr, a + 1, b, CDE::BORDER - 1, len, p.dark);
            fill(cr, a, b + len, CDE::BORDER, 1, p.light);
            if (darkOuter)
                fill(cr, a + CDE::BORDER - 1, b + len, 1, 1, p.dark);
        } else {
            fill(cr, a, b + 1, 1, CDE::BORDER - 2, p.dark);
            fill(cr, a + 1, b + 1, 1, CDE::BORDER - 2, p.light);
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
    // The clickable FIELD, not the glyph. The glyph is 9x4..11x11 inside a FIELD_W-wide
    // field, so hit-testing the glyph alone leaves a few-pixel target and clicks feel
    // spotty — the user aims at the field, which is what Motif makes clickable.
    CBox buttonFieldRect(Vector2D size, int btn, bool showButtons) {
        if (!showButtons || btn < 0 || btn >= BTN_COUNT)
            return {};

        const double fieldTop = BEVEL + 2 + GROOVE * 2;
        const double fieldH   = size.y - fieldTop - SIDE;

        double fieldX;
        if (btn == BTN_CLOSE)
            fieldX = LEFT_SEP + SIDE * 2;
        else if (btn == BTN_MAX)
            fieldX = rightFieldStart(size) + SIDE * 2 + FIELD_W + SIDE * 2;
        else
            fieldX = rightFieldStart(size) + SIDE * 2;

        return {std::floor(fieldX), fieldTop, FIELD_W, fieldH};
    }

    CBox buttonRect(Vector2D size, int btn, bool showButtons) {
        const CBox field = buttonFieldRect(size, btn, showButtons);
        if (!field.w)
            return {};

        const double centre = field.y + field.h / 2;

        double w = 4, h = 4;
        if (btn == BTN_MAX)
            w = h = 11; // 2px larger than the 9x9 reference, still centred
        else if (btn == BTN_CLOSE)
            w = 9, h = 4;
        else if (btn == BTN_FLOAT)
            w = h = FLOAT_GLYPH;

        // Floor the origin on both axes: the field centre is x.5 for an odd field height,
        // and fractional origins make cairo blend across two rows/columns instead of
        // hitting the reference cells exactly, which shows up as pale half-pixels down
        // one edge of a glyph.
        return {std::floor(field.x + (field.w - w) / 2), std::floor(centre - h / 2), w, h};
    }

    // Which button, if any, is under a bar-local point. Lives next to the geometry that
    // defines the fields so hit-testing and drawing cannot drift apart.
    int buttonAt(Vector2D size, const Vector2D& p, bool showButtons) {
        for (int b = 0; b < BTN_COUNT; ++b) {
            const CBox f = buttonFieldRect(size, b, showButtons);
            if (f.w && p.x >= f.x && p.x <= f.x + f.w && p.y >= f.y && p.y <= f.y + f.h)
                return b;
        }
        return BTN_NONE;
    }

    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize, bool active) {
        const int      w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));
        const SPalette p = currentPalette(active);

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

        // The corner grab's vertical rule spans the full height of the grab square, from
        // bar-rel 1 to the bottom edge — unlike the field separators, which start below
        // the bevel. It lands on the same columns as the left-field and mid-right
        // separators, but reaches further, so it is drawn separately rather than by
        // widening those. The groove's light row breaks it, which is what the reference
        // shows, hence the two spans.
        if (size.x > CORNER_GRAB * 2 + 4) {
            const double above = BEVEL + GROOVE + 1;          // rows 1..BEVEL+GROOVE
            const double below = above + GROOVE + 1;          // rows BEVEL+GROOVE+2..h-1
            auto        grabRule = [&](double x) {
                separator(cr, x, 1, above, p);
                separator(cr, x, below, size.y - below, p);
            };
            grabRule(CORNER_GRAB);
            grabRule(size.x - CORNER_GRAB - 2);
        }

        if (config().showButtons) {
            for (int b = 0; b < BTN_COUNT; ++b) {
                const CBox r = buttonRect(size, b, true);
                if (!r.w)
                    continue;

                // Hover and held both read as INSET. Motif swaps the bevel when a button
                // is held — the lit edge moves to the bottom/right, the opposite of
                // raised — and hover borrows that same swap. It is deliberately NOT a
                // wash of highlight colour across the field: painting the background
                // flattens the bevel and reads as a mis-coloured patch sitting on the
                // bar rather than as part of it. (The held state is not visible in the
                // reference, so this mirrors the unheld rule rather than confirming it.)
                const SPalette bp = (hovered == b || pressed == b) ? inverted(p) : p;

                // The float toggle carries its own icon instead of a bare raised rect,
                // and takes the same swap.
                if (b == BTN_FLOAT)
                    drawFloatToggle(cr, r.x, r.y, bp);
                else
                    glyphFrame(cr, r, bp.light, bp.dark);

                // Motif's other two are bare raised rects; Motif puts no icon in them.
            }
        }

        // Title, centred in the title field. Motif draws it in white on the face.
        if (!title.empty() && fontSize > 0) {
            const CBox field = titleField(size);

            // "Chicago" is the macOS System font as reissued in ChicagoFLF (nixpkgs
            // `chicago-font`) — the blocky, square-cornered face in examples/title.png.
            // The toy API takes one family with no fallback chain, so this relies on
            // fontconfig resolving it (it is installed here); if it ever is not, Cairo
            // silently substitutes a default face rather than failing.
            cairo_select_font_face(cr, TITLE_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, fontSize);

            // Aliased, per examples/title.png: the reference has NO intermediate
            // colours anywhere — every glyph pixel is either fully text or fully
            // background, so cairo's default greyscale antialiasing is wrong here.
            // Hinting stays on (the default) so stems snap to the pixel grid; without it
            // hinted-and-aliased text renders with visibly broken stems on some faces.
            static cairo_font_options_t* fontOpts = [] {
                cairo_font_options_t* o = cairo_font_options_create();
                cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_NONE);
                cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_FULL);
                cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_ON);
                return o;
            }();
            cairo_set_font_options(cr, fontOpts);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, title.c_str(), &ext);

            const double avail = field.w - 8;
            if (ext.width > avail && ext.width > 0) {
                // Shrink to fit rather than overflow into the button fields.
                cairo_set_font_size(cr, fontSize * avail / ext.width);
                cairo_text_extents(cr, title.c_str(), &ext);
            }

            // Centre on the FIELD (rows 6..20), not on the bar — the same band the button glyphs
            // centre on — and do it with the font's own ascent/descent rather than the
            // string's ink height, which shifts with the glyphs present. Baseline = centre
            // + (ascent - descent)/2 puts the middle of the ascent..descent box on it.
            cairo_font_extents_t fe;
            cairo_font_extents(cr, &fe);
            const double fieldTop    = BEVEL + 2 + GROOVE * 2;
            const double fieldCentre = fieldTop + (size.y - fieldTop - SIDE) / 2;

            // FLOOR both axes. With ANTIALIAS_NONE cairo rounds each glyph to whole
            // pixels independently, so a fractional origin spaces the stems unevenly —
            // and with the 1px shadow underneath, that reads as text drawn twice.
            const double tx = std::floor(field.x + (field.w - ext.width) / 2 - ext.x_bearing);
            const double ty = std::floor(fieldCentre + (fe.ascent - fe.descent) / 2);

            // Simple hard shadow, measured off examples/title.png: a second copy of the
            // same string offset ONE pixel down and right, in black. No blur, no alpha
            // ramp — the reference's shadow pixels are as solid as the glyph's. Drawn
            // first so the text sits on top of it.
            cairo_move_to(cr, tx + TITLE_SHADOW_DX, ty + TITLE_SHADOW_DY);
            cairo_set_source_rgb(cr, p.shadow.r, p.shadow.g, p.shadow.b);
            cairo_show_text(cr, title.c_str());

            cairo_move_to(cr, tx, ty);
            cairo_set_source_rgb(cr, p.title.r, p.title.g, p.title.b);
            cairo_show_text(cr, title.c_str());
        }

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

    SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale, bool active) {
        const int      w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));
        const SPalette p = currentPalette(active);

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

        // Corners. Each strip owns the two corners it runs through: the side strips run
        // the full window height so they take the top pair (and paint over the bar's top
        // rows, which is what makes the bevel read as spanning the full width), while the
        // bottom strip is drawn last and so takes the bottom pair. The top corner grab
        // boundaries need no new rules — the bar's bottom edge and its own field
        // separators already fall exactly on CORNER_GRAB.
        if (edge == DECORATION_EDGE_LEFT) {
            drawCorner(cr, 0, 0, TL, p);
            // Horizontal rule at each end. The top one lands on the bar's bottom edge.
            grabSeparator(cr, 0, CDE::CORNER_GRAB, 1, true, false, p);
            grabSeparator(cr, 0, size.y - CDE::CORNER_GRAB - 2, 1, true, false, p);
        } else if (edge == DECORATION_EDGE_RIGHT) {
            drawCorner(cr, 0, 0, TR, p);
            grabSeparator(cr, 0, CDE::CORNER_GRAB, 1, true, true, p);
            grabSeparator(cr, 0, size.y - CDE::CORNER_GRAB - 2, 1, true, true, p);
        } else if (edge == DECORATION_EDGE_BOTTOM) {
            drawCorner(cr, 0, 0, BL, p);
            drawCorner(cr, size.x - CDE::BORDER, 0, BR, p);

            // Vertical rule, spanning the middle 3 rows of the strip.
            grabSeparator(cr, CDE::CORNER_GRAB, 0, 0, false, true, p);
            grabSeparator(cr, size.x - CDE::CORNER_GRAB - 2, 0, 0, false, true, p);
        }

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

} // namespace CDE
