#ifndef RBTMATHCLOUDS_H
#define RBTMATHCLOUDS_H

/**
 * @file rbtmathclouds.h
 * @brief Rabbittus Math's cloud layers: the density of a layer of cloud
 * bounded by a spherical shell or a flat slab, from its shape, two baked
 * noise volumes (rbtmathnoise.h) and a coverage map, at many points a call.
 *
 * Built in src/math/rbtmathclouds.cpp, with headless tests in
 * tests/rbtmathclouds_tests.c and tests/rbtmathclouds_bits_tests.c. Not part
 * of rbtrend.h, which is Rend's alone; Rend's clouds (rbtrendclouds.h) take
 * these structs and march the same definition in a shader, to a tolerance
 * (a GPU filters texels in its own fixed point and may fuse a multiply and
 * an add).
 *
 * A layer's density at a place is a pure function of: the layer's bounds (a
 * spherical shell or a flat slab), its shape (height profiles by type, the
 * noise's tile sizes, erosion, a density scale, how it answers the map), two
 * tiling noise volumes (rbtmath_noise_volume_bake(): a PERLIN_WORLEY base and
 * a WORLEY detail), a coverage map (a shell's six cube faces, a slab's one),
 * and where the noise and the map sit this moment (offsets: wind, the
 * caller's place in the tiling). The model is Nubis's (Schneider, 2015):
 * coverage from the map, a height profile by cloud type, the base's shape
 * eroded by the detail, wisps low and billows high.
 *
 * General purpose: nothing here knows what a layer is (weather, a fog bank,
 * dust, a nebula's sheet) or what an answer becomes (a sensor, a sound, an
 * aircraft's icing).
 *
 * Out of scope: light, which is drawing's; a ray's transmittance, which a
 * caller sums from densities; other models of cloud.
 *
 * @par Definition
 * Every float operation binary32, rounded once, in the order the
 * parentheses give; no contraction. "sat(x)" is min(max(x, 0), 1), NaN
 * never arising (every input is checked finite). "sample(V, q)" is
 * rbtmath_noise_volume_sample()'s read of volume V at q (rbtmathnoise.h,
 * "Reading a volume").
 * @verbatim
 * For a point p, three floats relative to the bounds' origin (a shell's
 * centre; a slab's point on its base plane, at height 0):
 * 1. HEIGHT. SHELL: r = sqrt(((p0 * p0) + (p1 * p1)) + (p2 * p2)),
 *    y = r - bottom. SLAB: y = (((p0 * up0) + (p1 * up1)) + (p2 * up2)) -
 *    bottom. h = y / (top - bottom). h < 0 or h > 1: density 0, stop.
 * 2. COVERAGE. SHELL (r 0 is density 0): the face by the largest |p_a|,
 *    ties to x, then y, then z; (sc, tc, ma) from the table below;
 *    x_g = sc / |ma|, y_g = tc / |ma|; PLAIN: a = x_g, b = y_g;
 *    EQUI_ANGULAR: a = (float)(atan((double)x_g) * 0x1.45f306dc9c883p+0)
 *    (4/pi in double, atan rbtmathfunctions.h's), b alike;
 *    s = (a + 1) * 0.5f, t = (b + 1) * 0.5f. The face's texels:
 *    u = (s * M) - 0.5f, v = (t * M) - 0.5f, bilinear (the lerp of
 *    rbtmathnoise.h's read, x then y), each index clamped to 0 to M - 1 (no
 *    face reads another's texels). SLAB: v_axis = up x map_axis
 *    ((up1 * m2) - (up2 * m1), (up2 * m0) - (up0 * m2), (up0 * m1) -
 *    (up1 * m0)); s = ((p . map_axis) / extent) + off0,
 *    t = ((p . v_axis) / extent) + off1, the dots as in step 1;
 *    u = (s * M) - 0.5f, v = (t * M) - 0.5f; REPEAT: indices wrapped as
 *    rbtmathnoise.h's read wraps them; CLAMP: indices clamped to 0 to M - 1.
 *    A texel's byte b reads b / 255.0f. The coverage
 *    c = sat((R * coverage_scale) + coverage_bias), the type g = G, the
 *    density factor k = B; A is never read.
 *
 *    | face | ma  | sc  | tc  |      (the order +X, -X, +Y, -Y, +Z, -Z:
 *    | +X   | +x  | -z  | -y  |       the GPU's cube faces, a layer each,
 *    | -X   | -x  | +z  | -y  |       face f at byte offset f * M * M * 4)
 *    | +Y   | +y  | +x  | +z  |
 *    | -Y   | -y  | +x  | -z  |
 *    | +Z   | +z  | +x  | -y  |
 *    | -Z   | -z  | -x  | -y  |
 *
 * 3. PROFILE. x = g * (float)(type_count - 1); i = min(floor(x),
 *    type_count - 2) (0 when type_count is 1); f = x - i. Profile n's four
 *    heights (a, b, c, d) = profiles[4n .. 4n + 3]:
 *    P_n(h) = min(sat((h - a) / (b - a)), sat((d - h) / (d - c))).
 *    P = P_i(h) + (f * (P_(i+1)(h) - P_i(h))), or P_0(h) for one type.
 * 4. BASE. (R, G, B, A) = sample(base, (p / base_tile) + offsets.base), the
 *    division per component; l = ((G * 0.625f) + (B * 0.25f)) +
 *    (A * 0.125f); e = sat((R - (l - 1)) / (1 - (l - 1)))
 *    (Nubis's remap(R, l - 1, 1, 0, 1)); shaped = e * P.
 * 5. COVERAGE APPLIED. c 0: density 0, stop. d0 = sat((shaped - (1 - c)) /
 *    c) * c. d0 0: density 0, stop (the detail isn't read).
 * 6. EROSION. (R, G, B, _) = sample(detail, (p / detail_tile) +
 *    offsets.detail); w = ((R * 0.625f) + (G * 0.25f)) + (B * 0.125f);
 *    m = w + (sat(h / billow_height) * ((1 - w) - w)) (wisps low, billows
 *    high); e2 = m * erosion; e2 at least 1: density 0, stop;
 *    d1 = sat((d0 - e2) / (1 - e2)).
 * 7. DENSITY = (d1 * density_scale) * k.
 * @endverbatim
 * A density is unitless: a caller multiplies it by an extinction (per unit
 * length) to get a medium's.
 *
 * @par Same bits
 * Every call gives the same bits on every platform for the same input,
 * under IEEE 754's default environment: the source is built without
 * floating-point contraction, the equi-angular warp's arctangent is
 * rbtmathfunctions.h's (within 1 ulp by a written evaluation, the same bits
 * everywhere), and its only libm calls are floor and sqrt (correctly
 * rounded everywhere).
 *
 * @par Threads
 * Every call reads only its arguments and writes only its outputs, so any
 * of them may run at once, from any thread, on any data; each call holds
 * nothing between calls.
 *
 * @par Results
 * Calls return RBT_RESULT (rbtcore.h): RBT_RESULT_OK, or
 * RBT_RESULT_INVALID_ARGUMENT with the reason in the RBTMATH_ERROR the
 * caller passes (NULL to skip it), nothing written. The structs are checked
 * in parameter order, each field by field in its members' order, each its
 * own check and message, then the arrays, then every point.
 *
 * @par Allocation
 * None: every call writes only into the caller's arrays and its own stack.
 */

