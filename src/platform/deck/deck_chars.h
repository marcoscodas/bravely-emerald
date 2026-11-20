#ifndef GUARD_DECK_CHARS_H
#define GUARD_DECK_CHARS_H

#include <stdbool.h>
#include <stdint.h>

// Phase 6 characters: outline and sun rim (G4, voxel_outline) and the soft blob shadow (B4, voxel_shadow). GL (Core 0) thread only.
// Both are off when their setting is 0, voxel_shaders is 0 or the program failed to build: the plain billboards and the silhouette
// shadows alone stay.

void DeckChars_Init(void);
void DeckChars_Shutdown(void);

// G4. Begin binds the outline program for the character billboards (sprite atlas texture already bound on unit 0); false = nothing
// bound, draw the plain way. The sun (VoxelSun dx, dz, strength) sets the rim side and strength.
bool DeckChars_OutlineBegin(float sunDx, float sunDz, float sunStrength);
// Once a frame: indoors and rain amount (0..1, 0 when it does not rain) for the sprite lighting (voxel_sprlight), which is the same
// program: sun side shading and the one rim (see docs/deck_notes_parts/sprlight.md).
void DeckChars_SetEnv(bool indoor, float rain);
void DeckChars_OutlineEnd(void);
// Night lantern halo: a round warm billboard behind each character card. verts is the entity vertex array (x, y, z floats first),
// stride its vertex size in bytes, [first, end) the character range (6 vertices per card). Call after DeckChars_OutlineBegin of the
// frame (it sets the amount, 0 by day, indoors or with sprite lighting OFF) and before the cards are drawn; no-op when the amount is
// 0. Leaves blend off, depth mask on and the client array pointers pointing at its own buffer: set them again afterwards.
void DeckChars_DrawGlow(const void *verts, uint32_t stride, uint32_t first, uint32_t end);

// B4. Factor for the alpha of the silhouette shadows: below 1 while the blobs are drawn, so both together do not double the darkening.
float DeckChars_SilhouetteScale(void);
bool DeckChars_ShadowEnabled(void);
// Blobs: x, y, z of the ground under the feet and the strength (alpha peak), count <= DECK_CHARS_MAX_BLOBS. Call before the silhouette
// shadows. Needs the 3D passes' state (depth test on); leaves blend off and the depth mask on.
#define DECK_CHARS_MAX_BLOBS 64
void DeckChars_DrawBlobs(const float (*blobs)[4], int count, float sunDx, float sunDz);

#endif
