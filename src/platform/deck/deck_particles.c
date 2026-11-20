#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deck_particles.h"
#include "deck_levels.h"
#include "deck_gl_shader.h"
#include "deck_bump.h"
#include "../voxel/voxel_atlas.h"
#include "global.h"
#include "constants/map_types.h"
#include "platform/vita_extras.h"

#define N_POLLEN 192
#define N_FIRE 64
#define N_LEAF 32
#define N_ALL (N_POLLEN + N_FIRE + N_LEAF)

static bool sReady = false;
static char sOff[64];
static bool Off(const char *name) { return strstr(sOff, name) != NULL; }
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Vertex layout as deck_rain.c: seed x y z in the vertex position, corner x y in texcoord 0.
static const float kCorner[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

static GLuint MakeVbo(void)
{
    float *v = (float *)malloc((size_t)N_ALL * 6 * 5 * sizeof(float));
    GLuint vbo = 0;
    uint32_t seed = 0x9A471C0Du;

    if (v == NULL)
        return 0;
    for (int i = 0; i < N_ALL; ++i)
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
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)N_ALL * 6 * 5 * sizeof(float)), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    free(v);
    return vbo;
}

// ---- F2: the particle program --------------------------------------------------------------------------
static const char *kPartVs =
    "#version 120\n"
    "uniform float uTime;\n"
    "uniform float uViewH;\n"      // height in pixels of the viewport the particles draw into
    "uniform vec3 uTarget;\n"      // camera target x, ground, z
    "uniform vec2 uUp;\n"          // cos, sin of the camera pitch
    "uniform float uKind;\n"       // 0 pollen, 1 firefly, 2 leaf
    "uniform vec4 uColor;\n"       // tint, alpha scale
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "float h(float n) { return fract(sin(n) * 43758.5453); }\n"
    "void main() {\n"
    "    vec3 seed = gl_Vertex.xyz;\n"
    "    vec3 r = vec3(h(seed.x * 91.7 + seed.z * 13.1), h(seed.y * 57.3 + seed.x * 7.7), h(seed.z * 31.7 + seed.y * 3.3));\n"
    "    vec2 span = vec2(22.0, 18.0);\n"                      // the mote box of DrawMotes
    "    vec2 vel; float size; float y; float a = 1.0;\n"
    "    vec2 c = gl_MultiTexCoord0.xy;\n"
    "    if (uKind < 0.5) {\n"
    "        vel = vec2(0.18 + 0.25 * r.x, 0.07 + 0.10 * r.y);\n"
    "        size = 0.05 + 0.035 * r.z;\n"
    "    } else if (uKind < 1.5) {\n"
    "        vel = vec2(0.05, 0.03) * (r.x - 0.5);\n"
    "        size = 0.16;\n"
    "    } else {\n"
    "        vel = vec2(0.55 + 0.3 * r.x, 0.35 * r.y);\n"       // the wind direction of DeckGl_SetFrameUniforms, a little off
    "        size = 0.10 + 0.04 * r.z;\n"
    "    }\n"
    "    vec2 p = fract(seed.xz + fract((uTime * vel - uTarget.xz) / span));\n"
    "    vec2 d = 0.5 - abs(p - 0.5);\n"
    "    float edge = clamp(min(d.x * span.x, d.y * span.y) / 2.5, 0.0, 1.0);\n"
    "    vec3 w = vec3(uTarget.x - 11.0 + p.x * span.x, 0.0, uTarget.z - 14.0 + p.y * span.y);\n"
    "    vec3 rgb = uColor.rgb;\n"
    "    if (uKind < 0.5) {\n"
    // Smooth motion: everything below is a sum of slow sines of the continuous clock uTime (seconds), each particle with its own
    // phase and rate: no frame counts, no ticks, no rounding of positions. Dust: the straight wind drift above plus a lazy
    // sway on both axes, a slow bob and a gust that leans the higher motes more. Fireflies: wandering paths of three
    // incommensurate sines per axis (never a closed circle), a slow vertical bob, a slow smooth pulse.
    "        vec2 ph = r.xy * 6.2832;\n"
    "        w.xz += vec2(0.30 * sin(uTime * 0.31 * (0.7 + r.x) + ph.x) + 0.14 * sin(uTime * 0.83 * (0.6 + r.y) + ph.y * 1.7),\n"
    "                     0.26 * sin(uTime * 0.27 * (0.7 + r.y) + ph.y) + 0.12 * sin(uTime * 0.71 * (0.6 + r.z) + ph.x * 1.3));\n"
    "        y = 0.25 + seed.y * 2.9 + 0.14 * sin(uTime * (0.45 + 0.5 * r.x) + r.y * 6.2832) + 0.06 * sin(uTime * (1.3 + 0.6 * r.z) + ph.x);\n"
    "        w.x += y * 0.10 * sin(uTime * 0.23 + r.z * 6.2832);\n"
    "        float tw = 0.5 + 0.5 * sin(uTime * (0.5 + 0.9 * r.y) + r.z * 20.0);\n"
    "        a = edge * (0.35 + 0.65 * tw);\n"
    "    } else if (uKind < 1.5) {\n"
    "        vec2 ph = r.xy * 6.2832;\n"
    "        w.xz += vec2(0.80 * sin(uTime * 0.23 * (0.8 + r.x) + ph.x) + 0.45 * sin(uTime * 0.51 * (0.7 + r.y) + ph.y * 2.3) + 0.20 * sin(uTime * 1.07 * (0.8 + r.z) + ph.x * 3.1),\n"
    "                     0.80 * cos(uTime * 0.19 * (0.8 + r.y) + ph.y) + 0.45 * sin(uTime * 0.47 * (0.7 + r.z) + ph.x * 1.9) + 0.20 * sin(uTime * 0.97 * (0.8 + r.x) + ph.y * 2.7));\n"
    "        y = 0.35 + seed.y * 1.4 + 0.22 * sin(uTime * (0.50 + 0.30 * r.x) + r.x * 6.2832) + 0.08 * sin(uTime * (1.4 + 0.5 * r.y) + ph.y);\n"
    "        float b = 0.5 + 0.5 * sin(uTime * (0.55 + 0.45 * r.x) + r.z * 40.0);\n"
    "        a = edge * (0.10 + 0.90 * smoothstep(0.15, 0.85, b));\n"
    "    } else {\n"
    "        float py = fract(seed.y - uTime * (0.30 + 0.2 * r.x) / 5.0);\n"
    "        y = py * 5.0;\n"
    "        w.xz += vec2(0.35 * sin(uTime * 1.7 + r.x * 6.2832), 0.25 * cos(uTime * 1.3 + r.y * 6.2832));\n"
    "        a = edge * smoothstep(0.0, 0.1, py) * (1.0 - smoothstep(0.9, 1.0, py));\n"
    "        float ang = uTime * (1.2 + r.y) + r.z * 6.2832;\n"
    "        c = mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * c;\n"
    "        c.x *= 0.35 + 0.65 * abs(cos(uTime * 2.1 + r.x * 6.2832));\n"
    "        rgb *= mix(vec3(0.42, 0.62, 0.20), vec3(0.85, 0.52, 0.16), r.z);\n"
    "    }\n"
    "    w.y = uTarget.y + y;\n"
    // Sub-pixel smoothness: a quad narrower than about 2.6 pixels is widened to that and its alpha scaled by the area ratio, so a
    // slow drift moves brightness across neighbouring pixels instead of jumping a whole pixel.
    "    float clipW = max((gl_ModelViewProjectionMatrix * vec4(w, 1.0)).w, 0.1);\n"
    "    float pxPerUnit = gl_ProjectionMatrix[1][1] * 0.5 * uViewH / clipW;\n"
    "    float sz = max(size, 2.6 / pxPerUnit);\n"
    "    a *= (size * size) / (sz * sz);\n"
    "    vec3 pos = w + vec3(1.0, 0.0, 0.0) * c.x * sz + vec3(0.0, uUp.x, uUp.y) * c.y * sz;\n"
    "    vCol = vec4(rgb, a * uColor.a);\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";

