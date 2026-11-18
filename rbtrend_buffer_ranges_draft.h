/*
 * rbtrend_buffer_ranges_draft.h - writing some bytes of an
 * RBTREND_BUFFER_WRITES_MANY buffer while submitted work reads others:
 * the rule by bytes, the submission serial a write names, and the check
 * (card A62; RBTREND_TODO.md item 61).
 *
 * DRAFT, 2026-11-17, version 1 (seven critique passes), for the user's
 * approval. APPROVED 2026-11-17 with the user, every question as
 * recommended; card A62 decided b. This drafts its
 * recommendation, b ("the write names the last submission that may read
 * those bytes, refused in every build if it hasn't finished"), as the
 * card asks for a Rend draft ahead of the decision; its alternatives are
 * Q1 below. Written unattended on the MacBook (M5 Pro) against, as read on
 * 2026-11-17 (rabbittus bf0c92a, 0.3):
 *   include/render/rbtrendbuffers.h ("Where a buffer is kept", "Per-frame
 *     copies", RBTREND_BUFFER_WRITES, rbtrend_buffer_write(), the pools'
 *     rules);
 *   include/render/rbtrendcontext.h ("Threads", "Destroying", "Results and
 *     errors", rbtrend_instance_next_frame(), _wait_idle());
 *   include/render/rbtrendcommands.h (rbtrend_command_list_submit());
 *   include/rbtcore.h (RBT_RESULT to RBT_RESULT_REND_REFLECTION_MISMATCH,
 *     5004; struct types to 5038);
 *   src/render/rbtrend_internal.hpp (rbtrend_instance_t: submit_serial,
 *     frame_fence, frame_last_serial; rbtrend_buffer_store,
 *     rbtrend_buffer_upload, rbtrend_instance_submit(),
 *     rbtrend_instance_wait_serial());
 *   src/render/rbtrendbuffers.cpp (rbtrend_buffer_store_write() and its
 *     _staging and _own forms, rbtrend_buffer_write_mapped(),
 *     rbtrend_buffer_store_uploads_submitted(), rbtrend_buffer_write());
 *   src/render/rbtrendcommands.cpp (rbtrend_cmd_record_uploads(), the
 *     submit's upload step);
 *   src/render/rbtrendcontext.cpp (rbtrend_instance_submit(),
 *     rbtrend_instance_collect()'s fence read); src/render/rbtrendpools.cpp
 *     (rbtrend_pool_reclaim()'s fence read);
 *   carrottus/include/crgpu.h (crgpu_fence_*, crgpu_submit_desc,
 *     crgpu_buffer_map(), crgpu_buffer_flush(), CRGPU_MEMORY_KIND) and its
 *     backends' fence reads (crgpuvulkansync.c, crgpud3d12sync.cpp,
 *     crgpumetalsync.m) and Metal's hazard tracking
 *     (crgpumetalrenderpass.m's barrier note, crgpumetalbuffer.m);
 *   the first caller's need, as card A62 records it (its draft is named
 *     on the card, not here: Rend names no caller);
 *   rabbittustests/tests/rbtrend_gpu_tests.c (test_buffer_writes,
 *     test_frame_allocations, test_destroyed_handles) and its README;
 *   Hazel's renderer (../.refs/HazelRenderer2025/Hazel/Hazel, present:
 *     src/Hazel/Renderer/VertexBuffer.cpp, StorageBuffer.cpp, and NVRHI's
 *     writeBuffer, vendor/nvrhi/include/nvrhi/nvrhi.h).
 * Not built, not in any target, not compilable: it repeats declarations of
 * rbtcore.h, rbtrendcontext.h and rbtrendbuffers.h to show them changed.
 * Every declaration below is marked [ADDED] or [CHANGED]; nothing unmarked
 * changes, and nothing is [REMOVED]. Nothing is built until it is
 * approved; once approved it is frozen (ROADMAP.md, section 1).
 *
 * ===========================================================================
 * Revision notes (the seven passes, 2026-11-17, each fixed before the next)
 * ===========================================================================
 *
 * Caller names: none (Rabbittus names no caller; check.sh caller-names).
 * The first caller's draft and numbers are card A62's to cite.
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Missing: Rend's own read. On a staged store (memory of the device's
 *    own, or a storage buffer without DEVICE_MAPPABLE) a write lands in the
 *    store's staging buffer, and the submission that copies it to the
 *    device reads those staging bytes until it finishes. The caller's serial
 *    names the caller's reads; it doesn't cover Rend's copy when bytes are
 *    written, copied by a submission that draws other bytes, and written
 *    again before it finishes (a slot written and dropped before ever being
 *    drawn). Changed: Rend counts its own copies (section 4): a staged
 *    store keeps the ranges an unfinished submission copies, with that
 *    submission's serial, and the check refuses a write over one of them as
 *    it refuses one over the caller's named reads.
 * 2. More than needed: a version 0 sketch let the new write take a buffer
 *    of per-frame copies. Its copies are already kept apart by the frame
 *    wait; a serial adds nothing and two rules for one buffer would. Changed:
 *    refused (Q5).
 * 3. More than needed: a public wait for a serial
 *    (rbtrend_instance_wait_submission()). rbtrend_instance_next_frame()
 *    already paces every frame, and the caller this is for never waits on
 *    one write. Changed: left out (Q4), the internal
 *    rbtrend_instance_wait_serial() kept internal.
 * 4. Checked why the existing vertex pool (rbtrendbuffers.h, "Vertex and
 *    index pools") doesn't already serve: a pool spans several blocks,
 *    each its own buffer, where one indirect draw binds one; each
 *    allocation's write makes an UPLOAD buffer of its own (a ONCE store), a
 *    Carrottus buffer per slot written; and the pool chooses the range, where the
 *    caller's slot arithmetic (base vertex = slot x block) needs fixed
 *    places. The pool's free is this draft's rule done inside Rend
 *    ("Allocating is safe while submitted work is drawing other ranges of
 *    the same block"; a freed range comes back once the fence passes the
 *    serial taken at the free): cited as its precedent in section 2.
 *
 * Pass 2 (general purpose, industry standard compared).
 * 1. Checked: nothing names a caller or what it draws in the
 *    declarations; the uses named are general (a pool of slots, a video
 *    decoder's ring of frames, a streamed mesh store, a particle
 *    emitter's ring).
 * 2. Compared (sections 6 and 7): D3D11's MAP_WRITE_NO_OVERWRITE, Unity's
 *    SubUpdates buffers and Unreal's RLM_WriteOnly_NoOverwrite take the
 *    same promise with no check; D3D12's fenced ring buffer and Vulkan's
 *    and GL's persistent mapping with fences are the same mechanism the
 *    caller runs here; this draft adds the check those leave out.
 *    WebGPU's queue.writeBuffer, NVRHI's writeBuffer (Hazel's), bgfx's
 *    update and Unreal's scatter uploads are the other family: the write
 *    is a queue operation, staged through a ring and copied on the GPU,
 *    so there is no rule for the caller at all. That is a real
 *    alternative the card's options a to c don't list. Changed: added as
 *    Q1 d, with its costs, recommended against for now.
 * 3. Considered and left out: the caller marking a range "no longer read
 *    from now" and Rend stamping it with the current serial (the pool's
 *    shape over a caller's buffer). It keeps a range list per buffer in
 *    Rend that duplicates the caller's own retirement ring. The serial the
 *    caller already holds is less (Q3).
 *
 * Pass 3 (the C API to standard).
 * 1. Name and order: the version 0 name, rbtrend_buffer_write_range(),
 *    said nothing a write of a range doesn't already do. Changed:
 *    rbtrend_buffer_write_after(), parameters as rbtrend_buffer_write()'s
 *    with `last_reader` before `out_error`, so the two calls read alike
 *    and a caller moves from one to the other by one argument.
 * 2. Checks in parameter order, each its own message, as
 *    rbtrend_buffer_write(): the buffer (NULL, destroyed), the overlap
 *    check, data, data_size, offset, the run past the end, ONCE, per-frame,
 *    then `last_reader` (a serial no submission took), then the two "in
 *    use" checks. A refused write changes nothing.
 * 3. The code for "not yet": RBT_RESULT_INVALID_ARGUMENT ("a call in the
 *    wrong state") would do, but a program can't tell a write it may
 *    retry next frame from a bad argument. Changed: a Rend code,
 *    RBT_RESULT_REND_IN_USE (5005, the next free in Rend's block; no draft
 *    claims it, searched), Q2.
 * 4. The getters return the value, 0 for a NULL or destroyed instance, as
 *    rbtrend_instance_get_frame_index() does; 0 is also "no submission",
 *    which a write's `last_reader` takes as "nothing reads these bytes".
 * 5. No struct is added, so no struct type is taken. Checked the next
 *    free one for the record: rbtcore.h ends at 5038
 *    (RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR) and rbtrend_lit_layers_draft.h
 *    plans 5039 (RBT_STRUCT_TYPE_REND_LIT_LAYER_VALUES), so the next truly
 *    free is 5040; this draft takes neither.
 *
 * Pass 4 (data, allocation, CPU writes into data in flight).
 * 1. CONVENTIONS.md, "No CPU write into anything the GPU or another role
 *    may be reading": stated per buffer (section 2, "Who writes what,
 *    when"): the caller's write lands only in bytes no unfinished
 *    submission reads, the caller's own reads named by the serial and
 *    Rend's copy counted by Rend; the first submission to read the new
 *    bytes is one made after the write (direct: flushed before it;
 *    staged: copied in it, ahead of its lists).
 * 2. No allocation in the steady state: the direct path allocates
 *    nothing; the staged path's two lists (the uploads waiting, the copies
 *    in flight) keep their capacity, and the room for the copies in
 *    flight is reserved by the write, not by the submission, so a
 *    submission never allocates for it and can't fail for it. Changed: the
 *    reserve moved from rbtrend_buffer_store_uploads_submitted() (version
 *    0) to rbtrend_buffer_store_write_staging().
 * 3. The new list is eq::array of a plain struct with RBT_PLAIN_DATA
 *    (CONVENTIONS.md: new containers are eqstd's), not std::vector as the
 *    store's older `uploads` is.
 * 4. The check's cost: a compare against a cached completed serial, and a
 *    fence read only when the named serial is past the cache. A caller
 *    that names frames rbtrend_instance_next_frame() has waited for never
 *    reads the fence here.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. The write is under rbtrendcontext.h's "Threads" with every other
 *    write: RBTREND_ENTER in Debug. The cache of the completed serial is
 *    written only by it, so it is plain data of the instance.
 * 2. The getters: version 0 had rbtrend_instance_get_completed_submission()
 *    write the cache, which would put a get_ call outside the get_ calls'
 *    exception. Changed: it only reads (the atomic submit_serial, then the
 *    fence), writes nothing, and is marked "Thread:" as overlapping any
 *    call but the instance's destroy; so is
 *    rbtrend_instance_get_last_submission(). Both are true of the code:
 *    submit_serial is std::atomic, the fence is made with the instance and
 *    crgpu_fence_get_value() "may overlap any call on the fence and its
 *    device except crgpu_fence_destroy() and destroying the device".
 * 3. Determinism: whether a write is refused depends on how far the GPU
 *    has got, so a caller that writes too early may pass on one run and
 *    fail on another. A caller that names only serials of frames
 *    rbtrend_instance_next_frame() has waited for, or reads
 *    rbtrend_instance_get_completed_submission() first, never sees the
 *    refusal: said in the call's text.
 * 4. Device loss: crgpu_fence_get_value() gives 0 when the API fails
 *    (Vulkan's vkGetSemaphoreCounterValue on a lost device), and D3D12's
 *    GetCompletedValue gives UINT64_MAX on a removed device (Carrottus
 *    passes it through). Changed: Rend reads submit_serial first and
 *    clamps the fence's value to it, so UINT64_MAX never reaches a caller
 *    and a value past every serial taken never passes the check; the
 *    loss itself is reported where it is today, by
 *    rbtrend_instance_next_frame(). Either answer on a lost device is
 *    harmless: nothing runs.
 * 5. Messages: developer English per call (card A2), naming the buffer,
 *    the bytes, the serial named and the one finished.
 *
 * Pass 6 (correctness against the code as built; every file cited read
 * again).
 * 1. rbtrend_buffer_store_uploads_submitted(store) takes no serial today
 *    (rbtrendbuffers.cpp); the submit has it (`serial`, from
 *    rbtrend_instance_submit(), rbtrendcommands.cpp). Changed under "Not
 *    in a header": it takes the serial.
 * 2. Every submission takes a serial, not only rbtrend_command_list_submit():
 *    the upload batch submitted on its own by rbtrend_instance_next_frame(),
 *    _wait_idle() and a readback (rbtrend_instance_flush_upload_batch())
 *    goes through rbtrend_instance_submit() too, and a failed submission
 *    takes none ("so the fence's value never has a gap"). Changed: the
 *    getter's text says "every submission through the instance", not
 *    "every command list submit".
 * 3. A staged MANY store's write maps the staging buffer for the write's
 *    bytes only and unmaps it (rbtrend_buffer_write_mapped()); Carrottus
 *    allows a mapping while the GPU uses the buffer (crgpu_buffer_map()).
 *    The direct store is mapped for life. Both write and flush only the
 *    write's bytes. Checked; section 5 corrected from "mapped for life"
 *    for both.
 * 4. The device copy's barrier is the whole buffer's (COMMON to
 *    COPY_DEST, then to the use state; rbtrend_cmd_record_uploads()), and
 *    "a copy is ordered after every earlier submission's reading of what
 *    it overwrites, by the barrier ahead of it". So on the staged path the
 *    GPU side was already safe for writes to bytes in use; only the
 *    staging bytes and, on the direct path, the buffer itself need the
 *    rule. Said in section 5. COMMON as the before state is right on
 *    D3D12, where a buffer decays to COMMON at the end of each
 *    ExecuteCommandLists.
 * 5. Rend already reads the fence raw in rbtrend_instance_collect() and
 *    rbtrend_pool_reclaim(), and neither clamps D3D12's UINT64_MAX: on a
 *    removed device they release everything. Harmless there (nothing
 *    runs), and not this draft's to change; noted under "Not in a header"
 *    so the new helper is the one place that clamps, and the two may move
 *    to it.
 * 6. On the Mac (unified memory) a vertex buffer is direct (UPLOAD,
 *    mapped for life): the staged path is reached on Metal only through a
 *    STORAGE buffer on a device made without
 *    CRGPU_FEATURE_DEVICE_MAPPABLE_MEMORY, which rabbittustests' second
 *    device is. Changed: the staged test uses that (section 11).
 *
 * Pass 7 (tests and the build).
 * 1. "Refused while in use" needs the GPU to be busy when the test writes,
 *    which nothing in Rend or Carrottus holds deterministically (Carrottus
 *    has no queue wait on a CPU-signaled value; a binary semaphore never
 *    signaled is invalid in Vulkan). Changed: the test reads the completed
 *    serial before and after the write and accepts OK only if the
 *    submission finished in between; a long compute dispatch ahead of it
 *    makes the refusal the usual case, and the run prints how often it
 *    was reached. The parts that can be deterministic are: a serial not
 *    yet taken (refused, INVALID_ARGUMENT), and a serial already finished
 *    (accepted).
 * 2. test_destroyed_handles is generated (make_destroyed_tests.py): the
 *    new write is one more call refused with a destroyed buffer (167 to
 *    168 on the device with features). The misuse tests are generated
 *    from abi.txt (tests/misuse/make_misuse_tests.py): three functions
 *    more, zeroed; both getters return 0 for NULL, which the generator
 *    accepts for a non-RBT_RESULT return.
 * 3. The in-flight list is plain logic: a unit test beside
 *    tests/rbtrendranges_tests.cpp, no device.
 * 4. check.sh: abi (three functions and one result code added, no break;
 *    --update-abi), doxygen, plain-data (the new element type),
 *    conventions. No version raised (card X2: Rabbittus' 0.3 is
 *    untagged).
 *
 * ===========================================================================
 * 1. The need, and today's rule
 * ===========================================================================
 *
 * rbtrendbuffers.h, "Where a buffer is kept": a write "must not be made
 * while submitted work that uses the buffer has yet to finish". A buffer
 * drawn every frame never meets that. A pool of slots in one buffer (a
 * streamed mesh store drawn by one indirect draw, a decoder's ring of
 * frames, an emitter's ring) is drawn every frame and written a few slots
 * at a time, each slot one no unfinished submission reads: the caller's
 * retirement keeps a slot out of use until every frame that drew it has
 * finished. The first caller's numbers: up to 6,144 slots of 24.3 KB (149
 * MB), at most a few dozen slots written a frame.
 *
 * Per-frame copies (RBTREND_BUFFER_DESC::per_frame) don't serve: frames
 * in flight copies of 149 MB, and a write reaches only the current one, so
 * a slot would have to be written into each in turn.
 *
 * ===========================================================================
 * 2. The rule by bytes
 * ===========================================================================
 *
 * A second write call, rbtrend_buffer_write_after(), takes one more value:
 * the serial of the last submission that may read the bytes it writes
 * (section 3). It may be made while submitted work reads other bytes of
 * the buffer. It is refused, in every build, with RBT_RESULT_REND_IN_USE
 * and nothing written, if:
 *
 * - that submission has not finished; or
 * - Rend's own copy of an earlier write of any of those bytes, on a
 *   staged buffer, is in a submission that has not finished (section 4).
 *
 * What Rend can't check is that the serial named is the right one: a
 * submission that reads the bytes through an indirect draw, a storage
 * slot or a compute shader reads them on the GPU, where Rend never sees
 * which bytes. So the caller's promise is the serial; Rend's check is
 * that the GPU has passed it. A caller that names too early a serial
 * writes into bytes in use, as a caller of rbtrend_buffer_write() who
 * breaks its rule does today. The check catches the common mistake (the
 * right record, read too soon), in Release as in Debug, at the cost of a
 * compare.
 *
 * rbtrend_buffer_write() keeps its whole-buffer rule, unchanged and still
 * unchecked: it is the call for a buffer written while nothing uses it.
 *
 * The precedent inside Rend: a vertex or index pool's freed range is
 * handed out again once the fence passes the serial taken at the free,
 * and "allocating is safe while submitted work is drawing other ranges of
 * the same block" (rbtrendbuffers.h). This is that rule, over a buffer the
 * caller lays out, with the serial the caller's.
 *
 * Who writes what, when (CONVENTIONS.md, "No CPU write into anything the
 * GPU or another role may be reading"):
 * - Direct store (mapped for life: unified memory, and every uniform
 *   buffer): the caller's write lands in the memory the GPU reads, in
 *   bytes no unfinished submission reads (the check), flushed in the call;
 *   the first submission to read them is one made after the call.
 * - Staged store: the write lands in the staging buffer, in bytes no
 *   unfinished submission reads, Rend's copies included (the check); the
 *   next submission of a list that uses the buffer copies them to the
 *   device ahead of its own lists, ordered after every earlier
 *   submission's reads by the copy's barrier. No reader sees a partial
 *   write: the bytes are whole before any submission that reads them is
 *   made.
 *
 * ===========================================================================
 * 3. Submissions and their serials
 * ===========================================================================
 *
 * What Rend has today (src/render): every submission through the instance
 * (rbtrend_instance_submit(): a command list's submit, and the upload
 * batch submitted on its own by rbtrend_instance_next_frame(),
 * rbtrend_instance_wait_idle() or a readback) signals the instance's one
 * fence (frame_fence, a Carrottus fence: a timeline VkSemaphore, an
 * ID3D12Fence, an MTLSharedEvent) to the next serial, submit_serial + 1,
 * from 1. A failed submission takes none. The instance submits on one
 * queue, so serials finish in order: the fence at N means every
 * submission up to N has finished. rbtrend_instance_next_frame() waits
 * for frame_last_serial[index], the last serial of the frame
 * `frames_in_flight` ago. Nothing of it is public.
 *
 * What is added: two reads.
 * - rbtrend_instance_get_last_submission(): the serial the latest
 *   submission took, 0 before the first. Read right after
 *   rbtrend_command_list_submit(), it is that submission's: under
 *   "Threads" nothing else submits in between.
 * - rbtrend_instance_get_completed_submission(): the highest serial the
 *   GPU has finished, read from the fence without waiting, never past the
 *   last taken.
 *
 * How a caller uses them: when it submits a frame that reads a slot, it
 * keeps rbtrend_instance_get_last_submission() as the slot's last reader
 * (or keeps one serial per frame, and a slot keeps the frame it was last
 * drawn in). A slot is free to write once
 * rbtrend_instance_get_completed_submission() has reached it; the write
 * names it. A slot never drawn names 0.
 *
 * Serials, not frame numbers (Q3): a frame may hold several submissions,
 * and a caller that submits more than once a frame (a decoder, an upload
 * between passes) names the one that reads. A frame's last serial is one
 * read after the frame's last submit.
 *
 * ===========================================================================
 * 4. Rend's own read: the staging copy
 * ===========================================================================
 *
 * On a staged MANY store, the submission that copies a write to the
 * device reads the write's staging bytes until it finishes. The caller
 * can't know which submission that is (the next one of any list that
 * uses the buffer, which may draw other slots), so Rend counts it. Each
 * staged MANY store keeps the ranges it copied in submissions not known to
 * have finished, each with that submission's serial: at a submission the
 * store's waiting uploads are appended, with its serial, and the list is
 * emptied from the front as the fence passes (serials only go up along
 * it). rbtrend_buffer_write_after() refuses a write overlapping one of
 * them (touching is not overlapping). rbtrend_buffer_write() doesn't look:
 * its rule already forbids a write while any submission using the buffer
 * is unfinished.
 *
 * Size: one range per disjoint run written each submission, kept for
 * frames in flight: a few dozen writes a frame keeps under a hundred,
 * checked linearly. A direct store and a ONCE store keep none.
 *
 * ===========================================================================
 * 5. What the two copies already do per range (checked in src/render)
 * ===========================================================================
 *
 * - Direct store: memcpy into the mapping at the offset and
 *   crgpu_buffer_flush() of those bytes (rbtrend_buffer_store_write()).
 *   Per range.
 * - Staged MANY store: the staging buffer mapped for the write's bytes,
 *   written, flushed and unmapped (rbtrend_buffer_write_mapped()); the
 *   range merged into `uploads`, sorted and disjoint, "bytes between two
 *   ranges are never copied" (rbtrend_buffer_store_write_staging()). Per
 *   range.
 * - The device copy: one crgpu_command_list_copy_buffer() per waiting
 *   range, in the submission's upload list ahead of its lists
 *   (rbtrend_cmd_record_uploads()). Per range. Its barrier is the whole
 *   buffer's: COMMON to COPY_DEST before, COPY_DEST to the use state
 *   after. That orders the copy after every earlier submission's reads of
 *   any byte of the buffer on the one queue, so the device side was
 *   already safe; the cost, a wait at the head of a submission that copies
 *   for earlier work reading the buffer, is today's, unchanged here.
 *   Carrottus' buffer barrier has no range, and only Vulkan's has one
 *   (D3D12's transitions and enhanced buffer barriers and Metal's tracking
 *   are whole-resource), so the whole buffer is the portable shape.
 *
 * So what was missing is the contract and its check, as the card says,
 * plus Rend's count of its own staging reads (section 4).
 *
 * ===========================================================================
 * 6. The platforms' own rules for a resource partly in use
 * ===========================================================================
 *
 * Vulkan. Host access to mapped memory is under the memory model: a host
 * write and a device read of the same memory location not ordered by a
 * dependency are a data race; bytes the device doesn't access may be
 * written while it runs (no rule forbids writing a mapped allocation in
 * use; the spec's "Host Access to Device Memory Objects"). Host writes
 * made before vkQueueSubmit are visible to that submission (the submit's
 * host write ordering guarantee), which is why the new bytes are read
 * only by later submissions; on memory without HOST_COHERENT,
 * vkFlushMappedMemoryRanges first, which Carrottus widens to
 * nonCoherentAtomSize (crgpuvulkanbuffer.c); widening makes available only
 * what the host wrote, and UPLOAD memory is never written by the device,
 * so the neighbouring bytes are not disturbed. Reuse of a part of a
 * resource is gated by a timeline semaphore's value
 * (vkGetSemaphoreCounterValue), exactly this draft's check. Device-side
 * ordering of a copy into a range: a buffer memory barrier, which may
 * name a range (offset, size).
 *
 * D3D12. An UPLOAD heap is CPU-written while mapped (Map with a NULL
 * read range; it may stay mapped); the documentation's rule is that the
 * CPU must not write data the GPU is still reading, and its own pattern
 * for partial reuse is the fenced ring buffer ("Uploading different types
 * of resources", "Suballocation within buffers"): a region is rewritten
 * once ID3D12Fence::GetCompletedValue() has passed the value signaled
 * after the work that read it. That is this draft's check. A copy into a
 * DEFAULT buffer is ordered by a transition barrier, which covers the
 * whole buffer; a buffer decays to COMMON at the end of each
 * ExecuteCommandLists, Rend's "before" state.
 *
 * Metal. A shared (MTLStorageModeShared) buffer is CPU-written through
 * `contents` at any time; Metal's hazard tracking
 * (MTLHazardTrackingModeTracked, Carrottus' choice for every resource
 * made on the device, crgpumetalbuffer.m and crgpumetalheap.m) orders GPU
 * accesses of a tracked resource between passes and encoders, for the
 * whole resource, and never tracks CPU writes: the CPU side is the app's,
 * by the completion of what read the bytes (Apple's "Synchronizing CPU
 * and GPU work": a semaphore of frames in flight; an MTLSharedEvent's
 * signaledValue for finer reuse, which is Carrottus' fence). Writing bytes
 * no in-flight command buffer reads is allowed. Untracked mode (heaps
 * only) moves GPU ordering to the app as well; Carrottus doesn't use it
 * for buffers.
 *
 * D3D11 and GL, for the record. D3D11's Map with
 * D3D11_MAP_WRITE_NO_OVERWRITE: "the app promises not to overwrite data
 * in use by the GPU", with no check, the closest precedent for the
 * promise; MAP_WRITE_DISCARD renames the whole buffer (per-frame copies'
 * shape). GL's persistent mapping (GL_MAP_PERSISTENT_BIT, or
 * GL_MAP_UNSYNCHRONIZED_BIT) with glFenceSync per region and
 * glClientWaitSync is the fenced-region pattern.
 *
 * All three APIs agree: a partly used buffer's free bytes may be written
 * by the CPU, gated by a completion counter the app reads. None checks
 * it; the debug layers don't see CPU writes.
 *
 * Carrottus says a buffer "may stay mapped ... including while the GPU
 * uses it" (crgpu_buffer_map()) but says nothing about which bytes may be
 * written meanwhile. Each API allows disjoint bytes; Q6 asks for the
 * sentence in crgpu.h.
 *
 * ===========================================================================
 * 7. Engines and WebGPU compared
 * ===========================================================================
 *
 * The promise, unchecked (this draft's family):
 * - Unreal: RHILockBuffer with RLM_WriteOnly_NoOverwrite, D3D11's
 *   promise across its RHIs.
 * - Unity: a GraphicsBuffer made with UsageFlags.LockBufferForWrite
 *   (ComputeBufferMode.SubUpdates before it): LockBufferForWrite /
 *   BeginWrite give direct memory, and regions the GPU still reads are
 *   the script's to avoid.
 * - Hazel: VertexBuffer::SetData maps the buffer and copies with no
 *   synchronization at all (and offsets the source, not the
 *   destination); RT_SetData goes through NVRHI below.
 *
 * The queue write, no rule for the caller (Q1 d's family):
 * - WebGPU: GPUQueue.writeBuffer(buffer, offset, data) copies the bytes
 *   at the call and is ordered on the queue timeline after everything
 *   submitted before it; the buffer may be in use. mapAsync, the direct
 *   path, is whole-buffer and refused while the buffer is in use.
 * - NVRHI (Hazel's): ICommandList::writeBuffer suballocates the command
 *   list's upload ring and records a copy (or vkCmdUpdateBuffer up to 64
 *   KB); its "volatile" constant buffers version per write.
 * - bgfx: update(DynamicVertexBufferHandle, startVertex, mem) is copied
 *   at the frame's submission, ordered, no rule.
 * - sokol: sg_update_buffer once a frame per buffer, whole; sg_append_buffer
 *   appends; sokol keeps num_inflight_frames copies of a stream buffer,
 *   so a partial update of a buffer in use isn't offered at all.
 * - Unreal's streamed pools (Nanite's streaming pages, virtual
 *   textures): uploads staged per frame and scattered into the pool by a
 *   GPU pass (FRDGScatterUploadBuffer), ordered on the queue.
 *
 * Rend's staged path is already half the queue write (a staging buffer
 * and a copy ordered by a barrier); what keeps it from being the whole of
 * it is the staging buffer kept for life, the size of the buffer, where
 * the queue write stages through a per-frame ring. Q1 d weighs it.
 *
 * ===========================================================================
 * 8. Threads, determinism and errors
 * ===========================================================================
 *
 * - rbtrend_buffer_write_after() is a buffer write: one caller at a time
 *   with everything of the instance (rbtrendcontext.h, "Threads"), checked
 *   in Debug by RBTREND_ENTER on the instance, and never at once with a
 *   submission (the write changes what the next one copies).
 * - The two getters write nothing and read an atomic and the fence: they
 *   may overlap any call on the instance but its destroy, marked
 *   "Thread:" (a renderer's streaming thread may ask whether a slot is
 *   free while the render thread submits).
 * - Whether a write is refused depends on the GPU's progress. A caller
 *   that names only serials of frames rbtrend_instance_next_frame() has
 *   waited for, or that reads rbtrend_instance_get_completed_submission()
 *   before choosing what to write, is never refused, whatever the timing.
 * - The refusal is RBT_RESULT_REND_IN_USE, nothing written, in every
 *   build, with an RBTREND_ERROR such as:
 *     "bytes 1048576 to 1073407 of the buffer 'pool stream 0' may be read
 *      by submission 812, which hasn't finished (the GPU has finished
 *      810); nothing written"
 *     "bytes 1048576 to 1073407 of the buffer 'pool stream 0' are still
 *      being copied to the device by submission 812 (an earlier write of
 *      them), which hasn't finished; nothing written"
 *   (the second names the first overlapping range in flight).
 * - On a lost device the completed serial is whatever the fence reads,
 *   clamped to the last taken; the loss is reported by
 *   rbtrend_instance_next_frame() as today.
 *
 * ===========================================================================
 * 9. Allocation
 * ===========================================================================
 *
 * Direct store: none. Staged MANY store: the write may grow `uploads` (as
 * today) and the copies-in-flight list (reserved there for every waiting
 * range, so the submission's append never allocates), both keeping their
 * capacity: after the first frames, none. RBT_RESULT_OUT_OF_MEMORY, with
 * nothing written, if a reserve fails. test_frame_allocations gains a
 * write a frame and must still count none of Rend's.
 *
 * ===========================================================================
 * 10. ABI, versions, struct types
 * ===========================================================================
 *
 * Added: three functions (rbtrend_instance_get_last_submission(),
 * rbtrend_instance_get_completed_submission(),
 * rbtrend_buffer_write_after()) and one result code
 * (RBT_RESULT_REND_IN_USE = 5005). No layout, signature or value changes;
 * no break for an existing caller. rbtrend_buffer_write() is unchanged.
 * No version is raised while Rabbittus' 0.3 is untagged (card X2).
 * No struct is added, so no RBT_STRUCT_TYPE is taken (the next free is
 * 5040: rbtcore.h ends at 5038, and rbtrend_lit_layers_draft.h plans
 * 5039).
 *
 * ===========================================================================
 * 11. Tests
 * ===========================================================================
 *
 * In rabbittustests (tests/rbtrend_gpu_tests.c), on Metal under Metal's
 * API and shader validation with warnings fatal, Debug and Release, on
 * both of its devices (with and without CRGPU_FEATURE_DEVICE_MAPPABLE_MEMORY);
 * D3D12 and Vulkan owed (the RTX A2000, memory of its own, is the staged
 * path's real run; the Iris Xe and WARP besides):
 *
 * 1. test_buffer_write_after: a MANY vertex buffer of 8 slots, one quad a
 *    slot, frames_in_flight 3. Each frame draws slots 0..3 by one indirect
 *    draw and, from frame 4 on, writes slot 4 + (frame % 4) naming the
 *    serial of the frame that last drew it (0 for a slot never drawn), then
 *    draws it next frame; 40 frames; each frame's pixels read back and
 *    compared with the colours written. Every write accepted (the caller
 *    is right), every slot drawn with its latest bytes. Direct on Metal's
 *    devices for a vertex buffer.
 * 2. test_buffer_write_after_staged: the same through a STORAGE buffer
 *    read by a compute shader that writes each slot's first word into a
 *    readback buffer; on the device without DEVICE_MAPPABLE it is staged,
 *    on Metal too. Then Rend's own read: a slot written and copied by a
 *    submission that doesn't read it, and written again at once naming 0:
 *    refused RBT_RESULT_REND_IN_USE (the second message) unless that
 *    submission finished in between (read before and after), and accepted
 *    once it has; the readback shows the second write's bytes.
 * 3. test_buffer_write_after_contract (misuse): NULL buffer; destroyed
 *    buffer (Debug); NULL data; data_size 0; offset past the end; bytes
 *    running past the end; a ONCE buffer; a per-frame buffer; a
 *    `last_reader` past the last serial taken (deterministic: INVALID_ARGUMENT,
 *    the message naming both); a `last_reader` finished (OK); and the
 *    in-use refusal: a long compute dispatch submitted, its serial named at
 *    once: RBT_RESULT_REND_IN_USE with nothing written unless the
 *    completed serial read after the call has reached it (then OK is
 *    accepted); the run prints how many of 20 tries were refused. After a
 *    wait, the bytes read back are the last accepted write's.
 * 4. The getters: 0 before the first submission and for NULL; the last
 *    one equal to the serial a submit then a lone batch took (2 then 3);
 *    completed never above last, and equal after rbtrend_instance_wait_idle().
 * 5. test_destroyed_handles (generated): rbtrend_buffer_write_after()
 *    refused with a destroyed buffer; the getters return 0 with a
 *    destroyed instance (Debug).
 * 6. test_frame_allocations: one write_after a frame into a pool buffer
 *    (direct on the first device, staged on the second): none of Rend's.
 *
 * In Rabbittus (no device): rbt_misuse_tests.c regenerated from abi.txt;
 * rbtrend_contract_tests.c: the three calls with NULL; a unit test of the
 * copies-in-flight list beside rbtrendranges_tests.cpp (append with
 * serials, prune from the front, overlap versus touch, a reserve that
 * makes the append allocation-free). Under the asan and tsan presets as
 * every suite.
 *
 * ===========================================================================
 * 12. Questions
 * ===========================================================================
 *
 * Q1. Which rule. a: by bytes, checked in Debug against direct draws'
 *     ranges (misses indirect draws, the case that needs it). b: by bytes,
 *     the write naming the last reader's serial, refused in every build
 *     (this draft; the card's recommendation). c: keep the rule, the
 *     caller keeps two pools (149 MB more, every slot written twice). d
 *     (added by pass 2): a queue write, as WebGPU's writeBuffer: a new
 *     buffer kind with no staging for life, each write staged in the
 *     frame's transient memory and copied on the GPU, ordered after
 *     everything before it, no rule for the caller at all. d is the safer
 *     contract and would drop the staged path's 149 MB staging buffer on a
 *     device with memory of its own; it costs a GPU copy of every write
 *     on unified memory (the Mac, where b writes in place), a buffer the
 *     CPU never maps there, transient memory the instance must have, and a
 *     second placement rule in "Where a buffer is kept": a draft of its
 *     own. Recommendation: b now, as drafted; d recorded as the way to
 *     drop the staging buffer if a device with memory of its own shows it
 *     matters.
 * Q2. The refusal's code. a: a new RBT_RESULT_REND_IN_USE (5005). b:
 *     RBT_RESULT_INVALID_ARGUMENT. Recommendation: a; a "not yet, nothing
 *     written" is one a program branches on, distinct from a bad argument.
 * Q3. How a caller names a submission. a: a serial, read by
 *     rbtrend_instance_get_last_submission() after the submit (this
 *     draft). b: an out parameter on rbtrend_command_list_submit() (a
 *     signature change, every caller touched). c: a frame number (coarser;
 *     a caller submitting twice a frame can't name the earlier one). d:
 *     the caller marks a range unread and Rend stamps it (a range list in
 *     Rend duplicating the caller's). Recommendation: a.
 * Q4. A public wait for a serial. a: none (this draft). b:
 *     rbtrend_instance_wait_submission(instance, serial, timeout_ns, error)
 *     over the internal rbtrend_instance_wait_serial(). Recommendation: a;
 *     rbtrend_instance_next_frame() paces frames, and b is added when a
 *     caller needs to wait on one write.
 * Q5. A buffer of per-frame copies. a: refused (this draft). b: accepted,
 *     the serial checked against the current copy. Recommendation: a; its
 *     copies are kept apart by the frame wait already.
 * Q6. Carrottus' sentence. crgpu.h's crgpu_buffer_map() allows a mapping
 *     while the GPU uses the buffer and says nothing of writing bytes the
 *     GPU doesn't read, which this rule rests on and which Vulkan, D3D12
 *     and Metal each allow. a: one contract sentence there ("bytes no
 *     unfinished work reads may be written and flushed while other bytes
 *     are in use"), approved with this draft, recorded in Carrottus'
 *     commit. b: a Carrottus draft of its own (card X21's rule for a
 *     changed public item). Recommendation: a; no item, signature or
 *     behaviour changes, only what was already true is said.
 * Q7. A Debug cross-check too (option a's): direct draws' vertex and
 *     index ranges compared with the writes. Recommendation: not now; it
 *     can't see the indirect draws that are the case, and costs a range
 *     record per draw.
 */

/* ===========================================================================
 * rbtcore.h
 * =========================================================================== */

/* In typedef enum RBT_RESULT, Rend's block, after
 * RBT_RESULT_REND_REFLECTION_MISMATCH = 5004: */

	/**
	 * [ADDED] Bytes the call would write may still be read by submitted
	 * work that hasn't finished: a submission the caller named, or Rend's
	 * own copy of an earlier write of them. Nothing is written. It is a
	 * "not yet": the same call succeeds once that submission has finished
	 * (rbtrendbuffers.h, rbtrend_buffer_write_after()).
	 *
	 * Why: Q2 a. A program tells a write it retries later from a bad
	 * argument. ABI: a value added to Rend's block (5005, the next free;
	 * no draft claims it). Tests: test_buffer_write_after_contract,
	 * test_buffer_write_after_staged.
	 */
	RBT_RESULT_REND_IN_USE = 5005,

/* ===========================================================================
 * rbtrendcontext.h
 * =========================================================================== */

/*
 * [CHANGED] The opening comment gains a paragraph after "Destroying":
 *
 * @par Submissions and their serials
 * Every submission through the instance takes a serial: a command list's
 * submit (rbtrend_command_list_submit()), and the batch of texture updates,
 * readbacks and clears when rbtrend_instance_next_frame(),
 * rbtrend_instance_wait_idle() or a readback submits it on its own. The
 * first takes 1 and each the next; a submission that fails takes none.
 * The instance submits on one queue, so they finish in that order: once a
 * serial has finished, every one before it has.
 * rbtrend_instance_get_last_submission() reads the latest taken and
 * rbtrend_instance_get_completed_submission() the latest finished. A
 * caller keeps the serial of the last submission that reads something, and
 * knows from the second when the GPU is done with it:
 * rbtrend_buffer_write_after() (rbtrendbuffers.h) takes one. Work the
 * caller submits to Carrottus GPU itself takes no serial.
 *
 * Why: the rule by bytes needs a name for a submission, and the instance
 * already counts them for its frame pacing and release queue; this makes
 * the count public, read-only. Tests: test 4 (section 11).
 */

