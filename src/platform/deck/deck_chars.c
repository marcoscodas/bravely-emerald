#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "deck_chars.h"
#include "deck_levels.h"
#include "deck_bump.h"
#include "../voxel/voxel_entities.h"
#include "deck_gl_shader.h"
#include "platform/vita_extras.h"
#include "platform/vita_daynight.h"

// ---- G4 outline and rim --------------------------------------------------------------------------------
static const char *kOutVs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    vEye = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

// Sprite texel (alpha > 0.5): the plain modulate result plus a warm rim where the texel toward the sun is clear.
// Clear texel with a sprite texel at +-1 in x or y: a dark outline (blended). Everything else is discarded as before.
static const char *kOutFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexel;\n"
    "uniform vec2 uSunDir;\n"       // texels toward the sun, in uv
    "uniform vec2 uParams;\n"       // outline alpha, (unused)
    "uniform float uEmis;\n"        // night self glow of the sprite (0 = none)
    "uniform vec4 uLight;\n"        // sprite lighting: x shade amplitude (0 = none), y rim strength, z dusk 0..1, w sun 1 / moon 0
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    // atlas texel at offset o (texels) from p, kept inside the 64 x 64 slot of p
    "vec4 scTap(sampler2D s, vec2 p, vec2 o) {\n"
    "    vec2 org = floor(p / 64.0) * 64.0;\n"
    "    vec2 q = clamp(floor(p + o), org, org + 63.0);\n"
    "    return texture2D(s, (q + 0.5) * uTexel);\n"
    "}\n"
    "void main() {\n"
    "    vec4 c = texture2D(uTex, vUv);\n"
    "    float bm = 1.0;\n"
    "#ifdef DECK_BUMP\n"
    "    bm = deckBumpMul(uTex, vUv, c.rgb, vCol.rgb, vEye);\n"   // before the branch: the screen derivatives need the whole quad
    "#endif\n"
    "    if (c.a > 0.5) {\n"
    // the one rim (uLight.y: the outline level alone with sprite lighting OFF, else the sprite lighting level); dusk widens it to 2 texels
    "        float r1 = 1.0 - step(0.5, texture2D(uTex, vUv + uSunDir * uTexel).a);\n"
    "        float r2 = (1.0 - step(0.5, texture2D(uTex, vUv + uSunDir * (2.0 * uTexel)).a)) * (1.0 - r1);\n"
    "        float rim = (r1 + uLight.z * 0.6 * r2) * uLight.y;\n"
    "        vec3 lit = c.rgb * vCol.rgb * bm;\n"
    "        if (uLight.x > 0.0) {\n"
    // pseudo normal: card texels away from the sun minus toward it along the sun axis (+1 on the sun side edge, -1 on the far
    // edge, 0 in the middle), plus the luminance step across the texel; luminance factor stays within 1 +- uLight.x
    "            vec2 p = vUv / uTexel;\n"
    "            vec2 d = vec2(uSunDir.x, 0.5 * uSunDir.y);\n"
    "            float nrm = 0.0;\n"
    "            for (int k = 1; k <= 6; k++) {\n"
    "                vec2 o = d * (2.0 * float(k));\n"
    "                nrm += step(0.5, scTap(uTex, p, -o).a) - step(0.5, scTap(uTex, p, o).a);\n"
    "            }\n"
    "            nrm /= 6.0;\n"
    "            vec4 tT = scTap(uTex, p, d);\n"
    "            vec4 tA = scTap(uTex, p, -d);\n"
    "            float hint = (tT.a > 0.5 && tA.a > 0.5) ? dot(tA.rgb - tT.rgb, vec3(0.299, 0.587, 0.114)) : 0.0;\n"
    "            float shade = clamp(0.8 * nrm + 0.9 * hint, -1.0, 1.0);\n"
    "            vec3 warm = mix(vec3(0.97, 1.0, 1.06), vec3(1.04, 1.0, 0.94), uLight.w) - vec3(0.0, 0.015, 0.03) * uLight.z;\n"
    "            vec3 cool = vec3(0.95, 0.98, 1.05);\n"
    "            vec3 tint = mix(vec3(1.0), mix(cool, warm, 0.5 + 0.5 * shade), min(uLight.x * 5.0, 1.0));\n"
    "            lit *= (1.0 + uLight.x * shade) * tint;\n"
    "        }\n"
    "        vec3 rimCol = mix(vec3(0.06, 0.10, 0.22), mix(vec3(0.34, 0.24, 0.10), vec3(0.42, 0.20, 0.06), uLight.z), uLight.w);\n"
    // night self glow: the sprite itself lifts toward a warm light, independent of the dark vertex tint, keeping its own colours
    "        lit = lit * (1.0 + 0.9 * uEmis) + c.rgb * vec3(1.0, 0.82, 0.55) * (0.55 * uEmis);\n"
    "        vec3 rgb = lit + rimCol * rim * vCol.rgb;\n"
    "        gl_FragColor = vec4(min(rgb, vec3(1.0)), c.a * vCol.a);\n"
    "        return;\n"
    "    }\n"
    "    float n = max(max(texture2D(uTex, vUv + vec2(uTexel.x, 0.0)).a, texture2D(uTex, vUv - vec2(uTexel.x, 0.0)).a),\n"
    "                  max(texture2D(uTex, vUv + vec2(0.0, uTexel.y)).a, texture2D(uTex, vUv - vec2(0.0, uTexel.y)).a));\n"
    "    if (n >= 0.5 && uParams.x > 0.0) {\n"
    "        gl_FragColor = vec4(vec3(0.04, 0.06, 0.13) * vCol.rgb, uParams.x * vCol.a);\n"
    "        return;\n"
    "    }\n"
    "    discard;\n"
    "}\n";

