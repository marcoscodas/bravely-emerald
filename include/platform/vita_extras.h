#ifndef VITA_EXTRAS_H
#define VITA_EXTRAS_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    VITA_TAB_GENERAL = 0,   // Text, Sound, Volume, Fast-Forward, Button Remap
    VITA_TAB_WORLD,         // 3D Voxel, Day/Night, Clock Speed, HGSS Follower, Field QoL
    VITA_TAB_BATTLE,        // 3D Battles, Native HD HUD, Battle Speed, Move Split, Exp Share
    VITA_TAB_MODES,         // Nuzlocke, Level Cap, Save Slots, Scaler, Quit
    VITA_TAB_CHEATS,        // Shiny Odds, Always Catch, Infinite TMs, Money, Dex, Give Items
    VITA_TAB_COUNT,

    // Legacy aliases preserved for compatibility
    VITA_TAB_SYSTEM       = VITA_TAB_GENERAL,
    VITA_TAB_ENHANCEMENTS = VITA_TAB_WORLD
};

typedef struct
{
    uint8_t tab;                  // VITA_TAB_*
    const char *group;            // Emerald 3DS sub-group header label
    const char *name;             // ASCII label displayed in menu
    const char *key;              // Key name in settings.txt
    uint8_t count;                // Number of cyclic values (0: executable action)
    uint8_t fallback;             // Default value when absent from settings.txt
    const char *const *values;    // Value labels (e.g. "OFF", "ON")
    void (*act)(void);            // Callback upon value change or action execution
    void (*step)(int direction);  // Optional custom stepper (e.g. GiveItemStep)
    const uint8_t *(*text)(void); // Optional dynamic text in game charmap
} VitaExtra;

extern const VitaExtra gVitaExtras[];
extern const unsigned gVitaExtraCount;
extern const char *const gVitaExtrasOffOn[2];

// Lifecycle & Settings Persistence (ux0:data/POKEVITA/settings.txt)
void VitaExtras_Init(void);
void VitaExtras_Save(void);

