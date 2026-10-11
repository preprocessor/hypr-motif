#pragma once

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace CDE {

    struct SConfig {
        int        barHeight      = 25;
        CHyprColor face           = {0.600, 0.600, 0.600, 1};
        CHyprColor light          = {0.820, 0.820, 0.820, 1};
        CHyprColor dark           = {0.306, 0.306, 0.306, 1};
        CHyprColor highlight      = {1.000, 1.000, 1.000, 1};
        CHyprColor title          = {1.000, 1.000, 1.000, 1};
        bool       showButtons    = true;
        int        titleFontSize  = 11;
    };

    // Registers every plugin:cde: value. Must be called from pluginInit.
    void registerConfig(HANDLE h);

    // Reads current values; safe to call per raster, it never caches.
    SConfig config();

    // Bumped on config.reloaded. Decorations compare against it to force a re-raster.
    void     bumpGeneration();
    uint64_t generation();

} // namespace CDE