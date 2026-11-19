#include "rbtrendfog_internal.hpp"

#include "rbtrendimages_internal.hpp"

#include "math/rbtmathnoise.h"

#include <cstdlib>
#include <cstring>

// ---- The handle ------------------------------------------------------------------------

rbtrend_fog_t::~rbtrend_fog_t() {
	// Each texture, view, buffer, material and set goes through the release
	// queue on its own; the rest is the CPU's.
	if (this->views) {
		for (uint32_t v = 0; v < this->desc.max_views; ++v) {
			rbtrend_fog_view &view = this->views[v];
			for (uint32_t d = 0; d < 2; ++d) {
				rbtrend_binding_set_destroy(view.integrate_sets[d]);
				rbtrend_material_destroy(view.materials[d]);
			}
			rbtrend_buffer_destroy(view.tiles);
			for (uint32_t g = 0; g < 3; ++g) {
				rbtrend_texture_view_destroy(view.storage[g]);
				rbtrend_texture_view_destroy(view.sampled[g]);
				rbtrend_texture3d_destroy(view.grids[g]);
			}
		}
	}
	free(this->views);
	free(this->records);
	rbtrend_texture_view_destroy(this->noise_view);
	rbtrend_texture3d_destroy(this->noise);
	// The desc's name is the copy made at creation; the handle's may have
	// been set by the caller since (RBT_SET_DEBUG_NAME), and isn't ours.
	free((char *)this->desc.debug_name);
}

// A 3D texture of [format] at [w] x [h] x [d], sampled and, with
// [storage], written by compute.
static rbtrend_texture3d rbtrend_fog_texture(rbtrend_instance instance, uint32_t w, uint32_t h, uint32_t d, CRGPU_FORMAT format, bool storage) {
	RBTREND_TEXTURE3D_DESC desc{};
	desc.base       = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE3D_DESC);
	desc.width      = w;
	desc.height     = h;
	desc.depth      = d;
	desc.format     = format;
	desc.usage      = RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE | (storage ? RBTREND_TEXTURE_USAGE_BIT_STORAGE : 0);
	desc.mip_levels = 1;
	rbtrend_texture3d made = nullptr;
	(void)rbtrend_texture3d_create(instance, &desc, &made, nullptr);
	return made;
}

// A whole view of [texture] of [type].
static rbtrend_texture_view rbtrend_fog_texture_view(rbtrend_texture3d texture, RBTREND_TEXTURE_VIEW_TYPE type) {
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base        = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = type;
	vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_3D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.mip_count   = 1;
	vd.layer_count = 1;
	rbtrend_texture_view made = nullptr;
	(void)rbtrend_texture_view_create_3d(texture, &vd, &made, nullptr);
	return made;
}

