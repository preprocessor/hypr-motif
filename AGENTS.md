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

Renders today, confirmed by screenshot rather than by assumption. **All geometry below
was verified against live pixels** (kitty at 1674x1365 client, 1684px bar), not just
arithmetically:

- Motif-grey title bar on the **measured** palette (`#999999` face, `#d1d1d1` light,
  `#4e4e4e` dark), not the earlier hand-picked greys.
- A **5px** frame on the left, right and bottom edges, as separate reserved decorations.
  It is a three-band bevel — `light, face, dark` — not a two-tone rule. Left is
  `light(2) face(2) dark(1)`; right and bottom are `light(1) face(2) dark(2)`. The
  client genuinely shrinks inward; it is not an overlay.
- The frame runs the **full** window height, alongside the title bar as well as below it.
  The side strips are not "below the bar" — measured down the window edge, the bands are
  continuous from the top.
- The title bar spans the **full** window width, frame bands included. All four corners
  are seamless.
- **Mitred corners on all four edges**, not overlapping rectangles — see `## Corners`.
  The bottom pair comes from CDE-7.png, the top pair from new-standard.png; both verified
  pixel-for-pixel against a live render.
- **Corner grab areas**: a 21px square at each corner delimited by separator lines, on
  all four corners.
- Fielded layout: close button left, flexible title field, min/max right, each field
  delimited by a 1px dark-over-light separator. Five separators total: rel 4, 21, and
  `width-40`, `width-23`, `width-6`.
- Title drawn **into the bar texture** with Cairo's toy text API, in the palette's title
  colour, centred **within the title field**. A second pass element does not work — see
  gotcha 4.
- **Focused windows use a separate palette** (tan by default); unfocused windows use the
  un-suffixed colours. `isActive()` is *polled* from `ensureTextures` rather than hooked
  to `window.active`, because that event names only the newly-focused window — an event
  handler would have to track and invalidate the previous one by hand.
- The bar reserves its configured height (22px), so the client shrinks beneath it.

Known gaps, in the order they bite:

- **Button hit-testing is unverified.** No way to inject synthetic input here, and
  `onInputOnDeco`'s `pos` is assumed **decoration-local**. If it is actually global,
  nothing will ever register a hit. Check this first — `barDeco.cpp:211` subtracts
  `assignedBoxGlobal().pos()` from raw mouse coords, which hints the raw form is global.
- **No resize handles on the frame edges.** The left/right/bottom decorations swallow no
  input and do nothing; CDE has resize grips in the corners.
- **`bar_height` / `border_size` changes at runtime are untested.** They feed
  `desiredExtents`, so they only take effect once the positioner re-runs.
- **The active border still double-draws** — see `## Known unknown`.

## Measured reference values

**`examples/CDE-7.png` is the authoritative reference.** It is 1013x765 at **1:1** with
no scaling to undo, so every pixel is a real CDE pixel.

`examples/example.png` is the older 2x-scaled capture. It **disagrees** with CDE-7 on two
things, and where they conflict, CDE-7 wins: title bar height (25 vs 22) and the
right-hand field layout (three separators at 43/23/3 from the right edge vs two). Do not
re-derive geometry from example.png.

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

**Extract the grid with step 2 on BOTH axes — but only for `example.png`.** That PNG is
2x-scaled, so one logical pixel is 2x2 image pixels. Sampling x at step 2 but y at step 1
produces a grid that is right in one direction and nonsense in the other, and yields
plausible-looking but wrong measurements. Sample every 2nd row *and* every 2nd column.
`CDE-7.png` needs no such handling — it is 1:1.

**Anchor every row index to the bar, not the window.** In `example.png` the title bar
starts at window row 3 — rows 0-2 are the window frame. Reporting a glyph as "row 16"
when it means window row 16 puts it 3px lower than reality, which looks exactly like a
vertical centring bug and is not one. Compare bar-relative to bar-relative.

The title bar greys are genuinely **neutral** (R=G=B). The warm tone that reads as
"authentic CDE" in the screenshot comes from the *client* background, which we do not
draw. Do not warm the chrome to match the impression.

## Corners

CDE mitres the corners — the two bevels wrap and step, they do not overlap. An earlier
revision drew the side strips full-height and the bottom strip full-width and let them
overlap, which is wrong in the 5x5px corner.

