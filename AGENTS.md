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
src/config.hpp            config struct, generation counter
src/config.cpp            plugin:cde:* value registration + live-read
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

- Motif-grey title bar on the **measured** palette (`#999999` face, `#d1d1d1` light,
  `#4e4e4e` dark), not the earlier hand-picked greys.
- Fielded layout: close button left, flexible title field, min/max right, each field
  delimited by a 1px dark-over-light separator.
- Title drawn **into the bar texture** with Cairo's toy text API, in `#ffffff`, centred
  **within the title field**. A second pass element does not work — see gotcha 4.
- The bar reserves its configured height (25px), so the client shrinks beneath it.

Known gaps, in the order they bite:

- **Button hit-testing is unverified.** No way to inject synthetic input here, and
  `onInputOnDeco`'s `pos` is assumed **decoration-local**. If it is actually global,
  nothing will ever register a hit. Check this first — `barDeco.cpp:211` subtracts
  `assignedBoxGlobal().pos()` from raw mouse coords, which hints the raw form is global.
- **`bar_height` changes at runtime are untested.** It feeds `desiredExtents`, so it
  only takes effect once the positioner re-runs.
- **The active border still double-draws** — see `## Known unknown`.

## Measured reference values

Sampled from `example.png`. **The PNG is 2x-scaled, so image pixels ÷ 2 = logical pixels.**
Re-measure rather than eyeball; the greys differ by only a few counts and eyeballing
gets them wrong.

| Element | Hex | Notes |
|---|---|---|
| Title bar face | `#999999` | |
| Bevel light | `#d1d1d1` | |
| Bevel dark | `#4e4e4e` | |
| Title text | `#ffffff` | white on the face, **not** dark |
| Window frame | `#76818f` | the grey edge above the bar; not ours |
| Menu bar | `#5c90a4` | n/a — out of scope |
| Client background | `#a59990` | n/a — not ours to draw |

**There is no white highlight band.** An earlier revision of this file claimed one,
measured from a column scan that happened to land on the title glyphs. The bar has no
highlight strip; the only `#ffffff` in it is the title text.

**Extract the grid with step 2 on BOTH axes.** The PNG is 2x-scaled, so one logical
pixel is 2x2 image pixels. Sampling x at step 2 but y at step 1 produces a grid that is
right in one direction and nonsense in the other, and yields plausible-looking but
wrong measurements. Sample every 2nd row *and* every 2nd column.

**Anchor every row index to the bar, not the window.** The title bar starts at window
row 3 — rows 0-2 are the window frame. Reporting a glyph as "row 16" when it means
window row 16 puts it 3px lower than reality, which looks exactly like a vertical
centring bug and is not one. Compare bar-relative to bar-relative, or subtract 3.

The title bar greys are genuinely **neutral** (R=G=B). The warm tone that reads as
"authentic CDE" in the screenshot comes from the *client* background, which we do not
draw. Do not warm the chrome to match the impression.

## Title bar anatomy

Measured across the Application Manager title bar (`example.png`, grey variant).
Window top-left is image (80, 312); logical px = image px ÷ 2.

Horizontal fields, left to right:

```
| bevel | close field | sep | title field (flexible)      | sep | min field | sep | max field | sep | edge |
   1px      18px       1px                                1px      18px      1px     18px      1px   4px
```

The right pair holds two buttons. The left field holds one — in real Motif that is the
window-menu button; we put close there to keep the 1-left/2-right shape while retaining
all three buttons. To move close to the right, flip the ordering in `buttonRect` — it is
one function.

Vertical bevel order, top-down, for a `BAR_HEIGHT` of **25**:

```
2px  raised light   (outer bevel)
2px  face
1px  dark           ┐ groove
1px  light          ┘
17px face           (fields, glyphs and title live here)
1px  dark           (bottom edge)
```

Separators are a 1px dark line over a 1px light line — that pairing is what makes the
fields read as three panels rather than one bar. Buttons are a bare raised glyph box on
the face; Motif does not draw a button *box* around the title-bar controls.

