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

A DM is Storyteller, Narrator, Actor, Arbiter/Referee, and Pacing/Coordinator. The engine reproduces each as a subsystem in `gns_core` (UI-free, testable)
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
- **Generative layer (optional — local model, see LLM section):** if enabled, an embedded llama.cpp
  model rewrites/extends the authored text and voices NPCs. With it **off** (or absent), the Narrator
  falls back to authored text verbatim — the game is fully playable offline.

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

## The optional LLM narration/dialogue layer (local-only)

The creative DM roles (vivid narration, NPC dialogue) are where a table-driven engine is weakest.
Add a **pluggable** layer behind a clean interface so the procedural core stays the source of truth
and the model only embellishes. **Decision: the model runs fully local and offline — the game must
play with no internet, no API key, and nothing ever leaving the machine. There is no cloud/Claude
provider.**

```cpp
// gns_core, no SDL/ImGui dependency
struct DmContext {                 // assembled from authored + runtime state
  std::string areaName, areaPlayerText, areaDmText;
  std::string situation;           // e.g. "party enters; 3 orcs, reaction: hostile"
  std::vector<std::string> facts;  // rule outcomes already decided by the rules engine
};
class INarrationProvider {
public:
  virtual ~INarrationProvider() = default;
  virtual std::string narrate(const DmContext&) = 0;      // area/event prose
  virtual std::string speakNpc(const DmContext&, const std::string& npc) = 0;
};
```

- **`TemplateNarrationProvider` (default, always on, offline):** returns authored `playerText` +
  rule outcomes formatted as plain sentences. Zero LLM, deterministic, no dependencies. **The game
  ships and is fully playable with only this** — it is the permanent offline floor.
- **`LlamaNarrationProvider` (optional, local):** an embedded **llama.cpp** model loaded from a local
  GGUF file that rewrites/extends the authored text and voices NPCs. The model **never decides
  outcomes** — the rules engine has already rolled the dice and the results arrive as immutable
  `facts`; the model only renders them as prose. That keeps play fair and deterministic where it
  matters, and authentic to the author's intent.

### Why a small local model is enough (the key design point)
Quantized 7–8B models are weak at reasoning/math/long-horizon coherence — **none of which the
Narrator needs**, because `RulesAdjudicator` / `PlotTracker` / `EncounterDirector` own every decision
and roll. The model's only job is the narrow task of *rendering already-decided facts as prose and
speaking one NPC line* — constrained rewriting, which small instruct models do well. We do **not**
train or fine-tune a model (DM behavior comes from the system prompt + structured context, not custom
weights), and this is **not** a RAG/embeddings system (the relevant context is the current game
state, assembled deterministically — not retrieved from a corpus). A future embeddings index over
module lore for long-campaign memory is an optional nicety, not a requirement.

### Embedded-llama.cpp integration specifics (later slice, not step 3)
- **Vendor `llama.cpp`** under `game/third_party/` (matches the no-package-manager convention) and
  link it into a `LlamaNarrationProvider` in `gns_core` (kept UI-free; the engine owns the thread).
- The player drops a **GGUF model file** next to the executable (suggested default: a 7–8B instruct
  model at Q4_K_M); the provider memory-maps it once at session start. No download at play time.
