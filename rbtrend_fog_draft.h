/*
 * rbtrend_fog_draft.h - card A59's Rend half: fog near the eye, in the
 * decided stages. Stage 1, an analytic height fog, a setting of the lit
 * scene applied by Rend's lit shaders to every lit surface, the sky and
 * transparent surfaces. Stage 2, rbtrend_fog: a froxel grid over a view
 * fed by the scene's height fog and local fog volumes, lit by the scene's
 * directional lights through their cascaded shadows, integrated, kept with
 * a history, and put over the picture against depth. Stage 3, point and
 * spot lights in the grid, and transparents reading it (RBTREND_TODO.md item
 * 56; card A59 in DECISIONS.md, "Decided", 2026-11-17). Rabbittus Math's
 * half, the densities, is rbtmath_fog_draft.h; the caller's own half is
 * drafted beside the caller's headers.
 *
 * APPROVED 2026-11-18 with the user, every question as recommended.
 * DRAFT, 2026-11-18, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built yet, not in any target, not
 * compilable. Written on the MacBook (M5 Pro) against what is built
 * (rabbittus, the working tree of 2026-11-18, 0.3): render/rbtrendlit.h
 * (the scene's settings calls; RBTREND_LIT_AMBIENT, line 1342;
 * RBTREND_LIT_ATMOSPHERE, line 1399; set 0 and its sets, lines 33 to 48 of
 * the file comment), render/rbtrendcommands.h (commands to 51; the 120-byte
 * parameters), render/rbtrendimages.h (rbtrend_texture3d, line 534, usage
 * SHADER_RESOURCE | STORAGE; rbtrend_texture_view_create_3d, line 1015),
 * shaders/rbtrend_lit_bindings.slang (set 0's sixteen bindings),
 * shaders/rbtrend_lit_scene.slang (the scene block, flags 1 to 2048, line
 * 50 on), rbtcore.h (struct types to 5040); and the drafts beside it:
 * rbtrend_clouds_draft.h (card A57, version 1, this session: the history's
 * internals this shares, LIT_CLOUDS's composition this extends) and
 * rbtrend_atmosphere_space_draft.h (card A55, approved, built 2026-11-17).
 *
 * The decided lines: "Volumetric fog near the eye, every question as
 * recommended (Q1a to Q4a): a froxel grid of height fog and local volumes
 * lit by every light and its shadows, staged from analytic height fog;
 * density in Math, the grid in Rend; internals shared with A57's clouds,
 * each its own object." RBTREND_TODO.md item 56 adds: "applied to every
 * surface and to transparents, with a history (Q1a); staged: analytic
 * height fog first (its density in Rabbittus Math), then the grid with
 * directional lights and local volumes, then point and spot lights and
 * transparents (Q2a) ... To be drafted; needs the 3D-texture GPU test,
 * A39's history, the transparency stage for transparents."
 *
 * Why two drafts, not one with the clouds' (asked of this session): the
 * decided line makes each its own public object, approved alone; the
 * fog's stage 1 needs nothing of the clouds and can be built first; what
 * they share (the history's records and reprojection, the WORLEY volume's
 * bake and read) is internal to Rend or in Math's public noise header, said
 * in both drafts (section 5 here, section 4 there).
 *
 * ===========================================================================
 * Revision notes (seven passes, 2026-11-18, each fixed before the next)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: version 0 applied stage 1's fog in a pass after the surfaces
 *    (Unreal's height fog pass), which leaves transparent surfaces unfogged
 *    until stage 3, and the sky's sun discs undimmed. Rend's lit shaders
 *    already apply the aerial volume per fragment (card L62's stage 3):
 *    the analytic fog the same way reaches opaque and transparent surfaces
 *    and the sky at once, from the scene's block, with no binding (set 0
 *    is full). Changed (section 3, Q1).
 * 2. Missing: how stage 1 and stage 2 meet. The grid holds the height fog
 *    near the eye and the analytic fog the rest; without a rule both add
 *    the near part. Now the scene's fog starts at its `start_distance`,
 *    which a caller drawing the grid sets to the grid's far distance (the
 *    command refuses a scene whose fog starts nearer: section 4).
 * 3. Missing: the clouds. LIT_CLOUDS puts the scene's air in front of the
 *    clouds' depth; the scene's fog belongs there too. Added as a
 *    [CHANGED] of LIT_CLOUDS's composition (section 6), built with
 *    whichever is built second.
 * 4. More than the need: version 0's fog colour as a free colour (Unreal's
 *    "fog inscattering colour"). The decided "lit by every light": the
 *    fog's light is its albedo times the lights (and ambient) that reach
 *    it, so a sunset reddens it and a night darkens it with no setting.
 *    Removed; Q5 asks.
 * 5. More than the need: a "fog density multiplier per light" (Unreal's
 *    volumetric scattering intensity per light). Not decided. Removed.
 *
 * Pass 2 (industry standard, compared; section 9).
 * 1. Unreal (Wronski, "Volumetric Fog", SIGGRAPH 2014's Assassin's Creed
 *    IV talk, and Unreal's volumetric fog after it, as remembered): a
 *    froxel grid of tiles of the screen (8 or 16 texels) and slices spaced
 *    by distance (exponentially), injected with media and lights, shadowed
 *    by the cascades, temporally reprojected with a jitter in depth,
 *    integrated front to back, the height fog analytic beyond the grid's
 *    distance. Frostbite (Hillaire 2015, "Towards Unified and Physically-
 *    Based Volumetric Lighting in Frostbite"): the same, energy-conserving
 *    integration, local volumes as boxes, material properties scattering
 *    and extinction. HDRP's volumetric fog and local volumetric fog: the
 *    same shape. This draft follows them; the grid's sizes, distances and
 *    history weight are the caller's (Unreal's are console variables).
 * 2. The analytic fog in the surface shader: Unreal applies height fog in
 *    a pass to opaque and per vertex or pixel to translucent; Godot's fog
 *    in its scene shader. Here per fragment (pass 1.1), as Rend's aerial
 *    perspective already is.
 *
 * Pass 3 (the C API to standard).
 * 1. RBTREND_LIT_FOG and RBTREND_FOG_DESC based (a setting and a desc);
 *    RBTREND_FOG_VOLUME_INSTANCE frozen, an array element sent a frame.
 * 2. rbtrend_lit_scene_set_fog() takes NULL for none, as
 *    rbtrend_lit_scene_set_ambient_occlusion() takes a NULL view: one call
 *    sets or clears, no separate flag.
 * 3. Limits named: RBTREND_FOG_MAX_VOLUMES, _MAX_VIEWS, _MAX_GRID_SIDE,
 *    _MAX_GRID_SLICES, RBTREND_LIT_FOG_MAX_SUNS.
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. Stage 1 allocates nothing: the fog is members of the scene's block,
 *    staged at the submit as every block member is.
 * 2. Stage 2's records made at creation from the desc (the grid's sizes
 *    are froxels, not texels, so no target size is needed), as the
 *    clouds'; a steady frame allocates nothing.
 * 3. The volumes copied at the send, resolved at the compile into the
 *    record's per-frame block; each froxel tests only the volumes whose
 *    box meets its tile's frustum (a cull pass a frame, internal, as the
 *    lights' tiles are), not every volume.
 * 4. Version 0 kept four grids a view: this frame's media, two histories
 *    and the integrated grid. The injection writes the blended value
 *    straight into this frame's history (the one not read), so the media's
 *    own grid held nothing the history doesn't: three grids, 21.1 MiB a
 *    view at 160 x 90 x 64, not 28.1.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. A scene's fog is a scene setting: one caller at a time with the
 *    scene. The fog object one object for the rule.
 * 2. One LIT_FOG a scene a frame (its history), the second refused.
 * 3. Errors: each setting checked in member order, Math's fog by Math's
 *    checks; the command's refusals at the compile, each its message.
 *
 * Pass 6 (correctness against the platforms and the code as built).
 * 1. The scene block (rbtrend_lit_scene.slang) is a uniform block read by
 *    every lit shader; its flags reach 2048 (RBTREND_LIT_SCENE_SKY, line
 *    59). The fog adds a flag (4096) and its members: Rend's layout, no
 *    binding, no caller's layout changed (rbtrend_lit_get_scene_layout()
 *    returns the same layout).
 * 2. The grid's froxel textures are written by compute (3D storage
 *    images): nothing in Rend writes a 3D texture from compute yet, and
 *    the atmosphere's stages 2 and 3 used fragment passes to avoid it
 *    (card A57's "What Rend and Carrottus have"). A GPU test of a 3D
 *    texture written by compute and sampled, on three backends, is stage
 *    2's first step (test 6), as item 56 asks.
 * 3. The cascades: the grid's compute binds the scene's set 0 for its
 *    block and shadow atlas (bindings 0, 2, 3, 4), as the shell pass binds
 *    set 0; a directional light whose shadow is a traced mask
 *    (rbtrendlit.h, RBTREND_LIT_SHADOW_KIND) has no shadow in the grid (a
 *    mask is a screen's, not a volume's): said on the command.
 * 4. The sky shader (rbtrend_lit_sky_frag.slang) draws at depth's far
 *    value: its ray reaches the fog's end distance; the suns' discs over it
 *    dimmed by the same transmittance.
 * 5. Blending into a float target: the same gap as the shells' and the
 *    clouds' (R16G16B16A16 alone guaranteed by Vulkan; card S51).
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Tests named (section 10), stage by stage, Math's transmittance the
 *    CPU's answer for stage 1.
 * 2. Stage 1 is buildable now (after Math's half, after A61); stage 2
 *    after the 3D-texture compute test and the clouds' history module
 *    (whichever is built first writes rbtrendhistory.hpp); stage 3 after a
 *    binding plan for set 0 (with A57's stage 3) and the caller's
 *    transparency stage.
 * 3. No version raise while 0.3 is untagged (card X2). Struct types the
 *    next free (5043 and 5044 as written, after the clouds' 5041 and
 *    5042), command 53 the next free after LIT_CLOUDS.
 *
 * ===========================================================================
 * 1. What is drawn
 * ===========================================================================
 *
 * Fog near the eye: a valley's morning mist, haze over a plain, dust in a
 * cave's shaft of light, a world with no atmosphere still hazy (card L74's
 * correction), steam over a vent. Stage 1 gives a height fog everywhere the
 * lit shaders draw, cheap, with the suns' light in it but no shadows.
 * Stage 2 gives shafts of light and shadow near the eye, local volumes, and
 * a fog that changes across the view.
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * Stage 1:
 * [ADDED] RBTREND_LIT_FOG (based): Math's RBTMATH_HEIGHT_FOG, where it is,
 *   and how it takes light; rbtrend_lit_scene_set_fog() (NULL for none);
 *   RBTREND_LIT_FOG_MAX_SUNS.
 * [CHANGED] the lit surface and sky shaders apply it (Rend's own shaders;
 *   no declaration changes); RBTREND_COMMAND_LIT_CLOUDS's composition
 *   applies it in front of the clouds (once both are built).
 * Stage 2:
 * [ADDED] rbtrend_fog (an object: a WORLEY noise volume, and a record a view:
 *   its grid, its history, its integrated grid), RBTREND_FOG_DESC,
 *   rbtrend_fog_create(), _destroy(), _write_noise();
 *   RBTREND_FOG_VOLUME_INSTANCE (frozen); RBTREND_COMMAND_LIT_FOG (53);
 *   four limits.
 * [CHANGED] RBTREND_LIT_SCENE_DESC::max_view_sets' comment gains LIT_FOG.
 * Stage 3 (section 8): additions only, drafted with set 0's binding plan.
 *
 * ===========================================================================
 * 3. Stage 1: the analytic height fog
 * ===========================================================================
 *
 * For a lit fragment at distance D from the eye (or the sky's ray, D
 * infinite), along its view ray, between distances s = `start_distance`
 * and e = min(D, `end_distance`) (none where e <= s):
 * - T = Math's height fog transmittance of the segment (rbtmath_fog_draft.h
 *   3c, the GPU's copy to a tolerance), floored at 1 - `max_opacity`;
 * - the fog's light, taken as even along the segment (Unreal's model): the
 *   fog's `albedo` x (for each of the scene's first `sun_count` directional
 *   lights: pi x its radiance x phase(cos between the view ray and it), its
 *   phase Henyey-Greenstein's of `phase_g`; through the scene atmosphere's
 *   transmittance at the eye where the scene has one, as the sky's sun
 *   disc is; plus the scene's ambient light's irradiance / pi at the
 *   segment's middle height's up, x `ambient_scale`);
 * - the fragment's colour c (after the aerial volume, where the scene has
 *   one) becomes c x T + light x (1 - T), before the exposure. The fog
 *   lies in front of the scene's air (an approximation where both are thick
 *   at once: a fog is low and near, the air far).
 * For the sky: the same over its ray from s to e, and each sun's disc
 * dimmed by T. A transparent surface takes it as an opaque one does, its
 * light premultiplied by its opacity as the rest of its colour.
 *
 * The fog's place: `origin` in the scene's space this frame (a plane's
 * point, a sphere's centre); the fog's height and up are Math's. Sun
 * directions are the scene's lights'; placed directional lights, once
 * item 58 is built, at the eye.
 *
 * Cost: per fragment two exponentials and a few dozen operations (a plane;
 * a sphere's approximation about twice), against the lit fragment's own
 * several hundred (its lights, shadows, environment and aerial reads):
 * estimated a few per cent of the lit pass, under 0.05 ms at 1440p with an
 * overdraw of 1.5 on the M5 Pro (method: operations counted against the
 * lit fragment's; not measured: test 13 measures it).
 *
 * ===========================================================================
 * 4. Stage 2: the grid
 * ===========================================================================
 *
 * A view record holds, at the desc's grid (W x H froxels across and down
 * the view, S slices), three 3D textures of R16G16B16A16_FLOAT: the
 * history, two in turn (each the media and light of a frame, blended:
 * scattered light RGB, extinction A; one read as last frame's, the other
 * written as this frame's), and the integrated grid (the light
 * gathered from the eye to each slice's far side, RGB, and the
 * transmittance, A). Slices from `_near_distance` to `_far_distance`,
 * spaced exponentially (slice i's far side at near x (far / near)^((i + 1) /
 * S)), the screen in W x H tiles.
 *
 * LIT_FOG, each frame for its scene:
 * 1. CULL: the volumes against each tile's frustum (a list a tile, as the
 *    lights' tiles).
 * 2. INJECT AND LIGHT (compute, a thread a froxel): at a point of the
 *    froxel (its centre, moved in depth by the frame's jitter, a fixed
 *    sequence), the extinction: the scene's height fog's density (Math's
 *    3b) plus each of its tile's volumes' (Math's 3d, the volume's frame
 *    from its instance); the scattering: each medium's density x its
 *    albedo, summed; its light: for each of the scene's first sun_count
 *    directional lights, pi x radiance x phase x its shadow (the cascade
 *    that holds the point, one comparison, the atlas's; none for a light
 *    whose shadow is a traced mask) x the scene atmosphere's
 *    transmittance where it has one; plus ambient as stage 1's. The
 *    froxel's value blended with the history: last frame's grid read at
 *    the point moved by `_space_shift` through last frame's
 *    view-projection (which the record kept), weight `_history_weight`
 *    for the history, where it lands inside last frame's grid; else this
 *    frame's alone. At `_history_reset` or the record's first frame, this
 *    frame's alone.
 * 3. INTEGRATE (compute, a thread a tile's column): front to back,
 *    Hillaire's energy-conserving step over each slice's length: light +=
 *    T x S x (1 - exp(-sigma_t l)) / sigma_t; T x= exp(-sigma_t l).
 * 4. APPLY (a full-screen pass over `_target`): each texel's depth from
 *    `_depth` (the far value: the far side of the grid), its tile and slice
 *    by its distance, the integrated grid read there (trilinear), and
 *    target x T + L. Blend ONE, SRC_ALPHA; alpha kept.
 * The scene's own fog (stage 1) beyond the grid: the scene's fog
 * `start_distance` is the grid's `_far_distance` (refused otherwise: both
 * would hold the near part), so the lit shaders fog beyond the grid and
 * the grid the near part; their light meets at the far side within the
 * history's lag.
 *
 * Transparent surfaces drawn after LIT_FOG keep stage 1's fog beyond the
 * grid and nothing of the grid's until stage 3.
 *
 * Why the grid's sizes are froxels (no target size): a froxel is a share
 * of the view, not of its texels; a target of any size reads the same
 * grid. W x H of the view's aspect is the caller's (Unreal's 16-texel
 * tiles: 160 x 90 at 2560 x 1440).
 *
 * ===========================================================================
 * 5. What is shared with the clouds (the decided Q4a), and what isn't
 * ===========================================================================
 *
 * Internal to Rend: the history's records (found by the scene's handle,
 * made at creation, the least recently used taken, RBT_RESULT_LIMIT when
 * none is free), the reprojection (a place moved by the space's shift
 * through last frame's view-projection, kept in the record), and the
 * jitter sequences: one module, src/render/rbtrendhistory.hpp, written by
 * whichever of the two is built first. Public in Math: the WORLEY volume's
 * bake and read (rbtmathnoise.h, the clouds' Math half). Not shared: the
 * objects (each its own, as decided), the passes (a march in screen space
 * against a grid in the frustum), the noise's copy on the GPU (the fog's
 * object holds its own WORLEY volume, 128 KiB at 32^3, so a scene with fog
 * and no clouds needs no clouds object: Q6).
 *
 * ===========================================================================
 * 6. LIT_CLOUDS gains the scene's fog [CHANGED, a comment and its shader]
 * ===========================================================================
 *
 * RBTREND_COMMAND_LIT_CLOUDS's composition (rbtrend_clouds_draft.h, section
 * 5) puts the scene's air in front of the clouds' depth d; with a scene fog,
 * also the fog between s and min(d, e) the same way (c x T + light x (1 -
 * T)), after the air. Its comment gains "and the scene's fog
 * (rbtrend_lit_scene_set_fog())". No parameter changes. Built with
 * whichever of the two is built second.
 *
 * ===========================================================================
 * 7. Cost and memory (estimates; measured before a caller turns it on)
 * ===========================================================================
 *
 * Stage 2 at a 160 x 90 x 64 grid (921,600 froxels; Unreal's 16-texel
 * tiles at 1440p): inject and light about 20 to 40 operations, two to four
 * texture reads (history, shadow, noise) a froxel, and a loop over its
 * tile's volumes; integrate 14,400 columns of 64; apply 3.7 million texels,
 * one read each. Method: A55's measured shell march (0.66 ns a texel of up
 * to 32 steps of two reads, item 52) taken as 10 to 20 ps a read: inject
 * 0.1 to 0.3 ms, integrate under 0.05, apply 0.1 to 0.2: 0.25 to 0.55 ms
 * at 1440p on the M5 Pro, Release. Unreal's measured costs on consoles
 * are of the same order, as remembered.
 *
 * Memory (rbtrend_fog_create, nothing later): the WORLEY volume (side^3 x
 * 4 bytes; 128 KiB at 32); a record a view: three 3D textures of 8 bytes a
 * froxel (two histories, the integrated grid): 3 x 921,600 x 8 =
 * 22,118,400 bytes (21.1 MiB) at 160 x 90 x 64; a tile's volume list
 * (W x H x (1 + RBTREND_FOG_MAX_TILE_VOLUMES) x 4 bytes, 0.9 MiB); the
 * per-frame block (RBTREND_FOG_MAX_VOLUMES x 96 bytes a frame in flight).
 * Stage 1: 64 bytes of the scene's block.
 *
 * ===========================================================================
 * 8. Stage 3 (additions only, its own amendment)
 * ===========================================================================
 *
 * - Point and spot lights in the grid: each froxel lit by the scene's point
 *   and spot lights whose range meets it (a light list a froxel tile and
 *   slice range, as the surfaces' tiles), shadowed by their atlas views;
 *   the command takes no new parameter (the scene's lights).
 * - Transparents reading the grid: the lit surface shader reads the
 *   integrated grid at its fragment, which needs a binding in set 0, full.
 *   One binding plan for set 0 serves this and A57's cloud shadows (stage
 *   3 of each), drafted together; the caller's transparency stage before
 *   it is used.
 *
 * ===========================================================================
 * 9. Compared and alternatives (pass 2)
 * ===========================================================================
 *
 * - Unreal: Exponential Height Fog (analytic, two layers, start, cutoff,
 *   max opacity, directional in-scattering) and Volumetric Fog (froxels,
 *   16-texel tiles, 64 or 128 slices, a view distance, history weight 0.9
 *   by default, lights' volumetric scattering intensity) and Local Fog
 *   Volumes (as remembered from its documentation).
 * - Frostbite (Hillaire 2015, as remembered): froxels, participating media
 *   as boxes with material properties, energy-conserving integration,
 *   temporal reprojection.
 * - HDRP (as remembered): volumetric fog with froxels and local
 *   volumetric fog with a 3D mask texture.
 * - Godot 4 (as remembered): a froxel volumetric fog and FogVolume nodes
 *   (boxes, ellipsoids, cones, cylinders), shaders for density.
 * - Hazel (../.refs/HazelRenderer2025, present): a distance fog in
 *   Hazelnut's SceneComposite.glsl (smoothstep of depth, start 5.5 and
 *   falloff 30 written in the shader), disabled (HZ_RENDERER_FOG_EFFECT 0,
 *   line 25); no height fog, no volumetric fog.
 * Not taken: stage 1 as a pass (pass 1.1); the fog as part of
 * RBTREND_LIT_ATMOSPHERE (the atmosphere is a shell's medium; the fog a
 * plane's or a near haze, decided apart); one object for fog and clouds
 * (decided apart).
 *
 * ===========================================================================
 * 10. Tests (C through the public API; rabbittustests on Metal under its
 *     validation, warnings fatal, Debug and Release; each seen failing
 *     first)
 * ===========================================================================
 *
 * Stage 1:
 * 1. test_fog_refusals: rbtrend_lit_scene_set_fog's checks, each its
 *    message; NULL clears.
 * 2. test_fog_against_math: a black face at 10, 100 and 1,000 units under
 *    a plane fog, no light but ambient: its colour against
 *    (1 - T) x albedo x ambient with T from rbtmath_height_fog_compute_
 *    transmittance, within 1%; a sphere's the same within Math's recorded
 *    bound plus 1%.
 * 3. test_fog_distances: start, end and max opacity: none nearer than
 *    start, none past end, T never below 1 - max opacity.
 * 4. test_fog_sky_and_transparent: the sky's texels and a sun disc dimmed
 *    by T; a half-transparent face fogged as an opaque one, x its opacity.
 * 5. test_fog_none_unchanged: every lit and sky test with no fog set: the
 *    same pixels.
 * Stage 2:
 * 6. test_texture3d_compute (before the rest): a 3D texture of
 *    R16G16B16A16_FLOAT written by a compute shader and read back, then
 *    sampled by a fragment shader: each texel as written; on Metal here,
 *    D3D12 and Vulkan in their sessions (card X25).
 * 7. test_fog_grid_uniform: a uniform fog (a height fog of huge scale
 *    height) and no light but ambient: the applied T at each texel against
 *    exp(-density x distance) within 2%, at every slice's far side.
 * 8. test_fog_grid_shadow: a sun and a cascaded shadow of a wall: the
 *    froxels behind the wall darker than beside it (a shaft), the edge
 *    within a tile.
 * 9. test_fog_volume: a box volume in no height fog: T through its middle
 *    against Math's density summed along the ray, within 3%; its edge's
 *    fade; nothing outside.
 * 10. test_fog_meets_analytic: a plane fog with the grid to 100 units and
 *    the scene's start at 100: a face at 300 units fogged within 2% of the
 *    same scene with no grid and start 0.
 * 11. test_fog_history: as the clouds' test 11 (a still view converges, a
 *    turned and shifted view within 3% after 8 frames, reset).
 * 12. test_fog_records and test_frame_allocations (eight volumes, the
 *    grid each frame: none of Rend's); refusals of the command; the
 *    misuse harness and the destroyed-handle tests regenerated.
 * 13. test_fog_measure (RABBITTUSTESTS_MEASURE): stage 1 at 1080p and 1440p
 *    against none; the grid at 160 x 90 x 64 and 160 x 90 x 128, 0, 8 and
 *    32 volumes.
 * The build as the clouds'. D3D12 and Vulkan in their sessions (X25).
 *
 * ===========================================================================
 * 11. ABI, versions, records when built
 * ===========================================================================
 *
 * Additions only: stage 1 a based struct, a call and a macro in
 * rbtrendlit.h; stage 2 a new header render/rbtrendfog.h (a handle, a based
 * desc, a frozen element, four calls, four macros), command 53 and its
 * parameters (72 bytes of the 120, section 12), two struct types. Comments
 * changed: max_view_sets, LIT_CLOUDS. No version raise while 0.3 is
 * untagged (card X2). Records: RBTREND_TODO.md item 56, stage by stage,
 * the measured costs against section 7; rabbittus/CLAUDE.md's Rend row.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card A59's Rend half; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. Stage 1's fog: a, applied by the lit shaders per fragment from the
 *     scene's block (recommended: opaque, transparent and sky at once, no
 *     binding, as the aerial volume); b, a pass after the surfaces
 *     (Unreal's for opaque; transparents unfogged until stage 3).
 * Q2. How stage 1 and the grid meet: a, the scene fog's start is the
 *     grid's far distance, refused otherwise (recommended: one rule the
 *     caller sees); b, Rend moves the analytic fog's start itself when a
 *     grid is drawn (a hidden coupling between a scene setting and a
 *     command).
 * Q3. Stage 2 over the picture: a, a pass against depth after the
 *     surfaces and the clouds, transparents in stage 3 (recommended: set 0
 *     is full; one binding plan for stage 3 with the clouds' shadows); b, a
 *     binding plan now.
 * Q4. The grid's history: a, a record a view inside the object, made at
 *     creation, as the clouds' (recommended: the shared internals); b,
 *     card A39's handle.
 * Q5. The fog's light: a, its albedo times the lights and ambient that
 *     reach it (recommended: "lit by every light"; a sunset reddens it);
 *     b, a colour of the caller's added to it (Unreal's in-scattering
 *     colour), a look the light doesn't give.
 * Q6. The noise's copy on the GPU: a, the fog object's own WORLEY volume
 *     (recommended: 128 KiB, a scene with fog and no clouds needs no
 *     clouds); b, the clouds' detail volume, named by the command.
 * Q7. Shadows in the grid: a, the cascades and map shadows of the atlas;
 *     a light shadowed by a traced mask unshadowed in the fog
 *     (recommended: a mask is the screen's); b, traced shadows in the grid
 *     (a ray a froxel: a later card if wanted).
 */

