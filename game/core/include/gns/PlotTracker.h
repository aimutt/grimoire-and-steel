#pragma once
#include <set>
#include <string>
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

    // --- Decision flags (set by area choices) ---------------------------------
    // Named string flags recorded when the party makes a choice. They drive which
    // player-facing text an area shows (see AreaConditionalText) and can gate future
    // content. Like the completed set, this is a plain serializable value.
    void setFlag(const std::string& name) { flags_.insert(name); }
    bool hasFlag(const std::string& name) const { return flags_.count(name) != 0; }
    const std::set<std::string>& flags() const { return flags_; }
    void setFlags(std::set<std::string> f) { flags_ = std::move(f); }

    // --- Resolved choice areas ------------------------------------------------
    // Areas whose choices have already been decided, so the engine stops re-prompting
    // once a choice was made there (even after the party leaves and returns).
    void resolveChoiceArea(int areaId) { resolvedChoiceAreas_.insert(areaId); }
    bool isChoiceResolved(int areaId) const { return resolvedChoiceAreas_.count(areaId) != 0; }
    const std::set<int>& resolvedChoiceAreas() const { return resolvedChoiceAreas_; }
    void setResolvedChoiceAreas(std::set<int> ids) { resolvedChoiceAreas_ = std::move(ids); }

private:
    std::set<int> completed_;
    std::set<std::string> flags_;
    std::set<int> resolvedChoiceAreas_;
};

// The player-facing text an area should display given the current plot state: the first of
// `area.altTexts` whose flag is set, else `area.playerText` (the default). Empty alternate text
// is honoured (returns an empty string), so an author can suppress an area's prose once decided.
const std::string& areaDisplayText(const Area& area, const PlotTracker& plot);

} // namespace gns
