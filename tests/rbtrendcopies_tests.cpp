// Headless tests for the copies a staged buffer has in flight
// (src/render/rbtrendcopies_internal.hpp; card A62, RBTREND_TODO.md item 61).
//
// The list is the part of a staged buffer's bookkeeping that is only
// arithmetic: which of its bytes a submission not known to have finished is
// still copying to the device, with that submission's serial. It is an
// internal header, which this one test includes on purpose, as
// rbtrendranges_tests.cpp includes the pools' range list.

#include "rbtrendcopies_internal.hpp"
#include <stdint.h>
#include <stdio.h>
#include <vector>

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

// ---- Helpers ------------------------------------------------------------------

// The same sequence on every platform and every run.
static uint32_t random_state = 0x2545F491u;
static uint32_t next_random()
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

// The slow obvious answer: every range, kept or not, looked at one by one.
static size_t reference_overlap(const std::vector<rbtrend_copy_in_flight> &ranges, uint64_t completed, uint64_t offset,
                                uint64_t size)
{
    for (size_t i = 0; i < ranges.size(); i++)
    {
        if (ranges[i].serial <= completed)
            continue;
        const bool before = ranges[i].offset + ranges[i].size <= offset;
        const bool after = ranges[i].offset >= offset + size;
        if (!before && !after)
            return i;
    }
    return SIZE_MAX;
}

// ---- Tests --------------------------------------------------------------------

static void test_empty_list()
{
    current_test = "empty list";
    eq::array<rbtrend_copy_in_flight> copies;
    CHECK(rbtrend_copies_find_overlap(copies, 0, 1) == SIZE_MAX);
    rbtrend_copies_prune(copies, 0);
    rbtrend_copies_prune(copies, UINT64_MAX);
    CHECK(copies.empty());
    // No room reserved: the append refuses rather than allocate.
    CHECK(!rbtrend_copies_append(copies, 0, 16, 1));
    CHECK(copies.empty());
}

static void test_append_and_prune()
{
    current_test = "append and prune";
    eq::array<rbtrend_copy_in_flight> copies;
    CHECK(rbtrend_copies_reserve(copies, 4));
    CHECK(rbtrend_copies_append(copies, 0, 16, 3));
    CHECK(rbtrend_copies_append(copies, 64, 16, 3));
    CHECK(rbtrend_copies_append(copies, 32, 8, 5));
    CHECK(rbtrend_copies_append(copies, 128, 8, 6));
    CHECK(copies.size() == 4);
    // Room for four, all taken: a fifth is refused, nothing changed.
    CHECK(!rbtrend_copies_append(copies, 256, 8, 7));
    CHECK(copies.size() == 4);

    // Nothing finished: nothing goes.
    rbtrend_copies_prune(copies, 2);
    CHECK(copies.size() == 4);
    // Serial 3 finished: both of its ranges go, from the front.
    rbtrend_copies_prune(copies, 3);
    CHECK(copies.size() == 2);
    CHECK(copies[0].offset == 32 && copies[0].serial == 5);
    // Past 5 but short of 6.
    rbtrend_copies_prune(copies, 5);
    CHECK(copies.size() == 1);
    CHECK(copies[0].offset == 128);
    rbtrend_copies_prune(copies, 6);
    CHECK(copies.empty());
}

static void test_overlap_and_touch()
{
    current_test = "overlap and touch";
    eq::array<rbtrend_copy_in_flight> copies;
    CHECK(rbtrend_copies_reserve(copies, 2));
    CHECK(rbtrend_copies_append(copies, 100, 50, 1)); // bytes 100 to 149
    CHECK(rbtrend_copies_append(copies, 300, 10, 2)); // bytes 300 to 309

    // Touching is not overlapping.
    CHECK(rbtrend_copies_find_overlap(copies, 0, 100) == SIZE_MAX);
    CHECK(rbtrend_copies_find_overlap(copies, 150, 150) == SIZE_MAX);
    CHECK(rbtrend_copies_find_overlap(copies, 310, 1) == SIZE_MAX);
    // One byte in, at either end.
    CHECK(rbtrend_copies_find_overlap(copies, 0, 101) == 0);
    CHECK(rbtrend_copies_find_overlap(copies, 149, 1) == 0);
    CHECK(rbtrend_copies_find_overlap(copies, 309, 4) == 1);
    // Inside, and around.
    CHECK(rbtrend_copies_find_overlap(copies, 120, 2) == 0);
    CHECK(rbtrend_copies_find_overlap(copies, 50, 1000) == 0);
    CHECK(rbtrend_copies_find_overlap(copies, 200, 1000) == 1);
    // At the very top of the address range, no sum wraps.
    CHECK(rbtrend_copies_find_overlap(copies, UINT64_MAX - 4, 4) == SIZE_MAX);
}

