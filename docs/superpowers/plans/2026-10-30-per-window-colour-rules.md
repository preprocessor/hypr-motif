# Per-window Colour and Chrome Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every colour option a window rule of the same name, replace the three
`cde:no_*` hiding rules with `show_*` options that also have matching rules, and resolve
all of it per window at read time.

**Architecture:** `configFor(PHLWINDOW)` returns the global config with the window's rule
values layered on top, one key at a time. Every decoration already holds its window, so the
six global config reads become per-window reads and no painter changes. `edgeHidden` /
`titleHidden` stay and read a cheap three-bool helper. A `window.updateRules` listener
invalidates and damages; only a change to the three `show_*` flags re-enters the positioner.

**Tech Stack:** C++20, Hyprland 0.56.2 plugin API, Cairo; no test framework. Verification is
a build, `hyprctl`, `grim` and pixel reads with `magick`.

**Spec:** `docs/superpowers/specs/2026-10-30-per-window-colour-rules-design.md`

## Global Constraints

- Rule names are `cde:` + the config key suffix, exactly: the nine `color_*`,
  `show_titlebar`, `show_border`, `show_title`. No other rule names exist after this work.
- New options default to `true` and are registered with `addConfigValueV2` alongside the
  existing ones; `MIN_BAR_HEIGHT` / `MIN_TITLE_FONT_SIZE` clamps stay untouched.
- Colours parse with `Config::ParserUtils::parseColor`, which returns
  `std::expected<int64_t, std::string>` — test `has_value()`, not `.value_or`.
- A colour the plugin cannot use falls back to the config value. No throw, no notification.
- Painter code (`chrome.cpp`) is not touched. This is a refactor plus a resolver; if a pixel
  moves with no rule set, that is a bug.
- Build to a **new filename** every load; never overwrite a loaded `.so`.
- `AGENTS.md` is rewritten once, in Task 4, not incrementally.

**Environment facts this plan relies on** (measured in this session, some contradicting
AGENTS.md — trust these):

- The repo has no `tools/` and no `examples/` directory, whatever AGENTS.md says. There is
  no `verify-pixels.sh`; do the before/after capture inline as the steps show.
- Hyprland's PID: `pgrep -f '^Hyprland'` (the harness's own shell command lines also match
  `bin/Hyprland`, so anchor the pattern).
- The live config is Lua; the user's `~/.config/hypr/dynamic.lua` sets the dark palette,
  `bar_height = 26`, and a `cde:no_titlebar` rule for Steam.
- `hyprctl dispatch <classic-name>` is a syntax error under Lua. To focus a window, click
  its client area with `/tmp/opencode/relmouse2.py click <x> <y>`.
- A rule created with `hyprctl eval` is **not** removed by `hyprctl reload`; use it only on
  throwaway windows. Rules under test that must come back out go in `dynamic.lua`
  (back it up, edit, `hyprctl reload`, restore, reload).

## Review Focus

- A colour spelling `parseColor` rejects (`#ff0000`, `rgb(ff00ff`) or an alpha value the
  plugin cannot use (`rgba(ff00ff80)`): the window keeps the config colour, nothing crashes.
- A rule that sets only `color_face` and not `color_face_active`: the bar changes colour
  when the window gains focus, which is the point of the 1:1 mapping and looks like a bug
  if it is undocumented.
- `show_titlebar = false` with `show_border = true`: the bar goes, a 5px frame band stays,
  and the client's `at[1]` becomes 5 — not 0.
- A `show_*` rule toggled at a live window: the window must re-lay-out without a reload,
  without a positioner loop or a crash.
- A rule matching a window that opens later: applied at open, like any window rule.

---

### Task 1: Per-window config resolution, no rules yet

