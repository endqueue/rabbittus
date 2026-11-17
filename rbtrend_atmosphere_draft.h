/*
 * rbtrend_atmosphere_draft.h - a scattering medium in a sphere's shell, as
 * another way to fill and draw Rend's lit sky (RBTREND_TODO.md item 52;
 * card L62, stages 2 and 3, Draft B; DECISIONS.md and ROADMAP.md name the
 * session and its caller's own half, drafted beside the caller's headers).
 *
 * DRAFT, 2026-11-16, version 10 (after the Critique's first round of
 * seven passes, the user's answers B1 to B4, a round over the change B3
 * made, and the user's second round of seven), APPROVED by the user
 * 2026-11-16 on that round's completion. Not built, not in any target, not
 * compilable. Two new headers: math/rbtmathmedium.h in Rabbittus Math (the
 * medium and its CPU functions: pure maths, B3) and
 * render/rbtrendatmosphere.h in Rend, reached by rbtrend.h's umbrella;
 * every declaration in it is [ADDED]; what changes in rbtrendlit.h,
 * rbtrendcommands.h and rbtcore.h is marked [ADDED] there too, and nothing
 * existing is [CHANGED] in meaning. Written on the MacBook (M5 Pro) against
 * rbtrendlit.h, rbtrendcommands.h (LIT_SKY, LIT_ENVIRONMENT, the 120-byte
 * parameters), rbtcore.h (struct types 5000+, results),
 * shaders/rbtrend_lit_sky_frag.slang, rbtrend_lit_scene.slang and the
 * prefilter's, src/render/rbtrendlit.cpp, as read on 2026-11-16 (rabbittus
 * 58b3e87, 0.2.0), and Hazel's renderer (../.refs/HazelRenderer2025:
 * Renderer.cpp's CreatePreethamSky, PreethamSky.glsl, SceneEnvironment.h).
 *
 * Decided with the user 2026-11-16 (card L62), built to here, not asked
 * again: Hillaire's model ("A Scalable and Production Ready Sky and
 * Atmosphere Rendering Technique", EGSR 2020; Unreal's SkyAtmosphere), in
 * stages; this draft is stages 2 (the tables, the sky from inside) and 3
 * (aerial perspective). Stage 4 (the shell seen from space, blended) is
 * not in it: it waits on blending the shell over what is behind it,
 * and the critique (pass 6) says what stage 4 needs.
 *
 * ---------------------------------------------------------------------------
 * 1. Part of Rend's lit pipeline, not beside it
 * ---------------------------------------------------------------------------
 *
 * Rend's lit sky today: an rbtrend_lit_environment (a prefiltered specular
 * cube and an irradiance cube), filled by LIT_ENVIRONMENT (a picture) or
 * LIT_SKY (Preetham's or Hosek and Wilkie's analytic sky); a scene's
 * RBTREND_LIT_AMBIENT names it, and every lit surface's ambient and
 * reflections read it; RBTREND_LIT_SHADER_SKY_FRAGMENT draws it behind
 * everything, with each directional light's disc over it; directional
 * lights light surfaces at their _radiance. An atmosphere is another way to
 * fill and draw that same sky. For each existing piece, what it reads from
 * the atmosphere and what stays:
 *
 * | Piece                         | With an atmosphere on the scene                                   | Without (today, unchanged) |
 * |-------------------------------|-------------------------------------------------------------------|----------------------------|
 * | The environment (IBL)         | filled by LIT_ATMOSPHERE_ENVIRONMENT from the scene's sky-view     | LIT_ENVIRONMENT, LIT_SKY   |
 * |                               | table, then prefiltered by the same code LIT_SKY's fill uses       |                            |
 * | Lit surfaces' ambient         | the environment, as now (it holds the atmosphere's sky)           | as now                     |
 * | The sky drawn behind          | SKY_FRAGMENT reads the sky-view table (sharp, every frame) where  | the environment's mip 0    |
 * |                               | the scene has an atmosphere, the environment where it hasn't      |                            |
 * | A directional light's disc    | its radiance times the transmittance from the eye to the edge of  | its radiance               |
 * |                               | the shell along the light: red and dim at a low sun               |                            |
 * | A directional light on a      | its radiance times the transmittance from the surface towards the | its radiance               |
 * | surface                       | light (one lookup in the transmittance table, in the lit pass)    |                            |
 * | Aerial perspective (stage 3)  | each lit surface's colour times the view's transmittance to it,   | nothing                    |
 * |                               | plus the light scattered into the ray before it, from the aerial  |                            |
 * |                               | volume, in the lit pass every lit surface already goes through    |                            |
 * | Shadows, clusters, SSR, AO    | as now                                                            | as now                     |
 *
 * Hosek and Wilkie's and Preetham's models stay, as the analytic skies
 * beside it, for a caller with no atmosphere (a flat world's daylight).
 * Nothing here is a second sky system: no second sky shader, no second
 * environment kind, no second light list. A scene with no atmosphere set
 * reads none of it (a flag in the scene's uniforms, as the environment's
 * is), and draws the same pixels as today (tests below). The risk to that:
 * Metal compiles shaders with fast math, so code added beside today's
 * path, even untaken, may let the compiler reorder today's arithmetic and
 * move a pixel by a bit. If test 11 sees it, the surface and sky shaders
 * get an atmosphere variant of their own (an RBTREND_LIT_SHADER value
 * each, the caller choosing the pipeline), so today's shaders stay byte
 * for byte as they are; never a looser comparison.
 *
 * The environment's frame (with Draft A's 4a): Rend fills it in the frame
 * the scene's ambient names (RBTREND_LIT_AMBIENT.environment_rotation),
 * read by LIT_ATMOSPHERE_ENVIRONMENT from the scene; a caller that sets
 * the eye's local frame, its +Z towards the sun's bearing, needs to fill
 * it again only when the sun's elevation or the eye's height changes.
 *
 * The one place the medium is described is Rabbittus Math's
 * RBTMATH_SHELL_MEDIUM (section 5, answer B3): what the medium is, its
 * density and its transmittance are maths with no picture in them; Rend
 * takes that struct and renders it, its shaders implementing the same
 * model, tested against Rabbittus Math's functions.
 *
 * ---------------------------------------------------------------------------
 * 2. The model (Hillaire 2020), as Rend computes it
 * ---------------------------------------------------------------------------
 *
 * A medium fills the shell between two radii, bottom_radius (the ground,
 * opaque, of ground_albedo) and top_radius, round a centre the caller
 * places per view (2a). Three kinds of particle, each with a density
 * profile of height h above the bottom:
 * - Rayleigh (molecules): scattering rayleigh_scattering (per unit length,
 *   red, green, blue), density exp(-h / rayleigh_scale_height), phase
 *   3/(16 pi) (1 + cos^2);
 * - Mie (aerosols): scattering mie_scattering and extinction mie_extinction
 *   (absorption the difference), density exp(-h / mie_scale_height), phase
 *   Cornette-Shanks with mie_anisotropy g;
 * - an absorbing layer (ozone): extinction absorption_extinction, density a
 *   tent of half-width absorption_half_width round absorption_center_height
 *   (Unreal's and Bruneton's ozone).
 * Units of length are the scene's (the unit its positions are in) and
 * every coefficient is per that unit; nothing in Rend assumes the earth.
 * A float's precision is relative, so the earth in metres (a radius of
 * 6.36e6) holds a height near the ground to about half a metre, as it
 * would in kilometres to half a thousandth: the unit changes nothing, and
 * no scale between the scene and the medium is needed.
 *
 * The tables, each a texture Rend makes at the atmosphere's or the scene's
 * creation, sized from the caller's desc:
 * - transmittance: T from a height up a direction to the top of the shell
 *   (0 where the ray meets the ground), parameterised as Bruneton's (height
 *   and view zenith, the horizon's texels kept), RGBA16F, desc's size
 *   (256 x 64 is the paper's), built when the medium changes;
 * - multiple scattering: Hillaire's isotropic multiple-scattering term
 *   (the infinite series of orders 2+ in one pass, 64 directions a texel),
 *   RGBA16F, desc's size (32 x 32), built when the medium changes, after
 *   transmittance;
 * - sky-view: the sky's radiance seen from the scene's eye, in a latitude
 *   and longitude map round the eye's local up with more texels near the
 *   horizon (the paper's non-linear latitude), RGBA16F (R11G11B10 would
 *   lose the night's low end), scene's size (192 x 108), a frame;
 * - aerial perspective: a froxel volume over the view's frustum, in-scattered
 *   light in RGB and the mean transmittance in A, the slices spread to
 *   aerial_distance, RGBA16F, scene's size (32 x 32 x 32), a frame.
 * How many samples each march takes is the caller's (the atmosphere's desc
 * for its tables, the scene's for its sky-view and aerial volume), not a
 * constant at a point of use: Unreal exposes the same counts. The
 * multiple-scattering table's 64 directions a texel are the technique's
 * (the paper's 8 by 8 sphere), a named constant beside its shader.
 * The light is the scene's first sun_count directional lights (1 or 2, as
 * Unreal's), each a sun of illuminance pi * _radiance (a white matte face
 * square to it reads _radiance, rbtrendlit.h's scale), so the sky's
 * brightness and the surfaces' agree with no factor of the caller's.
 *
 * 2a. WHERE IT IS. The medium is a shape and its stuff; where its centre is
 * belongs to each view (RBTREND_LIT_ATMOSPHERE.center, in the scene's
 * space), set as often as the scene's view is. Moving a world moves its
 * atmosphere without rebuilding a table; turning it doesn't matter (a
 * sphere). Its up at any point is away from the centre: the same up
 * a caller gives a sky turned to the local up (Draft A's frame).
 *
 * 2b. INSIDE AND OUTSIDE. The sky-view table and the aerial volume are
 * worked out from wherever the eye is: inside the shell, from the eye;
 * just above it, from where the view ray enters the top (the paper's), so
 * a camera climbing out keeps a sky (thin, then black with the sun's disc)
 * with no step at the top. Below the ground (an eye under bottom_radius)
 * the eye is taken at the ground. What the sky-view table can't do is a
 * camera far outside, where the shell is a thin ring a latitude map holds
 * in a few texels: that is stage 4, the paper's per-pixel march over the
 * screen, blended over what is drawn by the march's transmittance. Found
 * in writing this: stage 4 needs a blended full-screen pass Rend records
 * itself after the opaque surfaces, not L7's transparency (sorted
 * transparent surfaces), which only matters for a transparent object seen
 * through a shell. So stage 4 need not wait for L7: told to the user, for
 * card L62's order; not built here.
 *
 * ---------------------------------------------------------------------------
 * 3. What a frame does, and who writes what when
 * ---------------------------------------------------------------------------
 *
 * In one command list, in this order (the caller's, as every Rend command's
 * order is):
 * 1. RBTREND_COMMAND_ATMOSPHERE_TABLES (outside a pass), for each
 *    atmosphere a scene of the list uses: rebuilds its transmittance table,
 *    then its multiple-scattering table, where its medium changed since they
 *    were last built (rbtrend_atmosphere_set_medium); else records nothing.
 *    Writes the atmosphere's two tables, read after it by 2, 3 and the lit
 *    pass.
 * 2. RBTREND_COMMAND_LIT_ATMOSPHERE (outside a pass), once per scene a
 *    frame, after the scene's view, lights and atmosphere are set: writes the
 *    scene's sky-view table and, where the scene's atmosphere asks for it
 *    (stage 3), its aerial volume, from the atmosphere's tables.
 * 3. RBTREND_COMMAND_LIT_ATMOSPHERE_ENVIRONMENT (outside a pass), when the
 *    caller wants the environment to follow (its own rule: costly, see 4):
 *    fills an environment from the scene's sky-view table and prefilters it.
 * 4. The frame pass: the sky shader and the lit surfaces read the tables,
 *    the sky-view and the volume.
 * Rend records the barriers between them as for its other commands (a
 * storage write before a sampled read, by the list's tracked states); one
 * queue orders frame N+1's writes after frame N's reads. A scene drawn with
 * its atmosphere but no LIT_ATMOSPHERE in the list since its view changed
 * reads the tables of the last one (a frame stale): the compile says so in
 * Debug through the list's report, as an unfilled environment's clear is.
 * A table never built reads as no atmosphere (transmittance 1, nothing
 * scattered): Rend clears it at its creation's first compile, as it clears
 * an unfilled environment.
 *
 * Nothing is allocated a frame. The atmosphere's two tables at its create;
 * the scene's sky-view and volume at rbtrend_lit_scene_set_atmosphere when
 * their sizes differ from those it holds (once, at the first; the same
 * sizes again allocate nothing); past a device's limit, a result.
 *
 * ---------------------------------------------------------------------------
 * 4. Cost (to be measured when built; the paper's own figures are for a
 *    console GPU and are not repeated here until read from it)
 * ---------------------------------------------------------------------------
 *
 * - The tables, on a medium's change only: one dispatch each.
 * - A frame: the sky-view table (192 x 108 texels, ~30 samples each, the
 *   multiple scattering read, not marched) and, in stage 3, the aerial
 *   volume (32^3); the lit pass gains one transmittance read per
 *   directional sun and one volume read per texel; the sky shader one read.
 * - The environment's fill: the prefilter's cost, as LIT_SKY's (4.9 to 6.6
 *   ms on the M5 Pro, measured 2026-11-16, rabbittustests' test_lit_sky_measure): never
 *   a frame by default. The caller fills it when the sun or the eye has
 *   moved enough (by an angle of the sun and a height of the eye, say).
 * Measured before and after in Release on the M5 Pro: rabbittustests'
 * test_atmosphere_measure (tables, sky-view, volume, each on its own,
 * RABBITTUSTESTS_MEASURE), and a caller's whole frame with and without.
 *
 * ---------------------------------------------------------------------------
 * 5. What a caller can ask on the CPU: Rabbittus Math (answer B3)
 * ---------------------------------------------------------------------------
 *
 * The user's line, 2026-11-16: pure general-purpose maths is Rabbittus
 * Math's, rendered graphics Rend's. So the medium and the questions a
 * caller asks of it with no picture in them are Rabbittus Math's, in a new
 * header, math/rbtmathmedium.h:
 * - RBTMATH_SHELL_MEDIUM: a frozen value struct, a layered medium in a
 *   spherical shell (two radii, Rayleigh-like and Mie-like exponential
 *   layers, an absorbing tent layer, a ground albedo) in maths terms, no
 *   sky, planet or rendering word;
 * - rbtmath_shell_medium_compute_density(): each layer's density relative to the
 *   bottom at a batch of points relative to the centre;
 * - rbtmath_shell_medium_compute_transmittance(): transmittance along a batch of
 *   rays to where each leaves the shell (0 into the ground), marched in
 *   the caller's steps;
 * - rbtmath_shell_medium_compute_phase(): the two phase functions (Rayleigh's;
 *   Cornette-Shanks with g) at a batch of cosines, the model's last pure
 *   part.
 * A game's air drag, a star's brightness through the air, a light's colour
 * at sunset: a headless server or a physics step asks these and never
 * includes a renderer. Rabbittus Math's conventions: 4 spaces and Allman
 * braces in src/, tabs in the header, RBT_RESULT and a per-call
 * RBTMATH_ERROR, batch-shaped, no allocation, no instance. In float, as
 * the shaders compute and a game's positions are (Rabbittus Math's
 * ray queries are double; this answers in the precision its callers hold),
 * its source built with floating-point contraction off, as its polygons
 * are, so the same bits on every platform; tested bit for bit alone and in
 * four slices on Rabbittus Jobs' pool at 1 and 4 threads.
 *
 * Rend: rbtrend_atmosphere_set_medium() takes a const RBTMATH_SHELL_MEDIUM
 * *, as Rend already takes Rbt* maths types (rbtrend includes rbtmath.h's
 * headers; Rabbittus Math includes nothing of Rend's). Rend's shaders
 * (rbtrend_atmosphere_model.slang) implement the same model, each
 * function's comment naming its C counterpart; the GPU's tables are tested
 * against Rabbittus Math's functions within a stated tolerance (half
 * floats, the GPU's exp), not bits. Rend adds what is rendering only: the
 * tables, the sky-view, the aerial volume, the multiple-scattering term
 * (an approximation that exists to make a sky cheaply, not a quantity a
 * caller asks for), the environment's fill, the commands.
 *
 * ---------------------------------------------------------------------------
 * 6. Several media
 * ---------------------------------------------------------------------------
 *
 * Each medium is an rbtrend_atmosphere, with its own tables; as many as the
 * caller makes. A scene draws one at a time
 * (rbtrend_lit_scene_set_atmosphere): which one is the caller's choice (the
 * one whose shell holds its camera, the smallest such, and none outside
 * every shell, is one rule a caller may use). Moving from one to another is the caller setting
 * another, which costs nothing at the scene (its sky-view and volume are
 * the scene's, re-filled next frame from the other's tables). Seen from
 * outside every shell, a medium isn't drawn in stages 2 and 3 (stage 4).
 *
 * ---------------------------------------------------------------------------
 * 7. Who else uses it, with no change to the design
 * ---------------------------------------------------------------------------
 *
 * - A flat world's sky: one medium whose bottom_radius
 *   is large against the playfield (6,360 km in kilometres, earth's), its
 *   centre below the world's origin by that much; the camera always inside.
 *   Or none, and the analytic sky as today.
 * - A day and night: the sun moves; the sky-view a frame follows it
 *   at no fill; twilight and night come from the model (Hosek and Wilkie's
 *   ends at the horizon). The moon a second sun (sun_count 2), dimmer.
 * - Several worlds in one game: a medium each, the camera's drawn.
 * - The editor's viewport: the same scene calls; the editor never applies
 *   its effects level to it (a game's rendering).
 * - A CAD or BIM viewport: a building's daylight study under a real sky,
 *   day and night, from a site's latitude; the earth's medium in metres,
 *   the model in its own units. Nothing in the terms (a sphere's shell, a
 *   sun, a medium) belongs to a game or a planet.
 * - Not for water, fog in a box or under a plane (underwater,
 *   height fog): a medium in a shell has no flat boundary.
 *   The aerial-perspective application in the lit pass is the place such a
 *   fog would also be applied; that is a card of its own.
 *
 * ---------------------------------------------------------------------------
 * 8. Compared
 * ---------------------------------------------------------------------------
 *
 * - Hillaire 2020 and its sample (read for the technique, never copied in;
 *   nothing vendored): the same four tables and the same medium; the paper
 *   marches the sky per pixel from space, which this draft leaves to stage 4.
 * - Unreal's SkyAtmosphere: the same parameters by other names (bottom
 *   radius, atmosphere height, Rayleigh and Mie scattering, absorption and
 *   their exponential distributions, the absorption's tent, ground albedo,
 *   two lights); it adds a multiple-scattering factor and a sky luminance
 *   factor (art controls, left out: the caller's exposure and its light's
 *   colour do the same); it places the planet by a transform, as here by a
 *   centre.
 * - Bruneton 2017: one precomputed 4D scattering table per medium, costly to
 *   rebuild on a change and heavy in memory for several media (card L62's
 *   reason against); its parameterisation of transmittance is used here.
 * - Unity HDRP's physically based sky: precomputed tables of the planet's
 *   medium, a planet centre and radius, a camera able to leave the shell;
 *   the same terms.
 * - Godot 4's PhysicalSkyMaterial: a sky shader with Rayleigh and Mie
 *   parameters, no shell and no aerial perspective: what Rend's analytic
 *   skies already are.
 * - Hazel (Rend's bar): a Preetham sky computed into a cube on change
 *   (Renderer::CreatePreethamSky, PreethamSky.glsl), used as both radiance
 *   and irradiance with no prefilter, no disc, no atmosphere or
 *   transmittance. Rend's analytic skies already pass it; this goes past.
 *
 * ---------------------------------------------------------------------------
 * 9. Rend's contract, kept
 * ---------------------------------------------------------------------------
 *
 * - rbtrend_atmosphere: an opaque handle; a create taking a based desc
 *   (RBT_STRUCT_TYPE_REND_ATMOSPHERE_DESC, registered in rbtcore.h's Rend
 *   block); a destroy whose release waits for the GPU through Rend's release
 *   queue; RBT_RESULT and a per-call RBTREND_ERROR; argument checks a line
 *   each; RBTREND_ENTER on every call on it; RBT_STALE after debug_name, a
 *   Debug destroy ending in RBT_RETIRE into the instance's quarantine, a
 *   factory in rabbittustests' destroyed-handle tests; malloc and in-place
 *   construction; the commands' parameters within RBTREND_COMMAND's 120
 *   bytes (each below names its handles only: 8 to 24 bytes).
 * - Its records: the atmosphere's state (`rbtrend_atmosphere_t`: the
 *   medium, a changed flag, two textures and their views, the pipelines'
 *   indices) is cold, read at a command; what a frame walks is the scene's
 *   uniform block, which grows by the atmosphere's block (centre, radii,
 *   flags, 80 bytes, padded to 16): RBT_HOT_RECORD's budget for it says so.
 *   The medium is RBT_PLAIN_DATA.
 * - Shaders: Slang in shaders/: rbtrend_atmosphere_model.slang (a module:
 *   the medium's densities, phases, the transmittance's parameterisation),
 *   rbtrend_atmosphere_transmittance_comp, _multiscatter_comp,
 *   _skyview_comp, _aerial_comp, and the environment's fill
 *   (rbtrend_atmosphere_environment_comp, before the existing prefilter);
 *   the sky and lit-scene shaders import the model and the scene's bindings
 *   module, which gains the atmosphere's set entries (declared whole, read
 *   or not, as Carrottus GPU's rule asks). Written from the paper.
 * - Features: compute and storage images, which every device Rend runs on
 *   has today; a 3D storage image for the aerial volume: read from the
 *   device, and an atmosphere asking for a volume (aerial_depth above 0)
 *   refused with RBT_RESULT_UNSUPPORTED and its message where it lacks it.
 * - Doxygen on every item; rbtrend.h includes rbtrendatmosphere.h.
 *
 * ---------------------------------------------------------------------------
 * 10. ABI
 * ---------------------------------------------------------------------------
 *
 * Additions only: two headers (Rabbittus Math's medium, Rend's atmosphere),
 * a frozen value struct and three functions in Rabbittus Math, a handle and
 * two based structs and their types (5036, 5037) in Rend, three commands
 * (48, 49, 50) and their parameters in RBTREND_COMMAND's union (within its
 * 120 bytes), a scene call, no new shader enum value (the sky fragment
 * shader is the same one, reading the scene's flag). The lit scene layout (set 0) gains
 * bindings: a layout Rend owns and a caller only names, so its pipelines
 * made from it keep working, but a caller that built a pipeline layout by
 * hand from set 0's bindings would not: none known does. Listed
 * as a break in the commit by ./check.sh --update-abi. Rabbittus 0.2 to 0.3.
 *
 * ---------------------------------------------------------------------------
 * 11. Tests (C, through the public API; rabbittustests on a GPU, under
 *     validation, warnings fatal; Rabbittus' own for the CPU functions)
 * ---------------------------------------------------------------------------
 *
 * Rabbittus Math (no GPU), tests/rbtmathmedium_tests.c, beside the BVH
 * and polygon tests (and the phase functions: each integrating to 1 over
 * the sphere, Rayleigh's symmetric, the Mie layer's forward for g > 0):
 * 1. density: Rayleigh's at the bottom 1, at one scale height 1/e, above
 *    the top 0; the tent's peak at its centre and 0 past its width.
 * 2. transmittance against a brute-force reference in the test (a plain
 *    Riemann sum at 100,000 steps): straight up, along the horizon, a ray
 *    into the ground (0), from outside the shell through it; within 1e-4.
 * 3. the earth's medium (the paper's values, in kilometres): the vertical
 *    transmittance at sea level against the brute force; the zenith's
 *    optical depth against the closed form for an exponential atmosphere,
 *    H * beta.
 * 4. bit for bit: 10,000 points and rays, alone and in four slices on
 *    Rabbittus Jobs' pool at 1 and 4 threads: the same bytes.
 * 5. the misuse generator over the two functions; every argument check.
 * rabbittustests (GPU), each under Metal's validation (Vulkan and D3D12
 * written, run in their own sessions, card X25):
 * 6. test_atmosphere_transmittance_table: read back, against the CPU
 *    function at 32 texels' heights and angles, within 2e-3.
 * 7. test_atmosphere_zenith_and_horizon: the earth's medium, the sun 45
 *    degrees up: the sky-view's zenith bluer than red, the horizon whiter
 *    (less blue to red than the zenith).
 * 8. test_atmosphere_sunset: the sun at 1 degree: the sky round the sun
 *    redder than blue; the disc's radiance times transmittance red.
 * 9. test_atmosphere_climb: the eye at 0, 10, 50 and 100 km and 200 km
 *    (outside): the zenith's radiance falling each time, no step between
 *    two heights 1 km apart beyond 5%, and at 200 km near black but for
 *    the disc.
 * 10. test_atmosphere_aerial: a far white face at 50 km's distance in the
 *    lit pass, against the same face near: dimmer and bluer by the
 *    volume's transmittance and in-scatter, within 2% of the CPU's
 *    transmittance along the ray.
 * 11. test_atmosphere_none_unchanged: every lit and sky test's pixels with
 *    no atmosphere the same as before (the checksums the tests already
 *    hold; each run twice first); and every caller's own render tests,
 *    the same pixels.
 * 12. test_atmosphere_environment: the environment filled from the sky-view
 *    lights a white face upward bluer than sideways at noon.
 * 13. destroyed handles (make_destroyed_tests.py regenerated), the frame's
 *    allocations with an atmosphere drawn (test_frame_allocations: none),
 *    and synchronization validation silent on Vulkan (its session).
 */

