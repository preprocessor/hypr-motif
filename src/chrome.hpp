#pragma once

#include <string>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace CDE {

    // Measured from examples/CDE-7.png, which is 1:1 with no scaling to undo.
    // Reference for every number here; see AGENTS.md.
    //
    // Vertical, top-down, for a BAR_HEIGHT of 22:
    //   rows 0-1  light   outer top bevel
    //   rows 2-3  face
    //   row  4    dark    ┐ groove
    //   row  5    light   ┘
    //   rows 6-20 face    fields, glyphs and title live here
    //   row  21   dark    bottom edge
    //
    // There is NO white highlight band — what looks like one at a glance is the
    // title text, which is #ffffff.
    inline constexpr int BAR_HEIGHT   = 22;
    inline constexpr int BEVEL       = 2; // outer top bevel thickness
    inline constexpr int GROOVE      = 1; // dark+light groove below the bevel
    inline constexpr int SIDE        = 1; // separator is dark+light, this is each
    inline constexpr int FIELD_W     = 15; // width of a button field
    inline constexpr int LEFT_SEP     = 4; // first separator, bar-relative
    inline constexpr int RIGHT_MARGIN = 6; // window edge to the outermost separator

    // Window frame. 5px, and it is a three-band bevel, not two: the outer half is lit
    // and shadowed with a grey face between. Left is lit on its outer side; right and
    // bottom on their inner side.
    inline constexpr int BORDER      = 5;
    inline constexpr int FACE_BAND  = 2; // grey face between the two bevels

    // CDE marks a 21px square resize grip at each corner, delimited by separator lines.
    // Measured from CDE-7.png: the vertical rule sits at bar-rel 21 and width-23, the
    // horizontal 21px up from the bottom edge. Fixed, not derived — the only other
    // window in that screenshot is cut off at the image edge, so there is nothing to
    // confirm a formula against.
    inline constexpr int CORNER_GRAB = 21;

    // How far the two 45-degree chamfers wrap PAST the window corner: the bottom edge's
    // texture starts this many px left of the window, the right edge's this many px above
    // it. The chamfer formula is shifted by the same amount (see renderDitherShadow), so
    // the fade keeps its shape instead of being truncated at the box edge — growing the box
    // alone would only add blank columns, because the extra distance would be >= the gate.
    inline constexpr int SHADOW_WRAP = 4;

    enum eButton : int { BTN_CLOSE = 0, BTN_MIN, BTN_MAX, BTN_COUNT, BTN_NONE };

    // One texture holds the whole bar, title text included. Drawing the title into
    // the same Cairo surface avoids queueing a second pass element, which the
    // compositor culls when the opaque bar already covers that area.
    // `active` selects the focused-window palette (see currentPalette in chrome.cpp).
    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize, bool active);

    // The frame strip for a non-top edge. `edge` is an eDecorationEdges value; the
    // strip's own width/height is whichever axis is the border thickness.
    SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale, bool active);

    // Ordered 8x8 Bayer dither drop shadow, cast on right and bottom edges. `color` is
    // the strong inner tone, `color2` the soft outer one; an alpha-0 `color2` collapses
    // the ramp back to a single tone.
    SP<Render::ITexture> renderDitherShadow(uint32_t edge, Vector2D size, float scale, int shadowRange, CHyprColor color, CHyprColor color2);


    // Logical-px rect of a button's glyph box within the bar, or {} for none/hidden.
    CBox buttonRect(Vector2D size, int btn, bool showButtons);

    // The flexible region between the left button field and the right pair.
    CBox titleField(Vector2D size);

    // x of the left field's trailing separator and the right pair's leading one.
    double leftFieldEnd(Vector2D size);
    double rightFieldStart(Vector2D size);

} // namespace CDE
