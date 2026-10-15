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

    // The focused window paints with the `_active` colours, everything else with the
    // un-suffixed ones. Both sets are config values, so nothing here is hardcoded.
    //
    // The shadow is the one exception: it is NOT per-window. examples/title.png shows one
    // black shadow under both states, so a single colour serves both palettes.
    SPalette currentPalette(bool active) {
        const CDE::SConfig& c = CDE::config();

        SPalette p;
        p.face   = active ? c.faceActive : c.face;
        p.light  = active ? c.lightActive : c.light;
        p.dark   = active ? c.darkActive : c.dark;
        p.title  = active ? c.titleActive : c.title;
        p.shadow = c.titleShadow;
        return p;
    }

    // Swap light and dark, keep face, title and shadow. This is what makes a button read
    // as INSET: the lit edge moves to the bottom/right, the opposite of raised. Motif does
    // this for the held state, and hover borrows the same treatment.
    SPalette inverted(const SPalette& p) {
        SPalette flipped;
        flipped.face   = p.face;
        flipped.light  = p.dark;
        flipped.dark   = p.light;
        flipped.title  = p.title;
        flipped.shadow = p.shadow;
        return flipped;
    }

    // The colour a character in one of our glyph patterns stands for: 'L' is the lit
    // bevel colour, 'D' the dark one, anything else (used for the opaque interior of the
    // float icon) the face colour.
    CHyprColor colourForGlyphChar(char ch, const SPalette& p) {
        if (ch == 'L')
            return p.light;
        if (ch == 'D')
            return p.dark;
        return p.face;
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

    // A raised rect: `lit` along the top and left, `shade` along the bottom and right.
    //
    // ORDER MATTERS. The shade goes down FIRST so the lit colour wins both corners where
    // the two bevels meet — that is what the reference shows. The other way round, shade
    // overwrites them and the rect is a pixel wrong at each corner.
    void glyphFrame(cairo_t* cr, const CBox& b, const CHyprColor& lit, const CHyprColor& shade) {
        fill(cr, b.x, b.y + b.h - CDE::SIDE, b.w, CDE::SIDE, shade);
        fill(cr, b.x + b.w - CDE::SIDE, b.y, CDE::SIDE, b.h, shade);

        fill(cr, b.x, b.y, b.w, CDE::SIDE, lit);
        fill(cr, b.x, b.y, CDE::SIDE, b.h, lit);
    }

    // The one and only bevel painter, shared by the title bar and the frame strips.
    //
    // A strip is three bands: a lit one, the grey face in the middle, a dark one. Which
    // of the lit/dark bands is the WIDE one depends on which end faces away from the
    // client, which is what `outerIsHigh` says:
    //
    //   left   (outerIsHigh=false)  light(2) face(2) dark(1)
    //   right  (outerIsHigh=true)   light(1) face(2) dark(2)
    //   bottom (outerIsHigh=true)   light(1) face(2) dark(2)
    void bevelBands(cairo_t* cr, double a, double b, double len, bool vertical, bool outerIsHigh, const SPalette& p) {
        const double outwardLitW  = CDE::BORDER - CDE::FACE_BAND - CDE::SIDE; // the wide band
        const double inwardLitW   = CDE::SIDE;                               // the narrow band
        const double litW         = outerIsHigh ? inwardLitW : outwardLitW;
        const double darkW        = CDE::BORDER - CDE::FACE_BAND - litW;
        const double faceOffset   = litW;
        const double darkOffset   = litW + CDE::FACE_BAND;

        if (vertical) {
            fill(cr, a, b, litW, len, p.light);
            fill(cr, a + faceOffset, b, CDE::FACE_BAND, len, p.face);
            fill(cr, a + darkOffset, b, darkW, len, p.dark);
        } else {
            // Horizontal: `len` runs along x and the band widths along y. Swapping these
            // draws the lit band as a 1px column, which leaves the bottom frame missing
            // its inner highlight entirely.
            fill(cr, a, b, len, litW, p.light);
            fill(cr, a, b + faceOffset, len, CDE::FACE_BAND, p.face);
            fill(cr, a, b + darkOffset, len, darkW, p.dark);
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
                const char ch = pat[r * CDE::BORDER + c];
                fill(cr, x + c, y + r, 1, 1, colourForGlyphChar(ch, p));
            }
    }

    // The float-toggle glyph, measured from examples/float-toggle.png (15x15, trimmed to
    // its 9x9 core): two overlapping 6x6 outlined squares, the back one offset up-right by
    // three, the front one's interior opaque so it occludes the back one's bottom-left
    // corner. Same bevel convention as the other glyphs: light top/left, dark bottom/right.
    // '.' is transparent. This reference outranks CDE-7.png for this button.
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
                fill(cr, x + c, y + r, 1, 1, colourForGlyphChar(ch, p));
            }
    }

    // A rule marking off the corner grab square on a frame strip. The lit pixel goes on
    // local x=0 in both directions — the OUTER edge on the left strip, the inner one on
    // the right, as the reference has it — so `darkOuter` keeps the outermost pixel of the
    // second row dark where that matters.
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

    // Font options that make text hard-edged, per examples/title.png: no intermediate
    // colours anywhere in the reference's glyphs. Built once and shared — cairo copies
    // them into the context on set, so the caller must not free them.
    //
    // Hinting stays ON: hinted-and-aliased text renders solid stems, while aliased without
    // hinting breaks stems on some faces.
    cairo_font_options_t* aliasedFontOptions() {
        static cairo_font_options_t* options = [] {
            cairo_font_options_t* o = cairo_font_options_create();
            cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_NONE);
            cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_FULL);
            cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_ON);
            return o;
        }();
        return options;
    }

} // namespace

