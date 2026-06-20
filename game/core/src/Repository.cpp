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
static double toDouble(const std::string& s, double def = 0.0) {
    if (s.empty()) return def;
    try { return std::stod(s); } catch (...) { return def; }
}

Repository::Repository(Database& db) : db_(db) { load(); }

void Repository::load() {
    for (auto& r : db_.query(
            "SELECT id, name, COALESCE(abbreviation,''), COALESCE(sort_order,0) "
            "FROM ability ORDER BY sort_order")) {
        Ability a{toInt(r[0]), r[1], r[2], toInt(r[3])};
        abilityByName_[a.name] = (int)abilities_.size();
        abilityById_[a.id] = (int)abilities_.size();
        abilities_.push_back(std::move(a));
    }
    for (auto& r : db_.query(
            "SELECT applies_to, score_low, score_high, modifier, COALESCE(effect,'') "
            "FROM ability_adjustment")) {
        abilityAdj_.push_back({r[0], toInt(r[1]), toInt(r[2]), toInt(r[3]), r[4]});
    }
    for (auto& r : db_.query(
            "SELECT id, name, COALESCE(restrictions,''), COALESCE(special_ability,''), "
            "COALESCE(carry_capacity_modifier,1.0), COALESCE(base_movement_ft,120) FROM race")) {
        Race rc;
        rc.id = toInt(r[0]); rc.name = r[1]; rc.restrictions = r[2];
        rc.specialAbility = r[3]; rc.carryModifier = toDouble(r[4], 1.0);
        rc.baseMovementFt = toInt(r[5], 120);
        raceByName_[rc.name] = (int)races_.size();
        races_.push_back(std::move(rc));
    }
    for (auto& r : db_.query(
            "SELECT id, name, COALESCE(parent_class_id,''), COALESCE(prime_requisite_ability_id,''), "
            "COALESCE(restrictions,''), COALESCE(special_abilities,'') FROM character_class")) {
        CharacterClass c;
        c.id = toInt(r[0]); c.name = r[1];
        c.parentId = optInt(r[2]); c.primeRequisiteAbilityId = optInt(r[3]);
        c.restrictions = r[4]; c.specialAbilities = r[5];
        classByName_[c.name] = (int)classes_.size();
        classes_.push_back(std::move(c));
    }
    for (auto& r : db_.query(
            "SELECT class_id, level, COALESCE(title,''), experience_points, "
            "COALESCE(hit_dice,''), COALESCE(spells,'') FROM class_level")) {
        classLevels_.push_back({toInt(r[0]), toInt(r[1]), r[2], toInt(r[3]), r[4], r[5]});
    }
    for (auto& r : db_.query(
            "SELECT class_group, vs_spell_staff, vs_magic_wand, vs_death_poison, vs_stone, "
            "vs_dragon_breath FROM saving_throw")) {
        saves_.push_back({r[0], toInt(r[1]), toInt(r[2]), toInt(r[3]), toInt(r[4]), toInt(r[5])});
    }
    for (auto& r : db_.query(
            "SELECT strength_score, base_capacity_lbs, max_capacity_lbs FROM strength_capacity")) {
        strCap_.push_back({toInt(r[0]), toInt(r[1]), toInt(r[2])});
    }
    for (auto& r : db_.query(
            "SELECT id, name, COALESCE(category,''), COALESCE(cost_gp,0), COALESCE(weight_lbs,0), "
            "COALESCE(damage,''), COALESCE(damage_min,0), COALESCE(damage_max,0), two_handed, "
            "cleric_usable, COALESCE(range_short_ft,''), COALESCE(range_medium_ft,''), "
            "COALESCE(range_long_ft,'') FROM weapon")) {
        WeaponDef w;
        w.id = toInt(r[0]); w.name = r[1]; w.category = r[2]; w.costGp = toInt(r[3]);
        w.weightLbs = toInt(r[4]); w.damage = r[5]; w.damageMin = toInt(r[6]);
        w.damageMax = toInt(r[7]); w.twoHanded = toInt(r[8]) != 0; w.clericUsable = toInt(r[9]) != 0;
        w.rangeShort = optInt(r[10]); w.rangeMedium = optInt(r[11]); w.rangeLong = optInt(r[12]);
        weaponByName_[w.name] = (int)weapons_.size();
        weapons_.push_back(std::move(w));
    }
    for (auto& r : db_.query(
            "SELECT id, name, COALESCE(armor_class,''), COALESCE(ac_modifier,''), "
            "COALESCE(cost_gp,0), COALESCE(weight_lbs,0), is_shield FROM armor")) {
        ArmorDef a;
        a.id = toInt(r[0]); a.name = r[1]; a.armorClass = optInt(r[2]); a.acModifier = optInt(r[3]);
        a.costGp = toInt(r[4]); a.weightLbs = toInt(r[5]); a.isShield = toInt(r[6]) != 0;
        armorByName_[a.name] = (int)armors_.size();
        armors_.push_back(std::move(a));
    }
    for (auto& r : db_.query(
            "SELECT id, name, COALESCE(hit_dice_text,''), COALESCE(hit_dice_num,''), "
            "COALESCE(armor_class_text,''), COALESCE(armor_class_num,''), COALESCE(alignment_text,''), "
            "COALESCE(attacks_text,''), COALESCE(damage_text,''), COALESCE(treasure_type_code,''), "
            "is_group, COALESCE(parent_id,'') FROM monsters")) {
        MonsterDef m;
        m.id = toInt(r[0]); m.name = r[1]; m.hitDiceText = r[2]; m.hitDiceNum = optInt(r[3]);
        m.armorClassText = r[4]; m.armorClassNum = optInt(r[5]); m.alignment = r[6];
        m.attacks = r[7]; m.damage = r[8]; m.treasureCode = r[9];
        m.isGroup = toInt(r[10]) != 0; m.parentId = optInt(r[11]);
        monsterByName_[m.name] = (int)monsters_.size();
        monsters_.push_back(std::move(m));
    }
    for (auto& r : db_.query(
            "SELECT hit_dice, value, COALESCE(special_ability_bonus,0) FROM monster_xp")) {
        monsterXp_.push_back({r[0], toInt(r[1]), toInt(r[2])});
    }
    for (auto& r : db_.query(
            "SELECT attacker_kind, attacker_label, target_ac, roll_needed FROM attack_table")) {
        attack_[r[0] + "|" + r[1] + "|" + r[2]] = toInt(r[3]);
    }
    for (auto& r : db_.query(
            "SELECT e.name, wm.environment_id, wm.monster_id, m.name, wm.party_level_min, "
            "wm.party_level_max, COALESCE(wm.number_min,1), COALESCE(wm.number_max,1), "
            "COALESCE(wm.weight,1) FROM wandering_monster wm "
            "JOIN environment e ON e.id=wm.environment_id "
            "JOIN monsters m ON m.id=wm.monster_id")) {
        WanderingEntry w;
        w.environment = r[0]; w.environmentId = toInt(r[1]); w.monsterId = toInt(r[2]);
        w.monster = r[3]; w.levelMin = toInt(r[4]); w.levelMax = toInt(r[5]);
        w.numberMin = toInt(r[6]); w.numberMax = toInt(r[7]); w.weight = toInt(r[8], 1);
        wandering_.push_back(std::move(w));
    }
    for (auto& r : db_.query(
            "SELECT cleric_level, undead_type, result FROM turn_undead")) {
        turnUndead_.push_back({toInt(r[0]), r[1], r[2]});
    }
    for (auto& r : db_.query(
            "SELECT code, COALESCE(copper,''), COALESCE(silver,''), COALESCE(electrum,''), "
            "COALESCE(gold,''), COALESCE(platinum,''), COALESCE(gems_jewelry,''), "
            "COALESCE(maps_magic,'') FROM treasure_type")) {
        treasure_.push_back({r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]});
    }
}

