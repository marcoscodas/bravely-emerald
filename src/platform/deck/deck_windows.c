#include "deck_windows.h"
#include "deck_levels.h"
#include "platform/vita_daynight.h"
#include "platform/vita_extras.h"
#include "../voxel/voxel_building.h"
#include <stdlib.h>
#include <string.h>

#define WIN_SLOTS 6
#define WIN_BUILDS_PER_FRAME 2

// Level table (LOW, MEDIUM, HIGH): emissive gain.
static const float kGain[3] = { 0.80f, 1.00f, 1.20f };

// Daylight where the windows start (0.45) and are fully lit (0.20).
#define WIN_DAY_START 0.45f
#define WIN_DAY_FULL  0.20f

static const char kVs[] =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec2 vLocal;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vLocal = gl_Vertex.xz;\n"           // map-local tiles (chunks are written in their map's own frame): stable hash
    "    gl_Position = ftransform();\n"      // same transform as the colour pass: same depth
    "}\n";

// Window texels only (mask > 0). Per house (cells of 5 tiles): 15% dark, a slow flicker of a few percent with its own
// phase. Per window (tile): a small brightness spread. Output is premultiplied: blend ONE, ONE_MINUS_SRC_ALPHA, so the
// teal glass is replaced by the warm light rather than added to.
static const char kFs[] =
    "#version 120\n"
    "uniform sampler2D uMask;\n"
    "uniform float uAmt;\n"
    "uniform float uTime;\n"
    "varying vec2 vUv;\n"
    "varying vec2 vLocal;\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "void main() {\n"
    "    float m = texture2D(uMask, vUv).r;\n"
    "    if (m < 0.02) discard;\n"
    "    float hb = hash(floor(vLocal / 5.0));\n"
    "    if (hb < 0.15) discard;\n"
    "    float hw = 0.84 + 0.16 * hash(floor(vLocal * 0.8) + 7.0);\n"
    "    float fl = 1.0 - 0.045 * sin(uTime * (1.1 + hb * 1.3) + hb * 40.0) - 0.02 * sin(uTime * 4.3 + hb * 91.0);\n"
    "    float k = clamp(m * 1.25, 0.0, 1.0) * hw * fl * uAmt;\n"
    "    vec3 c = vec3(1.0, 0.78, 0.42) * k * 1.15;\n"
    "    c += vec3(0.26, 0.22, 0.12) * k * k;\n"     // hotter core: luminance clears the bloom threshold
    "    gl_FragColor = vec4(c, k * 0.8);\n"
    "}\n";

static DeckGlProgram sProg;
static GLint sUMask, sUAmt;
static int sState; // 0 not built, 1 ready, -1 failed (not tried again)

typedef struct
{
    int page;
    GLuint tex;
    unsigned used;
} MaskSlot;

static MaskSlot sSlots[WIN_SLOTS];
static unsigned sTick;
static int sBuiltThisFrame;

int DeckWindows_Level(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 ? DeckLevel(FAST_EXTRA_VOXEL_WINDOWS) : -1;
}

float DeckWindows_Amount(void)
{
    const VitaDayNightLight *dn;
    const int lvl = DeckWindows_Level();
    float x;

    if (sState < 0 || lvl < 0)
        return 0.0f;
    dn = VitaDayNight_Get();
    if (dn == NULL)
        return 0.0f;
    x = (WIN_DAY_START - dn->daylight) / (WIN_DAY_START - WIN_DAY_FULL);
    if (x <= 0.0f)
        return 0.0f;
    if (x > 1.0f)
        x = 1.0f;
    return x * x * (3.0f - 2.0f * x) * kGain[lvl];
}

bool DeckWindows_Begin(float amount)
{
    if (sState < 0)
        return false;
    if (sState == 0)
    {
        if (!DeckGl_Init() || !DeckGl_BuildProgram(&sProg, "windows", kVs, kFs))
        {
            sState = -1;
            DeckGl_Log("[WINDOWS] program failed: windows stay as drawn\n");
            return false;
        }
        sUMask = DeckGl_Uniform(&sProg, "uMask");
        sUAmt = DeckGl_Uniform(&sProg, "uAmt");
        for (int i = 0; i < WIN_SLOTS; ++i)
            sSlots[i].page = -1;
        sState = 1;
        DeckGl_Log("[WINDOWS] program ready\n");
    }
    sBuiltThisFrame = 0;
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(&sProg);
    DeckGl_Set1i(sUMask, 0);
    DeckGl_Set1f(sUAmt, amount);
    return true;
}

// Morton (8x8 tiles, as the colour pages) to rows bottom up, like Voxel_UnswizzleMortonToLinearEx, one byte a texel.
static void UnswizzleBytes(const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h, uint32_t texH)
{
    const uint32_t tilesPerRow = w >> 3;

    memset(dst, 0, (size_t)w * texH);
    for (uint32_t y = 0; y < h; ++y)
    {
        uint8_t *const row = &dst[(size_t)(texH - 1 - y) * w];

        for (uint32_t x = 0; x < w; ++x)
        {
            const uint32_t xi = x & 7, yi = y & 7;
            const uint32_t m = (xi & 1) | ((yi & 1) << 1) | ((xi & 2) << 1) | ((yi & 2) << 2) | ((xi & 4) << 2) | ((yi & 4) << 3);

            row[x] = src[(((y >> 3) * tilesPerRow + (x >> 3)) << 6) + m];
        }
    }
}

static GLuint BuildMask(int page)
{
    uint32_t w = 0, h = 0, th = 0;
    uint8_t *morton, *lin;
    GLuint tex = 0;

    if (!VoxelBuildings_PageSize((uint32_t)page, &w, &h, &th) || w < 8 || (w & 7) != 0)
        return 0;
    morton = (uint8_t *)calloc((size_t)w * th, 1);
    lin = (uint8_t *)malloc((size_t)w * th);
    if (morton != NULL && lin != NULL && VoxelBuildings_ReadGlow((uint32_t)page, 0, w * h, morton))
    {
        UnswizzleBytes(morton, lin, w, h, th);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)w, (GLsizei)th, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lin);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    free(morton);
    free(lin);
    return tex;
}

GLuint DeckWindows_PageMask(int page)
{
    int victim = 0;

    if (page < 0 || !VoxelBuildings_PageHasGlow((uint32_t)page))
        return 0;
    ++sTick;
    for (int i = 0; i < WIN_SLOTS; ++i)
    {
        if (sSlots[i].page == page)
        {
            sSlots[i].used = sTick;
            return sSlots[i].tex;
        }
    }
    if (sBuiltThisFrame >= WIN_BUILDS_PER_FRAME)
        return 0; // a page that is not ready draws without lit windows until a later frame
    for (int i = 1; i < WIN_SLOTS; ++i)
        if (sSlots[i].page < 0 || (sSlots[victim].page >= 0 && sSlots[i].used < sSlots[victim].used))
            victim = i;
    if (sSlots[victim].tex != 0)
        glDeleteTextures(1, &sSlots[victim].tex);
    sSlots[victim].tex = BuildMask(page);
    sSlots[victim].page = sSlots[victim].tex != 0 ? page : -1;
    sSlots[victim].used = sTick;
    ++sBuiltThisFrame;
    return sSlots[victim].tex;
}

void DeckWindows_End(void)
{
    DeckGl_UseProgram(NULL);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
