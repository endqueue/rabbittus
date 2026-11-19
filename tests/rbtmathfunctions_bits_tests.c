// The same bits on every platform (include/math/rbtmathfunctions.h, "Same
// bits"; card A61, the approved draft's section 10): 2^20 inputs for each
// function and range, made from a fixed integer generator as bits (never by
// arithmetic, so this test's own build can't change them), the outputs
// hashed (FNV-1a, 64 bits) against values recorded on the first machine to
// run it (tests/data/rbtmathfunctions/bits.inc). The inputs reach each
// path of the Definition: every interval edge, tan's switch at 0.671875 and
// its odd and even k, atan2's four quadrants and its scaled carry,
// subnormals, and every encoding (NaNs and infinities among them).
//
// These bits are worlds: once released they freeze. If a change is meant to
// change one, run with --print, check the change is the intended one, and
// record the new values with what changed and why; a released function's
// change is refused at review (a new function instead).
//
// Also run with subnormals flushed (arm64's FPCR.FZ, x86's FTZ and DAZ),
// where the platform's headers offer a control, checking the header's
// stated differences and nothing else: exp2, exp, log2 and atan2 give what
// the default environment gives with each subnormal input made a zero of
// its sign, a subnormal result then a zero of its sign; tan and atan
// unchanged.
//
// Written in C, as the other tests of a C header are; built with contraction
// off, as the library is.

#include "math/rbtmathfunctions.h"
#include <fenv.h>
#include <float.h>
#if !defined(FE_DFL_DISABLE_DENORMS_ENV) && !defined(_MCW_DN) && defined(__has_include)
#if __has_include(<xmmintrin.h>) && __has_include(<pmmintrin.h>)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif
#endif
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
#include "data/rbtmathfunctions/bits.inc"
};

static void check(const char *what, uint64_t got)
{
    if (print_only)
    {
        printf("    {\"%s\", 0x%016llXull},\n", what, (unsigned long long)got);
        return;
    }
    for (size_t i = 0; i < sizeof(recorded) / sizeof(recorded[0]); i++)
        if (recorded[i].what && strcmp(recorded[i].what, what) == 0)
        {
            if (got != recorded[i].hash)
            {
                printf("FAIL %s: 0x%016llX, recorded 0x%016llX\n", what, (unsigned long long)got,
                       (unsigned long long)recorded[i].hash);
                failures++;
            }
            return;
        }
    printf("FAIL %s: nothing recorded\n", what);
    failures++;
}

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

// splitmix64: integers only.
static uint64_t state;
static uint64_t next(void)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// A double of either sign whose biased exponent is in [lo, hi] and whose
// significand is random: made from bits alone.
static uint64_t exponent_range(uint64_t lo, uint64_t hi, bool positive)
{
    const uint64_t e = lo + next() % (hi - lo + 1);
    const uint64_t s = positive ? 0 : (next() & 1) << 63;
    return s | (e << 52) | (next() & 0x000FFFFFFFFFFFFFull);
}

// Within n encodings of v's, either sign.
static uint64_t near(uint64_t v, uint64_t n, bool positive)
{
    const uint64_t off = next() % (2 * n + 1);
    const uint64_t u   = v + off - n;
    return positive ? u : u | ((next() & 1) << 63);
}

typedef enum { F_EXP2, F_EXP, F_LOG2, F_ATAN, F_ATAN2, F_TAN, F_COUNT } function;

typedef enum
{
    I_ANY,            // every encoding
    I_UNIT,           // |x| below 2: both signs
    I_WIDE,           // |x| below 2^11
    I_EXP_WIDE,       // |x| below 2^10
    I_SUBNORMAL,      // subnormal, both signs
    I_POSITIVE,       // any positive finite
    I_NEAR_ONE,       // [0.5, 2)
    I_ATAN_MID,       // |x| in [2^-30, 2^3)
    I_ATAN_EDGES,     // within 2^12 encodings of each interval edge
    I_TAN_SMALL,      // |x| below 2 (k 0 and +-1, the switch)
    I_TAN_MID,        // |x| in [1, 32): odd and even k, the switch
    I_TAN_SWITCH,     // within 2^24 encodings of 0.671875
    I_TAN_WIDE,       // |x| below 2^21: past the limit too
    I_TAN_LIMIT,      // within 2^10 encodings of 2^20
    I_PAIR_NEAR,      // atan2: both |v| in [2^-5, 2^6), four quadrants
    I_PAIR_HUGE,      // atan2: both about 2^960 (the scaled carry)
    I_PAIR_TINY,      // atan2: both about 2^-1000 (the scaled carry)
    I_PAIR_SUBNORMAL, // atan2: one subnormal
    I_PAIR_RATIO,     // atan2: ratios about 2^-27 and 2^66
} inputs;

