#pragma once
#include "gns/Module.h"   // gns::Area

#include <string>
#include <vector>

// Actor: assembles encounters for the play-session (milestone M4).
//
// The EncounterDirector turns an area's authored monster chance into a concrete,
// ready-to-fight encounter: it resolves each monster type to a gns.db stat block
// (fixed Life/Defense/attack/damage/AP) and rolls a 2d6 reaction. It binds a
// read-only Repository + the session's seeded Dice. NPC voice/personality is the
// optional LLM's job; the director itself is fully table-driven and deterministic.

namespace gns {

class Repository;
class Dice;
struct MonsterDef;

// 2d6 monster reaction: 2 hostile, 3-5 unfriendly, 6-8 neutral, 9-11 indifferent,
// 12 friendly. A genre-neutral social roll the Guide may use.
enum class Reaction { Hostile, Unfriendly, Neutral, Indifferent, Friendly };
Reaction reactionFor2d6(int total);
const char* reactionText(Reaction r);

// One monster in an encounter, with its (fixed) Life ready for the combat loop.
struct Combatant {
    std::string name;
    int maxLife = 1;
    int life = 1;
    int defense = 10;
    int attackBonus = 0;
    std::string damage;     // damage expression for its attack (from MonsterDef)
    int apValue = 0;        // advancement points this monster is worth
    std::string specialRule;
};

// A resolved encounter handed to the combat loop (or skipped when !occurred).
struct Encounter {
    bool occurred = false;        // false when the presence roll failed
    std::string monsterType;      // requested type (gns.db name or free text)
    bool known = false;           // true when resolved to a gns.db MonsterDef
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

    // Core builder: resolve a monster type + group size into a populated Encounter
    // (occurred = true). Unknown types still yield combatants with sensible
    // defaults so combat can proceed; known = false flags them.
    Encounter makeEncounter(const std::string& monsterType, int count);

    // Roll a 2d6 reaction.
    Reaction rollReaction();

private:
    // Append `count` combatants of `monsterType` to an existing encounter.
    void appendMonsters(Encounter& e, const std::string& monsterType, int count);
    const Repository& repo_;
    Dice& dice_;
};

} // namespace gns
