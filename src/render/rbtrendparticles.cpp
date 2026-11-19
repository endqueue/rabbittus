#include "rbtrendparticles_internal.hpp"

#include "rbtrendimages_internal.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "rbtrend_particles_clear_comp.package.inc"
#include "rbtrend_particles_simulate_comp.package.inc"
#include "rbtrend_particles_spawn_comp.package.inc"
#include "rbtrend_particles_arguments_comp.package.inc"
#include "rbtrend_particles_sort_count_comp.package.inc"
#include "rbtrend_particles_sort_scan_comp.package.inc"
#include "rbtrend_particles_sort_scatter_comp.package.inc"
#include "rbtrend_particles_vert.package.inc"
#include "rbtrend_particles_lit_frag.package.inc"
#include "rbtrend_particles_unlit_frag.package.inc"
#include "rbtrend_particles_mesh_vert.package.inc"

// ---- The instance's particle objects ----------------------------------------------

namespace {

// Each shader by RBTREND_PARTICLES_SHADER_INDEX_, its entry point, and the
// push constants a step's pipeline of it takes (compute only).
struct rbtrend_particles_shader_entry {
	const RBTREND_SHADER_PACKAGE *package;
	const char *entry;
	uint32_t push;
};

const rbtrend_particles_shader_entry s_particle_shaders[RBTREND_PARTICLES_SHADER_INDEX_COUNT] = {
    {&rbtrend_particles_clear_comp_package, "rbtrend_particles_clear_comp", (uint32_t)sizeof(rbtrend_particles_step_push)},
    {&rbtrend_particles_simulate_comp_package, "rbtrend_particles_simulate_comp", (uint32_t)sizeof(rbtrend_particles_step_push)},
    {&rbtrend_particles_spawn_comp_package, "rbtrend_particles_spawn_comp", (uint32_t)sizeof(rbtrend_particles_step_push)},
    {&rbtrend_particles_arguments_comp_package, "rbtrend_particles_arguments_comp", (uint32_t)sizeof(rbtrend_particles_step_push)},
    {&rbtrend_particles_sort_count_comp_package, "rbtrend_particles_sort_count_comp", (uint32_t)sizeof(rbtrend_particles_sort_push)},
    {&rbtrend_particles_sort_scan_comp_package, "rbtrend_particles_sort_scan_comp", 0},
    {&rbtrend_particles_sort_scatter_comp_package, "rbtrend_particles_sort_scatter_comp", (uint32_t)sizeof(rbtrend_particles_sort_push)},
    {&rbtrend_particles_vert_package, "rbtrend_particles_vert", 0},
    {&rbtrend_particles_lit_frag_package, "rbtrend_particles_lit_frag", 0},
    {&rbtrend_particles_unlit_frag_package, "rbtrend_particles_unlit_frag", 0},
    {&rbtrend_particles_mesh_vert_package, "rbtrend_particles_mesh_vert", 0},
};

// The block's members against the reflection of [layout]'s binding 0.
bool rbtrend_particles_check_block(rbtrend_binding_layout layout) {
	typedef rbtrend_particles_block B;
	const rbtrend_block_member members[] = {
	    {"kind", (uint32_t)offsetof(B, kind)},
	    {"options", (uint32_t)offsetof(B, options)},
	    {"flipbook", (uint32_t)offsetof(B, flipbook)},
	    {"life_size", (uint32_t)offsetof(B, life_size)},
	    {"rotation", (uint32_t)offsetof(B, rotation)},
	    {"color", (uint32_t)offsetof(B, color)},
	    {"spawn_size", (uint32_t)offsetof(B, spawn_size)},
	    {"spawn_speed", (uint32_t)offsetof(B, spawn_speed)},
	    {"forces", (uint32_t)offsetof(B, forces)},
	    {"noise", (uint32_t)offsetof(B, noise)},
	    {"noise_scroll", (uint32_t)offsetof(B, noise_scroll)},
	    {"collision", (uint32_t)offsetof(B, collision)},
	    {"wrap", (uint32_t)offsetof(B, wrap)},
	    {"draw", (uint32_t)offsetof(B, draw)},
	    {"streak", (uint32_t)offsetof(B, streak)},
	    {"mesh", (uint32_t)offsetof(B, mesh)},
	    {"color_times", (uint32_t)offsetof(B, color_times)},
	    {"color_values", (uint32_t)offsetof(B, color_values)},
	    {"size_times", (uint32_t)offsetof(B, size_times)},
	    {"size_values", (uint32_t)offsetof(B, size_values)},
	    {"curves", (uint32_t)offsetof(B, curves)},
	};
	return rbtrend_lit_check_block(layout, 0, "RbtrendParticles", (uint32_t)sizeof(B), members, sizeof(members) / sizeof(members[0]));
}

// Shader [index], made if it isn't. NULL, reported, if it can't be.
rbtrend_shader rbtrend_particles_shader(rbtrend_instance instance, uint32_t index) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (!lit.particle_shaders[index]) {
		(void)rbtrend_shader_create(instance, s_particle_shaders[index].package, &lit.particle_shaders[index], nullptr);
		if (!lit.particle_shaders[index])
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's particle shader %s can't be created", s_particle_shaders[index].entry);
	}
	return lit.particle_shaders[index];
}

// [*out], the layout of set [set] made from shaders [first] to [last]
// (indices), its block checked, if it isn't made. False, reported, if it
// can't be.
bool rbtrend_particles_layout(rbtrend_instance instance, uint32_t first, uint32_t last, uint32_t set, rbtrend_binding_layout *out, const char *what) {
	if (*out)
		return true;
	rbtrend_shader shaders[RBTREND_PARTICLES_SHADER_INDEX_COUNT]{};
	for (uint32_t i = first; i <= last; ++i) {
		shaders[i - first] = rbtrend_particles_shader(instance, i);
		if (!shaders[i - first])
			return false;
	}
	(void)rbtrend_binding_layout_create_from_shaders(instance, shaders, last - first + 1, set, out, nullptr);
	if (!*out || !rbtrend_particles_check_block(*out)) {
		rbtrend_binding_layout_destroy(*out);
		*out = nullptr;
		rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's particles' %s can't be created", what);
		return false;
	}
	return true;
}

} // namespace

