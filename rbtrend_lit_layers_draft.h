/*
 * rbtrend_lit_layers_draft.h - Rend's layered lit surface: a lit fragment
 * shader that blends up to sixteen layers of a material, read from texture
 * arrays, by weights its caller's vertex shader hands it, with height
 * blending and triplanar reads by layer, and lights the blend with the lit
 * surface's one lighting (card L68, its Rend half; the caller's half and
 * the session that asked are named on the card).
 *
 * DRAFT, 2026-11-17, version 2 (version 1, then seven critique passes,
 * each fixed before the next: the revision notes below), for the user's
 * approval. Written unattended on the Mac (Apple M5 Pro) against
 * include/render/rbtrendlit.h ("Sets", "A caller's vertex shader",
 * RBTREND_LIT_SHADER_*, the lit material and its maps),
 * include/render/rbtrendmaterial.h, rbtrendimages.h (2D textures with
 * array_layers, RBTREND_TEXTURE_VIEW_SHAPE_2D_ARRAY, samplers),
 * shaders/rbtrend_lit_bindings.slang (set 0's sixteen bindings),
 * rbtrend_lit_material.slang, rbtrend_lit_surface.slang (lit_shade(): one
 * material's reads, then the lighting) and rbtrend_lit_frag.slang, as read
 * on 2026-11-17 (rabbittus ede3990, 0.3); ../carrottus/include/crgpu.h
 * (CRGPU_MAX_DESCRIPTOR_BINDINGS_PER_SET 16, max_image_array_layers, no
 * draw count from a buffer); and the drafts beside it:
 * rbtrend_block_formats_draft.h (A54: a range of layers in one upload),
 * rbtrend_large_worlds_draft.h (L66's Rend half: struct type 5038).
 * Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel, present:
 * Renderer/Material.h, MaterialAsset.h, Image.h, SceneRenderer.cpp, and
 * Hazel/src searched for terrain, splat, triplanar and foliage): its
 * material is one albedo, normal, metalness and roughness set, as Rend's lit
 * material is; its images have layers (Image.h's Layers), used for
 * render targets and cubes, never as a material's array of layers; no
 * terrain, no splatting, no triplanar reads. Nothing in Hazel to measure
 * this against beyond that it has none.
 * Not built, not in any target, not compilable. Every declaration below is
 * marked [ADDED] or [CHANGED]; nothing unmarked changes. Nothing is built
 * until it is approved; once approved it is frozen.
 *
 * ---------------------------------------------------------------------------
 * Revision notes (the seven passes, 2026-11-17; lenses as the workspace's
 * critique prompt's, named on card L68)
 * ---------------------------------------------------------------------------
 *
 * 2026-11-17: caller names removed (check.sh caller-names), meaning kept.
 *
 * Pass 1 (wrong, missing, more than the need). 1. Version 1 put the
 * layers' arrays in set 0 beside the scene's: set 0 holds sixteen
 * bindings, the sixteenth the atmosphere's tables, Carrottus GPU's cap a
 * set. Changed: everything layered is the material's, set 1, a layout of
 * its own (five bindings). 2. Version 1 had a fourth array for the height.
 * Changed: ORMH (the height in the ORM map's alpha, which the lit material
 * doesn't read). 3. Missing: the reflecting variant (two more targets for
 * LIT_SSR and the traced reflections), so a layered surface would be a
 * hole in a reflecting pass. Changed: RBTREND_LIT_SHADER_LAYERED_SURFACE_
 * REFLECT_FRAGMENT. 4. More than the need: version 1 let each layer name
 * its array layer; a layer's index is its array layer. Removed.
 *
 * Pass 2 (what breaks an existing caller). 1. lit_shade() is split into the
 * surface's reads and the lighting so both fragments share one lighting;
 * every existing lit and reflect test must give the same pixels (the
 * shaders' bits may differ by the split, the pixels must not). Said, with
 * the test. 2. The lit material's table (path tracing) can't take a layered
 * material: a TLAS's material table refuses it rather than misreading it.
 * Said. Looked at and kept: no layout, struct, enum value or call changes;
 * the shader enum is appended.
 *
 * Pass 3 (Rend's contract). 1. The values were a plain struct; a layer's
 * values will grow (a detail map's scale, a distance blend's). Changed: a
 * based struct, RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES registered. 2. The
 * create took no count and sized for sixteen. Changed: layer_count at
 * create, checked against views at set_map (a view with fewer layers
 * refused). 3. Each check its own if, in parameter order, its own message;
 * RBT_STALE then RBTREND_ENTER on every call taking a material, as the lit
 * material's. 4. A declaration's comment named the caller that mirrors a
 * constant (rabbittus/CLAUDE.md: "Rabbittus doesn't know who calls it").
 * Changed: the declarations say what, never who; callers are named only in
 * this draft's discussion ("Who else uses it"), as Rend's drafts do. 5.
 * The block is staged and copied in the next submission as
 * the lit material's values are: no write into a frame in flight. Looked
 * at: allocation at create only; destroy through rbtrend_material_destroy()
 * and the release queue; Slang modules; Doxygen.
 *
 * Pass 4 (what a reviewer would refuse). 1. "Unity's HDRP terrain blends
 * eight layers by height" asserted: HDRP's TerrainLit documents up to eight
 * layers and a height-based blend; kept, its blend's formula not claimed.
 * 2. Version 1 had the fragment read a dither for a fade; a dithered fade
 * is a separate question (Q-L4), not this surface's.
 *
 * Pass 5 (the maths). 1. Triplanar normals: version 1 blended the three
 * planes' tangent-space normals as if in one frame. Changed: the whiteout
 * blend per plane (Golus, 2017), swizzled into the projection's frame, then
 * turned into the scene's by the caller's quaternion. 2. The height blend
 * at a blend depth of 0 divides by nothing where only one layer is left:
 * its definition said so (the heaviest kept whole). 3. The weights a pixel
 * receives are interpolated and needn't sum to 1 after a cut to four:
 * renormalised after the cut, and a sum of 0 (no layer) draws layer 0.
 *
 * Pass 6 (who else; tests). 1. No test of the reflect variant's targets.
 * Changed: test 6. 2. Who else: a game engine's terrain (first), a
 * painted terrain, a CAD model's site ground, vertex-painted walls and
 * floors; nothing shaped by one game in the names.
 *
 * Pass 7 (records). 1. RBTREND_TODO.md item and rabbittus/CLAUDE.md's "Rend"
 * section named; the struct type's number said as the next free when built.
 *
 * ---------------------------------------------------------------------------
 * 1. The need
 * ---------------------------------------------------------------------------
 *
 * A surface made of several materials at once, in proportions that change
 * across it: a terrain's grass, sand, rock and snow; a building site's
 * gravel and paving; a wall with moss painted into it. Rend's lit surface
 * (rbtrendlit.h) shades one material: its values and four maps at set 1.
 * A caller blending layers before Rend's lighting has no way in: Rend's
 * shaders own the lighting, and the fragment shader is Rend's.
 *
 * What a blend needs: per pixel, the weights of a few layers (from the
 * caller's geometry: per vertex, interpolated); each layer's maps, read at
 * the layer's scale, along the texture coordinate or, for steep ground,
 * along three axes (triplanar); the layers' heights to make transitions
 * follow the texture's relief (sand between stones) instead of a smooth
 * fade; then one surface, lit as any.
 *
 * ---------------------------------------------------------------------------
 * 2. The design
 * ---------------------------------------------------------------------------
 *
 * - A LAYERED MATERIAL: an rbtrend_material of a layout of its own,
 *   rbtrend_lit_get_layered_material_layout(), bound at set 1 in place of
 *   a lit material: a block of up to RBTREND_LIT_LAYERED_MAX_LAYERS (16)
 *   layers' values and the blend depth, three 2D texture arrays of layer i
 *   at array layer i (base colour, normal, ORMH: occlusion, roughness,
 *   metalness, and the height in alpha), and a sampler. Five bindings of
 *   set 1's sixteen. Set 0, the scene's, is unchanged.
 * - TWO FRAGMENT SHADERS, appended to RBTREND_LIT_SHADER:
 *   LAYERED_SURFACE_FRAGMENT and its reflecting twin, as the lit surface and
 *   RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT are.
 * - THE CALLER'S VERTEX SHADER writes the lit surface's five outputs
 *   (locations 0 to 4) and six more: the weights of eight layers (5, 6), the
 *   eight layers' indices (7, flat), the triplanar place (8) and normal
 *   (9) in the projection's frame, and that frame's turn into the scene's
 *   space (10, flat).
 * - ONE LIGHTING. lit_shade() (rbtrend_lit_surface.slang) is split in two:
 *   the material's reads into a surface (base colour, opacity, normal,
 *   roughness, metalness, occlusion, specular and the rest), and the
 *   lighting of that surface (every light, shadows, the environment, the
 *   atmosphere's aerial perspective, ambient occlusion, the reflect
 *   variant's outputs). The lit surface reads its one material into the
 *   surface; the layered surface reads and blends its layers into the same
 *   surface; both call the one lighting. No second copy of any light.
 *
 * ---------------------------------------------------------------------------
 * 3. What a pixel does (the definition the header carries)
 * ---------------------------------------------------------------------------
 *
 * Inputs: w[8] (locations 5 and 6), idx[8] (location 7: byte e of the two
 * uints is entry e's layer, 0xFF for none), the texture coordinate t
 * (location 2), the tangent (3), the colour (4), the triplanar place q and
 * normal m (8, 9, the projection's frame), the turn r (10).
 * 1. CUT: the RBTREND_LIT_LAYERED_PIXEL_LAYERS (4) largest w with a layer
 *    (ties to the lower entry), renormalised to sum 1; a sum of 0 reads
 *    layer idx[0] whole, or layer 0 where idx[0] is none.
 * 2. HEIGHTS: for each kept layer, its ORMH read (by its projection,
 *    below); h its alpha, c its height_contrast; a = w + c h; with the
 *    material's blend depth d: w' = max(a - max(a) + d, 0), renormalised
 *    (Mishkinis, 2013). At d = 0 only the largest a is left, whole.
 * 3. READS: for each layer with w' above 0, its base colour and normal
 *    (the ORMH already read), by its projection:
 *    - UV: at t x uv_scale, the normal map through the tangent frame
 *      (locations 1 and 3), as the lit surface's;
 *    - TRIPLANAR: at q x uv_scale on the three planes (q.zy, q.xz, q.xy),
 *      weighted by |m|^4 normalised; each plane's normal by the whiteout
 *      blend into the projection's frame (Golus, "Normal Mapping for a
 *      Triplanar Shader", 2017), summed by the weights, normalised, turned
 *      by r into the scene's space.
 * 4. BLEND: base colour, ORMH's occlusion, roughness and metalness, the
 *    values (base colour factor, roughness, metalness, specular, normal
 *    scale, occlusion strength) and the normals, each the w'-weighted sum;
 *    the normal normalised. Times the colour at location 4 (rgb times the
 *    base colour, a the opacity), as the lit surface's.
 * 5. LIGHT: the surface through the one lighting.
 * No clear coat, sheen, transmission or cut-out: a layer has none of them
 * (Q-L3).
 *
 * Reads a pixel: a UV layer 3, a TRIPLANAR one 9; at most 4 layers. The lit
 * surface reads 4 maps; the layered one 3 to 6 for ground of one or two UV
 * layers, 12 for four, 36 at worst (four triplanar layers).
 *
 * ---------------------------------------------------------------------------
 * 4. Why each shape
 * ---------------------------------------------------------------------------
 *
 * IN REND, NOT THE CALLER'S (Q-L1). Blending before lighting is shading,
 * and rbtrendlit.h's "What is whose" gives shading to Rend. Weighed:
 * a. A LAYERED SURFACE OF REND'S (chosen): one more surface kind beside
 *    the lit one, its lighting shared. Unreal splits a surface into a
 *    vertex factory and a shading model; this is a second way to fill the
 *    same shading model's inputs.
 * b. REND'S LIGHTING AS A MODULE A CALLER IMPORTS: a caller's fragment
 *    shader computes any surface and calls Rend's lighting. The most
 *    general, and its inputs, bindings and module interface become public
 *    and frozen at 1.0, which nothing yet needs. Carded for when a second
 *    custom surface asks.
 * c. A CALLER'S OWN LIGHTING: two copies of every light to keep equal.
 *
 * TEXTURE ARRAYS (Q-L2). Weighed:
 * a. 2D ARRAYS, one per map (chosen): one binding a map, hardware repeat
 *    and filtering per layer, on every backend (Carrottus GPU reports
 *    max_image_array_layers: at least 256 on Vulkan, 2,048 on D3D12 and
 *    Metal); every layer of a map one size, format and level count.
 * b. AN ATLAS: no hardware repeat, mip bleed, anisotropy across tiles.
 * c. BINDLESS: layers of any size; but Rend has no bindless binding path
 *    and descriptor indexing is optional on Vulkan, tiered on Metal.
 * d. A BINDING A LAYER: 48 bindings for sixteen layers, past a set's 16.
 *
 * EIGHT WEIGHTS IN, FOUR LAYERS A PIXEL. Eight interpolants carry a
 * palette the caller chooses per draw or per primitive (the first
 * caller's: a terrain chunk's); a pixel blends its four heaviest. Four is where a fifth's
 * share is almost always invisible, and it caps the reads at 4 x 9.
 *
 * TRIPLANAR BY LAYER, NOT BY SLOPE. A layer's projection says how it is
 * read wherever it is; the caller's rules decide where a layer is. So the
 * nine reads are paid only where a triplanar layer has weight, never by a
 * slope test every pixel makes.
 *
 * THE PROJECTION'S FRAME FROM THE CALLER. Rend's positions are in the
 * caller's space (eye-relative, for the first caller), which moves with the eye
 * and turns with nothing; a triplanar read must be fixed to the ground (its
 * body's axes) and exact far from any origin. So the caller hands the
 * place in the projection's frame, already reduced by its period (the first
 * caller's draw record does it in double), the normal in that frame, and the
 * frame's turn as a unit quaternion (flat): Rend never sees a body or a
 * period.
 *
 * ---------------------------------------------------------------------------
 * 5. Who else uses it
 * ---------------------------------------------------------------------------
 *
 * - A game engine's terrain with biomes (card L68): the first.
 * - A painted terrain of any engine on Rend: weights from a caller's paint.
 * - A CAD model's site ground (graded earth, gravel, paving and grass
 *   painted per vertex on a site's surface): the same material, its
 *   weights from the caller's model.
 * - A wall or floor with vertex-painted wear, moss or puddles' damp.
 * Each fits as written: weights and a palette from its vertices, the
 * triplanar inputs left 0 where every layer is UV.
 *
 * ---------------------------------------------------------------------------
 * 6. Compared
 * ---------------------------------------------------------------------------
 *
 * - Unreal: Landscape Layer Blend (weight, height and alpha blends) in a
 *   landscape material, layers from weightmaps; a material function
 *   per projection (WorldAlignedTexture for triplanar). Taken: the height
 *   blend, triplanar as a layer's choice. Not taken: a material graph.
 * - Unity HDRP TerrainLit: up to eight layers a terrain, a mask map
 *   (metallic, occlusion, height, smoothness) a layer, a height-based
 *   transition. Taken: the mask with height. Different: its weights come
 *   from control textures, here from the caller's vertices.
 * - Frostbite (Andersson, SIGGRAPH 2007): procedural shader splatting,
 *   masks per pixel in the shader. Not taken: the masks are the caller's.
 * - Hazel: none (above).
 *
 * ---------------------------------------------------------------------------
 * 7. Rend's contract kept
 * ---------------------------------------------------------------------------
 *
 * - The material is an rbtrend_material (rbtrendmaterial.h): destroyed with
 *   rbtrend_material_destroy(), its GPU objects through the release queue;
 *   rbtrend_material_copy() copies it. Its block is staged by each setter
 *   and copied in the next submission, ahead of what reads it, as the lit
 *   material's values are: a frame submitted reads what it was submitted
 *   with.
 * - Allocation: at create only (its record and its block of layer_count x
 *   96 bytes plus 16, at most 1,552 bytes); no setter allocates; drawing
 *   allocates nothing.
 * - RBT_RESULT and RBTREND_ERROR on every call; each check its own if, in
 *   parameter order, its own message; RBT_STALE, then RBTREND_ENTER on the
 *   material's instance, as the lit material's calls.
 * - RBTREND_LIT_LAYER_VALUES based, its type registered
 *   (RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES, 5039, after L66's 5038, or the
 *   next free when built), checked by rbtrend_base_check().
 * - Slang modules: rbtrend_lit_layered_material.slang (set 1's bindings and
 *   block), the split rbtrend_lit_surface.slang, two entry points
 *   (rbtrend_lit_layered_frag.slang, rbtrend_lit_layered_reflect_frag.slang),
 *   compiled at build time and embedded as every lit shader.
 * - No device feature: arrays, their views, flat integer interpolants and
 *   the sixteen interpolants (11 used) are on every backend.
 * - Not in a ray scene: a layered material has no entry in the instance's
 *   lit-material table (rbtrend_lit_material_create()'s); a TLAS's material
 *   table refuses one (RBT_RESULT_INVALID_ARGUMENT, its message saying so).
 * - Doxygen on every item; rbtrendlit.h is the header (the umbrella
 *   unchanged).
 *
 * ---------------------------------------------------------------------------
 * 8. Declarations (in rbtrendlit.h)
 * ---------------------------------------------------------------------------
 */

