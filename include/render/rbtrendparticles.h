#ifndef RBTREND_PARTICLES_H
#define RBTREND_PARTICLES_H

#include "rbtrendcommands.h"
#include "rbtrendlit.h"

/**
 * @file rbtrendparticles.h
 * @brief Particles simulated on the GPU by Rend: spawned, moved by forces
 * and curl noise, collided with a depth buffer, aged and killed, sorted for
 * blending and drawn as billboards, streaks, meshes or ribbons lit by the
 * lit scene, never read back but for a count; each system's look a fixed
 * set of modules with curves, the caller's data; its particles kept
 * relative to an anchor whose place the caller gives each step.
 *
 * @par What a system is
 * An rbtrend_particles is a system: a pool of `capacity` particles on the
 * device, its effect (RBTREND_PARTICLE_EFFECT and the modules chained on
 * it, copied by rbtrend_particles_set_effect()), and the noise volume and
 * depth it borrows (rbtrend_particles_set_noise(),
 * rbtrend_particles_set_depth()). Each step the caller sends it an
 * RBTREND_PARTICLES_STEP: the time since its last step, how many to spawn
 * (the GPU spawns as many as it has dead slots for, the rest dropped), a
 * seed for the step's random values, where its anchor is in the scene's
 * space and how far it moved since the last step (both from the caller's
 * own precision), its emitter's transform from the anchor and its
 * velocity, the air's velocity, and whether to kill every particle first.
 * Each particle spawned is numbered in the system's spawn order (a
 * clear doesn't start the count again), which a ribbon is drawn in.
 * Every particle's place is kept relative to the anchor, so it is small
 * wherever the anchor is: an emitter's place, or a grid cell round a camera
 * that moves by whole cells.
 *
 * @par Two spaces
 * In RBTREND_PARTICLE_SPACE_WORLD each particle stays where it is as the
 * anchor moves (the step's `_anchor_moved` is taken off every particle:
 * exact for a grid cell's whole moves). In RBTREND_PARTICLE_SPACE_EMITTER
 * particles are kept in the emitter's own frame and move and turn with it:
 * gravity and the wind are turned into that frame each step, and a
 * particle's velocity there is its own (the emitter's velocity is not
 * inherited).
 *
 * @par The modules
 * Each a based struct chained onto RBTREND_PARTICLE_EFFECT, at most once;
 * absent, off. In the order a step runs them: SPAWN (where and how a
 * particle starts; without it, at the emitter's origin, still), FORCES (an
 * acceleration and a drag towards the step's wind), NOISE (a velocity read
 * from the system's noise volume, added to the motion, not kept), COLLISION
 * (against the system's depth, through the step's scene's view), WRAP (a
 * box round the anchor that places wrap into), and for the draw
 * COLOR_OVER_LIFE and SIZE_OVER_LIFE (curves multiplying the effect's
 * colour and a particle's size), FLIPBOOK (frames of the material's base
 * colour map), SOFT (a fade where a particle meets the depth) and STREAK
 * (a streak's length). Random values are a hash of the step's seed and the
 * particle's place in the step's spawn, so a particle's start does not
 * depend on which slot it takes.
 *
 * @par A step on the GPU
 * RBTREND_COMMAND_PARTICLES_STEP (rbtrendcommands.h) runs, outside a pass,
 * for every system of its array, each stage for every system before the
 * next: a clear (a step with `_clear`, and a system's first step ever),
 * the simulation (each alive particle aged, killed, or moved and appended
 * to this step's alive list), the spawn (dead slots taken), the arguments
 * (next step's simulation and this step's draw, from the alive count, and
 * the count copied for rbtrend_particles_get_alive_count()), and for a
 * sorted effect a radix sort of a 16-bit key of each particle's distance
 * from the eye (four passes of 4 bits, each a count, a scan and a scatter),
 * back to front, stable. Its scene gives the eye and the view-projection
 * the collision and the sort read; the depth the systems collide with is
 * each system's own, which must be the command's `_depth`.
 *
 * @par Drawing
 * As for lit surfaces (rbtrendlit.h, "What is whose"), Rend ships the
 * shaders and the caller makes the pipeline: Rend's particle vertex shader
 * (RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX, no vertex buffer) and one of
 * its fragment shaders (RBTREND_PARTICLES_SHADER_LIT_FRAGMENT, lit as a lit
 * surface is, or RBTREND_PARTICLES_SHADER_UNLIT_FRAGMENT), over set 0 the
 * lit scene's layout, set 1 the lit material's, and set
 * RBTREND_PARTICLES_SET (2) the system's own, rbtrend_particles_get_layout()
 * (set 3 stays the caller's); the blend, the targets and depth testing the
 * caller's. Both fragment shaders write colour premultiplied by its
 * opacity: ONE, ONE_MINUS_SRC_ALPHA for smoke (sorted), ONE, ONE for
 * sparks and rain (unsorted). The vertex shader writes locations 0 to 4 as
 * rbtrendlit.h's "A caller's vertex shader" says (the place, a normal
 * facing the eye, the flipbook frame's texture coordinate, no tangent, the
 * colour over life) and location 5, the flipbook's next frame and blend,
 * which Rend's two particle fragment shaders read. Inside the caller's
 * pass, with its pipeline and sets 0 and 1 set, RBTREND_COMMAND_DRAW_PARTICLES
 * binds the system's set and draws indirectly what its last step left alive
 * (six vertices a particle; for a ribbon, six a segment between each
 * particle and the one spawned before it, one with a dead end drawing
 * nothing). A mesh is drawn with Rend's mesh vertex shader
 * (RBTREND_PARTICLES_SHADER_MESH_VERTEX) over the caller's vertex buffer of
 * RBTREND_LIT_VERTEX at slot 0 and its index buffer, the MESH module's
 * range of it an instance a particle. Systems are drawn in the caller's
 * order; a system's own particles in its sort's.
 *
 * @par Lit
 * The lit fragment shader is the lit surface's shading (by the scene's
 * directional lights and their shadows, its point and spot lights, its
 * ambient light and environment, faded by its atmosphere's aerial volume
 * and its fog), with the material's opacity times the colour over life's
 * alpha times the soft fade. The unlit one is the material's base colour
 * times its map times the colour over life, plus its emission, exposed and
 * encoded as the scene's surfaces are. Particles cast no shadow.
 *
 * @par Precision
 * A particle's place is a float relative to its anchor: within a system's
 * reach (a few hundred metres for weather's box, tens for an explosion) a
 * float holds it to a micrometre; the anchor is the caller's float, rounded
 * once from its own precision.
 *
 * @par What is whose
 * Rend simulates, sorts and ships the shaders; the caller decides when a
 * system steps and by how much, how many spawn, the pipeline, the blend
 * and the order systems are drawn in. A system's particles are never read
 * back: nothing but the lagged count (rbtrend_particles_get_alive_count())
 * is the caller's to read, and no gameplay depends on a particle.
 *
 * @par Determinism
 * None across GPUs or runs: a dead slot is taken by an atomic, so which
 * slot a particle takes and the alive list's order differ from run to run;
 * a sorted system's draw order is its keys', ties apart (the sort is
 * stable, the order before it is not). The count read back is exact.
 *
 * @par Threads
 * A system is one object for the library's rule (rbtcore.h, "Threads"):
 * one caller at a time. A compile and a submission of a list that steps or
 * draws it use it (the submission counts its steps and marks its
 * readback): no call on the system overlaps them.
 *
 * @par Out of scope
 * Shadows cast by particles; lighting through a volume (a later stage of
 * the fog's grid); a particle read by gameplay; an emitter's rate, bursts
 * or clock (the caller's arithmetic). A material's alpha mode: the lit
 * fragment shader shades its material as blended by its opacity
 * whatever its alpha_cutoff (a cut-out discards, as a lit surface's does).
 */

