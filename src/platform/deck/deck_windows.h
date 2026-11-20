#ifndef GUARD_DECK_WINDOWS_H
#define GUARD_DECK_WINDOWS_H

#include <stdbool.h>
#include "deck_gl_shader.h"

// Lit windows at dusk and night (Deck/Windows only). buildings.bin already carries a glow mask for the pages whose models
// have glass: one byte per texel, same page layout as the colours (VoxelBuildings_ReadGlow), non zero on the window texels.
// Each page that has one gets an 8-bit luminance texture (cached, a few slots). The renderer draws the visible buildings'
// triangles a second time after the day/night grade (so the grade does not darken them) with the mask, a warm emissive
// colour and "replace" blending; the colour passes 1.0 in the bright pass of the bloom. Setting voxel_windows
// (OFF / AUTO / LOW / MEDIUM / HIGH). GL (Core 0) thread only. Notes: docs/deck_notes_parts/windows.md

// -1 off, else 0 low, 1 medium, 2 high (AUTO resolved against voxel_quality). Off also when voxel_shaders is off.
int DeckWindows_Level(void);
// Strength 0..1 of the lit windows now: 0 by day, ramps in as daylight falls from 0.45 to 0.20, 0 when the setting is off.
// The caller adds its own scene tests (outdoors, not a 3D battle).
float DeckWindows_Amount(void);
// Binds the program and the blend state. false: the program failed to build (nothing changed, windows stay as drawn).
bool DeckWindows_Begin(float amount);
// The glow texture of a building page (bound to texture unit 0 by the caller), 0 when the page has none or is not ready.
GLuint DeckWindows_PageMask(int page);
// Restores the state Pass_DayNightGrade leaves (blend off, depth write on, no program).
void DeckWindows_End(void);

#endif
