#ifndef RBTREND_ATMOSPHERE_INTERNAL_HPP
#define RBTREND_ATMOSPHERE_INTERNAL_HPP

/*
 * Rend's atmosphere (rbtrendatmosphere.h): the handle, the block its passes
 * read, and what the commands and the lit scene ask of it.
 *
 * Who writes what, when (rbtrendatmosphere.h, the draft's section 3): an
 * atmosphere's two tables are written by RBTREND_COMMAND_ATMOSPHERE_TABLES
 * only, as passes in its list, and read after them; a scene's sky-view table
 * (and aerial volume) by its RBTREND_COMMAND_LIT_ATMOSPHERE, read after it
 * by the frame's sky, surfaces and environment fill; each a render target
 * the list's tracked states move to SHADER_READ_ONLY when written, so its
 * readers are ordered after it, and one queue orders a frame's writes after
 * the frame before's reads. The blocks are materials, copied into the
 * frame's arena by the compile that binds them: the CPU never writes what a
 * frame in flight reads.
 */

#include "rbtrend_internal.hpp"
#include "rbtrendlit_internal.hpp"
#include "render/rbtrendatmosphere.h"
#include "../rbtplaindata.hpp"
#include "../rbtstale.hpp"

#include <cstdint>

/* RbtrendAtmospherePass in shaders/rbtrend_atmosphere_pass.slang: the
 * medium, the marches' steps, the tables' sizes, and a scene's eye, suns
 * and environment frame. Its members' offsets are checked against the
 * shader's reflection when the layout is made. */
struct rbtrend_atmosphere_pass_block {
	float radii[4];
	float rayleigh[4];
	float mie_scattering[4];
	float mie_extinction[4];
	float absorption[4];
	float ground_albedo[4];
	uint32_t steps[4];
	float sizes[4];
	float more_sizes[4];
	float frame_up[4];
	float frame_x[4];
	float frame_y[4];
	float sun_direction[2][4];
	float sun_illuminance[2][4];
	float environment_rotation[12];
	float array_sizes[4];
	float inverse_view_projection[16];
	float view_direction[4];
};
RBT_PLAIN_DATA(rbtrend_atmosphere_pass_block);
static_assert(sizeof(rbtrend_atmosphere_pass_block) == 400, "RbtrendAtmospherePass in rbtrend_atmosphere_pass.slang");

/* RbtrendAerialParams in shaders/rbtrend_atmosphere_aerial_frag.slang: the
 * slice a pass writes, and the volume's texels across and down. */
struct rbtrend_aerial_params {
	uint32_t slice;
	uint32_t padding;
	float size[2];
};

/* ShellParams in shaders/rbtrend_atmosphere_shell_frag.slang, a shell's
 * push constants (RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS): its centre and
 * light count, each light's direction (w 1 where it lights the shell) and
 * illuminance (pi times its radiance) at the centre, and the least and
 * most steps, whether the scene's own air is in front, and whether depth
 * is reversed. */
struct rbtrend_shell_params {
	float center_lights[4];
	float light_direction[2][4];
	float light_illuminance[2][4];
	uint32_t steps[4];
};
RBT_PLAIN_DATA(rbtrend_shell_params);
static_assert(sizeof(rbtrend_shell_params) == 96, "ShellParams in rbtrend_atmosphere_shell_frag.slang: 96 bytes of push constants");

/* A shell is copied into the command list at the send and read at the
 * compile (rbtrendcommands.cpp). */
RBT_PLAIN_DATA(RBTREND_LIT_ATMOSPHERE_SHELL);
static_assert(sizeof(RBTREND_LIT_ATMOSPHERE_SHELL) == sizeof(void *) + 24, "RBTREND_LIT_ATMOSPHERE_SHELL: 32 bytes on a 64-bit build");

/* The passes, each a fragment shader over the full-screen vertex shader
 * rendering RBTREND_ATMOSPHERE_FORMAT. */
enum {
	RBTREND_ATMOSPHERE_PASS_TRANSMITTANCE = 0, /* an atmosphere's transmittance table */
	RBTREND_ATMOSPHERE_PASS_MULTISCATTER  = 1, /* its multiple-scattering table, from the first */
	RBTREND_ATMOSPHERE_PASS_SKY_VIEW      = 2, /* a scene's sky-view table, from both */
	RBTREND_ATMOSPHERE_PASS_ENVIRONMENT   = 3, /* a face of an environment's radiance cube, from a sky-view table */
	RBTREND_ATMOSPHERE_PASS_COPY          = 4, /* a table into a layer of a scene's atmosphere array, texel for texel */
	RBTREND_ATMOSPHERE_PASS_AERIAL        = 5, /* a slice of a scene's aerial volume, its light and its transmittance (two targets), from both of an atmosphere's tables */
	RBTREND_ATMOSPHERE_PASS_COUNT         = 6,
};

