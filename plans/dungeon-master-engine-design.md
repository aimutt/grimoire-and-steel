# Design: The Game Engine as Dungeon Master

## Context

For *Grimoire & Steel*, the Game Engine (`game/engine/`) is meant to **play the role of the
Dungeon Master**: take a loaded `.gnsmod` module plus the read-only `gns.db` rules/content and
run a turn-based session — narrating areas, controlling monsters/NPCs, adjudicating the rules,
revealing the map as the party explores, tracking plot progress, and saving/restoring the game.

Today that role does not exist in code. `GameEngine` is still the **M0 vertical slice**: a single
ImGui window that lists monsters and spells from `gns.db` (`game/engine/src/main.cpp`). The pieces
the DM needs are real but unconnected:

- **Rules engine is done (M1)** — `gns_core` already adjudicates the hard parts: to-hit via attack
  tables, damage, saving throws, encumbrance/movement, turn undead, monster XP, weighted wandering
  selection, and treasure generation (`game/core/include/gns/Rules.h`, `Character.h`, `Dice.h`,
  `Repository.h`). `gns.db` is fully populated (spells, ~70+ monsters, weapons, armor, classes,
  races, saves, treasure types, wandering tables by environment+party level).
- **Module format exists (M2)** — `.gnsmod` carries maps (fine terrain grid + per-cell area ids),
  per-area DM/player text, per-area monster/treasure/trap/lock/hidden **percent chances**,
  control points/items, and area prerequisites (`game/core/include/gns/Module.h`,
  `ModuleIO.cpp`). The Module Creator (M3) authors all of this.

The gap is the **runtime**: there is no Session/Party/PlayState model, no combat loop, no
encounter resolution wiring, no fog-of-war, no `.gnssav`. The design doc
(`Grimore-and-Steel-Design.txt`) already commits to a **procedural DM**: the engine drives play
from the module's authored text + per-area stats + the `gns.db` rules. This document maps the
classic DM roles onto concrete engine subsystems, adds an **optional, pluggable LLM layer** (off
by default, fully offline without it) for the creative narration/NPC side, and names what the
module format still needs.

This is an **architecture blueprint to guide M4**, not a single code slice. The recommended first
slice is listed at the end.

---

## The five DM roles → engine subsystems

A DM is Storyteller, Narrator, Actor, Arbiter/Referee, and Pacing/Coordinator (per the Wikipedia
and dndduet references). The engine reproduces each as a subsystem in `gns_core` (UI-free, testable)
that the SDL/ImGui app renders:

| DM role | What it means at the table | Engine subsystem (new, in `gns_core`) | Drives from |
|---|---|---|---|
| **Referee / Arbiter** | Resolve actions by the rules, consistently | `RulesAdjudicator` — thin orchestrator over existing `Rules`/`Dice`/`Repository` | `gns.db` (already complete) |
| **Storyteller** | Track plot, gate progress, win/lose | `PlotTracker` over `Module::controlPoints` + `prerequisiteControlPointIds` | module control points |
| **Narrator** | Describe what the party perceives | `Narrator` — selects authored `playerText`/`dmText`, formats rule outcomes | module area text; optional LLM embellishment |
| **Actor** | Voice NPCs/monsters, set reactions | `EncounterDirector` + reaction rolls; optional LLM dialogue | per-area stats + `gns.db` monsters; reaction table |
| **Pacing / Coordinator** | Turn order, when things happen, fog of war | `Session` state machine + `MapVisibility` | runtime play-state |

### 1. Referee — `RulesAdjudicator` (deterministic core)
A stateless façade that the session calls to resolve every player action against `gns_core`'s
existing functions. It does **not** reimplement rules — it routes:
- attack → `Rules::characterAttack` / `monsterAttack`
- saving throw → `Rules::saveNeeded` + `rollSave`
- search/listen/locks/traps → roll a `Dice::percent(...)` against the **area's authored chance**
  (`monsterChancePct`, `trapChancePct`, `lockChancePct`, `hiddenChancePct`) and narrate the result
- wandering check → `Rules::rollWandering(environment, partyLevel)`
- treasure → `Rules::rollTreasure(code, ...)`

Determinism: the session owns a single seeded `Dice` (already seedable) so a save can store the RNG
state and replays/tests stay reproducible. **This is the irreducible heart of being a DM and it is
almost entirely already built** — M4 is mostly wiring, not new rules.

