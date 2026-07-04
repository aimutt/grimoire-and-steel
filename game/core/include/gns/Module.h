#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "gns/Item.h"        // gns::InventoryItem
#include "gns/Character.h"   // gns::Character (AreaContext::characters)

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
    WoodenBridge = 20,  // plank deck, paintable (drag-drawn, one cell wide)
    StoneBridge  = 21,  // stone deck, paintable
    Stairs       = 22,  // steps, paintable (straight / L-shaped runs)
    WoodenStairs = 23,  // wooden flight: side rails + treads, paintable
    Dirt         = 24,  // packed earth: speckles like Sand but darker brown
    Count     = 25,
};

// Furniture / props placed at sub-grid positions on a map (not snapped to cells).
enum class ObjectType : int {
    Door = 0, Table, Chair, Chest, Fireplace, Cabinet, Box, Bar, Barrel, Bed,
    Well, StoneWall, WoodenWall, Fence, Altar,
    WoodenBridgeS, WoodenBridgeM, WoodenBridgeL,
    StoneBridgeS, StoneBridgeM, StoneBridgeL,
    CaveEntrance,
    SpiralStairs,
    Compass,
    Count
};

// A single placed prop. Position is in cell units (sub-grid), at the object centre.
struct MapObject {
    int id = 0;
    int type = 0;            // ObjectType
    float x = 0.0f;
    float y = 0.0f;
    float rotationDeg = 0.0f;  // clockwise rotation about the centre
    float scale = 1.0f;        // size multiplier (enlarge/shrink)
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

// One monster type the engine may spawn on area entry, with how many of them.
struct AreaMonster {
    std::string type;   // monster name (from gns.db) or free text
    int count = 1;
};

// A character (NPC) authored onto an area's Context. At play they join the encounter: a Foe
// fights on the monsters' side against the party; an Ally fights alongside the party against the
// monsters (allies are transient — they do not persist after the fight).
struct AreaCharacter {
    Character character;   // full character build (traits, gear, inventory, ...)
    bool foe = true;       // true = enemy (with the monsters); false = ally (with the party)
};

// One treasure-type possibility for an area, with its own independent chance.
struct AreaTreasure {
    std::string type;     // treasure-type code A-T or free text
    int chancePct = 0;    // 0-100
};

// A purchasable item in a shop/market area, priced in gold pieces.
struct ShopItem {
    std::string name;        // e.g. "Long sword", "Rations (1 week)"
    std::string description; // flavour / details shown to the party
    int costGp = 0;          // price in gold pieces
    int stock = 0;           // number available in stock
    std::string imagePath;   // optional free-file item image (fallback if no imageId)
    std::string imageId;     // baked-in item-art catalog id (filename), shown in both apps
};

// (InventoryItem now lives in gns/Item.h so Character.h and Module.h can share it without a
// cyclic include; see the include above.)

// One of an area's images. `direction` selects it by the party's facing at play time
// (-1 = any/default-eligible, 0 = North, 1 = East, 2 = South, 3 = West).
struct AreaImage {
    std::string path;
    int direction = -1;
};

// A link from one area to another (typically on a different map): e.g. stairs in a
// Level 1 area leading to a room on Level 2. Area ids are unique across all maps, so the
// target id alone identifies both the destination map and area.
struct AreaTransition {
    int targetAreaId = 0;   // destination area (0 = unset)
    std::string label;      // optional, e.g. "Stairs down to the crypt"
};

// Type of a module-level global variable. Persisted as int -- append only, never renumber.
enum class VarType : int { Bool = 0, Int = 1, String = 2, Float = 3 };

// A module-wide typed variable, authored at the module level and visible to every area. Its
// value is carried around as a canonical string: Bool "true"/"false", Int "5", Float "1.5",
// String verbatim. The play-session's current values live in PlotTracker::globals(); the type
// lives here (so PlotTracker stays module-independent).
struct ModuleVariable {
    std::string name;
    VarType type = VarType::Bool;
    std::string defaultValue;   // canonical string (see above)
};

// One AND-ed clause of a Context's activation condition: <varName> <op> <literal value>.
// op: 0 ==, 1 !=, 2 <, 3 <=, 4 >, 5 >=. Ordering ops (<,<=,>,>=) apply to Int/Float only.
struct ContextClause {
    std::string varName;
    int op = 0;
    std::string value;   // canonical literal, compared per the variable's VarType
};

// A choice's mutation of a global variable. op: 0 set, 1 add, 2 subtract (add/subtract are
// Int/Float only; Bool/String support set). An AreaChoice with no mutations (and no other
// effect) is the "do nothing" choice.
struct VarMutation {
    std::string varName;
    int op = 0;
    std::string value;   // canonical literal operand
};

// A single decision the party can make at an area (cap 3 per context). Every effect field is
// optional -- an empty/zero field means "no effect". Presented inline in the engine's area
// view; the chosen option's effects are applied to the active character and recorded in the
// play-session's PlotTracker so the choice sticks across re-entry and into a save file.
struct AreaChoice {
    std::string label;              // button text, e.g. "We'll help you."
    std::string journalEntry;       // appended to the journal when chosen
    std::string setFlag;            // LEGACY decision flag (kept for load/migration; use mutations)
    int completeControlPointId = 0; // 0 = none; else marks a Control Point complete (unlocks gated areas)
    int goldDelta = 0;              // +/- gold applied to EVERY party member (each gets the full amount)
    std::string grantItemName;      // LEGACY plain item name (migrated into grantItem.name on load)
    InventoryItem grantItem;        // granted to the active character (empty name = none); quantity = count
    std::string takeItemName;       // removed from the active character's inventory (empty = none)
    std::vector<VarMutation> mutations;  // mutations to global variables (empty = none)
    bool deactivateArea = false;    // when chosen, remove this area from the map + stop triggering it
    bool deleteContext = false;     // when chosen, stop THIS context from ever activating again
};

// Player-facing text shown *instead of* Area::playerText when its flag is set. First match
// (in list order) wins; if none match, Area::playerText (the default) is shown. Empty text is
// allowed (e.g. show nothing once a decision is made -- the journal keeps the full record).
struct AreaConditionalText {
    std::string requiredFlag;
    std::string text;
};

// A Context owns all of an area's *branchable* content -- descriptions, statistics, images,
// music, shop, transitions, and decisions -- plus a condition (AND-ed clauses over module
// variables) deciding when it is the area's active context. At play time EXACTLY ONE context
// may be active: 2+ simultaneously active is a logic error that halts gameplay; 0 active means
// the area is inert (shows nothing). A context with no conditions is always active, so it is
// only valid as an area's sole context.
struct AreaContext {
    std::string name;                          // unique within the area (author-facing, internal)
    std::vector<ContextClause> conditions;     // all AND-ed; empty = always active