#include "../rbtcore.h"
#include "rbtmatherror.h"

#include <stdint.h>

#if __cplusplus
extern "C" {
#endif

/** @brief The most cloud types a shape blends (RBTMATH_CLOUD_SHAPE::type_count). */
#define RBTMATH_CLOUD_MAX_TYPES 4u

/** @brief The largest coverage map side, in texels. */
#define RBTMATH_CLOUD_MAX_COVERAGE_SIZE 4096u

/** @brief What bounds a cloud layer. */
typedef enum RBTMATH_CLOUD_BOUNDS_KIND RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_CLOUD_BOUNDS_KIND_UNKNOWN = 0,
	/** Between two spheres round the origin, radii #RBTMATH_CLOUD_BOUNDS::bottom
	 * and #RBTMATH_CLOUD_BOUNDS::top; its coverage map a cube's six faces. */
	RBTMATH_CLOUD_BOUNDS_KIND_SHELL = 1,
	/** Between two planes across #RBTMATH_CLOUD_BOUNDS::up, at heights
	 * bottom and top above the origin; endless across, its coverage map one
	 * face laid along the plane. */
	RBTMATH_CLOUD_BOUNDS_KIND_SLAB = 2,
} RBTMATH_CLOUD_BOUNDS_KIND;

/** @brief How a shell's cube faces map directions to texels. */
typedef enum RBTMATH_CLOUD_CUBE_WARP RBT_ENUM_BASE {
	/** Not chosen: refused with a shell; must be this with a slab. */
	RBTMATH_CLOUD_CUBE_WARP_UNKNOWN = 0,
	/** Gnomonic: a texel's place on its face proportional to the
	 * direction's tangent (a GPU's cube map). Texels at a face's centre 2.28
	 * times the linear size of those at its corners. */
	RBTMATH_CLOUD_CUBE_WARP_PLAIN = 1,
	/** Equi-angular: proportional to the angle, (4 / pi) atan; texels
	 * within 1.19 of each other in linear size. */
	RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR = 2,
} RBTMATH_CLOUD_CUBE_WARP;

