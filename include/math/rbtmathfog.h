#ifndef RBTMATHFOG_H
#define RBTMATHFOG_H

/**
 * @file rbtmathfog.h
 * @brief Rabbittus Math's fog: a height fog's density and transmittance
 * (over a plane or round a sphere), and a local fog volume's density (a box
 * or a sphere, edge faded, broken by a baked noise volume, rbtmathnoise.h).
 *
 * Built in src/math/rbtmathfog.cpp, with headless tests in
 * tests/rbtmathfog_tests.c and tests/rbtmathfog_bits_tests.c. Not part of
 * rbtrend.h, which is Rend's alone; Rend's fog (rbtrendlit.h's scene fog,
 * rbtrendfog.h's grid) takes these structs and draws them, its shaders
 * working out the same definition to a tolerance.
 *
 * Two kinds of medium. A height fog fills space and thins with height above
 * a base: a plane's (a flat level) or a sphere's (a round world). Its
 * density is held at its base value below the base (a fog lying on the
 * ground), and thins by 1/e every scale height above. A fog volume is a box
 * or a sphere a caller places, its density faded in from its boundary and
 * broken up by a WORLEY noise volume (rbtmathnoise.h). Densities are the
 * extinction itself, per unit length of the caller's unit, one channel:
 * fog is grey in extinction; its colour is its light's, which is drawing's.
 *
 * General purpose: nothing here knows what the fog is (mist, haze, dust,
 * smoke, a gas giant's deck) or what an answer becomes (a draw distance, a
 * sight, a sensor).
 *
 * Out of scope: light (scattering, phase functions, colour), which is
 * drawing's; a second layer of height fog (a caller sums two calls'
 * optical depths); ellipsoids and other volume shapes; a ray to infinity
 * (a fog has no edge: a ray from below a plane's fog upwards to infinity
 * is lit, one downwards is not; a segment says which).
 *
 * @par Definition
 * Every float operation binary32, rounded once, in the order the
 * parentheses give; no contraction. "sat(x)" is min(max(x, 0), 1). E(x) is
 * (float)exp((double)x), exp being rbtmathfunctions.h's (within 1 ulp,
 * the same bits everywhere).
 * @verbatim
 * HEIGHT. For a point p relative to the fog's origin:
 *   PLANE:  y = ((p0 * up0) + (p1 * up1)) + (p2 * up2) - base;
 *   SPHERE: y = sqrt(((p0 * p0) + (p1 * p1)) + (p2 * p2)) - base.
 *
 * DENSITY. y >= 0: rho = density * E(-(y / scale_height)); y < 0 (and a
 *   height not a number never arises: points are checked finite):
 *   rho = density.
 *
 * TRANSMITTANCE OF A SEGMENT, a to b, T = E(-tau), tau rounded to float
 *   from double. From here every sum and product is in double, from the
 *   floats; L = |b - a| in double.
 *   PLANE: the heights y_a, y_b (as HEIGHT, in float). Where they lie
 *   either side of 0 (one below, one at or above), the segment is split
 *   at t_c = y_a / (y_a - y_b) of the way, the crossing's height 0. A part
 *   below the base adds density * its length. A part above, from height y0
 *   to y1 over length l: x = (y1 - y0) / scale_height (signed), and
 *   tau += density * l * E(-(y0 / scale_height)) * g(x), with
 *   g(x) = (1 - E(-x)) / x for |x| >= 2^-4, else
 *   1 - x/2 + x^2/6 - x^3/24 + x^4/120, by Horner's rule from the highest
 *   term: ((((x / 120 - 1/24) x + 1/6) x - 1/2) x + 1).
 *   SPHERE: the segment's points at parameter t in [0, 1]; split at the
 *   closest point to the centre, t_m = -(a . (b - a)) / L^2 where it lies
 *   strictly inside, and at each crossing of the base radius strictly
 *   inside (the roots of |a + t (b - a)| = base). Each part's radius is
 *   then monotonic and on one side of the base. A part below the base adds
 *   density * its length. A part above, oriented outward (from its point
 *   of smaller radius, r0 with the cosine mu0 between its radius and the
 *   way out, to its point of larger radius r1, mu1):
 *   tau += D(r0, mu0) - D(r1, mu1), D(r, mu) = density * scale_height *
 *   exp(-(r - base) / scale_height) * C(r / scale_height, mu), the optical
 *   depth from a point outwards to infinity, by the Chapman function's
 *   large-x form, for mu at least 0 (mu clamped to [0, 1]):
 *   C(x, mu) = sqrt(pi x / 2) * erfcx(sqrt(x / 2) * mu). A part's
 *   difference is taken as 0 where
 *   it would be below 0.
 *
 * ERFCX(y) = exp(y^2) erfc(y), y at least 0, in double: below 2,
 *   exp(y^2) * (1 - (2 / sqrt(pi)) * sum), the sum erf's Maclaurin series
 *   y^(2n+1) (-1)^n / (n! (2n + 1)) for n = 0 to 40, each term from the
 *   last (t = t * (-(y * y) / (n + 1))); from 2, 1 / (sqrt(pi) * f), f
 *   Laplace's continued fraction worked from the bottom: f = y, then for
 *   k = 60 down to 1, f = y + (k / 2) / f.
 *
 * A FOG VOLUME'S DENSITY at p in its own frame (centred, axes its box's):
 *   the inside distance i: BOX: the smallest of half_extents[a] - |p_a|
 *   over the axes in order; SPHERE: half_extents[0] -
 *   sqrt(((p0 * p0) + (p1 * p1)) + (p2 * p2)). i <= 0: 0. The fade
 *   f = sat(i / edge), or 1 with edge 0. The noise n: with noise_strength
 *   0, 1, the volume not read; else (R, G, B, _) =
 *   rbtmath_noise_volume_sample() of a WORLEY volume at
 *   (p_a / noise_tile) + offset_a per axis; w = ((R * 0.625f) +
 *   (G * 0.25f)) + (B * 0.125f); n = 1 + (noise_strength * (w - 1)).
 *   rho = (density * f) * n.
 * @endverbatim
 *
 * @par Accuracy
 * A plane's transmittance is closed: tests/rbtmathfog_tests.c holds it to
 * 1e-5 relative in tau against a 2^16-step march in double. A sphere's
 * outward depth is the Chapman function's large-x form, low by about
 * 1 / x straight up and less towards the horizon (x = r / scale_height):
 * 2% at x = 50, 1.3% at x = 75, 0.1% at x = 800, 4% at x = 20; a fog
 * whose radius is fewer scale heights than that is outside what it holds
 * to. Against a dense march, the tests' segments on a 150 km sphere
 * (ground to horizon, grazing, through the closest point and the base)
 * are within 0.07% at a 100 m scale height and 1.3% at 2 km
 * (RBTMATH_TODO.md, "Fog density").
 *
 * @par Same bits
 * Every call gives the same bits on every platform for the same input,
 * under IEEE 754's default environment: the source is built without
 * floating-point contraction, its exponential is rbtmathfunctions.h's, and
 * its only libm calls are floor and sqrt (correctly rounded everywhere).
 *
 * @par Threads
 * Every call reads only its arguments and writes only its outputs, so any
 * of them may run at once, from any thread, on any data; each call holds
 * nothing between calls.
 *
 * @par Results
 * Calls return RBT_RESULT (rbtcore.h): RBT_RESULT_OK, or
 * RBT_RESULT_INVALID_ARGUMENT with the reason in the RBTMATH_ERROR the
 * caller passes (NULL to skip it), nothing written. A fog or a volume is
 * checked field by field, in the struct's order, each its own check and
 * message, then the other arguments in parameter order, then every point.
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

/** @brief What a height fog's height is measured from. */
typedef enum RBTMATH_FOG_HEIGHT_KIND RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_FOG_HEIGHT_KIND_UNKNOWN = 0,
	/** Height along #RBTMATH_HEIGHT_FOG::up above a plane through the
	 * origin. A flat level. */
	RBTMATH_FOG_HEIGHT_KIND_PLANE = 1,
	/** Height above a sphere round the origin. A round world. */
	RBTMATH_FOG_HEIGHT_KIND_SPHERE = 2,
} RBTMATH_FOG_HEIGHT_KIND;

