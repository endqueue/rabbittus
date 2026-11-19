#ifndef RBTREND_FOG_H
#define RBTREND_FOG_H

#include "rbtrendcommands.h"

#include "../math/rbtmathfog.h"

/**
 * @file rbtrendfog.h
 * @brief Rend's volumetric fog near the eye: a froxel grid over each view
 * it draws, fed by the scene's height fog and local fog volumes, lit by the
 * scene's directional lights through their shadows, kept with a history,
 * integrated, and put over the picture against depth.
 *
 * A fog (an rbtrend_fog) holds a WORLEY noise volume (Rabbittus Math's,
 * rbtmathnoise.h: the bytes rbtmath_noise_volume_bake() makes, written by
 * rbtrend_fog_write_noise()) that its volumes are broken up by, and a
 * record a view: a grid of froxels across, down and in depth of the view
 * (the desc's sizes), its history (two grids, one read as last frame's and
 * one written as this frame's, in turn) and the grid integrated from the
 * eye. Every one of them is made by rbtrend_fog_create(); nothing is made
 * later.
 *
 * Each frame, RBTREND_COMMAND_LIT_FOG (rbtrendcommands.h) draws one scene's
 * grid: the scene's height fog (rbtrend_lit_scene_set_fog(), rbtrendlit.h)
 * and the command's volumes (RBTREND_FOG_VOLUME_INSTANCE, Rabbittus Math's
 * RBTMATH_FOG_VOLUME placed in the scene) are injected into the froxels
 * between the command's near and far distances (slices spaced
 * exponentially, the first reaching back to the eye), each froxel lit by
 * the fog's suns (the scene's first directional lights, its
 * `sun_count`) through their cascaded and map shadows and the scene
 * atmosphere's transmittance, and by the scene's ambient light; blended
 * with the history of the same scene, integrated front to back
 * (Hillaire's energy-conserving step), and put over the picture at each
 * texel's depth: picture x T + L.
 *
 * The scene's own fog (stage 1, applied by the lit shaders) starts where
 * the grid ends: a caller drawing the grid sets the scene fog's
 * `start_distance` to the command's `_far_distance`, so the lit shaders
 * fog what lies beyond the grid and the grid what lies inside it.
 *
 * Every density is Rabbittus Math's by its definition (rbtmathfog.h: a
 * height fog's density, a volume's with its fade and noise), worked out
 * on the GPU in float to a tolerance; the light is the fog's albedo (the
 * scene fog's, or a volume's own) times the light that reaches the froxel,
 * through the scene fog's Henyey-Greenstein phase.
 *
 * Out of scope (RBTREND_TODO.md item 56's stage 3): point and spot lights
 * in the grid, and transparent surfaces reading it (they see the scene's
 * fog beyond the grid, not the grid); traced shadows (a directional light
 * whose shadow is a traced mask is unshadowed in the grid: a mask is the
 * screen's, not a volume's).
 *
 * @par Threads
 * A fog is one object for the library's rule (rbtcore.h, "Threads"): one
 * caller at a time. A compile that draws it (RBTREND_COMMAND_LIT_FOG)
 * enters its instance, and the fog's records are read and written only
 * there.
 */

