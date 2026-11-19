/*
 * rbtrend_clouds_draft.h - card A57's Rend half: volumetric clouds, an
 * object of Rend's own (its noise volumes, coverage maps, looks and the
 * history of each view it draws), its layers bounded by a spherical shell
 * or a flat slab, lit through an atmosphere when the caller gives one and
 * by the scene's lights otherwise, several layers marched a frame at a
 * quarter of the target's resolution and filled out by reprojection
 * (RBTREND_TODO.md item 55; card A57 in DECISIONS.md, "Decided",
 * 2026-11-17). Rabbittus Math's half, the density this marches, is
 * rbtmath_clouds_draft.h; the caller's own half is drafted beside the
 * caller's headers.
 *
 * VERSION 2 APPROVED 2026-11-18 with the user (card A70), every question as
 * recommended (Q8 to Q12 a); not built yet.
 * DRAFT, 2026-11-18, VERSION 2, for the user's approval (card A70): the
 * history rule revised (sections 3 step 2, 4, 7, 10, 11 and 12, and
 * Questions Q8 to Q12; "Version 2" below), written, then seven critique
 * passes on the revised parts, each fixed before the next. Nothing of
 * version 2 is built. Version 1 approved 2026-11-18 with the user, every
 * question as recommended, and its stage 1 built the same day on the Mac
 * (src/render/rbtrendclouds.cpp, rbtrendclouds_internal.hpp, the
 * LIT_CLOUDS compile in rbtrendcommands.cpp, shaders/rbtrend_clouds_*;
 * RBTREND_TODO.md item 55). Everything outside the revised parts is
 * version 1 as approved.
 *
 * Version 1 was written on the MacBook (M5 Pro) against what is built
 * (rabbittus at the working tree of 2026-11-18, 0.3): render/rbtrendlit.h
 * (RBTREND_LIT_SCENE_DESC::max_view_sets, line 732, card A67;
 * RBTREND_LIT_SCENE_LIGHT, line 884; RBTREND_LIT_ATMOSPHERE, line 1399;
 * RBTREND_LIT_ATMOSPHERE_SHELL, line 1504), render/rbtrendcommands.h
 * (commands 48 to 51, LIT_ATMOSPHERE_SHELLS at line 1225, its parameters
 * at line 1713, the 120-byte `_params`), render/rbtrendatmosphere.h,
 * render/rbtrendimages.h (rbtrend_texture3d, line 534: no mip generated,
 * every level the caller's; rbtrend_texture3d_update, line 673), rbtcore.h
 * (struct types to 5040, line 374), shaders/rbtrend_lit_bindings.slang (set
 * 0's sixteen bindings, the sixteenth the atmosphere's array, line 48),
 * shaders/rbtrend_lit_scene.slang (the scene block), RBTREND_TODO.md item
 * 52 (stage 4 built 2026-11-17 and its measurements; cards A64 to A69) and
 * item 63 (card A67); the approved drafts rbtrend_atmosphere_space_draft.h
 * (version 9, card A55), rbtrend_large_worlds_draft.h (card L66: Rend fed a
 * space whose origin is the eye) and rbtmath_functions_draft.h (card A61);
 * and Hazel's renderer (../.refs/HazelRenderer2025, present: no clouds).
 *
 * The decided lines (DECISIONS.md, "Decided", 2026-11-17, card A57): "Nubis's
 * model (Q1a); density in Math, noise baked from A56, the march Rend's
 * (Q2a); quarter resolution with history (Q4a); a cube-map coverage map
 * (Q5a); flight, orbit, shadows in stages (Q6a). Q3a reshaped:
 * rbtrend_clouds its own object, bounded by a shell or a flat slab, lit
 * through an atmosphere when given one else by the scene's lights, several
 * layers a frame, its look the caller's data." RBTREND_TODO.md item 55
 * adds: "an object of its own (its noise volumes, coverage maps and
 * history), not part of the atmosphere ... the reduced-resolution march and
 * its history Rend's internals, shared later with item 56's fog."
 *
 * This draft is stage 1 of Q6a (from under and inside: flight), whole; it
 * says what stages 2 (from outside a shell, in A55's order) and 3 (cloud
 * shadows on the ground) add, each its own amendment, and shapes nothing
 * here that either would break (section 9).
 *
 * ===========================================================================
 * Version 2 (card A70, decided 2026-11-18 (a)): why, and what was measured
 * ===========================================================================
 *
 * Why. Version 1 contradicted itself: section 3's step 2 offsets each
 * texel's first step "so the history averages the steps' aliasing away",
 * while section 4's step 2 gives the texel marched this frame "the fresh
 * value", so no texel ever holds more than its last march. Card A70's
 * decided line: "a short revision of the draft's history rule - the
 * marched texel blended with its reprojected history by a weight of its
 * own, the rest clamped to its neighbourhood (as Nubis/Horizon and Hillaire
 * do) - measured, its bounds set from the measurement and named."
 *
 * Today's rule, exactly as built (rabbittus working tree, 2026-11-18):
 * - The order. rbtrendcommands.cpp, rbtrend_cmd_lit_clouds: `view.frames`
 *   set to 0 at a record taken anew, `_history_reset` or a target size
 *   changed (line 5718); the history texel of each march texel's 2 x 2
 *   block marched this frame `sub = order[frames % 4]`, order {0, 3, 1, 2}
 *   (lines 5723-5724: top left, bottom right, top right, bottom left; bit
 *   0 x, bit 1 y); `cycle = frames / 4` (line 5725).
 * - The jitter. Lines 5749-5751: x = J(cycle), y = J(5 cycle + 3), the
 *   step's offset z = J(3 frames + 1), J being rbtrend_history_jitter()
 *   (src/render/rbtrendhistory.hpp, line 96: base-2 van der Corput over
 *   RBTREND_HISTORY_JITTERS 16, each at the middle of its sixteenth). The
 *   march (shaders/rbtrend_clouds_march_comp.slang, lines 104-105) goes
 *   through target place (2 h + 2 (x, y)), h the history texel: anywhere
 *   in that history texel's 2 x 2 target texels; its steps at
 *   (q + z) dt (line 206).
 * - The history. shaders/rbtrend_clouds_reconstruct_comp.slang, a thread a
 *   history texel: the marched one takes the march's value and depth (lines
 *   44-47, the branch at line 48 skipped); each other one, unless reset,
 *   carries the cloud at its march texel's depth (line 58: the eye plus the
 *   ray times the march's depth, plus `_space_shift`) through last frame's
 *   view-projection (line 59, kept at line 5859 of the compile), samples
 *   last frame's history bilinearly where it lands inside it (lines
 *   62-67), and keeps it if both are clear (T above 0.999) or the depths
 *   agree within a fifth of the larger (lines 68-70), clamped per channel
 *   to the min and max of the 3 x 3 march texels round its own (lines
 *   71-79), with last frame's depth; else the march's value.
 *
 * Two faults in the order besides the rule, found re-deriving it for this
 * version (pass 6; computed from lines 5723-5751 over 64 frames):
 * - x is at or above a half exactly when the cycle is odd, y exactly when
 *   it is even (J's top bit is its index's lowest, and 5c + 3 has the
 *   other parity from c), so of each history texel's four target texels
 *   only the two off its diagonal, (1, 0) and (0, 1), are ever marched.
 * - A history texel is marched at frames s, s + 4, s + 8, ...; its z is
 *   J(3 frames + 1), whose indices then step by 12 (mod 16) and take four
 *   values in one quarter of the step: the texel at sub 0 is only ever
 *   offset 0.53 to 0.72 of a step, sub 1's 0.03 to 0.22, sub 2's 0.78 to
 *   0.97, sub 3's 0.28 to 0.47. No average of them could remove the steps'
 *   aliasing; neighbouring history texels are offset differently, a fixed
 *   pattern of 2 x 2.
 *
 * What was measured as built (rabbittustests' test_clouds_history, 64 x 64
 * target, one slab of 1,000 to 1,400, 96 steps, M5 Pro, Metal, Debug under
 * validation, 2026-11-18; RBTREND_TODO.md item 55): a still view's frames
 * 4 and 64 2.49 levels apart at most (version 1 said 1), and a view turned
 * 10 degrees a frame with the world moved under the eye 3.19% off a still
 * view of the pose after 4 frames (version 1 said 3%), both at the deck's
 * texels nearest the horizon. The tests hold the measured, rounded up:
 * CLOUDS_STILL_LEVELS 3 and CLOUDS_TURN_SHARE 4% (rabbittustests,
 * tests/rbtrend_gpu_tests.c, lines 25689-25690). Blending the marched texel
 * into its reprojected history by 1 / n (at least a quarter), the order,
 * the jitter and the clamp unchanged, was tried and reached 2.24 levels;
 * not kept.
 *
 * Why 2.24 and not less: test 11's still claim compares frame 4, which
 * holds a single march at every history texel under any rule (frames 0 to
 * 3 march each texel once after the reset), with frame 64; their
 * difference is at least frame 4's own error against the texel's mean,
 * the spread of one march over the jitter, which no history removes. With
 * the 1 / n blend, frame 64's error falls to about 0.38 of one march's
 * (a weight of a quarter keeps sqrt(1/4 / (2 - 1/4)) of it), so the
 * difference falls from about sqrt 2 to sqrt(1 + 0.14) of one march's
 * spread: 2.49 x 1.07 / 1.41 = 1.9 levels expected, 2.24 measured (the
 * worst texel, and the per-channel min-max clamp pulling the averaged
 * history back towards a single march, below). So the claim, not only the
 * rule, was wrong: version 2 states what a history can be held to
 * (section 11, test 11) and keeps frame 4 against 64 as a recorded number.
 *
 * Version 2's rule in short (section 4): one fixed order of the sixteen
 * target texels of each march texel's 4 x 4 block, one a frame, each
 * history texel's four in turn; the step's offset by the history texel's
 * own visit, rotated by a per-texel noise; every history texel's
 * reprojected history clipped to the variance of the march's 3 x 3
 * neighbourhood; the marched one blended into it by a weight, 1 / (v + 1)
 * on its v-th visit since a reset and never below a floor (a quarter
 * recommended, Q8). No C API change (section 8). Its numbers are estimates
 * until measured; the tests' bounds are set from the measurement when it
 * is built, each named in the test.
 *
 * ===========================================================================
 * Version 2's critique passes (2026-11-18, on the revised parts, each fixed
 * before the next)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: the first wording blended the marched texel by a share of the
 *    record's frames, 1 / (frames + 1). A texel is marched once in four
 *    frames, so by frame 4 its second march would be weighted a fifth, as
 *    if four had come before it. Now by the texel's visit, v = frames / 4
 *    (every texel marched in one frame is on the same visit).
 * 2. Missing: what the marched texel's depth becomes. Blending depths as
 *    they are mixes a cloud's distance with the "nothing gathered" distance
 *    (the segments' first start). Now weighted by each side's opacity,
 *    1 - T, the fresh depth where both are clear.
 * 3. Missing: a disocclusion's next visits. A texel whose history is
 *    refused takes its march texel's value (as built); with no age of its
 *    own it converges at the floor's rate, said (Q11 asks whether to keep a
 *    per-texel age).
 * 4. More than the need: the first wording kept a jitter inside the target
 *    texel as well (Halton 2, 3 over 16-frame cycles). The history is half
 *    the target each way and the composite upsamples it: a place inside a
 *    target texel is below what anything reads. Removed; the march goes
 *    through target texel centres.
 * 5. Kept, though it looked like more than the need: the per-texel noise
 *    rotating the step's offset. Without it every march texel of a frame
 *    is offset alike, so the 3 x 3 neighbourhood the clip reads holds one
 *    offset's banding, its variance small, and the clip pulls the averaged
 *    history back to that frame's band: the measured fault's mechanism
 *    again. With it, the neighbourhood spans the offsets.
 *
 * Pass 2 (industry practice; section 10).
 * 1. Karis 2014 (TAA, as remembered): the history clamped (better, clipped
 *    towards the box's centre) to the current frame's 3 x 3 neighbourhood,
 *    in YCoCg, before it is blended. Salvi 2016 (as remembered): the box
 *    from the neighbourhood's mean and standard deviation, mean +- gamma
 *    sigma, tighter than min-max where one sample is far off. Version 2:
 *    Salvi's box, Karis's clip towards its centre, applied to every texel's
 *    history before the marched one's blend.
 * 2. Nubis (Schneider 2015 and 2017, as remembered; Forbidden West's
 *    Nubis cubed, 2022 and 2023, not compared in this session): one texel
 *    of each 4 x 4 marched a frame, the other fifteen reprojected; the
 *    marched one replaces. Version 1 took that rule for its marched texel;
 *    its history being at full resolution, Nubis's fresh texel is a target
 *    texel, not an average of four, so replacing it costs less there.
 *    Version 2 departs from it for the reason card A70 gives.
 * 3. Unreal's volumetric render target (forum answers from Epic, as found
 *    2026-11-18, not its source): mode 0 integrates the lighting over four
 *    frames; mode 2 traces one texel of each 4 x 4 at full resolution and
 *    reprojects the other fifteen; a "ReprojectionBoxConstraint" setting
 *    clamps the reprojection to a box, off unless asked (users turn it on
 *    against ghosting in mode 3). Unity HDRP's volumetric clouds take a
 *    "temporal accumulation factor", the history's share (as remembered,
 *    0.95 by default, a caller's setting). Hillaire 2016 (Frostbite, as
 *    remembered): clouds traced at a reduced resolution and accumulated
 *    over frames with reprojection, the ray's start jittered by a noise
 *    over space and time. Version 2 is that family: an accumulation by a
 *    weight, a box, a jittered start.
 * 4. Interleaved gradient noise (Jimenez 2014, Call of Duty: Advanced
 *    Warfare, as remembered) is the per-texel rotation: deterministic,
 *    one line, no texture. A blue-noise texture is the alternative (Q10).
 * 5. Hazel (../.refs/HazelRenderer2025/Hazel, present): no temporal pass,
 *    no TAA, no history; its GTAO's noise takes a temporal index "always 0
 *    without TAA" (Hazelnut/Resources/Shaders/PostProcessing/GTAO.hlsl,
 *    line 457). Nothing to compare.
 *
 * Pass 3 (the C API).
 * 1. No declaration changes. The weight's floor, the clip's gamma and the
 *    order are Rend's internals (the decided "the reduced-resolution march
 *    and its history Rend's internals"), constants in src/ set from the
 *    measurement. Q8's option b (the caller's weight, as LIT_FOG's
 *    `_history_weight`) would be the one [CHANGED] item: command 52's
 *    parameters gain a float after `_target`, 72 to 76 bytes of the 120,
 *    offsets of every member before it unchanged, listed against abi.txt;
 *    not recommended.
 * 2. One comment changes when built: rbtrendclouds.h's opening comment
 *    (lines 39-41, "the march's where it marched this frame ... clamped to
 *    the march's neighbourhood") says the new rule: [CHANGED, a comment],
 *    no ABI change.
 *
 * Pass 4 (data and allocation).
 * 1. No new texture or record. The history stays two of R16G16B16A16_FLOAT
 *    and R32_FLOAT at W/2 x H/2; the march W/4 x H/4. 23.7 MiB a record at
 *    2560 x 1440 as before.
 * 2. The blocks keep their sizes: the history block (112 bytes,
 *    rbtrendclouds_internal.hpp line 87) takes the weight in `frame[1]` (a
 *    float's bits) and the clip's gamma in `frame[2]`, both spare today;
 *    the march block's `jitter` (line 80) holds the target texel's offset
 *    in its 4 x 4 block (x, y) and the visit's step offset (z), `w` spare.
 * 3. Q11's option b, a per-texel age, would cost a channel: the history's
 *    depth to R32G32_FLOAT, 4 more bytes a history texel twice, 7.4 MB more
 *    at 2560 x 1440. Not recommended.
 * 4. A history in R16G16B16A16_FLOAT blended by a quarter: half's 11
 *    significant bits carry a step of a level's hundredth at the picture's
 *    linear values near 1; no drift worth a format.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Nothing new is shared: the visit count is the record's `frames`,
 *    written by the compile that holds the record (rbtrendhistory.hpp's
 *    rule), and the blocks are per-frame copies as before.
 * 2. Deterministic: the order (a fixed table), the step offset (J of the
 *    visit, rotated by a fixed function of the march texel), the weight (of
 *    the visit) and the clip; the GPU's result still isn't bits (a history
 *    depends on the frames before).
 * 3. No new refusal and no new message: nothing the caller gives changes.
 *
 * Pass 6 (the code as built).
 * 1. Found: the order's two faults (above), computed from lines 5723-5751
 *    over 64 frames and checked in the reconstruct's and the march's
 *    reading of them (march line 105). Version 2's table keeps the built
 *    history order {0, 3, 1, 2} for frames 0 to 3 (its first four entries
 *    land in those history texels) so a reset's first frame is the same
 *    texels as today.
 * 2. Kept from the build: the reprojection (line 58: the march texel's
 *    depth; line 59: last frame's view-projection; `_space_shift`), the
 *    depth agreement within a fifth and the "both clear" case (lines
 *    68-70), the fresh value where a texel lands outside or behind, the
 *    reset's causes (line 5718) and the bilinear read of the history (line
 *    66; Q12 asks about a bicubic one).
 * 3. Changed in the build's terms: the branch at line 48 runs for every
 *    texel (the marched one too), not only `here != frame.x`; lines 71-79's
 *    min-max clamp becomes the variance clip; the marched texel's value and
 *    depth become the blend.
 * 4. The fog (item 56) blends by the same shape (rbtrendcommands.cpp, lines
 *    5418-5426: the history's share n / (n + 1) while below its weight).
 *    The clouds' weight by visit is one function in rbtrendhistory.hpp both
 *    call, the fog with its frames and its caller's weight, the clouds with
 *    their visits and their floor.
 *
 * Pass 7 (tests, records, the build).
 * 1. Test 11 restated (section 11): a still view's flicker and its distance
 *    from its own long mean, both bounded, and frame 4 against 64 printed
 *    and recorded, not bounded; the turn against a still view as before.
 *    Each bound set from the measurement when built and named in the test
 *    (CLOUDS_STILL_FLICKER_LEVELS, CLOUDS_STILL_MEAN_LEVELS,
 *    CLOUDS_TURN_SHARE), with the measured number beside it.
 * 2. Added: a headless test of the order and the weight in
 *    tests/rbtrendhistory_tests.cpp (section 11, test 16), seen failing
 *    first against today's sequence (it fails both faults).
 * 3. The build: Rend's Metal Debug and Release under validation, -asan and
 *    -tsan, rabbittustests' whole suite (test_frame_allocations' clouds
 *    scenario unchanged: no allocation added); check.sh's abi (no change),
 *    caller-names, doxygen, conventions. D3D12 and Vulkan in their sessions
 *    (card X25), as version 1.
 *
 * ===========================================================================
 * Version 1's revision notes (seven passes, 2026-11-18, each fixed before
 * the next; the lenses are the workspace's critique prompt's)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: version 0 kept a history per view as A39's caller-owned handle
 *    (the original Q4a's words, "A39's handle"). The reshaped Q3a puts the
 *    history in the object ("its noise volumes, coverage maps and
 *    history"), and A39's decided line makes its handle "the caller's
 *    per-view history" of the picture, with velocity, for a traced
 *    shadow's mask. The clouds reproject by their own depth, never by the
 *    picture's velocity. Changed: a history per view inside the clouds,
 *    records sized at creation (section 4), A39's handle left to its own
 *    draft; Q1 asks.
 * 2. Missing: the eye's move in a space that moves with it. A caller that
 *    feeds Rend eye-relative places (card L66: the eye at the origin
 *    every frame) gives the same eye position each frame, so a history
 *    can't be reprojected from the views alone. Added `_space_shift`: this
 *    frame's origin in the last frame's space (0 for a caller whose space
 *    doesn't move), and `_history_reset` for a cut.
 * 3. Missing: what is marched from outside a shell before stage 2. A
 *    shell layer seen from outside is marched by the same command (the ray
 *    enters the shell); what stage 1 lacks is its composition inside A55's
 *    shell pass. Said (section 9), not refused (Q7).
 * 4. More than the need: version 0 had a powder term (Schneider's dark
 *    edges). Hillaire's multiple-scattering octaves (2016) give the same
 *    look within an energy-conserving model; one way, not two. Removed.
 * 5. More than the need: version 0 took the caller's 3D textures and cube
 *    texture by views. The decided object holds its volumes and maps, and
 *    a view of the caller's texture would need its format, size, usage and
 *    layout checked on every send. Now the object owns them, written by
 *    two calls (Q3).
 * 6. Missing: a turning world. A shell's map and noise lie in the world's
 *    own axes (its weather turns with it; a caller's sky turns with its
 *    entity, card L62's stage 1); version 0 read them in the scene's axes,
 *    so a world's clouds stood still while its ground turned under them.
 *    Added RBTREND_CLOUD_LAYER::_axes, the layer's frame in the scene's
 *    space: a point is carried into it (relative to `_origin`) before
 *    Math's definition, which works in that frame.
 *
 * Pass 2 (industry standard, compared; section 10).
 * 1. Nubis (Horizon Zero Dawn, 2015 and 2017, as remembered): one texel of
 *    each 4 x 4 block marched a frame, at full resolution after 16 frames.
 *    Unreal's Volumetric Cloud (as remembered from its documentation:
 *    traced at a reduced resolution, reconstructed with temporal
 *    reprojection, composited with the SkyAtmosphere's aerial perspective
 *    at the clouds' mean depth, lit by the atmosphere's lights' transmittance,
 *    multiple scattering by octaves): a quarter of the pixels each way
 *    marched, reconstructed at half resolution. Hillaire 2016 ("Physically
 *    Based Sky, Atmosphere and Cloud Rendering in Frostbite"): the energy-
 *    conserving step, Wrenninge's octaves, the mean depth for aerial
 *    perspective. This draft: Unreal's and Hillaire's composition, the
 *    history's resolution Q2.
 * 2. A cloud layer's lights: Unreal's clouds take the atmosphere lights
 *    (directional) alone; local lights light its volumetric fog. Here the
 *    same: directional lights by index, as A55's shells; point and spot
 *    lights refused (Q6), card A59's fog takes them.
 * 3. Grey extinction: every engine compared keeps cloud extinction one
 *    channel, the albedo coloured; so here (Q5).
 *
 * Pass 3 (the C API to standard).
 * 1. RBTREND_CLOUDS_DESC and RBTREND_CLOUD_LOOK are based (a desc and a
 *    setting, both may grow); RBTREND_CLOUD_LAYER is an array element sent
 *    each frame: frozen, `_`-prefixed members, as RBTREND_LIT_ATMOSPHERE_SHELL.
 *    The look holds Math's frozen RBTMATH_CLOUD_SHAPE by value (a frozen
 *    struct in a based one is allowed; a based one by value is not).
 * 2. The two write calls follow rbtrend_texture3d_update's shape (queue,
 *    region, data and its exact size), recorded with the frame's batch.
 * 3. Limits named: RBTREND_CLOUDS_MAX_LAYERS (the frozen block's array),
 *    _MAX_LOOKS, _MAX_COVERAGE_MAPS, _MAX_VIEWS, _MAX_STEPS,
 *    _MAX_LIGHT_STEPS, _MAX_SCATTERING_OCTAVES; no 4 or 8 at a point of use.
 * 4. Checks one condition each, in parameter order then member order.
 * 5. `_history_reset` was a bool; no command's parameters hold one
 *    (rbtrendcommands.h: flags and counts are uint32_t, LIT_PATH_TRACE's
 *    `_accumulated` 0 "starts over"): now a uint32_t, 0 or 1, any other
 *    value refused, so a later meaning can't be read as 1.
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. Version 0 made a view's history at its first compile, at its
 *    target's size: a GPU allocation in a compile, and again at a resize.
 *    CONVENTIONS.md: records made at creation, sized from the desc, nothing
 *    grown behind the caller. Now the desc names the largest target and
 *    the views: every texture is made by rbtrend_clouds_create(), a target
 *    larger refused at the compile (RBT_RESULT_LIMIT). A steady frame
 *    allocates nothing (test_frame_allocations).
 * 2. The layers are copied at the send into the list's side storage, as
 *    shells are; resolved at the compile into the view record's per-frame
 *    block (a per-frame copy, rbtrendbuffers.h), never written while a frame
 *    in flight reads it.
 * 3. The looks: one GPU block of the object, RBTREND_CLOUDS_MAX_LOOKS slots,
 *    written by rbtrend_clouds_set_look() into the next submission's copy
 *    (per-frame copies), so a look set while a frame is in flight changes
 *    the next frame, never the one in flight.
 * 4. One pass marches every layer for a texel (segments sorted along the
 *    ray), not a pass a layer: the per-texel work is the march, the
 *    layers a short loop over a block in constant memory.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. The object is one object for the rule (rbtcore.h, "Threads"): its
 *    writes, its looks and every command naming it one caller at a time,
 *    with the rest of its instance.
 * 2. A command names a view record by its scene: two LIT_CLOUDS for one
 *    scene in one frame would advance its history twice; refused at the
 *    compile (the second), naming the scene.
 * 3. The GPU's result isn't bits: a history depends on the frames before.
 *    What is deterministic: the march's jitter order (a fixed sequence by
 *    the record's own frame count), the layer order (by the ray's segments,
 *    ties by the array's order), and every refusal.
 * 4. Errors: rbtrend_fail's per-call RBTREND_ERROR at the calls; the
 *    compile's report for the command (rbtrend_cmd_report), each refusal
 *    its own message naming the layer by index.
 *
 * Pass 6 (correctness against the platforms and the code as built).
 * 1. Set 0 is full (rbtrend_lit_bindings.slang, line 48: the atmosphere's
 *    array the sixteenth binding, CRGPU_MAX_DESCRIPTOR_BINDINGS_PER_SET 16).
 *    The march reads the scene's set 0 as the shell pass does (LIT_REFLECT's
 *    shape) and adds nothing to it; its own sets are 1 to 3 (section 6),
 *    within CRGPU_MAX_DESCRIPTOR_SET_LAYOUTS 4.
 * 2. Version 0 sampled the coverage cube with a cube view. A shell's map
 *    and a slab's in one pass would need both a TextureCube and a
 *    Texture2D binding a map slot, and a GPU's seamless cube filtering
 *    isn't the CPU's definition. Now one 2D array, six layers a map, read
 *    by hand (Math's face table, rbtmath_clouds_draft.h 3c), one binding.
 * 3. rbtrend_texture3d has no generated mips (rbtrendimages.h, line 552):
 *    the volumes are one level, read at level 0 everywhere, which is also
 *    what Math's CPU density reads.
 * 4. Blending into a float target: Vulkan guarantees blending of
 *    R16G16B16A16_SFLOAT alone; Carrottus GPU has no per-format query
 *    (card S51, decided a, drafted beside this): R32G32B32A32_FLOAT and
 *    R11G11B10_FLOAT are LIT_REFLECT's and the shells' same gap, refused
 *    when S51's query says so once it is built.
 * 5. The command's parameters: 72 bytes of the 120 (counted, section 8;
 *    version 0 said 76, a padding counted twice).
 * 6. The shell pass's view-set records (card A67): LIT_CLOUDS takes one
 *    set from the scene's records too (its depth and its layers'
 *    atmospheres' tables); RBTREND_LIT_SCENE_DESC::max_view_sets' comment
 *    names it beside the four commands it lists (line 739).
 * 7. Numbers re-derived (section 7): 640 x 360 marched texels at 2560 x
 *    1440; 24.9 MiB a view record there.
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Tests named for rabbittustests (section 11), each a claim of sections
 *    3 to 7, against Math's density on the CPU where one exists.
 * 2. Before it is built: the GPU test of a 3D texture sampled on three
 *    backends that RBTREND_TODO.md item 55 asks for (test 1 here, written
 *    first; the clouds don't write a 3D texture from compute: A59's fog
 *    does, and its test covers that).
 * 3. Versions: no raise while 0.3 is untagged (card X2). ABI: additions
 *    only (section 8).
 * 4. Struct types and the command's number are the next free at the build:
 *    5041 and 5042 and 52 as of this writing (rbtrend_lit_layers_draft.h
 *    plans 5039; another draft of the same day may take 52 first).
 *
 * ===========================================================================
 * 1. What is drawn
 * ===========================================================================
 *
 * Clouds a camera flies under, through and over: each layer a medium whose
 * density is Rabbittus Math's (rbtmath_cloud_compute_density's definition),
 * lit by the sun (or two) through the layer itself and, where given, through
 * an atmosphere, with the scene's ambient light, composited over the
 * picture with the scene's own air in front. Low cumulus and high cirrus,
 * an overcast deck, a storm, dust, or a fog bank in a valley are settings
 * (looks) and maps, the same command.
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * [ADDED] rbtrend_clouds, an object of an instance: two noise volumes, its
 * coverage maps, its looks, and a record a view (a scene drawn) holding
 * that view's march and history. Made by rbtrend_clouds_create() from
 * RBTREND_CLOUDS_DESC (sizes and counts, no default), destroyed by
 * rbtrend_clouds_destroy().
 *
 * [ADDED] rbtrend_clouds_write_noise(), rbtrend_clouds_write_coverage():
 * the caller's bytes into the object's volumes and maps (Math's bake, its
 * map), recorded with the frame's batch as rbtrend_texture3d_update is.
 *
 * [ADDED] rbtrend_clouds_set_look(): a look into a slot (RBTREND_CLOUD_LOOK:
 * Math's shape and the light's numbers).
 *
 * [ADDED] RBTREND_COMMAND_LIT_CLOUDS (52), outside a pass, after the lit
 * surfaces and the shells, before transparents: marches the layers it is
 * given (RBTREND_CLOUD_LAYER, frozen, an array copied at the send) for its
 * scene's view, against depth, and composites them over the target.
 *
 * [CHANGED] RBTREND_LIT_SCENE_DESC::max_view_sets' comment: LIT_CLOUDS
 * among the commands that take a set from the records.
 *
 * Two struct types, no result code. rbtrend.h's umbrella gains
 * render/rbtrendclouds.h (check.sh umbrella), which includes
 * rbtrendlit.h and math/rbtmathclouds.h as rbtrendatmosphere.h includes
 * math/rbtmathmedium.h.
 *
 * ===========================================================================
 * 3. The march (stage 1: from under and inside)
 * ===========================================================================
 *
 * For each texel marched this frame (section 4), the view ray from the
 * scene's eye through a point of the texel (jittered, section 4):
 * 1. SEGMENTS. Each layer's bounds meet the ray in at most two segments:
 *    a slab one, between its planes; a shell one where the ray stays
 *    outside its inner sphere, two where it passes through the inner
 *    sphere (into the shell, out into the inner sphere, back into the
 *    shell, out past its top): the inner sphere is no ground to the
 *    clouds, whose bounds hold none (a ground is a surface in `_depth`,
 *    or an atmosphere's, which shadows the light, step 4). Each segment
 *    cut at the eye (from t 0), at `_max_distance`, and at the surface
 *    `_depth` holds (a texel at the projection's far value holds none,
 *    whatever its far plane, as A55's). Shell roots by A55's stable form
 *    (sphere_hits_stable, rbtrend_atmosphere_model.slang). Segments of
 *    every layer sorted by their start, ties by the layer's index: front
 *    to back.
 * 2. STEPS. At most `_max_steps` samples over the segments' total length,
 *    spaced evenly, each segment at least one, each segment's first offset
 *    by a fraction z of a step [CHANGED in version 2, card A70]: z =
 *    frac(J(v) + N(m)), J the history's jitter sequence (base-2 van der
 *    Corput over 16, each at the middle of its sixteenth), v the visit
 *    (section 4: how many times this history texel has been marched since
 *    its reset, the same for every texel marched in one frame), N
 *    interleaved gradient noise of the march texel m, frac(52.9829189 x
 *    frac(0.06711056 m.x + 0.00583715 m.y)). Each history texel is so
 *    offset through all sixteen fractions in its first sixteen visits, and
 *    neighbouring texels differently within a frame; the history's blend
 *    (section 4) averages the steps' aliasing away. (Version 1's z,
 *    J(3 frames + 1), gave each history texel four fractions within one
 *    quarter of a step: "Version 2" above.)
 * 3. AT A SAMPLE: the density rho by Math's definition (its steps 1 to 7,
 *    the layer's look, bounds, offsets and map), and sigma_t = rho x the
 *    look's `extinction`, sigma_s = sigma_t x the look's `albedo` (per
 *    channel). Where rho is 0 nothing more is read (Nubis's early out).
 * 4. LIGHT. For each of the layer's lights (RBTREND_CLOUD_LAYER::_lights:
 *    directional; placed once item 58 is built), a march towards it of
 *    `_light_steps` samples over a cone, spaced growing by 2 (Nubis's six,
 *    the last far), summing the optical depth tau of every layer whose
 *    bounds hold a sample (an upper deck shades a lower one; layers
 *    seldom share heights, so usually one density a sample). Its light: the light's
 *    radiance x pi (rbtrendlit.h's scale), x the atmosphere's transmittance
 *    from the sample towards it where the layer names an atmosphere (its
 *    table at the sample's height above that atmosphere's bottom radius
 *    and the light's angle there: dark where the ground hides it), x
 *    Wrenninge's octaves: for n = 0 to `scattering_octaves` - 1,
 *    b^n x exp(-a^n tau) x phase(c^n g), a, b, c the look's three
 *    octave numbers, phase the look's two Henyey-Greenstein lobes blended.
 * 5. AMBIENT. The scene's ambient light (RBTREND_LIT_AMBIENT: its
 *    hemisphere, and its environment's irradiance where it has one) at
 *    the layer's up for the top and its down for the bottom, blended by the
 *    sample's height in the layer, x the look's `ambient_scale`.
 * 6. INTEGRATION. Hillaire's energy-conserving step (2015): the light
 *    scattered over a step dt is S x (1 - exp(-sigma_t dt)) / sigma_t,
 *    added x the transmittance so far; the transmittance x exp(-sigma_t
 *    dt). The march stops when the transmittance is below 2^-10 (taken as
 *    0). Its depth: the transmittance-weighted mean distance of what it
 *    gathered (Hillaire's), the segments' first start where nothing was.
 * 7. RESULT: light L (RGB), transmittance T (one channel), depth d.
 *
 * Lit through an atmosphere or not (the decided "lit through an atmosphere
 * when the caller gives one, else by the scene's lights alone"): a layer's
 * `_atmosphere` NULL takes its lights as they are (no air dims them, no
 * ground shadows a shell: the caller's sun is the sun); one given dims each
 * light by that atmosphere's transmittance table at each sample, so a deck
 * at sunset is lit red and a shell's night side is dark. Its tables built
 * (ATMOSPHERE_TABLES) before, as the shells require; one never built, or
 * with no medium, is taken as none, the compile's Debug report saying so.
 *
 * ===========================================================================
 * 4. Resolution, history and reconstruction (Q1, Q2, and version 2's Q8
 *    to Q12; Rend's internals, shared with item 56's fog)
 * ===========================================================================
 *
 * A view record holds, at the desc's largest target (W x H):
 * - the march: W/4 x H/4 (rounded up), light and transmittance
 *   (R16G16B16A16_FLOAT) and depth (R32_FLOAT);
 * - the history, two of each (read last frame's, write this frame's) at
 *   W/2 x H/2: R16G16B16A16_FLOAT and R32_FLOAT.
 * Each frame, for the view's target (its size from the scene's view, at
 * most W x H, in the top left of every texture) [REWRITTEN in version 2,
 * card A70; version 1's steps 1 and 2 are in "Version 2" above, as built]:
 *
 * Terms. A march texel m covers the 4 x 4 target texels from 4m, and the
 * 2 x 2 history texels from 2m. f is the record's frame since its last
 * reset (`frames` in the build, 0 at the reset), and v = floor(f / 4) the
 * visit: every history texel is marched once in each run of four frames,
 * so the texels marched in frame f are all on their v-th visit.
 *
 * 1. MARCH every march texel m through the centre of target texel 4m +
 *    B(f mod 16), B the 4 x 4 ordered-dither order, frames 0 to 15:
 *      (0,0) (2,2) (2,0) (0,2)  (1,1) (3,3) (3,1) (1,3)
 *      (1,0) (3,2) (3,0) (1,2)  (0,1) (2,3) (2,1) (0,3)
 *    (the place clamped to the target's last texel centre at its right and
 *    bottom edges, as built). Its history texel, 2m + B / 2, runs (0,0),
 *    (1,1), (1,0), (0,1) in every four frames: the built order {0, 3, 1,
 *    2}. Each history texel is marched through each of its four target
 *    texels once in sixteen frames (visits 0 to 3 at (0,0), (1,1), (1,0),
 *    (0,1) of its own 2 x 2), and the 4 x 4 block's sixteen in sixteen
 *    frames. Its steps offset by section 3's z, of the visit v. The march
 *    writes its value F (light L, transmittance T) and depth d_F.
 * 2. RECONSTRUCT every history texel h, its march texel m = floor(h / 2):
 *    a. REPROJECT, unless the record was reset this frame: the point on
 *       h's ray (through its 2 x 2 target texels' shared corner, as built)
 *       at m's depth d_F, plus `_space_shift` (this frame's origin in last
 *       frame's space), through last frame's view-projection (the record
 *       keeps it); where it falls in front of last frame's eye and inside
 *       last frame's target, last frame's history H and its depth d_H read
 *       there, bilinearly. Accepted where both are clear (T_F and T_H above
 *       0.999) or the depths agree within a fifth (|d_H - d_F| at most 0.2
 *       x the larger), as built; else refused (a disocclusion: the cloud
 *       H held is not what this ray meets, or no history covers it).
 *    b. CLIP an accepted H (every history texel, the marched one too, before
 *       its blend): the mean mu and standard deviation sigma of each of the
 *       four channels (L's three and T) over m's 3 x 3 march texels (edges
 *       clamped, as built); H moved along the line from mu towards H to the
 *       box mu +- gamma sigma where it lies outside it (Karis's clip towards
 *       the box's centre, Salvi's box), gamma a constant of Rend's set from
 *       the measurement (estimate 1.0 to 1.5; Q9). Channels as stored: L
 *       linear and premultiplied by the cloud's opacity, and T (Q9).
 *    c. BLEND, the texel marched this frame: H' (the clipped H) and the
 *       march's F by a weight alpha = max(1 / (v + 1), alpha_min): the value
 *       H' + alpha (F - H'), its depth the two depths weighted by alpha and
 *       1 - alpha and each by its opacity (1 - T), d_F where both opacities
 *       are below 2^-10. alpha_min a constant of Rend's, a quarter
 *       recommended (Q8): the first visit after a reset takes the march
 *       alone (alpha 1), the second a half, the third a third, every later
 *       one a quarter, an exponential average of (2 - alpha) / alpha, 7,
 *       marches once settled.
 *    d. KEEP, a texel not marched this frame: H' and d_H.
 *    e. REFUSED, or the record reset this frame: F and d_F (m's march, a
 *       quarter resolution), marched texel or not. A refused texel has no
 *       age of its own: its next visits blend at the record's alpha (the
 *       floor, once settled), so its value starts from its march texel's
 *       and reaches its own over about 4 / alpha_min frames (Q11).
 *    The record resets, as built, at `_history_reset`, at a record taken
 *    anew or never written (rbtrendhistory.hpp), and when the target's size
 *    changed; f is then 0.
 * 3. COMPOSITE at full resolution over `_target`: the history upsampled by
 *    depth (the four nearest half-resolution texels weighted by distance
 *    and by how near each one's cloud depth is to the drawn surface's,
 *    where there is one in front), then the scene's air in front of the
 *    clouds' depth d (section 5), then blended: target x T + L'.
 *    (Unchanged.)
 *
 * What it gives (estimates, version 2's, to be measured when built; the
 * tests' bounds set from the measurement, section 11):
 * - From a reset: every history texel holds one march after four frames
 *   (a texel no history covers, the screen's edge turning in, the same),
 *   the floor's average after sixteen.
 * - A still view, settled: a texel moves by alpha_min of a march's
 *   deviation at each visit, so frame to frame by at most a quarter of the
 *   largest deviation (and a texel not marched only where the clip moves
 *   it, its history outside a frame's box); at test 11's worst texels (2.49 levels between two
 *   marches as built) about 0.6 to 1 level, the horizon's texels the
 *   worst. Against its own long mean (frames 64 to 127 averaged): about
 *   0.38 of one march's spread, 1 to 1.5 levels at the worst texel.
 * - Frame 4 against frame 64: still about one march's spread from the mean
 *   (frame 4 holds one march under any rule): expected 1.9 to 2.5 levels;
 *   recorded, not a bound.
 * - Turning 10 degrees a frame: each frame's bilinear read of the history
 *   softens it a little, and no texel's visits restart (the clip, not a
 *   reset, bounds what the turn carries); against a still view of the pose
 *   after four frames, about as version 1's measured, 3 to 4%.
 * - Lag: a change the neighbourhood shows (a deck that thins, a sun that
 *   drops) is clipped to within gamma sigma of this frame's marches at
 *   once; within the box it fades over about 4 / alpha_min frames, 16 at a
 *   quarter (a quarter of a second at 60 frames a second).
 * - Cost: the reconstruct reads what it read (nine march texels, the
 *   history twice) for every texel now, not three of four, and adds the
 *   mean and variance arithmetic: estimate under 0.05 ms more at 2560 x
 *   1440, within the measured spread of section 7.
 *
 * Why inside the object, per view (Q1): a history is the clouds' own (their
 * depth, their light), useless to anything else; one record a view, made
 * with the object, so nothing is allocated in a frame. Item 56's fog keeps
 * froxel histories of the same kind; the reprojection (a place by its
 * depth, shifted, through last frame's view-projection) and the records'
 * bookkeeping are one internal module (src/render/rbtrendhistory.hpp),
 * not public.
 *
 * Which record a scene takes: the record that drew it before, else one no
 * list compiled and not yet submitted holds (an empty one, else the one used
 * least recently, its history then reset), else the compile is refused with
 * RBT_RESULT_LIMIT naming RBTREND_CLOUDS_DESC::max_views. As card A67's
 * view-set records, by the scene's handle.
 *
 * ===========================================================================
 * 5. Composition
 * ===========================================================================
 *
 * The frame's order: the lit surfaces (their sky drawn by the sky shader),
 * LIT_REFLECT, LIT_ATMOSPHERE_SHELLS, then LIT_CLOUDS, then transparent
 * surfaces and the tone map. The clouds are over everything behind them
 * and under what is drawn in front (the march stops at `_depth`).
 *
 * The air in front: with a scene atmosphere that has an aerial volume, its
 * light and transmittance between the eye and the clouds' depth d, read at
 * d as a lit surface reads them (per channel, card A64), applied as A55's
 * shell pass applies them over a drawn surface: L' = L_air(d) x (1 - T) +
 * T_air(d) x L. Past the volume's last slice, the last slice's (as a
 * surface past it). With no volume, or no scene atmosphere: L' = L. (Card
 * A59's height fog, once built, is applied the same way at d: its draft
 * says so.)
 *
 * The blend: colour ONE, SRC_ALPHA (target x T + L'); alpha ZERO, ONE (the
 * target's alpha kept). Grey T, so no dual-source blending (Q5).
 *
 * ===========================================================================
 * 6. Sets, bindings, blocks (pass 6.1)
 * ===========================================================================
 *
 * - Set 0: the scene's own set (its block: view, eye, lights, ambient,
 *   environment, the atmosphere's array for the air in front), as the
 *   shell pass binds it. Nothing added.
 * - Set 1, the object's, made by rbtrend_clouds_create(): the base volume,
 *   the detail volume, the coverage maps' array (six layers a map), the
 *   looks' block, a repeating linear sampler (the volumes), a clamping
 *   linear sampler (the maps, the history). Six bindings.
 * - Set 2, the view record's, two made with it (one for each history
 *   direction): its march, its two histories (one read, one written), its
 *   per-frame layers' block. Five bindings.
 * - Set 3, from the scene's view-set records (card A67): `_depth` and each
 *   layer's atmosphere's transmittance table (up to
 *   RBTREND_CLOUDS_MAX_LAYERS; an unnamed one bound to the first's, unread).
 *   Five bindings. Layers naming the same atmospheres in the same order
 *   share it.
 * - Push constants: the pass's (which part of the history, the frame's
 *   jitter), at most 32 bytes.
 * The layers' block, per frame per record: RBTREND_CLOUDS_MAX_LAYERS
 * entries of the layer resolved (bounds, origin, offsets, look and map
 * slots, lights' directions and radiances taken from the scene's lights at
 * the compile, the atmosphere's centre and radii, the layer's axes): 256
 * bytes an entry (about 200 used), 1 KiB a block.
 *
 * ===========================================================================
 * 7. Cost and memory (estimates; to be measured before the first caller
 *    turns it on)
 * ===========================================================================
 *
 * Method: A55's measured shell march (RBTREND_TODO.md item 52: the view
 * filled from 100 km up at 4 to 32 steps, 2.42 to 2.46 ms at 1440p over
 * 3,686,400 texels, about 0.66 ns a texel at up to 32 steps, two table
 * reads a step a sun) scaled by the reads: a cloud step reads two volumes
 * and the map (three), and in cloud six light samples of the base volume
 * and the map, each with a profile's arithmetic: 4 to 8 times a shell step,
 * at up to 64 steps (2x), early outs on empty air (Nubis's) about halving
 * it in a typical sky. Per marched texel 5 to 10 ns; 640 x 360 = 230,400
 * marched at 1440p: 1.2 to 2.3 ms, the reconstruction and composite
 * (3.7 million texels, a few reads each) 0.15 to 0.3 ms. Total 1.3 to 2.6
 * ms at 1440p, 0.8 to 1.5 at 1080p, on the M5 Pro, Release. The card's
 * budget: 1.5 to 2 ms at 1440p. Levers, each the caller's: `_max_steps`,
 * `_light_steps`, `_max_distance`, a look's `scattering_octaves`.
 * Measured by test_clouds_measure before any caller turns it on, the
 * estimate replaced. (Measured 2026-11-18: RBTREND_TODO.md item 55. Version
 * 2 adds to the reconstruct only, estimate under 0.05 ms at 2560 x 1440,
 * remeasured by the same test when built.)
 *
 * Memory (each made by rbtrend_clouds_create, none later):
 * - the volumes: base^3 x 4 + detail^3 x 4 (Nubis's 128 and 32: 8 MiB +
 *   128 KiB);
 * - the maps: coverage_maps x 6 x size^2 x 4 (one map of 512: 6 MiB);
 * - the looks' block: max_looks x 256 bytes, x frames in flight;
 * - a view record at W x H: (W/4 x H/4) x 12 + 2 x (W/2 x H/2) x 12 bytes,
 *   and its layers' block (1 KiB x frames in flight): at 2560 x 1440,
 *   2,764,800 + 22,118,400 = 24,883,200 bytes (23.7 MiB; 24.9 MB); at
 *   1920 x 1080, 14.0 MB.
 * - descriptors: set 1, six; set 2, five a direction, ten a record; a set 3
 *   from the scene's records.
 *
 * ===========================================================================
 * 8. ABI
 * ===========================================================================
 *
 * Additions only: a new header render/rbtrendclouds.h (a handle, two based
 * structs, one frozen, five calls, eight limit macros); command 52 and its
 * parameters (72 bytes of the 120: scene 8, clouds 8, layers 8, count 4,
 * max steps 4, light steps 4, max distance 4, space shift 12, reset 4,
 * depth 8, target 8); struct
 * types REND_CLOUDS_DESC and REND_CLOUD_LOOK; one comment changed
 * (max_view_sets). Nothing breaks. No version raise while 0.3 is untagged
 * (card X2).
 *
 * Version 2 (card A70): no declaration changes and nothing in abi.txt. One
 * comment: rbtrendclouds.h's opening comment, lines 39-41 as built ("Each
 * half-resolution texel of the history is then the march's where it
 * marched this frame and last frame's history reprojected by the cloud's
 * depth elsewhere, clamped to the march's neighbourhood") [CHANGED, a
 * comment], to: "Each half-resolution texel of the history is then last
 * frame's history reprojected by the cloud's depth and clipped to the
 * march's neighbourhood, the texel marched this frame blended with it; the
 * history averages the marches over frames." Only Q8's option b would
 * change the C API (pass 3.1).
 *
 * ===========================================================================
 * 9. Stages 2 and 3, and what they add (Q6a; nothing here closes them)
 * ===========================================================================
 *
 * STAGE 2, FROM OUTSIDE A SHELL (orbit). The march already enters a shell
 * from outside; what is missing is its place in A55's shell pass, which
 * marches each world's air farthest first and blends it over the picture:
 * a world's clouds sit inside its air, with air in front and behind. Stage 2
 * hands the shell pass each shell's clouds (their L, T and depth, from this
 * object's record) so its march adds the clouds at their depth, by a new
 * command or a new parameter of a new number (command 51's parameters are
 * released), drafted then. Until it is built, a layer seen from outside is
 * drawn as from inside (composited over what is behind with the scene's
 * own air in front, none from its own world's shell), correct only where
 * no shell is drawn over it; LIT_ATMOSPHERE_SHELLS recorded before it
 * puts its air under the clouds. Said on the command (Q7).
 *
 * STAGE 3, CLOUD SHADOWS ON THE GROUND. A map a light, the clouds'
 * transmittance towards it over the ground (top-down for each directional
 * light, at the cascades' anchor), made by this object; read by the lit
 * surfaces, which needs a binding in set 0, full (pass 6.1). Its binding
 * plan is a draft of its own, with card A59's stage 3 (transparents
 * reading the fog's grid needs one too): both are one plan for set 0.
 *
 * ===========================================================================
 * 10. Compared (pass 2) and alternatives
 * ===========================================================================
 *
 * - Horizon's Nubis (Schneider 2015, 2017; as remembered): its model, its
 *   early outs, its light cone of six samples; its reprojection marches
 *   one texel of 4 x 4 a frame at full resolution (Q2's b: 16 frames to
 *   fill from a cut, 4 times the history's memory).
 * - Hillaire 2016 (Frostbite, as remembered): energy-conserving
 *   integration, octaves of multiple scattering, the mean depth for the
 *   air in front; this draft's sections 3 and 5.
 * - Unreal's Volumetric Cloud (documentation, as remembered): a layer in
 *   the SkyAtmosphere's shell, a material's density, the atmosphere
 *   lights' transmittance, reconstructed at a reduced resolution, cloud
 *   shadows on the ground and a shadow map for the atmosphere. Here a
 *   slab too, no atmosphere needed, several layers a frame (Unreal draws
 *   one cloud layer, as remembered: Rend's choice is the decided "several
 *   layers a frame").
 * - Unity HDRP's Volumetric Clouds (as remembered): a cloud layer over a
 *   planet in its Physically Based Sky, a cloud map, presets (sparse,
 *   cloudy, overcast, stormy) as data: the decided "look as data".
 * - Hazel (../.refs/HazelRenderer2025, present): no clouds; its only fog
 *   a distance fade in Hazelnut's Resources/Shaders/PostProcessing/
 *   SceneComposite.glsl, disabled (HZ_RENDERER_FOG_EFFECT 0, line 25),
 *   its distances constants in the shader.
 * - Version 2's history (pass 2 of its notes): Karis 2014's clip towards
 *   the neighbourhood's centre, Salvi 2016's variance box, Unreal's
 *   accumulation over four frames and its optional box constraint, Unity
 *   HDRP's accumulation factor, Hillaire 2016's jittered start and
 *   accumulation, Jimenez 2014's interleaved gradient noise. Nubis's
 *   replacing of the marched texel is version 1's rule, left (card A70).
 * Alternatives not taken: a part of RBTREND_LIT_ATMOSPHERE (the card's
 * Q3b, decided against); a pass a layer (pass 4.4); half-resolution march
 * with no history (the card's Q4b); the GPU's own noise (Q2b).
 *
 * ===========================================================================
 * 11. Tests (C through the public API; rabbittustests on Metal under its
 *     validation, warnings fatal, Debug and Release; each seen failing
 *     first)
 * ===========================================================================
 *
 * 1. test_texture3d_sampled (before the rest, item 55's prerequisite, owed
 *    whether or not clouds come): a 3D texture of RGBA8 written by
 *    rbtrend_texture3d_update and sampled with a repeating linear sampler
 *    by a test shader at texel centres, edges and across the wrap: each
 *    within 1/255 of Math's rbtmath_noise_volume_sample; on Metal here,
 *    D3D12 and Vulkan in their sessions (card X25).
 * 2. test_clouds_density_probe: a test shader running the march's density
 *    function at 4,096 points of a shell and a slab (both warps, both
 *    addresses) against rbtmath_cloud_compute_density: within a recorded
 *    tolerance (estimate 0.02 of the density scale; measured, recorded).
 * 3. test_clouds_refusals: every check of every call and of the command's
 *    compile, each its message, in order; nothing changed.
 * 4. test_clouds_deck_from_below: an overcast slab (coverage 1, a stratus
 *    profile) over a sky, sun overhead: the picture darker than the sky
 *    behind by Beer's law of the deck's optical depth within 5%, every texel
 *    of the deck's middle within 2 levels of its neighbours after 4 frames.
 * 5. test_clouds_thin_layer_transmittance: one thin uniform layer (a
 *    profile of a full thickness, coverage 1, no erosion, a constant
 *    volume): T against exp(-sigma_t x path) worked out for each texel's
 *    ray, within 1%.
 * 6. test_clouds_shell_and_slab: the same look, map and offsets as a shell
 *    of a large radius and a slab, seen from under at the shell's top:
 *    the middle third of the picture within 2% (the bounds change height
 *    and coverage only).
 * 7. test_clouds_layers: two layers (low and high) against each alone: the
 *    pair equals the near over the far within 1%, ray by ray; their order
 *    in the array swapped: the same picture.
 * 8. test_clouds_depth: a surface in front of a deck (none drawn over it,
 *    bit for bit) and behind it (dimmed by T).
 * 9. test_clouds_atmosphere: the same deck lit by a sun 2 degrees up with
 *    and without an atmosphere: redder with (its red over blue above
 *    without's), and on a shell's night side (the sun under its horizon)
 *    black but for ambient.
 * 10. test_clouds_air_in_front: a deck 20 km off through a scene
 *    atmosphere with a volume: L' against A55's formula worked from the
 *    volume's readback, within 1%.
 * 11. test_clouds_history [REWRITTEN in version 2, card A70]: a still view
 *    reset at frame 0 and drawn to frame 127. (a) Flicker: the worst
 *    channel difference between consecutive frames from 64 to 80 within
 *    CLOUDS_STILL_FLICKER_LEVELS (estimate 0.6 to 1 level). (b) Settled:
 *    frame 64 against the mean of frames 64 to 127, read back and averaged
 *    by the test, within CLOUDS_STILL_MEAN_LEVELS (estimate 1 to 1.5
 *    levels). (c) Frame 4 against frame 64 printed and recorded in item 55,
 *    no bound (one march's spread: estimate 1.9 to 2.5 levels). The view
 *    turned 10 degrees a frame and moved with `_space_shift` (the eye at
 *    the origin, the world moved): the deck's texels within
 *    CLOUDS_TURN_SHARE of a still view's of the same pose after 4 frames
 *    (estimate 3 to 4%). `_history_reset`: that frame the march's own
 *    values, bit for bit a record's first frame (as built). Each bound set
 *    from the measurement when version 2 is built, rounded up, named in
 *    the test with the measured number beside it, and recorded in item 55;
 *    seen failing first: (a) and (b) against today's rule.
 * 12. test_clouds_records: three scenes over a desc of two views: the third
 *    takes the least recently used record; all three in one list refused
 *    with RBT_RESULT_LIMIT naming max_views; one scene twice in a frame
 *    refused.
 * 13. test_clouds_writes_in_flight: a look and a map region changed while
 *    a frame is in flight: that frame unchanged, the next changed.
 * 14. test_frame_allocations with clouds drawn (two layers, a look and a
 *    map region written each frame): none of Rend's. The destroyed-handle
 *    tests (make_destroyed_tests.py) with the clouds destroyed; the misuse
 *    harness regenerated from abi.txt; rbtrend_contract_tests headless.
 * 15. test_clouds_measure (RABBITTUSTESTS_MEASURE, Release, submit to idle
 *    less the same frame with none, means of 50 after 10): 1920 x 1080 and
 *    2560 x 1440; a broken deck from under; inside a deck; two layers; at
 *    32 and 64 steps, 4 and 6 light steps, 1 and 3 octaves. The estimate in
 *    section 7 replaced in RBTREND_TODO.md item 55.
 * 16. [ADDED in version 2] tests/rbtrendhistory_tests.cpp, headless (the
 *    history module's own tests, rabbittus): the clouds' order covers each
 *    of a 4 x 4 block's sixteen target texels once in sixteen frames and
 *    each history texel's four in its four visits, the history texel of
 *    frames 0 to 3 being {0, 3, 1, 2}; each history texel's step offsets
 *    over its first sixteen visits are J's sixteen values (before the
 *    per-texel rotation); the weight is 1, 1/2, 1/3 then the floor by visit,
 *    and the fog's share by its frames as built. Seen failing first against
 *    today's sequence (both of "Version 2"'s faults).
 * The build: on Metal under its validation, Debug and Release, -asan and
 * -tsan; check.sh's abi, struct-bases, plain-data, doxygen, caller-names,
 * umbrella; D3D12 and Vulkan written, run in their sessions (card X25).
 *
 * ===========================================================================
 * 12. Records when built
 * ===========================================================================
 *
 * RBTREND_TODO.md item 55: stage 1 built, where, the measured cost against
 * section 7's estimate and the budget, the tolerances recorded, what is
 * owed (D3D12, Vulkan; stages 2 and 3). Version 2 when built: test 11's
 * three numbers and its turn against section 4's estimates, the bounds
 * named, the reconstruct's cost, the constants alpha_min and gamma as set;
 * rbtrendclouds.h's comment (section 8); the claude_made_decisions.md
 * entry for any constant set otherwise than recommended. rabbittus/CLAUDE.md's module map
 * row for Rend (clouds) and rbtrend.h's count of headers. The ABI snapshot
 * (additions). The caller's half's record in its own TODO.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card A57's Rend half; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. The history: a, a record a view inside the object, made at its
 *     creation for the desc's largest target, by the scene's handle as card
 *     A67's records (recommended: the reshaped Q3a's "its ... history", no
 *     allocation in a frame); b, card A39's caller-owned per-view handle,
 *     drafted here as its first caller; c, textures the caller makes and
 *     names each frame.
 * Q2. The history's resolution, the march a quarter each way: a, half each
 *     way, four frames to fill (recommended: 23.7 MiB a view at 1440p, the
 *     flight's fast turns refilled in four frames); b, full, Horizon's, 16
 *     frames, about 4 times the memory; c, a quarter, no history but a
 *     blend over frames (softer).
 * Q3. The volumes and maps: a, the object's own, written by two calls
 *     (recommended: the decided object holds them; no view of a caller's
 *     texture to check every frame); b, the caller's textures, named by
 *     views.
 * Q4. A shell's coverage: a, a 2D array of six layers a map read by hand
 *     with Math's face table and the layer's warp (recommended: one binding
 *     for shells and slabs, the CPU's definition exactly, no seamless-
 *     filtering difference); b, a cube view sampled by the GPU (PLAIN
 *     only).
 * Q5. Extinction: a, one channel, the albedo coloured (recommended: every
 *     engine compared; no dual-source blending); b, per channel, blended
 *     dual-source as A55's shells (coloured dust that reddens what is
 *     behind it).
 * Q6. A layer's lights: a, the scene's directional lights by index, at
 *     most RBTREND_CLOUDS_MAX_LAYER_LIGHTS, placed ones once item 58 is
 *     built (recommended: A55's shells' rule, Unreal's); b, point and spot
 *     lights too (lightning in a cloud, a city's glow), a later amendment.
 * Q7. Before stage 2, a shell layer seen from outside: a, drawn as from
 *     inside, its composition with A55's pass said on the command
 *     (recommended: a caller that never leaves its air loses nothing, and
 *     stage 2 changes the picture only); b, refused while the eye is
 *     outside the shell's top.
 *
 * Version 2's questions (card A70; Q1 to Q7 stay as approved)
 *
 * Q8. The marched texel's weight: a, 1 / (v + 1) by its visit, never below
 *     a floor of Rend's, a quarter, set from the measurement (recommended:
 *     the history is Rend's internals by the decided line; a quarter at a
 *     visit every four frames is Unreal's four-frame integration's order of
 *     memory, 16 frames, and averages seven marches); b, the caller's
 *     weight, a `_history_weight` on the command as LIT_FOG's (Unity's
 *     accumulation factor): [CHANGED] command 52's parameters, a float
 *     after `_target`, 72 to 76 bytes of the 120, earlier offsets unchanged,
 *     listed against abi.txt (0.3 untagged, no version raise); c, a floor
 *     of an eighth (15 marches, 32 frames' lag: smoother, slower to follow
 *     a sun or a weather).
 * Q9. The neighbourhood: a, a variance box over the march's 3 x 3, mean +-
 *     gamma sigma, gamma of Rend's (estimate 1 to 1.5, set from the
 *     measurement), the history clipped towards the mean, in the channels
 *     as stored (L and T), every texel's history before any blend
 *     (recommended: Salvi's and Karis's; tighter than min-max against one
 *     far march, loose enough that a settled average isn't pulled to one
 *     march; clouds are near grey, so YCoCg's separation of brightness from
 *     colour buys little and T has none); b, the min-max box clamped per
 *     channel, as built (measured: it holds a settled history near one
 *     march); c, a's box in YCoCg for L; d, none (Nubis's; Unreal's box off
 *     unless asked): a sun or weather change then lags the full 16 frames.
 * Q10. The step offset's per-texel rotation: a, interleaved gradient noise
 *     of the march texel (recommended: one line, no texture, no binding;
 *     without a rotation the 3 x 3 neighbourhood shares one offset and the
 *     clip returns the banding, pass 1.5); b, a tiling blue-noise texture,
 *     a binding in the record's set and 4 KiB to 16 KiB of texture (finer
 *     noise at the same count); c, none (each frame's banding coherent).
 * Q11. A refused (disoccluded) texel's age: a, none, its next visits at the
 *     record's weight (recommended: its start, its march texel's value, is
 *     a fair estimate; it reaches its own in about 16 frames); b, an age a
 *     texel, the history's depth made R32G32_FLOAT, 7.4 MB more a record at
 *     2560 x 1440, its next visit weighted 1.
 * Q12. Reading the history under a moving view: a, bilinear, as built
 *     (recommended until measured: test 11's turn says whether it softens
 *     past its bound); b, Catmull-Rom from five reads (Jimenez's, as TAA
 *     does), sharper under motion, more reads, ringing to clip.
 */