/* Every table's format: half floats keep a night sky's low end. */
#define RBTREND_ATMOSPHERE_FORMAT CRGPU_FORMAT_R16G16B16A16_FLOAT

typedef struct rbtrend_atmosphere_t {
	char *debug_name{nullptr}; /* first, as every handle's: rbt_debug_name_set() writes it (rbtcore.h) */
	rbtrend_atmosphere_t() = default;
	~rbtrend_atmosphere_t();
	rbtrend_instance instance{nullptr};
	/* The desc as given; its debug_name not kept (the handle's own copy is). */
	RBTREND_ATMOSPHERE_DESC desc{};
	/* The medium as last set, whether one has been, and whether the tables
	 * were last built from another (ATMOSPHERE_TABLES rebuilds them then). */
	RBTMATH_SHELL_MEDIUM medium{};
	bool has_medium{false};
	bool changed{false};
	/* Whether a compile has built the tables from a medium yet: until then a
	 * scene draws no atmosphere from them. */
	bool built{false};
	/* The tables: each a texture, a render target view and a view to read. */
	rbtrend_texture2d transmittance{nullptr};
	rbtrend_texture_view transmittance_target{nullptr};
	rbtrend_texture_view transmittance_view{nullptr};
	rbtrend_texture2d multiple_scattering{nullptr};
	rbtrend_texture_view multiple_scattering_target{nullptr};
	rbtrend_texture_view multiple_scattering_view{nullptr};
	/* Of the atmosphere pass layout: the block its table passes read,
	 * written by ATMOSPHERE_TABLES. */
	rbtrend_material material{nullptr};
	rbt_overlap entered{};
	RBT_STALE_TAG
} rbtrend_atmosphere_t;

#define RBTREND_ENTER_ATMOSPHERE(atmosphere, who) RBT_ENTER((atmosphere)->entered, "rbtrend", who, "atmosphere")

/* The atmosphere passes' shaders and their layout, made if they aren't.
 * False, reported, if they can't be. */
bool rbtrend_atmosphere_prepare(rbtrend_instance instance);

/* Pass [pass]'s pipeline, made if it isn't. NULL, reported, if it can't be. */
rbtrend_graphics_pipeline rbtrend_atmosphere_pipeline(rbtrend_instance instance, uint32_t pass);

/* [atmosphere]'s medium, its tables' sizes and their steps into [block]. */
void rbtrend_atmosphere_fill_block(const rbtrend_atmosphere_t &atmosphere, rbtrend_atmosphere_pass_block &block);

/* Whether a table has been built from [atmosphere]'s medium. */
bool rbtrend_atmosphere_drawable(const rbtrend_atmosphere_t &atmosphere);

/* The width, height and layers of a scene's atmosphere array for
 * [atmosphere] as set (rbtrendlit_internal.hpp,
 * rbtrend_lit_scene_t::atmosphere_tables): every table's largest side, and
 * two layers and two a slice of the volume (its light, its transmittance). */
void rbtrend_lit_atmosphere_array_size(const RBTREND_LIT_ATMOSPHERE &atmosphere, uint32_t out_size[3]);

/* The scene's atmosphere members of its block (rbtrend_lit_scene_block),
 * from its atmosphere as set, its eye and its first directional light: the
 * centre and radii, the eye's frame (its up away from the centre, x
 * towards the first sun's bearing round it), the suns' count, the tables'
 * sizes, and the flags. Called by every commit. */
void rbtrend_lit_scene_resolve_atmosphere(rbtrend_lit_scene scene);

/* The block a scene's atmosphere passes read: its atmosphere's medium and
 * steps, its eye's frame, its suns and its environment's frame. */
void rbtrend_lit_scene_atmosphere_block(rbtrend_lit_scene scene, rbtrend_atmosphere_pass_block &block);

/* Writes [block] into every slot of [material] that holds the atmosphere
 * pass block, for the compile that binds it next. */
void rbtrend_atmosphere_write_block(rbtrend_material material, const rbtrend_atmosphere_pass_block &block);

#endif // RBTREND_ATMOSPHERE_INTERNAL_HPP
