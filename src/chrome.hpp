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
    inline constexpr int FLOAT_GLYPH = 9; // float-toggle icon, from examples/float-toggle.png
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


    enum eButton : int { BTN_CLOSE = 0, BTN_MIN, BTN_MAX, BTN_COUNT, BTN_NONE };

    // One texture holds the whole bar, title text included. Drawing the title into
    // the same Cairo surface avoids queueing a second pass element, which the
    // compositor culls when the opaque bar already covers that area.
    // `active` selects the focused-window palette (see currentPalette in chrome.cpp).
    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize, bool active);

    // The frame strip for a non-top edge. `edge` is an eDecorationEdges value; the
    // strip's own width/height is whichever axis is the border thickness.
    SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale, bool active);


    // Logical-px rect of a button's glyph box within the bar, or {} for none/hidden.
    CBox buttonRect(Vector2D size, int btn, bool showButtons);

    // The flexible region between the left button field and the right pair.
    CBox titleField(Vector2D size);

    // x of the left field's trailing separator and the right pair's leading one.
    double leftFieldEnd(Vector2D size);
    double rightFieldStart(Vector2D size);

} // namespace CDE
