#include "CCdeDecoration.hpp"

#include <algorithm>

#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

using namespace Hyprutils::Math;

namespace {
    std::vector<CCdeDecoration*>& registry() {
        static std::vector<CCdeDecoration*> reg;
        return reg;
    }

    // The box the compositor gave this decoration. const_cast because the positioner takes
    // a non-const pointer even though it only reads.
    CBox ownBox(const CCdeDecoration* decoration) {
        return g_pDecorationPositioner->getWindowDecorationBox(const_cast<CCdeDecoration*>(decoration));
    }

}

CCdeDecoration::CCdeDecoration(PHLWINDOW w, uint32_t edge, std::string name)
    : IHyprWindowDecoration(w)
    , m_window(w)
    , m_edge(edge)
    , m_name(std::move(name)) {
    registry().push_back(this);
}

CCdeDecoration::~CCdeDecoration() {
    auto& reg = registry();
    reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());
}

void CCdeDecoration::forEach(const std::function<void(CCdeDecoration&, PHLWINDOW)>& fn) {
    auto& reg = registry();
    for (size_t i = 0; i < reg.size(); ++i)
        fn(*reg[i], reg[i]->m_window.lock());
}

bool CCdeDecoration::windowHas(PHLWINDOW window, const std::string& name) {
    // The compositor's list, not our registry. The registry only knows this copy's
    // decorations, and catching the ones left by a DIFFERENT still-mapped copy is the
    // whole point — a registry-local check would wave those through and let two copies
    // draw on top of each other. Runs once per window, so the scan costs nothing.
    for (const auto& d : window->m_windowDecorations)
        if (d && d->getDisplayName() == name)
            return true;
    return false;
}

void CCdeDecoration::removeAll(HANDLE handle) {
    // Backwards: removeWindowDecoration destroys the object, and the destructor erases
    // from the registry being iterated.
    auto& reg = registry();
    for (size_t i = reg.size(); i > 0; --i)
        HyprlandAPI::removeWindowDecoration(handle, reg[i - 1]);
    reg.clear();
}

bool CCdeDecoration::suppressed() const {
    const PHLWINDOW self = m_window.lock();

    // The top edge has two things to draw — the bar, and the top frame band behind it when
    // `cde:no_titlebar` hides the bar — so it only disappears when BOTH rules hide them.
    // Every other edge is the frame alone, which `cde:no_border` switches off.
    if (isTop())
        return CDE::edgeHidden(self, DECORATION_EDGE_TOP) && CDE::edgeHidden(self, DECORATION_EDGE_LEFT);

    return CDE::edgeHidden(self, m_edge);
}