### 2. Storyteller — `PlotTracker`
Holds the set of completed control-point ids for the current game. Provides:
- `isAreaEnterable(areaId)` — all `area.prerequisiteControlPointIds` satisfied?
- `complete(controlPointId)` — mark a milestone/item acquired; re-narrate, unlock gated areas
- win/lose detection: reaching `Module::endAreaId`, or (future) a designated control point
Per the design doc, **if a module defines no control points, plot tracking is simply inert** — the
tracker must no-op gracefully so freeform/sandbox modules still play.

### 3. Narrator — `Narrator`
Turns game events into the text the player sees. Two layers:
- **Authored layer (always on):** on area entry, emit `area.playerText`; keep `area.dmText`
  internal (used to seed the DM's decisions and the optional LLM, never shown to the player).
  Format rule outcomes into plain sentences ("The orc's blade bites for 4 damage.").
- **Generative layer (optional — see LLM section):** if enabled, rewrite/extend the authored text
  and voice NPCs. With it **off**, the Narrator falls back to authored text verbatim — the game is
  fully playable offline.

### 4. Actor — `EncounterDirector`
On area entry (and on wandering checks), rolls the area's `monsterChancePct`; on success resolves
`monsterType` (a `gns.db` monster name, or free text) into a stat block via `Repository::monster`,
rolls group size and individual HP, rolls a **reaction** (2d6 reaction table already in `gns.db`),
and hands the encounter to combat. NPC personality/voice is where the optional LLM adds the most;
without it, the Actor still produces correct, table-driven monster behavior and reactions.

### 5. Pacing / Coordinator — `Session` + `MapVisibility`
- **`Session`** is the top-level runtime object and a small state machine:
  `Exploration ↔ Encounter/Combat ↔ Dialogue → (Victory|Defeat|Save/Quit)`. It owns the `Party`,
  the loaded `Module`, the `PlotTracker`, current map/area, the seeded `Dice`, and a turn counter.
- **`MapVisibility`** implements fog of war ("portions not visited are not revealed", per the
  design doc): a per-map visited-cells bitset the renderer consults; updated as the party moves.
- **Combat** is the turn-based loop: initiative, per-combatant HP tracking, round/turn order,
  applying `RulesAdjudicator` results, morale/retreat (future), XP award on victory.

---

## Runtime model (new types in `gns_core`, mirrors the existing `Module.h` style)

```
Session            // top-level play-state; serializes to .gnssav
  Party            // roster of Character (Character already exists in gns_core)
    Character[]    // live hp, equipped weapon/armor, inventory, xp  (extend Character)
  PlayState        // current mapId/areaId, turn count, Dice seed+state, mode enum
  PlotTracker      // completed controlPointIds (set<int>)
  MapVisibility    // per-map visited/seen cell bitsets
  EncounterState?  // present only during Encounter/Combat: combatants, initiative, round
```

`.gnssav` is a **separate SQLite file** (same pattern as `.gnsmod`, its own `PRAGMA user_version`),
written via a new `SaveIO.cpp` modeled on `ModuleIO.cpp`. It stores game progress only and
references the module by name/id — it never duplicates `gns.db` reference data. Follow the existing
**append-only, column-set-fallback** versioning discipline from `ModuleIO.cpp` so saves stay
forward/backward tolerant.

---

## The optional LLM narration/dialogue layer

The creative DM roles (vivid narration, improvised NPC dialogue) are where a table-driven engine is
weakest. Per your decision, add a **pluggable** layer behind a clean interface so the procedural
core is the source of truth and the LLM only embellishes:

```cpp
// gns_core, no SDL/ImGui dependency
struct DmContext {                 // assembled from authored + runtime state
  std::string areaDmText, areaPlayerText;
  std::string situation;           // e.g. "party enters; 3 orcs, reaction: hostile"
  std::vector<std::string> facts;  // rule outcomes already decided by RulesAdjudicator
};
class INarrationProvider {
public:
  virtual ~INarrationProvider() = default;
  virtual std::string narrate(const DmContext&) = 0;      // area/event prose
  virtual std::string speakNpc(const DmContext&, const std::string& npc) = 0;
};
```

- **`TemplateNarrationProvider` (default, offline):** returns authored text + formatted rule
  outcomes. Zero network, deterministic. **The game ships and is fully playable with only this.**
- **`ClaudeNarrationProvider` (opt-in):** calls the Claude API to rewrite/extend the authored text
  and voice NPCs. The LLM **never decides outcomes** — `RulesAdjudicator` has already rolled the
  dice and the results are passed in as immutable `facts`; the model only renders them as prose.
  This keeps the game fair, deterministic where it matters, and authentic to the module author's
  intent.

### LLM integration specifics (when enabled)
- **No official Anthropic C++ SDK exists**, so call the REST API directly over HTTPS with
  **libcurl** (add as a vendored dep under `game/third_party/`, consistent with the no-package-manager
  convention) plus a small JSON writer/reader. Endpoint `POST /v1/messages`.
- **Model:** `claude-opus-4-8` (current most capable; the latest model is the right default for an
  AI feature). Use **adaptive thinking** (`"thinking": {"type": "adaptive"}`) and **stream** the
  response so narration appears progressively in the ImGui text panel instead of blocking the turn.
- **Prompt caching:** the system prompt (DM persona + rules-of-narration) and the module's static
  context are a stable prefix — mark them with `cache_control` so repeated turns are cheap/fast.
- **Config & safety:** disabled by default; enabled only when the player supplies their own API key
  (settings file / `ANTHROPIC_API_KEY`). Surface clearly in-app that enabling it **sends module and
  play-state text to an external service**. The engine degrades to `TemplateNarrationProvider` on any
  network/timeout/refusal so a session never stalls. Keep the call off the render thread.

---

## Module-format gaps a live DM needs (future `.gnsmod` additions)

The current format covers spatial + narrative + percent-chance simulation well. A richer DM-driven
session wants the following — add them to `Module.h`/`ModuleIO.cpp` **append-only** (the format is at
`user_version` 5; never reorder existing `Terrain`/`ObjectType` enums or columns):

- **Named NPCs** (distinct from rolled monsters): name, optional `gns.db` stat reference,
  disposition, and authored dialogue/voice notes — feeds the Actor and the LLM `speakNpc`.
- **Scripted/triggered events:** "on entering area X with item Y, do Z" — even a small condition→effect
  list closes most of the gap between static chances and authored set-pieces.
- **Quest/objective text** tied to control points so the engine can show the "key plot information
  summaries" the design doc calls for (a journal/recap screen).
- **Trap/lock mechanics:** today these are free-text + a percent. Optionally add a save category / DC /
  damage expression so `RulesAdjudicator` can resolve them instead of only narrating.

None of these block M4 — the engine should ship reading today's format, treating missing fields as
"none," and gain the above incrementally (each as a new save/module schema version).

---

## Recommended first slice (concrete M4 starting point)

Build the smallest end-to-end DM loop, then layer combat and saves:

1. **`Session` + `Party` + `PlayState`** in `gns_core`; load a `.gnsmod` and seat the start
   map/area (`Module::startMapId`/`startAreaId`).
2. **Engine screen swap:** replace the M0 data-list in `game/engine/src/main.cpp` with a play view —
   map panel (with `MapVisibility` fog), narration/text panel, party status panel (avatars, HP,
   abilities), per the design doc's screen list.
3. **Exploration turn:** move party → reveal cells → on area entry, `Narrator` emits `playerText`;
   `PlotTracker` checks prerequisites; `EncounterDirector` rolls `monsterChancePct`.
4. **`RulesAdjudicator` + combat loop:** initiative, HP tracking, attack/damage/saves via existing
   `Rules`, XP on victory.
5. **`.gnssav` via `SaveIO.cpp`** (save/restore `Session`, including `Dice` state and plot/visibility).
6. **`INarrationProvider` seam** with `TemplateNarrationProvider` only; add `ClaudeNarrationProvider`
   last, behind config.

Keep every new piece in `gns_core` (UI-free) so it's covered by the `gns_tests` harness.

---

## Verification

- **Unit (gns_tests, `game/tests/`):** add `check(...)` cases against the real `gns.db` for each new
  subsystem with a **seeded `Dice`** so outcomes are deterministic — encounter rolls, prerequisite
  gating (enterable/blocked), a scripted combat round reaching a known HP/XP result, and a
  `.gnssav` round-trip (save → load → state identical, like the existing `.gnsmod` round-trip test).
  `ctest --test-dir game/build -C Debug` green is the bar before advancing.
- **Manual end-to-end:** build (`cmake --build game/build --config Debug`), author a tiny 2-area test
  module in the Module Creator (one start area with a control point, one gated end area with a
  monster chance), then run `GameEngine.exe` and walk the loop: enter start → fog reveals →
  read narration → trigger/resolve an encounter → complete the control point → confirm the gated area
  unlocks → reach `endAreaId` (victory) → save, quit, reload, verify state.
- **LLM layer (only if/when built):** verify the game is fully playable with the provider **off**
  (offline, deterministic), then enable `ClaudeNarrationProvider` with a key and confirm narration
  streams in and that a forced network failure cleanly falls back to authored text without stalling.
