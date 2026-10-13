# AGENTS.md

## Project

A Hyprland plugin that renders Common Desktop Environment (Motif) window chrome.

**Scope:** the title bar and its frame — the beveled border, the fields the bar is divided
into, and the three buttons. Drawn **procedurally with Cairo** at runtime; there are no
image assets, and there should not be. The chrome is simple 3D bevels; drawing it beats
slicing pixels out of `example.png` (itself 2x-scaled and shadowed).

## Commands

```sh
nix develop                                          # gcc 14.4, cmake 4.4.3, hyprland 0.56.2 headers
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
cp build/libcde.so build/libcde-next.so              # fresh path — see gotcha 3
hyprctl plugin unload ~/.local/share/hyprland/plugins/libcde-old.so
hyprctl plugin load ~/.local/share/hyprland/plugins/libcde-next.so
hyprctl plugin list | grep -c "Plugin cde"            # must be 1
```

Build to a **new filename** every time — never over the one you are loading. The
compositor keeps the old inode mapped and silently runs stale code.

`flake.nix` pins `nixpkgs-unstable`, yielding **hyprland 0.56.2**, `aquamarine 0.15.1`,
`cairo 1.18.4**. Treat those versions as exact.

## ABI constraint — read before touching the build

The compositor `dlopen`s the plugin and passes live C++ objects across the boundary.
**There is no ABI stability.** A plugin built against a different hyprland than the one
running is ejected, or crashes the compositor outright. It will not fail gracefully.

`flake.nix` forces `gcc14Stdenv` for exactly this reason — do not "modernise" it to the
default stdenv. If you bump `flake.lock`, rebuild before touching your running session.

## Layout

```
src/main.cpp           plugin entry: pluginAPIVersion / pluginInit / pluginExit
src/CCdeDecoration.hpp IHyprWindowDecoration subclass — declaration
src/CCdeDecoration.cpp positioning, draw(), input handling
src/config.hpp         config struct, generation counter
src/config.cpp         plugin:cde:* registration + live read
src/chrome.hpp         layout constants, button rects, Cairo entry points
src/chrome.cpp         Cairo drawing of the bevels, bar, corners and glyphs
```

**Build wiring — do not hand-roll include directories.** `hyprland.pc` already exists and
pulls cairo transitively, so CMake needs exactly:

```cmake
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
target_link_libraries(cde PRIVATE PkgConfig::HYPRLAND)
```

## Architecture

One `IHyprWindowDecoration` per window **per edge** — four: `CDE` (bar), `CDE:left`,
`CDE:right`, `CDE:bottom`. Added from `windowopen` via `HyprlandAPI::addWindowDecoration`.
Four instances are safe: `getDecorationByType` is declared but never called internally, and
`DECORATION_CUSTOM` has no special-casing. Display names are what the dedupe check keys
off, so `onNewWindow` stays idempotent.

Positioning is `DECORATION_POSITION_STICKY` with `reserved = true`, so every band takes
**real space** and the client shrinks inward — the frame is not an overlay. Do not switch
to `DECORATION_POSITION_ABSOLUTE`: it is simpler but clips the client by the border width.

`desiredExtents`: `SBoxExtents.topLeft` is `{left, top}` and `bottomRight` is
`{right, bottom}` — TOP `{0, barHeight}`, LEFT `{borderSize, 0}`, RIGHT
`bottomRight = {borderSize, 0}`, BOTTOM `bottomRight = {0, borderSize}`.

`draw(PHLMONITOR, float const& alpha)` runs once per frame per decoration. See gotcha 1 —
it cannot touch the framebuffer.

## Status

Verified against live pixels, not arithmetic. Renders today:

- Motif-grey bar on the **measured** palette, not hand-picked greys.
- **5px frame** on left/right/bottom as separate reserved decorations, as a three-band
  bevel (`light, face, dark`): left `light(2) face(2) dark(1)`, right/bottom
  `light(1) face(2) dark(2)`. The frame runs the **full** window height — continuous from
  the top, alongside the bar as well as below it.
- Bar spans the **full** window width, frame bands included; all four corners seamless.
- **Mitred corners** on all four edges (`## Corners`), plus a **21px corner grab square**
  marked off by separator rules on all four corners.