**Files:**
- Modify: `src/config.hpp`, `src/config.cpp`, `src/CCdeDecoration.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `CDE::SConfig CDE::configFor(PHLWINDOW window)` — global config today, per-window from
    Task 2. Public; `CDE::config()` is removed from the header.
  - Anonymous-namespace `SConfig globalConfig()` in `config.cpp`, the old `config()` body.

- [ ] **Step 1: Widen the declaration**

In `src/config.hpp`, replace the `SConfig config();` declaration with:

```cpp
// The palette for one window: current config values plus any window-rule overrides.
// A null window yields the global config unchanged.
SConfig configFor(PHLWINDOW window);
```

Rename the old function's comment accordingly. Keep `edgeHidden` / `titleHidden` as they
are for now.

- [ ] **Step 2: Move the body and add the wrapper**

In `src/config.cpp`, rename `SConfig config()` to a file-local `SConfig globalConfig()` in
the anonymous namespace, and add to `namespace CDE`:

```cpp
SConfig configFor(PHLWINDOW window) {
    // Window-rule overrides land here in the next task; the window is taken now so every
    // caller is already shaped correctly and the painters never see a global palette.
    (void)window;
    return globalConfig();
}
```

`globalConfig` reads the same `SP` values as before, so nothing about the clamps changes.

- [ ] **Step 3: Repoint the six call sites**

| File:line (before this task) | Becomes |
|---|---|
| `src/CCdeDecoration.cpp:82` `getPositioningInfo` | `const CDE::SConfig cfg = CDE::configFor(m_window.lock());` |
| `src/CCdeDecoration.cpp:205` `cornerAt` | `CDE::configFor(self).barHeight` |
| `src/CCdeDecoration.cpp:290` `retexture` | `const CDE::SConfig cfg = CDE::configFor(m_window.lock());` |
| `src/CCdeDecoration.cpp:327` `syncHover` | `CDE::configFor(m_window.lock()).showButtons` |
| `src/CCdeDecoration.cpp:489` `onInputOnDeco` | `CDE::configFor(m_window.lock()).showButtons` |
| `src/main.cpp:85` `outerBoxOf` | `CDE::configFor(window).barHeight` |

- [ ] **Step 4: Build**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
Expected: clean link, no warnings about `config`.

- [ ] **Step 5: Capture the baseline, then load one fresh copy and confirm health**

With the OLD build still loaded, capture the focused window's bar. `bar_height` is 26 in
this session; adjust `26` if `hyprctl getoption plugin:cde:bar_height` disagrees.

```sh
read AX AY < <(hyprctl activewindow -j | jq -r '"\(.at[0]) \(.at[1])"')
grim /tmp/opencode/t1-before.png
magick /tmp/opencode/t1-before.png -crop 200x26+$((AX+40))+$((AY-26)) +repage /tmp/opencode/t1-before-bar.png
```

Then swap:

```sh
for p in $(grep -o '[^ ]*libcde[^ ]*\.so' /proc/$(pgrep -f '^Hyprland')/maps | sort -u); do
  hyprctl plugin unload "$p"
