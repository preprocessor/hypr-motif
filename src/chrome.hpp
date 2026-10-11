#pragma once

#include <string>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace CDE {

    // Measured from example.png (2x-scaled, so image px / 2). See AGENTS.md.
    inline constexpr int BEVEL          = 2; // outer raised bevel thickness
    inline constexpr int SEPARATOR       = 1; // dark-over-light field divider
    inline constexpr int HIGHLIGHT       = 3; // white band along the bottom edge
    inline constexpr int BTN_SIZE        = 14;
    inline constexpr int LEFT_FIELD      = 18; // width of the single-button left field
    inline constexpr int RIGHT_FIELD     = 38; // width of the two-button right field
    inline constexpr int FIELD_INSET     = 2;

    enum eButton : int { BTN_CLOSE = 0, BTN_MIN, BTN_MAX, BTN_COUNT, BTN_NONE };

    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed);
    SP<Render::ITexture> renderTitle(const std::string& title, float scale, int fontSize);

    // Logical-px rect of a button within the bar, or {} for BTN_NONE / hidden buttons.
    CBox buttonRect(Vector2D size, int btn, bool showButtons);

    // x positions of the two field separators.
    double leftFieldEnd(Vector2D size);
    double rightFieldStart(Vector2D size);

    // The flexible region between the two button fields; the title is centred here.
    CBox titleField(Vector2D size);

} // namespace CDE