/* ========================================================================= */
/* render/rbtrendlit.h                                  [ADDED: stage 1]     */
/* ========================================================================= */

/** The most suns (the scene's first directional lights) a scene's fog takes light from. */
#define RBTREND_LIT_FOG_MAX_SUNS 2u
/* [ADDED] Why: as a scene atmosphere's sun_count (1 or 2), the check named. */

/**
 * @brief A scene's height fog: Math's fog, where it is, and how it takes
 * light. No field has a default.
 */
typedef struct RBTREND_LIT_FOG {
	/** RBT_STRUCT_TYPE_REND_LIT_FOG, and its chain, which takes nothing. */
	RBT_STRUCT_BASE base;
	/** The fog: each field in its range (math/rbtmathfog.h). */
	RBTMATH_HEIGHT_FOG fog;
	/** Its origin in the scene's space this frame (a plane's point at
	 * height 0, a sphere's centre): finite. Set as often as it moves. */
	float origin[3];
	/** The share of its extinction that scatters, three channels: each
	 * finite, 0 to 1. */
	float albedo[3];
	/** Its phase's g for the suns' light (Henyey-Greenstein): finite,
	 * -0.999 to 0.999; above 0 brighter towards a sun. */
	float phase_g;
	/** How many of the scene's directional lights, the first, light it: 0 to
	 * RBTREND_LIT_FOG_MAX_SUNS. */
	uint32_t sun_count;
	/** The scene's ambient light's share: finite, at least 0. */
	float ambient_scale;
	/** How far from the eye it starts: finite, at least 0. A caller
	 * drawing the grid (RBTREND_COMMAND_LIT_FOG) sets its far distance here. */
	float start_distance;
	/** How far from the eye it ends: finite, above #start_distance; the
	 * sky's ray stops here. */
	float end_distance;
	/** The most it hides: finite, 0 to 1 (its transmittance never below
	 * 1 minus this). */
	float max_opacity;
} RBTREND_LIT_FOG;
/* [ADDED] Why: stage 1 decided ("analytic height fog first"), its light
 * the scene's (Q5), its distances Unreal's. RBT_STRUCT_TYPE_REND_LIT_FOG
 * (5043, or the next free). */

