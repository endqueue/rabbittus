/*
 * rbtrend_scene_block_draft.h - Rend's lit scene block split: its
 * per-light and per-shadow-view arrays (the point and spot lights, their
 * shadow records, the shadow views: every cascade, spot map and point
 * light face) leave the uniform block for the scene's storage buffer, and
 * the scalars stay in the uniform block, which goes from 16,384 bytes (full)
 * to 1,024 (card A75).
 *
 * DRAFT, 2026-11-20, version 2 (version 1, then seven critique passes,
 * each fixed before the next), for the user's approval; not built.
 * Card A75 decided 2026-11-20 (b): "split it, the per-light and per-cascade
 * arrays into a storage buffer of their own and the scalars kept in the
 * uniform block (a set 0 binding freed or merged first); a Rend public
 * layout change for every lit shader a caller writes, so a draft first".
 * RBTREND_TODO.md item 64 (the cover took the block's last 48 bytes).
 *
 * No version raise while Rabbittus' 0.3 is untagged (card X2).
 *
 * Written unattended on the Mac (Apple M5 Pro, macOS 27) against
 * include/render/rbtrendlit.h ("Sets", lines 35 to 52; the limits, 156 to
 * 174; RBTREND_LIT_SCENE_DESC and rbtrend_lit_scene_create(), 1040 to
 * 1110; RBTREND_LIT_COVER, 1964), include/render/rbtrendmaterial.h ("When
 * uniform values take effect": an owned block's one copy in the material
 * arena, copied from transient memory by the GPU ahead of a submission's
 * commands, waiting on the earlier submissions that read it),
 * shaders/rbtrend_lit_scene.slang (RbtrendLitScene, binding 0 of set 0),
 * shaders/rbtrend_lit_bindings.slang (set 0's sixteen bindings; binding 1
 * RbtrendLitTiles, raw, readonly), rbtrend_lit_lighting.slang (lines 156,
 * 251, 468 to 476: the shadow views, the shadows, the lights by tile or
 * all), rbtrend_lit_cull.slang (binding 1 read-write, lines 79 to 92),
 * rbtrend_fog_inject_comp.slang (71: a shadow view), rbtrend_rt_path_trace.slang
 * (470) and rbtrend_rt_reflections_comp.slang (112: the lights);
 * src/render/rbtrendlit.cpp (the block's static_asserts, 63 to 69;
 * rbtrend_lit_scene_layout(), 1348; the cull layout, 1095 to 1115;
 * rbtrend_lit_scene_resolve_shadows(), 2486; rbtrend_lit_scene_commit(),
 * 2598, which writes the whole block into two materials, the scene's and
 * the cull's; rbtrend_lit_scene_rt_set() and _ssr_set(), 2607 and 2628,
 * each binding the block's arena range alone; scene creation, 2800 to 2860;
 * set_view's tiles buffer, 2925 to 2950), rbtrendlit_internal.hpp
 * (rbtrend_lit_scene_block, 101), rbtrendmaterial.cpp (the owned blocks'
 * copies, 320 to 345), rbtrendparticles.cpp (117: the particles' lit
 * pipeline takes the scene layout at slot 0), rbtrendraytracing.cpp (1849
 * to 1971: the traces' scene layout from their shader, set 0); the drafts
 * waiting on this: rbtrend_transparency_draft.h (approved; "nothing in set 0
 * or the scene block"), rbtrend_fog_draft.h (stage 3: transparents read the
 * fog's grid, a set-0 binding), rbtrend_clouds_draft.h (stage 3: cloud
 * shadows, a set-0 binding); ../carrottus/include/crgpu.h
 * (CRGPU_MAX_DESCRIPTOR_BINDINGS_PER_SET 16; storage_buffer_offset_alignment);
 * and Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel, present:
 * Renderer/SceneRenderer.h and .cpp, Scene/Scene.h, read for its lights'
 * buffers). rabbittus b77c873, 0.3, untagged. Not built, not in any target,
 * not compilable. Every declaration below is marked [ADDED], [CHANGED] or
 * [REMOVED]; nothing unmarked changes.
 *
 * ---------------------------------------------------------------------------
 * Revision notes (version 1 written, then seven passes, each fixed before
 * the next; one lens a pass)
 * ---------------------------------------------------------------------------
 *
 * Pass 1 (correctness against the built code).
 *  1. Wrong in the card and in item 64: "the lights at 32 bytes each". An
 *     RbtrendLitLight is four float4, 64 bytes (rbtrendlit.cpp 63's
 *     static_assert: 64 * RBTREND_LIT_MAX_LIGHTS). The block: the two
 *     matrices 128, shadow_views 64 x 80 = 5,120, directional 4 x 112 = 448,
 *     shadows 64 x 32 = 2,048, the scalars and small vectors 208, lights
 *     128 x 64 = 8,192, atmosphere 112, fog 80, cover 48: 16,384. The arrays
 *     that move (lights, shadows, shadow_views) are 15,360; 1,024 stay.
 *  2. Found: the block is written twice a commit, into the scene's material
 *     and the cull's (rbtrendlit.cpp 2603 and 2604), so each change copies
 *     32 KiB from transient memory, and the arena holds two 16 KiB ranges a
 *     scene. After the split both materials keep 1 KiB blocks and the
 *     arrays are copied once, into one buffer both bind.
 *  3. Found: the traces' and screen-space reflections' scene sets bind the
 *     block's arena range alone (rbtrend_lit_scene_rt_set(), _ssr_set());
 *     the traces read the lights (path trace 470, traced reflections 112),
 *     so their set gains the scene's storage buffer at binding 1. The
 *     screen-space reflection reads no array: its set is unchanged.
 *  4. Found: an untiled view reads every light (lighting 475), so the
 *     arrays are needed with no tiles; today the tiles buffer is the
 *     placeholder then. The scene now owns its storage buffer from
 *     creation (the arrays), and set_view remakes it with the tiles after
 *     them as today's tiles buffer is remade.
 *  5. Checked: the directional records (448 bytes) are read by the fog's
 *     inject, the clouds' march and reconstruct, the atmosphere's shells and
 *     the traced mask, all through the block; they stay in it (Q2), so none
 *     of those layouts changes. The fog's inject reads a shadow view (71)
 *     and so binds binding 1 (it binds the scene's own set, which has it).
 *
 * Pass 2 (Rend's API conventions: what is public, what is the caller's,
 * errors, no default that decides).
 *  1. Wrong in the card's wording, "a Rend public layout change for every
 *     lit shader a caller writes": rbtrendlit.h says a caller's vertex
 *     shader "declares nothing in sets 0 and 1" (49 to 50), and the
 *     fragment shaders that read set 0 are Rend's. Searched: no shader
 *     outside Rend imports rbtrend_lit_scene or declares set 0's bindings.
 *     The layout object a caller's pipeline names at slot 0 comes from
 *     rbtrend_lit_get_scene_layout(), and changes inside. So the public
 *     change is comments (the "Sets" paragraph; what a scene allocates and
 *     when) and no declaration; ABI: none. Said in "What callers above
 *     see".
 *  2. The limits (RBTREND_LIT_MAX_LIGHTS 128, _MAX_SHADOW_VIEWS 64) stay:
 *     the storage buffer could hold more, but a raise is a decision of its
 *     own (tile lists, cull cost), not this card's. "Left out".
 *  3. No new error: a failed buffer at scene creation is the create's
 *     RBT_RESULT_REND_GPU_FAILURE or OUT_OF_MEMORY, as its other parts are;
 *     at set_view as today's tiles buffer (rbtrend_lit_storage_buffer()'s
 *     result).
 *
 * Pass 3 (the platform APIs on all three backends, through Carrottus GPU).
 *  1. Vulkan: a uniform buffer's guaranteed range is 16,384
 *     (maxUniformBufferRange's minimum), a storage buffer's 2^27
 *     (maxStorageBufferRange); storage buffers per stage at least 4
 *     (maxPerStageDescriptorStorageBuffers): the fragment stage has one,
 *     binding 1, before and after. A fragment stage's storage buffer is
 *     readonly (NonWritable) on a device without fragmentStoresAndAtomics,
 *     as binding 1 already is.
 *  2. D3D12: Carrottus GPU views every storage buffer raw
 *     (rbtrend_lit_bindings.slang's comment), so the arrays are read with
 *     Load at byte offsets, as the tiles are; offsets multiples of 16.
 *  3. Metal: a storage buffer is a device-address-space buffer in the set's
 *     argument buffer, as binding 1 is now; nothing new.
 *  4. Found while counting (a fact for the set-0 plan, not this card): the
 *     lit fragment stage samples 11 images from set 0 (the atlas, two
 *     cubes, the BRDF table, occlusion, scene colour, four masks, the
 *     atmosphere's array) and 4 from set 1, 15 of the 16 Vulkan guarantees
 *     a stage (maxPerStageDescriptorSampledImages' minimum). The fog's
 *     stage 3 and the clouds' stage 3 each want one more image in set 0:
 *     they need sampled images freed, not only bindings. Q1 (b) is how;
 *     this draft spends no sampled image.
 *
 * Pass 4 (industry practice, and Hazel).
 *  1. Hazel (read here): SceneRenderer.h's UBPointLights holds 1024
 *     PointLights in a uniform buffer of its own, UBSpotLights 1000
 *     SpotLights, UBSpotShadowData 1000 matrices, UBShadow four cascade
 *     matrices, UBScene the scalars, and storage buffers of visible light
 *     indices per tile (m_SBSVisiblePointLightIndicesBuffer). So Hazel
 *     splits as card A75 does, arrays apart from scalars, but into uniform
 *     buffers past Vulkan's 16 KiB floor (it counts on desktop drivers' 64
 *     KiB); Rend keeps to the floor and takes a storage buffer.
 *  2. Unreal (as remembered): forward lights in structured buffers
 *     (ForwardLightData's buffers), view scalars in a uniform buffer. Unity
 *     URP (as remembered): additional lights in a structured buffer where
 *     storage buffers are available, constant arrays otherwise. Filament
 *     (as remembered): lights in a uniform buffer sized to 16 KiB at most
 *     (256 records), froxel records apart. Bevy (as remembered): point
 *     lights in a storage buffer where the device has them, a uniform
 *     fallback of 256. The common shape: scalars in a uniform block, the
 *     per-light arrays in a storage buffer.
 *  3. Divergent indices: within a tile every pixel reads the same lights,
 *     but neighbouring tiles in one SIMD group can differ; NVIDIA's constant
 *     cache serializes divergent addresses, its storage path doesn't (as
 *     remembered of NVIDIA's guidance). So the move may help on NVIDIA and
 *     should be near even on Apple: measured, not assumed (pass 5).
 *
 * Pass 5 (performance).
 *  1. Version 1 copied the whole 15,360 bytes at every change. Now each
 *     array's live prefix only (lights up to light_count, shadows and views
 *     up to the counts resolve_shadows reaches), one copy each, from the
 *     same transient span (Q3): a scene of 8 lights and 4 cascades copies
 *     512 + 0 + 320 bytes, not 15 KiB. The rest of the region is never
 *     read (every shader bounds its loops by the counts in the block).
 *  2. The block's copy drops from 2 x 16,384 to 2 x 1,024 bytes a change.
 *  3. Reads: the lighting loop's lights move from the constant path to the
 *     storage path. Measured (Tests, 8) on the M5 Pro before and after, 128
 *     point lights tiled over 2560 x 1440 and 64 shadow views; Windows'
 *     two GPUs owed (where pass 4, 3 predicts a gain on the A2000).
 *  4. Memory: per scene, the arena's ranges shrink by 2 x 15,360 bytes and
 *     a storage buffer of 15,360 bytes plus the tiles is made at creation
 *     (the tiles buffer was made at set_view): net about 15 KiB less a scene.
 *  5. No allocation in a steady frame: the copies take transient memory as
 *     the block does; test_frame_allocations holds it.
 *
 * Pass 6 (tests: each failing first, on every backend).
 *  1. The strongest check is that nothing changes on screen: every lit GPU
 *     test's printed figures and hashes are the same before and after,
 *     bit for bit, Debug and Release, both devices (the same floats reach
 *     the same math). Not a failing-first test; it is the regression gate.
 *  2. Failing first: each new test is run against a stub that makes the
 *     storage buffer but never copies the arrays into it (zeros: no light,
 *     shadow views of zero matrices), so every lit pixel a light should
 *     reach is black and each test fails; then the copies are built.
 *  3. Rend's backends: Metal built and run here; D3D12 and Vulkan never
 *     built from Rend's side (no backend branch in Rend: the shaders are
 *     compiled for each, so their reflection checks run here at build).
 *
 * Pass 7 (what callers above see).
 *  1. No declaration changes; no rebuild of a caller's bindings beyond the
 *     library itself. A caller's pipelines take the scene layout from
 *     rbtrend_lit_get_scene_layout() as before; its set 0 binding 1 is now
 *     the scene's storage buffer (arrays, then tiles), its stages widened to
 *     every reader (the traces' set included).
 *  2. Pixels: the same (pass 6, 1).
 *  3. Memory and timing: rbtrend_lit_scene_create() makes the scene's
 *     storage buffer (about 15 KiB) where it made none; set_view remakes it
 *     with the tiles as it remade the tiles buffer. Said in the header.
 *  4. The next scene-wide value has 15 KiB of the block: the waiting
 *     stages (the fog's 3, the clouds' 2 and 3, a caller's tiling) take
 *     their bytes there; their set-0 images are pass 3, 4's matter.
 *
 * ---------------------------------------------------------------------------
 * 1. Why
 * ---------------------------------------------------------------------------
 *
 * The lit scene's uniform block is exactly the 16,384 bytes Vulkan
 * guarantees a uniform buffer, after the cover packed its last fields into
 * half floats, and set 0's sixteen bindings are taken. The next scene-wide
 * value has nowhere to go. Most of the block (15,360 bytes) is three
 * fixed-size arrays read by index; those belong in a storage buffer, where
 * the size floor is 2^27 bytes, and the scalars read uniformly by every
 * pixel stay in the uniform block.
 *
 * ---------------------------------------------------------------------------
 * 2. What changes, in one paragraph
 * ---------------------------------------------------------------------------
 *
 * Internal: RbtrendLitScene loses lights[128], shadows[64] and
 * shadow_views[64] and becomes 1,024 bytes. Binding 1 of set 0, today the
 * view's tile lists, becomes the scene's storage buffer: the lights at byte
 * 0, the shadows at 8,192, the shadow views at 10,240, the tiles from
 * 15,360 (Q1 a: a binding merged, none added). Rend's shaders read the three
 * arrays through accessors in one module (lit_light(i), lit_shadow(i),
 * lit_shadow_view(i)), loading float4s at fixed offsets (Q4). The arrays are
 * written by the material's owned-block rule, their live prefixes only, into
 * the head of the scene's buffer (Q3). The traces' scene set gains binding
 * 1. Public: comments of rbtrendlit.h only; ABI none.
 *
 * ---------------------------------------------------------------------------
 * 3. The layouts (internal; the shader is the definition, the CPU mirrors)
 * ---------------------------------------------------------------------------
 *
 * Set 0, after:
 *  - binding 0, uniform: RbtrendLitScene, 1,024 bytes: view_projection,
 *    inverse_view_projection, directional[4], eye_position ...
 *    scene_color_mips (as now), the atmosphere's seven float4, the fog's
 *    five, the cover's three. 15,360 bytes free.
 *  - binding 1, storage, raw (RbtrendLitStorage; read-write in the cull,
 *    readonly elsewhere):
 *      [0, 8192)       RbtrendLitLight lights[128], 64 bytes each
 *      [8192, 10240)   RbtrendLitShadow shadows[64], 32 bytes each
 *      [10240, 15360)  RbtrendLitShadowView shadow_views[64], 80 bytes each
 *      [15360, ...)    the view's tiles, as today (a count and up to 63
 *                      indices a tile), present only for a tiled view
 *    Offsets named once (rbtrend_lit_scene.slang constants, mirrored by
 *    static_asserts in rbtrendlit.cpp); every one a multiple of 256, so a
 *    later range binding meets every backend's storage offset alignment.
 *  - bindings 2 to 15: unchanged.
 *
 * ---------------------------------------------------------------------------
 * 4. What was compared
 * ---------------------------------------------------------------------------
 *
 *  - Hazel (read here): pass 4, 1.
 *  - Unreal, Unity URP, Filament, Bevy (as remembered): pass 4, 2.
 *  - Vulkan's limits and D3D12's raw views, through Carrottus GPU: pass 3.
 *
 * ---------------------------------------------------------------------------
 * 5. Left out, and why
 * ---------------------------------------------------------------------------
 *
 *  - Raising RBTREND_LIT_MAX_LIGHTS or _MAX_SHADOW_VIEWS: a decision of its
 *    own (pass 2, 2).
 *  - Unpacking the cover's half floats now that there is room: a built
 *    layout and its test's figures; a later item's if wanted.
 *  - The set-0 plan for the fog's and clouds' stage 3 (their images): their
 *    drafts', with pass 3, 4's count and Q1 (b) as the likely way.
 *  - The directional records' move (Q2 b).
 *
 * ---------------------------------------------------------------------------
 * 6. Questions (each with options and a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. THE BINDING (set 0's sixteen are taken).
 *     (a) RECOMMENDED. Merged: binding 1 becomes the scene's storage
 *         buffer, the arrays at its head and the tiles after. No binding
 *         added, none removed, no sampled image spent; the two readers of
 *         the lights that bind set 0 (the surfaces, the cull) already bind
 *         binding 1. The arrays share a buffer with the tiles the cull
 *         writes: the copy into the head is ordered before the cull by the
 *         copy rule (ahead of the submission's commands), and the cull
 *         writes only past 15,360.
 *     (b) Freed: the four directional mask bindings (11 to 14) become one
 *         2D array view of four layers, freeing three bindings and three
 *         sampled images, the arrays in a storage buffer of their own at one
 *         of them. A public change: a mask shadow names a layer of one
 *         array view (RBTREND_LIT_SHADOW's `_mask`) and
 *         RBTREND_COMMAND_LIT_SHADOW_MASK writes a layer. The way for the
 *         stage-3 images (pass 3, 4), not needed for this card.
 *     (c) Freed: the two comparison samplers (3, 4) merged, a reversed atlas
 *         compared by hand from a gather: a filter's cost and look change.
 *     (d) Carrottus GPU's sixteen raised: a Carrottus draft, and Vulkan's
 *         per-stage floors still bound the images.
 *
 * Q2. WHAT MOVES.
 *     (a) RECOMMENDED. The lights, the shadows and the shadow views (15,360
 *         bytes), the arrays read by index. The directional records stay:
 *         four, read by the same index across a draw, and read by five
 *         compute passes through the block alone.
 *     (b) The directional records too (448 bytes more room): five more
 *         layouts gain binding 1.
 *
 * Q3. HOW THE ARRAYS REACH THE GPU.
 *     (a) RECOMMENDED. rbtrendmaterial.h's owned-block rule, its destination
 *         the scene buffer's head instead of an arena range: at a submission
 *         of a list that binds the scene, the arrays changed since their last
 *         copy are written to transient memory and copied by the GPU ahead of
 *         the list's commands, waiting on the earlier submissions that read
 *         them; each array's live prefix only (pass 5, 1). Internal to Rend
 *         (a slot whose destination is a buffer of the lit scene's).
 *     (b) A per-frame storage buffer written by rbtrend_buffer_write():
 *         frames_in_flight copies, each bound by its own set per frame, so
 *         the scene's material rebuilds or keeps a set per frame.
 *     (c) The whole 15,360 bytes each change: simpler, up to 30 times the
 *         bytes for a small scene.
 *
 * Q4. HOW THE SHADERS READ.
 *     (a) RECOMMENDED. Accessors loading float4s (Load4) at the fixed
 *         offsets and building each struct, the view's matrix from its four
 *         columns as the CPU writes them: the layout is exactly the CPU
 *         struct's on every target.
 *     (b) Slang's generic Load<T>: shorter, and a matrix's majority per
 *         target taken on trust; checked only by pixels.
 *
 * ---------------------------------------------------------------------------
 * 7. Breaks
 * ---------------------------------------------------------------------------
 *
 *  - Public declarations: none. ABI: none (./check.sh abi shows nothing).
 *  - Public comments: rbtrendlit.h's "Sets" paragraph, rbtrend_lit_scene_create()
 *    (what it makes), rbtrend_lit_scene_set_view() (what it remakes).
 *  - Behaviour: none on screen; memory and copy sizes as pass 5.
 *  - Internal layouts: RbtrendLitScene and rbtrend_lit_scene_block (1,024
 *    bytes; the reflection check rbtrend_lit_check_scene_block() follows);
 *    set 0 binding 1's contents; the traces' scene layout and set.
 *  - No version raise while 0.3 is untagged (card X2).
 *
 * ---------------------------------------------------------------------------
 * 8. Tests
 * ---------------------------------------------------------------------------
 *
 * rabbittustests (Metal's validation with warnings as asserts, Debug and
 * Release, both devices), and Rend's own:
 *  1. Every lit, fog, clouds, atmosphere, particles and trace GPU test's
 *     printed figures and hashes the same as before the build, line for
 *     line (the regression gate).
 *  2. test_lit_scene_arrays_last: the last of each array: light 127 (a point
 *     light lighting a quad, its pixel the reference), shadow 63 and shadow
 *     view 63 (a spot's map shadow placed at the last record, half the quad
 *     shadowed). Fails first against the stub (pass 6, 2).
 *  3. test_lit_scene_arrays_untiled: an untiled view (no target size) with
 *     8 lights: every light reached through the loop over all.
 *  4. test_lit_scene_arrays_resize: a tiled view resized (the tiles' count
 *     changes, the buffer remade) with no light set again: the lights still
 *     drawn (the arrays copied into the new buffer). Fails first if the
 *     remade buffer isn't marked for the copy.
 *  5. test_lit_scene_arrays_prefix: 3 lights, then 1 (light_count lowered):
 *     the two lights past the count gone from the picture; then 3 again,
 *     set anew: all three.
 *  6. The traces with lights: the path trace's and traced reflections'
 *     existing light cases unchanged (1), and their scene set's binding 1
 *     checked in rbtrend_contract_tests (the traces' layout has it).
 *  7. test_frame_allocations: 128 lights and 64 shadow views set every frame,
 *     0 of ours; the transient bytes a frame printed (the prefixes).
 *  8. test_lit_scene_arrays_measure (Release, no validation, the M5 Pro;
 *     Windows owed): 128 point lights tiled over 2560 x 1440, 64 shadow
 *     views; submit to idle, three rounds of the mean of 50 after 10, before
 *     the build (the same test against the old block) and after.
 *  9. The block's static_asserts (1,024 bytes; the offsets of section 3)
 *     and the reflection checks, at build and layout creation.
 * Where each runs: the Mac (Metal). D3D12 and Vulkan owed on Windows 11 and
 * Fedora (the shaders compiled for each at build; their runs owed).
 *
 * ---------------------------------------------------------------------------
 * 9. rbtrendlit.h (comments only)
 * ---------------------------------------------------------------------------
 */