#if __cplusplus
extern "C" {
#endif

/** The most particles a system holds: 1,048,576. The alive and dead lists'
 * indices and the sort's are 32-bit, and the sort's counts are sized from
 * it; past a million, a caller splits a system. */
#define RBTREND_PARTICLES_MAX_CAPACITY 1048576u

/** The most keys a curve has. A curve is a fixed record in the system's
 * block. */
#define RBTREND_PARTICLE_CURVE_MAX_KEYS 8u

/** The set slot of a particle pipeline's system, after the lit scene's (0)
 * and the material's (1). */
#define RBTREND_PARTICLES_SET 2u

/** The most steps one RBTREND_COMMAND_PARTICLES_STEP takes: they are copied
 * into the list at the send. */
#define RBTREND_PARTICLES_MAX_STEPS 4096u

/**
 * @brief A system's pool. No field has a default.
 */
typedef struct RBTREND_PARTICLES_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_PARTICLES_DESC, and its chain,
	 * which takes nothing (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** How many particles it holds at most: 1 to RBTREND_PARTICLES_MAX_CAPACITY. */
	uint32_t capacity;
} RBTREND_PARTICLES_DESC;

/**
 * @brief Creates a system with no effect, every particle dead. Until
 * rbtrend_particles_set_effect(), a step spawns nothing and
 * RBTREND_COMMAND_DRAW_PARTICLES draws nothing.
 *
 * Memory, all of it here and nothing later, on the device: the particles,
 * 48 bytes each; their alive and dead lists, 12 bytes each; the sort's keys
 * and values, 12 bytes each, and its counts, 64 bytes a group of 256; the
 * system's record and arguments, 176 bytes; three blocks of the effect
 * (496 bytes each) with the descriptors of their three sets; and a readback
 * of 16 bytes a frame in flight. About 72 bytes a particle: 4.5 MiB at
 * 65,536, 72 MiB at the most.
 *
 * @param[in]  instance      Instance it is of.
 * @param[in]  desc          Its capacity. Only read during the call.
 * @param[out] out_particles Receives the system; NULL on failure.
 * @param[out] out_error     Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if an argument is NULL
 *         or the capacity is 0 or past RBTREND_PARTICLES_MAX_CAPACITY, each
 *         its own check and message; the struct codes for `desc->base`;
 *         RBT_RESULT_OUT_OF_MEMORY; or RBT_RESULT_REND_GPU_FAILURE if a
 *         buffer, set or Rend's particle pipelines can't be made.
 */
RBT_RESULT rbtrend_particles_create(
	rbtrend_instance instance,
	const RBTREND_PARTICLES_DESC *desc,
	rbtrend_particles *out_particles,
	RBTREND_ERROR *out_error);

/**
 * @brief Destroys a system once the GPU is past it (rbtrendcontext.h,
 * "Destroying"). No list may name it afterwards: a list compiled with it
 * and not yet submitted is refused at submit. NULL does nothing.
 *
 * @param[in] particles The system.
 */
void rbtrend_particles_destroy(rbtrend_particles particles);

/** @brief Where a system's particles live. */
typedef enum RBTREND_PARTICLE_SPACE RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_PARTICLE_SPACE_NONE    = 0,
	/** Each stays where it is in the world as its anchor moves (smoke left
	 * behind, rain in a box round a camera). */
	RBTREND_PARTICLE_SPACE_WORLD   = 1,
	/** Each moves and turns with its emitter (a flame on an engine). */
	RBTREND_PARTICLE_SPACE_EMITTER = 2,
} RBTREND_PARTICLE_SPACE;