done
hyprctl plugin list | grep -c 'Plugin cde'      # expect 0
cp build/libcde.so /tmp/opencode/libcde-task1.so
hyprctl plugin load /tmp/opencode/libcde-task1.so
hyprctl plugin list | grep -c 'Plugin cde'      # expect 1
hyprctl getoption plugin:cde:color_face         # expect a value, not "no such option"
```

If the count is not 1, or the option is missing, the session is in the zombie state from
AGENTS.md: stop, restart Hyprland, and repeat. Do not measure pixels until both checks pass.

- [ ] **Step 6: Prove pixel-neutrality against the baseline**

```sh
grim /tmp/opencode/t1-after.png
magick /tmp/opencode/t1-after.png -crop 200x26+$((AX+40))+$((AY-26)) +repage /tmp/opencode/t1-after-bar.png
magick compare -metric AE /tmp/opencode/t1-before-bar.png /tmp/opencode/t1-after-bar.png null: ; echo
magick /tmp/opencode/t1-after-bar.png -format '%k colours\n' info:
```

Expected with the dark palette: `0` differing pixels, `4 colours` (face, light, dark,
title), and the face pixel (`magick ... -format '%[pixel:p{80,13}]' info:`) still
`srgb(51,51,51)`. Any of those differing means the refactor moved a pixel.

- [ ] **Step 7: Commit**

```bash
git add src/config.hpp src/config.cpp src/CCdeDecoration.cpp src/main.cpp
git commit -m "refactor: resolve config per window through configFor()"
```

---

### Task 2: Twelve rules, 1:1 with config keys; three `show_*` options replace the `no_*` rules

**Files:**
- Modify: `src/config.hpp`, `src/config.cpp`, `src/main.cpp`, `README.md`
- Modify: `src/CCdeDecoration.cpp` (comments naming `cde:no_titlebar` / `cde:no_border`)

**Interfaces:**
- Consumes: `CDE::configFor(PHLWINDOW)` from Task 1.
- Produces:
  - `SConfig` gains `bool showTitlebar`, `bool showBorder`, `bool showTitle`, all `true`,
    declared after `showButtons`.
  - `CDE::edgeHidden(PHLWINDOW, uint32_t edge)` and `CDE::titleHidden(PHLWINDOW)` keep
    their signatures; `edgeHidden` returns the negation of `showTitlebar` for
    `DECORATION_EDGE_TOP` and of `showBorder` otherwise.
  - Twelve registered effects, named in `RULE_NAMES`.

- [ ] **Step 1: Write the failing check — the option does not exist yet**

Run: `hyprctl getoption plugin:cde:show_titlebar`
Expected: FAIL — `no such option: plugin:cde:show_titlebar`.

Do not touch `dynamic.lua` yet: the Lua backend validates `hl.config` keys against the
registered options, so an unknown name fails the whole reload.

- [ ] **Step 2: Add the options and the flags**

In `src/config.hpp`, add to `SConfig` after `showButtons`:

```cpp
// Whether each part of the chrome is drawn. Frame, bar and title text; the bar and
// frame reserve space when shown and reserve nothing when hidden.
bool showTitlebar = true;
bool showBorder   = true;
bool showTitle    = true;
```

In `src/config.cpp`, register:

```cpp
showTitlebar = makeShared<Config::Values::CBoolValue>("plugin:cde:show_titlebar", "Draw the title bar", true);
showBorder   = makeShared<Config::Values::CBoolValue>("plugin:cde:show_border", "Draw the left, right and bottom frame", true);
showTitle    = makeShared<Config::Values::CBoolValue>("plugin:cde:show_title", "Draw the title text", true);
```

Add them to the `all` vector and to `globalConfig()`'s designated initialisers, in
declaration order.

- [ ] **Step 3: Replace the old rules with option-backed flags**

In `src/config.cpp`, delete `g_noTitlebar`, `g_noBorder`, `g_noTitle`, the three
`registerEffect` calls and the three `unregisterEffect` calls. In the anonymous namespace,
add the flag helper over the three new option values:

```cpp
struct SChromeFlags {
    bool titlebar = true, border = true, title = true;
};

// Option values; Task 2's later steps overlay the matching window rules on top.
SChromeFlags chromeFlags(PHLWINDOW window) {
    SChromeFlags flags;
    flags.titlebar = showTitlebar->value();
    flags.border   = showBorder->value();
    flags.title    = showTitle->value();
    return flags;
}
```

and rewrite the two consumers:

```cpp
bool edgeHidden(PHLWINDOW window, uint32_t edge) {
    const SChromeFlags flags = chromeFlags(window);
    return edge == DECORATION_EDGE_TOP ? !flags.titlebar : !flags.border;
}

bool titleHidden(PHLWINDOW window) {
    return !chromeFlags(window).title;
}
```

`chromeFlags` takes the null window and null applicator safely when the rule overlay lands.
Update the header comments that still describe `cde:no_titlebar` / `cde:no_border` /
`cde:no_title`, and `CCdeDecoration`'s comments that name the same rules.

- [ ] **Step 4: Register the twelve effects and parse their values**

In `src/config.cpp` (add `#include <hyprland/src/config/shared/parserUtils/ParserUtils.hpp>`,
`#include <expected>` and `#include <optional>`):

```cpp
enum eRuleSlot : size_t {
    RULE_COLOR_FACE, RULE_COLOR_LIGHT, RULE_COLOR_DARK, RULE_COLOR_TITLE, RULE_COLOR_TITLE_SHADOW,
    RULE_COLOR_FACE_ACTIVE, RULE_COLOR_LIGHT_ACTIVE, RULE_COLOR_DARK_ACTIVE, RULE_COLOR_TITLE_ACTIVE,
    RULE_SHOW_TITLEBAR, RULE_SHOW_BORDER, RULE_SHOW_TITLE,
    RULE_COUNT
};

constexpr const char* RULE_NAMES[RULE_COUNT] = {
    "cde:color_face", "cde:color_light", "cde:color_dark", "cde:color_title", "cde:color_title_shadow",
    "cde:color_face_active", "cde:color_light_active", "cde:color_dark_active", "cde:color_title_active",
    "cde:show_titlebar", "cde:show_border", "cde:show_title",
};

Desktop::Rule::CWindowRuleEffectContainer::storageType g_rules[RULE_COUNT] = {};
```

