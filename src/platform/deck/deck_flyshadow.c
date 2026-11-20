#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deck_flyshadow.h"
#include "deck_gl_shader.h"
#include "deck_levels.h"
#include "platform/vita_extras.h"
#include "global.h"
#include "pokemon.h"
#include "wild_encounter.h"
#include "constants/map_groups.h"
#include "constants/map_types.h"
#include "constants/species.h"

#define LAYERS 3
#define MAX_SHADOWS 2
#define CELL 64                 // one sprite frame cell of the shadow texture, pixels
#define TEX_W (2 * CELL)        // two frames side by side: the wing beat
#define TEX_H CELL
#define FMN_HEADER 8
#define FMN_PALETTES 64
#define FMN_FRAMES 8
#define SPAN_STRETCH 1.2f       // the shadow is longer along the flight line than the sprite is tall

enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

typedef struct
{
    uint16_t species;
    float span;     // tiles, the long side of the silhouette
    float hz;       // wing beats a second (the frame changes twice a beat)
} FlyerInfo;

// Real species with a size class; any other Flying-type species with a sprite gets kDefault.
static const FlyerInfo kFlyers[] = {
    { SPECIES_ZUBAT, 1.2f, 3.0f },      { SPECIES_GOLBAT, 2.0f, 2.4f },     { SPECIES_CROBAT, 2.4f, 2.4f },
    { SPECIES_WINGULL, 1.6f, 2.4f },    { SPECIES_PELIPPER, 2.4f, 1.4f },   { SPECIES_TAILLOW, 1.6f, 3.0f },
    { SPECIES_SWELLOW, 2.4f, 1.8f },    { SPECIES_ALTARIA, 2.4f, 1.4f },    { SPECIES_SKARMORY, 3.2f, 1.2f },
    { SPECIES_BUTTERFREE, 1.6f, 3.0f }, { SPECIES_BEAUTIFLY, 1.8f, 2.4f },  { SPECIES_DUSTOX, 1.6f, 2.6f },
    { SPECIES_MASQUERAIN, 1.6f, 2.4f }, { SPECIES_VIBRAVA, 2.0f, 3.2f },    { SPECIES_FLYGON, 2.8f, 2.0f },
    { SPECIES_LATIOS, 2.4f, 1.2f },     { SPECIES_LATIAS, 2.4f, 1.2f },     { SPECIES_RAYQUAZA, 5.0f, 0.8f },
    { SPECIES_LUNATONE, 1.6f, 1.0f },   { SPECIES_SOLROCK, 1.6f, 1.0f },    { SPECIES_SALAMENCE, 3.2f, 1.2f },
    { SPECIES_DRAGONITE, 3.2f, 1.2f },  { SPECIES_CHARIZARD, 3.2f, 1.2f },
};
static const FlyerInfo kDefault = { 0, 1.8f, 2.2f };
// fly or hover without being of the Flying type
static const uint16_t kExtra[] = {
    SPECIES_VIBRAVA, SPECIES_FLYGON, SPECIES_LATIOS, SPECIES_LATIAS, SPECIES_LUNATONE, SPECIES_SOLROCK, SPECIES_DUSTOX,
};
static const uint16_t kRare[] = {
    SPECIES_RAYQUAZA, SPECIES_LATIOS, SPECIES_LATIAS, SPECIES_SALAMENCE, SPECIES_DRAGONITE, SPECIES_CHARIZARD,
};
// the land table's slot odds, percent (src/wild_encounter.c)
static const uint8_t kSlotPct[LAND_WILD_COUNT] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };

typedef struct
{
    bool live;
    uint16_t species;
    int dir;               // DIR_*: which follower frames, picked from the heading
    float x, z;            // world tiles
    float vx, vz;          // tiles a second
    float age, dur;
    float phase;           // wing phase offset, radians
    float hz;
    float u0, v0, u1, v1;  // silhouette rectangle in frame 0 of the texture; frame 1 is CELL pixels to the right
    float w, h;            // tiles, the silhouette's size before the stretch
} FlyShadow;