**Two references.** `examples/CDE-7.png` supplies the bottom pair and the 22px bar;
`examples/new-standard.png` supplies the top pair. The latter is a *different* Motif
dialect — tan palette (`#f7d9c0`/`#eda870`/`#7d593b`), a 25px bar and a menu bar. We take
its **corner geometry only** and deliberately do not adopt the palette, the 25px bar or
the menu bar (a non-goal below). Note it is **Adam7 interlaced** (`interlace=1`), which
the ad-hoc PNG reader in this repo silently mangles into garbage — de-interlace before
measuring.

Measured 5x5, bottom-left corner at (x48, y641), rows inner to outer:

```
LLFFD      <- the bottom frame's light row; the outer two columns stay light
LLFFF
LLFFF
LLDDD
LDDDD
```

Bottom-right is the mirror and is **not** a mirror image — measure it, do not derive it:

```
LFFDD
FFFDD
FFFDD
DDDDD
DDDDD
```

Top pair from `new-standard.png` (corner at x0,y0 and x756,y0). Column 0 is the
**outermost**:

```
TL            TR
LLLLL         LLLLL
LLLLL         LLLLD
LLFFF         FFFDD
LLFFF         FFFDD
LLFFD         DFFDD
```

TL/TR rows 0-1 are light across, which is why the bar's bevel appears to span the full
window width — it is the mitre, not the bar overdrawing the frame. Normal bands resume
at row 5, below the mitre. All four are encoded as literal patterns in `chrome.cpp`.

**Ownership, which is what stops them fighting:** the bottom strip is drawn last, so it
paints both bottom corners itself. The side strips run the full window height, so they
paint the top pair — and because they draw after the bar, their mitre is what lands on
screen in the bar's top rows. No reordering is needed.

### Corner grab areas

A 21px square at each corner, marked off by a `dark(1) + light(1)` rule. All **four**
corners have both rules; the top ones were added after the bottom pair was already
correct, so do not assume "the corners" means the bottom two.

| rule | position | spans |
|---|---|---|
| vertical, left | bar-rel 21 (dark), 22 (light) | bar-rel **1..21**, broken at row 5 |
| vertical, right | bar-rel `width-23` (dark), `width-23+1` (light) | same |
| horizontal, top | bar-rel 21 (dark row), 22 (light row) | full frame width |
| horizontal, bottom | 21px up from the bottom edge | full frame width |

The vertical rule reaches **bar-rel 1..21**, not just the field rows: unlike the field
separators, which start below the bevel at row 6, the grab rule is drawn separately so it
can start one row under the top bevel. It lands on the same columns as the left-field and
mid-right separators, so it must be drawn as its own pass rather than by widening those.

It is **broken at row 5**, where the groove's light row wins — visible as a 1px gap. The
reference does the same, so reproduce it rather than "fixing" it.

The horizontal rule's two rows are asymmetric between the sides, and that is deliberate:

```
left  frame:  LDDDD   then  LLLLL
right frame:  LDDDD   then  LLLLD
```

The lit pixel sits at local x=0 on both, which is the *outer* edge on the left strip and
the *inner* edge on the right. On a dark-outer strip the outermost pixel of the second row
stays dark.

`CORNER_GRAB` is a fixed 21, measured from one window. The only other window in CDE-7.png
is cut off at the image edge, so there is nothing to confirm a formula against — do not
"derive" it from `BORDER` without a second data point.

`new-standard.png` **does** give the second data point, and it agrees on the relation if
not the absolute value: its grab is 24 with a 25px bar, CDE-7's is 21 with a 22px bar.
Both are `BAR_HEIGHT - 1`. We keep 21 because we keep the 22px bar; if the bar height
changes, the grab follows it, and nothing else about the corner changes.

## Title bar anatomy

Measured across the Application Manager title bar in `examples/CDE-7.png`. That window is
551px wide at 1:1, so bar-relative offsets are real pixels — no division.

Horizontal fields, left to right:

```
| bevel | close field | sep | title field (flexible)      | sep | min field | sep | max field | sep | edge |
   1px      15px       1px                                1px      15px      1px     15px      1px   4px
```

The right pair holds two buttons. The left field holds one — in real Motif that is the
window-menu button; we put close there to keep the 1-left/2-right shape while retaining
all three buttons. To move close to the right, flip the ordering in `buttonRect` — it is
one function.