`registerRules` loops `RULE_NAMES` into `g_rules`; `unregisterRules` loops `g_rules`.
`ruleEffect(window, slot)` returns the prop's `effect` string or `std::nullopt`.
`parseColour` uses `Config::ParserUtils::parseColor` and `has_value()`; `parseBool` lowercases
and returns false for `"0"`, `"false"`, `"no"`, `"off"`, true otherwise. Then in
`configFor`, after `globalConfig()`:

```cpp
applyColour(window, RULE_COLOR_FACE, cfg.face);
applyColour(window, RULE_COLOR_LIGHT, cfg.light);
applyColour(window, RULE_COLOR_DARK, cfg.dark);
applyColour(window, RULE_COLOR_TITLE, cfg.title);
applyColour(window, RULE_COLOR_TITLE_SHADOW, cfg.titleShadow);
applyColour(window, RULE_COLOR_FACE_ACTIVE, cfg.faceActive);
applyColour(window, RULE_COLOR_LIGHT_ACTIVE, cfg.lightActive);
applyColour(window, RULE_COLOR_DARK_ACTIVE, cfg.darkActive);
applyColour(window, RULE_COLOR_TITLE_ACTIVE, cfg.titleActive);
```

`applyColour` leaves the field untouched when the rule is absent or unparsable. Extend
`chromeFlags` with the same lookup so the rules win over the options, per flag:

```cpp
applyBool(window, RULE_SHOW_TITLEBAR, flags.titlebar);
applyBool(window, RULE_SHOW_BORDER, flags.border);
applyBool(window, RULE_SHOW_TITLE, flags.title);
```

- [ ] **Step 5: Invalidate on a live rule change**

In `src/main.cpp`, after the `config.reloaded` listener:

```cpp
// A rule change alters this window's palette and possibly which parts of the chrome
// exist. Geometry is Task 3; here it is pixels only.
keepListener(Event::bus()->m_events.window.updateRules, [](PHLWINDOW window) {
    CCdeDecoration::forEach([&](CCdeDecoration& decoration, PHLWINDOW owner) {
        if (owner != window)
            return;

        decoration.invalidate();
        decoration.damageBar();
    });
});
```

- [ ] **Step 6: Migrate the live config, then build and load**

The user's `~/.config/hypr/dynamic.lua` has `["cde:no_titlebar"] = true` for Steam, and the
Lua backend rejects unregistered rule names — so the new build cannot be loaded while that
line is still there, and `PLUGIN_INIT` calls `reloadConfig()`, so the rejection would happen
at load time. Unloading first is what makes this safe: the block is inside
`utils.is_plugin_loaded("cde")`, so it is skipped while the plugin is gone.

```sh
for p in $(grep -o '[^ ]*libcde[^ ]*\.so' /proc/$(pgrep -f '^Hyprland')/maps | sort -u); do
  hyprctl plugin unload "$p"
done
cp ~/.config/hypr/dynamic.lua ~/.config/hypr/dynamic.lua.cde-bak
```

In that file, change the Steam rule to `show_titlebar = false,`, then snapshot the migrated
file — the tests below add and remove rules in it, and the snapshot is what they restore:

```sh
cp ~/.config/hypr/dynamic.lua ~/.config/hypr/dynamic.lua.cde-migrated
cmake --build build && cp build/libcde.so /tmp/opencode/libcde-task2.so
hyprctl plugin load /tmp/opencode/libcde-task2.so
hyprctl plugin list | grep -c 'Plugin cde'          # expect 1
hyprctl getoption plugin:cde:show_titlebar          # expect bool, not "no such option"
hyprctl clients -j | jq -r '.[] | select(.class=="steam") | "\(.at) \(.size)"'
```

Expected: Steam `at` is `[489, 5]` — 5px top frame band, not 0 — and the reload reported no
error. If the load prints a Lua config error, the old rule name is still in the file.

- [ ] **Step 7: Verify a colour rule and the 1:1 focus semantics**

Put the test rule in `dynamic.lua` (back it up first), matching a throwaway class:

```lua
hl.window_rule({
    match = { class = "^cde-test$" },
    ["cde:color_face"] = "rgb(ff0000)",
})
```

Open a test window: `kitty --class cde-test -e sleep 99999 &`, wait for it, focus it, then:

