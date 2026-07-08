#include "gns/PlotTracker.h"
#include "gns/Module.h"   // gns::Area, AreaContext, ModuleVariable, ContextClause

#include <cstdlib>
#include <string>

namespace gns {

bool PlotTracker::complete(int controlPointId) {
    return completed_.insert(controlPointId).second;
}

bool PlotTracker::isComplete(int controlPointId) const {
    return completed_.count(controlPointId) != 0;
}

bool PlotTracker::isAreaEnterable(const Area& area) const {
    for (int cp : area.prerequisiteControlPointIds)
        if (completed_.count(cp) == 0) return false;
    return true;
}

// ---- Global variables + context selection -----------------------------------

namespace {

bool truthy(const std::string& s) { return s == "true" || s == "1"; }

// Compare two canonical-string operands by `op`, interpreting them per `type`.
//   op: 0 ==, 1 !=, 2 <, 3 <=, 4 >, 5 >=  (ordering ops meaningful for Int/Float only).
bool compare(VarType type, const std::string& lhs, const std::string& rhs, int op) {
    if (type == VarType::Int || type == VarType::Float) {
        double a = std::atof(lhs.c_str());
        double b = std::atof(rhs.c_str());
        switch (op) {
            case 0: return a == b;
            case 1: return a != b;
            case 2: return a <  b;
            case 3: return a <= b;
            case 4: return a >  b;
            case 5: return a >= b;
            default: return false;
        }
    }
    // Bool + String: equality by normalised value (Bool tolerates true/false vs 1/0).
    bool eq = (type == VarType::Bool) ? (truthy(lhs) == truthy(rhs)) : (lhs == rhs);
    switch (op) {
        case 0: return eq;
        case 1: return !eq;
        default: return false;  // ordering not defined for Bool/String
    }
}

} // namespace

std::string varKey(int scopeAreaId, const std::string& name) {
    if (scopeAreaId == 0) return name;   // module global: plain name (legacy-compatible)
    return "@" + std::to_string(scopeAreaId) + ":" + name;
}

const ModuleVariable* resolveVar(const Module& mod, int scopeAreaId, const std::string& name) {
    if (scopeAreaId == 0) {
        for (const auto& v : mod.variables)
            if (v.name == name) return &v;
        return nullptr;
    }
    const Area* a = const_cast<Module&>(mod).areaById(scopeAreaId);
    if (!a) return nullptr;
    for (const auto& v : a->variables)
        if (v.name == name) return &v;
    return nullptr;
}

void initGlobals(PlotTracker& plot, const Module& mod) {
    std::map<std::string, std::string> g;
    for (const auto& v : mod.variables) g[varKey(0, v.name)] = v.defaultValue;
    for (const auto& map : mod.maps)
        for (const auto& area : map.areas)
            for (const auto& v : area.variables) g[varKey(area.id, v.name)] = v.defaultValue;
    plot.setGlobals(std::move(g));
}

bool evalClause(const ContextClause& clause, const PlotTracker& plot, const Module& mod) {
    const ModuleVariable* v = resolveVar(mod, clause.scopeAreaId, clause.varName);
    if (!v) return false;   // clause references an unknown variable -> never holds
    // Current value: the plot's live value if set, else the declared default.
    std::string key = varKey(clause.scopeAreaId, clause.varName);
    std::string cur = plot.hasGlobal(key) ? plot.getGlobal(key) : v->defaultValue;
    return compare(v->type, cur, clause.value, clause.op);
}

bool contextConditionHolds(const AreaContext& ctx, const PlotTracker& plot, const Module& mod) {
    for (const auto& clause : ctx.conditions)
        if (!evalClause(clause, plot, mod)) return false;
    return true;   // empty condition -> always true
}

const AreaContext* activeContext(const Area& area, const PlotTracker& plot, const Module& mod,
                                 std::string* conflict) {
    const AreaContext* found = nullptr;
    for (const auto& ctx : area.contexts) {
        if (plot.isContextDeleted(area.id, ctx.name)) continue;   // removed by a choice
        if (!contextConditionHolds(ctx, plot, mod)) continue;
        if (found) {
            if (conflict) {
                std::string an = area.name.empty() ? area.label : area.name;
                *conflict = "Logic error in area '" + an + "': contexts '" + found->name +
                            "' and '" + ctx.name + "' are both active. Fix their conditions.";
            }
            return nullptr;   // 2+ active -> logic error
        }
        found = &ctx;
    }
    return found;   // exactly one, or nullptr when none matched
}

const std::string& areaContextText(const AreaContext& ctx, const PlotTracker& plot) {
    for (const auto& alt : ctx.altTexts)
        if (plot.hasFlag(alt.requiredFlag)) return alt.text;
    return ctx.playerText;
}

} // namespace gns
