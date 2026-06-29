#include "gns/ui/MapRender.h"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace gns::ui {

ImU32 rgbaToImU32(std::uint32_t c, int alpha) {
    int a = alpha >= 0 ? alpha : (int)(c & 0xFF);
    return IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, a);
}

std::uint32_t terrainColor(int t) {
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
        case gns::Terrain::Path:     return 0xB8A06BFF;  // dusty tan dirt path
        case gns::Terrain::Ruins:    return 0x6E6A62FF;  // weathered broken stone
        case gns::Terrain::Graveyard:return 0x5A6356FF;  // dull grey-green
        case gns::Terrain::Lava:     return 0xC23A12FF;  // molten orange-red
        case gns::Terrain::AcidPool: return 0x7FB23AFF;  // sickly green
        case gns::Terrain::Ditch:    return 0x4A3E2EFF;  // dark earth trench
        case gns::Terrain::Crevice:  return 0x2A2622FF;  // near-black chasm
        case gns::Terrain::Hills:    return 0x6E7A4AFF;  // muted green-brown
        case gns::Terrain::WoodenBridge: return 0x9A6A3AFF;  // plank-brown deck
        case gns::Terrain::StoneBridge:  return 0x8A8A90FF;  // grey stone deck
        case gns::Terrain::Stairs:       return 0x9A958AFF;  // worn stone steps
        case gns::Terrain::WoodenStairs: return 0x8A5E32FF;  // wooden flight
        case gns::Terrain::Dirt:         return 0x6F5234FF;  // dark packed earth
        case gns::Terrain::Empty:
        default:                     return 0x2B313BFF;
    }
}