Vertical bevel order, top-down, for a `BAR_HEIGHT` of **22**:

```
2px  raised light   (outer bevel)
2px  face
1px  dark           ┐ groove
1px  light          ┘
15px face           (fields, glyphs and title live here)
1px  dark           (bottom edge)
```

Separators are a 1px dark line over a 1px light line — that pairing is what makes the
fields read as three panels rather than one bar. Buttons are a bare raised glyph box on
the face; Motif does not draw a button *box* around the title-bar controls.

The right side has **three** separators, not two: title/min, between the pair, and the
outer right edge. They sit at `width-40`, `width-23` and `width-6` bar-relative, which
at the reference width of 551 is 511, 528 and 545. Missing the third is what makes a
first attempt look visibly wrong on the right-hand end.

There is also a separator on the **left** at bar-rel 4, before the close field — the field
is inset within the bar rather than butting against the frame bevel. Easy to miss; the
live render confirms all five.

Glyphs, measured for a `BAR_HEIGHT` of 22. All three share one centre line, and it is
**not** the centre of the bar — it is the centre of the *field*, the band between the
groove and the bottom edge:

```
bar row   0  1 | 2  3 | 4 | 5 | 6 ......................... 20 | 21
          light  face  D  l  |------ field, centre 13.5 -----| dark
                                       ^ all glyphs centre here
```

| Glyph | Size | Bar rows | Notes |
|---|---|---|---|
| left (window menu) | 9x4 | 11-14 | we draw close here — a bare raised rect, no X |
| minimize | 4x4 | 11-14 | bare raised rect, no icon — matches the source exactly |
| maximize | 9x9 | 9-17 | bare raised rect, in the rightmost field |

The field centre is `x.5` because the field is an odd height. **Floor both axes of the
glyph origin.** The field's own centre is half a pixel on y, and `FIELD_W - w` is odd
for the 4px minimize glyph, so its x lands on `.5` too. Hand Cairo a fractional origin
and it anti-aliases across two rows/columns instead of hitting the reference cells —
visible as pale half-pixels down one edge of the minimize glyph, and invisible until you
look for it. Only minimize is affected on x; close and max both divide evenly.

Centring on the bar instead of the field puts every glyph ~2px high, which is very
visible against the reference. Motif draws **no icon inside any of the three**; do not
add one, and in particular do not draw an X — a 9x4 box has no room for one and it
renders as a dark blob.

Minimize is in the field left of maximize. Getting that order backwards is invisible in
the code and obvious on screen.

## Config options

All namespaced `plugin:cde:`, registered with `addConfigValueV2` in `src/config.cpp`.
Set them under `hl.config({ plugin = { cde = { ... } } })` in your config.

| Key | Type | Default |
|---|---|---|
| `bar_height` | int | `22` |
| `color_face` | colour | `#999999` |
| `color_light` | colour | `#d1d1d1` |
| `color_dark` | colour | `#4e4e4e` |
| `color_highlight` | colour | `#ffffff` |
| `show_buttons` | bool | `true` |
| `color_title` | colour | `#ffffff` |
| `title_font_size` | int | `11` |
| `color_face_active` | colour | `#eda870` |
| `color_light_active` | colour | `#f7d9c0` |
| `color_dark_active` | colour | `#7d593b` |
| `color_title_active` | colour | `#000000` |
| `shadow_enabled` | bool | `true` |
| `shadow_range` | int | `10` |
| `shadow_color` | colour | `#000000` |
| `shadow_color2` | colour | `#000000` |
| `shadow_opacity1` | float | `0.5` |
| `shadow_opacity2` | float | `0.75` |

The un-suffixed colours apply to **unfocused** windows; the `_active` ones to the focused
window. The active face/light/dark are measured from `examples/new-standard.png`.
`color_title_active` defaults to black rather than the reference's `#ffffff`, because
white on `#eda870` is about 2.9:1 — set it to `#ffffff` for reference fidelity.

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

**2. Position every edge from `getWindowDecorationBox(this)`, not from
`assignedGeometry` or `logicalBox()`.** `assignedGeometry` is relative to the edge's
defined point and the positioner anchors each decoration to the *outer* window, so
translating by the edge point double-offsets by the bar height. `logicalBox()` is worse
still: it does not report the window box at all (it returned `(12,-1,1708,1464)` for a
window Hyprland reported at `[31,52] 1670x1357`).

