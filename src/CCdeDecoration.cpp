#include "CCdeDecoration.hpp"

#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

using namespace Hyprutils::Math;

CCdeDecoration::CCdeDecoration(PHLWINDOW w, uint32_t edge, std::string name)
    : IHyprWindowDecoration(w)
    , m_window(w)
    , m_edge(edge)
    , m_name(std::move(name)) {}

CCdeDecoration::~CCdeDecoration() = default;

SDecorationPositioningInfo CCdeDecoration::getPositioningInfo() {
    const auto& cfg = CDE::config();

    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_STICKY;
    info.edges    = m_edge;
    info.priority = 10005; // outrank hyprland's own border decoration
    info.reserved = true;

    // SBoxExtents.topLeft is {left, top}; bottomRight is {right, bottom}.
    switch (m_edge) {
        case DECORATION_EDGE_TOP: info.desiredExtents = {{0, cfg.barHeight}, {0, 0}}; break;
        case DECORATION_EDGE_LEFT: info.desiredExtents = {{CDE::BORDER, 0}, {0, 0}}; break;
        case DECORATION_EDGE_RIGHT: info.desiredExtents = {{0, 0}, {CDE::BORDER, 0}}; break;
        case DECORATION_EDGE_BOTTOM: info.desiredExtents = {{0, 0}, {0, CDE::BORDER}}; break;
        default: info.desiredExtents = {{0, 0}, {0, 0}}; break;
    }

    return info;
}

void CCdeDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_assigned = reply.assignedGeometry;
    // Geometry can change without the title, scale or config changing — notably when a
    // sibling edge is repositioned slightly after we first raster. ensureTextures also
    // compares the box size, but dropping it here avoids a frame of wrong pixels.
    m_barTex.reset();
}

void CCdeDecoration::updateWindow(PHLWINDOW) {
    // Title, class or size may all have changed; the next draw() re-checks cheaply.
    m_barTex.reset();
}

CBox CCdeDecoration::outerBox() const {
    auto siblingBox = [&](const char* name) {
        for (const auto& d : m_window->m_windowDecorations)
            if (d && d->getDisplayName() == name)
                return g_pDecorationPositioner->getWindowDecorationBox(d.get());
        return CBox{};
    };

    const CBox   own      = g_pDecorationPositioner->getWindowDecorationBox(const_cast<CCdeDecoration*>(this));
    const CBox   topSib   = siblingBox("CDE");
    const CBox   botSib   = siblingBox("CDE:bottom");
    const CBox   leftSib  = siblingBox("CDE:left");
    const CBox   rightSib = siblingBox("CDE:right");

    // Take the outer span from the side decorations rather than deriving it from `own`.
    // `own` is NOT the same rectangle for every edge: for the top it is the client box,
    // but for the left it already starts at the window's outer edge. Re-expanding a
    // uniform `own.x - BORDER` therefore shifts the side frames a whole border further
    // out than the client, leaving a gap of bare wallpaper between frame and client.
    // The reserved boxes are positioned by the compositor, so read them directly.
    const double winX      = leftSib.w  > 0 ? leftSib.x : own.x;
    const double winRight  = rightSib.w > 0 ? rightSib.x + rightSib.w : own.x + own.w;
    const double winY      = topSib.h  > 0 ? topSib.y : own.y;
    const double winBottom = botSib.h  > 0 ? botSib.y + botSib.h : own.y + own.h;
    const double winW      = winRight - winX;
    const double winH      = winBottom - winY;

    switch (m_edge) {
        // Motif's title bar spans the FULL window width, frame bands included — that is
        // the whole point of it. Using the client width leaves wallpaper gaps in the
        // top corners.
        case DECORATION_EDGE_TOP: return {winX, winY, winW, topSib.h > 0 ? topSib.h : own.h};
        // The side frames run the FULL height: alongside the bar as well as below it.
        case DECORATION_EDGE_LEFT: return {winX, winY, CDE::BORDER, winH};
        case DECORATION_EDGE_RIGHT: return {winX + winW - CDE::BORDER, winY, CDE::BORDER, winH};
        // Bottom spans the full width, overlapping both corners.
        case DECORATION_EDGE_BOTTOM: return {winX, winBottom - CDE::BORDER, winW, CDE::BORDER};
        default: return {};
    }
}

