#ifndef RBTREND_LIT_H
#define RBTREND_LIT_H

#include "rbtrendcommands.h"
#include "rbtrendmaterial.h"
#include "../rbtmath.h"
#include "../math/rbtmathfog.h"

#if __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @file rbtrendlit.h
 * @brief Rend's lit surfaces: physically based shading of metal/roughness
 * materials, with a clear coat and sheen, under directional lights with
 * cascaded shadows, point and spot lights, each with a shadow of its own, a
 * hemisphere of ambient light, an environment (a picture or a sky) and
 * ambient occlusion - shaders Rend ships, and the objects that feed them.
 *
 * @par What is whose
 * Rend ships the shading, never the geometry. A surface is drawn by a
 * pipeline the caller makes, with Rend's surface fragment shader
 * (RBTREND_LIT_SHADER_SURFACE_FRAGMENT) and a vertex shader: the caller's
 * own, for geometry of its own kind, or Rend's
 * (RBTREND_LIT_SHADER_SURFACE_VERTEX), for meshes of RBTREND_LIT_VERTEX with
 * a transform per instance. Unreal splits a surface the same way (a vertex
 * factory per kind of geometry, a shading model per kind of surface); Hazel
 * ships one shader with both in it, for its one vertex layout. There is no
 * scene graph and no list of what to draw: what is drawn, and in what
 * order, is the caller's (a draw list, rbtrenddrawlist.h, fits). What Rend
 * does decide is what the light does once a surface is drawn.
 *
 * @par Sets
 * The surface fragment shader reads two sets, whose layouts are Rend's:
 * - set RBTREND_LIT_SCENE_SET (0), rbtrend_lit_get_scene_layout(): what one
 *   view of the world has in common - the eye, the lights, the ambient light
 *   and the shadows. It is an rbtrend_lit_scene's material
 *   (rbtrend_lit_scene_get_material()), bound once per pass with
 *   RBTREND_COMMAND_SET_MATERIAL.
 * - set RBTREND_LIT_MATERIAL_SET (1), rbtrend_lit_get_material_layout(): one
 *   surface - an rbtrend_material from rbtrend_lit_material_create(), bound
 *   per draw (a draw list binds it, at slot 1).
 * Sets 2 and 3 are the caller's, for its vertex shader (a camera, say).
 * A pipeline names Rend's two layouts at slots 0 and 1, and its own after
 * them (a layered pipeline names the scene's layout at slot 0 and
 * rbtrend_lit_get_layered_material_layout() at slot 1); a caller's vertex shader declares nothing in sets 0 and 1. A pass
 * that renders a shadow binds, at set 0, the material
 * rbtrend_lit_scene_get_shadow_pass_material() gives for the light and view
 * it renders (of rbtrend_lit_get_shadow_layout()), which holds the matrix the
 * view is rendered through and leaves out the atlas itself: a pass can't
 * read what it renders into.
 *
 * @par A caller's vertex shader
 * It writes, as outputs the fragment shader reads, every one of:
 * - `layout(location = 0) out vec3`: the surface's position;
 * - `layout(location = 1) out vec3`: its normal, of any length (0 faces the
 *   eye);
 * - `layout(location = 2) out vec2`: its texture coordinate (0 if it has
 *   none: every map then reads its one texel at the corner);
 * - `layout(location = 3) out vec4`: its tangent, xyz along the texture's
 *   u, w +1 or -1 for which way v runs (glTF's and MikkTSpace's), or 0 for
 *   none, which turns normal mapping off;
 * - `layout(location = 4) out vec4`: its color, linear RGBA, which
 *   multiplies the base color and the opacity, as glTF 2.0's COLOR_0 does
 *   (the base color factor times the base color map times the vertex
 *   color); (1, 1, 1, 1) for geometry with none. Emission is not
 *   multiplied by it.
 * All five are written even where the geometry has no use for them: Metal
 * refuses a pipeline whose fragment shader reads what its vertex shader
 * doesn't write, and D3D12 matches the stages by register in order
 * (Carrottus GPU's rule: every output from location 0 up to the last one read).
 *
 * For the layered fragment shaders (RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT
 * and its reflecting twin) it writes five more after the five, every one:
 * - `layout(location = 5) out vec4` and `layout(location = 6) out vec4`:
 *   the weights of the eight entries of location 7, each at least 0;
 * - `layout(location = 7) flat out uvec2`: the eight entries' layer
 *   indices, a byte each (entry e in byte e % 4 of component e / 4),
 *   RBTREND_LIT_LAYERED_NO_LAYER for none (an index at or past the
 *   material's layer count reads as none); the same at every vertex of a
 *   primitive (a flat output is read from one of them);
 * - `layout(location = 8) out vec3`: the triplanar place, in the
 *   projection's frame, in the units a layer's `uv_scale` divides (0 where
 *   no layer is triplanar);
 * - `layout(location = 9) flat out vec4`: the projection's frame's turn
 *   into the positions' space, a unit quaternion (x, y, z, w);
 *   (0, 0, 0, 1) where they are one. The triplanar weights and normals read
 *   location 1's normal turned back by it.
 * Location 2 is the texture coordinate UV layers read, times their scale.
 *
 * The same holds for a caller's vertex shader in a pass that renders a
 * shadow map or a depth pre-pass with RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT:
 * it writes locations 0 to 4 as above (the cut-out fragment shader reads the
 * texture coordinate and the color).
 *
 * Positions and directions are in any one space of the
 * caller's choosing - world space, the eye at the origin, any other - and
 * every position, direction and matrix an rbtrend_lit_scene is given is in
 * that same space. A caller whose places are far from its origin draws
 * with the eye at the origin, each place taken from the eye's in its own
 * precision before Rend sees it, and chains an RBTREND_LIT_CASCADE_ANCHOR
 * onto its cascade fits. The push constants are all the caller's; Rend's
 * shaders declare none.
 *
 * @par Colors and light
 * Every color in is linear, and so is all light. A color picked as sRGB (as
 * most are) is the caller's to convert; a texture of colors is read through
 * an `_SRGB` format, which converts as it is sampled. A light's radiance is
 * scaled so that a white, matte face square to it reads the light's color:
 * a directional light at radiance 1 lights a face of albedo 0.5 to 0.5; a point light
 * does so at distance 1, falling off with the square of the distance; a
 * placed directional light (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED) at
 * its reference distance, falling off with the square of the distance from
 * its source, and lighting each point from the direction of its source.
 * What
 * the surface writes is linear radiance times the view's exposure, for a
 * float target and then RBTREND_COMMAND_TONE_MAP (rbtrendcommands.h), or
 * already clamped and sRGB-encoded for an 8-bit UNORM target shown as it is
 * (RBTREND_LIT_VIEW::output_encoding). It is premultiplied by the opacity:
 * blending is the pipeline's, and an opaque surface writes alpha 1.
 *
 * @par Shading
 * Direct light: Cook-Torrance with GGX, Smith-Schlick (k = (r + 1)^2 / 8,
 * for analytic lights) and Schlick's Fresnel, as Unreal's default lit model
 * and Hazel's. A clear coat is a second, smooth GGX lobe of 4% over the base
 * (Filament's, glTF's KHR_materials_clearcoat), a sheen the Charlie
 * distribution (KHR_materials_sheen). Point and spot lights fall off by the
 * inverse square, windowed to 0 at their range (Karis's, Unreal's). Ambient
 * light: the hemisphere and the environment's irradiance for the diffuse
 * part, the hemisphere at the reflection and the environment's prefiltered
 * mips for the specular part, weighted by the split sum's scale and bias
 * from Rend's BRDF lookup texture, as Unreal and Hazel do. A face seen
 * from behind is lit as its front. A scene's cover (rbtrend_lit_scene_set_cover())
 * wets and snows a material that answers it between its material's reads
 * and its lighting: Lagarde's porous wetness, then snow by slope and
 * height; the traced paths (RBTREND_COMMAND_LIT_PATH_TRACE and traced
 * reflections) see every material bare.
 *
 * @par What a compile does for a scene
 * The first compile that binds a lit scene makes the instance's BRDF lookup
 * texture, once; one that binds a scene whose environment has never been
 * filled clears it to black; and one that binds a scene with a target size
 * (RBTREND_LIT_VIEW) culls its lights for each 16-texel tile of the target
 * with a compute pass, so a fragment reads its tile's lights, not all of
 * them. Each is recorded at the top of the list, before anything else.
 *
 * @par Threads
 * The rule is in rbtrendcontext.h ("Threads"). A scene, an environment and a
 * material are each used by one caller at a time with everything else of
 * their instance; every set call writes the object and nothing else. The
 * fitting functions read their arguments and write their results only, so
 * they take no object and may run at once.
 */

/** @brief The set slot of a lit pipeline's scene. */
#define RBTREND_LIT_SCENE_SET 0u
/** @brief The set slot of a lit pipeline's material. */
#define RBTREND_LIT_MATERIAL_SET 1u
/** @brief The most point and spot lights a scene holds: 128. */
#define RBTREND_LIT_MAX_LIGHTS 128u
/** @brief The most directional lights a scene holds, beside its point and spot lights (Hazel's 4). */
#define RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS 4u
/** @brief The most cascades a directional light's shadow has. */
#define RBTREND_LIT_MAX_CASCADES 4u
/**
 * @brief The most views a scene's shadows draw from its atlas, together: a
 * cascade, a spot's map and each of a point light's six faces are one each.
 */
#define RBTREND_LIT_MAX_SHADOW_VIEWS 64u
/** @brief The side of a tile of a view's target that lights are culled for, in texels. */
#define RBTREND_LIT_TILE_SIZE 16u
/** @brief The most lights one tile keeps; beyond them a tile drops the rest, in the order they were set. */
#define RBTREND_LIT_MAX_TILE_LIGHTS 63u

/* ---- Shaders and layouts ------------------------------------------------ */

/**
 * @brief The shaders Rend ships for lit surfaces.
 */
typedef uint32_t RBTREND_LIT_SHADER;
enum {
	/**
	 * The surface, shaded: what the top of this header describes. Reads
	 * locations 0 to 4; its color at 4 multiplies the base color and the
	 * opacity.
	 */
	RBTREND_LIT_SHADER_SURFACE_FRAGMENT = 0,
	/**
	 * A fragment shader that writes nothing, for a pass that renders depth
	 * alone (a shadow map, a depth pre-pass) with no color target: a
	 * pipeline needs a fragment shader. Reads no input.
	 */
	RBTREND_LIT_SHADER_DEPTH_FRAGMENT = 1,
	/**
	 * Meshes of RBTREND_LIT_VERTEX at vertex buffer slot 0, with an
	 * RBTREND_LIT_INSTANCE per instance at slot 1 (rbtrend_get_lit_vertex_layout()
	 * and rbtrend_get_lit_instance_layout() for the pipeline's vertex bindings),
	 * through the view's view_projection; positions out in the space the
	 * instance's transform takes them to. Writes the color at location 4
	 * as (1, 1, 1, 1).
	 */
	RBTREND_LIT_SHADER_SURFACE_VERTEX = 2,
	/**
	 * The same meshes through a shadow pass material's matrix
	 * (rbtrend_lit_get_shadow_layout() at set 0), for a pass that renders a
	 * view of a shadow; it writes locations 0 to 4 (the position, normal and
	 * tangent as 0, the texture coordinate, and the color as white), for
	 * RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT.
	 */
	RBTREND_LIT_SHADER_SHADOW_VERTEX = 3,
	/**
	 * Depth only, as RBTREND_LIT_SHADER_DEPTH_FRAGMENT, for cut-out
	 * materials: a fragment whose opacity (the material's base color alpha,
	 * times the base color map's alpha, times the color's alpha at location
	 * 4) is below the material's alpha_cutoff is discarded, so the holes in
	 * leaves or a perforated screen let light through. Reads the lit
	 * material at set 1 and locations 0 to 4 (it uses 2 and 4).
	 */
	RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT = 4,
	/**
	 * The scene's environment as a background, for a pass over the whole
	 * target with rbtrend_instance_get_fullscreen_vertex_shader() (rbtrendshader.h),
	 * drawn before the surfaces: each texel looks along its ray through the
	 * view's view_projection and reads the environment there, times its
	 * intensity and the exposure, written out as a surface is. Black with
	 * no environment. With directional lights whose `_sky_disc` is true,
	 * each disc is drawn over it at its `_radius`, its edge softened over a
	 * texel. Reads set 0 and needs the view's target size.
	 */
	RBTREND_LIT_SHADER_SKY_FRAGMENT = 5,
	/**
	 * Meshes of RBTREND_LIT_VERTEX at vertex buffer slot 0 with a color per
	 * vertex at slot 1 (rbtrend_get_lit_vertex_color_layout()), and an
	 * RBTREND_LIT_INSTANCE per instance at slot 2: as
	 * RBTREND_LIT_SHADER_SURFACE_VERTEX, but the instance a slot later and
	 * the color at location 4 the vertex's own. A pipeline's vertex bindings
	 * are rbtrend_get_lit_vertex_layout(), rbtrend_get_lit_vertex_color_layout() and
	 * rbtrend_get_lit_instance_layout(), the last per instance; a draw list's
	 * `_instance_slot` is 2.
	 *
	 * Meshes whose colors are per vertex (glTF's COLOR_0, a painted
	 * terrain, a scan) keep them under lit shading.
	 */
	RBTREND_LIT_SHADER_SURFACE_VERTEX_COLOR = 6,
	/**
	 * The shadow pass of the same colored meshes, as
	 * RBTREND_LIT_SHADER_SHADOW_VERTEX with the bindings of
	 * RBTREND_LIT_SHADER_SURFACE_VERTEX_COLOR, handing the color on at
	 * location 4 for RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT. A colored mesh
	 * whose material isn't cut out may instead be drawn with
	 * RBTREND_LIT_SHADER_SHADOW_VERTEX, named by its first vertex buffer
	 * alone in a draw list whose `_instance_slot` is 1.
	 *
	 * A cut-out's holes come from its opacity, the color's alpha included,
	 * and the shadow's are the same holes.
	 */
	RBTREND_LIT_SHADER_SHADOW_VERTEX_COLOR = 7,
	/**
	 * The surface, as RBTREND_LIT_SHADER_SURFACE_FRAGMENT, writing two more
	 * color targets for reflections (rbtrendcommands.h: LIT_SSR,
	 * LIT_TRACE_REFLECTIONS, LIT_REFLECT). At color target 1, the shading
	 * normal (xyz, unit, in the scene's space, the normal map applied) and
	 * the roughness (w, after the ORM map), for an
	 * CRGPU_FORMAT_R16G16B16A16_FLOAT target. At color target 2, the
	 * specular weight the environment's specular light is multiplied by
	 * (the split sum's f0 x scale + bias, times the occlusion, and under a
	 * sheen or a clear coat the share of the light they leave the surface
	 * below them), linear, in rgb of an CRGPU_FORMAT_R8G8B8A8_UNORM
	 * target; its alpha is written 0 and read by nothing. A pipeline with
	 * it has three color targets, target 0 the picture as with
	 * RBTREND_LIT_SHADER_SURFACE_FRAGMENT. A cut-out discards in all three.
	 *
	 * A forward renderer has no G-buffer: a pipeline with this variant pays
	 * for the two targets where a caller reflects, and nowhere else. The
	 * caller clears both to 0 before the surfaces: a normal of 0 is a texel
	 * with no surface, which nothing reflects.
	 */
	RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT = 8,
	/**
	 * The layered surface (@ref rbtrend_lit_layered "Layered materials"):
	 * reads locations 0 to 9 and a layered material at set
	 * RBTREND_LIT_MATERIAL_SET (rbtrend_lit_get_layered_material_layout()),
	 * the scene at set RBTREND_LIT_SCENE_SET as the lit surface does, and
	 * lights the blend with the lit surface's lighting. Opaque: its opacity
	 * is the color's alpha at location 4; no cut-out.
	 */
	RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT = 9,
	/**
	 * The layered surface writing the two reflection targets as
	 * RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT does, from the blended
	 * surface: a pipeline with it has the same three color targets.
	 */
	RBTREND_LIT_SHADER_LAYERED_SURFACE_REFLECT_FRAGMENT = 10,
};

/**
 * @brief One of Rend's lit shaders, as RBTREND_LIT_SHADER describes it. Its
 * entry point is `main`.
 *
 * The shader is the instance's, made on the first call and destroyed with
 * the instance. Don't pass it to rbtrend_shader_destroy().
 *
 * @param[in]  instance   Instance the shader is of.
 * @param[in]  shader     Which.
 * @param[out] out_shader Receives the shader; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_shader is NULL or @p shader is not an RBTREND_LIT_SHADER;
 *         or RBT_RESULT_REND_GPU_FAILURE if it can't be created.
 */
RBT_RESULT rbtrend_lit_get_shader(rbtrend_instance instance, RBTREND_LIT_SHADER shader, rbtrend_shader *out_shader, RBTREND_ERROR *out_error);

/**
 * @brief The layout of set RBTREND_LIT_SCENE_SET, for slot 0 of a lit
 * pipeline: what an rbtrend_lit_scene fills. Made from every one of Rend's
 * shaders that read it - the surface, its two vertex shaders and the sky -
 * so one layout serves them all.
 *
 * The instance's, as rbtrend_lit_get_shader()'s are: don't destroy it.
 *
 * @param[in]  instance   Instance the layout is of.
 * @param[out] out_layout Receives the layout; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_layout is NULL; or RBT_RESULT_REND_GPU_FAILURE if it can't
 *         be made.
 */
RBT_RESULT rbtrend_lit_get_scene_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error);

/**
 * @brief The layout of set 0 of a pass that renders a view of a shadow
 * with RBTREND_LIT_SHADER_SHADOW_VERTEX or
 * RBTREND_LIT_SHADER_SHADOW_VERTEX_COLOR (both read it): one matrix, the one
 * the view is rendered through. Filled by a scene's shadow pass materials
 * (rbtrend_lit_scene_get_shadow_pass_material()). The instance's: don't
 * destroy it.
 *
 * @param[in]  instance   Instance the layout is of.
 * @param[out] out_layout Receives the layout; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_layout is NULL; or RBT_RESULT_REND_GPU_FAILURE if it can't
 *         be made.
 */
RBT_RESULT rbtrend_lit_get_shadow_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error);

