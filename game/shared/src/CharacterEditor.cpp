#include "gns/ui/CharacterEditor.h"
#include "gns/Repository.h"
#include "gns/Model.h"
#include "gns/Rules.h"   // spellCastBonus
#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace gns::ui {
namespace {

// --- small ImGui/std::string helpers (local to keep gns_ui self-contained) -------------------
int strResizeCb(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize((size_t)data->BufTextLen);
        data->Buf = s->data();
    }
    return 0;
}
bool inputText(const char* label, std::string* s, ImGuiInputTextFlags flags = 0) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, s->data(), s->capacity() + 1, flags, strResizeCb, s);
}
bool inputMultiline(const char* label, std::string* s, const ImVec2& size) {
    return ImGui::InputTextMultiline(label, s->data(), s->capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize, strResizeCb, s);
}
bool comboField(const char* label, std::string* value, const std::vector<std::string>& options) {
    bool changed = false;
    if (ImGui::BeginCombo(label, value->c_str())) {
        for (const auto& opt : options) {
            bool sel = (opt == *value);
            if (ImGui::Selectable(opt.c_str(), sel)) { *value = opt; changed = true; }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
void sectionHeader(const char* label) { ImGui::Spacing(); ImGui::SeparatorText(label); }

bool selected(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
void toggle(std::vector<std::string>& v, const std::string& s, bool on) {
    auto it = std::find(v.begin(), v.end(), s);
    if (on && it == v.end()) v.push_back(s);
    else if (!on && it != v.end()) v.erase(it);
}

// A thumbnail for an equipment/inventory item by its catalog imageId (nullptr-safe host).
SDL_Texture* itemTex(const CharacterEditorHost& host, const std::string& imageId) {
    return (host.itemTexture && !imageId.empty()) ? host.itemTexture(imageId) : nullptr;
}

// Truncate `s` with a trailing "..." so it fits in `maxW` px on one line (keeps gallery captions
// to a single line for a clean, aligned grid; the full name is always in the tooltip).
std::string fitLabel(const std::string& s, float maxW) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    std::string out = s;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > maxW) out.pop_back();
    return out + "...";
}

// Item art is portrait-oriented (~3:4); NEVER render it square or it distorts. Given a tile
// width, the height is width * 4/3.
constexpr float kThumbW = 76.0f;
inline float thumbH(float w) { return w * 4.0f / 3.0f; }

// Is there an inventory item carrying this catalog art?
bool hasInvImage(const std::vector<InventoryItem>& inv, const std::string& imageId) {
    if (imageId.empty()) return false;
    for (const auto& it : inv) if (it.imageId == imageId) return true;
    return false;
}
// Toggle a catalog item in/out of the inventory (keyed by its baked-in imageId, so a later rename
// doesn't lose the selection). Adds with the catalog's cost as the default value.
void toggleInvByImage(std::vector<InventoryItem>& inv, const Equipment& e) {
    auto it = std::find_if(inv.begin(), inv.end(),
                           [&](const InventoryItem& x) { return x.imageId == e.imageId; });
    if (it != inv.end()) inv.erase(it);
    else inv.push_back(InventoryItem{e.name, e.description, e.imageId, "", 1, e.costGp});
}
// The character's primary weapon = the first Weapon-category catalog item present in inventory.
const Equipment* primaryWeaponEquip(const Repository& repo, const std::vector<InventoryItem>& inv) {
    for (const auto& it : inv) {
        if (it.imageId.empty()) continue;
        for (const auto& e : repo.equipment())
            if (e.category == "Weapon" && e.imageId == it.imageId) return &e;
    }
    return nullptr;
}

// One selectable 3:4 thumbnail with a highlight when `sel`, a single-line caption, and a tooltip.
// Returns true when clicked. `id` disambiguates; `tx` may be null (draws a labeled button).
bool drawSelectableThumb(const CharacterEditorHost& host, const std::string& id, SDL_Texture* tx,
                         const std::string& name, bool sel, const std::string& tip) {
    const float w = kThumbW, h = thumbH(kThumbW);
    ImGui::PushID(id.c_str());
    ImGui::BeginGroup();
    if (sel) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.62f, 0.86f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.70f, 0.93f, 1.0f));
    }
    bool clicked = tx ? ImGui::ImageButton("t", (ImTextureID)tx, ImVec2(w, h))
                      : ImGui::Button(fitLabel(name, w).c_str(), ImVec2(w, h));
    if (sel) ImGui::PopStyleColor(2);
    ImGui::TextUnformatted(fitLabel(name, w).c_str());
    ImGui::EndGroup();
    if (ImGui::IsItemHovered() && !tip.empty()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(300.0f);
        ImGui::TextUnformatted(tip.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    return clicked;
}

// How many thumbnails fit per row in the current content region.
int thumbsPerRow() {
    ImGuiStyle& st = ImGui::GetStyle();
    float cell = kThumbW + st.FramePadding.x * 2 + st.ItemSpacing.x;
    return std::max(1, (int)((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) / cell));
}

} // namespace

