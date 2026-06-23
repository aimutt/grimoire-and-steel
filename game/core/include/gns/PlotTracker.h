#pragma once
#include <set>
#include <utility>

// Storyteller: plot-progress tracking for a play-session (milestone M4).
//
// A PlotTracker records which of a module's ControlPoints the party has completed
// (milestones reached / Control Items acquired) and answers whether a gated Area
// may be entered yet. It is a plain, serializable value -- just the set of
// completed control-point ids, with NO pointer back into the Module -- so a
// Session that owns one stays freely copyable/movable and the set drops straight
// into a .gnssav later. Queries that need module data take it as an argument.
//
// Gating is intentionally permissive: an Area with no prerequisites is always
// enterable, so a module that defines no control points (or no prerequisites)
// plays unrestricted -- the tracker is simply inert.

namespace gns {

struct Area;   // defined in Module.h; only a reference is needed here

class PlotTracker {
public:
    // Mark a control point complete. Returns true if it was newly completed
    // (false if already recorded) so the engine can fire one-time unlock /
    // re-narration exactly on the transition.
    bool complete(int controlPointId);

    bool isComplete(int controlPointId) const;

    // True when every prerequisite control point of `area` is complete. An area
    // with no prerequisites is always enterable.
    bool isAreaEnterable(const Area& area) const;

    const std::set<int>& completedIds() const { return completed_; }

    // Restore from a save: replace the completed set wholesale.
    void setCompletedIds(std::set<int> ids) { completed_ = std::move(ids); }

private:
    std::set<int> completed_;
};

} // namespace gns
