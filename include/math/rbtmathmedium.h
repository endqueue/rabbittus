#ifndef RBTMATHMEDIUM_H
#define RBTMATHMEDIUM_H

/**
 * @file rbtmathmedium.h
 * @brief Rabbittus Math's layered media in a spherical shell: how dense
 * one is at a point, how much light gets through it along a ray, and how it
 * scatters light by angle.
 *
 * Built in src/math/rbtmathmedium.cpp, with headless tests in
 * tests/rbtmathmedium_tests.c. It is not part of rbtrend.h, which is Rend's
 * alone; Rend's atmosphere (rbtrendatmosphere.h) takes the same medium and
 * draws it, its shaders working out the same model.
 *
 * A medium fills the shell between two spheres round a centre the caller
 * places: an inner one, an opaque ground, and an outer one, beyond which
 * there is nothing. Inside, three layers of particles, each with a density
 * relative to its own at the bottom:
 * - a first layer that scatters and doesn't absorb, whose density falls by
 *   1/e every #RBTMATH_SHELL_MEDIUM::rayleigh_scale_height (Rayleigh's
 *   scattering, by molecules: its phase function is Rayleigh's);
 * - a second that scatters and absorbs, falling by 1/e every
 *   #RBTMATH_SHELL_MEDIUM::mie_scale_height (Mie's, by larger particles:
 *   its phase function is Cornette and Shanks', forward by its anisotropy);
 * - a third that only absorbs, densest at a height and falling linearly to
 *   nothing either side (a tent).
 * Lengths are the caller's unit, whatever it is, and every coefficient is
 * per that unit. Three channels throughout (red, green and blue, or any
 * three wavelengths the caller chooses).
 *
 * General purpose: nothing here knows what the medium is (an atmosphere, a
 * nebula, a glow round a sphere) or what the caller does with an answer (a
 * drag, a light's colour, a star's dimming).
 *
 * Out of scope: drawing it (Rend's); scattering summed along a ray (the
 * light a ray gathers, which needs a light and is drawing's work); media
 * that aren't spherical shells; more than three layers.
 *
 * @par Same bits
 * Every call gives the same bits on every platform, given the same input:
 * the source is built without floating-point contraction, and its
 * exponential is its own (a range reduction and a fixed polynomial in
 * doubles), not the platform's libm, whose rounding differs between
 * platforms. Its square roots are IEEE's, correctly rounded everywhere.
 *
 * @par Threads
 * Every call reads only its arguments and writes only its outputs, so any
 * of them may run at once, from any thread, on any data; each call holds
 * nothing between calls.
 *
 * @par Results
 * Calls return RBT_RESULT (rbtcore.h): RBT_RESULT_OK, or
 * RBT_RESULT_INVALID_ARGUMENT with the reason in the RBTMATH_ERROR the
 * caller passes (NULL to skip it), nothing written. A medium is checked
 * field by field, in the struct's order, each its own check and message,
 * by every call that takes one.
 */

#include "../rbtcore.h"
#include "rbtmatherror.h"

#include <stdint.h>

#if __cplusplus
extern "C" {
#endif

/**
 * @brief A layered medium in a spherical shell. A value: the calls copy
 * what they read. Lengths in the caller's unit, coefficients per that unit.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_SHELL_MEDIUM {
	/** The inner sphere's radius, an opaque ground: finite, above 0. */
	float bottom_radius;
	/** The outer sphere's radius: finite, above #bottom_radius. */
	float top_radius;
	/** The first layer's scattering at the bottom, per unit length, three
	 * channels: each finite, at least 0. */
	float rayleigh_scattering[3];
	/** Its density falls by 1/e every this much height: finite, above 0. */
	float rayleigh_scale_height;
	/** The second layer's scattering at the bottom: each finite, at least 0. */
	float mie_scattering[3];
	/** Its extinction (scattering and absorption) at the bottom: each
	 * finite, at least #mie_scattering's. */
	float mie_extinction[3];
	/** Its density falls by 1/e every this much height: finite, above 0. */
	float mie_scale_height;
	/** Its phase function's anisotropy g: finite, -0.999 to 0.999; above 0
	 * scatters forward. */
	float mie_anisotropy;
	/** The third layer's extinction at its peak (it only absorbs): each
	 * finite, at least 0. */
	float absorption_extinction[3];
	/** The height of its peak above the bottom: finite. */
	float absorption_center_height;
	/** Its density falls linearly to 0 this far either side of its peak:
	 * finite, above 0. */
	float absorption_half_width;
	/** How much the ground sends back, linear, three channels: each finite,
	 * 0 to 1. Read by a caller drawing the medium; no call here uses it. */
	float ground_albedo[3];
} RBTMATH_SHELL_MEDIUM;