/** [ADDED] @brief The most layers a layered material has. Why: a layer's
 * index is a byte of location 7 and its values a row of the block; sixteen
 * covers a terrain's materials with room (Whittaker's nine biomes of one or
 * two surfaces each). */
#define RBTREND_LIT_LAYERED_MAX_LAYERS 16u
/** [ADDED] @brief The layers' weights a fragment receives (locations 5 and
 * 6), and indices (location 7). Why: two float4 and two uints. */
#define RBTREND_LIT_LAYERED_INPUT_LAYERS 8u
/** [ADDED] @brief The most layers a pixel blends: its heaviest. Why: caps
 * a pixel's reads at 4 x 9 (section 3). */
#define RBTREND_LIT_LAYERED_PIXEL_LAYERS 4u
/** [ADDED] @brief The index at location 7 that names no layer. */
#define RBTREND_LIT_LAYERED_NO_LAYER 0xFFu

/*
 * [CHANGED: appended] RBTREND_LIT_SHADER:
 *
 *	/ **
 *	 * The layered surface (section 3): reads locations 0 to 10 and the
 *	 * layered material at set 1 (rbtrend_lit_get_layered_material_layout()),
 *	 * the scene at set 0 as the lit surface does. Opaque: opacity is the
 *	 * colour's alpha at location 4; no cut-out.
 *	 * /
 *	RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT = 9,
 *	/ **
 *	 * The layered surface writing the two reflection targets as
 *	 * RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT does, from the blended
 *	 * surface.
 *	 * /
 *	RBTREND_LIT_SHADER_LAYERED_SURFACE_REFLECT_FRAGMENT = 10,
 *
 * Why: the layered surface must take part wherever the lit surface does,
 * reflecting passes included.
 */

