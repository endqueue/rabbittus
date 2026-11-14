/*
 * rbtarchive_codec_draft.h - a codec's failure says which: out of memory
 * told apart from corrupt data (card A52, decided 2026-11-14 with the user:
 * b; RBTARCHIVE_TODO.md, "A codec's failure says which").
 *
 * APPROVED by the user 2026-11-14, version 8. Written unattended
 * on the Mac (Apple M5 Pro), the workspace's session prompt of 2026-11-14, part 4b;
 * revised by the workspace's review prompt of 2026-11-14, whose record has
 * each finding (C<cycle>.<n>).
 *  - Version 2 (stage A): Q1-Q4 answered with the user, each (a) as
 *    recommended; the declarations already followed (a), so none changed.
 *  - Version 3 (cycle 1, the library's rules): rbtarchive_codecs_compress()'s
 *    message names the method, having no entry (C1.6); stream_begin
 *    returning RBT_RESULT_OK with no state is the codec's mistake (C1.7).
 *  - Version 4 (cycle 2, the libraries underneath): Q2's claim about the
 *    presets corrected, put to the user (C2.7); the DEFLATE codec's
 *    stream_process never allocates (C2.8); a compress given less room than
 *    its bound (C2.9); libdeflate's options and zlib-ng's zalloc made exact
 *    (C2.10); libarchive's claim marked unconfirmed (C2.11).
 *  - Version 5 (cycle 5, failures): what a reader, an entry reader and a
 *    writer are after a codec's failure (C5.3).
 *  - Version 6 (cycle 6, threads): libdeflate's process-wide allocator is
 *    shared state another thread may set; the options keep the codec off it
 *    (C6.6).
 *  - Version 7 (cycle 7, tests): a two-thread test of failing codecs, and
 *    where each test runs (C7.5).
 *  - Version 8 (the user's answer to C2.7, 2026-11-14): Q2 answered (b),
 *    a run-time refusal: RBT_STRUCT_TYPE_ARCHIVE_CODEC_2 (1006), the old
 *    type refused by rbtarchive_codecs_add().
 * Cycles 3 and 4 changed only the TODO (the build plan; the callers'
 * follow-up). Approved as version 8; frozen. Its SHA-256 is in
 * RBTARCHIVE_TODO.md, under A52 (card X23).
 * Not compilable: it repeats RBTARCHIVE_CODEC and parts of rbtarchive.h's
 * comments to show them changed. Every declaration below is marked
 * [ADDED], [CHANGED] or [REMOVED]; nothing unmarked changes. Nothing is
 * built until it is approved; once approved it is frozen (ROADMAP.md,
 * section 1, "Work built from approved drafts"). The TODO named
 * include/archive/ for this file; it sits at the repository's root beside
 * Rabbittus' other drafts.
 *
 * ---------------------------------------------------------------------------
 * Why
 * ---------------------------------------------------------------------------
 *
 * RBTARCHIVE_CODEC's compress, decompress and stream_process return a bool,
 * and stream_begin a pointer, so a codec that can't allocate and a codec
 * handed bad data look the same to Rabbittus Archive. It then names a
 * cause it can't know: rbtarchive_reader_read_entry() reports
 * RBT_RESULT_CORRUPT_DATA ("doesn't decompress to its declared ... bytes")
 * when the DEFLATE codec's libdeflate_alloc_decompressor() returned NULL;
 * the writer reports RBT_RESULT_INVALID_ARGUMENT for a compressor it
 * couldn't allocate; and an entry reader's stream_begin that returns NULL
 * is reported as RBT_RESULT_OUT_OF_MEMORY even when the codec refused for
 * another reason. A caller then tells its person the archive is damaged
 * when memory ran out (found 2026-11-13 by a caller's allocation-failure
 * test, which counts those runs apart).
 *
 * The fix is the codec's to say and Rabbittus Archive's to pass on: each
 * callback that can fail returns RBT_RESULT, and the archive's calls carry
 * the code, with their own message naming the entry.
 *
 * ---------------------------------------------------------------------------
 * What was compared
 * ---------------------------------------------------------------------------
 *
 *  - zlib (zlib-ng 2.3.3, the pinned copy's zlib-ng.h): every call returns
 *    an int code, Z_MEM_ERROR (-4) apart from Z_DATA_ERROR (-3) and
 *    Z_BUF_ERROR, and z_stream's zalloc / zfree / opaque let the caller
 *    supply the memory (zlib-ng.h lines 111-113, 157: "zalloc must return
 *    NULL if there is not enough memory").
 *  - libdeflate (1.26, the pinned .deps copy): allocation happens only in
 *    libdeflate_alloc_compressor[_ex]() and _decompressor[_ex](), which
 *    return NULL; the work calls allocate nothing and report
 *    LIBDEFLATE_BAD_DATA, _SHORT_OUTPUT or _INSUFFICIENT_SPACE
 *    (libdeflate.h lines 196-208). struct libdeflate_options takes
 *    malloc_func / free_func with no user pointer, and takes priority over
 *    libdeflate_set_memory_allocator()'s process-wide one (lines 352-400).
 *  - libarchive: a filter's or format's callback returns ARCHIVE_FATAL (or
 *    _WARN, _RETRY) and records why on the archive object with
 *    archive_set_error(a, errno_value, ...), ENOMEM for memory, so the code
 *    is coarse and the cause is the errno beside it. (From libarchive's
 *    documented API; no copy of it is in the workspace to read tonight.)
 *    [unconfirmed (C2.11): read against no copy or page of libarchive's in
 *    this review either; nothing in the draft depends on it.]
 *  - Hazel (.refs/HazelRenderer2025): its asset pack reserves a Flags field
 *    for "compressed type" (Hazel/Serialization/AssetPackFile.h, lines 16
 *    and 23) and has no codec interface: nothing to compare.
 *
 * So both libraries Rabbittus wraps already tell memory from data; the bool
 * is where it is lost. A code per call (zlib's way) fits Rabbittus' shape,
 * which returns RBT_RESULT everywhere; libarchive's code-plus-errno is the
 * shape rbtcore.h's per-call error struct already gives the archive's own
 * calls.
 *
 * ---------------------------------------------------------------------------
 * The version and the breaks
 * ---------------------------------------------------------------------------
 *
 * RBTARCHIVE_CODEC's four callbacks change type: a listed break against
 * rabbittus/abi.txt before 1.0 (RABBITTUS_VERSION 0.1.0 to 0.2.0, a minor
 * for the break; the workspace's CLAUDE.md, "Versions and releases"). A
 * caller's own codec, written against the bool shape, is refused: by
 * rbtarchive_codecs_add() at run time on every compiler, its base's type
 * being the old one, and by most compilers before that (question 2, b).
 * The struct's layout (its fields' sizes and offsets) doesn't change:
 * function pointers all; its type number does
 * (RBT_STRUCT_TYPE_ARCHIVE_CODEC_2, 1006, the old 1000 kept, retired). No
 * call is added or removed; the archive calls' result lists gain the codes their
 * codecs pass on (below). rbtarchive_codecs_add_deflate()'s behaviour
 * changes only in which code a failure is.
 *
 * Thread: no new rule. RBTARCHIVE_CODEC's line stands: the callbacks are
 * called inside the reader's and writer's calls, on the thread of each,
 * and from several threads at once where readers, writers or a reader's
 * reads overlap. The DEFLATE codec still makes its libdeflate object per
 * call, never on the table (card A52: the table and a reader's reads may
 * overlap; a read with no allocation is card A32's).
 *
 * New handles: none. Formats touched: none (an archive's bytes are the
 * same, byte for byte, at every level).
 *
 * What reaches a caller above: RBT_RESULT_OUT_OF_MEMORY from a read or a
 * write whose codec ran out of memory, where today it gets
 * RBT_RESULT_CORRUPT_DATA or RBT_RESULT_INVALID_ARGUMENT; and the codes of
 * its own codecs passed on as question 1 says. A caller that branches on
 * CORRUPT_DATA to tell its person the file is damaged now does so only
 * when it is.
 *
 * ---------------------------------------------------------------------------
 * Questions (numbered; each with options and a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. WHICH CODES EACH CALLBACK MAY RETURN, AND HOW THE ARCHIVE PASSES EACH.
 *     ANSWERED 2026-11-14 with the user: (a), as recommended; no change
 *     to the option.
 *     (a) RECOMMENDED. A closed list per callback, written on it:
 *           decompress      OK, OUT_OF_MEMORY, CORRUPT_DATA;
 *           compress        OK, OUT_OF_MEMORY, INVALID_ARGUMENT (a level it
 *                           refuses);
 *           stream_begin    OK, OUT_OF_MEMORY, INVALID_ARGUMENT (a level it
 *                           refuses; never when decompressing);
 *           stream_process  OK, OUT_OF_MEMORY, CORRUPT_DATA (decompressing).
 *         Every call that runs a codec passes each listed code on as it is,
 *         with its own message naming the entry and what it was doing
 *         ("out of memory decompressing 'a.bin'"), and its result list
 *         names them; rbtarchive_codecs_compress(), which has no entry,
 *         names the method ("out of memory compressing with method 8";
 *         C1.6). A stream_begin that returns RBT_RESULT_OK and leaves its
 *         state NULL is outside the contract too (C1.7): the call returns
 *         RBT_RESULT_INVALID_ARGUMENT naming the method, and calls nothing
 *         more of the codec for it. Any other code (a codec outside its contract) is
 *         RBT_RESULT_INVALID_ARGUMENT from the call, the message naming the
 *         method and the code the codec gave, so the call's list stays
 *         closed and the caller's codec bug is told as one. Today's checks
 *         after a callback (in_used, out_size or the compressed size past
 *         what was given) stay, and keep their codes: CORRUPT_DATA on a
 *         read, INVALID_ARGUMENT on a write.
 *     (b) Any code, passed on unchanged: a caller's codec may return
 *         RBT_RESULT_IO or LIMIT and the caller sees it. Open-ended: no
 *         call can list what it returns.
 *     (c) Only OK, OUT_OF_MEMORY and "failed", the third mapped as today
 *         (CORRUPT_DATA reading, INVALID_ARGUMENT writing): the smallest
 *         change, but a compress refusing a level and one given a buffer it
 *         overran stay one code.
 *
 * Q2. HOW A CALLER'S OWN CODEC, WRITTEN AGAINST THE BOOL SHAPE, IS TOLD.
 *     ANSWERED 2026-11-14 with the user: (a), as recommended; no change
 *     to the option. ANSWERED AGAIN 2026-11-14 with the user: (b), on
 *     review finding C2.7 (below), with its refusal's message.
 *     (a) Recommended in version 1. By the compiler: the callbacks' types change, so
 *         assigning a bool-returning function is a compile error in C++ and
 *         a diagnostic in C (incompatible pointer types), and the break
 *         is listed against abi.txt with the version 0.1.0 to 0.2.0. No
 *         RBT_STRUCT_TYPE change: the layout is the same, and a type
 *         number says what a struct is, not which version of its
 *         callbacks.
 *         [CHANGED by C2.7: version 1 said the diagnostic is "an error
 *         under -Werror and MSVC's /we4113 that the presets set". Neither is
 *         set: Rabbittus' CMakeLists.txt and CMakePresets.json set no
 *         -Werror, /WX or /we4113 (read 2026-11-14), and a caller's build is
 *         its own. Apple clang 21 refuses an old codec by default in C and
 *         C++ (-Wincompatible-function-pointer-types is an error; a scratch
 *         program, the review's record), as GCC 14 does in C
 *         (-Wincompatible-pointer-types, an error from GCC 14 on). MSVC's C
 *         compiler only warns (C4113, C4133), so there an old codec builds
 *         and is misread: its true read as RBT_RESULT_INVALID_ARGUMENT (1),
 *         its false as RBT_RESULT_OK (0), a failed decompress taken for
 *         one done.]
 *         So (a) alone leaves the break unrefused on MSVC's C compiler.
 *     (b) TAKEN. A new struct type (RBT_STRUCT_TYPE_ARCHIVE_CODEC_2) and the old
 *         one refused by rbtarchive_codecs_add with STRUCT_WRONG_TYPE: a
 *         run-time refusal of a caller compiled against the old header.
 *         Costs a type number for a pre-1.0 break, which the version
 *         already covers. With it, "refused, never misread" holds on every
 *         compiler; the compile errors stay where compilers give them.
 *         The message: "codec for method %u was built against the bool
 *         callbacks (Rabbittus before 0.2): rebuild it".
 *
 * Q3. stream_end's SHAPE (it returns nothing today).
 *     ANSWERED 2026-11-14 with the user: (a), as recommended; no change
 *     to the option.
 *     (a) RECOMMENDED. Unchanged: void. It frees a state, finished or not;
 *         freeing can't run out of memory, and zng_deflateEnd()'s and
 *         zng_inflateEnd()'s only failure is a state that isn't one, which
 *         the archive never hands it. As every destroy in the workspace,
 *         it can't fail.
 *     (b) RBT_RESULT, for symmetry: a code nobody can act on (the state is
 *         gone either way).
 *
 * Q4. THE TESTS (tests/rbtarchive_tests.cpp, and one new test program).
 *     ANSWERED 2026-11-14 with the user: (a), as recommended; no change
 *     to the option.
 *     (a) RECOMMENDED.
 *       - A test codec whose `user` says which callback fails how: each of
 *         decompress, compress, stream_begin and stream_process returning
 *         each code of its list, and one code outside it, through every
 *         call that runs it (rbtarchive_reader_read_entry, an entry
 *         reader's read, rbtarchive_writer_add_entry, begin_entry and
 *         write_entry, rbtarchive_codecs_compress). Each reaches the caller
 *         as Q1 says, its message naming the entry; a writer after a
 *         failed write is failed as today; nothing is written to the sink
 *         after it.
 *       - The DEFLATE codec's allocations failed: a test program of
 *         Rabbittus' own, rbtarchive_allocfail_tests, with an allocation-
 *         failure injector beside alloctrace.h (as a caller's
 *         tests/allocfail.h does: malloc interposed on Apple, replaced on
 *         Linux, the import slots on Windows; the last two written, not
 *         compiled, until those machines build it). Each of the four
 *         DEFLATE paths (whole and streamed, compress and decompress) run
 *         failing its Nth allocation for every N it makes: each a code of
 *         Q1's list, OUT_OF_MEMORY for every injected failure and never
 *         CORRUPT_DATA, nothing left allocated. libdeflate's allocations
 *         go through libdeflate_alloc_*_ex with malloc and free named in
 *         its options (Rabbittus' own allocation, never the process-wide
 *         allocator libdeflate_set_memory_allocator() sets), and zlib-ng's
 *         through zalloc and zfree set to the same, so the injector reaches
 *         both.
 *       - Archives' bytes the same as before at every level (the existing
 *         byte-identity tests, unchanged).
 *       - Two threads (C7.5): two entry readers of one reader, overlapping,
 *         over the test codec failing one with OUT_OF_MEMORY and the other
 *         with CORRUPT_DATA; each sees its own code and message, and
 *         neither the other's (the per-call error, never the object's).
 *         In every build, so -tsan runs it.
 *       - Where they run: rbtarchive_tests and rbtarchive_allocfail_tests
 *         on the Mac (Debug, Release, -asan, -tsan), Windows (Debug,
 *         Release, -asan) and Linux (Debug, Release, -asan, -tsan); the
 *         injector's Windows and Linux halves are written on the Mac and
 *         first compiled there (C7.5).
 *       - A codec whose base's type is the old RBT_STRUCT_TYPE_ARCHIVE_CODEC
 *         refused by rbtarchive_codecs_add() with STRUCT_WRONG_TYPE, the
 *         table unchanged and the message saying to rebuild (Q2 b).
 *       - The misuse generator rerun after `./check.sh --update-abi` (no
 *         call changes; it reads the struct).
 *     (b) Only the test codec, and the DEFLATE codec's allocations left to
 *         a caller's allocation-failure test: no injector in Rabbittus, but
 *         its own codec's main fix then tested only from above.
 *
 * ---------------------------------------------------------------------------
 * rbtarchive.h
 * ---------------------------------------------------------------------------
 */

