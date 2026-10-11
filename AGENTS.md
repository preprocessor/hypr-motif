# AGENTS.md

## Project

A Hyprland plugin that renders Common Desktop Environment (Motif) window chrome.

**Scope:** the title bar — its beveled frame, the three fields it is divided into, and
the min/max/close buttons. The look is drawn **procedurally with Cairo** at runtime —
there are no image assets. Do not add a bitmap pipeline; the chrome is simple 3D bevels
and drawing it beats slicing pixels out of `example.png` (which is itself 2x-scaled
and shadowed).

See `## Status` for what currently renders and `## Roadmap` for what is next.

## Commands

```sh
nix develop                                          # gcc 14.4, cmake 4.4.3, hyprland 0.56.2 headers
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
cp build/libcde.so build/libcde-next.so              # fresh path — see gotcha 3
hyprctl plugin unload "$(hyprctl plugin list | grep -B1 libcde.so | head -1)"
hyprctl plugin load ~/.local/share/hyprland/plugins/libcde.so
```

Build to a **new filename** each time, never over the one you are loading — the
compositor keeps the old inode mapped and silently runs stale code.

`flake.nix` pins `nixpkgs-unstable` and yields **hyprland 0.56.2**, `aquamarine 0.15.1`,
`cairo 1.18.4`. Treat those versions as exact, not "whatever is in the store".

## ABI constraint — read this before touching the build

The compositor `dlopen`s the plugin and passes live C++ objects across the boundary.
**There is no ABI stability.** A plugin built against a different hyprland than the one
running will be ejected, or crash the compositor outright. It will not fail gracefully.

`flake.nix` forces `gcc14Stdenv` for exactly this reason — do not "modernise" it to the
default stdenv. If you bump `flake.lock`, rebuild before you touch your running session.

## Layout

```
src/main.cpp              plugin entry: pluginAPIVersion / pluginInit / pluginExit
src/CCdeDecoration.hpp    IHyprWindowDecoration subclass — declaration
src/CCdeDecoration.cpp    positioning, draw(), input handling
src/chrome.hpp            layout constants, button rects, Cairo entry points
src/chrome.cpp            Cairo drawing of the bevels, title bar and buttons
CMakeLists.txt
```

## Build wiring

Do **not** hand-roll include directories. `hyprland.pc` already exists in the dev
output and pulls cairo transitively, so CMake needs exactly:

```cmake
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
target_link_libraries(cde PRIVATE PkgConfig::HYPRLAND)
```

`pkg-config --cflags --libs hyprland` resolves correctly inside `nix develop`.

## Architecture

One `IHyprWindowDecoration` instance per window. Added from the `windowopen` event
via `HyprlandAPI::addWindowDecoration`, torn down on close.

`draw(PHLMONITOR, float const& alpha)` is invoked once per frame per decoration.
Positioning is `DECORATION_POSITION_STICKY` on `DECORATION_EDGE_TOP` with
`reserved = true`, so the title bar takes **real space** and the client shrinks beneath
it, rather than the chrome floating over the client area.

## Status

Renders today, confirmed by screenshot rather than by assumption:

- Motif-grey title bar with a raised outer bevel.
- Three beveled buttons with glyphs (minimize, maximize, close).
- Title text rendered via `CHyprRenderer::renderText` and blitted centred.
- The bar reserves 22px, so the client area shrinks beneath it.

Known gaps, in the order they bite:

- **Button hit-testing is unverified.** No way to inject synthetic input here, and
  `onInputOnDeco`'s `pos` is assumed **decoration-local**. If it is actually global,
  nothing will ever register a hit. Check this first — `barDeco.cpp:211` subtracts
  `assignedBoxGlobal().pos()` from raw mouse coords, which hints the raw form is global.
- **The bar is not yet fielded.** Real CDE divides it into three bevel-delimited
  regions; ours is flat with all three buttons on the left. See `## Title bar anatomy`.
- **The active border still double-draws** — see `## Known unknown`.

## Measured reference values

