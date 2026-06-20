// Grimoire & Steel — Module Creator (M3).
//
// A mouse-driven grid map editor for authoring adventure modules. Paint terrain
// and areas onto a fine grid, label areas via a coarse overlay grid (A1, C4…),
// fill in DM/player descriptions and per-area statistics, place plot control
// points, set area prerequisites, and save/load .gnsmod files (gns::saveModule /
// gns::loadModule). Reference data (monster names) is pulled from gns.db when
// present, degrading to free text otherwise.
//
// Layout: a main menu bar plus three windows — Tools, Map (canvas), Inspector.

#define NOMINMAX
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include "gns/Module.h"
#include "gns/Database.h"
#include "gns/Repository.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

// ---------------------------------------------------------------------------
// std::string helpers for ImGui::InputText (no imgui_stdlib in this vendor copy)
// ---------------------------------------------------------------------------
static int gInputTextResize(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}
static bool InputStr(const char* label, std::string* s, ImGuiInputTextFlags flags = 0) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, s->data(), s->capacity() + 1, flags, gInputTextResize, s);
}
static bool InputStrMultiline(const char* label, std::string* s, const ImVec2& size) {
    return ImGui::InputTextMultiline(label, s->data(), s->capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize, gInputTextResize, s);
}

// ---------------------------------------------------------------------------
// Native file dialogs (Win32). Returns "" if cancelled / unsupported.
// ---------------------------------------------------------------------------
static std::string fileDialog(bool save) {
#ifdef _WIN32
    char buf[1024] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Grimoire & Steel Module (*.gnsmod)\0*.gnsmod\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.lpstrDefExt = "gnsmod";
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

// ---------------------------------------------------------------------------
// Color conversion (model stores RGBA in a uint32; ImGui draws with ImU32 ABGR)
// ---------------------------------------------------------------------------
static void rgbaToFloat4(std::uint32_t c, float out[4]) {
    out[0] = ((c >> 24) & 0xFF) / 255.0f;
    out[1] = ((c >> 16) & 0xFF) / 255.0f;
    out[2] = ((c >> 8) & 0xFF) / 255.0f;
    out[3] = (c & 0xFF) / 255.0f;
}
static std::uint32_t float4ToRgba(const float in[4]) {
    auto q = [](float f) { int v = (int)(f * 255.0f + 0.5f); return v < 0 ? 0 : (v > 255 ? 255 : v); };
    return ((std::uint32_t)q(in[0]) << 24) | ((std::uint32_t)q(in[1]) << 16) |
           ((std::uint32_t)q(in[2]) << 8) | (std::uint32_t)q(in[3]);
}
static ImU32 rgbaToImU32(std::uint32_t c, int alpha = -1) {
    int a = alpha >= 0 ? alpha : (int)(c & 0xFF);
    return IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, a);
}

// Distinct default colors handed out to new areas.
static const std::uint32_t kAreaPalette[] = {
    0x4F8FE0FF, 0xE0884FFF, 0x6FBF73FF, 0xD05D7CFF, 0xB58FE0FF,
    0xD9C24FFF, 0x4FD0C7FF, 0xE05D5DFF, 0x8FA1B5FF, 0x9FD04FFF,
};

// Terrain base fill colors (RGBA). Decorative motifs are drawn on top (see
// drawTerrainMotif) so each type also reads at a glance.
static std::uint32_t terrainColor(int t) {
    switch (static_cast<gns::Terrain>(t)) {
        case gns::Terrain::Floor:    return 0x6B6B5AFF;
        case gns::Terrain::Wall:     return 0x3A3A3AFF;
        case gns::Terrain::Door:     return 0x8B5A2BFF;
        case gns::Terrain::Water:    return 0x2E5A8BFF;
        case gns::Terrain::Grass:    return 0x4E8A3CFF;
        case gns::Terrain::Trees:    return 0x356B2EFF;
        case gns::Terrain::Rocky:    return 0x7A746BFF;
        case gns::Terrain::Mountain: return 0x8A8A86FF;
        case gns::Terrain::Sand:     return 0xD9C58CFF;
        case gns::Terrain::Swamp:    return 0x4C5A3EFF;
        case gns::Terrain::Road:     return 0x9A8B6BFF;
        case gns::Terrain::Empty:
        default:                     return 0x2B313BFF;
    }
}
// Display names, indexed by Terrain value (must match the enum order).
static const char* kTerrainNames[] = {
    "Empty", "Floor", "Wall", "Door", "Water",
    "Grass", "Trees", "Rocky", "Mountain", "Sand", "Swamp", "Road"};

// Small per-cell decoration so terrain types are visually distinct (only when the
// cell is big enough to be worth it).
static void drawTerrainMotif(ImDrawList* dl, ImVec2 p0, float cs, int t) {
    if (cs < 9.0f) return;
    ImVec2 c(p0.x + cs * 0.5f, p0.y + cs * 0.5f);
    switch (static_cast<gns::Terrain>(t)) {
        case gns::Terrain::Trees: {
            ImU32 canopy = IM_COL32(36, 112, 42, 255), trunk = IM_COL32(92, 62, 32, 255);
            float r = cs * 0.26f;
            dl->AddRectFilled(ImVec2(c.x - cs * 0.04f, c.y), ImVec2(c.x + cs * 0.04f, c.y + cs * 0.32f), trunk);
            dl->AddCircleFilled(ImVec2(c.x, c.y - cs * 0.05f), r, canopy);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.7f, c.y + cs * 0.05f), r * 0.7f, canopy);
            dl->AddCircleFilled(ImVec2(c.x + r * 0.7f, c.y + cs * 0.05f), r * 0.7f, canopy);
            break;
        }
        case gns::Terrain::Rocky: {
            ImU32 a = IM_COL32(150, 148, 142, 255), b = IM_COL32(96, 94, 90, 255);
            dl->AddCircleFilled(ImVec2(c.x - cs * 0.18f, c.y + cs * 0.10f), cs * 0.16f, a, 6);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.16f, c.y - cs * 0.08f), cs * 0.13f, b, 6);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.05f, c.y + cs * 0.20f), cs * 0.10f, b, 6);
            break;
        }
        case gns::Terrain::Water: {
            ImU32 wave = IM_COL32(185, 212, 240, 200);
            for (int i = 0; i < 2; ++i) {
                float yy = p0.y + cs * (0.42f + 0.24f * i);
                dl->AddLine(ImVec2(p0.x + cs * 0.15f, yy), ImVec2(p0.x + cs * 0.45f, yy - cs * 0.06f), wave, 1.5f);
                dl->AddLine(ImVec2(p0.x + cs * 0.45f, yy - cs * 0.06f), ImVec2(p0.x + cs * 0.85f, yy), wave, 1.5f);
            }
            break;
        }
        case gns::Terrain::Grass: {
            ImU32 blade = IM_COL32(64, 146, 58, 220);
            for (int i = -1; i <= 1; ++i) {
                float bx = c.x + i * cs * 0.22f;
                dl->AddLine(ImVec2(bx, c.y + cs * 0.20f), ImVec2(bx, c.y - cs * 0.18f), blade, 1.5f);
            }
            break;
        }
        case gns::Terrain::Mountain: {
            ImU32 rock = IM_COL32(108, 104, 100, 255), snow = IM_COL32(236, 239, 245, 255);
            ImVec2 a(c.x, p0.y + cs * 0.18f), b(p0.x + cs * 0.18f, p0.y + cs * 0.82f),
                   d(p0.x + cs * 0.82f, p0.y + cs * 0.82f);
            dl->AddTriangleFilled(a, b, d, rock);
            dl->AddTriangleFilled(a, ImVec2(c.x - cs * 0.12f, p0.y + cs * 0.40f),
                                  ImVec2(c.x + cs * 0.12f, p0.y + cs * 0.40f), snow);
            break;
        }
        case gns::Terrain::Sand: {
            ImU32 dot = IM_COL32(196, 176, 116, 255);
            dl->AddCircleFilled(ImVec2(c.x - cs * 0.18f, c.y - cs * 0.10f), 1.5f, dot);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.12f, c.y + cs * 0.05f), 1.5f, dot);
            dl->AddCircleFilled(ImVec2(c.x, c.y - cs * 0.20f), 1.5f, dot);
            break;
        }
        case gns::Terrain::Swamp: {
            ImU32 murk = IM_COL32(72, 92, 60, 255);
            dl->AddCircleFilled(ImVec2(c.x - cs * 0.15f, c.y), cs * 0.10f, murk, 6);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.15f, c.y + cs * 0.10f), cs * 0.08f, murk, 6);
            break;
        }
        case gns::Terrain::Road: {
            ImU32 dash = IM_COL32(232, 222, 182, 200);
            dl->AddLine(ImVec2(c.x, p0.y + cs * 0.20f), ImVec2(c.x, p0.y + cs * 0.80f), dash, 1.5f);
            break;
        }
        default: break;
    }
}