/**
 * @brief A compression method, by its ZIP method id.
 *
 * [CHANGED] (the comment's last paragraph added)
 *
 * Methods other than STORED and DEFLATE (zstd is 93, LZMA 14, ...) aren't
 * read by most ZIP tools: an archive using them only opens in software that
 * registers them too.
 *
 * A decode-only codec leaves `compress` and `compress_bound` NULL. A codec
 * with the three stream callbacks also works on entries in pieces
 * (rbtarchive_writer_begin_entry(), rbtarchive_reader_open_entry()); one
 * without them works on whole entries only.
 *
 * Each callback that can fail returns RBT_RESULT, one of those its comment
 * lists: RBT_RESULT_OUT_OF_MEMORY when it couldn't allocate, apart from the
 * data's or the level's failure. The reader's and writer's calls pass each
 * listed code on, with a message naming the entry; a code not listed is
 * the codec's mistake, and the call returns RBT_RESULT_INVALID_ARGUMENT
 * naming the method and the code (Q1 a).
 *
 * Thread: the callbacks are called from inside the reader's and writer's
 * calls, on the thread of each; with readers or writers used at once, or a
 * reader's calls overlapping, from several threads at once.
 *
 * @see rbtarchive_codecs_add()
 */
typedef struct RBTARCHIVE_CODEC {
	/** [CHANGED] This struct's type, RBT_STRUCT_TYPE_ARCHIVE_CODEC_2 (was
	 * RBT_STRUCT_TYPE_ARCHIVE_CODEC), and its chain (rbtcore.h, "Struct
	 * bases"). rbtarchive_codecs_add() refuses the old type with
	 * RBT_RESULT_STRUCT_WRONG_TYPE and a message saying to rebuild the
	 * codec (Q2 b). Why: the callbacks' break refused at run time on every
	 * compiler (C2.7). ABI: the type's value. Tests: a codec of the old
	 * type refused, its message read back; the misuse generator. */
	RBT_STRUCT_BASE base;
	/** The ZIP method id. Never RBTARCHIVE_METHOD_STORED. */
	uint16_t method;
	/** Passed to each callback as its `user`. */
	void *user;
	/** The most bytes compressing `in_size` bytes can produce. NULL for a
	 * decode-only codec. Can't fail. */
	uint64_t (*compress_bound)(void *user, uint64_t in_size);
	/** [CHANGED] Returns RBT_RESULT, was bool.
	 * Compresses `in` into `out` (at least #compress_bound bytes) at
	 * `level`, whose meaning is the codec's; sets `out_size` on
	 * RBT_RESULT_OK. Returns RBT_RESULT_OK; RBT_RESULT_OUT_OF_MEMORY if it
	 * couldn't allocate; RBT_RESULT_INVALID_ARGUMENT for a level it
	 * refuses, or an `out_capacity` below #compress_bound's for `in_size`
	 * (C2.9: the archive never passes one; libdeflate answers it with 0
	 * bytes). NULL for a decode-only codec.
	 * Why: a compressor that couldn't be made was reported as a refused
	 * level. ABI: the pointer's type (A52's break). Tests: Q4. */
	RBT_RESULT (*compress)(void *user, int32_t level, const void *in, uint64_t in_size, void *out,
	                       uint64_t out_capacity, uint64_t *out_size);
	/** [CHANGED] Returns RBT_RESULT, was bool.
	 * Decompresses `in` into exactly `out_size` bytes. Returns
	 * RBT_RESULT_OK; RBT_RESULT_OUT_OF_MEMORY if it couldn't allocate;
	 * RBT_RESULT_CORRUPT_DATA if the data is malformed or decompresses to
	 * any other size. Never NULL.
	 * Why: card A52's finding, a read reported as damaged when memory ran
	 * out. ABI: the pointer's type. Tests: Q4. */
	RBT_RESULT (*decompress)(void *user, const void *in, uint64_t in_size, void *out, uint64_t out_size);
	/** [CHANGED] Returns RBT_RESULT and the state through `out_state`, was
	 * the state or NULL.
	 * Starts one entry's stream: compressing at `level` if `compress`
	 * (`level` as #compress takes it, or its own range, which the codec
	 * documents), decompressing if not (`level` 0). On RBT_RESULT_OK sets
	 * `*out_state` to the stream's state, handed to #stream_process and
	 * #stream_end; on failure leaves it NULL and holds nothing. Returns
	 * RBT_RESULT_OK; RBT_RESULT_OUT_OF_MEMORY if it couldn't allocate;
	 * RBT_RESULT_INVALID_ARGUMENT for a level it refuses (compressing
	 * only). NULL, with the other two, for a codec that works on whole
	 * entries only; never NULL alone.
	 * Why: a NULL state was out of memory to a reader and a refused level
	 * to a writer, whichever it was. ABI: the pointer's type and a
	 * parameter added. Tests: Q4. */
	RBT_RESULT (*stream_begin)(void *user, bool compress, int32_t level, void **out_state);
	/** [CHANGED] Returns RBT_RESULT, was bool.
	 * Takes up to `in_size` bytes of `in`, setting `in_used` to how many it
	 * took, and writes up to `out_capacity` bytes to `out`, setting
	 * `out_size`. `finish` says no input follows `in`: a compressing stream
	 * then writes everything it holds, and sets `done` once it has; a
	 * decompressing one sets `done` once it reaches its data's end, finish
	 * or not. Called again while it takes input or gives output; one call
	 * that does neither with `finish` set and `done` unset is a failure.
	 * Returns RBT_RESULT_OK; RBT_RESULT_OUT_OF_MEMORY if it couldn't
	 * allocate (a stack that allocates as it goes; the DEFLATE codec's
	 * never does: zlib-ng 2.3.3 allocates a stream's state and window at
	 * its init, inflate.c's alloc_inflate, so its OUT_OF_MEMORY is
	 * #stream_begin's; C2.8); RBT_RESULT_CORRUPT_DATA for malformed data,
	 * when decompressing. The stream is then failed: the archive calls only
	 * #stream_end on it.
	 * Why: as #decompress. ABI: the pointer's type. Tests: Q4. */
	RBT_RESULT (*stream_process)(void *user, void *state, const void *in, uint64_t in_size, uint64_t *in_used,
	                             void *out, uint64_t out_capacity, uint64_t *out_size, bool finish, bool *done);
	/** Frees a stream's state, whether it finished or not. Unchanged (Q3 a):
	 * freeing can't fail. */
	void (*stream_end)(void *user, void *state);
} RBTARCHIVE_CODEC;

