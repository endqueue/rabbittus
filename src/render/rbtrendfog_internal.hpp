#ifndef RBTREND_FOG_INTERNAL_HPP
#define RBTREND_FOG_INTERNAL_HPP

/*
 * Rend's volumetric fog near the eye (rbtrendfog.h; card A59's stage 2,
 * RBTREND_TODO.md item 56): the handle, a record a view, the blocks and
 * push constants its passes read, and what RBTREND_COMMAND_LIT_FOG's compile
 * (rbtrendcommands.cpp) asks of it.
 *
 * Who writes what, when: everything here is made by rbtrend_fog_create()
 * and nothing later. A view's records (which scene each draws, when, its
 * last view-projection) are the history module's (rbtrendhistory.hpp),
 * read and written only by a compile, which enters the instance. Each
 * frame a LIT_FOG writes its record's block for the way its history runs
 * this frame (one material each way), which the submission copies into the
 * block's place on the GPU in order with the frames before; the cull
 * writes the record's tiles, the inject one history grid while it reads
 * the other, the integrate the integrated grid, the apply reads it: four
 * passes of one list, each ordered after the one before by the list's
 * tracked states (storage images and buffers written by a dispatch are
 * barriered before the next reads them).
 */

#include "rbtrend_internal.hpp"
#include "rbtrendhistory.hpp"
#include "rbtrendlit_internal.hpp"
#include "render/rbtrendfog.h"
#include "../rbtoverlap.hpp"
#include "../rbtplaindata.hpp"
#include "../rbtstale.hpp"

#include <cstdint>

/* RbtrendFogVolume in shaders/rbtrend_fog_bindings.slang: a volume of the
 * command resolved at the compile, eight float4. */
struct rbtrend_fog_volume_block {
	float to_volume[3][4];
	float half_shape[4];
	float params[4];
	float noise_offset[4];
	float albedo[4];
	float sphere[4];
};
RBT_PLAIN_DATA(rbtrend_fog_volume_block);
static_assert(sizeof(rbtrend_fog_volume_block) == 128, "RbtrendFogVolume in rbtrend_fog_bindings.slang: 128 bytes");

/* RbtrendFog in shaders/rbtrend_fog_bindings.slang: a view record's block,
 * written by each LIT_FOG's compile for the way its history runs. Its
 * members' offsets are checked against the shader's reflection when the
 * layout is made. */
struct rbtrend_fog_block {
	float last_view_projection[16];
	float last_eye[4];
	float shift[4];
	float range[4];
	float last_range[4];
	uint32_t grid[4];
	float history[4];
	rbtrend_fog_volume_block volumes[RBTREND_FOG_MAX_VOLUMES];
};
RBT_PLAIN_DATA(rbtrend_fog_block);
static_assert(sizeof(rbtrend_fog_block) == 160 + 128 * RBTREND_FOG_MAX_VOLUMES, "RbtrendFog in rbtrend_fog_bindings.slang");

/* FogIntegrateParams in shaders/rbtrend_fog_integrate_comp.slang. */
struct rbtrend_fog_integrate_params {
	uint32_t grid[4];
	float range[4];
};
RBT_PLAIN_DATA(rbtrend_fog_integrate_params);
static_assert(sizeof(rbtrend_fog_integrate_params) == 32, "FogIntegrateParams in rbtrend_fog_integrate_comp.slang");

/* FogApplyParams in shaders/rbtrend_fog_apply_frag.slang. */
struct rbtrend_fog_apply_params {
	float range[4];
	uint32_t flags[4];
};
RBT_PLAIN_DATA(rbtrend_fog_apply_params);
static_assert(sizeof(rbtrend_fog_apply_params) == 32, "FogApplyParams in rbtrend_fog_apply_frag.slang");

/* The words of a tile's list in a record's tiles: a count, then up to
 * RBTREND_FOG_MAX_TILE_VOLUMES indices. */
#define RBTREND_FOG_TILE_WORDS (RBTREND_FOG_MAX_TILE_VOLUMES + 1u)

/* A volume is copied into the command list at the send and read at the
 * compile (rbtrendcommands.cpp). */
RBT_PLAIN_DATA(RBTREND_FOG_VOLUME_INSTANCE);

/* What a view's record holds beside its history record (by the same
 * index): its three grids (the history two, by the way it runs, and the
 * integrated grid), each a 3D texture with a view to sample and a view to
 * write; its tiles' lists; the cull and inject passes' set 1 for each way
 * its history runs (a material, its block this frame's data) and the
 * integrate pass's set; and what last frame's history was drawn with:
 * which of the two grids was written, the eye, the range, how many frames
 * of the current history it holds (its jitter's step, from 0 at each
 * reset). Plain handles and numbers: calloc'd with the fog. */
struct rbtrend_fog_view {
	rbtrend_texture3d grids[3];
	rbtrend_texture_view sampled[3];
	rbtrend_texture_view storage[3];
	rbtrend_buffer tiles;
	rbtrend_material materials[2];
	rbtrend_binding_set integrate_sets[2];
	uint32_t frames;
	uint32_t last_written;
	float last_eye[3];
	float last_range[4];
};

typedef struct rbtrend_fog_t {
	char *debug_name{nullptr}; /* first, as every handle's: rbt_debug_name_set() writes it (rbtcore.h) */
	rbtrend_fog_t() = default;
	~rbtrend_fog_t();
	rbtrend_instance instance{nullptr};
	/* The desc as given; its debug_name not kept (the handle's own copy is). */
	RBTREND_FOG_DESC desc{};
	/* The WORLEY volume, RGBA8 at noise_size^3, and its view. */
	rbtrend_texture3d noise{nullptr};
	rbtrend_texture_view noise_view{nullptr};
	/* A record a view: the history module's and the fog's own, by index,
	 * desc.max_views of each; calloc'd. The clock is each take's `used`. */
	rbtrend_history_record *records{nullptr};
	rbtrend_fog_view *views{nullptr};
	uint64_t clock{0};
	rbt_overlap entered{};
	RBT_STALE_TAG
} rbtrend_fog_t;

#define RBTREND_ENTER_FOG(fog, who) RBT_ENTER((fog)->entered, "rbtrend", who, "fog")

/* The fog's passes: their shaders, layouts and pipelines (in
 * rbtrend_lit_builtins), made if they aren't. False, reported, if they
 * can't be. */
bool rbtrend_lit_fog_prepare(rbtrend_instance instance);

/* The apply pass's pipeline rendering onto [format], made if it isn't:
 * colour ONE + destination SRC_ALPHA, alpha ZERO + destination ONE. NULL,
 * reported, if it can't be. */
rbtrend_graphics_pipeline rbtrend_lit_fog_apply_pipeline(rbtrend_instance instance, CRGPU_FORMAT format);

/* [fog]'s block of view [index] written for the way its history runs this
 * frame, [direction]: into that material's block. */
void rbtrend_fog_write_block(rbtrend_fog fog, uint32_t index, uint32_t direction, const rbtrend_fog_block &block);

#endif // RBTREND_FOG_INTERNAL_HPP
