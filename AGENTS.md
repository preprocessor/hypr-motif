# AGENTS.md

## Project

A Hyprland plugin that renders Common Desktop Environment (Motif) window chrome.

**Scope:** the title bar and its frame — the beveled border, the fields the bar is divided
into, and the three buttons. Drawn **procedurally with Cairo** at runtime; there are no
image assets, and there should not be. The chrome is simple 3D bevels; drawing it beats
slicing pixels out of `example.png` (itself 2x-scaled and shadowed).

## Code style

Human-readable and self-documenting beats clever. The rules that actually apply here:

- **Name every number that is not obviously local.** A magic `2` or `BEVEL + 2 + GROOVE * 2`
  in a drawing call is a bug waiting to happen. `FIELD_TOP`, `CLOSE_GLYPH_W`, `TITLE_PADDING`
  and the rest live in `chrome.hpp`; add one rather than inlining a second use.
- **One idea per function.** `renderBar` used to be ~150 lines doing six jobs; it is now a
  sequence of `drawBarBackground` / `drawFieldSeparators` / `drawCornerGrabRules` /
  `drawButtons` / `drawTitle`. Split when a function needs a comment to introduce each
  section.
- **Comments explain WHY, not WHAT.** Keep them where there is a trap that will bite the
  next reader — an ordering that must not change, an API that looks like it does something
  else, a measured number that looks arbitrary. Two or three lines naming the invariant is
  enough. The long derivations belong in this file; link to the section instead of
  repeating it.
- **Stale comments are worse than none.** When a measurement changes, fix the comment that
  quotes it — several here once described glyph sizes the code had long since changed.
- **Do not compress a boolean or an aggregate initializer onto one line.** A six-term cache
  key is unreadable inline; give it a name (`textureIsCurrent`) and a term per line.
- **Helpers return what they say they return.** Every function returning a `CBox` ends in
  `Rect`, so `buttonFieldRect` (the clickable area), `buttonGlyphRect` (the artwork) and
  `titleFieldRect` are distinguishable at a call site.
- **Delete dead code rather than commenting it.** `FIELD_W`, `CORNER_GRAB_REF`,
  `m_dragStart` and the never-drawn `color_highlight` were all removed once the sizes they
  referenced became derived; a constant that nothing reads is a lie about the design.
- **Pass the config in; never read it from a drawing helper.** `paletteFor`,
  `drawButtons`, `cornerGrab` and `renderBar`/`renderBorder` all take `const SConfig&`
  rather than calling `CDE::config()` themselves. A geometry helper that quietly consults
  global config hides where a pixel's value came from, and costs a full config read per
  raster. The caller already has it; pass it.
- **Do not dispatch an axis with a bool if it changes the argument order.** `bevelBands` and
  `grabSeparator` both took a `vertical`/`horizontal` flag over `(x, y, len)` and needed a
  comment explaining that transposing the `fill()` calls by mistake produced a 1px lit
  column. They are now `bevelBandsVertical`/`bevelBandsHorizontal` and
  `grabRuleAcross`/`grabRuleDown`. A trap that needs a comment to explain is a shape that
  should be split.

**Refactors must be pixel-neutral.** `renderBar`/`renderBorder` are the only things that
decide what a pixel looks like, so reorganising them is safe *only* if verified: screenshot
the bars of every visible window at two or three `bar_height` values with the old build
loaded, swap to the new build, and diff the bar regions. That check earned its keep — it
caught a groove row moving by one pixel and a grab-rule span losing a row, both introduced
while "just renaming a constant". See `## Verification`.

## Commands

```sh
nix develop                                          # gcc 14.4, cmake 4.4.3
# The devshell's hyprland tracks HEAD (0.57 today) while the compositor runs 0.56.2, so
# point pkg-config at the nixpkgs 0.56.2 headers or load refuses the ABI:
nix develop -c env PKG_CONFIG_PATH=<hyprland-0.56.2-dev>/share/pkgconfig:$PKG_CONFIG_PATH \
  sh -c 'cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build'
cp build/libcde.so /tmp/opencode/libcde-next.so      # fresh path — see gotcha 3
hyprctl plugin unload <the path that is loaded>
hyprctl plugin load /tmp/opencode/libcde-next.so
hyprctl plugin list | grep -c "Plugin cde"            # must be 1
hyprctl getoption plugin:cde:color_face               # must answer, or the copy is a zombie
nix build .#default                                 # packaging, needs a tracked tree
```

`inputs.hyprland` comes from Hyprland's own flake, so it tracks **HEAD**, not the
compositor this session runs. `packages.cde.override { hyprland = …; }` builds against a
different compositor — use the nixpkgs 0.56.2 one unless you are also running HEAD. There
is no `checks` output; the ABI match is enforced at load time by the API-hash check in
`main.cpp`, so a mismatched plugin is refused with a notification instead of crashing.

Build to a **new filename** every time — never over the one you are loading. The
compositor keeps the old inode mapped and silently runs stale code.

**Testing clicks.** There is no `xdotool`/`ydotool`/`wlrctl` on this box, and `hyprctl
dispatch movecursor 37,36` fails under a Lua config (`hl.dispatch(movecursor 37 36)` is a
Lua syntax error). What works:

```sh
grim /tmp/opencode/shot.png        # screenshot, then read pixels with magick
```

**XTest does NOT reach Hyprland.** It drives the XWayland core pointer, which a native
Wayland compositor never reads — a "real click" via libXtst moves the cursor and produces
no `input.mouse.button` event at all. Measured, not assumed. A **`/dev/uinput` virtual
mouse does work**: libinput enumerates it as a pointer (`hyprctl devices` lists it while
the fd is open) and its events reach decorations. Use a **relative** device — an absolute
one maps erratically, and the cursor keeps drifting after a press. Two details the kernel
on this box insists on, both of which surface as `EINVAL` on the `uinput_user_dev` write:

