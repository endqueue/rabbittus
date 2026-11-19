// The accuracy test's reference table (include/math/rbtmathfunctions.h; card
// A61, the approved draft's section 10, test 4): 4,096 inputs for each
// function and range, each with its correctly rounded result, worked out by
// MPFR at 300 bits and written to tests/data/rbtmathfunctions/reference.inc.
//
// Run by hand, not built: it needs MPFR, which isn't a dependency of
// Rabbittus and won't become one for a program run once. Its inputs come
// from a fixed integer generator, so a run gives the same file every time.
// On macOS with Homebrew's MPFR (brew install mpfr), from rabbittus/:
//
//   cc -O2 -std=c11 -I/opt/homebrew/include tests/rbtmathfunctions_reference.c \
//       -L/opt/homebrew/lib -lmpfr -lgmp -o /tmp/rbtmathfunctions_reference
//   /tmp/rbtmathfunctions_reference > tests/data/rbtmathfunctions/reference.inc
//
// On Linux the same without the Homebrew paths (Fedora: mpfr-devel).
//
// The file holds the ranges' names (under RBTFN_REFERENCE_RANGES) and then
// the cases, range by range, 4,096 each in the ranges' order, each as its
// bits in hex: X(x, result) for a function of one argument, Y(y, x, result)
// for atan2. The test defines X and Y.

#include <gmp.h>
#include <mpfr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { CASES = 4096, PRECISION = 300 };

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

// splitmix64: the inputs, the same on every machine.
static uint64_t state = 0xA61A61A61A61A61Aull;
static uint64_t next(void)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform in [lo, hi].
static double uniform(double lo, double hi)
{
    const double u = (double)(next() >> 11) * 0x1p-53;
    return lo + (hi - lo) * u;
}

// Any finite double, uniform over the encodings (sign included unless
// positive_only).
static double any_double(int positive_only)
{
    for (;;)
    {
        uint64_t u = next();
        if (positive_only)
            u &= 0x7FFFFFFFFFFFFFFFull;
        const double v = from_bits(u);
        if (v == v && v - v == 0.0)
            return v;
    }
}

// A double n ulps from v (n may be negative), staying on v's side of 0.
static double ulps_from(double v, int64_t n)
{
    return from_bits((uint64_t)((int64_t)bits_of(v) + n));
}

static double random_sign(double v)
{
    return (next() & 1) ? -v : v;
}

typedef enum { F_EXP2, F_EXP, F_LOG2, F_ATAN, F_ATAN2, F_TAN } function;
static const char *const function_names[] = {"exp2", "exp", "log2", "atan", "atan2", "tan"};

typedef struct range
{
    function f;
    const char *name;
} range;

static const range ranges[] = {
    {F_EXP2, "[-1, 1]"},
    {F_EXP2, "[-1022, 1024] (normal results)"},
    {F_EXP2, "[-1075, -1022] (subnormal)"},
    {F_EXP, "[-1, 1]"},
    {F_EXP, "[-708, 709.78] (normal)"},
    {F_EXP, "[-745.1, -708.4] (subnormal)"},
    {F_LOG2, "any positive double"},
    {F_LOG2, "[0.5, 2]"},
    {F_LOG2, "subnormal inputs"},
    {F_ATAN, "[-4, 4]"},
    {F_ATAN, "any double"},
    {F_ATAN, "within 64 ulps of its interval edges"},
    {F_ATAN2, "[-10, 10] x [-10, 10]"},
    {F_ATAN2, "any doubles"},
    {F_ATAN2, "both about 2^960"},
    {F_ATAN2, "both about 2^-1000"},
    {F_ATAN2, "one subnormal"},
    {F_TAN, "(-pi/2, pi/2)"},
    {F_TAN, "[-2^20, 2^20]"},
    {F_TAN, "within 512 ulps of k pi/2"},
    {F_TAN, "within 64 ulps of its switch at 0.671875 from k pi/2"},
};
enum { RANGES = sizeof(ranges) / sizeof(ranges[0]) };

// The double nearest k pi/2 + offset.
static double near_k_pio2(int64_t k, double offset)
{
    mpfr_t v, o;
    mpfr_inits2(PRECISION, v, o, (mpfr_ptr)0);
    mpfr_const_pi(v, MPFR_RNDN);
    mpfr_mul_si(v, v, (long)k, MPFR_RNDN);
    mpfr_div_2ui(v, v, 1, MPFR_RNDN);
    mpfr_set_d(o, offset, MPFR_RNDN);
    mpfr_add(v, v, o, MPFR_RNDN);
    const double d = mpfr_get_d(v, MPFR_RNDN);
    mpfr_clears(v, o, (mpfr_ptr)0);
    return d;
}

