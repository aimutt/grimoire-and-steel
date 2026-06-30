// Grimoire & Steel — Game Engine (M0 vertical slice).
// Proves the toolchain: SDL2 + Dear ImGui + sqlite3 + gns.db all linked & running.
#define NOMINMAX
#include <SDL.h>
#include <SDL_image.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include "gns/Database.h"
#include "gns/Content.h"
#include "gns/Module.h"
#include "gns/Repository.h"
#include "gns/Character.h"
#include "gns/CharacterIO.h"
#include "gns/Rules.h"
#include "gns/Session.h"
#include "gns/EncounterDirector.h"
#include "gns/CombatEngine.h"
#include "gns/RulesAdjudicator.h"
#include "gns/Narrator.h"
#include "gns/ui/MapRender.h"

#include "embedded_assets.h"   // generated: kEmbeddedPortraits (filename -> RCDATA id)
#include "embedded_items.h"    // generated: kItemCatalog (shop item-art baked into the binary)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

static std::string basePath() {
    char* base = SDL_GetBasePath();
    std::string p = base ? base : "";
    if (base) SDL_free(base);
    return p;
}
static std::string dbPath() { return basePath() + "data/gns.db"; }

// Native open dialog for a .gnsmod module. Returns "" if cancelled / unsupported.
static std::string openModuleDialog() {
#ifdef _WIN32
    char buf[1024] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Grimoire & Steel Module (*.gnsmod)\0*.gnsmod\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return buf;
#endif
    return "";
}

static bool isAbsolutePath(const std::string& p) {
    return p.size() > 1 && (p[1] == ':' || p[0] == '/' || p[0] == '\\');
}
static std::string dirOf(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? "" : p.substr(0, s + 1);
}
static SDL_Texture* loadImage(SDL_Renderer* r, const std::string& path) {
    if (path.empty()) return nullptr;
    SDL_Texture* tex = nullptr;
    if (SDL_Surface* s = IMG_Load(path.c_str())) {
        tex = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
    }
    return tex;
}

// Native save/open dialog for a .gnschar character. Returns "" if cancelled.
static std::string characterDialog(bool save) {
#ifdef _WIN32
    char buf[1024] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Grimoire & Steel Character (*.gnschar)\0*.gnschar\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.lpstrDefExt = "gnschar";
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&ofn)) return buf;
    } else {
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn)) return buf;
    }
#else
    (void)save;
#endif
    return "";
}

// ImGui InputText bound to a std::string (resizes on demand) — the canonical wrapper.
static int strResizeCb(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}
static bool inputText(const char* label, std::string* s,
                      ImGuiInputTextFlags flags = 0) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, s->data(), s->capacity() + 1, flags, strResizeCb, s);
}
static bool inputTextMultiline(const char* label, std::string* s, const ImVec2& size) {
    return ImGui::InputTextMultiline(label, s->data(), s->capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize, strResizeCb, s);
}