- Fielded layout, five separators at bar-rel 4, 21, `width-40`, `width-23`, `width-6`.
- Title drawn **into the bar texture** (gotcha 4), centred within the title field.
- Focused windows use a separate palette — see `## Active-window palette`.

Gaps, in the order they bite:

- **Button hit-testing is unverified.** No way to inject synthetic input here, and
  `onInputOnDeco`'s `pos` is assumed **decoration-local**. If it is global, nothing ever
  registers a hit. Check this first: bar decorations in Hyprland subtract
  `assignedBoxGlobal().pos()` from raw coords, which hints the raw form is global.
- **No resize handles on the frame edges.** The left/right/bottom decorations swallow no
  input and do nothing; CDE has resize grips in the corners.
- **`bar_height` changes at runtime are untested** — it feeds `desiredExtents`, so it only
  takes effect once the positioner re-runs (`damageEntire()`). Most likely to need a
  second pass.
- The active border still double-draws — see `## Known unknown`.
- The float-toggle icon on the minimize button **still dispatches `minimize`**. The glyph
  and the action disagree; wiring it to `togglefloating` has been offered, not done.

## References, and how to measure them

**`examples/CDE-7.png` is authoritative.** 1013x765 at **1:1** — every pixel is a real CDE
pixel.

`examples/example.png` is an older 2x-scaled capture that **disagrees** with CDE-7 on
title bar height (25 vs 22) and the right-hand field layout. Where they conflict, CDE-7
wins. Do not re-derive geometry from example.png.

`examples/new-standard.png` is a **different Motif dialect** (tan palette, 25px bar, menu
bar). We take its **corner geometry and active palette only**. Note it is **Adam7
interlaced** (`interlace=1`), which the ad-hoc PNG reader in this repo mangles into
garbage — de-interlace before measuring.

`examples/float-toggle.png` is user-supplied and **outranks CDE-7 for the minimize
button**. It has been revised twice; re-measure rather than trusting this file.

| Element | Hex | Notes |
|---|---|---|
| Title bar face | `#999999` | |
| Bevel light | `#d1d1d1` | |
| Bevel dark | `#4e4e4e` | |
| Title text | `#ffffff` | white on the face, **not** dark |
| Window frame | `#76818f` | the grey edge above the bar; not ours |
| Menu bar / client bg | `#5c90a4` / `#a59990` | n/a — not ours to draw |

**There is no white highlight band.** An earlier revision of this file claimed one,
measured from a column scan that happened to land on the title glyphs. The only `#ffffff`
in the bar is the title text.

**Sample `example.png` with step 2 on BOTH axes.** It is 2x-scaled, so one logical pixel is
2x2 image pixels. Stepping x by 2 but y by 1 yields a grid that is right in one direction
and nonsense in the other, and therefore plausible-looking but wrong measurements.
`CDE-7.png` needs no such handling.

**Anchor every row index to the bar, not the window.** In `example.png` the bar starts at
window row 3 — rows 0-2 are the frame. Reporting a glyph as "row 16" when it means *bar*
row 16 puts it 3px low, which looks exactly like a vertical centring bug and is not one.

The bar greys are genuinely **neutral** (R=G=B). The warm tone that reads as "authentic
CDE" comes from the *client* background, which we do not draw. Do not warm the chrome.

## Corners

CDE mitres the corners — the two bevels wrap and step, they do not overlap. Overlapping
rectangles are wrong in the 5x5 corner.

Measured 5x5, rows inner to outer. `L` light, `F` face, `D` dark. Bottom pair from
CDE-7.png (BL at x48,y641); top pair from new-standard.png (TL at x0,y0, TR at x756,y0).
Top patterns read from the **outermost** column inward.

