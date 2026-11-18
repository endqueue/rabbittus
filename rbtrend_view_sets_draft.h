/*
 * rbtrend_view_sets_draft.h - the binding sets Rend makes over the views a
 * command names (LIT_SSR, LIT_TRACE_REFLECTIONS, LIT_REFLECT,
 * LIT_ATMOSPHERE_SHELLS), kept per lit scene in records the caller sizes,
 * and a compile that would need more of them than it sized refused with
 * a message naming the limit (card A67; RBTREND_TODO.md item 52's "found,
 * not fixed", and item 24's reflections).
 * Its caller's own half is drafted beside the caller's headers.
 *
 * DRAFT, 2026-11-17, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval.
 * APPROVED 2026-11-17 with the user, every question as recommended
 * (Q1 b, Q2 to Q7 a). Built in A55's session after A65 and A66. Card A67 was decided 2026-11-17 as option a: "the cache's size
 * comes from the caller's desc, and a list that needs more distinct sets
 * than that is refused at the compile with a message naming the limit".
 * This drafts that, and Q1 decides the desc it is on, as the card asks.
 * Written on the MacBook (M5 Pro), unattended, against the working tree of
 * rabbittus as read on 2026-11-17 (3d85641, 0.3, plus the day's
 * uncommitted LIT_ATMOSPHERE_SHELLS, command 51):
 *   include/rbtcore.h (RBT_RESULT_LIMIT, 6, "Over a limit the caller set";
 *     struct types to 5038, RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR);
 *   include/render/rbtrendcontext.h (RBTREND_INSTANCE_DESC, 232-276;
 *     "Threads", "Destroying");
 *   include/render/rbtrendlit.h (rbtrend_lit_scene_create(), 739, which
 *     takes no desc; rbtrend_lit_scene_destroy());
 *   include/render/rbtrendcommands.h (RBTREND_COMMAND_LIT_SSR 45,
 *     _LIT_TRACE_REFLECTIONS 46, _LIT_REFLECT 47, _LIT_ATMOSPHERE_SHELLS
 *     51 at 1206; rbtrend_command_list_compile(), 2017, its @return from
 *     1922);
 *   src/render/rbtrendlit_internal.hpp (the cache: 354-382,
 *     RBTREND_VIEW_SETS 16u at 362, rbtrend_view_set at 363-371; the
 *     instance's lit.view_sets at 489; rbtrend_lit_scene_t at 619);
 *   src/render/rbtrendlit.cpp (rbtrend_view_set_get() 740-771,
 *     rbtrend_view_sets_destroy() 773-778, rbtrend_lit_builtins_destroy()
 *     908-910, rbtrend_lit_scene_create() 1889, _destroy() 1969);
 *   src/render/rbtrendraytracing_internal.hpp (reflection_sets, 294) and
 *     rbtrendraytracing.cpp (its destroy, 134);
 *   src/render/rbtrendcommands.cpp (every user of rbtrend_view_set_get():
 *     LIT_SSR 4546, LIT_TRACE_REFLECTIONS 4603, LIT_REFLECT 4649,
 *     LIT_ATMOSPHERE_SHELLS 4865, its two loops 4870-4895 and 4919-4921;
 *     rbtrend_cmd_use_handle() 104; the compiled lists: _unlist() 305,
 *     _enlist() 324, _uses() 338, rbtrend_instance_forget() 357; the
 *     compile 5262, its unlist 5272; the submit's unlist 6168; destroy 408
 *     and reset 422);
 *   src/render/rbtrendcontext.cpp (rbtrend_instance_retire(), 350-372,
 *     the serial it stamps at 358);
 *   src/render/rbtrendpipeline.cpp (rbtrend_binding_set_destroy(),
 *     2095-2105; a layout's descriptor pools, 99-133,
 *     RBTREND_BINDING_SETS_PER_POOL 32 in rbtrend_internal.hpp:143);
 *   src/render/rbtrendrendertargets.cpp (rbtrend_render_target_take(),
 *     115-158: a taken target free again once its frame has finished);
 *   carrottus/include/crgpu.h (crgpu_device_desc::max_descriptors and
 *     max_samplers, 1120-1142);
 *   RBTREND_TODO.md items 24 (reflections, card A30) and 52 (stage 4);
 *   rbtrend_atmosphere_space_draft.h (version 9, approved; its pass-1
 *     note 2 and section 4's "so a steady frame makes none");
 *   the first caller's need as card A67 records it (up to 64 shells a
 *     target; its own half is named on the card, not here: Rend names no
 *     caller);
 *   Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel, present):
 *     src/Hazel/Platform/Vulkan/DescriptorSetManager.h/.cpp,
 *     src/Hazel/Renderer/RendererConfig.h, src/Hazel/ImGui/ImGuiRenderer.h
 *     and .cpp, and NVRHI as vendored there
 *     (vendor/nvrhi/src/vulkan/vulkan-resource-bindings.cpp,
 *     vulkan-graphics.cpp, include/nvrhi/d3d12.h).
 * Not built, not in any target, not compilable: it repeats declarations of
 * rbtcore.h, rbtrendlit.h and rbtrendcommands.h to show them changed.
 * Every declaration below is marked [ADDED] or [CHANGED]; nothing unmarked
 * changes, and nothing public is [REMOVED]. Nothing is built until it is
 * approved; once approved it is frozen (ROADMAP.md, section 1).
 *
 * ===========================================================================
 * Revision notes (the seven passes, 2026-11-17, each fixed before the next)
 * ===========================================================================
 *
 * Caller names: none (Rabbittus names no caller; check.sh caller-names).
 * The first caller's numbers are card A67's to cite.
 *
 * Version 0 (before the passes) was the card's words taken literally: a
 * field on RBTREND_INSTANCE_DESC sizing the instance's one cache, and a
 * compile refused when one list names more distinct sets than that.
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: "a list that needs more than that" is not the limit that
 *    matters. The cache is shared by every list compiled on the instance,
 *    and a compiled list that has not been submitted still binds its sets.
 *    A second list's compile that evicts one of them marks the first stale
 *    (rbtrend_binding_set_destroy() -> rbtrend_instance_forget()), so the
 *    first is refused at its submit with RBT_RESULT_REND_STALE; compiling
 *    it again evicts the second's. Two lists of 10 sets each ping-pong
 *    under a limit of 16 though neither passes it. Changed: what counts is
 *    the sets HELD: those of the compile in progress and of every list
 *    compiled and not yet submitted (section 2). A set a submitted list
 *    binds is free to evict: its release already waits for the GPU.
 * 2. Missing: today's code has a GPU hazard, not only a limit (section 1,
 *    "Found"). The list compiling is off the compiled lists while it
 *    compiles (rbtrendcommands.cpp 5272), so evicting a set it already
 *    binds marks nothing stale, and the set is retired at the serial of the
 *    last submission (rbtrendcontext.cpp 358), which is before this
 *    list's own. The hold rule removes it.
 * 3. Missing: LIT_TRACE_REFLECTIONS has a second fixed cache of 16,
 *    rbtrend_rt_state::reflection_sets. Changed: one cache serves the four
 *    commands (Q6).
 * 4. Wrong place: on the instance, the caller's count must cover every
 *    scene's commands at once. A caller that draws several targets, each
 *    with its own scene, sizes it by a number of targets it may not know
 *    when it makes the instance, or its targets evict each other's sets
 *    every frame (a set made and destroyed a frame: the steady state
 *    allocates). Every one of the four commands names a `_scene`, and a
 *    scene already keeps sets of its own (rbtrend_lit_scene_ssr_set(), its
 *    ray-traced set). Changed: one cache per lit scene, sized from a new
 *    RBTREND_LIT_SCENE_DESC (Q1, the instance's desc kept as option a).
 * 5. More than needed, left out: a getter of the count (the caller set
 *    it); a count per command kind (one cache keyed by layout serves all
 *    four, as today's does); a call to make the sets ahead (they are made
 *    on first use, as today, and that is a creation, stated).
 *
 * Pass 2 (the platforms and industry practice; section 8).
 * 1. Vulkan: a descriptor set must not be freed or updated while a
 *    command buffer that binds it is pending (without UPDATE_AFTER_BIND).
 *    Rend's release queue covers a submitted list; nothing covered a
 *    compiled, unsubmitted one. The hold rule is that rule for the second
 *    case. Checked D3D12's (descriptors a shader-visible heap holds are not
 *    overwritten while work in flight reads them) and Metal's (an argument
 *    buffer is the caller's to keep until the command buffer completes):
 *    the same rule, the same fix.
 * 2. Compared caches: sokol_gfx's WebGPU bind-group cache is sized by
 *    its caller (sg_desc's wgpu_bindgroups_cache_size) and evicts on a
 *    collision with no refusal; NVRHI (Hazel's) and wgpu count references,
 *    so a set lives while any command list holds it, and nothing is ever
 *    refused, nor bounded. The second is a real alternative: Q2 option d
 *    (grow past the caller's size while a set is held), against the
 *    decided card and CONVENTIONS.md ("nothing grows behind the caller's
 *    back").
 * 3. Carrottus: each set's descriptors come out of the device's budget,
 *    crgpu_device_desc::max_descriptors (and max_samplers), which its
 *    caller sets; a pool past it is refused. Changed: the field's text
 *    says the sets it allows cost descriptors there, so the two are sized
 *    together.
 * 4. Hazel keeps no shared cache: each render pass, compute pass and
 *    material owns a DescriptorSetManager whose sets are made per frame in
 *    flight and made again when an input changes. Ownership by the object
 *    that draws matches Q1 b; Hazel bounds nothing, as NVRHI keeps sets
 *    alive by reference.
 *
 * Pass 3 (the C API, CONVENTIONS.md).
 * 1. The create takes a desc in the place rbtrend_lit_environment_create()
 *    takes its own: (instance, desc, out_scene, out_error). The desc is
 *    based (RBT_STRUCT_BASE, RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC 5040: the
 *    next free; 5039 is rbtrend_lit_layers_draft.h's), so the scene's
 *    creation can grow by a field or a chained struct.
 * 2. The field `max_view_sets`, as Rend's other caller-set limits are
 *    named (`_max_instances`, `_max_steps`); its range, its 0 and its
 *    costs in Doxygen.
 * 3. Checks in parameter order, one condition, one result, one message:
 *    instance NULL, destroyed; desc NULL; desc->base (wrong type, a chain);
 *    max_view_sets past RBTREND_LIT_SCENE_MAX_VIEW_SETS; out_scene NULL.
 * 4. The code past the limit: RBT_RESULT_LIMIT ("Over a limit the caller
 *    set", rbtcore.h), as rbtrendraytracing.h refuses a write with every
 *    set in use; no new code (Q5). The compile's @return names it.
 * 5. Each of the four commands' text gains one paragraph saying what it
 *    takes from the scene's records, so the caller can count without
 *    reading Rend's source.
 * 6. A named maximum beside the check (Q4), not a constant at a point of
 *    use.
 *
 * Pass 4 (no hidden policy, no defaults).
 * 1. Version 0 had "0 keeps the 16 of today": a default a caller never
 *    chose. Changed: 0 is a scene with no records, whose four commands are
 *    refused at the compile naming the field (Q3); the precedent is
 *    RBTREND_INSTANCE_DESC::transient_chunk_size, whose 0 is "no transient
 *    memory".
 * 2. Considered and struck: Rend keeping frames_in_flight times the
 *    caller's count, so sets over views that change each frame are kept
 *    without the caller counting them. That decides for the caller and
 *    hides the cost. The header tells the caller what to count instead
 *    (section 3).
 * 3. Which record a new set replaces (the least recently handed out of
 *    those not held) changes no output, only whether a set is made again;
 *    it is stated so a caller can reason about a steady state.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Recency was the instance's frame_number: every set handed out in a
 *    frame ties, and a caller that compiles without
 *    rbtrend_instance_next_frame() never ages a record. Changed: a counter
 *    per scene, raised at each hand-out. What is refused, and what is
 *    replaced, then depends on the calls and their order alone, never on
 *    frames or the GPU's progress.
 * 2. The hold check reads the instance's compiled lists, which a list's
 *    destroy or reset may change from another thread; it is read under
 *    release_mutex, as rbtrend_instance_forget() reads it. Only on a miss:
 *    a hit takes no lock.
 * 3. Errors: refused before anything is made, so a refused compile makes
 *    no set past the limit; sets it made before the refusal stay in the
 *    records, as the compile keeps a material's set it built whether or
 *    not the rest succeeds (rbtrendcommands.h). The message names the
 *    command, the scene's limit and the field.
 * 4. Device loss: no change (nothing here waits or reads a fence).
 *
 * Pass 6 (the code as built; every file cited read again).
 * 1. rbtrend_view_set_get() doesn't add the set to the compile's
 *    used_handles; each command does, after (4338, 4438, 4663, 4889).
 *    The hold check reads used_handles, so a future command that forgot
 *    would be unprotected. Changed: the hand-out adds it.
 * 2. LIT_ATMOSPHERE_SHELLS asks for each shell's set twice: in the loop
 *    that readies the sets (4870-4895) and in the draw loop (4919-4921).
 *    Past 16 distinct, the second loop misses every time and makes each
 *    set again, every frame (the approved draft's "a steady frame makes
 *    none" does not hold there). Under the hold rule the second always
 *    hits; the 17-shells test holds it.
 * 3. LIT_SSR's Hi-Z pyramid comes from the render target pool
 *    (rbtrendrendertargets.cpp 115-158), which hands out another target
 *    while the frame that took one is unfinished: its set changes from
 *    frame to frame, over up to frames_in_flight pyramids. Counted in
 *    section 3.
 * 4. The cache's destroys move: rbtrendlit.cpp 910 and
 *    rbtrendraytracing.cpp 134 go; the scene's destroy (1969, through
 *    rbtrend_lit_scene_free_parts()) destroys its records' sets, which
 *    retire through the queue as today's do.
 * 5. RBT_RESULT_LIMIT is 6 (rbtcore.h 182); struct types end at 5038
 *    (rbtcore.h 373).
 * 6. Every caller of rbtrend_lit_scene_create() read: rbtrendlit.cpp,
 *    rbtrend_contract_tests.c, rbt_misuse_tests.c (generated),
 *    rabbittustests' rbtrend_gpu_tests.c (9) and rbtrend_destroyed_calls.inc
 *    (generated), and one caller outside the repository (card A67).
 *
 * Pass 7 (tests, records, the build).
 * 1. The hazard needs a test that would catch it: sets evicted across
 *    submissions under every validation layer, with images compared
 *    (section 10, test 5).
 * 2. A unit test of the choice alone, no device: the choice factored into
 *    a function over plain records and a "held" answer
 *    (rbtrend_view_set_choose(), section 5).
 * 3. The no-allocation scenario at the limit, and a scene at 0 drawing no
 *    view command (the other callers' case).
 * 4. Records listed (section 11). No version raise (card X2: Rabbittus'
 *    0.3 is untagged). Built with its caller's half in the same session:
 *    the create's signature breaks it.
 *
 * ===========================================================================
 * 1. The need, today's code, and what was found
 * ===========================================================================
 *
 * Four commands bind a set Rend makes over views the command names:
 * LIT_SSR (depth, normal and roughness, colour, its Hi-Z pyramid, the
 * reflection target), LIT_TRACE_REFLECTIONS (depth, normal and roughness,
 * reflection, the scene's two cubes, the BRDF table), LIT_REFLECT (depth,
 * normal and roughness, specular, reflection) and LIT_ATMOSPHERE_SHELLS
 * (the depth and a shell's atmosphere's two tables, one set a shell).
 * Making a set a compile is an allocation and a descriptor write a frame,
 * so Rend keeps them: today 16 records on the instance for the first,
 * third and fourth, and 16 more for the second, each record a set, its
 * layout and the views (and their serials) it was made over. A compile that
 * names the same views finds the record; one that doesn't makes a set into
 * the least recently handed out, destroying what was there.
 *
 * A caller of LIT_ATMOSPHERE_SHELLS may draw up to 64 shells a target
 * (card A67), each a set. Past 16, today's code:
 *
 * - Evicts a set the compile in progress already binds. The list being
 *   compiled is off the instance's compiled lists (rbtrend_cmd_list_unlist()
 *   at the compile's start), so rbtrend_instance_forget() marks nothing, and
 *   rbtrend_binding_set_destroy() retires the set at the serial of the last
 *   submission taken (rbtrend_instance_retire()), which is before this
 *   list's own. Once the fence passes that serial, the set's descriptor
 *   set goes back to its layout's free list, and the next set of that
 *   layout is written into it, while this list's submission may still be
 *   reading it. Found, a hazard; nothing yet reported it (no test passes
 *   16 distinct sets in one list).
 * - Makes every set past the 16th again in the draw loop, every frame
 *   (pass 6, note 2): the steady state allocates.
 * - Across lists: a list compiled and not yet submitted is marked stale by
 *   another's eviction, refused at its submit, and evicts the other's when
 *   compiled again.
 *
 * CONVENTIONS.md: "Records are made at creation, sized from the caller's
 * desc. Nothing grows behind the caller's back: past a limit is a result
 * code." A constant 16 is "a constant where a limit belongs".
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * A lit scene keeps `max_view_sets` records, made when the scene is
 * created (RBTREND_LIT_SCENE_DESC), empty. The four commands take their
 * sets from the records of the scene they name (`_scene`).
 *
 * A record is HELD while a set it holds is bound by the compile in
 * progress or by a command list compiled and not yet submitted (nor since
 * compiled again, reset or destroyed). A submitted list holds nothing: its
 * sets' release waits for the GPU, as every destroyed handle's does.
 *
 * At the compile, for each set a command needs, in the order the commands
 * were sent:
 * - A record over the same layout and the same views (the same handles,
 *   not destroyed and made again since) hands its set out. Nothing is made.
 * - Else a record not held takes a new set: an empty one first, else the
 *   one handed out least recently. Its old set is destroyed.
 * - Else (every record held) the compile is refused with RBT_RESULT_LIMIT
 *   and a message naming the command, the scene's `max_view_sets` and the
 *   field, before the set is made. "command 31: LIT_ATMOSPHERE_SHELLS:
 *   shell 17 needs a view set, and the scene's 16 (max_view_sets,
 *   RBTREND_LIT_SCENE_DESC) are all held by this compile and lists
 *   compiled and not yet submitted".
 *
 * So the limit is the caller's: no more than `max_view_sets` distinct sets
 * held at once for one scene. It never depends on frames, on timing or on
 * the GPU: the same calls in the same order are refused or not alike.
 *
 * ===========================================================================
 * 3. How a caller counts
 * ===========================================================================
 *
 * The sets a command takes, from its scene's records:
 * - LIT_SSR: one, over its five views and its pyramid. The pyramid is
 *   pooled, and the pool hands out another while a frame that took one is
 *   unfinished, so from frame to frame it is one of up to
 *   frames_in_flight sets.
 * - LIT_TRACE_REFLECTIONS: one, over its three views, the scene's
 *   environment's two cubes (or Rend's placeholder) and the BRDF table.
 * - LIT_REFLECT: one, over its four views.
 * - LIT_ATMOSPHERE_SHELLS: one a shell it draws, over `_depth` and the
 *   shell's atmosphere's two tables; shells naming one atmosphere share
 *   one. A shell off the screen takes none, so size for every shell the
 *   command may list, not for those usually seen.
 *
 * Two numbers, then:
 * - Refusal: at most `max_view_sets` distinct sets for the scene across
 *   the lists that are compiled and not yet submitted at once, the one
 *   compiling included. A caller that compiles and submits one list at a
 *   time counts one list.
 * - A steady state that makes nothing: every distinct set the scene's
 *   commands name over the frames before a view repeats. A view the
 *   caller names afresh each frame (a per-frame copy of a target, of which
 *   it keeps frames_in_flight) multiplies the sets over it, as LIT_SSR's
 *   pyramid does. Below that, nothing is refused, but sets are made again
 *   each frame, which is an allocation.
 *
 * Each record costs 120 bytes when the scene is made; each set made costs
 * a Rend binding set and the descriptors its layout holds (3 to 7),
 * counted against the device's crgpu_device_desc::max_descriptors (and a
 * sampler against max_samplers where the layout has one). Sets are made by
 * descriptor pools of 32 a layout (Rend's), so the first set of a layout,
 * and every 33rd, makes a pool.
 *
 * ===========================================================================
 * 4. The declarations
 * ===========================================================================
 */

/* ---- rbtcore.h ---------------------------------------------------------- */

/*
 * [ADDED] RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC = 5040,
 *         /**< RBTREND_LIT_SCENE_DESC. * /
 * Why: the new desc is based (CONVENTIONS.md, "Struct bases": every struct
 * that may grow starts with a base). 5040 is the next free: rbtcore.h ends
 * at 5038, and rbtrend_lit_layers_draft.h (approved) takes 5039.
 */

/* ---- render/rbtrendlit.h ------------------------------------------------ */

/**
 * [ADDED] The most records a scene keeps of the binding sets its
 * commands make over views (RBTREND_LIT_SCENE_DESC::max_view_sets).
 *
 * Why it earns its place: a set is found by a scan of the scene's records,
 * at each command that takes one; this bounds a scan's cost (256 records
 * of 120 bytes, 30 KiB) and what a scene allocates at creation. A bound
 * the design imposes, named beside the check (CONVENTIONS.md, "Limits come
 * from data"). Raising it later breaks no caller. (Q4.)
 */
#define RBTREND_LIT_SCENE_MAX_VIEW_SETS 256u

/**
 * [ADDED] How a lit scene is created.
 *
 * Why it earns its place: the scene's records are made at its creation and
 * sized by its caller (CONVENTIONS.md, "Records are made at creation, sized
 * from the caller's desc"), and the create has nothing to size them from
 * today. Based, so the scene's creation can take more later.
 */
