#include "rbtrendclouds_internal.hpp"

#include "rbtrendimages_internal.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

// ---- The handle ------------------------------------------------------------------------

rbtrend_clouds_t::~rbtrend_clouds_t() {
	// Each texture, view, material and set goes through the release queue
	// on its own; the rest is the CPU's.
	if (this->views) {
		for (uint32_t v = 0; v < this->desc.max_views; ++v) {
			rbtrend_clouds_view &view = this->views[v];
			for (uint32_t d = 0; d < 2; ++d) {
				rbtrend_binding_set_destroy(view.composite[d]);
				rbtrend_material_destroy(view.reconstruct[d]);
			}
			rbtrend_material_destroy(view.march);
			for (uint32_t t = 0; t < 6; ++t) {
				rbtrend_texture_view_destroy(view.storage[t]);
				rbtrend_texture_view_destroy(view.sampled[t]);
				rbtrend_texture2d_destroy(view.textures[t]);
			}
		}
	}
	free(this->views);
	free(this->records);
	rbtrend_material_destroy(this->material);
	rbtrend_texture_view_destroy(this->maps_view);
	rbtrend_texture_view_destroy(this->detail_view);
	rbtrend_texture_view_destroy(this->base_view);
	rbtrend_texture2d_destroy(this->maps);
	rbtrend_texture3d_destroy(this->detail);
	rbtrend_texture3d_destroy(this->base);
	// The desc's name is the copy made at creation; the handle's may have
	// been set by the caller since (RBT_SET_DEBUG_NAME), and isn't ours.
	free((char *)this->desc.debug_name);
}

// A 3D RGBA8 volume of [side], sampled.
static rbtrend_texture3d rbtrend_clouds_volume(rbtrend_instance instance, uint32_t side) {
	RBTREND_TEXTURE3D_DESC desc{};
	desc.base       = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE3D_DESC);
	desc.width      = side;
	desc.height     = side;
	desc.depth      = side;
	desc.format     = CRGPU_FORMAT_R8G8B8A8_UNORM;
	desc.usage      = RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE;
	desc.mip_levels = 1;
	rbtrend_texture3d made = nullptr;
	(void)rbtrend_texture3d_create(instance, &desc, &made, nullptr);
	return made;
}

static rbtrend_texture_view rbtrend_clouds_volume_view(rbtrend_texture3d texture) {
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base        = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE;
	vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_3D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.mip_count   = 1;
	vd.layer_count = 1;
	rbtrend_texture_view made = nullptr;
	(void)rbtrend_texture_view_create_3d(texture, &vd, &made, nullptr);
	return made;
}

// A 2D texture of [format] at [w] x [h] with [layers], sampled and, with
// [storage], written by compute.
static rbtrend_texture2d rbtrend_clouds_texture(rbtrend_instance instance, uint32_t w, uint32_t h, uint32_t layers, CRGPU_FORMAT format, bool storage,
                                                const char *name) {
	RBTREND_TEXTURE2D_DESC desc{};
	desc.base         = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC);
	desc.width        = w;
	desc.height       = h;
	desc.format       = format;
	desc.usage        = RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE | (storage ? RBTREND_TEXTURE_USAGE_BIT_STORAGE : 0);
	desc.mip_levels   = 1;
	desc.array_layers = layers;
	desc.sample_count = 1;
	desc.debug_name   = name;
	rbtrend_texture2d made = nullptr;
	(void)rbtrend_texture2d_create(instance, &desc, &made, nullptr);
	return made;
}

// A whole view of [texture] of [type], 2D or, with [layers] above 1, a 2D
// array.
static rbtrend_texture_view rbtrend_clouds_texture_view(rbtrend_texture2d texture, RBTREND_TEXTURE_VIEW_TYPE type, uint32_t layers) {
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base        = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = type;
	vd.shape       = layers > 1 ? RBTREND_TEXTURE_VIEW_SHAPE_2D_ARRAY : RBTREND_TEXTURE_VIEW_SHAPE_2D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.mip_count   = 1;
	vd.layer_count = layers;
	rbtrend_texture_view made = nullptr;
	(void)rbtrend_texture_view_create_2d(texture, &vd, &made, nullptr);
	return made;
}

