#include "math/rbtmathnoise.h"
#include "../rbtstructbase.hpp"
#include "rbtmathnoise_internal.hpp"
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <stdio.h>

// RbtMath's noise (rbtmathnoise.h): coherent noise from double places, the
// same bits everywhere, by the header's Definition. Each call checks its
// arguments whole before writing anything, builds the octave table on the
// stack, and walks the places in blocks of 16: per block, octave by octave,
// each place's lattice coordinate (in double), then its octave (in float),
// summed into the place's value and gradient. A place's order of operations
// is the same whatever block it falls in, so a batch split anywhere gives
// the same bits. Nothing allocates.
//
// Built without floating-point contraction (CMakeLists.txt, by the
// compiler's ID); rbtmathnoise_internal.hpp asserts FLT_EVAL_METHOD 0.

using namespace rbtnoise;

namespace
{

// ---- Errors -----------------------------------------------------------------

RBT_RESULT fail(RBTMATH_ERROR *error, RBT_RESULT result, const char *format, ...)
{
    if (error)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return result;
}

void clear(RBTMATH_ERROR *error)
{
    if (error)
        error->message[0] = '\0';
}

// ---- The desc ---------------------------------------------------------------

// Which call checks a desc: the compute calls (a cellular noise names its
// return) or the nearest cells (cellular, one octave, no return).
enum class Use
{
    compute,
    nearest
};

// The finest octave's frequency, f_(n-1), as step 3 makes it.
double finest_frequency(const RBTMATH_NOISE_DESC &d)
{
    double f = d.frequency;
    for (uint32_t i = 1; i < d.octaves; i++)
        f = f * d.lacunarity;
    return f;
}

bool reads_lacunarity(const RBTMATH_NOISE_DESC &d) { return d.fractal != RBTMATH_NOISE_FRACTAL_SINGLE; }

// A desc, field by field in the struct's order, each its own check and
// message; `what` names the desc in the message (a warp's field, say).
RBT_RESULT check_desc(const RBTMATH_NOISE_DESC *desc, Use use, const char *what, RBTMATH_ERROR *error)
{
    if (!desc)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s is NULL", what);
    const RBT_RESULT based = rbt_struct_check(&desc->base, RBT_STRUCT_TYPE_MATH_NOISE_DESC);
    if (based != RBT_RESULT_OK)
        return fail(error, based, "%s: %s", what, rbt_struct_check_message(based));
    const RBTMATH_NOISE_DESC &d = *desc;
    if (d.dimensions != 2 && d.dimensions != 3)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's dimensions, %u, is not 2 or 3", what, d.dimensions);
    if (d.kind < RBTMATH_NOISE_KIND_GRADIENT || d.kind > RBTMATH_NOISE_KIND_CELLULAR)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's kind, %u, is not a RBTMATH_NOISE_KIND", what, (unsigned)d.kind);
    if (use == Use::nearest)
    {
        if (d.kind != RBTMATH_NOISE_KIND_CELLULAR)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's kind is not RBTMATH_NOISE_KIND_CELLULAR, which the nearest cells need",
                        what);
    }
    if (d.fractal < RBTMATH_NOISE_FRACTAL_SINGLE || d.fractal > RBTMATH_NOISE_FRACTAL_ERODED)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's fractal, %u, is not a RBTMATH_NOISE_FRACTAL", what, (unsigned)d.fractal);
    if (d.kind == RBTMATH_NOISE_KIND_CELLULAR)
    {
        if (d.fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's fractal is RIDGED, which takes a gradient or simplex kind, not cellular",
                        what);
        if (d.fractal == RBTMATH_NOISE_FRACTAL_ERODED)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's fractal is ERODED, which takes a gradient or simplex kind, not cellular",
                        what);
    }
    if (use == Use::nearest)
    {
        if (d.fractal != RBTMATH_NOISE_FRACTAL_SINGLE)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's fractal is not RBTMATH_NOISE_FRACTAL_SINGLE, which the nearest cells need",
                        what);
    }
    if (d.fractal == RBTMATH_NOISE_FRACTAL_SINGLE)
    {
        if (d.octaves != 1)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's octaves, %u, is not 1, which SINGLE needs", what, d.octaves);
    }
    if (d.octaves == 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's octaves is 0", what);
    if (d.octaves > RBTMATH_NOISE_MAX_OCTAVES)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's octaves, %u, is above RBTMATH_NOISE_MAX_OCTAVES (%u)", what, d.octaves,
                    RBTMATH_NOISE_MAX_OCTAVES);
    if (!std::isfinite(d.frequency))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's frequency is not finite", what);
    if (!(d.frequency > 0.0))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's frequency, %g, is not above 0", what, d.frequency);
    // Each field below is checked as read or as unread (then it must be 0):
    // one condition an if.
    if (reads_lacunarity(d))
    {
        if (!std::isfinite(d.lacunarity))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's lacunarity is not finite", what);
        if (!(d.lacunarity > 1.0))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's lacunarity, %g, is not above 1", what, d.lacunarity);
        if (!std::isfinite(finest_frequency(d)))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT,
                        "%s's lacunarity, %g, makes the finest octave's frequency (frequency x lacunarity^(octaves - 1)) not finite",
                        what, d.lacunarity);
        if (!std::isfinite(d.gain))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's gain is not finite", what);
        if (!(d.gain > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's gain, %g, is not above 0", what, (double)d.gain);
    }
    else
    {
        if (d.lacunarity != 0.0)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's lacunarity is set, which SINGLE doesn't read: it must be 0", what);
        if (d.gain != 0.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's gain is set, which SINGLE doesn't read: it must be 0", what);
    }
    if (d.fractal == RBTMATH_NOISE_FRACTAL_RIDGED)
    {
        if (!std::isfinite(d.ridge_feedback))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's ridge_feedback is not finite", what);
        if (!(d.ridge_feedback > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's ridge_feedback, %g, is not above 0", what, (double)d.ridge_feedback);
    }
    else if (d.ridge_feedback != 0.0f)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's ridge_feedback is set, which only RIDGED reads: it must be 0", what);
    if (d.fractal == RBTMATH_NOISE_FRACTAL_ERODED)
    {
        if (!std::isfinite(d.erosion))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's erosion is not finite", what);
        if (!(d.erosion > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's erosion, %g, is not above 0", what, (double)d.erosion);
    }
    else if (d.erosion != 0.0f)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's erosion is set, which only ERODED reads: it must be 0", what);
    if (d.kind == RBTMATH_NOISE_KIND_CELLULAR)
    {
        if (d.cell_distance < RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_distance is not chosen", what);
        if (d.cell_distance > RBTMATH_NOISE_CELL_DISTANCE_CHEBYSHEV)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_distance, %u, is not a RBTMATH_NOISE_CELL_DISTANCE", what,
                        (unsigned)d.cell_distance);
        if (use == Use::compute)
        {
            if (d.cell_return < RBTMATH_NOISE_CELL_RETURN_F1)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_return is not chosen", what);
            if (d.cell_return > RBTMATH_NOISE_CELL_RETURN_CELL_VALUE)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_return, %u, is not a RBTMATH_NOISE_CELL_RETURN", what,
                            (unsigned)d.cell_return);
        }
        else if (d.cell_return != RBTMATH_NOISE_CELL_RETURN_UNKNOWN)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_return is set: the nearest cells give every return, so it must be 0",
                        what);
        if (!std::isfinite(d.cell_jitter))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_jitter is not finite", what);
        if (!(d.cell_jitter > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_jitter, %g, is not above 0", what, (double)d.cell_jitter);
        if (d.cell_jitter > 1.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_jitter, %g, is above 1", what, (double)d.cell_jitter);
    }
    else
    {
        if (d.cell_distance != RBTMATH_NOISE_CELL_DISTANCE_UNKNOWN)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_distance is set, which only CELLULAR reads: it must be 0", what);
        if (d.cell_return != RBTMATH_NOISE_CELL_RETURN_UNKNOWN)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_return is set, which only CELLULAR reads: it must be 0", what);
        if (d.cell_jitter != 0.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's cell_jitter is set, which only CELLULAR reads: it must be 0", what);
    }
    if (d.kind == RBTMATH_NOISE_KIND_SIMPLEX && d.dimensions == 3)
    {
        if (d.orientation < RBTMATH_NOISE_ORIENTATION_EVEN)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's orientation is not chosen, which 3D SIMPLEX needs", what);
        if (d.orientation > RBTMATH_NOISE_ORIENTATION_Z_UP)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's orientation, %u, is not a RBTMATH_NOISE_ORIENTATION", what,
                        (unsigned)d.orientation);
    }
    else if (d.orientation != RBTMATH_NOISE_ORIENTATION_UNKNOWN)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's orientation is set, which only 3D SIMPLEX reads: it must be 0", what);
    if (d.dimensions == 2)
    {
        if (d.period[2] != 0)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's period[2] is set in 2D: it must be 0", what);
    }
    for (int a = 0; a < 3; a++)
    {
        if (d.period[a] == 0)
            continue;
        if (d.kind == RBTMATH_NOISE_KIND_SIMPLEX)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's period[%d] is set, which SIMPLEX doesn't take: it doesn't tile", what, a);
        if (reads_lacunarity(d))
        {
            if (std::floor(d.lacunarity) != d.lacunarity)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's period[%d] is set with a lacunarity, %g, that isn't a whole number",
                            what, a, d.lacunarity);
        }
        double last = (double)d.period[a];
        for (uint32_t i = 1; i < d.octaves; i++)
            last = last * d.lacunarity;
        if (last > RBTMATH_NOISE_MAX_CELLS)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT,
                        "%s's period[%d], %u, is %g cells at the last octave, above RBTMATH_NOISE_MAX_CELLS", what, a, d.period[a], last);
    }
    return RBT_RESULT_OK;
}

// ---- Ranges ------------------------------------------------------------------

// D, a cell's diagonal in the distance (rbtmathnoise.h,
// RBTMATH_NOISE_CELL_RETURN).
float cell_diagonal(const RBTMATH_NOISE_DESC &d)
{
    if (d.cell_distance == RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN)
        return std::sqrt(d.dimensions == 3 ? 3.0f : 2.0f);
    if (d.cell_distance == RBTMATH_NOISE_CELL_DISTANCE_MANHATTAN)
        return (float)d.dimensions;
    return 1.0f;
}

// One octave's range, the clamp's bounds.
void octave_range(const RBTMATH_NOISE_DESC &d, float &lo, float &hi)
{
    if (d.kind != RBTMATH_NOISE_KIND_CELLULAR)
    {
        lo = -1.0f;
        hi = 1.0f;
        return;
    }
    const float dd = cell_diagonal(d);
    switch (d.cell_return)
    {
    case RBTMATH_NOISE_CELL_RETURN_F1:
        lo = 0.0f;
        hi = dd;
        return;
    case RBTMATH_NOISE_CELL_RETURN_F2:
    case RBTMATH_NOISE_CELL_RETURN_F2_MINUS_F1:
        lo = 0.0f;
        hi = 2.0f * dd;
        return;
    default:
        lo = -1.0f;
        hi = 1.0f;
        return;
    }
}

// The largest magnitude a field's value can have: its octave's range times
// S, in double (a warp's bound).
double field_bound(const RBTMATH_NOISE_DESC &d)
{
    float lo, hi;
    octave_range(d, lo, hi);
    double m = std::fabs((double)lo) > std::fabs((double)hi) ? std::fabs((double)lo) : std::fabs((double)hi);
    double s = 0.0;
    float a  = 1.0f;
    for (uint32_t i = 0; i < d.octaves; i++)
    {
        s += (double)a;
        a = a * d.gain;
    }
    return m * s;
}

// ---- The octave table --------------------------------------------------------

void make_octaves(const RBTMATH_NOISE_DESC &d, Octave *table)
{
    double f    = d.frequency;
    float a     = 1.0f;
    int64_t L   = d.fractal == RBTMATH_NOISE_FRACTAL_SINGLE ? 1 : (int64_t)d.lacunarity;
    int64_t per[3] = {(int64_t)d.period[0], (int64_t)d.period[1], (int64_t)d.period[2]};
    for (uint32_t i = 0; i < d.octaves; i++)
    {
        Octave &o     = table[i];
        o.frequency   = f;
        o.frequency_f = (float)f;
        o.seed        = hash32(d.seed ^ octave_x(i));
        o.amplitude   = a;
        for (int ax = 0; ax < 3; ax++)
        {
            o.offset[ax] = (float)((double)(hash32(o.seed ^ AXIS_A[ax]) >> 8) * 0x1p-24);
            o.period[ax] = per[ax];
            per[ax]      = per[ax] * L;
        }
        f = f * d.lacunarity;
        a = a * d.gain;
    }
}

// ---- One octave at one place ---------------------------------------------------

// The octave's n and its per-cell gradient g, scaled and clamped
// (Definition, AN OCTAVE'S VALUE), from the place p in double.
template <int D>
float octave_at(const RBTMATH_NOISE_DESC &d, const Octave &o, const double *p, float g[3])
{
    double u[3] = {0.0, 0.0, 0.0};
    for (int a = 0; a < D; a++)
        u[a] = p[a] * o.frequency + (double)o.offset[a];
    int64_t c[3] = {0, 0, 0};
    float t[3]   = {0.0f, 0.0f, 0.0f};
    float raw    = 0.0f;
    float scale  = 1.0f;
    g[0] = g[1] = g[2] = 0.0f;
    float lo, hi;
    octave_range(d, lo, hi);
    switch (d.kind)
    {
    case RBTMATH_NOISE_KIND_GRADIENT:
        for (int a = 0; a < D; a++)
            split(u[a], c[a], t[a]);
        raw   = D == 3 ? gradient3_raw(o.seed, c, t, o.period, g) : gradient2_raw(o.seed, c, t, o.period, g);
        scale = D == 3 ? SCALE_GRADIENT_3D : SCALE_GRADIENT_2D;
        break;
    case RBTMATH_NOISE_KIND_SIMPLEX:
        if (D == 2)
        {
            const double s = (u[0] + u[1]) * F2;
            u[0]           = u[0] + s;
            u[1]           = u[1] + s;
            split(u[0], c[0], t[0]);
            split(u[1], c[1], t[1]);
            raw   = simplex2_raw(o.seed, c, t, g);
            scale = SCALE_SIMPLEX_2D;
        }
        else
        {
            double v[3];
            if (d.orientation == RBTMATH_NOISE_ORIENTATION_EVEN)
            {
                const double r = ((u[0] + u[1]) + u[2]) * T23;
                v[0]           = r - u[0];
                v[1]           = r - u[1];
                v[2]           = r - u[2];
            }
            else if (d.orientation == RBTMATH_NOISE_ORIENTATION_Z_UP)
            {
                const double w = u[0] + u[1], s = w * GD, z = u[2] * R3;
                v[0] = (u[0] + s) + z;
                v[1] = (u[1] + s) + z;
                v[2] = (w * -R3) + z;
            }
            else
            {
                const double w = u[0] + u[2], s = w * GD, y = u[1] * R3;
                v[0] = (u[0] + s) + y;
                v[2] = (u[2] + s) + y;
                v[1] = (w * -R3) + y;
            }
            for (int a = 0; a < 3; a++)
                split(v[a], c[a], t[a]);
            float gt[3];
            raw = simplex3_raw(o.seed, c, t, gt);
            // The gradient turned back, by the turn's transpose, in float.
            if (d.orientation == RBTMATH_NOISE_ORIENTATION_EVEN)
            {
                const float r = ((gt[0] + gt[1]) + gt[2]) * T23F;
                g[0]          = r - gt[0];
                g[1]          = r - gt[1];
                g[2]          = r - gt[2];
            }
            else if (d.orientation == RBTMATH_NOISE_ORIENTATION_Z_UP)
            {
                const float w = gt[0] + gt[1], s = w * G, z = gt[2] * R3F;
                g[0] = (gt[0] + s) - z;
                g[1] = (gt[1] + s) - z;
                g[2] = (w * R3F) + z;
            }
            else
            {
                const float w = gt[0] + gt[2], s = w * G, y = gt[1] * R3F;
                g[0] = (gt[0] + s) - y;
                g[2] = (gt[2] + s) - y;
                g[1] = (w * R3F) + y;
            }
            scale = SCALE_SIMPLEX_3D;
        }
        break;
    default: // cellular: no scale, its ranges exact
    {
        for (int a = 0; a < D; a++)
            split(u[a], c[a], t[a]);
        Nearest n;
        cellular_nearest<D>(o.seed, c, t, o.period, d.cell_jitter, (int)d.cell_distance, n);
        float g1[3], g2[3];
        float value;
        switch (d.cell_return)
        {
        case RBTMATH_NOISE_CELL_RETURN_F1:
            feature_gradient<D>((int)d.cell_distance, n.e[0], n.f[0], g);
            value = n.f[0];
            break;
        case RBTMATH_NOISE_CELL_RETURN_F2:
            feature_gradient<D>((int)d.cell_distance, n.e[1], n.f[1], g);
            value = n.f[1];
            break;
        case RBTMATH_NOISE_CELL_RETURN_F2_MINUS_F1:
            feature_gradient<D>((int)d.cell_distance, n.e[0], n.f[0], g1);
            feature_gradient<D>((int)d.cell_distance, n.e[1], n.f[1], g2);
            for (int a = 0; a < 3; a++)
                g[a] = g2[a] - g1[a];
            value = n.f[1] - n.f[0];
            break;
        default:
            value = ((float)(n.id[0] >> 8) * 0x1p-23f) - 1.0f;
            break;
        }
        if (value < lo || value > hi)
        {
            value = value < lo ? lo : hi;
            g[0] = g[1] = g[2] = 0.0f;
        }
        return value;
    }
    }
    float n = raw * scale;
    for (int a = 0; a < D; a++)
        g[a] = g[a] * scale;
    if (n < lo || n > hi)
    {
        n    = n < lo ? lo : hi;
        g[0] = g[1] = g[2] = 0.0f;
    }
    return n;
}

// ---- A block of places -----------------------------------------------------------

constexpr uint32_t BLOCK = 16;

// The fractal's sums for one place, in octave order from +0 (Definition,
// FRACTALS). `gradient` NULL when the caller asks for none.
template <int D>
void place_value(const RBTMATH_NOISE_DESC &d, const Octave *table, const double *p, float &out_value, float *out_gradient)
{
    float v = 0.0f;
    float G[3] = {0.0f, 0.0f, 0.0f};
    float w = 1.0f, dw[3] = {0.0f, 0.0f, 0.0f};
    float Dsum[3] = {0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i < d.octaves; i++)
    {
        const Octave &o = table[i];
        float g[3];
        const float n = octave_at<D>(d, o, p, g);
        float gamma[3] = {0.0f, 0.0f, 0.0f};
        for (int a = 0; a < D; a++)
            gamma[a] = g[a] * o.frequency_f;
        const float ai = o.amplitude;
        switch (d.fractal)
        {
        case RBTMATH_NOISE_FRACTAL_RIDGED:
        {
            const float sg = std::fabs(n) < SIGN0 ? 0.0f : (n > 0.0f ? 1.0f : -1.0f);
            const float r  = 1.0f - std::fabs(n);
            const float t  = (r * r) * w;
            v              = v + (ai * t);
            float dt[3]    = {0.0f, 0.0f, 0.0f};
            for (int a = 0; a < D; a++)
            {
                dt[a] = ((((r * -2.0f) * sg) * w) * gamma[a]) + ((r * r) * dw[a]);
                G[a]  = G[a] + (ai * dt[a]);
            }
            const float x = t * d.ridge_feedback;
            if (x > 1.0f)
            {
                w     = 1.0f;
                dw[0] = dw[1] = dw[2] = 0.0f;
            }
            else
            {
                w = x;
                for (int a = 0; a < D; a++)
                    dw[a] = dt[a] * d.ridge_feedback;
            }
            break;
        }
        case RBTMATH_NOISE_FRACTAL_ERODED:
        {
            for (int a = 0; a < D; a++)
                Dsum[a] = Dsum[a] + g[a];
            float q = (Dsum[0] * Dsum[0]) + (Dsum[1] * Dsum[1]);
            if (D == 3)
                q = q + (Dsum[2] * Dsum[2]);
            v = v + ((ai * n) / (1.0f + (d.erosion * q)));
            break;
        }
        default: // SINGLE and FBM
            v = v + (ai * n);
            for (int a = 0; a < D; a++)
                G[a] = G[a] + (ai * gamma[a]);
            break;
        }
    }
    out_value = v;
    if (out_gradient)
        for (int a = 0; a < D; a++)
            out_gradient[a] = G[a];
}

// Where a call's places come from: a list of doubles, offsets about an
// origin, or a grid. Each gives place k's coordinates by the Definition's
// rounding for its call.
struct Places
{
    enum Kind
    {
        points,
        offsets,
        grid
    } kind;
    const double *point_array;
    const double *origin;
    const float *offset_array;
    const RBTMATH_NOISE_GRID *grid_desc;
};

template <int D>
void place(const Places &src, uint32_t k, double *p)
{
    switch (src.kind)
    {
    case Places::points:
        for (int a = 0; a < D; a++)
            p[a] = src.point_array[(size_t)k * D + a];
        return;
    case Places::offsets:
        for (int a = 0; a < D; a++)
            p[a] = src.origin[a] + (double)src.offset_array[(size_t)k * D + a];
        return;
    default:
    {
        const RBTMATH_NOISE_GRID &g = *src.grid_desc;
        uint32_t idx[3];
        idx[0] = k % g.count[0];
        idx[1] = (k / g.count[0]) % g.count[1];
        idx[2] = k / (g.count[0] * g.count[1]);
        for (int a = 0; a < D; a++)
            p[a] = g.origin[a] + ((double)idx[a] * g.step[a]);
        return;
    }
    }
}

// Every place: in blocks of 16, each place's coordinates made once and its
// octaves summed. The order of operations per place is the same whatever
// block it falls in.
template <int D>
void evaluate(const RBTMATH_NOISE_DESC &d, const Octave *table, const Places &src, uint32_t count, float *out_values,
              float *out_gradients)
{
    double block[BLOCK][3];
    for (uint32_t first = 0; first < count; first += BLOCK)
    {
        const uint32_t n = count - first < BLOCK ? count - first : BLOCK;
        for (uint32_t k = 0; k < n; k++)
            place<D>(src, first + k, block[k]);
        for (uint32_t k = 0; k < n; k++)
            place_value<D>(d, table, block[k], out_values[first + k], out_gradients ? out_gradients + (size_t)(first + k) * D : nullptr);
    }
}

// Whether a place is in range: finite, and |p[a]| x the finest octave's
// frequency at most RBTMATH_NOISE_MAX_CELLS on every axis.
RBT_RESULT check_place(const double *p, uint32_t dims, double finest, double margin, uint32_t index, RBTMATH_ERROR *error)
{
    for (uint32_t a = 0; a < dims; a++)
    {
        if (!std::isfinite(p[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "place %u's coordinate %u is not finite", index, a);
        if ((std::fabs(p[a]) + margin) * finest > RBTMATH_NOISE_MAX_CELLS)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT,
                        "place %u's coordinate %u, %g, is past RBTMATH_NOISE_MAX_CELLS cells of the finest octave%s", index, a, p[a],
                        margin > 0.0 ? " once the warp may move it" : "");
    }
    return RBT_RESULT_OK;
}

RBT_RESULT run(const RBTMATH_NOISE_DESC &d, const Places &src, uint32_t count, float *out_values, float *out_gradients)
{
    Octave table[RBTMATH_NOISE_MAX_OCTAVES];
    make_octaves(d, table);
    if (d.dimensions == 3)
        evaluate<3>(d, table, src, count, out_values, out_gradients);
    else
        evaluate<2>(d, table, src, count, out_values, out_gradients);
    return RBT_RESULT_OK;
}

} // namespace

extern "C" RBT_RESULT rbtmath_noise_compute_points(const RBTMATH_NOISE_DESC *desc, const double *points, uint32_t count,
                                                   float *out_values, float *out_gradients, RBTMATH_ERROR *error)
{
    clear(error);
    RBT_RESULT r = check_desc(desc, Use::compute, "the desc", error);
    if (r != RBT_RESULT_OK)
        return r;
    if (count > 0 && !points)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (count > 0 && !out_values)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_values is NULL with a count of %u", count);
    if (out_gradients && desc->fractal == RBTMATH_NOISE_FRACTAL_ERODED)
        return fail(error, RBT_RESULT_UNSUPPORTED, "an ERODED noise has no gradient: it would need each octave's second derivatives");
    const double finest = finest_frequency(*desc);
    for (uint32_t i = 0; i < count; i++)
    {
        r = check_place(points + (size_t)i * desc->dimensions, desc->dimensions, finest, 0.0, i, error);
        if (r != RBT_RESULT_OK)
            return r;
    }
    Places src = {Places::points, points, nullptr, nullptr, nullptr};
    return run(*desc, src, count, out_values, out_gradients);
}

extern "C" RBT_RESULT rbtmath_noise_compute_offsets(const RBTMATH_NOISE_DESC *desc, const double *origin, const float *offsets,
                                                    uint32_t count, float *out_values, float *out_gradients, RBTMATH_ERROR *error)
{
    clear(error);
    RBT_RESULT r = check_desc(desc, Use::compute, "the desc", error);
    if (r != RBT_RESULT_OK)
        return r;
    const uint32_t dims = desc->dimensions;
    if (!origin)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "origin is NULL");
    for (uint32_t a = 0; a < dims; a++)
        if (!std::isfinite(origin[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "origin's coordinate %u is not finite", a);
    if (count > 0 && !offsets)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "offsets is NULL with a count of %u", count);
    if (count > 0 && !out_values)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_values is NULL with a count of %u", count);
    if (out_gradients && desc->fractal == RBTMATH_NOISE_FRACTAL_ERODED)
        return fail(error, RBT_RESULT_UNSUPPORTED, "an ERODED noise has no gradient: it would need each octave's second derivatives");
    const double finest = finest_frequency(*desc);
    for (uint32_t i = 0; i < count; i++)
    {
        double p[3] = {0.0, 0.0, 0.0};
        for (uint32_t a = 0; a < dims; a++)
        {
            const float o = offsets[(size_t)i * dims + a];
            if (!std::isfinite(o))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "offset %u's coordinate %u is not finite", i, a);
            p[a] = origin[a] + (double)o;
        }
        r = check_place(p, dims, finest, 0.0, i, error);
        if (r != RBT_RESULT_OK)
            return r;
    }
    Places src = {Places::offsets, nullptr, origin, offsets, nullptr};
    return run(*desc, src, count, out_values, out_gradients);
}

extern "C" RBT_RESULT rbtmath_noise_compute_grid(const RBTMATH_NOISE_DESC *desc, const RBTMATH_NOISE_GRID *grid, float *out_values,
                                                 float *out_gradients, RBTMATH_ERROR *error)
{
    clear(error);
    RBT_RESULT r = check_desc(desc, Use::compute, "the desc", error);
    if (r != RBT_RESULT_OK)
        return r;
    const uint32_t dims = desc->dimensions;
    if (!grid)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "grid is NULL");
    const RBTMATH_NOISE_GRID &g = *grid;
    for (uint32_t a = 0; a < 3; a++)
        if (!std::isfinite(g.origin[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's origin[%u] is not finite", a);
    if (dims == 2)
    {
        if (g.origin[2] != 0.0)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's origin[2] is not 0 in 2D");
    }
    for (uint32_t a = 0; a < 3; a++)
        if (!std::isfinite(g.step[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's step[%u] is not finite", a);
    if (dims == 2)
    {
        if (g.step[2] != 0.0)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's step[2] is not 0 in 2D");
    }
    for (uint32_t a = 0; a < 3; a++)
        if (g.count[a] == 0)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's count[%u] is 0", a);
    if (dims == 2)
    {
        if (g.count[2] != 1)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's count[2], %u, is not 1 in 2D", g.count[2]);
    }
    const uint64_t total = (uint64_t)g.count[0] * g.count[1] * g.count[2];
    if (total > UINT32_MAX)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the grid's places, %llu, are more than UINT32_MAX", (unsigned long long)total);
    if (!out_values)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_values is NULL");
    if (out_gradients && desc->fractal == RBTMATH_NOISE_FRACTAL_ERODED)
        return fail(error, RBT_RESULT_UNSUPPORTED, "an ERODED noise has no gradient: it would need each octave's second derivatives");
    // Both corners: the places are linear along each axis, so its extremes.
    const double finest = finest_frequency(*desc);
    double corner[3];
    for (uint32_t a = 0; a < 3; a++)
        corner[a] = g.origin[a];
    r = check_place(corner, dims, finest, 0.0, 0, error);
    if (r != RBT_RESULT_OK)
        return r;
    for (uint32_t a = 0; a < 3; a++)
        corner[a] = g.origin[a] + ((double)(g.count[a] - 1) * g.step[a]);
    r = check_place(corner, dims, finest, 0.0, (uint32_t)(total - 1), error);
    if (r != RBT_RESULT_OK)
        return r;
    Places src = {Places::grid, nullptr, nullptr, nullptr, grid};
    return run(*desc, src, (uint32_t)total, out_values, out_gradients);
}

extern "C" RBT_RESULT rbtmath_noise_warp_points(const RBTMATH_NOISE_WARP_DESC *desc, const double *points, uint32_t count,
                                                double *out_points, float *out_jacobians, RBTMATH_ERROR *error)
{
    clear(error);
    if (!desc)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the warp desc is NULL");
    const RBT_RESULT based = rbt_struct_check(&desc->base, RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC);
    if (based != RBT_RESULT_OK)
        return fail(error, based, "the warp desc: %s", rbt_struct_check_message(based));
    RBT_RESULT r = check_desc(desc->field, Use::compute, "the warp's field", error);
    if (r != RBT_RESULT_OK)
        return r;
    if (!std::isfinite(desc->amplitude))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the warp's amplitude is not finite");
    if (!(desc->amplitude > 0.0))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the warp's amplitude, %g, is not above 0", desc->amplitude);
    if (count > 0 && !points)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (count > 0 && !out_points)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_points is NULL with a count of %u", count);
    const RBTMATH_NOISE_DESC &field = *desc->field;
    if (out_jacobians && field.fractal == RBTMATH_NOISE_FRACTAL_ERODED)
        return fail(error, RBT_RESULT_UNSUPPORTED, "an ERODED field has no gradient, so no Jacobian");
    // Every place before anything is written, in place included: at the most
    // the field could move it.
    const uint32_t dims  = field.dimensions;
    const double finest  = finest_frequency(field);
    const double reach   = desc->amplitude * field_bound(field);
    for (uint32_t i = 0; i < count; i++)
    {
        r = check_place(points + (size_t)i * dims, dims, finest, reach, i, error);
        if (r != RBT_RESULT_OK)
            return r;
    }
    // Each channel's table: the field with the seed field.seed ^ Y_a.
    Octave tables[3][RBTMATH_NOISE_MAX_OCTAVES];
    RBTMATH_NOISE_DESC channel[3];
    for (uint32_t a = 0; a < dims; a++)
    {
        channel[a]      = field;
        channel[a].seed = field.seed ^ channel_y(a);
        make_octaves(channel[a], tables[a]);
    }
    // In blocks: each block's places read whole before its moved places are
    // written, so the call may move them in place.
    double block[BLOCK][3];
    float v[3][BLOCK], gr[3][BLOCK][3];
    for (uint32_t first = 0; first < count; first += BLOCK)
    {
        const uint32_t n = count - first < BLOCK ? count - first : BLOCK;
        for (uint32_t k = 0; k < n; k++)
            for (uint32_t a = 0; a < dims; a++)
                block[k][a] = points[(size_t)(first + k) * dims + a];
        for (uint32_t a = 0; a < dims; a++)
            for (uint32_t k = 0; k < n; k++)
            {
                if (dims == 3)
                    place_value<3>(channel[a], tables[a], block[k], v[a][k], out_jacobians ? gr[a][k] : nullptr);
                else
                    place_value<2>(channel[a], tables[a], block[k], v[a][k], out_jacobians ? gr[a][k] : nullptr);
            }
        for (uint32_t k = 0; k < n; k++)
        {
            for (uint32_t a = 0; a < dims; a++)
                out_points[(size_t)(first + k) * dims + a] = block[k][a] + (desc->amplitude * (double)v[a][k]);
            if (out_jacobians)
                for (uint32_t a = 0; a < dims; a++)
                    for (uint32_t b = 0; b < dims; b++)
                        out_jacobians[((size_t)(first + k) * dims + a) * dims + b] =
                            (float)((a == b ? 1.0 : 0.0) + (desc->amplitude * (double)gr[a][k][b]));
        }
    }
    return RBT_RESULT_OK;
}

extern "C" RBT_RESULT rbtmath_noise_compute_nearest_cells(const RBTMATH_NOISE_DESC *desc, const double *points, uint32_t count,
                                                          float *out_distances, uint32_t *out_ids, float *out_offsets,
                                                          RBTMATH_ERROR *error)
{
    clear(error);
    RBT_RESULT r = check_desc(desc, Use::nearest, "the desc", error);
    if (r != RBT_RESULT_OK)
        return r;
    if (count > 0 && !points)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (count > 0 && !out_distances && !out_ids && !out_offsets)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_distances, out_ids and out_offsets are all NULL with a count of %u", count);
    const uint32_t dims = desc->dimensions;
    for (uint32_t i = 0; i < count; i++)
    {
        r = check_place(points + (size_t)i * dims, dims, desc->frequency, 0.0, i, error);
        if (r != RBT_RESULT_OK)
            return r;
    }
    Octave table[1];
    make_octaves(*desc, table);
    const Octave &o = table[0];
    for (uint32_t i = 0; i < count; i++)
    {
        const double *p = points + (size_t)i * dims;
        int64_t c[3] = {0, 0, 0};
        float t[3]   = {0.0f, 0.0f, 0.0f};
        for (uint32_t a = 0; a < dims; a++)
            split(p[a] * o.frequency + (double)o.offset[a], c[a], t[a]);
        Nearest n;
        if (dims == 3)
            cellular_nearest<3>(o.seed, c, t, o.period, desc->cell_jitter, (int)desc->cell_distance, n);
        else
            cellular_nearest<2>(o.seed, c, t, o.period, desc->cell_jitter, (int)desc->cell_distance, n);
        for (int k = 0; k < 2; k++)
        {
            if (out_distances)
                out_distances[(size_t)i * 2 + k] = n.f[k] / o.frequency_f;
            if (out_ids)
                out_ids[(size_t)i * 2 + k] = n.id[k];
            if (out_offsets)
                for (uint32_t a = 0; a < dims; a++)
                    out_offsets[((size_t)i * 2 + k) * dims + a] = n.e[k][a] / o.frequency_f;
        }
    }
    return RBT_RESULT_OK;
}


// ---- Baked volumes (rbtmathnoise.h, "Baked volumes" and "Reading a volume") -----------
//
// A row of texels at a time: its places (at most RBTMATH_NOISE_VOLUME_MAX_SIZE
// of them) and one value a noise for each, on the stack; each noise run
// through run() as rbtmath_noise_compute_points() runs it, so a texel's
// bits are the definition's whatever slices a call bakes.

namespace
{

constexpr uint32_t VOLUME_CELLULAR_FREQUENCIES = 5; // W_f for five f, from the kind's lowest

RBTMATH_NOISE_DESC volume_gradient_desc(uint32_t seed)
{
    RBTMATH_NOISE_DESC d = {};
    d.base.type          = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
    d.dimensions         = 3;
    d.kind               = RBTMATH_NOISE_KIND_GRADIENT;
    d.fractal            = RBTMATH_NOISE_FRACTAL_FBM;
    d.seed               = seed ^ 0x243F6A88u;
    d.octaves            = 4;
    d.frequency          = 4.0;
    d.lacunarity         = 2.0;
    d.gain               = 0.5f;
    d.period[0] = d.period[1] = d.period[2] = 4;
    return d;
}

RBTMATH_NOISE_DESC volume_cellular_desc(uint32_t seed, uint32_t f)
{
    RBTMATH_NOISE_DESC d = {};
    d.base.type          = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
    d.dimensions         = 3;
    d.kind               = RBTMATH_NOISE_KIND_CELLULAR;
    d.fractal            = RBTMATH_NOISE_FRACTAL_SINGLE;
    d.seed               = (seed ^ 0x85A308D3u) ^ f;
    d.octaves            = 1;
    d.frequency          = (double)f;
    d.cell_distance      = RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN;
    d.cell_return        = RBTMATH_NOISE_CELL_RETURN_F1;
    d.cell_jitter        = 1.0f;
    d.period[0] = d.period[1] = d.period[2] = f;
    return d;
}

inline float sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline uint8_t to_byte(float v) { return (uint8_t)((v * 255.0f) + 0.5f); }
inline float worley_fbm(float w1, float w2, float w4) { return ((w1 * 0.625f) + (w2 * 0.25f)) + (w4 * 0.125f); }

} // namespace

extern "C" RBT_RESULT rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND kind, uint32_t seed, uint32_t size, uint32_t first_slice,
                                                uint32_t slice_count, uint8_t *out_texels, RBTMATH_ERROR *error)
{
    clear(error);
    if (kind != RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY && kind != RBTMATH_NOISE_VOLUME_KIND_WORLEY)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "kind, %u, is not one of RBTMATH_NOISE_VOLUME_KIND's", (unsigned)kind);
    if (size < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "size is 0");
    if (size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "size, %u, is above RBTMATH_NOISE_VOLUME_MAX_SIZE (%u)", size,
                    RBTMATH_NOISE_VOLUME_MAX_SIZE);
    if (first_slice >= size)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "first_slice, %u, is not below size, %u", first_slice, size);
    if (slice_count < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "slice_count is 0");
    if (slice_count > size - first_slice)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "slice_count, %u, is above size - first_slice, %u", slice_count, size - first_slice);
    if (!out_texels)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_texels is NULL");

    const bool perlin_worley = kind == RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY;
    // The cellular frequencies: 4 to 64 for PERLIN_WORLEY, 2 to 32 for WORLEY.
    const uint32_t lowest = perlin_worley ? 4u : 2u;
    RBTMATH_NOISE_DESC cellular[VOLUME_CELLULAR_FREQUENCIES];
    Octave cellular_table[VOLUME_CELLULAR_FREQUENCIES];
    for (uint32_t k = 0; k < VOLUME_CELLULAR_FREQUENCIES; k++)
    {
        cellular[k] = volume_cellular_desc(seed, lowest << k);
        make_octaves(cellular[k], &cellular_table[k]);
    }
    const RBTMATH_NOISE_DESC gradient = volume_gradient_desc(seed);
    Octave gradient_table[4];
    make_octaves(gradient, gradient_table);

    double places[RBTMATH_NOISE_VOLUME_MAX_SIZE][3];
    float w[VOLUME_CELLULAR_FREQUENCIES][RBTMATH_NOISE_VOLUME_MAX_SIZE];
    float P[RBTMATH_NOISE_VOLUME_MAX_SIZE];
    for (uint32_t z = first_slice; z < first_slice + slice_count; z++)
    {
        for (uint32_t y = 0; y < size; y++)
        {
            for (uint32_t x = 0; x < size; x++)
            {
                places[x][0] = ((double)x + 0.5) / (double)size;
                places[x][1] = ((double)y + 0.5) / (double)size;
                places[x][2] = ((double)z + 0.5) / (double)size;
            }
            for (uint32_t k = 0; k < VOLUME_CELLULAR_FREQUENCIES; k++)
                for (uint32_t x = 0; x < size; x++)
                {
                    float F1 = 0.0f;
                    place_value<3>(cellular[k], &cellular_table[k], places[x], F1, nullptr);
                    w[k][x] = 1.0f - (F1 < 1.0f ? F1 : 1.0f);
                }
            if (perlin_worley)
                for (uint32_t x = 0; x < size; x++)
                    place_value<3>(gradient, gradient_table, places[x], P[x], nullptr);
            uint8_t *row = out_texels + (((size_t)(z - first_slice) * size + y) * size) * 4;
            for (uint32_t x = 0; x < size; x++)
            {
                // V for the kind's lowest f, then 2f and 4f, from W at f to 16f.
                const float v1 = worley_fbm(w[0][x], w[1][x], w[2][x]);
                const float v2 = worley_fbm(w[1][x], w[2][x], w[3][x]);
                const float v4 = worley_fbm(w[2][x], w[3][x], w[4][x]);
                uint8_t *t     = row + (size_t)x * 4;
                if (perlin_worley)
                {
                    const float p = sat((P[x] * 0x1.111112p-2f) + 0.5f);
                    t[0]          = to_byte(sat(v1 + (p * (1.0f - v1))));
                    t[1]          = to_byte(v1);
                    t[2]          = to_byte(v2);
                    t[3]          = to_byte(v4);
                }
                else
                {
                    t[0] = to_byte(v1);
                    t[1] = to_byte(v2);
                    t[2] = to_byte(v4);
                    t[3] = 0;
                }
            }
        }
    }
    return RBT_RESULT_OK;
}

extern "C" RBT_RESULT rbtmath_noise_volume_sample(const uint8_t *texels, uint32_t size, const float *coordinates, uint32_t count,
                                                  float *out_values, RBTMATH_ERROR *error)
{
    clear(error);
    if (!texels && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "texels is NULL with a count of %u", count);
    if (size < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "size is 0");
    if (size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "size, %u, is above RBTMATH_NOISE_VOLUME_MAX_SIZE (%u)", size,
                    RBTMATH_NOISE_VOLUME_MAX_SIZE);
    if (!coordinates && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "coordinates is NULL with a count of %u", count);
    if (!out_values && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_values is NULL with a count of %u", count);
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t a = 0; a < 3; a++)
        {
            const float q = coordinates[(size_t)i * 3 + a];
            if (!std::isfinite(q))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "coordinate %u's axis %u is not finite", i, a);
            if (std::fabs(q) > RBTMATH_NOISE_VOLUME_MAX_COORDINATE)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "coordinate %u's axis %u, %g, is past RBTMATH_NOISE_VOLUME_MAX_COORDINATE", i, a,
                            (double)q);
        }
    for (uint32_t i = 0; i < count; i++)
        volume_read(texels, size, coordinates + (size_t)i * 3, out_values + (size_t)i * 4);
    return RBT_RESULT_OK;
}