```
BL            BR            TL            TR
LLFFD         LFFDD         LLLLL         LLLLL
LLFFF         FFFDD         LLLLL         LLLLD
LLFFF         FFFDD         LLFFF         FFFDD
LLDDD         DDDDD         LLFFF         FFFDD
LDDDD         DDDDD         LLFFD         DFFDD
```

Bottom-right is **not** a mirror of bottom-left — measure it, do not derive it. TL/TR rows
0-1 are light across, which is why the bar's bevel appears to span the full window width:
it is the mitre, not the bar overdrawing the frame. All four are literal patterns in
`chrome.cpp` (`drawCorner`), the same idiom as the float glyph.

**Ownership, which is what stops them fighting:** the bottom strip is drawn last so it
paints both bottom corners itself; the side strips run the full window height so they paint
the top pair, and because they draw after the bar, their mitre is what lands on screen in
the bar's top rows. No reordering needed.

### Corner grab areas

A 21px square at each corner, marked by a `dark(1) + light(1)` rule. All **four** corners
have both rules — the top ones were added after the bottom pair was already correct, so do
not read "the corners" as "the bottom two".

| rule | position | spans |
|---|---|---|
| vertical, left | bar-rel 21 (dark), 22 (light) | bar-rel **1..21**, broken at row 5 |
| vertical, right | bar-rel `width-23`, `width-23+1` | same |
| horizontal, top | bar-rel 21, 22 | full frame width |
| horizontal, bottom | 21px up from the bottom edge | full frame width |

The vertical rule reaches **bar-rel 1..21**, unlike the field separators which start below
the bevel at row 6 — it is drawn as its own pass for that reason, and because it lands on
the same columns as two field separators, so widening those would not do.

It is **broken at row 5**, where the groove's light row wins — a 1px gap. The reference
does the same; reproduce it, do not "fix" it.

The horizontal rule's two rows are deliberately asymmetric:

```
left  frame:  LDDDD   then  LLLLL
right frame:  LDDDD   then  LLLLD
```

The lit pixel sits at local x=0 on both — the *outer* edge on the left strip, the *inner*
one on the right, so on a dark-outer strip the outermost pixel of row two stays dark.

`CORNER_GRAB` is a fixed 21, measured from one window. The only other window in CDE-7.png
is cut off at the image edge. Do not "derive" it from `BORDER` without a second data point
— though new-standard.png gives one that agrees on the relation: 24 with a 25px bar, i.e.
`BAR_HEIGHT - 1`. We keep 21 because we keep the 22px bar; if the bar height changes the
grab follows it and nothing else about the corner changes.

## Title bar anatomy

Measured across the Application Manager bar in `examples/CDE-7.png` — 551px wide at 1:1,
so bar-relative offsets are real pixels.

```
| bevel | close field | sep | title field (flexible)      | sep | min field | sep | max field | sep | edge |
   1px      15px       1px                                1px      15px      1px     15px      1px   4px
```

The right side has **three** separators, not two: title/min, between the pair, and the
outer right edge, at `width-40`, `width-23`, `width-6`. Missing the third is what makes a
first attempt look visibly wrong on the right-hand end. There is also one on the **left**
at bar-rel 4 — the field is inset within the bar rather than butting the frame bevel.

The left field holds one button. In real Motif that is the window-menu button; we put
close there to keep the 1-left/2-right shape while keeping all three buttons. To move
close to the right, flip the ordering in `buttonRect` — it is one function.

Vertical bevel order, top-down, for `BAR_HEIGHT` 22:

```
2px  raised light   (outer bevel)
2px  face
1px  dark           ┐ groove
1px  light          ┘
15px face           (fields, glyphs and title live here)
1px  dark           (bottom edge)
```

Separators are 1px dark over 1px light — that pairing is what makes the fields read as
three panels rather than one bar. Buttons are bare raised glyph boxes on the face; Motif
draws no button *box* around the title-bar controls.

