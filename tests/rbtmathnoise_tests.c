// RbtMath's noise (include/math/rbtmathnoise.h; card A56, the approved
// draft's section 10, tests 1 to 13). Headless, no GPU.
//
// Each test checks the library against the Definition in the header,
// through tests/rbtmathnoise_reference.h (an independent copy written from
// the header's text), or against itself where a property is the point (a
// split batch, a period). Counts are reduced for ctest; --full runs the
// draft's counts (by hand, Release: RBTMATH_TODO.md has the run).
//
// Written in C, as the other tests of a C header are; built with contraction
// off, as the library is, since it works inputs out in floats too.

#include "jobs/rbtjobs.h"
#include "rbtmathnoise_reference.h"
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static bool full = false;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

// Stop a test that has already failed many times, so a broken build prints a
// screen, not a million lines.
#define CHECK_LIMITED(cond, counter)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            if ((counter)++ < 5)                                                                                       \
                printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

// Repeatable random numbers, the same on every platform.
static uint64_t rng_state = 0x2545F4914F6CDD1Dull;
static uint64_t next_u64(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static double next_unit(void) { return (double)(next_u64() >> 11) * 0x1p-53; }
static double next_range(double lo, double hi) { return lo + (hi - lo) * next_unit(); }

static RBTMATH_NOISE_DESC make_desc(uint32_t dims, RBTMATH_NOISE_KIND kind, RBTMATH_NOISE_FRACTAL fractal, uint32_t octaves)
{
    RBTMATH_NOISE_DESC d;
    memset(&d, 0, sizeof(d));
    d.base.type = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
    d.dimensions = dims;
    d.kind = kind;
    d.fractal = fractal;
    d.seed = 1234u;
    d.octaves = fractal == RBTMATH_NOISE_FRACTAL_SINGLE ? 1u : octaves;
    d.frequency = 1.0;
    if (fractal != RBTMATH_NOISE_FRACTAL_SINGLE)
    {
        d.lacunarity = 2.0;
        d.gain = 0.5f;
    }
    if (fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
        d.ridge_feedback = 2.0f;
    if (fractal == RBTMATH_NOISE_FRACTAL_ERODED)
        d.erosion = 1.0f;
    if (kind == RBTMATH_NOISE_KIND_CELLULAR)
    {
        d.cell_distance = RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN;
        d.cell_return = RBTMATH_NOISE_CELL_RETURN_F1;
        d.cell_jitter = 1.0f;
    }
    if (kind == RBTMATH_NOISE_KIND_SIMPLEX && dims == 3)
        d.orientation = RBTMATH_NOISE_ORIENTATION_EVEN;
    return d;
}

// S, the sum of the amplitudes, as the Definition makes them (floats).
static double amplitude_sum(const RBTMATH_NOISE_DESC *d)
{
    double s = 0.0;
    float a = 1.0f;
    for (uint32_t i = 0; i < d->octaves; i++)
    {
        s += (double)a;
        a = a * d->gain;
    }
    return s;
}

static float cell_diagonal(const RBTMATH_NOISE_DESC *d)
{
    if (d->cell_distance == RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN)
        return d->dimensions == 3 ? sqrtf(3.0f) : sqrtf(2.0f);
    if (d->cell_distance == RBTMATH_NOISE_CELL_DISTANCE_MANHATTAN)
        return (float)d->dimensions;
    return 1.0f;
}

// One octave's range [lo, hi], as rbtmathnoise.h states it per kind.
static void octave_range(const RBTMATH_NOISE_DESC *d, double *lo, double *hi)
{
    if (d->kind != RBTMATH_NOISE_KIND_CELLULAR)
    {
        *lo = d->fractal == RBTMATH_NOISE_FRACTAL_RIDGED ? 0.0 : -1.0;
        *hi = 1.0;
        return;
    }
    double dd = (double)cell_diagonal(d);
    switch (d->cell_return)
    {
    case RBTMATH_NOISE_CELL_RETURN_F1: *lo = 0.0; *hi = dd; break;
    case RBTMATH_NOISE_CELL_RETURN_F2: *lo = 0.0; *hi = 2.0 * dd; break;
    case RBTMATH_NOISE_CELL_RETURN_F2_MINUS_F1: *lo = 0.0; *hi = 2.0 * dd; break;
    default: *lo = -1.0; *hi = 1.0; break;
    }
}

static RBT_RESULT points(const RBTMATH_NOISE_DESC *d, const double *p, uint32_t n, float *v, float *g)
{
    RBTMATH_ERROR error;
    RBT_RESULT r = rbtmath_noise_compute_points(d, p, n, v, g, &error);
    if (r != RBT_RESULT_OK)
        printf("  (noise refused: %s)\n", error.message);
    return r;
}

// Every (kind, fractal, dimensions) a compute call takes, with cellular's
// returns and distances, for the tests that walk them all.
typedef struct config
{
    RBTMATH_NOISE_DESC desc;
    char name[96];
} config;

static uint32_t make_configs(config *out, uint32_t octaves)
{
    static const char *kinds[] = {"", "gradient", "simplex", "cellular"};
    static const char *fractals[] = {"", "single", "fbm", "ridged", "eroded"};
    uint32_t n = 0;
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind++)
            for (int fractal = 1; fractal <= 4; fractal++)
            {
                if (kind == RBTMATH_NOISE_KIND_CELLULAR && fractal > RBTMATH_NOISE_FRACTAL_FBM)
                    continue;
                int returns = kind == RBTMATH_NOISE_KIND_CELLULAR ? 4 : 1;
                int distances = kind == RBTMATH_NOISE_KIND_CELLULAR ? 3 : 1;
                int orientations = (kind == RBTMATH_NOISE_KIND_SIMPLEX && dims == 3) ? 3 : 1;
                for (int ret = 1; ret <= returns; ret++)
                    for (int dist = 1; dist <= distances; dist++)
                        for (int o = 1; o <= orientations; o++)
                        {
                            config *c = &out[n++];
                            c->desc = make_desc(dims, (RBTMATH_NOISE_KIND)kind, (RBTMATH_NOISE_FRACTAL)fractal, octaves);
                            if (kind == RBTMATH_NOISE_KIND_CELLULAR)
                            {
                                c->desc.cell_return = (RBTMATH_NOISE_CELL_RETURN)ret;
                                c->desc.cell_distance = (RBTMATH_NOISE_CELL_DISTANCE)dist;
                            }
                            if (orientations > 1)
                                c->desc.orientation = (RBTMATH_NOISE_ORIENTATION)o;
                            snprintf(c->name, sizeof(c->name), "%uD %s %s ret %d dist %d orient %d", dims, kinds[kind],
                                     fractals[fractal], kind == 3 ? ret : 0, kind == 3 ? dist : 0, orientations > 1 ? o : 0);
                        }
            }
    return n;
}

#define BATCH 4096u

// ---- 1. Ranges ------------------------------------------------------------------

static void test_ranges(void)
{
    static config configs[128];
    uint32_t nconfigs = make_configs(configs, 8);
    const uint64_t per = full ? 100000000ull : 200000ull;
    static double pts[BATCH * 3];
    static float vals[BATCH];
    for (uint32_t ci = 0; ci < nconfigs; ci++)
    {
        const RBTMATH_NOISE_DESC *d = &configs[ci].desc;
        double lo, hi;
        octave_range(d, &lo, &hi);
        double s = amplitude_sum(d);
        double rlo = lo * s, rhi = hi * s;
        float seen_lo = INFINITY, seen_hi = -INFINITY;
        uint64_t at_bound = 0, outside = 0;
        for (uint64_t done = 0; done < per; done += BATCH)
        {
            for (uint32_t i = 0; i < BATCH * d->dimensions; i++)
                pts[i] = next_range(-1000.0, 1000.0);
            if (points(d, pts, BATCH, vals, NULL) != RBT_RESULT_OK)
            {
                failures++;
                break;
            }
            for (uint32_t i = 0; i < BATCH; i++)
            {
                if (vals[i] < seen_lo)
                    seen_lo = vals[i];
                if (vals[i] > seen_hi)
                    seen_hi = vals[i];
                if ((double)vals[i] < rlo || (double)vals[i] > rhi)
                    outside++;
                // The clamp acts on an octave; on a single octave a value at
                // a bound of a scaled kind is the clamp's.
                if (d->fractal == RBTMATH_NOISE_FRACTAL_SINGLE && d->kind != RBTMATH_NOISE_KIND_CELLULAR &&
                    (vals[i] == 1.0f || vals[i] == -1.0f))
                    at_bound++;
            }
        }
        CHECK(outside == 0);
        CHECK(at_bound == 0);
        if (full || outside || at_bound)
            printf("  ranges %s: seen [%.7g, %.7g] of [%.7g, %.7g], %llu outside, %llu at a bound\n", configs[ci].name,
                   (double)seen_lo, (double)seen_hi, rlo, rhi, (unsigned long long)outside, (unsigned long long)at_bound);
    }
}

// ---- 2. Gradients against central differences ---------------------------------------
//
// In double places, at steps of 2^-8 and 2^-9 of the finest octave's cell.
// The draft names 1e-4 of a cell, but the Definition's float evaluation has
// a noise of about 2^-20 of a value (the fade's cancellation near t = 1), so
// at 1e-4 a central difference is mostly that noise; at 2^-8 it is a few
// parts in 10^4. The two steps agreeing says the field is smooth across them
// (no ridge's crest or cellular border inside); where they don't, the place
// is skipped.

static void octave_values(const RBTMATH_NOISE_DESC *d, uint32_t i, const double *p, uint32_t n, float *v, float *g);

static void test_gradients(void)
{
    static config configs[128];
    uint32_t nconfigs = make_configs(configs, 4);
    const int per = full ? 20000 : 300;
    int printed = 0;
    for (uint32_t ci = 0; ci < nconfigs; ci++)
    {
        const RBTMATH_NOISE_DESC *d = &configs[ci].desc;
        if (d->fractal == RBTMATH_NOISE_FRACTAL_ERODED)
            continue;
        uint32_t dims = d->dimensions;
        double finest = d->frequency;
        for (uint32_t i = 1; i < d->octaves; i++)
            finest *= d->lacunarity;
        const double h = 0x1p-8 / finest;
        const double noise = 0x1p-20 * amplitude_sum(d); // the evaluation's noise in a value
        for (int far_out = 0; far_out < 2; far_out++)
        {
            double worst = 0.0;
            int compared = 0;
            for (int n = 0; n < per; n++)
            {
                double p[3], q[3 * 13];
                for (uint32_t a = 0; a < dims; a++)
                    p[a] = next_range(-50.0, 50.0) + (far_out ? 1e7 : 0.0);
                // The place, then per axis +h, -h, +h/2, -h/2: one call.
                uint32_t count = 1 + 4 * dims;
                for (uint32_t k = 0; k < count; k++)
                    for (uint32_t a = 0; a < dims; a++)
                        q[k * dims + a] = p[a];
                for (uint32_t a = 0; a < dims; a++)
                {
                    q[(1 + 4 * a) * dims + a] = p[a] + h;
                    q[(2 + 4 * a) * dims + a] = p[a] - h;
                    q[(3 + 4 * a) * dims + a] = p[a] + h / 2;
                    q[(4 + 4 * a) * dims + a] = p[a] - h / 2;
                }
                float v[13], g[13 * 3];
                if (points(d, q, count, v, g) != RBT_RESULT_OK)
                {
                    failures++;
                    return;
                }
                if (d->kind == RBTMATH_NOISE_KIND_CELLULAR && d->cell_return == RBTMATH_NOISE_CELL_RETURN_CELL_VALUE)
                {
                    for (uint32_t a = 0; a < dims; a++)
                        CHECK(g[a] == 0.0f);
                    compared++;
                    continue;
                }
                // RIDGED has two kinks: a crest (an octave's n through 0),
                // and where an octave's weight for the next,
                // clamp(t_i * ridge_feedback, 0, 1), reaches the clamp's 1.
                // Skip a place where either is within its slope's reach
                // across the stencil. Test 12 holds RIDGED's chain rule.
                if (d->fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
                {
                    bool near_kink = false;
                    double fo = d->frequency;
                    double w = 1.0;
                    for (uint32_t o = 0; o < d->octaves && !near_kink; o++)
                    {
                        float on, og[3];
                        octave_values(d, o, p, 1, &on, og);
                        double slope = 0.0;
                        for (uint32_t a = 0; a < dims; a++)
                            slope += fabs((double)og[a]) * fo;
                        if (fabs((double)on) <= 2.0 * slope * h + 1e-6)
                            near_kink = true;
                        // t_i = (1 - |n_i|)^2 w_i; its slope at most
                        // 2 (1 - |n_i|) |dn_i| w_i, doubled for w_i's own.
                        const double r = 1.0 - fabs((double)on);
                        const double t = r * r * w;
                        const double feedback = (double)d->ridge_feedback;
                        const double t_slope = 4.0 * r * slope * w * feedback;
                        if (fabs(t * feedback - 1.0) <= 2.0 * t_slope * h + 1e-6)
                            near_kink = true;
                        w = t * feedback > 1.0 ? 1.0 : (t * feedback < 0.0 ? 0.0 : t * feedback);
                        fo *= d->lacunarity;
                    }
                    if (near_kink)
                        continue;
                }
                // Four one-sided slopes at h/2 spacing: on a smooth field their
                // successive differences change linearly (the third
                // difference is tiny); a kink in any interval breaks that.
                bool smooth = true;
                double numeric[3];
                for (uint32_t a = 0; a < dims; a++)
                {
                    const double vp1 = v[1 + 4 * a], vm1 = v[2 + 4 * a], vp2 = v[3 + 4 * a], vm2 = v[4 + 4 * a], v0 = v[0];
                    const double half = h / 2;
                    const double up1 = (vp1 - vp2) / half, up2 = (vp2 - v0) / half;
                    const double dn2 = (v0 - vm2) / half, dn1 = (vm2 - vm1) / half;
                    const double third1 = (up1 - up2) - (up2 - dn2), third2 = (up2 - dn2) - (dn2 - dn1);
                    double n1 = (vp1 - vm1) / (2.0 * h);
                    double n2 = (vp2 - vm2) / h;
                    double scale = fabs(n2) > finest ? fabs(n2) : finest;
                    double allowed = 5e-4 * scale + 8.0 * noise / half;
                    if (fabs(third1) > allowed || fabs(third2) > allowed)
                        smooth = false;
                    // Two kinks in the stencil can cancel in the third
                    // differences (a cellular F2 - F1 at 1e7 did); their
                    // second differences still show them. A smooth field's
                    // slope changes about 2 pi 2^-9 (1.2%) over h / 2 at
                    // the finest octave: past 5%, a kink.
                    const double second[3] = {up1 - up2, up2 - dn2, dn2 - dn1};
                    for (int k = 0; k < 3; k++)
                        if (fabs(second[k]) > 5e-2 * scale + 8.0 * noise / half)
                            smooth = false;
                    numeric[a] = (4.0 * n2 - n1) / 3.0; // Richardson: the step's h^2 term gone
                }
                if (!smooth)
                    continue;
                compared++;
                for (uint32_t a = 0; a < dims; a++)
                {
                    double analytic = (double)g[a];
                    double scale = fabs(analytic) > finest ? fabs(analytic) : finest;
                    double err = fabs(numeric[a] - analytic) / scale;
                    double allowed = 1e-3 + 4.0 * noise / h / scale;
                    if (err > worst)
                        worst = err;
                    if (err > allowed)
                    {
                        if (printed++ < 8)
                            printf("FAIL gradient %s %s axis %u at (%.17g, %.17g, %.17g): analytic %.9g numeric %.9g\n",
                                   configs[ci].name, far_out ? "far" : "near", a, p[0], p[1], dims > 2 ? p[2] : 0.0, analytic,
                                   numeric[a]);
                        failures++;
                    }
                }
            }
            if (compared <= per / 4)
                printf("FAIL gradient %s %s: only %d of %d places smooth enough to compare\n", configs[ci].name,
                       far_out ? "far" : "near", compared, per);
            CHECK(compared > per / 4);
            if (full)
                printf("  gradients %s %s: %d compared, worst %.3g relative\n", configs[ci].name, far_out ? "at 1e7" : "near",
                       compared, worst);
        }
    }
}

// ---- 3. Continuity across cell borders ---------------------------------------------

// The largest step between neighbouring places along a line, against the
// gradient's bound there.
static void walk_line(const RBTMATH_NOISE_DESC *d, const double start[3], const double dir[3], double length, uint32_t steps,
                      const char *what)
{
    uint32_t dims = d->dimensions;
    static double p[BATCH * 3];
    static float v[BATCH], g[BATCH * 3];
    double step = length / steps;
    float last_v = 0.0f, last_g[3] = {0, 0, 0};
    int bad = 0;
    for (uint32_t done = 0; done < steps; done += BATCH)
    {
        uint32_t n = steps - done < BATCH ? steps - done : BATCH;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t a = 0; a < dims; a++)
                p[i * dims + a] = start[a] + dir[a] * (step * (double)(done + i));
        if (points(d, p, n, v, g) != RBT_RESULT_OK)
        {
            failures++;
            return;
        }
        for (uint32_t i = 0; i < n; i++)
        {
            if (done + i > 0)
            {
                double slope = 0.0;
                for (uint32_t a = 0; a < dims; a++)
                {
                    double ga = fabs((double)g[i * dims + a]), la = fabs((double)last_g[a]);
                    slope += (ga > la ? ga : la) * fabs(dir[a]);
                }
                // The place's own resolution: past 2^28 cells the fraction's
                // (the header's precision table), set by where the library
                // rounds in double. Each octave's cell coordinate is
                // p * f_i, and simplex's skew adds the coordinates' sum to
                // each, so a coordinate is rounded at up to (1 + dims) times
                // the largest |p| f_i: four of its spacings, in places
                // (/ f_i), at the octave where that is largest.
                double largest = 0.0;
                for (uint32_t a = 0; a < dims; a++)
                    if (fabs(p[i * dims + a]) > largest)
                        largest = fabs(p[i * dims + a]);
                double reach = 0.0;
                double f = d->frequency;
                uint32_t octaves = d->octaves > 0 ? d->octaves : 1u;
                for (uint32_t o = 0; o < octaves; o++, f *= d->lacunarity)
                {
                    double at = largest * f * (double)(1 + dims);
                    double spacing = at > 0.0 ? ldexp(1.0, ilogb(at) - 52) : 0.0;
                    if (4.0 * spacing / f > reach)
                        reach = 4.0 * spacing / f;
                }
                reach += 0x1p-24 / d->frequency;
                double allowed = 2.0 * slope * (step + reach) + 64.0 * (double)FLT_EPSILON * (1.0 + fabs((double)v[i]));
                double jump = fabs((double)v[i] - (double)last_v);
                if (jump > allowed)
                {
                    if (bad++ < 3)
                        printf("FAIL continuity %s: a step of %.9g where %.9g is allowed\n", what, jump, allowed);
                    failures++;
                }
            }
            last_v = v[i];
            for (uint32_t a = 0; a < dims; a++)
                last_g[a] = g[i * dims + a];
        }
    }
}

static void test_continuity(void)
{
    const uint32_t steps = full ? 4000000u : 200000u;
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind++)
            for (int fractal = 1; fractal <= 2; fractal++)
            {
                RBTMATH_NOISE_DESC d = make_desc(dims, (RBTMATH_NOISE_KIND)kind, (RBTMATH_NOISE_FRACTAL)fractal, 4);
                if (kind == RBTMATH_NOISE_KIND_CELLULAR)
                    d.cell_return = RBTMATH_NOISE_CELL_RETURN_F1; // continuous; F2 - F1 too, CELL_VALUE not
                double dir[3] = {0.6, 0.48, 0.64};
                if (dims == 2)
                {
                    dir[0] = 0.6;
                    dir[1] = 0.8;
                    dir[2] = 0;
                }
                // Lines across many borders near the origin, at -1/0 and at
                // 2^32 - 1/2^32 in cells (frequency 1 here).
                const double starts[3] = {-3.3, 4294967293.7, -4294967298.1};
                for (int s = 0; s < 3; s++)
                {
                    double start[3] = {starts[s], starts[s] * 0.5 + 0.25, starts[s] - 0.75};
                    char what[64];
                    snprintf(what, sizeof(what), "%uD kind %d fractal %d at %.0f", dims, kind, fractal, starts[s]);
                    walk_line(&d, start, dir, 6.0, steps, what);
                }
            }
    // The exact borders of gradient noise's cells at -1/0 and 2^32 - 1/2^32:
    // u = p f + o, so p = c - o0 at frequency 1 puts u on the border, and one
    // 2^-24 below is the cell before.
    for (uint32_t dims = 2; dims <= 3; dims++)
    {
        RBTMATH_NOISE_DESC d = make_desc(dims, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
        uint32_t s0 = ref_octave_seed(d.seed, 0);
        const double borders[4] = {-1.0, 0.0, 4294967295.0, 4294967296.0};
        for (int b = 0; b < 4; b++)
        {
            double p[6];
            for (uint32_t a = 0; a < dims; a++)
            {
                double on = borders[b] - ref_offset(s0, (int)a);
                p[a] = on;
                p[dims + a] = on - 0x1p-24;
            }
            float v[2], g[6];
            CHECK(points(&d, p, 2, v, g) == RBT_RESULT_OK);
            double slope = 0.0;
            for (uint32_t a = 0; a < dims; a++)
                slope += fabs((double)g[a]) + fabs((double)g[dims + a]);
            CHECK(fabs((double)v[0] - (double)v[1]) <= slope * 0x1p-24 + 8.0 * (double)FLT_EPSILON);
        }
    }
}

// ---- 4. Periods -------------------------------------------------------------------

static void test_periods(void)
{
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind += 2)
            for (int fractal = 1; fractal <= 2; fractal++)
            {
                RBTMATH_NOISE_DESC d = make_desc(dims, (RBTMATH_NOISE_KIND)kind, (RBTMATH_NOISE_FRACTAL)fractal, 3);
                d.frequency = 1.0 / 8.0;
                d.period[0] = 64;
                d.period[1] = 64;
                d.period[2] = dims == 3 ? 64 : 0;
                // One period is 512 units; two grids a period apart on each
                // axis, 16 places a period... places exact (multiples of 8).
                RBTMATH_NOISE_GRID a, b;
                memset(&a, 0, sizeof(a));
                for (uint32_t ax = 0; ax < 3; ax++)
                {
                    a.origin[ax] = ax < dims ? -24.0 : 0.0;
                    a.step[ax] = ax < dims ? 13.0 : 0.0;
                    a.count[ax] = ax < dims ? 40 : 1;
                }
                b = a;
                for (uint32_t ax = 0; ax < dims; ax++)
                    b.origin[ax] = a.origin[ax] + 512.0 * (double)(ax + 1);
                uint32_t n = a.count[0] * a.count[1] * a.count[2];
                float *va = (float *)malloc(n * sizeof(float)), *vb = (float *)malloc(n * sizeof(float));
                float *ga = (float *)malloc(n * dims * sizeof(float)), *gb = (float *)malloc(n * dims * sizeof(float));
                RBTMATH_ERROR error;
                CHECK(rbtmath_noise_compute_grid(&d, &a, va, ga, &error) == RBT_RESULT_OK);
                CHECK(rbtmath_noise_compute_grid(&d, &b, vb, gb, &error) == RBT_RESULT_OK);
                CHECK(memcmp(va, vb, n * sizeof(float)) == 0);
                CHECK(memcmp(ga, gb, n * dims * sizeof(float)) == 0);
                free(va);
                free(vb);
                free(ga);
                free(gb);
            }
    RBTMATH_ERROR error;
    double p[3] = {1, 2, 3};
    float v;
    RBTMATH_NOISE_DESC d = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.period[0] = 8;
    CHECK(rbtmath_noise_compute_points(&d, p, 1, &v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_FBM, 4);
    d.lacunarity = 2.5;
    d.period[0] = 8;
    CHECK(rbtmath_noise_compute_points(&d, p, 1, &v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
}

// ---- 5. The shapes agree ------------------------------------------------------------

static void test_shapes(void)
{
    static config configs[128];
    uint32_t nconfigs = make_configs(configs, 3);
    enum { N = 300 };
    for (uint32_t ci = 0; ci < nconfigs; ci++)
    {
        const RBTMATH_NOISE_DESC *d = &configs[ci].desc;
        uint32_t dims = d->dimensions;
        bool grads = d->fractal != RBTMATH_NOISE_FRACTAL_ERODED;
        double origin[3] = {123456.75, -98765.5, 4321.25};
        float off[N * 3];
        double pt[N * 3];
        for (uint32_t i = 0; i < N * dims; i++)
        {
            off[i] = (float)next_range(-300.0, 300.0);
            pt[i] = origin[i % dims] + (double)off[i];
        }
        float v1[N], v2[N], g1[N * 3], g2[N * 3];
        RBTMATH_ERROR error;
        CHECK(rbtmath_noise_compute_offsets(d, origin, off, N, v1, grads ? g1 : NULL, &error) == RBT_RESULT_OK);
        CHECK(rbtmath_noise_compute_points(d, pt, N, v2, grads ? g2 : NULL, &error) == RBT_RESULT_OK);
        CHECK(memcmp(v1, v2, sizeof(v1)) == 0);
        if (grads)
            CHECK(memcmp(g1, g2, N * dims * sizeof(float)) == 0);
        // A grid against points at origin + i * step.
        RBTMATH_NOISE_GRID grid;
        memset(&grid, 0, sizeof(grid));
        uint32_t counts[3] = {7, 6, dims == 3 ? 5u : 1u};
        double steps[3] = {0.37, -1.25, dims == 3 ? 2.0625 : 0.0};
        for (uint32_t a = 0; a < 3; a++)
        {
            grid.origin[a] = a < dims ? origin[a] : 0.0;
            grid.step[a] = steps[a];
            grid.count[a] = counts[a];
        }
        uint32_t n = counts[0] * counts[1] * counts[2];
        uint32_t k = 0;
        for (uint32_t z = 0; z < counts[2]; z++)
            for (uint32_t y = 0; y < counts[1]; y++)
                for (uint32_t x = 0; x < counts[0]; x++, k++)
                {
                    uint32_t idx[3] = {x, y, z};
                    for (uint32_t a = 0; a < dims; a++)
                        pt[k * dims + a] = grid.origin[a] + ((double)idx[a] * grid.step[a]);
                }
        CHECK(rbtmath_noise_compute_grid(d, &grid, v1, grads ? g1 : NULL, &error) == RBT_RESULT_OK);
        CHECK(rbtmath_noise_compute_points(d, pt, n, v2, grads ? g2 : NULL, &error) == RBT_RESULT_OK);
        CHECK(memcmp(v1, v2, n * sizeof(float)) == 0);
        if (grads)
            CHECK(memcmp(g1, g2, n * dims * sizeof(float)) == 0);
        // A float place widened gives the double place's bits.
        double zero[3] = {0, 0, 0};
        for (uint32_t i = 0; i < N * dims; i++)
            pt[i] = (double)off[i];
        CHECK(rbtmath_noise_compute_offsets(d, zero, off, N, v1, NULL, &error) == RBT_RESULT_OK);
        CHECK(rbtmath_noise_compute_points(d, pt, N, v2, NULL, &error) == RBT_RESULT_OK);
        CHECK(memcmp(v1, v2, sizeof(v1)) == 0);
    }
}

// ---- 6. The split of a batch ----------------------------------------------------------

typedef struct split_job
{
    const RBTMATH_NOISE_DESC *desc;
    const double *points;
    float *values;
    float *gradients;
    RBT_RESULT result[64];
} split_job;

static void split_task(uint32_t first, uint32_t end, uint32_t worker, void *data)
{
    (void)worker;
    split_job *job = (split_job *)data;
    for (uint32_t i = first; i < end; i++)
    {
        uint32_t dims = job->desc->dimensions;
        uint32_t lo = i * 100u, n = 100u;
        job->result[i] = rbtmath_noise_compute_points(job->desc, job->points + (size_t)lo * dims, n, job->values + lo,
                                                     job->gradients + (size_t)lo * dims, NULL);
    }
}

static void test_split(void)
{
    enum { N = 6400 };
    static double pts[N * 3];
    static float v_one[N], g_one[N * 3], v_parts[N], g_parts[N * 3];
    static config configs[128];
    uint32_t nconfigs = make_configs(configs, 5);
    rbtjobs_pool pools[2] = {NULL, NULL};
    RBTJOBS_SCHEDULER schedulers[2];
    for (int w = 0; w < 2; w++)
    {
        RBTJOBS_POOL_DESC pd;
        memset(&pd, 0, sizeof(pd));
        pd.base.type = RBT_STRUCT_TYPE_JOBS_POOL_DESC;
        pd.thread_count = w == 0 ? 0 : 3; // 1 and 4 workers: the waiting thread is one
        pd.max_tasks = 4;
        CHECK(rbtjobs_pool_create(&pd, &pools[w], NULL) == RBT_RESULT_OK);
        memset(&schedulers[w], 0, sizeof(schedulers[w]));
        schedulers[w].base.type = RBT_STRUCT_TYPE_JOBS_SCHEDULER;
        CHECK(rbtjobs_pool_get_scheduler(pools[w], &schedulers[w]) == RBT_RESULT_OK);
    }
    for (uint32_t ci = 0; ci < nconfigs; ci++)
    {
        const RBTMATH_NOISE_DESC *d = &configs[ci].desc;
        if (d->fractal == RBTMATH_NOISE_FRACTAL_ERODED)
            continue;
        uint32_t dims = d->dimensions;
        for (uint32_t i = 0; i < N * dims; i++)
            pts[i] = next_range(-2e5, 2e5);
        CHECK(rbtmath_noise_compute_points(d, pts, N, v_one, g_one, NULL) == RBT_RESULT_OK);
        // In 16 calls of uneven sizes.
        uint32_t at = 0;
        for (int part = 0; part < 16; part++)
        {
            uint32_t n = part == 15 ? N - at : (uint32_t)(17 + part * 37);
            CHECK(rbtmath_noise_compute_points(d, pts + (size_t)at * dims, n, v_parts + at, g_parts + (size_t)at * dims, NULL) ==
                  RBT_RESULT_OK);
            at += n;
        }
        CHECK(memcmp(v_one, v_parts, sizeof(v_one)) == 0);
        CHECK(memcmp(g_one, g_parts, N * dims * sizeof(float)) == 0);
        // On the pool at 1 and 4 workers, 64 tasks of 100.
        for (int w = 0; w < 2; w++)
        {
            memset(v_parts, 0, sizeof(v_parts));
            split_job job;
            memset(&job, 0, sizeof(job));
            job.desc = d;
            job.points = pts;
            job.values = v_parts;
            job.gradients = g_parts;
            void *task = schedulers[w].enqueue(split_task, 64, 1, &job, schedulers[w].user);
            schedulers[w].wait(task, schedulers[w].user);
            for (int t = 0; t < 64; t++)
                CHECK(job.result[t] == RBT_RESULT_OK);
            CHECK(memcmp(v_one, v_parts, sizeof(v_one)) == 0);
            CHECK(memcmp(g_one, g_parts, N * dims * sizeof(float)) == 0);
        }
    }
    rbtjobs_pool_destroy(pools[0]);
    rbtjobs_pool_destroy(pools[1]);
}

// ---- 7. Cellular against brute force ---------------------------------------------------

static void test_cellular_brute_force(void)
{
    const uint32_t per = full ? 1000000u : 20000u;
    const float jitters[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    int bad = 0;
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int dist = 1; dist <= 3; dist++)
            for (int j = 0; j < 4; j++)
            {
                RBTMATH_NOISE_DESC d = make_desc(dims, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
                d.cell_return = RBTMATH_NOISE_CELL_RETURN_UNKNOWN;
                d.cell_distance = (RBTMATH_NOISE_CELL_DISTANCE)dist;
                d.cell_jitter = jitters[j];
                d.frequency = 0.37;
                uint32_t s0 = ref_octave_seed(d.seed, 0);
                float f0 = (float)d.frequency;
                static double pts[BATCH * 3];
                static float dist_out[BATCH * 2], off_out[BATCH * 6];
                static uint32_t ids[BATCH * 2];
                for (uint32_t done = 0; done < per; done += BATCH)
                {
                    uint32_t n = per - done < BATCH ? per - done : BATCH;
                    for (uint32_t i = 0; i < n * dims; i++)
                        pts[i] = next_range(-5e4, 5e4);
                    RBTMATH_ERROR error;
                    RBT_RESULT r = rbtmath_noise_compute_nearest_cells(&d, pts, n, dist_out, ids, off_out, &error);
                    CHECK(r == RBT_RESULT_OK);
                    if (r != RBT_RESULT_OK)
                        return;
                    for (uint32_t i = 0; i < n; i++)
                    {
                        int64_t c[3] = {0, 0, 0};
                        float t[3] = {0, 0, 0};
                        for (uint32_t a = 0; a < dims; a++)
                        {
                            double u = pts[i * dims + a] * d.frequency + ref_offset(s0, (int)a);
                            ref_split(u, &c[a], &t[a]);
                        }
                        ref_cell_result want;
                        ref_cellular((int)dims, d.cell_distance, d.cell_jitter, s0, c, t, NULL, &want);
                        for (int k = 0; k < 2; k++)
                        {
                            CHECK_LIMITED(dist_out[i * 2 + k] == want.f[k] / f0, bad);
                            CHECK_LIMITED(ids[i * 2 + k] == want.id[k], bad);
                            for (uint32_t a = 0; a < dims; a++)
                                CHECK_LIMITED(off_out[(i * 2 + k) * dims + a] == want.e[k][a] / f0, bad);
                        }
                    }
                }
            }
}

// ---- 8. Seeds and octaves --------------------------------------------------------------

static double correlation(const float *a, const float *b, uint32_t n)
{
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        sa += a[i];
        sb += b[i];
        saa += (double)a[i] * a[i];
        sbb += (double)b[i] * b[i];
        sab += (double)a[i] * b[i];
    }
    double ma = sa / n, mb = sb / n;
    double cov = sab / n - ma * mb, va = saa / n - ma * ma, vb = sbb / n - mb * mb;
    return cov / sqrt(va * vb);
}

static double chi_square(const uint32_t *counts, int bins, uint64_t total)
{
    double expected = (double)total / bins, x = 0;
    for (int i = 0; i < bins; i++)
        x += ((double)counts[i] - expected) * ((double)counts[i] - expected) / expected;
    return x;
}

static void test_seeds_and_octaves(void)
{
    const uint32_t n = full ? 1000000u : 100000u;
    double *pts = (double *)malloc(n * 3 * sizeof(double));
    float *a = (float *)malloc(n * sizeof(float)), *b = (float *)malloc(n * sizeof(float));
    for (uint32_t i = 0; i < n * 3; i++)
        pts[i] = next_range(-1e4, 1e4);
    for (int kind = 1; kind <= 2; kind++)
        for (uint32_t dims = 2; dims <= 3; dims++)
        {
            RBTMATH_NOISE_DESC d = make_desc(dims, (RBTMATH_NOISE_KIND)kind, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
            CHECK(rbtmath_noise_compute_points(&d, pts, n, a, NULL, NULL) == RBT_RESULT_OK);
            d.seed += 1u;
            CHECK(rbtmath_noise_compute_points(&d, pts, n, b, NULL, NULL) == RBT_RESULT_OK);
            double c = correlation(a, b, n);
            CHECK(fabs(c) < 0.01);
            // Octave 1 against octave 0 at the same frequency: a SINGLE call
            // with seed ^ X_1 ^ X_0 is octave 1's lattice.
            d.seed = 1234u ^ ref_x(1) ^ ref_x(0);
            CHECK(rbtmath_noise_compute_points(&d, pts, n, b, NULL, NULL) == RBT_RESULT_OK);
            double c2 = correlation(a, b, n);
            CHECK(fabs(c2) < 0.01);
            if (full)
                printf("  seeds: kind %d %uD, s against s + 1 %.4f, octave 0 against 1 %.4f\n", kind, dims, c, c2);
        }
    free(pts);
    free(a);
    free(b);
    // The hash's gradient choices uniform: Perlin's sixteen (h >> 28) over a
    // 64^3 block at the origin, at 1e9 cells, across 2^32, and along the
    // diagonal and the planes x = y and x = -y. 99% bound on 15 degrees of
    // freedom: 30.58.
    const int64_t starts[3] = {0, 1000000000, 4294967296ll - 32};
    for (int s = 0; s < 3; s++)
    {
        uint32_t counts[16] = {0};
        for (int64_t z = 0; z < 64; z++)
            for (int64_t y = 0; y < 64; y++)
                for (int64_t x = 0; x < 64; x++)
                {
                    int64_t c[3] = {starts[s] + x, starts[s] + y, starts[s] + z};
                    counts[ref_hash(0x12345678u, 3, c, NULL) >> 28]++;
                }
        double x2 = chi_square(counts, 16, 64ull * 64 * 64);
        CHECK(x2 < 30.58);
        if (full)
            printf("  hash at %lld: chi-square %.2f\n", (long long)starts[s], x2);
    }
    for (int line = 0; line < 3; line++)
    {
        uint32_t counts[16] = {0};
        for (int64_t i = -131072; i < 131072; i++)
            for (int64_t z = 0; z < 2; z++)
            {
                int64_t c[3] = {i, line == 0 ? i : (line == 1 ? i : -i), line == 0 ? i : z};
                counts[ref_hash(0x9ABCDEF0u, 3, c, NULL) >> 28]++;
            }
        double x2 = chi_square(counts, 16, 524288);
        CHECK(x2 < 30.58);
        if (full)
            printf("  hash along %s: chi-square %.2f\n", line == 0 ? "the diagonal" : (line == 1 ? "x = y" : "x = -y"), x2);
    }
}

// ---- 9. Warps ---------------------------------------------------------------------------

static void test_warps(void)
{
    enum { N = 500 };
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind++)
        {
            RBTMATH_NOISE_DESC field = make_desc(dims, (RBTMATH_NOISE_KIND)kind, RBTMATH_NOISE_FRACTAL_FBM, 3);
            if (kind == RBTMATH_NOISE_KIND_CELLULAR)
                field.cell_return = RBTMATH_NOISE_CELL_RETURN_F1;
            field.frequency = 0.01;
            RBTMATH_NOISE_WARP_DESC w;
            memset(&w, 0, sizeof(w));
            w.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
            w.field = &field;
            w.amplitude = 37.5;
            double p[N * 3], moved[N * 3];
            float jac[N * 9];
            for (uint32_t i = 0; i < N * dims; i++)
                p[i] = next_range(-1e5, 1e5);
            RBTMATH_ERROR error;
            CHECK(rbtmath_noise_warp_points(&w, p, N, moved, jac, &error) == RBT_RESULT_OK);
            // The three channels computed by hand.
            float ch[3][N], cg[3][N * 3];
            for (uint32_t a = 0; a < dims; a++)
            {
                RBTMATH_NOISE_DESC fa = field;
                fa.seed = field.seed ^ ref_y(a);
                CHECK(rbtmath_noise_compute_points(&fa, p, N, ch[a], cg[a], &error) == RBT_RESULT_OK);
            }
            for (uint32_t i = 0; i < N; i++)
                for (uint32_t a = 0; a < dims; a++)
                {
                    CHECK(moved[i * dims + a] == p[i * dims + a] + (w.amplitude * (double)ch[a][i]));
                    for (uint32_t b = 0; b < dims; b++)
                    {
                        float want = (float)((a == b ? 1.0 : 0.0) + (w.amplitude * (double)cg[a][i * dims + b]));
                        CHECK(jac[(i * dims + a) * dims + b] == want);
                    }
                }
            // In place, the same as apart.
            double inplace[N * 3];
            memcpy(inplace, p, sizeof(inplace));
            CHECK(rbtmath_noise_warp_points(&w, inplace, N, inplace, NULL, &error) == RBT_RESULT_OK);
            CHECK(memcmp(inplace, moved, N * dims * sizeof(double)) == 0);
            // The Jacobian against central differences.
            double h = 1e-3 / field.frequency / 4.0;
            int bad = 0;
            for (uint32_t i = 0; i < 40; i++)
                for (uint32_t b = 0; b < dims; b++)
                {
                    double q[6], m[6];
                    memcpy(q, &p[i * dims], dims * sizeof(double));
                    memcpy(q + dims, &p[i * dims], dims * sizeof(double));
                    q[b] += h;
                    q[dims + b] -= h;
                    CHECK(rbtmath_noise_warp_points(&w, q, 2, m, NULL, &error) == RBT_RESULT_OK);
                    for (uint32_t a = 0; a < dims; a++)
                    {
                        double numeric = (m[a] - m[dims + a]) / (2.0 * h);
                        double analytic = (double)jac[(i * dims + a) * dims + b];
                        if (kind == RBTMATH_NOISE_KIND_CELLULAR)
                            continue; // kinked inside cells: test 2 checks its gradients, the bits above its Jacobian
                        if (fabs(numeric - analytic) > 2e-3 * (1.0 + fabs(analytic)) && bad < 5)
                            printf("  warp %uD kind %d place %u J[%u][%u]: analytic %.9g numeric %.9g\n", dims, kind, i, a, b, analytic,
                                   numeric);
                        CHECK_LIMITED(fabs(numeric - analytic) <= 2e-3 * (1.0 + fabs(analytic)), bad);
                    }
                }
            // A place whose bound passes the limit, refused in place with the
            // array unchanged: the third place's bound is past range.
            double edge[9], before[9];
            for (uint32_t i = 0; i < 3 * dims; i++)
                edge[i] = 10.0;
            edge[2 * dims] = RBTMATH_NOISE_MAX_CELLS / field.frequency / 4.0 - 1.0; // finest octave x4
            memcpy(before, edge, sizeof(edge));
            CHECK(rbtmath_noise_warp_points(&w, edge, 3, edge, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
            CHECK(memcmp(edge, before, 3 * dims * sizeof(double)) == 0);
            CHECK(strstr(error.message, "2") != NULL);
        }
}

// ---- 10. Misuse --------------------------------------------------------------------------

static int expect_refused(const RBTMATH_NOISE_DESC *d, RBT_RESULT want, const char *needle, int line)
{
    double p[3] = {1.5, 2.5, 3.5};
    float v = 12345.0f, g[3] = {777.0f, 777.0f, 777.0f};
    RBTMATH_ERROR error;
    error.message[0] = '\0';
    RBT_RESULT r = rbtmath_noise_compute_points(d, p, 1, &v, g, &error);
    int ok = r == want && v == 12345.0f && g[0] == 777.0f && (!needle || strstr(error.message, needle));
    if (!ok)
    {
        printf("FAIL misuse at line %d: result %d (wanted %d), message \"%s\" (wanted \"%s\")\n", line, (int)r, (int)want,
               error.message, needle ? needle : "");
        failures++;
    }
    return ok;
}

#define REFUSE(d, needle) expect_refused(&(d), RBT_RESULT_INVALID_ARGUMENT, needle, __LINE__)

static void test_misuse(void)
{
    RBTMATH_ERROR error;
    double p[6] = {1, 2, 3, 4, 5, 6};
    float v[2] = {5.0f, 5.0f}, g[6];
    RBTMATH_NOISE_DESC good = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_FBM, 4);
    RBTMATH_NOISE_DESC d;

    CHECK(rbtmath_noise_compute_points(NULL, p, 1, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    d = good;
    d.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
    expect_refused(&d, RBT_RESULT_STRUCT_WRONG_TYPE, NULL, __LINE__);
    RBT_STRUCT_BASE extra = {RBT_STRUCT_TYPE_MATH_CAST_DESC, NULL};
    d = good;
    d.base.next = &extra;
    expect_refused(&d, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN, NULL, __LINE__);

    d = good; d.dimensions = 1; REFUSE(d, "dimensions");
    d = good; d.dimensions = 4; REFUSE(d, "dimensions");
    d = good; d.kind = RBTMATH_NOISE_KIND_UNKNOWN; REFUSE(d, "kind");
    d = good; d.kind = (RBTMATH_NOISE_KIND)4; REFUSE(d, "kind");
    d = good; d.fractal = RBTMATH_NOISE_FRACTAL_UNKNOWN; REFUSE(d, "fractal");
    d = good; d.fractal = (RBTMATH_NOISE_FRACTAL)5; REFUSE(d, "fractal");
    d = make_desc(3, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_FBM, 3);
    d.fractal = RBTMATH_NOISE_FRACTAL_RIDGED; d.ridge_feedback = 2.0f; REFUSE(d, "fractal");
    d = good; d.octaves = 0; REFUSE(d, "octaves");
    d = good; d.octaves = RBTMATH_NOISE_MAX_OCTAVES + 1; REFUSE(d, "octaves");
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_SINGLE, 1); d.octaves = 2; REFUSE(d, "octaves");
    d = good; d.frequency = 0.0; REFUSE(d, "frequency");
    d = good; d.frequency = -1.0; REFUSE(d, "frequency");
    d = good; d.frequency = INFINITY; REFUSE(d, "frequency");
    d = good; d.frequency = NAN; REFUSE(d, "frequency");
    d = good; d.lacunarity = 1.0; REFUSE(d, "lacunarity");
    d = good; d.lacunarity = NAN; REFUSE(d, "lacunarity");
    d = good; d.lacunarity = 1e300; REFUSE(d, "lacunarity"); // the finest octave's frequency overflows
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_SINGLE, 1); d.lacunarity = 2.0; REFUSE(d, "lacunarity");
    d = good; d.gain = 0.0f; REFUSE(d, "gain");
    d = good; d.gain = INFINITY; REFUSE(d, "gain");
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_SINGLE, 1); d.gain = 0.5f; REFUSE(d, "gain");
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_RIDGED, 3); d.ridge_feedback = 0.0f; REFUSE(d, "ridge_feedback");
    d = good; d.ridge_feedback = 2.0f; REFUSE(d, "ridge_feedback");
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_ERODED, 3); d.erosion = -1.0f; REFUSE(d, "erosion");
    d = good; d.erosion = 1.0f; REFUSE(d, "erosion");
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.cell_distance = RBTMATH_NOISE_CELL_DISTANCE_UNKNOWN; REFUSE(d, "cell_distance");
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.cell_distance = (RBTMATH_NOISE_CELL_DISTANCE)4; REFUSE(d, "cell_distance");
    d = good; d.cell_distance = RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN; REFUSE(d, "cell_distance");
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.cell_return = RBTMATH_NOISE_CELL_RETURN_UNKNOWN; REFUSE(d, "cell_return");
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.cell_return = (RBTMATH_NOISE_CELL_RETURN)5; REFUSE(d, "cell_return");
    d = good; d.cell_return = RBTMATH_NOISE_CELL_RETURN_F1; REFUSE(d, "cell_return");
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1); d.cell_jitter = 0.0f; REFUSE(d, "cell_jitter");
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1); d.cell_jitter = 1.5f; REFUSE(d, "cell_jitter");
    d = good; d.cell_jitter = 0.5f; REFUSE(d, "cell_jitter");
    d = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.orientation = RBTMATH_NOISE_ORIENTATION_UNKNOWN; REFUSE(d, "orientation");
    d = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.orientation = (RBTMATH_NOISE_ORIENTATION)4; REFUSE(d, "orientation");
    d = make_desc(2, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    d.orientation = RBTMATH_NOISE_ORIENTATION_EVEN; REFUSE(d, "orientation");
    d = good; d.orientation = RBTMATH_NOISE_ORIENTATION_Z_UP; REFUSE(d, "orientation");
    d = make_desc(2, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_SINGLE, 1); d.period[2] = 4; REFUSE(d, "period");
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_FBM, 32); d.period[0] = 1u << 30; REFUSE(d, "period");

    // A place not finite, or past range, its index named.
    d = good;
    double bad_pts[6] = {1, 2, 3, 4, NAN, 6};
    v[0] = v[1] = 5.0f;
    CHECK(rbtmath_noise_compute_points(&d, bad_pts, 2, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strstr(error.message, "1") != NULL);
    CHECK(v[0] == 5.0f && v[1] == 5.0f);
    double far_pts[6] = {1, 2, 3, 4, RBTMATH_NOISE_MAX_CELLS, 6};
    CHECK(rbtmath_noise_compute_points(&d, far_pts, 2, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(v[0] == 5.0f && v[1] == 5.0f);
    // NULL arrays with a count; a count of 0.
    CHECK(rbtmath_noise_compute_points(&d, NULL, 1, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtmath_noise_compute_points(&d, p, 1, NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtmath_noise_compute_points(&d, NULL, 0, NULL, NULL, &error) == RBT_RESULT_OK);
    CHECK(error.message[0] == '\0');
    // ERODED with gradients.
    d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_ERODED, 3);
    CHECK(rbtmath_noise_compute_points(&d, p, 1, v, g, &error) == RBT_RESULT_UNSUPPORTED);
    // The other calls' own arguments.
    d = good;
    float off[3] = {1, 2, 3};
    double bad_origin[3] = {INFINITY, 0, 0};
    CHECK(rbtmath_noise_compute_offsets(&d, NULL, off, 1, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtmath_noise_compute_offsets(&d, bad_origin, off, 1, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtmath_noise_compute_offsets(&d, p, NULL, 1, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtmath_noise_compute_grid(&d, NULL, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    RBTMATH_NOISE_GRID grid;
    memset(&grid, 0, sizeof(grid));
    grid.count[0] = 1; grid.count[1] = 1; grid.count[2] = 0;
    CHECK(rbtmath_noise_compute_grid(&d, &grid, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    grid.count[2] = 1; grid.step[0] = NAN;
    CHECK(rbtmath_noise_compute_grid(&d, &grid, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    grid.step[0] = 0; grid.count[0] = 65536; grid.count[1] = 65536; grid.count[2] = 2;
    CHECK(rbtmath_noise_compute_grid(&d, &grid, v, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    RBTMATH_NOISE_WARP_DESC w;
    memset(&w, 0, sizeof(w));
    w.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
    w.amplitude = 1.0;
    double out[3];
    CHECK(rbtmath_noise_warp_points(&w, p, 1, out, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    w.field = &d;
    w.amplitude = 0.0;
    CHECK(rbtmath_noise_warp_points(&w, p, 1, out, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    w.amplitude = 1.0;
    w.base.type = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
    CHECK(rbtmath_noise_warp_points(&w, p, 1, out, NULL, &error) == RBT_RESULT_STRUCT_WRONG_TYPE);
    w.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
    RBTMATH_NOISE_DESC eroded = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_ERODED, 3);
    w.field = &eroded;
    float jac[9];
    CHECK(rbtmath_noise_warp_points(&w, p, 1, out, jac, &error) == RBT_RESULT_UNSUPPORTED);
    CHECK(rbtmath_noise_warp_points(&w, p, 1, out, NULL, &error) == RBT_RESULT_OK);
    // Nearest cells: the desc as the call says, and an output.
    RBTMATH_NOISE_DESC cell = make_desc(3, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    float dist[2];
    CHECK(rbtmath_noise_compute_nearest_cells(&cell, p, 1, dist, NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    cell.cell_return = RBTMATH_NOISE_CELL_RETURN_UNKNOWN;
    CHECK(rbtmath_noise_compute_nearest_cells(&cell, p, 1, NULL, NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtmath_noise_compute_nearest_cells(&cell, p, 1, dist, NULL, NULL, &error) == RBT_RESULT_OK);
    CHECK(rbtmath_noise_compute_nearest_cells(&good, p, 1, dist, NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    RBTMATH_NOISE_DESC cellfbm = make_desc(3, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_FBM, 3);
    cellfbm.cell_return = RBTMATH_NOISE_CELL_RETURN_UNKNOWN;
    CHECK(rbtmath_noise_compute_nearest_cells(&cellfbm, p, 1, dist, NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
}

// ---- 11. Two calls at once on one desc --------------------------------------------------

typedef struct two_calls
{
    const RBTMATH_NOISE_DESC *desc;
    const double *points;
    float *values[2];
    float *gradients[2];
    RBT_RESULT result[2];
} two_calls;

static void two_calls_task(uint32_t first, uint32_t end, uint32_t worker, void *data)
{
    (void)worker;
    two_calls *t = (two_calls *)data;
    for (uint32_t i = first; i < end; i++)
        t->result[i] = rbtmath_noise_compute_points(t->desc, t->points, 20000, t->values[i], t->gradients[i], NULL);
}

static void test_two_threads(void)
{
    enum { N = 20000 };
    static double pts[N * 3];
    static float v0[N], v1[N], g0[N * 3], g1[N * 3], want_v[N], want_g[N * 3];
    for (uint32_t i = 0; i < N * 3; i++)
        pts[i] = next_range(-1e3, 1e3);
    RBTMATH_NOISE_DESC d = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 6);
    CHECK(rbtmath_noise_compute_points(&d, pts, N, want_v, want_g, NULL) == RBT_RESULT_OK);
    RBTJOBS_POOL_DESC pd;
    memset(&pd, 0, sizeof(pd));
    pd.base.type = RBT_STRUCT_TYPE_JOBS_POOL_DESC;
    pd.thread_count = 2;
    pd.max_tasks = 4;
    rbtjobs_pool pool = NULL;
    RBTJOBS_SCHEDULER sched;
    memset(&sched, 0, sizeof(sched));
    sched.base.type = RBT_STRUCT_TYPE_JOBS_SCHEDULER;
    CHECK(rbtjobs_pool_create(&pd, &pool, NULL) == RBT_RESULT_OK);
    CHECK(rbtjobs_pool_get_scheduler(pool, &sched) == RBT_RESULT_OK);
    two_calls t = {&d, pts, {v0, v1}, {g0, g1}, {RBT_RESULT_NOT_IMPLEMENTED, RBT_RESULT_NOT_IMPLEMENTED}};
    void *task = sched.enqueue(two_calls_task, 2, 1, &t, sched.user);
    sched.wait(task, sched.user);
    CHECK(t.result[0] == RBT_RESULT_OK && t.result[1] == RBT_RESULT_OK);
    CHECK(memcmp(v0, want_v, sizeof(v0)) == 0 && memcmp(v1, want_v, sizeof(v1)) == 0);
    CHECK(memcmp(g0, want_g, sizeof(g0)) == 0 && memcmp(g1, want_g, sizeof(g1)) == 0);
    rbtjobs_pool_destroy(pool);
}

// ---- 12. Levels of detail ------------------------------------------------------------------
//
// Octave i of a desc is a SINGLE desc with seed ^ X_i ^ X_0 at frequency 1,
// sampled at p * f_i (the product rounded, as step 5 rounds it): its value
// is the octave's n and its gradient the octave's per-cell g exactly.

static void octave_values(const RBTMATH_NOISE_DESC *d, uint32_t i, const double *p, uint32_t n, float *v, float *g)
{
    RBTMATH_NOISE_DESC s = *d;
    s.fractal = RBTMATH_NOISE_FRACTAL_SINGLE;
    s.octaves = 1;
    s.lacunarity = 0.0;
    s.gain = 0.0f;
    s.ridge_feedback = 0.0f;
    s.erosion = 0.0f;
    s.seed = d->seed ^ ref_x(i) ^ ref_x(0);
    double f = d->frequency;
    for (uint32_t k = 0; k < i; k++)
        f = f * d->lacunarity;
    s.frequency = 1.0;
    for (uint32_t a = 0; a < 3; a++)
        s.period[a] = 0;
    double *q = (double *)malloc((size_t)n * d->dimensions * sizeof(double));
    for (uint32_t k = 0; k < n * d->dimensions; k++)
        q[k] = p[k] * f;
    CHECK(rbtmath_noise_compute_points(&s, q, n, v, g, NULL) == RBT_RESULT_OK);
    free(q);
}

static void test_levels_of_detail(void)
{
    enum { N = 2000, MAXO = 7 };
    static double pts[N * 3];
    static float coarse[N], fine[N], cg[N * 3], fg[N * 3];
    static float on[MAXO + 1][N], og[MAXO + 1][N * 3];
    int bad = 0;
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind++)
            for (int fractal = 2; fractal <= 4; fractal++)
            {
                if (kind == RBTMATH_NOISE_KIND_CELLULAR && fractal != RBTMATH_NOISE_FRACTAL_FBM)
                    continue;
                for (uint32_t i = 0; i < N * dims; i++)
                    pts[i] = next_range(-3e3, 3e3);
                for (uint32_t n = 1; n < MAXO; n++)
                {
                    RBTMATH_NOISE_DESC d = make_desc(dims, (RBTMATH_NOISE_KIND)kind, (RBTMATH_NOISE_FRACTAL)fractal, n);
                    d.gain = 0.55f;
                    d.lacunarity = 2.03125;
                    bool grads = fractal != RBTMATH_NOISE_FRACTAL_ERODED;
                    CHECK(rbtmath_noise_compute_points(&d, pts, N, coarse, grads ? cg : NULL, NULL) == RBT_RESULT_OK);
                    d.octaves = n + 1;
                    CHECK(rbtmath_noise_compute_points(&d, pts, N, fine, grads ? fg : NULL, NULL) == RBT_RESULT_OK);
                    for (uint32_t o = 0; o <= n; o++)
                        octave_values(&d, o, pts, N, on[o], og[o]);
                    float a_n = 1.0f;
                    for (uint32_t k = 0; k < n; k++)
                        a_n = a_n * d.gain;
                    double f_n = d.frequency;
                    for (uint32_t k = 0; k < n; k++)
                        f_n = f_n * d.lacunarity;
                    float fn = (float)f_n;
                    for (uint32_t i = 0; i < N; i++)
                    {
                        if (fractal == RBTMATH_NOISE_FRACTAL_FBM)
                        {
                            CHECK_LIMITED(fine[i] == coarse[i] + (a_n * on[n][i]), bad);
                            for (uint32_t a = 0; a < dims; a++)
                                CHECK_LIMITED(fg[i * dims + a] == cg[i * dims + a] + (a_n * (og[n][i * dims + a] * fn)), bad);
                        }
                        else if (fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
                        {
                            // The weight entering octave n, from the octaves before.
                            float w = 1.0f, dw[3] = {0, 0, 0};
                            float f_k = (float)d.frequency;
                            double fk = d.frequency;
                            for (uint32_t k = 0; k <= n; k++)
                            {
                                fk = k == 0 ? d.frequency : fk * d.lacunarity;
                                f_k = (float)fk;
                                float nv = on[k][i];
                                float sg = fabsf(nv) < 0x1p-100f ? 0.0f : (nv > 0 ? 1.0f : -1.0f);
                                float r = 1.0f - fabsf(nv);
                                float t = (r * r) * w;
                                float dt[3];
                                for (uint32_t a = 0; a < dims; a++)
                                {
                                    float gamma = og[k][i * dims + a] * f_k;
                                    dt[a] = ((((r * -2.0f) * sg) * w) * gamma) + ((r * r) * dw[a]);
                                }
                                if (k == n)
                                {
                                    CHECK_LIMITED(fine[i] == coarse[i] + (a_n * t), bad);
                                    for (uint32_t a = 0; a < dims; a++)
                                        CHECK_LIMITED(fg[i * dims + a] == cg[i * dims + a] + (a_n * dt[a]), bad);
                                }
                                float x = t * d.ridge_feedback;
                                if (x > 1.0f)
                                {
                                    w = 1.0f;
                                    dw[0] = dw[1] = dw[2] = 0.0f;
                                }
                                else
                                {
                                    w = x;
                                    for (uint32_t a = 0; a < dims; a++)
                                        dw[a] = dt[a] * d.ridge_feedback;
                                }
                            }
                        }
                        else
                        {
                            float dd[3] = {0, 0, 0};
                            for (uint32_t k = 0; k <= n; k++)
                                for (uint32_t a = 0; a < dims; a++)
                                    dd[a] = dd[a] + og[k][i * dims + a];
                            float q = (dd[0] * dd[0]) + (dd[1] * dd[1]);
                            if (dims == 3)
                                q = q + (dd[2] * dd[2]);
                            CHECK_LIMITED(fine[i] == coarse[i] + ((a_n * on[n][i]) / (1.0f + (d.erosion * q))), bad);
                        }
                    }
                }
            }
}

// ---- 13. Simplex whole ------------------------------------------------------------------------

static void test_simplex_whole(void)
{
    const uint32_t per = full ? 1000000u : 20000u;
    static double pts[BATCH * 3];
    static float vals[BATCH], grads[BATCH * 3];
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int o = 1; o <= (dims == 3 ? 3 : 1); o++)
        {
            RBTMATH_NOISE_DESC d = make_desc(dims, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
            if (dims == 3)
                d.orientation = (RBTMATH_NOISE_ORIENTATION)o;
            uint32_t s0 = ref_octave_seed(d.seed, 0);
            double off[3];
            for (int a = 0; a < 3; a++)
                off[a] = ref_offset(s0, a);
            // The scale, from the ratio at a place well inside the range.
            double scale = 0.0, scale_raw = 0.0;
            float scale_val = 0.0f;
            double worst = 0.0;
            int bad = 0;
            for (uint32_t done = 0; done < per; done += BATCH)
            {
                uint32_t n = per - done < BATCH ? per - done : BATCH;
                for (uint32_t i = 0; i < n * dims; i++)
                    pts[i] = next_range(-1e4, 1e4);
                CHECK(rbtmath_noise_compute_points(&d, pts, n, vals, NULL, NULL) == RBT_RESULT_OK);
                for (uint32_t i = 0; i < n; i++)
                {
                    double u[3] = {0, 0, 0};
                    for (uint32_t a = 0; a < dims; a++)
                        u[a] = pts[i * dims + a] * d.frequency + off[a];
                    double raw = ref_simplex_full((int)dims, d.orientation, s0, u);
                    if (scale == 0.0)
                    {
                        // The scale from the first batch's largest raw value.
                        if (i + 1 < n)
                        {
                            if (fabs(raw) > fabs(scale_raw))
                            {
                                scale_raw = raw;
                                scale_val = vals[i];
                            }
                            continue;
                        }
                        scale = (double)scale_val / scale_raw;
                        continue;
                    }
                    double err = fabs((double)vals[i] - raw * scale);
                    if (err > worst)
                        worst = err;
                    // 2^-19, not the draft's 2^-20 (claude_made_decisions,
                    // 2026-11-17): the library sums up to 16 kernel terms in
                    // float and scales by about 12.5 in 3D, and measures up
                    // to 1.06e-6 off the double sum over 10^6 places a case.
                    // What this test exists to catch, a kernel's tails
                    // dropped, is 0.0041 (OpenSimplex2's reference).
                    CHECK_LIMITED(err <= 0x1p-19, bad);
                }
            }
            if (full || bad)
                printf("  simplex whole %uD orientation %d: scale %.9g, worst %.3g against the double sum\n", dims, o, scale, worst);
            CHECK(scale > 0.0);
        }
    // Across the planes where OpenSimplex2's reference changes its point:
    // |x| = |y| of the turned frame, about lattice 0's nearest point. The
    // values 2^-24 of a cell either side within the gradient's bound.
    const uint32_t cuts = full ? 100000u : 10000u;
    for (int o = 1; o <= 3; o++)
    {
        RBTMATH_NOISE_DESC d = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
        d.orientation = (RBTMATH_NOISE_ORIENTATION)o;
        uint32_t s0 = ref_octave_seed(d.seed, 0);
        double off[3];
        for (int a = 0; a < 3; a++)
            off[a] = ref_offset(s0, a);
        int bad = 0;
        double worst = 0.0;
        for (uint32_t k = 0; k < cuts; k++)
        {
            double v[3];
            double cx = floor(next_range(-500, 500)), cy = floor(next_range(-500, 500)), cz = floor(next_range(-500, 500));
            double ax = next_range(0.02, 0.48);
            double sgn = next_unit() < 0.5 ? -1.0 : 1.0;
            v[0] = cx + ax;
            v[1] = cy + sgn * ax;
            v[2] = cz + next_range(-0.48, 0.48);
            double u[3];
            ref_unturn(d.orientation, v, u);
            // Across the plane: along the turned frame's y, back in u.
            double dv[3] = {0, 0x1p-24, 0}, du[3];
            ref_unturn(d.orientation, dv, du);
            double p[6];
            for (int a = 0; a < 3; a++)
            {
                p[a] = (u[a] - du[a]) - off[a];
                p[3 + a] = (u[a] + du[a]) - off[a];
            }
            float val[2], g[6];
            CHECK(rbtmath_noise_compute_points(&d, p, 2, val, g, NULL) == RBT_RESULT_OK);
            double slope = 0.0;
            for (int a = 0; a < 3; a++)
            {
                double ga = fabs((double)g[a]), gb = fabs((double)g[3 + a]);
                slope += (ga > gb ? ga : gb) * fabs(2.0 * du[a]);
            }
            double jump = fabs((double)val[1] - (double)val[0]);
            double allowed = slope + 16.0 * (double)FLT_EPSILON;
            if (jump > worst)
                worst = jump;
            CHECK_LIMITED(jump <= allowed, bad);
        }
        if (full || bad)
            printf("  simplex across OpenSimplex2's cut planes, orientation %d: largest step %.3g\n", o, worst);
    }
}

int main(int argc, char **argv)
{
    full = argc > 1 && strcmp(argv[1], "--full") == 0;
    double probe[3] = {0.5, 0.5, 0.5};
    float probe_v;
    RBTMATH_NOISE_DESC probe_d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
    if (rbtmath_noise_compute_points(&probe_d, probe, 1, &probe_v, NULL, NULL) == RBT_RESULT_NOT_IMPLEMENTED)
    {
        printf("rbtmathnoise_tests: not implemented\n");
        return 1;
    }
    // --full [name...]: only the tests named (by hand, Release).
    const char *only = argc > 2 ? argv[2] : NULL;
#define RUN(name)                                                                                                      \
    if (!only || strstr(#name, only))                                                                                  \
    name()
    RUN(test_misuse);
    RUN(test_shapes);
    RUN(test_split);
    RUN(test_periods);
    RUN(test_levels_of_detail);
    RUN(test_warps);
    RUN(test_two_threads);
    RUN(test_cellular_brute_force);
    RUN(test_simplex_whole);
    RUN(test_continuity);
    RUN(test_gradients);
    RUN(test_seeds_and_octaves);
    RUN(test_ranges);
#undef RUN
    if (failures)
    {
        printf("rbtmathnoise_tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("rbtmathnoise_tests: all passed%s\n", full ? " (full counts)" : "");
    return 0;
}
