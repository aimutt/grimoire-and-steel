#pragma once
#include "gns/Module.h"   // gns::Area

#include <string>
#include <vector>

// Actor: assembles encounters for the play-session (milestone M4).
//
// The EncounterDirector turns an area's authored monster chance (or a wandering
// check) into a concrete, ready-to-fight encounter: it resolves the monster type
// to a gns.db stat block, rolls group size + per-monster HP, and rolls a 2d6
// reaction. It binds a read-only Repository + the session's seeded Dice (the same
// shape as RulesAdjudicator); the monster-presence roll is the same Dice::percent
// primitive the adjudicator's check() wraps. NPC voice/personality is the optional
// LLM's job -- the director itself is fully table-driven and deterministic.
//
// The reaction is computed in code from the canonical Basic D&D 2d6 table; it is
// not yet data-driven from gns.db (an easy later swap).

namespace gns {

class Repository;
class Dice;
struct MonsterDef;

// 2d6 monster reaction (Basic D&D): 2 hostile, 3-5 unfriendly, 6-8 neutral,
// 9-11 indifferent, 12 friendly.
enum class Reaction { Hostile, Unfriendly, Neutral, Indifferent, Friendly };
Reaction reactionFor2d6(int total);
const char* reactionText(Reaction r);

// One monster in an encounter, with HP already rolled for the combat loop.
struct Combatant {
    std::string name;
    int maxHp = 1;
    int hp = 1;
    int armorClass = 9;
    int hitDiceNum = 1;
    std::string damage;     // damage expression for its attack (from MonsterDef)
};

// A resolved encounter handed to the combat loop (or skipped when !occurred).
struct Encounter {
    bool occurred = false;        // false when the presence / wandering roll failed
    std::string monsterType;      // requested type (gns.db name or free text)
    bool known = false;           // true when resolved to a gns.db MonsterDef
    bool fromWandering = false;
    std::vector<Combatant> monsters;
    Reaction reaction = Reaction::Neutral;
};

class EncounterDirector {
public:
    EncounterDirector(const Repository& repo, Dice& dice);

    // Area entry: roll the area's monsterChancePct once; on success spawn every
    // {type, count} row in area.monsters (falling back to the legacy single
    // area.monsterType, group size 1, when the list is empty).
    Encounter checkArea(const Area& area);

    // Wandering check for an environment + party level (count comes from the
    // wandering table's number-appearing).
    Encounter checkWandering(const std::string& environment, int partyLevel);

    // Core builder: resolve a monster type + group size into a populated Encounter
    // (occurred = true). Unknown types still yield combatants with sensible
    // defaults so combat can proceed; known = false flags them.
    Encounter makeEncounter(const std::string& monsterType, int count, bool fromWandering);

    // Roll a 2d6 reaction.
    Reaction rollReaction();

private:
    int rollMonsterHp(const MonsterDef& def);
    // Append `count` combatants of `monsterType` to an existing encounter.
    void appendMonsters(Encounter& e, const std::string& monsterType, int count);
    const Repository& repo_;
    Dice& dice_;
};

} // namespace gns
