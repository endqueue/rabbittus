/*
 * rbtrend_placed_light_draft.h - a directional light given a source place:
 * RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED, whose direction, light and
 * disc are worked out at each point it lights from where its source is,
 * keeping everything a directional light carries (its cascades, its traced
 * shadow, the sky's disc and colour, the atmosphere's suns and shells)
 * (RBTREND_TODO.md item 58; card L78 in DECISIONS.md, which names the
 * session that asked). Its caller's own half is drafted beside the
 * caller's headers.
 *
 * APPROVED 2026-11-18 with the user, every question as recommended.
 * DRAFT, 2026-11-18, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built yet. Not in any target, not
 * compilable. Written on the MacBook (M5 Pro) against Rabbittus as
 * committed 2026-11-17 (c79f7d6, 0.3, with card A55's stage 4 built), read
 * 2026-11-18: include/render/rbtrendlit.h (RBTREND_LIT_LIGHT_TYPE, lines
 * 870-878; RBTREND_LIT_SCENE_LIGHT, frozen, 884-924;
 * rbtrend_lit_scene_set_lights, 926-958; RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS,
 * 135; RBTREND_LIT_CASCADE_FIT::light_direction, 1203-1205; RBTREND_LIT_
 * ATMOSPHERE::sun_count, 1407-1410; RBTREND_LIT_ATMOSPHERE_SHELL::_lights,
 * 1513-1522), rbtrendcommands.h (LIT_SHADOW_MASK, 917-951; LIT_PATH_TRACE,
 * 974; LIT_ATMOSPHERE_SHELLS, 1170-1225), rbtrendatmosphere.h ("Units and
 * light"); src/render/rbtrendlit.cpp (rbtrend_lit_scene_directional_slot,
 * 1782-1787; rbtrend_lit_kind_fits, 1839-1850; rbtrend_lit_light_valid,
 * 2071-2109; rbtrend_lit_scene_set_lights, 2124-2183),
 * rbtrendlit_internal.hpp (rbtrend_lit_directional_block, 31-42),
 * rbtrendatmosphere.cpp (atmosphere_frame, 186-211;
 * rbtrend_lit_scene_atmosphere_block's suns, 288-295), rbtrendcommands.cpp
 * (rbtrend_shell_lights_ok, 737-756; the shell's prepare, 4908-4930;
 * LIT_SHADOW_MASK's slot, 4448), rbtrendraytracing.cpp (1521); the shaders
 * that read a directional light: rbtrend_lit_scene.slang (RbtrendLitDirectional,
 * 29-35), rbtrend_lit_surface.slang (directional_visibility, 193-215; the
 * light loop, 381-386), rbtrend_lit_atmosphere.slang
 * (lit_atmosphere_sun_transmittance, 51-63), rbtrend_lit_sky_frag.slang
 * (the discs, 44-58), rbtrend_rt_shadow_mask.slang (mask_visibility, 89-105),
 * rbtrend_rt_path_trace.slang (next-event, 443-463; the miss's discs,
 * 566-575), rbtrend_rt_reflections_comp.slang (shade_hit, 100-108),
 * rbtrend_atmosphere_shell_frag.slang; and rabbittustests'
 * tests/rbtrend_gpu_tests.c (test_atmosphere_shell_placed_light,
 * 21364-21393, pending). Read with the approved
 * rbtrend_atmosphere_space_draft.h (version 9: a shell's lights, its
 * section 3 and pass 12.1) and rbtrend_large_worlds_draft.h (version 2:
 * Rend fed a space whose origin is the eye; RBTREND_LIT_CASCADE_ANCHOR).
 * Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel/src/Hazel,
 * present): Scene/Scene.h's DirectionalLight (a direction, a radiance, an
 * intensity) and Scene/Components.h's DirectionalLightComponent (no place;
 * LightSize for PCSS): a fixed direction only, nothing to borrow beyond
 * that it has the same one kind Rend has today.
 *
 * ===========================================================================
 * Revision notes (the seven passes, 2026-11-18, each fixed before the next;
 * the lenses are the workspace's critique prompt's, as the task names
 * them)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: the traced shadow (LIT_SHADOW_MASK) and the path tracer cast a
 *    directional light's shadow ray to "infinity" (params.max_distance,
 *    rt_visible(..., 1e30, ...)). For a placed light a body beyond the
 *    source would shadow the near side of it. Changed: a placed light's
 *    shadow ray stops at the source's surface (d - R, section 2), Q3.
 * 2. Missing: the first version took the eye's values (what the sky-view,
 *    the aerial volume and the sky's disc read) at set_lights only. The
 *    eye moves with set_view, so a scene whose lights are set once would
 *    keep the first eye's direction. Changed: the eye's values are
 *    resolved whenever set_lights or set_view runs (section 2, "The eye's
 *    values").
 * 3. Missing: a point inside the source (d below R) had no rule; the
 *    inverse square went to infinity at the place. Changed: the distance
 *    is taken no less than R, so the light there is the source's surface's
 *    (section 2), which needs R above 0: a placed light's `_radius` must
 *    be above 0 (Q2).
 * 4. More than needed: a CPU helper (rbtrend_lit_placed_light_at()) that
 *    gave a placed light's direction and radiance at a point. Struck: its
 *    callers work in double from their own places (a caller far from its
 *    origin must not round to Rend's floats first), the rule is five lines
 *    in the header, and no caller asked. The shells' CPU arithmetic stays
 *    internal.
 * 5. More than needed: a new based struct for a placed light. Struck: the
 *    frozen RBTREND_LIT_SCENE_LIGHT already has a place, a range and a
 *    radius a directional light doesn't read; the type says which fields
 *    are read, as it does for point and spot lights (Q1).
 *
 * Pass 2 (industry standard).
 * 1. Unreal's DirectionalLight (Source Angle, Source Soft Angle, Atmosphere
 *    Sun Light Index), Unity HDRP's Directional Light (Angular Diameter,
 *    the Physically Based Sky's sun), Godot 4's DirectionalLight3D (Angular
 *    Distance) and Frostbite's ("Moving Frostbite to PBR", 2014: the sun as
 *    a disc of illuminance in lux) all take one fixed direction a frame,
 *    the disc a size in degrees. A placed light keeps that model: its
 *    `_radius` stays the disc's angular radius, seen from the reference
 *    distance; its `_radiance` stays what a face square to it reads, at
 *    that distance. Nothing new to learn for a caller who knows them.
 * 2. KSP lights far planets in a scaled-space pass from their star's
 *    direction per body; Elite Dangerous, Star Citizen and Space Engine
 *    light each body from its star in renderers of their own. Rend's rule
 *    (direction per pixel, the source's place in the scene's space) is the
 *    general form of those per-body hacks, at a per-pixel cost of a
 *    normalise and a division (section 7).
 * 3. Area lights (Unreal's Rect Light, HDRP's sphere and disc lights,
 *    Filament's sphere lights with the representative-point method) were
 *    considered and are not this: they have a range, cube or spot shadows
 *    and no sun's role (card L78's "not a sphere light").
 *
 * Pass 3 (the C API).
 * 1. The type is appended to the light types (an anonymous enum on
 *    `typedef uint32_t RBTREND_LIT_LIGHT_TYPE`, its neighbours' form) at 3;
 *    0 stays POINT, as today: no "not chosen" zero is added to an existing
 *    enum (a break), as rbtrendlit.h's neighbours have none.
 * 2. Every check is its own refusal with its own message, in the struct's
 *    order (section 6); the existing checks that named "directional"
 *    (`_direction` of 0, `_radius` above 0.5) say which kind they apply to.
 * 3. "Directional light" is defined once on the type (a DIRECTIONAL or a
 *    DIRECTIONAL_PLACED light), so the 43 places in the render headers
 *    that say "directional light" keep their words and gain the placed
 *    kind; the places whose meaning differs for it are changed one by one
 *    below ([CHANGED] comments).
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. The scene's directional block (rbtrendlit_internal.hpp,
 *    rbtrend_lit_directional_block) gains two float4s a light, 128 bytes
 *    for the four: internal, staged with the block as today; no allocation
 *    anywhere, set_lights and set_view write the block in place.
 * 2. The per-pixel rule is one inline function every reader calls
 *    (directional_at(), in rbtrend_lit_scene.slang), its fixed-direction
 *    branch returning the stored values bit for bit, so no shader keeps a
 *    second copy of the rule and an ordinary light's pictures don't move.
 * 3. A placed light is not culled per tile (as a directional light isn't):
 *    it reaches every texel.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Threads: the scene's rule, unchanged (rbtrendlit.h, "Threads").
 * 2. The CPU values (the eye's, the shells' centres) are worked out in
 *    double from the float inputs and rounded once, so they are the same
 *    bits on every machine with contraction off (card A63); the GPU's
 *    per-pixel values are a picture's, not reproducible across vendors,
 *    and nothing reads them back.
 * 3. Errors: set_lights' RBT_RESULT and RBTREND_ERROR; the shell's
 *    refusal of a place inside its top sphere through the command list's
 *    report, at the send and again at the compile (the A55 draft named the
 *    compile; the send's check is the same function's, so a list that sends
 *    a bad shell hears it first), naming the shell, its slot and the light.
 *
 * Pass 6 (the code as built, re-read 2026-11-18).
 * 1. rbtrend_lit_light_valid (rbtrendlit.cpp 2073) refuses any type above
 *    DIRECTIONAL (`l._type > RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL`): changed
 *    to above DIRECTIONAL_PLACED. Its `_direction` check (2091) applies to
 *    every type but POINT: a placed light doesn't read `_direction`, so the
 *    check becomes "spot or directional", its message saying so. Its
 *    `_range` check (2093) applies to every type but DIRECTIONAL: it
 *    already covers a placed light, its message ("a point or spot light")
 *    reworded. Its `_radius` and `_sky_disc` checks (2101-2107) test
 *    `== DIRECTIONAL` and `!= DIRECTIONAL`: each split for the placed kind.
 * 2. rbtrend_lit_scene_set_lights (2139, 2157) counts and fills by
 *    `== RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL`; rbtrend_lit_scene_directional_slot
 *    (1785) and rbtrend_lit_kind_fits (1843) the same. Each takes both
 *    types (an internal `rbtrend_lit_type_directional()`), or a placed
 *    light would count as a point light and be refused by LIT_SHADOW_MASK
 *    (rbtrendcommands.cpp 4448, rbtrendraytracing.cpp 1521) and by every
 *    shell (4919).
 * 3. rbtrend_shell_lights_ok (rbtrendcommands.cpp 737-756) refuses POINT
 *    and SPOT by name and lets anything else through: today that is the
 *    one other type; it gains the placed light's place test, and the
 *    shell's prepare (4918-4930) its centre's direction and illuminance in
 *    place of `direction_versine` (which for a placed light holds the
 *    eye's, section 2).
 * 4. atmosphere_frame (rbtrendatmosphere.cpp 197-202) and
 *    rbtrend_lit_scene_atmosphere_block (288-295) read
 *    `directional[k].direction_versine` and `radiance_disc`: with the eye's
 *    values written there for a placed light, both read the right thing
 *    with no change (the sky-view, the aerial volume and the environment
 *    fill are from the eye). Said, not changed.
 * 5. rbtrend_lit_sky_frag.slang (44-58) draws each disc at
 *    `direction_versine` from the eye: right with the eye's values, the
 *    disc's radiance per texel (pi radiance over its solid angle) unchanged
 *    with distance, as a real star's surface brightness is. Said.
 * 6. rbtrend_rt_reflections_comp.slang (106-108) shades a reflected hit by
 *    each directional light's stored direction: a placed light there reads
 *    directional_at() at the hit. rbtrend_rt_path_trace.slang's next-event
 *    (443-463) the same, its ray to d - R; its miss (566-575) tests the
 *    disc from the bounce's origin.
 * 7. The pending test (rbtrend_gpu_tests.c 21364-21393) prints "pending
 *    (RBT_RESULT_NOT_IMPLEMENTED)": it becomes the test of section 9's
 *    T10, its point and spot refusals kept.
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Every test named with what it holds and its tolerance (section 9);
 *    every existing lit, atmosphere and traced test held to its own
 *    figures unchanged (an ordinary light's branch is the stored values).
 * 2. No format: Rend saves nothing. ABI: one enumerator added, no layout
 *    change; set_lights accepts what it refused (type 3). Listed by
 *    ./check.sh --update-abi when built. No version raise while 0.3 is
 *    untagged (card X2).
 * 3. The build's order (section 10): this, then its caller's half; Metal
 *    built and run, D3D12 and Vulkan written (no backend branch), run in
 *    their sessions (card X25). The records it writes when built: section
 *    10.
 * 4. ./check.sh caller-names, run at the end: the caller's half named by
 *    its path; now "beside the caller's headers", the meaning kept.
 *
 * ===========================================================================
 * 1. Why
 * ===========================================================================
 *
 * A directional light lights every point of a scene from one direction.
 * That holds while the light's source is far from the scene against the
 * scene's extent: the earth's sun across a 10 km level differs by 7e-8
 * radians. It fails where the source is near against the extent: a
 * system's star 1e7 m from a moon 2e6 m across lights the moon's two
 * limbs 0.2 radians (11 degrees) apart, and two worlds either side of it
 * from opposite directions, which one direction can't say. Today a caller
 * re-aims the light each frame at the world nearest the eye; every other
 * world in view is lit from the wrong side, and A55's shells, which each
 * take their lights at their own centres, need a light whose direction is
 * a function of where it is read.
 *
 * A placed directional light is that function: a source place (in the
 * scene's space), a reference distance at which its `_radiance` and
 * `_radius` are given, and the rule below. Everything a directional light
 * does it still does; nothing a point light does (a range, culling per
 * tile, cube shadows) is taken on.
 *
 * Who else uses it: any caller drawing bodies near a light against their
 * sizes. A space game's stars; a planetarium or an orrery; a CAD
 * application's sun study of a site seen from far enough out that the
 * sun's parallax matters (it doesn't on earth: such a caller keeps an
 * ordinary light); a film's set lit by a "sun" lamp at a finite distance,
 * whose shadows fan out as a real lamp's do while keeping cascades.
 *
 * ===========================================================================
 * 2. The rule
 * ===========================================================================
 *
 * A light of type RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED reads, of its
 * frozen RBTREND_LIT_SCENE_LIGHT:
 * - `_position`: its source place S, in the scene's space;
 * - `_range`: the reference distance D, above 0;
 * - `_radiance`: what a white matte face square to it reads at distance D
 *   from S (a directional light's meaning, at D);
 * - `_radius`: its disc's angular radius seen from distance D, above 0 and
 *   at most 0.5;
 * - `_sky_disc`: whether the sky shader draws its disc, as a directional
 *   light's.
 * It doesn't read `_direction`, `_inner_angle` or `_outer_angle` (checked
 * finite, as for every type, and otherwise unread).
 *
 * The source's radius R = D sin(`_radius`), in the scene's unit. At a point
 * P, with v = S - P and d = |v|, d' = max(d, R):
 * - its direction from P towards the light: v / d (d above 0), else the
 *   eye's direction (below);
 * - its radiance there: `_radiance` x (D / d')^2;
 * - its disc's angular radius there: min(asin(R / d'), 0.5), as the
 *   versine 1 - cos, worked out as (R/d')^2 / (1 + sqrt(1 - (R/d')^2)) so
 *   a small disc keeps its digits;
 * - a traced shadow ray from P towards it stops at d - R (the source's
 *   surface), not at the ray's limit: a body beyond the source doesn't
 *   shadow it (Q3).
 * A point inside the source (d below R) reads the light at its surface
 * (Q2). For a placed light far from every point against its size, the
 * rule is a directional light's at each point.
 *
 * WHERE IT IS WORKED OUT.
 * - Per pixel, at the point lit: the lit surface's direct light (its
 *   direction, radiance and visibility), the transmittance from the point
 *   towards it through the scene's atmosphere (lit_atmosphere_sun_
 *   transmittance()), LIT_SHADOW_MASK's rays, LIT_PATH_TRACE's next-event
 *   rays and its misses' discs (from each bounce's origin), and
 *   LIT_TRACE_REFLECTIONS' hits. One inline function, directional_at(),
 *   in rbtrend_lit_scene.slang, which every one of them calls; its branch
 *   for an ordinary directional light returns the stored values bit for
 *   bit.
 * - At the eye (the scene's `eye_position`, RBTREND_LIT_VIEW): what is
 *   seen from the eye. The sky's disc, the sky-view table, the aerial
 *   volume, the environment's fill from the sky-view, and the atmosphere's
 *   frame (the sky-view's seam opposite the first sun). These read the
 *   scene's stored direction and radiance (`direction_versine`,
 *   `radiance_disc`); for a placed light Rend writes the rule's values at
 *   the eye there (THE EYE'S VALUES, below), so none of them changes.
 * - At each shell's centre (LIT_ATMOSPHERE_SHELLS): its direction and
 *   illuminance (pi times its radiance), on the CPU at the compile, in
 *   double from the floats, as A55's draft decided (its section 3). A
 *   placed light whose source is inside or on a shell's top sphere is
 *   refused for that shell: no one direction lights it from its centre.
 * - Along the caller's direction: cascades. RBTREND_LIT_CASCADE_FIT takes
 *   a light direction from its caller and no scene; for a placed light the
 *   caller passes one (its own direction from the view's eye towards the
 *   source, or one held while the light turns, card A60). The shadow's
 *   projection is fixed along it while the shading direction varies per
 *   pixel: inside a cascade of radius r the two differ by at most r / d
 *   radians (5e-5 for a 500 m cascade 1e7 m from its source), far below a
 *   texel's slope bias.
 *
 * THE EYE'S VALUES. For a placed light, the scene's stored direction,
 * versine and radiance (the internal `direction_versine` and
 * `radiance_disc`) are the rule's at the scene's eye. They are resolved
 * whenever rbtrend_lit_scene_set_lights() or rbtrend_lit_scene_set_view()
 * changes either (in double from the floats, rounded once), so a scene
 * whose lights are set once and whose eye moves each frame stays right.
 * A placed light whose source is at the eye (d 0) takes +Y of the scene's
 * space there, an arbitrary choice for a picture that has none (the eye
 * inside its source).
 *
 * ===========================================================================
 * 3. Precision
 * ===========================================================================
 *
 * A caller far from its origin feeds Rend a space whose origin is the eye
 * (card L66): S - eye is rounded to float once, by the caller, from its
 * own precision. A float's relative step is 6e-8, so a source 1.5e11 m off
 * is placed within 9 km, and its direction from any point within 6e-8
 * radians: below a texel at any field of view. d^2 overflows a float past
 * 1.8e19 m, so the shaders take D / d' first and square that (never d^2).
 * In the shaders v = S - P loses nothing new: P is within the view's
 * extent of the eye, and S's own rounding is the caller's.
 *
 * ===========================================================================
 * 4. Item 58's open questions, answered
 * ===========================================================================
 *
 * 1. A placed light counts among RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS (4)
 *    and in an atmosphere's `sun_count` (the first `sun_count` directional
 *    lights of the list, of either kind, in its order): it is a
 *    directional light, its slot one of the four, its shadow one of their
 *    kinds.
 * 2. The tables' sun direction: the transmittance and multiple-scattering
 *    tables don't depend on a sun. The sky-view, the aerial volume, the
 *    environment's fill and the atmosphere's frame take the eye's values;
 *    a surface's transmittance takes the point's; a shell takes its
 *    centre's.
 * 3. The reference distance: `_radiance` and `_radius` are given at D; the
 *    light falls with the inverse square from D, brighter nearer; a point
 *    nearer than the source's radius R reads it at R. Nothing is refused
 *    for being nearer than D.
 * 4. A place inside a shell's top sphere: refused for that shell, at the
 *    send and the compile (A55's draft decided the compile; the send's
 *    check is the same function's, rbtrend_shell_lights_ok()).
 * 5. The cascades' direction: the caller's (section 2). Recommended for
 *    the caller: its direction from the view's eye, held until it turns
 *    a set angle (card A60, decided a for its first caller's targets): a parallax
 *    crawl of (the eye's motion) / d radians becomes an occasional step.
 *    Fitting them along the direction from a fixed place instead would be
 *    off at the eye by (that place's distance from the eye) / d.
 *
 * ===========================================================================
 * 5. What was compared
 * ===========================================================================
 *
 * - Unreal Engine 5 (DirectionalLight: Source Angle, Source Soft Angle;
 *   SkyAtmosphere's two atmosphere lights by index): one direction a
 *   frame; a space project re-aims it per frame. No placed kind.
 * - Unity HDRP (Directional Light's Angular Diameter; Physically Based
 *   Sky's one planet): one direction; the sun disc from it.
 * - Godot 4 (DirectionalLight3D, Angular Distance; PhysicalSkyMaterial):
 *   one direction.
 * - Frostbite ("Moving Frostbite to PBR", Lagarde and de Rousiers, 2014):
 *   the sun as a disc of illuminance, one direction, the disc's solid
 *   angle as Rend's.
 * - KSP: a scaled-space pass lights far planets from their star; the near
 *   world's light re-aimed. Elite Dangerous, Star Citizen, Space Engine:
 *   each body lit from its star in custom renderers (public talks and
 *   postmortems, read as remembered: no source code).
 * - Area-light practice (Filament's and Unreal's sphere lights, Karis's
 *   representative point, "Real Shading in Unreal Engine 4", 2013): a
 *   sphere light is what a placed light is not (a range, tile culling, a
 *   cube shadow); its "closest point on the sphere" is what the clamp at R
 *   does for a point inside the source.
 * - Hazel (present): one fixed-direction directional light, no place, no
 *   disc (LightSize is PCSS's). Nothing to measure Rend against beyond it.
 *
 * ===========================================================================
 * 6. ABI, versions, allocation, threads, determinism, errors
 * ===========================================================================
 *
 * - ABI: RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED (3) added; no struct's
 *   layout changes (RBTREND_LIT_SCENE_LIGHT is frozen and read differently
 *   by type, as point and spot are); set_lights accepts type 3, which it
 *   refused. Comments [CHANGED] as below. An addition, no break; listed by
 *   ./check.sh --update-abi when built. No version raise while Rabbittus'
 *   0.3 is untagged (card X2).
 * - Internal: the scene's block's directional entries grow by 32 bytes
 *   (two float4s), mirrored in rbtrend_lit_scene.slang and checked against
 *   reflection as every member is. Set 0's layout is unchanged (no
 *   binding added); a caller's own vertex shader declares nothing there.
 * - Allocation: none, anywhere (block writes in place; the shell's values
 *   in its push constants as today). test_frame_allocations covers a
 *   frame with a placed light, a shell lit by it and a traced mask.
 * - Threads: rbtrendlit.h's rule, unchanged.
 * - Determinism: the CPU's values in double, rounded once (card A63's
 *   contraction off); the GPU's per pixel a picture's, never read back.
 * - Errors, in set_lights' order for each light (by its index), each its
 *   own check and message: a type that is not an RBTREND_LIT_LIGHT_TYPE;
 *   `_position`, `_direction`, `_radiance`, `_range`, `_inner_angle`,
 *   `_outer_angle`, `_radius` not finite (each its own); a negative
 *   radiance; a `_direction` of 0 on a spot or directional light (not a
 *   placed one); a `_range` not above 0 on a point, spot or placed light;
 *   the spot's angles; a directional light's `_radius` negative or above
 *   0.5; a placed light's `_radius` not above 0, or above 0.5; a point or
 *   spot light's `_radius` not 0; `_sky_disc` on a point or spot light.
 *   Then the counts (RBT_RESULT_LIMIT): point and spot past 128, directional
 *   of both kinds past 4.
 *
 * ===========================================================================
 * 7. Costs, with their method
 * ===========================================================================
 *
 * Estimated by counting operations, to be measured when built (Release,
 * the M5 Pro, no validation, submit to idle, means of 50 after 10, a
 * full-screen lit face at 1440p with one ordinary directional light, then
 * the same face with one placed light; the difference is the cost):
 * - per pixel per placed light, about 20 ALU operations (a subtraction,
 *   a dot, an rsqrt, a max, a division and a square, the versine's
 *   sqrt where a soft or traced shadow reads it): 3.7 million pixels x
 *   20 = 74 million operations, under 0.01 ms on a GPU of several
 *   TFLOPS. The branch is uniform (a light's kind is the same for every
 *   pixel), so no divergence.
 * - per frame on the CPU: the eye's values, four lights at most, a few
 *   dozen double operations at each set_lights or set_view; each shell's
 *   centre, the same per light per shell at the compile.
 * - memory: 128 bytes more in each scene's block.
 *
 * ===========================================================================
 * 8. Shapes not taken
 * ===========================================================================
 *
 * - A sphere light (a point light with a radius): card L78's "not a sphere
 *   light"; a range, tile culling, cube shadows, no sun's role.
 * - A new based struct for a placed light's values (Q1 b): the frozen light
 *   already holds what it needs.
 * - The direction worked out per draw or per instance (the caller's
 *   instance's centre): a body's two limbs still 11 degrees apart in
 *   section 1's example; per pixel costs the same order as per draw.
 * - A CPU helper giving a placed light at a point (pass 1.4).
 *
 * ===========================================================================
 * 9. Tests (rabbittustests, Metal's validation with warnings fatal, Debug
 * and Release; rbtrend_contract_tests headless; each seen failing first)
 * ===========================================================================
 *
 * T1. rbtrend_contract_tests: set_lights takes a placed light; refuses,
 *     each with its message naming the light's index: type 4; `_range` 0
 *     and -1; `_radius` 0, -0.1 and 0.6; `_position` with a NaN in each
 *     axis; five directional lights of both kinds (RBT_RESULT_LIMIT); a
 *     placed light's `_direction` of 0 accepted (unread). A cascades and a
 *     mask shadow set on a placed light kept when the list is sent again;
 *     the light changed to a point light: its shadow gone.
 * T2. test_lit_placed_matches_directional: a face lit by a placed light
 *     (D = 10, the source 40 off the face's centre) against the same face
 *     lit by an ordinary light whose direction is from the face's centre
 *     and whose radiance is scaled by (10/40)^2: the centre texel within
 *     1/1024 in a float target.
 * T3. test_lit_placed_sides: two faces either side of a placed light, each
 *     facing away from the other: each lit on its own side, the back of
 *     each dark; across one face 4 units wide 40 off the source, the light's
 *     direction turning by atan(2/40) each side (the texels' diffuse
 *     against cos of the computed angle within 1/512).
 * T4. test_lit_placed_inverse_square: faces at D, 2D and 4D read 1, 1/4
 *     and 1/16 of the light, within 1/1024 (float target).
 * T5. test_lit_placed_inside: a face inside the source (d below R) and at
 *     it: finite, the same reading (the clamp), no NaN or infinity in the
 *     target.
 * T6. test_lit_placed_sky: the sky shader's disc for a placed light: its
 *     centre at the direction from the eye, within half a texel; its
 *     angular size the rule's at the eye's distance; the disc's texel the
 *     same at two distances (surface brightness held); the sky-view, the
 *     aerial volume and the environment's fill bit for bit those of an
 *     ordinary light at the eye's direction and radiance.
 * T7. Every existing lit, atmosphere and traced test: its figures and
 *     bits unchanged (the ordinary branch).
 * T8. test_rt_shadow_mask_placed: a blocker between a face and the source
 *     shadows it; the same blocker moved beyond the source doesn't; with a
 *     disc, the penumbra's width the rule's angle.
 * T9. test_rt_path_trace_placed: the path tracer's converged face under a
 *     placed light against T2's ordinary light, within its own tolerance
 *     (test_rt_path_trace's); a reflected hit
 *     (LIT_TRACE_REFLECTIONS) lit from the hit's side.
 * T10. test_atmosphere_shell_placed_light, no longer pending (card A55's
 *     test 9): a star as a placed light 1e3 radii off between two worlds:
 *     each world's lit limb faces it (the brightest texel of each disc on
 *     its star's side); a placed light whose source is inside a shell's
 *     top sphere refused at the send and at the compile, its message
 *     naming the shell, the slot and the light; a point and a spot light
 *     still refused (kept).
 * T11. test_atmosphere_placed_sun_through_air: test_atmosphere_sun_
 *     through_air's face lit by a placed sun 1e4 radii off: within 1/1024
 *     of its directional figures; two faces either side of the world's
 *     terminator, each its own side's transmittance.
 * T12. test_lit_placed_cascades: cascades fitted along the direction from
 *     the eye; a pole's shadow on a face near the eye the same texels as
 *     under T2's ordinary light.
 * T13. test_frame_allocations: a frame with a placed light, a shell lit by
 *     it, a traced mask and the eye moved: 0 of Rend's.
 * T14. Measured (section 7), recorded with the build.
 *
 * ===========================================================================
 * 10. When built
 * ===========================================================================
 *
 * The order: this (the type, the checks, the block's two float4s and
 * directional_at() with every reader, the eye's values, the shells, the
 * traced rays), then its caller's half. Metal built and run on the Mac;
 * D3D12 and Vulkan written through Carrottus GPU with no backend branch,
 * run in their sessions (card X25). Records: RBTREND_TODO.md item 58 (as
 * built) and item 52's pending line struck; rbtrendlit.h's file comment
 * ("Colors and light": a placed light's rule); rabbittustests' README
 * (T10's line); ./check.sh --update-abi with the addition listed;
 * ../claude_made_decisions.md for any shape the build takes beyond this.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card L78's draft; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. Where a placed light's values live: a, in the frozen
 *     RBTREND_LIT_SCENE_LIGHT's fields a directional light leaves unread
 *     (`_position` the place, `_range` the reference distance, `_radius`
 *     the disc at it), the type saying which are read, as point and spot
 *     lights do (recommended: no new struct, no new call, the list stays
 *     one array); b, a new based struct and a new setter; c, a separate
 *     list of placed lights beside the scene's.
 * Q2. A point nearer the source than its radius: a, `_radius` above 0
 *     required for a placed light, the distance taken no less than the
 *     source's radius D sin(`_radius`) (recommended: a finite, physical
 *     answer, the source's surface; a sphere light's clamp); b, `_radius`
 *     0 allowed and the light's factor capped at a fixed bound (a constant
 *     with no meaning); c, no clamp (infinity at the place; the caller
 *     keeps every point out of it).
 * Q3. A traced shadow ray towards a placed light: a, it stops at the
 *     source's surface, d - R (recommended: a body beyond the source
 *     doesn't shadow it); b, unbounded as a directional light's (cheaper by
 *     nothing, wrong beyond the source).
 */

