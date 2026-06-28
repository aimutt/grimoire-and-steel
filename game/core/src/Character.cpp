#include "gns/Character.h"
#include "gns/Repository.h"
#include <algorithm>
#include <array>

namespace gns {

int Traits::get(TraitId t) const {
    switch (t) {
        case TraitId::Might:  return might;
        case TraitId::Grace:  return grace;
        case TraitId::Wits:   return wits;
        case TraitId::Spirit: return spirit;
    }
    return 0;
}
void Traits::set(TraitId t, int v) {
    switch (t) {
        case TraitId::Might:  might = v;  break;
        case TraitId::Grace:  grace = v;  break;
        case TraitId::Wits:   wits = v;   break;
        case TraitId::Spirit: spirit = v; break;
    }
}

bool validTraitSpread(const Traits& t) {
    std::array<int, 4> got{t.might, t.grace, t.wits, t.spirit};
    std::array<int, 4> want{-1, 0, 1, 2};
    std::sort(got.begin(), got.end());
    return got == want;
}

int requiredTrainingCount(const std::string& kinName) {
    return kinName == "Human" ? 4 : 3;   // Human's Adaptable gift grants a 4th training
}

bool hasTraining(const Character& c, const std::string& trainingName) {
    for (const auto& t : c.trainings)
        if (t == trainingName) return true;
    return false;
}

namespace {
// The weapon-type trainings that add their +2 to an attack roll.
bool hasWeaponTraining(const Character& c) {
    return hasTraining(c, "Blades") || hasTraining(c, "Axes") ||
           hasTraining(c, "Bows") || hasTraining(c, "Shields");
}
} // namespace

int meleeAttackBonus(const Character& c) {
    return c.traits.might + c.weaponBonus + (hasWeaponTraining(c) ? 2 : 0);
}
int rangedAttackBonus(const Character& c) {
    return c.traits.grace + c.weaponBonus + (hasWeaponTraining(c) ? 2 : 0);
}
int strainLimit(const Character& c) {
    return std::max(1, 3 + c.traits.spirit);
}

Character makeCharacter(const Repository& repo, const std::string& name,
                        const std::string& kinName, const std::string& callingName,
                        const Traits& traits, const std::vector<std::string>& trainings,
                        const std::string& armorName, bool shield) {
    Character ch;
    ch.name = name;
    ch.kin = kinName;
    ch.calling = callingName;
    ch.level = 1;
    ch.traits = traits;
    ch.trainings = trainings;
    ch.armorName = armorName;
    ch.shield = shield;

    // Life = 10 + Might (minimum 6); Dwarves are tougher by +1.
    int life = std::max(6, 10 + traits.might);
    if (kinName == "Dwarf") life += 1;
    ch.maxLife = life;
    ch.life = life;

    // Defense = 10 + Grace + armor bonus + shield bonus.
    int armorBonus = 0;
    if (const Armor* a = repo.armor(armorName)) armorBonus = a->defenseBonus;
    int shieldBonus = 0;
    if (shield) {
        if (const Armor* sh = repo.armor("Shield")) shieldBonus = sh->defenseBonus;
        else shieldBonus = 1;
    }
    ch.defense = 10 + traits.grace + armorBonus + shieldBonus;

    ch.ap = 0;
    ch.strain = 0;
    return ch;
}

} // namespace gns