/**
 * @brief [ADDED] Gets the serial of the latest submission made through an
 * instance ("Submissions and their serials", at the top of this header).
 *
 * Read right after rbtrend_command_list_submit() returns RBT_RESULT_OK, it
 * is that submission's serial: under "Threads", no other call submits in
 * between. A caller that keeps it as the last submission reading
 * something can later name it to rbtrend_buffer_write_after().
 *
 * Why: Q3 a. A submission needs a name, and the count is the instance's
 * already (rbtrend_instance_submit()); a getter after the submit adds it
 * without touching rbtrend_command_list_submit()'s signature. ABI: a
 * function added. Tests: test 4, test_buffer_write_after.
 *
 * Thread: may overlap any call on the instance but
 * rbtrend_instance_destroy(); one made while a submission runs gives the
 * serial from before it or after it.
 *
 * @param[in] instance Instance to read.
 *
 * @return The serial, from 1; 0 if nothing has been submitted yet or
 *         @p instance is NULL (or destroyed, in Debug).
 */
uint64_t rbtrend_instance_get_last_submission(rbtrend_instance instance);

/**
 * @brief [ADDED] Gets the serial of the latest submission the GPU has
 * finished, with every one before it, without waiting.
 *
 * Read from the instance's fence (a timeline semaphore's counter, a
 * fence's completed value, a shared event's signaled value). It never goes
 * down, and is never above rbtrend_instance_get_last_submission(). Once
 * rbtrend_instance_next_frame() has returned, it has reached the last
 * serial of the frame `frames_in_flight` frames ago.
 *
 * On a lost device it is whatever the fence then reads, kept at or below
 * the last serial taken; the loss is reported by
 * rbtrend_instance_next_frame().
 *
 * Why: a caller choosing which slot to write next needs to know which are
 * free before it writes, rather than learn it from a refusal; Vulkan's
 * vkGetSemaphoreCounterValue, D3D12's GetCompletedValue and Metal's
 * signaledValue are this read. ABI: a function added. Tests: test 4,
 * test_buffer_write_after_contract.
 *
 * Thread: may overlap any call on the instance but
 * rbtrend_instance_destroy(). It writes nothing.
 *
 * @param[in] instance Instance to read.
 *
 * @return The serial; 0 if nothing has finished, nothing has been
 *         submitted, the fence can't be read, or @p instance is NULL (or
 *         destroyed, in Debug).
 */