/* ========================================================================= */
/* rbtrendlit.h                                                              */
/* ========================================================================= */

/**
 * @brief The kinds of light.
 *
 * [CHANGED comment] Gains: "A directional light, wherever this library
 * says so, is one of type RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL or
 * RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED: it lights every point it
 * reaches with no range, isn't culled per tile, is one of
 * RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS, may have cascades or a traced mask,
 * and may be an atmosphere's sun or a shell's light." Why: one
 * definition, so the headers' other mentions keep their words (pass 3.3).
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
	 * environment filled from it) takes its values at the scene's eye; an
	 * atmosphere's shell at the shell's centre (rbtrendcommands.h,
	 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS); a directional light's
	 * cascades along the direction the caller fits them by
	 * (RBTREND_LIT_CASCADE_FIT::light_direction).
	 *
	 * [ADDED] Why it earns its place: a light whose source is near the
	 * scene against the scene's extent lights it from many directions; no
	 * other type does that and keeps a sun's role (card L78).
	 */
	RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED = 3,
};

/*
 * RBTREND_LIT_SCENE_LIGHT: its layout unchanged (frozen). [CHANGED
 * comments] on its members, each gaining the placed kind:
 * - `_position`: "Where a point or spot light is, or where a placed
 *   directional light's source is, in the scene's space. Not read for a
 *   directional light."
 * - `_direction`: "...a directional light's direction, from the surface
 *   towards the light. Any length but 0. Not read for a point light or a
 *   placed directional light."
 * - `_radiance`: "...(a point or spot light at distance 1; a placed
 *   directional light at its `_range`)."
 * - `_range`: "Where a point or spot light's light has fallen to 0,
 *   smoothly: above 0. What tiled culling bounds it by. A placed
 *   directional light's reference distance: where its `_radiance` and
 *   `_radius` are given, above 0. Not read for a directional light."
 * - `_radius`: "A directional light's angular radius, in radians, 0 to
 *   0.5 (a placed one's above 0, seen from its `_range`)..."
 * - `_sky_disc`: "A directional light only (either kind)..."
 * Why: the struct's fields carry the placed kind (Q1a); each says when it
 * is read.
 */

