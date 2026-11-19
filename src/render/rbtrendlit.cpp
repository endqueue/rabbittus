#include "render/rbtrendlit.h"
#include "render/rbtrendpost.h"
#include "rbtrend_internal.hpp"
#include "rbtrendraytracing_internal.hpp"
#include "rbtrendbuiltin_internal.hpp"
#include "rbtrendimages_internal.hpp"
#include "rbtrendlit_internal.hpp"
#include "rbtrendatmosphere_internal.hpp"
#include "rbtrendfog_internal.hpp"
#include "rbtrendclouds_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The packages of shaders/, embedded by the build (cmake/rbtrend_slang.cmake).
#include "rbtrend_lit_frag.package.inc"
#include "rbtrend_lit_depth_frag.package.inc"
#include "rbtrend_lit_vert.package.inc"
#include "rbtrend_lit_color_vert.package.inc"
#include "rbtrend_lit_shadow_vert.package.inc"
#include "rbtrend_lit_shadow_color_vert.package.inc"
#include "rbtrend_lit_depth_cutout_frag.package.inc"
#include "rbtrend_lit_sky_frag.package.inc"
#include "rbtrend_lit_cull_comp.package.inc"
#include "rbtrend_lit_cull_bounded_comp.package.inc"
#include "rbtrend_environment_from_cube_frag.package.inc"
#include "rbtrend_environment_from_equirect_frag.package.inc"
#include "rbtrend_environment_prefilter_frag.package.inc"
#include "rbtrend_environment_irradiance_frag.package.inc"
#include "rbtrend_environment_sky_frag.package.inc"
#include "rbtrend_environment_hosek_frag.package.inc"
#include "rbtrend_lit_brdf_lut_frag.package.inc"
#include "rbtrend_lit_occlusion_frag.package.inc"
#include "rbtrend_lit_occlusion_blur_frag.package.inc"
#include "rbtrend_exposure_luminance_frag.package.inc"
#include "rbtrend_exposure_adapt_frag.package.inc"
#include "rbtrend_exposure_histogram_comp.package.inc"
#include "rbtrend_exposure_adapt_histogram_frag.package.inc"
#include "rbtrend_tone_map_frag.package.inc"
#include "rbtrend_lit_surface_reflect_frag.package.inc"
#include "rbtrend_lit_hiz_frag.package.inc"
#include "rbtrend_lit_hiz_down_frag.package.inc"
#include "rbtrend_lit_ssr_comp.package.inc"
#include "rbtrend_lit_reflect_frag.package.inc"
#include "rbtrend_atmosphere_shell_frag.package.inc"
#include "rbtrend_fog_cull_comp.package.inc"
#include "rbtrend_fog_inject_comp.package.inc"
#include "rbtrend_fog_integrate_comp.package.inc"
#include "rbtrend_fog_apply_frag.package.inc"
#include "rbtrend_clouds_march_comp.package.inc"
#include "rbtrend_clouds_reconstruct_comp.package.inc"
#include "rbtrend_clouds_composite_frag.package.inc"

static_assert(sizeof(RBTREND_LIT_VERTEX) == 48, "RBTREND_LIT_VERTEX is 48 bytes");
static_assert(sizeof(RBTREND_LIT_INSTANCE) == 64, "RBTREND_LIT_INSTANCE is 64 bytes");
static_assert(sizeof(rbtrend_lit_scene_block) == 128 + 80 * RBTREND_LIT_MAX_SHADOW_VIEWS + 112 * RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS +
                                                 32 * RBTREND_LIT_MAX_SHADOW_VIEWS + 208 + 64 * RBTREND_LIT_MAX_LIGHTS + 7 * 16 + 5 * 16,
              "RbtrendLitScene's std140 size");
// Vulkan's guaranteed maxUniformBufferRange, the smallest any device may
// report; Carrottus GPU reports no limit of its own for a uniform buffer, so the
// block keeps under this one on every device.
static_assert(sizeof(rbtrend_lit_scene_block) <= 16384, "RbtrendLitScene fits Vulkan's guaranteed uniform buffer range");
static_assert(sizeof(rbtrend_lit_material_block) == 288, "RbtrendLitMaterial's std140 size");
static_assert(sizeof(rbtrend_lit_shadow_pass_block) == 64, "RbtrendLitShadowPass's std140 size");
static_assert(sizeof(rbtrend_occlusion_block) == 176, "RbtrendOcclusionPass's std140 size");
static_assert(sizeof(rbtrend_hosek_sky_block) == 160, "RbtrendHosekSky's std140 size");
static_assert(sizeof(rbtrend_environment_params) % 8 == 0 && sizeof(rbtrend_sky_params) % 8 == 0 && sizeof(rbtrend_tone_map_params) % 8 == 0 &&
                  sizeof(rbtrend_adapt_params) % 8 == 0 && sizeof(rbtrend_size_params) % 8 == 0 &&
                  sizeof(rbtrend_histogram_params) % 8 == 0 && sizeof(rbtrend_histogram_adapt_params) % 8 == 0,
              "a push Metal aligns to 8 bytes is a multiple of 8");
static_assert(sizeof(rbtrend_hiz_params) == 16 && sizeof(rbtrend_reflect_params) == 16, "HizParams and ReflectParams are 16 bytes");
static_assert(sizeof(rbtrend_ssr_params) == 48, "SsrParams in rbtrend_lit_ssr_comp.slang");

// ---- Checks -------------------------------------------------------------------------

static bool finite(float v) { return std::isfinite(v); }
static bool all_finite(const float *v, size_t n) {
	for (size_t i = 0; i < n; ++i)
		if (!finite(v[i]))
			return false;
	return true;
}
static float length3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static void unit3(const float v[3], float out[4]) {
	const float l = length3(v);
	for (int i = 0; i < 3; ++i)
		out[i] = l > 0.0f ? v[i] / l : 0.0f;
	out[3] = 0.0f;
}
static bool power_of_2(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }
static uint32_t log2_of(uint32_t v) {
	uint32_t l = 0;
	while (v > 1) {
		v >>= 1;
		++l;
	}
	return l;
}
static glm::vec3 vec3_of(const float v[3]) { return glm::vec3(v[0], v[1], v[2]); }

// [projection] with clip space's y negated. glm's projections put +Y up the
// view, as Carrottus GPU's clip space has it on every backend (card S19); each
// shadow map is rendered with y negated all the same, so it holds the image
// (and the winding) it did when clip space was +Y down the target, and the
// surface shader reads it by the same matrix either way.
static glm::mat4 rbtrend_flip_clip_y(const glm::mat4 &projection) {
	return glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f)) * projection;
}

typedef rbtrend_block_member expected_member;

bool rbtrend_lit_check_block(rbtrend_binding_layout layout, uint32_t binding, const char *what, uint32_t size,
                                   const expected_member *members, size_t count) {
	uint32_t block_size               = 0;
	const RBTREND_SHADER_MEMBER *found = nullptr;
	uint32_t found_count              = 0;
	if (!(rbtrend_binding_layout_get_block(layout, binding, &block_size, &found, &found_count) == RBT_RESULT_OK)) {
		rbtrend_report(layout->instance, RBT_RESULT_INVALID_ARGUMENT, "%s has no block at binding %u", what, binding);
		return false;
	}
	bool ok = block_size <= size && size - block_size < 16;
	if (!ok)
		rbtrend_report(layout->instance, RBT_RESULT_INVALID_ARGUMENT, "%s is %u bytes in the shader, and its mirror %u", what, block_size, size);
	for (size_t i = 0; i < count; ++i) {
		bool here = false;
		for (uint32_t m = 0; m < found_count && !here; ++m)
			here = strcmp(found[m].name, members[i].name) == 0 && found[m].offset == members[i].offset;
		if (!here) {
			rbtrend_report(layout->instance, RBT_RESULT_INVALID_ARGUMENT, "%s's member \"%s\" is not at %u in the shader", what, members[i].name, members[i].offset);
			ok = false;
		}
	}
	return ok;
}

#define MEMBER(T, field) (uint32_t) offsetof(T, field)
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static bool rbtrend_lit_check_scene_block(rbtrend_binding_layout layout) {
	typedef rbtrend_lit_scene_block B;
	typedef rbtrend_lit_light_block L;
	typedef rbtrend_lit_directional_block D;
	typedef rbtrend_lit_shadow_block S;
	typedef rbtrend_lit_shadow_view_block V;
	const uint32_t last_light       = MEMBER(B, lights) + (uint32_t)sizeof(L) * (RBTREND_LIT_MAX_LIGHTS - 1);
	const uint32_t last_directional = MEMBER(B, directional) + (uint32_t)sizeof(D) * (RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS - 1);
	const uint32_t last_shadow      = MEMBER(B, shadows) + (uint32_t)sizeof(S) * (RBTREND_LIT_MAX_SHADOW_VIEWS - 1);
	const uint32_t last_view        = MEMBER(B, shadow_views) + (uint32_t)sizeof(V) * (RBTREND_LIT_MAX_SHADOW_VIEWS - 1);
	const expected_member members[] = {
	    {"view_projection", MEMBER(B, view_projection)},
	    {"inverse_view_projection", MEMBER(B, inverse_view_projection)},
	    {"shadow_views[0].view_projection", MEMBER(B, shadow_views)},
	    {"shadow_views[0].rect", MEMBER(B, shadow_views) + MEMBER(V, rect)},
	    {"shadow_views[63].rect", last_view + MEMBER(V, rect)},
	    {"directional[0].direction_versine", MEMBER(B, directional)},
	    {"directional[0].shadow_views", MEMBER(B, directional) + MEMBER(D, shadow_views)},
	    {"directional[3].shadow_views", last_directional + MEMBER(D, shadow_views)},
	    {"directional[0].place_reference", MEMBER(B, directional) + MEMBER(D, place_reference)},
	    {"directional[3].placed_radiance_radius", last_directional + MEMBER(D, placed_radiance_radius)},
	    {"shadows[0].params", MEMBER(B, shadows)},
	    {"shadows[63].views", last_shadow + MEMBER(S, views)},
	    {"eye_position", MEMBER(B, eye_position)},
	    {"view_direction", MEMBER(B, view_direction)},
	    {"ambient_up", MEMBER(B, ambient_up)},
	    {"ambient_sky", MEMBER(B, ambient_sky)},
	    {"ambient_ground", MEMBER(B, ambient_ground)},
	    {"environment_rotation", MEMBER(B, environment_rotation)},
	    {"target_size", MEMBER(B, target_size)},
	    {"shadow_atlas_size", MEMBER(B, shadow_atlas_size)},
	    {"environment_intensity", MEMBER(B, environment_intensity)},
	    {"environment_mips", MEMBER(B, environment_mips)},
	    {"exposure", MEMBER(B, exposure)},
	    {"output_encoding", MEMBER(B, output_encoding)},
	    {"flags", MEMBER(B, flags)},
	    {"light_count", MEMBER(B, light_count)},
	    {"directional_count", MEMBER(B, directional_count)},
	    {"tile_size", MEMBER(B, tile_size)},
	    {"tiles_x", MEMBER(B, tiles_x)},
	    {"tiles_y", MEMBER(B, tiles_y)},
	    {"occlusion_strength", MEMBER(B, occlusion_strength)},
	    {"scene_color_mips", MEMBER(B, scene_color_mips)},
	    {"atmosphere_center", MEMBER(B, atmosphere_center)},
	    {"atmosphere_up", MEMBER(B, atmosphere_up)},
	    {"atmosphere_x", MEMBER(B, atmosphere_x)},
	    {"atmosphere_y", MEMBER(B, atmosphere_y)},
	    {"atmosphere_sizes", MEMBER(B, atmosphere_sizes)},
	    {"atmosphere_aerial", MEMBER(B, atmosphere_aerial)},
	    {"atmosphere_array", MEMBER(B, atmosphere_array)},
	    {"fog_up_base", MEMBER(B, fog_up_base)},
	    {"fog_eye", MEMBER(B, fog_eye)},
	    {"fog_albedo", MEMBER(B, fog_albedo)},
	    {"fog_range", MEMBER(B, fog_range)},
	    {"fog_light", MEMBER(B, fog_light)},
	    {"lights[0].position_range", MEMBER(B, lights)},
	    {"lights[0].outer_shadow", MEMBER(B, lights) + MEMBER(L, outer_shadow)},
	    {"lights[127].outer_shadow", last_light + MEMBER(L, outer_shadow)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendLitScene", (uint32_t)sizeof(B), members, COUNT_OF(members));
}

static bool rbtrend_lit_check_material_block(rbtrend_binding_layout layout) {
	typedef rbtrend_lit_material_block B;
	const expected_member members[] = {
	    {"base_color", MEMBER(B, base_color)},
	    {"emission", MEMBER(B, emission)},
	    {"sheen", MEMBER(B, sheen)},
	    {"roughness", MEMBER(B, roughness)},
	    {"metalness", MEMBER(B, metalness)},
	    {"specular", MEMBER(B, specular)},
	    {"normal_scale", MEMBER(B, normal_scale)},
	    {"occlusion_strength", MEMBER(B, occlusion_strength)},
	    {"alpha_cutoff", MEMBER(B, alpha_cutoff)},
	    {"clearcoat", MEMBER(B, clearcoat)},
	    {"clearcoat_roughness", MEMBER(B, clearcoat_roughness)},
	    {"maps", MEMBER(B, maps)},
	    {"uv_transform", MEMBER(B, uv_transform)},
	    {"map_uv_transform", MEMBER(B, map_uv_transform)},
	    {"transmission", MEMBER(B, transmission)},
	    {"attenuation", MEMBER(B, attenuation)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendLitMaterial", (uint32_t)sizeof(B), members, COUNT_OF(members));
}

static bool rbtrend_lit_check_shadow_pass_block(rbtrend_binding_layout layout) {
	const expected_member members[] = {{"view_projection", 0}};
	return rbtrend_lit_check_block(layout, 0, "RbtrendLitShadowPass", (uint32_t)sizeof(rbtrend_lit_shadow_pass_block), members, 1);
}

static bool rbtrend_lit_check_hosek_block(rbtrend_binding_layout layout) {
	typedef rbtrend_hosek_sky_block B;
	const expected_member members[] = {
	    {"config", MEMBER(B, config)},
	    {"radiance", MEMBER(B, radiance)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendHosekSky", (uint32_t)sizeof(B), members, COUNT_OF(members));
}

static bool rbtrend_lit_check_occlusion_block(rbtrend_binding_layout layout) {
	typedef rbtrend_occlusion_block B;
	const expected_member members[] = {
	    {"view_projection", MEMBER(B, view_projection)},
	    {"inverse_view_projection", MEMBER(B, inverse_view_projection)},
	    {"target_size", MEMBER(B, target_size)},
	    {"eye_position", MEMBER(B, eye_position)},
	    {"params", MEMBER(B, params)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendOcclusionPass", (uint32_t)sizeof(B), members, COUNT_OF(members));
}

// ---- Shaders, layouts and the objects every lit thing shares -------------------------

static const struct {
	const RBTREND_SHADER_PACKAGE *package;
	const char *name;
} s_lit_shaders[9] = {
    {&rbtrend_lit_frag_package, "surface fragment"},
    {&rbtrend_lit_depth_frag_package, "depth fragment"},
    {&rbtrend_lit_vert_package, "surface vertex"},
    {&rbtrend_lit_shadow_vert_package, "shadow vertex"},
    {&rbtrend_lit_depth_cutout_frag_package, "cut-out depth fragment"},
    {&rbtrend_lit_sky_frag_package, "sky fragment"},
    {&rbtrend_lit_color_vert_package, "colored surface vertex"},
    {&rbtrend_lit_shadow_color_vert_package, "colored shadow vertex"},
    {&rbtrend_lit_surface_reflect_frag_package, "reflecting surface fragment"},
};

static rbtrend_shader rbtrend_lit_shader(rbtrend_instance instance, uint32_t which) {
	rbtrend_shader &shader = instance->builtins.lit.shaders[which];
	if (!shader) {
		(void)rbtrend_shader_create(instance, s_lit_shaders[which].package, &shader, nullptr);
		if (!shader)
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit %s shader can't be created", s_lit_shaders[which].name);
	}
	return shader;
}

static rbtrend_sampler rbtrend_lit_sampler(rbtrend_instance instance, CRGPU_ADDRESS_MODE address, bool compare, CRGPU_COMPARE_OP op,
                                         const char *name) {
	RBTREND_SAMPLER_DESC desc{};
	desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_SAMPLER_DESC);
	desc.mag_filter     = CRGPU_FILTER_LINEAR;
	desc.min_filter     = CRGPU_FILTER_LINEAR;
	desc.mipmap_filter  = compare ? CRGPU_FILTER_NEAREST : CRGPU_FILTER_LINEAR;
	desc.address_mode_u = address;
	desc.address_mode_v = address;
	desc.address_mode_w = address;
	desc.mip_lod_bias   = 0.0f;
	desc.min_lod        = 0.0f;
	desc.max_lod        = compare ? 0.0f : 1000.0f;
	desc.max_anisotropy = 1.0f;
	desc.compare_enable = compare;
	desc.compare_op     = op;
	desc.debug_name = name;
	rbtrend_sampler sampler = nullptr;
	(void)rbtrend_sampler_create(instance, &desc, &sampler, nullptr);
	if (!sampler)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s can't be created", name);
	return sampler;
}

// A storage buffer of [size] bytes only shaders write, one copy or one per
// frame in flight ([per_frame]), named [name]. [error] says why it failed.
static RBT_RESULT rbtrend_lit_storage_buffer(rbtrend_instance instance, uint64_t size, bool per_frame, const char *name, rbtrend_buffer *out_buffer, RBTREND_ERROR *error) {
	RBTREND_BUFFER_DESC desc{};
	desc.base       = rbt_struct_base(RBT_STRUCT_TYPE_REND_BUFFER_DESC);
	desc.size       = size;
	desc.usage      = RBTREND_BUFFER_USAGE_STORAGE;
	desc.writes     = RBTREND_BUFFER_WRITES_ONCE;
	desc.per_frame  = per_frame;
	desc.debug_name = name;
	return rbtrend_buffer_create(instance, &desc, out_buffer, error);
}

// A placeholder texture of 1x1 texels: one layer, six for a cube; of depth
// for a shadow map. Never written. Its view in the binding's shape.
static bool rbtrend_lit_placeholder(rbtrend_instance instance, RBTREND_TEXTURE_VIEW_SHAPE shape, bool depth, rbtrend_texture2d *out_texture,
                                   rbtrend_texture_view *out_view) {
	if (*out_view)
		return true;
	const bool cube = shape == RBTREND_TEXTURE_VIEW_SHAPE_CUBE;
	if (!*out_texture) {
		RBTREND_TEXTURE2D_DESC desc{};
		desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC);
		desc.width        = 1;
		desc.height       = 1;
		desc.format       = depth ? CRGPU_FORMAT_D32_FLOAT : CRGPU_FORMAT_R8G8B8A8_UNORM;
		desc.usage        = RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE | (depth ? RBTREND_TEXTURE_USAGE_BIT_DEPTH_STENCIL : 0);
		desc.mip_levels   = 1;
		desc.array_layers = cube ? 6 : 1;
		desc.sample_count = 1;
		desc.cube_capable = cube;
		(void)rbtrend_texture2d_create(instance, &desc, out_texture, nullptr);
		if (!*out_texture) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit placeholder texture can't be created");
			return false;
		}
	}
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE;
	vd.shape       = shape;
	vd.aspect      = depth ? RBTREND_TEXTURE_VIEW_ASPECT_DEPTH : RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.base_mip    = 0;
	vd.mip_count   = 1;
	vd.base_layer  = 0;
	vd.layer_count = cube ? 6 : 1;
	(void)rbtrend_texture_view_create_2d(*out_texture, &vd, out_view, nullptr);
	if (!*out_view)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit placeholder view can't be created");
	return *out_view != nullptr;
}

bool rbtrend_lit_prepare_resources(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (!rbtrend_lit_placeholder(instance, RBTREND_TEXTURE_VIEW_SHAPE_2D, false, &lit.placeholder_2d, &lit.placeholder_2d_view))
		return false;
	if (!rbtrend_lit_placeholder(instance, RBTREND_TEXTURE_VIEW_SHAPE_CUBE, false, &lit.placeholder_cube, &lit.placeholder_cube_view))
		return false;
	if (!rbtrend_lit_placeholder(instance, RBTREND_TEXTURE_VIEW_SHAPE_2D, true, &lit.placeholder_depth, &lit.placeholder_depth_view))
		return false;
	if (!rbtrend_lit_placeholder(instance, RBTREND_TEXTURE_VIEW_SHAPE_2D_ARRAY, false, &lit.placeholder_2d_array, &lit.placeholder_2d_array_view))
		return false;
	if (!lit.placeholder_tiles) {
		if (rbtrend_lit_storage_buffer(instance, 64, false, "rbtrend lit placeholder tiles", &lit.placeholder_tiles, nullptr) != RBT_RESULT_OK) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit placeholder tiles can't be created");
			return false;
		}
	}
	if (!lit.material_sampler)
		lit.material_sampler = rbtrend_lit_sampler(instance, CRGPU_ADDRESS_MODE_REPEAT, false, 0, "lit material sampler");
	if (!lit.environment_sampler)
		lit.environment_sampler = rbtrend_lit_sampler(instance, CRGPU_ADDRESS_MODE_CLAMP_TO_EDGE, false, 0, "lit environment sampler");
	if (!lit.shadow_sampler)
		lit.shadow_sampler =
		    rbtrend_lit_sampler(instance, CRGPU_ADDRESS_MODE_CLAMP_TO_EDGE, true, CRGPU_COMPARE_OP_LESS_OR_EQUAL, "lit shadow sampler");
	if (!lit.shadow_sampler_reversed)
		lit.shadow_sampler_reversed = rbtrend_lit_sampler(instance, CRGPU_ADDRESS_MODE_CLAMP_TO_EDGE, true, CRGPU_COMPARE_OP_GREATER_OR_EQUAL,
		                                                 "lit reversed shadow sampler");
	return lit.material_sampler && lit.environment_sampler && lit.shadow_sampler && lit.shadow_sampler_reversed;
}

bool rbtrend_lit_prepare_brdf_lut(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.brdf_lut_view && lit.brdf_lut_target)
		return true;
	if (!lit.brdf_lut) {
		RBTREND_TEXTURE2D_DESC desc{};
		desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC);
		desc.width        = RBTREND_BRDF_LUT_SIZE;
		desc.height       = RBTREND_BRDF_LUT_SIZE;
		desc.format       = RBTREND_BRDF_LUT_FORMAT;
		desc.usage        = RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE | RBTREND_TEXTURE_USAGE_BIT_RENDER_TARGET;
		desc.mip_levels   = 1;
		desc.array_layers = 1;
		desc.sample_count = 1;
		(void)rbtrend_texture2d_create(instance, &desc, &lit.brdf_lut, nullptr);
		if (!lit.brdf_lut) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's BRDF lookup texture can't be created");
			return false;
		}
	}
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_2D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.mip_count   = 1;
	vd.layer_count = 1;
	if (!lit.brdf_lut_view) {
		vd.type           = RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE;
		(void)rbtrend_texture_view_create_2d(lit.brdf_lut, &vd, &lit.brdf_lut_view, nullptr);
	}
	if (!lit.brdf_lut_target) {
		vd.type             = RBTREND_TEXTURE_VIEW_TYPE_RENDER_TARGET;
		(void)rbtrend_texture_view_create_2d(lit.brdf_lut, &vd, &lit.brdf_lut_target, nullptr);
	}
	if (!lit.brdf_lut_view || !lit.brdf_lut_target)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the views of Rend's BRDF lookup texture can't be created");
	return lit.brdf_lut_view && lit.brdf_lut_target;
}

// A pipeline over rbtrend_fullscreen_vert.slang with [fragment], rendering to
// [color_count] targets each of [format] (the first blended by [blend]
// where given), [layouts] at its first slots, [push_size] bytes of push
// constants (none if 0).
static rbtrend_graphics_pipeline rbtrend_lit_fullscreen_pipeline(rbtrend_instance instance, rbtrend_shader fragment, const rbtrend_binding_layout *layouts,
                                                               uint32_t layout_count, CRGPU_FORMAT format, uint32_t push_size,
                                                               const char *name, const crgpu_blend_attachment_desc *blend = nullptr,
                                                               uint32_t color_count = 1) {
	RBTREND_GRAPHICS_PIPELINE_DESC desc{};
	desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_GRAPHICS_PIPELINE_DESC);
	desc._debug_name = name;
	desc._vertex_binding_count       = 0;
	desc._topology                   = CRGPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	desc._cull_mode                  = CRGPU_CULL_MODE_NONE;
	desc._front_face_ccw             = true;
	desc._depth_test                 = false;
	desc._depth_write                = false;
	desc._depth_compare_op           = CRGPU_COMPARE_OP_NEVER;
	desc.vertex_shader               = rbtrend_builtin_fullscreen_vert(instance);
	desc.fragment_shader             = fragment;
	desc._vertex_entry_point         = "rbtrend_fullscreen_vert";
	desc._fragment_entry_point       = rbtrend_shader_get_reflection(fragment)->entry_point;
	for (uint32_t i = 0; i < color_count; ++i)
		desc.color_attachment_formats[i] = format;
	desc.color_attachment_count = color_count;
	desc.depth_format           = CRGPU_FORMAT_UNKNOWN;
	if (blend)
		desc._blends[0] = *blend;
	for (uint32_t i = 0; i < layout_count; ++i)
		desc._binding_layouts[i] = layouts[i];
	desc._binding_layout_count = layout_count;
	if (push_size > 0) {
		desc._push_constant_ranges[0].stage_flags = CRGPU_SHADER_STAGE_FRAGMENT;
		desc._push_constant_ranges[0].offset      = 0;
		desc._push_constant_ranges[0].size        = push_size;
		desc._push_constant_range_count           = 1;
	}
	if (!desc.vertex_shader)
		return nullptr;
	rbtrend_graphics_pipeline pipeline = nullptr;
	(void)rbtrend_graphics_pipeline_create(instance, &desc, &pipeline, nullptr);
	if (!pipeline)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s pipeline can't be created for format %d", name, (int)format);
	return pipeline;
}

