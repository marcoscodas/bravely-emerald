#include "deck_sunshadow.h"
#include "deck_gl_shader.h"
#include "deck_levels.h"
#include "deck_post.h"
#include "platform/vita_extras.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---- Tuning constants (to judge by eye) ----
#define SS_HALF      32.0f   // half size of the shadow box in tiles (light space x and y)
#define SS_DIST      48.0f   // eye distance from the box centre along the sun direction
#define SS_RANGE     96.0f   // world depth covered by the map (depth 0..1)
#define SS_FACE_REF  0.90f   // the face term a lit surface carries: the shadow multiplier is the ambient over this
#define SS_MIN_SUN   0.04f   // below this sun strength no map is drawn (night)

static const int kMapSize[3] = { 1024, 2048, 2048 };      // by level
static const int kRadius[3] = { 0, 1, 2 };                 // PCF: 2x2, 3x3, 5x5 taps
static const float kSpacing[3] = { 1.0f, 1.0f, 1.25f };   // tap spacing in texels

static DeckGlTarget sMap;
static bool sOff;                // DECK_SUNSHADOW_OFF
static bool sEnvRead, sFailed;
static DeckSunResolve sRes;
static float sInvVP[16];
static bool sHaveCam;
static int sLoggedLevel = -2;

typedef float Mat4[16];          // column-major, m[col * 4 + row]

static void Mul4(Mat4 out, const float *a, const float *b)
{
    Mat4 r;

    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++)
        {
            float s = 0.0f;

            for (int k = 0; k < 4; k++)
                s += a[k * 4 + rw] * b[c * 4 + k];
            r[c * 4 + rw] = s;
        }
    memcpy(out, r, sizeof(r));
}

// General 4x4 inverse (cofactors). False when singular.
static bool Inv4(Mat4 out, const float *m)
{
    float inv[16], det;

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1.0e-20f)
        return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * det;
    return true;
}

static void ReadEnv(void)
{
    const char *e;

    if (sEnvRead)
        return;
    sEnvRead = true;
    e = getenv("DECK_SUNSHADOW_OFF");
    sOff = e != NULL && strcmp(e, "0") != 0;
    if (sOff)
        DeckGl_Log("[SUNSHADOW] off by DECK_SUNSHADOW_OFF\n");
}

int DeckSunShadow_Level(void)
{
    int lvl;

    ReadEnv();
    if (sOff || sFailed || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0)
        return -1;
    lvl = DeckLevel(FAST_EXTRA_VOXEL_SUNSHADOW);
    if (lvl != sLoggedLevel)
    {
        DeckGl_Log("[SUNSHADOW] level %d (-1 off, 0 low .. 2 high), post resolve %s\n", lvl, DeckPost_SunReady() ? "ready" : "NOT ready");
        sLoggedLevel = lvl;
    }
    return DeckPost_SunReady() ? lvl : -1;
}

bool DeckSunShadow_BakedOff(void)
{
    return DeckSunShadow_Level() >= 0;
}

void DeckSunShadow_Reset(void)
{
    sRes.active = false;
    sHaveCam = false;
}

void DeckSunShadow_CaptureCamera(void)
{
    Mat4 p, mv, vp;

    glGetFloatv(GL_PROJECTION_MATRIX, p);
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    Mul4(vp, p, mv);
    sHaveCam = Inv4(sInvVP, vp);
}

bool DeckSunShadow_CameraInvVP(float out[16])
{
    if (!sHaveCam)
        return false;
    memcpy(out, sInvVP, sizeof(sInvVP));
    return true;
}

static bool EnsureTarget(int size)
{
    if (sMap.fbo != 0 && sMap.w == size)
        return true;
    DeckGl_DeleteTarget(&sMap);
    if (!DeckGl_CreateTarget(&sMap, size, size, DECK_GL_DEPTH_TEXTURE24, GL_NEAREST) || sMap.depthTex == 0)
    {
        DeckGl_Log("[SUNSHADOW] %dx%d depth target failed: sun shadows off\n", size, size);
        DeckGl_DeleteTarget(&sMap);
        sFailed = true;
        return false;
    }
    DeckGl_Log("[SUNSHADOW] shadow map %dx%d ready\n", size, size);
    return true;
}

