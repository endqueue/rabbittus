/*
 * rbtrend_transparency_draft.h - what a caller's transparency stage needs of
 * Rend's lit surfaces: a lit material's alpha mode. OPAQUE (its opacity
 * ignored, written whole), MASK (a cut-out, as alpha_cutoff above 0 is
 * today) and BLEND (written premultiplied by its opacity for a blended
 * pipeline, reading none of the view's screen-space inputs, which hold the
 * opaque surface behind it). RBTREND_TODO.md item 24 ("Lighting: what is
 * left"); the caller's own half is drafted beside the caller's headers.
 *
 * DRAFT, 2026-11-19, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built, not in any target, not compilable. Written on
 * the MacBook (M5 Pro) against the working tree of 2026-11-19 (0.3; read
 * only, another session building in it the same day): render/rbtrendlit.h
 * (the file comment's "Colors and light", lines 84 to 98;
 * RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT, lines 185 to 192;
 * RBTREND_LIT_MATERIAL_VALUES, lines 393 to 460, alpha_cutoff at 410 to
 * 419; rbtrend_lit_material_create and _set_values, lines 511 and 531;
 * RBTREND_LIT_SHADOW_KIND_MASK, line 1002), render/rbtrendcommands.h
 * (LIT_CULL_LIGHTS, lines 824 to 849; COPY_SCENE_COLOR, 850 to 868; the
 * frame's order on LIT_ATMOSPHERE_SHELLS, LIT_FOG and LIT_CLOUDS, 51 to
 * 53), render/rbtrendpipeline.h (the blend presets, lines 630 to 658),
 * render/rbtrenddrawlist.h (RBTREND_DRAW_LIST_ORDER_SUBMITTED, lines 91 to
 * 97), shaders/rbtrend_lit_surface.slang (the opacity and cut-out, lines
 * 319 to 325; the view's occlusion and bent normals, 333 to 337; a light's
 * traced mask, 194 to 200; the premultiplied output, 455),
 * shaders/rbtrend_lit_depth_cutout_frag.slang (lines 25 to 30),
 * src/render/rbtrendlit.cpp (the start values, alpha_cutoff 0, line 1532;
 * set_values' checks, finite alone, line 1567), rbtcore.h
 * (RBT_STRUCT_TYPE_REND_LIT_MATERIAL_VALUES 5018); and
 * ../rabbittustests/tests/rbtrend_gpu_tests.c (grey_values, lines 7086 to
 * 7100, alpha_cutoff 0; the cut-out tests at 7425, 9051 and 9637; the
 * blend presets' test, 12411).
 *
 * The decided line: card L7 (DECISIONS.md, "Decided", 2026-10-28): the
 * caller renders through rbtrendlit, "then textures as engine handles,
 * transparency, tone mapping"; each of its stages a draft for approval.
 * This is the Rend part of the transparency stage. Card A7 (2026-10-28)
 * already gave transmission its scene colour copy (COPY_SCENE_COLOR).
 *
 * ===========================================================================
 * Revision notes (seven passes, 2026-11-19, each fixed before the next)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Version 0 said Rend needed nothing for transparency: blending is the
 *    pipeline's (the presets), the order the caller's (ORDER_SUBMITTED),
 *    the fog and aerial volume premultiplied already (test 4 of the fog,
 *    "a half-transparent face 27 against 27.3"). Wrong twice, found in the
 *    shader: (a) a surface is always written premultiplied by its opacity
 *    (rbtrend_lit_surface.slang 320 to 325, 455), so an opaque surface
 *    drawn by an unblended pipeline whose base colour, map or vertex
 *    colour alpha is below 1 is written darker, and nothing says "the
 *    opacity is ignored" (glTF's OPAQUE); a caller can force the factor
 *    and the vertex colour to 1, never a map's alpha. (b) A blended surface
 *    reads the view's ambient occlusion and bent normals at its texel
 *    (333 to 337) and a light's traced mask (194 to 200): all three were
 *    made from the opaque depth, so a pane of glass darkens with the
 *    crease behind it and takes the floor's shadow. Both need the
 *    surface's mode in the shader. Added: the alpha mode.
 * 2. Missing: the depth-bounded cull (LIT_CULL_LIGHTS) bounds each tile
 *    by the opaque depth, so a blended surface in front of a tile's
 *    nearest depth misses a light whose range lies wholly in front of it.
 *    Said on the command ([CHANGED], a comment); a second list or a far
 *    bound alone is a later item when a caller with both asks (none
 *    records LIT_CULL_LIGHTS and blends today).
 * 3. More than the need: version 0 had ADDITIVE and MODULATE modes
 *    (Unreal's). Additive is the additive preset over BLEND's
 *    premultiplied output (light times opacity added), so the shader has
 *    nothing to do for it: struck, said on BLEND. Modulate has no user:
 *    struck.
 * 4. More than the need: a sort helper (a key per draw). The order is the
 *    caller's, ORDER_SUBMITTED already takes it: struck.
 *
 * Pass 2 (industry standard; section 3). glTF 2.0's alphaMode is the
 * three modes taken (OPAQUE, MASK, BLEND; alphaCutoff read for MASK
 * alone, default 0.5). Filament's material `blending` and `maskThreshold`
 * (opaque the default), Unreal's Blend Mode with its Opacity Mask Clip
 * Value, Unity's Surface Type with Alpha Clipping, Godot's transparency
 * mode: every one a mode on the material, opaque by default, the
 * cut-out's threshold read in the masked mode alone. Screen-space
 * occlusion applied to opaque surfaces only: Unreal's and HDRP's, as
 * remembered (not checked here). Changed from version 0's flag to the
 * enum (Q1).
 *
 * Pass 3 (the C API to standard). The mode a field of the based values
 * struct (set and read with the rest by set_values and get_values, as
 * every lit value but the per-map transforms); its enum RBT_ENUM_BASE,
 * as RBTREND_LIT_SHADOW_KIND. Appended after attenuation_distance, so no
 * field before it moves. alpha_cutoff's meaning narrowed to MASK and its
 * range checked there (today any finite value). A zeroed field is OPAQUE
 * (Q3).
 *
 * Pass 4 (data-oriented design and allocation). One 4-byte field in the
 * values; one word of the material's block (internal, 256 bytes: the
 * block's last word is free, so it doesn't grow). No allocation anywhere;
 * one uniform branch a fragment; BLEND reads three textures fewer.
 *
 * Pass 5 (threads, determinism, errors). As set_values (the material, one
 * caller at a time). The same pixels for every surface of opacity 1 as
 * before (test 5). Refusals each their own check and message, in member
 * order, nothing changed.
 *
 * Pass 6 (the code as built, re-read 2026-11-19). 1. The path trace and
 *    traced reflections read the lit-material table (rbtrendlit.h, line
 *    517: "the values reach a path trace through the material's entry"):
 *    their hits stay opaque, as built (item 24: "cut-outs opaque to rays
 *    and hits reading no maps"); the mode is carried in the table's entry
 *    and read by no ray yet, said. 2. RBTREND_LIT_SHADER_DEPTH_CUTOUT_
 *    FRAGMENT returns at alpha_cutoff <= 0 (depth_cutout_frag 25): it
 *    tests the mode instead. 3. rabbittustests' grey_values memsets the
 *    struct: its surfaces become OPAQUE, the same pixels at opacity 1; the
 *    tests drawing a blended lit face set BLEND (listed in section 5).
 *    4. The particles' fragment shaders (item 57, approved, not built)
 *    read a lit material at set 1: they read it as BLEND whatever its
 *    mode (a particle is always blended): a build note for item 57, not a
 *    change to that draft.
 *
 * Pass 7 (tests, formats, records, the build). Tests named (section 5),
 * seen failing first on today's shader (test 1: darker; test 3: occluded
 * and shadowed). No stored format (Rend stores no material). A listed ABI
 * break (section 6). No version raise while 0.3 is untagged (card X2).
 *
 * ===========================================================================
 * 1. Why
 * ===========================================================================
 *
 * A caller drawing see-through lit surfaces (glass, water, bubbles, a
 * ghost) and opaque ones needs three things of Rend, and has two:
 * - blending: the pipeline's (rbtrend_get_blend_premultiplied(),
 *   _additive()), over the surface's premultiplied output: has it;
 * - an order: the caller's (RBTREND_DRAW_LIST_ORDER_SUBMITTED, or its own
 *   commands): has it;
 * - a surface that knows what it is: an opaque one ignoring its opacity, a
 *   blended one reading nothing the opaque depth made. Missing (pass 1.1).
 * The fog (stage 1) and the aerial volume already reach a blended surface
 * premultiplied; the fog grid's stage 3 reads the grid at the fragment's
 * own place, not the screen's, so a BLEND surface reads it (said for
 * item 56's stage 3).
 *
 * ===========================================================================
 * 2. What changes
 * ===========================================================================
 *
 * [ADDED]   RBTREND_LIT_ALPHA_MODE (OPAQUE, MASK, BLEND).
 * [ADDED]   RBTREND_LIT_MATERIAL_VALUES::alpha_mode, appended.
 * [CHANGED] RBTREND_LIT_MATERIAL_VALUES::alpha_cutoff: read for MASK alone,
 *           there above 0 and at most 1.
 * [CHANGED] rbtrend_lit_material_create(): its start values OPAQUE
 *           (alpha_cutoff 0 meant blended before).
 * [CHANGED] rbtrend_lit_material_set_values(): two refusals more.
 * [CHANGED] RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT: discards for MASK
 *           alone.
 * [CHANGED] the file comment's "Colors and light": the output by mode.
 * [CHANGED] RBTREND_COMMAND_LIT_CULL_LIGHTS: a comment (pass 1.2).
 * The shaders: rbtrend_lit_surface.slang (and its reflect variant) by
 * mode; rbtrend_lit_depth_cutout_frag.slang; the material block's word.
 *
 * ===========================================================================
 * 3. What was compared (pass 2)
 * ===========================================================================
 *
 * - glTF 2.0: `alphaMode` OPAQUE ("the alpha value is ignored and the
 *   rendered output is fully opaque"), MASK (`alphaCutoff`, default 0.5),
 *   BLEND. Taken whole.
 * - Filament: `blending` (opaque, transparent, add, masked, fade,
 *   multiply, screen) and `maskThreshold`; its "transparent" premultiplied
 *   and "fade" straight. Here "add" is the pipeline's preset over BLEND,
 *   "fade" the caller's to premultiply (Rend's output always is).
 * - Unreal: Blend Mode (Opaque, Masked, Translucent, Additive, Modulate,
 *   AlphaComposite), Opacity Mask Clip Value; translucency drawn after the
 *   opaque passes, sorted per primitive by distance; screen-space AO not on
 *   translucency (as remembered).
 * - Unity URP and HDRP: Surface Type Opaque or Transparent, Blending Mode
 *   (Alpha, Premultiply, Additive, Multiply), Alpha Clipping a separate
 *   toggle; transparents sorted back to front by their bounds' centre.
 * - Godot 4: transparency Disabled, Alpha, Alpha Scissor, Alpha Hash,
 *   Depth Pre-Pass; blend mode Mix, Add, Sub, Mul.
 * - Frostbite: sorted alpha blending of surfaces and particles after the
 *   opaque and volumetric passes (as remembered).
 * - Hazel (present, ../.refs/HazelRenderer2025/Hazel/Hazel/src/Hazel/
 *   Renderer): a material asset's `m_Transparent` chooses another shader,
 *   "HazelPBR_Transparent", with a `Transparency` uniform
 *   (MaterialAsset.cpp lines 22 to 28, 245 to 252); its pipeline blends
 *   SrcAlpha, OneMinusSrcAlpha (Pipeline.cpp 203 to 211), reversed depth
 *   GreaterOrEqual (SceneRenderer.cpp 807 to 812); transparent meshes go to
 *   their own draw lists keyed by mesh, not sorted by distance (2305,
 *   2403, 3014 to 3039); a transparent pre-depth pass feeds its composite
 *   (721 to 757, 1234); no animated transparent pipeline (its own TODO,
 *   823). Taken: a mode on the material, a pass after the opaque.
 *   Not taken: a shader of its own (one shader, the mode a uniform branch),
 *   straight alpha (Rend premultiplies), the unsorted list (the caller
 *   sorts).
 * Order-independent transparency (weighted blended, per-pixel lists) is the
 * caller's choice of pipeline and targets, not a Rend mode: the caller's
 * draft weighs it.
 *
 * ===========================================================================
 * 4. The modes in the shader
 * ===========================================================================
 *
 * opacity = base colour alpha x the base colour map's alpha x the colour's
 * alpha at location 4, clamped 0 to 1, as today. Then:
 * - OPAQUE: opacity taken as 1; nothing discarded; written (colour, 1).
 *   The view's occlusion, bent normals and a light's traced mask read as
 *   today.
 * - MASK: a fragment of opacity below alpha_cutoff discarded, the rest as
 *   OPAQUE (today's alpha_cutoff above 0, unchanged).
 * - BLEND: written (colour x opacity, opacity), today's alpha_cutoff 0
 *   path, for a pipeline blending with rbtrend_get_blend_premultiplied()
 *   (over) or rbtrend_get_blend_additive() (its light times its opacity
 *   added). It reads none of the view's screen-space inputs: no view
 *   occlusion (its ORM map's occlusion still applies), no bent normals
 *   (the shading normal), and a directional light whose shadow is a traced
 *   mask lights it unshadowed; cascades and map shadows, the tile's
 *   lights, the environment, the aerial volume and the fog as any surface.
 *   Transmission (KHR_materials_transmission) is read in every mode: a
 *   transmitting surface is usually OPAQUE (glTF's), drawn after
 *   COPY_SCENE_COLOR.
 * The reflect variant (RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT) the
 * same, its specular weight premultiplied for BLEND as today.
 *
 * Cost: one branch on a uniform a fragment, the same for every fragment of
 * a draw; BLEND skips three texture reads. Estimated under 1% of the lit
 * fragment (method: operations counted against the fragment's several
 * hundred; not measured, test_lit_measure's existing scenes rerun).
 *
 * ===========================================================================
 * 5. Tests (rabbittustests, Metal under API and shader validation, warnings
 *    fatal; Vulkan type-checked; D3D12 written, card X25)
 * ===========================================================================
 *
 * 1. test_lit_alpha_opaque (seen failing first: darker): a face of base
 *    alpha 0.25, a base colour map of alpha 0 and a vertex colour of alpha
 *    0.5, OPAQUE, drawn by an unblended pipeline: the same pixels as at
 *    alpha 1, alpha written 1.
 * 2. test_lit_alpha_mask: the cut-out tests (7425, 9051, 9637) restated
 *    with MASK, the same pixels; an OPAQUE material with alpha_cutoff 0.5
 *    and opacity 0.25 not cut, in the surface and through
 *    DEPTH_CUTOUT_FRAGMENT (a shadow whole).
 * 3. test_lit_alpha_blend_screen_space (seen failing first: occluded and
 *    shadowed): a BLEND face of opacity 1 under the premultiplied preset,
 *    over a crease whose view occlusion is 0.2 at its texels: its colour as
 *    with no view occlusion, its ORM occlusion still applied; bent normals
 *    on: not read; a light with a traced mask shadowing the floor behind
 *    it: the face lit, an OPAQUE face at the same place shadowed; a
 *    cascaded shadow over it: shadowed in both modes.
 * 4. test_lit_alpha_refusals: an alpha_mode past BLEND; MASK with an
 *    alpha_cutoff of 0, of 1.5, of NaN; each its message, nothing changed;
 *    a cutoff of 7 under OPAQUE or BLEND accepted and not read.
 * 5. Every existing lit test the same pixels: grey_values' surfaces OPAQUE
 *    at opacity 1; test_fog_sky_and_transparent's half-transparent face and
 *    the transmission tests set BLEND where they blend (27 against 27.3
 *    unchanged).
 * 6. Headless: set_values and get_values round-trip the mode;
 *    rbtrend_material_copy carries it; test_frame_allocations unchanged
 *    (set_values allocates nothing); the misuse harness regenerated.
 *
 * ===========================================================================
 * 6. ABI, versions, threads, records when built
 * ===========================================================================
 *
 * ABI: RBTREND_LIT_MATERIAL_VALUES grows by 4 bytes at its end (a based
 * struct grown by a field before 1.0: listed in the commit from
 * `./check.sh --update-abi`); its start values change (OPAQUE where a
 * zero cutoff meant blended): a caller who blended a lit surface by
 * default sets BLEND. alpha_cutoff's refusals under MASK are new. No
 * enum value moves; no struct type added. No stored format. Versions: no
 * raise while 0.3 is untagged (card X2).
 *
 * Threads: as rbtrend_lit_material_set_values (rbtrendcontext.h,
 * "Threads"): the material, one caller at a time. Allocation: none.
 * Determinism: a shader branch on a value; no new source of difference.
 *
 * Records when built: RBTREND_TODO.md item 24 (this entry, checked, with
 * what ran); item 57's build note (pass 6.4); item 56's stage 3 note (the
 * grid read by BLEND surfaces: section 1); claude_made_decisions.md for
 * any shape chosen while building; abi.txt.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. How a surface says what it is: a, an enum field, OPAQUE, MASK,
 *     BLEND, alpha_cutoff read for MASK (recommended: glTF's and every
 *     engine's shape; one meaning a value); b, a bool `opaque`, alpha_cutoff
 *     keeping its two meanings (smaller, the mode spread over two fields);
 *     c, a negative alpha_cutoff meaning opaque (no layout change; a
 *     sentinel no reader would guess).
 * Q2. What BLEND leaves out: a, every screen-space input the opaque depth
 *     made: the view's occlusion, bent normals, a light's traced mask
 *     (recommended: each holds the surface behind); b, the occlusion and
 *     bent normals only, a traced mask still read (a shadow at the wrong
 *     depth); c, nothing, a caller's problem (no caller can fix it: the
 *     scene's set is one a frame).
 * Q3. A zeroed alpha_mode: a, OPAQUE (recommended: glTF's and Filament's
 *     default; a zeroed struct writes what an unblended pipeline expects);
 *     b, BLEND (today's zero-cutoff meaning, no test changes; an opaque
 *     surface of alpha below 1 still written darker unless set).
 */