**The frame and the bar share one bevel painter** (`bevelBands`), because the bar spans the
full window width and the side strips the full height, so they **overlap in the corners** —
two separate bevels would show a join. `outerIsHigh` says which end faces away from the
client, which is what makes the lit band swap sides between left and right/bottom.
**Watch the `fill()` argument order**: the horizontal branch is `fill(x, y, len, width)`
and the vertical branch is `fill(x, y, width, len)`. Swapped, the bottom frame's lit band
becomes a 1px *column* and the inner highlight vanishes.

### Glyphs

All three share one centre line, and it is **not** the centre of the bar — it is the
centre of the *field*, the band between the groove and the bottom edge:

```
bar row   0  1 | 2  3 | 4 | 5 | 6 ......................... 20 | 21
          light  face  D  l  |------ field, centre 13.5 -----| dark
                                       ^ all glyphs centre here
```

| Glyph | Size | Bar rows | Notes |
|---|---|---|---|
| left (window menu) | 9x4 | 11-14 | we draw close here — a bare raised rect, no X |
| minimize | 9x9 | 9-17 | **float-toggle icon**, user-supplied, replaces CDE-7's 4x4 rect |
| maximize | 11x11 | 8-18 | bare raised rect; 2px larger than the 9x9 reference, still centred |

Centring on the bar instead of the field puts every glyph ~2px high, very visible against
the reference. **Floor both axes of the glyph origin** — the field centre is half a pixel
on y, and a fractional origin makes Cairo anti-alias across two rows/columns instead of
hitting the reference cells, showing as pale half-pixels down one edge.

Motif draws **no icon inside any of the three**; do not add one, and in particular do not
draw an X — a 9x4 box has no room for one and it renders as a dark blob.
**Exception, deliberate:** the minimize button carries the float-toggle icon, the literal
`FLOAT_TOGGLE` pattern in `chrome.cpp`. Two overlapping 6x6 outlined squares (4x4 hollow),
back one offset **three** up-right, front interior opaque (`F`) so it occludes the back
square's bottom-left corner. Light top/left, dark bottom/right; pressed inverts the bevel
like `glyphFrame` does.

Minimize is in the field left of maximize. Getting that order backwards is invisible in
the code and obvious on screen.

## Config options

All namespaced `plugin:cde:`, registered with `addConfigValueV2` in `src/config.cpp`, set
under `hl.config({ plugin = { cde = { ... } } })`.

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

Un-suffixed colours apply to **unfocused** windows, `_active` to the focused one. The
active face/light/dark are measured from new-standard.png. `color_title_active` defaults
to black rather than the reference's `#ffffff`, because white on `#eda870` is ~2.9:1 —
set it to `#ffffff` for reference fidelity.

Two constraints on implementing these:

- `Config::Values::IValue` exposes **no per-value change callback**. Live reload must go
  through `Event::bus()->m_events.config.reloaded` plus the generation counter that
  decorations compare against. This is what hyprbars does; there is no way around it.
- `CColorValue::value()` returns a packed integer; `CHyprColor(uint64_t)` consumes it.

## The four things that will waste your day

**1. `draw()` cannot touch the framebuffer.** No GL context is current inside it.
`g_pHyprOpenGL->renderTexture()` there compiles, runs, and draws *nothing* — no error, no
log line. Queue a pass element and let the compositor's element renderer issue the draw:

```cpp
CTexPassElement::SRenderData data;
data.tex = tex; data.box = box; data.a = alpha;
data.damage = CRegion(box); data.clipBox = box; data.clipRegion = CRegion(box);
g_pHyprRenderer->currentPass().add(Hyprutils::Memory::makeUnique<CTexPassElement>(std::move(data)));
```

`currentPass()` is public (`render/Renderer.hpp:139`). This is why hyprbars carries its
own `BarPassElement`.

**2. Position every edge from `getWindowDecorationBox(this)`** — never from
`assignedGeometry` or `logicalBox()`. `assignedGeometry` is relative to the edge's defined
point while the positioner anchors each decoration to the *outer* window, so translating
by the edge point double-offsets by the bar height. `logicalBox()` is worse: it does not
report the window box at all (returned `(12,-1,1708,1464)` for a window Hyprland reported
at `[31,52] 1670x1357`).

