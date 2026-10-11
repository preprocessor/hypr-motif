# AGENTS.md

## Project

A Hyprland plugin that renders Common Desktop Environment (Motif) window chrome.

**v1 scope:** the outer beveled frame, the title bar, and the min/max/close buttons.
The look is drawn **procedurally with Cairo** at runtime — there are no image assets.
Do not add a bitmap pipeline; the chrome is simple 3D bevels and drawing it beats
slicing pixels out of `example.png` (which is itself 2x-scaled and shadowed).

## Commands

```sh
nix develop                                          # gcc 14.4, cmake 4.4.3, hyprland 0.56.2 headers
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
cp build/libcde.so ~/.local/share/hyprland/plugins/
hyprctl plugin load ~/.local/share/hyprland/plugins/libcde.so
```

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

`draw(PHLMONITOR, float const& alpha)` is invoked inside the compositor's render pass;
the chrome texture is blitted there. Positioning is `DECORATION_POSITION_STICKY` on
`DECORATION_EDGE_TOP` with `reserved = true`, so the title bar takes **real space** and
the client shrinks beneath it, rather than the chrome floating over the client area.

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

## Non-goals for v1

Deliberately out of scope — do not add these by accident:

- App menu bars. Motif has a menubar, but populating it is app territory, not decoration.
- The status bar strip at the bottom of the client area.
- The bottom Motif dock / launcher. That is a separate compositor surface, not a decoration.