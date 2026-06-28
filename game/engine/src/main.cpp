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

#include <algorithm>
#include <cctype>
#include <cmath>
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

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);   // cover-art splash images

    SDL_Window* window = SDL_CreateWindow(
        "Grimoire & Steel — Game Engine (M0)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
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
            const char* hint = "Click or press Enter to begin";
            ImVec2 hs = ImGui::CalcTextSize(hint);
            ImGui::SetCursorPosX((win.x - hs.x) * 0.5f);
            ImGui::TextDisabled("%s", hint);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
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
                if (ImGui::MenuItem("Show Cover", nullptr, false, haveModule)) showSplash = true;
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

        const bool isMystic = (draft.calling == "Mystic");
        const int needTraining = gns::requiredTrainingCount(draft.kin);
        const bool traitsOk = gns::validTraitSpread(tr);
        const bool trainingOk = (int)chosenTrainings.size() == needTraining;
        const bool spellsOk = !isMystic || chosenSpells.size() == 3;
        const bool canSave = repo && !draft.name.empty() && traitsOk && trainingOk && spellsOk;

        ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(540, 690), ImGuiCond_FirstUseEver);
        ImGui::Begin("Create Character");
        if (!repo) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "DB error: %s", dbErr.c_str());
        } else {
            ImGui::SeparatorText("Identity");
            inputText("Name", &draft.name);
            inputText("Player", &draft.player);
            inputText("Background", &draft.background);
            inputText("Goal", &draft.goal);

            ImGui::SeparatorText("Portrait");
            if (portraitFiles.empty()) {
                ImGui::TextDisabled("No portraits found in assets/portraits.");
            } else {
                const float sz = 64.0f;
                ImGuiStyle& st = ImGui::GetStyle();
                float availW = ImGui::GetContentRegionAvail().x;
                int perRow = std::max(1, (int)((availW + st.ItemSpacing.x) / (sz + st.ItemSpacing.x)));
                int placed = 0;
                auto rowBreak = [&]() { if (placed % perRow != 0) ImGui::SameLine(); };
                rowBreak();
                if (ImGui::Button("None##port", ImVec2(sz, sz))) draft.portraitPath.clear();
                ++placed;
                for (size_t i = 0; i < portraitFiles.size(); ++i) {
                    rowBreak();
                    bool sel = (draft.portraitPath == portraitFiles[i]);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.62f, 0.86f, 1.0f));
                    std::string id = "##port" + std::to_string(i);
                    SDL_Texture* tex = portraitTexture(portraitFiles[i]);
                    bool clicked = tex
                        ? ImGui::ImageButton(id.c_str(), (ImTextureID)tex, ImVec2(sz, sz))
                        : ImGui::Button((std::to_string(i + 1) + id).c_str(), ImVec2(sz, sz));
                    if (clicked) draft.portraitPath = portraitFiles[i];
                    if (sel) ImGui::PopStyleColor();
                    ++placed;
                }
            }

            ImGui::SeparatorText("Kin & Calling");
            comboField("Kin", &draft.kin, kinNames);
            if (kin) ImGui::TextWrapped("Gift (%s): %s", kin->giftName.c_str(),
                                        kin->giftDescription.c_str());
            comboField("Calling", &draft.calling, callingNames);
            if (calling) {
                ImGui::TextWrapped("Gift (%s): %s", calling->giftName.c_str(),
                                   calling->giftDescription.c_str());
                ImGui::TextWrapped("Armor: %s", calling->armorAllowed.c_str());
                ImGui::TextWrapped("Starting gear: %s", calling->startingGear.c_str());
            }

            ImGui::SeparatorText("Traits  (assign +2, +1, +0, -1)");
            static const char* traitLabels[4] = {"Might", "Grace", "Wits", "Spirit"};
            static const int traitChoices[4] = {2, 1, 0, -1};
            for (int t = 0; t < 4; ++t) {
                ImGui::PushID(t);
                ImGui::TextUnformatted(traitLabels[t]); ImGui::SameLine(90);
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
            }
            if (!traitsOk)
                ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
                                   "Assign each of +2, +1, +0, -1 exactly once.");

            ImGui::SeparatorText("Training");
            ImGui::Text("Choose %d (%d selected)", needTraining, (int)chosenTrainings.size());
            for (size_t i = 0; i < trainingNames.size(); ++i) {
                bool sel = draft.trainingSel[i] != 0;
                bool offered = calling &&
                    std::find(calling->trainingOptions.begin(), calling->trainingOptions.end(),
                              trainingNames[i]) != calling->trainingOptions.end();
                std::string lbl = trainingNames[i] + (offered ? "  (calling)" : "");
                if (ImGui::Checkbox(lbl.c_str(), &sel)) draft.trainingSel[i] = sel ? 1 : 0;
            }

            ImGui::SeparatorText("Equipment");
            if (comboField("Weapon type", &draft.weaponCat, weaponCatNames)) {
                if (const gns::WeaponCategory* w = repo->weaponCategory(draft.weaponCat))
                    draft.weaponName = w->name;   // seed a sensible default name
            }
            inputText("Main attack", &draft.weaponName);
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Weapon bonus", &draft.weaponBonus, 0, 3);
            std::vector<std::string> armorOpts = allowedArmors(calling, *repo);
            // Snap armor to an allowed option if the calling forbids the current pick.
            if (std::find(armorOpts.begin(), armorOpts.end(), draft.armorName) == armorOpts.end()
                && !armorOpts.empty())
                draft.armorName = armorOpts.front();
            comboField("Armor", &draft.armorName, armorOpts);
            if (callingAllowsShield(calling)) ImGui::Checkbox("Shield (+1 Defense)", &draft.shield);
            else draft.shield = false;

            if (isMystic) {
                ImGui::SeparatorText("Spells  (choose 3)");
                ImGui::Text("%d selected", (int)chosenSpells.size());
                for (size_t i = 0; i < spellNames.size(); ++i) {
                    bool sel = draft.spellSel[i] != 0;
                    bool atLimit = chosenSpells.size() >= 3 && !sel;
                    if (atLimit) ImGui::BeginDisabled();
                    if (ImGui::Checkbox(spellNames[i].c_str(), &sel))
                        draft.spellSel[i] = sel ? 1 : 0;
                    if (atLimit) ImGui::EndDisabled();
                }
            }

            ImGui::SeparatorText("Derived");
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

        // ----- Roster (loaded characters; the future party) -----
        ImGui::SetNextWindowPos(ImVec2(560, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 690), ImGuiCond_FirstUseEver);
        ImGui::Begin("Roster / Party");
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

        // --- Map canvas (left) ---
        ImGui::SetNextWindowPos(wp);
        ImGui::SetNextWindowSize(ImVec2(ws.x - rightW, ws.y));
        ImGui::Begin("Map", nullptr, pf);
        if (!session) {
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
            gns::ui::renderMapView(dl, m, mod.controlPoints, m.id, origin, cs, visMin, visMax);

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
                if (target == curId) return;
                if (session->isAreaEnterable(target)) { enterArea(target); playStatus.clear(); }
                else {
                    std::string need;
                    if (const gns::Area* ta = mod.areaById(target))
                        for (int cpid : ta->prerequisiteControlPointIds)
                            for (const auto& cp : mod.controlPoints)
                                if (cp.id == cpid) { if (!need.empty()) need += ", "; need += cp.name; }
                    playStatus = "Locked \xE2\x80\x94 requires: " + (need.empty() ? "an objective" : need);
                }
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

        // Placeholder avatar colour derived from the character's name.
        auto nameColor = [](const std::string& s) -> ImU32 {
            unsigned h = 2166136261u;
            for (char c : s) { h ^= (unsigned char)c; h *= 16777619u; }
            return IM_COL32(70 + (h & 0x7F), 70 + ((h >> 8) & 0x7F), 70 + ((h >> 16) & 0x7F), 255);
        };
        // One card per party member: placeholder avatar + name / kin·calling / Life·Def·AP.
        // Draw an `av`-sized avatar at the current cursor: the character's portrait if it
        // loads, else a name-hashed placeholder square with the initial.
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
        auto drawPartyCards = [&](const std::vector<gns::Character>& members) {
            const float av = 44.0f;
            for (const auto& pc : members) {
                drawAvatar(pc, av);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(pc.name.empty() ? "(unnamed)" : pc.name.c_str());
                ImGui::TextDisabled("%s %s  \xC2\xB7  Lv %d", pc.kin.c_str(), pc.calling.c_str(), pc.level);
                ImGui::Text("Life %d/%d   Def %d   AP %d", pc.life, pc.maxLife, pc.defense, pc.ap);
                ImGui::EndGroup();
                ImGui::Spacing();
            }
        };
        // Full character sheet per member — shown in the HUD when the party is on open terrain.
        auto joinList = [](const std::vector<std::string>& v) {
            std::string s;
            for (const auto& x : v) { if (!s.empty()) s += ", "; s += x; }
            return s.empty() ? std::string("\xE2\x80\x94") : s;
        };
        auto drawPartyDetails = [&](const std::vector<gns::Character>& members) {
            for (size_t i = 0; i < members.size(); ++i) {
                const gns::Character& pc = members[i];
                if (i) ImGui::Separator();
                drawAvatar(pc, 64.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(pc.name.empty() ? "(unnamed)" : pc.name.c_str());
                ImGui::TextDisabled("%s %s  \xC2\xB7  Lv %d", pc.kin.c_str(), pc.calling.c_str(), pc.level);
                ImGui::Text("Life %d/%d   Def %d   AP %d   Strain %d",
                            pc.life, pc.maxLife, pc.defense, pc.ap, pc.strain);
                ImGui::Text("Might %+d  Grace %+d  Wits %+d  Spirit %+d",
                            pc.traits.might, pc.traits.grace, pc.traits.wits, pc.traits.spirit);
                ImGui::EndGroup();
                ImGui::TextWrapped("Training: %s", joinList(pc.trainings).c_str());
                std::string weap = pc.weaponName.empty() ? "Unarmed" : pc.weaponName;
                weap += " (" + (pc.weaponDamageDie.empty() ? std::string("1d6") : pc.weaponDamageDie) + ")";
                if (pc.weaponBonus) weap += " +" + std::to_string(pc.weaponBonus);
                ImGui::TextWrapped("Weapon: %s", weap.c_str());
                ImGui::TextWrapped("Armor: %s%s", pc.armorName.c_str(), pc.shield ? " + shield" : "");
                if (!pc.spells.empty()) ImGui::TextWrapped("Spells: %s", joinList(pc.spells).c_str());
                ImGui::Spacing();
            }
        };
        // Draw the current area's artwork scaled to the column (capped height).
        auto drawAreaImage = [&](const gns::Area* a) {
            if (!a || a->artworkPath.empty()) return;
            SDL_Texture* tex = loadCachedImage(a->artworkPath);
            if (!tex) return;
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            if (tw <= 0 || th <= 0) return;
            float w = ImGui::GetContentRegionAvail().x;
            float scale = w / (float)tw;
            const float maxH = 320.0f;
            if (th * scale > maxH) { scale = maxH / (float)th; w = tw * scale; }
            ImGui::Image((ImTextureID)tex, ImVec2(w, th * scale));
        };

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
                const gns::Area* ca = session->currentArea();

                // On a defined area, show the compact party strip + the area image/text; on
                // open terrain, show only the detailed party sheet (no stale last-area info).
                int cursorAreaId = 0;
                if (const gns::Map* cm2 = session->currentMap())
                    if (cursorX >= 0 && cursorY >= 0 && cursorX < cm2->gridW && cursorY < cm2->gridH)
                        cursorAreaId = cm2->cellArea[(size_t)cursorY * cm2->gridW + cursorX];
                if (cursorAreaId == 0) {
                    ImGui::SeparatorText("Party Details");
                    drawPartyDetails(session->party().members);
                } else {
                    ImGui::SeparatorText("Party");
                    drawPartyCards(session->party().members);
                    ImGui::SeparatorText(areaLabel(ca).c_str());
                    drawAreaImage(ca);
                    if (ca && !ca->playerText.empty()) ImGui::TextWrapped("%s", ca->playerText.c_str());
                }

                ImGui::Spacing();
                if (!playStatus.empty())
                    ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", playStatus.c_str());
                if (ImGui::Button("Restart")) { session.reset(); journal.clear(); playStatus.clear(); }
                ImGui::SameLine();
                ImGui::TextDisabled("Arrow keys move the party \xC2\xB7 Enter to enter an area / take an item");
                ImGui::Separator();
                ImGui::TextUnformatted("Journal");
                ImGui::BeginChild("journal", ImVec2(0, 0), ImGuiChildFlags_Borders);
                for (const auto& line : journal) { ImGui::TextWrapped("%s", line.c_str()); ImGui::Spacing(); }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
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
