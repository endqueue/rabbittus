#include "math/rbtmathmedium.h"
#include <cmath>
#include <cstdarg>
#include <stdio.h>

// A layered medium in a spherical shell (rbtmathmedium.h): the densities,
// the transmittance along a ray and the phase functions, the model Rend's
// atmosphere shaders work out too (shaders/rbtrend_atmosphere_model.slang
// names each of these as its counterpart).
//
// Built without floating-point contraction (CMakeLists.txt), and with an
// exponential of its own (portable_exp), so every call gives the same bits on
// every platform. Everything is worked out in doubles and written as floats.

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

// ---- The exponential ----------------------------------------------------------

// e^x the same bits everywhere: x = k ln 2 + r with |r| <= ln 2 / 2 (ln 2
// split in two so k ln 2 is exact), e^r by its Taylor series to the 13th
// power (the first term left out is under 2^-60 of the sum there), then
// scaled by 2^k exactly. Only additions, multiplications, a floor and
// ldexp, each IEEE's, with contraction off: no platform's libm.
constexpr double ln2_hi   = 6.93147180369123816490e-01;
constexpr double ln2_lo   = 1.90821492927058770002e-10;
constexpr double log2_e   = 1.44269504088896338700e+00;
constexpr double exp_low  = -745.0; // below it e^x is 0 in a double
constexpr double exp_high = 709.0;  // above it, past a double's range

double portable_exp(double x)
{
    if (x < exp_low)
        return 0.0;
    if (x > exp_high)
        x = exp_high;
    const double k = std::floor(x * log2_e + 0.5);
    const double r = (x - k * ln2_hi) - k * ln2_lo;
    double term    = 1.0;
    double sum     = 1.0;
    for (int n = 1; n <= 13; ++n)
    {
        term = term * r / (double)n;
        sum  = sum + term;
    }
    return std::ldexp(sum, (int)k);
}

// ---- The medium ---------------------------------------------------------------

