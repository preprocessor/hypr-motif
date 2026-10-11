#pragma once

#define WLR_USE_UNSTABLE

#include <string>

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

#include "chrome.hpp"
#include "config.hpp"

class CCdeDecoration : public IHyprWindowDecoration {
  public:
    // `edge` is an eDecorationEdges value. Exactly one deco per edge per window.
    CCdeDecoration(PHLWINDOW w, uint32_t edge, std::string name);
    ~CCdeDecoration() override = default;

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

    // The bar is only re-rasterised when the title, scale or button state changes.
    void ensureTextures(float scale);

    PHLWINDOWREF m_window;
    uint32_t     m_edge;
    std::string  m_name;
    CBox         m_assigned = {0, 0, 0, 0};

    SP<Render::ITexture> m_barTex;

    std::string m_lastTitle;
    float       m_lastScale = 0.f;
    Vector2D    m_lastSize  = {-1, -1};
    uint64_t    m_generation = 0;
    int         m_hovered    = CDE::BTN_NONE;
    int         m_pressed    = CDE::BTN_NONE;
    Vector2D    m_dragStart;

    void        retexture(float scale);
    void        dispatch(const std::string& cmd, const std::string& args = "");
};