void initCharacterDraft(const Repository& repo, CharacterDraft& draft) {
    if (draft.kin.empty() && !repo.kins().empty()) draft.kin = repo.kins().front().name;
    if (draft.calling.empty() && !repo.callings().empty()) draft.calling = repo.callings().front().name;
}

bool characterDraftValid(const Repository& repo, const CharacterDraft& d, std::string* whyNot) {
    Traits tr{d.traitVals[0], d.traitVals[1], d.traitVals[2], d.traitVals[3]};
    if (d.name.empty()) { if (whyNot) *whyNot = "Name is required."; return false; }
    if (!validTraitSpread(tr)) { if (whyNot) *whyNot = "Assign each of +2, +1, +0, -1 once."; return false; }
    if ((int)d.trainings.size() != requiredTrainingCount(d.kin)) {
        if (whyNot) *whyNot = "Wrong number of trainings chosen."; return false;
    }
    if (d.calling == "Mystic" && d.spells.size() != 3) {
        if (whyNot) *whyNot = "A Mystic must choose 3 spells."; return false;
    }
    (void)repo;
    return true;
}

Character buildCharacter(const Repository& repo, const CharacterDraft& d) {
    Traits tr{d.traitVals[0], d.traitVals[1], d.traitVals[2], d.traitVals[3]};
    Character ch = makeCharacter(repo, d.name, d.kin, d.calling, tr, d.trainings, "No armor", false);
    ch.weaponBonus = d.weaponBonus;
    ch.armorName = d.armorName.empty() ? "No armor" : d.armorName;
    ch.shield = d.shield;
    // Equipment-driven Defense (bypasses the armor-name -> rules-table lookup so catalog names work).
    ch.defense = 10 + tr.grace + d.armorDefenseBonus + (d.shield ? d.shieldDefenseBonus : 0);
    // The combat weapon is the first Weapon-category item carried in inventory (its catalog damage
    // die drives attacks; a renamed weapon still counts, matched by baked-in imageId).
    if (const Equipment* we = primaryWeaponEquip(repo, d.inventory)) {
        ch.weaponName = we->name;
        ch.weaponDamageDie = we->damage.empty() ? "1d6" : we->damage;
    } else {
        ch.weaponName.clear();
        ch.weaponDamageDie = "1d6";
    }
    ch.spells = d.spells;
    ch.inventory = d.inventory;
    ch.playerName = d.player;
    ch.background = d.background;
    ch.goal = d.goal;
    ch.personality = d.personality;
    ch.notes = d.notes;
    ch.portraitPath = d.portraitPath;
    ch.gold = d.gold;
    return ch;
}