static const struct {
	const RBTREND_SHADER_PACKAGE *package;
	CRGPU_FORMAT format;
	uint32_t push_size;
	const char *name;
} s_passes[RBTREND_LIT_PASS_COUNT] = {
    {&rbtrend_environment_from_cube_frag_package, RBTREND_ENVIRONMENT_FORMAT, sizeof(rbtrend_environment_params),
     "environment from a cube"},
    {&rbtrend_environment_from_equirect_frag_package, RBTREND_ENVIRONMENT_FORMAT,
     sizeof(rbtrend_environment_params), "environment from a picture"},
    {&rbtrend_environment_prefilter_frag_package, RBTREND_ENVIRONMENT_FORMAT, sizeof(rbtrend_environment_params),
     "environment prefilter"},
    {&rbtrend_environment_irradiance_frag_package, RBTREND_ENVIRONMENT_FORMAT,
     sizeof(rbtrend_environment_params), "environment irradiance"},
    {&rbtrend_environment_sky_frag_package, RBTREND_ENVIRONMENT_FORMAT, sizeof(rbtrend_sky_params), "sky"},
    {&rbtrend_lit_brdf_lut_frag_package, RBTREND_BRDF_LUT_FORMAT, sizeof(rbtrend_size_params), "BRDF lookup"},
    {&rbtrend_lit_occlusion_frag_package, RBTREND_OCCLUSION_FORMAT, 0, "ambient occlusion"},
    {&rbtrend_lit_occlusion_blur_frag_package, CRGPU_FORMAT_UNKNOWN, sizeof(rbtrend_size_params),
     "ambient occlusion blur"},
    {&rbtrend_exposure_luminance_frag_package, RBTREND_EXPOSURE_LUMINANCE_FORMAT, sizeof(rbtrend_size_params),
     "exposure luminance"},
    {&rbtrend_exposure_adapt_frag_package, RBTREND_EXPOSURE_FORMAT, sizeof(rbtrend_adapt_params), "exposure adapt"},
    {&rbtrend_environment_hosek_frag_package, RBTREND_ENVIRONMENT_FORMAT, sizeof(rbtrend_sky_params),
     "Hosek-Wilkie sky"},
    {&rbtrend_exposure_adapt_histogram_frag_package, RBTREND_EXPOSURE_FORMAT,
     sizeof(rbtrend_histogram_adapt_params), "exposure adapt from a histogram"},
    {&rbtrend_lit_hiz_frag_package, RBTREND_HIZ_FORMAT, sizeof(rbtrend_hiz_params), "Hi-Z pyramid"},
    {&rbtrend_lit_hiz_down_frag_package, RBTREND_HIZ_FORMAT, sizeof(rbtrend_size_params), "Hi-Z pyramid level"},
};

// Every pass's fragment shader, the pass layout (a texture at 0, a sampler
// at 1), and the occlusion pass's own layout. False, reported, if one can't
// be made.
static bool rbtrend_lit_prepare_passes(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (!rbtrend_lit_prepare_resources(instance))
		return false;
	for (uint32_t pass = 0; pass < RBTREND_LIT_PASS_COUNT; ++pass) {
		if (lit.pass_frags[pass])
			continue;
		(void)rbtrend_shader_create(instance, s_passes[pass].package, &lit.pass_frags[pass], nullptr);
		if (!lit.pass_frags[pass]) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s shader can't be created", s_passes[pass].name);
			return false;
		}
	}
	if (!lit.pass_layout) {
		// The passes that read a texture and a sampler at set 0.
		rbtrend_shader readers[5] = {lit.pass_frags[RBTREND_LIT_PASS_FROM_CUBE], lit.pass_frags[RBTREND_LIT_PASS_FROM_EQUIRECT],
		                            lit.pass_frags[RBTREND_LIT_PASS_PREFILTER], lit.pass_frags[RBTREND_LIT_PASS_IRRADIANCE],
		                            lit.pass_frags[RBTREND_LIT_PASS_BLUR]};
		(void)rbtrend_binding_layout_create_from_shaders(instance, readers, 5, 0, &lit.pass_layout, nullptr);
		if (!lit.pass_layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's passes can't be created");
			return false;
		}
	}
	if (!lit.occlusion_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.pass_frags[RBTREND_LIT_PASS_OCCLUSION], 1, 0, &lit.occlusion_layout, nullptr);
		if (!lit.occlusion_layout || !rbtrend_lit_check_occlusion_block(lit.occlusion_layout)) {
			rbtrend_binding_layout_destroy(lit.occlusion_layout);
			lit.occlusion_layout = nullptr;
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's ambient occlusion can't be created");
			return false;
		}
	}
	if (!lit.histogram_comp) {
		(void)rbtrend_shader_create(instance, &rbtrend_exposure_histogram_comp_package, &lit.histogram_comp, nullptr);
		if (!lit.histogram_comp) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's exposure histogram shader can't be created");
			return false;
		}
	}
	if (!lit.histogram_layout) {
		// The histogram's dispatch writes the bins and the adapting pass
		// reads them: one layout, so one set serves both.
		rbtrend_shader readers[2] = {lit.histogram_comp, lit.pass_frags[RBTREND_LIT_PASS_ADAPT_HISTOGRAM]};
		(void)rbtrend_binding_layout_create_from_shaders(instance, readers, 2, 0, &lit.histogram_layout, nullptr);
		if (!lit.histogram_layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's exposure histogram can't be created");
			return false;
		}
	}
	if (!lit.hosek_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.pass_frags[RBTREND_LIT_PASS_HOSEK_SKY], 1, 0, &lit.hosek_layout, nullptr);
		if (!lit.hosek_layout || !rbtrend_lit_check_hosek_block(lit.hosek_layout)) {
			rbtrend_binding_layout_destroy(lit.hosek_layout);
			lit.hosek_layout = nullptr;
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's Hosek-Wilkie sky can't be created");
			return false;
		}
	}
	return true;
}

bool rbtrend_lit_prepare_passes_for(rbtrend_instance instance) { return rbtrend_lit_prepare_passes(instance); }

rbtrend_graphics_pipeline rbtrend_lit_fullscreen_pipeline_for(rbtrend_instance instance, rbtrend_shader fragment, const rbtrend_binding_layout *layouts,
                                                            uint32_t layout_count, CRGPU_FORMAT format, uint32_t color_count, uint32_t push_size,
                                                            const char *name) {
	return rbtrend_lit_fullscreen_pipeline(instance, fragment, layouts, layout_count, format, push_size, name, nullptr, color_count);
}

rbtrend_graphics_pipeline rbtrend_lit_pass_pipeline(rbtrend_instance instance, uint32_t pass) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.pass_pipelines[pass])
		return lit.pass_pipelines[pass];
	if (!rbtrend_lit_prepare_passes(instance))
		return nullptr;
	// What each reads: a texture at set 0 (and the previous exposure at set
	// 1); nothing; or the occlusion block and a depth buffer.
	rbtrend_binding_layout layouts[2] = {lit.pass_layout, lit.pass_layout};
	uint32_t count                   = 1;
	if (pass == RBTREND_LIT_PASS_SKY || pass == RBTREND_LIT_PASS_BRDF_LUT)
		count = 0;
	else if (pass == RBTREND_LIT_PASS_OCCLUSION) {
		layouts[0] = lit.occlusion_layout;
		count      = 2;
	} else if (pass == RBTREND_LIT_PASS_HOSEK_SKY)
		layouts[0] = lit.hosek_layout;
	else if (pass == RBTREND_LIT_PASS_ADAPT_HISTOGRAM) {
		layouts[0] = lit.histogram_layout;
		count      = 2;
	}
	else if (pass == RBTREND_LIT_PASS_ADAPT)
		count = 2;
	lit.pass_pipelines[pass] = rbtrend_lit_fullscreen_pipeline(instance, lit.pass_frags[pass], layouts, count, s_passes[pass].format,
	                                                         s_passes[pass].push_size, s_passes[pass].name);
	return lit.pass_pipelines[pass];
}

static rbtrend_graphics_pipeline rbtrend_lit_format_pipeline(rbtrend_instance instance, std::vector<rbtrend_lit_builtins::format_pipeline> &pipelines,
                                                           rbtrend_shader fragment, const rbtrend_binding_layout *layouts, uint32_t layout_count,
                                                           CRGPU_FORMAT format, uint32_t push_size, const char *name,
                                                           const crgpu_blend_attachment_desc *blend = nullptr) {
	for (const rbtrend_lit_builtins::format_pipeline &entry : pipelines)
		if (entry.format == format)
			return entry.pipeline;
	rbtrend_graphics_pipeline pipeline = rbtrend_lit_fullscreen_pipeline(instance, fragment, layouts, layout_count, format, push_size, name, blend);
	if (!pipeline)
		return nullptr;
	try {
		pipelines.push_back({format, pipeline});
	} catch (...) {
		rbtrend_graphics_pipeline_destroy(pipeline);
		rbtrend_report(instance, RBT_RESULT_INVALID_ARGUMENT, "no memory to keep Rend's %s pipeline", name);
		return nullptr;
	}
	return pipeline;
}

rbtrend_graphics_pipeline rbtrend_lit_blur_pipeline(rbtrend_instance instance, CRGPU_FORMAT format) {
	if (!rbtrend_lit_prepare_passes(instance))
		return nullptr;
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	return rbtrend_lit_format_pipeline(instance, lit.blur_pipelines, lit.pass_frags[RBTREND_LIT_PASS_BLUR], &lit.pass_layout, 1, format,
	                                  sizeof(rbtrend_size_params), "ambient occlusion blur");
}

rbtrend_graphics_pipeline rbtrend_lit_tone_map_pipeline(rbtrend_instance instance, CRGPU_FORMAT format) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	// It reads its source through the view's bloom set, and an exposure
	// through a view's environment set.
	if (!rbtrend_builtin_bloom_prepare(instance) || !rbtrend_lit_prepare_passes(instance))
		return nullptr;
	if (!lit.tone_map_frag) {
		(void)rbtrend_shader_create(instance, &rbtrend_tone_map_frag_package, &lit.tone_map_frag, nullptr);
		if (!lit.tone_map_frag) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's tone map shader can't be created");
			return nullptr;
		}
	}
	if (!lit.tone_map_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.tone_map_frag, 1, 0, &lit.tone_map_layout, nullptr);
		if (!lit.tone_map_layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's tone map shader can't be created");
			return nullptr;
		}
	}
	const rbtrend_binding_layout layouts[2] = {lit.tone_map_layout, lit.pass_layout};
	return rbtrend_lit_format_pipeline(instance, lit.tone_map_pipelines, lit.tone_map_frag, layouts, 2, format, sizeof(rbtrend_tone_map_params),
	                                  "tone map");
}

static rbtrend_binding_layout rbtrend_lit_scene_layout(rbtrend_instance instance);

// A shader of Rend's made from [package] into [*shader] if it isn't, and
// the layout of its set [index] into [*layout] if it isn't. False, reported
// naming [what], if either can't be.
static bool rbtrend_lit_own_shader(rbtrend_instance instance, const RBTREND_SHADER_PACKAGE *package, rbtrend_shader *shader, uint32_t index,
                                  rbtrend_binding_layout *layout, const char *what) {
	if (!*shader) {
		(void)rbtrend_shader_create(instance, package, shader, nullptr);
		if (!*shader) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s shader can't be created", what);
			return false;
		}
	}
	if (!*layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, shader, 1, index, layout, nullptr);
		if (!*layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's %s shader can't be created", what);
			return false;
		}
	}
	return true;
}

rbtrend_compute_pipeline rbtrend_lit_ssr_pipeline(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.ssr_pipeline)
		return lit.ssr_pipeline;
	if (!rbtrend_lit_prepare_resources(instance))
		return nullptr;
	if (!rbtrend_lit_own_shader(instance, &rbtrend_lit_ssr_comp_package, &lit.ssr_comp, 0, &lit.ssr_scene_layout, "screen-space reflections"))
		return nullptr;
	if (!rbtrend_lit_own_shader(instance, &rbtrend_lit_ssr_comp_package, &lit.ssr_comp, 1, &lit.ssr_input_layout, "screen-space reflections"))
		return nullptr;
	if (!rbtrend_lit_check_scene_block(lit.ssr_scene_layout)) {
		rbtrend_binding_layout_destroy(lit.ssr_scene_layout);
		lit.ssr_scene_layout = nullptr;
		return nullptr;
	}
	RBTREND_COMPUTE_PIPELINE_DESC desc{};
	desc.base                                 = rbt_struct_base(RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC);
	desc._debug_name                          = "rbtrend screen-space reflections";
	desc.compute_shader                       = lit.ssr_comp;
	desc._entry_point                         = "rbtrend_lit_ssr_comp";
	desc._binding_layouts[0]                  = lit.ssr_scene_layout;
	desc._binding_layouts[1]                  = lit.ssr_input_layout;
	desc._binding_layout_count                = 2;
	desc._push_constant_ranges[0].stage_flags = CRGPU_SHADER_STAGE_COMPUTE;
	desc._push_constant_ranges[0].offset      = 0;
	desc._push_constant_ranges[0].size        = sizeof(rbtrend_ssr_params);
	desc._push_constant_range_count           = 1;
	RBTREND_ERROR error;
	(void)rbtrend_compute_pipeline_create(instance, &desc, &lit.ssr_pipeline, &error);
	if (!lit.ssr_pipeline)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's screen-space reflections pipeline can't be created: %s", error.message);
	return lit.ssr_pipeline;
}

rbtrend_graphics_pipeline rbtrend_lit_reflect_pipeline(rbtrend_instance instance, CRGPU_FORMAT format) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	rbtrend_binding_layout scene_layout = rbtrend_lit_scene_layout(instance);
	if (!scene_layout)
		return nullptr;
	if (!rbtrend_lit_own_shader(instance, &rbtrend_lit_reflect_frag_package, &lit.reflect_frag, 1, &lit.reflect_input_layout, "reflection composite"))
		return nullptr;
	// Added onto the picture: what the shader writes is the change.
	const crgpu_blend_attachment_desc blend = rbtrend_get_blend_additive();
	const rbtrend_binding_layout layouts[2]    = {scene_layout, lit.reflect_input_layout};
	return rbtrend_lit_format_pipeline(instance, lit.reflect_pipelines, lit.reflect_frag, layouts, 2, format, sizeof(rbtrend_reflect_params),
	                                  "reflection composite", &blend);
}

rbtrend_graphics_pipeline rbtrend_lit_shell_pipeline(rbtrend_instance instance, CRGPU_FORMAT format) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.shell_pipelines)
		if (entry.format == format)
			return entry.pipeline;
	rbtrend_binding_layout scene_layout = rbtrend_lit_scene_layout(instance);
	if (!scene_layout)
		return nullptr;
	// Set 1 is an atmosphere's block, of the atmosphere passes' layout.
	if (!rbtrend_atmosphere_prepare(instance))
		return nullptr;
	if (!rbtrend_lit_own_shader(instance, &rbtrend_atmosphere_shell_frag_package, &lit.shell_frag, 2, &lit.shell_input_layout, "atmosphere shell"))
		return nullptr;
	// The texel becomes texel x T + X per channel: X the first output, T
	// the second (rbtrend_atmosphere_shell_frag.slang); the alpha kept, the
	// first output's 0 and the second's 1.
	crgpu_blend_attachment_desc blend{};
	blend.blend_enable                      = true;
	blend.src_color_blend_factor            = CRGPU_BLEND_FACTOR_ONE;
	blend.dst_color_blend_factor            = CRGPU_BLEND_FACTOR_SRC1_COLOR;
	blend.color_blend_op                    = CRGPU_BLEND_OP_ADD;
	blend.src_alpha_blend_factor            = CRGPU_BLEND_FACTOR_ONE;
	blend.dst_alpha_blend_factor            = CRGPU_BLEND_FACTOR_SRC1_ALPHA;
	blend.alpha_blend_op                    = CRGPU_BLEND_OP_ADD;
	const rbtrend_binding_layout layouts[3] = {scene_layout, lit.atmosphere_layout, lit.shell_input_layout};
	return rbtrend_lit_format_pipeline(instance, lit.shell_pipelines, lit.shell_frag, layouts, 3, format, sizeof(rbtrend_shell_params), "atmosphere shell",
	                                  &blend);
}

static bool rbtrend_fog_check_block(rbtrend_binding_layout layout) {
	typedef rbtrend_fog_block B;
	typedef rbtrend_fog_volume_block V;
	const uint32_t last = (uint32_t)offsetof(B, volumes) + (uint32_t)sizeof(V) * (RBTREND_FOG_MAX_VOLUMES - 1);
	const rbtrend_block_member members[] = {
	    {"last_view_projection", (uint32_t)offsetof(B, last_view_projection)},
	    {"last_eye", (uint32_t)offsetof(B, last_eye)},
	    {"shift", (uint32_t)offsetof(B, shift)},
	    {"range", (uint32_t)offsetof(B, range)},
	    {"last_range", (uint32_t)offsetof(B, last_range)},
	    {"grid", (uint32_t)offsetof(B, grid)},
	    {"volumes[0].to_volume", (uint32_t)offsetof(B, volumes)},
	    {"volumes[0].half_shape", (uint32_t)(offsetof(B, volumes) + offsetof(V, half_shape))},
	    {"volumes[0].params", (uint32_t)(offsetof(B, volumes) + offsetof(V, params))},
	    {"volumes[0].noise_offset", (uint32_t)(offsetof(B, volumes) + offsetof(V, noise_offset))},
	    {"volumes[0].albedo", (uint32_t)(offsetof(B, volumes) + offsetof(V, albedo))},
	    {"volumes[0].sphere", (uint32_t)(offsetof(B, volumes) + offsetof(V, sphere))},
	    {"volumes[63].sphere", last + (uint32_t)offsetof(V, sphere)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendFog", (uint32_t)sizeof(B), members, sizeof(members) / sizeof(members[0]));
}

// A compute pipeline of Rend's fog: [shader] over [layouts], [push] bytes
// of push constants (none at 0), into [*out] if it isn't made. False,
// reported, if it can't be.
static bool rbtrend_lit_fog_compute(rbtrend_instance instance, rbtrend_shader shader, const char *entry, const rbtrend_binding_layout *layouts,
                                   uint32_t layout_count, uint32_t push, const char *name, rbtrend_compute_pipeline *out) {
	if (*out)
		return true;
	RBTREND_COMPUTE_PIPELINE_DESC desc{};
	desc.base           = rbt_struct_base(RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC);
	desc._debug_name    = name;
	desc.compute_shader = shader;
	desc._entry_point   = entry;
	for (uint32_t i = 0; i < layout_count; ++i)
		desc._binding_layouts[i] = layouts[i];
	desc._binding_layout_count = layout_count;
	if (push > 0) {
		desc._push_constant_ranges[0].stage_flags = CRGPU_SHADER_STAGE_COMPUTE;
		desc._push_constant_ranges[0].offset      = 0;
		desc._push_constant_ranges[0].size        = push;
		desc._push_constant_range_count           = 1;
	}
	RBTREND_ERROR error;
	(void)rbtrend_compute_pipeline_create(instance, &desc, out, &error);
	if (!*out)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s pipeline can't be created: %s", name, error.message);
	return *out != nullptr;
}

bool rbtrend_lit_fog_prepare(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.fog_cull_pipeline && lit.fog_inject_pipeline && lit.fog_integrate_pipeline && lit.fog_apply_layout)
		return true;
	if (!rbtrend_lit_prepare_resources(instance))
		return false;
	rbtrend_binding_layout scene_layout = rbtrend_lit_scene_layout(instance);
	if (!scene_layout)
		return false;
	struct {
		const RBTREND_SHADER_PACKAGE *package;
		rbtrend_shader *shader;
		const char *name;
	} const shaders[4] = {{&rbtrend_fog_cull_comp_package, &lit.fog_cull_comp, "fog cull"},
	                      {&rbtrend_fog_inject_comp_package, &lit.fog_inject_comp, "fog inject"},
	                      {&rbtrend_fog_integrate_comp_package, &lit.fog_integrate_comp, "fog integrate"},
	                      {&rbtrend_fog_apply_frag_package, &lit.fog_apply_frag, "fog apply"}};
	for (const auto &entry : shaders) {
		if (*entry.shader)
			continue;
		(void)rbtrend_shader_create(instance, entry.package, entry.shader, nullptr);
		if (!*entry.shader) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s shader can't be created", entry.name);
			return false;
		}
	}
	if (!lit.fog_layout) {
		// Set 1, alike in the cull and the inject.
		rbtrend_shader both[2] = {lit.fog_cull_comp, lit.fog_inject_comp};
		(void)rbtrend_binding_layout_create_from_shaders(instance, both, 2, 1, &lit.fog_layout, nullptr);
		if (!lit.fog_layout || !rbtrend_fog_check_block(lit.fog_layout)) {
			rbtrend_binding_layout_destroy(lit.fog_layout);
			lit.fog_layout = nullptr;
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's fog grid can't be created");
			return false;
		}
	}
	if (!lit.fog_integrate_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.fog_integrate_comp, 1, 0, &lit.fog_integrate_layout, nullptr);
		if (!lit.fog_integrate_layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's fog integration can't be created");
			return false;
		}
	}
	const rbtrend_binding_layout two[2] = {scene_layout, lit.fog_layout};
	if (!rbtrend_lit_fog_compute(instance, lit.fog_cull_comp, "rbtrend_fog_cull_comp", two, 2, 0, "rbtrend fog cull", &lit.fog_cull_pipeline))
		return false;
	if (!rbtrend_lit_fog_compute(instance, lit.fog_inject_comp, "rbtrend_fog_inject_comp", two, 2, 0, "rbtrend fog inject", &lit.fog_inject_pipeline))
		return false;
	if (!rbtrend_lit_fog_compute(instance, lit.fog_integrate_comp, "rbtrend_fog_integrate_comp", &lit.fog_integrate_layout, 1,
	                             (uint32_t)sizeof(rbtrend_fog_integrate_params), "rbtrend fog integrate", &lit.fog_integrate_pipeline))
		return false;
	if (!lit.fog_apply_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.fog_apply_frag, 1, 1, &lit.fog_apply_layout, nullptr);
		if (!lit.fog_apply_layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's fog apply can't be created");
			return false;
		}
	}
	return true;
}

