// The scale constants' search (rbtmathnoise.h, Definition, step 10): for each
// scaled kind (gradient and simplex, 2D and 3D), the largest magnitude one
// octave can reach, so SCALE = 1 / that maximum puts the octave in [-1, 1]
// with the clamp a guarantee that never acts. Run by hand once, Release; its
// output is recorded in RBTMATH_TODO.md and the constants written into the
// header and src/math/rbtmathnoise_internal.hpp.
//
// The maximum, exactly: each lattice point's gradient is drawn by its own
// hash, independently, and each kind's value is a sum over lattice points of
// a weight that is never negative (the lerps' (1 - s) and s products; a
// kernel's a^4) times the point's dot q . d. So over every choice of
// gradients, the largest value at a place is the sum of the weights times
// each point's largest dot (over the table), and the octave's maximum is the
// largest of that over the places of one cell. The tables are symmetric
// (each vector's negation is in it), so the minimum is its negation. That is
// searched in double: a grid over the cell, then a pattern search from the
// best cells of the grid, down to steps of 2^-30.
//
// Then, as the Definition asks, 10^9 samples per kind of the octave as the
// library evaluates it (float, rbtmathnoise_internal.hpp), at random cells,
// fractions and seeds, counting where the scaled value passes 1: the clamp
// acting. It must be none.

#include "rbtmathnoise_internal.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace rbtnoise;

