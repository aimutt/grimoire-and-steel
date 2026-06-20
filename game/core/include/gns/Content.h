#pragma once
#include <string>
#include <vector>

namespace gns {

class Database;

// A monster summary used by the M0 vertical slice.
struct MonsterBrief {
    std::string name;
    std::string hitDice;
    std::string armorClass;
    std::string alignment;
};

// A spell summary used by the M0 vertical slice.
struct SpellBrief {
    std::string name;
    int level = 0;
    std::string spellClass;   // "magic-user" | "cleric"
    std::string range;
};

std::vector<MonsterBrief> topMonsters(Database& db, int limit);
std::vector<SpellBrief>   allSpells(Database& db);
int countRows(Database& db, const std::string& table);

} // namespace gns