- Run generation on a **worker thread** and stream tokens into the ImGui narration panel so the turn
  never blocks. A tight **system prompt** (DM persona + "render only the given facts, invent no
  outcomes") plus a one-shot voice example is what gets small-model quality where it needs to be.
- **Graceful degradation:** if no GGUF is present or the model fails to load, the engine silently
  uses `TemplateNarrationProvider` — a session never stalls and the game is always playable.

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
6. **`INarrationProvider` seam** with `TemplateNarrationProvider` only; add the local
   `LlamaNarrationProvider` (embedded llama.cpp + GGUF) last, as an optional drop-in.

Keep every new piece in `gns_core` (UI-free) so it's covered by the `gns_tests` harness.

> **Status:** the DM core is being built UI-free and testable first, ahead of the engine-UI screen
> swap (step 2). Done and green under `gns_tests`: step 1 (`Session`/`Party`/`PlayState`), the
> Storyteller `PlotTracker`, the Narrator (`Narrator` + `TemplateNarrationProvider` + the
> `INarrationProvider` seam), the Referee (`RulesAdjudicator`), and the Actor (`EncounterDirector`).
> All five DM-role subsystems now exist; remaining M4 work is the combat loop, `MapVisibility` fog of
> war, the engine play-view UI (step 2), and `.gnssav`.

---

## Step 3 (done) — Narrator + offline provider seam

The always-on authored narration layer **plus** the provider seam the local model later plugs into.
UI-free, deterministic, **no new dependencies** (llama.cpp is a later slice). All in `gns_core`,
covered by `gns_tests`.

**Files:** `game/core/include/gns/Narrator.h`, `game/core/src/Narrator.cpp` (added to
`game/core/CMakeLists.txt`).

**Contents:**
- `DmContext` + `INarrationProvider` (shapes in the LLM section) — the seam every provider implements.
- `TemplateNarrationProvider : INarrationProvider` — `narrate()` returns the area's `playerText`
  followed by the formatted `facts`; it **never emits `dmText`** (DM-only — carried in the context
  solely to seed a future model). `speakNpc()` returns a simple templated line.
- `Narrator` — a thin service the engine constructs with a provider (defaults to the template
  provider; a `LlamaNarrationProvider` drops in later). `describeAreaEntry(const Area&)` pairs with
  `Session::currentArea()`; `describe(facts)` renders a bare outcome list; `speak(npc, area)` voices a
  line. `factFor(const AttackResult&, attacker, target)` formats an existing `Rules.h` result into a
  fact sentence (more formatters land with their producing slices).

**Decoupling:** `Narrator` does **not** live inside `Session` (Session owns play-*state*; narration is
a presentation service). The engine calls `narrator.describeAreaEntry(*session.currentArea())`.

**Tests (`== narrator ==`):** template `narrate()` emits `playerText` and never leaks `dmText`; facts
fold in order; `factFor(AttackResult)` renders hit/miss; `speakNpc()` is non-empty; and an injected
test-double provider proves `Narrator` calls through the seam (so the local model drops in cleanly).

---

## Step 4 (done) — Referee / `RulesAdjudicator`

A thin, session-facing façade over the existing M1 rules (`Rules.h`) — **no new rules**, pure
routing plus two small result structs. UI-free, deterministic, in `gns_core`, covered by `gns_tests`.

**Files:** `game/core/include/gns/RulesAdjudicator.h`, `game/core/src/RulesAdjudicator.cpp` (added to
`game/core/CMakeLists.txt`).

**Shape.** Binds a `const Repository&` (engine-owned, from `gns.db`) + the session's seeded `Dice&`
(`Session::dice()`), then exposes methods that drop those args: `characterAttack` / `monsterAttack`
(forward to the rich-result functions), `savingThrow` → `{needed, roll, success}`, a generic
`check(pct)` over `Dice::percent`, `trapCheck` / `lockCheck` / `hiddenCheck` → `{occurred,
description}` from the area's authored chance + text, and forwards for `wandering` / `treasure` /
`turnUndead` / `monsterXp`. Members call the `gns::`-qualified free functions to avoid recursion.

**Decoupling.** Not owned by `Session` — Session stays clean play-state with no `Repository`/
`Database` dependency. The engine constructs `RulesAdjudicator adj(repo, session.dice());` so every
roll runs on the one seeded stream (deterministic `.gnssav` replay).

**Scope.** Monster *encounter assembly* stays in the future `EncounterDirector` (the adjudicator only
gives it `check()`); `computeLoad`/movement is deferred until inventory carries weight.

**Tests (`== adjudicator ==`):** seed-equivalence proves pure forwarding (same seed through the façade
vs the free function → identical results, for attack/monsterAttack/wandering/treasure/turnUndead);
value checks anchored to existing constants (`savingThrow` needed 12; `characterAttack` needed 10 vs
AC9 with damage 1..8 on hit; `monsterXp(Orc)` 10); and the `100%`/`0%` authored-chance extremes.

---

## Step 5 (done) — Actor / `EncounterDirector`

Turns an area's authored monster chance (or a wandering check) into a concrete, ready-to-fight
encounter. UI-free, deterministic, in `gns_core`, covered by `gns_tests`.

**Files:** `game/core/include/gns/EncounterDirector.h`, `game/core/src/EncounterDirector.cpp` (added to
`game/core/CMakeLists.txt`).

**Shape.** Binds a `const Repository&` + the session's seeded `Dice&` (same shape as the adjudicator;
the presence roll is the same `Dice::percent` primitive). `checkArea(area)` rolls
`monsterChancePct` then builds from `area.monsterType` (group size 1); `checkWandering(env, level)`
forwards to `rollWandering` and uses its number-appearing as the count; `makeEncounter(type, count,
fromWandering)` resolves the type to a `MonsterDef` (`Repository::monster`), rolls per-monster HP
(`N d8 + mod` parsed from the hit-dice line, `1d4` for sub-1 HD), and a 2d6 reaction. Produces an
`Encounter { occurred, monsterType, known, fromWandering, vector<Combatant>, reaction }` where each
`Combatant` carries rolled `hp`/`maxHp`, AC, hit dice, and damage ready for the combat loop.

**Notes / deviations.** The **reaction is computed in code** from the canonical Basic D&D 2d6 table
(`reactionFor2d6`) — gns.db has no reaction accessor wired in; data-driving it later is an easy swap.
Unknown / free-text monster types still yield usable combatants (`known = false` flags them) so play
never dead-ends. Group size for an area encounter defaults to 1 (the module names a type, not a count).

**Tests (`== encounter ==`):** the 2d6 reaction mapping; an area encounter at 100% resolving a known
`Orc` with HP in a valid HD range and `hp == maxHp`; no encounter at 0%; a group of 3 with AC pulled
from the stat block; an unknown type still built and flagged; and a wandering check yielding a valid
encounter for a populated environment.

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
- **Narration layer:** verify the game is fully playable with `TemplateNarrationProvider` only
  (offline, deterministic). When the local `LlamaNarrationProvider` is added, confirm a missing/bad
  GGUF cleanly falls back to the template provider without stalling, and that generation runs off the
  render thread.