/** @brief How a system's particles are drawn. */
typedef enum RBTREND_PARTICLE_DRAW RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_PARTICLE_DRAW_NONE      = 0,
	/** A quad facing the eye, turned by its rotation. */
	RBTREND_PARTICLE_DRAW_BILLBOARD = 1,
	/** A quad along its velocity as the eye sees it, its length from the
	 * STREAK module (rain, sparks); its width the particle's size. */
	RBTREND_PARTICLE_DRAW_STREAK    = 2,
	/** A mesh of RBTREND_LIT_VERTEX, the MESH module's index range of the
	 * index buffer the caller sets, scaled by the particle's size and
	 * turned about its space's z by its rotation and spin. */
	RBTREND_PARTICLE_DRAW_MESH      = 3,
	/** One strip through the alive particles in spawn order (a trail):
	 * a segment between each particle and the one spawned before it while
	 * both are alive, as wide as each end's size, facing the eye, the
	 * material's map repeated along it as the RIBBON module says (no
	 * FLIPBOOK read). Never sorted. */
	RBTREND_PARTICLE_DRAW_RIBBON    = 4,
} RBTREND_PARTICLE_DRAW;

/** @brief The SPAWN module's shapes, in the emitter's space. */
typedef enum RBTREND_PARTICLE_SHAPE RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_PARTICLE_SHAPE_NONE   = 0,
	/** The emitter's origin. */
	RBTREND_PARTICLE_SHAPE_POINT  = 1,
	/** A sphere of radius size[0]. */
	RBTREND_PARTICLE_SHAPE_SPHERE = 2,
	/** A box of half extents size. */
	RBTREND_PARTICLE_SHAPE_BOX    = 3,
	/** A cone along +Y from its apex at the origin, its base's radius
	 * size[0] at the height size[1]. */
	RBTREND_PARTICLE_SHAPE_CONE   = 4,
	/** A disc in the emitter's XZ plane of radius size[0]. */
	RBTREND_PARTICLE_SHAPE_DISC   = 5,
} RBTREND_PARTICLE_SHAPE;

