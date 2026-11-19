// Numbers, not a test (include/math/rbtmathfunctions.h; card A61): each
// batch call's cost an element against a loop over the platform's libm on
// the same inputs, and the shell medium's transmittance and density, whose
// exponential is rbtmathfunctions.h's. Run by hand, Release; the numbers go
// in RBTMATH_TODO.md with the machine named.
//
//   build-macos-metal/Release/rbtmathfunctions_bench

#include "math/rbtmathfunctions.h"
#include "math/rbtmathmedium.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

enum { N = 1 << 20, RUNS = 7 };

static uint64_t state = 0xBE4C4BE4C4BE4C4Bull;
static double uniform(double lo, double hi)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return lo + (hi - lo) * ((double)(z >> 11) * 0x1p-53);
}

static double *a, *b, *y;
static volatile double sink;

typedef enum { F_EXP2, F_EXP, F_LOG2, F_ATAN, F_ATAN2, F_TAN, F_COUNT } function;

static void ours(function f)
{
    switch (f)
    {
    case F_EXP2: rbtmath_compute_exp2(a, N, y, NULL); break;
    case F_EXP: rbtmath_compute_exp(a, N, y, NULL); break;
    case F_LOG2: rbtmath_compute_log2(a, N, y, NULL); break;
    case F_ATAN: rbtmath_compute_atan(a, N, y, NULL); break;
    case F_ATAN2: rbtmath_compute_atan2(a, b, N, y, NULL); break;
    case F_TAN: rbtmath_compute_tan(a, N, y, NULL); break;
    default: break;
    }
    sink = y[N / 2];
}

static void libm(function f)
{
    switch (f)
    {
    case F_EXP2: for (int i = 0; i < N; i++) y[i] = exp2(a[i]); break;
    case F_EXP: for (int i = 0; i < N; i++) y[i] = exp(a[i]); break;
    case F_LOG2: for (int i = 0; i < N; i++) y[i] = log2(a[i]); break;
    case F_ATAN: for (int i = 0; i < N; i++) y[i] = atan(a[i]); break;
    case F_ATAN2: for (int i = 0; i < N; i++) y[i] = atan2(a[i], b[i]); break;
    case F_TAN: for (int i = 0; i < N; i++) y[i] = tan(a[i]); break;
    default: break;
    }
    sink = y[N / 2];
}

static double best_ns(void (*run)(function), function f)
{
    double best = 1e30;
    for (int r = 0; r < RUNS; r++)
    {
        const double t0 = now_seconds();
        run(f);
        const double t = (now_seconds() - t0) * 1e9 / N;
        if (t < best)
            best = t;
    }
    return best;
}

static RBTMATH_SHELL_MEDIUM earth(void)
{
    RBTMATH_SHELL_MEDIUM m;
    memset(&m, 0, sizeof(m));
    m.bottom_radius = 6360.0f;
    m.top_radius = 6420.0f;
    m.rayleigh_scattering[0] = 5.802e-3f;
    m.rayleigh_scattering[1] = 13.558e-3f;
    m.rayleigh_scattering[2] = 33.1e-3f;
    m.rayleigh_scale_height = 8.0f;
    m.mie_scattering[0] = m.mie_scattering[1] = m.mie_scattering[2] = 3.996e-3f;
    m.mie_extinction[0] = m.mie_extinction[1] = m.mie_extinction[2] = 4.4e-3f;
    m.mie_scale_height = 1.2f;
    m.mie_anisotropy = 0.8f;
    m.absorption_extinction[0] = 0.65e-3f;
    m.absorption_extinction[1] = 1.881e-3f;
    m.absorption_extinction[2] = 0.085e-3f;
    m.absorption_center_height = 25.0f;
    m.absorption_half_width = 15.0f;
    m.ground_albedo[0] = m.ground_albedo[1] = m.ground_albedo[2] = 0.3f;
    return m;
}

static void medium(void)
{
    enum { RAYS = 10000, STEPS = 64, POINTS = 1 << 18 };
    static float origins[RAYS * 3], dirs[RAYS * 3], t[RAYS * 3], points[POINTS * 3], dens[POINTS * 3];
    for (int i = 0; i < RAYS; i++)
    {
        // From 0 to 50 km up, looking anywhere above the horizon's dip.
        const double h = uniform(0.0, 50.0);
        origins[i * 3 + 0] = 0.0f;
        origins[i * 3 + 1] = (float)(6360.0 + h);
        origins[i * 3 + 2] = 0.0f;
        const double up = uniform(-0.05, 1.0);
        const double side = sqrt(1.0 - up * up);
        dirs[i * 3 + 0] = (float)side;
        dirs[i * 3 + 1] = (float)up;
        dirs[i * 3 + 2] = 0.0f;
    }
    for (int i = 0; i < POINTS; i++)
    {
        points[i * 3 + 0] = 0.0f;
        points[i * 3 + 1] = (float)(6360.0 + uniform(0.0, 60.0));
        points[i * 3 + 2] = 0.0f;
    }
    const RBTMATH_SHELL_MEDIUM m = earth();
    double best_t = 1e30, best_d = 1e30;
    for (int r = 0; r < RUNS; r++)
    {
        double t0 = now_seconds();
        rbtmath_shell_medium_compute_transmittance(&m, origins, dirs, RAYS, STEPS, t, NULL);
        double dt = now_seconds() - t0;
        if (dt < best_t)
            best_t = dt;
        t0 = now_seconds();
        rbtmath_shell_medium_compute_density(&m, points, POINTS, dens, NULL);
        dt = now_seconds() - t0;
        if (dt < best_d)
            best_d = dt;
    }
    printf("shell medium: transmittance %.3f ms for %d rays of %d steps (%.1f ns a step); density %.2f ns a point (%d points); best of %d\n",
           best_t * 1e3, RAYS, STEPS, best_t * 1e9 / (RAYS * (double)STEPS), best_d * 1e9 / POINTS, POINTS, RUNS);
}

int main(void)
{
    a = (double *)malloc(N * sizeof(double));
    b = (double *)malloc(N * sizeof(double));
    y = (double *)malloc(N * sizeof(double));
    if (!a || !b || !y)
        return 1;
    static const char *const names[F_COUNT] = {"exp2", "exp", "log2", "atan", "atan2", "tan"};
    static const char *const ranges[F_COUNT] = {"[-30, 30]", "[-30, 30]", "(0, 1000]", "[-4, 4]", "[-10, 10]^2", "[-pi/2, pi/2]"};
    printf("%d elements, best of %d, ns an element\n", N, RUNS);
    printf("function  range          ours   libm\n");
    for (int f = 0; f < F_COUNT; f++)
    {
        for (int i = 0; i < N; i++)
        {
            switch (f)
            {
            case F_EXP2:
            case F_EXP: a[i] = uniform(-30.0, 30.0); break;
            case F_LOG2: a[i] = uniform(0x1p-20, 1000.0); break;
            case F_ATAN: a[i] = uniform(-4.0, 4.0); break;
            case F_ATAN2:
                a[i] = uniform(-10.0, 10.0);
                b[i] = uniform(-10.0, 10.0);
                break;
            case F_TAN: a[i] = uniform(-1.5707963267948966, 1.5707963267948966); break;
            }
        }
        const double o = best_ns(ours, (function)f);
        const double l = best_ns(libm, (function)f);
        printf("%-9s %-13s %6.2f %6.2f\n", names[f], ranges[f], o, l);
    }
    medium();
    free(a);
    free(b);
    free(y);
    return 0;
}
