/*
 * rbtrend_atmosphere_space_draft.h - card L62's stage 4: an atmosphere's
 * shell seen from outside it (a world seen whole from orbit, its limb's
 * glow and its terminator, several worlds' shells in one frame), as one
 * blended pass of Rend's own after the lit surfaces (RBTREND_TODO.md item
 * 52; card A55 in DECISIONS.md, which names the session that asked).
 * Its caller's own half is drafted beside the caller's headers.
 *
 * DRAFT, 2026-11-16, version 8 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), NOT APPROVED. Not
 * built, not in any target, not compilable. Written on the MacBook (M5
 * Pro) against the approved rbtrend_atmosphere_draft.h (version 10) and
 * what was built from it (rabbittus ede3990, 0.3): render/rbtrendatmosphere.h,
 * rbtrendlit.h (RBTREND_LIT_ATMOSPHERE, rbtrend_lit_scene_set_atmosphere,
 * RBTREND_LIT_SCENE_LIGHT), rbtrendcommands.h (commands 39, 45 to 50, the
 * 120-byte parameters, rbtrend_command_list_send/_compile), rbtcore.h
 * (struct types to 5037), src/render/rbtrendatmosphere.cpp,
 * shaders/rbtrend_atmosphere_model.slang, _skyview_frag, _aerial_frag,
 * rbtrend_lit_atmosphere.slang, rbtrend_lit_sky_frag.slang; carrottus's
 * crgpu.h (CRGPU_BLEND_FACTOR: no dual-source factor;
 * CRGPU_MAX_DESCRIPTOR_BINDINGS_PER_SET 16, CRGPU_MAX_DESCRIPTOR_SET_LAYOUTS
 * 4); the concurrent large-worlds drafts (rabbittus/rbtrend_large_worlds_draft.h
 * and its caller's half, version 1, not approved:
 * Rend fed a space whose origin is the eye); and Hazel's renderer
 * (../.refs/HazelRenderer2025, present: Hazelnut's
 * Resources/Shaders/PostProcessing/SceneComposite.glsl).
 *
 * ===========================================================================
 * Revision notes (the critique's seven passes, 2026-11-16; each fixed
 * before the next; the lenses are the workspace's critique prompt's)
 * ===========================================================================
 *
 * 2026-11-17: caller names removed (check.sh caller-names), meaning kept.
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. The first shape drew the scene's own atmosphere from outside by a
 *    rule of Rend's: above its top, the sky shader and the aerial volume
 *    would stop and the new command take over. That changed what
 *    rbtrendatmosphere.h promises ("climbing out keeps a sky") and made a
 *    caller that never records the command lose its air in orbit. Found:
 *    the caller already sets no atmosphere on its scene outside every shell
 *    (card L62's B1: "none holding it: none drawn (stage 4 draws a shell
 *    from outside)"), so from outside every world is simply a shell the
 *    command is given. Changed: additions only for the sky; the scene's
 *    own atmosphere keeps its stage 2 and 3 meaning everywhere.
 * 2. With no scene atmosphere outside, a world's ground seen from orbit
 *    lost its suns' transmittance (no reddened terminator, every texel up
 *    to the night side under the full sun). Changed: a scene's atmosphere
 *    may now have no sky (sky-view sizes 0, as the aerial volume's 0
 *    already means none), lighting surfaces through its transmittance and
 *    drawing nothing behind them; the shell pass draws its air (section 4c,
 *    question Q3). The one [CHANGED] item.
 * 3. The shells' suns were the scene's first directional lights, as the
 *    sky from inside has them. In a system, two worlds either side of a
 *    star are lit from opposite directions, which one scene-wide direction
 *    can't say. Changed: each shell names its lights by index into the
 *    scene's own lights, a point light (a star) included, its direction and
 *    radiance taken at the shell's centre (section 3, Q2).
 * 4. Missing: where transparent surfaces go (after this pass: a cockpit's
 *    glass is in front of the air). Added to section 5's order.
 *
 * Pass 2 (what breaks an existing caller).
 * 1. sky_view_width and _height of 0 were refused; now both 0 mean "no
 *    sky". No caller passes 0 today (the first caller passes its
 *    settings' 32 to 1024), so nothing that worked changes. Only one of the two 0, or a
 *    volume without a sky-view, is still refused, each its own check.
 * 2. Found in the built code: a scene turns its atmosphere on only once
 *    its sky-view is first filled (rbtrendatmosphere.cpp,
 *    rbtrend_lit_scene_resolve_atmosphere: `!scene->sky_view_filled`
 *    returns). With no sky-view it would never turn on, and its surfaces
 *    would never read the transmittance. Changed: section 4c says it turns
 *    on after the first LIT_ATMOSPHERE copies the transmittance into its
 *    layer, with or without a sky-view.
 * 3. Checked: set 0 of the lit scene layout gains nothing (the shell pass
 *    has a layout of its own, section 7), so no caller's own lit shader
 *    breaks, unlike stages 2 and 3's binding 15. The existing shaders'
 *    sphere_hits keeps its meaning; the shell pass uses the stable form
 *    (section 6) and stage 2's passes move to it only if their tests,
 *    against Rabbittus Math within their tolerances, still hold.
 *
 * Pass 3 (what CONVENTIONS.md forbids).
 * 1. A constant at a point of use: the march's "longer ray, more steps"
 *    needed a reference length. Unreal makes it a setting (a distance at
 *    which the most steps are taken); here it is the shell's own longest
 *    chord, 2 sqrt(top^2 - bottom^2), so no number hides in the shader
 *    and none is added to the API. Stated in section 3.
 * 2. Allocation in a frame: the shells array is copied at the send into
 *    the list's own storage, as viewports are (steady state: none); the
 *    copy route for coloured transmittance (Q1's b) would take a target
 *    from the render target pool, kept from frame to frame (none after the
 *    first); dual-source blending (Q1's a) takes nothing. Stated.
 * 3. A cap at a point of use: the first shape had a fixed most-shells
 *    constant. Each shell is its own draw with its own sets, so nothing
 *    bounds the count but the list's storage: the constant removed.
 * 4. RBTREND_LIT_ATMOSPHERE_SHELL is an array element read at a compile:
 *    frozen, RBT_PLAIN_DATA, its fields `_`-prefixed as
 *    RBTREND_LIT_SCENE_LIGHT's (the scene's other per-frame array element).
 *
 * Pass 4 (what a reviewer would refuse).
 * 1. "Grey transmittance is a fudge": over a mean of three channels, the
 *    sun's disc seen through the limb (the sunset from orbit) and anything
 *    behind the limb are dimmed, not reddened. Rend's rule forbids a grey
 *    path on some devices and a coloured one on others ("never behavioural
 *    differences", rabbittus/CLAUDE.md), so it is one answer for every
 *    device: Q1 with three options and the cost of each; dual-source
 *    blending recommended, which needs a Carrottus draft first.
 * 2. "Why not Bruneton's precomputed in-scattering, one lookup a pixel?"
 *    Answered in section 9 (alternatives): its artefacts at the horizon
 *    are worst exactly at the limb, it differences two lookups against
 *    geometry, and it is a fourth table per world; the march is the
 *    integral the sky-view table approximates, so the crossing is
 *    continuous.
 * 3. Hazel compared from its source, not from memory (section 8): it has a
 *    depth-based fog in its composite pass, disabled by a define, its
 *    distances and colour constants in the shader. Claims about Unreal not
 *    read in a source in this workspace are marked as such.
 *
 * Pass 5 (the first user: a caller drawing round worlds at a new scale).
 * Worlds of 60 to 250 km radius, tops 20 to 40 km up, low orbits a few
 * hundred km up, worlds thousands to tens of thousands of km apart.
 * 1. At the crossing (the eye rising through a world's top) the caller
 *    switches from the scene's tables to the shell pass. The sky is
 *    continuous (the same integral both sides); far surfaces are not
 *    unless the aerial volume reaches every surface the eye sees from the
 *    top: sqrt(top^2 - bottom^2), 99.5 km for a 150 km world with a 30 km
 *    top, where the first caller's default reach is 32 km and its game's
 *    today 400 m. Changed: section 4b states the rule and its test; the caller's
 *    choice is Q4 in the card (the caller's half).
 * 2. Precision at system distances: a centre 1e7 to 1e8 m from the eye in
 *    floats (camera-relative, the large-worlds drafts). Section 6 works
 *    the ray-sphere sum in the stable form and marches from the entry
 *    point; the far-plane depth case found (pass 6.1).
 * 3. Checked: a whole 150 km world from 4 radii fills about a sixth of a
 *    60-degree view; the cost estimate in section 7 uses that caller's
 *    numbers, not the earth's.
 *
 * Pass 6 (the edges).
 * 1. "No surface drawn" was read as depth at its far value, but a caller's
 *    finite far plane (the large-worlds draft's 1e9 m) would then cut a
 *    march short at the far plane, and a world beyond it would lose its
 *    air. Changed: a texel at the projection's cleared far value (0
 *    reversed, 1 forward, read from the view's projection) is "nothing
 *    drawn", marched to the shell's exit whatever the far plane; a world
 *    past the far plane is drawn by its air and its ground sphere alone
 *    (section 4d: what a world needs no mesh for).
 * 2. Overlapping shells (two spheres that intersect) can't be ordered for
 *    every ray by any one sort; disjoint ones can (section 4e: the power
 *    of the eye). Said, with what goes wrong in the overlap.
 * 3. A shell listed while it holds the eye: marched from the eye, every
 *    pixel; allowed, costly (section 7), and how a caller draws its own
 *    world's air per pixel from inside (the no-sky scene atmosphere).
 * 4. A shell naming the scene's own drawn atmosphere (one with a sky-view)
 *    would add its air twice: refused at the compile.
 * 5. A light index whose light changed type or went away between the send
 *    and the compile: checked again at the compile.
 * 6. A shell behind the eye, or one whose bounds fall off the screen:
 *    nothing drawn, nothing copied.
 *
 * Pass 7 (tests, measurements, records).
 * 1. The tests named for rabbittustests (section 12) each check one claim
 *    of sections 3 to 6, against Rabbittus Math's transmittance where a
 *    CPU answer exists, against the sky-view where it doesn't (in-scattered
 *    light has no CPU function, by the approved draft's pass 7.3).
 * 2. The cost is an estimate (section 7), to be measured in Release on the
 *    M5 Pro at 1080p and 1440p by test_atmosphere_shell_measure before the
 *    first caller turns it on, as stages 2 and 3 were.
 * 3. ABI listed (section 11); the version raise shared with the large-
 *    worlds draft if both are built in one session.
 *
 * ===========================================================================
 * 1. What is drawn, and why a draft
 * ===========================================================================
 *
 * From outside a shell, the sky-view table can't draw it: the shell is a
 * thin ring a latitude map holds in a few texels (approved draft, 2b).
 * What is seen of a world from outside, all of it the same medium and the
 * same model as from inside:
 * - the air over the day side, its blue in front of the ground, thicker
 *   (bluer, brighter) towards the limb;
 * - the limb: a ring of glow where a view ray grazes the shell, longest
 *   through the air; against space beyond the world's edge, and
 *   reddened where the sun is behind the world (the sunset seen from
 *   orbit);
 * - the terminator: the ground and the air in its shadow going dark,
 *   the air's last light reddened along it;
 * - whatever is behind or inside the shell, dimmed by its transmittance:
 *   the ground (a mesh drawn, or the medium's own ground sphere where
 *   nothing is), stars, the sun's disc through the limb, another world.
 *
 * The approved draft's B4 found that this needs a blended full-screen pass
 * of Rend's own, not L7's transparency. Its commands (48 to 50) and structs
 * name none of it, so its public shape is this draft's.
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * [ADDED] One command, RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS (51): outside
 * a pass, after the lit surfaces (and after LIT_REFLECT where the frame has
 * it), it marches every shell it is given at every texel each one covers,
 * against `_depth`, and blends each one's light and transmittance over
 * `_target`, the farthest first.
 *
 * [ADDED] One frozen value struct, RBTREND_LIT_ATMOSPHERE_SHELL, an
 * element of the command's array: an atmosphere (its medium and its two
 * tables, rbtrendatmosphere.h), where its centre is this frame, and which
 * of the scene's lights light it.
 *
 * [ADDED] One limit macro, RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS.
 *
 * [CHANGED] RBTREND_LIT_ATMOSPHERE's sky-view sizes accept both 0: an
 * atmosphere that lights a scene's surfaces through its transmittance and
 * draws no sky (section 4c).
 *
 * Nothing else: no handle, no based struct (so no struct type: 5038 stays
 * free for the large-worlds draft's RBTREND_LIT_CASCADE_ANCHOR), no call,
 * no result code. Why the shells are in the command and not on the scene:
 * which worlds are seen, and where, changes every frame; a scene call
 * would hold a list Rend must size (an allocation when it grows) and
 * which the command reads anyway. The command's array is copied at the
 * send as every command's arrays are.
 *
 * ===========================================================================
 * 3. The march (Hillaire 2020's per-pixel path, as Rend computes it)
 * ===========================================================================
 *
 * For each shell, at each texel of `_target` its bounds cover (section 7):
 * 1. The view ray: from the scene's eye through the texel's centre, by the
 *    scene's inverse view-projection; its direction d, unit.
 * 2. Where it meets the shell: o = eye - centre; the entry t0 and exit t1
 *    of the sphere of top_radius (stable form, section 6). A miss, or t1
 *    behind the eye: the texel is left as it is, bit for bit.
 * 3. Where it stops: the nearest of the exit t1, the ground sphere's entry
 *    (bottom_radius) and the drawn surface's distance from `_depth`
 *    (section 4d). The march runs from max(t0, 0) to there.
 * 4. Steps: from `_min_steps` to `_max_steps`, more for a longer part of
 *    the ray inside the shell: steps = min + (max - min) * saturate(length
 *    / longest), longest = 2 sqrt(top^2 - bottom^2), the shell's longest
 *    chord that misses the ground; spaced closer near the entry, as the
 *    sky-view's march (stage 2's atmosphere_march, the same function: one
 *    model, one march, in rbtrend_atmosphere_model.slang).
 * 5. At each step: each of the shell's lights' light scattered once
 *    (through the transmittance towards the light, read from the shell's
 *    transmittance table; none where the ground sphere hides it: the
 *    world's own shadow, its night side), by each layer's phase, plus the
 *    multiple-scattering table's term; the light gathered and dimmed by
 *    the transmittance back to the entry, per channel.
 * 6. Where the ray stopped at the ground sphere with nothing drawn: the
 *    medium's ground, lit by each light through its transmittance, of
 *    ground_albedo, diffuse, dimmed by the march's transmittance (as the
 *    sky-view draws its ground, stage 2): RBTMATH_SHELL_MEDIUM defines the
 *    bottom as an opaque ground, so it is drawn opaque.
 * 7. The result, per channel: L (light the air adds) and T (what of the
 *    texel behind it gets through); the texel becomes texel x T + L, or
 *    with the ground (6) the ground's light and T 0.
 *
 * A shell's lights (Q2): each `_lights` entry names a light of the scene
 * (rbtrend_lit_scene_set_lights' list), directional or point. A
 * directional light's direction and _radiance are used as they are; a
 * point light's direction is from the shell's centre towards it, and its
 * radiance what a white matte face square to it at the centre would read
 * (its _radiance and fall-off at that distance): a star far from its
 * world, whose direction and strength don't change across the shell to
 * within the shell's radius over its distance. A spot light is refused (a
 * cone is no sun). Each light's illuminance is pi x radiance, as inside
 * (rbtrendatmosphere.h, "Units and light"). The lights are the very ones
 * that light the world's ground, so its ground and its air agree.
 *
 * ===========================================================================
 * 4. How it composes
 * ===========================================================================
 *
 * 4a. WITH NOTHING ELSE. A scene with no atmosphere: each shell is drawn
 * over what the lit pass left (surfaces, the sky shader's environment or
 * analytic sky, the suns' discs, a caller's stars), farthest first.
 *
 * 4b. WITH THE SCENE'S OWN ATMOSPHERE, AND THE CROSSING. While the eye is
 * inside a world's shell its caller draws that world as the scene's
 * atmosphere (stages 2 and 3); outside, as a shell given here. At the top
 * the two must agree. They do for the sky: the sky-view's texel and the
 * per-pixel march are the same integral from the same point (the sky-view
 * from just inside, the march from just outside), differing by the
 * table's resolution and step counts: test_atmosphere_shell_crossing holds
 * them within 3%. For surfaces they agree only where the aerial volume
 * reaches: inside, a surface beyond aerial_distance reads the volume's last
 * slice (air up to aerial_distance only); outside, the march reaches it.
 * So a caller that crosses tops with no step sets aerial_distance at
 * least sqrt(top^2 - bottom^2), the farthest ground seen from the top (99.5
 * km for a 150 km world with a 30 km top; 1,131 km for the earth's 6,360
 * and 6,460 km). Whether the caller does that, or switches to the per-pixel
 * path lower (4c), or Rend later marches only the remainder beyond the
 * volume, is the caller's choice and Q4 (none needs another item here).
 *
 * Another world's shell seen from inside the scene's atmosphere (a moon's
 * air, a neighbour in the sky): its light comes through the scene's own
 * air, and the scene's air is in front of it. The pass puts it behind:
 * with L_s, T_s the shell's and L_h, T_h the scene's own air along the
 * texel's ray (for a texel with nothing drawn, the sky-view's radiance and
 * the transmittance from the eye out of the scene's shell, one lookup
 * each; for a drawn surface, the aerial volume's slice at its distance),
 * the texel P = L_h + T_h x S becomes
 *     P x T_s + L_h x (1 - T_s) + T_h x L_s  =  L_h + T_h x (L_s + T_s x S),
 * per channel and with no division: the blend's source is the last two
 * terms and its factor T_s (section 5).
 *
 * 4c. AN ATMOSPHERE WITH NO SKY ([CHANGED]). RBTREND_LIT_ATMOSPHERE with
 * sky_view_width and sky_view_height both 0 (and so no aerial volume):
 * - each of its sun_count suns reaches each lit surface through its
 *   transmittance, as with a sky (the world's terminator on its ground);
 * - the sky shader draws the sky as a scene with no atmosphere does (its
 *   environment or analytic sky, its suns' discs not dimmed): the shell
 *   pass, given the same atmosphere and centre, dims and lights them;
 * - LIT_ATMOSPHERE copies its transmittance table into the scene's array
 *   and fills nothing else; the scene reads its atmosphere from the frame
 *   after that copy (today: after the first sky-view fill, which a scene
 *   with no sky-view never has: pass 2.2);
 * - LIT_ATMOSPHERE_ENVIRONMENT leaves the environment as it was (as for a
 *   sky-view not yet filled): a caller keeps the last one filled, from
 *   nearer the ground, which is the better light for that ground.
 * This is how a caller draws a world it is outside of with its ground
 * still lit through its air (the shell pass drawing the air), and how it
 * draws a world it is inside of per pixel (4b's other way): the same
 * atmosphere given to the scene with no sky and given as a shell. Listing
 * as a shell the scene's atmosphere with a sky-view is refused (its air
 * twice, pass 6.4); with no sky, it is what the shell is for.
 *
 * 4d. AGAINST DEPTH. `_depth` is the depth the lit surfaces were drawn
 * with, through the scene's view-projection. A texel whose depth is the
 * projection's cleared far value (0 for a reversed projection, 1 for a
 * forward one, read from the view-projection Rend holds) is "nothing
 * drawn": its ray runs to the shell's exit or its ground, whatever the
 * projection's far plane. Elsewhere the surface's distance along the ray,
 * from the depth by the inverse view-projection. Points of the march under
 * the ground sphere (a mesh's valley below bottom_radius) are read at it,
 * as the aerial volume's are (stage 3's fix), and no ground sphere is drawn
 * where a surface is: a world drawn as a mesh is lit and hazed as a mesh,
 * a world past the far plane (or with no mesh) is drawn by its air and its
 * ground sphere, at no cost to the caller. A surface nearer than the
 * shell's entry is untouched.
 *
 * 4e. SEVERAL SHELLS. Each its own draw, ordered by the eye's power
 * against its top sphere, |eye - centre|^2 - top^2, greatest first: for
 * spheres that don't intersect this is an order every ray agrees with (the
 * power diagram's acyclicity, Edelsbrunner 1990); a shell holding the eye
 * has a negative power and is drawn last, nearest. Rend sorts the copied
 * array at the compile (a few entries, insertion, in place). Two shells
 * that intersect (a moon's air dipping into a planet's) are drawn in that
 * order and composed wrongly where they overlap: a game keeps shells
 * apart, as card L62's B1 already asks of nesting.
 *
 * ===========================================================================
 * 5. Blending, the frame's order, barriers
 * ===========================================================================
 *
 * The texel becomes P x T + X (section 3's 7, 4b's composition), T and X
 * per channel. A fixed-function blend multiplies the destination by one
 * factor from the source; Carrottus GPU's CRGPU_BLEND_FACTOR has no second
 * source today, so a per-channel T can't be the factor while X is the
 * colour. Q1, one answer for every device (Rend's rule: no behaviour by
 * device):
 * a. Dual-source blending (recommended): the pass writes X to its first
 *    output and T to its second, blended ONE, SRC1_COLOR. One draw a shell,
 *    nothing more in memory, coloured. Needs Carrottus GPU's draft first
 *    (carrottus/crgpu_dual_source_blend_draft.h, not written: the
 *    SRC1_COLOR, ONE_MINUS_SRC1_COLOR, SRC1_ALPHA and ONE_MINUS_SRC1_ALPHA
 *    factors and a feature bit; Metal's MTLBlendFactorSource1Color, D3D12's
 *    D3D12_BLEND_SRC1_COLOR, Vulkan's dualSrcBlend), and the shader step
 *    to emit a second output on each backend (Slang's, to SPIR-V, DXIL and
 *    a .metallib: checked in that draft). This command then names the
 *    feature and is refused (RBT_RESULT_UNSUPPORTED, at the compile) on a
 *    device created without it, as Rend refuses other features.
 * b. A copy, then a write: the shell's bounds copied from `_target` to a
 *    render-target-pool texture of its format, then the march reads the
 *    copy and writes P x T + X with no blend. Every device, coloured; a
 *    texture the target's size kept by the pool (29.5 MB at 2560 x 1440 in
 *    R16G16B16A16_FLOAT) and the copy's bandwidth (about 0.2 ms at 1440p
 *    for a shell filling the view, section 7).
 * c. Grey: T's mean as the source alpha, blended ONE, SRC_ALPHA, as stage
 *    3's volume keeps one T. Every device, no memory; what is behind the
 *    limb (the sun's disc at an orbital sunset, a moon setting) dimmed,
 *    not reddened.
 *
 * The frame's order (the caller's to record, as every command's):
 * ATMOSPHERE_TABLES for each atmosphere a shell or the scene names;
 * LIT_ATMOSPHERE for the scene; the frame pass (the lit surfaces and the
 * sky); COPY_SCENE_COLOR, LIT_SSR, LIT_TRACE_REFLECTIONS, LIT_REFLECT where
 * the frame has them; LIT_ATMOSPHERE_SHELLS; the transparent surfaces (a
 * cockpit's glass in front of the air; a transparent surface far inside a
 * shell isn't hazed, as today inside: L7's later stage); bloom, exposure,
 * the tone map. Rend records the barriers by the list's tracked states as
 * for LIT_REFLECT: `_depth` and the tables to SHADER_READ_ONLY, `_target`
 * to COLOR_ATTACHMENT_OPTIMAL (loaded, stored), left there.
 *
 * ===========================================================================
 * 6. Precision far from the origin
 * ===========================================================================
 *
 * Rend takes positions in the scene's space, whatever its origin. The
 * large-worlds drafts (version 1, not approved) feed Rend a space whose
 * origin is the eye, the caller subtracting in double on the CPU: then a
 * centre 1e8 m off (between worlds of a system) is a float good to about 6
 * m, under a millionth of a radian from the eye, well under a texel. This
 * draft holds in such a space and in any other; it needs nothing of the
 * caller but the scene's space, and it adds no double to Rend's API.
 * What the pass does to stay exact at those distances:
 * - The sphere's hits in the stable form (Haines, Guenther and
 *   Akenine-Moller, "Precision Improvements for Ray/Sphere Intersection",
 *   Ray Tracing Gems, 2019, ch. 7): b = dot(o, d); the discriminant as
 *   r^2 - |o - b d|^2, not b^2 - (|o|^2 - r^2), whose two terms of 1e16
 *   cancel in floats; the near root as c / q with q = -b - sign(b)
 *   sqrt(disc). Measured in the test at 1e7 and 1e9 units (section 12).
 * - The march from the entry point: the entry p0 = o + t0 d found once (to
 *   a few metres at 1e8 m, a thousandth of a scale height), every step's
 *   point taken as p0 + s d with s from 0, so heights inside the shell
 *   carry the precision of a radius, not of the eye's distance.
 * - Depth: the caller's projection; reversed float depth holds a
 *   centimetre's near plane with a far plane of 1e9 m (the large-worlds
 *   drafts' test). A surface's distance is read from depth as the view's
 *   linear depth over the ray's cosine with the view's axis, not by
 *   rebuilding a world position and subtracting the eye.
 * - No table changes: the tables are indexed by radius and angle, which
 *   the entry point gives exactly as above.
 *
 * ===========================================================================
 * 7. Cost, memory, the 16-binding cap, no compute
 * ===========================================================================
 *
 * Each shell's draw covers its bounds on the screen only: the projected
 * bounds of its top sphere (Mara and McGuire, "2D Polyhedral Bounds of a
 * Clipped, Perspective-Projected 3D Sphere", JCGT 2013), as a scissor
 * rectangle; the whole screen where the eye is inside or the sphere
 * crosses the near plane. A texel inside the rectangle whose ray misses
 * the sphere costs one intersection and is left as it was.
 *
 * Estimated (to be measured, never relied on): a step is about 150
 * arithmetic operations counting each exp, sqrt or division as four, and
 * two texture reads per light (the transmittance, the multiple
 * scattering). The M5 Pro's GPU taken at 8 to 9 TFLOPS of float32 (its
 * cores, 128 lanes each, about 1.6 GHz; the measurement decides) at half
 * of that in practice:
 * - a whole 150 km world from 4 radii (the first caller's world from high orbit; about
 *   a sixth of a 60-degree view; most rays near-vertical through 30 km of
 *   air, about 8 steps of a 4 to 32 range): 0.1 to 0.2 ms at 1440p;
 * - from low orbit, the world filling half the view, about 10 steps on
 *   average: 0.5 to 0.7 ms at 1440p, 0.3 to 0.4 ms at 1080p;
 * - the worst: every texel marched at 32 steps (the eye inside a shell
 *   drawn per pixel, 4c, with long rays near the horizon): about 4 ms at
 *   1440p and 2.3 ms at 1080p. The caller's `_max_steps` halves it at 16;
 *   it is why inside a world the tables stay the way (the sky-view and
 *   volume: 0.95 ms measured, stage 3);
 * - Q1's b adds the copy: a shell's bounds read and written once, 59 MB at
 *   1440p for a shell filling the view, about 0.2 ms at the M5 Pro's
 *   bandwidth; a and c add nothing.
 * Budget: 1.5 ms at 1440p for a world filling the view from low orbit, in
 * Release, submit to idle (test_atmosphere_shell_measure, section 12),
 * before the first caller turns it on; over it, the steps first, then a
 * card for a half-resolution march (an upsample by depth at the limb's
 * edges), measured, not assumed.
 *
 * Memory: nothing per shell (each atmosphere's tables exist already, 136
 * KiB); a shell's position and lights are push constants (the centre, the
 * two lights' directions and illuminances: 64 bytes, within the 128 every
 * backend gives). Q1's b alone adds the pool's texture.
 *
 * Bindings: the pass has its own layout, four sets, within Carrottus GPU's
 * four set layouts and 16 bindings a set: set 0 the atmosphere's medium
 * block (its material, as its tables' passes have it); set 1 its
 * transmittance table and sampler; set 2 its multiple-scattering table
 * and sampler; set 3 the frame's: `_depth`, the scene's atmosphere array
 * (for 4b's composition: the sky-view, the transmittance, the volume's
 * slices), their sampler, and under Q1's b the copy: 5 bindings. Nothing
 * of set 0 of the lit scene's layout changes.
 *
 * No compute and no 3D storage image: a full-screen fragment pass a shell,
 * as stages 2 and 3's tables are, so nothing is refused for a missing
 * storage feature anywhere (Q1's a refuses for its blend factor alone).
 *
 * ===========================================================================
 * 8. Compared
 * ===========================================================================
 *
 * - Hillaire 2020 and its sample (read for the technique, never copied in):
 *   the per-pixel ray march over the screen with depth is the paper's
 *   ground truth and its path for a camera in space, where the sky-view
 *   table can't hold the shell; the sample moves a ray's start to the top
 *   of the atmosphere for a camera above it (its MoveToTopAtmosphere), as
 *   here. Its tables and march are stage 2's, unchanged.
 * - Unreal's SkyAtmosphere (from its documentation and source as
 *   remembered, not in this workspace; to be checked at approval): the
 *   sky-view table inside the atmosphere and a per-pixel march outside it;
 *   a sample count between a least and a most by the ray's length (its
 *   SampleCountMin and SampleCountMax, with DistanceToSampleCountMax a
 *   setting where this takes the shell's longest chord); a coloured
 *   transmittance by dual-source blending where the platform has it and a
 *   grey one where not (which Rend's rule forbids it to copy); one
 *   SkyAtmosphere per scene, a planet's centre by its transform. Several
 *   worlds at once go past it.
 * - Bruneton 2008/2017: a 4D in-scattering table per medium gives a ray's
 *   light from space in one or two lookups; card L62 chose Hillaire over it
 *   (a table per medium, rebuilt on change, heavy for several worlds). See
 *   section 9's alternative D.
 * - KSP's Scatterer (blackrack's mod, open source; read for what it does,
 *   not its code): Bruneton's tables per planet, applied over the scene
 *   from the depth buffer as a post-process, the planet drawn from orbit in
 *   KSP's scaled space (far bodies in a second, scaled-down camera) for
 *   precision. Here precision comes from the eye-relative space and the
 *   stable intersection (section 6), with no second camera.
 * - Space Engine and Elite Dangerous (by what they show, no source): every
 *   body of a system with its own atmosphere seen from anywhere, a body far
 *   off drawn as its air and a coloured sphere: what 4d's ground sphere
 *   gives a caller for a world past its far plane.
 * - Hazel (Rend's bar): no atmosphere; its composite pass has a fog read
 *   from depth (SceneComposite.glsl, HZ_RENDERER_FOG_EFFECT, off), a
 *   smoothstep of linear depth towards a constant colour, its distances and
 *   colour literals in the shader. The same shape, a full-screen pass over
 *   the picture against depth, from a physical medium in place of
 *   constants. Rend's stages already pass it; this goes further.
 *
 * ===========================================================================
 * 9. Alternatives, and why not
 * ===========================================================================
 *
 * A. Rend switches the scene's own atmosphere to this pass above its top
 *    (the first shape, pass 1.1): a changed promise, and a caller without
 *    the command losing its air. Not chosen.
 * B. The shells held on the scene (a set call with an array): an
 *    allocation when the count grows, a list the caller sends every frame
 *    anyway. Not chosen.
 * C. Shells as geometry (a sphere mesh round each world, drawn in the lit
 *    pass's order with its own shader): needs depth read in the pass that
 *    writes it (Carrottus GPU has no read-only depth layout, item 32), and
 *    sorted transparency. Not chosen.
 * D. Bruneton's in-scattering for viewers in space (a 3D table per world:
 *    view zenith at the entry, sun zenith, sun-view angle), one lookup a
 *    texel: cheap per pixel, but a fourth table per world (1 MiB at 64 x
 *    64 x 32 half floats), its known artefacts at the horizon (worst at the
 *    limb, the one place this is for), and against a drawn surface a
 *    difference of two lookups with the same artefacts. Not chosen; the
 *    march is the integral the sky-view approximates, so the crossing is
 *    continuous.
 * E. Half-resolution march with a depth-aware upsample: kept for the
 *    budget's sake if the measurement asks (section 7), not before.
 * F. Suns as explicit directions and radiances per shell (Q2's b): the
 *    caller could then light a world's air differently from its ground.
 *    Not recommended.
 *
 * ===========================================================================
 * 10. Rend's contract, kept; Rabbittus Math's line
 * ===========================================================================
 *
 * - The command's checks at the send, each its own, kept as every
 *   command's refusals are and reported at the compile, naming the
 *   command and the field (rbtrend_command_list_compile's list gains
 *   LIT_ATMOSPHERE_SHELLS): a NULL or destroyed scene (RBT_STALE); a
 *   `_shell_count` above 0 with NULL `_shells`; a step count out of range,
 *   the least above the most; a `_depth` not a SHADER_RESOURCE 2D view of
 *   depth at the view's target size, single-sampled; a `_target` not a
 *   RENDER_TARGET 2D view of R16G16B16A16_FLOAT, R32G32B32A32_FLOAT or
 *   R11G11B10_FLOAT at that size; and in each shell (its index named): a
 *   NULL or destroyed atmosphere, or one of another instance; a centre not
 *   finite (each axis); a `_light_count` above 2; a light index past the
 *   scene's lights, or naming a spot light. At the compile again: the
 *   lights (they may change after the send); an atmosphere whose medium
 *   was never set or whose tables were never built (the shell is skipped,
 *   and the compile's Debug report says so, as a stale sky-view is); a
 *   shell naming the scene's own atmosphere with a sky-view (refused,
 *   RBT_RESULT_INVALID_ARGUMENT); Q1's a on a device without the feature
 *   (RBT_RESULT_UNSUPPORTED).
 * - Errors: RBT_RESULT and the compile's RBTREND_ERROR (the send keeps no
 *   result of its own, rbtrendcommands.h); nothing printed.
 * - The Debug overlap check: the send is the list's (RBTREND_ENTER on the
 *   list); the compile reads each atmosphere as LIT_ATMOSPHERE reads it,
 *   one caller at a time (rbtcore.h, "Threads"). No new object, so no new
 *   stale tag, quarantine or factory; the destroyed-handle tests gain a
 *   shell naming a destroyed atmosphere.
 * - Records: RBTREND_LIT_ATMOSPHERE_SHELL is RBT_PLAIN_DATA (copied into
 *   the list at the send); the per-shell draw reads it at the compile, no
 *   hot record (a few entries, cold beside a draw list's).
 * - Shaders: shaders/rbtrend_atmosphere_shell_frag.slang (its entry point
 *   rbtrend_atmosphere_shell_frag), importing rbtrend_atmosphere_model
 *   (the march, the stable hits added there) and Rend's full-screen
 *   vertex shader; added to RABBITTUS_SHADERS; a second output under Q1's
 *   a. Written from the paper.
 * - Rabbittus Math's line (the user's): nothing here is a question a caller
 *   asks with no picture in it. The transmittance through a shell from
 *   outside is already rbtmath_shell_medium_compute_transmittance's (its
 *   rays may start outside the shell: approved draft's test 2); the light
 *   in-scattered along a ray stays unasked on the CPU (approved draft,
 *   pass 7.3); the stable ray-sphere sum is a line of the shader, and of
 *   rbtmathmedium.cpp's own sphere_hits where its test asks. So Rabbittus
 *   Math gains nothing, and Rend nothing of the medium's definition.
 * - Doxygen on every item; rbtrendatmosphere.h's "Inside and outside",
 *   "Out of scope" and @par lines say what stage 4 now draws.
 *
 * ===========================================================================
 * 11. ABI
 * ===========================================================================
 *
 * Additions: one enumerator (RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS = 51),
 * its parameters in RBTREND_COMMAND's union (48 bytes of 120), one frozen
 * struct, one macro. Changed in meaning, no layout: RBTREND_LIT_ATMOSPHERE's
 * sky-view sizes accept both 0 (a value refused before now has a meaning,
 * so no caller that worked breaks); rbtrend_lit_scene_set_atmosphere's and
 * LIT_ATMOSPHERE's comments. No layout, enum value or result changes; set
 * 0 of the lit scene's layout unchanged. Listed in the commit by
 * ./check.sh --update-abi. Rabbittus 0.3 to 0.4 (the session's one raise,
 * shared with the large-worlds draft's if both are built together).
 * Under Q1's a, Carrottus's own draft lists its additions.
 *
 * ===========================================================================
 * 12. Tests (C through the public API; rabbittustests on Metal under its
 *     validation, warnings fatal; Vulkan and D3D12 written, run in their
 *     sessions, card X25)
 * ===========================================================================
 *
 * The earth's medium in kilometres and the first caller's (a 150 km world, 30
 * km top, its scale heights) in metres, each where it says.
 * 1. test_atmosphere_shell_refusals: every check of section 10, each its
 *    own message naming the field and the shell's index.
 * 2. test_atmosphere_shell_transmittance: from 3 bottom radii out, a white
 *    surface drawn at the ground under a vertical ray, under the limb's
 *    ray and at a grazing ray: its texel's T (lights off, so L is 0) against
 *    rbtmath_shell_medium_compute_transmittance from the entry to the
 *    surface, within 1%, per channel (under Q1's c, the mean).
 * 3. test_atmosphere_shell_crossing: the eye 1e-4 of the thickness under
 *    the top drawing the scene's atmosphere (sky-view and a volume
 *    reaching sqrt(top^2 - bottom^2)), then as far above it drawing the
 *    shell: the sky's texels and a far surface's within 3%, row by row.
 * 4. test_atmosphere_shell_whole_world: from 4 radii, the light at 90
 *    degrees to the view: the day side's air bluer than red, the limb's
 *    ring brighter than the disc's middle, the night side's ground black
 *    (its air nearly so), and with the light behind the world the far
 *    limb's ring redder than blue (under Q1's a or b: the sun's disc seen
 *    through it red as well).
 * 5. test_atmosphere_shell_depth: a surface inside the shell hazed; a
 *    surface in front of the shell, and every texel outside the shell's
 *    bounds, unchanged bit for bit; a far projection plane nearer than
 *    the world: the world still drawn by its air and ground sphere.
 * 6. test_atmosphere_shell_order: two disjoint shells overlapping on the
 *    screen, given in both orders: the same bytes; each drawn alone and
 *    composed by hand on the CPU from the two readbacks within 1%.
 * 7. test_atmosphere_shell_inside: the eye inside the scene's atmosphere,
 *    a second shell (a moon's) in its sky: the moon's air within 2% of
 *    4b's formula from the scene's sky alone and the moon's shell alone.
 * 8. test_atmosphere_shell_far: the same world at 1e7 and 1e9 units from
 *    the eye (eye-relative), and scaled down a thousandfold: the limb's
 *    radius on the screen within half a texel of the analytic, no texel
 *    across the limb out of order (no speckle), the scaled scene's texels
 *    within 1%.
 * 9. test_atmosphere_shell_point_light: a star as a point light 1e3 radii
 *    off on each side of two worlds: each world's lit side faces it.
 * 10. test_atmosphere_no_sky: a scene atmosphere with no sky-view: the
 *    sky shader's texels those of the same scene with no atmosphere, bit
 *    for bit; a surface's sun light dimmed as with a sky-view, bit for bit
 *    against test_atmosphere_sun_through_air's.
 * 11. test_atmosphere_none_unchanged (stage 2's) and every lit and sky
 *    test: the same pixels with no shell recorded.
 * 12. test_atmosphere_shell_measure (RABBITTUSTESTS_MEASURE, Release,
 *    submit to idle): 1920 x 1080 and 2560 x 1440; a world filling the view
 *    from low orbit; a whole world at 4 radii; eight small worlds; the
 *    eye inside drawing per pixel at 16 and 32 steps.
 * 13. test_frame_allocations with shells drawn: none of Rend's; the
 *    destroyed-handle tests (make_destroyed_tests.py) with a shell's
 *    destroyed atmosphere; the misuse harness regenerated from abi.txt.
 */