/* ========================================================================= */
/* render/rbtrendlit.h                                                       */
/* ========================================================================= */

/*
 * The file comment's "Colors and light", its last sentences [CHANGED]:
 *
 *   ... (RBTREND_LIT_VIEW::output_encoding). What alpha it writes is the
 *   material's alpha mode's (RBTREND_LIT_ALPHA_MODE): 1 for an opaque or
 *   cut-out surface, its opacity for a blended one, whose colour is then
 *   premultiplied by it; blending is the pipeline's.
 *
 * Was: "It is premultiplied by the opacity: blending is the pipeline's, and
 * an opaque surface writes alpha 1." (lines 97 and 98), which no value let
 * a surface be.
 */

/**
 * @brief What a lit surface's opacity does (glTF 2.0's alphaMode).
 *
 * Its opacity is the base color's alpha times the base color map's times
 * the color's alpha at location 4, 0 to 1.
 */
typedef enum RBTREND_LIT_ALPHA_MODE RBT_ENUM_BASE {
	/** Opaque: the opacity is ignored; the surface is written whole, alpha
	 * 1, for a pipeline that doesn't blend. */
	RBTREND_LIT_ALPHA_MODE_OPAQUE = 0,
	/** A cut-out: a fragment whose opacity is below
	 * RBTREND_LIT_MATERIAL_VALUES::alpha_cutoff is discarded, in its shadows
	 * too where they are rendered with RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT;
	 * the rest is opaque. */
	RBTREND_LIT_ALPHA_MODE_MASK   = 1,
	/**
	 * Blended: written premultiplied by its opacity, for a pipeline that
	 * blends it over what is drawn (rbtrend_get_blend_premultiplied()) or
	 * adds its light times its opacity (rbtrend_get_blend_additive()),
	 * drawn after the opaque surfaces in the caller's order, back to front
	 * for the first.
	 *
	 * It reads nothing of the view's that the opaque depth made, which
	 * holds the surface behind it: not the view's ambient occlusion
	 * (rbtrend_lit_scene_set_ambient_occlusion(); its ORM map's still
	 * applies) nor its bent normals, and a directional light whose shadow
	 * is a traced mask (RBTREND_LIT_SHADOW_KIND_MASK) lights it unshadowed.
	 * Cascades and map shadows, the lights, the environment, the scene's
	 * atmosphere and fog reach it as any surface.
	 */
	RBTREND_LIT_ALPHA_MODE_BLEND  = 2,
} RBTREND_LIT_ALPHA_MODE;
/* [ADDED] Why: an opaque surface's opacity can't be ignored today (pass
 * 1.1a), and a blended one reads the opaque's screen-space inputs (1.1b).
 * glTF's three modes (pass 2). */