bool rbtrend_particles_prepare(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	if (lit.particle_pipelines[RBTREND_PARTICLES_PIPELINE_COUNT - 1] && lit.particle_draw_layout && lit.placeholder_3d_view)
		return true;
	if (!rbtrend_lit_prepare_resources(instance))
		return false;
	rbtrend_binding_layout scene_layout = nullptr;
	if (rbtrend_lit_get_scene_layout(instance, &scene_layout, nullptr) != RBT_RESULT_OK)
		return false;
	if (!rbtrend_particles_layout(instance, RBTREND_PARTICLES_SHADER_INDEX_SIMULATE, RBTREND_PARTICLES_SHADER_INDEX_SPAWN, 1, &lit.particle_sim_layout,
	                              "simulation") ||
	    !rbtrend_particles_layout(instance, RBTREND_PARTICLES_SHADER_INDEX_VERTEX, RBTREND_PARTICLES_SHADER_INDEX_MESH, RBTREND_PARTICLES_SET,
	                              &lit.particle_draw_layout, "draw"))
		return false;
	// The control's set is the clear's and the arguments' (indices 0 and 3):
	// made from the two.
	if (!lit.particle_control_layout) {
		rbtrend_shader both[2] = {rbtrend_particles_shader(instance, RBTREND_PARTICLES_SHADER_INDEX_CLEAR),
		                          rbtrend_particles_shader(instance, RBTREND_PARTICLES_SHADER_INDEX_ARGUMENTS)};
		if (!both[0] || !both[1])
			return false;
		(void)rbtrend_binding_layout_create_from_shaders(instance, both, 2, 0, &lit.particle_control_layout, nullptr);
		if (!lit.particle_control_layout || !rbtrend_particles_check_block(lit.particle_control_layout)) {
			rbtrend_binding_layout_destroy(lit.particle_control_layout);
			lit.particle_control_layout = nullptr;
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the binding layout of Rend's particles' control can't be created");
			return false;
		}
	}
	// The sort's shaders declare the simulation's set too: made with it.
	for (uint32_t i = RBTREND_PARTICLES_SHADER_INDEX_SORT_COUNT; i <= RBTREND_PARTICLES_SHADER_INDEX_SORT_SCATTER; ++i)
		if (!rbtrend_particles_shader(instance, i))
			return false;
	for (uint32_t i = 0; i < RBTREND_PARTICLES_PIPELINE_COUNT; ++i) {
		if (lit.particle_pipelines[i])
			continue;
		const bool control = i == RBTREND_PARTICLES_SHADER_INDEX_CLEAR || i == RBTREND_PARTICLES_SHADER_INDEX_ARGUMENTS;
		RBTREND_COMPUTE_PIPELINE_DESC desc{};
		desc.base           = rbt_struct_base(RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC);
		desc._debug_name    = s_particle_shaders[i].entry;
		desc.compute_shader = lit.particle_shaders[i];
		desc._entry_point   = s_particle_shaders[i].entry;
		if (control) {
			desc._binding_layouts[0]   = lit.particle_control_layout;
			desc._binding_layout_count = 1;
		} else {
			desc._binding_layouts[0]   = scene_layout;
			desc._binding_layouts[1]   = lit.particle_sim_layout;
			desc._binding_layout_count = 2;
		}
		if (s_particle_shaders[i].push > 0) {
			desc._push_constant_ranges[0].stage_flags = CRGPU_SHADER_STAGE_COMPUTE;
			desc._push_constant_ranges[0].offset      = 0;
			desc._push_constant_ranges[0].size        = s_particle_shaders[i].push;
			desc._push_constant_range_count           = 1;
		}
		RBTREND_ERROR error;
		(void)rbtrend_compute_pipeline_create(instance, &desc, &lit.particle_pipelines[i], &error);
		if (!lit.particle_pipelines[i]) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "Rend's particle pipeline %s can't be created: %s", s_particle_shaders[i].entry,
			               error.message);
			return false;
		}
	}
	if (!lit.placeholder_3d) {
		// A volume's placeholder, never read: a system's NOISE without a
		// volume is refused at the compile.
		RBTREND_TEXTURE3D_DESC desc{};
		desc.base       = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE3D_DESC);
		desc.width      = 1;
		desc.height     = 1;
		desc.depth      = 1;
		desc.format     = CRGPU_FORMAT_R8G8B8A8_SNORM;
		desc.usage      = RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE;
		desc.mip_levels = 1;
		(void)rbtrend_texture3d_create(instance, &desc, &lit.placeholder_3d, nullptr);
		if (!lit.placeholder_3d) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the particles' placeholder volume can't be created");
			return false;
		}
	}
	if (!lit.placeholder_3d_view) {
		RBTREND_TEXTURE_VIEW_DESC vd{};
		vd.base        = rbt_struct_base(RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC);
		vd.type        = RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE;
		vd.shape       = RBTREND_TEXTURE_VIEW_SHAPE_3D;
		vd.aspect      = RBTREND_TEXTURE_VIEW_ASPECT_COLOR;
		vd.mip_count   = 1;
		vd.layer_count = 1;
		(void)rbtrend_texture_view_create_3d(lit.placeholder_3d, &vd, &lit.placeholder_3d_view, nullptr);
		if (!lit.placeholder_3d_view) {
			rbtrend_report(instance, RBT_RESULT_REND_GPU_FAILURE, "the particles' placeholder volume's view can't be created");
			return false;
		}
	}
	return true;
}

void rbtrend_particles_builtins_destroy(rbtrend_instance instance) {
	rbtrend_lit_builtins &lit = instance->builtins.lit;
	for (rbtrend_compute_pipeline &pipeline : lit.particle_pipelines) {
		rbtrend_compute_pipeline_destroy(pipeline);
		pipeline = nullptr;
	}
	rbtrend_binding_layout *layouts[3] = {&lit.particle_sim_layout, &lit.particle_control_layout, &lit.particle_draw_layout};
	for (rbtrend_binding_layout *layout : layouts) {
		rbtrend_binding_layout_destroy(*layout);
		*layout = nullptr;
	}
	for (rbtrend_shader &shader : lit.particle_shaders) {
		rbtrend_shader_destroy(shader);
		shader = nullptr;
	}
	rbtrend_texture_view_destroy(lit.placeholder_3d_view);
	lit.placeholder_3d_view = nullptr;
	rbtrend_texture3d_destroy(lit.placeholder_3d);
	lit.placeholder_3d = nullptr;
}

// ---- A system ------------------------------------------------------------------------

rbtrend_particles_t::~rbtrend_particles_t() {
	// Every buffer and material goes through the release queue on its own
	// (rbtrend_particles_destroy lets them go first; a failed create here);
	// the readback is the GPU's until this runs, after it.
	rbtrend_material_destroy(this->draw);
	rbtrend_material_destroy(this->control);
	rbtrend_material_destroy(this->sim);
	rbtrend_buffer_destroy(this->arguments);
	rbtrend_buffer_destroy(this->sort);
	rbtrend_buffer_destroy(this->counters);
	rbtrend_buffer_destroy(this->lists);
	rbtrend_buffer_destroy(this->state);
	if (this->readback) {
		if (this->readback_mapped)
			crgpu_buffer_unmap(this->readback);
		crgpu_buffer_destroy(this->readback);
	}
	free((char *)this->desc.debug_name);
}

// [block] into each of [particles]' materials, for the next submission
// that binds it.
static void rbtrend_particles_write_block(rbtrend_particles particles) {
	rbtrend_material materials[3] = {particles->sim, particles->control, particles->draw};
	for (rbtrend_material material : materials) {
		for (uint32_t i = 0; i < material->slot_count; ++i) {
			if (material->slots[i].bytes) {
				memcpy(material->slots[i].bytes, &particles->block, sizeof(particles->block));
				material->slots[i].dirty = true;
			}
		}
	}
}

// The views the materials read: the noise volume (or the placeholder), the
// simulation's depth (or the placeholder), and the draw's depth for an
// effect with SOFT (or the placeholder). False if one can't be set.
static bool rbtrend_particles_set_views(rbtrend_particles particles) {
	const rbtrend_lit_builtins &lit = particles->instance->builtins.lit;
	rbtrend_texture_view noise      = particles->noise ? particles->noise : lit.placeholder_3d_view;
	rbtrend_texture_view depth      = particles->depth ? particles->depth : lit.placeholder_depth_view;
	const bool soft                 = (particles->modules & RBTREND_PARTICLES_MODULE_SOFT) && particles->depth;
	return rbtrend_material_set_texture(particles->sim, "particles_noise", 0, noise, nullptr) == RBT_RESULT_OK &&
	       rbtrend_material_set_texture(particles->sim, "particles_depth", 0, depth, nullptr) == RBT_RESULT_OK &&
	       rbtrend_material_set_texture(particles->draw, "particles_draw_depth", 0, soft ? particles->depth : lit.placeholder_depth_view, nullptr) ==
	           RBT_RESULT_OK;
}

// A material of [layout] for [particles] whose storage buffers [names] are
// [buffers]. NULL if it can't be made.
static rbtrend_material rbtrend_particles_material(rbtrend_particles particles, rbtrend_binding_layout layout, const char *const *names,
                                                  const rbtrend_buffer *buffers, uint32_t count) {
	RBTREND_MATERIAL_DESC md{};
	md.base                   = rbt_struct_base(RBT_STRUCT_TYPE_REND_MATERIAL_DESC);
	md._layout                = layout;
	rbtrend_material material = nullptr;
	if (rbtrend_material_create(particles->instance, &md, &material, nullptr) != RBT_RESULT_OK)
		return nullptr;
	for (uint32_t i = 0; i < count; ++i) {
		if (rbtrend_material_set_storage_buffer(material, names[i], 0, buffers[i], nullptr) != RBT_RESULT_OK) {
			rbtrend_material_destroy(material);
			return nullptr;
		}
	}
	return material;
}

rbtrend_particles_step_push rbtrend_particles_push_of(const RBTREND_PARTICLES_STEP &s, uint32_t capacity, uint32_t flags) {
	rbtrend_particles_step_push push{};
	const uint32_t spawn = s._spawn_count < capacity ? s._spawn_count : capacity;
	for (int c = 0; c < 3; ++c) {
		push.anchor_dt[c]      = s._anchor[c];
		push.moved_seed[c]     = s._anchor_moved[c];
		push.velocity_spawn[c] = s._emitter_velocity[c];
		push.wind_flags[c]     = s._wind[c];
	}
	push.anchor_dt[3] = s._delta_time;
	memcpy(&push.moved_seed[3], &s._seed, sizeof(uint32_t));
	memcpy(&push.velocity_spawn[3], &spawn, sizeof(uint32_t));
	memcpy(&push.wind_flags[3], &flags, sizeof(uint32_t));
	memcpy(push.emitter, s._emitter, sizeof(push.emitter));
	return push;
}

void rbtrend_particles_submitted(rbtrend_particles particles, uint32_t frame, uint64_t serial) {
	// What the copy held is read first if its writer has finished and it is
	// newer than what is known: the copy is about to be written again.
	const uint64_t completed = rbtrend_instance_read_completed(particles->instance);
	if (particles->readback_step[frame] > particles->known_step && particles->readback_serial[frame] <= completed) {
		uint32_t alive = 0;
		if (crgpu_buffer_invalidate(particles->readback, (uint64_t)frame * RBTREND_PARTICLES_READBACK_STRIDE, sizeof(alive)) == CR_SUCCESS) {
			memcpy(&alive, (const uint8_t *)particles->readback_mapped + (size_t)frame * RBTREND_PARTICLES_READBACK_STRIDE, sizeof(alive));
			particles->known_alive = alive;
			particles->known_step  = particles->readback_step[frame];
		}
	}
	++particles->steps_submitted;
	particles->readback_step[frame]   = particles->steps_submitted;
	particles->readback_serial[frame] = serial;
}

