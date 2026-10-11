#include "CCdeDecoration.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

using namespace Hyprutils::Math;

static const char* DISPLAY_NAME = "CDE";

CCdeDecoration::CCdeDecoration(PHLWINDOW w)
    : IHyprWindowDecoration(w)
    , m_window(w) {}

SDecorationPositioningInfo CCdeDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy         = DECORATION_POSITION_STICKY;
    info.edges          = DECORATION_EDGE_TOP;
    info.priority       = 10005; // outrank hyprland's own border decoration
    info.reserved       = true;
    info.desiredExtents = {{0, CDE::config().barHeight}, {0, 0}};
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
    return DISPLAY_NAME;
}

void CCdeDecoration::dispatch(const std::string& cmd, const std::string& args) {
    HyprlandAPI::invokeHyprctlCommand("dispatch", args.empty() ? cmd : cmd + " " + args);
}

void CCdeDecoration::retexture(float scale) {
    m_barTex = CDE::renderBar(m_assigned.size(), scale, m_hovered, m_pressed, m_window->m_title, CDE::config().titleFontSize);

    m_lastTitle    = m_window->m_title;
    m_lastScale    = scale;
    m_generation   = CDE::generation();
}

void CCdeDecoration::ensureTextures(float scale) {
    if (m_barTex && m_lastScale == scale && m_lastTitle == m_window->m_title && m_generation == CDE::generation())
        return;

    retexture(scale);
}

bool CCdeDecoration::onInputOnDeco(const eInputType type, const Vector2D& pos, std::any) {
    const Vector2D size = m_assigned.size();
    const bool     buttons = CDE::config().showButtons;

    auto buttonAt = [&](const Vector2D& p) -> int {
        for (int b = 0; b < CDE::BTN_COUNT; ++b) {
            const CBox r = CDE::buttonRect(size, b, buttons);
            if (p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h)
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
            m_pressed     = CDE::BTN_NONE;
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

    // For STICKY, assignedGeometry is relative to the top edge's defined point — NOT the
    // window origin. Scrolling layouts additionally shift by the workspace render offset.
    CBox global = m_assigned;
    global.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_TOP, m_window));
    if (m_window->m_workspace && !m_window->m_pinned)
        global.translate(m_window->m_workspace->m_renderOffset->value());

    // Decorations cannot draw to the framebuffer directly: draw() runs outside the GL
    // pass. Queue a texture element and let the compositor's element renderer issue the
    // draw with a current context.
    auto queue = [&](SP<Render::ITexture> tex, const CBox& box) {
        if (!tex)
            return;

        CTexPassElement::SRenderData data;
        data.tex        = tex;
        data.box        = box;
        data.a          = alpha;
        data.damage     = CRegion(box);
        data.clipBox    = box;
        data.clipRegion = CRegion(box);

        g_pHyprRenderer->currentPass().add(Hyprutils::Memory::makeUnique<CTexPassElement>(std::move(data)));
    };

    queue(m_barTex, global);
}