static DeckGlProgram sOut;
static DeckBumpUniforms sOBump;
static GLint sOTex = -1, sOTexel = -1, sOSun = -1, sOParams = -1, sOLight = -1, sOEmis = -1;
static float sGlowAmt = 0.0f;   // lantern glow amount of the frame (0 = none), set by DeckChars_OutlineBegin
static bool sBlend = false;
static bool sEnvIndoor = false;
static float sEnvRain = 0.0f;

// ---- B4 blob shadow ------------------------------------------------------------------------------------
static const char *kBlobVs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

static const char *kBlobFs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    float r = length(vUv);\n"
    "    float k = 1.0 - smoothstep(0.0, 1.0, r);\n"
    "    float a = vCol.a * k * k;\n"
    "    if (a < 0.004) discard;\n"
    "    gl_FragColor = vec4(vCol.rgb, a);\n"
    "}\n";

static DeckGlProgram sBlob;

// ---- Lantern glow: a round warm halo billboard behind each character card -----------------------------------
// vUv.x runs -1..1 across the quad, vUv.y -1..1 from the bottom (uv y 0 is the body centre). The quad's bottom edge is the feet line:
// the halo fades to nothing over its first part, so the ground never cuts it into a visible straight edge.
static const char *kGlowFs =
    "#version 120\n"
    "uniform float uAmt;\n"          // alpha at the body centre
    "uniform float uFoot;\n"         // vUv.y of the feet line
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    float r = length(vUv);\n"
    "    float k = 1.0 - smoothstep(0.0, 1.0, r);\n"
    "    float a = uAmt * k * k * smoothstep(uFoot, uFoot + 0.12, vUv.y);\n"
    "    if (a < 0.004) discard;\n"
    // premultiplied, with a lowered alpha: mostly adds light, so it still shows once the night grade darkens the world
    "    gl_FragColor = vec4(vec3(1.0, 0.78, 0.45) * a, a * 0.4);\n"
    "}\n";

static DeckGlProgram sGlow;
static GLint sGAmt = -1, sGFoot = -1;

#define GLOW_RADIUS 1.5f   // sprite heights
#define GLOW_CENTRE 0.40f  // body centre above the feet, sprite heights

typedef struct
{
    float x, y, z, u, v;
    uint8_t r, g, b, a;
} BlobVertex;

static BlobVertex sBlobVerts[DECK_CHARS_MAX_BLOBS * 6];
static BlobVertex sGlowVerts[DECK_CHARS_MAX_BLOBS * 6];

#define BLOB_HALF_X 0.40f   // tiles
#define BLOB_HALF_Z 0.26f
#define BLOB_SUN_PUSH 0.07f // centre pushed away from the sun (the silhouette falls the same way)

