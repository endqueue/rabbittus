#include "rbtrendatmosphere_internal.hpp"

#include "rbtrendbuiltin_internal.hpp"
#include "rbtrendimages_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

// The packages of shaders/, embedded by the build (cmake/rbtrend_slang.cmake).
#include "rbtrend_atmosphere_transmittance_frag.package.inc"
#include "rbtrend_atmosphere_multiscatter_frag.package.inc"
#include "rbtrend_atmosphere_skyview_frag.package.inc"
#include "rbtrend_atmosphere_environment_frag.package.inc"
#include "rbtrend_atmosphere_copy_frag.package.inc"
#include "rbtrend_atmosphere_aerial_frag.package.inc"

#define MEMBER(T, field) (uint32_t) offsetof(T, field)
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

// ---- The passes ------------------------------------------------------------------------

static const struct {
	const RBTREND_SHADER_PACKAGE *package;
	const char *name;
} s_atmosphere_passes[RBTREND_ATMOSPHERE_PASS_COUNT] = {
    {&rbtrend_atmosphere_transmittance_frag_package, "atmosphere transmittance"},
    {&rbtrend_atmosphere_multiscatter_frag_package, "atmosphere multiple scattering"},
    {&rbtrend_atmosphere_skyview_frag_package, "atmosphere sky-view"},
    {&rbtrend_atmosphere_environment_frag_package, "atmosphere environment"},
    {&rbtrend_atmosphere_copy_frag_package, "atmosphere table copy"},
    {&rbtrend_atmosphere_aerial_frag_package, "atmosphere aerial volume"},
};

static bool rbtrend_atmosphere_check_block(rbtrend_binding_layout layout) {
	typedef rbtrend_atmosphere_pass_block B;
	const rbtrend_block_member members[] = {
	    {"radii", MEMBER(B, radii)},
	    {"rayleigh", MEMBER(B, rayleigh)},
	    {"mie_scattering", MEMBER(B, mie_scattering)},
	    {"mie_extinction", MEMBER(B, mie_extinction)},
	    {"absorption", MEMBER(B, absorption)},
	    {"ground_albedo", MEMBER(B, ground_albedo)},
	    {"steps", MEMBER(B, steps)},
	    {"sizes", MEMBER(B, sizes)},
	    {"more_sizes", MEMBER(B, more_sizes)},
	    {"frame_up", MEMBER(B, frame_up)},
	    {"frame_x", MEMBER(B, frame_x)},
	    {"frame_y", MEMBER(B, frame_y)},
	    {"sun_direction", MEMBER(B, sun_direction)},
	    {"sun_illuminance", MEMBER(B, sun_illuminance)},
	    {"environment_rotation", MEMBER(B, environment_rotation)},
	    {"array_sizes", MEMBER(B, array_sizes)},
	    {"inverse_view_projection", MEMBER(B, inverse_view_projection)},
	    {"view_direction", MEMBER(B, view_direction)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendAtmospherePass", (uint32_t)sizeof(B), members, COUNT_OF(members));
}

bool rbtrend_atmosphere_prepare(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (!rbtrend_lit_prepare_resources(instance))
		return false;
	for (uint32_t pass = 0; pass < RBTREND_ATMOSPHERE_PASS_COUNT; ++pass) {
		if (lit.atmosphere_frags[pass])
			continue;
		(void)rbtrend_shader_create(instance, s_atmosphere_passes[pass].package, &lit.atmosphere_frags[pass], nullptr);
		if (!lit.atmosphere_frags[pass]) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's %s shader can't be created", s_atmosphere_passes[pass].name);
			return false;
		}
	}
	if (!lit.atmosphere_layout) {
		// Set 0 is the block, alike in every pass that reads it (all but
		// the copy, whose set 0 is a view's).
		(void)rbtrend_binding_layout_create_from_shaders(instance, lit.atmosphere_frags, RBTREND_ATMOSPHERE_PASS_COPY, 0, &lit.atmosphere_layout, nullptr);
		if (!lit.atmosphere_layout || !rbtrend_atmosphere_check_block(lit.atmosphere_layout)) {
			rbtrend_binding_layout_destroy(lit.atmosphere_layout);
			lit.atmosphere_layout = nullptr;
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's atmosphere passes can't be created");
			return false;
		}
	}
	return rbtrend_lit_prepare_passes_for(instance);
}

rbtrend_graphics_pipeline rbtrend_atmosphere_pipeline(rbtrend_instance instance, uint32_t pass) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.atmosphere_pipelines[pass])
		return lit.atmosphere_pipelines[pass];
	if (!rbtrend_atmosphere_prepare(instance))
		return nullptr;
	// Set 0 the block; then a table each, as a view's environment set (a
	// texture and a sampler): none for the transmittance, the transmittance
	// for the multiple scattering, both for the sky-view, the sky-view for
	// the environment (whose face and size are push constants), both for an
	// aerial slice (whose slice and size are push constants).
	rbtrend_binding_layout layouts[3] = {lit.atmosphere_layout, lit.pass_layout, lit.pass_layout};
	uint32_t count                   = 1;
	uint32_t push                    = 0;
	if (pass == RBTREND_ATMOSPHERE_PASS_MULTISCATTER || pass == RBTREND_ATMOSPHERE_PASS_ENVIRONMENT)
		count = 2;
	else if (pass == RBTREND_ATMOSPHERE_PASS_SKY_VIEW || pass == RBTREND_ATMOSPHERE_PASS_AERIAL)
		count = 3;
	if (pass == RBTREND_ATMOSPHERE_PASS_ENVIRONMENT)
		push = (uint32_t)sizeof(rbtrend_environment_params);
	if (pass == RBTREND_ATMOSPHERE_PASS_AERIAL)
		push = (uint32_t)sizeof(rbtrend_aerial_params);
	if (pass == RBTREND_ATMOSPHERE_PASS_COPY)
		layouts[0] = lit.pass_layout;
	lit.atmosphere_pipelines[pass] = rbtrend_lit_fullscreen_pipeline_for(instance, lit.atmosphere_frags[pass], layouts, count, RBTREND_ATMOSPHERE_FORMAT,
	                                                                     push, s_atmosphere_passes[pass].name);
	return lit.atmosphere_pipelines[pass];
}

