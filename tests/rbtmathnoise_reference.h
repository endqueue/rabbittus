// An independent copy of rbtmathnoise.h's Definition, written from the
// header's text alone (not from src/), for the tests to check the library
// against: the hash, the octave table, the split, the tables, a cellular
// search of every cell within 3, and simplex noise summed over every lattice
// point within 3 cells in double. Shared by rbtmathnoise_tests.c and
// rbtmathnoise_bits_tests.c. Built with contraction off (CMakeLists.txt), as
// the library is, so its float steps round as the Definition says.
//
// Written in C, as the other tests of a C header are.

#ifndef RBTMATHNOISE_REFERENCE_H
#define RBTMATHNOISE_REFERENCE_H

#include "math/rbtmathnoise.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

// ---- Step 1: H -----------------------------------------------------------------

static uint32_t ref_h(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// ---- Step 2: seeds -------------------------------------------------------------

static uint32_t ref_x(uint32_t i) { return ref_h(0x9E3779B9u * (i + 1u)); }
static uint32_t ref_y(uint32_t a) { return ref_h(0x632BE5ABu * (a + 1u)); }
static uint32_t ref_octave_seed(uint32_t seed, uint32_t i) { return ref_h(seed ^ ref_x(i)); }

#define REF_FLIP 0xCC9E2D51u

// ---- Step 4: offsets -------------------------------------------------------------

static const uint32_t ref_axis_a[3] = {0x68E31DA4u, 0xB5297A4Du, 0x1B56C4E9u};

static double ref_offset(uint32_t octave_seed, int axis)
{
    return (double)(ref_h(octave_seed ^ ref_axis_a[axis]) >> 8) * 0x1p-24;
}

// ---- Step 7: the split -------------------------------------------------------------

static void ref_split(double u, int64_t *c, float *t)
{
    double f = floor(u);
    *c = (int64_t)f;
    *t = (float)(floor((u - f) * 0x1p24) * 0x1p-24);
}

// ---- Step 8: a lattice point's hash ----------------------------------------------------

static const uint32_t ref_k[3] = {0x9E3779B1u, 0x85EBCA77u, 0xC2B2AE3Du};
static const uint32_t ref_kk[3] = {0x632BE5ABu, 0x27D4EB2Fu, 0x165667B1u};

static uint32_t ref_p(int axis, int64_t c)
{
    return ((uint32_t)c * ref_k[axis]) ^ ((uint32_t)((uint64_t)c >> 32) * ref_kk[axis]);
}

static int64_t ref_wrap(int64_t c, int64_t period)
{
    if (period == 0)
        return c;
    int64_t r = c % period;
    return r < 0 ? r + period : r;
}

static uint32_t ref_hash(uint32_t s, int dims, const int64_t c[3], const int64_t period[3])
{
    uint32_t x = s;
    for (int a = 0; a < dims; a++)
        x ^= ref_p(a, ref_wrap(c[a], period ? period[a] : 0));
    return ref_h(x);
}

// ---- The constants and tables (Definition, CONSTANTS and TABLES) ---------------------

#define REF_F2 0x1.76cf5d0b09954p-2
#define REF_G (-0x1.b0cb18p-3f)
#define REF_GD (-0x1.b0cb174df99c8p-3)
#define REF_E1 0x1.93cd3ap-1f
#define REF_E11 0x1.279a74p-1f
#define REF_T23 0x1.5555555555555p-1
#define REF_T23F 0x1.555556p-1f
#define REF_R3 0x1.279a74590331dp-1
#define REF_R3F 0x1.279a74p-1f
#define REF_K2 0x1p-1f
#define REF_K3 0x1.333334p-1f

static const float ref_perlin16[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1},
};

static const float ref_sines[6] = {0x1.0b515p-3f, 0x1.87de2ap-2f, 0x1.37af94p-1f,
                                   0x1.963268p-1f, 0x1.d906bcp-1f, 0x1.fb9eaap-1f};

// The 24 directions at (7.5 + 15 k) degrees: (cos, sin), cos x = sin(90 - x).
static void ref_direction24(int k, float out[2])
{
    int quadrant = k / 6, j = k % 6;
    float c = ref_sines[5 - j], s = ref_sines[j];
    switch (quadrant)
    {
    case 0: out[0] = c; out[1] = s; break;
    case 1: out[0] = -s; out[1] = c; break;
    case 2: out[0] = -c; out[1] = -s; break;
    default: out[0] = s; out[1] = -c; break;
    }
}

#define REF_A48 0x1.1cc47p+1f
#define REF_B48 0x1.8b0ac8p+1f
#define REF_C48 0x1.2c121cp+0f