namespace CDE {

    // Size of the corner grab square: bar height minus one, from both reference data
    // points (21 at a 22px bar, 24 at 25px). Floored at 6, below which the grab rules and
    // the field separators overlap into mush. See AGENTS.md `## Corners`.
    int cornerGrab() {
        return std::max(6, config().barHeight - 1);
    }

    // A button field is a SQUARE sized by the bar: as wide as the field is tall. At the
    // 22px bar the reference measured a 15px field, and 22 - FIELD_TOP - SIDE is exactly
    // 15, so this reproduces it and then grows both axes with the bar.
    double buttonFieldWidth(Vector2D size) {
        return std::max(0.0, size.y - FIELD_TOP - SIDE);
    }

    // Bar-relative x of the separator closing the close field: 21 at the reference's
    // 22px bar, tracking the field width as the bar grows.
    double leftFieldEnd(Vector2D size) {
        return LEFT_SEP + SIDE * 2 + buttonFieldWidth(size);
    }

    // Bar-relative x where the right-hand field pair starts: width-40 at the reference's
    // 22px bar.
    double rightFieldStart(Vector2D size) {
        return size.x - RIGHT_MARGIN - 2 * (buttonFieldWidth(size) + SIDE * 2);
    }

    // The flexible region between the close field and the right pair.
    CBox titleFieldRect(Vector2D size) {
        const double x = leftFieldEnd(size) + SIDE * 2;
        return {x, 0, std::max(0.0, rightFieldStart(size) - x), size.y};
    }

    // The clickable area of one button: the square field the glyph sits in, which is also
    // what the hover highlight covers. Hit-testing the glyph instead leaves a 9x4px target
    // on close and clicks feel spotty — the user aims at the field, as Motif makes the
    // whole field clickable.
    CBox buttonFieldRect(Vector2D size, int btn, bool showButtons) {
        if (!showButtons || btn < 0 || btn >= BTN_COUNT)
            return {};

        const double fieldTop = FIELD_TOP;
        const double fieldH   = size.y - fieldTop - SIDE;
        const double fieldW   = buttonFieldWidth(size);

        // Close sits in the left field; float and max are the pair on the right, max second.
        double fieldX;
        if (btn == BTN_CLOSE)
            fieldX = LEFT_SEP + SIDE * 2;
        else if (btn == BTN_MAX)
            fieldX = rightFieldStart(size) + SIDE * 2 + fieldW + SIDE * 2;
        else
            fieldX = rightFieldStart(size) + SIDE * 2;

        return {std::floor(fieldX), fieldTop, fieldW, fieldH};
    }