void rbtrend_atmosphere_write_block(rbtrend_material material, const rbtrend_atmosphere_pass_block &block) {
	for (uint32_t i = 0; i < material->slot_count; ++i) {
		if (material->slots[i].bytes) {
			memcpy(material->slots[i].bytes, &block, sizeof(block));
			material->slots[i].dirty = true;
		}
	}
}

void rbtrend_atmosphere_fill_block(const rbtrend_atmosphere_t &a, rbtrend_atmosphere_pass_block &b) {
	const RBTMATH_SHELL_MEDIUM &m = a.medium;
	b.radii[0]                    = m.bottom_radius;
	b.radii[1]                    = m.top_radius;
	b.radii[2]                    = m.rayleigh_scale_height;
	b.radii[3]                    = m.mie_scale_height;
	for (int c = 0; c < 3; ++c) {
		b.rayleigh[c]       = m.rayleigh_scattering[c];
		b.mie_scattering[c] = m.mie_scattering[c];
		b.mie_extinction[c] = m.mie_extinction[c];
		b.absorption[c]     = m.absorption_extinction[c];
		b.ground_albedo[c]  = m.ground_albedo[c];
	}
	b.rayleigh[3]       = m.mie_anisotropy;
	b.mie_scattering[3] = m.absorption_center_height;
	b.mie_extinction[3] = m.absorption_half_width;
	b.absorption[3]     = 0.0f;
	b.ground_albedo[3]  = 0.0f;
	b.steps[0]          = a.desc.transmittance_steps;
	b.steps[1]          = a.desc.multiple_scattering_steps;
	b.sizes[0]          = (float)a.desc.transmittance_width;
	b.sizes[1]          = (float)a.desc.transmittance_height;
	b.more_sizes[0]     = (float)a.desc.multiple_scattering_size;
}

bool rbtrend_atmosphere_drawable(const rbtrend_atmosphere_t &atmosphere) {
	return atmosphere.has_medium && atmosphere.built;
}

// ---- A scene's atmosphere --------------------------------------------------------------

