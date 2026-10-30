#pragma once

#include <string>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace CDE {

    // Forward declaration is enough — it is only ever a `const&` parameter. Including
    // config.hpp here instead would be circular: it includes this file, and needs BAR_HEIGHT.
    struct SConfig;

    // Display names, one per edge. These are the decorations' identity: onNewWindow dedupes
    // on them and windowBox() matches the sibling list against them.
    inline constexpr auto DECO_BAR    = "CDE";
    inline constexpr auto DECO_LEFT   = "CDE:left";
    inline constexpr auto DECO_RIGHT  = "CDE:right";
    inline constexpr auto DECO_BOTTOM = "CDE:bottom";

    // Outranks Hyprland's own border decoration, which otherwise draws a second frame.
    inline constexpr int DECO_PRIORITY = 10005;

    // The frame strips must be positioned BEFORE the bar, not merely alongside it. The
    // positioner places a single-edge decoration inside the space the higher-priority
    // edges have already taken, so a LEFT/RIGHT strip placed after the TOP one starts one
    // bar-height DOWN and is given `client.h + barHeight` of height — its box then hangs
    // 2 * barHeight - BORDER below the window. Painting still uses outerBox(), so nothing
    // looks wrong, but `getBoxWithIncludedDecos` (which Hyprland's drop shadow measures)
    // unions that box in and the shadow grows a band below the window. Below, not above:
    // the bar's own box is correct either way.
    inline constexpr int DECO_PRIORITY_FRAME = 10006;

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

    // Where the groove's dark half starts, drawn directly in drawBarBackground.
    inline constexpr int GROOVE_TOP = BEVEL + 2;

    // A separator or drag rule is always SIDE of dark + SIDE of light.
    inline constexpr int SIDES_PAIRED = SIDE * 2;

    // Config floors. bar_height below FIELD_TOP leaves no band for the fields to live in.
    inline constexpr int MIN_BAR_HEIGHT      = 16;
    inline constexpr int TITLE_FONT_SIZE     = 12;
    inline constexpr int MIN_TITLE_FONT_SIZE = 6;

    // Below this the corner grab rules and the field separators overlap into mush.
    inline constexpr int MIN_CORNER_GRAB = 6;

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

    // The drag rules are four light lines stacked in the field band, each with its dark
    // shadow one pixel right and one row below.
    inline constexpr int DRAG_RULE_LINES = 4;

    // Shadow offset, and the face row between one line's shadow and the next line's. The
    // gap is fixed: growing it would spread the lines apart instead of leaving the
    // clearance above and below them, which drawDragRules derives from the field band
    // (3px at the reference bar).
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


    // Corner-grab size for a given bar height. See AGENTS.md `## Corners` for why it
    // scales with the bar.
    int cornerGrab(int barHeight);

    // Named for what they DO, not for Motif's usual role: the middle button carries a
    // float-toggle glyph (examples/float-toggle.png) and toggles floating, not minimize.
    // Motif's window-menu slot is where we put close, to keep the 1-left/2-right shape.
    enum eButton : int { BTN_CLOSE = 0, BTN_FLOAT, BTN_MAX, BTN_COUNT, BTN_NONE };

    // One texture holds the whole bar, title text included: a second pass element for the
    // title would be culled, since the opaque bar already covers that area.
    //
    // The palette, button visibility and font size come from `cfg` rather than from
    // config() inside — see the code-style rule in AGENTS.md.
    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, const SConfig& cfg, bool active);

    // The frame strip for one edge: left, right, bottom, or the top band that replaces the
    // bar when `cde:no_titlebar` hides it. `barVisible` tells the side strips whether their
    // top grab marker still has a bar to sit on.
    SP<Render::ITexture> renderBorder(uint32_t edge, Vector2D size, float scale, const SConfig& cfg, bool active, bool barVisible);

    // Which button a bar-local point is over, or BTN_NONE.
    //
    // The only part of the button layout CCdeDecoration.cpp needs: it draws from the same
    // rects this tests against, so the clickable area cannot drift from the drawn one.
    int buttonAt(Vector2D size, const Vector2D& p, bool showButtons);

} // namespace CDE
