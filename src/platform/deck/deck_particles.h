#ifndef GUARD_DECK_PARTICLES_H
#define GUARD_DECK_PARTICLES_H

#include <stdbool.h>

// Phase 6 particles (F2) and water glints. GL (Core 0) thread only. Setting voxel_particles; also off with voxel_shaders = 0 or when
// the programs failed to build: the legacy motes and the plain ground stay. Env DECK_PART_OFF=pollen,fire,leaf,glint for single A/B.

typedef struct
{
    float seconds;
    float target[3];       // camera target x, ground height, target z (the particle box is centred on it)
    float pitchCosSin[2];  // camera pitch: billboard up = (0, cos, sin)
    float light[3];        // day/night light colour
    float motes;           // light.motes: sunlit pollen strength 0..1
    float glow;            // VitaDayNight_Get()->glow
    int treeVerts;         // tree vertices of the visible chunks (leaf density), 0 = none
    bool rain;             // weather is rain: no particles at all
    bool calm;             // weather is not sun/clear (fog, sand, ash, snow): no pollen or leaves
    float density;         // count multiplier (1 = the field; battle scenery 0.6 and below)
} DeckPartFrame;

void DeckParticles_Init(void);
void DeckParticles_Shutdown(void);
bool DeckParticles_Enabled(void);
// Outdoors only (the caller culls indoors, underground and battles). Returns true if pollen was drawn (the legacy motes then skip).
bool DeckParticles_Draw(const DeckPartFrame *f);

// Water glints: ground program with sparkles on water texels. Begin binds it (false: nothing bound, old ground), Chunk sets the chunk's
// world offset, End unbinds. sun = sun strength 0..1, glow = night factor.
bool DeckParticles_GlintBegin(float sun, float glow, bool rain);
void DeckParticles_GlintChunk(float worldX, float worldZ);
void DeckParticles_GlintEnd(void);

#endif