/*
 * [CHANGED: the file comment] "A caller's vertex shader" gains, for the
 * layered shaders, six more outputs after the five, every one written:
 * - `layout(location = 5) out vec4` and `layout(location = 6) out vec4`:
 *   the weights of the eight entries of location 7, each at least 0;
 * - `layout(location = 7) flat out uvec2`: the eight entries' layer
 *   indices, a byte each (entry e in byte e % 4 of component e / 4),
 *   RBTREND_LIT_LAYERED_NO_LAYER for none;
 * - `layout(location = 8) out vec3`: the triplanar place, in the
 *   projection's frame, in the units the layers' uv_scale divides (0 where
 *   no layer is triplanar);
 * - `layout(location = 9) out vec3`: the normal in the projection's frame
 *   (any length; 0 where no layer is triplanar);
 * - `layout(location = 10) flat out vec4`: the projection's frame's turn
 *   into the positions' space, a unit quaternion (x, y, z, w); (0, 0, 0, 1)
 *   where they are one.
 * Location 2 is the texture coordinate UV layers read, times their scale.
 * "Sets" gains: a layered pipeline names the scene's layout at slot 0 and
 * rbtrend_lit_get_layered_material_layout() at slot 1.
 */

/**
 * [ADDED] @brief The layout of set RBTREND_LIT_MATERIAL_SET of a layered
 * pipeline: what a layered material fills (the block, three arrays and a
 * sampler). The instance's: don't destroy it.
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

/** [ADDED] @brief The maps of a layered material, each a 2D array whose
 * layer i is layer i's. */
