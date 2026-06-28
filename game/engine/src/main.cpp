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

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

static std::string dbPath() {
    char* base = SDL_GetBasePath();
    std::string p = base ? base : "";
    if (base) SDL_free(base);
    return p + "data/gns.db";
}

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
    } catch (const std::exception& e) {
        dbErr = e.what();
    }

    // Loaded adventure module + its cover-art splash.
    gns::Module mod;
    bool haveModule = false;
    std::string moduleStatus;
    SDL_Texture* coverTex = nullptr;
    bool showSplash = false;

    auto openModule = [&](const std::string& path) {
        try {
            mod = gns::loadModule(path);
            haveModule = true;
            if (coverTex) { SDL_DestroyTexture(coverTex); coverTex = nullptr; }
            if (!mod.coverArtPath.empty()) {
                std::string full = isAbsolutePath(mod.coverArtPath)
                                       ? mod.coverArtPath : dirOf(path) + mod.coverArtPath;
                coverTex = loadImage(renderer, full);
            }
            moduleStatus = "Loaded: " + (mod.name.empty() ? "(untitled)" : mod.name);
            showSplash = true;   // show the cover (or a title card) whenever a game loads
        } catch (const std::exception& e) {
            haveModule = false;
            moduleStatus = std::string("Open failed: ") + e.what();
        }
    };

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
            if (!moduleStatus.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", moduleStatus.c_str());
            }
            ImGui::EndMainMenuBar();
        }

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

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 20, 32, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (coverTex) SDL_DestroyTexture(coverTex);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
