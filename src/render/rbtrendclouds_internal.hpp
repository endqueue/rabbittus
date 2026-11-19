#ifndef RBTREND_CLOUDS_INTERNAL_HPP
#define RBTREND_CLOUDS_INTERNAL_HPP

/*
 * Rend's volumetric clouds (rbtrendclouds.h; card A57's stage 1,
 * RBTREND_TODO.md item 55): the handle, a record a view, the blocks and push
 * constants its passes read, and what RBTREND_COMMAND_LIT_CLOUDS's compile
 * (rbtrendcommands.cpp) asks of it.
 *
 * Who writes what, when: everything here is made by rbtrend_clouds_create()
 * and nothing later. The looks' block is the clouds' material's, written by
 * rbtrend_clouds_set_look() into the material's bytes, which the next
 * submission copies into the block's place on the GPU in order with the
 * frames before (a frame in flight keeps the look it had). A view's records
 * (which scene each draws, when, its last view-projection) are the history
 * module's (rbtrendhistory.hpp), read and written only by a compile, which
 * enters the instance. Each frame a LIT_CLOUDS writes its record's march
 * block and the history block of the way its history runs this frame; the
 * march writes the record's march textures, the reconstruct one history
 * while it reads the other, the composite reads the history written: three
 * passes of one list, each ordered after the one before by the list's
 * tracked states.
 */

#include "rbtrend_internal.hpp"
#include "rbtrendhistory.hpp"
#include "rbtrendlit_internal.hpp"
#include "render/rbtrendclouds.h"
#include "../rbtoverlap.hpp"
#include "../rbtplaindata.hpp"
#include "../rbtstale.hpp"

#include <cstdint>

/* CloudLook in shaders/rbtrend_clouds_model.slang: a look resolved, ten
 * float4 (one of them spare). */
struct rbtrend_clouds_look_block {
	float profiles[4][4];
	float tiles[4];
	float shape[4];
	float albedo[4];
	float phase[4];
	float octave[4];
	float spare[4];
};
RBT_PLAIN_DATA(rbtrend_clouds_look_block);
static_assert(sizeof(rbtrend_clouds_look_block) == 160, "CloudLook in rbtrend_clouds_model.slang");

/* CloudLayer in shaders/rbtrend_clouds_model.slang: a layer resolved at the
 * compile, sixteen float4. */
struct rbtrend_clouds_layer_block {
	float axes[3][4];
	float origin_kind[4];
	float bounds[4];
	float up_look[4];
	float map_axis_layer[4];
	float base_offset[4];
	float detail_offset[4];
	float light_direction[2][4];
	float light_illuminance[2][4];
	float atmosphere[4];
	float atmosphere_radii[4];
	float spare[4];
};
RBT_PLAIN_DATA(rbtrend_clouds_layer_block);
static_assert(sizeof(rbtrend_clouds_layer_block) == 256, "CloudLayer in rbtrend_clouds_model.slang");

/* RbtrendCloudsLooks in shaders/rbtrend_clouds_bindings.slang. */
struct rbtrend_clouds_looks_block {
	rbtrend_clouds_look_block looks[RBTREND_CLOUDS_MAX_LOOKS];
};
RBT_PLAIN_DATA(rbtrend_clouds_looks_block);

/* RbtrendCloudsMarch in shaders/rbtrend_clouds_bindings.slang: a record's
 * frame, written by each LIT_CLOUDS's compile. */
struct rbtrend_clouds_march_block {
	uint32_t sizes[4];
	float target[4];
	uint32_t counts[4];
	float jitter[4];
	rbtrend_clouds_layer_block layers[RBTREND_CLOUDS_MAX_LAYERS];
};
RBT_PLAIN_DATA(rbtrend_clouds_march_block);
static_assert(sizeof(rbtrend_clouds_march_block) == 64 + 256 * RBTREND_CLOUDS_MAX_LAYERS, "RbtrendCloudsMarch in rbtrend_clouds_bindings.slang");

/* RbtrendCloudsHistory in shaders/rbtrend_clouds_reconstruct_comp.slang. */
struct rbtrend_clouds_history_block {
	float last_view_projection[16];
	float shift[4];
	uint32_t sizes[4];
	uint32_t frame[4];
};
RBT_PLAIN_DATA(rbtrend_clouds_history_block);
static_assert(sizeof(rbtrend_clouds_history_block) == 112, "RbtrendCloudsHistory in rbtrend_clouds_reconstruct_comp.slang");

