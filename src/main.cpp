#define WLR_USE_UNSTABLE

#include <format>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

// Same hazard, one instance: the registered hyprctl command holds a std::function pointing
// into this shared object, and Hyprland keeps calling it for as long as it is registered.
static SP<SHyprCtlCommand> g_hyprCtlCommand;

// The window's outer rect: the client box grown by the chrome actually reserved on it.
//
// NOT getWindowBoxUnified(RESERVED_EXTENTS), which looks like the obvious call and is not.
// It agrees with this on every window that fits the work area — measured, six of six — but it
// CLAMPS to that area, so a window overhanging it is under-reported. One live example: a
// Steam window whose real box is [435,-26] 2570x1471 came back as [440,0] 2560x1440, while
// the bottom frame was plainly on screen. Pixels are what settled it.
//
// NOT getFullWindowBoundingBox() either, which reads ~47px taller — Hyprland's shadow extent
// leaking into a session where a plugin reserved space.
//
// The extents are the ones getPositioningInfo reserves, so this cannot drift from the screen:
// a strip a rule hid reserves nothing and contributes nothing here either.
static CBox outerBoxOf(PHLWINDOW window) {
    const CBox   client   = window->getWindowMainSurfaceBox();
    const bool   noBar    = CDE::edgeHidden(window, DECORATION_EDGE_TOP);
    const bool   noFrame  = CDE::edgeHidden(window, DECORATION_EDGE_LEFT);
    // The top band is the bar, or the frame's own BORDER once `show_titlebar = false` hides the
    // bar. Both rules hide the band entirely.
    const double top      = noBar ? (noFrame ? 0 : CDE::BORDER) : CDE::configFor(window).barHeight;
    const double side     = noFrame ? 0 : CDE::BORDER;

    return {client.x - side, client.y - top, client.w + side * 2, client.h + top + side};
}

// The two JSON shapes the patch below matches on. `at` and `size` are the ONLY
// two-integer arrays in the payload — measured over the whole document — and there is
// exactly one of each per object. A window title cannot fake a match: quotes inside a JSON
// string come back escaped, so a literal `"at": [` cannot appear in one.
// The custom delimiter is not decoration: R"(...)" would end early, because the capture
// groups put a `)` immediately before a `"` — as in `[a-fA-F]+)"`.
static const std::regex ADDRESS_FIELD(R"RE("address"\s*:\s*"(0x[0-9a-fA-F]+)")RE");
static const std::regex RECT_FIELD(R"RE("([a-z]+)"\s*:\s*\[\s*-?\d+\s*,\s*-?\d+\s*\])RE");

// Rewrites the at and size inside one object's span. Both matches are collected before
// anything is written, because replacing one shifts the other's offset — so the writes go
// in back to front.
static void patchRect(std::string& json, size_t from, size_t to, const CBox& box) {
    struct SPatch {
        size_t      at;
        size_t      len;
        std::string text;
    };

    std::vector<SPatch> patches;

    for (std::smatch field; std::regex_search(json.cbegin() + from, json.cbegin() + to, field, RECT_FIELD);) {
        const size_t at = from + static_cast<size_t>(field.position());
        from            = at + field.length();

        const std::string key  = field[1].str();
        const bool        isAt = key == "at";

        if (key != "at" && key != "size")
            continue;

        patches.push_back({at, static_cast<size_t>(field.length()),
                           std::format(R"("{}": [{}, {}])", key, isAt ? static_cast<int>(box.x) : static_cast<int>(box.w),
                                      isAt ? static_cast<int>(box.y) : static_cast<int>(box.h))});
    }

    for (auto patch = patches.rbegin(); patch != patches.rend(); ++patch)
        json.replace(patch->at, patch->len, patch->text);
}

// `hyprctl cde clients` — `hyprctl clients -j` with at and size rewritten to the outer
// rect, chrome included. The payload is the compositor's own rather than one built here, so
// every field it has is already in it and a field added upstream arrives for free.
//
// Why it cannot just be hyprctl clients: the positioner subtracts reserved space before the
// IPC ever sees the window, for Hyprland's own decorations as much as for ours (measured —
// border_size = 6 moved hyprctl clients from [5,26] to [11,32] and shrank it by 12).
// IHyprWindowDecoration has no reported-extents hook, so a plugin cannot widen what
// `hyprctl clients` prints; this command is the only way to publish the outer rect.
//
// The format argument is ignored and the output is always JSON — patching the text form
// would be string surgery with no field names to anchor on.
static std::string cdeClients(eHyprCtlOutputFormat, std::string) {
    std::string json = HyprlandAPI::invokeHyprctlCommand("clients", "", "json");

    // Loud, not silent: if the format argument is not what this Hyprland wants, returning
    // the untouched client rects would look like a working answer that is quietly wrong.
    if (json.find(R"("address")") == std::string::npos)
        return R"(hyprctl cde clients: the compositor returned no JSON for `clients`.)";

    std::unordered_map<std::string, CBox> rects;
    for (auto& window : Desktop::windowState()->windows()) {
        if (!window || !validMapped(window))
            continue;

        rects[std::format("0x{:x}", reinterpret_cast<uint64_t>(window.get()))] = outerBoxOf(window);
    }

    // Object by object: each begins at an "address" and runs up to the next one, so a
    // number is only ever rewritten inside its own object.
    size_t cursor = 0;

    for (std::smatch address; std::regex_search(json.cbegin() + cursor, json.cend(), address, ADDRESS_FIELD);) {
        const size_t objectStart = cursor + static_cast<size_t>(address.position());
        cursor                   = objectStart + address.length();

        size_t objectEnd = json.size();
        if (std::smatch next; std::regex_search(json.cbegin() + cursor, json.cend(), next, ADDRESS_FIELD))
            objectEnd = cursor + static_cast<size_t>(next.position());

        const auto rect = rects.find(address[1].str());
        if (rect != rects.end())
            patchRect(json, objectStart, objectEnd, rect->second);
    }

    return json;
}

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
    CDE::registerRules();

    // Prefix-matched, so `hyprctl cde clients` arrives here with its subcommand as the
    // argument. `exact` = true would demand the whole request equal the name.
    g_hyprCtlCommand = HyprlandAPI::registerHyprCtlCommand(PHANDLE, {.name = "cde", .exact = false, .fn = cdeClients});

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
            // A reload that changes only pixels (a theme colour, `show_title`) produces
            // no geometry change, so the positioner's fast path does nothing and nothing
            // would repaint the dropped texture. Damage is what makes the new raster land.
            decoration.damageBar();
        });
    });

    // A rule change alters this window's palette and possibly which parts of the chrome
    // exist. Geometry is Task 3; here it is pixels only.
    keepListener(Event::bus()->m_events.window.updateRules, [](PHLWINDOW window) {
        CCdeDecoration::forEach([&](CCdeDecoration& decoration, PHLWINDOW owner) {
            if (owner != window)
                return;

            decoration.invalidate();
            decoration.damageBar();
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
    // Before anything else, for the same reason as g_listeners below: the handler is a
    // std::function into this shared object and Hyprland keeps calling it while registered.
    if (g_hyprCtlCommand) {
        HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_hyprCtlCommand);
        g_hyprCtlCommand.reset();
    }

    // Hand back the rule effect names while our code is still mapped — see g_listeners for
    // why the ordering here matters.
    CDE::unregisterRules();

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
