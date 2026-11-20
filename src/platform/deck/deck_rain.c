#include "deck_rain.h"
#include "deck_levels.h"
#include "deck_bump.h"
#include "../voxel/voxel_atlas.h"
#include "deck_gl_shader.h"
#include "platform/vita_extras.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Rain box = the one DrawWeather uses (voxel_tuning.h VOXEL_WEATHER_*): 24 x 8.5 x 28 around the camera target,
// 20 units north of the target at the far side, starting 0.4 under the ground.
#define BOX_X 24.0f
#define BOX_Y 8.5f
#define BOX_Z 28.0f

#define STREAK_COUNT 2048
#define SPLASH_COUNT 1024

static bool sReady = false;
static char sOff[96];

// DECK_RAIN_OFF is a comma, space or semicolon separated list; a name matches only as a whole token.
static bool Off(const char *name)
{
    const size_t len = strlen(name);
    const char *p = sOff;

    while (*p != '\0')
    {
        size_t n = strcspn(p, ", ;");

        if (n == len && strncmp(p, name, len) == 0)
            return true;
        p += n;
        if (*p != '\0')
            ++p;
    }
    return false;
}

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ---- Vertex buffer helpers: 5 floats a vertex, x y z in the vertex position, corner x y in texcoord 0.
static const float kCorner[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

static GLuint MakeQuadVbo(int count, uint32_t seed)
{
    float *v = (float *)malloc((size_t)count * 6 * 5 * sizeof(float));
    GLuint vbo = 0;

    if (v == NULL)
        return 0;
    for (int i = 0; i < count; ++i)
    {
        float s[3];

        for (int c = 0; c < 3; ++c)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            s[c] = (float)(seed & 0xFFFFu) / 65536.0f;
        }
        for (int k = 0; k < 6; ++k)
        {
            float *o = v + ((size_t)i * 6 + (size_t)k) * 5;

            o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
            o[3] = kCorner[k][0]; o[4] = kCorner[k][1];
        }
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * 6 * 5 * sizeof(float)), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    free(v);
    return vbo;
}