rbtrend_graphics_pipeline rbtrend_lit_fog_apply_pipeline(rbtrend_instance instance, CRGPU_FORMAT format) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.fog_apply_pipelines)
		if (entry.format == format)
			return entry.pipeline;
	if (!rbtrend_lit_fog_prepare(instance))
		return nullptr;
	// The texel becomes texel x T + L: L the output's colour, T its alpha;
	// the alpha kept.
	crgpu_blend_attachment_desc blend{};
	blend.blend_enable                      = true;
	blend.src_color_blend_factor            = CRGPU_BLEND_FACTOR_ONE;
	blend.dst_color_blend_factor            = CRGPU_BLEND_FACTOR_SRC_ALPHA;
	blend.color_blend_op                    = CRGPU_BLEND_OP_ADD;
	blend.src_alpha_blend_factor            = CRGPU_BLEND_FACTOR_ZERO;
	blend.dst_alpha_blend_factor            = CRGPU_BLEND_FACTOR_ONE;
	blend.alpha_blend_op                    = CRGPU_BLEND_OP_ADD;
	const rbtrend_binding_layout layouts[2] = {lit.scene_layout, lit.fog_apply_layout};
	return rbtrend_lit_format_pipeline(instance, lit.fog_apply_pipelines, lit.fog_apply_frag, layouts, 2, format, sizeof(rbtrend_fog_apply_params),
	                                  "fog apply", &blend);
}

static bool rbtrend_clouds_check_blocks(rbtrend_binding_layout looks_layout, rbtrend_binding_layout march_layout, rbtrend_binding_layout history_layout) {
	typedef rbtrend_clouds_look_block K;
	typedef rbtrend_clouds_march_block M;
	typedef rbtrend_clouds_layer_block L;
	typedef rbtrend_clouds_history_block H;
	const uint32_t last_look = (uint32_t)sizeof(K) * (RBTREND_CLOUDS_MAX_LOOKS - 1);
	const rbtrend_block_member looks[] = {
	    {"looks[0].profiles", (uint32_t)offsetof(K, profiles)},
	    {"looks[0].tiles", (uint32_t)offsetof(K, tiles)},
	    {"looks[0].shape", (uint32_t)offsetof(K, shape)},
	    {"looks[0].albedo", (uint32_t)offsetof(K, albedo)},
	    {"looks[0].phase", (uint32_t)offsetof(K, phase)},
	    {"looks[0].octave", (uint32_t)offsetof(K, octave)},
	    {"looks[15].octave", last_look + (uint32_t)offsetof(K, octave)},
	};
	const uint32_t first = (uint32_t)offsetof(M, layers);
	const uint32_t last  = first + (uint32_t)sizeof(L) * (RBTREND_CLOUDS_MAX_LAYERS - 1);
	const rbtrend_block_member march[] = {
	    {"sizes", (uint32_t)offsetof(M, sizes)},
	    {"target", (uint32_t)offsetof(M, target)},
	    {"counts", (uint32_t)offsetof(M, counts)},
	    {"jitter", (uint32_t)offsetof(M, jitter)},
	    {"layers[0].axes", first},
	    {"layers[0].origin_kind", first + (uint32_t)offsetof(L, origin_kind)},
	    {"layers[0].bounds", first + (uint32_t)offsetof(L, bounds)},
	    {"layers[0].up_look", first + (uint32_t)offsetof(L, up_look)},
	    {"layers[0].map_axis_layer", first + (uint32_t)offsetof(L, map_axis_layer)},
	    {"layers[0].base_offset", first + (uint32_t)offsetof(L, base_offset)},
	    {"layers[0].detail_offset", first + (uint32_t)offsetof(L, detail_offset)},
	    {"layers[0].light_direction", first + (uint32_t)offsetof(L, light_direction)},
	    {"layers[0].light_illuminance", first + (uint32_t)offsetof(L, light_illuminance)},
	    {"layers[0].atmosphere", first + (uint32_t)offsetof(L, atmosphere)},
	    {"layers[0].atmosphere_radii", first + (uint32_t)offsetof(L, atmosphere_radii)},
	    {"layers[3].atmosphere_radii", last + (uint32_t)offsetof(L, atmosphere_radii)},
	};
	const rbtrend_block_member history[] = {
	    {"last_view_projection", (uint32_t)offsetof(H, last_view_projection)},
	    {"shift", (uint32_t)offsetof(H, shift)},
	    {"sizes", (uint32_t)offsetof(H, sizes)},
	    {"frame", (uint32_t)offsetof(H, frame)},
	};
	return rbtrend_lit_check_block(looks_layout, 0, "RbtrendCloudsLooks", (uint32_t)sizeof(rbtrend_clouds_looks_block), looks,
	                               sizeof(looks) / sizeof(looks[0])) &&
	       rbtrend_lit_check_block(march_layout, 0, "RbtrendCloudsMarch", (uint32_t)sizeof(M), march, sizeof(march) / sizeof(march[0])) &&
	       rbtrend_lit_check_block(history_layout, 0, "RbtrendCloudsHistory", (uint32_t)sizeof(H), history, sizeof(history) / sizeof(history[0]));
}

// The shaders the scene's layout is made from that are the clouds': made
// here, if they aren't, for rbtrend_lit_scene_layout() and the clouds'
// passes alike. False, reported, if one can't be.
static bool rbtrend_lit_clouds_shaders(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	struct {
		const RBTREND_SHADER_PACKAGE *package;
		rbtrend_shader *shader;
		const char *name;
	} const shaders[3] = {{&rbtrend_clouds_march_comp_package, &lit.clouds_march_comp, "clouds march"},
	                      {&rbtrend_clouds_reconstruct_comp_package, &lit.clouds_reconstruct_comp, "clouds reconstruct"},
	                      {&rbtrend_clouds_composite_frag_package, &lit.clouds_composite_frag, "clouds composite"}};
	for (const auto &entry : shaders) {
		if (*entry.shader)
			continue;
		(void)rbtrend_shader_create(instance, entry.package, entry.shader, nullptr);
		if (!*entry.shader) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s shader can't be created", entry.name);
			return false;
		}
	}
	return true;
}

bool rbtrend_lit_clouds_prepare(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.clouds_march_pipeline && lit.clouds_reconstruct_pipeline && lit.clouds_composite_layout)
		return true;
	if (!rbtrend_lit_prepare_resources(instance))
		return false;
	if (!rbtrend_lit_clouds_shaders(instance))
		return false;
	rbtrend_binding_layout scene_layout = rbtrend_lit_scene_layout(instance);
	if (!scene_layout)
		return false;
	struct {
		rbtrend_shader shaders[2];
		uint32_t count;
		uint32_t set;
		rbtrend_binding_layout *out;
		const char *name;
	} const layouts[5] = {
	    {{lit.clouds_march_comp, lit.clouds_composite_frag}, 2, 1, &lit.clouds_input_layout, "clouds input"},
	    {{lit.clouds_march_comp, nullptr}, 1, 2, &lit.clouds_layout, "clouds"},
	    {{lit.clouds_march_comp, nullptr}, 1, 3, &lit.clouds_march_layout, "clouds march"},
	    {{lit.clouds_reconstruct_comp, nullptr}, 1, 1, &lit.clouds_reconstruct_layout, "clouds reconstruct"},
	    {{lit.clouds_composite_frag, nullptr}, 1, 2, &lit.clouds_composite_layout, "clouds composite"},
	};
	for (const auto &entry : layouts) {
		if (*entry.out)
			continue;
		rbtrend_shader shaders[2] = {entry.shaders[0], entry.shaders[1]};
		(void)rbtrend_binding_layout_create_from_shaders(instance, shaders, entry.count, entry.set, entry.out, nullptr);
		if (!*entry.out) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's %s can't be created", entry.name);
			return false;
		}
	}
	if (!rbtrend_clouds_check_blocks(lit.clouds_layout, lit.clouds_march_layout, lit.clouds_reconstruct_layout)) {
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the blocks of Rend's clouds don't match their shaders");
		return false;
	}
	const rbtrend_binding_layout march[4] = {scene_layout, lit.clouds_input_layout, lit.clouds_layout, lit.clouds_march_layout};
	if (!rbtrend_lit_fog_compute(instance, lit.clouds_march_comp, "rbtrend_clouds_march_comp", march, 4, 0, "rbtrend clouds march",
	                             &lit.clouds_march_pipeline))
		return false;
	const rbtrend_binding_layout reconstruct[2] = {scene_layout, lit.clouds_reconstruct_layout};
	if (!rbtrend_lit_fog_compute(instance, lit.clouds_reconstruct_comp, "rbtrend_clouds_reconstruct_comp", reconstruct, 2, 0, "rbtrend clouds reconstruct",
	                             &lit.clouds_reconstruct_pipeline))
		return false;
	return true;
}

rbtrend_graphics_pipeline rbtrend_lit_clouds_composite_pipeline(rbtrend_instance instance, CRGPU_FORMAT format) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.clouds_composite_pipelines)
		if (entry.format == format)
			return entry.pipeline;
	if (!rbtrend_lit_clouds_prepare(instance))
		return nullptr;
	// The texel becomes texel x T + L: L the output's colour, T its alpha;
	// the alpha kept.
	crgpu_blend_attachment_desc blend{};
	blend.blend_enable                      = true;
	blend.src_color_blend_factor            = CRGPU_BLEND_FACTOR_ONE;
	blend.dst_color_blend_factor            = CRGPU_BLEND_FACTOR_SRC_ALPHA;
	blend.color_blend_op                    = CRGPU_BLEND_OP_ADD;
	blend.src_alpha_blend_factor            = CRGPU_BLEND_FACTOR_ZERO;
	blend.dst_alpha_blend_factor            = CRGPU_BLEND_FACTOR_ONE;
	blend.alpha_blend_op                    = CRGPU_BLEND_OP_ADD;
	const rbtrend_binding_layout layouts[3] = {lit.scene_layout, lit.clouds_input_layout, lit.clouds_composite_layout};
	return rbtrend_lit_format_pipeline(instance, lit.clouds_composite_pipelines, lit.clouds_composite_frag, layouts, 3, format,
	                                  sizeof(rbtrend_clouds_composite_params), "clouds composite", &blend);
}

rbtrend_compute_pipeline rbtrend_lit_histogram_pipeline(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.histogram_pipeline)
		return lit.histogram_pipeline;
	if (!rbtrend_lit_prepare_passes(instance))
		return nullptr;
	RBTREND_COMPUTE_PIPELINE_DESC desc{};
	desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC);
	desc._debug_name = "rbtrend exposure histogram";
	desc.compute_shader                       = lit.histogram_comp;
	desc._entry_point                         = "rbtrend_exposure_histogram_comp";
	desc._binding_layouts[0]                  = lit.histogram_layout;
	desc._binding_layout_count                = 1;
	desc._push_constant_ranges[0].stage_flags = CRGPU_SHADER_STAGE_COMPUTE;
	desc._push_constant_ranges[0].offset      = 0;
	desc._push_constant_ranges[0].size        = sizeof(rbtrend_histogram_params);
	desc._push_constant_range_count           = 1;
	(void)rbtrend_compute_pipeline_create(instance, &desc, &lit.histogram_pipeline, nullptr);
	if (!lit.histogram_pipeline)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's exposure histogram pipeline can't be created");
	return lit.histogram_pipeline;
}

rbtrend_compute_pipeline rbtrend_lit_cull_pipeline(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.cull_pipeline)
		return lit.cull_pipeline;
	if (!lit.cull_comp) {
		(void)rbtrend_shader_create(instance, &rbtrend_lit_cull_comp_package, &lit.cull_comp, nullptr);
		if (!lit.cull_comp) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's light culling shader can't be created");
			return nullptr;
		}
	}
	if (!lit.cull_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.cull_comp, 1, 0, &lit.cull_layout, nullptr);
		if (!lit.cull_layout || !rbtrend_lit_check_scene_block(lit.cull_layout)) {
			rbtrend_binding_layout_destroy(lit.cull_layout);
			lit.cull_layout = nullptr;
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's light culling can't be created");
			return nullptr;
		}
	}
	RBTREND_COMPUTE_PIPELINE_DESC desc{};
	desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC);
	desc._debug_name = "rbtrend light culling";
	desc.compute_shader        = lit.cull_comp;
	desc._entry_point          = "rbtrend_lit_cull_comp";
	desc._binding_layouts[0]   = lit.cull_layout;
	desc._binding_layout_count = 1;
	(void)rbtrend_compute_pipeline_create(instance, &desc, &lit.cull_pipeline, nullptr);
	if (!lit.cull_pipeline)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's light culling pipeline can't be created");
	return lit.cull_pipeline;
}

rbtrend_compute_pipeline rbtrend_lit_cull_bounded_pipeline(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.cull_bounded_pipeline)
		return lit.cull_bounded_pipeline;
	if (!rbtrend_lit_cull_pipeline(instance) || !rbtrend_lit_prepare_passes(instance))
		return nullptr;
	if (!lit.cull_bounded_comp) {
		(void)rbtrend_shader_create(instance, &rbtrend_lit_cull_bounded_comp_package, &lit.cull_bounded_comp, nullptr);
		if (!lit.cull_bounded_comp) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's light culling shader with depth bounds can't be created");
			return nullptr;
		}
	}
	// Set 0 as the unbounded culling's, so the scene's cull material serves
	// both; set 1 the depth, in a layout of its own that the compute stage
	// sees (the pass layout's are the fragment stage's).
	if (!lit.cull_depth_layout) {
		(void)rbtrend_binding_layout_create_from_shaders(instance, &lit.cull_bounded_comp, 1, 1, &lit.cull_depth_layout, nullptr);
		if (!lit.cull_depth_layout) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the depth layout of Rend's light culling with depth bounds can't be created");
			return nullptr;
		}
	}
	RBTREND_COMPUTE_PIPELINE_DESC desc{};
	desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC);
	desc._debug_name = "rbtrend light culling with depth bounds";
	desc.compute_shader        = lit.cull_bounded_comp;
	desc._entry_point          = "rbtrend_lit_cull_bounded_comp";
	desc._binding_layouts[0]   = lit.cull_layout;
	desc._binding_layouts[1]   = lit.cull_depth_layout;
	desc._binding_layout_count = 2;
	(void)rbtrend_compute_pipeline_create(instance, &desc, &lit.cull_bounded_pipeline, nullptr);
	if (!lit.cull_bounded_pipeline)
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's light culling pipeline with depth bounds can't be created");
	return lit.cull_bounded_pipeline;
}

rbtrend_binding_set rbtrend_texture_view_cull_depth_set(rbtrend_texture_view view) {
	if (view->cull_depth_set)
		return view->cull_depth_set;
	rbtrend_instance instance = view->instance;
	// The depth alone: the shader loads texels, with no sampler, so its
	// layout (made from it) has none.
	RBTREND_BINDING_SET_ENTRY entries[1]{};
	entries[0]._binding                  = 0;
	entries[0]._type                     = CRGPU_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	entries[0]._resource._image._view    = view;
	(void)rbtrend_binding_set_create(instance->builtins.lit.cull_depth_layout, entries, 1, &view->cull_depth_set, nullptr);
	if (!view->cull_depth_set)
		rbtrend_report(view->instance, RBT_RESULT_REND_GPU_FAILURE, "the binding set that reads a depth view for light culling can't be created");
	return view->cull_depth_set;
}

rbtrend_binding_set rbtrend_texture_view_environment_set(rbtrend_texture_view view) {
	if (view->environment_set)
		return view->environment_set;
	rbtrend_instance instance = view->instance;
	if (!rbtrend_lit_prepare_passes(instance))
		return nullptr;
	RBTREND_BINDING_SET_ENTRY entries[2]{};
	entries[0]._binding                  = 0;
	entries[0]._type                     = CRGPU_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	entries[0]._resource._image._view    = view;
	entries[1]._binding                  = 1;
	entries[1]._type                     = CRGPU_DESCRIPTOR_TYPE_SAMPLER;
	entries[1]._resource._image._sampler = instance->builtins.lit.environment_sampler;
	(void)rbtrend_binding_set_create(instance->builtins.lit.pass_layout, entries, 2, &view->environment_set, nullptr);
	if (!view->environment_set)
		rbtrend_report(view->instance, RBT_RESULT_REND_GPU_FAILURE, "the binding set that reads a view can't be created");
	return view->environment_set;
}

void rbtrend_lit_builtins_destroy(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.reflect_pipelines)
		rbtrend_graphics_pipeline_destroy(entry.pipeline);
	lit.reflect_pipelines.clear();
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.shell_pipelines)
		rbtrend_graphics_pipeline_destroy(entry.pipeline);
	lit.shell_pipelines.clear();
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.fog_apply_pipelines)
		rbtrend_graphics_pipeline_destroy(entry.pipeline);
	lit.fog_apply_pipelines.clear();
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.clouds_composite_pipelines)
		rbtrend_graphics_pipeline_destroy(entry.pipeline);
	lit.clouds_composite_pipelines.clear();
	rbtrend_compute_pipeline *fog_pipelines[] = {&lit.fog_cull_pipeline, &lit.fog_inject_pipeline, &lit.fog_integrate_pipeline,
	                                             &lit.clouds_march_pipeline, &lit.clouds_reconstruct_pipeline};
	for (rbtrend_compute_pipeline *pipeline : fog_pipelines) {
		rbtrend_compute_pipeline_destroy(*pipeline);
		*pipeline = nullptr;
	}
	rbtrend_compute_pipeline_destroy(lit.ssr_pipeline);
	lit.ssr_pipeline = nullptr;
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.tone_map_pipelines)
		rbtrend_graphics_pipeline_destroy(entry.pipeline);
	lit.tone_map_pipelines.clear();
	for (const rbtrend_lit_builtins::format_pipeline &entry : lit.blur_pipelines)
		rbtrend_graphics_pipeline_destroy(entry.pipeline);
	lit.blur_pipelines.clear();
	for (rbtrend_graphics_pipeline &pipeline : lit.pass_pipelines) {
		rbtrend_graphics_pipeline_destroy(pipeline);
		pipeline = nullptr;
	}
	for (rbtrend_graphics_pipeline &pipeline : lit.atmosphere_pipelines) {
		rbtrend_graphics_pipeline_destroy(pipeline);
		pipeline = nullptr;
	}
	rbtrend_compute_pipeline_destroy(lit.cull_pipeline);
	lit.cull_pipeline = nullptr;
	rbtrend_compute_pipeline_destroy(lit.cull_bounded_pipeline);
	lit.cull_bounded_pipeline = nullptr;
	rbtrend_compute_pipeline_destroy(lit.histogram_pipeline);
	lit.histogram_pipeline           = nullptr;
	rbtrend_binding_layout *layouts[] = {&lit.shell_input_layout, &lit.reflect_input_layout, &lit.ssr_input_layout, &lit.ssr_scene_layout,  &lit.tone_map_layout,
	                                    &lit.pass_layout,          &lit.occlusion_layout, &lit.hosek_layout,       &lit.histogram_layout,
	                                    &lit.cull_depth_layout,    &lit.cull_layout,      &lit.material_layout,    &lit.shadow_layout,
	                                    &lit.scene_layout,         &lit.atmosphere_layout, &lit.fog_layout,         &lit.fog_integrate_layout,
	                                    &lit.fog_apply_layout,     &lit.clouds_input_layout, &lit.clouds_layout,   &lit.clouds_march_layout,
	                                    &lit.clouds_reconstruct_layout, &lit.clouds_composite_layout};
	for (rbtrend_binding_layout *layout : layouts) {
		rbtrend_binding_layout_destroy(*layout);
		*layout = nullptr;
	}
	rbtrend_shader_destroy(lit.tone_map_frag);
	lit.tone_map_frag = nullptr;
	rbtrend_shader_destroy(lit.ssr_comp);
	lit.ssr_comp = nullptr;
	rbtrend_shader_destroy(lit.reflect_frag);
	lit.reflect_frag = nullptr;
	rbtrend_shader_destroy(lit.shell_frag);
	lit.shell_frag = nullptr;
	rbtrend_shader *fog_shaders[] = {&lit.fog_cull_comp,          &lit.fog_inject_comp,           &lit.fog_integrate_comp, &lit.fog_apply_frag,
	                                 &lit.clouds_march_comp,      &lit.clouds_reconstruct_comp, &lit.clouds_composite_frag};
	for (rbtrend_shader *shader : fog_shaders) {
		rbtrend_shader_destroy(*shader);
		*shader = nullptr;
	}
	rbtrend_shader_destroy(lit.cull_comp);
	lit.cull_comp = nullptr;
	rbtrend_shader_destroy(lit.cull_bounded_comp);
	lit.cull_bounded_comp = nullptr;
	rbtrend_shader_destroy(lit.histogram_comp);
	lit.histogram_comp = nullptr;
	for (rbtrend_shader &shader : lit.pass_frags) {
		rbtrend_shader_destroy(shader);
		shader = nullptr;
	}
	for (rbtrend_shader &shader : lit.atmosphere_frags) {
		rbtrend_shader_destroy(shader);
		shader = nullptr;
	}
	for (rbtrend_shader &shader : lit.shaders) {
		rbtrend_shader_destroy(shader);
		shader = nullptr;
	}
	rbtrend_sampler *samplers[] = {&lit.shadow_sampler_reversed, &lit.shadow_sampler, &lit.environment_sampler, &lit.material_sampler};
	for (rbtrend_sampler *sampler : samplers) {
		rbtrend_sampler_destroy(*sampler);
		*sampler = nullptr;
	}
	rbtrend_texture_view *views[] = {&lit.brdf_lut_target, &lit.brdf_lut_view, &lit.placeholder_depth_view, &lit.placeholder_cube_view,
	                                &lit.placeholder_2d_view, &lit.placeholder_2d_array_view};
	for (rbtrend_texture_view *view : views) {
		rbtrend_texture_view_destroy(*view);
		*view = nullptr;
	}
	rbtrend_texture2d *textures[] = {&lit.brdf_lut, &lit.placeholder_depth, &lit.placeholder_cube, &lit.placeholder_2d, &lit.placeholder_2d_array};
	for (rbtrend_texture2d *texture : textures) {
		rbtrend_texture2d_destroy(*texture);
		*texture = nullptr;
	}
	rbtrend_buffer_destroy(lit.placeholder_tiles);
	lit.placeholder_tiles = nullptr;
}

