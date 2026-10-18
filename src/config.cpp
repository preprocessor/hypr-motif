#include "config.hpp"

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <algorithm>
#include <vector>

using namespace Hyprutils::Memory;

namespace {

    // registerConfig runs before a single decoration exists, so config() is never reached
    // with these unset — which is why it reads them without null-checking any of them.
    SP<Config::Values::CIntValue>   barHeight, titleFontSize;
    SP<Config::Values::CBoolValue>  showButtons;
    SP<Config::Values::CColorValue> face, light, dark, title, titleShadow;
    SP<Config::Values::CColorValue> faceActive, lightActive, darkActive, titleActive;

    // Ids the compositor handed back for our two rule effects. Zero is
    // WINDOW_RULE_EFFECT_NONE, which is never a key in a window's custom-prop map, so it
    // doubles as "no such rule" without a separate sentinel.
    Desktop::Rule::CWindowRuleEffectContainer::storageType g_noTitlebar = 0, g_noBorder = 0;

    uint64_t g_generation = 0;

} // namespace

namespace CDE {

    void registerConfig(HANDLE h) {
        // Colours are packed 0xAARRGGBB — what CColorValue::value() returns and what
        // CHyprColor(uint64_t) consumes.
        barHeight     = makeShared<Config::Values::CIntValue>("plugin:cde:bar_height", "Title bar height in logical pixels", CDE::BAR_HEIGHT);
        titleFontSize = makeShared<Config::Values::CIntValue>("plugin:cde:title_font_size", "Title text size in points", CDE::TITLE_FONT_SIZE);
        showButtons  = makeShared<Config::Values::CBoolValue>("plugin:cde:show_buttons", "Whether to draw the title bar buttons", true);
        face         = makeShared<Config::Values::CColorValue>("plugin:cde:color_face", "Title bar face colour", 0xff999999);
        light        = makeShared<Config::Values::CColorValue>("plugin:cde:color_light", "Bevel highlight colour", 0xffd1d1d1);
        dark         = makeShared<Config::Values::CColorValue>("plugin:cde:color_dark", "Bevel shadow colour", 0xff4e4e4e);
        title        = makeShared<Config::Values::CColorValue>("plugin:cde:color_title", "Title text colour (inactive window)", 0xffb0b0b0);
        titleShadow  = makeShared<Config::Values::CColorValue>("plugin:cde:color_title_shadow", "Title text 1px drop shadow", 0xff000000);

        // Active-window palette, measured from examples/new-standard.png.
        faceActive  = makeShared<Config::Values::CColorValue>("plugin:cde:color_face_active", "Title bar face colour (active window)", 0xffeda870);
        lightActive = makeShared<Config::Values::CColorValue>("plugin:cde:color_light_active", "Bevel highlight colour (active window)", 0xfff7d9c0);
        darkActive  = makeShared<Config::Values::CColorValue>("plugin:cde:color_dark_active", "Bevel shadow colour (active window)", 0xff7d593b);
        titleActive = makeShared<Config::Values::CColorValue>("plugin:cde:color_title_active", "Title text colour (active window)", 0xffffffff);

        // addConfigValueV2 takes SP<IValue>, so the derived types upcast implicitly. A
        // vector rather than a braced list because the list would have to deduce a common
        // type across CInt/CBool/CColor.
        const std::vector<SP<Config::Values::IValue>> all = {
            barHeight,
            titleFontSize,
            showButtons,
            face,
            light,
            dark,
            title,
            titleShadow,
            faceActive,
            lightActive,
            darkActive,
            titleActive,
        };

        // addConfigValueV2 takes SP<IValue>, so every derived type upcasts implicitly.
        for (const auto& value : all)
            HyprlandAPI::addConfigValueV2(h, value);
    }

    void registerRules() {
        g_noTitlebar = Desktop::Rule::windowEffects()->registerEffect("cde:no_titlebar");
        g_noBorder   = Desktop::Rule::windowEffects()->registerEffect("cde:no_border");
    }

    void unregisterRules() {
        Desktop::Rule::windowEffects()->unregisterEffect(g_noTitlebar);
        Desktop::Rule::windowEffects()->unregisterEffect(g_noBorder);
    }

    bool edgeHidden(PHLWINDOW window, uint32_t edge) {
        if (!window || !window->m_ruleApplicator)
            return false;

        // Presence in the custom-prop map IS the match: a valueless token in windowrulev2
        // reaches the effect as "1", so there is nothing left to parse.
        const auto& props = window->m_ruleApplicator->m_otherProps.props;

        return props.contains(edge == DECORATION_EDGE_TOP ? g_noTitlebar : g_noBorder);
    }

    SConfig config() {
        // Designated initialisers, in SConfig's own order, so the mapping from option to
        // field is one readable line each. The clamps are what stops a nonsensical value
        // producing a bar the geometry cannot be drawn into.
        return {
            .barHeight    = std::max(CDE::MIN_BAR_HEIGHT, static_cast<int>(barHeight->value())),
            .showButtons  = showButtons->value(),
            .titleFontSize = std::max(CDE::MIN_TITLE_FONT_SIZE, static_cast<int>(titleFontSize->value())),
            .face         = CHyprColor(face->value()),
            .light        = CHyprColor(light->value()),
            .dark         = CHyprColor(dark->value()),
            .title        = CHyprColor(title->value()),
            .titleShadow  = CHyprColor(titleShadow->value()),
            .faceActive   = CHyprColor(faceActive->value()),
            .lightActive  = CHyprColor(lightActive->value()),
            .darkActive   = CHyprColor(darkActive->value()),
            .titleActive  = CHyprColor(titleActive->value()),
        };
    }

    void bumpGeneration() {
        ++g_generation;
    }

    uint64_t generation() {
        return g_generation;
    }

} // namespace CDE