bool DeckSunShadow_Begin(float fx, float fy, float fz, float dx, float dz, float strength)
{
    const int lvl = DeckSunShadow_Level();
    float L[3], s[3], u[3], c[3], eye[3], len, texel, ps, pu;
    Mat4 vp;

    sRes.active = false;
    if (lvl < 0 || !sHaveCam || strength < SS_MIN_SUN)
        return false;
    if (!EnsureTarget(kMapSize[lvl]))
        return false;
    // Toward the sun: the baked rays climb one tile for every tile along (-dx, -dz) (RayPoint in voxel_lighting.c)
    L[0] = -dx; L[1] = 1.0f; L[2] = -dz;
    len = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    for (int i = 0; i < 3; i++)
        L[i] /= len;
    // light basis: s = up x L, u = L x s
    s[0] = L[2];  s[1] = 0.0f;  s[2] = -L[0];     // cross((0,1,0), L)
    len = sqrtf(s[0] * s[0] + s[2] * s[2]);
    if (len < 1.0e-4f)
        return false;
    s[0] /= len; s[2] /= len;
    u[0] = L[1] * s[2] - L[2] * s[1];
    u[1] = L[2] * s[0] - L[0] * s[2];
    u[2] = L[0] * s[1] - L[1] * s[0];
    // texel snap: the box centre moves in whole texels along s and u, so the shadows do not shimmer while the camera walks
    texel = 2.0f * SS_HALF / (float)sMap.w;
    ps = s[0] * fx + s[1] * fy + s[2] * fz;
    pu = u[0] * fx + u[1] * fy + u[2] * fz;
    {
        const float ss = floorf(ps / texel) * texel, su = floorf(pu / texel) * texel;

        for (int i = 0; i < 3; i++)
            c[i] = (i == 0 ? fx : (i == 1 ? fy : fz)) + s[i] * (ss - ps) + u[i] * (su - pu);
    }
    for (int i = 0; i < 3; i++)
        eye[i] = c[i] + L[i] * SS_DIST;
    {
        const float es = s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2];
        const float eu = u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2];
        const float el = L[0] * eye[0] + L[1] * eye[1] + L[2] * eye[2];
        // rows of the world -> clip matrix: x = s.(p-eye)/H, y = u.(p-eye)/H, z = 2 * (-(L.(p-eye)) / RANGE) - 1
        const float r0[4] = { s[0] / SS_HALF, s[1] / SS_HALF, s[2] / SS_HALF, -es / SS_HALF };
        const float r1[4] = { u[0] / SS_HALF, u[1] / SS_HALF, u[2] / SS_HALF, -eu / SS_HALF };
        const float r2[4] = { -2.0f * L[0] / SS_RANGE, -2.0f * L[1] / SS_RANGE, -2.0f * L[2] / SS_RANGE, 2.0f * el / SS_RANGE - 1.0f };

        for (int k = 0; k < 4; k++)
        {
            vp[k * 4 + 0] = r0[k];
            vp[k * 4 + 1] = r1[k];
            vp[k * 4 + 2] = r2[k];
            vp[k * 4 + 3] = k == 3 ? 1.0f : 0.0f;
        }
    }
    memcpy(sRes.sunVP, vp, sizeof(vp));
    memcpy(sRes.invVP, sInvVP, sizeof(sInvVP));
    memcpy(sRes.toSun, L, sizeof(L));
    sRes.depthTex = sMap.depthTex;
    sRes.amount = strength > 1.0f ? 1.0f : strength;
    {
        const float amb = 1.0f - (1.0f - 0.70f) * sRes.amount;   // VOXEL_AMBIENT 0.70 (voxel_lighting.h), as VoxelLighting_SetSun
        const float m = amb / SS_FACE_REF;

        sRes.mul = m > 1.0f ? 1.0f : m;
    }
    sRes.texelWorld = texel;
    sRes.texelUv = 1.0f / (float)sMap.w;
    sRes.range = SS_RANGE;
    sRes.spacing = kSpacing[lvl];
    sRes.radius = kRadius[lvl];

    glBindFramebuffer(GL_FRAMEBUFFER, sMap.fbo);
    glViewport(0, 0, sMap.w, sMap.h);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(vp);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    sRes.active = true;
    return true;
}

void DeckSunShadow_End(void)
{
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

const DeckSunResolve *DeckSunShadow_Resolve(void)
{
    return sRes.active ? &sRes : NULL;
}

void DeckSunShadow_Shutdown(void)
{
    DeckGl_DeleteTarget(&sMap);
    sRes.active = false;
}