/* ========================================================================= */
/* render/rbtrendclouds.h                                  [ADDED: new file] */
/* ========================================================================= */

/*
 * @file rbtrendclouds.h
 * @brief Rend's volumetric clouds: layers of cloud bounded by spherical
 * shells or flat slabs, their density Rabbittus Math's
 * (math/rbtmathclouds.h), marched against the scene's depth at a quarter of
 * the target's resolution and filled out by each view's own history, lit
 * through an atmosphere or by the scene's lights alone, composited with the
 * scene's air in front.
 *
 * (The file's opening comment: what an rbtrend_clouds holds; the frame's
 * order; "Units and light" as rbtrendatmosphere.h's (lengths the scene's
 * unit, a light's illuminance pi times its radiance); "Inside and outside"
 * (section 9's stage 2 note); "Out of scope": shadows on the ground (stage
 * 3), point and spot lights, a cloud's own motion in the history
 * (reprojection takes the clouds as still between frames; wind moves them
 * slowly against a frame); "@par Threads": one object for the rule.)
 */

/** The most layers one RBTREND_COMMAND_LIT_CLOUDS marches. */
#define RBTREND_CLOUDS_MAX_LAYERS 4u
/* [ADDED] Why: the layers' block's array (section 6) and the segments' sort
 * in a shader's registers; "several layers a frame": a low deck, a high
 * one, a fog bank, one more. */