namespace {

void sub3(const float a[3], const float b[3], float out[3]) {
	for (int i = 0; i < 3; ++i)
		out[i] = a[i] - b[i];
}

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void cross3(const float a[3], const float b[3], float out[3]) {
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

bool normalize3(float v[3]) {
	const float l = std::sqrt(dot3(v, v));
	if (!(l > 0.0f))
		return false;
	for (int i = 0; i < 3; ++i)
		v[i] /= l;
	return true;
}

// The eye's frame round [scene]'s atmosphere: its up (away from the
// centre) and x (towards the first sun's bearing round the up, or any
// direction across the up where there is no bearing), y their cross
// product, as rbtrend_lit_atmosphere.slang has it; and the eye's distance.
void atmosphere_frame(rbtrend_lit_scene scene, float up[3], float x[3], float y[3], float *distance) {
	const rbtrend_lit_scene_block &b = scene->block;
	sub3(b.eye_position, scene->atmosphere.center, up);
	*distance = std::sqrt(dot3(up, up));
	if (!normalize3(up)) {
		up[0] = 0.0f;
		up[1] = 1.0f;
		up[2] = 0.0f;
	}
	bool found = false;
	if (b.directional_count > 0) {
		const float *sun = b.directional[0].direction_versine;
		const float along = dot3(sun, up);
		for (int i = 0; i < 3; ++i)
			x[i] = sun[i] - up[i] * along;
		found = std::sqrt(dot3(x, x)) > 1e-4f && normalize3(x);
	}
	if (!found) {
		const float other[3] = {std::fabs(up[1]) < 0.9f ? 0.0f : 1.0f, std::fabs(up[1]) < 0.9f ? 1.0f : 0.0f, 0.0f};
		float across[3];
		cross3(other, up, across);
		normalize3(across);
		memcpy(x, across, sizeof(across));
	}
	cross3(up, x, y);
}

} // namespace

void rbtrend_lit_atmosphere_array_size(const RBTREND_LIT_ATMOSPHERE &a, uint32_t out_size[3]) {
	uint32_t width  = a.sky_view_width;
	uint32_t height = a.sky_view_height;
	if (a.atmosphere) {
		width  = a.atmosphere->desc.transmittance_width > width ? a.atmosphere->desc.transmittance_width : width;
		height = a.atmosphere->desc.transmittance_height > height ? a.atmosphere->desc.transmittance_height : height;
	}
	width       = a.aerial_width > width ? a.aerial_width : width;
	height      = a.aerial_height > height ? a.aerial_height : height;
	out_size[0] = width;
	out_size[1] = height;
	out_size[2] = 2 + a.aerial_depth;
}

void rbtrend_lit_scene_resolve_atmosphere(rbtrend_lit_scene scene) {
	rbtrend_lit_scene_block &b = scene->block;
	b.flags &= ~(uint32_t)(RBTREND_LIT_SCENE_FLAG_ATMOSPHERE | RBTREND_LIT_SCENE_FLAG_AERIAL);
	rbtrend_atmosphere atmosphere = scene->atmosphere.atmosphere;
	if (!atmosphere || !rbtrend_atmosphere_drawable(*atmosphere) || !scene->sky_view_filled)
		return;
	float up[3], x[3], y[3], distance = 0.0f;
	atmosphere_frame(scene, up, x, y, &distance);
	for (int i = 0; i < 3; ++i) {
		b.atmosphere_center[i] = scene->atmosphere.center[i];
		b.atmosphere_up[i]     = up[i];
		b.atmosphere_x[i]      = x[i];
		b.atmosphere_y[i]      = y[i];
	}
	b.atmosphere_center[3] = atmosphere->medium.bottom_radius;
	b.atmosphere_up[3]     = atmosphere->medium.top_radius;
	b.atmosphere_x[3]      = distance;
	b.atmosphere_y[3]      = (float)scene->atmosphere.sun_count;
	b.atmosphere_sizes[0]  = (float)atmosphere->desc.transmittance_width;
	b.atmosphere_sizes[1]  = (float)atmosphere->desc.transmittance_height;
	b.atmosphere_sizes[2]  = (float)scene->atmosphere.sky_view_width;
	b.atmosphere_sizes[3]  = (float)scene->atmosphere.sky_view_height;
	b.atmosphere_aerial[0] = (float)scene->atmosphere.aerial_depth;
	b.atmosphere_aerial[1] = scene->atmosphere.aerial_distance;
	b.atmosphere_aerial[2] = (float)scene->atmosphere.aerial_width;
	b.atmosphere_aerial[3] = (float)scene->atmosphere.aerial_height;
	b.atmosphere_array[0]  = (float)scene->atmosphere_array_size[0];
	b.atmosphere_array[1]  = (float)scene->atmosphere_array_size[1];
	b.atmosphere_array[2]  = (float)scene->atmosphere_array_size[2];
	b.atmosphere_array[3]  = 0.0f;
	b.flags |= RBTREND_LIT_SCENE_FLAG_ATMOSPHERE;
	if (scene->atmosphere.aerial_depth > 0 && scene->aerial_filled)
		b.flags |= RBTREND_LIT_SCENE_FLAG_AERIAL;
}

void rbtrend_lit_scene_atmosphere_block(rbtrend_lit_scene scene, rbtrend_atmosphere_pass_block &out) {
	out                           = rbtrend_atmosphere_pass_block{};
	rbtrend_atmosphere atmosphere = scene->atmosphere.atmosphere;
	if (!atmosphere)
		return;
	rbtrend_atmosphere_fill_block(*atmosphere, out);
	const rbtrend_lit_scene_block &b = scene->block;
	float distance                   = 0.0f;
	atmosphere_frame(scene, out.frame_up, out.frame_x, out.frame_y, &distance);
	out.frame_up[3] = distance;
	out.steps[2]    = scene->atmosphere.sky_view_min_steps;
	out.steps[3]    = scene->atmosphere.sky_view_max_steps;
	out.sizes[2]    = (float)scene->atmosphere.sky_view_width;
	out.sizes[3]    = (float)scene->atmosphere.sky_view_height;
	out.more_sizes[1] = (float)scene->atmosphere.aerial_depth;
	out.more_sizes[2] = scene->atmosphere.aerial_distance;
	out.more_sizes[3] = (float)scene->atmosphere.aerial_steps_per_slice;
	const uint32_t suns = scene->atmosphere.sun_count < b.directional_count ? scene->atmosphere.sun_count : b.directional_count;
	for (uint32_t k = 0; k < suns && k < 2; ++k) {
		for (int i = 0; i < 3; ++i) {
			out.sun_direction[k][i]   = b.directional[k].direction_versine[i];
			out.sun_illuminance[k][i] = 3.14159265358979f * b.directional[k].radiance_disc[i];
		}
		out.sun_direction[k][3] = 1.0f;
	}
	memcpy(out.environment_rotation, b.environment_rotation, sizeof(out.environment_rotation));
	out.array_sizes[0] = (float)scene->atmosphere_array_size[0];
	out.array_sizes[1] = (float)scene->atmosphere_array_size[1];
	out.array_sizes[2] = (float)scene->atmosphere_array_size[2];
	memcpy(out.inverse_view_projection, b.inverse_view_projection, sizeof(out.inverse_view_projection));
	memcpy(out.view_direction, b.view_direction, sizeof(out.view_direction));
}

// ---- The handle ------------------------------------------------------------------------

rbtrend_atmosphere_t::~rbtrend_atmosphere_t() {
	rbtrend_material_destroy(this->material);
	rbtrend_texture_view_destroy(this->transmittance_target);
	rbtrend_texture_view_destroy(this->transmittance_view);
	rbtrend_texture2d_destroy(this->transmittance);
	rbtrend_texture_view_destroy(this->multiple_scattering_target);
	rbtrend_texture_view_destroy(this->multiple_scattering_view);
	rbtrend_texture2d_destroy(this->multiple_scattering);
	// The desc's name is the copy made at creation; the handle's may have
	// been set by the caller since (RBT_SET_DEBUG_NAME), and isn't ours.
	free((char *)this->desc.debug_name);
}

// A table: rendered to and read, one mip, one layer.
static rbtrend_texture2d rbtrend_atmosphere_table(rbtrend_instance instance, uint32_t width, uint32_t height) {
	RBTREND_TEXTURE2D_DESC desc{};
	desc.base         = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC);
	desc.width        = width;
	desc.height       = height;
	desc.format       = RBTREND_ATMOSPHERE_FORMAT;
	desc.usage        = RBTREND_TEXTURE_USAGE_BIT_RENDER_TARGET | RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE;
	desc.mip_levels   = 1;
	desc.array_layers = 1;
	desc.sample_count = 1;
	rbtrend_texture2d made = nullptr;
	(void)rbtrend_texture2d_create(instance, &desc, &made, nullptr);
	return made;
}