#if __cplusplus
extern "C" {
#endif

/** The most volumes one RBTREND_COMMAND_LIT_FOG takes. */
#define RBTREND_FOG_MAX_VOLUMES 64u
/** The most volumes a tile's list holds; more meeting a tile are left out
 * of it, the later in the command's order first (as the lit scene's tiles
 * leave out lights). */
#define RBTREND_FOG_MAX_TILE_VOLUMES 15u
/** The most views a fog keeps a grid for. */
#define RBTREND_FOG_MAX_VIEWS 16u
/** The most froxels across or down a grid. */
#define RBTREND_FOG_MAX_GRID_SIDE 512u
/** The most slices a grid has. */
#define RBTREND_FOG_MAX_GRID_SLICES 256u

/**
 * @brief How a fog is made: its grid's sizes and its views. No field has a
 * default.
 */
typedef struct RBTREND_FOG_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_FOG_DESC, and its chain,
	 * which takes nothing (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** Froxels across a view, 1 to RBTREND_FOG_MAX_GRID_SIDE (160 is
	 * 16-texel tiles at 2560 texels). */
	uint32_t grid_width;
	/** Froxels down a view, 1 to RBTREND_FOG_MAX_GRID_SIDE. */
	uint32_t grid_height;
	/** Slices in depth, 1 to RBTREND_FOG_MAX_GRID_SLICES (64 is a good start). */
	uint32_t grid_slices;
	/** How many views it keeps a grid for, 1 to RBTREND_FOG_MAX_VIEWS. */
	uint32_t max_views;
	/** Its WORLEY noise volume's side (rbtmathnoise.h), 1 to
	 * RBTMATH_NOISE_VOLUME_MAX_SIZE. */
	uint32_t noise_size;
} RBTREND_FOG_DESC;

/**
 * @brief Creates a fog: its noise volume (undefined until written) and a
 * record a view, each with its three grids and its sets.
 *
 * Memory, all of it here and nothing later: the noise volume,
 * `noise_size`^3 x 4 bytes (128 KiB at 32); a view's grids, 3 x W x H x S x
 * 8 bytes (21.1 MiB at 160 x 90 x 64), its tiles' volume lists, W x H x 64
 * bytes (0.9 MiB), and its two blocks of RBTREND_FOG_MAX_VOLUMES x 128 bytes
 * and 144 more (8.1 KiB each); the descriptors of four sets a view (two of
 * seven, two of two).
 *
 * @param[in]  instance  Instance the fog is of.
 * @param[in]  desc      Its sizes. Only read during the call.
 * @param[out] out_fog   Receives the fog; NULL on failure.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if an argument is NULL
 *         or a size is out of its range, each its own check and message in
 *         member order; the struct codes for `desc->base`;
 *         RBT_RESULT_OUT_OF_MEMORY; or RBT_RESULT_REND_GPU_FAILURE if a
 *         texture, buffer, set or Rend's fog pipelines can't be made.
 */
RBT_RESULT rbtrend_fog_create(
	rbtrend_instance instance,
	const RBTREND_FOG_DESC *desc,
	rbtrend_fog *out_fog,
	RBTREND_ERROR *out_error);

/**
 * @brief Destroys a fog once the GPU is past it (rbtrendcontext.h,
 * "Destroying"). No list may draw it afterwards. NULL does nothing.
 *
 * @param[in] fog The fog.
 */
void rbtrend_fog_destroy(rbtrend_fog fog);

/**
 * @brief Writes slices of the fog's noise volume: bytes
 * rbtmath_noise_volume_bake() made, of kind
 * RBTMATH_NOISE_VOLUME_KIND_WORLEY and the desc's `noise_size`. Recorded,
 * staged and submitted with the frame's batch as rbtrend_texture3d_update().
 * Until a slice is written, a volume reading it reads what the texture
 * held (undefined).
 *
 * @param[in]  fog         The fog.
 * @param[in]  queue       As rbtrend_texture3d_update()'s.
 * @param[in]  first_slice Below the side.
 * @param[in]  slice_count 1 to the side less @p first_slice.
 * @param[in]  data        side x side x @p slice_count x 4 bytes.
 * @param[in]  data_size   Exactly that.
 * @param[out] out_error   Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL fog, queue
 *         or data, a slice or count out of range or a size not the
 *         slices', each its own check and message; otherwise as
 *         rbtrend_texture3d_update().
 */
RBT_RESULT rbtrend_fog_write_noise(
	rbtrend_fog fog,
	crgpu_command_queue queue,
	uint32_t first_slice,
	uint32_t slice_count,
	const void *data,
	uint64_t data_size,
	RBTREND_ERROR *out_error);

/**
 * @brief One local fog volume of an RBTREND_COMMAND_LIT_FOG: Rabbittus
 * Math's volume, where its frame is, its noise's place, and its albedo.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_FOG_VOLUME_INSTANCE {
	/** The volume (math/rbtmathfog.h): each field in its range. */
	RBTMATH_FOG_VOLUME _volume;
	/** The scene's space into the volume's frame, a 3 x 4 matrix of rows
	 * (x' = row 0 . (x, y, z, 1), and so on): a rotation and a move, finite,
	 * its 3 x 3 part's rows unit and square to each other within 1e-3 (a
	 * scale is the volume's half sizes'). */
	float _to_volume[12];
	/** The noise's coordinate at the volume's origin this frame, in tiles:
	 * each finite, within RBTMATH_NOISE_VOLUME_MAX_COORDINATE. */
	float _noise_offset[3];
	/** The share of its extinction that scatters: each finite, 0 to 1. */
	float _albedo[3];
} RBTREND_FOG_VOLUME_INSTANCE;

#if __cplusplus
}
#endif

#endif // RBTREND_FOG_H
