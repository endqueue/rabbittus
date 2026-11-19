/*
 * rbtrend_weather_draft.h - the one thing weather needs of Rend that Rend
 * hasn't got or decided: a cover over its lit surfaces, snow lying on what
 * faces up and wetness darkening and smoothing what is porous, set for a
 * scene from values its caller works out each frame, answered by each
 * material (card L74's cover stage, which card L68's second round carded
 * as "the layered surface's later stage": rbtrend_lit_layers_draft.h,
 * section 4, "a cover input"; the session that asked is named on the
 * cards). Its caller's half, drafted beside the caller's headers, is the
 * weather itself.
 *
 * Everything else weather draws is drafted elsewhere and only named here:
 * precipitation is particles (rbtrend_particles_draft.h, card L73: a
 * box wrapped round the camera, drag towards the step's wind); clouds take
 * a coverage map from their caller (card A57, drafted beside this by
 * another session); haze raises an atmosphere's Mie density through
 * rbtrend_atmosphere_set_medium() as built, and where there is no
 * atmosphere it is card A59's height fog (drafted beside this by another
 * session). None of them is redesigned here.
 *
 * APPROVED 2026-11-18 with the user, every question as recommended.
 * DRAFT, 2026-11-18, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built yet. Not in any target, not
 * compilable. Written on the MacBook (M5 Pro) against Rabbittus c79f7d6
 * (0.3), read 2026-11-18: include/render/rbtrendlit.h
 * (RBTREND_LIT_MATERIAL_VALUES, based, 392-459; rbtrend_lit_material_set_
 * values, 530; RBTREND_LIT_ATMOSPHERE, 1399-1446: a scene's setting in a
 * based struct, set each frame, allocating nothing at the same sizes),
 * rbtcore.h (struct types to 5040); shaders/rbtrend_lit_scene.slang (the
 * scene's block, flags to 2048), rbtrend_lit_bindings.slang (set 0's
 * sixteen bindings: full), rbtrend_lit_surface.slang (lit_shade: the
 * material's reads, then the lighting); and the approved
 * rbtrend_lit_layers_draft.h (version 4: RBTREND_LIT_LAYER_VALUES, based,
 * its section 4 "What grows later": "a cover input: a weight added per
 * pixel from a value the caller sets per draw and the normal's slope").
 * Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel/src/Hazel,
 * present; searched for snow, wet, rain, weather): nothing. Its material
 * has no cover input.
 *
 * ===========================================================================
 * Revision notes (seven passes, 2026-11-18, each fixed before the next)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. The layered draft carded the cover as a per-draw value. A per-draw
 *    snow amount steps at every chunk's edge on a terrain and is a value
 *    per draw for every mesh a caller has. Changed: the scene's, one set
 *    per frame, varied per pixel by slope and by height against a snow
 *    line, which is how far mountains are white while the valley is wet.
 * 2. Missing: the plain lit surface. Rain wets roads, roofs and cars, not
 *    only terrain. Changed: both of Rend's lit surfaces (the material's
 *    and the layered one) read the cover, through lit_shade().
 * 3. Missing: what "up" and "height" are on a round world. Changed: the
 *    cover has a frame, round (a centre: up is away from it, height the
 *    distance from it) or flat (an up and an origin).
 * 4. More than needed: a cover map (a texture of cover over the world).
 *    Struck for now: set 0 is full (sixteen bindings), and a map is a
 *    binding; it waits on bindless (card A15) or a set of its own, carded
 *    in Q1's b.
 * 5. More than needed: puddles (a height-blended water layer) and
 *    ripples. Struck: a later chained value of the same cover, when asked.
 *
 * Pass 2 (industry standard).
 * 1. Compared: Lagarde's "Water drop" series (2012-2013) and Frostbite's
 *    and Remember Me's wet surfaces: albedo darkened by porosity, roughness
 *    lowered, the wet BRDF the same model; Far Cry 5's and Unreal's
 *    runtime snow (a material layer by the normal's slope and a global
 *    amount; Unreal's "Material Parameter Collection" is the scene-wide
 *    value); Horizon Zero Dawn's and Red Dead Redemption 2's snow lines
 *    (talks, as remembered); Unity HDRP's weather samples (a global wetness
 *    and snow in shader graphs). Taken: Lagarde's wet model, a slope and a
 *    snow line over a scene-wide amount, a material's response.
 * 2. No engine has the cover in its renderer's core as such; each puts it
 *    in its materials. Rend's materials are Rend's shaders, so it is
 *    Rend's, behind one setting.
 *
 * Pass 3 (the C API).
 * 1. The scene's cover is a based struct set like the atmosphere
 *    (rbtrend_lit_scene_set_cover(), NULL for none); a material's answer
 *    a based struct chained onto RBTREND_LIT_MATERIAL_VALUES and onto
 *    RBTREND_LIT_LAYER_VALUES (the struct bases' growth: their layouts
 *    unchanged). A material without one takes no cover (Q2): no hidden
 *    default.
 * 2. The frame's kind an RBT_ENUM_BASE enum with a refused zero.
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. The scene's block gains the cover (48 bytes); a material's block its
 *    response (8 bytes, padded in its last float4's spare lanes where it
 *    has them: checked in rbtrend_lit_material.slang when built). No
 *    binding, no allocation: set_cover writes the block in place.
 * 2. Per pixel: a few dozen ALU operations where the cover is on, a
 *    branch on the scene's flag where it is off (uniform).
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Threads: the scene's and the material's rules, unchanged.
 * 2. Determinism: a picture's.
 * 3. Errors: set_cover's checks each its own, in the struct's order; the
 *    response's in set_values' and set_layer's order, as a chained
 *    struct's are.
 *
 * Pass 6 (the code as built, re-read 2026-11-18).
 * 1. rbtrend_lit_scene.slang's flags reach 2048 (SKY): COVER takes 4096.
 * 2. lit_shade() reads the material then lights it, one function for the
 *    lit surface and (by the layered draft) the layered one: the cover
 *    goes between, once, for both. rbtrend_lit_surface_reflect_frag and
 *    the depth-cutout shader don't shade: unchanged.
 * 3. The traced paths (LIT_PATH_TRACE, LIT_TRACE_REFLECTIONS) read
 *    materials through their own instance tables (rbtrendraytracing.cpp),
 *    with no scene cover: a reflected snowy roof is bare. Said, and a
 *    later stage (Q3).
 * 4. The height against the snow line is |P - centre| in float with the
 *    eye at the scene's origin (card L66): a world's centre 6.4e6 m off
 *    puts the height's float step at 0.5 m, below any snow line's band
 *    (tens of metres): said in section 3.
 * 5. The head's line ranges re-read: RBTREND_LIT_MATERIAL_VALUES ends at
 *    459 (was written 420), RBTREND_LIT_ATMOSPHERE at 1446 (was 1450).
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Tests below; every existing lit test unchanged with no cover set.
 *    Found writing T5: section 1's wet rule was left "written in full when
 *    built"; a test needs the number. Now Lagarde's factor, stated, and
 *    the order of wet and snow.
 * 2. No format. ABI: additions only; listed by ./check.sh --update-abi.
 *    No version raise while 0.3 is untagged (card X2).
 * 3. The layered draft's chained response is built with whichever of the
 *    two comes second: if the layered surface is built first, this adds
 *    its chain; if this is, the layered build takes it.
 * 4. ./check.sh caller-names, run at the end: the caller's half named by
 *    its path; now "beside the caller's headers", the meaning kept.
 *
 * ===========================================================================
 * 1. What the cover does
 * ===========================================================================
 *
 * At each lit pixel, after its material's reads and before its lighting:
 * - SNOW. s = amount x slope(N) x line(h) x the material's `cover`, where
 *   N is the surface's geometric normal (not its normal map's, so the snow
 *   lies on the shape), slope(N) = smoothstep(slope_begin, slope_full,
 *   dot(N, up)), line(h) = smoothstep(snow_line, snow_line + snow_band, h)
 *   for h the height (below). The base colour, roughness, metalness and
 *   normal blend towards the snow's (its colour, its roughness, metalness
 *   0, the geometric normal) by s; its emission is kept.
 * - WET. With w = wetness (Lagarde's "Water drop 2b", the porous model):
 *   the base colour times lerp(1, 0.2, w x the material's `porosity`)
 *   (water filling a porous surface darkens it; a sealed one keeps its
 *   colour), its roughness lerped towards `wet_roughness` by w (the water
 *   film on top of any surface), metalness kept. A surface under snow is
 *   wet beneath it but shows the snow: the wet rule runs first, the snow's
 *   over it.
 * Up and height: ROUND, up = normalize(P - centre), h = |P - centre|;
 * FLAT, up = the given up, h = dot(P - origin, up).
 *
 * ===========================================================================
 * 2. Who else uses it
 * ===========================================================================
 *
 * Any caller with lit surfaces and a climate: a game's weather (its
 * first), a CAD or architecture caller's site seen in winter or after rain
 * (a presentation view's look), a driving simulator's wet road. Nothing
 * here knows what weather is.
 *
 * ===========================================================================
 * 3. Precision, costs, memory
 * ===========================================================================
 *
 * Precision: pass 6.4. Memory: 48 bytes a scene's block, 8 a material's.
 * Cost, estimated by counting (to be measured when built: Release, the M5
 * Pro, a full-screen lit face at 1440p with the cover off and on, means of
 * 50 after 10): about 40 ALU operations a pixel (two smoothsteps, a length
 * for ROUND, three lerps and a pow for the wet albedo), 150 million at
 * 1440p, under 0.02 ms; off, a uniform branch.
 *
 * ===========================================================================
 * 4. Tests (rabbittustests under Metal's validation with warnings fatal,
 * Debug and Release; rbtrend_contract_tests headless; each seen failing
 * first)
 * ===========================================================================
 *
 * T1. Refusals: each field of RBTREND_LIT_COVER out of range, each its own
 *     message; a response's fields; the frame kind 0 or 3; a flat up of 0.
 * T2. test_lit_cover_off: no cover set, or a cover with amount and wetness
 *     0: every lit test's pixels bit for bit.
 * T3. test_lit_cover_snow_slope: a face turned from up to sideways under a
 *     full snow amount: white (the snow's colour lit) facing up, its own
 *     colour past slope_begin, the blend between; a normal-mapped face
 *     snowed by its geometric normal.
 * T4. test_lit_cover_snow_line: faces at heights across the line: bare
 *     below, snowed above, round (a centre 6.4e6 m off, the eye at the
 *     origin) and flat.
 * T5. test_lit_cover_wet: a porous face at wetness 1: darker by Lagarde's
 *     factor within 1/255, its highlight sharper; porosity 0: colour kept,
 *     roughness lowered.
 * T6. test_lit_cover_response: a material with no response chained: bare
 *     and dry under a full cover; one with `cover` 0.5: half the snow.
 * T7. The layered surface (when built): each layer's own response.
 * T8. test_frame_allocations: a frame with a cover set each frame: 0.
 *
 * Records when built: RBTREND_TODO.md (a new item for the cover, with
 * item 53's line); rbtrendlit.h's "Shading"; ./check.sh --update-abi.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card L74's Rend part; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. Where a cover varies: a, scene-wide values a frame, varied per pixel
 *     by slope and a snow line's height (recommended: no binding, which set
 *     0 has no room for; a frame's view sees one weather nearby, and the
 *     snow line whitens far mountains); b, a cover map over the world (a
 *     cube or 2D texture of snow and wetness, regional), which needs a
 *     binding: carded for when bindless (A15) or a set of its own makes
 *     room; c, per draw, as the layered draft first carded (steps at each
 *     draw's edge).
 * Q2. A material with no response chained: a, takes no cover
 *     (recommended: what a material looked like is kept until its caller
 *     says; no default in Rend); b, takes full snow and a middling
 *     porosity (every material at once, indoors too).
 * Q3. Traced reflections and the path tracer: a, without the cover in this
 *     stage, said in the header (recommended: their materials are their
 *     instance tables'; a stage of its own if a caller asks); b, with it
 *     now.
 */