/** The most looks an object holds (RBTREND_CLOUDS_DESC::max_looks). */
#define RBTREND_CLOUDS_MAX_LOOKS 16u
/* [ADDED] Why: the looks' block's size bound. */

/** The most coverage maps an object holds. */
#define RBTREND_CLOUDS_MAX_COVERAGE_MAPS 8u
/* [ADDED] Why: the maps' array is six layers a map; 48 layers bounds it. */

/** The most views an object keeps a history for. */
#define RBTREND_CLOUDS_MAX_VIEWS 16u
/* [ADDED] Why: records are found by a scan of the scene handles, as A67's. */

/** The most samples a texel's march takes. */
#define RBTREND_CLOUDS_MAX_STEPS 256u
/** The most samples a light's march takes from a sample. */
#define RBTREND_CLOUDS_MAX_LIGHT_STEPS 16u
/** The most lights a layer names: the size of RBTREND_CLOUD_LAYER::_lights. */
#define RBTREND_CLOUDS_MAX_LAYER_LIGHTS 2u
/** The most octaves of multiple scattering a look takes. */
#define RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES 4u
/* [ADDED] Why each: the checks of the command and the look, and the frozen
 * array's size (lights), as the shells' RBTREND_LIT_ATMOSPHERE_MAX_SHELL_*. */