/**
 * @brief Sets a scene's height fog, or takes it away (NULL). Copied; from
 * the next submission, every lit surface, transparent ones included, and
 * the sky (RBTREND_LIT_SHADER_SKY_FRAGMENT, its suns' discs too) are seen
 * through it, after the scene atmosphere's air where it has one. Nothing
 * allocated: set each frame as it moves.
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
/* [ADDED] */

/* ========================================================================= */
/* render/rbtrendfog.h                         [ADDED: new file, stage 2]    */
/* ========================================================================= */

/*
 * @file rbtrendfog.h
 * @brief Rend's volumetric fog near the eye: a froxel grid over each view
 * it draws, fed by the scene's height fog and local fog volumes, lit by the
 * scene's directional lights through their shadows, kept with a history,
 * integrated, and put over the picture against depth. (Opening comment:
 * what an rbtrend_fog holds; the frame's order; stage 1's fog beyond the
 * grid; "Out of scope": point and spot lights and transparents (stage 3),
 * traced shadows; "@par Threads".)
 */

/** The most volumes one RBTREND_COMMAND_LIT_FOG takes. */
#define RBTREND_FOG_MAX_VOLUMES 64u
/** The most volumes a tile's list holds; more meeting a tile are left out
 * of it, the compile's Debug report saying which tile. */