static void make_input(int r, double *a, double *b)
{
    static const double atan_edges[6] = {0x1.cp-2, 0x1.6p-1, 0x1.3p+0, 0x1.38p+1, 0x1p-27, 0x1p66};
    *b = 0.0;
    switch (r)
    {
    case 0: *a = uniform(-1.0, 1.0); break;
    case 1: *a = uniform(-1022.0, 1024.0); break;
    case 2: *a = uniform(-1075.0, -1022.0); break;
    case 3: *a = uniform(-1.0, 1.0); break;
    case 4: *a = uniform(-708.0, 709.78); break;
    case 5: *a = uniform(-745.1, -708.4); break;
    case 6: *a = any_double(1); break;
    case 7: *a = uniform(0.5, 2.0); break;
    case 8: *a = from_bits(1 + (next() % ((1ull << 52) - 1))); break;
    case 9: *a = uniform(-4.0, 4.0); break;
    case 10: *a = any_double(0); break;
    case 11: *a = random_sign(ulps_from(atan_edges[next() % 6], (int64_t)(next() % 129) - 64)); break;
    case 12:
        *a = uniform(-10.0, 10.0);
        *b = uniform(-10.0, 10.0);
        break;
    case 13:
        *a = any_double(0);
        *b = any_double(0);
        break;
    case 14:
        *a = random_sign(uniform(0x1p959, 0x1p961));
        *b = random_sign(uniform(0x1p959, 0x1p961));
        break;
    case 15:
        *a = random_sign(uniform(0x1p-1001, 0x1p-999));
        *b = random_sign(uniform(0x1p-1001, 0x1p-999));
        break;
    case 16:
    {
        const double sub   = random_sign(from_bits(1 + (next() % ((1ull << 52) - 1))));
        const double other = random_sign(uniform(0x1p-1040, 0x1p-1000));
        if (next() & 1)
        {
            *a = sub;
            *b = other;
        }
        else
        {
            *a = other;
            *b = sub;
        }
        break;
    }
    case 17: *a = uniform(-0x1.921fb54442d18p+0, 0x1.921fb54442d18p+0); break;
    case 18: *a = uniform(-0x1p20, 0x1p20); break;
    case 19:
    {
        // k in [-667544, 667544]: |k pi/2| below 2^20.
        const int64_t k = (int64_t)(next() % 1335089) - 667544;
        if (k == 0)
        {
            *a = ulps_from(0x1p-20, (int64_t)(next() % 513));
            break;
        }
        *a = ulps_from(near_k_pio2(k, 0.0), (int64_t)(next() % 1025) - 512);
        break;
    }
    case 20:
    {
        const int64_t k = (int64_t)(next() % 2001) - 1000;
        const double s  = (next() & 1) ? 0.671875 : -0.671875;
        *a              = ulps_from(near_k_pio2(k, s), (int64_t)(next() % 129) - 64);
        break;
    }
    }
}

static double reference(function f, double a, double b)
{
    mpfr_t x, y, out;
    mpfr_inits2(PRECISION, x, y, out, (mpfr_ptr)0);
    mpfr_set_d(x, a, MPFR_RNDN);
    mpfr_set_d(y, b, MPFR_RNDN);
    switch (f)
    {
    case F_EXP2: mpfr_exp2(out, x, MPFR_RNDN); break;
    case F_EXP: mpfr_exp(out, x, MPFR_RNDN); break;
    case F_LOG2: mpfr_log2(out, x, MPFR_RNDN); break;
    case F_ATAN: mpfr_atan(out, x, MPFR_RNDN); break;
    case F_ATAN2: mpfr_atan2(out, x, y, MPFR_RNDN); break;
    case F_TAN: mpfr_tan(out, x, MPFR_RNDN); break;
    }
    const double d = mpfr_get_d(out, MPFR_RNDN);
    mpfr_clears(x, y, out, (mpfr_ptr)0);
    return d;
}

int main(void)
{
    printf("// The correctly rounded results the accuracy test checks against\n");
    printf("// (tests/rbtmathfunctions_tests.c, test 4): %d inputs a function and range,\n", CASES);
    printf("// worked out by MPFR %s at %d bits. Written by\n", mpfr_get_version(), PRECISION);
    printf("// tests/rbtmathfunctions_reference.c, run by hand; never edited.\n");
    printf("#ifdef RBTFN_REFERENCE_RANGES\n");
    for (int r = 0; r < RANGES; r++)
        printf("    {\"%s\", \"%s\"},\n", function_names[ranges[r].f], ranges[r].name);
    printf("#else\n");
    for (int r = 0; r < RANGES; r++)
        for (int i = 0; i < CASES; i++)
        {
            double a, b;
            make_input(r, &a, &b);
            const double want = reference(ranges[r].f, a, b);
            if (ranges[r].f == F_ATAN2)
                printf("Y(%016llX,%016llX,%016llX),\n", (unsigned long long)bits_of(a), (unsigned long long)bits_of(b),
                       (unsigned long long)bits_of(want));
            else
                printf("X(%016llX,%016llX),\n", (unsigned long long)bits_of(a), (unsigned long long)bits_of(want));
        }
    printf("#endif\n");
    return 0;
}