/* ========================================================================= */
/* render/rbtrendatmosphere.h                                    [ADDED]     */
/* ========================================================================= */

/** @brief A medium in a sphere's shell and its two tables. */
typedef struct rbtrend_atmosphere_t *rbtrend_atmosphere;

/* ========================================================================= */
/* math/rbtmathmedium.h (Rabbittus Math)                         [ADDED]     */
/* ========================================================================= */

/**
 * @brief A layered medium in a spherical shell: what fills the space
 * between two radii round a centre the caller places, in three layers. A
 * value; lengths in the caller's unit, coefficients per that unit.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_SHELL_MEDIUM {
	/** The inner radius, an opaque ground: finite, above 0. */
	float bottom_radius;
	/** The outer radius: finite, above bottom_radius. */
	float top_radius;
	/** The first layer's scattering at the bottom (Rayleigh-like), per
	 * unit, three channels: each finite, at least 0. */
	float rayleigh_scattering[3];
	/** Its density falls by 1/e each this height: finite, above 0. */
	float rayleigh_scale_height;
	/** The second layer's scattering at the bottom (Mie-like): each finite, at least 0. */
	float mie_scattering[3];
	/** Its extinction at the bottom: each finite, at least mie_scattering's. */
	float mie_extinction[3];
	/** Its density falls by 1/e each this height: finite, above 0. */
	float mie_scale_height;
	/** Its phase's anisotropy g: finite, -0.999 to 0.999. */
	float mie_anisotropy;
	/** The third layer's extinction at its peak (absorbing only): each finite, at least 0. */
	float absorption_extinction[3];
	/** The height of its peak above the bottom: finite. */
	float absorption_center_height;
	/** Its density falls linearly to 0 this far either side: finite, above 0. */
	float absorption_half_width;
	/** The ground's albedo: each finite, 0 to 1. */
	float ground_albedo[3];
} RBTMATH_SHELL_MEDIUM;