// The slot of [material]'s block named [name]: where its bytes are.
static rbtrend_material_slot *rbtrend_lit_block_slot(rbtrend_material material, const char *name) {
	for (uint32_t i = 0; i < material->slot_count; ++i)
		if (material->slots[i].bytes && strcmp(material->slots[i].name, name) == 0)
			return &material->slots[i];
	return nullptr;
}

static void rbtrend_lit_write_block(rbtrend_material material, const char *name, const void *bytes, size_t size) {
	rbtrend_material_slot *slot = rbtrend_lit_block_slot(material, name);
	memcpy(slot->bytes, bytes, size);
	slot->dirty = true;
}

// ---- Vertex layouts -----------------------------------------------------------------

static const RBTREND_VERTEX_ELEMENT s_lit_vertex_elements[] = {
    {"in_position", RBTREND_VERTEX_ELEMENT_TYPE_VEC3, (uint32_t)offsetof(RBTREND_LIT_VERTEX, position)},
    {"in_normal", RBTREND_VERTEX_ELEMENT_TYPE_VEC3, (uint32_t)offsetof(RBTREND_LIT_VERTEX, normal)},
    {"in_uv", RBTREND_VERTEX_ELEMENT_TYPE_VEC2, (uint32_t)offsetof(RBTREND_LIT_VERTEX, uv)},
    {"in_tangent", RBTREND_VERTEX_ELEMENT_TYPE_VEC4, (uint32_t)offsetof(RBTREND_LIT_VERTEX, tangent)},
};
static const RBTREND_VERTEX_ELEMENT s_lit_color_elements[] = {
    {"color", RBTREND_VERTEX_ELEMENT_TYPE_VEC4, 0},
};
static const RBTREND_VERTEX_ELEMENT s_lit_instance_elements[] = {
    {"in_transform0", RBTREND_VERTEX_ELEMENT_TYPE_VEC4, 0},
    {"in_transform1", RBTREND_VERTEX_ELEMENT_TYPE_VEC4, 16},
    {"in_transform2", RBTREND_VERTEX_ELEMENT_TYPE_VEC4, 32},
    {"in_transform3", RBTREND_VERTEX_ELEMENT_TYPE_VEC4, 48},
};

// rbtrend_lit_get_scene_layout's work: the instance's layout, made on
// first use. NULL, reported, if it can't be.
static rbtrend_binding_layout rbtrend_lit_scene_layout(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.scene_layout)
		return lit.scene_layout;
	// Every shader that reads set 0, so each binding has every stage that
	// reads it: the fog grid's inject pass among them (RBTREND_COMMAND_LIT_FOG
	// binds the scene's own set from compute), which widens the stages and
	// changes no binding.
	rbtrend_lit_builtins &made = instance->builtins.lit;
	if (!made.fog_inject_comp) {
		(void)rbtrend_shader_create(instance, &rbtrend_fog_inject_comp_package, &made.fog_inject_comp, nullptr);
		if (!made.fog_inject_comp) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's fog inject shader can't be created");
			return nullptr;
		}
	}
	// The clouds' march and reconstruct passes bind it from compute too
	// (RBTREND_COMMAND_LIT_CLOUDS).
	if (!rbtrend_lit_clouds_shaders(instance))
		return nullptr;
	rbtrend_shader shaders[7] = {rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SURFACE_FRAGMENT),
	                            rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SURFACE_VERTEX),
	                            rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SURFACE_VERTEX_COLOR),
	                            rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SKY_FRAGMENT), made.fog_inject_comp, made.clouds_march_comp,
	                            made.clouds_reconstruct_comp};
	for (rbtrend_shader shader : shaders)
		if (!shader)
			return nullptr;
	rbtrend_binding_layout layout = nullptr;
	(void)rbtrend_binding_layout_create_from_shaders(instance, shaders, 7, RBTREND_LIT_SCENE_SET, &layout, nullptr);
	if (!layout) {
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's lit scene can't be created");
		return nullptr;
	}
	if (!rbtrend_lit_check_scene_block(layout)) {
		rbtrend_binding_layout_destroy(layout);
		return nullptr;
	}
	lit.scene_layout = layout;
	return layout;
}

extern "C" {

RBTREND_VERTEX_LAYOUT rbtrend_get_lit_vertex_layout(void) {
	RBTREND_VERTEX_LAYOUT layout{};
	layout.count    = 4;
	layout.elements = s_lit_vertex_elements;
	layout.stride   = (uint32_t)sizeof(RBTREND_LIT_VERTEX);
	return layout;
}

RBTREND_VERTEX_LAYOUT rbtrend_get_lit_vertex_color_layout(void) {
	RBTREND_VERTEX_LAYOUT layout{};
	layout.count    = 1;
	layout.elements = s_lit_color_elements;
	layout.stride   = (uint32_t)(sizeof(float) * 4);
	return layout;
}

RBTREND_VERTEX_LAYOUT rbtrend_get_lit_instance_layout(void) {
	RBTREND_VERTEX_LAYOUT layout{};
	layout.count    = 4;
	layout.elements = s_lit_instance_elements;
	layout.stride   = (uint32_t)sizeof(RBTREND_LIT_INSTANCE);
	return layout;
}

RBT_RESULT rbtrend_lit_get_shader(rbtrend_instance instance, RBTREND_LIT_SHADER shader, rbtrend_shader *out_shader, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_shader)
		*out_shader = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_lit_get_shader", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	if (shader > RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "%u is not an RBTREND_LIT_SHADER", shader);
	if (!out_shader)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_shader is NULL");
	*out_shader = rbtrend_lit_shader(instance, shader);
	if (!*out_shader)
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit shader %u can't be made", shader);
	return RBT_RESULT_OK;
}

// rbtrend_lit_get_shadow_layout's work: the instance's layout, made on
// first use. NULL, reported, if it can't be.
static rbtrend_binding_layout rbtrend_lit_shadow_layout(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.shadow_layout)
		return lit.shadow_layout;
	rbtrend_shader vertices[2] = {rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SHADOW_VERTEX),
	                             rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SHADOW_VERTEX_COLOR)};
	if (!vertices[0])
		return nullptr;
	if (!vertices[1])
		return nullptr;
	rbtrend_binding_layout layout = nullptr;
	(void)rbtrend_binding_layout_create_from_shaders(instance, vertices, 2, RBTREND_LIT_SCENE_SET, &layout, nullptr);
	if (!layout) {
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's lit shadow pass can't be created");
		return nullptr;
	}
	if (!rbtrend_lit_check_shadow_pass_block(layout)) {
		rbtrend_binding_layout_destroy(layout);
		return nullptr;
	}
	lit.shadow_layout = layout;
	return layout;
}

// rbtrend_lit_get_material_layout's work: the instance's layout, made on
// first use. NULL, reported, if it can't be.
static rbtrend_binding_layout rbtrend_lit_material_layout(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.material_layout)
		return lit.material_layout;
	rbtrend_shader shaders[2] = {rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_SURFACE_FRAGMENT),
	                            rbtrend_lit_shader(instance, RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT)};
	if (!shaders[0] || !shaders[1])
		return nullptr;
	rbtrend_binding_layout layout = nullptr;
	(void)rbtrend_binding_layout_create_from_shaders(instance, shaders, 2, RBTREND_LIT_MATERIAL_SET, &layout, nullptr);
	if (!layout) {
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's lit materials can't be created");
		return nullptr;
	}
	if (!rbtrend_lit_check_material_block(layout)) {
		rbtrend_binding_layout_destroy(layout);
		return nullptr;
	}
	lit.material_layout = layout;
	return layout;
}

// A layout getter's public form: the layout through [out_layout], or why
// it can't be made.
static RBT_RESULT rbtrend_lit_layout_result(
    const char *who,
    rbtrend_instance instance,
    rbtrend_binding_layout (*make)(rbtrend_instance),
    const char *what,
    rbtrend_binding_layout *out_layout,
    RBTREND_ERROR *error) {
	rbtrend_error_clear(error);
	if (out_layout)
		*out_layout = nullptr;
	if (!instance)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, who, "instance"))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	if (!out_layout)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_layout is NULL");
	*out_layout = make(instance);
	if (!*out_layout)
		return rbtrend_fail(error, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's lit %s can't be made", what);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_get_scene_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error) {
	return rbtrend_lit_layout_result("rbtrend_lit_get_scene_layout", instance, rbtrend_lit_scene_layout, "scene", out_layout, out_error);
}

RBT_RESULT rbtrend_lit_get_shadow_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error) {
	return rbtrend_lit_layout_result("rbtrend_lit_get_shadow_layout", instance, rbtrend_lit_shadow_layout, "shadow pass", out_layout, out_error);
}

RBT_RESULT rbtrend_lit_get_material_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error) {
	return rbtrend_lit_layout_result("rbtrend_lit_get_material_layout", instance, rbtrend_lit_material_layout, "materials", out_layout, out_error);
}

// ---- Materials ----------------------------------------------------------------------

static const char *const s_map_bindings[4] = {"base_color_map", "normal_map", "orm_map", "emission_map"};

// [material]'s block, through [out_slot], if it is a lit material;
// RBT_RESULT_INVALID_ARGUMENT, [error] saying why, if not. [who] is the
// public call, for the destroyed-handle check (rbtstale.hpp).
static RBT_RESULT rbtrend_lit_material_slot(const char *who, rbtrend_material material, rbtrend_material_slot **out_slot, RBTREND_ERROR *error) {
	if (!material)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "material is NULL");
	if (RBT_STALE(material, who, "material"))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "material is destroyed");
	if (!material->layout || material->layout != material->instance->builtins.lit.material_layout)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "material is not a lit material (rbtrend_lit_material_create)");
	rbtrend_material_slot *slot = rbtrend_lit_block_slot(material, "u_lit_material");
	if (!slot || slot->block_size < sizeof(rbtrend_lit_material_block))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "the material has no lit block");
	*out_slot = slot;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_create(rbtrend_instance instance, rbtrend_material *out_material, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_material)
		*out_material = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_lit_material_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_lit_material_create");
	if (!out_material)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_material is NULL");
	rbtrend_binding_layout layout = nullptr;
	if (const RBT_RESULT made = rbtrend_lit_get_material_layout(instance, &layout, out_error))
		return made;
	if (!rbtrend_lit_prepare_resources(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit resources can't be made");
	RBTREND_MATERIAL_DESC desc{};
	desc.base                = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
	desc._layout             = layout;
	rbtrend_material material = nullptr;
	if (const RBT_RESULT made = rbtrend_material_create(instance, &desc, &material, out_error))
		return made;
	const rbtrend_lit_builtins &lit = instance->builtins.lit;
	RBT_RESULT set                   = rbtrend_material_set_sampler(material, "material_sampler", 0, lit.material_sampler, out_error);
	for (const char *binding : s_map_bindings)
		if (set == RBT_RESULT_OK)
			set = rbtrend_material_set_texture(material, binding, 0, lit.placeholder_2d_view, out_error);
	rbtrend_material_slot *slot = nullptr;
	if (set == RBT_RESULT_OK)
		set = rbtrend_lit_material_slot("rbtrend_lit_material_create", material, &slot, out_error);
	if (set != RBT_RESULT_OK) {
		rbtrend_material_destroy(material);
		return set;
	}
	rbtrend_lit_material_block block{};
	for (int i = 0; i < 4; ++i)
		block.base_color[i] = 1.0f;
	block.sheen[3]            = 0.5f;
	block.roughness           = 0.5f;
	block.metalness           = 0.0f;
	block.specular            = 0.5f;
	block.normal_scale        = 1.0f;
	block.occlusion_strength  = 1.0f;
	block.alpha_cutoff        = 0.0f;
	block.clearcoat           = 0.0f;
	block.clearcoat_roughness = 0.1f;
	block.maps                = 0;
	block.uv_transform[0]     = 1.0f;
	block.uv_transform[5]     = 1.0f;
	for (float *rows : block.map_uv_transform) {
		rows[0] = 1.0f;
		rows[5] = 1.0f;
	}
	block.transmission[1] = 1.5f; // glTF's default index
	for (int i = 0; i < 3; ++i)
		block.attenuation[i] = 1.0f;
	memcpy(slot->bytes, &block, sizeof(block));
	slot->dirty = true;
	// Its entry in the lit-material table, on a device with ray queries (D4).
	if (const RBT_RESULT taken = rbtrend_rt_lit_take(instance, &block, &material->rt_lit_slot, out_error)) {
		rbtrend_material_destroy(material);
		return taken;
	}
	*out_material = material;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_set_values(rbtrend_material material, const RBTREND_LIT_MATERIAL_VALUES *values, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	rbtrend_material_slot *slot = nullptr;
	if (const RBT_RESULT lit = rbtrend_lit_material_slot("rbtrend_lit_material_set_values", material, &slot, out_error))
		return lit;
	if (!values)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "values is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "values", &values->base, RBT_STRUCT_TYPE_REND_LIT_MATERIAL_VALUES))
		return base;
	const RBTREND_LIT_MATERIAL_VALUES &v = *values;
	if (!all_finite(v.base_color, 4) || !all_finite(v.emission, 3) || !finite(v.roughness) || !finite(v.metalness) || !finite(v.specular) ||
	    !finite(v.normal_scale) || !finite(v.occlusion_strength) || !finite(v.alpha_cutoff) || !all_finite(v.uv_transform, 6) ||
	    !finite(v.clearcoat) || !finite(v.clearcoat_roughness) || !all_finite(v.sheen_color, 3) || !finite(v.sheen_roughness) ||
	    !finite(v.transmission) || !finite(v.ior) || !finite(v.thickness) || !all_finite(v.attenuation_color, 3) || !finite(v.attenuation_distance)) {
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a value is not a finite number");
	}
	rbtrend_lit_material_block block;
	memcpy(&block, slot->bytes, sizeof(block));
	memcpy(block.base_color, v.base_color, sizeof(block.base_color));
	memcpy(block.emission, v.emission, sizeof(float) * 3);
	block.emission[3] = 0.0f;
	memcpy(block.sheen, v.sheen_color, sizeof(float) * 3);
	block.sheen[3]            = v.sheen_roughness;
	block.roughness           = v.roughness;
	block.metalness           = v.metalness;
	block.specular            = v.specular;
	block.normal_scale        = v.normal_scale;
	block.occlusion_strength  = v.occlusion_strength;
	block.alpha_cutoff        = v.alpha_cutoff;
	block.clearcoat           = v.clearcoat;
	block.clearcoat_roughness = v.clearcoat_roughness;
	const float transmission[4] = {v.transmission, v.ior, v.thickness, v.attenuation_distance};
	memcpy(block.transmission, transmission, sizeof(transmission));
	memcpy(block.attenuation, v.attenuation_color, sizeof(float) * 3);
	block.attenuation[3] = 0.0f;
	// The maps' bits stay: they follow the textures set, not the values.
	const float *t      = v.uv_transform;
	const float rows[8] = {t[0], t[1], t[2], 0.0f, t[3], t[4], t[5], 0.0f};
	memcpy(block.uv_transform, rows, sizeof(rows));
	memcpy(slot->bytes, &block, sizeof(block));
	slot->dirty = true;
	// Staged for the next submission that reads the lit-material table (D4).
	rbtrend_rt_lit_stage(material->instance, material->rt_lit_slot, &block);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_get_values(rbtrend_material material, RBTREND_LIT_MATERIAL_VALUES *out_values, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	rbtrend_material_slot *slot = nullptr;
	if (const RBT_RESULT lit = rbtrend_lit_material_slot("rbtrend_lit_material_get_values", material, &slot, out_error))
		return lit;
	if (!out_values)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_values is NULL");
	rbtrend_lit_material_block block;
	memcpy(&block, slot->bytes, sizeof(block));
	RBTREND_LIT_MATERIAL_VALUES &v = *out_values;
	v.base.type = RBT_STRUCT_TYPE_REND_LIT_MATERIAL_VALUES; // field by field: the caller's padding stays as it was
	v.base.next = nullptr;
	memcpy(v.base_color, block.base_color, sizeof(v.base_color));
	memcpy(v.emission, block.emission, sizeof(v.emission));
	memcpy(v.sheen_color, block.sheen, sizeof(v.sheen_color));
	v.sheen_roughness     = block.sheen[3];
	v.roughness           = block.roughness;
	v.metalness           = block.metalness;
	v.specular            = block.specular;
	v.normal_scale        = block.normal_scale;
	v.occlusion_strength  = block.occlusion_strength;
	v.alpha_cutoff        = block.alpha_cutoff;
	v.clearcoat           = block.clearcoat;
	v.clearcoat_roughness = block.clearcoat_roughness;
	v.transmission         = block.transmission[0];
	v.ior                  = block.transmission[1];
	v.thickness            = block.transmission[2];
	v.attenuation_distance = block.transmission[3];
	memcpy(v.attenuation_color, block.attenuation, sizeof(v.attenuation_color));
	const float *r        = block.uv_transform;
	const float t[6]      = {r[0], r[1], r[2], r[4], r[5], r[6]};
	memcpy(v.uv_transform, t, sizeof(t));
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_set_map(rbtrend_material material, RBTREND_LIT_MAP map, rbtrend_texture_view view, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	rbtrend_material_slot *slot = nullptr;
	if (const RBT_RESULT lit = rbtrend_lit_material_slot("rbtrend_lit_material_set_map", material, &slot, out_error))
		return lit;
	if (map > RBTREND_LIT_MAP_EMISSION)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "%u is not an RBTREND_LIT_MAP", map);
	if (view && RBT_STALE(view, "rbtrend_lit_material_set_map", "view"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view is destroyed");
	if (view && (view->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_2D || view->desc.aspect != RBTREND_TEXTURE_VIEW_ASPECT_COLOR))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view is not a 2D view of color");
	rbtrend_texture_view given = view ? view : material->instance->builtins.lit.placeholder_2d_view;
	if (const RBT_RESULT set = rbtrend_material_set_texture(material, s_map_bindings[map], 0, given, out_error))
		return set;
	rbtrend_lit_material_block block;
	memcpy(&block, slot->bytes, sizeof(block));
	if (view)
		block.maps |= 1u << map;
	else
		block.maps &= ~(1u << map);
	memcpy(slot->bytes, &block, sizeof(block));
	slot->dirty = true;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_set_map_uv_transform(rbtrend_material material, RBTREND_LIT_MAP map, const float transform[6], RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	rbtrend_material_slot *slot = nullptr;
	if (const RBT_RESULT lit = rbtrend_lit_material_slot("rbtrend_lit_material_set_map_uv_transform", material, &slot, out_error))
		return lit;
	if (map > RBTREND_LIT_MAP_EMISSION)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "%u is not an RBTREND_LIT_MAP", map);
	if (!transform)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transform is NULL");
	if (!all_finite(transform, 6))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a value of transform is not a finite number");
	rbtrend_lit_material_block block;
	memcpy(&block, slot->bytes, sizeof(block));
	const float rows[8] = {transform[0], transform[1], transform[2], 0.0f, transform[3], transform[4], transform[5], 0.0f};
	memcpy(block.map_uv_transform[map], rows, sizeof(rows));
	memcpy(slot->bytes, &block, sizeof(block));
	slot->dirty = true;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_get_map_uv_transform(rbtrend_material material, RBTREND_LIT_MAP map, float out_transform[6], RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	rbtrend_material_slot *slot = nullptr;
	if (const RBT_RESULT lit = rbtrend_lit_material_slot("rbtrend_lit_material_get_map_uv_transform", material, &slot, out_error))
		return lit;
	if (map > RBTREND_LIT_MAP_EMISSION)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "%u is not an RBTREND_LIT_MAP", map);
	if (!out_transform)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_transform is NULL");
	rbtrend_lit_material_block block;
	memcpy(&block, slot->bytes, sizeof(block));
	const float *r = block.map_uv_transform[map];
	out_transform[0] = r[0];
	out_transform[1] = r[1];
	out_transform[2] = r[2];
	out_transform[3] = r[4];
	out_transform[4] = r[5];
	out_transform[5] = r[6];
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_material_set_sampler(rbtrend_material material, rbtrend_sampler sampler, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	rbtrend_material_slot *slot = nullptr;
	if (const RBT_RESULT lit = rbtrend_lit_material_slot("rbtrend_lit_material_set_sampler", material, &slot, out_error))
		return lit;
	if (sampler && RBT_STALE(sampler, "rbtrend_lit_material_set_sampler", "sampler"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "sampler is destroyed");
	return rbtrend_material_set_sampler(material, "material_sampler", 0, sampler ? sampler : material->instance->builtins.lit.material_sampler, out_error);
}

} // extern "C"

// ---- Environments -------------------------------------------------------------------

rbtrend_lit_environment_t::~rbtrend_lit_environment_t() {
	// Each goes through the release queue on its own.
	rbtrend_material_destroy(this->sky_material);
	rbtrend_texture_view_destroy(this->irradiance_cube);
	for (rbtrend_texture_view view : this->irradiance_faces)
		rbtrend_texture_view_destroy(view);
	rbtrend_texture2d_destroy(this->irradiance);
	rbtrend_texture_view_destroy(this->specular_cube);
	for (rbtrend_texture_view view : this->specular_faces)
		rbtrend_texture_view_destroy(view);
	rbtrend_texture2d_destroy(this->specular);
	rbtrend_texture_view_destroy(this->radiance_cube);
	for (rbtrend_texture_view view : this->radiance_faces)
		rbtrend_texture_view_destroy(view);
	rbtrend_texture2d_destroy(this->radiance);
}

// A texture of [layers] square layers of [size] and [mips] for Rend's own
// work, rendered to and sampled; a cube when [layers] is 6.
static rbtrend_texture2d rbtrend_lit_work_texture(rbtrend_instance instance, uint32_t size, uint32_t mips, uint32_t layers, CRGPU_FORMAT format) {
	RBTREND_TEXTURE2D_DESC desc{};
	desc.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC);
	desc.width        = size;
	desc.height       = size;
	desc.format       = format;
	desc.usage        = RBTREND_TEXTURE_USAGE_BIT_RENDER_TARGET | RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE;
	desc.mip_levels   = mips;
	desc.array_layers = layers;
	desc.sample_count = 1;
	desc.cube_capable = layers == 6;
	rbtrend_texture2d texture_made = nullptr;
	(void)rbtrend_texture2d_create(instance, &desc, &texture_made, nullptr);
	return texture_made;
}

static rbtrend_texture_view rbtrend_lit_target_view(rbtrend_texture2d texture, uint32_t mip, uint32_t layer) {
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = RBTREND_TEXTURE_VIEW_TYPE_RENDER_TARGET;
	vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_2D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.base_mip    = mip;
	vd.mip_count   = 1;
	vd.base_layer  = layer;
	vd.layer_count = 1;
	rbtrend_texture_view view_made = nullptr;
	(void)rbtrend_texture_view_create_2d(texture, &vd, &view_made, nullptr);
	return view_made;
}

static rbtrend_texture_view rbtrend_lit_whole_view(rbtrend_texture2d texture, bool cube) {
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE;
	vd.shape       = cube ? RBTREND_TEXTURE_VIEW_SHAPE_CUBE : RBTREND_TEXTURE_VIEW_SHAPE_2D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.base_mip    = 0;
	vd.mip_count   = RBTREND_TEXTURE_VIEW_REMAINING;
	vd.base_layer  = 0;
	vd.layer_count = cube ? 6 : 1;
	rbtrend_texture_view view_made = nullptr;
	(void)rbtrend_texture_view_create_2d(texture, &vd, &view_made, nullptr);
	return view_made;
}

extern "C" {

RBT_RESULT rbtrend_lit_environment_create(
    rbtrend_instance instance,
    const RBTREND_LIT_ENVIRONMENT_DESC *desc,
    rbtrend_lit_environment *out_environment,
    RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_environment)
		*out_environment = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_lit_environment_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_lit_environment_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_LIT_ENVIRONMENT_DESC))
		return base;
	if (!power_of_2(desc->specular_size) || desc->specular_size < 16 || desc->specular_size > 2048)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "specular_size %u is not a power of 2 from 16 to 2048", desc->specular_size);
	if (!power_of_2(desc->irradiance_size) || desc->irradiance_size < 8 || desc->irradiance_size > 128)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "irradiance_size %u is not a power of 2 from 8 to 128", desc->irradiance_size);
	if (!out_environment)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_environment is NULL");
	RBTREND_MALLOC__T(env, rbtrend_lit_environment_t);
	if (!env)
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the environment");
	RBTREND_CONSTRUCT__T(env, rbtrend_lit_environment_t);
	env->instance      = instance;
	env->desc          = *desc;
	env->specular_mips = log2_of(desc->specular_size) - 3 + 1; // down to 8
	try {
		env->specular_faces.resize((size_t)env->specular_mips * 6, nullptr);
	} catch (...) {
		RBTREND_DESTRUCT__T(env, rbtrend_lit_environment_t);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the environment's views");
	}
	env->radiance   = rbtrend_lit_work_texture(instance, desc->specular_size, log2_of(desc->specular_size) + 1, 6, RBTREND_ENVIRONMENT_FORMAT);
	env->specular   = rbtrend_lit_work_texture(instance, desc->specular_size, env->specular_mips, 6, RBTREND_ENVIRONMENT_FORMAT);
	env->irradiance = rbtrend_lit_work_texture(instance, desc->irradiance_size, 1, 6, RBTREND_ENVIRONMENT_FORMAT);
	bool ok         = env->radiance && env->specular && env->irradiance;
	for (uint32_t face = 0; ok && face < 6; ++face) {
		env->radiance_faces[face]   = rbtrend_lit_target_view(env->radiance, 0, face);
		env->irradiance_faces[face] = rbtrend_lit_target_view(env->irradiance, 0, face);
		ok                          = env->radiance_faces[face] && env->irradiance_faces[face];
		for (uint32_t mip = 0; ok && mip < env->specular_mips; ++mip) {
			rbtrend_texture_view &view = env->specular_faces[(size_t)mip * 6 + face];
			view                      = rbtrend_lit_target_view(env->specular, mip, face);
			ok                        = view != nullptr;
		}
	}
	if (ok) {
		env->radiance_cube   = rbtrend_lit_whole_view(env->radiance, true);
		env->specular_cube   = rbtrend_lit_whole_view(env->specular, true);
		env->irradiance_cube = rbtrend_lit_whole_view(env->irradiance, true);
		ok                   = env->radiance_cube && env->specular_cube && env->irradiance_cube;
	}
	if (!ok) {
		RBTREND_DESTRUCT__T(env, rbtrend_lit_environment_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "a texture or view of the environment can't be created");
	}
	*out_environment = env;
	return RBT_RESULT_OK;
}

void rbtrend_lit_environment_destroy(rbtrend_lit_environment environment) {
	if (!environment)
		return;
	if (RBT_STALE(environment, "rbtrend_lit_environment_destroy", "environment"))
		return;
	// What it holds on the GPU goes through the release queue, each on its
	// own; the rest is the CPU's.
	RBT_RETIRE(environment->instance, environment, rbtrend_lit_environment_t);
}

uint32_t rbtrend_lit_environment_get_specular_mip_count(rbtrend_lit_environment environment) {
	if (!environment)
		return 0;
	if (RBT_STALE(environment, "rbtrend_lit_environment_get_specular_mip_count", "environment"))
		return 0;
	return environment->specular_mips;
}

} // extern "C"