The right side has **three** separators, not two: title/right-pair, between the pair,
and the outer right edge 4px in from the window edge. Measured from the right, the
separators sit at 4, 24 and 44 logical px. Missing the third one is what makes a first
attempt look visibly wrong on the right-hand end.

Glyphs, measured for a `BAR_HEIGHT` of 25. All three share one centre line, and it is
**not** the centre of the bar — it is the centre of the *field*, the band between the
groove and the bottom edge:

```
bar row   0  1 | 2  3 | 4 | 5 | 6 ......................... 23 | 24
          light  face  D  l  |------ field, centre 15 --------| dark
                                      ^ all glyphs centre here
```

| Glyph | Size | Bar rows | Notes |
|---|---|---|---|
| left (window menu) | 12x4 | 13-16 | we draw close here — a bare raised rect, no X |
| minimize | 4x4 | 13-16 | bare raised rect, no icon — matches the source exactly |
| maximize | 12x12 | 9-20 | bare raised rect, in the rightmost field |

Centring on the bar instead of the field puts every glyph ~3px low, which is very
visible against the reference. Motif draws **no icon inside any of the three**; do not
add one, and in particular do not draw an X — a 12x4 box has no room for one and it
renders as a dark blob.

Minimize is in the field left of maximize. Getting that order backwards is invisible in
the code and obvious on screen.

## Config options

All namespaced `plugin:cde:`, registered with `addConfigValueV2` in `src/config.cpp`.
Set them under `hl.config({ plugin = { cde = { ... } } })` in your config.

| Key | Type | Default |
|---|---|---|
| `bar_height` | int | `25` |
| `color_face` | colour | `#999999` |
| `color_light` | colour | `#d1d1d1` |
| `color_dark` | colour | `#4e4e4e` |
| `color_highlight` | colour | `#ffffff` |
| `show_buttons` | bool | `true` |
| `color_title` | colour | `#ffffff` |
| `title_font_size` | int | `11` |

Two constraints on implementing these:

- `Config::Values::IValue` exposes **no per-value change callback**. Live reload must
  go through `Event::bus()->m_events.config.reloaded` and a generation counter that
  decorations compare against. This is what hyprbars does; there is no way around it.
- `bar_height` feeds `desiredExtents`, so changing it at runtime only takes effect if
  the positioner is re-run — that means `damageEntire()`. **This is untested** and is
  the item most likely to need a second pass.
- `CColorValue::value()` returns a packed integer; `CHyprColor(uint64_t)` consumes it.

## The four things that will waste your day

All four cost hours to find. They are not obvious from the headers.

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

Corollary 2: setting a plugin config value with `hyprctl eval 'hl.config({...})'` does
**not** reliably fire `config.reloaded`, so the bar can keep the old theme even though
`hyprctl getoption` reports the new value. Follow it with `hyprctl reload`.

**4. A second pass element inside an opaque one is silently dropped.** Queueing the bar
and the title as two `CTexPassElement`s draws the bar and nothing else — no error, the
title just never appears. The compositor culls an element whose region is already covered
by an opaque one. Draw the title into the bar's Cairo surface instead; one texture, one
pass element, and exact control over where the text lands.

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

The title bar fidelity increment is done. Remaining, in the order worth doing:

1. **Verify button clicks.** Never exercised — see the hit-testing note in `## Status`.
   This is the single most valuable unknown left.
2. Make `bar_height` changes take effect at runtime (needs `damageEntire()` on reload).
3. Resolve the double-drawn active border, or document the `border_size = 0` workaround.
4. Optional: an active/inactive title colour pair, so the bar reflects focus the way
   CDE's does.

## Non-goals

Deliberately out of scope — do not add these by accident:

- App menu bars. Motif has a menubar, but populating it is app territory, not decoration.
- The status bar strip at the bottom of the client area.
- The bottom Motif dock / launcher. That is a separate compositor surface, not a decoration.
- **The outer frame on the left/right/bottom window edges.** Only the top edge is
  decorated. A full surround means three more decoration instances with their own edge
  and reserved-space math, so it is its own increment — not an oversight.
- Per-button config toggles. `show_buttons` covers what has actually been asked for.