/**
 * @brief A height fog: extinction per unit length at its base, thinning
 * by 1/e every scale height above, held below. A value.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_HEIGHT_FOG {
	/** A plane or a sphere. */
	RBTMATH_FOG_HEIGHT_KIND kind;
	/** A plane's up: finite, its length within 1e-4 of 1. Read by PLANE;
	 * all 0 for a sphere. */
	float up[3];
	/** A plane's base height above the origin along #up, or a sphere's
	 * base radius: finite; a sphere's above 0. */
	float base;
	/** The extinction at the base and below, per unit length: finite, at
	 * least 0. */
	float density;
	/** How far above the base it falls by 1/e: finite, above 0. */
	float scale_height;
} RBTMATH_HEIGHT_FOG;

/**
 * @brief A height fog's density (extinction per unit length) at each
 * point.
 *
 * @param[in]  fog         The fog. Only read during the call.
 * @param[in]  points      @p count points relative to its origin, three
 *                         floats each, each finite. NULL with a count of 0.
 * @param[in]  count       How many; 0 writes nothing.
 * @param[out] out_density @p count floats. NULL with a count of 0.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL fog, a NULL
 *         array with a count above 0, a field out of its range or set where
 *         unread, or a point not finite (its index named). Nothing is
 *         written on failure.
 */
