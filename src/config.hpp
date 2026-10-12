#pragma once

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "chrome.hpp"

namespace CDE {

    struct SConfig {
        int        barHeight      = CDE::BAR_HEIGHT;
        // Inactive (unfocused) window.
        CHyprColor face           = {0.600, 0.600, 0.600, 1};
        CHyprColor light          = {0.820, 0.820, 0.820, 1};
        CHyprColor dark           = {0.306, 0.306, 0.306, 1};
        CHyprColor highlight      = {1.000, 1.000, 1.000, 1};
        CHyprColor title          = {1.000, 1.000, 1.000, 1};

        // Active (focused) window. Face/light/dark are measured from
        // examples/new-standard.png. The title defaults to black rather than the
        // reference's #ffffff: white on #eda870 is ~2.9:1, which is poor. Set
        // color_title_active to #ffffff for reference fidelity.
        CHyprColor faceActive     = {0.929, 0.659, 0.439, 1};
        CHyprColor lightActive    = {0.969, 0.851, 0.753, 1};
        CHyprColor darkActive     = {0.490, 0.349, 0.231, 1};
        CHyprColor titleActive    = {0.000, 0.000, 0.000, 1};

        bool       showButtons    = true;
        int        titleFontSize  = 11;

        // We draw our own shadow rather than using Hyprland's: CHyprDropShadowDecoration
        // derives its box from the window geometry and gets it wrong when a plugin
        // reserves space — the shadow came out ~46px below the frame instead of ~8.
        bool       shadowEnabled  = true;
        // A multiple of the 8px Bayer period: the bottom-right corner is exactly
        // shadow_range wide, so it tiles the dither lattice with no alias columns.
        int        shadowRange    = 16;
        CHyprColor shadowColor    = {0.000, 0.000, 0.000, 0.561}; // 0x8f black
        // Second dither tone: fills the band between `density > t/2` and `density > t`,
        // adding a tone step to the ramp. Alpha 0 disables it (single-tone behaviour).
        CHyprColor shadowColor2   = {0.000, 0.000, 0.000, 0.282}; // 0x48 black
        // Opacity knobs OVERRIDE the two tones' alphas: the colour configs above
        // contribute RGB only. opacity2 = 0 disables the second tone.
        double     shadowOpacity1 = 0.5;
        double     shadowOpacity2 = 0.75;
    };

    // Registers every plugin:cde: value. Must be called from pluginInit.
    void registerConfig(HANDLE h);

    // Reads current values; safe to call per raster, it never caches.
    SConfig config();

    // Bumped on config.reloaded. Decorations compare against it to force a re-raster.
    void     bumpGeneration();
    uint64_t generation();

} // namespace CDE