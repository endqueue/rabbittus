# Rabbittus Archive TODO

`include/archive/rbtarchive.h` reads and writes ZIP archives: STORED and DEFLATE
(or any registered codec), entries whole or streamed in pieces, raw entries,
updating, ZIP64, aligned data, caller extra fields, attributes, comments,
directory entries, reader limits and strict reading.

Items 1-10 below were what a general-purpose ZIP library had that this one
didn't, for the products that will need it (a game engine's asset packs, CGI
scene and cache files, mechanical CAD); all ten were built on 2026-10-30.

## The rule for adding any of it

Nothing here may change existing files or existing callers:

- **Files:** today's calls keep writing exactly the bytes they write now, and
  today's reader keeps opening every archive it opens now. The writer's output is
  plain ZIP, so a later feature is a different set of standard ZIP records, used
  only by the entries that ask for it.
- **Callers:** a feature is a new function, or a new struct chained onto an
  existing one (`rbtcore.h`, "Struct bases"). A caller that doesn't chain it on
  gets what it gets now, so leaving it out must mean "as before".

Each item says how it fits that rule.

## Built (2026-10-30, macOS): items 1-10

All ten, under the rule above: new calls, and fields at the end of
`RBTARCHIVE_CODEC`, `RBTARCHIVE_WRITER_DESC`, `RBTARCHIVE_ENTRY_DESC`,
`RBTARCHIVE_ENTRY_INFO` and `RBTARCHIVE_READER_DESC` whose zero is the old
behaviour (pre-1.0 ABI breaks, each listed against `abi.txt`; source is
unchanged for a caller that zeroes its structs). `test_old_bytes_unchanged`
holds the rule: an archive made with every call and field that existed
before, its CRC-32 and size recorded from the code as it was before these
items were written, and the same after. The reader opens every archive it
opened before: the shift (item 10) is taken only when the directory isn't
where the end record says.

1. **Streamed entries.** `rbtarchive_writer_begin_entry` /
   `write_entry_data` / `end_entry` (general-purpose bit 3, a data
   descriptor with its signature, 8-byte sizes under `zip64`; an entry
   reaching 4 GiB without `zip64` is `RBT_RESULT_LIMIT` before a byte past
   it); `rbtarchive_reader_open_entry` and `rbtarchive_entry_reader_read` /
   `_destroy`, the CRC-32 and size checked by the read that hands out the
   last byte. `RBTARCHIVE_CODEC` gains `stream_begin`, `stream_process` and
   `stream_end` (all or none). DEFLATE streams through zlib-ng 2.3.3,
   native mode (card A24), raw DEFLATE, window 15, memory level 8, default
   strategy, levels 0-9 (10 and up refused for a streamed entry); whole
   entries stay libdeflate's. A streamed archive's bytes are recorded in
   the tests (`test_streamed_entries`) so a platform whose zlib-ng picks
   other matches fails. Known difference, written into the header:
   libdeflate decodes DEFLATE data that uses a code its Huffman table left
   unassigned; zlib-ng refuses it, so such an entry reads whole and fails
   in pieces with `RBT_RESULT_CORRUPT_DATA` (found by `rbtarchive_fuzz`, kept
   in its regressions).
2. **Raw entries.** `rbtarchive_writer_add_raw_entry` (any method, the
   codec table not needed; STORED's sizes and CRC-32 checked, data with no
   stored bytes refused) and `rbtarchive_codecs_compress_bound` /
   `rbtarchive_codecs_compress`, which give exactly the bytes
   `add_entry` stores (tested: an archive written raw equals one written
   whole).
3. **Updating.** `RBTARCHIVE_UPDATE_DESC` (struct type 1005: the reader, a
   `truncate` callback, the sink, codecs) and
   `rbtarchive_writer_create_update`; `rbtarchive_writer_remove_entry`
   drops an entry, so replacing is remove and add. Kept entries are not
   read or moved; their central records are written again as read (less
   ZIP64 records, rewritten where needed), so updating with no change
   gives the same bytes. The cut happens just before the first byte is
   written: a writer destroyed unused leaves the archive alone. An archive
   with data in front is updated with true offsets.
