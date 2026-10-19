# Tests

`general_tests` (`general/`, no Qt) and `qt_tests` (`qt/`, headless Qt harness) are Catch2 v3 suites;
shared helpers live in `support/`. Build and run them as `AGENTS.md` describes.

## Tags

Each case carries one tag naming the production unit it exercises, plus any cross-cutting tags that
apply. Unit tags are snake_case and follow the unit's name (`MapEdge` → `[map_edge]`,
`SelectionManager` → `[selection_manager]`); a test file's cases normally share its unit tag. Run a
tag with `./build-claude/general_tests "[map_edge]"`.

Cross-cutting tags:

- `[roundtrip]` — a format read → write (→ read) round-trip, usually byte-exact.
- `[cli]` — exercises the headless CLI library (`src/cli`, target `gecko_cli`).
- `[mcp]` — exercises the MCP server or its tools (`src/mcp`).
- `[scripting]` — Luau scripting: the runtime, `MapScriptApi` and what runs scripts.
- `[ssl]` — the SSL front end (`src/ssl`): preprocessor, parser, analysis, `.int` reading.
- `[undo]` — undo/redo behaviour.
- `[macos]` — macOS `.app` bundle handling (runs on every platform).
- `[performance]` — time budgets that guard operations which once regressed badly.
- `[.corpus]` — hidden; checks a whole script tree named by `GECK_SSL_CORPUS`.

Unit tags whose name does not say what they cover:

- `[map]` — `Map` and the `.map` reader/writer; `[map_object]` — `MapObject`.
- `[pro]` — `Pro` and the `.pro` reader/writer; `[pro_editor]` — the PRO editor widgets.
- `[dat]` — DAT archives (`DatReader` and the DAT2 file system); `[pal]` — `.pal` colour palettes;
  `[palette_model]` / `[palette_view]` / `[object_palette]` — the object/tile palette panels.
- `[exit_grid]` — exit-grid direction, art and placement logic.
- `[lua]` — the Luau runtime (`LuaScriptRuntime`, `LuaSandboxHost`).
- `[reachability]` — the shared reachability core (`editor/Reachability`).
