#pragma once
#include "imgui.h"
#include "gns/Module.h"

#include <cstdint>
#include <vector>

struct SDL_Renderer;   // SDL forward decls (avoid pulling SDL into this header)
struct SDL_Texture;

// Shared map-rendering helpers used by both the Module Creator (authoring canvas) and the
// Game Engine (read-only play view). These are the pure draw routines extracted from the
// creator so the two apps render an identical map. They depend on Dear ImGui, so they live
// in the gns_ui library rather than the UI-free gns_core.

namespace gns::ui {

// Model stores RGBA in a uint32; ImGui draws with ImU32 (ABGR). `alpha` overrides the
// low byte when >= 0.
ImU32 rgbaToImU32(std::uint32_t c, int alpha = -1);

// Base fill color (RGBA) for a Terrain value; motifs are drawn on top.
std::uint32_t terrainColor(int t);

// Run direction for a stairs/bridge cell, inferred from same-type 4-neighbours.
enum class RunDir { Default, Horizontal, Vertical, Landing };
RunDir runDirection(const gns::Map& m, int x, int y, int terr);
bool isStairOrBridge(int terr);

// Per-cell decoration so terrain types read at a glance (no-op for tiny cells).
void drawTerrainMotif(ImDrawList* dl, ImVec2 p0, float cs, int t);
// Orientation-aware stair/bridge motif (treads/planks aligned to the run).
void drawStairBridgeMotif(ImDrawList* dl, ImVec2 p0, float cs, int terr, RunDir dir);
// A placed prop centred at `ctr`, fitting an `s`-sized box, rotated `rotDeg` clockwise.
void drawObjectIcon(ImDrawList* dl, ImVec2 ctr, float s, int type, float rotDeg, bool selected);
// Trace the boundary edges of an area's cells in `col`.
void drawAreaOutline(ImDrawList* dl, const gns::Map& m, int areaId, ImU32 col, float th,
                     ImVec2 origin, float cs, ImVec2 visMin, ImVec2 visMax);
// Integer cell centroid of an area (outX/outY = -1 when the area owns no cells).
void areaCentroid(const gns::Map& m, int areaId, int& outX, int& outY);

// Read-only composite: terrain + area tints + motifs + fine grid + coarse overlay labels +
// objects + texts + control-point markers + no-fill area outlines. No selection/brush UI.
// When `hideHiddenAreas` is true (engine play view), areas flagged `hidden` get no tint/outline
// so players can't see them (terrain still draws). Objects are scaled by `MapObject::scale`.
void renderMapView(ImDrawList* dl, const gns::Map& m,
                   const std::vector<gns::ControlPoint>& cps, int mapId,
                   ImVec2 origin, float cs, ImVec2 visMin, ImVec2 visMax,
                   bool hideHiddenAreas = false);

// Load a baked-in RCDATA resource (PNG) into an SDL_Texture (Windows-only; nullptr elsewhere
// or on failure). Caller owns the texture. Used for the item-art catalog embedded in both apps.
SDL_Texture* loadEmbeddedTexture(SDL_Renderer* renderer, const char* resName);

} // namespace gns::ui
