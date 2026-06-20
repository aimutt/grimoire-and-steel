// gns_core unit tests (M0 smoke + M1 rules). Opens the real gns.db (path from CMake).
#include "gns/Database.h"
#include "gns/Content.h"
#include "gns/Dice.h"
#include "gns/Repository.h"
#include "gns/Character.h"
#include "gns/Rules.h"
#include "gns/Module.h"

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
            m.maps.push_back(map);

            ControlPoint cp; cp.id = 1; cp.name = "Sealed gate";
            cp.description = "Opens the crypt."; cp.mapId = 1; cp.areaId = 11;
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

    } catch (const std::exception& ex) {
        std::printf("EXCEPTION: %s\n", ex.what());
        return 2;
    }

    std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return failures ? 1 : 0;
}