/**
 * @brief The layout of set RBTREND_LIT_MATERIAL_SET, for slot 1 of a lit
 * pipeline (and of a cut-out shadow pass): what a lit material fills. The
 * instance's: don't destroy it.
 *
 * @param[in]  instance   Instance the layout is of.
 * @param[out] out_layout Receives the layout; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_layout is NULL; or RBT_RESULT_REND_GPU_FAILURE if it can't
 *         be made.
 */
RBT_RESULT rbtrend_lit_get_material_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error);

/**
 * @brief One vertex of a mesh for Rend's lit vertex shaders: 48 bytes,
 * tightly packed.
 */
/* Frozen: a vertex in GPU memory, laid out as the lit shaders read it. It never changes; a new need gets a new type. */
typedef struct RBTREND_LIT_VERTEX {
	/** The position, in the mesh's own space. */
	float position[3];
	/** The normal, in the mesh's own space. */
	float normal[3];
	/** The texture coordinate, v down the image (its first row at 0). */
	float uv[2];
	/** xyz along u, w +1 or -1 for which way v runs; all 0 for none. */
	float tangent[4];
} RBTREND_LIT_VERTEX;

/**
 * @brief One instance for Rend's lit vertex shaders: a draw list's
 * instance bytes (rbtrenddrawlist.h, a stride of 64), or a vertex buffer that
 * steps per instance.
 */
/* Frozen: an instance in GPU memory, laid out as the lit shaders read it. It never changes; a new need gets a new type. */
typedef struct RBTREND_LIT_INSTANCE {
	/**
	 * The mesh's transform into the space the scene is in, column after
	 * column. Normals go through its inverse transpose, so a scale that
	 * isn't the same along every axis, or a mirror, keeps them square.
	 */
	float transform[16];
} RBTREND_LIT_INSTANCE;

/**
 * @brief The vertex layout of RBTREND_LIT_VERTEX, for vertex binding 0 of a
 * pipeline with Rend's lit vertex shaders. Its elements are Rend's own,
 * static, so the layout may be kept as long as anything likes.
 *
 * @return The layout: four elements, a stride of 48.
 */
RBTREND_VERTEX_LAYOUT rbtrend_get_lit_vertex_layout(void);

/**
 * @brief The vertex layout of RBTREND_LIT_INSTANCE (four VEC4 columns), for
 * vertex binding 1, per instance. Static, as rbtrend_get_lit_vertex_layout()'s.
 *
 * @return The layout: four elements, a stride of 64.
 */
RBTREND_VERTEX_LAYOUT rbtrend_get_lit_instance_layout(void);

/**
 * @brief The vertex layout of a per-vertex color, for vertex binding 1 of a
 * pipeline with RBTREND_LIT_SHADER_SURFACE_VERTEX_COLOR or
 * RBTREND_LIT_SHADER_SHADOW_VERTEX_COLOR: one RBTREND_VERTEX_ELEMENT_TYPE_VEC4
 * element named `color` at offset 0, linear RGBA. Static, as
 * rbtrend_get_lit_vertex_layout()'s, so it may be kept as long as anything likes.
 *
 * A caller whose colors are interleaved in a buffer of its own gives its
 * own layout instead: one VEC4 element at its offset, at its stride.
 *
 * @return The layout: one element, a stride of 16.
 */
RBTREND_VERTEX_LAYOUT rbtrend_get_lit_vertex_color_layout(void);

/* ---- Materials ---------------------------------------------------------- */

/**
 * @brief A lit material's values: the metal/roughness parameters Unreal's
 * default lit shading takes and glTF's are, each multiplied by its map, and
 * glTF's clear coat and sheen.
 */
typedef struct RBTREND_LIT_MATERIAL_VALUES {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_MATERIAL_VALUES, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** rgb: the surface's color, linear: what a non-metal scatters and a
	 * metal reflects. a: its opacity, 0 to 1. Times the base color map. */
	float base_color[4];
	/** Light the surface gives off of itself, linear, times the emission map. */
	float emission[3];
	/** 0 (polished) to 1 (matte), as perceived; times the ORM map's green.
	 * Below 0.045 it is taken as 0.045. */
	float roughness;
	/** 0 (a dielectric) to 1 (a metal); times the ORM map's blue. */
	float metalness;
	/** A dielectric's reflectance, 0 to 1: 0.5 is 4% head on, which suits
	 * most materials. A metal's comes from its base color. Unreal's Specular. */
	float specular;
	/** How strongly the normal map bends the normal: 1 as the map is, 0 not at all. */
	float normal_scale;
	/** How much of the ORM map's red (ambient occlusion) is applied, 0 to 1. */
	float occlusion_strength;
	/**
	 * 0 for a surface blended by its opacity. Above 0, a cut-out: a
	 * fragment whose opacity is below this is discarded and the rest are
	 * opaque (glTF's MASK), in its shadows too when they are rendered with
	 * RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT.
	 */
	float alpha_cutoff;
	/**
	 * The texture coordinate's transform before any map is read, as two
	 * rows of a 2x3 matrix: u' = [0]u + [1]v + [2], v' = [3]u + [4]v + [5].
	 * Identity is {1, 0, 0, 0, 1, 0}: what a texture's size in the world
	 * comes to, for a caller whose coordinates are distances. Each map has a
	 * transform of its own after it (rbtrend_lit_material_set_map_uv_transform()).
	 */
	float uv_transform[6];
	/** A clear coat over the surface, 0 (none) to 1: lacquer, car paint, a
	 * varnished floor. It reflects 4% head on, on the face's own normal. */
	float clearcoat;
	/** The coat's roughness, as #roughness. */
	float clearcoat_roughness;
	/** A sheen's color, linear, 0 for none: the soft rim of fabric and
	 * velvet. */
	float sheen_color[3];
	/** The sheen's roughness, 0.07 to 1. */
	float sheen_roughness;
	/** How much light passes through the surface instead of scattering
	 * back, 0 (none) to 1: glTF's KHR_materials_transmission. That share
	 * of the diffuse light is replaced by the opaque picture behind, bent
	 * by #ior through #thickness and blurred by the roughness, read from
	 * the scene's color (rbtrend_lit_scene_set_scene_color()); without one,
	 * nothing passes. A metal transmits nothing. What is behind other
	 * transmitting surfaces, or off the screen, doesn't show. */
	float transmission;
	/** The index of refraction, from 1 (no bending): 1.5 for glass, 1.33
	 * for water (glTF's KHR_materials_ior). Below 1 is taken as 1. */
	float ior;
	/** How thick the surface is, in the scene's units: how far the bent
	 * view ray travels inside it, so how far what is behind seems to move,
	 * and how far light travels for #attenuation_distance (glTF's
	 * KHR_materials_volume). 0 for a thin sheet, which shifts nothing. */
	float thickness;
	/** The color light passing through is tinted to after
	 * #attenuation_distance, linear. */
	float attenuation_color[3];
	/** How far light travels inside before it is tinted to
	 * #attenuation_color (Beer's law); 0 for never. */
	float attenuation_distance;
} RBTREND_LIT_MATERIAL_VALUES;

/**
 * @brief The maps of a lit material.
 */