typedef uint32_t RBTREND_LIT_LAYERED_MAP;
enum {
	/** rgb times the layer's base colour; alpha not read. An `_SRGB` format
	 * for colours stored as sRGB. White where none is given. */
	RBTREND_LIT_LAYERED_MAP_BASE_COLOR = 0,
	/** A tangent-space normal map, stored 0 to 1 (z up). Flat where none. */
	RBTREND_LIT_LAYERED_MAP_NORMAL = 1,
	/** Occlusion in red, roughness in green, metalness in blue (as
	 * RBTREND_LIT_MAP_ORM), the layer's height in alpha, 0 to 1. (1, 1, 1,
	 * 0.5) where none. */
	RBTREND_LIT_LAYERED_MAP_ORMH = 2,
};

/** [ADDED] @brief How a layer's maps are read (section 3). */
typedef uint32_t RBTREND_LIT_LAYER_PROJECTION;
enum {
	/** Refused. */
	RBTREND_LIT_LAYER_PROJECTION_NONE = 0,
	/** At the texture coordinate (location 2) times uv_scale. */
	RBTREND_LIT_LAYER_PROJECTION_UV = 1,
	/** On three planes of the triplanar place (location 8) times uv_scale. */
	RBTREND_LIT_LAYER_PROJECTION_TRIPLANAR = 2,
};