/**
 * @brief A curve over a particle's life: keys at times 0 to 1, values
 * linear between them, the first key's before it and the last's after.
 */
/* Frozen: a value inside the modules that hold one; a new kind of curve gets a new type. */
typedef struct RBTREND_PARTICLE_CURVE {
	/** How many keys: 1 to RBTREND_PARTICLE_CURVE_MAX_KEYS. */
	uint32_t _key_count;
	/** Each key's time, 0 to 1, each at least the one before. */
	float _times[RBTREND_PARTICLE_CURVE_MAX_KEYS];
	/** Each key's value: four for a colour (linear RGBA), the first alone
	 * for a scalar. Finite. */
	float _values[RBTREND_PARTICLE_CURVE_MAX_KEYS][4];
} RBTREND_PARTICLE_CURVE;

/**
 * @brief An effect: what every particle of a system shares, with its
 * modules chained on `base` (each once; absent, off). No field has a
 * default.
 */
typedef struct RBTREND_PARTICLE_EFFECT {
	/** This struct's type, RBT_STRUCT_TYPE_REND_PARTICLE_EFFECT, and its chain of modules. */
	RBT_STRUCT_BASE base;
	/** Its space. */
	RBTREND_PARTICLE_SPACE space;
	/** How it is drawn. */
	RBTREND_PARTICLE_DRAW draw;
	/** Whether a step sorts its particles back to front (for blending that
	 * isn't additive). */
	bool sort;
	/** A particle's life, seconds: finite, above 0, the least at most the most. */
	float life_min;
	/** See #life_min. */
	float life_max;
	/** Its colour at birth, linear RGBA, each finite and at least 0 (the
	 * curve multiplies it). */
	float color[4];
	/** Its size at birth, the scene's unit: finite, above 0, the least at
	 * most the most. */
	float size_min;
	/** See #size_min. */
	float size_max;
	/** Its rotation at birth, radians round the axis to the eye: finite,
	 * the least at most the most. */
	float rotation_min;
	/** See #rotation_min. */
	float rotation_max;
	/** Its spin, radians a second: finite, the least at most the most. */
	float spin_min;
	/** See #spin_min. */
	float spin_max;
} RBTREND_PARTICLE_EFFECT;

/** @brief SPAWN: where and how particles start. */
typedef struct RBTREND_PARTICLE_SPAWN {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_SPAWN, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** Its shape in the emitter's space. */
	RBTREND_PARTICLE_SHAPE shape;
	/** The shape's sizes (RBTREND_PARTICLE_SHAPE says which it reads),
	 * finite, at least 0. */
	float size[3];
	/** On the shape's surface only, not filling it. */
	bool surface;
	/** The angle a start's direction spreads from the shape's normal (the
	 * emitter's +Y for a point, a box or a disc; outwards for a sphere;
	 * along the side from the apex for a cone), radians, 0 to pi. */
	float spread;
	/** The start's speed, the scene's unit a second: finite, at least 0,
	 * the least at most the most. */
	float speed_min;
	/** See #speed_min. */
	float speed_max;
	/** How much of the step's emitter velocity a particle starts with
	 * (WORLD space only): finite. */
	float inherit_velocity;
} RBTREND_PARTICLE_SPAWN;

/** @brief FORCES: gravity and the air. */
typedef struct RBTREND_PARTICLE_FORCES {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_FORCES, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** A constant acceleration, the scene's axes, the scene's unit a second
	 * squared: finite (gravity, or a buoyancy upwards). */
	float acceleration[3];
	/** How fast a particle's velocity goes to the step's wind, per second
	 * (v becomes wind + (v - wind) e^(-drag dt)): finite, at least 0, 0
	 * none. */
	float drag;
} RBTREND_PARTICLE_FORCES;

