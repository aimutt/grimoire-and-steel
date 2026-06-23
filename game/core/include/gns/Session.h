#pragma once
#include "gns/Module.h"
#include "gns/Character.h"
#include "gns/Dice.h"
#include "gns/PlotTracker.h"

#include <cstdint>
#include <string>
#include <vector>

// Live play-session runtime (milestone M4, first slice).
//
// A Session is the top-level object the Game Engine drives while *playing* a
// module. It is distinct from the authored Module (read-only adventure data) and
// from the gns.db rules database. A Session owns the loaded Module by value, the
// player's Party, the mutable PlayState (position/progress), and the single
// seeded Dice that will drive every adjudicated roll -- keeping rolls on one
// seeded stream is what lets a save replay deterministically (and tests stay
// reproducible). This slice covers construction + seating the party at the
// module's declared start; combat, plot tracking, fog of war, and .gnssav are
// later slices.

namespace gns {

// The party of player characters exploring a module.
struct Party {
    std::vector<Character> members;

    bool empty() const { return members.empty(); }
    // True only when there is at least one member and every member is at/below
    // 0 hp (a total-party kill). An empty party is "empty", not "wiped".
    bool isWiped() const;
    // Average member level, rounded down -- for level-keyed rules such as the
    // wandering-monster tables. Returns 0 for an empty party.
    int averageLevel() const;
};

// What the engine is doing right now; drives the top-level state machine. Only
// Exploration is exercised by this slice -- the rest are seated for later use.
enum class PlayMode { Exploration, Encounter, Dialogue, Victory, Defeat };

// The mutable position/progress of a play-session: everything that changes turn
// to turn. Serialized to .gnssav in a later slice.
struct PlayState {
    int mapId = 0;       // current map (Module map id)
    int areaId = 0;      // current area (0 = between / unknown areas)
    int turnCount = 0;   // exploration turns elapsed
    PlayMode mode = PlayMode::Exploration;
};

class Session {
public:
    // Build a session from a loaded module and a party, seating the play-state at
    // the module's declared start (startMapId / startAreaId). When those ids are
    // unset or do not resolve, it falls back to the first map and that map's first
    // area, so a freeform module still seats somewhere sensible. `seed` makes
    // every subsequent roll reproducible; pass a fixed value in tests.
    Session(Module module, Party party, std::uint64_t seed);

    const Module& module() const { return module_; }
    const Party& party() const { return party_; }
    Party& party() { return party_; }
    const PlayState& state() const { return state_; }
    PlayState& state() { return state_; }
    Dice& dice() { return dice_; }
    std::uint64_t seed() const { return seed_; }
    const PlotTracker& plot() const { return plot_; }
    PlotTracker& plot() { return plot_; }

    // Current map / area resolved against the owned module (nullptr if none).
    const Map* currentMap() const;
    const Area* currentArea() const;

    // True when the party was seated on the module's declared start area (rather
    // than a fallback) -- a useful signal for the engine UI and for tests.
    bool seatedAtDeclaredStart() const { return seatedAtStart_; }

    // Storyteller queries against the owned module + plot state.

    // True when `areaId` exists in the module and all its prerequisite control
    // points are complete. Unknown ids are not enterable.
    bool isAreaEnterable(int areaId) const;

    // Record a milestone / Control Item by control-point id. Returns true only
    // when the id names a control point this module defines AND it was newly
    // completed; unknown ids and repeats return false (and unknown ids are not
    // recorded).
    bool completeControlPoint(int controlPointId);

    // True when the party is standing on the module's declared end area.
    bool isAtEnd() const;

private:
    Module module_;
    Party party_;
    PlayState state_;
    std::uint64_t seed_ = 0;
    Dice dice_;
    PlotTracker plot_;
    bool seatedAtStart_ = false;
};

// Convenience: load a .gnsmod from disk and start a session seated at its start
// area. Throws gns::DbError if the module cannot be read.
Session startSessionFromFile(const std::string& modulePath, Party party,
                             std::uint64_t seed);

} // namespace gns
