#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/vita_extras.h"
#include "platform/vita_daynight.h"
#include "platform/vita_nuzlocke.h"
#include "vita_platform.h"
#include "platform.h"
#include "global.h"
#include "pokedex.h"
#include "pokemon.h"
#include "item.h"
#include "money.h"
#include "constants/item.h"
#include "constants/items.h"
#include "constants/pokedex.h"
#include "constants/characters.h"
#include "event_data.h"
#include "sound.h"
#include "constants/songs.h"
#ifdef PLATFORM_DECK
#include "../deck/deck_window.h"
#endif

#define MAX_SETTINGS_ENTRIES 128
#define SETTINGS_FILENAME "settings.txt"

typedef struct
{
    char key[32];
    int32_t value;
} SettingEntry;

static SettingEntry sSettings[MAX_SETTINGS_ENTRIES];
static uint32_t sSettingCount = 0;
static bool sInitialized = false;
static int32_t sFastSettings[FAST_EXTRA_COUNT];

static inline int32_t LookupFastId(const char *key)
{
    if (key == NULL || key[0] == '\0')
        return -1;
    switch (key[0])
    {
    case 'a':
        if (strcmp(key, "auto_run") == 0) return FAST_EXTRA_AUTO_RUN;
        if (strcmp(key, "always_catch") == 0) return FAST_EXTRA_ALWAYS_CATCH;
#ifdef PLATFORM_DECK
        if (strcmp(key, "ambient_level") == 0) return FAST_EXTRA_AMBIENT_VOLUME;
        if (strcmp(key, "ambient_density") == 0) return FAST_EXTRA_AMBIENT_DENSITY;
#endif
        break;
    case 'b':
        if (strcmp(key, "battle_speed") == 0) return FAST_EXTRA_BATTLE_SPEED;
        break;
    case 'c':
        if (strcmp(key, "clock_speed") == 0) return FAST_EXTRA_CLOCK_SPEED;
        break;
    case 'd':
        if (strcmp(key, "day_night") == 0) return FAST_EXTRA_DAY_NIGHT;
        break;
    case 'e':
        if (strcmp(key, "exp_share") == 0) return FAST_EXTRA_EXP_SHARE;
        if (strcmp(key, "exp_catch") == 0) return FAST_EXTRA_EXP_CATCH;
        break;
    case 'f':
        if (strcmp(key, "follower") == 0) return FAST_EXTRA_FOLLOWER;
        if (strcmp(key, "field_hms") == 0) return FAST_EXTRA_FIELD_HMS;
        if (strcmp(key, "fast_eggs") == 0) return FAST_EXTRA_FAST_EGGS;
        break;
    case 'i':
        if (strcmp(key, "infinite_tms") == 0) return FAST_EXTRA_INFINITE_TMS;
        if (strcmp(key, "ivs_evs") == 0) return FAST_EXTRA_IVS_EVS;
        break;
    case 'l':
        if (strcmp(key, "level_cap") == 0) return FAST_EXTRA_LEVEL_CAP;
        break;
    case 'm':
        if (strcmp(key, "move_split") == 0) return FAST_EXTRA_MOVE_SPLIT;
        break;
    case 'n':
        if (strcmp(key, "nuzlocke") == 0) return FAST_EXTRA_NUZLOCKE;
        break;
    case 'r':
        if (strcmp(key, "run_indoors") == 0) return FAST_EXTRA_RUN_INDOORS;
        if (strcmp(key, "repel_prompt") == 0) return FAST_EXTRA_REPEL_PROMPT;
        break;
    case 's':
        if (strcmp(key, "shiny_odds") == 0) return FAST_EXTRA_SHINY_ODDS;
        break;
    case 't':
        if (strcmp(key, "timed_encounters") == 0) return FAST_EXTRA_TIMED_ENCOUNTERS;
        if (strcmp(key, "trade_evo") == 0) return FAST_EXTRA_TRADE_EVO;
        break;
    case 'u':
        if (strcmp(key, "ui_v2") == 0) return FAST_EXTRA_UI_V2;
        if (strcmp(key, "ui_theme") == 0) return FAST_EXTRA_UI_THEME;
        break;
    case 'v':
        if (strcmp(key, "voxel_3d") == 0) return FAST_EXTRA_VOXEL_3D;
        if (strcmp(key, "voxel_battle") == 0) return FAST_EXTRA_VOXEL_BATTLE;
        if (strcmp(key, "voxel_res") == 0) return FAST_EXTRA_VOXEL_RES;
        if (strcmp(key, "voxel_fps") == 0) return FAST_EXTRA_VOXEL_FPS;
        if (strcmp(key, "voxel_blur") == 0) return FAST_EXTRA_VOXEL_BLUR;
#ifdef PLATFORM_DECK
        if (strcmp(key, "voxel_quality") == 0) return FAST_EXTRA_VOXEL_QUALITY;
        if (strcmp(key, "voxel_shaders") == 0) return FAST_EXTRA_VOXEL_SHADERS;
        if (strcmp(key, "voxel_grade") == 0) return FAST_EXTRA_VOXEL_GRADE;
        if (strcmp(key, "voxel_bloom") == 0) return FAST_EXTRA_VOXEL_BLOOM;
        if (strcmp(key, "voxel_sway") == 0) return FAST_EXTRA_VOXEL_SWAY;
        if (strcmp(key, "voxel_grass") == 0) return FAST_EXTRA_VOXEL_GRASS;
        if (strcmp(key, "voxel_rain") == 0) return FAST_EXTRA_VOXEL_RAIN;
        if (strcmp(key, "voxel_particles") == 0) return FAST_EXTRA_VOXEL_PARTICLES;
        if (strcmp(key, "voxel_outline") == 0) return FAST_EXTRA_VOXEL_OUTLINE;
        if (strcmp(key, "voxel_shadow") == 0) return FAST_EXTRA_VOXEL_SHADOW;
        if (strcmp(key, "voxel_wet") == 0) return FAST_EXTRA_VOXEL_WET;
        if (strcmp(key, "voxel_mist") == 0) return FAST_EXTRA_VOXEL_MIST;
        if (strcmp(key, "voxel_splash") == 0) return FAST_EXTRA_VOXEL_SPLASH;
        if (strcmp(key, "voxel_glint") == 0) return FAST_EXTRA_VOXEL_GLINT;
        if (strcmp(key, "voxel_pitch") == 0) return FAST_EXTRA_VOXEL_PITCH;
        if (strcmp(key, "voxel_tiltshift") == 0) return FAST_EXTRA_VOXEL_TILTSHIFT;
        if (strcmp(key, "voxel_shafts") == 0) return FAST_EXTRA_VOXEL_SHAFTS;
        if (strcmp(key, "voxel_lut") == 0) return FAST_EXTRA_VOXEL_LUT;
        if (strcmp(key, "voxel_bump") == 0) return FAST_EXTRA_VOXEL_BUMP;
        if (strcmp(key, "voxel_uiglow") == 0) return FAST_EXTRA_VOXEL_UIGLOW;
        if (strcmp(key, "voxel_groundrelief") == 0) return FAST_EXTRA_VOXEL_GROUNDRELIEF;
        if (strcmp(key, "voxel_battledim") == 0) return FAST_EXTRA_VOXEL_BATTLEDIM;
        if (strcmp(key, "voxel_battlefx") == 0) return FAST_EXTRA_VOXEL_BATTLEFX;
        if (strcmp(key, "voxel_sunshadow") == 0) return FAST_EXTRA_VOXEL_SUNSHADOW;
        if (strcmp(key, "voxel_uiglass") == 0) return FAST_EXTRA_VOXEL_UIGLASS;
        if (strcmp(key, "voxel_uitint") == 0) return FAST_EXTRA_VOXEL_UITINT;
        if (strcmp(key, "voxel_uitext") == 0) return FAST_EXTRA_VOXEL_UITEXT;
        if (strcmp(key, "voxel_uisel") == 0) return FAST_EXTRA_VOXEL_UISEL;
        if (strcmp(key, "voxel_uianim") == 0) return FAST_EXTRA_VOXEL_UIANIM;
        if (strcmp(key, "voxel_uibars") == 0) return FAST_EXTRA_VOXEL_UIBARS;
        if (strcmp(key, "voxel_uiglyphs") == 0) return FAST_EXTRA_VOXEL_UIGLYPHS;
        if (strcmp(key, "voxel_uilayout") == 0) return FAST_EXTRA_VOXEL_UILAYOUT;
        if (strcmp(key, "voxel_flatfx") == 0) return FAST_EXTRA_VOXEL_FLATFX;
        if (strcmp(key, "voxel_title") == 0) return FAST_EXTRA_VOXEL_TITLE;
        if (strcmp(key, "voxel_reveal") == 0) return FAST_EXTRA_VOXEL_REVEAL;
        if (strcmp(key, "voxel_ao") == 0) return FAST_EXTRA_VOXEL_AO;
        if (strcmp(key, "voxel_battlescene") == 0) return FAST_EXTRA_VOXEL_BATTLESCENE;
        if (strcmp(key, "voxel_clouds") == 0) return FAST_EXTRA_VOXEL_CLOUDS;
        if (strcmp(key, "voxel_windows") == 0) return FAST_EXTRA_VOXEL_WINDOWS;
        if (strcmp(key, "voxel_crowns") == 0) return FAST_EXTRA_VOXEL_CROWNS;
        if (strcmp(key, "voxel_sprlight") == 0) return FAST_EXTRA_VOXEL_SPRLIGHT;
        if (strcmp(key, "voxel_flyshadow") == 0) return FAST_EXTRA_VOXEL_FLYSHADOW;
#endif
        break;
    default:
        break;
    }
    return -1;
}