- the struct is exactly **92 + 4 × 256 bytes**; more is rejected. `struct.pack('%di' % 64, …)`
  is 256 bytes (`'%di' % 64` is `'64i'`, *not* `'di' * 64`), and there are **four** abs
  arrays, not five.
- the device has to stay open for ~3s before the compositor enumerates it.

Accel makes an open-loop pixel count useless, so drive it closed-loop: move in small
steps, read `hyprctl cursorpos`, correct by the error, repeat until it is within a pixel
or two. `/tmp/opencode/relmouse.py` does this (`click` / `move` / `drag`). Note
`hyprctl cursorpos` is the *animated* position and lags; the compositor's internal one is
what hit-testing uses.

```lua
-- moving the cursor, the Lua form of the old dispatcher:
hl.dsp.cursor.move({x=37, y=36})
```

`SButtonEvent{button, state}` is what `INPUT_TYPE_BUTTON` carries; the release edge is
how a click is told from a press.

## ABI constraint — read before touching the build

The compositor `dlopen`s the plugin and passes live C++ objects across the boundary.
**There is no ABI stability.** A plugin built against a different hyprland than the one
running is ejected, or crashes the compositor outright. It will not fail gracefully.

`flake.nix` forces `gcc14Stdenv` for exactly this reason — do not "modernise" it to the
default stdenv. If you bump `flake.lock`, rebuild before touching your running session.

## Layout

```
src/main.cpp           plugin entry: pluginAPIVersion / pluginInit / pluginExit
src/CCdeDecoration.hpp IHyprWindowDecoration subclass — declaration, the registry
src/CCdeDecoration.cpp positioning, draw(), input handling
src/config.hpp         config struct, rule effects, generation counter
src/config.cpp         plugin:cde:* registration + live read, window rules
src/chrome.hpp         layout constants, Cairo entry points
src/chrome.cpp         Cairo drawing of the bevels, bar, corners and glyphs
README.md              user-facing config docs
```

**There is no `tools/` or `examples/` directory in this tree**, whatever `## Verification`
and `## References` below say — those sections describe the harness and the reference
captures that produced the measurements, and the files themselves are not checked in.
Verify with `grim` + `magick` directly.

**Build wiring — do not hand-roll include directories.** `hyprland.pc` already exists and
pulls cairo transitively, so CMake needs exactly:

```cmake
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
target_link_libraries(cde PRIVATE PkgConfig::HYPRLAND)
```

**Three build systems, because three consumers need three things.** `CMakeLists.txt` is
the real one and what the flake uses — it is the only one with `install(TARGETS)`, which is
what makes nixpkgs' install phase put the `.so` in `$out/lib`. `Makefile` exists because
hyprpm runs `make all` and reads the `output` path straight out of the source tree; its
target is **parsed from `hyprpm.toml`**, so the manifest and the built file cannot
disagree. It deliberately has **no** `install` target, matching hyprbars.
`meson.build` is for meson users. Keep the source list in `meson.build` explicit rather
than globbed — it is four files and a glob would silently survive a rename.

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
  **Aliased, with a hard 1px down-right shadow** — see `## Title text style`.
- **Two groups of three drag rules** flank the title, 10px clear of it and of the button
  fields, light with a dark row under each. See `## Dragging`.
- **The bar drags the window**, natively and with no mouse binds required — see
  `## Dragging`.
- Focused windows use a separate palette — see `## Active-window palette`.
- **All three buttons act**, verified by real clicks against a live session: close →
  `Actions::closeWindow`, float toggle → `Actions::floatWindow(TOGGLE)`,
  maximize → `Actions::fullscreenWindow(FSMODE_FULLSCREEN)`. See `## Button input`.
- **Twelve window rules, one per config key** — nine colours plus `show_titlebar`,
  `show_border` and `show_title` — switch colour or chrome per window, and a rule applies
  live. See `## Window rules`.

Gaps, in the order they bite:

- **No resize handles on the frame edges.** The left/right/bottom decorations swallow no
  input and do nothing; CDE has resize grips in the corners.
- **`bar_height` changes at runtime now work**, but only through `config.reloaded` — see
  `## Bar height`. A `hyprctl eval 'hl.config(...)'` alone will not do it.
- The active border still double-draws — see `## Known unknown`.
- Buttons work only when **no mouse binding consumes the press**. See `## Button input`.

## Verification

Nothing in this codebase is unit-testable — the chrome is Cairo drawing decisions whose
whole specification is the pixel comparisons in `examples/`. So "verified" means **the bars
of every visible window, at two or three `bar_height` values, byte-identical before and
after**.

**The `tools/` scripts this section names are not in this tree** — no `verify-pixels.sh`,
`capture-bars.sh` or `set-bar-height.sh`, and no `examples/` directory either. The capture
below is the inline equivalent that was used to verify the per-window rules: `grim`, then
`magick -crop` the bar rows of the focused window, then `magick compare -metric AE` the two.
`hyprctl activewindow -j` gives the client rect; the bar is the `bar_height` rows above
`at[1]`.

The working harness was `tools/verify-pixels.sh`, wrapping `tools/capture-bars.sh` and
`tools/set-bar-height.sh`. What the capture does, and why each part matters:

- **Crops only the bar rows, never a full screenshot.** `hyprctl clients` `at` is the
  CLIENT rect — the frame has already reserved space, so the bar occupies `[y-bar_height,
  y)`. A full screenshot is never diffable here anyway: this session has a video window.
- **Exactly `bar_height` rows tall.** Rows below that are client content, which moves on its
  own and would show up as a false difference.
