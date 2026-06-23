#pragma once
#include "gns/EncounterDirector.h"   // Encounter, Combatant
#include "gns/Session.h"             // Party (and Character)
#include "gns/RulesAdjudicator.h"    // RulesAdjudicator (owned member)

#include <string>
#include <vector>

// Combat loop (milestone M4): an auto-resolving turn-based fight that ties the
// Actor (EncounterDirector -> Encounter/Combatant) to the Referee
// (RulesAdjudicator). It reimplements no rules -- it orchestrates initiative,
// per-combatant HP tracking, attack resolution, end-of-fight detection, and the
// XP award to survivors. Deterministic given the session's seeded Dice (so a
// .gnssav replays identically). Interactive per-turn target/action choice is a
// later UI-layer addition that will reuse the same RulesAdjudicator calls.

namespace gns {

class Repository;
class Dice;

enum class CombatOutcome { PartyVictory, PartyDefeat };

struct CombatResult {
    CombatOutcome outcome = CombatOutcome::PartyDefeat;
    int rounds = 0;
    int xpAwarded = 0;       // total monster XP from the encounter
    int xpPerSurvivor = 0;   // base share before each survivor's prime-req bonus
    std::vector<std::string> log;   // narratable lines (built via Narrator factFor)
};

// Apply the prime-requisite earned-XP bonus to an XP amount.
int applyXpBonus(int amount, int xpBonusPct);

class CombatEngine {
public:
    CombatEngine(const Repository& repo, Dice& dice);

    // Auto-resolve the party vs the encounter's monsters. Mutates party hp + xp
    // and the encounter combatants' hp; awards XP to survivors on victory.
    CombatResult run(Party& party, Encounter& enc, int maxRounds = 100);

    // Sum of monster XP for the encounter (no RNG).
    int encounterXp(const Encounter& enc) const;

private:
    const Repository& repo_;
    Dice& dice_;
    RulesAdjudicator adj_;
};

} // namespace gns