// View [index]'s grids, tiles, materials and sets. False if one can't be
// made (what was made is the destructor's).
static bool rbtrend_fog_make_view(rbtrend_fog fog, uint32_t index) {
	rbtrend_instance instance       = fog->instance;
	const rbtrend_lit_builtins &lit = instance->builtins.lit;
	const RBTREND_FOG_DESC &d       = fog->desc;
	rbtrend_fog_view &view          = fog->views[index];
	for (uint32_t g = 0; g < 3; ++g) {
		view.grids[g] = rbtrend_fog_texture(instance, d.grid_width, d.grid_height, d.grid_slices, CRGPU_FORMAT_R16G16B16A16_FLOAT, true);
		if (!view.grids[g])
			return false;
		view.sampled[g] = rbtrend_fog_texture_view(view.grids[g], RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE);
		view.storage[g] = rbtrend_fog_texture_view(view.grids[g], RBTREND_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS);
		if (!view.sampled[g] || !view.storage[g])
			return false;
	}
	RBTREND_BUFFER_DESC bd{};
	bd.base       = rbt_struct_base(RBT_STRUCT_TYPE_REND_BUFFER_DESC);
	bd.size       = (uint64_t)d.grid_width * d.grid_height * RBTREND_FOG_TILE_WORDS * sizeof(uint32_t);
	bd.usage      = RBTREND_BUFFER_USAGE_STORAGE;
	bd.writes     = RBTREND_BUFFER_WRITES_ONCE;
	bd.per_frame  = false;
	bd.debug_name = "rbtrend fog tiles";
	if (rbtrend_buffer_create(instance, &bd, &view.tiles, nullptr) != RBT_RESULT_OK)
		return false;
	// Way [w] writes grid w and reads the other; its integration reads
	// grid w and writes the integrated grid, 2.
	for (uint32_t w = 0; w < 2; ++w) {
		RBTREND_MATERIAL_DESC md{};
		md.base    = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
		md._layout = lit.fog_layout;
		if (rbtrend_material_create(instance, &md, &view.materials[w], nullptr) != RBT_RESULT_OK)
			return false;
		rbtrend_material m = view.materials[w];
		if (rbtrend_material_set_texture(m, "fog_noise", 0, fog->noise_view, nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_sampler(m, "fog_noise_sampler", 0, lit.material_sampler, nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "fog_history", 0, view.sampled[1 - w], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_sampler(m, "fog_history_sampler", 0, lit.environment_sampler, nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "fog_written", 0, view.storage[w], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_storage_buffer(m, "FogTiles", 0, view.tiles, nullptr) != RBT_RESULT_OK)
			return false;
		RBTREND_BINDING_SET_ENTRY entries[2]{};
		entries[0]._binding                  = 0;
		entries[0]._type                     = CRGPU_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		entries[0]._resource._image._view    = view.sampled[w];
		entries[1]._binding                  = 1;
		entries[1]._type                     = CRGPU_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		entries[1]._resource._image._view    = view.storage[2];
		if (rbtrend_binding_set_create(lit.fog_integrate_layout, entries, 2, &view.integrate_sets[w], nullptr) != RBT_RESULT_OK)
			return false;
	}
	return true;
}

void rbtrend_fog_write_block(rbtrend_fog fog, uint32_t index, uint32_t direction, const rbtrend_fog_block &block) {
	rbtrend_material material = fog->views[index].materials[direction];
	for (uint32_t i = 0; i < material->slot_count; ++i) {
		if (material->slots[i].bytes) {
			memcpy(material->slots[i].bytes, &block, sizeof(block));
			material->slots[i].dirty = true;
		}
	}
}

extern "C" {

RBT_RESULT rbtrend_fog_create(rbtrend_instance instance, const RBTREND_FOG_DESC *desc, rbtrend_fog *out_fog, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_fog)
		*out_fog = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_fog_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_fog_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_FOG_DESC))
		return base;
	if (desc->grid_width == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "grid_width is 0");
	if (desc->grid_width > RBTREND_FOG_MAX_GRID_SIDE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "grid_width %u is above RBTREND_FOG_MAX_GRID_SIDE, %u", desc->grid_width,
		                    RBTREND_FOG_MAX_GRID_SIDE);
	if (desc->grid_height == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "grid_height is 0");
	if (desc->grid_height > RBTREND_FOG_MAX_GRID_SIDE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "grid_height %u is above RBTREND_FOG_MAX_GRID_SIDE, %u", desc->grid_height,
		                    RBTREND_FOG_MAX_GRID_SIDE);
	if (desc->grid_slices == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "grid_slices is 0");
	if (desc->grid_slices > RBTREND_FOG_MAX_GRID_SLICES)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "grid_slices %u is above RBTREND_FOG_MAX_GRID_SLICES, %u", desc->grid_slices,
		                    RBTREND_FOG_MAX_GRID_SLICES);
	if (desc->max_views == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_views is 0");
	if (desc->max_views > RBTREND_FOG_MAX_VIEWS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_views %u is above RBTREND_FOG_MAX_VIEWS, %u", desc->max_views, RBTREND_FOG_MAX_VIEWS);
	if (desc->noise_size == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise_size is 0");
	if (desc->noise_size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise_size %u is above RBTMATH_NOISE_VOLUME_MAX_SIZE, %u", desc->noise_size,
		                    RBTMATH_NOISE_VOLUME_MAX_SIZE);
	if (!out_fog)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_fog is NULL");
	if (!rbtrend_lit_fog_prepare(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's fog passes can't be made");

	char *name = nullptr;
	if (desc->debug_name) {
		const size_t length = strlen(desc->debug_name) + 1;
		name                = (char *)malloc(length);
		if (!name)
			return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the fog's name");
		memcpy(name, desc->debug_name, length);
	}
	RBTREND_MALLOC__T(fog, rbtrend_fog_t);
	if (!fog) {
		free(name);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the fog");
	}
	RBTREND_CONSTRUCT__T(fog, rbtrend_fog_t);
	fog->instance        = instance;
	fog->desc            = *desc;
	fog->desc.debug_name = name;
	fog->debug_name      = name;
	fog->records         = (rbtrend_history_record *)calloc(desc->max_views, sizeof(rbtrend_history_record));
	fog->views           = (rbtrend_fog_view *)calloc(desc->max_views, sizeof(rbtrend_fog_view));
	if (!fog->records || !fog->views) {
		RBTREND_DESTRUCT__T(fog, rbtrend_fog_t);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the fog's records");
	}
	fog->noise      = rbtrend_fog_texture(instance, desc->noise_size, desc->noise_size, desc->noise_size, CRGPU_FORMAT_R8G8B8A8_UNORM, false);
	fog->noise_view = fog->noise ? rbtrend_fog_texture_view(fog->noise, RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE) : nullptr;
	bool ok         = fog->noise_view != nullptr;
	for (uint32_t v = 0; ok && v < desc->max_views; ++v)
		ok = rbtrend_fog_make_view(fog, v);
	if (!ok) {
		RBTREND_DESTRUCT__T(fog, rbtrend_fog_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "a grid, buffer or set of the fog can't be created");
	}
	*out_fog = fog;
	return RBT_RESULT_OK;
}

void rbtrend_fog_destroy(rbtrend_fog fog) {
	if (!fog)
		return;
	if (RBT_STALE(fog, "rbtrend_fog_destroy", "fog"))
		return;
	RBT_RETIRE(fog->instance, fog, rbtrend_fog_t);
}

RBT_RESULT rbtrend_fog_write_noise(rbtrend_fog fog, crgpu_command_queue queue, uint32_t first_slice, uint32_t slice_count, const void *data, uint64_t data_size,
                                   RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!fog)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog is NULL");
	if (RBT_STALE(fog, "rbtrend_fog_write_noise", "fog"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog is destroyed");
	RBTREND_ENTER_FOG(fog, "rbtrend_fog_write_noise");
	const uint32_t side = fog->desc.noise_size;
	if (!queue)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "queue is NULL");
	if (first_slice >= side)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "first_slice %u is not below the noise's side, %u", first_slice, side);
	if (slice_count == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "slice_count is 0");
	if (slice_count > side - first_slice)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "slice_count %u from first_slice %u runs past the noise's side, %u", slice_count, first_slice,
		                    side);
	if (!data)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "data is NULL");
	const uint64_t want = (uint64_t)side * side * slice_count * 4u;
	if (data_size != want)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "data_size %llu is not the slices' %llu bytes", (unsigned long long)data_size,
		                    (unsigned long long)want);
	return rbtrend_texture3d_update(fog->noise, queue, 0, 0, 0, first_slice, side, side, slice_count, data, data_size, out_error);
}

} // extern "C"
