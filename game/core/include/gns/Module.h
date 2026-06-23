#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Authored-adventure data model + .gnsmod persistence (milestone M2).
//
// A Module is the unit the Module Creator authors and the Game Engine plays. It
// owns one or more grid Maps; each Map cell carries a Terrain paint value and an
// owning Area id. Areas hold the DM/player descriptions and the per-area
// statistics the engine uses to drive play. ControlPoints are plot markers placed
// at an area; an Area may list prerequisite control points that must be completed
// before a party may enter it.
//
// Modules are stored as standalone SQLite files (separate from the read-only
// gns.db rules database). Saves (.gnssav) are a later milestone.

namespace gns {

// Fine-grid cell paint. Stored as int in Map::cells; values are persisted, so keep
// existing numbers stable and only append new terrain types.
enum class Terrain : int {
    Empty    = 0,
    Floor    = 1,
    Wall     = 2,
    Door     = 3,
    Water    = 4,   // pond / lake / river
    Grass    = 5,
    Trees    = 6,   // forest / woods
    Rocky    = 7,   // rocky / boulder ground
    Mountain = 8,
    Sand     = 9,
    Swamp    = 10,
    Road     = 11,  // path / trail
    Path      = 12, // dirt path / trail through open ground
    Ruins     = 13, // broken stone / rubble
    Graveyard = 14,
    Lava      = 15,
    AcidPool  = 16,
    Ditch     = 17,
    Crevice   = 18,
    Hills     = 19,
    Count     = 20,
};

// Furniture / props placed at sub-grid positions on a map (not snapped to cells).
enum class ObjectType : int {
    Door = 0, Table, Chair, Chest, Fireplace, Cabinet, Box, Bar, Barrel, Bed,
    Well, StoneWall, WoodenWall, Fence, Altar,
    WoodenBridgeS, WoodenBridgeM, WoodenBridgeL,
    StoneBridgeS, StoneBridgeM, StoneBridgeL,
    CaveEntrance,
    Count
};

// A single placed prop. Position is in cell units (sub-grid), at the object centre.
struct MapObject {
    int id = 0;
    int type = 0;            // ObjectType
    float x = 0.0f;
    float y = 0.0f;
    float rotationDeg = 0.0f;  // clockwise rotation about the centre
};

// A free text label placed on a map at a sub-grid position, any colour/size.
struct MapText {
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
    std::uint32_t color = 0xFFFFFFFFu;  // RGBA
    float sizePx = 18.0f;
};

// A plot marker placed on a map. Referenced by id from Area prerequisites.
struct ControlPoint {
    int id = 0;
    std::string name;
    std::string description;
    int mapId = 0;     // where the control point lives
    int areaId = 0;    // area under the marker (0 = none), for prerequisite linkage
    int kind = 0;      // 0 = Control Point, 1 = Control Item (plot item to acquire/use)
    float x = -1.0f;   // sub-grid position; <0 means "legacy: use area centroid"
    float y = -1.0f;
};

// A described region of a map (a set of fine cells sharing an id).
struct Area {
    int id = 0;
    std::string label;          // coarse-grid coordinate, e.g. "A1"
    std::string name;           // optional human name, e.g. "Guard Room"
    std::uint32_t color = 0x3366CCFFu;  // RGBA fill tint on the map

    std::string dmText;         // DM-only description
    std::string playerText;     // player-facing description

    // Statistics the engine uses (chances are 0-100).
    int monsterChancePct = 0;
    std::string monsterType;    // monster name (from gns.db) or free text
    int treasureChancePct = 0;
    std::string treasureType;   // treasure-type code A-T or free text
    int trapChancePct = 0;
    std::string trapDescription;
    int lockChancePct = 0;      // chance a character can open/pick
    std::string lockDescription;
    int hiddenChancePct = 0;    // chance a party discovers hidden items
    std::string hiddenDescription;

    std::string artworkPath;    // relative path; image preview is deferred

    // Control points that must be completed before a party may enter this area.
    std::vector<int> prerequisiteControlPointIds;
};

// An overhead grid map. cells/cellArea are row-major, size gridW*gridH.
struct Map {
    int id = 0;
    std::string name;
    int gridW = 32, gridH = 24;   // fine-grid cell counts
    int overlayW = 8, overlayH = 6;  // coarse overlay grid for A1/C4 labels
    std::vector<int> cells;       // Terrain value per fine cell
    std::vector<int> cellArea;    // owning area id per fine cell (0 = none)
    std::vector<Area> areas;
    std::vector<MapObject> objects;   // sub-grid props
    std::vector<MapText> texts;       // free text labels
};

// A complete adventure module.
struct Module {
    std::string name;
    std::string author;
    std::string summary;
    std::vector<Map> maps;
    std::vector<ControlPoint> controlPoints;
    int startMapId = 0;   // beginning of the adventure
    int startAreaId = 0;
    int endAreaId = 0;    // end of the adventure

    // Find the next unused id across maps / areas / control points so the editor
    // can hand out stable ids without collisions.
    int nextMapId() const;
    int nextAreaId() const;     // unique across all maps
    int nextControlPointId() const;
    int nextObjectId() const;   // unique across all maps
    int nextTextId() const;     // unique across all maps

    Map* mapById(int id);
    Area* areaById(int id);     // searches every map
};

// On-disk format version, written to PRAGMA user_version.
// v2 added the map_objects table (sub-grid props); v3 added object rotation;
// v4 added control-point position (x,y) and the map_texts table; v5 added the
// control-point kind column (Control Point vs Control Item).
constexpr int kModuleFormatVersion = 5;

// Persist a module to a .gnsmod SQLite file (overwrites). Throws gns::DbError.
void saveModule(const Module& mod, const std::string& path);

// Load a module from a .gnsmod SQLite file. Throws gns::DbError.
Module loadModule(const std::string& path);

} // namespace gns
