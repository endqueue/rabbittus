#include "math/rbtmathclouds.h"
#include "math/rbtmathnoise.h"
#include "rbtmathfunctions_internal.hpp"
#include "rbtmathnoise_internal.hpp"
#include <cmath>
#include <cstdarg>
#include <stdio.h>

// RbtMath's cloud layers (rbtmathclouds.h): a layer's density over two baked
// volumes (rbtmathnoise.h's read, rbtnoise::volume_read) and a coverage map,
// every step in the order of the header's Definition. Nothing allocates.
//
// Built without floating-point contraction (CMakeLists.txt, by the
// compiler's ID), the equi-angular warp's arctangent rbtmathfunctions.h's
// (rbtfn::atan), its only libm calls floor and sqrt: the same bits
// everywhere.

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

double length_of(const float *v)
{
    return std::sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);
}

inline float sat(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// ---- Checks -------------------------------------------------------------------

RBT_RESULT check_shape(const RBTMATH_CLOUD_SHAPE &s, RBTMATH_ERROR *error)
{
    if (s.type_count < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's type_count is 0");
    if (s.type_count > RBTMATH_CLOUD_MAX_TYPES)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's type_count, %u, is above RBTMATH_CLOUD_MAX_TYPES (%u)", s.type_count,
                    RBTMATH_CLOUD_MAX_TYPES);
    for (uint32_t i = 0; i < 16; i++)
    {
        const float v = s.profiles[i];
        if (i >= s.type_count * 4)
        {
            if (v != 0.0f)
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's profiles[%u], %g, is not 0 past type_count's profiles", i, (double)v);
            continue;
        }
        if (!std::isfinite(v))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's profiles[%u] is not finite", i);
        if (!(v >= 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's profiles[%u], %g, is below 0", i, (double)v);
        if (!(v <= 1.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's profiles[%u], %g, is above 1", i, (double)v);
        if (i % 4 == 3)
        {
            const float *p = s.profiles + (i - 3);
            const uint32_t n = i / 4;
            if (!(p[0] < p[1]))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "profile %u's a, %g, is not below its b, %g", n, (double)p[0], (double)p[1]);
            if (!(p[1] <= p[2]))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "profile %u's b, %g, is above its c, %g", n, (double)p[1], (double)p[2]);
            if (!(p[2] < p[3]))
                return fail(error, RBT_RESULT_INVALID_ARGUMENT, "profile %u's c, %g, is not below its d, %g", n, (double)p[2], (double)p[3]);
        }
    }
    if (!std::isfinite(s.base_tile))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's base_tile is not finite");
    if (!(s.base_tile > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's base_tile, %g, is not above 0", (double)s.base_tile);
    if (!std::isfinite(s.detail_tile))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's detail_tile is not finite");
    if (!(s.detail_tile > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's detail_tile, %g, is not above 0", (double)s.detail_tile);
    if (!std::isfinite(s.erosion))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's erosion is not finite");
    if (!(s.erosion >= 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's erosion, %g, is below 0", (double)s.erosion);
    if (!(s.erosion <= 1.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's erosion, %g, is above 1", (double)s.erosion);
    if (!std::isfinite(s.billow_height))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's billow_height is not finite");
    if (!(s.billow_height > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's billow_height, %g, is not above 0", (double)s.billow_height);
    if (!(s.billow_height <= 1.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's billow_height, %g, is above 1", (double)s.billow_height);
    if (!std::isfinite(s.density_scale))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's density_scale is not finite");
    if (!(s.density_scale >= 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's density_scale, %g, is below 0", (double)s.density_scale);
    if (!std::isfinite(s.coverage_scale))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's coverage_scale is not finite");
    if (!std::isfinite(s.coverage_bias))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the shape's coverage_bias is not finite");
    return RBT_RESULT_OK;
}

RBT_RESULT check_unread_vector(const float *v, const char *name, RBTMATH_ERROR *error)
{
    for (int a = 0; a < 3; a++)
    {
        if (v[a] != 0.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' %s is not 0 for a shell, which doesn't read it", name);
    }
    return RBT_RESULT_OK;
}

RBT_RESULT check_bounds(const RBTMATH_CLOUD_BOUNDS &b, RBTMATH_ERROR *error)
{
    if (b.kind != RBTMATH_CLOUD_BOUNDS_KIND_SHELL && b.kind != RBTMATH_CLOUD_BOUNDS_KIND_SLAB)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' kind, %u, is not one of RBTMATH_CLOUD_BOUNDS_KIND's", (unsigned)b.kind);
    const bool shell = b.kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL;
    if (!std::isfinite(b.bottom))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' bottom is not finite");
    if (shell)
    {
        if (!(b.bottom > 0.0f))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' bottom, a shell's inner radius, %g, is not above 0", (double)b.bottom);
    }
    if (!std::isfinite(b.top))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' top is not finite");
    if (!(b.top > b.bottom))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' top, %g, is not above its bottom, %g", (double)b.top, (double)b.bottom);
    if (shell)
    {
        RBT_RESULT r = check_unread_vector(b.up, "up", error);
        if (r != RBT_RESULT_OK)
            return r;
        r = check_unread_vector(b.map_axis, "map_axis", error);
        if (r != RBT_RESULT_OK)
            return r;
        if (b.map_extent != 0.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_extent is not 0 for a shell, which doesn't read it");
        if (b.map_address != RBTMATH_CLOUD_MAP_ADDRESS_UNKNOWN)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_address is not UNKNOWN for a shell, which doesn't read it");
        if (b.cube_warp != RBTMATH_CLOUD_CUBE_WARP_PLAIN && b.cube_warp != RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' cube_warp, %u, is not PLAIN or EQUI_ANGULAR for a shell", (unsigned)b.cube_warp);
        return RBT_RESULT_OK;
    }
    if (!finite_xyz(b.up))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' up is not finite");
    const double up_length = length_of(b.up);
    if (!(std::fabs(up_length - 1.0) <= 1e-4))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' up's length, %.9g, is not within 1e-4 of 1", up_length);
    if (!finite_xyz(b.map_axis))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_axis is not finite");
    const double axis_length = length_of(b.map_axis);
    if (!(std::fabs(axis_length - 1.0) <= 1e-4))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_axis's length, %.9g, is not within 1e-4 of 1", axis_length);
    const double dot = (double)b.up[0] * b.map_axis[0] + (double)b.up[1] * b.map_axis[1] + (double)b.up[2] * b.map_axis[2];
    if (!(std::fabs(dot) <= 1e-4))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_axis is not square to up within 1e-4 (their dot %.9g)", dot);
    if (!std::isfinite(b.map_extent))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_extent is not finite");
    if (!(b.map_extent > 0.0f))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_extent, %g, is not above 0", (double)b.map_extent);
    if (b.map_address != RBTMATH_CLOUD_MAP_ADDRESS_REPEAT && b.map_address != RBTMATH_CLOUD_MAP_ADDRESS_CLAMP)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' map_address, %u, is not REPEAT or CLAMP for a slab", (unsigned)b.map_address);
    if (b.cube_warp != RBTMATH_CLOUD_CUBE_WARP_UNKNOWN)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the bounds' cube_warp is not UNKNOWN for a slab, which doesn't read it");
    return RBT_RESULT_OK;
}

RBT_RESULT check_texels(const RBTMATH_CLOUD_TEXELS &t, RBTMATH_ERROR *error)
{
    if (!t.base)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' base is NULL");
    if (t.base_size < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' base_size is 0");
    if (t.base_size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' base_size, %u, is above RBTMATH_NOISE_VOLUME_MAX_SIZE (%u)", t.base_size,
                    RBTMATH_NOISE_VOLUME_MAX_SIZE);
    if (!t.detail)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' detail is NULL");
    if (t.detail_size < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' detail_size is 0");
    if (t.detail_size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' detail_size, %u, is above RBTMATH_NOISE_VOLUME_MAX_SIZE (%u)",
                    t.detail_size, RBTMATH_NOISE_VOLUME_MAX_SIZE);
    if (!t.coverage)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' coverage is NULL");
    if (t.coverage_size < 1)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' coverage_size is 0");
    if (t.coverage_size > RBTMATH_CLOUD_MAX_COVERAGE_SIZE)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the texels' coverage_size, %u, is above RBTMATH_CLOUD_MAX_COVERAGE_SIZE (%u)",
                    t.coverage_size, RBTMATH_CLOUD_MAX_COVERAGE_SIZE);
    return RBT_RESULT_OK;
}

RBT_RESULT check_offsets(const RBTMATH_CLOUD_OFFSETS &o, bool shell, RBTMATH_ERROR *error)
{
    for (int a = 0; a < 3; a++)
    {
        if (!std::isfinite(o.base[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the offsets' base[%d] is not finite", a);
        if (!(std::fabs(o.base[a]) <= RBTMATH_NOISE_VOLUME_MAX_COORDINATE))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the offsets' base[%d], %g, is past RBTMATH_NOISE_VOLUME_MAX_COORDINATE", a,
                        (double)o.base[a]);
    }
    for (int a = 0; a < 3; a++)
    {
        if (!std::isfinite(o.detail[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the offsets' detail[%d] is not finite", a);
        if (!(std::fabs(o.detail[a]) <= RBTMATH_NOISE_VOLUME_MAX_COORDINATE))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the offsets' detail[%d], %g, is past RBTMATH_NOISE_VOLUME_MAX_COORDINATE", a,
                        (double)o.detail[a]);
    }
    for (int a = 0; a < 2; a++)
    {
        if (!std::isfinite(o.coverage[a]))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the offsets' coverage[%d] is not finite", a);
        if (shell && o.coverage[a] != 0.0f)
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "the offsets' coverage[%d] is not 0 for a shell, which doesn't read it", a);
    }
    return RBT_RESULT_OK;
}

// ---- The definition -------------------------------------------------------------

// Step 1: h, the height as a fraction of the thickness; r a shell's radius.
inline float height_of(const RBTMATH_CLOUD_BOUNDS &b, const float *p, float &r)
{
    float y;
    if (b.kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL)
    {
        r = std::sqrt(((p[0] * p[0]) + (p[1] * p[1])) + (p[2] * p[2]));
        y = r - b.bottom;
    }
    else
    {
        r = 0.0f;
        y = (((p[0] * b.up[0]) + (p[1] * b.up[1])) + (p[2] * b.up[2])) - b.bottom;
    }
    return y / (b.top - b.bottom);
}

inline bool inside(float h)
{
    return !(h < 0.0f) && !(h > 1.0f);
}

// The noise coordinates of steps 4 and 6.
inline void noise_coordinate(const float *p, float tile, const float *offset, float q[3])
{
    q[0] = (p[0] / tile) + offset[0];
    q[1] = (p[1] / tile) + offset[1];
    q[2] = (p[2] / tile) + offset[2];
}

inline uint32_t wrap_index(int64_t i, int64_t m)
{
    int64_t r = i % m;
    if (r < 0)
        r += m;
    return (uint32_t)r;
}

inline uint32_t clamp_index(int64_t i, int64_t m)
{
    return (uint32_t)(i < 0 ? 0 : (i > m - 1 ? m - 1 : i));
}

// A map face's bilinear read (step 2): x then y, three channels (A never read).
inline void map_read(const uint8_t *face, uint32_t m, float s, float t, bool repeat, float out[3])
{
    const float u   = (s * (float)m) - 0.5f;
    const float v   = (t * (float)m) - 0.5f;
    const int64_t iu = (int64_t)std::floor(u);
    const int64_t iv = (int64_t)std::floor(v);
    const float tu  = u - (float)iu;
    const float tv  = v - (float)iv;
    const int64_t mm = (int64_t)m;
    uint32_t x0, x1, y0, y1;
    if (repeat)
    {
        x0 = wrap_index(iu, mm);
        x1 = wrap_index(iu + 1, mm);
        y0 = wrap_index(iv, mm);
        y1 = wrap_index(iv + 1, mm);
    }
    else
    {
        x0 = clamp_index(iu, mm);
        x1 = clamp_index(iu + 1, mm);
        y0 = clamp_index(iv, mm);
        y1 = clamp_index(iv + 1, mm);
    }
    const uint8_t *c00 = face + ((size_t)y0 * m + x0) * 4;
    const uint8_t *c10 = face + ((size_t)y0 * m + x1) * 4;
    const uint8_t *c01 = face + ((size_t)y1 * m + x0) * 4;
    const uint8_t *c11 = face + ((size_t)y1 * m + x1) * 4;
    for (int c = 0; c < 3; c++)
    {
        const float a  = (float)c00[c] / 255.0f;
        const float b  = (float)c10[c] / 255.0f;
        const float d  = (float)c01[c] / 255.0f;
        const float e  = (float)c11[c] / 255.0f;
        const float r0 = a + (tu * (b - a));
        const float r1 = d + (tu * (e - d));
        out[c]         = r0 + (tv * (r1 - r0));
    }
}

inline float warp(float g, bool equi)
{
    if (!equi)
        return g;
    return (float)(rbtfn::atan((double)g) * 0x1.45f306dc9c883p+0);
}

// Step 2: the map's R, G, B at p.
inline void coverage_at(const RBTMATH_CLOUD_BOUNDS &b, const RBTMATH_CLOUD_TEXELS &t, const RBTMATH_CLOUD_OFFSETS &o, const float *p,
                        float out[3])
{
    const uint32_t m = t.coverage_size;
    if (b.kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL)
    {
        const float ax = std::fabs(p[0]), ay = std::fabs(p[1]), az = std::fabs(p[2]);
        int face;
        float sc, tc, ma;
        if (ax >= ay && ax >= az)
        {
            face = p[0] >= 0.0f ? 0 : 1;
            ma   = ax;
            sc   = p[0] >= 0.0f ? -p[2] : p[2];
            tc   = -p[1];
        }
        else if (ay >= az)
        {
            face = p[1] >= 0.0f ? 2 : 3;
            ma   = ay;
            sc   = p[0];
            tc   = p[1] >= 0.0f ? p[2] : -p[2];
        }
        else
        {
            face = p[2] >= 0.0f ? 4 : 5;
            ma   = az;
            sc   = p[2] >= 0.0f ? p[0] : -p[0];
            tc   = -p[1];
        }
        const bool equi = b.cube_warp == RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR;
        const float a   = warp(sc / ma, equi);
        const float bb  = warp(tc / ma, equi);
        const float s   = (a + 1.0f) * 0.5f;
        const float tt  = (bb + 1.0f) * 0.5f;
        map_read(t.coverage + (size_t)face * m * m * 4, m, s, tt, false, out);
        return;
    }
    const float *u  = b.up;
    const float *ma = b.map_axis;
    const float v0  = (u[1] * ma[2]) - (u[2] * ma[1]);
    const float v1  = (u[2] * ma[0]) - (u[0] * ma[2]);
    const float v2  = (u[0] * ma[1]) - (u[1] * ma[0]);
    const float dm  = ((p[0] * ma[0]) + (p[1] * ma[1])) + (p[2] * ma[2]);
    const float dv  = ((p[0] * v0) + (p[1] * v1)) + (p[2] * v2);
    const float s   = (dm / b.map_extent) + o.coverage[0];
    const float tt  = (dv / b.map_extent) + o.coverage[1];
    map_read(t.coverage, m, s, tt, b.map_address == RBTMATH_CLOUD_MAP_ADDRESS_REPEAT, out);
}

inline float profile(const float *p4, float h)
{
    const float up = sat((h - p4[0]) / (p4[1] - p4[0]));
    const float dn = sat((p4[3] - h) / (p4[3] - p4[2]));
    return up < dn ? up : dn;
}

float density_of(const RBTMATH_CLOUD_SHAPE &s, const RBTMATH_CLOUD_BOUNDS &b, const RBTMATH_CLOUD_TEXELS &t, const RBTMATH_CLOUD_OFFSETS &o,
                 const float *p)
{
    float r;
    const float h = height_of(b, p, r);
    if (!inside(h))
        return 0.0f;
    if (b.kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL && r == 0.0f)
        return 0.0f;
    float map[3];
    coverage_at(b, t, o, p, map);
    const float c = sat((map[0] * s.coverage_scale) + s.coverage_bias);
    const float g = map[1];
    const float k = map[2];
    // Step 3.
    float P;
    if (s.type_count == 1)
        P = profile(s.profiles, h);
    else
    {
        const float x = g * (float)(s.type_count - 1);
        float fi      = std::floor(x);
        if (fi > (float)(s.type_count - 2))
            fi = (float)(s.type_count - 2);
        const uint32_t i = (uint32_t)fi;
        const float f    = x - fi;
        const float p0   = profile(s.profiles + 4 * i, h);
        const float p1   = profile(s.profiles + 4 * (i + 1), h);
        P                = p0 + (f * (p1 - p0));
    }
    // Step 4.
    float q[3], v[4];
    noise_coordinate(p, s.base_tile, o.base, q);
    rbtnoise::volume_read(t.base, t.base_size, q, v);
    const float l      = ((v[1] * 0.625f) + (v[2] * 0.25f)) + (v[3] * 0.125f);
    const float e      = sat((v[0] - (l - 1.0f)) / (1.0f - (l - 1.0f)));
    const float shaped = e * P;
    // Step 5.
    if (c == 0.0f)
        return 0.0f;
    const float d0 = sat((shaped - (1.0f - c)) / c) * c;
    if (d0 == 0.0f)
        return 0.0f;
    // Step 6.
    noise_coordinate(p, s.detail_tile, o.detail, q);
    rbtnoise::volume_read(t.detail, t.detail_size, q, v);
    const float w  = ((v[0] * 0.625f) + (v[1] * 0.25f)) + (v[2] * 0.125f);
    const float m  = w + (sat(h / s.billow_height) * ((1.0f - w) - w));
    const float e2 = m * s.erosion;
    if (e2 >= 1.0f)
        return 0.0f;
    const float d1 = sat((d0 - e2) / (1.0f - e2));
    // Step 7.
    return (d1 * s.density_scale) * k;
}

bool within_limit(const float q[3])
{
    for (int a = 0; a < 3; a++)
        if (!(std::fabs(q[a]) <= RBTMATH_NOISE_VOLUME_MAX_COORDINATE))
            return false;
    return true;
}

} // namespace

extern "C" RBT_RESULT rbtmath_cloud_compute_density(const RBTMATH_CLOUD_SHAPE *shape, const RBTMATH_CLOUD_BOUNDS *bounds,
                                                    const RBTMATH_CLOUD_TEXELS *texels, const RBTMATH_CLOUD_OFFSETS *offsets,
                                                    const float *points, uint32_t count, float *out_density, RBTMATH_ERROR *error)
{
    clear(error);
    if (!shape)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "shape is NULL");
    if (!bounds)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "bounds is NULL");
    if (!texels)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "texels is NULL");
    if (!offsets)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "offsets is NULL");
    RBT_RESULT checked = check_shape(*shape, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    checked = check_bounds(*bounds, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    checked = check_texels(*texels, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    checked = check_offsets(*offsets, bounds->kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    if (!points && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "points is NULL with a count of %u", count);
    if (!out_density && count > 0)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_density is NULL with a count of %u", count);
    const RBTMATH_CLOUD_SHAPE &s   = *shape;
    const RBTMATH_CLOUD_BOUNDS &b  = *bounds;
    const RBTMATH_CLOUD_OFFSETS &o = *offsets;
    for (uint32_t i = 0; i < count; i++)
    {
        const float *p = points + (size_t)i * 3;
        if (!finite_xyz(p))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "point %u is not finite", i);
        float r;
        if (!inside(height_of(b, p, r)))
            continue;
        float q[3];
        noise_coordinate(p, s.base_tile, o.base, q);
        if (!within_limit(q))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "point %u is read at a base coordinate past RBTMATH_NOISE_VOLUME_MAX_COORDINATE", i);
        noise_coordinate(p, s.detail_tile, o.detail, q);
        if (!within_limit(q))
            return fail(error, RBT_RESULT_INVALID_ARGUMENT, "point %u is read at a detail coordinate past RBTMATH_NOISE_VOLUME_MAX_COORDINATE",
                        i);
    }
    for (uint32_t i = 0; i < count; i++)
        out_density[i] = density_of(s, b, *texels, o, points + (size_t)i * 3);
    return RBT_RESULT_OK;
}
