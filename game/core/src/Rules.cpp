#include "gns/Rules.h"
#include "gns/Repository.h"
#include "gns/Dice.h"
#include <algorithm>

namespace gns {

CheckResult resolveCheck(Dice& dice, int bonus, int target) {
    CheckResult r;
    r.roll = dice.d20();
    r.bonus = bonus;
    r.total = r.roll + bonus;
    r.target = target;
    r.success = (r.roll == 20) || (r.total >= target);
    return r;
}

AttackResult resolveAttack(Dice& dice, int attackBonus, int targetDefense,
                           const std::string& damageDie, int damageBonus) {
    AttackResult r;
    r.roll = dice.d20();
    r.bonus = attackBonus;
    r.total = r.roll + attackBonus;
    r.defense = targetDefense;
    r.hit = (r.roll == 20) || (r.total >= targetDefense);
    if (r.hit) {
        int d = dice.rollExpr(damageDie);
        if (d <= 0) d = dice.roll(1, 6);   // fallback if the die string is unparseable
        r.damage = std::max(1, d + damageBonus);
    }
    return r;
}

CastResult castSpell(Dice& dice, int castBonus, int challenge,
                     int currentStrain, int strainLimitValue) {
    CastResult r;
    r.roll = dice.d20();
    r.bonus = castBonus;
    r.total = r.roll + castBonus;
    r.challenge = challenge;
    r.success = (r.roll == 20) || (r.total >= challenge);
    if (!r.success) {
        r.strainGained = 1;
        r.backlash = (currentStrain + r.strainGained) > strainLimitValue;
    }
    return r;
}

int spellCastBonus(const Character& c) {
    return c.traits.spirit + (hasTraining(c, "Sorcery") ? 2 : 0);
}

int apPerSurvivor(int totalAp, int survivors) {
    return survivors > 0 ? totalAp / survivors : 0;
}

} // namespace gns