rbtrend_material rbtrend_lit_environment_sky_material(rbtrend_lit_environment environment) {
	if (environment->sky_material)
		return environment->sky_material;
	RBTREND_MATERIAL_DESC desc{};
	desc.base                 = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
	desc._layout              = environment->instance->builtins.lit.hosek_layout;
	(void)rbtrend_material_create(environment->instance, &desc, &environment->sky_material, nullptr);
	if (!environment->sky_material)
		rbtrend_report(environment->instance, RBT_RESULT_REND_GPU_FAILURE, "LIT_SKY: the material of an environment's Hosek-Wilkie sky can't be made");
	return environment->sky_material;
}

// ---- Hosek and Wilkie's sky -------------------------------------------------------------
//
// The coefficients are Hosek and Wilkie's, version 1.4a, under their
// licence:
//
//   Copyright (c) 2012 - 2013, Lukas Hosek and Alexander Wilkie
//   All rights reserved. Published under the 3-clause BSD license (the
//   full text is at the top of the dataset, rbtrend_hosek_rgb.inc, which
//   CMakeLists.txt makes from their ArHosekSkyModelData_RGB.h).
//
// The cooking follows their ArHosekSkyModel_CookConfiguration and
// ArHosekSkyModel_CookRadianceConfiguration: each coefficient is a quintic
// Bezier over six control points in the cube root of the sun's elevation,
// mixed between the two turbidities either side and the albedos 0 and 1.

#include "rbtrend_hosek_rgb.inc"

// The Bezier of control points [points] (each [stride] doubles apart) at
// [e], 0 to 1.
static double rbtrend_hosek_bezier(const double *points, int stride, double e) {
	const double f    = 1.0 - e;
	const double b[6] = {f * f * f * f * f,         5.0 * f * f * f * f * e, 10.0 * f * f * f * e * e,
	                     10.0 * f * f * e * e * e, 5.0 * f * e * e * e * e, e * e * e * e * e};
	double sum        = 0.0;
	for (int k = 0; k < 6; ++k)
		sum += b[k] * points[k * stride];
	return sum;
}

// One channel's nine coefficients ([dataset]) or radiance ([rad_dataset]),
// cooked: [count] values, each [count] * 6 doubles to an albedo's
// turbidity, 10 turbidities to an albedo.
static void rbtrend_hosek_cook_channel(const double *dataset, int count, double turbidity, double albedo, double e, double *out) {
	const int lower      = (int)turbidity;
	const double above   = turbidity - (double)lower;
	const int block      = count * 6;
	const double weights[2][2] = {{(1.0 - albedo) * (1.0 - above), (1.0 - albedo) * above}, {albedo * (1.0 - above), albedo * above}};
	for (int i = 0; i < count; ++i) {
		double value = 0.0;
		for (int a = 0; a < 2; ++a) {
			for (int t = 0; t < 2; ++t) {
				// The turbidity above 10 has no data, and no weight there.
				if (lower + t > 10)
					continue;
				const double *points = dataset + block * 10 * a + block * (lower + t - 1) + i;
				value += weights[a][t] * rbtrend_hosek_bezier(points, count, e);
			}
		}
		out[i] = value;
	}
}

// The extended Perez function of [config] at [theta] and [gamma], as their
// ArHosekSkyModel_GetRadianceInternal has it.
static double rbtrend_hosek_perez(const double config[9], double theta, double gamma) {
	const double cos_gamma = std::cos(gamma);
	const double exp_m     = std::exp(config[4] * gamma);
	const double ray_m     = cos_gamma * cos_gamma;
	const double mie_m     = (1.0 + cos_gamma * cos_gamma) / std::pow(1.0 + config[8] * config[8] - 2.0 * config[8] * cos_gamma, 1.5);
	const double zenith    = std::sqrt(std::cos(theta));
	return (1.0 + config[0] * std::exp(config[1] / (std::cos(theta) + 0.01))) *
	       (config[2] + config[3] * exp_m + config[5] * ray_m + config[6] * mie_m + config[7] * zenith);
}

void rbtrend_lit_hosek_cook(const float sun[3], float turbidity, const float albedo[3], float intensity, rbtrend_hosek_sky_block *out) {
	const double pi        = 3.14159265358979323846;
	const double elevation = std::asin(std::clamp((double)sun[1], 0.0, 1.0));
	const double e         = std::pow(elevation / (pi / 2.0), 1.0 / 3.0);
	double config[3][9];
	double radiance[3];
	for (int c = 0; c < 3; ++c) {
		rbtrend_hosek_cook_channel(rbtrend_hosek_datasetsRGB[c], 9, turbidity, albedo[c], e, config[c]);
		rbtrend_hosek_cook_channel(rbtrend_hosek_datasetsRGBRad[c], 1, turbidity, albedo[c], e, &radiance[c]);
	}
	// The zenith's luminance (Rec. 709's, the dataset's primaries), at the
	// sun's angle from it.
	const double zenith_gamma = pi / 2.0 - elevation;
	double zenith[3];
	for (int c = 0; c < 3; ++c)
		zenith[c] = rbtrend_hosek_perez(config[c], 0.0, zenith_gamma) * radiance[c];
	const double luminance = 0.2126 * zenith[0] + 0.7152 * zenith[1] + 0.0722 * zenith[2];
	const double scale     = luminance > 0.0 ? (double)intensity / luminance : 0.0;
	for (int i = 0; i < 9; ++i) {
		for (int c = 0; c < 3; ++c)
			out->config[i][c] = (float)config[c][i];
		out->config[i][3] = 0.0f;
	}
	for (int c = 0; c < 3; ++c)
		out->radiance[c] = (float)(radiance[c] * scale);
	out->radiance[3] = 0.0f;
}

// ---- Scenes -------------------------------------------------------------------------

static const char *const s_directional_masks[RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS] = {"directional_mask_0", "directional_mask_1", "directional_mask_2",
                                                                                   "directional_mask_3"};

// The shadows as set, into the block: each map shadow's views into the view
// table in record order, a directional light's shadow into its record, a
// point or spot light's into the shadow table it names; and each
// directional light's mask bound at its slot (a placeholder where it has
// none). Every view, name and handle here was checked when it was set, so
// binding a mask can't fail.
static void rbtrend_lit_scene_resolve_shadows(rbtrend_lit_scene scene) {
	rbtrend_lit_scene_block &b = scene->block;
	for (uint32_t i = 0; i < RBTREND_LIT_MAX_LIGHTS; ++i)
		b.lights[i].outer_shadow[1] = -1.0f;
	rbtrend_texture_view masks[RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS];
	for (uint32_t d = 0; d < RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS; ++d) {
		memset(b.directional[d].shadow_params, 0, sizeof(b.directional[d].shadow_params));
		memset(b.directional[d].shadow_views, 0, sizeof(b.directional[d].shadow_views));
		memset(b.directional[d].splits, 0, sizeof(b.directional[d].splits));
		masks[d] = scene->instance->builtins.lit.placeholder_2d_view;
	}
	uint32_t views   = 0;
	uint32_t shadows = 0;
	for (uint32_t r = 0; r < scene->shadow_count; ++r) {
		const rbtrend_lit_shadow_record &record = scene->shadows[r];
		const rbtrend_lit_light_entry entry     = scene->lights[record.light];
		if (record.kind == RBTREND_LIT_SHADOW_KIND_MASK) {
			rbtrend_lit_directional_block &d = b.directional[entry.slot];
			d.shadow_views[0]               = RBTREND_LIT_DIRECTIONAL_MASK;
			d.shadow_params[3]              = record.strength;
			masks[entry.slot]               = record.mask;
			continue;
		}
		const uint32_t first = views;
		for (uint32_t v = 0; v < record.view_count; ++v, ++views) {
			rbtrend_lit_shadow_view_block &view = b.shadow_views[views];
			memcpy(view.view_projection, record.view_projection[v], sizeof(view.view_projection));
			view.rect[0] = (float)record.rects[v]._x;
			view.rect[1] = (float)record.rects[v]._y;
			view.rect[2] = (float)record.rects[v]._width;
			view.rect[3] = (float)record.rects[v]._height;
		}
		const float params[4] = {record.depth_bias, record.normal_offset, record.softness, record.strength};
		if (record.kind == RBTREND_LIT_SHADOW_KIND_CASCADES) {
			rbtrend_lit_directional_block &d = b.directional[entry.slot];
			d.shadow_views[0]               = RBTREND_LIT_DIRECTIONAL_CASCADES;
			d.shadow_views[1]               = first;
			d.shadow_views[2]               = record.view_count;
			memcpy(d.shadow_params, params, sizeof(params));
			memcpy(d.splits, record.split_depths, sizeof(d.splits));
			continue;
		}
		rbtrend_lit_shadow_block &shadow = b.shadows[shadows];
		memcpy(shadow.params, params, sizeof(params));
		shadow.views[0]                         = first;
		shadow.views[1]                         = record.view_count;
		shadow.views[2]                         = 0;
		shadow.views[3]                         = 0;
		b.lights[entry.slot].outer_shadow[1] = (float)shadows;
		++shadows;
	}
	for (uint32_t d = 0; d < RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS; ++d)
		(void)rbtrend_material_set_texture(scene->material, s_directional_masks[d], 0, masks[d], nullptr);
}

// The block as the scene holds it, into the materials that read it, for the
// next submission that binds them.
// The fog as set, into the block: its eye the scene's eye less its origin,
// taken in double so a fog far from the scene's origin keeps the eye's
// place to the float's precision of the difference.
static void rbtrend_lit_scene_resolve_fog(rbtrend_lit_scene scene) {
	rbtrend_lit_scene_block &b = scene->block;
	b.flags &= ~(uint32_t)RBTREND_LIT_SCENE_FLAG_FOG;
	if (!scene->has_fog)
		return;
	const RBTREND_LIT_FOG &f = scene->fog;
	for (int i = 0; i < 3; ++i) {
		b.fog_up_base[i] = f.fog.kind == RBTMATH_FOG_HEIGHT_KIND_PLANE ? f.fog.up[i] : 0.0f;
		b.fog_eye[i]     = (float)((double)b.eye_position[i] - (double)f.origin[i]);
		b.fog_albedo[i]  = f.albedo[i];
	}
	b.fog_up_base[3] = f.fog.base;
	b.fog_eye[3]     = f.fog.density;
	b.fog_albedo[3]  = f.phase_g;
	b.fog_range[0]   = f.start_distance;
	b.fog_range[1]   = f.end_distance;
	b.fog_range[2]   = 1.0f - f.max_opacity;
	b.fog_range[3]   = f.fog.scale_height;
	b.fog_light[0]   = (float)f.sun_count;
	b.fog_light[1]   = f.ambient_scale;
	b.fog_light[2]   = 0.0f;
	b.fog_light[3]   = 0.0f;
	b.flags |= RBTREND_LIT_SCENE_FLAG_FOG;
}

// Each placed directional light's values at the scene's eye, into its
// stored direction, versine and radiance (what the sky, the atmosphere's
// tables, the fog and the clouds read): worked out again by each commit,
// so they follow the eye set by rbtrend_lit_scene_set_view() and the
// lights set by rbtrend_lit_scene_set_lights().
static void rbtrend_lit_scene_resolve_placed(rbtrend_lit_scene scene) {
	rbtrend_lit_scene_block &b = scene->block;
	const double eye[3]        = {(double)b.eye_position[0], (double)b.eye_position[1], (double)b.eye_position[2]};
	for (uint32_t i = 0; i < b.directional_count && i < RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS; ++i) {
		rbtrend_lit_directional_block &d = b.directional[i];
		if (!(d.place_reference[3] > 0.0f))
			continue;
		rbtrend_lit_placed_values at;
		rbtrend_lit_placed_values_at(d, eye, at);
		for (int c = 0; c < 3; ++c) {
			d.direction_versine[c] = at.direction[c];
			d.radiance_disc[c]     = at.radiance[c];
		}
		d.direction_versine[3] = at.versine;
	}
}

void rbtrend_lit_scene_commit(rbtrend_lit_scene scene) {
	rbtrend_lit_scene_resolve_placed(scene);
	rbtrend_lit_scene_resolve_shadows(scene);
	rbtrend_lit_scene_resolve_atmosphere(scene);
	rbtrend_lit_scene_resolve_fog(scene);
	rbtrend_lit_write_block(scene->material, "u_lit_scene", &scene->block, sizeof(scene->block));
	rbtrend_lit_write_block(scene->cull_material, "u_lit_scene", &scene->block, sizeof(scene->block));
}

rbtrend_binding_set rbtrend_lit_scene_rt_set(rbtrend_lit_scene scene, rbtrend_binding_layout layout) {
	if (scene->rt_set)
		return scene->rt_set;
	// The block's range of the material arena, which is the scene's for its
	// life and copied into by every submission that binds the scene.
	rbtrend_material_slot *slot = rbtrend_lit_block_slot(scene->material, "u_lit_scene");
	RBTREND_BINDING_SET_ENTRY entry{};
	entry._binding                    = 0;
	entry._type                       = CRGPU_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	entry._resource._uniform._buffer  = slot->arena->buffer;
	entry._resource._uniform._offset  = slot->offset;
	entry._resource._uniform._range   = slot->block_size;
	RBTREND_ERROR error;
	if (rbtrend_binding_set_create(layout, &entry, 1, &scene->rt_set, &error) != RBT_RESULT_OK) {
		rbtrend_report(scene->instance, RBT_RESULT_REND_GPU_FAILURE, "the scene's set for a trace can't be made: %s", error.message);
		return nullptr;
	}
	return scene->rt_set;
}

rbtrend_binding_set rbtrend_lit_scene_ssr_set(rbtrend_lit_scene scene) {
	if (scene->ssr_set)
		return scene->ssr_set;
	// The block's range of the material arena, as rbtrend_lit_scene_rt_set
	// binds it.
	rbtrend_material_slot *slot = rbtrend_lit_block_slot(scene->material, "u_lit_scene");
	RBTREND_BINDING_SET_ENTRY entry{};
	entry._binding                   = 0;
	entry._type                      = CRGPU_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	entry._resource._uniform._buffer = slot->arena->buffer;
	entry._resource._uniform._offset = slot->offset;
	entry._resource._uniform._range  = slot->block_size;
	RBTREND_ERROR error;
	if (rbtrend_binding_set_create(scene->instance->builtins.lit.ssr_scene_layout, &entry, 1, &scene->ssr_set, &error) != RBT_RESULT_OK) {
		rbtrend_report(scene->instance, RBT_RESULT_REND_GPU_FAILURE, "the scene's set for screen-space reflections can't be made: %s", error.message);
		return nullptr;
	}
	return scene->ssr_set;
}

bool rbtrend_lit_scene_depth_reversed(rbtrend_lit_scene scene) {
	// Depth 0 and depth 1 at the middle of the view brought back through
	// the inverse: the one nearer the eye is near. A point at infinity (w
	// 0, as a reversed infinite projection's depth 0) is the far one.
	const float *m   = scene->block.inverse_view_projection;
	const float *eye = scene->block.eye_position;
	float distance[2] = {0.0f, 0.0f};
	for (int z = 0; z < 2; ++z) {
		// Column-major: element (row, column) at column * 4 + row.
		const float h[4] = {m[8] * (float)z + m[12], m[9] * (float)z + m[13], m[10] * (float)z + m[14], m[11] * (float)z + m[15]};
		if (std::fabs(h[3]) < 1e-30f) {
			distance[z] = INFINITY;
			continue;
		}
		const float d[3] = {h[0] / h[3] - eye[0], h[1] / h[3] - eye[1], h[2] / h[3] - eye[2]};
		distance[z]      = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	}
	return distance[1] < distance[0];
}

uint32_t rbtrend_lit_scene_directional_slot(rbtrend_lit_scene scene, uint32_t light) {
	if (light >= scene->light_count)
		return UINT32_MAX;
	if (!rbtrend_lit_type_directional(scene->lights[light].type))
		return UINT32_MAX;
	return scene->lights[light].slot;
}

// The rule's angular radius is capped at 0.5 (rbtrendlit.h), so its versine
// at 1 - cos(0.5).
static const double rbtrend_lit_placed_max_versine = 1.0 - 0.87758256189037271;

void rbtrend_lit_placed_values_at(const rbtrend_lit_directional_block &d, const double point[3], rbtrend_lit_placed_values &out) {
	double v[3];
	double largest = 0.0;
	for (int i = 0; i < 3; ++i) {
		v[i]    = (double)d.place_reference[i] - point[i];
		largest = std::fmax(largest, std::fabs(v[i]));
	}
	// The distance scaled by the largest part first, so no square
	// overflows however far the source is.
	double distance = 0.0;
	if (largest > 0.0) {
		const double x = v[0] / largest, y = v[1] / largest, z = v[2] / largest;
		distance       = largest * std::sqrt(x * x + y * y + z * z);
	}
	const double reference = (double)d.place_reference[3];
	const double radius    = (double)d.placed_radiance_radius[3];
	for (int i = 0; i < 3; ++i)
		out.direction[i] = distance > 0.0 ? (float)(v[i] / distance) : (i == 1 ? 1.0f : 0.0f);
	const double nearest = distance > radius ? distance : radius;
	const double k       = reference / nearest;
	for (int i = 0; i < 3; ++i)
		out.radiance[i] = (float)((double)d.placed_radiance_radius[i] * (k * k));
	// 1 - cos(asin(q)) as q^2 / (1 + sqrt(1 - q^2)), so a small disc keeps
	// its digits.
	const double q       = radius / nearest;
	const double versine = q * q / (1.0 + std::sqrt(1.0 - q * q));
	out.versine          = (float)(versine < rbtrend_lit_placed_max_versine ? versine : rbtrend_lit_placed_max_versine);
}