static void SyncFastExtras(void)
{
    for (uint32_t i = 0; i < FAST_EXTRA_COUNT; i++)
        sFastSettings[i] = 0;

    for (uint32_t i = 0; i < sSettingCount; i++)
    {
        const int32_t fastId = LookupFastId(sSettings[i].key);
        if (fastId >= 0)
            sFastSettings[fastId] = sSettings[i].value;
    }
}

int32_t VitaExtras_GetFast(FastExtraId id)
{
    if (!sInitialized)
        VitaExtras_Init();
    if ((uint32_t)id < FAST_EXTRA_COUNT)
        return sFastSettings[id];
    return 0;
}

const char *const gVitaExtrasOffOn[2] = { "OFF", "ON" };
static const char *const sSpeedValues[] = { "2X", "3X", "4X", "5X", NULL };
static const char *const sFFButtonValues[] = { "HOLD", "TOGGLE", NULL };
static const char *const sFilterValues[] = { "NEAREST", "BILINEAR", "SHARP", NULL };
// Order = UI_THEME_* (src/platform/ui/ui_internal.h); RED is the default
static const char *const sUiThemeValues[] = { "RED", "EMERALD", "CRYSTAL", "TROPICAL", "ROYAL", NULL };
static const char *const sVoxelFpsValues[] = { "30", "60", NULL };
#ifdef PLATFORM_DECK
static const char *const sVoxelQualityValues[] = { "LOW", "MEDIUM", "HIGH", NULL };
// Level rows (deck_levels.h): index = stored value. 1 = AUTO is what old files stored as ON, so 0/1 keys keep their meaning.
static const char *const sVoxelLevelValues[] = { "OFF", "AUTO", "LOW", "MEDIUM", "HIGH", NULL };
static const char *const sVoxelPitchValues[] = { "CLASSIC", "36", "32", "28", "24", NULL };
static const char *const sUiGlyphValues[] = { "OFF", "GBA", "DECK", NULL };
static const char *const sAmbientValues[] = { "OFF", "LOW", "MEDIUM", "HIGH", NULL };
static const char *const sAmbientDensityValues[] = { "SPARSE", "NORMAL", "FREQUENT", NULL };
static const char *const sTitleValues[] = { "ORIGINAL", "HD-2D", NULL };
#endif
static const char *const sSaveSlotValues[] = { "SLOT 1", "SLOT 2", "SLOT 3", NULL };
static const char *const sDayNightValues[] = { "OFF", "AUTO", NULL };
static const char *const sClockSpeedValues[] = { "1X", "6X", "12X", "24X", "60X", "120X", "1000X", NULL };
static const char *const sBattleSpeedValues[] = { "1X", "1.5X", "2X", "3X", "4X", NULL };
static const char *const sLevelCapValues[] = { "OFF", "GYM CAP", "STRICT", NULL };
static const char *const sShinyOddsValues[] = { "1/8192", "1/4096", "1/1024", "1/256", "1/64", "ALWAYS", NULL };
#ifdef PLATFORM_DECK
static const char *const sScreenModeValues[] = { "WINDOWED", "FULLSCREEN", NULL };