CharacterDraft draftFromCharacter(const Repository& repo, const Character& ch) {
    CharacterDraft d;
    d.name = ch.name; d.player = ch.playerName; d.background = ch.background; d.goal = ch.goal;
    d.personality = ch.personality; d.notes = ch.notes;
    d.kin = ch.kin; d.calling = ch.calling;
    d.traitVals[0] = ch.traits.might; d.traitVals[1] = ch.traits.grace;
    d.traitVals[2] = ch.traits.wits;  d.traitVals[3] = ch.traits.spirit;
    d.trainings = ch.trainings;
    d.spells = ch.spells;
    d.weaponBonus = ch.weaponBonus;
    d.armorName = ch.armorName;
    if (const Equipment* e = repo.equipment(ch.armorName)) d.armorDefenseBonus = e->defenseBonus;
    d.shield = ch.shield;   // shield is a simple checkbox (all shields give +1 Defense)
    d.portraitPath = ch.portraitPath;
    d.gold = ch.gold;
    d.inventory = ch.inventory;
    initCharacterDraft(repo, d);
    return d;
}

bool drawCharacterEditor(const Repository& repo, CharacterDraft& draft, const CharacterEditorHost& host) {
    bool changed = false;
    const gns::Calling* calling = repo.calling(draft.calling);
    const gns::Kin* kin = repo.kin(draft.kin);

    sectionHeader("Identity");
    ImGui::SetNextItemWidth(240); if (inputText("Name", &draft.name)) changed = true;
    ImGui::SetNextItemWidth(240); if (inputText("Background", &draft.background)) changed = true;
    ImGui::SetNextItemWidth(240); if (inputText("Goal", &draft.goal)) changed = true;
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Gold", &draft.gold)) { if (draft.gold < 0) draft.gold = 0; changed = true; }

    // Portrait grid.
    sectionHeader("Portrait");
    if (host.portraits.empty()) {
        ImGui::TextDisabled("No portraits available.");
    } else {
        const float pw = 72.0f, ph = 96.0f;
        ImGuiStyle& st = ImGui::GetStyle();
        float tileW = pw + st.FramePadding.x * 2.0f;
        int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) /
                                       (tileW + st.ItemSpacing.x)));
        int placed = 0;
        auto rowBreak = [&]() { if (placed % perRow != 0) ImGui::SameLine(); };
        rowBreak();
        if (ImGui::Button("None##port", ImVec2(pw, ph))) { draft.portraitPath.clear(); changed = true; }
        ++placed;
        for (size_t i = 0; i < host.portraits.size(); ++i) {
            rowBreak();
            bool sel = (draft.portraitPath == host.portraits[i]);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.62f, 0.86f, 1.0f));
            std::string id = "##port" + std::to_string(i);
            SDL_Texture* tex = host.portraitTexture ? host.portraitTexture(host.portraits[i]) : nullptr;
            bool clicked = tex ? ImGui::ImageButton(id.c_str(), (ImTextureID)tex, ImVec2(pw, ph))
                               : ImGui::Button((std::to_string(i + 1) + id).c_str(), ImVec2(pw, ph));
            if (clicked) { draft.portraitPath = host.portraits[i]; changed = true; }
            if (sel) ImGui::PopStyleColor();
            ++placed;
        }
    }

    sectionHeader("Kin & Calling");
    ImGui::SetNextItemWidth(240);
    { std::vector<std::string> names; for (const auto& k : repo.kins()) names.push_back(k.name);
      if (comboField("Kin", &draft.kin, names)) changed = true; }
    if (kin) ImGui::TextWrapped("Gift (%s): %s", kin->giftName.c_str(), kin->giftDescription.c_str());
    ImGui::SetNextItemWidth(240);
    { std::vector<std::string> names; for (const auto& c : repo.callings()) names.push_back(c.name);
      if (comboField("Calling", &draft.calling, names)) changed = true; }
    if (calling) {
        ImGui::TextWrapped("Gift (%s): %s", calling->giftName.c_str(), calling->giftDescription.c_str());
        ImGui::TextWrapped("Armor: %s", calling->armorAllowed.c_str());
    }

    sectionHeader("Traits  (assign +2, +1, +0, -1)");
    static const char* traitLabels[4] = {"Might", "Grace", "Wits", "Spirit"};
    static const int traitChoices[4] = {2, 1, 0, -1};
    ImGui::Columns(2, "traitCols", false);
    for (int t = 0; t < 4; ++t) {
        ImGui::PushID(t);
        ImGui::TextUnformatted(traitLabels[t]); ImGui::SameLine(70);
        ImGui::SetNextItemWidth(80);
        std::string cur = (draft.traitVals[t] >= 0 ? "+" : "") + std::to_string(draft.traitVals[t]);
        if (ImGui::BeginCombo("##v", cur.c_str())) {
            for (int ci = 0; ci < 4; ++ci) {
                std::string lbl = (traitChoices[ci] >= 0 ? "+" : "") + std::to_string(traitChoices[ci]);
                if (ImGui::Selectable(lbl.c_str(), draft.traitVals[t] == traitChoices[ci]))
                    { draft.traitVals[t] = traitChoices[ci]; changed = true; }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    Traits liveTraits{draft.traitVals[0], draft.traitVals[1], draft.traitVals[2], draft.traitVals[3]};
    if (!validTraitSpread(liveTraits))
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Assign each of +2, +1, +0, -1 exactly once.");

    sectionHeader("Training");
    int needTraining = requiredTrainingCount(draft.kin);
    ImGui::Text("Choose %d (%d selected)", needTraining, (int)draft.trainings.size());
    {
        int cols = ImGui::GetContentRegionAvail().x > 360 ? 2 : 1;
        if (cols > 1) ImGui::Columns(cols, "trainingCols", false);
        for (const auto& tr : repo.trainings()) {
            bool sel = selected(draft.trainings, tr.name);
            bool offered = calling && selected(calling->trainingOptions, tr.name);
            std::string lbl = tr.name + (offered ? "  (calling)" : "");
            if (ImGui::Checkbox(lbl.c_str(), &sel)) { toggle(draft.trainings, tr.name, sel); changed = true; }
            if (cols > 1) ImGui::NextColumn();
        }
        if (cols > 1) ImGui::Columns(1);
    }

    // Weapons: click thumbnails to add/remove (multi-select). Carried in inventory; the first is
    // the combat weapon. Like the portrait grid, the selected ones are highlighted.
    sectionHeader("Weapons  (click to add or remove; you may pick more than one)");
    {
        int perRow = thumbsPerRow();
        int placed = 0;
        for (const auto& e : repo.equipment()) {
            if (e.category != "Weapon") continue;
            if (placed++ % perRow != 0) ImGui::SameLine();
            std::string tip = e.name + "   " + std::to_string(e.costGp) + " GP   dmg " + e.damage +
                              (e.description.empty() ? "" : ("\n" + e.description));
            if (drawSelectableThumb(host, "w" + std::to_string(e.id), itemTex(host, e.imageId),
                                    e.name, hasInvImage(draft.inventory, e.imageId), tip))
                { toggleInvByImage(draft.inventory, e); changed = true; }
        }
    }
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("Weapon bonus", &draft.weaponBonus, 0, 3)) changed = true;

    // Armor: single-select (you wear one). A leading "No armor" tile clears it.
    sectionHeader("Armor  (choose one)");
    {
        int perRow = thumbsPerRow();
        int placed = 0;
        bool noneSel = draft.armorName.empty() || draft.armorName == "No armor";
        if (drawSelectableThumb(host, "arm-none", nullptr, "No armor", noneSel, "No armor"))
            { draft.armorName = "No armor"; draft.armorDefenseBonus = 0; changed = true; }
        ++placed;
        for (const auto& e : repo.equipment()) {
            if (e.category != "Armor") continue;
            if (placed++ % perRow != 0) ImGui::SameLine();
            std::string tip = e.name + "   " + std::to_string(e.costGp) + " GP   +" +
                              std::to_string(e.defenseBonus) + " Def" +
                              (e.description.empty() ? "" : ("\n" + e.description));
            if (drawSelectableThumb(host, "a" + std::to_string(e.id), itemTex(host, e.imageId),
                                    e.name, draft.armorName == e.name, tip))
                { draft.armorName = e.name; draft.armorDefenseBonus = e.defenseBonus; changed = true; }
        }
    }
    if (ImGui::Checkbox("Shield (+1 Defense)", &draft.shield)) changed = true;

    if (draft.calling == "Mystic") {
        sectionHeader("Spells  (choose 3)");
        ImGui::Text("%d selected", (int)draft.spells.size());
        int cols = ImGui::GetContentRegionAvail().x > 360 ? 2 : 1;
        if (cols > 1) ImGui::Columns(cols, "spellCols", false);
        for (const auto& sp : repo.spells()) {
            bool sel = selected(draft.spells, sp.name);
            bool atLimit = draft.spells.size() >= 3 && !sel;
            if (atLimit) ImGui::BeginDisabled();
            if (ImGui::Checkbox(sp.name.c_str(), &sel)) { toggle(draft.spells, sp.name, sel); changed = true; }
            if (atLimit) ImGui::EndDisabled();
            if (cols > 1) ImGui::NextColumn();
        }
        if (cols > 1) ImGui::Columns(1);
    }

    // Items: click thumbnails to add/remove (multi-select) - potions, gear, valuables, quest items.
    sectionHeader("Items  (click to add or remove; you may pick more than one)");
    {
        int perRow = thumbsPerRow();
        int placed = 0;
        for (const auto& e : repo.equipment()) {
            if (e.category == "Weapon" || e.category == "Armor" || e.category == "Shield") continue;
            if (placed++ % perRow != 0) ImGui::SameLine();
            std::string extra = e.effect.empty() ? e.description : e.effect;
            std::string tip = e.name + "   " + std::to_string(e.costGp) + " GP" +
                              (extra.empty() ? "" : ("\n" + extra));
            if (drawSelectableThumb(host, "it" + std::to_string(e.id), itemTex(host, e.imageId),
                                    e.name, hasInvImage(draft.inventory, e.imageId), tip))
                { toggleInvByImage(draft.inventory, e); changed = true; }
        }
    }

    // The resulting inventory (weapons + items). Each row's name / description / value / quantity is
    // editable, so a generic "Map" can be renamed "Lake Charles Map" with a custom value.
    sectionHeader("Inventory  (edit name, description, value, quantity)");
    if (draft.inventory.empty()) ImGui::TextDisabled("(nothing selected)");
    int removeItem = -1;
    for (size_t i = 0; i < draft.inventory.size(); ++i) {
        InventoryItem& it = draft.inventory[i];
        ImGui::PushID((int)i + 5000);
        if (SDL_Texture* tx = itemTex(host, it.imageId)) {
            ImGui::Image((ImTextureID)tx, ImVec2(40.0f, thumbH(40.0f))); ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(200); if (inputText("Name", &it.name)) changed = true;
        ImGui::SetNextItemWidth(200); if (inputText("Description", &it.description)) changed = true;
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("Qty", &it.quantity)) { if (it.quantity < 1) it.quantity = 1; changed = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputInt("Value (GP)", &it.value)) { if (it.value < 0) it.value = 0; changed = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeItem = (int)i;
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::Separator();
    }
    if (removeItem >= 0) { draft.inventory.erase(draft.inventory.begin() + removeItem); changed = true; }

    // Live derived preview.
    Character preview = buildCharacter(repo, draft);
    sectionHeader("Derived");
    ImGui::Text("Life %d   Defense %d", preview.maxLife, preview.defense);
    ImGui::Text("Melee +%d   Ranged +%d   Strain limit %d",
                meleeAttackBonus(preview), rangedAttackBonus(preview), strainLimit(preview));
    if (draft.calling == "Mystic") ImGui::Text("Spell cast bonus +%d", spellCastBonus(preview));

    ImGui::TextUnformatted("Notes");
    if (inputMultiline("##notes", &draft.notes, ImVec2(-1, 50))) changed = true;

    return changed;
}

} // namespace gns::ui
