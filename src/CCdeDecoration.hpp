#pragma once

#define WLR_USE_UNSTABLE

#include <string>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

#include "chrome.hpp"
#include "config.hpp"

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

  private:
    bool isTop() const {
        return m_edge == DECORATION_EDGE_TOP;
    }

    // The CDE frame is one ring around the client, so every strip is sized against the
    // window's OUTER span, not the reserved box the positioner hands back (which has
    // already subtracted the bands we reserve). Both draw() and retexture() must use
    // this — a texture rastered at one size and stretched over the other puts the
    // separators out of register with the buttons.
    CBox outerBox() const;

    // Drop shadow box for this edge. Only RIGHT and BOTTOM cast shadows.
    CBox shadowBox() const;

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

    // The bar is only re-rasterised when the title, scale, focus or button state changes.
    void ensureTextures(float scale);

    PHLWINDOWREF m_window;
    uint32_t     m_edge;
    std::string  m_name;
    CBox         m_assigned = {0, 0, 0, 0};

    SP<Render::ITexture> m_barTex;
    SP<Render::ITexture> m_shadowTex;
    CBox                 m_lastShadowBox = {};


    std::string m_lastTitle;
    float       m_lastScale = 0.f;
    Vector2D    m_lastSize  = {-1, -1};
    // int, not bool, so the first frame always re-rasterises: a bool default of false
    // would look like a valid cache hit for an unfocused window.
    int         m_lastFocused = -1;
    uint64_t    m_generation = 0;
    int         m_hovered    = CDE::BTN_NONE;
    int         m_pressed    = CDE::BTN_NONE;
    Vector2D    m_dragStart;

    void        retexture(float scale);
    void        dispatch(const std::string& cmd, const std::string& args = "");
};