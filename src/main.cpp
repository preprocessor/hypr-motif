#define WLR_USE_UNSTABLE

#include <stdexcept>
#include <utility>
#include <vector>

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprutils/signal/Listener.hpp>

#include "CCdeDecoration.hpp"
#include "config.hpp"

static HANDLE PHANDLE = nullptr;

// One decoration per edge. The top is the title bar; the other three are the frame.
struct SEdge {
    uint32_t    edge;
    const char* name;
};

static constexpr SEdge EDGES[] = {
    {DECORATION_EDGE_TOP, CDE::DECO_BAR},
    {DECORATION_EDGE_LEFT, CDE::DECO_LEFT},
    {DECORATION_EDGE_RIGHT, CDE::DECO_RIGHT},
    {DECORATION_EDGE_BOTTOM, CDE::DECO_BOTTOM},
};

static void onNewWindow(PHLWINDOW window) {
    if (!window || !validMapped(window))
        return;

    for (const auto& e : EDGES) {
        if (CCdeDecoration::windowHas(window, e.name))
            continue;

        HyprlandAPI::addWindowDecoration(PHANDLE, window, Hyprutils::Memory::makeUnique<CCdeDecoration>(window, e.edge, e.name));
    }
}

// Every listener we register, so PLUGIN_EXIT can let them all go.
//
// THIS IS NOT OPTIONAL. The bus holds only a weak reference, and a listener's handler is
// a std::function holding code from this shared object. `listen()` hands back the last
// strong reference, so parking it in a function-local `static` (the idiom hyprbars uses)
// leaves the signal permanently registered: on unload the count is never decremented,
// because the reference that would decrement it lives in a .bss about to be unmapped. The
// next emit calls straight into freed code. Clearing this vector while we are still
// mapped is the only moment the handlers can safely be destroyed.
static std::vector<Hyprutils::Signal::CHyprSignalListener> g_listeners;

// Not named `listen`: that is POSIX ::listen, and this would be an overload of it the
// moment <sys/socket.h> arrives transitively.
template <typename Signal, typename Fn>
static void keepListener(Signal& signal, Fn&& handler) {
    g_listeners.emplace_back(signal.listen(std::forward<Fn>(handler)));
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    if (std::string(__hyprland_api_get_hash()) != std::string(__hyprland_api_get_client_hash())) {
        // A bare throw here is invisible — the plugin just does not appear.
        HyprlandAPI::addNotification(handle, "[cde] ABI mismatch: this plugin was built against a different hyprland than the one running. Rebuild it.",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 10000);
        throw std::runtime_error("[cde] ABI mismatch: rebuild against the running hyprland");
    }

    CDE::registerConfig(handle);

    keepListener(Event::bus()->m_events.window.open, [](PHLWINDOW w) { onNewWindow(w); });

    // IValue has no per-value change callback, so live reload goes through the config
    // event and a generation counter the decorations compare against.
    //
    // Bumping the generation alone is not enough for bar_height: it feeds desiredExtents,
    // and the positioner only re-runs when told to, so a bar of the old height would just
    // be re-rasterised. onWindowUpdate is what makes the change take effect, and the
    // textures go too since their geometry is about to change under them.
    keepListener(Event::bus()->m_events.config.reloaded, [] {
        CDE::bumpGeneration();

        CCdeDecoration::forEach([](CCdeDecoration& decoration, PHLWINDOW window) {
            decoration.invalidate();
            g_pDecorationPositioner->onWindowUpdate(window);
        });
    });

    // A title change must be handled HERE, not from draw(): nothing damages a decoration
    // when only its text changes, and Hyprland only calls draw() on a decoration being
    // repainted — so with no damage draw() never runs and never notices the new title.
    // Without this the title refreshes only when a resize or the zoom effect happens to
    // damage the same pixels. See AGENTS.md `## Title text style`.
    keepListener(Event::bus()->m_events.window.title, [](PHLWINDOW window) {
        CCdeDecoration::forEach([&](CCdeDecoration& decoration, PHLWINDOW owner) {
            if (owner != window)
                return;

            decoration.invalidate();
            decoration.damageBar();
        });
    });

    // Hover and the drag start have to be driven from here too, for the same reason:
    // Hyprland sends decorations no INPUT_TYPE_MOTION at all, and a poll inside draw()
    // would never run for a window that is not being repainted — which is the state a
    // window is in while it waits to be dragged. syncHover() drops the texture when the
    // hovered button changes; damaging the bar is what makes the next frame repaint it.
    // See AGENTS.md `## Button input` and `## Dragging`.
    keepListener(Event::bus()->m_events.input.mouse.move, [](const Vector2D& position, Event::SCallbackInfo&) {
        CCdeDecoration::forEach([&](CCdeDecoration& decoration, PHLWINDOW) {
            if (decoration.syncDrag(position) || decoration.syncHover(position))
                decoration.damageBar();
        });
    });

    // The release of a drag or a corner resize, from the one listener that sees EVERY button
    // event. A release that lands off the window never reaches onInputOnDeco, and nothing
    // else retires a gesture the plugin started — the compositor ends its own drags from the
    // mouse bind that owns them, and there is no bind here. Without this the window keeps
    // following the cursor after the button is long gone.
    keepListener(Event::bus()->m_events.input.mouse.button, [](const IPointer::SButtonEvent& e, Event::SCallbackInfo&) {
        // Only the button a gesture is armed with: a right-click release while the left one
        // is still down must not end the drag.
        if (e.button == BTN_LEFT && e.state == WL_POINTER_BUTTON_STATE_RELEASED)
            CCdeDecoration::forEach([](CCdeDecoration& decoration, PHLWINDOW) { decoration.endGesture(); });
    });

    // Windows that already exist are not covered by the open event.
    for (auto& w : Desktop::windowState()->windows())
        onNewWindow(w);

    // Without this the config file is not read until the NEXT reload, so a fresh
    // `plugin load` comes up on the compiled defaults and the file looks ignored.
    HyprlandAPI::reloadConfig();

    return {"cde", "Common Desktop Environment window chrome for Hyprland", "wyspr", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Drop the listeners while our code is still mapped — see g_listeners.
    g_listeners.clear();

    // Then hand the decorations back. Order matters: with a listener still live we could
    // be called into a half-torn-down plugin, and while any decoration survives Hyprland
    // keeps this shared object mapped, so the next `plugin load` would find them in place
    // and skip its own — leaving the old code on screen. See CCdeDecoration::removeAll.
    CCdeDecoration::removeAll(PHANDLE);

    for (const auto& monitor : State::monitorState()->monitors())
        monitor->m_scheduledRecalc = true;
}
