# Keyboard shortcuts — implementation plan

*Implementation plan for `docs/keybindings-plan.md`: the new default bindings, and the
Preferences page that makes them remappable. Three shippable slices; each one is useful on its
own and each is a separate PR.*

---

## 0. Corrections to the source doc (verified against the code)

Four things in `keybindings-plan.md`'s "Already-bound keys" table are wrong or incomplete. They
change what the slices below can safely claim.

| Claim in the doc | What the code actually does |
|---|---|
| `F11` / `F16` are bound (spatial-script dialog / kill-type) | **Not bound anywhere.** `F11` appears only in `SFMLWidget::convertQtKeyToSf()` (`src/ui/widgets/SFMLWidget.cpp:316`), which is a Qt→SFML key-code translation table, not a binding. Both keys are free. |
| — (missing) | **`P` is bound**: eyedropper, samples whatever is under the cursor into the matching palette (`src/ui/input/InputHandler.cpp:346`). Canvas-scoped already. |
| — (missing) | **`Space` is bound**: in "Draw edge" mode it flips which side the exit-grid bars sit on (`src/ui/input/InputHandler.cpp:352`). Tool-modal. |
| `Enter` is free | **`Enter` is already bound in `MarkExits` mode** — it finalizes the in-progress Draw-edge polyline (`src/ui/input/InputHandler.cpp:330`). The proposed `Enter` → Selection panel *must* stand down while that tool is active. |

One more, on the doc's design caveat ("typing `b` in a search field places scroll blockers"):
the risk is real but narrower than stated. `QLineEdit` (and `QSpinBox`, which embeds one)
accepts `QEvent::ShortcutOverride` for any key below `Qt::Key_Escape`, so a plain letter typed
into a panel's filter box is **not** stolen by a window-scoped `QAction`. The keys *are* stolen
when focus sits on a non-text widget — a palette grid, a `QTreeView`, a non-editable
`QComboBox`, a dock title bar. That is still a good enough reason to scope single-letter keys to
the canvas, and it means the existing `B` / `R` have the same latent bug today.

---

## 1. Final default table

Scope column: **App** = window-scoped `QAction` shortcut; **Canvas** = `QShortcut` on the
`SFMLWidget` with `Qt::WidgetWithChildrenShortcut`; **Tool** = stays inside `InputHandler`'s
tool state machine, not user-remappable (see §5).

| Key | Action id | Label | Scope | Slice |
|---|---|---|---|---|
| `Return` | `panel.selection.reveal` | Inspect selection (reveal Selection panel) | Canvas | 1 |
| `Alt+1` | `panel.mapInfo` | Map Information panel | App | 1 |
| `Alt+2` | `panel.selection` | Selection panel | App | 1 |
| `Alt+3` | `panel.scripts` | Scripts panel | App | 1 |
| `Alt+4` | `panel.tilePalette` | Tile Palette panel | App | 1 |
| `Alt+5` | `panel.objectPalette` | Object Palette panel | App | 1 |
| `Alt+6` | `panel.fileBrowser` | File Browser panel | App | 1 |
| `` Alt+` `` | `panel.log` | Log panel | App | 1 |
| `Ctrl+1` | `view.elevation1` | Elevation 1 | App | 2 |
| `Ctrl+2` | `view.elevation2` | Elevation 2 | App | 2 |
| `Ctrl+3` | `view.elevation3` | Elevation 3 | App | 2 |
| `S` | `tool.select` | Select mode | Canvas | 2 |
| `G` | `view.hexGrid` | Toggle hex grid | Canvas | 2 |
| `Home` | `view.centerOnPlayer` | Center on player start | Canvas | 2 |
| `F` | `view.fitMap` | Fit map in view | Canvas | 2 |
| `Ctrl+Shift+E` | `script.editSource` | Edit script source of selection | App | 2 |
| `B` | `tool.scrollBlockerRect` | Scroll-blocker rectangle | Canvas *(moved from App)* | 2 |
| `R` | `tool.rotate` | Rotate selected object | Canvas *(moved from App)* | 2 |
| `P` | `tool.pick` | Eyedropper | Canvas *(moved from InputHandler)* | 3 |

Decisions folded in:

- **`` Alt+` `` goes to the Log dock**, not the Script Console. The console is behind
  `GECK_SCRIPTING_ENABLED` (`MainWindow.cpp:1222`) and would leave the key dead in a
  scripting-off build; the Log dock is unconditional. The console keeps its View-menu entry.