/** @brief NOISE: turbulence from the system's noise volume
 * (rbtrend_particles_set_noise()). */
typedef struct RBTREND_PARTICLE_NOISE {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_NOISE, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The velocity it adds at full strength (the volume's xyz times
	 * this), the scene's unit a second: finite, at least 0. */
	float strength;
	/** How many of the volume's tiles a unit of the scene spans: finite,
	 * above 0. */
	float frequency;
	/** How fast the volume moves through the particles, tiles a second:
	 * finite. */
	float scroll[3];
} RBTREND_PARTICLE_NOISE;

/** @brief COLLISION, against the system's depth (rbtrend_particles_set_depth()). */
typedef struct RBTREND_PARTICLE_COLLISION {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_COLLISION, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The part of its speed across the surface it keeps, 0 to 1. */
	float restitution;
	/** The part of its speed along the surface it loses, 0 to 1. */
	float friction;
	/** The part of its life a hit takes, 0 to 1 (1 kills). */
	float life_loss;
	/** How far behind the depth surface a particle is still caught, the
	 * scene's unit: finite, above 0. */
	float thickness;
} RBTREND_PARTICLE_COLLISION;

/** @brief WRAP: a box round the anchor that places wrap into
 * (precipitation round a camera). */
typedef struct RBTREND_PARTICLE_WRAP {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_WRAP, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The box's half extents round the anchor, in the particles' space:
	 * finite, above 0. */
	float half_extents[3];
} RBTREND_PARTICLE_WRAP;

/** @brief COLOR_OVER_LIFE: a fade, a fire's cooling. */
typedef struct RBTREND_PARTICLE_COLOR_OVER_LIFE {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_COLOR_OVER_LIFE, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** RGBA multiplying the effect's colour; each value at least 0. */
	RBTREND_PARTICLE_CURVE curve;
} RBTREND_PARTICLE_COLOR_OVER_LIFE;

/** @brief SIZE_OVER_LIFE: smoke growing, sparks shrinking. */
typedef struct RBTREND_PARTICLE_SIZE_OVER_LIFE {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_SIZE_OVER_LIFE, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The first value multiplying a particle's size; each at least 0. */
	RBTREND_PARTICLE_CURVE curve;
} RBTREND_PARTICLE_SIZE_OVER_LIFE;

/** @brief FLIPBOOK: frames in the material's base colour map, in rows from
 * the top left. */
typedef struct RBTREND_PARTICLE_FLIPBOOK {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_FLIPBOOK, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The grid's columns, 1 to 64. */
	uint32_t columns;
	/** The grid's rows, 1 to 64. */
	uint32_t rows;
	/** How many frames, 1 to columns x rows. */
	uint32_t frame_count;
	/** Frames a second, finite, above 0, looping; or 0 for the frames
	 * spread over its life (frame k from life k / frame_count). */
	float frames_per_second;
	/** Blend each frame into the next. */
	bool blend;
} RBTREND_PARTICLE_FLIPBOOK;

/** @brief SOFT: a fade where a particle meets the system's depth (soft
 * particles). */
typedef struct RBTREND_PARTICLE_SOFT {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_SOFT, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The distance in front of the depth over which it fades, the scene's
	 * unit: finite, above 0. */
	float distance;
} RBTREND_PARTICLE_SOFT;

/** @brief STREAK, for RBTREND_PARTICLE_DRAW_STREAK: a streak from where a
 * particle is back along its velocity. */
typedef struct RBTREND_PARTICLE_STREAK {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_STREAK, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** Its length per speed, seconds (the distance covered in this time):
	 * finite, at least 0. */
	float seconds;
	/** Its length's bounds, the scene's unit: finite, at least 0, the least
	 * at most the most. */
	float length_min;
	/** See #length_min. */
	float length_max;
} RBTREND_PARTICLE_STREAK;

/** @brief MESH, for RBTREND_PARTICLE_DRAW_MESH: the range of the index
 * buffer the caller sets (RBTREND_COMMAND_SET_INDEX_BUFFER) before
 * RBTREND_COMMAND_DRAW_PARTICLES, of the vertex buffer it sets at slot 0
 * (debris, leaves). */
