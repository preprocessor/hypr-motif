#define WLR_USE_UNSTABLE

#include <algorithm>
#include <ranges>
#include <cstdio>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <stdexcept>

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>

#include "CCdeDecoration.hpp"
#include "config.hpp"

static HANDLE PHANDLE = nullptr;

// One decoration per edge. The top is the title bar; the other three are the frame.
struct SEdge {
    uint32_t    edge;
    const char* name;
};

static constexpr SEdge EDGES[] = {
    {DECORATION_EDGE_TOP, "CDE"},
    {DECORATION_EDGE_LEFT, "CDE:left"},
    {DECORATION_EDGE_RIGHT, "CDE:right"},
    {DECORATION_EDGE_BOTTOM, "CDE:bottom"},
};

static void onNewWindow(PHLWINDOW window) {
    if (!window || !validMapped(window))
        return;

    const auto has = [&](const char* name) {
        return std::ranges::any_of(window->m_windowDecorations, [&](const auto& d) { return d && d->getDisplayName() == name; });
    };

    for (const auto& e : EDGES) {
        // Keyed on display name so re-running this stays idempotent.
        if (has(e.name))
            continue;

        HyprlandAPI::addWindowDecoration(PHANDLE, window, Hyprutils::Memory::makeUnique<CCdeDecoration>(window, e.edge, e.name));
    }
}

// Runs `fn` against every one of our decorations on every mapped window. The event
// listeners below all need the same walk, and each decoration has to be narrowed back
// from IHyprWindowDecoration to ours before use.
template <typename Fn>
static void forEachCdeDecoration(Fn&& fn) {
    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window || !window->m_isMapped)
            continue;

        for (const auto& decoration : window->m_windowDecorations) {
            if (auto* cde = dynamic_cast<CCdeDecoration*>(decoration.get()))
                fn(*cde, window);
        }
    }
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
    //
    // bar_height feeds getPositioningInfo's desiredExtents, and the positioner only
    // re-runs when it is told to — bumping the generation alone just re-rasterises a bar
    // that is still the old height. onWindowUpdate is what makes the change take effect
    // at runtime; the decorations' textures are dropped too, since their geometry is
    // about to change under them.
    static auto PR = Event::bus()->m_events.config.reloaded.listen([] {
        CDE::bumpGeneration();

        forEachCdeDecoration([](CCdeDecoration& decoration, PHLWINDOW window) {
            decoration.invalidate();
            g_pDecorationPositioner->onWindowUpdate(window);
        });
    });

    // A title change must be handled HERE, not from draw(): nothing damages a decoration
    // when only its text changes, and Hyprland only calls draw() on a decoration that is
    // being repainted — so with no damage draw() never runs and never notices the new
    // title. Without this the title refreshes only when a resize or the zoom effect
    // happens to damage the same pixels. See AGENTS.md `## Title text style`.
    static auto PT = Event::bus()->m_events.window.title.listen([](PHLWINDOW window) {
        if (!window)
            return;

        for (const auto& decoration : window->m_windowDecorations) {
            if (auto* cde = dynamic_cast<CCdeDecoration*>(decoration.get())) {
                cde->invalidate();
                cde->damageBar();
            }
        }
    });

    // Hover and the drag start have to be driven from here too, for the same reason:
    // Hyprland sends decorations no INPUT_TYPE_MOTION at all, and a poll inside draw()
    // would never run for a window that is not being repainted — which is the state a
    // window is in while it waits to be dragged. syncHover() drops the texture when the
    // hovered button changes; damaging the bar is what makes the next frame repaint it.
    // See AGENTS.md `## Button input` and `## Dragging`.
    static auto PM = Event::bus()->m_events.input.mouse.move.listen([](const Vector2D& position, Event::SCallbackInfo&) {
        forEachCdeDecoration([&](CCdeDecoration& decoration, PHLWINDOW) {
            if (decoration.syncDrag(position) || decoration.syncHover(position))
                decoration.damageBar();
        });
    });

    static auto PB = Event::bus()->m_events.input.mouse.button.listen([](const IPointer::SButtonEvent& e, Event::SCallbackInfo&) {
        FILE* f = fopen("/home/wyspr/cde-dbg.log", "a");
        if (f) { fprintf(f, "BTN btn=%d state=%d mouse=%d mousepos=%.0f,%.0f held=%d\n", e.button, (int)e.state, (int)e.mouse, g_pInputManager->getMouseCoordsInternal().x, g_pInputManager->getMouseCoordsInternal().y, (int)g_pInputManager->hasHeldButtons()); fclose(f); }
    });

    // Windows that already exist are not covered by the open event.
    for (auto& w : Desktop::windowState()->windows())
        onNewWindow(w);

    return {"cde", "Common Desktop Environment window chrome for Hyprland", "wyspr", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {}