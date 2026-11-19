#ifndef RBTREND_PARTICLES_INTERNAL_HPP
#define RBTREND_PARTICLES_INTERNAL_HPP

/*
 * Rend's particles (rbtrendparticles.h; card L73's Rend half,
 * RBTREND_TODO.md item 57): the handle, the block and push constants its
 * shaders read, and what RBTREND_COMMAND_PARTICLES_STEP's and
 * RBTREND_COMMAND_DRAW_PARTICLES' compiles (rbtrendcommands.cpp) ask of it.
 *
 * Who writes what, when: every buffer is made by rbtrend_particles_create()
 * and written only by the GPU, by a step's stages (shaders/rbtrend_particles_*):
 * - the particles (`state`), 48 bytes each;
 * - `lists`: two alive lists and the dead list, `capacity` words each;
 * - `counters`: the system's record (COUNTER_* in rbtrend_particles_common.slang):
 *   what its last step left, the anchor and emitter its draw places it by;
 * - `sort`: two key lists and one value list (the other is the alive
 *   list itself), then the sort's counts, 16 a group of 256;
 * - `arguments`: the next simulation's dispatch, the count read back, the
 *   draw's and the sort's (ARGS_*); a storage and an indirect argument
 *   buffer, moved between the two by the list's tracking (item 15).
 * The effect's block is the CPU's: rbtrend_particles_set_effect() and
 * _set_depth() write it into each of the system's three materials (the
 * simulation's set 1, the control's set 0, the draw's set 2), whose
 * submission copies it. The readback is the CPU's to read: a copy a frame
 * in flight of the count the step's arguments stage wrote, each marked at
 * the submission that writes it (the serial and the step's number) and read
 * once that serial has completed (rbtrend_instance_read_completed()).
 * Which list is current and how many are alive is the GPU's alone, so a
 * list compiled and never submitted leaves nothing behind but a step not
 * counted.
 */

#include "rbtrend_internal.hpp"
#include "rbtrendlit_internal.hpp"
#include "render/rbtrendparticles.h"
#include "../rbtoverlap.hpp"
#include "../rbtplaindata.hpp"
#include "../rbtstale.hpp"

#include <cstdint>

/* RbtrendParticles in shaders/rbtrend_particles_common.slang: the effect as
 * every set of a system holds it. Checked member by member against the
 * shaders' reflection when the layouts are made. */
struct rbtrend_particles_block {
	uint32_t kind[4];
	uint32_t options[4];
	uint32_t flipbook[4];
	float life_size[4];
	float rotation[4];
	float color[4];
	float spawn_size[4];
	float spawn_speed[4];
	float forces[4];
	float noise[4];
	float noise_scroll[4];
	float collision[4];
	float wrap[4];
	float draw[4];
	float streak[4];
	uint32_t mesh[4];
	float color_times[2][4];
	float color_values[8][4];
	float size_times[2][4];
	float size_values[2][4];
	uint32_t curves[4];
};
RBT_PLAIN_DATA(rbtrend_particles_block);
static_assert(sizeof(rbtrend_particles_block) == 496, "RbtrendParticles in rbtrend_particles_common.slang: 496 bytes");

/* ParticlesStep in shaders/rbtrend_particles_common.slang: a step's values,
 * the push constants of the simulation, spawn and arguments stages. Each w
 * but anchor_dt's holds a uint's bits (the seed, the spawn count, the
 * flags). */
struct rbtrend_particles_step_push {
	float anchor_dt[4];
	float moved_seed[4];
	float emitter[3][4];
	float velocity_spawn[4];
	float wind_flags[4];
};
RBT_PLAIN_DATA(rbtrend_particles_step_push);
static_assert(sizeof(rbtrend_particles_step_push) == 112, "ParticlesStep in rbtrend_particles_common.slang: 112 bytes");

/* ParticlesSortPass in shaders/rbtrend_particles_sort.slang. */
struct rbtrend_particles_sort_push {
	uint32_t pass[4];
};
RBT_PLAIN_DATA(rbtrend_particles_sort_push);

/* A step is copied into the command list at the send and read at the
 * compile (rbtrendcommands.cpp). */
RBT_PLAIN_DATA(RBTREND_PARTICLES_STEP);

/* RbtrendParticles.kind.x's bits: the modules an effect has. */
enum : uint32_t {
	RBTREND_PARTICLES_MODULE_SPAWN           = 1u,
	RBTREND_PARTICLES_MODULE_FORCES          = 2u,
	RBTREND_PARTICLES_MODULE_NOISE           = 4u,
	RBTREND_PARTICLES_MODULE_COLLISION       = 8u,
	RBTREND_PARTICLES_MODULE_WRAP            = 16u,
	RBTREND_PARTICLES_MODULE_COLOR_OVER_LIFE = 32u,
	RBTREND_PARTICLES_MODULE_SIZE_OVER_LIFE  = 64u,
	RBTREND_PARTICLES_MODULE_FLIPBOOK        = 128u,
	RBTREND_PARTICLES_MODULE_SOFT            = 256u,
	RBTREND_PARTICLES_MODULE_STREAK          = 512u,
	RBTREND_PARTICLES_MODULE_MESH            = 1024u,
	RBTREND_PARTICLES_MODULE_RIBBON          = 2048u,
};

/* The step's flags (ParticlesStep.wind_flags.w): it clears; the system's
 * first step ever (its record is not yet one: the spawn count starts at 0). */
