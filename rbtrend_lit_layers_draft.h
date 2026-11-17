/*
 * rbtrend_lit_layers_draft.h - Rend's layered lit surface: a lit fragment
 * shader that blends up to sixteen layers of a material, read from texture
 * arrays, by weights its caller's vertex shader hands it, with height
 * blending and triplanar reads by layer, and lights the blend with the lit
 * surface's one lighting (card L68, its Rend half; the caller's half and
 * the session that asked are named on the card).
 *
 * DRAFT, 2026-11-17, version 4: version 1, seven critique passes, version
 * 2 (its questions decided as recommended, Q-L1a to Q-L4a), then at the
 * user's asking a second round of seven passes (passes 8 to 14: general
 * purpose, industry standard), each fixed before the next: the revision
 * notes below. APPROVED by the user 2026-11-17 as version 3; anisotropy
 * above 1 waits on Carrottus GPU's anisotropy card (S49), Rend's sampler
 * check after it. No version raise while Rabbittus' 0.3 is untagged.
 * Amended by the third critique round, 2026-11-17, at the user's asking
 * (passes 15 to 21, version 4: chiefly against the built shaders).
 * Written unattended on the Mac (Apple M5 Pro) against
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
 * Revision notes, second round (passes 8 to 14, 2026-11-17, over version 2)
 * ---------------------------------------------------------------------------
 *
 * Asked: general infrastructure to industry standard, not one caller's.
 * The decided answers (Q-L1a to Q-L4a) are kept; none was shown wrong.
 *
 * Pass 8 (general purpose). 1. Weights come only from the vertex shader:
 * a caller painting at texel resolution (Unity's and Unreal's weightmaps)
 * has no way in. Said, and carded: weights read from a texture the caller
 * binds, a later input mode of the same surface (section 5, Q-later); a
 * caller with weightmaps meanwhile reads them in its vertex shader at a
 * dense mesh's vertices. 2. More than sixteen layers (Godot's Terrain3D
 * takes 32): said how (a material a region, section 5). 3. Location 4's
 * colour multiplies every layer's base colour: said as a caller's per-vertex
 * tint or macro variation (section 3). Looked at: no name, limit or
 * example below is one caller's; the triplanar frame is any caller's frame
 * (a turning body, a tilted plane, the identity).
 *
 * Pass 9 (industry standard). 1. The normals' blend space was unsaid.
 * Changed: each layer's normal into the positions' space, then weighted,
 * normalised (section 3, the weight blend of Unity's and Unreal's terrain
 * shaders; Golus's whiteout for each triplanar plane). 2. Anti-tiling
 * (a distance blend, macro variation, a rotation per layer, stochastic
 * tiling): one later stage, each a chained value of the based layer
 * struct, so nothing frozen closes one off (section 4). 3. Compared: Godot's
 * Terrain3D added (section 6).
 *
 * Pass 10 (the C API). 1. set_blend_depth and set_sampler lacked their
 * @param and @return. Changed. 2. Each check of set_layer named, its order
 * (section 8). 3. The enums follow rbtrendlit.h's neighbours (typedef
 * uint32_t and an anonymous enum, as RBTREND_LIT_MAP): checked, kept.
 *
 * Pass 11 (data and allocation). 1. "96 bytes a layer" had no layout
 * behind it. Changed: the block's layout written out, 48 bytes a layer in
 * three float4, 16 for the blend depth, at most 784 bytes; the C side's
 * offsets statically asserted, and tested against the shader's reflection
 * (section 7, test 11).
 *
 * Pass 12 (threads and errors). 1. Each refusal's message named (section
 * 8). Kept: staged values, one caller at a time, no Thread: exception.
 *
 * Pass 13 (the GPU). 1. Version 2 read a layer's base colour and normal
 * "only for layers whose blended weight is above 0": a sample with
 * implicit derivatives in control flow that differs between a 2x2 quad's
 * pixels is undefined (SPIR-V's and HLSL's rule), and seams of wrong mips
 * would show along every ecotone. Changed: the gradients of the texture
 * coordinate and of the triplanar place are taken once, before any branch,
 * and every read in the blend is a sample with those gradients (section
 * 3). 2. Anisotropy: a caller's sampler above 1 is outside Vulkan's rules
 * until Carrottus GPU reports and enables the device's anisotropic
 * filtering; the sampler setter says so, and Rend's sampler check waits
 * on that report (section 7, a prerequisite). 3. Shader permutations
 * counted: two entry points; projection and layer count are values, not
 * permutations. 4. Interpolants: eleven locations, under every backend's
 * floor; said. 5. Video: a map is any 2D array view a caller fills, so a
 * layer's texels may change each frame; nothing frozen rules video out.
 *
 * Pass 14 (tests, records). 1. Tests added: no seam along a weight
 * boundary (gradients), the block's offsets, normals' space, an ecotone
 * of UV and triplanar layers lit consistently. 2. Records: the Carrottus
 * prerequisite named (section 10).
 *
 * ---------------------------------------------------------------------------
 * Revision notes, third round (passes 15 to 21, 2026-11-17, over version 3)
 * ---------------------------------------------------------------------------
 *
 * Asked: critique again, the earlier rounds' fixes not repeated, pass 20's
 * lens (pass 6's) against the code as built. The decided answers (Q-L1a to
 * Q-L4a) are kept; none was shown wrong.
 *
 * Pass 15 (general purpose). 1. "The first caller's draw record does it
 * in double", "eye-relative, for the first caller", "the first caller's: a
 * terrain chunk's" in section 4 assumed a caller. Changed: said of any
 * caller (rabbittus/CLAUDE.md, "Rabbittus doesn't know who calls it").
 * 2. The height blend could never give back a caller's own weights (pass
 * 19): a CAD site or a painted wall wanting the plain weighted mix had no
 * setting for it. Changed with pass 19's fix: a large blend depth is that.
 *
 * Pass 16 (industry standard). 1. The lit material has
 * rbtrend_lit_material_get_values(); the layered one had no way to read a
 * layer back (an editor's round trip, a test's). Changed:
 * rbtrend_lit_layered_material_get_layer [ADDED], as get_values. 2. The
 * height blend's large-depth limit compared with Unity HDRP's height
 * transition, whose result is multiplied by the layers' weights, so a wide
 * transition returns the painted weights: taken (pass 19).
 *
 * Pass 17 (the C API). 1. rbtrend_material_set_uniform() works on every
 * material by its block's names, a layered one included, bypassing
 * set_layer's checks. Changed: said (section 7: allowed as on any
 * material, unchecked, the block's member names no part of this
 * contract). 2. set_blend_depth's range 0 to 1 was a limit with no reason
 * (pass 19). Changed: finite, at least 0.
 *
 * Pass 18 (data and allocation). 1. "Its block of layer_count x 48 bytes
 * plus 16, at most 784": the block's size is the layout's, from the
 * shader's reflection, 784 whatever the count (a 1,024-byte range of the
 * material arena, ranges 256-aligned, rbtrendmaterial.h). Changed. 2. The
 * neutral arrays were "of layer_count layers" with nothing saying who
 * makes them or when. Changed: three 1 x 1 arrays of sixteen layers, the
 * instance's, made by the first layered create (section 7).
 *
 * Pass 19 (threads, determinism, errors; and the blend's arithmetic). 1.
 * Test 3 said "depth 1: both" for equal weights, heights 0 and 1 and
 * contrast 1: by the formula a = (0.5, 1.5), and max(0.5 - 1.5 + 1, 0) is
 * 0: layer 1 only. The formula was also unbounded the other way: a large d
 * mixed the kept layers towards equal shares whatever their weights (0.9
 * and 0.1 at d = 10: 52% and 48%), so "1: heights barely matter" was
 * wrong too. Changed: w' = w x max(a - max(a) + d, 0) (the band
 * Mishkinis's, the product by w Unity HDRP's height transition's), so d =
 * 0 is the hard edge, a growing d tends to the weights themselves, and a
 * layer's height wins only where it has weight; the cut keeps only
 * entries whose weight is above 0 (a zero-weight entry of high height
 * would otherwise take the pixel, or leave every w' 0); test 3's numbers
 * re-derived. 2. Kept: no thread, no new code, messages per check.
 *
 * Pass 20 (the GPU, against the code as built). 1. lit_shade()'s split as
 * written couldn't build: rbtrend_lit_surface.slang imports
 * rbtrend_lit_material (set 1's bindings 0 to 5) and its transmitted()
 * reads u_lit_material directly, so a layered shader importing it would
 * declare both materials' bindings at set 1, against Rend's rule that a
 * shader declares each set exactly as its layout holds it (rabbittus/
 * CLAUDE.md, "Build"; Metal refuses it). Changed: the lighting moves to a
 * module of its own, rbtrend_lit_lighting.slang, which imports the scene,
 * its bindings and the atmosphere and no material; every material value
 * the lighting reads (transmission, ior, thickness, attenuation, emission,
 * clear coat, sheen) is a field of its surface (section 2). 2. Location 9
 * duplicated location 1: the normal in the projection's frame is the
 * positions' normal turned back by the quaternion, and rotation commutes
 * with interpolation. Changed: removed; the quaternion moves to location 9;
 * the fragment turns location 1's normal by r's conjugate; 10 locations,
 * not 11. 3. An index at location 7 past the material's layer count read
 * an array layer the view may not have (clamped on Vulkan and D3D12,
 * Metal's unsaid). Changed: the block's first row carries the layer count,
 * and such an index is none. 4. The binding budget, counted against the
 * built set 0 (rbtrend_lit_bindings.slang, rbtrend_lit_scene.slang): set 0
 * has 11 sampled images and 3 samplers; the lit surface reads 15 images of
 * Vulkan's floor of 16 a stage (maxPerStageDescriptorSampledImages), the
 * layered one 14, and 4 samplers of 16. Said (section 7) with what it
 * leaves the later stages: two images. 5. Set 1's bindings numbered
 * (block 0, arrays 1 to 3, sampler 4; D3D12 b0, t1 to t3, s4 in space1).
 * 6. Checked and kept: four set slots (CRGPU_MAX_DESCRIPTOR_SET_LAYOUTS),
 * Rend's two and the caller's two; the shader enum's next values 9 and 10
 * (the built enum ends at 8); struct type 5039 after the atmosphere's 5037
 * and L66's 5038; the neutral ORMH (1, 1, 1, 0.5) matching the cook's
 * neutral height; the normal map's three channels read, as L70's cook keeps
 * them (no two-channel normal format); flat integer interpolants and the
 * array slice as a coordinate, never a descriptor index, so no
 * non-uniform indexing.
 *
 * Pass 21 (tests, records). 1. Tests added: an index past the layer count
 * drawn as none; a zero-weight entry of high height not taking the pixel;
 * get_layer's round trip; the layered fragment's reflection holding set
 * 0's sixteen bindings and set 1's five, and at most 16 sampled images.
 * 2. Records: rbtrend_lit_lighting.slang in RABBITTUS_SHADER_MODULES.
 *
 * Consistency pass, 2026-11-17 (after the third round, across the drafts;
 * no version raise): 1. The neutral ORMH's height is the byte 128
 * (128/255, 0.502), the value an 8-bit source holds for 0.5, so a layer
 * with no height image and the neutral array give the same height (pass
 * 20's "(1, 1, 1, 0.5)" read so). 2. The caller's draw rows a vertex
 * shader makes the quaternion from must be a rotation with no scale; the
 * first caller's terrain draft now says so of its instance.
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
 *   set 1's sixteen: the block at 0, the base colour, normal and ORMH
 *   arrays at 1 to 3, the sampler at 4 (D3D12: b0, t1 to t3, s4, space1).
 *   Set 0, the scene's, is unchanged.
 * - TWO FRAGMENT SHADERS, appended to RBTREND_LIT_SHADER:
 *   LAYERED_SURFACE_FRAGMENT and its reflecting twin, as the lit surface and
 *   RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT are.
 * - THE CALLER'S VERTEX SHADER writes the lit surface's five outputs
 *   (locations 0 to 4) and five more: the weights of eight layers (5, 6),
 *   the eight layers' indices (7, flat), the triplanar place in the
 *   projection's frame (8), and that frame's turn into the positions'
 *   space (9, flat). The normal in the projection's frame is location 1's
 *   turned back by that turn (no location of its own: turning commutes
 *   with interpolation).
 * - ONE LIGHTING. lit_shade() (rbtrend_lit_surface.slang) is split in two,
 *   the lighting into a module of its own, rbtrend_lit_lighting.slang,
 *   which imports the scene, set 0's bindings and the atmosphere and no
 *   material (a shader declares each set exactly as its layout holds it,
 *   so the lighting can't import set 1's lit bindings, which the built
 *   module's transmitted() reads today). It holds the surface struct (base
 *   colour, opacity, the eye-facing geometric normal, the shading normal,
 *   roughness, metalness, specular, the material's occlusion, emission,
 *   clear coat and its roughness, sheen and its roughness, transmission,
 *   ior, thickness and attenuation), the eye-facing geometric frame both
 *   surfaces' normal maps start from, and the lighting of a surface (every
 *   light, shadows, the environment, the view's ambient occlusion and bent
 *   normals, the atmosphere's aerial perspective, exposure and encoding,
 *   the reflect variant's outputs). rbtrend_lit_surface.slang reads its
 *   one material into the surface (its cut-out discard stays there); the
 *   layered module reads and blends its layers into the same surface,
 *   the coat, sheen, transmission and emission 0; both call the one
 *   lighting. No second copy of any light.
 *
 * ---------------------------------------------------------------------------
 * 3. What a pixel does (the definition the header carries)
 * ---------------------------------------------------------------------------
 *
 * Inputs: w[8] (locations 5 and 6), idx[8] (location 7: byte e of the two
 * uints is entry e's layer, 0xFF for none; an index at or past the
 * material's layer count, which the block's first row carries, is none),
 * the texture coordinate t (location 2), the normal n (1), the tangent
 * (3), the colour (4), the triplanar place q (8, the projection's frame),
 * the turn r (9); the normal in the projection's frame m = r* n r (r's
 * conjugate; where n is 0, the q.xz plane alone is read).
 * 0. GRADIENTS, before any branch: the screen-space derivatives of t and q
 *    (ddx, ddy), taken once while every pixel of the quad runs the same
 *    code. Every read below samples with them (SampleGrad, times uv_scale
 *    for its layer), never with implicit derivatives: which layers a pixel
 *    keeps and whether it reads them differ between neighbouring pixels,
 *    and an implicit derivative in such control flow is undefined (it
 *    would pick wrong mips in a line along every edge between layers).
 * 1. CUT: the RBTREND_LIT_LAYERED_PIXEL_LAYERS (4) largest w above 0 with
 *    a layer (ties to the lower entry), renormalised to sum 1; with none
 *    (every w 0, or no entry a layer) it reads layer idx[0] whole, or
 *    layer 0 where idx[0] is none.
 * 2. HEIGHTS: for each kept layer, its ORMH read (by its projection,
 *    below); h its alpha, c its height_contrast; a = w + c h; with the
 *    material's blend depth d: w' = w x max(a - max(a) + d, 0),
 *    renormalised. The band is Mishkinis's (2013): a layer shares the
 *    pixel only where its a is within d of the highest; the product by w
 *    is Unity HDRP's height transition's, so a layer's height wins only
 *    where it has weight, and as d grows w' tends to the weights
 *    themselves (the plain weighted mix, for a caller that wants it). At
 *    d = 0 only the largest a is left, whole (ties to the lower entry).
 *    With every c 0, d alone sharpens the weights' ecotone: at d = 0.2 a
 *    layer shows where its weight is within 0.2 of the heaviest's.
 * 3. READS: for each layer with w' above 0, its base colour and normal
 *    (the ORMH already read), by its projection:
 *    - UV: at t x uv_scale, the normal map through the tangent frame
 *      (locations 1 and 3), as the lit surface's;
 *    - TRIPLANAR: at q x uv_scale on the three planes (q.zy, q.xz, q.xy),
 *      weighted by |m|^4 normalised (m from location 1, above); each
 *      plane's normal by the whiteout
 *      blend into the projection's frame (Golus, "Normal Mapping for a
 *      Triplanar Shader", 2017), summed by the weights, normalised, turned
 *      by r into the scene's space.
 * 4. BLEND: base colour, ORMH's occlusion, roughness and metalness, the
 *    values (base colour factor, roughness, metalness, specular, normal
 *    scale, occlusion strength), each the w'-weighted sum. Normals: each
 *    layer's, its normal_scale applied, is first in the positions' space
 *    (a UV layer's through the tangent frame, a triplanar one's through
 *    r), then the w'-weighted sum, normalised: the weight blend of Unity's
 *    and Unreal's terrain shaders, in one space whatever mix of projections
 *    a pixel has. Times the colour at location 4 (rgb times the base
 *    colour, a the opacity), as the lit surface's: a caller's per-vertex
 *    tint, or a macro variation it works out per vertex, multiplies every
 *    layer.
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
 * palette the caller chooses per draw or per primitive (a mesh chunk's,
 * say); location 7 is flat, so it is read from one vertex of a primitive
 * (the provoking one), and every vertex of a primitive must write the same
 * palette or its weights would be read against another vertex's layers. A
 * pixel blends its four heaviest. Four is where a fifth's share is almost
 * always invisible, and it caps the reads at 4 x 9.
 *
 * TRIPLANAR BY LAYER, NOT BY SLOPE. A layer's projection says how it is
 * read wherever it is; the caller's rules decide where a layer is. So the
 * nine reads are paid only where a triplanar layer has weight, never by a
 * slope test every pixel makes.
 *
 * THE PROJECTION'S FRAME FROM THE CALLER. Rend's positions are in the
 * caller's space (eye-relative, for a caller drawing far from its origin),
 * which may move with the eye and turn with nothing; a triplanar read must
 * be fixed to the surface it textures (a body's axes) and exact far from
 * any origin. So the caller hands the place in the projection's frame,
 * already reduced by its period (in double on the CPU, where its places
 * are large), and the frame's turn as a unit quaternion (flat); the normal
 * in that frame is location 1's turned back. Rend never sees a body or a
 * period. A caller whose frame is its positions' own passes the identity.
 *
 * WHAT GROWS LATER, AND HOW. A layer's values are a based struct, so each
 * later value is a chained struct and a layout's change is Rend's alone:
 * - the anti-tiling stage, its candidates weighed together when a caller
 *   needs one: a second read at a larger scale blended by distance
 *   (Unreal's and Unity's distance blend), a macro variation (a
 *   low-frequency colour over a layer), a rotation and offset per layer
 *   (Godot's Terrain3D turns its reads per texel), stochastic tiling
 *   (Heitz and Neyret, "High-Performance By-Example Noise using a
 *   Histogram-Preserving Blending Operator", 2018);
 * - a cover input: a weight added per pixel from a value the caller sets
 *   per draw and the normal's slope (snow, wetness, dust that change
 *   continuously), a later stage of the same surface;
 * - weights from a texture the caller binds (a weightmap or control map,
 *   Unity's and Unreal's input) in place of locations 5 to 7, a later
 *   input mode of the same surface (another entry point, the same
 *   lighting).
 * None needs a change to what version 3 freezes.
 *
 * ---------------------------------------------------------------------------
 * 5. Who else uses it
 * ---------------------------------------------------------------------------
 *
 * - A game engine's terrain with biomes: the first to ask for it.
 * - A painted terrain of any engine on Rend: weights from a caller's paint.
 * - A CAD model's site ground (graded earth, gravel, paving and grass
 *   painted per vertex on a site's surface): the same material, its
 *   weights from the caller's model.
 * - A wall or floor with vertex-painted wear, moss or puddles' damp.
 * Each fits as written: weights and a palette from its vertices, the
 * triplanar inputs left 0 where every layer is UV. A caller with more
 * than sixteen materials over one surface makes a material a region
 * (sixteen a draw), as Unreal's components bound their layers. A caller
 * whose weights are painted per texel reads its weightmap in its vertex
 * shader over a mesh dense enough, until the texture input mode (section
 * 4) is built.
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
 * - Godot's Terrain3D: texture arrays as here, up to 32 textures, a
 *   control map of two ids and a blend a texel whose four nearest texels
 *   the shader blends by hand, a UV rotation and scale a texel, a slope
 *   rule in the shader. Taken: arrays a map. Here: four layers a pixel by
 *   height where it has two; sixteen layers a material where it has 32;
 *   rotation a later chained value (section 4).
 * - Height blending: Mishkinis (2013), as Unity's and Unreal's height
 *   blends; triplanar normals: Golus (2017), whiteout per plane.
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
 * - Allocation: at create only (its record, and its block, which is the
 *   layout's size from the shader's reflection, 784 bytes whatever the
 *   layer count, in a 1,024-byte range of the instance's material arena,
 *   ranges 256-aligned: rbtrendmaterial.h); the first layered create of an
 *   instance also makes its three neutral arrays (1 x 1, sixteen layers:
 *   white, the flat normal (0.5, 0.5, 1), ORMH (1, 1, 1, and a height of
 *   the byte 128, 128/255, as an 8-bit source writes 0.5)) and their
 *   views, kept by the instance; no setter allocates; drawing allocates
 *   nothing. The block may also be written with
 *   rbtrend_material_set_uniform(), as any material's: nothing is checked
 *   then, and its member names are no part of this contract (the setters
 *   are).
 * - THE BLOCK'S LAYOUT (shared with the GPU, so explicit): at offset 0 the
 *   blend depth, the layer count as a uint, and two of 0 (a float4); then a
 *   layer at 16 + 48 i, three float4: (base_color.rgb, roughness),
 *   (metalness, specular,
 *   normal_scale, occlusion_strength), (height_contrast, uv_scale,
 *   projection as a uint, 0). The same in std140 and std430 and HLSL's
 *   constant buffer packing (no member crosses a 16-byte boundary); the
 *   C++ writer's offsets are static_asserted, and a contract test reads
 *   the shader's build-time reflection and checks it against them (test
 *   11).
 * - RBT_RESULT and RBTREND_ERROR on every call; each check its own if, in
 *   parameter order, its own message; RBT_STALE, then RBTREND_ENTER on the
 *   material's instance, as the lit material's calls.
 * - RBTREND_LIT_LAYER_VALUES based, its type registered
 *   (RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES, 5039, after L66's 5038, or the
 *   next free when built), checked by rbtrend_base_check().
 * - Slang modules: rbtrend_lit_layered_material.slang (set 1's bindings and
 *   block), rbtrend_lit_lighting.slang (the lighting, importing no
 *   material; section 2), rbtrend_lit_surface.slang reduced to the lit
 *   material's reads, two entry points
 *   (rbtrend_lit_layered_frag.slang, rbtrend_lit_layered_reflect_frag.slang),
 *   compiled at build time and embedded as every lit shader.
 * - No device feature: arrays, their views, flat integer interpolants and
 *   the sixteen interpolants (10 used) are on every backend (Vulkan's
 *   floor is 64 output components, sixteen locations; D3D12's 32
 *   registers; Metal's 124 components). The array slice is a texture
 *   coordinate, never a descriptor index, so nothing indexes descriptors
 *   non-uniformly.
 * - THE BINDING BUDGET, counted against the built shaders: set 0 holds 11
 *   sampled images (shadow atlas, two environment cubes, the BRDF table,
 *   ambient occlusion, scene colour, four directional masks, the
 *   atmosphere's tables), 3 samplers, a uniform and a storage buffer. The
 *   lit surface reads 15 sampled images in its fragment stage, the layered
 *   one 14 (11 and 3), against Vulkan's floor of 16 a stage
 *   (maxPerStageDescriptorSampledImages; D3D12 and Metal allow far more);
 *   samplers 4 of 16 (Vulkan's and Metal's floor). So the later stages
 *   (section 4) have two images before a device at the floor: weights from
 *   a texture takes one; an anti-tiling stage's maps share the arrays
 *   (more layers of them) or the last. A contract test holds the count
 *   (test 14). Set slots: Rend's 0 and 1, the caller's 2 and 3, of
 *   Carrottus GPU's four (CRGPU_MAX_DESCRIPTOR_SET_LAYOUTS).
 * - SHADERS, COUNTED: two entry points. A layer's projection and the
 *   material's layer count are values the shader reads, so there is no
 *   permutation by either; the branches on them are per layer, their
 *   reads with explicit gradients (section 3).
 * - ANISOTROPY, A PREREQUISITE BELOW. A sampler with max_anisotropy above
 *   1 is outside Vulkan's rules unless the device was created with
 *   samplerAnisotropy, and its level past maxSamplerAnisotropy; Carrottus
 *   GPU reports neither and enables neither today (its D3D12 and Metal
 *   backends take up to 16 on every device). Before a layered material is
 *   read through an anisotropic sampler on Vulkan: a Carrottus GPU draft of
 *   its own (card S49: a feature bit and a limit), and Rend's sampler create refusing
 *   a level the device can't take (RBT_RESULT_INVALID_ARGUMENT, its message
 *   naming the level and the limit), for every sampler, not only this
 *   material's. Nothing in this draft's shape waits on it.
 * - VIDEO: a map is any 2D array view of the instance, filled by the
 *   caller as it likes; a layer whose texels change each frame (a video's
 *   frame converted to RGBA by the caller) is drawn as written. No queue,
 *   format or colour space is added or fixed here.
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
 *	 * The layered surface (section 3): reads locations 0 to 9 and the
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
 * layered shaders, five more outputs after the five, every one written:
 * - `layout(location = 5) out vec4` and `layout(location = 6) out vec4`:
 *   the weights of the eight entries of location 7, each at least 0;
 * - `layout(location = 7) flat out uvec2`: the eight entries' layer
 *   indices, a byte each (entry e in byte e % 4 of component e / 4),
 *   RBTREND_LIT_LAYERED_NO_LAYER for none (an index at or past the
 *   material's layer count reads as none); the same at every vertex of a
 *   primitive (a flat output is read from one of them);
 * - `layout(location = 8) out vec3`: the triplanar place, in the
 *   projection's frame, in the units the layers' uv_scale divides (0 where
 *   no layer is triplanar);
 * - `layout(location = 9) flat out vec4`: the projection's frame's turn
 *   into the positions' space, a unit quaternion (x, y, z, w); (0, 0, 0, 1)
 *   where they are one. The triplanar weights and normals read location
 *   1's normal turned back by it.
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
 * PROJECTION_UV; the blend depth 0.2; every map the instance's neutral
 * array (sixteen layers, made by the instance's first layered create);
 * Rend's sampler (linear, mipmapped, repeating). It
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
 *         check, the field named). The checks, in order: @p material, its
 *         layout, @p layer, @p values, its base, then each field in
 *         declaration order, the message "layer <n>: <field> is <value>;
 *         it must be <range>".
 */