void DeckChars_Init(void)
{
    sOut.program = 0;
    sBlob.program = 0;
    if (DeckBump_Build(&sOut, &sOBump, "char-outline", kOutVs, kOutFs))
    {
        sOTex = DeckGl_Uniform(&sOut, "uTex");
        sOTexel = DeckGl_Uniform(&sOut, "uTexel");
        sOSun = DeckGl_Uniform(&sOut, "uSunDir");
        sOParams = DeckGl_Uniform(&sOut, "uParams");
        sOLight = DeckGl_Uniform(&sOut, "uLight");
        sOEmis = DeckGl_Uniform(&sOut, "uEmis");
    }
    else
        DeckGl_Log("[CHARS] outline shader build failed: plain billboards stay\n");
    if (!DeckGl_BuildProgram(&sBlob, "char-blob", kBlobVs, kBlobFs))
        DeckGl_Log("[CHARS] blob shader build failed: silhouette shadows only\n");
    sGlow.program = 0;
    if (DeckGl_BuildProgram(&sGlow, "char-glow", kBlobVs, kGlowFs))
    {
        sGAmt = DeckGl_Uniform(&sGlow, "uAmt");
        sGFoot = DeckGl_Uniform(&sGlow, "uFoot");
    }
    else
        DeckGl_Log("[CHARS] glow shader build failed: no lantern halo\n");
    DeckGl_Log("[CHARS] init: outline %s, blob shadow %s, glow %s\n", sOut.program != 0 ? "ready" : "failed",
               sBlob.program != 0 ? "ready" : "failed", sGlow.program != 0 ? "ready" : "failed");
}

void DeckChars_Shutdown(void)
{
    if (sOut.program != 0)
        DeckGl_DeleteProgram(&sOut);
    if (sBlob.program != 0)
        DeckGl_DeleteProgram(&sBlob);
    if (sGlow.program != 0)
        DeckGl_DeleteProgram(&sGlow);
}

// One halo quad per character card (6 vertices each, as EmitBillboard writes them), in the card's own plane: centred GLOW_CENTRE
// sprite heights above the feet, GLOW_RADIUS heights to every side, its bottom edge on the feet line.
void DeckChars_DrawGlow(const void *verts, uint32_t stride, uint32_t first, uint32_t end)
{
    const float peak = 0.7f * sGlowAmt;   // only a faint ground light now; the sprite glows itself (uEmis)
    const float footV = -GLOW_CENTRE / GLOW_RADIUS;
    const float spanV = 1.0f - footV;
    int n = 0;
    GLboolean culled;

    if (sGlow.program == 0 || sGlowAmt <= 0.0f || verts == NULL || end <= first)
        return;
    for (uint32_t c = first; c + 6 <= end && n + 6 <= DECK_CHARS_MAX_BLOBS * 6; c += 6)
    {
        const float *p[6];
        const float *lo = NULL, *lo2 = NULL, *nearV = NULL;
        float rx, ry, rz, ux, uy, uz, rl, ul, h;
        float cx, cy, cz, hx, hy, hz;
        static const float kU[6] = { -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

        for (int k = 0; k < 6; ++k)
            p[k] = (const float *)((const char *)verts + (size_t)(c + (uint32_t)k) * stride);
        for (int k = 0; k < 6; ++k)
            if (lo == NULL || p[k][1] < lo[1])
                lo = p[k];
        for (int k = 0; k < 6; ++k)
            if (p[k][1] < lo[1] + 0.0001f && (fabsf(p[k][0] - lo[0]) + fabsf(p[k][2] - lo[2])) > 0.0001f)
            {
                lo2 = p[k];
                break;
            }
        for (int k = 0; k < 6; ++k)
        {
            const float d = fabsf(p[k][0] - lo[0]) + fabsf(p[k][1] - lo[1]) + fabsf(p[k][2] - lo[2]);

            if (p[k][1] > lo[1] + 0.0001f && (nearV == NULL || d < fabsf(nearV[0] - lo[0]) + fabsf(nearV[1] - lo[1]) + fabsf(nearV[2] - lo[2])))
                nearV = p[k];
        }
        if (lo2 == NULL || nearV == NULL)
            continue;
        rx = lo2[0] - lo[0]; ry = lo2[1] - lo[1]; rz = lo2[2] - lo[2];
        ux = nearV[0] - lo[0]; uy = nearV[1] - lo[1]; uz = nearV[2] - lo[2];
        rl = sqrtf(rx * rx + ry * ry + rz * rz);
        ul = sqrtf(ux * ux + uy * uy + uz * uz);
        if (rl < 0.0001f || ul < 0.0001f)
            continue;
        h = ul;
        rx /= rl; ry /= rl; rz /= rl;
        ux /= ul; uy /= ul; uz /= ul;
        // feet middle (the card's bottom edge centre), then the left and right ends of the halo's bottom and top edges
        cx = 0.5f * (lo[0] + lo2[0]);
        cy = 0.5f * (lo[1] + lo2[1]);
        cz = 0.5f * (lo[2] + lo2[2]);
        hx = rx * GLOW_RADIUS * h; hy = ry * GLOW_RADIUS * h; hz = rz * GLOW_RADIUS * h;
        for (int k = 0; k < 6; ++k)
        {
            BlobVertex *v = &sGlowVerts[n++];
            const float side = kU[k];                       // -1 left, 1 right
            const float top = (k == 2 || k == 4 || k == 5) ? 1.0f : 0.0f;
            const float up = top * spanV * GLOW_RADIUS * h; // above the feet line

            v->x = cx + side * hx + ux * up;
            v->y = cy + side * hy + uy * up;
            v->z = cz + side * hz + uz * up;
            v->u = side;
            v->v = footV + top * spanV;
            v->r = v->g = v->b = 255;
            v->a = 255;
        }
    }
    if (n == 0)
        return;
    DeckGl_UseProgram(&sGlow);
    DeckGl_Set1f(sGAmt, peak);
    DeckGl_Set1f(sGFoot, footV);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    culled = glIsEnabled(GL_CULL_FACE);                     // the quad's winding follows the card's right edge: draw both sides
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(BlobVertex), &sGlowVerts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BlobVertex), &sGlowVerts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(BlobVertex), &sGlowVerts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, n);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (culled)
        glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    DeckGl_UseProgram(NULL);
}

