#include "CCdeDecoration.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

using namespace Hyprutils::Math;

CCdeDecoration::CCdeDecoration(PHLWINDOW w, uint32_t edge, std::string name)
    : IHyprWindowDecoration(w)
    , m_window(w)
    , m_edge(edge)
    , m_name(std::move(name)) {}

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

void CCdeDecoration::dispatch(const std::string& cmd, const std::string& args) {
    HyprlandAPI::invokeHyprctlCommand("dispatch", args.empty() ? cmd : cmd + " " + args);
}

CBox CCdeDecoration::shadowStrip() const {
        const auto cfg = CDE::config();
        if (!cfg.shadowEnabled || cfg.shadowRange <= 0)
            return {};

        const CBox outer = outerBox();
        if (!outer.w || !outer.h)
            return {};

        const double r = cfg.shadowRange;

        switch (m_edge) {
            case DECORATION_EDGE_TOP: return {outer.x, outer.y - r, outer.w, r};
            case DECORATION_EDGE_LEFT: return {outer.x - r, outer.y, r, outer.h};
            case DECORATION_EDGE_RIGHT: return {outer.x + outer.w, outer.y, r, outer.h};
            case DECORATION_EDGE_BOTTOM: return {outer.x, outer.y + outer.h, outer.w, r};
            default: return {};
        }
    }

    CBox CCdeDecoration::shadowCorner(bool second) const {
        const auto cfg = CDE::config();
        if (!cfg.shadowEnabled || cfg.shadowRange <= 0 || (m_edge != DECORATION_EDGE_TOP && m_edge != DECORATION_EDGE_BOTTOM))
            return {};

        const CBox outer = outerBox();
        if (!outer.w || !outer.h)
            return {};

        const double r = cfg.shadowRange;

        const double x = second ? outer.x + outer.w : outer.x - r;
        const double y = m_edge == DECORATION_EDGE_TOP ? outer.y - r : outer.y + outer.h;
        return {x, y, r, r};
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

    const CBox sh = shadowStrip();
    if (sh.w && sh.h) {
        const bool alongWindow = m_edge == DECORATION_EDGE_TOP || m_edge == DECORATION_EDGE_BOTTOM;
        const bool nearAtMax   = m_edge == DECORATION_EDGE_TOP || m_edge == DECORATION_EDGE_LEFT;
        m_shadowTex            = CDE::renderShadow(sh.size(), scale, alongWindow, nearAtMax, CDE::config().shadowColor);
    } else
        m_shadowTex = nullptr;

    const CBox ca = shadowCorner(false);
    const CBox cb = shadowCorner(true);
    const auto  col = CDE::config().shadowColor;
    const bool  topEdge = m_edge == DECORATION_EDGE_TOP;
    m_shadowCornerA = ca.w && ca.h ? CDE::renderShadowCorner(ca.size(), scale, true, topEdge, col) : nullptr;
    m_shadowCornerB = cb.w && cb.h ? CDE::renderShadowCorner(cb.size(), scale, false, topEdge, col) : nullptr;
}

void CCdeDecoration::ensureTextures(float scale) {
    // Size is part of the cache key. The four edges are positioned independently and a
    // sibling can be repositioned after we raster, which would otherwise leave a
    // stale texture stretched over the new box — e.g. a frame band one pixel thin.
    if (m_barTex && m_lastScale == scale && m_lastTitle == m_window->m_title && m_generation == CDE::generation() && m_lastSize == outerBox().size() && m_lastFocused == (isActive() ? 1 : 0))
        return;

    retexture(scale);
}

bool CCdeDecoration::onInputOnDeco(const eInputType type, const Vector2D& pos, std::any) {
    // Only the top edge has buttons; the frame edges are decorative for now.
    if (!isTop())
        return true;

    const Vector2D size   = outerBox().size(); // match the widened bar the glyphs were drawn into
    const bool     buttons = CDE::config().showButtons;

    auto buttonAt = [&](const Vector2D& p) -> int {
        for (int b = 0; b < CDE::BTN_COUNT; ++b) {
            const CBox r = CDE::buttonRect(size, b, buttons);
            if (r.w && p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h)
                return b;
        }
        return CDE::BTN_NONE;
    };

    switch (type) {
        case INPUT_TYPE_MOTION: {
            const int hit = buttonAt(pos);
            if (hit != m_hovered) {
                m_hovered = hit;
                m_barTex.reset();
            }
            return true;
        }

        case INPUT_TYPE_BUTTON: {
            const int hit = buttonAt(pos);
            if (hit != m_pressed) {
                m_pressed = hit;
                m_barTex.reset();
            }
            return false; // swallow, so the click activates a button rather than the client
        }

        case INPUT_TYPE_DRAG_START: {
            m_dragStart = pos;
            m_pressed   = buttonAt(pos);
            m_barTex.reset();
            return m_pressed == CDE::BTN_NONE;
        }

        case INPUT_TYPE_DRAG_END: {
            const int hit  = buttonAt(pos);
            const int held = m_pressed;
            m_pressed      = CDE::BTN_NONE;
            m_barTex.reset();

            if (held != CDE::BTN_NONE && held == hit) {
                switch (held) {
                    case CDE::BTN_MIN: dispatch("minimize"); break;
                    case CDE::BTN_MAX: dispatch("fullscreen", "toggle"); break;
                    case CDE::BTN_CLOSE: dispatch("closewindow"); break;
                    default: break;
                }
            } else if (held == CDE::BTN_NONE && std::abs(pos.x - m_dragStart.x) > 2)
                dispatch("movewindow", "cursor,0");

            return true;
        }

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
    // window's outer span. Do NOT use getBoxWithIncludedDecos() here: it already accounts
    // for the space we ourselves reserved, so slicing it per edge is a feedback loop that
    // drifts by the border width.
    CBox global = outerBox();
    if (!global.w || !global.h)
        return;

    if (m_window->m_workspace && !m_window->m_pinned)
        global.translate(m_window->m_workspace->m_renderOffset->value());

// Shadow first, so the frame paints over any overlap. This sits outside the window's
    // own box, which is exactly why we draw it ourselves: Hyprland's shadow decoration
    // cannot place it correctly once a plugin reserves space.
    const auto queueShadow = [&](const CBox& sb, const SP<Render::ITexture>& tex) {
        if (!tex || !sb.w || !sb.h)
            return;

        CBox g = sb;
        if (m_window->m_workspace && !m_window->m_pinned)
            g.translate(m_window->m_workspace->m_renderOffset->value());

        CTexPassElement::SRenderData sd;
        sd.tex        = tex;
        sd.box        = g;
        sd.a          = alpha;
        sd.damage     = CRegion(g);
        sd.clipBox    = g;
        sd.clipRegion = CRegion(g);
        g_pHyprRenderer->currentPass().add(Hyprutils::Memory::makeUnique<CTexPassElement>(std::move(sd)));
    };

    queueShadow(shadowStrip(), m_shadowTex);
    queueShadow(shadowCorner(false), m_shadowCornerA);
    queueShadow(shadowCorner(true), m_shadowCornerB);

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