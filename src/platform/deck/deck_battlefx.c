#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "deck_battlefx.h"
#include "deck_gl_shader.h"
#include "deck_levels.h"
#include "platform/vita_daynight.h"
#include "global.h"
#include "main.h"
#include "sprite.h"
#include "battle.h"
#include "battle_anim.h"
#include "../voxel/voxel_battle.h"

// Strength per level (LOW, MEDIUM, HIGH); AUTO follows voxel_quality. All values are multipliers of the HIGH look.
//                                  LOW   MED   HIGH
static const float kHitTint[3]   = { 0.50f, 0.80f, 1.00f };  // colour flash and glow on the target
static const float kAberr[3]     = { 0.00f, 0.60f, 1.00f };  // RGB split on hit frames (0 = off on LOW)
static const float kShake[3]     = { 0.50f, 0.80f, 1.00f };  // screen shake on big hits
static const float kDrift[3]     = { 0.60f, 0.80f, 1.00f };  // slow zoom and pan of the backdrop
static const float kMotes[3]     = { 0.40f, 0.70f, 1.00f };  // floating light motes
static const float kWeather[3]   = { 0.50f, 0.80f, 1.00f };  // rain / sand / hail / sun glare density
static const float kSpot[3]      = { 0.60f, 0.80f, 1.00f };  // spotlight on the platforms
static const float kLowHp[3]     = { 0.60f, 0.80f, 1.00f };  // red edge pulse and desaturation

#define N_MOTES 48
#define N_RAIN 110
#define N_SAND 46
#define N_HAIL 70
#define MAX_QUADS 400

static bool sReady;
static DeckGlProgram sProg;
static GLint sUKind = -1;
static DeckGlProgram sFg;   // battle scenery: foreground bokeh program (one fullscreen quad)
static GLint sFgTime = -1, sFgN = -1, sFgCol = -1, sFgCol2 = -1, sFgAlpha = -1;

// Written by the game thread, read by the GL thread: a sequence number last (release), so a reader that sees the new number sees the payload.
static struct { uint8_t type, target; bool crit, big; } sHit;
static atomic_uint sHitSeq;

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float Hash(float n) { float s = sinf(n) * 43758.5453f; return s - floorf(s); }

static bool EnvOff(void)
{
    static int s = -1;

    if (s < 0)
        s = getenv("DECK_BATTLEFX_OFF") != NULL ? 1 : 0;
    return s == 1;
}

static int Level(void)
{
    return DeckLevel(FAST_EXTRA_VOXEL_BATTLEFX); // -1 off, 0 low .. 2 high
}

// The soft sprite program: gl_Vertex = uv position of the corner (y down), texcoord = corner -1..1, colour = rgba (alpha premultiplied by the caller).
// Kind 0 gaussian disc, 1 streak (long axis along texcoord y), 2 small hard dot.
static const char *kVs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

static const char *kFs =
    "#version 120\n"
    "uniform float uKind;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    float r2 = dot(vUv, vUv);\n"
    "    float s;\n"
    "    if (uKind < 0.5) s = exp(-r2 * 3.4) * (1.0 - smoothstep(0.75, 1.0, r2));\n"
    "    else if (uKind < 1.5) s = (1.0 - smoothstep(0.0, 1.0, abs(vUv.x))) * (1.0 - smoothstep(0.55, 1.0, abs(vUv.y)));\n"
    "    else s = 1.0 - smoothstep(0.35, 1.0, r2);\n"
    "    float a = s * vCol.a;\n"
    "    if (a < 0.003) discard;\n"
    "    gl_FragColor = vec4(vCol.rgb, a);\n"
    "}\n";

