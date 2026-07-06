// gns_core unit tests (Grimoire & Steel). Opens the real gns.db (path from CMake).
#include "gns/Database.h"
#include "gns/Content.h"
#include "gns/Dice.h"
#include "gns/Repository.h"
#include "gns/Character.h"
#include "gns/CharacterIO.h"
#include "gns/SaveIO.h"
#include "gns/Rules.h"
#include "gns/Module.h"
#include "gns/Session.h"
#include "gns/PlotTracker.h"
#include "gns/Narrator.h"
#include "gns/RulesAdjudicator.h"
#include "gns/EncounterDirector.h"
#include "gns/CombatEngine.h"
#include "gns/AtomicDb.h"
#include "sqlite3.h"

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
        check("Short Sword damage = 1d6", repo.equipment("Short Sword")->damage == "1d6");
        check("Long Sword damage = 1d8", repo.equipment("Long Sword")->damage == "1d8");
        check("Battle Axe damage = 1d10", repo.equipment("Battle Axe")->damage == "1d10");
        check("Staff damage = 1d4", repo.equipment("Staff")->damage == "1d4");
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

        // ---- Equip / unequip (drag-and-drop gear management) ----
        {
            Traits gtr; gtr.might = 1; gtr.grace = 1; gtr.wits = 0; gtr.spirit = -1;
            Character eq = makeCharacter(repo, "Gear", "Human", "Blade", gtr,
                                         {"Blades", "Shields", "Survival", "Lore"}, "Mail armor", false);
            check("makeCharacter records armor defense bonus", eq.armorDefenseBonus == 2);
            eq.weaponName = "Short sword"; eq.weaponDamageDie = "1d6";
            InventoryItem axe; axe.name = "Battle Axe"; axe.slot = 1; axe.damageDie = "1d10";
            InventoryItem plate; plate.name = "Plate Armor"; plate.slot = 2; plate.defenseBonus = 3;
            eq.inventory = {axe, plate};
            equipInventoryItem(eq, 0);
            check("equip weapon updates combat die", eq.weaponName == "Battle Axe" && eq.weaponDamageDie == "1d10");
            bool oldWeaponBack = false;
            for (const auto& iv : eq.inventory) if (iv.name == "Short sword" && iv.slot == 1) oldWeaponBack = true;
            check("unequipped old weapon returns to inventory", oldWeaponBack && eq.inventory.size() == 2);
            size_t pidx = 0;
            for (size_t k = 0; k < eq.inventory.size(); ++k) if (eq.inventory[k].name == "Plate Armor") pidx = k;
            equipInventoryItem(eq, pidx);
            check("equip armor recomputes Defense",
                  eq.armorName == "Plate Armor" && eq.armorDefenseBonus == 3 &&
                  eq.defense == 10 + eq.traits.grace + 3);
            unequipToInventory(eq, 1);
            bool axeStored = false;
            for (const auto& iv : eq.inventory) if (iv.name == "Battle Axe" && iv.slot == 1) axeStored = true;
            check("unequip weapon clears the slot and stores it", eq.weaponName.empty() && axeStored);
        }

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
            c.armorName = "Chain Mail"; c.shield = true; c.armorDefenseBonus = 2;   // equip profile (v7)
            c.spells = {"Flame", "Heal", "Veil"};
            c.ap = 250; c.level = 2; c.life = 7; c.strain = 1;
            c.gold = 275;   // economy (v3); rich inventory items (v4); per-item value (v5)
            c.inventory = {{"Torch", "A pitch-soaked brand.", "torch.png", "", 3, 1},
                           {"Lake Charles Map", "A map of the town.", "map.png", "", 1, 25},
                           {"Long Sword", "A blade.", "sword-long.png", "", 1, 20}};
            c.inventory[2].slot = 1; c.inventory[2].damageDie = "1d8"; c.inventory[2].dropable = false;  // v7
            c.inventory[0].onAcquire = {{"litTorches", 1, "1"}};        // carried-item hooks (v6)
            c.inventory[0].onUnacquire = {{"litTorches", 2, "1"}};

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
                  r.armorName == "Chain Mail" && r.shield == true && r.armorDefenseBonus == 2 &&
                  r.weaponName == "Staff" && r.weaponDamageDie == "1d4" && r.weaponBonus == 1);
            check("character flavor preserved",
                  r.background == "Hedge witch" && r.goal == "Find the lost grimoire" &&
                  r.personality == "Curious, wary" &&
                  r.notes == "Owes a debt to the river spirits.");
            check("character portrait preserved", r.portraitPath == "portrait05.png");
            check("character gold + inventory preserved", r.gold == 275 &&
                  r.inventory.size() == 3 && r.inventory[0].name == "Torch" &&
                  r.inventory[0].description == "A pitch-soaked brand." &&
                  r.inventory[0].imageId == "torch.png" && r.inventory[0].quantity == 3 &&
                  r.inventory[1].name == "Lake Charles Map" && r.inventory[1].quantity == 1 &&
                  r.inventory[1].value == 25);
            check("character item equip profile preserved (v7)",
                  r.inventory.size() == 3 && r.inventory[2].slot == 1 &&
                  r.inventory[2].damageDie == "1d8" && r.inventory[2].dropable == false &&
                  r.inventory[0].dropable == true);
            check("character item mutations preserved (v6)",
                  r.inventory.size() == 3 && r.inventory[0].onAcquire.size() == 1 &&
                  r.inventory[0].onAcquire[0].varName == "litTorches" &&
                  r.inventory[0].onAcquire[0].op == 1 &&
                  r.inventory[0].onUnacquire.size() == 1 &&
                  r.inventory[0].onUnacquire[0].op == 2 &&
                  r.inventory[1].onAcquire.empty());
            check("character trainings preserved",
                  r.trainings.size() == 3 && r.trainings[0] == "Sorcery" &&
                  r.trainings[1] == "Lore" && r.trainings[2] == "Healing");
            check("character spells preserved",
                  r.spells.size() == 3 && r.spells[0] == "Flame" &&
                  r.spells[1] == "Heal" && r.spells[2] == "Veil");
        }

        // ---- GameSave .gnssav round-trip (M4 progress save) ----
        std::printf("== save io ==\n");
        {
            Traits t1; t1.might = 2; t1.grace = 1; t1.wits = 0; t1.spirit = -1;
            Character c1 = makeCharacter(repo, "Bram", "Human", "Blade", t1,
                                         {"Blades", "Athletics", "Command", "Survival"}, "Chain", true);
            c1.gold = 40; c1.armorDefenseBonus = 2;   // equip profile (save v7)
            c1.inventory = {{"Long sword", "", "", "", 1}, {"Signet Ring", "A gold signet.", "ring-gold.png", "", 2, 50}};
            c1.inventory[0].slot = 1; c1.inventory[0].damageDie = "1d8"; c1.inventory[0].dropable = false;
            c1.inventory[1].onAcquire = {{"ringWorn", 0, "true"}};     // carried-item hooks (save v5)
            c1.inventory[1].onUnacquire = {{"ringWorn", 0, "false"}};
            c1.ap = 120;
            Traits t2; t2.might = -1; t2.grace = 0; t2.wits = 1; t2.spirit = 2;
            Character c2 = makeCharacter(repo, "Lyra", "Elf", "Mystic", t2,
                                         {"Sorcery", "Lore", "Healing"}, "No armor", false);
            c2.gold = 90; c2.spells = {"Flame", "Veil"}; c2.inventory = {{"Rope (25')", "", "", "", 1}};

            GameSave gs;
            gs.modulePath = "modules/GoblinKingsHollow.gnsmod";
            gs.seed = 0xFEEDFACEC0FFEEULL;
            gs.mapId = 1; gs.areaId = 10; gs.turnCount = 7; gs.mode = 2;
            gs.cursorX = 4; gs.cursorY = 3; gs.faceX = 0; gs.faceY = -1; gs.activeChar = 1;
            gs.elapsedMinutes = 545; gs.paused = 1; gs.minutesSinceRest = 120;   // game clock (v8)
            gs.controlPoints = {1, 3};
            gs.flags = {"helped_mayor", "found_map"};
            gs.resolvedContexts = {{10, "default"}, {12, "guarded"}};
            gs.globals = {{"questAccepted", "true"}, {"teleportsLeft", "3"}};
            gs.deactivatedAreas = {12};
            gs.deletedContexts = {{10, "default"}, {12, "ambush"}};
            gs.grantedDropable = {{"Signet Ring", false}, {"Ancient Key", true}};   // v7
            gs.journal = {"Entered the tavern.", "Agreed to rescue the family."};
            gs.party = {c1, c2};

            const std::string path = "gns_save_roundtrip_test.gnssav";
            std::remove(path.c_str());
            saveGame(path, gs);
            GameSave r = loadGame(path);
            std::remove(path.c_str());

            check("save meta preserved",
                  r.modulePath == "modules/GoblinKingsHollow.gnsmod" &&
                  r.seed == 0xFEEDFACEC0FFEEULL && r.mapId == 1 && r.areaId == 10 &&
                  r.turnCount == 7 && r.mode == 2);
            check("save cursor/facing/active preserved",
                  r.cursorX == 4 && r.cursorY == 3 && r.faceX == 0 && r.faceY == -1 &&
                  r.activeChar == 1);
            check("save game clock preserved (v8)",
                  r.elapsedMinutes == 545 && r.paused == 1 && r.minutesSinceRest == 120);
            check("save plot state preserved",
                  r.controlPoints == gs.controlPoints && r.flags == gs.flags &&
                  r.resolvedContexts == gs.resolvedContexts && r.globals == gs.globals &&
                  r.deactivatedAreas == gs.deactivatedAreas && r.deletedContexts == gs.deletedContexts);
            check("save granted dropable preserved (v7)", r.grantedDropable == gs.grantedDropable);
            check("save item equip profile + armor bonus preserved (v7)",
                  r.party[0].armorDefenseBonus == 2 && r.party[0].inventory[0].slot == 1 &&
                  r.party[0].inventory[0].damageDie == "1d8" && r.party[0].inventory[0].dropable == false);
            check("save journal preserved",
                  r.journal.size() == 2 && r.journal[0] == "Entered the tavern." &&
                  r.journal[1] == "Agreed to rescue the family.");
            check("save party size + order preserved",
                  r.party.size() == 2 && r.party[0].name == "Bram" && r.party[1].name == "Lyra");
            check("save party gold/inventory preserved",
                  r.party[0].gold == 40 && r.party[0].inventory.size() == 2 &&
                  r.party[0].inventory[0].name == "Long sword" &&
                  r.party[0].inventory[1].name == "Signet Ring" &&
                  r.party[0].inventory[1].imageId == "ring-gold.png" &&
                  r.party[0].inventory[1].quantity == 2 &&
                  r.party[0].inventory[1].value == 50 &&
                  r.party[1].gold == 90 && r.party[1].ap == c2.ap);
            check("save item mutations preserved (v5)",
                  r.party[0].inventory.size() == 2 &&
                  r.party[0].inventory[1].onAcquire.size() == 1 &&
                  r.party[0].inventory[1].onAcquire[0].varName == "ringWorn" &&
                  r.party[0].inventory[1].onAcquire[0].value == "true" &&
                  r.party[0].inventory[1].onUnacquire.size() == 1 &&
                  r.party[0].inventory[1].onUnacquire[0].value == "false" &&
                  r.party[0].inventory[0].onAcquire.empty());
            check("save party spells/trainings preserved",
                  r.party[0].trainings.size() == 4 && r.party[1].spells.size() == 2 &&
                  r.party[1].spells[0] == "Flame");
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
            m.startDay = 3; m.startHour = 14; m.startMinute = 30;   // starting date/time (v21)
            m.startYear = 1147; m.eraName = "the Ember Reign";      // calendar year + era name (v22)
            m.variables = {                              // typed globals (v15)
                {"questAccepted", VarType::Bool, "false"},
                {"teleportsLeft", VarType::Int, "5"},
                {"factionName", VarType::String, "neutral"},
                {"threatLevel", VarType::Float, "1.5"},
            };

            Map map;
            map.id = 1; map.name = "Level 1";
            map.gridW = 4; map.gridH = 3;
            map.overlayW = 2; map.overlayH = 1;
            map.minutesPerStep = 25; map.fatigueRestHours = 6;   // per-map game-time tuning (v21)
            map.cells.assign(4 * 3, static_cast<int>(Terrain::Empty));
            map.cellArea.assign(4 * 3, 0);
            map.cells[0] = static_cast<int>(Terrain::Floor);
            map.cells[5] = static_cast<int>(Terrain::Wall);
            map.cellArea[0] = 10;
            map.cellArea[1] = 10;

            Area a1; a1.id = 10; a1.label = "A1"; a1.name = "Entry";
            a1.color = 0x4F8FE0FF;
            a1.hidden = true;                                        // hidden at play (v13)
            a1.offLimits = true;                                     // trigger-from-border, no entry (v20)
            a1.fillEnabled = false;                                 // outline-only (#18)
            a1.labelAuto = false;                                    // hand-edited label (v9)
            a1.prerequisiteControlPointIds = {1};
            a1.onEnter = {{"teleportsLeft", 2, "1"}};               // OnEnter: subtract 1 (v18)
            a1.onExit  = {{"factionName", 0, "wary"}, {"threatLevel", 1, "0.5"}};  // OnExit: set + add

            // Rich default context, active while the quest is not yet accepted (v15).
            AreaContext c0;
            c0.name = "beforeQuest";
            c0.conditions = {{"questAccepted", 0, "false"}};   // questAccepted == false
            c0.dmText = "A trap lurks."; c0.playerText = "A dusty hall.";
            c0.monsterChancePct = 25; c0.monsterType = "Skeleton";
            c0.treasureChancePct = 50; c0.treasureType = "C";
            c0.trapChancePct = 30; c0.trapDescription = "Pit";
            c0.lockChancePct = 15; c0.lockDescription = "Iron door";
            c0.hiddenChancePct = 40; c0.hiddenDescription = "Loose brick";
            c0.artworkPath = "art/entry.png";
            c0.images = {{"art/north.png", 0}, {"art/default.png", -1}};   // multi-image
            c0.defaultImage = 1;
            c0.musicPath = "audio/entry.ogg";
            c0.monsters = {{"Skeleton", 4}, {"Cave Goblin", 2}};
            c0.treasures = {{"C", 50}, {"D", 20}};
            c0.isShop = true;
            c0.shopItems = {{"Long sword", "A fine blade.", 15, 3, "art/sword.png", "battle-axe.png"},
                            {"Rations (1 week)", "", 5, 20, "", ""}};
            c0.shopItems[0].onAcquire = {{"factionName", 0, "armed"}};   // item acquire/loss hooks (v18)
            c0.shopItems[0].onUnacquire = {{"teleportsLeft", 1, "2"}};
            c0.transitions = {{11, "Stairs down to the crypt"}};
            c0.choicePrompt = "The mayor's wife begs for your help.";
            AreaChoice ch0;
            ch0.label = "We'll help you."; ch0.journalEntry = "Agreed to rescue the family.";
            ch0.setFlag = "helped_mayor"; ch0.completeControlPointId = 1; ch0.goldDelta = 50;
            ch0.grantItem = {"Signet Ring", "A gold signet.", "ring-gold.png", "", 2};  // rich granted item (v16)
            ch0.grantItem.onAcquire = {{"questAccepted", 0, "true"}};   // granted-item hooks (v18)
            ch0.grantItem.onUnacquire = {{"factionName", 0, "cursed"}};
            ch0.grantItem.slot = 1; ch0.grantItem.damageDie = "1d8";    // equip profile + dropable (v19)
            ch0.grantItem.weaponBonus = 1; ch0.grantItem.dropable = false;
            ch0.deactivateArea = true;
            ch0.mutations = {{"questAccepted", 0, "true"}, {"teleportsLeft", 2, "1"}};  // set, subtract
            AreaChoice ch1;
            ch1.label = "Not our problem."; ch1.journalEntry = "Declined the plea.";
            ch1.setFlag = "refused_mayor"; ch1.takeItemName = "Old Map";   // no mutations = "does nothing"
            ch1.deleteContext = true;
            ch1.dropableSets = {{"Signet Ring", true}};   // "Set item dropable" effect (v19)
            c0.choices = {ch0, ch1};
            c0.altTexts = {{"helped_mayor", "You've agreed to help - see your journal."}};
            // Authored Context characters (v17): a foe and an ally, with child lists.
            Character foeChar;
            foeChar.name = "Bandit Chief"; foeChar.kin = "Human"; foeChar.calling = "Blade";
            foeChar.maxLife = 14; foeChar.life = 14; foeChar.defense = 12;
            foeChar.weaponName = "Long Sword"; foeChar.weaponDamageDie = "1d8";
            foeChar.trainings = {"Blades"};
            foeChar.inventory = {{"Gold Ring", "A signet.", "ring-gold.png", "", 1, 50}};
            Character allyChar;
            allyChar.name = "Town Guard"; allyChar.kin = "Dwarf"; allyChar.calling = "Blade";
            allyChar.maxLife = 12; allyChar.life = 12; allyChar.defense = 13;
            allyChar.weaponDamageDie = "1d6";
            c0.characters = {{foeChar, true}, {allyChar, false}};

            // Second context, active once the quest is accepted -- proves multiple contexts persist.
            AreaContext c1;
            c1.name = "afterQuest";
            c1.conditions = {{"questAccepted", 0, "true"}};
            c1.playerText = "The hall feels lighter now.";
            a1.contexts = {c0, c1};

            Area a2; a2.id = 11; a2.label = "B1"; a2.name = "Crypt";
            a2.monsterType = "Ogre";   // legacy fields + no contexts -> migrated to a default context
            map.areas.push_back(a1);
            map.areas.push_back(a2);

            MapObject ob1; ob1.id = 1; ob1.type = static_cast<int>(ObjectType::Table);
            ob1.x = 1.5f; ob1.y = 0.5f; ob1.rotationDeg = 90.0f; ob1.scale = 1.75f;  // scale (v13)
            MapObject ob2; ob2.id = 2; ob2.type = static_cast<int>(ObjectType::Compass);  // (v13)
            ob2.x = 2.25f; ob2.y = 1.75f; ob2.rotationDeg = 45.0f; ob2.scale = 0.5f;
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
            check("module start date/time preserved (v21)",
                  r.startDay == 3 && r.startHour == 14 && r.startMinute == 30);
            check("module year + era name preserved (v22)",
                  r.startYear == 1147 && r.eraName == "the Ember Reign");
            check("start/end markers preserved", r.startMapId == 1 && r.startAreaId == 10 && r.endAreaId == 11);
            check("one map preserved", r.maps.size() == 1);
            check("grid dims preserved", r.maps[0].gridW == 4 && r.maps[0].gridH == 3);
            check("overlay dims preserved", r.maps[0].overlayW == 2 && r.maps[0].overlayH == 1);
            check("map game-time fields preserved (v21)",
                  r.maps[0].minutesPerStep == 25 && r.maps[0].fatigueRestHours == 6);
            check("cell arrays preserved", r.maps[0].cells == m.maps[0].cells &&
                                           r.maps[0].cellArea == m.maps[0].cellArea);
            check("two areas preserved", r.maps[0].areas.size() == 2);
            check("module variables preserved", r.variables.size() == 4 &&
                  r.variables[0].name == "questAccepted" && r.variables[0].type == VarType::Bool &&
                  r.variables[0].defaultValue == "false" &&
                  r.variables[1].name == "teleportsLeft" && r.variables[1].type == VarType::Int &&
                  r.variables[1].defaultValue == "5" &&
                  r.variables[2].type == VarType::String && r.variables[2].defaultValue == "neutral" &&
                  r.variables[3].type == VarType::Float && r.variables[3].defaultValue == "1.5");

            const Area* ra = r.areaById(10);
            check("area identity preserved", ra && ra->label == "A1" && ra->name == "Entry" &&
                  ra->color == 0x4F8FE0FF && ra->hidden == true && ra->offLimits == true &&
                  ra->fillEnabled == false && ra->labelAuto == false);
            check("area prerequisites preserved", ra && ra->prerequisiteControlPointIds.size() == 1 &&
                  ra->prerequisiteControlPointIds[0] == 1);
            check("area onEnter/onExit mutations preserved (v18)",
                  ra && ra->onEnter.size() == 1 && ra->onEnter[0].varName == "teleportsLeft" &&
                  ra->onEnter[0].op == 2 && ra->onEnter[0].value == "1" &&
                  ra->onExit.size() == 2 && ra->onExit[0].varName == "factionName" &&
                  ra->onExit[0].op == 0 && ra->onExit[0].value == "wary" &&
                  ra->onExit[1].varName == "threatLevel" && ra->onExit[1].op == 1 &&
                  ra->onExit[1].value == "0.5");
            check("area legacy content cleared", ra && ra->dmText.empty() && ra->choices.empty() &&
                  ra->shopItems.empty() && ra->monsters.empty());
            check("area two contexts preserved", ra && ra->contexts.size() == 2);
            const AreaContext* rc = (ra && ra->contexts.size() == 2) ? &ra->contexts[0] : nullptr;
            check("context name + condition preserved", rc && rc->name == "beforeQuest" &&
                  rc->conditions.size() == 1 && rc->conditions[0].varName == "questAccepted" &&
                  rc->conditions[0].op == 0 && rc->conditions[0].value == "false");
            check("context text/stats preserved", rc && rc->dmText == "A trap lurks." &&
                  rc->playerText == "A dusty hall." && rc->monsterChancePct == 25 &&
                  rc->treasureChancePct == 50 && rc->trapChancePct == 30 &&
                  rc->lockChancePct == 15 && rc->hiddenChancePct == 40 &&
                  rc->artworkPath == "art/entry.png" && rc->musicPath == "audio/entry.ogg");
            check("context images preserved", rc && rc->images.size() == 2 &&
                  rc->images[0].path == "art/north.png" && rc->images[0].direction == 0 &&
                  rc->images[1].direction == -1 && rc->defaultImage == 1);
            check("context monsters preserved", rc && rc->monsters.size() == 2 &&
                  rc->monsters[0].type == "Skeleton" && rc->monsters[0].count == 4 &&
                  rc->monsters[1].type == "Cave Goblin" && rc->monsters[1].count == 2);
            check("context treasures preserved", rc && rc->treasures.size() == 2 &&
                  rc->treasures[0].type == "C" && rc->treasures[0].chancePct == 50 &&
                  rc->treasures[1].type == "D" && rc->treasures[1].chancePct == 20);
            check("context shop preserved", rc && rc->isShop && rc->shopItems.size() == 2 &&
                  rc->shopItems[0].name == "Long sword" &&
                  rc->shopItems[0].description == "A fine blade." &&
                  rc->shopItems[0].costGp == 15 && rc->shopItems[0].stock == 3 &&
                  rc->shopItems[0].imagePath == "art/sword.png" &&
                  rc->shopItems[0].imageId == "battle-axe.png" &&
                  rc->shopItems[1].name == "Rations (1 week)" &&
                  rc->shopItems[1].costGp == 5 && rc->shopItems[1].stock == 20);
            check("context shop item mutations preserved (v18)", rc && rc->shopItems.size() == 2 &&
                  rc->shopItems[0].onAcquire.size() == 1 &&
                  rc->shopItems[0].onAcquire[0].varName == "factionName" &&
                  rc->shopItems[0].onAcquire[0].op == 0 &&
                  rc->shopItems[0].onAcquire[0].value == "armed" &&
                  rc->shopItems[0].onUnacquire.size() == 1 &&
                  rc->shopItems[0].onUnacquire[0].varName == "teleportsLeft" &&
                  rc->shopItems[0].onUnacquire[0].op == 1 &&
                  rc->shopItems[1].onAcquire.empty() && rc->shopItems[1].onUnacquire.empty());
            check("context transitions preserved", rc && rc->transitions.size() == 1 &&
                  rc->transitions[0].targetAreaId == 11 &&
                  rc->transitions[0].label == "Stairs down to the crypt");
            check("context choices preserved", rc && rc->choicePrompt == "The mayor's wife begs for your help." &&
                  rc->choices.size() == 2 &&
                  rc->choices[0].label == "We'll help you." &&
                  rc->choices[0].setFlag == "helped_mayor" &&
                  rc->choices[0].completeControlPointId == 1 &&
                  rc->choices[0].goldDelta == 50 &&
                  rc->choices[0].grantItem.name == "Signet Ring" &&
                  rc->choices[0].grantItem.description == "A gold signet." &&
                  rc->choices[0].grantItem.imageId == "ring-gold.png" &&
                  rc->choices[0].grantItem.quantity == 2 &&
                  rc->choices[0].deactivateArea == true &&
                  rc->choices[1].setFlag == "refused_mayor" &&
                  rc->choices[1].takeItemName == "Old Map" &&
                  rc->choices[1].deleteContext == true);
            check("context choice grant equip profile + dropable preserved (v19)", rc &&
                  rc->choices[0].grantItem.slot == 1 &&
                  rc->choices[0].grantItem.damageDie == "1d8" &&
                  rc->choices[0].grantItem.weaponBonus == 1 &&
                  rc->choices[0].grantItem.dropable == false);
            check("context choice dropable-set effect preserved (v19)", rc &&
                  rc->choices[1].dropableSets.size() == 1 &&
                  rc->choices[1].dropableSets[0].first == "Signet Ring" &&
                  rc->choices[1].dropableSets[0].second == true);
            check("context choice mutations preserved", rc && rc->choices[0].mutations.size() == 2 &&
                  rc->choices[0].mutations[0].varName == "questAccepted" &&
                  rc->choices[0].mutations[0].op == 0 && rc->choices[0].mutations[0].value == "true" &&
                  rc->choices[0].mutations[1].varName == "teleportsLeft" &&
                  rc->choices[0].mutations[1].op == 2 && rc->choices[0].mutations[1].value == "1" &&
                  rc->choices[1].mutations.empty());
            check("context granted-item mutations preserved (v18)", rc && rc->choices.size() == 2 &&
                  rc->choices[0].grantItem.onAcquire.size() == 1 &&
                  rc->choices[0].grantItem.onAcquire[0].varName == "questAccepted" &&
                  rc->choices[0].grantItem.onAcquire[0].value == "true" &&
                  rc->choices[0].grantItem.onUnacquire.size() == 1 &&
                  rc->choices[0].grantItem.onUnacquire[0].varName == "factionName" &&
                  rc->choices[0].grantItem.onUnacquire[0].value == "cursed");
            check("context alt texts preserved", rc && rc->altTexts.size() == 1 &&
                  rc->altTexts[0].requiredFlag == "helped_mayor" &&
                  rc->altTexts[0].text == "You've agreed to help - see your journal.");
            check("context characters preserved", rc && rc->characters.size() == 2 &&
                  rc->characters[0].foe == true &&
                  rc->characters[0].character.name == "Bandit Chief" &&
                  rc->characters[0].character.weaponDamageDie == "1d8" &&
                  rc->characters[0].character.trainings.size() == 1 &&
                  rc->characters[0].character.trainings[0] == "Blades" &&
                  rc->characters[0].character.inventory.size() == 1 &&
                  rc->characters[0].character.inventory[0].value == 50 &&
                  rc->characters[1].foe == false &&
                  rc->characters[1].character.name == "Town Guard" &&
                  rc->characters[1].character.defense == 13);
            const AreaContext* rc1 = (ra && ra->contexts.size() == 2) ? &ra->contexts[1] : nullptr;
            check("second context preserved", rc1 && rc1->name == "afterQuest" &&
                  rc1->conditions.size() == 1 && rc1->conditions[0].value == "true" &&
                  rc1->playerText == "The hall feels lighter now.");
            const Area* rb = r.areaById(11);
            check("area default fillEnabled=true", rb && rb->fillEnabled == true);
            check("area default labelAuto=true preserved", rb && rb->labelAuto == true);
            check("legacy area migrated to one default context", rb && rb->contexts.size() == 1 &&
                  rb->contexts[0].name == "default" && rb->contexts[0].monsters.size() == 1 &&
                  rb->contexts[0].monsters[0].type == "Ogre" && rb->contexts[0].monsters[0].count == 1);
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
                  r.maps[0].objects[1].type == static_cast<int>(ObjectType::Compass) &&
                  r.maps[0].objects[1].x == 2.25f && r.maps[0].objects[1].y == 1.75f);
            check("object rotation preserved", r.maps[0].objects.size() == 2 &&
                  r.maps[0].objects[0].rotationDeg == 90.0f &&
                  r.maps[0].objects[1].rotationDeg == 45.0f);
            check("object scale preserved", r.maps[0].objects.size() == 2 &&
                  r.maps[0].objects[0].scale == 1.75f && r.maps[0].objects[1].scale == 0.5f);
        }

        // ---- Atomic, non-destructive save (data-loss guardrail) ----
        // A save must never destroy the existing file the way the old in-place DROP+CREATE
        // did: it writes a temp file, then swaps it in (keeping a .bak). If the write throws,
        // the original stays intact and no .tmp is left behind.
        std::printf("== atomic save ==\n");
        {
            auto exists = [](const std::string& p) {
                if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
                return false;
            };

            Module m;
            m.name = "Original";
            Map map; map.id = 1; map.name = "M"; map.gridW = 2; map.gridH = 2;
            map.cells.assign(4, static_cast<int>(Terrain::Empty));
            map.cellArea.assign(4, 0);
            Area a; a.id = 1; a.label = "A1"; a.name = "Start"; map.areas.push_back(a);
            m.maps.push_back(map);
            m.startMapId = 1; m.startAreaId = 1; m.endAreaId = 1;

            const std::string path = "gns_atomic_test.gnsmod";
            const std::string tmp  = path + ".tmp";
            const std::string bak  = path + ".bak";
            std::remove(path.c_str()); std::remove(tmp.c_str()); std::remove(bak.c_str());

            saveModule(m, path);
            check("atomic first save leaves no .tmp", !exists(tmp));
            check("atomic first save is loadable", loadModule(path).name == "Original");

            Module m2 = m; m2.name = "Updated";
            saveModule(m2, path);
            check("overwrite leaves no .tmp", !exists(tmp));
            check("overwrite kept previous file as .bak", exists(bak));
            check("overwrite loads new content", loadModule(path).name == "Updated");

            // The guardrail: a save that throws part-way must leave the original untouched.
            bool threw = false;
            try {
                writeDatabaseAtomically(path, [](sqlite3* db) {
                    char* e = nullptr;
                    sqlite3_exec(db, "CREATE TABLE partial(x);", nullptr, nullptr, &e);
                    if (e) sqlite3_free(e);
                    throw DbError("simulated mid-save failure");
                });
            } catch (const std::exception&) { threw = true; }
            check("failed save reports the error", threw);
            check("failed save leaves no .tmp", !exists(tmp));
            check("failed save leaves the original intact", loadModule(path).name == "Updated");

            std::remove(path.c_str()); std::remove(tmp.c_str()); std::remove(bak.c_str());
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

            // decision flags + resolved areas + alt-text selection (v14)
            check("fresh tracker has no flags", !pt.hasFlag("helped_mayor"));
            pt.setFlag("helped_mayor");
            check("flag recorded", pt.hasFlag("helped_mayor") && pt.flags().count("helped_mayor") == 1);
            check("context not resolved by default", !pt.isChoiceResolved(10, "ctxA"));
            pt.resolveChoiceContext(10, "ctxA");
            check("context marked resolved", pt.isChoiceResolved(10, "ctxA") &&
                  pt.resolvedChoiceContexts().count({10, "ctxA"}) == 1);
            check("sibling context in same area still unresolved", !pt.isChoiceResolved(10, "ctxB"));
            pt.setFlags({"refused_mayor"});
            check("flags restored wholesale", pt.hasFlag("refused_mayor") && !pt.hasFlag("helped_mayor"));
            pt.setResolvedChoiceContexts({{20, "start"}});
            check("resolved contexts restored wholesale",
                  pt.isChoiceResolved(20, "start") && !pt.isChoiceResolved(10, "ctxA"));

            // Granted-item dropable status (v19): non-granted always droppable; seed sets a default;
            // setItemDropable overrides; seed is insert-if-absent.
            check("unknown item is droppable", pt.isDropable("Random Rock") && !pt.isGrantedName("Random Rock"));
            pt.seedItemDropable("Cursed Blade", false);
            check("seeded granted item honors default", pt.isGrantedName("Cursed Blade") && !pt.isDropable("Cursed Blade"));
            pt.seedItemDropable("Cursed Blade", true);   // insert-if-absent: does not overwrite
            check("seed does not overwrite existing", !pt.isDropable("Cursed Blade"));
            pt.setItemDropable("Cursed Blade", true);
            check("setItemDropable overrides", pt.isDropable("Cursed Blade"));

            // v15: global variables, context conditions, and single-active-context selection.
            std::vector<ModuleVariable> vars = {
                {"questAccepted", VarType::Bool, "false"},
                {"gold", VarType::Int, "10"},
                {"faction", VarType::String, "neutral"},
                {"threat", VarType::Float, "1.5"},
            };
            PlotTracker pt3;
            initGlobals(pt3, vars);
            check("globals seeded from defaults",
                  pt3.getGlobal("questAccepted") == "false" && pt3.getGlobal("gold") == "10");

            // areaContextText: default player text, then legacy alt-text within a context.
            AreaContext ctxT;
            ctxT.playerText = "The mayor's wife begs for help.";
            ctxT.altTexts = {{"helped_mayor", "You've agreed to help."}};
            check("context text default when flag unset",
                  areaContextText(ctxT, pt3) == "The mayor's wife begs for help.");
            pt3.setFlag("helped_mayor");
            check("context alt text used when flag set",
                  areaContextText(ctxT, pt3) == "You've agreed to help.");

            // Clause evaluation, one per type + ordering.
            check("bool clause == default", evalClause({"questAccepted", 0, "false"}, pt3, vars));
            check("int clause > holds", evalClause({"gold", 4, "5"}, pt3, vars));      // 10 > 5
            check("int clause <= fails", !evalClause({"gold", 3, "5"}, pt3, vars));    // !(10 <= 5)
            check("string clause != holds", evalClause({"faction", 1, "evil"}, pt3, vars));
            check("float clause >= holds", evalClause({"threat", 5, "1.5"}, pt3, vars));

            // activeContext: exactly one, flips with a variable, none = inert, two = conflict.
            Area asel; asel.id = 40; asel.name = "Gate";
            AreaContext before; before.name = "before"; before.conditions = {{"questAccepted", 0, "false"}};
            AreaContext after;  after.name = "after";   after.conditions = {{"questAccepted", 0, "true"}};
            asel.contexts = {before, after};
            std::string conflict;
            const AreaContext* act = activeContext(asel, pt3, vars, &conflict);
            check("exactly one context active", act && act->name == "before" && conflict.empty());
            pt3.setGlobal("questAccepted", "true");
            act = activeContext(asel, pt3, vars, &conflict);
            check("active context flips with the variable", act && act->name == "after");

            Area none; none.id = 41;
            AreaContext only; only.name = "x"; only.conditions = {{"questAccepted", 0, "false"}};
            none.contexts = {only};
            conflict.clear();
            check("no active context is inert (nullptr, no error)",
                  activeContext(none, pt3, vars, &conflict) == nullptr && conflict.empty());

            Area two; two.id = 42;
            AreaContext ca; ca.name = "ca"; ca.conditions = {{"questAccepted", 0, "true"}};
            AreaContext cb; cb.name = "cb"; cb.conditions = {{"gold", 4, "0"}};   // gold > 0, also true
            two.contexts = {ca, cb};
            conflict.clear();
            check("two active contexts report a conflict",
                  activeContext(two, pt3, vars, &conflict) == nullptr && !conflict.empty());

            // A choice-deleted context is skipped, so the remaining one becomes the sole active
            // context (a conflict becomes a clean single-active resolution).
            pt3.deleteContext(42, "ca");
            conflict.clear();
            const AreaContext* actDel = activeContext(two, pt3, vars, &conflict);
            check("deleted context is skipped by activeContext",
                  actDel && actDel->name == "cb" && conflict.empty());
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