static void ref_direction48(int k, float out[3])
{
    if (k < 24)
    {
        int m = k / 8, b = k % 8;
        int lo = m == 0 ? 1 : 0, hi = m == 2 ? 1 : 2;
        out[m] = (b & 1) ? -1.0f : 1.0f;
        out[lo] = (b & 2) ? -REF_A48 : REF_A48;
        out[hi] = (b & 4) ? -REF_A48 : REF_A48;
    }
    else
    {
        int j = k - 24, m = j / 8, b = j % 8;
        int lo = m == 0 ? 1 : 0, hi = m == 2 ? 1 : 2;
        float lov = (b & 1) ? REF_C48 : REF_B48, hiv = (b & 1) ? REF_B48 : REF_C48;
        out[m] = 0.0f;
        out[lo] = (b & 2) ? -lov : lov;
        out[hi] = (b & 4) ? -hiv : hiv;
    }
}

// ---- The 3D simplex turn, in double ----------------------------------------------------

static void ref_turn(RBTMATH_NOISE_ORIENTATION o, const double u[3], double out[3])
{
    if (o == RBTMATH_NOISE_ORIENTATION_EVEN)
    {
        double r = ((u[0] + u[1]) + u[2]) * REF_T23;
        out[0] = r - u[0];
        out[1] = r - u[1];
        out[2] = r - u[2];
    }
    else if (o == RBTMATH_NOISE_ORIENTATION_Z_UP)
    {
        double w = u[0] + u[1], s = w * REF_GD, z = u[2] * REF_R3;
        out[0] = (u[0] + s) + z;
        out[1] = (u[1] + s) + z;
        out[2] = (w * -REF_R3) + z;
    }
    else
    {
        double w = u[0] + u[2], s = w * REF_GD, y = u[1] * REF_R3;
        out[0] = (u[0] + s) + y;
        out[2] = (u[2] + s) + y;
        out[1] = (w * -REF_R3) + y;
    }
}

// The turn's inverse (its transpose), in double: for building places on a
// plane of the turned frame.
static void ref_unturn(RBTMATH_NOISE_ORIENTATION o, const double v[3], double out[3])
{
    if (o == RBTMATH_NOISE_ORIENTATION_EVEN)
    {
        double r = ((v[0] + v[1]) + v[2]) * REF_T23;
        out[0] = r - v[0];
        out[1] = r - v[1];
        out[2] = r - v[2];
    }
    else if (o == RBTMATH_NOISE_ORIENTATION_Z_UP)
    {
        double w = v[0] + v[1], s = w * REF_GD, z = v[2] * REF_R3;
        out[0] = (v[0] + s) - z;
        out[1] = (v[1] + s) - z;
        out[2] = (w * REF_R3) + z;
    }
    else
    {
        double w = v[0] + v[2], s = w * REF_GD, y = v[1] * REF_R3;
        out[0] = (v[0] + s) - y;
        out[2] = (v[2] + s) - y;
        out[1] = (w * REF_R3) + y;
    }
}

// ---- Simplex summed over every lattice point within 3 cells, in double, unscaled ------
//
// The place in the octave's coordinate u (step 5's), before the skew or the
// turn: skewed or turned in double as step 6 does, split as step 7 does (the
// fraction to 24 bits), and every point of the lattice (2D: the triangular
// one; 3D: both cubic lattices) within 3 cells of that quantised place,
// whatever the kernel, summed in double. What differs from the library's
// value is its float arithmetic alone.

