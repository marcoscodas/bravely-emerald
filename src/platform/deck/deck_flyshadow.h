#ifndef GUARD_DECK_FLYSHADOW_H
#define GUARD_DECK_FLYSHADOW_H

#include <stdbool.h>

// Flying Pokemon shadows (Deck only, setting voxel_flyshadow = FLYING SHADOWS). Now and then the soft shadow of a real flying
// species (chosen from the current map's wild table, else a Hoenn regional list) glides in a straight line across the ground
// flat under the camera target. The silhouette is the alpha mask of the species' own follower sprite (followers/mon/NNN.bin),
// read at run time, the frames of the heading's direction animating the wing beat. GL (Core 0) thread only. Env
// DECK_FLYSHADOW_NOW=1 spawns one at once and one every 6 s; DECK_FLYSHADOW_SPECIES=<internal species id> forces the species.

// Once per frame. dt seconds, camera target x and z in world tiles, outdoor (not indoors or underground), battle
// (3D battle stage), rain, sun = sun strength 0..1. Spawns, moves and retires the shadows.
void DeckFlyShadow_Update(float dt, float camX, float camZ, bool outdoor, bool battle, bool rain, float sun);
// Draws the live shadows in the shadow pass (fixed pipeline, world space, depth tested, no depth write). groundY is the
// height of the ground plane under the camera target.
void DeckFlyShadow_Emit(float groundY);

#endif
