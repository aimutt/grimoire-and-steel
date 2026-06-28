#pragma once
#include "gns/Character.h"
#include <string>

namespace gns {

class Repository;
class Dice;
struct MonsterDef;

// Core resolution: 1d20 + bonus vs a target number. A natural 20 always succeeds.
struct CheckResult {
    int roll = 0;       // raw d20
    int bonus = 0;
    int total = 0;      // roll + bonus
    int target = 0;
    bool success = false;
};
CheckResult resolveCheck(Dice& dice, int bonus, int target);

// ---- Combat ----------------------------------------------------------------

// One attack: 1d20 + attackBonus vs the target's Defense. Hit on natural 20 or
// total >= defense; on a hit, damage = roll(damageDie) + damageBonus (min 1).
struct AttackResult {
    int roll = 0;       // raw d20
    int bonus = 0;      // attack bonus added
    int total = 0;      // roll + bonus
    int defense = 0;    // target defense
    bool hit = false;
    int damage = 0;
};
AttackResult resolveAttack(Dice& dice, int attackBonus, int targetDefense,
                           const std::string& damageDie, int damageBonus = 0);

// ---- Spellcasting / Strain -------------------------------------------------

// A spell cast: 1d20 + castBonus (Spirit + Sorcery) vs the spell's Challenge.
// On failure the caster gains 1 Strain; backlash triggers if total strain would
// exceed the caster's safe limit.
struct CastResult {
    int roll = 0;
    int bonus = 0;
    int total = 0;
    int challenge = 0;
    bool success = false;
    int strainGained = 0;   // 0 on success, 1 on failure
    bool backlash = false;  // failure pushed strain past the safe limit
};
CastResult castSpell(Dice& dice, int castBonus, int challenge,
                     int currentStrain, int strainLimitValue);

// Caster's bonus for a spell roll: Spirit + Sorcery training (+2 when trained).
int spellCastBonus(const Character& c);

// ---- Advancement -----------------------------------------------------------

// AP a fight awards, split evenly (rounded down) among `survivors` participants.
int apPerSurvivor(int totalAp, int survivors);

} // namespace gns