typedef uint32_t RBTREND_LIT_MAP;
enum {
	/** rgb times the base color, a times the opacity: read through an `_SRGB`
	 * format for colors stored as sRGB. White where none is given. */
	RBTREND_LIT_MAP_BASE_COLOR = 0,
	/** A tangent-space normal map, stored as 0 to 1 (z up). Flat where none
	 * is given. */
	RBTREND_LIT_MAP_NORMAL = 1,
	/** Ambient occlusion in red, roughness in green, metalness in blue: glTF's
	 * and Unreal's packing. White where none is given. */
	RBTREND_LIT_MAP_ORM = 2,
	/** rgb times the emission. White where none is given. */
	RBTREND_LIT_MAP_EMISSION = 3,
};

/**
 * @brief Creates a lit material: an rbtrend_material (rbtrendmaterial.h) of
 * rbtrend_lit_get_material_layout(), bound at set RBTREND_LIT_MATERIAL_SET.
 *
 * It starts white, opaque, half rough (0.5), a dielectric of specular 0.5,
 * with no emission, normal scale 1, occlusion strength 1, no cut-out, the
 * identity uv transform (the material's and each map's), no clear coat and
 * no sheen; every map is Rend's
 * neutral one, read through Rend's sampler (linear, mipmapped,
 * repeating). It is an rbtrend_material in every other respect: destroyed
 * with rbtrend_material_destroy(), and its values may also be set one by one
 * by their names in the block (`base_color`, `roughness`, ...) with
 * rbtrend_material_set_uniform().
 *
 * It also takes an entry in the instance's lit-material table, which it
 * keeps for its life: one buffer of every lit material's values that a
 * path trace reads (RBTREND_COMMAND_LIT_PATH_TRACE, rbtrendcommands.h), the
 * entry named from a TLAS's material table (rbtrendraytracing.h). The table
 * grows by doubling when it is full, an allocation then and only then.
 * Destroying the material frees the entry, its generation advanced, once
 * the frames using it have completed.
 *
 * @param[in]  instance     Instance the material is of.
 * @param[out] out_material Receives the material; NULL on failure, with no
 *                          table entry taken.
 * @param[out] out_error    Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_material is NULL; RBT_RESULT_LIMIT if the lit-material table
 *         holds 2^24 materials; or the code it can't be made with.
 */
RBT_RESULT rbtrend_lit_material_create(rbtrend_instance instance, rbtrend_material *out_material, RBTREND_ERROR *out_error);

/**
 * @brief Sets every value of a lit material at once.
 *
 * The values reach a path trace through the material's entry in the
 * instance's lit-material table (rbtrend_lit_material_create()): staged
 * here, and copied into the table in the next submission, ahead of what
 * reads it, so a frame already submitted reads the values it was submitted
 * with.
 *
 * An RBTREND_LIT_COVER_RESPONSE may be chained on: how the material
 * answers its scene's cover. Without one the material takes no cover, so
 * setting values with none takes a response set before away.
 *
 * @param[in] material Material to change.
 * @param[in] values   The values, and a response chained on or none. Only
 *                     read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if an argument is NULL, @p material is not of
 *         rbtrend_lit_get_material_layout(), or a value is not finite;
 *         RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN for a struct chained on other
 *         than a response and RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN for two
 *         responses, checked with `values->base`, before any value; and
 *         RBT_RESULT_INVALID_ARGUMENT for a response's field out of its
 *         range, after the values', each its own check in member order.
 */
RBT_RESULT rbtrend_lit_material_set_values(rbtrend_material material, const RBTREND_LIT_MATERIAL_VALUES *values, RBTREND_ERROR *out_error);

/**
 * @brief Reads a lit material's values back.
 *
 * An RBTREND_LIT_COVER_RESPONSE chained onto @p out_values by the caller
 * (its `base.type` set) receives the material's response; the chain is
 * left as the caller made it. With nothing chained, `base.next` is set to
 * NULL.
 *
 * @param[in]  material   Material to read.
 * @param[out] out_values Receives the values.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         written, if an argument is NULL or @p material is not of
 *         rbtrend_lit_get_material_layout(); then the struct codes for a
 *         chain holding other than one RBTREND_LIT_COVER_RESPONSE, and
 *         RBT_RESULT_INVALID_ARGUMENT where a response is chained on and the
 *         material has none (it takes no cover).
 */
RBT_RESULT rbtrend_lit_material_get_values(rbtrend_material material, RBTREND_LIT_MATERIAL_VALUES *out_values, RBTREND_ERROR *out_error);

/**
 * @brief Puts a texture in one of a lit material's maps, or puts back
 * Rend's neutral one.
 *
 * @param[in] material Material to change.
 * @param[in] map      Which map.
 * @param[in] view     A SHADER_RESOURCE view of a 2D texture of a format a
 *                     shader reads as floats, or NULL for the neutral map. It
 *                     must outlive the material, or be replaced before the
 *                     next compile after it goes.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if @p material is NULL or not of
 *         rbtrend_lit_get_material_layout(), @p map is not an RBTREND_LIT_MAP,
 *         or @p view is of the wrong type or instance.
 */
RBT_RESULT rbtrend_lit_material_set_map(rbtrend_material material, RBTREND_LIT_MAP map, rbtrend_texture_view view, RBTREND_ERROR *out_error);

/**
 * @brief Sets the transform of one map's texture coordinate, applied after
 * the material's own (RBTREND_LIT_MATERIAL_VALUES::uv_transform), so each
 * map can be tiled, moved or turned on its own: glTF's
 * KHR_texture_transform, whose offset, rotation and scale make the matrix
 * that extension gives, and which the caller writes here.
 *
 * Every map's starts as the identity. rbtrend_lit_material_set_values()
 * leaves them as they are; rbtrend_material_copy() copies them.
 *
 * @param[in] material  Material to change.
 * @param[in] map       Which map.
 * @param[in] transform Two rows of a 2x3 matrix, as `uv_transform`'s:
 *                      u' = [0]u + [1]v + [2], v' = [3]u + [4]v + [5].
 *                      Identity is {1, 0, 0, 0, 1, 0}. Only read during the
 *                      call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if @p material is NULL or not of
 *         rbtrend_lit_get_material_layout(), @p map is not an RBTREND_LIT_MAP,
 *         @p transform is NULL, or a value of it is not finite.
 */
RBT_RESULT rbtrend_lit_material_set_map_uv_transform(rbtrend_material material, RBTREND_LIT_MAP map, const float transform[6], RBTREND_ERROR *out_error);

/**
 * @brief Reads the transform of one map's texture coordinate back.
 *
 * @param[in]  material      Material to read.
 * @param[in]  map           Which map.
 * @param[out] out_transform Receives the two rows, as
 *                           rbtrend_lit_material_set_map_uv_transform() takes
 *                           them.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         written, if @p material is NULL or not of
 *         rbtrend_lit_get_material_layout(), @p map is not an RBTREND_LIT_MAP,
 *         or @p out_transform is NULL.
 */
RBT_RESULT rbtrend_lit_material_get_map_uv_transform(rbtrend_material material, RBTREND_LIT_MAP map, float out_transform[6], RBTREND_ERROR *out_error);

/**
 * @brief Puts the sampler every map of a lit material is read through, or
 * puts back Rend's (linear, mipmapped, repeating).
 *
 * @param[in] material Material to change.
 * @param[in] sampler  The sampler, or NULL for Rend's. It must outlive
 *                     the material, or be replaced before the next compile
 *                     after it goes.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if @p material is NULL or not of
 *         rbtrend_lit_get_material_layout(), or @p sampler is of another
 *         instance.
 */
RBT_RESULT rbtrend_lit_material_set_sampler(rbtrend_material material, rbtrend_sampler sampler, RBTREND_ERROR *out_error);

/* ---- Layered materials -------------------------------------------------- */

/**
 * @anchor rbtrend_lit_layered
 * @par Layered materials
 * A surface of several materials at once, in proportions that change
 * across it (a terrain's grass, sand, rock and snow; a site's gravel and
 * paving; a wall with moss painted in): a layered material, bound at set
 * RBTREND_LIT_MATERIAL_SET of a pipeline with
 * RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT, holds up to
 * RBTREND_LIT_LAYERED_MAX_LAYERS layers' values and three 2D texture
 * arrays (base color, normal, ORMH), layer i of each map at array layer i.
 * The caller's vertex shader hands each pixel the weights of eight layers
 * (locations 5 to 7) and the triplanar inputs (8 and 9). Each pixel:
 * 0. takes the screen-space gradients of the texture coordinate and the
 *    triplanar place once, before any branch; every read below samples
 *    with them (so which layers neighbouring pixels read never picks a
 *    wrong mip along their edge);
 * 1. keeps the RBTREND_LIT_LAYERED_PIXEL_LAYERS largest weights above 0
 *    that name a layer (ties to the lower entry), renormalised to sum 1;
 *    with none it reads layer idx[0] whole, or layer 0 where that is none;
 * 2. reads each kept layer's ORMH (h its alpha, c its height_contrast),
 *    a = w + c h, and with the material's blend depth d,
 *    w' = w x max(a - max(a) + d, 0), renormalised (Mishkinis's band, 2013,
 *    times the weight as Unity HDRP's height transition): at d = 0 only the
 *    largest a, whole (ties to the lower entry); as d grows, the weights
 *    themselves;
 * 3. reads each layer with w' above 0 by its projection: UV at the texture
 *    coordinate times uv_scale, its normal through the tangent frame;
 *    TRIPLANAR on the three planes of the triplanar place (q.zy, q.xz,
 *    q.xy) times uv_scale, weighted by the projection frame's normal to the
 *    fourth power, each plane's normal by the whiteout blend (Golus, 2017)
 *    turned into the positions' space by location 9's quaternion;
 * 4. blends base color, occlusion, roughness, metalness, specular and each
 *    layer's normal (in the positions' space, its normal_scale applied) by
 *    w', the normal normalised, times location 4's color; and each layer's
 *    RBTREND_LIT_COVER_RESPONSE, chained on its values, by w' (a layer with
 *    none answers none of the scene's cover);
 * 5. lights the blend with the lit surface's lighting.
 * A layer has no clear coat, sheen, transmission or cut-out. A layered
 * material takes no entry in the instance's lit-material table, so a traced
 * path can't name it (rbtrend_tlas refuses it). Reads a pixel: a UV layer
 * 3, a TRIPLANAR one 9, at most four layers.
 */

/** @brief The most layers a layered material has: a layer's index is a byte of location 7, its values a row of the block. */
#define RBTREND_LIT_LAYERED_MAX_LAYERS 16u
/** @brief The layers' weights (locations 5 and 6) and indices (location 7) a fragment receives. */
#define RBTREND_LIT_LAYERED_INPUT_LAYERS 8u
/** @brief The most layers a pixel blends: its heaviest. */
#define RBTREND_LIT_LAYERED_PIXEL_LAYERS 4u
/** @brief The index at location 7 that names no layer. */
#define RBTREND_LIT_LAYERED_NO_LAYER 0xFFu

/**
 * @brief The layout of set RBTREND_LIT_MATERIAL_SET of a layered pipeline:
 * what a layered material fills (its block at binding 0, the base color,
 * normal and ORMH arrays at 1 to 3, a sampler at 4). The instance's: don't
 * destroy it.
 *
 * @param[in]  instance   Instance the layout is of.
 * @param[out] out_layout Receives the layout; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance or
 *         @p out_layout is NULL; or RBT_RESULT_REND_GPU_FAILURE if it can't
 *         be made.
 */
RBT_RESULT rbtrend_lit_get_layered_material_layout(rbtrend_instance instance, rbtrend_binding_layout *out_layout, RBTREND_ERROR *out_error);

/** @brief The maps of a layered material, each a 2D array whose layer i is layer i's. */
typedef uint32_t RBTREND_LIT_LAYERED_MAP;
enum {
	/** rgb times the layer's base color; alpha not read. An `_SRGB` format
	 * for colors stored as sRGB. White where none is given. */
	RBTREND_LIT_LAYERED_MAP_BASE_COLOR = 0,
	/** A tangent-space normal map, stored 0 to 1 (z up). Flat where none. */
	RBTREND_LIT_LAYERED_MAP_NORMAL = 1,
	/** Occlusion in red, roughness in green, metalness in blue (as
	 * RBTREND_LIT_MAP_ORM), the layer's height in alpha, 0 to 1. (1, 1, 1,
	 * 128/255) where none: the byte an 8-bit source holds for 0.5. */
	RBTREND_LIT_LAYERED_MAP_ORMH = 2,
};

