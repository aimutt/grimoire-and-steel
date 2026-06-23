// gns_core unit tests (M0 smoke + M1 rules). Opens the real gns.db (path from CMake).
#include "gns/Database.h"
#include "gns/Content.h"
#include "gns/Dice.h"
#include "gns/Repository.h"
#include "gns/Character.h"
#include "gns/Rules.h"
#include "gns/Module.h"
#include "gns/Session.h"
#include "gns/PlotTracker.h"
#include "gns/Narrator.h"
#include "gns/RulesAdjudicator.h"
#include "gns/EncounterDirector.h"
#include "gns/CombatEngine.h"

#include <cstdio>
#include <exception>
#include <string>

static int failures = 0;
static void check(const std::string& label, bool cond) {
    std::printf("  [%s] %s\n", cond ? "OK " : "XX ", label.c_str());
    if (!cond) ++failures;
}

int main() {
    using namespace gns;
    try {
        Database db(GNS_DB_PATH);

        // ---- M0 smoke ----
        std::printf("== data ==\n");
        check("monsters == 97", countRows(db, "monsters") == 97);
        check("all_spells == 66", countRows(db, "all_spells") == 66);

        // ---- Dice ----
        std::printf("== dice ==\n");
        Dice dice(12345);
        DiceExpr e;
        check("parse '1d8'", parseDice("1d8", e) && !e.isRange && e.count == 1 && e.sides == 8);
        check("parse '2d6+1'", parseDice("2d6+1", e) && e.count == 2 && e.sides == 6 && e.plus == 1);
        check("parse '2-13'", parseDice("2-13", e) && e.isRange && e.min == 2 && e.max == 13);
        {
            bool inRange = true, sawLow = false, sawHigh = false;
            for (int i = 0; i < 2000; ++i) {
                int v = dice.roll(1, 8);
                if (v < 1 || v > 8) inRange = false;
                if (v == 1) sawLow = true;
                if (v == 8) sawHigh = true;
            }
            check("1d8 stays in [1,8] and hits both ends", inRange && sawLow && sawHigh);
        }
        {
            Dice a(7), b(7);
            check("same seed -> same sequence", a.roll(3, 6) == b.roll(3, 6));
        }

        // ---- Repository lookups ----
        std::printf("== repository ==\n");
        Repository repo(db);
        check("Fighter prime-req XP @ STR16 = +10%", repo.abilityAdjustment("Prime Requisite", 16) == 10);
        check("CON 14 hit-die mod = 0", repo.abilityAdjustment("Constitution", 14) == 0);
        check("CON 18 hit-die mod = +3", repo.abilityAdjustment("Constitution", 18) == 3);
        check("char L1-3 to-hit vs AC2 = 17", repo.attackRollNeeded("character", "1st-3rd Level Character", 2) == 17);
        check("monster '11 up' vs AC9 = 0", repo.attackRollNeeded("monster", "11 up", 9) == 0);
        check("STR 16 base capacity = 100", repo.strengthCapacity(16)->baseLbs == 100);
        check("Two-handed Sword dmg 1..10", repo.weapon("Two-handed Sword")->damageMax == 10);
        check("Orc XP = 10", [&] {
            const MonsterDef* o = repo.monster("Orc"); return o && monsterXp(repo, *o) == 10;
        }());

        // ---- Character creation (Morgan-like: Fighter/Human, STR16 CON14) ----
        std::printf("== character ==\n");
        AbilityScores sc;
        sc.str = 16; sc.intel = 7; sc.wis = 9; sc.dex = 13; sc.con = 14; sc.cha = 8;
        Character morgan = makeCharacter(repo, dice, "Morgan", "Human", "Fighter", sc, 1);
        check("xp bonus 10%", morgan.xpBonusPct == 10);
        check("class group = Fighting Man", morgan.classGroup == "Fighting Man, Thief, Hobgoblin");
        check("save vs death/poison = 12", morgan.saveDeathPoison == 12);
        check("save vs dragon breath = 15", morgan.saveDragonBreath == 15);
        check("HP between 1 and 8", morgan.maxHp >= 1 && morgan.maxHp <= 8);
        check("base movement 120 (Human)", morgan.baseMovementFt == 120);
        // Dwarf uses the Dwarves & Halflings save group + 90' movement.
        Character dorin = makeCharacter(repo, dice, "Dorin", "Dwarf", "Fighter", sc, 1);
        check("Dwarf save group", dorin.classGroup == "Dwarves & Halflings");
        check("Dwarf movement 90", dorin.baseMovementFt == 90);

        // ---- Encumbrance / movement ----
        std::printf("== encumbrance ==\n");
        LoadInfo light = computeLoad(repo, morgan, 80);   // <= 100 capacity
        check("light load: full movement", !light.encumbered && light.effectiveMovementFt == 120);
        LoadInfo heavy = computeLoad(repo, morgan, 120);   // > 100, <= 200
        check("encumbered: half movement", heavy.encumbered && heavy.effectiveMovementFt == 60);
        LoadInfo over = computeLoad(repo, morgan, 250);    // > max
        check("overloaded: no movement", over.effectiveMovementFt == 0);

        // ---- Combat ----
        std::printf("== combat ==\n");
        check("characterToHit AC9 = 10", characterToHit(repo, 1, 9) == 10);
        check("monster 1 HD vs AC9 needs 10", monsterToHit(repo, 1, 9) == 10);
        {
            bool ok = true;
            for (int i = 0; i < 500; ++i) {
                AttackResult ar = characterAttack(repo, dice, 1, 7, "Sword");
                if (ar.hit && (ar.damage < 1 || ar.damage > 8)) ok = false;  // Sword = 1d8
            }
            check("Sword hit damage in 1..8", ok);
        }

        // ---- Saving throw ----
        check("save needed (poison) = 12", saveNeeded(morgan, SaveCategory::DeathPoison) == 12);

        // ---- Turn undead ----
        std::printf("== turn undead ==\n");
        Character cleric = makeCharacter(repo, dice, "Father", "Human", "Cleric", sc, 1);
        (void)cleric;
        TurnResult t1 = turnUndead(repo, dice, 1, "Skeleton");
        check("L1 cleric can attempt to turn Skeleton (needs 7)", t1.possible && t1.needed == 7);
        TurnResult t3 = turnUndead(repo, dice, 3, "Zombie");
        check("L3 cleric auto-turns Zombie", t3.autoTurned && t3.turned);
        TurnResult tv = turnUndead(repo, dice, 1, "Vampire");
        check("L1 cleric cannot turn Vampire", !tv.possible);

        // ---- Wandering ----
        std::printf("== wandering ==\n");
        {
            bool sawSkeleton = false, sawElephant = false, allValid = true;
            for (int i = 0; i < 800; ++i) {
                WanderingResult w = rollWandering(repo, dice, "Dungeon", 1);
                if (!w.any || w.count < 1) allValid = false;
                if (w.monster == "Skeleton") sawSkeleton = true;
                if (w.monster == "Elephant") sawElephant = true;
            }
            check("Dungeon L1 yields valid encounters", allValid);
            check("Dungeon L1 can yield Skeleton", sawSkeleton);
            check("Dungeon L1 never yields Elephant", !sawElephant);
        }

        // ---- Treasure ----
        std::printf("== treasure ==\n");
        {
            // Type Q is gems only; should never produce coins.
            bool coinsSeen = false, gemsSeen = false;
            for (int i = 0; i < 200; ++i) {
                TreasureResult tr = rollTreasure(repo, dice, "Q");
                if (tr.copper || tr.silver || tr.electrum || tr.gold || tr.platinum) coinsSeen = true;
                if (!tr.gems.empty()) gemsSeen = true;
            }
            check("Type Q yields no coins", !coinsSeen);
            check("Type Q can yield gems", gemsSeen);
            // Type A should occasionally produce a non-zero hoard.
            bool nonZero = false;
            for (int i = 0; i < 200 && !nonZero; ++i)
                if (rollTreasure(repo, dice, "A").totalGpValue() > 0) nonZero = true;
            check("Type A can yield treasure", nonZero);
        }

        // ---- Module .gnsmod round-trip (M2 I/O) ----
        std::printf("== module io ==\n");
        {
            Module m;
            m.name = "Tomb of Tests";
            m.author = "QA";
            m.summary = "Round-trip fixture.";

            Map map;
            map.id = 1; map.name = "Level 1";
            map.gridW = 4; map.gridH = 3;
            map.overlayW = 2; map.overlayH = 1;
            map.cells.assign(4 * 3, static_cast<int>(Terrain::Empty));
            map.cellArea.assign(4 * 3, 0);
            map.cells[0] = static_cast<int>(Terrain::Floor);
            map.cells[5] = static_cast<int>(Terrain::Wall);
            map.cellArea[0] = 10;
            map.cellArea[1] = 10;

            Area a1; a1.id = 10; a1.label = "A1"; a1.name = "Entry";
            a1.color = 0x4F8FE0FF;
            a1.dmText = "A trap lurks."; a1.playerText = "A dusty hall.";
            a1.monsterChancePct = 25; a1.monsterType = "Skeleton";
            a1.treasureChancePct = 50; a1.treasureType = "C";
            a1.trapChancePct = 30; a1.trapDescription = "Pit";
            a1.lockChancePct = 15; a1.lockDescription = "Iron door";
            a1.hiddenChancePct = 40; a1.hiddenDescription = "Loose brick";
            a1.artworkPath = "art/entry.png";
            a1.prerequisiteControlPointIds = {1};
            Area a2; a2.id = 11; a2.label = "B1"; a2.name = "Crypt";
            map.areas.push_back(a1);
            map.areas.push_back(a2);

            MapObject ob1; ob1.id = 1; ob1.type = static_cast<int>(ObjectType::Table);
            ob1.x = 1.5f; ob1.y = 0.5f; ob1.rotationDeg = 90.0f;
            MapObject ob2; ob2.id = 2; ob2.type = static_cast<int>(ObjectType::Chest);
            ob2.x = 2.25f; ob2.y = 1.75f; ob2.rotationDeg = 45.0f;
            map.objects.push_back(ob1);
            map.objects.push_back(ob2);

            MapText tx; tx.id = 1; tx.x = 0.5f; tx.y = 2.5f; tx.text = "Hall of Echoes";
            tx.color = 0x33CCFFFFu; tx.sizePx = 24.0f;
            map.texts.push_back(tx);

            m.maps.push_back(map);

            ControlPoint cp; cp.id = 1; cp.name = "Sealed gate";
            cp.description = "Opens the crypt."; cp.mapId = 1; cp.areaId = 11;
            cp.kind = 1;   // Control Item
            cp.x = 2.5f; cp.y = 1.25f;
            m.controlPoints.push_back(cp);
            m.startMapId = 1; m.startAreaId = 10; m.endAreaId = 11;

            const std::string path = "gns_roundtrip_test.gnsmod";
            std::remove(path.c_str());
            saveModule(m, path);
            Module r = loadModule(path);
            std::remove(path.c_str());

            check("module name preserved", r.name == "Tomb of Tests");
            check("module author/summary preserved", r.author == "QA" && r.summary == "Round-trip fixture.");
            check("start/end markers preserved", r.startMapId == 1 && r.startAreaId == 10 && r.endAreaId == 11);
            check("one map preserved", r.maps.size() == 1);
            check("grid dims preserved", r.maps[0].gridW == 4 && r.maps[0].gridH == 3);
            check("overlay dims preserved", r.maps[0].overlayW == 2 && r.maps[0].overlayH == 1);
            check("cell arrays preserved", r.maps[0].cells == m.maps[0].cells &&
                                           r.maps[0].cellArea == m.maps[0].cellArea);
            check("two areas preserved", r.maps[0].areas.size() == 2);
            const Area* ra = r.areaById(10);
            check("area stats preserved", ra && ra->label == "A1" && ra->name == "Entry" &&
                  ra->color == 0x4F8FE0FF && ra->dmText == "A trap lurks." &&
                  ra->playerText == "A dusty hall." && ra->monsterChancePct == 25 &&
                  ra->monsterType == "Skeleton" && ra->treasureChancePct == 50 &&
                  ra->treasureType == "C" && ra->trapChancePct == 30 &&
                  ra->lockChancePct == 15 && ra->hiddenChancePct == 40 &&
                  ra->artworkPath == "art/entry.png");
            check("area prerequisites preserved", ra && ra->prerequisiteControlPointIds.size() == 1 &&
                  ra->prerequisiteControlPointIds[0] == 1);
            check("control point preserved", r.controlPoints.size() == 1 &&
                  r.controlPoints[0].id == 1 && r.controlPoints[0].name == "Sealed gate" &&
                  r.controlPoints[0].areaId == 11);
            check("control point position preserved", r.controlPoints.size() == 1 &&
                  r.controlPoints[0].x == 2.5f && r.controlPoints[0].y == 1.25f);
            check("control point kind preserved", r.controlPoints.size() == 1 &&
                  r.controlPoints[0].kind == 1);
            check("map text preserved", r.maps[0].texts.size() == 1 &&
                  r.maps[0].texts[0].id == 1 && r.maps[0].texts[0].x == 0.5f &&
                  r.maps[0].texts[0].y == 2.5f && r.maps[0].texts[0].text == "Hall of Echoes" &&
                  r.maps[0].texts[0].color == 0x33CCFFFFu && r.maps[0].texts[0].sizePx == 24.0f);
            check("two objects preserved", r.maps[0].objects.size() == 2);
            check("object fields preserved", r.maps[0].objects.size() == 2 &&
                  r.maps[0].objects[0].id == 1 &&
                  r.maps[0].objects[0].type == static_cast<int>(ObjectType::Table) &&
                  r.maps[0].objects[0].x == 1.5f && r.maps[0].objects[0].y == 0.5f &&
                  r.maps[0].objects[1].type == static_cast<int>(ObjectType::Chest) &&
                  r.maps[0].objects[1].x == 2.25f && r.maps[0].objects[1].y == 1.75f);
            check("object rotation preserved", r.maps[0].objects.size() == 2 &&
                  r.maps[0].objects[0].rotationDeg == 90.0f &&
                  r.maps[0].objects[1].rotationDeg == 45.0f);
        }

        // ---- Session / Party / PlayState seating (M4 slice 1) ----
        std::printf("== session ==\n");
        {
            // Minimal module: one map, two areas, declared start = area 10.
            Module m;
            m.name = "Seating Test";
            Map map; map.id = 1; map.name = "Level 1";
            map.gridW = 2; map.gridH = 2;
            map.cells.assign(4, static_cast<int>(Terrain::Floor));
            map.cellArea.assign(4, 0);
            Area entry; entry.id = 10; entry.label = "A1"; entry.name = "Entry";
            entry.playerText = "A dusty hall.";
            Area crypt; crypt.id = 11; crypt.label = "B1"; crypt.name = "Crypt";
            map.areas.push_back(entry);
            map.areas.push_back(crypt);
            m.maps.push_back(map);
            m.startMapId = 1; m.startAreaId = 10; m.endAreaId = 11;

            Party party;
            party.members.push_back(morgan);   // level-1 Fighter built above

            Session s(m, party, 4242);
            check("session seats at declared start map", s.state().mapId == 1);
            check("session seats at declared start area", s.state().areaId == 10);
            check("session reports declared-start seating", s.seatedAtDeclaredStart());
            check("session current area resolves to Entry",
                  s.currentArea() && s.currentArea()->name == "Entry");
            check("session current map resolves", s.currentMap() && s.currentMap()->id == 1);
            check("session begins in exploration at turn 0",
                  s.state().mode == PlayMode::Exploration && s.state().turnCount == 0);
            check("party not wiped, average level 1",
                  !s.party().isWiped() && s.party().averageLevel() == 1);

            // Fallback: no declared start -> seat on the first area of the first map.
            Module m2 = m; m2.startMapId = 0; m2.startAreaId = 0;
            Session s2(m2, party, 1);
            check("session falls back to first map/area",
                  s2.state().mapId == 1 && s2.state().areaId == 10);
            check("fallback is not flagged as declared start", !s2.seatedAtDeclaredStart());

            // Disk path: save then start a session straight from the .gnsmod file.
            const std::string path = "gns_session_roundtrip_test.gnsmod";
            std::remove(path.c_str());
            saveModule(m, path);
            Session s3 = startSessionFromFile(path, party, 7);
            std::remove(path.c_str());
            check("startSessionFromFile seats at the declared start area",
                  s3.state().areaId == 10 && s3.seatedAtDeclaredStart());
        }

        // ---- PlotTracker: prerequisite gating + completion (M4 Storyteller) ----
        std::printf("== plot ==\n");
        {
            // Module with a gated area: area 11 requires control point 1.
            Module m;
            m.name = "Gated Test";
            Map map; map.id = 1; map.name = "Level 1";
            map.gridW = 2; map.gridH = 2;
            map.cells.assign(4, static_cast<int>(Terrain::Floor));
            map.cellArea.assign(4, 0);
            Area entry; entry.id = 10; entry.name = "Entry";   // no prerequisites
            Area crypt; crypt.id = 11; crypt.name = "Crypt";
            crypt.prerequisiteControlPointIds = {1};           // gated behind cp 1
            map.areas.push_back(entry);
            map.areas.push_back(crypt);
            m.maps.push_back(map);
            ControlPoint cp; cp.id = 1; cp.name = "Sealed gate";
            cp.mapId = 1; cp.areaId = 11; cp.kind = 0;
            m.controlPoints.push_back(cp);
            m.startMapId = 1; m.startAreaId = 10; m.endAreaId = 11;

            Party party; party.members.push_back(morgan);
            Session s(m, party, 1);

            check("ungated area enterable from the start", s.isAreaEnterable(10));
            check("gated area blocked before prerequisite", !s.isAreaEnterable(11));
            check("unknown area id is not enterable", !s.isAreaEnterable(999));

            check("completing a control point reports newly-done", s.completeControlPoint(1));
            check("plot records the completed id",
                  s.plot().isComplete(1) && s.plot().completedIds().count(1) == 1);
            check("gated area unlocks after prerequisite", s.isAreaEnterable(11));
            check("re-completing reports already-done", !s.completeControlPoint(1));
            check("unknown control point id is rejected and not recorded",
                  !s.completeControlPoint(999) && s.plot().completedIds().count(999) == 0);

            check("not at end while seated at entry", !s.isAtEnd());
            s.state().areaId = m.endAreaId;
            check("at end once seated on the end area", s.isAtEnd());

            // Restore-from-save path + inert-by-default behavior (standalone tracker).
            PlotTracker pt;
            check("fresh tracker gates the crypt", !pt.isAreaEnterable(*m.areaById(11)));
            check("ungated area enterable on a bare tracker", pt.isAreaEnterable(*m.areaById(10)));
            pt.setCompletedIds({1});
            check("restored tracker opens the crypt", pt.isAreaEnterable(*m.areaById(11)));
        }

        // ---- Narrator: authored text + provider seam (M4 slice 3) ----
        std::printf("== narrator ==\n");
        {
            Area area;
            area.id = 10; area.name = "Entry";
            area.playerText = "A dusty hall stretches north.";
            area.dmText = "A pit trap hides under the third flagstone.";  // must never leak

            Narrator narrator;  // built-in TemplateNarrationProvider

            const std::string entry = narrator.describeAreaEntry(area);
            check("narrate emits the player text",
                  entry == "A dusty hall stretches north.");
            check("narrate never leaks DM-only text",
                  entry.find("pit trap") == std::string::npos);

            std::vector<std::string> facts = {"The door creaks open.", "A cold draft escapes."};
            const std::string withFacts = narrator.describeAreaEntry(area, facts);
            check("narrate folds facts in order after the player text",
                  withFacts ==
                  "A dusty hall stretches north.\nThe door creaks open.\nA cold draft escapes.");

            const std::string only = narrator.describe(facts);
            check("describe renders a bare fact list",
                  only == "The door creaks open.\nA cold draft escapes.");

            const std::string line = narrator.speak("Old Hermit", area);
            check("speakNpc returns a non-empty line naming the NPC",
                  !line.empty() && line.find("Old Hermit") != std::string::npos);

            // Fact formatter over an existing Rules result (deterministic, no RNG).
            AttackResult hit; hit.hit = true; hit.damage = 6;
            AttackResult miss; miss.hit = false;
            check("factFor(AttackResult) renders a hit",
                  factFor(hit, "Morgan", "Goblin") == "Morgan hits Goblin for 6 damage.");
            check("factFor(AttackResult) renders a miss",
                  factFor(miss, "Morgan", "Goblin") == "Morgan misses Goblin.");

            // Seam proof: an injected provider is what Narrator calls through to,
            // guaranteeing the local model drops in cleanly later.
            struct StubProvider : INarrationProvider {
                std::string narrate(const DmContext& ctx) override {
                    return "STUB-NARRATE:" + ctx.areaName;
                }
                std::string speakNpc(const DmContext&, const std::string& npc) override {
                    return "STUB-SPEAK:" + npc;
                }
            } stub;
            Narrator custom(stub);
            check("injected provider drives narrate",
                  custom.describeAreaEntry(area) == "STUB-NARRATE:Entry");
            check("injected provider drives speakNpc",
                  custom.speak("Old Hermit", area) == "STUB-SPEAK:Old Hermit");
        }

        // ---- RulesAdjudicator: thin façade over the rules engine (M4 Referee) ----
        std::printf("== adjudicator ==\n");
        {
            // Seed-equivalence: the same seed through the façade vs the free
            // function must produce identical results -- proves pure forwarding.
            {
                Dice da(2024), db(2024);
                RulesAdjudicator adj(repo, da);
                AttackResult fa = adj.characterAttack(1, 9, "Sword");
                AttackResult fb = characterAttack(repo, db, 1, 9, "Sword");
                check("characterAttack forwards (seed-equivalent)",
                      fa.roll == fb.roll && fa.needed == fb.needed &&
                      fa.hit == fb.hit && fa.damage == fb.damage);
            }
            {
                Dice da(55), db(55);
                RulesAdjudicator adj(repo, da);
                AttackResult ma = adj.monsterAttack(1, 9, "1d6");
                AttackResult mb = monsterAttack(repo, db, 1, 9, "1d6");
                check("monsterAttack forwards (seed-equivalent)",
                      ma.roll == mb.roll && ma.hit == mb.hit && ma.damage == mb.damage);
            }
            {
                Dice da(7), db(7);
                RulesAdjudicator adj(repo, da);
                WanderingResult wa = adj.wandering("Dungeon", 1);
                WanderingResult wb = rollWandering(repo, db, "Dungeon", 1);
                check("wandering forwards (seed-equivalent)",
                      wa.any == wb.any && wa.monster == wb.monster && wa.count == wb.count);
            }
            {
                Dice da(3), db(3);
                RulesAdjudicator adj(repo, da);
                TreasureResult ta = adj.treasure("A");
                TreasureResult tb = rollTreasure(repo, db, "A");
                check("treasure forwards (seed-equivalent)",
                      ta.totalGpValue() == tb.totalGpValue() && ta.gold == tb.gold &&
                      ta.magicItems.size() == tb.magicItems.size());
            }
            {
                Dice da(9), db(9);
                RulesAdjudicator adj(repo, da);
                TurnResult ua = adj.turnUndead(1, "Skeleton");
                TurnResult ub = turnUndead(repo, db, 1, "Skeleton");
                check("turnUndead forwards (seed-equivalent)",
                      ua.possible == ub.possible && ua.needed == ub.needed &&
                      ua.roll == ub.roll && ua.turned == ub.turned);
            }

            // Value checks anchored to existing rules-test constants.
            {
                Dice d(1);
                RulesAdjudicator adj(repo, d);
                RulesAdjudicator::SaveOutcome sv = adj.savingThrow(morgan, SaveCategory::DeathPoison);
                check("savingThrow needed matches table (12)", sv.needed == 12);
                check("savingThrow success is roll>=needed", sv.success == (sv.roll >= sv.needed));

                AttackResult at = adj.characterAttack(1, 9, "Sword");
                check("characterAttack needed vs AC9 = 10", at.needed == 10);
                check("characterAttack hit flag consistent with roll",
                      at.hit == (at.roll >= at.needed));
                check("characterAttack damage in 1..8 on hit",
                      !at.hit || (at.damage >= 1 && at.damage <= 8));

                check("monsterXp(Orc) via facade = 10", adj.monsterXp(*repo.monster("Orc")) == 10);
            }

            // Authored-chance area checks at the deterministic extremes.
            {
                Dice d(1);
                RulesAdjudicator adj(repo, d);
                Area trapped; trapped.trapChancePct = 100; trapped.trapDescription = "Pit trap";
                Area safe;    safe.trapChancePct = 0;      safe.trapDescription = "Pit trap";
                RulesAdjudicator::CheckOutcome sprung = adj.trapCheck(trapped);
                RulesAdjudicator::CheckOutcome clear = adj.trapCheck(safe);
                check("trapCheck at 100% fires with authored text",
                      sprung.occurred && sprung.description == "Pit trap");
                check("trapCheck at 0% does not fire and has no text",
                      !clear.occurred && clear.description.empty());
                check("check(100)/check(0) extremes", adj.check(100) && !adj.check(0));
            }
        }

        // ---- EncounterDirector: assemble encounters (M4 Actor) ----
        std::printf("== encounter ==\n");
        {
            EncounterDirector dir(repo, dice);   // reuse the suite's seeded Dice

            // Reaction table (pure, deterministic).
            check("reaction 2 = hostile", reactionFor2d6(2) == Reaction::Hostile);
            check("reaction 7 = neutral", reactionFor2d6(7) == Reaction::Neutral);
            check("reaction 12 = friendly", reactionFor2d6(12) == Reaction::Friendly);
            check("reactionText neutral", std::string(reactionText(Reaction::Neutral)) == "neutral");

            // Area with a guaranteed encounter resolves a known gns.db monster.
            Area lair; lair.monsterChancePct = 100; lair.monsterType = "Orc";
            Encounter e = dir.checkArea(lair);
            const MonsterDef* orc = repo.monster("Orc");
            const int orcHd = orc ? orc->hitDiceNum.value_or(1) : 1;
            check("area encounter occurs at 100%", e.occurred);
            check("area encounter resolves a known monster",
                  e.known && e.monsters.size() == 1 && e.monsters[0].name == "Orc");
            check("orc hp rolled in a valid HD range and hp==maxHp",
                  e.monsters[0].maxHp >= 1 && e.monsters[0].maxHp <= orcHd * 8 + 8 &&
                  e.monsters[0].hp == e.monsters[0].maxHp);

            // No encounter at 0%.
            Area empty; empty.monsterChancePct = 0; empty.monsterType = "Orc";
            Encounter none = dir.checkArea(empty);
            check("no area encounter at 0%", !none.occurred && none.monsters.empty());

            // Group size honored; AC pulled from the stat block.
            Encounter band = dir.makeEncounter("Orc", 3, false);
            check("group of 3 built and known", band.monsters.size() == 3 && band.known);
            check("combatant AC from stat block",
                  orc && band.monsters[0].armorClass == orc->armorClassNum.value_or(9));

            // Unknown / free-text type still yields usable combatants.
            Encounter weird = dir.makeEncounter("Giant Space Hamster", 2, false);
            check("unknown monster flagged but still built",
                  !weird.known && weird.monsters.size() == 2 &&
                  weird.monsters[0].name == "Giant Space Hamster" && weird.monsters[0].hp >= 1);

            // Wandering check yields a valid encounter for a populated environment.
            Encounter w = dir.checkWandering("Dungeon", 1);
            check("wandering yields a valid encounter",
                  w.occurred && w.fromWandering && !w.monsters.empty() && w.monsters[0].hp >= 1);
        }

        // ---- CombatEngine: auto-resolve a fight (M4 combat loop) ----
        std::printf("== combat loop ==\n");
        {
            Character bram = morgan; bram.name = "Bram";   // second fighter, same stats

            // Pure XP math (no RNG): 3 Orcs at 10 XP each = 30.
            {
                EncounterDirector dir(repo, dice);
                CombatEngine combat(repo, dice);
                Encounter threeOrcs = dir.makeEncounter("Orc", 3, false);
                check("encounterXp sums monster XP (3 Orcs = 30)",
                      combat.encounterXp(threeOrcs) == 30);
            }

            // Termination + bookkeeping invariants (hold for any seed).
            {
                Party party;
                party.members.push_back(morgan);
                party.members.push_back(bram);
                for (auto& pc : party.members) pc.weaponName = "Sword";
                std::vector<int> xp0;
                for (auto& pc : party.members) xp0.push_back(pc.experiencePoints);

                EncounterDirector dir(repo, dice);
                CombatEngine combat(repo, dice);
                Encounter enc = dir.makeEncounter("Orc", 2, false);
                const int preXp = combat.encounterXp(enc);

                CombatResult r = combat.run(party, enc);
                check("combat terminates within the cap", r.rounds >= 1 && r.rounds <= 100);
                check("combat produced a log", !r.log.empty());

                bool monstersDown = true;
                for (auto& m : enc.monsters) if (m.hp > 0) monstersDown = false;
                if (r.outcome == CombatOutcome::PartyVictory) {
                    check("victory <=> all monsters down", monstersDown);
                    check("xp awarded equals encounter xp", r.xpAwarded == preXp);
                    bool xpOk = true; int survivors = 0;
                    for (std::size_t i = 0; i < party.members.size(); ++i) {
                        const Character& pc = party.members[i];
                        if (pc.hp > 0) {
                            ++survivors;
                            if (pc.experiencePoints !=
                                xp0[i] + applyXpBonus(r.xpPerSurvivor, pc.xpBonusPct)) xpOk = false;
                        } else if (pc.experiencePoints != xp0[i]) {
                            xpOk = false;   // a fallen PC earns nothing
                        }
                    }
                    check("survivors gained bonus-adjusted xp share", xpOk && survivors >= 1);
                } else {
                    check("defeat <=> party wiped", party.isWiped());
                }
            }

            // Determinism: identical seeds -> identical result (proves .gnssav replay).
            {
                Party pa; pa.members.push_back(morgan); pa.members.push_back(bram);
                for (auto& pc : pa.members) pc.weaponName = "Sword";
                Party pb = pa;
                Dice d1(2025), d2(2025);
                EncounterDirector da(repo, d1), db(repo, d2);
                Encounter ea = da.makeEncounter("Orc", 2, false);
                Encounter eb = db.makeEncounter("Orc", 2, false);
                CombatEngine ca(repo, d1), cb(repo, d2);
                CombatResult ra = ca.run(pa, ea);
                CombatResult rb = cb.run(pb, eb);
                check("combat is deterministic for a fixed seed",
                      ra.outcome == rb.outcome && ra.rounds == rb.rounds &&
                      ra.xpAwarded == rb.xpAwarded);
            }

            // Empty encounter -> immediate victory.
            {
                Party pe; pe.members.push_back(morgan);
                Encounter none;   // occurred = false, no monsters
                CombatEngine ce(repo, dice);
                CombatResult rn = ce.run(pe, none);
                check("empty encounter is immediate victory",
                      rn.outcome == CombatOutcome::PartyVictory && rn.rounds == 0 &&
                      rn.xpAwarded == 0);
            }
        }

    } catch (const std::exception& ex) {
        std::printf("EXCEPTION: %s\n", ex.what());
        return 2;
    }

    std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return failures ? 1 : 0;
}