- **`Return` vs numpad Enter.** A `QShortcut` on `Qt::Key_Return` does not catch
  `Qt::Key_Enter`. Register the reveal action with two `QShortcut` sinks; only the `Return` one
  is rebindable.
- **`B` and `R` move to canvas scope** in slice 2. This is a user-visible behaviour change —
  they stop firing when a palette grid or tree has focus — and it retires the
  `_rotateAction->setEnabled(...)` workaround at `MainWindow.cpp:999` that exists only because
  `R` is currently window-scoped.

---

## 2. Slice 1 — panel reveal + `Return` (no new infrastructure)

Everything lands in `MainWindow`. No `Settings` change, no new files.

### 2.1 Reveal-and-focus semantics

Today `addPanelToggleAction()` (`MainWindow.cpp:311`) makes a checkable menu action whose
`toggled` handler shows or hides the dock. "Toggle" is the wrong verb for a keyboard shortcut on
a **tabbed** dock: `Alt+1` on a Map Info dock that is visible-but-tabbed-behind Scripts would
hide it rather than surface it.

Add alongside it:

```cpp
// MainWindow.cpp
void MainWindow::revealPanel(QDockWidget* dock) {
    if (!dock) return;
    const bool onTop = dock->isVisible() && !dock->visibleRegion().isEmpty();
    const bool focused = dock->isAncestorOf(QApplication::focusWidget());
    if (onTop && focused) {          // already the active, focused tab -> hide
        dock->hide();
        return;
    }
    dock->show();                    // hidden, or visible but tabbed behind / unfocused
    dock->raise();
    if (QWidget* w = dock->widget()) w->setFocus(Qt::ShortcutFocusReason);
}
```

`dock->hide()` / `show()` both fire `QDockWidget::visibilityChanged`, which the existing handler
at `MainWindow.cpp:344` already uses to re-sync the menu action's check state and persist the
layout — so the menu checkmarks stay correct for free.

### 2.2 Wiring the `Alt+…` family

`addPanelToggleAction()` gains a `const QKeySequence& shortcut` parameter and calls
`revealPanel()` instead of the inline `showDock` lambda. `setupPanelsMenu()`
(`MainWindow.cpp:2308`) grows a `shortcut` field in its `PanelToggleSpec` array.