// Display names, indexed by ObjectType value (must match the enum order).
static const char* kObjectNames[] = {
    "Door", "Table", "Chair", "Chest", "Fireplace", "Cabinet", "Box", "Bar", "Barrel", "Bed"};

// Draw a placed prop centred at `ctr`, fitting roughly in an `s`-sized box, rotated
// `rotDeg` degrees clockwise. All geometry is expressed in local fraction-of-s coords
// and passed through P() so the whole icon rotates about its centre.
static void drawObjectIcon(ImDrawList* dl, ImVec2 ctr, float s, int type, float rotDeg, bool selected) {
    const ImU32 wood  = IM_COL32(150, 102, 58, 255);
    const ImU32 wood2 = IM_COL32(108, 72, 40, 255);
    const ImU32 line  = IM_COL32(54, 36, 22, 255);
    const ImU32 stone = IM_COL32(122, 122, 128, 255);
    const ImU32 metal = IM_COL32(186, 188, 196, 255);
    float ang = rotDeg * 3.14159265f / 180.0f;
    float ca = std::cos(ang), sa = std::sin(ang);
    auto P = [&](float lx, float ly) {
        float wx = lx * s, wy = ly * s;
        return ImVec2(ctr.x + wx * ca - wy * sa, ctr.y + wx * sa + wy * ca);
    };
    auto R = [&](float ax, float ay, float bx, float by, ImU32 col) {
        dl->AddQuadFilled(P(ax, ay), P(bx, ay), P(bx, by), P(ax, by), col);
    };
    auto Ro = [&](float ax, float ay, float bx, float by, ImU32 col) {
        dl->AddQuad(P(ax, ay), P(bx, ay), P(bx, by), P(ax, by), col, 1.5f);
    };
    auto L = [&](float x1, float y1, float x2, float y2, ImU32 col, float th) {
        dl->AddLine(P(x1, y1), P(x2, y2), col, th);
    };
    auto C = [&](float cx, float cy, float r, ImU32 col) {
        dl->AddCircleFilled(P(cx, cy), r * s, col);
    };
    switch (static_cast<gns::ObjectType>(type)) {
        case gns::ObjectType::Door:
            R(-0.16f, -0.46f, 0.16f, 0.46f, wood); Ro(-0.16f, -0.46f, 0.16f, 0.46f, line);
            C(0.08f, 0.0f, 0.045f, metal);
            break;
        case gns::ObjectType::Table:
            R(-0.40f, -0.22f, 0.40f, 0.22f, wood); Ro(-0.40f, -0.22f, 0.40f, 0.22f, line);
            R(-0.38f, 0.22f, -0.30f, 0.40f, wood2); R(0.30f, 0.22f, 0.38f, 0.40f, wood2);
            break;
        case gns::ObjectType::Chair:
            R(-0.22f, -0.10f, 0.22f, 0.30f, wood); R(-0.22f, -0.34f, 0.22f, -0.22f, wood2);
            break;
        case gns::ObjectType::Chest:
            R(-0.34f, -0.10f, 0.34f, 0.32f, wood); R(-0.34f, -0.26f, 0.34f, -0.10f, wood2);
            Ro(-0.34f, -0.26f, 0.34f, 0.32f, line);
            R(-0.05f, -0.16f, 0.05f, 0.02f, metal);
            break;
        case gns::ObjectType::Fireplace:
            R(-0.40f, -0.40f, 0.40f, 0.42f, stone); R(-0.24f, -0.10f, 0.24f, 0.42f, IM_COL32(30, 24, 22, 255));
            dl->AddTriangleFilled(P(0.0f, 0.02f), P(-0.12f, 0.40f), P(0.12f, 0.40f), IM_COL32(240, 150, 40, 255));
            break;
        case gns::ObjectType::Cabinet:
            R(-0.28f, -0.46f, 0.28f, 0.46f, wood); Ro(-0.28f, -0.46f, 0.28f, 0.46f, line);
            L(0.0f, -0.46f, 0.0f, 0.46f, line, 1.5f);
            C(-0.06f, 0.0f, 0.035f, metal); C(0.06f, 0.0f, 0.035f, metal);
            break;
        case gns::ObjectType::Box:
            R(-0.30f, -0.30f, 0.30f, 0.30f, wood); Ro(-0.30f, -0.30f, 0.30f, 0.30f, line);
            L(-0.30f, -0.30f, 0.30f, 0.30f, line, 1.2f);
            L(0.30f, -0.30f, -0.30f, 0.30f, line, 1.2f);
            break;
        case gns::ObjectType::Bar:
            R(-0.46f, -0.16f, 0.46f, 0.20f, wood); R(-0.46f, -0.16f, 0.46f, -0.06f, wood2);
            Ro(-0.46f, -0.16f, 0.46f, 0.20f, line);
            break;
        case gns::ObjectType::Barrel:
            dl->AddEllipseFilled(ctr, ImVec2(0.24f * s, 0.40f * s), wood, ang);
            dl->AddEllipse(ctr, ImVec2(0.24f * s, 0.40f * s), line, ang, 0, 1.5f);
            L(-0.24f, -0.12f, 0.24f, -0.12f, line, 1.2f);
            L(-0.24f, 0.12f, 0.24f, 0.12f, line, 1.2f);
            break;
        case gns::ObjectType::Bed:
            R(-0.40f, -0.30f, 0.40f, 0.34f, wood); R(-0.40f, -0.30f, -0.16f, 0.34f, IM_COL32(220, 220, 230, 255));
            Ro(-0.40f, -0.30f, 0.40f, 0.34f, line);
            break;
        default: break;
    }
    if (selected) {
        dl->AddRect(ImVec2(ctr.x - 0.5f * s, ctr.y - 0.52f * s),
                    ImVec2(ctr.x + 0.5f * s, ctr.y + 0.52f * s),
                    IM_COL32(255, 230, 0, 255), 2.0f, 0, 2.0f);
    }
}

// ---------------------------------------------------------------------------
// Editor state
// ---------------------------------------------------------------------------
enum class Tool { Select, PaintTerrain, AssignArea, Erase, PlaceControlPoint, PlaceObject, PlaceText };

// Fixed three-pane layout: each panel is pinned to a dedicated region every frame
// (this build of ImGui has no docking branch), so panels never float or stack.
static constexpr float kLeftPaneW = 300.0f;
static constexpr float kRightPaneW = 360.0f;
static const ImGuiWindowFlags kPaneFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

struct App {
    gns::Module mod;
    std::string path;          // current .gnsmod file ("" = unsaved)
    bool dirty = false;

    int currentMapId = 0;      // map shown in the canvas
    // The active area: shown in the inspector, painted by Assign Area, cyan border on
    // the map. While an area is active, terrain painting is disabled (0 = none).
    int selectedAreaId = 0;

    Tool tool = Tool::PaintTerrain;
    int paintTerrain = static_cast<int>(gns::Terrain::Grass);
    int brushSize = 1;         // square brush edge in cells (1,3,5,...) for paint tools

    int paintObjectType = static_cast<int>(gns::ObjectType::Table);
    int selectedObjectId = 0;  // currently selected placed object (0 = none)
    bool draggingObject = false;

    // Text tool.
    std::string paintTextBuf = "Label";
    std::uint32_t paintTextColor = 0xFFFF66FFu;
    float paintTextSize = 20.0f;
    int selectedTextId = 0;
    bool draggingText = false;

    // Control point tool.
    int selectedControlPointId = 0;
    bool draggingCp = false;

    // One undo snapshot per drag (set when a move actually starts).
    bool dragSnapshotTaken = false;
    bool strokeOpen = false;   // a paint stroke is in progress (one snapshot/stroke)

    // Hand-tool (Select) panning vs click.
    bool handDragging = false;
    ImVec2 handPressPos{0, 0};

    float cellPx = 22.0f;      // zoom (pixels per cell)
    bool fitRequested = false;  // canvas fits whole map next frame

    // Undo history (whole-module snapshots).
    std::vector<gns::Module> undo;

    // Pending modal dialogs.
    int confirmRemoveMapId = 0;   // map awaiting delete confirmation (0 = none)
    bool wantClose = false;       // close requested; confirm if dirty
    int renameMapId = 0;          // map whose name is being edited inline (0 = none)

    // gns.db reference pickers (optional).
    bool haveRepo = false;
    std::vector<std::string> monsterNames;
};

static gns::Map* currentMap(App& app) { return app.mod.mapById(app.currentMapId); }

