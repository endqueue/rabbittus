#include "math/rbtmathfog.h"
#include "math/rbtmathnoise.h"
#include "rbtmathfunctions_internal.hpp"
#include "rbtmathnoise_internal.hpp"
#include <cmath>
#include <cstdarg>
#include <stdio.h>

// RbtMath's fog (rbtmathfog.h): a height fog's density and its transmittance
// along a segment (a plane's closed, a sphere's by the Chapman function's
// large-x form), and a fog volume's density over a baked WORLEY
// volume (rbtmathnoise.h's read, rbtnoise::volume_read). Every step in the
// order of the header's Definition. Nothing allocates.
//
// Built without floating-point contraction (CMakeLists.txt, by the
// compiler's ID), its exponential rbtmathfunctions.h's (rbtfn::exp), its
// only libm calls floor and sqrt: the same bits everywhere.

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

bool finite_xyz(const float *v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// ---- The fog ------------------------------------------------------------------

RBT_RESULT check_fog(const RBTMATH_HEIGHT_FOG &f, RBTMATH_ERROR *error)
{
    if (f.kind != RBTMATH_FOG_HEIGHT_KIND_PLANE && f.kind != RBTMATH_FOG_HEIGHT_KIND_SPHERE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's kind, %u, is not one of RBTMATH_FOG_HEIGHT_KIND's", (unsigned)f.kind);
    if (f.kind == RBTMATH_FOG_HEIGHT_KIND_PLANE)
    {
        if (!finite_xyz(f.up))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's up is not finite");
        const double length = std::sqrt((double)f.up[0] * f.up[0] + (double)f.up[1] * f.up[1] + (double)f.up[2] * f.up[2]);
        if (!(std::fabs(length - 1.0) <= 1e-4))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's up's length, %.9g, is not within 1e-4 of 1", length);
    }
    else
    {
        if (!(f.up[0] == 0.0f && f.up[1] == 0.0f && f.up[2] == 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's up is not 0 for a sphere, which doesn't read it");
    }
    if (!std::isfinite(f.base))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's base is not finite");
    if (f.kind == RBTMATH_FOG_HEIGHT_KIND_SPHERE)
    {
        if (!(f.base > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's base, a sphere's radius, %g, is not above 0", (double)f.base);
    }
    if (!std::isfinite(f.density))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's density is not finite");
    if (!(f.density >= 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's density, %g, is below 0", (double)f.density);
    if (!std::isfinite(f.scale_height))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's scale_height is not finite");
    if (!(f.scale_height > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the fog's scale_height, %g, is not above 0", (double)f.scale_height);
    return RBT_RESULT_OK;
}

// E(x) = (float)exp((double)x), the Definition's.
inline float E(float x) { return (float)rbtfn::exp((double)x); }
inline double Ed(double x) { return (double)(float)rbtfn::exp(x); }

// HEIGHT, in float.
inline float height_of(const RBTMATH_HEIGHT_FOG &f, const float *p)
{
    if (f.kind == RBTMATH_FOG_HEIGHT_KIND_PLANE)
        return (((p[0] * f.up[0]) + (p[1] * f.up[1])) + (p[2] * f.up[2])) - f.base;
    return std::sqrt(((p[0] * p[0]) + (p[1] * p[1])) + (p[2] * p[2])) - f.base;
}

// DENSITY.
inline float density_of(const RBTMATH_HEIGHT_FOG &f, float y)
{
    if (y < 0.0f)
        return f.density;
    return f.density * E(-(y / f.scale_height));
}

// g(x) = (1 - E(-x)) / x, its series below 2^-4 (Horner's, from the highest term).
inline double g_of(double x)
{
    if (std::fabs(x) >= 0x1p-4)
        return (1.0 - Ed(-x)) / x;
    return ((((x / 120.0 - 1.0 / 24.0) * x + 1.0 / 6.0) * x - 0.5) * x) + 1.0;
}

// A plane's part above the base, from height y0 to y1 over length l.
inline double plane_part(const RBTMATH_HEIGHT_FOG &f, double y0, double y1, double l)
{
    const double s = (double)f.scale_height;
    const double x = (y1 - y0) / s;
    return (double)f.density * l * Ed(-(y0 / s)) * g_of(x);
}

double plane_tau(const RBTMATH_HEIGHT_FOG &f, const float *a, const float *b)
{
    const double d[3] = {(double)b[0] - (double)a[0], (double)b[1] - (double)a[1], (double)b[2] - (double)a[2]};
    const double L    = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (L == 0.0)
        return 0.0;
    const double ya = (double)height_of(f, a);
    const double yb = (double)height_of(f, b);
    const bool a_below = ya < 0.0;
    const bool b_below = yb < 0.0;
    if (a_below && b_below)
        return (double)f.density * L;
    if (!a_below && !b_below)
        return plane_part(f, ya, yb, L);
    const double tc    = ya / (ya - yb);
    const double l_a   = L * tc;
    const double l_b   = L - l_a;
    // The part from a to the crossing, then from the crossing to b.
    const double tau_a = a_below ? (double)f.density * l_a : plane_part(f, ya, 0.0, l_a);
    const double tau_b = b_below ? (double)f.density * l_b : plane_part(f, 0.0, yb, l_b);
    return tau_a + tau_b;
}

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_OVER_SQRT_PI = 1.1283791670955126; // 2 / sqrt(pi)
constexpr double SQRT_PI          = 1.7724538509055159;

// erfcx(y) = exp(y^2) erfc(y) for y at least 0 (the header's Definition,
// ERFCX): below 2, exp(y^2) (1 - erf(y)) by erf's Maclaurin series to its
// 40th term; from 2, Laplace's continued fraction to 60 terms, worked from
// the bottom. Within 1e-8 relative everywhere (measured against libm's
// erfc, 2026-11-18).
double erfcx(double y)
{
    if (y < 2.0)
    {
        double t = y;
        double s = 0.0;
        for (int n = 0; n <= 40; n++)
        {
            s = s + t / (double)(2 * n + 1);
            t = t * (-(y * y) / (double)(n + 1));
        }
        return rbtfn::exp(y * y) * (1.0 - TWO_OVER_SQRT_PI * s);
    }
    double f = y;
    for (int k = 60; k >= 1; k--)
        f = y + ((double)k * 0.5) / f;
    return 1.0 / (SQRT_PI * f);
}

// The optical depth from a point at radius r outwards to infinity, at the
// cosine mu (at least 0) between its radius and its way: the Chapman
// function's large-x form, Ch(x, mu) = sqrt(pi x / 2) erfcx(sqrt(x / 2) mu),
// x = r / scale_height.
inline double outward_depth(const RBTMATH_HEIGHT_FOG &f, double r, double mu)
{
    const double s = (double)f.scale_height;
    const double x = r / s;
    if (mu < 0.0)
        mu = 0.0;
    if (mu > 1.0)
        mu = 1.0;
    const double ch = std::sqrt(PI * x / 2.0) * erfcx(std::sqrt(x / 2.0) * mu);
    return (double)f.density * s * rbtfn::exp(-(r - (double)f.base) / s) * ch;
}

struct Point
{
    double p[3];
};

inline Point along(const double a[3], const double d[3], double t)
{
    return Point{{a[0] + t * d[0], a[1] + t * d[1], a[2] + t * d[2]}};
}

inline double radius_of(const Point &q)
{
    return std::sqrt(q.p[0] * q.p[0] + q.p[1] * q.p[1] + q.p[2] * q.p[2]);
}

// A sphere's part between parameters t0 and t1 of the segment a + t d (d
// the whole segment), its radius monotonic and on one side of the base.
double sphere_part(const RBTMATH_HEIGHT_FOG &f, const double a[3], const double d[3], double L, double t0, double t1)
{
    const double l = (t1 - t0) * L;
    if (!(l > 0.0))
        return 0.0;
    const Point q0 = along(a, d, t0);
    const Point q1 = along(a, d, t1);
    const double r0 = radius_of(q0);
    const double r1 = radius_of(q1);
    // Below the base: the middle's radius says which side the part lies on.
    const Point qm = along(a, d, 0.5 * (t0 + t1));
    if (radius_of(qm) < (double)f.base)
        return (double)f.density * l;
    // Oriented outward: from the smaller radius to the larger.
    const Point &lo  = r0 <= r1 ? q0 : q1;
    const Point &hi  = r0 <= r1 ? q1 : q0;
    const double rl  = r0 <= r1 ? r0 : r1;
    const double rh  = r0 <= r1 ? r1 : r0;
    const double way = r0 <= r1 ? 1.0 : -1.0;
    const double w[3] = {way * d[0] / L, way * d[1] / L, way * d[2] / L};
    const double mu_l = rl > 0.0 ? (lo.p[0] * w[0] + lo.p[1] * w[1] + lo.p[2] * w[2]) / rl : 1.0;
    const double mu_h = rh > 0.0 ? (hi.p[0] * w[0] + hi.p[1] * w[1] + hi.p[2] * w[2]) / rh : 1.0;
    const double tau  = outward_depth(f, rl, mu_l) - outward_depth(f, rh, mu_h);
    return tau > 0.0 ? tau : 0.0;
}

double sphere_tau(const RBTMATH_HEIGHT_FOG &f, const float *a_f, const float *b_f)
{
    const double a[3] = {(double)a_f[0], (double)a_f[1], (double)a_f[2]};
    const double d[3] = {(double)b_f[0] - a[0], (double)b_f[1] - a[1], (double)b_f[2] - a[2]};
    const double LL   = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (LL == 0.0)
        return 0.0;
    const double L = std::sqrt(LL);
    // The splits, in increasing order: the closest point, the base's crossings.
    double cuts[5];
    int n          = 0;
    cuts[n++]      = 0.0;
    const double ad = a[0] * d[0] + a[1] * d[1] + a[2] * d[2];
    const double tm = -ad / LL;
    double inner[3];
    int m = 0;
    if (tm > 0.0 && tm < 1.0)
        inner[m++] = tm;
    // |a + t d|^2 = R^2: LL t^2 + 2 ad t + (aa - R^2) = 0.
    const double R    = (double)f.base;
    const double aa   = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
    const double disc = ad * ad - LL * (aa - R * R);
    if (disc > 0.0)
    {
        const double root = std::sqrt(disc);
        // The stable pair of roots.
        const double qq = -(ad + (ad >= 0.0 ? root : -root));
        double t1 = qq / LL;
        double t2 = qq != 0.0 ? (aa - R * R) / qq : t1;
        if (t1 > t2)
        {
            const double t = t1;
            t1             = t2;
            t2             = t;
        }
        if (t1 > 0.0 && t1 < 1.0)
            inner[m++] = t1;
        if (t2 > 0.0 && t2 < 1.0)
            inner[m++] = t2;
    }
    // Sort the inner cuts (at most three).
    for (int i = 1; i < m; i++)
        for (int j = i; j > 0 && inner[j - 1] > inner[j]; j--)
        {
            const double t = inner[j];
            inner[j]       = inner[j - 1];
            inner[j - 1]   = t;
        }
    for (int i = 0; i < m; i++)
        cuts[n++] = inner[i];
    cuts[n++] = 1.0;
    double tau = 0.0;
    for (int i = 0; i + 1 < n; i++)
        tau = tau + sphere_part(f, a, d, L, cuts[i], cuts[i + 1]);
    return tau;
}

// ---- Fog volumes ----------------------------------------------------------------

RBT_RESULT check_volume(const RBTMATH_FOG_VOLUME &v, RBTMATH_ERROR *error)
{
    if (v.shape != RBTMATH_FOG_VOLUME_SHAPE_BOX && v.shape != RBTMATH_FOG_VOLUME_SHAPE_SPHERE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's shape, %u, is not one of RBTMATH_FOG_VOLUME_SHAPE's", (unsigned)v.shape);
    const int read = v.shape == RBTMATH_FOG_VOLUME_SHAPE_BOX ? 3 : 1;
    for (int a = 0; a < 3; a++)
    {
        if (!std::isfinite(v.half_extents[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's half_extents[%d] is not finite", a);
        if (a < read)
        {
            if (!(v.half_extents[a] > 0.0f))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's half_extents[%d], %g, is not above 0", a, (double)v.half_extents[a]);
        }
        else
        {
            if (v.half_extents[a] != 0.0f)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's half_extents[%d] is not 0 for a sphere, which doesn't read it", a);
        }
    }
    float smallest = v.half_extents[0];
    for (int a = 1; a < read; a++)
        smallest = v.half_extents[a] < smallest ? v.half_extents[a] : smallest;
    if (!std::isfinite(v.edge))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's edge is not finite");
    if (!(v.edge >= 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's edge, %g, is below 0", (double)v.edge);
    if (!(v.edge <= smallest))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's edge, %g, is above its smallest half size, %g", (double)v.edge, (double)smallest);
    if (!std::isfinite(v.density))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's density is not finite");
    if (!(v.density >= 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's density, %g, is below 0", (double)v.density);
    if (!std::isfinite(v.noise_strength))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's noise_strength is not finite");
    if (!(v.noise_strength >= 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's noise_strength, %g, is below 0", (double)v.noise_strength);
    if (!(v.noise_strength <= 1.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's noise_strength, %g, is above 1", (double)v.noise_strength);
    if (v.noise_strength > 0.0f)
    {
        if (!std::isfinite(v.noise_tile))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's noise_tile is not finite");
        if (!(v.noise_tile > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's noise_tile, %g, is not above 0 with noise", (double)v.noise_tile);
    }
    else
    {
        if (v.noise_tile != 0.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the volume's noise_tile is not 0 with a noise_strength of 0, which doesn't read it");
    }
    return RBT_RESULT_OK;
}

// The inside distance i (the Definition's).
inline float inside_of(const RBTMATH_FOG_VOLUME &v, const float *p)
{
    if (v.shape == RBTMATH_FOG_VOLUME_SHAPE_BOX)
    {
        float i = v.half_extents[0] - std::fabs(p[0]);
        const float i1 = v.half_extents[1] - std::fabs(p[1]);
        const float i2 = v.half_extents[2] - std::fabs(p[2]);
        i = i1 < i ? i1 : i;
        i = i2 < i ? i2 : i;
        return i;
    }
    return v.half_extents[0] - std::sqrt(((p[0] * p[0]) + (p[1] * p[1])) + (p[2] * p[2]));
}

inline void noise_coordinate(const RBTMATH_FOG_VOLUME &v, const float *offset, const float *p, float q[3])
{
    for (int a = 0; a < 3; a++)
        q[a] = (p[a] / v.noise_tile) + offset[a];
}

} // namespace

extern "C" RBT_RESULT rbtmath_height_fog_compute_density(const RBTMATH_HEIGHT_FOG *fog, const float *points, uint32_t count, float *out_density,
                                                         RBTMATH_ERROR *error)
{
    clear(error);
    if (!fog)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "fog is NULL");
    const RBT_RESULT checked = check_fog(*fog, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    if (!points && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (!out_density && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_density is NULL with a count of %u", count);
    for (uint32_t i = 0; i < count; i++)
    {
        if (!finite_xyz(points + (size_t)i * 3))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "point %u is not finite", i);
    }
    for (uint32_t i = 0; i < count; i++)
        out_density[i] = density_of(*fog, height_of(*fog, points + (size_t)i * 3));
    return RBT_RESULT_OK;
}

extern "C" RBT_RESULT rbtmath_height_fog_compute_transmittance(const RBTMATH_HEIGHT_FOG *fog, const float *starts, const float *ends,
                                                               uint32_t count, float *out_transmittance, RBTMATH_ERROR *error)
{
    clear(error);
    if (!fog)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "fog is NULL");
    const RBT_RESULT checked = check_fog(*fog, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    if (!starts && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "starts is NULL with a count of %u", count);
    if (!ends && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "ends is NULL with a count of %u", count);
    if (!out_transmittance && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_transmittance is NULL with a count of %u", count);
    for (uint32_t i = 0; i < count; i++)
    {
        if (!finite_xyz(starts + (size_t)i * 3))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "start %u is not finite", i);
        if (!finite_xyz(ends + (size_t)i * 3))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "end %u is not finite", i);
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const float *a   = starts + (size_t)i * 3;
        const float *b   = ends + (size_t)i * 3;
        const double tau = fog->kind == RBTMATH_FOG_HEIGHT_KIND_PLANE ? plane_tau(*fog, a, b) : sphere_tau(*fog, a, b);
        out_transmittance[i] = E(-(float)tau);
    }
    return RBT_RESULT_OK;
}

extern "C" RBT_RESULT rbtmath_fog_volume_compute_density(const RBTMATH_FOG_VOLUME *volume, const uint8_t *noise, uint32_t noise_size,
                                                         const float noise_offset[3], const float *points, uint32_t count, float *out_density,
                                                         RBTMATH_ERROR *error)
{
    clear(error);
    if (!volume)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "volume is NULL");
    const RBT_RESULT checked = check_volume(*volume, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    const bool noisy = volume->noise_strength > 0.0f;
    if (noisy)
    {
        if (!noise)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise is NULL with a noise_strength above 0");
        if (noise_size < 1)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise_size is 0 with noise");
        if (noise_size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise_size, %u, is above RBTMATH_NOISE_VOLUME_MAX_SIZE (%u)", noise_size,
                        RBTMATH_NOISE_VOLUME_MAX_SIZE);
        if (!noise_offset)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise_offset is NULL with noise");
        for (int a = 0; a < 3; a++)
        {
            if (!std::isfinite(noise_offset[a]))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise_offset[%d] is not finite", a);
            if (std::fabs(noise_offset[a]) > RBTMATH_NOISE_VOLUME_MAX_COORDINATE)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise_offset[%d], %g, is past RBTMATH_NOISE_VOLUME_MAX_COORDINATE", a,
                            (double)noise_offset[a]);
        }
    }
    else
    {
        if (noise)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise is given with a noise_strength of 0, which doesn't read it");
        if (noise_size != 0)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "noise_size, %u, is not 0 with no noise", noise_size);
    }
    if (!points && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (!out_density && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_density is NULL with a count of %u", count);
    for (uint32_t i = 0; i < count; i++)
    {
        const float *p = points + (size_t)i * 3;
        if (!finite_xyz(p))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "point %u is not finite", i);
        if (noisy && inside_of(*volume, p) > 0.0f)
        {
            float q[3];
            noise_coordinate(*volume, noise_offset, p, q);
            for (int a = 0; a < 3; a++)
                if (!(std::fabs(q[a]) <= RBTMATH_NOISE_VOLUME_MAX_COORDINATE))
                    return fail(error, RBT_RESULT_INVALID_ARGUMENT,
                                "point %u is read at a noise coordinate past RBTMATH_NOISE_VOLUME_MAX_COORDINATE on axis %d", i, a);
        }
    }
    const RBTMATH_FOG_VOLUME &v = *volume;
    for (uint32_t i = 0; i < count; i++)
    {
        const float *p = points + (size_t)i * 3;
        const float in = inside_of(v, p);
        if (!(in > 0.0f))
        {
            out_density[i] = 0.0f;
            continue;
        }
        float f = 1.0f;
        if (v.edge > 0.0f)
        {
            f = in / v.edge;
            f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        }
        float n = 1.0f;
        if (noisy)
        {
            float q[3], s[4];
            noise_coordinate(v, noise_offset, p, q);
            rbtnoise::volume_read(noise, noise_size, q, s);
            const float w = ((s[0] * 0.625f) + (s[1] * 0.25f)) + (s[2] * 0.125f);
            n             = 1.0f + (v.noise_strength * (w - 1.0f));
        }
        out_density[i] = (v.density * f) * n;
    }
    return RBT_RESULT_OK;
}