void CCdeDecoration::damageEntire() {
    m_barTex.reset();
}

eDecorationType CCdeDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

eDecorationLayer CCdeDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CCdeDecoration::getDecorationFlags() {
    return DECORATION_ALLOWS_MOUSE_INPUT | DECORATION_PART_OF_MAIN_WINDOW;
}

std::string CCdeDecoration::getDisplayName() {
    return m_name;
}

CBox CCdeDecoration::barBoxGlobal() const {
    CBox box = outerBox();
    if (m_window->m_workspace && !m_window->m_pinned)
        box.translate(m_window->m_workspace->m_renderOffset->value());
    return box;
}

void CCdeDecoration::runButtonAction(int btn) {
    // Actions take the window explicitly: the click may land on the bar of a window that
    // is not the focused one, and a global dispatcher would act on the wrong window.
    //
    // Deliberately NOT going through `HyprlandAPI::invokeHyprctlCommand("dispatch", ...)`:
    // under a Lua config `hyprctl dispatch <name> <args>` is eval'd as
    // `hl.dispatch(name args)`, which is a syntax error for every classic dispatcher name
    // (measured: `hyprctl dispatch exec true` → "')' expected near 'true'"). The Actions
    // API is called in-process and needs no config-type branch.
    const PHLWINDOW self = m_window.lock();

    switch (btn) {
        case CDE::BTN_CLOSE: Config::Actions::closeWindow(self); break;
        case CDE::BTN_MAX: Config::Actions::fullscreenWindow(Fullscreen::FSMODE_FULLSCREEN, true, self); break;
        // The float glyph wants floatWindow(TOGGLE_ACTION_TOGGLE). Note there is no
        // `minimize` dispatcher or action in 0.56.2 at all — the old string dispatch for
        // it was silently doing nothing.
        default: break;
    }
}

void CCdeDecoration::retexture(float scale) {
    // Raster at exactly the box draw() will use, so the texture and the geometry cannot
    // drift apart.
    const CBox  box = outerBox();
    const bool act = isActive();

    if (isTop())
        m_barTex = CDE::renderBar(box.size(), scale, m_hovered, m_pressed, m_window->m_title, CDE::config().titleFontSize, act);
    else
        m_barTex = CDE::renderBorder(m_edge, box.size(), scale, act);

    m_lastTitle   = m_window->m_title;
    m_lastScale   = scale;
    m_lastSize    = box.size();
    m_lastFocused = act ? 1 : 0;
    m_generation  = CDE::generation();
}

void CCdeDecoration::ensureTextures(float scale) {
    // Size is part of the cache key. The four edges are positioned independently and a
    // sibling can be repositioned after we raster, which would otherwise leave a
    // stale texture stretched over the new box — e.g. a frame band one pixel thin.
    if (m_barTex && m_lastScale == scale && m_lastTitle == m_window->m_title && m_generation == CDE::generation() && m_lastSize == outerBox().size() && m_lastFocused == (isActive() ? 1 : 0))
        return;

    retexture(scale);
}