/*
 * [CHANGED] rbtarchive_codecs_add_deflate()'s comment gains: "Its
 * libdeflate compressors and decompressors are made through
 * libdeflate_alloc_compressor_ex() and _decompressor_ex() with Rabbittus'
 * own allocation calls (malloc and free) named in their options, never
 * the process-wide allocator libdeflate_set_memory_allocator() sets, and
 * zlib-ng's streams through zalloc and zfree set to the same: a failed
 * allocation in either is RBT_RESULT_OUT_OF_MEMORY from the call. Still
 * one per call: nothing is kept on the table." Its declaration doesn't
 * change.
 * Made exact (C2.10, C6.6), in the .cpp, not the header: the options are
 * zeroed with `sizeof_options` set to their size (libdeflate.h, 379-405;
 * libdeflate refuses options it can't size); zalloc is a function of
 * Rabbittus' own taking zlib's (opaque, items, size), refusing a product
 * that overflows `size_t` with NULL before calling malloc; zfree calls
 * free. libdeflate_set_memory_allocator() writes two process-wide pointers
 * that its plain allocation calls read, so a caller setting it on one
 * thread while a codec allocates on another is a data race the options
 * keep Rabbittus out of (C6.6).
 *
 * [CHANGED] The result lists of the calls that run a codec gain
 * RBT_RESULT_OUT_OF_MEMORY where they lacked it, and CORRUPT_DATA where a
 * stream_process can give it:
 *   - rbtarchive_reader_read_entry(), an entry reader's read
 *     (rbtarchive_entry_reader_read) and rbtarchive_reader_open_entry():
 *     OUT_OF_MEMORY from the codec, CORRUPT_DATA as before;
 *   - rbtarchive_writer_add_entry(), _begin_entry(), _write_entry(),
 *     _end_entry() and rbtarchive_codecs_compress(): OUT_OF_MEMORY from
 *     the codec, INVALID_ARGUMENT for a refused level as before.
 * "@par Results" at the top of the header gains one line under
 * RBT_RESULT_OUT_OF_MEMORY: "also a codec that couldn't allocate, its own
 * or the DEFLATE codec's."
 *
 * [CHANGED] What each call is after a codec's failure (C5.3), in the
 * comments of the calls named above, as the code is today:
 *   - rbtarchive_reader_read_entry() and rbtarchive_codecs_compress()
 *     leave the reader and the table as they were, the output buffer's
 *     bytes undefined: the call may be made again, and after
 *     OUT_OF_MEMORY succeeds once memory is there.
 *   - An entry reader whose read failed (the codec's code passed on) is
 *     failed: each later read returns RBT_RESULT_INVALID_ARGUMENT ("an
 *     earlier read of the entry failed"); the caller destroys it and opens
 *     the entry again, from its start.
 *   - A writer whose add_entry, begin_entry, write_entry or end_entry
 *     failed in its codec is failed, as after any failed write: every later
 *     call but destroy returns RBT_RESULT_INVALID_ARGUMENT, and what it has
 *     written to its sink is not an archive. OUT_OF_MEMORY tells the caller
 *     why, not that the writer can go on.
 *
 * ---------------------------------------------------------------------------
 * rbtcore.h
 * ---------------------------------------------------------------------------
 *
 * [ADDED] RBT_STRUCT_TYPE_ARCHIVE_CODEC_2 = 1006, in Rabbittus Archive's
 * block, its comment "RBTARCHIVE_CODEC, its callbacks returning RBT_RESULT
 * (since 0.2.0)". [CHANGED] RBT_STRUCT_TYPE_ARCHIVE_CODEC = 1000 kept,
 * its comment "Retired in 0.2.0: RBTARCHIVE_CODEC with bool callbacks,
 * refused by rbtarchive_codecs_add()" (no number is reused; Q2 b).
 *
 * [CHANGED] rbtarchive_codecs_add()'s result list gains
 * "RBT_RESULT_STRUCT_WRONG_TYPE for a codec whose base's type is
 * RBT_STRUCT_TYPE_ARCHIVE_CODEC, built against the bool callbacks".
 *
 * [CHANGED] RABBITTUS_VERSION_MINOR 1 to 2 (the callbacks' break). No
 * result is added: OUT_OF_MEMORY, CORRUPT_DATA and INVALID_ARGUMENT exist.
 */