void drawTerrainMotif(ImDrawList* dl, ImVec2 p0, float cs, int t) {
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
            ImU32 dark = IM_COL32(46, 104, 38, 235), lite = IM_COL32(96, 172, 72, 230);
            static const float sp[][2] = {
                {0.16f, 0.20f}, {0.38f, 0.13f}, {0.60f, 0.23f}, {0.82f, 0.16f},
                {0.26f, 0.44f}, {0.50f, 0.37f}, {0.73f, 0.47f}, {0.90f, 0.35f},
                {0.18f, 0.70f}, {0.42f, 0.74f}, {0.65f, 0.66f}, {0.86f, 0.78f},
            };
            for (size_t i = 0; i < IM_ARRAYSIZE(sp); ++i)
                dl->AddCircleFilled(ImVec2(p0.x + cs * sp[i][0], p0.y + cs * sp[i][1]), 1.4f,
                                    (i & 1) ? lite : dark);
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
        case gns::Terrain::Dirt: {
            ImU32 lite = IM_COL32(150, 120, 84, 255), dark = IM_COL32(58, 42, 26, 255);
            static const float sp[][2] = {
                {0.22f, 0.26f}, {0.52f, 0.18f}, {0.78f, 0.30f}, {0.34f, 0.54f},
                {0.64f, 0.50f}, {0.18f, 0.74f}, {0.50f, 0.78f}, {0.82f, 0.68f},
            };
            for (size_t i = 0; i < IM_ARRAYSIZE(sp); ++i)
                dl->AddCircleFilled(ImVec2(p0.x + cs * sp[i][0], p0.y + cs * sp[i][1]), 1.5f,
                                    (i & 1) ? lite : dark);
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
        case gns::Terrain::Path: {
            ImU32 dot = IM_COL32(150, 128, 84, 220);
            dl->AddCircleFilled(ImVec2(c.x - cs * 0.16f, c.y - cs * 0.08f), 1.4f, dot);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.10f, c.y + cs * 0.12f), 1.4f, dot);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.02f, c.y - cs * 0.16f), 1.4f, dot);
            break;
        }
        case gns::Terrain::Ruins: {
            ImU32 stone = IM_COL32(120, 116, 108, 255);
            dl->AddRectFilled(ImVec2(c.x - cs * 0.24f, c.y + cs * 0.04f),
                              ImVec2(c.x - cs * 0.08f, c.y + cs * 0.24f), stone);
            dl->AddRectFilled(ImVec2(c.x + cs * 0.04f, c.y - cs * 0.18f),
                              ImVec2(c.x + cs * 0.20f, c.y + cs * 0.10f), stone);
            break;
        }
        case gns::Terrain::Graveyard: {
            ImU32 head = IM_COL32(176, 180, 172, 255);
            dl->AddRectFilled(ImVec2(c.x - cs * 0.10f, c.y - cs * 0.06f),
                              ImVec2(c.x + cs * 0.10f, c.y + cs * 0.24f), head);
            dl->AddCircleFilled(ImVec2(c.x, c.y - cs * 0.06f), cs * 0.10f, head);
            dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + cs * 0.14f), IM_COL32(90, 94, 88, 255), 1.2f);
            dl->AddLine(ImVec2(c.x - cs * 0.06f, c.y + cs * 0.05f),
                        ImVec2(c.x + cs * 0.06f, c.y + cs * 0.05f), IM_COL32(90, 94, 88, 255), 1.2f);
            break;
        }
        case gns::Terrain::Lava: {
            ImU32 hot = IM_COL32(255, 196, 70, 255), glow = IM_COL32(255, 120, 40, 220);
            dl->AddCircleFilled(ImVec2(c.x - cs * 0.14f, c.y + cs * 0.02f), cs * 0.09f, hot, 6);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.16f, c.y - cs * 0.10f), cs * 0.06f, glow, 6);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.04f, c.y + cs * 0.16f), cs * 0.05f, glow, 6);
            break;
        }
        case gns::Terrain::AcidPool: {
            ImU32 bub = IM_COL32(196, 240, 120, 230);
            dl->AddCircle(ImVec2(c.x - cs * 0.12f, c.y), cs * 0.08f, bub, 8, 1.4f);
            dl->AddCircle(ImVec2(c.x + cs * 0.14f, c.y + cs * 0.10f), cs * 0.05f, bub, 8, 1.4f);
            break;
        }
        case gns::Terrain::Ditch: {
            ImU32 sh = IM_COL32(34, 28, 20, 220);
            dl->AddRectFilled(ImVec2(p0.x + cs * 0.12f, c.y - cs * 0.10f),
                              ImVec2(p0.x + cs * 0.88f, c.y + cs * 0.10f), sh);
            break;
        }
        case gns::Terrain::Crevice: {
            ImU32 crack = IM_COL32(10, 8, 8, 240);
            dl->AddLine(ImVec2(p0.x + cs * 0.30f, p0.y + cs * 0.12f),
                        ImVec2(c.x, c.y), crack, 2.0f);
            dl->AddLine(ImVec2(c.x, c.y),
                        ImVec2(p0.x + cs * 0.70f, p0.y + cs * 0.88f), crack, 2.0f);
            break;
        }
        case gns::Terrain::Hills: {
            ImU32 hump = IM_COL32(96, 108, 64, 255);
            dl->AddCircleFilled(ImVec2(c.x - cs * 0.16f, c.y + cs * 0.12f), cs * 0.13f, hump, 10);
            dl->AddCircleFilled(ImVec2(c.x + cs * 0.16f, c.y + cs * 0.14f), cs * 0.11f, hump, 10);
            break;
        }
        default: break;
    }
}

RunDir runDirection(const gns::Map& m, int x, int y, int terr) {
    auto same = [&](int xx, int yy) {
        return xx >= 0 && yy >= 0 && xx < m.gridW && yy < m.gridH &&
               m.cells[(size_t)yy * m.gridW + xx] == terr;
    };
    bool h = same(x - 1, y) || same(x + 1, y);
    bool v = same(x, y - 1) || same(x, y + 1);
    if (h && v) return RunDir::Landing;
    if (h)      return RunDir::Horizontal;
    if (v)      return RunDir::Vertical;
    return RunDir::Default;
}