#define RBTREND_PARTICLES_STEP_CLEAR 1u
#define RBTREND_PARTICLES_STEP_FIRST 2u

/* The record and the arguments, in bytes; where the arguments' parts are. */
#define RBTREND_PARTICLES_COUNTER_BYTES 96u
#define RBTREND_PARTICLES_ARGUMENT_BYTES 80u
#define RBTREND_PARTICLES_ARGS_SIMULATE 0u
#define RBTREND_PARTICLES_ARGS_ALIVE 12u
#define RBTREND_PARTICLES_ARGS_DRAW 16u
#define RBTREND_PARTICLES_ARGS_SORT 32u
#define RBTREND_PARTICLES_ARGS_DRAW_INDEXED 48u
/* The threads of a step's group, and of a sort's. */
#define RBTREND_PARTICLES_STEP_GROUP 64u
#define RBTREND_PARTICLES_SORT_GROUP 256u
/* Each frame copy's bytes in the readback. */
#define RBTREND_PARTICLES_READBACK_STRIDE 16u

/* Rend's particle shaders and a step's pipelines, by their index in
 * rbtrend_lit_builtins (particle_shaders, particle_pipelines). */
enum : uint32_t {
	RBTREND_PARTICLES_SHADER_INDEX_CLEAR        = 0,
	RBTREND_PARTICLES_SHADER_INDEX_SIMULATE     = 1,
	RBTREND_PARTICLES_SHADER_INDEX_SPAWN        = 2,
	RBTREND_PARTICLES_SHADER_INDEX_ARGUMENTS    = 3,
	RBTREND_PARTICLES_SHADER_INDEX_SORT_COUNT   = 4,
	RBTREND_PARTICLES_SHADER_INDEX_SORT_SCAN    = 5,
	RBTREND_PARTICLES_SHADER_INDEX_SORT_SCATTER = 6,
	RBTREND_PARTICLES_SHADER_INDEX_VERTEX       = 7,
	RBTREND_PARTICLES_SHADER_INDEX_LIT          = 8,
	RBTREND_PARTICLES_SHADER_INDEX_UNLIT        = 9,
	RBTREND_PARTICLES_SHADER_INDEX_MESH         = 10,
	RBTREND_PARTICLES_SHADER_INDEX_COUNT        = 11,
	RBTREND_PARTICLES_PIPELINE_COUNT            = 7,
};

typedef struct rbtrend_particles_t {
	char *debug_name{nullptr}; /* first, as every handle's: rbt_debug_name_set() writes it (rbtcore.h) */
	rbtrend_particles_t() = default;
	~rbtrend_particles_t();
	rbtrend_instance instance{nullptr};
	/* The desc as given; its debug_name the copy made at creation. */
	RBTREND_PARTICLES_DESC desc{};
	rbtrend_buffer state{nullptr};
	rbtrend_buffer lists{nullptr};
	rbtrend_buffer counters{nullptr};
	rbtrend_buffer sort{nullptr};
	rbtrend_buffer arguments{nullptr};
	/* Of the simulation's layout (set 1 of the simulation, spawn and sort),
	 * the control's (set 0 of the clear and arguments) and the draw's (set
	 * 2 of a particle pipeline). */
	rbtrend_material sim{nullptr};
	rbtrend_material control{nullptr};
	rbtrend_material draw{nullptr};
	/* The borrowed views as set (NULL for none) and the depth's direction. */
	rbtrend_texture_view noise{nullptr};
	rbtrend_texture_view depth{nullptr};
	bool reversed{false};
	/* The effect as set: whether there is one, its modules, its draw, and
	 * the block every set reads. */
	bool has_effect{false};
	uint32_t modules{0};
	RBTREND_PARTICLE_DRAW draw_kind{RBTREND_PARTICLE_DRAW_NONE};
	bool sorted{false};
	rbtrend_particles_block block{};
	/* The readback: a copy a frame in flight, mapped for life; each copy's
	 * writer's serial and step number (0, 0 for none yet), the latest
	 * count read from one whose serial had completed, and its step. */
	crgpu_buffer readback{nullptr};
	void *readback_mapped{nullptr};
	uint64_t readback_serial[RBTREND_MAX_FRAMES_IN_FLIGHT]{};
	uint64_t readback_step[RBTREND_MAX_FRAMES_IN_FLIGHT]{};
	uint32_t known_alive{0};
	uint64_t known_step{0};
	/* Steps submitted: once one has, the system's buffers hold a record
	 * (its first step clears them). */
	uint64_t steps_submitted{0};
	rbt_overlap entered{};
	RBT_STALE_TAG
} rbtrend_particles_t;

#define RBTREND_ENTER_PARTICLES(particles, who) RBT_ENTER((particles)->entered, "rbtrend", who, "particles")

/* The particle shaders, layouts, pipelines and placeholder, made if they
 * aren't. False, reported, if one can't be. */
bool rbtrend_particles_prepare(rbtrend_instance instance);

/* A step's values as its stages' push constants: [s] with its spawn count
 * no more than [capacity], its flags [flags] (RBTREND_PARTICLES_STEP_*). */
rbtrend_particles_step_push rbtrend_particles_push_of(const RBTREND_PARTICLES_STEP &s, uint32_t capacity, uint32_t flags);

/* A submission's work for [particles]: the step it ran counted, and frame
 * copy [frame] of its readback marked as written by it at [serial]. */
void rbtrend_particles_submitted(rbtrend_particles particles, uint32_t frame, uint64_t serial);

#endif // RBTREND_PARTICLES_INTERNAL_HPP