// Binds the quad VBO as vertex (3 floats) + texcoord 0 (2 floats), draws, unbinds.
static void DrawQuadVbo(GLuint vbo, int quads)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 5 * sizeof(float), (const void *)0);
    glTexCoordPointer(2, GL_FLOAT, 5 * sizeof(float), (const void *)(3 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, quads * 6);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---- C2 streaks ----------------------------------------------------------------------------------------
static const char *kStreakVs =
    "#version 120\n"
    "uniform float uTime;\n"
    "uniform vec3 uTarget;\n"            // camera target x, ground, z
    "uniform vec3 uCam;\n"
    "uniform vec3 uVel;\n"               // units / second
    "uniform vec3 uParams;\n"            // length, min width, pixel width per unit distance
    "varying vec2 vUv;\n"
    "varying float vFade;\n"
    "void main() {\n"
    "    vec3 seed = gl_Vertex.xyz;\n"
    "    vec3 span = vec3(24.0, 8.5, 28.0);\n"
    "    vec3 p = fract(seed + fract((uTime * uVel - uTarget) / span));\n"
    "    vec3 w = vec3(uTarget.x - 12.0, uTarget.y - 0.4, uTarget.z - 20.0) + p * span;\n"
    "    vec3 d = 0.5 - abs(p - 0.5);\n"
    "    float fade = clamp(min(min(d.x * span.x / 3.0, d.z * span.z / 3.0), d.y * span.y / 1.2), 0.0, 1.0);\n"
    "    vec3 toCam = w - uCam;\n"
    "    float dist = length(toCam);\n"
    "    float lens = clamp((dist - 2.0) / 2.5, 0.0, 1.0);\n"
    "    vec3 dir = normalize(uVel);\n"
    "    float len = uParams.x * (0.8 + 0.45 * fract(seed.x * 7.0 + seed.z * 3.0));\n"
    "    vec3 side = normalize(cross(dir, toCam));\n"
    "    float width = max(uParams.y, dist * uParams.z);\n"
    "    vec3 pos = w + dir * (gl_MultiTexCoord0.y * 0.5 * len) + side * (gl_MultiTexCoord0.x * width);\n"
    "    vFade = fade * lens * min(1.0, 1.6 * uParams.y / width);\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";

static const char *kStreakFs =
    "#version 120\n"
    "uniform vec4 uColor;\n"
    "varying vec2 vUv;\n"
    "varying float vFade;\n"
    "void main() {\n"
    "    float across = 1.0 - vUv.x * vUv.x;\n"
    "    float along = sin((vUv.y * 0.5 + 0.5) * 3.14159265);\n"
    "    float a = uColor.a * vFade * across * (0.3 + 0.7 * along);\n"
    "    if (a < 0.004) discard;\n"
    "    gl_FragColor = vec4(uColor.rgb, a);\n"
    "}\n";

static DeckGlProgram sStreak;
static GLint sStTarget = -1, sStCam = -1, sStVel = -1, sStParams = -1, sStColor = -1;
static GLuint sStreakVbo = 0;

static void StreakInit(void)
{
    if (!DeckGl_BuildProgram(&sStreak, "rain-streaks", kStreakVs, kStreakFs))
    {
        DeckGl_Log("[RAIN] streak shader build failed: legacy rain particles stay\n");
        return;
    }
    sStTarget = DeckGl_Uniform(&sStreak, "uTarget");
    sStCam = DeckGl_Uniform(&sStreak, "uCam");
    sStVel = DeckGl_Uniform(&sStreak, "uVel");
    sStParams = DeckGl_Uniform(&sStreak, "uParams");
    sStColor = DeckGl_Uniform(&sStreak, "uColor");
    sStreakVbo = MakeQuadVbo(STREAK_COUNT, 0x5EED0001u);
    if (sStreakVbo == 0)
        DeckGl_DeleteProgram(&sStreak);
}

bool DeckRain_DrawStreaks(const DeckRainFrame *f)
{
    if (!DeckRain_Enabled() || sStreak.program == 0 || sStreakVbo == 0 || Off("streaks") || f->amount <= 0.005f
        || DeckLevel(FAST_EXTRA_VOXEL_RAIN) < 0)
        return false;

    const float amount = Clamp01(f->amount);
    int count = (int)(amount * (float)STREAK_COUNT + 0.5f);
    float alpha = 0.45f * (0.5f + 0.5f * amount) + 0.16f * Clamp01(f->bolt);

    if (count < STREAK_COUNT / 4)
        count = STREAK_COUNT / 4;
    count = (int)((float)count * DeckLevelPick(FAST_EXTRA_VOXEL_RAIN, 0.5f, 1.0f, 1.0f) * f->density);
    if (alpha > 1.0f)
        alpha = 1.0f;

    DeckGl_UseProgram(&sStreak);
    DeckGl_Set3f(sStTarget, f->target[0], f->target[1], f->target[2]);
    DeckGl_Set3f(sStCam, f->cam[0], f->cam[1], f->cam[2]);
    DeckGl_Set3f(sStVel, -2.2f, -15.0f, 0.6f);
    DeckGl_Set3f(sStParams, 1.05f, 0.035f, 0.0022f);
    // rain colour (0xFFF4E8DE as DrawWeather) x day/night light
    DeckGl_Set4f(sStColor, 0.871f * f->light[0], 0.910f * f->light[1], 0.957f * f->light[2], alpha);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    DrawQuadVbo(sStreakVbo, count);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
    return true;
}

// ---- C3 splashes ---------------------------------------------------------------------------------------
// Rings on the camera ground plane (spec: the cheap option, flat routes). Every quad has its own period; at each new cycle
// it picks a new spot from a hash of its seed and the cycle number, so nothing is stored or uploaded per frame.
static const char *kSplashVs =
    "#version 120\n"
    "uniform float uTime;\n"
    "uniform vec3 uTarget;\n"            // camera target x, ground, z
    "uniform vec3 uParams;\n"            // ring life (s), max radius, height above the ground
    "varying vec2 vUv;\n"
    "varying float vK;\n"
    "varying float vFade;\n"
    "float hash(float n) { return fract(sin(n) * 43758.5453); }\n"
    "void main() {\n"
    "    vec3 seed = gl_Vertex.xyz;\n"
    "    float period = 0.8 + 0.5 * seed.z;\n"
    "    float t = uTime / period + seed.y;\n"
    "    float cyc = floor(t);\n"
    "    float k = fract(t) * period / uParams.x;\n"           // 0..1 while the ring lives, then > 1
    "    vec2 r = vec2(hash(cyc * 1.37 + seed.x * 91.7), hash(cyc * 2.11 + seed.x * 57.3 + seed.z * 13.1));\n"
    "    vec2 pos = vec2(uTarget.x - 10.0 + r.x * 20.0, uTarget.z - 14.0 + r.y * 22.0);\n"
    "    float d = length(pos - uTarget.xz);\n"
    "    float rad = k < 1.0 ? uParams.y * (0.3 + 0.7 * k) : 0.0;\n"
    "    vec3 w = vec3(pos.x + gl_MultiTexCoord0.x * rad, uTarget.y + uParams.z, pos.y + gl_MultiTexCoord0.y * rad);\n"
    "    vK = k;\n"
    "    vFade = 1.0 - smoothstep(8.0, 12.0, d);\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(w, 1.0);\n"
    "}\n";

static const char *kSplashFs =
    "#version 120\n"
    "uniform vec3 uColor;\n"
    "varying vec2 vUv;\n"
    "varying float vK;\n"
    "varying float vFade;\n"
    "void main() {\n"
    "    float r = length(vUv);\n"
    "    float ring = smoothstep(0.52, 0.72, r) * (1.0 - smoothstep(0.72, 0.92, r));\n"
    "    float crown = (1.0 - smoothstep(0.0, 0.2, r)) * clamp(1.0 - vK * 3.0, 0.0, 1.0);\n"
    "    float a = (ring * 0.55 + crown * 0.7) * (1.0 - vK) * vFade;\n"
    "    if (a < 0.01) discard;\n"
    "    gl_FragColor = vec4(uColor, a);\n"
    "}\n";

static DeckGlProgram sSplash;
static GLint sSpTarget = -1, sSpParams = -1, sSpColor = -1;
static GLuint sSplashVbo = 0;

static void SplashInit(void)
{
    if (!DeckGl_BuildProgram(&sSplash, "rain-splash", kSplashVs, kSplashFs))
    {
        DeckGl_Log("[RAIN] splash shader build failed: no splashes\n");
        return;
    }
    sSpTarget = DeckGl_Uniform(&sSplash, "uTarget");
    sSpParams = DeckGl_Uniform(&sSplash, "uParams");
    sSpColor = DeckGl_Uniform(&sSplash, "uColor");
    sSplashVbo = MakeQuadVbo(SPLASH_COUNT, 0x5EED0002u);
    if (sSplashVbo == 0)
        DeckGl_DeleteProgram(&sSplash);
}

bool DeckRain_DrawSplashes(const DeckRainFrame *f)
{
    if (!DeckRain_Enabled() || sSplash.program == 0 || sSplashVbo == 0 || Off("splash") || f->amount <= 0.05f
        || DeckLevel(FAST_EXTRA_VOXEL_SPLASH) < 0)
        return false;

    int count = (int)(Clamp01(f->amount) * (float)SPLASH_COUNT + 0.5f);

    if (count < SPLASH_COUNT / 4)
        count = SPLASH_COUNT / 4;
    count = (int)((float)count * DeckLevelPick(FAST_EXTRA_VOXEL_SPLASH, 0.5f, 1.0f, 1.0f) * f->density);
    DeckGl_UseProgram(&sSplash);
    DeckGl_Set3f(sSpTarget, f->target[0], f->target[1], f->target[2]);
    DeckGl_Set3f(sSpParams, 0.35f, 0.38f, 0.03f);
    DeckGl_Set3f(sSpColor, 0.85f * f->light[0], 0.90f * f->light[1], 0.95f * f->light[2]);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    DrawQuadVbo(sSplashVbo, count);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
    return true;
}

// ---- C4 wet ground -------------------------------------------------------------------------------------
static const char *kWetVs =
    "#version 120\n"
    "uniform vec2 uChunk;\n"             // chunk draw offset in the world frame (puddle pattern stays put)
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    "varying vec2 vWorld;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    vEye = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
    "    vWorld = gl_Vertex.xz + uChunk;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

// No normal attribute on the ground: the normal comes from the screen-space derivatives of the eye position.
static const char *kWetFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform float uWet;\n"
    "uniform vec3 uSky;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    "varying vec2 vWorld;\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
    "float noise(vec2 p) {\n"
    "    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex, vUv);\n"
    "    vec4 c = t * vCol;\n"
    "    vec3 up = normalize(mat3(gl_ModelViewMatrix) * vec3(0.0, 1.0, 0.0));\n"
    "    vec3 cr = cross(dFdx(vEye), dFdy(vEye));\n"
    "    float crLen = length(cr);\n"
    "    vec3 n = crLen > 1.0e-12 ? cr / crLen : up;\n"
    "    float flatness = smoothstep(0.7, 0.95, abs(dot(n, up)));\n"
    "    float puddle = smoothstep(0.52, 0.72, noise(vWorld * 0.45) * 0.65 + noise(vWorld * 1.3) * 0.35);\n"
    "    float wet = uWet * flatness;\n"
    "    float bm = 1.0;\n"
    "#ifdef DECK_BUMP\n"
    "    bm = deckBumpMul(uTex, vUv, t.rgb, vCol.rgb, vEye) * deckGroundMul(vWorld, t.rgb, vCol.rgb, vEye);\n"
    "    bm = 1.0 + (bm - 1.0) * (1.0 - 0.4 * wet);\n"    // a wet surface is smoother: less relief under the sheen
    "#endif\n"
    "    vec3 col = c.rgb * bm * (1.0 - 0.30 * wet - 0.18 * wet * puddle);\n"
    "    float l = dot(col, vec3(0.299, 0.587, 0.114));\n"
    "    col = mix(vec3(l), col, 1.0 + 0.25 * wet);\n"
    "    float ndv = abs(dot(n, normalize(-vEye)));\n"
    "    float fres = clamp(pow(1.0 - ndv, 2.0) * 3.0, 0.0, 1.0);\n"
    "    float refl = wet * (0.08 + 0.45 * puddle) * (0.4 + 0.6 * fres);\n"
    "    col = mix(col, uSky, clamp(refl, 0.0, 0.6));\n"
    "    gl_FragColor = vec4(col, c.a);\n"
    "}\n";

static DeckGlProgram sWetProg;
static DeckBumpUniforms sWetBump;
static GLint sWetTex = -1, sWetWet = -1, sWetSky = -1, sWetChunk = -1;
static float sWet = 0.0f;
static float sLastSeconds = -1.0f;
static float sLastLog = -100.0f;

static void WetInit(void)
{
    if (!DeckBump_Build(&sWetProg, &sWetBump, "rain-wet", kWetVs, kWetFs))
    {
        DeckGl_Log("[RAIN] wet ground shader build failed: flat ground stays\n");
        return;
    }
    sWetTex = DeckGl_Uniform(&sWetProg, "uTex");
    sWetWet = DeckGl_Uniform(&sWetProg, "uWet");
    sWetSky = DeckGl_Uniform(&sWetProg, "uSky");
    sWetChunk = DeckGl_Uniform(&sWetProg, "uChunk");
}

float DeckRain_Wetness(void) { return sWet; }

bool DeckRain_WetBegin(const float light[3])
{
    if (!DeckRain_Enabled() || sWetProg.program == 0 || Off("wet") || DeckLevel(FAST_EXTRA_VOXEL_WET) < 0 || sWet <= 0.01f)
        return false;
    DeckGl_UseProgram(&sWetProg);
    DeckGl_Set1i(sWetTex, 0);
    DeckGl_Set1f(sWetWet, sWet * DeckLevelPick(FAST_EXTRA_VOXEL_WET, 0.5f, 1.0f, 1.0f));
    // pale overcast sky x the light of the hour
    DeckGl_Set3f(sWetSky, 0.62f * light[0], 0.70f * light[1], 0.80f * light[2]);
    DeckBump_Set(&sWetBump, (float)VOXEL_ATLAS_W, (float)VOXEL_ATLAS_H, (float)VOXEL_ATLAS_SLOT);
    DeckBump_ApplyIndoor(&sWetBump);
    return true;
}

// Dry ground with relief: the same program with no wetness (uWet 0 is the plain texture times the vertex colour).
bool DeckRain_BumpBegin(const float light[3])
{
    if (sWetProg.program == 0 || !sWetBump.ok || !(DeckBump_Enabled() || DeckBump_GroundOn()))
        return false;
    DeckGl_UseProgram(&sWetProg);
    DeckGl_Set1i(sWetTex, 0);
    DeckGl_Set1f(sWetWet, 0.0f);
    DeckGl_Set3f(sWetSky, 0.62f * light[0], 0.70f * light[1], 0.80f * light[2]);
    DeckBump_Set(&sWetBump, (float)VOXEL_ATLAS_W, (float)VOXEL_ATLAS_H, (float)VOXEL_ATLAS_SLOT);
    DeckBump_ApplyIndoor(&sWetBump);
    return true;
}

void DeckRain_WetChunk(float worldX, float worldZ)
{
    DeckGl_Set2f(sWetChunk, worldX, worldZ);
}

void DeckRain_WetEnd(void)
{
    DeckGl_UseProgram(NULL);
}

// ---- State ---------------------------------------------------------------------------------------------
void DeckRain_Init(void)
{
    const char *e = getenv("DECK_RAIN_OFF");

    sOff[0] = '\0';
    if (e != NULL)
        strncat(sOff, e, sizeof(sOff) - 1);
    DeckRain_Shutdown(); // idempotent: a second Init deletes the first one's programs and buffers
    StreakInit();
    WetInit();
    SplashInit();
    sReady = sStreak.program != 0 || sWetProg.program != 0 || sSplash.program != 0;
    DeckGl_Log("[RAIN] init: streaks %s, wet %s, splash %s, off list '%s'\n", sStreak.program != 0 ? "ready" : "failed",
               sWetProg.program != 0 ? "ready" : "failed", sSplash.program != 0 ? "ready" : "failed", sOff);
}

void DeckRain_Shutdown(void)
{
    if (sStreak.program != 0)
        DeckGl_DeleteProgram(&sStreak);
    if (sWetProg.program != 0)
        DeckGl_DeleteProgram(&sWetProg);
    if (sSplash.program != 0)
        DeckGl_DeleteProgram(&sSplash);
    if (sSplashVbo != 0)
        glDeleteBuffers(1, &sSplashVbo);
    sSplashVbo = 0;
    if (sStreakVbo != 0)
        glDeleteBuffers(1, &sStreakVbo);
    sStreakVbo = 0;
    sWet = 0.0f;
    sLastSeconds = -1.0f;
    sLastLog = -100.0f;
    sReady = false;
}

float DeckRain_Mist(float weatherAmount)
{
    float t = (Clamp01(weatherAmount) - 0.35f) / 0.65f;

    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_MIST, 0.5f, 1.0f, 1.0f);

    if (!DeckRain_Enabled() || Off("mist") || lvl <= 0.0f || t <= 0.0f)
        return 0.0f;
    return t * t * (3.0f - 2.0f * t) * 0.5f * sWet * lvl;
}

