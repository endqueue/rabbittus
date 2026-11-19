// Headless tests of the history records Rend keeps a view inside its
// volumetric objects (src/render/rbtrendhistory.hpp; card A59's fog,
// rbtrend_fog_draft.h section 5, and card A57's clouds,
// rbtrend_clouds_draft.h section 4, share it): which record a scene takes
// (its own, an empty one, else the least recently used one no list holds,
// else none), when its history starts again, the jitter sequence, and a
// place carried into last frame's clip space through the space's shift.
// Plain logic with no device, as the view-set records' tests.

#include "rbtrendhistory.hpp"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static const char *current_test = "";

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("FAIL %s (%s:%d): %s\n", current_test, __FILE__, __LINE__, #condition);                           \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static const void *scene(uintptr_t n)
{
    return (const void *)(0x2000 + n * 0x10);
}

// Held: the records whose indices are in the list [user] points at, ended
// by UINT32_MAX.
static bool held_in(const void *user, uint32_t record)
{
    for (const uint32_t *r = (const uint32_t *)user; *r != UINT32_MAX; ++r)
        if (*r == record)
            return true;
    return false;
}

static const uint32_t none_held[1] = {UINT32_MAX};

static void test_take(void)
{
    current_test = "test_take";
    rbtrend_history_record records[3];
    memset(records, 0, sizeof(records));
    uint64_t clock = 0;
    bool reset = false;
    // Empty first, each a reset.
    uint32_t a = rbtrend_history_take(records, 3, scene(1), ++clock, held_in, none_held, &reset);
    CHECK(a == 0 && reset);
    uint32_t b = rbtrend_history_take(records, 3, scene(2), ++clock, held_in, none_held, &reset);
    CHECK(b == 1 && reset);
    // Its own again, no reset once its history has a frame.
    rbtrend_history_mark_written(&records[a]);
    uint32_t again = rbtrend_history_take(records, 3, scene(1), ++clock, held_in, none_held, &reset);
    CHECK(again == a && !reset);
    // A record never written resets even for its own scene.
    again = rbtrend_history_take(records, 3, scene(2), ++clock, held_in, none_held, &reset);
    CHECK(again == b && reset);
    uint32_t c = rbtrend_history_take(records, 3, scene(3), ++clock, held_in, none_held, &reset);
    CHECK(c == 2 && reset);
    // Full: the least recently used, scene 1's (used at 3), taken by scene 4.
    uint32_t d = rbtrend_history_take(records, 3, scene(4), ++clock, held_in, none_held, &reset);
    CHECK(d == a && reset);
    CHECK(records[d].scene == scene(4));
    // Every record held: none.
    const uint32_t all_held[4] = {0, 1, 2, UINT32_MAX};
    CHECK(rbtrend_history_take(records, 3, scene(5), ++clock, held_in, all_held, &reset) == UINT32_MAX);
    // A held record is passed over for the next least recent.
    const uint32_t first_held[2] = {1, UINT32_MAX};
    uint32_t e = rbtrend_history_take(records, 3, scene(5), ++clock, held_in, first_held, &reset);
    CHECK(e == 2);
    // A scene's own record is its own even when held (the same list's).
    CHECK(rbtrend_history_take(records, 3, scene(5), ++clock, held_in, all_held, &reset) == e);
    // No records at all.
    CHECK(rbtrend_history_take(records, 0, scene(1), ++clock, held_in, none_held, &reset) == UINT32_MAX);
}

static void test_jitter(void)
{
    current_test = "test_jitter";
    float seen[RBTREND_HISTORY_JITTERS];
    for (uint32_t i = 0; i < RBTREND_HISTORY_JITTERS; ++i)
    {
        seen[i] = rbtrend_history_jitter(i);
        CHECK(seen[i] > 0.0f && seen[i] < 1.0f);
        for (uint32_t j = 0; j < i; ++j)
            CHECK(seen[j] != seen[i]);
        // A fixed sequence, repeating.
        CHECK(rbtrend_history_jitter(i + RBTREND_HISTORY_JITTERS) == seen[i]);
    }
    // Van der Corput's in base 2 over 16, each at the middle of its step:
    // 0.5/16, 8.5/16, 4.5/16, 12.5/16 ...
    CHECK(seen[0] == 0.5f / 16.0f);
    CHECK(seen[1] == 8.5f / 16.0f);
    CHECK(seen[2] == 4.5f / 16.0f);
    CHECK(seen[3] == 12.5f / 16.0f);
    // Their mean is a half: no bias in depth.
    double sum = 0.0;
    for (uint32_t i = 0; i < RBTREND_HISTORY_JITTERS; ++i)
        sum += seen[i];
    CHECK(fabs(sum / RBTREND_HISTORY_JITTERS - 0.5) < 1e-6);
}

static void test_reproject(void)
{
    current_test = "test_reproject";
    // Last frame's view-projection: the identity but for a perspective w =
    // -z (column-major).
    float vp[16];
    memset(vp, 0, sizeof(vp));
    vp[0] = 1.0f;
    vp[5] = 1.0f;
    vp[10] = 1.0f;
    vp[11] = -1.0f;
    const float shift[3] = {0.0f, 0.0f, 0.0f};
    const float p[3] = {0.5f, -0.25f, -2.0f};
    float ndc[3];
    CHECK(rbtrend_history_reproject(vp, shift, p, ndc));
    CHECK(fabsf(ndc[0] - 0.25f) < 1e-6f && fabsf(ndc[1] + 0.125f) < 1e-6f && fabsf(ndc[2] + 1.0f) < 1e-6f);
    // This frame's origin moved +1 in x in last frame's space: the place is
    // 1 further along x there.
    const float moved[3] = {1.0f, 0.0f, 0.0f};
    CHECK(rbtrend_history_reproject(vp, moved, p, ndc));
    CHECK(fabsf(ndc[0] - 0.75f) < 1e-6f);
    // Behind last frame's eye: no history there.
    const float behind[3] = {0.0f, 0.0f, 3.0f};
    CHECK(!rbtrend_history_reproject(vp, shift, behind, ndc));
}

// The clouds' order (rbtrend_clouds_draft.h version 2, section 4, card
// A70): each march texel's 4 x 4 target texels each marched once in
// sixteen frames, each history texel's four once in its four visits, the
// history texels of frames 0 to 3 {0, 3, 1, 2}; each history texel's step
// offsets over its first sixteen visits J's sixteen values.
static void test_clouds_order(void)
{
    current_test = "test_clouds_order";
    bool seen[16] = {false};
    bool seen_in[4][4] = {{false}};
    for (uint32_t f = 0; f < 16; ++f)
    {
        float place[2];
        rbtrend_history_clouds_place(f, place);
        CHECK(place[0] > 0.0f && place[0] < 4.0f && place[1] > 0.0f && place[1] < 4.0f);
        const uint32_t x = (uint32_t)place[0], y = (uint32_t)place[1];
        if (x > 3 || y > 3)
            continue;
        // Through a target texel's centre.
        CHECK(place[0] == (float)x + 0.5f && place[1] == (float)y + 0.5f);
        CHECK(!seen[y * 4 + x]);
        seen[y * 4 + x] = true;
        // The history texel it lies in, and where in it.
        const uint32_t texel = rbtrend_history_clouds_texel(f);
        CHECK(texel == (x >> 1) + 2u * (y >> 1));
        const uint32_t within = (x & 1u) + 2u * (y & 1u);
        CHECK(!seen_in[texel][within]);
        seen_in[texel][within] = true;
    }
    for (uint32_t i = 0; i < 16; ++i)
        CHECK(seen[i]);
    for (uint32_t t = 0; t < 4; ++t)
        for (uint32_t w = 0; w < 4; ++w)
            CHECK(seen_in[t][w]);
    // Frames 0 to 3 as built: top left, bottom right, top right, bottom left.
    const uint32_t first[4] = {0u, 3u, 1u, 2u};
    for (uint32_t f = 0; f < 4; ++f)
        CHECK(rbtrend_history_clouds_texel(f) == first[f]);
    // Each history texel is marched once in every four frames, and its step
    // offsets over its first sixteen visits are J's sixteen values.
    for (uint32_t t = 0; t < 4; ++t)
    {
        bool offsets[RBTREND_HISTORY_JITTERS] = {false};
        uint32_t visits = 0;
        for (uint32_t f = 0; f < 4 * RBTREND_HISTORY_JITTERS; ++f)
        {
            if (rbtrend_history_clouds_texel(f) != t)
                continue;
            CHECK(f / 4 == visits);
            ++visits;
            const float z = rbtrend_history_clouds_step(f);
            for (uint32_t j = 0; j < RBTREND_HISTORY_JITTERS; ++j)
                if (z == rbtrend_history_jitter(j))
                {
                    CHECK(!offsets[j]);
                    offsets[j] = true;
                }
        }
        CHECK(visits == RBTREND_HISTORY_JITTERS);
        for (uint32_t j = 0; j < RBTREND_HISTORY_JITTERS; ++j)
            CHECK(offsets[j]);
    }
    // Every texel marched in one frame is on the same visit, so the step's
    // offset is the visit's: the same in each run of four frames.
    for (uint32_t f = 0; f < 64; ++f)
        CHECK(rbtrend_history_clouds_step(f) == rbtrend_history_clouds_step(f - f % 4));
}

// The weights: the clouds' marched texel by its visit, 1, 1/2, 1/3, then
// the floor; the fog's history share by its frames as built (an even share
// while below its weight), bit for bit.
static void test_weights(void)
{
    current_test = "test_weights";
    const float want[5] = {1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 0.25f};
    for (uint32_t v = 0; v < 5; ++v)
        for (uint32_t f = 4 * v; f < 4 * v + 4; ++f)
            CHECK(fabsf(rbtrend_history_clouds_weight(f) - want[v]) < 1e-6f);
    for (uint32_t f = 20; f < 1000; ++f)
        CHECK(rbtrend_history_clouds_weight(f) == RBTREND_HISTORY_CLOUDS_WEIGHT_FLOOR);
    const float weights[5] = {0.0f, 0.5f, 0.9f, 0.95f, 0.999f};
    for (uint32_t w = 0; w < 5; ++w)
        for (uint32_t n = 0; n < 2000; ++n)
        {
            // rbtrendcommands.cpp's fog block before the module took it.
            const float even = (float)n / (float)(n + 1);
            const float built = weights[w] < even ? weights[w] : even;
            const float got = rbtrend_history_share(n, weights[w]);
            CHECK(memcmp(&got, &built, sizeof(float)) == 0);
        }
}

int main(void)
{
    test_take();
    test_jitter();
    test_reproject();
    test_clouds_order();
    test_weights();
    if (failures)
    {
        printf("rbtrendhistory_tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("rbtrendhistory_tests: all passed\n");
    return 0;
}