#define RBTREND_FOG_MAX_TILE_VOLUMES 15u
/** The most views a fog keeps a grid for. */
#define RBTREND_FOG_MAX_VIEWS 16u
/** The most froxels across or down a grid. */
#define RBTREND_FOG_MAX_GRID_SIDE 512u
/** The most slices a grid has. */
#define RBTREND_FOG_MAX_GRID_SLICES 256u
/* [ADDED] Why each: a check's bound, the block's and the tile list's sizes
 * named (pass 3.3). */

/** @brief Opaque handle to a fog: a noise volume and a grid a view. */
typedef struct rbtrend_fog_t *rbtrend_fog;
/* [ADDED] Why: the decided "the grid Rend's ... each its own object". */

/**
 * @brief How a fog is made: its grid's sizes and its views. No default.
 */
typedef struct RBTREND_FOG_DESC {
	/** RBT_STRUCT_TYPE_REND_FOG_DESC, and its chain, which takes nothing. */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** Froxels across a view, 1 to RBTREND_FOG_MAX_GRID_SIDE. */
	uint32_t grid_width;
	/** Froxels down a view, 1 to RBTREND_FOG_MAX_GRID_SIDE. */
	uint32_t grid_height;
	/** Slices in depth, 1 to RBTREND_FOG_MAX_GRID_SLICES. */
	uint32_t grid_slices;
	/** How many views it keeps a grid for, 1 to RBTREND_FOG_MAX_VIEWS. */
	uint32_t max_views;
	/** Its WORLEY noise volume's side (rbtmathnoise.h), 1 to
	 * RBTMATH_NOISE_VOLUME_MAX_SIZE. */
	uint32_t noise_size;
} RBTREND_FOG_DESC;
/* [ADDED] Why: every allocation at creation (pass 4.2).
 * RBT_STRUCT_TYPE_REND_FOG_DESC (5044, or the next free). */

