#include "gns/Session.h"

#include <utility>

namespace gns {

// ---- Party ------------------------------------------------------------------

bool Party::isWiped() const {
    if (members.empty()) return false;
    for (const auto& c : members)
        if (c.hp > 0) return false;
    return true;
}

int Party::averageLevel() const {
    if (members.empty()) return 0;
    long sum = 0;
    for (const auto& c : members) sum += c.level;
    return static_cast<int>(sum / static_cast<long>(members.size()));
}

// ---- lookups ----------------------------------------------------------------

namespace {

const Map* findMap(const Module& m, int id) {
    for (const auto& mp : m.maps)
        if (mp.id == id) return &mp;
    return nullptr;
}

// Find the area with `id`; report the owning map through `outMap` when non-null.
const Area* findArea(const Module& m, int id, const Map** outMap) {
    for (const auto& mp : m.maps)
        for (const auto& a : mp.areas)
            if (a.id == id) {
                if (outMap) *outMap = &mp;
                return &a;
            }
    if (outMap) *outMap = nullptr;
    return nullptr;
}

const ControlPoint* findControlPoint(const Module& m, int id) {
    for (const auto& cp : m.controlPoints)
        if (cp.id == id) return &cp;
    return nullptr;
}

} // namespace

// ---- Session ----------------------------------------------------------------

Session::Session(Module module, Party party, std::uint64_t seed)
    : module_(std::move(module)), party_(std::move(party)), seed_(seed) {
    dice_.seed(seed_);

    // Seat the map: the declared start map if it resolves, else the first map.
    const Map* startMap =
        module_.startMapId != 0 ? findMap(module_, module_.startMapId) : nullptr;
    if (!startMap && !module_.maps.empty()) startMap = &module_.maps.front();
    state_.mapId = startMap ? startMap->id : 0;

    // Seat the area: the declared start area if it exists (snapping the seated map
    // to the one that owns it). Area ids are unique across the module, so this is
    // unambiguous.
    if (module_.startAreaId != 0) {
        const Map* owner = nullptr;
        if (const Area* a = findArea(module_, module_.startAreaId, &owner)) {
            state_.areaId = a->id;
            if (owner) state_.mapId = owner->id;
            seatedAtStart_ = true;
        }
    }
    // Fallback: first area of the seated map.
    if (state_.areaId == 0) {
        if (const Map* mp = findMap(module_, state_.mapId))
            if (!mp->areas.empty()) state_.areaId = mp->areas.front().id;
    }

    state_.turnCount = 0;
    state_.mode = PlayMode::Exploration;
}

const Map* Session::currentMap() const {
    return findMap(module_, state_.mapId);
}

const Area* Session::currentArea() const {
    return findArea(module_, state_.areaId, nullptr);
}

bool Session::isAreaEnterable(int areaId) const {
    const Area* a = findArea(module_, areaId, nullptr);
    return a && plot_.isAreaEnterable(*a);
}

bool Session::completeControlPoint(int controlPointId) {
    if (!findControlPoint(module_, controlPointId)) return false;  // unknown id
    return plot_.complete(controlPointId);
}

bool Session::isAtEnd() const {
    return module_.endAreaId != 0 && state_.areaId == module_.endAreaId;
}

Session startSessionFromFile(const std::string& modulePath, Party party,
                             std::uint64_t seed) {
    return Session(loadModule(modulePath), std::move(party), seed);
}

} // namespace gns