static FlyShadow sShadow[MAX_SHADOWS];
static GLuint sTex[MAX_SHADOWS];
static float sTime, sTimer = -1.0f;
static uint32_t sRng;
static bool sInit, sDebug;
static int sForced = -1;
static float sLastX, sLastZ;
static bool sHaveLast;
static bool sRain;
static float sSun;
static uint8_t sHasSprite[NUM_SPECIES];   // 0 unknown, 1 has a follower sprite, 2 has none

typedef struct
{
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} FlyVertex;

static FlyVertex sVerts[LAYERS * 6];

static float Rand01(void)
{
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return (float)(sRng & 0xFFFFFFu) / 16777216.0f;
}

static float RandRange(float lo, float hi)
{
    return lo + (hi - lo) * Rand01();
}

static void Init(void)
{
    const char *e = getenv("DECK_FLYSHADOW_NOW");

    sInit = true;
    sRng = 0x2545F491u ^ (uint32_t)(uintptr_t)&sRng;
    if (sRng == 0)
        sRng = 1;
    sDebug = e != NULL && e[0] != '\0' && e[0] != '0';
    e = getenv("DECK_FLYSHADOW_SPECIES");
    if (e != NULL && e[0] != '\0')
        sForced = (int)strtol(e, NULL, 0);
    sTimer = sDebug ? 0.0f : RandRange(10.0f, 30.0f);
}

// Opacity of the shadow core: LOW 0.14, MEDIUM 0.18, HIGH 0.22; 0 = off
static float Level(void)
{
    if (VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0)
        return 0.0f;
    return DeckLevelPick(FAST_EXTRA_VOXEL_FLYSHADOW, 0.14f, 0.18f, 0.22f);
}

// ---- the sprite: followers/mon/NNN.bin (FMN1, as src/follower_mon.c reads it) ----

// Reads the whole file the way follower_mon.c's loader does (the fopen wrap maps the Vita prefixes on Deck). NULL if absent.
static uint8_t *ReadFile(const char *key, long *outSize)
{
    static const char *const kFmt[] = {
        "app0:followers/mon/%s.bin", "ux0:data/POKEVITA/followers/mon/%s.bin", "app0:assets/followers/mon/%s.bin",
    };
    char path[160];

    for (int i = 0; i < 3; ++i)
    {
        FILE *f;
        long size;
        uint8_t *buf;

        snprintf(path, sizeof(path), kFmt[i], key);
        f = fopen(path, "rb");
        if (f == NULL)
            continue;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = size > 0 ? (uint8_t *)malloc((size_t)size) : NULL;
        if (buf != NULL && fread(buf, 1, (size_t)size, f) != (size_t)size)
        {
            free(buf);
            buf = NULL;
        }
        fclose(f);
        if (buf != NULL)
        {
            *outSize = size;
            return buf;
        }
    }
    return NULL;
}

static void KeyOf(char *key, uint16_t species)
{
    snprintf(key, 8, "%03u", (unsigned)SpeciesToNationalPokedexNum(species));
}

static bool HasSprite(uint16_t species)
{
    char key[8];
    long size;
    uint8_t *f;

    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return false;
    if (sHasSprite[species] == 0)
    {
        KeyOf(key, species);
        f = ReadFile(key, &size);
        sHasSprite[species] = (f != NULL && size >= FMN_HEADER + FMN_PALETTES && memcmp(f, "FMN1", 4) == 0) ? 1 : 2;
        free(f);
    }
    return sHasSprite[species] == 1;
}