/** @brief Each layer's density relative to its bottom's at each point
 * (relative to the centre), three floats a point; 0 outside the shell.
 * Thread: any; reads only its arguments.
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument
 *         with a count above 0 or a medium field out of range (each its own
 *         check and message, in the struct's order). */
RBT_RESULT rbtmath_shell_medium_compute_density(const RBTMATH_SHELL_MEDIUM *medium, const float *points, uint32_t count,
                                        float *out_density, RBTMATH_ERROR *error);

/** @brief The transmittance along each ray from its origin (relative to
 * the centre) to where it leaves the shell, three channels; 0 into the
 * ground. Marched in @p steps (1 to 1024) over the part inside.
 * Thread: any. @return as the density's, and a direction of 0 (its index
 * named) or steps out of range. */
RBT_RESULT rbtmath_shell_medium_compute_transmittance(const RBTMATH_SHELL_MEDIUM *medium, const float *origins,
                                              const float *directions, uint32_t count, uint32_t steps,
                                              float *out_transmittance, RBTMATH_ERROR *error);

/** @brief The two layers' phase functions at each cosine (-1 to 1) of the
 * angle between the light's and the view's directions: Rayleigh's and the
 * Mie layer's (Cornette-Shanks with g), two floats a cosine.
 * Thread: any. @return as the density's, and a cosine out of range. */
