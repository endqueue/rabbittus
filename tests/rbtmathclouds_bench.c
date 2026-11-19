// The cost of a cloud layer's density (rbtmathclouds.h; card A57's Math
// half, rbtmath_clouds_draft.h, test 11): ns a point for a shell of each
// warp and a slab, over Nubis's sizes (a 128^3 PERLIN_WORLEY, a 32^3 WORLEY)
// baked here, the points spread through the layer. Release, by hand; not a
// ctest. The numbers go into RBTMATH_TODO.md, "Cloud density".

#include "math/rbtmathclouds.h"
#include "math/rbtmathnoise.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ns(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static uint32_t lcg = 11u;
static float uniform(float lo, float hi)
{
    lcg = lcg * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((float)(lcg >> 8) / 16777216.0f);
}

enum
{
    N   = 1 << 16,
    MAP = 512
};
static float points[N * 3], out[N];

int main(void)
{
    const uint32_t bs = 128, ds = 32;
    uint8_t *base   = (uint8_t *)malloc((size_t)bs * bs * bs * 4);
    uint8_t *detail = (uint8_t *)malloc((size_t)ds * ds * ds * 4);
    uint8_t *map    = (uint8_t *)malloc((size_t)6 * MAP * MAP * 4);
    if (!base || !detail || !map)
        return 1;
    double t0 = now_ns();
    rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY, 1u, bs, 0, bs, base, NULL);
    double t1 = now_ns();
    rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 2u, ds, 0, ds, detail, NULL);
    double t2 = now_ns();
    printf("bake PERLIN_WORLEY 128^3: %.2f s (%.1f ns a texel); WORLEY 32^3: %.1f ms\n", (t1 - t0) * 1e-9, (t1 - t0) / (bs * bs * bs),
           (t2 - t1) * 1e-6);
    for (size_t i = 0; i < (size_t)6 * MAP * MAP * 4; i++)
        map[i] = (uint8_t)(i * 2654435761u >> 24);
    RBTMATH_CLOUD_TEXELS t = {base, bs, detail, ds, map, MAP};
    RBTMATH_CLOUD_SHAPE s;
    memset(&s, 0, sizeof(s));
    s.type_count = 2;
    const float prof[8] = {0.0f, 0.1f, 0.3f, 0.5f, 0.05f, 0.25f, 0.7f, 1.0f};
    memcpy(s.profiles, prof, sizeof(prof));
    s.base_tile = 4000.0f;
    s.detail_tile = 800.0f;
    s.erosion = 0.4f;
    s.billow_height = 0.5f;
    s.density_scale = 1.0f;
    s.coverage_scale = 1.0f;
    RBTMATH_CLOUD_OFFSETS o;
    memset(&o, 0, sizeof(o));
    for (int which = 0; which < 3; which++)
    {
        RBTMATH_CLOUD_BOUNDS b;
        memset(&b, 0, sizeof(b));
        if (which < 2)
        {
            b.kind = RBTMATH_CLOUD_BOUNDS_KIND_SHELL;
            b.bottom = 6361000.0f;
            b.top = 6365000.0f;
            b.cube_warp = which == 0 ? RBTMATH_CLOUD_CUBE_WARP_PLAIN : RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR;
            for (int i = 0; i < N; i++)
            {
                float d[3] = {uniform(-1, 1), uniform(-1, 1), uniform(-1, 1)};
                float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-6f;
                float r = uniform(6361000.0f, 6365000.0f);
                for (int a = 0; a < 3; a++)
                    points[i * 3 + a] = d[a] / l * r;
            }
        }
        else
        {
            b.kind = RBTMATH_CLOUD_BOUNDS_KIND_SLAB;
            b.bottom = 1500.0f;
            b.top = 4000.0f;
            b.up[1] = 1.0f;
            b.map_axis[0] = 1.0f;
            b.map_extent = 50000.0f;
            b.map_address = RBTMATH_CLOUD_MAP_ADDRESS_REPEAT;
            for (int i = 0; i < N; i++)
            {
                points[i * 3 + 0] = uniform(-50000, 50000);
                points[i * 3 + 1] = uniform(1500, 4000);
                points[i * 3 + 2] = uniform(-50000, 50000);
            }
        }
        double best = 1e30;
        for (int rep = 0; rep < 10; rep++)
        {
            double a = now_ns();
            rbtmath_cloud_compute_density(&s, &b, &t, &o, points, N, out, NULL);
            double z = now_ns();
            if (z - a < best)
                best = z - a;
        }
        int inside = 0;
        for (int i = 0; i < N; i++)
            inside += out[i] > 0.0f;
        static const char *names[3] = {"shell PLAIN", "shell EQUI_ANGULAR", "slab REPEAT"};
        printf("%-20s %.1f ns a point (%d of %d with cloud)\n", names[which], best / N, inside, N);
    }
    free(base);
    free(detail);
    free(map);
    return 0;
}
