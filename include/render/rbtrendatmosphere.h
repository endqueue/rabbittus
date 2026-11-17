#ifndef RBTREND_ATMOSPHERE_H
#define RBTREND_ATMOSPHERE_H

#include "rbtrendcommands.h"

#include "../math/rbtmathmedium.h"

/**
 * @file rbtrendatmosphere.h
 * @brief A scattering medium in a sphere's shell, drawn as Rend's lit sky
 * from inside it: another way to fill and draw the sky a lit scene already
 * has (rbtrendlit.h), not a second sky beside it.
 *
 * The medium is Rabbittus Math's RBTMATH_SHELL_MEDIUM (math/rbtmathmedium.h):
 * what fills the shell between two radii round a centre, in three layers
 * (a Rayleigh-like one that scatters, a Mie-like one that scatters and
 * absorbs, an absorbing tent), with its density, transmittance and phase
 * functions there for any caller to ask on the CPU. Rend renders it by
 * Hillaire's model ("A Scalable and Production Ready Sky and Atmosphere
 * Rendering Technique", EGSR 2020), its shaders working out the same
 * model, and adds only what drawing needs: the tables.
 *
 * An atmosphere (an rbtrend_atmosphere) is a medium and two tables Rend
 * makes from it, rebuilt only when it changes:
 * - transmittance: how much light gets through the medium from a height
 *   up a direction to the top of the shell (0 into the ground), by height
 *   and the direction's angle from the vertical (Bruneton's
 *   parameterisation, every texel's height and angle on the table);
 * - multiple scattering: Hillaire's term for light scattered more than
 *   once, from a height towards the sun at an angle.
 * A lit scene draws one atmosphere at a time
 * (rbtrend_lit_scene_set_atmosphere(), rbtrendlit.h), placing its centre
 * as often as its view moves; it holds its own sky-view table, the sky's
 * radiance all round its eye worked out each frame from the atmosphere's
 * tables (RBTREND_COMMAND_LIT_ATMOSPHERE). With one set:
 * - the sky behind (RBTREND_LIT_SHADER_SKY_FRAGMENT) is the sky-view
 *   table's, every frame, sharp, and each directional light's disc over it
 *   is dimmed and reddened by the transmittance from the eye;
 * - each of the scene's first `sun_count` directional lights reaches a lit
 *   surface through the transmittance from the surface towards it;
 * - an environment filled from the scene
 *   (RBTREND_COMMAND_LIT_ATMOSPHERE_ENVIRONMENT) gives the surfaces the
 *   atmosphere's ambient light and reflections, as any environment does.
 * A scene with none set draws exactly as before.
 *
 * @par Units and light
 * Lengths are the scene's unit and coefficients per that unit; nothing in
 * Rend assumes a planet or the earth's size. Each sun is one of the
 * scene's directional lights, its illuminance pi times its radiance (a
 * matte white face square to it reads its radiance, rbtrendlit.h's
 * scale), so the sky and the surfaces agree with no factor of the
 * caller's.
 *
 * @par Inside and outside
 * The sky-view is worked out from wherever the eye is: inside the shell
 * from the eye, above it from where the view enters the shell, so a camera
 * climbing out keeps a sky with no step at the top (thin, then black but
 * for the sun's disc). An eye under the ground is taken at the ground. A
 * camera far outside, where the shell is a thin ring, is not drawn here:
 * that is the shell seen from space, a pass of its own not yet built.
 *
 * @par Several media
 * As many atmospheres as the caller makes, each with its own tables; which
 * one a scene draws is the caller's choice, set each frame.
 *
 * Out of scope: water, fog in a box or under a plane (a shell has no flat
 * boundary); the shell seen from outside; clouds.
 *
 * @par Threads
 * An atmosphere is one object for the library's rule (rbtcore.h,
 * "Threads"): one caller at a time.
 */