RBT_RESULT rbtrend_lit_layered_material_set_layer(rbtrend_material material, uint32_t layer, const RBTREND_LIT_LAYER_VALUES *values, RBTREND_ERROR *out_error);

/**
 * [ADDED, third round] @brief Reads one layer's values back: the last
 * set, staged or not. Why: as rbtrend_lit_material_get_values() for the
 * lit material, an editor's or a test's round trip.
 *
 * @param[in]  material   Material to read: of the layered layout.
 * @param[in]  layer      Below the material's layer count.
 * @param[out] out_values Receives the values; its base set by the caller
 *                        (RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES, next
 *                        NULL), as for set_layer.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise RBT_RESULT_INVALID_ARGUMENT,
 *         RBT_RESULT_STRUCT_WRONG_TYPE or RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN,
 *         nothing written, if @p material is NULL or not of the layered
 *         layout, @p layer is out of range, or @p out_values is NULL or of
 *         another type (each its own check, in that order).
 */
RBT_RESULT rbtrend_lit_layered_material_get_layer(rbtrend_material material, uint32_t layer, RBTREND_LIT_LAYER_VALUES *out_values, RBTREND_ERROR *out_error);

/**
 * [ADDED] @brief Sets the blend depth d of section 3, in the units of a
 * layer's weight plus its contrast times its height: 0 is a hard edge
 * where those cross (only the highest shows); 0.1 to 0.3 the usual height
 * blend; as d grows the blend tends to the weights alone (heights
 * ignored). Why apart from the layers': it is the material's, one for the
 * blend. Staged and copied in the next submission as set_layer's.
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
 * anisotropy (ground at grazing angles) gives a sampler made with it, on
 * a device that takes it (section 7's prerequisite).
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
 *          error, as for set_map's view.
 */
