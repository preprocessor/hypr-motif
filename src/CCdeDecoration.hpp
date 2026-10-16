#pragma once

#define WLR_USE_UNSTABLE

#include <string>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

#include "chrome.hpp"
#include "config.hpp"

// How far the cursor must travel after a press on the bare bar before the window starts
// following it. `binds:drag_threshold` defaults to 0, and dragging straight off the press
// would float a tiled window on every click that lands on the title bar to focus it.
static constexpr double DRAG_THRESHOLD = 3;

class CCdeDecoration : public IHyprWindowDecoration {
  public:
    // `edge` is an eDecorationEdges value. Exactly one deco per edge per window.
    CCdeDecoration(PHLWINDOW w, uint32_t edge, std::string name);
    ~CCdeDecoration() override;

    SDecorationPositioningInfo getPositioningInfo() override;
    void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                       draw(PHLMONITOR monitor, float const& alpha) override;
    eDecorationType            getDecorationType() override;
    void                       updateWindow(PHLWINDOW w) override;
    void                       damageEntire() override;
    eDecorationLayer           getDecorationLayer() override;
    uint64_t                   getDecorationFlags() override;
    std::string                getDisplayName() override;
    bool                       onInputOnDeco(const eInputType type, const Vector2D& pos, std::any data = {}) override;

  public:
    // Hyprland does not damage a plugin decoration when only its CONTENTS change (a
    // title update), so the new texture's pass is culled and the old text stays. This
    // damages the bar's region explicitly; called whenever we build a fresh texture.
    // Public because main.cpp's window.title listener needs it — the damage has to come
    // from outside draw(), which is not called at all when nothing is damaged.
    void damageBar();

    // Drops the cached raster so the next draw() rebuilds it. Public for the same
    // reason; the event handler has no other way to invalidate the cache.
    void invalidate() {
        m_barTex.reset();
    }

    // Recomputes which button the cursor is over, from a GLOBAL position. Returns true
    // if it changed, in which case the texture is already dropped and the caller only
    // has to damage the bar.
    //
    // This has to be callable from OUTSIDE draw(): with nothing damaged, Hyprland never
    // calls draw(), so a hover polled from inside it can never notice the cursor arriving
    // — the same chicken-and-egg as the title update. main.cpp calls it from the mouse
    // motion listener. Uses the cached bar box so the common "cursor is nowhere near this
    // window" case is one rect test.
    bool syncHover(const Vector2D& globalPos);

    // Starts the window drag once the cursor passes DRAG_THRESHOLD from the press, and
    // ends a drag whose release landed off the window. Called from the mouse motion
    // listener for the same reason syncHover is: Hyprland sends decorations no
    // INPUT_TYPE_MOTION, and a poll inside draw() would never run for a window that is
    // sitting still — which is exactly the state a window is in while waiting to be
    // dragged. Returns true if a drag started, so the caller can damage the bar.
    bool syncDrag(const Vector2D& globalPos);

    // Retires a gesture this decoration armed or started, whether a bar drag or a corner
    // resize. Public because main.cpp's mouse.button listener calls it for every decoration:
    // a release that lands off the window never reaches onInputOnDeco, and nothing else
    // ends a gesture the plugin started — the compositor's own drag is ended by the mouse
    // bind that owns it, and there is no bind here.
    void endGesture();

  private:
    bool isTop() const {
        return m_edge == DECORATION_EDGE_TOP;
    }

    // The window's outer rect, from the boxes the compositor gave the four sibling strips.
    // This is what the corner grab squares are measured from.
    CBox windowBox() const;

    // The CDE frame is one ring around the client, so every strip is sized against the
    // window's OUTER span, not the reserved box the positioner hands back (which has
    // already subtracted the bands we reserve). Both draw() and retexture() must use
    // this — a texture rastered at one size and stretched over the other puts the
    // separators out of register with the buttons.
    CBox outerBox() const;