/**
 * [ADDED] @brief A layer's values, each multiplying its maps as a lit
 * material's do. Why based: a layer's values will grow (a detail map's
 * scale, a distance blend's), each a chained struct.
 */
typedef struct RBTREND_LIT_LAYER_VALUES {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES, and its
	 * chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Linear, times the base colour map. */
	float base_color[3];
	/** 0 to 1, times the ORMH map's green; below 0.045 taken as 0.045. */
	float roughness;
	/** 0 to 1, times its blue. */
	float metalness;
	/** A dielectric's reflectance, 0 to 1 (0.5 is 4%). */
	float specular;
	/** How strongly the normal map bends the normal: 1 as the map is. */
	float normal_scale;
	/** How much of the ORMH map's red is applied, 0 to 1. */
	float occlusion_strength;
	/** How much the height (alpha) shifts the layer's share (section 3): 0
	 * to 4. */
	float height_contrast;
	/** Repeats a unit of the texture coordinate or triplanar place: finite,
	 * above 0 (1 / a tile's size, for a caller whose units are metres). */
	float uv_scale;
	/** An RBTREND_LIT_LAYER_PROJECTION. */
	RBTREND_LIT_LAYER_PROJECTION projection;
} RBTREND_LIT_LAYER_VALUES;

/*
 * [ADDED] rbtcore.h: RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES = 5039, /< *
 * RBTREND_LIT_LAYER_VALUES. * / (after L66's 5038; the next free when built).
 */

/**
 * [ADDED] @brief Creates a layered material: an rbtrend_material of
 * rbtrend_lit_get_layered_material_layout(), bound at set
 * RBTREND_LIT_MATERIAL_SET of a layered pipeline.
 *
 * Every layer starts white, half rough (0.5), a dielectric of specular
 * 0.5, normal scale 1, occlusion strength 1, height contrast 0, uv_scale 1,
 * PROJECTION_UV; the blend depth 0.2; every map Rend's neutral array of
 * layer_count layers; Rend's sampler (linear, mipmapped, repeating). It
 * takes no entry in the lit-material table: a TLAS can't name it.
 *
 * @param[in]  instance     Instance the material is of.
 * @param[in]  layer_count  1 to RBTREND_LIT_LAYERED_MAX_LAYERS.
 * @param[out] out_material Receives the material; NULL on failure.
 * @param[out] out_error    Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance is NULL,
 *         @p layer_count is out of range, or @p out_material is NULL (each
 *         its own check); or the code it can't be made with.
 */
RBT_RESULT rbtrend_lit_layered_material_create(rbtrend_instance instance, uint32_t layer_count, rbtrend_material *out_material, RBTREND_ERROR *out_error);

/**
 * [ADDED] @brief Sets one layer's values, staged and copied in the next
 * submission as rbtrend_lit_material_set_values()'s are.
 *
 * @param[in] material Material to change.
 * @param[in] layer    Below the material's layer count.
 * @param[in] values   The values, based (RBT_STRUCT_TYPE_REND_LIT_LAYER_
 *                     VALUES). Only read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT,
 *         RBT_RESULT_STRUCT_WRONG_TYPE or RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN,
 *         with nothing changed, if @p material is NULL or not of the layered
 *         layout, @p layer is out of range, @p values is NULL or of another
 *         type, or a value is not finite or out of its range (each its own
 *         check, the field named).
 */
