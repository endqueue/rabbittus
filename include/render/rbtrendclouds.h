#ifndef RBTREND_CLOUDS_H
#define RBTREND_CLOUDS_H

#include "rbtrendcommands.h"
#include "rbtrendlit.h"

#include "../math/rbtmathclouds.h"
#include "../math/rbtmathnoise.h"

/**
 * @file rbtrendclouds.h
 * @brief Rend's volumetric clouds: layers of cloud bounded by spherical
 * shells or flat slabs, their density Rabbittus Math's
 * (math/rbtmathclouds.h), marched against the scene's depth at a quarter of
 * the target's resolution and filled out by each view's own history, lit
 * through an atmosphere or by the scene's lights alone, composited with the
 * scene's air and fog in front.
 *
 * Clouds (an rbtrend_clouds) hold two noise volumes (Rabbittus Math's
 * PERLIN_WORLEY base and WORLEY detail, rbtmathnoise.h: the bytes
 * rbtmath_noise_volume_bake() makes, written by rbtrend_clouds_write_noise()),
 * coverage maps (six faces each, RBTMATH_CLOUD_TEXELS::coverage's bytes,
 * written by rbtrend_clouds_write_coverage()), looks in slots
 * (rbtrend_clouds_set_look(): Math's shape and how the cloud takes light),
 * and a record a view: the view's march at a quarter of the largest target
 * each way and its history at half, two of each (one read as last frame's,
 * one written as this frame's, in turn). Every one of them is made by
 * rbtrend_clouds_create(); nothing is made later.
 *
 * Each frame, RBTREND_COMMAND_LIT_CLOUDS (rbtrendcommands.h) draws one
 * scene's clouds: its layers (RBTREND_CLOUD_LAYER, each a look and a map by
 * slot, bounds, a frame and offsets) marched along each view ray from the
 * eye, a texel of each 4 x 4 block a frame, each sample's density Math's
 * definition, lit by the layer's directional lights through every layer
 * towards them (an upper deck shades a lower) and, where the layer names
 * one, its atmosphere's transmittance (a deck at sunset is lit red; a
 * shell's night side is dark), Wrenninge's octaves of multiple scattering,
 * and the scene's ambient light; integrated by Hillaire's energy-conserving
 * step. Each half-resolution texel of the history is then the march's where
 * it marched this frame and last frame's history reprojected by the cloud's
 * depth elsewhere, clamped to the march's neighbourhood. The history is put
 * over the picture at full resolution against its depth, with the scene's
 * aerial volume and its height fog in front of the clouds' depth:
 * picture x T + L.
 *
 * The frame's order: the lit surfaces (their sky drawn by the sky shader),
 * LIT_REFLECT, LIT_ATMOSPHERE_SHELLS, then LIT_CLOUDS, then the transparent
 * surfaces and the tone map. The clouds are over everything behind them
 * and under what is drawn in front: the march stops at the depth.
 *
 * @par Units and light
 * Lengths are the scene's unit (an atmosphere's tables in the same unit, as
 * rbtrendatmosphere.h has it); a directional light's illuminance is pi times
 * its radiance (rbtrendlit.h's scale). A density is Math's, unitless: a
 * look's `extinction` (per scene unit) turns it into the medium's.
 *
 * @par Inside and outside
 * Stage 1 draws clouds from under, inside and over a layer (a flight). A
 * shell layer seen from outside its top is marched by the same command, and
 * drawn as from inside: composited over what is behind it with the scene's
 * own air in front, none of its own world's around it, which is right only
 * where no shell is drawn over it (RBTREND_TODO.md item 55's stage 2 gives
 * clouds their place in LIT_ATMOSPHERE_SHELLS).
 *
 * Out of scope (RBTREND_TODO.md item 55's stages 2 and 3, and later): a
 * shell's clouds inside A55's shell pass; cloud shadows on the ground;
 * point and spot lights; a cloud's own motion in the history (reprojection
 * takes the clouds as still between frames; wind moves them slowly against
 * a frame).
 *
 * @par Threads
 * Clouds are one object for the library's rule (rbtcore.h, "Threads"): its
 * writes, its looks and every command naming it, one caller at a time with
 * the rest of its instance. A compile that draws them
 * (RBTREND_COMMAND_LIT_CLOUDS) enters its instance, and the records are read
 * and written only there.
 */