Sampled from `example.png`. **The PNG is 2x-scaled, so image pixels ÷ 2 = logical pixels.**
Re-measure rather than eyeball; the greys differ by only a few counts and eyeballing
gets them wrong.

| Element | Hex | Current constant |
|---|---|---|
| Title bar face | `#999999` | `0.655` (too light) |
| Bevel light | `#d1d1d1` | `0.855` (too light) |
| Bevel dark | `#4e4e4e` | `0.325` (too light) |
| Bottom highlight | `#ffffff` | not drawn |
| Menu bar | `#5c90a4` | n/a — out of scope |
| Client background | `#a59990` | n/a — not ours to draw |

The title bar greys are genuinely **neutral** (R=G=B). The warm tone that reads as
"authentic CDE" in the screenshot comes from the *client* background, which we do not
draw. Do not warm the chrome to match the impression.

## Title bar anatomy

Measured across the Application Manager title bar (`example.png`, grey variant):

Horizontal fields, left to right:

```
| bevel | close field | sep | title field (flexible)      | sep | min/max field | bevel |
   1px      18px       1px                                1px      38px        1px
```

The right field holds two buttons with a separator roughly 20px in. The left field holds
one — in real Motif that is the window-menu button; we put close there to keep the
1-left/2-right shape while retaining all three buttons. To move close to the right, flip
the ordering in `buttonRect` — it is one function.

Vertical bevel order, top-down:

```
2px  raised light   (outer bevel)
2px  face
1px  dark           ┐ separator
1px  light          ┘
7px  title field
3px  white highlight (bottom edge)
```

Separators are a 1px dark line over a 1px light line — that pairing is what makes the
fields read as three panels rather than one bar.

## Config options

Planned; all namespaced `plugin:cde:` and registered with `addConfigValueV2`.

| Key | Type | Default |
|---|---|---|
| `bar_height` | int | `22` |
| `color_face` | colour | `#999999` |
| `color_light` | colour | `#d1d1d1` |
| `color_dark` | colour | `#4e4e4e` |
| `color_highlight` | colour | `#ffffff` |
| `show_buttons` | bool | `true` |
| `title_font_size` | int | `11` |

Two constraints on implementing these:

- `Config::Values::IValue` exposes **no per-value change callback**. Live reload must
  go through `Event::bus()->m_events.config.reloaded` and a generation counter that
  decorations compare against. This is what hyprbars does; there is no way around it.
- `bar_height` feeds `desiredExtents`, so changing it at runtime only takes effect if
  the positioner is re-run — that means `damageEntire()`. **This is untested** and is
  the item most likely to need a second pass.
- `CColorValue::value()` returns a packed integer; `CHyprColor(uint64_t)` consumes it.

## The three things that will waste your day

All three cost hours to find. They are not obvious from the headers.

**1. `draw()` cannot touch the framebuffer.** There is no GL context current inside it.
Calling `g_pHyprOpenGL->renderTexture()` or `renderRect()` there compiles, runs, and
draws *nothing at all* — no error, no log line. You must queue a pass element and let
the compositor's element renderer issue the draw:

```cpp
CTexPassElement::SRenderData data;
data.tex = tex; data.box = box; data.a = alpha;
data.damage = CRegion(box); data.clipBox = box; data.clipRegion = CRegion(box);
g_pHyprRenderer->currentPass().add(Hyprutils::Memory::makeUnique<CTexPassElement>(std::move(data)));
```

`currentPass()` is public (`render/Renderer.hpp:139`). This is the whole reason hyprbars
carries its own `BarPassElement`.

**2. `assignedGeometry` is relative to the edge point, not the window.** For STICKY it
must be translated by `g_pDecorationPositioner->getEdgeDefinedPoint(edges, window)`,
then by the workspace `m_renderOffset` for non-pinned windows. Adding it to
`window->logicalBox()` instead puts the bar far off-screen — `logicalBox()` already
excludes decoration extents, so you double-subtract.