static uint64_t make_one(inputs i)
{
    static const uint64_t atan_edges[6] = {0x3FDC000000000000ull, 0x3FE6000000000000ull, 0x3FF3000000000000ull,
                                           0x4003800000000000ull, 0x3E40000000000000ull, 0x4410000000000000ull};
    switch (i)
    {
    case I_ANY: return next();
    case I_UNIT: return exponent_range(1023 - 60, 1023, false);
    case I_WIDE: return exponent_range(1023 - 60, 1023 + 10, false);
    case I_EXP_WIDE: return exponent_range(1023 - 60, 1023 + 9, false);
    case I_SUBNORMAL: return (next() & 0x800FFFFFFFFFFFFFull);
    case I_POSITIVE: return next() % 0x7FF0000000000000ull;
    case I_NEAR_ONE: return exponent_range(1022, 1023, true);
    case I_ATAN_MID: return exponent_range(1023 - 30, 1023 + 2, false);
    case I_ATAN_EDGES: return near(atan_edges[next() % 6], 4096, false);
    case I_TAN_SMALL: return exponent_range(1023 - 30, 1023, false);
    case I_TAN_MID: return exponent_range(1023, 1023 + 4, false);
    case I_TAN_SWITCH: return near(0x3FE5800000000000ull, 1ull << 24, false);
    case I_TAN_WIDE: return exponent_range(1023 - 10, 1023 + 20, false);
    case I_TAN_LIMIT: return near(0x4130000000000000ull, 1024, false);
    default: return 0;
    }
}

static void make_pair(inputs i, uint64_t *y, uint64_t *x)
{
    switch (i)
    {
    case I_ANY:
        *y = next();
        *x = next();
        return;
    case I_PAIR_NEAR:
        *y = exponent_range(1023 - 5, 1023 + 5, false);
        *x = exponent_range(1023 - 5, 1023 + 5, false);
        return;
    case I_PAIR_HUGE:
        *y = exponent_range(1023 + 958, 1023 + 962, false);
        *x = exponent_range(1023 + 958, 1023 + 962, false);
        return;
    case I_PAIR_TINY:
        *y = exponent_range(1023 - 1002, 1023 - 998, false);
        *x = exponent_range(1023 - 1002, 1023 - 998, false);
        return;
    case I_PAIR_SUBNORMAL:
        if (next() & 1)
        {
            *y = next() & 0x800FFFFFFFFFFFFFull;
            *x = exponent_range(1, 1023 - 990, false);
        }
        else
        {
            *y = exponent_range(1, 1023 - 990, false);
            *x = next() & 0x800FFFFFFFFFFFFFull;
        }
        return;
    case I_PAIR_RATIO:
    {
        // y's exponent 27 or 66 (+-2) from x's: the two cut-offs.
        const uint64_t ex = 1023 - 400 + next() % 800;
        const uint64_t d  = (next() & 1) ? 66 : 27;
        const uint64_t ey = (next() & 1) ? ex + d - 2 + next() % 5 : ex - d - 2 + next() % 5;
        *x = exponent_range(ex, ex, false);
        *y = exponent_range(ey, ey, false);
        return;
    }
    default:
        *y = 0;
        *x = 0;
        return;
    }
}

typedef struct record
{
    function f;
    inputs in;
    const char *what;
} record;