void drawStairBridgeMotif(ImDrawList* dl, ImVec2 p0, float cs, int terr, RunDir dir) {
    if (cs < 9.0f) return;
    bool horizontal = (dir == RunDir::Horizontal);
    gns::Terrain T = static_cast<gns::Terrain>(terr);

    if (T == gns::Terrain::Stairs || T == gns::Terrain::WoodenStairs) {
        if (dir == RunDir::Landing) return;
        bool wood = (T == gns::Terrain::WoodenStairs);
        ImU32 step = wood ? IM_COL32(74, 48, 26, 235) : IM_COL32(60, 56, 50, 230);
        ImU32 rail = IM_COL32(120, 86, 46, 255);
        if (wood) {
            if (horizontal) {
                dl->AddLine(ImVec2(p0.x, p0.y + cs * 0.12f), ImVec2(p0.x + cs, p0.y + cs * 0.12f), rail, 1.6f);
                dl->AddLine(ImVec2(p0.x, p0.y + cs * 0.88f), ImVec2(p0.x + cs, p0.y + cs * 0.88f), rail, 1.6f);
            } else {
                dl->AddLine(ImVec2(p0.x + cs * 0.12f, p0.y), ImVec2(p0.x + cs * 0.12f, p0.y + cs), rail, 1.6f);
                dl->AddLine(ImVec2(p0.x + cs * 0.88f, p0.y), ImVec2(p0.x + cs * 0.88f, p0.y + cs), rail, 1.6f);
            }
        }
        float lo = wood ? 0.12f : 0.10f, hi = wood ? 0.88f : 0.90f;
        for (int i = 1; i < 5; ++i) {
            float t = (float)i / 5.0f;
            if (horizontal)
                dl->AddLine(ImVec2(p0.x + cs * t, p0.y + cs * lo),
                            ImVec2(p0.x + cs * t, p0.y + cs * hi), step, 1.4f);
            else
                dl->AddLine(ImVec2(p0.x + cs * lo, p0.y + cs * t),
                            ImVec2(p0.x + cs * hi, p0.y + cs * t), step, 1.4f);
        }
        return;
    }

    bool wooden = (T == gns::Terrain::WoodenBridge);
    ImU32 rail  = wooden ? IM_COL32(120, 86, 46, 255) : IM_COL32(160, 160, 166, 255);
    ImU32 plank = wooden ? IM_COL32(82, 54, 30, 220)  : IM_COL32(74, 74, 80, 220);
    ImU32 gap   = IM_COL32(16, 20, 26, 140);
    if (dir == RunDir::Landing) {
        dl->AddRect(ImVec2(p0.x + cs * 0.10f, p0.y + cs * 0.10f),
                    ImVec2(p0.x + cs * 0.90f, p0.y + cs * 0.90f), rail, 0, 0, 2.0f);
        return;
    }
    if (horizontal) {
        dl->AddRectFilled(p0, ImVec2(p0.x + cs, p0.y + cs * 0.16f), gap);
        dl->AddRectFilled(ImVec2(p0.x, p0.y + cs * 0.84f), ImVec2(p0.x + cs, p0.y + cs), gap);
        dl->AddLine(ImVec2(p0.x, p0.y + cs * 0.16f), ImVec2(p0.x + cs, p0.y + cs * 0.16f), rail, 2.2f);
        dl->AddLine(ImVec2(p0.x, p0.y + cs * 0.84f), ImVec2(p0.x + cs, p0.y + cs * 0.84f), rail, 2.2f);
        dl->AddLine(ImVec2(p0.x, p0.y + cs * 0.50f), ImVec2(p0.x + cs, p0.y + cs * 0.50f), plank, 1.2f);
    } else {
        dl->AddRectFilled(p0, ImVec2(p0.x + cs * 0.16f, p0.y + cs), gap);
        dl->AddRectFilled(ImVec2(p0.x + cs * 0.84f, p0.y), ImVec2(p0.x + cs, p0.y + cs), gap);
        dl->AddLine(ImVec2(p0.x + cs * 0.16f, p0.y), ImVec2(p0.x + cs * 0.16f, p0.y + cs), rail, 2.2f);
        dl->AddLine(ImVec2(p0.x + cs * 0.84f, p0.y), ImVec2(p0.x + cs * 0.84f, p0.y + cs), rail, 2.2f);
        dl->AddLine(ImVec2(p0.x + cs * 0.50f, p0.y), ImVec2(p0.x + cs * 0.50f, p0.y + cs), plank, 1.2f);
    }
}

bool isStairOrBridge(int terr) {
    return terr == static_cast<int>(gns::Terrain::Stairs) ||
           terr == static_cast<int>(gns::Terrain::WoodenStairs) ||
           terr == static_cast<int>(gns::Terrain::WoodenBridge) ||
           terr == static_cast<int>(gns::Terrain::StoneBridge);
}