/*
 * [CHANGED] (comment only) the file comment's "Sets":
 *
 * - set RBTREND_LIT_SCENE_SET (0), rbtrend_lit_get_scene_layout(): what one
 *   view of the world has in common - the eye, the lights, the ambient light
 *   and the shadows: a uniform block of the scene's values and a storage
 *   buffer of its lights, shadows and shadow views and the view's tile
 *   lists, beside its textures. ... (unchanged after)
 */

/**
 * [CHANGED] (comment only)
 * @brief Creates a scene. ...
 *
 * It makes, with its own material, RBTREND_LIT_MAX_SHADOW_VIEWS shadow pass
 * materials ..., `max_view_sets` records ..., [ADDED] and a storage buffer
 * of 15,360 bytes for its lights, shadows and shadow views (with the view's
 * tiles after them once rbtrend_lit_scene_set_view() gives a target): what
 * setting a shadow or compiling a command needs is allocated here, once.
 * ... (unchanged after)
 */

/**
 * [CHANGED] (comment only)
 * @brief rbtrend_lit_scene_set_view(): "a view with a target and a
 * projection makes the scene's tiles buffer, again when the number of tiles
 * changes" becomes "... remakes the scene's storage buffer, its lights and
 * shadows and then the tiles, again when the number of tiles changes; the
 * lights and shadows are copied into the new one at the next submission".
 */