/** @brief How a layer's maps are read. */
typedef uint32_t RBTREND_LIT_LAYER_PROJECTION;
enum {
	/** Refused. */
	RBTREND_LIT_LAYER_PROJECTION_NONE = 0,
	/** At the texture coordinate (location 2) times `uv_scale`. */
	RBTREND_LIT_LAYER_PROJECTION_UV = 1,
	/** On three planes of the triplanar place (location 8) times `uv_scale`. */
	RBTREND_LIT_LAYER_PROJECTION_TRIPLANAR = 2,
};

/**
 * @brief A layer's values, each multiplying its maps as a lit material's
 * do. An RBTREND_LIT_COVER_RESPONSE may be chained on: how the layer
 * answers its scene's cover (none: it takes none).
 */
typedef struct RBTREND_LIT_LAYER_VALUES {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Linear, each 0 to 1, times the base color map. */
	float base_color[3];
	/** 0 to 1, times the ORMH map's green; below 0.045 taken as 0.045. */
	float roughness;
	/** 0 to 1, times its blue. */
	float metalness;
	/** A dielectric's reflectance, 0 to 1 (0.5 is 4%). */
	float specular;
	/** How strongly the normal map bends the normal: finite, at least 0; 1 as the map is. */
	float normal_scale;
	/** How much of the ORMH map's red is applied, 0 to 1. */
	float occlusion_strength;
	/** How much the height (alpha) shifts the layer's share: 0 to 4. */
	float height_contrast;
	/** Repeats a unit of the texture coordinate or triplanar place: finite,
	 * above 0 (1 / a tile's size, for a caller whose units are metres). */
	float uv_scale;
	/** An RBTREND_LIT_LAYER_PROJECTION, not NONE. */
	RBTREND_LIT_LAYER_PROJECTION projection;
} RBTREND_LIT_LAYER_VALUES;

/**
 * @brief Creates a layered material: an rbtrend_material of
 * rbtrend_lit_get_layered_material_layout(), bound at set
 * RBTREND_LIT_MATERIAL_SET of a layered pipeline, destroyed with
 * rbtrend_material_destroy().
 *
 * Every layer starts white, half rough (0.5), a dielectric of specular 0.5,
 * normal scale 1, occlusion strength 1, height contrast 0, uv_scale 1,
 * RBTREND_LIT_LAYER_PROJECTION_UV, with no cover response; the blend depth
 * 0.2; every map the instance's neutral array (1 x 1, sixteen layers, made
 * by the instance's first layered create and filled by the first compile
 * that binds a layered material); Rend's sampler (linear, mipmapped,
 * repeating). Its block, 784 bytes whatever the layer count, is allocated
 * here, and nothing after. It takes no entry in the lit-material table.
 *
 * @param[in]  instance     Instance the material is of.
 * @param[in]  layer_count  1 to RBTREND_LIT_LAYERED_MAX_LAYERS.
 * @param[out] out_material Receives the material; NULL on failure.
 * @param[out] out_error    Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance is NULL,
 *         @p layer_count is 0 or past RBTREND_LIT_LAYERED_MAX_LAYERS, or
 *         @p out_material is NULL (each its own check); or the code it
 *         can't be made with.
 */
RBT_RESULT rbtrend_lit_layered_material_create(rbtrend_instance instance, uint32_t layer_count, rbtrend_material *out_material, RBTREND_ERROR *out_error);

/**
 * @brief Sets one layer's values, staged in the material's block and
 * copied in the next submission, as rbtrend_lit_material_set_values()'s
 * are: a frame already submitted reads what it was submitted with.
 *
 * @param[in] material Material to change.
 * @param[in] layer    Below the material's layer count.
 * @param[in] values   The values, based (RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES),
 *                     an RBTREND_LIT_COVER_RESPONSE chained on or none. Only
 *                     read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise, with nothing changed, each its own
 *         check in this order: RBT_RESULT_INVALID_ARGUMENT if @p material
 *         is NULL or not of the layered layout, @p layer is out of range,
 *         or @p values is NULL; the struct codes for `values->base`
 *         (RBT_RESULT_STRUCT_WRONG_TYPE, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN
 *         for a struct chained on other than a response,
 *         RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN for two); then
 *         RBT_RESULT_INVALID_ARGUMENT for a field not finite or out of its
 *         range, in declaration order, the response's after, the message
 *         "layer <n>: <field> is <value>; it must be <range>".
 */
RBT_RESULT rbtrend_lit_layered_material_set_layer(rbtrend_material material, uint32_t layer, const RBTREND_LIT_LAYER_VALUES *values, RBTREND_ERROR *out_error);

/**
 * @brief Reads one layer's values back: the last set, staged or not, as
 * rbtrend_lit_material_get_values() does for a lit material.
 *
 * An RBTREND_LIT_COVER_RESPONSE chained onto @p out_values by the caller
 * receives the layer's response; the chain is left as the caller made it.
 *
 * @param[in]  material   Material to read: of the layered layout.
 * @param[in]  layer      Below the material's layer count.
 * @param[out] out_values Receives the values; its base set by the caller
 *                        (RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES), a
 *                        response chained on or none.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise, with nothing written, each its own
 *         check in this order: RBT_RESULT_INVALID_ARGUMENT if @p material
 *         is NULL or not of the layered layout, @p layer is out of range,
 *         or @p out_values is NULL; the struct codes for its base and
 *         chain; RBT_RESULT_INVALID_ARGUMENT where a response is chained on
 *         and the layer has none.
 */
RBT_RESULT rbtrend_lit_layered_material_get_layer(rbtrend_material material, uint32_t layer, RBTREND_LIT_LAYER_VALUES *out_values, RBTREND_ERROR *out_error);

/**
 * @brief Sets the blend depth d (@ref rbtrend_lit_layered "Layered
 * materials", step 2), in the units of a layer's weight plus its contrast
 * times its height: 0 is a hard edge where those cross (only the highest
 * shows); 0.1 to 0.3 the usual height blend; as d grows the blend tends to
 * the weights alone. The material's, one for the blend. Staged as
 * rbtrend_lit_layered_material_set_layer()'s values are.
 *
 * @param[in]  material  Material to change: of the layered layout.
 * @param[in]  depth     Finite, at least 0.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT, nothing changed, for a
 *         NULL @p material, one not of the layered layout, a depth not
 *         finite, or a depth below 0 (each its own check and message).
 */
RBT_RESULT rbtrend_lit_layered_material_set_blend_depth(rbtrend_material material, float depth, RBTREND_ERROR *out_error);

/**
 * @brief Puts a texture array in one of a layered material's maps, or puts
 * back Rend's neutral one.
 *
 * @param[in] material Material to change.
 * @param[in] map      Which map.
 * @param[in] view     A SHADER_RESOURCE view of shape
 *                     RBTREND_TEXTURE_VIEW_SHAPE_2D_ARRAY of color, of at
 *                     least the material's layer count of layers, in a
 *                     format a shader reads as floats; or NULL for the
 *                     neutral array. It must outlive the material, or be
 *                     replaced before the next compile after it goes.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT, nothing
 *         changed, if @p material is NULL or not layered, @p map is not an
 *         RBTREND_LIT_LAYERED_MAP, or @p view is of another instance, not a
 *         SHADER_RESOURCE view of color, not a 2D array, or has too few
 *         layers (each its own check and message).
 */
RBT_RESULT rbtrend_lit_layered_material_set_map(rbtrend_material material, RBTREND_LIT_LAYERED_MAP map, rbtrend_texture_view view, RBTREND_ERROR *out_error);

/**
 * @brief Puts the sampler every map is read through, or puts back Rend's,
 * as rbtrend_lit_material_set_sampler(). A caller wanting anisotropy
 * (ground at grazing angles) gives a sampler made with it, on a device
 * that takes it: until Carrottus GPU reports a device's anisotropic
 * filtering, a level above 1 is outside Vulkan's rules.
 *
 * @param[in]  material  Material to change: of the layered layout.
 * @param[in]  sampler   A sampler of the material's instance, or NULL for
 *                       Rend's (linear, mipmapped, repeating). It must
 *                       outlive the material, or be replaced before the
 *                       next compile after it goes.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT, nothing changed, for a
 *         NULL @p material, one not of the layered layout, or a sampler of
 *         another instance (each its own check and message).
 *
 * @warning The sampler is kept, not copied: destroying it while the
 *          material may still be compiled into a frame is the caller's
 *          error, as for rbtrend_lit_layered_material_set_map()'s view.
 */
RBT_RESULT rbtrend_lit_layered_material_set_sampler(rbtrend_material material, rbtrend_sampler sampler, RBTREND_ERROR *out_error);

/* ---- Environments -------------------------------------------------------- */

/**
 * @brief Opaque handle to an environment: the light all round a scene,
 * prefiltered for shading - its irradiance for diffuse light, and a chain of
 * mips each blurred for a roughness for specular light.
 *
 * An environment is filled in a command list: from a picture with
 * RBTREND_COMMAND_LIT_ENVIRONMENT (a cube or an equirectangular image), or
 * from a sky with RBTREND_COMMAND_LIT_SKY (rbtrendcommands.h); filling it
 * again (a sky that changes) is the same command again. It is used by
 * naming it in an rbtrend_lit_scene's ambient light. The handle is declared
 * in rbtrendcommands.h, beside those commands.
 */

/**
 * @brief Configuration descriptor for an environment. No field has a
 * default.
 */
typedef struct RBTREND_LIT_ENVIRONMENT_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_ENVIRONMENT_DESC, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/**
	 * The width of a face of the specular chain's first mip, which is the
	 * picture itself, for mirrors: a power of 2 from 16 to 2048. 256 suits
	 * most uses. The chain runs down to 8 texels a face.
	 */
	uint32_t specular_size;
	/** The width of a face of the irradiance cube: a power of 2 from 8 to 128.
	 * Irradiance is smooth, so 32 is plenty. */
	uint32_t irradiance_size;
} RBTREND_LIT_ENVIRONMENT_DESC;

/**
 * @brief Creates an environment, black until it is filled.
 *
 * @param[in]  instance        Instance the environment is of.
 * @param[in]  desc            The environment's sizes. Only read during the
 *                             call.
 * @param[out] out_environment Receives the environment; NULL on failure.
 * @param[out] out_error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if an argument is NULL or a
 *         size is not a power of 2 in its range; the struct codes for
 *         `desc->base`; RBT_RESULT_OUT_OF_MEMORY; or
 *         RBT_RESULT_REND_GPU_FAILURE if a texture can't be created.
 */
RBT_RESULT rbtrend_lit_environment_create(
    rbtrend_instance instance,
    const RBTREND_LIT_ENVIRONMENT_DESC *desc,
    rbtrend_lit_environment *out_environment,
    RBTREND_ERROR *out_error);

/**
 * @brief Destroys an environment. The GPU doesn't have to be finished with
 * it (rbtrendcontext.h, "Destroying"); no scene may name it afterwards.
 *
 * Thread: may overlap calls on other handles of its instance, from any
 * thread; no call may be using @p environment itself meanwhile (rbtrendcontext.h,
 * "Threads" and "Destroying").
 *
 * @param[in] environment Environment to destroy. NULL is accepted and does
 *                        nothing.
 */
void rbtrend_lit_environment_destroy(rbtrend_lit_environment environment);

/**
 * @brief How many mips an environment's specular chain has: the roughness of
 * mip m is m / (count - 1).
 *
 * @param[in] environment Environment to read.
 *
 * @return The count, or 0 if @p environment is NULL.
 */
uint32_t rbtrend_lit_environment_get_specular_mip_count(rbtrend_lit_environment environment);

/* ---- Scenes ------------------------------------------------------------- */

/**
 * @brief Opaque handle to a scene: what one view of the world shades with.
 *
 * A scene is a value store, not a world: it holds the view, the lights and
 * their shadows, the shadow atlas, the ambient light and the ambient
 * occlusion as they were last set, and is drawn with by binding
 * its material (rbtrend_lit_scene_get_material()) at set
 * RBTREND_LIT_SCENE_SET. Values set take effect as an rbtrend_material's do
 * (rbtrendmaterial.h, "When uniform values take effect"): at the next
 * submission of a list that binds it, and kept until set again. One scene
 * serves every pass of a view; two views of one world (two viewports) are
 * two scenes.
 */
typedef struct rbtrend_lit_scene_t *rbtrend_lit_scene;

/**
 * @brief The most records a scene keeps of the binding sets its commands
 * make over views (RBTREND_LIT_SCENE_DESC::max_view_sets): a set is found by
 * a scan of the records at each command that takes one, so this bounds a
 * scan and what a scene allocates when it is made.
 */
#define RBTREND_LIT_SCENE_MAX_VIEW_SETS 256u

/**
 * @brief How a lit scene is created (rbtrend_lit_scene_create()).
 */