/**
 * @brief Creates a fog: its noise volume (undefined until written) and a
 * record a view (its three grids and sets). Memory: noise_size^3 x 4
 * bytes, and a view 3 x W x H x S x 8 bytes, its tiles' lists W x H x 64
 * bytes, and RBTREND_FOG_MAX_VOLUMES x 96 bytes a frame in flight (21.1 MiB
 * a view at 160 x 90 x 64). Nothing is made later.
 *
 * @param[in]  instance  Instance it is of.
 * @param[in]  desc      Its sizes. Only read during the call.
 * @param[out] out_fog   Receives it; NULL on failure.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return As rbtrend_clouds_create().
 */
RBT_RESULT rbtrend_fog_create(rbtrend_instance instance, const RBTREND_FOG_DESC *desc, rbtrend_fog *out_fog, RBTREND_ERROR *out_error);
/* [ADDED] */

/**
 * @brief Destroys a fog once the GPU is past it. NULL does nothing.
 *
 * @param[in] fog The fog.
 */
void rbtrend_fog_destroy(rbtrend_fog fog);
/* [ADDED] */

/**
 * @brief Writes slices of the fog's noise volume: bytes
 * rbtmath_noise_volume_bake() made, of kind
 * RBTMATH_NOISE_VOLUME_KIND_WORLEY and the desc's side. Recorded and
 * staged as rbtrend_texture3d_update().
 *
 * @param[in] fog         The fog.
 * @param[in] queue       As rbtrend_texture3d_update()'s.
 * @param[in] first_slice Below the side.
 * @param[in] slice_count 1 to the side - @p first_slice.
 * @param[in] data        side x side x slice_count x 4 bytes.
 * @param[in] data_size   Exactly that.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return As rbtrend_clouds_write_noise().
 */
