#include "config.hpp"

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

using namespace Hyprutils::Memory;

namespace {

    SP<Config::Values::CIntValue>   barHeight, titleFontSize;
    SP<Config::Values::CColorValue> face, light, dark, highlight, title;
    SP<Config::Values::CColorValue> faceActive, lightActive, darkActive, titleActive;
    SP<Config::Values::CColorValue> shadowColor, shadowColor2;
    SP<Config::Values::CBoolValue>  showButtons, shadowEnabled;
    SP<Config::Values::CFloatValue> shadowOpacity1, shadowOpacity2;

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
        title       = makeShared<Config::Values::CColorValue>("plugin:cde:color_title", "Title text colour (inactive window)", HEX(0xffffffff));

        // Active-window palette, measured from examples/new-standard.png.
        faceActive   = makeShared<Config::Values::CColorValue>("plugin:cde:color_face_active", "Title bar face colour (active window)", HEX(0xffeda870));
        lightActive  = makeShared<Config::Values::CColorValue>("plugin:cde:color_light_active", "Bevel highlight colour (active window)", HEX(0xfff7d9c0));
        darkActive   = makeShared<Config::Values::CColorValue>("plugin:cde:color_dark_active", "Bevel shadow colour (active window)", HEX(0xff7d593b));
        titleActive  = makeShared<Config::Values::CColorValue>("plugin:cde:color_title_active", "Title text colour (active window)", HEX(0xff000000));
        showButtons = makeShared<Config::Values::CBoolValue>("plugin:cde:show_buttons", "Whether to draw the title bar buttons", true);
        titleFontSize = makeShared<Config::Values::CIntValue>("plugin:cde:title_font_size", "Title text size in points", 11);
        shadowEnabled = makeShared<Config::Values::CBoolValue>("plugin:cde:shadow_enabled", "Draw a drop shadow around the frame", true);
        shadowColor   = makeShared<Config::Values::CColorValue>("plugin:cde:shadow_color", "Shadow colour (RGB; alpha overridden by shadow_opacity1)", HEX(0xff000000));
        shadowColor2  = makeShared<Config::Values::CColorValue>("plugin:cde:shadow_color2", "Second dither tone (RGB; alpha overridden by shadow_opacity2)", HEX(0x48000000));
        shadowOpacity1 = makeShared<Config::Values::CFloatValue>("plugin:cde:shadow_opacity1", "Opacity of the strong dither tone", 0.5F);
        shadowOpacity2 = makeShared<Config::Values::CFloatValue>("plugin:cde:shadow_opacity2", "Opacity of the soft dither tone (0 disables it)", 0.75F);

        HyprlandAPI::addConfigValueV2(h, barHeight);
        HyprlandAPI::addConfigValueV2(h, face);
        HyprlandAPI::addConfigValueV2(h, light);
        HyprlandAPI::addConfigValueV2(h, dark);
        HyprlandAPI::addConfigValueV2(h, highlight);
        HyprlandAPI::addConfigValueV2(h, title);
        HyprlandAPI::addConfigValueV2(h, faceActive);
        HyprlandAPI::addConfigValueV2(h, lightActive);
        HyprlandAPI::addConfigValueV2(h, darkActive);
        HyprlandAPI::addConfigValueV2(h, titleActive);
        HyprlandAPI::addConfigValueV2(h, showButtons);
        HyprlandAPI::addConfigValueV2(h, titleFontSize);
        HyprlandAPI::addConfigValueV2(h, shadowEnabled);
        HyprlandAPI::addConfigValueV2(h, shadowColor);
        HyprlandAPI::addConfigValueV2(h, shadowColor2);
        HyprlandAPI::addConfigValueV2(h, shadowOpacity1);
        HyprlandAPI::addConfigValueV2(h, shadowOpacity2);
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
        if (faceActive)
            c.faceActive = CHyprColor(faceActive->value());
        if (lightActive)
            c.lightActive = CHyprColor(lightActive->value());
        if (darkActive)
            c.darkActive = CHyprColor(darkActive->value());
        if (titleActive)
            c.titleActive = CHyprColor(titleActive->value());
        if (showButtons)
            c.showButtons = showButtons->value();
        if (titleFontSize)
            c.titleFontSize = std::max(6, static_cast<int>(titleFontSize->value()));
        if (shadowEnabled)
            c.shadowEnabled = shadowEnabled->value();
        if (shadowColor)
            c.shadowColor = CHyprColor(shadowColor->value());
        if (shadowColor2)
            c.shadowColor2 = CHyprColor(shadowColor2->value());
        if (shadowOpacity1)
            c.shadowOpacity1 = std::clamp(shadowOpacity1->value(), 0.0F, 1.0F);
        if (shadowOpacity2)
            c.shadowOpacity2 = std::clamp(shadowOpacity2->value(), 0.0F, 1.0F);

        return c;
    }

    void bumpGeneration() {
        ++g_generation;
    }

    uint64_t generation() {
        return g_generation;
    }

} // namespace CDE