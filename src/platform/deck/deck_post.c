#include "deck_post.h"
#include "deck_gl_shader.h"
#include "deck_video.h"
#include "deck_levels.h"
#include "deck_sunshadow.h"
#include "platform/vita_extras.h"
#include "../vita/vita_perf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Presets (Decision 2: Medium "faithful plus", High stronger). Starting values, tuned in Task 5.
typedef struct
{
    float grade;       // strength of the time-of-day tint
    float saturation;  // 1.0 = unchanged
    float vignette;    // darkening at the corners
    float bloomGain;   // composite bloom = light.bloom * bloomGain (0 = no bloom in this preset)
    float threshold;   // bright pass luminance threshold
    int bloomPasses;   // separable blur iterations at half resolution
} DeckPostPreset;

// Bloom gain by level (the old preset values, but LOW now glows a little instead of nothing).
static const float kBloomGain[3] = { 1.5f, 3.0f, 5.0f };
// Share of the diorama blur at the band edges by level (MEDIUM and HIGH are the old look).
static const float kBlurMul[3] = { 0.5f, 1.0f, 1.0f };
// Light shaft gain by level (times the frame's sun amount).
static const float kShaftGain[3] = { 0.125f, 0.235f, 0.325f };
// Beam layers by level (item 5), and the colours: warm day, golden dawn and dusk, pale blue moon.
static const float kShaftLayers[3] = { 3.0f, 4.0f, 6.0f };
static const float kShaftDay[3] = { 1.0f, 0.92f, 0.74f };
static const float kShaftGold[3] = { 1.0f, 0.72f, 0.38f };
static const float kShaftMoon[3] = { 0.52f, 0.66f, 1.0f };
// LUT: gentle S-curve strength by time of day (day, dusk, night) and the level factor.
static const float kLutCurve[3] = { 0.20f, 0.32f, 0.14f };
static const float kLutLevel[3] = { 0.5f, 1.0f, 1.3f };
// Tilt-shift by level: largest gather radius (fraction of the picture height at full circle of confusion) and taps.
static const float kTiltRadius[3] = { 0.6f * 0.014f, 0.014f, 1.4f * 0.014f };
static const float kTiltTaps[3] = { 8.0f, 12.0f, 16.0f };
// Ambient occlusion (voxel_ao) by level: gather radius (world units, 1 tile = 1), slice directions, steps per side. Thickness and fade are in kAo*.
static const float kAoRadius[3] = { 0.88f, 0.88f, 1.05f };
static const float kAoDirs[3] = { 2.0f, 3.0f, 3.0f };
static const float kAoSteps[3] = { 4.0f, 5.0f, 6.0f };
// (thickness default 0.60 in sAoThick) samples further than this in view depth from the pixel do not occlude; samples further than this in view depth from the pixel do not occlude (thin cutouts: crowns, grass, signs)
// (intensity default 1.0 in sAoInt, power 2.6, radius x1.5: the stronger look) final ao = mix(1, ao, intensity)
static float sAoRadMul = 1.5f, sAoThick = 1.60f, sAoInt = 1.0f, sAoPow = 2.6f; // debug aids: env DECK_AO_RADIUS (multiplier), DECK_AO_THICK, DECK_AO_INTENSITY, DECK_AO_POW (read once in DeckPost_Init)
static int sAoDirsOv = 0, sAoStepsOv = 0, sAoBlurOv = -1;        // env DECK_AO_DIRS (1..3), DECK_AO_STEPS (1..6), DECK_AO_BLUR (0 = none, 1 = H+V)
static const float kAoFadeStart = 30.0f, kAoFadeEnd = 55.0f; // the effect fades to nothing with view distance (zFar 62)
// Cloud shadows (voxel_clouds) by level: darkest multiply at full daylight (notes in docs/deck_notes_parts/clouds.md).
static const float kCloudMax[3] = { 0.10f, 0.15f, 0.22f };
static float sCloudStr = 1.0f; // debug aid: env DECK_CLOUDS_STRENGTH (multiplier on the level), DECK_CLOUDS_DEBUG=1 shows the mask grey
static int sCloudDbg = 0;
// 3D battle backdrop (voxel_battledim) by level: blur passes at half resolution, their step, and the black overlay opacity.
static const int kBackPasses[3] = { 1, 2, 3 };
static const float kBackStep[3] = { 1.0f, 1.0f, 1.6f };
static const float kBackDim[3] = { 0.25f, 0.40f, 0.55f };
// Battle scenery (voxel_battlescene), per level LOW MED HIGH: see docs/deck_notes_parts/battlescene.md
static const float kSceneK[3] = { 0.4f, 0.7f, 1.0f };            // strength of every scenery effect
static const float kSceneDim[3] = { 0.05f, 0.085f, 0.12f };      // overlay opacity (the dim, replaces kBackDim; scaled down by the battledim level)
static const float kSceneBloom = 0.70f;                          // bloom gain multiplier of the field
static const float kSceneShaft = 0.60f;                          // shaft gain multiplier of the field
static const float kSceneTiltRadius = 0.75f;                     // gentler than the field
static const float kSceneBandA = 0.18f, kSceneBandB = 0.75f;      // focus band: relative depth error where blur starts / is full (field 0.06 / 0.40)

static const DeckPostPreset kPresets[3] = {
    /* low    */ { 0.60f, 1.04f, 0.12f, 0.0f, 0.70f, 1 },
    /* medium */ { 0.80f, 1.06f, 0.18f, 3.0f, 0.65f, 1 },
    /* high   */ { 1.00f, 1.12f, 0.30f, 5.0f, 0.58f, 2 },
};

// Tints per time of day: shadows (cool by day, purple at dusk, deep blue at night) and highlights.
static const float kShadowTint[3][3] = { { 0.94f, 0.97f, 1.06f }, { 1.02f, 0.90f, 1.08f }, { 0.86f, 0.92f, 1.12f } };
static const float kHighTint[3][3]   = { { 1.04f, 1.01f, 0.95f }, { 1.10f, 0.97f, 0.84f }, { 0.97f, 1.00f, 1.05f } };

// ---- Shaders (GLSL 1.20). Quads are drawn in [0,1]^2 with gl_Vertex; vPos is the screen
// position, y up, which is also the uv of every target (all targets cover the full frame).
static const char kFullscreenVs[] =
    "#version 120\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vPos = gl_Vertex.xy;\n"
    "    gl_Position = vec4(gl_Vertex.xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char kBrightFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform float uThreshold;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos).rgb;\n"
    "    float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    gl_FragColor = vec4(c * smoothstep(uThreshold, 1.0, l), 1.0);\n"
    "}\n";

// The half resolution copy (tilt-shift and blur source) is a 2x2 box filter: four taps at the centres of the four scene texels under
// each half-res pixel (uTexel = 0.5 / scene size). A single tap point-sampled every other row and column of the scene, so the fine
// horizontal structure of the building art (and the relief on it) aliased into stripes after the blur: the banding on houses.
static const char kCopyFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform vec2 uTexel;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos + vec2(-uTexel.x, -uTexel.y)).rgb;\n"
    "    c += texture2D(uScene, vPos + vec2(uTexel.x, -uTexel.y)).rgb;\n"
    "    c += texture2D(uScene, vPos + vec2(-uTexel.x, uTexel.y)).rgb;\n"
    "    c += texture2D(uScene, vPos + vec2(uTexel.x, uTexel.y)).rgb;\n"
    "    gl_FragColor = vec4(c * 0.25, 1.0);\n"
    "}\n";

// 9-tap Gaussian folded into 5 bilinear taps (offsets between texels, GL_LINEAR source).
static const char kBlurFs[] =
    "#version 120\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uStep;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uSrc, vPos).rgb * 0.2270270270;\n"
    "    c += texture2D(uSrc, vPos + uStep * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture2D(uSrc, vPos - uStep * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture2D(uSrc, vPos + uStep * 3.2307692308).rgb * 0.0702702703;\n"
    "    c += texture2D(uSrc, vPos - uStep * 3.2307692308).rgb * 0.0702702703;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