// Pixel colour index of frame `frame` (4bpp tiles, row-major, low nibble the left pixel, as the GBA's 1D OBJ mapping).
static int PixelIndex(const uint8_t *frameData, int w, int x, int y)
{
    const uint8_t b = frameData[((y >> 3) * (w >> 3) + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];

    return (x & 1) ? (b >> 4) : (b & 15);
}

// Decodes the two walking frames of direction `dir` into texture slot `slot` (white RGBA, the alpha is the sprite's own mask:
// palette index 0 is clear) and fills the silhouette rectangle and size into `s`. False if the file is no good.
static bool LoadShadowTexture(int slot, FlyShadow *s, float span)
{
    static uint8_t rgba[TEX_W * TEX_H * 4];
    static const int kFirstFrame[4] = { 0, 2, 4, 6 };   // up, down, left, right: two frames each
    char key[8];
    long size;
    uint8_t *file;
    int w, h, bx0 = CELL, by0 = CELL, bx1 = -1, by1 = -1;
    size_t frameBytes;

    KeyOf(key, s->species);
    file = ReadFile(key, &size);
    if (file == NULL)
        return false;
    w = file[4];
    h = file[5];
    frameBytes = (size_t)w * h / 2;
    if (memcmp(file, "FMN1", 4) != 0 || file[6] != FMN_FRAMES || !((w == 32 && h == 32) || (w == 64 && h == 64))
     || (size_t)size < FMN_HEADER + FMN_PALETTES + FMN_FRAMES * frameBytes)
    {
        free(file);
        return false;
    }
    memset(rgba, 0, sizeof(rgba));
    for (int k = 0; k < 2; ++k)
    {
        const uint8_t *data = file + FMN_HEADER + FMN_PALETTES + (size_t)(kFirstFrame[s->dir] + k) * frameBytes;

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint8_t *px;

                if (PixelIndex(data, w, x, y) == 0)
                    continue;
                px = &rgba[(y * TEX_W + k * CELL + x) * 4];
                px[0] = px[1] = px[2] = px[3] = 255;
                if (x < bx0) bx0 = x;
                if (y < by0) by0 = y;
                if (x > bx1) bx1 = x;
                if (y > by1) by1 = y;
            }
    }
    free(file);
    if (bx1 < 0)
        return false;
    // one clear pixel around the silhouette, inside the frame
    bx0 = bx0 > 0 ? bx0 - 1 : 0;
    by0 = by0 > 0 ? by0 - 1 : 0;
    bx1 = bx1 < w - 1 ? bx1 + 1 : w - 1;
    by1 = by1 < h - 1 ? by1 + 1 : h - 1;
    {
        const float pxScale = span / (float)(((bx1 - bx0 + 1) > (by1 - by0 + 1)) ? (bx1 - bx0 + 1) : (by1 - by0 + 1));

        s->u0 = ((float)bx0 + 0.5f) / (float)TEX_W;
        s->u1 = ((float)bx1 + 0.5f) / (float)TEX_W;
        s->v0 = ((float)by0 + 0.5f) / (float)TEX_H;
        s->v1 = ((float)by1 + 0.5f) / (float)TEX_H;
        s->w = (float)(bx1 - bx0 + 1) * pxScale;
        s->h = (float)(by1 - by0 + 1) * pxScale;
    }
    // one upload per shadow, when it spawns; the texture is never rewritten while it flies, so there is no stall
    {
        GLint prev = 0;

        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
        if (sTex[slot] == 0)
            glGenTextures(1, &sTex[slot]);
        glBindTexture(GL_TEXTURE_2D, sTex[slot]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    }
    return true;
}

// ---- which species ----

static const FlyerInfo *InfoOf(uint16_t species)
{
    for (size_t i = 0; i < sizeof(kFlyers) / sizeof(kFlyers[0]); ++i)
        if (kFlyers[i].species == species)
            return &kFlyers[i];
    return &kDefault;
}

static bool IsFlyer(uint16_t species)
{
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return false;
    if (gSpeciesInfo[species].types[0] == TYPE_FLYING || gSpeciesInfo[species].types[1] == TYPE_FLYING)
        return true;
    for (size_t i = 0; i < sizeof(kExtra) / sizeof(kExtra[0]); ++i)
        if (kExtra[i] == species)
            return true;
    return false;
}

static const struct WildPokemonInfo *LandTable(void)
{
    if (gWildMonHeaders == NULL)
        return NULL;
    for (int i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); ++i)
        if (gWildMonHeaders[i].mapGroup == gSaveBlock1Ptr->location.mapGroup
         && gWildMonHeaders[i].mapNum == gSaveBlock1Ptr->location.mapNum)
            return gWildMonHeaders[i].landMonsInfo;
    return NULL;
}