// Master gate only (programs built, shaders on); streaks, wet ground, mist and splashes each have a level key
// (voxel_rain, voxel_wet, voxel_mist, voxel_splash: OFF / AUTO / LOW / MEDIUM / HIGH, LOW = half).
bool DeckRain_Enabled(void)
{
    return sReady && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0;
}

void DeckRain_Update(bool rain, float amount, bool indoor, float seconds)
{
    float dt = sLastSeconds < 0.0f ? 0.0f : seconds - sLastSeconds;

    sLastSeconds = seconds;
    if (dt < 0.0f)
        dt = 0.0f; // first frame or a clock reset
    if (dt > 0.5f)
        dt = 0.5f; // a stall or fade does not jump the clock, but slow frames still make progress
    // Wetness always runs: indoors, with the setting off or after a toggle it dries at the 90 s rate, never freezes.
    if (rain && !indoor && DeckRain_Enabled() && (DeckLevel(FAST_EXTRA_VOXEL_WET) >= 0 || DeckLevel(FAST_EXTRA_VOXEL_MIST) >= 0))
        sWet += dt / 20.0f * Clamp01(amount);
    else
        sWet -= dt / 90.0f;
    sWet = Clamp01(sWet);
    if (seconds - sLastLog >= 10.0f && (rain || sWet > 0.0f))
    {
        sLastLog = seconds;
        DeckGl_Log("[RAIN] t=%.0f s amount %.2f wet %.2f\n", seconds, amount, sWet);
    }
}