// Ambient occlusion (voxel_ao): a port of the GTAO pass of three.js (examples/jsm/shaders/GTAOShader.js, MIT, Copyright 2010-2026 three.js
// authors; Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect Occlusion") to GLSL 1.20. Differences: no matrices (the view
// position comes from the linear depth, tan of half the field of view and the aspect), the normal is rebuilt from the depth like its
// NORMAL_VECTOR_TYPE 0, per pixel rotation by interleaved gradient noise instead of a noise texture, loop bounds are uniforms under constant maxima.
// Runs at half resolution and reads the full-resolution scene depth at the lower left texel of each 2x2 block (pc = vPos - 0.25 uTexel is that texel's
// centre, so the +-1 and +-2 half-pixel offsets of the normal also land on texel centres). Writes the occlusion (1 = open) to r.
static const char kAoFs[] =
    "#version 120\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2 uZ;\n"          // near, far
    "uniform vec2 uProj;\n"       // tan of half the vertical fov, aspect
    "uniform vec2 uTexel;\n"      // texel of this half resolution target (uv)
    "uniform vec4 uP;\n"          // radius, thickness, fade start, fade end (world units)
    "uniform vec2 uQ;\n"          // slice directions (max 3), steps per side (max 6)
    "uniform float uPow;\n"       // exponent on the occlusion
    "varying vec2 vPos;\n"
    "float linZ(float d) { return 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - (2.0 * d - 1.0) * (uZ.y - uZ.x)); }\n"
    "vec3 viewPos(vec2 uv, float d) { return vec3((uv * 2.0 - 1.0) * vec2(uProj.y * uProj.x, uProj.x) * linZ(d), -linZ(d)); }\n"
    "float dep(vec2 uv) { return texture2D(uDepth, uv).r; }\n"
    "vec3 normalAt(vec2 uv, float d0, vec3 ce) {\n"
    "    vec2 dx = vec2(uTexel.x, 0.0);\n"
    "    vec2 dy = vec2(0.0, uTexel.y);\n"
    "    float l1 = dep(uv - dx), l2 = dep(uv - 2.0 * dx), r1 = dep(uv + dx), r2 = dep(uv + 2.0 * dx);\n"
    "    float b1 = dep(uv - dy), b2 = dep(uv - 2.0 * dy), t1 = dep(uv + dy), t2 = dep(uv + 2.0 * dy);\n"
    "    float dl = abs((2.0 * l1 - l2) - d0);\n"
    "    float dr = abs((2.0 * r1 - r2) - d0);\n"
    "    float db = abs((2.0 * b1 - b2) - d0);\n"
    "    float dt = abs((2.0 * t1 - t2) - d0);\n"
    "    vec3 px = (dl < dr) ? ce - viewPos(uv - dx, l1) : viewPos(uv + dx, r1) - ce;\n"
    "    vec3 py = (db < dt) ? ce - viewPos(uv - dy, b1) : viewPos(uv + dy, t1) - ce;\n"
    "    vec3 n = cross(px, py);\n"
    "    float nl = length(n);\n"
    "    return nl > 1.0e-9 ? n / nl : vec3(0.0, 0.0, 1.0);\n"
    "}\n"
    // The horizon cosine of one sample, or -2 when it does not count (off screen, or further than the thickness in view depth).
    "float horizon(vec3 q, vec3 P, vec3 V, out float wt) {\n"
    "    wt = 0.0;\n"
    "    vec2 su = vec2(q.x / (-q.z) / (uProj.y * uProj.x), q.y / (-q.z) / uProj.x) * 0.5 + 0.5;\n"
    "    if (su.x < 0.0 || su.x > 1.0 || su.y < 0.0 || su.y > 1.0) return -2.0;\n"
    "    vec3 dv = viewPos(su, dep(su)) - P;\n"
    "    float dl = length(dv);\n"
    "    float az = dv.z > 0.0 ? dv.z * 2.5 : -dv.z;\n"                      // an occluder nearer to the eye than the pixel (a floating crown, a card over the ground behind it) counts less
    "    wt = 1.0 - smoothstep(0.55 * uP.y, uP.y, az);\n"
    "    if (wt <= 0.0 || dl < 1.0e-6) return -2.0;\n"
    "    return dot(V, dv / dl);\n"
    "}\n"
    "void main() {\n"
    "    vec2 pc = vPos - 0.25 * uTexel;\n"
    "    float d = dep(pc);\n"
    "    if (d >= 0.9999) { gl_FragColor = vec4(1.0); return; }\n"            // sky
    "    vec3 P = viewPos(pc, d);\n"
    "    if (-P.z > uP.w) { gl_FragColor = vec4(1.0); return; }\n"
    "    vec3 N = normalAt(pc, d, P);\n"
    "    vec3 V = normalize(-P);\n"
    "    float n1 = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "    float n2 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "    float ao = 0.0;\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "        if (float(i) >= uQ.x) break;\n"
    "        float ang = (float(i) + n1) / uQ.x * 3.14159265;\n"
    "        vec3 sd = vec3(cos(ang), sin(ang), 0.0);\n"
    "        float rr = uP.x * (0.5 + 0.5 * n2);\n"
    "        vec3 sb = normalize(cross(sd, V));\n"
    "        vec3 st = cross(sb, V);\n"
    "        vec3 nis = N - sb * dot(N, sb);\n"
    "        float nil = length(nis);\n"
    "        nis = nil > 1.0e-5 ? nis / nil : N;\n"
    "        vec3 tn = cross(nis, sb);\n"
    "        vec2 ch = vec2(dot(V, tn), dot(V, -tn));\n"
    "        for (int j = 0; j < 6; j++) {\n"
    "            if (float(j) >= uQ.y) break;\n"
    "            vec3 off = sd * (rr * float(j + 1) / uQ.y);\n"
    "            float fo = 2.0 / float(j + 2);\n"                                // distanceFallOff on
    "            float wt;\n"
    "            float h = horizon(P + off, P, V, wt);\n"
    "            if (h > -1.5) ch.x += max(0.0, (h - ch.x) * fo * wt);\n"
    "            h = horizon(P - off, P, V, wt);\n"
    "            if (h > -1.5) ch.y += max(0.0, (h - ch.y) * fo * wt);\n"
    "        }\n"
    "        ch = clamp(ch, -1.0, 1.0);\n"
    "        vec2 sh = sqrt(max(1.0 - ch * ch, 0.0));\n"
    "        float nx = dot(nis, st);\n"
    "        float ny = dot(nis, V);\n"
    "        float nxb = 0.5 * (acos(ch.y) - acos(ch.x) + sh.x * ch.x - sh.y * ch.y);\n"
    "        float nyb = 0.5 * (2.0 - ch.x * ch.x - ch.y * ch.y);\n"
    "        ao += nx * nxb + ny * nyb;\n"
    "    }\n"
    "    ao = clamp(ao / uQ.x, 0.0, 1.0);\n"
    "    ao = pow(ao, uPow);\n"
    "    ao = mix(ao, 1.0, smoothstep(uP.z, uP.w, -P.z));\n"
    "    gl_FragColor = vec4(vec3(ao), 1.0);\n"
    "}\n";

// Occlusion blur at half resolution: 5 taps (1 4 6 4 1) along uStep, each weighted by the depth likeness to the centre (relative view depth difference),
// so the occlusion does not bleed across silhouettes. Depth is read at the same lower left texel as the gather (uBias = 0.25 uTexel).
static const char kAoBlurFs[] =
    "#version 120\n"
    "uniform sampler2D uSrc;\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2 uStep;\n"
    "uniform vec2 uBias;\n"
    "uniform vec2 uZ;\n"
    "varying vec2 vPos;\n"
    "float linZ(float d) { return 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - (2.0 * d - 1.0) * (uZ.y - uZ.x)); }\n"
    "void main() {\n"
    "    float zc = linZ(texture2D(uDepth, vPos - uBias).r);\n"
    "    float acc = texture2D(uSrc, vPos).r * 6.0;\n"
    "    float ws = 6.0;\n"
    "    for (int i = -2; i <= 2; i++) {\n"
    "        if (i == 0) continue;\n"
    "        vec2 uv = clamp(vPos + uStep * float(i), uBias * 2.0, 1.0 - uBias * 2.0);\n"
    "        float w = (i == -1 || i == 1) ? 4.0 : 1.0;\n"
    "        w *= 1.0 - smoothstep(0.02, 0.08, abs(linZ(texture2D(uDepth, uv - uBias).r) - zc) / zc);\n"
    "        acc += texture2D(uSrc, uv).r * w;\n"
    "        ws += w;\n"
    "    }\n"
    "    gl_FragColor = vec4(vec3(acc / ws), 1.0);\n"
    "}\n";