- **Sorts the window list by `(y, x, w, class)`** and names files by that index.
  `hyprctl clients` order is NOT stable between calls, so unsorted names mean the diff
  compares window A to window B and reports noise.
- Verified reproducible: three consecutive runs were byte-identical.

```sh
tools/verify-pixels.sh baseline     # with the OLD build loaded
# ...build, load the new code (gotcha 3)...
tools/verify-pixels.sh compare      # captures again and diffs
```

**Three ways this comparison lies, all of them handled in the script, all hit in
practice:**

1. If the set of windows or their positions changed between the baseline and the after, the
   indices no longer line up. Both modes fingerprint `hyprctl clients -j` and `compare`
   refuses rather than reporting a diff it cannot justify.
2. If a **second copy** of the plugin is mapped, you are diffing the old build against
   itself and every pixel matches. `compare` counts the mapped `libcde-*.so` and refuses on
   more than one. See gotcha 3.
3. If the plugin was never unloaded, `plugin load` finds the old decorations already in
   place, skips its own, and the "after" is still the old code — a green diff of nothing.
   That is the one failure this harness cannot detect from inside itself; check
   `hyprctl plugin list | grep -c "Plugin cde"` == 1 and that the mapped path is the one
   you just loaded.

`tools/set-bar-height.sh` edits `bar_height` in `~/.config/hypr/dynamic.lua` and runs
`hyprctl reload`, because that file's `config.reloaded` handler re-asserts the value — an
eval alone gets overridden (see `## Bar height`). It backs the config up and restores it on
exit.

Beyond the diff, on the dark palette the bar must contain exactly four colours — face,
light, dark and the title. Intermediate greys mean antialiasing crept back into the title.

**A headless renderer is not possible here, and it was tried.** Compiling the painters
outside the compositor would remove every source of flake from this check, so it is worth
recording why it does not work. `chrome.cpp` reaches the compositor in exactly two includes:
`render/Renderer.hpp` for `g_pHyprRenderer` (used only by `rasterise`) and
`render/decorations/DecorationPositioner.hpp` for `eDecorationEdges`. Stubbing both is easy.
The blocker is `helpers/Color.hpp`, which reaches `helpers/cm/ColorManagement.hpp` via
`macros.hpp`; that header's namespace-scope inline initialisers call
`NColorManagement::getPrimaries` and `CImageDescription::from`, which are defined in the
compositor binary. Hyprland ships **no linkable library** — plugins resolve those symbols
against the running process at `dlopen` time — so there is nothing to link against, and
leaving them undefined segfaults before `main()`. `CHyprColor`'s three constructors are the
same story. The gcc pin makes it worse rather than better: the devshell is gcc14 for ABI
reasons while hyprland's own libraries are built with gcc16, so a harness additionally needs
a different libstdc++ at both link and run time.

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

`examples/float-toggle.png` is user-supplied and **outranks CDE-7 for the middle
button**. It has been revised twice; re-measure rather than trusting this file.

`examples/title.png` is user-supplied and **outranks everything for the title text's
style**: aliased glyphs plus a hard 1px shadow. CDE-7's title is anti-aliased and
shadowless, so the two references disagree — follow title.png.

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

`CORNER_GRAB` is **derived from the bar height**, not fixed:
`cornerGrab(barHeight) = max(MIN_CORNER_GRAB, barHeight - 1)`.
Two reference data points agree on the relation — CDE-7's grab is 21 with a 22px bar,
new-standard.png's is 24 with a 25px bar. Both are `BAR_HEIGHT - 1`, so it scales with
`plugin:cde:bar_height`. This is the "corner accent", and it has to scale because it is measured
along the bar's own height — the same distance marks the same square on the left, right and
bottom frame strips, which is why one value drives all four corners. Verified live by
changing the bar 22 → 30: the vertical rule moves from bar-rel 21 to 29, and at 22 it
happens to coincide with the left-field separator at 21 (they are the same column, which
is why `grabRule` is drawn as its own pass rather than by widening those separators).

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

The title field carries two more marks that Motif does not have: the **drag rules** that
show the bar is draggable (`## Dragging`). They are ours, not measured from a reference, so
they are the one piece of the bar with no example image behind them.

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
| middle | 9x9 | 9-17 | **float-toggle icon**, user-supplied, replaces CDE-7's 4x4 rect |
| maximize | 11x11 | 8-18 | bare raised rect; 2px larger than the 9x9 reference, still centred |

The middle button is `BTN_FLOAT`, **not** `BTN_MIN`: it carries a float glyph and toggles
floating. Renamed from `BTN_MIN` once the action existed, because the old name described a
minimise button that 0.56.2 does not have.

Centring on the bar instead of the field puts every glyph ~2px high, very visible against
the reference. **Floor both axes of the glyph origin** — the field centre is half a pixel
on y, and a fractional origin makes Cairo anti-alias across two rows/columns instead of
hitting the reference cells, showing as pale half-pixels down one edge.

Motif draws **no icon inside any of the three**; do not add one, and in particular do not
draw an X — a 9x4 box has no room for one and it renders as a dark blob.
**Exception, deliberate:** the middle button carries the float-toggle icon, the literal
`FLOAT_TOGGLE` pattern in `chrome.cpp`. Two overlapping 6x6 outlined squares (4x4 hollow),
back one offset **three** up-right, front interior opaque (`F`) so it occludes the back
square's bottom-left corner. Light top/left, dark bottom/right; pressed inverts the bevel
like `glyphFrame` does.

The float toggle is in the field left of maximize. Getting that order backwards is
invisible in the code and obvious on screen.

## Title text style

`examples/title.png` (65x17) is authoritative for the title's **style**, and it disagrees
with CDE-7: the glyphs there are **aliased** and carry a **hard shadow**. Measured on that
image: text `#d8d8d8`, shadow `#101010` offset exactly **(+1, +1)** down and right, and
only three colours present — no intermediate greys anywhere, so it is true hard-edged
aliasing rather than lightly-antialiased text.