typedef struct RBTREND_LIT_SCENE_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC, and its
	 * chain, which takes nothing today (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/**
	 * How many binding sets over views the scene keeps, for the commands
	 * that name it and make one: RBTREND_COMMAND_LIT_SSR,
	 * _LIT_TRACE_REFLECTIONS, _LIT_REFLECT, _LIT_ATMOSPHERE_SHELLS, _LIT_FOG
	 * and _LIT_CLOUDS
	 * (rbtrendcommands.h says which sets each takes). 0 to
	 * RBTREND_LIT_SCENE_MAX_VIEW_SETS.
	 *
	 * A set is kept and handed out again to a later compile that names the
	 * same views, so a steady frame makes none. A compile that needs a set
	 * while every record holds one bound by it or by a list compiled and
	 * not yet submitted is refused with RBT_RESULT_LIMIT, naming this
	 * field; otherwise a record not so held takes the new set (an empty
	 * one, else the one handed out least recently), its old set destroyed.
	 *
	 * 0 is a scene with no records: those six commands naming it are
	 * refused at the compile, with RBT_RESULT_LIMIT naming this field; it
	 * draws lit surfaces, skies and every other command as any scene.
	 *
	 * Costs 120 bytes a record, allocated by rbtrend_lit_scene_create().
	 * Each set made costs the descriptors of its layout (3 to 7) from the
	 * device's crgpu_device_desc::max_descriptors, and its sampler from
	 * max_samplers, until the record takes another or the scene is
	 * destroyed.
	 */
	uint32_t max_view_sets;
} RBTREND_LIT_SCENE_DESC;

/**
 * @brief Creates a scene. It starts with exposure 1 and LINEAR output and
 * everything else 0: no view, no lights, no shadow atlas, no ambient light,
 * no shadows, so a surface drawn with it is black but for its emission.
 *
 * It makes, with its own material, RBTREND_LIT_MAX_SHADOW_VIEWS shadow pass
 * materials (rbtrend_lit_scene_get_shadow_pass_material()), handed out to
 * shadows' views as they are set, and @p desc's `max_view_sets` records of
 * the sets its commands make over views (RBTREND_LIT_SCENE_DESC): what
 * setting a shadow or compiling a command needs is allocated here, once.
 *
 * @param[in]  instance  Instance the scene is of.
 * @param[in]  desc      How it is made. Copied: only read during the call.
 * @param[out] out_scene Receives the scene; NULL on failure.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance is
 *         NULL or destroyed, @p desc is NULL, `max_view_sets` is above
 *         RBTREND_LIT_SCENE_MAX_VIEW_SETS, or @p out_scene is NULL;
 *         RBT_RESULT_STRUCT_WRONG_TYPE or RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN
 *         for `desc->base`; RBT_RESULT_OUT_OF_MEMORY; or
 *         RBT_RESULT_REND_GPU_FAILURE if what it is drawn with can't be made.
 */
RBT_RESULT rbtrend_lit_scene_create(
	rbtrend_instance instance,
	const RBTREND_LIT_SCENE_DESC *desc,
	rbtrend_lit_scene *out_scene,
	RBTREND_ERROR *out_error);

/**
 * @brief Destroys a scene, its materials and the sets its records hold.
 * What it names (shadow maps, an environment, an occlusion texture) is the
 * caller's. The GPU doesn't have to be finished with it.
 *
 * Thread: may overlap calls on other handles of its instance, from any
 * thread; no call may be using @p scene itself meanwhile (rbtrendcontext.h,
 * "Threads" and "Destroying").
 *
 * @param[in] scene Scene to destroy. NULL is accepted and does nothing.
 */
void rbtrend_lit_scene_destroy(rbtrend_lit_scene scene);

/**
 * @brief The material a scene is drawn with, for RBTREND_COMMAND_SET_MATERIAL
 * at slot RBTREND_LIT_SCENE_SET. The scene's: set it through the scene's
 * calls, not the material's, and don't destroy it.
 *
 * @param[in] scene Scene to read.
 *
 * @return The material, or NULL if @p scene is NULL.
 */
rbtrend_material rbtrend_lit_scene_get_material(rbtrend_lit_scene scene);

/**
 * @brief Where a scene is seen from.
 */
typedef struct RBTREND_LIT_VIEW {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_VIEW, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Where the eye is: what view-dependent shading (highlights, Fresnel)
	 * is worked out from. */
	float eye_position[3];
	/** Which way the view looks: what directional lights' cascades are
	 * measured along. Any length; 0 only for a scene with no cascades. */
	float view_direction[3];
	/**
	 * Positions to clip space, column after column: what Rend's surface
	 * vertex shader draws through, the sky's rays are cast through, ambient
	 * occlusion reads depth through, and lights are culled for. A caller
	 * with a vertex shader of its own gives it too if it uses any of those.
	 * Carrottus GPU's clip space: depth 0 to 1, +Y up the target (card S19).
	 */
	float view_projection[16];
	/**
	 * The target the view is drawn into, in texels; 0 by 0 for none. With
	 * a size and a view_projection, a compile that binds the scene culls
	 * its lights for each RBTREND_LIT_TILE_SIZE-texel tile of it; the sky and
	 * the ambient occlusion are read by it.
	 */
	uint32_t target_width;
	/** The target's height, in texels, as #target_width. */
	uint32_t target_height;
	/** What the color is multiplied by before it is written out. 1 leaves it. */
	float exposure;
	/** How the surface writes its color. */
	RBTREND_COLOR_ENCODING output_encoding;
} RBTREND_LIT_VIEW;

/**
 * @brief Sets where a scene is seen from.
 *
 * It moves where each placed directional light
 * (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED) is seen from: what the sky
 * and the atmosphere's tables read of it.
 *
 * @param[in] scene Scene to change.
 * @param[in] view  Where from. Only read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if an argument is NULL, a value is not finite, the
 *         exposure is negative, only one of the target's sides is 0, the
 *         target is larger than 16384 texels a side, or the encoding is not
 *         an RBTREND_COLOR_ENCODING.
 */
RBT_RESULT rbtrend_lit_scene_set_view(rbtrend_lit_scene scene, const RBTREND_LIT_VIEW *view, RBTREND_ERROR *out_error);

/**
 * @brief The kinds of light.
 *
 * A directional light, wherever this library says so, is one of type
 * RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL or
 * RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED: it lights every point it
 * reaches with no range, isn't culled per tile, is one of
 * RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS, may have cascades or a traced mask,
 * and may be an atmosphere's sun or a shell's light.
 */
typedef uint32_t RBTREND_LIT_LIGHT_TYPE;
enum {
	/** Light from one point, every way. */
	RBTREND_LIT_LIGHT_TYPE_POINT = 0,
	/** Light from one point, in a cone. */
	RBTREND_LIT_LIGHT_TYPE_SPOT = 1,
	/** Light from one direction, everywhere alike: a sun, a moon. */
	RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL = 2,
	/**
	 * A directional light whose source has a place: its direction, light
	 * and disc are worked out at each point it lights from where its source
	 * is (a system's star lighting its worlds each from its own side). It
	 * reads `_position` (its source's place), `_range` (the distance D at
	 * which `_radiance` and `_radius` are given, above 0), `_radiance`,
	 * `_radius` (its disc's angular radius at D, above 0, at most 0.5) and
	 * `_sky_disc`. With R = D sin(`_radius`) the source's radius, at a point
	 * P a distance d from the source and d' = max(d, R): its direction is
	 * from P towards the source, its radiance `_radiance` (D / d')^2, its
	 * disc's angular radius min(asin(R / d'), 0.5), and a traced shadow
	 * ray towards it stops at the source's surface, d - R. A point inside
	 * the source reads it at the surface. What is seen from the eye (the
	 * sky's disc, the atmosphere's sky-view and aerial volume, the
	 * environment filled from it, the height fog and the clouds) takes its
	 * values at the scene's eye; an atmosphere's shell at the shell's
	 * centre (rbtrendcommands.h, RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS); a
	 * directional light's cascades along the direction the caller fits
	 * them by (RBTREND_LIT_CASCADE_FIT::light_direction).
	 */
	RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED = 3,
};

/**
 * @brief One light of a scene, of any type.
 */
/* Frozen: an array element, set every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_LIT_SCENE_LIGHT {
	/** Which type. */
	RBTREND_LIT_LIGHT_TYPE _type;
	/** Where a point or spot light is, or where a placed directional
	 * light's source is, in the scene's space. Not read for a directional
	 * light (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL). */
	float _position[3];
	/**
	 * A spot's axis, from the light outwards; a directional light's
	 * direction, from the surface towards the light. Any length but 0. Not
	 * read for a point light or a placed directional light.
	 */
	float _direction[3];
	/**
	 * Its color times its strength, linear: what a white matte face square
	 * to it reads (a point or spot light at distance 1; a placed
	 * directional light at its #_range). 0 is a light that gives nothing.
	 */
	float _radiance[3];
	/** Where a point or spot light's light has fallen to 0, smoothly: above
	 * 0. What tiled culling bounds it by. A placed directional light's
	 * reference distance: where its #_radiance and #_radius are given,
	 * above 0. Not read for a directional light
	 * (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL). */
	float _range;
	/** A spot's full light within this angle of its axis, in radians... */
	float _inner_angle;
	/** ...fading to none at this one: #_inner_angle to pi / 2. */
	float _outer_angle;
	/**
	 * A directional light's angular radius, in radians, 0 to 0.5 (a placed
	 * one's above 0, seen from its #_range): the angle from its disc's
	 * centre to its edge (0.00465 for the sun seen from the earth). Its light is spread over the disc, so it lights
	 * nothing more than #_radiance; a traced shadow and the path trace
	 * read it as the light's size. A point or spot light's: 0 (card A38).
	 */
	float _radius;
	/**
	 * A directional light only (either kind): whether RBTREND_LIT_SHADER_SKY_FRAGMENT
	 * draws its disc, at #_radius. Each texel of a disc reads pi times
	 * #_radiance over its solid angle, 2 pi (1 - cos(_radius)). Must be
	 * false for the other types.
	 */
	bool _sky_disc;
} RBTREND_LIT_SCENE_LIGHT;

/**
 * @brief Sets a scene's lights, every type, replacing the ones it had.
 * Shadows, the traced mask command and the path trace name a light by its
 * place in this list.
 *
 * A shadow set for a light stays while its index names a light of a type
 * its kind fits; the call takes away the rest (an index past the new
 * count, or a kind the new light's type can't have). So a list resent each
 * frame keeps its shadows. Directional lights aren't culled per tile; point
 * and spot lights are.
 *
 * @param[in] scene  Scene to change.
 * @param[in] lights The lights; NULL with a count of 0 for none. Only read
 *                   during the call.
 * @param[in] count  How many: up to RBTREND_LIT_MAX_LIGHTS plus
 *                   RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK. Otherwise, with nothing changed:
 *         RBT_RESULT_INVALID_ARGUMENT if @p scene is NULL, @p lights is NULL
 *         with a count above 0, or a light (named by its index) has a type
 *         that is not an RBTREND_LIT_LIGHT_TYPE, a value not finite, a
 *         negative radiance, a direction of 0 where it is read (spot,
 *         directional), a range not above 0 (point, spot, placed
 *         directional), angles out of order or past pi / 2 (spot), a radius
 *         out of 0 to 0.5 (directional), not above 0 or above 0.5 (placed
 *         directional) or not 0 (point, spot), or `_sky_disc` true on a
 *         point or spot light; RBT_RESULT_LIMIT if more than
 *         RBTREND_LIT_MAX_LIGHTS are point and spot lights, or more than
 *         RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS are directional (either kind).
 */
RBT_RESULT rbtrend_lit_scene_set_lights(rbtrend_lit_scene scene, const RBTREND_LIT_SCENE_LIGHT *lights, uint32_t count,
                                     RBTREND_ERROR *out_error);

/**
 * @brief Sets the depth atlas every shadow drawn from maps reads, or takes
 * it away.
 *
 * The caller renders each view of each shadow into its rectangle of the
 * atlas (RBTREND_LIT_SHADOW_MAPS::_rects): a depth-only pass over the
 * atlas, its viewport and scissor the rectangle
 * (RBTREND_COMMAND_SET_VIEWPORTS, RBTREND_COMMAND_SET_SCISSOR_RECTS), binding
 * rbtrend_lit_scene_get_shadow_pass_material() for that light and view at
 * set 0. Which rectangle each view has, and how large, is the caller's:
 * rectangles may differ in size, and nothing checks two don't overlap.
 * Every view's depth is rendered with the same direction of depth.
 *
 * One binding serves any number of shadows of any size, the caps then the
 * view table's; a small or distant light's shadow costs what the caller
 * gives it (Unreal's, Godot's and Unity's atlases).
 *
 * @param[in] scene          Scene to change.
 * @param[in] atlas          A SHADER_RESOURCE view, DEPTH aspect, shaped
 *                           2D, of a depth texture; NULL for none, which
 *                           every map shadow then reads as lit. Borrowed:
 *                           it outlives its use by the scene, until set
 *                           again or the scene goes.
 * @param[in] reversed_depth The views are rendered near at 1, far at 0.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK. Otherwise, with nothing changed:
 *         RBT_RESULT_INVALID_ARGUMENT if @p scene is NULL, or @p atlas is of
 *         another instance or not a depth view shaped as above; or
 *         RBT_RESULT_INVALID_ARGUMENT, naming the light, if a shadow already
 *         set has a rectangle past the atlas's size.
 */
