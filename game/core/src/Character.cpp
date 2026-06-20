#include "gns/Character.h"
#include "gns/Repository.h"
#include "gns/Dice.h"
#include <algorithm>

namespace gns {

int AbilityScores::get(Abil a) const {
    switch (a) {
        case Abil::STR: return str;
        case Abil::INT: return intel;
        case Abil::WIS: return wis;
        case Abil::DEX: return dex;
        case Abil::CON: return con;
        case Abil::CHA: return cha;
    }
    return 10;
}
void AbilityScores::set(Abil a, int v) {
    switch (a) {
        case Abil::STR: str = v; break;
        case Abil::INT: intel = v; break;
        case Abil::WIS: wis = v; break;
        case Abil::DEX: dex = v; break;
        case Abil::CON: con = v; break;
        case Abil::CHA: cha = v; break;
    }
}

AbilityScores rollAbilities(Dice& dice) {
    AbilityScores s;
    s.str = dice.roll(3, 6);
    s.intel = dice.roll(3, 6);
    s.wis = dice.roll(3, 6);
    s.dex = dice.roll(3, 6);
    s.con = dice.roll(3, 6);
    s.cha = dice.roll(3, 6);
    return s;
}

int scoreForAbility(const AbilityScores& s, const std::string& name) {
    if (name == "Strength") return s.str;
    if (name == "Intelligence") return s.intel;
    if (name == "Wisdom") return s.wis;
    if (name == "Dexterity") return s.dex;
    if (name == "Constitution") return s.con;
    if (name == "Charisma") return s.cha;
    return 0;
}

std::string classGroupFor(const Repository& repo, const std::string& className,
                          const std::string& raceName) {
    if (raceName == "Dwarf" || raceName == "Halfling")
        return "Dwarves & Halflings";
    // Resolve sub-class to its base class.
    std::string base = className;
    if (const CharacterClass* c = repo.charClass(className); c && c->parentId) {
        for (auto& cc : repo.classes())
            if (cc.id == *c->parentId) { base = cc.name; break; }
    }
    if (base == "Magic-User") return "Magic-user";
    if (base == "Cleric") return "Cleric";
    // Fighter and Thief both map to the fighting-man group.
    return "Fighting Man, Thief, Hobgoblin";
}

Character makeCharacter(const Repository& repo, Dice& dice, const std::string& name,
                        const std::string& raceName, const std::string& className,
                        const AbilityScores& scores, int level) {
    Character ch;
    ch.name = name;
    ch.race = raceName;
    ch.charClass = className;
    ch.level = level;
    ch.abilities = scores;

    const CharacterClass* cls = repo.charClass(className);
    const Race* race = repo.race(raceName);
    ch.baseMovementFt = race ? race->baseMovementFt : 120;

    // Prime-requisite earned-XP bonus.
    if (cls && cls->primeRequisiteAbilityId) {
        if (const Ability* pa = repo.abilityById(*cls->primeRequisiteAbilityId)) {
            int s = scoreForAbility(scores, pa->name);
            ch.xpBonusPct = repo.abilityAdjustment("Prime Requisite", s);
        }
    }

    // Hit points: roll the class hit die for each level, + CON per-die modifier.
    int conMod = repo.abilityAdjustment("Constitution", scores.con);
    int hp = 0;
    if (cls) {
        const ClassLevel* cl = repo.classLevel(cls->id, level);
        DiceExpr e;
        if (cl && parseDice(cl->hitDice, e) && !e.isRange) {
            // hitDice is the *total* dice at this level (e.g. "3d8").
            for (int i = 0; i < e.count; ++i)
                hp += dice.roll(1, e.sides) + conMod;
            ch.experiencePoints = cl->experiencePoints;
        }
    }
    ch.maxHp = std::max(1, hp);
    ch.hp = ch.maxHp;

    // Saving throws.
    ch.classGroup = classGroupFor(repo, className, raceName);
    if (const SavingThrow* st = repo.savingThrow(ch.classGroup)) {
        ch.saveSpellStaff = st->vsSpellStaff;
        ch.saveMagicWand = st->vsMagicWand;
        ch.saveDeathPoison = st->vsDeathPoison;
        ch.saveStone = st->vsStone;
        ch.saveDragonBreath = st->vsDragonBreath;
    }

    ch.armorClass = 9;   // unarmored; equipment sets this later
    return ch;
}

} // namespace gns