// Whether all three of a point's or a direction's coordinates are finite:
// one condition, so a check of a vector is one if.
bool finite_xyz(const float *v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool is_zero_xyz(const float *v)
{
    return v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f;
}

bool finite3(const float v[3])
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool at_least(const float v[3], float least)
{
    return v[0] >= least && v[1] >= least && v[2] >= least;
}

// Checks a medium field by field in the struct's order, each its own check.
RBT_RESULT check_medium(const RBTMATH_SHELL_MEDIUM &m, RBTMATH_ERROR *error)
{
    if (!std::isfinite(m.bottom_radius))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's bottom_radius is not finite");
    if (!(m.bottom_radius > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's bottom_radius, %g, is not above 0", (double)m.bottom_radius);
    if (!std::isfinite(m.top_radius))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's top_radius is not finite");
    if (!(m.top_radius > m.bottom_radius))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's top_radius, %g, is not above its bottom_radius, %g", (double)m.top_radius,
                    (double)m.bottom_radius);
    if (!finite3(m.rayleigh_scattering))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's rayleigh_scattering is not finite");
    if (!at_least(m.rayleigh_scattering, 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's rayleigh_scattering is below 0");
    if (!std::isfinite(m.rayleigh_scale_height))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's rayleigh_scale_height is not finite");
    if (!(m.rayleigh_scale_height > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's rayleigh_scale_height, %g, is not above 0", (double)m.rayleigh_scale_height);
    if (!finite3(m.mie_scattering))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_scattering is not finite");
    if (!at_least(m.mie_scattering, 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_scattering is below 0");
    if (!finite3(m.mie_extinction))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_extinction is not finite");
    for (int c = 0; c < 3; ++c)
    {
        if (!(m.mie_extinction[c] >= m.mie_scattering[c]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_extinction[%d], %g, is below its mie_scattering[%d], %g", c,
                        (double)m.mie_extinction[c], c, (double)m.mie_scattering[c]);
    }
    if (!std::isfinite(m.mie_scale_height))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_scale_height is not finite");
    if (!(m.mie_scale_height > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_scale_height, %g, is not above 0", (double)m.mie_scale_height);
    if (!std::isfinite(m.mie_anisotropy))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_anisotropy is not finite");
    if (!(m.mie_anisotropy >= -0.999f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_anisotropy, %g, is below -0.999", (double)m.mie_anisotropy);
    if (!(m.mie_anisotropy <= 0.999f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's mie_anisotropy, %g, is above 0.999", (double)m.mie_anisotropy);
    if (!finite3(m.absorption_extinction))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's absorption_extinction is not finite");
    if (!at_least(m.absorption_extinction, 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's absorption_extinction is below 0");
    if (!std::isfinite(m.absorption_center_height))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's absorption_center_height is not finite");
    if (!std::isfinite(m.absorption_half_width))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's absorption_half_width is not finite");
    if (!(m.absorption_half_width > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's absorption_half_width, %g, is not above 0", (double)m.absorption_half_width);
    if (!finite3(m.ground_albedo))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's ground_albedo is not finite");
    if (!at_least(m.ground_albedo, 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's ground_albedo is below 0");
    for (int c = 0; c < 3; ++c)
    {
        if (!(m.ground_albedo[c] <= 1.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the medium's ground_albedo[%d], %g, is above 1", c, (double)m.ground_albedo[c]);
    }
    return RBT_RESULT_OK;
}

// The three layers' densities at a height above the bottom; 0 outside.
void densities_at(const RBTMATH_SHELL_MEDIUM &m, double radius, double out[3])
{
    const double h = radius - (double)m.bottom_radius;
    if (h < 0.0 || radius > (double)m.top_radius)
    {
        out[0] = 0.0;
        out[1] = 0.0;
        out[2] = 0.0;
        return;
    }
    out[0]              = portable_exp(-h / (double)m.rayleigh_scale_height);
    out[1]              = portable_exp(-h / (double)m.mie_scale_height);
    const double tent   = 1.0 - std::fabs(h - (double)m.absorption_center_height) / (double)m.absorption_half_width;
    out[2]              = tent > 0.0 ? tent : 0.0;
}

double length3(const double v[3])
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Where the ray o + t d (d unit) crosses a sphere of radius r round the
// origin: its two distances, false if it misses.
bool sphere_hits(const double o[3], const double d[3], double r, double *near_t, double *far_t)
{
    const double b    = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
    const double c    = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - r * r;
    const double disc = b * b - c;
    if (disc < 0.0)
        return false;
    const double root = std::sqrt(disc);
    *near_t           = -b - root;
    *far_t            = -b + root;
    return true;
}

// The transmittance along one ray (origin o, unit direction d).
void transmittance_along(const RBTMATH_SHELL_MEDIUM &m, const double o[3], const double d[3], uint32_t steps, double out[3])
{
    out[0] = 1.0;
    out[1] = 1.0;
    out[2] = 1.0;
    double top_near = 0.0, top_far = 0.0;
    if (!sphere_hits(o, d, (double)m.top_radius, &top_near, &top_far) || top_far <= 0.0)
        return;
    double start = top_near > 0.0 ? top_near : 0.0;
    double end   = top_far;
    double ground_near = 0.0, ground_far = 0.0;
    if (sphere_hits(o, d, (double)m.bottom_radius, &ground_near, &ground_far) && ground_far > 0.0)
    {
        // Into the ground (or starting under it): nothing gets through.
        out[0] = 0.0;
        out[1] = 0.0;
        out[2] = 0.0;
        return;
    }
    const double step = (end - start) / (double)steps;
    double depth[3]   = {0.0, 0.0, 0.0};
    for (uint32_t i = 0; i < steps; ++i)
    {
        const double t    = start + ((double)i + 0.5) * step;
        const double p[3] = {o[0] + t * d[0], o[1] + t * d[1], o[2] + t * d[2]};
        double rho[3];
        densities_at(m, length3(p), rho);
        for (int c = 0; c < 3; ++c)
        {
            const double extinction = (double)m.rayleigh_scattering[c] * rho[0] + (double)m.mie_extinction[c] * rho[1] +
                                      (double)m.absorption_extinction[c] * rho[2];
            depth[c] = depth[c] + extinction * step;
        }
    }
    for (int c = 0; c < 3; ++c)
        out[c] = portable_exp(-depth[c]);
}

constexpr double pi = 3.14159265358979323846;

} // namespace

RBT_RESULT rbtmath_shell_medium_compute_density(const RBTMATH_SHELL_MEDIUM *medium, const float *points, uint32_t count, float *out_density,
                                        RBTMATH_ERROR *error)
{
    clear(error);
    if (!medium)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "medium is NULL");
    if (!points && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (!out_density && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_density is NULL with a count of %u", count);
    const RBT_RESULT checked = check_medium(*medium, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!finite_xyz(points + i * 3))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "point %u is not finite", i);
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const double p[3] = {(double)points[i * 3 + 0], (double)points[i * 3 + 1], (double)points[i * 3 + 2]};
        double rho[3];
        densities_at(*medium, length3(p), rho);
        for (int c = 0; c < 3; ++c)
            out_density[i * 3 + c] = (float)rho[c];
    }
    return RBT_RESULT_OK;
}

RBT_RESULT rbtmath_shell_medium_compute_transmittance(const RBTMATH_SHELL_MEDIUM *medium, const float *origins, const float *directions, uint32_t count,
                                              uint32_t steps, float *out_transmittance, RBTMATH_ERROR *error)
{
    clear(error);
    if (!medium)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "medium is NULL");
    if (!origins && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "origins is NULL with a count of %u", count);
    if (!directions && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "directions is NULL with a count of %u", count);
    if (steps < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "steps is 0");
    if (steps > RBTMATH_SHELL_MEDIUM_MAX_STEPS)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "steps, %u, is above %u", steps, RBTMATH_SHELL_MEDIUM_MAX_STEPS);
    if (!out_transmittance && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_transmittance is NULL with a count of %u", count);
    const RBT_RESULT checked = check_medium(*medium, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    for (uint32_t i = 0; i < count; ++i)
    {
        const float *o = origins + i * 3;
        const float *d = directions + i * 3;
        if (!finite_xyz(o))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "origin %u is not finite", i);
        if (!finite_xyz(d))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "direction %u is not finite", i);
        if (is_zero_xyz(d))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "direction %u is 0", i);
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const double o[3]   = {(double)origins[i * 3 + 0], (double)origins[i * 3 + 1], (double)origins[i * 3 + 2]};
        double d[3]         = {(double)directions[i * 3 + 0], (double)directions[i * 3 + 1], (double)directions[i * 3 + 2]};
        const double length = length3(d);
        for (int c = 0; c < 3; ++c)
            d[c] = d[c] / length;
        double t[3];
        transmittance_along(*medium, o, d, steps, t);
        for (int c = 0; c < 3; ++c)
            out_transmittance[i * 3 + c] = (float)t[c];
    }
    return RBT_RESULT_OK;
}

RBT_RESULT rbtmath_shell_medium_compute_phase(const RBTMATH_SHELL_MEDIUM *medium, const float *cosines, uint32_t count, float *out_phase, RBTMATH_ERROR *error)
{
    clear(error);
    if (!medium)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "medium is NULL");
    if (!cosines && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "cosines is NULL with a count of %u", count);
    if (!out_phase && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_phase is NULL with a count of %u", count);
    const RBT_RESULT checked = check_medium(*medium, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!(cosines[i] >= -1.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "cosine %u is below -1 or not a number", i);
        if (!(cosines[i] <= 1.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "cosine %u is above 1", i);
    }
    const double g  = (double)medium->mie_anisotropy;
    const double g2 = g * g;
    for (uint32_t i = 0; i < count; ++i)
    {
        const double c   = (double)cosines[i];
        const double c2  = c * c;
        const double ray = 3.0 / (16.0 * pi) * (1.0 + c2);
        // Cornette and Shanks: 3 (1 - g^2)(1 + c^2) / (8 pi (2 + g^2)(1 + g^2 - 2 g c)^(3/2)).
        const double base = 1.0 + g2 - 2.0 * g * c;
        const double mie  = 3.0 * (1.0 - g2) * (1.0 + c2) / (8.0 * pi * (2.0 + g2) * base * std::sqrt(base));
        out_phase[i * 2 + 0] = (float)ray;
        out_phase[i * 2 + 1] = (float)mie;
    }
    return RBT_RESULT_OK;
}