/* ========================================================================= */
/* rbtrendlit.h                                                              */
/* ========================================================================= */

/** [ADDED] How a cover's up and height are found. Why: round and flat
 * worlds alike (card L74's correction). */
typedef enum RBTREND_LIT_COVER_FRAME RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_LIT_COVER_FRAME_NONE  = 0,
	/** Up is away from a centre; height is the distance from it. */
	RBTREND_LIT_COVER_FRAME_ROUND = 1,
	/** Up is one direction; height is along it from an origin. */
	RBTREND_LIT_COVER_FRAME_FLAT  = 2,
} RBTREND_LIT_COVER_FRAME;

/**
 * @brief Snow and wetness over a scene's lit surfaces (section 1), set
 * each frame as the weather changes. No field has a default.
 *
 * [ADDED] Why: card L74's cover stage; the look of weather on surfaces,
 * Rend's to draw since its materials are Rend's shaders.
 */
typedef struct RBTREND_LIT_COVER {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_COVER, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** Its frame. */
	RBTREND_LIT_COVER_FRAME frame;
	/** ROUND: the centre; FLAT: the origin; the scene's space, finite. */
	float point[3];
	/** FLAT: up, any length but 0; not read for ROUND. Finite. */
	float up[3];
	/** How much snow lies where it can, 0 to 1. */
	float snow_amount;
	/** The snow's colour, linear, each 0 to 1. */
	float snow_color[3];
	/** The snow's roughness, 0 to 1. */
	float snow_roughness;
	/** Snow begins on faces whose up-facing cosine is above this... -1 to 1. */
	float slope_begin;
	/** ...and lies fully above this: slope_begin to 1. */
	float slope_full;
	/** The height below which no snow lies, the scene's unit: finite. */
	float snow_line;
	/** The height over which snow comes in above the line: above 0. */
	float snow_band;
	/** How wet surfaces are, 0 to 1. */
	float wetness;
	/** The roughness a fully wet surface tends to, 0 to 1. */
	float wet_roughness;
} RBTREND_LIT_COVER;

