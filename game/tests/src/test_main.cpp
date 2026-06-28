// gns_core unit tests (Grimoire & Steel). Opens the real gns.db (path from CMake).
#include "gns/Database.h"
#include "gns/Content.h"
#include "gns/Dice.h"
#include "gns/Repository.h"
#include "gns/Character.h"
#include "gns/CharacterIO.h"
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

        // ---- data smoke ----
        std::printf("== data ==\n");
        check("monster == 6", countRows(db, "monster") == 6);
        check("spell == 10", countRows(db, "spell") == 10);
        check("trait == 4", countRows(db, "trait") == 4);
        check("calling == 4", countRows(db, "calling") == 4);
        check("advancement_level == 40", countRows(db, "advancement_level") == 40);

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
        check("Normal challenge target = 12", repo.challenge("Normal") == 12);
        check("Hard challenge target = 15", repo.challenge("Hard") == 15);
        check("One-handed weapon die = 1d6",
              repo.weaponCategory("One-handed weapon")->damageDie == "1d6");
        check("Two-handed weapon die = 1d8",
              repo.weaponCategory("Two-handed weapon")->damageDie == "1d8");
        check("Mail armor defense bonus = 2", repo.armor("Mail armor")->defenseBonus == 2);
        check("Plate armor defense bonus = 3", repo.armor("Plate armor")->defenseBonus == 3);
        check("Ogre life = 22", repo.monster("Ogre")->life == 22);
        check("Ogre AP = 75", repo.monster("Ogre")->apValue == 75);
        check("Heal challenge = 12", repo.spell("Heal")->challengeNumber.value_or(0) == 12);
        check("Blade is a known calling", repo.calling("Blade") != nullptr);
        check("Blade has training options", !repo.calling("Blade")->trainingOptions.empty());
        {
            const Calling* blade = repo.calling("Blade");
            check("Blade level-5 AP = 1000", blade && repo.apRequired(blade->id, 5) == 1000);
            check("Blade level-1 AP = 0", blade && repo.apRequired(blade->id, 1) == 0);
            check("Blade level for 1200 AP = 5", blade && repo.levelForAp(blade->id, 1200) == 5);
        }

        // ---- Character creation (Dwarf Blade, Might+2 Grace+1 Wits0 Spirit-1) ----
        std::printf("== character ==\n");
        Traits tr; tr.might = 2; tr.grace = 1; tr.wits = 0; tr.spirit = -1;
        check("trait spread {+2,+1,0,-1} is valid", validTraitSpread(tr));
        check("Human needs 4 trainings", requiredTrainingCount("Human") == 4);
        check("Dwarf needs 3 trainings", requiredTrainingCount("Dwarf") == 3);
        Character morgan = makeCharacter(repo, "Morgan", "Dwarf", "Blade", tr,
                                         {"Blades", "Survival", "Lore"}, "Mail armor", true);
        morgan.weaponName = "Sword";
        morgan.weaponDamageDie = "1d6";
        check("Dwarf Blade Life = 13 (10 + Might 2 + Dwarf 1)", morgan.maxLife == 13);
        check("life starts at maxLife", morgan.life == morgan.maxLife);
        check("Defense = 14 (10 + Grace 1 + Mail 2 + Shield 1)", morgan.defense == 14);
        check("melee attack bonus = 4 (Might 2 + Blades 2)", meleeAttackBonus(morgan) == 4);
        check("strain limit = 2 (3 + Spirit -1)", strainLimit(morgan) == 2);
        check("starts with 0 AP", morgan.ap == 0);
        // A Human gains the extra training and no Dwarf Life bonus.
        Character mira = makeCharacter(repo, "Mira", "Human", "Mystic", tr,
                                       {"Sorcery", "Lore", "Healing", "Persuasion"}, "No armor", false);
        check("Human Mystic Life = 12 (10 + Might 2)", mira.maxLife == 12);
        check("Mystic unarmored Defense = 11 (10 + Grace 1)", mira.defense == 11);
        check("spell cast bonus = 1 (Spirit -1 + Sorcery 2)", spellCastBonus(mira) == 1);

        // ---- Core resolution ----
        std::printf("== resolution ==\n");
        {
            CheckResult easy = resolveCheck(dice, 5, 9);
            check("resolveCheck reports total = roll + bonus", easy.total == easy.roll + 5);
            check("resolveCheck success iff nat20 or total>=target",
                  easy.success == (easy.roll == 20 || easy.total >= 9));
            bool valid = true;
            for (int i = 0; i < 500; ++i) {
                CheckResult r = resolveCheck(dice, 0, 12);
                if (r.success != (r.roll == 20 || r.roll >= 12)) valid = false;
            }
            check("resolveCheck consistent over many rolls", valid);
        }

        // ---- Combat ----
        std::printf("== combat ==\n");
        {
            bool ok = true, sawHit = false;
            for (int i = 0; i < 500; ++i) {
                AttackResult ar = resolveAttack(dice, 4, 13, "1d6");   // vs Defense 13
                if (ar.hit) {
                    sawHit = true;
                    if (ar.damage < 1 || ar.damage > 6) ok = false;     // 1d6
                    if (!(ar.roll == 20 || ar.total >= 13)) ok = false;
                } else if (ar.damage != 0) ok = false;
            }
            check("attack hits land 1..6 damage and respect Defense", ok && sawHit);
        }
        {
            // Deterministic nat-20 path: with a huge bonus, every attack hits.
            AttackResult ar = resolveAttack(dice, 50, 13, "1d6");
            check("overwhelming bonus always hits", ar.hit && ar.damage >= 1);
        }

        // ---- Spellcasting / strain ----
        std::printf("== spell ==\n");
        {
            // Impossible challenge -> always fails -> gains strain; backlash past limit.
            CastResult fail = castSpell(dice, 0, 999, /*strain=*/2, /*limit=*/2);
            check("failed cast gains 1 strain", !fail.success && fail.strainGained == 1);
            check("failure beyond strain limit triggers backlash", fail.backlash);
            // Trivial challenge -> always succeeds -> no strain.
            CastResult win = castSpell(dice, 0, 1, 0, 2);
            check("trivial cast succeeds with no strain", win.success && win.strainGained == 0);
        }

        // ---- Character .gnschar round-trip ----
        std::printf("== character io ==\n");
        {
            // A Mystic with flavor, trainings, spells, and equipment exercises every field.
            Traits mtr; mtr.might = 0; mtr.grace = 1; mtr.wits = 2; mtr.spirit = -1;
            Character c = makeCharacter(repo, "Yenna", "Elf", "Mystic", mtr,
                                        {"Sorcery", "Lore", "Healing"}, "No armor", false);
            c.playerName = "Sam";
            c.background = "Hedge witch";
            c.goal = "Find the lost grimoire";
            c.personality = "Curious, wary";
            c.notes = "Owes a debt to the river spirits.";
            c.portraitPath = "portrait05.png";
            c.weaponName = "Staff";
            c.weaponDamageDie = "1d4";
            c.weaponBonus = 1;
            c.spells = {"Flame", "Heal", "Veil"};
            c.ap = 250; c.level = 2; c.life = 7; c.strain = 1;

            const std::string path = "gns_character_roundtrip_test.gnschar";
            std::remove(path.c_str());
            saveCharacter(c, path);
            Character r = loadCharacter(path);
            std::remove(path.c_str());

            check("character identity preserved",
                  r.name == "Yenna" && r.playerName == "Sam" && r.kin == "Elf" &&
                  r.calling == "Mystic" && r.level == 2);
            check("character traits preserved",
                  r.traits.might == 0 && r.traits.grace == 1 &&
                  r.traits.wits == 2 && r.traits.spirit == -1);
            check("character derived stats preserved",
                  r.maxLife == c.maxLife && r.life == 7 && r.defense == c.defense &&
                  r.ap == 250 && r.strain == 1);
            check("character equipment preserved",
                  r.armorName == "No armor" && r.shield == false &&
                  r.weaponName == "Staff" && r.weaponDamageDie == "1d4" && r.weaponBonus == 1);
            check("character flavor preserved",
                  r.background == "Hedge witch" && r.goal == "Find the lost grimoire" &&
                  r.personality == "Curious, wary" &&
                  r.notes == "Owes a debt to the river spirits.");
            check("character portrait preserved", r.portraitPath == "portrait05.png");
            check("character trainings preserved",
                  r.trainings.size() == 3 && r.trainings[0] == "Sorcery" &&
                  r.trainings[1] == "Lore" && r.trainings[2] == "Healing");
            check("character spells preserved",
                  r.spells.size() == 3 && r.spells[0] == "Flame" &&
                  r.spells[1] == "Heal" && r.spells[2] == "Veil");
        }

        // ---- Module .gnsmod round-trip (M2 I/O) ----
        std::printf("== module io ==\n");
        {
            Module m;
            m.name = "Tomb of Tests";
            m.author = "QA";
            m.summary = "Round-trip fixture.";
            m.coverArtPath = "art/cover.png";   // module splash image (v8)
            m.splashMusicPath = "audio/splash.mp3";     // module splash/default music (v12)
            m.defaultMusicPath = "audio/overworld.ogg";

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
            a1.musicPath = "audio/entry.ogg";   // per-area music (v12)
            a1.fillEnabled = false;                                 // outline-only (#18)
            a1.labelAuto = false;                                    // hand-edited label (v9)
            a1.monsters = {{"Skeleton", 4}, {"Cave Goblin", 2}};     // multi-type (#23)
            a1.treasures = {{"C", 50}, {"D", 20}};                   // multi-treasure (v10)
            a1.isShop = true;                                         // shop/market (v10/v11)
            a1.shopItems = {{"Long sword", "A fine blade.", 15, 3, "art/sword.png"},
                            {"Rations (1 week)", "", 5, 20, ""}};
            a1.transitions = {{11, "Stairs down to the crypt"}};     // cross-area exit (v7)
            a1.prerequisiteControlPointIds = {1};
            Area a2; a2.id = 11; a2.label = "B1"; a2.name = "Crypt";
            a2.monsterType = "Ogre";   // legacy single field, empty list -> migrated on load
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
            check("module cover art preserved", r.coverArtPath == "art/cover.png");
            check("module music preserved", r.splashMusicPath == "audio/splash.mp3" &&
                  r.defaultMusicPath == "audio/overworld.ogg");
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
            check("area music preserved", ra && ra->musicPath == "audio/entry.ogg");
            check("area prerequisites preserved", ra && ra->prerequisiteControlPointIds.size() == 1 &&
                  ra->prerequisiteControlPointIds[0] == 1);
            check("area fillEnabled=false preserved", ra && ra->fillEnabled == false);
            check("area monster list preserved", ra && ra->monsters.size() == 2 &&
                  ra->monsters[0].type == "Skeleton" && ra->monsters[0].count == 4 &&
                  ra->monsters[1].type == "Cave Goblin" && ra->monsters[1].count == 2);
            check("area treasure list preserved", ra && ra->treasures.size() == 2 &&
                  ra->treasures[0].type == "C" && ra->treasures[0].chancePct == 50 &&
                  ra->treasures[1].type == "D" && ra->treasures[1].chancePct == 20);
            check("area shop preserved", ra && ra->isShop && ra->shopItems.size() == 2 &&
                  ra->shopItems[0].name == "Long sword" &&
                  ra->shopItems[0].description == "A fine blade." &&
                  ra->shopItems[0].costGp == 15 && ra->shopItems[0].stock == 3 &&
                  ra->shopItems[0].imagePath == "art/sword.png" &&
                  ra->shopItems[1].name == "Rations (1 week)" &&
                  ra->shopItems[1].costGp == 5 && ra->shopItems[1].stock == 20);
            check("area transitions preserved", ra && ra->transitions.size() == 1 &&
                  ra->transitions[0].targetAreaId == 11 &&
                  ra->transitions[0].label == "Stairs down to the crypt");
            check("area labelAuto=false preserved", ra && ra->labelAuto == false);
            const Area* rb = r.areaById(11);
            check("area default fillEnabled=true", rb && rb->fillEnabled == true);
            check("area default labelAuto=true preserved", rb && rb->labelAuto == true);
            check("area default isShop=false", rb && rb->isShop == false && rb->shopItems.empty());
            check("legacy monsterType migrated to list", rb && rb->monsters.size() == 1 &&
                  rb->monsters[0].type == "Ogre" && rb->monsters[0].count == 1);
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
            party.members.push_back(morgan);   // level-1 Blade built above

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

            Module m2 = m; m2.startMapId = 0; m2.startAreaId = 0;
            Session s2(m2, party, 1);
            check("session falls back to first map/area",
                  s2.state().mapId == 1 && s2.state().areaId == 10);
            check("fallback is not flagged as declared start", !s2.seatedAtDeclaredStart());

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

            AttackResult hit; hit.hit = true; hit.damage = 6;
            AttackResult miss; miss.hit = false;
            check("factFor(AttackResult) renders a hit",
                  factFor(hit, "Morgan", "Goblin") == "Morgan hits Goblin for 6 damage.");
            check("factFor(AttackResult) renders a miss",
                  factFor(miss, "Morgan", "Goblin") == "Morgan misses Goblin.");

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
                Dice da(2024), db2(2024);
                RulesAdjudicator adj(repo, da);
                AttackResult fa = adj.characterAttack(morgan, 13);
                AttackResult fb = resolveAttack(db2, meleeAttackBonus(morgan), 13, morgan.weaponDamageDie);
                check("characterAttack forwards (seed-equivalent)",
                      fa.roll == fb.roll && fa.total == fb.total &&
                      fa.hit == fb.hit && fa.damage == fb.damage);
            }
            {
                Dice da(55), db2(55);
                RulesAdjudicator adj(repo, da);
                AttackResult ma = adj.monsterAttack(4, 14, "1d8");
                AttackResult mb = resolveAttack(db2, 4, 14, "1d8");
                check("monsterAttack forwards (seed-equivalent)",
                      ma.roll == mb.roll && ma.hit == mb.hit && ma.damage == mb.damage);
            }
            {
                Dice da(9), db2(9);
                RulesAdjudicator adj(repo, da);
                CastResult ua = adj.castSpell(mira, 12);
                CastResult ub = castSpell(db2, spellCastBonus(mira), 12, mira.strain, strainLimit(mira));
                check("castSpell forwards (seed-equivalent)",
                      ua.success == ub.success && ua.total == ub.total &&
                      ua.strainGained == ub.strainGained);
            }

            // Value checks anchored to known stat blocks.
            {
                Dice d(1);
                RulesAdjudicator adj(repo, d);
                AttackResult at = adj.characterAttack(morgan, 13);
                check("characterAttack uses Defense as the target", at.defense == 13);
                check("characterAttack hit flag consistent",
                      at.hit == (at.roll == 20 || at.total >= 13));
                check("characterAttack damage in 1..6 on hit",
                      !at.hit || (at.damage >= 1 && at.damage <= 6));
                CheckResult cr = adj.resolve(2, 12);
                check("resolve forwards a 1d20+bonus check", cr.target == 12 && cr.bonus == 2);
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

            check("reaction 2 = hostile", reactionFor2d6(2) == Reaction::Hostile);
            check("reaction 7 = neutral", reactionFor2d6(7) == Reaction::Neutral);
            check("reaction 12 = friendly", reactionFor2d6(12) == Reaction::Friendly);
            check("reactionText neutral", std::string(reactionText(Reaction::Neutral)) == "neutral");

            // Area with a guaranteed encounter resolves a known gns.db monster.
            Area lair; lair.monsterChancePct = 100; lair.monsterType = "Ogre";
            Encounter en = dir.checkArea(lair);
            const MonsterDef* ogre = repo.monster("Ogre");
            check("area encounter occurs at 100%", en.occurred);
            check("area encounter resolves a known monster",
                  en.known && en.monsters.size() == 1 && en.monsters[0].name == "Ogre");
            check("ogre combatant has fixed Life from the stat block and life==maxLife",
                  ogre && en.monsters[0].maxLife == ogre->life &&
                  en.monsters[0].life == en.monsters[0].maxLife);
            check("combatant Defense/AP from stat block",
                  ogre && en.monsters[0].defense == ogre->defense &&
                  en.monsters[0].apValue == ogre->apValue);

            Area empty; empty.monsterChancePct = 0; empty.monsterType = "Ogre";
            Encounter none = dir.checkArea(empty);
            check("no area encounter at 0%", !none.occurred && none.monsters.empty());

            Encounter band = dir.makeEncounter("Cave Goblin", 3);
            check("group of 3 built and known", band.monsters.size() == 3 && band.known);

            Encounter weird = dir.makeEncounter("Giant Space Hamster", 2);
            check("unknown monster flagged but still built",
                  !weird.known && weird.monsters.size() == 2 &&
                  weird.monsters[0].name == "Giant Space Hamster" && weird.monsters[0].life >= 1);
        }

        // ---- CombatEngine: auto-resolve a fight (M4 combat loop) ----
        std::printf("== combat loop ==\n");
        {
            Character bram = morgan; bram.name = "Bram";   // second blade, same stats

            // Pure AP math (no RNG): 3 Cave Goblins at 10 AP each = 30.
            {
                EncounterDirector dir(repo, dice);
                CombatEngine combat(repo, dice);
                Encounter threeGoblins = dir.makeEncounter("Cave Goblin", 3);
                check("encounterAp sums monster AP (3 Cave Goblins = 30)",
                      combat.encounterAp(threeGoblins) == 30);
            }

            // Termination + bookkeeping invariants (hold for any seed).
            {
                Party party;
                party.members.push_back(morgan);
                party.members.push_back(bram);
                std::vector<int> ap0;
                for (auto& pc : party.members) ap0.push_back(pc.ap);

                EncounterDirector dir(repo, dice);
                CombatEngine combat(repo, dice);
                Encounter enc = dir.makeEncounter("Cave Goblin", 2);
                const int preAp = combat.encounterAp(enc);

                CombatResult rr = combat.run(party, enc);
                check("combat terminates within the cap", rr.rounds >= 1 && rr.rounds <= 100);
                check("combat produced a log", !rr.log.empty());

                bool monstersDown = true;
                for (auto& mm : enc.monsters) if (mm.life > 0) monstersDown = false;
                if (rr.outcome == CombatOutcome::PartyVictory) {
                    check("victory <=> all monsters down", monstersDown);
                    check("ap awarded equals encounter ap", rr.apAwarded == preAp);
                    bool apOk = true; int survivors = 0;
                    for (std::size_t i = 0; i < party.members.size(); ++i) {
                        const Character& pc = party.members[i];
                        if (pc.life > 0) {
                            ++survivors;
                            if (pc.ap != ap0[i] + rr.apPerSurvivor) apOk = false;
                        } else if (pc.ap != ap0[i]) {
                            apOk = false;   // a fallen PC earns nothing
                        }
                    }
                    check("survivors gained the AP share", apOk && survivors >= 1);
                } else {
                    check("defeat <=> party wiped", party.isWiped());
                }
            }

            // Determinism: identical seeds -> identical result (proves .gnssav replay).
            {
                Party pa; pa.members.push_back(morgan); pa.members.push_back(bram);
                Party pb = pa;
                Dice d1(2025), d2(2025);
                EncounterDirector da(repo, d1), db2(repo, d2);
                Encounter ea = da.makeEncounter("Cave Goblin", 2);
                Encounter eb = db2.makeEncounter("Cave Goblin", 2);
                CombatEngine ca(repo, d1), cb(repo, d2);
                CombatResult ra = ca.run(pa, ea);
                CombatResult rb = cb.run(pb, eb);
                check("combat is deterministic for a fixed seed",
                      ra.outcome == rb.outcome && ra.rounds == rb.rounds &&
                      ra.apAwarded == rb.apAwarded);
            }

            // Empty encounter -> immediate victory.
            {
                Party pe; pe.members.push_back(morgan);
                Encounter none;   // occurred = false, no monsters
                CombatEngine ce(repo, dice);
                CombatResult rn = ce.run(pe, none);
                check("empty encounter is immediate victory",
                      rn.outcome == CombatOutcome::PartyVictory && rn.rounds == 0 &&
                      rn.apAwarded == 0);
            }
        }

    } catch (const std::exception& ex) {
        std::printf("EXCEPTION: %s\n", ex.what());
        return 2;
    }

    std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return failures ? 1 : 0;
}