// RESOLUTION row: the choices are the sane display modes found at start (deck_video.c), so it steps itself.
static void ResolutionStep(int32_t direction)
{
    const int32_t count = DeckVideo_ResolutionCount();
    int32_t next = (DeckVideo_GetResolutionIndex() + direction) % count;

    if (next < 0)
        next += count;
    VitaExtras_Set("resolution", next);
}
#endif
static const char *const sActionSetValues[] = { "EXECUTE", NULL };
static const char *const sGivePockets[] = { "ITEMS", "POKE BALLS", "TMS & HMS", "BERRIES", "KEY ITEMS", NULL };
static const char *const sGiveCounts[] = { "1", "5", "10", "50", "99", NULL };
static const u8 sGiveCountValues[] = { 1, 5, 10, 50, 99 };

static void ActionCompleteHoennDex(void)
{
    for (u16 hoenn = 1; hoenn <= HOENN_DEX_COUNT; hoenn++)
    {
        const u16 national = HoennToNationalOrder(hoenn);
        GetSetPokedexFlag(national, FLAG_SET_SEEN);
        GetSetPokedexFlag(national, FLAG_SET_CAUGHT);
    }
    PlaySE(SE_SUCCESS);
}

static void ActionCompleteNationalDex(void)
{
    EnableNationalPokedex();
    for (u16 national = 1; national <= NATIONAL_DEX_COUNT; national++)
    {
        GetSetPokedexFlag(national, FLAG_SET_SEEN);
        GetSetPokedexFlag(national, FLAG_SET_CAUGHT);
    }
    PlaySE(SE_SUCCESS);
}

