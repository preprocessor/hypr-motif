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

    // Top of the band the fields, glyphs and title live in: everything above the groove
    // (bevel, plus its two face rows, plus the groove itself). The one number that says
    // "below the decorative part of the bar".
    inline constexpr int FIELD_TOP = BEVEL + 2 + GROOVE * 2;

    // Glyph sizes, all measured from the references. These do NOT scale with the bar — the
    // field they sit in grows, the artwork inside it keeps its measured size.
    inline constexpr int CLOSE_GLYPH_W = 9;  // left (window menu) slot in CDE-7.png
    inline constexpr int CLOSE_GLYPH_H = 4;
    inline constexpr int FLOAT_GLYPH   = 9;  // examples/float-toggle.png
    inline constexpr int MAX_GLYPH     = 11; // 9x9 in CDE-7.png, enlarged by request

    // Breathing room either side of the title text inside the title field.
    inline constexpr int TITLE_PADDING = 8;

    // Clearance the drag rules leave between themselves and the nearest button field
    // separator or title glyph. They mark the middle of the bar as the drag area, so they
    // must not read as part of the text or of the controls.
    inline constexpr int DRAG_RULE_GAP = 10;

    // The drag rules are three light lines stacked in the field band, each with its dark
    // shadow one pixel right and one row below.
    inline constexpr int DRAG_RULE_LINES = 4;

    // Shadow offset, and the face row between one line's shadow and the next line's. The gap is
    // fixed: it is the measured one, and growing it would spread the lines apart instead of
    // leaving the clearance above and below them. The clearance itself is derived from the
    // field band in drawDragRules — 3px at the reference bar.
    inline constexpr int DRAG_RULE_SHADOW_DX = 1;
    inline constexpr int DRAG_RULE_STACK_GAP = 1;

    // A drag rule shorter than this is a stub that looks like a rendering fault, so it is
    // dropped and the title keeps the space to itself.
    inline constexpr int DRAG_RULE_MIN = 4;

    // Hard shadow under the title text, from examples/title.png: a copy of the string
    // offset one pixel down and right, drawn before the text. No blur and no alpha ramp —
    // the reference's shadow pixels are as solid as the glyphs'.
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
    // glyphs keep their measured size whatever the bar height; the FIELD they sit in grows.
    CBox buttonGlyphRect(Vector2D size, int btn, bool showButtons);

    // The clickable FIELD around that glyph — what hit-testing uses, and the hover
    // highlight. Aiming at the glyph alone is a 9x4px target on close and feels spotty.
    CBox buttonFieldRect(Vector2D size, int btn, bool showButtons);

    // Which button a bar-local point is over, or BTN_NONE. Draws no icon and dispatches
    // nothing; it is geometry only.
    int  buttonAt(Vector2D size, const Vector2D& p, bool showButtons);

    // The flexible region between the left button field and the right pair.
    CBox titleFieldRect(Vector2D size);

    // x of the left field's trailing separator and the right pair's leading one.
    double leftFieldEnd(Vector2D size);
    double rightFieldStart(Vector2D size);

} // namespace CDE