Do **not** reach for `getBoxWithIncludedDecos()` either — it already accounts for the
space we ourselves reserved, so slicing it per edge is a feedback loop that drifts by
the bar height. The bar must span the full `outer.w` or it is inset by the frame width
on each side, leaving a wallpaper gap in both top corners.

**Corollary: `own` is a DIFFERENT rectangle for each edge, so never derive all edges
from one decoration's box.** For the top, `getWindowDecorationBox(this)` is the client
box; for the left it already starts at the window's outer edge. Re-expanding a uniform
`own.x - BORDER` therefore shifts the side frames a whole border further out than the
client and leaves **5px of bare wallpaper between frame and client on both sides**. Take
the outer span from the sibling decorations instead — `outerBox()` in `CCdeDecoration.cpp`
reads `CDE:left` / `CDE:right` / `CDE` / `CDE:bottom` and lets the compositor's own
placement define the ring. `draw()`, `retexture()` and hit-testing all call it, because a
texture rastered at one size and stretched over another puts the separators out of
register with the buttons.

**3. Rebuilding over the same path does not reload.** `hyprctl plugin unload` + `load`
on a rebuilt `build/libcde.so` silently keeps running the **old** code — the compositor
holds the previous, now-deleted inode, and `hyprctl plugin list` shows stale metadata
from the old build. Build, then `cp build/libcde.so build/libcde-<new>.so` and load the
*new* path. Check `grep -o "libcde[a-z0-9.-]*\.so" /proc/$(pgrep -f "bin/Hyprland" | head -1)/maps`
— it shows `(deleted)` when you have been fooled, and more than one `libcde-*.so` means
you have two copies fighting over the same decorations.

Corollary: **`hyprctl plugin unload` only works by path.** Unloading by handle (or
`-1`, or a comma list) prints `plugin not loaded` and is a **no-op**. The handle keeps
appearing in `hyprctl plugin list` and the old copy keeps drawing. Use
`hyprctl plugin unload /full/path/libcde-<old>.so` and confirm with
`hyprctl plugin list | grep -c "Plugin cde"` returning 1.

Corollary: **`pgrep -x Hyprland` returns nothing even when Hyprland is running.** The
process is `/etc/profiles/per-user/wyspr/bin/Hyprland`, which does not match the exact
name `-x`. Use `pgrep -f "bin/Hyprland"`. Believing the `-x` result leads to concluding
the compositor is dead and restarting a healthy session.

Corollary: `printf` from a plugin goes nowhere when Hyprland was started by a greeter,
because its stdout is a socket. To debug, append to a file under `$HOME`.

Corollary 2: setting a plugin config value with `hyprctl eval 'hl.config({...})'` does
**not** reliably fire `config.reloaded`, so the bar can keep the old theme even though
`hyprctl getoption` reports the new value. Follow it with `hyprctl reload`.

Corollary 3: **never `rm -rf "$XDG_RUNTIME_DIR/hypr"`**, not even after
`pgrep` suggests it is dead. That directory holds the live instance's `socket.sock` and
`socket2.sock`; deleting them leaves a perfectly healthy compositor that `hyprctl` can no
longer reach, and there is no way to reattach. Only a compositor restart recovers.

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

## Next increment (planned)

Part 1 is now implemented — see `## Status`. Part 2 is not.

**Part 1 — four reserved edges.** The left/right/bottom frame, not an overlay: each
edge gets its own `IHyprWindowDecoration` with `reserved = true`, so the client shrinks
inward and the frame is real. Do not switch to `DECORATION_POSITION_ABSOLUTE`; it is
simpler but clips the client by the border width.

`CCdeDecoration` is parameterised by edge rather than duplicated. Confirmed safe: four
`DECORATION_CUSTOM` instances per window, because `getDecorationByType` is declared but
never called internally and `DECORATION_CUSTOM` has no special-casing in the headers.

- `renderBorder(edge, size, scale)` draws the 5px frame as a **three-band bevel**: a lit
  band, the grey face, then a shadowed band. Left is `light(2) face(2) dark(1)`; right
  and bottom are `light(1) face(2) dark(2)` — the lit band swaps to the inner side. Both
  this and the title bar's own outer edges go through the single `bevelBands()` painter in
  `chrome.cpp`, because the bar spans the full window width and the side strips the full
  window height, so **bar and frame overlap in the corners**; if each drew its own bevel
  the join would show. `outerIsHigh` selects which end faces away from the client.