RBT_RESULT rbtrend_fog_write_noise(rbtrend_fog fog, crgpu_command_queue queue, uint32_t first_slice, uint32_t slice_count,
                                   const void *data, uint64_t data_size, RBTREND_ERROR *out_error);
/* [ADDED] Why: Q6a; one kind, so no kind parameter. */

/**
 * @brief One local fog volume of a LIT_FOG command: Math's volume, where
 * its frame is, its noise's place, and its albedo.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_FOG_VOLUME_INSTANCE {
	/** The volume (math/rbtmathfog.h): each field in its range. */
	RBTMATH_FOG_VOLUME _volume;
	/** The scene's space into the volume's frame, a 3 x 4 matrix, rows
	 * (x' = row 0 . (x, y, z, 1), ...): a rotation and a move, finite (a
	 * scale is the volume's half sizes'). */
	float _to_volume[12];
	/** The noise's coordinate at the volume's origin this frame, in tiles:
	 * each finite, within RBTMATH_NOISE_VOLUME_MAX_COORDINATE. */
	float _noise_offset[3];
	/** The share of its extinction that scatters: each finite, 0 to 1. */
	float _albedo[3];
} RBTREND_FOG_VOLUME_INSTANCE;
/* [ADDED] Why: the decided local volumes, placed a frame (they move with
 * their owners), each its own albedo (dust beside mist). */