void rbtrend_clouds_write_material_block(rbtrend_material material, const void *bytes, size_t size) {
	for (uint32_t i = 0; i < material->slot_count; ++i) {
		if (material->slots[i].bytes) {
			memcpy(material->slots[i].bytes, bytes, size);
			material->slots[i].dirty = true;
		}
	}
}

// View [index]'s textures, materials and sets. False if one can't be made
// (what was made is the destructor's).
static bool rbtrend_clouds_make_view(rbtrend_clouds clouds, uint32_t index) {
	rbtrend_instance instance       = clouds->instance;
	const rbtrend_lit_builtins &lit = instance->builtins.lit;
	rbtrend_clouds_view &view       = clouds->views[index];
	for (uint32_t t = 0; t < 6; ++t) {
		const bool march         = t < 2;
		const bool depth         = t % 2 == 1;
		const uint32_t w         = march ? clouds->march_size[0] : clouds->history_size[0];
		const uint32_t h         = march ? clouds->march_size[1] : clouds->history_size[1];
		const CRGPU_FORMAT format = depth ? CRGPU_FORMAT_R32_FLOAT : CRGPU_FORMAT_R16G16B16A16_FLOAT;
		view.textures[t]          = rbtrend_clouds_texture(instance, w, h, 1, format, true, march ? "rbtrend clouds march" : "rbtrend clouds history");
		if (!view.textures[t])
			return false;
		view.sampled[t] = rbtrend_clouds_texture_view(view.textures[t], RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, 1);
		view.storage[t] = rbtrend_clouds_texture_view(view.textures[t], RBTREND_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS, 1);
		if (!view.sampled[t] || !view.storage[t])
			return false;
	}
	RBTREND_MATERIAL_DESC md{};
	md.base    = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
	md._layout = lit.clouds_march_layout;
	if (rbtrend_material_create(instance, &md, &view.march, nullptr) != RBT_RESULT_OK)
		return false;
	if (rbtrend_material_set_texture(view.march, "clouds_march_out", 0, view.storage[0], nullptr) != RBT_RESULT_OK ||
	    rbtrend_material_set_texture(view.march, "clouds_march_depth_out", 0, view.storage[1], nullptr) != RBT_RESULT_OK)
		return false;
	// Way [w] writes history w and reads the other.
	for (uint32_t w = 0; w < 2; ++w) {
		const uint32_t written = 2 + 2 * w, read = 2 + 2 * (1 - w);
		md._layout             = lit.clouds_reconstruct_layout;
		if (rbtrend_material_create(instance, &md, &view.reconstruct[w], nullptr) != RBT_RESULT_OK)
			return false;
		rbtrend_material m = view.reconstruct[w];
		if (rbtrend_material_set_texture(m, "clouds_march", 0, view.sampled[0], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "clouds_march_depth", 0, view.sampled[1], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "clouds_last", 0, view.sampled[read], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "clouds_last_depth", 0, view.sampled[read + 1], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_sampler(m, "clouds_history_sampler", 0, lit.environment_sampler, nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "clouds_written", 0, view.storage[written], nullptr) != RBT_RESULT_OK ||
		    rbtrend_material_set_texture(m, "clouds_written_depth", 0, view.storage[written + 1], nullptr) != RBT_RESULT_OK)
			return false;
		RBTREND_BINDING_SET_ENTRY entries[2]{};
		entries[0]._binding               = 0;
		entries[0]._type                  = CRGPU_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		entries[0]._resource._image._view = view.sampled[written];
		entries[1]._binding               = 1;
		entries[1]._type                  = CRGPU_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		entries[1]._resource._image._view = view.sampled[written + 1];
		if (rbtrend_binding_set_create(lit.clouds_composite_layout, entries, 2, &view.composite[w], nullptr) != RBT_RESULT_OK)
			return false;
	}
	return true;
}

// The checks of a look, in its members' order, each its own: Math's of its
// shape (asked for no density, over stand-ins that pass), then Rend's.
static RBT_RESULT rbtrend_clouds_look_check(const RBTREND_CLOUD_LOOK &l, RBTREND_ERROR *out_error) {
	static const uint8_t one_texel[4] = {0, 0, 0, 0};
	RBTMATH_CLOUD_BOUNDS bounds{};
	bounds.kind        = RBTMATH_CLOUD_BOUNDS_KIND_SLAB;
	bounds.top         = 1.0f;
	bounds.up[1]       = 1.0f;
	bounds.map_axis[0] = 1.0f;
	bounds.map_extent  = 1.0f;
	bounds.map_address = RBTMATH_CLOUD_MAP_ADDRESS_REPEAT;
	const RBTMATH_CLOUD_TEXELS texels{one_texel, 1, one_texel, 1, one_texel, 1};
	const RBTMATH_CLOUD_OFFSETS offsets{};
	RBTMATH_ERROR checked{};
	const RBT_RESULT valid = rbtmath_cloud_compute_density(&l.shape, &bounds, &texels, &offsets, nullptr, 0, nullptr, &checked);
	if (valid != RBT_RESULT_OK)
		return rbtrend_fail(out_error, valid, "look: %s", checked.message);
	if (!std::isfinite(l.extinction))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's extinction is not finite");
	if (!(l.extinction > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's extinction %g is not above 0", (double)l.extinction);
	for (int k = 0; k < 3; ++k) {
		if (!std::isfinite(l.albedo[k]))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's albedo[%d] is not finite", k);
		if (!(l.albedo[k] >= 0.0f && l.albedo[k] <= 1.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's albedo[%d] %g is outside 0 to 1", k, (double)l.albedo[k]);
	}
	if (!std::isfinite(l.phase_forward))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's phase_forward is not finite");
	if (!(l.phase_forward >= 0.0f && l.phase_forward <= 0.999f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's phase_forward %g is outside 0 to 0.999", (double)l.phase_forward);
	if (!std::isfinite(l.phase_backward))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's phase_backward is not finite");
	if (!(l.phase_backward >= -0.999f && l.phase_backward <= 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's phase_backward %g is outside -0.999 to 0", (double)l.phase_backward);
	if (!std::isfinite(l.phase_blend))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's phase_blend is not finite");
	if (!(l.phase_blend >= 0.0f && l.phase_blend <= 1.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's phase_blend %g is outside 0 to 1", (double)l.phase_blend);
	if (l.scattering_octaves == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's scattering_octaves is 0");
	if (l.scattering_octaves > RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's scattering_octaves %u is above RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES, %u",
		                    l.scattering_octaves, RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES);
	const bool octaves = l.scattering_octaves > 1;
	const float ratios[3] = {l.octave_extinction, l.octave_scattering, l.octave_phase};
	const char *names[3]  = {"octave_extinction", "octave_scattering", "octave_phase"};
	for (int k = 0; k < 3; ++k) {
		if (!std::isfinite(ratios[k]))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's %s is not finite", names[k]);
		if (!octaves) {
			if (ratios[k] != 0.0f)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's %s is not 0 with one octave, which doesn't read it", names[k]);
			continue;
		}
		if (!(ratios[k] >= 0.0f && ratios[k] <= 1.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's %s %g is outside 0 to 1", names[k], (double)ratios[k]);
		if (k == 1 && !(l.octave_scattering <= l.octave_extinction))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's octave_scattering %g is above its octave_extinction %g: energy would be made",
			                    (double)l.octave_scattering, (double)l.octave_extinction);
	}
	if (!std::isfinite(l.ambient_scale))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's ambient_scale is not finite");
	if (!(l.ambient_scale >= 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look's ambient_scale %g is below 0", (double)l.ambient_scale);
	return RBT_RESULT_OK;
}

extern "C" {

RBT_RESULT rbtrend_clouds_create(rbtrend_instance instance, const RBTREND_CLOUDS_DESC *desc, rbtrend_clouds *out_clouds, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_clouds)
		*out_clouds = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_clouds_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_clouds_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_CLOUDS_DESC))
		return base;
	if (desc->base_noise_size == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "base_noise_size is 0");
	if (desc->base_noise_size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "base_noise_size %u is above RBTMATH_NOISE_VOLUME_MAX_SIZE, %u", desc->base_noise_size,
		                    RBTMATH_NOISE_VOLUME_MAX_SIZE);
	if (desc->detail_noise_size == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "detail_noise_size is 0");
	if (desc->detail_noise_size > RBTMATH_NOISE_VOLUME_MAX_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "detail_noise_size %u is above RBTMATH_NOISE_VOLUME_MAX_SIZE, %u", desc->detail_noise_size,
		                    RBTMATH_NOISE_VOLUME_MAX_SIZE);
	if (desc->coverage_size == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "coverage_size is 0");
	if (desc->coverage_size > RBTMATH_CLOUD_MAX_COVERAGE_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "coverage_size %u is above RBTMATH_CLOUD_MAX_COVERAGE_SIZE, %u", desc->coverage_size,
		                    RBTMATH_CLOUD_MAX_COVERAGE_SIZE);
	if (desc->coverage_maps == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "coverage_maps is 0");
	if (desc->coverage_maps > RBTREND_CLOUDS_MAX_COVERAGE_MAPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "coverage_maps %u is above RBTREND_CLOUDS_MAX_COVERAGE_MAPS, %u", desc->coverage_maps,
		                    RBTREND_CLOUDS_MAX_COVERAGE_MAPS);
	if (desc->max_looks == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_looks is 0");
	if (desc->max_looks > RBTREND_CLOUDS_MAX_LOOKS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_looks %u is above RBTREND_CLOUDS_MAX_LOOKS, %u", desc->max_looks, RBTREND_CLOUDS_MAX_LOOKS);
	if (desc->max_views == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_views is 0");
	if (desc->max_views > RBTREND_CLOUDS_MAX_VIEWS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_views %u is above RBTREND_CLOUDS_MAX_VIEWS, %u", desc->max_views, RBTREND_CLOUDS_MAX_VIEWS);
	if (desc->max_target_width == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_target_width is 0");
	if (desc->max_target_width > RBTREND_CLOUDS_MAX_TARGET_SIDE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_target_width %u is above RBTREND_CLOUDS_MAX_TARGET_SIDE, %u", desc->max_target_width,
		                    RBTREND_CLOUDS_MAX_TARGET_SIDE);
	if (desc->max_target_height == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_target_height is 0");
	if (desc->max_target_height > RBTREND_CLOUDS_MAX_TARGET_SIDE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "max_target_height %u is above RBTREND_CLOUDS_MAX_TARGET_SIDE, %u", desc->max_target_height,
		                    RBTREND_CLOUDS_MAX_TARGET_SIDE);
	if (!out_clouds)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_clouds is NULL");
	if (!rbtrend_lit_clouds_prepare(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's cloud passes can't be made");

	char *name = nullptr;
	if (desc->debug_name) {
		const size_t length = strlen(desc->debug_name) + 1;
		name                = (char *)malloc(length);
		if (!name)
			return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the clouds' name");
		memcpy(name, desc->debug_name, length);
	}
	RBTREND_MALLOC__T(clouds, rbtrend_clouds_t);
	if (!clouds) {
		free(name);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the clouds");
	}
	RBTREND_CONSTRUCT__T(clouds, rbtrend_clouds_t);
	clouds->instance        = instance;
	clouds->desc            = *desc;
	clouds->desc.debug_name = name;
	clouds->debug_name      = name;
	clouds->march_size[0]   = (desc->max_target_width + 3) / 4;
	clouds->march_size[1]   = (desc->max_target_height + 3) / 4;
	clouds->history_size[0] = (desc->max_target_width + 1) / 2;
	clouds->history_size[1] = (desc->max_target_height + 1) / 2;
	clouds->records         = (rbtrend_history_record *)calloc(desc->max_views, sizeof(rbtrend_history_record));
	clouds->views           = (rbtrend_clouds_view *)calloc(desc->max_views, sizeof(rbtrend_clouds_view));
	if (!clouds->records || !clouds->views) {
		RBTREND_DESTRUCT__T(clouds, rbtrend_clouds_t);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the clouds' records");
	}
	const rbtrend_lit_builtins &lit = instance->builtins.lit;
	clouds->base                    = rbtrend_clouds_volume(instance, desc->base_noise_size);
	clouds->detail                  = rbtrend_clouds_volume(instance, desc->detail_noise_size);
	clouds->maps = rbtrend_clouds_texture(instance, desc->coverage_size, desc->coverage_size, desc->coverage_maps * 6, CRGPU_FORMAT_R8G8B8A8_UNORM, false,
	                                      "rbtrend clouds coverage");
	clouds->base_view   = clouds->base ? rbtrend_clouds_volume_view(clouds->base) : nullptr;
	clouds->detail_view = clouds->detail ? rbtrend_clouds_volume_view(clouds->detail) : nullptr;
	clouds->maps_view   = clouds->maps ? rbtrend_clouds_texture_view(clouds->maps, RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, desc->coverage_maps * 6) : nullptr;
	bool ok             = clouds->base_view && clouds->detail_view && clouds->maps_view;
	if (ok) {
		RBTREND_MATERIAL_DESC md{};
		md.base    = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
		md._layout = lit.clouds_layout;
		ok         = rbtrend_material_create(instance, &md, &clouds->material, nullptr) == RBT_RESULT_OK;
		ok = ok && rbtrend_material_set_texture(clouds->material, "clouds_base", 0, clouds->base_view, nullptr) == RBT_RESULT_OK &&
		     rbtrend_material_set_texture(clouds->material, "clouds_detail", 0, clouds->detail_view, nullptr) == RBT_RESULT_OK &&
		     rbtrend_material_set_texture(clouds->material, "clouds_maps", 0, clouds->maps_view, nullptr) == RBT_RESULT_OK &&
		     rbtrend_material_set_sampler(clouds->material, "clouds_repeat", 0, lit.material_sampler, nullptr) == RBT_RESULT_OK;
		if (ok)
			rbtrend_clouds_write_material_block(clouds->material, &clouds->looks, sizeof(clouds->looks));
	}
	for (uint32_t v = 0; ok && v < desc->max_views; ++v)
		ok = rbtrend_clouds_make_view(clouds, v);
	if (!ok) {
		RBTREND_DESTRUCT__T(clouds, rbtrend_clouds_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "a texture, material or set of the clouds can't be created");
	}
	*out_clouds = clouds;
	return RBT_RESULT_OK;
}

void rbtrend_clouds_destroy(rbtrend_clouds clouds) {
	if (!clouds)
		return;
	if (RBT_STALE(clouds, "rbtrend_clouds_destroy", "clouds"))
		return;
	RBT_RETIRE(clouds->instance, clouds, rbtrend_clouds_t);
}

RBT_RESULT rbtrend_clouds_write_noise(rbtrend_clouds clouds, crgpu_command_queue queue, RBTMATH_NOISE_VOLUME_KIND kind, uint32_t first_slice,
                                      uint32_t slice_count, const void *data, uint64_t data_size, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!clouds)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "clouds is NULL");
	if (RBT_STALE(clouds, "rbtrend_clouds_write_noise", "clouds"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "clouds is destroyed");
	RBTREND_ENTER_CLOUDS(clouds, "rbtrend_clouds_write_noise");
	if (!queue)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "queue is NULL");
	if (kind != RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY && kind != RBTMATH_NOISE_VOLUME_KIND_WORLEY)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "kind %u is not PERLIN_WORLEY (the base) or WORLEY (the detail)", (unsigned)kind);
	const bool base     = kind == RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY;
	const uint32_t side = base ? clouds->desc.base_noise_size : clouds->desc.detail_noise_size;
	if (first_slice >= side)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "first_slice %u is not below the volume's side, %u", first_slice, side);
	if (slice_count == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "slice_count is 0");
	if (slice_count > side - first_slice)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "slice_count %u from first_slice %u runs past the volume's side, %u", slice_count, first_slice,
		                    side);
	if (!data)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "data is NULL");
	const uint64_t want = (uint64_t)side * side * slice_count * 4u;
	if (data_size != want)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "data_size %llu is not the slices' %llu bytes", (unsigned long long)data_size,
		                    (unsigned long long)want);
	return rbtrend_texture3d_update(base ? clouds->base : clouds->detail, queue, 0, 0, 0, first_slice, side, side, slice_count, data, data_size, out_error);
}

RBT_RESULT rbtrend_clouds_write_coverage(rbtrend_clouds clouds, crgpu_command_queue queue, uint32_t map, uint32_t face, uint32_t x, uint32_t y, uint32_t width,
                                         uint32_t height, const void *data, uint64_t data_size, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!clouds)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "clouds is NULL");
	if (RBT_STALE(clouds, "rbtrend_clouds_write_coverage", "clouds"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "clouds is destroyed");
	RBTREND_ENTER_CLOUDS(clouds, "rbtrend_clouds_write_coverage");
	const uint32_t side = clouds->desc.coverage_size;
	if (!queue)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "queue is NULL");
	if (map >= clouds->desc.coverage_maps)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "map %u is not below coverage_maps, %u", map, clouds->desc.coverage_maps);
	if (face > 5)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "face %u is not 0 to 5", face);
	if (x >= side)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "x %u is not below the map's side, %u", x, side);
	if (y >= side)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "y %u is not below the map's side, %u", y, side);
	if (width == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "width is 0");
	if (width > side - x)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "width %u from x %u runs past the map's side, %u", width, x, side);
	if (height == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "height is 0");
	if (height > side - y)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "height %u from y %u runs past the map's side, %u", height, y, side);
	if (!data)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "data is NULL");
	const uint64_t want = (uint64_t)width * height * 4u;
	if (data_size != want)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "data_size %llu is not the region's %llu bytes", (unsigned long long)data_size,
		                    (unsigned long long)want);
	return rbtrend_texture2d_update(clouds->maps, queue, 0, map * 6 + face, x, y, width, height, data, data_size, out_error);
}

RBT_RESULT rbtrend_clouds_set_look(rbtrend_clouds clouds, uint32_t index, const RBTREND_CLOUD_LOOK *look, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!clouds)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "clouds is NULL");
	if (RBT_STALE(clouds, "rbtrend_clouds_set_look", "clouds"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "clouds is destroyed");
	RBTREND_ENTER_CLOUDS(clouds, "rbtrend_clouds_set_look");
	if (index >= clouds->desc.max_looks)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "index %u is not below max_looks, %u", index, clouds->desc.max_looks);
	if (!look)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "look is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "look", &look->base, RBT_STRUCT_TYPE_REND_CLOUD_LOOK))
		return base;
	if (const RBT_RESULT checked = rbtrend_clouds_look_check(*look, out_error))
		return checked;
	rbtrend_clouds_look_block &b = clouds->looks.looks[index];
	memset(&b, 0, sizeof(b));
	const RBTMATH_CLOUD_SHAPE &s = look->shape;
	for (uint32_t t = 0; t < 4; ++t)
		for (uint32_t k = 0; k < 4; ++k)
			b.profiles[t][k] = s.profiles[t * 4 + k];
	b.tiles[0]  = s.base_tile;
	b.tiles[1]  = s.detail_tile;
	b.tiles[2]  = s.erosion;
	b.tiles[3]  = s.billow_height;
	b.shape[0]  = s.density_scale;
	b.shape[1]  = s.coverage_scale;
	b.shape[2]  = s.coverage_bias;
	b.shape[3]  = (float)s.type_count;
	b.albedo[0] = look->albedo[0];
	b.albedo[1] = look->albedo[1];
	b.albedo[2] = look->albedo[2];
	b.albedo[3] = look->extinction;
	b.phase[0]  = look->phase_forward;
	b.phase[1]  = look->phase_backward;
	b.phase[2]  = look->phase_blend;
	b.phase[3]  = (float)look->scattering_octaves;
	b.octave[0] = look->octave_extinction;
	b.octave[1] = look->octave_scattering;
	b.octave[2] = look->octave_phase;
	b.octave[3] = look->ambient_scale;
	clouds->looks_set |= 1u << index;
	rbtrend_clouds_write_material_block(clouds->material, &clouds->looks, sizeof(clouds->looks));
	return RBT_RESULT_OK;
}

} // extern "C"