uint64_t rbtrend_instance_get_completed_submission(rbtrend_instance instance);

/* ===========================================================================
 * rbtrendbuffers.h
 * =========================================================================== */

/*
 * [CHANGED] The opening comment, "Where a buffer is kept": the paragraph
 * "The rule for the caller is one rule: ..." becomes:
 *
 * The rule for the caller: a write (at creation, by rbtrend_buffer_write()
 * or by rbtrend_buffer_write_after()) is seen by any submission made
 * after it. rbtrend_buffer_write() must not be made while submitted work
 * that uses the buffer has yet to finish. rbtrend_buffer_write_after() may
 * be, into bytes no unfinished submission reads ("Writing while the
 * buffer is in use", below). A buffer written and never used by a
 * submitted list is never copied, and keeps its staging copy until it is.
 * A uniform buffer's write lands in the memory the GPU reads, so the rule
 * holds more sharply there: written while a submitted frame still reads
 * it, it changes what that frame sees. A buffer rewritten every frame is
 * made of per-frame copies.
 *
 * [ADDED] A paragraph after "Per-frame copies":
 *
 * @par Writing while the buffer is in use
 * A buffer drawn every frame and written a part at a time (a pool of
 * slots, a ring) is written with rbtrend_buffer_write_after(), which
 * names the last submission that may read the bytes it writes
 * (rbtrendcontext.h, "Submissions and their serials"). It may be made
 * while submitted work reads other bytes of the buffer. It is refused, in
 * every build, with RBT_RESULT_REND_IN_USE and nothing written, while that
 * submission hasn't finished, or while Rend's own copy of an earlier write
 * of any of those bytes to the device is in a submission that hasn't (a
 * buffer with a staging copy). Which submissions read which bytes is the
 * caller's to know: an indirect draw, a storage slot or a shader reads
 * them on the GPU, where Rend never sees which. Rend checks that the GPU
 * has passed the submission named, not that it was the right one to name.
 *
 * Why: card A62 (Q1 b). The section opening this draft and its section 2.
 */

