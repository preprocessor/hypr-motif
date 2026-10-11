#include "config.hpp"

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

using namespace Hyprutils::Memory;

namespace {

    SP<Config::Values::CIntValue>   barHeight, titleFontSize;
    SP<Config::Values::CColorValue> face, light, dark, highlight, title;
    SP<Config::Values::CBoolValue>  showButtons;

    uint64_t g_generation = 0;

    // Packed config colours are 0xAARRGGBB.
    constexpr Config::INTEGER HEX(uint32_t argb) {
        return argb;
    }

} // namespace

namespace CDE {

    void registerConfig(HANDLE h) {
        barHeight   = makeShared<Config::Values::CIntValue>("plugin:cde:bar_height", "Title bar height in logical pixels", CDE::BAR_HEIGHT);
        face        = makeShared<Config::Values::CColorValue>("plugin:cde:color_face", "Title bar face colour", HEX(0xff999999));
        light       = makeShared<Config::Values::CColorValue>("plugin:cde:color_light", "Bevel highlight colour", HEX(0xffd1d1d1));
        dark        = makeShared<Config::Values::CColorValue>("plugin:cde:color_dark", "Bevel shadow colour", HEX(0xff4e4e4e));
        highlight   = makeShared<Config::Values::CColorValue>("plugin:cde:color_highlight", "Highlight along the bar's bottom edge", HEX(0xffffffff));
        title       = makeShared<Config::Values::CColorValue>("plugin:cde:color_title", "Title text colour", HEX(0xffffffff));
        showButtons = makeShared<Config::Values::CBoolValue>("plugin:cde:show_buttons", "Whether to draw the title bar buttons", true);
        titleFontSize = makeShared<Config::Values::CIntValue>("plugin:cde:title_font_size", "Title text size in points", 11);

        HyprlandAPI::addConfigValueV2(h, barHeight);
        HyprlandAPI::addConfigValueV2(h, face);
        HyprlandAPI::addConfigValueV2(h, light);
        HyprlandAPI::addConfigValueV2(h, dark);
        HyprlandAPI::addConfigValueV2(h, highlight);
        HyprlandAPI::addConfigValueV2(h, title);
        HyprlandAPI::addConfigValueV2(h, showButtons);
        HyprlandAPI::addConfigValueV2(h, titleFontSize);
    }

    SConfig config() {
        SConfig c;

        if (barHeight)
            c.barHeight = std::max(16, static_cast<int>(barHeight->value()));
        if (face)
            c.face = CHyprColor(face->value());
        if (light)
            c.light = CHyprColor(light->value());
        if (dark)
            c.dark = CHyprColor(dark->value());
        if (highlight)
            c.highlight = CHyprColor(highlight->value());
        if (title)
            c.title = CHyprColor(title->value());
        if (showButtons)
            c.showButtons = showButtons->value();
        if (titleFontSize)
            c.titleFontSize = std::max(6, static_cast<int>(titleFontSize->value()));

        return c;
    }

    void bumpGeneration() {
        ++g_generation;
    }

    uint64_t generation() {
        return g_generation;
    }

} // namespace CDE