typedef struct RBTREND_PARTICLE_MESH {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_MESH, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** Its indices: above 0, a multiple of 3. */
	uint32_t index_count;
	/** The first. */
	uint32_t first_index;
	/** Added to each index. */
	int32_t vertex_offset;
} RBTREND_PARTICLE_MESH;

/** @brief RIBBON, for RBTREND_PARTICLE_DRAW_RIBBON (trails). */
typedef struct RBTREND_PARTICLE_RIBBON {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_RIBBON, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** How many times the material's map repeats along the strip's whole
	 * length (the u of its texture coordinate, 0 at the newest particle):
	 * finite, above 0. */
	float repeats;
} RBTREND_PARTICLE_RIBBON;

/**
 * @brief Sets a system's effect: copied. Every particle alive takes it
 * from the next step and the next draw (its life, size, rotation and spin
 * drawn at its birth kept); the pool is kept.
 *
 * @param[in]  particles The system.
 * @param[in]  effect    The effect and its chained modules, each field in
 *                       its range (the structs above). Only read during the
 *                       call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument
 *         or a field out of its range, each its own check and message in
 *         the structs' order (the effect's, then each module's as chained);
 *         RBT_RESULT_STRUCT_WRONG_TYPE for the effect's base,
 *         RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN for a struct chained on that
 *         isn't a module, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN for a module
 *         twice; RBT_RESULT_INVALID_ARGUMENT for STREAK, MESH or RIBBON
 *         drawn without its module, or RIBBON with `sort` (a ribbon is
 *         drawn in spawn order).
 *         Nothing changes on failure.
 */
RBT_RESULT rbtrend_particles_set_effect(
	rbtrend_particles particles,
	const RBTREND_PARTICLE_EFFECT *effect,
	RBTREND_ERROR *out_error);

/**
 * @brief Sets the noise volume its NOISE module reads, or none.
 *
 * @param[in]  particles The system.
 * @param[in]  noise     A SHADER_RESOURCE 3D view of a tiling vector field
 *                       (its xyz each -1 to 1, read repeating and linear),
 *                       of R8G8B8A8_SNORM, R16G16B16A16_FLOAT or
 *                       R32G32B32A32_FLOAT; NULL for none. Borrowed: it
 *                       outlives every list that steps the system with it.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL system, a
 *         view of another instance, not a shader resource, not 3D, or of
 *         another format, each its own check. A step of a system whose
 *         effect has NOISE and which has no volume is refused at the
 *         compile.
 */
RBT_RESULT rbtrend_particles_set_noise(
	rbtrend_particles particles,
	rbtrend_texture_view noise,
	RBTREND_ERROR *out_error);

/**
 * @brief Sets the depth its COLLISION and SOFT modules read, or none: the
 * depth the step's scene's view drew, read in steps and draws until set
 * again.
 *
 * The draw reads it only for an effect with SOFT; a pass that draws such a
 * system can't have it as its depth target (refused at the compile).
 *
 * @param[in]  particles      The system.
 * @param[in]  depth          A SHADER_RESOURCE 2D view, DEPTH aspect, of
 *                            one mip and one layer of a single-sampled
 *                            texture; NULL for none. Borrowed, as a scene's
 *                            scene colour is.
 * @param[in]  reversed_depth It was drawn near at 1, far at 0.
 * @param[out] out_error      Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL system or a
 *         view of another instance, not a shader resource, not 2D, not of
 *         depth, of more than one mip or layer, or multisampled, each its
 *         own check.
 */
RBT_RESULT rbtrend_particles_set_depth(
	rbtrend_particles particles,
	rbtrend_texture_view depth,
	bool reversed_depth,
	RBTREND_ERROR *out_error);

/**
 * @brief How many particles were alive after the system's last step whose
 * submission has completed (a frame or two behind the steps sent), and
 * which step that was (the count of steps submitted to it, from 1).
 *
 * @param[in]  particles The system.
 * @param[out] out_alive Receives the count; 0 before any step completed.
 * @param[out] out_step  Receives that step's number; 0 for none. May be NULL.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL system or
 *         @p out_alive.
 */
RBT_RESULT rbtrend_particles_get_alive_count(
	rbtrend_particles particles,
	uint32_t *out_alive,
	uint64_t *out_step,
	RBTREND_ERROR *out_error);

