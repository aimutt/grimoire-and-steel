#pragma once
#include <string>

// A carried item, shared by the character model (Character::inventory), the authored module
// model (AreaChoice::grantItem), and saves. Kept in its own tiny header so both Character.h and
// Module.h can include it without a cyclic dependency (Character.h <- Module.h).

namespace gns {

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
};

} // namespace gns
