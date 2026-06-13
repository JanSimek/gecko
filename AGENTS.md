# Repository Guidelines

## Project Structure & Module Organization
- `src/` main C++20 application modules (Qt UI under `ui/`, shared runtime resource services under `resource/`, loaders in `state/loader`, VFS bits under `vfs/`).
- `src/CMakeLists.txt` builds two reusable libraries: `gecko_app` for the Qt/SFML editor code (also linked into `qt_tests`) and `vault` for file formats, readers, and writers.
- `tests/` includes Catch2 suites (`general/`, `performance/`) plus Qt regression coverage under `tests/qt/`; data fixtures live in `tests/data/`, with additional prototype fixtures under `tests/prototype/`.
- `resources/` holds runtime assets; drop `master.dat` and `critter.dat` here before packaging.
- `format.sh` hosts formatting automation; `cmake/` and `vendor/` provide toolchain modules and bundled deps.
- `docs/` and `reference/` collect design notes and reverse-engineered file specs.

## Build, Test, and Development Commands
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` configures the project with Qt6, SFML, and bundled libraries.
- `cmake --build build --target gecko` compiles the editor; append `--config Release` on multi-config IDEs.
- `ctest --test-dir build --output-on-failure` runs the registered Catch2 executables plus `qt_tests` after a build.
- `./format.sh` applies clang-format across `.cpp` and `.h` files; run before committing.
- `cmake --build build --target general_tests performance_tests qt_tests` rebuilds the current test executables when iterating.

## Coding Style & Naming Conventions
- Formatting follows `.clang-format` (WebKit baseline): 4-space indent, attached braces, unsorted includes, aligned trailing comments.
- Prefer PascalCase for types (`Application`, `EditorWidget`) and camelCase for methods/variables; keep namespaces lowercase as in `geck::`.
- Group headers by module (`ui/*`, `state/*`) and avoid transitive includes; favor RAII and smart pointers already in use.
- For Qt UI work, reuse `src/ui/common/BaseWidget.h` and `src/ui/common/BasePanel.h`, pull colors/spacing/styles from `src/ui/theme/ThemeManager.h`, and use MIME constants from `src/ui/dragdrop/MimeTypes.h` instead of hardcoded strings.

## Engine Data Fidelity
- Treat Fallout 2 engine behavior and shipped data files as the source of truth for game-facing values.
- Prefer parsing runtime data such as `proto.msg`, `perk.msg`, `stat.msg`, and related files through existing loaders/helpers instead of hardcoding duplicate label or enum tables in UI code.
- Preserve stored numeric IDs exactly as the engine does; widget labels may differ from internal values, so combos and editors must map labels to engine values rather than relying on `currentIndex()` unless the format truly stores a contiguous index.
- Do not add fallback label tables, placeholder enum names, or substitute values when required engine data is missing or incomplete. Fail explicitly and fix the data path or parser instead.
- When behavior is unclear, verify it against `/Users/jansimek/Development/fallout2-ce` before introducing new constants, remapped enums, or editor-only interpretations of engine data.
- Runtime asset access goes through injected `resource::GameResources` (`src/resource/GameResources.h`, `src/resource/README.md`); pass it through constructors like `EditorWidget(resource::GameResources&, ...)` rather than adding global accessors or direct filesystem parsing in UI code.

## Testing Guidelines
- Coverage currently relies on the `general_tests`, `performance_tests`, and `qt_tests` executables.
- Name new specs `test_<feature>.cpp` under the relevant suite (`tests/general/`, `tests/performance/`, or `tests/qt/`) and add them to `tests/CMakeLists.txt`.
- Provide representative fixtures in `tests/data/`; update copy rules if new directories are needed.
- `qt_tests` links against `gecko::app`; keep UI regressions compatible with the headless harness in `tests/qt/qt_test_main.cpp`, which sets a temporary HOME and enables offscreen Qt where needed.
- Run `ctest` locally before PRs; ensure tests pass with `CI=1` environment to mirror CI registration.

## Commit & Pull Request Guidelines
- Follow the existing imperative subject format (`Add armor preview widgets...`); keep subjects under ~72 chars and omit trailing periods.
- Each commit should bundle a cohesive change and include formatting updates if they touch edited files.
- Pull requests should summarize behavior changes, link tracking issues, and note manual test steps; include screenshots or GIFs for UI tweaks.
- Confirm Release builds succeed (`cmake --build ... --config Release`) and mention any asset requirements in the description.

## Asset & Configuration Tips
- macOS builds require `export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6:$CMAKE_PREFIX_PATH"` before configuring.
- Place Fallout 2 DAT archives in `resources/` or configure alternate paths via the application settings dialog.