/**
 * @brief The layout of a particle pipeline's set RBTREND_PARTICLES_SET: the
 * instance's, made on first use (don't destroy it).
 *
 * @param[in]  instance   The instance.
 * @param[out] out_layout Receives the layout; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_layout is NULL; or RBT_RESULT_REND_GPU_FAILURE if it or
 *         the shaders it is made from can't be created.
 */
RBT_RESULT rbtrend_particles_get_layout(
	rbtrend_instance instance,
	rbtrend_binding_layout *out_layout,
	RBTREND_ERROR *out_error);

/** @brief The shaders Rend ships for particles. */
typedef uint32_t RBTREND_PARTICLES_SHADER;
enum {
	/** Billboards, streaks and ribbons: no vertex buffer; writes locations
	 * 0 to 4 as rbtrendlit.h's "A caller's vertex shader" says, and 5.
	 * Entry point "rbtrend_particles_vert". */
	RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX = 0,
	/** Meshes of RBTREND_LIT_VERTEX at vertex buffer slot 0
	 * (rbtrend_get_lit_vertex_layout()), an instance a particle, drawn
	 * indexed from the MESH module's range of the index buffer the caller
	 * sets: scaled, turned and placed as RBTREND_PARTICLE_DRAW_MESH says;
	 * writes locations 0 to 4 (the mesh's normal and tangent turned with
	 * it) and 5. Entry point "rbtrend_particles_mesh_vert". */
	RBTREND_PARTICLES_SHADER_MESH_VERTEX      = 1,
	/** Lit as RBTREND_LIT_SHADER_SURFACE_FRAGMENT is, with the SOFT fade and
	 * the FLIPBOOK's blend. Entry point "rbtrend_particles_lit_frag". */
	RBTREND_PARTICLES_SHADER_LIT_FRAGMENT     = 2,
	/** The colour over life times the material's base colour and its map,
	 * plus its emission, exposed and encoded as the scene's surfaces are;
	 * SOFT and FLIPBOOK as the lit one. Entry point
	 * "rbtrend_particles_unlit_frag". */
	RBTREND_PARTICLES_SHADER_UNLIT_FRAGMENT   = 3,
};

/**
 * @brief One of Rend's particle shaders: the instance's, made on first
 * use (don't destroy it).
 *
 * @param[in]  instance   The instance.
 * @param[in]  shader     Which.
 * @param[out] out_shader Receives the shader; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_shader is NULL or @p shader is not an
 *         RBTREND_PARTICLES_SHADER; or RBT_RESULT_REND_GPU_FAILURE if it
 *         can't be created.
 */
RBT_RESULT rbtrend_particles_get_shader(
	rbtrend_instance instance,
	RBTREND_PARTICLES_SHADER shader,
	rbtrend_shader *out_shader,
	RBTREND_ERROR *out_error);

/**
 * @brief One system's step, an element of RBTREND_COMMAND_PARTICLES_STEP's
 * array. Each value is checked at the send, in member order.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_PARTICLES_STEP {
	/** The system: of the list's instance, at most once in an array. */
	rbtrend_particles _particles;
	/** Seconds since its last step: finite, 0 to 1 (0 holds every particle
	 * still and ages none). */
	float _delta_time;
	/** How many to spawn: as many as it has dead slots for are; past its
	 * capacity counts as its capacity. */
	uint32_t _spawn_count;
	/** This step's random values' seed. */
	uint32_t _seed;
	/** Kill every particle before anything else. */
	bool _clear;
	/** Its anchor's place, the scene's space (for a caller drawing relative
	 * to the eye, its anchor's place less the eye's): finite. */
	float _anchor[3];
	/** How far its anchor moved since its last step, the scene's axes:
	 * finite. */
	float _anchor_moved[3];
	/** Its emitter from the anchor: three rows of a 3x4 matrix, row after
	 * row (x' = row 0 . (x, y, z, 1)), finite, its 3x3 part invertible. */
	float _emitter[12];
	/** Its emitter's velocity, the scene's unit a second: finite. */
	float _emitter_velocity[3];
	/** The air's velocity at the anchor, the scene's unit a second: finite. */
	float _wind[3];
} RBTREND_PARTICLES_STEP;

#if __cplusplus
}
#endif

#endif // RBTREND_PARTICLES_H
