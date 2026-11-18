// Numbers, not a test (card A56, the approved draft's section 10): RbtMath's
// noise in samples a second, one core and on a pool of every core, per kind
// x dimension x fractal (1 and 8 octaves) x with and without gradients;
// points against offsets against a grid; a warp; nearest cells; places at
// the origin and at 1e7; and a terrain chunk's proxy (a 33 x 33 grid of a
// sphere's places, four layers and a warp: milliseconds a chunk). Run by
// hand, Release; the numbers go in RBTMATH_TODO.md with the machine named.
//
// Written in C, as the other tests of a C header are.

#include "jobs/rbtjobs.h"
#include "math/rbtmathnoise.h"
#include <math.h>
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

enum { N = 65536 };
static double places[N * 3], far_places[N * 3];
static float offsets[N * 3];
static float values[N], gradients[N * 9], distances[N * 2];
static uint32_t ids[N * 2];
static double moved[N * 3];

static RBTMATH_NOISE_DESC make_desc(uint32_t dims, RBTMATH_NOISE_KIND kind, RBTMATH_NOISE_FRACTAL fractal, uint32_t octaves)
{
    RBTMATH_NOISE_DESC d;
    memset(&d, 0, sizeof(d));
    d.base.type = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
    d.dimensions = dims;
    d.kind = kind;
    d.fractal = fractal;
    d.seed = 99;
    d.octaves = octaves;
    d.frequency = 0.01;
    if (fractal != RBTMATH_NOISE_FRACTAL_SINGLE)
    {
        d.lacunarity = 2.0;
        d.gain = 0.5f;
    }
    if (fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
        d.ridge_feedback = 2.0f;
    if (kind == RBTMATH_NOISE_KIND_CELLULAR)
    {
        d.cell_distance = RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN;
        d.cell_return = RBTMATH_NOISE_CELL_RETURN_F2_MINUS_F1;
        d.cell_jitter = 1.0f;
    }
    if (kind == RBTMATH_NOISE_KIND_SIMPLEX && dims == 3)
        d.orientation = RBTMATH_NOISE_ORIENTATION_EVEN;
    return d;
}

// Nanoseconds a place, the best of five runs of N places.
static double time_points(const RBTMATH_NOISE_DESC *d, const double *p, int gradients_too)
{
    double best = 1e30;
    for (int r = 0; r < 5; r++)
    {
        double t0 = now_seconds();
        rbtmath_noise_compute_points(d, p, N, values, gradients_too ? gradients : NULL, NULL);
        double t = (now_seconds() - t0) * 1e9 / N;
        if (t < best)
            best = t;
    }
    return best;
}

typedef struct pool_job
{
    const RBTMATH_NOISE_DESC *desc;
} pool_job;

static void pool_task(uint32_t first, uint32_t end, uint32_t worker, void *data)
{
    (void)worker;
    pool_job *job = (pool_job *)data;
    uint32_t dims = job->desc->dimensions;
    rbtmath_noise_compute_points(job->desc, places + (size_t)first * dims, end - first, values + first, NULL, NULL);
}

int main(void)
{
    for (int i = 0; i < N * 3; i++)
    {
        places[i] = (double)((i * 7919) % 100003) * 0.731 - 36000.0;
        far_places[i] = places[i] + 1e7;
        offsets[i] = (float)places[i];
    }
    static const char *kinds[] = {"", "gradient", "simplex", "cellular"};
    static const char *fractals[] = {"", "single", "fbm", "ridged", "eroded"};
    printf("rbtmathnoise_bench: ns a place, one core (best of 5 x %d places)\n", N);
    printf("%-34s %10s %10s %10s\n", "", "value", "+gradient", "at 1e7");
    for (uint32_t dims = 2; dims <= 3; dims++)
        for (int kind = 1; kind <= 3; kind++)
            for (int fractal = 1; fractal <= 4; fractal++)
            {
                if (kind == RBTMATH_NOISE_KIND_CELLULAR && fractal > RBTMATH_NOISE_FRACTAL_FBM)
                    continue;
                uint32_t octaves = fractal == RBTMATH_NOISE_FRACTAL_SINGLE ? 1 : 8;
                RBTMATH_NOISE_DESC d = make_desc(dims, (RBTMATH_NOISE_KIND)kind, (RBTMATH_NOISE_FRACTAL)fractal, octaves);
                if (fractal == RBTMATH_NOISE_FRACTAL_ERODED)
                    d.erosion = 1.0f;
                char name[64];
                snprintf(name, sizeof(name), "%uD %s %s (%u)", dims, kinds[kind], fractals[fractal], octaves);
                double v = time_points(&d, places, 0);
                double g = fractal == RBTMATH_NOISE_FRACTAL_ERODED ? 0.0 : time_points(&d, places, 1);
                double far = time_points(&d, far_places, 0);
                printf("%-34s %10.1f %10.1f %10.1f\n", name, v, g, far);
            }

    // Points against offsets against a grid, 3D gradient fBm of 4.
    RBTMATH_NOISE_DESC d = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_FBM, 4);
    double origin[3] = {1e6, 2e5, -3e4};
    RBTMATH_NOISE_GRID grid;
    memset(&grid, 0, sizeof(grid));
    grid.step[0] = grid.step[1] = grid.step[2] = 0.5;
    grid.count[0] = grid.count[1] = 64;
    grid.count[2] = 16;
    double t0 = now_seconds();
    rbtmath_noise_compute_offsets(&d, origin, offsets, N, values, NULL, NULL);
    double t_off = (now_seconds() - t0) * 1e9 / N;
    t0 = now_seconds();
    rbtmath_noise_compute_grid(&d, &grid, values, NULL, NULL);
    double t_grid = (now_seconds() - t0) * 1e9 / N;
    printf("3D gradient fbm (4): points %.1f, offsets %.1f, grid %.1f ns a place\n", time_points(&d, places, 0), t_off, t_grid);

    // A warp, 3D simplex fBm of 4, with and without Jacobians.
    RBTMATH_NOISE_DESC field = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 4);
    RBTMATH_NOISE_WARP_DESC w;
    memset(&w, 0, sizeof(w));
    w.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
    w.field = &field;
    w.amplitude = 50.0;
    t0 = now_seconds();
    rbtmath_noise_warp_points(&w, places, N, moved, NULL, NULL);
    double t_warp = (now_seconds() - t0) * 1e9 / N;
    t0 = now_seconds();
    rbtmath_noise_warp_points(&w, places, N, moved, gradients, NULL);
    double t_warp_j = (now_seconds() - t0) * 1e9 / N;
    printf("a 3D warp of simplex fbm (4): %.1f ns a place, %.1f with Jacobians\n", t_warp, t_warp_j);

    // Nearest cells.
    for (uint32_t dims = 2; dims <= 3; dims++)
    {
        RBTMATH_NOISE_DESC c = make_desc(dims, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE, 1);
        c.cell_return = RBTMATH_NOISE_CELL_RETURN_UNKNOWN;
        t0 = now_seconds();
        rbtmath_noise_compute_nearest_cells(&c, places, N, distances, ids, gradients, NULL);
        printf("nearest cells %uD: %.1f ns a place\n", dims, (now_seconds() - t0) * 1e9 / N);
    }

    // On a pool of every core: 3D simplex fBm of 8.
    uint32_t cores = rbtjobs_get_processor_count();
    RBTJOBS_POOL_DESC pd;
    memset(&pd, 0, sizeof(pd));
    pd.base.type = RBT_STRUCT_TYPE_JOBS_POOL_DESC;
    pd.thread_count = cores;
    rbtjobs_pool pool = NULL;
    if (rbtjobs_pool_create(&pd, &pool, NULL) == RBT_RESULT_OK)
    {
        RBTJOBS_SCHEDULER s;
        memset(&s, 0, sizeof(s));
        s.base.type = RBT_STRUCT_TYPE_JOBS_SCHEDULER;
        rbtjobs_pool_get_scheduler(pool, &s);
        RBTMATH_NOISE_DESC f8 = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 8);
        pool_job job = {&f8};
        double best = 1e30;
        for (int r = 0; r < 5; r++)
        {
            t0 = now_seconds();
            rbtjobs_run(&s, pool_task, N, 1024, &job);
            double t = (now_seconds() - t0) * 1e9 / N;
            if (t < best)
                best = t;
        }
        printf("3D simplex fbm (8) on a pool of %u: %.1f ns a place (one core %.1f)\n", cores, best,
               time_points(&f8, places, 0));
        rbtjobs_pool_destroy(pool);
    }

    // A terrain chunk's proxy: a 33 x 33 grid on a 150 km sphere's face, in
    // double, four layers (simplex fBm 8, ridged simplex 6, gradient fBm 6,
    // cellular F1 3) and a 3D warp of simplex fBm 4, gradients on.
    enum { C = 33 * 33 };
    static double chunk[C * 3], warped[C * 3];
    static float cv[C], cg[C * 3];
    const double radius = 150000.0;
    for (int j = 0; j < 33; j++)
        for (int i = 0; i < 33; i++)
        {
            double x = 1.0, y = -0.2 + 0.4 * i / 32.0, z = -0.2 + 0.4 * j / 32.0;
            double l = sqrt(x * x + y * y + z * z);
            double *p = &chunk[(j * 33 + i) * 3];
            p[0] = x / l * radius;
            p[1] = y / l * radius;
            p[2] = z / l * radius;
        }
    RBTMATH_NOISE_DESC layers[4];
    layers[0] = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 8);
    layers[1] = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_RIDGED, 6);
    layers[2] = make_desc(3, RBTMATH_NOISE_KIND_GRADIENT, RBTMATH_NOISE_FRACTAL_FBM, 6);
    layers[3] = make_desc(3, RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_FBM, 3);
    for (int l = 0; l < 4; l++)
        layers[l].frequency = 1.0 / 20000.0;
    RBTMATH_NOISE_DESC wf = make_desc(3, RBTMATH_NOISE_KIND_SIMPLEX, RBTMATH_NOISE_FRACTAL_FBM, 4);
    wf.frequency = 1.0 / 30000.0;
    RBTMATH_NOISE_WARP_DESC cw;
    memset(&cw, 0, sizeof(cw));
    cw.base.type = RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC;
    cw.field = &wf;
    cw.amplitude = 2000.0;
    double best = 1e30;
    for (int r = 0; r < 20; r++)
    {
        t0 = now_seconds();
        rbtmath_noise_warp_points(&cw, chunk, C, warped, NULL, NULL);
        for (int l = 0; l < 4; l++)
            rbtmath_noise_compute_points(&layers[l], warped, C, cv, cg, NULL);
        double t = (now_seconds() - t0) * 1e3;
        if (t < best)
            best = t;
    }
    printf("a terrain chunk's proxy (33 x 33, four layers and a warp, gradients): %.3f ms on one core\n", best);
    return 0;
}