#if __cplusplus
extern "C" {
#endif

/** The most layers one RBTREND_COMMAND_LIT_CLOUDS marches. */
#define RBTREND_CLOUDS_MAX_LAYERS 4u
/** The most looks clouds hold (RBTREND_CLOUDS_DESC::max_looks). */
#define RBTREND_CLOUDS_MAX_LOOKS 16u
/** The most coverage maps clouds hold. */
#define RBTREND_CLOUDS_MAX_COVERAGE_MAPS 8u
/** The most views clouds keep a history for. */
#define RBTREND_CLOUDS_MAX_VIEWS 16u
/** The most samples a texel's march takes. */
#define RBTREND_CLOUDS_MAX_STEPS 256u
/** The most samples a light's march takes from a sample. */
#define RBTREND_CLOUDS_MAX_LIGHT_STEPS 16u
/** The most lights a layer names: the size of RBTREND_CLOUD_LAYER::_lights. */
#define RBTREND_CLOUDS_MAX_LAYER_LIGHTS 2u
/** The most octaves of multiple scattering a look takes. */
#define RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES 4u
/** The largest target side clouds are made for, in texels. */
#define RBTREND_CLOUDS_MAX_TARGET_SIDE 16384u

/**
 * @brief How clouds are made: every size and count, each texture made once,
 * here. No field has a default.
 */
typedef struct RBTREND_CLOUDS_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_CLOUDS_DESC, and its chain,
	 * which takes nothing (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** The base volume's side (Math's PERLIN_WORLEY), 1 to
	 * RBTMATH_NOISE_VOLUME_MAX_SIZE (Nubis's 128). */
	uint32_t base_noise_size;
	/** The detail volume's side (Math's WORLEY), 1 to
	 * RBTMATH_NOISE_VOLUME_MAX_SIZE (Nubis's 32). */
	uint32_t detail_noise_size;
	/** Each coverage map's side, 1 to RBTMATH_CLOUD_MAX_COVERAGE_SIZE. */
	uint32_t coverage_size;
	/** How many coverage maps, 1 to RBTREND_CLOUDS_MAX_COVERAGE_MAPS; each
	 * six faces (a slab's reads its first). */
	uint32_t coverage_maps;
	/** How many look slots, 1 to RBTREND_CLOUDS_MAX_LOOKS. */
	uint32_t max_looks;
	/** How many views it keeps a history for, 1 to RBTREND_CLOUDS_MAX_VIEWS:
	 * one for each scene drawn in a frame. */
	uint32_t max_views;
	/** The largest target a view draws into, in texels, 1 to
	 * RBTREND_CLOUDS_MAX_TARGET_SIDE: every record's textures are made at
	 * this size. */
	uint32_t max_target_width;
	/** See #max_target_width. */
	uint32_t max_target_height;
} RBTREND_CLOUDS_DESC;

/**
 * @brief Creates clouds: their volumes and maps (undefined until written),
 * their looks (none set: a layer naming an unset look is refused at the
 * compile), and @p desc's view records with their sets.
 *
 * Memory, all of it here and nothing later: the volumes (base^3 + detail^3,
 * four bytes a texel; 8 MiB + 128 KiB at Nubis's 128 and 32), the maps
 * (coverage_maps x 6 x size^2 x 4 bytes), the looks' block (2,560 bytes a
 * frame in flight: RBTREND_CLOUDS_MAX_LOOKS slots of 160), and a record a
 * view: (W/4 x H/4) x 12 + 2 x (W/2 x H/2) x 12 bytes at the largest target
 * W x H, each rounded up (23.7 MiB at 2560 x 1440), and its blocks (1,088
 * bytes, and 112 for each way its history runs, a frame in flight).
 *
 * @param[in]  instance   Instance the clouds are of.
 * @param[in]  desc       Their sizes. Only read during the call.
 * @param[out] out_clouds Receives them; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL or destroyed
 *         instance, a NULL desc or @p out_clouds, or a field out of its
 *         range, each its own check and message in member order; the struct
 *         codes for `desc->base`; RBT_RESULT_OUT_OF_MEMORY; or
 *         RBT_RESULT_REND_GPU_FAILURE if a texture, set or Rend's cloud
 *         pipelines can't be made.
 */
RBT_RESULT rbtrend_clouds_create(
	rbtrend_instance instance,
	const RBTREND_CLOUDS_DESC *desc,
	rbtrend_clouds *out_clouds,
	RBTREND_ERROR *out_error);

/**
 * @brief Destroys clouds once the GPU is past them (rbtrendcontext.h,
 * "Destroying"). No list may name them afterwards. NULL does nothing.
 *
 * @param[in] clouds The clouds.
 */
void rbtrend_clouds_destroy(rbtrend_clouds clouds);

/**
 * @brief Writes slices of a noise volume: bytes rbtmath_noise_volume_bake()
 * made, of the volume's kind and side. Recorded with the frame's batch and
 * staged, as rbtrend_texture3d_update(); the next submission carries it,
 * before any LIT_CLOUDS of that submission reads the volume. Until a slice
 * is written, a march reading it reads what the texture held (undefined).
 *
 * @param[in]  clouds      The clouds.
 * @param[in]  queue       As rbtrend_texture3d_update()'s.
 * @param[in]  kind        RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY for the
 *                         base, RBTMATH_NOISE_VOLUME_KIND_WORLEY for the
 *                         detail.
 * @param[in]  first_slice Below the volume's side.
 * @param[in]  slice_count 1 to the side less @p first_slice.
 * @param[in]  data        side x side x @p slice_count x 4 bytes. Only read
 *                         during the call.
 * @param[in]  data_size   Exactly that.
 * @param[out] out_error   Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL clouds, queue
 *         or data, a kind not one of the two, a slice range or size out of
 *         range, each its own check and message; otherwise as
 *         rbtrend_texture3d_update(). Nothing changes on failure.
 */
RBT_RESULT rbtrend_clouds_write_noise(
	rbtrend_clouds clouds,
	crgpu_command_queue queue,
	RBTMATH_NOISE_VOLUME_KIND kind,
	uint32_t first_slice,
	uint32_t slice_count,
	const void *data,
	uint64_t data_size,
	RBTREND_ERROR *out_error);

/**
 * @brief Writes a region of one face of a coverage map: four bytes a texel
 * (RBTMATH_CLOUD_TEXELS::coverage's meaning: R coverage, G type, B a
 * density factor, A unread), rows of @p width, tightly packed. Recorded and
 * staged as rbtrend_clouds_write_noise(). A frame in flight reads the map
 * as it was; the next submission's frame the region written.
 *
 * @param[in]  clouds    The clouds.
 * @param[in]  queue     As rbtrend_clouds_write_noise().
 * @param[in]  map       Below RBTREND_CLOUDS_DESC::coverage_maps.
 * @param[in]  face      0 to 5, in the order +X, -X, +Y, -Y, +Z, -Z (a
 *                       shell's); a slab reads face 0.
 * @param[in]  x         The region's left, in texels.
 * @param[in]  y         Its top.
 * @param[in]  width     Its width, 1 to the side less @p x.
 * @param[in]  height    Its height, 1 to the side less @p y.
 * @param[in]  data      @p width x @p height x 4 bytes. Only read during the
 *                       call.
 * @param[in]  data_size Exactly that.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return As rbtrend_clouds_write_noise(), each argument its own check.
 */
RBT_RESULT rbtrend_clouds_write_coverage(
	rbtrend_clouds clouds,
	crgpu_command_queue queue,
	uint32_t map,
	uint32_t face,
	uint32_t x,
	uint32_t y,
	uint32_t width,
	uint32_t height,
	const void *data,
	uint64_t data_size,
	RBTREND_ERROR *out_error);

/**
 * @brief A look: the density's shape (Rabbittus Math's) and how the cloud
 * takes light. No field has a default.
 */
typedef struct RBTREND_CLOUD_LOOK {
	/** This struct's type, RBT_STRUCT_TYPE_REND_CLOUD_LOOK, and its chain,
	 * which takes nothing. */
	RBT_STRUCT_BASE base;
	/** The density's shape: each field in its range (math/rbtmathclouds.h). */
	RBTMATH_CLOUD_SHAPE shape;
	/** The extinction at density 1, per scene unit: finite, above 0 (a
	 * cumulus about 0.05 per metre). */
	float extinction;
	/** The share of extinction that scatters, three channels: each finite,
	 * 0 to 1 (water clouds about 0.99; dust lower, tinted). */
	float albedo[3];
	/** The forward Henyey-Greenstein lobe's g: finite, 0 to 0.999. */
	float phase_forward;
	/** The backward lobe's g: finite, -0.999 to 0. */
	float phase_backward;
	/** The forward lobe's weight, the backward's 1 minus it: finite, 0 to 1. */
	float phase_blend;
	/** Octaves of multiple scattering (Wrenninge's), 1 (single scattering)
	 * to RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES. */
	uint32_t scattering_octaves;
	/** Each octave's extinction over the one before (a): finite, 0 to 1.
	 * Read with more than one octave, else must be 0. */
	float octave_extinction;
	/** Each octave's light over the one before (b): finite, 0 to 1, at most
	 * #octave_extinction (energy kept). Read as #octave_extinction. */
	float octave_scattering;
	/** Each octave's phase g over the one before (c): finite, 0 to 1. Read
	 * as #octave_extinction. */
	float octave_phase;
	/** The scene's ambient light's share: finite, at least 0 (1 as a lit
	 * surface takes it). */
	float ambient_scale;
} RBTREND_CLOUD_LOOK;

/**
 * @brief Sets look @p index. Copied; a frame in flight draws the look it
 * had, the next submission's frame this one.
 *
 * @param[in]  clouds    The clouds.
 * @param[in]  index     Below RBTREND_CLOUDS_DESC::max_looks.
 * @param[in]  look      The look: each field in its range. Only read during
 *                       the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument, an
 *         index out of range or a field out of its range (Math's shape
 *         checked by Math's rules), each its own check and message; the
 *         struct codes for `look->base`. Nothing changes on failure.
 */
RBT_RESULT rbtrend_clouds_set_look(
	rbtrend_clouds clouds,
	uint32_t index,
	const RBTREND_CLOUD_LOOK *look,
	RBTREND_ERROR *out_error);

/**
 * @brief One layer of an RBTREND_COMMAND_LIT_CLOUDS: where it is this frame,
 * its look and map by slot, where its tiling sits, and what lights it.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_CLOUD_LAYER {
	/** Its look's slot (rbtrend_clouds_set_look()), set. */
	uint32_t _look;
	/** Its coverage map, below RBTREND_CLOUDS_DESC::coverage_maps. */
	uint32_t _coverage_map;
	/** Its bounds and its map's lie (math/rbtmathclouds.h), relative to
	 * #_origin: each field in its range. */
	RBTMATH_CLOUD_BOUNDS _bounds;
	/** The bounds' origin in the scene's space this frame (a shell's
	 * centre, a point of a slab's base plane): finite. */
	float _origin[3];
	/** The layer's own frame this frame: its x, y and z axes in the scene's
	 * space, three rows, each finite, orthonormal within 1e-4. A point p of
	 * the scene is read as Math's point axes x (p - #_origin): its bounds,
	 * map and noise turn with it (a world's rotation). */
	float _axes[9];
	/** Where the tiling sits at #_origin this frame (wind, the caller's place
	 * in the tiling): each in its range. */
	RBTMATH_CLOUD_OFFSETS _offsets;
	/** How many of #_lights light it: 0 to RBTREND_CLOUDS_MAX_LAYER_LIGHTS
	 * (0: the ambient light alone). */
	uint32_t _light_count;
	/** Its lights, by their places in the scene's light list: each a
	 * directional light. A point or spot light is refused. Entries past
	 * #_light_count are not read. */
	uint32_t _lights[RBTREND_CLOUDS_MAX_LAYER_LIGHTS];
	/** An atmosphere its lights reach it through (rbtrendatmosphere.h), of
	 * the scene's instance, outliving the list's submission; NULL for none
	 * (the lights as they are). One whose tables were never built is taken
	 * as none. */
	rbtrend_atmosphere _atmosphere;
	/** That atmosphere's centre in the scene's space this frame: finite.
	 * Not read with no #_atmosphere. */
	float _atmosphere_center[3];
} RBTREND_CLOUD_LAYER;

#if __cplusplus
}
#endif

#endif // RBTREND_CLOUDS_H
