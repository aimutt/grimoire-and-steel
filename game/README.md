# Grimoire & Steel — game system (C++ / VS2022)

Private. Three parts share a core library:
- `core/`    — **gns_core** static lib: SQLite access, domain model, rules engine, module/save I/O (no UI).
- `engine/`  — **GameEngine** app (SDL2 + Dear ImGui) — plays modules.
- `creator/` — **ModuleCreator** app (SDL2 + Dear ImGui) — authors modules.
- `tests/`   — gns_core unit tests.

`gns.db` is the read-only rules/content source (copied to each app's `data/` at build).
Modules (`.gnsmod`) and saves (`.gnssav`) are separate SQLite files (defined in milestone M2).

## Dependencies (all vendored — no package manager)
- `third_party/SDL2`, `third_party/SDL2_image` — prebuilt VC dev libraries.
- `third_party/sqlite3` — amalgamation (`sqlite3.c/.h`).
- `third_party/imgui` — Dear ImGui + SDL2/SDL_Renderer backends.

To re-fetch sqlite3 + Dear ImGui: `py third_party/_fetch_deps.py`.
(SDL2/SDL2_image were downloaded from the libsdl.org GitHub releases as `*-devel-*-VC.zip`.)

## Build & run
Open the `game/` folder in **Visual Studio 2022** (File ▸ Open ▸ Folder) — it picks up
`CMakePresets.json` automatically. Or from a terminal:

```sh
cmake -S game -B game/build -G "Visual Studio 17 2022" -A x64
cmake --build game/build --config Debug
game/build/engine/Debug/GameEngine.exe
ctest --test-dir game/build -C Debug          # runs gns_tests
```

## Status
- **M0 (done):** build pipeline + vertical slice — GameEngine window lists live `gns.db` data.
- **M1 (done):** `gns_core` domain model + rules engine —
  - `Dice` (NdX+Y / range parsing, seedable RNG),
  - `Repository` (typed DAOs over gns.db: abilities, adjustments, races, classes, class levels,
    saves, attack tables, strength capacity, weapons, armor, monsters, monster XP, wandering,
    turn-undead, treasure),
  - `Character` (3d6 creation, prime-req XP bonus, HP from class hit die + CON, saves by class
    group, racial movement),
  - `Rules` (to-hit via attack tables, damage, saving throws, encumbrance/movement, turn undead,
    monster XP, weighted wandering selection, treasure generation).
  - `tests/` covers all of the above against the real gns.db — `ctest` green.
- **M2 (in progress):** module SQLite format + I/O — `gns_core` `Module` model (`Module.h`:
  maps as fine grids with per-cell terrain + area ids, areas with DM/player text and
  monster/treasure/trap/lock/hidden stats, control points, area prerequisites) and
  `saveModule`/`loadModule` for `.gnsmod` files (`ModuleIO.cpp`). `.gnsmod` is plain SQLite
  (cells stored as BLOBs) — opens directly in `tools/db_console`. Round-trip covered in
  `tests/` (`ctest` green). Saves (`.gnssav`) still to come.
- **M3 (in progress):** Module Creator — mouse-driven grid map editor (paint terrain/areas,
  coarse overlay grid with A1/C4 labels, control-point placement), full per-area authoring
  inspector, monster picker sourced from `gns.db`, and New/Open/Save of `.gnsmod` via native
  file dialogs.
- **Next:** finish M2/M3 (artwork preview, `.gnssav`), then M4 (Game Engine), M5 (polish).
  See the plan for details.