RBT_RESULT rbtrend_lit_layered_material_set_layer(rbtrend_material material, uint32_t layer, const RBTREND_LIT_LAYER_VALUES *values, RBTREND_ERROR *out_error);

/**
 * [ADDED] @brief Sets the blend depth d of section 3: 0 (only the highest
 * layer shows, a hard edge where heights cross) to 1 (heights barely
 * matter). Why apart from the layers': it is the material's, one for the
 * blend.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT, nothing changed, for a
 *         NULL or not layered @p material, or a depth not finite or out of
 *         0 to 1.
 */
RBT_RESULT rbtrend_lit_layered_material_set_blend_depth(rbtrend_material material, float depth, RBTREND_ERROR *out_error);

/**
 * [ADDED] @brief Puts a texture array in one of a layered material's maps,
 * or puts back Rend's neutral one.
 *
 * @param[in] material Material to change.
 * @param[in] map      Which map.
 * @param[in] view     A SHADER_RESOURCE view of shape
 *                     RBTREND_TEXTURE_VIEW_SHAPE_2D_ARRAY of at least the
 *                     material's layer count of layers, in a format a shader
 *                     reads as floats; or NULL for the neutral array. It must
 *                     outlive the material, or be replaced before the next
 *                     compile after it goes.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT, nothing
 *         changed, if @p material is NULL or not layered, @p map is not an
 *         RBTREND_LIT_LAYERED_MAP, or @p view is of another instance, not a
 *         2D array, or has too few layers (each its own check).
 */
RBT_RESULT rbtrend_lit_layered_material_set_map(rbtrend_material material, RBTREND_LIT_LAYERED_MAP map, rbtrend_texture_view view, RBTREND_ERROR *out_error);