void drawObjectIcon(ImDrawList* dl, ImVec2 ctr, float s, int type, float rotDeg, bool selected) {
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
    auto Co = [&](float cx, float cy, float r, ImU32 col, float th) {
        dl->AddCircle(P(cx, cy), r * s, col, 0, th);
    };
    auto bridge = [&](float hl, ImU32 deck, ImU32 edge, int segs) {
        R(-hl, -0.45f, hl, 0.45f, deck);
        Ro(-hl, -0.45f, hl, 0.45f, edge);
        for (int i = 1; i < segs; ++i) {
            float x = -hl + 2.0f * hl * i / segs;
            L(x, -0.45f, x, 0.45f, edge, 1.2f);
        }
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
        case gns::ObjectType::Well:
            C(0.0f, 0.0f, 0.34f, stone); Co(0.0f, 0.0f, 0.34f, line, 1.5f);
            C(0.0f, 0.0f, 0.20f, IM_COL32(40, 52, 70, 255));
            break;
        case gns::ObjectType::StoneWall:
            R(-0.46f, -0.12f, 0.46f, 0.12f, stone); Ro(-0.46f, -0.12f, 0.46f, 0.12f, line);
            L(-0.15f, -0.12f, -0.15f, 0.12f, line, 1.0f);
            L(0.15f, -0.12f, 0.15f, 0.12f, line, 1.0f);
            L(-0.46f, 0.0f, 0.46f, 0.0f, line, 1.0f);
            break;
        case gns::ObjectType::WoodenWall:
            R(-0.46f, -0.12f, 0.46f, 0.12f, wood); Ro(-0.46f, -0.12f, 0.46f, 0.12f, line);
            for (float x = -0.30f; x <= 0.30f; x += 0.30f) L(x, -0.12f, x, 0.12f, wood2, 1.5f);
            break;
        case gns::ObjectType::Fence:
            L(-0.46f, -0.04f, 0.46f, -0.04f, wood, 2.0f);
            L(-0.46f, 0.10f, 0.46f, 0.10f, wood, 2.0f);
            for (float x = -0.40f; x <= 0.40f; x += 0.20f) L(x, -0.18f, x, 0.20f, wood2, 2.0f);
            break;
        case gns::ObjectType::Altar:
            R(-0.34f, -0.10f, 0.34f, 0.28f, stone); Ro(-0.34f, -0.10f, 0.34f, 0.28f, line);
            R(-0.40f, -0.20f, 0.40f, -0.10f, IM_COL32(150, 150, 156, 255));
            Ro(-0.40f, -0.20f, 0.40f, -0.10f, line);
            break;
        case gns::ObjectType::WoodenBridgeS: bridge(0.30f, wood, line, 4); break;
        case gns::ObjectType::WoodenBridgeM: bridge(0.42f, wood, line, 6); break;
        case gns::ObjectType::WoodenBridgeL: bridge(0.50f, wood, line, 8); break;
        case gns::ObjectType::StoneBridgeS:  bridge(0.30f, stone, line, 4); break;
        case gns::ObjectType::StoneBridgeM:  bridge(0.42f, stone, line, 6); break;
        case gns::ObjectType::StoneBridgeL:  bridge(0.50f, stone, line, 8); break;
        case gns::ObjectType::CaveEntrance: {
            R(-0.40f, -0.10f, 0.40f, 0.40f, IM_COL32(96, 92, 86, 255));
            dl->AddTriangleFilled(P(0.0f, -0.42f), P(-0.40f, 0.10f), P(0.40f, 0.10f),
                                  IM_COL32(110, 106, 100, 255));
            R(-0.16f, 0.04f, 0.16f, 0.40f, IM_COL32(18, 16, 16, 255));
            dl->AddTriangleFilled(P(0.0f, -0.10f), P(-0.16f, 0.06f), P(0.16f, 0.06f),
                                  IM_COL32(18, 16, 16, 255));
            break;
        }
        case gns::ObjectType::SpiralStairs: {
            Co(0.0f, 0.0f, 0.42f, stone, 2.0f);
            C(0.0f, 0.0f, 0.10f, IM_COL32(70, 70, 76, 255));
            for (int i = 0; i < 8; ++i) {
                float a2 = i * 3.14159265f / 4.0f;
                L(0.10f * std::cos(a2), 0.10f * std::sin(a2),
                  0.42f * std::cos(a2), 0.42f * std::sin(a2), line, 1.4f);
            }
            break;
        }
        case gns::ObjectType::Compass: {
            // Outer ring, N/E/S/W ticks + letters, and a two-tone needle (red N, white S).
            const ImU32 face = IM_COL32(238, 230, 208, 255);
            C(0.0f, 0.0f, 0.46f, face); Co(0.0f, 0.0f, 0.46f, line, 2.0f);
            for (int i = 0; i < 4; ++i) {
                float a2 = i * 3.14159265f / 2.0f;
                L(0.40f * std::cos(a2), 0.40f * std::sin(a2),
                  0.46f * std::cos(a2), 0.46f * std::sin(a2), line, 1.6f);
            }
            dl->AddTriangleFilled(P(0.0f, -0.34f), P(-0.12f, 0.0f), P(0.12f, 0.0f),
                                  IM_COL32(190, 40, 40, 255));     // north needle
            dl->AddTriangleFilled(P(0.0f, 0.34f), P(-0.12f, 0.0f), P(0.12f, 0.0f),
                                  IM_COL32(235, 235, 240, 255));   // south needle
            dl->AddTriangle(P(0.0f, -0.34f), P(-0.12f, 0.0f), P(0.12f, 0.0f), line, 1.0f);
            dl->AddTriangle(P(0.0f, 0.34f), P(-0.12f, 0.0f), P(0.12f, 0.0f), line, 1.0f);
            C(0.0f, 0.0f, 0.05f, line);
            break;
        }
        default: break;
    }
    if (selected) {
        dl->AddRect(ImVec2(ctr.x - 0.5f * s, ctr.y - 0.52f * s),
                    ImVec2(ctr.x + 0.5f * s, ctr.y + 0.52f * s),
                    IM_COL32(255, 230, 0, 255), 2.0f, 0, 2.0f);
    }
}