// Battle scenery foreground (docs/deck_notes_parts/battlescene.md): up to 8 out-of-focus discs and leaf shapes drifting slowly down the left
// and right edges (x within 0..0.11 of the edge, y within the top 75 percent: never the centre third, never the text box rows).
// vUv = the screen uv of the pixel (y down). Colour premultiplied by the shape alpha, composited with normal alpha blending.
static const char *kFgFs =
    "#version 120\n"
    "uniform float uTime;\n"
    "uniform float uN;\n"
    "uniform vec3 uCol;\n"
    "uniform vec3 uCol2;\n"
    "uniform float uAlpha;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "float h(float n) { return fract(sin(n) * 43758.5453); }\n"
    "void main() {\n"
    "    vec2 asp = vec2(960.0 / 544.0, 1.0);\n"
    "    vec3 col = vec3(0.0);\n"
    "    float A = 0.0;\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if (float(i) >= uN) break;\n"
    "        float fi = float(i);\n"
    "        float r0 = h(fi * 12.9898 + 1.0), r1 = h(fi * 78.233 + 2.0), r2 = h(fi * 37.719 + 3.0), r3 = h(fi * 5.17 + 4.0);\n"
    "        float right = mod(fi, 2.0);\n"
    "        float sp = 0.02 + 0.03 * r1;\n"                                                  // screen heights per second
    "        float py = fract(r0 + uTime * sp / 0.8 * (right > 0.5 ? -1.0 : 1.0));\n"
    "        float y = py * 0.8 - 0.05;\n"
    "        float x = 0.01 + 0.10 * r2 + 0.010 * sin(uTime * 0.13 * (0.6 + r3) + r1 * 6.2832);\n"
    "        if (right > 0.5) x = 1.0 - x;\n"
    "        float rad = 0.05 + 0.055 * r3;\n"                                                // height units
    "        vec2 d = (vUv - vec2(x, y)) * asp;\n"
    "        float fade = smoothstep(0.0, 0.15, py) * (1.0 - smoothstep(0.85, 1.0, py));\n"
    "        float s;\n"
    "        if (mod(fi, 3.0) < 1.0) {\n"                                                      // bokeh disc: soft edge, slightly brighter rim
    "            float r = length(d) / rad;\n"
    "            s = (1.0 - smoothstep(0.80, 1.0, r)) * (0.6 + 0.4 * smoothstep(0.45, 0.9, r));\n"
    "        } else {\n"                                                                        // leaf: a lens along a slowly turning axis
    "            float ang = r2 * 6.2832 + uTime * 0.06 * (r1 - 0.5);\n"
    "            vec2 q = vec2(cos(ang) * d.x + sin(ang) * d.y, -sin(ang) * d.x + cos(ang) * d.y);\n"
    "            q.x /= rad * 1.8;\n"
    "            q.y /= rad * 0.55;\n"
    "            s = smoothstep(0.0, 0.45, (1.0 - q.x * q.x) - abs(q.y));\n"
    "            s *= 0.85;\n"
    "        }\n"
    "        float sa = s * fade * uAlpha * (0.75 + 0.25 * r2);\n"
    "        vec3 sc = mix(uCol, uCol2, r0);\n"
    "        col = col * (1.0 - sa) + sc * sa;\n"
    "        A = A + sa * (1.0 - A);\n"
    "    }\n"
    "    if (A < 0.003) discard;\n"
    "    gl_FragColor = vec4(col / A, A);\n"
    "}\n";

typedef struct { float x, y, u, v, r, g, b, a; } Vtx;
static Vtx sBatch[MAX_QUADS * 6];
static int sBatchN;