4. **Directory entries:** `RBTARCHIVE_WRITER_DESC::allow_directory_entries`
   (STORED and empty; never streamed). Symbolic links are item 5's Unix
   mode (0120777) with the target as data.
5. **Attributes:** `host_system`, `internal_attributes`,
   `external_attributes` on the entry desc; `version_made_by`,
   `internal_attributes`, `external_attributes` on the info.
6. **Central extra:** `central_extra` / `central_extra_size` (NULL: the
   local extra, as before; non-NULL with size 0: none).
7. **Comments:** `comment` / `comment_size` on the desc and the info;
   `rbtarchive_writer_set_comment` (refusing the end record's signature
   inside it) and `rbtarchive_reader_get_comment`.
8. **Extra-field helpers:** `rbtarchive_extra_find`; the extended timestamp
   (0x5455), NTFS times (0x000A) and Unicode path (0x7075) read and
   written, times as `RBTARCHIVE_FILE_TIMES` (100 ns ticks since 1970,
   frozen).
9. **Strict reading:** `RBTARCHIVE_READER_DESC::strict`, `RBTARCHIVE_STRICT`
   bits: no archive comment, no entry comments, no directory entries, no
   gaps (reading every local header and data descriptor on open, the
   descriptor checked against the directory). A refusal is
   `RBT_RESULT_ARCHIVE_MALFORMED`; an unknown bit is
   `RBT_RESULT_INVALID_ARGUMENT`.
10. **Data in front:** the shift found from where the directory (and the
    ZIP64 end record) actually sits; every offset reported is the
    source's; `rbtarchive_reader_get_offset_shift`.

`rbtarchive_fuzz` covers each new reader path: pieces against whole reads,
the four strict flags, the comment, the shift, the extra helpers on every
entry, an update (one entry dropped, one added) reopened with every kept
entry as it was, a raw copy of every entry, and the rewrite streaming
every other entry. Also converted: the writer is allocated by hand
(`CLAUDE.md`, "Code conventions"), as the entry reader is.

**What has run where:** macOS (Apple M5 Pro, Debug), 2026-10-30: the
tests plain, under ASan and UBSan, and under TSan; `rbtarchive_fuzz` (see
"Testing still owed"). **Windows and Linux have built none of it:** there,
zlib-ng 2.3.3 through its own CMake under MSVC and GCC, `rbtarchive_tests`
(whose recorded streamed-DEFLATE bytes are the cross-platform check), the
fuzz target's replay, Windows' `-asan` and Linux's `-asan` and `-tsan`.
**Windows, 2026-10-30** (Windows 11 (ThinkPad P1 Gen 5; MSVC 14.51, Vulkan SDK 1.4.363.0, LLVM clang 23 for the fuzz preset)): zlib-ng 2.3.3 built by
MSVC through its own CMake with no change, and by clang for the fuzz
preset; `rbtarchive_tests` passes on `windows-d3d12` and `windows-vulkan`
in Debug, Release and RelWithDebInfo, and under both `-asan` presets:
its recorded streamed-DEFLATE bytes are MSVC's too, so zlib-ng's output
is the same on both platforms. `rbtarchive_fuzz` ran 10 minutes there
(1,834,271 runs): nothing found. **Linux has built none of it.**

## A codec's failure says which (card A52, decided 2026-11-14 with you: b; built on the Mac, Windows and Linux, checked off 2026-11-16)

Found 2026-11-13 by a caller's allocation-failure test on the Mac:
`RBTARCHIVE_CODEC`'s `compress` and `decompress` return a `bool`, so when
the DEFLATE codec can't allocate its libdeflate decompressor, a read
reports `RBT_RESULT_CORRUPT_DATA` and the caller tells its person the
archive is damaged when memory ran out.

