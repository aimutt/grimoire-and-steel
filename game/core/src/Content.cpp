#include "gns/Content.h"
#include "gns/Database.h"

namespace gns {

std::vector<MonsterBrief> topMonsters(Database& db, int limit) {
    auto rows = db.query(
        "SELECT name, COALESCE(hit_dice_text,''), COALESCE(armor_class_text,''), "
        "COALESCE(alignment_text,'') FROM monsters WHERE parent_id IS NULL "
        "ORDER BY name LIMIT " + std::to_string(limit));
    std::vector<MonsterBrief> out;
    out.reserve(rows.size());
    for (auto& r : rows)
        out.push_back({r[0], r[1], r[2], r[3]});
    return out;
}

std::vector<SpellBrief> allSpells(Database& db) {
    auto rows = db.query(
        "SELECT name, spell_level, spell_class, COALESCE(range_text,'') "
        "FROM all_spells ORDER BY spell_class, spell_level, name");
    std::vector<SpellBrief> out;
    out.reserve(rows.size());
    for (auto& r : rows) {
        SpellBrief s;
        s.name = r[0];
        s.level = r[1].empty() ? 0 : std::stoi(r[1]);
        s.spellClass = r[2];
        s.range = r[3];
        out.push_back(std::move(s));
    }
    return out;
}

int countRows(Database& db, const std::string& table) {
    return static_cast<int>(db.scalarInt("SELECT COUNT(*) FROM " + table));
}

} // namespace gns
