# hypr-CDE-borders

Common Desktop Environment (Motif) window chrome for Hyprland, drawn procedurally with
Cairo at runtime — there are no image assets.

![Motif title bar](example.png)

## Features

- Fielded Motif title bar: close on the left, float and fullscreen on the right, each in
  its own separator-delimited field.
- Aliased title text with a hard 1px shadow, centred in the title field.
- Bar dragging and bottom-corner resizing.
- Separate palette for the focused window.
- Per-window rules to hide the bar, the title text or the frame (below).
- `hyprctl cde clients` reports the outer window rect, chrome included.

## Requirements

Built and verified against **hyprland 0.56.2**. There is no ABI stability across `dlopen`:
build against the same hyprland you run, or the plugin is rejected or takes the compositor
down.

## Install

```sh
# hyprpm
hyprpm add https://github.com/wyspr/hypr-CDE-borders
hyprpm enable cde
```

```sh
# from source; nix develop supplies the matching toolchain
nix develop
make                # or: cmake -B build && cmake --build build
```

Load it with `hyprctl plugin load /path/to/cde.so`, or from your config:

```ini
plugin = /path/to/cde.so
```

## Config

All options are under `plugin:cde` (Lua: `hl.config({ plugin = { cde = { … } } })`):

```ini
plugin {
    cde {
        bar_height = 22
    }
}
```

| property | type | default | description |
| --- | --- | --- | --- |
| `bar_height` | int | `22` | bar height in logical pixels, min 16 |
| `show_buttons` | bool | `true` | draw the three title bar buttons |
| `title_font` | string | `Chicago` | title font family, any fontconfig name |
| `title_font_size` | int | `12` | title size in points, min 6 |
| `color_face` | color | `#999999` | bar face, unfocused |
| `color_light` | color | `#d1d1d1` | bevel highlight, unfocused |
| `color_dark` | color | `#4e4e4e` | bevel shadow, unfocused |
| `color_title` | color | `#b0b0b0` | title text, unfocused |
| `color_face_active` | color | `#eda870` | bar face, focused |
| `color_light_active` | color | `#f7d9c0` | bevel highlight, focused |
| `color_dark_active` | color | `#7d593b` | bevel shadow, focused |
| `color_title_active` | color | `#ffffff` | title text, focused |
| `color_title_shadow` | color | `#000000` | the title's hard 1px drop shadow |

## Window rules

Rules remove parts of the chrome per window. A hidden strip reserves no space, so the
window closes up over it. Rules apply at plugin load and on `hyprctl reload`; `hyprctl
eval` alone does not trigger the reload that repaints them.

| rule | effect |
| --- | --- |
| `cde:no_titlebar` | hide the bar: buttons, title and drag area go, the 5px frame stays |
| `cde:no_title` | hide only the title text; the drag rules span the whole title field |
| `cde:no_border` | hide the frame: nothing on the left, right or bottom edge |

`no_titlebar` and `no_border` together leave only the client.

```lua
hl.window_rule({ match = { class = "^kitty$" }, ["cde:no_titlebar"] = true })
```

## Reporting geometry

`hyprctl clients` reports the **client** rect: the bar and frame are excluded, by the same
rule for Hyprland's own decorations. A plugin cannot change that, so this one adds
`hyprctl cde clients` — the compositor's `clients -j` document with each window's `at` and
`size` rewritten to the outer rect, chrome included. The output is always JSON and the
outer rect is invariant under the window rules above, because the client absorbs the freed
space.