typedef struct
{
    uint16_t species;
    int weight;
} Pick;

static uint16_t PickWeighted(const Pick *list, int n)
{
    int total = 0, r;

    for (int i = 0; i < n; ++i)
        if (list[i].weight > 0 && HasSprite(list[i].species))
            total += list[i].weight;
    if (total <= 0)
        return SPECIES_NONE;
    r = (int)(Rand01() * (float)total);
    for (int i = 0; i < n; ++i)
    {
        if (list[i].weight <= 0 || !HasSprite(list[i].species))
            continue;
        if (r < list[i].weight)
            return list[i].species;
        r -= list[i].weight;
    }
    return SPECIES_NONE;
}

// Flyers of the current map's land table, weighted by the table's own slot odds. SPECIES_NONE if there are none.
static uint16_t PickFromTable(void)
{
    const struct WildPokemonInfo *t = LandTable();
    Pick list[LAND_WILD_COUNT];
    int n = 0, total = 0;

    if (t == NULL || t->wildPokemon == NULL)
        return SPECIES_NONE;
    for (int i = 0; i < LAND_WILD_COUNT; ++i)
    {
        const uint16_t sp = t->wildPokemon[i].species;

        if (!IsFlyer(sp))
            continue;
        list[n].species = sp;
        list[n].weight = kSlotPct[i];
        if (HasSprite(sp))
            total += kSlotPct[i];
        ++n;
    }
    // a table whose only flyer is a rare slot lets the regional birds in most of the time
    if (total <= 0 || Rand01() >= (total >= 20 ? 1.0f : (total < 7 ? 0.35f : (float)total / 20.0f)))
        return SPECIES_NONE;
    return PickWeighted(list, n);
}

static uint16_t PickFallback(bool night)
{
    static const Pick kOcean[] = { { SPECIES_WINGULL, 55 }, { SPECIES_PELIPPER, 35 }, { SPECIES_TAILLOW, 10 } };
    static const Pick kTown[] = { { SPECIES_TAILLOW, 50 }, { SPECIES_WINGULL, 30 }, { SPECIES_SWELLOW, 20 } };
    static const Pick kDay[] = { { SPECIES_TAILLOW, 40 }, { SPECIES_WINGULL, 25 }, { SPECIES_SWELLOW, 15 } };
    static const Pick kNight[] = { { SPECIES_ZUBAT, 30 }, { SPECIES_GOLBAT, 15 }, { SPECIES_WINGULL, 15 }, { SPECIES_SWELLOW, 5 } };
    const int type = gMapHeader.mapType;

    if (type == MAP_TYPE_OCEAN_ROUTE)
        return PickWeighted(kOcean, 3);
    if (type == MAP_TYPE_TOWN || type == MAP_TYPE_CITY)
        return night ? PickWeighted(kNight, 4) : PickWeighted(kTown, 3);
    return night ? PickWeighted(kNight, 4) : PickWeighted(kDay, 3);
}

static uint16_t PickSpecies(void)
{
    const bool night = sSun < 0.25f;
    uint16_t sp = SPECIES_NONE;

    if (sForced > 0 && sForced < NUM_SPECIES)
        return HasSprite((uint16_t)sForced) ? (uint16_t)sForced : SPECIES_NONE;
    // 1 in 40 on a route (never in a town or city, nor on the water): a big one
    if (gMapHeader.mapType == MAP_TYPE_ROUTE && Rand01() < 1.0f / 40.0f)
    {
        Pick list[6];

        for (int i = 0; i < 6; ++i)
        {
            list[i].species = kRare[i];
            list[i].weight = 1;
        }
        sp = PickWeighted(list, 6);
    }
    if (sp == SPECIES_NONE && gMapHeader.mapType != MAP_TYPE_OCEAN_ROUTE)
        sp = PickFromTable();
    if (sp == SPECIES_NONE)
        sp = PickFallback(night);
    return sp;
}