static const record records[] = {
    {F_EXP2, I_ANY, "exp2 any encoding"},
    {F_EXP2, I_UNIT, "exp2 |x| < 2"},
    {F_EXP2, I_WIDE, "exp2 |x| < 2^11"},
    {F_EXP2, I_SUBNORMAL, "exp2 subnormal x"},
    {F_EXP, I_ANY, "exp any encoding"},
    {F_EXP, I_UNIT, "exp |x| < 2"},
    {F_EXP, I_EXP_WIDE, "exp |x| < 2^10"},
    {F_EXP, I_SUBNORMAL, "exp subnormal x"},
    {F_LOG2, I_ANY, "log2 any encoding"},
    {F_LOG2, I_POSITIVE, "log2 any positive"},
    {F_LOG2, I_NEAR_ONE, "log2 [0.5, 2)"},
    {F_LOG2, I_SUBNORMAL, "log2 subnormal x"},
    {F_ATAN, I_ANY, "atan any encoding"},
    {F_ATAN, I_ATAN_MID, "atan |x| in [2^-30, 8)"},
    {F_ATAN, I_ATAN_EDGES, "atan interval edges"},
    {F_ATAN, I_SUBNORMAL, "atan subnormal x"},
    {F_ATAN2, I_ANY, "atan2 any encodings"},
    {F_ATAN2, I_PAIR_NEAR, "atan2 four quadrants"},
    {F_ATAN2, I_PAIR_HUGE, "atan2 both about 2^960"},
    {F_ATAN2, I_PAIR_TINY, "atan2 both about 2^-1000"},
    {F_ATAN2, I_PAIR_SUBNORMAL, "atan2 one subnormal"},
    {F_ATAN2, I_PAIR_RATIO, "atan2 ratios about 2^-27 and 2^66"},
    {F_TAN, I_ANY, "tan any encoding"},
    {F_TAN, I_TAN_SMALL, "tan |x| < 2"},
    {F_TAN, I_TAN_MID, "tan |x| in [1, 32)"},
    {F_TAN, I_TAN_SWITCH, "tan about its switch"},
    {F_TAN, I_TAN_WIDE, "tan |x| < 2^21"},
    {F_TAN, I_TAN_LIMIT, "tan about 2^20"},
    {F_TAN, I_SUBNORMAL, "tan subnormal x"},
};
enum { RECORDS = sizeof(records) / sizeof(records[0]), N = 1 << 20 };

static double *in_a, *in_b, *out_default, *out_flushed, *zero_a, *zero_b, *out_zeroed;

static RBT_RESULT compute(function f, const double *a, const double *b, double *out)
{
    RBTMATH_ERROR error;
    switch (f)
    {
    case F_EXP2: return rbtmath_compute_exp2(a, N, out, &error);
    case F_EXP: return rbtmath_compute_exp(a, N, out, &error);
    case F_LOG2: return rbtmath_compute_log2(a, N, out, &error);
    case F_ATAN: return rbtmath_compute_atan(a, N, out, &error);
    case F_ATAN2: return rbtmath_compute_atan2(a, b, N, out, &error);
    case F_TAN: return rbtmath_compute_tan(a, N, out, &error);
    default: return RBT_RESULT_INVALID_ARGUMENT;
    }
}

static void make_inputs(int r)
{
    state = 0xB175B175B175B175ull ^ ((uint64_t)r * 0x9E3779B97F4A7C15ull);
    for (int i = 0; i < N; i++)
    {
        if (records[r].f == F_ATAN2)
        {
            uint64_t y, x;
            make_pair(records[r].in, &y, &x);
            in_a[i] = from_bits(y);
            in_b[i] = from_bits(x);
        }
        else
        {
            in_a[i] = from_bits(make_one(records[r].in));
            in_b[i] = 0.0;
        }
    }
}

// ---- Flushing subnormals ----------------------------------------------------------------
//
// Through whichever control this platform's own headers offer: fenv.h's
// (FE_DFL_DISABLE_DENORMS_ENV: arm64's FPCR.FZ), MSVC's float.h (_MCW_DN),
// SSE's xmmintrin.h and pmmintrin.h (_MM_FLUSH_ZERO_ON, _MM_DENORMALS_ZERO_ON:
// x86's FTZ and DAZ). None: skipped, and said so.

#if defined(FE_DFL_DISABLE_DENORMS_ENV)
#define FLUSH_CONTROL "FE_DFL_DISABLE_DENORMS_ENV"
static fenv_t flush_saved;
static void flush_on(void) { fegetenv(&flush_saved); fesetenv(FE_DFL_DISABLE_DENORMS_ENV); }
static void flush_off(void) { fesetenv(&flush_saved); }
#elif defined(_MCW_DN)
#define FLUSH_CONTROL "_MCW_DN"
static unsigned int flush_saved;
static void flush_on(void) { unsigned int now; _controlfp_s(&flush_saved, 0, 0); _controlfp_s(&now, _DN_FLUSH, _MCW_DN); }
static void flush_off(void) { unsigned int now; _controlfp_s(&now, flush_saved & _MCW_DN, _MCW_DN); }
#elif defined(_MM_FLUSH_ZERO_ON) && defined(_MM_DENORMALS_ZERO_ON)
#define FLUSH_CONTROL "_MM_FLUSH_ZERO_ON and _MM_DENORMALS_ZERO_ON"
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
#endif

