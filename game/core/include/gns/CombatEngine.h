#pragma once
#include "gns/EncounterDirector.h"   // Encounter, Combatant
#include "gns/Session.h"             // Party (and Character)
#include "gns/RulesAdjudicator.h"    // RulesAdjudicator (owned member)

#include <string>
#include <vector>

// Combat loop (milestone M4): an auto-resolving turn-based fight that ties the
// Actor (EncounterDirector -> Encounter/Combatant) to the Referee
// (RulesAdjudicator). It reimplements no rules -- it orchestrates initiative,
// per-combatant Life tracking, attack resolution, end-of-fight detection, and the
// AP award to survivors. Deterministic given the session's seeded Dice (so a
// .gnssav replays identically). Interactive per-turn target/action choice is a
// later UI-layer addition that will reuse the same RulesAdjudicator calls.

namespace gns {

class Repository;
class Dice;

enum class CombatOutcome { PartyVictory, PartyDefeat };

struct CombatResult {
    CombatOutcome outcome = CombatOutcome::PartyDefeat;
    int rounds = 0;
    int apAwarded = 0;       // total monster AP from the encounter
    int apPerSurvivor = 0;   // AP each surviving participant receives
    std::vector<std::string> log;   // narratable lines (built via Narrator factFor)
};

class CombatEngine {
public:
    CombatEngine(const Repository& repo, Dice& dice);

    // Auto-resolve the party vs the encounter's monsters. Mutates party life + ap
    // and the encounter combatants' life; awards AP to survivors on victory.
    CombatResult run(Party& party, Encounter& enc, int maxRounds = 100);

    // Sum of monster AP for the encounter (no RNG).
    int encounterAp(const Encounter& enc) const;

private:
    const Repository& repo_;
    Dice& dice_;
    RulesAdjudicator adj_;
};

} // namespace gns