static rbtrend_texture_view rbtrend_atmosphere_view(rbtrend_texture2d texture, RBTREND_TEXTURE_VIEW_TYPE type) {
	RBTREND_TEXTURE_VIEW_DESC vd{};
	vd.base        = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
	vd.type        = type;
	vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_2D;
	vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
	vd.mip_count   = 1;
	vd.layer_count = 1;
	rbtrend_texture_view made = nullptr;
	(void)rbtrend_texture_view_create_2d(texture, &vd, &made, nullptr);
	return made;
}

extern "C" {

RBT_RESULT rbtrend_atmosphere_create(
    rbtrend_instance instance,
    const RBTREND_ATMOSPHERE_DESC *desc,
    rbtrend_atmosphere *out_atmosphere,
    RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_atmosphere)
		*out_atmosphere = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_atmosphere_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_atmosphere_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_ATMOSPHERE_DESC))
		return base;
	if (desc->transmittance_width < RBTREND_ATMOSPHERE_MIN_TRANSMITTANCE_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transmittance_width %u is below %u", desc->transmittance_width,
		                   RBTREND_ATMOSPHERE_MIN_TRANSMITTANCE_SIZE);
	if (desc->transmittance_width > RBTREND_ATMOSPHERE_MAX_TRANSMITTANCE_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transmittance_width %u is above %u", desc->transmittance_width,
		                   RBTREND_ATMOSPHERE_MAX_TRANSMITTANCE_SIZE);
	if (desc->transmittance_height < RBTREND_ATMOSPHERE_MIN_TRANSMITTANCE_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transmittance_height %u is below %u", desc->transmittance_height,
		                   RBTREND_ATMOSPHERE_MIN_TRANSMITTANCE_SIZE);
	if (desc->transmittance_height > RBTREND_ATMOSPHERE_MAX_TRANSMITTANCE_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transmittance_height %u is above %u", desc->transmittance_height,
		                   RBTREND_ATMOSPHERE_MAX_TRANSMITTANCE_SIZE);
	if (desc->multiple_scattering_size < RBTREND_ATMOSPHERE_MIN_MULTIPLE_SCATTERING_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "multiple_scattering_size %u is below %u", desc->multiple_scattering_size,
		                   RBTREND_ATMOSPHERE_MIN_MULTIPLE_SCATTERING_SIZE);
	if (desc->multiple_scattering_size > RBTREND_ATMOSPHERE_MAX_MULTIPLE_SCATTERING_SIZE)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "multiple_scattering_size %u is above %u", desc->multiple_scattering_size,
		                   RBTREND_ATMOSPHERE_MAX_MULTIPLE_SCATTERING_SIZE);
	if (desc->transmittance_steps < RBTREND_ATMOSPHERE_MIN_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transmittance_steps %u is below %u", desc->transmittance_steps, RBTREND_ATMOSPHERE_MIN_STEPS);
	if (desc->transmittance_steps > RBTREND_ATMOSPHERE_MAX_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "transmittance_steps %u is above %u", desc->transmittance_steps, RBTREND_ATMOSPHERE_MAX_STEPS);
	if (desc->multiple_scattering_steps < RBTREND_ATMOSPHERE_MIN_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "multiple_scattering_steps %u is below %u", desc->multiple_scattering_steps,
		                   RBTREND_ATMOSPHERE_MIN_STEPS);
	if (desc->multiple_scattering_steps > RBTREND_ATMOSPHERE_MAX_STEPS)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "multiple_scattering_steps %u is above %u", desc->multiple_scattering_steps,
		                   RBTREND_ATMOSPHERE_MAX_STEPS);
	if (!out_atmosphere)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_atmosphere is NULL");
	if (!rbtrend_atmosphere_prepare(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's atmosphere passes can't be made");

	char *name = nullptr;
	if (desc->debug_name) {
		const size_t length = strlen(desc->debug_name) + 1;
		name                = (char *)malloc(length);
		if (!name)
			return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the atmosphere's name");
		memcpy(name, desc->debug_name, length);
	}
	RBTREND_MALLOC__T(atmosphere, rbtrend_atmosphere_t);
	if (!atmosphere) {
		free(name);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the atmosphere");
	}
	RBTREND_CONSTRUCT__T(atmosphere, rbtrend_atmosphere_t);
	atmosphere->instance        = instance;
	atmosphere->desc            = *desc;
	atmosphere->desc.debug_name = name;
	atmosphere->debug_name      = name;
	atmosphere->transmittance       = rbtrend_atmosphere_table(instance, desc->transmittance_width, desc->transmittance_height);
	atmosphere->multiple_scattering = rbtrend_atmosphere_table(instance, desc->multiple_scattering_size, desc->multiple_scattering_size);
	bool ok                         = atmosphere->transmittance && atmosphere->multiple_scattering;
	if (ok) {
		atmosphere->transmittance_target       = rbtrend_atmosphere_view(atmosphere->transmittance, RBTREND_TEXTURE_VIEW_TYPE_RENDER_TARGET);
		atmosphere->transmittance_view         = rbtrend_atmosphere_view(atmosphere->transmittance, RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE);
		atmosphere->multiple_scattering_target = rbtrend_atmosphere_view(atmosphere->multiple_scattering, RBTREND_TEXTURE_VIEW_TYPE_RENDER_TARGET);
		atmosphere->multiple_scattering_view   = rbtrend_atmosphere_view(atmosphere->multiple_scattering, RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE);
		ok = atmosphere->transmittance_target && atmosphere->transmittance_view && atmosphere->multiple_scattering_target &&
		     atmosphere->multiple_scattering_view;
	}
	if (ok) {
		RBTREND_MATERIAL_DESC md{};
		md.base    = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
		md._layout = instance->builtins.lit.atmosphere_layout;
		(void)rbtrend_material_create(instance, &md, &atmosphere->material, nullptr);
		ok = atmosphere->material != nullptr;
	}
	if (!ok) {
		RBTREND_DESTRUCT__T(atmosphere, rbtrend_atmosphere_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "a table of the atmosphere can't be created");
	}
	*out_atmosphere = atmosphere;
	return RBT_RESULT_OK;
}

void rbtrend_atmosphere_destroy(rbtrend_atmosphere atmosphere) {
	if (!atmosphere)
		return;
	if (RBT_STALE(atmosphere, "rbtrend_atmosphere_destroy", "atmosphere"))
		return;
	// Its textures and material go through the release queue, each on its
	// own; the rest is the CPU's.
	RBT_RETIRE(atmosphere->instance, atmosphere, rbtrend_atmosphere_t);
}

RBT_RESULT rbtrend_atmosphere_set_medium(
    rbtrend_atmosphere atmosphere,
    const RBTMATH_SHELL_MEDIUM *medium,
    RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!atmosphere)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere is NULL");
	if (RBT_STALE(atmosphere, "rbtrend_atmosphere_set_medium", "atmosphere"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "atmosphere is destroyed");
	RBTREND_ENTER_ATMOSPHERE(atmosphere, "rbtrend_atmosphere_set_medium");
	if (!medium)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "medium is NULL");
	// Rabbittus Math's own checks, field by field in the struct's order:
	// asked for no phase, it checks the medium alone.
	RBTMATH_ERROR checked{};
	const RBT_RESULT valid = rbtmath_shell_medium_compute_phase(medium, nullptr, 0, nullptr, &checked);
	if (valid != RBT_RESULT_OK)
		return rbtrend_fail(out_error, valid, "%s", checked.message);
	atmosphere->medium     = *medium;
	atmosphere->has_medium = true;
	atmosphere->changed    = true;
	return RBT_RESULT_OK;
}

} // extern "C"
