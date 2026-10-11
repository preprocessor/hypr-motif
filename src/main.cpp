#define WLR_USE_UNSTABLE

#include <algorithm>
#include <ranges>
#include <stdexcept>

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "CCdeDecoration.hpp"
#include "config.hpp"

static HANDLE      PHANDLE   = nullptr;
static const char* DECO_NAME = "CDE";

static void onNewWindow(PHLWINDOW window) {
    if (!window || !validMapped(window))
        return;

    const bool already = std::ranges::any_of(window->m_windowDecorations, [](const auto& d) { return d && d->getDisplayName() == DECO_NAME; });
    if (already)
        return;

    HyprlandAPI::addWindowDecoration(PHANDLE, window, Hyprutils::Memory::makeUnique<CCdeDecoration>(window));
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string(__hyprland_api_get_hash()) != std::string(__hyprland_api_get_client_hash()))
        throw std::runtime_error("[cde] ABI mismatch: rebuild against the running hyprland");

    CDE::registerConfig(handle);

    static auto P = Event::bus()->m_events.window.open.listen([&](PHLWINDOW w) { onNewWindow(w); });

    // IValue has no per-value change callback, so live reload has to go through
    // the config event and a generation counter the decorations compare against.
    static auto PR = Event::bus()->m_events.config.reloaded.listen([] { CDE::bumpGeneration(); });

    // Windows that already exist are not covered by the open event.
    for (auto& w : Desktop::windowState()->windows())
        onNewWindow(w);

    return {"cde", "Common Desktop Environment window chrome for Hyprland", "wyspr", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {}