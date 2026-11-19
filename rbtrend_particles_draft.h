/*
 * rbtrend_particles_draft.h - particles simulated on the GPU by Rend:
 * spawned, moved by forces and curl noise, collided with the depth buffer,
 * aged and killed, sorted for blending and drawn as billboards, streaks,
 * meshes or ribbons lit by the lit scene, never read back; each system's
 * look a fixed set of modules with curves, the caller's data; its
 * particles kept relative to an anchor whose place from the eye the caller
 * gives each step (RBTREND_TODO.md item 57; card L73 in DECISIONS.md,
 * which names the session that asked). Its caller's own half is drafted
 * beside the caller's headers.
 *
 * APPROVED 2026-11-18 with the user, every question as recommended.
 * DRAFT, 2026-11-18, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built yet. Not in any target, not
 * compilable. Needs, before it is built: Carrottus GPU's indirect dispatch
 * (RBTREND_TODO.md item 34, decided with card A15 2026-10-28; a Carrottus
 * draft first, not yet written: vkCmdDispatchIndirect, D3D12's
 * ExecuteIndirect with a dispatch signature, Metal's
 * dispatchThreadgroupsWithIndirectBuffer:), and Rend's command of it
 * (DISPATCH_INDIRECT, a small Rend draft after it); a GPU test of a 3D
 * texture sampled by compute on three backends (the one card A57 needs).
 * Written on the MacBook (M5 Pro) against Rabbittus c79f7d6 (0.3), read
 * 2026-11-18: include/render/rbtrendcommands.h (commands 0-51, the
 * 120-byte parameters, SET_COMPUTE_PIPELINE and DISPATCH 15-16,
 * DRAW_INDIRECT 17, arrays copied at the send, LIT_ATMOSPHERE_SHELLS' as
 * the model of a command over an array), rbtrendlit.h ("Sets": set 0 the
 * scene's, 1 the material's, 2 and 3 the caller's; "A caller's vertex
 * shader", locations 0-4; RBTREND_LIT_SHADER, a typedef uint32_t and an
 * anonymous enum; rbtrend_lit_get_shader; RBTREND_LIT_MATERIAL_VALUES;
 * rbtrend_lit_scene_set_scene_color, a borrowed view on a scene),
 * rbtrendatmosphere.h (an object with a desc, a create, a setter and a
 * command that rebuilds what changed: the model of this one),
 * rbtrendbuffers.h, rbtrendimages.h (3D textures), rbtcore.h (struct types
 * to 5040); RBTREND_TODO.md items 14 (compute: storage barriers follow
 * what the shaders write; no storage buffer readback; direct dispatch
 * only), 15 (indirect draws: a storage buffer that is also an argument
 * buffer is tracked between its two states; one state a pass) and 34;
 * shaders/rbtrend_lit_bindings.slang (set 0's sixteen bindings: full),
 * rbtrend_lit_surface.slang (lit_shade); and the approved drafts beside
 * it: rbtrend_large_worlds_draft.h (card L66: the scene's space the
 * eye's), rbtrend_lit_layers_draft.h (L68: a lit fragment shader of
 * Rend's over a caller's vertex shader, version 4), and the decided lines
 * of cards A57 (clouds) and A59 (fog: its froxel grid "applied to every
 * surface and to transparents"), drafted beside this by another session,
 * not read. Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel,
 * present; src/Hazel searched for particle, emitter, sort, indirect):
 * no particle system; its PipelineSpecification.h has a DrawIndirect stage
 * flag and no indirect dispatch; Hazelnut's ExplosiveParticle.hprefab is
 * an entity with a mesh and a point light (gameplay's particles as
 * entities, card L73's Q1a's other half). Nothing in Hazel to measure this
 * against.
 *
 * ===========================================================================
 * Revision notes (seven passes, 2026-11-18, each fixed before the next)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: a system's particles in "the anchor's space" moved with the
 *    anchor, so smoke left by a flying ship flew with it. Two spaces, each
 *    the caller's choice: WORLD (each particle stays where it is in the
 *    world as the anchor moves: the step's `_anchor_moved` is taken off
 *    every particle, exact for a camera's grid cell, whose moves are whole
 *    cells) and EMITTER (particles move and turn with their emitter: an
 *    engine's flame, a ship's shield).
 * 2. Missing: wind. A drag towards a still air leaves rain falling
 *    straight in a gale. Changed: drag pulls a particle's velocity towards
 *    the step's `_wind` (the air's velocity at the anchor, the caller's),
 *    as Unreal's Niagara's Drag with wind velocity and Unity's External
 *    Forces with a wind zone do.
 * 3. Missing: precipitation round a moving camera, card L74's first use. A
 *    box of rain re-spawned as the camera moves wastes its particles; a box
 *    that wraps keeps them. Added: the WRAP module (positions wrapped into
 *    a box round the anchor), the standard rain and snow box (Unreal's and
 *    Unity's camera-relative precipitation).
 * 4. More than needed: an "update rate" and "fixed step" per system. Struck:
 *    the caller gives each step's time; a caller that wants a fixed step
 *    sends several steps. A per-system spawn rate and bursts: struck from
 *    Rend; how many to spawn this step is the caller's arithmetic (its
 *    emitter's rate, bursts, distance moved), Rend spawns what it is told.
 * 5. More than needed: a draw command that also binds a pipeline of Rend's
 *    choosing. Struck: as for lit surfaces, Rend ships the shaders and the
 *    caller makes the pipeline (its blend, its target); Rend's draw command
 *    binds the system's own set and draws indirectly, nothing more.
 * 6. Missing: how a caller knows a system is spent (an explosion done, its
 *    entity to remove) with nothing read back. Added: a lagged alive count,
 *    read from the last completed submission (Q3), never gameplay's.
 *
 * Pass 2 (industry standard).
 * 1. Compared (section 8): Unreal's Niagara (GPU emitters, modules on a
 *    stack, curves, depth-buffer collision, sorting, sprite, mesh and
 *    ribbon renderers, local or world space, fixed bounds); Unity's VFX
 *    Graph (GPU systems by capacity, contexts Spawn/Initialize/Update/
 *    Output, a 3D vector field, depth collision, sorting, camera-relative
 *    simulation) and its Shuriken (CPU modules, gradients of at most 8
 *    keys); Godot 4's GPUParticles3D (a process material of fixed
 *    modules, curves, attractors, collision from a heightfield or an SDF,
 *    a "local coords" flag); Frostbite ("GPU-based particles", Battlefield
 *    3 and later; Gareth Thomas's "Compute-based GPU particle systems",
 *    GDC 2014, AMD: dead and alive lists, a depth-buffer collision, a
 *    bitonic sort, tiled rendering). Kept from them: the dead and alive
 *    lists of Thomas's talk, depth collision, modules of fixed meaning (no
 *    graph, card L73 Q2a), curves of 8 keys (Shuriken's gradient limit),
 *    the two spaces, the four kinds of draw.
 * 2. The sort: Thomas's and Unity's bitonic sort was compared with AMD's
 *    FidelityFX Parallel Sort (a reduce-then-scan radix sort, portable,
 *    no forward-progress guarantee needed, which Metal doesn't give and a
 *    one-sweep sort assumes) and taken: a 16-bit key in four passes of 4
 *    bits, each three dispatches, all indirect over the alive count.
 * 3. Lighting translucent particles: per pixel through Rend's lit
 *    surface (HDRP's lit particles; Niagara's "Surface ForwardShading"),
 *    over per-vertex (Unreal's "Volumetric Per Vertex NonDirectional") and
 *    a lighting volume (Frostbite's, Unreal's translucency volume): per
 *    pixel shares the one lighting and its shadows; a volume comes with
 *    A59's froxel grid, which the decided line already applies to
 *    transparents (Q2).
 *
 * Pass 3 (the C API).
 * 1. The effect is a based struct with each module a based struct
 *    chained onto it, its presence turning the module on: the struct
 *    bases' growth (a later module is a new struct; a module's later field
 *    a new struct chained to it), each registered, refused twice in one
 *    chain (RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN). The step is a frozen
 *    array element, sent in an array, as LIT_ATMOSPHERE_SHELLS' shells.
 * 2. Enums: RBT_ENUM_BASE with a zero "not chosen" that is refused, as a
 *    new header's are (rbtcore.h); the shaders' list follows rbtrendlit.h's
 *    neighbour form (typedef uint32_t and an anonymous enum), since it is a
 *    shader list of the same shape.
 * 3. Every pointer says borrowed, copied or kept; the noise and depth
 *    views are borrowed as a scene's scene colour is.
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. A step of many systems is one command: every system's simulate
 *    dispatches recorded together, then one barrier, then every spawn,
 *    then one, and so on (section 4), one pipeline a stage for every
 *    system (the modules are data in each system's block, not shader
 *    variants), so a hundred systems cost a hundred dispatches a stage and
 *    a handful of barriers, not a hundred pipelines.
 * 2. A particle is 48 bytes in three float4s, structure of arrays where a
 *    stage reads a part alone (the sort's keys apart from the state; the
 *    draw reading the state through the sorted indices). Section 6.
 * 3. Allocation: every buffer at create; set_effect copies into a fixed
 *    block; the command copies its steps into the list's side storage;
 *    the steady state allocates nothing, held by test_frame_allocations.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Threads: one caller at a time on a system, with its instance
 *    (rbtrendcontext.h's rule); the lagged count's read is an ordinary
 *    call on the system.
 * 2. Determinism: none across GPUs or runs. A dead slot is taken by an
 *    atomic, so which slot a particle takes and the alive list's order
 *    differ from run to run; a sorted system's draw order is its keys',
 *    ties apart. Random values are a hash of (the step's `_seed`, the
 *    particle's spawn number), so a particle's start doesn't depend on
 *    its slot. Never read back but for the lagged count (Q3), which is a
 *    count, not a picture: no gameplay depends on a particle (card L73
 *    Q1a).
 * 3. Errors: create and set calls RBT_RESULT and RBTREND_ERROR, each check
 *    its own message in the struct's order; the commands at the send and
 *    the compile through the list's report. Nothing printed.
 *
 * Pass 6 (the code as built, re-read 2026-11-18).
 * 1. Set 0 is full (rbtrend_lit_bindings.slang: sixteen bindings,
 *    CRGPU_MAX_DESCRIPTOR_BINDINGS_PER_SET), so a particle's buffers and
 *    the depth for soft fading can't go there: they are the system's own
 *    set at slot 2, RBTREND_PARTICLES_SET, which a caller's particle
 *    pipeline names after Rend's two (rbtrendlit.h, "Sets": 2 and 3 were
 *    the caller's; with a particle pipeline 2 is Rend's, 3 still the
 *    caller's).
 * 2. Item 15: a buffer is in one state for a whole pass, and an argument
 *    buffer's state is tracked only when it is also a storage buffer. The
 *    draw arguments are written by the simulate's last stage (storage) and
 *    read by DRAW_PARTICLES (indirect) in a later pass: the tracked case.
 *    The particles' state is read by the draw's vertex shader (storage,
 *    read) in the pass and written only outside passes.
 * 3. Item 14: no storage buffer readback. The lagged count needs one
 *    four-byte copy a step into a host-visible buffer, one per frame in
 *    flight (the frames' rule, item 12), read when that frame's fence has
 *    passed: an internal readback of Rend's own, not a public one.
 * 4. The command's parameters stay inside 120 bytes: PARTICLES_STEP's are
 *    32 (a scene, a depth, the steps' pointer and count; the depth's
 *    direction is the system's, set_depth's); DRAW_PARTICLES' 8.
 * 5. The struct types after rbtcore.h's 5040 (LIT_SCENE_DESC) and the
 *    numbers the approved drafts have taken (L68's 5039; the view sets'
 *    and others' as built first): named here by role, numbered when built,
 *    as L67's asset kind was.
 * 6. Commands 52 and 53, or the next free when built (L68's and A57's
 *    drafts may take some first).
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Tests (section 9), each with what it holds; the GPU ones read pixels
 *    and the lagged count, never the particles.
 * 2. No format: Rend saves nothing; the caller's cooked effect is its own.
 * 3. ABI: a new header in rbtrend.h's umbrella, additions only; listed by
 *    ./check.sh --update-abi when built. No version raise while 0.3 is
 *    untagged (card X2). The build's order and records: section 10.
 * 4. Found counting the declarations against section 7: it said eleven
 *    based structs and five calls; there are fourteen (the desc, the
 *    effect, twelve modules) and eight calls. Corrected, with the struct
 *    types' list.
 *
 * ===========================================================================
 * 1. Why, and who else uses it
 * ===========================================================================
 *
 * Smoke, sparks, dust, fire, rain, snow, a ship's exhaust, a spell's
 * motes: thousands to a million short-lived points that look alive and
 * that no gameplay reads. On the CPU each costs a write a frame and an
 * upload; on the GPU they cost a few dispatches a frame and nothing on the
 * CPU (card L73 Q1a). What gameplay reads (a shell's fragments that hurt,
 * pickups) stays the caller's own objects.
 *
 * Any caller of Rend that draws moving matter: a game engine's effects
 * (its first), a CAD application's flow display (particles along a fan's
 * airflow, a fountain in an architectural walkthrough), a scientific
 * viewer's tracers. Nothing here knows what an effect is for.
 *
 * ===========================================================================
 * 2. What a system is
 * ===========================================================================
 *
 * An rbtrend_particles is a system: a pool of `capacity` particles on the
 * device, its effect (the modules, copied into its block), and the noise
 * and depth views it borrows. Each step the caller sends it (in an
 * RBTREND_PARTICLES_STEP):
 * - the time since its last step, `_delta_time`;
 * - how many to spawn, `_spawn_count` (the GPU spawns as many as it has
 *   dead slots for, the rest dropped);
 * - a seed, `_seed`, for this step's random values;
 * - where its anchor is from the eye, `_anchor` (the scene's space: for a
 *   caller drawing relative to the eye, its anchor's place less the eye's,
 *   from its own precision), and how far the anchor moved since its last
 *   step, `_anchor_moved` (in the same axes, from the caller's own
 *   precision, so a far anchor's motion isn't the difference of two
 *   rounded floats);
 * - its emitter's transform from the anchor, `_emitter` (three float rows,
 *   a 3x4 matrix: where it spawns, which way, how large), and its
 *   velocity, `_emitter_velocity`;
 * - the air's velocity at the anchor, `_wind`;
 * - whether to kill every particle first, `_clear`.
 * Every particle's place is kept relative to the anchor, so it is small
 * wherever the anchor is, and the anchor is where the caller says: an
 * emitter's place (an explosion), or a grid cell round the camera (rain),
 * which moves by whole cells (card L73 Q3a).
 *
 * THE MODULES (each a based struct chained onto RBTREND_PARTICLE_EFFECT;
 * absent, off), in the order a step runs them:
 * - SPAWN: where and how a particle starts: a shape (a point, a sphere, a
 *   box, a cone, a disc) in the emitter's space, filled or on its surface,
 *   a speed range along the shape's normal or the emitter's +Y spread by a
 *   cone's angle, the emitter's velocity times a factor. Without it,
 *   particles start at the emitter's origin, still.
 * - FORCES: a constant acceleration (gravity, in the scene's axes) and a
 *   drag towards the wind.
 * - NOISE: curl noise from the system's noise volume (a borrowed 3D
 *   texture of a tiling vector field the caller made, card A57's way:
 *   baked from Rabbittus Math's noise on its own jobs and uploaded): a
 *   strength, a frequency, a scroll.
 * - COLLISION: against the depth the system borrows, through the step's
 *   scene's view: a particle that crossed the depth surface (within a
 *   thickness, so a far wall doesn't catch what is behind it) is put back
 *   on it, its velocity reflected by a restitution and slowed by a
 *   friction along it, and its life shortened by a set fraction (1 kills).
 * - WRAP: a box round the anchor its places are wrapped into
 *   (precipitation round a camera).
 * - COLOR_OVER_LIFE, SIZE_OVER_LIFE: curves over a particle's life (0 at
 *   birth, 1 at death), of at most RBTREND_PARTICLE_CURVE_MAX_KEYS keys,
 *   linear between, multiplying the effect's colour and size.
 * - FLIPBOOK: a grid of frames in the material's base colour map, played
 *   over life or at a rate, blended between frames or not.
 * - SOFT: a fade where a particle meets the depth, over a distance (soft
 *   particles).
 * - STREAK, MESH, RIBBON: what the effect's draw kind needs (a streak's
 *   length per speed; a mesh's index range; a ribbon's width).
 * The effect itself: the life's range, the space (WORLD or EMITTER), the
 * draw kind (BILLBOARD, STREAK, MESH, RIBBON), whether to sort, the
 * starting colour, size, rotation and spin ranges.
 *
 * Changing the effect (rbtrend_particles_set_effect) applies to every
 * particle from the next step; the pool is kept. A different capacity is a
 * different system.
 *
 * ===========================================================================
 * 3. Drawing
 * ===========================================================================
 *
 * As for lit surfaces (rbtrendlit.h, "What is whose"), Rend ships the
 * shaders and the caller makes the pipeline: Rend's particle vertex shader
 * (RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX for billboards, streaks and
 * ribbons, _MESH_VERTEX for meshes of RBTREND_LIT_VERTEX) and fragment
 * shader (_LIT_FRAGMENT, the lit surface's lighting with the soft fade and
 * the flipbook's blend; _UNLIT_FRAGMENT, colour times the map, emissive,
 * exposed, for additive sparks), sets 0 and 1 the lit scene's and a lit
 * material's (its base colour map the sprite or flipbook, its emission),
 * set 2 the system's (rbtrend_particles_get_layout()), and the blend the
 * caller's (premultiplied alpha for smoke, sorted; additive for sparks and
 * rain, unsorted). The vertex shader writes locations 0 to 4 as a caller's
 * vertex shader does: the place (the particle's, plus the anchor, plus its
 * quad's corner turned to the eye, or along its velocity for a streak, or
 * a mesh's vertex scaled and turned), a normal facing the eye (a mesh's
 * own), the flipbook frame's texture coordinate, no tangent (a mesh's
 * own), and the colour over life.
 *
 * Inside the caller's pass, after setting its pipeline and sets 0 and 1,
 * the caller records RBTREND_COMMAND_DRAW_PARTICLES for a system: it binds
 * the system's set at 2 and draws indirectly from the arguments the step
 * wrote (six vertices a particle, or the mesh's indices a particle, or a
 * strip between neighbours for a ribbon). The caller's transparent
 * surfaces and particle systems are drawn in the caller's order (systems
 * against each other by their anchors' distance, a caller's choice; a
 * system's particles against each other by its sort).
 *
 * Lit as a surface is: by the scene's directional lights and their
 * shadows, its point and spot lights (the tile's), its ambient light and
 * environment, faded by its atmosphere's aerial volume, as any lit surface
 * (Q2). Particles cast no shadow (a later stage, if asked: Niagara's and
 * VFX Graph's are opt-in).
 *
 * ===========================================================================
 * 4. A step on the GPU (RBTREND_COMMAND_PARTICLES_STEP)
 * ===========================================================================
 *
 * Outside a pass, after the depth the systems collide with is drawn (the
 * frame's opaque surfaces) and before the passes that draw them. For the
 * steps in the array, each stage recorded for every system before the
 * next stage, a barrier between stages (Rend's tracking, item 14):
 * 1. CLEAR, for a step with `_clear`: the alive count 0, every slot dead.
 * 2. SIMULATE, indirect over last step's alive list: age each; kill the
 *    old (its slot pushed on the dead list); else move it (the forces,
 *    noise, drag towards the wind, the anchor's move in WORLD space, the
 *    wrap), collide it, and append it to this step's alive list.
 * 3. SPAWN, direct over `_spawn_count` (known on the CPU): pop a dead slot
 *    (an atomic; none left, none spawned), start it by the SPAWN module
 *    and the step's hashed random values, append it.
 * 4. ARGUMENTS, one thread a system: this step's draw arguments and next
 *    step's simulate dispatch from the alive count; the count copied to
 *    the lagged readback.
 * 5. SORT, for a system whose effect sorts: a 16-bit key of each alive
 *    particle's distance from the eye (its quantised log), radix-sorted
 *    (four passes of 4 bits, a count, a scan and a scatter each, indirect
 *    over the alive count), back to front.
 * Its scene gives the eye, the view direction and the view-projection the
 * collision and the sort read; its depth is the one the systems collide
 * with (the depth each system borrows must be it, or NULL: checked at the
 * compile).
 *
 * ===========================================================================
 * 5. Precision and space
 * ===========================================================================
 *
 * A particle's place is a float relative to its anchor: within a system's
 * reach (a few hundred metres for weather's box, tens for an explosion) a
 * float holds it to a micrometre. The anchor from the eye is the caller's
 * float, rounded once from its own precision (card L66's rule), so a
 * system a thousand kilometres off is drawn where it is, its particles
 * still exact against each other. In WORLD space the anchor's motion is
 * taken off each particle each step: a grid cell's move is a whole number
 * of cells, exact in float; an emitter's anchor moving at 3 km/s takes 50 m
 * a step off each particle, whose rounding (2^-24 relative) is 3 microns.
 *
 * ===========================================================================
 * 6. Memory, costs, with their method
 * ===========================================================================
 *
 * A system of capacity N: the state, 48 bytes a particle (place and age,
 * velocity and life, size, rotation, spin and spawn number); two alive
 * lists and the dead list, 4 bytes each; the sort's keys and values,
 * double-buffered, 12 bytes (a 16-bit key padded to 32 and a 32-bit
 * index, twice, less the shared); the per-group counts of the radix
 * passes, 64 bytes a group of 256; the block (the effect, about 640 bytes:
 * three curves of 8 keys of 4 floats, 384, and the modules' values); the
 * arguments, 64 bytes; the readback, 4 bytes a frame in flight. About 76
 * bytes a particle: 4.8 MiB at 65,536, 76 MiB at the most, 1,048,576.
 *
 * GPU time, estimated (to be measured when built: Release, the M5 Pro, no
 * validation, submit to idle less the same frame without, means of 50
 * after 10, 1440p):
 * - simulate and spawn: about 150 ALU operations, one noise and one depth
 *   sample and 100 bytes moved a particle: 100,000 particles move 10 MB,
 *   0.03 to 0.05 ms at a few hundred GB/s, the ALU negligible;
 * - a step's fixed cost: about 5 microseconds a dispatch on Apple's GPUs
 *   (Rend's aerial pass, 32 passes in 0.94 ms, bounds it from above at 30
 *   a pass, fragment passes with targets); an unsorted system 3
 *   dispatches, a sorted one 15: 100 unsorted systems about 1.5 ms of
 *   dispatches unless batched (they are, by stage: overlapping on the GPU,
 *   about 0.3 ms estimated), 10 sorted ones about 0.75 ms;
 * - the draw: fill rate, the caller's (overdraw); rain of 50,000 thin
 *   streaks at 1440p 0.2 to 0.4 ms lit, about half that unlit (an
 *   estimate from the lit surface's per-pixel cost on the M5 Pro).
 * The step's batching and the sort's cost are what to measure first.
 *
 * ===========================================================================
 * 7. ABI, versions, threads, determinism, errors
 * ===========================================================================
 *
 * - ABI: a new header, render/rbtrendparticles.h, in rbtrend.h; one
 *   handle, fourteen based structs (the desc, the effect and its twelve
 *   modules), one frozen curve, one frozen step, four enums, a shader
 *   list, eight calls and two commands. Additions only. No version raise while
 *   Rabbittus' 0.3 is untagged (card X2).
 * - Features: indirect dispatch, through Carrottus GPU's feature or call
 *   once its draft says which (named on the commands, refused with
 *   RBT_RESULT_UNSUPPORTED at the compile, in
 *   rbtrend_get_optional_gpu_features() if it is optional on a backend).
 * - Threads: one caller at a time on a system, with everything of its
 *   instance (rbtrendcontext.h, "Threads"). No exception.
 * - Determinism: none promised (pass 5.2); the lagged count exact.
 * - Errors: section 9's T1 lists every refusal.
 *
 * ===========================================================================
 * 8. What was compared (pass 2 has the conclusions)
 * ===========================================================================
 *
 * - Unreal Engine 5's Niagara (GPU compute sim, module stacks, curves,
 *   collision against the scene depth and distance fields, sorting, the
 *   sprite, mesh and ribbon renderers, local space, fixed bounds, the
 *   "Translucency Lighting Mode").
 * - Unity: the VFX Graph (GPU, capacity per system, Spawn, Initialize,
 *   Update and Output contexts, vector fields as 3D textures, depth
 *   collision, camera sorting, world or local space) and the Particle
 *   System (Shuriken: CPU modules, gradients of at most 8 keys); HDRP's
 *   lit particles per pixel.
 * - Godot 4: GPUParticles3D with a ParticleProcessMaterial (fixed modules
 *   and curves, local coords, collision with a heightfield or an SDF,
 *   trails as ribbons).
 * - Frostbite and AMD (Gareth Thomas, "Compute-based GPU Particle
 *   Systems", GDC 2014): dead and alive lists by atomics, depth collision,
 *   sorting, culling into tiles; FidelityFX Parallel Sort for the radix.
 * - Hazel (present): none (head).
 *
 * ===========================================================================
 * 9. Tests (rabbittustests, Metal's validation with warnings fatal, Debug
 * and Release; rbtrend_contract_tests headless; each seen failing first)
 * ===========================================================================
 *
 * T1. rbtrend_contract_tests and refusals: create with each desc field out
 *     of range (capacity 0, past RBTREND_PARTICLES_MAX_CAPACITY; base
 *     wrong); set_effect with each field of each module out of range, each
 *     its own message, in the struct's order; a module chained twice
 *     (RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN); a curve's keys out of order
 *     or past 8; MESH drawn with no MESH module; NOISE with no noise view
 *     at the compile; COLLISION or SOFT with no depth; a step naming a
 *     destroyed system, a system twice, a non-finite value (each its own);
 *     DRAW_PARTICLES outside a pass, of a system never stepped.
 * T2. test_particles_spawn_count: 1,000 spawned into a capacity of 600: the
 *     lagged count 600; after their life, 0; a clear empties it.
 * T3. test_particles_motion: one particle, gravity only, drawn as a point
 *     sprite after 1 s at 60 steps: its texel where the integrator puts it
 *     (semi-implicit Euler, worked out in the test), within one texel.
 * T4. test_particles_world_space: an anchor moved by 1e3 cells while its
 *     particles hang still: drawn in the same texels; EMITTER space: they
 *     move with it.
 * T5. test_particles_wrap: a box of rain round a camera crossing ten cells:
 *     the drawn count in view steady within 2%; none outside the box.
 * T6. test_particles_collision: particles dropped on a floor drawn in depth:
 *     none drawn below it; with restitution 0 and friction 1 they rest on
 *     it; with lifetime_loss 1 none survive a hit.
 * T7. test_particles_sorted: two overlapping layers of opposite colours
 *     drawn premultiplied: the near colour on top whatever their spawn
 *     order.
 * T8. test_particles_lit: a billboard lit by a directional light against a
 *     lit surface facing the eye with the same material: within 1/255;
 *     in a cascade's shadow, dark as the surface is.
 * T9. test_particles_soft and _flipbook: a particle meeting a wall fades
 *     over the distance; a 4 x 4 flipbook shows frame k at life k/16.
 * T10. test_particles_mesh and _ribbon: a mesh drawn per particle at its
 *     size and spin; a ribbon's strip joins its particles in spawn order,
 *     no gap, no crossing to a dead one.
 * T11. test_particles_many: 200 systems in one step command: the same
 *     pixels as 200 commands of one (the batching changes nothing seen).
 * T12. test_frame_allocations: a frame stepping and drawing 20 systems,
 *     sorted and not: 0 of Rend's.
 * T13. test_particles_measure: section 6's figures.
 *
 * ===========================================================================
 * 10. When built
 * ===========================================================================
 *
 * After Carrottus GPU's indirect dispatch (its draft, approval, build) and
 * Rend's DISPATCH_INDIRECT, and the 3D-texture compute test; then this in
 * stages: (1) billboards and streaks, unsorted and sorted, the modules but
 * MESH and RIBBON; (2) meshes; (3) ribbons. Metal built and run, D3D12 and
 * Vulkan written through Carrottus GPU with no backend branch, run in
 * their sessions (card X25). Records: RBTREND_TODO.md item 57; rabbittus/
 * CLAUDE.md's module map (Rend's line); rbtrend.h's umbrella and
 * check.sh umbrella; ./check.sh --update-abi with the additions; the
 * misuse factories (`rbtrend_particles`); ../claude_made_decisions.md.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card L73, Rend's half; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. How many particles to spawn each step: a, the caller's count each
 *     step (recommended: rates, bursts and spawning by distance are the
 *     caller's rules over its own clock; Rend spawns what it is told and
 *     keeps no time); b, a rate and bursts in the effect, Rend counting
 *     (Rend holding an emitter's clock and its fractions).
 * Q2. Lighting translucent particles: a, per pixel through the lit
 *     surface's lighting, shadows included (recommended: one lighting, no
 *     second model; its cost the overdraw's, measured); b, per vertex
 *     (cheaper, flat, no shadow detail); c, a lighting volume, which
 *     waits on A59's froxel grid and is its stage, not this one's.
 * Q3. A count read back: a, the alive count of the last completed
 *     submission, a frame or two late, four bytes a step (recommended:
 *     a caller removes a spent effect and budgets by it; tests read it;
 *     never a picture or a particle); b, nothing read back at all (a
 *     caller times its effects by their longest life).
 * Q4. The noise volume: a, the caller's 3D texture (recommended, as card
 *     A57 decided for clouds: baked from Math's noise on the caller's jobs,
 *     shared between systems); b, a volume Rend bakes on the GPU at first
 *     use from a noise of its own (Rend's noise then differs from Math's).
 */

/* ========================================================================= */
/* render/rbtrendparticles.h (new; in rbtrend.h's umbrella)                  */
/* ========================================================================= */

/*
 * [ADDED] The file comment carries sections 2 to 5 for a caller (what a
 * system is, the modules, drawing, a step, precision), "What is whose"
 * (Rend simulates and ships the shaders; the caller decides when, how
 * many, the pipeline and the order), and @par Threads (the instance's
 * rule, no exception).
 */

#include "rbtrendcommands.h"
#include "rbtrendlit.h"

#if __cplusplus
extern "C" {
#endif

/** The most particles a system holds: 1,048,576 (76 MiB). [ADDED] Why:
 * the alive and dead lists' indices and the sort's index are 32-bit and
 * the sort's per-group counts are sized from it; past a million, a caller
 * splits a system. */
#define RBTREND_PARTICLES_MAX_CAPACITY 1048576u

/** The most keys a curve has. [ADDED] Why: a curve is a fixed record in the
 * system's block; Unity's gradients hold 8, and an effect's curves are
 * shaped by a few. */
#define RBTREND_PARTICLE_CURVE_MAX_KEYS 8u

/** The set slot of a particle pipeline's system, after the lit scene's (0)
 * and the material's (1). [ADDED] Why: set 0 is full; the system's
 * buffers and depth are a set of their own. */
#define RBTREND_PARTICLES_SET 2u

/** The most steps one RBTREND_COMMAND_PARTICLES_STEP takes. [ADDED] Why:
 * the steps are copied into the list's side storage, and a bound keeps a
 * mistaken count from asking for gigabytes; 4096 systems a step is past
 * any frame's. */
#define RBTREND_PARTICLES_MAX_STEPS 4096u

/** [ADDED] A particle system: a pool on the device, an effect, borrowed
 * views. Why: the object the steps and draws name (section 2). */
typedef struct rbtrend_particles_t *rbtrend_particles;

/**
 * @brief A system's pool. No field has a default.
 *
 * [ADDED] Why: a system's one fixed property is its capacity; based, so it
 * can grow (a memory budget's chained struct, card X15's allocator).
 */
typedef struct RBTREND_PARTICLES_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_PARTICLES_DESC, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/** For the debugger and messages; copied. NULL for none. */
	const char *debug_name;
	/** How many particles it holds at most: 1 to RBTREND_PARTICLES_MAX_CAPACITY. */
	uint32_t capacity;
} RBTREND_PARTICLES_DESC;

/**
 * @brief Creates a system with no effect, every particle dead. Until
 * rbtrend_particles_set_effect(), a step spawns nothing and DRAW_PARTICLES
 * draws nothing.
 *
 * Memory: about 76 bytes a particle (section 6's list), made here, on the
 * device; nothing after.
 *
 * @param[in]  instance      Instance it is of.
 * @param[in]  desc          Its capacity. Only read during the call.
 * @param[out] out_particles Receives the system; NULL on failure.
 * @param[out] out_error     Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if an argument is NULL
 *         or the capacity is 0 or past RBTREND_PARTICLES_MAX_CAPACITY, each
 *         its own check; the struct codes for `desc->base`;
 *         RBT_RESULT_OUT_OF_MEMORY; or RBT_RESULT_REND_GPU_FAILURE.
 *
 * [ADDED] Why: the system's lifetime.
 */
RBT_RESULT rbtrend_particles_create(
	rbtrend_instance instance,
	const RBTREND_PARTICLES_DESC *desc,
	rbtrend_particles *out_particles,
	RBTREND_ERROR *out_error);

/**
 * @brief Destroys a system once the GPU is past it (rbtrendcontext.h,
 * "Destroying"). No list may name it afterwards. NULL does nothing.
 *
 * [ADDED] Why: the system's lifetime.
 */
void rbtrend_particles_destroy(rbtrend_particles particles);

/** [ADDED] Where a system's particles live. Why: section 2, pass 1.1. */
typedef enum RBTREND_PARTICLE_SPACE RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_PARTICLE_SPACE_NONE    = 0,
	/** Each stays where it is in the world as its anchor moves (smoke left
	 * behind, rain in a box round a camera). */
	RBTREND_PARTICLE_SPACE_WORLD   = 1,
	/** Each moves and turns with its emitter (a flame on an engine). */
	RBTREND_PARTICLE_SPACE_EMITTER = 2,
} RBTREND_PARTICLE_SPACE;

/** [ADDED] How a system's particles are drawn. Why: section 3; the draw's
 * arguments and the vertex shader's shape follow it. */
typedef enum RBTREND_PARTICLE_DRAW RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_PARTICLE_DRAW_NONE      = 0,
	/** A quad facing the eye, turned by its rotation. */
	RBTREND_PARTICLE_DRAW_BILLBOARD = 1,
	/** A quad along its velocity as seen by the eye, its length from the
	 * STREAK module (rain, sparks). */
	RBTREND_PARTICLE_DRAW_STREAK    = 2,
	/** A mesh of RBTREND_LIT_VERTEX, the MESH module's index range. */
	RBTREND_PARTICLE_DRAW_MESH      = 3,
	/** One strip through the alive particles in spawn order (a trail), the
	 * RIBBON module's width. */
	RBTREND_PARTICLE_DRAW_RIBBON    = 4,
} RBTREND_PARTICLE_DRAW;

