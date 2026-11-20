#ifndef GUARD_DECK_BATTLEFX_H
#define GUARD_DECK_BATTLEFX_H

#include <stdbool.h>
#include <stdint.h>

// Battle effects (voxel_battlefx, Deck only; docs/DECK_NOTES.md "Phase 15"). They only ever touch the 3D backdrop: the composite
// (deck_post.c) and a backdrop pass drawn after it and BEFORE Pass_Overlay2D, which draws the battlers, HP boxes and text.
//
// Game thread (battle script, src/battle_script_commands.c Cmd_hitanimation): a hit landed on `targetBattler`.
void DeckBattleFx_Hit(uint8_t moveType, uint8_t targetBattler, bool crit, uint16_t damage, uint16_t targetMaxHp);

// Composite parameters of this frame (all 0 / zoom 1 = no change).
typedef struct
{
    float hit;          // hit tint amount 0..1
    float aberration;   // RGB split, uv
    float flash;        // white flash (crit) 0..1
    float low;          // low HP red edge and desaturation 0..1 (the pulse is in it)
    float tint[3];      // hit tint colour
    float entry;        // 1 -> 0 over the first moments of a battle
    float center[2];    // hit centre, composite uv (y up)
    float radius;       // hit tint radius (uv height units)
    float offset[2];    // drift + shake, uv
    float zoom;         // >= 1: drift and entry zoom
} DeckBattleFxFrame;

// GL thread, once per battle frame before DeckPost_Run. false: effects off (level OFF, env DECK_BATTLEFX_OFF, build failed).
bool DeckBattleFx_Begin(float seconds, float fade, DeckBattleFxFrame *out);
bool DeckBattleFx_Enabled(void);   // level >= LOW and the program built (for DeckPost_WantScene)
// GL thread, after DeckPost_Run, before the overlay: motes, weather, spotlights, hit glow, additive, into the window.
void DeckBattleFx_DrawBackdrop(float seconds, float fade);
// Battle scenery (voxel_battlescene): type glow and foreground bokeh, drawn right after DrawBackdrop. Level -1 = off (also when voxel_shaders is 0).
int DeckBattleFx_SceneLevel(void);
float DeckBattleFx_SceneK(void);     // 0 off, else 0.4 / 0.7 / 1.0 (LOW / MEDIUM / HIGH)
bool DeckBattleFx_SceneDebug(void);  // env DECK_BATTLESCENE_DEBUG
void DeckBattleFx_DrawScene(float seconds, float fade);
void DeckBattleFx_Init(void);
void DeckBattleFx_Shutdown(void);

#endif