    // The glyph inside a button field: a fixed measured size, centred on the FIELD rather
    // than on the bar. Centring on the bar puts every glyph ~1.5px high at the reference
    // size, which reads as a vertical alignment bug.
    CBox buttonGlyphRect(Vector2D size, int btn, bool showButtons) {
        const CBox field = buttonFieldRect(size, btn, showButtons);
        if (!field.w)
            return {};

        const double centre = field.y + field.h / 2;

        double       w = CLOSE_GLYPH_W, h = CLOSE_GLYPH_H;
        if (btn == BTN_MAX)
            w = h = MAX_GLYPH;
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

    // Fills the whole bar with the face colour, then the top bevel and the groove that
    // separate the decorative rows from the field row everything else lives in.
    void drawBarBackground(cairo_t* cr, Vector2D size, const SPalette& p) {
        fill(cr, 0, 0, size.x, size.y, p.face);

        fill(cr, 0, 0, size.x, BEVEL, p.light);
        fill(cr, 0, BEVEL + 2, size.x, GROOVE, p.dark);
        // The groove's light half starts right after its dark half, so this is
        // FIELD_TOP - GROOVE, not FIELD_TOP: the field begins only once both are drawn.
        fill(cr, 0, FIELD_TOP - GROOVE, size.x, GROOVE, p.light);

        // The bar's left and right ends ARE the frame's side bevels — the bar spans the
        // full window width, so these columns are shared with the side strips and must
        // be painted by the same routine or the join shows.
        bevelBands(cr, 0, BEVEL, size.y - BEVEL, true, false, p);
        bevelBands(cr, size.x - CDE::BORDER, BEVEL, size.y - BEVEL, true, true, p);

        fill(cr, 0, size.y - SIDE, size.x, SIDE, p.dark);
    }

    // The five separators that divide the bar into fields: the left inset, the one closing
    // the close field, the three on the right. Skipped on a window too narrow to hold them.
    void drawFieldSeparators(cairo_t* cr, Vector2D size, const SPalette& p) {
        const double rightFieldX = rightFieldStart(size);
        if (LEFT_SEP >= rightFieldX)
            return;

        const double sepTop   = FIELD_TOP;
        const double sepH     = size.y - sepTop - SIDE;
        const double fieldW   = buttonFieldWidth(size);

        separator(cr, LEFT_SEP, sepTop, sepH, p);
        separator(cr, leftFieldEnd(size), sepTop, sepH, p);
        separator(cr, rightFieldX, sepTop, sepH, p);
        separator(cr, rightFieldX + fieldW + SIDE * 2, sepTop, sepH, p);
        separator(cr, size.x - RIGHT_MARGIN, sepTop, sepH, p);
    }

    // The vertical rules marking off the corner grab square. They run the full height of
    // the square, from bar-rel 1 to the bottom edge, unlike the field separators which
    // start below the bevel — and they land on the same columns as two of those, so
    // drawing them separately is the only way to reach further without widening them.
    // The groove's light row breaks them, as in the reference, hence the two spans.
    void drawCornerGrabRules(cairo_t* cr, Vector2D size, const SPalette& p) {
        const int grab = cornerGrab();
        // Below this width the two rules would collide, so a narrow window gets none.
        if (size.x <= grab * 2 + 4)
            return;

        const double aboveTop = 1;                      // first row of the rule
        const double aboveBot = BEVEL + GROOVE + 1;     // last row of the upper span, inclusive
        const double belowTop = aboveBot + GROOVE + 1;  // first row after the groove's light row

        auto drawRule = [&](double x) {
            separator(cr, x, aboveTop, aboveBot - aboveTop + 1, p);
            separator(cr, x, belowTop, size.y - belowTop, p);
        };

        drawRule(grab);
        drawRule(size.x - grab - 2);
    }

    // The three buttons. Hover and held both read as INSET: the lit edge moves to the
    // bottom/right, the opposite of raised. Hover is deliberately NOT a wash of colour
    // across the field — that flattens the bevel and reads as a patch sitting on the bar
    // rather than as part of it.
    void drawButtons(cairo_t* cr, Vector2D size, int hovered, int pressed, const SPalette& palette) {
        if (!config().showButtons)
            return;

        for (int b = 0; b < BTN_COUNT; ++b) {
            const CBox field = buttonFieldRect(size, b, true);
            const CBox glyph = buttonGlyphRect(size, b, true);
            if (!field.w || !glyph.w)
                continue;

            const bool      highlighted = hovered == b || pressed == b;
            const SPalette p = highlighted ? inverted(palette) : palette;

            // The highlight covers the FIELD — the whole square the user clicks — not the
            // glyph. The glyph is a small fixed size inside a square that grows with the
            // bar, so lighting it alone leaves most of the clickable area inert.
            if (highlighted)
                glyphFrame(cr, field, p.light, p.dark);

            // Only the float toggle has artwork inside it. Close and max are bare boxes,
            // as the reference draws them.
            if (b == BTN_FLOAT)
                drawFloatToggle(cr, glyph.x, glyph.y, p);
            else
                glyphFrame(cr, glyph, p.light, p.dark);
        }
    }

    // The window title, centred in the title field, aliased, with a hard 1px shadow.
    //
    // The baseline is centred on the FIELD (rows FIELD_TOP..size.y-1) using the font's own
    // ascent/descent — not the string's ink height, which moves with the glyphs present.
    // Both axes are floored: with antialiasing off cairo rounds each glyph to whole pixels
    // independently, so a fractional origin spaces the stems unevenly and, with the shadow
    // underneath, reads as text drawn twice.
    void drawTitle(cairo_t* cr, Vector2D size, const std::string& title, int fontSize, const SPalette& p) {
        if (title.empty() || fontSize <= 0)
            return;

        const CBox field = titleFieldRect(size);

        cairo_select_font_face(cr, TITLE_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, fontSize);
        cairo_set_font_options(cr, aliasedFontOptions());

        cairo_text_extents_t textExtents;
        cairo_text_extents(cr, title.c_str(), &textExtents);

        // Shrink to fit rather than overflow into the button fields.
        const double available = field.w - TITLE_PADDING;
        if (textExtents.width > available && textExtents.width > 0) {
            cairo_set_font_size(cr, fontSize * available / textExtents.width);
            cairo_text_extents(cr, title.c_str(), &textExtents);
        }

        cairo_font_extents_t fontExtents;
        cairo_font_extents(cr, &fontExtents);

        const double fieldCentre = FIELD_TOP + (size.y - FIELD_TOP - SIDE) / 2;
        const double baseline     = std::floor(fieldCentre + (fontExtents.ascent - fontExtents.descent) / 2);
        const double left         = std::floor(field.x + (field.w - textExtents.width) / 2 - textExtents.x_bearing);

        // The shadow: the same string one pixel down and right, in black. No blur and no
        // alpha ramp — the reference's shadow pixels are as solid as the glyphs'. Drawn
        // first so the text sits on top of it.
        cairo_move_to(cr, left + TITLE_SHADOW_DX, baseline + TITLE_SHADOW_DY);
        cairo_set_source_rgb(cr, p.shadow.r, p.shadow.g, p.shadow.b);
        cairo_show_text(cr, title.c_str());

        cairo_move_to(cr, left, baseline);
        cairo_set_source_rgb(cr, p.title.r, p.title.g, p.title.b);
        cairo_show_text(cr, title.c_str());
    }

    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize, bool active) {
        const int surfaceW = std::max(1, sc<int>(size.x * scale));
        const int surfaceH = std::max(1, sc<int>(size.y * scale));

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfaceW, surfaceH);
        cairo_t*         cr   = cairo_create(surf);
        cairo_scale(cr, scale, scale); // draw in logical pixels; the surface is device-sized