static const char *kPartFs =
    "#version 120\n"
    "uniform float uKind;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    float r2 = dot(vUv, vUv);\n"
    "    float s;\n"
    "    if (uKind < 0.5) s = exp(-r2 * 3.0) * (1.0 - smoothstep(0.7, 1.0, r2));\n"
    "    else if (uKind < 1.5) s = exp(-r2 * 3.5) * 0.8 + 0.5 * (1.0 - smoothstep(0.0, 0.12, r2));\n"
    "    else s = 1.0 - smoothstep(0.55, 1.0, length(vUv * vec2(1.5, 0.8)));\n"
    "    float a = s * vCol.a;\n"
    "    if (a < 0.01) discard;\n"
    "    gl_FragColor = vec4(vCol.rgb, min(a, 1.0));\n"
    "}\n";

static DeckGlProgram sPart;
static GLint sUTarget = -1, sUUp = -1, sUKind = -1, sUColor = -1, sUViewH = -1;
static GLuint sVbo = 0;

static void PartInit(void)
{
    if (!DeckGl_BuildProgram(&sPart, "particles", kPartVs, kPartFs))
    {
        DeckGl_Log("[PART] particle shader build failed: legacy motes stay\n");
        return;
    }
    sUTarget = DeckGl_Uniform(&sPart, "uTarget");
    sUUp = DeckGl_Uniform(&sPart, "uUp");
    sUKind = DeckGl_Uniform(&sPart, "uKind");
    sUColor = DeckGl_Uniform(&sPart, "uColor");
    sUViewH = DeckGl_Uniform(&sPart, "uViewH");
    sVbo = MakeVbo();
    if (sVbo == 0)
        DeckGl_DeleteProgram(&sPart);
}

