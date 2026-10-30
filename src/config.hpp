#pragma once

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "chrome.hpp"

namespace CDE {

    struct SConfig {
        int        barHeight      = CDE::BAR_HEIGHT;
        bool       showButtons    = true;
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

    // Registers the two plugin:cde: window rule effects. pluginInit and PLUGIN_EXIT only —
    // the compositor owns the name -> id map, and unregistering blanks the name rather than
    // erasing it, so a re-register takes a fresh slot every reload.
    void registerRules();
    void unregisterRules();

    // Whether a window rule has switched this edge's chrome off. A hidden strip reserves no
    // space, draws nothing and takes no input — the rule removes the chrome from the window
    // rather than just blanking its pixels. One rule hides the bar, one the frame; both
    // together hide everything.
    //
    // Read from getPositioningInfo, which the positioner only re-runs when told to — so a
    // rule takes effect at plugin load and at every `hyprctl reload`, the same as
    // bar_height. See AGENTS.md `## Window rules`.
    bool edgeHidden(PHLWINDOW window, uint32_t edge);

    // Whether `cde:no_title` hides the title text on this window. The bar, its fields and
    // the drag rules stay; the free space is used by grabber bars that run the full title
    // field. Painting is the only thing this changes — no geometry moves.
    bool titleHidden(PHLWINDOW window);

    // Reads current values; safe to call per raster, it never caches.
    SConfig config();

    // Bumped on config.reloaded. Decorations compare against it to force a re-raster.
    void     bumpGeneration();
    uint64_t generation();

} // namespace CDE