- `compress` and `decompress` (and `stream_begin` and `stream_process`)
  return `RBT_RESULT`: `RBT_RESULT_OUT_OF_MEMORY` apart from corrupt data.
  A listed break of `RBTARCHIVE_CODEC` against `abi.txt`, before 1.0.
- The built-in DEFLATE codec makes its compressor and decompressor with
  `libdeflate_alloc_compressor_ex` / `_decompressor_ex`, given Rabbittus'
  allocation calls, so an allocation-failure test can fail them.
- Still one per call: the codecs table is read by any number of readers and
  writers at once and a reader's reads may overlap (`rbtarchive.h`,
  "Threads"), so a decompressor kept on the table would be shared by
  overlapping reads. A read with no allocation is card A32's, and the
  decompressor there is the read's or the caller's, never the table's.
- A draft header first (`include/archive/rbtarchive_codec_draft.h`), each
  change marked, for approval (card X21); then built with its tests first,
  the caller's allocation-failure test then passing its archive stages
  without counting them apart.
- **Drafted 2026-11-14** (`rbtarchive_codec_draft.h` at the repository's
  root, beside Rabbittus' other drafts; version 1, written unattended on
  the Mac; waiting on your review): `compress`, `decompress`,
  `stream_begin` (its state through an `out_state`) and `stream_process`
  return `RBT_RESULT`, each with a closed list of codes; `stream_end`
  unchanged; libdeflate through `_ex` with `malloc`/`free` named and
  zlib-ng's `zalloc`/`zfree` the same; 0.1.0 to 0.2.0. Four questions
  numbered there.