RBT_RESULT rbtrend_lit_scene_set_shadow_atlas(rbtrend_lit_scene scene, rbtrend_texture_view atlas, bool reversed_depth,
                                           RBTREND_ERROR *out_error);

/**
 * @brief What kind of shadow a light has.
 */
typedef enum RBTREND_LIT_SHADOW_KIND RBT_ENUM_BASE {
	/** Not a kind: a desc that has not chosen, and is refused. */
	RBTREND_LIT_SHADOW_KIND_NONE      = 0,
	/** A traced mask, a directional light's, written by
	 * RBTREND_COMMAND_LIT_SHADOW_MASK (rbtrendcommands.h) for the light. */
	RBTREND_LIT_SHADOW_KIND_MASK      = 1,
	/** A directional light's cascades, from the atlas. Its fields are an
	 * RBTREND_LIT_SHADOW_MAPS chained on the shadow's base. */
	RBTREND_LIT_SHADOW_KIND_CASCADES  = 2,
	/** A spot's map, one view in the atlas; an RBTREND_LIT_SHADOW_MAPS
	 * chained on. */
	RBTREND_LIT_SHADOW_KIND_SPOT_MAP  = 3,
	/** A point light's six faces, +X, -X, +Y, -Y, +Z, -Z, each a view in the
	 * atlas; an RBTREND_LIT_SHADOW_MAPS chained on. A fragment is looked up
	 * in the face its offset from the light is largest along. */
	RBTREND_LIT_SHADOW_KIND_POINT_MAP = 4,
} RBTREND_LIT_SHADOW_KIND;

/**
 * @brief One light's shadow: its kind, its strength and, for a mask, the
 * mask. A map kind's fields are an RBTREND_LIT_SHADOW_MAPS chained on `base`
 * (its `next`); a mask has none chained.
 */
typedef struct RBTREND_LIT_LIGHT_SHADOW {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_LIGHT_SHADOW, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Which kind. */
	RBTREND_LIT_SHADOW_KIND _kind;
	/** How dark the shadow is: 1 takes all the light away, 0 none of it. */
	float _strength;
	/**
	 * RBTREND_LIT_SHADOW_KIND_MASK: a SHADER_RESOURCE 2D view of color of
	 * the mask RBTREND_COMMAND_LIT_SHADOW_MASK writes for this light, at the
	 * scene's view's target size, read at each fragment's place on the
	 * target. Borrowed: it outlives its use by the scene, until the light's
	 * shadow is set again or the scene goes. Not read for the other kinds.
	 */
	rbtrend_texture_view _mask;
} RBTREND_LIT_LIGHT_SHADOW;

/**
 * @brief A rectangle of a scene's shadow atlas, in texels.
 */
/* Frozen: an array element of RBTREND_LIT_SHADOW_MAPS. It never changes; a new need gets a new type. */
typedef struct RBTREND_LIT_SHADOW_RECT {
	/** Its left column. */
	uint32_t _x;
	/** Its top row. */
	uint32_t _y;
	/** Its width: above 0. */
	uint32_t _width;
	/** Its height: above 0. */
	uint32_t _height;
} RBTREND_LIT_SHADOW_RECT;

/**
 * @brief A map shadow's views: each one's matrix and rectangle of the
 * atlas, and the filtering. Chained on RBTREND_LIT_LIGHT_SHADOW's base.
 *
 * Cascades: a fragment is shadowed from the first cascade whose split is
 * beyond it along the view direction, and over the last tenth of that
 * cascade's stretch (from the split before it, or the eye for the first) it
 * is blended into the next by a smoothstep, so no seam shows where one
 * cascade's detail gives way to the next's. The last fades out to no shadow
 * over its own last tenth; past the last split a fragment is lit. A caller
 * framing its own cascades lets each after the first reach back over the
 * last tenth of the one before, as rbtrend_lit_fit_cascades() does: where a
 * cascade's view doesn't reach, it reads as lit.
 */
typedef struct RBTREND_LIT_SHADOW_MAPS {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** How many views: 1 to RBTREND_LIT_MAX_CASCADES for CASCADES, 1 for a
	 * SPOT_MAP, 6 for a POINT_MAP. */
	uint32_t _view_count;
	/**
	 * Each view's matrix from the scene's space to its clip space, column
	 * after column, as rendered into its rectangle (the rectangle is the
	 * whole of that clip space). rbtrend_lit_fit_cascades(),
	 * rbtrend_lit_spot_shadow_view_projection() and
	 * rbtrend_lit_point_shadow_view_projection() frame them.
	 */
	float _view_projection[6][16];
	/** Each view's rectangle of the atlas, inside it. Filtering reads within
	 * a rectangle only, clamped at its edge. */
	RBTREND_LIT_SHADOW_RECT _rects[6];
	/**
	 * CASCADES only: how far along the view direction from the eye each
	 * cascade reaches, increasing and above 0, blended as above (the last
	 * tenth of each into the next; the last fades out). Not read for the
	 * other kinds.
	 */
	float _split_depths[RBTREND_LIT_MAX_CASCADES];
	/** Depth added in the light's favour before the test, in the map's
	 * depth units: against acne. */
	float _depth_bias;
	/** How far a surface's position is moved along its normal before it is
	 * looked up, in the scene's units: against acne on slopes. */
	float _normal_offset;
	/** The edge, in texels: 0 for the hardware's own filter alone, up to 4. */
	float _softness;
} RBTREND_LIT_SHADOW_MAPS;

/**
 * @brief Sets light @p light's one shadow, of any kind its type can have,
 * replacing what it had, or takes it away.
 *
 * A directional light takes a MASK or CASCADES; a spot a SPOT_MAP; a point
 * light a POINT_MAP. A map shadow's rectangles must lie inside the scene's
 * atlas (rbtrend_lit_scene_set_shadow_atlas()), and the scene's views
 * together stay within RBTREND_LIT_MAX_SHADOW_VIEWS. The shadow pass
 * materials of its views get their matrices here.
 *
 * A mask is read by the lit surface for that light in place of maps: the
 * light at a fragment is multiplied by 1 - `_strength` x (1 - m), m the
 * mask's red at the fragment's place on the view's target. The mask is
 * written by RBTREND_COMMAND_LIT_SHADOW_MASK earlier in the list that draws
 * with it, or before; a surface drawn in a list with no mask written yet
 * reads whatever the texture holds. A map shadow multiplies the light by
 * 1 - `_strength` x (1 - v), v the share of the filtered comparisons that
 * pass. Either takes effect as the scene's other values do, at the next
 * submission of a list that binds the scene.
 *
 * A light has one shadow, set by naming the light; a change to one light's
 * shadow touches that light alone (Unreal, Godot and Unity set shadows on
 * each light).
 *
 * @param[in] scene     Scene to change.
 * @param[in] light     Which light: its place in the scene's lights.
 * @param[in] shadow    The shadow, or NULL for none. Only read during the
 *                      call; a mask view it names is borrowed, as `_mask`
 *                      says.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK. Otherwise, checked in this order, with nothing
 *         changed: RBT_RESULT_INVALID_ARGUMENT if @p scene is NULL;
 *         RBT_RESULT_INVALID_ARGUMENT if @p light names no light of the
 *         scene; the struct codes for `shadow->base`;
 *         RBT_RESULT_INVALID_ARGUMENT if `_kind` is NONE or not an
 *         RBTREND_LIT_SHADOW_KIND, or a kind the light's type can't have, or
 *         `_strength` is not finite or out of 0 to 1; for a mask, if
 *         `_mask` is NULL, of another instance, or not a SHADER_RESOURCE 2D
 *         view of color; for a map kind, the struct codes for the chained
 *         RBTREND_LIT_SHADOW_MAPS (missing: RBT_RESULT_INVALID_ARGUMENT), then
 *         RBT_RESULT_INVALID_ARGUMENT if `_view_count` is wrong for the kind,
 *         a value is not finite, a rectangle is empty or past the atlas (or
 *         there is no atlas), the splits are not increasing above 0
 *         (CASCADES), the softness is out of 0 to 4 or a bias negative;
 *         RBT_RESULT_LIMIT if the scene's views would pass
 *         RBTREND_LIT_MAX_SHADOW_VIEWS.
 */
RBT_RESULT rbtrend_lit_scene_set_light_shadow(rbtrend_lit_scene scene, uint32_t light, const RBTREND_LIT_LIGHT_SHADOW *shadow,
                                           RBTREND_ERROR *out_error);

/**
 * @brief The material a pass that renders view @p view of light @p light's
 * map shadow binds at set 0: of rbtrend_lit_get_shadow_layout(), holding
 * that view's matrix. The scene's: don't destroy it. It changes with the
 * light's shadow; one taken before a later
 * rbtrend_lit_scene_set_light_shadow() for the light holds the new matrix.
 *
 * The scene makes RBTREND_LIT_MAX_SHADOW_VIEWS such materials when it is
 * created (rbtrend_lit_scene_create()) and hands them out as shadows take
 * views: nothing is allocated here or by rbtrend_lit_scene_set_light_shadow().
 *
 * @param[in] scene Scene to read.
 * @param[in] light Which light.
 * @param[in] view  Which view of its shadow: a cascade, or a face.
 *
 * @return The material, or NULL if @p scene is NULL, @p light has no map
 *         shadow, or @p view is not below its view count.
 */
rbtrend_material rbtrend_lit_scene_get_shadow_pass_material(rbtrend_lit_scene scene, uint32_t light, uint32_t view);

/**
 * @brief How rbtrend_lit_fit_cascades() frames a directional light's
 * cascades.
 */
typedef struct RBTREND_LIT_CASCADE_FIT {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_CASCADE_FIT, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** The view's eye, as RBTREND_LIT_VIEW has it. */
	float eye_position[3];
	/** Which way the view looks; any length but 0. */
	float view_direction[3];
	/** Which way is up on the view's target; not along view_direction. */
	float view_up[3];
	/** true for a parallel projection, false for a perspective one. */
	bool orthographic;
	/** A perspective view's vertical field of view, in radians. */
	float fov_y;
	/** A parallel view's half height, in the scene's units. */
	float half_height;
	/** The target's width over its height. */
	float aspect;
	/** The stretch of the view's depth the cascades cover: near_depth above 0
	 * for a perspective view, far_depth beyond it. */
	float near_depth;
	/** The far end of that stretch: beyond #near_depth. */
	float far_depth;
	/** 1 to RBTREND_LIT_MAX_CASCADES. */
	uint32_t cascade_count;
	/** How the stretch is split, 0 to 1: 0 evenly, 1 by a constant ratio
	 * (logarithmically); between, a mix of the two (Zhang's practical split
	 * scheme). 0.75 suits most views. */
	float split_lambda;
	/** From the scene towards the light: a directional light's
	 * RBTREND_LIT_SCENE_LIGHT::_direction; for a placed directional light,
	 * a direction of the caller's choosing towards its source (from the
	 * view's eye, or one held while the light turns): its shadow is
	 * projected along it, while its shading turns per point. */
	float light_direction[3];
	/** How far beyond a cascade towards the light its view still catches
	 * what casts a shadow into it, in the scene's units: the height of the
	 * tallest thing that can stand between a surface and the light. */
	float caster_distance;
	/** The width in texels of each cascade's rectangle of the atlas: what
	 * each cascade is snapped to, so its shadows don't shimmer as the view
	 * moves. */
	uint32_t map_size;
	/** Whether the views will be rendered with depth reversed. */
	bool reversed_depth;
} RBTREND_LIT_CASCADE_FIT;

/**
 * @brief Where a cascade fit's scene space is, in a space that doesn't
 * move, so that its cascades are snapped there (rbtrend_lit_fit_cascades()).
 *
 * A caller drawing relative to the eye (so that places far from its origin
 * stay exact) has a scene space that moves every frame; snapped there,
 * cascades shimmer as the view moves. With this chained onto an
 * RBTREND_LIT_CASCADE_FIT, once, they are snapped where the fixed space's
 * origin lands, worked out in double, and stay still at any distance from
 * it under a still light. Under a light that turns between fits, the grid
 * near the view slides by the view's distance from the fixed space's origin
 * times the turn: the nearer that origin is to the view, the less.
 */