static double ref_simplex_full(int dims, RBTMATH_NOISE_ORIENTATION orientation, uint32_t s, const double u[3])
{
    double sum = 0.0;
    if (dims == 2)
    {
        double sk = (u[0] + u[1]) * REF_F2;
        double v[2] = {u[0] + sk, u[1] + sk};
        int64_t c[3] = {0, 0, 0};
        float t[2];
        ref_split(v[0], &c[0], &t[0]);
        ref_split(v[1], &c[1], &t[1]);
        for (int64_t j = -3; j <= 3; j++)
            for (int64_t i = -3; i <= 3; i++)
            {
                // The offset from the point (c + (i, j)) to the place
                // (c + t), in the skewed frame, then unskewed.
                double e0 = (double)t[0] - (double)i, e1 = (double)t[1] - (double)j;
                double un = (e0 + e1) * REF_GD;
                double d0 = e0 + un, d1 = e1 + un;
                double a = 0.5 - (d0 * d0 + d1 * d1);
                if (a <= 0)
                    continue;
                int64_t cc[3] = {c[0] + i, c[1] + j, 0};
                float q[2];
                ref_direction24((int)((ref_hash(s, 2, cc, NULL) >> 8) % 24u), q);
                sum += (a * a) * (a * a) * ((double)q[0] * d0 + (double)q[1] * d1);
            }
        return sum;
    }
    double v[3];
    ref_turn(orientation, u, v);
    int64_t c[3];
    float t[3];
    for (int a = 0; a < 3; a++)
        ref_split(v[a], &c[a], &t[a]);
    for (int l = 0; l < 2; l++)
    {
        double off = l ? 0.5 : 0.0;
        uint32_t ls = l ? (s ^ REF_FLIP) : s;
        for (int64_t k = -3; k <= 3; k++)
            for (int64_t j = -3; j <= 3; j++)
                for (int64_t i = -3; i <= 3; i++)
                {
                    // Lattice l's point indexed c + (i, j, k) sits at that
                    // plus off (lattice 1's index q at q + 0.5).
                    double d0 = (double)t[0] - ((double)i + off), d1 = (double)t[1] - ((double)j + off),
                           d2 = (double)t[2] - ((double)k + off);
                    double a = 0.6 - (d0 * d0 + d1 * d1 + d2 * d2);
                    if (a <= 0)
                        continue;
                    int64_t cc[3] = {c[0] + i, c[1] + j, c[2] + k};
                    float q[3];
                    ref_direction48((int)((ref_hash(ls, 3, cc, NULL) >> 8) % 48u), q);
                    sum += (a * a) * (a * a) * ((double)q[0] * d0 + (double)q[1] * d1 + (double)q[2] * d2);
                }
    }
    return sum;
}

// ---- Cellular: a search of every cell within 3, exactly as the Definition rounds -------

typedef struct ref_cell_result
{
    float f[2];          // F1, F2, in cells
    uint32_t id[2];
    float e[2][3];       // offsets from the place to each feature, in cells
    int order[2];        // the cells' places in the search order (for ties)
} ref_cell_result;

static float ref_cell_distance(RBTMATH_NOISE_CELL_DISTANCE kind, int dims, const float e[3])
{
    if (kind == RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN)
    {
        float q = (e[0] * e[0]) + (e[1] * e[1]);
        if (dims == 3)
            q = q + (e[2] * e[2]);
        return sqrtf(q);
    }
    if (kind == RBTMATH_NOISE_CELL_DISTANCE_MANHATTAN)
    {
        float m = fabsf(e[0]) + fabsf(e[1]);
        if (dims == 3)
            m = m + fabsf(e[2]);
        return m;
    }
    float m = fabsf(e[0]);
    if (fabsf(e[1]) > m)
        m = fabsf(e[1]);
    if (dims == 3 && fabsf(e[2]) > m)
        m = fabsf(e[2]);
    return m;
}

// The two nearest over the 7^d cells round the place's own, ties to the
// earlier cell (z, then y, then x, ascending).
static void ref_cellular(int dims, RBTMATH_NOISE_CELL_DISTANCE kind, float jitter, uint32_t s, const int64_t c[3],
                         const float t[3], const int64_t period[3], ref_cell_result *out)
{
    out->f[0] = out->f[1] = INFINITY;
    out->order[0] = out->order[1] = 1 << 30;
    int zr = dims == 3 ? 3 : 0;
    int order = 0;
    for (int dz = -zr; dz <= zr; dz++)
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++, order++)
            {
                int delta[3] = {dx, dy, dz};
                int64_t cc[3] = {c[0] + dx, c[1] + dy, c[2] + dz};
                uint32_t h = ref_hash(s, dims, cc, period);
                float e[3] = {0, 0, 0};
                for (int b = 0; b < dims; b++)
                {
                    float f = 0.5f + jitter * ((float)((h >> (10 * b)) & 1023u) * 0x1p-10f - 0.5f);
                    e[b] = ((float)delta[b] + f) - t[b];
                }
                float d = ref_cell_distance(kind, dims, e);
                uint32_t id = ref_h(h ^ 0x5BD1E995u);
                // Visited in ascending order, so a tie never displaces.
                if (d < out->f[0])
                {
                    out->f[1] = out->f[0];
                    out->id[1] = out->id[0];
                    memcpy(out->e[1], out->e[0], sizeof(out->e[0]));
                    out->order[1] = out->order[0];
                    out->f[0] = d;
                    out->id[0] = id;
                    memcpy(out->e[0], e, sizeof(e));
                    out->order[0] = order;
                }
                else if (d < out->f[1])
                {
                    out->f[1] = d;
                    out->id[1] = id;
                    memcpy(out->e[1], e, sizeof(e));
                    out->order[1] = order;
                }
            }
}

#endif // RBTMATHNOISE_REFERENCE_H
