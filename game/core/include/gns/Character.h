#pragma once
#include <string>

namespace gns {

class Repository;
class Dice;

enum class Abil { STR, INT, WIS, DEX, CON, CHA };

struct AbilityScores {
    int str = 10, intel = 10, wis = 10, dex = 10, con = 10, cha = 10;
    int get(Abil a) const;
    void set(Abil a, int v);
};

struct Character {
    std::string name;
    std::string race;
    std::string charClass;
    int level = 1;
    AbilityScores abilities;
    int maxHp = 1;
    int hp = 1;
    int armorClass = 9;        // 9 = unarmored
    int baseMovementFt = 120;
    int xpBonusPct = 0;        // prime-requisite earned-XP modifier
    int experiencePoints = 0;
    // saving throws (level 1-3)
    int saveSpellStaff = 0, saveMagicWand = 0, saveDeathPoison = 0,
        saveStone = 0, saveDragonBreath = 0;
    std::string classGroup;    // saving-throw / class group
};

// Roll 3d6 for each ability.
AbilityScores rollAbilities(Dice& dice);

// Map an ability name ("Strength", ...) to the matching score.
int scoreForAbility(const AbilityScores& s, const std::string& abilityName);

// Saving-throw class group for a class+race (Dwarf/Halfling have their own group).
std::string classGroupFor(const Repository& repo, const std::string& className,
                          const std::string& raceName);

// Build a level-N character: prime-requisite XP bonus, HP (class hit die + CON per die),
// derived saves and movement.
Character makeCharacter(const Repository& repo, Dice& dice, const std::string& name,
                        const std::string& raceName, const std::string& className,
                        const AbilityScores& scores, int level = 1);

} // namespace gns