RBT_RESULT rbtmath_shell_medium_compute_phase(const RBTMATH_SHELL_MEDIUM *medium, const float *cosines, uint32_t count,
                                      float *out_phase, RBTMATH_ERROR *error);

/* ========================================================================= */
/* render/rbtrendatmosphere.h (Rend), continued                  [ADDED]     */
/* ========================================================================= */

/** @brief Configuration descriptor for an atmosphere: its tables' sizes. No default. */
typedef struct RBTREND_ATMOSPHERE_DESC {
	/** RBT_STRUCT_TYPE_REND_ATMOSPHERE_DESC, and its chain. */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** The transmittance table: width (view zenith) and height (height), each 16 to 1024. */
	uint32_t transmittance_width;
	uint32_t transmittance_height;
	/** The multiple-scattering table's side: 8 to 128. */
	uint32_t multiple_scattering_size;
	/** Steps a transmittance texel's march takes: 4 to 256 (the paper's 40). */
	uint32_t transmittance_steps;
	/** Steps each of a multiple-scattering texel's 64 rays takes: 4 to 256 (20). */
	uint32_t multiple_scattering_steps;
} RBTREND_ATMOSPHERE_DESC;

/**
 * @brief Creates an atmosphere with no medium: until
 * rbtrend_atmosphere_set_medium(), a scene that draws it draws no
 * atmosphere (transmittance 1, nothing scattered).
 *
 * Memory: its two tables, RGBA16F (8 bytes a texel): 256 x 64 and 32 x 32
 * are 136 KiB.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument or
 *         a size out of range, each its own check and message; the struct
 *         codes for desc->base; RBT_RESULT_OUT_OF_MEMORY;
 *         RBT_RESULT_REND_GPU_FAILURE if a table can't be made.
 */
