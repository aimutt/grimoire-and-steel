#pragma once
#include <functional>
#include <string>
#include <vector>
#include "gns/Character.h"
#include "gns/Item.h"

struct SDL_Texture;

namespace gns { class Repository; }

// A shared Grimoire & Steel character builder used by BOTH apps: the Game Engine's "Create
// Character" screen and the Module Creator's per-Context character authoring. Kept in gns_ui (which
// links imgui + gns_core + SDL2) and made app-agnostic via texture-resolver callbacks so it never
// depends on a per-app baked art catalog.
namespace gns::ui {

// Editable, UI-facing character state. Distinct from gns::Character (the built result) because the
// editor tracks a few extra things (equipment Defense bonuses chosen from the catalog, the raw
// trait spread) and previews derived stats live. buildCharacter() turns this into a Character.
struct CharacterDraft {
    std::string name, player, background, goal, personality, notes;
    std::string kin, calling;
    int traitVals[4] = {2, 1, 0, -1};       // Might, Grace, Wits, Spirit
    std::vector<std::string> trainings;     // selected training names
    std::vector<std::string> spells;        // selected spell names (Mystic)

    // Equipment, chosen from the gns.db `equipment` catalog gallery (drives combat directly).
    std::string weaponName;                 // e.g. "Long Sword" ("" = unarmed)
    std::string weaponDamageDie = "1d6";    // damage die of the chosen weapon
    int weaponBonus = 0;                    // magic +0..+3
    std::string armorName = "No armor";     // e.g. "Chain Mail" or "No armor"
    int armorDefenseBonus = 0;              // Defense bonus of the chosen armor
    bool shield = false;
    std::string shieldName;                 // display name of the chosen shield (editor-only)
    int shieldDefenseBonus = 1;             // Defense bonus of the chosen shield (+1)

    std::string portraitPath;               // portrait filename ("" = placeholder)
    int gold = 25;
    std::vector<InventoryItem> inventory;   // carried items (name/description/value/quantity/art)
};

// Host-supplied resolvers so this shared code needs no per-app baked catalog.
struct CharacterEditorHost {
    std::function<SDL_Texture*(const std::string& imageId)> itemTexture;      // catalog id -> texture
    std::vector<std::string> portraits;                                       // portrait filenames
    std::function<SDL_Texture*(const std::string& portraitFile)> portraitTexture;
};

// Seed a fresh draft's kin/calling to the first available options.
void initCharacterDraft(const gns::Repository& repo, CharacterDraft& draft);

// Draw the whole editor into the current ImGui window; mutates `draft`. Returns true if anything
// changed this frame.
bool drawCharacterEditor(const gns::Repository& repo, CharacterDraft& draft,
                         const CharacterEditorHost& host);

// True when the draft is a complete, saveable character. `whyNot` (optional) gets a reason if not.
bool characterDraftValid(const gns::Repository& repo, const CharacterDraft& draft,
                         std::string* whyNot = nullptr);

// Build the finished Character (derived stats from the chosen equipment, inventory, flavor...).
gns::Character buildCharacter(const gns::Repository& repo, const CharacterDraft& draft);

// Reconstruct a draft from an existing Character so it can be re-edited.
CharacterDraft draftFromCharacter(const gns::Repository& repo, const gns::Character& ch);

} // namespace gns::ui
