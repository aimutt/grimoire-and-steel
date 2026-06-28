#pragma once
#include <optional>
#include <string>
#include <vector>

// Reference data structures mirroring the Grimoire & Steel gns.db schema. These
// are loaded once by Repository and consumed by the rules engine. The game treats
// gns.db as read-only reference data.

namespace gns {

// One of the four traits (Might, Grace, Wits, Spirit).
struct Trait {
    int id = 0;
    std::string name;
    std::string description;
    int sortOrder = 0;
};

// An ancestry: each kin has a single gift.
struct Kin {
    int id = 0;
    std::string name;
    std::string description;
    std::string giftName;
    std::string giftDescription;
    int sortOrder = 0;
};

// A training tag; grants +2 to applicable rolls.
struct Training {
    int id = 0;
    std::string name;
    std::string description;
    int sortOrder = 0;
};

// A class ("calling"). Carries its starting choices and signature gift, plus the
// training/weapon options offered at creation (loaded from the join tables).
struct Calling {
    int id = 0;
    std::string name;
    std::string description;
    std::string startingGear;
    std::string armorAllowed;
    std::string giftName;
    std::string giftDescription;
    int sortOrder = 0;
    std::vector<std::string> trainingOptions;   // training names
    std::vector<std::string> weaponOptions;     // weapon names
};

// A weapon class with its damage die (e.g. "One-handed weapon" -> "1d6").
struct WeaponCategory {
    int id = 0;
    std::string name;
    std::string damageDie;   // e.g. "1d6"
    std::string examples;
    int sortOrder = 0;
};

// An armor (or shield) option granting a flat Defense bonus.
struct Armor {
    int id = 0;
    std::string name;
    int defenseBonus = 0;
    std::string notes;
    int sortOrder = 0;
};

// A named difficulty band and its target number for the core 1d20 roll.
struct ChallengeNumber {
    std::string name;        // "Easy", "Normal", ...
    int target = 0;          // target number
    std::string description;
    int sortOrder = 0;
};

// A creature stat block. Life/Defense/attack are fixed numbers (no hit dice).
struct MonsterDef {
    int id = 0;
    std::string name;
    std::string description;
    int life = 1;
    int defense = 10;
    int attackBonus = 0;
    std::string damage;       // damage expression, e.g. "1d8"
    int apValue = 0;          // advancement points awarded
    std::string specialRule;
};

// A spell. All current spells share Challenge 12; some have a combat effect.
struct SpellDef {
    int id = 0;
    std::string name;
    std::string description;
    std::string combatEffect;          // empty if non-combat
    std::optional<int> challengeNumber;
    std::string notes;
};

// An example enchanted weapon (+1..+3).
struct MagicWeaponExample {
    int id = 0;
    std::string name;
    int bonus = 0;
    std::string description;
};

// One row of a calling's cumulative AP advancement table.
struct AdvancementLevel {
    int callingId = 0;
    int level = 0;
    int apRequired = 0;
};

// Advancement points granted for completing a module of a given size.
struct ModuleCompletionAward {
    std::string moduleType;
    int apAward = 0;
    std::string description;
    int sortOrder = 0;
};

// A choice available when a character gains a level.
struct LevelImprovementOption {
    std::string description;
    int sortOrder = 0;
};

} // namespace gns