extern "C" {

RBT_RESULT rbtrend_particles_create(rbtrend_instance instance, const RBTREND_PARTICLES_DESC *desc, rbtrend_particles *out_particles,
                                    RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_particles)
		*out_particles = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_particles_create", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_particles_create");
	if (!desc)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");
	if (const RBT_RESULT base = rbtrend_base_check(out_error, "desc", &desc->base, RBT_STRUCT_TYPE_REND_PARTICLES_DESC))
		return base;
	if (desc->capacity == 0)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "capacity is 0");
	if (desc->capacity > RBTREND_PARTICLES_MAX_CAPACITY)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "capacity %u is above RBTREND_PARTICLES_MAX_CAPACITY, %u", desc->capacity,
		                    RBTREND_PARTICLES_MAX_CAPACITY);
	if (!out_particles)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_particles is NULL");
	if (!rbtrend_particles_prepare(instance))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's particle shaders and pipelines can't be made");

	char *name = nullptr;
	if (desc->debug_name) {
		const size_t length = strlen(desc->debug_name) + 1;
		name                = (char *)malloc(length);
		if (!name)
			return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the system's name");
		memcpy(name, desc->debug_name, length);
	}
	RBTREND_MALLOC__T(particles, rbtrend_particles_t);
	if (!particles) {
		free(name);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory for the system");
	}
	RBTREND_CONSTRUCT__T(particles, rbtrend_particles_t);
	particles->instance        = instance;
	particles->desc            = *desc;
	particles->desc.debug_name = name;
	particles->debug_name      = name;

	const uint64_t n      = desc->capacity;
	const uint64_t groups = (n + RBTREND_PARTICLES_SORT_GROUP - 1) / RBTREND_PARTICLES_SORT_GROUP;
	RBT_RESULT made       = rbtrend_buffer_create_device_storage(instance, n * 48u, 0, "rbtrend particles", &particles->state, out_error);
	if (made == RBT_RESULT_OK)
		made = rbtrend_buffer_create_device_storage(instance, n * 3u * 4u, 0, "rbtrend particles' lists", &particles->lists, out_error);
	if (made == RBT_RESULT_OK)
		made = rbtrend_buffer_create_device_storage(instance, RBTREND_PARTICLES_COUNTER_BYTES, 0, "rbtrend particles' record", &particles->counters,
		                                            out_error);
	if (made == RBT_RESULT_OK)
		made = rbtrend_buffer_create_device_storage(instance, (n * 3u + groups * 16u) * 4u, 0, "rbtrend particles' sort", &particles->sort, out_error);
	if (made == RBT_RESULT_OK)
		made = rbtrend_buffer_create_device_storage(instance, RBTREND_PARTICLES_ARGUMENT_BYTES, CRGPU_BUFFER_USAGE_INDIRECT, "rbtrend particles' arguments",
		                                            &particles->arguments, out_error);
	if (made != RBT_RESULT_OK) {
		RBTREND_DESTRUCT__T(particles, rbtrend_particles_t);
		return made;
	}
	const rbtrend_lit_builtins &lit = instance->builtins.lit;
	const char *const sim_names[4]  = {"ParticlesState", "ParticlesLists", "ParticlesCounters", "ParticlesSort"};
	const rbtrend_buffer sim_buffers[4] = {particles->state, particles->lists, particles->counters, particles->sort};
	const char *const control_names[3]  = {"ParticlesLists", "ParticlesCounters", "ParticlesArguments"};
	const rbtrend_buffer control_buffers[3] = {particles->lists, particles->counters, particles->arguments};
	const char *const draw_names[4]         = {"ParticlesState", "ParticlesLists", "ParticlesCounters", "ParticlesSort"};
	const rbtrend_buffer draw_buffers[4]    = {particles->state, particles->lists, particles->counters, particles->sort};
	particles->sim                          = rbtrend_particles_material(particles, lit.particle_sim_layout, sim_names, sim_buffers, 4);
	particles->control = particles->sim ? rbtrend_particles_material(particles, lit.particle_control_layout, control_names, control_buffers, 3) : nullptr;
	particles->draw    = particles->control ? rbtrend_particles_material(particles, lit.particle_draw_layout, draw_names, draw_buffers, 4) : nullptr;
	bool ok            = particles->draw && rbtrend_material_set_sampler(particles->sim, "particles_noise_sampler", 0, lit.material_sampler, nullptr) ==
	                                         RBT_RESULT_OK &&
	          rbtrend_particles_set_views(particles);
	if (!ok) {
		RBTREND_DESTRUCT__T(particles, rbtrend_particles_t);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "a set of the system can't be made");
	}
	particles->block.kind[3] = desc->capacity;
	rbtrend_particles_write_block(particles);

	crgpu_buffer_desc rd{};
	rd.base.type   = CR_STRUCT_TYPE_GPU_BUFFER_DESC;
	rd.memory_kind = CRGPU_MEMORY_KIND_READBACK;
	rd.size        = (uint64_t)RBTREND_MAX_FRAMES_IN_FLIGHT * RBTREND_PARTICLES_READBACK_STRIDE;
	rd.usage       = CRGPU_BUFFER_USAGE_TRANSFER_DST;
	CR_RESULT sr   = crgpu_buffer_create(instance->desc.device, &rd, &particles->readback);
	if (CR_SUCCESS == sr)
		sr = crgpu_buffer_map(particles->readback, 0, rd.size, &particles->readback_mapped);
	if (CR_FAILED(sr)) {
		RBTREND_DESTRUCT__T(particles, rbtrend_particles_t);
		return rbtrend_fail_cr(out_error, sr, "the system's readback can't be created");
	}
	// Its destroy's retire, promised now so the destroy can't fail.
	if (rbtrend_instance_reserve_release(instance, 1) != RBT_RESULT_OK) {
		RBTREND_DESTRUCT__T(particles, rbtrend_particles_t);
		return rbtrend_fail(out_error, RBT_RESULT_OUT_OF_MEMORY, "no memory to release the system later");
	}
	*out_particles = particles;
	return RBT_RESULT_OK;
}

void rbtrend_particles_destroy(rbtrend_particles particles) {
	if (!particles)
		return;
	if (RBT_STALE(particles, "rbtrend_particles_destroy", "particles"))
		return;
	// A list compiled with it and not submitted is refused at submit; its
	// buffers and materials go now, each through the release queue, and
	// the handle with its readback once the GPU is past it.
	rbtrend_instance_forget(particles->instance, particles);
	rbtrend_material_destroy(particles->draw);
	rbtrend_material_destroy(particles->control);
	rbtrend_material_destroy(particles->sim);
	rbtrend_buffer_destroy(particles->arguments);
	rbtrend_buffer_destroy(particles->sort);
	rbtrend_buffer_destroy(particles->counters);
	rbtrend_buffer_destroy(particles->lists);
	rbtrend_buffer_destroy(particles->state);
	particles->draw = particles->control = particles->sim = nullptr;
	particles->arguments = particles->sort = particles->counters = particles->lists = particles->state = nullptr;
	RBT_KILL(particles);
	rbtrend_instance_retire(particles->instance, particles, &rbtrend_release_handle<rbtrend_particles_t>);
}

// Whether [v]'s [count] values are each finite.
static bool rbtrend_particles_finite(const float *v, uint32_t count) {
	for (uint32_t i = 0; i < count; ++i)
		if (!std::isfinite(v[i]))
			return false;
	return true;
}

