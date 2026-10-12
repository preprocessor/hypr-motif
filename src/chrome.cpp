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
        CHyprColor face, light, dark, title, hover;
    };

    // The active (focused) window gets its own palette; everything else uses the inactive
    // one. Both sets are configurable, so nothing here is hardcoded.
    SPalette currentPalette(bool active) {
        const auto c = CDE::config();
        return {active ? c.faceActive : c.face, active ? c.lightActive : c.light, active ? c.darkActive : c.dark, active ? c.titleActive : c.title, CHyprColor{0.72, 0.72, 0.72, 1}};
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

        // Floor the origin on both axes: the field centre is x.5 for an odd field height, and
        // `FIELD_W - w` is odd for the 4px minimize glyph. Handing cairo a fractional
        // origin makes it blend the glyph across two rows/columns instead of hitting the
        // reference cells exactly, which shows up as pale half-pixels down one edge.
        return {std::floor(fieldX + (fieldW - w) / 2), std::floor(centre - h / 2), w, h};
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

                if (hovered == b)
                    fill(cr, r.x - 1, r.y - 1, r.w + 2, r.h + 2, p.hover);

                // Motif inverts the bevel when a button is held, so the raised colour
                // becomes dark. Same painter, colours swapped — the raised edge still
                // wins the corners. (The held state is not visible in the reference, so
                // this only mirrors the unheld rule rather than confirming it.)
                glyphFrame(cr, r, pressed == b ? p.dark : p.light, pressed == b ? p.light : p.dark);

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

    static constexpr uint8_t BAYER8[8][8] = {
        {  0, 32,  8, 40,  2, 34, 10, 42 },
        { 48, 16, 56, 24, 50, 18, 58, 26 },
        { 12, 44,  4, 36, 14, 46,  6, 38 },
        { 60, 28, 52, 20, 62, 30, 54, 22 },
        {  3, 35, 11, 43,  1, 33,  9, 41 },
        { 51, 19, 59, 27, 49, 17, 57, 25 },
        { 15, 47,  7, 39, 13, 45,  5, 37 },
        { 63, 31, 55, 23, 61, 29, 53, 21 },
    };

    SP<Render::ITexture> renderDitherShadow(uint32_t edge, Vector2D size, float scale, int shadowRange, CHyprColor color, CHyprColor color2) {
        const int w = std::max(1, sc<int>(size.x * scale)), h = std::max(1, sc<int>(size.y * scale));
        const double rScaled = shadowRange * scale;
        if (rScaled <= 0)
            return nullptr;

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        uint32_t*        data = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surf));
        const int        stride = cairo_image_surface_get_stride(surf) / sizeof(uint32_t);

        const uint8_t a = sc<uint8_t>(std::clamp(color.a, 0.0, 1.0) * 255.0);
        const uint8_t cr = sc<uint8_t>(std::clamp(color.r * color.a, 0.0, 1.0) * 255.0);
        const uint8_t cg = sc<uint8_t>(std::clamp(color.g * color.a, 0.0, 1.0) * 255.0);
        const uint8_t cb = sc<uint8_t>(std::clamp(color.b * color.a, 0.0, 1.0) * 255.0);
        const uint32_t dot = (a << 24) | (cr << 16) | (cg << 8) | cb;

        // Second tone, same packing. It fills the band density/2..density so the ramp has
        // a tone step instead of falling straight from `dot` to nothing.
        const uint8_t a2 = sc<uint8_t>(std::clamp(color2.a, 0.0, 1.0) * 255.0);
        const uint8_t cr2 = sc<uint8_t>(std::clamp(color2.r * color2.a, 0.0, 1.0) * 255.0);
        const uint8_t cg2 = sc<uint8_t>(std::clamp(color2.g * color2.a, 0.0, 1.0) * 255.0);
        const uint8_t cb2 = sc<uint8_t>(std::clamp(color2.b * color2.a, 0.0, 1.0) * 255.0);
        const uint32_t dot2 = (a2 << 24) | (cr2 << 16) | (cg2 << 8) | cb2;


        const bool isBottom = edge == DECORATION_EDGE_BOTTOM;
        // For bottom: width includes window plus corner. Window right edge is at w - rScaled.
        const double winRightScaled = std::max(0.0, w - rScaled);
        // Bottom-right corner only: dividing the radial distance by this pushes the
        // zero-density contour from rScaled out to rScaled * CORNER_ROUND, so the arc
        // bulges diagonally past the corner further than the flat strips fade — the
        // corner reads round. The other two corners are 45-degree chamfers instead.
        constexpr double CORNER_ROUND = 2.0;

        for (int y = 0; y < h; ++y) {
            uint32_t* row = data + y * stride;
            for (int x = 0; x < w; ++x) {
                double d = 0.0;
                if (isBottom) {
                    if (x < rScaled) {
                        // Bottom-left corner: 45-degree chamfer. Manhattan distance has
                        // iso-lines that are exact 45-degree diagonals, so the shadow's
                        // outer edge cuts straight across the corner. At the junction
                        // (x = rScaled-1) dx is 0 and d reduces to y — continuous with
                        // the flat strip.
                        const double dx = rScaled - 1.0 - x;
                        d = dx + y;
                    } else if (x <= winRightScaled) {
                        // Flat bottom strip
                        d = y;
                    } else {
                        // Bottom-right corner: enlarged quarter-round (see CORNER_ROUND).
                        const double dx = x - winRightScaled;
                        const double dy = y;
                        d = std::hypot(dx, dy) / CORNER_ROUND;
                    }
                } else {
                    // Right edge: x is distance outward from window.
                    if (y < rScaled) {
                        // Top-right corner: 45-degree chamfer, same construction as the
                        // bottom-left — at y = rScaled-1, d reduces to x.
                        const double dy = rScaled - 1.0 - y;
                        d = x + dy;
                    } else {
                        // Flat right strip
                        d = x;
                    }
                }

                if (d < rScaled) {
                    const double density = 1.0 - (d / rScaled);
                    // The distance axis must always drive the Bayer ROW, or the two edges
                    // fade differently at the same offset: BAYER8 is not transpose-symmetric,
                    // so column k distributes its values differently from row k (measured:
                    // 75% dots on the bottom vs 50% on the right at 4px out). Bottom walks
                    // rows with y; the right edge must therefore walk rows with x.
                    //
                    // Two tones off ONE cell: `density > t` paints the strong colour,
                    // otherwise `2*density > t` (t in [density, 2*density)) paints the soft
                    // colour. Both thresholds share the cell, so the second band is the
                    // exact complement of the first — no second matrix, and the transpose
                    // rule above still holds for both decisions. The ramp picks up a tone
                    // step (dot -> dot2 -> nothing) instead of falling straight to zero.
                    const double threshold = isBottom ? (BAYER8[y % 8][x % 8] + 0.5) / 64.0
                                                      : (BAYER8[x % 8][y % 8] + 0.5) / 64.0;
                    if (density > threshold)
                        row[x] = dot;
                    else if (density * 2.0 > threshold)
                        row[x] = dot2;
                    else
                        row[x] = 0;
                } else {
                    row[x] = 0;
                }
            }
        }

        cairo_surface_mark_dirty(surf);
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
