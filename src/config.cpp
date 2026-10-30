#include "config.hpp"

#include <hyprland/src/config/shared/parserUtils/ParserUtils.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

using namespace Hyprutils::Memory;

namespace {

    // registerConfig runs before a single decoration exists, so config() is never reached
    // with these unset — which is why it reads them without null-checking any of them.
    SP<Config::Values::CIntValue>   barHeight, titleFontSize;
    SP<Config::Values::CBoolValue>  showButtons, showTitlebar, showBorder, showTitle;
    SP<Config::Values::CStringValue> titleFont;
    SP<Config::Values::CColorValue> face, light, dark, title, titleShadow;
    SP<Config::Values::CColorValue> faceActive, lightActive, darkActive, titleActive;

    // One rule per config key, in the order the table in AGENTS.md lists them, so the two
    // cannot drift apart unnoticed. Zero is WINDOW_RULE_EFFECT_NONE, which is never a key in
    // a window's custom-prop map, so it doubles as "no such rule" without a sentinel.
    enum eRuleSlot : size_t {
        RULE_COLOR_FACE,
        RULE_COLOR_LIGHT,
        RULE_COLOR_DARK,
        RULE_COLOR_TITLE,
        RULE_COLOR_TITLE_SHADOW,
        RULE_COLOR_FACE_ACTIVE,
        RULE_COLOR_LIGHT_ACTIVE,
        RULE_COLOR_DARK_ACTIVE,
        RULE_COLOR_TITLE_ACTIVE,
        RULE_SHOW_TITLEBAR,
        RULE_SHOW_BORDER,
        RULE_SHOW_TITLE,
        RULE_COUNT
    };

    constexpr const char* RULE_NAMES[RULE_COUNT] = {
        "cde:color_face",         "cde:color_light",        "cde:color_dark",         "cde:color_title",
        "cde:color_title_shadow", "cde:color_face_active",  "cde:color_light_active", "cde:color_dark_active",
        "cde:color_title_active", "cde:show_titlebar",      "cde:show_border",        "cde:show_title",
    };

    Desktop::Rule::CWindowRuleEffectContainer::storageType g_rules[RULE_COUNT] = {};

    // The rule's value as the compositor stored it, or nothing when the window did not match
    // that rule. A valueless token arrives as "1", which is why a bool rule reads as true.
    std::optional<std::string> ruleEffect(PHLWINDOW window, size_t slot) {
        if (!window || !window->m_ruleApplicator || g_rules[slot] == 0)
            return std::nullopt;

        const auto& props = window->m_ruleApplicator->m_otherProps.props;
        const auto  prop  = props.find(g_rules[slot]);
        if (prop == props.end() || !prop->second)
            return std::nullopt;

        return prop->second->effect;
    }

    // The same parser the config file uses, so there is one colour syntax, not two. Bad
    // input is not an error: this runs inside a raster, so it falls back to the configured
    // colour rather than throwing or notifying.
    std::optional<CHyprColor> parseColour(const std::string& value) {
        if (value.empty())
            return std::nullopt;

        const auto parsed = Config::ParserUtils::parseColor(value);
        if (!parsed.has_value())
            return std::nullopt;

        return CHyprColor(static_cast<uint64_t>(*parsed));
    }

    bool parseBool(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return !(value == "0" || value == "false" || value == "no" || value == "off");
    }

    void applyColour(PHLWINDOW window, size_t slot, CHyprColor& field) {
        const auto effect = ruleEffect(window, slot);
        if (!effect)
            return;

        if (const auto colour = parseColour(*effect))
            field = *colour;
    }

    void applyBool(PHLWINDOW window, size_t slot, bool& flag) {
        const auto effect = ruleEffect(window, slot);
        if (!effect)
            return;

        flag = parseBool(*effect);
    }

    struct SChromeFlags {
        bool titlebar = true;
        bool border   = true;
        bool title    = true;
    };

    // The configured flags with the matching rules layered on top, one flag at a time.
    SChromeFlags chromeFlags(PHLWINDOW window) {
        SChromeFlags flags;
        flags.titlebar = showTitlebar->value();
        flags.border   = showBorder->value();
        flags.title    = showTitle->value();

        applyBool(window, RULE_SHOW_TITLEBAR, flags.titlebar);
        applyBool(window, RULE_SHOW_BORDER, flags.border);
        applyBool(window, RULE_SHOW_TITLE, flags.title);

        return flags;
    }

