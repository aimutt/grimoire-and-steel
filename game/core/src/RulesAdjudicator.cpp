#include "gns/RulesAdjudicator.h"
#include "gns/Dice.h"

namespace gns {

RulesAdjudicator::RulesAdjudicator(const Repository& repo, Dice& dice)
    : repo_(repo), dice_(dice) {}

// ---- Combat -----------------------------------------------------------------
// gns:: qualification routes to the free functions in Rules.h (the member names
// match, so an unqualified call would recurse).

AttackResult RulesAdjudicator::characterAttack(int level, int targetAc,
                                               const std::string& weapon) {
    return gns::characterAttack(repo_, dice_, level, targetAc, weapon);
}

AttackResult RulesAdjudicator::monsterAttack(int hitDice, int targetAc,
                                             const std::string& damageExpr) {
    return gns::monsterAttack(repo_, dice_, hitDice, targetAc, damageExpr);
}

// ---- Saving throw -----------------------------------------------------------

RulesAdjudicator::SaveOutcome RulesAdjudicator::savingThrow(const Character& c, SaveCategory cat) {
    SaveOutcome o;
    o.needed = saveNeeded(c, cat);
    o.roll = dice_.d20();
    o.success = o.roll >= o.needed;
    return o;
}

// ---- Authored-chance checks -------------------------------------------------

bool RulesAdjudicator::check(int chancePct) {
    return dice_.percent(chancePct);
}

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

// ---- Encounter / reward forwards --------------------------------------------

WanderingResult RulesAdjudicator::wandering(const std::string& environment, int partyLevel) {
    return rollWandering(repo_, dice_, environment, partyLevel);
}

TreasureResult RulesAdjudicator::treasure(const std::string& code, int individuals) {
    return rollTreasure(repo_, dice_, code, individuals);
}

TurnResult RulesAdjudicator::turnUndead(int clericLevel, const std::string& undeadType) {
    return gns::turnUndead(repo_, dice_, clericLevel, undeadType);
}

int RulesAdjudicator::monsterXp(const MonsterDef& m) {
    return gns::monsterXp(repo_, m);
}

} // namespace gns
