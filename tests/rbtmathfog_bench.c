// Numbers, not a test (card A59's Math half, rbtmath_fog_draft.h, section
// 4; and the baked noise volumes, card A57's Math half, its test 11): one
// core, ns a point for each fog call and a volume's read, and ns a texel for
// each kind of bake. Run by hand, Release; the numbers go in
// RBTMATH_TODO.md with the machine named.
//
// Written in C, as the other tests of a C header are.

#include "math/rbtmathfog.h"
#include "math/rbtmathnoise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

enum { N = 65536, ROUNDS = 40 };
static float a[N * 3], b[N * 3], out[N * 4];
static uint8_t worley[32 * 32 * 32 * 4];
static uint8_t base[128 * 128 * 4 * 4];
static volatile float sink;

static void report(const char *what, double seconds, double items)
{
    printf("%-52s %8.2f ns\n", what, seconds * 1e9 / items);
}

int main(void)
{
    unsigned seed = 7u;
    for (int i = 0; i < N * 3; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        a[i] = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 400.0f;
        seed = seed * 1664525u + 1013904223u;
        b[i] = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 400.0f;
    }
    RBTMATH_HEIGHT_FOG plane;
    memset(&plane, 0, sizeof(plane));
    plane.kind = RBTMATH_FOG_HEIGHT_KIND_PLANE;
    plane.up[1] = 1.0f;
    plane.density = 0.02f;
    plane.scale_height = 30.0f;
    RBTMATH_HEIGHT_FOG sphere = plane;
    sphere.kind = RBTMATH_FOG_HEIGHT_KIND_SPHERE;
    sphere.up[1] = 0.0f;
    sphere.base = 150.0f;
    RBTMATH_FOG_VOLUME box;
    memset(&box, 0, sizeof(box));
    box.shape = RBTMATH_FOG_VOLUME_SHAPE_BOX;
    box.half_extents[0] = box.half_extents[1] = box.half_extents[2] = 250.0f;
    box.edge = 20.0f;
    box.density = 0.1f;
    box.noise_strength = 0.6f;
    box.noise_tile = 40.0f;
    const float offset[3] = {0.1f, 0.2f, 0.3f};

    double t0 = now_seconds();
    double t1 = 0.0;
    for (int r = 0; r < ROUNDS; r++)
        rbtmath_height_fog_compute_density(&plane, a, N, out, NULL);
    report("a plane's density, a point", now_seconds() - t0, (double)N * ROUNDS);
    t0 = now_seconds();
    for (int r = 0; r < ROUNDS; r++)
        rbtmath_height_fog_compute_transmittance(&plane, a, b, N, out, NULL);
    report("a plane's transmittance, a segment", now_seconds() - t0, (double)N * ROUNDS);
    t0 = now_seconds();
    for (int r = 0; r < ROUNDS; r++)
        rbtmath_height_fog_compute_transmittance(&sphere, a, b, N, out, NULL);
    report("a sphere's transmittance, a segment", now_seconds() - t0, (double)N * ROUNDS);
    rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 1u, 32, 0, 32, worley, NULL);
    t0 = now_seconds();
    for (int r = 0; r < ROUNDS; r++)
        rbtmath_fog_volume_compute_density(&box, worley, 32, offset, a, N, out, NULL);
    report("a box volume's density with noise, a point", now_seconds() - t0, (double)N * ROUNDS);
    t0 = now_seconds();
    for (int r = 0; r < ROUNDS; r++)
        rbtmath_noise_volume_sample(worley, 32, b, N, out, NULL);
    report("a volume's read, a place", now_seconds() - t0, (double)N * ROUNDS);
    t0 = now_seconds();
    rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 1u, 32, 0, 32, worley, NULL);
    t1 = now_seconds();
    report("WORLEY bake (32^3), a texel", t1 - t0, 32.0 * 32 * 32);
    printf("%-52s %8.1f ms\n", "WORLEY bake 32^3, whole, one core", (t1 - t0) * 1e3);
    t0 = now_seconds();
    rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY, 1u, 128, 0, 4, base, NULL);
    t1 = now_seconds();
    report("PERLIN_WORLEY bake (four slices of 128^3), a texel", t1 - t0, 128.0 * 128 * 4);
    printf("%-52s %8.2f s\n", "PERLIN_WORLEY 128^3 at that rate, one core", (t1 - t0) * 32.0);
    sink = out[0];
    return 0;
}