static void Spawn(float camX, float camZ)
{
    for (int i = 0; i < MAX_SHADOWS; ++i)
    {
        FlyShadow *s = &sShadow[i];
        const float ang = Rand01() * 6.2831853f;
        const float fx = cosf(ang), fz = sinf(ang);
        const float lateral = RandRange(-4.0f, 4.0f);
        const float reach = 17.0f;                  // starts and ends outside the view
        const FlyerInfo *info;
        float dur;

        if (s->live)
            continue;
        memset(s, 0, sizeof(*s));
        s->species = PickSpecies();
        DeckGl_Log("[FLYSHADOW] spawn species %d\n", (int)s->species);
        if (s->species == SPECIES_NONE)
            return;
        info = InfoOf(s->species);
        // the follower frames facing the heading: the north is up on the screen, as in the GBA's view
        s->dir = fabsf(fx) > fabsf(fz) ? (fx > 0.0f ? DIR_RIGHT : DIR_LEFT) : (fz < 0.0f ? DIR_UP : DIR_DOWN);
        if (!LoadShadowTexture(i, s, info->span))
        {
            DeckGl_Log("[FLYSHADOW] texture load failed\n");
            memset(s, 0, sizeof(*s));
            return;
        }
        dur = info->span < 1.7f ? RandRange(8.0f, 10.0f) : (info->span < 2.7f ? RandRange(10.0f, 12.0f)
              : (info->span < 4.0f ? RandRange(11.0f, 14.0f) : RandRange(14.0f, 16.0f)));
        s->dur = dur;
        s->hz = info->hz;
        s->vx = fx * 2.0f * reach / dur;
        s->vz = fz * 2.0f * reach / dur;
        s->x = camX - fx * reach - fz * lateral;
        s->z = camZ - fz * reach + fx * lateral;
        s->phase = Rand01() * 6.2831853f;
        s->live = true;
        return;
    }
}

void DeckFlyShadow_Update(float dt, float camX, float camZ, bool outdoor, bool battle, bool rain, float sun)
{
    const float lvl = Level();

    if (!sInit)
        Init();
    if (dt < 0.0f)
        dt = 0.0f;
    if (dt > 0.1f)
        dt = 0.1f;
    sRain = rain;
    sSun = sun;
    sTime += dt;
    // a jump of the camera (warp, map change) retires what flies
    if (sHaveLast && (fabsf(camX - sLastX) > 12.0f || fabsf(camZ - sLastZ) > 12.0f))
        memset(sShadow, 0, sizeof(sShadow));
    sLastX = camX;
    sLastZ = camZ;
    sHaveLast = true;
    if (lvl <= 0.0f || !outdoor || battle)
    {
        memset(sShadow, 0, sizeof(sShadow));
        return;
    }
    for (int i = 0; i < MAX_SHADOWS; ++i)
    {
        FlyShadow *s = &sShadow[i];

        if (!s->live)
            continue;
        s->age += dt;
        s->x += s->vx * dt;
        s->z += s->vz * dt;
        if (s->age >= s->dur)
            s->live = false;
    }
    sTimer -= dt;
    if (sTimer <= 0.0f)
    {
        Spawn(camX, camZ);
        sTimer = sDebug ? 6.0f : RandRange(25.0f, 70.0f) * (rain ? 2.5f : 1.0f);
    }
}

