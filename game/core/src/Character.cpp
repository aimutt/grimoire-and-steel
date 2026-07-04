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
    ch.armorDefenseBonus = armorBonus;
    ch.defense = 10 + traits.grace + armorBonus + shieldBonus;

    ch.ap = 0;
    ch.strain = 0;
    return ch;
}

void recomputeDefense(Character& c) {
    c.defense = 10 + c.traits.grace + c.armorDefenseBonus + (c.shield ? 1 : 0);
}

void equipInventoryItem(Character& c, size_t idx) {
    if (idx >= c.inventory.size()) return;
    InventoryItem item = c.inventory[idx];   // copy before erase
    const int slot = item.slot;
    if (slot < 1 || slot > 3) return;        // only weapon/armor/shield equip
    c.inventory.erase(c.inventory.begin() + idx);
    if (slot == 1) {                          // weapon
        if (!c.weaponName.empty()) {
            InventoryItem old;
            old.name = c.weaponName; old.slot = 1;
            old.damageDie = c.weaponDamageDie; old.weaponBonus = c.weaponBonus;
            c.inventory.push_back(old);
        }
        c.weaponName = item.name;
        c.weaponDamageDie = item.damageDie.empty() ? "1d6" : item.damageDie;
        c.weaponBonus = item.weaponBonus;
    } else if (slot == 2) {                   // armor
        if (c.armorName != "No armor" && !c.armorName.empty()) {
            InventoryItem old;
            old.name = c.armorName; old.slot = 2; old.defenseBonus = c.armorDefenseBonus;
            c.inventory.push_back(old);
        }
        c.armorName = item.name;
        c.armorDefenseBonus = item.defenseBonus;
    } else {                                  // shield (name not tracked on the character)
        if (c.shield) {
            InventoryItem old; old.name = "Shield"; old.slot = 3; old.defenseBonus = 1;
            c.inventory.push_back(old);
        }
        c.shield = true;
    }
    recomputeDefense(c);
}

void unequipToInventory(Character& c, int slot) {
    if (slot == 1) {
        if (c.weaponName.empty()) return;
        InventoryItem it;
        it.name = c.weaponName; it.slot = 1;
        it.damageDie = c.weaponDamageDie; it.weaponBonus = c.weaponBonus;
        c.inventory.push_back(it);
        c.weaponName.clear(); c.weaponDamageDie = "1d6"; c.weaponBonus = 0;
    } else if (slot == 2) {
        if (c.armorName == "No armor" || c.armorName.empty()) return;
        InventoryItem it;
        it.name = c.armorName; it.slot = 2; it.defenseBonus = c.armorDefenseBonus;
        c.inventory.push_back(it);
        c.armorName = "No armor"; c.armorDefenseBonus = 0;
    } else if (slot == 3) {
        if (!c.shield) return;
        InventoryItem it; it.name = "Shield"; it.slot = 3; it.defenseBonus = 1;
        c.inventory.push_back(it);
        c.shield = false;
    }
    recomputeDefense(c);
}

} // namespace gns