/*
 * rbtrend_lit_scene_set_lights(): its signature unchanged. [CHANGED
 * comment] Its @return's list, as section 6's order: "...a direction of 0
 * where it is read (spot, directional), a range not above 0 (point, spot,
 * placed directional), angles out of order or past pi / 2 (spot), a
 * radius out of 0 to 0.5 (directional), not above 0 or above 0.5 (placed
 * directional) or not 0 (point, spot), or `_sky_disc` true on a point or
 * spot light; RBT_RESULT_LIMIT if more than RBTREND_LIT_MAX_LIGHTS are
 * point and spot lights, or more than RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS
 * are directional (either kind)." And its text gains: "A placed
 * directional light's values at the scene's eye (what the sky and the
 * atmosphere's tables read) follow the eye: set again by
 * rbtrend_lit_scene_set_view()." Why: what the call checks and keeps.
 */

/*
 * rbtrend_lit_scene_set_view(): [CHANGED comment] gains: "It moves where
 * each placed directional light is seen from (RBTREND_LIT_LIGHT_TYPE_
 * DIRECTIONAL_PLACED)." No new failure. Why: the eye's values (section 2)
 * are written by it too.
 */

/*
 * RBTREND_LIT_CASCADE_FIT::light_direction: [CHANGED comment] "From the
 * scene towards the light: a directional light's
 * RBTREND_LIT_SCENE_LIGHT::_direction; for a placed directional light, a
 * direction of the caller's choosing towards its source (from the view's
 * eye, or one held while the light turns): its shadow is projected along
 * it, while its shading turns per point." Why: the fit takes no scene and
 * a placed light has no one direction; the caller says which (section 2).
 */