// One layer of one shadow: the sprite's silhouette rectangle laid flat on the ground, grown by `grow` (fraction of its size)
// about its centre. Two triangles. The sprite's forward axis lies along the flight line (mirrored for left/right frames,
// which already face their way), its up/across axis across it. Returns vertices written.
static int QuadLayer(FlyVertex *out, const FlyShadow *s, int frame, float grow, uint8_t alpha, float groundY)
{
    const float len = sqrtf(s->vx * s->vx + s->vz * s->vz);
    const float fx = len > 0.0001f ? s->vx / len : 0.0f, fz = len > 0.0001f ? s->vz / len : 1.0f;
    float px = -fz, pz = fx;                       // across the flight line
    float ux, uz, vx, vz;                          // ground vectors for the full width (u) and the full height (v)
    const float k = 1.0f + grow;
    const float du = (float)frame * (float)CELL / (float)TEX_W;
    static const int kCornU[6] = { 0, 1, 1, 0, 1, 0 }, kCornV[6] = { 0, 0, 1, 0, 1, 1 };

    if (s->dir == DIR_LEFT || s->dir == DIR_RIGHT)
    {
        // side view: the length runs along the heading, the top of the sprite is the north side
        const float sgn = s->dir == DIR_RIGHT ? 1.0f : -1.0f;

        if (pz < 0.0f) { px = -px; pz = -pz; }
        ux = fx * sgn * s->w * SPAN_STRETCH;
        uz = fz * sgn * s->w * SPAN_STRETCH;
        vx = px * s->h;
        vz = pz * s->h;
    }
    else
    {
        // front/back view: the top of the sprite leads (up) or trails (down), the sprite's right is the east side
        const float sgn = s->dir == DIR_UP ? -1.0f : 1.0f;

        if (px < 0.0f) { px = -px; pz = -pz; }
        ux = px * s->w;
        uz = pz * s->w;
        vx = fx * sgn * s->h * SPAN_STRETCH;
        vz = fz * sgn * s->h * SPAN_STRETCH;
    }
    for (int i = 0; i < 6; ++i)
    {
        const float cu = (float)kCornU[i], cv = (float)kCornV[i];

        out[i].x = s->x + (ux * (cu - 0.5f) + vx * (cv - 0.5f)) * k;
        out[i].y = groundY;
        out[i].z = s->z + (uz * (cu - 0.5f) + vz * (cv - 0.5f)) * k;
        out[i].u = (cu > 0.5f ? s->u1 : s->u0) + du;
        out[i].v = cv > 0.5f ? s->v1 : s->v0;
        out[i].r = 0x08;
        out[i].g = 0x14;
        out[i].b = 0x28;   // 0xFF281408, the silhouette shadow colour
        out[i].a = alpha;
    }
    return 6;
}

void DeckFlyShadow_Emit(float groundY)
{
    static const float kGrow[LAYERS] = { 0.12f, 0.06f, 0.0f };   // outer copy first
    const float lvl = Level();
    uint8_t a;
    bool any = false;
    GLboolean cull, tex, alphaTest;
    GLint prevTex = 0;

    if (!sInit || lvl <= 0.0f)
        return;
    {
        // sunlit by day, the moon's soft shadow at night; rain dims it
        const float sunF = sSun < 0.0f ? 0.0f : (sSun > 0.6f ? 1.0f : sSun / 0.6f);
        const float light = (0.30f + 0.70f * sunF) * (sRain ? 0.5f : 1.0f);

        // three translucent copies, each a little larger: the opacities sum to lvl at the core
        a = (uint8_t)(lvl * light * (1.0f / LAYERS) * 255.0f + 0.5f);
    }
    if (a == 0)
        return;
    for (int i = 0; i < MAX_SHADOWS; ++i)
        any = any || (sShadow[i].live && sTex[i] != 0);
    if (!any)
        return;
    cull = glIsEnabled(GL_CULL_FACE);
    tex = glIsEnabled(GL_TEXTURE_2D);
    alphaTest = glIsEnabled(GL_ALPHA_TEST);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(FlyVertex), &sVerts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(FlyVertex), &sVerts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(FlyVertex), &sVerts[0].r);
    for (int i = 0; i < MAX_SHADOWS; ++i)
    {
        const FlyShadow *s = &sShadow[i];
        int n = 0, frame;

        if (!s->live || sTex[i] == 0)
            continue;
        // the real animation: the two frames of the heading's direction alternate twice a beat
        frame = (int)floorf(sTime * s->hz * 2.0f + s->phase * 0.3183099f) & 1;
        for (int l = 0; l < LAYERS; ++l)
            n += QuadLayer(&sVerts[n], s, frame, kGrow[l], a, groundY + 0.03f);
        glBindTexture(GL_TEXTURE_2D, sTex[i]);
        glDrawArrays(GL_TRIANGLES, 0, n);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (!tex)
        glDisable(GL_TEXTURE_2D);
    if (alphaTest)
        glEnable(GL_ALPHA_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
}