    std::string dmText;         // DM-only description
    std::string playerText;     // player-facing description

    // Statistics the engine uses (chances are 0-100).
    int monsterChancePct = 0;
    std::string monsterType;    // legacy single type (kept for backward-compat/migration)
    std::vector<AreaMonster> monsters;
    std::vector<AreaCharacter> characters;  // authored NPCs (foes fight with monsters, allies with the party)
    int treasureChancePct = 0;
    std::string treasureType;   // legacy single treasure (kept for backward-compat/migration)
    std::vector<AreaTreasure> treasures;
    int trapChancePct = 0;
    std::string trapDescription;
    int lockChancePct = 0;
    std::string lockDescription;
    int hiddenChancePct = 0;
    std::string hiddenDescription;

    std::string artworkPath;    // legacy single image (kept for back-compat/migration)
    std::vector<AreaImage> images;
    int defaultImage = 0;
    std::string musicPath;

    bool isShop = false;
    std::vector<ShopItem> shopItems;

    std::vector<AreaTransition> transitions;

    std::string choicePrompt;
    std::vector<AreaChoice> choices;

    // Legacy flag-gated alternate player text (kept for migration; superseded by contexts).
    std::vector<AreaConditionalText> altTexts;
};

// A described region of a map (a set of fine cells sharing an id).
struct Area {
    int id = 0;
    std::string label;          // coarse-grid coordinate, e.g. "A1"
    bool labelAuto = true;      // true = label auto-tracks the area's map position
                                // (set false once the author edits the label by hand)
    std::string name;           // optional human name, e.g. "Guard Room"
    std::uint32_t color = 0x3366CCFFu;  // RGBA fill tint on the map
    bool fillEnabled = true;    // false = outline only (no fill tint drawn)

    bool hidden = false;        // hidden at play time (invisible on the map, still playable)