// What the scene made, for its destroy and a failed create alike. The
// scene itself is the caller's to free: at once after a failed create, and
// with RBT_RETIRE after a destroy (../rbtstale.hpp, card X27).
static void rbtrend_lit_scene_free_parts(rbtrend_lit_scene scene) {
	// The view-set records' sets retire through the release queue, as
	// every destroyed set does; the array is the CPU's.
	for (uint32_t i = 0; i < scene->view_set_count; ++i)
		rbtrend_binding_set_destroy(scene->view_sets[i].set);
	free(scene->view_sets);
	scene->view_sets      = nullptr;
	scene->view_set_count = 0;
	rbtrend_binding_set_destroy(scene->rt_set);
	rbtrend_binding_set_destroy(scene->ssr_set);
	if (scene->material)
		scene->material->lit_scene = nullptr;
	rbtrend_material_destroy(scene->material);
	for (rbtrend_material material : scene->shadow_pass_materials)
		rbtrend_material_destroy(material);
	rbtrend_material_destroy(scene->cull_material);
	rbtrend_material_destroy(scene->occlusion_material);
	rbtrend_buffer_destroy(scene->tiles);
	rbtrend_material_destroy(scene->atmosphere_material);
	for (rbtrend_texture_view view : scene->atmosphere_targets)
		rbtrend_texture_view_destroy(view);
	rbtrend_texture_view_destroy(scene->atmosphere_tables_view);
	rbtrend_texture_view_destroy(scene->sky_view_view);
	rbtrend_texture2d_destroy(scene->atmosphere_tables);
}

// The record of [light]'s shadow in [scene], or NULL if it has none.
static rbtrend_lit_shadow_record *rbtrend_lit_scene_find_shadow(rbtrend_lit_scene scene, uint32_t light) {
	for (uint32_t r = 0; r < scene->shadow_count; ++r)
		if (scene->shadows[r].light == light)
			return &scene->shadows[r];
	return nullptr;
}

// Takes record [r] of [scene]'s shadows away: its pass materials go back to
// the free ones, and the records after it move down, keeping their order.
static void rbtrend_lit_scene_remove_shadow(rbtrend_lit_scene scene, uint32_t r) {
	const rbtrend_lit_shadow_record &record = scene->shadows[r];
	for (uint32_t v = 0; v < record.view_count; ++v)
		scene->free_pass_materials[scene->free_pass_count++] = record.materials[v];
	for (uint32_t i = r + 1; i < scene->shadow_count; ++i)
		scene->shadows[i - 1] = scene->shadows[i];
	--scene->shadow_count;
}

// Whether a shadow of [kind] fits a light of [type].
static bool rbtrend_lit_kind_fits(RBTREND_LIT_SHADOW_KIND kind, uint32_t type) {
	switch (kind) {
	case RBTREND_LIT_SHADOW_KIND_MASK:
	case RBTREND_LIT_SHADOW_KIND_CASCADES:
		return rbtrend_lit_type_directional(type);
	case RBTREND_LIT_SHADOW_KIND_SPOT_MAP:
		return type == RBTREND_LIT_LIGHT_TYPE_SPOT;
	case RBTREND_LIT_SHADOW_KIND_POINT_MAP:
		return type == RBTREND_LIT_LIGHT_TYPE_POINT;
	default:
		return false;
	}
}

extern "C" {

RBT_RESULT rbtrend_lit_scene_create(rbtrend_instance instance, const RBTREND_LIT_SCENE_DESC *desc, rbtrend_lit_scene *out_scene, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_scene)
		*out_scene = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_lit_scene_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_lit_scene_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC))
		return base;
	if (desc->max_view_sets > RBTREND_LIT_SCENE_MAX_VIEW_SETS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc->max_view_sets %u is above RBTREND_LIT_SCENE_MAX_VIEW_SETS (%u)",
		                   desc->max_view_sets, RBTREND_LIT_SCENE_MAX_VIEW_SETS);
	if (!out_scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_scene is NULL");
	rbtrend_binding_layout layout        = nullptr;
	rbtrend_binding_layout shadow_layout = nullptr;
	if (const RBT_RESULT made = rbtrend_lit_get_scene_layout(instance, &layout, out_error))
		return made;
	if (const RBT_RESULT made = rbtrend_lit_get_shadow_layout(instance, &shadow_layout, out_error))
		return made;
	if (!rbtrend_lit_prepare_resources(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit resources can't be made");
	if (!rbtrend_lit_prepare_brdf_lut(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit BRDF table can't be made");
	if (!rbtrend_lit_cull_pipeline(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's light culling pipeline can't be made");
	if (!rbtrend_lit_prepare_passes(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's lit passes can't be made");
	RBTREND_MALLOC__T(scene, rbtrend_lit_scene_t);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the scene");
	RBTREND_CONSTRUCT__T(scene, rbtrend_lit_scene_t);
	scene->instance = instance;
	// The view-set records, empty: every one a compile may need, made
	// here (none for 0).
	if (desc->max_view_sets > 0) {
		scene->view_sets = (rbtrend_view_set *)calloc(desc->max_view_sets, sizeof(rbtrend_view_set));
		if (!scene->view_sets) {
			RBTREND_DESTRUCT__T(scene, rbtrend_lit_scene_t);
			return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the scene's %u view-set records", desc->max_view_sets);
		}
		scene->view_set_count = desc->max_view_sets;
	}
	const rbtrend_lit_builtins &lit = instance->builtins.lit;
	RBTREND_MATERIAL_DESC md{};
	md.base    = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
	md._layout = layout;
	(void)rbtrend_material_create(instance, &md, &scene->material, nullptr);
	md._layout = shadow_layout;
	for (rbtrend_material &material : scene->shadow_pass_materials)
		(void)rbtrend_material_create(instance, &md, &material, nullptr);
	md._layout = lit.cull_layout;
	(void)rbtrend_material_create(instance, &md, &scene->cull_material, nullptr);
	md._layout = lit.occlusion_layout;
	(void)rbtrend_material_create(instance, &md, &scene->occlusion_material, nullptr);

	bool ok = scene->material && scene->cull_material && scene->occlusion_material;
	for (rbtrend_material material : scene->shadow_pass_materials)
		ok = ok && material;
	rbtrend_material m = scene->material;
	ok                = ok && rbtrend_material_set_storage_buffer(m, "RbtrendLitTiles", 0, lit.placeholder_tiles, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_texture(m, "shadow_atlas", 0, lit.placeholder_depth_view, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_sampler(m, "shadow_sampler", 0, lit.shadow_sampler, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_sampler(m, "shadow_sampler_reversed", 0, lit.shadow_sampler_reversed, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_texture(m, "environment_specular", 0, lit.placeholder_cube_view, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_texture(m, "environment_irradiance", 0, lit.placeholder_cube_view, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_texture(m, "brdf_lut", 0, lit.brdf_lut_view, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_sampler(m, "environment_sampler", 0, lit.environment_sampler, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_texture(m, "ambient_occlusion", 0, lit.placeholder_2d_view, nullptr) == RBT_RESULT_OK;
	ok                = ok && rbtrend_material_set_texture(m, "scene_color", 0, lit.placeholder_2d_view, nullptr) == RBT_RESULT_OK;
	for (const char *mask : s_directional_masks)
		ok = ok && rbtrend_material_set_texture(m, mask, 0, lit.placeholder_2d_view, nullptr) == RBT_RESULT_OK;
	ok = ok && rbtrend_material_set_texture(m, "atmosphere_tables", 0, lit.placeholder_2d_array_view, nullptr) == RBT_RESULT_OK;
	ok = ok && rbtrend_material_set_storage_buffer(scene->cull_material, "RbtrendLitTiles", 0, lit.placeholder_tiles, nullptr) == RBT_RESULT_OK;
	ok = ok && rbtrend_lit_block_slot(m, "u_lit_scene") && rbtrend_lit_block_slot(scene->cull_material, "u_lit_scene");
	if (!ok) {
		rbtrend_lit_scene_free_parts(scene);
		RBTREND_DESTRUCT__T(scene, rbtrend_lit_scene_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "the scene's materials can't be made");
	}
	// Every pass material free, handed out from the end down so the first
	// shadow set takes material 0.
	for (uint32_t i = 0; i < RBTREND_LIT_MAX_SHADOW_VIEWS; ++i)
		scene->free_pass_materials[i] = (uint8_t)(RBTREND_LIT_MAX_SHADOW_VIEWS - 1 - i);
	scene->free_pass_count       = RBTREND_LIT_MAX_SHADOW_VIEWS;
	scene->material->lit_scene   = scene;
	scene->block.exposure        = 1.0f;
	scene->block.output_encoding = RBTREND_COLOR_ENCODING_LINEAR;
	rbtrend_lit_scene_commit(scene);
	*out_scene = scene;
	return RBT_RESULT_OK;
}

void rbtrend_lit_scene_destroy(rbtrend_lit_scene scene) {
	if (!scene)
		return;
	if (RBT_STALE(scene, "rbtrend_lit_scene_destroy", "scene"))
		return;
	rbtrend_lit_scene_free_parts(scene);
	RBT_RETIRE(scene->instance, scene, rbtrend_lit_scene_t);
}

rbtrend_material rbtrend_lit_scene_get_material(rbtrend_lit_scene scene) {
	if (!scene)
		return nullptr;
	if (RBT_STALE(scene, "rbtrend_lit_scene_get_material", "scene"))
		return nullptr;
	return scene->material;
}

rbtrend_material rbtrend_lit_scene_get_shadow_pass_material(rbtrend_lit_scene scene, uint32_t light, uint32_t view) {
	if (!scene)
		return nullptr;
	if (RBT_STALE(scene, "rbtrend_lit_scene_get_shadow_pass_material", "scene"))
		return nullptr;
	const rbtrend_lit_shadow_record *record = rbtrend_lit_scene_find_shadow(scene, light);
	if (!record)
		return nullptr;
	if (view >= record->view_count)
		return nullptr;
	return scene->shadow_pass_materials[record->materials[view]];
}

RBT_RESULT rbtrend_lit_scene_set_view(rbtrend_lit_scene scene, const RBTREND_LIT_VIEW *view, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_view", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	if (!view)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view is NULL");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_view");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "view", &view->base, RBT_STRUCT_TYPE_REND_LIT_VIEW))
		return base;
	const RBTREND_LIT_VIEW &v = *view;
	if (!all_finite(v.eye_position, 3) || !all_finite(v.view_direction, 3) || !all_finite(v.view_projection, 16) || !finite(v.exposure) ||
	    v.exposure < 0.0f) {
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a value is not a finite number, or the exposure is negative");
	}
	if (v.output_encoding > RBTREND_COLOR_ENCODING_SRGB)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "%u is not an RBTREND_COLOR_ENCODING", v.output_encoding);
	if ((v.target_width == 0) != (v.target_height == 0) || v.target_width > 16384 || v.target_height > 16384)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a target of %u by %u: both sides 0, or both from 1 to 16384", v.target_width, v.target_height);
	glm::mat4 vp;
	memcpy(&vp, v.view_projection, sizeof(vp));
	bool projected = false;
	for (int i = 0; i < 16 && !projected; ++i)
		projected = v.view_projection[i] != 0.0f;

	// The tiles, for a target with a projection: a buffer of the right
	// size, made again when the number of tiles changes.
	const bool tiled               = projected && v.target_width > 0;
	const uint32_t tiles_x         = tiled ? (v.target_width + RBTREND_LIT_TILE_SIZE - 1) / RBTREND_LIT_TILE_SIZE : 0;
	const uint32_t tiles_y         = tiled ? (v.target_height + RBTREND_LIT_TILE_SIZE - 1) / RBTREND_LIT_TILE_SIZE : 0;
	rbtrend_lit_scene_block &b      = scene->block;
	const rbtrend_lit_builtins &lit = scene->instance->builtins.lit;
	if (tiles_x != b.tiles_x || tiles_y != b.tiles_y || (tiled && !scene->tiles)) {
		rbtrend_buffer tiles = nullptr;
		if (tiled) {
			const uint64_t size = (uint64_t)tiles_x * tiles_y * (RBTREND_LIT_MAX_TILE_LIGHTS + 1) * sizeof(uint32_t);
			const RBT_RESULT made = rbtrend_lit_storage_buffer(scene->instance, size, true, "rbtrend lit tiles", &tiles, out_error);
			if (made != RBT_RESULT_OK)
				return made;
		}
		rbtrend_buffer buffer = tiles ? tiles : lit.placeholder_tiles;
		RBT_RESULT set         = rbtrend_material_set_storage_buffer(scene->material, "RbtrendLitTiles", 0, buffer, out_error);
		if (set == RBT_RESULT_OK)
			set = rbtrend_material_set_storage_buffer(scene->cull_material, "RbtrendLitTiles", 0, buffer, out_error);
		if (set != RBT_RESULT_OK) {
			rbtrend_buffer_destroy(tiles);
			return set;
		}
		rbtrend_buffer_destroy(scene->tiles);
		scene->tiles = tiles;
	}
	memcpy(b.eye_position, v.eye_position, sizeof(float) * 3);
	b.eye_position[3] = 1.0f;
	unit3(v.view_direction, b.view_direction);
	memcpy(b.view_projection, v.view_projection, sizeof(b.view_projection));
	// A view_projection that can't be inverted leaves rays and positions
	// at 0: nothing that reads them is right, and nothing is undefined.
	const glm::mat4 inverse = projected && std::fabs(glm::determinant(vp)) > 1e-30f ? glm::inverse(vp) : glm::mat4(0.0f);
	memcpy(b.inverse_view_projection, &inverse, sizeof(b.inverse_view_projection));
	b.target_size[0]  = (float)v.target_width;
	b.target_size[1]  = (float)v.target_height;
	b.target_size[2]  = v.target_width ? 1.0f / (float)v.target_width : 0.0f;
	b.target_size[3]  = v.target_height ? 1.0f / (float)v.target_height : 0.0f;
	b.exposure        = v.exposure;
	b.output_encoding = v.output_encoding;
	b.tile_size       = RBTREND_LIT_TILE_SIZE;
	b.tiles_x         = tiles_x;
	b.tiles_y         = tiles_y;
	if (tiled)
		b.flags |= RBTREND_LIT_SCENE_FLAG_TILES;
	else
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_TILES;
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

} // extern "C"

// Whether any of [v]'s three is below 0.
static bool any_negative3(const float v[3]) {
	bool negative = false;
	for (int i = 0; i < 3; ++i)
		negative = negative || v[i] < 0.0f;
	return negative;
}

// Whether [l] is a light rbtrend_lit_scene_set_lights() takes; [error] names
// it by its [index] if not.
static RBT_RESULT rbtrend_lit_light_valid(const RBTREND_LIT_SCENE_LIGHT &l, uint32_t index, RBTREND_ERROR *error) {
	const float half_pi = 1.57079632679f;
	if (l._type > RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u has type %u, which is not an RBTREND_LIT_LIGHT_TYPE", index, l._type);
	if (!all_finite(l._position, 3))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _position is not finite", index);
	if (!all_finite(l._direction, 3))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _direction is not finite", index);
	if (!all_finite(l._radiance, 3))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _radiance is not finite", index);
	if (!finite(l._range))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _range is not finite", index);
	if (!finite(l._inner_angle))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _inner_angle is not finite", index);
	if (!finite(l._outer_angle))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _outer_angle is not finite", index);
	if (!finite(l._radius))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u's _radius is not finite", index);
	if (any_negative3(l._radiance))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u has a negative _radiance", index);
	const bool reads_direction = l._type == RBTREND_LIT_LIGHT_TYPE_SPOT || l._type == RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL;
	const bool directional     = rbtrend_lit_type_directional(l._type);
	if (reads_direction && length3(l._direction) == 0.0f)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u, a spot or directional light, has a _direction of 0", index);
	if (l._type != RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL && !(l._range > 0.0f))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u, a point, spot or placed directional light, has a _range that is not above 0",
		                    index);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_SPOT && l._inner_angle < 0.0f)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "spot light %u's _inner_angle is negative", index);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_SPOT && l._inner_angle > l._outer_angle)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "spot light %u's _inner_angle is past its _outer_angle", index);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_SPOT && l._outer_angle > half_pi)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "spot light %u's _outer_angle is past pi / 2", index);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL && l._radius < 0.0f)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "directional light %u's _radius is negative", index);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL && l._radius > 0.5f)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "directional light %u's _radius %g is above 0.5", index, (double)l._radius);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED && !(l._radius > 0.0f))
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "placed directional light %u's _radius is not above 0", index);
	if (l._type == RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED && l._radius > 0.5f)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "placed directional light %u's _radius %g is above 0.5", index, (double)l._radius);
	if (!directional && l._radius != 0.0f)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u, a point or spot light, has a _radius that is not 0 (card A38)", index);
	if (!directional && l._sky_disc)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "light %u, a point or spot light, has _sky_disc set", index);
	return RBT_RESULT_OK;
}

// Whether every rectangle of [maps]' first [count] views lies inside an
// atlas of [width] by [height]: their index, or UINT32_MAX if they do.
static uint32_t rbtrend_lit_rect_outside(const RBTREND_LIT_SHADOW_RECT *rects, uint32_t count, uint32_t width, uint32_t height) {
	for (uint32_t v = 0; v < count; ++v) {
		const RBTREND_LIT_SHADOW_RECT &r = rects[v];
		if ((uint64_t)r._x + r._width > width || (uint64_t)r._y + r._height > height)
			return v;
	}
	return UINT32_MAX;
}