    uint64_t g_generation = 0;

    CDE::SConfig globalConfig() {
        // Designated initialisers, in SConfig's own order, so the mapping from option to
        // field is one readable line each. The clamps are what stops a nonsensical value
        // producing a bar the geometry cannot be drawn into.
        return {
            .barHeight    = std::max(CDE::MIN_BAR_HEIGHT, static_cast<int>(barHeight->value())),
            .showButtons  = showButtons->value(),
            .showTitlebar = showTitlebar->value(),
            .showBorder   = showBorder->value(),
            .showTitle    = showTitle->value(),
            .titleFontSize = std::max(CDE::MIN_TITLE_FONT_SIZE, static_cast<int>(titleFontSize->value())),
            .titleFont    = titleFont->value(),
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

} // namespace

namespace CDE {

    void registerConfig(HANDLE h) {
        // Colours are packed 0xAARRGGBB — what CColorValue::value() returns and what
        // CHyprColor(uint64_t) consumes.
        barHeight     = makeShared<Config::Values::CIntValue>("plugin:cde:bar_height", "Title bar height in logical pixels", CDE::BAR_HEIGHT);
        titleFontSize = makeShared<Config::Values::CIntValue>("plugin:cde:title_font_size", "Title text size in points", CDE::TITLE_FONT_SIZE);
        titleFont     = makeShared<Config::Values::CStringValue>("plugin:cde:title_font", "Title text font family (fontconfig name)", CDE::TITLE_FONT);
        showButtons  = makeShared<Config::Values::CBoolValue>("plugin:cde:show_buttons", "Whether to draw the title bar buttons", true);
        showTitlebar = makeShared<Config::Values::CBoolValue>("plugin:cde:show_titlebar", "Draw the title bar", true);
        showBorder   = makeShared<Config::Values::CBoolValue>("plugin:cde:show_border", "Draw the left, right and bottom frame", true);
        showTitle    = makeShared<Config::Values::CBoolValue>("plugin:cde:show_title", "Draw the title text", true);
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
            titleFont,
            showButtons,
            showTitlebar,
            showBorder,
            showTitle,
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
        for (size_t slot = 0; slot < RULE_COUNT; ++slot)
            g_rules[slot] = Desktop::Rule::windowEffects()->registerEffect(RULE_NAMES[slot]);
    }

    void unregisterRules() {
        for (const auto id : g_rules)
            Desktop::Rule::windowEffects()->unregisterEffect(id);
    }

    bool buttonsShown() {
        return showButtons->value();
    }

    bool edgeHidden(PHLWINDOW window, uint32_t edge) {
        const SChromeFlags flags = chromeFlags(window);
        return edge == DECORATION_EDGE_TOP ? !flags.titlebar : !flags.border;
    }

    bool titleHidden(PHLWINDOW window) {
        return !chromeFlags(window).title;
    }

    SConfig configFor(PHLWINDOW window) {
        SConfig cfg = globalConfig();

        // Rule wins over config, one key at a time: a rule that sets only `color_face`
        // leaves the focused palette alone, which is the documented 1:1 semantics.
        applyColour(window, RULE_COLOR_FACE, cfg.face);
        applyColour(window, RULE_COLOR_LIGHT, cfg.light);
        applyColour(window, RULE_COLOR_DARK, cfg.dark);
        applyColour(window, RULE_COLOR_TITLE, cfg.title);
        applyColour(window, RULE_COLOR_TITLE_SHADOW, cfg.titleShadow);
        applyColour(window, RULE_COLOR_FACE_ACTIVE, cfg.faceActive);
        applyColour(window, RULE_COLOR_LIGHT_ACTIVE, cfg.lightActive);
        applyColour(window, RULE_COLOR_DARK_ACTIVE, cfg.darkActive);
        applyColour(window, RULE_COLOR_TITLE_ACTIVE, cfg.titleActive);

        return cfg;
    }

    void bumpGeneration() {
        ++g_generation;
    }

    uint64_t generation() {
        return g_generation;
    }

} // namespace CDE
