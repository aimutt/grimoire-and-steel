#pragma once
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

struct Area;            // defined in Module.h; only a reference is needed here
struct AreaContext;     // defined in Module.h
struct ModuleVariable;  // defined in Module.h
struct ContextClause;   // defined in Module.h

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

    // --- Global variables -----------------------------------------------------
    // Current values of the module's typed globals (ModuleVariable), stored as canonical
    // strings (the type lives on the module so this stays module-independent/serializable).
    // Set by area choices; read by context conditions. Drops straight into a .gnssav.
    void setGlobal(const std::string& name, const std::string& value) { globals_[name] = value; }
    bool hasGlobal(const std::string& name) const { return globals_.count(name) != 0; }
    std::string getGlobal(const std::string& name) const {
        auto it = globals_.find(name);
        return it == globals_.end() ? std::string() : it->second;
    }
    const std::map<std::string, std::string>& globals() const { return globals_; }
    void setGlobals(std::map<std::string, std::string> g) { globals_ = std::move(g); }

    // --- Deactivated areas (set by area choices) ------------------------------
    // Areas a choice has removed from play: no longer drawn on the map and no longer
    // triggered when the party walks over them. Serialized into a .gnssav.
    void deactivateArea(int areaId) { deactivatedAreas_.insert(areaId); }
    bool isAreaActive(int areaId) const { return deactivatedAreas_.count(areaId) == 0; }
    const std::set<int>& deactivatedAreas() const { return deactivatedAreas_; }
    void setDeactivatedAreas(std::set<int> ids) { deactivatedAreas_ = std::move(ids); }

    // --- Deleted contexts (set by area choices) -------------------------------
    // Contexts a choice has removed, keyed by (areaId, context name). A deleted context
    // is skipped by activeContext so it never activates again. Serialized into a .gnssav.
    void deleteContext(int areaId, const std::string& ctxName) { deletedContexts_.insert({areaId, ctxName}); }
    bool isContextDeleted(int areaId, const std::string& ctxName) const {
        return deletedContexts_.count({areaId, ctxName}) != 0;
    }
    const std::set<std::pair<int, std::string>>& deletedContexts() const { return deletedContexts_; }
    void setDeletedContexts(std::set<std::pair<int, std::string>> c) { deletedContexts_ = std::move(c); }

private:
    std::set<int> completed_;
    std::set<std::string> flags_;
    std::set<int> resolvedChoiceAreas_;
    std::map<std::string, std::string> globals_;
    std::set<int> deactivatedAreas_;
    std::set<std::pair<int, std::string>> deletedContexts_;
};

// Initialise a tracker's globals from a module's variable declarations (each variable's
// default value). Call when a session starts.
void initGlobals(PlotTracker& plot, const std::vector<ModuleVariable>& vars);

// True when a single clause (<var> <op> <literal>) holds against the current globals, comparing
// by the variable's declared VarType (Bool/String support == and !=; Int/Float support all six).
bool evalClause(const ContextClause& clause, const PlotTracker& plot,
                const std::vector<ModuleVariable>& vars);

// True when *every* clause of a context's condition holds (AND). An empty condition is always
// true (so a no-condition context is always active -- valid only as an area's sole context).
bool contextConditionHolds(const AreaContext& ctx, const PlotTracker& plot,
                           const std::vector<ModuleVariable>& vars);

// The single active context of an area, or nullptr when none is active. When two or more are
// simultaneously active this is a LOGIC ERROR: returns nullptr and, if `conflict` is non-null,
// sets *conflict to a message naming the two offending contexts (the engine halts play on it).
const AreaContext* activeContext(const Area& area, const PlotTracker& plot,
                                 const std::vector<ModuleVariable>& vars,
                                 std::string* conflict = nullptr);

// The player-facing text a context should display: the first of its legacy `altTexts` whose flag
// is set (back-compat), else its `playerText`. Empty text is honoured (returns empty).
const std::string& areaContextText(const AreaContext& ctx, const PlotTracker& plot);

} // namespace gns
