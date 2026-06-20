// Grimoire & Steel — Game Engine (M0 vertical slice).
// Proves the toolchain: SDL2 + Dear ImGui + sqlite3 + gns.db all linked & running.
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include "gns/Database.h"
#include "gns/Content.h"

#include <memory>
#include <string>

static std::string dbPath() {
    char* base = SDL_GetBasePath();
    std::string p = base ? base : "";
    if (base) SDL_free(base);
    return p + "data/gns.db";
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

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
        monsterCount = gns::countRows(*db, "monsters");
        spellCount = gns::countRows(*db, "all_spells");
        monsters = gns::topMonsters(*db, 25);
        spells = gns::allSpells(*db);
    } catch (const std::exception& e) {
        dbErr = e.what();
    }

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

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
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
                ImGui::TableSetupColumn("HD");
                ImGui::TableSetupColumn("AC");
                ImGui::TableSetupColumn("Alignment");
                ImGui::TableHeadersRow();
                for (auto& m : monsters) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(m.name.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(m.hitDice.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(m.armorClass.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(m.alignment.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(440, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(640, 690), ImGuiCond_FirstUseEver);
        ImGui::Begin("Spells");
        if (dbErr.empty() && ImGui::BeginTable("spells", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Lvl");
            ImGui::TableSetupColumn("Class");
            ImGui::TableSetupColumn("Range");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (auto& s : spells) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%d", s.level);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.spellClass.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.range.c_str());
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

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