void areaCentroid(const gns::Map& m, int areaId, int& outX, int& outY) {
    long sx = 0, sy = 0, n = 0;
    for (int y = 0; y < m.gridH; ++y)
        for (int x = 0; x < m.gridW; ++x)
            if (m.cellArea[(size_t)y * m.gridW + x] == areaId) { sx += x; sy += y; ++n; }
    if (n == 0) { outX = outY = -1; return; }
    outX = (int)(sx / n);
    outY = (int)(sy / n);
}

void drawAreaOutline(ImDrawList* dl, const gns::Map& m, int areaId, ImU32 col, float th,
                     ImVec2 origin, float cs, ImVec2 visMin, ImVec2 visMax) {
    auto owns = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < m.gridW && y < m.gridH &&
               m.cellArea[(size_t)y * m.gridW + x] == areaId;
    };
    for (int y = 0; y < m.gridH; ++y)
        for (int x = 0; x < m.gridW; ++x) {
            if (!owns(x, y)) continue;
            ImVec2 p0(origin.x + x * cs, origin.y + y * cs), p1(p0.x + cs, p0.y + cs);
            if (p1.x < visMin.x || p1.y < visMin.y || p0.x > visMax.x || p0.y > visMax.y) continue;
            if (!owns(x, y - 1)) dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y), col, th);
            if (!owns(x, y + 1)) dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), col, th);
            if (!owns(x - 1, y)) dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p1.y), col, th);
            if (!owns(x + 1, y)) dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p1.y), col, th);
        }
}