/** [ADDED] The SPAWN module's shapes. Why: section 2. */
typedef enum RBTREND_PARTICLE_SHAPE RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTREND_PARTICLE_SHAPE_NONE   = 0,
	/** The emitter's origin. */
	RBTREND_PARTICLE_SHAPE_POINT  = 1,
	/** A sphere of radius size[0]. */
	RBTREND_PARTICLE_SHAPE_SPHERE = 2,
	/** A box of half extents size. */
	RBTREND_PARTICLE_SHAPE_BOX    = 3,
	/** A cone along +Y, its base's radius size[0] and its height size[1]. */
	RBTREND_PARTICLE_SHAPE_CONE   = 4,
	/** A disc in the emitter's XZ plane of radius size[0]. */
	RBTREND_PARTICLE_SHAPE_DISC   = 5,
} RBTREND_PARTICLE_SHAPE;

/**
 * @brief A curve over a particle's life: keys at times 0 to 1, values
 * linear between them, the first key's before it and the last's after.
 */
/* Frozen: a value inside the modules that hold one; a new kind of curve gets a new type. */
typedef struct RBTREND_PARTICLE_CURVE {
	/** How many keys: 1 to RBTREND_PARTICLE_CURVE_MAX_KEYS. */
	uint32_t _key_count;
	/** Each key's time, 0 to 1, each at least the one before. */
	float _times[RBTREND_PARTICLE_CURVE_MAX_KEYS];
	/** Each key's value: four for a colour (linear RGBA), the first alone
	 * for a scalar. Finite. */
	float _values[RBTREND_PARTICLE_CURVE_MAX_KEYS][4];
} RBTREND_PARTICLE_CURVE;
/* [ADDED] Why: colour and size over life (card L73 Q2a's "curves"). */