bool CCdeDecoration::onInputOnDeco(const eInputType type, const Vector2D& pos, std::any data) {
    // Only the top edge has buttons; the frame edges are decorative for now.
    if (!isTop())
        return true;

    const Vector2D size    = outerBox().size(); // match the widened bar the glyphs were drawn into
    const bool     buttons = CDE::config().showButtons;

    // Hyprland passes the GLOBAL cursor position: CWindow::checkInputOnDecos
    // (desktop/view/Window.cpp) forwards the very same coords it just tested with
    // containsPoint(), and the built-in group bar subtracts its assignedBoxGlobal().pos()
    // for exactly this reason. Every rect chrome.cpp hands back is bar-relative, so
    // convert once here — hit-testing raw global coords against local rects silently
    // never hits anything, which is why no button responded before.
    const Vector2D p = pos - barBoxGlobal().pos();

    auto buttonAt = [&](const Vector2D& q) -> int {
        for (int b = 0; b < CDE::BTN_COUNT; ++b) {
            const CBox r = CDE::buttonRect(size, b, buttons);
            if (r.w && q.x >= r.x && q.x <= r.x + r.w && q.y >= r.y && q.y <= r.y + r.h)
                return b;
        }
        return CDE::BTN_NONE;
    };

    // Keep the pressed highlight in step with the click being tracked. Runs on both
    // edges, so the button un-presses even when the release lands somewhere else.
    auto syncPressed = [&] {
        if (m_clickButton != m_pressed) {
            m_pressed = m_clickButton;
            m_barTex.reset();
        }
    };

    switch (type) {
        // INPUT_TYPE_BUTTON arrives on press AND release (InputManager.cpp,
        // processMouseDownNormal), with the event in `data`. The action fires on the
        // release — deliberately NOT on the drag pair: a DRAG_START we swallow stops
        // Hyprland creating a drag target, and CDragStateController::dragEnd()
        // early-returns without one, so DRAG_END would never reach us.
        case INPUT_TYPE_BUTTON: {
            const auto* ev = std::any_cast<IPointer::SButtonEvent>(&data);
            if (!ev || !ev->mouse || ev->button != BTN_LEFT)
                return false;

            if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED)
                m_clickButton = buttonAt(p);
            else {
                const int held = m_clickButton;
                const int hit  = buttonAt(p);
                if (held != CDE::BTN_NONE && held == hit)
                    runButtonAction(held);
            }

            syncPressed();
            return false; // swallow both edges, so the click never reaches the client
        }

        case INPUT_TYPE_DRAG_START: {
            m_dragStart = p;
            // False on a button: a press landing on a button must never start a window
            // drag. True on the bare bar, so a user-bound `mouse:... movewindow` can
            // still drag the window by its title bar.
            return buttonAt(p) == CDE::BTN_NONE;
        }

        // Reached only when a drag really started, i.e. the press missed every button.
        // The compositor has already moved the window by now; just drop the drag state.
        case INPUT_TYPE_DRAG_END:
            m_clickButton = CDE::BTN_NONE;
            syncPressed();
            return true;

        default: return true;
    }
}

void CCdeDecoration::draw(PHLMONITOR monitor, float const& alpha) {
    if (!m_assigned.w || !m_assigned.h || !m_window || !m_window->m_isMapped)
        return;

    const float scale = monitor ? monitor->scale() : 1.f;

    ensureTextures(scale);

    if (!m_barTex)
        return;

// Positioned from the positioner's answer for this decoration, re-expanded to the
    // window's outer span — the same box onInputOnDeco converts global input into, so a
    // click lands where it looks like it lands. Do NOT use getBoxWithIncludedDecos()
    // here: it already accounts for the space we ourselves reserved, so slicing it per
    // edge is a feedback loop that drifts by the border width.
    CBox global = barBoxGlobal();
    if (!global.w || !global.h)
        return;

    // Decorations cannot draw to the framebuffer directly: draw() runs outside the GL
    // pass. Queue a texture element and let the compositor's element renderer issue the
    // draw with a current context.
    CTexPassElement::SRenderData data;
    data.tex        = m_barTex;
    data.box        = global;
    data.a          = alpha;
    data.damage     = CRegion(global);
    data.clipBox    = global;
    data.clipRegion = CRegion(global);

    g_pHyprRenderer->currentPass().add(Hyprutils::Memory::makeUnique<CTexPassElement>(std::move(data)));
}