    // Which bottom corner grab square a GLOBAL point is in, or CORNER_NONE. One square spans
    // the bottom strip and the foot of both side strips, so which decoration happens to see
    // the press is an accident of iteration order — all four test the same two rects.
    Layout::eRectCorner cornerAt(const Vector2D& globalPos) const;

    // outerBox() translated into the space the compositor actually hands input and draw
    // calls in: global screen coordinates, workspace render offset included. The bar
    // edge only.
    CBox barBoxGlobal() const;

    // What a completed click on `btn` does. Actions are called through
    // Config::Actions with our own window, so they target the window whose bar was
    // clicked rather than whatever happens to be focused, and so they work under both
    // the hyprlang and the Lua config backends.
    void runButtonAction(int btn);

    // Hands the window to the compositor's own drag controller, which is what makes the bar
    // drag and the corners resize natively: it is the same drag a `mouse:…, movewindow` /
    // `resizeactive` bind produces, so float-on-drag, drag_threshold, min/max sizes,
    // snapping and aspect-ratio all behave as the user configured them. Not
    // CWindow::moveWindowPos / moveWindowSize, which would ignore every one of those.
    // m_dragMode is MBIND_MOVE for the bar and MBIND_RESIZE for a corner, where
    // m_dragCorner says which corner was grabbed.
    void startDrag();

    // Focused windows use a different palette, so a focus change must invalidate the
    // cached texture. Polled rather than subscribed: window.active only names the newly
    // focused window, not the one losing focus, so an event handler would have to track
    // and invalidate the previous one by hand. This is a pointer compare per frame.
    // isWindowActive() takes a PHLWINDOW (shared ptr) and m_window is a PHLWINDOWREF (weak
    // ptr), which does not implicitly convert. Comparing the raw pointers sidesteps
    // constructing a shared pointer just to ask a yes/no question.
    bool isActive() const {
        return m_window && Desktop::focusState()->window().get() == m_window.get();
    }

    // Cached raster is only valid while the window is still mapped and placed.
    bool windowVisible() const {
        return m_window && m_window->m_isMapped && m_assigned.w && m_assigned.h;
    }

    // Whether the cached texture still matches its cache key.
    bool textureIsCurrent(float scale) const;

    // Re-rasterises when the cache key no longer matches, and refreshes hover meanwhile.
    void ensureTextures(float scale);

    PHLWINDOWREF m_window;
    uint32_t     m_edge;
    std::string  m_name;
    CBox         m_assigned = {0, 0, 0, 0};

    SP<Render::ITexture> m_barTex;

    std::string m_lastTitle;
    float       m_lastScale = 0.f;
    Vector2D    m_lastSize  = {-1, -1};
    // int, not bool, so the first frame always re-rasterises: a bool default of false
    // would look like a valid cache hit for an unfocused window.
    int         m_lastFocused = -1;
    uint64_t    m_generation = 0;
    int         m_hovered    = CDE::BTN_NONE;
    int         m_pressed    = CDE::BTN_NONE;
    // Button the press landed on. Kept separate from m_pressed because the action fires
    // on release, and only if the release lands on the same button.
    int         m_clickButton = CDE::BTN_NONE;
    // A press on the bare bar (MBIND_MOVE) or on a corner grab square (MBIND_RESIZE), waiting
    // for the cursor to travel DRAG_THRESHOLD before the window starts following it.
    bool               m_dragArmed  = false;
    Vector2D           m_dragOrigin = {};
    eMouseBindMode     m_dragMode   = MBIND_INVALID;
    Layout::eRectCorner m_dragCorner = Layout::CORNER_NONE;
    // Whether the compositor is currently dragging or resizing THIS window on our behalf.
    bool               m_dragging   = false;
    // The bar's global box from the last draw(). Hover is tested against this rather than
    // recomputing outerBox() (five positioner lookups) on every mouse-move event.
    CBox        m_cachedBar = {};

    void        retexture(float scale);
};