/* CloudsCompositeParams in shaders/rbtrend_clouds_composite_frag.slang. */
struct rbtrend_clouds_composite_params {
	uint32_t sizes[4];
};
RBT_PLAIN_DATA(rbtrend_clouds_composite_params);

/* A layer is copied into the command list at the send and read at the
 * compile (rbtrendcommands.cpp). */
RBT_PLAIN_DATA(RBTREND_CLOUD_LAYER);

/* What a view's record holds beside its history record (by the same
 * index): its march (light and transmittance, and depth) and its two
 * histories of each, every texture with a view to sample and a view to
 * write; the march's set 3 (a material: its block this frame's data); the
 * reconstruct's set 1 for each way its history runs (a material, its block
 * this frame's) and the composite's set 2 for each; and what last frame's
 * history was drawn with: which of the two was written, the target's size,
 * and how many frames the current history holds (from 0 at each reset).
 * Plain handles and numbers: calloc'd with the clouds. */
struct rbtrend_clouds_view {
	rbtrend_texture2d textures[6]; /* march, march depth, history 0, history 0 depth, history 1, history 1 depth */
	rbtrend_texture_view sampled[6];
	rbtrend_texture_view storage[6];
	rbtrend_material march;
	rbtrend_material reconstruct[2];
	rbtrend_binding_set composite[2];
	uint32_t frames;
	uint32_t last_written;
	uint32_t last_size[2];
};

typedef struct rbtrend_clouds_t {
	char *debug_name{nullptr}; /* first, as every handle's: rbt_debug_name_set() writes it (rbtcore.h) */
	rbtrend_clouds_t() = default;
	~rbtrend_clouds_t();
	rbtrend_instance instance{nullptr};
	/* The desc as given; its debug_name not kept (the handle's own copy is). */
	RBTREND_CLOUDS_DESC desc{};
	/* The two volumes, RGBA8 each, the maps' array (six layers a map), and
	 * their views. */
	rbtrend_texture3d base{nullptr};
	rbtrend_texture3d detail{nullptr};
	rbtrend_texture2d maps{nullptr};
	rbtrend_texture_view base_view{nullptr};
	rbtrend_texture_view detail_view{nullptr};
	rbtrend_texture_view maps_view{nullptr};
	/* The march's set 2: the looks' block and the volumes and maps. */
	rbtrend_material material{nullptr};
	/* Which looks have been set: a bit each. */
	uint32_t looks_set{0};
	/* The looks as set, the march's block's source. */
	rbtrend_clouds_looks_block looks{};
	/* The march sizes at the desc's largest target. */
	uint32_t march_size[2]{};
	uint32_t history_size[2]{};
	/* A record a view: the history module's and the clouds' own, by index,
	 * desc.max_views of each; calloc'd. The clock is each take's `used`. */
	rbtrend_history_record *records{nullptr};
	rbtrend_clouds_view *views{nullptr};
	uint64_t clock{0};
	rbt_overlap entered{};
	RBT_STALE_TAG
} rbtrend_clouds_t;

#define RBTREND_ENTER_CLOUDS(clouds, who) RBT_ENTER((clouds)->entered, "rbtrend", who, "clouds")

/* The clouds' passes: their shaders, layouts and pipelines (in
 * rbtrend_lit_builtins), made if they aren't. False, reported, if they
 * can't be. */
bool rbtrend_lit_clouds_prepare(rbtrend_instance instance);

/* The composite's pipeline rendering onto [format], made if it isn't:
 * colour ONE + destination SRC_ALPHA, alpha ZERO + destination ONE. NULL,
 * reported, if it can't be. */
rbtrend_graphics_pipeline rbtrend_lit_clouds_composite_pipeline(rbtrend_instance instance, CRGPU_FORMAT format);

/* [material]'s block written whole with [bytes] of [size]. */
void rbtrend_clouds_write_material_block(rbtrend_material material, const void *bytes, size_t size);

#endif // RBTREND_CLOUDS_INTERNAL_HPP