static void test_reserve_keeps_append_allocation_free()
{
    current_test = "reserve keeps the append allocation-free";
    eq::array<rbtrend_copy_in_flight> copies;
    CHECK(rbtrend_copies_reserve(copies, 8));
    const rbtrend_copy_in_flight *data = copies.data;
    const size_t capacity = copies.capacity;
    CHECK(capacity >= 8);
    // A steady state: four in, the oldest out, again and again. The storage
    // never moves and never grows.
    uint64_t serial = 1;
    for (int round = 0; round < 100; round++)
    {
        for (int i = 0; i < 4; i++)
            CHECK(rbtrend_copies_append(copies, (uint64_t)i * 64, 32, serial));
        if (serial > 1)
            rbtrend_copies_prune(copies, serial - 1);
        CHECK(copies.size() <= 8);
        serial++;
        // A reserve that already has its room changes nothing.
        CHECK(rbtrend_copies_reserve(copies, 4));
    }
    CHECK(copies.data == data);
    CHECK(copies.capacity == capacity);
}

static void test_against_brute_force()
{
    current_test = "against brute force";
    eq::array<rbtrend_copy_in_flight> copies;
    std::vector<rbtrend_copy_in_flight> all;
    uint64_t serial = 1;
    uint64_t completed = 0;
    for (uint32_t step = 0; step < 20000; step++)
    {
        const uint32_t what = next_random() % 8;
        if (what < 3)
        {
            // A submission copying up to three ranges.
            const uint32_t count = 1 + next_random() % 3;
            CHECK(rbtrend_copies_reserve(copies, count));
            serial++;
            for (uint32_t i = 0; i < count; i++)
            {
                const uint64_t offset = next_random() % 4096;
                const uint64_t size = 1 + next_random() % 256;
                CHECK(rbtrend_copies_append(copies, offset, size, serial));
                all.push_back({offset, size, serial});
            }
        }
        else if (what < 5)
        {
            // The GPU moves on, never past the last serial taken.
            if (completed < serial)
                completed += 1 + next_random() % (uint32_t)(serial - completed);
            rbtrend_copies_prune(copies, completed);
        }
        else
        {
            const uint64_t offset = next_random() % 4400;
            const uint64_t size = 1 + next_random() % 300;
            const size_t expected = reference_overlap(all, completed, offset, size);
            rbtrend_copies_prune(copies, completed);
            const size_t found = rbtrend_copies_find_overlap(copies, offset, size);
            if ((expected == SIZE_MAX) != (found == SIZE_MAX))
            {
                printf("FAIL %s: the list and the reference disagree at step %u\n", current_test, step);
                failures++;
                return;
            }
            // The first overlapping range kept is the reference's first
            // overlapping range not finished.
            if (found != SIZE_MAX)
            {
                const rbtrend_copy_in_flight &mine = copies[found];
                const rbtrend_copy_in_flight &theirs = all[expected];
                CHECK(mine.offset == theirs.offset && mine.size == theirs.size && mine.serial == theirs.serial);
            }
        }
        // What is kept is in serial order, and every kept serial unfinished.
        for (size_t i = 0; i < copies.size(); i++)
        {
            if (i > 0)
                CHECK(copies[i - 1].serial <= copies[i].serial);
        }
    }
}

int main()
{
    test_empty_list();
    test_append_and_prune();
    test_overlap_and_touch();
    test_reserve_keeps_append_allocation_free();
    test_against_brute_force();

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all copies-in-flight tests passed\n");
    return 0;
}
