#pragma once
#include "gns/Rules.h"    // AttackResult / CastResult / CheckResult
#include "gns/Module.h"   // gns::Area

#include <string>

// Referee / Arbiter: the session-facing façade over the rules engine (milestone M4).
//
// RulesAdjudicator binds a read-only Repository (engine-owned, loaded from gns.db)
// and the session's seeded Dice, then exposes methods that drop those two arguments
// and route to the functions in Rules.h. It reimplements no rules -- it is pure
// wiring plus small result structs so outcomes are narratable. Keeping every roll on
// the one seeded Dice stream is what makes a .gnssav replay deterministic.

namespace gns {

class Repository;
class Dice;

class RulesAdjudicator {
public:
    RulesAdjudicator(const Repository& repo, Dice& dice);

    // ---- Combat ----
    // A character's melee (default) or ranged attack against a target Defense.
    AttackResult characterAttack(const Character& c, int targetDefense, bool ranged = false);
    // A monster's attack: its attack bonus + damage expression vs a target Defense.
    AttackResult monsterAttack(int attackBonus, int targetDefense, const std::string& damage);

    // ---- Spellcasting ----
    CastResult castSpell(const Character& c, int challenge);

    // ---- Generic authored-chance roll (EncounterDirector reuses this) ----
    bool check(int chancePct);                  // thin Dice::percent wrapper

    // ---- Generic skill/ability check: 1d20 + bonus vs a challenge ----
    CheckResult resolve(int bonus, int challenge);

    // ---- Area authored-chance checks: did it happen + the text to narrate ----
    struct CheckOutcome { bool occurred = false; std::string description; };
    CheckOutcome trapCheck(const Area& a);      // party falls victim -> trapDescription
    CheckOutcome lockCheck(const Area& a);      // character opens/picks -> lockDescription
    CheckOutcome hiddenCheck(const Area& a);    // party discovers -> hiddenDescription

private:
    const Repository& repo_;
    Dice& dice_;
};

} // namespace gns
