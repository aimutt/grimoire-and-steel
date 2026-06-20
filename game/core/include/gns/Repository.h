#pragma once
#include "gns/Model.h"
#include <map>
#include <string>
#include <vector>

namespace gns {

class Database;

// Loads reference data from gns.db once and provides typed lookups for the
// rules engine. Read-only.
class Repository {
public:
    explicit Repository(Database& db);

    // --- reference collections ---
    const std::vector<Ability>& abilities() const { return abilities_; }
    const std::vector<Race>& races() const { return races_; }
    const std::vector<CharacterClass>& classes() const { return classes_; }
    const std::vector<MonsterDef>& monsters() const { return monsters_; }

    // --- lookups ---
    const Ability* ability(const std::string& name) const;
    const Ability* abilityById(int id) const;
    const Race* race(const std::string& name) const;
    const CharacterClass* charClass(const std::string& name) const;
    // class progression at a level; resolves sub-class to its base class.
    const ClassLevel* classLevel(int classId, int level) const;

    // Earned-XP %, CON per-hit-die, or DEX missile modifier for a score.
    int abilityAdjustment(const std::string& appliesTo, int score) const;

    const SavingThrow* savingThrow(const std::string& classGroup) const;
    const StrengthCapacity* strengthCapacity(int score) const;

    const WeaponDef* weapon(const std::string& name) const;
    const ArmorDef* armor(const std::string& name) const;

    const MonsterDef* monster(const std::string& name) const;
    // XP for a monster given its hit-dice label and whether it has special abilities.
    int monsterXpValue(const std::string& hitDiceLabel, bool special) const;

    // Roll needed for attacker to hit target AC. attackerKind: "character"|"monster".
    int attackRollNeeded(const std::string& attackerKind,
                         const std::string& attackerLabel, int targetAc) const;

    // Wandering entries valid for an environment + party level.
    std::vector<const WanderingEntry*> wandering(const std::string& env, int partyLevel) const;

    // Turn-undead result string ("7", "T", "no effect", or "" if unknown).
    std::string turnUndead(int clericLevel, const std::string& undeadType) const;

    const TreasureTypeRow* treasureType(const std::string& code) const;

    Database& db() { return db_; }

private:
    Database& db_;
    std::vector<Ability> abilities_;
    std::vector<AbilityAdjustment> abilityAdj_;
    std::vector<Race> races_;
    std::vector<CharacterClass> classes_;
    std::vector<ClassLevel> classLevels_;
    std::vector<SavingThrow> saves_;
    std::vector<StrengthCapacity> strCap_;
    std::vector<WeaponDef> weapons_;
    std::vector<ArmorDef> armors_;
    std::vector<MonsterDef> monsters_;
    std::vector<MonsterXpRow> monsterXp_;
    std::vector<WanderingEntry> wandering_;
    std::vector<TurnUndeadEntry> turnUndead_;
    std::vector<TreasureTypeRow> treasure_;
    std::map<std::string, int> attack_;   // "kind|label|ac" -> roll_needed

    std::map<std::string, int> abilityByName_;
    std::map<int, int> abilityById_;
    std::map<std::string, int> raceByName_;
    std::map<std::string, int> classByName_;
    std::map<std::string, int> weaponByName_;
    std::map<std::string, int> armorByName_;
    std::map<std::string, int> monsterByName_;

    void load();
};

} // namespace gns