RBT_RESULT rbtrend_atmosphere_create(
	rbtrend_instance instance,
	const RBTREND_ATMOSPHERE_DESC *desc,
	rbtrend_atmosphere *out_atmosphere,
	RBTREND_ERROR *out_error);

/** @brief Destroys an atmosphere once the GPU is past it (rbtrendcontext.h,
 * "Destroying"). No scene may name it afterwards. NULL does nothing. */
void rbtrend_atmosphere_destroy(rbtrend_atmosphere atmosphere);

/**
 * @brief Sets what fills the shell. Copied. Its tables are rebuilt by the
 * next RBTREND_COMMAND_ATMOSPHERE_TABLES recorded for it; until then a
 * scene reads the tables of the medium before.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument or
 *         a field out of its range (each field its own check and message,
 *         in the struct's order); the struct codes for medium->base.
 *         Nothing changes on failure.
 */
RBT_RESULT rbtrend_atmosphere_set_medium(
	rbtrend_atmosphere atmosphere,
	const RBTMATH_SHELL_MEDIUM *medium,
	RBTREND_ERROR *out_error);

/* ========================================================================= */
/* render/rbtrendlit.h                                           [ADDED]     */
/* ========================================================================= */

/**
 * @brief The atmosphere a scene draws, where it is, and the sizes of the
 * scene's own tables. No field has a default.
 */
