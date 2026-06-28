#pragma once
#include <string>
#include <vector>

namespace gns {

class Database;

// A monster summary used by the engine's reference browser.
struct MonsterBrief {
    std::string name;
    int life = 0;
    int defense = 0;
    int attackBonus = 0;
    std::string damage;
};

// A spell summary used by the engine's reference browser.
struct SpellBrief {
    std::string name;
    int challengeNumber = 0;   // 0 if the spell has no casting challenge
    std::string combatEffect;  // empty for non-combat spells
};

std::vector<MonsterBrief> topMonsters(Database& db, int limit);
std::vector<SpellBrief>   allSpells(Database& db);
int countRows(Database& db, const std::string& table);

} // namespace gns