/**
 * @brief An effect: what every particle of a system shares, with its
 * modules chained on `base` (each once; absent, off). No field has a
 * default.
 *
 * [ADDED] Why: the caller's data that is the look (card L73 Q2a); the
 * modules chained so each grows on its own.
 */
typedef struct RBTREND_PARTICLE_EFFECT {
	/** This struct's type, RBT_STRUCT_TYPE_REND_PARTICLE_EFFECT, and its chain of modules. */
	RBT_STRUCT_BASE base;
	/** Its space. */
	RBTREND_PARTICLE_SPACE space;
	/** How it is drawn. */
	RBTREND_PARTICLE_DRAW draw;
	/** Whether a step sorts its particles back to front (for blending that
	 * isn't additive). */
	bool sort;
	/** A particle's life, seconds: above 0, the least at most the most. */
	float life_min;
	/** See #life_min. */
	float life_max;
	/** Its colour at birth, linear RGBA, each finite and at least 0 (the
	 * curve multiplies it). */
	float color[4];
	/** Its size at birth, the scene's unit: finite, above 0, the least at
	 * most the most. */
	float size_min;
	/** See #size_min. */
	float size_max;
	/** Its rotation at birth, radians round the axis to the eye: finite. */
	float rotation_min;
	/** See #rotation_min. */
	float rotation_max;
	/** Its spin, radians a second: finite. */
	float spin_min;
	/** See #spin_min. */
	float spin_max;
} RBTREND_PARTICLE_EFFECT;

/** @brief SPAWN (section 2). [ADDED] Why: where and how particles start. */
typedef struct RBTREND_PARTICLE_SPAWN {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_SPAWN, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** Its shape in the emitter's space. */
	RBTREND_PARTICLE_SHAPE shape;
	/** The shape's sizes (above, per shape), finite, at least 0. */
	float size[3];
	/** On the shape's surface only, not filling it. */
	bool surface;
	/** The angle a start's direction spreads from the shape's normal (the
	 * emitter's +Y for a point or a box), radians, 0 to pi. */
	float spread;
	/** The start's speed, the scene's unit a second: finite, at least 0,
	 * the least at most the most. */
	float speed_min;
	/** See #speed_min. */
	float speed_max;
	/** How much of the emitter's velocity a particle starts with: finite. */
	float inherit_velocity;
} RBTREND_PARTICLE_SPAWN;

/** @brief FORCES. [ADDED] Why: gravity and the air. */
typedef struct RBTREND_PARTICLE_FORCES {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_FORCES, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** A constant acceleration, the scene's axes, the scene's unit a second
	 * squared: finite (gravity, or a buoyancy upwards). */
	float acceleration[3];
	/** How fast a particle's velocity goes to the step's wind, per second:
	 * finite, at least 0 (0 none). */
	float drag;
} RBTREND_PARTICLE_FORCES;

/** @brief NOISE. [ADDED] Why: turbulence (smoke, snow's drift). */
typedef struct RBTREND_PARTICLE_NOISE {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_NOISE, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The velocity it adds at full strength, the scene's unit a second: finite, at least 0. */
	float strength;
	/** How many of the volume's tiles a unit of the scene spans: above 0. */
	float frequency;
	/** How fast the volume moves through the particles, tiles a second: finite. */
	float scroll[3];
} RBTREND_PARTICLE_NOISE;

/** @brief COLLISION, against the system's depth. [ADDED] Why: sparks
 * bouncing, rain stopping at the ground, never read back. */
