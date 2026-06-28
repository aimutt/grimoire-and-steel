#include "gns/Repository.h"
#include "gns/Database.h"
#include <optional>

namespace gns {

static std::optional<int> optInt(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try { return std::stoi(s); } catch (...) { return std::nullopt; }
}
static int toInt(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

Repository::Repository(Database& db) : db_(db) { load(); }

void Repository::load() {
    for (auto& r : db_.query(
            "SELECT trait_id, name, COALESCE(description,''), sort_order "
            "FROM trait ORDER BY sort_order")) {
        Trait t{toInt(r[0]), r[1], r[2], toInt(r[3])};
        traitByName_[t.name] = (int)traits_.size();
        traits_.push_back(std::move(t));
    }
    for (auto& r : db_.query(
            "SELECT kin_id, name, COALESCE(description,''), COALESCE(gift_name,''), "
            "COALESCE(gift_description,''), sort_order FROM kin ORDER BY sort_order")) {
        Kin k{toInt(r[0]), r[1], r[2], r[3], r[4], toInt(r[5])};
        kinByName_[k.name] = (int)kins_.size();
        kins_.push_back(std::move(k));
    }
    for (auto& r : db_.query(
            "SELECT training_id, name, COALESCE(description,''), sort_order "
            "FROM training ORDER BY sort_order")) {
        Training t{toInt(r[0]), r[1], r[2], toInt(r[3])};
        trainingByName_[t.name] = (int)trainings_.size();
        trainings_.push_back(std::move(t));
    }
    for (auto& r : db_.query(
            "SELECT calling_id, name, COALESCE(description,''), COALESCE(starting_gear,''), "
            "COALESCE(armor_allowed,''), COALESCE(gift_name,''), COALESCE(gift_description,''), "
            "sort_order FROM calling ORDER BY sort_order")) {
        Calling c;
        c.id = toInt(r[0]); c.name = r[1]; c.description = r[2]; c.startingGear = r[3];
        c.armorAllowed = r[4]; c.giftName = r[5]; c.giftDescription = r[6];
        c.sortOrder = toInt(r[7]);
        callingByName_[c.name] = (int)callings_.size();
        callingById_[c.id] = (int)callings_.size();
        callings_.push_back(std::move(c));
    }
    // Per-calling training options.
    for (auto& r : db_.query(
            "SELECT cto.calling_id, t.name FROM calling_training_option cto "
            "JOIN training t ON t.training_id = cto.training_id "
            "ORDER BY cto.calling_id, t.sort_order")) {
        auto it = callingById_.find(toInt(r[0]));
        if (it != callingById_.end()) callings_[it->second].trainingOptions.push_back(r[1]);
    }
    // Per-calling weapon options.
    for (auto& r : db_.query(
            "SELECT calling_id, weapon_name FROM calling_weapon_option "
            "ORDER BY calling_id, weapon_name")) {
        auto it = callingById_.find(toInt(r[0]));
        if (it != callingById_.end()) callings_[it->second].weaponOptions.push_back(r[1]);
    }
    for (auto& r : db_.query(
            "SELECT weapon_category_id, name, COALESCE(damage_die,''), COALESCE(examples,''), "
            "sort_order FROM weapon_category ORDER BY sort_order")) {
        WeaponCategory w{toInt(r[0]), r[1], r[2], r[3], toInt(r[4])};
        weaponCatByName_[w.name] = (int)weaponCats_.size();
        weaponCats_.push_back(std::move(w));
    }
    for (auto& r : db_.query(
            "SELECT armor_id, name, defense_bonus, COALESCE(notes,''), sort_order "
            "FROM armor ORDER BY sort_order")) {
        Armor a{toInt(r[0]), r[1], toInt(r[2]), r[3], toInt(r[4])};
        armorByName_[a.name] = (int)armors_.size();
        armors_.push_back(std::move(a));
    }
    for (auto& r : db_.query(
            "SELECT name, target_number, COALESCE(description,''), sort_order "
            "FROM challenge_number ORDER BY sort_order")) {
        ChallengeNumber c{r[0], toInt(r[1]), r[2], toInt(r[3])};
        challengeByName_[c.name] = (int)challenges_.size();
        challenges_.push_back(std::move(c));
    }
    for (auto& r : db_.query(
            "SELECT monster_id, name, COALESCE(description,''), life, defense, attack_bonus, "
            "COALESCE(damage,''), ap_value, COALESCE(special_rule,'') "
            "FROM monster ORDER BY sort_order")) {
        MonsterDef m;
        m.id = toInt(r[0]); m.name = r[1]; m.description = r[2]; m.life = toInt(r[3]);
        m.defense = toInt(r[4]); m.attackBonus = toInt(r[5]); m.damage = r[6];
        m.apValue = toInt(r[7]); m.specialRule = r[8];
        monsterByName_[m.name] = (int)monsters_.size();
        monsters_.push_back(std::move(m));
    }
    for (auto& r : db_.query(
            "SELECT spell_id, name, COALESCE(description,''), COALESCE(combat_effect,''), "
            "COALESCE(challenge_number,''), COALESCE(notes,'') FROM spell ORDER BY sort_order")) {
        SpellDef s;
        s.id = toInt(r[0]); s.name = r[1]; s.description = r[2]; s.combatEffect = r[3];
        s.challengeNumber = optInt(r[4]); s.notes = r[5];
        spellByName_[s.name] = (int)spells_.size();
        spells_.push_back(std::move(s));
    }
    for (auto& r : db_.query(
            "SELECT name, bonus, COALESCE(description,'') FROM magic_weapon_example "
            "ORDER BY magic_weapon_id")) {
        magicWeapons_.push_back({0, r[0], toInt(r[1]), r[2]});
    }
    for (auto& r : db_.query(
            "SELECT calling_id, level, ap_required FROM advancement_level "
            "ORDER BY calling_id, level")) {
        advancement_.push_back({toInt(r[0]), toInt(r[1]), toInt(r[2])});
    }
    for (auto& r : db_.query(
            "SELECT module_type, ap_award, COALESCE(description,''), sort_order "
            "FROM module_completion_award ORDER BY sort_order")) {
        moduleAwards_.push_back({r[0], toInt(r[1]), r[2], toInt(r[3])});
    }
    for (auto& r : db_.query(
            "SELECT description, sort_order FROM level_improvement_option ORDER BY sort_order")) {
        levelOptions_.push_back({r[0], toInt(r[1])});
    }
}

const Trait* Repository::trait(const std::string& name) const {
    auto it = traitByName_.find(name);
    return it == traitByName_.end() ? nullptr : &traits_[it->second];
}
const Kin* Repository::kin(const std::string& name) const {
    auto it = kinByName_.find(name);
    return it == kinByName_.end() ? nullptr : &kins_[it->second];
}
const Calling* Repository::calling(const std::string& name) const {
    auto it = callingByName_.find(name);
    return it == callingByName_.end() ? nullptr : &callings_[it->second];
}
const Calling* Repository::callingById(int id) const {
    auto it = callingById_.find(id);
    return it == callingById_.end() ? nullptr : &callings_[it->second];
}
const Training* Repository::training(const std::string& name) const {
    auto it = trainingByName_.find(name);
    return it == trainingByName_.end() ? nullptr : &trainings_[it->second];
}
const WeaponCategory* Repository::weaponCategory(const std::string& name) const {
    auto it = weaponCatByName_.find(name);
    return it == weaponCatByName_.end() ? nullptr : &weaponCats_[it->second];
}
const Armor* Repository::armor(const std::string& name) const {
    auto it = armorByName_.find(name);
    return it == armorByName_.end() ? nullptr : &armors_[it->second];
}
const MonsterDef* Repository::monster(const std::string& name) const {
    auto it = monsterByName_.find(name);
    return it == monsterByName_.end() ? nullptr : &monsters_[it->second];
}
const SpellDef* Repository::spell(const std::string& name) const {
    auto it = spellByName_.find(name);
    return it == spellByName_.end() ? nullptr : &spells_[it->second];
}

int Repository::challenge(const std::string& name) const {
    auto it = challengeByName_.find(name);
    return it == challengeByName_.end() ? 0 : challenges_[it->second].target;
}

int Repository::apRequired(int callingId, int level) const {
    if (level <= 1) return 0;
    for (auto& a : advancement_)
        if (a.callingId == callingId && a.level == level) return a.apRequired;
    return -1;
}

int Repository::levelForAp(int callingId, int ap) const {
    int best = 1;
    for (auto& a : advancement_)
        if (a.callingId == callingId && ap >= a.apRequired && a.level > best)
            best = a.level;
    return best;
}

} // namespace gns