/* ========================================================================= */
/* render/rbtrendlit.h                                           [ADDED]     */
/* ========================================================================= */

/** The most steps a texel's march takes in RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS. */
#define RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS 128u
/* [ADDED] Why: the command's step counts are checked against it, as the
 * sky-view's are against RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_STEPS. */

/**
 * @brief An atmosphere's shell drawn from where the eye is, by
 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS: which atmosphere, where its
 * centre is this frame, and which of the scene's lights light it.
 *
 * [ADDED] Why it earns its place: the command draws any number of worlds'
 * air, each its own medium, place and lights, which change every frame.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_LIT_ATMOSPHERE_SHELL {
	/** The atmosphere (rbtrendatmosphere.h): its medium and tables. Of the
	 * scene's instance; it must outlive the list's submission. */
	rbtrend_atmosphere _atmosphere;
	/** Its centre in the scene's space this frame: finite. */
	float _center[3];
	/** How many of #_lights light it: 0 to 2. 0: its air is dark, its
	 * ground black, and it still dims what is behind it (a world's night). */
	uint32_t _light_count;
	/**
	 * Its lights, by their places in the scene's light list
	 * (rbtrend_lit_scene_set_lights()): each a directional or a point
	 * light. A directional light's direction and radiance are used as they
	 * are; a point light's direction from the centre towards it and its
	 * radiance at the centre (a star, far from the shell against its
	 * radius). Entries past #_light_count are not read.
	 */
	uint32_t _lights[2];
} RBTREND_LIT_ATMOSPHERE_SHELL;