typedef struct RBTREND_LIT_SCENE_DESC {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC, and its
	 * chain, which takes nothing today (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/**
	 * How many binding sets over views the scene keeps, for the commands
	 * that name it and make one: RBTREND_COMMAND_LIT_SSR,
	 * _LIT_TRACE_REFLECTIONS, _LIT_REFLECT and _LIT_ATMOSPHERE_SHELLS
	 * (rbtrendcommands.h says which sets each takes). 0 to
	 * RBTREND_LIT_SCENE_MAX_VIEW_SETS.
	 *
	 * A set is kept and handed out again to a later compile that names the
	 * same views, so a steady frame makes none. A compile that needs a set
	 * while every record holds one bound by it or by a list compiled and
	 * not yet submitted is refused with RBT_RESULT_LIMIT, naming this
	 * field; otherwise a record not so held takes the new set (an empty
	 * one, else the one handed out least recently), its old set destroyed.
	 *
	 * 0 is a scene with no records: those four commands naming it are
	 * refused at the compile, with RBT_RESULT_LIMIT naming this field; it
	 * draws lit surfaces, skies and every other command as any scene.
	 *
	 * Costs 120 bytes a record, allocated by rbtrend_lit_scene_create().
	 * Each set made costs the descriptors of its layout (3 to 7) from the
	 * device's crgpu_device_desc::max_descriptors, and its sampler from
	 * max_samplers, until the record takes another or the scene is
	 * destroyed.
	 */
	uint32_t max_view_sets;
} RBTREND_LIT_SCENE_DESC;

/**
 * [CHANGED] rbtrend_lit_scene_create() takes a desc.
 * Why: the records are sized from it. In the place the other creates of
 * rbtrendlit.h take theirs (rbtrend_lit_environment_create(): instance,
 * desc, out, error). A break: every caller passes a desc (section 9).
 *
 * @brief Creates a scene. It starts with exposure 1 and LINEAR output and
 * everything else 0: no view, no lights, no shadow atlas, no ambient light,
 * no shadows, so a surface drawn with it is black but for its emission.
 *
 * It makes, with its own material, RBTREND_LIT_MAX_SHADOW_VIEWS shadow pass
 * materials (rbtrend_lit_scene_get_shadow_pass_material()), handed out to
 * shadows' views as they are set, and @p desc's `max_view_sets` records of
 * the sets its commands make over views (RBTREND_LIT_SCENE_DESC): what
 * setting a shadow or compiling a command needs is allocated here, once.
 *
 * @param[in]  instance  Instance the scene is of.
 * @param[in]  desc      How it is made. Copied: only read during the call.
 * @param[out] out_scene Receives the scene; NULL on failure.
 * @param[out] out_error Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT if @p instance is
 *         NULL or destroyed, @p desc is NULL, `max_view_sets` is above
 *         RBTREND_LIT_SCENE_MAX_VIEW_SETS, or @p out_scene is NULL;
 *         RBT_RESULT_STRUCT_WRONG_TYPE or RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN
 *         for `desc->base`; RBT_RESULT_OUT_OF_MEMORY; or
 *         RBT_RESULT_REND_GPU_FAILURE if what it is drawn with can't be made.
 */
RBT_RESULT rbtrend_lit_scene_create(
	rbtrend_instance instance,
	const RBTREND_LIT_SCENE_DESC *desc,
	rbtrend_lit_scene *out_scene,
	RBTREND_ERROR *out_error);

/**
 * [CHANGED] rbtrend_lit_scene_destroy(): its text gains one clause.
 * Why: the records' sets go with it.
 *
 * @brief Destroys a scene, its materials and the sets its records hold.
 * What it names (shadow maps, an environment, an occlusion texture) is the
 * caller's. The GPU doesn't have to be finished with it.
 * (Thread: and the rest unchanged.)
 */
void rbtrend_lit_scene_destroy(rbtrend_lit_scene scene);

/* ---- render/rbtrendcommands.h ------------------------------------------- */

/*
 * [CHANGED] Text only, no value changes. Each of the four commands gains
 * one paragraph, after what it reads:
 *
 * RBTREND_COMMAND_LIT_SSR (45):
 *   "It takes one set from `_scene`'s view-set records
 *   (RBTREND_LIT_SCENE_DESC::max_view_sets), over its views and its Hi-Z
 *   pyramid. The pyramid is taken from the render target pool, which hands
 *   out another while a frame that took one is unfinished: over frames,
 *   one of up to frames_in_flight sets."
 * RBTREND_COMMAND_LIT_TRACE_REFLECTIONS (46):
 *   "It takes one set from `_scene`'s view-set records, over its views,
 *   the scene's environment's cubes (or Rend's placeholder) and the BRDF
 *   table."
 * RBTREND_COMMAND_LIT_REFLECT (47):
 *   "It takes one set from `_scene`'s view-set records, over its four
 *   views."
 * RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS (51):
 *   "It takes one set from `_scene`'s view-set records for each shell it
 *   draws, over `_depth` and the shell's atmosphere's two tables; shells
 *   naming one atmosphere share it, and a shell off the screen takes none.
 *   A caller sizes the records for every shell it may list."
 *
 * Why: the caller counts its sets from the header, not from Rend's source
 * (section 3); each sentence is the command's own cost.
 *
 * [CHANGED] rbtrend_command_list_compile(): @return gains
 *   "RBT_RESULT_LIMIT for a LIT_SSR, LIT_TRACE_REFLECTIONS, LIT_REFLECT or
 *   LIT_ATMOSPHERE_SHELLS that needs a set of its scene's view-set records
 *   while each holds one bound by this compile or by a list compiled and
 *   not yet submitted (RBTREND_LIT_SCENE_DESC::max_view_sets), and for any
 *   of them naming a scene whose max_view_sets is 0;"
 * and its list of what it changes outside the list gains: "the view-set
 *   records of the scenes its commands name: a set made into a record
 *   stays there whether or not the rest of the compile succeeds".
 * Why: the call's own failures are on its @return (CONVENTIONS.md).
 */

/*
 * ===========================================================================
 * 5. Not in a header
 * ===========================================================================
 *
 * - rbtrendlit_internal.hpp: RBTREND_VIEW_SETS (16u) goes. rbtrend_view_set
 *   keeps its fields; `used` becomes the scene's hand-out counter's value
 *   (uint64_t), not the instance's frame_number. RBTREND_VIEW_SET_VIEWS
 *   (6) stays: the most views one of the four commands names.
 * - rbtrend_lit_scene_t gains `rbtrend_view_set *view_sets` (calloc'd
 *   at create, `max_view_sets` records, NULL for 0), `uint32_t
 *   view_set_count` and `uint64_t view_set_handouts`. The instance's
 *   lit.view_sets (489) and the ray-tracing state's reflection_sets (294)
 *   go, with their destroys (rbtrendlit.cpp 910, rbtrendraytracing.cpp
 *   134).
 * - rbtrend_view_set_get(cmd_list, scene, layout, views, view_count,
 *   entries, entry_count, what): takes the list (for its used_handles and
 *   its report) and the scene in place of the instance and the array. On
 *   a hit, raises the counter, stamps the record, adds the set to the
 *   list's used_handles, returns it. On a miss, under release_mutex, asks
 *   rbtrend_view_set_choose() for a record; none: reports RBT_RESULT_LIMIT
 *   with the message of section 2 and returns NULL, nothing made. Else
 *   makes the set (a failure reported as today, the record unchanged),
 *   destroys the old one, fills the record, adds the set to used_handles.
 * - rbtrend_view_set_choose(records, count, held, user): [ADDED, internal]
 *   the index of the first empty record, else of the record with the
 *   lowest `used` whose set `held` says is not held, else UINT32_MAX. Plain
 *   logic over plain records, unit-tested without a device.
 * - "Held": the set is in the compiling list's used_handles, or in the
 *   used_handles of a list on the instance's compiled_lists
 *   (rbtrend_cmd_list_uses()'s lookup, under release_mutex). Read only on
 *   a miss.
 * - rbtrend_lit_scene_free_parts() destroys each record's set
 *   (rbtrend_binding_set_destroy(): retired, released once the GPU has
 *   finished what was submitted) and frees the array. A scene is destroyed
 *   before its instance, so the layouts the sets are of (the instance's)
 *   outlive them, as today's order has it (sets before layouts).
 * - The four commands pass their scene: LIT_SSR p._scene, the trace's
 *   p._scene, LIT_REFLECT p._scene, the shells' scene. Their own
 *   rbtrend_cmd_use_handle() of the set stays (harmless: the lookup finds
 *   it).
 *
 * ===========================================================================
 * 6. Allocation
 * ===========================================================================
 *
 * - rbtrend_lit_scene_create(): one calloc of max_view_sets x 120 bytes
 *   (sizeof(rbtrend_view_set): a set and a layout pointer, 6 views, 6
 *   serials, the counter; 8-byte aligned, no padding), none for 0. At 64,
 *   7,680 bytes; at the maximum, 30,720.
 * - A compile: a set made on a miss is a creation (a Rend binding set,
 *   its release promise, and a descriptor pool for the first and each
 *   33rd set of a layout), stated in the field's text. A hit allocates
 *   nothing. The steady state (every set found) allocates nothing: held
 *   by test_frame_allocations (section 10).
 * - The record array is an array of structs: a lookup compares the layout,
 *   then the views and their serials together, then reads `set` and writes
 *   `used`; every field is read together on a match, and the layout alone
 *   rejects most records of another command. Plain data
 *   (RBT_PLAIN_DATA(rbtrend_view_set) stays; check.sh plain-data).
 * - Cost of a lookup: at most max_view_sets records scanned; 64 shells
 *   over 64 records is about 2,000 record visits a loop, two loops, a few
 *   microseconds (to be measured: section 10, test 9).
 *
 * ===========================================================================
 * 7. Threads, determinism, errors
 * ===========================================================================
 *
 * - Threads: unchanged rules. A scene's records are read and written only
 *   by a compile, which enters the instance (RBTREND_ENTER), and by the
 *   scene's create and destroy. The hold check reads the instance's
 *   compiled lists under release_mutex, which a list's destroy or reset
 *   on another thread takes too (rbtrend_cmd_list_unlist()). A hit takes
 *   no lock.
 * - Who writes what, when (CONVENTIONS.md, "No CPU write into anything the
 *   GPU ... may be reading"): a set is written once, when it is made, and
 *   never updated; a set any compiled-and-unsubmitted list binds is never
 *   replaced (the hold rule); a set a submitted list binds is released by
 *   the queue after the GPU passes a serial at or past that submission's
 *   (rbtrend_instance_retire()), so its descriptors are never written
 *   while read.
 * - Determinism: what is refused and which record is replaced depend on
 *   the calls and their order only: the counter, not frame_number; the
 *   compiled lists, not the fence.
 * - Errors: RBT_RESULT_LIMIT, refused before the set is made; a failure to
 *   make a set (Carrottus) as today, RBT_RESULT_REND_GPU_FAILURE, the
 *   record unchanged. A refused compile is a failed compile: no
 *   submittable result.
 *
 * ===========================================================================
 * 8. What was compared
 * ===========================================================================
 *
 * The platforms (their specifications and documentation; not re-read
 * today, cited from what Carrottus translates):
 * - Vulkan: descriptor pools are the caller's, sized at creation
 *   (VkDescriptorPoolCreateInfo::maxSets and pool sizes); past them an
 *   allocation fails (VK_ERROR_OUT_OF_POOL_MEMORY, _FRAGMENTED_POOL). A set
 *   must not be freed or updated while a command buffer that binds it is
 *   recording or pending, unless its bindings are UPDATE_AFTER_BIND.
 *   Carrottus' pools take the same desc (crgpuvulkandescriptors.c 256).
 * - D3D12: a descriptor heap's size is the caller's at creation
 *   (D3D12_DESCRIPTOR_HEAP_DESC::NumDescriptors); the shader-visible
 *   CBV/SRV/UAV heap holds up to 1,000,000 on tiers 1 and 2, the sampler
 *   heap 2,048. Descriptors work in flight reads are not to be
 *   overwritten. Carrottus makes one shader-visible heap the size of
 *   crgpu_device_desc::max_descriptors, the caller's.
 * - Metal: an argument buffer is an MTLBuffer the caller sizes and
 *   encodes; it is the caller's to keep unchanged until the command buffer
 *   that reads it completes.
 * All three: the caller sizes what holds bindings, past it is a failure,
 * and nothing in use is rewritten. This draft brings Rend's own cache to
 * the same three.
 *
 * Industry practice (from their sources where vendored, else from memory,
 * said so):
 * - NVRHI (vendored in Hazel, read): on Vulkan a descriptor pool of one set
 *   per binding set (vulkan-resource-bindings.cpp 225-250); a command list
 *   keeps every object it binds alive until it completes
 *   (referencedResources, vulkan-graphics.cpp 643); D3D12's
 *   shader-visible heap sizes are the caller's, with defaults
 *   (d3d12.h 107-119: shaderResourceViewHeapSize 16384, samplerHeapSize
 *   1024). Unbounded, by reference counting.
 * - wgpu (from memory): bind groups are the caller's objects, kept alive
 *   by the queue until the submissions that use them finish; no cache.
 * - sokol_gfx (from memory): its WebGPU backend caches bind groups in a
 *   table the caller sizes (sg_desc's wgpu_bindgroups_cache_size, a power
 *   of two), replacing an entry on a collision, never refusing; its other
 *   pools are sized by the caller, 0 taking a default sokol chooses (the
 *   default CONVENTIONS.md forbids).
 * - bgfx (from memory): sizes its per-frame descriptor scratch from
 *   compile-time configuration (BGFX_CONFIG_*), not at run time.
 * Rend takes the caller-sized table (as sokol's) with the refusal its own
 * rules require in place of an eviction of something in use, and the
 * reference counting's guarantee (nothing bound is freed) by the hold rule
 * rather than by counts.
 *
 * Hazel (../.refs/HazelRenderer2025, present, read): no shared cache. Each
 * RenderPass, ComputePass and Material owns a DescriptorSetManager
 * (Platform/Vulkan/DescriptorSetManager.h 169) that makes its binding sets
 * once for each frame in flight (DescriptorSetManager.cpp 356-360,
 * RendererConfig.h 9: FramesInFlight 3) and makes them again when an input
 * changes (InvalidateAndUpdate); the ImGui renderer keeps an
 * std::unordered_map of sets by texture, never emptied (ImGuiRenderer.h
 * 148, ImGuiRenderer.cpp 317-334). Nothing is bounded; NVRHI's references
 * keep a set in use alive. What Rend takes from it: sets owned by the
 * object that draws (the scene here, Q1 b), and kept across frames. What
 * it doesn't: a limit nobody sets, and a map that grows.
 *
 * ===========================================================================
 * 9. ABI
 * ===========================================================================
 *
 * Breaks (listed, as CONVENTIONS.md asks before 1.0; `./check.sh
 * --update-abi` names them):
 * - rbtrend_lit_scene_create(): a parameter added (desc, second). Every
 *   caller changes: rbtrendlit.h's own text, the contract and misuse
 *   tests, rabbittustests (9 calls, and the generated destroyed-handle
 *   calls), and the caller outside the repository card A67 names, in the
 *   same session.
 * - Behaviour: a scene that a caller of the four commands creates must
 *   size its records; 0 refuses them where today 16 shared records
 *   served. A caller of none of the four passes 0 and sees no change.
 * Additions: RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC (5040),
 * RBTREND_LIT_SCENE_DESC, RBTREND_LIT_SCENE_MAX_VIEW_SETS.
 * No new result code. No version raise (card X2: 0.3 untagged).
 *
 * ===========================================================================
 * 10. Tests (each seen failing first, where it can fail before the change)
 * ===========================================================================
 *
 * Headless (rabbittus/tests):
 * 1. rbtrend_contract_tests.c: RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC ==
 *    5040, RBTREND_LIT_SCENE_MAX_VIEW_SETS == 256, the desc's size and
 *    offsets; rbtrend_lit_scene_create()'s refusals with no device (NULL
 *    instance, NULL desc, wrong type, a chain, max_view_sets 257,
 *    NULL out_scene), each its message.
 * 2. rbtrend_view_set_choose(), a unit test beside
 *    rbtrendranges_tests.cpp: empty first; least recently handed out of
 *    those not held; a held record never chosen; all held gives
 *    UINT32_MAX; count 0 gives UINT32_MAX.
 * 3. rbt_misuse_tests.c regenerated (tests/misuse/make_misuse_tests.py)
 *    from abi.txt: the changed create zeroed and NULL.
 *
 * GPU (rabbittustests/tests/rbtrend_gpu_tests.c; Metal under validation
 * with warnings fatal, and D3D12 and Vulkan on the Windows run; the
 * device with features and the bare one):
 * 4. test_view_sets_limit: a scene of max_view_sets 4; LIT_ATMOSPHERE_
 *    SHELLS of 4 distinct atmospheres on the screen compiles and draws;
 *    of 5, refused with RBT_RESULT_LIMIT, the message naming the command,
 *    4 and max_view_sets, nothing submitted; of 5 naming 4 atmospheres
 *    (two shells sharing one), compiles.
 * 5. test_view_sets_eviction: max_view_sets 4, frames_in_flight 2; frame
 *    after frame, shells over 4 atmospheres, then 4 others, then the
 *    first 4, each frame submitted without waiting, every record replaced
 *    while the last frame's submission may run; under Vulkan's
 *    synchronization validation and Metal's validation no message, and
 *    each frame's image equal to the same frame drawn alone after a wait.
 *    (Today's hazard as a test: with 17 shells at today's 16 it would
 *    reuse a descriptor set in flight.)
 * 6. test_view_sets_across_lists: max_view_sets 4; list A compiled with 3
 *    shells and not submitted; list B with 2 other atmospheres refused
 *    (LIMIT: A holds 3); A submitted, then B compiles and submits; A,
 *    compiled again and submitted, is never refused as stale (today's
 *    ping-pong).
 * 7. test_view_sets_many_shells: max_view_sets 64, 64 shells of distinct
 *    atmospheres in one list against two lists of 32: the images equal,
 *    and the second compile of the 64 makes no set (Rend's count of sets
 *    made, read through the allocation hook).
 * 8. test_view_sets_zero: max_view_sets 0; LIT_SSR, LIT_REFLECT,
 *    LIT_ATMOSPHERE_SHELLS, and LIT_TRACE_REFLECTIONS on the device with
 *    ray tracing, each refused with RBT_RESULT_LIMIT naming max_view_sets;
 *    lit surfaces and LIT_SKY with that scene drawn as before.
 * 9. test_frame_allocations: its scenario with shells sized at its limit
 *    (two shells, max_view_sets 2) and a LIT_SSR scene at
 *    frames_in_flight + 2: 0 allocations of Rend's in the steady state.
 *    And test_atmosphere_shell_measure gains the compile's own time at 64
 *    shells (Release), before and after.
 * 10. test_destroyed_handles regenerated (make_destroyed_tests.py), the
 *    create's new signature in it; every test that makes a scene passes a
 *    desc (max_view_sets as its commands need, 0 elsewhere).
 * Under -asan and -tsan on the Mac: the headless suites and the GPU
 * tests; the two-thread test of a list destroyed on another thread while
 * a compile on the same instance looks for held records (test 6's lists,
 * one destroyed from a second thread), so -tsan reads the lock.
 *
 * ===========================================================================
 * 11. Records to update when built
 * ===========================================================================
 *
 * - rabbittus/RBTREND_TODO.md: item 52's "Found, not fixed" sentence on
 *   the view-set cache marked fixed, pointing at a new item 63 ("The
 *   view-set records sized by the caller", card A67) with what was built,
 *   the hazard found and its test, the measures; item 24 (reflections)
 *   names the scene's records.
 * - rabbittus/abi.txt (./check.sh --update-abi), the breaks listed in the
 *   commit; rabbittus/CLAUDE.md where it describes a scene's creation, if
 *   it does.
 * - rabbittus/rbtrend_atmosphere_space_draft.h: no edit (approved,
 *   frozen); item 63 says its "a steady frame makes none" held only to 16
 *   sets until this.
 * - rabbittustests: the tests above, its README's list.
 * - DECISIONS.md card A67's entry: the draft's version approved; ROADMAP.md
 *   where A67 is listed.
 * - The caller's half and its deps.lock (./check.sh --update-locks) once
 *   rabbittus is pushed; each repository's .git/COMMIT_DRAFT.
 * - check.sh whole: abi, doxygen, plain-data, struct-bases, conventions,
 *   caller-names, misuse regenerated.
 *
 * ===========================================================================
 * 12. Questions (each with options and a recommendation)
 * ===========================================================================
 *
 * Q1. Where the size is set.
 *   a) A field at the end of RBTREND_INSTANCE_DESC, one cache for the
 *      instance (the two of today merged). No signature changes, but the
 *      caller counts every scene's sets at once when it makes the
 *      instance, before it knows its targets; too few and targets evict
 *      each other's sets each frame (an allocation a frame).
 *   b) A new RBTREND_LIT_SCENE_DESC on rbtrend_lit_scene_create(), one
 *      cache per scene. The count is local to what one scene draws; a
 *      caller of several targets with a scene each needs no total;
 *      ownership as Hazel's (sets with the object that draws) and as the
 *      scene's own SSR and traced sets already are. Costs a changed
 *      create signature (every caller, listed).
 *   c) A chained struct on RBTREND_INSTANCE_DESC (the 1.0 way to grow a
 *      released desc): a's semantics, a's cost, more ceremony before 1.0.
 *   d) A count on each command (e.g. LIT_ATMOSPHERE_SHELLS' own): records
 *      can't be made at a command's send without allocating in a frame.
 *   Recommended: b.
 *
 * Q2. What the limit counts (when a record may be replaced).
 *   a) Records whose set is bound by the compile in progress or a list
 *      compiled and not yet submitted (the hold rule). Exact: refuses
 *      only what would break, independent of frames.
 *   b) Records handed out in the current frame (frame_number). Simpler,
 *      but refuses a caller that compiles many lists without
 *      rbtrend_instance_next_frame(), and counts submitted lists' sets.
 *   c) Per list (the card's words alone): doesn't stop one list's compile
 *      evicting another unsubmitted list's set (section 1).
 *   d) No refusal: grow past the caller's size while sets are held, as
 *      NVRHI's references do. Against the decided card and CONVENTIONS.md.
 *   Recommended: a.
 *
 * Q3. What 0 means.
 *   a) No records: the four commands naming the scene refused at the
 *      compile with RBT_RESULT_LIMIT naming the field. A caller of none of
 *      them passes 0 and pays nothing (precedent: transient_chunk_size 0).
 *   b) 0 refused at create: every scene sizes records it may never use.
 *   Recommended: a.
 *
 * Q4. A maximum.
 *   a) RBTREND_LIT_SCENE_MAX_VIEW_SETS 256, named beside the check: bounds
 *      the scan (30 KiB) and a scene's creation; raising it later breaks
 *      nothing. The first caller's need is 64.
 *   b) None, the multiplication checked for overflow: a scan of thousands
 *      of records at every command that takes a set.
 *   c) No maximum, the records indexed by a hash of (layout, views): the
 *      scan's cost goes, at more code than any caller needs now.
 *   Recommended: a.
 *
 * Q5. The code past the limit.
 *   a) RBT_RESULT_LIMIT, "over a limit the caller set": what it is.
 *   b) A Rend code of its own: no caller branches differently on it.
 *   c) RBT_RESULT_INVALID_ARGUMENT: hides that it is the caller's size.
 *   Recommended: a.
 *
 * Q6. LIT_TRACE_REFLECTIONS' cache.
 *   a) Into the scene's records with the other three (keyed by layout, as
 *      today's shared cache is): one size for the caller to set.
 *   b) Kept apart on the ray-tracing state at a fixed 16: the same
 *      constant and hazard, left.
 *   Recommended: a.
 *
 * Q7. Today's hazard until this is built.
 *   a) Nothing in between: this draft is the fix, built as soon as it is
 *      approved; meanwhile no test or caller passes 16 distinct sets in one
 *      list (none does today).
 *   b) A stop-gap first: the hold rule at the fixed 16, internal, refused
 *      with RBT_RESULT_LIMIT. Safe sooner, but work thrown away, and a
 *      refusal no header documents yet.
 *   Recommended: a, unless the build waits more than a session.
 */