void renderMapView(ImDrawList* dl, const gns::Map& m,
                   const std::vector<gns::ControlPoint>& cps, int mapId,
                   ImVec2 origin, float cs, ImVec2 visMin, ImVec2 visMax,
                   bool hideHiddenAreas) {
    auto cellTL = [&](int x, int y) { return ImVec2(origin.x + x * cs, origin.y + y * cs); };
    auto isHidden = [&](int areaId) {
        if (!hideHiddenAreas || areaId == 0) return false;
        for (const auto& a : m.areas) if (a.id == areaId) return a.hidden;
        return false;
    };

    // Terrain + area tint + motifs.
    for (int y = 0; y < m.gridH; ++y) {
        for (int x = 0; x < m.gridW; ++x) {
            ImVec2 p0 = cellTL(x, y);
            ImVec2 p1(p0.x + cs, p0.y + cs);
            if (p1.x < visMin.x || p1.y < visMin.y || p0.x > visMax.x || p0.y > visMax.y) continue;
            size_t idx = (size_t)y * m.gridW + x;
            int terr = m.cells[idx];
            int areaId = m.cellArea[idx];
            std::uint32_t ac = 0x808080FF;
            bool fill = true;
            if (areaId != 0)
                for (const auto& a : m.areas) if (a.id == areaId) { ac = a.color; fill = a.fillEnabled; break; }
            dl->AddRectFilled(p0, p1, rgbaToImU32(terrainColor(terr)));
            if (areaId != 0 && fill && !isHidden(areaId))   // hidden areas show no tint at play
                dl->AddRectFilled(p0, p1, rgbaToImU32(ac, 110));
            if (isStairOrBridge(terr))
                drawStairBridgeMotif(dl, p0, cs, terr, runDirection(m, x, y, terr));
            else
                drawTerrainMotif(dl, p0, cs, terr);
        }
    }

    // Fine grid lines.
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

    // Placed objects.
    for (const auto& o : m.objects) {
        ImVec2 ctr(origin.x + o.x * cs, origin.y + o.y * cs);
        if (ctr.x + cs < visMin.x || ctr.y + cs < visMin.y ||
            ctr.x - cs > visMax.x || ctr.y - cs > visMax.y) continue;
        drawObjectIcon(dl, ctr, cs * 0.9f * (o.scale > 0.0f ? o.scale : 1.0f),
                       o.type, o.rotationDeg, false);
    }

    // Free text labels.
    for (const auto& tx : m.texts) {
        ImVec2 pos(origin.x + tx.x * cs, origin.y + tx.y * cs);
        dl->AddText(ImGui::GetFont(), tx.sizePx, pos, rgbaToImU32(tx.color), tx.text.c_str());
    }

    // Control-point markers (legacy files: at the area centroid).
    for (const auto& cp : cps) {
        if (cp.mapId != mapId) continue;
        ImVec2 center;
        if (cp.x >= 0) {
            center = ImVec2(origin.x + cp.x * cs, origin.y + cp.y * cs);
        } else {
            int ax, ay; areaCentroid(m, cp.areaId, ax, ay);
            if (ax < 0) continue;
            center = ImVec2(origin.x + (ax + 0.5f) * cs, origin.y + (ay + 0.5f) * cs);
        }
        float rad = std::max(4.0f, cs * 0.3f);
        if (cp.kind == 1) {
            ImVec2 q[4] = {ImVec2(center.x, center.y - rad), ImVec2(center.x + rad, center.y),
                           ImVec2(center.x, center.y + rad), ImVec2(center.x - rad, center.y)};
            dl->AddConvexPolyFilled(q, 4, IM_COL32(255, 198, 64, 235));
            dl->AddPolyline(q, 4, IM_COL32(120, 80, 10, 235), ImDrawFlags_Closed, 1.5f);
        } else {
            dl->AddCircleFilled(center, rad, IM_COL32(255, 60, 60, 230));
        }
        dl->AddText(ImVec2(center.x + 5, center.y - 6), IM_COL32(255, 255, 255, 255),
                    std::to_string(cp.id).c_str());
    }

    // Outline no-fill areas in their own colour (skip hidden areas at play time).
    for (const auto& a : m.areas)
        if (!a.fillEnabled && !(hideHiddenAreas && a.hidden))
            drawAreaOutline(dl, m, a.id, rgbaToImU32(a.color, 235),
                            std::max(2.0f, cs * 0.10f), origin, cs, visMin, visMax);
}

SDL_Texture* loadEmbeddedTexture(SDL_Renderer* renderer, const char* resName) {
#ifdef _WIN32
    HRSRC h = FindResourceA(nullptr, resName, reinterpret_cast<LPCSTR>(RT_RCDATA));
    if (!h) return nullptr;
    HGLOBAL g = LoadResource(nullptr, h);
    void* data = g ? LockResource(g) : nullptr;
    DWORD sz = SizeofResource(nullptr, h);
    if (!data || !sz) return nullptr;
    SDL_RWops* rw = SDL_RWFromConstMem(data, (int)sz);
    SDL_Surface* surf = IMG_Load_RW(rw, 1);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
#else
    (void)renderer; (void)resName;
    return nullptr;
#endif
}

} // namespace gns::ui