Do **not** use `getBoxWithIncludedDecos()` either — it already accounts for the space we
reserved, so slicing it per edge is a feedback loop that drifts by the bar height.

**Corollary: `own` is a DIFFERENT rectangle per edge**, so never derive all edges from one
decoration's box. For TOP it is the client box; for LEFT it already starts at the window's
outer edge, so re-expanding a uniform `own.x - BORDER` shifts the side frames a whole
border further out and leaves 5px of bare wallpaper between frame and client. Take the
outer span from the sibling decorations instead — `outerBox()` in `CCdeDecoration.cpp`
reads `CDE:left` / `CDE:right` / `CDE` / `CDE:bottom` and lets the compositor's own
placement define the ring. `draw()`, `retexture()` and hit-testing all call it: a texture
rastered at one size and stretched over another puts the separators out of register with
the buttons.

**3. Rebuilding over the same path does not reload.** `unload` + `load` on a rebuilt
`build/libcde.so` silently keeps running the **old** code — the compositor holds the
previous, now-deleted inode. Build, `cp` to a new name, load the new path. Verify with
`grep -o "libcde[a-z0-9.-]*\.so" /proc/$(pgrep -f "bin/Hyprland" | head -1)/maps` — it
shows `(deleted)` when you have been fooled, and more than one `libcde-*.so` means two
copies are fighting over the same decorations.

- `hyprctl plugin unload` **only works by path**. By handle (or `-1`, or a comma list) it
  prints `plugin not loaded` and is a **no-op** — the handle keeps appearing in
  `plugin list` and the old copy keeps drawing. Confirm with
  `hyprctl plugin list | grep -c "Plugin cde"` returning 1.
- **`pgrep -x Hyprland` returns nothing even when Hyprland is running.** The process is
  `/etc/profiles/per-user/wyspr/bin/Hyprland`. Use `pgrep -f "bin/Hyprland"`.
- `printf` from a plugin goes nowhere when Hyprland was started by a greeter (stdout is a
  socket). Append to a file under `$HOME` instead.
- Setting a plugin config value with `hyprctl eval 'hl.config({...})'` does **not**
  reliably fire `config.reloaded`, so the bar can keep the old theme even though
  `hyprctl getoption` reports the new one. Follow with `hyprctl reload`.
- **Never `rm -rf "$XDG_RUNTIME_DIR/hypr"`**, not even after `pgrep` suggests it is dead.
  It holds the live `socket.sock` / `socket2.sock`; deleting them leaves a healthy
  compositor that `hyprctl` can no longer reach. Only a restart recovers.

**4. A second pass element inside an opaque one is silently dropped.** Queueing the bar and
the title as two `CTexPassElement`s draws the bar and nothing else — no error, the title
just never appears. The compositor culls an element whose region is already covered by an
opaque one. Draw the title into the bar's Cairo surface instead: one texture, one pass
element, exact control over where the text lands.

## API notes

Verify against the headers before trusting these; the plugin API moves between releases.

- `addWindowDecoration` is the **only** path to drawing chrome from a plugin.
- `getDecorationType()` must return `DECORATION_CUSTOM`.
- Flags must include `DECORATION_ALLOWS_MOUSE_INPUT` (or buttons get no clicks) and
  `DECORATION_PART_OF_MAIN_WINDOW` (so shadows and animations account for the geometry).
- `onInputOnDeco` returns `false` to swallow the event.
- `Render::CGLTexture` is the only concrete `ITexture` in the shipped headers — the only
  way to get pixels on screen from a plugin.
- Header root is `${hyprland.dev}/include/hyprland/src`.
- `getDecorationLayer()`, `getDecorationFlags()` and `getDisplayName()` are non-pure
  virtuals on the base class — override only what you need.