typedef struct RBTREND_LIT_CASCADE_ANCHOR {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/**
	 * Where the scene's origin is in the fixed space, in the scene's units:
	 * for a scene whose origin is the eye, the eye's place in the fixed
	 * space. Finite: x, then y, then z, each its own check. The fixed
	 * space's origin is where the cascades are snapped.
	 */
	RbtDVec3 scene_origin;
} RBTREND_LIT_CASCADE_ANCHOR;


/**
 * @brief Frames a directional light's cascades for a view: for each, the
 * matrix its view is rendered through and how far along the view it
 * reaches, into @p out_maps's `_view_count`, `_view_projection` and
 * `_split_depths` (the views past the count, and the splits past it, are
 * zeroed). Its rectangles, biases and softness are left as they are, the
 * caller's; its base, which the caller sets as for
 * rbtrend_lit_scene_set_light_shadow(), is checked and left as it is.
 *
 * Each cascade bounds its stretch of the view's frustum with a sphere, so
 * its size doesn't change as the view turns (a cascade after the first from
 * the last tenth of the stretch before it, which the surface blends over),
 * looks at it down the light's direction with a parallel projection (its
 * clip space's y negated, as rbtrend_lit_point_shadow_view_projection()'s),
 * and is moved by whole texels of its rectangle, so as the view moves its
 * shadows stay still: the stable cascades of Valient's "Stable Rendering of
 * Cascaded Shadow Maps", as Unreal and Hazel have them. Moved where the
 * scene's origin lands; or, with an RBTREND_LIT_CASCADE_ANCHOR chained on
 * @p fit, where the origin of the space it names lands, worked out in
 * double: what a caller drawing relative to the eye chains, so its shadows
 * stay still as the eye moves under a still light. A light that turns
 * between fits moves the grid near the view by the view's distance from the
 * snapped origin times the turn. Depth runs 0 at the light's end to 1 (or
 * reversed).
 *
 * @param[in]  fit       The view and how its cascades are framed. Only read
 *                       during the call.
 * @param[out] out_maps  Receives the cascades, as above.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         written, if an argument is NULL, a value is not finite, a direction
 *         is 0, up runs along the view, the depths or the field of view are
 *         out of their ranges, the cascade count is 0 or too many, the map
 *         size is 0, a chained anchor's scene_origin is not finite, or
 *         either struct's base or chain is wrong
 *         (RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN for the anchor chained on
 *         twice).
 */
RBT_RESULT rbtrend_lit_fit_cascades(const RBTREND_LIT_CASCADE_FIT *fit, RBTREND_LIT_SHADOW_MAPS *out_maps, RBTREND_ERROR *out_error);

/**
 * @brief The matrix face @p face of a point light's shadow is rendered
 * through: a perspective from the light along the face's axis (+X, -X, +Y,
 * -Y, +Z, -Z for faces 0 to 5), from @p near_depth out to the light's range,
 * a little wider than 90 degrees (the tangent of its half angle is 1.05),
 * so a soft shadow's taps near a face's edge stay inside the face's view.
 * Its clip space's y is negated, as a directional light's cascades' and a
 * spot's are: the view is the picture it was when clip space was +Y down
 * the target, and the surface reads it through the same matrix, so either
 * way works.
 *
 * @param[in]  light               The point light (of type
 *                                 RBTREND_LIT_LIGHT_TYPE_POINT).
 * @param[in]  face                Which face, 0 to 5.
 * @param[in]  near_depth          The near plane's distance from the light.
 * @param[in]  reversed_depth      Whether the view is rendered with depth
 *                                 reversed: near at 1, far at 0.
 * @param[out] out_view_projection Receives the matrix, column after column.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         written, if an argument is NULL, the light is not a valid point
 *         light (as rbtrend_lit_scene_set_lights() checks), @p face is not
 *         below 6, or @p near_depth is not between 0 and the range.
 */
RBT_RESULT rbtrend_lit_point_shadow_view_projection(const RBTREND_LIT_SCENE_LIGHT *light, uint32_t face, float near_depth, bool reversed_depth,
                                                 float out_view_projection[16], RBTREND_ERROR *out_error);

/**
 * @brief The matrix a spot light's shadow is rendered through: a perspective
 * from the light along its axis, wide enough for its cone, from
 * @p near_depth out to its range, its clip space's y negated (as
 * rbtrend_lit_point_shadow_view_projection()'s).
 *
 * @param[in]  light               The spot (of type
 *                                 RBTREND_LIT_LIGHT_TYPE_SPOT).
 * @param[in]  near_depth          The near plane's distance from the light.
 * @param[in]  reversed_depth      Whether the view is rendered with depth
 *                                 reversed: near at 1, far at 0.
 * @param[out] out_view_projection Receives the matrix, column after column.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         written, if an argument is NULL, the light is not a valid spot (as
 *         rbtrend_lit_scene_set_lights() checks), or @p near_depth is not
 *         between 0 and the range.
 */
RBT_RESULT rbtrend_lit_spot_shadow_view_projection(const RBTREND_LIT_SCENE_LIGHT *light, float near_depth, bool reversed_depth,
                                                float out_view_projection[16], RBTREND_ERROR *out_error);

/**
 * @brief The light that comes from all round: a hemisphere of two colors,
 * and an environment, added together.
 */
typedef struct RBTREND_LIT_AMBIENT {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_AMBIENT, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Which way is up: the sky is that way, the ground the other. Any length
	 * but 0 while either color isn't 0. */
	float up[3];
	/** The light from straight up, linear. */
	float sky[3];
	/** The light from straight down, linear. */
	float ground[3];
	/** An environment, or NULL for none. It must outlive its use by the
	 * scene: until another is set, or the scene goes. */
	rbtrend_lit_environment environment;
	/** What the environment's light is multiplied by. */
	float environment_intensity;
	/**
	 * Directions in the scene's space into the environment's, row after row
	 * of a 3x3 rotation: the environment's +Y is its up, and an
	 * equirectangular picture's middle column faces its +Z. The identity
	 * {1,0,0, 0,1,0, 0,0,1} is right for a scene that is Y up; a Z-up scene
	 * takes {1,0,0, 0,0,1, 0,-1,0}.
	 */
	float environment_rotation[9];
} RBTREND_LIT_AMBIENT;

/**
 * @brief Sets a scene's ambient light.
 *
 * @param[in] scene   Scene to change.
 * @param[in] ambient The ambient light. Only read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if an argument is NULL, a value is not finite, a color or
 *         the intensity is negative, up is 0 while a color isn't, or the
 *         environment is of another instance.
 */
RBT_RESULT rbtrend_lit_scene_set_ambient(rbtrend_lit_scene scene, const RBTREND_LIT_AMBIENT *ambient, RBTREND_ERROR *out_error);

/** The fewest texels a side of a scene's sky-view table has. */
#define RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE 32u
/** The most texels a side of a scene's sky-view table has. */
#define RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE 1024u
/** The fewest texels a side of a scene's aerial volume has. */
#define RBTREND_LIT_ATMOSPHERE_MIN_AERIAL_SIZE 8u
/** The most texels a side of a scene's aerial volume has. */
#define RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_SIZE 128u
/** The most steps a texel of the sky-view table takes. */
#define RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS 128u
/** The most steps an aerial slice takes. */
#define RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_STEPS 16u

/**
 * @brief The atmosphere a scene draws (rbtrendatmosphere.h), where it is,
 * and the sizes of the scene's own tables. No field has a default.
 */
typedef struct RBTREND_LIT_ATMOSPHERE {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_ATMOSPHERE, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** The atmosphere, or NULL for none (every other field then unread).
	 * It must outlive its use by the scene, as an environment must. */
	rbtrend_atmosphere atmosphere;
	/** Its centre, in the scene's space: finite. Set as often as it moves. */
	float center[3];
	/** How many of the scene's directional lights (either kind), the
	 * first in the list's order, light the medium: 1 or 2. A scene with fewer has as many as it has; with none,
	 * nothing lights the medium (a black sky, as a night with no moon). */
	uint32_t sun_count;
	/** The sky-view table's width (round the eye) and height (from the
	 * zenith to the nadir), each RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE to
	 * RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE (192 x 108 is the paper's),
	 * or both 0 for no sky: the atmosphere then lights the scene's surfaces
	 * through its transmittance and draws nothing behind them, the aerial
	 * volume's sizes 0 too (its air drawn by
	 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS, given the same atmosphere and
	 * centre). */
	uint32_t sky_view_width;
	/** See #sky_view_width. */
	uint32_t sky_view_height;
	/** The aerial volume's width, height and depth (its slices), each
	 * RBTREND_LIT_ATMOSPHERE_MIN_AERIAL_SIZE to
	 * RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_SIZE, or all three 0 for none
	 * (surfaces aren't faded by the air). 32 x 32 x 32 is the paper's. */
	uint32_t aerial_width;
	/** See #aerial_width. */
	uint32_t aerial_height;
	/** See #aerial_width. */
	uint32_t aerial_depth;
	/** How far from the eye the volume's last slice is, in the scene's
	 * unit: finite, above 0; beyond it, the last slice's. Unread without a
	 * volume. */
	float aerial_distance;
	/** The least and most steps a texel of the sky-view table takes, more
	 * for a longer ray (the paper's 4 and 14): 1 to
	 * RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS each, the least at most the
	 * most. */
	uint32_t sky_view_min_steps;
	/** See #sky_view_min_steps. */
	uint32_t sky_view_max_steps;
	/** The steps each slice of the aerial volume takes: 1 to
	 * RBTREND_LIT_ATMOSPHERE_MAX_AERIAL_STEPS (the paper's 1). Unread without
	 * a volume. */
	uint32_t aerial_steps_per_slice;
} RBTREND_LIT_ATMOSPHERE;

/**
 * @brief Sets the atmosphere a scene draws, or none. The scene's sky-view
 * table and aerial volume are made (or remade) here where their sizes
 * differ from those it holds; the same sizes again allocate nothing, so it
 * is called each frame as the centre moves.
 *
 * With an atmosphere, from the frame's RBTREND_COMMAND_LIT_ATMOSPHERE on:
 * RBTREND_LIT_SHADER_SKY_FRAGMENT draws the sky-view table; each of the
 * first `sun_count` directional lights reaches a surface, and its disc the
 * eye, through the atmosphere's transmittance; each lit surface is faded
 * by the aerial volume where the scene has one. With none, nothing of this
 * is read, and the scene draws as it did.
 *
 * With no sky (both sky-view sizes 0): the sky shader draws as with no
 * atmosphere, the suns' discs undimmed; each of the first `sun_count`
 * directional lights still reaches a surface through the transmittance.
 * Refused: one sky-view size 0 and not the other; a volume without a
 * sky-view. The sky-view's steps are checked as ever, and not read.
 *
 * Memory: one array of R16G16B16A16_FLOAT layers, each the largest of the
 * sky-view table, the atmosphere's transmittance table and a slice of the
 * volume across and down: two layers (the sky-view, the transmittance),
 * and two a slice of the volume (its light and its transmittance, each per
 * channel). 432 KiB with a 192 x 108 sky-view and a 256 x 64 transmittance
 * table and no volume; 13.9 MiB (66 layers of 256 x 108) with a 32 x 32 x
 * 32 volume. With no sky, two layers of the transmittance table's size
 * (the first unread); an array the scene already
 * holds that is as large is kept, so switching an atmosphere between a sky
 * and none makes nothing.
 *
 * @param[in]  scene      The scene.
 * @param[in]  atmosphere What it draws. Only read during the call.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument or
 *         a field out of its range, each its own check and message; the
 *         struct codes for `atmosphere->base`; RBT_RESULT_OUT_OF_MEMORY; or
 *         RBT_RESULT_REND_GPU_FAILURE if a table can't be made. Nothing
 *         changes on failure.
 */
RBT_RESULT rbtrend_lit_scene_set_atmosphere(rbtrend_lit_scene scene, const RBTREND_LIT_ATMOSPHERE *atmosphere, RBTREND_ERROR *out_error);

/** The most suns (the scene's first directional lights) a scene's fog
 * takes light from. */
#define RBTREND_LIT_FOG_MAX_SUNS 2u

/**
 * @brief A scene's height fog: Rabbittus Math's fog (math/rbtmathfog.h),
 * where it is, and how it takes light. No field has a default.
 *
 * Its light is its own albedo times the light that reaches it: for each of
 * the scene's first #RBTREND_LIT_FOG::sun_count directional lights, pi times its radiance
 * times Henyey and Greenstein's phase of #RBTREND_LIT_FOG::phase_g at the angle between the
 * view and the light, through the scene atmosphere's transmittance at the
 * eye where the scene has one; and #RBTREND_LIT_FOG::ambient_scale times the scene's
 * ambient light (rbtrend_lit_scene_set_ambient(): the hemisphere and the
 * environment's irradiance) from the fog's up at the middle of the part
 * seen. It is taken as even along the part seen (Unreal's height fog's
 * model).
 */