// Coarse-grid label (e.g. "A1") for a fine cell, from the overlay grid.
static std::string coarseLabel(const gns::Map& m, int cx, int cy) {
    int ow = m.overlayW > 0 ? m.overlayW : 1;
    int oh = m.overlayH > 0 ? m.overlayH : 1;
    int col = m.gridW > 0 ? cx * ow / m.gridW : 0;
    int row = m.gridH > 0 ? cy * oh / m.gridH : 0;
    col = std::clamp(col, 0, ow - 1);
    row = std::clamp(row, 0, oh - 1);
    return std::string(1, static_cast<char>('A' + col)) + std::to_string(row + 1);
}

// Average fine-cell position owned by an area, or {-1,-1} if it owns none.
static void areaCentroid(const gns::Map& m, int areaId, int& outX, int& outY) {
    long sx = 0, sy = 0, n = 0;
    for (int y = 0; y < m.gridH; ++y)
        for (int x = 0; x < m.gridW; ++x)
            if (m.cellArea[(size_t)y * m.gridW + x] == areaId) { sx += x; sy += y; ++n; }
    if (n == 0) { outX = outY = -1; return; }
    outX = (int)(sx / n);
    outY = (int)(sy / n);
}

// Filename portion of a path (after the last / or \), for compact display.
static std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// Re-derive every area's coordinate label (e.g. "A1") from its centroid + overlay grid.
static void relabelAreas(gns::Map& m) {
    for (auto& a : m.areas) {
        int ax, ay; areaCentroid(m, a.id, ax, ay);
        if (ax >= 0) a.label = coarseLabel(m, ax, ay);
    }
}

// Does the cell at (cx,cy) have a 4-neighbour already belonging to areaId?
static bool hasAdjacentSameArea(const gns::Map& m, int cx, int cy, int areaId) {
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
        int x = cx + dx[k], y = cy + dy[k];
        if (x >= 0 && y >= 0 && x < m.gridW && y < m.gridH &&
            m.cellArea[(size_t)y * m.gridW + x] == areaId)
            return true;
    }
    return false;
}

static bool areaHasCells(const gns::Map& m, int areaId) {
    for (int v : m.cellArea) if (v == areaId) return true;
    return false;
}

// Keep only the largest 4-connected component of areaId; un-assign the rest (terrain
// is left untouched). Maintains the "an area is one connected blob" invariant.
static void pruneAreaConnectivity(gns::Map& m, int areaId) {
    int W = m.gridW, H = m.gridH;
    std::vector<int> comp((size_t)W * H, -1);
    std::vector<int> sizes;
    std::vector<int> stack;
    for (int i = 0; i < W * H; ++i) {
        if (m.cellArea[i] != areaId || comp[i] != -1) continue;
        int id = (int)sizes.size(), count = 0;
        stack.push_back(i);
        comp[i] = id;
        while (!stack.empty()) {
            int c = stack.back(); stack.pop_back();
            ++count;
            int cx = c % W, cy = c / W;
            const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int x = cx + dx[k], y = cy + dy[k];
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                int n = y * W + x;
                if (m.cellArea[n] == areaId && comp[n] == -1) { comp[n] = id; stack.push_back(n); }
            }
        }
        sizes.push_back(count);
    }
    if (sizes.size() <= 1) return;   // already connected (or empty)
    int best = 0;
    for (int i = 1; i < (int)sizes.size(); ++i) if (sizes[i] > sizes[best]) best = i;
    for (int i = 0; i < W * H; ++i)
        if (m.cellArea[i] == areaId && comp[i] != best) m.cellArea[i] = 0;
}

static gns::Map makeBlankMap(int id, const std::string& name, int w, int h) {
    gns::Map m;
    m.id = id;
    m.name = name;
    m.gridW = w;
    m.gridH = h;
    m.overlayW = std::max(1, w / 4);
    m.overlayH = std::max(1, h / 4);
    m.cells.assign((size_t)w * h, static_cast<int>(gns::Terrain::Empty));
    m.cellArea.assign((size_t)w * h, 0);
    return m;
}

static void newModule(App& app) {
    app.mod = gns::Module{};
    app.mod.name = "Untitled Module";
    app.mod.maps.push_back(makeBlankMap(1, "Level 1", 32, 24));
    app.mod.startMapId = 1;
    app.path.clear();
    app.dirty = false;
    app.currentMapId = 1;
    app.selectedAreaId = 0;
    app.selectedObjectId = 0;
    app.selectedTextId = 0;
    app.selectedControlPointId = 0;
    app.undo.clear();
}

// ---- Undo (whole-module snapshots) ----------------------------------------
static void pushUndo(App& app) {
    app.undo.push_back(app.mod);
    if (app.undo.size() > 64) app.undo.erase(app.undo.begin());
}
static void doUndo(App& app) {
    if (app.undo.empty()) return;
    app.mod = std::move(app.undo.back());
    app.undo.pop_back();
    app.dirty = true;
    // Clamp selections that may no longer exist.
    if (!app.mod.mapById(app.currentMapId))
        app.currentMapId = app.mod.maps.empty() ? 0 : app.mod.maps.front().id;
    if (!app.mod.areaById(app.selectedAreaId)) app.selectedAreaId = 0;
    app.selectedObjectId = 0;
    app.selectedTextId = 0;
    app.selectedControlPointId = 0;
}

static void resizeMapGrid(gns::Map& m, int w, int h) {
    w = std::clamp(w, 4, 256);
    h = std::clamp(h, 4, 256);
    std::vector<int> cells((size_t)w * h, static_cast<int>(gns::Terrain::Empty));
    std::vector<int> cellArea((size_t)w * h, 0);
    for (int y = 0; y < std::min(h, m.gridH); ++y)
        for (int x = 0; x < std::min(w, m.gridW); ++x) {
            cells[(size_t)y * w + x] = m.cells[(size_t)y * m.gridW + x];
            cellArea[(size_t)y * w + x] = m.cellArea[(size_t)y * m.gridW + x];
        }
    m.gridW = w; m.gridH = h;
    m.cells = std::move(cells);
    m.cellArea = std::move(cellArea);
    relabelAreas(m);   // grid coordinates shift when the map is resized (#8)
}

// ---------------------------------------------------------------------------
// gns.db reference data (optional)
// ---------------------------------------------------------------------------
static void loadReference(App& app) {
    char* base = SDL_GetBasePath();
    std::string dbPath = (base ? base : "") + std::string("data/gns.db");
    if (base) SDL_free(base);
    try {
        gns::Database db(dbPath);
        gns::Repository repo(db);
        for (const auto& mdef : repo.monsters()) app.monsterNames.push_back(mdef.name);
        std::sort(app.monsterNames.begin(), app.monsterNames.end());
        app.haveRepo = true;
    } catch (const std::exception&) {
        app.haveRepo = false;   // fall back to free-text fields
    }
}

// A text field with an optional "pick from list" combo that overwrites it.
static bool pickerField(const char* label, std::string* value,
                        const std::vector<std::string>& options) {
    bool changed = InputStr(label, value);
    if (!options.empty()) {
        ImGui::SameLine();
        std::string popup = std::string("pick##") + label;
        if (ImGui::SmallButton((std::string("Pick##") + label).c_str()))
            ImGui::OpenPopup(popup.c_str());
        if (ImGui::BeginPopup(popup.c_str())) {
            for (const auto& opt : options)
                if (ImGui::Selectable(opt.c_str())) { *value = opt; changed = true; }
            ImGui::EndPopup();
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Persistence actions
// ---------------------------------------------------------------------------
static std::string gStatus;   // shown in the menu bar

static void doSaveAs(App& app);
static void doSave(App& app) {
    if (app.path.empty()) { doSaveAs(app); return; }
    try {
        gns::saveModule(app.mod, app.path);
        app.dirty = false;
        gStatus = "Saved " + baseName(app.path);
    } catch (const std::exception& e) {
        gStatus = std::string("Save failed: ") + e.what();
    }
}
static void doSaveAs(App& app) {
    std::string p = fileDialog(true);
    if (p.empty()) return;
    app.path = p;
    doSave(app);
}
static void doOpen(App& app) {
    std::string p = fileDialog(false);
    if (p.empty()) return;
    try {
        app.mod = gns::loadModule(p);
        app.path = p;
        app.dirty = false;
        app.currentMapId = app.mod.maps.empty() ? 0 : app.mod.maps.front().id;
        app.selectedAreaId = 0;
        app.selectedObjectId = 0;
        app.selectedTextId = 0;
        app.selectedControlPointId = 0;
        app.undo.clear();
        gStatus = "Opened " + baseName(p);
    } catch (const std::exception& e) {
        gStatus = std::string("Open failed: ") + e.what();
    }
}

// ---------------------------------------------------------------------------
// UI: menu bar
// ---------------------------------------------------------------------------
static void drawMenuBar(App& app) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) newModule(app);
            if (ImGui::MenuItem("Open…", "Ctrl+O")) doOpen(app);
            if (ImGui::MenuItem("Save", "Ctrl+S")) doSave(app);
            if (ImGui::MenuItem("Save As…")) doSaveAs(app);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) app.wantClose = true;
            ImGui::EndMenu();
        }
        std::string title = (app.path.empty() ? "untitled" : baseName(app.path)) +
                            (app.dirty ? " *" : "");
        ImGui::Separator();
        ImGui::TextUnformatted(title.c_str());
        if (!gStatus.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", gStatus.c_str());
        }
        ImGui::EndMainMenuBar();
    }
}