typedef struct RBTREND_LIT_ATMOSPHERE {
	/** RBT_STRUCT_TYPE_REND_LIT_ATMOSPHERE, and its chain. */
	RBT_STRUCT_BASE base;
	/** The atmosphere, or NULL for none (every other field then unread). It
	 * must outlive its use by the scene, as an environment must. */
	rbtrend_atmosphere atmosphere;
	/** Its centre, in the scene's space: finite. Set as often as it moves. */
	float center[3];
	/** How many of the scene's directional lights, the first, light the
	 * medium: 1 or 2. A scene with fewer has as many as it has; with none,
	 * nothing lights the medium (a black sky, as a night with no moon). */
	uint32_t sun_count;
	/** The sky-view table: 32 to 1024 each. */
	uint32_t sky_view_width;
	uint32_t sky_view_height;
	/** The aerial volume: each 8 to 128, or all three 0 for none (stage 3
	 * off: surfaces aren't faded). */
	uint32_t aerial_width;
	uint32_t aerial_height;
	uint32_t aerial_depth;
	/** How far from the eye its last slice is, in the scene's unit: finite,
	 * above 0. Beyond it, the last slice's. Unread without a volume. */
	float aerial_distance;
	/** The sky-view's march: the least and most steps a texel takes, more
	 * for a longer ray (the paper's 4 and 14 min and max, Unreal's 'sample
	 * count'): 1 to 128 each, least at most most. */
	uint32_t sky_view_min_steps;
	uint32_t sky_view_max_steps;
	/** Steps per aerial slice: 1 to 16 (the paper's 1). Unread without a volume. */
	uint32_t aerial_steps_per_slice;
} RBTREND_LIT_ATMOSPHERE;

