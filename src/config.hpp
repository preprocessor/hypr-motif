#pragma once

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "chrome.hpp"

namespace CDE {

    struct SConfig {
        int        barHeight      = CDE::BAR_HEIGHT;
        bool       showButtons    = true;

        // Whether each part of the chrome is drawn. Frame, bar and title text; the bar and
        // frame reserve space when shown and reserve nothing when hidden.
        bool        showTitlebar  = true;
        bool        showBorder    = true;
        bool        showTitle     = true;

        int        titleFontSize  = CDE::TITLE_FONT_SIZE;

        // Font family for the title, resolved by fontconfig through cairo's toy API. The
        // default is the measured reference face; if it is not installed cairo silently
        // substitutes another one rather than failing.
        std::string titleFont     = CDE::TITLE_FONT;

        // Inactive (unfocused) window.
        CHyprColor face           = {0.600, 0.600, 0.600, 1};
        CHyprColor light          = {0.820, 0.820, 0.820, 1};
        CHyprColor dark           = {0.306, 0.306, 0.306, 1};
        // Unfocused titles are grey so the focused one, which is white, stands out —
        // focus is then readable from the bar alone at a glance.
        CHyprColor title          = {0.690, 0.690, 0.690, 1}; // #b0b0b0

        // The title's 1px drop shadow. Black, and deliberately NOT the bevel's dark
        // colour: that is brown (#7d593b) on the tan active palette, and a brown shadow
        // on a tan bar reads as a smudge rather than as depth.
        CHyprColor titleShadow    = {0.000, 0.000, 0.000, 1};

        // Active (focused) window. Face/light/dark are measured from
        // examples/new-standard.png.
        CHyprColor faceActive     = {0.929, 0.659, 0.439, 1};
        CHyprColor lightActive    = {0.969, 0.851, 0.753, 1};
        CHyprColor darkActive     = {0.490, 0.349, 0.231, 1};
        CHyprColor titleActive    = {1.000, 1.000, 1.000, 1};
    };

    // Registers every plugin:cde: value. Must be called from pluginInit.
    void registerConfig(HANDLE h);

    // Registers the twelve plugin:cde: window-rule effects, one per config key — the nine
    // colours and the three show_* flags. pluginInit and PLUGIN_EXIT only — the compositor
    // owns the name -> id map, and unregistering blanks the name rather than erasing it, so
    // a re-register takes a fresh slot every reload.
    void registerRules();
    void unregisterRules();

    // Whether chrome is switched off for this window, from `plugin:cde:show_titlebar` /
    // `show_border` and the same-named rules. A hidden strip reserves no space, draws
    // nothing and takes no input — the rule removes the chrome from the window rather than
    // just blanking its pixels. One flag hides the bar, one the frame; both together hide
    // everything.
    //
    // Read from getPositioningInfo, which the positioner only re-runs when told to — so a
    // change takes effect at plugin load, at every `hyprctl reload`, and from a live rule
    // change through main.cpp's window.updateRules listener. See AGENTS.md
    // `## Window rules`.
    bool edgeHidden(PHLWINDOW window, uint32_t edge);

    // Whether `show_title = false` hides the title text on this window. The bar, its fields
    // and the drag rules stay; the free space is used by grabber bars that run the full
    // title field. Painting is the only thing this changes — no geometry moves.
    bool titleHidden(PHLWINDOW window);

    // Whether the title bar buttons are drawn. `show_buttons` has no window rule, so this is
    // a global read: the hover path asks it on every mouse move and must not parse the whole
    // palette to answer a yes/no question.
    bool buttonsShown();

    // The palette for one window: current config values plus any window-rule overrides.
    // A null window yields the global config unchanged. Safe to call per raster, it never
    // caches.
    SConfig configFor(PHLWINDOW window);

    // Bumped on config.reloaded. Decorations compare against it to force a re-raster.
    void     bumpGeneration();
    uint64_t generation();

} // namespace CDE