```sh
read AX AY < <(hyprctl activewindow -j | jq -r '"\(.at[0]) \(.at[1])"')
grim /tmp/opencode/t2a.png
magick /tmp/opencode/t2a.png -format '%[pixel:p{'$((AX+80))','$((AY-13))'}]\n' info:
```

Expected: tan `srgb(237,168,112)` — the active face, because only the inactive face was
overridden. Click elsewhere to unfocus it, re-grab the shot at the same coordinates, expect
`srgb(255,0,0)`. That is the documented 1:1 behaviour, not a bug.

Then add `["cde:color_face_active"] = "rgb(00ff00)"`, reload, focus the test window and
expect `srgb(0,255,0)`.

- [ ] **Step 8: Verify the remaining Review Focus cases**

- Invalid value: change the rule to `"#ff0000"`, reload, expect the config colour
  `srgb(237,168,112)` on the focused window, no crash, no changed colour count.
- Rule on a window opened later: with the rule in place, close the test window and open a
  new one; it comes up red/green without a reload.
- `show_title` rule: `["cde:show_title"] = false` on the test window hides only the text;
  the bar, separators and drag rules stay, and the title field is all drag area.
- `show_border` rule: `["cde:show_border"] = false` removes the left/right/bottom strips;
  the client widens by 10px and its `at[0]` drops by 5.
- Alpha: `"rgba(ff00ff80)"` on `color_face` renders and does not crash.

Restore the migrated file and reload at the end: `cp ~/.config/hypr/dynamic.lua.cde-migrated
~/.config/hypr/dynamic.lua && hyprctl reload`. It must end up with `show_titlebar = false`
for Steam and no `cde-test` rule.

- [ ] **Step 9: Update the README rules section**

Replace the three-row table with the twelve rules, a line saying rule `cde:X` overrides
option `plugin:cde:X` for matching windows and that colour rules need the same spelling as
config colours, and a Lua example using the accepted syntax measured in Steps 7–8. Update
the config table with `show_titlebar` / `show_border` / `show_title`.

- [ ] **Step 10: Commit**

```bash
git add src/config.hpp src/config.cpp src/main.cpp src/CCdeDecoration.cpp README.md
git commit -m "feat: window rules 1:1 with every colour option and the new show_* options"
```

---

### Task 3: A `show_*` rule re-lays-out the window