// Fast cached extras for high-frequency gameplay queries
typedef enum
{
    FAST_EXTRA_DAY_NIGHT = 0,
    FAST_EXTRA_TIMED_ENCOUNTERS,
    FAST_EXTRA_CLOCK_SPEED,
    FAST_EXTRA_RUN_INDOORS,
    FAST_EXTRA_MOVE_SPLIT,
    FAST_EXTRA_FOLLOWER,
    FAST_EXTRA_VOXEL_3D,
    FAST_EXTRA_INFINITE_TMS,
    FAST_EXTRA_REPEL_PROMPT,
    FAST_EXTRA_IVS_EVS,
    FAST_EXTRA_EXP_SHARE,
    FAST_EXTRA_EXP_CATCH,
    FAST_EXTRA_AUTO_RUN,
    FAST_EXTRA_FIELD_HMS,
    FAST_EXTRA_VOXEL_BATTLE,
    FAST_EXTRA_BATTLE_SPEED,
    FAST_EXTRA_FAST_EGGS,
    FAST_EXTRA_NUZLOCKE,
    FAST_EXTRA_LEVEL_CAP,
    FAST_EXTRA_SHINY_ODDS,
    FAST_EXTRA_ALWAYS_CATCH,
    FAST_EXTRA_TRADE_EVO,
    FAST_EXTRA_VOXEL_RES,
    FAST_EXTRA_VOXEL_FPS,
    FAST_EXTRA_VOXEL_BLUR,
    FAST_EXTRA_UI_V2,          // new UI (phase B test screen on START)
    FAST_EXTRA_UI_THEME,       // colour theme of the new UI menus (UI_THEME_*)
#ifdef PLATFORM_DECK
    FAST_EXTRA_VOXEL_QUALITY,  // Deck effects preset: 0 low, 1 medium, 2 high
    FAST_EXTRA_VOXEL_SHADERS,  // Deck shader master switch: 0 off (legacy picture), 1 on
    // Leveled effect keys (deck_levels.h): 0 OFF, 1 AUTO (follow voxel_quality; what old files stored as "on"), 2 LOW, 3 MEDIUM, 4 HIGH
    FAST_EXTRA_VOXEL_GRADE,    // Deck colour grade + vignette
    FAST_EXTRA_VOXEL_BLOOM,    // Deck bloom
    FAST_EXTRA_VOXEL_SWAY,     // Deck wind sway of tree crowns and long grass (amplitude)
    FAST_EXTRA_VOXEL_GRASS,    // Deck standing tall grass + flowers (density)
    FAST_EXTRA_VOXEL_RAIN,     // Deck rain streaks (count); off = legacy rain particles
    FAST_EXTRA_VOXEL_PARTICLES, // Deck particles (pollen, fireflies, leaves) density; off = legacy motes
    FAST_EXTRA_VOXEL_OUTLINE,  // Deck character outline and sun rim (strength)
    FAST_EXTRA_VOXEL_SHADOW,   // Deck soft blob shadow under characters (strength)
    FAST_EXTRA_VOXEL_WET,      // Deck wet ground in rain (puddle strength)
    FAST_EXTRA_VOXEL_MIST,     // Deck rain mist (opacity)
    FAST_EXTRA_VOXEL_SPLASH,   // Deck rain splash rings (count)
    FAST_EXTRA_VOXEL_GLINT,    // Deck water glints (density)
    FAST_EXTRA_VOXEL_LUT,      // Deck LUT colour grade (16x16x16 table built per time of day); off = the procedural grade
    FAST_EXTRA_VOXEL_SHAFTS,   // Deck light shafts (screen-space radial blur toward the sun, strength)
    FAST_EXTRA_VOXEL_TILTSHIFT, // Deck depth-based tilt-shift with bokeh (strength); off = the band blur
    FAST_EXTRA_VOXEL_PITCH,    // Deck camera tilt: 0 classic 40 deg, 1 36, 2 32 (default), 3 28, 4 24
    FAST_EXTRA_VOXEL_BUMP,     // Deck pseudo normal map: relief from the albedo luminance, lit by the sun (strength)
    FAST_EXTRA_VOXEL_SUNSHADOW,   // Deck sun shadow map (casters: trees, buildings, characters; resolved in the composite)
    FAST_EXTRA_VOXEL_BATTLEFX,    // Deck battle effects: type hit flash, drift, motes, spotlight, weather, entry, low HP, aberration
    FAST_EXTRA_VOXEL_BATTLEDIM,   // Deck 3D battle backdrop: blur radius and black overlay opacity
    FAST_EXTRA_VOXEL_GROUNDRELIEF,   // Deck ground micro-relief: procedural height by ground kind plus undulation, shading only
    FAST_EXTRA_VOXEL_UIGLOW,   // Deck UI depth glow: faint dark halo under the UI (UI passes only)
    FAST_EXTRA_AMBIENT_VOLUME, // Deck ambient bed volume, key ambient_level: 0 OFF (no DSP), 1 LOW, 2 MEDIUM (default), 3 HIGH
    FAST_EXTRA_AMBIENT_DENSITY, // Deck ambient one-shot rate, key ambient_density: 0 SPARSE (default), 1 NORMAL, 2 FREQUENT
    FAST_EXTRA_VOXEL_FLATFX,   // Deck 2D scene effects (intro, title, menus): bloom, glow, grade, vignette, grain, pulse; off = the plain picture
    FAST_EXTRA_VOXEL_TITLE,    // Deck title screen: 0 ORIGINAL, 1 HD-2D (BRAVELY EMERALD)
    FAST_EXTRA_VOXEL_REVEAL,   // Deck reveal flash: title and intro scenes start flat and flash into the HD-2D look (0 off, 1 on)
#endif
#ifdef PLATFORM_DECK
    // UI polish (docs/DECK_NOTES.md "UI polish"). Leveled like the effects above (0 OFF, 1 AUTO, 2 LOW, 3 MEDIUM, 4 HIGH)
    FAST_EXTRA_VOXEL_UIGLASS,  // frosted glass behind panels
    FAST_EXTRA_VOXEL_UITINT,   // moving two-colour gradient on panels
    FAST_EXTRA_VOXEL_UITEXT,   // text shadow / outline (LOW shadow, MEDIUM outline + shadow, HIGH full outline + shadow)
    FAST_EXTRA_VOXEL_UISEL,    // animated menu cursor
    FAST_EXTRA_VOXEL_UIANIM,   // menu open / close fade and slide
    FAST_EXTRA_VOXEL_UIBARS,   // HP / EXP bars: smooth fill, colour, low HP glow
    FAST_EXTRA_VOXEL_UIGLYPHS, // button prompts: 0 OFF (the port's PlayStation glyphs), 1 GBA, 2 DECK (stored value = index)
    FAST_EXTRA_VOXEL_UILAYOUT, // wide layout, item shadows, party bob
    FAST_EXTRA_VOXEL_AO,       // Deck ground truth ambient occlusion (half-res GTAO from the scene depth), key voxel_ao
    FAST_EXTRA_VOXEL_BATTLESCENE,  // Deck battle scenery: sharp focus band, weather, grade and light shafts, foreground leaves, type glow in 3D battles, key voxel_battlescene
    FAST_EXTRA_VOXEL_CLOUDS,  // Deck cloud shadows drifting over the ground, key voxel_clouds
    FAST_EXTRA_VOXEL_WINDOWS,  // Deck lit windows at night, key voxel_windows
    FAST_EXTRA_VOXEL_CROWNS,  // Deck tree crown shading (top lit, dark underneath), key voxel_crowns
    FAST_EXTRA_VOXEL_SPRLIGHT,  // Deck sun lit characters (side and rim light), key voxel_sprlight
    FAST_EXTRA_VOXEL_FLYSHADOW,  // Deck shadows of flying Pokemon crossing the ground, key voxel_flyshadow
#endif
    FAST_EXTRA_COUNT
} FastExtraId;

// Fast O(1) query & mutation
int VitaExtras_GetFast(FastExtraId id);
int VitaExtras_Get(const char *key);
void VitaExtras_Set(const char *key, int value);
void VitaExtras_Step(const VitaExtra *extra, int direction);

// Tab querying
unsigned VitaExtras_GetTabExtraCount(uint8_t tab);
const VitaExtra *VitaExtras_GetTabExtra(uint8_t tab, unsigned index);

#endif // VITA_EXTRAS_H