static bool is_subnormal(uint64_t u)
{
    return (u & 0x7FF0000000000000ull) == 0 && (u & 0x000FFFFFFFFFFFFFull) != 0;
}

// A subnormal made a zero of its sign; anything else as it is.
static uint64_t flushed_value(uint64_t u)
{
    return is_subnormal(u) ? u & 0x8000000000000000ull : u;
}

// What flushing may change, per the header: true if the flushed result is
// allowed against the default one (want) and the default one on the inputs
// with their subnormals made zeros (zeroed).
static bool flushed_allowed(function f, uint64_t want, uint64_t zeroed, uint64_t got)
{
    if (got == want)
        return true;
    if (f == F_TAN || f == F_ATAN)
        return false;
    return got == flushed_value(want) || got == flushed_value(zeroed);
}

static void test_flushed(int r)
{
#if defined(FLUSH_CONTROL)
    for (int i = 0; i < N; i++)
    {
        zero_a[i] = from_bits(flushed_value(bits_of(in_a[i])));
        zero_b[i] = from_bits(flushed_value(bits_of(in_b[i])));
    }
    const RBT_RESULT zeroed = compute(records[r].f, zero_a, zero_b, out_zeroed);
    flush_on();
    const RBT_RESULT result = compute(records[r].f, in_a, in_b, out_flushed);
    flush_off();
    if (result != RBT_RESULT_OK || zeroed != RBT_RESULT_OK)
    {
        printf("FAIL %s with subnormals flushed: refused\n", records[r].what);
        failures++;
        return;
    }
    int bad = 0, changed = 0;
    for (int i = 0; i < N; i++)
    {
        const uint64_t want = bits_of(out_default[i]), got = bits_of(out_flushed[i]);
        changed += want != got;
        if (!flushed_allowed(records[r].f, want, bits_of(out_zeroed[i]), got))
        {
            if (bad < 3)
                printf("FAIL %s with subnormals flushed: (%a, %a) gave %016llX, %016llX by default\n", records[r].what, in_a[i], in_b[i],
                       (unsigned long long)got, (unsigned long long)want);
            bad++;
        }
    }
    if (bad)
        failures++;
    else if (changed)
        printf("rbtmathfunctions_bits_tests: %s with subnormals flushed: %d of %d changed, each as the header says\n", records[r].what,
               changed, N);
#else
    (void)r;
#endif
}

int main(int argc, char **argv)
{
    print_only = argc > 1 && strcmp(argv[1], "--print") == 0;
    in_a = (double *)malloc(N * sizeof(double));
    in_b = (double *)malloc(N * sizeof(double));
    out_default = (double *)malloc(N * sizeof(double));
    out_flushed = (double *)malloc(N * sizeof(double));
    zero_a = (double *)malloc(N * sizeof(double));
    zero_b = (double *)malloc(N * sizeof(double));
    out_zeroed = (double *)malloc(N * sizeof(double));
    if (!in_a || !in_b || !out_default || !out_flushed || !zero_a || !zero_b || !out_zeroed)
    {
        printf("rbtmathfunctions_bits_tests: out of memory\n");
        return 1;
    }
    for (int r = 0; r < RECORDS; r++)
    {
        make_inputs(r);
        const RBT_RESULT result = compute(records[r].f, in_a, in_b, out_default);
        if (result != RBT_RESULT_OK)
        {
            printf("FAIL %s: refused (%d)\n", records[r].what, (int)result);
            failures++;
            continue;
        }
        check(records[r].what, hash_bytes(FNV_START, out_default, N * sizeof(double)));
        if (!print_only)
            test_flushed(r);
    }
#if !defined(FLUSH_CONTROL)
    if (!print_only)
        printf("rbtmathfunctions_bits_tests: no control over flushing subnormals in this platform's headers; skipped\n");
#endif
    free(in_a);
    free(in_b);
    free(out_default);
    free(out_flushed);
    free(zero_a);
    free(zero_b);
    free(out_zeroed);
    if (print_only)
        return 0;
    if (failures)
    {
        printf("rbtmathfunctions_bits_tests: %d failures\n", failures);
        return 1;
    }
    printf("rbtmathfunctions_bits_tests: %d records the same bits as recorded\n", RECORDS);
    return 0;
}