**3. Rebuilding over the same path does not reload.** `hyprctl plugin unload` + `load`
on a rebuilt `build/libcde.so` silently keeps running the **old** code — the compositor
holds the previous, now-deleted inode, and `hyprctl plugin list` shows stale metadata
from the old build. Build, then `cp build/libcde.so build/libcde-<new>.so` and load the
*new* path. Check `ls -l /proc/$(pgrep -x Hyprland)/maps | grep cde` — it shows
`(deleted)` when you have been fooled.

Corollary: `printf` from a plugin goes nowhere when Hyprland was started by a greeter,
because its stdout is a socket. To debug, append to a file under `$HOME`.

## Reference implementation

`hyprbars` — `/home/wyspr/Repos/hyprland-plugins/hyprbars` — is a working Hyprland
titlebar plugin and the canonical example of an `IHyprWindowDecoration` in the wild.
Consult it when the API notes below are not enough. It renders its title once into an
`SP<Render::ITexture>` and blits it, the same approach our Cairo chrome should take.

**Copy its structure, not its signatures.** hyprbars tracks Hyprland master, and its
`draw()` takes `Render::CRenderContext&` and `Render::SWindowRenderPresentation&` —
neither exists in our pinned 0.56.2, where the signature is `draw(PHLMONITOR, float const&)`
(`render/decorations/IHyprWindowDecoration.hpp:41`). Its recent commits are literally
"chase hyprland", so expect more drift.

## API notes

Facts that are expensive to rediscover. Verify against the headers before trusting them;
the plugin API moves between hyprland releases.

- `addWindowDecoration` is the **only** path to drawing chrome from a plugin.
- `getDecorationType()` must return `DECORATION_CUSTOM`.
- Flags must include `DECORATION_ALLOWS_MOUSE_INPUT`, or the buttons get no clicks,
  and `DECORATION_PART_OF_MAIN_WINDOW` so drop shadows and animations account for the
  new geometry.
- `onInputOnDeco` returns `false` to swallow the event.
- `Render::CGLTexture` is the only concrete `ITexture` in the shipped headers — the only
  way to get pixel data on screen from a plugin.
- Header root is `${hyprland.dev}/include/hyprland/src`, e.g. `#include <plugins/PluginAPI.hpp>`.
- The base class defines non-pure virtuals `getDecorationLayer()`, `getDecorationFlags()`
  and `getDisplayName()` — override only what you need.

## Known unknown

Whether Hyprland's built-in `CHyprBorderDecoration` suppresses itself when a plugin
decoration is present. `doesntWantBorders()` is private and its body is **not** in the
shipped headers — nixpkgs ships headers only, no `.cpp`. Expect to need
`decoration:border_size = 0` in `hyprland.conf` to avoid a double frame. Confirm the
behaviour empirically before trying to "fix" it in code.

## Roadmap

Agreed next increment — **title bar fidelity plus config options**, in this order:

1. Rework `SPalette` from one scalar per role to three floats, so colour config values
   map straight through. Today `setGrey` forces R=G=B, which is why nothing can be warm.
2. Apply the measured values and draw the fielded layout: left close field, flexible
   title field, right min/max field, each delimited by a 1px dark-over-light separator.
   Add the 3px white highlight along the bottom edge.
3. Move `buttonRect` to the 1-left/2-right arrangement described above.
4. Wire the config table. Bump a generation counter on `config.reloaded`; decorations
   compare it in `ensureTextures()` and re-rasterise on mismatch.
5. Verify button clicks, which has never been exercised.

## Non-goals

Deliberately out of scope — do not add these by accident:

- App menu bars. Motif has a menubar, but populating it is app territory, not decoration.
- The status bar strip at the bottom of the client area.
- The bottom Motif dock / launcher. That is a separate compositor surface, not a decoration.
- **The outer frame on the left/right/bottom window edges.** Only the top edge is
  decorated. A full surround means three more decoration instances with their own edge
  and reserved-space math, so it is its own increment — not an oversight.
- Per-button config toggles. `show_buttons` covers what has actually been asked for.