static void ActionInfiniteMoney(void)
{
    SetMoney(&gSaveBlock1Ptr->money, 999999);
    PlaySE(SE_SUCCESS);
}

static bool8 GiveItemUsable(const u16 item, const u8 pocket)
{
    u8 name[ITEM_NAME_LENGTH + 1];
    if (item == ITEM_NONE || item >= ITEMS_COUNT || GetPocketByItemId(item) != pocket)
        return FALSE;
    CopyItemName(item, name);
    return name[0] != CHAR_QUESTION_MARK && name[0] != EOS;
}

static u8 GivePocket(void)
{
    const int32_t pocket = VitaExtras_Get("give_pocket");
    return POCKET_ITEMS + (pocket >= 0 && pocket < 5 ? pocket : 0);
}

static u16 GiveItem(void)
{
    u16 item = (u16)VitaExtras_Get("give_item");
    const u8 pocket = GivePocket();
    if (GiveItemUsable(item, pocket))
        return item;
    for (item = 1; item < ITEMS_COUNT; item++)
    {
        if (GiveItemUsable(item, pocket))
            return item;
    }
    return ITEM_NONE;
}

static void GiveItemStep(int32_t direction)
{
    u16 item = GiveItem();
    const u8 pocket = GivePocket();
    if (item == ITEM_NONE)
        return;
    for (u16 tries = 0; tries < ITEMS_COUNT; tries++)
    {
        item = direction < 0 ? (item <= 1 ? ITEMS_COUNT - 1 : item - 1) : (item + 1 >= ITEMS_COUNT ? 1 : item + 1);
        if (GiveItemUsable(item, pocket))
            break;
    }
    VitaExtras_Set("give_item", item);
}

static const u8 *GiveItemText(void)
{
    static u8 sName[ITEM_NAME_LENGTH + 1];
    const u16 item = GiveItem();
    if (item == ITEM_NONE)
        sName[0] = EOS;
    else
        CopyItemName(item, sName);
    return sName;
}

static void GiveItemsNow(void)
{
    const u16 item = GiveItem();
    const int32_t count = VitaExtras_Get("give_count");
    u16 quantity = sGiveCountValues[count >= 0 && count < 5 ? count : 0];
    if (GetPocketByItemId(item) == POCKET_KEY_ITEMS)
        quantity = 1;
    if (item != ITEM_NONE && CheckBagHasSpace(item, quantity) && AddBagItem(item, quantity))
        PlaySE(SE_SUCCESS);
    else
        PlaySE(SE_FAILURE);
}

static void ActionMaxContestStats(void)
{
    const int32_t count = (int32_t)CalculatePlayerPartyCount();
    if (count <= 0)
    {
        PlaySE(SE_FAILURE);
        return;
    }
    const uint8_t maxStat = 255;
    for (int32_t i = 0; i < count; i++)
    {
        struct Pokemon *const mon = &gPlayerParty[i];
        if (GetMonData(mon, MON_DATA_IS_EGG))
            continue;
        SetMonData(mon, MON_DATA_COOL, &maxStat);
        SetMonData(mon, MON_DATA_BEAUTY, &maxStat);
        SetMonData(mon, MON_DATA_CUTE, &maxStat);
        SetMonData(mon, MON_DATA_SMART, &maxStat);
        SetMonData(mon, MON_DATA_TOUGH, &maxStat);
        SetMonData(mon, MON_DATA_SHEEN, &maxStat);
    }
    PlaySE(SE_SUCCESS);
}