/** @brief Opaque handle to clouds: noise volumes, coverage maps, looks, and a
 * record a view drawn. */
typedef struct rbtrend_clouds_t *rbtrend_clouds;
/* [ADDED] Why: the decided "rbtrend_clouds, an object of its own". */

/**
 * @brief How clouds are made: every size and count, each texture made
 * once, here. No field has a default.
 */
typedef struct RBTREND_CLOUDS_DESC {
	/** RBT_STRUCT_TYPE_REND_CLOUDS_DESC, and its chain, which takes nothing (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** The base volume's side (Math's PERLIN_WORLEY), 1 to
	 * RBTMATH_NOISE_VOLUME_MAX_SIZE (Nubis's 128). */
	uint32_t base_noise_size;
	/** The detail volume's side (Math's WORLEY), 1 to
	 * RBTMATH_NOISE_VOLUME_MAX_SIZE (Nubis's 32). */
	uint32_t detail_noise_size;
	/** Each coverage map's side, 1 to RBTMATH_CLOUD_MAX_COVERAGE_SIZE. */
	uint32_t coverage_size;
	/** How many coverage maps, 1 to RBTREND_CLOUDS_MAX_COVERAGE_MAPS; each
	 * six faces (a slab's reads its first). */
	uint32_t coverage_maps;
	/** How many look slots, 1 to RBTREND_CLOUDS_MAX_LOOKS. */
	uint32_t max_looks;
	/** How many views it keeps a history for, 1 to
	 * RBTREND_CLOUDS_MAX_VIEWS: one for each scene drawn in a frame. */
	uint32_t max_views;
	/** The largest target a view draws into, in texels, 1 to 16384 each:
	 * every record's textures are made at this size. */
	uint32_t max_target_width;
	/** See #max_target_width. */
	uint32_t max_target_height;
} RBTREND_CLOUDS_DESC;
/* [ADDED] Why: every allocation at creation, from the caller's numbers
 * (pass 4.1). RBT_STRUCT_TYPE_REND_CLOUDS_DESC (5041, or the next free). */

