#include "gns/EncounterDirector.h"
#include "gns/Repository.h"   // Repository, MonsterDef (via Model.h)
#include "gns/Dice.h"

#include <algorithm>

namespace gns {

// ---- Reaction (2d6 social roll) ---------------------------------------------

Reaction reactionFor2d6(int total) {
    if (total <= 2)  return Reaction::Hostile;
    if (total <= 5)  return Reaction::Unfriendly;
    if (total <= 8)  return Reaction::Neutral;
    if (total <= 11) return Reaction::Indifferent;
    return Reaction::Friendly;
}

const char* reactionText(Reaction r) {
    switch (r) {
        case Reaction::Hostile:     return "hostile";
        case Reaction::Unfriendly:  return "unfriendly";
        case Reaction::Neutral:     return "neutral";
        case Reaction::Indifferent: return "indifferent";
        case Reaction::Friendly:    return "friendly";
    }
    return "neutral";
}

// ---- EncounterDirector ------------------------------------------------------

EncounterDirector::EncounterDirector(const Repository& repo, Dice& dice)
    : repo_(repo), dice_(dice) {}

void EncounterDirector::appendMonsters(Encounter& e, const std::string& monsterType,
                                       int count) {
    const MonsterDef* def = repo_.monster(monsterType);
    const int n = std::max(1, count);
    for (int i = 0; i < n; ++i) {
        Combatant c;
        if (def) {
            c.name = def->name;
            c.defense = def->defense;
            c.attackBonus = def->attackBonus;
            c.damage = def->damage;
            c.apValue = def->apValue;
            c.specialRule = def->specialRule;
            c.maxLife = std::max(1, def->life);
        } else {
            // Free-text / unknown type: usable defaults so combat can proceed.
            c.name = monsterType;
            c.defense = 10;
            c.attackBonus = 1;
            c.damage = "1d6";
            c.apValue = 0;
            c.maxLife = 6;
        }
        c.life = c.maxLife;
        e.monsters.push_back(c);
    }
}

Encounter EncounterDirector::makeEncounter(const std::string& monsterType, int count) {
    Encounter e;
    e.occurred = true;
    e.monsterType = monsterType;
    e.known = (repo_.monster(monsterType) != nullptr);
    appendMonsters(e, monsterType, count);
    e.reaction = rollReaction();
    return e;
}

Encounter EncounterDirector::checkArea(const Area& area) {
    if (!dice_.percent(area.monsterChancePct)) return Encounter{};   // occurred = false
    if (area.monsters.empty())
        return makeEncounter(area.monsterType, 1);

    // Multiple types: spawn each row's count into one encounter.
    Encounter e;
    e.occurred = true;
    e.monsterType = area.monsters.front().type;        // representative label
    e.known = (repo_.monster(e.monsterType) != nullptr);
    for (const auto& am : area.monsters)
        appendMonsters(e, am.type, am.count);
    e.reaction = rollReaction();
    return e;
}

Reaction EncounterDirector::rollReaction() {
    return reactionFor2d6(dice_.roll(2, 6));
}

} // namespace gns