static void Flush(int kind, bool additive)
{
    if (sBatchN == 0)
        return;
    DeckGl_Set1f(sUKind, (float)kind);
    glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glVertexPointer(2, GL_FLOAT, sizeof(Vtx), &sBatch[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), &sBatch[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(Vtx), &sBatch[0].r);
    glDrawArrays(GL_TRIANGLES, 0, sBatchN);
    sBatchN = 0;
}

// An oriented soft quad centred (cx, cy) in uv (y down): half sizes (hx along the direction, hy across), direction angle ang (0 = +x).
static void Quad(float cx, float cy, float hx, float hy, float ang, const float c[4])
{
    static const float qx[6] = { -1, 1, 1, -1, 1, -1 }, qy[6] = { -1, -1, 1, -1, 1, 1 };
    const float ca = cosf(ang), sa = sinf(ang);
    const float asp = 544.0f / 960.0f; // uv x spans 960 px, uv y 544 px: sizes are given in height units

    if (sBatchN + 6 > MAX_QUADS * 6)
        return;
    for (int k = 0; k < 6; k++)
    {
        const float lx = qx[k] * hx, ly = qy[k] * hy;
        Vtx *v = &sBatch[sBatchN++];

        v->x = cx + (lx * ca - ly * sa) * asp;
        v->y = cy + (lx * sa + ly * ca);
        v->u = qx[k];
        v->v = qy[k];
        v->r = c[0]; v->g = c[1]; v->b = c[2]; v->a = c[3];
    }
}

// Move type to tint (the 0..17 order of the type constants).
static void TypeTint(int t, float rgb[3])
{
    static const float k[18][3] = {
        { 1.00f, 1.00f, 0.95f }, // normal: white
        { 1.00f, 0.55f, 0.35f }, // fighting
        { 0.75f, 0.85f, 1.00f }, // flying
        { 0.75f, 0.35f, 0.95f }, // poison
        { 0.90f, 0.70f, 0.35f }, // ground
        { 0.85f, 0.75f, 0.50f }, // rock
        { 0.70f, 0.95f, 0.30f }, // bug
        { 0.60f, 0.35f, 0.90f }, // ghost: purple
        { 0.80f, 0.85f, 0.95f }, // steel
        { 0.80f, 0.80f, 0.80f }, // ???
        { 1.00f, 0.50f, 0.12f }, // fire: orange
        { 0.25f, 0.55f, 1.00f }, // water: blue
        { 0.35f, 0.95f, 0.35f }, // grass: green
        { 1.00f, 0.92f, 0.20f }, // electric: yellow
        { 1.00f, 0.45f, 0.80f }, // psychic: pink
        { 0.45f, 0.95f, 1.00f }, // ice: cyan
        { 0.45f, 0.40f, 1.00f }, // dragon
        { 0.50f, 0.30f, 0.75f }, // dark: purple
    };

    if (t < 0 || t > 17)
        t = 0;
    rgb[0] = k[t][0]; rgb[1] = k[t][1]; rgb[2] = k[t][2];
}

void DeckBattleFx_Hit(uint8_t moveType, uint8_t targetBattler, bool crit, uint16_t damage, uint16_t targetMaxHp)
{
    sHit.type = moveType;
    sHit.target = targetBattler;
    sHit.crit = crit;
    sHit.big = targetMaxHp > 0 && (uint32_t)damage * 100u >= (uint32_t)targetMaxHp * 25u; // 25 percent of the max HP or more
    atomic_fetch_add_explicit(&sHitSeq, 1u, memory_order_release);
}

void DeckBattleFx_Init(void)
{
    sReady = false;
    if (!DeckGl_BuildProgram(&sProg, "battlefx", kVs, kFs))
    {
        DeckGl_Log("[BATTLEFX] sprite program build failed: only the composite effects stay\n");
        return;
    }
    sUKind = DeckGl_Uniform(&sProg, "uKind");
    if (DeckGl_BuildProgram(&sFg, "battlefx_fg", kVs, kFgFs))
    {
        sFgTime = DeckGl_Uniform(&sFg, "uTime");
        sFgN = DeckGl_Uniform(&sFg, "uN");
        sFgCol = DeckGl_Uniform(&sFg, "uCol");
        sFgCol2 = DeckGl_Uniform(&sFg, "uCol2");
        sFgAlpha = DeckGl_Uniform(&sFg, "uAlpha");
    }
    else
        DeckGl_Log("[BATTLEFX] foreground program build failed: no scenery leaves\n");
    sReady = true;
    DeckGl_Log("[BATTLEFX] ready\n");
}

void DeckBattleFx_Shutdown(void)
{
    if (sFg.program != 0)
        DeckGl_DeleteProgram(&sFg);
    if (sProg.program != 0)
        DeckGl_DeleteProgram(&sProg);
    sReady = false;
}

bool DeckBattleFx_Enabled(void)
{
    return !EnvOff() && Level() >= 0;
}

// ---- per battle frame state ----
static float sLastSeen = -100.0f;   // seconds of the last battle frame (a gap = a new battle)
static float sEntryStart;
static unsigned sSeenHit;
static float sHitStart = -100.0f;
static float sHitTint[3];
static float sHitPos[2] = { 0.5f, 0.5f };
static bool sHitCrit, sHitBig;
static float sLowSmooth;

// Where a battler stands in composite uv (y up) and drawing uv (y down); false when there is none.
static bool BattlerUv(unsigned b, float *ux, float *uy)
{
    const struct Sprite *sprite;
    unsigned id;

    if (b >= MAX_BATTLERS_COUNT || gBattlerSpriteIds[b] >= MAX_SPRITES)
        return false;
    id = gBattlerSpriteIds[b];
    sprite = &gSprites[id];
    if (!sprite->inUse)
        return false;
    // GBA x,y -> the 16:9 window (the 3D battle overlay maps the 240x160 picture x3.4, centred in 960)
    *ux = (72.0f + (float)(sprite->x + sprite->x2) * 3.4f) / 960.0f;
    *uy = (float)GetBattlerSpriteCoord(b, BATTLER_COORD_Y) / 160.0f;
    return true;
}

bool DeckBattleFx_Begin(float seconds, float fade, DeckBattleFxFrame *out)
{
    const int lvl = Level();
    const float keep = 1.0f - Clamp01(fade);
    const unsigned seq = atomic_load_explicit(&sHitSeq, memory_order_acquire);
    float t, env;

    memset(out, 0, sizeof(*out));
    out->zoom = 1.0f;
    if (EnvOff() || lvl < 0)
        return false;
    if (seconds - sLastSeen > 0.6f)
    {
        sEntryStart = seconds;   // a new battle (or the first frame after a gap)
        sLowSmooth = 0.0f;
        sHitStart = -100.0f;
        sSeenHit = seq;          // hits of an earlier battle do not replay
    }
    sLastSeen = seconds;

    if (seq != sSeenHit)
    {
        float ux, uy;

        sSeenHit = seq;
        sHitStart = seconds;
        sHitCrit = sHit.crit;
        sHitBig = sHit.big;
        TypeTint(sHit.type, sHitTint);
        if (BattlerUv(sHit.target, &ux, &uy))
        {
            sHitPos[0] = ux;
            sHitPos[1] = uy;
        }
        else
        {
            sHitPos[0] = 0.5f;
            sHitPos[1] = 0.5f;
        }
        DeckGl_Log("[BATTLEFX] hit: type %d target %d crit %d big %d at uv %.2f,%.2f\n", (int)sHit.type, (int)sHit.target, (int)sHit.crit, (int)sHit.big, sHitPos[0], sHitPos[1]);
    }

    t = seconds - sEntryStart;
    out->entry = t < 0.9f ? (1.0f - t / 0.9f) : 0.0f;
    out->entry = out->entry * out->entry * keep;
    // drift: slow zoom and pan (no stretch: the zoom is uniform); the entry adds a quick zoom out from 1.2
    out->zoom = 1.04f + 0.012f * sinf(seconds * 0.13f) * kDrift[lvl] + 0.20f * out->entry;
    out->offset[0] = 0.006f * sinf(seconds * 0.09f + 1.0f) * kDrift[lvl];
    out->offset[1] = 0.004f * sinf(seconds * 0.11f) * kDrift[lvl];

    t = seconds - sHitStart;
    env = (t >= 0.0f && t < 1.2f) ? expf(-t * 5.0f) : 0.0f;
    if (env > 0.003f)
    {
        out->hit = env * kHitTint[lvl] * keep;
        out->tint[0] = sHitTint[0]; out->tint[1] = sHitTint[1]; out->tint[2] = sHitTint[2];
        out->center[0] = sHitPos[0];
        out->center[1] = 1.0f - sHitPos[1];
        out->radius = 0.28f + 0.20f * (1.0f - env);
        out->aberration = 0.0045f * env * kAberr[lvl] * keep;
        if (sHitBig)
        {
            out->offset[0] += 0.012f * env * sinf(t * 70.0f) * kShake[lvl];
            out->offset[1] += 0.009f * env * cosf(t * 83.0f) * kShake[lvl];
        }
        if (sHitCrit && t < 0.4f)
            out->flash = 0.32f * expf(-t * 9.0f) * kHitTint[lvl] * keep;
    }

    // low HP: the player's active Pokemon (battler 0) under 20 percent
    {
        const bool low = gMain.inBattle && gBattleMons[0].maxHP > 0 && gBattleMons[0].hp > 0 && gBattleMons[0].hp * 5u < gBattleMons[0].maxHP;
        const float pulse = 0.5f + 0.5f * sinf(seconds * 2.6f);

        sLowSmooth += ((low ? 1.0f : 0.0f) - sLowSmooth) * 0.05f;
        out->low = sLowSmooth * (0.35f + 0.65f * pulse) * kLowHp[lvl] * keep;
    }
    return true;
}

void DeckBattleFx_DrawBackdrop(float seconds, float fade)
{
    const int lvl = Level();
    const float keep = 1.0f - Clamp01(fade);
    const u16 weather = gBattleWeather;
    const bool rain = (weather & B_WEATHER_RAIN) != 0, sand = (weather & B_WEATHER_SANDSTORM) != 0;
    const bool hail = (weather & B_WEATHER_HAIL) != 0, sun = (weather & B_WEATHER_SUN) != 0;

    if (!sReady || EnvOff() || lvl < 0 || keep < 0.01f)
        return;

    DeckGl_UseProgram(&sProg);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // spotlights on the platforms (the ground shadow ovals are the game's own and stay): a warm soft light on every battler's standing place
    {
        VoxelBattleShadow sh[4];
        const uint32_t n = VoxelBattle_Shadows(sh, 4);

        for (uint32_t i = 0; i < n; i++)
        {
            const float cx = (72.0f + sh[i].x * 3.4f) / 960.0f, cy = (sh[i].y - 2.0f) / 160.0f;
            const float c[4] = { 1.0f, 0.88f, 0.62f, 0.20f * kSpot[lvl] * keep };

            Quad(cx, cy, 0.20f, 0.075f, 0.0f, c);
        }
        Flush(0, true);
    }

    // hit glow: a big soft disc in the move type's colour behind the target (the battler's sprite is drawn over it, untinted)
    {
        const float t = seconds - sHitStart;

        if (t >= 0.0f && t < 1.0f)
        {
            const float env = expf(-t * 4.5f);
            const float c[4] = { sHitTint[0], sHitTint[1], sHitTint[2], 0.55f * env * kHitTint[lvl] * keep };

            Quad(sHitPos[0], sHitPos[1], 0.20f + 0.18f * t, 0.20f + 0.18f * t, 0.0f, c);
            Flush(0, true);
        }
    }

    // floating light motes: slow lazy paths, warm gold (or the weather's colour)
    {
        const int n = (int)((float)N_MOTES * kMotes[lvl]);
        const bool cold = rain || hail;

        for (int i = 0; i < n; i++)
        {
            const float r0 = Hash((float)i * 12.9898f), r1 = Hash((float)i * 78.233f), r2 = Hash((float)i * 37.719f);
            const float x = r0 + 0.04f * sinf(seconds * (0.21f + 0.2f * r1) + r2 * 6.28f) + seconds * (0.004f + 0.006f * r1);
            const float y = 1.0f - fmodf(r1 + seconds * (0.012f + 0.02f * r2), 1.0f);
            const float tw = 0.5f + 0.5f * sinf(seconds * (0.6f + 0.9f * r2) + r0 * 20.0f);
            const float c[4] = { cold ? 0.75f : 1.0f, cold ? 0.88f : 0.80f, cold ? 1.0f : 0.45f, (0.15f + 0.45f * tw) * keep };
            const float e = 0.5f - fabsf(y - 0.5f);

            Quad(fmodf(x, 1.0f), y, 0.010f + 0.010f * r2, 0.010f + 0.010f * r2, 0.0f, c);
            (void)e;
        }
        Flush(0, true);
    }

    // battle weather over the backdrop (the weather of the battle itself, gBattleWeather; the overworld weather code is not reused: it
    // is a 3D world pass, this is a screen space pass behind the battlers)
    if (rain)
    {
        const int n = (int)((float)N_RAIN * kWeather[lvl]);

        for (int i = 0; i < n; i++)
        {
            const float r0 = Hash((float)i * 3.17f), r1 = Hash((float)i * 9.71f);
            const float y = fmodf(r1 + seconds * (1.5f + 0.5f * r0), 1.0f) * 1.2f - 0.1f;
            const float x = fmodf(r0 - (y * 0.25f), 1.0f);
            const float c[4] = { 0.70f, 0.80f, 1.0f, 0.22f * keep };

            Quad(x < 0.0f ? x + 1.0f : x, y, 0.028f, 0.0025f, 1.75f, c);
        }
        Flush(1, true);
    }
    if (sand)
    {
        const int n = (int)((float)N_SAND * kWeather[lvl]);
        const float haze[4] = { 0.85f, 0.65f, 0.35f, 0.06f * keep };

        Quad(0.5f, 0.5f, 1.2f, 0.9f, 0.0f, haze);
        Flush(0, true);
        for (int i = 0; i < n; i++)
        {
            const float r0 = Hash((float)i * 4.31f), r1 = Hash((float)i * 6.11f);
            const float x = fmodf(r0 + seconds * (0.35f + 0.3f * r1), 1.0f);
            const float c[4] = { 0.92f, 0.76f, 0.46f, 0.20f * keep };

            Quad(x, r1, 0.06f, 0.003f, 0.05f * sinf(seconds + r0 * 6.0f), c);
        }
        Flush(1, true);
    }
    if (hail)
    {
        const int n = (int)((float)N_HAIL * kWeather[lvl]);

        for (int i = 0; i < n; i++)
        {
            const float r0 = Hash((float)i * 5.77f), r1 = Hash((float)i * 2.39f);
            const float y = fmodf(r1 + seconds * (0.7f + 0.3f * r0), 1.0f);
            const float x = fmodf(r0 + 0.02f * sinf(seconds * 1.3f + r1 * 9.0f), 1.0f);
            const float c[4] = { 0.92f, 0.97f, 1.0f, 0.65f * keep };

            Quad(x, y, 0.006f, 0.006f, 0.0f, c);
        }
        Flush(2, false);
    }
    if (sun)
    {
        const float c[4] = { 1.0f, 0.82f, 0.45f, (0.30f + 0.05f * sinf(seconds * 0.7f)) * kWeather[lvl] * keep };

        Quad(0.88f, 0.04f, 0.55f, 0.55f, 0.0f, c);
        Flush(0, true);
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    DeckGl_UseProgram(NULL);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

// ---- battle scenery (voxel_battlescene; docs/deck_notes_parts/battlescene.md) ----
static const float kSceneK[3] = { 0.4f, 0.7f, 1.0f };   // LOW MED HIGH: strength of the scenery effects
static const int kLeafCount[3] = { 3, 5, 8 };           // foreground bokeh shapes
#define SCENE_GLOW 0.14f                                // type glow strength at HIGH (additive alpha); pulses +-15 percent
#define SCENE_LEAF_ALPHA 0.35f                          // foreground shape alpha cap (HIGH)

int DeckBattleFx_SceneLevel(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 ? DeckLevel(FAST_EXTRA_VOXEL_BATTLESCENE) : -1;
}

float DeckBattleFx_SceneK(void)
{
    const int l = DeckBattleFx_SceneLevel();

    return l < 0 ? 0.0f : kSceneK[l];
}

bool DeckBattleFx_SceneDebug(void)
{
    static int s = -1;

    if (s < 0)
        s = getenv("DECK_BATTLESCENE_DEBUG") != NULL ? 1 : 0;
    return s == 1;
}

// After DeckBattleFx_DrawBackdrop, before Pass_Overlay2D: the opponent's type glow and the foreground bokeh.
void DeckBattleFx_DrawScene(float seconds, float fade)
{
    static float sSceneLog = -100.0f;
    const int lvl = DeckBattleFx_SceneLevel();
    const float keep = 1.0f - Clamp01(fade);
    const bool log = DeckBattleFx_SceneDebug() && seconds - sSceneLog >= 5.0f;
    float glowA = 0.0f;
    int glowType = -1, leaves = 0;

    if (!sReady || lvl < 0 || keep < 0.01f)
        return;
    if (log)
        sSceneLog = seconds;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    // ambient type glow: the opponent's primary type, a soft backlight and a faint tint on the ground, 6 s pulse, not during a hit flash
    if (gMain.inBattle && gBattleMons[B_POSITION_OPPONENT_LEFT].species != 0)
    {
        float ux, uy;
        const float since = seconds - sHitStart;

        if (BattlerUv(B_POSITION_OPPONENT_LEFT, &ux, &uy))
        {
            float rgb[3];
            const float pulse = 1.0f + 0.15f * sinf(seconds * (6.2831853f / 6.0f));
            const float calm = since >= 1.2f ? 1.0f : (since > 0.6f ? (since - 0.6f) / 0.6f : 0.0f); // fades back in after the hit flash

            glowType = gBattleMons[B_POSITION_OPPONENT_LEFT].types[0];
            TypeTint(glowType, rgb);
            glowA = SCENE_GLOW * kSceneK[lvl] * pulse * calm * keep;
            if (glowA > 0.001f)
            {
                const float back[4] = { rgb[0], rgb[1], rgb[2], glowA };
                const float ground[4] = { rgb[0], rgb[1], rgb[2], glowA * 0.8f };

                DeckGl_UseProgram(&sProg);
                glEnableClientState(GL_COLOR_ARRAY);
                Quad(ux, uy - 0.02f, 0.26f, 0.26f, 0.0f, back);
                Flush(0, true);
                Quad(ux, uy + 0.09f, 0.20f, 0.05f, 0.0f, ground);
                Flush(0, true);
                glDisableClientState(GL_COLOR_ARRAY);
            }
        }
    }

    // foreground bokeh: slow soft leaves and discs at the screen edges, green and yellow by day, cool and dark at night
    if (sFg.program != 0)
    {
        static const float quad[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
        const VitaDayNightLight *const dn = VitaDayNight_Get();
        const float day = dn != NULL ? Clamp01(dn->daylight / 0.6f) : 1.0f;
        const float dusk = dn != NULL ? Clamp01(dn->glow * 3.0f) : 0.0f;
        const float c1[3] = { 0.06f + (0.30f - 0.06f) * day + 0.25f * dusk * day, 0.10f + (0.58f - 0.10f) * day, 0.15f + (0.16f - 0.15f) * day };
        const float c2[3] = { 0.10f + (0.78f - 0.10f) * day, 0.14f + (0.70f - 0.14f) * day, 0.20f + (0.20f - 0.20f) * day };

        leaves = kLeafCount[lvl];
        DeckGl_UseProgram(&sFg);
        DeckGl_Set1f(sFgTime, seconds);
        DeckGl_Set1f(sFgN, (float)leaves);
        DeckGl_Set3f(sFgCol, c1[0], c1[1], c1[2]);
        DeckGl_Set3f(sFgCol2, c2[0], c2[1], c2[2]);
        DeckGl_Set1f(sFgAlpha, SCENE_LEAF_ALPHA * keep);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisableClientState(GL_COLOR_ARRAY);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glVertexPointer(2, GL_FLOAT, 0, quad);
        glTexCoordPointer(2, GL_FLOAT, 0, quad);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    if (log)
        DeckGl_Log("[BATTLESCENE] t=%.0f level %d: leaves %d (alpha %.2f), type glow type %d alpha %.3f, keep %.2f\n", seconds, lvl, leaves,
                   SCENE_LEAF_ALPHA * keep, glowType, glowA, keep);

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    DeckGl_UseProgram(NULL);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}