typedef struct RBTREND_PARTICLE_COLLISION {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_COLLISION, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The part of its speed across the surface it keeps, 0 to 1. */
	float restitution;
	/** The part of its speed along the surface it loses, 0 to 1. */
	float friction;
	/** The part of its life a hit takes, 0 to 1 (1 kills). */
	float life_loss;
	/** How far behind the depth surface a particle is still caught, the
	 * scene's unit: above 0. */
	float thickness;
} RBTREND_PARTICLE_COLLISION;

/** @brief WRAP. [ADDED] Why: precipitation round a camera (pass 1.3). */
typedef struct RBTREND_PARTICLE_WRAP {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_WRAP, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The box's half extents round the anchor, the scene's axes: above 0. */
	float half_extents[3];
} RBTREND_PARTICLE_WRAP;

/** @brief COLOR_OVER_LIFE. [ADDED] Why: a fade, a fire's cooling. */
typedef struct RBTREND_PARTICLE_COLOR_OVER_LIFE {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_COLOR_OVER_LIFE, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** RGBA multiplying the effect's colour; each at least 0. */
	RBTREND_PARTICLE_CURVE curve;
} RBTREND_PARTICLE_COLOR_OVER_LIFE;

/** @brief SIZE_OVER_LIFE. [ADDED] Why: smoke growing, sparks shrinking. */
typedef struct RBTREND_PARTICLE_SIZE_OVER_LIFE {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_SIZE_OVER_LIFE, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The first value multiplying its size; at least 0. */
	RBTREND_PARTICLE_CURVE curve;
} RBTREND_PARTICLE_SIZE_OVER_LIFE;

/** @brief FLIPBOOK, frames in the material's base colour map. [ADDED] Why:
 * animated sprites (fire, explosions). */
typedef struct RBTREND_PARTICLE_FLIPBOOK {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_FLIPBOOK, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The grid's columns and rows, each 1 to 64. */
	uint32_t columns;
	/** See #columns. */
	uint32_t rows;
	/** How many frames, 1 to columns x rows, in rows from the top left. */
	uint32_t frame_count;
	/** Frames a second, above 0; or 0 for the frames spread over its life. */
	float frames_per_second;
	/** Blend each frame into the next. */
	bool blend;
} RBTREND_PARTICLE_FLIPBOOK;

/** @brief SOFT. [ADDED] Why: no hard line where a particle meets a surface. */
typedef struct RBTREND_PARTICLE_SOFT {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_SOFT, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** The distance in front of the depth over which it fades, the scene's unit: above 0. */
	float distance;
} RBTREND_PARTICLE_SOFT;

/** @brief STREAK, for RBTREND_PARTICLE_DRAW_STREAK. [ADDED] Why: rain and sparks drawn along their motion. */
typedef struct RBTREND_PARTICLE_STREAK {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_STREAK, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** Its length per speed, seconds (the distance covered in this time): at least 0. */
	float seconds;
	/** Its length's bounds, the scene's unit: at least 0, the least at most the most. */
	float length_min;
	/** See #length_min. */
	float length_max;
} RBTREND_PARTICLE_STREAK;

/** @brief MESH, for RBTREND_PARTICLE_DRAW_MESH: the range of the index
 * buffer the caller sets before DRAW_PARTICLES. [ADDED] Why: debris,
 * leaves. */
typedef struct RBTREND_PARTICLE_MESH {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_MESH, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** Its indices: above 0, a multiple of 3. */
	uint32_t index_count;
	/** The first. */
	uint32_t first_index;
	/** Added to each index. */
	int32_t vertex_offset;
} RBTREND_PARTICLE_MESH;

/** @brief RIBBON, for RBTREND_PARTICLE_DRAW_RIBBON. [ADDED] Why: trails
 * (stage 3, section 10). */
typedef struct RBTREND_PARTICLE_RIBBON {
	/** RBT_STRUCT_TYPE_REND_PARTICLE_RIBBON, chained onto the effect. */
	RBT_STRUCT_BASE base;
	/** How many times the material's map repeats along the strip's whole length: above 0. */
	float repeats;
} RBTREND_PARTICLE_RIBBON;

/*
 * [ADDED] rbtcore.h: fourteen struct types after 5040 (or the next free
 * when built): REND_PARTICLES_DESC, REND_PARTICLE_EFFECT, and the twelve
 * modules' (SPAWN, FORCES, NOISE, COLLISION, WRAP, COLOR_OVER_LIFE,
 * SIZE_OVER_LIFE, FLIPBOOK, SOFT, STREAK, MESH, RIBBON). Why: every based struct registers its
 * type.
 */

/**
 * @brief Sets a system's effect: copied. Every particle alive takes it
 * from the next step (its life drawn at its birth kept); the pool is kept.
 *
 * @param[in]  particles The system.
 * @param[in]  effect    The effect and its chained modules, each field in
 *                       its range (the structs above). Only read during the
 *                       call.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL argument
 *         or a field out of its range, each its own check and message in
 *         the structs' order (the effect's, then each module's as chained);
 *         the struct codes for any base, RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN
 *         for a module twice; RBT_RESULT_INVALID_ARGUMENT for a draw kind
 *         without its module (MESH, STREAK, RIBBON). Nothing changes on
 *         failure.
 *
 * [ADDED] Why: the look is the caller's data, set when it changes.
 */
RBT_RESULT rbtrend_particles_set_effect(
	rbtrend_particles particles,
	const RBTREND_PARTICLE_EFFECT *effect,
	RBTREND_ERROR *out_error);

/**
 * @brief Sets the noise volume its NOISE module reads, or none.
 *
 * @param[in]  particles The system.
 * @param[in]  noise     A SHADER_RESOURCE 3D view of a tiling vector field
 *                       (its xyz each -1 to 1), of a float or SNORM format
 *                       with three or four channels; NULL for none.
 *                       Borrowed: it outlives its use by the system.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL system, a
 *         view of another instance, not 3D, or of another format, each its
 *         own check. A step of a system whose effect has NOISE and which
 *         has no volume is refused at the compile.
 *
 * [ADDED] Why: Q4a, the volume the caller's.
 */
RBT_RESULT rbtrend_particles_set_noise(
	rbtrend_particles particles,
	rbtrend_texture_view noise,
	RBTREND_ERROR *out_error);

/**
 * @brief Sets the depth its COLLISION and SOFT modules read, or none: the
 * depth the step's scene's view drew, read in steps and draws until set
 * again (one depth a system for every list it is in; a system drawn into
 * two views of two depths fades against the one set).
 *
 * @param[in]  particles      The system.
 * @param[in]  depth          A SHADER_RESOURCE 2D view, DEPTH aspect,
 *                            single-sampled; NULL for none. Borrowed, as a
 *                            scene's scene colour is. Not an attachment of
 *                            the pass DRAW_PARTICLES is in (refused there).
 * @param[in]  reversed_depth It was drawn near at 1, far at 0.
 * @param[out] out_error      Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL system or a
 *         view of another instance, not 2D, not of depth or multisampled,
 *         each its own check.
 *
 * [ADDED] Why: collision and soft fading need the scene's depth; set 0 is
 * full, so the system's own set holds it (pass 6.1).
 */