SDecorationPositioningInfo CCdeDecoration::getPositioningInfo() {
    const auto& cfg = CDE::config();

    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_STICKY;
    info.edges    = m_edge;
    info.priority = CDE::DECO_PRIORITY;
    info.reserved = true;

    // A rule switched this strip off: reserve nothing, so the client grows into the space
    // and the window is its old self minus the chrome. windowBox() already copes with a
    // zero-extent sibling — that is the same case as one not placed yet, and the client
    // edges it falls back to ARE the window's outer edges with nothing reserved there.
    if (suppressed()) {
        info.reserved = false;
        return info;
    }

    // SBoxExtents.topLeft is {left, top}; bottomRight is {right, bottom}.
    switch (m_edge) {
        case DECORATION_EDGE_TOP:
            // The bar, or just the frame band when `cde:no_titlebar` hides the bar. The band
            // is BORDER thick so the frame reads the same on all four sides.
            info.desiredExtents = {{0, CDE::edgeHidden(m_window.lock(), DECORATION_EDGE_TOP) ? CDE::BORDER : cfg.barHeight}, {0, 0}};
            break;
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

// The window's outer rect, assembled from the boxes the compositor gave the four sibling
// strips. This is what the corner grab squares are measured from.
CBox CCdeDecoration::windowBox() const {
    const CBox own = ownBox(this);
    CBox        top, left, right, bottom;

    for (const auto& d : m_window->m_windowDecorations) {
        if (!d)
            continue;

        const std::string& name = d->getDisplayName();
        if (name != CDE::DECO_BAR && name != CDE::DECO_LEFT && name != CDE::DECO_RIGHT && name != CDE::DECO_BOTTOM)
            continue;

        const CBox box = g_pDecorationPositioner->getWindowDecorationBox(d.get());

        if (name == CDE::DECO_BAR)
            top = box;
        else if (name == CDE::DECO_LEFT)
            left = box;
        else if (name == CDE::DECO_RIGHT)
            right = box;
        else
            bottom = box;
    }

    // The outer span comes from the SIBLING decorations, never from our own box. `own` is
    // a different rectangle per edge — for the top it is the client box, for the left it
    // already starts at the window's outer edge — so re-expanding it uniformly pushes the
    // side frames a border further out than the client and leaves bare wallpaper between
    // them. The compositor positions those boxes, so we read them directly, and fall back
    // to our own only while a sibling has not been placed yet.
    const double winX      = left.w > 0 ? left.x : own.x;
    const double winRight  = right.w > 0 ? right.x + right.w : own.x + own.w;
    const double winY      = top.h > 0 ? top.y : own.y;
    const double winBottom = bottom.h > 0 ? bottom.y + bottom.h : own.y + own.h;

    return {winX, winY, winRight - winX, winBottom - winY};
}

CBox CCdeDecoration::outerBox() const {
    const CBox win = windowBox();

    switch (m_edge) {
        // Motif's title bar spans the FULL window width, frame bands included.
        case DECORATION_EDGE_TOP:
            // The bar's height is the positioner's answer for THIS decoration: on the top
            // edge our own box *is* the sibling the other three read.
            return {win.x, win.y, win.w, ownBox(this).h};
        // The side frames run the FULL height: alongside the bar as well as below it.
        case DECORATION_EDGE_LEFT: return {win.x, win.y, CDE::BORDER, win.h};
        case DECORATION_EDGE_RIGHT: return {win.x + win.w - CDE::BORDER, win.y, CDE::BORDER, win.h};
        // Bottom spans the full width, overlapping both corners.
        case DECORATION_EDGE_BOTTOM: return {win.x, win.y + win.h - CDE::BORDER, win.w, CDE::BORDER};
        default: return {};
    }
}

// Which bottom corner grab square a GLOBAL point is in, or CORNER_NONE. One square spans
// the bottom strip and the foot of both side strips, so which decoration happens to see
// the press is an accident of iteration order — all four test the same two rects.
//
// Only the bottom pair is a live handle: the top pair's rules are drawn because the
// reference has them, but a grab square there would sit on the close and maximize fields.
Layout::eRectCorner CCdeDecoration::cornerAt(const Vector2D& globalPos) const {
    const PHLWINDOW self = m_window.lock();
    if (!self)
        return Layout::CORNER_NONE;

    // The grab square belongs to the bottom strip, which also draws its marker. With
    // `cde:no_border` that strip is hidden, so there is nothing to grab — and without this
    // the windowBox() fallback for the top decoration (whose `own` box is the BAR, not the
    // window) would put both rects inside the bar, where they steal button clicks.
    if (CDE::edgeHidden(self, DECORATION_EDGE_BOTTOM))
        return Layout::CORNER_NONE;

    const CBox   win  = windowBox();
    const double grab = CDE::cornerGrab(CDE::config().barHeight);

    if (!win.w || !win.h)
        return Layout::CORNER_NONE;

    // The handle is the window's outer EDGE: the full thickness of the bottom border, grab
    // wide, flush with the outermost pixel on both axes. The drawn grab square above it is a
    // marker measured from the same corners but stops two pixels short of the edge, so
    // aiming at the drawn rule lands outside a square measured that way.
    const CBox left  = {win.x, win.y + win.h - CDE::BORDER, grab, CDE::BORDER};
    const CBox right = {win.x + win.w - grab, left.y, grab, CDE::BORDER};

    if (left.containsPoint(globalPos))
        return Layout::CORNER_BOTTOMLEFT;
    if (right.containsPoint(globalPos))
        return Layout::CORNER_BOTTOMRIGHT;
    return Layout::CORNER_NONE;
}

void CCdeDecoration::damageEntire() {
    m_barTex.reset();
    damageBar();
}

// Hyprland never damages a plugin decoration on the plugin's behalf — not on a title
// change, and not for anything else that only changes our own contents. Without a damage
// the render pass is culled and the bar keeps showing stale pixels until something
// unrelated (a resize, the zoom effect) happens to damage the same area. See AGENTS.md
// `## Button input` and `## Title text style`.
void CCdeDecoration::damageBar() {
    if (!windowVisible())
        return;

    // Expanded by 1px on each side: a shortfall shows as a stale sliver along the bar's
    // bottom or right edge.
    const CBox box = barBoxGlobal().expand(1).noNegativeSize();
    g_pHyprRenderer->damageBox(box);
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
    // The window is passed explicitly: the click may land on the bar of a window that is
    // not the focused one, and a global dispatcher would act on the wrong window.
    //
    // Deliberately NOT routed through hyprctl: under a Lua config
    // `hyprctl dispatch <name> <args>` is eval'd as `hl.dispatch(name args)`, a syntax
    // error for every classic dispatcher name. These are called in-process, so they work
    // under both config backends.
    const PHLWINDOW self = m_window.lock();

    switch (btn) {
        case CDE::BTN_CLOSE: Config::Actions::closeWindow(self); break;
        case CDE::BTN_FLOAT: Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_TOGGLE, self); break;
        case CDE::BTN_MAX: Config::Actions::fullscreenWindow(Fullscreen::FSMODE_FULLSCREEN, true, self); break;
        default: break;
    }
}

void CCdeDecoration::retexture(float scale) {
    // Raster at exactly the box draw() will use, so the texture and the geometry cannot
    // drift apart.
    const CBox         box = outerBox();
    const bool         act = isActive();
    const CDE::SConfig cfg = CDE::config();

    const bool barVisible = !CDE::edgeHidden(m_window.lock(), DECORATION_EDGE_TOP);

    // `cde:no_title` hides the text but keeps the bar and its fields; an empty title makes
    // the drag rules span the whole title field, which is then all drag area.
    const std::string title = CDE::titleHidden(m_window.lock()) ? std::string{} : m_window->m_title;

    // A top edge with no bar is the frame's top band, not a title bar: same painter as the
    // other three sides. `barVisible` also tells the side strips whether their top grab
    // marker still has a bar to sit on.
    if (isTop() && barVisible)
        m_barTex = CDE::renderBar(box.size(), scale, m_hovered, m_pressed, title, cfg, act);
    else
        m_barTex = CDE::renderBorder(m_edge, box.size(), scale, cfg, act, barVisible);

    m_lastTitle   = m_window->m_title;
    m_lastScale   = scale;
    m_lastSize    = box.size();
    m_lastFocused = act ? 1 : 0;
    m_generation  = CDE::generation();

    // A brand new texture has to be repainted, not just re-queued: see damageBar().
    damageBar();
}

bool CCdeDecoration::syncHover(const Vector2D& globalPos) {
    if (!isTop() || !windowVisible())
        return false;

    // A drag owns the cursor: whatever button it crosses must not light up for the length
    // of the drag.
    if (m_dragArmed || m_dragging)
        return false;

    int under = CDE::BTN_NONE;
    if (m_cachedBar.w > 0 && m_cachedBar.h > 0 && m_cachedBar.containsPoint(globalPos))
        under = CDE::buttonAt(m_cachedBar.size(), globalPos - m_cachedBar.pos(), CDE::config().showButtons);

    if (under == m_hovered)
        return false;

    m_hovered = under;
    m_barTex.reset();
    return true;
}

void CCdeDecoration::startDrag() {
    const PHLWINDOW self = m_window.lock();
    if (!self)
        return;

    m_dragging = true;

    // Hover and pressed are click state, not drag state — drop them or a button the drag
    // happens to cross stays lit until the release.
    m_hovered     = CDE::BTN_NONE;
    m_pressed     = CDE::BTN_NONE;
    m_clickButton = CDE::BTN_NONE;
    m_barTex.reset();

    // The corner only means anything for a resize: a move leaves it unset so the controller
    // picks its own grab point.
    const std::optional<Layout::eRectCorner> corner = m_dragMode == MBIND_RESIZE ? std::optional(m_dragCorner) : std::nullopt;

    g_layoutManager->beginDragTarget(self->layoutTarget(), m_dragMode, corner);
}

void CCdeDecoration::endGesture() {
    const bool owned = m_dragging;

    // Cleared BEFORE ending: endDragTarget() routes back into this decoration's DRAG_END
    // handler, and re-entering a live endDrag() would be a mess.
    //
    // The texture is only dropped for a gesture that actually armed or started: this runs
    // for every decoration on every left release, and an unconditional reset would throw
    // away every window's chrome raster on each click.
    const bool hadGesture = m_dragging || m_dragArmed;
    m_dragging            = false;
    m_dragArmed           = false;
    if (hadGesture)
        m_barTex.reset();

    // Only if the compositor still has OUR window. It ends the drag itself when the window
    // unmaps mid-drag (CWindow::onWindowRemoved), and ending someone else's drag — a
    // `movewindow` bind that won the race — would cancel the user's own gesture.
    if (!owned || !m_window)
        return;

    if (g_layoutManager->dragController()->target() == m_window->layoutTarget())
        g_layoutManager->endDragTarget();
}

bool CCdeDecoration::syncDrag(const Vector2D& globalPos) {
    if (!windowVisible())
        return false;

    if (!m_dragArmed)
        return false;

    // Something is already dragging (a mouse bind beat us to the press). Leave it alone
    // and stand down — our release will disarm us.
    if (g_layoutManager->dragController()->target()) {
        m_dragArmed = false;
        return false;
    }

    if (globalPos.distance(m_dragOrigin) < DRAG_THRESHOLD)
        return false;

    m_dragArmed = false;

    // A fullscreen window does not move or resize, same as under the compositor's own move and
    // resize binds (CKeybindManager::changeMouseBindMode skips it before we are even asked).
    const PHLWINDOW self = m_window.lock();
    if (!self || Fullscreen::controller()->isFullscreen(self))
        return false;

    startDrag();
    return true;
}

// True when the cached texture still matches everything that goes into it. Anything
// that changes the pixels has to be listed here or the bar keeps showing a stale raster.
bool CCdeDecoration::textureIsCurrent(float scale) const {
    if (!m_barTex)
        return false;

    const bool sameScale   = m_lastScale == scale;
    const bool sameTitle   = m_lastTitle == m_window->m_title;
    const bool sameConfig  = m_generation == CDE::generation();
    const bool sameSize    = m_lastSize == outerBox().size();
    const bool sameFocused = m_lastFocused == (isActive() ? 1 : 0);

    return sameScale && sameTitle && sameConfig && sameSize && sameFocused;
}

void CCdeDecoration::ensureTextures(float scale) {
    // main.cpp drives hover from the mouse motion listener (Hyprland sends decorations no
    // INPUT_TYPE_MOTION at all). This poll is the fallback for when that event is missed.
    if (isTop() && windowVisible() && g_pInputManager) {
        syncHover(g_pInputManager->getMouseCoordsInternal());

        // A press whose release never arrived (dragged off the bar, or released outside
        // the decoration) would otherwise leave the glyph stuck in its pressed palette.
        if (m_clickButton != CDE::BTN_NONE && m_hovered != m_clickButton) {
            m_clickButton = CDE::BTN_NONE;
            m_pressed     = CDE::BTN_NONE;
            m_barTex.reset();
        }
    }

    // Size is part of the cache key: the four edges are positioned independently, so a
    // sibling can be repositioned after we raster and leave a stale texture stretched
    // over the new box — a frame band one pixel thin, for instance.
    if (textureIsCurrent(scale))
        return;

    retexture(scale);
}

bool CCdeDecoration::onInputOnDeco(const eInputType type, const Vector2D& pos, std::any data) {
    // Nothing is left of this strip, so it has no buttons to press and no corner squares to
    // arm a resize from. Report it as NOT handled — a hidden edge must never consume input.
    if (suppressed())
        return false;

    const auto* ev         = type == INPUT_TYPE_BUTTON ? std::any_cast<IPointer::SButtonEvent>(&data) : nullptr;
    const bool   leftDown  = ev && ev->button == BTN_LEFT && windowVisible();

    // The corner grab squares are tested before anything else and on EVERY edge: the square
    // spans the bottom strip and the foot of both sides, so which decoration gets the press
    // depends on the order the compositor walks them in. A press there is a resize handle,
    // never a click.
    const Layout::eRectCorner corner = leftDown ? cornerAt(pos) : Layout::CORNER_NONE;

    if (corner != Layout::CORNER_NONE) {
        if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED) {
            // Armed, not started: syncDrag() waits for DRAG_THRESHOLD so a click on a corner
            // does not nudge the window.
            m_dragArmed  = true;
            m_dragOrigin = pos;
            m_dragMode   = MBIND_RESIZE;
            m_dragCorner = corner;
        } else {
            endGesture();
        }
        return false;
    }

    // Only the bar has buttons. The frame edges are pass-through, and so is a top edge
    // whose bar is hidden (`cde:no_titlebar`) — that one is just the top frame band, not a
    // drag handle. `false` here means "not handled": returning true would be read by
    // checkInputOnDecos() as CONSUMED, swallowing a drag dropped over the frame and
    // cancelling a `movewindow` bind started there.
    if (!isTop() || CDE::edgeHidden(m_window.lock(), DECORATION_EDGE_TOP))
        return false;

    const Vector2D size    = outerBox().size(); // match the widened bar the glyphs were drawn into
    const bool     buttons = CDE::config().showButtons;

    // `pos` is GLOBAL, not bar-relative: checkInputOnDecos forwards the very coords it
    // tested with containsPoint(), and the built-in group bar converts them the same way.
    // Every rect chrome.cpp returns is bar-relative, so convert once, here.
    const Vector2D p = pos - barBoxGlobal().pos();

    // Keeps the pressed highlight in step with the click being tracked. Runs on both
    // edges of a click, so the button un-presses even when the release lands elsewhere.
    auto syncPressed = [&] {
        if (m_clickButton != m_pressed) {
            m_pressed = m_clickButton;
            m_barTex.reset();
        }
    };

    switch (type) {
        // INPUT_TYPE_BUTTON arrives on press AND release, with the event in `data`. A
        // click's action fires on the RELEASE, never on the press.
        case INPUT_TYPE_BUTTON: {
            if (!leftDown)
                return false;

            // chrome.cpp owns the field geometry AND the hit-test against it, so the clickable
            // area cannot drift from the drawn one.
            const int hit = CDE::buttonAt(size, p, buttons);

            if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED) {
                m_clickButton = hit;

                // A press on the bare bar arms a drag rather than starting one; the cursor
                // has to travel DRAG_THRESHOLD first. syncDrag() picks it up.
                if (hit == CDE::BTN_NONE) {
                    m_dragArmed  = true;
                    m_dragOrigin = pos;
                    m_dragMode   = MBIND_MOVE;
                }
            } else {
                const int held = m_clickButton;

                // Retire the press BEFORE acting. closeWindow() is async today, but the
                // action is allowed to tear the window down, and nothing below may touch
                // `this` after it. Ending the gesture first also un-highlights on both
                // edges of a click, not just when the release lands elsewhere.
                m_clickButton = CDE::BTN_NONE;
                syncPressed();
                endGesture();

                if (held != CDE::BTN_NONE && held == hit)
                    runButtonAction(held);
            }

            return false; // not "handled": the press may still focus and raise the window
        }

        // Only a mouse `movewindow` bind gets here: it asks the window whether it wants to
        // start the drag itself, and starts its own when the answer is no. False on the bare
        // bar therefore *enables* that bind rather than cancelling it. True on a button, so
        // a press landing there can never move the window.
        case INPUT_TYPE_DRAG_START:
            return CDE::buttonAt(size, p, buttons) != CDE::BTN_NONE;

        // Reached from the compositor's dragEnd() before it finishes the move it started, so
        // false lets it complete (geometry commit, focus, drag-into-group). Ending a drag
        // here is what the BUTTON release above does.
        case INPUT_TYPE_DRAG_END:
            m_clickButton = CDE::BTN_NONE;
            syncPressed();
            return false;

        // Anything else (axis, motion) is not ours: the bar must not eat input it does not
        // act on.
        default: return false;
    }
}

void CCdeDecoration::draw(PHLMONITOR monitor, float const& alpha) {
    if (!windowVisible())
        return;

    // The strip is gone, and so is everything cached from it: m_cachedBar is what
    // syncHover() tests the cursor against, so a stale one would keep a button lit on a bar
    // that is no longer drawn.
    if (suppressed()) {
        m_cachedBar = {};
        m_hovered   = CDE::BTN_NONE;
        m_barTex.reset();
        return;
    }

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

    // Refreshed here so syncHover() can reject a cursor that is nowhere near this bar
    // with a single rect test instead of recomputing the outer box per mouse move.
    if (isTop())
        m_cachedBar = global;

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