- **Reviewed 2026-11-14** (the workspace's review prompt of 2026-11-14; its record,
  in the workspace's `prompts/`, has every finding):
  - **Your answers (stage A):** Q1 a (a closed list of codes per
    callback, any other code `INVALID_ARGUMENT`), Q2 a (told by the
    compiler), Q3 a (`stream_end` stays `void`), Q4 a (the test codec and
    `rbtarchive_allocfail_tests`).
  - **Seven cycles of critique,** each writing its fixes in: the
    library's rules, libdeflate and zlib-ng underneath, the builder, the
    callers, failures and versions, threads, tests. The draft reached
    **version 7**. Q2's claim that the presets make an old codec a compile
    error was false (C2.7): Apple clang and GCC 14 refuse it by default,
    MSVC's C compiler only warns and an old codec is then misread.
    **Answered again 2026-11-14 with you: Q2 b,** a run-time refusal:
    `RBT_STRUCT_TYPE_ARCHIVE_CODEC_2` (1006), the old type refused by
    `rbtarchive_codecs_add()`. **Version 8.**
  - **APPROVED by you 2026-11-14, version 8;** frozen. SHA-256 (carriage
    returns removed) of `rbtarchive_codec_draft.h`:
    `9596cfa6dfdf11bb008040b96c110cd7b7adfaabee63424938da1fe54b87f3a5`.
    Ready to build, from the plan below: the Mac first,
    `../prompts/rbtarchive_codec_mac.md`, which writes the Windows and
    Fedora prompts.
  - **The build plan** (tests first, each step built and run):
    1. `tests/rbtarchive_tests.cpp`: the test codec (its `user` says which
       callback fails with which code), each code of Q1's lists and one
       outside, through every call that runs a codec; the two-thread case
       (two entry readers of one reader, overlapping, failing apart).
       Seen failing against today's bools.
    2. `tests/rbtarchive_allocfail_tests.c` and an injector beside
       `tests/alloctrace.h` (Apple interposing written and run here;
       Linux and Windows halves written, compiled on those machines),
       registered in `CMakeLists.txt` beside
       `rbtarchive_tests` (Rabbittus has no `tests/CMakeLists.txt`).
    3. `include/archive/rbtarchive.h`: `RBTARCHIVE_CODEC`'s four callback
       types and their comments; the calls' result lists; "@par Results".
       `include/rbtcore.h`: `RABBITTUS_VERSION_MINOR` 1 to 2 and
       `RBT_STRUCT_TYPE_ARCHIVE_CODEC_2` (1006), the old type kept,
       retired; `rbtarchive_codecs_add()` refusing it (test first).
    4. `src/archive/rbtarchive.cpp`: the DEFLATE codec (lines 240-330
       today: `_ex` with zeroed options and `sizeof_options`, `zalloc`/
       `zfree` of Rabbittus' own, every return a code); every call site
       (629, 691, 747, 1458, 1513, 1715, 2222 today) passing each listed
       code on with its message, any other as `INVALID_ARGUMENT` naming
       the method and code, an `OK` with no state likewise.
    5. `./check.sh --update-abi`, the misuse generator rerun, the break
       listed in the commit; `./check.sh`.
  - **Built on the Mac 2026-11-15** (Apple M5 Pro, macOS 27.0.1, Xcode
    27.2, Apple clang 21; `../prompts/rbtarchive_codec_mac.md`, its
    Progress; card X25: Windows and Linux owed,
    `../prompts/rbtarchive_codec_windows.md` and `_fedora.md`). Each plan
    step, by the X25 words:
    1. *Built and run:* `tests/rbtarchive_tests.cpp`'s test codec
       (method 99, its stored bytes a directive byte: the code to return),
       each code of Q1's lists and one outside them through
       `rbtarchive_reader_read_entry`, an entry reader's open and read,
       `rbtarchive_writer_add_entry`, `_begin_entry`, `_write_entry_data`,
       `_end_entry` and `rbtarchive_codecs_compress`; an OK with no state;
       less room than the bound refused before the codec; the states after
       a failure; the old type refused; two entry readers of one reader
       failing apart on two threads. Seen failing first: 8 compile errors
       against the bool callbacks.
    2. *Built and run on Apple; written, not compiled, for Linux and
       Windows:* `tests/rbtarchive_allocfail.h`, its Apple dylib
       `tests/rbtarchive_allocfail_interpose.c`, and
       `tests/rbtarchive_allocfail_tests.c`, registered beside
       `rbtarchive_tests`. The four DEFLATE paths make 15 (whole,
       compressing: a writer's entry and `rbtarchive_codecs_compress`), 20
       (streamed, compressing), 16 (whole, decompressing) and 20 (streamed,
       decompressing) allocations of ours; each failed in turn gives
       `RBT_RESULT_OUT_OF_MEMORY`, nothing left live. Seen failing first:
       6 runs gave `CORRUPT_DATA` or `INVALID_ARGUMENT`.
    3. *Built:* the headers as the draft has them; 0.2.0.
    4. *Built and run:* the DEFLATE codec through `_ex` with
       `malloc`/`free` and zlib-ng's `zalloc`/`zfree` of Rabbittus' own;
       every call site (`codec_fail`, `src/archive/rbtarchive.cpp`, words
       out of memory and a code outside a callback's list).
    5. *Run:* `./check.sh --update-abi` (the four callbacks' types changed,
       `RBT_STRUCT_TYPE_ARCHIVE_CODEC_2` added); the misuse generator rerun
       (the same calls; its own wording fixed, `claude_made_decisions.md`,
       2026-11-15).
    Every ctest (40) in Debug, Release, `-asan` and `-tsan`, no report, no
    warning; `rbt_misuse_tests` ten times, 0 failures (393 calls with
    zeroes, 144 with a destroyed handle); `rbtnoalloc_tests` none of ours,
    Debug and Release; `rbtarchive_fuzz` 15 minutes from a scratch
    corpus under `rabbittus-macos-metal-fuzz` (ASan and UBSan), 2,533,297
    inputs, nothing found, and its replay in every build. What builds on
    Rabbittus was rebuilt and tested on it: the workspace's record, the
    prompt's Progress.
  - **Built on Windows 2026-11-16** (Windows 11, the ThinkPad P1 Gen 5;
    MSVC 19.51.36260, toolset 14.51.36231, Windows SDK 10.0.26100.0;
    `../prompts/rbtarchive_codec_windows.md`, its Progress; card X25:
    Linux owed, `_fedora.md`). Each plan step, by the X25 words: 1, 3, 4
    and 5 *built and run on Windows* as the Mac has them; 2 *built and run
    on Windows*: the injector's Windows half (the executable's import
    slots) compiled with no change and no warning, at the presets' level
    (`/W3`, so no C4505). Its first run found what only MSVC can show:
    - **MSVC's STL allocates when a `std::unordered_set` or
      `std::unordered_map` is default-constructed** (a list sentinel and
      the buckets; libc++ and libstdc++ don't). `rbtarchive_writer_create`
      and `_create_update` (placement new of the writer, its `names`) and
      `rbtarchive_reader_open` (`new (std::nothrow)` of the reader, its
      `name_index`: `std::nothrow` covers only the block) built their
      object outside their `try`, so failing that allocation threw out of
      the C entry point: `std::terminate`, an abort whose Debug message box
      held the ctest to its 900 s timeout. Each now constructs inside a
      `try`, the block freed, `RBT_RESULT_OUT_OF_MEMORY`. A sweep of every
      module found one more, Rabbittus Sync's session create
      (`rbtsync_session_t`'s `awaiting_snapshot`), fixed the same way;
      Rabbittus Sync has no allocation-failure test to prove it (owed).
      The sweep looked for MSVC's allocating constructors; libstdc++'s
      `std::deque` allocates when default-constructed too (its map and
      first node), and two of Rend's creates built a deque-holding object
      outside a `try`: `rbtrend_shader_make` (the shader's reflection
      names) and `rbtrend_binding_layout_build` (its block names). Found
      on Fedora 44 2026-11-16 by a caller's allocation-failure test (a
      `std::bad_alloc` out of `rbtrend_shader_create`); each now constructs
      inside a `try`, the block freed, `RBT_RESULT_OUT_OF_MEMORY`. Debug
      and Release, `-asan` and `-tsan` there, 40 ctests each, passing.
    - **The injector's counts on Windows:** 20 (whole, compressing), 28
      (streamed, compressing), 17 (whole, decompressing) and 21 (streamed,
      decompressing) allocations of ours, the same in Debug and Release on
      both backends (the Debug STL's proxies passed over); each failed in
      turn gives `RBT_RESULT_OUT_OF_MEMORY`, nothing left live. Under both
      `-asan` presets it reports itself skipped and passes. Each failed
      allocation's stack, read once: libdeflate's compressor (the whole
      compressing path's 5th and 20th) and decompressor (the whole
      decompressing path's 14th and 17th) through `malloc_func` read
      after the patch, so through the hook; zlib-ng's through
      `deflate_zalloc`.
    - `rabbittus-windows-d3d12` and `-vulkan`, Debug and Release, and both
      `-asan` presets: no warning; every ctest (40) passing in each.
      `rbt_misuse_tests` ten times in Debug on each backend, 0 failures
      (393 calls with zeroes, 144 with a destroyed handle);
      `rbtnoalloc_tests` none of ours in its 16 scenarios, Debug and
      Release, both backends. `rbtarchive_fuzz` 15 minutes from a scratch
      corpus under `rabbittus-windows-d3d12-fuzz` (clang 23.1.4): 1,693,294
      inputs, nothing found; its replay passing in every build. That
      preset's build warns six times on the CRT's `strncpy` and `fopen`
      (clang's `-Wdeprecated-declarations`; `carrottus/src/crnet.c`,
      `craudiowasapi.c`, `tools/rbtrend_reflect`), none in A52's code.
    - What builds on Rabbittus was rebuilt and tested on it: the
      workspace's record, the prompt's Progress. A caller's
      allocation-failure test, its stacks read, found no failure of
      Rabbittus Archive's left: what it still reports wrongly on Windows
      is Carrottus Files' path conversion.
  - **Built on Linux 2026-11-16** (Fedora 44, the ThinkPad P1 Gen 5; GCC
    16.2.1, clang 22.1.8, glibc 2.43, libstdc++ under both;
    `../prompts/rbtarchive_codec_fedora.md`, its Progress). Each plan
    step, by the X25 words: 1, 3, 4 and 5 *built and run on Linux*; 2
    *built and run on Linux*: the injector's glibc half (malloc and the
    rest replaced in the executable, `dl_iterate_phdr` under
    `_GNU_SOURCE`) compiled with no change and no warning under GCC and
    clang, and needed no fix. Built on the Mac, Windows and Linux, **A52
    is checked off** (card X25).
    - **The injector's counts on Linux:** 15 (whole, compressing), 20
      (streamed, compressing), 16 (whole, decompressing) and 20 (streamed,
      decompressing) allocations of ours, the Mac's own, the same in Debug
      and Release with GCC and with clang (libstdc++, like libc++,
      allocates nothing for these paths' empty hash containers; MSVC's STL
      does, hence Windows' 20, 28, 17 and 21). Each failed in turn gives
      `RBT_RESULT_OUT_OF_MEMORY`, nothing left live. Under `-asan` and
      `-tsan` with each compiler it reports itself skipped and passes.
    - **libstdc++'s `operator new` reaches the executable's `malloc`:**
      the hooks are in the executable's `.dynsym` (the linker exports
      them because `libstdc++.so.6` names `malloc`), and a gdb breakpoint
      on the hook inside a region stopped 621 times with `operator
      new(unsigned long)` in `libstdc++.so.6` as its caller, from
      `std::vector<unsigned char>` growth in `rbtarchive.cpp`: vector
      growth is counted, and `ENABLE_EXPORTS` isn't needed. The same
      stacks show libdeflate's `_ex` compressor and decompressor taking
      the hook as `malloc_func`, and zlib-ng's through `deflate_zalloc`.
    - `rabbittus-linux-vulkan` Debug and Release, `-asan` and `-tsan`
      Debug, each with GCC and again with clang (`-clang` directories),
      every tree rebuilt whole: no warning; every ctest (40) passing in
      each of the eight builds; `rbtarchive_tests`' two-thread case clean
      under `-tsan` with both compilers. `rbt_misuse_tests` ten times in
      Debug with each compiler, 0 failures (393 calls with zeroes, 144
      with a destroyed handle); `rbtnoalloc_tests` none of ours in its 16
      scenarios, Debug and Release, both compilers. `rbtarchive_fuzz` 15
      minutes from a scratch corpus under `rabbittus-linux-vulkan-fuzz`
      (clang 22.1.8, ASan and UBSan): 14,524,743 inputs, nothing found; its
      replay passing in all eight builds. The fuzz build gives no warning.
    - What builds on Rabbittus was rebuilt and tested on it: the
      workspace's record, the prompt's Progress. A caller's
      allocation-failure test counted every stage with none wrong and
      none leaked. Nothing broke.
  - **For you (2026-11-15, from the build):**
    - **`rbtarchive_codecs_add()` has nowhere to put Q2 b's message.** It
      takes no `RBTARCHIVE_ERROR`, and the draft changes no call: an old
      codec is refused with `RBT_RESULT_STRUCT_WRONG_TYPE` alone, the
      "rebuild it" wording in the call's result list. A message there is
      an `out_error` parameter on the call, a public change for a draft of
      its own, or the code alone as built.
    - **C5.3's writer paragraph says more than the code does.** It says a
      writer whose `add_entry` or `begin_entry` failed in its codec "is
      failed"; the code (and an existing test: a refused stream level,
      then `finish`) leaves it usable, since nothing is written before the
      codec runs, and C5.3 was written "as the code has it". Built as the
      code has it, the header saying so; `write_entry_data` and
      `end_entry` fail the writer, as the draft says. If the writer is to
      fail after a codec's failure in those two calls too, it is a
      one-line change each and that test's change.
  - **The callers' follow-up** (C4.4, not this change's): a caller that
    told its person "damaged" on `CORRUPT_DATA` from a read now gets
    `OUT_OF_MEMORY` when memory ran out; its own message for that, and its
    allocation-failure test no longer counting the archive's stages apart,
    are its own TODO's once this is built.

## Smaller additions

- **Custom memory allocator.** Game engines expect to supply their own; today it's
  always the global allocator. Paused 2026-10-28 for every library (card
  X15); `eqstd`'s containers take an allocator when one is decided.
- **Progress and cancellation callbacks** for long compressions and reads.
- **Verifying an entry's CRC without returning its data,** for callers that
  memory-map stored entries and still want them checked.
- **Archive-level info:** whether ZIP64 is used, how many bytes come before the
  first entry.
- **Case-insensitive collision detection** when extracting to disk on Windows or
  macOS (`A.txt` and `a.txt`), as an option. Unicode normalization (NFC vs NFD)
  only needs a documented note.
- **A decompressor cache.** The DEFLATE codec allocates a libdeflate compressor or
  decompressor per call so it's safe from any thread; a per-thread cache would
  save that for archives with many small entries.

## Testing still owed

- **Fuzz the reader** - done 2026-10-28 on macOS (`tests/fuzz/rbtarchive_fuzz.cpp`,
  the `rabbittus-macos-metal-fuzz` preset; `CLAUDE.md`, "Tests"): open,
  every entry's info, name lookup, data offset and read, and the writer
  writing back what was read. About 14 minutes and 80 million inputs after
  the fix, with ASan and UBSan. It found one bug, fixed: the names buffer was
  reserved for an archive whose every entry fits, so a directory whose entry
  count promised more than it held grew the buffer under the name index's
  keys (a use-after-free, `tests/fuzz/regressions/rbtarchive_fuzz/`).
  Extended 2026-10-30 to items 1-10's reader paths (see "Built"): in its
  first minutes it found whole and streamed DEFLATE disagreeing on a
  stream with an unassigned Huffman code (kept as
  `deflate_unassigned_code_whole_only.zip`, documented, not a bug in
  either); then 15 minutes, 2,986,408 inputs, Debug, ASan and UBSan,
  found nothing. Still owed: running it on Linux and Windows, and longer
  runs on the CI machine.
- **Other tools:** CI opens what the tests write with Python's `zipfile` and
  Info-ZIP `unzip -t`, and Rabbittus Archive opens archives those tools wrote.
- **Real ZIP64 sizes:** the tests reach the ZIP64 records through an entry count
  over 65,535 and through `RBTARCHIVE_ENTRY_DESC::zip64`, never through an entry or
  an archive over 4 GiB.

## Out of scope, and the header says so

- **Encryption** (ZipCrypto or WinZip AES).
- **Split and multi-disk archives.**

## A compressed entry's read allocates (card A32, decided 2026-11-24 with you: a)

`rbtarchive_reader_read_entry` allocates three times a call (the local
header's look, the read's state, the DEFLATE decompressor) and the data
offset's getter once. The reader keeps what reading needs, made at open
or first use: a decompressor per codec and the local-header scratch, so a
repeated read allocates nothing. The reader's calls may overlap
("Threads"), and the codecs table is shared by any number of readers and
writers (card A52), so the kept state is per call's thread or follows the
overlap rule: settled in the change, with a no-allocation test of a
second DEFLATE read.
