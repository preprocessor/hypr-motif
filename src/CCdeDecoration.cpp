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
}

void CCdeDecoration::updateWindow(PHLWINDOW) {
    // Title, class or size may all have changed; the next draw() re-checks cheaply.
    m_barTex.reset();
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

void CCdeDecoration::retexture(float scale) {
    if (isTop())
        m_barTex = CDE::renderBar(m_assigned.size(), scale, m_hovered, m_pressed, m_window->m_title, CDE::config().titleFontSize);
    else
        m_barTex = CDE::renderBorder(m_edge, m_assigned.size(), scale);

    m_lastTitle  = m_window->m_title;
    m_lastScale  = scale;
    m_generation = CDE::generation();
}

void CCdeDecoration::ensureTextures(float scale) {
    if (m_barTex && m_lastScale == scale && m_lastTitle == m_window->m_title && m_generation == CDE::generation())
        return;

    retexture(scale);
}

bool CCdeDecoration::onInputOnDeco(const eInputType type, const Vector2D& pos, std::any) {
    // Only the top edge has buttons; the frame edges are decorative for now.
    if (!isTop())
        return true;

    const Vector2D size   = m_assigned.size();
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

// Every edge derives from the window's outer box (decorations included) rather
    // than from the assigned geometry: the positioner anchors each decoration to the
    // outer window, so translating by the edge point double-offsets, and logicalBox()
    // does not report the window box at all. The bar spans the full outer width so it
    // sits over the frame rather than leaving a gap at the top corners.
    const CBox outer = g_pDecorationPositioner->getBoxWithIncludedDecos(m_window.lock());
    if (outer.w <= 0 || outer.h <= 0)
        return;

    const double barH = CDE::config().barHeight;

    CBox global;
    switch (m_edge) {
        case DECORATION_EDGE_TOP: global = {outer.x, outer.y, outer.w, barH}; break;
        case DECORATION_EDGE_LEFT: global = {outer.x, outer.y + barH, CDE::BORDER, outer.h - barH - CDE::BORDER}; break;
        case DECORATION_EDGE_RIGHT: global = {outer.x + outer.w - CDE::BORDER, outer.y + barH, CDE::BORDER, outer.h - barH - CDE::BORDER}; break;
        case DECORATION_EDGE_BOTTOM: global = {outer.x, outer.y + outer.h - CDE::BORDER, outer.w, CDE::BORDER}; break;
        default: return;
    }

    if (m_window->m_workspace && !m_window->m_pinned)
        global.translate(m_window->m_workspace->m_renderOffset->value());

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