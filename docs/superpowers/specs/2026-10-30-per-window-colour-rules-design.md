# Per-window colour rules — design

Date: 2026-10-30
Status: awaiting approval

## Intent

Today every colour is global. A user who wants one window's bar to read as
"this is a terminal" (or a muted group of windows, or a red-alert window) has no
way to say so. The three existing window rules (`cde:no_titlebar`,
`cde:no_border`, `cde:no_title`) are the only per-window chrome controls, and
they do not correspond to any config option — the rule names and the config
names are two unrelated vocabularies.

Goal: **every rule name is a config key**. Rule `cde:X` overrides option
`plugin:cde:X` for the windows it matches; absent, the option is inherited.
The rules that cannot be expressed that way are replaced by options that can.

## Scope

Twelve rules, each 1:1 with a config key:

| Rule | Config option | Type | Default |
|---|---|---|---|
| `cde:color_face` | `plugin:cde:color_face` | colour | `#999999` |
| `cde:color_light` | `plugin:cde:color_light` | colour | `#d1d1d1` |
| `cde:color_dark` | `plugin:cde:color_dark` | colour | `#4e4e4e` |
| `cde:color_title` | `plugin:cde:color_title` | colour | `#b0b0b0` |
| `cde:color_title_shadow` | `plugin:cde:color_title_shadow` | colour | `#000000` |
| `cde:color_face_active` | `plugin:cde:color_face_active` | colour | `#eda870` |
| `cde:color_light_active` | `plugin:cde:color_light_active` | colour | `#f7d9c0` |
| `cde:color_dark_active` | `plugin:cde:color_dark_active` | colour | `#7d593b` |
| `cde:color_title_active` | `plugin:cde:color_title_active` | colour | `#ffffff` |
| `cde:show_titlebar` | `plugin:cde:show_titlebar` | bool | `true` |
| `cde:show_border` | `plugin:cde:show_border` | bool | `true` |
| `cde:show_title` | `plugin:cde:show_title` | bool | `true` |

The three `show_*` options are new. They replace `cde:no_titlebar`,
`cde:no_border` and `cde:no_title`, which are **removed** (user decision):
affirmative names so that a rule can force chrome back on as well as off, and so
that each rule has an option behind it.

`bar_height`, `title_font`, `title_font_size` and `show_buttons` stay global.
They are not colours and were not asked for; a per-window bar height would also
have to fight the positioner on every rule change.

## Semantics

- **One key at a time.** A rule overrides exactly its own key. A window with
  only `cde:color_face` gets a red unfocused face and the stock tan focused one —
  the 1:1 mapping the user chose. Setting `cde:color_face` *and*
  `cde:color_face_active` is how you pin a window's bar across focus.
- **Rule wins over config**, at read time, per key.
- **Colours** accept whatever `Config::ParserUtils::parseColor` accepts
  (`rgb(...)`, `rgba(...)`, `0xAARRGGBB`) — the same parser the config file
  uses, so there is one syntax, not two.
- **Booleans**: `0`, `false`, `no`, `off` (any case) are false; a valueless
  token (which Hyprland delivers as `"1"`) and anything else are true.
- **An unparsable or empty colour value falls back to the config colour.** No
  notification, no throw: a raster path must not be able to fail. The value is
  visible in `hyprctl clients` only as the rule, so a typo shows as "the rule did
  nothing", which is the current behaviour for a mistyped config colour.

## Approach

**Resolve on read.** `configFor(PHLWINDOW)` calls the existing global read and
lays the window's rule values on top:

```cpp
SConfig configFor(PHLWINDOW window);   // global config + this window's rules
```

Every consumer already has the window: `CCdeDecoration::retexture`,
`getPositioningInfo`, `syncHover`, `onInputOnDeco` and `main.cpp`'s
`outerBoxOf`. They pass it one level deeper and nothing else in the drawing
code changes — `renderBar` / `renderBorder` / `paletteFor` already take
`const SConfig&`.