RBT_RESULT rbtrend_lit_layered_material_set_sampler(rbtrend_material material, rbtrend_sampler sampler, RBTREND_ERROR *out_error);

/*
 * ---------------------------------------------------------------------------
 * 9. Breaks; tests; budget
 * ---------------------------------------------------------------------------
 *
 * BREAKS: none. Appended: two RBTREND_LIT_SHADER values, a map enum, a
 * projection enum, a based struct and its type, seven calls (the layout
 * getter, create, four setters and get_layer), four constants. The lit
 * surface's shaders are rebuilt from the split modules (the lighting in
 * rbtrend_lit_lighting.slang): the same pixels (test 1).
 *
 * TESTS (rabbittustests' GPU suite, Metal under API and shader validation,
 * warnings fatal; D3D12 and Vulkan owed; and Rabbittus' contract tests):
 * 1. Every existing lit and reflect case: the same pixels as before the
 *    split, compared at their run-to-run difference.
 * 2. Two layers of flat known colours on a quad, weights 0 to 1 across it:
 *    each read-back pixel within the lit shading of the weighted blend.
 * 3. The height blend: equal weights, layer 1's height 1 and layer 0's 0,
 *    contrast 1 (a = 0.5 and 1.5), depth 0.2: layer 1 only; depth 2: both,
 *    a third and two thirds; contrast 0 and depth 100: within 1% of the
 *    weights themselves; depth 0: the higher a whole. A zero-weight entry
 *    of height 1 and contrast 4 beside a weighted one of height 0: the
 *    weighted layer whole (the cut drops it).
 * 4. Four layers a pixel: eight weights, the four heaviest drawn (a fifth's
 *    colour absent).
 * 5. Triplanar: a cube's faces each read their plane (a checker's squares
 *    square on every face), the turn at location 9 a quarter turn moving
 *    the planes with it; its normals lit as the faces' (a light along +X
 *    lights the +X face's bumps).
 * 6. The reflect variant: its normal and roughness target the blend's.
 * 7. The material's calls: every refusal, one check each, nothing changed;
 *    a view of a 2D texture refused, of too few layers refused; a TLAS
 *    naming it refused; values staged and read by the next submission
 *    only.
 * 8. Destroyed handles (make_destroyed_tests.py), Debug's overlap check,
 *    test_frame_allocations with a layered draw: none.
 * 9. Gradients: a quad whose weights switch from one layer to another
 *    between neighbouring pixels, both layers a checker of distinct
 *    colours a mip level, seen at a grazing angle: every pixel's colour
 *    that of the mip the gradients choose (no line of a wrong mip along
 *    the switch), on every backend.
 * 10. Normals' space: one pixel's blend of a UV layer and a triplanar
 *    layer, both flat normal maps, lit as the geometric normal; with one
 *    layer's map bent, the blend's normal the weighted sum in the
 *    positions' space, normalised, against the test's own arithmetic.
 * 11. The block: its offsets as section 7 says, read back from the
 *    shader's reflection (a contract test, no GPU).
 * 12. A sampler of another instance refused by set_sampler; set_blend_depth
 *    and set_sampler each refusal its own message.
 * 13. An index at location 7 at or past the material's layer count (a
 *    material of two layers, an entry naming layer 5 with the most
 *    weight): drawn as if that entry were none. get_layer returns what
 *    set_layer set, each refusal its own check.
 * 14. The layered fragments' reflection (a contract test, no GPU): set 0
 *    as the lit surface's sixteen bindings, set 1 exactly the layered
 *    layout's five, nothing of the lit material's; at most 16 sampled
 *    images and 16 samplers in the fragment stage, the lit surface's too.
 *
 * BUDGET: reads a pixel as section 3 (3 to 6 typical, 36 at worst against
 * the lit surface's 4); ALU: the cut (eight compares), the height blend
 * (four), triplanar weights, the gradients (two derivatives of t and of
 * q, once): small beside the reads. A sample with explicit gradients costs
 * what an implicit one does on current GPUs, and the same anisotropy. Measured by the first
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
 * the misuse generator; .git/COMMIT_DRAFT. The later stages (anti-tiling,
 * a cover input, weights from a texture: section 4, within section 7's
 * two sampled images) as RBTREND_TODO.md
 * items, each built when a caller asks. Carrottus GPU's anisotropic
 * filtering (a feature bit and a limit) as a card and draft of carrottus's
 * own, and Rend's sampler check after it (section 7).
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
 *
 * DECIDED 2026-11-17 (card L68): Q-L1a, Q-L2a, Q-L3a, Q-L4a. The second
 * round kept each; it added how the surface grows (section 4) and fixed
 * what the answers didn't decide (gradients, the normals' space, the
 * block's layout). The third round kept each; Q-L1a's one lighting now
 * names its module (rbtrend_lit_lighting.slang, importing no material),
 * without which the answer couldn't be built.
 */
