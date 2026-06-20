#include "gns/Rules.h"
#include "gns/Repository.h"
#include "gns/Database.h"
#include "gns/Dice.h"
#include <algorithm>
#include <cmath>

namespace gns {

// ---------------- Combat ----------------

std::string monsterAttackLabel(int hd) {
    if (hd <= 1) return "up to 1+1";
    if (hd == 2) return "1+1 to 2";
    if (hd == 3) return "2 to 3";
    if (hd == 4) return "3+ to 4";
    if (hd <= 6) return "4+ to 6+";
    if (hd <= 8) return "7 to 8+";
    if (hd <= 10) return "9 to 10+";
    return "11 up";
}

int characterToHit(const Repository& repo, int level, int targetAc) {
    const char* label = (level >= 1 && level <= 3) ? "1st-3rd Level Character" : "Normal Man";
    return repo.attackRollNeeded("character", label, targetAc);
}
int monsterToHit(const Repository& repo, int hd, int targetAc) {
    return repo.attackRollNeeded("monster", monsterAttackLabel(hd), targetAc);
}

AttackResult characterAttack(const Repository& repo, Dice& dice, int level, int targetAc,
                             const std::string& weaponName) {
    AttackResult r;
    r.needed = characterToHit(repo, level, targetAc);
    r.roll = dice.d20();
    r.hit = (r.roll == 20) || (r.roll >= r.needed);
    if (r.hit) {
        if (const WeaponDef* w = repo.weapon(weaponName); w && w->damageMax > 0)
            r.damage = dice.rollRange(w->damageMin, w->damageMax);
        else
            r.damage = dice.roll(1, 6);   // default weapon damage
    }
    return r;
}

AttackResult monsterAttack(const Repository& repo, Dice& dice, int hd, int targetAc,
                           const std::string& damageExpr) {
    AttackResult r;
    r.needed = monsterToHit(repo, hd, targetAc);
    r.roll = dice.d20();
    r.hit = (r.roll == 20) || (r.roll >= r.needed);
    if (r.hit) {
        int d = dice.rollExpr(damageExpr);
        r.damage = d > 0 ? d : dice.roll(1, 6);
    }
    return r;
}

int saveNeeded(const Character& c, SaveCategory cat) {
    switch (cat) {
        case SaveCategory::SpellStaff: return c.saveSpellStaff;
        case SaveCategory::MagicWand: return c.saveMagicWand;
        case SaveCategory::DeathPoison: return c.saveDeathPoison;
        case SaveCategory::Stone: return c.saveStone;
        case SaveCategory::DragonBreath: return c.saveDragonBreath;
    }
    return 20;
}
bool rollSave(Dice& dice, int needed) { return dice.d20() >= needed; }

// ---------------- Encumbrance ----------------

LoadInfo computeLoad(const Repository& repo, const Character& c, int carriedLbs) {
    const Race* race = repo.race(c.race);
    double mod = race ? race->carryModifier : 1.0;
    const StrengthCapacity* sc = repo.strengthCapacity(c.abilities.str);
    int base = sc ? sc->baseLbs : 0;
    int mx = sc ? sc->maxLbs : 0;

    LoadInfo l;
    l.weightLbs = carriedLbs;
    l.capacityLbs = static_cast<int>(base * mod);
    l.maxCapacityLbs = static_cast<int>(mx * mod);
    l.encumbered = carriedLbs > l.capacityLbs;
    int bm = c.baseMovementFt;
    if (carriedLbs <= l.capacityLbs) l.effectiveMovementFt = bm;
    else if (carriedLbs <= l.maxCapacityLbs) l.effectiveMovementFt = bm / 2;
    else l.effectiveMovementFt = 0;
    return l;
}

// ---------------- Turn undead ----------------

TurnResult turnUndead(const Repository& repo, Dice& dice, int clericLevel,
                      const std::string& undeadType) {
    TurnResult t;
    t.raw = repo.turnUndead(clericLevel, undeadType);
    if (t.raw.empty() || t.raw == "no effect") return t;
    t.possible = true;
    if (t.raw == "T") { t.autoTurned = true; t.turned = true; return t; }
    try { t.needed = std::stoi(t.raw); } catch (...) { t.possible = false; return t; }
    t.roll = dice.roll(2, 6);
    t.turned = t.roll >= t.needed;
    return t;
}

// ---------------- XP ----------------

static std::string xpLabel(const MonsterDef& m) {
    if (!m.hitDiceNum) return "Under 1";
    int hd = *m.hitDiceNum;
    if (hd < 1) return "Under 1";
    bool plus1 = (m.hitDiceText.find("+ 1") != std::string::npos) ||
                 (m.hitDiceText.find("+1") != std::string::npos);
    if (hd > 5) return plus1 ? "5+1" : "5";   // beyond the Basic table; approximate
    return plus1 ? (std::to_string(hd) + "+1") : std::to_string(hd);
}

int monsterXp(const Repository& repo, const MonsterDef& m) {
    return repo.monsterXpValue(xpLabel(m), /*special=*/false);
}

// ---------------- Wandering ----------------

WanderingResult rollWandering(const Repository& repo, Dice& dice,
                              const std::string& environment, int partyLevel) {
    WanderingResult r;
    auto entries = repo.wandering(environment, partyLevel);
    if (entries.empty()) return r;
    int total = 0;
    for (auto* e : entries) total += std::max(1, e->weight);
    int pick = dice.rollRange(1, total);
    const WanderingEntry* chosen = entries.back();
    int acc = 0;
    for (auto* e : entries) {
        acc += std::max(1, e->weight);
        if (pick <= acc) { chosen = e; break; }
    }
    r.any = true;
    r.monster = chosen->monster;
    r.count = dice.rollRange(chosen->numberMin, chosen->numberMax);
    return r;
}

// ---------------- Treasure ----------------

struct CellAmount {
    bool present = false;
    bool perIndividual = false;
    int min = 0, max = 0;
    int pct = 0;
};

static bool leadingRange(const std::string& s, int& lo, int& hi) {
    try {
        std::size_t p = 0;
        lo = std::stoi(s, &p);
        while (p < s.size() && s[p] != '-') ++p;
        if (p >= s.size()) { hi = lo; return true; }
        ++p;
        hi = std::stoi(s.substr(p));
        return true;
    } catch (...) { return false; }
}

static CellAmount parseAmount(const std::string& cell) {
    CellAmount a;
    if (cell.empty() || cell == "nil") return a;
    if (cell.find("per individual") != std::string::npos) {
        if (leadingRange(cell, a.min, a.max)) { a.present = true; a.perIndividual = true; a.pct = 100; }
        return a;
    }
    std::size_t colon = cell.find(':');
    if (colon != std::string::npos) {
        if (leadingRange(cell.substr(0, colon), a.min, a.max)) {
            try { a.pct = std::stoi(cell.substr(colon + 1)); } catch (...) { a.pct = 0; }
            a.present = true;
        }
    }
    return a;
}

static int gemValue(Dice& dice) {
    static const int tiers[] = {10, 50, 100, 500, 1000};
    int roll = dice.d100();
    int idx = roll <= 20 ? 0 : roll <= 45 ? 1 : roll <= 75 ? 2 : roll <= 95 ? 3 : 4;
    while (idx < 4 && dice.d6() == 1) ++idx;
    return tiers[idx];
}

static int coins(Dice& dice, const CellAmount& a, int individuals) {
    if (!a.present) return 0;
    if (a.perIndividual)
        return dice.rollRange(a.min, a.max) * std::max(1, individuals);
    if (!dice.percent(a.pct)) return 0;
    return dice.rollRange(a.min, a.max) * 1000;   // columns are "1000's of"
}

static std::string randomMagicItem(Repository& repo, const std::string& category = "") {
    std::string sql = "SELECT name FROM magic_item";
    if (!category.empty())
        sql += " m JOIN magic_item_category c ON c.id=m.category_id WHERE c.name='" + category + "'";
    sql += " ORDER BY RANDOM() LIMIT 1";
    return repo.db().scalar(sql);
}

int TreasureResult::totalGpValue() const {
    double gp = gold + platinum * 5.0 + electrum * 0.5 + silver * 0.1 + copper * 0.02;
    for (int g : gems) gp += g;
    for (int j : jewelry) gp += j;
    return static_cast<int>(gp);
}

TreasureResult rollTreasure(const Repository& repo, Dice& dice, const std::string& code,
                            int individuals) {
    TreasureResult t;
    const TreasureTypeRow* tt = repo.treasureType(code);
    if (!tt) return t;

    t.copper = coins(dice, parseAmount(tt->copper), individuals);
    t.silver = coins(dice, parseAmount(tt->silver), individuals);
    t.electrum = coins(dice, parseAmount(tt->electrum), individuals);
    t.gold = coins(dice, parseAmount(tt->gold), individuals);
    t.platinum = coins(dice, parseAmount(tt->platinum), individuals);

    // Gems / Jewelry: "A-B:P%" or "A-B:P% / C-D:Q%" (gems / jewelry).
    {
        std::string cell = tt->gemsJewelry;
        std::string gemsPart = cell, jewelryPart;
        std::size_t slash = cell.find('/');
        if (slash != std::string::npos) {
            gemsPart = cell.substr(0, slash);
            jewelryPart = cell.substr(slash + 1);
        }
        CellAmount g = parseAmount(gemsPart);
        if (g.present && dice.percent(g.pct)) {
            int n = dice.rollRange(g.min, g.max);
            for (int i = 0; i < n; ++i) t.gems.push_back(gemValue(dice));
        }
        if (!jewelryPart.empty()) {
            CellAmount j = parseAmount(jewelryPart);
            if (j.present && dice.percent(j.pct)) {
                int n = dice.rollRange(j.min, j.max);
                for (int i = 0; i < n; ++i) t.jewelry.push_back(dice.roll(3, 6) * 100);
            }
        }
    }

    // Maps / Magic: "P%: any N ..." or "P%: 2-8 potions" / "P%: 1-4 scrolls".
    {
        std::string cell = tt->mapsMagic;
        std::size_t pc = cell.find('%');
        if (pc != std::string::npos) {
            int pct = 0;
            try { pct = std::stoi(cell); } catch (...) {}
            if (dice.percent(pct)) {
                std::size_t anyPos = cell.find("any");
                if (anyPos != std::string::npos) {
                    int n = 1;
                    try { n = std::stoi(cell.substr(anyPos + 3)); } catch (...) {}
                    for (int i = 0; i < n; ++i) {
                        if (dice.d100() >= 76) ++t.maps;
                        else t.magicItems.push_back(randomMagicItem(const_cast<Repository&>(repo)));
                    }
                } else if (cell.find("potions") != std::string::npos) {
                    int lo, hi;
                    if (leadingRange(cell.substr(pc + 2), lo, hi))
                        for (int i = 0, n = dice.rollRange(lo, hi); i < n; ++i)
                            t.magicItems.push_back(randomMagicItem(const_cast<Repository&>(repo), "Potion"));
                } else if (cell.find("scrolls") != std::string::npos) {
                    int lo, hi;
                    if (leadingRange(cell.substr(pc + 2), lo, hi))
                        for (int i = 0, n = dice.rollRange(lo, hi); i < n; ++i)
                            t.magicItems.push_back(randomMagicItem(const_cast<Repository&>(repo), "Scroll"));
                }
            }
        }
    }
    return t;
}

} // namespace gns