#if __cplusplus
extern "C" {
#endif

/** The fewest texels a side of the transmittance table has. */
#define RBTREND_ATMOSPHERE_MIN_TRANSMITTANCE_SIZE 16u
/** The most texels a side of the transmittance table has. */
#define RBTREND_ATMOSPHERE_MAX_TRANSMITTANCE_SIZE 1024u
/** The fewest texels a side of the multiple-scattering table has. */
#define RBTREND_ATMOSPHERE_MIN_MULTIPLE_SCATTERING_SIZE 8u
/** The most texels a side of the multiple-scattering table has. */
#define RBTREND_ATMOSPHERE_MAX_MULTIPLE_SCATTERING_SIZE 128u
/** The fewest steps a table's march takes. */
#define RBTREND_ATMOSPHERE_MIN_STEPS 4u
/** The most steps a table's march takes. */
#define RBTREND_ATMOSPHERE_MAX_STEPS 256u

/**
 * @brief Configuration descriptor for an atmosphere: its tables' sizes and
 * the steps their marches take. No field has a default.
 */
typedef struct RBTREND_ATMOSPHERE_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_ATMOSPHERE_DESC, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** The transmittance table's width (the direction's angle) and height
	 * (the height), each RBTREND_ATMOSPHERE_MIN_TRANSMITTANCE_SIZE to
	 * RBTREND_ATMOSPHERE_MAX_TRANSMITTANCE_SIZE. 256 x 64 is the paper's. */
	uint32_t transmittance_width;
	/** See #transmittance_width. */
	uint32_t transmittance_height;
	/** The multiple-scattering table's side, RBTREND_ATMOSPHERE_MIN_MULTIPLE_SCATTERING_SIZE
	 * to RBTREND_ATMOSPHERE_MAX_MULTIPLE_SCATTERING_SIZE. 32 is the paper's. */
	uint32_t multiple_scattering_size;
	/** The steps a transmittance texel's march takes, RBTREND_ATMOSPHERE_MIN_STEPS
	 * to RBTREND_ATMOSPHERE_MAX_STEPS. 40 is the paper's. */
	uint32_t transmittance_steps;
	/** The steps each of a multiple-scattering texel's 64 rays takes,
	 * RBTREND_ATMOSPHERE_MIN_STEPS to RBTREND_ATMOSPHERE_MAX_STEPS. 20 is the
	 * paper's. */
	uint32_t multiple_scattering_steps;
} RBTREND_ATMOSPHERE_DESC;

/**
 * @brief Creates an atmosphere with no medium: until
 * rbtrend_atmosphere_set_medium(), a scene that draws it draws no
 * atmosphere (transmittance 1, nothing scattered).
 *
 * Memory: its two tables, R16G16B16A16_FLOAT (8 bytes a texel): 136 KiB
 * at 256 x 64 and 32 x 32.
 *
 * @param[in]  instance       Instance the atmosphere is of.
 * @param[in]  desc           Its tables' sizes and steps. Only read during
 *                            the call.
 * @param[out] out_atmosphere Receives the atmosphere; NULL on failure.
 * @param[out] out_error      Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if an argument is NULL
 *         or a size or step count is out of its range, each its own check
 *         and message; the struct codes for `desc->base`;
 *         RBT_RESULT_OUT_OF_MEMORY; or RBT_RESULT_REND_GPU_FAILURE if a
 *         table can't be made.
 */
RBT_RESULT rbtrend_atmosphere_create(
	rbtrend_instance instance,
	const RBTREND_ATMOSPHERE_DESC *desc,
	rbtrend_atmosphere *out_atmosphere,
	RBTREND_ERROR *out_error);

/**
 * @brief Destroys an atmosphere once the GPU is past it (rbtrendcontext.h,
 * "Destroying"). No scene may draw it afterwards: set the scene's
 * atmosphere to another, or none, first. NULL does nothing.
 *
 * @param[in] atmosphere The atmosphere.
 */
void rbtrend_atmosphere_destroy(rbtrend_atmosphere atmosphere);

/**
 * @brief Sets what fills the shell. Copied. Its tables are rebuilt by the
 * next RBTREND_COMMAND_ATMOSPHERE_TABLES sent for it; until then a scene
 * reads the tables of the medium before (none: no atmosphere).
 *
 * @param[in]  atmosphere The atmosphere.
 * @param[in]  medium     The medium: each field in its range
 *                        (math/rbtmathmedium.h). Only read during the call.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument or
 *         a field out of its range, each field its own check and message,
 *         in the struct's order (Rabbittus Math's checks). Nothing changes
 *         on failure.
 */
RBT_RESULT rbtrend_atmosphere_set_medium(
	rbtrend_atmosphere atmosphere,
	const RBTMATH_SHELL_MEDIUM *medium,
	RBTREND_ERROR *out_error);

#if __cplusplus
}
#endif

#endif // RBTREND_ATMOSPHERE_H