Ordering gotcha: `setupPanelsMenu()` runs from `setupMenuBar()`, but `_logDock` is not created
until `setupDockWidgets()` (`MainWindow.cpp:1242`). Attach `` Alt+` `` where the log action is
already configured — `MainWindow.cpp:1252`, right after
`logAction->setText(tr("&Log"))` — using `_logDock->toggleViewAction()`, and route it through
`revealPanel()` rather than the raw toggle action so it gets the same reveal semantics.

### 2.3 `Return` → reveal Selection panel

```cpp
// after the SFMLWidget exists (EditorWidget construction / setCurrentEditorWidget)
_inspectSelectionShortcut = new QShortcut(QKeySequence(Qt::Key_Return), sfmlWidget);
_inspectSelectionShortcut->setContext(Qt::WidgetWithChildrenShortcut);
connect(_inspectSelectionShortcut, &QShortcut::activated, this, [this] {
    if (!_currentEditorWidget || !_currentEditorWidget->hasSelection()) return;  // no-op, per plan
    revealPanel(_selectionDock);
});
```

Plus a second `QShortcut` on `Qt::Key_Enter` sharing the same lambda.

**Must be disabled in `MarkExits` mode**, or it eats the Draw-edge finalize. `syncToolModeActions()`
(`MainWindow.cpp:959`) already runs on every mode change and already does exactly this dance for
`_rotateAction`; add:

```cpp
if (_inspectSelectionShortcut) {
    _inspectSelectionShortcut->setEnabled(mode != EditorMode::MarkExits);
}
```

`QShortcut` with `WidgetWithChildrenShortcut` only fires while the canvas has focus, so a
`Return` pressed in a panel's filter box is untouched.

**Files:** `src/ui/core/MainWindow.{h,cpp}`.
**Test:** extend `tests/qt/test_ui_regressions.cpp:751` ("MainWindow panel toggles stay wired in
the no-map layout") with reveal-semantics cases — hidden → shows+raises; tabbed-behind → raises
without hiding; visible+focused → hides.

---

## 3. Slice 2 — editor navigation keys

Still no registry; these are direct bindings that slice 3 later re-points at the table.

| Binding | Where the behaviour already lives |
|---|---|
| `Ctrl+1/2/3` | `_elevation1Action`…`_elevation3Action` already exist (`MainWindow.cpp:673-688`). One-line `setShortcut()` each. They are `setDisabled(true)` until `updateElevationMenu()` enables the elevations the map actually has — a shortcut on a disabled action is correctly a no-op. |
| `Home` | `EditorWidget::centerViewOnPlayerPosition()` already exists (`EditorWidget.cpp:2876`). Canvas `QShortcut` → call it. |
| `S` | `_selectToolAction` already exists (`MainWindow.cpp:861`). Do **not** `setShortcut()` on it (that would be window scope); add a canvas `QShortcut` that calls `_selectToolAction->trigger()`. |
| `G` | Same shape via `_showHexGridAction->toggle()`. |
| `Ctrl+Shift+E` | Reuses the Scripts-panel / Map-Info "Edit Script Source" path from the SSL toolchain work; resolves the selected object's script and hands off to `ScriptSourceService`. Enabled only when the selection has a script. |

### 3.1 `F` — fit map in view (the only new behaviour)

`ViewportController` has `centerViewOnMap()` and `setZoomLevel()` but no fit-to-extent. Add:

```cpp
// src/viewport/ViewportController.{h,cpp}
void fitMapInView();   // zoom so the whole 100x100 tile grid fits, then re-center
```

The tile grid spans roughly **7920 × 3576** world px (from `Constants.h`: `MAP_WIDTH/HEIGHT` 100,
`TILE_X_OFFSET` 48, `TILE_Y_OFFSET_LARGE` 32, `TILE_Y_OFFSET_SMALL` 24, `TILE_Y_OFFSET_TINY` 12,
plus `TILE_WIDTH` 80 / `TILE_HEIGHT` 36). At a 1600×900 viewport the required zoom is
`min(1600/8000, 900/3612) ≈ 0.20` — comfortably inside the existing `MIN_ZOOM = 0.1` clamp, so
no clamp change is needed. Derive the extents from the same `TileUtils` math
`centerViewOnMap()` uses rather than hardcoding, so the two cannot drift.

Unit-testable headless (no GL): `tests/general/test_viewport_controller.cpp` already exercises
this class.

### 3.2 Migrating `B` and `R` to canvas scope

Drop the `QKeySequence("B")` at `MainWindow.cpp:482` and the `QKeySequence("R")` at
`MainWindow.cpp:828`; replace with canvas `QShortcut`s triggering the same actions. Then delete
the `_rotateAction->setEnabled(...)` guard at `MainWindow.cpp:999` and update the comment at
`InputHandler.cpp:339-342`, which documents the workaround — `R` will now reach the viewport
without the toolbar action being disabled first.

**Files:** `src/ui/core/MainWindow.{h,cpp}`, `src/viewport/ViewportController.{h,cpp}`,
`src/ui/input/InputHandler.cpp` (comment only).
**Tests:** `tests/general/test_viewport_controller.cpp` (fit math, clamp behaviour);
`tests/qt/test_ui_regressions.cpp` (elevation shortcuts are no-ops on a map without that
elevation).

---

## 4. Slice 3 — central table + Preferences page

This is `PLAN.md` "Editor limitations §7" and the prerequisite for §8 (customizable toolbar).

### 4.1 New files

```
src/ui/input/ActionSpec.h              # the default table (id, label, category, keys, scope)
src/ui/input/KeyBindingRegistry.h/.cpp # overrides, conflicts, persistence, live rebind
src/ui/widgets/KeybindingsWidget.h/.cpp# the Preferences tab
tests/qt/test_keybindings.cpp
```

All four get added to `src/CMakeLists.txt` (explicit source lists — no globbing) and
`tests/CMakeLists.txt`'s `qt_tests` target.

### 4.2 The table

```cpp
// src/ui/input/ActionSpec.h
namespace geck {

enum class ActionScope { Application, Canvas };

struct ActionSpec {
    const char* id;          // stable + persisted — never renamed once shipped
    const char* label;       // "Selection Panel"
    const char* category;    // "Panels" | "File" | "Edit" | "View" | "Navigation" | "Tools"
    const char* defaultKeys; // QKeySequence portable text, "" = unbound by default
    ActionScope scope;
};

std::span<const ActionSpec> actionSpecs();

} // namespace geck
```

String ids rather than an enum because they are what lands in `settings.json`; a reordered enum
would silently repoint every user's overrides.

### 4.3 Registry

```cpp
class KeyBindingRegistry : public QObject {
    Q_OBJECT
public:
    explicit KeyBindingRegistry(std::shared_ptr<Settings> settings, QObject* parent = nullptr);