Cairo's toy text API antialiases by default, so `renderBar` sets
`CAIRO_ANTIALIAS_NONE` on the bar's font options (with hinting left on — hinted + aliased
renders solid stems; unhinted aliased text breaks stems on some faces). The shadow is a
second `cairo_show_text` of the same string, drawn **first** in `color_title_shadow`
(black), offset by `TITLE_SHADOW_DX/DY` (both 1). No blur and no alpha ramp: the
reference's shadow pixels are as solid as the glyphs'.

The face is **`TITLE_FONT` = "Chicago"** — the macOS System font as reissued in
ChicagoFLF (nixpkgs `chicago-font`, installed here), which is the blocky square-cornered
face the reference is set in. It was "Sans" before and did not look like the example at
all. The toy API takes one family with no fallback chain, so this relies on fontconfig
resolving the name; if Chicago is ever missing, Cairo silently substitutes a default face
rather than failing.

**The baseline is centred on the FIELD, not the bar, and both axes are floored.**
`baseline = floor(fieldCentre + (ascent - descent) / 2)`, where `fieldCentre` is the
centre of rows 6..20 (13.5) — the same band the button glyphs centre on — and the metrics
come from `cairo_font_extents`, NOT from `ext.height` of the string, which moves with
whichever glyphs are present. Two bugs lived here:

- `ty = (size.y + ext.height) / 2` centred on the *bar* using the string's ink height, so
  the title sat ~3px low and dropped into the bottom edge.
- With `ANTIALIAS_NONE` Cairo rounds **each glyph** to whole pixels independently, so a
  fractional origin spaces the stems unevenly; with the 1px shadow underneath, that reads
  as **text drawn twice**. `floor()` both the x origin and the baseline, same rule the
  button glyphs follow.

**12pt is close to the ceiling for a 22px bar.** Measured: the glyphs occupy bar rows
7..20 (before descenders push to 21), and the field is rows 6..20 with the bottom edge at
21. Raising `title_font_size` further, or lowering `bar_height`, will push the text into
the bottom edge; check the rendered row extent if either changes.

With the dark palette (`color_face #333333`, `color_light #555555`, `color_dark #111111`)
the rendered bar contains exactly four colours — face, light, dark and the title — which
is the check to run after touching this code. If greys appear between the glyph edges,
antialiasing came back.

## Dragging

The bar drags its window with **no mouse bind required**, and the drag is the compositor's
own: `g_layoutManager->beginDragTarget(target, MBIND_MOVE)` / `endDragTarget()`. That is the
same call a `mouse:…, movewindow` bind makes, so `binds:drag_threshold`, float-on-drag,
snapping and drag-into-group behave exactly as the user configured them. Do not reimplement
it with `CWindow::moveWindowPos` — that ignores every one of those.

**A press arms, motion starts it.** `INPUT_TYPE_BUTTON`'s press only records
`m_dragArmed` + `m_dragOrigin`; `syncDrag()` starts the drag once the cursor passes
`DRAG_THRESHOLD` (3px). Dragging straight off the press would be simpler and wrong:
`binds:drag_threshold` **defaults to 0**, so `dragBegin` would float a tiled window on every
click that lands on the title bar to focus it.

`syncDrag()` is called from `main.cpp`'s `input.mouse.move` listener, for the same reason
hover is: Hyprland sends decorations no `INPUT_TYPE_MOTION`, and a poll inside `draw()`
never runs for a window that is sitting still — which is exactly the state a window is in
while it waits to be dragged. The same listener is where a drag whose **release landed off
the window** gets ended: nothing else sees that release, so `hasHeldButtons()` is the
heal, on the next mouse move of any kind.

**`INPUT_TYPE_DRAG_START` returning `true` CANCELS Hyprland's drag, it does not start
one.** `CKeybindManager::changeMouseBindMode` (the `movewindow` dispatcher) asks the window
via `checkInputOnDecos(DRAG_START)` and calls `beginDragTarget` only when the answer is
*false*. So `onInputOnDeco` returns `true` only on a **button** — that is what stops a press
on a button from moving the window — and `false` everywhere else, which both lets a user's
`movewindow` bind work and hands our own path the pointer. (The pre-drag code had this
inverted, and a bound `mouse:272, movewindow` silently did nothing on the bar.)

`INPUT_TYPE_DRAG_END` returns **false**: it is reached from `CDragStateController::dragEnd()`
*before* it finishes the move, and returning true there skips the geometry commit, the focus
and the group merge.

The **drag rules** mark the draggable area: two groups of `DRAG_RULE_LINES` (4) horizontal
lines flanking the title, each line a light row with a dark shadow one pixel right and one
row below. They clear the title's ink box and the button field separators by
`DRAG_RULE_GAP` (10px) and fill whatever is left of the title field, so a long title pulls
them in rather than letting them run under the text; a gap under `DRAG_RULE_MIN` (4px)
drops the rule instead of drawing a stub. `drawTitle` returns the ink box for this —
re-deriving the font metrics in a second place is how they would drift.

Vertically the lines sit inside the **field band** (not the bar's outer rows — that band
starts below the groove) with one face row between them (`DRAG_RULE_STACK_GAP`), and what
the band has left over is the clearance above and below: `edgeGap`, which is **2px at the
22px reference bar and grows with the bar** (4/4 on a 26px one). Sharing the leftover with
the internal gaps instead was tried and rejected — it spreads the lines apart on a tall bar
and pins them against the top of the band, which reads as misplaced rather than centred. The
minimum bar that fits them at all is 23px (`slack ≥ DRAG_RULE_LINES * 2`, one face row above
and below); below that they are dropped.

