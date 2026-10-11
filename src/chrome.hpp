#pragma once

#include <string>

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace CDE {

    // Layout is in logical pixels; everything is multiplied by the monitor scale on raster.
    inline constexpr int BAR_HEIGHT = 22;
    inline constexpr int BEVEL      = 2;
    inline constexpr int BTN_SIZE   = 15;

    enum eButton : int { BTN_MIN = 0, BTN_MAX, BTN_CLOSE, BTN_COUNT, BTN_NONE };

    // Glyphs are blitted separately from the bar body so they survive theme changes.
    SP<Render::ITexture> renderBar(Vector2D size, float scale, int hovered, int pressed);
    SP<Render::ITexture> renderTitle(const std::string& title, float scale);

    // Logical-px rect of a button within the bar, or {} for BTN_NONE.
    CBox buttonRect(Vector2D size, int btn);

}