/* ========================================================================= */
/* render/rbtrendlit.h, RBTREND_LIT_ATMOSPHERE                   [CHANGED]   */
/* ========================================================================= */

/*
 * Two members' comments, no layout change:
 *
 *	/ ** The sky-view table's width (round the eye) and height (from the
 *	 * zenith to the nadir), each RBTREND_LIT_ATMOSPHERE_MIN_SKY_VIEW_SIZE to
 *	 * RBTREND_LIT_ATMOSPHERE_MAX_SKY_VIEW_SIZE (192 x 108 is the paper's),
 *	 * or both 0 for no sky: the atmosphere then lights the scene's surfaces
 *	 * through its transmittance and draws nothing behind them, the aerial
 *	 * volume's sizes 0 too (its air drawn by
 *	 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS, given the same atmosphere and
 *	 * centre). * /
 *	uint32_t sky_view_width;
 *	/ ** See #sky_view_width. * /
 *	uint32_t sky_view_height;
 *
 * [CHANGED] Why: a world seen from outside its shell keeps its ground lit
 * through its air (its terminator reddened) while its air is drawn per
 * pixel; and a caller may draw the air it is inside per pixel the same
 * way. A value refused until now gains a meaning: no caller that worked
 * changes.
 *
 * rbtrend_lit_scene_set_atmosphere()'s comment gains: "With no sky (both
 * sky-view sizes 0): the sky shader draws as with no atmosphere, the suns'
 * discs undimmed; each of the first sun_count directional lights still
 * reaches a surface through the transmittance. Refused: one sky-view size
 * 0 and not the other; a volume without a sky-view." And its Memory line:
 * "with no sky, the transmittance's layer only."
 *
 * RBTREND_COMMAND_LIT_ATMOSPHERE's comment gains: "For a scene whose
 * atmosphere has no sky, copies its transmittance table only; the scene
 * reads it from the next frame." LIT_ATMOSPHERE_ENVIRONMENT's "whose
 * sky-view table holds nothing yet" covers a scene with no sky: the
 * environment is left as it was.
 */