## Bar height

`plugin:cde:bar_height` (int, default 22, clamped to ≥16) sizes the title bar, and it
**works at runtime** — verified live by taking the bar from 22 to 30 and back.

It needs two things, and doing only the first is the trap:

1. `barHeight` feeds `getPositioningInfo`'s `desiredExtents`, so `config.reloaded` must
   bump the generation to re-raster… but
2. **bumping the generation is not enough.** The bar's height is decided by the
   *positioner*, which only re-runs when told to. Re-rastering alone produces a texture
   of the new height stretched over a box that is still the old one. `main.cpp`'s
   `config.reloaded` listener therefore calls `g_pDecorationPositioner->onWindowUpdate(w)`
   per mapped window (public, and documented "called on resize") and drops the decorations'
   textures, since their geometry is about to change under them.

**Only `config.reloaded` does this.** Setting the value with
`hyprctl eval 'hl.config({...})'` does not reliably fire that event, so the bar keeps the
old height even though `hyprctl getoption` reports the new one — measured, not assumed.
Follow an eval with `hyprctl reload`, or set it in `hyprland.conf`.

Everything that measures a distance along the bar derives from this value — the field
band, the title baseline and the corner grab (`## Corners`) — so a taller bar grows all of
them together rather than stretching the chrome.

## Window rules

**Every rule is a config key.** `registerRules` registers twelve effects, one per option, and
`PLUGIN_EXIT` releases all twelve: the nine `cde:color_*` and **`cde:show_titlebar`**,
**`cde:show_border`**, **`cde:show_title`**. There are no other rule names — a rule that
could not be named after an option was removed in favour of one that could, which is why
`cde:no_titlebar` / `cde:no_border` / `cde:no_title` no longer exist.

`configFor(window)` is the whole mechanism: `globalConfig()`, then `applyColour` for each of
the nine colours and `applyBool` for each flag, in the rule's own order. Rule wins over
config **one key at a time**, so `cde:color_face` alone leaves the focused palette at the
configured `_active` colours. `edgeHidden` / `titleHidden` read the same resolution through
`chromeFlags`, which does not parse colours — they sit on the input and positioning paths.

**Colour values go through `Config::ParserUtils::parseColor`**, the parser the config file
uses, so `#rrggbb`, `rgb(rrggbb)` and `rgba(rrggbbaa)` all work and there is one syntax, not
two. A value it rejects leaves the config colour in place: `parseColour` returns
`std::nullopt` and `applyColour` does not touch the field. It cannot throw or notify — this
runs inside a raster.

**Bool values**: `parseBool` lowercases and reads `0`, `false`, `no`, `off` as false,
everything else true. A valueless token arrives from the compositor as `"1"`, so
`["cde:show_titlebar"] = false` and `= 0` both hide the bar, and `= true` brings it back.

**`show_title = false` hands the whole title field to the drag rules.** `retexture` passes an
empty title to `renderBar`, and `drawDragRules` treats an empty ink box as "nothing to flank":
one group of lines spans `leftFieldEnd + DRAG_RULE_GAP` to `rightFieldStart −
DRAG_RULE_GAP` instead of splitting around the text. An empty title string does the same,
so there is one code path for both.

**`show_titlebar = false` leaves the top frame band behind.** The top edge is the only one
that carries two things, and it has three states: full bar, a `BORDER`-thick frame band when
the bar is hidden, nothing when both flags are set. Everything along the top is measured
from that reserve, so hiding the bar moves the client from `y = 26` to `y = 5` (not 0) and
grows it by `bar_height − BORDER`; hiding the border moves it from `x = 5` to `x = 0` and
widens it by `2 × BORDER`. The side strips then skip their top grab marker, which is
measured from a bar that is no longer there. Measured live on a `cde-test` window at
`[1725,26] 1710x1359`: border off `→ [1720,26] 1720x1364`, border back and bar off
`→ [1725,5] 1710x1380`, both off `→ [1720,0] 1720x1390`.

**A hidden edge reserves NOTHING — `getPositioningInfo` returns `reserved = false` with zero
extents.** Blanking the pixels would leave a hole where the chrome used to be; the client has
to close up over it. `draw()` and `onInputOnDeco()` test the same predicate, so a hidden
strip also takes no clicks and leaves no corner grab squares.

`windowBox()` needed no change for a zero-extent sibling. It already falls back to its own
box when a sibling is unplaced, and with nothing reserved on that edge **the client edges it
falls back to ARE the window's outer edges** — the same identity, arrived at by default.

**`window.updateRules` invalidates pixels only.** The listener calls `invalidate()` +
`damageBar()` for that window's decorations, which is what makes a colour rule or a
`show_title` change appear immediately. It does **not** call the positioner: Hyprland
repositions a window itself when a rule is applied to it — measured: eval'ing
`cde:show_border = false` at a live window moved its client from `[1725,721] 1710x664` to
`[1720,721] 1720x669` with no reload and no plugin involvement, and the frame pixels
vanished. Toggling back restored it, stable across frames. An earlier revision added
`g_pDecorationPositioner->onWindowUpdate()` from this event, which re-enters the positioner
from inside rule evaluation; it was never needed.

**The Lua config backend validates rule names, and a namespaced rule is a QUOTED key.**
`hl.window_rule({ ..., ["cde:color_face"] = "rgb(ff0000)" })` is accepted. The **bare**
option suffix is not the rule name: `show_titlebar = false` in a `hl.window_rule` reloaded
"ok" and did nothing, because it is a different key, not because it was rejected. Spell the
`cde:` prefix and quote it.