RBT_RESULT rbtmath_height_fog_compute_density(
	const RBTMATH_HEIGHT_FOG *fog,
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);

/**
 * @brief How much light gets through a height fog along each segment, from
 * its start to its end: exp(-tau), tau its extinction summed between. A
 * plane's in closed form; a sphere's by the Chapman function's large-x
 * form, within what the header's "Accuracy" names. Segments, not rays: a fog has
 * no edge (a ray to infinity through a plane's fog from below is 0).
 *
 * @param[in]  fog               The fog. Only read during the call.
 * @param[in]  starts            @p count points relative to its origin,
 *                               three floats each, each finite.
 * @param[in]  ends              @p count points, each finite.
 * @param[in]  count             How many; 0 writes nothing.
 * @param[out] out_transmittance @p count floats, 0 to 1.
 * @param[out] error             Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_height_fog_compute_density() (a start or an end not
 *         finite named by its index).
 */
RBT_RESULT rbtmath_height_fog_compute_transmittance(
	const RBTMATH_HEIGHT_FOG *fog,
	const float *starts,
	const float *ends,
	uint32_t count,
	float *out_transmittance,
	RBTMATH_ERROR *error);

/** @brief A fog volume's shape. */
typedef enum RBTMATH_FOG_VOLUME_SHAPE RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_FOG_VOLUME_SHAPE_UNKNOWN = 0,
	/** A box of #RBTMATH_FOG_VOLUME::half_extents. */
	RBTMATH_FOG_VOLUME_SHAPE_BOX = 1,
	/** A sphere of radius #RBTMATH_FOG_VOLUME::half_extents[0] (the others 0). */
	RBTMATH_FOG_VOLUME_SHAPE_SPHERE = 2,
} RBTMATH_FOG_VOLUME_SHAPE;

/**
 * @brief A local fog volume, in its own frame (centred on its origin, its
 * box along its axes). A value.
 */
/* Frozen: a value, read by the call below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_FOG_VOLUME {
	/** A box or a sphere. */
	RBTMATH_FOG_VOLUME_SHAPE shape;
	/** A box's half sizes, or a sphere's radius then two 0s: each finite,
	 * above 0 where read. */
	float half_extents[3];
	/** How far in from its boundary its density fades from 0 to full:
	 * finite, 0 (a hard edge) to its smallest half size (or radius). */
	float edge;
	/** Its extinction where full, per unit length: finite, at least 0. */
	float density;
	/** How much the noise breaks it up: finite, 0 (even; the noise not
	 * read) to 1 (its density times the noise's value). */
	float noise_strength;
	/** The noise volume's tile, in the caller's unit: finite, above 0;
	 * read with #noise_strength above 0, else must be 0. */
	float noise_tile;
} RBTMATH_FOG_VOLUME;

/**
 * @brief A fog volume's density (extinction per unit length) at each
 * point of its own frame.
 *
 * @param[in]  volume       The volume. Only read during the call.
 * @param[in]  noise        A WORLEY volume (rbtmath_noise_volume_bake()),
 *                          @p noise_size^3 x 4 bytes; NULL where
 *                          `volume->noise_strength` is 0. Only read during
 *                          the call.
 * @param[in]  noise_size   Its side, 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE; 0
 *                          with no noise.
 * @param[in]  noise_offset The noise's coordinate at the volume's origin,
 *                          in tiles (wind moves it): three floats, each
 *                          finite and within
 *                          RBTMATH_NOISE_VOLUME_MAX_COORDINATE. Read with
 *                          noise; may be NULL without.
 * @param[in]  points       @p count points in the volume's frame, three
 *                          floats each, each finite; with noise, each
 *                          inside the volume read at a coordinate within
 *                          RBTMATH_NOISE_VOLUME_MAX_COORDINATE.
 * @param[in]  count        How many; 0 writes nothing.
 * @param[out] out_density  @p count floats.
 * @param[out] error        Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL volume, a
 *         field out of its range, noise given or not as the strength asks,
 *         a noise size or offset out of range, a NULL array with a count
 *         above 0, or a point not finite or read past the noise's limit
 *         (its index named). Nothing is written on failure.
 */
RBT_RESULT rbtmath_fog_volume_compute_density(
	const RBTMATH_FOG_VOLUME *volume,
	const uint8_t *noise,
	uint32_t noise_size,
	const float noise_offset[3],
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);

#if __cplusplus
}
#endif

#endif // RBTMATHFOG_H
