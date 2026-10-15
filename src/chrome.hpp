#pragma once

#include <string>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace CDE {

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

    // Hard shadow under the title text, from examples/title.png: a copy of the string
    // offset one pixel down and right, drawn in the bevel's dark colour before the text.
    // No blur and no alpha ramp — the reference's shadow pixels are as solid as the glyphs.
    inline constexpr int TITLE_SHADOW_DX = 1;
    inline constexpr int TITLE_SHADOW_DY = 1;

    // Title face: the macOS System font as reissued in ChicagoFLF (nixpkgs
    // `chicago-font`), which is the blocky face examples/title.png was set in.
    inline constexpr auto TITLE_FONT = "Chicago";
    inline constexpr int LEFT_SEP     = 4; // first separator, bar-relative
    inline constexpr int RIGHT_MARGIN = 6; // window edge to the outermost separator

    // Window frame. 5px, and it is a three-band bevel, not two: the outer half is lit
    // and shadowed with a grey face between. Left is lit on its outer side; right and
    // bottom on their inner side.
    inline constexpr int BORDER      = 5;
    inline constexpr int FACE_BAND  = 2; // grey face between the two bevels

    // CDE marks a square resize grip at each corner, delimited by separator lines.
    // Measured from CDE-7.png: with a 22px bar the vertical rule sits at bar-rel 21 and
    // width-23, the horizontal 21px up from the bottom edge — and new-standard.png gives
    // the confirming second data point (24 with a 25px bar). Both are BAR_HEIGHT - 1, so
    // the grab is DERIVED from the configured bar height rather than fixed; the constant
    // below is only the measured value for the default bar, kept as the reference.
    //
    // This is the "corner accent": it scales with the bar, and it has to, because it is
    // measured along the bar's own height — on the side and bottom frames the same
    // distance marks the same square, so one value drives all four corners.
    inline constexpr int CORNER_GRAB_REF = 21;

    // Live corner-grab size for the current bar height.
    int cornerGrab();

    // Named for what they DO, not for Motif's usual role: the middle button carries a
    // float-toggle glyph (examples/float-toggle.png) and toggles floating, not minimize.
    // Motif's window-menu slot is where we put close, to keep the 1-left/2-right shape.
    enum eButton : int { BTN_CLOSE = 0, BTN_FLOAT, BTN_MAX, BTN_COUNT, BTN_NONE };

    // One texture holds the whole bar, title text included. Drawing the title into
    // the same Cairo surface avoids queueing a second pass element, which the
    // compositor culls when the opaque bar already covers that area.
    // `active` selects the focused-window palette (see currentPalette in chrome.cpp).
    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize, bool active);

    // The frame strip for a non-top edge. `edge` is an eDecorationEdges value; the
    // strip's own width/height is whichever axis is the border thickness.
    SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale, bool active);


    // Logical-px rect of a button's glyph within the bar, or {} for none/hidden. The
    // glyphs keep their measured size; the FIELD they sit in grows with the bar.
    CBox buttonRect(Vector2D size, int btn, bool showButtons);

    // The clickable FIELD around that glyph — what hit-testing uses, and the hover
    // highlight. Aiming at the glyph alone is a 9x4px target on close and feels spotty.
    CBox buttonFieldRect(Vector2D size, int btn, bool showButtons);

    // Which button a bar-local point is over, or BTN_NONE. Draws no icon and dispatches
    // nothing; it is geometry only.
    int  buttonAt(Vector2D size, const Vector2D& p, bool showButtons);

    // The flexible region between the left button field and the right pair.
    CBox titleField(Vector2D size);

    // x of the left field's trailing separator and the right pair's leading one.
    double leftFieldEnd(Vector2D size);
    double rightFieldStart(Vector2D size);

} // namespace CDE
