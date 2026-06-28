#include "gns/CombatEngine.h"
#include "gns/Repository.h"   // Repository
#include "gns/Dice.h"
#include "gns/Rules.h"        // AttackResult, apPerSurvivor
#include "gns/Narrator.h"     // factFor

#include <cstddef>

namespace gns {

CombatEngine::CombatEngine(const Repository& repo, Dice& dice)
    : repo_(repo), dice_(dice), adj_(repo, dice) {}

int CombatEngine::encounterAp(const Encounter& enc) const {
    int total = 0;
    for (const auto& c : enc.monsters) total += c.apValue;
    return total;
}

CombatResult CombatEngine::run(Party& party, Encounter& enc, int maxRounds) {
    CombatResult res;

    // Nothing to fight -> immediate victory (no AP, no rounds).
    if (!enc.occurred || enc.monsters.empty()) {
        res.outcome = CombatOutcome::PartyVictory;
        return res;
    }

    auto monstersAlive = [&] {
        for (const auto& m : enc.monsters) if (m.life > 0) return true;
        return false;
    };
    auto firstLiveMonster = [&]() -> int {
        for (std::size_t i = 0; i < enc.monsters.size(); ++i)
            if (enc.monsters[i].life > 0) return static_cast<int>(i);
        return -1;
    };
    auto firstLivePc = [&]() -> int {
        for (std::size_t i = 0; i < party.members.size(); ++i)
            if (party.members[i].life > 0) return static_cast<int>(i);
        return -1;
    };

    // Each living PC strikes the first living monster.
    auto partyActs = [&] {
        for (auto& pc : party.members) {
            if (pc.life <= 0) continue;
            int ti = firstLiveMonster();
            if (ti < 0) return;
            Combatant& target = enc.monsters[ti];
            AttackResult ar = adj_.characterAttack(pc, target.defense);
            if (ar.hit) target.life -= ar.damage;
            res.log.push_back(factFor(ar, pc.name, target.name));
        }
    };
    // Each living monster strikes the first living PC.
    auto monstersAct = [&] {
        for (auto& mon : enc.monsters) {
            if (mon.life <= 0) continue;
            int ti = firstLivePc();
            if (ti < 0) return;
            Character& target = party.members[ti];
            AttackResult ar = adj_.monsterAttack(mon.attackBonus, target.defense, mon.damage);
            if (ar.hit) target.life -= ar.damage;
            res.log.push_back(factFor(ar, mon.name, target.name));
        }
    };

    while (res.rounds < maxRounds && monstersAlive() && !party.isWiped()) {
        ++res.rounds;
        // Side initiative: higher d6 acts first; ties go to the party.
        int partyInit = dice_.d6();
        int monsterInit = dice_.d6();
        if (partyInit >= monsterInit) {
            partyActs();
            if (monstersAlive()) monstersAct();
        } else {
            monstersAct();
            if (!party.isWiped()) partyActs();
        }
    }

    if (!monstersAlive()) {
        res.outcome = CombatOutcome::PartyVictory;
        res.apAwarded = encounterAp(enc);
        int survivors = 0;
        for (const auto& pc : party.members) if (pc.life > 0) ++survivors;
        if (survivors > 0) {
            res.apPerSurvivor = apPerSurvivor(res.apAwarded, survivors);
            for (auto& pc : party.members)
                if (pc.life > 0) pc.ap += res.apPerSurvivor;
        }
    } else {
        res.outcome = CombatOutcome::PartyDefeat;
    }
    return res;
}

} // namespace gns
