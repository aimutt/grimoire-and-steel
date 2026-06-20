#pragma once
#include "gns/Character.h"
#include <string>
#include <vector>

namespace gns {

class Repository;
class Dice;
struct MonsterDef;

// ---- Combat ----
enum class SaveCategory { SpellStaff, MagicWand, DeathPoison, Stone, DragonBreath };

// Attack-table band label for a monster of the given (integer) hit dice.
std::string monsterAttackLabel(int hitDiceNum);

// Roll needed for a level-1..3 character to hit an Armor Class.
int characterToHit(const Repository& repo, int level, int targetAc);
// Roll needed for a monster (by hit dice) to hit an Armor Class.
int monsterToHit(const Repository& repo, int hitDiceNum, int targetAc);

struct AttackResult {
    int roll = 0;        // d20
    int needed = 0;
    bool hit = false;
    int damage = 0;
};
AttackResult characterAttack(const Repository& repo, Dice& dice, int level, int targetAc,
                             const std::string& weaponName);
AttackResult monsterAttack(const Repository& repo, Dice& dice, int hitDiceNum, int targetAc,
                           const std::string& damageExpr);

// Saving throw: needed number, and a rolled result.
int saveNeeded(const Character& c, SaveCategory cat);
bool rollSave(Dice& dice, int needed);   // d20 >= needed

// ---- Encumbrance / movement ----
struct LoadInfo {
    int weightLbs = 0;
    int capacityLbs = 0;
    int maxCapacityLbs = 0;
    bool encumbered = false;
    int effectiveMovementFt = 0;
};
LoadInfo computeLoad(const Repository& repo, const Character& c, int carriedLbs);

// ---- Turn undead ----
struct TurnResult {
    std::string raw;         // "7" | "T" | "no effect"
    bool possible = false;   // cleric can attempt
    bool autoTurned = false; // 'T'
    int needed = 0;          // 2d6 target (if numeric)
    int roll = 0;
    bool turned = false;
};
TurnResult turnUndead(const Repository& repo, Dice& dice, int clericLevel,
                      const std::string& undeadType);

// ---- XP ----
int monsterXp(const Repository& repo, const MonsterDef& m);

// ---- Wandering monsters ----
struct WanderingResult {
    bool any = false;
    std::string monster;
    int count = 0;
};
WanderingResult rollWandering(const Repository& repo, Dice& dice,
                              const std::string& environment, int partyLevel);

// ---- Treasure ----
struct TreasureResult {
    int copper = 0, silver = 0, electrum = 0, gold = 0, platinum = 0;
    std::vector<int> gems;       // each value in gp
    std::vector<int> jewelry;    // each value in gp
    int maps = 0;
    std::vector<std::string> magicItems;
    int totalGpValue() const;
};
TreasureResult rollTreasure(const Repository& repo, Dice& dice, const std::string& code,
                            int individuals = 0);

} // namespace gns
