// The same bits on every platform (include/math/rbtmathnoise.h, "Same
// bits"; card A56, the approved draft's section 10): a fixed run of every
// kind, fractal, dimension, distance and return, values, gradients, warps
// and nearest cells, at places near the origin, negative, on cell borders
// and at 1e7, each hashed, against values recorded on the first machine to
// run it (tests/data/rbtmathnoise/bits.inc). Apart from them, the parts of
// the Definition a copy must match exactly: H, the Pa terms, the octave
// table (seeds and offsets), the tables and the constants, with X_i, Y_a
// and FLIP checked pairwise distinct as step 2 says.
//
// These bits are worlds: once released they freeze. If a change is meant to
// change one, run with --print, check the change is the intended one, and
// record the new values with what changed and why; a released kind's change
// is refused at review (a new kind instead).
//
// Also run with subnormals flushed (arm64's FPCR.FZ, x86's FTZ and DAZ),
// where the platform's headers offer a control: every value and gradient
// within 2^-100 of the default environment's.
//
// Written in C, as the other tests of a C header are; built with contraction
// off, as the library is.

#include "rbtmathnoise_reference.h"
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

#define CHECK_OK(call)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((call) != RBT_RESULT_OK)                                                                                   \
        {                                                                                                              \
            printf("FAIL %s:%d: %s refused: %s\n", __FILE__, __LINE__, #call, error.message);                          \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

#define CHECK_NEAR(got, want, tolerance)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (fabs((double)(got) - (double)(want)) > (tolerance))                                                        \
        {                                                                                                              \
            printf("FAIL %s:%d: %s is %.17g, not %.17g\n", __FILE__, __LINE__, #got, (double)(got), (double)(want));    \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

typedef struct recorded_value
{
    const char *what;
    uint64_t hash;
} recorded_value;

static const recorded_value recorded[] = {
#include "data/rbtmathnoise/bits.inc"
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

// Repeatable places, the same on every platform: integers over 4096, and a
// few chosen ones (cell borders, negative, 1e7 out).
static uint64_t state = 0x9E3779B97F4A7C15ull;
static double next_place(void)
{
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return (double)(int64_t)((state >> 33) % 2000001u) / 4096.0 - 244.140625;
}

enum { N = 512 };
static double places[N * 3];

static void make_places(void)
{
    for (int i = 0; i < N * 3; i++)
        places[i] = next_place();
    // Chosen ones: the origin, cell borders, -1 and 2^32 cells, 1e7 out.
    const double chosen[8] = {0.0, -1.0, 1.0, -0.5, 4294967296.0, -4294967297.0, 1e7, -1e7 + 0.25};
    for (int i = 0; i < 8; i++)
        for (int a = 0; a < 3; a++)
            places[(N - 8 + i) * 3 + a] = chosen[i] + (double)a * 0.125;
}

static RBTMATH_NOISE_DESC make_desc(uint32_t dims, int kind, int fractal, uint32_t octaves)
{
    RBTMATH_NOISE_DESC d;
    memset(&d, 0, sizeof(d));
    d.base.type = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
    d.dimensions = dims;
    d.kind = (RBTMATH_NOISE_KIND)kind;
    d.fractal = (RBTMATH_NOISE_FRACTAL)fractal;
    d.seed = 0xC0FFEEu;
    d.octaves = fractal == RBTMATH_NOISE_FRACTAL_SINGLE ? 1u : octaves;
    d.frequency = 0.15625;
    if (fractal != RBTMATH_NOISE_FRACTAL_SINGLE)
    {
        d.lacunarity = 2.0;
        d.gain = 0.5f;
    }
    if (fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
        d.ridge_feedback = 2.0f;
    if (fractal == RBTMATH_NOISE_FRACTAL_ERODED)
        d.erosion = 1.0f;
    if (kind == RBTMATH_NOISE_KIND_CELLULAR)
    {
        d.cell_distance = RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN;
        d.cell_return = RBTMATH_NOISE_CELL_RETURN_F1;
        d.cell_jitter = 0.875f;
    }
    if (kind == RBTMATH_NOISE_KIND_SIMPLEX && dims == 3)
        d.orientation = RBTMATH_NOISE_ORIENTATION_EVEN;
    return d;
}

static float vals[N], grads[N * 3];

// One run's results, hashed; the far places' frequency kept below the limit.
static uint64_t run(const RBTMATH_NOISE_DESC *d, bool with_gradients)
{
    RBTMATH_ERROR error;
    RBT_RESULT r = rbtmath_noise_compute_points(d, places, N, vals, with_gradients ? grads : NULL, &error);
    if (r != RBT_RESULT_OK)
    {
        printf("FAIL a run refused: %s\n", error.message);
        failures++;
        return 0;
    }
    uint64_t h = hash_bytes(FNV_START, vals, sizeof(vals));
    if (with_gradients)
        h = hash_bytes(h, grads, (size_t)N * d->dimensions * sizeof(float));
    return h;
}

static void test_runs(void)
{
    static const char *kinds[] = {"", "gradient", "simplex", "cellular"};
    static const char *fractals[] = {"", "single", "fbm", "ridged", "eroded"};
    static const char *returns[] = {"", "F1", "F2", "F2-F1", "value"};
    static const char *distances[] = {"", "euclidean", "manhattan", "chebyshev"};
    static const char *orients[] = {"", "even", "y-up", "z-up"};
    char what[128];
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind++)
            for (int fractal = 1; fractal <= 4; fractal++)
            {
                if (kind == RBTMATH_NOISE_KIND_CELLULAR && fractal > RBTMATH_NOISE_FRACTAL_FBM)
                    continue;
                if (kind == RBTMATH_NOISE_KIND_CELLULAR)
                {
                    for (int ret = 1; ret <= 4; ret++)
                        for (int dist = 1; dist <= 3; dist++)
                        {
                            RBTMATH_NOISE_DESC d = make_desc(dims, kind, fractal, 5);
                            d.cell_return = (RBTMATH_NOISE_CELL_RETURN)ret;
                            d.cell_distance = (RBTMATH_NOISE_CELL_DISTANCE)dist;
                            snprintf(what, sizeof(what), "%uD cellular %s %s %s", dims, fractals[fractal], returns[ret],
                                     distances[dist]);
                            check(what, run(&d, true));
                        }
                    continue;
                }
                int orientations = (kind == RBTMATH_NOISE_KIND_SIMPLEX && dims == 3) ? 3 : 1;
                for (int o = 1; o <= orientations; o++)
                {
                    RBTMATH_NOISE_DESC d = make_desc(dims, kind, fractal, 5);
                    if (orientations > 1)
                        d.orientation = (RBTMATH_NOISE_ORIENTATION)o;
                    snprintf(what, sizeof(what), "%uD %s %s%s%s", dims, kinds[kind], fractals[fractal], orientations > 1 ? " " : "",
                             orientations > 1 ? orients[o] : "");
                    check(what, run(&d, fractal != RBTMATH_NOISE_FRACTAL_ERODED));
                }
            }
    // A tiling gradient fBm and a tiling cellular one.
    RBTMATH_NOISE_DESC d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_FBM, 4);
    d.period[0] = 16;
    d.period[1] = 8;
    d.period[2] = 4;
    check("3D gradient fbm periods 16 8 4", run(&d, true));
    d = make_desc(2, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_FBM, 3);
    d.period[0] = 2;
    d.period[1] = 5;
    check("2D cellular fbm periods 2 5", run(&d, true));
}

static void test_other_calls(void)
{
    RBTMATH_ERROR error;
    // Offsets about a far origin.
    RBTMATH_NOISE_DESC d = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 6);
    static float off[N * 3];
    for (int i = 0; i < N * 3; i++)
        off[i] = (float)places[i];
    double origin[3] = {6.5e6, -1.5e5, 3.25e4};
    CHECK_OK(rbtmath_noise_compute_offsets(&d, origin, off, N, vals, grads, &error));
    uint64_t h = hash_bytes(FNV_START, vals, sizeof(vals));
    check("offsets 3D simplex fbm", hash_bytes(h, grads, sizeof(grads)));
    // A grid.
    RBTMATH_NOISE_GRID grid;
    memset(&grid, 0, sizeof(grid));
    grid.origin[0] = -3.5;
    grid.origin[1] = 1e6;
    grid.step[0] = 0.0625;
    grid.step[1] = -0.375;
    grid.count[0] = 32;
    grid.count[1] = 16;
    grid.count[2] = 1;
    d = make_desc(2, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_RIDGED, 4);
    CHECK_OK(rbtmath_noise_compute_grid(&d, &grid, vals, grads, &error));
    h = hash_bytes(FNV_START, vals, sizeof(vals));
    check("grid 2D gradient ridged", hash_bytes(h, grads, (size_t)N * 2 * sizeof(float)));
    // A warp with its Jacobians.
    RBTMATH_NOISE_DESC field = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 3);
    field.orientation = RBTMATH_NOISE_ORIENTATION_Z_UP;
    RBTMATH_NOISE_WARP_DESC w;
    memset(&w, 0, sizeof(w));
    w.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
    w.field = &field;
    w.amplitude = 12.5;
    static double moved[N * 3];
    static float jac[N * 9];
    CHECK_OK(rbtmath_noise_warp_points(&w, places, N, moved, jac, &error));
    h = hash_bytes(FNV_START, moved, sizeof(moved));
    check("warp 3D simplex fbm z-up", hash_bytes(h, jac, sizeof(jac)));
    // Nearest cells, every distance, both dimensions.
    static float dist[N * 2], offs[N * 6];
    static uint32_t ids[N * 2];
    char what[96];
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int k = 1; k <= 3; k++)
        {
            RBTMATH_NOISE_DESC c = make_desc(dims, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
            c.cell_return = RBTMATH_NOISE_CELL_RETURN_UNKNOWN;
            c.cell_distance = (RBTMATH_NOISE_CELL_DISTANCE)k;
            c.cell_jitter = 1.0f;
            CHECK_OK(rbtmath_noise_compute_nearest_cells(&c, places, N, dist, ids, offs, &error));
            h = hash_bytes(FNV_START, dist, sizeof(dist));
            h = hash_bytes(h, ids, sizeof(ids));
            snprintf(what, sizeof(what), "nearest cells %uD distance %d", dims, k);
            check(what, hash_bytes(h, offs, (size_t)N * 2 * dims * sizeof(float)));
        }
}

// The Definition's parts a copy must match exactly.
static void test_definition(void)
{
    uint64_t h = FNV_START;
    for (uint32_t x = 0; x < 4096; x++)
    {
        uint32_t v = ref_h(x * 0x01000193u);
        h = hash_bytes(h, &v, sizeof(v));
    }
    check("H", h);
    h = FNV_START;
    const int64_t cells[6] = {0, -1, 1, 4294967295ll, 4294967296ll, -68719476736ll};
    for (int a = 0; a < 3; a++)
        for (int i = 0; i < 6; i++)
        {
            uint32_t v = ref_p(a, cells[i]);
            h = hash_bytes(h, &v, sizeof(v));
        }
    check("Pa", h);
    // The octave table: 32 octaves' seeds and offsets for one seed.
    h = FNV_START;
    for (uint32_t i = 0; i < 32; i++)
    {
        uint32_t s = ref_octave_seed(0xC0FFEEu, i);
        h = hash_bytes(h, &s, sizeof(s));
        for (int a = 0; a < 3; a++)
        {
            double o = ref_offset(s, a);
            h = hash_bytes(h, &o, sizeof(o));
        }
    }
    check("octave table", h);
    // X_i, Y_a and FLIP: no X_i ^ Y_a (Y absent too) equals another, nor one
    // xor FLIP, for i < 32 and a < 3.
    uint32_t combos[32 * 4];
    int n = 0;
    for (uint32_t i = 0; i < 32; i++)
    {
        combos[n++] = ref_x(i);
        for (uint32_t a = 0; a < 3; a++)
            combos[n++] = ref_x(i) ^ ref_y(a);
    }
    int clashes = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
        {
            if (i != j && combos[i] == combos[j])
                clashes++;
            if (combos[i] == (combos[j] ^ REF_FLIP))
                clashes++;
        }
    if (clashes)
    {
        printf("FAIL the octave and channel constants: %d clashes\n", clashes);
        failures++;
    }
    // The tables and constants.
    h = FNV_START;
    h = hash_bytes(h, ref_perlin16, sizeof(ref_perlin16));
    for (int k = 0; k < 24; k++)
    {
        float q[2];
        ref_direction24(k, q);
        h = hash_bytes(h, q, sizeof(q));
    }
    for (int k = 0; k < 48; k++)
    {
        float q[3];
        ref_direction48(k, q);
        h = hash_bytes(h, q, sizeof(q));
    }
    check("tables", h);
    const double dconst[5] = {REF_F2, REF_GD, REF_T23, REF_R3, RBTMATH_NOISE_MAX_CELLS};
    const float fconst[8] = {REF_G, REF_E1, REF_E11, REF_T23F, REF_R3F, REF_K2, REF_K3, 0.0f};
    h = hash_bytes(FNV_START, dconst, sizeof(dconst));
    check("constants", hash_bytes(h, fconst, sizeof(fconst)));
    // The constants are the values they name, to their precision.
    CHECK_NEAR(REF_F2, (sqrt(3.0) - 1.0) / 2.0, 1e-16);
    CHECK_NEAR(REF_GD, -(3.0 - sqrt(3.0)) / 6.0, 1e-16);
    CHECK_NEAR(REF_G, -(3.0 - sqrt(3.0)) / 6.0, 1e-8);
    CHECK_NEAR(REF_E1, 1.0 - (3.0 - sqrt(3.0)) / 6.0, 1e-7);
    CHECK_NEAR(REF_E11, 1.0 - (3.0 - sqrt(3.0)) / 3.0, 1e-7);
    CHECK_NEAR(REF_R3, 1.0 / sqrt(3.0), 1e-16);
    CHECK_NEAR(REF_T23, 2.0 / 3.0, 1e-16);
    CHECK_NEAR(REF_A48, 1.0 + sqrt(1.5), 1e-6);
    for (int k = 0; k < 6; k++)
        CHECK_NEAR(ref_sines[k], sin((7.5 + 15.0 * k) * 3.14159265358979323846 / 180.0), 1e-7);
}

// Everything the runs above give, under the current floating-point mode, in
// one array, for the flush comparison.
static float snapshot[64][N * 4];
static int snapshot_runs(void)
{
    int r = 0;
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 2; kind++)
            for (int fractal = 1; fractal <= 3; fractal++)
            {
                RBTMATH_NOISE_DESC d = make_desc(dims, kind, fractal, 5);
                rbtmath_noise_compute_points(&d, places, N, snapshot[r], snapshot[r] + N, NULL);
                r++;
            }
    return r;
}

// Flushing subnormals, through whichever control this platform's own
// headers offer, found by that control's macro: Apple's fenv.h
// (FE_DFL_DISABLE_DENORMS_ENV: arm64's FPCR.FZ), MSVC's float.h (_MCW_DN),
// SSE's xmmintrin.h and pmmintrin.h (_MM_FLUSH_ZERO_ON, _MM_DENORMALS_ZERO_ON:
// MXCSR's FTZ and DAZ). The last two written, not compiled on the Mac.
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

static void test_flushed_subnormals(void)
{
#if defined(FLUSH_CONTROL)
    static float normal[64][N * 4];
    int runs = snapshot_runs();
    memcpy(normal, snapshot, sizeof(normal));
    flush_on();
    snapshot_runs();
    flush_off();
    int bad = 0;
    for (int r = 0; r < runs; r++)
        for (int i = 0; i < N * 4; i++)
            if (fabsf(normal[r][i] - snapshot[r][i]) > 0x1p-100f)
                bad++;
    if (bad)
    {
        printf("FAIL with subnormals flushed: %d values or gradients moved by more than 2^-100\n", bad);
        failures++;
    }
    else
        printf("rbtmathnoise_bits_tests: with subnormals flushed (" FLUSH_CONTROL "), every value within 2^-100\n");
#else
    printf("rbtmathnoise_bits_tests: no control over flushing subnormals in this platform's headers; skipped\n");
#endif
}

int main(int argc, char **argv)
{
    print_only = argc > 1 && strcmp(argv[1], "--print") == 0;
    make_places();
    test_definition();
    test_runs();
    test_other_calls();
    if (!print_only)
        test_flushed_subnormals();
    if (failures)
    {
        printf("rbtmathnoise_bits_tests: %d failure(s)\n", failures);
        return 1;
    }
    if (!print_only)
        printf("rbtmathnoise_bits_tests: all passed\n");
    return 0;
}
