#pragma once
#include <optional>
#include <string>
#include <vector>

namespace gns {

struct Ability {
    int id = 0;
    std::string name;
    std::string abbreviation;
    int sortOrder = 0;
};

struct AbilityAdjustment {
    std::string appliesTo;   // "Prime Requisite" | "Constitution" | "Dexterity"
    int scoreLow = 0, scoreHigh = 0;
    int modifier = 0;
    std::string effect;
};

struct Race {
    int id = 0;
    std::string name;
    std::string restrictions;
    std::string specialAbility;
    double carryModifier = 1.0;
    int baseMovementFt = 120;
};

struct CharacterClass {
    int id = 0;
    std::string name;
    std::optional<int> parentId;             // sub-class -> base class
    std::optional<int> primeRequisiteAbilityId;
    std::string restrictions;
    std::string specialAbilities;
};

struct ClassLevel {
    int classId = 0;
    int level = 0;
    std::string title;
    int experiencePoints = 0;
    std::string hitDice;   // e.g. "1d8"
    std::string spells;    // e.g. "1 first level" / "None"
};

struct SavingThrow {
    std::string classGroup;
    int vsSpellStaff = 0, vsMagicWand = 0, vsDeathPoison = 0, vsStone = 0, vsDragonBreath = 0;
};

struct StrengthCapacity {
    int score = 0;
    int baseLbs = 0;
    int maxLbs = 0;
};

struct WeaponDef {
    int id = 0;
    std::string name;
    std::string category;
    int costGp = 0;
    int weightLbs = 0;
    std::string damage;       // "1d8 (1-8)"
    int damageMin = 0, damageMax = 0;
    bool twoHanded = false;
    bool clericUsable = false;
    std::optional<int> rangeShort, rangeMedium, rangeLong;
};

struct ArmorDef {
    int id = 0;
    std::string name;
    std::optional<int> armorClass;   // AC when worn (none for shields)
    std::optional<int> acModifier;   // e.g. -1 for shield
    int costGp = 0;
    int weightLbs = 0;
    bool isShield = false;
};

struct MonsterDef {
    int id = 0;
    std::string name;
    std::string hitDiceText;
    std::optional<int> hitDiceNum;
    std::string armorClassText;
    std::optional<int> armorClassNum;
    std::string alignment;
    std::string attacks;
    std::string damage;
    std::string treasureCode;   // "" if none
    bool isGroup = false;
    std::optional<int> parentId;
};

struct MonsterXpRow {
    std::string hitDice;   // "1+1", "Under 1", ...
    int value = 0;
    int specialBonus = 0;
};

struct WanderingEntry {
    int environmentId = 0;
    std::string environment;
    int monsterId = 0;
    std::string monster;
    int levelMin = 1, levelMax = 3;
    int numberMin = 1, numberMax = 1;
    int weight = 1;
};

struct TurnUndeadEntry {
    int clericLevel = 0;
    std::string undeadType;
    std::string result;   // number, "T", or "no effect"
};

struct TreasureTypeRow {
    std::string code;
    std::string copper, silver, electrum, gold, platinum, gemsJewelry, mapsMagic;
};

} // namespace gns
