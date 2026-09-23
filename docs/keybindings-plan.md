# Keyboard shortcuts plan

*Proposal for the default keybindings the editor should ship, and the one gap that motivated it:
there is currently **no keyboard shortcut to show/focus any panel** — only the `View › Panels` menu
mnemonics. This doc is the source list for the default bindings; every one is intended to be
user-remappable through the configurable-keybindings work package (see the last section).*

## Already-bound keys (do not collide)

| Key | Action |
|---|---|
| `Ctrl+N` / `Ctrl+O` / `Ctrl+B` | New map / Open map / Browse Maps |
| `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+W` | Save / Save As / Close map |
| `Ctrl+,` / `Ctrl+Q` | Preferences / Quit |
| `Ctrl+A` / `Ctrl+D` | Select All / Deselect All |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+E` | Show Exit Grids (view toggle) |
| `B` | Scroll-blocker rectangle mode |
| `R` | Cycle stamp variant |
| `F5` | Save & Play in Fallout 2 |
| `F11` / `F16` | Spatial-script dialog / kill-type (legacy) |
| `Esc` | Cancel current mode |
| `Delete` / `Backspace` | Delete selected objects |

Everything proposed below avoids these.

---

## 1. The motivating case: selected object → Selection panel

Bind this two ways, because they serve different moments:

- **`Enter` / `Return` — "inspect the selection."** After clicking a critter/object on the canvas,
  `Enter` shows *and* focuses the Selection panel (raising it if hidden or tabbed behind another
  dock). No number to remember — it reads as "open what I just selected," and is a no-op when
  nothing is selected. This is the binding that fits the scenario most naturally.
- **`Alt+2` — direct toggle of the Selection panel** regardless of selection (part of the numbered
  family below).

## 2. Panel family — `Alt+1…6`, `` Alt+` ``

One consistent, discoverable group; each toggles **and** focuses its dock.

| Key | Panel |
|---|---|
| `Alt+1` | Map Information |
| `Alt+2` | Selection |
| `Alt+3` | Scripts |
| `Alt+4` | Tile Palette |
| `Alt+5` | Object Palette |
| `Alt+6` | File Browser |
| `` Alt+` `` | Log / Script Console |

`Alt+digit` is chosen deliberately so `Ctrl+digit` stays free for elevation, and so it rarely
collides with text entry.

## 3. Other high-value gaps

Not panels, but the shortcuts a map editor is expected to have and Gecko currently lacks.

| Key | Action | Why |
|---|---|---|
| `Ctrl+1` / `Ctrl+2` / `Ctrl+3` | Switch to elevation 1 / 2 / 3 | No elevation shortcut exists today — the biggest everyday gap |
| `S` | Return to Select mode | Complements the existing `B` / `R` canvas keys; faster than `Esc` |
| `G` | Toggle grid | Universal editor convention |
| `Home` | Center view on player start | Fast "where's the entrance" jump |
| `F` | Fit whole map in view | Quick zoom-to-extent |
| `Ctrl+Shift+E` | Edit Script Source of the selected object's script | Jump from a scripted critter straight to its `.ssl` (ties into the SSL toolchain work) |

---

## Design caveats to build in

- **Scope single-letter keys to the canvas.** `S` / `G` (like the existing `B` / `R`) must fire
  only when the map view has focus, not while typing in a panel's filter box or a spin field —
  otherwise typing "b" in a search field places scroll blockers. These should be editor-widget
  shortcuts, not application-global `QAction`s.
- **Toggle + focus semantics for panels.** A panel shortcut should raise a hidden/tabbed dock and
  give it focus; pressing it again may hide it. "Show and focus" matters more than "toggle" for the
  `Enter`-on-selection case.

## Relationship to the configurable-keybindings work package

This list is the intended set of **defaults** for the central command/action table proposed in
`PLAN.md` (Editor limitations §7): a stable `action id → default QKeySequence + label/category`
table, a Preferences page to rebind with live conflict detection, and persistence via `Settings`,
driving both the menu/toolbar `QAction`s and the `InputHandler` dispatch. Rather than scattering
more `QKeySequence` literals through `MainWindow::setupMenuBar()` and `InputHandler`, these bindings
should land as rows in that table so they are discoverable and remappable from day one.

### Suggested sequencing

1. **Cheapest first slice (standalone):** `Enter` → reveal+focus the Selection panel, and the
   `Alt+1…6` / `` Alt+` `` panel family. Directly answers the motivating gap; no new infrastructure.
2. **Editor-navigation slice:** elevation `Ctrl+1/2/3`, `S` select, `G` grid, `Home` center, `F` fit
   — all canvas-scoped.
3. **Full slice:** fold everything above into the central keybinding table + Preferences rebind UI,
   so the defaults become user-remappable.