/*
 * RBTREND_LIT_MATERIAL_VALUES, two members [CHANGED] and one [ADDED]:
 *
 *	/ **
 *	 * Read when #alpha_mode is RBTREND_LIT_ALPHA_MODE_MASK alone: a
 *	 * fragment whose opacity is below this is discarded and the rest are
 *	 * opaque (glTF's alphaCutoff), above 0 and at most 1. Any finite value
 *	 * otherwise, not read.
 *	 * /
 *	float alpha_cutoff;
 *	(was: "0 for a surface blended by its opacity. Above 0, a cut-out ...")
 *
 *	... (every member as it is, to attenuation_distance) ...
 *
 *	/ ** What the surface's opacity does: RBTREND_LIT_ALPHA_MODE. A
 *	 * material starts OPAQUE (rbtrend_lit_material_create()). * /
 *	RBTREND_LIT_ALPHA_MODE alpha_mode;
 *	[ADDED, appended after attenuation_distance] Why: section 2; appended
 *	so no member before it moves.
 */

/*
 * rbtrend_lit_material_create() [CHANGED], its comment's start values: "...
 * alpha mode OPAQUE (alpha_cutoff 0, not read) ..." (was alpha_cutoff 0,
 * which meant blended).
 *
 * rbtrend_lit_material_set_values() [CHANGED], its @return: "...
 * RBT_RESULT_INVALID_ARGUMENT ... a value is not finite, alpha_mode isn't
 * one RBTREND_LIT_ALPHA_MODE names, or alpha_mode is MASK and alpha_cutoff
 * isn't above 0 and at most 1 (each its own check and message, in member
 * order)."
 *
 * RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT [CHANGED]: "... for cut-out
 * materials: a material of RBTREND_LIT_ALPHA_MODE_MASK discards a fragment
 * whose opacity ... is below its alpha_cutoff; any other is written whole
 * (an opaque or a blended material's shadow is its whole shape)."
 */

/* ========================================================================= */
/* render/rbtrendcommands.h                                                  */
/* ========================================================================= */

/*
 * RBTREND_COMMAND_LIT_CULL_LIGHTS [CHANGED], a paragraph added:
 *
 *   The bounds are the opaque depth's: a blended surface
 *   (RBTREND_LIT_ALPHA_MODE_BLEND) in front of a tile's nearest depth
 *   reads that tile's list, so a light whose range lies wholly in front of
 *   it is missing there. A caller drawing blended surfaces in such lights
 *   keeps the compile's cull (without depth bounds) for that scene.
 *
 * Why: pass 1.2; a comment only, no caller affected today.
 */