/*
 * render/rbtrendcommands.h [ADDED, stage 2]:
 *
 *	/ **
 *	 * Draws `_fog`'s grid for `_scene` over `_target`, outside a pass:
 *	 * the scene's height fog (rbtrend_lit_scene_set_fog(), its
 *	 * `start_distance` equal to `_far_distance`) and `_volumes` injected
 *	 * into a froxel grid from `_near_distance` to `_far_distance`, lit by
 *	 * the scene's fog's suns through their cascaded and map shadows (a light
 *	 * shadowed by a traced mask unshadowed here) and the scene atmosphere's
 *	 * transmittance, blended with this scene's history (`_history_weight`),
 *	 * integrated from the eye, and put over `_target` at each texel's
 *	 * depth: target x T + L.
 *	 *
 *	 * `_scene` as LIT_CLOUDS's; `_depth` and `_target` as LIT_CLOUDS's.
 *	 * `_volumes` 0 to RBTREND_FOG_MAX_VOLUMES, copied at the send.
 *	 * `_near_distance` finite, above 0; `_far_distance` above it.
 *	 * `_history_weight` finite, 0 (none) to below 1 (Unreal's 0.9).
 *	 * `_space_shift` and `_history_reset` as LIT_CLOUDS's.
 *	 *
 *	 * Refused at the compile, each its message: the scene has no fog, or
 *	 * its start isn't `_far_distance`; no record free (RBT_RESULT_LIMIT,
 *	 * naming RBTREND_FOG_DESC::max_views); the same scene twice in a
 *	 * frame; no set free in the scene's records.
 *	 *
 *	 * The frame's order: after LIT_CLOUDS (or the surfaces, LIT_REFLECT and
 *	 * the shells where there are no clouds), before the transparent
 *	 * surfaces and the tone map. Transparent surfaces see the scene's fog
 *	 * beyond the grid, not the grid (RBTREND_TODO.md item 56's stage 3).
 *	 *
 *	 * It takes one set from `_scene`'s view-set records, over `_depth`
 *	 * (and the scene atmosphere's tables). It binds pipelines and sets of
 *	 * its own, as BLOOM does.
 *	 * /
 *	RBTREND_COMMAND_LIT_FOG                     = 53,
 *
 *	struct {
 *		rbtrend_lit_scene _scene;                    / ** 8 * /
 *		rbtrend_fog _fog;                            / ** 8 * /
 *		const RBTREND_FOG_VOLUME_INSTANCE *_volumes; / ** 8 * /
 *		uint32_t _volume_count;                      / ** 4 * /
 *		float _near_distance;                        / ** 4 * /
 *		float _far_distance;                         / ** 4 * /
 *		float _history_weight;                       / ** 4 * /
 *		float _space_shift[3];                       / ** 12 * /
 *		uint32_t _history_reset;                     / ** 4: 0 or 1 * /
 *		rbtrend_texture_view _depth;                 / ** 8 * /
 *		rbtrend_texture_view _target;                / ** 8 * /
 *	} _lit_fog; / **< RBTREND_COMMAND_LIT_FOG. * /   (each member its
 *	                                                Doxygen line in the header)
 *
 * [ADDED] Why: the grid, its history and its application are one pass of
 * Rend's own, every cost the caller's (the grid's sizes in the desc, its
 * distances and history weight here). 72 bytes of the 120 (section 12).
 *
 * rbtrendlit.h, RBTREND_LIT_SCENE_DESC::max_view_sets [CHANGED, a
 * comment]: LIT_FOG among the commands.
 * rbtcore.h [ADDED]: RBT_STRUCT_TYPE_REND_LIT_FOG and
 * RBT_STRUCT_TYPE_REND_FOG_DESC, the next free.
 *
 * ===========================================================================
 * 12. The parameters' bytes (pass 6)
 * ===========================================================================
 *
 * 8 + 8 + 8 + 4 + 4 + 4 + 4 + 12 + 4 + 8 + 8 = 72 bytes (version 0
 * said 88, the views counted as 16 each). Within the 120.
 */