/**
 * @brief Creates clouds: their volumes and maps (undefined until written),
 * their looks (none set: a layer naming an unset look is refused at the
 * compile), and @p desc's view records with their sets.
 *
 * Memory: the volumes (base^3 + detail^3, four bytes a texel), the maps
 * (coverage_maps x 6 x size^2 x 4 bytes), the looks' block (max_looks x
 * 256 bytes a frame in flight), and a record a view: (W/4 x H/4) x 12 +
 * 2 x (W/2 x H/2) x 12 bytes at the largest target W x H, and 1 KiB a
 * frame in flight (23.7 MiB at 2560 x 1440). Nothing more is made later.
 *
 * @param[in]  instance   Instance the clouds are of.
 * @param[in]  desc       Their sizes. Only read during the call.
 * @param[out] out_clouds Receives them; NULL on failure.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL or
 *         destroyed instance, a NULL desc or out_clouds, or a field out of
 *         its range, each its own check and message; the struct codes for
 *         `desc->base`; RBT_RESULT_OUT_OF_MEMORY; or
 *         RBT_RESULT_REND_GPU_FAILURE if a texture or set can't be made.
 */
RBT_RESULT rbtrend_clouds_create(
	rbtrend_instance instance,
	const RBTREND_CLOUDS_DESC *desc,
	rbtrend_clouds *out_clouds,
	RBTREND_ERROR *out_error);
