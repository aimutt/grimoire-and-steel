#pragma once
#include "gns/Character.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

// .gnssav save/load (milestone M4). A standalone SQLite file capturing a whole play-session:
// enough to rebuild the Session (reload the module at modulePath, reseat the saved party under
// the recorded seed) plus the engine's view locals (cursor/facing/active character), the plot
// state (completed control points, decision flags, resolved choice areas) and the journal.
//
// Distinct from read-only gns.db, from the authored .gnsmod module, and from a single-character
// .gnschar. Same standalone-file + atomic-write discipline as ModuleIO/CharacterIO. For now the
// engine keeps a single auto-saved sidecar next to the module; a later slice moves saves to
// %APPDATA%. Follow the append-only-column + tolerant-load discipline when adding fields: bump
// kSaveFormatVersion and the header comment.

namespace gns {

constexpr int kSaveFormatVersion = 1;

// A complete snapshot of a play-session. PlayState fields are kept flat here so this struct does
// not track PlayState's own evolution; `mode` is a PlayMode cast to int.
struct GameSave {
    std::string modulePath;          // module to reload on resume (absolute or relative to the save)
    std::uint64_t seed = 0;          // session seed, so rolls continue on the same stream

    int mapId = 0, areaId = 0, turnCount = 0, mode = 0;      // PlayState
    int cursorX = 0, cursorY = 0, faceX = 0, faceY = 1;      // party token position + facing
    int activeChar = 0;                                      // selected party member (shopBuyer)

    std::set<int> controlPoints;         // PlotTracker completed ids
    std::set<std::string> flags;         // PlotTracker decision flags
    std::set<int> resolvedAreas;         // areas whose choices were decided

    std::vector<std::string> journal;    // adventure-log lines, in order
    std::vector<Character> party;        // full party (gold + inventory + AP ...)
};

// Persist a snapshot to a .gnssav SQLite file (atomic overwrite). Throws gns::DbError.
void saveGame(const std::string& path, const GameSave& save);

// Load a snapshot from a .gnssav SQLite file. Throws gns::DbError.
GameSave loadGame(const std::string& path);

} // namespace gns
