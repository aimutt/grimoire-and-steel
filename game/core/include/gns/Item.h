#pragma once
#include <string>
#include <vector>

// A carried item, shared by the character model (Character::inventory), the authored module
// model (AreaChoice::grantItem), and saves. Kept in its own tiny header so both Character.h and
// Module.h can include it without a cyclic dependency (Character.h <- Module.h).

namespace gns {

// A mutation of a global variable. op: 0 set, 1 add, 2 subtract (add/subtract are Int/Float only;
// Bool/String support set). Used by choices (AreaChoice::mutations), area enter/exit hooks
// (Area::onEnter/onExit), and item acquire/loss hooks (InventoryItem/ShopItem::onAcquire/onUnacquire).
// Lives here (not Module.h) so InventoryItem can carry mutation lists without a cyclic include.
struct VarMutation {
    std::string varName;
    int op = 0;
    std::string value;   // canonical literal operand
    // Scope of varName: 0 = module global (Module::variables); >0 = the owning area's id, whose
    // Area::variables this mutation targets. A mutation may only ever write a module global or its
    // OWN area's variable (other areas' variables are read-only), enforced by the Module Creator UI.
    int scopeAreaId = 0;
};

// An item a character carries. Same art model as ShopItem (baked-in imageId, else free-file
// imagePath). Identical names stack: quantity tracks how many are held. Granted by choices
// (AreaChoice::grantItem) or bought at shops; persisted in .gnschar and .gnssav.
struct InventoryItem {
    std::string name;        // display name; the stacking key (matched by name)
    std::string description; // flavour / details shown in the tooltip
    std::string imageId;     // baked-in item-art catalog id (filename)
    std::string imagePath;   // free-file item image (fallback if no imageId)
    int quantity = 1;        // number held (>=1); a stack of identical items
    int value = 0;           // GP worth of one item (editable when assigned; default = catalog cost)
    // Equip profile: lets any item be equipped/unequipped and used in combat (drag-and-drop on the
    // character sheet). slot: 0 none, 1 weapon, 2 armor, 3 shield.
    int slot = 0;
    std::string damageDie;   // weapon damage die (slot==1), e.g. "1d8"
    int defenseBonus = 0;    // Defense granted when worn (slot==2 armor / slot==3 shield)
    int weaponBonus = 0;     // magic weapon bonus (+1..+3) carried with an equipped weapon
    bool dropable = true;    // authored default on a grant; runtime status lives in PlotTracker by name
    std::vector<VarMutation> onAcquire;    // applied when this item enters an inventory
    std::vector<VarMutation> onUnacquire;  // applied when this item leaves an inventory
};

} // namespace gns