namespace
{

// The largest dot over a table's vectors with an offset d.
double best_dot2_24(double d0, double d1)
{
    double best = -1e300;
    for (int k = 0; k < 24; k++)
    {
        double v = (double)DIR24[k][0] * d0 + (double)DIR24[k][1] * d1;
        if (v > best)
            best = v;
    }
    return best;
}

double best_dot3_perlin(const double d[3])
{
    double best = -1e300;
    for (int k = 0; k < 16; k++)
    {
        double v = (double)PERLIN16[k].g[0] * d[0] + (double)PERLIN16[k].g[1] * d[1] + (double)PERLIN16[k].g[2] * d[2];
        if (v > best)
            best = v;
    }
    return best;
}

double best_dot3_48(const double d[3])
{
    double best = -1e300;
    for (int k = 0; k < 48; k++)
    {
        double v = (double)DIR48.d[k][0] * d[0] + (double)DIR48.d[k][1] * d[1] + (double)DIR48.d[k][2] * d[2];
        if (v > best)
            best = v;
    }
    return best;
}

double fade_d(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// Each kind's largest value at fractions t (in [0, 1)^D), over every
// choice of gradients.
double bound_gradient2(const double t[3])
{
    double sx = fade_d(t[0]), sy = fade_d(t[1]);
    double sum = 0.0;
    for (int k = 0; k < 4; k++)
    {
        int kx = k & 1, ky = k >> 1;
        double w = (kx ? sx : 1.0 - sx) * (ky ? sy : 1.0 - sy);
        sum += w * best_dot2_24(t[0] - kx, t[1] - ky);
    }
    return sum;
}

double bound_gradient3(const double t[3])
{
    double s[3] = {fade_d(t[0]), fade_d(t[1]), fade_d(t[2])};
    double sum = 0.0;
    for (int k = 0; k < 8; k++)
    {
        int kk[3] = {k & 1, (k >> 1) & 1, k >> 2};
        double w = 1.0, d[3];
        for (int a = 0; a < 3; a++)
        {
            w *= kk[a] ? s[a] : 1.0 - s[a];
            d[a] = t[a] - kk[a];
        }
        sum += w * best_dot3_perlin(d);
    }
    return sum;
}

double bound_simplex2(const double t[3])
{
    // The place's offsets to its triangle's three corners, as the
    // Definition makes them (in double here).
    double tau = (t[0] + t[1]) * (double)G;
    double e0 = t[0] + tau, e1 = t[1] + tau;
    double d[3][2] = {{e0, e1}, {0, 0}, {e0 - (double)E11, e1 - (double)E11}};
    if (e1 > e0)
    {
        d[1][0] = e0 - (double)G;
        d[1][1] = e1 - (double)E1;
    }
    else
    {
        d[1][0] = e0 - (double)E1;
        d[1][1] = e1 - (double)G;
    }
    double sum = 0.0;
    for (int k = 0; k < 3; k++)
    {
        double a = 0.5 - d[k][0] * d[k][0] - d[k][1] * d[k][1];
        if (a > 0)
            sum += a * a * a * a * best_dot2_24(d[k][0], d[k][1]);
    }
    return sum;
}

double bound_simplex3(const double t[3])
{
    double sum = 0.0;
    for (int l = 0; l < 2; l++)
        for (int k = 0; k < 8; k++)
        {
            double d[3];
            int kk[3] = {k & 1, (k >> 1) & 1, k >> 2};
            for (int a = 0; a < 3; a++)
            {
                double lt = l == 0 ? t[a] : (t[a] >= 0.5 ? t[a] - 0.5 : t[a] + 0.5);
                d[a] = lt - kk[a];
            }
            double a = 0.6 - d[0] * d[0] - d[1] * d[1] - d[2] * d[2];
            if (a > 0)
                sum += a * a * a * a * best_dot3_48(d);
        }
    return sum;
}

typedef double (*bound_fn)(const double t[3]);

// The cell's maximum: a grid of `steps` a side, then a pattern search from
// the best grid cells.
double search(bound_fn f, int dims, int steps, double best_t[3])
{
    struct candidate
    {
        double v, t[3];
    } top[32];
    int ntop = 0;
    double t[3] = {0, 0, 0};
    int zs = dims == 3 ? steps : 1;
    for (int z = 0; z < zs; z++)
        for (int y = 0; y < steps; y++)
            for (int x = 0; x < steps; x++)
            {
                t[0] = (x + 0.5) / steps;
                t[1] = (y + 0.5) / steps;
                t[2] = dims == 3 ? (z + 0.5) / steps : 0.0;
                double v = f(t);
                if (ntop < 32 || v > top[ntop - 1].v)
                {
                    int i = ntop < 32 ? ntop++ : 31;
                    top[i].v = v;
                    memcpy(top[i].t, t, sizeof(t));
                    for (; i > 0 && top[i].v > top[i - 1].v; i--)
                    {
                        candidate c = top[i];
                        top[i] = top[i - 1];
                        top[i - 1] = c;
                    }
                }
            }
    double best = -1e300;
    for (int c = 0; c < ntop; c++)
    {
        double p[3];
        memcpy(p, top[c].t, sizeof(p));
        double v = top[c].v, step = 1.0 / steps;
        while (step > 0x1p-30)
        {
            bool moved = false;
            for (int a = 0; a < dims; a++)
                for (int sgn = -1; sgn <= 1; sgn += 2)
                {
                    double q[3];
                    memcpy(q, p, sizeof(q));
                    q[a] += sgn * step;
                    if (q[a] < 0.0 || q[a] >= 1.0)
                        continue;
                    double w = f(q);
                    if (w > v)
                    {
                        v = w;
                        memcpy(p, q, sizeof(p));
                        moved = true;
                    }
                }
            if (!moved)
                step *= 0.5;
        }
        if (v > best)
        {
            best = v;
            memcpy(best_t, p, sizeof(p));
        }
    }
    return best;
}

uint64_t rng = 0x243F6A8885A308D3ull;
uint64_t next64()
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

// The octave as the library evaluates it, unscaled, at a random place.
float sample(int which, float &largest_raw)
{
    int64_t c[3];
    float t[3];
    for (int a = 0; a < 3; a++)
    {
        c[a] = (int64_t)(next64() >> 28) - (int64_t)(1ll << 35);
        t[a] = (float)((double)(next64() >> 40) * 0x1p-24);
    }
    uint32_t s = (uint32_t)next64();
    int64_t no_period[3] = {0, 0, 0};
    float g[3];
    float raw;
    switch (which)
    {
    case 0: raw = gradient2_raw(s, c, t, no_period, g); break;
    case 1: raw = gradient3_raw(s, c, t, no_period, g); break;
    case 2: raw = simplex2_raw(s, c, t, g); break;
    default: raw = simplex3_raw(s, c, t, g); break;
    }
    if (std::fabs(raw) > largest_raw)
        largest_raw = std::fabs(raw);
    return raw;
}

} // namespace

int main(int argc, char **argv)
{
    long long samples = argc > 1 ? atoll(argv[1]) : 1000000000ll;
    const char *names[4] = {"SCALE_GRADIENT_2D", "SCALE_GRADIENT_3D", "SCALE_SIMPLEX_2D", "SCALE_SIMPLEX_3D"};
    const bound_fn fns[4] = {bound_gradient2, bound_gradient3, bound_simplex2, bound_simplex3};
    const int dims[4] = {2, 3, 2, 3};
    const int steps[4] = {512, 96, 512, 96};
    for (int k = 0; k < 4; k++)
    {
        double at[3] = {0, 0, 0};
        double m = search(fns[k], dims[k], steps[k], at);
        // The float just below 1 / m: the scaled maximum at most 1.
        float scale = (float)(1.0 / m);
        if ((double)scale > 1.0 / m)
            scale = std::nextafter(scale, 0.0f);
        printf("%s: the octave's maximum %.17g at t = (%.9f, %.9f, %.9f); scale %a (%.9g)\n", names[k], m, at[0], at[1], at[2],
               (double)scale, (double)scale);
        // The clamp, over the samples.
        long long acted = 0;
        float largest_raw = 0.0f;
        for (long long n = 0; n < samples; n++)
        {
            float v = sample(k, largest_raw) * scale;
            if (v > 1.0f || v < -1.0f)
                acted++;
        }
        printf("    %lld samples: the largest |raw| %.9g (scaled %.9g), the clamp acting %lld times\n", samples, (double)largest_raw,
               (double)(largest_raw * scale), acted);
        fflush(stdout);
    }
    return 0;
}