/**
 * @brief Sets a scene's cover, or none (NULL, as a scene starts). Copied;
 * allocates nothing, so it is set every frame. With one, every lit surface
 * of the scene whose material answers the cover (RBTREND_LIT_COVER_
 * RESPONSE) is snowed and wetted by section 1's rule; the traced paths
 * aren't (LIT_PATH_TRACE, LIT_TRACE_REFLECTIONS: their materials bare).
 *
 * @param[in]  scene     The scene.
 * @param[in]  cover     The cover, or NULL. Only read during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL scene or a
 *         field out of its range, each its own check and message, in the
 *         struct's order; the struct codes for `cover->base`. Nothing
 *         changes on failure.
 *
 * [ADDED] Why: the scene-wide values (Q1a).
 */
RBT_RESULT rbtrend_lit_scene_set_cover(rbtrend_lit_scene scene, const RBTREND_LIT_COVER *cover, RBTREND_ERROR *out_error);

/**
 * @brief How a material answers a scene's cover, chained onto its
 * RBTREND_LIT_MATERIAL_VALUES (rbtrend_lit_material_set_values()) or onto
 * a layer's RBTREND_LIT_LAYER_VALUES (the layered surface). A material or
 * layer without one takes no cover (Q2a). Read back by the getters with
 * the rest.
 *
 * [ADDED] Why: a roof takes snow and a lamp's glass doesn't; a brick soaks
 * and a metal doesn't (Lagarde's porosity).
 */
typedef struct RBTREND_LIT_COVER_RESPONSE {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_COVER_RESPONSE, chained (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** How much of the scene's snow lies on it, 0 to 1. */
	float cover;
	/** How porous it is, 0 (wet only on top: darkens nothing) to 1. */
	float porosity;
} RBTREND_LIT_COVER_RESPONSE;

/*
 * [CHANGED comments] rbtrend_lit_material_set_values() and _get_values():
 * "An RBTREND_LIT_COVER_RESPONSE may be chained on" (its checks after the
 * values', in the struct's order); the layered draft's _set_layer and
 * _get_layer the same. [ADDED] rbtcore.h: RBT_STRUCT_TYPE_REND_LIT_COVER
 * and _COVER_RESPONSE, the next free when built. Why: the struct bases'
 * growth, the layouts unchanged.
 */