/** @brief What a slab's coverage map reads past its edges. */
typedef enum RBTMATH_CLOUD_MAP_ADDRESS RBT_ENUM_BASE {
	/** Not chosen: refused with a slab; must be this with a shell. */
	RBTMATH_CLOUD_MAP_ADDRESS_UNKNOWN = 0,
	/** The map tiles: an endless sky of a repeating weather. */
	RBTMATH_CLOUD_MAP_ADDRESS_REPEAT = 1,
	/** Its edge texels carry on: a map over a level, its rim's weather
	 * beyond it. */
	RBTMATH_CLOUD_MAP_ADDRESS_CLAMP = 2,
} RBTMATH_CLOUD_MAP_ADDRESS;

/**
 * @brief A cloud layer's shape: how its density rises and falls with height
 * by cloud type, its noise's scale and its erosion. A value; the calls copy
 * what they read.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_SHAPE {
	/** How many types the map's type channel blends between: 1 to
	 * RBTMATH_CLOUD_MAX_TYPES. Type 0 at the channel's 0, the last at its
	 * 1, evenly between. */
	uint32_t type_count;
	/**
	 * Each type's height profile, four floats a type, #type_count types
	 * from index 0 (the rest must be 0): heights as fractions of the layer's
	 * thickness, a < b <= c < d, each 0 to 1. The density's scale rises
	 * from 0 at a to 1 at b, holds to c, and falls to 0 at d (Nubis's
	 * height gradients: stratus low and thin, cumulus tall).
	 */
	float profiles[16];
	/** The base volume's tile, in the caller's unit: finite, above 0. */
	float base_tile;
	/** The detail volume's tile: finite, above 0. */
	float detail_tile;
	/** How much the detail erodes the base's edges: 0 (none) to 1. */
	float erosion;
	/** Up to this fraction of the thickness the detail turns from wisps
	 * (low) to billows (high): finite, above 0, at most 1. */
	float billow_height;
	/** What a full density is multiplied by: finite, at least 0. */
	float density_scale;
	/** What the map's coverage is multiplied by for this layer: finite (0:
	 * the map's coverage unread, #coverage_bias everywhere). */
	float coverage_scale;
	/** What is added to it, the sum clamped to 0 to 1: finite. One map
	 * drives several layers, each by its own scale and bias. */
	float coverage_bias;
} RBTMATH_CLOUD_SHAPE;