        const SPalette p = currentPalette(active);

        drawBarBackground(cr, size, p);
        drawFieldSeparators(cr, size, p);
        drawCornerGrabRules(cr, size, p);
        drawButtons(cr, size, hovered, pressed, p);
        drawTitle(cr, size, title, fontSize, p);

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

    // Paints one edge of the window frame: the bevel bands, then the two mitred corners
    // that strip owns.
    //
    // Ownership is what stops the corners fighting: the side strips run the FULL window
    // height so they take the top pair (painting over the bar's top rows, which is what
    // makes the bevel read as spanning the full width), and the bottom strip is drawn last
    // so it takes the bottom pair. No reordering needed.
    void drawFrameStrip(cairo_t* cr, uint32_t edge, Vector2D size, const SPalette& p) {
        // The grab square is measured along the BAR's height, so the same value marks the
        // grab square here as it does on the bar.
        const int grab = cornerGrab();

        switch (edge) {
            case DECORATION_EDGE_LEFT: bevelBands(cr, 0, 0, size.y, true, false, p); break;
            case DECORATION_EDGE_RIGHT: bevelBands(cr, 0, 0, size.y, true, true, p); break;
            case DECORATION_EDGE_BOTTOM: bevelBands(cr, 0, 0, size.x, false, true, p); break;
            default: return;
        }

        if (edge == DECORATION_EDGE_LEFT) {
            drawCorner(cr, 0, 0, TL, p);
            // Horizontal rule at each end. The top one lands on the bar's bottom edge.
            grabSeparator(cr, 0, grab, 1, true, false, p);
            grabSeparator(cr, 0, size.y - grab - 2, 1, true, false, p);
        } else if (edge == DECORATION_EDGE_RIGHT) {
            drawCorner(cr, 0, 0, TR, p);
            grabSeparator(cr, 0, grab, 1, true, true, p);
            grabSeparator(cr, 0, size.y - grab - 2, 1, true, true, p);
        } else {
            drawCorner(cr, 0, 0, BL, p);
            drawCorner(cr, size.x - CDE::BORDER, 0, BR, p);
            // Vertical rule, spanning the middle rows of the strip.
            grabSeparator(cr, grab, 0, 0, false, true, p);
            grabSeparator(cr, size.x - grab - 2, 0, 0, false, true, p);
        }
    }

    SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale, bool active) {
        const int surfaceW = std::max(1, sc<int>(size.x * scale));
        const int surfaceH = std::max(1, sc<int>(size.y * scale));

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfaceW, surfaceH);
        cairo_t*         cr   = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        drawFrameStrip(cr, edge, size, currentPalette(active));

        cairo_destroy(cr);

        SP<Render::ITexture> tex = g_pHyprRenderer->createTexture(surf);
        cairo_surface_destroy(surf);
        return tex;
    }

} // namespace CDE