/**
 * @brief [ADDED] Writes bytes into a buffer that submitted work may be
 * reading elsewhere, naming the last submission that may read these
 * bytes.
 *
 * As rbtrend_buffer_write() in where the bytes go and when they are seen
 * (a submission made after the call reads them: written in place and
 * flushed, or staged and copied ahead of the next submission of a list
 * that uses the buffer), but made while other bytes of the buffer are in
 * use ("Writing while the buffer is in use", at the top of this header).
 *
 * @p last_reader is the serial of the last submission that may read any of
 * the bytes from @p offset to `offset + data_size - 1`
 * (rbtrend_instance_get_last_submission() when it was made), or 0 if no
 * submission has read them. The write is refused with
 * RBT_RESULT_REND_IN_USE, and nothing is written, while
 * rbtrend_instance_get_completed_submission() is below it; and on a buffer
 * with a staging copy, while Rend's copy of an earlier write of any of
 * those bytes is in a submission that hasn't finished. A caller that names
 * only submissions of frames rbtrend_instance_next_frame() has waited for,
 * or reads rbtrend_instance_get_completed_submission() before choosing the
 * bytes, is never refused. The check reads the fence only when
 * @p last_reader is past the last finished serial the instance has seen,
 * and checks Rend's copies in flight on a staged buffer, a few dozen
 * ranges at most for a few dozen writes a frame.
 *
 * Not for a buffer of per-frame copies, whose copies are kept apart
 * already ("Per-frame copies"), or one written once.
 *
 * Every argument is checked before anything is written, each its own
 * check and message in parameter order, after the buffer's null and
 * destroyed checks and Rend's overlap check on its instance (RBTREND_ENTER),
 * as rbtrend_buffer_write().
 *
 * Why: card A62, Q1 b. A pool of slots in one buffer is drawn every frame
 * and written a slot at a time into slots no unfinished frame reads;
 * rbtrend_buffer_write()'s rule is never met, and per-frame copies would
 * hold frames-in-flight copies of the whole pool. The serial is the
 * mechanism Vulkan's timeline semaphores, D3D12's fenced ring buffers and
 * Metal's shared events give an application for reusing part of a
 * resource; the check is what D3D11's NO_OVERWRITE, Unreal's and Unity's
 * no-overwrite locks leave out. ABI: a function added. Tests:
 * test_buffer_write_after, test_buffer_write_after_staged,
 * test_buffer_write_after_contract, test_destroyed_handles,
 * test_frame_allocations.
 *
 * Thread: a buffer write, under rbtrendcontext.h's "Threads": not at once
 * with any other call on the instance but the exceptions marked there, a
 * submission included.
 *
 * @param[in]  buffer      Buffer to write: created RBTREND_BUFFER_WRITES_MANY,
 *                         not per_frame.
 * @param[in]  offset      Where in the buffer the bytes go.
 * @param[in]  data        The bytes. Read during the call only.
 * @param[in]  data_size   How many bytes. Not 0.
 * @param[in]  last_reader The serial of the last submission that may read
 *                         those bytes, or 0 for none. Not above
 *                         rbtrend_instance_get_last_submission().
 * @param[out] out_error   Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; otherwise nothing is written, checked in this
 *         order: RBT_RESULT_INVALID_ARGUMENT if @p buffer is NULL (or
 *         destroyed, in Debug), @p data is NULL, @p data_size is 0,
 *         @p offset is past the end of the buffer, the bytes run past it
 *         (checked by subtraction, so no sum wraps), the buffer was created
 *         RBTREND_BUFFER_WRITES_ONCE, it has per-frame copies, or
 *         @p last_reader is above the last serial the instance has taken
 *         (the message names both); RBT_RESULT_REND_IN_USE if submission
 *         @p last_reader hasn't finished, or Rend's copy of an earlier write
 *         of any of the bytes is in a submission that hasn't (the message
 *         names the bytes, the serial and the latest finished);
 *         RBT_RESULT_OUT_OF_MEMORY if there is no memory to note the write;
 *         or the code for Carrottus GPU's result if it can't be flushed or
 *         staged.
 */