// [curve] of the module named [who] checked: its keys, their times in
// order within 0 to 1, and [lanes] values of each finite and at least 0.
static RBT_RESULT rbtrend_particles_curve_check(RBTREND_ERROR *error, const char *who, const RBTREND_PARTICLE_CURVE &curve, uint32_t lanes) {
	if (curve._key_count == 0)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's curve._key_count is 0", who);
	if (curve._key_count > RBTREND_PARTICLE_CURVE_MAX_KEYS)
		return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's curve._key_count %u is above RBTREND_PARTICLE_CURVE_MAX_KEYS, %u", who, curve._key_count,
		                    RBTREND_PARTICLE_CURVE_MAX_KEYS);
	for (uint32_t k = 0; k < curve._key_count; ++k) {
		const float t = curve._times[k];
		if (!(std::isfinite(t) && t >= 0.0f && t <= 1.0f))
			return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's curve._times[%u] %g is outside 0 to 1", who, k, (double)t);
		if (k > 0 && t < curve._times[k - 1])
			return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's curve._times[%u] %g is before _times[%u] %g", who, k, (double)t, k - 1,
			                    (double)curve._times[k - 1]);
	}
	for (uint32_t k = 0; k < curve._key_count; ++k)
		for (uint32_t c = 0; c < lanes; ++c)
			if (!(std::isfinite(curve._values[k][c]) && curve._values[k][c] >= 0.0f))
				return rbtrend_fail(error, RBT_RESULT_INVALID_ARGUMENT, "%s's curve._values[%u][%u] %g is below 0 or not finite", who, k, c,
				                    (double)curve._values[k][c]);
	return RBT_RESULT_OK;
}

// [curve]'s keys into the block's times and values ([lanes] each).
static void rbtrend_particles_curve_into(const RBTREND_PARTICLE_CURVE &curve, float times[2][4], float *values, uint32_t lanes) {
	for (uint32_t k = 0; k < RBTREND_PARTICLE_CURVE_MAX_KEYS; ++k) {
		const uint32_t from = k < curve._key_count ? k : curve._key_count - 1;
		times[k / 4][k % 4] = curve._times[from];
		if (lanes == 4)
			memcpy(values + k * 4, curve._values[from], sizeof(float) * 4);
		else
			values[k] = curve._values[from][0];
	}
}

