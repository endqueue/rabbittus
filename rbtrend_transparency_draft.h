/*
 * rbtrend_transparency_draft.h - what a caller's transparency stage needs of
 * Rend's lit surfaces: a lit material's alpha mode. OPAQUE (its opacity
 * ignored, written whole), MASK (a cut-out, as alpha_cutoff above 0 is
 * today) and BLEND (written premultiplied by its opacity for a blended
 * pipeline, reading none of the view's screen-space inputs, which hold the
 * opaque surface behind it); and Rend's lit particles shaded as BLEND
 * whatever their material's mode. RBTREND_TODO.md item 24 ("Lighting: what
 * is left"), with item 57's "Left" line; the caller's own half is drafted
 * beside the caller's headers.
 *
 * DRAFT, version 2: version 1 (2026-11-19, written, then seven critique
 * passes, 1 to 7), then a second round of seven passes over it (passes 8
 * to 14, 2026-11-20, each fixed before the next, chiefly against the code
 * built since version 1 was written: the lighting split from the surface,
 * the layered surface, the lit cover, the particles, card A75): the
 * revision notes below. APPROVED by the user 2026-11-20 after a seven-pass
 * critique, every question as recommended (Q1a, Q2a, Q3a, Q4a). Nothing
 * built, not in any target, not compilable. No version raise while
 * Rabbittus' 0.3 is untagged (card X2). Once approved it is frozen; a
 * shape chosen while building goes to claude_made_decisions.md.
 *
 * Written on the MacBook (M5 Pro) against the working tree of 2026-11-20
 * (0.3; read only, other sessions building in it), cited by name, not by
 * line (version 1's line numbers drifted when the lighting was split out):
 * render/rbtrendlit.h (the file comment's "Colors and light";
 * RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT, _SURFACE_REFLECT_FRAGMENT and
 * _LAYERED_SURFACE_FRAGMENT; RBTREND_LIT_MATERIAL_VALUES, 144 bytes on a
 * 64-bit target; rbtrend_lit_material_create(), _set_values() and
 * _get_values(); RBTREND_LIT_COVER_RESPONSE, chained on the values;
 * RBTREND_LIT_SHADOW_KIND_MASK), render/rbtrendcommands.h
 * (RBTREND_COMMAND_LIT_CULL_LIGHTS, COPY_SCENE_COLOR, LIT_FOG, LIT_CLOUDS,
 * DRAW_PARTICLES and their "frame's order" paragraphs),
 * render/rbtrendparticles.h ("Drawing", "Lit", "Out of scope"),
 * render/rbtrendpipeline.h (RBTREND_GRAPHICS_PIPELINE_DESC::_blends and
 * the blend presets), rbtcore.h (RBT_STRUCT_TYPE_REND_LIT_MATERIAL_VALUES
 * 5018); shaders/rbtrend_lit_material.slang (RbtrendLitMaterial, 288
 * bytes: the cover's three words after `maps`, `emission.w` and
 * `attenuation.w` unused), rbtrend_lit_surface.slang (lit_shade() and
 * lit_shade_with_base_map(): the opacity and the cut-out's discard, then
 * every map's read), rbtrend_lit_lighting.slang (LitSurface; lit_light():
 * the view's occlusion and bent normals; directional_visibility(): a
 * light's traced mask; the output premultiplied at its end; it imports no
 * material), rbtrend_lit_layered.slang (its LitSurface, opacity the
 * colour's alpha), rbtrend_particles_lit_frag.slang (lit_shade_with_base_map()
 * of the particle's material), rbtrend_lit_depth_cutout_frag.slang (returns
 * at alpha_cutoff <= 0); src/render/rbtrendlit.cpp and
 * rbtrendlit_internal.hpp (rbtrend_lit_material_block, static_asserted 288;
 * the block's reflection check, rbtrend_lit_check_block(); set_values: one
 * joined finite check, then the response's, then the block copied whole to
 * the ray table, rbtrend_rt_lit_stage()); RBTREND_TODO.md items 24, 53, 56,
 * 57 and 64; DECISIONS.md card A75 (set 0's block at 16,384 of 16,384
 * bytes, its sixteen bindings taken; decided 2026-11-20 as its option b:
 * the per-light and per-cascade arrays to a storage buffer of their own,
 * a binding freed or merged first); ../rabbittustests/tests/
 * rbtrend_gpu_tests.c (grey_values(), alpha_cutoff 0; the cut-out tests in
 * test_lit_spot_and_cutout_shadows() and beside it;
 * test_fog_sky_and_transparent(); test_frame_allocations).
 *
 * The decided line: card L7 (DECISIONS.md, "Decided", 2026-10-28): the
 * caller renders through rbtrendlit, "then textures as engine handles,
 * transparency, tone mapping"; each of its stages a draft for approval.
 * This is the Rend part of the transparency stage. Card A7 (2026-10-28)
 * already gave transmission its scene colour copy (COPY_SCENE_COLOR).
 *
 * ===========================================================================
 * Revision notes, second round (passes 8 to 14, 2026-11-20, over version 1)
 * ===========================================================================
 *
 * Pass 8 (correctness against the built code and shaders).
 * 1. Version 1 put the mode's test in rbtrend_lit_surface.slang beside the
 *    screen-space reads. They moved: lit_light() and
 *    directional_visibility() in rbtrend_lit_lighting.slang read the view's
 *    occlusion, its bent normals and a light's traced mask, and that module
 *    imports no material (the layered surface and the particles call it
 *    too). Changed: LitSurface gains `screen_space` (1: read them; 0:
 *    don't), which each surface's reads set: the lit surface from its
 *    material's mode, the layered surface 1, the lit particle 0. One
 *    lighting still; no material reaches it.
 * 2. Version 1 put the mode in "the block's last word, free, 256 bytes".
 *    The block is 288 bytes since the lit cover (item 64: cover_amount,
 *    cover_porosity and cover_responds after `maps`), and its free words
 *    are `emission.w` and `attenuation.w`. Changed: `attenuation` becomes a
 *    float3 and `alpha_mode` a uint after it, at byte 284; the block stays
 *    288 bytes (the layered block's float3-then-scalar rows are the same
 *    packing in std140, std430, HLSL's and Metal's through Slang, built and
 *    run). The reflection check names the new member; the ray table's
 *    entry copies the block whole (rbtrend_rt_lit_stage()), so it carries
 *    the mode with nothing more.
 * 3. Version 1 left the particles a build note (pass 6.4). They are built
 *    (item 57, 2026-11-19): RBTREND_PARTICLES_SHADER_LIT_FRAGMENT shades
 *    through lit_shade_with_base_map(), so today a lit particle reads the
 *    view's occlusion, bent normals and a traced mask at its texel (a puff
 *    of smoke darkened by the crease behind it), and a material with an
 *    alpha_cutoff discards. Changed here, [CHANGED] below: it shades its
 *    material as BLEND whatever the mode (no cut-out, screen_space 0), as
 *    item 57's "Left" line already says, and rbtrendparticles.h's "Out of
 *    scope" line on alpha modes goes. Listed as a behaviour change.
 * 4. The layered surface (item 53) has no mode and is opaque; it reads the
 *    screen-space inputs as before. Its output is still premultiplied by
 *    its opacity, the colour's alpha at location 4, as its enumerator
 *    documents: unchanged and said, not a mode of its own.
 * 5. Card A75: the scene block is full and set 0's sixteen bindings are
 *    taken. This draft takes nothing there: the mode is set 1's (the
 *    material block's spare word), and nothing new is bound. A75's option
 *    b doesn't touch the material block; the fog grid's stage 3 (item 56)
 *    binds the grid in set 0 and so waits on A75's freed binding, not on
 *    this.
 * 6. Version 1 said the values struct grows by 4 bytes: with its base's
 *    pointer it is 8-aligned, so on a 64-bit target it grows from 144 to
 *    152 bytes (the mode and 4 of tail padding). Section 6 corrected.
 * 7. set_values' finite check is one joined check today ("a value is not a
 *    finite number"); the two new refusals are their own lines after it
 *    and before the response's, each with its message.
 *
 * Pass 9 (the C API's conventions). 1. Appended field or chained struct:
 *    the cover's response (item 64) grew the material by a chain, and
 *    Rabbittus' rule from 1.0 is a chain. A chain is for what some
 *    materials have (no response: no cover); a mode is every material's,
 *    read back by every get_values, and before 1.0 a field appended to a
 *    based struct is a listed break, not a silent one. Kept the field;
 *    asked (Q4). 2. No hidden policy: a zeroed mode is a stated value
 *    (OPAQUE, said on the member and the enum), not a default chosen for
 *    the caller; MASK's cut-off has no default (above 0 and at most 1, or
 *    refused). 3. Errors per call: each refusal's message written (section
 *    2). 4. The enum RBT_ENUM_BASE with its values written, only
 *    appended. 5. A member written by name (rbtrend_material_set_uniform(),
 *    `alpha_mode`) bypasses the check: the shader reads any value but MASK
 *    and BLEND as OPAQUE, said on the member. 6. Doxygen as rbtrendimages.h:
 *    @brief on the enum and each enumerator, the member's /** */.
 *
 * Pass 10 (industry practice; section 3). 1. Hazel's transparent shader
 *    read (HazelPBR_Transparent.glsl, Hazelnut/Resources/Shaders): its
 *    cascades and point lights, three outputs (colour, view normals and
 *    luminance, metalness and roughness), and no screen-space occlusion:
 *    the shape Q2 a takes, now checked rather than remembered. 2.
 *    Order-independent transparency weighed for what Rend would need: a
 *    weighted blended pass (McGuire and Bavoil, JCGT 2013) or a moment-based
 *    one (Munstermann, Krumpen, Klein and Peters, HPG 2018) writes a
 *    weighted pair to two targets from a weight made in the fragment shader
 *    from its depth and opacity: a caller's pipeline over BLEND's single
 *    premultiplied output can't apply it. Changed: section 3 says OIT over
 *    the lit surface would be a fragment variant of Rend's (its own draft
 *    when a caller asks), not a pipeline choice; none asks (the caller's
 *    half sorts). 3. Unreal's translucency lighting modes (per-pixel
 *    forward shading or a lighting volume) and Unity URP's SSAO applied to
 *    opaque surfaces alone, as remembered: BLEND is the forward shading
 *    mode.
 *
 * Pass 11 (performance and data-oriented design). The mode is a uint in
 *    the 16-byte row the shader already loads for the attenuation: no
 *    extra fetch, no growth of the block, one branch on a value uniform
 *    over a draw (no divergence). BLEND skips the view occlusion's sample
 *    and a traced mask's fetch a fragment; the lit particles, overdraw by
 *    nature, skip them too. set_values does one more comparison and two
 *    for MASK; nothing allocates; a settled frame allocates nothing of
 *    Rend's (test_frame_allocations unchanged). No bandwidth in Rend: the
 *    blend is the caller's pipeline's.
 *
 * Pass 12 (every backend). 1. The presets BLEND is drawn with (ONE,
 *    ONE_MINUS_SRC_ALPHA; ONE, ONE) are core blending on Metal, Vulkan and
 *    D3D12 for the float and UNORM targets the lit surface writes; no
 *    dual-source blending (the shells' stays theirs), no feature gate. The
 *    reflect variant's targets 1 and 2 under BLEND are blended with the
 *    same preset or not at all: different blends a target need
 *    CRGPU_FEATURE_INDEPENDENT_BLEND, as RBTREND_GRAPHICS_PIPELINE_DESC
 *    says (said on the variant). 2. Found: today's cut-out discards before
 *    the ORM, emission and normal maps are read with implicit derivatives.
 *    Where a discard ends the invocation (SPIR-V's OpKill before 1.6) its
 *    quad's later derivatives are undefined; a demote (D3D's discard,
 *    SPIR-V's OpDemoteToHelperInvocation) keeps them. Rather than depend
 *    on what Slang emits for each target, MASK's discard moves after every
 *    map's read in lit_shade_with_base_map(): defined on all three; a
 *    discarded fragment pays three reads it didn't before (MASK only).
 *    DEPTH_CUTOUT_FRAGMENT reads the base map alone, before its discard:
 *    unchanged. 3. The block's float3-then-uint row: packed alike by
 *    Slang for SPIR-V's std140, HLSL's cbuffer and Metal, as the layered
 *    block's rows are (built on Metal; on D3D12 and Vulkan written, not
 *    compiled, card X25), and checked at load by the reflection check on
 *    every backend.
 *
 * Pass 13 (tests). Each item has a test, the shader's seen failing first
 *    on today's code, each figure named in the test (section 5): 1's
 *    "same pixels" is byte-equal readbacks; 3's crease occlusion a named
 *    0.2 and its tolerance a named 1/1024 of the unoccluded colour. Added:
 *    test 7 (a lit particle over the crease: not darkened, its cut-out
 *    material not discarding; seen failing first), test 8 (the layered
 *    surface still reads the view's occlusion), test 9 (the block's
 *    reflection check names `alpha_mode` at 284; the ray table's entry
 *    carries it), determinism (each scene drawn twice, the same bytes),
 *    and test 11, the cost measured (version 1 cited a test_lit_measure
 *    that doesn't exist; rabbittustests' measuring tests are named
 *    `test_<what>_measure`, test_lit_placed_measure the nearest).
 *
 * Pass 14 (how the rest composes with it, and what a caller sees). Section
 *    7 added: the scene's height fog and aerial volume reach a BLEND
 *    surface per fragment as any surface; the fog grid (LIT_FOG) and the
 *    clouds (LIT_CLOUDS) composite against the opaque depth before the
 *    blended surfaces, so a BLEND surface is not seen through the grid
 *    nearer than its far distance nor through a cloud (the commands' own
 *    paragraphs say so); the cover applies in every mode (a BLEND
 *    surface's snow is as see-through as the surface; a caller gives no
 *    response to a material that shouldn't carry it); LIT_SSR and the
 *    traced reflections see what was drawn before them, so a BLEND
 *    surface drawn after them is in no reflection; particles always
 *    BLEND. A caller sees one member, three values, starting OPAQUE.
 *
 * ===========================================================================
 * Revision notes, first round (passes 1 to 7, 2026-11-19, version 1),
 * kept as written; where the second round changed one, it says so
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Version 0 said Rend needed nothing for transparency: blending is the
 *    pipeline's (the presets), the order the caller's (ORDER_SUBMITTED),
 *    the fog and aerial volume premultiplied already (test 4 of the fog,
 *    "a half-transparent face 27 against 27.3"). Wrong twice, found in the
 *    shader: (a) a surface is always written premultiplied by its opacity,
 *    so an opaque surface drawn by an unblended pipeline whose base colour,
 *    map or vertex colour alpha is below 1 is written darker, and nothing
 *    says "the opacity is ignored" (glTF's OPAQUE); a caller can force the
 *    factor and the vertex colour to 1, never a map's alpha. (b) A blended
 *    surface reads the view's ambient occlusion and bent normals at its
 *    texel and a light's traced mask: all three were made from the opaque
 *    depth, so a pane of glass darkens with the crease behind it and takes
 *    the floor's shadow. Both need the surface's mode in the shader
 *    (pass 8.1: in its LitSurface). Added: the alpha mode.
 * 2. Missing: the depth-bounded cull (LIT_CULL_LIGHTS) bounds each tile
 *    by the opaque depth, so a blended surface in front of a tile's
 *    nearest depth misses a light whose range lies wholly in front of it.
 *    Said on the command ([CHANGED], a comment); a second list or a far
 *    bound alone is a later item when a caller with both asks.
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
 * cut-out's threshold read in the masked mode alone. Changed from version
 * 0's flag to the enum (Q1).
 *
 * Pass 3 (the C API to standard). The mode a field of the based values
 * struct (set and read with the rest by set_values and get_values); its
 * enum RBT_ENUM_BASE, as RBTREND_LIT_SHADOW_KIND. Appended after
 * attenuation_distance, so no field before it moves (pass 9.1 weighs a
 * chain). alpha_cutoff's meaning narrowed to MASK and its range checked
 * there. A zeroed field is OPAQUE (Q3).
 *
 * Pass 4 (data-oriented design and allocation). One field in the values;
 * one word of the material's block (pass 8.2: `attenuation.w`, the block
 * 288 bytes, not 256). No allocation anywhere; one uniform branch a
 * fragment; BLEND reads fewer textures.
 *
 * Pass 5 (threads, determinism, errors). As set_values (the material, one
 * caller at a time). The same pixels for every surface of opacity 1 as
 * before (test 5). Refusals each their own check and message, nothing
 * changed on failure.
 *
 * Pass 6 (the code as built, 2026-11-19). 1. The path trace and traced
 * reflections read the lit-material table: their hits stay opaque, as
 * built; the mode is carried in the table's entry and read by no ray yet.
 * 2. DEPTH_CUTOUT_FRAGMENT returns at alpha_cutoff <= 0: it tests the mode
 * instead. 3. rabbittustests' grey_values() memsets the struct: its
 * surfaces become OPAQUE, the same pixels at opacity 1; the tests drawing
 * a blended lit face set BLEND. 4. The particles: a build note then;
 * pass 8.3 makes it a change here.
 *
 * Pass 7 (tests, formats, records, the build). Tests named (section 5),
 * seen failing first on today's shader. No stored format (Rend stores no
 * material). A listed ABI break (section 6). No version raise while 0.3 is
 * untagged (card X2).
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
 *   blended one reading nothing the opaque depth made. Missing (pass 1.1);
 *   and the lit particles, always blended, read it today (pass 8.3).
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
 * [CHANGED] rbtrend_lit_material_set_values(): two refusals more, after the
 *           finite check, before the response's: "alpha_mode, %u, is not an
 *           RBTREND_LIT_ALPHA_MODE"; for MASK "alpha_cutoff, %g, is not above
 *           0" and "alpha_cutoff, %g, is above 1".
 * [CHANGED] rbtrend_lit_material_get_values(): returns the mode with the
 *           rest (its comment unchanged but for the member).
 * [CHANGED] RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT: discards for MASK
 *           alone.
 * [CHANGED] RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT: a sentence on
 *           BLEND's targets 1 and 2.
 * [CHANGED] RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT: a sentence (no
 *           mode; opaque; reads the view's inputs).
 * [CHANGED] the file comment's "Colors and light": the output by mode.
 * [CHANGED] RBTREND_COMMAND_LIT_CULL_LIGHTS: a comment (pass 1.2).
 * [CHANGED] rbtrendparticles.h: "Lit" and "Out of scope" (pass 8.3);
 *           RBTREND_PARTICLES_SHADER_LIT_FRAGMENT's behaviour.
 * Internal: rbtrend_lit_material.slang's block (`attenuation` a float3,
 * `alpha_mode` a uint after it; rbtrend_lit_material_block mirrored, still
 * 288 bytes, its reflection check naming it); rbtrend_lit_lighting.slang's
 * LitSurface::screen_space and the two reads it gates;
 * rbtrend_lit_surface.slang (the mode; MASK's discard after the maps'
 * reads), rbtrend_lit_layered.slang (screen_space 1),
 * rbtrend_particles_lit_frag.slang (BLEND, screen_space 0),
 * rbtrend_lit_depth_cutout_frag.slang (the mode).
 * Nothing in set 0 or the scene block (card A75): no binding, no byte.
 *
 * ===========================================================================
 * 3. What was compared (passes 2 and 10)
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
 *   opaque passes, sorted per primitive by distance, lit per pixel
 *   (Surface ForwardShading) or through a lighting volume; screen-space AO
 *   not on translucency (as remembered).
 * - Unity URP and HDRP: Surface Type Opaque or Transparent, Blending Mode
 *   (Alpha, Premultiply, Additive, Multiply), Alpha Clipping a separate
 *   toggle; transparents sorted back to front by their bounds' centre;
 *   SSAO applied to opaque surfaces alone (as remembered).
 * - Godot 4: transparency Disabled, Alpha, Alpha Scissor, Alpha Hash,
 *   Depth Pre-Pass; blend mode Mix, Add, Sub, Mul.
 * - Frostbite: sorted alpha blending of surfaces and particles after the
 *   opaque and volumetric passes (as remembered).
 * - Hazel (present, ../.refs/HazelRenderer2025/Hazel/Hazel/src/Hazel/
 *   Renderer, read): a material asset's `m_Transparent` chooses another
 *   shader, "HazelPBR_Transparent", with a `Transparency` uniform
 *   (MaterialAsset.cpp, its constructor and Get/SetTransparency); its
 *   pipeline blends SrcAlpha, OneMinusSrcAlpha (Pipeline.cpp); transparent
 *   meshes go to their own draw lists keyed by mesh, not sorted by
 *   distance (SceneRenderer.cpp, m_TransparentDrawList and
 *   m_TransparentStaticMeshDrawList); a transparent pre-depth pass feeds
 *   its composite; no animated transparent pipeline (its own TODO). Its
 *   shader (Hazelnut/Resources/Shaders/HazelPBR_Transparent.glsl) reads its
 *   cascades and point lights and no screen-space occlusion. Taken: a mode
 *   on the material, a pass after the opaque, no screen-space occlusion on
 *   it. Not taken: a shader of its own (one shader, the mode a uniform
 *   branch), straight alpha (Rend premultiplies), the unsorted list (the
 *   caller sorts).
 * - Order-independent transparency: weighted blended (McGuire and Bavoil,
 *   JCGT 2013), moment-based (Munstermann, Krumpen, Klein and Peters, HPG
 *   2018), per-pixel lists (Yang, Hensley, Grun and Thibieroz, EGSR 2010),
 *   stochastic (Enderton, Sintorn, Shirley and Luebke, I3D 2010). The
 *   first two need a weight made in the fragment shader written to two
 *   targets; the third, atomics and storage the shader writes. None is a
 *   pipeline choice over BLEND's one premultiplied output: each would be a
 *   lit fragment variant of Rend's, its own draft when a caller asks.
 *
 * ===========================================================================
 * 4. The modes in the shader
 * ===========================================================================
 *
 * opacity = base colour alpha x the base colour map's alpha x the colour's
 * alpha at location 4, clamped 0 to 1, as today. Then, in
 * lit_shade_with_base_map(), the maps read first, then:
 * - OPAQUE: opacity taken as 1; nothing discarded; screen_space 1; written
 *   (colour, 1).
 * - MASK: a fragment of opacity below alpha_cutoff discarded (after every
 *   map's read: pass 12.2), the rest as OPAQUE (today's alpha_cutoff above
 *   0, unchanged in its pixels).
 * - BLEND: screen_space 0; written (colour x opacity, opacity), today's
 *   alpha_cutoff 0 path, for a pipeline blending with
 *   rbtrend_get_blend_premultiplied() (over) or rbtrend_get_blend_additive()
 *   (its light times its opacity added).
 * - Any other value (only by rbtrend_material_set_uniform(), which checks
 *   nothing): read as OPAQUE.
 * In lit_light(), screen_space 0 reads no view occlusion (the ORM map's
 * occlusion still applies) and no bent normals (the shading normal); in
 * directional_visibility(), a directional light whose shadow is a traced
 * mask lights it unshadowed. Cascades and map shadows, the tile's lights,
 * the environment, the aerial volume, the fog and the cover as any
 * surface. Transmission (KHR_materials_transmission) is read in every
 * mode: a transmitting surface is usually OPAQUE (glTF's), drawn after
 * COPY_SCENE_COLOR.
 * The reflect variant the same, its specular weight premultiplied for
 * BLEND as today. The layered surface: screen_space 1, no mode. The lit
 * particle: BLEND whatever its material's mode, times its soft fade as
 * today.
 *
 * Cost: one branch on a value uniform over a draw, read from a row already
 * loaded; BLEND skips the view occlusion's sample and a traced mask's
 * fetch; a MASK fragment discarded reads the ORM, emission and normal maps
 * it skipped before. Estimated under 1% of the lit fragment (method:
 * operations counted against the fragment's several hundred; not measured;
 * when built, test_lit_alpha_measure, new, times a full-target BLEND and an
 * OPAQUE face against today's shader, as test_lit_placed_measure does its
 * lights; version 1 named a test_lit_measure that doesn't exist).
 *
 * ===========================================================================
 * 5. Tests (rabbittustests, Metal under API and shader validation, warnings
 *    fatal, Debug and Release; Vulkan type-checked; D3D12 written, card X25)
 * ===========================================================================
 *
 * Each figure named in the test; "the same pixels" is byte-equal readbacks.
 * 1. test_lit_alpha_opaque (seen failing first: darker): a face of base
 *    alpha 0.25, a base colour map of alpha 0 and a vertex colour of alpha
 *    0.5, OPAQUE, drawn by an unblended pipeline: the same pixels as at
 *    alpha 1, alpha written 1.
 * 2. test_lit_alpha_mask: the existing cut-out tests restated with MASK,
 *    the same pixels; an OPAQUE material with alpha_cutoff 0.5 and opacity
 *    0.25 not cut, in the surface and through DEPTH_CUTOUT_FRAGMENT (a
 *    shadow whole); a MASK checker under a minified view (its derivatives
 *    used after the discard) clean under shader validation.
 * 3. test_lit_alpha_blend_screen_space (seen failing first: occluded and
 *    shadowed): a BLEND face of opacity 1 under the premultiplied preset,
 *    over a crease whose view occlusion (crease_occlusion) is 0.2 at its
 *    texels: its colour within blend_tolerance (1/1024 of it) of the face
 *    with no view occlusion, its ORM occlusion still applied; bent normals
 *    on: not read; a light with a traced mask shadowing the floor behind
 *    it: the face lit, an OPAQUE face at the same place shadowed; a
 *    cascaded shadow over it: shadowed in both modes.
 * 4. test_lit_alpha_refusals: an alpha_mode past BLEND; MASK with an
 *    alpha_cutoff of 0, of 1.5, of NaN (the finite check's); each its
 *    message, nothing changed (get_values as before); a cutoff of 7 under
 *    OPAQUE or BLEND accepted and not read.
 * 5. Every existing lit test the same pixels: grey_values()'s surfaces
 *    OPAQUE at opacity 1; test_fog_sky_and_transparent()'s half-transparent
 *    face and the transmission tests set BLEND where they blend (27
 *    against 27.3 unchanged).
 * 6. Headless: set_values and get_values round-trip the mode;
 *    rbtrend_material_copy() carries it; the misuse harness regenerated.
 * 7. test_particles_lit_screen_space (seen failing first: darkened, and
 *    cut): a lit particle system over the crease with view occlusion on:
 *    its colour as with occlusion off; its material MASK at 0.5 with a
 *    base alpha of 0.25: drawn blended at 0.25, not discarded.
 * 8. test_lit_layered_screen_space: a layered surface over the crease
 *    still darkened by the view's occlusion (unchanged).
 * 9. The block: the reflection check passes with `alpha_mode` at byte 284
 *    and the block at 288 on Metal (the check runs at load on each
 *    backend); a path trace's material table read back holds the mode
 *    set.
 * 10. Determinism: tests 1, 3 and 7 drawn twice, the same bytes.
 * 11. test_lit_alpha_measure (Release, timed, no pass or fail): section 4's
 *    estimate against what ran, written into item 24.
 * test_frame_allocations unchanged (set_values allocates nothing).
 *
 * ===========================================================================
 * 6. ABI, versions, threads, records when built
 * ===========================================================================
 *
 * ABI: RBTREND_LIT_MATERIAL_VALUES grows from 144 to 152 bytes on a 64-bit
 * target (the mode and 4 bytes of tail padding; 136 to 140 on a 32-bit
 * one), a based struct grown by a field before 1.0: listed in the commit
 * from `./check.sh --update-abi`. Its start values change (OPAQUE where a
 * zero cutoff meant blended): a caller who blended a lit surface by
 * default sets BLEND. alpha_cutoff's refusals under MASK are new. A lit
 * particle's material no longer cuts out, and no lit particle reads the
 * view's screen-space inputs. No enum value moves; no struct type added.
 * No stored format. Versions: no raise while 0.3 is untagged (card X2).
 *
 * Threads: as rbtrend_lit_material_set_values() (rbtrendcontext.h,
 * "Threads"): the material, one caller at a time. Allocation: none.
 * Determinism: a shader branch on a value; no new source of difference.
 *
 * Records when built: RBTREND_TODO.md item 24 (this entry, checked, with
 * what ran); item 57's "Left" line (the particles' change done); item 56's
 * stage 3 note (the grid read by BLEND surfaces, after A75's binding);
 * claude_made_decisions.md for any shape chosen while building; abi.txt.
 *
 * ===========================================================================
 * 7. How the rest composes with it (pass 14)
 * ===========================================================================
 *
 * - The scene's height fog (stage 1) and the aerial volume: per fragment,
 *   every mode, premultiplied with the rest for BLEND (test 5's fog face).
 * - The fog grid (LIT_FOG) and the clouds (LIT_CLOUDS): composited against
 *   the opaque depth before the blended surfaces (their "frame's order"),
 *   so a BLEND surface nearer than the grid's far distance sees the fog
 *   beyond it only, and one beyond a cloud draws over the cloud. Stage 3
 *   reads the grid at the fragment's own place, so a BLEND surface reads
 *   it then; its binding waits on card A75's.
 * - The cover: applied in every mode; a BLEND surface's snow is as
 *   see-through as the surface. A caller gives no response to a material
 *   whose cover it doesn't want.
 * - Reflections: LIT_SSR and the traced reflections reflect what was drawn
 *   before them; a BLEND surface drawn after them is in none. The path
 *   trace's hits stay opaque (pass 6.1).
 * - Particles: blended always, the same lighting, none of the view's
 *   screen-space inputs.
 * - The output encoding: a BLEND surface into an 8-bit sRGB-encoded target
 *   (RBTREND_LIT_VIEW::output_encoding) blends encoded values, as today:
 *   a float target and the tone map blend in linear light.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (each with a recommendation; approved 2026-11-20
 * as recommended)
 * ---------------------------------------------------------------------------
 *
 * Q1. How a surface says what it is: a, an enum field, OPAQUE, MASK,
 *     BLEND, alpha_cutoff read for MASK (RECOMMENDED: glTF's and every
 *     engine's shape; one meaning a value); b, a bool `opaque`, alpha_cutoff
 *     keeping its two meanings (smaller, the mode spread over two fields);
 *     c, a negative alpha_cutoff meaning opaque (no layout change; a
 *     sentinel no reader would guess).
 * Q2. What BLEND leaves out: a, every screen-space input the opaque depth
 *     made: the view's occlusion, bent normals, a light's traced mask
 *     (RECOMMENDED: each holds the surface behind; Hazel's transparent
 *     shader reads none); b, the occlusion and bent normals only, a traced
 *     mask still read (a shadow at the wrong depth); c, nothing, a caller's
 *     problem (no caller can fix it: the scene's set is one a frame).
 * Q3. A zeroed alpha_mode: a, OPAQUE (RECOMMENDED: glTF's and Filament's
 *     default; a zeroed struct writes what an unblended pipeline expects);
 *     b, BLEND (today's zero-cutoff meaning, no test changes; an opaque
 *     surface of alpha below 1 still written darker unless set).
 * Q4. Where the mode lives (pass 9.1): a, a member appended to
 *     RBTREND_LIT_MATERIAL_VALUES (RECOMMENDED: every material has a mode
 *     and every get_values reads it; before 1.0 a listed break); b, a
 *     struct chained on as the cover's response is (no layout change, but
 *     "no chain" must then mean OPAQUE and a reader chains to learn a value
 *     every material has).
 */

/* ========================================================================= */
/* render/rbtrendlit.h                                                       */
/* ========================================================================= */

/*
 * The file comment's "Colors and light", its last sentence [CHANGED]:
 *
 *   ... (RBTREND_LIT_VIEW::output_encoding). What alpha it writes is the
 *   material's alpha mode's (RBTREND_LIT_ALPHA_MODE): 1 for an opaque or
 *   cut-out surface, its opacity for a blended one, whose colour is then
 *   premultiplied by it; blending is the pipeline's.
 *
 * Was: "It is premultiplied by the opacity: blending is the pipeline's, and
 * an opaque surface writes alpha 1.", which no value let a surface be.
 */

/**
 * @brief What a lit surface's opacity does (glTF 2.0's alphaMode).
 *
 * Its opacity is the base color's alpha times the base color map's times
 * the color's alpha at location 4, 0 to 1. Only appended to.
 */
typedef enum RBTREND_LIT_ALPHA_MODE RBT_ENUM_BASE {
	/** @brief Opaque: the opacity is ignored; the surface is written whole,
	 * alpha 1, for a pipeline that doesn't blend. A zeroed member is this. */
	RBTREND_LIT_ALPHA_MODE_OPAQUE = 0,
	/** @brief A cut-out: a fragment whose opacity is below
	 * RBTREND_LIT_MATERIAL_VALUES::alpha_cutoff is discarded, in its shadows
	 * too where they are rendered with RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT;
	 * the rest is opaque. */
	RBTREND_LIT_ALPHA_MODE_MASK   = 1,
	/**
	 * @brief Blended: written premultiplied by its opacity, for a pipeline
	 * that blends it over what is drawn (rbtrend_get_blend_premultiplied())
	 * or adds its light times its opacity (rbtrend_get_blend_additive()),
	 * drawn after the opaque surfaces in the caller's order, back to front
	 * for the first.
	 *
	 * It reads nothing of the view's that the opaque depth made, which
	 * holds the surface behind it: not the view's ambient occlusion
	 * (rbtrend_lit_scene_set_ambient_occlusion(); its ORM map's still
	 * applies) nor its bent normals, and a directional light whose shadow
	 * is a traced mask (RBTREND_LIT_SHADOW_KIND_MASK) lights it unshadowed.
	 * Cascades and map shadows, the lights, the environment, the scene's
	 * atmosphere, fog and cover reach it as any surface.
	 */
	RBTREND_LIT_ALPHA_MODE_BLEND  = 2,
} RBTREND_LIT_ALPHA_MODE;
/* [ADDED] Why: an opaque surface's opacity can't be ignored today (pass
 * 1.1a), and a blended one reads the opaque's screen-space inputs (1.1b).
 * glTF's three modes (pass 2). */

/*
 * RBTREND_LIT_MATERIAL_VALUES, one member [CHANGED] and one [ADDED]:
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
 *	 * material starts OPAQUE (rbtrend_lit_material_create()). Written by
 *	 * name with rbtrend_material_set_uniform() (`alpha_mode`), unchecked, a
 *	 * value the enum doesn't name is drawn as OPAQUE. * /
 *	RBTREND_LIT_ALPHA_MODE alpha_mode;
 *	[ADDED, appended after attenuation_distance] Why: section 2; appended
 *	so no member before it moves (Q4).
 */

/*
 * rbtrend_lit_material_create() [CHANGED], its comment's start values: "...
 * opaque (alpha mode OPAQUE; alpha_cutoff 0, not read) ..." (was "no
 * cut-out", alpha_cutoff 0, which meant blended).
 *
 * rbtrend_lit_material_set_values() [CHANGED], its @return: "...
 * RBT_RESULT_INVALID_ARGUMENT ... a value is not finite, alpha_mode isn't
 * one RBTREND_LIT_ALPHA_MODE names, or alpha_mode is MASK and alpha_cutoff
 * isn't above 0 or is above 1 (each its own check and message, in that
 * order, after the finite check and before the response's)."
 *
 * rbtrend_lit_material_get_values() [CHANGED]: no text change; the member
 * is returned with the rest.
 *
 * RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT [CHANGED]: "... for cut-out
 * materials: a material of RBTREND_LIT_ALPHA_MODE_MASK discards a fragment
 * whose opacity ... is below its alpha_cutoff; any other is written whole
 * (an opaque or a blended material's shadow is its whole shape)."
 *
 * RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT [CHANGED], a sentence added:
 * "A blended surface (RBTREND_LIT_ALPHA_MODE_BLEND) writes targets 1 and 2
 * premultiplied by its opacity as target 0 is: the caller blends them with
 * target 0's blend, or not at all (blends that differ a target need
 * CRGPU_FEATURE_INDEPENDENT_BLEND)."
 *
 * RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT [CHANGED], a sentence added:
 * "It has no alpha mode: it reads the view's screen-space inputs as an
 * opaque surface does."
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
 * Why: pass 1.2; a comment only.
 */

/* ========================================================================= */
/* render/rbtrendparticles.h                                                 */
/* ========================================================================= */

/*
 * "Lit" [CHANGED], a sentence added: "It shades the material as
 * RBTREND_LIT_ALPHA_MODE_BLEND whatever the material's mode (rbtrendlit.h):
 * no cut-out, and none of the view's screen-space inputs (its ambient
 * occlusion, bent normals, a traced mask), which hold the surface behind
 * the particle."
 *
 * "Out of scope" [CHANGED]: its last sentence ("A material's alpha mode:
 * the lit fragment shader shades its material as blended by its opacity
 * whatever its alpha_cutoff (a cut-out discards, as a lit surface's
 * does).") removed.
 *
 * RBTREND_PARTICLES_SHADER_LIT_FRAGMENT [CHANGED], behaviour only, its
 * comment pointing at "Lit".
 *
 * Why: pass 8.3; item 57's "Left" line.
 */
