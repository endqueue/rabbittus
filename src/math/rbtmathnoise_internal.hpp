#pragma once

// RbtMath's noise, its parts below the batch calls (rbtmathnoise.h,
// "Definition"): the constants, tables, hash and octave record, and each
// kind's octave evaluated at one lattice coordinate, unscaled. Shared by
// rbtmathnoise.cpp and the scale constants' search
// (tests/rbtmathnoise_scale_search.cpp), which needs an octave's raw value.
//
// Every function is written in the Definition's order of operations, each
// float operation rounded once: the sources that include it are built with
// floating-point contraction off (CMakeLists.txt), and FLT_EVAL_METHOD is
// asserted 0. Nothing here is writable: the tables are constexpr, in
// read-only data.

#include "../rbtplaindata.hpp"
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>

static_assert(FLT_EVAL_METHOD == 0, "rbtmathnoise's bits need every float operation rounded to binary32 once");

namespace rbtnoise
{

// ---- Step 1 and 8: the hash ------------------------------------------------------

inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

constexpr uint32_t K[3]      = {0x9E3779B1u, 0x85EBCA77u, 0xC2B2AE3Du};
constexpr uint32_t KK[3]     = {0x632BE5ABu, 0x27D4EB2Fu, 0x165667B1u};
constexpr uint32_t FLIP      = 0xCC9E2D51u;
constexpr uint32_t ID_SALT   = 0x5BD1E995u;
constexpr uint32_t AXIS_A[3] = {0x68E31DA4u, 0xB5297A4Du, 0x1B56C4E9u};

inline uint32_t axis_term(int axis, int64_t c)
{
    return ((uint32_t)c * K[axis]) ^ ((uint32_t)((uint64_t)c >> 32) * KK[axis]);
}

// The Euclidean remainder: a period wraps -1 to period - 1. 0 is no period.
inline int64_t wrap(int64_t c, int64_t period)
{
    if (period == 0)
        return c;
    int64_t r = c % period;
    return r < 0 ? r + period : r;
}

inline uint32_t lattice_hash2(uint32_t s, int64_t c0, int64_t c1, const int64_t period[3])
{
    return hash32(s ^ axis_term(0, wrap(c0, period[0])) ^ axis_term(1, wrap(c1, period[1])));
}

inline uint32_t lattice_hash3(uint32_t s, int64_t c0, int64_t c1, int64_t c2, const int64_t period[3])
{
    return hash32(s ^ axis_term(0, wrap(c0, period[0])) ^ axis_term(1, wrap(c1, period[1])) ^ axis_term(2, wrap(c2, period[2])));
}

// ---- Step 2: seeds -------------------------------------------------------------

inline uint32_t octave_x(uint32_t i) { return hash32(0x9E3779B9u * (i + 1u)); }
inline uint32_t channel_y(uint32_t a) { return hash32(0x632BE5ABu * (a + 1u)); }

// ---- Constants (Definition, CONSTANTS) -----------------------------------------------

constexpr double F2   = 0x1.76cf5d0b09954p-2;
constexpr float G     = -0x1.b0cb18p-3f;
constexpr double GD   = -0x1.b0cb174df99c8p-3;
constexpr float E1    = 0x1.93cd3ap-1f;
constexpr float E11   = 0x1.279a74p-1f;
constexpr double T23  = 0x1.5555555555555p-1;
constexpr float T23F  = 0x1.555556p-1f;
constexpr double R3   = 0x1.279a74590331dp-1;
constexpr float R3F   = 0x1.279a74p-1f;
constexpr float K2    = 0x1p-1f;
constexpr float K3    = 0x1.333334p-1f;
constexpr float SIGN0 = 0x1p-100f; // RIDGED's sign is 0 below it

// The scale constants: the search's (tests/rbtmathnoise_scale_search.cpp),
// written into rbtmathnoise.h's Definition as well. Each the float just
// below 1 / the octave's maximum (RBTMATH_TODO.md has the search's output).
constexpr float SCALE_GRADIENT_2D = 0x1.6cbdaap+0f; // 1 / 0.70186810806957944
constexpr float SCALE_GRADIENT_3D = 0x1.ee0a2ep-1f; // 1 / 1.0363538112118027
constexpr float SCALE_SIMPLEX_2D  = 0x1.8f58eep+6f; // 1 / 0.010016341581612447
constexpr float SCALE_SIMPLEX_3D  = 0x1.91838ap+3f; // 1 / 0.07969837445285613

// ---- Tables (Definition, TABLES) -------------------------------------------------------

// Perlin's sixteen, as his reference grad(): for each, the two non-zero axes
// in axis order and their signs, so a dot is (+-d_u) + (+-d_v).
struct Perlin
{
    float g[3];
    int8_t u, v;
    float su, sv;
};
constexpr Perlin PERLIN16[16] = {
    {{1, 1, 0}, 0, 1, 1, 1},     {{-1, 1, 0}, 0, 1, -1, 1},  {{1, -1, 0}, 0, 1, 1, -1},  {{-1, -1, 0}, 0, 1, -1, -1},
    {{1, 0, 1}, 0, 2, 1, 1},     {{-1, 0, 1}, 0, 2, -1, 1},  {{1, 0, -1}, 0, 2, 1, -1},  {{-1, 0, -1}, 0, 2, -1, -1},
    {{0, 1, 1}, 1, 2, 1, 1},     {{0, -1, 1}, 1, 2, -1, 1},  {{0, 1, -1}, 1, 2, 1, -1},  {{0, -1, -1}, 1, 2, -1, -1},
    {{1, 1, 0}, 0, 1, 1, 1},     {{0, -1, 1}, 1, 2, -1, 1},  {{-1, 1, 0}, 0, 1, -1, 1},  {{0, -1, -1}, 1, 2, -1, -1},
};

// The 24 directions at (7.5 + 15 k) degrees, from the floats nearest
// sin 7.5, 22.5, 37.5, 52.5, 67.5 and 82.5 (cos x = sin(90 - x)).
constexpr float S1 = 0x1.0b515p-3f, S2 = 0x1.87de2ap-2f, S3 = 0x1.37af94p-1f;
constexpr float S4 = 0x1.963268p-1f, S5 = 0x1.d906bcp-1f, S6 = 0x1.fb9eaap-1f;
constexpr float DIR24[24][2] = {
    {S6, S1},   {S5, S2},   {S4, S3},   {S3, S4},   {S2, S5},   {S1, S6},
    {-S1, S6},  {-S2, S5},  {-S3, S4},  {-S4, S3},  {-S5, S2},  {-S6, S1},
    {-S6, -S1}, {-S5, -S2}, {-S4, -S3}, {-S3, -S4}, {-S2, -S5}, {-S1, -S6},
    {S1, -S6},  {S2, -S5},  {S3, -S4},  {S4, -S3},  {S5, -S2},  {S6, -S1},
};

// OpenSimplex2's 48, built from the Definition's rule.
constexpr float A48 = 0x1.1cc47p+1f, B48 = 0x1.8b0ac8p+1f, C48 = 0x1.2c121cp+0f;
struct Dir48Table
{
    float d[48][3];
    constexpr Dir48Table() : d{}
    {
        for (int k = 0; k < 48; k++)
        {
            if (k < 24)
            {
                int m = k / 8, b = k % 8;
                int lo = m == 0 ? 1 : 0, hi = m == 2 ? 1 : 2;
                d[k][m]  = (b & 1) ? -1.0f : 1.0f;
                d[k][lo] = (b & 2) ? -A48 : A48;
                d[k][hi] = (b & 4) ? -A48 : A48;
            }
            else
            {
                int j = k - 24, m = j / 8, b = j % 8;
                int lo = m == 0 ? 1 : 0, hi = m == 2 ? 1 : 2;
                float lov = (b & 1) ? C48 : B48, hiv = (b & 1) ? B48 : C48;
                d[k][m]  = 0.0f;
                d[k][lo] = (b & 2) ? -lov : lov;
                d[k][hi] = (b & 4) ? -hiv : hiv;
            }
        }
    }
};
constexpr Dir48Table DIR48{};

// ---- Step 7: the split ---------------------------------------------------------------

inline void split(double u, int64_t &c, float &t)
{
    const double f = std::floor(u);
    c              = (int64_t)f;
    t              = (float)(std::floor((u - f) * 0x1p24) * 0x1p-24);
}

// ---- An octave of each kind, unscaled -----------------------------------------------
//
// Each takes the octave's lattice coordinate (cells c, fractions t: the
// split of step 7, after simplex's skew or turn), returns the raw value and
// writes its gradient in the same frame (per cell; simplex 3D's in the
// turned frame).

inline float fade(float t) { return ((t * t) * t) * ((t * ((t * 6.0f) - 15.0f)) + 10.0f); }
inline float fade_slope(float t) { return ((t * t) * 30.0f) * ((t * (t - 2.0f)) + 1.0f); }

inline float gradient2_raw(uint32_t s, const int64_t c[3], const float t[3], const int64_t period[3], float g[3])
{
    float q[4], qg[4][2];
    for (int k = 0; k < 4; k++)
    {
        const int kx = k & 1, ky = k >> 1;
        const uint32_t h = lattice_hash2(s, c[0] + kx, c[1] + ky, period);
        const float *dir = DIR24[(h >> 8) % 24u];
        const float d0 = t[0] - (float)kx, d1 = t[1] - (float)ky;
        q[k]     = (dir[0] * d0) + (dir[1] * d1);
        qg[k][0] = dir[0];
        qg[k][1] = dir[1];
    }
    const float sx = fade(t[0]), sy = fade(t[1]);
    const float sxp = fade_slope(t[0]), syp = fade_slope(t[1]);
    // x-lerps: (q0, q1) at y = 0, (q2, q3) at y = 1.
    float l[2], lg[2][2];
    for (int j = 0; j < 2; j++)
    {
        const float A = q[2 * j], B = q[2 * j + 1];
        l[j]     = A + (sx * (B - A));
        lg[j][0] = (qg[2 * j][0] + (sx * (qg[2 * j + 1][0] - qg[2 * j][0]))) + (sxp * (B - A));
        lg[j][1] = qg[2 * j][1] + (sx * (qg[2 * j + 1][1] - qg[2 * j][1]));
    }
    const float A = l[0], B = l[1];
    g[0] = lg[0][0] + (sy * (lg[1][0] - lg[0][0]));
    g[1] = (lg[0][1] + (sy * (lg[1][1] - lg[0][1]))) + (syp * (B - A));
    g[2] = 0.0f;
    return A + (sy * (B - A));
}

inline float gradient3_raw(uint32_t s, const int64_t c[3], const float t[3], const int64_t period[3], float g[3])
{
    float q[8], qg[8][3];
    for (int k = 0; k < 8; k++)
    {
        const int kx = k & 1, ky = (k >> 1) & 1, kz = k >> 2;
        const uint32_t h = lattice_hash3(s, c[0] + kx, c[1] + ky, c[2] + kz, period);
        const Perlin &p  = PERLIN16[h >> 28];
        const float d[3] = {t[0] - (float)kx, t[1] - (float)ky, t[2] - (float)kz};
        q[k]             = (p.su * d[p.u]) + (p.sv * d[p.v]);
        qg[k][0]         = p.g[0];
        qg[k][1]         = p.g[1];
        qg[k][2]         = p.g[2];
    }
    const float sx = fade(t[0]), sy = fade(t[1]), sz = fade(t[2]);
    const float slope[3] = {fade_slope(t[0]), fade_slope(t[1]), fade_slope(t[2])};
    // One lerp along axis `axis`, its gradient: along its own axis the fade's
    // slope term is added, along another not.
    auto lerp = [&](float A, const float *dA, float B, const float *dB, float sv, int axis, float *out_g) {
        for (int a = 0; a < 3; a++)
        {
            const float base = dA[a] + (sv * (dB[a] - dA[a]));
            out_g[a]         = a == axis ? base + (slope[axis] * (B - A)) : base;
        }
        return A + (sv * (B - A));
    };
    float lx[4], lxg[4][3];
    for (int yz = 0; yz < 4; yz++) // yz = 00, 10, 01, 11
        lx[yz] = lerp(q[2 * yz], qg[2 * yz], q[2 * yz + 1], qg[2 * yz + 1], sx, 0, lxg[yz]);
    float ly[2], lyg[2][3];
    for (int z = 0; z < 2; z++)
        ly[z] = lerp(lx[2 * z], lxg[2 * z], lx[2 * z + 1], lxg[2 * z + 1], sy, 1, lyg[z]);
    return lerp(ly[0], lyg[0], ly[1], lyg[1], sz, 2, g);
}

// A kernel term (both simplex kinds): a lattice point at offset d from the
// place with gradient q, kernel K.
template <int D>
inline void kernel_term(const float *q, const float *d, float K, float &value, float *grad)
{
    float a = (K - (d[0] * d[0])) - (d[1] * d[1]);
    if (D == 3)
        a = a - (d[2] * d[2]);
    if (!(a > 0.0f))
        return;
    float p = (q[0] * d[0]) + (q[1] * d[1]);
    if (D == 3)
        p = p + (q[2] * d[2]);
    const float aa = a * a;
    const float w  = aa * aa;
    value          = value + (w * p);
    const float m  = (aa * a) * (p * -8.0f);
    for (int x = 0; x < D; x++)
        grad[x] = grad[x] + ((w * q[x]) + (m * d[x]));
}

inline float simplex2_raw(uint32_t s, const int64_t c[3], const float t[3], float g[3])
{
    const float tau = (t[0] + t[1]) * G;
    const float e0 = t[0] + tau, e1 = t[1] + tau;
    float value = 0.0f;
    g[0] = g[1] = g[2] = 0.0f;
    const int64_t no_period[3] = {0, 0, 0};
    {
        const float d[2] = {e0, e1};
        kernel_term<2>(DIR24[(lattice_hash2(s, c[0], c[1], no_period) >> 8) % 24u], d, K2, value, g);
    }
    if (e1 > e0)
    {
        const float d[2] = {e0 - G, e1 - E1};
        kernel_term<2>(DIR24[(lattice_hash2(s, c[0], c[1] + 1, no_period) >> 8) % 24u], d, K2, value, g);
    }
    else
    {
        const float d[2] = {e0 - E1, e1 - G};
        kernel_term<2>(DIR24[(lattice_hash2(s, c[0] + 1, c[1], no_period) >> 8) % 24u], d, K2, value, g);
    }
    {
        const float d[2] = {e0 - E11, e1 - E11};
        kernel_term<2>(DIR24[(lattice_hash2(s, c[0] + 1, c[1] + 1, no_period) >> 8) % 24u], d, K2, value, g);
    }
    return value;
}

// In the turned frame: c, t the split of the turned u'. Its gradient in that
// frame; the caller turns it back.
inline float simplex3_raw(uint32_t s, const int64_t c[3], const float t[3], float g[3])
{
    float value = 0.0f;
    g[0] = g[1] = g[2] = 0.0f;
    const int64_t no_period[3] = {0, 0, 0};
    for (int l = 0; l < 2; l++)
    {
        int64_t lc[3];
        float lt[3];
        for (int a = 0; a < 3; a++)
        {
            if (l == 0)
            {
                lc[a] = c[a];
                lt[a] = t[a];
            }
            else if (t[a] >= 0.5f)
            {
                lc[a] = c[a];
                lt[a] = t[a] - 0.5f;
            }
            else
            {
                lc[a] = c[a] - 1;
                lt[a] = t[a] + 0.5f;
            }
        }
        const uint32_t ls = l == 0 ? s : (s ^ FLIP);
        for (int k = 0; k < 8; k++)
        {
            const int kx = k & 1, ky = (k >> 1) & 1, kz = k >> 2;
            const float d[3] = {lt[0] - (float)kx, lt[1] - (float)ky, lt[2] - (float)kz};
            // The kernel's own test first: the hash only for a point inside.
            const float a = ((K3 - (d[0] * d[0])) - (d[1] * d[1])) - (d[2] * d[2]);
            if (!(a > 0.0f))
                continue;
            const uint32_t h = lattice_hash3(ls, lc[0] + kx, lc[1] + ky, lc[2] + kz, no_period);
            kernel_term<3>(DIR48.d[(h >> 8) % 48u], d, K3, value, g);
        }
    }
    return value;
}

// ---- Cellular --------------------------------------------------------------------------

// The two nearest features: F1 and F2 in cells, their ids, their offsets e
// from the place, and the cells' places in the search order, for ties.
struct Nearest
{
    float f[2];
    uint32_t id[2];
    float e[2][3];
    int order[2];
};

// distance: 1 Euclidean, 2 Manhattan, 3 Chebyshev (the enum's values).
template <int D>
inline float cell_distance(int distance, const float e[3])
{
    if (distance == 1)
    {
        float q = (e[0] * e[0]) + (e[1] * e[1]);
        if (D == 3)
            q = q + (e[2] * e[2]);
        return std::sqrt(q);
    }
    if (distance == 2)
    {
        float m = std::fabs(e[0]) + std::fabs(e[1]);
        if (D == 3)
            m = m + std::fabs(e[2]);
        return m;
    }
    float m = std::fabs(e[0]);
    if (std::fabs(e[1]) > m)
        m = std::fabs(e[1]);
    if (D == 3 && std::fabs(e[2]) > m)
        m = std::fabs(e[2]);
    return m;
}

// Whether (d, order) comes before (d1, order1): nearer, or as near and
// earlier in the search order.
inline bool before(float d, int order, float d1, int order1) { return d < d1 || (d == d1 && order < order1); }

template <int D>
inline void consider(Nearest &n, uint32_t s, const int64_t c[3], const float t[3], const int64_t period[3], float jitter,
                     int distance, int dx, int dy, int dz)
{
    const int delta[3] = {dx, dy, dz};
    const uint32_t h   = D == 3 ? lattice_hash3(s, c[0] + dx, c[1] + dy, c[2] + dz, period) : lattice_hash2(s, c[0] + dx, c[1] + dy, period);
    float e[3]         = {0.0f, 0.0f, 0.0f};
    for (int b = 0; b < D; b++)
    {
        const float f = 0.5f + (jitter * (((float)((h >> (10 * b)) & 1023u) * 0x1p-10f) - 0.5f));
        e[b]          = ((float)delta[b] + f) - t[b];
    }
    const float d   = cell_distance<D>(distance, e);
    const int order = D == 3 ? ((dz + 2) * 5 + (dy + 2)) * 5 + (dx + 2) : (dy + 2) * 5 + (dx + 2);
    if (before(d, order, n.f[0], n.order[0]))
    {
        n.f[1]     = n.f[0];
        n.id[1]    = n.id[0];
        n.order[1] = n.order[0];
        for (int b = 0; b < 3; b++)
            n.e[1][b] = n.e[0][b];
        n.f[0]     = d;
        n.id[0]    = hash32(h ^ ID_SALT);
        n.order[0] = order;
        for (int b = 0; b < 3; b++)
            n.e[0][b] = e[b];
    }
    else if (before(d, order, n.f[1], n.order[1]))
    {
        n.f[1]     = d;
        n.id[1]    = hash32(h ^ ID_SALT);
        n.order[1] = order;
        for (int b = 0; b < 3; b++)
            n.e[1][b] = e[b];
    }
}

// The true two nearest over the 5^D cells round the place's own: the 3^D
// first, then any outer cell whose nearest possible feature (its feature
// anywhere in the cell) isn't farther than F2. The result is the full
// search's, whatever is skipped, since a skipped cell is proved farther.
template <int D>
inline void cellular_nearest(uint32_t s, const int64_t c[3], const float t[3], const int64_t period[3], float jitter,
                             int distance, Nearest &n)
{
    n.f[0] = n.f[1] = INFINITY;
    n.order[0] = n.order[1] = 1 << 30;
    n.id[0] = n.id[1] = 0;
    const int zr = D == 3 ? 1 : 0;
    for (int dz = -zr; dz <= zr; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                consider<D>(n, s, c, t, period, jitter, distance, dx, dy, dz);
    const int zo = D == 3 ? 2 : 0;
    for (int dz = -zo; dz <= zo; dz++)
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
            {
                const int delta[3] = {dx, dy, dz};
                bool outer = false;
                for (int b = 0; b < D; b++)
                    outer = outer || delta[b] == 2 || delta[b] == -2;
                if (!outer)
                    continue;
                // The nearest any feature of that cell can be, per axis, in
                // double, and a margin for the float distance's rounding.
                double lb[3] = {0.0, 0.0, 0.0};
                for (int b = 0; b < D; b++)
                {
                    const int dd = delta[b];
                    lb[b] = dd > 0 ? (double)dd - (double)t[b] : (dd < 0 ? (double)(-dd) - 1.0 + (double)t[b] : 0.0);
                }
                double bound;
                if (distance == 1)
                    bound = std::sqrt(lb[0] * lb[0] + lb[1] * lb[1] + lb[2] * lb[2]);
                else if (distance == 2)
                    bound = lb[0] + lb[1] + lb[2];
                else
                    bound = lb[0] > lb[1] ? (lb[0] > lb[2] ? lb[0] : lb[2]) : (lb[1] > lb[2] ? lb[1] : lb[2]);
                if (bound > (double)n.f[1] * (1.0 + 0x1p-20) + 0x1p-40)
                    continue;
                consider<D>(n, s, c, t, period, jitter, distance, dx, dy, dz);
            }
}

// The gradient of a feature's distance F per cell of the octave, from the
// offset e to it (Definition, CELLULAR).
template <int D>
inline void feature_gradient(int distance, const float e[3], float F, float g[3])
{
    g[0] = g[1] = g[2] = 0.0f;
    if (F == 0.0f)
        return;
    auto sign = [](float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); };
    if (distance == 1)
    {
        for (int b = 0; b < D; b++)
            g[b] = (-e[b]) / F;
    }
    else if (distance == 2)
    {
        for (int b = 0; b < D; b++)
            g[b] = -sign(e[b]);
    }
    else
    {
        int m = 0;
        for (int b = 1; b < D; b++)
            if (std::fabs(e[b]) > std::fabs(e[m]))
                m = b;
        g[m] = -sign(e[m]);
    }
}

// ---- The octave table ------------------------------------------------------------------

// One octave's constants, made once per call on the stack. Fields read
// together per place (frequency, offsets, seed) first; the periods after.
// An array of structures: a place reads one octave's whole record at once.
struct Octave
{
    double frequency;  // f_i (step 3)
    int64_t period[3]; // octave i's periods in cells, 0 for none
    uint32_t seed;     // s_i (step 2)
    float amplitude;   // a_i
    float offset[3];   // o_i (step 4), exact as float
    float frequency_f; // F_i = (float)f_i
};
RBT_PLAIN_DATA(Octave);
RBT_HOT_RECORD(Octave, 64);

// ---- Reading a baked volume (rbtmathnoise.h, "Reading a volume") -----------------------
//
// The trilinear, repeating read every density over a baked volume is
// defined by: rbtmath_noise_volume_sample() and rbtmathfog.cpp's fog
// volumes call it, so both read the same bits. q in tiles, checked by the
// caller (finite, within RBTMATH_NOISE_VOLUME_MAX_COORDINATE); n the side.

inline void volume_axis(float q, uint32_t n, uint32_t &i0, uint32_t &i1, float &t)
{
    const float u   = (q * (float)n) - 0.5f;
    const float fl  = std::floor(u);
    const int64_t i = (int64_t)fl;
    t               = u - (float)i;
    const int64_t m = (int64_t)n;
    int64_t r       = i % m;
    if (r < 0)
        r += m;
    i0 = (uint32_t)r;
    i1 = (uint32_t)((r + 1) % m);
}

inline void volume_read(const uint8_t *texels, uint32_t n, const float q[3], float out[4])
{
    uint32_t x0, x1, y0, y1, z0, z1;
    float tx, ty, tz;
    volume_axis(q[0], n, x0, x1, tx);
    volume_axis(q[1], n, y0, y1, ty);
    volume_axis(q[2], n, z0, z1, tz);
    const std::size_t row   = (std::size_t)n * 4;
    const std::size_t slice = row * n;
    const uint8_t *c000 = texels + z0 * slice + y0 * row + (std::size_t)x0 * 4;
    const uint8_t *c100 = texels + z0 * slice + y0 * row + (std::size_t)x1 * 4;
    const uint8_t *c010 = texels + z0 * slice + y1 * row + (std::size_t)x0 * 4;
    const uint8_t *c110 = texels + z0 * slice + y1 * row + (std::size_t)x1 * 4;
    const uint8_t *c001 = texels + z1 * slice + y0 * row + (std::size_t)x0 * 4;
    const uint8_t *c101 = texels + z1 * slice + y0 * row + (std::size_t)x1 * 4;
    const uint8_t *c011 = texels + z1 * slice + y1 * row + (std::size_t)x0 * 4;
    const uint8_t *c111 = texels + z1 * slice + y1 * row + (std::size_t)x1 * 4;
    for (int c = 0; c < 4; c++)
    {
        const float v000 = (float)c000[c] / 255.0f, v100 = (float)c100[c] / 255.0f;
        const float v010 = (float)c010[c] / 255.0f, v110 = (float)c110[c] / 255.0f;
        const float v001 = (float)c001[c] / 255.0f, v101 = (float)c101[c] / 255.0f;
        const float v011 = (float)c011[c] / 255.0f, v111 = (float)c111[c] / 255.0f;
        const float x00  = v000 + (tx * (v100 - v000));
        const float x10  = v010 + (tx * (v110 - v010));
        const float x01  = v001 + (tx * (v101 - v001));
        const float x11  = v011 + (tx * (v111 - v011));
        const float y_0  = x00 + (ty * (x10 - x00));
        const float y_1  = x01 + (ty * (x11 - x01));
        out[c]           = y_0 + (tz * (y_1 - y_0));
    }
}

} // namespace rbtnoise