void DeckChars_SetEnv(bool indoor, float rain)
{
    sEnvIndoor = indoor;
    sEnvRain = rain < 0.0f ? 0.0f : (rain > 1.0f ? 1.0f : rain);
}

static float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

bool DeckChars_OutlineBegin(float sunDx, float sunDz, float sunStrength)
{
    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_OUTLINE, 0.5f, 1.0f, 1.0f);
    const float sl = DeckLevelPick(FAST_EXTRA_VOXEL_SPRLIGHT, 0.4f, 0.7f, 1.0f);
    static int sDebug = -1;
    float shadeAmp = 0.0f, rimStr, dusk = 0.0f, sunW = 1.0f, glow = 0.0f;
    static float sGlowMul = -1.0f;
    static int sGlowDebug = -1;

    if (sGlowMul < 0.0f)
    {
        const char *e = getenv("DECK_SPRGLOW_STRENGTH");
        const char *d = getenv("DECK_SPRGLOW_DEBUG");

        sGlowMul = e != NULL ? (float)atof(e) : 1.0f;
        if (sGlowMul < 0.0f)
            sGlowMul = 0.0f;
        sGlowDebug = (d != NULL && d[0] == '1') ? 1 : 0;
        if (sGlowDebug)
            DeckGl_Log("[SPRGLOW] DECK_SPRGLOW_DEBUG=1: night forced\n");
    }

    // The program also runs for the relief alone (outline OFF): outline alpha and rim 0, clear texels discarded
    if (sOut.program == 0 || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0
        || (lvl <= 0.0f && sl <= 0.0f && !(sOBump.ok && DeckBump_Enabled())))
        return false;
    if (sDebug < 0)
    {
        const char *e = getenv("DECK_SPRLIGHT_DEBUG");

        sDebug = (e != NULL && e[0] == '1') ? 1 : 0;
        if (sDebug)
            DeckGl_Log("[SPRLIGHT] DECK_SPRLIGHT_DEBUG=1: shade 0.45, rim 0.9 whatever the sun, level and weather\n");
    }
    if (sl > 0.0f)
    {
        const float s = Clamp01(sunStrength);
        const float moonW = 1.0f - Clamp01(s * 4.0f);                // 1 at night, 0 once the sun is up a little
        const float env = sEnvIndoor ? 0.10f : 1.0f - 0.85f * sEnvRain; // indoors and in rain almost flat
        const float lightK = s > 0.5f * moonW ? s : 0.5f * moonW;      // the moon lights at most half as much as the sun
        uint32_t t = 0;

        sunW = 1.0f - moonW;
        if (s < 0.02f && !sEnvIndoor && VitaDayNight_BeamTime(&t))
        {
            const VitaDayNightSun m = VitaDayNight_MoonPathAt(t);

            sunDx = m.dx;
            sunDz = m.dz;
        }
        dusk = Clamp01(1.0f - s / 0.6f) * sunW;                      // low sun: longer, redder rim
        shadeAmp = 0.20f * sl * env * lightK;
        rimStr = 0.9f * sl * env * (s > 0.4f * moonW ? s : 0.4f * moonW);
        if (!sEnvIndoor)
        {
            // 0 in daylight (sun strength 0.5 and up), 1 at full night; half in rain; scaled by the sprite lighting level
            float night = 1.0f - Clamp01((s - 0.05f) / 0.45f);

            if (sGlowDebug)
                night = 1.0f;
            glow = 0.60f * night * sl * (1.0f - 0.5f * sEnvRain) * sGlowMul;
        }
        if (sDebug)
        {
            shadeAmp = 0.45f;
            rimStr = 0.9f;
        }
    }
    else
        rimStr = 0.9f * sunStrength * lvl;                           // sprite lighting OFF: the old outline rim
    DeckGl_UseProgram(&sOut);
    DeckGl_Set1i(sOTex, 0);
    DeckGl_Set2f(sOTexel, 1.0f / (float)VOXEL_SPRITE_ATLAS_W, 1.0f / (float)VOXEL_SPRITE_ATLAS_H);
    DeckGl_Set2f(sOSun, sunDx > 0.0f ? -1.0f : 1.0f, sunDz > 0.0f ? 1.0f : -1.0f);
    DeckGl_Set2f(sOParams, lvl > 0.0f ? 0.62f : 0.0f, 0.0f);
    DeckGl_Set4f(sOLight, shadeAmp, rimStr, dusk, sunW);
    DeckGl_Set1f(sOEmis, glow);
    sGlowAmt = glow;
    DeckBump_Set(&sOBump, (float)VOXEL_SPRITE_ATLAS_W, (float)VOXEL_SPRITE_ATLAS_H, 0.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sBlend = true;
    return true;
}