    QKeySequence shortcut(QStringView id) const;         // override if set, else default
    QKeySequence defaultShortcut(QStringView id) const;
    bool isCustomized(QStringView id) const;

    // Empty result = no conflict. Canvas actions are checked against Application actions too:
    // a window-scoped QAction shortcut fires even while the canvas has focus, so the two
    // scopes are not independent namespaces.
    QString conflictingActionId(QStringView id, const QKeySequence& seq) const;

    void setShortcut(QStringView id, const QKeySequence& seq);  // empty sequence = unbound
    void resetToDefault(QStringView id);
    void resetAllToDefaults();
    void save();

    // Sinks: whatever is registered here is re-keyed automatically on rebind.
    void bind(QStringView id, QAction* action);
    void bind(QStringView id, QShortcut* shortcut);

signals:
    void bindingChanged(const QString& id, const QKeySequence& seq);
};
```

`bind()` holds `QPointer` sinks and connects `bindingChanged` to `setShortcut()` /
`setKey()`, so a rebind takes effect immediately with no restart and no re-walk of the menu bar.
Owned by `MainWindow` (constructed next to `_settings`), injected into `SettingsDialog`.

Migration is mechanical: every `QKeySequence("…")` literal in `setupMenuBar()`,
`setupToolBar()`, `setupPanelsMenu()` and the slice-1/2 `QShortcut`s becomes
`registry.bind(actions::X, action)` — the registry supplies the key. Slices 1 and 2 are written
so this is a find-and-replace, not a redesign.

### 4.4 Persistence

Add to `Settings`:

```cpp
QMap<QString, QString> getKeyBindings() const;               // actionId -> portable text
void setKeyBindings(const QMap<QString, QString>& bindings);
```

Stored as a top-level `"keyBindings"` object. **Write only entries that differ from the
default** — that way changing a default in a later release still reaches users who never
customized that action. An explicitly-unbound action persists as an empty string, which is
distinct from absent.

No `SETTINGS_VERSION` bump: an absent `"keyBindings"` key means "no overrides", which
`Settings::fromJson()` already tolerates. Avoid touching the version — the 1.0→1.1 data-path
migration at `Settings.cpp:178` keys off `_version != SETTINGS_VERSION` and would re-run
`expandDataPaths()` on every existing install if the constant moved.

### 4.5 Preferences page

`src/ui/widgets/KeybindingsWidget` follows the shape of `DataPathsWidget` / `ScriptToolsWidget`
(emit `changed()` + `statusChanged()`, edit an in-memory copy, commit on Apply/OK), and
`SettingsDialog` gains `setupKeybindingsTab()` in `setupTabs()` (`SettingsDialog.cpp:74`). The
registry pointer joins the constructor: `SettingsDialog(settings, registry, parent)`, null →
tab omitted, so a bare-constructed dialog in a test still builds.

Layout:

- Filter `QLineEdit` at the top (matches label, category, and current keys).
- `QTreeWidget` grouped by category — columns **Action / Shortcut / Default**, customized rows
  shown bold with a per-row reset affordance.
- Double-click the Shortcut cell → inline `QKeySequenceEdit` with
  `setMaximumSequenceLength(1)` (single chord; multi-chord sequences are not wanted here).
- Live conflict detection on `keySequenceChanged`: on a hit, mark the row with
  `ui::theme::colors::ERROR` and set the status line to
  *"Alt+2 is already assigned to Selection Panel"*, with a **Reassign** button that unbinds the
  other action. Escape reverts the edit.
- **Reset** (selected) and **Reset All** buttons.

Use `ui::theme::spacing::*` and `ui::theme::styles::status*()` per `CLAUDE.md` — no hardcoded
colours or paddings.

### 4.6 Status-bar hints

`hintForContext()` (`src/ui/core/EditorHints.h`) hardcodes key names and is explicitly
documented as "kept in lockstep with InputHandler's handlers and the toolbar shortcuts". Once
keys are remappable that comment becomes a lie. Give it a key-lookup callable
(`std::function<QString(QStringView actionId)>`) so it stays pure and headless-testable while
following rebinds. Small, and it keeps `tests/qt/test_editor_hints.cpp` meaningful.

---

## 5. What stays hardcoded, and why

`InputHandler`'s remaining keys are **tool state-machine keys**, not commands: `Esc` (cancel /
abandon the Draw-edge line), `Enter` (finalize that line), `Space` (flip the edge side), `R`
while stamping (cycle pattern variant), `Delete`/`Backspace`. They are meaningful only inside a
specific mode, they are dispatched in SFML key codes rather than `QKeySequence`, and rebinding
them independently of their mode would produce incoherent states.

Recommendation: leave them out of the registry and surface them in the Preferences page as a
**read-only "Tool keys" section**, so they are discoverable without being editable. `P`
(eyedropper) is the exception — it is a global canvas command wearing a tool key's clothes, so
migrate it to a canvas `QShortcut` in slice 3 and make it rebindable.

If tool keys are wanted as rebindable later, the migration path is a `CanvasShortcutDispatcher`
consulted from `SFMLWidget::keyPressEvent()` *before* the Qt→SFML translation, returning
handled/not-handled so unclaimed keys fall through to `InputHandler` unchanged.

---

## 6. Tests

Added to `qt_tests` as `tests/qt/test_keybindings.cpp`:

1. **Table integrity** — ids unique; every `defaultKeys` parses to a non-empty `QKeySequence`
   (or is deliberately `""`); no two Application-scope defaults collide; no Canvas default
   collides with an Application default.
2. **Persistence round-trip** — set an override, `save()`, reload a fresh `Settings`, read it
   back; confirm only the changed entry is written and a reset removes it from the JSON.
3. **Conflict detection** — `conflictingActionId()` finds the collision, returns empty for the
   action's own id, and treats an empty sequence as never conflicting.
4. **Live rebind** — a `bind()`-registered `QAction` and `QShortcut` both pick up a new key
   without re-registration.
5. **Reveal semantics** (slice 1) — hidden → show+raise; tabbed-behind → raise; visible+focused
   → hide.
6. **`Return` stands down in `MarkExits`** — the guard that keeps the Draw-edge finalize working.
7. **Fit-map math** (slice 2, in `tests/general/test_viewport_controller.cpp`) — the whole grid
   is inside the view afterwards, and the zoom stays within the clamp.

Existing suites that must stay green: `tests/qt/test_input_handler.cpp` (unchanged in slices 1–2;
slice 3 touches it only if `P` moves), `tests/qt/test_editor_hints.cpp` (signature change in
§4.6), `tests/qt/test_ui_regressions.cpp:751`.

---

## 7. Sequencing and effort

| Slice | Scope | Files | Rough size |
|---|---|---|---|
| 1 | `Return` + `Alt+1…6` + `` Alt+` `` reveal-and-focus | `MainWindow.{h,cpp}` | ~150 lines + tests |
| 2 | Elevation, `S`, `G`, `Home`, `F`, `Ctrl+Shift+E`; `B`/`R` → canvas | `MainWindow.{h,cpp}`, `ViewportController.{h,cpp}` | ~250 lines + tests |
| 3 | Registry + Settings + Preferences page; migrate all literals | 4 new files, `MainWindow`, `SettingsDialog`, `Settings`, `EditorHints` | ~900 lines + tests |

Slices 1 and 2 are independently shippable and answer the motivating gap. Slice 3 is the one
that pays down `PLAN.md` §7 and unblocks §8 (customizable toolbar shares the same table).

Per `CLAUDE.md`: run `./format.sh` before committing, open a PR per slice, wait for CI, and
address Copilot / SonarCloud comments before considering a slice done.

---

## 8. Open decisions

1. **`Ctrl+Shift+E` behaviour when the selection has no script** — no-op, or offer to attach one?
   Recommend no-op with a status-bar message, matching the `Return` no-op convention.
2. **Should slice 3 ship a "Keyboard shortcuts" reference (printable list)?** Cheap once the
   table exists — a read-only view of the same tree — and it is the discoverability half of the
   motivating gap. Recommend yes, in the Help menu.
3. **macOS `Alt` = Option.** `Alt+digit` does not collide with menu mnemonics on macOS (there
   are none) and `QLineEdit` will not see it as text, so the family is safe; worth a manual
   check on the first macOS build since Option-digit types glyphs in some contexts.