/* [ADDED] */

/**
 * @brief Destroys clouds once the GPU is past them (rbtrendcontext.h,
 * "Destroying"). No list may name them afterwards. NULL does nothing.
 *
 * @param[in] clouds The clouds.
 */
void rbtrend_clouds_destroy(rbtrend_clouds clouds);
/* [ADDED] */

/**
 * @brief Writes slices of a noise volume: bytes rbtmath_noise_volume_bake()
 * made, of the volume's kind and side. Recorded with the frame's batch
 * and staged, as rbtrend_texture3d_update(); the next submission carries
 * it, before any LIT_CLOUDS of that submission reads the volume.
 *
 * @param[in] clouds      The clouds.
 * @param[in] queue       Queue the copy is submitted on; it runs copies
 *                        between a buffer and an image. Never NULL.
 * @param[in] kind        RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY for the
 *                        base, _WORLEY for the detail.
 * @param[in] first_slice Below the volume's side.
 * @param[in] slice_count 1 to the side - @p first_slice.
 * @param[in] data        side x side x slice_count x 4 bytes. Only read
 *                        during the call.
 * @param[in] data_size   The bytes at @p data: exactly that.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument, a
 *         kind not one of the two, a slice range or size out of range, each
 *         its own check; otherwise as rbtrend_texture3d_update(). Nothing
 *         changes on failure.
 */
