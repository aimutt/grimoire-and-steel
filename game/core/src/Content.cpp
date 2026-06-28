#include "gns/Content.h"
#include "gns/Database.h"

namespace gns {

std::vector<MonsterBrief> topMonsters(Database& db, int limit) {
    auto rows = db.query(
        "SELECT name, life, defense, attack_bonus, COALESCE(damage,'') "
        "FROM monster ORDER BY sort_order LIMIT " + std::to_string(limit));
    std::vector<MonsterBrief> out;
    out.reserve(rows.size());
    for (auto& r : rows) {
        MonsterBrief m;
        m.name = r[0];
        m.life = r[1].empty() ? 0 : std::stoi(r[1]);
        m.defense = r[2].empty() ? 0 : std::stoi(r[2]);
        m.attackBonus = r[3].empty() ? 0 : std::stoi(r[3]);
        m.damage = r[4];
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<SpellBrief> allSpells(Database& db) {
    auto rows = db.query(
        "SELECT name, COALESCE(challenge_number,''), COALESCE(combat_effect,'') "
        "FROM spell ORDER BY sort_order");
    std::vector<SpellBrief> out;
    out.reserve(rows.size());
    for (auto& r : rows) {
        SpellBrief s;
        s.name = r[0];
        s.challengeNumber = r[1].empty() ? 0 : std::stoi(r[1]);
        s.combatEffect = r[2];
        out.push_back(std::move(s));
    }
    return out;
}

int countRows(Database& db, const std::string& table) {
    return static_cast<int>(db.scalarInt("SELECT COUNT(*) FROM " + table));
}

} // namespace gns