// Light shafts (item 5): procedural beams at half resolution, not a march over the scene. Layers of slanted bands along the
// light direction (aspect corrected, the lines run along uDir = the projected sun or moon direction), each with its own density, drift speed and breathing,
// strongest at the top of the picture, with dust sparkles inside the beams. uCol carries the colour (warm by day, golden at
// dawn and dusk, pale blue for the moon); uMotion is 1 for the sun and about 0.45 for the slower moon.
static const char kShaftFs[] =
    "#version 120\n"
    "uniform vec2 uDir;\n"         // unit screen direction toward the light (x right, y up), from the real camera
    "uniform float uLen;\n"        // falloff exponent: small (long beams) for a low sun
    "uniform float uTime;\n"
    "uniform float uAsp;\n"
    "uniform float uCount;\n"      // layers (3, 4 or 6)
    "uniform float uMotion;\n"
    "uniform vec3 uCol;\n"
    "varying vec2 vPos;\n"
    "float hh(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "void main() {\n"
    "    vec2 dir = normalize(uDir);\n"
    "    float a = dot(vec2(vPos.x * uAsp, vPos.y), vec2(dir.y, -dir.x));\n"   // across the beams (x for vertical ones)
    "    float t = uTime * uMotion;\n"
    "    float acc = 0.0;\n"
    "    for (int i = 0; i < 6; i++) {\n"
    "        if (float(i) >= uCount) break;\n"
    "        float fi = float(i);\n"
    "        float dir = mod(fi, 2.0) < 0.5 ? 1.0 : -1.0;\n"
    "        float pos = a * (1.1 + fi * 0.55) + fi * 13.1 + t * dir * (0.020 + 0.013 * fi);\n"
    "        float cell = floor(pos);\n"
    "        float fr = pos - cell;\n"
    "        float h1 = hh(vec2(cell, fi));\n"
    "        float h2 = hh(vec2(cell + 7.0, fi * 3.0));\n"
    "        float width = 0.05 + 0.13 * h2;\n"
    "        float prof = step(0.40, h1) * (1.0 - smoothstep(0.0, width, abs(fr - (0.25 + 0.5 * h1))));\n"
    "        float breath = 0.55 + 0.45 * sin(t * (0.30 + 0.19 * fi) + h1 * 6.2831);\n"    // slow intensity breathing
    "        acc += prof * breath * (1.0 - 0.07 * fi);\n"
    "    }\n"
    "    float fall = pow(clamp(vPos.y, 0.0, 1.0), uLen);\n"                                // strong at the top, gone at the bottom
    "    float beam = clamp(acc * 0.55, 0.0, 1.0) * fall;\n"
    "    float dust = step(0.955, hh(floor(vPos * vec2(190.0, 108.0)) + floor(t * 2.5))) * beam * 1.6;\n"  // shimmering motes
    "    gl_FragColor = vec4(uCol * (beam + dust), 1.0);\n"
    "}\n";

// Composite body. Built twice (BuildPrograms): with POST_X defined (tilt-shift and the other phase 7 features)
// and, if that fails to compile, without it (the phase 1 composite).
// uDof: x top band height, y bottom band height (fractions of the frame), z/w blur at the edges.
// Same band shape as Pass_DepthOfField (linear ramp from the edge alpha to 0), smooth blur.
static const char kCompositeBody[] =
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uBloom;\n"
    "uniform sampler2D uBlur;\n"
    "uniform float uBloomAmt;\n"
    "uniform float uBlurOn;\n"
    "uniform vec4 uDof;\n"
    "uniform float uGradeAmt;\n"
    "uniform vec3 uShadowTint;\n"
    "uniform vec3 uHighTint;\n"
    "uniform float uSaturation;\n"
    "uniform float uVignette;\n"
    "uniform float uAspect;\n"
    "uniform vec2 uBack;   // battle backdrop: x blur amount, y black overlay opacity\n"
    "uniform vec4 uBF0;    // battle fx: x hit tint, y aberration (uv), z crit flash, w low HP\n"
    "uniform vec4 uBF1;    // rgb hit tint colour, w entry\n"
    "uniform vec4 uBF2;    // xy hit centre (uv, y up), z radius (height units)\n"
    "uniform vec4 uBF3;    // xy drift and shake offset (uv), z zoom\n"
    "#ifdef POST_X\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec4 uTilt;   // x on, y focus distance, z max radius (fraction of the height), w taps\n"
    "uniform vec2 uZ;      // near, far\n"
    "uniform vec2 uTiltB;  // focus band: relative depth error where the blur starts and is full\n"
    "uniform sampler2D uShafts;\n"
    "uniform sampler2D uLut;\n"
    "uniform vec2 uLutOn;  // x on, y strength (the fade)\n"
    "uniform vec4 uShaft;  // x amount\n"
    "uniform sampler2D uAoTex;\n"
    "uniform mat4 uCamInv; // camera clip -> world (cloud shadows)\n"
    "uniform float uWinRays; // lit window light rays amount (0 = off)\n"
    "uniform float uWinSkew; // sideways lean of the rays per row, from the sun/moon screen direction\n"
    "uniform vec4 uCld;    // x strength (level x daylight x fade, 0 = off), y 1 = show the mask grey (DECK_CLOUDS_DEBUG), z seconds\n"
    "float cldH(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "float cldN(vec2 p) {\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(cldH(i), cldH(i + vec2(1.0, 0.0)), f.x), mix(cldH(i + vec2(0.0, 1.0)), cldH(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "uniform vec4 uAO;     // x strength (intensity x fade, 0 = off), y 1 = show the raw occlusion (DECK_AO_DEBUG), zw texel of the half resolution occlusion (uv)\n"
    "float aoLinZ(float d) { return 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - (2.0 * d - 1.0) * (uZ.y - uZ.x)); }\n"
    "#endif\n"
    "#ifdef POST_SUN\n"
    "uniform sampler2D uSunMap;\n"
    "uniform mat4 uInvVP;   // camera clip -> world\n"
    "uniform mat4 uSunVP;   // world -> sun clip\n"
    "uniform vec4 uSunA;    // x on (sun amount), y texel (uv), z PCF radius (0: 2x2, 1: 3x3, 2: 5x5), w multiplier in full shadow\n"
    "uniform vec4 uSunB;    // x depth range (world), y texel (world), z tap spacing (texels)\n"
    "uniform vec3 uSunL;    // toward the sun\n"
    "#endif\n"
    "varying vec2 vPos;\n"
    "#ifdef POST_X\n"
    "float tiltCoc(vec2 uv) {\n"
    "    float d = texture2D(uDepth, clamp(uv, 0.0, 1.0)).r;\n"
    "    float z = 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - (2.0 * d - 1.0) * (uZ.y - uZ.x));\n"
    "    float rel = (z - uTilt.y) / uTilt.y;\n"
    "    rel = rel < 0.0 ? -rel * 1.4 : rel;\n"
    "    return d >= 0.9999 ? 0.0 : smoothstep(uTiltB.x, uTiltB.y, rel);\n"   // sky (cleared depth) stays sharp
    "}\n"
    "#endif\n"
    "void main() {\n"
    // POST_SHADEONLY: the pre-pass. Sharp scene at this pixel (no battle offset), only the shadow terms, written out; the composite then blurs the shaded picture.
    "#ifdef POST_SHADEONLY\n"
    "    vec2 sp = vPos;\n"
    "#else\n"
    "    vec2 sp = clamp(vec2(0.5) + (vPos - 0.5) / uBF3.z + uBF3.xy, 0.0, 1.0);\n"
    "#endif\n"
    "    vec3 c = texture2D(uScene, sp).rgb;\n"
    "#if defined(POST_X) && !defined(POST_SHADEONLY)\n"
    "    if (uTilt.x > 0.5) {\n"
    // Upright cards (characters, the truck) lean toward the camera: their upper part reads nearer than their feet and would blur while the
    // feet stay sharp. The focus test is the sharper of this pixel and three taps further down the screen, so a card is judged by its feet.
    "        float coc = tiltCoc(vPos);\n"
    "        coc = min(coc, tiltCoc(vPos + vec2(0.0, 0.010)));\n"
    "        coc = min(coc, tiltCoc(vPos + vec2(0.0, 0.020)));\n"
    "        coc = min(coc, tiltCoc(vPos + vec2(0.0, 0.030)));\n"
    "        if (coc > 0.01) {\n"
    "            vec2 rad = vec2(1.0 / uAspect, 1.0) * (coc * uTilt.z);\n"
    "            vec3 acc = vec3(0.0);\n"
    // The same 8 to 16 tap pattern at every pixel undersampled the 1 pixel horizontal lines of the building art into stripes (the banding on
    // far houses); each pixel now rotates the pattern by interleaved gradient noise, so the error is fine grain instead of lines.
    "            float wsum = 0.0;\n"
    "            float ign = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "            for (int i = 0; i < 16; i++) {\n"
    "                if (float(i) >= uTilt.w) break;\n"
    "                float fi = float(i) + 0.5;\n"
    "                float a = fi * 2.39996323 + ign;\n"
    "                vec3 s = texture2D(uBlur, vPos + vec2(cos(a), sin(a)) * sqrt(fi / uTilt.w) * rad).rgb;\n"
    "                float l = dot(s, vec3(0.299, 0.587, 0.114));\n"
    "                float w = 1.0 + 3.0 * l * l;\n"
    "                acc += s * w;\n"
    "                wsum += w;\n"
    "            }\n"
    "            c = mix(c, acc / wsum, coc);\n"
    "        }\n"
    "    }\n"
    "#endif\n"
    "#ifdef POST_SUN\n"
    "    if (uSunA.x > 0.0) {\n"
    "        float sd = texture2D(uDepth, vPos).r;\n"
    "        vec4 wq = uInvVP * vec4(vPos * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);\n"
    "        vec3 wp = wq.xyz / wq.w;\n"
    // The normal from taps 3 pixels apart on each side (6 pixel baseline), not the 1 pixel derivatives of the reconstructed position: those were
    // noisy at grazing angles (depth quantisation) and flipped from row to row on far walls, which put the stripes on far houses.
    "        vec2 pxs = vec2(abs(dFdx(vPos.x)), abs(dFdy(vPos.y))) * 3.0;\n"
    "        vec4 qa = uInvVP * vec4((vPos + vec2(pxs.x, 0.0)) * 2.0 - 1.0, texture2D(uDepth, vPos + vec2(pxs.x, 0.0)).r * 2.0 - 1.0, 1.0);\n"
    "        vec4 qb = uInvVP * vec4((vPos - vec2(pxs.x, 0.0)) * 2.0 - 1.0, texture2D(uDepth, vPos - vec2(pxs.x, 0.0)).r * 2.0 - 1.0, 1.0);\n"
    "        vec4 qc = uInvVP * vec4((vPos + vec2(0.0, pxs.y)) * 2.0 - 1.0, texture2D(uDepth, vPos + vec2(0.0, pxs.y)).r * 2.0 - 1.0, 1.0);\n"
    "        vec4 qd = uInvVP * vec4((vPos - vec2(0.0, pxs.y)) * 2.0 - 1.0, texture2D(uDepth, vPos - vec2(0.0, pxs.y)).r * 2.0 - 1.0, 1.0);\n"
    "        vec3 nn = cross(qa.xyz / qa.w - qb.xyz / qb.w, qc.xyz / qc.w - qd.xyz / qd.w);\n"
    "        float nl = length(nn);\n"
    "        nn = nl > 1.0e-12 ? nn / nl : vec3(0.0, 1.0, 0.0);\n"
    "        vec4 cq = uInvVP * vec4(0.0, 0.0, -1.0, 1.0);\n"          // a point on the near plane: the eye side
    "        if (dot(nn, wp - cq.xyz / cq.w) > 0.0) nn = -nn;\n"      // face the viewer whatever the derivative handedness
    "        float ndl = dot(nn, uSunL);\n"
    // Near vertical surfaces (house walls, cliffs) are skipped: their normal from the depth derivatives is too noisy at grazing angles and
    // the map self shadowed them in horizontal stripes (the banding on far houses; gone with voxel_sunshadow off). They keep their baked face shade.
    "        if (sd < 0.9999 && ndl > 0.05) {\n"
    "            float sinT = sqrt(max(1.0 - ndl * ndl, 0.0));\n"
    "            float tw = uSunB.y;\n"
    "            float bias = min(tw * (1.0 + 3.0 * sinT / max(ndl, 0.25)), tw * 10.0);\n"
    "            vec4 sc = uSunVP * vec4(wp + nn * (tw * 1.2 * sinT), 1.0);\n"
    "            vec2 suv = sc.xy * 0.5 + 0.5;\n"
    "            float sz = (sc.z * 0.5 + 0.5) * uSunB.x - bias;\n"
    "            float edge = min(min(suv.x, 1.0 - suv.x), min(suv.y, 1.0 - suv.y));\n"
    "            float acc = 0.0;\n"
    "            float cnt = 0.0;\n"
    "            if (edge > 0.0) {\n"
    "                if (uSunA.z < 0.5) {\n"
    "                    for (int j = 0; j < 2; j++) {\n"
    "                        for (int i = 0; i < 2; i++) {\n"
    "                            vec2 o = (vec2(float(i), float(j)) - 0.5) * uSunA.y * uSunB.z;\n"
    "                            acc += (sz > texture2D(uSunMap, suv + o).r * uSunB.x) ? 1.0 : 0.0;\n"
    "                            cnt += 1.0;\n"
    "                        }\n"
    "                    }\n"
    "                } else {\n"
    "                    for (int j = -2; j <= 2; j++) {\n"
    "                        for (int i = -2; i <= 2; i++) {\n"
    "                            if (abs(float(i)) > uSunA.z || abs(float(j)) > uSunA.z) continue;\n"
    "                            vec2 o = vec2(float(i), float(j)) * uSunA.y * uSunB.z;\n"
    "                            acc += (sz > texture2D(uSunMap, suv + o).r * uSunB.x) ? 1.0 : 0.0;\n"
    "                            cnt += 1.0;\n"
    "                        }\n"
    "                    }\n"
    "                }\n"
    "                c *= mix(1.0, uSunA.w, (acc / cnt) * smoothstep(0.0, 0.08, edge));\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "#endif\n"
    "#ifndef POST_SHADEONLY\n"
    "    if (uBlurOn > 0.5) {\n"
    "        float wTop = uDof.z * clamp((vPos.y - (1.0 - uDof.x)) / uDof.x, 0.0, 1.0);\n"
    "        float wBot = uDof.w * clamp((uDof.y - vPos.y) / uDof.y, 0.0, 1.0);\n"
    "        c = mix(c, texture2D(uBlur, vPos).rgb, max(wTop, wBot));\n"
    "    }\n"
    "#endif\n"
    "#ifdef POST_X\n"
    // Cloud shadows: world XZ from the depth, drifting value noise (blobs ~9 tiles, wind 0.15 tile/s toward +x,+z), a cool tinted multiply on everything but the sky.
    // World space, so it does not shimmer with the camera. Before the AO multiply, bloom and grade.
    "    if (uCld.x > 0.0) {\n"
    "        float cd = texture2D(uDepth, vPos).r;\n"
    "        if (cd < 0.9999) {\n"
    "            vec4 cw4 = uCamInv * vec4(vPos * 2.0 - 1.0, cd * 2.0 - 1.0, 1.0);\n"
    "            vec2 cq = cw4.xz / cw4.w / 9.0 + vec2(0.8, 0.6) * (0.15 / 9.0) * uCld.z;\n"
    "            float cn = 0.65 * cldN(cq) + 0.35 * cldN(cq * 2.3 + 17.0);\n"
    "            float cm = smoothstep(0.42, 0.66, cn);\n"
    "            float cf = 1.0 - smoothstep(30.0, 58.0, aoLinZ(cd));\n"
    "            if (uCld.y > 0.5) { gl_FragColor = vec4(vec3(1.0 - cm * cf), 1.0); return; }\n"
    "            float cs = uCld.x * cm * cf;\n"
    "            c *= vec3(1.0 - cs, 1.0 - cs * 0.88, 1.0 - cs * 0.70);\n"
    "        }\n"
    "    }\n"
    // Ambient occlusion: bilinear upsample of the half resolution map over the 4 nearest texels, each weighted by its depth likeness to this pixel
    // (the half resolution depth is the lower left texel of the 2x2 block, as in the gather). After the tilt-shift, the sun and the band blur,
    // whose sources do not carry it; before the bloom and the grade.
    "    if (uAO.x > 0.0) {\n"
    "        float ad = texture2D(uDepth, vPos).r;\n"
    "        float ao = 1.0;\n"
    "        if (ad < 0.9999) {\n"
    "            float zc = aoLinZ(ad);\n"
    "            vec2 hb = vPos / uAO.zw - 0.5;\n"
    "            vec2 i0 = floor(hb);\n"
    "            vec2 fr = hb - i0;\n"
    "            float s = 0.0;\n"
    "            float ws = 0.0;\n"
    "            for (int k = 0; k < 4; k++) {\n"
    "                float kx = mod(float(k), 2.0);\n"
    "                float ky = floor(float(k) * 0.5);\n"
    "                vec2 tc = clamp((i0 + vec2(kx, ky) + 0.5) * uAO.zw, 0.5 * uAO.zw, 1.0 - 0.5 * uAO.zw);\n"
    "                float bw = mix(1.0 - fr.x, fr.x, kx) * mix(1.0 - fr.y, fr.y, ky);\n"
    "                float tz = aoLinZ(texture2D(uDepth, tc - 0.25 * uAO.zw).r);\n"
    "                float w = bw * (0.002 + 1.0 - smoothstep(0.02, 0.08, abs(tz - zc) / zc));\n"
    "                s += texture2D(uAoTex, tc).r * w;\n"
    "                ws += w;\n"
    "            }\n"
    "            ao = s / ws;\n"
    "        }\n"
    "        c *= mix(1.0, ao, uAO.x);\n"
    "        if (uAO.y > 0.5) { gl_FragColor = vec4(vec3(ao), 1.0); return; }\n"
    "    }\n"
    "#ifdef POST_SHADEONLY\n"
    "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "    return;\n"
    "#endif\n"
    // Window light rays: 3 fan columns x 8 taps going up the screen from the pixel; a tap that hits a warm bright (lit window) texel adds light,
    // weaker with distance. The facades face the camera, so the light spills down the screen and widens. Added before the bloom, like it.
    "    if (uWinRays > 0.0) {\n"
    "        vec3 wr = vec3(0.0);\n"
    "        for (int col = -1; col <= 1; col++) {\n"
    "            for (int j = 1; j <= 12; j++) {\n"
    "                float fj = float(j);\n"
    "                vec2 tp = vec2(sp.x + (float(col) * 0.004 + uWinSkew * 0.0065) * fj / uAspect, sp.y + fj * 0.0065);\n"
    "                vec3 t = texture2D(uScene, tp).rgb;\n"
    "                vec3 t2 = texture2D(uScene, tp + vec2(0.009 / uAspect, 0.0)).rgb;\n"
    "                vec3 t3 = texture2D(uScene, tp - vec2(0.009 / uAspect, 0.0)).rgb;\n"
    // a lit window is pale yellow: red and green both high, blue clearly lower (roofs, skin and flowers fail the green or the blue test).
    // Both of two taps a window-width apart must pass, so tiny bright dots (fireflies) do not streak.
    "                float hit = smoothstep(0.50, 0.70, dot(t, vec3(0.299, 0.587, 0.114))) * smoothstep(0.05, 0.18, t.r - t.b)\n"
    "                          * smoothstep(0.50, 0.70, dot(t2, vec3(0.299, 0.587, 0.114))) * smoothstep(0.05, 0.18, t2.r - t2.b)\n"
    "                          * smoothstep(0.50, 0.70, dot(t3, vec3(0.299, 0.587, 0.114))) * smoothstep(0.05, 0.18, t3.r - t3.b);\n"
    "                float fw = 1.0 - fj / 13.0;\n"
    "                wr += vec3(hit * fw * fw);\n"
    "            }\n"
    "        }\n"
    "        c += wr * (1.0 / 36.0) * 3.0 * uWinRays * 2.6 * vec3(1.0, 0.68, 0.30);\n"
    "    }\n"
    "#endif\n"
    "    c += texture2D(uBloom, vPos).rgb * uBloomAmt;\n"
    "    if (uBack.x > 0.0 || uBack.y > 0.0) {\n"
    "        c = mix(c, texture2D(uBlur, sp).rgb, uBack.x);\n"
    "        c *= 1.0 - uBack.y;\n"
    "    }\n"
    // Battle effects: only the backdrop is in this picture (the battlers, HP boxes and text are the overlay, drawn after it).
    "    if (uBF0.x + uBF0.y + uBF0.z + uBF0.w + uBF1.w > 0.0) {\n"
    "        vec2 asp = vec2(uAspect, 1.0);\n"
    "        if (uBF0.y > 0.0) {\n"                                                        // RGB split along the line from the hit
    "            vec2 dir = normalize(sp - uBF2.xy + vec2(1.0e-4)) * uBF0.y;\n"
    "            vec2 sr = clamp(sp + dir, 0.0, 1.0);\n"
    "            vec2 sb = clamp(sp - dir, 0.0, 1.0);\n"
    "            c.r = mix(texture2D(uScene, sr).r, texture2D(uBlur, sr).r, uBack.x) * (1.0 - uBack.y);\n"
    "            c.b = mix(texture2D(uScene, sb).b, texture2D(uBlur, sb).b, uBack.x) * (1.0 - uBack.y);\n"
    "        }\n"
    "        float hd = length((vPos - uBF2.xy) * asp);\n"
    "        c += uBF1.rgb * (uBF0.x * 0.45 * exp(-hd * hd / max(uBF2.z * uBF2.z, 1.0e-4)));\n"    // type coloured flash
    "        c += vec3(uBF0.z);\n"                                                          // crit: quick white flash
    "        float lum = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "        c = mix(c, vec3(lum), 0.4 * uBF0.w);\n"                                          // low HP: mild desaturation
    "        float edge = smoothstep(0.30, 0.95, length((vPos - 0.5) * asp));\n"
    "        c = mix(c, c * vec3(1.0, 0.3, 0.26) + vec3(0.30, 0.0, 0.02) * edge, edge * uBF0.w);\n"   // red edge pulse
    "        c += vec3(0.20, 0.19, 0.17) * uBF1.w * (1.0 - edge);\n"                          // battle start: a soft light burst
    "    }\n"
    "#ifdef POST_X\n"
    "    if (uShaft.x > 0.0)\n"
    "        c += texture2D(uShafts, vPos).rgb * uShaft.x;\n"
    "#endif\n"
    "#ifdef POST_X\n"
    "    if (uLutOn.x > 0.5) {\n"
    "        vec3 q = clamp(c, 0.0, 1.0) * 15.0;\n"
    "        float s0 = floor(q.b);\n"
    "        float s1 = min(s0 + 1.0, 15.0);\n"
    "        float f = q.b - s0;\n"
    "        vec2 uv0 = vec2((s0 * 16.0 + q.r + 0.5) / 256.0, (q.g + 0.5) / 16.0);\n"
    "        vec2 uv1 = vec2((s1 * 16.0 + q.r + 0.5) / 256.0, (q.g + 0.5) / 16.0);\n"
    "        c = mix(c, mix(texture2D(uLut, uv0).rgb, texture2D(uLut, uv1).rgb, f), uLutOn.y);\n"
    "    } else\n"
    "#endif\n"
    "    {\n"
    "        float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "        vec3 tint = mix(uShadowTint, uHighTint, smoothstep(0.15, 0.75, l));\n"
    "        vec3 g = mix(vec3(l), c * tint, uSaturation);\n"
    "        c = mix(c, g, uGradeAmt);\n"
    "    }\n"
    "    vec2 d = (vPos - 0.5) * vec2(uAspect, 1.0);\n"
    "    float v = 1.0 - smoothstep(0.35, 0.95, length(d));\n"
    "    c *= mix(1.0, v, uVignette);\n"
    "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

#define DECK_BASE_W 960   // the logical picture and the OFF/LOW scene width
#define DECK_BASE_H 544

static bool sReady;
static void DeleteTargets(void);
static bool CreateLut(void);
static bool sXOk;      // the composite with POST_X built
static bool sSunOk;    // the composite has the sun shadow resolve (POST_SUN)
static GLint sCTiltB, sCBack, sCBF0, sCBF1, sCBF2, sCBF3;
// The uniforms of the shadow terms (sun shadow resolve, cloud shadows, AO upsample): the composite and the shade pre-pass share them.
typedef struct
{
    GLint scene, depth, z, aoTex, ao, camInv, cld, sunMap, invVP, sunVP, sunA, sunB, sunL;
} ShadowLocs;
static ShadowLocs sCSh, sShadeSh;
static DeckGlProgram sShade;   // composite body with POST_SHADEONLY: sharp scene + shadow terms only
static DeckGlTarget sShadeTgt; // its full resolution RGBA8 output (no depth)
static bool sShadeProg;        // the shade program built
static bool sShadeOk;          // program and target exist: shadows are applied before the blur
static bool sShaftOk; // the shaft program built and the composite has POST_X
static DeckGlProgram sShaft;
static GLint sShaftSun, sShaftLen, sShaftTime, sShaftAsp, sShaftCount, sShaftMotion, sShaftCol, sCShafts, sCShaft;
static bool sLutOk;    // the LUT texture exists and the composite has POST_X
static GLuint sLutTex;
static float sLutDay = -1.0f, sLutGlow = -1.0f;   // the parameters the table was last built for
static int sLutGradeLvl = -2, sLutLvl = -2;
static bool sAoProg;   // the two AO programs built and the composite has POST_X
static bool sAoTgt;    // the two half resolution AO targets exist
static DeckGlProgram sAoGather, sAoBlur;
static GLint sCWinRays;
static GLint sCWinSkew;
static float sWinRaysStr = 1.0f; // debug aid: env DECK_WINRAYS_STRENGTH (multiplier), DECK_WINRAYS_DEBUG=1 forces amount 1
static int sWinRaysDbg = 0;
static GLint sAoDepth, sAoZ, sAoProj, sAoTexel, sAoP, sAoQ, sAoPowU, sAoBlSrc, sAoBlDepth, sAoBlStep, sAoBlBias, sAoBlZ;
static DeckGlTarget sAo[2];  // half resolution occlusion and its blur scratch
static bool sTiltOk;   // tilt-shift can run: POST_X composite and a depth texture on the scene target
static bool sSizeWarned;
static int sFailW;     // a scene width that could not be created (not tried again)
static DeckGlTarget sScene;
static DeckGlTarget sHalf[4]; // 0 bloom, 1 blur scratch, 2 DoF / tilt-shift copy, 3 light shafts
static DeckGlProgram sBright, sCopy, sBlur, sComposite;
static GLint sCopyTexel, sBrightScene, sBrightThreshold, sCopyScene, sBlurSrc, sBlurStep;
static GLint sCScene, sCBloom, sCBlur, sCBloomAmt, sCBlurOn, sCDof, sCGradeAmt, sCShadow, sCHigh, sCSat, sCVignette, sCAspect, sCDepth, sCTilt, sCZ, sCLut, sCLutOn;

static const float kQuad[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

// AO can run: programs, targets, and a depth texture on the scene target.
static bool AoUsable(void)
{
    return sAoProg && sAoTgt && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
}

static float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static int ClampKey(int v, int lo, int hi, int def)
{
    return (v < lo || v > hi) ? def : v;
}

DeckPostConfig DeckPost_ReadConfig(void)
{
    DeckPostConfig c;
    c.quality = ClampKey(VitaExtras_GetFast(FAST_EXTRA_VOXEL_QUALITY), 0, 2, 1);
    c.shaders = ClampKey(VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS), 0, 1, 1) != 0;
    c.gradeLvl = DeckLevel(FAST_EXTRA_VOXEL_GRADE);
    c.bloomLvl = DeckLevel(FAST_EXTRA_VOXEL_BLOOM);
    c.blurLvl = DeckLevel(FAST_EXTRA_VOXEL_BLUR);
    c.tiltLvl = DeckLevel(FAST_EXTRA_VOXEL_TILTSHIFT);
    c.shaftLvl = DeckLevel(FAST_EXTRA_VOXEL_SHAFTS);
    c.lutLvl = DeckLevel(FAST_EXTRA_VOXEL_LUT);
    c.aoLvl = DeckLevel(FAST_EXTRA_VOXEL_AO);
    c.cloudLvl = DeckLevel(FAST_EXTRA_VOXEL_CLOUDS);
    c.sunLvl = DeckSunShadow_Level();
    c.dimLvl = DeckLevel(FAST_EXTRA_VOXEL_BATTLEDIM);
    c.sceneLvl = DeckLevel(FAST_EXTRA_VOXEL_BATTLESCENE);
    c.grade = c.gradeLvl >= 0;
    c.bloom = c.bloomLvl >= 0;
    c.blur = c.blurLvl >= 0;
    c.tilt = c.tiltLvl >= 0;
    c.shafts = c.shaftLvl >= 0;
    c.lut = c.lutLvl >= 0;
    c.ao = c.aoLvl >= 0;
    c.clouds = c.cloudLvl >= 0;
    c.sun = c.sunLvl >= 0;
    return c;
}

static void FillShadowLocs(ShadowLocs *l, const DeckGlProgram *p)
{
    l->scene = DeckGl_Uniform(p, "uScene");
    l->depth = DeckGl_Uniform(p, "uDepth");   // -1 when the program does not have it (setters ignore it)
    l->z = DeckGl_Uniform(p, "uZ");
    l->aoTex = DeckGl_Uniform(p, "uAoTex");
    l->ao = DeckGl_Uniform(p, "uAO");
    l->camInv = DeckGl_Uniform(p, "uCamInv");
    l->cld = DeckGl_Uniform(p, "uCld");
    l->sunMap = DeckGl_Uniform(p, "uSunMap");
    l->invVP = DeckGl_Uniform(p, "uInvVP");
    l->sunVP = DeckGl_Uniform(p, "uSunVP");
    l->sunA = DeckGl_Uniform(p, "uSunA");
    l->sunB = DeckGl_Uniform(p, "uSunB");
    l->sunL = DeckGl_Uniform(p, "uSunL");
}

// Sets the shadow term uniforms on the program in use. Texture units: scene colour 0, depth 3, AO map 7, sun map 6.
static void SetShadowUniforms(const ShadowLocs *l, const DeckPostFrame *in, const DeckSunResolve *sun, const float ao[4], const float cld[4])
{
    DeckGl_Set1i(l->depth, 3);
    DeckGl_Set2f(l->z, in->zNear, in->zFar);
    DeckGl_Set1i(l->sunMap, 6);
    DeckGl_Set1i(l->aoTex, 7);
    DeckGl_Set4f(l->ao, ao[0], ao[1], ao[2], ao[3]);
    DeckGl_SetMat4(l->camInv, in->invVP);
    DeckGl_Set4f(l->cld, cld[0], cld[1], cld[2], cld[3]);
    if (sun != NULL)
    {
        DeckGl_SetMat4(l->invVP, sun->invVP);
        DeckGl_SetMat4(l->sunVP, sun->sunVP);
        DeckGl_Set4f(l->sunA, sun->amount, sun->texelUv, (float)sun->radius, sun->mul);
        DeckGl_Set4f(l->sunB, sun->range, sun->texelWorld, sun->spacing, 0.0f);
        DeckGl_Set3f(l->sunL, sun->toSun[0], sun->toSun[1], sun->toSun[2]);
    }
    else
        DeckGl_Set4f(l->sunA, 0.0f, 0.0f, 0.0f, 1.0f);
}

static bool BuildPrograms(void)
{
    static char src[20480];
    const char *noX = getenv("DECK_POST_NO_X");

    if (!DeckGl_BuildProgram(&sBright, "post_bright", kFullscreenVs, kBrightFs)
        || !DeckGl_BuildProgram(&sCopy, "post_copy", kFullscreenVs, kCopyFs)
        || !DeckGl_BuildProgram(&sBlur, "post_blur", kFullscreenVs, kBlurFs))
        return false;
    // The extended composite first; if it does not build (or DECK_POST_NO_X), the phase 1 composite.
    sXOk = false;
    sSunOk = false;
    if (noX == NULL || strcmp(noX, "0") == 0)
    {
        // POST_X with the sun shadow resolve first, then POST_X alone, then the phase 1 composite
        for (int withSun = 1; withSun >= 0 && !sXOk; withSun--)
        {
            if (snprintf(src, sizeof(src), "#version 120\n#define POST_X 1\n%s%s", withSun ? "#define POST_SUN 1\n" : "", kCompositeBody) >= (int)sizeof(src))
            {
                DeckGl_Log("[POST] composite source truncated (%d bytes): not built\n", (int)sizeof(src));
                return false;
            }
            sXOk = DeckGl_BuildProgram(&sComposite, withSun ? "post_composite_xs" : "post_composite_x", kFullscreenVs, src);
            sSunOk = sXOk && withSun;
            if (!sXOk)
                DeckGl_Log("[POST] composite %s failed\n", withSun ? "with the sun shadow (POST_SUN): trying without" : "extended: plain composite");
        }
    }
    if (!sXOk)
    {
        if (snprintf(src, sizeof(src), "#version 120\n%s", kCompositeBody) >= (int)sizeof(src))
        {
            DeckGl_Log("[POST] composite source truncated (%d bytes): not built\n", (int)sizeof(src));
            return false;
        }
        if (!DeckGl_BuildProgram(&sComposite, "post_composite", kFullscreenVs, src))
            return false;
    }
    sBrightScene = DeckGl_Uniform(&sBright, "uScene");
    sBrightThreshold = DeckGl_Uniform(&sBright, "uThreshold");
    sCopyScene = DeckGl_Uniform(&sCopy, "uScene");
    sCopyTexel = DeckGl_Uniform(&sCopy, "uTexel");
    sBlurSrc = DeckGl_Uniform(&sBlur, "uSrc");
    sBlurStep = DeckGl_Uniform(&sBlur, "uStep");
    sCScene = DeckGl_Uniform(&sComposite, "uScene");
    sCBloom = DeckGl_Uniform(&sComposite, "uBloom");
    sCBlur = DeckGl_Uniform(&sComposite, "uBlur");
    sCBloomAmt = DeckGl_Uniform(&sComposite, "uBloomAmt");
    sCBlurOn = DeckGl_Uniform(&sComposite, "uBlurOn");
    sCDof = DeckGl_Uniform(&sComposite, "uDof");
    sCGradeAmt = DeckGl_Uniform(&sComposite, "uGradeAmt");
    sCShadow = DeckGl_Uniform(&sComposite, "uShadowTint");
    sCHigh = DeckGl_Uniform(&sComposite, "uHighTint");
    sCSat = DeckGl_Uniform(&sComposite, "uSaturation");
    sCVignette = DeckGl_Uniform(&sComposite, "uVignette");
    sCAspect = DeckGl_Uniform(&sComposite, "uAspect");
    sCBack = DeckGl_Uniform(&sComposite, "uBack");
    sCBF0 = DeckGl_Uniform(&sComposite, "uBF0");
    sCBF1 = DeckGl_Uniform(&sComposite, "uBF1");
    sCBF2 = DeckGl_Uniform(&sComposite, "uBF2");
    sCBF3 = DeckGl_Uniform(&sComposite, "uBF3");
    sCDepth = DeckGl_Uniform(&sComposite, "uDepth");   // -1 in the plain composite (setters ignore it)
    sCTilt = DeckGl_Uniform(&sComposite, "uTilt");
    sCTiltB = DeckGl_Uniform(&sComposite, "uTiltB");
    sCZ = DeckGl_Uniform(&sComposite, "uZ");
    sCShafts = DeckGl_Uniform(&sComposite, "uShafts");
    sCShaft = DeckGl_Uniform(&sComposite, "uShaft");
    FillShadowLocs(&sCSh, &sComposite);
    sCWinRays = DeckGl_Uniform(&sComposite, "uWinRays");
    sCWinSkew = DeckGl_Uniform(&sComposite, "uWinSkew");
    sCLut = DeckGl_Uniform(&sComposite, "uLut");
    sCLutOn = DeckGl_Uniform(&sComposite, "uLutOn");
    // The shade pre-pass is optional: without it the shadows stay in the composite (after the blur, the old look).
    sShadeProg = false;
    if (sXOk)
    {
        if (snprintf(src, sizeof(src), "#version 120\n#define POST_X 1\n#define POST_SHADEONLY 1\n%s%s", sSunOk ? "#define POST_SUN 1\n" : "", kCompositeBody) < (int)sizeof(src))
            sShadeProg = DeckGl_BuildProgram(&sShade, "post_shade", kFullscreenVs, src);
        if (sShadeProg)
            FillShadowLocs(&sShadeSh, &sShade);
        else
        {
            DeckGl_DeleteProgram(&sShade);
            DeckGl_Log("[POST] shade pre-pass failed: shadows stay in the composite\n");
        }
    }
    // The shaft pass is optional: a failed build only switches the shafts off.
    sShaftOk = sXOk && DeckGl_BuildProgram(&sShaft, "post_shaft", kFullscreenVs, kShaftFs);
    if (sShaftOk)
    {
        sShaftSun = DeckGl_Uniform(&sShaft, "uDir");
        sShaftLen = DeckGl_Uniform(&sShaft, "uLen");
        sShaftTime = DeckGl_Uniform(&sShaft, "uTime");
        sShaftAsp = DeckGl_Uniform(&sShaft, "uAsp");
        sShaftCount = DeckGl_Uniform(&sShaft, "uCount");
        sShaftMotion = DeckGl_Uniform(&sShaft, "uMotion");
        sShaftCol = DeckGl_Uniform(&sShaft, "uCol");
    }
    // The AO passes are optional too: a failed build only switches the effect off.
    sAoProg = sXOk && DeckGl_BuildProgram(&sAoGather, "post_ao", kFullscreenVs, kAoFs) && DeckGl_BuildProgram(&sAoBlur, "post_ao_blur", kFullscreenVs, kAoBlurFs);
    if (sAoProg)
    {
        sAoDepth = DeckGl_Uniform(&sAoGather, "uDepth");
        sAoZ = DeckGl_Uniform(&sAoGather, "uZ");
        sAoProj = DeckGl_Uniform(&sAoGather, "uProj");
        sAoTexel = DeckGl_Uniform(&sAoGather, "uTexel");
        sAoP = DeckGl_Uniform(&sAoGather, "uP");
        sAoQ = DeckGl_Uniform(&sAoGather, "uQ");
        sAoPowU = DeckGl_Uniform(&sAoGather, "uPow");
        sAoBlSrc = DeckGl_Uniform(&sAoBlur, "uSrc");
        sAoBlDepth = DeckGl_Uniform(&sAoBlur, "uDepth");
        sAoBlStep = DeckGl_Uniform(&sAoBlur, "uStep");
        sAoBlBias = DeckGl_Uniform(&sAoBlur, "uBias");
        sAoBlZ = DeckGl_Uniform(&sAoBlur, "uZ");
    }
    else
    {
        DeckGl_DeleteProgram(&sAoGather);
        DeckGl_DeleteProgram(&sAoBlur);
        DeckGl_Log("[POST] ao programs failed: voxel_ao off\n");
    }
    return true;
}

void DeckPost_Shutdown(void)
{
    DeckGl_DeleteProgram(&sBright);
    DeckGl_DeleteProgram(&sCopy);
    DeckGl_DeleteProgram(&sBlur);
    DeckGl_DeleteProgram(&sComposite);
    DeckGl_DeleteProgram(&sShaft);
    sShaftOk = false;
    DeckGl_DeleteProgram(&sAoGather);
    DeckGl_DeleteProgram(&sAoBlur);
    sAoProg = false;
    DeckGl_DeleteProgram(&sShade);
    sShadeProg = false;
    if (sLutTex)
        glDeleteTextures(1, &sLutTex);
    sLutTex = 0;
    sLutOk = false;
    sLutDay = sLutGlow = -1.0f;
    sLutGradeLvl = sLutLvl = -2;
    DeleteTargets();
    DeckGl_TimerShutdown();
    sReady = false;
    sXOk = false;
    sTiltOk = false;
    sSunOk = false;
    DeckSunShadow_Shutdown();
}

// Scene width by voxel_res: OFF and LOW 960, MEDIUM 1120, HIGH 1280 (AUTO follows voxel_quality); height keeps 960:544, even.
static void WantedSize(int *w, int *h)
{
    static const int kWidth[3] = { 960, 1120, 1280 };
    const int lvl = DeckLevel(FAST_EXTRA_VOXEL_RES);

    *w = lvl < 0 ? DECK_BASE_W : kWidth[lvl];
    *h = (((*w * DECK_BASE_H + DECK_BASE_W - 1) / DECK_BASE_W) + 1) & ~1;
}

static bool CreateTargets(int w, int h)
{
    bool ok = DeckGl_CreateTarget(&sScene, w, h, DECK_GL_DEPTH_TEXTURE24, GL_NEAREST);

    if (!ok)
    {
        DeckGl_DeleteTarget(&sScene);
        ok = DeckGl_CreateTarget(&sScene, w, h, DECK_GL_DEPTH_RENDERBUFFER24, GL_NEAREST);
    }
    for (int i = 0; i < 4 && ok; i++)
        ok = DeckGl_CreateTarget(&sHalf[i], w / 2, h / 2, DECK_GL_DEPTH_NONE, GL_LINEAR);
    // The AO targets are optional: a failed create only switches voxel_ao off.
    sAoTgt = ok && DeckGl_CreateTarget(&sAo[0], w / 2, h / 2, DECK_GL_DEPTH_NONE, GL_LINEAR) && DeckGl_CreateTarget(&sAo[1], w / 2, h / 2, DECK_GL_DEPTH_NONE, GL_LINEAR);
    if (ok && !sAoTgt)
    {
        DeckGl_DeleteTarget(&sAo[0]);
        DeckGl_DeleteTarget(&sAo[1]);
        DeckGl_Log("[POST] ao targets failed at %dx%d: voxel_ao off\n", w / 2, h / 2);
    }
    // The shade target is optional: a failed create only keeps the shadows in the composite (after the blur).
    sShadeOk = ok && DeckGl_CreateTarget(&sShadeTgt, w, h, DECK_GL_DEPTH_NONE, GL_NEAREST);
    if (ok && !sShadeOk)
    {
        DeckGl_DeleteTarget(&sShadeTgt);
        DeckGl_Log("[POST] shade target failed at %dx%d: shadows stay in the composite\n", w, h);
    }
    return ok;
}

static void DeleteTargets(void)
{
    DeckGl_DeleteTarget(&sScene);
    for (int i = 0; i < 4; i++)
        DeckGl_DeleteTarget(&sHalf[i]);
    DeckGl_DeleteTarget(&sAo[0]);
    DeckGl_DeleteTarget(&sAo[1]);
    sAoTgt = false;
    DeckGl_DeleteTarget(&sShadeTgt);
    sShadeOk = false;
}

void DeckPost_Init(int sceneW, int sceneH)
{
    const char *force = getenv("DECK_POST_FORCE_FAIL");
    bool ok;
    sReady = false;
    sSizeWarned = false;
    {
        const char *e;

        if ((e = getenv("DECK_AO_RADIUS")) != NULL) sAoRadMul = (float)atof(e);
        if ((e = getenv("DECK_AO_THICK")) != NULL) sAoThick = (float)atof(e);
        if ((e = getenv("DECK_AO_INTENSITY")) != NULL) sAoInt = (float)atof(e);
        if ((e = getenv("DECK_AO_POW")) != NULL) sAoPow = (float)atof(e);
        if ((e = getenv("DECK_CLOUDS_STRENGTH")) != NULL) sCloudStr = (float)atof(e);
        if ((e = getenv("DECK_CLOUDS_DEBUG")) != NULL) sCloudDbg = strcmp(e, "0") != 0;
        if ((e = getenv("DECK_WINRAYS_STRENGTH")) != NULL) sWinRaysStr = (float)atof(e);
        if ((e = getenv("DECK_WINRAYS_DEBUG")) != NULL) sWinRaysDbg = strcmp(e, "0") != 0;
        if ((e = getenv("DECK_AO_DIRS")) != NULL) sAoDirsOv = atoi(e);
        if ((e = getenv("DECK_AO_STEPS")) != NULL) sAoStepsOv = atoi(e);
        if ((e = getenv("DECK_AO_BLUR")) != NULL) sAoBlurOv = atoi(e);
    }
    if (!DeckGl_Init())
    {
        DeckGl_Log("[POST] shaders unavailable: legacy path\n");
        return;
    }
    sFailW = 0;
    WantedSize(&sceneW, &sceneH);
    ok = CreateTargets(sceneW, sceneH);
    if (!ok && sceneW != DECK_BASE_W)
    {
        DeckGl_Log("[POST] %dx%d target failed at init: %dx%d\n", sceneW, sceneH, DECK_BASE_W, DECK_BASE_H);
        sFailW = sceneW; // not tried again by ApplyWantedSize
        DeleteTargets();
        ok = CreateTargets(DECK_BASE_W, DECK_BASE_H);
    }
    ok = ok && BuildPrograms();
    if (ok && force != NULL && strcmp(force, "0") != 0)
    {
        DeckGl_Log("[POST] DECK_POST_FORCE_FAIL set: forcing the fallback\n");
        ok = false;
    }
    if (!ok)
    {
        DeckPost_Shutdown();
        DeckGl_Log("[POST] fallback: legacy diorama FBO and Pass_DepthOfField in use\n");
        return;
    }
    DeckGl_TimerInit();
    sReady = true;
    sTiltOk = sXOk && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
    sLutOk = sXOk && CreateLut();
    const DeckPostConfig c = DeckPost_ReadConfig();
    DeckGl_Log("[POST] ready: scene %dx%d RGBA8 depth=%s extended=%d tilt=%d shafts=%d lut=%d ao=%d | config quality=%d shaders=%d grade=%d bloom=%d blur=%d tiltlvl=%d\n",
               sScene.w, sScene.h, sScene.depth == DECK_GL_DEPTH_TEXTURE24 ? "texture24" : "renderbuffer24",
               (int)sXOk, (int)sTiltOk, (int)sShaftOk, (int)sLutOk, (int)AoUsable(), c.quality, (int)c.shaders, (int)c.grade, (int)c.bloom, (int)c.blur, (int)c.tilt);
}

// A changed voxel_res: delete and create the scene and half-res targets at the new size (GL thread, between frames).
// A failed create goes back to the old size, remembers the width, and does not try it again.
static void ApplyWantedSize(void)
{
    int w, h;

    WantedSize(&w, &h);
    if (w == sScene.w || w == sFailW)
        return;
    const int oldW = sScene.w, oldH = sScene.h;
    DeleteTargets();
    if (CreateTargets(w, h))
    {
        DeckGl_Log("[POST] scene target %dx%d -> %dx%d (voxel_res)\n", oldW, oldH, sScene.w, sScene.h);
        sFailW = 0;
    }
    else
    {
        DeckGl_Log("[POST] scene target %dx%d failed: back to %dx%d\n", w, h, oldW, oldH);
        sFailW = w;
        DeleteTargets();
        if (!CreateTargets(oldW, oldH))
        {
            DeckPost_Shutdown();
            DeckGl_Log("[POST] fallback: legacy diorama FBO and Pass_DepthOfField in use\n");
            return;
        }
    }
    sTiltOk = sXOk && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
}

int DeckPost_SceneWidth(void) { return sScene.w; }
int DeckPost_SceneHeight(void) { return sScene.h; }

bool DeckPost_WantScene(int renderW, int renderH, bool indoor, bool battle)
{
    if (!sReady)
        return false;
    if (renderW != DECK_BASE_W || renderH != DECK_BASE_H)
    {
        if (!sSizeWarned)
            DeckGl_Log("[POST] render size %dx%d is not the logical %dx%d: legacy path\n",
                       renderW, renderH, DECK_BASE_W, DECK_BASE_H);
        sSizeWarned = true;
        return false;
    }
    ApplyWantedSize();
    if (!sReady)
        return false;
    const DeckPostConfig c = DeckPost_ReadConfig();
    if (!c.shaders)
        return false;
    if (battle)
        return c.dimLvl >= 0 || DeckBattleFx_Enabled() || c.sceneLvl >= 0; // the battle gets the backdrop blur and dim, the battle effects and the scenery
    return c.grade || c.bloom || (c.blur && !indoor) || (c.tilt && sTiltOk && !indoor) || (c.shafts && sShaftOk) || (c.ao && AoUsable()) || (c.clouds && sTiltOk && !indoor) || (c.lut && sLutOk) || (c.sun && sSunOk);
}

bool DeckPost_SunReady(void)
{
    return sReady && sSunOk && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
}

void DeckPost_BindScene(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, sScene.fbo);
}

static void DrawQuad(void)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, kQuad);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void BindTex(int unit, GLuint tex)
{
    DeckGl_ActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

// Bind first, then set the viewport (raw glViewport here: deck_post.c does not see the macro).
static void BeginTarget(const DeckGlTarget *t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
}

static void BlurPass(const DeckGlTarget *dst, const DeckGlTarget *src, float stepU, float stepV)
{
    BeginTarget(dst);
    BindTex(0, src->color);
    DeckGl_UseProgram(&sBlur);
    DeckGl_Set1i(sBlurSrc, 0);
    DeckGl_Set2f(sBlurStep, stepU, stepV);
    DrawQuad();
}

// The step grows with the scene width so the blur keeps its size on the picture at 1120 and 1280.
static void Blur(const DeckGlTarget *t, const DeckGlTarget *tmp, int passes, float stepMul)
{
    const float k = stepMul * (float)sScene.w / (float)DECK_BASE_W;

    for (int i = 0; i < passes; i++)
    {
        BlurPass(tmp, t, k / (float)t->w, 0.0f);
        BlurPass(t, tmp, 0.0f, k / (float)t->h);
    }
}

static void GradeTints(float daylight, float glow, float shadow[3], float high[3], float w[3])
{
    const float dusk = Clamp01(glow * 4.0f) * Clamp01(daylight / 0.35f);
    const float night = Clamp01(1.0f - daylight / 0.35f);
    const float day = Clamp01(1.0f - dusk - night);
    const float sum = day + dusk + night; // >= 1 by construction
    w[0] = day / sum;
    w[1] = dusk / sum;
    w[2] = night / sum;
    for (int i = 0; i < 3; i++)
    {
        shadow[i] = (kShadowTint[0][i] * day + kShadowTint[1][i] * dusk + kShadowTint[2][i] * night) / sum;
        high[i] = (kHighTint[0][i] * day + kHighTint[1][i] * dusk + kHighTint[2][i] * night) / sum;
    }
}

static bool CreateLut(void)
{
    GLint prevTex = 0;

    DeckGl_ActiveTexture(GL_TEXTURE0 + 4);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGenTextures(1, &sLutTex);
    glBindTexture(GL_TEXTURE_2D, sLutTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    DeckGl_ActiveTexture(GL_TEXTURE0);
    return sLutTex != 0;
}

static float Smooth01(float x)
{
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

// 16x16x16 strip, 16 slices of blue side by side (x = slice * 16 + red, y = green). Each texel is the procedural grade
// (tint by luminance, saturation, at the preset strength; none when the grade is off) plus a gentle S-curve.
static void BuildLut(float daylight, float glow, const DeckPostPreset *pr, bool gradeOn, int lutLvl)
{
    static unsigned char px[16 * 256 * 3];
    float shadow[3], high[3], w[3];
    GLint prevTex = 0;
    float amt;

    GradeTints(daylight, glow, shadow, high, w);
    amt = (kLutCurve[0] * w[0] + kLutCurve[1] * w[1] + kLutCurve[2] * w[2]) * kLutLevel[lutLvl];
    for (int b = 0; b < 16; b++)
    {
        for (int g = 0; g < 16; g++)
        {
            for (int r = 0; r < 16; r++)
            {
                const float c[3] = { (float)r / 15.0f, (float)g / 15.0f, (float)b / 15.0f };
                const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
                const float t = Smooth01((l - 0.15f) / 0.60f);
                unsigned char *const o = &px[(g * 256 + b * 16 + r) * 3];

                for (int k = 0; k < 3; k++)
                {
                    const float tint = shadow[k] + (high[k] - shadow[k]) * t;
                    const float graded = l + (c[k] * tint - l) * pr->saturation;
                    float v = gradeOn ? c[k] + (graded - c[k]) * pr->grade : c[k];

                    v = Clamp01(v);
                    v += amt * (Smooth01(v) - v);
                    o[k] = (unsigned char)(Clamp01(v) * 255.0f + 0.5f);
                }
            }
        }
    }
    DeckGl_ActiveTexture(GL_TEXTURE0 + 4);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glBindTexture(GL_TEXTURE_2D, sLutTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 16, GL_RGB, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    DeckGl_ActiveTexture(GL_TEXTURE0);
}

void DeckPost_Run(const DeckPostFrame *in)
{
    DeckPostConfig cfg = DeckPost_ReadConfig();
    const int backLvl = in->battle ? cfg.dimLvl : -1;
    // BATTLE SCENERY on: the focus band (tilt-shift) replaces the uniform backdrop blur, the dim is light, and grade, bloom, LUT and
    // shafts run at the field's levels (scaled below). Off: the battle is exactly the old one (backdrop blur and dim only).
    const bool scene = in->battle && cfg.sceneLvl >= 0;
    const float sceneK = scene ? kSceneK[cfg.sceneLvl] : 0.0f;
    const bool doBack = backLvl >= 0 && !scene;
    if (in->battle)
    {
        // a battle gets the backdrop blur and dim only; the scene stays crisp and ungraded (scenery: see above)
        const bool keepG = scene && cfg.grade, keepB = scene && cfg.bloom, keepL = scene && cfg.lut, keepS = scene && cfg.shafts;

        cfg.grade = keepG; cfg.bloom = keepB; cfg.lut = keepL; cfg.shafts = keepS;
        cfg.blur = cfg.sun = cfg.ao = cfg.clouds = false;
        cfg.tilt = scene && sTiltOk;
        if (!keepG) cfg.gradeLvl = -1;
        if (!keepB) cfg.bloomLvl = -1;
        if (!keepL) cfg.lutLvl = -1;
        if (!keepS) cfg.shaftLvl = -1;
        cfg.blurLvl = cfg.sunLvl = cfg.aoLvl = cfg.cloudLvl = -1;
        cfg.tiltLvl = cfg.tilt ? cfg.sceneLvl : -1;
    }
    const DeckPostPreset *const pr = &kPresets[cfg.gradeLvl >= 0 ? cfg.gradeLvl : cfg.quality];      // grade, saturation, vignette
    const DeckPostPreset *const pb = &kPresets[cfg.bloomLvl >= 0 ? cfg.bloomLvl : cfg.quality];      // bloom threshold and blur passes
    const float bloomGain = cfg.bloomLvl >= 0 ? kBloomGain[cfg.bloomLvl] * (scene ? kSceneBloom * sceneK : 1.0f) : 0.0f;
    const float blurMul = cfg.blurLvl >= 0 ? kBlurMul[cfg.blurLvl] : 0.0f;
    const float keep = 1.0f - Clamp01(in->fade);
    const bool doBloom = cfg.bloom && in->bloom > 0.0f && bloomGain > 0.0f;
    const bool doTilt = cfg.tilt && sTiltOk && (!in->indoor || scene) && in->focusDist > 0.0f; // replaces the band blur
    const bool doLut = cfg.lut && sLutOk;
    const float shaftAmt = in->sunAmount + in->moonAmount;
    const bool doShafts = cfg.shafts && sShaftOk && shaftAmt > 0.01f;
    const bool doBlur = cfg.blur && !in->indoor && !doTilt;
    const bool doClouds = cfg.clouds && sTiltOk && in->haveInvVP && !in->indoor && keep > 0.01f && (in->cloudAmount > 0.01f || sCloudDbg);
    const float winRays = (sTiltOk && !in->indoor && !in->battle && keep > 0.01f) ? (sWinRaysDbg ? 1.0f : Clamp01(in->winRaysAmount * 0.85f) * keep) * sWinRaysStr : 0.0f;
    const bool doAo = cfg.ao &&AoUsable() && in->tanHalfFov > 0.0f && keep > 0.01f;
    const DeckSunResolve *const sun = (cfg.sun && sSunOk && in->sun != NULL && in->sun->active) ? in->sun : NULL;
    float shadow[3], high[3];
    static int sAoDbg = -1; // debug aid: env DECK_AO_DEBUG=1 shows the raw occlusion as grey (screenshots)

    if (sAoDbg < 0)
    {
        const char *e = getenv("DECK_AO_DEBUG");

        sAoDbg = (e != NULL && strcmp(e, "0") != 0) ? 1 : 0;
    }
    // Shadows before the blur: the shade pre-pass puts the sun shadow, the cloud shadows and the AO on the sharp scene, then the blurs and the
    // composite read that picture, so a blurred object carries a blurred shadow. Not in the debug views (they return from the composite body).
    const bool shaded = sShadeProg && sShadeOk && (doAo || doClouds || sun != NULL) && !(doClouds && sCloudDbg) && !(doAo && sAoDbg);
    const DeckSunResolve *const sunC = shaded ? NULL : sun;   // what the composite itself still applies
    const bool aoC = doAo && !shaded, cloudsC = doClouds && !shaded;
    const float aoU[4] = { doAo ? sAoInt * keep : 0.0f, doAo ? (float)sAoDbg : 0.0f, doAo ? 1.0f / (float)sAo[0].w : 0.0f, doAo ? 1.0f / (float)sAo[0].h : 0.0f };
    const float cldU[4] = { doClouds ? (sCloudDbg ? kCloudMax[cfg.cloudLvl] : kCloudMax[cfg.cloudLvl] * Clamp01(in->cloudAmount) * keep) * sCloudStr : 0.0f, (doClouds && sCloudDbg) ? 1.0f : 0.0f, in->time, 0.0f };
    const float noU[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLuint sceneTex = sScene.color;

    if (scene)
    {
        static int sSceneDbg = -1;
        static float sSceneLog = -100.0f;

        if (sSceneDbg < 0)
            sSceneDbg = getenv("DECK_BATTLESCENE_DEBUG") != NULL ? 1 : 0;
        if (sSceneDbg == 1 && in->time - sSceneLog >= 5.0f)
        {
            sSceneLog = in->time;
            DeckGl_Log("[BATTLESCENE] post: level %d tilt %d (focus %.2f, band %.2f..%.2f) grade %d bloom %d (gain %.2f amt %.2f) lut %d shafts %d (amt %.2f) dim %.3f\n",
                       cfg.sceneLvl, (int)doTilt, in->focusDist, kSceneBandA, kSceneBandB, (int)cfg.grade, (int)doBloom, bloomGain, in->bloom,
                       (int)doLut, (int)doShafts, shaftAmt, cfg.dimLvl >= 0 ? kSceneDim[cfg.sceneLvl] : 0.0f);
        }
    }
    if (doLut)
    {
        // Rebuilt only when the time of day, the grade level or the LUT level moved (4096 texels, microseconds).
        if (sLutDay < 0.0f || fabsf(in->daylight - sLutDay) > 0.004f || fabsf(in->glow - sLutGlow) > 0.004f
            || cfg.gradeLvl != sLutGradeLvl || cfg.lutLvl != sLutLvl)
        {
            BuildLut(in->daylight, in->glow, pr, cfg.grade, cfg.lutLvl);
            sLutDay = in->daylight;
            sLutGlow = in->glow;
            sLutGradeLvl = cfg.gradeLvl;
            sLutLvl = cfg.lutLvl;
        }
    }
    VitaPerf_DeckMarkPostStart();
    DeckGl_TimerBegin();
    VitaPerf_DeckSetPostGpuMs(DeckGl_TimerLastMs());

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // Half-resolution reads of the scene: bilinear (one tap averages 2x2 scene texels).
    BindTex(0, sScene.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (doBloom)
    {
        BeginTarget(&sHalf[0]);
        DeckGl_UseProgram(&sBright);
        DeckGl_Set1i(sBrightScene, 0);
        DeckGl_Set1f(sBrightThreshold, pb->threshold);
        DrawQuad();
        Blur(&sHalf[0], &sHalf[1], pb->bloomPasses, 1.0f);
    }
    if (doShafts)
    {
        const float moonK = in->moonAmount / shaftAmt;
        const float goldK = Clamp01(in->glow * 4.0f) * (1.0f - moonK);
        float col[3];

        for (int k = 0; k < 3; k++)
        {
            const float sun = kShaftDay[k] + (kShaftGold[k] - kShaftDay[k]) * goldK;

            col[k] = sun + (kShaftMoon[k] - sun) * moonK;
        }
        BeginTarget(&sHalf[3]);
        DeckGl_UseProgram(&sShaft);
        DeckGl_Set2f(sShaftSun, in->beamX, in->beamY);
        DeckGl_Set1f(sShaftLen, Clamp01((in->beamElev - 0.45f) / 0.5f) * 0.5f + 0.85f); // elevation 0.59 (dawn) -> 0.92, 0.88 (noon) -> 1.34
        DeckGl_Set1f(sShaftTime, in->time);
        DeckGl_Set1f(sShaftAsp, (float)in->renderW / (float)in->renderH);
        DeckGl_Set1f(sShaftCount, kShaftLayers[cfg.shaftLvl]);
        DeckGl_Set1f(sShaftMotion, 1.0f - 0.55f * moonK);
        DeckGl_Set3f(sShaftCol, col[0], col[1], col[2]);
        DrawQuad();
    }
    if (doAo)
    {
        // Gather at half resolution into sAo[0], then the depth-aware blur there and back (horizontal into sAo[1], vertical into sAo[0]).
        const float kx = 1.0f / (float)sAo[0].w, ky = 1.0f / (float)sAo[0].h;

        BindTex(3, sScene.depthTex);
        BeginTarget(&sAo[0]);
        DeckGl_UseProgram(&sAoGather);
        DeckGl_Set1i(sAoDepth, 3);
        DeckGl_Set2f(sAoZ, in->zNear, in->zFar);
        DeckGl_Set2f(sAoProj, in->tanHalfFov, in->aspect);
        DeckGl_Set2f(sAoTexel, kx, ky);
        DeckGl_Set4f(sAoP, kAoRadius[cfg.aoLvl] * sAoRadMul, sAoThick, kAoFadeStart, kAoFadeEnd);
        DeckGl_Set2f(sAoQ, sAoDirsOv > 0 ? (float)sAoDirsOv : kAoDirs[cfg.aoLvl], sAoStepsOv > 0 ? (float)sAoStepsOv : kAoSteps[cfg.aoLvl]);
        DeckGl_Set1f(sAoPowU, sAoPow);
        DrawQuad();
        if (sAoBlurOv != 0)
        {
        DeckGl_UseProgram(&sAoBlur);
        DeckGl_Set1i(sAoBlSrc, 0);
        DeckGl_Set1i(sAoBlDepth, 3);
        DeckGl_Set2f(sAoBlBias, 0.25f * kx, 0.25f * ky);
        DeckGl_Set2f(sAoBlZ, in->zNear, in->zFar);
        BeginTarget(&sAo[1]);
        BindTex(0, sAo[0].color);
        DeckGl_Set2f(sAoBlStep, kx, 0.0f);
        DrawQuad();
        BeginTarget(&sAo[0]);
        BindTex(0, sAo[1].color);
        DeckGl_Set2f(sAoBlStep, 0.0f, ky);
        DrawQuad();
        }
    }
    if (shaded)
    {
        // Full resolution: the sharp scene with the sun shadow, the cloud shadows and the AO applied, nothing else.
        // Units: 0 scene colour, 3 depth, 6 sun map, 7 AO map (the composite reads the shaded picture on unit 0 instead).
        BindTex(7, doAo ? sAo[0].color : 0);
        BindTex(6, sun != NULL ? sun->depthTex : 0);
        BindTex(3, sScene.depthTex);
        BindTex(0, sScene.color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // 1:1 texels
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        BeginTarget(&sShadeTgt);
        DeckGl_UseProgram(&sShade);
        DeckGl_Set1i(sShadeSh.scene, 0);
        SetShadowUniforms(&sShadeSh, in, sun, aoU, cldU);
        DrawQuad();
        sceneTex = sShadeTgt.color;
    }
    if (doBlur || doTilt || doBack)
    {
        BindTex(0, sceneTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);    // the half resolution copy averages 2x2 texels
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        BeginTarget(&sHalf[2]);
        DeckGl_UseProgram(&sCopy);
        DeckGl_Set1i(sCopyScene, 0);
        DeckGl_Set2f(sCopyTexel, 0.5f / (float)sScene.w, 0.5f / (float)sScene.h);
        DrawQuad();
        if (doBlur)
            Blur(&sHalf[2], &sHalf[1], 1, 1.0f);
        else if (doBack)
            Blur(&sHalf[2], &sHalf[1], kBackPasses[backLvl], kBackStep[backLvl]); // the tilt-shift gathers from the plain half-res copy
    }

    // Composite into the letterbox: same viewport call as the legacy present (DeckGlViewport maps
    // (0,0,960,544) on framebuffer 0 to the letterboxed rect and enables the scissor).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    DeckGlViewport(0, 0, in->renderW, in->renderH);
    BindTex(5, doShafts ? sHalf[3].color : 0);
    BindTex(4, doLut ? sLutTex : 0);
    BindTex(6, sunC != NULL ? sunC->depthTex : 0);
    BindTex(3, (doTilt || sunC != NULL || aoC || cloudsC) ? sScene.depthTex : 0);
    BindTex(7, aoC ? sAo[0].color : 0);
    BindTex(2, (doBlur || doTilt || doBack) ? sHalf[2].color : 0);
    BindTex(1, doBloom ? sHalf[0].color : 0);
    BindTex(0, sceneTex);   // the shaded picture when the pre-pass ran (it also feeds the blur copy and the window ray taps)
    // 960: one texel per logical pixel, NEAREST keeps the pixel art sharp. 1120 and 1280 are not integer multiples
    // of the letterbox rect, NEAREST would shimmer on diagonal edges: LINEAR (the target is near 1:1 on the Deck).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sScene.w == DECK_BASE_W ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sScene.w == DECK_BASE_W ? GL_NEAREST : GL_LINEAR);
    float wgt[3];
    GradeTints(in->daylight, in->glow, shadow, high, wgt);
    DeckGl_UseProgram(&sComposite);
    DeckGl_Set1i(sCScene, 0);
    DeckGl_Set1i(sCBloom, 1);
    DeckGl_Set1i(sCBlur, 2);
    DeckGl_Set1i(sCDepth, 3);
    DeckGl_Set1i(sCShafts, 5);
    DeckGl_Set1i(sCLut, 4);
    DeckGl_Set2f(sCLutOn, doLut ? 1.0f : 0.0f, keep);
    // The shadow terms are applied here only when the pre-pass did not run (zero strength otherwise: never twice).
    SetShadowUniforms(&sCSh, in, sunC, aoC ? aoU : noU, cloudsC ? cldU : noU);
    {
        static float sGainDbg = -1.0f; // debug aid: env DECK_SHAFT_GAIN=<multiplier> (to see the beam angles in a screenshot)

        if (sGainDbg < 0.0f)
        {
            const char *e = getenv("DECK_SHAFT_GAIN");

            sGainDbg = e != NULL ? (float)atof(e) : 1.0f;
        }
        DeckGl_Set1f(sCWinRays, winRays > 0.01f ? winRays : 0.0f);
        // light travels away from the sun/moon; the tap looks back toward the window, so it leans toward the light (x right)
        DeckGl_Set1f(sCWinSkew, in->beamX * 1.6f);
        {
            static int sWrLogs;

            if (winRays > 0.01f && sWrLogs < 3)
            {
                ++sWrLogs;
                DeckGl_Log("[WINRAYS] amount %.2f (frame %.2f)\n", winRays, in->winRaysAmount);
            }
        }
        DeckGl_Set4f(sCShaft, doShafts ? shaftAmt * kShaftGain[cfg.shaftLvl] * (scene ? kSceneShaft * sceneK : 1.0f) * keep * sGainDbg : 0.0f, 0.0f, 0.0f, 0.0f);
    }
    {
        const int tl = doTilt ? cfg.tiltLvl : 0;
        DeckGl_Set4f(sCTilt, doTilt ? 1.0f : 0.0f, in->focusDist, kTiltRadius[tl] * (scene ? kSceneTiltRadius : 1.0f), kTiltTaps[tl]);
        DeckGl_Set2f(sCTiltB, scene ? kSceneBandA : 0.06f, scene ? kSceneBandB : 0.40f);
        DeckGl_Set2f(sCZ, in->zNear, in->zFar);
    }
    DeckGl_Set1f(sCBloomAmt, doBloom ? in->bloom * bloomGain * keep : 0.0f);
    DeckGl_Set1f(sCBlurOn, doBlur ? 1.0f : 0.0f);
    DeckGl_Set2f(sCBack, doBack ? keep : 0.0f, doBack ? kBackDim[backLvl] * keep : ((scene && cfg.dimLvl >= 0) ? kSceneDim[cfg.sceneLvl] * keep : 0.0f));
    if (in->battle && in->bfxOn)
    {
        const DeckBattleFxFrame *const b = &in->bfx;

        DeckGl_Set4f(sCBF0, b->hit, b->aberration, b->flash, b->low);
        DeckGl_Set4f(sCBF1, b->tint[0], b->tint[1], b->tint[2], b->entry);
        DeckGl_Set4f(sCBF2, b->center[0], b->center[1], b->radius, 0.0f);
        DeckGl_Set4f(sCBF3, b->offset[0], b->offset[1], b->zoom, 0.0f);
    }
    else
    {
        DeckGl_Set4f(sCBF0, 0.0f, 0.0f, 0.0f, 0.0f);
        DeckGl_Set4f(sCBF1, 0.0f, 0.0f, 0.0f, 0.0f);
        DeckGl_Set4f(sCBF2, 0.5f, 0.5f, 0.1f, 0.0f);
        DeckGl_Set4f(sCBF3, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    // Legacy band height: VOXEL_DOF_*_ROWS * (renderH / 544) / renderH = rows / 544.
    DeckGl_Set4f(sCDof, in->dofTopRows / 544.0f, in->dofBottomRows / 544.0f, in->dofTopAlpha * blurMul, in->dofBottomAlpha * blurMul);
    DeckGl_Set1f(sCGradeAmt, cfg.grade ? pr->grade * keep : 0.0f);
    DeckGl_Set3f(sCShadow, shadow[0], shadow[1], shadow[2]);
    DeckGl_Set3f(sCHigh, high[0], high[1], high[2]);
    DeckGl_Set1f(sCSat, pr->saturation);
    DeckGl_Set1f(sCVignette, cfg.grade ? pr->vignette * keep : 0.0f);
    DeckGl_Set1f(sCAspect, (float)in->renderW / (float)in->renderH);
    DrawQuad();

    // Leave the state Pass_DepthOfField leaves (see deck_post.h).
    DeckGl_UseProgram(NULL);
    BindTex(7, 0);
    BindTex(6, 0);
    BindTex(5, 0);
    BindTex(4, 0);
    BindTex(3, 0);
    BindTex(2, 0);
    BindTex(1, 0);
    BindTex(0, 0);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_BLEND);

    DeckGl_TimerEnd();
    VitaPerf_DeckMarkPostEnd();
}
