// The same bits on every platform (include/math/rbtmathfog.h, "Same bits";
// card A59's Math half, rbtmath_fog_draft.h, test 7; and the baked noise
// volumes of rbtmathnoise.h, card A57's Math half, its test 9's bakes): a
// fixed run of 4,096 densities and transmittances of each height fog kind,
// 4,096 densities of each volume shape with and without noise, and a 32^3
// bake of each noise volume kind, each hashed, against values recorded on
// the first machine to run it (tests/data/rbtmathfog/bits.inc).
//
// If a change is meant to change one, run with --print, check the change is
// the intended one, and record the new values with what changed and why.
//
// Written in C, as the other tests of a C header are; built with contraction
// off, as the library is.

#include "math/rbtmathfog.h"
#include "math/rbtmathnoise.h"

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
#include "data/rbtmathfog/bits.inc"
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

enum
{
    COUNT = 4096,
    BAKE  = 32
};

static uint32_t lcg = 0xF06u;
static float uniform(float lo, float hi)
{
    lcg = lcg * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((float)(lcg >> 8) / 16777216.0f);
}

static float a[COUNT * 3], b[COUNT * 3], out[COUNT];
static uint8_t bake[BAKE * BAKE * BAKE * 4];
static uint8_t noise[16 * 16 * 16 * 4];

static void fog_bits(const char *name, const RBTMATH_HEIGHT_FOG *fog, float extent)
{
    for (int i = 0; i < COUNT * 3; i++)
    {
        a[i] = uniform(-extent, extent);
        b[i] = uniform(-extent, extent);
    }
    char what[96];
    RBTMATH_ERROR error;
    if (rbtmath_height_fog_compute_density(fog, a, COUNT, out, &error) != RBT_RESULT_OK)
    {
        printf("FAIL %s density refused: %s\n", name, error.message);
        failures++;
    }
    snprintf(what, sizeof(what), "%s density", name);
    check(what, hash_bytes(FNV_START, out, sizeof(out)));
    if (rbtmath_height_fog_compute_transmittance(fog, a, b, COUNT, out, &error) != RBT_RESULT_OK)
    {
        printf("FAIL %s transmittance refused: %s\n", name, error.message);
        failures++;
    }
    snprintf(what, sizeof(what), "%s transmittance", name);
    check(what, hash_bytes(FNV_START, out, sizeof(out)));
}

static void volume_bits(const char *name, const RBTMATH_FOG_VOLUME *v, const uint8_t *n, uint32_t size)
{
    for (int i = 0; i < COUNT * 3; i++)
        a[i] = uniform(-6.0f, 6.0f);
    const float offset[3] = {0.375f, -11.25f, 4.0f};
    RBTMATH_ERROR error;
    if (rbtmath_fog_volume_compute_density(v, n, size, n ? offset : NULL, a, COUNT, out, &error) != RBT_RESULT_OK)
    {
        printf("FAIL %s refused: %s\n", name, error.message);
        failures++;
    }
    check(name, hash_bytes(FNV_START, out, sizeof(out)));
}

int main(int argc, char **argv)
{
    print_only = argc > 1 && strcmp(argv[1], "--print") == 0;

    RBTMATH_HEIGHT_FOG plane;
    memset(&plane, 0, sizeof(plane));
    plane.kind         = RBTMATH_FOG_HEIGHT_KIND_PLANE;
    plane.up[0]        = 0.6f;
    plane.up[1]        = 0.8f;
    plane.base         = -3.0f;
    plane.density      = 0.02f;
    plane.scale_height = 40.0f;
    fog_bits("plane", &plane, 400.0f);

    RBTMATH_HEIGHT_FOG sphere;
    memset(&sphere, 0, sizeof(sphere));
    sphere.kind         = RBTMATH_FOG_HEIGHT_KIND_SPHERE;
    sphere.base         = 1000.0f;
    sphere.density      = 0.01f;
    sphere.scale_height = 30.0f;
    fog_bits("sphere", &sphere, 1200.0f);

    RBTMATH_ERROR error;
    if (rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0xF06u, 16, 0, 16, noise, &error) != RBT_RESULT_OK)
    {
        printf("FAIL the volumes' noise refused: %s\n", error.message);
        failures++;
    }
    RBTMATH_FOG_VOLUME box;
    memset(&box, 0, sizeof(box));
    box.shape           = RBTMATH_FOG_VOLUME_SHAPE_BOX;
    box.half_extents[0] = 5.0f;
    box.half_extents[1] = 2.5f;
    box.half_extents[2] = 4.0f;
    box.edge            = 1.5f;
    box.density         = 0.3f;
    volume_bits("box", &box, NULL, 0);
    box.noise_strength = 0.8f;
    box.noise_tile     = 3.0f;
    volume_bits("box with noise", &box, noise, 16);
    RBTMATH_FOG_VOLUME ball;
    memset(&ball, 0, sizeof(ball));
    ball.shape           = RBTMATH_FOG_VOLUME_SHAPE_SPHERE;
    ball.half_extents[0] = 5.5f;
    ball.edge            = 2.0f;
    ball.density         = 0.7f;
    volume_bits("sphere volume", &ball, NULL, 0);
    ball.noise_strength = 0.4f;
    ball.noise_tile     = 1.75f;
    volume_bits("sphere volume with noise", &ball, noise, 16);

    if (rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY, 0x4C415452u, BAKE, 0, BAKE, bake, &error) != RBT_RESULT_OK)
    {
        printf("FAIL the PERLIN_WORLEY bake refused: %s\n", error.message);
        failures++;
    }
    check("PERLIN_WORLEY 32^3 bake", hash_bytes(FNV_START, bake, sizeof(bake)));
    if (rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0x434C4F44u, BAKE, 0, BAKE, bake, &error) != RBT_RESULT_OK)
    {
        printf("FAIL the WORLEY bake refused: %s\n", error.message);
        failures++;
    }
    check("WORLEY 32^3 bake", hash_bytes(FNV_START, bake, sizeof(bake)));

    if (print_only)
        return 0;
    if (failures)
    {
        printf("rbtmathfog_bits_tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("rbtmathfog_bits_tests: all passed\n");
    return 0;
}
