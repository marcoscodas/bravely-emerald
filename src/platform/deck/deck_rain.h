#ifndef GUARD_DECK_RAIN_H
#define GUARD_DECK_RAIN_H

#include <stdbool.h>

// Rain upgrade (phase 5): GPU streaks (C2), wet ground (C4), mist (C5), splashes (C3). GL (Core 0) thread only.
// All of it is off when voxel_rain = 0, voxel_shaders = 0 or the programs failed to build: the legacy rain
// particles and the flat ground stay. Env DECK_RAIN_OFF=streaks,wet,mist,splash switches single effects off.

typedef struct
{
    float seconds;            // f->seconds
    float amount;             // weatherAmount 0..1
    float bolt;               // lightning 0..1
    float cam[3];             // camera position
    float target[3];          // camera target x, ground height, target z (the rain box is centred on it)
    float light[3];           // day/night light colour (rain colour is scaled by it)
    float density;            // count multiplier of streaks and splashes (1 = field; battle scenery 0.6)
} DeckRainFrame;

void DeckRain_Init(void);
void DeckRain_Shutdown(void);
bool DeckRain_Enabled(void);
// Once a frame, before the ground pass: wetness ramps up in rain (20 s), dries out of it (90 s). Outdoors only.
void DeckRain_Update(bool rain, float amount, bool indoor, float seconds);
// C2: draws the streaks, returns true if it did (the caller then skips the legacy rain particles).
bool DeckRain_DrawStreaks(const DeckRainFrame *f);

// C4: wet ground. WetBegin binds the ground program while the ground is wet and the frame is outdoors (light = day/night
// colour for the sky reflection); returns false (nothing bound) otherwise. WetChunk sets the chunk's world offset per chunk.
bool DeckRain_WetBegin(const float light[3]);
// Relief only (voxel_bump on, ground not wet and no glints): the wet program with wetness 0. Same chunk and end calls.
bool DeckRain_BumpBegin(const float light[3]);
void DeckRain_WetChunk(float worldX, float worldZ);
void DeckRain_WetEnd(void);
float DeckRain_Wetness(void);

// C5: opacity (0..1, to give DrawFogBanks) of the low rain mist: grows with the weather amount and the wetness ramp.
float DeckRain_Mist(float weatherAmount);

// C3: ring splashes on the camera ground plane (target y); returns true if drawn. Call before the streaks.
bool DeckRain_DrawSplashes(const DeckRainFrame *f);

#endif