RBT_RESULT rbtrend_clouds_write_noise(
	rbtrend_clouds clouds,
	crgpu_command_queue queue,
	RBTMATH_NOISE_VOLUME_KIND kind,
	uint32_t first_slice,
	uint32_t slice_count,
	const void *data,
	uint64_t data_size,
	RBTREND_ERROR *out_error);
/* [ADDED] Why: the decided "noise ... baked on the caller's jobs and
 * uploaded", into the object's own volumes (Q3). */

/**
 * @brief Writes a region of one face of a coverage map: four bytes a texel
 * (RBTMATH_CLOUD_TEXELS::coverage's meaning: R coverage, G type, B a
 * density factor, A unread), rows of @p width, tightly packed. Recorded
 * and staged as rbtrend_clouds_write_noise(). A frame in flight reads
 * the map as it was; the next submission's frame the region written.
 *
 * @param[in] clouds     The clouds.
 * @param[in] queue      As rbtrend_clouds_write_noise().
 * @param[in] map        Below RBTREND_CLOUDS_DESC::coverage_maps.
 * @param[in] face       0 to 5, in the order +X, -X, +Y, -Y, +Z, -Z (a
 *                       shell's); a slab reads face 0.
 * @param[in] x          The region's left, in texels.
 * @param[in] y          Its top.
 * @param[in] width      Its width, 1 to the side - @p x.
 * @param[in] height     Its height, 1 to the side - @p y.
 * @param[in] data       width x height x 4 bytes. Only read during the call.
 * @param[in] data_size  Exactly that.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return As rbtrend_clouds_write_noise(), each argument its own check.
 */
RBT_RESULT rbtrend_clouds_write_coverage(
	rbtrend_clouds clouds,
	crgpu_command_queue queue,
	uint32_t map,
	uint32_t face,
	uint32_t x,
	uint32_t y,
	uint32_t width,
	uint32_t height,
	const void *data,
	uint64_t data_size,
	RBTREND_ERROR *out_error);
/* [ADDED] Why: a weather's map changes a region at a time; the caller's
 * weather writes it, Rend draws it. */

/**
 * @brief A look: the density's shape (Rabbittus Math's) and how the cloud
 * takes light. No field has a default.
 */
