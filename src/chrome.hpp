#pragma once

#include <string>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace CDE {

    // Measured from example.png (2x-scaled, so image px / 2). See AGENTS.md.
    //
    // Vertical, top-down, for a BAR_HEIGHT of 25:
    //   rows 0-1  light   outer top bevel
    //   rows 2-3  face
    //   row  4    dark    groove under the bevel
    //   row  5    light
    //   rows 6-23 face    content lives here
    //   row  24   dark    bottom edge
    //
    // There is NO white highlight band — what looked like one at a glance is the
    // title glyphs themselves, which are #ffffff.
    inline constexpr int BEVEL       = 2; // top bevel thickness
    inline constexpr int GROOVE      = 1; // dark+light groove below the bevel
    inline constexpr int SIDE        = 1; // outer side bevel
    inline constexpr int FIELD_W     = 18; // width of a button field
    inline constexpr int BTN_SIZE    = 13; // glyph box inside a field

    enum eButton : int { BTN_CLOSE = 0, BTN_MIN, BTN_MAX, BTN_COUNT, BTN_NONE };

    // One texture holds the whole bar, title text included. Drawing the title into
    // the same Cairo surface avoids queueing a second pass element, which the
    // compositor culls when the opaque bar already covers that area.
    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed, const std::string& title, int fontSize);

    // Logical-px rect of a button's glyph box within the bar, or {} for none/hidden.
    CBox buttonRect(Vector2D size, int btn, bool showButtons);

    // The flexible region between the left button field and the right pair.
    CBox titleField(Vector2D size);

    // x of the left field's trailing separator and the right pair's leading one.
    double leftFieldEnd(Vector2D size);
    double rightFieldStart(Vector2D size);

} // namespace CDE