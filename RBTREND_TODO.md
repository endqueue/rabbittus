# Rend TODO

`include/render/rbtrend*.h` is a renderer over Carrottus GPU: buffers, textures and
their views, samplers, framebuffers, shaders and graphics pipelines, binding
layouts and sets, a
recorded command list that is compiled and then submitted, and a swap chain. It
is the older generation of code in this repo (see `CLAUDE.md`, "Two generations
of code") and is mid-overhaul. This file holds what is known to be wrong or
unfinished in it.

## The umbrella header is `rbtrend.h` (card A27, decided 2026-10-28; done 2026-10-29)

`include/rabbittus.h` included every Rend header and nothing else,
so its name said more than it held. It is `include/rbtrend.h` since
2026-10-29, Rend's umbrella in name and fact; Rabbittus Math, Rabbittus Sync, Rabbittus Archive,
Rabbittus Audio and Rabbittus Text keep their own headers, with no header including all of
Rabbittus (as Carrottus has none). `check.sh umbrella` checks `rbtrend.h`
reaches every header in `include/render/`. The one caller in the workspace
that included it moved with it; the others include the `render/` headers
they use. `src/rabbittus.cpp`, which held only a
`hello()` nothing called, went with the old name. Built and tested on
macOS (Metal).

## Deferred items and the 1.0 room check (card A15, decided 2026-10-28)

**Deferred** means not built now, with no date: the item stays here and is
built when a caller needs it. Items 6, 12, 13, 21, 29, 36 and 40 are
deferred; item 30 is dropped; items 33 (bindless on Metal) and 34
(indirect dispatch) are taken.

**The room check, before Rabbittus 1.0** (root `CLAUDE.md`, "Versions
and releases": released structs freeze, enums only grow). For every
deferred item, one line saying how it would be added after 1.0 - a new
call, command, handle or chained struct - and a check that every struct it
touches has a struct base to chain from. **Revisit them all then: any that
can be added before 1.0 cheaply, or whose shape needs a field, is taken
before the freeze.**

| Item | How it would arrive | Room after 1.0 |
|---|---|---|
| 13, merging staged copies | internal | free |
| 29, pipeline statistics | a query handle, as the timers | free |
| 6, async readback | a command and a readback handle polled | free if no struct assumes a blocking readback |
| 40, compositor scaling | a chained struct on the swap chain desc | free if the desc has a struct base |
| 12, a second queue and per-thread command memory | submit names a queue; ownership moves between queues | **at risk**: layout tracking and submit assume one queue |
| 21, storage-buffer instances, a pipeline per draw | new per-draw data in the draw list | **at risk** if the per-draw record freezes without room |
| 36, a temporal history handle | a history handle, a jittered projection, motion vectors | **at risk**: camera and projection data may need fields. Card A39 (decided 2026-11-12, b): built, in a draft of its own, with the first caller that shows a traced shadow's noise, the shadow mask accumulating over frames as its first use; the jitter field designed before 1.0 regardless |
| video (card A28, decided 2026-11-12): a target's and the swap chain's colour space named by the caller, never assumed sRGB; tone mapping in `rbtrendpost` taking the output's colour space and peak brightness | a field or chained struct on the target, swap chain and tone-map descs (Carrottus' half, card S11 (c)) | **at risk** if any frozen desc or pass assumes sRGB: checked struct by struct |
| video (A28): offscreen rendering with no swap chain at any size and rate, frames read back asynchronously for an encoder | item 6's readback; a target with no swap chain | free if nothing assumes a swap chain or a blocking readback |
| video (A28): a texture made from an imported image (S11 (e)), sampled with YUV conversion (S7) | a texture create from an imported image, a sampler's conversion as a chained struct | free if the texture and sampler descs have struct bases |
| traced shadows for point and spot lights (card A38, decided 2026-11-12, b): deferred until a caller asks | `RBTREND_COMMAND_LIT_SHADOW_MASK` naming any light, `RBTREND_LIT_SCENE_LIGHT::_radius` allowed above 0 for them, a mask and a filter per light | free: `_radius` is already in the light (A35's rewrite), 0 for point and spot lights until then |
| video (A28): 10- and 16-bit targets wherever 8-bit ones are taken | formats only | **at risk** where a call names an 8-bit format rather than taking one |
| a caller's allocator (card X15) | a chained struct on each root desc (instance, and every module's top object), everything the object makes allocating through it | free if each root desc has a struct base; the cost is rerouting the internals, which grows with the code |

**For the three at risk, their shapes are designed before 1.0, not built:**
submit takes a queue (one exists), the draw list's per-draw record can be
extended, and the camera data carries a jitter offset that is zero today.
Video export needs 6 and 12 (card A28): nothing frozen may assume one
queue or a blocking readback.

## Public unions (card A79, decided 2026-11-24 with you)

`CONVENTIONS.md`'s "Bindable as written" bans unions in public headers.
Rend has two:
- **`RBTREND_COMMAND._params`: kept, a named exception (c).** 128 bytes
  (`_type`, then 52 members padded to 120); separate fields would be
  about 1,504, 24 cache lines against 2, in an array recorded every
  frame and passed by value to `rbtrend_command_list_send`. Owed: a
  layout test (its size, `_type` at offset 0 and `_params` after it,
  each member's size within the union), and the header saying the
  union is the rule's exception and how a binding maps it (C# explicit
  offsets; Swift imports it).
- **`RBTREND_BINDING_SET_ENTRY._resource`: split into fields (a).**
  40 bytes now, 56 after (storage and uniform buffers sharing one
  `_buffer`, `_offset` and `_range`, the image's `_view` and `_sampler`
  beside them), under one 64-byte line, in an array read when a set is
  created or updated, not per draw. A public layout change, so a draft
  header for your approval first (root `CLAUDE.md`, "Working
  conventions"), with its ABI break listed for the next minor version;
  once built, `check.baseline`'s line for it goes.

Hardening session D, which this waited on, finished 2026-11-23.

## The contract conversion (cards A2, A10, A14; decided 2026-10-28)

**Decided with the user 2026-10-28; not started.** All of Rend moves to
the newer modules' contract before Rabbittus' first release (a
condition of 1.0 under card X2), header by header, creation calls first,
each caller in the workspace converted in
the same change:

- Every call that can fail returns `RBT_RESULT`; a creation gives its handle
  through an `out_` parameter (null on failure). Today: 75 calls return
  `bool`, 32 creations return the handle or NULL, none returns `RBT_RESULT`.
- **No `printf` as the error channel** (about 850 today). The reason goes
  into a per-call `RBTREND_ERROR` the caller passes in (NULL to skip), as
  `RBTARCHIVE_ERROR` is: developer English, safe under the threading rule
  (root `CLAUDE.md`, "Threading"). For a compile that fails, it also names
  the failing command's index ("command 412: BLUR source has nothing in
  it").
- **A10:** `rbtrend_instance_next_frame()` returns a result, gives the frame
  index through `out_`, takes a timeout, and reports a lost device.
- **A14:** `rbtrendbuffers.h` is rewritten as one `rbtrend_buffer` with a
  usage mask and the existing `RBTREND_BUFFER_WRITES` hint; the pools stay.
  Not mesh types: those are a caller's.
- Card A1's three renames were made on their own, first (2026-10-28).
- **The lost device needs Carrottus GPU** (decided with the user 2026-10-29):
  Carrottus GPU has no device-lost result (its wait, submit and present give
  `CR_RESULT_API_SPECIFIC_FAILURE`, acquire `UINT32_MAX`). A
  `CR_RESULT_GPU_DEVICE_LOST` is added on every backend with A10's step
  (`VK_ERROR_DEVICE_LOST`; `DXGI_ERROR_DEVICE_REMOVED`, `_HUNG` and
  `_RESET`; a Metal command buffer's error), and Rend passes it on as
  `RBT_RESULT_REND_DEVICE_LOST`.
- **Names, before 1.0** (decided with the user 2026-10-29): Rend's value
  structs and enums take the newer modules' upper-case names
  (`rbtrend_texture2d_desc` becomes `RBTREND_TEXTURE2D_DESC`), and
  `rbtrend_send_command`, the one call with its verb first, is renamed for
  its object. A pass of its own after this conversion, with every caller,
  unless it proves small enough to make with it. **Done 2026-10-30, with
  the conversion:** the value structs group by group, and
  `rbtrend_send_command` as `rbtrend_command_list_send`, beside `_reset`,
  `_compile` and `_submit`, every caller with it.
- **Progress:** A27's rename done 2026-10-29 (above). The plan, header by
  header: core and context; images; shaders and pipelines; frames, passes,
  post, timers and transient memory; materials and lit; draw lists and the
  swap chain; commands. Then A10, then A14 (the buffers and pools convert
  there, not before). Each group renames its value structs upper-case with
  it, so callers break once.
  - **Group 1, core and context: done 2026-10-29.** `RBTREND_ERROR`
    (`rbtrendcontext.h`, with "Results and errors", the rule for every
    header), `RBT_RESULT_REND_GPU_FAILURE` (5001) in `rbtcore.h`; in `src/`
    `rbtrend_fail`, `rbtrend_fail_cr` (Carrottus GPU's result mapped and named),
    `rbtrend_base_check` and `rbtrend_error_clear`. Converted:
    `rbtrend_instance_create` (`out_instance`), `_wait_idle`, `_get_desc`,
    `_get_render_target_pool`, `_trim_render_targets` (its count through
    `out_trim_count`) and `_set_render_target_retirement`; renamed
    `RBTREND_INSTANCE_DESC` and `RBTREND_RENDER_TARGET_POOL_INFO`. None of
    these prints any more. Destroy calls can't fail: a create promises its
    retires (`rbtrend_instance_reserve_release`), and the queue keeps room
    for them; the creates take it group by group, and until 1f a retire
    nobody promised still prints if it finds no memory. New:
    `tests/rbtrend_contract_tests.c`, every refusal of these calls with its
    code and message, headless. Built and tested on macOS: Rabbittus'
    27 tests, plain and under ASan; `../rabbittustests`' GPU tests on Metal
    under validation with warnings fatal; a caller's 35 and another's 45
    tests over it. Windows and Linux not built.
  - Left in this header: `rbtrend_instance_next_frame` (A10, step 1d).
  - **Group 2, images: done 2026-10-29.** `rbtrendimages.h`: the 2D and 3D
    texture creates, updates, readbacks, resizes and mip generation, the
    view creates and the sampler create return `RBT_RESULT`, handles through
    `out_`, an `RBTREND_ERROR` on each that can fail for a reason worth a
    sentence; the desc getters write through `out_desc`. An update or a
    readback takes `const void *data` (`void *out_data`) and its
    `data_size`, which must be the region's bytes exactly, in place of the
    `void **ppData` it took. Renamed `RBTREND_TEXTURE2D_DESC`,
    `RBTREND_TEXTURE3D_DESC`, `RBTREND_TEXTURE_VIEW_DESC`,
    `RBTREND_SAMPLER_DESC`, `RBTREND_COLOR_TARGET` and `RBTREND_DEPTH_TARGET`.
    Every create promises its destroy's retire, a resize the old image's,
    and an upload or readback its own buffer's, so none of their retires
    can find no room. `rbtrendimages.cpp` prints nothing. The internal
    helpers under them (`rbtrend_record_immediate`,
    `rbtrend_immediate_on_texture`, `rbtrend_wait_immediate`,
    `rbtrend_instance_open_upload_batch`, the mip chain's) report through an
    `RBTREND_ERROR`; the framebuffer clears and `GENERATE_MIPS`, not yet
    converted, print what they hand back until their groups. Tests: every
    image call's NULL refusal headless; a caller's GPU tests check the
    region rules (sizes, NULLs, bounds) and a write read back exactly, and
    the struct base refusals by code and message. Built and tested on
    macOS (Rabbittus plain and ASan, the GPU tests on Metal under
    validation, a caller's 35 tests and another's 45).
  - **Group 3, shaders and pipelines: done 2026-10-29.** `rbtrendshader.h`
    and `rbtrendpipeline.h`: the shader creates (from memory and from a
    file), `rbtrend_instance_get_fullscreen_vertex_shader`, both pipeline creates,
    both binding layout creates and the binding set create give their
    handle through `out_` with an `RBTREND_ERROR`; `rbtrend_binding_set_update`
    takes one; the desc getters write through `out_desc`; the layout
    lookups (`_find`, `_get_binding`, `_get_block`, `_get_uniform_layout`)
    return `RBT_RESULT`, `RBT_RESULT_NOT_FOUND` where nothing matches. A file
    that isn't there is `RBT_RESULT_NOT_FOUND`, one that can't be read
    `RBT_RESULT_IO`; a set of more bindings than a layout takes
    `RBT_RESULT_LIMIT`. Every reflection mismatch a pipeline create finds is
    its message. Renamed `RBTREND_SHADER_MEMBER`, `RBTREND_SHADER_RESOURCE`,
    `RBTREND_SHADER_INPUT`, `RBTREND_SHADER_REFLECTION`,
    `RBTREND_BINDING_DESC`, `RBTREND_VERTEX_ELEMENT`, `RBTREND_VERTEX_LAYOUT`,
    `RBTREND_VERTEX_BINDING_DESC`, `RBTREND_GRAPHICS_PIPELINE_DESC`,
    `RBTREND_COMPUTE_PIPELINE_DESC` and `RBTREND_BINDING_SET_ENTRY`. Every
    create promises its destroy's retire. `rbtrendpipeline.cpp` prints only
    from `rbtrend_binding_set_matches_images`, which a compile calls (group
    7). Rend's built-ins (`rbtrendbuiltin.cpp`) still print what they
    can't make until the commands group. Built and tested on macOS as
    group 2 was.
  - **Group 4, framebuffers, passes, timers and transient memory: done
    2026-10-29.** `rbtrendframes.h`, `rbtrendpass.h`, `rbtrendtimer.h`,
    `rbtrendtransient.h`: the framebuffer, render pass, compute pass and
    timer creates give their handle through `out_` with an
    `RBTREND_ERROR`; the framebuffer's resize and clears, the passes'
    `_set_binding_set`, `rbtrend_gpu_timer_get_ms` (`RBT_RESULT_NOT_FOUND`
    for a slot with no time yet) and `rbtrend_transient_allocate`
    (`RBT_RESULT_LIMIT` with no transient memory or past its limit) take one;
    `rbtrend_framebuffer_get_desc` and `rbtrend_render_pass_get_size` return
    `RBT_RESULT`. Renamed `RBTREND_FRAMEBUFFER_TEXTURE_SPEC`,
    `RBTREND_FRAMEBUFFER_DESC`, `RBTREND_RENDER_PASS_DESC`,
    `RBTREND_COMPUTE_PASS_DESC`, `RBTREND_GPU_TIMER_DESC` and
    `RBTREND_TRANSIENT_SPAN`. A timer's create promises its retire; the
    framebuffer and the passes are freed at once, as before. What still
    prints in these files is what a compile reads (a span's or a
    framebuffer's state at `BEGIN_RENDER_PASS`), for the commands group.
    `rbtrendpost.h`'s exposure goes with lit, where it is built. Built and
    tested on macOS as group 2 was.
  - **Group 5, materials, lit and exposure: done 2026-10-30.**
    `rbtrendmaterial.h`, `rbtrendlit.h`, `rbtrendpost.h`: the material
    create and copy, `rbtrend_lit_material_create`,
    `rbtrend_lit_environment_create`, `rbtrend_lit_scene_create` and
    `rbtrend_exposure_create` give their handle through `out_` with an
    `RBTREND_ERROR`; so do the lit getters made on first use
    (`rbtrend_lit_get_shader`, `_get_scene_layout`, `_get_shadow_layout`,
    `_get_material_layout`). Every material setter and lookup, every lit
    material and scene setter, `rbtrend_lit_fit_sun_cascades` and both
    shadow view-projection calls return `RBT_RESULT` with an error
    (`RBT_RESULT_NOT_FOUND` where no binding or member has the name).
    `rbtrend_material_is_complete` stays a predicate. Renamed
    `RBTREND_MATERIAL_DESC`, `RBTREND_MATERIAL_UNIFORM`, `RBTREND_LIT_VERTEX`,
    `RBTREND_LIT_INSTANCE`, `RBTREND_LIT_MATERIAL_VALUES`,
    `RBTREND_LIT_ENVIRONMENT_DESC`, `RBTREND_LIT_VIEW`, `RBTREND_LIT_SUN`,
    `RBTREND_LIT_SUN_SHADOW`, `RBTREND_LIT_CASCADE_FIT`, `RBTREND_LIT_LIGHT`,
    `RBTREND_LIT_SPOT_SHADOW`, `RBTREND_LIT_POINT_SHADOW`,
    `RBTREND_LIT_AMBIENT` and `RBTREND_EXPOSURE_DESC`. A material's create
    and copy promise its retire. Left printing: what the lit built-ins
    can't make (`rbtrendlit.cpp`'s internal pipelines and placeholders, as
    `rbtrendbuiltin.cpp`'s), which a create reports as
    `RBT_RESULT_REND_GPU_FAILURE` with the part named, and what a compile
    or a submit finds (`rbtrend_material_prepare`,
    `rbtrend_material_stage_uploads`). Built and tested on macOS as group 2
    was.
  - **Group 6, draw lists and the swap chain: done 2026-10-30.**
    `rbtrenddrawlist.h`, `rbtrendswapchain.h`: both creates give their
    handle through `out_` with an `RBTREND_ERROR`; `rbtrend_draw_list_add`,
    `_build`, `rbtrend_swap_chain_resize`, `_acquire` and `_present` take
    one; the getters return `RBT_RESULT`. Renamed `RBTREND_DRAW_LIST_DESC`,
    `RBTREND_DRAW_MESH` and `RBTREND_SWAP_CHAIN_DESC`. An acquire Carrottus GPU
    can't make is `RBT_RESULT_REND_GPU_FAILURE` until Carrottus GPU says why
    (out of date, lost device: step 1d). Built and tested on macOS as
    group 2 was.
  - **Group 7, command lists: done 2026-10-30.** `rbtrendcommands.h`:
    `rbtrend_command_list_create` gives the list through `out_` and promises
    its retire; `_compile` and `_submit` return `RBT_RESULT` with an
    `RBTREND_ERROR`. A compile's message names the send it is about,
    counted from the reset, a send that stands for others
    (`BEGIN_RENDER_PASS`, `SET_COMPUTE_PASS`, `DRAW_LIST`) being one:
    "command 2: draw command recorded outside a rendering pass". A command
    refused when it was sent is reported by the next compile, at its send.
    A submit of a list compiled against what has changed since is
    `RBT_RESULT_REND_STALE` (5002, new). How it works: for the length of a
    compile or submit the list points at the caller's error, and the
    instance at the list (`rbtrend_cmd_report`, `rbtrend_report`,
    `rbtrend_cmd_report_scope`), so every check a compile reaches - the
    pass and draw list expansions, a material's preparation and uploads,
    the built-ins and lit's internal pipelines, the render target pool, a
    set's images against a pipeline's shaders - reports the first failure
    there, and nothing prints; a recording's first failure is kept on the
    list for the next compile. Outside a compile those internal checks
    keep nothing: a create that reaches them reports its own failure.
    `rbtrend_command_list_send` and `rbtrend_command_list_reset` stay `void`, as
    Vulkan's recording calls are. Tests: the index and code of a compile's
    failure, a submit before any compile, and a stale submit, in a
    caller's GPU tests. Built and tested on macOS as group 2 was.
  - **A10, `rbtrend_instance_next_frame`: done 2026-10-30.** It returns
    `RBT_RESULT`, takes a `timeout_ns` (UINT64_MAX without limit, 0 only
    looks) and gives the new index through `out_frame_index`, with an
    `RBTREND_ERROR`. It waits before it changes anything, so a wait that
    runs out (`RBT_RESULT_TIMEOUT`, 12, new and general) or fails leaves the
    frame where it was and the call can be made again; a lost device is
    `RBT_RESULT_REND_DEVICE_LOST` (5003, new), from Carrottus GPU's new
    `CR_RESULT_GPU_DEVICE_LOST`, which `rbtrend_instance_wait_idle` and the
    submissions pass on as well. A failed submission of the frame's
    updates and clears stops it there too (it dropped them before), and a
    command allocator's failed reset is its failure, not a print. Before,
    a failed wait moved the frame anyway. Carrottus GPU's half: Vulkan maps
    `VK_ERROR_DEVICE_LOST` from the fence wait and the submissions, D3D12
    asks `GetDeviceRemovedReason` before and after a fence wait and around
    a submission; both written on macOS and not compiled (no Vulkan or
    D3D12 headers here): the Windows run compiles and runs them. Metal
    never returns it: a device on Apple Silicon isn't lost
    (`MTLCommandBufferErrorDeviceRemoved` can't occur there, as the macOS
    27 SDK says), a faulting command buffer failing alone. Tests: a turn
    with no time to wait turns or runs out leaving the index, in a caller's
    GPU tests; a NULL instance headless. A lost device isn't made to
    happen by any test.
  - **A14, `rbtrendbuffers.h`: done 2026-10-30.** One `rbtrend_buffer` for
    every use, made from an `RBTREND_BUFFER_DESC` (new type 5025): a size, a
    usage mask (`RBTREND_BUFFER_USAGE_VERTEX`, `_INDEX`, `_UNIFORM`,
    `_STORAGE`, `_INDIRECT_ARGUMENTS`), `RBTREND_BUFFER_WRITES` (ONCE or
    MANY, NONE refused), the index stride for an index buffer, `per_frame`,
    the initial data and a debug name, copied. A mask is one bit, or
    STORAGE with INDIRECT_ARGUMENTS: the states a command list can move a
    buffer between; any other is `RBT_RESULT_UNSUPPORTED` until a list can.
    `rbtrend_buffer_create`, `_destroy`, `_get_desc`, `_write`, and
    `rbtrend_buffer_write_uniform` with the caller's `RBTREND_UNIFORM_LAYOUT`
    (a buffer keeps no layout); `rbtrend_uniform_layout_get_size` sizes a
    block and `_get_uniform_index` returns `RBT_RESULT` (`NOT_FOUND`). A
    uniform buffer is UPLOAD memory mapped for life on every device and
    starts as zeroes, as before; any other is kept as the vertex, index and
    storage buffers were, and a buffer written directly is now mapped once
    for its life rather than at each write. Buffer sets are gone:
    `per_frame` makes `frames_in_flight` copies behind one handle, the
    current frame's written and the compiled frame's read, so vertex, index
    and argument buffers may have copies too, and a binding set entry,
    `SET_VERTEX_BUFFERS`, `SET_INDEX_BUFFER`, an indirect draw and the
    material's setters take one `rbtrend_buffer` (their `_set` members and
    arguments went). Each use checks the usage it needs: the sends, binding
    sets, materials, draw lists. The pools stay, over `rbtrend_buffer`
    blocks named after the pool (`debug_name`, new in their descs), and
    return `RBT_RESULT` with an `RBTREND_ERROR`; a bad free is
    `RBT_RESULT_INVALID_ARGUMENT`. Renamed `RBTREND_UNIFORM`,
    `RBTREND_UNIFORM_LAYOUT` (its `uniforms` now `const`),
    `RBTREND_VERTEX_POOL_DESC`, `RBTREND_VERTEX_RANGE`,
    `RBTREND_INDEX_POOL_DESC`, `RBTREND_INDEX_RANGE`; `RBTREND_DEFAULT_VERTEX`,
    which nothing used, removed (no mesh types). Every create promises its
    destroy's retire, and a ONCE buffer's write promises the retire of the
    buffer it stages in. `rbtrendbuffers.cpp` and `rbtrendpools.cpp` print
    nothing. Tests: every NULL refusal and the layout calls headless
    (`rbtrend_contract_tests.c`); in a caller's GPU tests each create
    refusal by code and message, a named buffer's desc and message, writes
    to per-frame copies across frames on a two-frame instance, and the
    usage refusals of a send, a binding set and a material. Built and
    tested on macOS as group 2 was. Not run: copies drawn from (the GPU
    tests' instance has one frame in flight, as before), and Windows and
    Linux.
  - **The conversion is finished (step 1f, 2026-10-30).** Nothing in
    Rend prints but the Debug build's check of overlapping calls, a
    diagnostic that refuses nothing (`rbtrendcontext.h`, "Threads"); the
    headers say so (`rbtrendcontext.h`, "Results and errors"). Every retire
    is promised by what made the object: each create, a resize's old
    image, an upload's and a readback's staging buffer, a ONCE buffer's
    staged write (checked call by call); `rbtrend_instance_retire` asserts
    a promise in Debug, and its print for an unpromised one went, as did
    `rbtrend_base_ok` (every based struct is checked by
    `rbtrend_base_check`). `rbtrend_command_list_send` and
    `rbtrend_command_list_reset` return nothing, as Vulkan's recording
    calls don't, and a refused send is reported by the next compile. A
    caller's GPU tests no longer print "expect N refusals" before checks
    whose refusals now print nothing. Run on macOS (Debug) on 2026-10-30:
    Rabbittus' 27 tests plain and under ASan; the GPU tests on Metal
    under API and shader validation with warnings fatal, both devices, and
    under `leaks --atExit` (no leak without Metal's debug layer; with it,
    six `MTLDebugSamplerState` wrappers of the layer's own object cache,
    made for Rend's samplers, which are released); a caller's 35 tests
    and another's 45. **Run on Windows the same day** (ThinkPad P1, MSVC
    14.51): Rabbittus' tests on D3D12 and Vulkan in every
    configuration and under both `-asan` presets; the GPU tests on D3D12
    (Iris Xe, RTX A2000, WARP; debug layer, and GPU-based validation
    once) and Vulkan (both GPUs; validation, synchronization and
    GPU-assisted validation), every check passing (`../rabbittustests/README.md`);
    both callers' suites on both backends; Carrottus GPU's lost-device code
    compiled for the first time on D3D12 and Vulkan. The first MSVC build
    warned C4297 on a throwing helper inside an `extern "C"` block: MSVC
    assumes C linkage never throws and may drop the catch, so Rabbittus
    builds with `/EHs` there (`../claude_made_decisions.md`). **On Linux**
    (Fedora): the same over Vulkan, with the ASan and TSan presets. A
    device lost is not made to happen by any test.
  - **No allocation per frame (2026-10-30, Windows):** a settled frame
    made 147 allocations in Debug and 129 in Release
    (`../rabbittustests`' `test_frame_allocations`), each put down to its
    asker. Now none, on D3D12 and Vulkan: a command list keeps its
    compile's and submit's working arrays (pass images, stores, argument
    buffers, barriers, upload barriers and copies, the lit scenes' copy of
    the materials, the label stack) as `eq::array`s cleared at each use;
    the batch keeps its line segments; a material set to the resource it
    holds already (by the handle's serial from its instance, not its
    address) doesn't rebuild its binding set; a transient chunk keeps a
    view per offset and stride, reaching its end, and a rewind destroys
    the ones not used. Carrottus GPU's recording calls and submissions keep
    their converted arrays the same way. On the RTX A2000, Release, the
    frame's CPU time after the wait went from 122 to 106 us. Not run on
    the Mac.
  - **Destroyed handles (card X16, 2026-10-30):** every Rend handle
    holds a Debug-only tag (`src/rbtstale.hpp`); a destroy writes it dead
    at the call (the release queue frees it later), and every call refuses
    a destroyed handle as it refuses null, printing one line.
    `../rabbittustests`' `test_destroyed_handles` calls each function
    taking a Rend handle first with a destroyed one: 167 calls, all
    refused, D3D12 and Vulkan, both GPUs. Three lit structs
    (`rbtrend_lit_environment_t`, `rbtrend_lit_scene_t`,
    `rbtrend_exposure_t`) had no `debug_name` first, so `rbt_debug_name_set()`
    overwrote their first member; fixed.
  - **The names (2026-10-30):** `rbtrend_send_command` is
    `rbtrend_command_list_send`, every caller with it; the value structs'
    renames were made group by group. Nothing of the decision is left.

The workspace's error policy is in the root `CLAUDE.md`, "Errors".

## State of the code

- **Compiled on macOS on 2026-10-10; run there in part.** Everything in this section
  built with no errors and no warnings: Rend with Clang, against
  Carrottus GPU's Metal backend, through a caller's macOS Metal
  preset. That is two builds the same day: the first covered the three
  lists that follow, the second everything after them. Each needed one fix
  outside this repo. The first: `crgpu.h` had lost the declarations of the
  command allocator and command list calls and `crgpu_command_queue_wait_idle` in
  the fence rewrite, and they were put back. The second: a rename had run
  over the cube view constants in Carrottus GPU's Metal image backend.
  Still to do, and what "compiled" below doesn't mean:
  - One application has run on it, on macOS over Metal, 2026-10-11, and
    drew correctly. That exercises what that application uses and no more:
    instance creation, shaders and graphics pipelines (one of them with a
    push constant range, created and never drawn with), a uniform buffer
    in a binding set, vertex buffers written directly (unified memory) and
    destroyed through the release queue, 2D color and depth views, passes
    to a texture, and the swap chain. Each "none of it has run" below
    still stands for everything else: no push constant set, nothing
    staged, no compute, no indirect draw, no view of part of a texture, no
    layered pass. Nothing checked the release queue for an early release
    beyond the picture being right. That list is as of 2026-10-12. Since,
    `../rabbittustests` has run compute and storage ordering, the staged path
    (on the RTX A2000) and indirect draws (see "Suggested order", step 1);
    and on 2026-10-22 on Metal, and on 2026-10-23 on D3D12 and Vulkan, a
    layered pass and mesh shading.
  - The repo's headless tests passed then. Two of them are Rend's and
    need no device (`rbtrendranges_tests`, `rbtrendreflect_tests`, both
    registered with ctest); what is known of each run is in the last
    bullet of this section.
  - Which compiler built it on Linux is not recorded here (see the Linux
    bullet below), so "compiled with GCC" is still unconfirmed.
- **Compiled and run on Windows on 2026-10-11.** Everything in this section
  down to "View aspects and stencil sampling" built there that day, with the
  Carrottus GPU it depends on, and one application ran on it and exited with code
  0. The rest of the section was built there by 2026-10-12 (the bullet
  after the Linux one). That
  is the first build of this code outside macOS and the first of a Carrottus GPU
  backend other than Metal. The backend was D3D12 (a caller's host);
  Vulkan has not been built on Windows (it was on 2026-10-16, by
  `../rabbittustests`). As on macOS, it exercises what
  that application uses and no more (the list above); whether the device
  had memory of its own, which is what would have run the staged path, is
  not recorded either.
- **Compiled and run on Linux, recorded 2026-10-12.** Rend built there
  against Carrottus GPU's Vulkan backend and one application ran on it. That is the first build and the first run of Rend on Vulkan,
  and with it one application has run on all three backends: Metal on
  macOS, D3D12 on Windows and Vulkan on Linux. The Wayland driver issue that held the Linux
  machine no longer holds it. Not recorded: the compiler, the device, and
  whether the device had memory of its own (the staged path). As on the
  other two, it exercises what that application uses and no more (the list
  above).
- **Everything in this section is compiled on all three backends as of
  2026-10-12.** The Windows (D3D12) and Linux (Vulkan) runs were built
  from the current sources, as was the macOS (Metal) one, so every file in
  the library's source list, and the Carrottus GPU it rests on, has been through
  MSVC, the Linux compiler and Clang. "Compiled" in the bullets below
  means that and no more: "never run" and "none of it has run" still
  stand wherever the application doesn't use the thing, on every backend.
  The tests under `tests/` are not part of those builds; what is known of
  each is said where it is described.
- **The changes of 2026-10-09 are compiled on all three backends**:
  - `rbtrend_index_buffer_get_stride`, `rbtrend_buffer_element_type_get_size` and
    `rbtrend_buffer_layout_get_element_index` defined (the header declared them
    and nothing defined them).
  - `rbtrend_uniform_buffer_update_by_name`, `rbtrend_texture2d_resize` and
    `rbtrend_texture3d_resize` implemented (they returned false).
  - `rbtrend_command_list_send` copies the arrays of `SET_VERTEX_BUFFERS`,
    `SET_VIEWPORTS` and `SET_SCISSOR_RECTS` into the command list.
  - Texture update and readback and the framebuffer clears write a texture's
    tracked layout only after their submission succeeds, and leave the texture
    in the layout they found it in.
  - Storage buffers are created host-visible and host-coherent, and
    `rbtrend_storage_buffer_update` checks its range against the buffer's size.
  - `RBTREND_GRAPHICS_PIPELINE_DESC` takes `_cull_mode` and `_front_face_ccw` in
    place of `_backface_culling`.
- **The changes of 2026-10-10 are compiled on all three backends**:
  - `rbtrend_uniform_set` is replaced by `rbtrend_binding_layout` and
    `rbtrend_binding_set`, over every Carrottus GPU descriptor type and all four set
    slots (was item 9). A layout owns the pools its sets come from, 32 sets to
    a pool, and a destroyed set's descriptor set is reused.
  - `rbtrend_sampler`, and `RBTREND_TEXTURE_VIEW_TYPE_SHADER_RESOURCE` for a
    view that is sampled.
  - `RBTREND_COMMAND_SET_BINDING_SET` replaces `SET_UNIFORM_SET`. Sets are bound
    at the draw, and a pass's sampled and storage textures are moved into
    their layouts before the pass begins. Layout tracking covers 3D textures.
  - None of it has run: no texture has been sampled and no storage buffer or
    storage image has been read through a set.
- **The move to Carrottus GPU's memory kinds, 2026-10-10, is compiled on all
  three backends**. `CRGPU_MEMORY_FLAGS` is gone and every buffer names one
  `CRGPU_MEMORY_KIND`; where the bullets below say host-visible or
  device-local, read the kind:
  - Uniform buffers and every staging buffer written by the CPU are `UPLOAD`;
    the texture readback staging buffers are `READBACK`.
  - A vertex or index buffer is `UPLOAD` on a device whose memory is the
    system's and `DEVICE` with a staging buffer otherwise, as before.
  - A storage buffer is `DEVICE_MAPPABLE` on a device created with
    `CRGPU_FEATURE_DEVICE_MAPPABLE_MEMORY` (before 2026-10-21, a device
    property), and `DEVICE` with a staging buffer otherwise (was item 2).
    `rbtrend_instance_create` reads the device's features (item 39).
  - Every write through a mapping is followed by `crgpu_buffer_flush`:
    `rbtrend_buffer_store_write`, `rbtrend_uniform_buffer_update` (whose buffer
    stays mapped) and both texture update paths. The readbacks already called
    `crgpu_buffer_invalidate`.
  - `memory_flags` is gone from `RBTREND_TEXTURE2D_DESC` and
    `RBTREND_TEXTURE3D_DESC`: a Carrottus GPU image has no memory kind.
  - Carrottus GPU's heap and resource flags are gone too, and with them any
    zeroing: a new buffer's or image's contents are undefined on every
    backend. `rbtrend_uniform_buffer_create` zeroes its block for that
    reason. A vertex, index or storage buffer created with no data stays
    unwritten, as its header already said, and so does every texture level.
- **A second set of changes on 2026-10-10 is compiled on all three backends**. The
  numbers are the items they closed:
  - Vertex, index and uniform buffers ask for host-visible and host-coherent
    memory, as storage buffers do (was item 2).
  - Desc fields nothing read are gone (was item 4): `store_locally`,
    `initial_data` and its pitches on both texture descs, `_line_width` on the
    pipeline desc, `swap_chain_target` on the framebuffer desc, and the app
    and engine names and versions on `RBTREND_INSTANCE_DESC`, which is now the
    device alone. A view's `RBTREND_TEXTURE_VIEW_TYPE` is checked against the
    texture's usage bits, and a sampled view of a depth and stencil format is
    of the depth alone.
  - `_vertex_entry_point` and `_fragment_entry_point` on the pipeline desc,
    required and copied (was item 5).
  - The texture update and readback calls and the framebuffer clears take the
    queue they submit on (part of item 6).
  - Removed: the unused SPIR-V loader, the instance's unused command list, the
    forward declaration of a pipeline layout type, three unused includes.
    `rabbittus.h` includes all seven Rend headers (part of item 7).
  - Push constants (was item 10): `_push_constant_ranges` on the pipeline
    desc and `RBTREND_COMMAND_PUSH_CONSTANTS`.
  - Vertex bindings (was item 11): `_vertex_bindings` replaces
    `_vertex_layout`, each binding per vertex or per instance, read from the
    vertex buffer slot of the same number. A draw fails the compile if a slot
    its pipeline reads is empty or holds a buffer of another stride.
  - A pipeline is set inside a pass and lasts until the pass ends.
    `SET_SHADER_PIPELINE` outside a pass, and a draw in a pass that has set
    none, fail the compile. Before, a pipeline carried from one pass to the
    next in Rend's tracking while the backend had none bound.
  - A pass takes only the binding sets its draws read (was item 32's first
    point).
  - A uniform buffer is sized from its uniforms' offsets, with std140's array
    stride, and `rbtrend_uniform_buffer_update_by_name` writes an array's
    elements at that stride (item 1's small fix).
  - Texture update and readback take a mip level, and the 2D ones an array
    layer (part of item 3).
  - Uploads follow the device's memory (item 13). Vertex, index and
    storage buffers are host-visible and written directly on a device whose
    memory is the system's. On a device with its own they are device-local,
    with a host-visible staging buffer beside each; a write goes to staging,
    and `rbtrend_command_list_submit` copies the dirty range across in an
    upload list submitted ahead of the list's own. Uniform buffers are
    host-visible on every device. `rbtrend_instance_create` reads
    `CRGPU_ADAPTER_PROPERTIES::unified_memory` of the device's adapter
    (before 2026-10-21, a device property), and the copy uses
    `CRGPU_RESOURCE_STATE_COPY_DEST`: both are new in Carrottus GPU, and built
    on all three backends since. The vertex and index update calls check their range.
  - Frame pacing (item 12). The instance has a fence; every submission
    Rend makes (command lists and the immediate operations) signals it
    through `crgpu_command_queue_submit`, and `rbtrend_instance_next_frame` waits on
    it for the frame that last used the index. `rbtrend_instance_wait_idle`
    waits for everything submitted so far. It rests on Carrottus GPU's fence being a counter on every backend,
    which was rewritten the same day and is built on all three since.
    A wait for serial 0, a frame index nothing was submitted in or an
    instance destroyed before its first submission, returns at once without
    calling Carrottus GPU (2026-10-19): the fence starts at 0, and D3D12's debug
    layer warned at every such wait. Run on Windows, D3D12 and Vulkan.
  - Buffer sets (item 12). `RBTREND_INSTANCE_DESC::frames_in_flight`, a frame
    index moved on by `rbtrend_instance_next_frame`, and
    `rbtrend_uniform_buffer_set` and `rbtrend_storage_buffer_set` with one copy
    for each frame. A binding set entry takes a set in place of a buffer; such
    a binding set keeps a descriptor set for each frame, and a command list
    binds the one of the frame it is compiled in and is refused at submit in
    any other.
  - None of it has run: no push constant has been set, no per-instance buffer
    read, no mip below 0 written, and no staged buffer copied: every device
    this has been near is unified, so the staged path is the least tested
    code in Rend.
- **Compute, written 2026-10-10 and compiled on all three backends** (item 14):
  - `rbtrend_shader` accepts the compute stage and remembers its stage; a
    pipeline refuses a shader given as another stage than its own.
  - `rbtrend_compute_pipeline`: a shader, entry point, binding layouts and push
    constant ranges, over the same `rbtrend_binding_layout` objects a graphics
    pipeline declares.
  - `RBTREND_COMMAND_SET_COMPUTE_PIPELINE` and `RBTREND_COMMAND_DISPATCH`,
    outside a pass. `SET_BINDING_SET` serves both, and `PUSH_CONSTANTS`
    outside a pass writes the compute pipeline's block.
  - Right before a dispatch, the textures of its sets are moved into their
    layouts and its storage buffers and storage images are ordered after
    earlier writes. A pass does the same ordering for the sets its draws
    read; before, a storage image got an image barrier and a storage buffer
    nothing.
  - A binding set records every storage buffer it holds, not only the staged
    ones.
  - None of it has run: no compute shader has been dispatched on any backend.
- **Indirect draws and mesh shaders, written 2026-10-10 and compiled on
  all three backends** (item 15):
  - A storage buffer, or a storage buffer set, created with
    `RBTREND_STORAGE_BUFFER_USAGE_INDIRECT_ARGUMENTS` is the argument buffer of
    `RBTREND_COMMAND_DRAW_INDIRECT` and `RBTREND_COMMAND_DRAW_INDEXED_INDIRECT`,
    whose entries are checked against the buffer's size when the command is
    sent. The same buffer goes in a storage slot for a compute shader to
    fill. There is no indirect buffer type.
  - A command list tracks the state of such a buffer: it is moved to the
    indirect argument state before a pass that draws from it and back before
    a dispatch or pass that has it in a binding set, and
    `rbtrend_command_list_submit` puts it in the storage state ahead of the
    list's own commands.
  - `rbtrend_shader` accepts the mesh stage. A graphics pipeline takes a
    `mesh_shader` in place of its `vertex_shader`, has no vertex input, is
    refused on a device without mesh shading, and is run with
    `RBTREND_COMMAND_DISPATCH_MESH`. A draw command with a mesh shader
    pipeline, or a mesh dispatch with any other, fails the compile.
  - An indexed draw, indirect or not, fails the compile if no
    `SET_INDEX_BUFFER` came before it.
  - `RBTREND_RENDERING_FLAG_EXECUTES_BUNDLES` and `BEGIN_RENDERING`'s `_flags`
    are gone (item 33).
  - None of it has run.
- **The release queue, written 2026-10-10 and compiled on all three
  backends** (item 12):
  - An instance has a queue of what was destroyed while the GPU might still
    use it. Each entry carries the submit serial current when it was
    retired and is released, front first, once the instance's fence has
    reached it: after each submission, in `rbtrend_instance_next_frame` and
    `rbtrend_instance_wait_idle`, at each destroy call, and at
    `rbtrend_instance_destroy`, which waits first.
  - Every destroy call of a GPU-backed handle goes through it: the four
    buffer types, both texture types, texture views, samplers, shaders, both
    pipeline types, binding layouts and sets, and command lists. None of
    them needs the GPU to be finished any more. Uniform, vertex, index and
    storage buffers and binding sets now store their instance for it.
  - A resized texture's old image, and a 3D texture's old view, are retired
    on it.
  - Texture updates and framebuffer clears are submitted and not waited for;
    their list, allocator and staging buffer are retired. A readback still
    waits. (Since 2026-10-15 they are recorded into the frame's upload
    batch instead: see the bullet of that date.)
  - An instance submits on one queue and refuses another.
  - The queue has a mutex and the submit serial is atomic, so a destroy call
    is safe from any thread: it pushes an entry under the lock and releases
    nothing. Releasing is done outside the lock by the thread using the
    instance.
  - None of it has run. The first thing to check is that nothing is
    released early: a release queue that is wrong shows as rare corruption.
- **The thread contract is in the headers** (2026-10-10, comments only):
  `rbtrendcontext.h` holds the rule and the other six say what is particular
  to them. It describes the code as it is and changes none of it.
- **Views of a range, cubes and per-mip layouts, written 2026-10-10 and
  compiled on all three backends** (items 16 and 17):
  - `RBTREND_TEXTURE_VIEW_DESC` replaces the `type` argument of the two view
    create calls: a use, a shape (2D, 2D array, cube, cube array, 3D) and a
    mip and layer range, with `RBTREND_TEXTURE_VIEW_REMAINING` for "to the
    end". A 2D texture with several layers is viewed as a 2D array; before,
    it was asked for as a plain 2D view, which is invalid on Vulkan.
  - `RBTREND_TEXTURE2D_DESC::cube_capable`. A cube is a texture and a view.
  - A texture tracks a layout for each mip of each layer, and a command list
    an entry for each it touches. A view is moved over its range, with one
    barrier where the range is in one layout. Conflicts in a pass or a
    dispatch are by overlap, so two views of one texture that share no mip
    of a layer can be read and written together.
  - The texture update, readback and clear calls move only the mip of the
    layer they touch.
  - A storage image's ordering barrier is Carrottus GPU's layout barrier from
    GENERAL to GENERAL over the view's range, in place of a pipeline barrier
    over the whole image.
  - Carrottus GPU changes it rests on, built on all three backends: cube-capable images, D3D12
    state for each subresource, Vulkan's whole-image barrier covering every
    mip and layer, and the storage layout meaning the storage state on D3D12.
  - None of it has run. A view of part of a texture has never been created.
- **Layered passes, written 2026-10-10 and compiled on all three
  backends** (item 16):
  - A view of type RENDER_TARGET or DEPTH_STENCIL may be a 2D array of one
    mip, on a device created with `CRGPU_FEATURE_LAYERED_RENDERING`
    (before 2026-10-21, a device property read once into the instance);
    on any other, creating one fails.
  - BEGIN_RENDERING over 2D array views is a layered pass: the compile
    refuses targets that differ in shape or layer count, and the layer count
    goes to Carrottus GPU as `crgpu_rendering_begin_desc::layer_count` (0 for a
    pass over 2D views). Layouts and conflicts needed nothing new: a target
    is already moved, and checked for overlap, over its view's range.
  - Carrottus GPU changes it rests on, built on all three backends: `layer_count`, the
    layered rendering feature (then a property; it enables `shaderOutputLayer` on
    Vulkan where the device has it), and Metal pipelines always stating
    their primitive class, which Metal requires in a layered pass.
  - No shader that writes a layer had been through Carrottus GPU's SPIR-V
    cross-compile to HLSL or MSL, and no layered pass had run on any
    backend, until `test_layered_pass` (2026-10-22) ran one on Metal, the
    layer chosen in the vertex shader; on D3D12 and Vulkan on 2026-10-23
    (Iris Xe and RTX A2000), where Vulkan needed
    `VK_EXT_shader_viewport_index_layer` enabled for the SPIR-V 1.0 shader
    (item 39).
- **View aspects and stencil sampling, written 2026-10-11 and compiled on
  all three backends** (item 16):
  - `RBTREND_TEXTURE_VIEW_DESC::aspect` (COLOR, DEPTH, STENCIL,
    DEPTH_STENCIL), required and checked against the texture's format and
    the view's type. Before, Rend chose it from the format, and a sampled
    view of a depth and stencil texture was always the depth.
  - A SHADER_RESOURCE view with the STENCIL aspect is what a shader samples
    the stencil through, as an unsigned integer.
  - Carrottus GPU changes it rests on, built on all three backends: named aspect
    bits and one rule for a view's aspect; Metal's stencil view format; and
    on D3D12 a view that keeps a shader resource desc and an unordered
    access desc beside its attachment desc, readable formats for depth and
    stencil, and typeless depth resources. The last three also mean a
    texture that is rendered to and sampled, color or depth, was not
    correctly sampled on D3D12 before. The D3D12 run of 2026-10-11 was
    without these changes; the build recorded on 2026-10-12 has them.
  - No stencil had been sampled on any backend until
    `test_stencil_sampling` (2026-10-22 on Metal; 2026-10-23 on D3D12 and
    Vulkan. Its shader takes the larger of `.r` and `.g`, so which one
    each API fills isn't something the run shows).
- **`rbtrend_binding_set_update`, written 2026-10-11 and compiled on all
  three backends** (item 9): points some of a set's slots at other resources
  and keeps the handle. The set keeps a copy of the entries it was built
  from; an update merges the given ones in, builds a whole new set, swaps
  its contents into the handle, and destroys the new handle, which carries
  the old descriptor sets onto the release queue. Never run.
- **Transient memory, written 2026-10-11 and compiled on all three
  backends** (`rbtrendtransient.h`, new; items 13 and 23):
  - `rbtrend_transient_allocate` hands out a span of mapped upload memory
    for the current frame; the caller writes vertices or indices into it and
    binds it with `RBTREND_COMMAND_SET_TRANSIENT_VERTEX_BUFFER` (20) or
    `_INDEX_BUFFER` (21). The GPU reads the bytes where they were written.
  - The arena is on the instance (`rbtrendtransient_internal.hpp`,
    `rbtrendtransient.cpp`): a run of chunks for each frame index, bump
    allocated. `rbtrend_instance_next_frame` rewinds the run of the index it
    moves to, after its wait for that frame's submissions. Nothing is
    tracked for a span.
  - `RBTREND_INSTANCE_DESC` gains `transient_chunk_size` (0 is an instance
    with no transient memory) and `transient_memory_limit` (0 is no limit).
    A span larger than a chunk gets one of its own, destroyed at the rewind.
  - Every submission flushes what the frame's spans hold. A compile refuses
    a span of another frame or of an earlier turn of this one, and a submit
    refuses a list that binds spans in any frame but the one it was compiled
    in.
  - The compile now tracks a stride for each vertex slot and whether an
    index buffer is set, in place of the buffer handles, so a slot may hold
    either kind. That touches the draw checks of every list, transient or
    not.
  - Never run. No span has been bound on any backend, and drawing from
    upload memory through a view at an offset is untested on all three.
- **Every header is Javadoc-style Doxygen** (2026-10-11, comments only):
  `rbtrendcontext.h`, `rbtrendbuffers.h`, `rbtrendcommands.h`, `rbtrendframes.h`
  and `rbtrendswapchain.h` were rewritten to the style `rbtrendimages.h` and
  `rbtrendpipeline.h` already had, and the rest of `rbtrendtransient.h` and
  `rabbittus.h` with them. No declaration changed. The calls that had
  no comment have one now, written from the implementation: the framebuffer
  and swap chain calls, the buffer destroy and getter calls, the command
  list's create, destroy and reset, and each draw command. Doxygen reports
  no warnings over `include/render`.
- **Vertex and index pools, written 2026-10-11 and compiled on all three
  backends** (`rbtrendbuffers.h`; `rbtrendpools.cpp`, `rbtrendpools_internal.hpp`
  and `rbtrendranges_internal.hpp`, new; item 13):
  - `rbtrend_vertex_pool` and `rbtrend_index_pool` own a few large vertex or
    index buffers, the blocks, and hand out ranges of them
    (`RBTREND_VERTEX_RANGE`, `RBTREND_INDEX_RANGE`) that last until freed. For
    a caller with thousands of small meshes: a buffer each is a device
    allocation each, which a device limits in number and in smallest size.
  - A range names its block's buffer and its start. It is drawn as any
    buffer is, with the start in `_start_vertex_location`,
    `_start_index_location` or `_base_vertex_location`; a block is an
    ordinary `rbtrend_vertex_buffer` or `rbtrend_index_buffer`, and nothing in
    the command list knows a pool exists.
  - A range is written once, at allocation. That is what makes the write
    safe while the block is being drawn from: the bytes were free.
  - A free is deferred by the instance's fence, in the pool: the range
    carries the submit serial of its free and is handed out again once the
    fence has reached it. It doesn't go on the release queue, since nothing
    is destroyed; the pool looks at what is waiting at each allocate, trim
    and get_usage.
  - Every range has an entry in the pool's table of allocations and
    carries the entry's number and generation. A free is checked against
    the entry, so a range freed twice is refused, also when its elements
    have been handed out again in between, and so is a range of another
    pool. The freed ranges waiting for the GPU are a list through the
    entries, so a free needs no memory.
  - Blocks are added one at a time and given back only by the trim calls. A
    range longer than a block has a block to itself. `memory_limit` in the
    desc; no default block size.
  - `rbtrendranges_internal.hpp` is the list of a block's free elements, with
    nothing of Rend or Carrottus GPU in it, and `tests/rbtrendranges_tests.cpp`
    checks it against one flag an element. Whether that test has been
    built or run is not recorded.
  - Never run. No range has been allocated, drawn from or freed on any
    backend.
- **Materials, passes and draw lists, written 2026-10-11 and compiled on
  all three backends** (items 19, 20 and 21). Before that they were
  type-checked only: GCC 11 on
  Linux with `-fsyntax-only -Wall -Wextra`, with and without `_DEBUG`, over
  every file in `src/render`, and `rabbittus.h` as C11: no errors and
  no new warnings. They are linked into the library now, and have run on
  all three backends (last bullet). Doxygen has
  not been run over the three new headers, so "no warnings over
  `include/render`" above is not known to hold for them.
  - `rbtrendmaterial.h`, `rbtrendmaterial.cpp` (item 19): `rbtrend_material`
    is a binding layout and what each of its bindings holds, by name
    (revised the same day by item 18: names and blocks come from the
    layout). A uniform block the layout knows is one the material owns,
    unless the desc names it external: a CPU copy of its bytes, set by
    member name or as bytes, and the block on the GPU (revised again: see
    "The material arena" below). Textures, samplers and the caller's own buffers are set by slot
    name and array element. Its binding set is built at the first compile
    that binds it and rebuilt (`rbtrend_binding_set_update`) at the next
    compile after a resource changes.
  - `RBTREND_COMMAND_SET_MATERIAL` (22): SET_BINDING_SET with the material's
    set. `rbtrend_command_list_compile` builds every named material's set
    before it records anything, since a pass looks ahead at its sets; that
    is the one thing a compile now changes outside the list. A list keeps
    the materials it binds, and `rbtrend_command_list_submit` copies their
    changed blocks first.
  - `rbtrendpass.h`, `rbtrendpass.cpp` (item 20): `rbtrend_render_pass` is a
    graphics pipeline, its targets (views, or an `rbtrend_framebuffer`
    whose attachments are read when the pass is begun, so it follows a
    resize) with their ops, and the binding sets of the slots it fixes.
    Formats, view types, shapes and layer counts, and each set's layout are
    checked at creation. `RBTREND_COMMAND_BEGIN_RENDER_PASS` (23) is
    expanded when sent into BEGIN_RENDERING, SET_SHADER_PIPELINE, a
    viewport and scissor over the whole target, and SET_BINDING_SET for
    each fixed set; END_RENDERING ends it. `rbtrend_compute_pass` and
    `RBTREND_COMMAND_SET_COMPUTE_PASS` (24) are the same for a compute
    pipeline. A pass is a CPU description: nothing of it is on the GPU, and
    it is freed at destroy.
  - `rbtrenddrawlist.h`, `rbtrenddrawlist.cpp` (item 21): `rbtrend_draw_list`
    takes (mesh, material, instance bytes) draws. `rbtrend_draw_list_build`
    orders them (by material then mesh, or as added), merges draws of one
    mesh and material into one instanced draw, and writes every instance's
    bytes, in batch order, into one transient span. `RBTREND_COMMAND_DRAW_
    LIST` (25) is expanded when sent: the span at the list's instance slot,
    then for each batch SET_MATERIAL, SET_VERTEX_BUFFERS and
    SET_INDEX_BUFFER where they change, and DRAW_INSTANCED[_INDEXED] with
    the batch's first instance as `_start_instance_location`.
  - **Run on all three backends** (2026-10-17). First on Metal (Apple M5
    Pro, unified memory):
    `../rabbittustests`' `test_material`, `test_render_pass` and
    `test_draw_list` passed on their first build, Debug under the Metal
    validation settings and Release, with no validation message and no
    leak. Nothing in Rend needed fixing. What they covered: a layout made
    from two shaders and found by name; materials set by member name (a
    bare path, and `u_material.layers[1].add` qualified by its binding),
    a texture and a sampler; a value changed between two submissions of
    one frame, each submission drawing its own; a texture swapped, the
    set rebuilt at the next compile; an incomplete material refused at the
    compile; instanced draws from `_start_instance_location` 1 (the first
    of the two checks this entry asked for). A render pass over a view,
    over a framebuffer with a borrowed texture, with a fixed set changed
    after a recording (the recording keeps the old set) and with its slot
    left open; five descs refused; a compute pass with a fixed set. A draw
    list in both orders: three batches from four draws, one of them
    indexed, replayed into two passes of one list; the last added on top
    in submission order; a list changed since its build, a draw with no
    material and a list built in the frame before, each refused. The same
    day they passed on D3D12 and Vulkan on Windows, on an Intel Iris Xe
    and an NVIDIA RTX A2000 (memory of its own, so the material arena's
    copies and the draw list's instance bytes reached device memory
    there), with every other test (reported).
- **Lit surfaces, item 24, written 2026-10-17; run on Metal.**
  `include/render/rbtrendlit.h` (lit surfaces), `include/render/rbtrendpost.h`
  (adapting exposure), `src/render/rbtrendlit.cpp`, `rbtrendlit_internal.hpp`,
  the commands in `rbtrendcommands.cpp`, and 25 shaders in `shaders/` sharing
  `.glslh` includes (glslc `-I`; CMake rebuilds each when one changes).
  - **The shape.** Rend ships the shading, never the geometry: the
    surface fragment shader reads a position, normal, uv and tangent from
    any vertex shader, the scene at set 0 and the material at set 1 (item
    24 has why). The API is a handful of objects and calls, each value
    checked and each refusal printed: shaders and layouts (the
    instance's); a lit material (an `rbtrend_material` with typed values,
    maps and sampler); an environment; a scene (view, sun and its
    cascaded shadow, up to 128 point and spot lights, spot shadows,
    ambient light, ambient occlusion), which is a surface material, a
    block-only material per sun cascade and per spot shadow for the
    shadow passes, and Rend's own for culling and occlusion; an
    exposure. Fitting functions with no GPU in them: the sun's cascades
    for a view (`rbtrend_lit_fit_sun_cascades`, stable: bounding spheres,
    texel snapping) and a spot's shadow matrix. Commands: LIT_ENVIRONMENT
    (a cube or equirectangular picture), LIT_SKY (Preetham's daylight),
    LIT_AMBIENT_OCCLUSION, AUTO_EXPOSURE, TONE_MAP (clamp, ACES, Khronos
    PBR Neutral, AgX; an exposure's adapted value).
  - **Shading**: Cook-Torrance (GGX, Smith-Schlick, Schlick); a clear coat
    (Filament's lobe) and a sheen (Charlie, Neubelt's visibility); point
    and spot lights windowed to their range; a sun with up to four
    cascades, a spot with a shadow of its own, each read through a
    comparison sampler with nine bilinear taps; a hemisphere plus an
    environment (GGX-prefiltered specular chain, irradiance cube) weighted
    by a BRDF lookup texture made once per instance; ambient occlusion
    from the map and from the view; base color, normal, ORM and emission
    maps, cut-outs, a uv transform; linear or sRGB out.
  - **What a compile does for a scene**, at the top of the list: makes the
    BRDF lookup texture once, clears an environment never filled to
    black, and culls the scene's lights for each 16-texel tile of its
    view's target with a compute pass into a buffer set the surface shader
    reads (`rbtrend_cmd_prepare_lit_scenes`). Empty slots hold placeholders
    a block bit says not to read: nothing needs filling, no lit call needs
    a queue.
  - **Fixes below Rend** it brought, in Carrottus GPU: comparison samplers
    (`compare_enable`, `compare_op`) on all three backends; D3D12 begins a
    depth-only pass; Metal binds a set only to the stages whose function
    reads it (from Metal's pipeline reflection), and emits every declared
    resource into a shader's argument buffer struct, so a shader that uses
    some of a set's bindings reads the right ones (carrottus/CLAUDE.md has
    what is still a gap). Vulkan's and D3D12's sides have run, in the
    D3D12 and Vulkan runs below. On 2026-10-17 a caller's hosts also built
    and ran over them on Windows, D3D12 and Vulkan, and on macOS over
    the Metal changes (reported); that caller uses no lit surface, comparison
    sampler or depth-only pass, so that shows only that they break nothing
    there.
  - **What has run.** `../rabbittustests`, on an Apple M5 Pro (2026-10-17),
    Debug under the Metal validation settings with warnings fatal and
    Release, no leak: `test_lit_surface`, `test_lit_mesh_shadow`,
    `test_tone_map` (nine curves and encodings, AgX's against the same
    formula in C), `test_lit_environment`, `test_lit_cascades` (the fit
    on the CPU: splits, framing, texel snapping, refusals; on the GPU:
    the cascade chosen by depth, and past the last), `test_lit_spot_and_
    cutout_shadows` (a spot's shadow, 0 against 231 predicted lit; a
    cut-out's shadow there or not by its cutoff), `test_lit_coat_and_sheen`
    (against the lobes' formulas in C), `test_lit_tiles` (culled per tile,
    texel for texel the same as unculled), `test_lit_sky` (the zenith
    read 161, 255, 255, as the model gives), `test_auto_exposure` (46, 6,
    46, 11 as worked out), `test_lit_ambient_occlusion` (a step occluded
    beside it, 155 of 255; open elsewhere; the scene darkened by it
    there alone). Found on the way and fixed: Metal's argument buffer
    struct for a shader using some of a set's bindings (black sky), a set
    bound to a stage that doesn't read it, push blocks Metal rounds to 8.
    **Passed on D3D12 and Vulkan on 2026-10-17** (Windows 11, Intel Iris
    Xe and NVIDIA RTX A2000, Debug, at 7b18da8 with carrottus 69e6c43; D3D12
    under cdb, Vulkan with the validation layer, both clean), the first
    D3D12 run of a depth-only pass included, after one fix here: the
    clear of an environment never filled was to opaque black, and every
    texture is made with Carrottus GPU's clear value zeroed, so D3D12's debug
    layer warned of a mismatched clear value at each of the 30 faces
    (`CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE`). The faces are now
    cleared to 0 in every channel; only the cubes' color is read.
- **Uniforms found once, written 2026-10-17; run on Metal** (from "Next,
  from the review", its first step). `rbtrend_material_find_uniform`
  turns a member's name into an `RBTREND_MATERIAL_UNIFORM` once, against
  a binding layout, and `rbtrend_material_set_uniform_value` and
  `rbtrend_material_get_uniform_value` write and read it with no string
  read. `rbtrend_material_set_uniform` is now a find and a set.
  - The handle is a value of the layout, its binding's place and the
    member's place in its block, not a pointer into a material: one find
    serves every material of the layout, which is how a caller with one
    material per look and one layout for all of them uses it. It is
    checked at every set against the material's layout (another layout,
    even one of the same bindings, is refused) and against what the
    material owns (a block named external is refused).
  - What it was measured against. Hazel's `Material::Set(name, value)`
    hashes the name into its shader buffer's map at every call and has
    no handle. Unreal resolves a parameter once and sets it by index
    (`UMaterialInstanceDynamic::InitializeScalarParameterAndGetIndex`,
    `SetScalarParameterByIndex`), and its shader parameters are bound to
    offsets once at compile; GL's uniform locations, bgfx's uniform
    handles and Unity's property IDs are the same idea. Rend's is that
    shape in C: a plain struct handed back by value, no allocation, no
    lifetime of its own beyond the layout's.
  - One rule changed with it: a bare name is looked for in every block a
    material of the layout could own, a block some material leaves
    external included, so a name means the same member in every material
    of the layout. Before, it was looked for in the blocks the material
    at hand owned.
  - `../rabbittustests`' `test_material` finds `color` and `strength` and
    checks their type and offset, finds `layers[1].add` by its binding's
    name and its block's and gets the same handle, sets by handle between
    two submissions, reads values back, and is refused a zeroed handle,
    one of another layout made from the same shaders, and one into a
    block the material leaves external. Passed on Metal (Debug under
    validation, Release, no leak), 2026-10-17, and the same day on D3D12
    and Vulkan (Intel Iris Xe and NVIDIA RTX A2000, Debug, debug and
    validation layers clean).
- **Exact staged ranges and buffers written once, written 2026-10-18;
  run on Metal, where neither is staged** (item 13, step 2 of "Suggested
  order"). A staged store's one dirty range is now a list of uploads
  (`rbtrend_buffer_upload` in `rbtrend_internal.hpp`), each copied on its own
  by `rbtrend_command_list_submit`, and `RBTREND_BUFFER_WRITES` is a new
  required argument of `rbtrend_vertex_buffer_create`,
  `rbtrend_index_buffer_create`, `rbtrend_storage_buffer_create` and
  `rbtrend_storage_buffer_set_create` (0 is refused: there is no default).
  - `RBTREND_BUFFER_WRITES_MANY`: the staging buffer of the buffer's size,
    for life, as before. A write merges with the ranges it overlaps or
    touches and no others, so two writes far apart are two copies and
    nothing between them crosses the bus.
  - `RBTREND_BUFFER_WRITES_ONCE`: written at creation only; the update calls
    refuse it. No staging buffer for life: each write is staged in an
    `UPLOAD` buffer of its own size, copied by the next submission that
    uses the buffer, and retired then, so it is released once that
    submission has run. Created with no data, the buffer is never staged.
  - A pool's blocks are `ONCE` buffers written by the pool, a write a
    range: each range is staged on its own and copied exactly, and a block
    keeps no staging copy. The update calls on a block now refuse, as the
    pool's header already said they are not for the caller.
  - The lit scene's light tiles and its placeholder, written by a shader
    only, are `ONCE` with no data: on a device with memory of its own they
    had a staging copy each that was never written.
  - Callers changed in the same change: two callers in the workspace, whose
    four vertex or mesh buffers each are `ONCE`; none is ever updated.
    `../rabbittustests` passes `MANY` for the one buffer it updates and
    `ONCE` for the rest.
  - `../rabbittustests`' `test_buffer_writes`: a `MANY` buffer written in two
    ranges with a gap and then the gap; a `ONCE` buffer drawn in two frames
    and refused an update; a create with `writes` 0 refused; two pool
    ranges of one block drawn from where the pool put them, and the block
    refusing an update. Passed on Metal (Apple M5 Pro, Debug), 2026-10-18,
    with every other test, and callers' tests pass over
    it. That device is unified, so the buffers were written directly and
    none of the new staged code ran there. It ran first on the RTX A2000
    (memory of its own): every test passed on Windows on 2026-10-18, D3D12 under its debug layer and Vulkan under
    the validation layer with synchronization validation, on the Iris Xe
    and the RTX A2000 (reported; step 2 and 3 of "Suggested order" have
    what that run found). Not built on Linux.
- **Stale lists refused, and storage barriers from what shaders write,
  written 2026-10-18; run on Metal** (item 32, step 3 of "Suggested order").
  - The instance keeps every command list compiled and not yet submitted
    (`compiled_lists`, under `release_mutex`). Destroying a texture, a
    vertex, index or storage buffer, a material or a GPU timer, from any
    thread, and resizing a texture call `rbtrend_instance_forget`, which
    marks each such list that uses it stale; `rbtrend_command_list_submit`
    refuses a stale list before it reads anything of it. Before, a destroy
    was a use after free at submit, and a resize was refused only when the
    tracked layout happened to differ. A list records every buffer store it
    uses (`used_stores`), staged or not, so the refusal is the same on every
    device.
  - A pipeline records the storage bindings its shaders write
    (`storage_writes`: declared without `readonly`, used by the entry
    point), and a binding set the binding of each storage image and buffer
    it holds. The list keeps, for each subresource and each storage buffer,
    whether a shader may have used and written it since its last barrier
    (`layout_use::storage_accessed` / `storage_written`, `storage_hazards`;
    both start true, for the submission before). A use needs the ordering
    barrier if it was written since, or if this use writes and it was used
    since. Before, every storage image and buffer of a pass or dispatch got
    one. The lit light cull's dispatch goes through the same preparation
    now, in place of its own barrier, so the tiles it writes are known
    written.
  - The compile walk is inside a `try`: a vector growing out of memory in
    it could throw out of `rbtrend_command_list_compile`, a C entry point.
    The loop is indented one more tab for it; nothing else in it changed.
  - Carrottus GPU, found by the new test: Metal set the compute pipeline state on
    the encoder at every bind, and its validation refuses setting the same
    state twice. It sets it only when it differs now (carrottus).
  - `../rabbittustests`: `test_storage_ordering` (add, copy, copy, add, copy
    over r32f storage images: a write, two reads with none between, a write
    after reads, a read; the values checked) and `test_stale_lists` (a
    target resized, a target destroyed, a vertex buffer destroyed, each
    after a compile and refused at submit, and a list that goes). Every
    test passed on Metal (Apple M5 Pro), Debug under Metal API and shader
    validation with warnings fatal, and Release under `leaks` (none).
    Callers' tests pass over it. On Metal the barriers do
    nothing; every test passed on Windows on 2026-10-18, D3D12 under its debug layer and Vulkan under
    the validation layer with synchronization validation, on the Iris Xe
    and the RTX A2000 (reported; step 2 and 3 of "Suggested order" have
    what that run found). Not built on Linux. Under sync validation `test_storage_ordering` had
    no hazard, so the barrier left out between two reads is not needed.
- **`rbtrend_material_copy`, and no default resources by decision,
  written 2026-10-18; run on Metal** (from "Next, from the review", its
  first step).
  - `rbtrend_material_copy(source)` makes a material of the same layout and
    owned blocks, its own arena ranges holding the source's values, and
    the same resource handles (shared, not duplicated). The two are
    independent after; either may be destroyed first. No binding set and
    no lit scene are copied; the copy's blocks go across at its first
    submission. Create and copy share the owned block's setup
    (`rbtrend_material_own_block`).
  - Compared: Hazel's `Material::Copy` copies the uniform bytes and the
    slot assignments, sharing the resources, and leaves the flags to
    `Init`; Unreal's material instances read what they don't override
    from a parent, live, and its `CopyParameterOverrides` copies values
    once. Rend's is Hazel's shape (a material carries no flags here),
    and not Unreal's inheritance (item 19).
  - Default resources for empty slots are decided against, not left
    undone. Hazel fills every sampled 2D slot with one white texture,
    every cube with a black one and every sampler with one sampler
    (`DescriptorSetManager.cpp`, with its flag forced on); Unreal has no
    empty slot, since every parameter has a default its author gave it.
    What an empty slot should read depends on what it is for (white for
    a base color, black for emission, a flat normal), which Rend
    doesn't know, and a placeholder would have to match each slot's
    shape and scalar type to pass the compile's view checks. So the
    default is the author's, as in Unreal, and the author is the caller:
    a base material with its own choices, copied. Rend's lit material
    is the one place Rend is the author, and it fills its maps.
  - A compile that finds an empty binding names it, its element and what
    is missing (a texture view, a buffer or a sampler), and points at the
    copy. `rbtrend_material_is_complete` is the same search.
  - `rbtrendmaterial.h`, reviewed for the copy: a "Copying" section at the
    top; `rbtrend_material_destroy`'s warning was stale since the stale-list
    check (a list compiled with a destroyed material is refused at submit
    now); the two uniform-buffer setters take a block name as well as a
    binding name, which the code did and the docs didn't say; a view or
    sampler outlives every material that holds it. Nothing renamed or
    removed: the calls were consistent with each other and with the rest
    of Rend.
  - `../rabbittustests`' `test_material_copy`: a complete base copied, the
    copy's values read back, both drawn, each changed alone and drawn,
    the base destroyed and the copy drawn, a copy of NULL refused, and a
    copy of a lit material with its values. Passed on Metal (Apple M5
    Pro), Debug under Metal API and shader validation with warnings
    fatal and Release under `leaks` (none), with every other test.
    Two callers' tests pass over it, 26 suites and 10. Of the 10, a
    model test failed once, in the first run after the
    relink, with its output lost; nine runs since passed, and it calls
    no material code. Every test passed on Windows on 2026-10-18, D3D12 under its debug layer and Vulkan under
    the validation layer with synchronization validation, on the Iris Xe
    and the RTX A2000 (reported; step 2 and 3 of "Suggested order" have
    what that run found). Not built on Linux.
- **A uniform buffer's uniforms found once, written 2026-10-18; run on
  Metal** (from "Next, from the review", its first step).
  - `rbtrend_uniform_layout_get_uniform_index(layout, name)` finds a uniform
    of an `RBTREND_UNIFORM_LAYOUT` by name, once, and
    `rbtrend_uniform_buffer_update_uniform(buffer, index, data)` and
    `rbtrend_uniform_buffer_set_update_uniform(set, index, data)` write it
    with no search. `rbtrend_uniform_buffer_update_by_name` is the lookup on
    the buffer's copy of its layout and then the write, as before (the
    first uniform of the name).
  - The handle is the uniform's index in its layout, not a struct as the
    material's `RBTREND_MATERIAL_UNIFORM` is. A uniform buffer's layout is a
    plain struct the caller writes or reads from shaders and passes at
    create, not a handle, so the index is what names a uniform of it: it
    serves every buffer and buffer set of the layout, a caller that wrote
    the layout knows it already, and it has the shape of
    `rbtrend_buffer_layout_get_element_index` for a vertex layout. It is
    checked against the buffer's layout's count at each write.
  - Every data argument of `rbtrendbuffers.h` is `const void *` now (the
    create and update calls of every buffer and buffer set); each was
    only read. The same symbol and the same ABI, and source-compatible.
  - `../rabbittustests`' `test_uniform_buffer_index`: a uniform buffer and a
    buffer set made from the material shader's uniform layout, written by
    index, bound through two materials' external blocks, drawn green and
    blue, then swapped (the buffer by name, the set by index); unknown
    names find `UINT32_MAX`, an index past the layout and NULL data are
    refused. Passed on Metal (Apple M5 Pro), Debug under Metal API and
    shader validation with warnings fatal and Release under `leaks`
    (none), with every other test; callers' tests pass
    over it. Every test passed on Windows on 2026-10-18, D3D12 under its debug layer and Vulkan under
    the validation layer with synchronization validation, on the Iris Xe
    and the RTX A2000 (reported; step 2 and 3 of "Suggested order" have
    what that run found). Not built on Linux.
- **The uniform buffer cleaned up, written 2026-10-18; run on Metal.**
  From a review of `rbtrend_uniform_buffer_t`:
  - `rbtrend_uniform_buffer_create` checks a layout's `uniforms` against its
    `count` (it read through NULL), refuses a uniform whose type is no
    `RBTREND_UNIFORM_TYPE` (accepted before, and every write of it refused
    later) and a layout of no uniforms and no size, copies nothing for a
    layout of no uniforms (a `malloc(0)` that may return NULL failed the
    call), and prints each failure on its own line with what it was (it
    printed the result string bare, "success" for a failed `malloc`).
    Checks one to a line; the newer conventions throughout the section.
  - A buffer in the device's memory (the material arena's, which only the
    GPU's copies write) is the same struct with no mapping; every update
    call refuses one now, where it would have written through NULL. None
    can reach a caller, so this guards Rend's own code.
  - `rbtrend_uniform_buffer_get_layout` and `rbtrend_uniform_buffer_get_size`,
    as a vertex buffer and a storage buffer have: the layout is what
    `rbtrend_uniform_layout_get_uniform_index` reads, for a caller that
    holds only the buffer.
  - Removed: `rbtrend_uniform_buffer_get_crgpu_buffer_view` and the view
    behind it (made for every buffer and every copy of a set, read by
    nothing: binding sets bind the buffer with an offset and range; no
    caller in the workspace called it), and the stored
    `crgpu_buffer_view_desc`, whose offset was always 0 and was added
    into every write. The struct is the buffer, its size, its layout and
    its mapping.
  - `rbtrendbuffers.h` says for uniform buffers what it said for the others:
    a write lands where the GPU reads, so not while a submission that reads
    the buffer is unfinished; a block rewritten every frame belongs in a
    buffer set.
  - `../rabbittustests`' `test_uniform_buffer_index` checks the getters (the
    buffer's own layout finds the same indices), the three refused
    creates, and a block of no uniforms made with a size and written as
    bytes. Every test passed on Metal (Apple M5 Pro), Debug under Metal
    API and shader validation with warnings fatal and Release under
    `leaks` (none); callers' tests pass over it. Not
    built on Windows or Linux.
- **Buffer offsets follow the device's alignment, written 2026-10-18; run
  on Metal** (the rest of the uniform buffer review).
  - Carrottus GPU reports `uniform_buffer_offset_alignment` and
    `storage_buffer_offset_alignment` in `CRGPU_ADAPTER_PROPERTIES`
    (`CRGPU_DEVICE_PROPERTIES` until 2026-10-21):
    Vulkan's `minUniformBufferOffsetAlignment` and
    `minStorageBufferOffsetAlignment`; D3D12's 256 and 16 (a constant buffer
    view's placement, a raw buffer view's first byte); 256 for both on
    Metal, which has no query. The Vulkan and D3D12 halves were written on
    macOS without either API's headers, and first compiled and run on
    Windows on 2026-10-18 (carrottus b7ac7e6), with every test passing on
    both devices.
  - `rbtrend_binding_set_create` (and `_update`) refuses a uniform or storage
    buffer entry whose `_offset` is not a multiple of the device's
    alignment for its type, naming the entry and the alignment. Before,
    any offset inside the buffer was taken, and one that wasn't aligned
    was outside Vulkan's and D3D12's rules. Every caller bound at 0.
  - The material arena places blocks at the device's uniform alignment
    (16 at the least), not at 256 everywhere (item 19).
  - The uniform buffer update calls, and the set's create and update calls,
    print each refusal, as create does and as the material's calls do;
    they returned false silently.
  - `RBTREND_UNIFORM::size` is `array_count`: it was an element count named
    a size. Source-compatible for a caller that fills the struct by
    position (a caller in the workspace does); one that names the field changes the name.
  - `../rabbittustests`' `test_uniform_buffer_index`: the alignments Carrottus GPU
    reports are powers of two up to 256, an entry at the device's own
    alignment is taken and one at 4 refused (both, on Metal's 256). Every
    test passed on Metal (Apple M5 Pro), Debug under Metal API and shader
    validation with warnings fatal and Release under `leaks` (none);
    callers' tests pass over it. Every test passed on Windows on 2026-10-18, D3D12 under its debug layer and Vulkan under
    the validation layer with synchronization validation, on the Iris Xe
    and the RTX A2000 (reported; step 2 and 3 of "Suggested order" have
    what that run found). Not built on Linux.
- **Vertex buffers are bytes and vertex layouts the pipeline's, written
  2026-10-18; run on Metal.** As Unreal (`FVertexBuffer` and the vertex
  declaration), Hazel 2025 (`VertexBuffer` is a size and its bytes; the
  layouts are in `PipelineSpecification`) and all three APIs have it.
  - `rbtrend_vertex_buffer_create(instance, size, writes, pData)`,
    `rbtrend_vertex_buffer_update(buffer, offset, size, pData)` and
    `rbtrend_vertex_buffer_get_size`, in bytes, as the storage buffer's.
    The buffer holds no layout, no stride and no names: the copied layout's
    names pointed at the caller's strings (a use after free for a caller
    that freed them), and that is gone with it.
    `rbtrend_vertex_buffer_get_layout` and
    `rbtrend_vertex_buffer_get_crgpu_buffer_view` are removed; nothing in
    the workspace called either.
  - `RBTREND_VERTEX_LAYOUT` (in `rbtrendpipeline.h`, was `rbtrend_buffer_layout`
    in `rbtrendbuffers.h`): `count`, `elements` and a required `stride`.
    `RBTREND_VERTEX_ELEMENT` is `name`, `type` and `offset`; its size follows
    from the type (the `size` field, which nothing kept in step with the
    type, is gone). `RBTREND_VERTEX_ELEMENT_TYPE` (was
    `RBTREND_BUFFER_ELEMENT_TYPE`) gains `UINT` to `UINT4`, appended, so a
    `uint` vertex input can be fed. `rbtrend_vertex_element_type_get_size`
    and `rbtrend_vertex_layout_get_element_index` are the renamed calls;
    `rbtrend_buffer_layout_get_total_size` is gone with the computed stride.
    An explicit stride lets a vertex have padding after its last element
    and a pipeline read part of a larger vertex.
  - A graphics pipeline checks each layout (a stride not 0 and a multiple of
    4, each element's type valid, offset a multiple of 4, and ending inside
    the stride) and copies it, names included, so
    `rbtrend_graphics_pipeline_get_desc` hands back the pipeline's copy, not
    the caller's array.
  - The command list binds vertex buffers at each draw, at the bound
    pipeline's binding strides: a Carrottus GPU vertex view carries its stride,
    as D3D12's does, so each buffer keeps a view for each stride a pipeline
    has read it at (usually one), and a span's view is made at the first
    draw that reads it. A slot is bound again only where its view changes;
    the views survive a pipeline change, as vertex bindings do on every
    API, and are taken as unbound at each pass.
  - The stride check is replaced by a bounds check: a draw's vertices, an
    instanced draw's instances and the one instance any other draw reads
    must end inside the buffer or span, at the binding's stride and its
    furthest element's end. `SET_TRANSIENT_VERTEX_BUFFER` has no `_stride`;
    the span is read at the pipeline's.
  - A vertex pool's desc takes `vertex_stride` in place of a layout.
    Rend's lit layouts have their strides (`sizeof(RBTREND_LIT_VERTEX)`,
    `sizeof(RBTREND_LIT_INSTANCE)`).
  - Callers in the same change: the callers in the workspace (vertex
    layouts, vertex buffers sized in bytes, a solid pool's stride) and
    `../rabbittustests`.
  - `../rabbittustests`' `test_vertex_layouts`: six refused pipelines (stride
    0, stride 10, an offset of 2, an element past the stride, an unknown
    type, a `uint` input fed by an `INT` element); the pipeline's copy of a
    layout whose caller overwrote its array and names; a padded,
    interleaved vertex (32 bytes, elements to 24) read for its positions
    alone; a `uint` input that draws for 7 and not for 3; two draws past
    the end of a 6-vertex buffer refused. Every test passed on Metal (Apple
    M5 Pro), Debug under Metal API and shader validation with warnings
    fatal and Release under `leaks` (none); callers' tests
    pass over it. Every test passed on Windows on 2026-10-18, D3D12 under its debug layer and Vulkan under
    the validation layer with synchronization validation, on the Iris Xe
    and the RTX A2000 (reported; step 2 and 3 of "Suggested order" have
    what that run found). Not built on Linux. That includes D3D12, whose
    vertex view's stride is what the per-stride views are for.
    Two callers ran over it on Windows the same day
    (reported).
- **Shader reflection, written 2026-10-11 and compiled on all three
  backends** (item 18; closes item 1). Type-checked with GCC 11 as the
  entry above before that. Part of it has run without a device: see the
  last bullet.
  - `rbtrendshader.h` (new): the shader calls moved out of
    `rbtrendpipeline.h`, and `RBTREND_SHADER_REFLECTION`. A shader reflects
    its SPIR-V at creation through SPIRV-Cross's core
    (`rbtrendreflect.cpp`, `rbtrendreflect_internal.hpp`): each descriptor
    binding (set, binding, type, count, a block's size and members, an
    image's view shape, scalar type and depth compare, storage read-only),
    the push constant block, a vertex shader's inputs, a fragment shader's
    color output count and a compute or mesh shader's workgroup size, each
    marked used if the stage's one entry point reaches it. Block members
    are listed flat with their real offsets, array and matrix strides and
    majority, structs and arrays of structs as `lights[2].color`. Creation
    fails for SPIR-V that can't be read, has no entry point for the stage,
    or uses a texel buffer, input attachment, acceleration structure,
    unsized descriptor array, multisampled or 1D image.
  - Pipelines are checked against their shaders: entry point, every used
    binding declared by the layout at its set with its type, count and
    stage, push constant bytes covered by a range of the stage, and every
    used vertex input fed at each location by an element of its kind of
    number. `_vertex_inputs_by_name` (new, last field of the graphics
    pipeline desc) gives elements their locations from the inputs' names
    (item 11's positional locations).
  - `rbtrend_binding_layout_create_from_shaders`, and `_name` (new, last
    field) on `RBTREND_BINDING_DESC`; `rbtrend_binding_layout_find`,
    `_get_binding`, `_get_binding_count`, `_get_block`,
    `_get_uniform_layout`. A layout made from shaders keeps its bindings'
    names and blocks.
  - A command list checks the texture views of each set it binds for a
    pipeline against the shape the shader declares, and a stencil read as
    other than unsigned or a depth as other than float.
  - `RBTREND_UNIFORM_TYPE_MAT2` and `_MAT3`; `RBTREND_UNIFORM_LAYOUT::size`
    (new, last field). `rbtrend_uniform_buffer_create` sizes from it, and
    std140's matrix columns of 16 are kept in the footprint and by
    `rbtrend_uniform_buffer_update_by_name` (item 1).
  - The material (item 19) takes its names and blocks from the layout: its
    desc is a layout and the names of uniform blocks it shouldn't own. A
    member is set by its path, or its binding's name and its path.
  - CMake: SPIRV-Cross's core is linked PRIVATE, the target Carrottus already
    made on Metal and D3D12, built here from `../.deps/SPIRV-Cross` on
    Vulkan. Configured and built that way on all three as of 2026-10-12.
  - **What has run**, on Linux in a scratch build, not through the repo's
    CMake: `tests/rbtrendreflect_tests.cpp` (new) against SPIR-V from
    glslang 16.6.0, under AddressSanitizer and UBSan: passes. And a
    scratch harness (not in the repo) that linked all of `src/render`
    against a Carrottus GPU of stubs that create nothing and return success:
    layouts from shaders, every pipeline check passing and failing as
    meant, a material set by name and completed, a render pass, a draw
    list, a compile refused for a wrong view shape and passing once fixed,
    submits writing the material's frame copies (since replaced by the
    material arena, below) with the "read in this
    turn" rule, three frames, and teardown through the release queue,
    with no sanitizer error. The same harness made a caller's four
    pipelines (geometry, line, solid, overlay) the way
    that caller does, from its GLSL compiled with glslang,
    and none was refused by the new checks. That exercises Rend's logic
    and none of Carrottus GPU, so nothing is known about any backend from it.
  - Push constants are checked per member the stage uses (SPIR-V's active
    ranges), so two stages can share one block and each take a range for
    its own part.
- **Layouts of the same bindings are one layout, written 2026-10-11 and
  compiled on all three backends** (was step 1 of "Next, from the review").
  Run in the stub harness, not on a device.
  - `rbtrend_binding_layout_core` (`rbtrend_internal.hpp`): a layout's
    bindings without names (sorted by number), its Carrottus GPU layout and its
    descriptor pools. Both create calls look for the instance's core of
    the same bindings (number, type, count, stages) and make one only if
    there is none, so two layouts of the same bindings share one Carrottus GPU
    layout. Each handle keeps its own names and blocks.
  - A core is held by each layout handle and each binding set made through
    one, and freed with its last holder. Every holder lets go on the thread
    using the instance (a release function, or a failed create), so the
    count takes no lock and destroy calls stay safe from any thread. A set
    holds the core itself, so it no longer reads the handle it was made
    through after creation.
  - The compile, a dispatch, and a render or compute pass compare a set's
    core with the pipeline's layout's (`rbtrend_binding_set_fits`) where
    they compared the handles. A material made for one layout is drawn with
    a pipeline that declares another of the same bindings.
  - `rbtrendpipeline.h`, "Layouts of the same bindings": what counts (names
    don't, stages do), and that a layout made from shaders is made from
    every shader that reads the set and given to each pipeline that binds
    it.
  - Run in the stub harness: two layouts made separately from the same
    shaders, and one by hand with other names, share a core; another stage
    mask doesn't; a material made for one layout compiled into a pass
    whose pipeline declares the other; handles destroyed before the sets
    made through them; every core gone once the last layout and set are.
- **The material arena, written 2026-10-11 and compiled on all three
  backends** (was step 1 of "Next, from the review" after layouts were
  shared). Run in the stub harness, not on a device.
  - A material's owned uniform block is a fixed range of the instance's
    material arena (`rbtrend_material_arena`, `rbtrend_internal.hpp`) for
    the material's life, in place of a uniform buffer set: 64 KiB uniform
    buffers in the device's memory (`rbtrend_uniform_buffer_create_device`,
    new, internal), ranges 256-byte aligned and handed out with the pools'
    range list. A block larger than 64 KiB gets a buffer of its own, which
    goes with it. One copy of a block on the GPU, not one per frame, and
    one descriptor set for a material that holds no caller's buffer set.
  - `rbtrend_material_set_uniform` and `_bytes` mark the block dirty. At the
    submit of a list that binds the material, each dirty block is written
    to a span of the frame's transient memory and a copy from it into the
    block's range is recorded in the list's upload list, the arena
    buffer moved COMMON to COPY_DEST before and COPY_DEST to the uniform
    state after, once per buffer. The flag is cleared once the submission
    has gone in. A value set is seen by every later submission, of one
    frame or the next; the "read in this turn" rule and
    `rbtrend_instance_t::frame_count` are gone.
  - A material that owns a block needs an instance with transient memory
    and is refused without it. `rbtrend_material_destroy` puts the whole
    material on the release queue, so its ranges are handed out again only
    once the GPU has finished with what was submitted before it. The
    arena's buffers go with the instance.
  - `rbtrend_uniform_buffer_t`'s destructor unmaps, and destroys a view or
    buffer, only where there is one.
  - Run in the stub harness: the first submission copies the zeroed and
    set block (bytes read back from the transient chunk), an unchanged
    material copies nothing, a change between two submissions of one frame
    reaches the second, the next frame copies nothing, no frame-locked
    sets; three materials share an arena buffer at different ranges, a
    destroyed one's range comes back after the queue and is handed out
    again; a material is refused on an instance without transient memory
    unless its block is external; a block over 64 KiB gets a buffer of its
    own, gone after the material. Never on a GPU: the barrier that orders a
    copy after earlier submissions' reads is Carrottus GPU's, unverified on every
    backend, as for the staged buffers (item 13).
- **No tests in this repo of anything that needs a device.** There is no
  `tests/rbtrend_tests.*`: the repo's tests are headless with no GPU. The three
  parts of Rend that run without a device each have a test, all
  registered with ctest behind `RABBITTUS_BUILD_TESTS`:
  `tests/rbtrendranges_tests.cpp` (the pools' range list),
  `tests/rbtrendreflect_tests.cpp` (reflection; passed in a scratch build
  on Linux) and `tests/rbtrendfeatures_tests.c` (the optional feature set,
  item 39). The tests that need a device are
  `../rabbittustests` (`rbtrend_gpu_tests`), run on Metal (macOS) and on D3D12
  and Vulkan (Windows).
- **Reviewed by reading on 2026-10-13; nothing compiled or run for it.**
  Every file of `src/render` except `rbtrendpools.cpp`, `rbtrendreflect.cpp`
  and most of `rbtrendpipeline.cpp` was read against this file, and this
  file was brought into line with the code. What the review found wrong
  is items 37 and 38 and the additions to items 8, 12, 13 and 20; what it
  found already fixed was taken out of items 8, 12, 14 and 20. No code was
  changed.
- **Multiple render targets, depth bias, debug labels and GPU timers,
  written 2026-10-15; compiled on macOS over Metal through a caller's macOS host, which ran (reported 2026-10-16)** (items 25, 26, 29):
  - The host's run used one color target, no bias, no labels and no
    timer, so it shows the arrays and the new pipeline checks build and
    pass for what that host draws, and no more.
  - **Run on Windows on 2026-10-16 by `../rabbittustests`, all passed:** D3D12
    on an NVIDIA RTX A2000 Laptop (memory of its own) and an Intel Iris Xe
    (unified), debug layer clean; Vulkan on both, validation clean.
    `test_two_targets`, `test_pipeline_must_match_pass`,
    `test_depth_bias`, `test_labels` and `test_gpu_timer` (a time above 0
    and below a second) each passed there. The Vulkan run found that a
    descriptor's image layout and a binding's shader stages reached Vulkan
    untranslated, which turned every sampled texture black; fixed in
    Carrottus GPU.
  - **Run on macOS on 2026-10-16 by `../rabbittustests`, all passed:** Apple
    M5 Pro, Debug under Metal API and shader validation with warnings made
    fatal, and Release; no leak at exit. The timer read 0.08-0.11 ms for
    its pass (above 0 and below a second; not compared with a known cost).
    The run found five things in Carrottus GPU's Metal backend, all fixed there:
    samplers made without `supportArgumentBuffers` though every set is an
    argument buffer; every set bound to the vertex and fragment stages
    whatever its bindings' stages (a validation error for a stage whose
    function has no argument there); a depth-stencil state and the
    rasterizer state set again when the encoder already had them, which
    two pipelines with equal depth state do; the timestamp counter set
    used after its autorelease pool had freed it, which crashed creating a
    timer; and the array of devices from `crgpu_enumerate_devices` never
    freed (Vulkan too; it is the instance's now, as on D3D12). Labels have
    been compiled and submitted, not looked at in RenderDoc, PIX or Xcode.
  - Carrottus GPU: `crgpu_graphics_pipeline_desc` has `color_attachment_formats`,
    `blend_attachments` and `color_attachment_count` in place of the one
    format and blend, and `depth_bias_constant`, `_slope`, `_clamp`. The
    bundle inheritance desc has the formats too. New device properties
    `depth_bias_clamp`, `independent_blend` and `pass_timestamps`, the
    first two Vulkan features enabled where reported. The checks every
    backend makes on these are written once (`src/gpu/crgpushared.c`).
    Vulkan's blend state now translates the caller's factors and ops: it
    had been fixed to straight alpha whatever was asked.
  - Carrottus GPU debug labels (`crgpu_command_list_begin_debug_label`, `_end_`):
    `VK_EXT_debug_utils`, enabled on the instance wherever offered; PIX's
    ANSI events on D3D12; debug groups on Metal, on the render encoder in a
    pass and the command buffer outside one.
  - Carrottus GPU timestamp sets (`crgpu_timestamp_set_create`, reset, resolve,
    `crgpu_timestamp_set_get` in nanoseconds), written by a pass at its start
    and end through `crgpu_rendering_begin_desc::timestamp_set`. A query
    pool on Vulkan, a query heap and readback buffer on D3D12, a counter
    sample buffer sampled at stage boundaries on Metal. A new `query` file
    in each backend.
  - Rend: the pipeline desc's `color_attachment_formats` and
    `color_attachment_count` (0 for depth only), `_blends`, and the depth
    bias; a render pass takes up to `RBTREND_MAX_COLOR_TARGETS` targets,
    each checked against the pipeline's format at its index. The compile
    now refuses a pipeline set inside a pass whose targets it doesn't
    match (count, formats, depth), which nothing checked before.
  - Rend labels (`RBTREND_COMMAND_BEGIN_LABEL`, `END_LABEL`), checked at
    compile for nesting and for not crossing a pass's boundary, and closed
    in the Carrottus GPU list on a failed compile so it stays balanced.
  - `rbtrend_gpu_timer` (`rbtrendtimer.h`): slots a pass is timed into,
    through `BEGIN_RENDERING`'s `_timer` and `_timer_slot` or a render
    pass's, kept once for each frame in flight and read a frame late.
  - Every caller in the workspace follows: crgpusamples,
    `../rabbittustests` and the rest set the formats as arrays.
  - What to check first: a pipeline with two targets on each backend; a
    shadow pass's bias; labels in RenderDoc, PIX and Xcode; a timer's
    reading against a known cost, on Metal above all.
- **Command memory for each frame and the upload batch, written 2026-10-15;
  compiled on macOS over Metal through a caller's macOS host, which ran (reported 2026-10-16)** (items 37 and 6):
  - On Metal there is no allocator memory to leak, so the run shows the
    per-frame lists and the batch work there, not that the D3D12 leak is
    gone. Whether the host updated or read back a texture in the run is not
    recorded.
  - An instance has a Carrottus GPU command allocator for each frame index, and
    the Carrottus GPU lists recorded from it. `rbtrend_instance_next_frame` resets
    the index's allocator once its wait has succeeded. A list is handed out
    for one recording and not again until then, so none is reopened while
    it may still run (Vulkan can't reset a pending command buffer).
  - An `rbtrend_command_list` owns no Carrottus GPU list. A compile takes one from
    the frame, or records over its own if it was compiled in this frame and
    not submitted since; the upload list of its staged buffers is taken at
    submit. Every list is refused at submit in any frame but the one it was
    compiled in, counted by a frame number, which replaces the two checks by
    frame index (per-frame sets, transient spans). Those compared indices
    only, so a list `frames_in_flight` frames old passed them.
  - Texture updates and readbacks and framebuffer clears are recorded into
    one batch for the frame, from the frame's lists, which the next
    submission carries ahead of its own lists. `rbtrend_instance_next_frame`
    and `rbtrend_instance_wait_idle` submit it if nothing has; a readback
    submits it and waits for that serial. No more allocator, list and
    submission per call. What is destroyed while a batch waits is retired a
    serial later, for the submission that will carry it.
  - A texture update is staged in the frame's transient memory where the
    instance has some and the copy's offset rule is a power of two, and in a
    buffer of its own (as before) otherwise. The offset rule is new in
    Carrottus GPU: `crgpu_image_get_buffer_copy_offset_alignment` (512 on D3D12;
    the texel block size, and of 4 on Vulkan; the block size on Metal).
    `crgpu.h` also now says when an allocator may be reset and that a list
    is not reopened while its last recording may run.
  - The texture update and readback calls refuse a format with no whole
    number of bytes to a texel, where they divided by zero (item 38).
  - **The batch has an allocator of its own, fixed 2026-10-16; run on
    D3D12 (NVIDIA RTX A2000 Laptop) by `rbtrend_gpu_tests`, all passed.**
    The batch was taken from the frame's one allocator and stays open
    while other lists of that allocator are opened, which D3D12 refuses
    (`COMMAND_ALLOCATOR_CONTENTION`): the compile's list stayed closed
    and `BeginRenderPass` on it faulted in the driver. Each frame index
    now has two pools, `recordings` and `batches`, each an allocator and
    its lists, both reset by `rbtrend_instance_next_frame`. `crgpu.h`
    now says that one list of an allocator is open at a time. Run on Vulkan
    on Windows the same day (both devices, validation clean), and on Metal
    by `rbtrend_gpu_tests` (validation clean).
  - **The batch and the per-frame memory, run on Windows on 2026-10-16 by `../rabbittustests`, all passed:** D3D12
    on an NVIDIA RTX A2000 Laptop (memory of its own) and an Intel Iris Xe
    (unified), debug layer clean; Vulkan on both, validation clean.
    `test_updates_batched` passed there: two updates in order ahead of a
    pass, staged in transient memory (so D3D12's 512-byte offset rule
    inside a chunk held), and an update carried across a frame turn by
    `rbtrend_instance_next_frame`. What the tests don't show is the leak of
    item 37 gone: they turn the frame twice. That needs a host running for
    minutes on D3D12 with memory watched.
  - **Run on macOS on 2026-10-16 by `../rabbittustests`, all passed**, with
    `test_updates_batched`, under Metal validation (see the entry above).
  - **Memory flat on D3D12, reported 2026-10-16:** a caller's host on
    Windows over D3D12 ran idle for 20 minutes with no growth in memory,
    the check item 37 was waiting on. Whether its viewport drew frames
    throughout while idle, and on which device, isn't recorded.
    **It showed less than it seemed** (found 2026-10-17): that caller never
    called `rbtrend_instance_next_frame` - it waits for the GPU after every
    render and kept one frame in flight, so it saw no need - and an idle
    window doesn't render. So the per-frame allocators were never reset
    under a host, and every compile took a new Carrottus GPU list. It came to
    light when a viewport glow's bloom took a new pooled chain every frame
    and ran out of memory. That caller now turns the frame after each render
    call's wait. Memory over minutes of rendering, not idling, is still to
    be watched.
- **Bloom and post-processing, written 2026-10-16; run on Metal the same
  day**
  (item 36): `RBTREND_COMMAND_BLOOM` (source, target, threshold, knee,
  radius, intensity, levels), outside a pass: four fragment passes in
  `shaders/rbtrend_bloom_*.frag` over a float chain taken from the
  instance's new render target pool (`rbtrendrendertargets.cpp`), added
  onto the target. No bloom handle and no header of its own: the command's
  doc in `rbtrendcommands.h` describes bloom, `rbtrendcontext.h` ("Textures a
  command uses in between") the pool, and
  `rbtrend_instance_get_fullscreen_vertex_shader` (`rbtrendshader.h`) hands the
  built-in vertex shader to a caller's own pass over a texture.
  Built-in pipelines per stage and target format (`rbtrendbuiltin.cpp`), a
  shared `rbtrend_record_fullscreen_pass` (mip generation records through
  it too), each view keeping the set that reads it, and a command list
  keeping what its compile took from the pool until it is submitted.
  `../rabbittustests`' `test_bloom` **passed on Metal on 2026-10-16** (Apple
  M5 Pro) on its first build: Debug under Metal API and shader validation
  with warnings fatal, and Release; no leak at exit. The white square read
  255 at its centre, 34 ten texels out on both sides (white, alpha the
  same), 2 in the corner. On 2026-10-17 it passed on D3D12 and Vulkan on
  Windows, on an Intel Iris Xe and an NVIDIA RTX A2000 (reported).
- **Mip generation in a command list, written 2026-10-16; run on Metal
  the same day** (item 31): `RBTREND_COMMAND_GENERATE_MIPS`, outside a pass,
  checked as it is sent (texture, instance, level, filter, usage, format)
  and again at compile (a resize since), with the base level's contents
  taken from the list's own layout tracking. Its views and sets, and those
  of `rbtrend_texture2d_generate_mips`, are now kept by the texture
  (`rbtrend_mip_chain` in `rbtrendimages_internal.hpp`) instead of made and
  retired each call; both forms record through one
  `rbtrend_record_mip_pass`. `../rabbittustests`' `test_generate_mips_command`
  renders mip 0, generates, and samples mip 3 in one list, twice, and
  checks three compile failures. **Passed on Metal on 2026-10-16** (Apple
  M5 Pro) on its first build: Debug under Metal API and shader validation
  with warnings fatal, and Release; no leak at exit. **Passed on D3D12 and
  Vulkan the same evening** (Windows 11, Intel Iris Xe and NVIDIA RTX
  A2000, Debug; D3D12 under cdb with the debug layer clean, Vulkan with
  the validation layer clean), on the first run, with every other check.
- **Rend ships shaders, and generates mips with them; written
  2026-10-16; run on Metal the same day** (items 22, 31, 7):
  - GLSL in `shaders/` is compiled to SPIR-V by glslc at build time and
    embedded in the library as C arrays (`-mfmt=c`), on every platform, as
    callers in the workspace and `../rabbittustests` already do: nothing to deploy or load
    at run time. glslc is now needed on Apple too. The block that compiled
    `apricot_default.vert`/`.frag` to `.spv` files on Windows and Linux
    only, and linked Vulkan for it, is gone with those shaders (item 7);
    nothing loaded them.
  - `src/render/rbtrendbuiltin.cpp` (`rbtrendbuiltin_internal.hpp`): the
    instance's built-in objects, made through Rend's own create calls
    when first needed and destroyed through the release queue with the
    instance. So far a full-target vertex shader (`rbtrend_fullscreen.vert`,
    three vertices, no buffer), a downsample shader
    (`rbtrend_downsample.frag`, a separate texture and sampler since
    Carrottus GPU's Metal backend has no combined one), its layout, a clamped
    sampler for each filter, and a pipeline for each format used.
  - `rbtrend_texture2d_generate_mips` (item 31's immediate form): every
    level below a base level of every layer, a pass each, sampling the
    level above through a one-mip, one-layer 2D array view (the shader
    declares a `texture2DArray`: D3D12 has no 2D view of a layer past the
    first, and a 2D shader texture given an array view is undefined
    there); recorded into the upload batch,
    each level left in the layout it was found in. Refused, printed: a
    texture without render target and shader resource usage, a format
    outside the eleven every backend renders to and filters, a base level
    some layer has never had written.
  - `../rabbittustests`' `test_generate_mips` checks a two-layer 8x8 chain
    level by level and the refusals. **Passed on Metal on 2026-10-16** (Apple
    M5 Pro, unified memory), on its first run: Debug under Metal API and
    shader validation with warnings fatal, and Release; no leak at exit.
    **Passed on D3D12 and Vulkan on 2026-10-16** (Windows 11, Intel Iris Xe
    and NVIDIA RTX A2000; D3D12 under cdb with the debug layer clean, Vulkan
    with the validation layer clean), after a fix in Carrottus GPU's D3D12 backend:
    every 2D view there saw layer 0, so layer 1 was rendered into and
    sampled from layer 0. The array-view source came after, and has run on
    D3D12 and Vulkan, and then on Metal (M5 Pro, 2026-10-16: Debug under
    validation with warnings fatal, and Release; no leak at exit).
- **Struct bases, 2026-10-20.** Rend's 24 descs and lit scene settings
  start with an `RBT_STRUCT_BASE` (`rbtcore.h`; `rbtrendcontext.h`, "Struct
  bases"), and every other public struct is marked frozen.
  `RBTREND_COMMAND`'s `_params` is padded to 120 bytes (the command to 128).
  `rbtrend_graphics_pipeline_create()` and `rbtrend_compute_pipeline_create()`
  take their desc by pointer, as every other create does. **Run on Metal on
  2026-10-20** (M5 Pro): `../rabbittustests` passes, Debug under Metal API and
  shader validation with warnings fatal, with a new `test_struct_bases`
  (a zeroed base, another struct's type and a chained struct refused; the
  getter's base). Run on Windows, D3D12 and Vulkan, on 2026-10-22 (item
  39's run).
- **Section A of the autonomous work plan, written 2026-10-22; run on
  Metal, then on Windows over D3D12 and Vulkan on 2026-10-23** (Apple M5
  Pro: `../rabbittustests` Debug under Metal API and shader validation with
  warnings fatal, both devices, every test passing; Release under `leaks`,
  none; Rabbittus' headless tests, carrottus's, and two callers' tests
  (17 and 33) over it, all passing, with no Rend message in any of them).
  Not built on Linux. The Windows run, against Rabbittus `8c79f76` and
  carrottus `1810c98` with the fixes below: Windows 11, Debug,
  `../rabbittustests` on the Intel Iris Xe (`--device 0`, adapter 0x1dff,
  first device 0x7f) and the NVIDIA RTX A2000 (`--device 1`, 0x1fbf, first
  device 0x23f), each on its two devices; D3D12 with its debug layer
  printing (no debugger), Vulkan with `VK_LAYER_KHRONOS_validation`, plain
  and under synchronization validation. Every check passed in all six
  runs, with no debug layer or validation message and no Rend line but
  the refusals each test expects. Synchronization validation found no
  hazard on either device, so the run-at-a-time barriers of item 17 hold
  there (`test_generate_mips`, `_command`, `test_bloom`,
  `test_storage_ordering`, `test_sampled_mip_views`, all in it). None of
  the reflection checks refused a pipeline or view an older test passes.
  The first Vulkan run found two validation errors, each fixed (carrottus,
  Rabbittus), every check still passing: a mesh shader module was
  created on a device without mesh shading (the SPIR-V needs
  `VK_EXT_mesh_shader` even as a module), and the layered pass's vertex
  shader, SPIR-V 1.0, declares `ShaderViewportIndexLayerEXT`, which needs
  `VK_EXT_shader_viewport_index_layer` beside `shaderOutputLayer` (item
  39). The same two callers' tests pass over it on both backends, no
  Rend message, and a caller's host ran on both (item 38).
  Each point is closed in its item below:
  - **Stale lists, for everything a recording names** (item 32): a command
    list records, in a hash set (`used_handles`; an open-addressing table
    since 2026-10-23, see the next bullet of this section), every pipeline and
    binding set it binds and what each set holds (views, samplers, uniform
    buffers and a buffer set's copies), every pass target view, the views
    of the built-in effects' commands, and each texture's layout tracking.
    The destroy calls of views, samplers, both pipelines, binding sets and
    uniform buffers now call `rbtrend_instance_forget`, as textures, stores,
    materials and timers did; so do `rbtrend_binding_set_update` (a list
    compiled before would bind descriptor sets now on the release queue)
    and `rbtrend_swap_chain_destroy`. `rbtrend_command_list_submit` refuses
    such a list. It is how Vulkan treats a command buffer whose resources
    are destroyed (it becomes invalid), turned into a refused submit;
    Unreal and Hazel keep the resources alive by reference counts held by
    the command list, which Rend's C API has no place for.
    `test_stale_handles` covers six cases.
  - **The swap chain** (item 38): a generation count that each new Carrottus GPU
    swap chain moves on; a list records it and submit refuses one compiled
    before a resize. `rbtrend_swap_chain_acquire` returns false, printing,
    when Carrottus GPU acquires no image (it returns `UINT32_MAX`, which the TODO
    had said it couldn't). `PRESENT_TEXTURE` refuses a source of another
    format than the back buffers (the copy converts nothing on D3D12 and
    Metal) or smaller than them (the rest of what is shown was undefined);
    a larger one is cropped, as before. `rbtrend_swap_chain_get_back_buffer_size`
    (new) reads the size Carrottus GPU made them at, which the surface may decide.
    No test makes a swap chain: the tests have no window. A caller's host
    ran over it on Windows on 2026-10-23 (D3D12 and Vulkan, RTX A2000; see
    item 38): on Vulkan the size check found that the caller sized its
    image to the size it asked for, not the size the surface gave.
  - **Item 8**: `RBTREND_DESTRUCT__T` is one statement; `rbtrend_command_list_send`
    catches `std::bad_alloc` (the list then fails its compile, as for a
    malformed command); `rbtrend_framebuffer_create` and `_resize` reserve
    their vectors under a `catch` and check their arguments one to a line.
  - **Reflection checks** (item 18): a pipeline is refused if its fragment
    shader writes a color location it has no target for, leaves one of its
    targets unwritten (Carrottus GPU has no write mask, so the target would get
    undefined values on every API), or writes the other kind of number
    (float to an integer target, or the other way round). Binding a set
    refuses a color view whose format reads as another kind of number than
    the shader's, and a storage image view of another format than the one
    the shader declares (`layout(rgba8)`, through Vulkan's table of
    compatible formats). `rbtrend_binding_layout_create_from_shaders`
    refuses two declarations of one block that differ other than in length.
    What the frozen `RBTREND_SHADER_REFLECTION` has no field for is kept in
    Rend's own store (`storage_formats`, `output_types`).
  - **The thread rule checked** (item 12): in a Debug build each call that
    meets in instance state opens with `RBTREND_ENTER`, which prints when a
    second thread enters while one is inside. Not tested: two threads
    can't be made to overlap inside a call on purpose. It never fired in
    any of the runs above, nor on Windows on 2026-10-23, in a caller's
    host that renders off its UI thread (D3D12 and Vulkan).
  - **Layouts** (item 17): a list finds a subresource's entry through a
    hash map (an open-addressing table since 2026-10-23), and a range in more than one layout is moved a run of mips
    of a layer at a time.
  - **Odd mip sizes** (item 31): a linear generation is an exact box over
    each target texel's footprint, so an odd side's every texel weighs the
    same; `test_generate_mips_odd` reads 51 where the old filter read 0.
  - **Carrottus GPU's Metal backend** (carrottus): render push constants are sent at
    the next draw (item 11), and combined image samplers work (item 9),
    their sampler at an argument index of its own.
    `test_push_constants_unused` and `test_combined_image_samplers` fail
    against the old code (the first stops the run under validation, the
    second's shader didn't compile) and pass now. Both pass on D3D12 and
    Vulkan (2026-10-23), combined samplers on both GPUs and both devices.
  - **First runs**: `test_layered_pass` (a layered pass, the layer chosen in
    the vertex shader), `test_mesh_shading` (a mesh shader pipeline drawn),
    `test_stencil_sampling` (a stencil cleared and read as an unsigned
    integer), `test_cube_array_sampling` (a cube array sampled), and in
    `test_feature_checks` a mesh shader and a swap chain refused without
    their features. Each passed on Metal on its first run, and on D3D12 and
    Vulkan on 2026-10-23: the layered pass and the cube array on the first
    device of both GPUs (the Iris Xe's D3D12 adapter has
    `VPAndRTArrayIndexFromAnyShaderFeedingRasterizer`), skipped on the
    featureless ones; the mesh shader on the RTX A2000's first device only
    (`quad.mesh`, SPIR-V 1.5, through SPIRV-Cross to HLSL on D3D12), the
    Iris Xe having no mesh shading on either API; the stencil read on every
    device. `test_feature_checks` refused a mesh *shader*, not a pipeline,
    from 2026-10-23 (item 39).
- **A command list's lookups and copies, data-oriented, 2026-10-23; run
  on Metal only** (Rabbittus `2e21b05`, with the workspace's
  conventions and plain-data checks). Nothing in the API changed.
  - `used_handles` and `layout_index` are `rbt_hash_index`es
    (`src/rbthashindex.hpp`, new): a 64-bit hash to a 32-bit value, one
    array of slots probed in order from where the hash lands (Fibonacci
    hashing, so aligned pointers spread), no node and no erase. `find()`
    takes a test of the key, so two keys of one hash are told apart.
    `clear()` keeps the slots, so a list compiled every frame stops
    allocating once its tables have grown. `used_handles` hashes by address
    (the address is the key); `layout_index` by
    `rbtrend_subresource_hash()` (image, mip, layer), each match checked
    against the `layout_use` it names. They were a `std::unordered_set` and
    a `std::unordered_map` (2026-10-22), which allocated a node per entry
    every compile. `tests/rbthashindex_tests.cpp` checks the table against
    a `std::map` with keys crowded onto 64 hashes.
  - What a recorded command points at (each BEGIN_LABEL's text, and the
    arrays of BEGIN_RENDERING, SET_VERTEX_BUFFERS, SET_VIEWPORTS,
    SET_SCISSOR_RECTS and PUSH_CONSTANTS) is copied into one byte arena,
    `side_bytes`, each copy at an offset aligned to
    `alignof(std::max_align_t)`; `side_refs` keeps (command, offset), and a
    compile points each such command at its copy before it reads any,
    since growing the arena moves it. Before, a `std::deque<std::string>`
    and five vectors of vectors, an allocation per command. A reset clears
    both and frees nothing. Recorded commands still never point at caller
    memory.
  - The element types of the list's per-command arrays have
    `RBT_PLAIN_DATA` asserts (trivially copyable, standard layout;
    `src/rbtplaindata.hpp`, new), and `RBTREND_COMMAND` a size budget of
    two cache lines (`RBT_HOT_RECORD`), as do a draw list's draw and
    batch and the pools' range tables. `rbtrend_pool_block` is the one that
    fails (its range list holds a `std::vector`), in the workspace's
    `check.baseline`.
  - Run on Metal (Apple M5 Pro, Debug): Rabbittus' 17 tests, plain and
    under `-asan`; `../rabbittustests` under Metal API and shader validation
    with warnings fatal; two callers' tests over it. **Not built on Windows
    or Linux**: the command list's recording path is every backend's, so it
    wants a D3D12 and a Vulkan run.
- **Section B of the autonomous work plan, from 2026-10-24; run on Metal,
  and on Windows over D3D12 and Vulkan on 2026-10-29** (items 24, 36 and 40
  say how) (Apple M5 Pro: `../rabbittustests` Debug under Metal API and shader
  validation with warnings fatal, both devices, and Release under `leaks`;
  Rabbittus' headless tests; two callers' tests over it; the
  workspace's `./check.sh`). Item 40 is recorded in its item. Each point is
  closed in its item below:
  - **A draw list's batched order is the same on every run** (item 21):
    keyed by the first draw naming each material, set of vertex buffers and
    index buffer, never by an address. `test_draw_list_order` is new, and
    fails against the old order. No API change.
  - **Post-processing** (item 36): BLOOM's `_mask` and `_lens_dirt`;
    `rbtrend_instance_get_render_target_pool()` and
    `rbtrend_instance_trim_render_targets()`; `RBTREND_COMMAND_BLUR` (36), a
    separable Gaussian; `RBTREND_COMMAND_OUTLINE` (37), a jump flood. Two
    built-in shaders (`rbtrend_blur.frag`, `rbtrend_outline.frag`), and the
    blur and outline take from the render target pool. Four tests, each
    against the formula worked out in C, each shown to fail against a
    broken version. Additive API. The new commands record passes and
    barriers through the same functions BLOOM uses, so they want a D3D12
    and a Vulkan run, synchronization validation included.
  - **Lighting** (item 24): cascade blending; a sun disc
    (`RBTREND_LIT_SUN::disc_angle`) and Hosek and Wilkie's sky
    (`RBTREND_LIT_SKY_MODEL`, their dataset vendored and cooked on the CPU);
    a histogram for exposure (`low_share`, `high_share`, a compute
    dispatch); per-map uv transforms; GTAO proper with bent normals
    (`rbtrend_lit_scene_set_bent_normals`); point-light shadows
    (`rbtrend_lit_scene_set_point_shadows`, six faces a light); depth
    bounds for the tiles (`RBTREND_COMMAND_LIT_CULL_LIGHTS`). Fields appended
    to two unreleased based structs (`RBTREND_LIT_SUN`,
    `RBTREND_EXPOSURE_DESC`) and to the lit material block; the rest is new
    calls and commands. Each test against the formula worked out in C, and
    shown to fail against a broken or old version. A Carrottus GPU Metal fix
    (redundant compute set binds). New compute dispatches and barriers
    (the histogram, the bounded cull) want a D3D12 and a Vulkan run under
    synchronization validation; the point shadows' 2D array lookups too.

## Wrong things

### 1. Uniform layouts are declared by hand

Closed 2026-10-11 by item 18, compiled and not run on a device: `rbtrend_binding_layout_get_uniform_layout`
gives a uniform buffer's layout as the shader has it, `mat2` and `mat3` have
types, structs and arrays of structs are listed member by member, and every
offset is the SPIR-V's. A layout written by hand is still the caller's to
get right, and `rbtrend_buffer_write_uniform` assumes std140 (a
uniform block in `layout(std430)` or `scalar` through an extension would be
written at the wrong strides; the material, which reads the strides, is
not).

### 2. A host-visible storage buffer on D3D12

Closed 2026-10-10, compiled on all three backends. A storage buffer written directly is
`CRGPU_MEMORY_KIND_DEVICE_MAPPABLE`, which a device has when it is
created with `CRGPU_FEATURE_DEVICE_MAPPABLE_MEMORY` (since 2026-10-21;
before, `CRGPU_DEVICE_PROPERTIES::device_mappable_memory`, item 39) and which D3D12 backs
with a custom heap, not an upload heap. A device without it takes the staged
path for storage buffers alone. The D3D12 heap has never run: an integrated
GPU under D3D12 is the case to test first.

### 3. Nothing fills a texture's mips but the caller

A texture created with `mip_levels` 0 gets every level, and each starts
undefined. `rbtrend_texture*_update` writes any one level, so a caller with its
own mip data can fill the chain. A 2D texture without it can now have the
chain made from its first level (`rbtrend_texture2d_generate_mips`, item 31;
run on all three backends). A 3D texture still can't: such a texture wants
`mip_levels` 1, or a sampler whose `max_lod` is 0.

### 6. Immediate operations: what is left

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

Texture updates and readbacks and framebuffer clears are recorded into one
batch for the frame, carried by the next submission, and an update is
staged in the frame's transient memory (see "State of the code",
2026-10-15). Left over:

- **A readback blocks**, and has to: the caller reads the result. One that
  hands the result over later (a pending read the caller polls) is the only
  way it wouldn't, and nothing asks for it. It also submits the whole batch
  early, with whatever updates the frame has recorded so far.
- **An update can't be placed between two commands of a command list.** The
  batch goes ahead of the next submission's lists, so an update between two
  passes of one list (write, draw, write again, draw) is two lists. A
  command that records a copy from a transient span inside the list is the
  way, and nothing asks for it yet.
- **The staging falls back to a buffer of its own** on an instance with no
  transient memory, past `transient_memory_limit`, and for a format whose
  copy offset rule is not a power of two (a 12-byte texel on Vulkan). Each
  of those still makes and retires a buffer per call.
- **The batch's layouts are written when it is recorded.** If the
  submission that carries it fails, and `rbtrend_instance_next_frame` then
  drops it, the tracked layouts say what the dropped barriers would have
  done. A failed submission is a lost device in practice.

### 7. The default shaders are unused

Closed 2026-10-16 with item 22: `shaders/apricot_default.vert` and `.frag`,
loaded by nothing, are gone, and with them the block of `CMakeLists.txt`
that compiled them on Windows and Linux only.

### 8. Older-generation habits still to convert

Per `CLAUDE.md`, only when the function is already being changed: joined
argument checks (`if (!instance || !desc)`, 14 of them in `src/render` as of
2026-10-13), failure reported by `printf` with no result code, and
`std::vector` growth inside C entry points with nothing catching
`std::bad_alloc`. The two `malloc`s this item named as unchecked (the layout
copy in `rbtrend_vertex_buffer_create`, the file buffer in
`rbtrend_shader_read_from_file_spirv`) are checked now.

Where the vector growth mattered most is done: `rbtrend_command_list_compile`
catches it over its whole walk since 2026-10-18, and closes the Carrottus GPU
list on the way out; `rbtrend_command_list_send` catches it since 2026-10-22 and
fails the list's next compile; `rbtrend_framebuffer_create` and `_resize`
reserve their vectors under a `catch` since the same day, so nothing after
grows them.

In the command list: vertex buffers, the index buffer, viewports and
scissor rects sent before a pass are passed to Carrottus GPU where they stand,
outside the pass, and whether they are still set inside it is the backend's.
(`SET_INDEX_BUFFER` with a NULL buffer is refused at compile; this item
used to say it was dereferenced.)

Smaller things the review of 2026-10-13 found, none of them a wrong result
today:

- **Done: `RBTREND_DESTRUCT__T` is one statement** (`do { } while (0)`,
  2026-10-22). It was two, and under an `if` with no braces only the
  destructor was guarded.
- **Done: two comments named a function of a caller**
  (`create_offscreen_color_target`, in `rbtrendframes.cpp` and
  `rbtrendimages_internal.hpp`), which `CLAUDE.md` rules out. They mean "the
  caller's array may be a local". Both say that now (2026-10-22).
- **Done 2026-10-28 (card A1, with the user): three breaking renames,**
  in one change with every caller in the workspace.
  `RBTREND_FRAMEBUFFER_TEXTURE_SPEC::use_for_gui`, which did exactly what
  `shader_resource` does and named what a caller does with the texture, is
  gone; the misspelt `RBTREND_TEXTURE_VIEW_TYPE_RENDER_TAGET` is
  `..._RENDER_TARGET`; and `rbtrend_destroy_texture_view`, the one destroy
  call with the verb first, is `rbtrend_texture_view_destroy`. Run on macOS
  (Metal).

### 37. The instance's command allocator is never reset

Closed 2026-10-15; run in a caller's macOS host on Metal, and by `../rabbittustests` on Metal, and on D3D12 and Vulkan on Windows (2026-10-16) (see "State of the code"). Each frame index
has its own allocator, reset by `rbtrend_instance_next_frame` after its wait,
and a recording takes a list of the current frame's; a list compiled in
one frame is refused at submit in a later one. Carrottus GPU needed no new call
for it: a list stays tied to the allocator it was created on, which is what
Vulkan requires, and Rend keeps lists for each frame. The D3D12 leak it
fixes was found by reading and never measured before the fix; after it,
a caller's host ran idle on D3D12 for 20 minutes with memory flat
(reported 2026-10-16) - which showed little, as that caller didn't turn the
frame until 2026-10-17 (see "State of the code"). Left: memory watched
over minutes of a host rendering.

### 38. Unchecked inputs and stale lists, from the review of 2026-10-13

Numbered out of order, as item 31. Each was found by reading and none has
been reproduced.

- **Fixed 2026-10-15, not compiled: `rbtrend_texture2d_update` and
  `_get_data` (and the 3D pair) divided by the format's bytes per pixel**,
  0 for a block-compressed format and for one with no known pixel size.
  They now refuse such a format. A compressed texture still has no upload
  path: rows of texels are not what it is made of.
  **Card A48, decided 2026-11-02 with you: a.** A block-aware update:
  rows of blocks, a region in whole blocks, a block-compressed format's
  bytes per block from its format; and ASTC formats in Carrottus GPU, a Carrottus
  card of its own written when this is built. The block formats are
  already chosen, so nothing waits on a measurement. Built when a caller
  first uploads a compressed texture. Not built.
- **Done: `rbtrend_shader_read_from_file_spirv` leaked the file's bytes**
  when the read failed. The Carrottus Files rework (2026-10-22) moved the read
  into `rbtrend_read_spirv_file`, which frees them on every failure.
- **Done: a list that presents could outlive the swap chain it was
  compiled for** (2026-10-22; see "State of the code", "Section A"). The
  swap chain counts its Carrottus GPU swap chains, and submit refuses a list
  compiled against an earlier one, even at the same back buffer index;
  destroying the swap chain refuses it as well.
- **Done: `PRESENT_TEXTURE` checks size and format** (2026-10-22). A
  source of another format than the back buffers, or smaller than them, is
  refused at compile; a larger one is cropped to its top-left region. Run
  on Windows on 2026-10-23 in a caller's host (Debug, RTX A2000), its
  window resized 46 times, 40 of them 50 ms apart, then left rendering a
  minute: silent on D3D12, and on Vulkan refused 11 frames of the fast
  resizes, each source the previous size and the back buffer the window's
  current one (Vulkan's swap chain takes the surface's `currentExtent`,
  which had moved on). The frame after each was drawn. The caller now
  sizes its image from `rbtrend_swap_chain_get_back_buffer_size` after
  each create and resize; rerun, 92 resizes on each backend, no refusal.
- **Done: `rbtrend_swap_chain_acquire` refuses a failed acquire**
  (2026-10-22). Carrottus GPU's acquire does say it failed, with `UINT32_MAX`;
  Rend kept that as the index and returned true.
- **Done, in Debug builds: `rbtrend_texture2d_resize` with views alive**
  (2026-10-18, step 2 of "Suggested order"). A Debug build counts a
  texture's views and refuses a resize, 2D or 3D, while one the caller
  made is alive. A Release build still checks nothing, as the header
  says.

## Real gaps: what a renderer at Hazel's level has

Hazel (`../.refs/HazelRenderer2025/Hazel/Hazel/src/Hazel/Renderer`) is the bar;
see `CLAUDE.md`, "Reference: Hazel", for how its idioms are written here. Each
item names where Hazel does it. Unless it says otherwise, Carrottus GPU already
exposes what the item needs.

### 9. Binding: what is left

Binding sets cover every descriptor type and set slot (see "State of the
code"). Left over:

- **Changing a set costs a set.** `rbtrend_binding_set_update` (see "State
  of the code") builds new descriptor sets from every entry, changed or not,
  and retires the old ones. Writing only the changed slots needs Carrottus GPU to
  copy descriptors from one set to another, which it doesn't offer.
- **Done: a compiled list is told its set was updated** (2026-10-22):
  `rbtrend_binding_set_update` marks every compiled list that binds the set
  stale, and submit refuses it.
- **A layout has no debug name**: `rbtrend_binding_layout_create` takes no desc.
  Two layouts of the same bindings share one Carrottus GPU layout (see "State of
  the code"), so a debug name would be a handle's, not the Carrottus GPU
  object's.
- A layout is checked against the SPIR-V when a pipeline is made from it
  (item 18); a binding set against its layout, and its views' shapes
  against the shader when it is bound.
- **Done: a combined image sampler slot works on Carrottus GPU's Metal backend**
  (2026-10-22, carrottus). Its sampler half has an argument of its own, at
  `CRGPU_METAL_COMBINED_SAMPLER_INDEX_BASE` past the binding, in the
  shader's argument buffer and the layout's encoder; before, only the
  texture was written and a shader with one didn't compile.
  `test_combined_image_samplers` passed on Metal, and on D3D12 and Vulkan
  (which implemented it already) on 2026-10-23, both GPUs, debug and
  validation layers silent.

### 10. Push constants: what is left

A pipeline declares ranges and `RBTREND_COMMAND_PUSH_CONSTANTS` writes them (see
"State of the code"). The bytes are written for the pipeline bound then and
are not tracked: after another `SET_SHADER_PIPELINE` or `SET_COMPUTE_PIPELINE`
the caller sends them again. A graphics pipeline's are written inside a pass
and a compute pipeline's outside one.

### 11. Vertex bindings: what is left

A pipeline declares up to `RBTREND_MAX_VERTEX_BINDINGS` bindings, each per vertex
or per instance (see "State of the code"). Left over:

- **No matrix element type.** A per-instance `mat4` is four `VEC4` elements,
  at four locations.
- **A per-instance binding steps every instance.** Carrottus GPU's binding has no
  step rate, so one element for every N instances can't be asked for.
- **Locations are positional by default**: element N across the bindings
  is location N. `_vertex_inputs_by_name` (item 18) gives them from the
  shader's input names instead, and a shader that skips a location can be
  fed that way.
- **A vertex element with fewer components than its input is allowed, on
  purpose** (decided 2026-10-22). Every API fills the missing components
  with (0, 0, 0, 1) and ignores extra ones, so a `VEC2` element feeding a
  `vec4` input is defined; item 18 named the check, and it would refuse what
  is correct.
- **Only 32-bit element types.** No normalized 8- or 16-bit formats (a
  color in four bytes, a packed normal), no half floats: vertices are two to
  four times larger than they need be. Appended to
  `RBTREND_VERTEX_ELEMENT_TYPE` when they come, with the pipeline's format
  map, scalar-type check and the multiple-of-4 rule (a 16-bit format reads
  from 2-byte offsets on every API) changed with them.
- **What a draw reads is checked only where the compile can see it**
  (2026-10-18): a draw's vertices, an instanced draw's instances, and the
  one instance a draw that isn't instanced reads. An indexed draw's
  vertices and an indirect draw's anything are read where the indices or
  arguments say.
- **Nothing checks how the bytes were packed.** A vertex buffer is bytes and
  the pipeline says what they are, so a buffer packed at one stride and
  read at another draws garbage inside its bounds, as on every API. A draw
  list's `_instance_stride` is the stride it packs instances at, and is not
  compared with the pipeline's binding (the transient command had a
  `_stride` that was, until 2026-10-18).
- **Done: a compile that fails inside a pass after a PUSH_CONSTANTS**
  (2026-10-22, carrottus). Carrottus GPU's Metal backend sends render push
  constants at the next draw now, so an encoder never ends with bytes no
  draw used, which Metal's validation with warnings fatal stopped for.
  `test_push_constants_unused`; it passes on D3D12 and Vulkan too
  (2026-10-23), where nothing had needed changing.

### 12. Frames in flight: what is left

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

A buffer of per-frame copies (`RBTREND_BUFFER_DESC::per_frame`, since
2026-10-30; buffer sets of uniform and storage buffers before) gives each
frame in flight its own copy, so the frame being written doesn't share a
buffer with one the GPU is reading. Left over:

- **Pacing counts only what goes through Rend**, on one queue. Every
  submission is made by `rbtrend_instance_submit`: command lists, and the
  immediate operations, of which only a readback then waits for its own
  serial (item 6). Each signals
  the instance's fence to the next serial, and `rbtrend_instance_next_frame`
  waits for the last serial of the frame that used the index before. Work a
  caller submits to Carrottus GPU itself is outside it. An instance submits on one
  queue, the queue of its first submission, and a submission on another is
  refused (written 2026-10-10): the serials are an order of
  completion on a single queue only. A second queue, for copies or compute,
  would need a fence and serials of its own.
- **The swap chain still waits for its queue to go idle** before a resize or
  a destroy. That is every submission on the queue, the caller's included,
  which is what replacing back buffers needs; `rbtrend_instance_wait_idle`
  would cover Rend's alone.
- **The wait has no time limit and no way to report failure**:
  `rbtrend_instance_next_frame` returns the index either way and prints.
- **Done: a command list compiled against an object and submitted after
  its destroy is refused** (2026-10-22, item 32), for every kind of handle
  a recording names. The release queue still waits for submissions, not
  compiles; the refusal is what keeps such a list from running.
- **Threads: the contract is written, and nothing enforces or widens it.**
  `rbtrendcontext.h` ("Threads") says an instance and everything of it is
  used by one thread at a time, why, and the three things several threads
  may do at once; each other header says what is particular to it. A
  destroy call is safe from any thread (see "State of the code"). Wanted,
  in this order, once what is written has been built and run:
  - Done: a Debug-build check (2026-10-22, `RBTREND_ENTER`). The rule lets
    an instance move from thread to thread between calls, so the check is
    of two threads inside it at once, not of one thread remembered: each
    call that meets in instance state enters, and one that finds another
    thread inside prints, as Vulkan's validation layer does for an object
    used by two threads. Not tested by forcing an overlap. It never fired
    on Windows on 2026-10-23 either: `../rabbittustests`, a caller's tests
    and a caller's host that renders off its UI thread, over D3D12 and
    Vulkan.
  - Command memory for each thread, so different lists can be compiled
    from different threads as they can already be recorded. Each frame
    index has an allocator of its own now (item 37); recording in parallel
    needs one for each thread and frame index, with the frame's lists kept
    apart the same way. Not before something records in parallel.
  - What two instances on one device may do at once is Carrottus GPU's to say,
    and `crgpu.h` says nothing about threads beyond one command list.
- **A destroy call releases nothing itself.** It queues, and the queue is
  drained by a submission, `rbtrend_instance_next_frame` or
  `rbtrend_instance_wait_idle`. So with the GPU idle and nothing more
  submitted, what is destroyed stays allocated until one of those or
  `rbtrend_instance_destroy`. That is the price of a destroy being safe from
  any thread. A caller that wants the memory back at once calls
  `rbtrend_instance_wait_idle` after the destroy: with the GPU already idle
  it returns immediately and releases everything waiting, and it only blocks
  while work is still in flight. No separate non-blocking call is needed;
  the one case it would add, draining while work is in flight without
  waiting, is what the next submission does anyway.
- **With no memory to queue an entry, the object is leaked** and a line
  printed. There is no thread on which a destroy call could safely release
  it instead.
- **A handle destroyed after its instance touches freed memory.** Before, it
  only outlived its device, which was already wrong.
- **Released objects are held a little longer.** A resize every frame keeps
  the last few images alive until the frames using them finish.
- **Some destroy calls don't go through the queue**, each for its own
  reason. The swap chain waits for its queue, as above. A framebuffer, a
  pass and a draw list are freed at once: what they own on the GPU (a
  framebuffer's views and textures) is retired, and the handle itself holds
  nothing the GPU reads. A buffer, per-frame copies and all, goes through
  the queue as one handle (since 2026-10-30; a buffer set was freed at once
  before, its buffers retired).
- **A buffer of one copy is one allocation** that must not be written while
  a submitted frame reads it. Any buffer can be made of per-frame copies,
  vertex and index buffers too (since 2026-10-30), which a caller that
  rewrites geometry every frame uses.
- **A copy is not carried forward.** A copy holds what was written to it
  `frames_in_flight` frames ago. A caller that writes part of a buffer of
  per-frame copies each frame reads stale bytes in the rest. Hazel's sets
  behave the same; copies that carried forward what wasn't rewritten would
  hide the cost.
- **A list that binds a per-frame set is compiled every frame.** It reads the
  frame's copies through that frame's descriptor set, fixed at compile. A set
  bound through a dynamic offset would lift this; Carrottus GPU has none.
- **Descriptor sets multiply**: a binding set with a buffer of per-frame
  copies in it takes `frames_in_flight` sets from the layout's pool, 32 to
  a pool.

Hazel: `UniformBufferSet`, `StorageBufferSet`, `Renderer::SubmitResourceFree`,
`RendererConfig::FramesInFlight`.

### 13. Uploads: what is left

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

Vertex, index and storage buffers follow the device's memory (see "State of
the code"). Left over:

- **Staged writes don't use the transient arena yet.** Its chunks are made
  as copy sources for this, and it is the per-frame half of the shared
  upload path: a write whose copy is submitted in the same frame could take
  a span and record an exact copy. What it can't hold is a write whose copy
  is submitted in a later frame (a buffer written and not drawn yet), since
  the frame's spans are taken back at the next turn. That needs spans that
  are counted, or the staging buffer kept for those.
- **A buffer written once gives its staging back; one written many times
  doesn't** (written 2026-10-18, see "State of the code"; the staged path
  has not run). An `RBTREND_BUFFER_WRITES_ONCE` buffer, and every pool
  block, stages each write in a buffer of its own and retires it after
  the submission that copies it. An `RBTREND_BUFFER_WRITES_MANY` buffer
  keeps a staging buffer of its own size for life, so it costs its size
  twice on a device with its own memory. That is the right price for one
  rewritten often; one rewritten rarely would be cheaper staged a write at
  a time, as `ONCE` is, and nothing chooses that.
- **Each `ONCE` write and each pool range makes a Carrottus GPU buffer of its
  own.** Many small ranges allocated together (a scene's meshes loaded at
  once) are as many buffer creations. A shared staging buffer, written
  through by many small writes and retired whole once the last copy from
  it has run, would make them one. Not built.
- **A write must not be made while submitted work that uses the buffer is
  unfinished.** On a unified device the write lands under the GPU; on a staged
  one the copy that hasn't run yet reads the new bytes. The same rule either
  way. Buffer sets lift it for uniform and storage buffers (item 12); the
  release queue doesn't, since nothing is destroyed.
- **Every staged write is its own copy command.** A `MANY` buffer's
  ranges merge only where they overlap or touch, so a thousand scattered
  small writes are a thousand copies in the upload list. Nothing merges
  ranges whose gap is small, which would copy a little more for fewer
  commands; that threshold would be a choice nothing has measured.
- **A pool is never compacted and frees nothing by itself.** Ranges don't
  move. A pool's table of allocations only grows: an entry is used again,
  never removed, so a pool that once held a million ranges keeps a million
  entries (24 bytes each) until it is destroyed.
- **Every submission flushes all of the frame's transient memory handed
  out so far**, from the start of each chunk. Kept, decided 2026-10-22: the
  flush is free on Metal, on D3D12 and on Vulkan's host-coherent memory
  (Carrottus GPU returns at once), which is every device this has run on; and a
  span may be taken before one submission and written after it, so
  flushing only what was taken since the last would miss it. A narrower
  flush needs a contract that says when a span is written, as Unreal's
  dynamic buffers have with `Commit()`; nothing has measured a cost.
- **Uniform buffers stay host-visible on every device**, on purpose: they are
  small and rewritten every frame, and a staged copy per buffer per frame
  costs more than the bus read it saves. A large uniform buffer written once
  would be better staged; nothing asks for it.
- **A texture upload is staged in transient memory** and recorded into the
  frame's batch (item 6); where it falls back to a buffer of its own is
  item 6's.
- **A compiled list holds a pointer to each buffer it uses**, as it does
  to each texture. Destroying the buffer and then submitting the list is
  refused since 2026-10-18 (item 32).
- **Done: a batch never submitted is closed before what it uses goes**
  (2026-10-31). Destroying an instance with updates or clears recorded
  and not submitted released every object first and ended the batch's
  list after, which Vulkan's validation reports
  (`VUID-vkEndCommandBuffer-commandBuffer-00059`: the list was invalidated
  by the objects destroyed under it). `../rabbittustests`' `--only
  test_resize_with_views` showed it (its mips are generated and never
  submitted); a whole run submits the batch in the next test. Older than
  the Slang change. The batch's list is ended first now, and still dropped
  unsubmitted.
- **How Carrottus GPU's memory kinds land**, for whoever touches this next. The
  contract of each is in `crgpu.h` (`CRGPU_MEMORY_KIND`); this is only
  what is underneath:

| Kind | Vulkan | D3D12 | Metal |
|---|---|---|---|
| `DEVICE` | A device-local memory type | Default heap | Private storage |
| `UPLOAD` | A host-visible memory type; coherent or not, the flush covers both | Upload heap | Shared storage, write-combined |
| `READBACK` | A host-visible memory type, cached if there is one | Readback heap | Shared storage |
| `DEVICE_MAPPABLE` | A memory type both device-local and host-visible, on an integrated device | Custom heap, pool L0, on a UMA adapter | Shared storage, on a device with unified memory |

### 14. Compute: what is left

Compute is in (see "State of the code"): compute shaders,
`rbtrend_compute_pipeline`, and `SET_COMPUTE_PIPELINE` and `DISPATCH` in the
same command list as rendering, outside a pass. Left over:

- **Storage barriers follow what the shaders write** (item 32,
  2026-10-18): a pipeline records the storage bindings its shaders write
  (`storage_writes`, from reflection's `!RBTREND_SHADER_RESOURCE::read_only`),
  and the command list uses it to leave out the ordering barrier between
  two reads (see "State of the code").
- **A buffer a shader writes has no way back to the CPU**: there is no
  storage buffer readback.
- **A compute pipeline lasts until the next pass or present**, and is set
  again after either. That is the rule that holds on every backend; on some
  it would have survived.
- **Dispatches are direct only.** No indirect dispatch.
- **The compute pass object is `rbtrend_compute_pass`** (item 20), and a
  material is bound for a dispatch with `SET_MATERIAL`, since
  `SET_BINDING_SET` serves both. What Hazel's `ComputePass` has beyond it
  is inputs set by name, which is item 20's. Its resource states are
  tracked by NVRHI; Rend's own tracking does that job.
- **Carrottus GPU on Vulkan orders storage writes for the compute, vertex and
  fragment stages only.** A storage resource written from another stage is
  not covered by the barrier Rend records.

Hazel: `PipelineCompute`, `ComputePass`, `Renderer::BeginComputePass`,
`Renderer::DispatchCompute`, and their use in `SceneRenderer.cpp`.

### 15. Indirect draws and mesh shaders: what is left

Both are in (see "State of the code"). What was listed here and isn't Rend's
to finish has moved to the Carrottus GPU group: bundles and bindless (item 33), and
indirect dispatch, a draw count from the GPU and task shaders (item 34). What
is left is how the built part behaves:

- **Buffer state is tracked only for a storage buffer that is also an
  argument buffer.** A command list moves such a buffer between the state a
  storage slot uses it in and the one an indirect draw reads it in, and
  submit puts it in the first ahead of the list. Every other buffer is used
  in one state for life and isn't tracked. A buffer with a third use would
  need its states added to this, not a second mechanism.
- **A buffer is in one state for a whole pass**, so it can't be the argument
  buffer of a draw and in a storage slot the same pass's draws read. Reading
  the arguments from the shader as well (to index per-draw data by them)
  needs a read-only state for both at once, which Rend doesn't use.
- **An argument buffer adds a barrier to every submit** that uses it, to give
  the list a known state to start from, even when nothing was written.
- **An index buffer, like a vertex buffer, is tracked across passes.** An
  indexed draw is refused only if no `SET_INDEX_BUFFER` came before it
  anywhere in the list, not in its own pass; whether a buffer set before a
  pass is still set inside it is the backend's (item 8).
- **Mesh shading is a per-device fact.** A mesh shader pipeline fails to
  create on a device without it, with a message; Rend has no query of its
  own, since the caller holds the device and can ask Carrottus GPU.

### 16. Views of a range, cubes: what is left

A view covers a range of its texture's mips and layers and has a shape, and a
2D texture can be cube-capable (see "State of the code"). Left over:

- **A pass renders to one mip.** Through 2D views it renders to one layer;
  through 2D array views to all their layers at once, the layer chosen in
  the vertex or mesh shader (see "State of the code"). What that leaves:
  - Rendering to several mips in one pass isn't offered, and no graphics
    API offers it.
  - A device created without `CRGPU_FEATURE_LAYERED_RENDERING` can't
    make a 2D array view to render to, and Rend has no fallback: the
    caller renders a layer at a time through 2D views there. Rend has no
    query of its own for it; the caller asks for the feature when it
    creates the device (item 39).
  - The layer can't be chosen in a geometry shader, which Rend doesn't
    take and Metal doesn't have.
  - Multiview (one draw repeated to each layer by the device, a view index
    in the shader) isn't offered. Carrottus GPU has nothing for it.
  - The framebuffer's attachments and its clear calls are still mip 0 of
    layer 0; an `rbtrend_framebuffer` is never layered.
- **A storage image slot is one mip, and stays so.** It is not a gap: an
  image store takes no mip level in SPIR-V, and a D3D12 unordered access
  view is one mip slice. Writing several mips in one dispatch is an array
  of slots (`image2D mips[N]`, `RBTREND_BINDING_DESC::_count`) with a one-mip
  view in each, which the binding set and the per-mip layouts already take;
  `rbtrendimages.h` says so. No dispatch has run with one.
- **1D textures and 1D views: not planned until something needs them.** A
  lookup table, ramp or gradient is a 2D texture one texel high, declared
  `sampler2D` and sampled at `vec2(u, 0.5)`: the same memory and the same
  result. A 1D type would have to be the narrowest of the three APIs', and
  Metal refuses a 1D texture as a render target, with mips, and in a
  compressed format. Hazel has none either: its `TextureType` is 2D and
  cube, and its lookup tables are `Texture2D`. Carrottus GPU has the 1D image and
  view types; Rend doesn't use them.
- **A view's format is its texture's.** No view in another format of the
  same size. The one exception is built: a view of a depth and stencil
  texture says which aspect it covers, so the stencil can be sampled (see
  "State of the code").
- **The stencil can't be sampled in a pass that tests against the same
  mip's depth.** A mip of a layer has one layout, not one for each aspect
  (item 17), so sampling either aspect moves both out of the attachment
  layout. Read-only depth with the stencil sampled needs a layout for each
  aspect and Carrottus GPU's read-only depth and stencil layouts.
- **Not taken from Hazel, on purpose.** Its `Image2D` makes and keeps
  per-layer and per-mip views itself (`CreatePerLayerImageViews`,
  `GetMipImageView`); here the caller makes the views it needs and owns
  them. Its `TextureCube` is a class of its own and an asset type; here a
  cube is a texture and a view.
- **Carrottus GPU's Rust bindings don't have `cube_capable`**; their image
  descriptor was already out of step with the header.

Hazel: `ImageViewSpecification`, `ImageSpecification` (`Mips`, `Layers`),
`TextureCube`.

### 17. Layout tracking: what is left

A layout is tracked for each mip of each layer (see "State of the code"), so
one mip can be sampled while another is rendered to or written by a compute
shader. Left over:

- **A list keeps an entry for every mip of every layer it touches**, found
  through a hash map since 2026-10-22 (before, a linear search): a
  `std::unordered_map` until 2026-10-23, an open-addressing table
  (`rbt_hash_index`) since, which allocates nothing once grown.
- **A range whose subresources differ is moved a run at a time**
  (2026-10-22): each run of mips of a layer that share a layout is one
  barrier. Runs across layers aren't merged. On Metal the barriers do
  nothing; Vulkan's synchronization validation found no hazard on
  2026-10-23 (`../rabbittustests`, Iris Xe and RTX A2000, both devices of
  each), with D3D12's debug layer silent too.
- **A resize to a different mip count replaces the layout storage**, so a
  list compiled before the resize holds pointers into what was freed. With
  the same mip count the storage is kept. The header says to recompile after
  a resize; nothing checks (item 32).
- **The stencil of a depth and stencil texture follows its depth.** There is
  one layout for a mip of a layer, not one for each aspect.

### 32. Texture layouts: what the command list gets wrong or can't do

Numbered out of order, as item 31. Item 17 is the one limit that needs a
redesign; these are the rest, all in `rbtrendcommands.cpp`. A pass takes only
the sets its draws read (see "State of the code").

- **A depth texture can't be tested against and sampled in one pass.** A
  sampled texture goes to `SHADER_READ_ONLY` and a target to its attachment
  layout, and a texture in both is refused. Reading depth in the shader while
  it is the pass's depth target, with depth writes off, needs a read-only
  depth layout, which `CRGPU_IMAGE_LAYOUT` doesn't have, and a rule for when
  it is used.
  **Decided 2026-10-28 with the user (card A11):** Carrottus GPU gains the
  read-only depth layouts (`../carrottus/CRGPU_DEVICE_TODO.md`, "Later"),
  and Rend uses one by itself when a pass's pipelines all have depth
  writes off and a draw samples the mip the pass tests against; callers
  never see layouts. For soft particles, decals, fog and glass reading the
  depth behind them. Tests: a pass testing against and sampling one depth
  texture, read back, on every backend. Not started.
- **Storage barriers follow what the shaders write** (written 2026-10-18,
  see "State of the code"). A storage image or buffer no pipeline has
  written since its last barrier in the list, used by one that only reads
  it, gets no barrier. Left: the first use in each list always gets one,
  since the list doesn't know what the submission before it did; a texture
  could carry "written since the last barrier" from one submission to the
  next, as it carries its layout, and the submit check would compare it.
- **Done: a list's other handles are checked at submit** (2026-10-22).
  Views, samplers, pipelines, binding sets and what they hold, uniform
  buffers and swap chains are refused as textures, buffers, materials and
  timers were (2026-10-18), through `used_handles` (see "State of the
  code", "Section A"). `test_stale_handles` passed on Metal, and on D3D12
  and Vulkan on 2026-10-23.
- **A list leaves a sampled texture in `SHADER_READ_ONLY` and a storage image
  in `GENERAL`.** Nothing puts it back. Right for most uses; written down
  because the immediate operations do put a texture back.
- **Done: a placeholder a binding holds is moved too** (2026-10-31).
  `TONE_MAP` without `_auto_exposure` binds the lit placeholder for its
  exposure and moved it nowhere, so on Vulkan the descriptor named
  `SHADER_READ_ONLY` while the image was still `UNDEFINED`
  (`VUID-vkCmdDraw-None-09600`). A whole run hid it: an earlier test's lit
  draw had moved the placeholder; `../rabbittustests`' `--only test_tone_map`
  showed it, on both Vulkan devices. Older than the Slang change (the
  shader always declared the binding). Now moved to `SHADER_READ_ONLY` like
  any other read; run alone and in the whole suite, validation silent.

### 18. Shader reflection: what is left

Written (see "State of the code"). Read from Slang's SPIR-V since 2026-10-31
(item 22): what Slang writes differently is read back as the source
declares it where it can be, so RBTREND_SHADER_REFLECTION is what it was
for the GLSL (SPIR-V whose generator word is Slang's, tool 40):

- **Block type names**: Slang names a uniform block's type `<name>_std140`
  and a push constant block's `<name>_std430`; the suffix is dropped
  (`RbtrendLitScene`).
- **Arrays in a block**: Slang wraps each in a struct of its own,
  `_Array_std140_<element><count> { data[count] }`; it is listed as the
  member itself (`sun_shadow_view_projection`, 4 mat4s;
  `lights[1].radiance_type`), its offset, size and strides the wrapped
  array's.
- **Matrices**: Slang's SPIR-V holds each transposed and decorates a
  column-major one RowMajor; `row_major` is read inverted, so it is false
  for a column-major matrix as before, its matrix stride between the
  source's columns.
- **Structured and byte address buffers**: their block is the buffer
  type's own struct (`RWStructuredBuffer`, member `__member0`, an array
  with no size); `block_name` is the variable's name (`RbtrendLitTiles`, as
  the GLSL's block was). The members are what Slang wrote (`__member0`,
  `block_size` 0, `runtime_array_stride` 4, where the GLSL's `bins[64]` had
  256): not recoverable, and nothing in Rend reads a storage block's
  members.
- **Vertex inputs** that are fields of a struct parameter are named
  `<parameter>.<field>`; the field's own name is reported (`position`,
  `transform0`, where the GLSL's were `in_position`...): not the old names,
  which no Slang source has. The batcher's elements are named `r0` to `r7`
  for it.
- **`readonly` and `writeonly`** are lost on a Slang RW type not declared
  `readonly` (no NonWritable or NonReadable), so `read_only` is now also
  true for a storage buffer or image the module never writes through (a
  scan of its stores, atomics, image writes and calls). What reads it: a
  pipeline's storage writes, which order a later use after its draws; a
  binding wrongly counted written would only add a barrier.
- **Depth comparison**: Slang's image types leave depth unknown (Depth 2,
  which SPIRV-Cross reads as false); `image_depth_compare` is also true
  for an image the code samples with a comparison (a Dref sample or
  gather). Nothing in Rend reads it.
- **What a shader doesn't use is absent** from Slang's SPIR-V (bindings,
  inputs, push constants), where glslang declared it unused. Layouts made
  from shaders already took used bindings only; the one change was the
  samplers wrapped only round `texelFetch` in GLSL (item 22), whose sets
  no longer write them. A caller that writes every binding of a layout
  made from shaders writes only what some shader of it uses
  (`../rabbittustests`' `make_sampled_pipeline` checks its layout for the
  sampler).

Tested by `rbtrendreflect_tests`' `test_slang_shaders` on the built-in
shaders' SPIR-V, and by every `../rabbittustests` read-back on D3D12 and
Vulkan (2026-10-31). Hazel:
`Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h` (`Reflect`),
`Shader.h` (`ShaderBuffer`, `ShaderUniform`), `DescriptorSetManager::Validate`.
Left over:

- **One entry point per stage is assumed** to tell used from declared.
  SPIR-V with several entry points for a stage counts everything as used,
  so a pipeline over it needs layouts for all of them.
- **Reflection is redone for every shader created**, with no cache. Fine
  for a handful of shaders; a cache belongs with item 22.
- **Specialization constants**: a workgroup size or array size set by one
  reads as 0, and a descriptor array sized by one is refused.
- **Done: formats are checked** (2026-10-22): a storage image's declared
  format against its view's, a sampled or storage view's kind of number
  against the shader's, and each color output against its target (written,
  and of its kind of number). A vertex element's component count is
  deliberately not (item 11). On D3D12 and Vulkan (2026-10-23) these
  refused nothing an older test passes.
- **The bindings of one set must agree across stages** in type; different
  array sizes take the largest, and two declarations of one block must be
  one the start of the other (2026-10-22: refused otherwise), the longer
  kept.
- **SPIRV-Cross on D3D12 builds is Carrottus's git tag**, a different
  revision from the vendored one; the core API used here is old and
  stable, but the two aren't pinned together.

### 19. Material: what is left

`rbtrend_material` is written (see "State of the code"). Hazel: `Material.h`.
Left over:

- **Names come from the layout**, which for owned blocks has to be made
  from shaders (item 18). A hand-written layout gives names to textures
  and buffers the caller sets, and no owned block.
- **A changed resource rebuilds the whole binding set**, at the next
  compile (item 9's first point). Fine for a texture swapped now and then;
  a material whose textures change every frame wants item 33's bindless.
- **A list compiled before a resource changed and submitted after it**
  binds what the material held at its compile, until another compile
  builds the material's set again; then it is refused at submit (item 9's
  update, 2026-10-22).
- **One material is one set slot.** A shader that reads per-material data
  from two sets needs two materials.
- **No owned block in an array binding**, and a buffer the caller gives
  is always bound whole: no `_offset` or `_range` through the material.
- **No push constants in a material.** Hazel's materials can be push
  constant blocks; here a material is a binding set.
- **A copy is a copy, not an instance of a parent** (2026-10-18). Unreal's
  material instances read every parameter they don't override from their
  parent, live; `rbtrend_material_copy` takes the values once. A caller
  that wants a parent's later changes to reach its children propagates
  them itself: inheritance needs change tracking and ties the parent's
  life to its children's, an asset system's concern, not a renderer's.
  Nor is there a call that copies values into a material that exists
  (Unreal's `CopyParameterOverrides`); nothing asks for one.
- **The arena keeps one empty buffer, and no more** (2026-10-11;
  run in the stub harness only). When a material's range comes back
  and leaves its buffer empty, the buffer goes if another is empty
  already, and is kept as the spare if not. This happens in the
  material's release function, after the GPU has finished with it, so the
  buffer is destroyed there and then. Left: a new block goes to the first
  buffer with room, not the fullest, so a buffer with one long-lived
  material in it may never empty; nothing releases the spare
  (`rbtrend_vertex_pool_trim` has no counterpart for the arena); nothing
  compacts, by design, since moving a block means a copy and a rebuilt
  descriptor set. Item 35 may replace the range list underneath.
- **An owned block takes at least the device's uniform buffer offset
  alignment** (`CRGPU_ADAPTER_PROPERTIES::uniform_buffer_offset_alignment`,
  16 at the least) since 2026-10-18: 256 on D3D12 and Metal, often 64 on
  Vulkan. Before, 256 everywhere, since Carrottus GPU didn't report it.
- **A changed block is copied whole**, not the changed bytes.
- **A material of a frequently changing block** (one written every frame)
  pays a copy and a barrier on its arena buffer each submission; a block
  that changes every frame is the job of a buffer of per-frame copies, as an
  external block.

### 20. Render and compute passes: what is left

`rbtrend_render_pass` and `rbtrend_compute_pass` are written, and a render pass
renders to an `rbtrend_framebuffer` and checks it against the pipeline (see
"State of the code"). Hazel: `RenderPass.h`, `ComputePass.h`,
`Renderer::BeginRenderPass`. Left over:

- **The render area is always the whole target**, and the viewport and
  scissor cover it. A caller wanting less sends its own SET_VIEWPORTS
  after the begin, or BEGIN_RENDERING by hand.
- **Ops are fixed per pass.** A pass that clears on its first use in a
  frame and loads on its second is two passes.
- **Inputs are by slot, not by name** (Hazel's `SetInput("u_...")`).
  Reflection (item 18) is written and a layout made from shaders has its
  bindings' names; a pass doesn't use them. A material does, so a pass's
  named inputs today are a material at one of its slots.
- **A pass over a framebuffer with a borrowed texture reports the wrong
  size after a resize.** `rbtrend_framebuffer_resize` doesn't resize an
  `existing_texture`, and `rbtrend_render_pass_get_size` returns the
  framebuffer's size, not the attachment's.
- **A pipeline set inside the pass** with SET_SHADER_PIPELINE is not
  checked against the targets' formats, as BEGIN_RENDERING never was.
- **The swap chain is not a target.** Presenting is still
  `PRESENT_TEXTURE`'s copy from a texture the pass rendered to.
  **Decided 2026-10-28 with the user (card A12): it stays so.**
  `PRESENT_TEXTURE` is the one path: the frame is rendered offscreen (HDR,
  post-processing, resize, screenshots and exports from the same texture,
  the fit by rectangles) and copied into the swap chain by one full-screen
  pass, as Unreal and Hazel finish theirs. Revisit only if a measurement
  on a phone or tablet shows the copy matters; then the last
  post-processing step (tone mapping) writes into the swap chain image,
  not every pass.
- **A compute pass only sets state**: there is no begin and end, and
  nothing groups its dispatches.

### 21. Draw list: what is left

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

`rbtrend_draw_list` is written (see "State of the code"). Hazel:
`SceneRenderer` (`MeshKey`, `DrawCommand`, `m_SBSInstanceTransforms`),
`Renderer::RenderMesh`. Left over:

- **Instance bytes are a per-instance vertex binding, not a storage
  buffer** as this item first asked. A vertex binding needs no binding
  layout of the list's own, works the same on every backend, and takes its
  memory from the frame's transient arena, so it needs no per-frame copies. What
  it costs: a 4x4 matrix is four VEC4 elements at four locations (item 11),
  and a shader can't index another instance's data. A storage buffer path
  would be a second mode of the list, for a shader that wants one.
- **No culling and no GPU-driven path.** The list draws what it is given;
  culling is the caller's, and an indirect path built by a compute shader
  waits on item 34.
- **Batched order is the same on every run (done 2026-10-24; run on Metal,
  and on Windows over D3D12 and Vulkan on 2026-10-29, `test_draw_list_order`
  on every adapter, validation silent).** It sorted by handle value: draws of one material were together
  and each material bound once, but which came first followed addresses,
  which change from run to run. Now each handle a draw names (material,
  set of vertex buffers, index buffer) is keyed by the index of the first
  draw that names it, found through `rbt_hash_index` tables the list
  keeps, and the build sorts plain `sort_key`s (material, vertex buffers,
  index buffer, `_first`, `_count`, `_base_vertex`, the draw's own index)
  with `std::sort`; the last field makes the order total, so instances of
  a batch keep their order without a stable sort. Equal lists, added in
  the same order, build into the same order whatever order their objects
  were made in; the caller decides it by the order it adds. The header
  says so (`RBTREND_DRAW_LIST_ORDER_BATCHED`). No API change.
  - Compared: Hazel's draw lists are `std::map<MeshKey, ...>`, ordered by
    (mesh asset, submesh, material asset, selected), the assets by their
    `UUID`: random when an asset is made, the same every run once saved,
    and unrelated to the order anything was loaded. Unreal (as I know it,
    not checked against source) sorts a pass's mesh draw commands by a
    64-bit `FMeshDrawCommandSortKey`, which for the base pass is built
    from hashes of the shaders' pointers under its masked and background
    bits, so its order within a bucket also follows addresses; it merges
    equal commands into instanced draws by state buckets, a hash of the
    command's contents. Rend has no persistent id to sort by (Hazel's
    asset handle is an application concept) and no content hash that
    tells two materials of one layout apart, so the first appearance in
    the list is the key: it is the caller's order, it groups exactly as
    before (each material bound once, each mesh one batch), and reading it
    is one table lookup a handle.
  - `../rabbittustests`' `test_draw_list_order`: one list built twice, from
    materials made A then B and then B then A (four alive at once), the
    draws added in one order (B whole, A left, B right): red left and cyan
    right both times. Against the address order it fails, the first
    build on one device and the second on the other.
  - Left: nothing sorts front to back for early depth rejection: that
    needs a depth, or a key, the caller would hand over, which is API.
- **No pipeline per draw.** Draws that need different pipelines are in
  different lists, each replayed after its own SET_SHADER_PIPELINE.
- **A second build in one frame takes transient memory again**; the first
  span is not reused until the frame comes round.

### 22. Shaders are precompiled SPIR-V only

Half settled on 2026-10-16. Rend ships shaders of its own: GLSL in
`shaders/`, compiled to SPIR-V by glslc when the library is built and
embedded in it (see "State of the code"), and the objects made of them kept
by the instance (`rbtrendbuiltin_internal.hpp`). A caller's shaders are
still SPIR-V it hands over: item 18 has Rend reflect them, so SPIR-V is
all it needs. Written 2026-10-16 and run on all three backends by `../rabbittustests` (mip generation and bloom draw with them; D3D12 and Vulkan by 2026-10-17). Left:

- **No runtime compilation, macros, cache or reload**, for the
  caller's shaders or Rend's. Rend's shaders share `.glslh` includes
  through glslc's `-I shaders` and `#include` (`GL_GOOGLE_include_directive`)
  at build time, with no compiler at run time. Hazel:
  `VulkanShaderCompiler`, `ShaderPreprocessor`, `VulkanShaderCache`,
  `ShaderPack`.
- **Variants.** One pipeline per target format is made on demand. A
  shader that needs a variant of itself (an integer format, a different
  sample count) is a second shader, or a specialization constant Carrottus GPU
  doesn't expose.
- **Built-ins are made on first use**, so the first call that needs one
  pays for a pipeline. A call to make them ahead of time is for when that
  shows.

**Decided 2026-10-28 with the user (card A9):**

1. ~~**Rend stays SPIR-V in.**~~ **Revised the same day with the user:**
   Rend takes a **shader package**, the same shader in each API's own
   format (SPIR-V, DXIL, `.metallib`), and gives Carrottus GPU the one its
   device takes (Carrottus GPU compiles nothing any more:
   `../carrottus/CRGPU_DEVICE_TODO.md`, "Later"). Still no compiler in the
   renderer. Rend's built-in shaders are compiled to all three at build
   time (DXC on Windows, Apple's compiler on Apple platforms, each
   platform building its own), so a shipped application carries neither
   DXC nor SPIRV-Cross. Every caller changes in one change, before 1.0.
2. **Runtime compilation is a separate, optional Rabbittus module**
   (working name ApShaderc): source in, a shader package out (SPIR-V, and
   DXIL and Metal through SPIRV-Cross and the platform compilers; the same
   pipeline as the build step). The language is card A29. No file
   I/O: the caller hands it source and an include callback. A cache keyed
   by a hash of source and options, in memory the caller may save and load.
   For hot reload, an editor's material or shader graph, debug builds; a
   shipped application needn't link it. Compared: Hazel builds shaderc into
   its renderer, Godot uses runtime glslang with a disk cache, Unreal
   separate worker processes and a shared cache, bgfx an offline tool. Its
   own TODO when started. Not started.
3. **Specialization constants in Carrottus GPU** for variants without
   recompiling source (`../carrottus/CRGPU_DEVICE_TODO.md`, "Later"); Rend
   passes them on a pipeline desc. Used by item 27's sample counts, item
   31's 3D and integer paths, and items 24 and 18. Not started.

Later, noted: **pipeline caches** in Carrottus GPU (Vulkan's `VkPipelineCache`,
D3D12's pipeline library, Metal's binary archives), the bytes handed to
the caller to keep, so a second launch compiles nothing.

**Decided 2026-10-31 with the user (card A29): the source language is
Slang**, for Rend's shaders and its callers'.

1. **One compiler, three outputs.** Slang (`slangc` at build time,
   `libslang` in ApShaderc) writes SPIR-V itself, DXIL through DXC, and
   Metal source that Apple's `metal` compiler turns into a `.metallib`.
   Item 22's shader package is built from a `.slang` file per shader.
2. **A fallback route for Metal**, chosen per shader at build time: Slang
   to SPIR-V, then SPIRV-Cross to Metal source (today's route). Slang's
   own Metal output is the default; a shader whose Slang Metal output is
   wrong or doesn't compile takes the fallback, named in the build, with
   the reason beside it. As of 2026-10-31 Slang's Metal ray queries are
   young (inline `RayQuery` to `intersection_query` asked for in
   2025, issue 6614; flags and candidate kinds still being fixed, issues
   13408, 13609, 13688; its own Metal ray query test never built an
   acceleration structure, 13860), so card S12's first test runs both
   routes; mesh shaders are in Slang's Metal guide, task shaders aren't.
3. **Modules, not includes.** The `.glslh` files become Slang modules
   (`import`); interfaces and generics where a shader has variants
   (materials, the lit surface's paths), parameter blocks for each binding
   space.
4. **One conversion, before 1.0**, with A9's native formats: Rend's 45
   shaders and every caller's, in the same change, each
   checked against its SPIR-V output by the existing tests and
   `../rabbittustests` on all three backends. glslang and glslc leave the
   build; SPIRV-Cross stays only as the Metal fallback and for
   reflection where the native format has none.

Not GLSL (no modules; DXIL through two translations, weakest exactly for
ray queries and mesh shaders) and not HLSL (Metal either a translation or
Apple's Metal Shader Converter, closed and imposing its own binding layout
on Carrottus GPU's Metal backend). Compared: Unreal HLSL, Godot and Hazel GLSL,
NVIDIA's Falcor and RTX Remix Slang.

#### Built 2026-10-31 on Windows (D3D12 and Vulkan): Rend's shaders are Slang packages

- **The build step** is `cmake/rbtrend_slang.cmake` (`rbtrend_add_slang_shader`,
  with `cmake/rbtrend_embed.cmake`), the model of `../carrottus/cmake/cr_slang.cmake`:
  per shader SPIR-V 1.5 (`-fvk-use-entrypoint-name -matrix-layout-column-major
  -warnings-as-errors all`, then `spirv-val --target-env vulkan1.2`); on
  Windows DXIL (Slang HLSL, `dxc -WX`, DXC's validator signing it); both
  Metal sources (Slang's, and `crgpu_msl`'s from the SPIR-V, card S20)
  wherever Carrottus's `crgpu_msl` target exists; on Apple a `.metallib`
  from `crgpu_msl`'s by default, `METAL_ROUTE SLANG <reason>` for Slang's
  own (card S21, decided 2026-10-31 with the user: Slang's Metal output
  can't take the Metal backend's layout from these sources; written, not
  run on a Mac). Each binary is a `uint32_t` array
  with a `_size` macro, and `<stem>.package.inc` defines `static const
  RBTREND_SHADER_PACKAGE <stem>_package`. A caller's build may include the
  file: `../rabbittustests` does. `RABBITTUS_SHADERS` lists the 36 stage
  files by stem, `RABBITTUS_SHADER_MODULES` the 9 modules they import.
  The GLSL is gone from `shaders/`; glslc and glslang left the build;
  SPIRV-Cross stays, for reflection (and Carrottus's `crgpu_msl`).
- **Every built-in shader is made with `rbtrend_shader_create()`** from its
  package (`rbtrendbuiltin.cpp`, `rbtrendbatch.cpp`, `rbtrendlit.cpp`), and
  every built-in pipeline names its shaders' entry points (the stems:
  `rbtrend_fullscreen_vert`...). Rend's pipeline descs keep their entry
  point fields, checked against the shader's reflection as before;
  Carrottus GPU's went (each module has its one, F2), so they are no longer
  passed down. `rbtrend_instance_get_fullscreen_vertex_shader()`'s entry point is
  `rbtrend_fullscreen_vert`, so a caller's pipeline over it names that.
- **Clip space is +Y up (card S19).** The shaders that compute clip
  positions were flipped in the conversion. Rend's own matrices into clip
  space are flipped to match: the sun's cascades
  (`rbtrend_lit_fit_sun_cascades`), and the point and spot shadows'
  (`rbtrend_lit_point_shadow_view_projection`,
  `rbtrend_lit_spot_shadow_view_projection`), each with y negated, so a map
  holds the picture (and winding) it did and the surface reads it the same
  way; and the batcher's CPU projection of a world point to a pixel
  (`rbtrend_batch_project`) negates clip y as the shader's `batch_pixel`
  does. `rbtrendlit.h` and `rbtrendbatch.h` say +Y up.
- **Samplers no shader reads are no longer written.** Where a GLSL shader
  wrapped a sampler round `texelFetch`/`textureSize` only, the sampler was
  used; Slang's `Load` reads none and its SPIR-V leaves the declaration
  out, so a layout made from the shader has no binding for it. The two sets
  Rend wrote one into regardless, the light culling's depth set
  (`rbtrend_texture_view_cull_depth_set`) and the exposure histogram's set,
  write the texture (and the bins) alone. Every other built-in set's
  sampler is read by a shader of its layout.
- **Storage buffers a fragment shader reads are declared `readonly`**
  (`RbtrendLitTiles` in `rbtrend_lit_bindings.slang`, `RbtrendExposureBins` in
  the histogram's adapting pass): a Slang RW type is writable in SPIR-V,
  and Vulkan refuses a fragment shader's writable storage buffer on a
  device without `fragmentStoresAndAtomics`
  (VUID-RuntimeSpirv-NonWritable-06340, found on both GPUs here).
  `readonly` keeps the `u` register on D3D12.
- **Run**: `rbtrend_contract_tests` and `rbtrendreflect_tests` with every
  ctest, `rabbittus-windows-d3d12` and `-vulkan`, Debug; and
  `../rabbittustests`' `rbtrend_gpu_tests` on D3D12 (Intel Iris Xe, NVIDIA RTX
  A2000, WARP; debug layer silent) and Vulkan (Iris Xe, RTX A2000;
  validation silent but for card S22's message below), every read-back
  within its old tolerance, 2026-10-31.
- **Not yet:** the Metal half (never built; Slang's own Metal output does
  not have F4's argument-buffer layout, card S21); `rbtrend_fullscreen_vert`
  and `rbtrend_batch_vert` read `SV_VertexID`, which Slang's SPIR-V turns
  into the DrawParameters capability, and Carrottus GPU's Vulkan device doesn't
  enable `shaderDrawParameters` (card S22): validation reports
  VUID-VkShaderModuleCreateInfo-pCode-08740 at each of the two modules,
  and both devices here draw correctly regardless; a package's SPIR-V
  costs D3D12 and Metal builds its bytes (P1), not measured.

**Metal, 2026-11-01** (Apple M5 Pro, macOS 27.0.1, Xcode 27.2 and its Metal
toolchain 32023.922, Slang 2026.18.3-2; `../prompts/ray_tracing_mac.md`):
every one of Rend's 45 shaders compiled to a `.metallib` by
`crgpu_msl`'s route, card S21's default; none takes Slang's. Each was made
by both routes and their bindings read: the tool's is S17's layout (each
set's argument buffer at 26 + set, the push constants at 30); Slang's has no
argument buffer, puts the push constants and a set's first resource both at
`[[buffer(0)]]`, and 15 of the 45 don't compile from it (two resources at one
index), the six traced ones, the two instance writers and the emitter pass
among them. The `.metallib` step names this build's SDK (`macosx`,
`iphoneos`, `iphonesimulator`) and deployment target, and an iOS build runs
`crgpu_msl` built for the Mac (carrottus's ExternalProject,
`${CRGPU_MSL_EXECUTABLE}`): Rend's shaders built for the iOS device
and the simulator, in a caller's iOS build. **Found:** card S27 (`../DECISIONS.md`): Slang keeps
only the bindings an entry point reads, and Metal lays out a shader's
argument buffer from what it declares, so the sky (`rbtrend_lit_sky_frag`),
histogram exposure (`rbtrend_exposure_histogram_comp`, `_adapt_histogram_frag`)
and glyph (`rbtrend_batch_glyph_frag`) shaders read the wrong members on
Metal: `rabbittustests`' `test_lit_sky`, `test_auto_exposure` and
`test_batch_rbttext_glyph` fail there (11 checks a device), and wait on it.
**Decided 2026-11-01 with you (card S27, a):** a module declares every
binding of each set it reads, with its layout's type and count, and no
other; Carrottus GPU states it for every backend and refuses a module that
differs on Metal (carrottus's `crgpu_shaderbindings_draft.h`), and
`cmake/rbtrend_slang.cmake` passes Slang's reflection of each shader's
declared bindings to `crgpu_msl`, which pads each set, as carrottus's
step does. Rend's shaders declare their sets whole through their
bindings modules; where one declares only what it reads (the histogram
pair's sampler at 1, the glyph shader's texture at 0, if so), its source
declares the rest. A binding a bindings module declares that none of a
layout's shaders reads is refused on Metal (the layout made from shaders
leaves it out), so the bindings modules declare only what their group
reads. Rend's contract changes in comments only, to say so:
`rbtrend_shaderbindings_draft.h`, approved 2026-11-01 by you, as written.
Not built; the three tests
pass on Metal when it is. **Decided 2026-11-01 with you (card X1c):**
`cmake/rbtrend_slang.cmake` writes Metal source and runs `crgpu_msl` only
where a .metallib is made, and off Apple only under carrottus's
`CR_SHADERS_CHECK_METAL` (off by default, on in the Linux CI runner).
**Decided 2026-11-01 with you (card X18, b, in two steps):** first, with
S27's build, `cmake/rbtrend_slang.cmake` becomes a wrapper over carrottus's
one shader step (`cr_add_shader()`, `carrottus/cmake/cr_shaders.cmake`)
that adds the package embedding, and `CMakeLists.txt` always builds its own
`spirv-cross-core` rather than taking a target another repository made.
Then the reflection is made at build time: `src/render/rbtrendreflect.cpp`
moves, computing the same view, into a build-machine tool,
`tools/rbtrend_reflect`, reading SPIR-V from any compiler (not Slang's
JSON); `RBTREND_SHADER_PACKAGE` carries its output, a versioned table of
plain data checked whole before it is read; SPIR-V becomes optional; a
shader Rend can't bind fails the build; SPIRV-Cross leaves the library.
`rbtrend_shaderreflection_draft.h` approved 2026-11-01 by you, as
written.

**S27 and X18's first step, built 2026-11-01 on the Mac** (Apple M5 Pro,
macOS 27.0.1, Xcode 27.2, Slang 2026.18.3-2; Windows and Linux not built):
- `cmake/rbtrend_slang.cmake` is a wrapper over carrottus's `cr_add_shader()`
  (the file carrottus names in `CR_SHADERS_CMAKE`) adding only the package;
  `cmake/rbtrend_embed.cmake` went (carrottus's `cr_embed.cmake` writes the
  arrays). `rbtrend_add_slang_shader()`'s arguments are as they were, so
  a caller's build through it is unchanged.
  `CMakeLists.txt` builds its own `spirv-cross-core`, always.
- `rbtrendpipeline.h`'s comments from `rbtrend_shaderbindings_draft.h`, as
  written.
- The padding is carrottus's tool's own (carrottus `CRGPU_DEVICE_TODO.md`):
  a set is padded to what the shader's source declares. The glyph shader
  needed nothing: `rbtrend_batch_inputs.slang` declares its set whole. Five
  sources declared what their layouts don't hold, or less, and were
  changed: `rbtrend_lit_vert` imports `rbtrend_lit_bindings` (it declared
  only `u_lit_scene` of the lit scene's 14 bindings, 19 refusals in
  rabbittustests); the histogram pair (`rbtrend_exposure_histogram_comp`,
  `_adapt_histogram_frag`) and `rbtrend_lit_cull_bounded_comp` no longer
  declare a sampler no shader of their layout reads; rabbittustests'
  `stencil_frag` the same.
- rabbittustests' `test_set_declared_whole`: a layout made from two shaders
  sharing set 0, one declaring only the texture it loads: refused on Metal
  with `RBT_RESULT_REND_GPU_FAILURE` and Carrottus GPU's
  `CR_RESULT_GPU_SHADER_LAYOUT_MISMATCH` in the message; the whole one
  made.
- Ran: 36 ctests on `rabbittus-macos-metal` in Debug and Release,
  `-asan` and `-tsan`; `rbt_misuse_tests` ten times (368 calls with
  zeroes, 119 with a destroyed handle, no failure); rabbittustests whole on
  Metal, Debug and Release, under API and shader validation with warnings
  fatal, both of its devices: every check passed, `test_lit_sky`,
  `test_auto_exposure` and `test_batch_rbttext_glyph` included, and Rend's
  ray tracing as before; `test_frame_allocations` none of ours in the lit
  and the ray-traced frames, Debug and Release.

**X18's second step, built 2026-11-01 on the Mac:** the reflection made
at build time.
- `tools/rbtrend_reflect` (`rbtrend_reflect.cpp`, its `CMakeLists.txt`):
  `src/render/rbtrendreflect.cpp`'s SPIR-V half moved there unchanged in
  what it computes (`rbtrend_reflect_spirv.cpp`), with a writer of the
  table (`rbtrend_reflect_write.cpp`). Built for the build machine by
  `cmake/rbtrend_slang.cmake` through ExternalProject (or
  `RBTREND_REFLECT_EXECUTABLE`), which runs it on each shader's SPIR-V and
  embeds its output (`<stem>.reflection.inc`) in the package. A shader
  Rend can't bind fails the build. The library keeps the block member
  writers (`rbtrendreflect.cpp`).
- The table (`src/render/rbtrendreflect_table.hpp`, version 1): a 27-word
  header, then resources, members, inputs, storage formats and output
  types as fixed records of 32-bit words, then the strings, each a 0
  inside the area; no pointer. The reader
  (`rbtrendreflect_table.cpp`, `rbtrend_reflect_table_read`) checks the
  magic, the version (`RBT_RESULT_REND_REFLECTION_MISMATCH`), the stated
  size, the stage and entry point, every part's bounds, every string, every
  enum and flag, the resources' order, and every block member's extent
  inside its block, before it reads anything, by memcpy.
  **To do (reviewed 2026-11-01 with the user, ready, no card):** the
  reader skips the extent check for any member without an array size,
  wherever it is (`rbtrendreflect_table.cpp`, the resources' loop), though
  its comment says one may only end a storage block. Refuse one that
  isn't its block's last member, or whose resource isn't a storage
  buffer, so the check and the comment agree. Not a hole today: no write
  goes through such a member's record (`rbtrend_material_*` refuses one,
  `rbtrend_material_set_uniform_bytes` is bounded by the block's size). A
  case in `rbtrendreflect_tests`' refusals with it. **Built 2026-11-01 on
  macOS:** the reader refuses such a member that isn't its resource's
  last record, or whose resource isn't a storage buffer
  (`RBT_RESULT_CORRUPT_DATA`, a message each); `rbtrendreflect_tests` sets
  the lit surface's scene block's first and last members to no size and
  reads each message; it and `rbtrend_reflection_fuzz_regressions` pass in
  Debug, Release, `-asan` and `-tsan`.
- `rbtrendshader.h` and `rbtcore.h` from `rbtrend_shaderreflection_draft.h`:
  `RBTREND_SHADER_PACKAGE::reflection` and `reflection_size`, SPIR-V
  optional, `RBT_RESULT_REND_REFLECTION_MISMATCH` (5004),
  `rbtrend_shader_create()`'s refusals and the file comment. One clause not
  built as written (card A45): the tool can't refuse an acceleration
  structure "outside Rend's own layouts", since Rend's traced shaders
  need theirs; a caller's shader using one is still refused at run time.
  **Decided 2026-11-01 with the user (card A45, a):** it stays a run-time
  refusal, as built (`rbtrendshader.h`: "the entry point using an
  acceleration structure (Rend's own traced passes alone bind one)"):
  it is about who calls, which only the call knows. No tool switch.
  ABI: `RBTREND_SHADER_PACKAGE` 80 to 96 bytes, two members at its end;
  5004 added.
- A package holds the code of the build's `GRAPHICS_BACKEND` alone: SPIR-V
  only for Vulkan.
- SPIRV-Cross is out of the library: `nm` on the Debug and Release
  `libRabbittus.a` shows no `spirv_cross` symbol.
- Tests: `rbtrendreflect_tests` builds the tool's code in and runs every
  case through the table and the reader, the view read back compared with
  the reflected one field for field (the brute-force comparison, kept);
  the build's tables for five of Rend's shaders equal the writer's byte
  for byte; the reader's refusals, and the lit fragment shader's table cut
  at every length (33,224 of 33,224 refused, none read past its end,
  `-asan` included). rabbittustests' `test_shader_create_refusals`: the
  SPIR-V, DXIL, .metallib and reflection pairs, a table's wrong magic,
  another version, a cut one, another stage's and another entry point's,
  and a package without SPIR-V made on Metal. `rbtrend_reflection_fuzz`,
  with its corpus (eight of Rend's tables) and a regression.
- **Decided 2026-11-05 with the user (card A51, b): a vertex shader's
  declared inputs, from Slang's reflection.** Slang's SPIR-V keeps only the
  inputs the entry point reads, and its DXIL keeps every declared input in
  the signature, which D3D12's `CreateInputLayout` requires the layout to
  feed (found 2026-11-05 on Windows, `[ 65 ]`, after Rabbittus
  `7d539c1` left unread locations out of by-position layouts; that was put
  back the same day). `tools/rbtrend_reflect` reads the vertex shader's
  inputs from Slang's reflection JSON beside the SPIR-V
  (`<stem>.reflection.json`, which the shader step writes), each field of a
  struct parameter an input, and marks `used` the ones the SPIR-V reads;
  `rbtrendshader.h` already promises declared inputs with `used`, so no
  public declaration changes. A pipeline by position makes an attribute
  for each element at a declared input's location and none past them
  (`rbtrendpipeline.cpp`), so D3D12 gets its signature and Vulkan no
  attribute nothing consumes. Not changed: a pipeline still requires only
  the inputs the shader uses to be fed; one that leaves a declared, unread
  input unfed is refused by D3D12, as before.
  **Built 2026-11-05 on Windows** (ThinkPad P1 Gen 5, MSVC 14.51), one
  thing found building it: a table listing the JSON's inputs on a Vulkan
  build made Vulkan's validation report "vertex attribute at location 1
  [and 3] not consumed" (the SPIR-V the package carries has no such input).
  So the table describes the code its package carries: `rbtrend_slang.cmake`
  passes `--declared` only where the package carries DXIL (a D3D12 build,
  the same condition that puts DXIL in it); SPIR-V, and the Metal made from
  it, keep the SPIR-V's inputs (`../claude_made_decisions.md`, 2026-11-05).
  - `tools/rbtrend_reflect/rbtrend_reflect_inputs.cpp`:
    `rbtrend_reflect_declared_inputs()`, a bounded JSON reader of its own
    (depth 64; the tool links nothing new) and the completion: every SPIR-V
    input declared at its location and name, each dropped one added, not
    used; refused, failing the build, are a JSON it can't read, a missing
    entry point, an input declared elsewhere or by another name, a dropped
    matrix or array (its locations aren't read; none of the workspace's 46
    vertex shaders has one), overlapping inputs. No fuzz target: its input
    is the build's own file, never a caller's.
  - `rbtrendreflect_tests`' `test_declared_inputs`: the shadow vertex
    shader's SPIR-V (6 inputs) completed from its embedded JSON (8; normal
    and tangent not used), the build's table byte for byte (the completed
    one on D3D12, the SPIR-V's on Vulkan), read back field for field; a
    fragment shader left as it is; eleven refusals from JSON built in the
    test; the real JSON cut at every length, accepted only once whole.
  - Ran (Windows 11): `rabbittus-windows-d3d12` Debug and Release and
    `-vulkan` Debug, no warning, 36 of 37 ctests each (A49), both `-asan`
    presets the same, no report; `rbt_misuse_tests` ten times (119 calls
    with a destroyed handle, 0 failures). rabbittustests: every check passing
    on D3D12 (Iris Xe, RTX A2000, WARP; the debug layer's one message a
    device is card S38's) and on Vulkan (Iris Xe, RTX A2000) under
    validation, synchronization validation and performance warnings: no
    attribute reported unconsumed (the 20 a run before), only card A50's
    batch output at location 9. Two callers' suites rebuilt over it on both
    backends, A49 the one failure in each (the workspace's `ROADMAP.md`,
    section 2). Metal
    and Linux not run (the Metal build passes no `--declared`; its table is
    the SPIR-V's, as before).


### 23. No 2D, line, text or debug drawing

Hazel: `Renderer2D` (batched quads, lines, circles, MSDF text), `DebugRenderer`.
Needs items 9 and 12.

The memory for it is there: transient spans (`rbtrendtransient.h`, see "State
of the code") are what a batch of quads or glyphs is written into each
frame. Not there: the batcher, the shaders, a font atlas, and uniform data
from transient memory (a binding set reads a buffer, not a span).

**Built 2026-10-30 (below).** Before that: **Not started, on 2026-10-24, by decision.** The glyph batch needs Rabbittus Text,
which doesn't exist (`RBTTEXT_TODO.md`). The quads, lines, circles and debug
drawing have no design here to build without it: only their memory and the
glyph batch's shape are written down. What a batch's data is (push
constants, or uniform data from transient memory, which isn't built), what
space a batch is in and how its camera reaches it, and what the kinds are,
are design session item A3 (`../DESIGN_SESSION_AGENDA.md`), whose
recommendation is a one-page design approved before it is built.
**Order confirmed 2026-10-28 with the user (card A16):** section B ran
items 40, 21, 36 and 24 (written 2026-10-24), and this item comes last,
after card A3 decides its design.

#### Built 2026-10-30, run on Metal, then D3D12 and Vulkan

`include/render/rbtrendbatch.h`, `src/render/rbtrendbatch.cpp`, five shaders
(`rbtrend_batch*`). `rbtrend_batch_create` (the color and depth formats,
`max_shapes`) makes its pipelines: four kinds by three depth modes (over,
tested, tested reversed). `rbtrend_batch_begin` takes a frame
(`RBTREND_BATCH_FRAME`: screen or world space, depth mode, view_projection,
viewport, pixels per paper millimetre, the quads' texture and the glyphs'
MTSDF atlas and its range) and room for `max_shapes` records of 128 bytes in
transient memory; `rbtrend_batch_add_quads`, `_add_circles`, `_add_line`,
`_add_glyphs` write records straight into it, checking everything first so
a refused call adds nothing; `RBTREND_COMMAND_DRAW_BATCH` (40) is expanded at
send into a pipeline, slot 0's material, the push constants (96 bytes: the
view_projection, the viewport, the space and the atlas range) and an
instanced draw for each run of one kind, in the order added. Debug:
`rbtrend_batch_debug_box` (twelve edges), `_sphere` (three great circles of
48 segments), `_arrow`, `_axes`. Lines: a record a segment; every measure
in pixels after projection; world-space segments cut at the near plane, the
line broken there; the dash phase laid out along the projected line on the
CPU. Each segment's fragment shader works out a signed distance: the body,
caps (butt, square, round), and at each corner the stroke split on the
bisector so no texel is drawn twice, within which the body carried on is the
mitre, cut by the outer chord for a bevel or a mitre past its limit, or the
corner's distance for a round join; dashes up to four pairs, each finished
with the cap. Circles and arcs by distance in the radius's own units,
antialiased by `fwidth`; glyphs as designed below (median of RGB, the range
from the frame, the outline from alpha, a fade below a pixel of range).
Decided on the way (`../claude_made_decisions.md`, 2026-10-30): straight
alpha in, premultiplied blending; quads, circles and glyphs as a corner (or
centre) and two edges, so world-space ones lie in any plane; up to four
dash pairs (CAD's dashed, centre and most others); push constants to the
fragment stage only for glyphs (Metal's validation refuses bytes a stage
doesn't read); `RBTREND_VERTEX_LAYOUT::elements` made `const`, which took
two `check.baseline` entries with it.

`../rabbittustests`' `test_batch`: a quad; a ring's hole, body and an edge
through a texel against its distance measured as `fwidth` does; an arc's
kept and dropped halves; a line's middle, an edge through texel centres
(half), past it and past a butt end; a square cap; dashes and a dash's end;
paper millimetres; the mitre, bevel and round join each at one texel
worked out against the corner; a translucent L covered once at its inner and
outer corners; glyphs from a hand-made two-page atlas, fill and outline; a
world-space line; a line from behind the eye cut to one record; the debug
calls' record counts; a call past `max_shapes` adding nothing; fourteen
refusals. `test_batch_rbttext_glyph`: Rabbittus Text's 'a' generated as an MTSDF and
drawn at twice its size, its 100 texels well inside filled and 60 well
outside empty. Both devices, Metal's validation, and `leaks`: clean.
D3D12 and Vulkan ran them on Windows the same day (`../rabbittustests/README.md`,
2026-10-30: the Iris Xe, the RTX A2000 and WARP; this file said "Needs
D3D12 and Vulkan runs" until 2026-11-05) and again on 2026-11-05 (the same
adapters, D3D12's debug layer, Vulkan's validation and synchronization
validation, both devices each): `test_batch` and `test_batch_rbttext_glyph`
passing, silent. Linux: on the Iris Xe 2026-11-04 (rabbittustests' whole
run). Not built: a depth-tested frame is
untested on the GPU (only its refusals); bitmap atlases (the design's
"later"); a 2D batch of filled polygons is Rabbittus Math's triangulation and the
caller's mesh, as the header says.

#### The batcher: the design (decided 2026-10-28 with the user, card A3)

The one-page design the card asked for, approved as written here.
Compared with Hazel's `Renderer2D` (four vertices per quad, 20,000 per
batch, separate line and circle batches), Unreal's canvas and batched
elements, and bgfx's and Sokol's transient buffers.

- **One record per shape**, expanded into a quad by the vertex shader:
  position, size, rotation, color, texture rectangle, and the kind's own
  values. Records are written into transient spans (`rbtrendtransient.h`)
  and read as a per-instance vertex binding, as the draw list's instances
  are. A quarter of Hazel's vertex traffic.
- **Per-batch data in push constants**, within 128 bytes: the batch's
  view-projection matrix, the viewport's size in pixels, the batch's mode
  and the paper scale for millimetre widths.
- **A batch is in screen space or world space,** chosen per batch: screen
  space in pixels (interface, HUDs, labels), world space in the scene's
  coordinates (annotation in 3D, debug drawing), there depth-tested
  against the scene or drawn over it, the caller's choice.
- **The kinds:**
  - quads, solid or sampling a texture;
  - lines: width in pixels or in paper millimetres (a pen at a scale),
    butt, square or round caps, mitre, bevel or round joins on a
    polyline, and **dash patterns** (a pattern of dash and gap lengths,
    measured along the line from its start, so dashes stay even at every
    zoom and continue across a polyline's corners);
  - circles, rings and arcs, antialiased by their distance in the
    fragment shader, exact at any size;
  - glyphs, as designed under "Text" below, when Rabbittus Text exists.
- **A debug layer** over it: calls that draw a line, box, sphere, arrow or
  axes for this frame only, collected into the same batches.
- **Built in the autonomous phase**, glyphs excepted; each kind tested
  against its formula worked out in C, read back, as section B's tests.
- **Uniform data from transient memory** is Carrottus GPU's dynamic uniform
  offsets (a buffer bound from a byte offset per draw), its own carrottus
  change (`../carrottus/CRGPU_DEVICE_TODO.md`, "Later"); the batcher
  doesn't need it, materials do.

#### Text: the design (decided 2026-10-15, not started)

Text is split in two. Everything about turning a string into glyphs is CPU
work in a new module, Rabbittus Text (`RBTTEXT_TODO.md`); Rend only draws what
Rabbittus Text hands it. Rend never sees a string, a font or a codepoint.

**The method: MTSDF.** A multi-channel signed distance field (MSDF) in RGB
with a true single-channel SDF in alpha, generated with msdfgen. One atlas
draws a glyph at any size, scale, rotation and under perspective, so text
placed in a 3D scene and zoomed stays sharp; the alpha channel's true
distance is what an outline, halo or soft shadow is computed from (MSDF's
RGB is only exact near the edge). The cost is softer small text than a
hinted bitmap at its exact size. Small fixed-size text is the case bitmap
atlases win, and is not what this is for; a bitmap mode can be added later
as a second atlas kind if a caller needs it, as Unreal has both.

**What Rend holds:**

- **The atlas is an ordinary 2D texture** the caller creates and updates:
  RGBA8 unorm, linear filtering, clamp, one mip. Rabbittus Text reports which
  rectangles changed since the last upload, and the caller updates those
  with `rbtrend_texture2d_update`. Nothing in Rend knows it is a font atlas.
  It grows by layers (a 2D array) or by a bigger texture and a re-pack;
  which is Rabbittus Text's choice, and Rend needs nothing new for either.
- **The glyph batch** is part of the 2D batcher above: per glyph one
  instance (position or 3D transform row, size, atlas rectangle, layer,
  color, outline color and width), written into a transient span, drawn as
  instanced quads. Screen-space and world-space glyphs are one batch kind
  with the transform in the vertex shader; the caller chooses which per
  batch.
- **The shader:** median of RGB, `screenPxRange` from `fwidth` of the atlas
  coordinate and the atlas's distance range, coverage clamped to [0, 1];
  outline and halo from the alpha channel. The distance range (pxRange) is
  data from the atlas, passed in, never a constant in the shader: Hazel
  hard-codes 2.0 in both places and they must agree by hand. Depth test,
  depth write and blending are the pipeline's, set by the caller.
- **No mips.** MSDF doesn't sample mips, so this doesn't wait on item 31.
  At strong minification it aliases; the shader fades coverage once
  `screenPxRange` drops below 1 rather than relying on a mip chain.

**Needs:** items 9 and 12 as above, and uniform data from transient memory.
Item 22 has answered how Rend ships a shader (embedded SPIR-V, built-ins
kept by the instance), so the glyph shader is one more in `shaders/`.

**What not to copy from Hazel** (`Renderer/UI/Font.cpp`,
`Renderer2D::DrawString`, `Renderer2D_Text.glsl`):

- Layout inside the renderer. `DrawString` decodes UTF-8, wraps lines,
  applies kerning and writes vertices in one function. Here layout is
  Rabbittus Text's and the draw takes positioned glyphs.
- No shaping: per-codepoint lookup with kerning pairs only, so no
  ligatures and no complex scripts. Rabbittus Text shapes with HarfBuzz.
- A fixed charset baked at load (Latin, Latin-1, Cyrillic), and a missing
  glyph silently skipped. Rabbittus Text adds glyphs on demand and has fallback
  faces.
- An RGBA32F atlas, four times RGBA8's memory for no visible difference.
- The atlas cached to a disk path from inside the font class, and default
  fonts in static globals. Rend and Rabbittus Text do no file I/O and hold no
  global state.

**Reference: Unreal.** Slate shapes with HarfBuzz, breaks lines and
reorders bidirectional text with ICU, and caches glyphs on demand in a
dynamic atlas with fallback faces per Unicode range (composite fonts).
Since UE 5.5 a font face chooses Bitmap, MSDF, SDF or an approximated SDF
(`EFontRasterizationMode`, with a pixels-per-em setting for the distance
field and per-platform overrides). In-world text is `TextRenderComponent`
over a font texture, or the Text3D plugin's extruded meshes.

**Not this:** single-stroke (plotter-style) fonts are lines, not filled
glyphs, and go through the line batch; Rabbittus Text gives their strokes as
polylines (`RBTTEXT_TODO.md`). Exact curve rendering with no atlas (Slug) is
the quality ceiling and is commercial; MTSDF is the choice until something
shows it isn't enough.

#### Open: the glyph-only vertex output (card A50, decided 2026-11-12 with you)

Every batch pipeline but the glyphs' draws `rbtrend_batch_vert`'s location
9, `extra` (a glyph's outline width), which only the glyph fragment shader
reads, so Vulkan reports `WARNING-Shader-OutputNotConsumed` 20 times a run
on every adapter. Performance warnings count as findings in the
validation runs since card X19 (2026-11-12), so: **a second batch vertex
entry point without `extra`** for the quad, circle and line kinds, the
pipeline chosen by kind as now; the glyphs keep the one that writes it.
Done when rabbittustests' batch tests run under performance warnings
with no message, on Vulkan, D3D12 and Metal.

### 24. Lighting: what is left

Written 2026-10-17 and run on Metal (see "State of the code"). **Every
box but screen-space reflections, transmission and variants done
2026-10-24, run on Metal** (below); those three wait on bigger designs
or measurements, as the plan has them. Run on Windows 2026-10-29 (`../rabbittustests`' `rbtrend_gpu_tests`, Debug: D3D12 on the Intel Iris Xe, the NVIDIA RTX A2000 and WARP with the debug layer printing, and once each under GPU-based validation; Vulkan on the Iris Xe and the RTX A2000 under the validation layer, synchronization validation and GPU-assisted validation; and all five under AddressSanitizer): every check of the seven pieces (`test_lit_cascades`' blending, `test_lit_sky`'s disc and Hosek-Wilkie sky, `test_auto_exposure`'s histogram, `test_lit_surface`'s per-map uv transforms, `test_lit_ambient_occlusion`'s GTAO, `test_lit_point_shadows`, `test_lit_tile_depth_bounds`) passed, with no validation message from them; GPU-based validation on the Iris Xe reports `[942]` twice in `test_material`, which `claude_made_decisions.md` (2026-10-29) takes as the validator's. GTAO's half resolution is still unmeasured. Against the
chain Hazel's `SceneRenderer::FlushDrawList` runs - directional and spot
shadow maps, pre-depth, tiled light culling, skybox, PBR geometry, GTAO,
SSR, bloom, composite - Rend has every stage but screen-space
reflections: cascaded sun shadows and spot shadows, tiled culling (as a
compute pass a compile records), a sky and its background, the PBR
surface with Hazel's model, a horizon-based ambient occlusion, bloom
(item 36) and a composite with four tone curves and adapting exposure.
Against Unreal's forward path: its material inputs (base color,
metallic, specular, roughness, emissive, opacity, opacity mask), clear
coat, a cloth-like sheen, split-sum image-based light from a lookup
texture, stable cascades, eye adaptation. Unreal's deferred path, virtual
shadow maps, Lumen and Nanite are past what a forward renderer is for.

**The shape, decided 2026-10-17.** Rend ships the shading, never the
geometry: the surface fragment shader has a documented interface and the
caller's vertex shader decides the vertex format, the transforms and the
space. That is Unreal's split - a vertex factory per kind of geometry, a
shading model per kind of surface, a material that only hands over values
- not Hazel's, whose one PBR shader is tied to its one vertex layout and
keeps its material values in push constants. Rend's own vertex shaders
are there for a caller with no geometry of its own; a caller with its
own keeps it. Set 0 for the view and set 1 for the material is the usual order of
binding frequency. There is no scene graph: what is drawn, in what order
and into which shadow map is the caller's, and Rend gives it the
matrices (`rbtrend_lit_fit_sun_cascades`,
`rbtrend_lit_spot_shadow_view_projection`) as functions with no GPU in
them, so a caller that frames its own keeps doing so.

Left:

- [x] **Run on D3D12 and Vulkan.** `../rabbittustests` passed on both, on
      an Intel Iris Xe and an NVIDIA RTX A2000, on 2026-10-17: its shadow
      tests the first use there of Carrottus GPU's comparison samplers and of
      D3D12's depth-only passes. One fix came of it, the clear value of an
      environment never filled (see "What has run" under "State of the
      code").
- [x] **Screen-space reflections.** A forward pass has no normal or
      roughness target to reflect from: it needs the surface to write a
      second target (a variant of the surface shader, and a pipeline of
      two targets, item 25), then a ray march over depth and a resolve.
      Hazel does it deferred-style over its own G-buffer outputs.
      **Decided 2026-10-28 with the user (card A6):** (1) the surface
      shader gets a variant that also writes a normal-and-roughness
      target (RGBA16F), paid for only by a caller using SSR; (2) SSR is an
      opt-in command, as BLOOM, its targets from the render target pool;
      (3) a half-resolution march over a Hi-Z depth pyramid; (4) a blur by
      roughness, standing in for the temporal filter item 36 defers; (5)
      where a ray leaves the screen or misses, the sky and environment
      lighting Rend already has (reflection probes later, their own
      item); (6) no G-buffer mode: Rend stays forward. Godot 4's
      forward renderer is the closest match. Tests: a mirror plane's
      reflection of a known quad read back against its place worked out in
      C, on every backend. Not started. **Drafted 2026-11-01, for
      approval, not built:** `rbtrend_reflections_draft.h`: the surface
      variant writing the normal-and-roughness target (and, over card
      A30's recommendation c, the specular target), and three commands,
      LIT_SSR (the march into a reflection target with its confidence),
      LIT_TRACE_REFLECTIONS (S12's rays where the confidence is short) and
      LIT_REFLECT (onto the picture). **Card A30 decided 2026-11-02 with you, c:** after the surfaces, the variant writing a second target with the specular color, as the draft has it. **Draft approved 2026-11-02 with you, with six fixes written into it:** the header (written over A30's c), a half-resolution texel taking the nearest-to-the-eye of its 2x2, the trace undenoised (`_max_roughness` bounds its noise; a filter is a card of its own when asked for), the specular target's alpha unused, the frame's order (opaque, COPY_SCENE_COLOR, SSR, trace, REFLECT, transparent, tone map), and the composite cancelling the environment only to the targets' precision (the gold-mirror test a tolerance). Built 2026-11-02 on the Mac (below). Checked against what a
      caller tracing per view within a distance needs (on or off, a
      distance, the refusal without ray queries, a top level rebuilt each
      frame): nothing more is missing in Rend than what is already
      recorded (cut-outs opaque to rays and hits reading no maps, card
      A15; no vertex color at a hit, A43's c).
      **Built 2026-11-02 on the Mac** (Apple M5 Pro, macOS 27.0.1, Xcode
      27.2, Slang 2026.18.3-2; the workspace's `ROADMAP.md`, the session of
      2026-11-02, steps 1 and 2), from the approved draft with its six fixes; D3D12
      and Vulkan written, not compiled then. Compiled and run since: Vulkan
      on Linux 2026-11-04 (the Iris Xe), D3D12 (Iris Xe, RTX A2000, WARP)
      and Vulkan (Iris Xe, RTX A2000) on Windows 2026-11-05, every
      reflection test passing (`../rabbittustests/README.md`); D3D12's debug
      layer reports one `[ 1422 ]` a device in `test_lit_ssr`, the test's
      reflection texture copied back when a compute pass was the first to
      write it (card S38):
      - `rbtrendlit.h`: RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT (8),
        `shaders/rbtrend_lit_surface_reflect_frag.slang`: target 1 the
        shading normal and roughness, target 2 the specular weight (alpha
        0). The shading is now the module `rbtrend_lit_surface.slang`,
        which it and `rbtrend_lit_frag.slang` call; `hemisphere()` and
        `to_environment()` are `rbtrend_lit_scene.slang`'s.
      - `rbtrendcommands.h`: RBTREND_COMMAND_LIT_SSR (45),
        LIT_TRACE_REFLECTIONS (46), LIT_REFLECT (47), their parameters 64,
        56 and 48 bytes in the union (`rbtrend_contract_tests`'
        `test_reflection_commands`), every refusal at the send one `if`
        each in member order, inside a pass and what the compile can see
        (a view with nothing in it, the scene's view of another size, an
        encoded scene for LIT_REFLECT, a TLAS never built) at the compile.
      - LIT_SSR: a Hi-Z pyramid of nearness from the render target pool
        (`rbtrend_lit_hiz_frag`, `rbtrend_lit_hiz_down_frag`, R32_FLOAT,
        every level to a texel, the pool's entry given a view of all its
        levels), then `rbtrend_lit_ssr_comp`: the 2x2's nearest texel, the
        reflected ray cut at the eye, Uludag's Hi-Z traversal, a hit
        within `_thickness` along the view, the color read at a cone's mip,
        the confidence faded at the edges and as the ray turns back.
      - LIT_TRACE_REFLECTIONS: `rbtrend_rt_reflections_comp` (traced, set 2
        the targets, the scene's environment cubes or the placeholder, the
        BRDF lookup texture), GGX visible-normal rays in R2 directions
        turned by interleaved gradient noise, a hit shaded by the path
        trace's lit BSDF (its `Hit` and `hit_at()` made public), lerp
        under the march's result. Its read-modify-write of an RGBA16F
        storage image needs D3D12's typed UAV loads of that format
        (TypedUAVLoadAdditionalFormats), which every device with ray
        tracing has; recorded for the D3D12 run.
      - LIT_REFLECT: `rbtrend_lit_reflect_frag`, additive, over the scene's
        own set 0, the upsample weighted by plane distance and normal.
      - The sets over the commands' views: two caches of 16
        (`rbtrend_view_set`, lit and ray tracing), matched by layout, views
        and serials. Since card A67 (2026-11-17, item 63) one set of
        records per lit scene, sized by its desc's `max_view_sets`,
        serves all four commands, the traced reflections' cache merged in. `claude_made_decisions.md` (2026-11-02) has every
        call the draft left open.
      Ran: Rabbittus' 37 ctests in Debug, Release, `-asan` and
      `-tsan` (37 before: the new test is a function in
      `rbtrend_contract_tests`); `rbt_misuse_tests` ten times (119 calls
      with a destroyed handle, 0 failures; no new function, the harness
      regenerated unchanged); `../rabbittustests` whole on Metal, Debug and
      Release, under API and shader validation with warnings fatal, both
      devices: `test_lit_ssr` (the floor: 288 texels reflecting the
      square, 846 reflecting nothing, 546 plain and 1,134 sky against C,
      0 wrong; gold within 1.04% of its Fresnel; the floor's silhouette
      18 half texels, 12 the square's, 0 blended; the edge fade within
      0.047; past `_max_roughness` and at confidence 0 the picture
      unchanged bit for bit), `test_lit_trace_reflections` (the wall
      marched: nothing; traced: 784 texels reflecting the square behind
      the eye and 2,652 nothing, 0 wrong; the floor traced and marched 0
      steps of 255 apart over 288 texels; refused without ray queries on
      the device with none), `test_lit_reflection_refusals`, and
      `test_frame_allocations`' 100 frames of reflections, 0 of Rend's
      (with the trace, and without on the device with none). Every
      existing lit test's image unchanged.
      **Hardware ray tracing (card S12, decided 2026-10-28):** once
      Carrottus GPU's ray queries exist, Rend builds and refits acceleration
      structures from what it draws (memory through item 35's allocator),
      adds ray-traced reflections as an opt-in command over SSR's normal
      and roughness target, tracing where SSR misses or roughness is low,
      then ray-traced sun shadows. Inline queries only.
      **A path tracer (card T16, decided 2026-10-31 with the user):**
      Rend's, not a caller's, since nothing in it is any one
      application's: a progressive path-traced target made from an
      `rbtrend_lit_scene` and its draws, whose step call adds N samples per
      pixel to an accumulation buffer and reports how many it holds. The
      caller drives it (samples or a time budget, when to stop, output).
      It renders the lit material model as it is (glTF metallic-roughness,
      emission, opacity, item 24's transmission and volume), so it agrees
      with the raster path; lights are the sun with an angular size, the
      sky as an importance-sampled environment, emissive surfaces, and
      point, spot and area lights as the scene has them (IES later). One
      compute megakernel per sample pass with inline ray queries, the
      material a Slang interface (card A29), its stages separate functions
      so a wavefront split stays possible when measured. Geometry relative
      to the eye, as the raster path's. On a device without ray queries it
      is refused (`RBT_RESULT_UNSUPPORTED`); no software traversal. Denoising
      is not Rend's: Intel Open Image Denoise, CPU first, as an optional
      Rabbittus module of its own (its GPU devices sit outside
      Carrottus GPU). After S12's Carrottus GPU half. Not started.
      **The API, approved 2026-10-31 by the user:** `rbtrend_raytracing_draft.h`,
      its eleventh version, as written: `rbtrend_blas` and `rbtrend_tlas`
      (resources, slots with generations, 64-byte instance records, sets
      translated on the GPU, staged material tables, builds of one TLAS in
      the order sent), RBTREND_COMMAND_LIT_SHADOW_MASK and
      RBTREND_COMMAND_LIT_PATH_TRACE, and `rbtrend_lit_scene_set_light_shadow()`.
      Built in Phase 7 of `prompts/ray_tracing.md`
      (`prompts/ray_tracing_phase7.md`).
      **Phase 7a, the contract: written 2026-10-31 on the ThinkPad**
      (Windows 11, ThinkPad P1 Gen 5: Intel Iris Xe, NVIDIA RTX A2000, WARP
      under D3D12). Against the draft, item by item:
      - `include/render/rbtrendraytracing.h` (new, in `rbtrend.h`): the two
        handles, `RBTREND_BLAS_DESC`, `rbtrend_blas_create`/`_destroy`/
        `_get_slot`/`_get_info`, `RBTREND_BLAS_INFO`, `RBTREND_TLAS_DESC`,
        `rbtrend_tlas_create`/`_destroy`, `RBTREND_RAY_INSTANCE` (frozen, 64
        bytes, read back from the ABI at offsets 0/48/52/56/60),
        `rbtrend_tlas_write_instances`, `_set_instance_count`,
        `_set_materials`, `RBTREND_TLAS_INFO`, `rbtrend_tlas_get_info`: the
        draft's fields, types and order, with Doxygen holding D1-D8 and
        T1-T12 as rules the caller follows.
      - `rbtrendcommands.h`: `RBTREND_BUILD_MODE` (`RBT_ENUM_BASE`, NONE 0
        refused, BUILD 1, REFIT 2, COMPACT 3) and the four commands at
        **41-44, not the draft's 40-43** (card A40, decided with the user:
        DRAW_BATCH has been 40 since 2026-10-30), their params in the
        draft's order; `rbtrend_command_list_send()`'s refusals,
        `_compile()`'s and `_submit()`'s T5 refusal in their comments. The
        handles are declared again there, beside the commands, as
        `rbtrend_lit_scene` and `rbtrend_lit_environment` are.
      - `rbtrendlit.h`: `RBTREND_LIT_SHADOW_KIND` (NONE 0 refused, MASK 1,
        CASCADES 2 refused naming `rbtrend_lit_scene_set_sun_shadow()` until
        A35), `RBTREND_LIT_LIGHT_SHADOW`, `rbtrend_lit_scene_set_light_shadow()`;
        `rbtrend_lit_scene_set_sun_shadow()`'s comment (light 0's one shadow,
        the last of the two calls wins); `rbtrend_lit_material_create()` and
        `_set_values()` (the lit-material table, staged entries).
      - `rbtcore.h`: struct types 5030-5034.
      - `CRGPU_FEATURE_RAY_QUERY` is group 2 of item 39 (a call that needs
        it names it and refuses without it, `RBT_RESULT_UNSUPPORTED`); it is
        not returned by `rbtrend_get_optional_gpu_features()`, which is
        group 1.
      - Where building found the draft wrong or incomplete, decided with the
        user the same day: **A40** (the command values); **A41** (a path
        trace reads a hit's vertices from an instance-wide geometry arena
        each build and refit copies into, since HLSL can't load through a
        buffer address and Metal has no bindless; T2's "storage usage"
        requirement goes with it); **A42** (every BLAS vertex is an
        `RBTREND_LIT_VERTEX`, 48 bytes, position first: T2's "its stride"
        named a stride no buffer or mesh has).
      - The ABI: `abi.txt` 100 lines added, none removed or changed.
      - Every new call returns `RBT_RESULT_NOT_IMPLEMENTED` after its leading
        checks (null, destroyed, struct bases, out pointer, and
        `RBT_RESULT_UNSUPPORTED` without ray queries); the four commands are
        refused at the send (`UNSUPPORTED` or `NOT_IMPLEMENTED`), which the
        compile reports. The three changed calls behave as before.
      - Tests (`../rabbittustests`, each pending until built):
        `test_rt_shadow_mask` (the mask against cascades on the same scene;
        the cull mask; set_light_shadow and set_sun_shadow each replacing
        light 0's shadow; half strength; NULL; the penumbra wider for a disc
        of 0.2, with and without a filter; the refusals),
        `test_rt_path_trace` (the furnace; an emissive square against
        Lambert's polygon formula; the guides; determinism; 8 steps of 1
        against 1 of 8, and 1024 samples against the formula; a
        two-geometry BLAS), `test_rt_instance_sets` (partial writes carried
        over; a write after a sent build; staged material values read by the
        frame in flight and the next; the emitter table's reuse by the same
        bits; every set in use refused), `test_rt_build_order` (T5),
        `test_rt_blas_life` (refit; compaction's sizes and the same shadow,
        a set carried over from before it, a rebuild full size; a reused
        slot refused naming the record), `test_rt_materials` (T7's NULL
        entries naming the instance; values changed; another layout and a
        destroyed material refused), `test_rt_refusals` (every refusal of
        the new calls and commands, in the header's order),
        `test_rt_unsupported` (a device without the feature: every call and
        command refused naming it), `test_frame_allocations`' ray-traced
        frame (asserted zero), and two factories for
        `test_destroyed_handles`. Each new call's forbidden overlap: nine
        cases in `tests/rbtoverlap_tests.cpp`, all reported in Debug.
      - Run, Debug, D3D12: pending on the RTX A2000 and WARP; refused as
        designed on the Iris Xe and on every adapter's device with no
        optional features; every run passing.
        Runs (Windows 11, ThinkPad P1 Gen 5, MSVC 14.51, Vulkan SDK 1.4.363.0):
        Rabbittus `rabbittus-windows-d3d12` and `-vulkan`, Debug and
        Release, 36 ctests each, passing; `rbt_misuse_tests` ten times in
        Debug, passing; `rbtoverlap_tests`' nine new cases reported.
        rabbittustests, Debug, every test passing: D3D12 on the Iris Xe, the
        RTX A2000 and WARP under the debug layer, silent, and once each under
        GPU-based validation (the A2000 and WARP silent; the Iris Xe's 26
        `[942]`, as on 2026-10-30, the validator's); Vulkan on the Iris Xe
        and the RTX A2000 under the validation layer, synchronization
        validation and GPU-assisted validation, silent but GPU-AV's notices
        of its own settings. The ray tracing tests pending on the A2000 and
        WARP, refused as designed on the Iris Xe and every bare device.
        The callers' suites (both on their D3D12 and Vulkan presets, Debug,
        47 and 66 ctests), passing. `check.sh`: clean but `dep-locks` (the
        callers' locks named the commits before Phases 1-6, until
        `--update-locks` after this phase's commit) and the two this machine can't run
        (`mutable-globals`, `doxygen`). Not run this phase: the `-asan`
        presets (no Rabbittus code path ran but the stubs). Metal and
        Linux not built; ThreadSanitizer hasn't seen it.
      **Phase 7b, the implementation: built 2026-11-01 on the ThinkPad**
      (Windows 11, ThinkPad P1 Gen 5: Intel Iris Xe, NVIDIA RTX A2000, WARP
      under D3D12; `prompts/ray_tracing_phase7b.md`). Every 7a test turned
      on unedited and passes; none had to change. The calls made alone are
      in `../claude_made_decisions.md` (2026-11-01, "Phase 7b"), open for
      review. Step by step:
      - **0, the infrastructure.** The acceleration structure is descriptor
        type 6 (`RBTREND_DESCRIPTOR_TYPE_COUNT`): reflected
        (`rbtrendreflect.cpp`), counted in the pools, written in a set. Only
        Rend's traced shaders and sets hold one: a caller's shader that uses
        one is still refused at `rbtrend_shader_create()`, a caller's layout
        refuses the type, and a caller's set has no entry for it
        (`rbtrend_shader_create_own()`, `rbtrend_binding_set_create_traced()`,
        internal). Vertex and index buffers gain BUILD_INPUT and TRANSFER_SRC
        (card S23). Internal device storage buffers
        (`rbtrend_buffer_create_device_storage()`) are multi-state, tracked
        by the list like an indirect argument buffer.
      - **1, the tables** (`rbtrend_rt_state`, made by an instance's first
        BLAS, TLAS or lit material on a device with ray queries). The BLAS
        table is a structure of arrays on the CPU (per index: 8-bit
        generation, live, built-sent, Debug's 32-bit generation, current
        structure, geometry count; index 0 never handed out) and one 16-byte
        GPU entry per index (reference, arena word, geometry count). The
        lit-material table holds a 48-byte entry per lit material (base
        color, emission, roughness, metalness, specular), a slot for the
        material's life, staged on the CPU and copied through transient
        memory by the next submission of a list holding a path trace (D4).
        Both, and the geometry arena, start at 256 entries (64K units of 16
        bytes) and double when full: an allocation at the create that fills
        them, the old contents copied through the upload batch.
      - **2, BLAS.** `rbtrend_blas_create()` checks in the header's order,
        sizes and creates the structure (T3's flags), takes its arena range
        (card A41: the geometries' headers, then each one's
        `RBTREND_LIT_VERTEX`es, card A42, and 32-bit indices) and its slot.
        BUILD_BLAS records, in its run: the table entry and the headers
        (through transient memory), the vertices (and at a BUILD the
        indices) into the arena, the builds and refits in one Carrottus GPU call,
        and for a static BLAS its compacted size into a buffer read back
        once the build's submission has completed and the frame moved on.
        COMPACT makes a structure of that size at the compile and copies
        into it; a BUILD of a compacted BLAS makes full-size storage again
        (T4); the structure given up is released once that frame has
        completed. Allocated per create: the handle, its geometries, the
        structure (2,816 bytes for the test square on the A2000, 216 on
        WARP), a 16-byte size buffer and a READBACK one.
      - **3, TLAS.** `_max_instance_sets` sets, each records and a material
        table in UPLOAD memory mapped for life; per TLAS one device copy of
        each (the current records and table), the device's instances, the
        emitter table; the CPU's copy of the records and table as the writes
        left them, the written-block bitsets, per-index slot counts (one
        generation and others), per-record NULL reach and per-mask-bit
        counts (T7). A write checks each slot against two arrays, copies the
        range into the CPU's copy and fills each block it touches whole into
        the set being prepared; nothing else per record. The send of
        BUILD_TLAS seals the set into the record's padding (T6 checked from
        the counts, the bottom levels listed); a reset or destroyed list's
        unsubmitted seals carry their blocks into the next set; a set is free
        once its build's submission has completed. T5 at submit names both
        lists.
      - **4, BUILD_TLAS.** The set's written blocks copied into the current
        records and table (a region per run of blocks; the rest carried over
        in place), the instance writer (`rbtrend_rt_instances_row64_comp`:
        the record's transform, its material base as the 24-bit user index,
        its mask, triangle culling disabled (D8), its BLAS's reference from
        the table) one dispatch a TLAS, then every TLAS of the run in one
        build call, after one barrier between the BLASes and the TLASes (T8);
        a run building one TLAS twice is two calls. Metal's 72-byte writer
        (`rbtrend_rt_instances_col72_comp`) is written and built here, run on
        the Mac. The bottom-level list Carrottus GPU makes resident on Metal is the
        seal's.
      - **5, LIT_SHADOW_MASK** (`rbtrend_rt_shadow_mask.slang`, two entry
        points by the target's format). A texel's point from the depth and
        the inverse view-projection, its normal from the nearer neighbour on
        each axis, `_ray_count` rays over the sun's disc in R2 directions
        turned by interleaved gradient noise, the hits from
        `RayQuery<ACCEPT_FIRST_HIT_AND_END_SEARCH>`; the filter over a
        group's tile with an apron of the radius, weights Gaussian, by
        normal and by depth. `rbtrend_lit_scene_set_light_shadow()` sets the
        scene's `sun_mask` (binding 13 of set 0, flag 512), and
        `rbtrend_lit_scene_set_sun_shadow()` and it each replace light 0's
        one shadow. The lit surface reads the mask at its fragment.
      - **6, LIT_PATH_TRACE** (`rbtrend_rt_path_trace.slang`, four entry
        points by the guides' formats). The emitter table
        (`rbtrend_rt_emitters_comp`) first when a seal with writes, a material
        table change or a lit material's values changed since it was built
        (D6). One thread a pixel: NEE with the power heuristic for the
        environment, the sun over its disc and one emissive triangle chosen
        uniformly; point and spot lights without MIS, falling off as the
        raster surface does; Burley's Owen-scrambled Sobol; Russian roulette
        from half the bounces; the clamp; Ray Tracing Gems' offsets; a
        material interface (`IRtBsdf`) with the lit model's Lambert and GGX
        lobes. **Not built here:** the environment's cubes (the hemisphere
        of `RBTREND_LIT_AMBIENT` is the environment, sampled with the cosine;
        nothing is importance-sampled by luminance), recorded to come with
        the environment's own sampling table. T7 refuses a cull mask that lets in
        an instance reaching a NULL entry, naming it.
      - **Gaps:** the review of the diff (a second reader, the
        same day) found thirteen; fixed: a BLAS destroy's table writes racing
        the instance's calls (queued and taken in by them, and a send's reads
        under the release mutex), unwritten device tables read (the first set
        writes every block, slot 0 is an inactive instance), one BLAS twice in
        a run (a run of its own), the scratch shared by two runs and BLASes
        built in an earlier run (barriers), an instance count change not
        rebuilding the emitter table, the path tracer's material reads
        unbounded, the compacted size not read again after a rebuild, a
        retiring structure overwritten, the arena past 4 GiB, the mask view
        kept bound after set_sun_shadow, the absent guide's texture leaked,
        set_materials failing after writing. **Kept, each recorded:** a
        compaction or a rebuild compiled in a list that is then reset is not
        undone; a set's BLAS counts as built when a BUILD_BLAS of it was sent
        in any list, not only earlier in the same one, and stays so if that
        list is reset; a reset list's seal leaves the TLAS's "built" and its
        count, and blocks it carries into a set another list has compiled
        don't reach that list; T7's check and the emitter table follow the
        writes as sent, not GPU order, for a path trace sent before a
        BUILD_TLAS in its list; `rbtrend_blas_create()` and
        `rbtrend_tlas_create()` can return OUT_OF_MEMORY (the instance's tables
        made) before their later argument checks. Card A44:
        compiling a submitted BUILD_TLAS again is refused. **Decided
        2026-11-01 with you (card A44, a):** the refusal stays and is the
        contract. `rbtrend_command_list_compile()`'s list of refusals, in its
        BUILD_BLAS ... LIT_PATH_TRACE bullet, gains "a BUILD_TLAS whose list
        was submitted since it was sent (sent again, it seals the next
        set)", the card's own sentence, so no draft of its own;
        `RBT_RESULT_INVALID_ARGUMENT`, the message naming the command by its
        place as every compile refusal does, and losing its "(card A44)".
        A test in rabbittustests: a list holding a BUILD_TLAS, submitted,
        compiled again, refused; sent again, compiled and submitted.
        **Built 2026-11-01 on the Mac:** the line in
        `rbtrend_command_list_compile()`'s refusals; the message "command N:
        BUILD_TLAS's list was submitted since it was sent: its set is built
        and free; send BUILD_TLAS again, which seals the next set";
        rabbittustests' `test_rt_resubmit`, passing on Metal under validation
        with warnings fatal (the device with ray queries; skipped on the one
        without). **Run on Windows 2026-11-05** (ThinkPad P1 Gen 5, Debug):
        passing on D3D12 on the NVIDIA RTX A2000 and WARP under the debug
        layer, and on Vulkan on the A2000 under validation and
        synchronization validation, all silent; skipped, saying so, on the
        Intel Iris Xe (no ray queries on either API) and every bare device.
        Linux: NVK and llvmpipe offer no ray queries (card S36). Not
        b (a set held for good by a list kept, rebuilding what didn't
        change) or c (the seal at compile, against T5's snapshots per
        send). Revisited only if lists become prebuilt (compiled once into
        bundles or indirect command buffers, the compile skipped): then a
        BUILD_TLAS would read the TLAS's latest sealed state at submit, with
        instance writes as commands, a new card; refusing now keeps that an
        addition.
      - **Card A43's measure:** the Dragon of CrGPUMeshShaders takes
        7,279,648 bytes of the arena (4,855,968 of vertices, 2,423,664 of
        indices); positions alone would make the vertices 1,213,992.
        On Metal (2026-11-01, the M5 Pro) its BLAS is 19,236,096 bytes,
        10,831,312 compacted, against 14,150,784 and 5,932,928 on D3D12 and
        Vulkan: the driver's own format, whatever stride it read, so the
        arena's 3,641,976 bytes of attributes are 34% of the compacted BLAS
        there and 61% on the A2000. What the 48-byte stride costs a build
        isn't measured. **Decided 2026-11-01 with you (card A43): a now,
        c with A15, b never.** A42 stands until bindless is built: one
        vertex type, copied whole into the arena. With A15 the arena goes
        (A41 says it can), and a BLAS geometry takes the caller's position
        stride and format, chained on its desc (Vulkan's `vertexStride` and
        `vertexFormat`, D3D12's `StrideInBytes` and `VertexFormat`, Metal's
        `vertexStride` and `vertexFormat`), the build reading the caller's
        buffer and a shading trace reading the caller's attributes through
        bindless: nothing copied. A chained struct breaks no caller, so 1.0
        doesn't close it. Not b: it tunes a copy that is to go and gives
        the caller a per-BLAS choice the copy alone makes. If a large
        model's arena is a measured problem first, that brings A15 forward,
        not b. A draft for c with A15's.
      Runs (Windows 11, ThinkPad P1 Gen 5, MSVC 14.51, Vulkan SDK 1.4.363.0),
      on the final code: Rabbittus `rabbittus-windows-d3d12` and
      `-vulkan`, Debug and Release, and both `-asan` presets, 36 ctests each,
      passing; `rbt_misuse_tests` ten times on each preset, passing.
      rabbittustests, Debug, every test passing: D3D12 on the Iris Xe (every
      ray tracing call and command refused as designed), the RTX A2000 and
      WARP under the debug layer, silent, and once each under GPU-based
      validation (the A2000 and WARP silent; the Iris Xe's 26 `[942]`, the
      validator's, as before); Vulkan on the Iris Xe and the A2000 under the
      validation layer, synchronization validation and GPU-assisted
      validation, silent but GPU-AV's notices of its own settings. The ray
      tracing tests read the same on the A2000 (both APIs) and WARP: the mask
      0 and 127 (128 on WARP) as the cascades, the penumbra 0 texels with no
      disc and 6 with 0.2, the furnace 1.0000 (worst texel 0.0000), 8 steps
      of 1 equal to 1 of 8, the emitter's ground 0.05005 against Lambert's
      0.05004, the two geometries exact. `test_frame_allocations`' ray-traced
      frame: none of ours. `test_destroyed_handles`: 177 calls refused.
      The callers' suites: passing, but for one caller's no-allocation
      scenario that fails in its own code on both presets, which this phase
      doesn't reach (`../ROADMAP.md`, section 2, names it). `check.sh` clean but `dep-locks` (Rabbittus'
      uncommitted work) and the two this machine can't run (`mutable-globals`,
      `doxygen`). Metal written, not compiled; Linux not built;
      ThreadSanitizer hasn't seen it.
      Measured (Release, RTX A2000, D3D12 / Vulkan; `test_rt_measure` with
      `RABBITTUSTESTS_MEASURE`, times on the CPU's clock until the device is
      idle): 100,000 instances writing 1% a frame: the write 20 / 14 us, the
      compile and submit 118 / 72 us, the frame 2.28 / 2.16 ms; 10%: 216 /
      108 us, 203 / 76 us, 2.54 / 2.25 ms; 100%: 1,873 / 1,653 us, 237 /
      134 us, 3.04 / 2.79 ms. The test square's BLAS 2,816 bytes, compacted
      to 1,152 in 0.34 / 0.32 ms; the Dragon's built in 5.0 / 5.7 ms,
      14,150,784 bytes, compacted in 0.58 / 0.54 ms to 5,932,928 (Phase 5's
      Carrottus GPU numbers: 7.24 / 7.85 ms, the same sizes). The path tracer at
      1920x1080, 4 samples a step over two instances: 13.2 / 12.2 ms a step,
      627 / 678 million samples a second; a step building the emitter table
      again costs no measurable more over two instances. Not measured: the
      mask against the cascaded pass at a common size (the test kit renders
      at 64x64), and the emitter table over many emissive instances.
      **Directional lights (card A35, decided 2026-10-31 with the user,
      option b; not built):** the sun becomes a light type. The lit scene's
      lights gain directional ones, up to a fixed count (Hazel's 4 the
      model), each a direction, a radiance and its angular radius; a shadow
      attaches to a light by index, cascades or a traced mask, per light,
      one or the other (setting one while the other is set refused);
      RBTREND_COMMAND_LIT_SKY takes a mask of directional lights and draws a
      disc for each, at its own angular radius (none, a sun, a sun and a
      moon); nothing in Rend is the sun. `RBTREND_LIT_SUN`,
      `rbtrend_lit_scene_set_sun`, `_set_sun_shadow` and
      `rbtrend_lit_fit_sun_cascades` become their per-light forms, and every
      caller changes in the same change (Rend's tests among them).
      Lights stay data; passes are commands naming the lights they
      act on (no engine records lights as commands: Unreal, Unity, Godot,
      Filament, Bevy and Hazel keep them as scene data). The ray tracing
      commands are written per light from the start
      (`rbtrend_raytracing_draft.h`), over today's one sun as directional
      light 0 until this is built.
      **Shadows per light and in one atlas (cards A36, a, and A37, b,
      decided 2026-11-01 with you), built with A35:** one setter,
      `rbtrend_lit_scene_set_light_shadow()`, for any light of the scene,
      its kinds a spot's map, a point's map (six faces), cascades and the
      traced mask, a kind the light's type can't have refused;
      `rbtrend_lit_scene_set_spot_shadows()`, `_set_point_shadows()` and
      their materials' index retire. Every map shadow reads one depth
      atlas per scene, the caller's: each cascade, spot and point face
      names its rectangle, sized per light by the caller, and Rend
      samples within it, its filter clamped at the edge. A caller needs
      several directional lights shadowed now, so A35 is next: its draft
      (`rbtrend_lights_draft.h`) with A36's and A37's public halves, before
      item 50 is built.
      **A35, A36 and A37 built 2026-11-01 on macOS** (Apple M5 Pro, Metal;
      the approved `rbtrend_lights_draft.h`, every item as written but one,
      card A47: the draft renames two of RBTREND_LIT_CASCADE_FIT's fields
      and names one new name, so `map_size` keeps its name, its comment
      the rectangle's width; **decided 2026-11-02 with you: a, `map_size`
      kept**, "map" being the shadow kinds' word for a view in the atlas). One light list (RBTREND_LIT_SCENE_LIGHT, up
      to 4 directional and 128 point and spot lights), `_radius` and
      `_sky_disc`; `rbtrend_lit_scene_set_shadow_atlas()`;
      `rbtrend_lit_scene_set_light_shadow()` for every kind, map kinds over
      a chained RBTREND_LIT_SHADOW_MAPS; 64 shadow pass materials made with
      the scene (`rbtrend_lit_scene_get_shadow_pass_material()`);
      `rbtrend_lit_fit_cascades()`. The scene block stays one uniform
      buffer, 16,016 bytes, under Vulkan's guaranteed 16,384 (Carrottus GPU
      reports no uniform limit); each directional light's mask is its own
      binding by its place among them (`directional_mask_0` to `_3`); the
      surface, the sky (a disc per light that asks), the traced mask (its
      `_light` any directional light, resolved again at the compile) and
      the path trace (every directional light over its disc) read the
      records (`../claude_made_decisions.md`, 2026-11-01). Ran: Rabbittus'
      37 ctests in Debug, Release, `-asan` and `-tsan`, `rbt_misuse_tests`
      ten times (regenerated), `rbtrend_contract_tests`' new cases
      (`rbtrend_lit_fit_cascades()` the same bits as
      `rbtrend_lit_fit_sun_cascades()` in three framings, captured from the
      old code's Debug and Release builds, which agreed); rabbittustests whole
      on Metal, Debug and Release, under API and shader validation with
      warnings fatal: every lit and shadow test moved to the atlas and
      passing its old checks, and new ones: two directional lights' cascades
      in their own squares (green alone under the red one's shadow, red
      alone under the green's, both between), two spots' maps in squares of
      64 and 32 texels with a neighbour of depth 0 and a filter of 4 reaching
      for it (236, 236 against the formula's 236.6), the sky's two discs
      (red 204 and green 204, the model 204; 9 and 12 texels; none with
      `_sky_disc` off), the refusals by message (`test_lit_light_refusals`:
      the 129th point light and the 5th directional, the 65th view, an atlas
      smaller than a rectangle naming the light, a shadow kept and one taken
      away when the lights are sent again), the mask naming light 1 (0 and
      128), the path trace with the sun at light 1 the same bits as at 0, and
      `test_frame_allocations` with four directional lights and their
      shadows sent again each frame and drawn into an atlas: none of
      Rend's (401,280 and 416,178 allocations apart, the platform's).
      The workspace's callers built and passed against it unchanged
      (`../ROADMAP.md`, the same night). D3D12 and Vulkan not built
      (`../ROADMAP.md`, the handoff).
      **Built on Windows 2026-11-02** (the ThinkPad, MSVC, in a caller's
      D3D12 and Vulkan Debug builds): MSVC refused
      `rbtrendlit.cpp` (C2732: `rbtrend_lit_scene_layout()` declared static
      outside the `extern "C"` block and defined inside it; the definition
      moved out, nothing else changed). Then every Rabbittus ctest
      passed but `rbtrend_contract_tests`' `lit_fit_cascades`: 4 of its
      framings' bits differ from the Mac's. Not fixed, two causes to
      choose between: the fit calls `std::pow` (the splits) and `std::tan`
      (the frustum), which aren't correctly rounded and differ between
      Apple's libm and the UCRT; and Apple clang contracts `a * b + c` to
      FMA on arm64 where MSVC doesn't (Rabbittus Math's polygons build with
      contraction off for this). Bit-equal everywhere needs both: the
      fit's file built with contraction off, and the two functions made
      portable (or the test's bits replaced by a tolerance, with the
      reason, as `CONVENTIONS.md`'s "Floating point" allows). rabbittustests
      not run here.
      **Decided 2026-11-12 with you (card A49, b):** a tolerance, the
      functions kept. `rbtrendlit.cpp` is built with contraction off
      (`-ffp-contract=off` on Clang and GCC; MSVC doesn't contract), so the
      fit's arithmetic is the same on every compiler, and
      `lit_fit_cascades` compares each value within 8 ULPs of the captured
      bits (`FIT_ULPS`, its reason beside it): only libm's rounding of
      `std::pow` and `std::tan` is left, and the matrices are no saved or
      synced data. On Linux (Fedora 44, GCC 16, x86-64) one value of each
      framing lay 1 ULP off; Rabbittus' 39 ctests then passed in Debug,
      Release, `-asan` and `-tsan`, and the workspace's callers' copies of
      the test with them. Not yet run on Windows or the Mac (whose bits move
      once contraction is off: the capture was made with FMA).
      **Waiting on Metal's bindless (card A15, decided, not built),
      recorded 2026-10-31 with the user** (`rbtrend_raytracing_draft.h`, T3
      and T7): alpha-tested geometry in ray queries (a cut-out material's
      holes letting rays through) and the path tracer reading material
      maps. Both read a material's textures from one shader over every
      draw, which needs bindless textures on every backend; Vulkan and D3D12
      have them, Metal doesn't yet. Their API is in place (a lit material's
      maps and `alpha_cutoff`, read from the draw list), so each is a change
      inside Rend once A15 is built. With them, card A43 (decided
      2026-11-01): the geometry arena goes, and a BLAS geometry reads the
      caller's positions at the caller's stride and format, chained on its
      desc (a public change: a draft with A15's).
      If ray tracing pipelines come (two sibling Carrottus GPU EXTs, one on
      Vulkan and D3D12, one translating Metal's function tables; carrottus's
      `CRGPU_DEVICE_TODO.md`), Rend writes that work once per EXT, and
      its Metal path's shaders are Metal: an ordinary shader package entry
      since item 22's revision.
- [x] **Shadows from point lights** (a cube map each, six passes). Done
      2026-10-24, run on Metal only: `rbtrend_lit_scene_set_point_shadows()`
      with `RBTREND_LIT_POINT_SHADOW` (frozen: a light and six face
      matrices, bias, offset, softness, strength), up to
      `RBTREND_LIT_MAX_POINT_SHADOWS` (4); face f of shadow i in layer
      6i + f of a 2D array depth map, rendered through
      `rbtrend_lit_scene_get_point_shadow_material(scene, i, f)`;
      `rbtrend_lit_point_shadow_view_projection(light, face, near, reversed)`
      frames each face, a perspective a little past 90 degrees (the tangent
      of its half angle 1.05) so a soft shadow's taps near a face's edge
      stay inside its map. The surface picks the face by the largest axis
      of the fragment's offset from the light and looks that layer up as a
      spot's (`point_shadow_map`, set 0 binding 11; the light block's
      `outer_shadow.z`). Compared: Hazel has no point-light shadows (its
      point lights are unshadowed; its spot lights have maps). Unreal's
      forward renderer shadows point lights through cube maps, one draw a
      face or one layered pass into a cube render target (as I know it).
      A cube array view would need `CRGPU_FEATURE_CUBE_ARRAY_VIEWS`, and a
      layered pass `LAYERED_RENDERING`, both optional; a 2D array of six
      layers a light, rendered a face a pass and looked up by the
      fragment's axis, needs neither, at six passes a light, as the item
      had it. `test_lit_point_shadows`: a light 0.5 over the ground, the
      caster at 0.25; texels reached through the -Z and the -X face read 0,
      their mirrors through -Z and +X the formula (189, 91 against 188.8 and
      90.9), and without the shadow each left texel reads its mirror; five
      refusals. It fails with the lights not linked to their shadows.
- [x] **Depth bounds for the tiles**: without a depth pre-pass a tile's
      frustum runs the whole depth of the view (Hazel's forward+ uses its
      pre-depth for each tile's range), so a light behind a wall is kept.
      A pass that reduces a depth buffer per tile, and culling against it.
      Done 2026-10-24, run on Metal only: `RBTREND_COMMAND_LIT_CULL_LIGHTS`
      (38), a scene and its view's depth, recorded where the caller puts it
      (after the depth pre-pass); its tiles' near and far planes are at the
      least and most depth the tile's texels hold, read by the culling
      thread of each tile (`rbtrend_lit_cull_bounded.comp`, the body shared
      with the unbounded shader through `rbtrend_lit_cull.glslh`). A list
      holding one for a scene has no cull at its top for that scene. The
      depth is read through a set the view keeps, in a layout the compute
      stage sees (`rbtrend_texture_view_t::cull_depth_set`). Compared:
      Hazel's `LightCulling.glsl` is a group a tile reducing its pre-depth
      in shared memory, then culling; Unreal's forward renderer builds a
      clustered light grid (depth slices, froxels) that needs no pre-pass
      (as I know it). The item chose bounds from a pre-pass, as Hazel's;
      clusters are the alternative if a pre-pass is ever too dear. A
      thread a tile reads its 256 texels serially, where Hazel's group
      reduces them in parallel: measured when it shows.
      `test_lit_tile_depth_bounds`: 69 decoy lights behind the ground set
      before one real one; culled over the whole depth, the middle tiles
      fill with decoys and drop the real light (1024 texels dark); culled
      against the depth, the picture is the untiled one texel for texel;
      two refusals.
- [x] **Transmission** (glTF's KHR_materials_transmission): refraction of
      what is behind, needing a copy of the opaque picture.
      **Decided 2026-10-28 with the user (card A7):** a scene color copy
      command, copying the current color target into a mipped texture
      from the render target pool, recorded by the caller between its
      opaque and transparent draws; the lit surface samples it for a
      material with transmission, offset for refraction and at a mip by
      roughness (frosted), tinted by thickness and attenuation as glTF's
      `KHR_materials_volume`. As Unreal's scene color, Godot's
      `SCREEN_TEXTURE`, three.js. Limits: glass behind glass and what is
      off screen don't show through (ray tracing, card S12, later).
      Tests: a refracted known quad read back against its place worked out
      in C, on every backend.
      **Done 2026-10-30, run on Metal** (both devices, under the
      debug layer and shader validation); `test_lit_transmission` ran on
      D3D12 and Vulkan on Windows the same day (`../rabbittustests/README.md`,
      2026-10-30) and again 2026-11-05 (Iris Xe, RTX A2000 and WARP on
      D3D12 under the debug layer; Iris Xe and RTX A2000 on Vulkan under
      validation and synchronization validation; passing, silent), and on
      Linux on the Iris Xe 2026-11-04. `RBTREND_COMMAND_COPY_SCENE_COLOR`
      (39): `_source` stretched onto `_target`'s level (DRAW_TEXTURE's
      path), then GENERATE_MIPS below it, linear both. The texture is the
      caller's, as GTAO's target is, not the pool's: the scene binds it
      for every draw after the command. `rbtrend_lit_scene_set_scene_color()`
      (set 0 binding 12, flag 256, its mip count in the block's old
      padding). `RBTREND_LIT_MATERIAL_VALUES` gains `transmission`, `ior`
      (1.5 by default), `thickness`, `attenuation_color` and
      `attenuation_distance` (block 256 to 288 bytes). The surface gives
      the transmitted share of its diffuse lobe to the picture behind:
      the view ray refracted, followed through the thickness, projected,
      read at a mip of roughness times clamp(2 ior - 2) (three.js's
      choice), decoded and unexposed as the surface wrote it, tinted by
      Beer's law, times (1 - the split-sum's specular). Known limits, as
      the card had them: glass behind glass and what is off screen.
      `../rabbittustests`' `test_lit_transmission`: a red ramp copied and
      read back (mip 0 exact, mip 1 the average), then four texels across
      a full-target face at thickness 4 read within a step of the place
      worked out in C (12 to 48 red away from where the unbent ray would
      read), the tint halving them, no scene color giving black, and seven
      refusals. **Needs D3D12 and Vulkan runs.**
- [x] **A sun disc** in the sky shader (left out of the environment on
      purpose, where it would only blur), and Hosek-Wilkie's sky for a
      better horizon than Preetham's. Done 2026-10-24, run on Metal only.
      - The disc: `RBTREND_LIT_SUN::disc_angle` (appended; 0 for none, to
        0.5 radians), drawn by `RBTREND_LIT_SHADER_SKY_FRAGMENT` over the
        environment, or over black: the sun's radiance spread over the
        disc's solid angle (pi times radiance over 2 pi (1 - cos)), so it
        carries the light the sun lights surfaces with, its edge softened
        over a texel's change in angle (1 - cos compared, not cos, which
        cancels near 1). It rides in `sun_radiance.w` of the scene block.
        Compared: Hazel's Preetham sky draws no disc; Unreal's sky
        atmosphere draws the directional light's disc from its source
        angle, with limb darkening (as I know it). Rend's is a uniform
        disc of the sun's own energy; limb darkening is left.
        `test_lit_sky`: 128 inside against 128 worked out in C, 0 outside,
        left and right alike; two refusals. Fails against the old shader.
      - Hosek-Wilkie: `RBTREND_COMMAND_LIT_SKY` gains `_model`
        (`RBTREND_LIT_SKY_MODEL_PREETHAM` 0, `_HOSEK_WILKIE` 1). The
        authors' 1.4a RGB dataset (3-clause BSD) is vendored at
        `../.deps/HosekWilkie-1.4a`, or fetched by its zip's SHA-256; CMake
        writes it into the build with every table `static const`
        (`rbtrend_hosek_rgb.inc`; their file declares writable globals,
        which `check.sh mutable-globals` refuses), and `rbtrendlit.cpp`
        cooks the nine coefficients and the radiance of each channel for
        the sun's elevation, the turbidity and the ground's albedo (their
        Bezier in the cube root of the elevation), scaled so the zenith's
        luminance is the intensity. The coefficients reach the pass
        (`rbtrend_environment_hosek.frag`) through a material the
        environment keeps; the dataset's order has the paper's I and H at
        7 and 8. Compared: Hazel has Preetham only (`PreethamSky.glsl`).
        Unreal's sky atmosphere is Hillaire's physically based model
        (scattering lookup tables), past an analytic sky; its older
        `SkySphere` was a hand-made blend. Hosek-Wilkie is the analytic
        model fitted to brute-force simulation, with a truer horizon and
        circumsolar glow than Preetham's; the published cost is the
        coefficients, cooked on the CPU per fill. `test_lit_sky`: the
        zenith and 10 degrees up towards and away from the sun, each
        channel against the model worked out in C from the same dataset
        (the test includes the generated file), equal in every channel;
        three refusals. Fails with the coefficients in the paper's order.
- [x] **Cascade blending**: a fragment takes one cascade; a blend over the
      last stretch of each would hide the seam between them (only the
      last fades out now). Done 2026-10-24, run on Metal only: each
      cascade blends into the next over the last tenth of its own stretch
      (from the split before it, or the eye) by a smoothstep, the last into
      no shadow over its own; `rbtrend_lit_fit_sun_cascades` makes each
      cascade after the first reach back over that tenth, so the blend
      reads two maps that both cover it. The tenth is Rend's, as the
      last cascade's fade already was. Compared: Hazel blends two cascades
      over a band centred on each split, its width a renderer setting (1
      unit); Unreal over a fraction of each cascade's length at its far
      end (`CascadeTransitionFraction` on the light, as I know it, 0.1 the
      usual). Rend's is Unreal's shape: the far end, so the nearer, finer
      cascade is the one that fades. `test_lit_cascades`: the ground 5
      along the view, split at 5.25, an empty cascade blended into a
      shadowed one and the other way round, and a shadowed last fading
      out, each against smoothstep in C (59, 68, 47 against 59.4, 68.6,
      47.2); the fit reaches back over the band. Fails against the old
      shader (128, 0, 68).
- [x] **GTAO proper** (Jimenez et al.: cosine-weighted horizons and a
      bent normal) in place of the horizon-based estimate, and at half
      resolution with an upsample, when it is measured. Done 2026-10-24
      but the half resolution (not measured), run on Metal only:
      `rbtrend_lit_occlusion.frag` is the paper's estimator: four slices
      through each texel's own view ray turned by the 4x4 pattern, two
      horizons each from four steps either side (a sample's weight fading
      over the outer 0.615 of the radius, XeGTAO's), clamped to the
      projected normal's hemisphere, the visible arc integrated with the
      cosine in closed form and weighted by the projected normal's length;
      `_intensity` is now a power on the result (1 as measured). The bent
      normal (the paper's Algorithm 2) goes into the target's green, blue
      and alpha, averaged by the blur; `rbtrend_lit_scene_set_bent_normals()`
      has the surface look its diffuse ambient light up along it. The raw
      intermediate is R16G16B16A16_FLOAT. Compared: Hazel runs Intel's
      XeGTAO, the same estimator in compute, view-space, with a denoise
      pass; its bent normals are an option, off, and read by nothing.
      Unreal's GTAO (r.AmbientOcclusion.Method) is Jimenez's too, with
      temporal accumulation (as I know it). Rend's is the estimator in a
      fragment pass with its 4x4 blur, no temporal part (Rend keeps no
      history). `test_lit_ambient_occlusion`: on the step's lower side,
      where the normal is the view vector in every slice, 32 texels at
      radius 0.3 and at 0.6 against the estimator worked out in C over the
      same samples and blur (188 against 188.4 at the step); the bent
      normal straight at the eye far off and leaning away from the step
      beside it; the surface's diffuse ambient light along a bent normal of
      +X half the sky (127) and along its own normal the sky (255). The old
      estimator gets all 32 wrong at radius 0.6.
      **Half resolution, 2026-11-20: the caller's, not here.**
      LIT_AMBIENT_OCCLUSION's contract is a texel of `_depth` each, at the
      view's target size, and rbtrend_lit_scene_set_ambient_occlusion()
      names a texture of that size, so a caller wanting it cheaper draws
      its depth at half the size, measures there and draws the measure up
      onto the target's size with DRAW_TEXTURE's linear filter, with no
      change here. A half resolution inside the command (its raw pass at
      half, its blur the upsample, depth-aware) would change the
      command's contract: a draft first.
- [x] **A histogram for exposure** (Unreal's), which can leave the
      darkest and brightest few percent out, in place of the log
      average. Done 2026-10-24, run on Metal only:
      `RBTREND_EXPOSURE_DESC::low_share` and `high_share` (appended); 0 and
      1 measure every texel by the exact log average as before; otherwise
      `rbtrend_exposure_histogram.comp` counts the luminance texture into 64
      bins over min_log to max_log (a group's bins in shared memory, one
      atomic a bin a group), into a storage buffer set the exposure keeps,
      emptied by a first dispatch; `rbtrend_exposure_adapt_histogram.frag`
      averages the bins between the shares (a bin split where a share's
      edge falls, each standing for its middle) and adapts as before.
      Compared: Unreal's eye adaptation is this (`PostProcessHistogram`,
      64 bins, low and high percent; its defaults 0.1 and 0.9, as I know
      them); Hazel has no adapting exposure. `test_auto_exposure`: half
      the picture at 2^-1.875 and half at 2^3.125 (bin middles); the
      darkest 40% exposes by the dark half (46 there), from 60% up by the
      bright (46 there, 1 in the dark half), each against key / L in C;
      three refusals. The log-average path reads 7 there.
      The run found a Carrottus GPU Metal bug: binding a compute set already
      bound at its index set the argument buffer again, which validation
      refuses (the histogram binds one set for two dispatches); the
      compute encoder now keeps what it has at each index, as it keeps its
      pipeline state (carrottus).
- [x] **Per-map uv transforms** (KHR_texture_transform's), one for all
      maps now. Done 2026-10-24, run on Metal only:
      `rbtrend_lit_material_set_map_uv_transform()` and `_get_` (a 2x3
      matrix a map, applied after the material's own, the identity at
      create, copied by `rbtrend_material_copy`, left alone by
      `set_values`); the material block grows to 256 bytes. A call rather
      than a field of `RBTREND_LIT_MATERIAL_VALUES`, since glTF's transform
      is per texture and `set_map` is per map. Compared: Hazel has one
      tiling for a material; Unreal's material graph transforms any
      texture's coordinates as the author wires it. Rend's is glTF's.
      `test_lit_surface`: the base color map's own transform undoes the
      material's offset (red), the emission map's identity reads where the
      material's puts it (green) until its own moves it back (red); three
      refusals. Fails with the per-map transform ignored.
- [x] **A lit surface's alpha mode** (a caller's transparency stage):
      OPAQUE (its opacity ignored), MASK, BLEND (reading none of the view's
      screen-space inputs). Drafted 2026-11-19: `rbtrend_transparency_draft.h`
      (version 1, seven critique passes), not approved; nothing built.
      Approved 2026-11-20 after seven passes (8 to 14), version 2, hash
      e8dfad9b (`shasum -a 256`, first 8 hex), every question as
      recommended. **Built 2026-11-20 on the Mac (M5 Pro, macOS 27, Metal),
      Debug and Release; D3D12 and Vulkan written, not compiled (card
      X25).** What was built:
      - `RBTREND_LIT_ALPHA_MODE` (OPAQUE 0, MASK 1, BLEND 2) and
        `RBTREND_LIT_MATERIAL_VALUES::alpha_mode`, appended (144 to 152
        bytes on a 64-bit target: a listed break, `abi.txt`);
        `alpha_cutoff` read for MASK alone. A material starts OPAQUE.
        set_values refuses, after the finite check and before the
        response's, each its own check and message: "alpha_mode, %u, is
        not an RBTREND_LIT_ALPHA_MODE"; for MASK "alpha_cutoff, %g, is not
        above 0" and "alpha_cutoff, %g, is above 1". get_values returns the
        mode.
      - The material block (`rbtrend_lit_material.slang`, mirrored by
        `rbtrend_lit_material_block`): `attenuation` a float3 and
        `alpha_mode` a uint at byte 284, the block still 288 bytes on
        Metal, the reflection check naming the member. Nothing in set 0 or
        the scene block (card A75).
      - `LitSurface::screen_space` (`rbtrend_lit_lighting.slang`): 0 reads
        neither the view's occlusion and bent normals in `lit_light()` nor
        a light's traced mask in `directional_visibility()` (lit
        unshadowed by it). The lit surface sets it from the mode
        (`lit_shade_as()`, which `lit_shade_with_base_map()` calls with
        the material's mode), the layered surface 1, the lit particle 0
        (it shades as BLEND whatever the material's mode: item 57's
        "Left" line done).
      - MASK's discard moved after every map's read (the ORM, emission and
        normal maps): defined derivatives on every backend. No cut-out
        picture moved on Metal: every existing figure, cut-outs included,
        is the same as before, bit for bit where a test prints it.
        `RBTREND_LIT_SHADER_DEPTH_CUTOUT_FRAGMENT` discards for MASK alone.
      - The lit-material table's entry (`rbtrend_rt_lit_entry`) carries the
        mode's bits in `surface[3]`, read by no ray (hits stay opaque). The
        draft (pass 8.2) says the entry copies the block whole: it doesn't
        (48 bytes of chosen fields), so the mode went into the one unread
        word, 0 for OPAQUE as before.
      - Not built, the draft wrong against the code: the reflect variant's
        sentence ("writes targets 1 and 2 premultiplied by its opacity as
        target 0 is: the caller blends them with target 0's blend"). Target
        1 is the normal and roughness, its alpha the roughness, unscaled by
        the opacity; target 2's alpha is 0: a premultiplied blend of either
        reads an alpha that isn't the opacity. The variant writes as before
        (the specular weight premultiplied, the normal and roughness not);
        a caller's choice of what to do with them for BLEND waits on a
        decision (the session's report has the options). **Decided
        2026-11-20 with the user (card A76): a**: a BLEND surface is drawn
        with the plain lit variant, or the reflect variant with targets 1
        and 2's writes masked off (the caller's pipeline choice), so the
        surface behind keeps its normal, roughness and specular weight;
        the header says so (documentation only). **Written 2026-11-20**:
        `RBTREND_LIT_ALPHA_MODE_BLEND`'s comment in `rbtrendlit.h`; no
        declaration, layout or ABI changed (`./check.sh abi doxygen
        caller-names` clean).
      - Tests (`../rabbittustests`, `rbtrend_gpu_tests`), each seen failing
        on the code before the mode (the header alone added):
        `test_lit_alpha_opaque` (opacity 0.25 x a map's 0 x a vertex
        color's 0.5, OPAQUE: the same bytes as at 1, alpha 255; failed
        black, alpha 0), `test_lit_alpha_mask` (OPAQUE with a cutoff not
        cut, its shadow whole; MASK cut; a MASK checker read minified, its
        maps after the discard, clean under shader validation; failed: cut
        and no shadow), `test_lit_alpha_blend_screen_space` (BLEND over a
        crease of view occlusion 0.2 within 1/1024 of no occlusion, the
        ORM map's still applied; bent normals not read; a traced mask not
        read; a cascade still shadowing; failed: 26 against 128, 128
        against 255, shadowed), `test_lit_alpha_refusals` (failed: taken,
        and a cutoff of 7 cut), `test_lit_alpha_values` (round trips, a
        new material OPAQUE, rbtrend_material_copy(), `alpha_mode` at 284
        and `attenuation` 12 bytes at 272 by the layout's reflection;
        failed: no member), `test_particles_lit_screen_space` (a lit
        particle over the crease as with no occlusion; its MASK material
        drawn as BLEND; failed: 26 against 128, cut); and two that guard
        what doesn't change: `test_lit_layered_screen_space` (the layered
        surface still 0.2 over the crease) and `test_rt_materials`' BLEND
        trace (the same accumulation). Tests 1, 3 and 7 draw twice, the
        same bytes. The existing cut-out tests say MASK, the blended ones
        BLEND (`test_lit_surface`'s emission at half opacity,
        `test_fog_sky_and_transparent`'s face): the same pixels.
        `test_frame_allocations` unchanged. Every ctest, Debug and Release;
        `rbtrend_gpu_tests` on both devices, Debug and Release, under
        Metal's API and shader validation with warnings as asserts.
      - **Measured** (`test_lit_alpha_measure`, `RABBITTUSTESTS_MEASURE`,
        Release, no validation; a lit face with four 1024 maps over 2560 x
        1440, the view's occlusion read, submit to idle, the mean of 50;
        settled runs): before the mode, 0.86 to 0.88 ms for the blended
        path and the cut-out kept, 0.26 ms all cut; after, OPAQUE 0.87,
        BLEND 0.84 to 0.87, MASK kept 0.87 to 0.89, MASK all cut 0.26 to
        0.27 ms. Within the run-to-run noise (about 1%): the draft's
        estimate (under 1% of the lit fragment) met; BLEND's skipped
        occlusion read not visible at this size on this GPU, nor MASK's
        three reads after a discard.
- [ ] **Variants**: branches on the encoding, the map bits, the shadows
      and the tiles are at run time; a shader per case, or specialization
      constants (item 22), when a measurement shows them.

### 36. Post-processing, and bloom first

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

**Bloom written 2026-10-16, run on Metal the same day** (see "State of
the code"). **Its lens dirt and mask, the pool's report and trim, a
separable blur and an outline written 2026-10-24, run on Metal**
(below). Run on Windows 2026-10-29 (`../rabbittustests`' `rbtrend_gpu_tests`, Debug: D3D12 on the Intel Iris Xe, the NVIDIA RTX A2000 and WARP with the debug layer printing, and once each under GPU-based validation; Vulkan on the Iris Xe and the RTX A2000 under the validation layer, synchronization validation and GPU-assisted validation; and all five under AddressSanitizer): every check of them (`test_bloom_mask_and_dirt`, `test_render_target_pool` with the caller-set retirement, `test_blur`, `test_outline`) passed, with no validation message from them; GPU-based validation on the Iris Xe reports `[942]` twice in `test_material`, which `claude_made_decisions.md` (2026-10-29) takes as the validator's. What was decided:

- **No module of its own.** In Hazel bloom is a step of `SceneRenderer`
  (`BloomSettings` beside `DOFSettings`, its textures members of the scene
  renderer), and in Unreal one stage of `AddPostProcessingPasses`, its
  settings fields of `FPostProcessSettings` and its textures taken from the
  render graph's pool for the frame. In both, each effect is a building
  block that records its passes into the frame, and the order and settings
  of the effects are the scene renderer's (`SceneRenderer`,
  `AddPostProcessingPasses`). Rend's building blocks are commands; the
  order and settings belong to whatever owns the caller's frame
  (the caller's renderer), not to Rend. The textures an effect needs in
  between come from a pool the instance keeps (as Unreal's), not from a
  handle the caller makes. No header of its own: the command's doc
  describes bloom, `rbtrendcontext.h` the pool.
- **A temporal effect will need a handle** (TAA, auto-exposure, motion
  blur): history that lasts from one frame to the next, which a pool that
  lends textures within a frame can't hold. Unreal keeps it per view
  (`FSceneViewState`), Hazel in `SceneRenderer`. Not before one is wanted.
- **A pass over a texture** needed one thing Rend didn't hand out: a
  full-target vertex shader. `rbtrend_instance_get_fullscreen_vertex_shader`
  (`rbtrendshader.h`) gives the instance's own; with it a caller's pass
  over a texture is the commands that exist already (a pass on the
  destination, a pipeline with no vertex input and its fragment shader, a
  binding set of the source, a draw of 3). No further command.
- **What glows is the caller's.** `RBTREND_COMMAND_BLOOM` blooms whatever
  source it is given onto a target; a threshold and knee of 0 make every
  texel glow, which is what a caller drawing only what should glow into a
  source of its own wants. The composite adds onto the target with an
  additive blend, so the source is what glows and the target is the
  picture: no third texture.
- **Fragment passes, not compute**, so the chain renders through the
  machinery mip generation already runs, and a source needs no storage
  usage.
- **One mipped R16G16B16A16_FLOAT texture** of `_levels` levels from the
  instance's render target pool (`rbtrendrendertargets_internal.hpp`), level
  0 half the source's first mip: taken at compile, free again once the
  frame it was taken in has finished (rbtrend_instance_next_frame's wait),
  never shared by two takes of a frame, given back by a compile that is
  not submitted, retired as the caller's retirement says (none until it
  is set; 2026-10-27). Prefilter (13 taps and the soft threshold),
  downsample (13 taps, Jimenez), upsample (a tent `_radius` texels wide,
  added into the level above), composite (the same tent, times
  `_intensity`, added onto the target). Places from `gl_FragCoord` with
  the target size pushed, so odd sizes and a source of another size read
  right.
- **Alpha**: the glow carries the source's coverage, weighted with its
  color, and is added to the target's alpha.
- **Sets live on views.** The set that reads a view (a level of a pooled
  chain, or the caller's source) is kept by the view
  (`rbtrend_texture_view_t::bloom_set`) and released with it. A set kept by
  the pool or the list could outlive the view, or be taken for a new view
  made at the same address; and updating a set at compile retires
  descriptors a compiled, unsubmitted list may still bind.

Left:

- [x] **Run on D3D12 and Vulkan.** `../rabbittustests`' `test_bloom` passed
      on both, on an Intel Iris Xe and an NVIDIA RTX A2000, on
      2026-10-17, after Metal the day before.
- [x] **Exposure and a tone curve** belong with an HDR pipeline (item 24),
      in the composite or after it. Met by item 24, not here:
      `RBTREND_COMMAND_TONE_MAP` (exposure, then a clamp, ACES, Khronos PBR
      Neutral or AgX, then the encoding) after the bloom, and
      `RBTREND_COMMAND_AUTO_EXPOSURE` before it, as Hazel's
      `SceneComposite.glsl` and Unreal's tonemapper both put exposure and
      the curve after bloom. Nothing of it moves into the bloom.
- [x] **A lens dirt texture** (Hazel's) and a mask texture weighting the
      glow (done 2026-10-24; run on Metal only). `_bloom` gains `_mask`,
      `_lens_dirt` and `_lens_dirt_intensity`, each NULL or 0 as before.
      The mask's red weights each of the prefilter's 13 taps before they
      are summed, so a masked texel adds nothing however bright, rather
      than being blurred into its neighbours and then weighted. The dirt is
      read at the target's place in [0, 1] and scales the composite's glow
      by 1 + dirt * intensity, in color; alpha stays the glow's. Each is a
      second set of the bloom layout on its pass (`rbtrend_bloom_prefilter`,
      `_composite`), the source's or level 0's set rebound when absent, a
      flag in the push block saying which. Compared: Hazel adds `bloom +
      bloom * dirt * BloomDirtIntensity` in `SceneComposite.glsl`, the same
      formula; Unreal's `BloomDirtMask` with an intensity and a tint (as I
      know it, not checked against source) multiplies the bloom in its
      tonemapper the same way. Neither has a mask: what glows is the
      threshold's, or the emissive's. Rend's mask is for a caller that
      picks what glows without drawing a source of its own. The dirt's tint
      is the caller's texture. `test_bloom_mask_and_dirt` reads a mask of
      128 as exactly 128/255 of the glow (a threshold of 0 makes the glow
      linear in its taps), a half mask as the glow of the other half, and
      dirt of 128 at intensity 2 as 1 + 2 * 128/255 of it on the dirty side
      and the glow alone on the other, each against the formula in C; it
      fails with the mask and dirt switched off in the recording.
- [ ] **Cost** is two passes a level and two more, every time the command
      is compiled in. Fewer levels is the caller's setting. Nothing is
      skipped for a source that has nothing in it.
- [ ] **The first bloom makes four pipelines**, the first onto a target
      format another, and the first of a size a pooled texture, inside a
      compile.
- [ ] **A view a bloom read keeps its set until it is destroyed.**
- [x] **The pool's memory**: a pooled texture for every size and level
      count taken and not yet retired, and one per bloom compiled in a
      frame, times the frames in flight. Reported and trimmed since
      2026-10-24 (run on Metal only):
      `rbtrend_instance_get_render_target_pool()` fills an
      `RBTREND_RENDER_TARGET_POOL_INFO` (`RBT_STRUCT_BASE_OUT`, the first
      in Rend: textures, how many are busy, and their texel bytes, every
      mip, as the formats count them), and
      `rbtrend_instance_trim_render_targets(instance, idle_frames)` destroys
      every texture the GPU is done with that has gone untaken for
      `idle_frames` (0: all of them), through the release queue, and says
      how many. **Decided 2026-10-27 with you (card X1b, line 9):** the
      pool's own 120-frame retirement is gone, as both Unreal's and
      CRYENGINE's leave the standing policy to their caller (a floor in MB;
      a budget in MB). `rbtrend_instance_set_render_target_retirement(instance,
      idle_frames, keep_bytes)` sets it: at each next frame, while the pool
      holds more than `keep_bytes`, what has gone `idle_frames` untaken
      goes. Unset, the pool destroys nothing by itself; the workspace's
      callers set 120 frames and no floor, as before. Written 2026-10-27, run on
      Metal only (rabbittustests' `test_render_target_pool`). Busy
      means taken in a frame the GPU may still run, or by a compile of this
      frame, whose list holds the take. Compared: Hazel has no pool (each
      pass owns its framebuffers). Unreal's `FRenderTargetPool` keeps what
      it made, frees elements unused for some frames once it is over a
      budget (`r.RenderTargetPoolMin`, as I recall the name), and reports
      its memory to a console command; its render graph's transient
      allocator aliases memory within a frame, which Carrottus GPU's heaps would
      allow later (item 35). Rend reports and lets the caller trim, and
      decides no budget. `test_render_target_pool`: emptied, a bloom's
      chain of (32*32 + 16*16 + 8*8 + 4*4) * 8 bytes worked out in C, busy
      in its frame (a trim keeps it), free once the frames turn, kept by a
      trim of more idle frames than it has sat and destroyed by one of
      exactly as many; two refusals. It fails against a trim that ignores
      busy textures.
- [x] **The pool is bloom's alone so far.** Since 2026-10-24 the blur and
      the outline take from it too (below).

Hazel: `SceneRenderer::BloomCompute`, `Resources/Shaders/PostProcessing/Bloom.glsl`,
`SceneComposite.glsl`.

After bloom, in the order a caller is likeliest to want them: a separable
blur on its own (it falls out of the chain), an outline from a mask, a
tone curve, then the lit pipeline's stages under item 24. The tone curve
is item 24's `TONE_MAP`. The blur and the outline, written 2026-10-24 and
run on Metal only:

- **`RBTREND_COMMAND_BLUR` (36)**: a Gaussian of a caller's sigma (above 0,
  at most `RBTREND_BLUR_MAX_SIGMA`, 32), reaching ceil(3 sigma) texels each
  way with the weights normalized, as two passes (`shaders/rbtrend_blur.frag`):
  across from the source into a pooled R16G16B16A16_FLOAT texture of the
  target's size, then down into the target. Taps are read in pairs through
  the linear filter (Rákos, "Efficient Gaussian blur with linear sampling",
  2010), r + 1 samples for a radius of r. The target is the source's size:
  a paired tap reads two texels only on the target's own grid, and a first
  version that read a source of another size at the target's spacing
  aliased (a 32x32 blur of a 64x64 line read 24, 0, 25, 52 across its
  middle), so a smaller blur is the source drawn down first
  (`DRAW_TEXTURE`). Its set is the bloom layout's shape, so a view's bloom
  set serves it. Compared: Hazel's only blurs are fixed 7-tap Gaussians in
  compute for one purpose each (SSR's pre-convolution, GTAO's denoise); no
  general one. Unreal's `AddGaussianBlurPass` (`PostProcessWeightedSampleSum`)
  is separable, sized by a percentage of the view, its kernel in linear
  pairs too, and blurs at its input's size. Rend's is Unreal's shape at
  Rend's layer: sigma in texels (the caller knows its sizes), fragment
  passes, no view. `test_blur`: a one-texel white line down and one
  across, blurred at sigma 2, every texel 8 each side of the line at both
  ends against the weights worked out in C (51 on the line against
  50.92); six refusals. It fails with the pairs read at texel i, not at
  their weighted place.
- **`RBTREND_COMMAND_OUTLINE` (37)**: an outline round a mask (red above
  0.5 is inside) of a caller's color and width, over the target, by a jump
  flood (`shaders/rbtrend_outline.frag`, three modes): a pass seeding each
  inside texel's centre, a pass for each halving jump from the first power
  of two k with 2k - 1 reaching width + 1, one more of 1 (JFA+1), and the
  pass that draws, ping-ponging between two pooled R32G32_FLOAT textures
  read with `texelFetch`. Each texel outside the mask is covered by width
  + 1 less the distance between its centre and the nearest inside centre,
  held to [0, 1], so a whole width covers that many texels from a straight
  edge with a soft outer side; inside texels are untouched; the color is
  blended premultiplied over the target. The mask is the target's size.
  Compared: Hazel's selection outline is a jump flood too
  (`JumpFlood_Init`, `_Pass`, `_Composite`): a nearest-vector per texel,
  both sides of the edge, a fixed orange and a fixed smoothstep width in
  uv. Unreal's editor outline (`PostProcessSelectionOutline`, as I know
  it) samples a selected object's custom depth and stencil over a small
  neighbourhood, so its width is a few texels. Ben Golus's "The Quest for
  Very Wide Outlines" (2020) measures the two and finds the jump flood the
  one whose cost doesn't grow with the width. Rend takes Hazel's jump
  flood with the width and color the caller's, outside the mask only, in
  texels. `test_outline`: a disc of radius 8, outlines of width 2.5, 1 at
  half alpha and 20, every one of the 4096 texels against the coverage
  worked out in C by brute force (every inside centre tried), none wrong;
  six refusals. It fails with the long jumps left out (40 and 2224 texels
  wrong).

Left of item 36: the cost points above (two passes a level, pipelines
made inside a compile, a view's set kept until it goes), all unchanged; an
outline inside the mask, or centred on its edge, is not there (a caller
draws one by outlining the mask's inverse).

### 40. Present scaling: a source of another size than the back buffer

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

Numbered out of order, as item 31. **Decided 2026-10-23. Rend's half
written 2026-10-24 and run on Metal; Carrottus GPU's half done (2026-10-23,
run on Metal, D3D12 and Vulkan; see "Swap chain scaling" below).** Run on Windows 2026-10-29 (`../rabbittustests`' `rbtrend_gpu_tests`, Debug: D3D12 on the Intel Iris Xe, the NVIDIA RTX A2000 and WARP with the debug layer printing, and once each under GPU-based validation; Vulkan on the Iris Xe and the RTX A2000 under the validation layer, synchronization validation and GPU-assisted validation; and all five under AddressSanitizer): every check of `test_draw_texture` (its rectangles included) passed, with no validation message from it; GPU-based validation on the Iris Xe reports `[942]` twice in `test_material`, which `claude_made_decisions.md` (2026-10-29) takes as the validator's. `test_present_scaling` is Metal's alone (it skips off Apple).

**Decided 2026-10-27 with you (card X1b, line 7): rectangles.**
`DRAW_TEXTURE` takes `_source_rect` and `_target_rect`
(`RBTREND_TEXEL_RECT`, appended, a zeroed one the whole mip), as Unreal's
`AddDrawTexturePass` and CRYENGINE's `CStretchRegionPass` take theirs: the
source rectangle fitted into the target rectangle by `_scaling`, the
target outside it kept (the pass loads and scissors only then). A linear
filter at a source rectangle's edge mixes in the texel beyond it, as any
sampled edge does. `PRESENT_TEXTURE` is unchanged (both whole). Written
2026-10-27, run on Metal only (rabbittustests' `test_draw_texture`: three
drawings and three refusals); D3D12 and Vulkan not run.

**What was written, 2026-10-24** (the design below, with two changes, both
in `claude_made_decisions.md`):
- The enum is `RBTREND_TEXTURE_SCALING` (NONE, STRETCH, ASPECT_FIT), not
  `RBTREND_PRESENT_SCALING` with EXACT, since a second command takes it.
  `PRESENT_TEXTURE`'s NONE (0) is the copy of item 38, unchanged.
- A second command, `RBTREND_COMMAND_DRAW_TEXTURE` (35): a SHADER_RESOURCE
  2D view of one mip drawn into a RENDER_TARGET 2D view of a float-holding
  format, in each fit (its NONE is drawn texel for texel from the top left,
  the border past the source). It is the same draw
  (`rbtrend_cmd_draw_fitted`, `rbtrendcommands.cpp`) that `PRESENT_TEXTURE`'s
  STRETCH and ASPECT_FIT make onto the back buffer, and is what the tests
  read pixels through, having no window. Unreal's `AddDrawTexturePass` is
  the same tool.
- `shaders/rbtrend_present.frag`, a pipeline per target format
  (`rbtrend_builtin_present_pipeline`), a set per view and filter kept by
  the view (`rbtrend_texture_view_t::present_sets`, through the downsample
  samplers), and for `PRESENT_TEXTURE` a view of the source's mip 0 kept by
  the texture (`rbtrend_texture2d_t::present_view`, released with it,
  retired at a resize, not counted against the resize's view check).
- The back buffer is moved from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL,
  drawn whole, and moved to PRESENT_SRC; the swap chain needed nothing new.
- The scaling, filter and border are checked when the command is sent
  (one check for both commands), the rest at the compile.
- `../rabbittustests`: `test_draw_texture` (STRETCH of a 2x2 source by
  quadrant, nearest, the right way up; linear at the middle; ASPECT_FIT of
  32x16 into 64x64 with bars; NONE from the top left; a half-float source
  onto RGBA8; eight refusals; STRETCH taking a NaN border it doesn't read)
  and `test_present_scaling` (Metal only: a Rend swap chain on a
  windowless `CAMetalLayer`, a 2x2 RGBA8 source presented into BGRA8 back
  buffers by STRETCH and by ASPECT_FIT, each frame acquired, submitted and
  presented; the copy's format refusal). The swap chain test can't read the
  back buffer: the pixels are `test_draw_texture`'s. The test devices now
  ask for `CRGPU_FEATURE_PRESENT` where the adapter has it.
- Run on Metal (Apple M5 Pro): `../rabbittustests` Debug under Metal API and
  shader validation with warnings fatal, both devices (the bare one skips
  the swap chain), and Release under `leaks` (none); Rabbittus' 17
  headless tests; two callers' tests (18 and 34) over the header, whose
  presents are zeroed and so copies. **Not built on Windows or Linux.**
- Both callers chose ASPECT_FIT the same day (never STRETCH: neither
  wants a picture out of shape, even for a frame). One letterboxes in its
  background color, the other in black; each renders at the back buffers'
  size, so the fit is one to one but while a resize lags. One caller's
  player ran 300 frames in a real window on Metal under validation with
  warnings fatal, exit 0, the picture right. Left: a window resized by
  hand, to see the bars, and a run on D3D12 and Vulkan.
`PRESENT_TEXTURE` is a copy, and since 2026-10-22 it refuses a source of
another format than the back buffers or smaller than them (item 38). That
stays the default; scaling is a mode the caller names, never chosen for it.

**Two kinds of scaling, at two layers:**

| Layer | What it scales | How |
|---|---|---|
| Rend, `PRESENT_TEXTURE` (this item) | The caller's texture into the back buffer | A full-target draw: the same picture, filter and bar color on every backend, and the shader converts format |
| Carrottus GPU, the swap chain (carrottus) | The back buffer onto the window | The compositor: DXGI scaling, `VK_KHR_swapchain_maintenance1`'s present scaling, `CAMetalLayer.contentsGravity`; free, but each platform takes a different subset |

Unreal and Hazel present with a full-target draw (Unreal's back buffer
pass, Hazel's composite into the swap chain), which is what this item is.
The compositor's half is Carrottus GPU's to translate, and is not here.

**What Rend adds** (in `RBTREND_COMMAND`'s 120-byte `_params`, so the
command stays 128 bytes):

```c
/** How PRESENT_TEXTURE puts its source into the back buffer. */
typedef uint32_t RBTREND_PRESENT_SCALING;
enum {
	/** 0: a copy, the rule of item 38: the source in the back buffers'
	 *  format and at least their size, a larger one cropped. */
	RBTREND_PRESENT_SCALING_EXACT      = 0,
	/** A full-target draw: the source filtered to fill the back buffer. */
	RBTREND_PRESENT_SCALING_STRETCH    = 1,
	/** The same, aspect kept and centred; the bars are _border_color. */
	RBTREND_PRESENT_SCALING_ASPECT_FIT = 2,
};

/** RBTREND_COMMAND_PRESENT_TEXTURE. */
struct {
	rbtrend_texture2d _source;
	rbtrend_swap_chain _swap_chain;
	RBTREND_PRESENT_SCALING _scaling;
	/** STRETCH and ASPECT_FIT: CRGPU_FILTER_LINEAR or _NEAREST. Not read for EXACT. */
	CRGPU_FILTER _filter;
	/** ASPECT_FIT's bars, linear RGBA. Not read otherwise. */
	float _border_color[4];
} _present_texture;
```

- **0 is today's behaviour**, so existing callers compile and run as they
  do. Its comment says what 0 means, as the "no defaults" rule allows.
- **The draw**: Rend's full-target vertex shader
  (`rbtrend_instance_get_fullscreen_vertex_shader`) and a new built-in fragment
  shader, `rbtrend_present.frag`: `gl_FragCoord` mapped into the source's
  rectangle in the back buffer (all of it for STRETCH, the centred fit for
  ASPECT_FIT), the bar color outside it, sampled through the instance's
  linear or nearest sampler. A pipeline per back-buffer format, made on
  first use and kept by the instance like the other built-ins
  (`rbtrendbuiltin_internal.hpp`).
- **Format**: the draw writes the back buffer's own format, so a source of
  another (an HDR or float picture onto an 8-bit back buffer, sRGB encoding
  included) is taken in the draw modes; EXACT keeps its refusals. A source
  needs `RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE` in the draw modes, and
  is moved to `SHADER_READ_ONLY` (not `TRANSFER_SRC`).
- **The back buffer** is rendered into, which Carrottus GPU's back buffers always
  allow (they are color attachments on every backend); a swap chain needs
  no new usage. Its view is the swap chain's
  (`crgpu_swap_chain_get_image_view`), moved to `COLOR_ATTACHMENT_OPTIMAL`
  from `UNDEFINED` and to `PRESENT_SRC` after.
- **Checks**, one to a line, each printed: a scaling that is no
  `RBTREND_PRESENT_SCALING`; a filter other than LINEAR or NEAREST; a
  border color not finite (ASPECT_FIT); a source without shader-resource
  usage (draw modes).
- **Swap chain scaling: Carrottus GPU's half is done (2026-10-23).** Carrottus GPU's
  swap chain names a required `CRGPU_PRESENT_SCALING` (ONE_TO_ONE,
  STRETCH, ASPECT_FIT; NONE refused), and
  `crgpu_surface_get_present_scalings` says which a surface takes.
  `rbtrend_swap_chain_create` passes ONE_TO_ONE, which every surface takes:
  a back buffer the window's size, nothing for the compositor to scale.
  Compiled and run on Metal (macOS); on 2026-10-23 also compiled and run on
  Windows 11 over D3D12 and Vulkan (Debug, debug layer and validation on,
  `carrottus/CLAUDE.md`), where Carrottus GPU's Vulkan backend needed two fixes
  first (a crash in the scaling query on a device with
  `VK_KHR_swapchain_maintenance1`, and ASPECT_FIT offered on a Win32
  surface and shown as a stretch). Rend's swap chains ran in two
  callers' windows: each of the two, on both backends (one on both GPUs,
  Intel Iris Xe and NVIDIA RTX A2000, the other on the RTX A2000), resized
  by hand. The picture was never stretched or squashed while the window
  was resized; one caller's redrew continuously during the drag, the
  other's scene stopped moving during the drag and its picture either
redrew during it or only once the drag ended (reported). No debug
  layer or validation message (synchronization validation too on Vulkan),
  and each closed with exit 0. Vulkan's one-to-one on the Iris Xe is the
  unscaled swap chain at the window's size (it has no
  `swapchain_maintenance1`); on the RTX A2000 it is the extension's
  one-to-one. Not run: Linux. Letting a caller choose the compositor's
  scaling instead (back buffers at the source's size, an EXACT copy, the
  compositor stretching for free, where the query says the surface takes
  it) is a later option, not added: it would be a field on
  `RBTREND_SWAP_CHAIN_DESC`.

**Tests** (`../rabbittustests`, no window): the present shader drawn into an
offscreen target standing in for a back buffer (the draw is the same
function with a view and a size): STRETCH of a 2x2 four-color source onto
64x64 (each quadrant its color); ASPECT_FIT of a 32x16 source onto 64x64
(bars of the border color above and below, the source between); NEAREST
against LINEAR at a quadrant's edge; a float source onto an 8-bit target;
each refusal. The swap chain itself is only checked in a caller's host on
each backend.

**Why not a blit:** Carrottus GPU's blit resamples only on Vulkan; on D3D12 and
Metal it is a copy (`crgpu.h`).

### 41-49. From the comparison with Hazel (2026-10-30)

`ROADMAP.md` section 3.3: Hazel's `Renderer/`, `UI/` and shaders
(`Hazelnut/Resources/Shaders/`, with `PostProcessing/` and
`Include/GLSL/`) read against Rend's headers and every item here. These
nine are what Hazel can do that nothing above lists. Every one adds a call
or a field, so none is built here (section 3.3 builds only what leaves the
API's shape alone); each wants its shape decided first. Paths: `R/` is
`Hazel/src/Hazel/Renderer/`, `Sh/` the shaders.

- [ ] **41. Depth of field**: single-pass bokeh from a focus distance and a
      blur size (`R/SceneRenderer.h:100` `DOFSettings`,
      `R/SceneRenderer.cpp:1245-1277`, `Sh/PostProcessing/DOF.glsl`). A
      command beside BLOOM.
- [ ] **42. Edges from normals and depth**: outlines on silhouettes and
      creases, mixed in at the composite (`R/SceneRenderer.cpp:1173-1237`,
      `Sh/PostProcessing/EdgeDetection.glsl`). OUTLINE works from a mask
      only; SSR's normal target (card A6, waiting on A30) is the input.
- [ ] **43. Soft shadows sized by the light (PCSS)**: a blocker search,
      then a filter as wide as the penumbra (`Sh/Include/GLSL/
      ShadowMapping.glslh:10,51,160`, `LightSize` in `Buffers.glslh`).
      Rend's `softness` is a fixed width in texels; the shadow structs
      are frozen, so a light size is a new or chained struct.
- [ ] **44. Lighting debug views**: a heat map of lights per tile and
      cascades tinted (`Sh/HazelPBR_Static.glsl:309,325`). A debug mode on
      the lit scene.
- [ ] **45. Skinned meshes' built-in shaders**: skinned surface, pre-depth
      and shadow vertex shaders over bones in a storage buffer
      (`Sh/HazelPBR_Anim.glsl`, `DirShadowMap_Anim.glsl`,
      `PreDepth_Anim.glsl`). Item 24's shape already lets a caller's own
      vertex shader skin; whether Rend ships skinned
      `RBTREND_LIT_SHADER_*` vertex shaders is the question.
- [ ] **46. A blurred sky background**: the environment drawn at a chosen
      mip (`Sh/Skybox.glsl:31,39`, Hazel's `SkyboxLod`); Rend's sky reads
      mip 0. A field on LIT_SKY.
- [ ] **47. GPU memory report**: used and available, allocation counts and
      sizes for buffers and images (`R/GPUStats.h:7-18`). Cheap once item
      35's allocator (VMA's statistics) exists.
- [ ] **48. Per-frame draw and instance counts** (`R/Renderer.h:204-205`,
      `R/SceneRenderer.h:121-131`): what a list's compile issued.
- [ ] **49. More than one texture in a 2D batch** (Hazel's 32 slots,
      `R/Renderer2D.h:146,182`): the batcher has one texture a frame;
      `RBTREND_BATCH_QUAD` is frozen, so a slot index is a new quad type.

Checked and already covered: every pass of Hazel's scene renderer (shadows,
pre-depth, tiled culling, sky, IBL, PBR, GTAO, SSR as decided, bloom,
composite, selection outline), its pipeline state, materials, command
buffers, compute, instancing, 2D and debug drawing and text. Hazel's fog
and film grain are switched off in its own composite. Hazel has no mesh
LOD.

## Needs something Carrottus GPU doesn't expose yet

What Carrottus GPU exposes follows from the GPU APIs it translates and is decided
there, not by this list. These are what Rend can't do until it does.

**Phase 7 on Metal, 2026-11-01** (Apple M5 Pro, macOS 27.0.1, Xcode 27.2;
`../prompts/ray_tracing_mac.md`): built and run as 7b left it, with no change
to Rend's ray tracing code. `rabbittustests`, Debug and Release, Metal API and
shader validation with warnings fatal: every ray tracing test passes, reading
as on the RTX A2000 and WARP: the mask 0 and 128 against the cascades' 0 and
128, the penumbra 0 texels with no disc and 6 with 0.2, the furnace 1.0000
(worst texel 0.0000), 8 steps of 1 equal to 1 of 8 (the same bits), the
emitter's ground 0.05005 against Lambert's 0.05004, the two geometries exact,
instance sets, build order, a BLAS's life (compacted 640 to 528 bytes) and
materials as designed; `test_rt_unsupported` refuses everything on a device
without ray queries. The Metal instance writer
(`rbtrend_rt_instances_col72_comp`) ran for the first time, its records read
by Metal's builds (Carrottus GPU asserts the 72-byte descriptor field by field);
residency is the seal's bottom levels, made resident by Carrottus GPU in one call
per encoder. `test_frame_allocations`' ray-traced frame: none of ours; 133 a
frame of Metal's own without the debug layer (the AGX driver's: 50 in the
builds, its scratch allocator and resource tracker; 18 in the encoders; 18
in blit copies; 17 in the compute sets' residency; 10 in the size queries;
the rest the command buffer and submit). `test_destroyed_handles` 177 of
177. Failing, not ray tracing: the three tests card S27 holds (item 22;
decided 2026-11-01, a; built the same evening, and all three pass).
Measured (Release, no validation layer, `test_rt_measure`, beside the A2000's
D3D12 / Vulkan above): 1% of 100,000 written a frame: the write 16.4 us, the
compile and submit 101.7 us, the frame 4.73 ms; 10%: 111.8 us, 121.6 us,
4.52 ms; 100%: 692.1 us, 76.6 us, 3.13 ms (the GPU's clock falls with the
lighter frames, so the frame time isn't the work's). The test square's BLAS
built in 4.25 ms, 640 bytes, compacted to 528 in 0.96 ms; the Dragon's built in
4.09 ms, 19,236,096 bytes, compacted in 1.45 ms to 10,831,312 (Metal's sizes,
larger than D3D12's and Vulkan's). The path tracer at 1920x1080, 4 samples a
step: 14.69 ms a step, 564.7 million samples a second; a step building the
emitter table again 14.35 ms. Not measured, as on the ThinkPad: the mask
against the cascaded pass at a common size. ThreadSanitizer: every
Rabbittus suite clean (`-tsan`), which runs no GPU work of Rend's.


### 25. Multiple render targets per pipeline

Closed 2026-10-15; run in a caller's macOS host on Metal, and by `../rabbittustests` on Metal, and on D3D12 and Vulkan on Windows (2026-10-16) (see "State of the code"). Carrottus GPU's
pipeline desc has a format and a blend state for each color attachment
(`color_attachment_formats`, `blend_attachments`, `color_attachment_count`)
and Rend's mirrors it (`color_attachment_formats`, `_blends`). A render
pass takes up to `RBTREND_MAX_COLOR_TARGETS` targets. Since 2026-10-22 a
pipeline is refused if its fragment shader doesn't write each target, or
writes past them (item 18).

### 26. Depth bias

Closed 2026-10-15; run in a caller's macOS host on Metal, and by `../rabbittustests` on Metal, and on D3D12 and Vulkan on Windows (2026-10-16). `_depth_bias_constant`,
`_depth_bias_slope` and `_depth_bias_clamp` on the pipeline desc, through
Carrottus GPU on all three backends; a clamp needs a device created with
`CRGPU_FEATURE_DEPTH_BIAS_CLAMP` (item 39).

### 27. MSAA and resolve

(Card A4 below.) No sample count on images or pipelines and no resolve attachment.
`rbtrend_texture2d_create` refuses `sample_count > 1` and `BEGIN_RENDERING`
refuses a `resolve_view` for this reason. Hazel doesn't use MSAA either, so this
is not part of the bar.

**Decided 2026-10-28 with the user (card A4): add it.** A line or edge
under a pixel wide crawls as the camera moves, and post-process AA (FXAA,
TAA) can't recover it and smears in motion; MSAA samples the geometry.
Revit and Rhino use MSAA in their viewports; Unreal offers it in its
forward renderer. Not started; Carrottus GPU's half comes first
(`../carrottus/CRGPU_DEVICE_TODO.md`, "Later").

Rend's API already has the room: the two refusals go.

```c
/* The caller asks what the device takes, then chooses. */
uint32_t mask = 0;
rbtrend_get_format_sample_counts(instance, CRGPU_FORMAT_RGBA16_FLOAT, &mask);
uint32_t samples = (mask & 4) ? 4 : 1;   /* the caller's choice */

RBTREND_TEXTURE2D_DESC msaa_desc = {
    .width = w, .height = h,
    .format = CRGPU_FORMAT_RGBA16_FLOAT,
    .usage = RBTREND_TEXTURE_USAGE_COLOR_TARGET | RBTREND_TEXTURE_USAGE_TRANSIENT,
    .mip_levels = 1, .array_layers = 1,
    .sample_count = samples,              /* refused above 1 today */
};
rbtrend_texture2d msaa_color;
rbtrend_texture2d_create(instance, &msaa_desc, &msaa_color);
/* ...plus the single-sample texture it resolves into, and a multisampled
 *    depth target with the same sample_count... */

RBTREND_COLOR_TARGET target = {
    .view = msaa_color_view,
    .resolve_view = scene_color_view,     /* must be NULL today */
    .load_op = CRGPU_LOAD_OP_CLEAR,
    .store_op = CRGPU_STORE_OP_DONT_CARE, /* samples discarded after resolve */
};

RBTREND_GRAPHICS_PIPELINE_DESC pipe = { /* ... */ .sample_count = samples };
```

- `rbtrend_get_format_sample_counts()` passes Carrottus GPU's query through
  (`crgpu_get_format_properties()`, card A13).
- `sample_count` on `RBTREND_TEXTURE2D_DESC` and a new one on
  `RBTREND_GRAPHICS_PIPELINE_DESC`, which must match the pass's targets;
  `BEGIN_RENDERING` refuses a mismatch.
- `resolve_view` on `RBTREND_COLOR_TARGET` takes the resolve, and the depth
  target gains its own resolve view and mode (one sample, nearest, or
  farthest), for what reads depth after the pass (GTAO, OUTLINE).
- `RBTREND_TEXTURE_USAGE_TRANSIENT`: a target that lives within one pass
  and is never stored, so on tile-based GPUs (Apple's) the samples stay in
  tile memory and MSAA costs little memory or bandwidth.
- Everything after the pass reads the resolved texture and never knows.
  Forward rendering is where it fits; deferred shading with MSAA is not
  planned.
- Tests: an edge's resolved coverage read back against the fraction
  worked out in C, at each supported count, on every backend.

### 28. Stencil state

Stencil load and store ops exist on the depth target, but there is no stencil
test or op state on the pipeline. Hazel leaves stencil off as well.

**Decided 2026-10-28 with the user (card A5): add it.** Not started;
Carrottus GPU's half first (`../carrottus/CRGPU_DEVICE_TODO.md`, "Later").
`RBTREND_GRAPHICS_PIPELINE_DESC` takes stencil state as Carrottus GPU's (compare,
pass, fail and depth-fail ops for front and back faces, read and write
masks), and a command sets the reference value between draws. What it is
for: filling cut faces where a clipping plane or section box slices a
closed mesh (a front- and back-face count marks the cut), outlines from
an exact mask (item 36's OUTLINE; Unreal's custom stencil), decal regions
and irregular clipping. Tests: a capped cut read back against the area
worked out in C, on every backend. The format is the caller's, from what
the device reports (no D24S8 on Apple GPUs).

### 29. GPU timing and debug markers

(Card A15, 2026-10-28: what remains here is deferred; see "Deferred items and the 1.0 room check".)

Closed 2026-10-15; run in a caller's macOS host on Metal, and by `../rabbittustests` on Metal, and on D3D12 and Vulkan on Windows (2026-10-16) (see "State of the code"). Debug labels are
`RBTREND_COMMAND_BEGIN_LABEL` and `END_LABEL`; GPU time is an
`rbtrend_gpu_timer` (`rbtrendtimer.h`) whose slots a pass is timed into, read
`frames_in_flight` frames later. Left:

- **Only whole passes are timed.** Carrottus GPU writes timestamps at a pass's
  start and end, the one place Apple GPUs sample them, so compute
  dispatches and time between passes are in no slot. Timing compute needs
  a compute pass in Carrottus GPU (Metal's compute pass descriptor has the same
  sample buffer attachments), which it doesn't have.
- **No pipeline statistics.** Vulkan and D3D12 have them and Metal's
  counters differ; nothing asks.
- **The Metal conversion is checked for sense only.** GPU ticks are set
  against `sampleTimestamps:` taken at the set's creation and at each read.
  On an M5 Pro it gives 0.08-0.11 ms for `rbtrend_gpu_tests`' pass, inside
  the test's bounds; it hasn't been held against a known cost, nor
  checked on another Apple GPU or over a long span.
- Hazel: `RenderCommandBuffer` (`RT_BeginTimerQuery`, `RT_BeginMarker`).

### 30. Hardware without dynamic rendering

**Dropped 2026-10-28 with the user (card A15)**, unless a device that
needs it is named. Kept for the record:

`CLAUDE.md`, "Rend": Carrottus GPU's plan is a caller-owned `crgpu_framebuffer`,
designed and not built. When it exists, `rbtrend_framebuffer` holds and caches
it.

### 31. Mip generation

Numbered out of order so the other items keep their numbers. The immediate
form is written (2026-10-16; `test_generate_mips` passed on Metal, D3D12 and Vulkan):
`rbtrend_texture2d_generate_mips` renders each level below a base level of
every layer, a pass per level sampling the one above through Rend's own
downsample shader (item 22). Carrottus GPU's blit resamples only on Vulkan, so a
blit-based form would have done nothing on Metal and D3D12. The command
form, `RBTREND_COMMAND_GENERATE_MIPS`, is written (2026-10-16;
`test_generate_mips_command` passed on Metal, D3D12 and Vulkan): the same passes recorded into a command list outside any pass,
through the list's layout tracking, for a texture rendered earlier in the
same list; every level it fills, and the base, ends in
`SHADER_READ_ONLY` for a later pass. The views and sets both forms render
through are kept by the texture (`rbtrend_mip_chain`), one chain per base
level and filter, made by the first generation and released with the
texture, or retired with its image by a resize; a list compiled every
frame makes nothing new. Left:
- **3D textures**: their depth halves too, which a 2D pass can't do; a
  compute shader or a pass per slice of the target pair.
- **Formats**: eleven every backend renders to and filters. 32-bit float
  and snorm formats depend on the device, and Carrottus GPU has no query for
  what a format supports; integer formats can't be filtered, and would
  need a shader that reads texels and averages them itself.
- **Decided 2026-10-28 with the user (card A13):** 3D textures and
  integer formats are generated by a compute shader: each texel of the
  level below is the average of its box in the level above (2x2x2, wider
  at odd sizes, as the 2D path's exact box), read texel by texel, so
  neither filtering nor rendering is needed. Which path a format takes
  (render or compute, or neither) comes from Carrottus GPU's new
  `crgpu_get_format_properties()` in place of the list of eleven;
  `rbtrend_get_format_sample_counts()` (card A4) becomes a pass-through of
  the same query. Tests: each path against the average worked out in C,
  on every backend. Not started.
- **Done: odd sizes** (2026-10-22). A linear generation is an exact box
  over each target texel's footprint (two and a bit source texels on an
  odd side), read with `texelFetch`, so every source texel weighs the
  same; on an even side it is the 2x2 average it was. A nearest one is one
  texel, as before. `test_generate_mips_odd`; run on Metal, and on D3D12
  and Vulkan on 2026-10-23.
- **The first generation in a format makes a pipeline**, and a texture's
  first of a base level and filter its views and sets, inside whatever
  call or compile it is.
- **A chain is kept for the texture's life**: a texture generated from
  several base levels or with both filters keeps one for each. Nothing
  drops one sooner.

Hazel: `Texture2D::GenerateMips`.

### 33. Bundles and bindless: not on every backend

**Decided 2026-10-28 with the user (card A15): bindless on Metal is taken,** through
argument buffers tier 2 (Apple Silicon), in Carrottus GPU's Metal backend first;
then Rend offers bindless on every backend. Bundles stay not planned.

Numbered out of order, as item 31. Carrottus GPU has both on Vulkan and D3D12 and
compiles both out on Metal (`CRGPU_EXT_BUNDLES`,
`CRGPU_EXT_BINDLESS`), so Rend can't offer either as
the same thing everywhere, and offers neither.

- **Bundles: not planned.** A bundle is a second kind of command list,
  recorded ahead and replayed inside a pass. Metal has no such thing, so an
  Rend API for it would be compiled out on one backend and a caller would
  have two ways to draw the same thing. What a bundle is for, recording
  draws once and replaying them, a Rend command list already does: it is
  compiled once and submitted as often as its textures and buffers allow.
  `RBTREND_RENDERING_FLAG_EXECUTES_BUNDLES` and the `_flags` of
  `BEGIN_RENDERING` were removed on 2026-10-10: the
  flag told Carrottus GPU a pass would execute bundles when no command could
  record or execute one. If bundles are wanted after all, the flag comes
  back with the commands that give it something to mean.
- **Bindless: waits on Carrottus GPU's Metal backend.** One table of every texture
  and buffer, indexed from the shader, in place of a binding set for each
  material. It changes what a binding layout is, so it is a design of its
  own, and it can't be the only path while one backend lacks it. Worth
  designing once Metal has it, and with the draw list (item 21), which is
  what would use it.

### 34. Indirect dispatch, a draw count from the GPU, task shaders

**Decided 2026-10-28 with the user (card A15): indirect dispatch is taken,** in Carrottus GPU
first (`../carrottus/CRGPU_DEVICE_TODO.md`, "Later"), then as a Rend
command. A draw count from the GPU and task shaders stay deferred.
Drafted 2026-11-18: `carrottus/crgpu_dispatch_indirect_draft.h` (version 1, seven critique passes), not approved; nothing built. **Approved 2026-11-18 with the user**, every question as recommended.
**Carrottus GPU's half built on the Mac 2026-11-18, compute only:**
`crgpu_command_list_dispatch_indirect()` and `crgpu_dispatch_indirect_args`
(Windows and Fedora owed; `../carrottus/CRGPU_DEVICE_TODO.md`, "Indirect
dispatch"). The mesh call was stopped there: Metal has indirect mesh draws
from Apple9 only, and every Metal adapter reports mesh shading; it waits on
a card. Carrottus GPU's rule on a direct or indirect mesh dispatch is now
each count at most 65535 and their product at most 1024. Rend's command
comes with particles.

Numbered out of order, as item 31. Each is a call or a path Carrottus GPU doesn't
have on every backend:

- **Indirect dispatch**, for compute and for mesh shaders: the group counts
  read from a buffer. Carrottus GPU has the compute call since 2026-11-18;
  the mesh call waits (above).
- **A draw count from the GPU.** `DRAW_INDIRECT`'s `_draw_count` is fixed
  when the command is recorded, so a compute pass that culls can zero an
  entry's instance count and can't shorten the list. Carrottus GPU has no
  count-buffer draw, and its Metal backend loops over the entries on the
  CPU, where a count read from the GPU isn't known.
- **Task shaders.** A mesh shader pipeline has a mesh stage and a fragment
  stage. Carrottus GPU's pipeline desc has a task module, and its Metal backend
  doesn't run one.

### 35. One device allocation for every buffer and image

Numbered out of order, as item 31. Every `crgpu_buffer_create` and
`crgpu_image_create` is a device allocation of its own: a
`vkAllocateMemory` on Vulkan (where many drivers allow about 4096 in all,
`maxMemoryAllocationCount`), a committed resource on D3D12 (64 KiB at the
least, whatever the buffer's size), a `newBufferWithLength` on Metal. Rend
works around it twice, with the vertex and index pools (item 13) and the
material arena (item 19), each suballocating ranges of a few large buffers;
every other buffer and every texture is still an allocation each.

Decided 2026-10-11: **Carrottus GPU exposes heaps and resources placed in them,
and Rend decides placement.** That is translation, and keeps the
decision with Carrottus's caller as `../CLAUDE.md` asks: a heap of a memory
kind, and a buffer or image created at an offset of one, which are
`vkAllocateMemory` and `vkBind*Memory` on Vulkan, `ID3D12Heap` and
`CreatePlacedResource` on D3D12, and `MTLHeap` on Metal, with each
resource's size and alignment requirement reported so the caller can place
it. The alternative, Vulkan Memory Allocator and D3D12 Memory Allocator
inside Carrottus GPU's backends, would be Carrottus deciding where memory goes, and
Metal would need one of its own.

- **Carrottus first**: done 2026-10-11, written and not run on a GPU.
  `crgpu_heap_create` (one memory kind, one resource class: buffers,
  images, attachment images), `crgpu_get_buffer/image_placement_requirements`
  (asked of the heap), `crgpu_buffer_create_placed/image` (ordinary
  handles, destroyed with the ordinary calls). Vulkan and D3D12
  type-checked on Linux, the Vulkan side run against a mock; Metal parsed
  against a stub only. See `../carrottus/CLAUDE.md` for what was settled.
- **Then Rend**: a device allocator over the heaps for every buffer and
  texture, with the placement policy (block sizes, which block a resource
  goes in, when an empty block is released). Its bookkeeping could be VMA's
  virtual allocator (`VmaVirtualBlock`, TLSF, which touches no Vulkan
  object and serves every backend) in place of `rbtrend_range_list`, under
  the pools and the arena as well.
- The pools and the arena stay: they place many ranges in one buffer,
  which is what one view, one barrier per buffer and a material table
  (item 21, item 33) need, and which a device allocator doesn't do.

**Decided 2026-10-28 with the user (card A8): `VmaVirtualBlock`,** vendored
in `.deps` and linked privately (never in a header of Rend's), for the
bookkeeping only; Rend keeps the policy. It replaces `rbtrend_range_list`
under the pools and the arena too (item 19), so one allocator is tested.
First, needing no decision: run Carrottus GPU's heaps on Metal, then D3D12 and
Vulkan. **Done for Carrottus GPU's half** (this said "Not started" until
2026-11-05): `../carrottus`'s `crgpu_objects_test` has made and used
placed buffers and images since 2026-10-30 (a buffer heap and an
attachment-images heap holding its draw's vertices, indices and target,
read back, and `test_heap_desc`), run on D3D12 and Vulkan on Windows
2026-10-30 and on Metal the same day under validation with warnings fatal
(`../carrottus/CLAUDE.md`, "Backend status"), on Linux 2026-11-04, and again
on Windows 2026-11-05 on every adapter (D3D12: Intel Iris Xe, NVIDIA RTX
A2000 and WARP, debug layer silent; Vulkan: both GPUs under
synchronization validation, silent). Rend's allocator over them is not
started.

- **VMA keeps books, it allocates nothing.** A virtual block is a number
  line `[0, size)` with alignment; it touches no GPU object and has never
  heard of a heap. VMA's full allocator (`VmaAllocator`), which does
  allocate Vulkan memory, is not used. What ties a virtual block to a
  `crgpu_heap` is Rend's own `rbtrend_device_block`, holding one of
  each: an offset VMA hands out is an offset into that heap because Rend
  passes it to `crgpu_create_placed_*` with that heap.
- **VMA's header includes Vulkan's** (for `VkResult` and the like) even for
  virtual blocks. Metal and D3D12 builds vendor Vulkan-Headers (header
  only, seen by the adapter's one file) and compile VMA with `VMA_STATIC_VULKAN_FUNCTIONS 0` and
  `VMA_DYNAMIC_VULKAN_FUNCTIONS 0`, so nothing calls Vulkan. The
  alternative, a TLSF of our own (about 400 lines), is kept in reserve.
- **Blocks need stable storage.** Placements hold a block's address, so
  a container that moves its elements on growth (`std::vector`) is wrong:
  `std::deque` (whose `push_back` keeps references), indices, or a chunked
  list (eqstd's, when it exists).
- **VMA stays behind one file** (asked by the user, 2026-10-28). An
  internal adapter, `rbtrend_suballocator` - create over a size, allocate a
  size and alignment to an offset and a handle, free a handle, ask whether
  it is empty - is the only thing that includes `vk_mem_alloc.h`, in its one
  `.cpp`. The rest of Rend sees Rend types only, never `VkResult` or a
  VMA handle, and a TLSF of our own would replace one file.
- **A block owns its heap and its books together.** Creating a block makes
  both, destroying it destroys both, and nothing outside it touches either
  alone, so a heap without its books, or the reverse, can't happen. The
  pairing is VMA's own design: its `VmaDeviceMemoryBlock` is a
  `VkDeviceMemory` with the same metadata a virtual block exposes, and
  D3D12MA pairs an `ID3D12Heap` the same way; Rend's block has a
  `crgpu_heap` in that place. It lives in Rend because placement is
  policy, which Carrottus never holds (2026-10-11).

The sketch, illustrative (Rend's names and `RBT_RESULT` codes invented;
Carrottus GPU's and VMA's calls real):

```cpp
// src/render/rbtrendsuballocator_internal.hpp (sketch)
//
// Which bytes of a run [0, size) are taken, with alignment. Knows nothing of
// GPUs or heaps. VMA's virtual block underneath, included only by
// rbtrendsuballocator.cpp.

struct rbtrend_suballocation {
    uint64_t offset = 0;
    uint64_t handle = 0;          // VMA's allocation, opaque here; 0 is none
};

class rbtrend_suballocator {
public:
    rbtrend_suballocator() = default;
    ~rbtrend_suballocator();                                    // frees the books
    rbtrend_suballocator(const rbtrend_suballocator &) = delete;
    rbtrend_suballocator &operator=(const rbtrend_suballocator &) = delete;

    RBT_RESULT create(uint64_t size);
    bool     allocate(uint64_t size, uint64_t alignment, rbtrend_suballocation *out);
    void     free(rbtrend_suballocation range);
    bool     is_empty() const;

private:
    void *books_ = nullptr;       // VmaVirtualBlock, opaque outside the .cpp
};
```

```cpp
// src/render/rbtrendsuballocator.cpp (sketch): the one file that sees VMA

#define VMA_STATIC_VULKAN_FUNCTIONS 0     // nothing here calls Vulkan
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"                 // .deps, with Vulkan-Headers on every backend
#include "rbtrendsuballocator_internal.hpp"

RBT_RESULT rbtrend_suballocator::create(uint64_t size) {
    VmaVirtualBlockCreateInfo info{};
    info.size = size;                     // TLSF over [0, size)
    VmaVirtualBlock block = VK_NULL_HANDLE;
    if (vmaCreateVirtualBlock(&info, &block) != VK_SUCCESS)
        return RBT_RESULT_OUT_OF_MEMORY;
    books_ = block;
    return RBT_RESULT_SUCCESS;
}

bool rbtrend_suballocator::allocate(uint64_t size, uint64_t alignment,
                                   rbtrend_suballocation *out) {
    VmaVirtualAllocationCreateInfo info{};
    info.size = size;
    info.alignment = alignment;
    VmaVirtualAllocation range = VK_NULL_HANDLE;
    if (vmaVirtualAllocate((VmaVirtualBlock)books_, &info, &range, &out->offset) != VK_SUCCESS)
        return false;
    out->handle = (uint64_t)(uintptr_t)range;
    return true;
}

void rbtrend_suballocator::free(rbtrend_suballocation r) {
    vmaVirtualFree((VmaVirtualBlock)books_, (VmaVirtualAllocation)(uintptr_t)r.handle);
}

bool rbtrend_suballocator::is_empty() const {
    return vmaIsVirtualBlockEmpty((VmaVirtualBlock)books_) == VK_TRUE;
}

rbtrend_suballocator::~rbtrend_suballocator() {
    if (books_)
        vmaDestroyVirtualBlock((VmaVirtualBlock)books_);
}
```

```cpp
// src/render/rbtrenddeviceallocator_internal.hpp (sketch)
//
// Where every buffer and texture lives: blocks of device memory, each a
// Carrottus GPU heap with a suballocator keeping its books. The policy (block
// size, which block, when a block goes) is here, in Rend.

#include <crgpu.h>
#include <deque>
#include "rbtrendsuballocator_internal.hpp"

// A block owns its heap and its books: made together, gone together.
class rbtrend_device_block {
public:
    static RBT_RESULT create(crgpu_device device, CRGPU_MEMORY_KIND kind,
                           CRGPU_HEAP_RESOURCE_CLASS resource_class,
                           uint64_t size, rbtrend_device_block *out) {
        crgpu_heap_desc hd{};
        hd.base.type      = CR_STRUCT_TYPE_GPU_HEAP_DESC;
        hd.memory_kind    = kind;
        hd.resource_class = resource_class;
        hd.size           = size;
        hd.debug_name     = "rbtrend device block";
        if (crgpu_heap_create(device, &hd, &out->heap_) != CR_RESULT_SUCCESS)
            return RBT_RESULT_OUT_OF_DEVICE_MEMORY;
        RBT_RESULT r = out->books_.create(size);
        if (r != RBT_RESULT_SUCCESS) {
            crgpu_heap_destroy(out->heap_);
            out->heap_ = nullptr;
        }
        return r;
    }
    ~rbtrend_device_block() { if (heap_) crgpu_heap_destroy(heap_); }   // books go with it

    crgpu_heap heap() const { return heap_; }
    bool take(const crgpu_placement_requirements &req, rbtrend_suballocation *out) {
        return books_.allocate(req.size, req.alignment, out);   // alignment from Carrottus GPU
    }
    void give_back(rbtrend_suballocation r) { books_.free(r); }
    bool is_empty() const { return books_.is_empty(); }

private:
    crgpu_heap        heap_ = nullptr;
    rbtrend_suballocator books_;
};

struct rbtrend_device_placement {
    rbtrend_device_block  *block = nullptr;
    rbtrend_suballocation  range{};
};

// One list of blocks per (memory kind, resource class), as Carrottus GPU's heaps
// require: buffers, images, and attachment images apart.
struct rbtrend_device_pool {
    CRGPU_MEMORY_KIND              memory_kind;
    CRGPU_HEAP_RESOURCE_CLASS      resource_class;
    std::deque<rbtrend_device_block>  blocks;   // push_back keeps every block's address
};

static constexpr uint64_t RBTREND_BLOCK_BYTES = 64ull << 20;   // to be measured

static uint64_t block_size_for(uint64_t need) {
    // Big resources get a block of their own; the rest share 64 MiB blocks.
    uint64_t size = need > RBTREND_BLOCK_BYTES / 2 ? need : RBTREND_BLOCK_BYTES;
    uint64_t g = CRGPU_HEAP_SIZE_GRANULARITY;
    return (size + g - 1) / g * g;
}

// The first block with room, else a new block.
static RBT_RESULT place(crgpu_device device, rbtrend_device_pool &pool,
                      const crgpu_placement_requirements &req,
                      rbtrend_device_placement *out) {
    for (rbtrend_device_block &b : pool.blocks)
        if (b.take(req, &out->range)) {
            out->block = &b;
            return RBT_RESULT_SUCCESS;
        }

    rbtrend_device_block &fresh = pool.blocks.emplace_back();
    RBT_RESULT r = rbtrend_device_block::create(device, pool.memory_kind, pool.resource_class,
                                             block_size_for(req.size), &fresh);
    if (r != RBT_RESULT_SUCCESS) {
        pool.blocks.pop_back();
        return r;
    }
    if (!fresh.take(req, &out->range))
        return RBT_RESULT_OUT_OF_DEVICE_MEMORY;   // alignment beyond a block
    out->block = &fresh;
    return RBT_RESULT_SUCCESS;
}

RBT_RESULT rbtrend_device_create_image(rbtrend_device_allocator *a,
                                    const crgpu_image_desc *desc,
                                    crgpu_image *out_image,
                                    rbtrend_device_placement *out_placement) {
    bool attachment = (desc->usage & (CRGPU_IMAGE_USAGE_COLOR_ATTACHMENT |
                                      CRGPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT)) != 0;
    rbtrend_device_pool &pool = attachment ? a->attachment_images : a->images;

    // Requirements are asked of a heap of the class, so a pool keeps one
    // block to ask (made on first use).
    rbtrend_device_block *probe = nullptr;
    RBT_RESULT r = a->first_block(pool, &probe);
    if (r != RBT_RESULT_SUCCESS) return r;

    crgpu_placement_requirements req{};
    if (crgpu_heap_get_image_placement_requirements(probe->heap(), desc, &req) != CR_RESULT_SUCCESS)
        return RBT_RESULT_INVALID_ARGUMENT;

    r = place(a->device, pool, req, out_placement);
    if (r != RBT_RESULT_SUCCESS) return r;

    if (crgpu_image_create_placed(out_placement->block->heap(), out_placement->range.offset,
                                    desc, out_image) != CR_RESULT_SUCCESS) {
        out_placement->block->give_back(out_placement->range);
        return RBT_RESULT_DEVICE_FAILURE;
    }
    return RBT_RESULT_SUCCESS;
}
// A buffer: the same, with crgpu_heap_get_buffer_placement_requirements() and
// crgpu_buffer_create_placed(), in the pool of its memory kind.

// The GPU may still read it in a frame in flight: the release waits on the
// release queue (item 13) for that frame's fence.
void rbtrend_device_release_image(rbtrend_device_allocator *a, crgpu_image image,
                                 rbtrend_device_placement p) {
    a->release_queue.push(a->current_frame_fence, [=] {
        crgpu_image_destroy(image);
        p.block->give_back(p.range);
        // An empty block is kept one more cycle, then it goes (heap and books
        // together), except the pool's last, kept to answer requirement queries.
        if (p.block->is_empty())
            a->mark_block_idle(p.block);
    });
}
```

### 39. A device has only the features its caller asked for

Numbered out of order, as item 31: written as item 37 on 2026-10-21, a
number already taken by the command allocator. Closed 2026-10-21, on
Carrottus GPU's adapter and device split (`../carrottus/CRGPU_DEVICE_TODO.md`).
Builds on macOS over Metal with no warnings and the repo's headless tests
pass. Run on Metal (Apple M5 Pro) on 2026-10-21 by `../rabbittustests`,
converted the same day, which runs every test twice: on a device with
these optional features and the tests' (0x7f of the adapter's 0xeff), and
on one with none, the first run of the staged storage buffers and of an
indirect draw split a call a draw on Metal; its `test_feature_checks` saw
each group 2 call made with its feature and refused without. All passed,
Debug under Metal validation and `leaks`, and Release. Built and run on
Windows 11 on 2026-10-22 (Debug, D3D12 and Vulkan, Intel Iris Xe and NVIDIA
RTX A2000): every check passed on both devices of every run, the first
devices 0x7f of 0x1dff (Iris Xe) and 0x3f of 0x1fbf (RTX A2000), with no
debug layer or validation layer message, none under Vulkan's
synchronization validation either. Not built on Linux.

A `crgpu_device` has exactly the `CRGPU_FEATURE_*` bits it was created
with, whatever its adapter supports. The device stays the caller's: Rend
is handed it and never creates it (the caller keeps the queue and feature
choices for its own needs, and two instances may share one device,
`rbtrendcontext.h`). So Rend says what it can use, in two groups:

| Feature | Use | Group |
|---|---|---|
| `MULTI_DRAW_INDIRECT` | an indirect draw command is one call, not split to the adapter's `max_draw_indirect_count` | 1 |
| `DEVICE_MAPPABLE_MEMORY` | storage buffers written directly, else staged | 1 |
| `PRESENT` | `rbtrend_swap_chain_create` | 2 |
| `PASS_TIMESTAMPS` | `rbtrend_gpu_timer_create` | 2 |
| `MESH_SHADING` | a mesh shader, and so a pipeline with `mesh_shader` | 2 |
| `LAYERED_RENDERING` | 2D array views rendered to | 2 |
| `CUBE_ARRAY_VIEWS` | `RBTREND_TEXTURE_VIEW_SHAPE_CUBE_ARRAY` | 2 |
| `DEPTH_BIAS_CLAMP`, `INDEPENDENT_BLEND` | a pipeline's `_depth_bias_clamp`, differing `_blends` | 2 |

What was done:

- **Group 1, the features only Rend's own code uses:**
  `rbtrend_get_optional_gpu_features()` (`rbtrendcontext.h`) returns them, a
  function so the value is the linked library's. The caller asks for
  `rbtrend_get_optional_gpu_features() & supported_features` when it
  creates the device; without them Rend takes the other path.
  `rbtrendfeatures_tests` (C, headless) checks the set.
- **Group 2, features one of Rend's calls needs:** each call names its
  feature in its header comment and checks the device itself, refusing in
  Rend's way (NULL after printing why) before Carrottus GPU sees the call:
  the swap chain, the GPU timer, a mesh shader (since 2026-10-23; a mesh
  pipeline before), a depth bias clamp,
  differing blends (by Carrottus GPU's rule: entries that don't blend are equal),
  a 2D array view rendered to, a cube array view. The caller asks for each
  of the calls it makes.
- **Rend reads the device, not the adapter, for features.**
  `rbtrend_instance_create` keeps `crgpu_device_get_features(device)` in
  the instance (`rbtrend_instance_t::features`), and every check reads
  that. Properties and limits come from
  `crgpu_adapter_get_properties(crgpu_device_get_adapter(device))`:
  `unified_memory`, the two buffer offset alignments, and
  `max_draw_indirect_count`, which counts only on a device with
  `MULTI_DRAW_INDIRECT` and is 1 without. `layered_rendering`, the
  instance's copy of the old property, is gone.
- **Confirmed: no internal pass needs a group 2 feature.** Rend's own
  views (mip generation, the lit placeholders, bloom and post) render to
  2D views and read 2D, 2D array and cube views: no layered pass and no
  cube array view, so group 1 stays the two features. No Rend shader
  writes `gl_Layer` or uses `nonuniformEXT` (the lit shaders' unsized
  `tile_lights[]` is a storage buffer's last member, core SPIR-V), so none
  needs a feature switched on behind one of these bits either.
- The instance's command allocators are direct, so the device needs a
  direct queue; `rbtrendcontext.h` says so, and Carrottus GPU refuses the
  allocators otherwise.

`../rabbittustests` on D3D12 and Vulkan, the first run of each fallback off
Metal (and on D3D12 of a device without device-mappable memory), is done
(2026-10-22, above). Mesh shading and the swap chain's refusals are covered
since 2026-10-22 (`test_feature_checks`, and a mesh pipeline drawn in
`test_mesh_shading`), run on Metal, and on D3D12 and Vulkan on 2026-10-23
(Windows 11, Debug, Iris Xe and RTX A2000, first devices 0x7f and 0x23f).
That run found two things on Vulkan, both fixed:

- **A mesh shader was made on a device without mesh shading.**
  `rbtrend_shader_create_spirv` passed it to Carrottus GPU, whose Vulkan backend
  made the `VkShaderModule`; its SPIR-V declares `MeshShadingEXT`, which
  needs `VK_EXT_mesh_shader` enabled even for a module (two validation
  errors for the capability and extension, two for the device's mesh
  output limits of 0). D3D12 and Metal
  cross-compile with no device and took it. Now
  `rbtrend_shader_create_spirv` and `_read_from_file_spirv` refuse a mesh
  shader without the feature, printing, and `crgpu_shader_module_create`
  refuses a mesh or task module with
  `CR_RESULT_GPU_EXT_MESH_SHADING_NOT_SUPPORTED` on every backend. The
  pipeline check stays, now unreachable through a shader of the same
  device. `test_feature_checks` expects the shader refused, not the
  pipeline.
- **A layered pass's vertex shader wasn't valid on Vulkan.** `gl_Layer`
  in a vertex shader compiled to SPIR-V 1.0 (glslc's default) is the
  `ShaderViewportIndexLayerEXT` capability, which needs
  `VK_EXT_shader_viewport_index_layer`; `shaderOutputLayer` alone covers
  only SPIR-V 1.5's `ShaderLayer`. Carrottus GPU's Vulkan adapter now has
  `CRGPU_FEATURE_LAYERED_RENDERING` only with the extension and
  `shaderOutputViewportIndex` as well (the extension needs both on), and
  the device enables all three. Both GPUs here keep the bit.

Rerun, every check passed with no message, plain and under
synchronization validation. Left: Linux, where it is not built.

### 50. A per-vertex color for lit surfaces (card A46)

**Decided 2026-11-01 with you (card A46, a):** Rend's lit surfaces read
an optional per-vertex color, linear RGBA, multiplied into the base color
as glTF 2.0's `COLOR_0` is (core spec: the base color factor times the
base color texture times `COLOR_0`). It is a second vertex stream beside
`RBTREND_LIT_VERTEX`, which doesn't change (a frozen 48 bytes, and every
BLAS vertex today, A42), and a surface vertex shader that reads it; a mesh
without one draws as today. Raised by a caller whose meshes carry
vertex colors that its lit surfaces must keep (the workspace's
`DECISIONS.md`, card A46); general purpose, as any glTF asset may have
them.

Its public half is a draft for your approval first. What the draft
settles: the stream's binding (slot 2, after the instance at 1) and
format (four floats, or RGBA8/RGBA16 normalized as glTF allows); one
vertex shader that reads it with a neutral white bound for meshes
without, or a second shader; the depth and shadow passes (which need no
color, but a cut-out reads opacity, which the color's alpha multiplies);
and what traced passes (item 24) read at a hit, since the arena holds
`RBTREND_LIT_VERTEX` alone: none until A43's c (the caller's vertex
streams, with bindless), or a color in the arena.

**Built 2026-11-01 on macOS from the approved `rbtrend_litcolor_draft.h`,
as written** (Apple M5 Pro, Metal): RBTREND_LIT_SHADER_SURFACE_VERTEX_COLOR
(6) and RBTREND_LIT_SHADER_SHADOW_VERTEX_COLOR (7)
(`shaders/rbtrend_lit_color_vert.slang`, `rbtrend_lit_shadow_color_vert.slang`),
`rbtrend_get_lit_vertex_color_layout()`; every lit vertex shader writes location
4 (white where there is no color), the surface and the cut-out depth
shader read it; rays read none. Ran: `rbtrend_contract_tests`' layout case;
rabbittustests on Metal, Debug and Release, under validation:
`test_lit_vertex_color` (the square's corners red, green, blue and white
under a white sky: each texel within 3 of (white - black) x color + black,
0 off measured, since the color multiplies the base color and not a
dielectric's specular part; the centre the diagonal's blend, the square
being two triangles; a base red of 0.5 halving red, 115 against 114.6; a
cut-out by the color's alpha: the surface shown only right of x -1/3 and
its shadow, through the colored shadow shader, under that part only),
lit_test_vert writing location 4 with test_lit_surface's readings
unchanged, a BLAS over a colored mesh tracing the same bits as the
uncolored one, and `test_frame_allocations` with a colored square in the
frame (none of Rend's). D3D12 and Vulkan not built.

### 51. A transient span's view is made again whenever its offset moves (found 2026-11-15, not carded)

`rbtrend_transient_span_view` (`src/render/rbtrendtransient.cpp`) keeps a
chunk's views by the exact offset and stride each span landed at, and the
rewind at a frame's turn destroys every view that turn didn't ask for. A
draw list whose instance count changes from frame to frame moves the
offset of every list built after it, so each of those makes a new
`crgpu_buffer_view` that frame: an allocation of ours in a steady state.
Found by a caller's shadow views, whose casters change as its cascades
follow the camera (its noalloc test); the same holds for any caller whose
visible counts change (a flying camera's culled list), which no test had
covered. Kept unused for 120 turns, as the render target pool keeps a
size, wasn't enough: a moving camera keeps bringing new offsets. The fix:
one view per chunk and stride at offset 0, the instance span's offset
padded to a multiple of its stride and handed to the draw as its first
instance (and a vertex span's as its base vertex), so the view never
depends on where a span lands. Every backend's draws take a first
instance; the draw list's and the commands' compile change, and
`rabbittustests`' `test_frame_allocations` gains a case of a list whose
count changes each frame. For the user: card it, or say go.

### 52. Atmospheric scattering round a sphere (card L62, decided 2026-11-16)

**Stage 4 decided 2026-11-17 with you (card A55), every question as
recommended:** `rbtrend_atmosphere_space_draft.h` approved:
`RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS` (51) over an array of frozen
shells, a sky-view size of 0 meaning no sky, coloured transmittance by
dual-source blending (waiting on Carrottus GPU's card S50), a shell's
lights by the scene's index (a placed directional light's direction at
the shell's centre, item 58), the ground lit through its air from outside.
Built in a session of its own, Rend first, measured in Release at 1080p
and 1440p.

Decided 2026-11-16 with the user: a, Hillaire's model ("A Scalable and
Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020), the
one Unreal's SkyAtmosphere uses, for any caller. What Rend takes: a
medium in the shell between two radii round a centre (Rayleigh, Mie and an absorbing layer's coefficients and heights),
a sun's direction and colour; what it gives: the transmittance and
multiple-scattering tables (rebuilt when the medium changes), a sky-view
table a frame (the sky from wherever the camera is, inside or outside the
shell), and an aerial-perspective volume over the frustum that lit
surfaces read. No planet, world or game word: the caller says what the
sphere is. Staged after the caller's own first stage (its sky's up
direction): 2) the tables and the sky from inside, 3) aerial
perspective, 4) the shell seen from space, blended (after transparency). A draft of its own for the
public commands, approved before it is built; Hazel has no atmosphere to
compare (its sky is Preetham's), so the comparison is Unreal's and
Hillaire's paper and sample. Tests on three backends: the tables against
the paper's reference values, a sunset's colour at the horizon, the
transmittance through the shell from space.

Drafted and approved 2026-11-16 (`rbtrend_atmosphere_draft.h`, with its
caller's own half beside the caller's headers; two rounds of seven
critiques). The user drew the line (B3): the medium and the questions a
caller asks of it with no picture in them are Rabbittus Math's
(`math/rbtmathmedium.h`, `RBTMATH_SHELL_MEDIUM`: density, transmittance
along a ray, the two phase functions; built 2026-11-16 on macOS with
contraction off and an exponential of its own, `rbtmathmedium_tests`
passing: brute force within 1e-4, the zenith's closed form, the phases
integrating to 1, 10,000 rays the same bytes alone and on 1 and 4
threads, CRC-32 7d88a849 on the M5 Pro for other platforms to match;
since 2026-11-18 its exponential is `rbtmathfunctions.h`'s exp, card
A61, and the CRC is still 7d88a849: the medium writes floats, and no
float moved, over the 10,000 rays nor over 10^8 arguments in [-30, 0]
compared alone; rabbittustests' atmosphere figures, Debug and Release
under Metal's validation, are the ones recorded below, Rabbittus Math's
0.887, 0.550, 0.847, 0.622, (0.925, 0.870, 0.745) and (0.792, 0.659,
0.415) among them, and nothing was re-recorded; `RBTMATH_TODO.md`, A61);
Rend's atmosphere takes that struct and draws it. Stage 4 (the shell
from space) needs a blended full-screen march of Rend's own, not L7's
transparency, so it moves before L7 as its own session (B4).

**Stages 2 and 3 built 2026-11-16 on macOS** (Metal, the M5 Pro; D3D12
and Vulkan never built): `render/rbtrendatmosphere.h` (the atmosphere: its medium,
its transmittance and multiple-scattering tables, RBTREND_COMMAND_ATMOSPHERE_TABLES),
`rbtrend_lit_scene_set_atmosphere` (rbtrendlit.h), and
RBTREND_COMMAND_LIT_ATMOSPHERE (a scene's sky-view table and, where it
has one, its aerial volume, a frame) and _ENVIRONMENT (the environment
filled from the sky-view). As built, against the draft:
- **One binding, an array.** Carrottus GPU holds at most 16 bindings a set
  (CRGPU_MAX_DESCRIPTOR_BINDINGS_PER_SET) and the lit scene's set 0 had
  15, so the scene's tables are one 2D array, `atmosphere_tables`
  (binding 15): layer 0 the sky-view, layer 1 the atmosphere's
  transmittance table copied texel for texel (`rbtrend_atmosphere_copy_frag`),
  layers 2 on the aerial volume's slices, each table in its layer's top
  left (read with its uv scaled and clamped). A set 0 binding added: a
  break for any caller's own lit shader, listed with the ABI's.
  7.2 MiB at the defaults (256 x 108 by 34 layers of half floats);
  13.9 MiB (66 layers) since card A64, below.
- **Fragment passes, not compute:** every table is a full-screen pass of
  Rend's own (no 3D storage image is asked of Carrottus GPU), so nothing is
  UNSUPPORTED anywhere. The aerial volume is a pass a slice: 32 passes at
  the defaults, which is its cost (below); a compute pass over a 3D
  storage image would be one, where Carrottus offers them.
- **The sky-view's seam** sits opposite the first sun (its bearing the
  table's middle column), where the sky is smoothest.
- **A frame's lag:** a scene turns its atmosphere (and its volume) on from
  the frame after the first fill, its block committed then.
- **Under the ground sphere:** an eye there is taken just above it; a
  point of the aerial volume's march under it is read at it (the sky-view's
  rays end at the ground, so only the volume's reach it). Found by a
  caller's test under a perspective view: the volume's middle row is half a texel under a level
  view, its ray met the ground sphere 13 m out from 0.4 m up, and every
  surface past it read 13 m of air; a surface drawn below that sphere
  (ground over it, anything on it) is in the densest air, not in none. The
  volume's march stops only where its ray leaves the shell.
- **Owed:** the path tracer and the traced reflections take no
  atmosphere's transmittance (their suns reach surfaces undimmed); the
  volume keeps the mean of the three channels' transmittance (the
  paper's), so a bright emissive surface far off is dimmed, not
  reddened (done: card A64, below); stage 4 (the shell from space).
Tests (`rabbittustests`, under Metal's validation with warnings fatal):
`test_atmosphere_refusals`, `_sun_through_air` (a face's light against
Rabbittus Math's transmittance: 0.889 / 0.887 at 30 degrees, 0.556 /
0.550 at 5), `_sky` (the zenith (31, 68, 164), the horizon (43, 53, 59),
a sunset towards the sun (124, 22, 2), darker as the eye climbs, black
from 200 km), `_environment` (the fill's zenith within 1/255 of the drawn),
`_aerial` (a face 8 and 24 km away: the volume's transmittance 0.844 and
0.621, Rabbittus Math's 0.847 and 0.622; the air's light bluer and
brighter further off), `_measure`, and `test_frame_allocations` (a frame
with the sky-view and the volume: 0 of ours). Release, the M5 Pro, submit
to idle: the tables 1.10 ms; the sky-view and its copy 0.24 to 0.26 ms;
with the aerial volume (32 x 32 x 32, a step a slice) 0.94 ms; the
environment's fill from it 4.2 ms (LIT_SKY's own fill's cost).

**Stage 4 amended by the third critique round, 2026-11-17 (the draft's
version 9, approval kept):**
- A shell's lights are `DIRECTIONAL` or `DIRECTIONAL_PLACED` (item 58)
  only; point and spot lights are refused for shells (they fade at range
  and are tile-culled), narrowing Q2a's wording as item 58 reads it. At
  most `RBTREND_LIT_ATMOSPHERE_MAX_SHELL_LIGHTS` a shell; a placed light
  whose place is inside a shell's top sphere refused when compiled.
- The turn-on fix split in two internal flags: ATMOSPHERE (set after the
  first transmittance copy) and SKY (set after the first non-zero
  sky-view fill, gating the lit atmosphere's sky and the sun disc's
  dimming); the sky-view pass skipped at size 0, and a scene with no sky
  keeps an adequate held array (the scene's array remade only on a size
  change).
- The pass's layout as the reflection pass's (LIT_REFLECT's): set 0 the
  scene's own, set 1 the medium's values, set 2 the depth, transmittance
  and multiple-scattering views; a shell's values 96 bytes of push
  constants. `atmosphere_march` (in `rbtrend_atmosphere_pass.slang`) takes
  the suns as arguments. The ray-sphere roots as min and max of q and c/q,
  the entry rescaled to the top sphere.
- The blend: colour `X * ONE + dst * SRC1_COLOR`, alpha `ONE, SRC1_ALPHA`,
  one colour attachment (Carrottus GPU's S50 bit 19 and its limit); dual
  source named in `rbtrend_get_optional_gpu_features()`, a scene with
  shells and no feature refused.
- Tests: a placed light (once item 58 is built; point and spot refused),
  the turn-on and the array made once, no blend feature refused. The
  build: S50 first, then Metal.

**Stage 4 built 2026-11-17 on the Mac** (Metal, the M5 Pro, built and
run; D3D12 and Vulkan never built: written for every backend through
Carrottus GPU, no backend branch in Rend; card A55, the approved
`rbtrend_atmosphere_space_draft.h` version 9, over card S50's dual-source
blending). As built:
- `RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS` (51), its 48 bytes of the 120;
  the frozen 32-byte `RBTREND_LIT_ATMOSPHERE_SHELL` (`rbtrendlit.h`,
  `RBT_PLAIN_DATA` in `src/`), `RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS`
  (128) and `_MAX_SHELL_LIGHTS` (2). Its send checks one each, in the
  members' order then each shell's (its index named); the shells copied
  into the list's side storage and sorted there at the compile by the
  eye's power against each top sphere (insertion, stable). At the compile
  the lights again, a shell naming the scene's own atmosphere while it
  draws a sky refused, and `RBT_RESULT_UNSUPPORTED` on a device without
  `CRGPU_FEATURE_DUAL_SOURCE_BLEND`, now in
  `rbtrend_get_optional_gpu_features()`. A shell whose atmosphere has no
  medium or no tables is skipped (no report: Rend has no Debug report to
  give one, unlike what the draft says).
- `RBTREND_LIT_ATMOSPHERE`'s sky-view sizes both 0 mean no sky ([CHANGED]):
  one 0 alone, and a volume without a sky-view, each refused on its own;
  an atmosphere with no sky keeps any array the scene holds that is as
  large. The turn-on as two flags: `RBTREND_LIT_SCENE_FLAG_ATMOSPHERE` once
  LIT_ATMOSPHERE has copied the transmittance (internal
  `transmittance_copied`), a new `RBTREND_LIT_SCENE_FLAG_SKY` (2048) once
  a sky's sky-view is filled, gating the sky shader's sky and its discs'
  dimming; LIT_ATMOSPHERE skips the sky-view pass with no sky.
- The pass (`shaders/rbtrend_atmosphere_shell_frag.slang`): set 0 the
  scene's own set, set 1 the atmosphere's block (the atmosphere passes'
  layout), set 2 the depth and the two tables from the view-set cache, a
  shell's values 96 bytes of push constants; one pass over the target for
  every shell, a draw each in the scissor of its top sphere's projected
  bounds (the conic of its dual quadric, from the view-projection alone);
  blend colour ONE / SRC1_COLOR, alpha ONE / SRC1_ALPHA. The stable
  ray-sphere roots (`sphere_hits_stable`), the entry put back on the top
  sphere; the ray's direction taken homogeneously, so an infinite far
  plane works. `atmosphere_march` takes the suns and the tables' sizes as
  arguments; the set-0 block moved to `rbtrend_atmosphere_pass_block.slang`
  (the shell pass's set 0 is the scene's).
- Found and fixed: the ground's light in the sky-view and the
  multiple-scattering table read the transmittance through
  `transmittance_towards` from a point on the ground, whose ground test
  then turned on its rounding: the sky-view's ground was speckled and the
  multiple-scattering table low. Now `ground_transmittance` reads the table
  at the ground's radius. `test_atmosphere_sky`'s zenith is (37, 78, 183)
  and its horizon (55, 66, 72) since (were 31, 68, 164 and 43, 53, 59).
- Found, not fixed: a scene's block is staged at the submit, so the whole
  submission that holds a scene's first LIT_ATMOSPHERE sees its flags on,
  a draw recorded before that command included (it would read layers not
  yet written); "a frame's lag" above is not what the code does. The
  sky shader (and the aerial pass) bring depth 0 back through the inverse,
  which an infinite far plane sends to infinity (NaN) (fixed: card A66,
  below). The view-set cache
  holds 16 sets for every command: more distinct shells' atmospheres in
  one list than it holds would evict a set the list uses (fixed: card
  A67, item 63; the approved draft's "a steady frame makes none" held
  only to 16 sets until then).
- ~~Pending: a shell lit by a placed directional light, once item 58 is
  built (`test_atmosphere_shell_placed_light` reports it pending).~~ Built
  with item 58 (2026-11-19, the Mac): a shell takes a placed light's
  direction and illuminance at its centre, and a placed light whose
  source is inside or on its top sphere is refused at the send and the
  compile (`rbtrend_shell_lights_ok`); the test turned on.
- ABI (`./check.sh --update-abi`): additions only, no break: command 51,
  its parameters, the shell struct, the two macros.
Tests (`rabbittustests`, Metal's validation with warnings fatal, Debug
and Release, the device with the features and the bare one, all passing;
each seen failing first): `test_atmosphere_shell_refusals` (every check,
each its message), `_transmittance` (from 3 radii, the GPU's T against
Rabbittus Math's within 0.15%: 0.9404 / 0.9405 vertical), `_crossing`
(rows within 0.70% at a 1024 x 512 sky-view; 11.8% at 192 x 108 in the
rows just above the horizon), `_whole_world`, `_depth`, `_order` (0.079%
against hand composition), `_inside` (0.169% against section 4b; see
the note under A65 below),
`_far` (the ground's edge within half a texel at 1e7 and 1e9 off, no
speckle; metres against kilometres 0.098%), `_placed_light` (point and
spot refused; the placed light since item 58, 2026-11-19),
`test_atmosphere_no_sky`,
`test_atmosphere_shell_unsupported`, `test_frame_allocations` (a scene
switched between a sky and none every frame and two shells: 0 of ours),
and `rbtrend_contract_tests`, `rbtrendfeatures_tests` headless.
Measured (Release, no validation, submit to idle less the same frame with
no shell, means of 50 after 10, the 150 km world with a 30 km top, 4 to
32 steps, 60 degrees high; `test_atmosphere_shell_measure`): at 1440p the
world whole from 4 radii (13% of the texels) 0.42 to 0.49 ms (estimate
0.1 to 0.2), half the view from low orbit 1.04 to 1.13 ms (0.5 to 0.7),
the view filled from 100 km up 2.42 to 2.46 ms, 1.41 to 1.53 ms at 4 to
16 steps (budget 1.5 ms: over at 32), eight small worlds 0.18 to 0.19 ms;
at 1080p 0.29 to 0.30, 0.66 to 0.70 (0.3 to 0.4), 1.41 to 1.46, 0.14; the
eye inside, every texel, 16 steps 2.9 ms at 1440p and 1.65 ms at 1080p,
32 steps 5.5 ms and 3.14 ms (estimate 4 and 2.3). Over the budget: the
steps first, then a half-resolution march (the draft's section 7).

**Card A64, decided 2026-11-17 by you (a): the aerial volume keeps its
transmittance per channel.** Built 2026-11-17 on the Mac (Metal, the M5
Pro, built and run; D3D12 and Vulkan never built: no backend branch, the
two colour targets through Carrottus GPU's dynamic rendering). Stage 4's
shell pass dims each channel and the volume kept the mean, so a distant
surface stepped in colour as the eye rose through a world's top (a
caller's crossing test: 13.8%, the sky within 0.86%). As built, no
public declaration changed (the ABI snapshot unchanged):
- **The layout:** the scene's array is 2 + 2D layers for a volume of D
  slices: 0 the sky-view, 1 the transmittance table, 2 to 2 + D - 1 the
  volume's light a slice (RGB), 2 + D to 2 + 2D - 1 its transmittance a
  slice (RGB, per channel), each in its layer's top left. The aerial pass
  is still one full-screen pass a slice, now rendering two targets (the
  slice's two layers) from one march; `lit_atmosphere_air()` returns a
  `LitAir` (light, transmittance, both RGB), and lit surfaces and the
  shell pass's composition over a drawn surface (`L_h (1 - T) + T_h L`)
  apply it channel by channel. An array with no sky is kept as before
  (two layers, no volume).
- **Memory:** the array is as wide and high as its largest table, so the
  volume's layers are too: at the paper's sizes (sky-view 192 x 108,
  transmittance 256 x 64, a 32 x 32 x 32 volume) 66 layers of 256 x 108
  half-float RGBA, 14,598,144 bytes (13.9 MiB), from 34 layers, 7,520,256
  bytes (7.2 MiB). Without a volume unchanged (432 KiB). Packing the
  transmittance beside the light in one layer (2W wide) would cost no
  memory here but march every texel twice; the second target costs no
  march. `rbtrendlit.h`'s memory note rewritten to say the array's true
  size (it gave the volume's own 256 KiB).
- **Measured** (Release, no validation, `test_atmosphere_measure`, the
  sky-view, its copy and the volume at 32 x 32 x 32, a step a slice,
  submit to idle, means of 50): before 1.15 to 1.41 ms over six runs,
  after 1.03 to 2.05 ms over sixteen, the Mac under another build's load
  throughout (the unchanged tables pass swung 1.06 to 2.13 ms in the same
  runs); the best of each 1.15 and 1.03 ms: no change the noise shows.
- **Re-recorded** (once): `test_atmosphere_aerial`, the white face 8 km
  off (68, 61, 52) is (75, 62, 46), 24 km off (54, 52, 50) is (68, 54,
  39); its mean transmittance 0.844 is 0.840 and 0.621 (0.623 when run
  before the change today) is 0.622, against Rabbittus Math's 0.847 and
  0.622 (tolerance 0.04 unchanged). No other recorded figure moved.
- **Test** (seen failing first): `test_atmosphere_aerial_channels`, the
  same faces 8 and 24 km off, red and blue each against Rabbittus Math's
  transmittance along the segment within 0.04: before, (0.835, 0.851,
  0.846) and (0.608, 0.627, 0.635) against (0.925, 0.870, 0.745) and
  (0.792, 0.659, 0.415), failing; after (0.924, 0.866, 0.731) and (0.785,
  0.657, 0.423). `test_frame_allocations` still 0 of ours.

**Card A65, decided 2026-11-17 by you (a): a lit surface on the ground
reads its suns as the ground does.** Built 2026-11-17 on the Mac (Metal,
the M5 Pro; D3D12 and Vulkan never built). A lit surface within a few
centimetres of its atmosphere's ground radius was speckled: its suns'
transmittance went through `transmittance_ray`, whose ray-sphere test
against the ground sphere, from a point within a few ulps of it, turned
on its rounding (a point under the ground was put on it and tested
there, where the horizon's cosine is exactly 0). As built:
`surface_transmittance_ray` (`rbtrend_atmosphere_model.slang`): inside
the shell, the radius taken no lower than the ground's and the direction
tested against that radius's horizon in closed form (none of the light
where its cosine with the vertical is at most -sqrt(r^2 - R^2) / r, so
at most 0 on the ground: `ground_transmittance`'s rule); above the shell,
`transmittance_ray`'s. Both of `rbtrend_lit_atmosphere.slang`'s readers
take it: `lit_atmosphere_sun_transmittance` (lit surfaces, and the sky's
suns' discs from the eye) and `lit_atmosphere_transmittance` (the shell
pass's scene air in front of a texel with nothing drawn). The shell
pass's own ground already read `ground_transmittance`. The tables'
marches (`transmittance_towards`) are as they were. No public change.
- **Test** (seen failing first): `test_atmosphere_ground_surface`, a
  white face 2 m across (lit_test_vert's square scaled by its uv.z, a
  test-shader change: 0 keeps every other test's square as it was) on a
  150 km world in units of 10 km, its up the face's normal, 0, 1, 2 and 5
  cm over the ground, a sun 30 degrees up: each face's middle even (no
  two texels more than 2 levels apart in a channel) and within 2 levels
  of the face 10 m up (92, 81, 63). Before: 0 cm from (0, 0, 0) to (92,
  81, 63), its mean (34.5, 30.4, 23.6); 1, 2 and 5 cm clean at this
  sun. After: all five (92, 81, 63) throughout. No other recorded figure
  moved.
- **`test_atmosphere_shell_inside`'s figure** (recorded above as 0.155%):
  0.169% since stage 4's fix of the ground's light in the sky-view and
  the multiple-scattering table (`ground_transmittance`, 2026-11-17), the
  scene's sky brighter (the middle texel (0.0258, 0.0548, 0.1259), was
  (0.0219, 0.0472, 0.1116)); its 0.155% was recorded before that fix and
  not re-recorded with `test_atmosphere_sky`'s. Shown by putting
  `transmittance_towards` from the ground point back in the two passes
  for one run: 0.155% and the sky's zenith (31, 68, 164) again; then
  restored. A64 and A65 don't reach it (no aerial volume; its rays leave
  the ground upwards).

**Card A66, decided 2026-11-17 by you (a): every atmosphere shader takes
its ray homogeneously.** Built 2026-11-17 on the Mac (Metal, the M5 Pro;
D3D12 and Vulkan never built). The sky shader
(`rbtrend_lit_sky_frag.slang`) and the aerial pass took a texel's ray as
`normalize(unproject(ndc, 1) - unproject(ndc, 0))`, dividing each point
by its w: under a far plane at infinity one of them has w 0 and the ray
was NaN (the sky drawn in a wrong colour, the volume black). Both now
take `normalize(b.xyz * a.w - a.xyz * b.w)` from the two points' homogeneous
coordinates, as the shell pass does; the near-or-far flip against the
view direction as before. Finite, infinite and reversed-depth
projections give the same rays. No public change.
- **Test** (seen failing first): `test_atmosphere_infinite_far`, the
  earth's sky 0.2 km up looking 15 degrees up and the air over a black
  face 24 km off through a 32 x 32 x 32 volume (the view looking 35
  degrees up), each under four perspective projections (a far plane
  and at infinity, each reversed and not): every texel at infinity within
  a level of the same projection's with a far plane. Before: the sky at
  infinity 139 levels off (a texel above the horizon (159, 142, 119)
  against (31, 62, 134)), the face's air (0, 0, 0) against (5, 9, 17).
  After: the sky 0 levels, the air at most 1. No recorded figure moved.
- **Found, not fixed (A65's cause in the tables' marches):**
  `atmosphere_march` reads each point's sun through
  `transmittance_towards`, whose ground test is the ray-sphere one A65
  replaced for lit surfaces; a march's point under the ground sphere is
  put on it, where that test turns on rounding. In the aerial volume,
  rays that go under the ground (a view looking level from 0.2 km)
  differ by up to 2 levels at scattered texels between any two
  projections, reversed against not included (the test looks up to stay
  clear of it). Taking `surface_transmittance_ray` in
  `transmittance_towards` removed it in a one-run trial (restored); it
  would change the tables' marches (the sky-view, the multiple
  scattering, the volume), so it waits on a decision of yours (decided:
  card A68, below). `rbtrend_lit_occlusion_frag.slang` and the path tracer
  (`rbtrend_rt_path_trace.slang`) bring depths 0 and 1 back through the
  inverse the same way (`p.xyz / p.w`), so their view ray is NaN under
  an infinite far plane too; outside A66's atmosphere shaders, not
  changed (decided: card A69, below).

**Card A68, decided 2026-11-17 by you (a): the marches read their sun by
the ground's rule.** Built 2026-11-18 on the Mac (Metal, the M5 Pro;
D3D12 and Vulkan never built). `transmittance_towards`
(`rbtrend_atmosphere_model.slang`) takes A65's
`surface_transmittance_ray` in place of `transmittance_ray`: a march's
point under the ground sphere, put on it, reads its sun's transmittance
at the ground's radius against that radius's horizon in closed form, not
by a ray-sphere test that turns on rounding there. It is what
`atmosphere_march` reads at each point and what the multiple-scattering
pass reads, so the sky-view, the multiple scattering, the aerial volume
and the shells' march all take it. No public change.
- **Test** (seen failing first): `test_atmosphere_infinite_far` now looks
  level for the volume (half its rays under the ground), and checks every
  projection against the first: the sky every texel the same (it was);
  the volume's light over the black face, below the horizon, every texel
  the same. Before: 2 levels at scattered texels below the horizon (rows
  34 to 55) between a projection not reversed and a reversed one, a far
  plane or none. After: none below the horizon; a projection not
  reversed differs from a reversed one by a level at 4 texels above it
  (its rays' own rounding across an 8-bit step, nothing under the
  ground), so the check across reversals is within a level, the draft's
  "0 levels" held below the horizon only.
- **Figures:** none moved. Every recorded atmosphere figure is the same
  after it: `test_atmosphere_sky` (37, 78, 183) and (55, 66, 72),
  `_sun_through_air`, `_aerial` (0.840, 0.622), `_aerial_channels`,
  `_environment`, `_shell_crossing` 0.70%, `_order` 0.079%, `_inside`
  0.169%, `_far`, `_whole_world`, `_depth`, `_no_sky`.

**Card A69, decided 2026-11-17 by you (a): GTAO and the path tracer take
their rays homogeneously.** Built 2026-11-18 on the Mac (Metal, the M5
Pro; D3D12 and Vulkan never built). `rbtrend_lit_occlusion_frag.slang`'s
view ray is `normalize(ha.xyz * hb.w - hb.xyz * ha.w)` from the depth-0
and depth-1 points' homogeneous coordinates, turned to the eye's side as
before. The path tracer's camera ray (`rbtrend_rt_path_trace.slang`)
starts at the near end, the one whose homogeneous w is the larger (w is
one over the clip w; the far end at infinity has w 0), or for an
orthographic view (w alike to 1e-4) the nearer the eye as before; its
direction the two ends taken homogeneously, turned along the view
direction. No public change.
- **Tests** (seen failing first): `test_lit_occlusion_projections` (a step
  of two planes, 2 and 1.6 ahead, through four perspective projections,
  each plane at its projection's depth): before, at infinity every texel
  0 against 249 to 255 with a far plane (255 levels off); after, within 1
  level of the same projection's with a far plane.
  `test_rt_path_trace_projections` (the emissive square over the ground
  in perspective from 5 above, 16 samples, on the device with
  CRGPU_FEATURE_RAY_QUERY; the bare device reports it skipped): before,
  at infinity the middle's mean 0 against 0.20324; after, 0.20324, within
  0.002% of the same projection's with a far plane, no texel not finite.

Found while drafting stage 1 (measured 2026-11-16, M5 Pro, Release,
`rabbittustests`' `test_lit_sky_measure`): one LIT_SKY fill at 128/32 is
4.9 to 6.6 ms from submit to idle, Preetham's the same as Hosek and
Wilkie's, so the prefilter is the cost, not the model. A cheaper fill
(fewer samples for a smooth sky, or the prefilter spread over frames) is
Rend's own work, not started.

### 53. A layered lit surface (card L68's Rend half, decided 2026-11-17)

Decided 2026-11-17 with you: `rbtrend_lit_layers_draft.h` approved as
recommended (Q-L1a: a layered surface of Rend's sharing the lit
lighting, `lit_shade()` split into its reads and its lighting; Q-L2a: 2D
texture arrays; Q-L3a: base colour, normal and ORMH a layer; Q-L4a: a
dithered fade carded, not added). Given a second round of seven critique
passes, at your asking, for general purpose and industry standard, and
approved as version 3 (explicit gradients for every read in a branch, the
normals' blend space, a 48-byte layer block). Its sampler's anisotropy
above 1 waits on Carrottus GPU's anisotropy bit and limit (card S49): every
Rend sampler made above 1 on Vulkan is outside Vulkan's rules until then. Its set 1 holds the layered material; set 0
is untouched. No version raise while 0.3 is untagged (card X2). Built
after its caller's terrain, its GPU tests first.

**Amended by the third critique round, 2026-11-17 (the draft's version
4, approval kept):** the split of `lit_shade()` as written couldn't be
built (`rbtrend_lit_surface.slang` imports the lit material's set 1), so
the lighting becomes a module of its own, `rbtrend_lit_lighting.slang`
(in `RABBITTUS_SHADER_MODULES`), importing no material, its surface
struct gaining transmission, coat, sheen and emission; location 9 is the
quaternion (the body's rotation, made in the caller's vertex shader from
its rows, which must be a rotation with no scale), ten locations in all,
the old duplicate of location 1 removed; `rbtrend_material_get_layer()`
added; the blend `w' = w * max(a - max(a) + d, 0)`, a zero-weight layer
dropped (the old formula pulled to equal shares); the layer count in the
block's row 0, an index past it no layer; set 1 numbered (block 0,
arrays 1 to 3, sampler 4); the block 784 bytes whatever the count; three
neutral 1 x 1 sixteen-layer arrays per instance (ORMH's height the byte
128); fragment sampled images 14 on the layered shader, 15 on the lit,
against Vulkan's floor of 16. Tests 13 and 14 added.

**The cover stage (snow, wetness; card L74's, which this item's draft
carded as "a cover input"):** drafted 2026-11-18 as
`rbtrend_weather_draft.h` (version 1, seven critique passes), approved
2026-11-18, every question as recommended. Built for the plain lit
surface on 2026-11-19 (item 64). This item's layered surface, when
built, takes the response chained onto `RBTREND_LIT_LAYER_VALUES` and
reads the scene's cover through the shared lighting (the weather draft's
pass 7.3: the second of the two to be built adds the chain), with the
weather draft's test T7 (each layer's own response). Its caller's half
is drafted beside the caller's headers.

**Built 2026-11-19 on the Mac** (Metal, the M5 Pro; D3D12 and Vulkan
never built: no backend branch, the Slang compiled to SPIR-V and DXIL's
reflection only on the Mac's own route). From the approved draft
(version 4, its hash checked first), in two sub-stages, each whole:
- **The split** (test 1 first): `rbtrend_lit_lighting.slang` holds
  `LitFragmentInput`, the lighting (`lit_light(input, LitSurface)`), the
  eye-facing frame (`lit_frame()`), the tangent frame
  (`lit_tangent_normal()`), the cover and the transmission, importing no
  material; `rbtrend_lit_surface.slang` is the lit material's reads into
  a `LitSurface`. `test_lit_split_same_pixels` (rabbittustests) hashes six
  pictures (a sun and a point light over every map, a coat, a sheen and
  emission; the cover over it; the fog over that; the reflecting
  variant's three targets) recorded from the build before the split
  (rabbittus 30047aa): the same bits after it, both devices, Debug and
  Release. `lit_cover()` takes the surface's cover, porosity and the
  share that answers (`wet.x * responds`: 1 for a lit material, so its
  bits unchanged).
- **The layered surface**: `RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT`
  (9) and `_REFLECT_FRAGMENT` (10); `rbtrend_lit_get_layered_material_layout`,
  `rbtrend_lit_layered_material_create`, `_set_layer`, `_get_layer`,
  `_set_blend_depth`, `_set_map`, `_set_sampler`; `RBTREND_LIT_LAYERED_MAP`,
  `RBTREND_LIT_LAYER_PROJECTION`, `RBTREND_LIT_LAYER_VALUES` (struct type
  **5061**, the next free: the draft's 5039 was taken), four constants.
  Shaders `rbtrend_lit_layered_material.slang` (set 1: block 0, arrays 1
  to 3, sampler 4), `rbtrend_lit_layered.slang` (steps 0 to 5),
  `rbtrend_lit_layered_frag.slang`, `rbtrend_lit_layered_reflect_frag.slang`.
  Set 0 untouched (the scene block's last byte stays as card A75 left
  it); fragment sampled images 14 (the lit surface's 15), samplers 4.
  ABI: additions only (`./check.sh --update-abi rabbittus`), no version
  raise (0.3 untagged, card X2); both misuse harnesses regenerated
  (rabbittus' and rabbittustests' destroyed calls, seven new calls each).
  `check_naming.py` lists `lit_layered_material` as Rend's value object
  beside `lit_material`.
- **T7 (the weather draft's)**: a layer's `RBTREND_LIT_COVER_RESPONSE`
  chained on its values; packed into its row's spare lane (the cover and
  the porosity over 32767 in 15 bits each, bit 31 "answers"), the exact
  values kept by the material for `_get_layer` (`rbtrend_material_t::
  lit_layer_responses`, 128 bytes at create, copied by
  `rbtrend_material_copy()`); the pixel blends cover, porosity and the
  share that answers by w'.
- **Shapes chosen within the contract** (for review): the neutral arrays
  are render targets cleared, a layer at a time, at the top of the first
  compile that binds a layered material, as an environment's cubes are
  (Rend holds no queue to upload them through at create); the normal one
  is half floats so (0.5, 0.5, 1) is exact, the others bytes (ORMH's
  height the byte 128). A clear of non-zero values may draw D3D12's
  debug-layer warning of a mismatched clear value (the textures are made
  with a zeroed one): to be seen on Windows. The triplanar weights read
  location 1's normal turned to the eye before it is turned back (a face
  seen from behind reads its front's planes, as the lit surface lights its
  front). At d = 0 (or wherever no layer is left by the band) the largest
  a takes the pixel whole, ties to the lower entry, as the draft says.
- **Tests** (rabbittustests, Metal under API and shader validation with
  warnings fatal, Debug and Release, both devices): test 1 above;
  `test_layered_refusals` (7, 12, 13's getter: every refusal its own
  check and message, a 2D view and one of too few layers refused, a
  sampler of another instance, a lit material's call refusing a layered
  one, a TLAS's table refusing it where ray queries exist, get_layer's
  round trip exact, values staged), `_weights` (2), `_height_blend` (3:
  0.2 one layer, 2 a third and two thirds, 0 the higher whole, contrast 0
  and depth 100 within 1%, a zero-weight high entry dropped), `_four` (4),
  `_triplanar` (5: a checker square on q.xy, q.zy and q.xz by the turn;
  the neutral normal lit as the face), `_reflect` (6: the colour target
  the plain variant's bits, roughness 0.5 of 0.2 and 0.8), `_gradients`
  (9: every texel its layer's level 2 across a depth-0 switch, none of
  another level), `_normals` (10: a UV and a triplanar layer flat, then
  one bent, the blend's normal by the test's own arithmetic under suns
  from +z and +x), `_index_past_count` (13), `_cover` (T7);
  `test_frame_allocations` with a layered square set each frame: 0 of
  ours (8). Contract tests (rbtrendreflect_tests): `test_layered_block`
  (11: every member's offset from the reflection against the C++ block)
  and `test_layered_sets` (14). Seen failing first: every GPU test against
  a build whose create returned RBT_RESULT_NOT_IMPLEMENTED; and three
  behavioural tests against a mutated shader (the turn ignored: the
  triplanar checker's q.zy and q.xz cases stripes; the d = 0 fallback
  removed: black; an index past the count clamped: green), the last after
  the test was corrected (its entry 1 had named the very layer a clamp
  gives). The two contract tests were written after the shaders; their
  first run failed by the test's own lookup, not the code.
- **Measured** (Release, no validation, `RABBITTUSTESTS_MEASURE=1
  test_layered_measure`: a face over 2560 x 1440 of 1024-texel maps,
  submit to idle, the mean of 50 after 10, both devices, two runs): the
  lit surface with four maps 0.72 to 0.86 ms; layered, one UV layer 1.09
  to 1.11, two 1.29 to 1.30, four 1.67 to 1.71, four TRIPLANAR (36 reads)
  2.99 to 3.01. The draft costed reads, not milliseconds (3 to 6 typical
  against the lit surface's 4; 36 at worst): about 1.5 times the lit
  surface for typical ground, 4 times at the worst.
- **Owed**: D3D12 and Vulkan; a sampler above anisotropy 1 still waits
  on S49; the later stages (anti-tiling, weights from a texture) as the
  draft's section 4, each when a caller asks.

### 54. A view-model pass: a second pass with its own field of view, depth cleared (card A58, decided 2026-11-17: drafted when wanted)

Asked for by you on 2026-11-17: a draft, when wanted, for drawing chosen
instances in a second pass with their own field of view and near plane,
depth cleared between, lit and shadowed with the scene. Card A58 says
what the draft settles. Not drafted; nothing waits on it.

### 55. Volumetric clouds (card A57, decided 2026-11-17)

Decided 2026-11-17 with you: clouds the Nubis way (tiling 3D noise and a
coverage map, density from height profiles and erosion, marched) (Q1a);
the density function Rabbittus Math's, the noise volumes baked from
Math's noise on the caller's jobs and uploaded, the march, its light and
its reprojection Rend's (Q2a); quarter resolution with a history of the
clouds' own, A39's handle (Q4a); a coverage map as a cube map on a sphere
(Q5a); stages: from under and inside, then from outside the shell, then
cloud shadows with a binding plan of their own (Q6a). **Q3a, the object,
as reshaped with you:** `rbtrend_clouds`, an object of its own (its noise
volumes, coverage maps and history), not part of the atmosphere, and not
shaped for one kind of world:
- its layers bounded by a spherical shell or a flat slab, the same object
  for a round world and a flat level;
- lit through an atmosphere when the caller gives one, else by the
  scene's lights alone, so a scene with no atmosphere has clouds;
- several layers drawn a frame (low and high clouds, a fog-bank layer),
  an array sent each frame as the atmosphere's shells are;
- its look entirely the caller's data through Math's density model
  (coverage, types, profiles, erosion), so overcast, storms, dust or a
  fog bank are settings, not code;
- the reduced-resolution march and its history Rend's internals, shared
  later with item 56's fog.
To be drafted (Rend's object and Math's density, for approval) after the
atmosphere's stage 4 is built, and a GPU test of a 3D texture written by
compute and sampled on three backends first. No version raise while 0.3
is untagged.

Drafted 2026-11-18: `rbtrend_clouds_draft.h` and `rbtmath_clouds_draft.h`
(version 1, seven critique passes), approved 2026-11-18 with the user, every question as recommended; nothing built. Stage 1
(from under and inside) whole, stages 2 and 3 said as additions; seven
questions in Rend's half (the history a record a view inside the object;
its resolution half each way; the volumes and maps the object's own; a
shell's map read by hand from a 2D array; grey extinction; directional
lights by index; a shell layer seen from outside drawn as from inside
until stage 2), five in Math's. Its prerequisite test, a 3D texture
sampled on three backends, is its test 1.

**Stage 1 built 2026-11-18 on the Mac (M5 Pro, macOS 27, Metal), Debug and
Release; D3D12 and Vulkan never built** (`rbtrend_clouds_draft.h` as
approved, every question as recommended: Q1 a, Q2 a, Q3 a, Q4 a, Q5 a, Q6
a, Q7 a). `render/rbtrendclouds.h` (twenty headers in `rbtrend.h` now):
`rbtrend_clouds` (`rbtrend_clouds_create`, `_destroy`, `_write_noise`,
`_write_coverage`, `_set_look`), `RBTREND_CLOUDS_DESC` (struct type
**5043**), `RBTREND_CLOUD_LOOK` (**5044**), the frozen
`RBTREND_CLOUD_LAYER`, nine limits; **`RBTREND_COMMAND_LIT_CLOUDS` = 53**
(52 was taken by LIT_FOG), its parameters 72 bytes of the 120;
`RBTREND_LIT_SCENE_DESC::max_view_sets`' comment names it (it takes one set
from the scene's records). `src/render/rbtrendclouds.cpp`,
`rbtrendclouds_internal.hpp`, the compile in `rbtrendcommands.cpp`
(`rbtrend_cmd_lit_clouds`), the pipelines in `rbtrendlit.cpp`
(`rbtrend_lit_clouds_prepare`); shaders `rbtrend_clouds_model.slang` (the
density, Math's steps 1 to 7, no bindings), `_input`, `_bindings`,
`rbtrend_clouds_march_comp`, `_reconstruct_comp`, `_composite_frag`.
Set 0 untouched (the march and reconstruct shaders join the shaders the
scene's layout is made from, which widens stages and changes no binding);
the lit scene block untouched. The record's history is
`rbtrendhistory.hpp` unchanged. Passes: the march (compute, a thread a
quarter-resolution texel: sets 0 the scene's, 1 a view-set record over the
depth and the four layers' transmittance tables, 2 the clouds' own
material, 3 the record's march material), the reconstruct (compute, a
thread a half-resolution texel: the scene's set and a material a way the
history runs), the composite (a full-target pass, blend ONE / SRC_ALPHA,
over the scene's set, the same view-set and the record's composite set a
way). Shapes chosen alone (none in the draft): `RBTREND_CLOUDS_MAX_TARGET_SIDE`
(16384, the desc's "1 to 16384" named); a look block of 160 bytes (the
draft said 256 an entry; ten float4 are all a look needs), a layer of 256;
the composite's air and then the scene's height fog in front of the
clouds' depth (`rbtrend_fog_draft.h` section 6's [CHANGED], built here as
the second of the two to be built); a jitter sequence of the history
module's van der Corput values (the texel within its history texel by the
4-frame cycle, the march's offset by the frame); an atmosphere never built
is taken as none silently (the draft said "the compile's Debug report
saying so": not written, no report channel that doesn't fail a compile).

Tests, each seen failing: `rbtrend_contract_tests`' `test_clouds_contract`
(9 failed checks against stubs returning NOT_IMPLEMENTED); in
rabbittustests (on Metal under API and shader validation, warnings fatal,
Debug and Release), run against a build whose LIT_CLOUDS compile returned
NOT_IMPLEMENTED and whose density returned 0, written after the code
(order of work, said here): `test_texture3d_sampled` (test 1; written
with the prerequisite's shader, passing first time), `test_clouds_density_probe`
(8 failed), `_refusals` (8), `_deck_from_below` (4), `_thin_layer_transmittance`
(10), `_shell_and_slab` (6), `_layers` (4), `_depth` (12), `_atmosphere`
(14), `_air_in_front` (4), `_records` (16), `_writes_in_flight` (12),
`test_frame_allocations` (4); `_history` hung in that build (below). Results:
- test 1: a texel's place read by the GPU's trilinear filter with repeat
  within 0.00227 of Math's `rbtmath_noise_volume_sample` (0.58 of a
  byte; bound 1/255), exact at texel centres.
- test 2: the march's density within 0.0002 of Math's over 4,096 points
  of shells of both warps and slabs of both addresses (estimate 0.02;
  `CLOUDS_PROBE_TOLERANCE` recorded at 0.001).
- tests 4 to 10: a stratus deck's T within 0.64% of Beer's law of Math's
  optical depth (bound 5%), its middle smooth; a thin layer within 0.41%
  of exp(-sigma_t x path) (1%; at 70 degrees on a reset frame, every
  texel of a 4 x 4 block showing one ray, the corners were 1.49% off: the
  test draws four frames at 50 degrees); a 1,000 km shell against a slab
  0.087% (2%); two layers the near over the far within 0.32% (1%),
  swapped bit for bit; a surface in front unchanged bit for bit, one
  behind dimmed within 1%; a deck at a 2 degree sun red over blue 2.51
  with the atmosphere, 1.00 without, and black (0) under the horizon with
  it; the air in front by the composite's own formula within 0.2% each
  channel (L_air from an opaque deck, T_air from a second).
- test 11: **two of the draft's bounds didn't hold, held at the measured**:
  a still view's frames 4 and 64 2.49 levels apart (the draft: 1), a view
  turned 10 degrees a frame with the world moved under the eye within
  3.19% of a still view after 4 frames (the draft: 3%), both at the
  deck's texels nearest the horizon. Section 4 keeps each history texel's
  last march (the marched texel takes the fresh value), so the history
  doesn't average the jitters as section 3's step 2 expects; blending the
  marched texel into its reprojected history (a share 1 / n, at least a
  quarter) was tried and changed the still case by 0.25 levels, so wasn't
  kept. Bounds `CLOUDS_STILL_LEVELS` 3 and `CLOUDS_TURN_SHARE` 4%. A reset
  frame bit for bit a record's first.
- tests 12 to 14: the third scene of three over two views takes the least
  used record; three in one list refused with RBT_RESULT_LIMIT naming
  max_views; one scene twice refused; a look and a map region set while
  a frame is in flight: that frame as before, bit for bit, the next with
  them; `test_frame_allocations`' clouds scenario (two layers lit by a sun
  through four light steps, a look and a map region written a frame, the
  history carried): none of Rend's, 100 frames; `test_destroyed_handles`
  189 calls (five the clouds', the factory `dead_rbtrend_clouds`).
- **Found:** a compile refused after a pass or a dispatch was recorded
  keeps its Metal command buffer for good (Carrottus' Metal backend: the
  encoders are autoreleased and a plain C caller has no pool that drains,
  so the never-committed buffer is never freed); a queue's 64 run out and
  `-commandBuffer` blocks. The sabotaged run hung so in test 11, and the
  first full run hung in `test_block_texture_sampling`'s second device
  after the clouds' refusals added theirs (and, with those cut down, a
  later run in `test_frame_allocations`' clouds scenario). The clouds'
  refusals now send the command alone where they can, and rabbittustests'
  `RUN` drains an autorelease pool of its own round every test
  (`objc_autoreleasePoolPush`/`Pop` on Apple; `REFUSAL_POOL_PUSH`), after
  which the whole suite runs on both devices under validation. The
  backend's leak is Carrottus' to fix (`crgpumetal___internal_acquire_command_buffer`'s
  comment says the buffer itself is handled, not its encoders), not
  touched here. **Fixed there 2026-11-18 (card S53; item 63's note):**
  `RUN`'s pool and `REFUSAL_POOL_PUSH` are gone, and the suite passes
  without them; the clouds' refusals still send the command alone.
Measured (`test_clouds_measure`, Release, no validation): the clouds' cost, submit to idle less the same frame with none, means of
50 after 10, on both devices' passes (the second pass's numbers in
brackets), over Nubis's 128 and 32 volumes and a 512 map. At **2560 x
1440**: 32 steps and 4 light steps 1.39 to 1.45 ms (1.34 to 1.47); 32 and
6, 1.61 to 1.79 (1.59 to 1.75); 64 and 4, 2.26 to 2.40 (2.22 to 2.45); 64
and 6, 2.66 to 2.85 (2.64 to 2.85); 64 and 6 with 3 octaves 2.66 to 2.90
(2.72 to 2.89), over a broken deck from under, inside a deck and two
layers. At **1920 x 1080**: 0.84 to 1.11 at 32 steps, 1.35 to 1.89 at 64.
The draft's estimate, 1.3 to 2.6 ms at 1440p and 0.8 to 1.5 at 1080p,
held at its low end (32 steps: inside the card's 1.5 to 2 ms budget at
1440p) and was passed at 64 steps and 6 light steps (2.9 ms at 1440p, 1.9
at 1080p: over the budget). The levers are the caller's (`_max_steps`,
`_light_steps`): 64 and 6 cost 2.9 ms at 1440p, 32 and 4 or 6 are within
the budget.
Memory as the draft's: 23.7 MiB a record at 2560 x 1440. Stages 2 and 3
not built.
Card A70 decided 2026-11-18 (a): the draft's history rule revised (version 2), approved the same day, every question as recommended (Q8 to Q12 a).

**Version 2's history rule (card A70) built 2026-11-19 on the Mac (M5 Pro,
macOS 27, Metal), Debug and Release under validation; D3D12 and Vulkan
never built.** No C API change (abi.txt unchanged); `rbtrendclouds.h`'s
opening comment says the new rule (the draft's section 8). The history
module (`src/render/rbtrendhistory.hpp`) holds it: the march's place in
each march texel's 4 x 4 block by the ordered-dither order of sixteen
frames, through a target texel's centre (`rbtrend_history_clouds_place`;
the history texel of frames 0 to 3 {0, 3, 1, 2} as before,
`_clouds_texel`); the step offset of the visit, J(frame / 4)
(`_clouds_step`), rotated in the march by the march texel's interleaved
gradient noise (Q10 a); the marched texel's weight 1 / (v + 1), never
below `RBTREND_HISTORY_CLOUDS_WEIGHT_FLOOR` a quarter (Q8 a,
`_clouds_weight`); and the share a history keeps, one function both
call (`rbtrend_history_share`): the fog's integrate with its frames and
its caller's weight (bit for bit as built: card A72 undecided, the fog's
behaviour unchanged, its tests as before), the clouds with their visits
and one less their floor. The reconstruct (`rbtrend_clouds_reconstruct_comp`)
runs the reprojection for every history texel, the marched one too;
clips an accepted history towards the mean of the march's 3 x 3 texels to
mean +- gamma sigma per channel (L's three and T, as stored; Karis's
clip, Salvi's box; Q9 a), `RBTREND_HISTORY_CLOUDS_CLIP_GAMMA` **1.5**;
blends the marched texel's march into it by the weight (its depth by
each side's weight and opacity, the march's where both are clear), keeps
the others; a refused texel or a reset frame the march's own, with no
per-texel age (Q11 a); the history read bilinearly (Q12 a). The weight
and gamma ride in the history block's spare `frame[1]` and `frame[2]`;
the march block's `jitter` holds the place (x, y) and the visit's
offset (z). No texture, record or block added.

Tests, each seen failing first: `rbtrendhistory_tests`' `test_clouds_order`
and `test_weights` (the draft's test 16), against today's sequence moved
into the module's functions unchanged: 1,188 failed checks, both faults
among them (each history texel's two diagonal target texels never
marched, 8; each history texel's step offsets four of sixteen, 48), and
the weight 1 at every visit. Test 11 restated (`test_clouds_history`,
rabbittustests): against stage 1's rule it measured (a) flicker 5.35
levels, (b) frame 64 against the mean of 64 to 127 1.12 levels, (c)
frames 4 and 64 2.49 levels, the turn 3.19%; the bounds below fail all
three. Measured with version 2 (Debug and Release, both devices, the
same): **(a) 0.9961 levels, (b) 0.5447, (c) 1.2451 (recorded, no bound),
the turn 2.5183%**, the still view's worst texels at the deck's row
nearest the horizon. **Bounds, the measured rounded up to a tenth:
`CLOUDS_STILL_FLICKER_LEVELS` 1.0, `CLOUDS_STILL_MEAN_LEVELS` 0.6,
`CLOUDS_TURN_SHARE` 2.6%** (replacing `CLOUDS_STILL_LEVELS` 3 and
`CLOUDS_TURN_SHARE` 4%). Against section 4's estimates: flicker 0.6 to 1
(held, at its top), the mean 1 to 1.5 (better), frame 4 against 64 1.9
to 2.5 (better: 1.25, frame 4's march now through a target texel's
centre, its offsets spread), the turn 3 to 4% (better). Gamma measured
at 1.0, 1.25, 1.5 and 2.0 (the draft's estimate 1 to 1.5): flicker 1.62,
1.37, 1.00, 0.37 levels; mean 0.94, 0.77, 0.54, 0.21; the turn 2.41%
(1.25) and 2.52% (1.5 and 2.0); 1.5 kept, the top of the approved
estimate (a wider box lags a sun or weather change more, which no test
measures yet). The other clouds tests moved within their bounds: a deck
from below 0.55% off Beer's law (was 0.64%), a thin layer 0.416% (0.41%),
two layers 0.551% (0.32%; bound 1%). `test_frame_allocations`' clouds
scenario: none of Rend's (no allocation added).
Measured again (`test_clouds_measure`, Release, no validation): at
**2560 x 1440, 32 steps and 4 light steps 1.51 to 1.66 ms** (stage 1:
1.34 to 1.47), 32 and 6 1.78 to 1.99, 64 and 4 2.38 to 2.58, 64 and 6
2.85 to 3.16, 3 octaves 2.90 to 3.23; at 1920 x 1080 0.79 to 1.47 at 32
steps (the run's first case once 1.79), 1.42 to 2.26 at 64. More than the draft's estimate (under 0.05
ms, the reconstruct's): with the per-texel rotation set to 0 (a trial,
not kept) 32 and 4 measured 1.39 to 1.52 and 64 and 6 2.71 to 2.93, so
the reconstruct costs about 0.05 ms and the rotation of the step offset
(neighbouring march texels sampling the volumes at different depths)
about 0.1 to 0.15 ms at 32 steps and 0.2 to 0.3 at 64. 32 and 4 stays
within the card's 1.5 to 2 ms budget at 1440p.
**Found in a caller's tests:** a round world turned 30 degrees in one
frame with its camera (the caller's space eye-relative, the clouds
turning with their world) fell outside that test's bounds at six frames
a picture under version 2 (4.25 levels apart on average, 2,387 of 4,096
texels over 2 levels, the worst 24; bounds 1, 341, 10): a world turned
that far in a frame is no move the reprojection follows, the depths agree
inside a shell so the history is accepted, and it then takes the
history's time to settle (stage 1's rule replaced every texel in four
frames). Section 4's "no texel's visits restart (the clip, not a reset,
bounds what the turn carries)" holds for a view's turn, not for a cut
like this one. Its bounds were kept and each of its pictures is drawn
from a settled history (64 frames each: 0.055 levels, 0 texels, the
worst 1; at 32 frames 0.53, 190, 4). A caller that turns its world by a
jump resets the history (`_history_reset`). **Card A74 decided
2026-11-20 with the user (a):** such a cut is the caller's to know and
reset (`_history_reset` when its world turns by more than a set angle in
a step, as at its other cuts); Rend unchanged, no angle rule of its own.


### 56. Volumetric fog near the eye (card A59, decided 2026-11-17)

Opened and decided 2026-11-17 with you, every question as recommended,
apart from item 55's clouds: a froxel grid fed by a height fog and local
fog volumes (boxes, spheres, their density from Math's noise and a
falloff), lit by the scene's lights and their shadows, integrated, applied
to every surface and to transparents, with a history (Q1a); staged:
analytic height fog first (its density in Rabbittus Math), then the grid
with directional lights and local volumes, then point and spot lights and
transparents (Q2a); fog's density Math's, the grid Rend's (Q3a); shared
internals with item 55's clouds, each its own public object (Q4a). To be
drafted; needs the 3D-texture GPU test, A39's history, the transparency
stage for transparents.

Drafted 2026-11-18: `rbtrend_fog_draft.h` and `rbtmath_fog_draft.h`
(version 1, seven critique passes), approved 2026-11-18 with the user, every question as recommended; nothing built. Stage 1
(an analytic height fog, a scene setting the lit shaders apply to
surfaces, transparents and the sky) and stage 2 (`rbtrend_fog`, its grid,
history and command) whole, stage 3 said as additions with one binding
plan for set 0 shared with item 55's cloud shadows; seven questions in
Rend's half, three in Math's (a sphere's transmittance by Schüler's
approximation of the Chapman function recommended). The history is a
record a view inside the object, as item 55's, not A39's handle (its
Q4).

**Stage 1 built 2026-11-18 on the Mac (M5 Pro, macOS 27, Metal), Debug and
Release; D3D12 and Vulkan never built.** `RBTREND_LIT_FOG` (struct type
5041, the next free: 5039 is planned by `rbtrend_lit_layers_draft.h`),
`rbtrend_lit_scene_set_fog()` (NULL clears; Math's checks of its fog, its
message prefixed `fog->fog:`), `RBTREND_LIT_FOG_MAX_SUNS`. The scene block
gains five float4 (`fog_up_base`, `fog_eye`, `fog_albedo`, `fog_range`,
`fog_light`: 80 bytes, not the draft's estimated 64; the block 16,208 of
the 16,384 Vulkan guarantees) and flag 4096; a sphere is told by its up
of 0, so no second flag. The eye less the fog's origin is taken in double
at each commit. `shaders/rbtrend_lit_fog.slang` (a module, in
`RABBITTUS_SHADER_MODULES`) is Math's definition in float: a plane's
closed form with g's series, a sphere's parts split at the closest point
and the base's crossings (no arrays: Metal's cross-compile refused the
unused constants an array's initialiser left), the outward depths by the
Chapman function's large-x form (Math's, fixed the same day: Hastings'
erfcx below 2 and an 8-term continued fraction above, within 3e-5);
the light its albedo times the suns' (pi x radiance x Henyey-Greenstein x
the atmosphere's transmittance at the eye) and the ambient light at the
middle of the part seen. Applied after the aerial volume in
`rbtrend_lit_surface.slang` (the specular target's weight times T, so
reflections added later are fogged too: a choice of the build's) and over
the sky and its discs in `rbtrend_lit_sky_frag.slang` (to the fog's end).
LIT_CLOUDS's composition (section 6) not built: LIT_CLOUDS isn't.
Tests in `../rabbittustests` (each seen failing first, against a stub
returning NOT_IMPLEMENTED: 52, 62, 16, 22 and 6 failed checks):
`test_fog_refusals`, `test_fog_against_math` (a black face at 10, 100 and
1,000 under a plane's and a sphere's fog: every one within a count of
Math's; the sphere's, with the Chapman fix, T 0.9845, 0.8665 and 0.7581
and texels 3, 27 and 49, were 0.9797, 0.8562 and 0.7581 and 4, 29 and
49), `test_fog_distances`, `test_fog_sky_and_transparent` (the sky
124 against Math's 123.9, a disc dimmed 128 to 50 against 50.3, a
half-transparent face 27 against 27.3), `test_fog_none_unchanged`;
`test_fog_measure` (RABBITTUSTESTS_MEASURE, Release, no validation; the
lit faces over every texel once, one ambient light, submit to idle): at
2048 x 2048 a plane's fog adds 0.21 to 0.24 ms and a sphere's 0.60 to 0.61
ms (0.45 to 0.47 before the Chapman fix), at 1024 x 1024 0.04 and 0.13 to
0.18 ms: about 0.20 and 0.53 ms at 1440p, against the
draft's estimate of under 0.05 ms (not met; the cost a texel grows with
the target, 6 to 7 times for 4 times the texels). Stage 1 allocates
nothing (a scene setting staged at the submit).
Stage 2's first step, `test_texture3d_compute` (test 6): a 4 x 4 x 4
R16G16B16A16_FLOAT texture written by compute through a 3D storage view
(`volume_write_comp.slang`) and read back exactly, then written and
sampled by a fragment shader (`volume_slice_frag.slang`) in one list:
passes on Metal with no change to Rend (so never seen failing: nothing in
Rend was missing). The rest of stage 2 is below; stage 3 isn't built.
The history records module, shared with item 55's clouds, built
2026-11-18 as both drafts describe it (`src/render/rbtrendhistory.hpp`:
a record a view, its scene by handle, the least recently used one no
list holds taken else none, a reset when a record changes hands or was
never written; a 16-step van der Corput jitter, each at its step's
middle; a place carried into last frame's clip space through the space's
shift), with `tests/rbtrendhistory_tests.cpp` (seen failing first against
a stub: it crashed on the stub's UINT32_MAX index; one expectation of the
test's own was wrong and fixed). Nothing uses it yet.
What stage 2 needs that the code showed (for its build):
- Its inject pass reads the scene's set 0 (the block, the shadow atlas and
  its comparison samplers, the atmosphere's tables, the environment's
  irradiance) from compute. The lit scene layout is made from the surface
  and sky shaders alone (`rbtrend_lit_scene_layout()`), so its bindings
  aren't visible to compute on Vulkan; the inject shader joins the shaders
  it is made from (the same handle, its stages wider, no caller's layout
  changed) and binds `scene->material->set` at set 0, as the shells'
  pass does from a fragment shader.
- The per-frame data (last frame's view-projection, the grid's distances
  and weight, the jitter, up to RBTREND_FOG_MAX_VOLUMES volumes resolved)
  is a uniform block of a record's own material (two a record, one a
  history direction), copied into the frame's arena when bound: about
  64 x 128 bytes of volumes (the draft's 96 a volume leaves out the
  bounding sphere the cull reads), 8.3 KiB. The tiles' lists are a
  storage buffer of RBTREND_BUFFER_WRITES_NONE, 0.9 MiB at 160 x 90.
- `rbtrend_cmd_own_dispatch()` dispatches in two dimensions; the grid's
  passes need a third (an internal variant).
- **Room in the scene block:** none needed by stage 2 (the grid's data is
  the record's material's, the fog's the scene's existing members). Stage
  3's transparents reading the grid need a set-0 binding, not block room;
  set 0 holds sixteen bindings, all taken. When stage 3 is built, the
  grid is read at the fragment's own place by BLEND surfaces
  (RBTREND_LIT_ALPHA_MODE_BLEND, item 24, built 2026-11-20), after card
  A75's freed binding. The block is at 16,208 of the
  16,384 bytes Vulkan guarantees (176 free): item 55's clouds as drafted
  add nothing there; anything else that would must be weighed against it.

**Stage 2 built 2026-11-18 on the Mac (M5 Pro, macOS 27, Metal), Debug and
Release; D3D12 and Vulkan never built. Stage 3 not built.** From the
approved draft:
- **Public:** `render/rbtrendfog.h` (new; `rbtrend.h` reaches it, nineteen
  headers): `RBTREND_FOG_DESC` (struct type 5042), `rbtrend_fog_create()`,
  `_destroy()`, `_write_noise()`, the frozen `RBTREND_FOG_VOLUME_INSTANCE`
  (104 bytes) and the five limits the draft declares (its "four" counts
  one fewer). `RBTREND_COMMAND_LIT_FOG` = 52 (`_lit_fog`, 72 bytes of the
  120); the `rbtrend_fog` typedef and `struct RBTREND_FOG_VOLUME_INSTANCE`
  forward-declared in `rbtrendcommands.h`, as `rbtrend_atmosphere`'s are.
  `max_view_sets`' comment names LIT_FOG (five commands). `./check.sh
  --update-abi rabbittus`: additions only (the constant, the struct type,
  three functions, five macros, the opaque handle, the command's
  parameters, the two structs and their typedefs), no break.
- **Shaders** (Slang, listed by hand): `rbtrend_fog_cull_comp` (8 x 8
  tiles: each volume's bounding sphere against the tile's four side planes
  from the eye and within the far distance, the first 15 kept in the
  command's order), `rbtrend_fog_inject_comp` (4 x 4 x 4 froxels),
  `rbtrend_fog_integrate_comp` (8 x 8 columns), `rbtrend_fog_apply_frag`
  (blend ONE, SRC_ALPHA; alpha ZERO, ONE); modules `rbtrend_fog_grid`
  (slice distances) and `rbtrend_fog_bindings` (the record's set 1 and its
  block, `RbtrendFog`, checked member by member at the layout). Stage 1's
  `rbtrend_lit_fog.slang` gains public `lit_fog_density()` (Math's
  DENSITY), `lit_fog_up()` and `lit_fog_ambient()` (taken out of
  `lit_fog_along()`, which now calls them: the same arithmetic) and makes
  `fog_phase()` public.
- **Set 0 from compute:** the inject shader joined the shaders the lit
  scene layout is made from (five; the same handle, its stages wider, no
  caller's layout changed); cull and inject bind `scene->material->set`
  at set 0, the scene's material taken by the compile's material pass.
- **Internals** (`src/render/rbtrendfog.cpp`, `rbtrendfog_internal.hpp`):
  a record a view: three R16G16B16A16_FLOAT 3D grids (the history two, the
  integrated one), each with a sampled and a storage view; its tiles' lists
  (a storage buffer of `RBTREND_BUFFER_WRITES_ONCE` made with no bytes:
  item 56's note above said WRITES_NONE, which `rbtrend_buffer_create`
  refuses; ONCE with no bytes is the header's "right for one only a shader
  writes"); two materials of the fog layout, one a way the history runs,
  each owning the 8,336-byte block (144 + 64 x 128; a volume with its
  bounding sphere), staged at the submit as every material's; two
  integrate sets. The apply's set (the depth, the integrated grid, the
  clamping sampler) comes from the scene's view-set records. The history
  module (`rbtrendhistory.hpp`) is used as it was, unchanged: its record
  keeps the scene and last frame's view-projection; what else last frame
  was drawn with (its eye, for the ray distance its slices are by; its
  range; which grid it wrote; its frames) is the fog's own, by the same
  index. `rbtrend_cmd_own_dispatch()` gains `groups_z` (1 by default) and
  pushes nothing for a size of 0. Same scene twice: refused while its
  record's materials are held by this compile or a list compiled and not
  yet submitted.
- **Shapes chosen alone (open for review):** slice i spans n (f / n)^(i /
  S) to n (f / n)^((i + 1) / S), the first from the eye (the draft's "from
  the eye to each slice's far side"); each frame's point is the jitter's
  share of its slice by distance (found: by slice coordinate it leans to
  the near end); the history is read at the froxel's middle (slice
  coordinate i + 1/2), not at the jittered point (found: reading at the
  point blends the neighbouring slices in each frame: a box of fog was
  3.8 to 4.4% off Math's, 0.26 to 0.64% after); while the history holds
  n frames fewer than its weight averages, it weighs n / (n + 1) (frames 4
  and 64 of a still view were 3.7% apart, 0.78% after); with a weight of
  0 every frame samples its slice's middle (no history to average a
  jitter); the jitter's step starts again with the history (a reset frame
  is a fresh record's, bit for bit); `_to_volume` refused unless its 3 x 3
  rows are unit and square to each other within 1e-3; a volume's noise
  place checked by Math with noise, and by Rend (finite, within Math's
  limit) always; the grid applies neither the scene fog's
  `end_distance` nor its `max_opacity` (beyond the grid the lit shaders
  do); every medium takes the scene fog's phase, suns and ambient share,
  the atmosphere's transmittance read at the froxel's point (stage 1's at
  the eye); a sun's shadow one comparison, no softness or normal offset,
  no cascade blend, its strength applied.
- **Not built, the draft against the code:** `RBTREND_FOG_MAX_TILE_VOLUMES`'
  "the compile's Debug report saying which tile": the cull runs on the
  GPU, and nothing it finds reaches the CPU in a compile. The header now
  says what happens (the later volumes left out, as the lit scene's tiles
  leave out lights). Alternatives: (a) in Debug, the compile counts each
  volume's screen rectangle in tiles on the CPU and reports a tile past
  15; (b) the cull writes an overflow count read back a frame later and
  reported at the next compile; (c) leave it as the lights' tiles are.
  Recommended (c), a card to decide. **Card A71 decided 2026-11-20 with
  the user (c):** left as built and as the header says (the later
  volumes left out, as the lights' tiles leave out lights); nothing a
  caller would branch on, nothing to build.
- **Tests, each seen failing first** (none passed against a stub that
  returned NOT_IMPLEMENTED): `rbtrend_contract_tests`' `fog_grid_contract`
  (4 failed checks: the NULL calls' results and messages); in
  `../rabbittustests`, a device's run, each doubled over the two devices:
  `test_fog_grid_uniform` (1), `_shadow` (2), `test_fog_volume` (1),
  `test_fog_meets_analytic` (1), `test_fog_history` (2), `test_fog_records`
  (27), `test_frame_allocations` (2), each at `rbtrend_fog_create`. Passing:
  test 7, T at each of 16 slices' far sides within 0.060% of exp(-density
  x distance); test 8, a roof's shadow in a sun-lit fog: the lit half
  0.1956 against the unshadowed 0.1995, the shadowed at most 0.0112; test
  9, a box (and the box broken by a WORLEY volume of 32 written with
  `rbtrend_fog_write_noise`) against Math's density marched along the
  columns the texel reads: 0.64, 0.39, 0.26% (0.56, 0.33, 0.23% noisy),
  its edge fading, 1.0000 past it; test 10, the grid to 100 and the sky's
  analytic fog past it against the sky's alone: 0.198% at worst; test 11
  (64 x 64 x 64 froxels), a still view's frames 4 and 64 0.78% apart, a
  view turned 10 degrees and climbing 0.2 a frame (the climb as
  `_space_shift`) 2.15% at worst (0.17% on average) from the still view of
  its pose after 8 frames, a jump with `_history_reset` the same as a fresh
  record's to 0.000000; test 12, every create, write_noise, send and
  compile refusal by its message, three scenes over two records, the
  three in one list refused (RBT_RESULT_LIMIT naming max_views), a scene
  twice in one list and in two unsubmitted lists refused, a scene with no
  view-set records refused; `test_frame_allocations`, 100 frames of a lit
  height fog and eight noisy volumes moved a frame, the history carried:
  none of ours on either device. `rbt_misuse_tests` and
  `test_destroyed_handles` regenerated (184 calls on the device with
  features, 174 on the bare one). Every ctest (51), Debug and Release;
  `rbtrend_gpu_tests` on both devices, Debug under Metal's API and shader
  validation (warnings as asserts) and Release.
- **Measured** (`test_fog_grid_measure`, `RABBITTUSTESTS_MEASURE`, Release,
  no validation; a 2560 x 1440 picture and depth, a sun and the ambient
  light, submit to idle, 50 runs, LIT_FOG against none): 160 x 90 x 64,
  +0.39 to 0.40 ms with 0, 8 or 32 volumes (the draft's estimate 0.25 to
  0.55 ms: met); 160 x 90 x 128, +0.52 to 0.58 ms. Memory as the draft's
  section 7: 22,118,400 bytes of grids a view (21.1 MiB), 921,600 of tiles,
  two blocks of 8,336, the noise volume (128 KiB at 32).
- **Owed:** D3D12 and Vulkan (the grid's compute over set 0, 3D storage
  images, the tiles' raw buffer); stage 3.
- **Card A72 decided 2026-11-20 with the user (a):** the grid's history
  blurs under motion (a view turning 10 degrees a frame and climbing:
  its top rows 9 to 16% off one frame's answer). The fog's integrate
  takes A70's rule (a variance clip and a floor weight, item 55) through
  the shared history module (`rbtrend_history_share`), measured, test 11
  held to one frame's answer again where the measurement allows. A70 is
  built, so it is ready to build; internal to Rend, no public change.
  **Built 2026-11-20 on the Mac (Metal; D3D12 and Vulkan never built).**
  The blend with the history is the inject's (`rbtrend_fog_inject_comp`),
  not the integrate's: each thread group (4 x 4 x 4 froxels) keeps this
  frame's values in group memory, and a froxel's reprojected history is
  clipped towards the mean of its 3 x 3 x 3 neighbours (those of the
  group) to mean +- gamma sigma per channel, the clouds' clip, then
  blended by the share as before. The gamma is the shared module's
  (`RBTREND_HISTORY_CLIP_GAMMA` 1.5, `RBTREND_HISTORY_CLOUDS_CLIP_GAMMA`
  renamed, the clouds' too), carried in the fog block's new `history`
  (16 bytes, checked against the reflection). A 3 x 3 box in the slice
  alone was tried and refused: a slice's froxels share the frame's jitter
  along it, so its spread is near 0 and the history was clipped to the
  one frame (`test_fog_volume`'s T 6.2 to 11.8% off Math's, past its 3%).
  The floor weight is the caller's `_history_weight` (the frame's weight
  never under 1 minus it, through `rbtrend_history_share` as before): a
  floor of A70's own (a quarter) over a caller's 0.9 would change what
  the parameter means (a header change) and isn't made (measured: test
  11's plain case 2.154 to 1.777%, against one frame's answer 2.664%).
  Test 11 (`test_fog_history`): a box of fog crossing the view turned 10
  degrees and climbing 0.2 a frame, after 8 frames, within 3.3% of the
  still view of the pose (seen failing first: 3.460%; now 2.909%, 0.196%
  on average from 0.223); the plain case 2.154% to 2.036% at worst (0.171
  to 0.155% on average), within its 3%; against one frame's answer
  recorded, not asserted: 3.163% either way, that frame being 2.4% from
  the converged view itself (its jitter), so the measurement allows no
  tighter bound; the still view's frames 4 and 64 0.779 to 0.753% apart.
  **Figures moved** (the clip biases a converged view a little where the
  fog changes inside a group): `test_fog_volume`'s even box at (24, 36)
  0.39 to 1.98% off Math's and (40, 28) 0.26 to 0.80%, the noisy box 0.33
  to 2.10% and 0.23 to 0.74% (the middle 0.64 and 0.56% as before), each
  within its 3%; every other fog test's figure as before. **Measured**
  (`test_fog_grid_measure`, Release, no validation, 2560 x 1440, three
  builds run in turn, three rounds): 160 x 90 x 64 +0.38 to 0.42 ms
  before, +0.60 to 0.76 ms with the clip (the in-slice box +0.43 to
  0.51); 160 x 90 x 128 +0.54 to 0.57 before, +1.00 to 1.06 ms (+0.63 to
  0.82). **For the user (card):** the clip costs 0.25 to 0.5 ms at
  1440p and 1 to 2% of a converged volume's accuracy for 0.55 points of
  error under fast motion; options: a, keep it (built); b, clip only
  while the view moves past a threshold (the share as before when
  still); c, take it out (A72's b). Recommended b. **Card A78 decided
  2026-11-24 with you: b**: the fog's inject clips its history only
  while the view moves (the camera's turn or move past a threshold the
  caller's fog desc states, no default), the share as before when still;
  measured again, test 11 held as now. Internal to Rend but the
  threshold, which follows the fog desc's pattern.

### 57. GPU particles (card L73, decided 2026-11-17)

Decided 2026-11-17 with you: particles simulated on the GPU in compute
over storage buffers Rend owns (spawn, forces, curl noise from a baked 3D
texture, collision with the depth buffer, death), sorted in compute for
blending, drawn as billboards, streaks, ribbons or meshes and lit by the
lit scene, never read back (Q1a); an effect a fixed set of modules with
parameters and curves over a particle's life, the caller's data, no
graph (Q2a); each system's particles relative to an anchor whose
eye-relative offset is given each frame (Q3a). Needs item 34's indirect
dispatch (Carrottus GPU's, a draft first), a compute sort, depth reads for
soft particles. To be drafted, for approval, with its caller's emitter.
Drafted 2026-11-18: `rbtrend_particles_draft.h`, with its caller's half
beside the caller's headers (version 1, seven critique passes), approved 2026-11-18 with the user, every question as recommended (Q1a the caller's spawn count each step, Q2a per-pixel lighting through the lit surface, Q3a a lagged alive count, Q4a the caller's noise volume).

**Built 2026-11-19 on the Mac (Metal, the M5 Pro; D3D12 and Vulkan never
built: no backend branch, every call through Carrottus GPU), the draft's
three stages in order (section 10), each green before the next: (1)
billboards and streaks, sorted and not, every module but MESH and RIBBON;
(2) meshes; (3) ribbons.** On Carrottus GPU's indirect dispatch (its
compute half, 2026-11-18): a core call, so nothing is refused for want of
a feature. Its caller's half (drafted beside the caller's headers) is not built:
it waits on the caller's transparency stage (the user's decision,
2026-11-18).
- **Public** (`render/rbtrendparticles.h`, new, the umbrella's 21st):
  `rbtrend_particles` (typedef'd in `rbtrendcommands.h` beside the other
  commands' handles, as the fog's is), `RBTREND_PARTICLES_DESC`,
  `rbtrend_particles_create`/`_destroy`/`_set_effect`/`_set_noise`/
  `_set_depth`/`_get_alive_count`/`_get_layout`/`_get_shader`, the enums
  `RBTREND_PARTICLE_SPACE`/`_DRAW`/`_SHAPE` (NONE refused), the frozen
  `RBTREND_PARTICLE_CURVE` and `RBTREND_PARTICLES_STEP` (120 bytes), the
  effect and its twelve modules, the shader list (the draft's numbers:
  `BILLBOARD_VERTEX` 0, `MESH_VERTEX` 1, `LIT_FRAGMENT` 2, `UNLIT_FRAGMENT`
  3), the four limits. **Numbers taken:** commands
  `RBTREND_COMMAND_PARTICLES_STEP` = 54 and `RBTREND_COMMAND_DRAW_PARTICLES`
  = 55; struct types 5047 `PARTICLES_DESC`, 5048 `PARTICLE_EFFECT`, 5049
  `SPAWN`, 5050 `FORCES`, 5051 `NOISE`, 5052 `COLLISION`, 5053 `WRAP`, 5054
  `COLOR_OVER_LIFE`, 5055 `SIZE_OVER_LIFE`, 5056 `FLIPBOOK`, 5057 `SOFT`,
  5058 `STREAK` (stage 1), 5059 `MESH` (stage 2), 5060 `RIBBON` (stage
  3); no result code. ABI: additions only, 164 lines over the three
  stages, none removed (`./check.sh --update-abi rabbittus` at each); no
  version raise (0.3 untagged, card X2). Misuse harnesses regenerated at
  each stage: `make_misuse_tests.py` (9 calls of `rbtrend_particles_*`
  with zeroes) and `make_destroyed_tests.py` with `dead_rbtrend_particles`
  (190 to 198 calls with a destroyed handle).
- **Set 0 untouched:** the step's compute reads the scene's own set 0
  (its block: the eye, the view-projection and its inverse, the view
  direction) as LIT_FOG does, adding no binding; every buffer of a system
  and its depth are its own sets.
- **A system** (`src/render/rbtrendparticles_internal.hpp`, `.cpp`): five
  device storage buffers made at create (the particles, 48 bytes each;
  two alive lists and the dead list, 12 bytes; the sort's two key lists
  and one value list, 12 bytes, and its counts, 64 bytes a group of 256;
  the record, 96 bytes; the arguments, 80 bytes, storage and indirect)
  and three materials of Rend's layouts (the simulation's set 1, the
  control's set 0, the draw's set 2), the effect's 496-byte block written
  into each by `set_effect` and `set_depth`; a readback of 16 bytes a frame
  in flight, mapped. About 72 bytes a particle (the draft's estimate 76).
  The noise and the depth are set into the materials (placeholders where
  none: a 1-texel SNORM volume, the lit placeholder depth); the draw's set
  holds the depth only for an effect with SOFT, so a pass without SOFT may
  test against it.
- **A step** (`rbtrend_cmd_particles_step`, `rbtrendcommands.cpp`): at the
  send each step checked in member order (NULL, destroyed in Debug, of
  another instance, a system twice, `_delta_time` 0 to 1, each vector
  finite), copied into the list; at the compile the systems' views (NOISE
  with no volume, COLLISION with no depth, a system's depth not the
  command's, an inverse view-projection for a collision), then each stage
  for every system before the next, the sets of all its systems made ready
  together (one barrier call a stage, the indirect arguments moved
  together), the pipeline bound once: the clear (a step's `_clear`, and a
  system's first step: no step submitted and none earlier in the compile),
  the simulation (indirect over the last count), the spawn (direct over
  the count, no more than the capacity), the arguments (one thread), the
  counts copied to each readback, and for sorted effects four passes of a
  count, a scan and a scatter (count and scatter indirect, the scan one
  group). Shaders: `shaders/rbtrend_particles_*` (seven compute, three
  draw, seven modules). Which alive list is current and how many are
  alive are the GPU's alone (the record), so a list compiled and never
  submitted leaves nothing behind but a step not counted.
- **The sort:** FidelityFX Parallel Sort's reduce-then-scan shape, a
  16-bit key (log2 of the distance from the eye in 1/2048 steps over
  2^-16 to 2^16, inverted), four passes of 4 bits; the scatter's rank
  stable by an inclusive scan across the group of sixteen 16-bit counters
  two to a word; pass 3 leaves the sorted values in the alive list itself,
  so the draw reads one list either way.
- **Atomics:** a group counts its appends, kills and spawned slots in its
  own memory and takes them from the record with one atomic each; a
  group's dead slots are taken by one decrement and, short, put back.
  Spawn numbers are taken the same way and run on across clears (0 only at
  a system's first step), so a ribbon's numbers never repeat.
- **The lagged count:** each submission marks the frame copy it writes
  with its serial and the step's number (`rbtrend_particles_submitted`,
  from `rbtrend_command_list_submit`); `get_alive_count` reads the newest
  copy whose serial `rbtrend_instance_read_completed()` has passed.
- **The draw** (`rbtrend_cmd_draw_particles`): inside a pass, the pipeline's
  set 2 the particles' layout, the system stepped earlier in the list or
  submitted, a depth set for SOFT, an index buffer set for a mesh; its set
  put at 2 (and looked ahead at by the pass's preparation, its arguments
  moved to INDIRECT_ARGUMENT before the pass), then
  `crgpu_command_list_draw_indirect` (six vertices an instance) or, for a
  mesh, `_draw_indexed_indirect` from the MESH range. The lit fragment
  shader is the lit surface's: `lit_shade()` split into
  `lit_shade_with_base_map()` (`rbtrend_lit_surface.slang`) so the
  flipbook's blended sample goes in; every other lit test's figures and
  hashes the same.
- **Ribbons:** the sort's key lists hold a table by spawn number modulo
  the capacity (the number and the slot); a killed ribbon particle is
  marked (its life -1); the draw draws a segment a number back from the
  newest over the window since the last clear (at most the capacity),
  each end looked up and drawn only when both are alive, each end's side
  across the strip's direction from its neighbours.
- **Tests** (rabbittustests' `rbtrend_gpu_tests`, Metal's validation with
  warnings as asserts and shader validation, Debug and Release, both
  devices; `rbtrend_contract_tests` headless): each first run against a
  stub whose calls returned NOT_IMPLEMENTED (the contract test 17
  failures; every GPU test failed at its kit or its system, 54 checks),
  stage 2's and 3's first against the stage before (not compiling: their
  structs undeclared; the refusals test expecting INVALID_ARGUMENT where
  RIBBON and MESH were UNSUPPORTED). `particles_contract` (commands, struct
  types, layouts, limits, shader numbers, every call with NULL);
  `test_particles_refusals` (T1: every desc, effect and module field, a
  module twice, a stranger in the chain, the views' kinds and formats,
  NOISE and COLLISION without their views and a depth not the command's
  at the compile, each step value at the send, the draw outside a pass,
  never stepped, a NULL system, a pipeline without the particles' layout,
  a destroyed system in Debug); `test_particles_spawn_count` (T2: 600 of
  1,000, 0 after their life, 50, 0 after a clear, each with its step);
  `_motion` (T3: semi-implicit Euler's place within a texel, 61 commands of
  one step in one list); `_world_space` (T4: 1,000 cells, the same texels;
  EMITTER moved 16 texels); `_wrap` (T5: 994 to 1,000 of 1,000 drawn over
  ten cells, none outside); `_collision` (T6: at rest on a wall drawn the
  same depth-tested as not, still a second later, 200 alive; none in front
  without COLLISION; none survives with life_loss 1); `_sorted` (T7: the
  near layer on top sorted either way round, the later one unsorted: seen
  failing first with the test's own mistake, a life the colour curve never
  reached); `_lit` (T8: the middle texel 128 against the face's 128; in a
  cascade's shadow over the left half, 0 and 128 against the face's 0 and
  128; its shadow half added after stage 3, passing at once: it tests code
  built in stage 1); `_soft` (T9: 127 against 128 at half, 255 at full; the
  pass's own depth refused); `_flipbook` (T9: frames 0 to 15 at lives k/16,
  the blend 40, 215); `_streak` (texels 24 to 39, two rows);
  `_noise_and_wind` (the volume's push and a drag to the wind, each to its
  texel); `_many` (T11: 200 systems in one command and in 200, the same
  800 texels); `_mesh` (T10: a square of 16 x 16 texels, a quarter-turned
  diamond, three on a row, the index buffer's absence refused);
  `_ribbon` (T10: 33 in a row lit from texel 6 to 57 with no gap, the
  oldest 16 dead and nothing drawn to them, after a clear no segment to
  the old trail; checked against a build without the dead mark, which
  draws to the dead and fails); `test_frame_allocations` (T12: twenty
  systems, every other sorted, ten spawned a frame, one step command and
  one pass, a lagged count read: 0 of ours in 100 frames); `test_destroyed_
  handles` with `dead_rbtrend_particles`; `test_particles_measure` (T13).
  Every Rabbittus ctest (53), the GPU tests and the workspace's other
  caller's suite green at each stage's end, Debug and Release.
- **Measured** (`test_particles_measure`, Release, no validation, the M5
  Pro, submit to idle less an empty list's (0.017 to 0.021 ms), the mean
  of 50 after 10, three runs of both devices; against section 6):
  - a step of 100,000 unsorted: 0.155 to 0.225 ms (estimated 0.03 to 0.05
    ms, three to five times over); sorted 0.320 to 0.386 ms (the sort
    about 0.16 ms for 12 dispatches);
  - 100 systems of 1,000 in one command: 1.17 to 1.30 ms, in 100 commands
    2.42 to 2.51 ms (estimated about 1.5 ms unbatched, 0.3 ms batched):
    batching halves it, no more, because Carrottus GPU's Metal compute
    encoder is serial (dispatches never overlap; its barriers are no-ops
    over Metal's hazard tracking), so the stage-by-stage order buys fewer
    binds and barriers, not overlap. About 12 microseconds a system a step
    (two dispatches and a copy), the draft's 5 a dispatch;
  - 10 sorted systems of 10,000 in one command: 0.93 to 1.11 ms (estimated
    about 0.75 ms);
  - 50,000 streaks of rain (two texels by twenty) over 2560 x 1440, the
    pass's own 0.23 ms taken off: lit 1.41 to 1.77 ms, unlit 0.44 to 0.55
    ms (estimated 0.2 to 0.4 ms lit, half that unlit: four to five times
    over; thin quads shade 2 x 2 blocks at half use, and the lit
    surface's whole shading runs per fragment).
  Group-aggregated atomics changed the 100,000 step by nothing measurable
  (0.16 ms before and after): the time is the dispatches' and the memory's,
  not the counters'.
- **Shapes chosen without a draft line** (open for the user's review):
  `_anchor` read as the anchor's place in the scene's space (the draft's
  "from the eye ... the scene's space"); a billboard faces the eye along
  the view's ray through it (from the inverse view-projection), so an
  orthographic view's billboards are square to it; the cone's apex at the
  emitter's origin; a mesh turned about its space's z; EMITTER space keeps
  particles in the emitter's frame and inherits no velocity (inherit is
  WORLD's); NOISE adds the volume's vector times the strength to the
  motion, not to the kept velocity; the sort's key the true distance; a
  RIBBON with `sort` refused (INVALID_ARGUMENT), FLIPBOOK not read by a
  ribbon; spawn numbers continue across clears; the draw's depth only for
  SOFT; no new result code.
- **Found:** (1) a shader that stores to one RWByteAddressBuffer at more
  than one width (`Store` and `Store3`) gets, in SPIR-V, one variable a
  width at the same binding, which Rend's reflection refuses ("resources
  aren't ordered by set and binding"): the clear and arguments store a
  word at a time (loads of mixed widths are fine). (2) Metal's validation
  refuses push bytes and a set bound for a function that reads neither
  ("unused binding in compute encoder"): the scan has no push range and
  the sort's passes leave set 0 unbound. (3) Carrottus GPU's Metal compute
  encoder is serial (above): overlap between systems needs a concurrent
  encoder with explicit memory barriers, a Carrottus GPU change (a draft).
- **Left:** D3D12 and Vulkan (card X25); the caller's half; shadows cast
  by particles and lighting through a volume (later stages, the draft's
  out of scope). Done 2026-11-20 (item 24, the alpha mode): the particle
  lit fragment shader shades as BLEND whatever its material's mode
  (`lit_shade_as()`: no cut-out, none of the view's screen-space inputs),
  and `rbtrendparticles.h`'s "Out of scope" line on alpha modes went.

### 58. A directional light given a place (card L78, decided 2026-11-17)

Decided 2026-11-17 with you (a, named in Rend's terms after comparing
Unreal, Unity HDRP, Godot and KSP): `RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED`,
appended to the light types (the light struct is frozen): a directional
light whose direction and irradiance are worked out per pixel from a
source place (an inverse square from a reference distance), its angular
radius the sun disc's (Unreal's Source Angle, HDRP's angular diameter).
It keeps everything a directional light carries: its cascades (at the
large-worlds cascade anchor), the sky's sun disc and colour, traced
shadows, and the atmosphere's shells (its direction at each shell's
centre, card A55's Q2a). Without a place a directional light is exactly
today's. Not a sphere light: an engine's sphere or point light has a
range, cube shadows and no sun's role. Needed where a light's source is
near the scene against its extent (a moon lit from a few thousand
kilometres off is 14 degrees wrong under one fixed direction). A draft
first, read with A55's.

**Open questions for its draft** (from the third critique round of A55's
and the large-worlds anchor's drafts, 2026-11-17):
- Does a placed light count among `RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS` and
  in the scene's sun count, as a directional light does?
- The sky-view's and transmittance tables' sun direction: per sun, at
  which place (the eye's, the shell's centre)?
- The reference-distance rule: the irradiance given at what distance, and
  what a place nearer than it means.
- A place inside a shell's top sphere: refused (A55's draft refuses it
  when a shell names the light).
- Along which direction its cascades are fitted: at the eye (a parallax
  crawl as the eye moves) or at a fixed place (off by the distance over
  the source's distance). Card A60 (open) covers the slide of cascades
  under any turning light, a placed one among them.

Drafted 2026-11-18: `rbtrend_placed_light_draft.h`, with its caller's half
beside the caller's headers (version 1, seven critique passes), approved 2026-11-18 with the user, every question as recommended; nothing built. It answers the five questions above
(section 4) and asks Q1 to Q3 (each recommended a); it turns on
`test_atmosphere_shell_placed_light` and the refusal of a placed light
inside a shell's top sphere (item 52's stage 4).

**Built 2026-11-19 on the Mac (M5 Pro, Metal; D3D12 and Vulkan written
through Carrottus GPU with no backend branch, never built: card X25), from
the approved draft, every question a.**
- `RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED` = 3 (`rbtrendlit.h`), the
  frozen `RBTREND_LIT_SCENE_LIGHT`'s `_position` its source, `_range` its
  reference distance D, `_radius` its disc at D; the comments the draft
  names [CHANGED] (the type's definition of "directional light", the
  light's members, `set_lights`' and `set_view`'s, the cascade fit's
  `light_direction`, the atmosphere's `sun_count`, the shell's `_lights`,
  LIT_SHADOW_MASK's, LIT_PATH_TRACE's and LIT_ATMOSPHERE_SHELLS'), and
  the clouds' layer `_lights` (a placed light's values at the eye, as the
  fog's draft says for the fog; shape chosen in the build).
- `set_lights`' checks in the draft's order, each its message: the type
  (`> DIRECTIONAL_PLACED`), a `_direction` of 0 on a spot or directional
  light only, a `_range` not above 0 on a point, spot or placed light
  ("a point, spot or placed directional light"), a placed light's
  `_radius` not above 0 or above 0.5; directional of both kinds counted
  against the four (`rbtrend_lit_type_directional()`,
  `rbtrendlit_internal.hpp`); `rbtrend_lit_scene_directional_slot` and
  `rbtrend_lit_kind_fits` take both, so LIT_SHADOW_MASK
  (`rbtrendcommands.cpp`, `rbtrendraytracing.cpp`), the shells and the
  clouds (send and compile) take a placed light.
- The scene's directional block gains `place_reference` (the source, D;
  D 0 for a light from one direction) and `placed_radiance_radius` (the
  radiance at D, R = D sin(radius) in double): 32 bytes a light, the block
  16,336 bytes of Vulkan's guaranteed 16,384. Each commit
  (`rbtrend_lit_scene_resolve_placed`, first, so `set_lights`, `set_view`
  and every other setter) writes a placed light's values at the eye into
  `direction_versine` and `radiance_disc` in double, rounded once
  (`rbtrend_lit_placed_values_at`): the sky's disc, the sky-view, the
  aerial volume, the environment fill, the atmosphere's frame, the fog
  and the clouds read them unchanged.
- One shader function, `directional_at()` (`rbtrend_lit_scene.slang`): a
  light from one direction its stored values bit for bit and an unbounded
  reach; a placed one's direction, radiance (D / d')^2, versine
  (q^2 / (1 + sqrt(1 - q^2)), capped at 1 - cos 0.5) and reach d - R, the
  distance scaled by its largest part so no square overflows. Read by the
  lit surface (with `lit_atmosphere_sun_transmittance_toward()`, the
  transmittance along the point's own direction; the old call kept for
  the eye's readers), LIT_SHADOW_MASK (its rays to min(`_max_distance`,
  reach)), the path tracer's next event (min(1e30, reach)) and its misses
  (each disc from the ray's origin), and LIT_TRACE_REFLECTIONS' hits.
- Shells: a placed light's direction and illuminance at the shell's
  centre in double (`rbtrend_lit_placed_values_at`); a source inside or
  on the top sphere refused at the send and at the compile, naming the
  shell, the slot and the light, with the distance and the top.
- ABI: one enumerator added, no break (`abi.txt`); no version raise
  (0.3 untagged, card X2). Misuse harnesses regenerated, unchanged.
- Tests (rabbittustests; Metal's validation with warnings fatal, Debug
  and Release, both devices; each seen failing first, every one refused
  at set_lights before the build: "light 0 has type 3"):
  `test_lit_placed_refusals` (T1), `_matches_directional` (T2: 0 apart
  at the texel read), `_sides` (T3: the rule within 4.9e-4, the turn
  0.0476 rad across 4 m at 40), `_inverse_square` (T4, T5: 0.25 and
  0.0625 exactly; inside and at the surface the same, 0.07% from
  (1 / R)^2), `_sky` (T6: every disc texel 0 apart from the ordinary
  light's, the disc's texel 204 and 205 at D and 2D, 9 and 5 texels; the
  sky-view the same bits), `test_atmosphere_placed_sun_through_air`
  (T11: 9.2e-4 at 1e4 radii, a half float's step; the terminator's far
  texel 0.0744 of its bare light against Math's 0.0743, the near one
  black), `test_lit_placed_cascades` (T12: 31 shadowed texels each, at
  most 1 apart), `test_rt_shadow_mask_placed` (T8: shadow 0 / lit 108;
  beyond the source 126; penumbra 8 texels at 0.2, 4 at 0.05, 8 under an
  ordinary light of the rule's disc), `test_rt_path_trace_placed` (T9:
  0.9994 against 1.0000; the reflected square 0.1625 lit from its side,
  0.0000 under a light from the eye's side), and
  `test_atmosphere_shell_placed_light` (T10: the brightest columns 49 and
  14 about the middle 32, an ordinary light's 49 and 49; the refusals);
  `test_frame_allocations` (T13: 100 shell frames and 100 traced frames
  under a placed light, the eye moved each frame: 0 of ours each);
  `test_lit_light_refusals`' type refusal moved to 4;
  `rbtrend_contract_tests` (the value 3). T7: every other test's printed
  figures the same as before the build (Debug and Release outputs
  compared, figures for figures). `rbtrendreflect_tests`' two offsets in
  the scene block re-recorded (the directional entry 112 bytes).
- Measured (T14, `test_lit_placed_measure`, Release, no validation, the
  M5 Pro, submit to idle, the mean of 50 after 10, a lit face over
  2560 x 1440; two runs, each on both devices): one ordinary light 0.650
  to 0.743 ms (one cold first reading of 0.962), one placed light 0.643
  to 0.730 ms: the difference, -0.012 to +0.062 ms, is inside the run's
  noise, as the draft's estimate (under 0.01 ms) has it.
### 59. Block-compressed textures: created, uploaded with their levels, read back (card A54, decided 2026-11-17)

Decided 2026-11-17 with you: `rbtrend_block_formats_draft.h` approved,
every question as recommended: `rbtrend_texture2d_update_levels()` writing
whole levels of a range of layers in one call, one staging and one
transition (Q1a); a pointer and a size per level, so a file's levels are
passed in place (Q2a); the device's block-format feature checked in
Rend's create with its own message (Q3a); block formats read back as they
are written (Q4a); a partial region in texels, block-aligned or reaching
the level's edge (Q5a). Every pitch and offset read from Carrottus GPU's
block query and copy layout; the scalar-type range test fixed so a format
numbered 1000 or more is never taken for a video one. After Carrottus
GPU's block formats (card S48). No version raise while 0.3 is untagged.

**Amended by the third critique round, 2026-11-17 (the draft's version
9, approval kept):** `update_levels`' results in order, its ranges by
subtraction, an overflow bound of 2^45 (S33's 32768); every copy's
`CR_RESULT` read, the first refusal reported through `rbtrend_fail_cr`
after the opening transitions are closed; a range of subresources
transitioned with one barrier per run of the same layout (today
`rbtrend_immediate_on_texture` goes per subresource); updates and
readback of planar formats refused (a listed break); the scalar fix names
the 28 ASTC formats only; BC4 and BC5 SNORM's constants 0x81 and 0x7F;
the cube test's faces in different layouts. Its copies use Carrottus
GPU's new row-pitch alignment query (S48's third round).

**Built 2026-11-17 on the Mac (Apple M5 Pro, macOS 27, Metal), from the
draft as approved (version 9), over S48 as built the same day (uncommitted
in carrottus). D3D12 and Vulkan written, not compiled or run (card X25).**
- **Public (`rbtrendimages.h`):** `RBTREND_TEXTURE_LEVEL_DATA` (frozen) and
  `rbtrend_texture2d_update_levels()`; the comments of the desc's
  `format`, create, update, get_data, generate_mips, resize and 3D create
  as the draft has them. One wording differs from the draft's: the
  staged row's pitch is named as a multiple of
  `crgpu_image_get_buffer_copy_row_pitch_alignment()` (and the block's
  bytes), not `crgpu_image_get_buffer_copy_size()`'s; the two agree for
  every whole level of a power-of-two block, and the query's is what a
  region narrower than its level stages at. The new call's thread
  sentence is plain prose, not a `Thread:` line (it is no exception).
- **Internals:** `rbtrend_texel_bytes` gave way to the texel block
  (`crgpu_format_get_texel_block()`); the region check takes the block
  (offsets on it, width and height on it or reaching the level's edge,
  data in rows of blocks), each its own check and message; staging re-packs
  rows of blocks at the pitch step (the block's bytes and the row
  alignment's least common multiple, so a 12-byte texel on D3D12 is
  staged at whole texels: the old pitch, `GetCopyableFootprints`' 256-byte
  `RowPitch`, isn't, and its row length of `pitch / 12` texels implied 252
  bytes; reasoned from the code, not run), one `memcpy` where
  the pitch is tight; `buffer_row_length` is `pitch / bytes * block
  width`. Every copy's `CR_RESULT` is read, the update's and readback's
  too, the first refusal returned through `rbtrend_fail_cr` naming its
  level and layer after the closing barriers are recorded; a readback
  waits only once its copy is recorded. `rbtrend_immediate_on_texture_range`
  (`rbtrendimages_internal.hpp`) moves a range with one barrier per run
  (levels in a row of one layout, then layers in a row found alike), and
  the one-subresource form calls it. `rbtrend_block_format_feature()` is
  the one thing Rend knows of formats: which feature a BC or ASTC value
  needs. `rbtrend_format_scalar_type()` names the 28 ASTC formats float.
  `generate_mips` refuses a block format with `RBT_RESULT_UNSUPPORTED`
  before its usage checks: the draft said it already did, but its usage
  check (no `RENDER_TARGET`) came first with `INVALID_ARGUMENT`.
- **Tests, each seen failing first:** the header's new call failed to
  compile in `rbtrend_contract_tests` (5 errors); with a stub returning
  `RBT_RESULT_NOT_IMPLEMENTED`, `rbtrend_contract_tests` failed 3 checks,
  and `rabbittustests`' `test_block_texture_upload` 714,
  `test_block_texture_sampling` 36, `test_block_texture_contract` 37 (each
  refusal reached Carrottus GPU and came back as its code) and
  `test_frame_allocations` 2. With everything but the scalar fix, the
  contract test's ASTC 4x4 view as BLOOM's lens dirt was refused ("format
  1000, which a shader doesn't read as floats"). Mutations: a row length
  one block too wide failed 257 checks, and the sub-range's copies made at
  layer 0 in place of `base_layer` 84.
- **Run:** every ctest, Debug and Release, and under ASan with UBSan and
  under TSan (43 each, every one passing, no report); `rbtrend_gpu_tests` Debug under Metal API and shader
  validation (errors and warnings as asserts) on both devices, and under
  `leaks --atExit`: 0 leaks without Metal's debug layer; with it, 12,
  every one an `MTLDebugSamplerState` wrapper in the debug layer's object
  cache from samplers `test_material_copy` and the tests' context make
  (10 with that test alone, 0 for it without the layer), none from A54's
  code. On the device with the tests' features: 42 block
  formats (14 BC, 28 ASTC) written and read back byte for byte (a 2D
  chain of 4 x 3 blocks and four regions of its level 0 and its last
  level, a 3-layer array and levels from 1 of layers 1 and 2, a cube whose
  faces 0 and 3 were written first, face 3 sampled), ASTC 5x4 20 x 8 level
  by level, an RGBA8 cube; 36 sampled exactly (BC1, BC4 and BC5 in UNORM
  and SNORM, BC7, ASTC, sRGB forms); 100 frames of an RGBA8 and a BC7
  chain each through update_levels, none of ours. On the device with no
  optional features: every block create refused with Rend's message, the
  RGBA8 cube and frames as above.
- **Measured** (Release, `RABBITTUSTESTS_MEASURE=1`, the M5 Pro, the mean
  of 20 runs): a 2048 chain as RGBA8 (22.4 MB) 1.79 ms of CPU through one
  update_levels against 1.97 ms through 12 updates (2.90 and 3.07 ms to
  the batch having run); BC7 (5.6 MB) 0.54 ms (1.01 ms), ASTC 4x4 0.49 ms
  (0.80 ms). Staging is the chain's own bytes on Metal (rows tight). On a
  unified-memory Mac the copy into staging is most of it; one call saves
  little time there, and its gain is one batch and one transition.
- **Owed:** Windows (D3D12: BC on every adapter, ASTC refused; the 256-byte
  pitch, which pass 6 names: a level of 4 BC7 blocks a row staged at 256;
  Vulkan on the Iris Xe and A2000) and Fedora (Vulkan), where barriers and
  layouts are real and the range's runs are seen by the validation
  layers; Metal makes neither visible. Not reachable by a test on Metal: a
  planar format's refusal (Metal makes no planar texture) and a copy's
  refusal by Carrottus GPU (Rend's checks come first).
- **Found, not in the draft (for a card):** a draw refused at compile
  inside a pass after `rbtrend_cmd_apply_vertex_buffers` has bound its
  vertex buffers (the binding sets' checks, `rbtrend_cmd_apply_binding_sets`,
  come after) leaves Metal's encoder with an unused vertex binding, which
  Metal's validation with warnings as asserts stops the process for
  ("unused vertex binding in render encoder at Buffer index 0"): seen
  drawing a BC7 view through a shader that reads unsigned integers. The
  scalar-type test went through BLOOM's lens dirt instead.

### 60. A material's binding sets kept per frame in flight (card L79, decided 2026-11-17)

Decided 2026-11-17 with you (a now, b when A15's item 33 is built): a
material keeps one binding set per frame in flight, and a changed
resource (a map swapped to another texture) is written into the set no
frame in flight reads (`crgpu_device_update_descriptor_sets`), so a swap
allocates nothing after the material's first frames; today it builds the
whole set again, about seven allocations a swap. Rend's internals; a draft
only if what `rbtrendmaterial.h` says a rebuild costs changes. Its test: a
swap a frame in the no-allocation tests, at zero. Bindless (item 33)
supersedes it where it exists.

**Built 2026-11-17 on the Mac (the session card L79 names, its step 0), Rend's
internals, no draft:** a material keeps up to `RBTREND_MAX_FRAMES_IN_FLIGHT
+ 1` spare sets, each with the submission serial it was let go at
(`rbtrend_material_t::spare_sets`, `spare_serials`). A change rewrites in
place a spare the frame fence has passed (`rbtrend_binding_set_rewrite`,
internal: `create`'s checks, the same frame sets and views, the writes from
the stack in batches of 16), takes it, and lets the held set go as a spare
at the current serial; while it has fewer spares, a new set; past that,
`rbtrend_binding_set_update` as before. Lists compiled with the set let go
of are stale (`rbtrend_instance_forget`), as a rebuild made them, so
`rbtrendmaterial.h`'s contract is unchanged; its sentence on what a
rebuild costs now says the rewrite (open for review: the card asked for a
draft if that sentence changed, and only the cost changed). `create` lost
its two scratch callocs on the way (the writes from the stack).
`rbtrend_gpu_tests`' `test_material_swap_in_flight`: a busy queue, then
eight swaps through three textures in one frame, each into its own target,
all right; seen failing (black targets) with the serial check taken out,
and with two textures it hadn't (a spare rewritten with the colour it
held); then 100 frames a swap each, none of ours (700 before, seen
failing first). The first caller's no-allocation test, a map swapped every
frame, held to zero (705 in 100 frames before). Every Rabbittus ctest, the GPU tests on
both Metal devices under validation and under `leaks` (0). Windows and
Linux owed.

### 61. Writing some bytes of a buffer while submitted work reads others (card A62, decided 2026-11-17: b)

Found 2026-11-17 by a caller's third critique round: `rbtrendbuffers.h` forbids any write to a buffer while submitted work that uses it is unfinished, so a large `RBTREND_BUFFER_WRITES_MANY` pool drawn every frame can never be written. The draft is `rbtrend_buffer_ranges_draft.h` (2026-11-17, version 1, seven critique passes, approved below). It drafts the card's recommendation b: `rbtrend_buffer_write_after()` names the serial of the last submission that may read the bytes, and is refused in every build with a new `RBT_RESULT_REND_IN_USE` while that submission is unfinished. Two getters, `rbtrend_instance_get_last_submission()` and `rbtrend_instance_get_completed_submission()`, make the instance's submission serials public. Rend tracks its own staging copies in flight per range. Q1 adds an option d, a queue write in the style of WebGPU's `writeBuffer`.

**Approved 2026-11-17 with the user, every question as recommended (Q1 b,
not d).**

**Built 2026-11-18 on the Mac (Apple M5 Pro, Metal); D3D12 and Vulkan
never built** (no backend branch in it; the staged path's real run is a
device with memory of its own, the RTX A2000). As the draft names:
- **Public:** `RBT_RESULT_REND_IN_USE` (5005); `rbtrendcontext.h`'s
  "Submissions and their serials", `rbtrend_instance_get_last_submission()`
  and `rbtrend_instance_get_completed_submission()` (both `Thread:`, and
  listed among "Threads"' exceptions); `rbtrendbuffers.h`'s rule paragraph,
  "Writing while the buffer is in use", `rbtrend_buffer_write_after()`, and
  `rbtrend_buffer_write()`'s rule line. `./check.sh --update-abi`: three
  functions and the code added, no break.
- **Inside:** `rbtrend_instance_read_completed()` (`submit_serial` read
  first, the fence clamped to it; the getter and every new check use it;
  `rbtrend_instance_collect()` and `rbtrend_pool_reclaim()` not moved to
  it); the instance's `completed_serial`, written only by the write;
  `copies_in_flight` on each store, an `eq::array` of
  `rbtrend_copy_in_flight {offset, size, serial}` (`RBT_PLAIN_DATA`) in
  `src/render/rbtrendcopies_internal.hpp` (reserve, append that refuses
  rather than allocate, prune from the front, first overlap); its room
  reserved by `rbtrend_buffer_store_write_staging()` (size + uploads + 1,
  `CR_RESULT_OUT_OF_MEMORY` with nothing written otherwise);
  `rbtrend_buffer_store_uploads_submitted(store, serial)` prunes (reading
  the fence only when something is kept) and appends; the store's destroy
  frees it.
- **Shapes not in the draft, recorded:** a per-frame buffer is refused by
  its desc's `per_frame`, not by `part_count > 1`, so the refusal is the
  same at `frames_in_flight` 1 ("the buffer has per-frame copies:
  rbtrend_buffer_write() writes the current one"); the serial past the
  last taken reads "last_reader N is past the last submission the
  instance has made, M"; the copy refusal also names the earlier write's
  bytes; the submit reads the fence only with copies already in flight.
  Q6's sentence is in `carrottus/include/crgpu.h`, `crgpu_buffer_map()`.
- **Tests, each seen failing first** (none compiled before the code):
  `rbtrend_contract_tests` (the three calls with NULL, the code's value);
  `rbtrendcopies_tests` (new: append and prune, overlap against touch, a
  reserve keeping 100 rounds allocation-free, 20,000 steps against brute
  force); `rbt_misuse_tests` regenerated (19 lines). In `rabbittustests`:
  `test_submission_serials`, `test_buffer_write_after` (a second instance
  of three frames in flight, 40 frames, every write accepted with no wait
  of the test's, every target read back), `_staged`, `_contract`,
  `_cost`, `test_frame_allocations`' pool scenario and
  `test_destroyed_handles` regenerated (180 calls on the device with
  features, 170 on the bare one). Every ctest (47) passes, Debug and
  Release; `rbtrend_gpu_tests` passes on both devices, Debug and Release,
  under Metal's API and shader validation with warnings as asserts; the
  workspace's other callers' suites pass, Debug and Release.
- **Runs:** the in-use refusal was reached on every run: 20 of 20 writes
  naming a running submission refused on both devices, Debug and Release,
  and the staged device's second write over a copy in flight refused each
  time. `test_frame_allocations`: 100 frames of a write by its bytes and a
  read, none of ours, direct (the device with
  `CRGPU_FEATURE_DEVICE_MAPPABLE_MEMORY`) and staged (the bare one).
- **Measured** (Release, the M5 Pro, three runs): a 24 KiB write, 4,000
  each, `rbtrend_buffer_write()` then `rbtrend_buffer_write_after()` naming
  a finished submission: direct 213 to 242 ns then 197 to 214; staged 226
  to 258 then 204 to 226 (the second loop runs warm; the check is within
  the noise). Staged, 16 KiB writes into the gaps between 48 of Rend's
  copies in flight (768 KiB of staging bytes counted, the copy still
  running): 232 to 273 ns a write, the 48 ranges checked and the fence
  read each time. No staging memory is added: the count is ranges of the
  staging buffer a `MANY` store already had.

### 62. Cascades anchored in a space that doesn't move (card L66, built 2026-11-17)

Built 2026-11-17 on the Mac (the session card L66 names, its first step) from the
approved `rbtrend_large_worlds_draft.h` (version 3): `RBTREND_LIT_CASCADE_ANCHOR`
(struct type 5038), chained once onto `RBTREND_LIT_CASCADE_FIT`, its
`scene_origin` the scene origin's place in a fixed space (`RbtDVec3`;
`rbtrendlit.h` includes `../rbtmath.h`). With it, each cascade is moved by
whole texels where the fixed space's origin lands: its landing worked out
from the fit's own float matrix, each product made exact as a pair
(Veltkamp's split and Dekker's product, no fused multiply-add) and summed
as a double-double, so the texel's fraction is exact however far the
origin is. Without it, today's bits (test 1). Refused: an axis not finite,
each its own check; the anchor twice (`RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN`);
any other struct in the fit's chain. `rbtrend_contract_tests`' `lit_cascade_anchor`:
a fixed point's texel under an eye moving 1 cm a fit for 600 fits drifts
7.6e-5 of a texel anchored at the origin, 1e7 m and 4.5e12 m out (0.5,
fully loose, without the anchor); at the origin the same snap as today's,
within 1e-6 of a texel; refusals; an anchored fit 1e7 out recorded
(`tests/data/rbtrend_contract/anchor_far_4.inc`, within FIT_ULPS). Recorded,
not asserted (card A60): a light turning 1.2e-6 radians a fit, the fixed
point at the view 0.00014 texels a fit, 200 m away 0.009, 250 km and 1e7 m
away a phase effectively random each fit (0.24 to 0.31 on average): the
slide A60 describes. Every ctest passes, and `rabbittustests` on Metal
under validation (other callers' cascades unchanged). Windows and Linux
owed: test 6's bits within FIT_ULPS, card X25. The first caller's renderer
chains it on every cascade fit, its scene origin the frame's eye.

**Card L82, 2026-11-20 on the Mac (Metal; D3D12 and Vulkan never built,
no backend branch): a scene a whole number of units away lands on the
same texels.** Found from a caller's test (a shadowed scene at x 0, 100 m
and 1e7 m under a light straight down: 773 and 712 pixels a shadow's edge
apart): the anchored grid is fixed in the fixed space, and a texel of any
size (2 radius / map_size, the radius rounded up to a sixteenth) put a
scene moved 100 units at another fraction of a texel. Two changes inside
`rbtrend_lit_fit_cascades()`, no public change: (1) each cascade's texel
is made a whole fraction of a power of two of the units, the smallest
power at least eight texels (`period`, by `frexp`/`ldexp`, so no libm
rounding), the radius grown to it (the texel at most an eighth larger)
and the projection's x and y scale set exactly, 2 n / (period map_size)
(exact for a map size that is a power of two); (2) the light's view built
from the light's direction itself, glm::lookAtRH's arithmetic with its
forward `-light` rather than the difference of two places, so a light
along an axis has the scene's axes exactly (the difference had left the
axis 1 ulp off, which 1e7 units turned into 0.15 of a texel). The grid
then repeats every `period` along the light's view's axes: a scene moved
by a multiple of it (any whole number of units while `period` is at most
one) under a light along an axis lands as it did; under any other light,
or by another distance, a fraction of a texel remains, as before.
`rbtrend_contract_tests`' `lit_cascade_anchor` test 8 (new): a view
anchored 100 units and (1e7, 0, -1e7) away, a light straight down, three
scene places' texels in each cascade 0 of a texel apart (seen failing
first: 0.481). Every fit's matrices moved, so the recorded tables were
recorded anew once by name with `--print-fits` (the old `--print-anchor`,
now printing every table): `s_fit_perspective_4`, `s_fit_parallel_3`,
`s_fit_reversed_2` and `anchor_far_4.inc`. Recorded figures now: a fixed
point's drift under a moving eye 7.09e-5 of a texel (7.6e-5 before) at
the origin and 1e7 out, 6.86e-5 at 4.5e12; a turning light's slide a fit
0.000128 texels at the view, 0.0081 at 200 m, 0.236 at 250 km and 0.411
at 1e7 m. Every ctest (53) Debug and Release, `rbtrend_gpu_tests` on both
device configurations Debug and Release under Metal's validation.

### 63. The view-set records sized by the caller (card A67, decided 2026-11-17)

Decided 2026-11-17 with you: `rbtrend_view_sets_draft.h` (version 1)
approved, every question as recommended (Q1 b, Q2 to Q7 a). Built
2026-11-17 on the Mac (Metal, the M5 Pro; D3D12 and Vulkan never built:
no backend branch), with its caller's half the same session.
- **Public:** `RBTREND_LIT_SCENE_DESC {base; max_view_sets}` (struct type
  5040), `RBTREND_LIT_SCENE_MAX_VIEW_SETS` 256,
  `rbtrend_lit_scene_create(instance, desc, out_scene, out_error)` (a
  break: the desc second; checks in order: instance NULL, destroyed; desc
  NULL; its base; max_view_sets above 256; out_scene NULL, each its own
  message). The four commands' text says which sets each takes; the
  compile's @return names RBT_RESULT_LIMIT and the records as something
  it changes outside the list. `./check.sh --update-abi`: the create's
  signature the one break; the struct, its type and the macro added.
- **Inside:** each scene keeps `max_view_sets` records (120 bytes each,
  one calloc at creation, none for 0; `src/render/rbtrendviewsets_internal.hpp`),
  serving LIT_SSR, LIT_TRACE_REFLECTIONS (its own 16 on the ray-tracing
  state merged in), LIT_REFLECT and LIT_ATMOSPHERE_SHELLS. The hand-out
  (`rbtrend_view_set_get`, now in `rbtrendcommands.cpp`, taking the list
  and the scene): a record over the same layout and views hands out its
  set; else, under the instance's release_mutex, `rbtrend_view_set_choose()`
  picks the first empty record or the least recently handed out (a
  per-scene counter, not frame_number) whose set is not held - in the
  compiling list's used_handles or any compiled, unsubmitted list's; none:
  RBT_RESULT_LIMIT naming the command (a shell by its index), the count
  and the field, nothing made ("command 8: LIT_ATMOSPHERE_SHELLS: shell 4
  needs a view set, and the scene's 4 (max_view_sets,
  RBTREND_LIT_SCENE_DESC) are all held by this compile and lists compiled
  and not yet submitted"); 0 records refused with its own message. Every
  hand-out puts the set in the compile's used_handles. The shells' double
  fetch removed: each shell's set taken once and kept in the list's
  reused `shell_sets` for the draws. The instance's and the ray-tracing
  state's caches and their destroys are gone; the scene's destroy
  destroys its records' sets (through the release queue).
- **The hazard (draft section 1), as found:** past 16 distinct sets in one
  list the old cache evicted a set the compile already bound, retired at
  an earlier submission's serial. On the M5 Pro it never showed in a
  picture (64 shells in one list matched eight lists of eight before and
  after), but the second compile of 64 shells made 393 allocations of
  ours (every set past the 16th made again in the draw loop); now 0.
- **Tests** (each seen failing first where it could; rabbittus:
  `rbtrend_contract_tests`' `lit_scene_desc` and its NULL-instance check,
  and `rbtrendviewsets_tests`, a new ctest, 44 now, both failing to
  compile before; `rbt_misuse_tests` regenerated. rabbittustests:
  `test_view_sets_refusals`; `_limit` (five atmospheres in four records
  compiled before, refused now); `_eviction` (six lists each replacing the
  records the list before binds, submitted without waiting: every picture
  equal to its group drawn alone, no validation message; it could not
  fail before, 16 records held both groups); `_across_lists` (B refused
  while A holds 3 of 4, compiled before; A never stale; 20 lists holding
  every record destroyed on Rabbittus Jobs' pool thread during B's
  compile, then B compiled); `_many_shells` (64 against eight lists of
  eight, the pictures equal; the second compile 0 allocations of ours,
  393 before); `_zero` (LIT_SSR, LIT_REFLECT, LIT_ATMOSPHERE_SHELLS and,
  where the device traces (the M5 Pro's device with the features does),
  LIT_TRACE_REFLECTIONS refused naming max_view_sets, each compiled
  before); `test_frame_allocations` (shells of two atmospheres at 2 records;
  reflections at frames_in_flight + 2: 0 of ours);
  `rbtrend_destroyed_calls.inc` regenerated. No -asan or -tsan run (no
  new build tree): the two-thread case ran under neither.
- **Found, not fixed (Carrottus GPU's Metal backend, not Rend's to
  change here):** a list compiled and never submitted (a refused compile,
  or a compiled list destroyed or reset unsubmitted) keeps its Metal
  command buffer's place in the queue: the backend gives it back only
  when committed (`crgpumetalcommand.m`'s own comment at
  `crgpumetal___internal_acquire_command_buffer`), and a queue hands out
  a fixed number, so enough of them in one process block a later compile
  for good. The two-thread test hung at 20 rounds in the whole suite (a
  compile waiting in `-commandBuffer`), alone it passed; it ran 3.
  Rend could commit an abandoned recording empty, or Carrottus give a
  reset list's buffer back: a card for you.
- **Fixed in Carrottus GPU, 2026-11-18 (card S53, decided a;
  `../carrottus/CRGPU_DEVICE_TODO.md`, "A recording dropped without a
  submit"):** the Metal backend makes each encoder in a pool of its own,
  takes a list's command buffer at each begin, and lets go of a recording
  (open encoder ended, buffer released, never committed) at the list's
  next begin, its allocator's reset and its destroy. Nothing changed in
  Rend. The two-thread test runs its 20 rounds again (a frame a round,
  as before), and rabbittustests' `RUN` no longer drains a pool round
  each test (`REFUSAL_POOL_PUSH` and its uses removed): the whole GPU
  suite passed on both devices under Metal's API and shader validation
  with warnings as asserts, Debug and Release, three times each in a row
  (the M5 Pro; rabbittus' 53 ctests Debug and Release with it).
- **Measured** (Release, no validation, `RABBITTUSTESTS_MEASURE`,
  `test_view_sets_many_shells`): the compile of a list of 64 tables'
  sends, LIT_ATMOSPHERE, a clear and 64 shells, every set found, 0.044 to
  0.045 ms (means of 50 after 10). Not measured before (the old code was
  replaced first; the draft's test 9 asked for both).
- **Ran:** rabbittus' 44 ctests Debug and Release; rabbittustests all
  passing, Debug and Release, both devices, under Metal's API and shader
  validation with warnings as asserts; the workspace's check.sh
  (abi, doxygen, conventions, caller-names, struct-bases, plain-data,
  naming and the hosts' check); the first caller's half built and run
  with it, recorded in that caller's own TODO.

### 64. A scene's cover: snow and wetness on lit surfaces (card L74's Rend half, decided 2026-11-18)

Decided 2026-11-18 with you: `rbtrend_weather_draft.h` approved, every
question as recommended (Q1a one set of values a scene, varied per pixel
by slope and a snow line; Q2a a material without a response takes no
cover; Q3a the traced paths without it). Built 2026-11-19 on the Mac
(Metal, the M5 Pro; D3D12 and Vulkan never built: no backend branch).
The draft's T7 (the layered surface) was built with item 53 on
2026-11-19 (`test_layered_cover`). Its caller's half, drafted beside the
caller's headers, is not built here.
- **Public** (`rbtrendlit.h`): `RBTREND_LIT_COVER_FRAME` (NONE refused,
  ROUND, FLAT), `RBTREND_LIT_COVER` (struct type 5045),
  `rbtrend_lit_scene_set_cover(scene, cover, out_error)` (NULL for none;
  checks in the struct's order, each its own message),
  `RBTREND_LIT_COVER_RESPONSE {base; cover; porosity}` (5046) chained
  onto `RBTREND_LIT_MATERIAL_VALUES`; the "Shading" paragraph, and
  `rbtrend_lit_material_set_values()`'s and `_get_values()`'s comments.
  ABI: additions only (`./check.sh --update-abi`), no version raise (0.3
  untagged, card X2). Misuse harnesses regenerated (one new call each).
- **The scene's block** was 16,336 of the 16,384 bytes Vulkan
  guarantees; the cover takes the last 48 (three float4s, appended:
  nothing moved), so **the block is now full**: the next scene setting
  needs room made first (a second scene block binding is impossible, set
  0's sixteen are taken; push constants, a material-layer route or
  repacking are the alternatives). The draft's fields don't fit 48 bytes
  as floats (14 scalars after folding), so set_cover folds them on the
  CPU and packs two lanes: `cover_frame` (a round frame's centre, or a
  flat frame's unit up; w the snow line's height along it, a flat one's
  origin folded in in double), `cover_snow` (slope_begin, 1 / the slope's
  span (3e38 where begin equals full: a step), 1 / the band, the amount),
  `cover_wet` (the wetness, its roughness, then the snow's colour and
  roughness as four half floats, rounded to nearest even on the CPU,
  `f16tof32` in the shader: within 1/2048, said in the header). Flags
  COVER 8192 (the draft's 4096 had gone to the fog) and COVER_ROUND
  16384.
- **The material's block:** the response in the three spare floats after
  `maps` (`cover_amount`, `cover_porosity`, `cover_responds`), its size
  unchanged (288). `set_values` writes all three every time: values with
  no response chained take a response set before away. `get_values`
  fills a response the caller chains on, and refuses one
  (INVALID_ARGUMENT, nothing written) where the material has none; with
  nothing chained it sets `base.next` to NULL as before.
- **Shader** (`rbtrend_lit_surface.slang`, `lit_cover()`): in
  `lit_shade()` after the normal map, before f0 and the diffuse, under
  the scene's flag and the material's `cover_responds` (uniform
  branches): wet first (albedo times lerp(1, 0.2, w x porosity),
  roughness lerped to the wet roughness), then snow by the turned
  geometric normal's slope and the height (round: |P - centre|; flat:
  dot(P, up)) against the line, s = amount x both smoothsteps x cover;
  only where s > 0 the albedo, roughness, metalness and shading normal
  blend (so a cover of no snow leaves the normal's bits alone); the
  roughness clamped to 0.045 to 1 after. The traced table's entries are
  made from the block's other fields (`rbtrend_rt_lit_entry_of`), so the
  path tracer and traced reflections see the material bare (Q3a), with
  no change there.
- **Tests** (rabbittustests, Metal's validation with warnings as asserts,
  Debug and Release, both devices; each seen failing first against a
  stub returning NOT_IMPLEMENTED and the old set_values, which refused
  any chain): `test_lit_cover_refusals` (T1: every field, the frame 0
  and 3, a flat up of 0 and a round one's accepted, the struct codes,
  the response's fields, its chain twice and a foreign struct on
  set_values and get_values, a response asked of a material with none,
  nothing changed by a refusal), `test_lit_cover_off` (T2: no cover, a
  response with no cover, and a cover of no snow and no wetness the same
  texels, hash 06a6f47e768ba383 before and after the build, Debug and
  Release), `test_lit_cover_snow_slope` (T3: 255 facing up, 134 at a
  cosine of 0.7 against 134, 38 bare at 0.3, a normal-mapped face 255
  against its bare 102), `test_lit_cover_snow_line` (T4: flat rows
  exactly the reference; round, a centre 6.4e6 off, within 2 levels of
  it, the tolerance 6), `test_lit_cover_wet` (T5: 26 against 25.5 at
  porosity 1, 77 at wetness 0.5, 128 sealed; the highlight's middle 129
  to 255 wet, its corner 128 to 123), `test_lit_cover_response` (T6: 128
  with no response under everything, 191 at cover 0.5, 77 at porosity
  0.5, 128 again after values with none), `test_frame_allocations` (T8:
  a cover set every frame: 0 of ours), and `rbtrend_contract_tests` (a
  NULL scene, headless). Every other GPU test's printed figures the same
  as before the build (Debug and Release logs compared line for line).
- **Measured** (`test_lit_cover_measure`, Release, no validation, the M5
  Pro, submit to idle, a lit face over 2560 x 1440, three rounds of the
  mean of 50 after 10): three runs, each on both devices: no cover
  0.653 to 0.673 ms warm (the first device's first readings 0.890 and
  1.057 ms, cold, not counted); a cover its material doesn't answer
  -0.023 to +0.007 ms from none (the uniform branch: nothing measurable);
  snowed and wet (a round frame, every pixel in the ramps) +0.034 to
  +0.055 ms, two to three times the draft's estimate (under 0.02 ms by
  counting ALU): the half-float unpacking and the normal's renormalising
  weren't counted, and submit-to-idle readings at this size move by
  about 0.02 ms.
- **Card A75 decided 2026-11-20 with the user (b): the scene's block,
  full, is split.** The per-light and per-cascade arrays (the lights at 32
  bytes each, a fixed count, and the cascades) move into a storage buffer
  of their own, and the scalars stay in the uniform block, so the next
  scene-wide value (A59's stage 3, A57's stages 2 and 3, a caller's
  tiling) has room. Set 0's sixteen bindings are all taken, so one is
  freed or merged first. A public layout change for every lit shader a
  caller writes, so **a Rend draft first** (seven critique passes; the
  binding freed, the read cost measured), **not yet written**. Not a (the
  whole block a storage buffer), c (repacking: a few hundred bytes), d
  (the device's own `max_uniform_buffer_range`: nothing on the guaranteed
  floor).
  Approved 2026-11-20 with the user after a second seven-pass critique: `rabbittus/rbtrend_scene_block_draft.h` version 3, 33b3a77d; not built.

## Suggested order

Each step is usable on its own, and later ones lean on earlier ones. Items 4
and 5 are closed and their numbers are not reused.

1. Run what is written. It builds on all three backends as of 2026-10-12
   (see "State of the code"); left of this step:
   - Run more of it: one application ran on Metal and on D3D12 on
     2026-10-11 and on Vulkan (Linux) by 2026-10-12, covering only what it
     uses (see "State of the code"). No push constant set, staging,
     compute, indirect draw, partial view or layered pass has run on any
     of the three (as of 2026-10-12; since, `../rabbittustests` has run
     compute and storage ordering, the staged path on the RTX A2000 and
     indirect draws, below and in step 3; a layered pass and mesh
     shading on Metal on 2026-10-22, and on no other backend yet).
     `../rabbittustests` (`rbtrend_gpu_tests`)
     is written for this: push constants, a buffer update (staged on a
     device with its own memory), sampled views of one mip, rendering to
     one mip and sampling it in a later pass, each checked by reading the
     pixels back. Ran on Metal on 2026-10-11 (Apple M5 Pro, unified memory,
     so not the staged path) and passed, after two fixes: a Carrottus GPU Metal
     descriptor set kept a rewritten descriptor's old resource and crashed
     on it once destroyed; and a compiled list could not in fact be
     submitted twice (a Carrottus GPU command list is submitted once per
     recording), so `rbtrend_command_list_submit()` now takes one compile
     once and the header says so. On 2026-10-16 it ran on Windows, D3D12
     and Vulkan, on an RTX A2000 (memory of its own, so the staged path
     ran for the first time) and an Iris Xe, and passed after fixes in
     Rend and Carrottus GPU (`../rabbittustests/README.md` lists them). The
     same day every test, the newer ones too, passed on Metal under its
     validation layers, after five fixes in Carrottus GPU's Metal backend. Not
     run on Linux.
   - Vulkan: builds and runs on Linux as of 2026-10-12 (see "State of the
     code"); the Wayland driver issue no longer holds that machine. Write
     its side of each change alongside Metal and D3D12 and build it there.
     Built and run on Windows by `../rabbittustests` (2026-10-16).
   - Run it on a device with its own memory as well, for the staged path:
     done on D3D12 and Vulkan (RTX A2000), 2026-10-16.
2. Freeing staging copies (item 13): written 2026-10-18, with exact
   staged ranges and `RBTREND_BUFFER_WRITES`, and run on Metal only, where
   nothing is staged. `test_buffer_writes` then passed on D3D12 and Vulkan
   on the RTX A2000 (memory of its own) and the Iris Xe, 2026-10-18, with
   every other test, Vulkan with the validation layer, both clean (at
   1f75d84 plus the fix below, carrottus b7ac7e6). That run found and fixed
   one thing here: an upload to a texture with no layout yet always left
   it in SHADER_READ_ONLY, which Vulkan forbids for an image without
   sampled usage (VUID-VkImageMemoryBarrier-oldLayout-01211, a
   storage-only texture). It now rests in a layout its usage allows
   (`rbtrend_texture_upload_rest_layout`): SHADER_READ_ONLY if sampled,
   GENERAL if storage only, else TRANSFER_DST. A crash on Vulkan in the
   same runs was the test's own: `test_stale_lists` resized a texture with
   a view of it still alive, against rbtrend_texture2d_resize()'s contract.
   A Debug build now refuses that resize (2D and 3D): each texture counts
   its views not destroyed (`live_views`, Debug only), and a 2D resize
   leaves out its own mip chain's views, which it releases itself.
   `test_resize_with_views` (2D) and `test_resize_3d_with_views` (3D, the
   first test of a 3D texture and a 3D view) cover it; both passed on
   D3D12 and Vulkan, both GPUs, 2026-10-18. The same evening the fixes of
   that run (and carrottus's Vulkan barrier and D3D12 message changes) were
   built on macOS and every test passed on Metal (Apple M5 Pro), Debug
   under Metal API and shader validation with warnings fatal, both resize
   tests refusing as they should, and Release under `leaks` (none);
   callers' tests pass over them.
   The shared upload path for
   textures is written (item 6), and staged buffers don't use it: a
   transient span is taken back at the next frame, and a buffer may be
   created a frame or more before a list that uses it is submitted.
   Rewriting a binding set in place (item 9) is written.
3. The rest of item 32: written 2026-10-18 and run on Metal (see "State
   of the code"), except reading a depth texture while it is the pass's
   depth target, which needs a read-only depth layout Carrottus GPU doesn't
   have and a test of what Metal allows; nothing asks for it yet.
   `test_storage_ordering` then ran on 2026-10-18 under Vulkan's
   synchronization validation (`VK_LAYER_VALIDATE_SYNC=1`) and under D3D12's
   debug layer (under cdb: Carrottus GPU registered no message callback then,
   and since the same day prints the layer's corruptions, errors and
   warnings to stdout itself in Debug builds), on the Iris Xe and the RTX
   A2000: no hazard on any dispatch or storage image, so the barrier left
   out between two reads is not needed. Sync validation did report 15
   hazards per device elsewhere, all from Carrottus GPU's Vulkan barriers
   inferring access and stages from a layout (`layout_to_access`,
   `layout_to_stage`, crgpuvulkanrenderpass.c), not from Rend:
   COLOR_ATTACHMENT_OPTIMAL gives write access only, so a pass that loads
   its target reads it unordered after the transition (6, READ_AFTER_WRITE
   at vkCmdBeginRendering); DEPTH_STENCIL_ATTACHMENT_OPTIMAL gives write
   only and the early fragment tests only, while depth is written at the
   late tests, so the transition out after a depth pass is unordered (9,
   WRITE_AFTER_WRITE, the lit tests' depth targets). Fixed in carrottus the
   same day (attachments read and written, depth at both test stages, and
   the same access in crgpu_command_list_pipeline_barrier's resource states);
   rerun, every test passed under sync validation on both devices with no
   message.
4. Item 18, which settles item 1 and unblocks 19. Written, and run on
   all three backends through the materials and passes made from it
   (`../rabbittustests`, 2026-10-17). What is left is item 18's own list.
5. Items 19, 20, 21: material, pass and draw list. Written, compiled, and
   run on Metal, D3D12 and Vulkan by `../rabbittustests` (2026-10-17, see
   "State of the code"). What is left of each item is in its item, and
   the first of it is "Next, from the review" below.
6. Items 25, 26 and 29 are written and have run on D3D12, Vulkan and
   Metal (`../rabbittustests`, 2026-10-16). Item 22 (Rend ships shaders)
   and item 31 (mip generation, both forms) are written and have run on
   all three. Bloom (item 36) is written and `test_bloom` has passed on
   all three (2026-10-17). Then 23 and 24, each a shader Rend
   ships, as item 22 allows (24 is written and has run on Metal,
   2026-10-17; what is left of it is in the item); and the caller's half
   of the selective glow.

Item 39 (a device has only its features) is written and has run on
Metal through `../rabbittustests`, and on D3D12 and Vulkan on 2026-10-22.
Left: Linux. Mesh shading and the swap chain's refusals are tested since
the same evening, on Metal only.

Item 37 (the command allocator) is written and has run on all three
backends. The 20-minute D3D12 check (2026-10-16) was of an idle host that
never turned Rend's frame; that caller turns it since 2026-10-17, and
memory over minutes of rendering is still to be watched. Item 38's points are small
and independent; fix each when its function is next touched (the division
by zero is fixed).

Item 2 is closed and its D3D12 heap has never run: it belongs to step 1's
"run it on a device with its own memory", on an integrated GPU under
D3D12.

Items 22, 27, 28, 30, 33 and 34 wait until something needs them, decides
them or Carrottus GPU gains them. Item 35's Carrottus half is written (heaps and
placed resources, 2026-10-11, not run on a GPU); its Rend half, the
device allocator, is not started and waits on those heaps having run.

## Next, from the review of items 18 and 19

From the review of the reflection and the material against Hazel on
2026-10-11. Most important first. Remove a step here when it is fixed,
and record what was done under "State of the code" and in the item it
belongs to.

1. **Reflection limits**: the list is item 18's "what is left", and is
   kept there only.
2. **The arena's remaining policy** (item 19): place a new block in the
   fullest buffer that has room, so lightly used buffers drain, and a trim
   that releases the spare buffer. Held until item 35 decides what the
   arena's bookkeeping is.

### 65. GPU memory from heaps, after a census (card A80, decided 2026-11-24 with you)

Rend makes every buffer and image as its own device allocation
(`crgpu_buffer_create()`, `crgpu_image_create()`), and Carrottus' heaps
and placed resources (`crgpu_heap_create()`, `crgpu_*_create_placed()`)
are unused above it. Vulkan guarantees 4,096 live allocations
(`maxMemoryAllocationCount`'s minimum), about what Windows drivers report.

- **First, the census:** the live allocation count in Warpwake's worst
  scene on the A2000 under Vulkan (its limit read from `vulkaninfo`) and
  on the M5, counted at the backend.
- **If past about a quarter of the A2000's limit:** Rend suballocates
  from heaps by kind and lifetime, a few hundred MB each: a TLSF-style
  allocator for long-lived resources, linear rings for per-frame ones.
  Otherwise one allocation per resource stays, the census recorded here.
- **Not in it:** two live resources over one heap's bytes (aliasing
  within a frame) is undefined today (`crgpu.h`,
  `crgpu_buffer_create_placed()`) and needs a Carrottus draft for its
  barriers first. Reuse one after another works now.
- **Video:** decode surfaces stay possible, as a heap of their own.

### 66. Texture creates and copies refused in Rend's own words (card A77, decided 2026-11-24 with you: a)

Built with Carrottus' card S58 (a usage without its format bit refused,
and a two-aspect copy without a named aspect). Rend reads the format
table (`crgpu`'s per-format capabilities, card S51) before creating a
texture, and names or refuses a two-aspect copy itself, each refusal
with Rend's own code and message in its error struct (card A2), so the
caller never meets Carrottus' codes from a texture call. Tests: each
refusal seen failing first, with its words.

### 67. A selection's glow as a halo only (card T60, decided 2026-11-24 with you: a)

The glow is written into the glow target over the element's whole face
and spread by one bloom whose chain adds its levels together, so about
five times the glow comes back over the face: a wall selected in Windows'
blue glowing at 0.2 or more turns cyan (seen 2026-11-08 in a caller,
over Rend's bloom). The glow is composited only outside the element's
coverage (or its own glow subtracted under it), so a strength means the
halo's and the selected face keeps its material and tint, which are
functional feedback. Every caller's selection glow inherits it; a test of a
selected face's colour at full glow against its unselected colour.

### 68. Metal's argument buffers padded in SPIR-V (card S31, decided 2026-11-24 with you: a)

The shader step pads each set's argument buffer for Metal by writing
Metal members into SPIRV-Cross's text output with a type mapping of its
own, which a SPIRV-Cross formatting change breaks and a wrong type passes.
Instead, a pass in the shader step (`cmake/rbtrend_slang.cmake`), from
Slang's `-reflection-json`, adds each binding Slang dropped as a real
SPIR-V variable of its declared type, only in the copy given to
Carrottus' `crgpu_msl`; the Vulkan SPIR-V and the reflection input stay
Slang's. SPIRV-Cross then writes every member itself, and the text
writer goes. Report Slang's `-preserve-params` keeping no bindings (and
aborting on a `readonly RWByteAddressBuffer`, 2026.18.3); when Slang
keeps them, the pass goes too. Run on the Mac and with the Metal check
on Linux.