**A rule added with `hyprctl eval` is NOT removed by `hyprctl reload`.** Measured with the
old names and unchanged by this work: after evaluating a rule for one class and reloading,
that window still had both effects in its custom-prop map and still rendered without chrome.
Only a compositor restart clears it. This bites when testing — a window kept without its
title bar after a reload is usually a leftover test rule, not a bug, and `props.size` on
that window is the tell.

`unregisterEffect` blanks the name rather than erasing it, so re-registering after an
unload takes a **fresh slot** — a slow leak, one entry per load/unload cycle, bounded by a
uint16_t. Registering without unregistering would leak the same amount and additionally leave
the name unusable while the plugin is gone.

## Reporting geometry

**`hyprctl clients` reports the CLIENT rect and will always exclude our chrome.** That is not
a plugin-shaped hole. The positioner subtracts reserved space before the IPC ever sees the
window, for Hyprland's own decorations exactly as much as for ours — measured, by setting
`general:border_size = 6`:

```
border_size = 0  →  helium at [5, 26]   size [1710, 1359]
border_size = 6  →  helium at [11, 32]  size [1698, 1347]
```

**There is no hook to change it.** `SDecorationPositioningInfo` carries `desiredExtents` (how
much to *reserve*) and `reserved`, and nothing else. `CHyprBorderDecoration` does hold a
private `m_reportedExtents`, but it is non-virtual and inaccessible. So `hyprctl cde clients`
is the only way to publish the outer rect:

```
$ hyprctl cde clients
[{"address": "0x5e86c8e02a40", "at": [440, 0], "size": [2560, 1440], ...}]
```

**It is `hyprctl clients -j` with two fields rewritten, not a payload built here.**
`HyprlandAPI::invokeHyprctlCommand("clients", "", "json")` hands back the compositor's own
document, so every field it has is already in it and a field added upstream arrives for
free. The patch keys on `"address"`, treats each object as the span up to the next address,
and rewrites only the `"at"` and `"size"` inside it.

Measured over the whole document, `at` and `size` are the **only** two-integer arrays and
there is exactly one of each per object — which is what makes the two patterns enough. A
window title cannot fake a match, because quotes inside a JSON string come back escaped: a
title containing `\"at\": [9, 9]` does not match `"at"\s*:\s*\[`. Checked offline against a
hand-written payload with those traps in it, plus negative coordinates, an empty array, a
window with no computed rect, and a single object with no trailing comma.

Two details that are not obvious from the code:

- **The custom raw-string delimiter `R"RE(...)RE"` is load-bearing.** `R"(...)"` terminates
  early on both patterns, because a capture group puts `)` immediately before a `"` — as in
  `[a-fA-F]+)"`.
- **Both matches are collected before anything is written**, then applied back to front.
  Replacing the `at` shifts the offset of the `size` behind it, so writing as we scanned
  would corrupt every object after the first.

`getWindowMainSurfaceBox()` plus the extents `getPositioningInfo` reserves is the outer
rect. Hand-rolled arithmetic beat both obvious alternatives, on measurement:

- `getWindowBoxUnified(RESERVED_EXTENTS)` — agrees with the arithmetic on every window that
  fits the work area, six of six measured, but it **clamps to that area**.
- `getFullWindowBoundingBox()` — reads **47px taller**, Hyprland's shadow extent leaking into
  a session where a plugin reserved space. `RESERVED_EXTENTS | FULL_EXTENTS` does the same
  thing, which is how the 47 was pinned down.
- `CWindow::windowBox()` — the outer rect the chrome is drawn against, but assembled from the
  four sibling boxes with `own` as the fallback for any missing one, and `own` is a DIFFERENT
  rectangle per edge (gotcha 2). Reading it from an arbitrary decoration is wrong the moment
  one strip is hidden by a rule.

**The outer rect does not move when a rule hides part of the chrome** — measured: with
`cde:show_titlebar = false` on a helium, `hyprctl clients` went `[5,26] 1710x1409` → `[5,0] 1710x1435`
while this stayed `[0,0] 1720x1440`. The client absorbs the freed space and the window does
not resize, so the outer box is invariant and is simply the window's own box. That is also
why a pixel check is the only thing that settles it: both readings fit the arithmetic, and
only the screen shows that a frame is there.

**`registerHyprCtlCommand` holds a `std::function` into this shared object for as long as it
is registered**, so `PLUGIN_EXIT` must unregister it before anything else — same hazard, same
reason, as `g_listeners`.

## Config options

All namespaced `plugin:cde:`, registered with `addConfigValueV2` in `src/config.cpp`, set
under `hl.config({ plugin = { cde = { ... } } })`. User-facing docs are in `README.md`;
this table is the same list.

| Key | Type | Default |
|---|---|---|
| `bar_height` | int | `22` |
| `color_face` | colour | `#999999` |
| `color_light` | colour | `#d1d1d1` |
| `color_dark` | colour | `#4e4e4e` |
| `show_buttons` | bool | `true` |
| `show_titlebar` | bool | `true` |
| `show_border` | bool | `true` |
| `show_title` | bool | `true` |
| `color_title` | colour | `#b0b0b0` |
| `color_title_shadow` | colour | `#000000` |
| `title_font_size` | int | `12` |
| `title_font` | string | `Chicago` |
| `color_face_active` | colour | `#eda870` |
| `color_light_active` | colour | `#f7d9c0` |
| `color_dark_active` | colour | `#7d593b` |
| `color_title_active` | colour | `#ffffff` |

There is no `color_highlight`: it was removed as never-drawn. `MIN_BAR_HEIGHT` (16) and
`MIN_TITLE_FONT_SIZE` (6) live in `chrome.hpp` and are the clamps `globalConfig()` applies.

**Every option has a same-named window rule** — option `plugin:cde:X` is rule `cde:X` — see
`## Window rules`. `bar_height`, `title_font`, `title_font_size` and `show_buttons` are
global only; `configFor(window)` overrides colours and the three `show_*` flags.