extern "C" {

RBT_RESULT rbtrend_lit_scene_set_lights(rbtrend_lit_scene scene, const RBTREND_LIT_SCENE_LIGHT *lights, uint32_t count, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_lights", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_lights");
	if (count > 0 && !lights)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "lights is NULL with a count of %u", count);
	uint32_t point_or_spot = 0;
	uint32_t directional   = 0;
	for (uint32_t i = 0; i < count; ++i) {
		if (const RBT_RESULT valid = rbtrend_lit_light_valid(lights[i], i, out_error))
			return valid;
		if (rbtrend_lit_type_directional(lights[i]._type))
			++directional;
		else
			++point_or_spot;
	}
	if (point_or_spot > RBTREND_LIT_MAX_LIGHTS)
		return rbtrend_fail(out_error, RBT_RESULT_LIMIT, "%u point and spot lights, and a scene holds %u", point_or_spot, RBTREND_LIT_MAX_LIGHTS);
	if (directional > RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS)
		return rbtrend_fail(out_error, RBT_RESULT_LIMIT, "%u directional lights, and a scene holds %u", directional, RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS);
	rbtrend_lit_scene_block &b = scene->block;
	memset(b.lights, 0, sizeof(b.lights));
	for (rbtrend_lit_directional_block &d : b.directional) {
		memset(d.direction_versine, 0, sizeof(d.direction_versine));
		memset(d.radiance_disc, 0, sizeof(d.radiance_disc));
		memset(d.place_reference, 0, sizeof(d.place_reference));
		memset(d.placed_radiance_radius, 0, sizeof(d.placed_radiance_radius));
	}
	uint32_t slots[2] = {0, 0};
	for (uint32_t i = 0; i < count; ++i) {
		const RBTREND_LIT_SCENE_LIGHT &l = lights[i];
		const bool is_directional       = rbtrend_lit_type_directional(l._type);
		const uint32_t slot             = slots[is_directional ? 1 : 0]++;
		scene->lights[i].type           = (uint8_t)l._type;
		scene->lights[i].slot           = (uint8_t)slot;
		if (l._type == RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED) {
			// Its place, reference and radiance there, and its source's
			// radius, worked out in double; its values at the eye by the
			// commit below.
			rbtrend_lit_directional_block &d = b.directional[slot];
			memcpy(d.place_reference, l._position, sizeof(float) * 3);
			d.place_reference[3] = l._range;
			memcpy(d.placed_radiance_radius, l._radiance, sizeof(float) * 3);
			d.placed_radiance_radius[3] = (float)((double)l._range * std::sin((double)l._radius));
			d.radiance_disc[3]          = l._sky_disc ? 1.0f : 0.0f;
			continue;
		}
		if (is_directional) {
			rbtrend_lit_directional_block &d = b.directional[slot];
			unit3(l._direction, d.direction_versine);
			// 1 - cos of the radius: 0 for none. Worked out in double, since
			// for the earth's sun it is 1e-5.
			d.direction_versine[3] = (float)(1.0 - std::cos((double)l._radius));
			memcpy(d.radiance_disc, l._radiance, sizeof(float) * 3);
			d.radiance_disc[3] = l._sky_disc ? 1.0f : 0.0f;
			continue;
		}
		rbtrend_lit_light_block &o = b.lights[slot];
		memcpy(o.position_range, l._position, sizeof(float) * 3);
		o.position_range[3] = l._range;
		memcpy(o.radiance_type, l._radiance, sizeof(float) * 3);
		o.radiance_type[3] = l._type == RBTREND_LIT_LIGHT_TYPE_SPOT ? 1.0f : 0.0f;
		unit3(l._direction, o.direction_inner);
		o.direction_inner[3] = std::cos(l._inner_angle);
		o.outer_shadow[0]    = std::cos(l._outer_angle);
	}
	scene->light_count  = count;
	b.light_count       = point_or_spot;
	b.directional_count = directional;
	// Each shadow whose index still names a light of a type its kind fits
	// stays; the rest go.
	for (uint32_t r = scene->shadow_count; r-- > 0;) {
		const rbtrend_lit_shadow_record &record = scene->shadows[r];
		if (record.light >= count || !rbtrend_lit_kind_fits(record.kind, lights[record.light]._type))
			rbtrend_lit_scene_remove_shadow(scene, r);
	}
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_shadow_atlas(rbtrend_lit_scene scene, rbtrend_texture_view atlas, bool reversed_depth, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_shadow_atlas", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_shadow_atlas");
	if (atlas && RBT_STALE(atlas, "rbtrend_lit_scene_set_shadow_atlas", "atlas"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atlas is destroyed");
	if (atlas && atlas->instance != scene->instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atlas is of another instance");
	if (atlas && atlas->view_type != RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atlas is not a SHADER_RESOURCE view");
	if (atlas && atlas->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_2D)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atlas is not a 2D view");
	if (atlas && atlas->desc.aspect != RBTREND_TEXTURE_VIEW_ASPECT_DEPTH)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atlas is not a view of depth");
	const uint32_t width  = atlas ? rbtrend_mip_size(atlas->texture._t2d->desc.width, atlas->desc.base_mip) : 0;
	const uint32_t height = atlas ? rbtrend_mip_size(atlas->texture._t2d->desc.height, atlas->desc.base_mip) : 0;
	for (uint32_t r = 0; atlas && r < scene->shadow_count; ++r) {
		const rbtrend_lit_shadow_record &record = scene->shadows[r];
		const uint32_t outside                 = rbtrend_lit_rect_outside(record.rects, record.view_count, width, height);
		if (outside != UINT32_MAX)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "light %u's shadow has view %u's rectangle past an atlas of %u by %u", record.light,
			                   outside, width, height);
	}
	const rbtrend_lit_builtins &lit = scene->instance->builtins.lit;
	if (const RBT_RESULT set = rbtrend_material_set_texture(scene->material, "shadow_atlas", 0, atlas ? atlas : lit.placeholder_depth_view, out_error))
		return set;
	scene->atlas        = atlas;
	scene->atlas_width  = width;
	scene->atlas_height = height;
	rbtrend_lit_scene_block &b = scene->block;
	b.shadow_atlas_size[0]    = (float)width;
	b.shadow_atlas_size[1]    = (float)height;
	b.shadow_atlas_size[2]    = width ? 1.0f / (float)width : 0.0f;
	b.shadow_atlas_size[3]    = height ? 1.0f / (float)height : 0.0f;
	if (atlas)
		b.flags |= RBTREND_LIT_SCENE_FLAG_SHADOW_ATLAS;
	else
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_SHADOW_ATLAS;
	if (reversed_depth)
		b.flags |= RBTREND_LIT_SCENE_FLAG_SHADOW_REVERSED;
	else
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_SHADOW_REVERSED;
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_light_shadow(rbtrend_lit_scene scene, uint32_t light, const RBTREND_LIT_LIGHT_SHADOW *shadow,
                                           RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_light_shadow", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_light_shadow");
	if (light >= scene->light_count)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "light %u names no light of the scene, which has %u", light, scene->light_count);
	rbtrend_lit_shadow_record *existing = rbtrend_lit_scene_find_shadow(scene, light);
	if (!shadow) {
		if (existing)
			rbtrend_lit_scene_remove_shadow(scene, (uint32_t)(existing - scene->shadows));
		rbtrend_lit_scene_commit(scene);
		return RBT_RESULT_OK;
	}
	// The shadow's own type; what is chained on is its kind's, checked below.
	if (shadow->base.type != RBT_STRUCT_TYPE_REND_LIT_LIGHT_SHADOW)
		return rbtrend_fail(out_error, RBT_RESULT_STRUCT_WRONG_TYPE, "shadow %s", rbt_struct_check_message(RBT_RESULT_STRUCT_WRONG_TYPE));
	const RBTREND_LIT_LIGHT_SHADOW &s = *shadow;
	const uint32_t type              = scene->lights[light].type;
	if (s._kind == RBTREND_LIT_SHADOW_KIND_NONE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_kind is RBTREND_LIT_SHADOW_KIND_NONE");
	if (s._kind > RBTREND_LIT_SHADOW_KIND_POINT_MAP)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_kind %u is not an RBTREND_LIT_SHADOW_KIND", (unsigned)s._kind);
	if (!rbtrend_lit_kind_fits(s._kind, type))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_kind %u is not a kind light %u, of type %u, can have", (unsigned)s._kind, light, type);
	if (!finite(s._strength))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_strength is not finite");
	if (s._strength < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_strength is below 0");
	if (s._strength > 1.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_strength is above 1");
	const uint32_t held = existing ? existing->view_count : 0;
	if (s._kind == RBTREND_LIT_SHADOW_KIND_MASK) {
		if (s.base.next)
			return rbtrend_fail(out_error, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN, "shadow is a mask, and nothing is chained onto a mask");
		if (!s._mask)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_mask is NULL");
		if (RBT_STALE(s._mask, "rbtrend_lit_scene_set_light_shadow", "shadow->_mask"))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_mask is destroyed");
		if (s._mask->instance != scene->instance)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_mask is of another instance");
		if (s._mask->view_type != RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_mask is not a SHADER_RESOURCE view");
		if (s._mask->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_2D)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_mask is not a 2D view");
		if (s._mask->desc.aspect != RBTREND_TEXTURE_VIEW_ASPECT_COLOR)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow->_mask is not a view of color");
		rbtrend_lit_shadow_record *record = existing;
		if (!record) {
			record = &scene->shadows[scene->shadow_count++];
		} else {
			for (uint32_t v = 0; v < record->view_count; ++v)
				scene->free_pass_materials[scene->free_pass_count++] = record->materials[v];
		}
		*record          = rbtrend_lit_shadow_record{};
		record->light    = light;
		record->kind     = s._kind;
		record->strength = s._strength;
		record->mask     = s._mask;
		rbtrend_lit_scene_commit(scene);
		return RBT_RESULT_OK;
	}
	if (!s.base.next)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "shadow is of a map kind, and has no RBTREND_LIT_SHADOW_MAPS chained on");
	const RBTREND_LIT_SHADOW_MAPS &maps = *(const RBTREND_LIT_SHADOW_MAPS *)s.base.next;
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "shadow's chained RBTREND_LIT_SHADOW_MAPS", &maps.base, RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS))
		return base;
	const uint32_t wanted = s._kind == RBTREND_LIT_SHADOW_KIND_POINT_MAP ? 6u : 1u;
	if (s._kind == RBTREND_LIT_SHADOW_KIND_CASCADES && (maps._view_count == 0 || maps._view_count > RBTREND_LIT_MAX_CASCADES))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_view_count %u: cascades are 1 to %u views", maps._view_count, RBTREND_LIT_MAX_CASCADES);
	if (s._kind != RBTREND_LIT_SHADOW_KIND_CASCADES && maps._view_count != wanted)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_view_count %u: a %s is %u", maps._view_count,
		                   s._kind == RBTREND_LIT_SHADOW_KIND_POINT_MAP ? "point light's map" : "spot's map", wanted);
	const uint32_t views = maps._view_count;
	if (!all_finite(&maps._view_projection[0][0], (size_t)views * 16))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a value of _view_projection is not finite");
	if (s._kind == RBTREND_LIT_SHADOW_KIND_CASCADES && !all_finite(maps._split_depths, views))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a value of _split_depths is not finite");
	if (!finite(maps._depth_bias))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_depth_bias is not finite");
	if (!finite(maps._normal_offset))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_normal_offset is not finite");
	if (!finite(maps._softness))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_softness is not finite");
	for (uint32_t v = 0; v < views; ++v) {
		if (maps._rects[v]._width == 0)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view %u's rectangle has a _width of 0", v);
		if (maps._rects[v]._height == 0)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view %u's rectangle has a _height of 0", v);
	}
	if (!scene->atlas)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the scene has no shadow atlas (rbtrend_lit_scene_set_shadow_atlas()) for the rectangles to be in");
	const uint32_t outside = rbtrend_lit_rect_outside(maps._rects, views, scene->atlas_width, scene->atlas_height);
	if (outside != UINT32_MAX)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view %u's rectangle is past the atlas, %u by %u", outside, scene->atlas_width, scene->atlas_height);
	for (uint32_t c = 0; s._kind == RBTREND_LIT_SHADOW_KIND_CASCADES && c < views; ++c) {
		if (!(maps._split_depths[c] > 0.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "cascade %u's split is not above 0", c);
		if (c > 0 && !(maps._split_depths[c] > maps._split_depths[c - 1]))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "cascade %u's split is not above the one before", c);
	}
	if (maps._softness < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_softness is below 0");
	if (maps._softness > 4.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_softness is above 4");
	if (maps._depth_bias < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_depth_bias is negative");
	if (maps._normal_offset < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "_normal_offset is negative");
	if (views > held && views - held > scene->free_pass_count)
		return rbtrend_fail(out_error, RBT_RESULT_LIMIT, "the scene's shadows would draw %u views, and a scene draws %u",
		                   RBTREND_LIT_MAX_SHADOW_VIEWS - scene->free_pass_count - held + views, RBTREND_LIT_MAX_SHADOW_VIEWS);

	rbtrend_lit_shadow_record *record = existing;
	uint8_t materials[6]             = {};
	if (record)
		memcpy(materials, record->materials, sizeof(materials));
	else
		record = &scene->shadows[scene->shadow_count++];
	// The views it held keep their materials, so one taken for a view before
	// holds the new matrix; more come from the free ones, and those past the
	// new count go back.
	for (uint32_t v = held; v < views; ++v)
		materials[v] = scene->free_pass_materials[--scene->free_pass_count];
	for (uint32_t v = views; v < held; ++v)
		scene->free_pass_materials[scene->free_pass_count++] = materials[v];
	*record               = rbtrend_lit_shadow_record{};
	record->light         = light;
	record->kind          = s._kind;
	record->strength      = s._strength;
	record->view_count    = views;
	record->depth_bias    = maps._depth_bias;
	record->normal_offset = maps._normal_offset;
	record->softness      = maps._softness;
	memcpy(record->materials, materials, sizeof(materials));
	memcpy(record->view_projection, maps._view_projection, sizeof(float) * 16 * views);
	memcpy(record->rects, maps._rects, sizeof(RBTREND_LIT_SHADOW_RECT) * views);
	if (s._kind == RBTREND_LIT_SHADOW_KIND_CASCADES)
		memcpy(record->split_depths, maps._split_depths, sizeof(float) * views);
	for (uint32_t v = 0; v < views; ++v) {
		rbtrend_lit_shadow_pass_block pass{};
		memcpy(pass.view_projection, record->view_projection[v], sizeof(pass.view_projection));
		rbtrend_lit_write_block(scene->shadow_pass_materials[materials[v]], "u_lit_shadow_pass", &pass, sizeof(pass));
	}
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_ambient(rbtrend_lit_scene scene, const RBTREND_LIT_AMBIENT *ambient, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_ambient", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	if (!ambient)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "ambient is NULL");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_ambient");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "ambient", &ambient->base, RBT_STRUCT_TYPE_REND_LIT_AMBIENT))
		return base;
	const RBTREND_LIT_AMBIENT &a = *ambient;
	if (!all_finite(a.up, 3) || !all_finite(a.sky, 3) || !all_finite(a.ground, 3) || !finite(a.environment_intensity) ||
	    !all_finite(a.environment_rotation, 9)) {
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a value is not a finite number");
	}
	bool colored = false;
	for (int i = 0; i < 3; ++i) {
		if (a.sky[i] < 0.0f || a.ground[i] < 0.0f)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a color is negative");
		colored = colored || a.sky[i] > 0.0f || a.ground[i] > 0.0f;
	}
	if (a.environment_intensity < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "environment_intensity is negative");
	if (colored && length3(a.up) == 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the hemisphere has colors and no up");
	if (a.environment && a.environment->instance != scene->instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "environment is of another instance");
	const rbtrend_lit_builtins &lit = scene->instance->builtins.lit;
	rbtrend_texture_view specular   = a.environment ? a.environment->specular_cube : lit.placeholder_cube_view;
	rbtrend_texture_view irradiance = a.environment ? a.environment->irradiance_cube : lit.placeholder_cube_view;
	if (const RBT_RESULT set = rbtrend_material_set_texture(scene->material, "environment_specular", 0, specular, out_error))
		return set;
	if (const RBT_RESULT set = rbtrend_material_set_texture(scene->material, "environment_irradiance", 0, irradiance, out_error))
		return set;
	scene->environment        = a.environment;
	rbtrend_lit_scene_block &b = scene->block;
	unit3(a.up, b.ambient_up);
	memcpy(b.ambient_sky, a.sky, sizeof(float) * 3);
	b.ambient_sky[3] = 0.0f;
	memcpy(b.ambient_ground, a.ground, sizeof(float) * 3);
	b.ambient_ground[3] = 0.0f;
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column)
			b.environment_rotation[row * 4 + column] = a.environment_rotation[row * 3 + column];
		b.environment_rotation[row * 4 + 3] = 0.0f;
	}
	b.environment_intensity = a.environment_intensity;
	b.environment_mips      = a.environment ? (float)a.environment->specular_mips : 0.0f;
	if (a.environment)
		b.flags |= RBTREND_LIT_SCENE_FLAG_ENVIRONMENT;
	else
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_ENVIRONMENT;
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_fog(rbtrend_lit_scene scene, const RBTREND_LIT_FOG *fog, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_fog", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_fog");
	if (!fog) {
		scene->has_fog = false;
		rbtrend_lit_scene_commit(scene);
		return RBT_RESULT_OK;
	}
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "fog", &fog->base, RBT_STRUCT_TYPE_REND_LIT_FOG))
		return base;
	const RBTREND_LIT_FOG &f = *fog;
	// Rabbittus Math's own checks of the fog, field by field in its order:
	// asked for no density, it checks the fog alone.
	RBTMATH_ERROR checked{};
	const RBT_RESULT valid = rbtmath_height_fog_compute_density(&f.fog, nullptr, 0, nullptr, &checked);
	if (valid != RBT_RESULT_OK)
		return rbtrend_fail(out_error, valid, "fog->fog: %s", checked.message);
	if (!all_finite(f.origin, 3))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->origin is not finite");
	if (!all_finite(f.albedo, 3))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->albedo is not finite");
	for (int i = 0; i < 3; ++i) {
		if (!(f.albedo[i] >= 0.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->albedo[%d], %g, is below 0", i, (double)f.albedo[i]);
		if (!(f.albedo[i] <= 1.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->albedo[%d], %g, is above 1", i, (double)f.albedo[i]);
	}
	if (!finite(f.phase_g))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->phase_g is not finite");
	if (!(f.phase_g >= -0.999f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->phase_g, %g, is below -0.999", (double)f.phase_g);
	if (!(f.phase_g <= 0.999f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->phase_g, %g, is above 0.999", (double)f.phase_g);
	if (f.sun_count > RBTREND_LIT_FOG_MAX_SUNS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->sun_count, %u, is above RBTREND_LIT_FOG_MAX_SUNS (%u)", f.sun_count,
		                   RBTREND_LIT_FOG_MAX_SUNS);
	if (!finite(f.ambient_scale))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->ambient_scale is not finite");
	if (!(f.ambient_scale >= 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->ambient_scale, %g, is below 0", (double)f.ambient_scale);
	if (!finite(f.start_distance))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->start_distance is not finite");
	if (!(f.start_distance >= 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->start_distance, %g, is below 0", (double)f.start_distance);
	if (!finite(f.end_distance))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->end_distance is not finite");
	if (!(f.end_distance > f.start_distance))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->end_distance, %g, is not above its start_distance, %g", (double)f.end_distance,
		                   (double)f.start_distance);
	if (!finite(f.max_opacity))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->max_opacity is not finite");
	if (!(f.max_opacity >= 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->max_opacity, %g, is below 0", (double)f.max_opacity);
	if (!(f.max_opacity <= 1.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fog->max_opacity, %g, is above 1", (double)f.max_opacity);
	scene->fog     = f;
	scene->has_fog = true;
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_ambient_occlusion(rbtrend_lit_scene scene, rbtrend_texture_view occlusion, float strength, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_ambient_occlusion", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_ambient_occlusion");
	if (!finite(strength) || strength < 0.0f || strength > 1.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "strength is not from 0 to 1");
	if (occlusion && RBT_STALE(occlusion, "rbtrend_lit_scene_set_ambient_occlusion", "occlusion"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "occlusion is destroyed");
	if (occlusion && (occlusion->instance != scene->instance || occlusion->view_type != RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE ||
	                  occlusion->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_2D || occlusion->desc.aspect != RBTREND_TEXTURE_VIEW_ASPECT_COLOR)) {
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "occlusion is not a SHADER_RESOURCE 2D view of color of the scene's instance");
	}
	if (const RBT_RESULT set = rbtrend_material_set_texture(scene->material, "ambient_occlusion", 0, occlusion ? occlusion : scene->instance->builtins.lit.placeholder_2d_view, out_error))
		return set;
	rbtrend_lit_scene_block &b = scene->block;
	b.occlusion_strength      = strength;
	if (occlusion)
		b.flags |= RBTREND_LIT_SCENE_FLAG_OCCLUSION;
	else
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_OCCLUSION;
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_scene_color(rbtrend_lit_scene scene, rbtrend_texture_view scene_color, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_scene_color", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_scene_color");
	if (scene_color && RBT_STALE(scene_color, "rbtrend_lit_scene_set_scene_color", "scene_color"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene_color is destroyed");
	if (scene_color && scene_color->instance != scene->instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene_color is of another instance");
	if (scene_color && scene_color->view_type != RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene_color is not a SHADER_RESOURCE view");
	if (scene_color && scene_color->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_2D)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene_color is not a 2D view");
	if (scene_color && scene_color->desc.aspect != RBTREND_TEXTURE_VIEW_ASPECT_COLOR)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene_color is not a view of color");
	if (const RBT_RESULT set = rbtrend_material_set_texture(scene->material, "scene_color", 0, scene_color ? scene_color : scene->instance->builtins.lit.placeholder_2d_view, out_error))
		return set;
	rbtrend_lit_scene_block &b = scene->block;
	if (scene_color) {
		b.flags |= RBTREND_LIT_SCENE_FLAG_SCENE_COLOR;
		b.scene_color_mips = (float)scene_color->desc.mip_count;
	} else {
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_SCENE_COLOR;
		b.scene_color_mips = 0.0f;
	}
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_scene_set_bent_normals(rbtrend_lit_scene scene, bool read, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_bent_normals", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_bent_normals");
	rbtrend_lit_scene_block &b = scene->block;
	if (read)
		b.flags |= RBTREND_LIT_SCENE_FLAG_BENT_NORMALS;
	else
		b.flags &= ~RBTREND_LIT_SCENE_FLAG_BENT_NORMALS;
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

// ---- Fitting shadows --------------------------------------------------------------

// The offset, in clip units, that moves a cascade's matrix by under a
// texel so that the fixed space's origin (the scene place -o) lands on a
// whole texel (RBTREND_LIT_CASCADE_ANCHOR). Its landing is the matrix's x and
// y rows applied to -o, scaled to texels: products of a float entry (24
// bits) and the half map size (at most 2^31) are exact in double; each
// product with a coordinate of o is made exact as a pair (Dekker's product,
// Veltkamp's split, no fused multiply-add), and the pairs summed exactly
// (Knuth's two-sum), so the texel's fraction is exact to about 2^-50 of a
// texel however far o is, the same bits everywhere (contraction off).
namespace {
struct rbtrend_two {
	double hi, lo;
};
inline rbtrend_two rbtrend_two_sum(double a, double b) {
	const double s  = a + b;
	const double bb = s - a;
	return {s, (a - (s - bb)) + (b - bb)};
}
inline void rbtrend_split(double a, double &hi, double &lo) {
	const double c = 134217729.0 * a; // 2^27 + 1
	hi             = c - (c - a);
	lo             = a - hi;
}
inline rbtrend_two rbtrend_two_product(double a, double b) {
	const double p = a * b;
	double ah, al, bh, bl;
	rbtrend_split(a, ah, al);
	rbtrend_split(b, bh, bl);
	return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}
// A sum of pairs kept as an unevaluated sum: hi carries, lo collects.
inline rbtrend_two rbtrend_add(rbtrend_two a, double b) {
	const rbtrend_two s = rbtrend_two_sum(a.hi, b);
	return rbtrend_two_sum(s.hi, s.lo + a.lo);
}
} // namespace

static glm::vec2 rbtrend_anchored_offset(const glm::mat4 &matrix, const RbtDVec3 &o, uint32_t map_size) {
	const double half = (double)map_size * 0.5;
	const double neg[3] = {-o.x, -o.y, -o.z};
	glm::vec2 offset(0.0f);
	for (int r = 0; r < 2; ++r) {
		// at = sum_j (M[j][r] half) (-o_j) + M[3][r] half, exactly as a pair.
		rbtrend_two at = {(double)matrix[3][r] * half, 0.0};
		for (int j = 0; j < 3; ++j) {
			const rbtrend_two p = rbtrend_two_product((double)matrix[j][r] * half, neg[j]);
			at                  = rbtrend_add(at, p.hi);
			at                  = rbtrend_add(at, p.lo);
		}
		// round(at) - at, from the pair: hi's whole part is exact (|hi| far
		// below 2^52 for any finite cascade), its fraction and lo summed.
		const double whole = std::floor(at.hi + 0.5);
		double fraction    = (at.hi - whole) + at.lo;
		const double nudge = std::floor(fraction + 0.5); // lo may carry the pair past a half
		fraction           = fraction - nudge;
		offset[r]          = (float)(-fraction / half);
	}
	return offset;
}

RBT_RESULT rbtrend_lit_fit_cascades(const RBTREND_LIT_CASCADE_FIT *fit, RBTREND_LIT_SHADOW_MAPS *out_maps, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!fit)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fit is NULL");
	if (!out_maps)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_maps is NULL");
	// The fit's chain: an RBTREND_LIT_CASCADE_ANCHOR, once, and nothing else.
	if (fit->base.type != RBT_STRUCT_TYPE_REND_LIT_CASCADE_FIT)
		return rbtrend_fail(out_error, RBT_RESULT_STRUCT_WRONG_TYPE, "fit %s", rbt_struct_check_message(RBT_RESULT_STRUCT_WRONG_TYPE));
	const RBTREND_LIT_CASCADE_ANCHOR *anchor = nullptr;
	for (const RBT_STRUCT_BASE *link = fit->base.next; link; link = link->next) {
		if (link->type != RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR)
			return rbtrend_fail(out_error, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN,
			                    "fit has a struct chained on other than RBTREND_LIT_CASCADE_ANCHOR, the one it takes");
		if (anchor)
			return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "fit has RBTREND_LIT_CASCADE_ANCHOR chained on twice");
		anchor = (const RBTREND_LIT_CASCADE_ANCHOR *)link;
	}
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "out_maps", &out_maps->base, RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS))
		return base;
	const RBTREND_LIT_CASCADE_FIT &f = *fit;
	if (!all_finite(f.eye_position, 3))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "eye_position is not finite");
	if (!all_finite(f.view_direction, 3))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view_direction is not finite");
	if (!all_finite(f.view_up, 3))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view_up is not finite");
	if (!finite(f.fov_y))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "fov_y is not finite");
	if (!finite(f.half_height))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "half_height is not finite");
	if (!finite(f.aspect))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "aspect is not finite");
	if (!finite(f.near_depth))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "near_depth is not finite");
	if (!finite(f.far_depth))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "far_depth is not finite");
	if (!finite(f.split_lambda))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "split_lambda is not finite");
	if (!all_finite(f.light_direction, 3))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "light_direction is not finite");
	if (!finite(f.caster_distance))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "caster_distance is not finite");
	const glm::vec3 forward_in = vec3_of(f.view_direction), up_in = vec3_of(f.view_up), sun_in = vec3_of(f.light_direction);
	if (glm::length(forward_in) == 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view_direction is 0");
	const glm::vec3 forward = glm::normalize(forward_in);
	glm::vec3 right         = glm::cross(forward, up_in);
	if (glm::length(right) <= 1e-6f * std::max(glm::length(up_in), 1e-30f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "view_up runs along the view direction");
	right              = glm::normalize(right);
	const glm::vec3 up = glm::cross(right, forward);
	if (!f.orthographic && !(f.fov_y > 0.0f && f.fov_y < 3.1f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a perspective view's fov_y is not between 0 and 3.1");
	if (f.orthographic && !(f.half_height > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a parallel view's half_height is not above 0");
	if (!(f.aspect > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "aspect is not above 0");
	if (!f.orthographic && !(f.near_depth > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "a perspective view's near_depth is not above 0");
	if (!(f.far_depth > f.near_depth))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "far_depth is not beyond near_depth");
	if (f.cascade_count == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "cascade_count is 0");
	if (f.cascade_count > RBTREND_LIT_MAX_CASCADES)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "cascade_count %u is above %u", f.cascade_count, RBTREND_LIT_MAX_CASCADES);
	if (f.split_lambda < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "split_lambda is below 0");
	if (f.split_lambda > 1.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "split_lambda is above 1");
	if (glm::length(sun_in) == 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "light_direction is 0");
	if (f.caster_distance < 0.0f)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "caster_distance is negative");
	if (f.map_size == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "map_size is 0");
	if (anchor) {
		if (!std::isfinite(anchor->scene_origin.x))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the anchor's scene_origin.x is not finite");
		if (!std::isfinite(anchor->scene_origin.y))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the anchor's scene_origin.y is not finite");
		if (!std::isfinite(anchor->scene_origin.z))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the anchor's scene_origin.z is not finite");
	}

	const glm::vec3 eye = vec3_of(f.eye_position);
	const glm::vec3 sun = glm::normalize(sun_in);
	const float n = f.near_depth, fa = f.far_depth;
	float start = n;
	// The split before the last: where the stretch the surface shader
	// blends over at the end of the last cascade began (0 before the first).
	float before = 0.0f;
	for (uint32_t c = 0; c < f.cascade_count; ++c) {
		// Zhang's practical split: a mix of even and logarithmic.
		const float t     = (float)(c + 1) / (float)f.cascade_count;
		const float even  = n + (fa - n) * t;
		const float ratio = n > 0.0f ? n * std::pow(fa / n, t) : even;
		const float end   = c + 1 == f.cascade_count ? fa : f.split_lambda * ratio + (1.0f - f.split_lambda) * even;
		// The stretch's eight corners, and the sphere round them: its
		// radius depends on the stretch's shape alone, so it doesn't change
		// as the view turns.
		// Each cascade after the first reaches back over the last tenth of
		// the one before, which the surface shader blends into it.
		const float reach     = c == 0 ? start : std::max(n, start - 0.1f * (start - before));
		glm::vec3 corners[8];
		const float depths[2] = {reach, end};
		for (int k = 0; k < 2; ++k) {
			const float d          = depths[k];
			const float hh         = f.orthographic ? f.half_height : d * std::tan(f.fov_y * 0.5f);
			const float hw         = hh * f.aspect;
			const glm::vec3 centre = eye + forward * d;
			for (int i = 0; i < 4; ++i)
				corners[k * 4 + i] = centre + right * ((i & 1) ? hw : -hw) + up * ((i & 2) ? hh : -hh);
		}
		glm::vec3 centre(0.0f);
		for (const glm::vec3 &corner : corners)
			centre += corner / 8.0f;
		float radius = 0.0f;
		for (const glm::vec3 &corner : corners)
			radius = std::max(radius, glm::length(corner - centre));
		radius = std::ceil(radius * 16.0f) / 16.0f;

		// Looking down the light's direction at the sphere, from far enough
		// out to catch what stands between it and the light.
		const glm::vec3 light_up = std::fabs(sun.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const float back         = radius + f.caster_distance;
		const glm::mat4 view     = glm::lookAtRH(centre + sun * back, centre, light_up);
		const float depth        = back + radius;
		glm::mat4 projection     = rbtrend_flip_clip_y(f.reversed_depth ? glm::orthoRH_ZO(-radius, radius, -radius, radius, depth, 0.0f)
		                                                               : glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, depth));
		// Moved by whole texels: where the scene's origin lands, rounded; or,
		// with an anchor, where the fixed space's origin lands (the scene
		// place -scene_origin), in double: rbtrend_anchored_offset().
		glm::mat4 matrix        = projection * view;
		const float half_texels = (float)f.map_size * 0.5f;
		if (anchor) {
			const glm::vec2 offset = rbtrend_anchored_offset(matrix, anchor->scene_origin, f.map_size);
			projection[3][0] += offset.x;
			projection[3][1] += offset.y;
		} else {
			const glm::vec4 origin = matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const glm::vec2 at     = glm::vec2(origin.x, origin.y) * half_texels;
			const glm::vec2 offset = (glm::round(at) - at) / half_texels;
			projection[3][0] += offset.x;
			projection[3][1] += offset.y;
		}
		matrix = projection * view;
		memcpy(out_maps->_view_projection[c], &matrix, sizeof(float) * 16);
		out_maps->_split_depths[c] = end;
		before                      = c == 0 ? 0.0f : start;
		start                       = end;
	}
	for (uint32_t c = f.cascade_count; c < 6; ++c)
		memset(out_maps->_view_projection[c], 0, sizeof(float) * 16);
	for (uint32_t c = f.cascade_count; c < RBTREND_LIT_MAX_CASCADES; ++c)
		out_maps->_split_depths[c] = 0.0f;
	out_maps->_view_count = f.cascade_count;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_point_shadow_view_projection(const RBTREND_LIT_SCENE_LIGHT *light, uint32_t face, float near_depth, bool reversed_depth,
                                                 float out_view_projection[16], RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!light)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "light is NULL");
	if (!out_view_projection)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_view_projection is NULL");
	if (light->_type != RBTREND_LIT_LIGHT_TYPE_POINT)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the light is not a point light");
	if (const RBT_RESULT valid = rbtrend_lit_light_valid(*light, 0, out_error))
		return valid;
	if (face >= 6)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "face %u is not below 6", face);
	if (!(near_depth > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "near_depth is not above 0");
	if (!(near_depth < light->_range))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "near_depth is not below the light's range");
	static const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	const glm::vec3 position = vec3_of(light->_position), axis = axes[face];
	// Any up that isn't the axis: what the matrix renders is what the
	// surface reads through it.
	const glm::vec3 up = face == 2 || face == 3 ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
	// A margin past 90 degrees, as a spot's has past its cone, for the taps
	// of a soft shadow near a face's edge.
	const float fov            = 2.0f * std::atan(1.05f);
	const glm::mat4 projection = rbtrend_flip_clip_y(reversed_depth ? glm::perspectiveRH_ZO(fov, 1.0f, light->_range, near_depth)
	                                                               : glm::perspectiveRH_ZO(fov, 1.0f, near_depth, light->_range));
	const glm::mat4 matrix     = projection * glm::lookAtRH(position, position + axis, up);
	memcpy(out_view_projection, &matrix, sizeof(float) * 16);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_lit_spot_shadow_view_projection(const RBTREND_LIT_SCENE_LIGHT *light, float near_depth, bool reversed_depth, float out_view_projection[16],
                                                RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!light)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "light is NULL");
	if (!out_view_projection)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_view_projection is NULL");
	if (light->_type != RBTREND_LIT_LIGHT_TYPE_SPOT)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the light is not a spot");
	if (const RBT_RESULT valid = rbtrend_lit_light_valid(*light, 0, out_error))
		return valid;
	if (!(near_depth > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "near_depth is not above 0");
	if (!(near_depth < light->_range))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "near_depth is not below the light's range");
	const glm::vec3 position = vec3_of(light->_position), axis = glm::normalize(vec3_of(light->_direction));
	const glm::vec3 up       = std::fabs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	// Wide enough for the cone, with a margin the softness reads into.
	const float fov            = std::min(light->_outer_angle * 2.0f * 1.05f, 3.1f);
	const glm::mat4 projection = rbtrend_flip_clip_y(reversed_depth ? glm::perspectiveRH_ZO(fov, 1.0f, light->_range, near_depth)
	                                                               : glm::perspectiveRH_ZO(fov, 1.0f, near_depth, light->_range));
	const glm::mat4 matrix     = projection * glm::lookAtRH(position, position + axis, up);
	memcpy(out_view_projection, &matrix, sizeof(float) * 16);
	return RBT_RESULT_OK;
}

} // extern "C"

// ---- Exposures ----------------------------------------------------------------------

rbtrend_exposure_t::~rbtrend_exposure_t() {
	rbtrend_binding_set_destroy(this->histogram_set);
	rbtrend_buffer_destroy(this->histogram);
	for (int i = 0; i < 2; ++i) {
		rbtrend_texture_view_destroy(this->value_views[i]);
		rbtrend_texture_view_destroy(this->value_targets[i]);
		rbtrend_texture2d_destroy(this->values[i]);
	}
	rbtrend_texture_view_destroy(this->luminance_view);
	rbtrend_texture_view_destroy(this->luminance_target);
	rbtrend_texture2d_destroy(this->luminance);
}

extern "C" {

RBT_RESULT rbtrend_exposure_create(
    rbtrend_instance instance,
    const RBTREND_EXPOSURE_DESC *desc,
    rbtrend_exposure *out_exposure,
    RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_exposure)
		*out_exposure = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_exposure_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_exposure_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_EXPOSURE_DESC))
		return base;
	const RBTREND_EXPOSURE_DESC &d = *desc;
	if (!finite(d.key) || !finite(d.min_log) || !finite(d.max_log) || !finite(d.up_rate) || !finite(d.down_rate) || !(d.key > 0.0f) ||
	    !(d.min_log < d.max_log) || !(d.up_rate > 0.0f) || !(d.down_rate > 0.0f)) {
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "the key and rates are above 0, and min_log below max_log, all finite");
	}
	if (!(d.low_share >= 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "low_share is negative or not a number");
	if (!(d.high_share <= 1.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "high_share is above 1 or not a number");
	if (!(d.low_share < d.high_share))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "low_share %g is not below high_share %g", (double)d.low_share, (double)d.high_share);
	// Every texel measured is the log average, exactly, through the
	// luminance texture's mips; leaving some out takes a histogram.
	const bool histogram = d.low_share > 0.0f || d.high_share < 1.0f;
	if (!out_exposure)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_exposure is NULL");
	if (histogram && !rbtrend_lit_histogram_pipeline(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's exposure histogram pipeline can't be made");
	RBTREND_MALLOC__T(exposure, rbtrend_exposure_t);
	if (!exposure)
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the exposure");
	RBTREND_CONSTRUCT__T(exposure, rbtrend_exposure_t);
	exposure->instance         = instance;
	exposure->desc             = d;
	exposure->luminance        = rbtrend_lit_work_texture(instance, RBTREND_EXPOSURE_LUMINANCE_SIZE, log2_of(RBTREND_EXPOSURE_LUMINANCE_SIZE) + 1, 1,
	                                                     RBTREND_EXPOSURE_LUMINANCE_FORMAT);
	exposure->luminance_target = exposure->luminance ? rbtrend_lit_target_view(exposure->luminance, 0, 0) : nullptr;
	exposure->luminance_view   = exposure->luminance ? rbtrend_lit_whole_view(exposure->luminance, false) : nullptr;
	bool ok                    = exposure->luminance_target && exposure->luminance_view;
	for (int i = 0; ok && i < 2; ++i) {
		exposure->values[i]        = rbtrend_lit_work_texture(instance, 1, 1, 1, RBTREND_EXPOSURE_FORMAT);
		exposure->value_targets[i] = exposure->values[i] ? rbtrend_lit_target_view(exposure->values[i], 0, 0) : nullptr;
		exposure->value_views[i]   = exposure->values[i] ? rbtrend_lit_whole_view(exposure->values[i], false) : nullptr;
		ok                         = exposure->value_targets[i] && exposure->value_views[i];
	}
	if (ok && histogram) {
		if (rbtrend_lit_storage_buffer(instance, RBTREND_EXPOSURE_BINS * sizeof(uint32_t), true, "rbtrend exposure histogram", &exposure->histogram, nullptr) == RBT_RESULT_OK) {
			// The luminance and the bins: the histogram loads texels, with
			// no sampler, so its layout (made from its shaders) has none.
			RBTREND_BINDING_SET_ENTRY entries[2]{};
			entries[0]._binding                   = 0;
			entries[0]._type                      = CRGPU_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			entries[0]._resource._image._view     = exposure->luminance_view;
			entries[1]._binding                   = 2;
			entries[1]._type                      = CRGPU_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			entries[1]._resource._storage._buffer = exposure->histogram;
			(void)rbtrend_binding_set_create(instance->builtins.lit.histogram_layout, entries, 2, &exposure->histogram_set, nullptr);
		}
		ok = exposure->histogram_set != nullptr;
	}
	if (!ok) {
		RBTREND_DESTRUCT__T(exposure, rbtrend_exposure_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "a texture, view, buffer or set of the exposure can't be created");
	}
	*out_exposure = exposure;
	return RBT_RESULT_OK;
}

void rbtrend_exposure_destroy(rbtrend_exposure exposure) {
	if (!exposure)
		return;
	if (RBT_STALE(exposure, "rbtrend_exposure_destroy", "exposure"))
		return;
	RBT_RETIRE(exposure->instance, exposure, rbtrend_exposure_t);
}

} // extern "C"

// ---- A scene's atmosphere (rbtrendatmosphere.h) -----------------------------------------

// A scene's atmosphere array (rbtrend_lit_scene_t::atmosphere_tables):
// [layers] layers of [width] by [height], rendered to a layer at a time,
// read whole as an array and layer 0 alone. False, with everything made
// destroyed, if any part can't be made.
static bool rbtrend_lit_atmosphere_array(rbtrend_instance instance, const uint32_t size[3], rbtrend_texture2d *out_texture,
                                        std::vector<rbtrend_texture_view> *out_targets, rbtrend_texture_view *out_array, rbtrend_texture_view *out_first) {
	RBTREND_TEXTURE2D_DESC desc{};
	desc.base         = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC);
	desc.width        = size[0];
	desc.height       = size[1];
	desc.format       = RBTREND_ATMOSPHERE_FORMAT;
	desc.usage        = RBTREND_TEXTURE_USAGE_BIT_RENDER_TARGET | RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE;
	desc.mip_levels   = 1;
	desc.array_layers = size[2];
	desc.sample_count = 1;
	rbtrend_texture2d texture = nullptr;
	(void)rbtrend_texture2d_create(instance, &desc, &texture, nullptr);
	if (!texture)
		return false;
	std::vector<rbtrend_texture_view> targets;
	try {
		targets.resize(size[2], nullptr);
	} catch (...) {
		rbtrend_texture2d_destroy(texture);
		return false;
	}
	bool ok = true;
	for (uint32_t layer = 0; ok && layer < size[2]; ++layer) {
		targets[layer] = rbtrend_lit_target_view(texture, 0, layer);
		ok             = targets[layer] != nullptr;
	}
	rbtrend_texture_view array = nullptr, first = nullptr;
	if (ok) {
		RBTREND_TEXTURE_VIEW_DESC vd{};
		vd.base        = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
		vd.type        = RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE;
		vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_2D_ARRAY;
		vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
		vd.mip_count   = 1;
		vd.layer_count = size[2];
		(void)rbtrend_texture_view_create_2d(texture, &vd, &array, nullptr);
		vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_2D;
		vd.layer_count = 1;
		(void)rbtrend_texture_view_create_2d(texture, &vd, &first, nullptr);
		ok = array && first;
	}
	if (!ok) {
		rbtrend_texture_view_destroy(array);
		rbtrend_texture_view_destroy(first);
		for (rbtrend_texture_view target : targets)
			rbtrend_texture_view_destroy(target);
		rbtrend_texture2d_destroy(texture);
		return false;
	}
	*out_texture = texture;
	*out_array   = array;
	*out_first   = first;
	out_targets->swap(targets);
	return true;
}

extern "C" {

RBT_RESULT rbtrend_lit_scene_set_atmosphere(rbtrend_lit_scene scene, const RBTREND_LIT_ATMOSPHERE *atmosphere, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!scene)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is NULL");
	if (RBT_STALE(scene, "rbtrend_lit_scene_set_atmosphere", "scene"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "scene is destroyed");
	RBTREND_ENTER(scene->instance, "rbtrend_lit_scene_set_atmosphere");
	if (!atmosphere)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "atmosphere", &atmosphere->base, RBT_STRUCT_TYPE_REND_LIT_ATMOSPHERE))
		return base;
	const RBTREND_LIT_ATMOSPHERE &a = *atmosphere;
	rbtrend_lit_builtins &lit       = scene->instance->builtins.lit;
	if (!a.atmosphere) {
		// None: the bindings back to their placeholders; the scene's own
		// tables kept for the next atmosphere of their sizes.
		scene->atmosphere            = RBTREND_LIT_ATMOSPHERE{};
		scene->sky_view_filled       = false;
		scene->aerial_filled         = false;
		scene->transmittance_copied = false;
		(void)rbtrend_material_set_texture(scene->material, "atmosphere_tables", 0, lit.placeholder_2d_array_view, nullptr);
		rbtrend_lit_scene_commit(scene);
		return RBT_RESULT_OK;
	}
	if (RBT_STALE(a.atmosphere, "rbtrend_lit_scene_set_atmosphere", "atmosphere->atmosphere"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->atmosphere is destroyed");
	if (a.atmosphere->instance != scene->instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->atmosphere is of another instance");
	for (int i = 0; i < 3; ++i) {
		if (!finite(a.center[i]))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->center[%d] is not finite", i);
	}
	if (a.sun_count < 1)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sun_count is 0");
	if (a.sun_count > 2)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sun_count %u is above 2", a.sun_count);
	// Both sky-view sizes 0: no sky (rbtrendlit.h, RBTREND_LIT_ATMOSPHERE).
	if (a.sky_view_width == 0 && a.sky_view_height != 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_width is 0 and sky_view_height %u is not (both 0 for no sky)",
		                   a.sky_view_height);
	if (a.sky_view_height == 0 && a.sky_view_width != 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_height is 0 and sky_view_width %u is not (both 0 for no sky)",
		                   a.sky_view_width);
	const bool sky = a.sky_view_width != 0;
	if (sky && a.sky_view_width < RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_width %u is below %u", a.sky_view_width,
		                   RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE);
	if (a.sky_view_width > RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_width %u is above %u", a.sky_view_width,
		                   RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE);
	if (sky && a.sky_view_height < RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_height %u is below %u", a.sky_view_height,
		                   RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE);
	if (a.sky_view_height > RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_height %u is above %u", a.sky_view_height,
		                   RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE);
	const bool volume = a.aerial_width != 0 || a.aerial_height != 0 || a.aerial_depth != 0;
	if (volume && !sky)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere has an aerial volume and no sky-view (its sky-view sizes 0)");
	if (volume) {
		const uint32_t sides[3]    = {a.aerial_width, a.aerial_height, a.aerial_depth};
		const char *const names[3] = {"aerial_width", "aerial_height", "aerial_depth"};
		for (int i = 0; i < 3; ++i) {
			if (sides[i] < RBTREND_LIT_ATMOSPHERE_MIN_AERIAL_SIZE)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->%s %u is below %u (or all three 0 for no volume)", names[i], sides[i],
				                   RBTREND_LIT_ATMOSPHERE_MIN_AERIAL_SIZE);
			if (sides[i] > RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_SIZE)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->%s %u is above %u", names[i], sides[i],
				                   RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_SIZE);
		}
		if (!finite(a.aerial_distance))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->aerial_distance is not finite");
		if (!(a.aerial_distance > 0.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->aerial_distance is not above 0");
	}
	if (a.sky_view_min_steps < 1)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_min_steps is 0");
	if (a.sky_view_min_steps > RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_min_steps %u is above %u", a.sky_view_min_steps,
		                   RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS);
	if (a.sky_view_max_steps < a.sky_view_min_steps)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_max_steps %u is below sky_view_min_steps %u", a.sky_view_max_steps,
		                   a.sky_view_min_steps);
	if (a.sky_view_max_steps > RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->sky_view_max_steps %u is above %u", a.sky_view_max_steps,
		                   RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS);
	if (volume && a.aerial_steps_per_slice < 1)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->aerial_steps_per_slice is 0");
	if (volume && a.aerial_steps_per_slice > RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere->aerial_steps_per_slice %u is above %u", a.aerial_steps_per_slice,
		                   RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_STEPS);

	// The scene's array, made where its size differs from the one held,
	// and its pass material, before anything of the scene changes. An
	// atmosphere with no sky reads only the transmittance table at layer 1,
	// so it keeps any array of two layers or more that holds that table: a
	// caller switching one between a sky and none makes nothing.
	uint32_t size[3];
	rbtrend_lit_atmosphere_array_size(a, size);
	const uint32_t *held = scene->atmosphere_array_size;
	const bool holds = scene->atmosphere_tables && held[0] >= size[0] && held[1] >= size[1] && held[2] >= size[2];
	const bool new_array = !scene->atmosphere_tables || (sky ? memcmp(size, held, sizeof(size)) != 0 : !holds);
	rbtrend_texture2d tables           = nullptr;
	rbtrend_texture_view tables_view   = nullptr;
	rbtrend_texture_view first_view    = nullptr;
	std::vector<rbtrend_texture_view> targets;
	rbtrend_material material = scene->atmosphere_material;
	bool ok                   = true;
	if (!material) {
		ok = rbtrend_atmosphere_prepare(scene->instance);
		if (ok) {
			RBTREND_MATERIAL_DESC md{};
			md.base    = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
			md._layout = lit.atmosphere_layout;
			(void)rbtrend_material_create(scene->instance, &md, &material, nullptr);
			ok = material != nullptr;
		}
	}
	if (ok && new_array)
		ok = rbtrend_lit_atmosphere_array(scene->instance, size, &tables, &targets, &tables_view, &first_view);
	if (!ok) {
		if (material != scene->atmosphere_material)
			rbtrend_material_destroy(material);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "the scene's atmosphere tables can't be made");
	}

	// From here nothing fails: the new array in, the old one out.
	scene->atmosphere_material = material;
	if (tables) {
		for (rbtrend_texture_view view : scene->atmosphere_targets)
			rbtrend_texture_view_destroy(view);
		rbtrend_texture_view_destroy(scene->atmosphere_tables_view);
		rbtrend_texture_view_destroy(scene->sky_view_view);
		rbtrend_texture2d_destroy(scene->atmosphere_tables);
		scene->atmosphere_tables = tables;
		scene->atmosphere_targets.swap(targets);
		scene->atmosphere_tables_view = tables_view;
		scene->sky_view_view          = first_view;
		memcpy(scene->atmosphere_array_size, size, sizeof(size));
		scene->sky_view_filled       = false;
		scene->aerial_filled         = false;
		scene->transmittance_copied = false;
	}
	if (scene->atmosphere.atmosphere != a.atmosphere) {
		scene->sky_view_filled       = false;
		scene->aerial_filled         = false;
		scene->transmittance_copied = false;
	}
	// A sky-view the scene filled before it had no sky, or one it holds
	// from a sky it no longer draws, is not this sky.
	if ((scene->atmosphere.sky_view_width != 0) != sky)
		scene->sky_view_filled = false;
	scene->atmosphere = a;
	(void)rbtrend_material_set_texture(scene->material, "atmosphere_tables", 0, scene->atmosphere_tables_view, nullptr);
	rbtrend_lit_scene_commit(scene);
	return RBT_RESULT_OK;
}

} // extern "C"

