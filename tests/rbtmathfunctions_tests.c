// Rabbittus Math's elementary functions (include/math/rbtmathfunctions.h;
// card A61, the approved draft's section 10, tests 1 to 7), through the
// public API:
//   1. special values, each against its expected bits; atan2's scaled
//      carry against the reference table;
//   2. exact cases: exp2(n) and log2(2^n) for every whole n in range;
//   3. tan and atan odd, bit for bit, over 2^20 inputs;
//   4. accuracy: every result within 1 ulp of the correctly rounded one
//      (tests/data/rbtmathfunctions/reference.inc, made by MPFR at 300 bits
//      by tests/rbtmathfunctions_reference.c), checked as integers: a
//      faithful result's encoding is the reference's or a neighbour's;
//   5. arrays: in place equal to apart, one call equal to 16 slices, a
//      count of 0 writing nothing;
//   6. refusals, in the header's order, each with its message, the output
//      untouched;
//   7. two calls at once from two threads.
//
// A call that returns RBT_RESULT_NOT_IMPLEMENTED marks its test pending and
// ends it; the refusals are checked as they are.
//
// Written in C, as the other tests of a C header are; built with contraction
// off, as the library is.

#include "jobs/rbtjobs.h"
#include "math/rbtmathfunctions.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int pending = 0;
static const char *current_test = "";
static bool test_pending = false;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("FAIL %s (%s:%d): %s\n", current_test, __FILE__, __LINE__, #condition);                           \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

// True if [result] is from a built call; marks the test pending if not.
static bool built(RBT_RESULT result, int line)
{
    if (result == RBT_RESULT_NOT_IMPLEMENTED)
    {
        if (!test_pending)
            printf("PENDING %s (%s:%d): not implemented yet\n", current_test, __FILE__, line);
        test_pending = true;
        return false;
    }
    return true;
}

#define BUILT(result) built((result), __LINE__)

static uint64_t bits_of(double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    return u;
}

static double from_bits(uint64_t u)
{
    double v;
    memcpy(&v, &u, 8);
    return v;
}

#define QNAN_BITS 0x7FF8000000000000ull
#define POS_INF   0x7FF0000000000000ull
#define NEG_INF   0xFFF0000000000000ull

// Repeatable inputs from integers.
static uint64_t state = 0x5EED5EED5EED5EEDull;
static uint64_t next(void)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// ---- One element through any function ----------------------------------------------

typedef enum { F_EXP2, F_EXP, F_LOG2, F_ATAN, F_ATAN2, F_TAN, F_COUNT } function;
static const char *const function_names[F_COUNT] = {"exp2", "exp", "log2", "atan", "atan2", "tan"};

static RBT_RESULT compute(function f, const double *a, const double *b, uint32_t count, double *out, RBTMATH_ERROR *error)
{
    switch (f)
    {
    case F_EXP2: return rbtmath_compute_exp2(a, count, out, error);
    case F_EXP: return rbtmath_compute_exp(a, count, out, error);
    case F_LOG2: return rbtmath_compute_log2(a, count, out, error);
    case F_ATAN: return rbtmath_compute_atan(a, count, out, error);
    case F_ATAN2: return rbtmath_compute_atan2(a, b, count, out, error);
    case F_TAN: return rbtmath_compute_tan(a, count, out, error);
    default: return RBT_RESULT_INVALID_ARGUMENT;
    }
}

// f(a) (or atan2(a, b)) as bits; false if pending or refused.
static bool one(function f, double a, double b, uint64_t *out)
{
    double y = 0.0;
    RBTMATH_ERROR error;
    const RBT_RESULT r = compute(f, &a, &b, 1, &y, &error);
    if (!BUILT(r))
        return false;
    CHECK(r == RBT_RESULT_OK);
    *out = bits_of(y);
    return r == RBT_RESULT_OK;
}

static void expect(function f, double a, double b, uint64_t want, int line)
{
    uint64_t got = 0;
    if (!one(f, a, b, &got))
        return;
    if (got != want)
    {
        printf("FAIL %s (%s:%d): %s(%a%s%a) is %016llX, not %016llX\n", current_test, __FILE__, line, function_names[f], a,
               f == F_ATAN2 ? ", " : "", f == F_ATAN2 ? b : 0.0, (unsigned long long)got, (unsigned long long)want);
        failures++;
    }
}

#define EXPECT1(f, a, want)    expect((f), (a), 0.0, (want), __LINE__)
#define EXPECT2(y, x, want)    expect(F_ATAN2, (y), (x), (want), __LINE__)

// Within 1 of the correctly rounded encoding: the faithful result's.
static bool faithful(uint64_t got, uint64_t want)
{
    if (got == want)
        return true;
    if ((got >> 63) != (want >> 63))
    {
        // Either side of zero: only +0 against -0's neighbours; never here.
        return false;
    }
    const uint64_t d = got > want ? got - want : want - got;
    return d == 1;
}

// ---- The reference table ----------------------------------------------------------

typedef struct reference_range
{
    const char *function;
    const char *range;
} reference_range;

typedef struct reference_case
{
    uint64_t a;
    uint64_t b;
    uint64_t want;
} reference_case;

static const reference_range reference_ranges[] = {
#define RBTFN_REFERENCE_RANGES
#include "data/rbtmathfunctions/reference.inc"
#undef RBTFN_REFERENCE_RANGES
};

#define X(a, w)    {0x##a##ull, 0ull, 0x##w##ull}
#define Y(a, b, w) {0x##a##ull, 0x##b##ull, 0x##w##ull}
static const reference_case reference_cases[] = {
#include "data/rbtmathfunctions/reference.inc"
};
#undef X
#undef Y

enum
{
    REFERENCE_RANGES = sizeof(reference_ranges) / sizeof(reference_ranges[0]),
    REFERENCE_PER_RANGE = 4096
};

static function function_named(const char *name)
{
    for (int f = 0; f < F_COUNT; f++)
        if (strcmp(function_names[f], name) == 0)
            return (function)f;
    return F_COUNT;
}

// Checks one range's cases in one call; false if pending.
static bool check_range(int r, uint32_t first, uint32_t count)
{
    static double a[REFERENCE_PER_RANGE], b[REFERENCE_PER_RANGE], y[REFERENCE_PER_RANGE];
    const function f = function_named(reference_ranges[r].function);
    CHECK(f != F_COUNT);
    if (f == F_COUNT)
        return true;
    const reference_case *cases = reference_cases + (size_t)r * REFERENCE_PER_RANGE + first;
    for (uint32_t i = 0; i < count; i++)
    {
        a[i] = from_bits(cases[i].a);
        b[i] = from_bits(cases[i].b);
    }
    RBTMATH_ERROR error;
    const RBT_RESULT result = compute(f, a, b, count, y, &error);
    if (!BUILT(result))
        return false;
    CHECK(result == RBT_RESULT_OK);
    int wrong = 0;
    for (uint32_t i = 0; i < count; i++)
        if (!faithful(bits_of(y[i]), cases[i].want))
        {
            if (wrong < 5)
                printf("FAIL %s: %s %s: %s(%a%s%a) is %016llX, the correctly rounded %016llX\n", current_test, reference_ranges[r].function,
                       reference_ranges[r].range, reference_ranges[r].function, a[i], f == F_ATAN2 ? ", " : "", f == F_ATAN2 ? b[i] : 0.0,
                       (unsigned long long)bits_of(y[i]), (unsigned long long)cases[i].want);
            wrong++;
        }
    if (wrong)
    {
        printf("FAIL %s: %s %s: %d of %u beyond 1 ulp\n", current_test, reference_ranges[r].function, reference_ranges[r].range, wrong, count);
        failures++;
    }
    return true;
}

static int range_named(const char *function, const char *range)
{
    for (int r = 0; r < REFERENCE_RANGES; r++)
        if (strcmp(reference_ranges[r].function, function) == 0 && strcmp(reference_ranges[r].range, range) == 0)
            return r;
    return -1;
}

// ---- 1. Special values ---------------------------------------------------------------

static void test_special_values(void)
{
    // NaNs of both signs and payloads, signalling ones too: the one NaN out.
    const uint64_t nans[6] = {0x7FF8000000000000ull, 0xFFF8000000000000ull, 0x7FF8000000000123ull,
                              0xFFFFFFFFFFFFFFFFull, 0x7FF0000000000001ull, 0xFFF4000000000000ull};
    for (int i = 0; i < 6; i++)
    {
        const double n = from_bits(nans[i]);
        EXPECT1(F_EXP2, n, QNAN_BITS);
        EXPECT1(F_EXP, n, QNAN_BITS);
        EXPECT1(F_LOG2, n, QNAN_BITS);
        EXPECT1(F_ATAN, n, QNAN_BITS);
        EXPECT1(F_TAN, n, QNAN_BITS);
        EXPECT2(n, 1.0, QNAN_BITS);
        EXPECT2(1.0, n, QNAN_BITS);
        EXPECT2(n, n, QNAN_BITS);
        EXPECT2(n, 0.0, QNAN_BITS);
        EXPECT2(from_bits(POS_INF), n, QNAN_BITS);
        if (test_pending)
            return;
    }
    const double pinf = from_bits(POS_INF);
    const double ninf = from_bits(NEG_INF);
    // Zeros.
    EXPECT1(F_EXP2, 0.0, bits_of(1.0));
    EXPECT1(F_EXP2, -0.0, bits_of(1.0));
    EXPECT1(F_EXP, 0.0, bits_of(1.0));
    EXPECT1(F_EXP, -0.0, bits_of(1.0));
    EXPECT1(F_LOG2, 0.0, NEG_INF);
    EXPECT1(F_LOG2, -0.0, NEG_INF);
    EXPECT1(F_LOG2, 1.0, bits_of(0.0));
    EXPECT1(F_ATAN, 0.0, bits_of(0.0));
    EXPECT1(F_ATAN, -0.0, bits_of(-0.0));
    EXPECT1(F_TAN, 0.0, bits_of(0.0));
    EXPECT1(F_TAN, -0.0, bits_of(-0.0));
    // Tiny arguments come back as they went in.
    EXPECT1(F_ATAN, 0x1p-28, bits_of(0x1p-28));
    EXPECT1(F_TAN, -0x1p-30, bits_of(-0x1p-30));
    EXPECT1(F_ATAN, from_bits(1), 1);
    EXPECT1(F_TAN, -from_bits(1), bits_of(-from_bits(1)));
    // Infinities.
    EXPECT1(F_EXP2, pinf, POS_INF);
    EXPECT1(F_EXP2, ninf, bits_of(0.0));
    EXPECT1(F_EXP, pinf, POS_INF);
    EXPECT1(F_EXP, ninf, bits_of(0.0));
    EXPECT1(F_LOG2, pinf, POS_INF);
    EXPECT1(F_LOG2, ninf, QNAN_BITS);
    EXPECT1(F_LOG2, -1.0, QNAN_BITS);
    EXPECT1(F_LOG2, -from_bits(1), QNAN_BITS);
    EXPECT1(F_ATAN, pinf, 0x3FF921FB54442D18ull);
    EXPECT1(F_ATAN, ninf, 0xBFF921FB54442D18ull);
    EXPECT1(F_ATAN, 0x1p66, 0x3FF921FB54442D18ull);
    EXPECT1(F_TAN, pinf, QNAN_BITS);
    EXPECT1(F_TAN, ninf, QNAN_BITS);
    // atan2 by C's Annex F.
    EXPECT2(0.0, 0.0, bits_of(0.0));
    EXPECT2(-0.0, 0.0, bits_of(-0.0));
    EXPECT2(0.0, -0.0, 0x400921FB54442D18ull);
    EXPECT2(-0.0, -0.0, 0xC00921FB54442D18ull);
    EXPECT2(0.0, 3.0, bits_of(0.0));
    EXPECT2(-0.0, -3.0, 0xC00921FB54442D18ull);
    EXPECT2(2.0, 0.0, 0x3FF921FB54442D18ull);
    EXPECT2(2.0, -0.0, 0x3FF921FB54442D18ull);
    EXPECT2(-2.0, 0.0, 0xBFF921FB54442D18ull);
    EXPECT2(pinf, pinf, 0x3FE921FB54442D18ull);
    EXPECT2(pinf, ninf, 0x4002D97C7F3321D2ull);
    EXPECT2(ninf, ninf, 0xC002D97C7F3321D2ull);
    EXPECT2(ninf, pinf, 0xBFE921FB54442D18ull);
    EXPECT2(1.0, pinf, bits_of(0.0));
    EXPECT2(-1.0, pinf, bits_of(-0.0));
    EXPECT2(1.0, ninf, 0x400921FB54442D18ull);
    EXPECT2(-1.0, ninf, 0xC00921FB54442D18ull);
    EXPECT2(pinf, 1.0, 0x3FF921FB54442D18ull);
    EXPECT2(ninf, -1.0, 0xBFF921FB54442D18ull);
    // Overflow and underflow edges.
    EXPECT1(F_EXP2, 1024.0, POS_INF);
    EXPECT1(F_EXP2, 1023.0, bits_of(0x1p1023));
    EXPECT1(F_EXP2, -1022.0, bits_of(0x1p-1022));
    EXPECT1(F_EXP2, -1074.0, 1);
    EXPECT1(F_EXP2, -1075.0, bits_of(0.0));
    EXPECT1(F_EXP2, -1076.0, bits_of(0.0));
    EXPECT1(F_EXP2, -2000.0, bits_of(0.0));
    EXPECT1(F_EXP2, 2000.0, POS_INF);
    // exp's largest finite input and smallest non-zero result (MPFR's
    // correctly rounded values), and the doubles past them.
    EXPECT1(F_EXP, 0x1.62e42fefa39efp+9, 0x7FEFFFFFFFFFFF2Aull);
    EXPECT1(F_EXP, 0x1.62e42fefa39fp+9, POS_INF);
    EXPECT1(F_EXP, -0x1.74910d52d3051p+9, 1);
    EXPECT1(F_EXP, -0x1.74910d52d3052p+9, bits_of(0.0));
    EXPECT1(F_EXP, 1000.0, POS_INF);
    EXPECT1(F_EXP, -1000.0, bits_of(0.0));
    // tan at 2^20 (MPFR's correctly rounded) and the next double past it.
    EXPECT1(F_TAN, 0x1p20, 0x3FD6692E5779206Full);
    EXPECT1(F_TAN, -0x1p20, 0xBFD6692E5779206Full);
    EXPECT1(F_TAN, 0x1.0000000000001p20, QNAN_BITS);
    EXPECT1(F_TAN, -0x1.0000000000001p20, QNAN_BITS);
    // The tan limit's macro is the evaluation's.
    CHECK(RBTMATH_TAN_MAX_ARGUMENT == 0x1p20);
    // atan2's scaled carry (both about 2^960, both about 2^-1000, one
    // subnormal): the reference table's first 64 of each.
    const char *const scaled[3] = {"both about 2^960", "both about 2^-1000", "one subnormal"};
    for (int i = 0; i < 3; i++)
    {
        const int r = range_named("atan2", scaled[i]);
        CHECK(r >= 0);
        if (r >= 0 && !check_range(r, 0, 64))
            return;
    }
}

// ---- 2. Exact cases ----------------------------------------------------------------------

static void test_exact(void)
{
    enum { N = 1074 + 1024 };
    static double n[N], y[N], p[N];
    for (int i = 0; i < N; i++)
    {
        const int e = i - 1074;
        n[i] = (double)e;
        p[i] = e < -1022 ? from_bits(1ull << (e + 1074)) : from_bits((uint64_t)(e + 1023) << 52);
    }
    RBTMATH_ERROR error;
    RBT_RESULT r = rbtmath_compute_exp2(n, N, y, &error);
    if (!BUILT(r))
        return;
    CHECK(r == RBT_RESULT_OK);
    int wrong = 0;
    for (int i = 0; i < N; i++)
        wrong += bits_of(y[i]) != bits_of(p[i]);
    CHECK(wrong == 0);
    r = rbtmath_compute_log2(p, N, y, &error);
    if (!BUILT(r))
        return;
    CHECK(r == RBT_RESULT_OK);
    wrong = 0;
    for (int i = 0; i < N; i++)
        wrong += bits_of(y[i]) != bits_of(n[i]);
    CHECK(wrong == 0);
    EXPECT1(F_EXP, 0.0, bits_of(1.0));
    EXPECT1(F_EXP2, 0.0, bits_of(1.0));
    EXPECT1(F_LOG2, 1.0, bits_of(0.0));
}

// ---- 3. Odd symmetry ---------------------------------------------------------------------

static void test_odd(void)
{
    enum { N = 1 << 20 };
    double *x = (double *)malloc(N * sizeof(double));
    double *nx = (double *)malloc(N * sizeof(double));
    double *y = (double *)malloc(N * sizeof(double));
    double *ny = (double *)malloc(N * sizeof(double));
    CHECK(x && nx && y && ny);
    if (!(x && nx && y && ny))
        goto done;
    for (int f = 0; f < 2; f++)
    {
        for (int i = 0; i < N; i++)
        {
            double v;
            if (f == 0)
            {
                // atan: any finite double's magnitude.
                do
                    v = from_bits(next() & 0x7FFFFFFFFFFFFFFFull);
                while (!(v - v == 0.0));
            }
            else
            {
                // tan: half its encodings below 2^20, half in (-pi/2, pi/2)'s scale.
                if (i & 1)
                    v = (double)(next() >> 11) * 0x1p-53 * 0x1p20;
                else
                    v = from_bits(next() % 0x3FF921FB54442D18ull);
            }
            x[i] = v;
            nx[i] = -v;
        }
        RBTMATH_ERROR error;
        RBT_RESULT r1 = f == 0 ? rbtmath_compute_atan(x, N, y, &error) : rbtmath_compute_tan(x, N, y, &error);
        RBT_RESULT r2 = f == 0 ? rbtmath_compute_atan(nx, N, ny, &error) : rbtmath_compute_tan(nx, N, ny, &error);
        if (!BUILT(r1) || !BUILT(r2))
            goto done;
        CHECK(r1 == RBT_RESULT_OK && r2 == RBT_RESULT_OK);
        int wrong = 0;
        for (int i = 0; i < N; i++)
            wrong += bits_of(ny[i]) != (bits_of(y[i]) ^ 0x8000000000000000ull);
        if (wrong)
            printf("FAIL %s: %s not odd on %d of %d\n", current_test, f == 0 ? "atan" : "tan", wrong, N);
        CHECK(wrong == 0);
    }
done:
    free(x);
    free(nx);
    free(y);
    free(ny);
}

// ---- 4. Accuracy --------------------------------------------------------------------------

static void test_accuracy(void)
{
    CHECK(sizeof(reference_cases) / sizeof(reference_cases[0]) == (size_t)REFERENCE_RANGES * REFERENCE_PER_RANGE);
    for (int r = 0; r < REFERENCE_RANGES; r++)
        if (!check_range(r, 0, REFERENCE_PER_RANGE))
            return;
}

// ---- 5. Arrays -----------------------------------------------------------------------------

static void test_arrays(void)
{
    enum { N = 4096 };
    static double a[N], b[N], apart[N], in_place[N], sliced[N];
    for (int f = 0; f < F_COUNT; f++)
    {
        for (int i = 0; i < N; i++)
        {
            a[i] = (double)(int64_t)(next() % 2000001) / 1000.0 - 1000.0;
            b[i] = (double)(int64_t)(next() % 2000001) / 1000.0 - 1000.0;
            if (f == F_LOG2)
                a[i] = a[i] < 0.0 ? -a[i] : a[i];
        }
        RBTMATH_ERROR error;
        RBT_RESULT r = compute((function)f, a, b, N, apart, &error);
        if (!BUILT(r))
            return;
        CHECK(r == RBT_RESULT_OK);
        // In place, over the first input (and atan2's second).
        memcpy(in_place, a, sizeof(a));
        CHECK(compute((function)f, in_place, b, N, in_place, &error) == RBT_RESULT_OK);
        CHECK(memcmp(in_place, apart, sizeof(apart)) == 0);
        if (f == F_ATAN2)
        {
            memcpy(in_place, b, sizeof(b));
            CHECK(rbtmath_compute_atan2(a, in_place, N, in_place, &error) == RBT_RESULT_OK);
            CHECK(memcmp(in_place, apart, sizeof(apart)) == 0);
        }
        // Sixteen slices of uneven sizes.
        uint32_t at = 0;
        for (int s = 0; s < 16; s++)
        {
            const uint32_t size = s == 15 ? N - at : (uint32_t)(next() % (N / 16));
            CHECK(compute((function)f, a + at, b + at, size, sliced + at, &error) == RBT_RESULT_OK);
            at += size;
        }
        CHECK(memcmp(sliced, apart, sizeof(apart)) == 0);
        // A count of 0: OK with any pointers, nothing written, the message empty.
        memset(sliced, 0xA5, sizeof(sliced));
        strcpy(error.message, "stale");
        CHECK(compute((function)f, a, b, 0, sliced, &error) == RBT_RESULT_OK);
        CHECK(error.message[0] == '\0');
        CHECK(compute((function)f, NULL, NULL, 0, NULL, &error) == RBT_RESULT_OK);
        CHECK(compute((function)f, a, b, 0, a + 1, NULL) == RBT_RESULT_OK);
        int touched = 0;
        for (int i = 0; i < N; i++)
            touched += bits_of(sliced[i]) != 0xA5A5A5A5A5A5A5A5ull;
        CHECK(touched == 0);
        // Success empties the message.
        strcpy(error.message, "stale");
        CHECK(compute((function)f, a, b, 1, sliced, &error) == RBT_RESULT_OK);
        CHECK(error.message[0] == '\0');
    }
}

// ---- 6. Refusals ---------------------------------------------------------------------------

static void refused(RBT_RESULT r, const RBTMATH_ERROR *error, const char *message, const double *out, int line)
{
    if (r == RBT_RESULT_NOT_IMPLEMENTED)
    {
        printf("FAIL %s (%s:%d): not implemented\n", current_test, __FILE__, line);
        failures++;
        return;
    }
    if (r != RBT_RESULT_INVALID_ARGUMENT)
    {
        printf("FAIL %s (%s:%d): result %d, not RBT_RESULT_INVALID_ARGUMENT\n", current_test, __FILE__, line, (int)r);
        failures++;
    }
    if (strcmp(error->message, message) != 0)
    {
        printf("FAIL %s (%s:%d): message \"%s\", not \"%s\"\n", current_test, __FILE__, line, error->message, message);
        failures++;
    }
    if (out)
        for (int i = 0; i < 8; i++)
            if (bits_of(out[i]) != 0xA5A5A5A5A5A5A5A5ull)
            {
                printf("FAIL %s (%s:%d): the output written\n", current_test, __FILE__, line);
                failures++;
                return;
            }
}

#define REFUSED(call, message, out)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        memset(buffer, 0xA5, sizeof(buffer));                                                                          \
        memset(&error, 0, sizeof(error));                                                                              \
        refused((call), &error, (message), (out), __LINE__);                                                           \
    } while (0)