static bool DebugOn(void)
{
    static int s = -1;

    if (s < 0)
    {
        const char *e = getenv("DECK_GL_DEBUG");

        s = (e != NULL && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return s == 1;
}

// voxel_particles level: OFF = the legacy motes; LOW = half the particles. The glints have their own key (voxel_glint).
bool DeckParticles_Enabled(void)
{
    return sReady && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 && DeckLevel(FAST_EXTRA_VOXEL_PARTICLES) >= 0;
}

static void DrawKind(int kind, int first, int count, const float rgba[4], bool additive)
{
    if (count <= 0)
        return;
    DeckGl_Set1f(sUKind, (float)kind);
    DeckGl_Set4f(sUColor, rgba[0], rgba[1], rgba[2], rgba[3]);
    glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, first * 6, count * 6);
}

bool DeckParticles_Draw(const DeckPartFrame *f)
{
    static float sLastLog = -100.0f;
    int pollen = 0, fire = 0, leaf = 0;
    const u8 mapType = gMapHeader.mapType;
    const bool fireMap = mapType == MAP_TYPE_ROUTE || mapType == MAP_TYPE_TOWN;
    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_PARTICLES, 0.5f, 1.0f, 1.0f) * f->density;

    if (!DeckParticles_Enabled() || sPart.program == 0 || sVbo == 0)
        return false;
    if (f->rain)
        ;                                   // rain: nothing (counts stay 0 and are logged)
    else if (!f->calm && !Off("pollen") && f->motes > 0.005f)
        pollen = (int)(Clamp01(f->motes) * (float)N_POLLEN + 0.5f);
    if (!f->rain && fireMap && !Off("fire") && f->glow > 0.3f)
        fire = (int)(Clamp01((f->glow - 0.3f) / 0.7f) * (float)N_FIRE + 0.5f);
    if (!f->rain && !f->calm && !Off("leaf") && f->treeVerts > 300 && f->glow < 0.6f)
        leaf = (int)(Clamp01((float)f->treeVerts / 2500.0f) * (float)N_LEAF + 0.5f);
    pollen = (int)((float)pollen * lvl);
    fire = (int)((float)fire * lvl);
    leaf = (int)((float)leaf * lvl);
    if (DebugOn() && f->seconds - sLastLog >= 5.0f)
    {
        sLastLog = f->seconds;
        DeckGl_Log("[PART] t=%.0f s drawn: pollen %d fireflies %d leaves %d (total %d) motes %.2f glow %.2f treeVerts %d mapType %d\n",
                   f->seconds, pollen, fire, leaf, pollen + fire + leaf, f->motes, f->glow, f->treeVerts, (int)mapType);
    }
    if (pollen + fire + leaf == 0)
        return false;

    DeckGl_UseProgram(&sPart);
    DeckGl_Set3f(sUTarget, f->target[0], f->target[1], f->target[2]);
    DeckGl_Set2f(sUUp, f->pitchCosSin[0], f->pitchCosSin[1]);
    {
        GLint vp[4] = { 0, 0, 960, 544 };

        glGetIntegerv(GL_VIEWPORT, vp);
        DeckGl_Set1f(sUViewH, (float)vp[3]);
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 5 * sizeof(float), (const void *)0);
    glTexCoordPointer(2, GL_FLOAT, 5 * sizeof(float), (const void *)(3 * sizeof(float)));
    {
        const float pc[4] = { 1.0f * (0.6f + 0.4f * f->light[0]), 0.94f * (0.6f + 0.4f * f->light[1]), 0.62f * (0.6f + 0.4f * f->light[2]), 0.85f };
        const float fc[4] = { 0.82f, 1.0f, 0.30f, 0.95f };
        const float lc[4] = { f->light[0], f->light[1], f->light[2], 0.95f };

        DrawKind(0, 0, pollen, pc, false);
        DrawKind(2, N_POLLEN + N_FIRE, leaf, lc, false);
        DrawKind(1, N_POLLEN, fire, fc, true);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
    return pollen > 0;
}

// ---- Water glints: the ground program with sparkles on water texels ------------------------------------
static const char *kGlintVs =
    "#version 120\n"
    "uniform vec2 uChunk;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec2 vWorld;\n"
    "varying vec3 vEye;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    vWorld = gl_Vertex.xz + uChunk;\n"
    "    vEye = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

static const char *kGlintFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform float uTime;\n"
    "uniform float uStrength;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec2 vWorld;\n"
    "varying vec3 vEye;\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex, vUv);\n"
    "    vec4 c = t * vCol;\n"
    "#ifdef DECK_BUMP\n"
    "    c.rgb *= deckBumpMul(uTex, vUv, t.rgb, vCol.rgb, vEye) * deckGroundMul(vWorld, t.rgb, vCol.rgb, vEye);\n"
    "#endif\n"
    "    float wb = clamp((t.b - max(t.r, t.g * 0.8) - 0.05) * 5.0, 0.0, 1.0);\n"      // blue texels: water
    "    vec2 g = vWorld * 2.2;\n"
    "    vec2 id = floor(g);\n"
    "    vec2 f = fract(g) - 0.5;\n"
    "    float hh = hash(id);\n"
    "    float h2 = hash(id + 7.7);\n"
    "    vec2 jit = vec2(h2 - 0.5, fract(hh * 13.7) - 0.5) * 0.5;\n"
    "    float d = length(f - jit);\n"
    "    float tw = pow(max(0.0, sin(uTime * (0.8 + 1.4 * hh) + hh * 40.0)), 10.0);\n"
    "    float spark = (1.0 - smoothstep(0.05, 0.24, d)) * tw * step(0.3, h2);\n"
    "    vec3 col = c.rgb + vec3(1.0, 0.97, 0.88) * (spark * wb * uStrength);\n"
    "    gl_FragColor = vec4(min(col, vec3(1.0)), c.a);\n"
    "}\n";

static DeckGlProgram sGlint;
static DeckBumpUniforms sGBump;
static GLint sGTex = -1, sGStrength = -1, sGChunk = -1;

static void GlintInit(void)
{
    if (!DeckBump_Build(&sGlint, &sGBump, "water-glint", kGlintVs, kGlintFs))
    {
        DeckGl_Log("[PART] glint shader build failed: plain ground stays\n");
        return;
    }
    sGTex = DeckGl_Uniform(&sGlint, "uTex");
    sGStrength = DeckGl_Uniform(&sGlint, "uStrength");
    sGChunk = DeckGl_Uniform(&sGlint, "uChunk");
}

bool DeckParticles_GlintBegin(float sun, float glow, bool rain)
{
    const float strength = Clamp01(sun * 0.95f + glow * 0.30f);

    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_GLINT, 0.5f, 1.0f, 1.0f);

    if (!sReady || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0 || lvl <= 0.0f || sGlint.program == 0
        || Off("glint") || rain || strength <= 0.02f)
        return false;
    DeckGl_UseProgram(&sGlint);
    DeckGl_Set1i(sGTex, 0);
    DeckGl_Set1f(sGStrength, strength * lvl);
    DeckBump_Set(&sGBump, (float)VOXEL_ATLAS_W, (float)VOXEL_ATLAS_H, (float)VOXEL_ATLAS_SLOT);
    return true;
}

void DeckParticles_GlintChunk(float worldX, float worldZ) { DeckGl_Set2f(sGChunk, worldX, worldZ); }

void DeckParticles_GlintEnd(void) { DeckGl_UseProgram(NULL); }

// ---- State ---------------------------------------------------------------------------------------------
void DeckParticles_Init(void)
{
    const char *e = getenv("DECK_PART_OFF");

    sOff[0] = '\0';
    if (e != NULL)
        strncat(sOff, e, sizeof(sOff) - 1);
    PartInit();
    GlintInit();
    sReady = sPart.program != 0 || sGlint.program != 0;
    DeckGl_Log("[PART] init: particles %s (%d quads), glints %s, off list '%s'\n", sPart.program != 0 ? "ready" : "failed", N_ALL,
               sGlint.program != 0 ? "ready" : "failed", sOff);
}

void DeckParticles_Shutdown(void)
{
    if (sPart.program != 0)
        DeckGl_DeleteProgram(&sPart);
    if (sGlint.program != 0)
        DeckGl_DeleteProgram(&sGlint);
    if (sVbo != 0)
        glDeleteBuffers(1, &sVbo);
    sVbo = 0;
    sReady = false;
}