RBT_RESULT rbtrend_particles_set_effect(rbtrend_particles particles, const RBTREND_PARTICLE_EFFECT *effect, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!particles)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is NULL");
	if (RBT_STALE(particles, "rbtrend_particles_set_effect", "particles"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is destroyed");
	RBTREND_ENTER_PARTICLES(particles, "rbtrend_particles_set_effect");
	if (!effect)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "effect is NULL");
	if (effect->base.type != RBT_STRUCT_TYPE_REND_PARTICLE_EFFECT)
		return rbtrend_fail(out_error, RBT_RESULT_STRUCT_WRONG_TYPE, "effect has a base.type other than its own");
	const RBTREND_PARTICLE_EFFECT &e = *effect;
	if (e.space != RBTREND_PARTICLE_SPACE_WORLD && e.space != RBTREND_PARTICLE_SPACE_EMITTER)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "space %u is not WORLD or EMITTER", (unsigned)e.space);
	if (e.draw != RBTREND_PARTICLE_DRAW_BILLBOARD && e.draw != RBTREND_PARTICLE_DRAW_STREAK && e.draw != RBTREND_PARTICLE_DRAW_MESH &&
	    e.draw != RBTREND_PARTICLE_DRAW_RIBBON)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "draw %u is not an RBTREND_PARTICLE_DRAW", (unsigned)e.draw);
	if (!(std::isfinite(e.life_min) && e.life_min > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "life_min %g is not above 0", (double)e.life_min);
	if (!(std::isfinite(e.life_max) && e.life_max >= e.life_min))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "life_max %g is below life_min %g or not finite", (double)e.life_max, (double)e.life_min);
	for (int c = 0; c < 4; ++c)
		if (!(std::isfinite(e.color[c]) && e.color[c] >= 0.0f))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "color[%d] %g is below 0 or not finite", c, (double)e.color[c]);
	if (!(std::isfinite(e.size_min) && e.size_min > 0.0f))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "size_min %g is not above 0", (double)e.size_min);
	if (!(std::isfinite(e.size_max) && e.size_max >= e.size_min))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "size_max %g is below size_min %g or not finite", (double)e.size_max, (double)e.size_min);
	if (!std::isfinite(e.rotation_min))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "rotation_min is not finite");
	if (!(std::isfinite(e.rotation_max) && e.rotation_max >= e.rotation_min))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "rotation_max %g is below rotation_min %g or not finite", (double)e.rotation_max,
		                    (double)e.rotation_min);
	if (!std::isfinite(e.spin_min))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "spin_min is not finite");
	if (!(std::isfinite(e.spin_max) && e.spin_max >= e.spin_min))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "spin_max %g is below spin_min %g or not finite", (double)e.spin_max, (double)e.spin_min);

	// The modules as chained, each once, each checked in its members' order.
	const RBTREND_PARTICLE_SPAWN *spawn                  = nullptr;
	const RBTREND_PARTICLE_FORCES *forces                = nullptr;
	const RBTREND_PARTICLE_NOISE *noise                  = nullptr;
	const RBTREND_PARTICLE_COLLISION *collision          = nullptr;
	const RBTREND_PARTICLE_WRAP *wrap                    = nullptr;
	const RBTREND_PARTICLE_COLOR_OVER_LIFE *color        = nullptr;
	const RBTREND_PARTICLE_SIZE_OVER_LIFE *size          = nullptr;
	const RBTREND_PARTICLE_FLIPBOOK *flipbook            = nullptr;
	const RBTREND_PARTICLE_SOFT *soft                    = nullptr;
	const RBTREND_PARTICLE_STREAK *streak                = nullptr;
	const RBTREND_PARTICLE_MESH *mesh                    = nullptr;
	const RBTREND_PARTICLE_RIBBON *ribbon                = nullptr;
	for (const RBT_STRUCT_BASE *link = e.base.next; link; link = link->next) {
		switch (link->type) {
		case RBT_STRUCT_TYPE_REND_PARTICLE_SPAWN: {
			if (spawn)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_SPAWN chained on twice");
			spawn                          = (const RBTREND_PARTICLE_SPAWN *)link;
			const RBTREND_PARTICLE_SPAWN &m = *spawn;
			if (m.shape < RBTREND_PARTICLE_SHAPE_POINT || m.shape > RBTREND_PARTICLE_SHAPE_DISC)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SPAWN's shape %u is not an RBTREND_PARTICLE_SHAPE", (unsigned)m.shape);
			for (int c = 0; c < 3; ++c)
				if (!(std::isfinite(m.size[c]) && m.size[c] >= 0.0f))
					return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SPAWN's size[%d] %g is below 0 or not finite", c, (double)m.size[c]);
			if (!(std::isfinite(m.spread) && m.spread >= 0.0f && m.spread <= 3.14159265358979f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SPAWN's spread %g is outside 0 to pi", (double)m.spread);
			if (!(std::isfinite(m.speed_min) && m.speed_min >= 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SPAWN's speed_min %g is below 0 or not finite", (double)m.speed_min);
			if (!(std::isfinite(m.speed_max) && m.speed_max >= m.speed_min))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SPAWN's speed_max %g is below speed_min %g or not finite",
				                    (double)m.speed_max, (double)m.speed_min);
			if (!std::isfinite(m.inherit_velocity))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SPAWN's inherit_velocity is not finite");
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_FORCES: {
			if (forces)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_FORCES chained on twice");
			forces = (const RBTREND_PARTICLE_FORCES *)link;
			if (!rbtrend_particles_finite(forces->acceleration, 3))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_FORCES' acceleration is not finite");
			if (!(std::isfinite(forces->drag) && forces->drag >= 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_FORCES' drag %g is below 0 or not finite", (double)forces->drag);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_NOISE: {
			if (noise)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_NOISE chained on twice");
			noise = (const RBTREND_PARTICLE_NOISE *)link;
			if (!(std::isfinite(noise->strength) && noise->strength >= 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_NOISE's strength %g is below 0 or not finite", (double)noise->strength);
			if (!(std::isfinite(noise->frequency) && noise->frequency > 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_NOISE's frequency %g is not above 0", (double)noise->frequency);
			if (!rbtrend_particles_finite(noise->scroll, 3))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_NOISE's scroll is not finite");
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_COLLISION: {
			if (collision)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_COLLISION chained on twice");
			collision                          = (const RBTREND_PARTICLE_COLLISION *)link;
			const RBTREND_PARTICLE_COLLISION &m = *collision;
			if (!(m.restitution >= 0.0f && m.restitution <= 1.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_COLLISION's restitution %g is outside 0 to 1", (double)m.restitution);
			if (!(m.friction >= 0.0f && m.friction <= 1.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_COLLISION's friction %g is outside 0 to 1", (double)m.friction);
			if (!(m.life_loss >= 0.0f && m.life_loss <= 1.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_COLLISION's life_loss %g is outside 0 to 1", (double)m.life_loss);
			if (!(std::isfinite(m.thickness) && m.thickness > 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_COLLISION's thickness %g is not above 0", (double)m.thickness);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_WRAP: {
			if (wrap)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_WRAP chained on twice");
			wrap = (const RBTREND_PARTICLE_WRAP *)link;
			for (int c = 0; c < 3; ++c)
				if (!(std::isfinite(wrap->half_extents[c]) && wrap->half_extents[c] > 0.0f))
					return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_WRAP's half_extents[%d] %g is not above 0", c,
					                    (double)wrap->half_extents[c]);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_COLOR_OVER_LIFE: {
			if (color)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_COLOR_OVER_LIFE chained on twice");
			color = (const RBTREND_PARTICLE_COLOR_OVER_LIFE *)link;
			if (const RBT_RESULT r = rbtrend_particles_curve_check(out_error, "RBTREND_PARTICLE_COLOR_OVER_LIFE", color->curve, 4))
				return r;
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_SIZE_OVER_LIFE: {
			if (size)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_SIZE_OVER_LIFE chained on twice");
			size = (const RBTREND_PARTICLE_SIZE_OVER_LIFE *)link;
			if (const RBT_RESULT r = rbtrend_particles_curve_check(out_error, "RBTREND_PARTICLE_SIZE_OVER_LIFE", size->curve, 1))
				return r;
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_FLIPBOOK: {
			if (flipbook)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_FLIPBOOK chained on twice");
			flipbook                          = (const RBTREND_PARTICLE_FLIPBOOK *)link;
			const RBTREND_PARTICLE_FLIPBOOK &m = *flipbook;
			if (m.columns == 0 || m.columns > 64)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_FLIPBOOK's columns %u is outside 1 to 64", m.columns);
			if (m.rows == 0 || m.rows > 64)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_FLIPBOOK's rows %u is outside 1 to 64", m.rows);
			if (m.frame_count == 0 || m.frame_count > m.columns * m.rows)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_FLIPBOOK's frame_count %u is outside 1 to columns x rows, %u",
				                    m.frame_count, m.columns * m.rows);
			if (!(std::isfinite(m.frames_per_second) && m.frames_per_second >= 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_FLIPBOOK's frames_per_second %g is below 0 or not finite",
				                    (double)m.frames_per_second);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_SOFT: {
			if (soft)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_SOFT chained on twice");
			soft = (const RBTREND_PARTICLE_SOFT *)link;
			if (!(std::isfinite(soft->distance) && soft->distance > 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_SOFT's distance %g is not above 0", (double)soft->distance);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_STREAK: {
			if (streak)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_STREAK chained on twice");
			streak                          = (const RBTREND_PARTICLE_STREAK *)link;
			const RBTREND_PARTICLE_STREAK &m = *streak;
			if (!(std::isfinite(m.seconds) && m.seconds >= 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_STREAK's seconds %g is below 0 or not finite", (double)m.seconds);
			if (!(std::isfinite(m.length_min) && m.length_min >= 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_STREAK's length_min %g is below 0 or not finite", (double)m.length_min);
			if (!(std::isfinite(m.length_max) && m.length_max >= m.length_min))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_STREAK's length_max %g is below length_min %g or not finite",
				                    (double)m.length_max, (double)m.length_min);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_MESH: {
			if (mesh)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_MESH chained on twice");
			mesh = (const RBTREND_PARTICLE_MESH *)link;
			if (mesh->index_count == 0)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_MESH's index_count is 0");
			if (mesh->index_count % 3 != 0)
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_MESH's index_count %u is not a multiple of 3", mesh->index_count);
			break;
		}
		case RBT_STRUCT_TYPE_REND_PARTICLE_RIBBON: {
			if (ribbon)
				return rbtrend_fail(out_error, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN, "effect has RBTREND_PARTICLE_RIBBON chained on twice");
			ribbon = (const RBTREND_PARTICLE_RIBBON *)link;
			if (!(std::isfinite(ribbon->repeats) && ribbon->repeats > 0.0f))
				return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "RBTREND_PARTICLE_RIBBON's repeats %g is not above 0", (double)ribbon->repeats);
			break;
		}
		default:
			return rbtrend_fail(out_error, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN, "effect has a struct chained on (type %u) that isn't one of its modules",
			                    (unsigned)link->type);
		}
	}
	if (e.draw == RBTREND_PARTICLE_DRAW_STREAK && !streak)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "draw is RBTREND_PARTICLE_DRAW_STREAK and no RBTREND_PARTICLE_STREAK is chained on");
	if (e.draw == RBTREND_PARTICLE_DRAW_MESH && !mesh)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "draw is RBTREND_PARTICLE_DRAW_MESH and no RBTREND_PARTICLE_MESH is chained on");
	if (e.draw == RBTREND_PARTICLE_DRAW_RIBBON && !ribbon)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "draw is RBTREND_PARTICLE_DRAW_RIBBON and no RBTREND_PARTICLE_RIBBON is chained on");
	if (e.draw == RBTREND_PARTICLE_DRAW_RIBBON && e.sort)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "draw is RBTREND_PARTICLE_DRAW_RIBBON and sort is set: a ribbon is drawn in spawn order");

	// Every check passed: the block whole.
	rbtrend_particles_block b{};
	uint32_t modules = 0;
	b.kind[1]        = (uint32_t)e.draw;
	b.kind[2]        = (uint32_t)e.space;
	b.kind[3]        = particles->desc.capacity;
	b.options[2]     = e.sort ? 1u : 0u;
	b.options[3]     = particles->reversed ? 1u : 0u;
	b.life_size[0]   = e.life_min;
	b.life_size[1]   = e.life_max;
	b.life_size[2]   = e.size_min;
	b.life_size[3]   = e.size_max;
	b.rotation[0]    = e.rotation_min;
	b.rotation[1]    = e.rotation_max;
	b.rotation[2]    = e.spin_min;
	b.rotation[3]    = e.spin_max;
	memcpy(b.color, e.color, sizeof(b.color));
	if (spawn) {
		modules |= RBTREND_PARTICLES_MODULE_SPAWN;
		b.options[0] = (uint32_t)spawn->shape;
		b.options[1] = spawn->surface ? 1u : 0u;
		memcpy(b.spawn_size, spawn->size, sizeof(float) * 3);
		b.spawn_size[3]  = spawn->spread;
		b.spawn_speed[0] = spawn->speed_min;
		b.spawn_speed[1] = spawn->speed_max;
		b.spawn_speed[2] = spawn->inherit_velocity;
	}
	if (forces) {
		modules |= RBTREND_PARTICLES_MODULE_FORCES;
		memcpy(b.forces, forces->acceleration, sizeof(float) * 3);
		b.forces[3] = forces->drag;
	}
	if (noise) {
		modules |= RBTREND_PARTICLES_MODULE_NOISE;
		b.noise[0] = noise->strength;
		b.noise[1] = noise->frequency;
		memcpy(b.noise_scroll, noise->scroll, sizeof(float) * 3);
	}
	if (collision) {
		modules |= RBTREND_PARTICLES_MODULE_COLLISION;
		b.collision[0] = collision->restitution;
		b.collision[1] = collision->friction;
		b.collision[2] = collision->life_loss;
		b.collision[3] = collision->thickness;
	}
	if (wrap) {
		modules |= RBTREND_PARTICLES_MODULE_WRAP;
		memcpy(b.wrap, wrap->half_extents, sizeof(float) * 3);
	}
	if (color) {
		modules |= RBTREND_PARTICLES_MODULE_COLOR_OVER_LIFE;
		rbtrend_particles_curve_into(color->curve, b.color_times, &b.color_values[0][0], 4);
		b.curves[0] = color->curve._key_count;
	}
	if (size) {
		modules |= RBTREND_PARTICLES_MODULE_SIZE_OVER_LIFE;
		rbtrend_particles_curve_into(size->curve, b.size_times, &b.size_values[0][0], 1);
		b.curves[1] = size->curve._key_count;
	}
	if (flipbook) {
		modules |= RBTREND_PARTICLES_MODULE_FLIPBOOK;
		b.flipbook[0] = flipbook->columns;
		b.flipbook[1] = flipbook->rows;
		b.flipbook[2] = flipbook->frame_count;
		b.flipbook[3] = flipbook->blend ? 1u : 0u;
		b.draw[0]     = flipbook->frames_per_second;
	}
	if (soft) {
		modules |= RBTREND_PARTICLES_MODULE_SOFT;
		b.draw[1] = soft->distance;
	}
	if (streak) {
		modules |= RBTREND_PARTICLES_MODULE_STREAK;
		b.draw[2]   = streak->seconds;
		b.streak[0] = streak->length_min;
		b.streak[1] = streak->length_max;
	}
	if (mesh) {
		modules |= RBTREND_PARTICLES_MODULE_MESH;
		b.mesh[0] = mesh->index_count;
		b.mesh[1] = mesh->first_index;
		memcpy(&b.mesh[2], &mesh->vertex_offset, sizeof(uint32_t));
	}
	if (ribbon) {
		modules |= RBTREND_PARTICLES_MODULE_RIBBON;
		b.draw[3] = ribbon->repeats;
	}
	b.kind[0] = modules;

	const uint32_t was_modules = particles->modules;
	particles->modules         = modules;
	// The draw's depth follows SOFT: set first, so nothing changes if it
	// can't be.
	if (((was_modules ^ modules) & RBTREND_PARTICLES_MODULE_SOFT) && !rbtrend_particles_set_views(particles)) {
		particles->modules = was_modules;
		(void)rbtrend_particles_set_views(particles);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "the system's draw set can't take its depth");
	}
	particles->block      = b;
	particles->has_effect = true;
	particles->draw_kind  = e.draw;
	particles->sorted     = e.sort;
	rbtrend_particles_write_block(particles);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_particles_set_noise(rbtrend_particles particles, rbtrend_texture_view noise, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!particles)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is NULL");
	if (RBT_STALE(particles, "rbtrend_particles_set_noise", "particles"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is destroyed");
	RBTREND_ENTER_PARTICLES(particles, "rbtrend_particles_set_noise");
	if (noise) {
		if (RBT_STALE(noise, "rbtrend_particles_set_noise", "noise"))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise is destroyed");
		if (noise->instance != particles->instance)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise is a view of another instance");
		if (noise->view_type != RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise is not a SHADER_RESOURCE view");
		if (noise->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_3D)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise is not a 3D view");
		const CRGPU_FORMAT format = noise->texture._t3d->desc.format;
		if (format != CRGPU_FORMAT_R8G8B8A8_SNORM && format != CRGPU_FORMAT_R16G16B16A16_FLOAT && format != CRGPU_FORMAT_R32G32B32A32_FLOAT)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "noise's format %u is not R8G8B8A8_SNORM, R16G16B16A16_FLOAT or R32G32B32A32_FLOAT",
			                    (unsigned)format);
	}
	rbtrend_texture_view was = particles->noise;
	particles->noise         = noise;
	if (!rbtrend_particles_set_views(particles)) {
		particles->noise = was;
		(void)rbtrend_particles_set_views(particles);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "the system's set can't take the volume");
	}
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_particles_set_depth(rbtrend_particles particles, rbtrend_texture_view depth, bool reversed_depth, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!particles)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is NULL");
	if (RBT_STALE(particles, "rbtrend_particles_set_depth", "particles"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is destroyed");
	RBTREND_ENTER_PARTICLES(particles, "rbtrend_particles_set_depth");
	if (depth) {
		if (RBT_STALE(depth, "rbtrend_particles_set_depth", "depth"))
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is destroyed");
		if (depth->instance != particles->instance)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is a view of another instance");
		if (depth->view_type != RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is not a SHADER_RESOURCE view");
		if (depth->desc.shape != RBTREND_TEXTURE_VIEW_SHAPE_2D)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is not a 2D view");
		if (depth->desc.aspect != RBTREND_TEXTURE_VIEW_ASPECT_DEPTH)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is not a view of the DEPTH aspect");
		if (depth->desc.mip_count != 1)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is a view of %u mips, not one", depth->desc.mip_count);
		if (depth->desc.layer_count != 1)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is a view of %u layers, not one", depth->desc.layer_count);
		if (depth->texture._t2d->desc.sample_count > 1)
			return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "depth is a view of a multisampled texture");
	}
	rbtrend_texture_view was = particles->depth;
	particles->depth         = depth;
	if (!rbtrend_particles_set_views(particles)) {
		particles->depth = was;
		(void)rbtrend_particles_set_views(particles);
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "the system's sets can't take the depth");
	}
	particles->reversed         = depth && reversed_depth;
	particles->block.options[3] = particles->reversed ? 1u : 0u;
	rbtrend_particles_write_block(particles);
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_particles_get_alive_count(rbtrend_particles particles, uint32_t *out_alive, uint64_t *out_step, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (!particles)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is NULL");
	if (RBT_STALE(particles, "rbtrend_particles_get_alive_count", "particles"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "particles is destroyed");
	RBTREND_ENTER_PARTICLES(particles, "rbtrend_particles_get_alive_count");
	if (!out_alive)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_alive is NULL");
	// The newest copy whose writer has finished.
	const uint64_t completed = rbtrend_instance_read_completed(particles->instance);
	uint32_t newest          = UINT32_MAX;
	for (uint32_t f = 0; f < RBTREND_MAX_FRAMES_IN_FLIGHT; ++f) {
		if (particles->readback_step[f] <= particles->known_step || particles->readback_serial[f] > completed)
			continue;
		if (newest == UINT32_MAX || particles->readback_step[f] > particles->readback_step[newest])
			newest = f;
	}
	if (newest != UINT32_MAX) {
		const uint64_t at = (uint64_t)newest * RBTREND_PARTICLES_READBACK_STRIDE;
		const CR_RESULT sr = crgpu_buffer_invalidate(particles->readback, at, sizeof(uint32_t));
		if (CR_FAILED(sr))
			return rbtrend_fail_cr(out_error, sr, "the system's readback can't be read");
		uint32_t alive = 0;
		memcpy(&alive, (const uint8_t *)particles->readback_mapped + at, sizeof(alive));
		particles->known_alive = alive;
		particles->known_step  = particles->readback_step[newest];
	}
	*out_alive = particles->known_alive;
	if (out_step)
		*out_step = particles->known_step;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_particles_get_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_layout)
		*out_layout = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_particles_get_layout", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_particles_get_layout");
	if (!out_layout)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_layout is NULL");
	if (!rbtrend_particles_layout(instance, RBTREND_PARTICLES_SHADER_INDEX_VERTEX, RBTREND_PARTICLES_SHADER_INDEX_MESH, RBTREND_PARTICLES_SET,
	                              &instance->builtins.lit.particle_draw_layout, "draw"))
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "the layout of Rend's particle pipelines' set 2 can't be made");
	*out_layout = instance->builtins.lit.particle_draw_layout;
	return RBT_RESULT_OK;
}

RBT_RESULT rbtrend_particles_get_shader(rbtrend_instance instance, RBTREND_PARTICLES_SHADER shader, rbtrend_shader *out_shader, RBTREND_ERROR *out_error) {
	rbtrend_error_clear(out_error);
	if (out_shader)
		*out_shader = nullptr;
	if (!instance)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is NULL");
	if (RBT_STALE(instance, "rbtrend_particles_get_shader", "instance"))
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "instance is destroyed");
	RBTREND_ENTER(instance, "rbtrend_particles_get_shader");
	if (shader > RBTREND_PARTICLES_SHADER_UNLIT_FRAGMENT)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "%u is not an RBTREND_PARTICLES_SHADER", shader);
	if (!out_shader)
		return rbtrend_fail(out_error, RBT_RESULT_INVALID_ARGUMENT, "out_shader is NULL");
	const uint32_t index = shader == RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX ? (uint32_t)RBTREND_PARTICLES_SHADER_INDEX_VERTEX
	                       : shader == RBTREND_PARTICLES_SHADER_MESH_VERTEX     ? (uint32_t)RBTREND_PARTICLES_SHADER_INDEX_MESH
	                       : shader == RBTREND_PARTICLES_SHADER_LIT_FRAGMENT    ? (uint32_t)RBTREND_PARTICLES_SHADER_INDEX_LIT
	                                                                            : (uint32_t)RBTREND_PARTICLES_SHADER_INDEX_UNLIT;
	*out_shader = rbtrend_particles_shader(instance, index);
	if (!*out_shader)
		return rbtrend_fail(out_error, RBT_RESULT_REND_GPU_FAILURE, "Rend's particle shader %u can't be made", shader);
	return RBT_RESULT_OK;
}

} // extern "C"