/**
 * @brief Sets the atmosphere a scene draws, or none. The scene's sky-view
 * table and aerial volume are made (or remade) here where their sizes
 * differ from those it holds; the same sizes again allocate nothing, so it
 * is called each frame as the centre moves.
 *
 * With an atmosphere, after the frame's RBTREND_COMMAND_LIT_ATMOSPHERE:
 * the sky shader draws the sky-view table; each of the first sun_count
 * directional lights reaches a surface and its disc through the
 * transmittance; each lit surface is faded by the aerial volume where it
 * has one. With none, nothing of this is read.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT (each field its own
 *         check and message); the struct codes; RBT_RESULT_UNSUPPORTED for
 *         a volume on a device without 3D storage images;
 *         RBT_RESULT_OUT_OF_MEMORY; RBT_RESULT_REND_GPU_FAILURE. Nothing
 *         changes on failure.
 */
RBT_RESULT rbtrend_lit_scene_set_atmosphere(
	rbtrend_lit_scene scene,
	const RBTREND_LIT_ATMOSPHERE *atmosphere,
	RBTREND_ERROR *out_error);

/* ========================================================================= */
/* render/rbtrendcommands.h                                      [ADDED]     */
/* ========================================================================= */

/*
 *	RBTREND_COMMAND_ATMOSPHERE_TABLES           = 48,
 *	    Outside a pass: rebuilds `_atmosphere`'s transmittance table, then
 *	    its multiple-scattering table, where its medium changed since they
 *	    were last built; records nothing otherwise.
 *	    struct { rbtrend_atmosphere _atmosphere; } _atmosphere_tables;
 *
 *	RBTREND_COMMAND_LIT_ATMOSPHERE              = 49,
 *	    Outside a pass, after `_scene`'s view, lights and atmosphere are set:
 *	    its sky-view table and aerial volume worked out for its view, from
 *	    its atmosphere's tables. Nothing for a scene with none.
 *	    struct { rbtrend_lit_scene _scene; } _lit_atmosphere;
 *
 *	RBTREND_COMMAND_LIT_ATMOSPHERE_ENVIRONMENT  = 50,
 *	    Outside a pass, after the scene's LIT_ATMOSPHERE: fills
 *	    `_environment` from `_scene`'s sky-view table, then prefilters it as
 *	    LIT_SKY's fill is. As costly (rbtrendlit.h): for a sky that moved
 *	    enough, not every frame.
 *	    struct { rbtrend_lit_environment _environment; rbtrend_lit_scene _scene; } _lit_atmosphere_environment;
 */

/* ========================================================================= */
/* rbtcore.h                                                     [ADDED]     */
/* ========================================================================= */

/*
 *	RBT_STRUCT_TYPE_REND_ATMOSPHERE_DESC   = 5036,
 *	RBT_STRUCT_TYPE_REND_LIT_ATMOSPHERE    = 5037,
 *	(The medium is Rabbittus Math's frozen value: no struct type.)
 *	RABBITTUS_VERSION_MINOR 2 -> 3.
 */