static void test_refusals(void)
{
    // buffer: [0..7] an input, [8..15] another, [16..31] room to overlap in.
    static double buffer[32];
    double in[8] = {0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0};
    double out[8];
    RBTMATH_ERROR error;
    typedef RBT_RESULT (*one_input)(const double *, uint32_t, double *, RBTMATH_ERROR *);
    const one_input calls[5] = {rbtmath_compute_tan, rbtmath_compute_atan, rbtmath_compute_exp2, rbtmath_compute_exp,
                                rbtmath_compute_log2};
    for (int c = 0; c < 5; c++)
    {
        REFUSED(calls[c](NULL, 8, buffer, &error), "x is NULL with a count of 8", buffer);
        REFUSED(calls[c](in, 8, NULL, &error), "out_y is NULL with a count of 8", NULL);
        REFUSED(calls[c](NULL, 8, NULL, &error), "x is NULL with a count of 8", NULL);
        // Overlapping x without being it: one element along, either way.
        REFUSED(calls[c](buffer + 16, 8, buffer + 17, &error), "out_y overlaps x without being it", NULL);
        CHECK(bits_of(buffer[16]) == 0xA5A5A5A5A5A5A5A5ull && bits_of(buffer[24]) == 0xA5A5A5A5A5A5A5A5ull);
        REFUSED(calls[c](buffer + 17, 8, buffer + 16, &error), "out_y overlaps x without being it", buffer + 16);
        // Touching ends don't overlap.
        memset(&error, 0, sizeof(error));
        for (int i = 0; i < 8; i++)
            buffer[i] = in[i];
        CHECK(calls[c](buffer, 8, buffer + 8, &error) == RBT_RESULT_OK);
        CHECK(calls[c](buffer + 8, 8, buffer, &error) == RBT_RESULT_OK);
        // With no error struct: still refused.
        CHECK(calls[c](NULL, 8, out, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    }
    REFUSED(rbtmath_compute_atan2(NULL, in, 8, buffer, &error), "y is NULL with a count of 8", buffer);
    REFUSED(rbtmath_compute_atan2(in, NULL, 8, buffer, &error), "x is NULL with a count of 8", buffer);
    REFUSED(rbtmath_compute_atan2(in, in, 8, NULL, &error), "out_angle is NULL with a count of 8", NULL);
    REFUSED(rbtmath_compute_atan2(NULL, NULL, 8, NULL, &error), "y is NULL with a count of 8", NULL);
    REFUSED(rbtmath_compute_atan2(buffer + 16, in, 8, buffer + 20, &error), "out_angle overlaps y without being it", NULL);
    REFUSED(rbtmath_compute_atan2(in, buffer + 16, 8, buffer + 12, &error), "out_angle overlaps x without being it", NULL);
    // Both overlapping: y's named first.
    REFUSED(rbtmath_compute_atan2(buffer + 16, buffer + 17, 8, buffer + 18, &error), "out_angle overlaps y without being it", NULL);
    // Being y, overlapping x: x's named.
    REFUSED(rbtmath_compute_atan2(buffer + 16, buffer + 17, 8, buffer + 16, &error), "out_angle overlaps x without being it", NULL);
    CHECK(rbtmath_compute_atan2(in, in, 8, out, NULL) == RBT_RESULT_OK);
}

// ---- 7. Two calls at once -------------------------------------------------------------------

typedef struct two_calls
{
    const double *x;
    double *out[2];
    RBT_RESULT result[2];
} two_calls;

enum { TWO_N = 1 << 18 };

static void two_calls_task(uint32_t first, uint32_t end, uint32_t worker, void *data)
{
    (void)worker;
    two_calls *t = (two_calls *)data;
    for (uint32_t i = first; i < end; i++)
        t->result[i] = rbtmath_compute_tan(t->x, TWO_N, t->out[i], NULL);
}

static void test_two_threads(void)
{
    static double x[TWO_N], want[TWO_N], o0[TWO_N], o1[TWO_N];
    for (int i = 0; i < TWO_N; i++)
        x[i] = (double)(next() >> 11) * 0x1p-53 * 2000.0 - 1000.0;
    const RBT_RESULT r = rbtmath_compute_tan(x, TWO_N, want, NULL);
    if (!BUILT(r))
        return;
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
    two_calls t = {x, {o0, o1}, {RBT_RESULT_NOT_IMPLEMENTED, RBT_RESULT_NOT_IMPLEMENTED}};
    void *task = sched.enqueue(two_calls_task, 2, 1, &t, sched.user);
    sched.wait(task, sched.user);
    CHECK(t.result[0] == RBT_RESULT_OK);
    CHECK(t.result[1] == RBT_RESULT_OK);
    CHECK(memcmp(o0, want, sizeof(want)) == 0);
    CHECK(memcmp(o1, want, sizeof(want)) == 0);
    rbtjobs_pool_destroy(pool);
}

// ---- Main ------------------------------------------------------------------------------------

#define RUN(test)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        current_test = #test;                                                                                          \
        test_pending = false;                                                                                          \
        test();                                                                                                        \
        if (test_pending)                                                                                              \
            pending++;                                                                                                 \
    } while (0)

int main(void)
{
    RUN(test_special_values);
    RUN(test_exact);
    RUN(test_odd);
    RUN(test_accuracy);
    RUN(test_arrays);
    RUN(test_refusals);
    RUN(test_two_threads);
    if (failures)
    {
        printf("rbtmathfunctions_tests: %d failures, %d tests pending\n", failures, pending);
        return 1;
    }
    printf("rbtmathfunctions_tests: all passed, %d tests pending\n", pending);
    return 0;
}