void DeckChars_OutlineEnd(void)
{
    if (sBlend)
        glDisable(GL_BLEND);
    sBlend = false;
    DeckGl_UseProgram(NULL);
}

bool DeckChars_ShadowEnabled(void)
{
    return sBlob.program != 0 && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0
        && DeckLevel(FAST_EXTRA_VOXEL_SHADOW) >= 0;
}

// Silhouette peak 0.36 x 0.62 = 0.22, blob peak 0.20: where they overlap the sum is about 0.38, the old single shadow was 0.36.
float DeckChars_SilhouetteScale(void)
{
    return DeckChars_ShadowEnabled() ? 0.62f : 1.0f;
}

void DeckChars_DrawBlobs(const float (*blobs)[4], int count, float sunDx, float sunDz)
{
    static const float kCorner[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_SHADOW, 0.5f, 1.0f, 1.0f);
    int n = 0;

    if (!DeckChars_ShadowEnabled() || count <= 0)
        return;
    if (count > DECK_CHARS_MAX_BLOBS)
        count = DECK_CHARS_MAX_BLOBS;
    {
        const float len = sqrtf(sunDx * sunDx + sunDz * sunDz);
        const float px = len > 0.001f ? sunDx / len * BLOB_SUN_PUSH : 0.0f;
        const float pz = len > 0.001f ? sunDz / len * BLOB_SUN_PUSH : 0.0f;

        for (int i = 0; i < count; ++i)
        {
            const uint8_t a = (uint8_t)(blobs[i][3] * lvl * 255.0f + 0.5f);

            if (a == 0)
                continue;
            for (int k = 0; k < 6; ++k)
            {
                BlobVertex *v = &sBlobVerts[n++];

                v->x = blobs[i][0] + px + kCorner[k][0] * BLOB_HALF_X;
                v->y = blobs[i][1];
                v->z = blobs[i][2] + pz + kCorner[k][1] * BLOB_HALF_Z;
                v->u = kCorner[k][0];
                v->v = kCorner[k][1];
                v->r = 8; v->g = 20; v->b = 40;          // the indigo of the silhouette shadows
                v->a = a;
            }
        }
    }
    if (n == 0)
        return;
    DeckGl_UseProgram(&sBlob);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(BlobVertex), &sBlobVerts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BlobVertex), &sBlobVerts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(BlobVertex), &sBlobVerts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, n);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
}