const VitaExtra gVitaExtras[] = {
    // ---- [GENERAL] ----
    { VITA_TAB_GENERAL, "PACING",     "FAST FWD SPEED",    "speed",            4, 1, sSpeedValues,       NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "PACING",     "FF BUTTON MODE",    "ff_button",        2, 0, sFFButtonValues,    NULL, NULL, NULL },
#ifdef PLATFORM_DECK
    { VITA_TAB_GENERAL, "SOUND",      "AMBIENT VOLUME",    "ambient_level",    4, 2, sAmbientValues,     NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "SOUND",      "AMBIENT FREQUENCY", "ambient_density",  3, 0, sAmbientDensityValues, NULL, NULL, NULL },
#endif
    { VITA_TAB_GENERAL, "DISPLAY",    "2D SCALER",         "filter",           3, 0, sFilterValues,      NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "DISPLAY",    "MENU COLOR",        "ui_theme",         5, 0, sUiThemeValues,     NULL, NULL, NULL },
#ifdef PLATFORM_DECK
#ifdef PLATFORM_WINDOWS
    { VITA_TAB_GENERAL, "DISPLAY",    "SCREEN MODE",       "fullscreen",       2, 0, sScreenModeValues,  NULL, NULL, NULL },
#else
    { VITA_TAB_GENERAL, "DISPLAY",    "SCREEN MODE",       "fullscreen",       2, 1, sScreenModeValues,  NULL, NULL, NULL },
#endif
    { VITA_TAB_GENERAL, "DISPLAY",    "RESOLUTION",        "resolution",       DECK_RES_MAX, 0, gDeckResValues, NULL, ResolutionStep, NULL },
#endif
#ifdef PLATFORM_DECK
    { VITA_TAB_GENERAL, "INTERFACE",  "PANEL GLASS",       "voxel_uiglass",    5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "PANEL TINT",        "voxel_uitint",     5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "TEXT CLARITY",      "voxel_uitext",     5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "MENU CURSOR",       "voxel_uisel",      5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "MENU ANIMATION",    "voxel_uianim",     5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "HP BARS",           "voxel_uibars",     5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "BUTTON ICONS",      "voxel_uiglyphs",   3, 2, sUiGlyphValues,     NULL, NULL, NULL },
    { VITA_TAB_GENERAL, "INTERFACE",  "WIDE UI LAYOUT",    "voxel_uilayout",   5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
#endif

    // ---- [WORLD] ----
    { VITA_TAB_WORLD,   "RENDERER",   "3D VOXEL DIORAMA",  "voxel_3d",         2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
#ifdef PLATFORM_DECK
    { VITA_TAB_WORLD,   "RENDERER",   "DIORAMA BLUR (DOF)","voxel_blur",       5, 1, sVoxelLevelValues,  NULL, NULL, NULL },
#else
    { VITA_TAB_WORLD,   "RENDERER",   "DIORAMA BLUR (DOF)","voxel_blur",       2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
#endif
    { VITA_TAB_WORLD,   "RENDERER",   "TARGET FRAMERATE",  "voxel_fps",        2, 0, sVoxelFpsValues,    NULL, NULL, NULL },
#ifdef PLATFORM_DECK
    { VITA_TAB_WORLD,   "EFFECTS",    "EFFECTS PRESET",    "voxel_quality",    3, 1, sVoxelQualityValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS",    "SHADERS",           "voxel_shaders",    2, 1, gVitaExtrasOffOn,    NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "COLOR GRADE",      "voxel_grade", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "BLOOM",            "voxel_bloom", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "WIND SWAY",        "voxel_sway", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "GRASS & FLOWERS",  "voxel_grass", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "RAIN STREAKS",     "voxel_rain", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "WET GROUND",       "voxel_wet", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "RAIN MIST",        "voxel_mist", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "RAIN SPLASHES",    "voxel_splash", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "PARTICLES",        "voxel_particles", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "WATER GLINTS",     "voxel_glint", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "CHARACTER OUTLINE","voxel_outline", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "SOFT SHADOWS",     "voxel_shadow", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "3D RESOLUTION",    "voxel_res", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "LUT GRADE",        "voxel_lut", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "RELIEF (BUMP)",    "voxel_bump", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "UI DEPTH GLOW", "voxel_uiglow", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "GROUND DETAIL", "voxel_groundrelief", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "BATTLE BACKDROP", "voxel_battledim", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "BATTLE EFFECTS", "voxel_battlefx", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "SUN SHADOWS", "voxel_sunshadow", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "AMBIENT OCCLUSION", "voxel_ao", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "BATTLE SCENERY", "voxel_battlescene", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "CLOUD SHADOWS", "voxel_clouds", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "NIGHT WINDOWS", "voxel_windows", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "TREE SHADING", "voxel_crowns", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "SPRITE LIGHTING", "voxel_sprlight", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "FLYING SHADOWS", "voxel_flyshadow", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "LIGHT SHAFTS",     "voxel_shafts", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "TILT-SHIFT",       "voxel_tiltshift", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "INTRO EFFECTS", "voxel_flatfx", 5, 1, sVoxelLevelValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "TITLE SCREEN",     "voxel_title", 2, 1, sTitleValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS", "REVEAL FLASH",     "voxel_reveal", 2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS",    "CAMERA TILT",       "voxel_pitch",      5, 2, sVoxelPitchValues,   NULL, NULL, NULL },
#endif
    { VITA_TAB_WORLD,   "TIME & SKY", "DAY / NIGHT CYCLE", "day_night",        2, 1, sDayNightValues,    NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "TIME & SKY", "TIMED ENCOUNTERS",  "timed_encounters", 2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "TIME & SKY", "CLOCK SPEED",       "clock_speed",      7, 0, sClockSpeedValues,  NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "COMPANION",  "HGSS FOLLOWER",     "follower",         2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EXPLORATION","RUN INDOORS",       "run_indoors",      2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EXPLORATION","AUTO RUN (NO B)",   "auto_run",         2, 0, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EXPLORATION","BAG HM SHORTCUTS",  "field_hms",        2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EXPLORATION","REPEL PROMPT",      "repel_prompt",     2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },

    // ---- [BATTLE] ----
    { VITA_TAB_BATTLE,  "ARENA",      "3D VOXEL BATTLE",   "voxel_battle",     2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_BATTLE,  "ARENA",      "BATTLE SPEED",      "battle_speed",     5, 0, sBattleSpeedValues, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,  "MECHANICS",  "PHYS/SPEC SPLIT",   "move_split",       2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_BATTLE,  "MECHANICS",  "PARTY EXP SHARE",   "exp_share",        2, 0, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_BATTLE,  "MECHANICS",  "EXP ON CATCH",      "exp_catch",        2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_BATTLE,  "INSPECTION", "IVS & EVS STATS",   "ivs_evs",          2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },

    // ---- [MODES] ----
    { VITA_TAB_MODES,   "CHALLENGE",  "NUZLOCKE MODE",     "nuzlocke",         2, 0, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_MODES,   "CHALLENGE",  "LEVEL CAP",         "level_cap",        3, 0, sLevelCapValues,    NULL, NULL, NULL },
    { VITA_TAB_MODES,   "PROFILES",   "ACTIVE SAVE SLOT",  "save_slot",        3, 0, sSaveSlotValues,    NULL, NULL, NULL },
    { VITA_TAB_MODES,   "PREVIEW",    "NEW UI (TEST)",     "ui_v2",            2, 0, gVitaExtrasOffOn,   NULL, NULL, NULL },

    // ---- [CHEATS] ----
    { VITA_TAB_CHEATS,  "CONVENIENCE","INFINITE TMS",      "infinite_tms",     2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "CONVENIENCE","TRADE EVO LV40",    "trade_evo",        2, 1, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "CONVENIENCE","FAST HATCH EGGS",   "fast_eggs",        2, 0, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "ENCOUNTERS", "SHINY ODDS",        "shiny_odds",       6, 0, sShinyOddsValues,   NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "ENCOUNTERS", "ALWAYS CATCH",      "always_catch",     2, 0, gVitaExtrasOffOn,   NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "REWARDS",    "MAX MONEY ($999K)", "give_money",       0, 0, sActionSetValues,   ActionInfiniteMoney,     NULL, NULL },
    { VITA_TAB_CHEATS,  "ITEM SPAWN", "GIVE POCKET",       "give_pocket",      5, 0, sGivePockets,       NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "ITEM SPAWN", "GIVE ITEM",         "give_item",        0, 0, NULL,               NULL, GiveItemStep, GiveItemText },
    { VITA_TAB_CHEATS,  "ITEM SPAWN", "GIVE COUNT",        "give_count",       5, 0, sGiveCounts,        NULL, NULL, NULL },
    { VITA_TAB_CHEATS,  "ITEM SPAWN", "DELIVER ITEM",      "give_now",         0, 0, sActionSetValues,   GiveItemsNow,            NULL, NULL },
    { VITA_TAB_CHEATS,  "POKEDEX",    "COMPLETE HOENN DEX","dex_hoenn",        0, 0, sActionSetValues,   ActionCompleteHoennDex,  NULL, NULL },
    { VITA_TAB_CHEATS,  "POKEDEX",    "UNLOCK NATIONAL",   "dex_national",     0, 0, sActionSetValues,   ActionCompleteNationalDex, NULL, NULL },
    { VITA_TAB_CHEATS,  "CONTESTS",   "MAX CONTEST STATS", "max_contest",      0, 0, sActionSetValues,   ActionMaxContestStats,     NULL, NULL },
};

const uint32_t gVitaExtraCount = sizeof(gVitaExtras) / sizeof(gVitaExtras[0]);

static int32_t FindSettingIndex(const char *key, const bool createIfMissing)
{
    for (uint32_t i = 0; i < sSettingCount; i++)
    {
        if (strcmp(sSettings[i].key, key) == 0)
            return (int32_t)i;
    }
    if (createIfMissing && sSettingCount < MAX_SETTINGS_ENTRIES)
    {
        const int32_t idx = (int32_t)sSettingCount++;
        strncpy(sSettings[idx].key, key, sizeof(sSettings[idx].key) - 1);
        sSettings[idx].key[sizeof(sSettings[idx].key) - 1] = '\0';
        sSettings[idx].value = 0;
        return idx;
    }
    return -1;
}

static void SyncPlatformSetting(const char *key, const int32_t value)
{
    if (strcmp(key, "auto_run") == 0) Platform_SetSetting(PLATFORM_SETTING_AUTO_RUN, (u8)value);
    else if (strcmp(key, "field_hms") == 0) Platform_SetSetting(PLATFORM_SETTING_HM_SHORTCUTS, (u8)value);
    else if (strcmp(key, "ivs_evs") == 0) Platform_SetSetting(PLATFORM_SETTING_STATS_INFO, (u8)value);
    else if (strcmp(key, "exp_share") == 0) Platform_SetSetting(PLATFORM_SETTING_EXP_SHARE, (u8)value);
    else if (strcmp(key, "voxel_3d") == 0) Platform_SetSetting(PLATFORM_SETTING_VOXEL_3D, (u8)value);
    else if (strcmp(key, "voxel_fps") == 0) Platform_SetSetting(PLATFORM_SETTING_VOXEL_FPS, (u8)value);
#ifdef PLATFORM_DECK
    else if (strcmp(key, "fullscreen") == 0) DeckVideo_RequestApply();
#endif
    else if (strcmp(key, "filter") == 0)
    {
        Platform_SetSetting(PLATFORM_SETTING_FILTER, (u8)value);
        Platform_SetSetting(PLATFORM_SETTING_UI_SCALE, (u8)(value != 0 ? 1 : 0));
    }
    else if (strcmp(key, "speed") == 0) Platform_SetSetting(PLATFORM_SETTING_FAST_FORWARD_SPEED, (u8)(value + 2));
    else if (strcmp(key, "ff_button") == 0) Platform_SetSetting(PLATFORM_SETTING_FAST_FORWARD_TOGGLE, (u8)value);
    else if (strcmp(key, "save_slot") == 0) Platform_SetSetting(PLATFORM_SETTING_SAVE_FILE, (u8)(value + 1));
}

#ifdef PLATFORM_DECK
static int32_t SettingValue(const char *key, const int32_t def)
{
    const int32_t i = FindSettingIndex(key, false);
    return i >= 0 ? sSettings[i].value : def;
}

static void SettingForce(const char *key, const int32_t value)
{
    const int32_t i = FindSettingIndex(key, true);
    if (i >= 0)
        sSettings[i].value = value;
}

// Files from before the level rows (no "fx_levels" key): bloom and grade stored 0 off, 1 on, 2 auto, which are 0 and 1
// in the level encoding (0 OFF, 1 AUTO, 2 LOW, 3 MEDIUM, 4 HIGH); every other 0/1 key already fits. The rain row used
// to switch four effects and the particles row the glints: an old OFF stays OFF for the keys split from them.
static void MigrateLevelSettings(const bool fileExisted)
{
    if (SettingValue("fx_levels", 0) == 0 && fileExisted)
    {
        static const char *const sOldTri[] = { "voxel_bloom", "voxel_grade" };
        for (int i = 0; i < 2; i++)
            if (SettingValue(sOldTri[i], 1) == 2)
                SettingForce(sOldTri[i], 1);
        if (SettingValue("voxel_rain", 1) == 0)
        {
            SettingForce("voxel_wet", 0);
            SettingForce("voxel_mist", 0);
            SettingForce("voxel_splash", 0);
        }
        if (SettingValue("voxel_particles", 1) == 0)
            SettingForce("voxel_glint", 0);
    }
    // The ambient volume row was 0 OFF, 1..10 (default 6, key ambient_volume); it is now ambient_level 0 OFF, 1 LOW, 2 MEDIUM, 3 HIGH.
    // An old file (no "amb_levels" key) maps 0 to OFF, 1..3 to LOW, 4..7 to MEDIUM (the old default 6 is MEDIUM), 8..10 to HIGH.
    if (SettingValue("amb_levels", 0) == 0 && fileExisted && SettingValue("ambient_volume", -1) >= 0)
    {
        const int32_t o = SettingValue("ambient_volume", 6);

        SettingForce("ambient_level", o <= 0 ? 0 : (o <= 3 ? 1 : (o <= 7 ? 2 : 3)));
    }
    SettingForce("amb_levels", 1);
    SettingForce("fx_levels", 1);
}
#endif

void VitaExtras_Init(void)
{
    if (sInitialized)
        return;
    sInitialized = true;
    sSettingCount = 0;

    for (uint32_t i = 0; i < gVitaExtraCount; i++)
    {
        const int32_t idx = FindSettingIndex(gVitaExtras[i].key, true);
        if (idx >= 0)
            sSettings[idx].value = gVitaExtras[i].fallback;
    }

    char path[256];
    VitaPlatform_BuildPath(path, sizeof(path), SETTINGS_FILENAME);
    FILE *const f = fopen(path, "r");
    if (f == NULL)
    {
#ifdef PLATFORM_DECK
        MigrateLevelSettings(false);
#endif
        VitaExtras_Save();
        SyncFastExtras();
        for (uint32_t i = 0; i < sSettingCount; i++)
            SyncPlatformSetting(sSettings[i].key, sSettings[i].value);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char key[32];
        int32_t val = 0;
#ifdef PLATFORM_DECK
        // resolution=auto or resolution=WxH: text, not a number
        if (strncmp(line, "resolution=", 11) == 0)
        {
            char text[24] = "";
            sscanf(line + 11, "%23s", text);
            DeckVideo_SetResolutionText(text);
            continue;
        }
#endif
        if (sscanf(line, "%31[^=]=%d", key, &val) == 2)
        {
            const int32_t idx = FindSettingIndex(key, true);
            if (idx >= 0)
                sSettings[idx].value = val;
        }
    }
    fclose(f);
#ifdef PLATFORM_DECK
    MigrateLevelSettings(true);
#endif

    SyncFastExtras();
    for (uint32_t i = 0; i < sSettingCount; i++)
        SyncPlatformSetting(sSettings[i].key, sSettings[i].value);
}

void VitaExtras_Save(void)
{
    char path[256];
    VitaPlatform_BuildPath(path, sizeof(path), SETTINGS_FILENAME);
    FILE *const f = fopen(path, "w");
    if (f == NULL)
        return;

    for (uint32_t i = 0; i < sSettingCount; i++)
    {
#ifdef PLATFORM_DECK
        if (strcmp(sSettings[i].key, "resolution") == 0)
        {
            fprintf(f, "resolution=%s\n", DeckVideo_GetResolutionText());
            continue;
        }
#endif
        fprintf(f, "%s=%d\n", sSettings[i].key, sSettings[i].value);
    }
    fclose(f);
}

int32_t VitaExtras_Get(const char *key)
{
    if (!sInitialized)
        VitaExtras_Init();

#ifdef PLATFORM_DECK
    if (strcmp(key, "resolution") == 0)
        return DeckVideo_GetResolutionIndex();
#endif
    const int32_t fastId = LookupFastId(key);
    if (fastId >= 0)
        return sFastSettings[fastId];

    const int32_t idx = FindSettingIndex(key, false);
    if (idx >= 0)
        return sSettings[idx].value;

    for (uint32_t i = 0; i < gVitaExtraCount; i++)
    {
        if (strcmp(gVitaExtras[i].key, key) == 0)
            return gVitaExtras[i].fallback;
    }
    return 0;
}

void VitaExtras_Set(const char *key, int32_t value)
{
    if (!sInitialized)
        VitaExtras_Init();

#ifdef PLATFORM_DECK
    if (strcmp(key, "resolution") == 0)
    {
        DeckVideo_SetResolutionIndex(value);   // the choice lives in deck_video.c; settings.txt gets its text
        VitaExtras_Save();
        return;
    }
#endif
    const int32_t idx = FindSettingIndex(key, true);
    if (idx < 0 || sSettings[idx].value == value)
        return;

    const bool isClockOrDayNight = (strcmp(key, "clock_speed") == 0 || strcmp(key, "day_night") == 0);
    if (isClockOrDayNight)
        VitaDayNight_Update();
    sSettings[idx].value = value;
    const int32_t fastId = LookupFastId(key);
    if (fastId >= 0)
        sFastSettings[fastId] = value;
    if (isClockOrDayNight)
        VitaDayNight_Update();
    VitaExtras_Save();
    SyncPlatformSetting(key, value);
}

void VitaExtras_Step(const VitaExtra *extra, int32_t direction)
{
    if (extra == NULL)
        return;

    if (extra->step != NULL)
    {
        extra->step(direction);
        return;
    }

    if (extra->count == 0)
    {
        if (extra->act != NULL)
            extra->act();
        return;
    }

    const int32_t current = VitaExtras_Get(extra->key);
    int32_t next = (current + direction) % extra->count;
    if (next < 0)
        next += extra->count;

    VitaExtras_Set(extra->key, next);
    if (extra->act != NULL)
        extra->act();
}

uint32_t VitaExtras_GetTabExtraCount(uint8_t tab)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < gVitaExtraCount; i++)
    {
        if (gVitaExtras[i].tab == tab)
            count++;
    }
    return count;
}

const VitaExtra *VitaExtras_GetTabExtra(uint8_t tab, uint32_t index)
{
    uint32_t current = 0;
    for (uint32_t i = 0; i < gVitaExtraCount; i++)
    {
        if (gVitaExtras[i].tab == tab)
        {
            if (current == index)
                return &gVitaExtras[i];
            current++;
        }
    }
    return NULL;
}