typedef struct RBTREND_LIT_FOG {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_FOG, and its chain, which takes nothing (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** The fog: each field in its range (math/rbtmathfog.h). */
	RBTMATH_HEIGHT_FOG fog;
	/** Its origin in the scene's space this frame (a plane's point at
	 * height 0, a sphere's centre): finite. Set as often as it moves. */
	float origin[3];
	/** The share of its extinction that scatters, three channels: each
	 * finite, 0 to 1. */
	float albedo[3];
	/** Its phase's g for the suns' light (Henyey and Greenstein's): finite,
	 * -0.999 to 0.999; above 0 brighter towards a sun. */
	float phase_g;
	/** How many of the scene's directional lights, the first, light it: 0 to
	 * RBTREND_LIT_FOG_MAX_SUNS. */
	uint32_t sun_count;
	/** The scene's ambient light's share: finite, at least 0. */
	float ambient_scale;
	/** How far from the eye it starts, in the scene's unit: finite, at least
	 * 0. A caller drawing the grid (RBTREND_COMMAND_LIT_FOG) sets the
	 * grid's far distance here. */
	float start_distance;
	/** How far from the eye it ends: finite, above #start_distance; the
	 * sky's ray stops here. */
	float end_distance;
	/** The most it hides: finite, 0 to 1 (its transmittance never below 1
	 * minus this). */
	float max_opacity;
} RBTREND_LIT_FOG;

/**
 * @brief Sets a scene's height fog, or takes it away (NULL). Copied; from
 * the next submission, every lit surface, transparent ones included, and
 * the sky (RBTREND_LIT_SHADER_SKY_FRAGMENT, its suns' discs too) are seen
 * through it, after the scene atmosphere's air where it has one: for a
 * texel at a distance D along its view ray (the sky's at the fog's end),
 * between `start_distance` and the nearer of D and `end_distance` (nothing
 * where that is no farther than the start), its colour c becomes
 * c x T + light x (1 - T), before the exposure: T the fog's transmittance
 * there (Math's, rbtmath_height_fog_compute_transmittance(), the shaders'
 * copy to a tolerance), never below 1 - `max_opacity`; the light as
 * RBTREND_LIT_FOG says. A transparent surface takes it as an opaque one
 * does, premultiplied by its opacity as the rest of its colour; the suns'
 * discs are dimmed by T. Nothing allocated: set each frame as it moves.
 *
 * @param[in]  scene     The scene.
 * @param[in]  fog       The fog, or NULL for none. Only read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL scene or a
 *         field out of its range, each its own check and message in member
 *         order (Math's fog by Math's checks); the struct codes for
 *         `fog->base`. Nothing changes on failure.
 */
RBT_RESULT rbtrend_lit_scene_set_fog(rbtrend_lit_scene scene, const RBTREND_LIT_FOG *fog, RBTREND_ERROR *out_error);

/* ---- Cover --------------------------------------------------------------- */

/** How a cover's up and height are found. */
typedef enum RBTREND_LIT_COVER_FRAME RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_LIT_COVER_FRAME_NONE = 0,
	/** Up is away from a centre; height is the distance from it. */
	RBTREND_LIT_COVER_FRAME_ROUND = 1,
	/** Up is one direction; height is along it from an origin. */
	RBTREND_LIT_COVER_FRAME_FLAT = 2,
} RBTREND_LIT_COVER_FRAME;

/**
 * @brief Snow and wetness over a scene's lit surfaces, set each frame as
 * the weather changes. No field has a default.
 *
 * At each lit pixel whose material answers the cover
 * (RBTREND_LIT_COVER_RESPONSE), after its material's reads and before its
 * lighting, with N the face's geometric normal turned to the eye (not its
 * normal map's, so snow lies on the shape):
 *   - Wet, first (Lagarde's porous model): with w = #RBTREND_LIT_COVER::wetness, the base
 *     colour times lerp(1, 0.2, w x the response's `porosity`), the
 *     roughness lerped towards #RBTREND_LIT_COVER::wet_roughness by w; metalness kept.
 *   - Snow, over it: s = #RBTREND_LIT_COVER::snow_amount x smoothstep(#RBTREND_LIT_COVER::slope_begin,
 *     #RBTREND_LIT_COVER::slope_full, dot(N, up)) x smoothstep(#RBTREND_LIT_COVER::snow_line, #RBTREND_LIT_COVER::snow_line +
 *     #RBTREND_LIT_COVER::snow_band, height) x the response's `cover`; the base colour,
 *     roughness, metalness and shading normal blend by s towards
 *     #RBTREND_LIT_COVER::snow_color, #RBTREND_LIT_COVER::snow_roughness, 0 and N. Emission is kept.
 *
 * Up and height: ROUND, up = normalize(P - #RBTREND_LIT_COVER::point), height = |P - #RBTREND_LIT_COVER::point|;
 * FLAT, up = normalize(#RBTREND_LIT_COVER::up), height = dot(P - #RBTREND_LIT_COVER::point, up). Worked out in
 * float at the pixel: a round centre 6.4e6 units from the scene's origin
 * puts the height's step at 0.5 units.
 *
 * Kept in 48 bytes of the scene's block: #RBTREND_LIT_COVER::snow_color and #RBTREND_LIT_COVER::snow_roughness
 * are held as half floats (within 1/2048 of each value), the rest as
 * floats.
 */
typedef struct RBTREND_LIT_COVER {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_COVER, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Its frame: ROUND or FLAT. */
	RBTREND_LIT_COVER_FRAME frame;
	/** ROUND: the centre; FLAT: the origin; the scene's space, finite. */
	float point[3];
	/** FLAT: up, any length but 0, finite; not read for ROUND. */
	float up[3];
	/** How much snow lies where it can, 0 to 1. */
	float snow_amount;
	/** The snow's colour, linear, each 0 to 1. */
	float snow_color[3];
	/** The snow's roughness, 0 to 1. */
	float snow_roughness;
	/** Snow begins on faces whose up-facing cosine is above this: -1 to 1. */
	float slope_begin;
	/** ...and lies fully above this: #slope_begin to 1 (equal: a step). */
	float slope_full;
	/** The height below which no snow lies, in the scene's unit: finite. */
	float snow_line;
	/** The height over which snow comes in above the line: above 0, finite. */
	float snow_band;
	/** How wet surfaces are, 0 to 1. */
	float wetness;
	/** The roughness a fully wet surface tends to, 0 to 1. */
	float wet_roughness;
} RBTREND_LIT_COVER;

/**
 * @brief Sets a scene's cover, or none (NULL, as a scene starts). Copied
 * into the scene's block; allocates nothing, so it is set every frame.
 * From the next submission, every lit surface of the scene whose material
 * answers the cover (RBTREND_LIT_COVER_RESPONSE) is wetted and snowed by
 * RBTREND_LIT_COVER's rule; the traced paths (RBTREND_COMMAND_LIT_PATH_TRACE,
 * traced reflections) see their materials bare.
 *
 * @param[in]  scene     The scene.
 * @param[in]  cover     The cover, or NULL. Only read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL scene or a
 *         field out of its range, each its own check and message, in the
 *         struct's order; the struct codes for `cover->base`. Nothing
 *         changes on failure.
 */
RBT_RESULT rbtrend_lit_scene_set_cover(rbtrend_lit_scene scene, const RBTREND_LIT_COVER *cover, RBTREND_ERROR *out_error);

/**
 * @brief How a material answers its scene's cover (RBTREND_LIT_COVER),
 * chained onto its RBTREND_LIT_MATERIAL_VALUES
 * (rbtrend_lit_material_set_values()). A material without one takes no
 * cover at all: neither snow nor wetness. Read back by
 * rbtrend_lit_material_get_values() with the rest.
 */
typedef struct RBTREND_LIT_COVER_RESPONSE {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_COVER_RESPONSE, chained (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** How much of the scene's snow lies on it, 0 to 1. */
	float cover;
	/** How porous it is, 0 (wet only on top: darkens nothing) to 1. */
	float porosity;
} RBTREND_LIT_COVER_RESPONSE;

/** The most steps a texel's march takes in RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS. */
#define RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS 128u

/** The most lights an RBTREND_LIT_ATMOSPHERE_SHELL names: the size of its
 * frozen #RBTREND_LIT_ATMOSPHERE_SHELL::_lights array, as a scene's
 * atmosphere takes at most two suns. */
#define RBTREND_LIT_ATMOSPHERE_MAX_SHELL_LIGHTS 2u

/**
 * @brief An atmosphere's shell drawn from where the eye is, by
 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS: which atmosphere, where its
 * centre is this frame, and which of the scene's lights light it.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_LIT_ATMOSPHERE_SHELL {
	/** The atmosphere (rbtrendatmosphere.h): its medium and tables. Of the
	 * scene's instance; it must outlive the list's submission. */
	rbtrend_atmosphere _atmosphere;
	/** Its centre in the scene's space this frame: finite. */
	float _center[3];
	/** How many of #_lights light it: 0 to
	 * RBTREND_LIT_ATMOSPHERE_MAX_SHELL_LIGHTS. 0: its air is dark, its
	 * ground black, and it still dims what is behind it (a world's night). */
	uint32_t _light_count;
	/**
	 * Its lights, by their places in the scene's light list
	 * (rbtrend_lit_scene_set_lights()): each a directional light
	 * (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL), its direction and radiance used
	 * as they are, or a placed directional light
	 * (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED), its direction and
	 * radiance taken at #_center by the type's rule; a placed light whose
	 * source is inside or on the shell's top sphere is refused. A point or
	 * spot light is refused. Entries past #_light_count are not read.
	 */
	uint32_t _lights[RBTREND_LIT_ATMOSPHERE_MAX_SHELL_LIGHTS];
} RBTREND_LIT_ATMOSPHERE_SHELL;

/**
 * @brief Sets the ambient occlusion of the view, or takes it away: a texture
 * the size of the view's target whose red is how open each texel is to the
 * sky (RBTREND_COMMAND_LIT_AMBIENT_OCCLUSION makes one from a depth buffer),
 * which darkens the ambient light there.
 *
 * @param[in] scene     Scene to change.
 * @param[in] occlusion A SHADER_RESOURCE 2D view of color, or NULL for none.
 *                      It must outlive its use by the scene.
 * @param[in] strength  How much of it is applied, 0 to 1.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if @p scene is NULL, the view is of another instance or of
 *         the wrong type or shape, or @p strength is out of 0 to 1.
 */
RBT_RESULT rbtrend_lit_scene_set_ambient_occlusion(rbtrend_lit_scene scene, rbtrend_texture_view occlusion, float strength, RBTREND_ERROR *out_error);

/**
 * @brief Sets whether the surface reads a bent normal from the ambient
 * occlusion's green, blue and alpha (RBTREND_COMMAND_LIT_AMBIENT_OCCLUSION
 * writes it into a target that has them): the average direction that is
 * open over each texel, along which the diffuse ambient light (the
 * hemisphere and the environment's irradiance) is then looked up, in place
 * of the surface's normal, so light comes from where the surface is open.
 * Specular light keeps the surface's normal. Not read without an
 * occlusion, or with this false, as a scene starts.
 *
 * @param[in] scene Scene to change.
 * @param[in] read  true to read the bent normal.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if @p scene is NULL.
 */
RBT_RESULT rbtrend_lit_scene_set_bent_normals(rbtrend_lit_scene scene, bool read, RBTREND_ERROR *out_error);

/**
 * @brief Sets the opaque picture surfaces with transmission show through,
 * or takes it away: what RBTREND_COMMAND_COPY_SCENE_COLOR (rbtrendcommands.h)
 * copied from the view's color target into a mipped texture, between the
 * opaque draws and the transmitting ones. It is read exposed and, if the
 * scene's output encoding is sRGB, encoded, as the surfaces wrote it.
 *
 * @param[in] scene       Scene to change.
 * @param[in] scene_color A SHADER_RESOURCE 2D view of color holding every
 *                        mip the copy fills, or NULL for none. It must
 *                        outlive its use by the scene.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT or the failure's
 *         own code (rbtrendcontext.h, "Results and errors"), with nothing
 *         changed, if @p scene is NULL, or the view is of another instance
 *         or of the wrong type, shape or aspect.
 */
RBT_RESULT rbtrend_lit_scene_set_scene_color(rbtrend_lit_scene scene, rbtrend_texture_view scene_color, RBTREND_ERROR *out_error);

#if __cplusplus
} // Extern "C"
#endif // __cplusplus

#endif // RBTREND_LIT_H