/* ========================================================================= */
/* render/rbtrendcommands.h                                      [ADDED]     */
/* ========================================================================= */

/*
 *	/ **
 *	 * Draws atmospheres' shells over `_target`, outside a pass: each of
 *	 * `_shells` (RBTREND_LIT_ATMOSPHERE_SHELL, rbtrendlit.h), the farthest
 *	 * first, by the eye's power against its top sphere. At each texel its
 *	 * bounds cover, the view ray from `_scene`'s eye is marched through the
 *	 * shell from where it enters (or the eye, inside it) to where it leaves,
 *	 * meets the ground sphere or meets the surface `_depth` holds (a texel
 *	 * at the projection's far value holds none, whatever its far plane), in
 *	 * `_min_steps` to `_max_steps` steps, more for a longer ray (the most
 *	 * at the shell's longest chord, 2 sqrt(top^2 - bottom^2)); the light it
 *	 * gathers from the shell's lights (Hillaire's model, with its
 *	 * atmosphere's two tables) is added and what is behind dimmed by its
 *	 * transmittance, per channel. Where the ray meets the ground sphere and
 *	 * no surface is drawn, the medium's ground is drawn there, opaque, lit
 *	 * by the lights through the air. A texel the ray doesn't reach in the
 *	 * shell is left as it was, bit for bit.
 *	 *
 *	 * Inside the scene's own atmosphere (with a sky-view), a shell is put
 *	 * behind the scene's air: its light dimmed by the scene's transmittance
 *	 * along the ray and the scene's air kept in front, read from the
 *	 * scene's sky-view and volume. A shell naming the scene's own
 *	 * atmosphere then is refused (its air twice); with no sky, naming it is
 *	 * how its air is drawn (rbtrendlit.h, RBTREND_LIT_ATMOSPHERE).
 *	 *
 *	 * `_scene` gives the view (its view-projection, which can be inverted,
 *	 * its eye and target size), its lights and its atmosphere. `_depth` is a
 *	 * SHADER_RESOURCE 2D view, DEPTH aspect, single-sampled, of the depth
 *	 * the surfaces were drawn with through that view-projection at the
 *	 * view's target size; `_target` a RENDER_TARGET 2D view of the
 *	 * picture, linear (before exposure and the tone map), of
 *	 * R16G16B16A16_FLOAT, R32G32B32A32_FLOAT or R11G11B10_FLOAT at that
 *	 * size, left in COLOR_ATTACHMENT_OPTIMAL. Each atmosphere's tables built
 *	 * (ATMOSPHERE_TABLES) earlier in the list or before it; one never built
 *	 * is skipped, the compile's Debug report saying which. `_min_steps` 1
 *	 * to `_max_steps`, `_max_steps` at most
 *	 * RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS. `_shells` copied at the send.
 *	 *
 *	 * The frame's order: after the lit surfaces and LIT_REFLECT, before the
 *	 * transparent surfaces and the tone map. [Under Q1's a:] Needs
 *	 * CRGPU_FEATURE_DUAL_SOURCE_BLEND on the device: refused with
 *	 * RBT_RESULT_UNSUPPORTED at the compile without it.
 *	 *
 *	 * It binds pipelines and sets of its own, as BLOOM does.
 *	 * /
 *	RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS       = 51,
 *
 *	struct {
 *		/ ** The scene: its view, lights and atmosphere. * /
 *		rbtrend_lit_scene _scene;
 *		/ ** The shells, `_shell_count` of them; copied at the send. * /
 *		const RBTREND_LIT_ATMOSPHERE_SHELL *_shells;
 *		/ ** How many: 0 draws nothing. * /
 *		uint32_t _shell_count;
 *		/ ** The least steps a texel's march takes: 1 to `_max_steps`. * /
 *		uint32_t _min_steps;
 *		/ ** The most: up to RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS (Unreal's 32 and the paper's are good starts). * /
 *		uint32_t _max_steps;
 *		/ ** The depth the surfaces were drawn with. * /
 *		rbtrend_texture_view _depth;
 *		/ ** The picture the shells are drawn over. * /
 *		rbtrend_texture_view _target;
 *	} _lit_atmosphere_shells; / **< RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS. * /
 *
 * [ADDED] Why: the one thing stage 4 is, a pass of Rend's own over the
 * picture against depth (approved draft, B4), with the steps the caller's
 * as the sky-view's are (Pass 2 of the approved draft: no cost hidden at a
 * point of use). 48 bytes of the 120.
 */

