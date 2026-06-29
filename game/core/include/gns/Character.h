#pragma once
#include <string>
#include <vector>

namespace gns {

class Repository;

// The four Grimoire & Steel traits.
enum class TraitId { Might, Grace, Wits, Spirit };

// A character's trait bonuses. At creation these are assigned from the spread
// {+2, +1, +0, -1}; they may rise to a maximum of +3 through advancement.
struct Traits {
    int might = 0, grace = 0, wits = 0, spirit = 0;
    int get(TraitId t) const;
    void set(TraitId t, int v);
};

// A playable character. Life replaces hit points; Defense replaces armor class;
// Advancement Points (AP) replace experience points; Strain tracks spell fatigue.
struct Character {
    std::string name;
    std::string kin;        // ancestry name: "Human", "Dwarf", ...
    std::string calling;    // class name: "Blade", "Shadow", "Sage", "Mystic"
    int level = 1;
    Traits traits;
    std::vector<std::string> trainings;   // training names the character has

    int maxLife = 1;
    int life = 1;
    int defense = 10;
    int ap = 0;             // cumulative advancement points
    int strain = 0;         // current accumulated strain
    int gold = 100;         // gold pieces (starting default; spent at shops)

    std::vector<std::string> spells;      // known spell names (Mystics)
    std::vector<std::string> inventory;   // item names the character carries (bought goods)

    // Equipment shaping combat (a small profile so the engine needn't re-derive).
    std::string armorName = "No armor";
    bool shield = false;
    std::string weaponName;               // display / chosen weapon
    std::string weaponDamageDie = "1d6";  // damage die for attacks
    int weaponBonus = 0;                   // magic weapon bonus (+1..+3)

    // Flavor / identity (no rules code reads these; persisted with the character).
    std::string portraitPath;   // avatar image filename (resolved against the engine's assets)
    std::string playerName;
    std::string background;
    std::string goal;
    std::string personality;
    std::string notes;
};

// --- creation ---------------------------------------------------------------

// True when the spread is exactly one each of {+2,+1,+0,-1} and none exceed +3.
bool validTraitSpread(const Traits& t);
// Trainings a new character of this kin should pick (4 for Human, else 3).
int requiredTrainingCount(const std::string& kinName);

// Build a level-1 character: Life = max(6, 10 + Might) (+1 for Dwarf);
// Defense = 10 + Grace + armor bonus (+1 for a shield). No dice are rolled.
Character makeCharacter(const Repository& repo, const std::string& name,
                        const std::string& kinName, const std::string& callingName,
                        const Traits& traits, const std::vector<std::string>& trainings,
                        const std::string& armorName = "No armor", bool shield = false);

// --- derived combat values --------------------------------------------------

bool hasTraining(const Character& c, const std::string& trainingName);
// Melee attack bonus: Might + weapon bonus, +2 when trained with a weapon type.
int meleeAttackBonus(const Character& c);
// Ranged attack bonus: Grace + weapon bonus, +2 when trained with a weapon type.
int rangedAttackBonus(const Character& c);
// Safe strain limit before backlash: max(1, 3 + Spirit).
int strainLimit(const Character& c);

} // namespace gns