// A labeled combo over a list of names. Updates *value; returns true on change.
static bool comboField(const char* label, std::string* value,
                       const std::vector<std::string>& options) {
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

// Which armors a calling may wear (interpreting Calling::armorAllowed text). "No armor"
// is always allowed; "light" -> also Light armor; "Any" -> all. Excludes the Shield row.
static std::vector<std::string> allowedArmors(const gns::Calling* c,
                                              const gns::Repository& repo) {
    const std::string allowed = c ? c->armorAllowed : "Any armor";
    bool any = allowed.find("Any") != std::string::npos ||
               allowed.find("any") != std::string::npos;
    bool light = any || allowed.find("ight") != std::string::npos;   // "light"/"Light"
    std::vector<std::string> out;
    for (const auto& a : repo.armors()) {
        if (a.name == "Shield") continue;
        if (a.name == "No armor") out.push_back(a.name);
        else if (a.name == "Light armor") { if (light) out.push_back(a.name); }
        else if (any) out.push_back(a.name);   // Mail, Plate
    }
    return out;
}
static bool callingAllowsShield(const gns::Calling* c) {
    const std::string allowed = c ? c->armorAllowed : "Any armor";
    return allowed.find("Any") != std::string::npos ||
           allowed.find("ight") != std::string::npos;   // not Mystic ("No armor")
}

// Best shop discount % a buyer gets, parsed from a shop area's DM notes. Recognizes tags
// like "[discount 20%]" (everyone) and "[discount Dwarf 50%]" / "[discount Mystic 10%]"
// (applies when the word matches the buyer's kin or calling). Returns the largest match.
static int parseDiscountPct(const std::string& dm, const std::string& kin, const std::string& calling) {
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };
    std::string low = lower(dm), kinl = lower(kin), calll = lower(calling);
    int best = 0;
    for (size_t pos = 0; (pos = low.find("[discount", pos)) != std::string::npos; ) {
        size_t end = low.find(']', pos);
        if (end == std::string::npos) break;
        std::string body = low.substr(pos + 9, end - (pos + 9));   // text after "[discount"
        pos = end + 1;
        int pct = 0; bool any = false;
        for (size_t i = 0; i < body.size(); ++i)
            if (std::isdigit((unsigned char)body[i])) {
                while (i < body.size() && std::isdigit((unsigned char)body[i])) { pct = pct * 10 + (body[i] - '0'); ++i; any = true; }
                break;
            }
        std::string tgt;
        for (size_t i = 0; i < body.size(); ++i)
            if (std::isalpha((unsigned char)body[i])) {
                size_t j = i; while (j < body.size() && std::isalpha((unsigned char)body[j])) ++j;
                tgt = body.substr(i, j - i); break;
            }
        bool applies = tgt.empty() || tgt == kinl || tgt == calll;
        if (any && applies && pct > best) best = pct > 100 ? 100 : pct;
    }
    return best;
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);   // cover-art splash images

    SDL_Window* window = SDL_CreateWindow(
        "Grimoire & Steel — Game Engine (M0)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Roomier, modern style — more breathing room and left padding than the ImGui defaults.
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding  = ImVec2(16, 14);
        style.FramePadding   = ImVec2(8, 5);
        style.ItemSpacing    = ImVec2(10, 8);
        style.FrameRounding  = 4.0f;
        style.WindowRounding = 4.0f;
    }

    // Load a real proportional font (Segoe UI) at a body + larger heading size, replacing
    // ImGui's blocky bitmap default. headingFont stays null if the semibold TTF is missing.
    ImFont* headingFont = nullptr;
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* bodyPath = "C:/Windows/Fonts/segoeui.ttf";
        const char* headPath = "C:/Windows/Fonts/seguisb.ttf";   // Segoe UI Semibold
        if (std::filesystem::exists(bodyPath)) io.Fonts->AddFontFromFileTTF(bodyPath, 18.0f);
        else                                   io.Fonts->AddFontDefault();
        if (std::filesystem::exists(headPath)) headingFont = io.Fonts->AddFontFromFileTTF(headPath, 23.0f);
    }

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // Load the rules database (read-only).
    std::unique_ptr<gns::Database> db;
    std::unique_ptr<gns::Repository> repo;
    std::string dbErr;
    int monsterCount = 0, spellCount = 0;
    std::vector<gns::MonsterBrief> monsters;
    std::vector<gns::SpellBrief> spells;
    try {
        db = std::make_unique<gns::Database>(dbPath());
        monsterCount = gns::countRows(*db, "monster");
        spellCount = gns::countRows(*db, "spell");
        monsters = gns::topMonsters(*db, 25);
        spells = gns::allSpells(*db);
        repo = std::make_unique<gns::Repository>(*db);
    } catch (const std::exception& e) {
        dbErr = e.what();
    }

    // ---- Top-level UI mode ----
    enum class Mode { Browser, Characters, Play };
    Mode mode = repo ? Mode::Characters : Mode::Browser;

    // Lists for the creation combos (built once from the repository).
    std::vector<std::string> kinNames, callingNames, trainingNames, weaponCatNames, spellNames;
    if (repo) {
        for (const auto& k : repo->kins()) kinNames.push_back(k.name);
        for (const auto& c : repo->callings()) callingNames.push_back(c.name);
        for (const auto& t : repo->trainings()) trainingNames.push_back(t.name);
        for (const auto& w : repo->weaponCategories()) weaponCatNames.push_back(w.name);
        for (const auto& s : repo->spells()) spellNames.push_back(s.name);
    }

    // In-progress character selections.
    struct Draft {
        std::string name, player, background, goal, personality, notes;
        std::string kin, calling;
        int traitVals[4] = {2, 1, 0, -1};   // Might, Grace, Wits, Spirit
        std::vector<char> trainingSel;       // parallel to trainingNames
        std::vector<char> spellSel;          // parallel to spellNames
        std::string weaponCat, weaponName, armorName = "No armor";
        int weaponBonus = 0;
        bool shield = false;
        std::string portraitPath;   // chosen avatar filename ("" = placeholder)
        int gold = 100;             // starting gold (editable)
    } draft;
    draft.trainingSel.assign(trainingNames.size(), 0);
    draft.spellSel.assign(spellNames.size(), 0);
    if (!kinNames.empty()) draft.kin = kinNames.front();
    if (!callingNames.empty()) draft.calling = callingNames.front();
    if (!weaponCatNames.empty()) {
        draft.weaponCat = weaponCatNames.front();
        if (const gns::WeaponCategory* wc = repo->weaponCategory(draft.weaponCat))
            draft.weaponName = wc->name;
    }
    std::string charStatus;
    std::vector<gns::Character> roster;
    int defaultPartyCount = 3;   // Quick Start party size (1-5)

    // Loaded adventure module + its cover-art splash.
    gns::Module mod;
    bool haveModule = false;
    std::string moduleStatus;
    std::string moduleDir;                              // folder of the open .gnsmod (for relative art)
    SDL_Texture* coverTex = nullptr;
    bool showSplash = false;
    Uint32 splashUntil = 0;   // module cover-art splash auto-dismisses at this tick

    // Cache of area/other artwork textures by resolved path (nullptr = failed, don't retry).
    std::map<std::string, SDL_Texture*> imgCache;
    auto clearImgCache = [&]() {
        for (auto& kv : imgCache) if (kv.second) SDL_DestroyTexture(kv.second);
        imgCache.clear();
    };
    // Load (and cache) an image, resolving relative paths against the module folder.
    auto loadCachedImage = [&](const std::string& path) -> SDL_Texture* {
        if (path.empty()) return nullptr;
        auto it = imgCache.find(path);
        if (it != imgCache.end()) return it->second;
        std::string full = isAbsolutePath(path) ? path : moduleDir + path;
        SDL_Texture* tex = loadImage(renderer, full);
        imgCache[path] = tex;
        return tex;
    };

    // Character portraits + the startup splash are baked into the executable as RCDATA
    // resources (see engine/CMakeLists.txt). Map portrait filename -> resource id.
    std::vector<std::string> portraitFiles;
    std::map<std::string, std::string> portraitRes;
    for (const auto& p : kEmbeddedPortraits) {
        portraitFiles.push_back(p.file);
        portraitRes[p.file] = p.res;
    }
    // Load (and cache) a texture from an embedded RCDATA resource by name.
    auto loadResourceTexture = [&](const std::string& resName) -> SDL_Texture* {
        std::string key = "res:" + resName;
        auto it = imgCache.find(key);
        if (it != imgCache.end()) return it->second;
        SDL_Texture* tex = nullptr;
#ifdef _WIN32
        if (HRSRC h = FindResourceA(nullptr, resName.c_str(), reinterpret_cast<LPCSTR>(RT_RCDATA))) {
            HGLOBAL g = LoadResource(nullptr, h);
            void* data = g ? LockResource(g) : nullptr;
            DWORD sz = SizeofResource(nullptr, h);
            if (data && sz) {
                SDL_RWops* rw = SDL_RWFromConstMem(data, (int)sz);
                if (SDL_Surface* surf = IMG_Load_RW(rw, 1)) {
                    tex = SDL_CreateTextureFromSurface(renderer, surf);
                    SDL_FreeSurface(surf);
                }
            }
        }
#endif
        imgCache[key] = tex;   // cache (incl. nullptr) so failures aren't retried
        return tex;
    };
    // Texture for a portrait filename via its embedded resource.
    auto portraitTexture = [&](const std::string& file) -> SDL_Texture* {
        auto it = portraitRes.find(file);
        return it == portraitRes.end() ? nullptr : loadResourceTexture(it->second);
    };
    // A section heading: padded above/below and drawn in the larger heading font.
    auto sectionHeader = [&](const char* label) {
        ImGui::Spacing();
        if (headingFont) ImGui::PushFont(headingFont);
        ImGui::SeparatorText(label);
        if (headingFont) ImGui::PopFont();
        ImGui::Spacing();
    };
    // Texture for a shop item's catalog id (filename) via its embedded resource.
    auto itemTexture = [&](const std::string& file) -> SDL_Texture* {
        if (file.empty()) return nullptr;
        for (const auto& ia : kItemCatalog)
            if (file == ia.file) return loadResourceTexture(ia.res);
        return nullptr;
    };
    // Best texture for a shop item: the baked-in catalog by id, else by the free-file's
    // basename (authors who used Browse still get the embedded art), else the free file itself.
    auto shopItemTexture = [&](const gns::ShopItem& it) -> SDL_Texture* {
        if (SDL_Texture* t = itemTexture(it.imageId)) return t;
        if (!it.imagePath.empty()) {
            std::string base = it.imagePath;
            size_t s = base.find_last_of("/\\");
            if (s != std::string::npos) base = base.substr(s + 1);
            if (SDL_Texture* t = itemTexture(base)) return t;
            return loadCachedImage(it.imagePath);
        }
        return nullptr;
    };

    auto openModule = [&](const std::string& path) {
        try {
            mod = gns::loadModule(path);
            haveModule = true;
            moduleDir = dirOf(path);
            clearImgCache();   // new module -> drop stale area textures
            if (coverTex) { SDL_DestroyTexture(coverTex); coverTex = nullptr; }
            if (!mod.coverArtPath.empty()) {
                std::string full = isAbsolutePath(mod.coverArtPath)
                                       ? mod.coverArtPath : moduleDir + mod.coverArtPath;
                coverTex = loadImage(renderer, full);
            }
            moduleStatus = "Loaded: " + (mod.name.empty() ? "(untitled)" : mod.name);
            showSplash = true;   // show the cover (or a title card) whenever a game loads
            splashUntil = SDL_GetTicks() + 3000;   // ...for ~3s, then auto-dismiss
        } catch (const std::exception& e) {
            haveModule = false;
            moduleStatus = std::string("Open failed: ") + e.what();
        }
    };

    // ---- Play state (Play mode) ----
    std::unique_ptr<gns::Session> session;   // null until an adventure starts
    std::vector<std::string> journal;
    std::string playStatus;
    int cursorX = 0, cursorY = 0;            // party token position, in map cells
    int faceX = 0, faceY = 1;                 // facing direction (default south)
    int shopBuyer = 0;                        // which party member is buying/selling
    bool areaView = false;                    // left region shows the area view, not the map
    int pendingBuy = -1;                      // shopItems index awaiting purchase confirmation
    int pendingSell = -1;                     // buyer inventory index awaiting sell confirmation
    int confirmChoice = 0;                    // confirm-popup highlight (0 = No, 1 = Yes)

    auto areaLabel = [](const gns::Area* a) -> std::string {
        if (!a) return "Unknown";
        if (!a->name.empty()) return a->name;
        return a->label.empty() ? "Unknown area" : a->label;
    };

    // Run the "beat" for entering an area: seat there, narrate, pick up Control Points
    // located here, resolve any encounter, and run trap/lock/hidden checks.
    auto enterArea = [&](int areaId) {
        if (!session || !repo) return;
        session->state().areaId = areaId;
        const gns::Area* a = session->currentArea();
        journal.push_back("== " + areaLabel(a) + " ==");
        if (a && !a->playerText.empty()) journal.push_back(a->playerText);
        else journal.push_back("You arrive at " + areaLabel(a) + ".");

        // Control Points (objectives) sitting in this area complete automatically;
        // Control Items (kind 1) are picked up by clicking their marker instead.
        for (const auto& cp : mod.controlPoints)
            if (cp.kind == 0 && cp.areaId == areaId && session->completeControlPoint(cp.id))
                journal.push_back("Objective reached: " + cp.name);

        if (a && a->monsterChancePct > 0) {
            gns::EncounterDirector dir(*repo, session->dice());
            gns::Encounter enc = dir.checkArea(*a);
            if (enc.occurred) {
                journal.push_back("A fight breaks out!");
                gns::CombatEngine combat(*repo, session->dice());
                gns::CombatResult r = combat.run(session->party(), enc);
                for (const auto& line : r.log) journal.push_back(line);
                journal.push_back(r.outcome == gns::CombatOutcome::PartyVictory
                    ? ("Victory! The party gains " + std::to_string(r.apAwarded) + " AP.")
                    : "The party has fallen...");
            }
        }
        if (a) {
            gns::RulesAdjudicator adj(*repo, session->dice());
            auto tc = adj.trapCheck(*a);   if (tc.occurred) journal.push_back("Trap! " + tc.description);
            auto lc = adj.lockCheck(*a);   if (lc.occurred) journal.push_back(lc.description);
            auto hc = adj.hiddenCheck(*a); if (hc.occurred) journal.push_back("You find: " + hc.description);
        }
    };

    // Generate a varied default party of `n` (1-5): distinct names, callings, traits, and
    // portraits. Replaces the roster (Quick Start = pregenerated band).
    auto quickStartParty = [&](int n) {
        if (!repo) return;
        struct Pre {
            const char* name; const char* kin; const char* calling;
            gns::Traits traits; std::vector<std::string> trainings;
            const char* armor; bool shield; const char* weapon; const char* die;
            std::vector<std::string> spells;
        };
        const Pre pres[5] = {
            {"Bram", "Human", "Blade",  {2,1,0,-1}, {"Blades","Shields","Survival","Lore"},
                "Mail armor", true,  "Short sword", "1d6", {}},
            {"Mira", "Elf", "Mystic",   {-1,0,1,2}, {"Sorcery","Lore","Healing"},
                "No armor", false, "Staff", "1d6", {"Flame","Heal","Ward"}},
            {"Dax", "Halfling", "Shadow", {0,2,1,-1}, {"Stealth","Locks","Survival"},
                "Light armor", false, "Dagger", "1d4", {}},
            {"Lyra", "Human", "Sage",   {-1,0,2,1}, {"Lore","Healing","Persuasion","Crafting"},
                "Light armor", false, "Short sword", "1d6", {}},
            {"Orin", "Dwarf", "Blade",  {2,0,-1,1}, {"Axes","Shields","Survival"},
                "Mail armor", true,  "Hand axe", "1d6", {}},
        };
        roster.clear();
        for (int i = 0; i < n; ++i) {
            const Pre& p = pres[i % 5];
            gns::Character c = gns::makeCharacter(*repo, p.name, p.kin, p.calling, p.traits,
                                                  p.trainings, p.armor, p.shield);
            c.weaponName = p.weapon;
            c.weaponDamageDie = p.die;
            c.spells = p.spells;
            if (!portraitFiles.empty()) c.portraitPath = portraitFiles[i % portraitFiles.size()];
            roster.push_back(std::move(c));
        }
    };

    auto startAdventure = [&]() {
        if (!repo || !haveModule || roster.empty()) return;
        gns::Party party;
        party.members = roster;   // the roster is the party
        session = std::make_unique<gns::Session>(mod, party, /*seed=*/1234u);
        journal.clear();
        playStatus.clear();
        areaView = false;
        if (const gns::Area* a = session->currentArea()) {
            // Seat the party token on the start area's centroid.
            if (const gns::Map* sm = session->currentMap()) {
                int ax, ay; gns::ui::areaCentroid(*sm, a->id, ax, ay);
                cursorX = ax >= 0 ? ax : 0;
                cursorY = ay >= 0 ? ay : 0;
            }
            enterArea(a->id);
        }
    };

    // Startup splash: the baked-in G&S splash, shown briefly on launch (skippable).
    SDL_Texture* startupSplashTex = loadResourceTexture("GNSSPLASH");
    Uint32 startupSplashUntil = SDL_GetTicks() + 2500;
    bool startupSplashDone = (startupSplashTex == nullptr);

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                ev.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- Startup splash: the baked-in G&S logo, shown for a couple seconds on launch ---
        if (!startupSplashDone) {
            bool skip = SDL_GetTicks() >= startupSplashUntil ||
                        ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                        ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (skip) {
                startupSplashDone = true;
            } else {
                const ImGuiViewport* vp = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(vp->WorkPos);
                ImGui::SetNextWindowSize(vp->WorkSize);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.03f, 0.05f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::Begin("##startupsplash", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoScrollbar);
                ImVec2 win = ImGui::GetWindowSize();
                int tw = 0, th = 0;
                SDL_QueryTexture(startupSplashTex, nullptr, nullptr, &tw, &th);
                float scale = (tw > 0 && th > 0) ? std::min(win.x / tw, win.y / th) : 1.0f;
                ImVec2 sz(tw * scale, th * scale);
                ImGui::SetCursorPos(ImVec2((win.x - sz.x) * 0.5f, (win.y - sz.y) * 0.5f));
                ImGui::Image((ImTextureID)startupSplashTex, sz);
                ImGui::End();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                ImGui::Render();
                SDL_SetRenderDrawColor(renderer, 10, 8, 12, 255);
                SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
                SDL_RenderPresent(renderer);
                continue;   // hold on the splash
            }
        }

        // --- Cover-art splash: shown full-window when a module loads, until dismissed ---
        if (showSplash) {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.03f, 0.05f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("##splash", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                         ImGuiWindowFlags_NoScrollbar);
            ImVec2 win = ImGui::GetWindowSize();
            if (coverTex) {
                int tw = 0, th = 0;
                SDL_QueryTexture(coverTex, nullptr, nullptr, &tw, &th);
                float scale = std::min(win.x * 0.9f / tw, (win.y - 70.0f) / th);
                if (scale < 0.0f) scale = 0.0f;
                ImVec2 sz(tw * scale, th * scale);
                ImGui::SetCursorPos(ImVec2((win.x - sz.x) * 0.5f, (win.y - sz.y - 48.0f) * 0.5f));
                ImGui::Image((ImTextureID)coverTex, sz);
            } else {
                ImGui::SetCursorPosY(win.y * 0.42f);   // no cover image: plain title card
            }
            std::string cap = mod.name.empty() ? "Untitled Module" : mod.name;
            ImVec2 ts = ImGui::CalcTextSize(cap.c_str());
            ImGui::SetCursorPosX((win.x - ts.x) * 0.5f);
            ImGui::TextUnformatted(cap.c_str());
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            // Auto-dismiss after ~2s (like the startup splash); a key/click skips it early.
            if (SDL_GetTicks() >= splashUntil ||
                ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                showSplash = false;

            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 10, 8, 12, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
            continue;   // skip the rest of the UI while the splash is up
        }

        // --- Menu bar: open a module (shows its cover), re-show the cover, or exit ---
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open Module\xE2\x80\xA6")) {
                    std::string p = openModuleDialog();
                    if (!p.empty()) openModule(p);
                }
                if (ImGui::MenuItem("Show Cover", nullptr, false, haveModule)) {
                    showSplash = true; splashUntil = SDL_GetTicks() + 3000;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Play", nullptr, mode == Mode::Play, repo != nullptr))
                    mode = Mode::Play;
                if (ImGui::MenuItem("Characters", nullptr, mode == Mode::Characters, repo != nullptr))
                    mode = Mode::Characters;
                if (ImGui::MenuItem("Reference Browser", nullptr, mode == Mode::Browser))
                    mode = Mode::Browser;
                ImGui::EndMenu();
            }
            if (!moduleStatus.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", moduleStatus.c_str());
            }
            ImGui::EndMainMenuBar();
        }

      if (mode == Mode::Browser) {
        ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 690), ImGuiCond_FirstUseEver);
        ImGui::Begin("Grimoire & Steel — gns.db");
        if (!dbErr.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "DB error: %s", dbErr.c_str());
        } else {
            ImGui::Text("Database loaded.");
            ImGui::BulletText("monsters: %d", monsterCount);
            ImGui::BulletText("spells:   %d", spellCount);
            ImGui::Separator();
            ImGui::TextUnformatted("First 25 monsters:");
            if (ImGui::BeginTable("monsters", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Life");
                ImGui::TableSetupColumn("Defense");
                ImGui::TableSetupColumn("Damage");
                ImGui::TableHeadersRow();
                for (auto& m : monsters) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(m.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%d", m.life);
                    ImGui::TableNextColumn(); ImGui::Text("%d", m.defense);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(m.damage.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(440, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(640, 690), ImGuiCond_FirstUseEver);
        ImGui::Begin("Spells");
        if (dbErr.empty() && ImGui::BeginTable("spells", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Challenge");
            ImGui::TableSetupColumn("Combat effect");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (auto& s : spells) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableNextColumn();
                if (s.challengeNumber > 0) ImGui::Text("%d", s.challengeNumber);
                else ImGui::TextUnformatted("-");
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.combatEffect.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::End();
      } else if (mode == Mode::Characters) {
        // ===================== Characters mode =====================
        const gns::Kin* kin = repo ? repo->kin(draft.kin) : nullptr;
        const gns::Calling* calling = repo ? repo->calling(draft.calling) : nullptr;

        // Keep weaponName/damage-die in sync with the chosen weapon category.
        const gns::WeaponCategory* wcat = repo ? repo->weaponCategory(draft.weaponCat) : nullptr;

        // Build the live character from the current selections (reuses the rules engine).
        gns::Traits tr;
        tr.might  = draft.traitVals[0];
        tr.grace  = draft.traitVals[1];
        tr.wits   = draft.traitVals[2];
        tr.spirit = draft.traitVals[3];
        std::vector<std::string> chosenTrainings;
        for (size_t i = 0; i < trainingNames.size(); ++i)
            if (draft.trainingSel[i]) chosenTrainings.push_back(trainingNames[i]);
        std::vector<std::string> chosenSpells;
        for (size_t i = 0; i < spellNames.size(); ++i)
            if (draft.spellSel[i]) chosenSpells.push_back(spellNames[i]);

        gns::Character ch = repo
            ? gns::makeCharacter(*repo, draft.name, draft.kin, draft.calling, tr,
                                 chosenTrainings, draft.armorName, draft.shield)
            : gns::Character{};
        ch.weaponName = draft.weaponName;
        ch.weaponDamageDie = wcat ? wcat->damageDie : "1d6";
        ch.weaponBonus = draft.weaponBonus;
        ch.spells = chosenSpells;
        ch.playerName = draft.player;
        ch.background = draft.background;
        ch.goal = draft.goal;
        ch.personality = draft.personality;
        ch.notes = draft.notes;
        ch.portraitPath = draft.portraitPath;
        ch.gold = draft.gold;

        const bool isMystic = (draft.calling == "Mystic");
        const int needTraining = gns::requiredTrainingCount(draft.kin);
        const bool traitsOk = gns::validTraitSpread(tr);
        const bool trainingOk = (int)chosenTrainings.size() == needTraining;
        const bool spellsOk = !isMystic || chosenSpells.size() == 3;
        const bool canSave = repo && !draft.name.empty() && traitsOk && trainingOk && spellsOk;

        // Responsive two-pane layout: Create Character fills the left ~66%, Roster the rest.
        // Sized from the viewport every frame so it always fills the window (and ignores any
        // stale size saved in imgui.ini).
        const ImGuiViewport* charVp = ImGui::GetMainViewport();
        const float rosterW = std::max(320.0f, charVp->WorkSize.x * 0.34f);
        const float createW = charVp->WorkSize.x - rosterW;
        const ImGuiWindowFlags paneFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::SetNextWindowPos(charVp->WorkPos);
        ImGui::SetNextWindowSize(ImVec2(createW, charVp->WorkSize.y));
        ImGui::Begin("Create Character", nullptr, paneFlags);
        if (!repo) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "DB error: %s", dbErr.c_str());
        } else {
            const float kFieldW = 240.0f;   // shared width for inputs/combos with short content
            sectionHeader("Identity");
            ImGui::SetNextItemWidth(kFieldW); inputText("Name", &draft.name);
            ImGui::SetNextItemWidth(kFieldW); inputText("Background", &draft.background);
            ImGui::SetNextItemWidth(kFieldW); inputText("Goal", &draft.goal);
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt("Gold", &draft.gold) && draft.gold < 0) draft.gold = 0;

            sectionHeader("Portrait");
            if (portraitFiles.empty()) {
                ImGui::TextDisabled("No portraits found in assets/portraits.");
            } else {
                // 3:4 portrait boxes (matches the source images, so no stretching).
                const float pw = 92.0f, ph = 122.0f;
                ImGuiStyle& st = ImGui::GetStyle();
                float availW = ImGui::GetContentRegionAvail().x;
                int perRow = std::max(1, (int)((availW + st.ItemSpacing.x) / (pw + st.ItemSpacing.x)));
                int placed = 0;
                auto rowBreak = [&]() { if (placed % perRow != 0) ImGui::SameLine(); };
                rowBreak();
                if (ImGui::Button("None##port", ImVec2(pw, ph))) draft.portraitPath.clear();
                ++placed;
                for (size_t i = 0; i < portraitFiles.size(); ++i) {
                    rowBreak();
                    bool sel = (draft.portraitPath == portraitFiles[i]);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.62f, 0.86f, 1.0f));
                    std::string id = "##port" + std::to_string(i);
                    SDL_Texture* tex = portraitTexture(portraitFiles[i]);
                    bool clicked = tex
                        ? ImGui::ImageButton(id.c_str(), (ImTextureID)tex, ImVec2(pw, ph))
                        : ImGui::Button((std::to_string(i + 1) + id).c_str(), ImVec2(pw, ph));
                    if (clicked) draft.portraitPath = portraitFiles[i];
                    if (sel) ImGui::PopStyleColor();
                    ++placed;
                }
            }

            sectionHeader("Kin & Calling");
            ImGui::SetNextItemWidth(kFieldW);
            comboField("Kin", &draft.kin, kinNames);
            if (kin) ImGui::TextWrapped("Gift (%s): %s", kin->giftName.c_str(),
                                        kin->giftDescription.c_str());
            ImGui::SetNextItemWidth(kFieldW);
            comboField("Calling", &draft.calling, callingNames);
            if (calling) {
                ImGui::TextWrapped("Gift (%s): %s", calling->giftName.c_str(),
                                   calling->giftDescription.c_str());
                ImGui::TextWrapped("Armor: %s", calling->armorAllowed.c_str());
                ImGui::TextWrapped("Starting gear: %s", calling->startingGear.c_str());
            }

            sectionHeader("Traits  (assign +2, +1, +0, -1)");
            static const char* traitLabels[4] = {"Might", "Grace", "Wits", "Spirit"};
            static const int traitChoices[4] = {2, 1, 0, -1};
            ImGui::Columns(2, "traitCols", false);
            for (int t = 0; t < 4; ++t) {
                ImGui::PushID(t);
                ImGui::TextUnformatted(traitLabels[t]); ImGui::SameLine(70);
                ImGui::SetNextItemWidth(80);
                std::string cur = (draft.traitVals[t] >= 0 ? "+" : "") +
                                  std::to_string(draft.traitVals[t]);
                if (ImGui::BeginCombo("##v", cur.c_str())) {
                    for (int ci = 0; ci < 4; ++ci) {
                        std::string lbl = (traitChoices[ci] >= 0 ? "+" : "") +
                                          std::to_string(traitChoices[ci]);
                        if (ImGui::Selectable(lbl.c_str(), draft.traitVals[t] == traitChoices[ci]))
                            draft.traitVals[t] = traitChoices[ci];
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
            if (!traitsOk)
                ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
                                   "Assign each of +2, +1, +0, -1 exactly once.");

            sectionHeader("Training");
            ImGui::Text("Choose %d (%d selected)", needTraining, (int)chosenTrainings.size());
            {
                int cols = ImGui::GetContentRegionAvail().x > 360 ? 2 : 1;
                if (cols > 1) ImGui::Columns(cols, "trainingCols", false);
                for (size_t i = 0; i < trainingNames.size(); ++i) {
                    bool sel = draft.trainingSel[i] != 0;
                    bool offered = calling &&
                        std::find(calling->trainingOptions.begin(), calling->trainingOptions.end(),
                                  trainingNames[i]) != calling->trainingOptions.end();
                    std::string lbl = trainingNames[i] + (offered ? "  (calling)" : "");
                    if (ImGui::Checkbox(lbl.c_str(), &sel)) draft.trainingSel[i] = sel ? 1 : 0;
                    if (cols > 1) ImGui::NextColumn();
                }
                if (cols > 1) ImGui::Columns(1);
            }

            sectionHeader("Equipment");
            ImGui::SetNextItemWidth(kFieldW);
            if (comboField("Weapon type", &draft.weaponCat, weaponCatNames)) {
                if (const gns::WeaponCategory* w = repo->weaponCategory(draft.weaponCat))
                    draft.weaponName = w->name;   // seed a sensible default name
            }
            ImGui::SetNextItemWidth(kFieldW); inputText("Main attack", &draft.weaponName);
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Weapon bonus", &draft.weaponBonus, 0, 3);
            std::vector<std::string> armorOpts = allowedArmors(calling, *repo);
            // Snap armor to an allowed option if the calling forbids the current pick.
            if (std::find(armorOpts.begin(), armorOpts.end(), draft.armorName) == armorOpts.end()
                && !armorOpts.empty())
                draft.armorName = armorOpts.front();
            ImGui::SetNextItemWidth(kFieldW);
            comboField("Armor", &draft.armorName, armorOpts);
            if (callingAllowsShield(calling)) ImGui::Checkbox("Shield (+1 Defense)", &draft.shield);
            else draft.shield = false;

            if (isMystic) {
                sectionHeader("Spells  (choose 3)");
                ImGui::Text("%d selected", (int)chosenSpells.size());
                int cols = ImGui::GetContentRegionAvail().x > 360 ? 2 : 1;
                if (cols > 1) ImGui::Columns(cols, "spellCols", false);
                for (size_t i = 0; i < spellNames.size(); ++i) {
                    bool sel = draft.spellSel[i] != 0;
                    bool atLimit = chosenSpells.size() >= 3 && !sel;
                    if (atLimit) ImGui::BeginDisabled();
                    if (ImGui::Checkbox(spellNames[i].c_str(), &sel))
                        draft.spellSel[i] = sel ? 1 : 0;
                    if (atLimit) ImGui::EndDisabled();
                    if (cols > 1) ImGui::NextColumn();
                }
                if (cols > 1) ImGui::Columns(1);
            }

            sectionHeader("Derived");
            ImGui::Text("Life %d   Defense %d", ch.maxLife, ch.defense);
            ImGui::Text("Melee +%d   Ranged +%d   Strain limit %d",
                        gns::meleeAttackBonus(ch), gns::rangedAttackBonus(ch),
                        gns::strainLimit(ch));
            if (isMystic) ImGui::Text("Spell cast bonus +%d", gns::spellCastBonus(ch));

            inputTextMultiline("Notes", &draft.notes, ImVec2(-1, 50));

            ImGui::Separator();
            if (!canSave) ImGui::BeginDisabled();
            if (ImGui::Button("Save Character\xE2\x80\xA6")) {
                std::string p = characterDialog(/*save=*/true);
                if (!p.empty()) {
                    try { gns::saveCharacter(ch, p); charStatus = "Saved: " + ch.name; }
                    catch (const std::exception& e) { charStatus = std::string("Save failed: ") + e.what(); }
                }
            }
            if (!canSave) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { draft = Draft{};
                draft.trainingSel.assign(trainingNames.size(), 0);
                draft.spellSel.assign(spellNames.size(), 0);
                if (!kinNames.empty()) draft.kin = kinNames.front();
                if (!callingNames.empty()) draft.calling = callingNames.front();
                if (!weaponCatNames.empty()) {
                    draft.weaponCat = weaponCatNames.front();
                    if (const gns::WeaponCategory* w = repo->weaponCategory(draft.weaponCat))
                        draft.weaponName = w->name;
                }
            }
            if (!charStatus.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", charStatus.c_str()); }
        }
        ImGui::End();

        // ----- Roster (loaded characters; the future party) — right pane -----
        ImGui::SetNextWindowPos(ImVec2(charVp->WorkPos.x + createW, charVp->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(rosterW, charVp->WorkSize.y));
        ImGui::Begin("Roster / Party", nullptr, paneFlags);
        if (ImGui::Button("Load Character\xE2\x80\xA6")) {
            std::string p = characterDialog(/*save=*/false);
            if (!p.empty()) {
                try { roster.push_back(gns::loadCharacter(p)); charStatus = "Loaded a character."; }
                catch (const std::exception& e) { charStatus = std::string("Load failed: ") + e.what(); }
            }
        }
        ImGui::Separator();
        if (roster.empty()) {
            ImGui::TextDisabled("No characters loaded. Create one, save it, then load it here.");
        } else if (ImGui::BeginTable("roster", 6,
                       ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kin");
            ImGui::TableSetupColumn("Calling");
            ImGui::TableSetupColumn("Life");
            ImGui::TableSetupColumn("Def");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            int removeAt = -1;
            for (size_t i = 0; i < roster.size(); ++i) {
                const gns::Character& c = roster[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.name.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.kin.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.calling.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%d/%d", c.life, c.maxLife);
                ImGui::TableNextColumn(); ImGui::Text("%d", c.defense);
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("Remove")) removeAt = (int)i;
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (removeAt >= 0) roster.erase(roster.begin() + removeAt);
        }
        ImGui::End();
      } else {
        // ===================== Play mode =====================
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 wp = vp->WorkPos, ws = vp->WorkSize;
        const float rightW = 600.0f;
        const ImGuiWindowFlags pf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

        // ---- Shared Play HUD lambdas (used by both the left region and the right panel) ----
        // Placeholder avatar colour derived from the character's name.
        auto nameColor = [](const std::string& s) -> ImU32 {
            unsigned h = 2166136261u;
            for (char c : s) { h ^= (unsigned char)c; h *= 16777619u; }
            return IM_COL32(70 + (h & 0x7F), 70 + ((h >> 8) & 0x7F), 70 + ((h >> 16) & 0x7F), 255);
        };
        // Avatar of width `w`; the box is 3:4 portrait (matches the source images, so no
        // stretching). Draws the portrait texture if available, else a name-hashed placeholder.
        auto drawAvatar = [&](const gns::Character& pc, float w) {
            const float h = w * 4.0f / 3.0f;
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 q(p.x + w, p.y + h);
            SDL_Texture* tex = portraitTexture(pc.portraitPath);
            if (tex) {
                pdl->AddImageRounded((ImTextureID)tex, p, q,
                                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
            } else {
                pdl->AddRectFilled(p, q, nameColor(pc.name), 6.0f);
                std::string initial(1, pc.name.empty() ? '?' :
                                    (char)std::toupper((unsigned char)pc.name[0]));
                ImVec2 ts = ImGui::CalcTextSize(initial.c_str());
                pdl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f),
                             IM_COL32(255, 255, 255, 255), initial.c_str());
            }
            pdl->AddRect(p, q, IM_COL32(20, 16, 24, 255), 6.0f, 0, 2.0f);
            ImGui::Dummy(ImVec2(w, h));
        };
        // One card per party member: placeholder avatar + name / kin·calling / Life·Def·AP.
        auto drawPartyCards = [&](const std::vector<gns::Character>& members) {
            const float av = 44.0f;
            for (const auto& pc : members) {
                drawAvatar(pc, av);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(pc.name.empty() ? "(unnamed)" : pc.name.c_str());
                ImGui::TextDisabled("%s %s  \xC2\xB7  Lv %d", pc.kin.c_str(), pc.calling.c_str(), pc.level);
                ImGui::Text("Life %d/%d   Def %d   AP %d   %d gp",
                            pc.life, pc.maxLife, pc.defense, pc.ap, pc.gold);
                ImGui::EndGroup();
                ImGui::Spacing();
            }
        };
        // Draw an area's artwork scaled to the column (capped at `maxH`). Chooses the image
        // matching the party's facing direction, else the area's default image, else legacy art.
        auto drawAreaImage = [&](const gns::Area* a, float maxW, float maxH) {
            if (!a) return;
            std::string path;
            if (!a->images.empty()) {
                int dir = (faceY < 0) ? 0 : (faceX > 0) ? 1 : (faceY > 0) ? 2 : 3;  // N,E,S,W
                for (const auto& im : a->images) if (im.direction == dir) { path = im.path; break; }
                if (path.empty()) {
                    int di = a->defaultImage;
                    path = (di >= 0 && di < (int)a->images.size()) ? a->images[di].path
                                                                   : a->images.front().path;
                }
            } else {
                path = a->artworkPath;
            }
            if (path.empty()) return;
            SDL_Texture* tex = loadCachedImage(path);
            if (!tex) return;
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            if (tw <= 0 || th <= 0) return;
            float avail = ImGui::GetContentRegionAvail().x;
            float w = (maxW > 0.0f) ? std::min(maxW, avail) : avail;
            float scale = w / (float)tw;
            if (th * scale > maxH) { scale = maxH / (float)th; w = tw * scale; }
            ImGui::Image((ImTextureID)tex, ImVec2(w, th * scale));
        };
        // Wrapped paragraph with extra line leading (each wrapped line is its own item, so
        // ItemSpacing.y separates them) — far less crowded than a single TextWrapped block.
        auto drawProse = [&](const std::string& text, float leading) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, leading));
            float wrapW = ImGui::GetContentRegionAvail().x;
            if (wrapW < 1.0f) wrapW = 1.0f;
            size_t i = 0, n = text.size();
            while (true) {
                size_t nl = text.find('\n', i);
                std::string hard = text.substr(i, (nl == std::string::npos ? n : nl) - i);
                std::string line;
                size_t w = 0;
                while (w < hard.size()) {
                    size_t sp = hard.find(' ', w);
                    std::string word = hard.substr(w, (sp == std::string::npos ? hard.size() : sp) - w);
                    std::string cand = line.empty() ? word : line + " " + word;
                    if (!line.empty() && ImGui::CalcTextSize(cand.c_str()).x > wrapW) {
                        ImGui::TextUnformatted(line.c_str());
                        line = word;
                    } else {
                        line = cand;
                    }
                    w = (sp == std::string::npos ? hard.size() : sp + 1);
                }
                if (!line.empty()) ImGui::TextUnformatted(line.c_str());
                else if (hard.empty()) ImGui::TextUnformatted("");   // preserve blank lines
                if (nl == std::string::npos) break;
                i = nl + 1;
            }
            ImGui::PopStyleVar();
        };

        // Which defined area (if any) the party token currently stands on — drives whether the
        // left region shows the map or the full area/shop view.
        gns::Area* hereArea = nullptr;
        if (session) {
            if (const gns::Map* cmh = session->currentMap())
                if (cursorX >= 0 && cursorY >= 0 && cursorX < cmh->gridW && cursorY < cmh->gridH) {
                    int aid = cmh->cellArea[(size_t)cursorY * cmh->gridW + cursorX];
                    if (aid) hereArea = mod.areaById(aid);
                }
        }

        // Full-window area view: replaces the map while the party is "inside" a defined area.
        // Shows the area art + description; for shops, clickable buy/sell grids with confirm
        // popups. Clicking an item never buys/sells directly — it opens a Yes/No dialog.
        auto drawAreaView = [&](gns::Area* a) {
            if (!a || !session) return;
            auto& party = session->party().members;
            if (shopBuyer < 0) shopBuyer = 0;
            if (shopBuyer >= (int)party.size()) shopBuyer = 0;

            // Up/down arrows cycle the active character while inside an area (map movement is
            // paused here, so the keys are free); clicking a card on the right does the same.
            // While a confirm popup is open it owns the keyboard, so don't cycle behind it.
            ImGuiIO& io = ImGui::GetIO();
            bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            if (!io.WantTextInput && !party.empty() && !popupOpen) {
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                    shopBuyer = (shopBuyer + 1) % (int)party.size();
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    shopBuyer = (shopBuyer + (int)party.size() - 1) % (int)party.size();
            }

            ImGui::SeparatorText(areaLabel(a).c_str());
            // Area art on the left; the description fills the space to its right.
            float topAvail = ImGui::GetContentRegionAvail().x;
            float imgW = std::min(560.0f, topAvail * 0.42f);
            ImGui::BeginGroup();
            drawAreaImage(a, imgW, 460.0f);
            ImGui::EndGroup();
            if (!a->playerText.empty()) {
                ImGui::SameLine(0.0f, 20.0f);
                ImGui::BeginGroup();
                drawProse(a->playerText, 4.0f);
                ImGui::EndGroup();
            }
            ImGui::Spacing();

            if (!a->isShop || party.empty()) return;   // non-shop areas: just art + text

            gns::Character& buyer = party[shopBuyer];
            int disc = parseDiscountPct(a->dmText, buyer.kin, buyer.calling);
            if (disc > 0) ImGui::Text("Buying as %s  -  %d gp  (discount %d%%)",
                                      buyer.name.c_str(), buyer.gold, disc);
            else ImGui::Text("Buying as %s  -  %d gp", buyer.name.c_str(), buyer.gold);
            auto priceOf = [&](const gns::ShopItem& it) { return it.costGp - it.costGp * disc / 100; };

            const ImVec2 tileSz(150.0f, 190.0f);
            // A clickable item tile: name + two info lines over a thumbnail. Click-anywhere via
            // an InvisibleButton with content painted on top (same drawlist trick as avatars).
            auto itemTile = [&](const std::string& id, SDL_Texture* tx, const std::string& name,
                                const std::string& line1, const std::string& line2,
                                bool enabled, const std::string& tip, bool invalid) -> bool {
                ImGui::PushID(id.c_str());
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                bool pressed = ImGui::InvisibleButton("tile", tileSz);
                bool hov = ImGui::IsItemHovered();
                if (hov && !tip.empty()) {
                    // Wrapped tooltip: a readable multi-row box (title + description) instead of
                    // one full-window-wide line.
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                    if (!name.empty()) { ImGui::TextUnformatted(name.c_str()); ImGui::Separator(); }
                    ImGui::TextUnformatted(tip.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                    ImGui::PopStyleVar();
                }
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p1(p0.x + tileSz.x, p0.y + tileSz.y);
                // Hovering an "invalid" tile (e.g. gear this shop won't buy) glows red.
                ImU32 hoverFill = invalid ? IM_COL32(96, 38, 38, 255) : IM_COL32(62, 70, 92, 255);
                dl->AddRectFilled(p0, p1, (hov && enabled) ? hoverFill
                                                           : IM_COL32(40, 44, 58, 255), 6.0f);
                ImU32 borderCol = (hov && invalid) ? IM_COL32(225, 70, 70, 255) : IM_COL32(20, 16, 24, 255);
                dl->AddRect(p0, p1, borderCol, 6.0f, 0, (hov && invalid) ? 3.0f : 2.0f);
                float imgS = tileSz.x - 24.0f;
                ImVec2 ip0(p0.x + 12.0f, p0.y + 10.0f), ip1(ip0.x + imgS, ip0.y + imgS);
                if (tx) dl->AddImage((ImTextureID)tx, ip0, ip1);
                else { dl->AddRectFilled(ip0, ip1, IM_COL32(28, 30, 40, 255), 4.0f);
                       dl->AddRect(ip0, ip1, IM_COL32(70, 74, 90, 255), 4.0f); }
                float ty = ip1.y + 6.0f;
                ImU32 tcol = enabled ? IM_COL32(235, 235, 240, 255) : IM_COL32(150, 150, 155, 255);
                dl->AddText(ImVec2(p0.x + 10.0f, ty), tcol, name.empty() ? "(item)" : name.c_str());
                if (!line1.empty()) dl->AddText(ImVec2(p0.x + 10.0f, ty + 18.0f),
                                                IM_COL32(205, 192, 130, 255), line1.c_str());
                if (!line2.empty()) dl->AddText(ImVec2(p0.x + 10.0f, ty + 34.0f),
                                                IM_COL32(160, 160, 170, 255), line2.c_str());
                ImGui::PopID();
                return pressed && enabled;
            };
            // Lay item tiles out in a wrapping grid sized to the current child's width.
            auto gridPerRow = [&]() {
                ImGuiStyle& st = ImGui::GetStyle();
                float availW = ImGui::GetContentRegionAvail().x;
                return std::max(1, (int)((availW + st.ItemSpacing.x) / (tileSz.x + st.ItemSpacing.x)));
            };

            // Two side-by-side grids, each with its own scrollbar: the shop's stock on the left,
            // the active character's items on the right (shown even when empty, for future
            // drag-and-drop). OpenPopup is deferred to after EndChild so its id matches the modal.
            bool openBuy = false, openSell = false, openCantSell = false;
            // Resolve an owned item's art + description from any shop in the module, so a
            // character's gear shows its image even at a shop that doesn't stock that item.
            auto ownedItemArt = [&](const std::string& nm, SDL_Texture*& tx, std::string& desc) {
                tx = nullptr; desc.clear();
                for (const auto& mp : mod.maps)
                    for (const auto& ar : mp.areas)
                        for (const auto& it : ar.shopItems)
                            if (it.name == nm) {
                                if (!tx) tx = shopItemTexture(it);
                                if (desc.empty()) desc = it.description;
                                if (tx && !desc.empty()) return;
                            }
            };
            float gridH = ImGui::GetContentRegionAvail().y;
            if (gridH < 200.0f) gridH = 200.0f;
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float colW = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

            ImGui::BeginChild("shopcol", ImVec2(colW, gridH), ImGuiChildFlags_Borders);
            ImGui::SeparatorText("For Sale");
            if (a->shopItems.empty()) {
                ImGui::TextDisabled("(nothing for sale)");
            } else {
                int perRow = gridPerRow();
                int placed = 0;
                for (size_t i = 0; i < a->shopItems.size(); ++i) {
                    gns::ShopItem& it = a->shopItems[i];
                    if (placed % perRow != 0) ImGui::SameLine();
                    int price = priceOf(it);
                    bool canBuy = it.stock > 0 && buyer.gold >= price;
                    std::string l1 = std::to_string(price) + (disc > 0 ? " gp*" : " gp");
                    std::string l2 = "stock " + std::to_string(it.stock);
                    if (itemTile("buy" + std::to_string(i), shopItemTexture(it), it.name,
                                 l1, l2, canBuy, it.description, /*invalid=*/false)) {
                        pendingBuy = (int)i; openBuy = true;
                    }
                    ++placed;
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("charcol", ImVec2(colW, gridH), ImGuiChildFlags_Borders);
            ImGui::SeparatorText((buyer.name + "'s Items").c_str());
            if (buyer.inventory.empty()) {
                ImGui::TextDisabled("(no items)");
            } else {
                int perRow = gridPerRow();
                int placed = 0;
                for (size_t k = 0; k < buyer.inventory.size(); ++k) {
                    const std::string& nm = buyer.inventory[k];
                    int cost = 0;
                    for (const auto& it : a->shopItems)
                        if (it.name == nm) { cost = it.costGp; break; }
                    SDL_Texture* tx = nullptr; std::string desc;
                    ownedItemArt(nm, tx, desc);    // art from any shop in the module
                    bool sellable = cost > 0;
                    if (placed % perRow != 0) ImGui::SameLine();
                    std::string l1 = sellable ? ("sell " + std::to_string(cost / 2) + " gp") : std::string();
                    std::string l2 = sellable ? std::string() : std::string("not sold here");
                    // Always clickable: selling here confirms, otherwise explains it can't be sold
                    // (and the tile glows red on hover to flag it as not sellable here).
                    if (itemTile("sell" + std::to_string(k), tx, nm, l1, l2, /*enabled=*/true, desc,
                                 /*invalid=*/!sellable)) {
                        pendingSell = (int)k;
                        if (sellable) openSell = true; else openCantSell = true;
                    }
                    ++placed;
                }
            }
            ImGui::EndChild();

            if (openBuy)      { ImGui::OpenPopup("Confirm Purchase"); confirmChoice = 0; }
            if (openSell)     { ImGui::OpenPopup("Confirm Sale");     confirmChoice = 0; }
            if (openCantSell) ImGui::OpenPopup("Cannot Sell");

            // Yes/No confirm row with keyboard support: "No" starts highlighted; Left moves to
            // Yes / Right moves back to No, Enter activates the highlighted choice, Escape cancels.
            // Returns 1 = confirmed, -1 = cancelled, 0 = still deciding. `yesEnabled` greys out Yes.
            auto confirmButtons = [&](bool yesEnabled) -> int {
                if (yesEnabled && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) confirmChoice = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))             confirmChoice = 0;
                int result = 0;
                auto choiceButton = [&](const char* label, int idx, bool enabled) -> bool {
                    bool selected = (confirmChoice == idx);
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.47f, 0.78f, 1.0f, 1.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                    }
                    if (!enabled) ImGui::BeginDisabled();
                    bool clicked = ImGui::Button(label, ImVec2(110, 0));
                    if (!enabled) ImGui::EndDisabled();
                    if (selected) { ImGui::PopStyleVar(); ImGui::PopStyleColor(2); }
                    return clicked;
                };
                if (choiceButton("Yes", 1, yesEnabled)) result = 1;
                ImGui::SameLine();
                if (choiceButton("No", 0, true))        result = -1;
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                    if (confirmChoice == 1 && yesEnabled) result = 1;
                    else if (confirmChoice == 0)          result = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) result = -1;
                return result;
            };

            if (ImGui::BeginPopupModal("Confirm Purchase", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (pendingBuy >= 0 && pendingBuy < (int)a->shopItems.size()) {
                    gns::ShopItem& it = a->shopItems[pendingBuy];
                    int price = priceOf(it);
                    ImGui::Text("Do you want to purchase %s for %d GP?",
                                it.name.empty() ? "this item" : it.name.c_str(), price);
                    ImGui::Spacing();
                    bool ok = it.stock > 0 && buyer.gold >= price;
                    int choice = confirmButtons(/*yesEnabled=*/ok);
                    if (choice == 1) {
                        buyer.gold -= price; it.stock -= 1;
                        buyer.inventory.push_back(it.name);
                        journal.push_back(buyer.name + " buys " + it.name + " for " +
                                          std::to_string(price) + " gp.");
                        pendingBuy = -1; ImGui::CloseCurrentPopup();
                    } else if (choice == -1) {
                        pendingBuy = -1; ImGui::CloseCurrentPopup();
                    }
                } else { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Confirm Sale", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (pendingSell >= 0 && pendingSell < (int)buyer.inventory.size()) {
                    const std::string nm = buyer.inventory[pendingSell];
                    int cost = 0;
                    for (const auto& it : a->shopItems) if (it.name == nm) { cost = it.costGp; break; }
                    int gain = cost / 2;
                    ImGui::Text("Are you sure you want to sell %s for %d GP?", nm.c_str(), gain);
                    ImGui::Spacing();
                    int choice = confirmButtons(/*yesEnabled=*/true);
                    if (choice == 1) {
                        buyer.gold += gain;
                        buyer.inventory.erase(buyer.inventory.begin() + pendingSell);
                        journal.push_back(buyer.name + " sells " + nm + " for " +
                                          std::to_string(gain) + " gp.");
                        pendingSell = -1; ImGui::CloseCurrentPopup();
                    } else if (choice == -1) {
                        pendingSell = -1; ImGui::CloseCurrentPopup();
                    }
                } else { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Cannot Sell", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (pendingSell >= 0 && pendingSell < (int)buyer.inventory.size())
                    ImGui::Text("You cannot sell %s at this shop.", buyer.inventory[pendingSell].c_str());
                else
                    ImGui::TextUnformatted("You cannot sell that item at this shop.");
                ImGui::Spacing();
                if (ImGui::Button("OK", ImVec2(110, 0))) { pendingSell = -1; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        };

        // Rich, clickable party cards for the in-session right panel. Clicking a card (or the
        // up/down arrows while in an area) makes that character active — the highlighted frame
        // marks the current one, and the active index drives the shop's buyer/seller.
        auto drawPartyPanel = [&](std::vector<gns::Character>& members) {
            ImGuiStyle& st = ImGui::GetStyle();
            const float av = 92.0f;                       // avatar width (3:4 -> avH tall)
            const float avH = av * 4.0f / 3.0f;
            // Card is exactly as tall as the avatar (plus the child's padding); the stats are
            // spread across two columns to its right so they fit without a scrollbar.
            const float cardH = avH + st.WindowPadding.y * 2.0f + st.CellPadding.y * 2.0f + 4.0f;
            for (size_t i = 0; i < members.size(); ++i) {
                gns::Character& pc = members[i];
                bool active = ((int)i == shopBuyer);
                ImGui::PushID((int)i);
                ImGui::PushStyleColor(ImGuiCol_Border, active ? IM_COL32(120, 200, 255, 255)
                                                              : IM_COL32(70, 70, 84, 255));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, active ? 2.5f : 1.0f);
                ImGui::BeginChild("card", ImVec2(0, cardH), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar);
                if (ImGui::BeginTable("cardgrid", 3)) {
                    ImGui::TableSetupColumn("av", ImGuiTableColumnFlags_WidthFixed, av + 4.0f);
                    ImGui::TableSetupColumn("a",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("b",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    drawAvatar(pc, av);

                    ImGui::TableSetColumnIndex(1);   // identity + vitals
                    ImGui::TextUnformatted(pc.name.empty() ? "(unnamed)" : pc.name.c_str());
                    ImGui::TextDisabled("%s %s  \xC2\xB7  Lv %d", pc.kin.c_str(), pc.calling.c_str(), pc.level);
                    ImGui::Text("Life %d/%d   Def %d", pc.life, pc.maxLife, pc.defense);
                    ImGui::Text("AP %d   Strain %d", pc.ap, pc.strain);

                    ImGui::TableSetColumnIndex(2);   // traits + gear
                    ImGui::Text("Might %+d   Grace %+d", pc.traits.might, pc.traits.grace);
                    ImGui::Text("Wits %+d   Spirit %+d", pc.traits.wits, pc.traits.spirit);
                    std::string weap = pc.weaponName.empty() ? "Unarmed" : pc.weaponName;
                    weap += " (" + (pc.weaponDamageDie.empty() ? std::string("1d6") : pc.weaponDamageDie) + ")";
                    if (pc.weaponBonus) weap += " +" + std::to_string(pc.weaponBonus);
                    ImGui::Text("Weapon: %s", weap.c_str());
                    ImGui::Text("%s%s  \xC2\xB7  %d gp",
                                pc.armorName.empty() ? "No armor" : pc.armorName.c_str(),
                                pc.shield ? " + shield" : "", pc.gold);

                    ImGui::EndTable();
                }
                bool hov = ImGui::IsWindowHovered();
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) shopBuyer = (int)i;
                ImGui::PopID();
                ImGui::Spacing();
            }
        };

        // --- Map canvas (left) ---
        ImGui::SetNextWindowPos(wp);
        ImGui::SetNextWindowSize(ImVec2(ws.x - rightW, ws.y));
        ImGui::Begin("Map", nullptr, pf);
        if (session && areaView && hereArea) {
            drawAreaView(hereArea);
        } else if (!session) {
            ImGui::TextWrapped("%s", haveModule
                ? "Press \"Start Adventure\" in the Adventure panel."
                : "Open a module first (File \xE2\x96\xB8 Open Module).");
        } else if (const gns::Map* cm = session->currentMap()) {
            const gns::Map& m = *cm;
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float cs = std::min(avail.x / (float)m.gridW, avail.y / (float)m.gridH);
            if (cs < 1.0f) cs = 1.0f;
            ImVec2 cur = ImGui::GetCursorScreenPos();
            ImVec2 origin(cur.x + (avail.x - cs * m.gridW) * 0.5f,
                          cur.y + (avail.y - cs * m.gridH) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 visMin = ImGui::GetWindowPos();
            ImVec2 visMax(visMin.x + ImGui::GetWindowSize().x, visMin.y + ImGui::GetWindowSize().y);
            gns::ui::renderMapView(dl, m, mod.controlPoints, m.id, origin, cs, visMin, visMax,
                                   /*hideHiddenAreas=*/true);

            // Highlight the currently-displayed area for orientation.
            const gns::Area* ca = session->currentArea();
            if (ca)
                gns::ui::drawAreaOutline(dl, m, ca->id, IM_COL32(90, 220, 255, 255),
                                         std::max(2.5f, cs * 0.14f), origin, cs, visMin, visMax);

            // Map cell occupied by a control item marker (or -1,-1 for none).
            auto controlItemCell = [&](const gns::ControlPoint& cp, int& ix, int& iy) {
                ix = iy = -1;
                if (cp.mapId != m.id || cp.kind != 1) return;
                if (cp.x >= 0) { ix = (int)cp.x; iy = (int)cp.y; }
                else { int px, py; gns::ui::areaCentroid(m, cp.areaId, px, py);
                       if (px >= 0) { ix = px; iy = py; } }
            };

            // Act on the cell the party token stands on: take a control item, or enter the
            // area there (gated by prerequisites).
            auto actOnCell = [&](int cx, int cy) {
                if (cx < 0 || cy < 0 || cx >= m.gridW || cy >= m.gridH) return;
                int target = m.cellArea[(size_t)cy * m.gridW + cx];
                // Pick up Control Items on this exact cell OR sitting inside the target area
                // (the latter resolves modules whose required item lives in the area it gates,
                // e.g. the Tavern's "Map of Mount Toggenburg").
                for (const auto& cp : mod.controlPoints) {
                    int ix, iy; controlItemCell(cp, ix, iy);
                    bool onCell = (ix == cx && iy == cy);
                    bool inArea = (cp.kind == 1 && cp.mapId == m.id && target != 0 && cp.areaId == target);
                    if (onCell || inArea) {
                        if (session->completeControlPoint(cp.id)) {
                            journal.push_back("Acquired: " + cp.name);
                            playStatus = "Acquired " + cp.name + ".";
                        }
                    }
                }
                int curId = session->currentArea() ? session->currentArea()->id : 0;
                if (target == 0) return;            // undefined cell: HUD shows party details
                if (!session->isAreaEnterable(target)) {
                    std::string need;
                    if (const gns::Area* ta = mod.areaById(target))
                        for (int cpid : ta->prerequisiteControlPointIds)
                            for (const auto& cp : mod.controlPoints)
                                if (cp.id == cpid) { if (!need.empty()) need += ", "; need += cp.name; }
                    playStatus = "Locked \xE2\x80\x94 requires: " + (need.empty() ? "an objective" : need);
                    return;
                }
                // Run the enter "beat" (narration/encounter) only on a fresh entry, but always
                // (re)open the area view — so stepping off and back onto a shop re-opens it.
                if (target != curId) { enterArea(target); playStatus.clear(); }
                areaView = true;
            };

            // Keyboard: arrow keys glide the party token (and set facing); holding Space turns
            // the party in place (facing only). Enter acts on the token's cell.
            ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput) {
                bool turnOnly = ImGui::IsKeyDown(ImGuiKey_Space);
                int dx = 0, dy = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  dx = -1;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = +1;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    dy = -1;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  dy = +1;
                if (dx || dy) {
                    faceX = dx; faceY = dy;          // face the pressed direction
                    if (!turnOnly) {                  // and step there unless turning in place
                        cursorX = std::min(m.gridW - 1, std::max(0, cursorX + dx));
                        cursorY = std::min(m.gridH - 1, std::max(0, cursorY + dy));
                        actOnCell(cursorX, cursorY);  // auto-enter/show the area we stepped onto
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
                    actOnCell(cursorX, cursorY);
            }
            // Mouse: clicking a cell moves the token there and acts on it.
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mp = io.MousePos;
                int cx = (int)((mp.x - origin.x) / cs), cy = (int)((mp.y - origin.y) / cs);
                if (cx >= 0 && cy >= 0 && cx < m.gridW && cy < m.gridH) {
                    cursorX = cx; cursorY = cy;
                    actOnCell(cx, cy);
                }
            }

            // Party token at the cursor cell, with a facing arrowhead.
            {
                ImVec2 tc(origin.x + (cursorX + 0.5f) * cs, origin.y + (cursorY + 0.5f) * cs);
                float rad = std::max(4.0f, cs * 0.34f);
                dl->AddCircleFilled(tc, rad, IM_COL32(255, 240, 120, 255));
                dl->AddCircle(tc, rad, IM_COL32(40, 30, 10, 255), 0, 2.5f);
                // Arrowhead pointing along the facing vector.
                float fl = (float)std::sqrt((float)(faceX * faceX + faceY * faceY));
                if (fl > 0.0f) {
                    float ux = faceX / fl, uy = faceY / fl;      // unit facing
                    float px = -uy, py = ux;                      // perpendicular
                    float tip = rad * 1.45f, base = rad * 0.65f, half = rad * 0.55f;
                    ImVec2 a(tc.x + ux * tip, tc.y + uy * tip);
                    ImVec2 b(tc.x + ux * base + px * half, tc.y + uy * base + py * half);
                    ImVec2 c(tc.x + ux * base - px * half, tc.y + uy * base - py * half);
                    dl->AddTriangleFilled(a, b, c, IM_COL32(180, 30, 30, 255));
                    dl->AddTriangle(a, b, c, IM_COL32(40, 30, 10, 255), 1.5f);
                }
            }
        }
        ImGui::End();

        // --- Adventure panel (right) ---
        ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - rightW, wp.y));
        ImGui::SetNextWindowSize(ImVec2(rightW, ws.y));
        ImGui::Begin("Adventure", nullptr, pf);
        if (!haveModule) {
            ImGui::TextWrapped("Open a module to begin (File \xE2\x96\xB8 Open Module).");
        } else {
            ImGui::TextWrapped("%s", mod.name.empty() ? "(untitled module)" : mod.name.c_str());
            if (!mod.summary.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", mod.summary.c_str()); }
            ImGui::Separator();
            if (!session) {
                ImGui::TextDisabled("Party from roster: %d character(s)", (int)roster.size());
                if (!roster.empty()) {
                    if (ImGui::Button("Start Adventure")) startAdventure();
                    ImGui::Spacing();
                    ImGui::SeparatorText("Party");
                    drawPartyCards(roster);
                    ImGui::Separator();
                }
                ImGui::SetNextItemWidth(160);
                ImGui::SliderInt("Characters", &defaultPartyCount, 1, 5);
                if (ImGui::Button("Quick Start (generate party)")) {
                    quickStartParty(defaultPartyCount);
                    startAdventure();
                }
                ImGui::TextDisabled("or build a party in Characters mode, then Load it.");
            } else {
                // Party panel: rich, clickable cards. Clicking a card (or up/down arrows while
                // in an area) picks the active character; the area/shop content fills the left
                // region via drawAreaView. `hereArea` is computed once before the windows.
                ImGui::SeparatorText("Party");
                drawPartyPanel(session->party().members);
                if (hereArea && !areaView) {
                    std::string lbl = "Enter " + areaLabel(hereArea);
                    if (ImGui::Button(lbl.c_str())) areaView = true;
                }

                ImGui::Spacing();
                if (!playStatus.empty())
                    ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", playStatus.c_str());
                // Restart (confirmed) and Leave grouped together.
                bool openRestart = false;
                if (ImGui::Button("Restart")) openRestart = true;
                if (areaView && hereArea) {
                    ImGui::SameLine();
                    if (ImGui::Button(hereArea->isShop ? "Leave Shop" : "Exit")) areaView = false;
                }
                if (openRestart) ImGui::OpenPopup("Confirm Restart");
                if (ImGui::BeginPopupModal("Confirm Restart", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Restart the game? All current progress will be lost.");
                    ImGui::Spacing();
                    if (ImGui::Button("Yes, restart", ImVec2(130, 0))) {
                        session.reset(); journal.clear(); playStatus.clear(); areaView = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::TextDisabled("Arrows move the party \xC2\xB7 Enter to act \xC2\xB7 in a shop Up/Down pick character");
                ImGui::Separator();
                // Journal is collapsed by default — expand to read. Fixed-height scroll child so
                // it doesn't eat the panel above when open.
                if (ImGui::CollapsingHeader("Journal")) {
                    ImGui::BeginChild("journal", ImVec2(0, 150), ImGuiChildFlags_Borders);
                    for (const auto& line : journal) { ImGui::TextWrapped("%s", line.c_str()); ImGui::Spacing(); }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
                    ImGui::EndChild();
                }
            }
        }
        ImGui::End();
      }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 20, 32, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (coverTex) SDL_DestroyTexture(coverTex);
    clearImgCache();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