/* ========================================================================= */
/* rbtcore.h                                                     [none]      */
/* ========================================================================= */

/*
 * No struct type (the shell is a frozen value), no result code. The
 * version: RABBITTUS_VERSION_MINOR 3 -> 4, once, shared with any other
 * draft built in the same session.
 */

/*
 * ---------------------------------------------------------------------------
 * Questions for the user (card A55; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. Coloured transmittance over what is behind a shell: a, dual-source
 *     blending, Carrottus GPU's draft first (recommended); b, a copy of the
 *     shell's bounds and a write; c, grey (the mean). Section 5.
 * Q2. A shell's lights: a, the scene's lights by index, a point light's
 *     direction and radiance at the centre (recommended); b, directions and
 *     radiances given per shell; c, the scene's first directional lights,
 *     as inside. Sections 3, 9F.
 * Q3. A world's ground seen from outside its shell: a, lit through its air
 *     by an atmosphere with no sky (the one [CHANGED] item; recommended);
 *     b, under the full sun (no change to RBTREND_LIT_ATMOSPHERE); c, an
 *     atmosphere per drawn instance, a design of its own later. Section 4c.
 * Q4 and Q5 are the caller's (its half): where it crosses from the tables
 *    to the shell, and how many worlds a target draws from outside.
 */