**Files:**
- Modify: `src/CCdeDecoration.hpp`, `src/CCdeDecoration.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: `configFor`, `edgeHidden`, `titleHidden` from Task 2.
- Produces: `bool CCdeDecoration::chromeStateChanged() const` — public, true when the
  window's resolved `show_*` triple differs from the one its current raster was built for.

- [ ] **Step 1: Add the state and the cache key**

In `src/CCdeDecoration.hpp`, add a public method and a private field:

```cpp
// True when the window's resolved show_* flags no longer match the last raster, so the
// positioner has to re-run before that raster is correct.
bool chromeStateChanged() const;
```

```cpp
int m_lastChrome = -1;   // int, not uint8_t: -1 means "never rastered", like m_lastFocused
```

with a file-local encoder:

```cpp
static int chromeStateOf(const CDE::SConfig& cfg) {
    return (cfg.showTitlebar ? 1 : 0) | (cfg.showBorder ? 2 : 0) | (cfg.showTitle ? 4 : 0);
}
```

Set `m_lastChrome = chromeStateOf(cfg)` at the end of `retexture`. Add
`const bool sameChrome = m_lastChrome == chromeStateOf(CDE::configFor(m_window.lock()));` to
`textureIsCurrent`'s `return` so a missed event cannot leave a stale disposition forever.

- [ ] **Step 2: Compare in the callable form**

```cpp
bool CCdeDecoration::chromeStateChanged() const {
    return m_lastChrome != chromeStateOf(CDE::configFor(m_window.lock()));
}
```

Only bits 0 and 1 move geometry; the caller does not need to know that, because a title
change costs one extra positioner pass and no wrong pixels.

- [ ] **Step 3: Re-enter the positioner only when geometry moved**

In `src/main.cpp`'s `updateRules` listener, capture the answer before invalidating:

```cpp
keepListener(Event::bus()->m_events.window.updateRules, [](PHLWINDOW window) {
    CCdeDecoration::forEach([&](CCdeDecoration& decoration, PHLWINDOW owner) {
        if (owner != window)
            return;

        // Read before invalidate(): the comparison is against the last raster.
        const bool geometry = decoration.chromeStateChanged();

        decoration.invalidate();
        decoration.damageBar();

        if (geometry)
            g_pDecorationPositioner->onWindowUpdate(window);
    });
});
```

- [ ] **Step 4: Build, load, and measure the live toggle**

Build to `/tmp/opencode/libcde-task3.so`, load it, confirm health. With a `cde-test` window
open and focused, add `["cde:show_titlebar"] = false` to its rule and `hyprctl reload`.
Then eval the same rule at the live window to exercise the event path:

```sh
hyprctl eval 'hl.window_rule({ match = { class = "^cde-test$" }, ["cde:show_border"] = false })'
hyprctl clients -j | jq -r '.[] | select(.class=="cde-test") | "\(.at) \(.size)"'
```

Expected: `at[0]` drops by 5 and width grows by 10 **without a reload**, the window does not
move again on the next frame, and Hyprland does not crash. Repeat the eval-unload cycle by
restarting the test window instead of unloading the rule (eval'd rules persist).

- [ ] **Step 5: Decide the fallback, honestly**

If Step 4 shows a loop, a crash, or geometry that lands after a second `onWindowUpdate`:

- remove the `onWindowUpdate` call, keep `invalidate` + `damageBar`,
- add one line to the README rules section: geometry rules (`show_*`) apply on
  `hyprctl reload`; colour rules apply live.

Either way record what was measured in the commit message.

- [ ] **Step 6: Commit**

```bash
git add src/CCdeDecoration.hpp src/CCdeDecoration.cpp src/main.cpp README.md
git commit -m "feat: show_* rule changes re-run the positioner for the changed window"
```

---

### Task 4: Documentation, user-config migration, final sweep

**Files:**
- Modify: `AGENTS.md`, `README.md`, `~/.config/hypr/dynamic.lua` (user config — flag this
  in the handoff)
- Modify: any `src/` comment still naming `cde:no_*`

**Interfaces:**
- Consumes: the measured answers from Tasks 2–3 (rule value spelling, whether live geometry
  works).
- Produces: nothing.

- [ ] **Step 1: Rewrite `AGENTS.md`'s rule and config sections**

- `## Window rules`: twelve rules, the 1:1 mapping sentence, the value syntax that measured
  working, which rules are live and which need a reload if Task 3 fell back, and the
  existing measured warnings about eval'd rules surviving reload.
- `## Config options`: add `show_titlebar`, `show_border`, `show_title`.
- `## Roadmap`: item 1 (rule-settable colours) is now done — delete it.
- Every `cde:no_titlebar` / `no_border` / `no_title` mention in prose, including
  `## Architecture`, `## Status`, `## Verification` and gotcha text, becomes the `show_*`
  spelling. `grep -rn 'no_titlebar\|no_border\|no_title' AGENTS.md src README.md` must come
  back with nothing but deliberate history notes.

- [ ] **Step 2: Confirm the user's config carries no old names**

`~/.config/hypr/dynamic.lua` was migrated in Task 2 Step 6 (`show_titlebar = false` for
Steam). Re-check it and every other file under `~/.config/hypr`:

```sh
grep -rn 'cde:no_\|no_titlebar\|no_border\|no_title' ~/.config/hypr
```

Expected: nothing. A leftover rule name now fails the whole Lua reload, so a hit here is a
broken session, not a cosmetic one. The migrated snapshot is at
`~/.config/hypr/dynamic.lua.cde-migrated` — restore from that, never from `.cde-bak`, which
still carries the old `cde:no_titlebar` spelling.

- [ ] **Step 3: Re-run the pixel-neutrality check with no rules**

With `dynamic.lua` migrated and only its global options in force, take the same bar crop as
Task 1 Step 6. Expected: `4 colours`, face `srgb(51,51,51)`, identical to the Task 1 crop.
The plugin's chrome is unchanged for a user who sets no colour rules.

- [ ] **Step 4: Full live pass**

One window through all twelve rules in two batches (colours, then the three `show_*`), each
checked by pixel or by `hyprctl clients` as in Task 2 Step 8. All five Review Focus cases
pass; none is left "looks fine".

- [ ] **Step 5: Commit**

```bash
git add AGENTS.md README.md src
git commit -m "docs: twelve 1:1 window rules, show_* options, migration notes"
```
