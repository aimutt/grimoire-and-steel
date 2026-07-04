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

// Convert an authored character into an enemy combatant (route: fights alongside monsters).
static Combatant combatantFromCharacter(const Character& pc) {
    Combatant c;
    c.name = pc.name.empty() ? "Enemy" : pc.name;
    c.maxLife = std::max(1, pc.maxLife);
    c.life = c.maxLife;
    c.defense = pc.defense;
    c.attackBonus = meleeAttackBonus(pc);
    c.damage = pc.weaponDamageDie.empty() ? "1d6" : pc.weaponDamageDie;
    if (pc.weaponBonus > 0) c.damage += "+" + std::to_string(pc.weaponBonus);
    c.apValue = std::max(1, pc.maxLife / 2);   // AP worth derived from toughness
    return c;
}

Encounter EncounterDirector::checkArea(const AreaContext& ctx) {
    bool hasFoe = false;
    for (const auto& ac : ctx.characters) if (ac.foe) { hasFoe = true; break; }

    Encounter e;
    // Monsters appear only when the presence roll passes; authored foes always appear.
    if (dice_.percent(ctx.monsterChancePct)) {
        if (ctx.monsters.empty() && !ctx.monsterType.empty()) {
            appendMonsters(e, ctx.monsterType, 1);
            e.monsterType = ctx.monsterType;
        } else {
            if (!ctx.monsters.empty()) e.monsterType = ctx.monsters.front().type;
            for (const auto& am : ctx.monsters) appendMonsters(e, am.type, am.count);
        }
    }
    // Authored characters: foes join the enemy side; allies fight for the party.
    for (const auto& ac : ctx.characters) {
        if (ac.foe) e.monsters.push_back(combatantFromCharacter(ac.character));
        else        e.allies.push_back(ac.character);
    }
    e.occurred = !e.monsters.empty() || hasFoe;   // allies alone do not start a fight
    e.known = (repo_.monster(e.monsterType) != nullptr);
    if (e.occurred) e.reaction = rollReaction();
    return e;
}

Reaction EncounterDirector::rollReaction() {
    return reactionFor2d6(dice_.roll(2, 6));
}

} // namespace gns
