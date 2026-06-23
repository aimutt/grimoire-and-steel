#pragma once
#include "gns/Rules.h"    // AttackResult/WanderingResult/TreasureResult/TurnResult, SaveCategory,
                          // Character (via Character.h), MonsterDef (forward-declared)
#include "gns/Module.h"   // gns::Area

#include <string>

// Referee / Arbiter: the session-facing façade over the M1 rules engine (milestone M4).
//
// RulesAdjudicator binds a read-only Repository (engine-owned, loaded from gns.db)
// and the session's seeded Dice, then exposes methods that drop those two arguments
// and route straight to the existing functions in Rules.h. It reimplements no rules
// -- it is pure wiring plus two small convenience result structs (SaveOutcome,
// CheckOutcome) so outcomes are narratable. Keeping every roll on the one seeded
// Dice stream is what makes a .gnssav replay deterministic.
//
// It deliberately does NOT live inside Session: Session stays clean, serializable
// play-state with no Repository/Database dependency. The engine constructs one per
// session: `RulesAdjudicator adj(repo, session.dice());`.

namespace gns {

class Repository;
class Dice;

class RulesAdjudicator {
public:
    RulesAdjudicator(const Repository& repo, Dice& dice);

    // ---- Combat (forward to the rich-result functions in Rules.h) ----
    AttackResult characterAttack(int level, int targetAc, const std::string& weapon);
    AttackResult monsterAttack(int hitDice, int targetAc, const std::string& damageExpr);

    // ---- Saving throw: combine saveNeeded + a d20 so the roll is narratable ----
    struct SaveOutcome { int needed = 0; int roll = 0; bool success = false; };
    SaveOutcome savingThrow(const Character& c, SaveCategory cat);

    // ---- Generic authored-chance roll (EncounterDirector reuses this) ----
    bool check(int chancePct);                  // thin Dice::percent wrapper

    // ---- Area authored-chance checks: did it happen + the text to narrate ----
    struct CheckOutcome { bool occurred = false; std::string description; };
    CheckOutcome trapCheck(const Area& a);      // party falls victim -> trapDescription
    CheckOutcome lockCheck(const Area& a);      // character opens/picks -> lockDescription
    CheckOutcome hiddenCheck(const Area& a);    // party discovers -> hiddenDescription

    // ---- Encounter / reward forwards ----
    WanderingResult wandering(const std::string& environment, int partyLevel);
    TreasureResult  treasure(const std::string& code, int individuals = 0);
    TurnResult      turnUndead(int clericLevel, const std::string& undeadType);
    int             monsterXp(const MonsterDef& m);

private:
    const Repository& repo_;
    Dice& dice_;
};

} // namespace gns
