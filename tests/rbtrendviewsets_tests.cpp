// Headless tests of which record a lit scene's view-set records give a new
// set (src/render/rbtrendviewsets_internal.hpp, card A67): the choice is
// plain logic over plain records and an answer of whether a record's set
// is held, so it is tested here with no device. An internal header, which
// this one test includes on purpose.

#include "rbtrendviewsets_internal.hpp"
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

// A set's handle for a test: any address, never dereferenced.
static rbtrend_binding_set fake_set(uintptr_t n)
{
    return (rbtrend_binding_set)(0x1000 + n * 0x10);
}

// Held: the sets in the list [user] points at, ended by NULL.
static bool held_in(const void *user, rbtrend_binding_set set)
{
    for (const rbtrend_binding_set *s = (const rbtrend_binding_set *)user; *s; ++s)
        if (*s == set)
            return true;
    return false;
}

static void test_empty_first(void)
{
    current_test = "empty_first";
    rbtrend_view_set records[4];
    memset(records, 0, sizeof(records));
    records[0].set = fake_set(0);
    records[0].used = 1;
    records[2].set = fake_set(2);
    records[2].used = 0;
    const rbtrend_binding_set none[1] = {nullptr};
    // Record 1 is empty: chosen before record 2, handed out least recently.
    CHECK(rbtrend_view_set_choose(records, 4, held_in, none) == 1u);
}

static void test_least_recent_not_held(void)
{
    current_test = "least_recent_not_held";
    rbtrend_view_set records[4];
    memset(records, 0, sizeof(records));
    for (uint32_t i = 0; i < 4; ++i)
    {
        records[i].set = fake_set(i);
        records[i].used = 10 + (i * 7) % 4; // 10, 13, 12, 11
    }
    const rbtrend_binding_set none[1] = {nullptr};
    CHECK(rbtrend_view_set_choose(records, 4, held_in, none) == 0u);
    // Record 0 held: the next least recent, record 3 (11).
    const rbtrend_binding_set first[2] = {fake_set(0), nullptr};
    CHECK(rbtrend_view_set_choose(records, 4, held_in, first) == 3u);
    // 0 and 3 held: record 2 (12).
    const rbtrend_binding_set two[3] = {fake_set(0), fake_set(3), nullptr};
    CHECK(rbtrend_view_set_choose(records, 4, held_in, two) == 2u);
}

static void test_held_never_chosen(void)
{
    current_test = "held_never_chosen";
    rbtrend_view_set records[3];
    memset(records, 0, sizeof(records));
    for (uint32_t i = 0; i < 3; ++i)
    {
        records[i].set = fake_set(i);
        records[i].used = i;
    }
    // Every choice over every subset held: never a held record, and
    // UINT32_MAX only when all are held.
    for (uint32_t mask = 0; mask < 8; ++mask)
    {
        rbtrend_binding_set held[4] = {nullptr, nullptr, nullptr, nullptr};
        uint32_t n = 0;
        for (uint32_t i = 0; i < 3; ++i)
            if (mask & (1u << i))
                held[n++] = fake_set(i);
        const uint32_t chosen = rbtrend_view_set_choose(records, 3, held_in, held);
        if (mask == 7u)
            CHECK(chosen == UINT32_MAX);
        else
        {
            CHECK(chosen < 3u);
            if (chosen < 3u)
                CHECK((mask & (1u << chosen)) == 0u);
            // The least recent of those not held.
            for (uint32_t i = 0; i < 3 && chosen < 3u; ++i)
                if (!(mask & (1u << i)))
                    CHECK(records[chosen].used <= records[i].used);
        }
    }
}

static void test_none(void)
{
    current_test = "none";
    const rbtrend_binding_set none[1] = {nullptr};
    CHECK(rbtrend_view_set_choose(nullptr, 0, held_in, none) == UINT32_MAX);
    rbtrend_view_set one;
    memset(&one, 0, sizeof(one));
    one.set = fake_set(5);
    const rbtrend_binding_set held[2] = {fake_set(5), nullptr};
    CHECK(rbtrend_view_set_choose(&one, 1, held_in, held) == UINT32_MAX);
    CHECK(rbtrend_view_set_choose(&one, 1, held_in, none) == 0u);
}

static void test_record_size(void)
{
    current_test = "record_size";
    // 120 bytes a record on a 64-bit build, as rbtrendlit.h states.
    if (sizeof(void *) == 8)
        CHECK(sizeof(rbtrend_view_set) == 120);
}

int main(void)
{
    test_empty_first();
    test_least_recent_not_held();
    test_held_never_chosen();
    test_none();
    test_record_size();
    if (failures)
    {
        printf("rbtrendviewsets_tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("rbtrendviewsets_tests: all passed\n");
    return 0;
}