// ---------------------------------------------------------------------------
// UI: tools window
// ---------------------------------------------------------------------------
static void drawToolsWindow(App& app) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kLeftPaneW, vp->WorkSize.y), ImGuiCond_Always);
    ImGui::Begin("Tools", nullptr, kPaneFlags);

    bool areaActive = app.selectedAreaId != 0;

    ImGui::SeparatorText("Tool");
    int t = static_cast<int>(app.tool);
    const char* tools[] = {"Hand (select)", "Paint Terrain", "Assign Area", "Erase",
                           "Control Point", "Object", "Text"};
    for (int i = 0; i < IM_ARRAYSIZE(tools); ++i) {
        // Assign Area needs an active area. Paint Terrain stays clickable so it
        // doubles as "leave the active area and go back to painting terrain".
        bool disabled = (i == (int)Tool::AssignArea && !areaActive);
        if (disabled) ImGui::BeginDisabled();
        if (ImGui::RadioButton(tools[i], &t, i)) {
            app.tool = static_cast<Tool>(i);
            if (app.tool == Tool::PaintTerrain) app.selectedAreaId = 0;   // exit area editing
        }
        if (disabled) ImGui::EndDisabled();
    }
    if (app.tool == Tool::AssignArea && !areaActive) app.tool = Tool::PaintTerrain;
    if (app.selectedAreaId != 0)
        ImGui::TextDisabled("Editing area '%s'. Click Paint Terrain to exit.",
            app.mod.areaById(app.selectedAreaId)->label.c_str());

    if (app.tool == Tool::PaintTerrain) {
        ImGui::SeparatorText("Terrain");
        for (int i = 0; i < IM_ARRAYSIZE(kTerrainNames); ++i) {
            ImGui::PushID(i);
            float col[4]; rgbaToFloat4(terrainColor(i), col);
            ImGui::ColorButton("##sw", ImVec4(col[0], col[1], col[2], 1.0f),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(16, 16));
            ImGui::SameLine();
            if (ImGui::Selectable(kTerrainNames[i], app.paintTerrain == i)) app.paintTerrain = i;
            ImGui::PopID();
        }
    }

    if (app.tool == Tool::PlaceObject) {
        ImGui::SeparatorText("Object");
        for (int i = 0; i < IM_ARRAYSIZE(kObjectNames); ++i)
            if (ImGui::Selectable(kObjectNames[i], app.paintObjectType == i)) app.paintObjectType = i;
        ImGui::TextDisabled("Click to place. Drag to move.\nR rotates 90\xc2\xb0. Del deletes.");

        // Placed-objects list — click to (re)select one, on the map or after a tool switch.
        ImGui::SeparatorText("Placed objects");
        if (gns::Map* m = currentMap(app)) {
            if (m->objects.empty()) {
                ImGui::TextDisabled("None yet — click the map to place.");
            } else {
                for (auto& o : m->objects) {
                    ImGui::PushID(o.id);
                    std::string lbl = "#" + std::to_string(o.id) + "  " + kObjectNames[o.type];
                    if (ImGui::Selectable(lbl.c_str(), o.id == app.selectedObjectId))
                        app.selectedObjectId = o.id;
                    ImGui::PopID();
                }
            }
        }

        // Find the selected object on the current map to edit its rotation.
        gns::MapObject* sel = nullptr;
        if (gns::Map* m = currentMap(app))
            for (auto& o : m->objects) if (o.id == app.selectedObjectId) { sel = &o; break; }

        if (sel) {
            ImGui::SeparatorText("Selected");
            ImGui::Text("%s", kObjectNames[sel->type]);
            if (ImGui::SliderFloat("Rotation", &sel->rotationDeg, 0.0f, 360.0f, "%.0f\xc2\xb0"))
                app.dirty = true;
            if (ImGui::Button("-90")) { sel->rotationDeg -= 90.0f; app.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Button("+90")) { sel->rotationDeg += 90.0f; app.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Button("0")) { sel->rotationDeg = 0.0f; app.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                gns::Map* m = currentMap(app);
                auto& v = m->objects;
                v.erase(std::remove_if(v.begin(), v.end(),
                    [&](const gns::MapObject& o) { return o.id == app.selectedObjectId; }), v.end());
                app.selectedObjectId = 0;
                app.dirty = true;
                sel = nullptr;   // erased — don't touch it below
            }
            // Normalise so the slider stays in range.
            if (sel) {
                while (sel->rotationDeg < 0.0f) sel->rotationDeg += 360.0f;
                while (sel->rotationDeg >= 360.0f) sel->rotationDeg -= 360.0f;
            }
        } else {
            ImGui::TextDisabled("Select an object to rotate it.");
        }
    }

    if (app.tool == Tool::PlaceText) {
        ImGui::SeparatorText("Text");
        ImGui::TextDisabled("Click to place. Drag to move. Del deletes.");
        ImGui::TextUnformatted("New text");
        ImGui::SetNextItemWidth(-1);
        InputStr("##newtext", &app.paintTextBuf);
        float tc[4]; rgbaToFloat4(app.paintTextColor, tc);
        if (ImGui::ColorEdit3("Color##new", tc)) { tc[3] = 1.0f; app.paintTextColor = float4ToRgba(tc); }
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Size##new", &app.paintTextSize, 8.0f, 96.0f, "%.0f px");

        ImGui::SeparatorText("Placed text");
        gns::MapText* selT = nullptr;
        if (gns::Map* m = currentMap(app)) {
            if (m->texts.empty()) ImGui::TextDisabled("None yet — click the map.");
            for (auto& tx : m->texts) {
                ImGui::PushID(tx.id);
                std::string lbl = "#" + std::to_string(tx.id) + "  " +
                                  (tx.text.empty() ? "(empty)" : tx.text);
                if (ImGui::Selectable(lbl.c_str(), tx.id == app.selectedTextId)) app.selectedTextId = tx.id;
                ImGui::PopID();
            }
            for (auto& tx : m->texts) if (tx.id == app.selectedTextId) { selT = &tx; break; }
        }
        if (selT) {
            ImGui::SeparatorText("Selected text");
            ImGui::SetNextItemWidth(-1);
            if (InputStr("##edittext", &selT->text)) app.dirty = true;
            float sc[4]; rgbaToFloat4(selT->color, sc);
            if (ImGui::ColorEdit3("Color", sc)) { sc[3] = 1.0f; selT->color = float4ToRgba(sc); app.dirty = true; }
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderFloat("Size", &selT->sizePx, 8.0f, 96.0f, "%.0f px")) app.dirty = true;
            if (ImGui::Button("Delete##text")) {
                pushUndo(app);
                gns::Map* m = currentMap(app);
                auto& v = m->texts;
                v.erase(std::remove_if(v.begin(), v.end(),
                    [&](const gns::MapText& t) { return t.id == app.selectedTextId; }), v.end());
                app.selectedTextId = 0; app.dirty = true;
            }
        }
    }

    if (app.tool == Tool::PlaceControlPoint) {
        ImGui::SeparatorText("Control Point");
        ImGui::TextDisabled("Click to place. Drag to move. Del deletes.");
        ImGui::SeparatorText("Placed on this map");
        gns::ControlPoint* selCp = nullptr;
        bool any = false;
        for (auto& cp : app.mod.controlPoints) {
            if (cp.mapId != app.currentMapId) continue;
            any = true;
            ImGui::PushID(cp.id);
            std::string lbl = "#" + std::to_string(cp.id) + "  " + cp.name;
            if (ImGui::Selectable(lbl.c_str(), cp.id == app.selectedControlPointId)) app.selectedControlPointId = cp.id;
            ImGui::PopID();
        }
        if (!any) ImGui::TextDisabled("None yet — click the map.");
        for (auto& cp : app.mod.controlPoints) if (cp.id == app.selectedControlPointId) { selCp = &cp; break; }
        if (selCp) {
            ImGui::SeparatorText("Selected control point");
            if (InputStr("Name##cp", &selCp->name)) app.dirty = true;
            if (InputStrMultiline("##cpdesc2", &selCp->description, ImVec2(-1, 40))) app.dirty = true;
            if (ImGui::Button("Delete##cp")) {
                pushUndo(app);
                int del = app.selectedControlPointId;
                auto& v = app.mod.controlPoints;
                v.erase(std::remove_if(v.begin(), v.end(),
                    [&](const gns::ControlPoint& c) { return c.id == del; }), v.end());
                for (auto& mm : app.mod.maps)
                    for (auto& a : mm.areas) {
                        auto& pre = a.prerequisiteControlPointIds;
                        pre.erase(std::remove(pre.begin(), pre.end(), del), pre.end());
                    }
                app.selectedControlPointId = 0; app.dirty = true;
            }
        }
    }

    bool paintTool = app.tool == Tool::PaintTerrain || app.tool == Tool::AssignArea ||
                     app.tool == Tool::Erase;
    if (paintTool) {
        ImGui::SeparatorText("Brush");
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt("Size", &app.brushSize, 1, 15))
            app.brushSize = app.brushSize | 1;   // keep odd so the brush is centred
    }

    ImGui::SeparatorText("Maps");
    // Rename field for the current map (prominent, top of section).
    if (gns::Map* m = currentMap(app)) {
        ImGui::TextUnformatted("Map name");
        ImGui::SetNextItemWidth(-1);
        if (InputStr("##mapname", &m->name)) app.dirty = true;
    }
    for (auto& mm : app.mod.maps) {
        bool sel = (mm.id == app.currentMapId);
        std::string lbl = (mm.name.empty() ? "(unnamed)" : mm.name) + "##map" + std::to_string(mm.id);
        if (ImGui::Selectable(lbl.c_str(), sel)) app.currentMapId = mm.id;
    }
    if (ImGui::Button("Add Map")) {
        pushUndo(app);
        int id = app.mod.nextMapId();
        app.mod.maps.push_back(makeBlankMap(id, "Level " + std::to_string(id), 32, 24));
        app.currentMapId = id;
        app.dirty = true;
    }
    if (app.mod.maps.size() > 1) {
        ImGui::SameLine();
        if (ImGui::Button("Remove Map")) app.confirmRemoveMapId = app.currentMapId;
    }
    if (gns::Map* m = currentMap(app)) {
        // Resize immediately on +/- or text entry; resizeMapGrid clamps to 4..256,
        // preserves cells, and re-labels areas with their new grid coordinates.
        int w = m->gridW;
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Cols", &w)) { pushUndo(app); resizeMapGrid(*m, w, m->gridH); app.dirty = true; }
        int h = m->gridH;
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Rows", &h)) { pushUndo(app); resizeMapGrid(*m, m->gridW, h); app.dirty = true; }
        ImGui::SetNextItemWidth(100); if (ImGui::InputInt("Overlay cols", &m->overlayW)) { m->overlayW = std::max(1, m->overlayW); relabelAreas(*m); app.dirty = true; }
        ImGui::SetNextItemWidth(100); if (ImGui::InputInt("Overlay rows", &m->overlayH)) { m->overlayH = std::max(1, m->overlayH); relabelAreas(*m); app.dirty = true; }
    }

    // Remove-map confirmation modal (#2).
    if (app.confirmRemoveMapId != 0) ImGui::OpenPopup("Remove map?");
    if (ImGui::BeginPopupModal("Remove map?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        gns::Map* dm = app.mod.mapById(app.confirmRemoveMapId);
        ImGui::Text("Are you sure you want to delete map '%s'?", dm ? dm->name.c_str() : "?");
        if (ImGui::Button("Delete") && dm) {
            pushUndo(app);
            int delId = app.confirmRemoveMapId;
            auto& v = app.mod.maps;
            v.erase(std::remove_if(v.begin(), v.end(),
                       [&](const gns::Map& x) { return x.id == delId; }), v.end());
            app.currentMapId = v.empty() ? 0 : v.front().id;
            app.selectedAreaId = 0;
            app.dirty = true;
            app.confirmRemoveMapId = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { app.confirmRemoveMapId = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Areas");
    if (gns::Map* m = currentMap(app)) {
        ImGui::TextDisabled("Click an area to make it active.");
        for (auto& a : m->areas) {
            ImGui::PushID(a.id);
            float col[4]; rgbaToFloat4(a.color, col);
            ImGui::ColorButton("##sw", ImVec4(col[0], col[1], col[2], 1.0f),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(14, 14));
            ImGui::SameLine();
            std::string lbl = (a.label.empty() ? "(area)" : a.label) +
                              (a.name.empty() ? "" : " — " + a.name);
            if (ImGui::Selectable(lbl.c_str(), a.id == app.selectedAreaId)) {
                app.selectedAreaId = a.id;
                if (app.tool == Tool::PaintTerrain) app.tool = Tool::AssignArea;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Area")) {
            pushUndo(app);
            gns::Area a;
            a.id = app.mod.nextAreaId();
            a.color = kAreaPalette[(a.id - 1) % IM_ARRAYSIZE(kAreaPalette)];
            a.label = "A" + std::to_string(a.id);
            m->areas.push_back(a);
            app.selectedAreaId = a.id;
            app.tool = Tool::AssignArea;
            app.dirty = true;
        }
    }

    ImGui::SeparatorText("View");
    ImGui::SliderFloat("Zoom", &app.cellPx, 2.0f, 64.0f, "%.0f px");
    if (ImGui::Button("Fit Map")) app.fitRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Actual Size")) app.cellPx = 22.0f;
    ImGui::TextDisabled("Pan: scrollbars or middle-drag.\nZoom: slider or Ctrl+wheel.");

    ImGui::End();
}

// ---------------------------------------------------------------------------
// UI: map canvas
// ---------------------------------------------------------------------------
// Paint-tool action on one cell (PaintTerrain / AssignArea / Erase). Hand-select,
// objects, text, and control points are handled directly in the canvas interaction.
static void applyToolAtCell(App& app, gns::Map& m, int cx, int cy) {
    size_t idx = (size_t)cy * m.gridW + cx;
    switch (app.tool) {
        case Tool::PaintTerrain:
            // Disabled while an area is active — you modify the area instead.
            if (app.selectedAreaId == 0 && m.cells[idx] != app.paintTerrain) {
                m.cells[idx] = app.paintTerrain; app.dirty = true;
            }
            break;
        case Tool::AssignArea:
            // Connectivity (#6): only the seed cell, or a cell 4-adjacent to the area's
            // existing cells, may be assigned — disconnected blobs can't start.
            if (app.selectedAreaId != 0 && m.cellArea[idx] != app.selectedAreaId &&
                (!areaHasCells(m, app.selectedAreaId) ||
                 hasAdjacentSameArea(m, cx, cy, app.selectedAreaId))) {
                m.cellArea[idx] = app.selectedAreaId;
                if (m.cells[idx] == static_cast<int>(gns::Terrain::Empty))
                    m.cells[idx] = static_cast<int>(gns::Terrain::Floor);
                app.dirty = true;
            }
            break;
        case Tool::Erase:
            if (m.cells[idx] != static_cast<int>(gns::Terrain::Empty) || m.cellArea[idx] != 0) {
                m.cells[idx] = static_cast<int>(gns::Terrain::Empty);
                m.cellArea[idx] = 0;
                app.dirty = true;
            }
            break;
        default: break;
    }
}

// Apply the current paint tool over a square brush centred on (cx,cy).
static void applyBrush(App& app, gns::Map& m, int cx, int cy) {
    int r = app.brushSize / 2;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && y >= 0 && x < m.gridW && y < m.gridH)
                applyToolAtCell(app, m, x, y);
        }
}

static void drawCanvasWindow(App& app) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float centerW = std::max(80.0f, vp->WorkSize.x - kLeftPaneW - kRightPaneW);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + kLeftPaneW, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(centerW, vp->WorkSize.y), ImGuiCond_Always);

    gns::Map* mp = currentMap(app);
    std::string mapName = mp ? (mp->name.empty() ? "(unnamed)" : mp->name) : "(none)";
    // Display name in the title bar; stable id after ### so renames don't reset the pane.
    std::string title = "Map \xE2\x80\x94 " + mapName + "###mapwin";
    ImGui::Begin(title.c_str(), nullptr, kPaneFlags);

    if (!mp) { ImGui::TextDisabled("No map. Add one in Tools."); ImGui::End(); return; }
    gns::Map& m = *mp;

    // Toolbar.
    ImGui::TextUnformatted(mapName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%d x %d cells @ %.0f px)", m.gridW, m.gridH, app.cellPx);
    ImGui::SameLine();
    if (ImGui::SmallButton("Zoom -")) app.cellPx = std::clamp(app.cellPx * 0.8f, 2.0f, 64.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Zoom +")) app.cellPx = std::clamp(app.cellPx * 1.25f, 2.0f, 64.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) app.fitRequested = true;
    ImGui::Separator();

    ImVec2 region = ImGui::GetContentRegionAvail();
    if (region.x < 32) region.x = 32;
    if (region.y < 32) region.y = 32;
    if (app.fitRequested) {
        app.cellPx = std::clamp(std::min(region.x / m.gridW, region.y / m.gridH), 2.0f, 64.0f);
        app.fitRequested = false;
    }
    float cs = app.cellPx;

    // Scrolling viewport: the canvas is sized to the full map, so ImGui supplies
    // horizontal + vertical scrollbars when zoomed in beyond the visible area.
    ImGui::BeginChild("canvasScroll", region, true, ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 origin = ImGui::GetCursorScreenPos();   // content top-left (scroll-adjusted)
    ImGui::InvisibleButton("canvas", ImVec2(m.gridW * cs, m.gridH * cs),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    // Middle/right-drag pans by moving the scroll; Ctrl+wheel zooms (plain wheel scrolls).
    if (active && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                   ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
    }
    if (hovered && io.KeyCtrl && io.MouseWheel != 0.0f)
        app.cellPx = std::clamp(app.cellPx * (io.MouseWheel > 0 ? 1.1f : 0.9f), 2.0f, 64.0f);

    // Mouse -> cell (int) and sub-grid position (float, in cell units).
    float mxf = (io.MousePos.x - origin.x) / cs;
    float myf = (io.MousePos.y - origin.y) / cs;
    int cx = (int)std::floor(mxf);
    int cy = (int)std::floor(myf);
    bool cellValid = cx >= 0 && cy >= 0 && cx < m.gridW && cy < m.gridH;
    bool inBounds = mxf >= 0 && myf >= 0 && mxf <= m.gridW && myf <= m.gridH;
    bool paintTool = app.tool == Tool::PaintTerrain || app.tool == Tool::AssignArea ||
                     app.tool == Tool::Erase;
    bool typingGuard = io.WantTextInput;
    bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool leftClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool leftRelease = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    auto cellAreaAt = [&](float fx, float fy) -> int {
        int x = (int)std::floor(fx), y = (int)std::floor(fy);
        if (x < 0 || y < 0 || x >= m.gridW || y >= m.gridH) return 0;
        return m.cellArea[(size_t)y * m.gridW + x];
    };

    if (paintTool && cellValid) {
        if (leftClick) { pushUndo(app); app.strokeOpen = true; }   // one undo entry per stroke
        if (active && leftDown) applyBrush(app, m, cx, cy);
        if (leftRelease && app.strokeOpen) {
            app.strokeOpen = false;
            if (app.tool == Tool::Erase)                       // erasing may split an area (#6)
                for (auto& a : m.areas) pruneAreaConnectivity(m, a.id);
        }
        if (!typingGuard && ImGui::IsKeyPressed(ImGuiKey_Delete)) doUndo(app);   // DELETE undoes (#4)
    } else if (app.tool == Tool::Select) {
        // Hand: left-drag pans the map; a click without drag selects an area (#10).
        if (leftClick) { app.handDragging = false; app.handPressPos = io.MousePos; }
        if (active && leftDown) {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            float dx = io.MousePos.x - app.handPressPos.x, dy = io.MousePos.y - app.handPressPos.y;
            if (dx * dx + dy * dy > 16.0f) app.handDragging = true;
        }
        if (leftRelease && !app.handDragging && cellValid)
            app.selectedAreaId = m.cellArea[(size_t)cy * m.gridW + cx];
    } else if (app.tool == Tool::PlaceObject) {
        if (hovered && inBounds && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = -1; float best = 0.36f;
            for (size_t i = 0; i < m.objects.size(); ++i) {
                float dx = m.objects[i].x - mxf, dy = m.objects[i].y - myf;
                if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; hit = (int)i; }
            }
            if (hit >= 0) { app.selectedObjectId = m.objects[hit].id; app.dragSnapshotTaken = false; }
            else {
                pushUndo(app);
                gns::MapObject o; o.id = app.mod.nextObjectId(); o.type = app.paintObjectType;
                o.x = mxf; o.y = myf;
                m.objects.push_back(o); app.selectedObjectId = o.id; app.dirty = true;
                app.dragSnapshotTaken = true;
            }
            app.draggingObject = true;
        }
        if (app.draggingObject && active && leftDown && app.selectedObjectId != 0) {
            if (!app.dragSnapshotTaken && moved) { pushUndo(app); app.dragSnapshotTaken = true; }
            for (auto& o : m.objects)
                if (o.id == app.selectedObjectId) {
                    o.x = std::clamp(mxf, 0.0f, (float)m.gridW);
                    o.y = std::clamp(myf, 0.0f, (float)m.gridH);
                    app.dirty = true;
                }
        }
        if (!leftDown) { app.draggingObject = false; app.dragSnapshotTaken = false; }
        if (app.selectedObjectId != 0 && !typingGuard) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                pushUndo(app);
                auto& v = m.objects;
                v.erase(std::remove_if(v.begin(), v.end(),
                    [&](const gns::MapObject& o) { return o.id == app.selectedObjectId; }), v.end());
                app.selectedObjectId = 0; app.dirty = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                pushUndo(app);
                for (auto& o : m.objects)
                    if (o.id == app.selectedObjectId) { o.rotationDeg += 90.0f; app.dirty = true; }
            }
        }
    } else if (app.tool == Tool::PlaceText) {
        if (hovered && inBounds && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = -1; float best = 0.6f;
            for (size_t i = 0; i < m.texts.size(); ++i) {
                float dx = m.texts[i].x - mxf, dy = m.texts[i].y - myf;
                if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; hit = (int)i; }
            }
            if (hit >= 0) { app.selectedTextId = m.texts[hit].id; app.dragSnapshotTaken = false; }
            else {
                pushUndo(app);
                gns::MapText tx; tx.id = app.mod.nextTextId(); tx.x = mxf; tx.y = myf;
                tx.text = app.paintTextBuf.empty() ? "text" : app.paintTextBuf;
                tx.color = app.paintTextColor; tx.sizePx = app.paintTextSize;
                m.texts.push_back(tx); app.selectedTextId = tx.id; app.dirty = true;
                app.dragSnapshotTaken = true;
            }
            app.draggingText = true;
        }
        if (app.draggingText && active && leftDown && app.selectedTextId != 0) {
            if (!app.dragSnapshotTaken && moved) { pushUndo(app); app.dragSnapshotTaken = true; }
            for (auto& tx : m.texts)
                if (tx.id == app.selectedTextId) {
                    tx.x = std::clamp(mxf, 0.0f, (float)m.gridW);
                    tx.y = std::clamp(myf, 0.0f, (float)m.gridH);
                    app.dirty = true;
                }
        }
        if (!leftDown) { app.draggingText = false; app.dragSnapshotTaken = false; }
        if (app.selectedTextId != 0 && !typingGuard && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            pushUndo(app);
            auto& v = m.texts;
            v.erase(std::remove_if(v.begin(), v.end(),
                [&](const gns::MapText& t) { return t.id == app.selectedTextId; }), v.end());
            app.selectedTextId = 0; app.dirty = true;
        }
    } else if (app.tool == Tool::PlaceControlPoint) {
        auto cpPos = [&](const gns::ControlPoint& cp, float& px, float& py) -> bool {
            if (cp.x >= 0) { px = cp.x; py = cp.y; return true; }
            int ax, ay; areaCentroid(m, cp.areaId, ax, ay);
            if (ax < 0) return false;
            px = ax + 0.5f; py = ay + 0.5f; return true;
        };
        if (hovered && inBounds && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = -1; float best = 0.36f;
            for (size_t i = 0; i < app.mod.controlPoints.size(); ++i) {
                if (app.mod.controlPoints[i].mapId != m.id) continue;
                float px, py;
                if (!cpPos(app.mod.controlPoints[i], px, py)) continue;
                float dx = px - mxf, dy = py - myf;
                if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; hit = (int)i; }
            }
            if (hit >= 0) { app.selectedControlPointId = app.mod.controlPoints[hit].id; app.dragSnapshotTaken = false; }
            else {
                pushUndo(app);
                gns::ControlPoint cp; cp.id = app.mod.nextControlPointId();
                cp.name = "CP " + std::to_string(cp.id); cp.mapId = m.id;
                cp.x = mxf; cp.y = myf; cp.areaId = cellAreaAt(mxf, myf);
                app.mod.controlPoints.push_back(cp); app.selectedControlPointId = cp.id;
                app.dirty = true; app.dragSnapshotTaken = true;
            }
            app.draggingCp = true;
        }
        if (app.draggingCp && active && leftDown && app.selectedControlPointId != 0) {
            if (!app.dragSnapshotTaken && moved) { pushUndo(app); app.dragSnapshotTaken = true; }
            for (auto& cp : app.mod.controlPoints)
                if (cp.id == app.selectedControlPointId) {
                    cp.x = std::clamp(mxf, 0.0f, (float)m.gridW);
                    cp.y = std::clamp(myf, 0.0f, (float)m.gridH);
                    cp.areaId = cellAreaAt(cp.x, cp.y);
                    app.dirty = true;
                }
        }
        if (!leftDown) { app.draggingCp = false; app.dragSnapshotTaken = false; }
        if (app.selectedControlPointId != 0 && !typingGuard && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            pushUndo(app);
            int del = app.selectedControlPointId;
            auto& v = app.mod.controlPoints;
            v.erase(std::remove_if(v.begin(), v.end(),
                [&](const gns::ControlPoint& c) { return c.id == del; }), v.end());
            for (auto& mm : app.mod.maps)
                for (auto& a : mm.areas) {
                    auto& pre = a.prerequisiteControlPointIds;
                    pre.erase(std::remove(pre.begin(), pre.end(), del), pre.end());
                }
            app.selectedControlPointId = 0; app.dirty = true;
        }
    }

    // Draw (skip cells outside the visible child rect).
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 visMin = ImGui::GetWindowPos();
    ImVec2 visMax(visMin.x + ImGui::GetWindowSize().x, visMin.y + ImGui::GetWindowSize().y);
    auto cellTL = [&](int x, int y) { return ImVec2(origin.x + x * cs, origin.y + y * cs); };

    for (int y = 0; y < m.gridH; ++y) {
        for (int x = 0; x < m.gridW; ++x) {
            ImVec2 p0 = cellTL(x, y);
            ImVec2 p1(p0.x + cs, p0.y + cs);
            if (p1.x < visMin.x || p1.y < visMin.y || p0.x > visMax.x || p0.y > visMax.y) continue;
            size_t idx = (size_t)y * m.gridW + x;
            dl->AddRectFilled(p0, p1, rgbaToImU32(terrainColor(m.cells[idx])));
            int areaId = m.cellArea[idx];
            if (areaId != 0) {
                std::uint32_t ac = 0x808080FF;
                for (const auto& a : m.areas) if (a.id == areaId) { ac = a.color; break; }
                dl->AddRectFilled(p0, p1, rgbaToImU32(ac, 110));
            }
            drawTerrainMotif(dl, p0, cs, m.cells[idx]);
        }
    }

    // Fine grid lines (light grey skeleton).
    ImU32 gridCol = IM_COL32(140, 148, 160, 120);
    for (int x = 0; x <= m.gridW; ++x) dl->AddLine(cellTL(x, 0), cellTL(x, m.gridH), gridCol);
    for (int y = 0; y <= m.gridH; ++y) dl->AddLine(cellTL(0, y), cellTL(m.gridW, y), gridCol);

    // Coarse overlay grid + A1/C4 labels.
    ImU32 coarseCol = IM_COL32(230, 230, 120, 160);
    for (int c = 0; c <= m.overlayW; ++c) {
        float gx = (float)c * m.gridW / m.overlayW;
        dl->AddLine(ImVec2(origin.x + gx * cs, origin.y),
                    ImVec2(origin.x + gx * cs, origin.y + m.gridH * cs), coarseCol, 1.5f);
    }
    for (int r = 0; r <= m.overlayH; ++r) {
        float gy = (float)r * m.gridH / m.overlayH;
        dl->AddLine(ImVec2(origin.x, origin.y + gy * cs),
                    ImVec2(origin.x + m.gridW * cs, origin.y + gy * cs), coarseCol, 1.5f);
    }
    for (int r = 0; r < m.overlayH; ++r)
        for (int c = 0; c < m.overlayW; ++c) {
            float gx = (float)c * m.gridW / m.overlayW;
            float gy = (float)r * m.gridH / m.overlayH;
            std::string lbl = std::string(1, (char)('A' + c)) + std::to_string(r + 1);
            dl->AddText(ImVec2(origin.x + gx * cs + 2, origin.y + gy * cs + 2),
                        IM_COL32(255, 255, 160, 200), lbl.c_str());
        }

    // Placed objects (sub-grid props), drawn on top of terrain/areas.
    for (const auto& o : m.objects) {
        ImVec2 ctr(origin.x + o.x * cs, origin.y + o.y * cs);
        if (ctr.x + cs < visMin.x || ctr.y + cs < visMin.y ||
            ctr.x - cs > visMax.x || ctr.y - cs > visMax.y) continue;
        bool showSel = app.tool == Tool::PlaceObject && o.id == app.selectedObjectId;
        drawObjectIcon(dl, ctr, cs * 0.9f, o.type, o.rotationDeg, showSel);
    }

    // Free text labels (any colour/size), drawn above objects.
    for (const auto& tx : m.texts) {
        ImVec2 pos(origin.x + tx.x * cs, origin.y + tx.y * cs);
        dl->AddText(ImGui::GetFont(), tx.sizePx, pos, rgbaToImU32(tx.color),
                    tx.text.c_str());
        if (app.tool == Tool::PlaceText && tx.id == app.selectedTextId) {
            ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(tx.sizePx, FLT_MAX, 0.0f, tx.text.c_str());
            dl->AddRect(ImVec2(pos.x - 2, pos.y - 2), ImVec2(pos.x + sz.x + 2, pos.y + sz.y + 2),
                        IM_COL32(255, 230, 0, 255), 0, 0, 1.5f);
        }
    }

    // Control-point markers at their placed position (legacy files: area centroid).
    for (const auto& cp : app.mod.controlPoints) {
        if (cp.mapId != m.id) continue;
        ImVec2 center;
        if (cp.x >= 0) {
            center = ImVec2(origin.x + cp.x * cs, origin.y + cp.y * cs);
        } else {
            int ax, ay; areaCentroid(m, cp.areaId, ax, ay);
            if (ax < 0) continue;
            center = ImVec2(origin.x + (ax + 0.5f) * cs, origin.y + (ay + 0.5f) * cs);
        }
        float rad = std::max(4.0f, cs * 0.3f);
        dl->AddCircleFilled(center, rad, IM_COL32(255, 60, 60, 230));
        if (app.tool == Tool::PlaceControlPoint && cp.id == app.selectedControlPointId)
            dl->AddCircle(center, rad + 3.0f, IM_COL32(255, 230, 0, 255), 0, 2.0f);
        dl->AddText(ImVec2(center.x + 5, center.y - 6), IM_COL32(255, 255, 255, 255),
                    std::to_string(cp.id).c_str());
    }

    // Outline the selected area (the one shown in the Inspector): draw only the
    // perimeter edges — cell sides whose neighbour is not part of the same area.
    if (app.selectedAreaId != 0) {
        ImU32 selCol = IM_COL32(90, 220, 255, 255);   // cyan, stands out from tints/coarse grid
        float th = std::max(2.5f, cs * 0.14f);
        auto owns = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < m.gridW && y < m.gridH &&
                   m.cellArea[(size_t)y * m.gridW + x] == app.selectedAreaId;
        };
        for (int y = 0; y < m.gridH; ++y)
            for (int x = 0; x < m.gridW; ++x) {
                if (!owns(x, y)) continue;
                ImVec2 p0 = cellTL(x, y), p1(p0.x + cs, p0.y + cs);
                if (p1.x < visMin.x || p1.y < visMin.y || p0.x > visMax.x || p0.y > visMax.y) continue;
                if (!owns(x, y - 1)) dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y), selCol, th);
                if (!owns(x, y + 1)) dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), selCol, th);
                if (!owns(x - 1, y)) dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p1.y), selCol, th);
                if (!owns(x + 1, y)) dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p1.y), selCol, th);
            }
    }

    // Hover highlight — shows the brush footprint for paint tools.
    if (cellValid) {
        int r = (paintTool ? app.brushSize : 1) / 2;
        ImVec2 p0 = cellTL(cx - r, cy - r);
        ImVec2 p1 = cellTL(cx + r + 1, cy + r + 1);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 220), 0, 0, 2.0f);
    }

    ImGui::EndChild();

    if (cellValid && hovered)
        ImGui::SetTooltip("%s  (%d,%d)", coarseLabel(m, cx, cy).c_str(), cx, cy);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// UI: inspector