RBT_RESULT rbtrend_particles_set_depth(
	rbtrend_particles particles,
	rbtrend_texture_view depth,
	bool reversed_depth,
	RBTREND_ERROR *out_error);

/**
 * @brief How many particles were alive after the system's last step whose
 * submission has completed (a frame or two behind the steps sent), and
 * which step that was (the count of steps sent to it, from 1).
 *
 * @param[in]  particles The system.
 * @param[out] out_alive Receives the count; 0 before any step completed.
 * @param[out] out_step  Receives that step's number; 0 for none. May be NULL.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL system or
 *         out_alive.
 *
 * [ADDED] Why: Q3a; a caller learns a system is spent with nothing else
 * read back.
 */
RBT_RESULT rbtrend_particles_get_alive_count(
	rbtrend_particles particles,
	uint32_t *out_alive,
	uint64_t *out_step,
	RBTREND_ERROR *out_error);

/**
 * @brief The layout of a particle pipeline's set RBTREND_PARTICLES_SET: the
 * instance's, as rbtrend_lit_get_scene_layout()'s is (don't destroy it).
 *
 * @return As rbtrend_lit_get_scene_layout().
 *
 * [ADDED] Why: the caller's pipeline names it at slot 2.
 */
RBT_RESULT rbtrend_particles_get_layout(
	rbtrend_instance instance,
	rbtrend_binding_layout *out_layout,
	RBTREND_ERROR *out_error);

/** @brief The shaders Rend ships for particles. [ADDED] Why: section 3. */
typedef uint32_t RBTREND_PARTICLES_SHADER;
enum {
	/** Billboards, streaks and ribbons: no vertex buffer; writes locations
	 * 0 to 4 as rbtrendlit.h's "A caller's vertex shader" says. */
	RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX = 0,
	/** Meshes of RBTREND_LIT_VERTEX (binding 0), one a particle. */
	RBTREND_PARTICLES_SHADER_MESH_VERTEX      = 1,
	/** Lit as RBTREND_LIT_SHADER_SURFACE_FRAGMENT, with the SOFT fade and
	 * the FLIPBOOK's blend. */
	RBTREND_PARTICLES_SHADER_LIT_FRAGMENT     = 2,
	/** The colour over life times the material's base colour map and its
	 * emission, exposed and encoded as the scene's surfaces are; SOFT and
	 * FLIPBOOK as the lit one. */
	RBTREND_PARTICLES_SHADER_UNLIT_FRAGMENT   = 3,
};

/**
 * @brief One of Rend's particle shaders: the instance's, made on first
 * use (don't destroy it).
 *
 * @return As rbtrend_lit_get_shader().
 *
 * [ADDED] Why: as rbtrend_lit_get_shader().
 */
RBT_RESULT rbtrend_particles_get_shader(
	rbtrend_instance instance,
	RBTREND_PARTICLES_SHADER shader,
	rbtrend_shader *out_shader,
	RBTREND_ERROR *out_error);

/**
 * @brief One system's step (section 2), an element of
 * RBTREND_COMMAND_PARTICLES_STEP's array.
 */
/* Frozen: an array element, sent every frame. It never changes; a new need gets a new type. */
typedef struct RBTREND_PARTICLES_STEP {
	/** The system: of the list's instance, at most once in an array. */
	rbtrend_particles _particles;
	/** Seconds since its last step: finite, 0 to 1 (0 holds it still). */
	float _delta_time;
	/** How many to spawn: as many as it has dead slots for are. */
	uint32_t _spawn_count;
	/** This step's random values' seed. */
	uint32_t _seed;
	/** Kill every particle before anything else. */
	bool _clear;
	/** Its anchor from the eye, the scene's space: finite. */
	float _anchor[3];
	/** How far its anchor moved since its last step, the scene's axes: finite. */
	float _anchor_moved[3];
	/** Its emitter from the anchor: three rows of a 3x4 matrix, row after
	 * row, finite. */
	float _emitter[12];
	/** Its emitter's velocity, the scene's unit a second: finite. */
	float _emitter_velocity[3];
	/** The air's velocity at the anchor, the scene's unit a second: finite. */
	float _wind[3];
} RBTREND_PARTICLES_STEP;
/* [ADDED] Why: the per-step values of section 2, one array for every system. */

/*
 * [ADDED] rbtrendcommands.h:
 *
 *	/**
 *	 * Steps systems (rbtrendparticles.h), outside a pass: each of `_steps`
 *	 * by section 4's stages, every system's stage before the next.
 *	 * `_scene` gives the eye, the view direction and the view-projection
 *	 * the collision and the sort read; `_depth` the depth systems collide
 *	 * with (each system's depth, where set, must be it), NULL for none.
 *	 * Recorded after that depth is drawn and before the passes that draw
 *	 * the systems. `_steps` copied at the send; at most
 *	 * RBTREND_PARTICLES_MAX_STEPS. Needs indirect dispatch (named here and
 *	 * refused, RBT_RESULT_UNSUPPORTED, at the compile without it). A
 *	 * system with no effect is skipped.
 *	 *
 *	 * It binds pipelines and sets of its own, as BLOOM does.
 *	 *\/
 *	RBTREND_COMMAND_PARTICLES_STEP = 52,
 *	/**
 *	 * Draws `_particles` in the current pass with the pipeline and the
 *	 * sets 0 and 1 the caller set: binds the system's set at
 *	 * RBTREND_PARTICLES_SET and draws indirectly from its last step's
 *	 * arguments (for MESH, with the index buffer the caller set). Inside
 *	 * a pass; the system stepped earlier in the list or before it; its
 *	 * depth not an attachment of the pass. A system with no alive
 *	 * particles draws nothing.
 *	 *\/
 *	RBTREND_COMMAND_DRAW_PARTICLES = 53,
 *
 * Their parameters:
 *	struct {
 *		/** The view the systems are stepped for. *\/
 *		rbtrend_lit_scene _scene;
 *		/** The depth they collide with, or NULL. *\/
 *		rbtrend_texture_view _depth;
 *		/** The steps, `_step_count` of them; copied at the send. *\/
 *		const struct RBTREND_PARTICLES_STEP *_steps;
 *		/** How many: 0 records nothing. *\/
 *		uint32_t _step_count;
 *	} _particles_step;
 *	struct {
 *		/** The system drawn. *\/
 *		rbtrend_particles _particles;
 *	} _draw_particles;
 *
 * Why: the step is one command over an array (pass 4.1); the draw a
 * command inside the caller's pass, as DRAW_LIST is.
 */

#if __cplusplus
}
#endif