RBT_RESULT rbtrend_buffer_write_after(
	rbtrend_buffer buffer,
	uint64_t offset,
	const void *data,
	uint64_t data_size,
	uint64_t last_reader,
	RBTREND_ERROR *out_error);

/*
 * [CHANGED] rbtrend_buffer_write()'s brief text, its rule line only:
 * "Not while a submission that reads that copy is unfinished ("Where a
 * buffer is kept", at the top of this header); to write some bytes while
 * others are in use, rbtrend_buffer_write_after()." Signature, checks and
 * results unchanged.
 */

/*
 * ===========================================================================
 * Not in a header: what changes in src/render
 * ===========================================================================
 *
 * - rbtrend_internal.hpp, rbtrend_instance_t: `completed_serial`
 *   (uint64_t, 0), the latest finished serial a write has seen, written
 *   only by rbtrend_buffer_write_after() under "Threads". A helper,
 *   rbtrend_instance_read_completed(instance): reads submit_serial
 *   (acquire) first, then crgpu_fence_get_value(frame_fence), and returns
 *   the smaller (pass 5: D3D12's UINT64_MAX on a removed device, and a
 *   submission that finishes between the two reads, both under-report,
 *   which is the safe side). The public getter returns it;
 *   rbtrend_instance_collect() and rbtrend_pool_reclaim() may move to it
 *   (pass 6), not required by this draft.
 * - rbtrend_internal.hpp, rbtrend_buffer_store: `copies_in_flight`, an
 *   eq::array of { uint64_t offset, size, serial; } with RBT_PLAIN_DATA
 *   (check.sh plain-data), for a staged MANY store only; appended in
 *   serial order, pruned from the front.
 * - rbtrendbuffers.cpp, rbtrend_buffer_store_write_staging(): besides
 *   `uploads`' reserve, reserves `copies_in_flight` to its size plus
 *   `uploads.size()` + 1, so the append at the submission never
 *   allocates (pass 4); RBT_RESULT_OUT_OF_MEMORY otherwise, nothing
 *   written.
 * - rbtrendbuffers.cpp, rbtrend_buffer_store_uploads_submitted(store,
 *   serial) (pass 6: it takes the serial now): for a MANY store, prunes
 *   ranges whose serial the fence has passed, then appends each upload's
 *   range with `serial`; then empties `uploads` as today. The submit
 *   (rbtrendcommands.cpp) passes the serial rbtrend_instance_submit()
 *   handed back.
 * - rbtrendbuffers.cpp, rbtrend_buffer_write_after(): rbtrend_buffer_write()'s
 *   checks, then per-frame (part_count > 1) refused, then `last_reader` >
 *   submit_serial refused; then if last_reader > completed_serial,
 *   completed_serial = rbtrend_instance_read_completed() and refused if
 *   still below; then, on a staged MANY store, `copies_in_flight` pruned
 *   against completed_serial (read again only if an entry is past it) and
 *   any overlap refused; then rbtrend_buffer_store_write() as
 *   rbtrend_buffer_write().
 * - rbtrendcontext.cpp: the two getters (RBT_STALE, no RBTREND_ENTER, as
 *   the get_ calls).
 * - carrottus/include/crgpu.h, crgpu_buffer_map(): Q6 a's sentence.
 *
 * ===========================================================================
 * Records when built
 * ===========================================================================
 *
 * RBTREND_TODO.md item 61: built, its runs. rabbittus/CLAUDE.md, "Rend":
 * a line on the rule by bytes. rabbittustests' README: the three tests
 * and the getters' test. abi.txt (--update-abi; additions only).
 * The first caller's draft (named on card A62): its "checked in Debug by
 * the ranges a list's draws name" becomes the serial rule (it keeps each
 * slot's last-drawn serial from rbtrend_instance_get_last_submission()).
 * The workspace's DECISIONS.md card A62 moves to "Decided".
 */