/**
 * @brief What bounds a layer, relative to an origin the caller places, and
 * how its coverage map lies on it. A value; the calls copy what they read.
 * Fields a kind doesn't read must be 0.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_BOUNDS {
	/** A shell or a slab. */
	RBTMATH_CLOUD_BOUNDS_KIND kind;
	/** A shell's inner radius, or a slab's lower height above its origin:
	 * finite; a shell's above 0. */
	float bottom;
	/** A shell's outer radius, or a slab's upper height: finite, above
	 * #bottom. */
	float top;
	/** A slab's up: finite, its length within 1e-4 of 1. Read by SLAB. */
	float up[3];
	/** The direction a slab's map's first axis runs: finite, its length
	 * within 1e-4 of 1, square to #up within 1e-4 (its dot). Its second
	 * axis is up x map_axis. Read by SLAB. */
	float map_axis[3];
	/** How far one map spans along each axis, in the caller's unit:
	 * finite, above 0. Read by SLAB. */
	float map_extent;
	/** Past a slab's map. Read by SLAB. */
	RBTMATH_CLOUD_MAP_ADDRESS map_address;
	/** A shell's faces' warp. Read by SHELL. */
	RBTMATH_CLOUD_CUBE_WARP cube_warp;
} RBTMATH_CLOUD_BOUNDS;

/**
 * @brief The texels a density reads, borrowed for one call: the two baked
 * volumes and the coverage map.
 */
/* Frozen: a value, read by the calls below. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_TEXELS {
	/** A PERLIN_WORLEY volume, #base_size^3 x 4 bytes (rbtmathnoise.h). */
	const uint8_t *base;
	/** Its side: 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE. */
	uint32_t base_size;
	/** A WORLEY volume, #detail_size^3 x 4 bytes. */
	const uint8_t *detail;
	/** Its side: 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE. */
	uint32_t detail_size;
	/**
	 * The coverage map, four bytes a texel, rows of #coverage_size, x
	 * fastest: a shell's six faces one after another (+X, -X, +Y, -Y, +Z,
	 * -Z), a slab's one. R: coverage, 0 none to 1 overcast. G: cloud type,
	 * 0 the first to 1 the last (RBTMATH_CLOUD_SHAPE::type_count). B: what
	 * the density is multiplied by (a storm's darker core; 255 for 1). A:
	 * never read, any value.
	 */
	const uint8_t *coverage;
	/** Its side: 1 to RBTMATH_CLOUD_MAX_COVERAGE_SIZE. */
	uint32_t coverage_size;
} RBTMATH_CLOUD_TEXELS;

/**
 * @brief Where the tiling sits at the points' origin this moment: the noise
 * volumes' coordinates, in tiles, and a slab's map's, in maps. Wind is a
 * caller moving these; a caller whose origin is far out puts its place in
 * the tiling here (each wraps at 1).
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_OFFSETS {
	/** The base volume's coordinate at the origin: each finite, |each| at
	 * most RBTMATH_NOISE_VOLUME_MAX_COORDINATE. */
	float base[3];
	/** The detail volume's, as #base. */
	float detail[3];
	/** A slab's map's coordinate at the origin, in maps: finite; 0 for a
	 * shell. */
	float coverage[2];
} RBTMATH_CLOUD_OFFSETS;

/**
 * @brief A cloud layer's density at each point: unitless, 0 outside the
 * layer, at most `shape->density_scale` (the header's "Definition").
 *
 * @param[in]  shape       The shape. Only read during the call.
 * @param[in]  bounds      The bounds and the map's lie.
 * @param[in]  texels      The volumes and the map: a shell's map six faces,
 *                         a slab's one.
 * @param[in]  offsets     Where the tiling sits at the origin.
 * @param[in]  points      @p count points relative to the bounds' origin,
 *                         three floats each, each finite; each inside the
 *                         layer read at noise coordinates within
 *                         RBTMATH_NOISE_VOLUME_MAX_COORDINATE. NULL with a
 *                         count of 0.
 * @param[in]  count       How many; 0 writes nothing.
 * @param[out] out_density @p count floats.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL struct, a
 *         NULL array with a count above 0, a field out of its range or set
 *         where unread (each its own check and message, the structs in
 *         parameter order, each in its members' order), or a point not
 *         finite or read past the noise's limit (its index named). Nothing
 *         is written on failure.
 */
RBT_RESULT rbtmath_cloud_compute_density(
	const RBTMATH_CLOUD_SHAPE *shape,
	const RBTMATH_CLOUD_BOUNDS *bounds,
	const RBTMATH_CLOUD_TEXELS *texels,
	const RBTMATH_CLOUD_OFFSETS *offsets,
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);

#if __cplusplus
}
#endif

#endif // RBTMATHCLOUDS_H
