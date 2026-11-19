// The same bits on every platform (include/math/rbtmathclouds.h, "Same
// bits"; card A57's Math half, rbtmath_clouds_draft.h, test 9): 4,096
// densities of each of four layers (a shell of each warp, a slab of each
// address), over a 32^3 bake of each noise volume kind and a random map,
// each hashed, against values recorded on the first machine to run it
// (tests/data/rbtmathclouds/bits.inc); and the same again with subnormals
// flushed (arm64's FPCR.FZ, x86's FTZ and DAZ). The bakes' own bits are
// rbtmathfog_bits_tests'.
//
// If a change is meant to change one, run with --print, check the change is
// the intended one, and record the new values with what changed and why.
//
// Written in C, as the other tests of a C header are; built with contraction
// off, as the library is.

#include "math/rbtmathclouds.h"
#include "math/rbtmathnoise.h"

#include <fenv.h>
#include <float.h>
#if !defined(FE_DFL_DISABLE_DENORMS_ENV) && !defined(_MCW_DN) && defined(__has_include)
#if __has_include(<xmmintrin.h>) && __has_include(<pmmintrin.h>)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif
#endif
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static bool print_only = false;

// FNV-1a over bytes, 64 bits.
static uint64_t hash_bytes(uint64_t hash, const void *bytes, size_t size)
{
    const unsigned char *p = (const unsigned char *)bytes;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= p[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

#define FNV_START 0xCBF29CE484222325ull

typedef struct recorded_value
{
    const char *what;
    uint64_t hash;
} recorded_value;

static const recorded_value recorded[] = {
#include "data/rbtmathclouds/bits.inc"
    {"", 0},
};

static void check(const char *what, uint64_t hash)
{
    if (print_only)
    {
        printf("    {\"%s\", 0x%016llXull},\n", what, (unsigned long long)hash);
        return;
    }
    for (size_t i = 0; i < sizeof(recorded) / sizeof(recorded[0]); i++)
    {
        if (strcmp(recorded[i].what, what) == 0)
        {
            if (recorded[i].hash != hash)
            {
                printf("FAIL %s: 0x%016llX, recorded 0x%016llX\n", what, (unsigned long long)hash, (unsigned long long)recorded[i].hash);
                failures++;
            }
            return;
        }
    }
    printf("FAIL %s: not recorded\n", what);
    failures++;
}

#if defined(FE_DFL_DISABLE_DENORMS_ENV)
static fenv_t flush_saved;
static void flush_on(void) { fegetenv(&flush_saved); fesetenv(FE_DFL_DISABLE_DENORMS_ENV); }
static void flush_off(void) { fesetenv(&flush_saved); }
#define FLUSH_CONTROL 1
#elif defined(_MCW_DN)
static unsigned int flush_saved;
static void flush_on(void) { unsigned int now; _controlfp_s(&flush_saved, 0, 0); _controlfp_s(&now, _DN_FLUSH, _MCW_DN); }
static void flush_off(void) { unsigned int now; _controlfp_s(&now, flush_saved & _MCW_DN, _MCW_DN); }
#define FLUSH_CONTROL 1
#elif defined(_MM_FLUSH_ZERO_ON) && defined(_MM_DENORMALS_ZERO_ON)
static unsigned int flush_ftz, flush_daz;
static void flush_on(void)
{
    flush_ftz = _MM_GET_FLUSH_ZERO_MODE();
    flush_daz = _MM_GET_DENORMALS_ZERO_MODE();
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}
static void flush_off(void)
{
    _MM_SET_FLUSH_ZERO_MODE(flush_ftz);
    _MM_SET_DENORMALS_ZERO_MODE(flush_daz);
}
#define FLUSH_CONTROL 1
#endif

enum
{
    COUNT = 4096,
    BAKE  = 32,
    MAP   = 16
};

static uint32_t lcg = 0xC10D5u;
static uint32_t next_u32(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return lcg;
}
static float uniform(float lo, float hi) { return lo + (hi - lo) * ((float)(next_u32() >> 8) / 16777216.0f); }

static uint8_t base_vol[BAKE * BAKE * BAKE * 4];
static uint8_t detail_vol[BAKE * BAKE * BAKE * 4];
static uint8_t map[6 * MAP * MAP * 4];
static float points[4][COUNT * 3];
static float out[COUNT];
static uint64_t hashes[4];

static void layer(int which, RBTMATH_CLOUD_SHAPE *s, RBTMATH_CLOUD_BOUNDS *b, RBTMATH_CLOUD_OFFSETS *o)
{
    memset(s, 0, sizeof(*s));
    memset(b, 0, sizeof(*b));
    memset(o, 0, sizeof(*o));
    s->type_count = 3;
    const float prof[12] = {0.0f, 0.05f, 0.2f, 0.35f, 0.05f, 0.2f, 0.55f, 0.8f, 0.1f, 0.3f, 0.8f, 1.0f};
    memcpy(s->profiles, prof, sizeof(prof));
    s->base_tile      = 3000.0f;
    s->detail_tile    = 700.0f;
    s->erosion        = 0.45f;
    s->billow_height  = 0.6f;
    s->density_scale  = 1.0f;
    s->coverage_scale = 1.1f;
    s->coverage_bias  = -0.05f;
    o->base[0]        = 0.125f;
    o->base[1]        = -3.5f;
    o->base[2]        = 17.25f;
    o->detail[0]      = 2.5f;
    o->detail[1]      = -0.375f;
    o->detail[2]      = 9.0f;
    if (which < 2)
    {
        b->kind      = RBTMATH_CLOUD_BOUNDS_KIND_SHELL;
        b->bottom    = 6361000.0f;
        b->top       = 6365000.0f;
        b->cube_warp = which == 0 ? RBTMATH_CLOUD_CUBE_WARP_PLAIN : RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR;
    }
    else
    {
        b->kind        = RBTMATH_CLOUD_BOUNDS_KIND_SLAB;
        b->bottom      = 1500.0f;
        b->top         = 4000.0f;
        b->up[0]       = 0.6f;
        b->up[1]       = 0.8f;
        b->map_axis[0] = 0.8f;
        b->map_axis[1] = -0.6f;
        b->map_extent  = 40000.0f;
        b->map_address = which == 2 ? RBTMATH_CLOUD_MAP_ADDRESS_REPEAT : RBTMATH_CLOUD_MAP_ADDRESS_CLAMP;
        o->coverage[0] = 0.3f;
        o->coverage[1] = -0.85f;
    }
}

static void make_points(void)
{
    for (int which = 0; which < 4; which++)
        for (int i = 0; i < COUNT; i++)
        {
            float *p = points[which] + i * 3;
            if (which < 2)
            {
                float d[3], len;
                do
                {
                    d[0] = uniform(-1.0f, 1.0f);
                    d[1] = uniform(-1.0f, 1.0f);
                    d[2] = uniform(-1.0f, 1.0f);
                    len  = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                } while (len < 0.1f);
                const float r = uniform(6360000.0f, 6366000.0f);
                for (int a = 0; a < 3; a++)
                    p[a] = d[a] / len * r;
            }
            else
            {
                const float across = uniform(-60000.0f, 60000.0f), along = uniform(-60000.0f, 60000.0f);
                const float height = uniform(1000.0f, 4500.0f);
                p[0] = across * 0.8f + height * 0.6f;
                p[1] = across * -0.6f + height * 0.8f;
                p[2] = along;
            }
        }
}

static void run(bool record)
{
    static const char *names[4] = {"shell PLAIN densities", "shell EQUI_ANGULAR densities", "slab REPEAT densities",
                                   "slab CLAMP densities"};
    RBTMATH_CLOUD_TEXELS t;
    t.base          = base_vol;
    t.base_size     = BAKE;
    t.detail        = detail_vol;
    t.detail_size   = BAKE;
    t.coverage      = map;
    t.coverage_size = MAP;
    for (int which = 0; which < 4; which++)
    {
        RBTMATH_CLOUD_SHAPE s;
        RBTMATH_CLOUD_BOUNDS b;
        RBTMATH_CLOUD_OFFSETS o;
        layer(which, &s, &b, &o);
        RBTMATH_ERROR error;
        if (rbtmath_cloud_compute_density(&s, &b, &t, &o, points[which], COUNT, out, &error) != RBT_RESULT_OK)
        {
            printf("FAIL %s refused: %s\n", names[which], error.message);
            failures++;
        }
        const uint64_t h = hash_bytes(FNV_START, out, sizeof(out));
        if (record)
        {
            hashes[which] = h;
            check(names[which], h);
        }
        else if (h != hashes[which])
        {
            printf("FAIL %s with subnormals flushed: 0x%016llX, 0x%016llX without\n", names[which], (unsigned long long)h,
                   (unsigned long long)hashes[which]);
            failures++;
        }
    }
}

int main(int argc, char **argv)
{
    print_only = argc > 1 && strcmp(argv[1], "--print") == 0;
    RBTMATH_ERROR error;
    if (rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY, 0x4C415452u, BAKE, 0, BAKE, base_vol, &error) != RBT_RESULT_OK)
    {
        printf("FAIL the PERLIN_WORLEY bake refused: %s\n", error.message);
        failures++;
    }
    if (rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0x434C4F44u, BAKE, 0, BAKE, detail_vol, &error) != RBT_RESULT_OK)
    {
        printf("FAIL the WORLEY bake refused: %s\n", error.message);
        failures++;
    }
    for (size_t i = 0; i < sizeof(map); i++)
        map[i] = (uint8_t)(next_u32() >> 24);
    make_points();
    run(true);
#if defined(FLUSH_CONTROL)
    if (!print_only)
    {
        flush_on();
        run(false);
        flush_off();
    }
#endif
    if (print_only)
        return 0;
    if (failures)
    {
        printf("rbtmathclouds_bits_tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("rbtmathclouds_bits_tests: all passed\n");
    return 0;
}