Un-suffixed colours apply to **unfocused** windows, `_active` to the focused one. The
active face/light/dark are measured from new-standard.png.

**The focused title is white and the unfocused one grey**, deliberately the opposite of
CDE-7 (which is white always) and the reverse of an earlier revision here. It makes focus
readable from the bar alone at a glance without looking at anything else. White on the tan
`#eda870` active face is only ~2.9:1, so a user keeping that palette may want a lighter
`color_title_active`; the default follows the request, not the reference.

`color_title_shadow` is a **single** value, not per-window: examples/title.png shows one
black shadow under both states. It is deliberately NOT the bevel's dark colour — that is
brown (`#7d593b`) on the tan palette, and a brown shadow on a tan bar reads as a smudge.

Two constraints on implementing these:

- `Config::Values::IValue` exposes **no per-value change callback**. Live reload must go
  through `Event::bus()->m_events.config.reloaded` plus the generation counter that
  decorations compare against. This is what hyprbars does; there is no way around it.
- `CColorValue::value()` returns a packed integer; `CHyprColor(uint64_t)` consumes it.

## The four things that will waste your day

**0. An event listener that is not released outlives the plugin.** `Event::bus()` holds
only a `CWeakPointer` to a listener (`Signal.hpp:21`) and `listen()` hands back the last
strong one (`Listener.hpp:29`). The idiom hyprbars uses — `static auto P = ...listen(...)` —
does keep it alive, but it keeps it alive *forever*: the reference doing the decrement lives
in a `.bss` that `dlclose` unmaps, so the signal stays registered with a `std::function`
handler pointing into freed code, and the next emit segfaults. `main.cpp` holds the five
handles in `g_listeners` and `PLUGIN_EXIT` clears it, which is the only moment the handlers
can be destroyed safely. Do not "simplify" that back into a `static`.

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

**3. Rebuilding over the same path does not reload — and unloading used to leave the
decorations behind.** `unload` + `load` on a rebuilt `build/libcde.so` silently keeps
running the **old** code — the compositor holds the previous, now-deleted inode. Build,
`cp` to a new name, load the new path. Verify with
`grep -o "libcde[a-z0-9.-]*\.so" /proc/$(pgrep -f "bin/Hyprland" | head -1)/maps` — it
shows `(deleted)` when you have been fooled, and more than one `libcde-*.so` means two
copies are fighting over the same decorations.

`PLUGIN_EXIT` now calls `CCdeDecoration::removeAll`, which is what actually makes
`plugin unload` mean unload. **Measured, before and after:** a build without it left its
four decorations per window in `m_windowDecorations` on unload, which kept the `.so` mapped
for the rest of the session — so the *next* `plugin load` found the display names already
present, skipped adding its own, and the screen kept showing the old build with `plugin list`
reporting `1`. Building and loading a new copy tested nothing at all. Now:

- decorations are removed before the handle goes away, so the mapping is released;
- `plugin unload` then `plugin load` really does run the new code;
- **a plugin built before this change can still be stuck in that state.** Its decorations
  belong to a `.so` that is already unmapped-by-policy and no longer loadable, so no
  `removeWindowDecoration` can be issued for them. Recovery is closing the windows (their
  decorations die with them, dropping the refcount) or restarting Hyprland. Treat "the bars
  still look like the old build after a reload" as this, not as a caching bug.

**A half-unloaded copy leaves a ZOMBIE that looks alive.** Symptom set, all measured in one
session: the bar drew **doubled** text, the title **stopped updating**, and
`hyprctl getoption plugin:cde:*` returned **`no such option` for every value** while
`plugin list` still showed **1** plugin. That combination is the fingerprint — it is not a
rendering bug and no amount of raster tweaking will touch it:

- two copies were mapped (`~/.local/share/hyprland/plugins/libcde-X.so` **and**
  `build/libcde.so`); unloading one removed its config registrations but its decorations
  kept drawing from the deleted inode, i.e. from an **older build**. So config reads fell
  back to struct defaults (hence "not updating") and the live pixels were stale code
  (hence "not centred", "double draw").
- Recovery: unload **every** mapped path until `plugin list` reports **0**, then load one
  fresh copy and confirm **both** `plugin list` count == 1 **and** that
  `hyprctl getoption plugin:cde:color_face` answers. Treat "options missing" as the loudest
  of the signals, because it is the only one that names the real cause.

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

**A title change is invisible to `draw()` on its own — handle it from an event.** This is
counter-intuitive and cost real time: a title change updates `m_window->m_title`, so
`draw()`'s cache key misses and `retexture()` builds a new texture… and nothing appears on
screen. Two compounding reasons, both in the compositor:

1. `CWindow::onUpdateMeta` posts events and calls `updateToplevel()` — it **never damages
   the decoration box**, so nothing tells the renderer that region went stale.
2. Hyprland only calls a decoration's `draw()` when that decoration is being repainted, so
   with no damage `draw()` does not run at all and the cache check never even happens.

Do not try to fix this from inside `draw()`; it cannot run. `main.cpp` listens to
`Event::bus()->m_events.window.title` and, for each of that window's decorations, calls
`invalidate()` + `damageBar()`. `damageBar()` also runs at the end of `retexture()`, which
covers every other source of a fresh texture.

The symptom was "the title only updates when I resize the window, or use the zoom effect" —
both damage those pixels as a side effect, which is what made it look like a raster bug
rather than a damage bug.

## Button input

Four things about `onInputOnDeco`, all read out of the 0.56.2 source rather than guessed.
Every one of them was a bug before it was a fact.

