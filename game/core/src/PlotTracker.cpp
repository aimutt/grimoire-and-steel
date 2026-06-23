#include "gns/PlotTracker.h"
#include "gns/Module.h"   // gns::Area

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

} // namespace gns