// ---------------------------------------------------------------------------
static void drawAreaInspector(App& app, gns::Area& a) {
    ImGui::SeparatorText("Area");
    if (InputStr("Label", &a.label)) app.dirty = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("From map")) {
        if (gns::Map* m = currentMap(app)) {
            int ax, ay; areaCentroid(*m, a.id, ax, ay);
            if (ax >= 0) { a.label = coarseLabel(*m, ax, ay); app.dirty = true; }
        }
    }
    if (InputStr("Name", &a.name)) app.dirty = true;
    float col[4]; rgbaToFloat4(a.color, col);
    if (ImGui::ColorEdit3("Color", col)) { col[3] = 1.0f; a.color = float4ToRgba(col); app.dirty = true; }

    ImGui::SeparatorText("Descriptions");
    ImGui::TextDisabled("DM only");
    if (InputStrMultiline("##dm", &a.dmText, ImVec2(-1, 70))) app.dirty = true;
    ImGui::TextDisabled("Player");
    if (InputStrMultiline("##player", &a.playerText, ImVec2(-1, 70))) app.dirty = true;

    ImGui::SeparatorText("Statistics");
    if (ImGui::SliderInt("Monster %", &a.monsterChancePct, 0, 100)) app.dirty = true;
    if (pickerField("Monster", &a.monsterType, app.monsterNames)) app.dirty = true;
    if (ImGui::SliderInt("Treasure %", &a.treasureChancePct, 0, 100)) app.dirty = true;
    if (InputStr("Treasure type (A-T)", &a.treasureType)) app.dirty = true;
    if (ImGui::SliderInt("Trap %", &a.trapChancePct, 0, 100)) app.dirty = true;
    if (InputStr("Trap desc", &a.trapDescription)) app.dirty = true;
    if (ImGui::SliderInt("Lock pick %", &a.lockChancePct, 0, 100)) app.dirty = true;
    if (InputStr("Lock desc", &a.lockDescription)) app.dirty = true;
    if (ImGui::SliderInt("Hidden discover %", &a.hiddenChancePct, 0, 100)) app.dirty = true;
    if (InputStr("Hidden desc", &a.hiddenDescription)) app.dirty = true;

    ImGui::SeparatorText("Artwork");
    if (InputStr("Path", &a.artworkPath)) app.dirty = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse")) {
        std::string p = fileDialog(false);
        if (!p.empty()) { a.artworkPath = p; app.dirty = true; }
    }

    ImGui::SeparatorText("Prerequisite control points");
    if (app.mod.controlPoints.empty()) {
        ImGui::TextDisabled("No control points defined.");
    } else {
        for (const auto& cp : app.mod.controlPoints) {
            auto& pre = a.prerequisiteControlPointIds;
            bool on = std::find(pre.begin(), pre.end(), cp.id) != pre.end();
            std::string lbl = "#" + std::to_string(cp.id) + " " + cp.name;
            if (ImGui::Checkbox(lbl.c_str(), &on)) {
                if (on) pre.push_back(cp.id);
                else pre.erase(std::remove(pre.begin(), pre.end(), cp.id), pre.end());
                app.dirty = true;
            }
        }
    }

    if (ImGui::Button("Delete Area")) {
        if (gns::Map* m = currentMap(app)) {
            pushUndo(app);
            for (auto& cell : m->cellArea) if (cell == a.id) cell = 0;
            int delId = a.id;
            m->areas.erase(std::remove_if(m->areas.begin(), m->areas.end(),
                [&](const gns::Area& x) { return x.id == delId; }), m->areas.end());
            app.selectedAreaId = 0;
            app.tool = Tool::PaintTerrain;
            app.dirty = true;
        }
    }
}

