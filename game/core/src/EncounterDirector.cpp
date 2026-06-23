#include "gns/EncounterDirector.h"
#include "gns/Repository.h"   // Repository, MonsterDef (via Model.h)
#include "gns/Dice.h"
#include "gns/Rules.h"        // rollWandering / WanderingResult

#include <algorithm>
#include <cctype>
#include <optional>

namespace gns {

// ---- Reaction (canonical Basic D&D 2d6 table) -------------------------------

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

// ---- helpers ----------------------------------------------------------------

namespace {
// Parse a monster hit-dice line ("1", "1+1", "2-1", "3+2") into a d8 count + a
// signed modifier. Prefers the pre-parsed numeric hit dice when present.
void parseHitDice(const std::string& text, const std::optional<int>& hdNum,
                  int& count, int& plus) {
    count = 0;
    plus = 0;
    if (hdNum.has_value()) {
        count = *hdNum;
    } else {
        std::size_t i = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i > 0) { try { count = std::stoi(text.substr(0, i)); } catch (...) { count = 0; } }
    }
    std::size_t s = text.find_first_of("+-");
    if (s != std::string::npos) {
        try { plus = std::stoi(text.substr(s)); } catch (...) { plus = 0; }
    }
}
} // namespace

// ---- EncounterDirector ------------------------------------------------------

EncounterDirector::EncounterDirector(const Repository& repo, Dice& dice)
    : repo_(repo), dice_(dice) {}

int EncounterDirector::rollMonsterHp(const MonsterDef& def) {
    int count = 0, plus = 0;
    parseHitDice(def.hitDiceText, def.hitDiceNum, count, plus);
    int hp = (count <= 0) ? dice_.roll(1, 4)            // sub-1 HD monster
                          : dice_.roll(count, 8, plus); // N d8 + modifier
    return std::max(1, hp);
}

Encounter EncounterDirector::makeEncounter(const std::string& monsterType, int count,
                                           bool fromWandering) {
    Encounter e;
    e.occurred = true;
    e.monsterType = monsterType;
    e.fromWandering = fromWandering;

    const MonsterDef* def = repo_.monster(monsterType);
    e.known = (def != nullptr);

    const int n = std::max(1, count);
    for (int i = 0; i < n; ++i) {
        Combatant c;
        if (def) {
            c.name = def->name;
            c.armorClass = def->armorClassNum.value_or(9);
            c.hitDiceNum = def->hitDiceNum.value_or(1);
            c.damage = def->damage;
            c.maxHp = rollMonsterHp(*def);
        } else {
            // Free-text / unknown type: usable defaults so combat can proceed.
            c.name = monsterType;
            c.armorClass = 9;
            c.hitDiceNum = 1;
            c.damage = "1d6";
            c.maxHp = std::max(1, dice_.roll(1, 8));
        }
        c.hp = c.maxHp;
        e.monsters.push_back(c);
    }

    e.reaction = rollReaction();
    return e;
}

Encounter EncounterDirector::checkArea(const Area& area) {
    if (!dice_.percent(area.monsterChancePct)) return Encounter{};   // occurred = false
    return makeEncounter(area.monsterType, 1, /*fromWandering=*/false);
}

Encounter EncounterDirector::checkWandering(const std::string& environment, int partyLevel) {
    WanderingResult w = rollWandering(repo_, dice_, environment, partyLevel);
    if (!w.any) return Encounter{};
    return makeEncounter(w.monster, w.count, /*fromWandering=*/true);
}

Reaction EncounterDirector::rollReaction() {
    return reactionFor2d6(dice_.roll(2, 6));
}

} // namespace gns
