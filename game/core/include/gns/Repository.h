#pragma once
#include "gns/Model.h"
#include <map>
#include <string>
#include <vector>

namespace gns {

class Database;

// Loads Grimoire & Steel reference data from gns.db once and provides typed
// lookups for the rules engine. Read-only.
class Repository {
public:
    explicit Repository(Database& db);

    // --- reference collections ---
    const std::vector<Trait>& traits() const { return traits_; }
    const std::vector<Kin>& kins() const { return kins_; }
    const std::vector<Calling>& callings() const { return callings_; }
    const std::vector<Training>& trainings() const { return trainings_; }
    const std::vector<WeaponCategory>& weaponCategories() const { return weaponCats_; }
    const std::vector<Armor>& armors() const { return armors_; }
    const std::vector<ChallengeNumber>& challenges() const { return challenges_; }
    const std::vector<MonsterDef>& monsters() const { return monsters_; }
    const std::vector<SpellDef>& spells() const { return spells_; }

    // --- lookups (nullptr / 0 / "" when not found) ---
    const Trait* trait(const std::string& name) const;
    const Kin* kin(const std::string& name) const;
    const Calling* calling(const std::string& name) const;
    const Calling* callingById(int id) const;
    const Training* training(const std::string& name) const;
    const WeaponCategory* weaponCategory(const std::string& name) const;
    const Armor* armor(const std::string& name) const;
    const MonsterDef* monster(const std::string& name) const;
    const SpellDef* spell(const std::string& name) const;

    // Target number for a named challenge band ("Normal" -> 12); 0 if unknown.
    int challenge(const std::string& name) const;

    // Cumulative AP required to reach `level` for a calling; 0 for level<=1, -1 if
    // the (calling, level) pair is unknown.
    int apRequired(int callingId, int level) const;
    // Highest level a calling has reached for a given cumulative AP total.
    int levelForAp(int callingId, int ap) const;

    Database& db() { return db_; }

private:
    Database& db_;
    std::vector<Trait> traits_;
    std::vector<Kin> kins_;
    std::vector<Calling> callings_;
    std::vector<Training> trainings_;
    std::vector<WeaponCategory> weaponCats_;
    std::vector<Armor> armors_;
    std::vector<ChallengeNumber> challenges_;
    std::vector<MonsterDef> monsters_;
    std::vector<SpellDef> spells_;
    std::vector<MagicWeaponExample> magicWeapons_;
    std::vector<AdvancementLevel> advancement_;
    std::vector<ModuleCompletionAward> moduleAwards_;
    std::vector<LevelImprovementOption> levelOptions_;

    std::map<std::string, int> traitByName_;
    std::map<std::string, int> kinByName_;
    std::map<std::string, int> callingByName_;
    std::map<int, int> callingById_;
    std::map<std::string, int> trainingByName_;
    std::map<std::string, int> weaponCatByName_;
    std::map<std::string, int> armorByName_;
    std::map<std::string, int> challengeByName_;
    std::map<std::string, int> monsterByName_;
    std::map<std::string, int> spellByName_;

    void load();
};

} // namespace gns