static void drawModuleInspector(App& app) {
    ImGui::SeparatorText("Module");
    if (InputStr("Name", &app.mod.name)) app.dirty = true;
    if (InputStr("Author", &app.mod.author)) app.dirty = true;
    ImGui::TextDisabled("Summary");
    if (InputStrMultiline("##summary", &app.mod.summary, ImVec2(-1, 80))) app.dirty = true;

    ImGui::SeparatorText("Start / End");
    auto mapCombo = [&](const char* label, int* id) {
        const char* cur = "(none)";
        for (auto& m : app.mod.maps) if (m.id == *id) cur = m.name.c_str();
        if (ImGui::BeginCombo(label, cur)) {
            for (auto& m : app.mod.maps)
                if (ImGui::Selectable(m.name.c_str(), m.id == *id)) { *id = m.id; app.dirty = true; }
            ImGui::EndCombo();
        }
    };
    auto areaCombo = [&](const char* label, int* id) {
        gns::Area* sel = app.mod.areaById(*id);
        const char* cur = sel ? (sel->label.empty() ? sel->name.c_str() : sel->label.c_str()) : "(none)";
        if (ImGui::BeginCombo(label, cur)) {
            if (ImGui::Selectable("(none)", *id == 0)) { *id = 0; app.dirty = true; }
            for (auto& m : app.mod.maps)
                for (auto& a : m.areas) {
                    std::string lbl = (a.label.empty() ? a.name : a.label) + " [" + m.name + "]";
                    if (ImGui::Selectable(lbl.c_str(), a.id == *id)) { *id = a.id; app.dirty = true; }
                }
            ImGui::EndCombo();
        }
    };
    mapCombo("Start map", &app.mod.startMapId);
    areaCombo("Start area", &app.mod.startAreaId);
    areaCombo("End area", &app.mod.endAreaId);
}