const Ability* Repository::ability(const std::string& name) const {
    auto it = abilityByName_.find(name);
    return it == abilityByName_.end() ? nullptr : &abilities_[it->second];
}
const Ability* Repository::abilityById(int id) const {
    auto it = abilityById_.find(id);
    return it == abilityById_.end() ? nullptr : &abilities_[it->second];
}
const Race* Repository::race(const std::string& name) const {
    auto it = raceByName_.find(name);
    return it == raceByName_.end() ? nullptr : &races_[it->second];
}
const CharacterClass* Repository::charClass(const std::string& name) const {
    auto it = classByName_.find(name);
    return it == classByName_.end() ? nullptr : &classes_[it->second];
}

const ClassLevel* Repository::classLevel(int classId, int level) const {
    // A sub-class inherits its base class's progression.
    int resolved = classId;
    for (auto& c : classes_)
        if (c.id == classId && c.parentId) { resolved = *c.parentId; break; }
    for (auto& cl : classLevels_)
        if (cl.classId == resolved && cl.level == level) return &cl;
    return nullptr;
}

int Repository::abilityAdjustment(const std::string& appliesTo, int score) const {
    for (auto& a : abilityAdj_)
        if (a.appliesTo == appliesTo && score >= a.scoreLow && score <= a.scoreHigh)
            return a.modifier;
    return 0;
}

const SavingThrow* Repository::savingThrow(const std::string& classGroup) const {
    for (auto& s : saves_) if (s.classGroup == classGroup) return &s;
    return nullptr;
}
const StrengthCapacity* Repository::strengthCapacity(int score) const {
    for (auto& s : strCap_) if (s.score == score) return &s;
    return nullptr;
}
const WeaponDef* Repository::weapon(const std::string& name) const {
    auto it = weaponByName_.find(name);
    return it == weaponByName_.end() ? nullptr : &weapons_[it->second];
}
const ArmorDef* Repository::armor(const std::string& name) const {
    auto it = armorByName_.find(name);
    return it == armorByName_.end() ? nullptr : &armors_[it->second];
}
const MonsterDef* Repository::monster(const std::string& name) const {
    auto it = monsterByName_.find(name);
    return it == monsterByName_.end() ? nullptr : &monsters_[it->second];
}

int Repository::monsterXpValue(const std::string& hitDiceLabel, bool special) const {
    for (auto& x : monsterXp_)
        if (x.hitDice == hitDiceLabel)
            return x.value + (special ? x.specialBonus : 0);
    return 0;
}

int Repository::attackRollNeeded(const std::string& kind, const std::string& label, int ac) const {
    auto it = attack_.find(kind + "|" + label + "|" + std::to_string(ac));
    return it == attack_.end() ? 99 : it->second;   // 99 = effectively impossible
}

std::vector<const WanderingEntry*> Repository::wandering(const std::string& env, int level) const {
    std::vector<const WanderingEntry*> out;
    for (auto& w : wandering_)
        if (w.environment == env && level >= w.levelMin && level <= w.levelMax)
            out.push_back(&w);
    return out;
}

std::string Repository::turnUndead(int level, const std::string& type) const {
    for (auto& t : turnUndead_)
        if (t.clericLevel == level && t.undeadType == type) return t.result;
    return "";
}

const TreasureTypeRow* Repository::treasureType(const std::string& code) const {
    for (auto& t : treasure_) if (t.code == code) return &t;
    return nullptr;
}

} // namespace gns