**`pos` is GLOBAL, not decoration-local.** `CWindow::checkInputOnDecos` forwards the very
same coords it just tested with `containsPoint()`; the built-in group bar subtracts its own
`assignedBoxGlobal().pos()` for exactly this reason. Hit-testing the raw coords against
bar-relative rects silently never hits anything — which is why no button ever responded
while this file claimed the code was "assuming decoration-local". Convert with
`barBoxGlobal()` (the same box `draw()` uses, workspace render offset included).

**`INPUT_TYPE_MOTION` is never sent.** The enum value exists in `SharedDefs.hpp` and
0.56.2 has **no call site for it** — the header is not documentation. Hover therefore
cannot arrive as an event: `ensureTextures` polls `g_pInputManager->getMouseCoordsInternal()`
every frame instead, and that poll is also what stops a press whose release never arrived
from leaving the glyph stuck in its inverted palette.

**Act on `INPUT_TYPE_BUTTON`'s release, never on `DRAG_END`.** `BUTTON` arrives on press
*and* release (`processMouseDownNormal`), carrying the event in `data` — read
`state`/`button` from it or a click cannot be told from a press. The drag is ended from the
same release: a `DRAG_START` we swallow would stop Hyprland creating a drag target at all,
and `CDragStateController::dragEnd()` early-returns without one, so `DRAG_END` would never
arrive. See `## Dragging` for what each of the two returns is doing.

**Call `Config::Actions::` directly; do not go through `hyprctl dispatch`.** Under a Lua
config `hyprctl dispatch <name> <args>` is eval'd as `hl.dispatch(name args)`, which is a
syntax error for every classic dispatcher name — measured: `hyprctl dispatch exec true` →
`"')' expected near 'true'"`. The same bug means `invokeHyprctlCommand("dispatch", …)` was
silently doing nothing here. `Config::Actions::closeWindow(self)` and friends are called
in-process, take the window explicitly (so a click on an unfocused window's bar acts on
*that* window), and work under both config backends. Note there is **no `minimize`
dispatcher or action at all** in 0.56.2 — hence the middle button being float, not minimize.

**Hit-test the FIELD, not the glyph.** `CDE::buttonAt` and the rects beside it in
`chrome.cpp` own the geometry and the test together, so they cannot drift; the glyph (9x4
on close) is far too small a target and made clicks feel spotty. Only `buttonAt` is
exported in `chrome.hpp` — the rects are internal, and handing out raw geometry is what let
the two lists drift apart in the first place.

**Hover swaps the bevel, it does not wash the background.** A flat highlight fill across
the field flattens the bevel and reads as a mis-coloured patch sitting *on* the bar rather
than as part of it. Hover and held both use `inverted(p)` — light and dark exchanged, face
and title kept — which is what makes the button read as inset. There is no `hover` colour
in `SPalette` any more. Measured at 1x: hovering changes exactly the field's 176 pixels
(16x11), the glyph's 1px edges swapping `#555555`/`#111111`.

**Buttons only fire when nothing else consumes the press.** `processMouseDownNormal`
returns before `checkInputOnDecos(INPUT_TYPE_BUTTON)` if a mouse bind matched the press.
This session now binds `mouse:272`/`mouse:273` to Lua, so it runs with
`binds:pass_mouse_when_bound = true`; a user with
`bindm = …, mouse:272, movewindow` needs the same or their bar buttons go dead. Dragging is
subject to the same gate — with the option off, the bar does not drag either.

## API notes

Verify against the headers before trusting these; the plugin API moves between releases.

- `addWindowDecoration` is the **only** path to drawing chrome from a plugin.
- `getDecorationType()` must return `DECORATION_CUSTOM`.
- Flags must include `DECORATION_ALLOWS_MOUSE_INPUT` (or buttons get no clicks) and
  `DECORATION_PART_OF_MAIN_WINDOW` (so shadows and animations account for the geometry).
- `onInputOnDeco` returns `true` to CONSUME the event (`checkInputOnDecos` stops on true);
  `false` means "not handled" and lets `DRAG_START`/`DRAG_END`/focus proceed. Getting this
  backwards on a frame strip silently swallows drag drops.
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
`plugin:cde:shadow_*` config values are gone. Do not reintroduce one by accident —
`decoration:shadow:enabled` renders Hyprland's own drop shadow, and it now measures the
window correctly.

**The ~46px band below the window was a decoration-order bug, not a shadow bug.** A drop
shadow is `getBoxWithIncludedDecos(client) + range`, and that helper unions the boxes the
positioner assigned to every `DECORATION_PART_OF_MAIN_WINDOW` deco. The positioner places a
single-edge deco *inside* the space the higher-priority edges have already taken: a
`LEFT`/`RIGHT` strip sorted after the `TOP` bar gets `y = client.y + barHeight` and
`height = client.h + barHeight`, so its box reaches `2 * barHeight - BORDER` past the
window's bottom (measured: 47px at a 26px bar). Painting was never affected — `outerBox()`
comes from the sibling boxes, not this one — so the only symptom was the shadow. All four
strips used to share `DECO_PRIORITY` and the positioner's sort is not stable, so which edge
landed last varied per window and per decoration list; that is why the band came and went.
The frame edges now carry `DECO_PRIORITY_FRAME` (higher) and the bar `DECO_PRIORITY`, so
left/right/bottom are always placed before it: they get the bare client band, and the bar
placed last expands over their extents to the full window width. Verified by pixel diff —
the bars are byte-identical before and after, and the shadow is a `range`-wide gradient on
all four sides.

## Roadmap

1. Resolve the double-drawn active border, or document the `border_size = 0` workaround.

## Non-goals

Deliberately out of scope — do not add these by accident:

- App menu bars. Motif has one, but populating it is app territory, not decoration.
- The status bar strip at the bottom of the client area.
- The bottom Motif dock / launcher — a separate compositor surface, not a decoration.
- Per-button config toggles. `show_buttons` covers what has been asked for.