- **Watch the fill() argument order in `bevelBands`.** The horizontal branch is
  `fill(x, y, len, width)` and the vertical branch is `fill(x, y, width, len)`. Swapping
  them draws the bottom frame's lit band as a 1px *column*, which silently drops the inner
  highlight and leaves `face(2) dark(2)`.
- `desiredExtents` per edge. `SBoxExtents.topLeft` is `{left, top}` and `bottomRight`
  is `{right, bottom}`: TOP `{0, barHeight}`, LEFT `{borderSize, 0}`,
  RIGHT `bottomRight = {borderSize, 0}`, BOTTOM `bottomRight = {0, borderSize}`.
- `draw()` must call `getEdgeDefinedPoint(m_edge, ...)`, not the hardcoded TOP.
- Display names are `CDE` for the bar and `CDE:left/right/bottom` for the frame; the
  dedupe check keys off them so `onNewWindow` stays idempotent.

**Part 2 — rule-settable colours.** Register four effects in `pluginInit`, unregister in
`PLUGIN_EXIT` (hyprbars' pattern):

```cpp
Desktop::Rule::windowEffects()->registerEffect("cde:face");  // + light, dark, title
```

`configFor(PHLWINDOW)` reads the global config then overrides any colour present in
`window->m_ruleApplicator->m_otherProps.props`. Precedence: **rule wins over config**,
one colour at a time, so a rule may set `cde:face` alone and inherit the rest.

```lua
windowrulev2 = float, title:^(kitty)$, cde:face rgb(ff00aa55)
```

Also needs `Event::bus()->m_events.window.updateRules` to invalidate that window, or a
rule change will not redraw until something else does.

**API drift to watch:** `Config::ParserUtils::parseColor` returns
`std::expected<int64_t, std::string>` in 0.56.2, not `std::optional`. hyprbars' `.value_or(0)`
will not compile against it — check `has_value()` instead.

## Roadmap

The title bar fidelity increment and the frame are both **done and verified against live
pixels**. Remaining, in order:

1. **Rule-settable colours** (Part 2 above). Cheap and self-contained — do it next.
2. **Verify button clicks.** Never exercised — see the hit-testing note in `## Status`.
   This is the single most valuable unknown left.
3. Make `bar_height` / `border_size` changes take effect at runtime (needs
   `damageEntire()` on reload).
4. Resolve the double-drawn active border, or document the `border_size = 0` workaround.
5. Optional: an active/inactive title colour pair, so the bar reflects focus the way
   CDE's does.

## Non-goals

Deliberately out of scope — do not add these by accident:

- App menu bars. Motif has a menubar, but populating it is app territory, not decoration.
- The status bar strip at the bottom of the client area.
- The bottom Motif dock / launcher. That is a separate compositor surface, not a decoration.
- Per-button config toggles. `show_buttons` covers what has actually been asked for.
## Active-window palette

The focused window paints with `*_active` colours, everything else with the un-suffixed
ones. Both sets are config values, so neither is hardcoded.

**Focus is polled, not subscribed.** `ensureTextures` compares
`m_lastFocused == isActive()` alongside the other cache keys, and `draw()` calls it every
frame, so a focus change re-rasterises on the next frame. The alternative —
`Event::bus()->m_events.window.active` — only names the newly focused window, not the one
losing focus, so a handler would have to remember the previous window and invalidate all
four of its decorations by hand. Polling is a pointer compare and cannot miss an event.

**`isWindowActive()` is not usable here.** It takes `PHLWINDOW` (a shared pointer) while
`m_window` is a `PHLWINDOWREF` (weak), and weak does not convert to shared — neither
implicitly nor by constructing from `.get()`, because `CSharedPointer` has no raw-pointer
constructor. `isActive()` compares the raw pointers against
`Desktop::focusState()->window()` instead, which avoids building a shared pointer to ask a
yes/no question.

`m_lastFocused` is an **int** initialised to `-1`, not a bool. A bool defaulting to false
would read as a valid cache hit on the first frame of an unfocused window.

## Shadow

We draw our own shadow rather than using Hyprland's. **Leave `decoration:shadow:enabled`
off** — with both on you get two shadows.

`CHyprDropShadowDecoration` derives its box from the window geometry and gets it wrong
once a plugin reserves space. Measured, with everything else identical:

| below the window | shadow extent |
|---|---|
| plugin unloaded | 8px (correct) |
| plugin loaded | **46px** |

The top and left edges stayed correct at 7-8px, so it is specific to the bottom edge. It is
**not** our decoration `priority` (measured unchanged at `priority = 0`) and **not**
`bar_height`. The exact mechanism is unknown: `getPositioningInfo()` is declared in
`CHyprDropShadowDecoration.hpp` with no body — nixpkgs ships headers only.

Because we already know the outer box exactly, drawing it ourselves is correct by
construction and cannot fight the positioner. The shadow is **not** a gradient — it is an
**ordered dither** with an 8×8 Bayer threshold matrix (`renderDitherShadow()` in
`chrome.cpp`).

### Dithered shadows

Only the RIGHT and BOTTOM edges cast a shadow (the two outer edges of the window ring).
Each builds one texture, blitted via `CTexPassElement` *before* the frame so the frame
paints over any bleed:

- Per-pixel distance field `d`: the flat strip uses the thin-axis coordinate; the two
  corner cells use `hypot()` so the shadow rounds off the window corner.
- Density is `1 - d/range`, so it is densest against the window and reaches zero at
  `shadow_range` px out.
- Threshold is indexed so the **distance axis always drives the Bayer row**: bottom uses
  `(BAYER8[y % 8][x % 8] + 0.5) / 64.0`, right uses `(BAYER8[x % 8][y % 8] + 0.5) / 64.0`.
  A pixel is painted when `density > threshold`.
- **The right edge's index is transposed, and that is load-bearing.** `BAYER8` is not
  transpose-symmetric (it is only 180°-rotation symmetric), so row k and column k
  distribute their values differently. With the naive `BAYER8[y % 8][x % 8]` on both
  edges, the bottom's fade walks the rows while the right's walks the columns, and the two
  edges fade at different rates at the same offset — measured 75% dots on the bottom vs
  50% on the right at 4px out (density 0.6), which reads as a lopsided shadow. Transposing
  the right edge's index makes both edges sample row `d % 8` at distance `d`, so the
  density ramp and rhythm match exactly. Verified against grim: same-distance scans on
  both edges now produce identical on-counts.
- The 8×8 Bayer pattern tiles every 8 pixels, which can read as a visible grid at the
  default 10px range on 1× scale.
- **Two tones off one cell.** The dither is not binary: `density > t` paints the strong
  tone, otherwise `2·density > t` paints the soft tone. Both thresholds read the SAME
  Bayer cell, so the soft band is the exact complement of the strong one — no second
  matrix, and the transpose rule above holds for both decisions unchanged. This gives the
  ramp a tone step (dot → dot2 → nothing) instead of falling straight to zero, which reads
  as smoother. Measured on the bottom edge at density 0.4: 50% strong + 50% soft + 0%
  bare, and the right edge produces identical counts at every distance.
- **`shadow_opacity1` / `shadow_opacity2` are the single source of truth for the two
  tones' alphas** (0.5 / 0.75 by default); the `shadow_color` / `shadow_color2` configs
  contribute RGB only, their own alpha is ignored. `shadow_opacity2 = 0` collapses the
  ramp back to single-tone behaviour.
- Config: `plugin:cde:shadow_enabled` (bool), `plugin:cde:shadow_range` (1–64 px),
  `plugin:cde:shadow_color` / `shadow_color2` (RGB of strong / soft tone),
  `plugin:cde:shadow_opacity1` / `shadow_opacity2` (float 0–1, tone alphas).

**A note on what looks like a bug in a pixel scan.** At density 0.5 the 8×8 Bayer can
produce long runs (e.g. `SSSS....SSS.SSSS` horizontally, `....SSSSSSSS....` vertically),
because a single row/column of an 8×8 Bayer is not itself well-distributed. That is
correct — the *matrix* distributes dots well across the full 8×8 area, but a slice
is not. Do not "fix" it by randomising per-row.

Untested: whether the shadow smears when a window moves, since the shadow sits outside the
window's damage region — stale pixels do linger after a drag (observed at a corner after
moving a window). Nothing can be dispatched in this environment to move a window
deliberately; the drag was incidental.