static void drawControlPointsSection(App& app) {
    if (!ImGui::CollapsingHeader("Control Points (module)")) return;
    int deleteId = 0;
    for (auto& cp : app.mod.controlPoints) {
        ImGui::PushID(cp.id);
        ImGui::Text("#%d", cp.id);
        ImGui::SameLine();
        if (InputStr("##cpname", &cp.name)) app.dirty = true;
        if (InputStrMultiline("##cpdesc", &cp.description, ImVec2(-1, 40))) app.dirty = true;
        gns::Area* a = app.mod.areaById(cp.areaId);
        ImGui::TextDisabled("at area: %s", a ? (a->label.empty() ? a->name.c_str() : a->label.c_str()) : "?");
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) deleteId = cp.id;
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteId) {
        auto& v = app.mod.controlPoints;
        v.erase(std::remove_if(v.begin(), v.end(),
            [&](const gns::ControlPoint& c) { return c.id == deleteId; }), v.end());
        for (auto& m : app.mod.maps)
            for (auto& a : m.areas) {
                auto& pre = a.prerequisiteControlPointIds;
                pre.erase(std::remove(pre.begin(), pre.end(), deleteId), pre.end());
            }
        app.dirty = true;
    }
}

static void drawInspectorWindow(App& app) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kRightPaneW, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kRightPaneW, vp->WorkSize.y), ImGuiCond_Always);
    ImGui::Begin("Inspector", nullptr, kPaneFlags);
    gns::Area* a = app.mod.areaById(app.selectedAreaId);
    if (a) drawAreaInspector(app, *a);
    else drawModuleInspector(app);
    ImGui::Separator();
    drawControlPointsSection(app);
    ImGui::End();
}