/*
 * ---------------------------------------------------------------------------
 * Questions for the user
 * ---------------------------------------------------------------------------
 *
 * B1. ANSWERED 2026-11-16: the smallest shell that holds the camera
 *     (the caller's rule); Rend draws what it is given.
 * B2. ANSWERED 2026-11-16: refilled by the caller's rule (Draft A's
 *     angle, the caller's refill height). Was: Whether the environment (lit surfaces' ambient and reflections)
 *     follows the atmosphere at a refill rule the caller sets (recommended:
 *     yes, the same rule as Draft A's, with a height), or every frame (5
 *     ms), or never (surfaces under the analytic sky's light while the sky
 *     behind is the atmosphere's: they would disagree at sunset).
 * B3. ANSWERED 2026-11-16: the medium and its CPU functions in Rabbittus
 *     Math (the user's line: pure maths there, rendered graphics in Rend);
 *     first recommended the other way, and changed.
 * B4. ANSWERED 2026-11-16: stage 4 moves before L7, its own session after
 *     stage 3 (card L62's Decided line changes).
 *
 * ---------------------------------------------------------------------------
 * Critique
 * ---------------------------------------------------------------------------
 *
 * Seven passes, 2026-11-16, over both halves (this file and the caller's
 * own draft) as each pass found them.
 *
 * Pass 1 (the model and its units). 1. The caller's half told a large world to
 * work "in kilometres" for precision, and Rend's examples gave the earth in
 * kilometres: a float's precision is relative, so the unit buys nothing
 * (the earth in metres keeps a height to half a metre near the ground).
 * Changed: the medium is in the scene's unit, both halves say why, no
 * scale between them. 2. Checked: the sun's illuminance pi * _radiance
 * follows rbtrendlit.h's scale (a matte white face reads _radiance), so
 * the sky and the surfaces agree.
 *
 * Pass 2 (constants at a point of use). 1. The marches' step counts were
 * the paper's constants inside the shaders: a caller trading cost for
 * quality (a small viewport, a phone) would need to change them, which
 * CONVENTIONS.md's "Limits come from data" forbids hiding. Changed: the
 * tables' steps in the atmosphere's desc, the sky-view's and aerial's in
 * the scene's; the 64 directions of the multiple-scattering stay a named
 * constant, being the technique's own sphere, not a cost.
 *
 * Pass 3 (does "the same pixels" hold where no medium is used?). 1. Metal
 * compiles with fast math, so new code beside today's may reorder today's
 * arithmetic even untaken. Changed: the fallback named (an atmosphere
 * variant of each shader, the old ones byte for byte), never a looser
 * comparison. 2. The lit scene's set 0 gains bindings: callers name the
 * layout Rend gives, none known builds it by
 * hand; listed as a break all the same.
 *
 * Pass 4 (who writes what, when; frames in flight). Looked at the order in
 * section 3, the barriers (Rend's tracked states, as BLOOM's and the
 * shadow atlas's per-frame writes), one queue ordering frame N+1's
 * sky-view writes after frame N's reads, a table read before it was ever
 * built (cleared at creation's first compile), a stale sky-view (a Debug
 * report). Found nothing to change.
 *
 * Pass 5 (one frame for the environment, with Draft A). 1. The environment
 * is filled in a frame, and neither half said which: a fill in a fixed
 * frame would have to happen whenever the sun's bearing moved round the
 * eye. Changed: Rend fills it in the frame the scene's ambient names; the
 * caller sets the eye's local frame facing the sun's bearing (Draft A's
 * 4a), so only the elevation and the height fill it; the caller's refill
 * height covers the height.
 *
 * Pass 6 (from outside, and stage 4). 1. "Climbing out keeps a sky" was
 * true just above the shell and said as if for anywhere outside. Changed:
 * 2b says where the sky-view stops working and what stage 4 is (the
 * paper's per-pixel march, blended). 2. Found: that blend is Rend's own
 * full-screen pass, not L7's transparency, so stage 4 needn't wait for L7.
 * Changed: B4 asks.
 *
 * Pass 7 (what a reviewer would refuse). 1. "RBTREND_LIT_ATMOSPHERE mixes a
 * per-frame centre with sizes that allocate": kept, because the same sizes
 * allocate nothing and one call keeps a scene's atmosphere whole (a scene's
 * environment is set the same way, with its object); said on the call.
 * 2. "sun_count reads the first lights, not flagged ones": the light
 * struct is frozen (an array element), so a flag would be a new light type
 * for one bit; the first-lights rule is the sky's own (its sun is the
 * first). Kept. 3. "Is a third CPU function owed, the in-scattered light
 * along a ray, for a star's sky behind it?" The sky's brightness behind a
 * star is the sky-view's, read on the GPU; on the CPU a game reads the
 * transmittance and works out a contrast (a game's own rule). Not added;
 * named as possible if a second caller asks.
  *
 * Second round, 2026-11-16, seven passes over the change B3 made (the
 * medium and its CPU functions moved to Rabbittus Math):
 *
 * Pass 1 (is the line where the user drew it?). Checked each piece: the
 * medium, density, transmittance and phase functions answer questions with
 * no picture (Math); the multiple-scattering term, though maths, is an
 * approximation for drawing a sky cheaply, not a quantity a caller asks
 * (Rend); the tables, sky-view, volume, fill and their sample counts are
 * GPU work traded against frame time (Rend). 1. The phase functions were
 * left in the shaders only. Changed: rbtmath_shell_medium_compute_phase, so the
 * whole pure model is in one place.
 * Pass 2 (layering). Rend includes Rabbittus Math's header and takes its
 * value struct, as it takes Rbt* types; Math includes nothing of Rend's.
 * Nothing to change.
 * Pass 3 (Math's conventions). 1. The medium was a based struct (Rend's
 * descs' scheme); Math's values are frozen, a new need a new type.
 * Changed: frozen, no struct type (5037 now the lit atmosphere's). 2.
 * Errors: RBTMATH_ERROR, not RBTREND_ERROR. Changed.
 * Pass 4 (precision). Math's ray queries are double; the medium's are
 * float, as the shaders and a game's positions are: said why in 5, and
 * the bit-for-bit test covers the float path.
 * Pass 5 (names). "Atmosphere" stays Rend's word for what it draws; Math's
 * is a shell medium, its fields named after the scattering they model
 * (Rayleigh, Mie), physics' terms, not a planet's. Kept.
 * Pass 6 (the caller). Its queries now call Rabbittus Math, not Rend; its
 * component's fields unchanged. The caller's half updated.
 * Pass 7 (tests). The GPU's tables against Math's functions; Math's own
 * tests beside its others. Nothing else moves.
 *
 * Third round, 2026-11-16, at the user's asking, seven fresh passes over
 * both halves as the answers and the B3 round left them:
 *
 * Pass 1 (between steps). 1. A world moved every step (a frame that
 * follows a star) would have its atmosphere's centre step while the camera
 * is drawn blended. Changed (the caller's half): the centre blended by the
 * frame's fraction as an entity's place is, cut by a teleport. Rend takes
 * whatever centre it is given each frame: no change here.
 * Pass 2 (fewer suns than asked). 1. sun_count 2 with one directional light
 * was unsaid. Changed: as many as the scene has; none, a black sky.
 * Pass 3 (arithmetic). 1. The tables' memory was 139 KiB; 256 x 64 x 8 +
 * 32 x 32 x 8 bytes is 136 KiB. Changed, both halves.
 * Pass 4 (defaults that cost). 1. The caller's refill height defaulted to 0, so
 * a target given atmosphere settings and nothing else would refill its
 * environment every frame its camera rose or fell, 5 ms each: a default
 * that costs a frame rate. Changed (the caller's half): the height is a share
 * of the shell's thickness, default 0.01 (a hundredth of the air's depth,
 * any world's size). The angle stays the target's (Draft A), its 0 today's
 * rule, which a game sets.
 * Pass 5 (the lit pass's reads). Looked at: one transmittance read per
 * directional sun per lit texel, one volume read per texel, the sky shader
 * one read; the scene's uniform block's growth under its budget. Nothing
 * changed.
 * Pass 6 (Math and Rend, the user's line). Looked at again: nothing of
 * the GPU in rbtmathmedium.h, nothing of the medium's definition in Rend.
 * Nothing changed.
 * Pass 7 (approval). The questions answered, the ABI listed, the tests,
 * the measurements owed, stage 4's order changed (B4). Nothing further.
 */
