#ifndef GUARD_DECK_SUNSHADOW_H
#define GUARD_DECK_SUNSHADOW_H

#include <stdbool.h>
#include <GL/gl.h>

// Deck sun shadow map (voxel_sunshadow). A depth pass from the sun draws the casters (trees, buildings, characters)
// into a texel-snapped box around the camera focus; deck_post.c's composite reads it with the scene depth and darkens
// every receiver (ground, trees, buildings, characters) in one place. GL thread only.
// Env DECK_SUNSHADOW_OFF=1 switches it off for A/B shots; a failed target or composite build also switches it off.

typedef struct
{
    bool active;            // a map was drawn this frame and the composite may use it
    float sunVP[16];        // world -> sun clip (column-major)
    float invVP[16];        // camera clip -> world (column-major)
    GLuint depthTex;
    float amount;           // 0..1: sun strength (night and dawn fade)
    float mul;              // colour multiplier inside a full shadow
    float texelWorld;       // world size of one shadow texel
    float texelUv;          // one shadow texel in uv
    float range;            // world depth range of the map (depth 0..1)
    float spacing;          // PCF tap spacing in texels
    int radius;             // 0 = 2x2 taps, 1 = 3x3, 2 = 5x5
    float toSun[3];         // unit vector toward the sun
} DeckSunResolve;

// -1 off, else 0 low, 1 medium, 2 high (voxel_sunshadow; AUTO follows voxel_quality). Off when shaders are off,
// DECK_SUNSHADOW_OFF is set, or the map or composite could not be built.
int DeckSunShadow_Level(void);
// The baked cast shadow rays of the chunk builder are skipped (no double darkening). Same on every thread.
bool DeckSunShadow_BakedOff(void);

void DeckSunShadow_Reset(void);                         // start of every frame: no map yet
void DeckSunShadow_CaptureCamera(void);                 // after the camera matrices are loaded: remembers P * MV for the composite
bool DeckSunShadow_CameraInvVP(float out[16]);           // camera clip -> world of this frame (independent of the sun map); false before the capture
// Binds the map target, loads the sun matrices (projection = world -> sun clip, modelview identity) and clears the depth.
// False: nothing to draw this frame (off, no sun, no target). Draw the casters with the fixed pipeline, then End.
bool DeckSunShadow_Begin(float focusX, float focusY, float focusZ, float dx, float dz, float strength);
void DeckSunShadow_End(void);                           // restores the matrices and the colour mask; the caller rebinds its target
const DeckSunResolve *DeckSunShadow_Resolve(void);      // NULL when no map was drawn this frame
void DeckSunShadow_Shutdown(void);

#endif