/*
 * ---------------------------------------------------------------------------
 * 10. Where it is done (not in a public header)
 * ---------------------------------------------------------------------------
 *
 * - shaders/rbtrend_lit_scene.slang: the three arrays out of RbtrendLitScene;
 *   the offsets' constants; rbtrend_lit_bindings.slang's binding 1 renamed
 *   RbtrendLitStorage (readonly), its comment, tile_lights() from 15,360,
 *   and lit_light(), lit_shadow(), lit_shadow_view() (Q4 a);
 *   rbtrend_lit_cull.slang's read-write declaration the same name, its
 *   lights through lit_light()'s twin over the RW buffer and its tiles
 *   stored from 15,360; rbtrend_lit_lighting.slang (156, 251, 473, 476),
 *   rbtrend_fog_inject_comp.slang (71), rbtrend_rt_path_trace.slang (470),
 *   rbtrend_rt_reflections_comp.slang (112) through the accessors.
 * - rbtrendlit_internal.hpp: rbtrend_lit_scene_block without the three
 *   arrays; rbtrend_lit_scene_arrays (the three, CPU side, RBT_PLAIN_DATA)
 *   on the scene; the scene's storage buffer replacing `tiles`.
 * - rbtrendlit.cpp: the static_asserts (1,024; the arrays' sizes and
 *   offsets); rbtrend_lit_scene_resolve_shadows() writing the scene's
 *   arrays; commit writing the 1 KiB block into both materials and marking
 *   the arrays' prefixes dirty; creation making the buffer; set_view
 *   remaking it and marking every prefix dirty; rt_set adding binding 1.
 * - rbtrendmaterial.cpp: an owned slot's destination a buffer and offset
 *   of the owner's (the arena's range, or the lit scene's buffer), copied
 *   by the same rbtrend_upload_copy list, the same wait; a slot may carry
 *   up to three ranges (the prefixes).
 *
 * ---------------------------------------------------------------------------
 * 11. Records, when it is built
 * ---------------------------------------------------------------------------
 *
 * RBTREND_TODO.md item 64's A75 paragraph: built, the measurements, the
 * regression gate's result. rabbittus/CLAUDE.md where Rend's set 0 is
 * described. The workspace's DECISIONS.md: a card for the set-0 images of
 * the stage-3 drafts (pass 3, 4; Q1 b), and the cover's half floats if
 * wanted.
 */