**Reference implementation:** hyprbars at `/home/wyspr/Repos/hyprland-plugins/hyprbars`.
Copy its **structure, not its signatures** — it tracks Hyprland master, and its `draw()`
takes `CRenderContext&` and `SWindowRenderPresentation&`, neither of which exists in our
pinned 0.56.2 where the signature is `draw(PHLMONITOR, float const&)`. Its commits are
literally "chase hyprland"; expect more drift.

**API drift to watch:** `Config::ParserUtils::parseColor` returns
`std::expected<int64_t, std::string>` in 0.56.2, not `std::optional` — hyprbars'
`.value_or(0)` will not compile, check `has_value()`.

## Known unknown

Whether Hyprland's built-in `CHyprBorderDecoration` suppresses itself when a plugin
decoration is present. `doesntWantBorders()` is private and its body is **not** in the
shipped headers. Expect to need `decoration:border_size = 0` to avoid a double frame.
Confirm empirically before trying to "fix" it in code.

## Active-window palette

The focused window paints with `*_active` colours, everything else with the un-suffixed
ones. Both sets are config values; neither is hardcoded.

**Focus is polled, not subscribed.** `ensureTextures` compares `m_lastFocused` against
`isActive()` alongside the other cache keys, so a focus change re-rasterises next frame.
`Event::bus()->m_events.window.active` only names the newly focused window, so a handler
would have to remember the previous one and invalidate all four of its decorations by
hand. Polling is a pointer compare and cannot miss an event.

**`isWindowActive()` is not usable here.** It takes `PHLWINDOW` (shared) while `m_window`
is a `PHLWINDOWREF` (weak), and weak does not convert to shared — not implicitly, not via
`.get()`, because `CSharedPointer` has no raw-pointer constructor. `isActive()` compares
raw pointers against `Desktop::focusState()->window()` instead.

`m_lastFocused` is an **int** initialised to `-1`, not a bool: a bool defaulting to false
would read as a valid cache hit on the first frame of an unfocused window.

## Shadow — removed

The plugin draws **no shadow**. A previous revision rendered an ordered 8x8 Bayer dither on
the right and bottom edges; renderer, `shadowBox()`, `m_shadowTex` and all
`plugin:cde:shadow_*` config values are gone. Do not reintroduce one by accident — in
particular `decoration:shadow:enabled` now renders Hyprland's own shadow unopposed, which
previously mis-sized its bottom edge to ~46px when a plugin reserved space (measured;
mechanism unknown, `getPositioningInfo()` has no body in the shipped headers). If
Hyprland's shadow looks wrong, leave it off.

## Roadmap

1. **Rule-settable colours.** Register four effects in `pluginInit`, unregister in
   `PLUGIN_EXIT` (hyprbars' pattern):
   ```cpp
   Desktop::Rule::windowEffects()->registerEffect("cde:face");  // + light, dark, title
   ```
   `configFor(PHLWINDOW)` reads the global config, then overrides any colour present in
   `window->m_ruleApplicator->m_otherProps.props` — **rule wins over config, one colour at
   a time**, so a rule may set `cde:face` alone and inherit the rest.
   ```lua
   windowrulev2 = float, title:^(kitty)$, cde:face rgb(ff00aa55)
   ```
   Also needs `Event::bus()->m_events.window.updateRules` to invalidate that window, or a
   rule change will not redraw until something else does. Cheap and self-contained.
2. **Verify button clicks.** Never exercised — see the hit-testing note in `## Status`.
   The single most valuable unknown left.
3. Make `bar_height` changes take effect at runtime (needs `damageEntire()` on reload).
4. Resolve the double-drawn active border, or document the `border_size = 0` workaround.

## Non-goals

Deliberately out of scope — do not add these by accident:

- App menu bars. Motif has one, but populating it is app territory, not decoration.
- The status bar strip at the bottom of the client area.
- The bottom Motif dock / launcher — a separate compositor surface, not a decoration.
- Per-button config toggles. `show_buttons` covers what has been asked for.