// Unsaved-changes confirmation when closing (#3).
static void drawExitModal(App& app, bool& running) {
    if (!app.wantClose) return;
    if (!app.dirty) { running = false; return; }   // nothing to lose
    ImGui::OpenPopup("Close Module Creator?");
    if (ImGui::BeginPopupModal("Close Module Creator?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Are you sure you want to close the Module Creator?\n"
                               "You have unsaved changes.");
        if (ImGui::Button("Save & Exit")) {
            doSave(app);
            if (!app.dirty) running = false;   // saved (not cancelled)
            app.wantClose = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard & Exit")) { running = false; app.wantClose = false; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { app.wantClose = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "Grimoire & Steel — Module Creator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // fixed layout; don't persist/restore window state
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    App app;
    newModule(app);
    loadReference(app);

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) app.wantClose = true;   // confirm if dirty (#3)
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Keyboard shortcuts.
        ImGuiIO& io = ImGui::GetIO();
        bool typing = io.WantTextInput;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) doSave(app);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) doOpen(app);
        if (io.KeyCtrl && !typing && ImGui::IsKeyPressed(ImGuiKey_Z)) doUndo(app);             // #4
        if (io.KeyCtrl && !typing && ImGui::IsKeyPressed(ImGuiKey_H)) app.tool = Tool::Select; // #11
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)))
            app.cellPx = std::clamp(app.cellPx * 1.1f, 2.0f, 64.0f);                            // #9
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)))
            app.cellPx = std::clamp(app.cellPx * 0.9f, 2.0f, 64.0f);                            // #9

        drawMenuBar(app);
        drawToolsWindow(app);
        drawCanvasWindow(app);
        drawInspectorWindow(app);
        drawExitModal(app, running);

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 24, 32, 255);
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