/** @brief The most steps rbtmath_shell_medium_compute_transmittance() marches. */
#define RBTMATH_SHELL_MEDIUM_MAX_STEPS 1024u

/**
 * @brief Each layer's density at each point, relative to its own at the
 * bottom: the first layer's, the second's and the third's, three floats a
 * point. 0 for all three outside the shell (below the bottom sphere or
 * past the top one).
 *
 * @param[in]  medium      The medium. Only read during the call.
 * @param[in]  points      @p count points relative to the shell's centre,
 *                         three floats each, each finite. NULL with a
 *                         count of 0.
 * @param[in]  count       How many points; 0 writes nothing.
 * @param[out] out_density 3 x @p count floats. NULL with a count of 0.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL medium, a
 *         NULL array with a count above 0, a medium field out of its range,
 *         or a point not finite (its index named). Nothing is written on
 *         failure.
 */
RBT_RESULT rbtmath_shell_medium_compute_density(
	const RBTMATH_SHELL_MEDIUM *medium,
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);

/**
 * @brief How much light gets through the medium along each ray, from its
 * origin to where it leaves the shell, three channels: exp(-tau), tau the
 * three layers' extinction summed along the way. 1 for a ray that never
 * meets the shell; 0 for one that meets the ground first. An origin
 * outside the shell starts where its ray enters it.
 *
 * The extinction is marched in @p steps equal steps over the part of the
 * ray inside the shell, each read at its middle.
 *
 * @param[in]  medium            The medium. Only read during the call.
 * @param[in]  origins           @p count origins relative to the shell's
 *                               centre, three floats each, each finite.
 * @param[in]  directions        @p count directions, three floats each,
 *                               each finite, any length but 0.
 * @param[in]  count             How many rays; 0 writes nothing.
 * @param[in]  steps             1 to RBTMATH_SHELL_MEDIUM_MAX_STEPS.
 * @param[out] out_transmittance 3 x @p count floats.
 * @param[out] error             Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL medium, a
 *         NULL array with a count above 0, steps out of range, a medium
 *         field out of its range, or an origin or direction not finite or
 *         a direction of 0 (its index named). Nothing is written on
 *         failure.
 */
RBT_RESULT rbtmath_shell_medium_compute_transmittance(
	const RBTMATH_SHELL_MEDIUM *medium,
	const float *origins,
	const float *directions,
	uint32_t count,
	uint32_t steps,
	float *out_transmittance,
	RBTMATH_ERROR *error);

/**
 * @brief The two scattering layers' phase functions at each cosine of the
 * angle between the light's way and the way it is scattered to: the first
 * layer's (Rayleigh's, 3 (1 + c^2) / (16 pi)) and the second's (Cornette
 * and Shanks', by #RBTMATH_SHELL_MEDIUM::mie_anisotropy), two floats a
 * cosine. Each integrates to 1 over the sphere.
 *
 * @param[in]  medium    The medium. Only read during the call.
 * @param[in]  cosines   @p count cosines, each finite, -1 to 1.
 * @param[in]  count     How many; 0 writes nothing.
 * @param[out] out_phase 2 x @p count floats.
 * @param[out] error     Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL medium, a
 *         NULL array with a count above 0, a medium field out of its range,
 *         or a cosine out of range (its index named). Nothing is written on
 *         failure.
 */
RBT_RESULT rbtmath_shell_medium_compute_phase(
	const RBTMATH_SHELL_MEDIUM *medium,
	const float *cosines,
	uint32_t count,
	float *out_phase,
	RBTMATH_ERROR *error);

#if __cplusplus
}
#endif

#endif // RBTMATHMEDIUM_H