/*
 * RBTREND_LIT_ATMOSPHERE::sun_count: [CHANGED comment] "How many of the
 * scene's directional lights (either kind), the first in the list's
 * order, light the medium..." Why: a placed light is a sun (section 4.1).
 */

/*
 * RBTREND_LIT_ATMOSPHERE_SHELL::_lights: [CHANGED comment] "Its lights, by
 * their places in the scene's light list: each a directional light, its
 * direction and radiance used as they are, or a placed directional light,
 * its direction and radiance taken at #_center by the type's rule; a
 * placed light whose source is inside or on the shell's top sphere is
 * refused. A point or spot light is refused. Entries past #_light_count
 * are not read." Why: replaces "a directional light given a source place
 * joins them once Rend has one" (A55's placeholder).
 */

/* ========================================================================= */
/* rbtrendcommands.h                                                         */
/* ========================================================================= */

/*
 * RBTREND_COMMAND_LIT_SHADOW_MASK: [CHANGED comment] "...towards its
 * light: a directional light's along its direction (through its disc);
 * a placed directional light's from each texel towards its source, each
 * ray stopping at the source's surface." Its refusal ("a light that isn't
 * directional") unchanged in words, the placed kind directional.
 *
 * RBTREND_COMMAND_LIT_PATH_TRACE: [CHANGED comment] "...each directional
 * light over its disc (each one's `_radius`; a placed one's from each
 * bounce, its rays stopping at its source's surface)..."
 *
 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS: [CHANGED comment] "...the light it
 * gathers from the shell's lights (directional, a placed one's taken at
 * the shell's centre; RBTREND_LIT_ATMOSPHERE_SHELL)..." and its refusals
 * gain "a placed directional light whose source is inside or on a shell's
 * top sphere (at the send and the compile, naming the shell, the slot and
 * the light)".
 *
 * Why: each command's contract says what it does with the new kind.
 */

/*
 * No struct type, no result code, no command. No version raise while 0.3
 * is untagged (card X2).
 */