    // All branchable content lives in contexts now. Exactly one is active at play time (see
    // AreaContext). A fresh area gets one empty-condition default context so it plays at once.
    std::vector<AreaContext> contexts;

    // Control points that must be completed before a party may enter this area.
    std::vector<int> prerequisiteControlPointIds;

    // --- LEGACY (v<=14) content fields: populated only when loading an old module, then folded
    // into a synthesized default context by loadModule. Unused once migrated; not written in v15+.
    std::string dmText;         // DM-only description
    std::string playerText;     // player-facing description

    // Statistics the engine uses (chances are 0-100).
    int monsterChancePct = 0;
    std::string monsterType;    // legacy single type (kept for backward-compat/migration)
    std::vector<AreaMonster> monsters;  // one or more types to spawn, each with a count
    int treasureChancePct = 0;
    std::string treasureType;   // legacy single treasure (kept for backward-compat/migration)
    std::vector<AreaTreasure> treasures;  // one or more treasure types, each with a chance
    int trapChancePct = 0;
    std::string trapDescription;
    int lockChancePct = 0;      // chance a character can open/pick
    std::string lockDescription;
    int hiddenChancePct = 0;    // chance a party discovers hidden items
    std::string hiddenDescription;

    std::string artworkPath;    // legacy single image (kept for back-compat/migration)
    std::vector<AreaImage> images;  // up to 4 images; chosen by facing, else defaultImage
    int defaultImage = 0;       // index into images shown when no direction matches
    std::string musicPath;      // music that plays while the party is in this area

    // Shop/market: when isShop, the party may buy & sell here from this supply list.
    bool isShop = false;
    std::vector<ShopItem> shopItems;

    // Exits from this area to other areas (often on another map).
    std::vector<AreaTransition> transitions;

    // Player decisions offered on entry (cap 3). When present and unresolved, the engine shows
    // choicePrompt plus a button per choice inline in the area view; the picked choice's effects
    // are applied and recorded so the area stops prompting once decided.
    std::string choicePrompt;                    // question shown above the choice buttons
    std::vector<AreaChoice> choices;             // <= 3
    std::vector<AreaConditionalText> altTexts;   // flag-gated alternates for playerText
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
    std::string coverArtPath;   // splash/cover image the engine shows when the module loads
    std::string splashMusicPath;   // music for the cover/splash screen
    std::string defaultMusicPath;  // music when the party is not in a defined area
    std::vector<Map> maps;
    std::vector<ControlPoint> controlPoints;
    std::vector<ModuleVariable> variables;  // module-wide typed globals, visible to every area
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
// control-point kind column (Control Point vs Control Item); v6 added the area
// fill_enabled column and the area_monsters table (multiple monster types/counts);
// v7 added the area_transitions table (cross-area/cross-map exits); v8 added the
// module cover_art column (splash image shown by the engine on load); v9 added the
// area label_auto column (label auto-tracks map position until edited by hand); v10
// added the area is_shop column + area_treasures and area_shop_items tables; v11 added
// shop-item description, stock, and image columns to area_shop_items; v12 added the module
// splash_music/default_music columns and the area music column; v13 added the area hidden
// column, the area_images table (multi-image + per-direction + default), the map_objects scale
// column, and the area_shop_items image_id column; v14 added the area choice_prompt column and
// the area_choices and area_alt_texts tables (per-area decisions + flag-gated alternate text);
// v15 moved all branchable area content into per-area Contexts (the module_variables table for
// typed globals; the area_contexts table + its context_* child tables -- conditions, monsters,
// treasures, images, shop_items, transitions, choices, and context_choice_mutations; the areas
// table dropped its content columns). Pre-v15 modules are migrated on load into one default
// context per area. v16 gave context choices a rich granted item (grant_item_desc/image/path/qty
// columns; the legacy grant_item name migrates into it) plus deactivate_area/delete_context flags
// (a chosen option can remove its area from play or delete its context). v17 added authored
// Context characters: the context_characters table (mirroring the .gnschar character columns + a
// foe flag) and its context_character_training/context_character_spell/context_character_item child
// tables (the last carrying the per-item value column).
constexpr int kModuleFormatVersion = 17;

// Persist a module to a .gnsmod SQLite file (overwrites). Throws gns::DbError.
void saveModule(const Module& mod, const std::string& path);

// Load a module from a .gnsmod SQLite file. Throws gns::DbError.
Module loadModule(const std::string& path);

} // namespace gns
