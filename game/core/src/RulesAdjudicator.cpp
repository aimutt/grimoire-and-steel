#include "gns/RulesAdjudicator.h"
#include "gns/Dice.h"

namespace gns {

RulesAdjudicator::RulesAdjudicator(const Repository& repo, Dice& dice)
    : repo_(repo), dice_(dice) {}

// ---- Combat -----------------------------------------------------------------

AttackResult RulesAdjudicator::characterAttack(const Character& c, int targetDefense,
                                               bool ranged) {
    int bonus = ranged ? rangedAttackBonus(c) : meleeAttackBonus(c);
    return resolveAttack(dice_, bonus, targetDefense, c.weaponDamageDie, /*damageBonus=*/0);
}

AttackResult RulesAdjudicator::monsterAttack(int attackBonus, int targetDefense,
                                             const std::string& damage) {
    return resolveAttack(dice_, attackBonus, targetDefense, damage, /*damageBonus=*/0);
}

// ---- Spellcasting -----------------------------------------------------------

CastResult RulesAdjudicator::castSpell(const Character& c, int challenge) {
    return gns::castSpell(dice_, spellCastBonus(c), challenge, c.strain, strainLimit(c));
}

// ---- Generic checks ---------------------------------------------------------

bool RulesAdjudicator::check(int chancePct) {
    return dice_.percent(chancePct);
}

CheckResult RulesAdjudicator::resolve(int bonus, int challenge) {
    return resolveCheck(dice_, bonus, challenge);
}

// ---- Authored-chance area checks --------------------------------------------

RulesAdjudicator::CheckOutcome RulesAdjudicator::trapCheck(const Area& a) {
    CheckOutcome o;
    o.occurred = dice_.percent(a.trapChancePct);
    if (o.occurred) o.description = a.trapDescription;
    return o;
}

RulesAdjudicator::CheckOutcome RulesAdjudicator::lockCheck(const Area& a) {
    CheckOutcome o;
    o.occurred = dice_.percent(a.lockChancePct);
    if (o.occurred) o.description = a.lockDescription;
    return o;
}

RulesAdjudicator::CheckOutcome RulesAdjudicator::hiddenCheck(const Area& a) {
    CheckOutcome o;
    o.occurred = dice_.percent(a.hiddenChancePct);
    if (o.occurred) o.description = a.hiddenDescription;
    return o;
}

} // namespace gns