typedef struct RBTREND_CLOUD_LOOK {
	/** RBT_STRUCT_TYPE_REND_CLOUD_LOOK, and its chain, which takes nothing. */
	RBT_STRUCT_BASE base;
	/** The density's shape: each field in its range (math/rbtmathclouds.h). */
	RBTMATH_CLOUD_SHAPE shape;
	/** The extinction at density 1, per scene unit: finite, above 0 (a
	 * cumulus about 0.05 per metre). */
	float extinction;
	/** The share of extinction that scatters, three channels: each finite, 0
	 * to 1 (water clouds about 0.99; dust lower, tinted). */
	float albedo[3];
	/** The forward Henyey-Greenstein lobe's g: finite, 0 to 0.999. */
	float phase_forward;
	/** The backward lobe's g: finite, -0.999 to 0. */
	float phase_backward;
	/** The forward lobe's weight, the backward's 1 minus it: finite, 0 to 1. */
	float phase_blend;
	/** Octaves of multiple scattering (Wrenninge's), 1 (single scattering)
	 * to RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES. */
	uint32_t scattering_octaves;
	/** Each octave's extinction over the one before (a): finite, 0 to 1.
	 * Read with more than one octave, else must be 0. */
	float octave_extinction;
	/** Each octave's light over the one before (b): finite, 0 to 1, at most
	 * #octave_extinction (energy kept). Read as #octave_extinction. */
	float octave_scattering;
	/** Each octave's phase g over the one before (c): finite, 0 to 1. Read
	 * as #octave_extinction. */
	float octave_phase;
	/** The scene's ambient light's share: finite, at least 0 (1 as a lit
	 * surface takes it). */
	float ambient_scale;
} RBTREND_CLOUD_LOOK;
/* [ADDED] Why: "its look entirely the caller's data": the shape is
 * Math's, the light's numbers Rend's; based, since a light's numbers may
 * grow. RBT_STRUCT_TYPE_REND_CLOUD_LOOK (5042, or the next free). */

/**
 * @brief Sets look @p index. Copied; a frame in flight draws the look it
 * had, the next submission's frame this one.
 *
 * @param[in]  clouds    The clouds.
 * @param[in]  index     Below RBTREND_CLOUDS_DESC::max_looks.
 * @param[in]  look      The look: each field in its range. Only read
 *                       during the call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument,
 *         an index out of range or a field out of its range (Math's shape
 *         checked by Math's rules), each its own check and message; the
 *         struct codes for `look->base`. Nothing changes on failure.
 */
RBT_RESULT rbtrend_clouds_set_look(
	rbtrend_clouds clouds,
	uint32_t index,
	const RBTREND_CLOUD_LOOK *look,
	RBTREND_ERROR *out_error);
/* [ADDED] Why: looks change rarely (a weather's change of type), places
 * every frame: the look a slot, set when it changes; the layer a frame's
 * value naming it. */

/**
 * @brief One layer of a LIT_CLOUDS command: where it is this frame, its
 * look and map by slot, where its tiling sits, and what lights it.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_CLOUD_LAYER {
	/** Its look's slot (rbtrend_clouds_set_look()), set. */
	uint32_t _look;
	/** Its coverage map, below RBTREND_CLOUDS_DESC::coverage_maps. */
	uint32_t _coverage_map;
	/** Its bounds and its map's lie (math/rbtmathclouds.h), relative to
	 * #_origin. */
	RBTMATH_CLOUD_BOUNDS _bounds;
	/** The bounds' origin in the scene's space this frame (a shell's centre,
	 * a point of a slab's base plane): finite. */
	float _origin[3];
	/** The layer's own frame this frame: its x, y and z axes in the scene's
	 * space, three rows, each finite, orthonormal within 1e-4. A point p
	 * of the scene is read as Math's point axes x (p - #_origin): its
	 * bounds, map and noise turn with it (a world's rotation). */
	float _axes[9];
	/** Where the tiling sits at #_origin this frame (wind, the caller's
	 * place in the tiling): each in its range. */
	RBTMATH_CLOUD_OFFSETS _offsets;
	/** How many of #_lights light it: 0 to RBTREND_CLOUDS_MAX_LAYER_LIGHTS
	 * (0: ambient light alone). */
	uint32_t _light_count;
	/** Its lights, by their places in the scene's light list: each a
	 * directional light (a placed one once Rend has it). A point or spot
	 * light is refused. Entries past #_light_count are not read. */
	uint32_t _lights[RBTREND_CLOUDS_MAX_LAYER_LIGHTS];
	/** An atmosphere its lights reach it through (rbtrendatmosphere.h), of
	 * the scene's instance, outliving the list's submission; NULL for none
	 * (the lights as they are). */
	rbtrend_atmosphere _atmosphere;
	/** That atmosphere's centre in the scene's space this frame: finite.
	 * Not read with no #_atmosphere. */
	float _atmosphere_center[3];
} RBTREND_CLOUD_LAYER;
/* [ADDED] Why: "several layers drawn a frame ... an array sent each frame
 * as the atmosphere's shells are", each with its own bounds, look,
 * map, lights and atmosphere (several worlds' layers in one frame, a
 * slab under a round sky). */

/*
 * render/rbtrendcommands.h [ADDED]:
 *
 *	/ **
 *	 * Draws clouds over `_target`, outside a pass: the `_layers`
 *	 * (RBTREND_CLOUD_LAYER, rbtrendclouds.h) of `_clouds`, marched along
 *	 * each view ray of `_scene` from the eye to `_max_distance`, the surface
 *	 * `_depth` holds or the layers' end, whichever is nearest, through a
 *	 * quarter of the texels each way this frame, the rest filled out from
 *	 * the clouds' history of this scene (rbtrendclouds.h); each sample's
 *	 * density Rabbittus Math's (rbtmath_cloud_compute_density's definition),
 *	 * lit by its layer's lights through every layer towards them and,
 *	 * where the layer names one, its atmosphere's transmittance, and by
 *	 * the scene's ambient light. The result is put over `_target` with the
 *	 * scene atmosphere's air in front of it where the scene has an aerial
 *	 * volume: target x T + L'.
 *	 *
 *	 * `_scene` gives the view (its view-projection, which can be inverted,
 *	 * its eye, its target size at most the clouds' largest), its lights,
 *	 * ambient light, exposure and atmosphere; its output encoding linear.
 *	 * `_depth` is a SHADER_RESOURCE 2D view, DEPTH aspect, single-sampled,
 *	 * of the depth the surfaces were drawn with at the view's target
 *	 * size; `_target` a RENDER_TARGET 2D view of the picture, linear and
 *	 * times the exposure, R16G16B16A16_FLOAT, R32G32B32A32_FLOAT or
 *	 * R11G11B10_FLOAT at that size, left in COLOR_ATTACHMENT_OPTIMAL.
 *	 * `_layers` 0 to RBTREND_CLOUDS_MAX_LAYERS, copied at the send; 0 draws
 *	 * nothing and keeps the history. `_max_steps` 1 to
 *	 * RBTREND_CLOUDS_MAX_STEPS, `_light_steps` 0 (no light march: ambient
 *	 * and the light unshadowed by cloud) to RBTREND_CLOUDS_MAX_LIGHT_STEPS,
 *	 * `_max_distance` finite, above 0.
 *	 *
 *	 * `_space_shift` is this frame's origin of the scene's space in the
 *	 * space of the frame before (0 where the caller's space doesn't move;
 *	 * a caller drawing with the eye at the origin gives the eye's move),
 *	 * finite; `_history_reset` 1 after a cut, a teleport, or anything
 *	 * the history shouldn't carry, else 0 (any other value refused).
 *	 *
 *	 * Refused at the compile, each with its message: a layer's look unset,
 *	 * a map or light index out of range, a light not directional; a scene
 *	 * target larger than the clouds' (RBT_RESULT_LIMIT); no record free
 *	 * (RBT_RESULT_LIMIT, naming RBTREND_CLOUDS_DESC::max_views); the same
 *	 * scene a second time in a frame; no set free in the scene's records
 *	 * (as LIT_REFLECT).
 *	 *
 *	 * The frame's order: after the lit surfaces, LIT_REFLECT and
 *	 * LIT_ATMOSPHERE_SHELLS, before the transparent surfaces and the tone
 *	 * map. A shell layer seen from outside its top is drawn as from
 *	 * inside, with no air of its own world around it, until clouds take
 *	 * their place in LIT_ATMOSPHERE_SHELLS (RBTREND_TODO.md item 55's
 *	 * stage 2).
 *	 *
 *	 * It takes one set from `_scene`'s view-set records
 *	 * (RBTREND_LIT_SCENE_DESC::max_view_sets), over `_depth` and its
 *	 * layers' atmospheres' tables. It binds pipelines and sets of its own,
 *	 * as BLOOM does.
 *	 * /
 *	RBTREND_COMMAND_LIT_CLOUDS                  = 52,
 *
 *	struct {
 *		/ ** The scene: its view, lights, ambient light and atmosphere. * /
 *		rbtrend_lit_scene _scene;
 *		/ ** The clouds. * /
 *		rbtrend_clouds _clouds;
 *		/ ** The layers, `_layer_count`; copied at the send. * /
 *		const RBTREND_CLOUD_LAYER *_layers;
 *		/ ** How many: 0 to RBTREND_CLOUDS_MAX_LAYERS. * /
 *		uint32_t _layer_count;
 *		/ ** The most samples a texel's march takes. * /
 *		uint32_t _max_steps;
 *		/ ** The samples a light's march takes from each sample. * /
 *		uint32_t _light_steps;
 *		/ ** How far from the eye clouds are marched, in the scene's unit. * /
 *		float _max_distance;
 *		/ ** This frame's origin in the last frame's space. * /
 *		float _space_shift[3];
 *		/ ** 1 starts the history again; 0 keeps it. * /
 *		uint32_t _history_reset;
 *		/ ** The depth the surfaces were drawn with. * /
 *		rbtrend_texture_view _depth;
 *		/ ** The picture the clouds are drawn over. * /
 *		rbtrend_texture_view _target;
 *	} _lit_clouds; / **< RBTREND_COMMAND_LIT_CLOUDS. * /
 *
 * [ADDED] Why: the march, its history and its composition are one pass of
 * Rend's own over the picture against depth (as the shells'), with every
 * cost the caller's (steps, light steps, distance). 72 bytes of the 120.
 *
 * render/rbtrendlit.h, RBTREND_LIT_SCENE_DESC::max_view_sets [CHANGED, a
 * comment]: "RBTREND_COMMAND_LIT_SSR, _LIT_TRACE_REFLECTIONS, _LIT_REFLECT,
 * _LIT_ATMOSPHERE_SHELLS and _LIT_CLOUDS".
 *
 * rbtcore.h [ADDED]: RBT_STRUCT_TYPE_REND_CLOUDS_DESC and
 * RBT_STRUCT_TYPE_REND_CLOUD_LOOK, the next two free (5041 and 5042 as
 * written). No result code. No version raise while 0.3 is untagged.
 */