`edgeHidden` and `titleHidden` keep their names and signatures (they read well
at the call sites) but are implemented over the same resolution, via a cheap
three-bool helper that does not parse colours. These sit on the input and
positioning paths, where parsing nine colours to answer a bool would be waste.

`config()` loses its last caller and is folded into the anonymous namespace as
the global reader, so the only public way to get a palette is one that knows
which window it is for.

### Invalidation

- `Event::bus()->m_events.window.updateRules` → that window's decorations
  `invalidate()` + `damageBar()`. Colours are pixels only.
- If the resolved `show_*` triple changed since the last raster, the listener
  also calls `g_pDecorationPositioner->onWindowUpdate(window)`, because those
  three move geometry (bar vs. frame band vs. nothing, and the client inset).
- The last rastered triple is stored on the decoration and is part of the cache
  key, alongside size and generation, so a missed event costs a stale disposition
  only until the next repaint rather than forever.
- `config.reloaded` keeps doing what it does today: bump, invalidate, position,
  damage every window. A rule change still lands through a `hyprctl reload` even
  if the event path turns out to be unreliable.

**Known risk, to be measured before it is claimed:** AGENTS.md records that a
`window.updateRules` listener calling `onWindowUpdate` was cut once rather than
debugged, because it re-enters the positioner from inside rule evaluation. The
plan is to try it and to measure it live. If it proves unstable the fallback is
stated plainly in the docs: a `show_*` rule change takes effect on the next
`hyprctl reload`, colours stay live.

## Parts of the how that are decided

- Rule registration grows from three to twelve `registerEffect` calls in
  `registerRules`; `unregisterRules` releases all twelve. Names are the display
  names the compositor maps, so they must be spelled exactly as in the table.
- Rule ids live in `config.cpp` next to `g_noTitlebar` today, in one fixed array
  so the twelve cannot drift apart from the twelve strings.
- `SConfig` gains `showTitlebar`, `showBorder`, `showTitle` (all `true`), read
  through the same designated-initialiser list as everything else.
- `configFor` reads `window->m_ruleApplicator->m_otherProps.props`; a null
  window or null applicator yields the global config unchanged, which is what
  the existing helpers already guard for.

## Left to the builder

- Exact spelling the two config backends accept for a colour value on a custom
  effect (`["cde:color_face"] = "rgb(ff00aa55)"` under Lua; `cde:color_face
  rgb(ff00aa55)` under classic). Measure, then put whichever the running backend
  takes in README.
- Whether `window.updateRules` fires for rule changes made by eval, as opposed to
  reload. Measure; document the answer either way.
- Whether `onWindowUpdate` from that listener is safe. See the risk above.
- README and AGENTS.md prose. AGENTS.md's `## Window rules` section, config
  table and roadmap item 1 all describe the old world and must be rewritten, not
  patched.

## Verification

The plugin has no unit tests; "done" means pixels, as `## Verification` in
AGENTS.md says. This change is pixel-neutral when no rule is set — same
`verify-pixels.sh` baseline/compare run, bars byte-identical — plus live checks:

1. A window with a face rule renders that face, and its frame strips take the
   same palette (the frame reads `SConfig` too).
2. Focus behaviour matches the 1:1 semantics: face-only rule reverts to tan when
   focused; face + face_active stays.
3. `show_titlebar` / `show_border` / `show_title` rules hide what they say and
   the client grows into the freed space — the `hyprctl cde clients` box and a
   screenshot both checked, since the outer box is invariant.
4. A rule changed at a live window redraws without a reload, if the event path
   works; if not, that sentence goes in the docs instead.

## Non-goals

- Per-window bar height, font, font size or `show_buttons`.
- Gradient or per-edge colours.
- Reading rules in `draw()`: the texture cache still owns that decision.

## Anti-goals

- Two names for one thing. If a rule cannot be named after a config key, it does
  not go in.
- Silent wrongness: a value the plugin cannot use must fall back to a documented
  default, never to uninitialised colour.