/**
 * [ADDED] @brief Puts the sampler every map is read through, or puts back
 * Rend's, as rbtrend_lit_material_set_sampler(). A caller wanting
 * anisotropy (ground at grazing angles) gives a sampler made with it.
 */
RBT_RESULT rbtrend_lit_layered_material_set_sampler(rbtrend_material material, rbtrend_sampler sampler, RBTREND_ERROR *out_error);

/*
 * ---------------------------------------------------------------------------
 * 9. Breaks; tests; budget
 * ---------------------------------------------------------------------------
 *
 * BREAKS: none. Appended: two RBTREND_LIT_SHADER values, a map enum, a
 * projection enum, a based struct and its type, five calls, four
 * constants. The lit surface's shaders are rebuilt from the split module:
 * the same pixels (test 1).
 *
 * TESTS (rabbittustests' GPU suite, Metal under API and shader validation,
 * warnings fatal; D3D12 and Vulkan owed; and Rabbittus' contract tests):
 * 1. Every existing lit and reflect case: the same pixels as before the
 *    split, compared at their run-to-run difference.
 * 2. Two layers of flat known colours on a quad, weights 0 to 1 across it:
 *    each read-back pixel within the lit shading of the weighted blend.
 * 3. The height blend: equal weights, layer 1's height 1 and layer 0's 0,
 *    contrast 1, depth 0.2: layer 1 only; depth 1: both.
 * 4. Four layers a pixel: eight weights, the four heaviest drawn (a fifth's
 *    colour absent).
 * 5. Triplanar: a cube's faces each read their plane (a checker's squares
 *    square on every face), the turn at location 10 a quarter turn moving
 *    the planes with it; its normals lit as the faces' (a light along +X
 *    lights the +X face's bumps).
 * 6. The reflect variant: its normal and roughness target the blend's.
 * 7. The material's calls: every refusal, one check each, nothing changed;
 *    a view of a 2D texture refused, of too few layers refused; a TLAS
 *    naming it refused; values staged and read by the next submission
 *    only.
 * 8. Destroyed handles (make_destroyed_tests.py), Debug's overlap check,
 *    test_frame_allocations with a layered draw: none.
 *
 * BUDGET: reads a pixel as section 3 (3 to 6 typical, 36 at worst against
 * the lit surface's 4); ALU: the cut (eight compares), the height blend
 * (four), triplanar weights: small beside the reads. Measured by the first
 * caller's bench (card L68) on the M5 Pro before its sets are fixed.
 *
 * ---------------------------------------------------------------------------
 * 10. Records, when it is built
 * ---------------------------------------------------------------------------
 *
 * RBTREND_TODO.md: an item, with D3D12 and Vulkan owed; rabbittus/CLAUDE.md,
 * "Rend": the layered surface as part of the lit pipeline (one lighting,
 * set 1 its own), what it is for and what it does not do (no clear coat,
 * sheen, transmission or cut-out a layer; not traced). The ABI snapshot;
 * the misuse generator; .git/COMMIT_DRAFT.
 *
 * ---------------------------------------------------------------------------
 * 11. Open questions (in card L68)
 * ---------------------------------------------------------------------------
 *
 * Q-L1. Where the blend lives: a layered surface of Rend's sharing the lit
 *     lighting - recommended; or Rend's lighting as a module callers import
 *     (carded for a second custom surface); or a caller's own lighting.
 * Q-L2. Maps: 2D arrays, one a map - recommended; or an atlas; or bindless
 *     (when Rend has it).
 * Q-L3. What a layer has: base colour, normal, ORMH and their values -
 *     recommended; or the lit material's whole set (clear coat, sheen,
 *     transmission) a layer: a larger block for surfaces a terrain never
 *     has.
 * Q-L4. A dithered fade (a fragment discarded by a screen pattern, for
 *     scatter and levels of detail): not here - recommended, carded for
 *     the lit surface when a fade is needed by the pixel; or now, as a
 *     cut-out mode of both surfaces.
 */
