/*
 * rbtarchive_allocfail.h - makes the Nth allocation of ours fail, for
 * rbtarchive_allocfail_tests.c (card A52): Rabbittus Archive's DEFLATE paths
 * failing for want of memory. Test code only, with globals of its own, beside
 * alloctrace.h (which only watches, and is left as it is). Written in C, for
 * the tests of C headers.
 *
 * Inside a region opened by alloc_fail_begin(n), on the thread that opened
 * it, the nth allocation made by code linked into the test executable
 * (Rabbittus, libdeflate, zlib-ng and the test itself) returns null; an
 * operator new of ours then throws std::bad_alloc, which the entry point
 * turns into RBT_RESULT_OUT_OF_MEMORY. The region counts every allocation of
 * ours it saw and keeps each one until it is freed, so what is still live at
 * any point is known. Only the opening thread is seen: the test runs on one.
 *
 * Whose an allocation is: the raw return addresses of its stack are walked
 * past the allocator's own frames (this injector's, the C runtime's and the
 * C++ runtime's: malloc's library, libc, libc++abi and libc++ on Apple; libc,
 * libstdc++ and libgcc on Linux), and the first one left is the asker. It is
 * ours if it lies in the test executable's image, whose range is read once,
 * before any region (alloc_fail_prepare). Nothing is looked up by symbol on
 * the hook's path and nothing is allocated inside the hook; a guard stops
 * the hook entering itself. A runtime's or a system framework's allocation
 * is never failed. Code whose allocations are a checking runtime's, not ours
 * (MSVC's Debug STL proxies, below), is read as ranges before any region,
 * and an allocation with a frame of it on its stack is never failed or
 * counted.
 *
 * How each platform's allocations are reached:
 * - macOS: a test-only dylib the test links (rbtarchive_allocfail_interpose.c)
 *   holds the hooks in dyld's __DATA,__interpose section, which dyld applies
 *   to every image loaded at launch: each image's calls to malloc, calloc,
 *   realloc, reallocf, valloc, aligned_alloc, posix_memalign and free reach
 *   them (libc++abi's operator new among them), and so does a pointer to
 *   malloc taken by the executable (libdeflate's options). The state lives
 *   in the dylib; the executable calls the functions below through it.
 *   Built and run on the Mac 2026-11-15.
 * - Linux (glibc): malloc and the rest replaced in the executable,
 *   forwarding to glibc's own (as alloctrace.h's Linux path); the image
 *   ranges from dl_iterate_phdr. Written, not compiled.
 * - Windows: the executable's import slots for the CRT's heap functions
 *   pointed at the hooks for the region's length (as alloctrace.h does for
 *   RtlAllocateHeap), so only the executable's own calls reach them, which
 *   is "ours" by construction. A Debug build frees through _free_dbg too,
 *   and its STL's iterator-check proxies, read by DbgHelp's SymEnumSymbols,
 *   are passed over. Written, not compiled.
 * - Under a sanitizer, whose heap replaces malloc, nothing is hooked and
 *   alloc_fail_available() says so; the test then reports itself skipped
 *   and passes.
 *
 * Include it in one file per executable.
 */

#ifndef RBTARCHIVE_ALLOCFAIL_H
#define RBTARCHIVE_ALLOCFAIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ALLOC_FAIL_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define ALLOC_FAIL_SANITIZED 1
#endif
#endif
#ifndef ALLOC_FAIL_SANITIZED
#define ALLOC_FAIL_SANITIZED 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* What a region saw: allocations of ours it counted, whether the one it was
 * asked to fail was reached, and how many of ours made in it are still
 * live. */
typedef struct alloc_fail_counts
{
    uint64_t made;
    bool failed;
    uint64_t live;
} alloc_fail_counts;

/* Whether this build hooks the allocator at all. */
#if !ALLOC_FAIL_SANITIZED && (defined(__APPLE__) || (defined(__linux__) && defined(__GLIBC__)) || defined(_WIN32))
#define ALLOC_FAIL_HOOKED 1
#else
#define ALLOC_FAIL_HOOKED 0
#endif

#if defined(_MSC_VER)
#define ALLOC_FAIL_NOINLINE __declspec(noinline)
#define ALLOC_FAIL_MAYBE_UNUSED
#else
#define ALLOC_FAIL_NOINLINE __attribute__((noinline))
#define ALLOC_FAIL_MAYBE_UNUSED __attribute__((unused))
#endif

/* The control calls. On Apple the dylib defines them (ALLOC_FAIL_DYLIB);
 * elsewhere this header does, static. */
#if defined(__APPLE__) && ALLOC_FAIL_HOOKED
#if defined(ALLOC_FAIL_DYLIB)
#define ALLOC_FAIL_API __attribute__((visibility("default")))
#else
#define ALLOC_FAIL_API extern
#define ALLOC_FAIL_DECLARATIONS_ONLY 1
#endif
#else
#define ALLOC_FAIL_API static ALLOC_FAIL_MAYBE_UNUSED
#endif

/* Whether allocations can be made to fail in this build. */
ALLOC_FAIL_API bool alloc_fail_available(void);
/* Reads the images' ranges, once, before any region: `ours` is the address
 * of any function in the test executable. Allocates (outside a region).
 * Returns false if a range couldn't be read, nothing failed then. */
ALLOC_FAIL_API bool alloc_fail_prepare(const void *ours);
/* Opens a region on this thread: the nth allocation of ours fails (1 the
 * first), or none for 0. */
ALLOC_FAIL_API void alloc_fail_begin(uint64_t fail_at);
/* What the region saw so far: live counts what it made, less what has been
 * freed since. */
ALLOC_FAIL_API alloc_fail_counts alloc_fail_counts_now(void);
/* Closes the region, returning what it saw. */
ALLOC_FAIL_API alloc_fail_counts alloc_fail_end(void);

#if !defined(ALLOC_FAIL_DECLARATIONS_ONLY)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ALLOC_FAIL_HOOKED

/* ---- The state, one region at a time ---- */

/* Allocations made in a region and still live, by address: open addressing,
 * a tombstone for a freed one. Larger than any path's count. */
#define ALLOC_FAIL_LIVE_SLOTS (1u << 16)
#define ALLOC_FAIL_TOMBSTONE ((uintptr_t)1)
/* Return addresses walked for an allocation. */
#define ALLOC_FAIL_DEPTH 64
/* Code ranges: the allocator's own images, and code passed over. */
#define ALLOC_FAIL_SKIP_RANGES 16
#define ALLOC_FAIL_PASSED_RANGES 256

typedef struct alloc_fail_range
{
    uintptr_t start;
    uintptr_t end;
} alloc_fail_range;

static uintptr_t alloc_fail_live_table[ALLOC_FAIL_LIVE_SLOTS];
static uint64_t alloc_fail_live_count;
static alloc_fail_range alloc_fail_ours;
static alloc_fail_range alloc_fail_skips[ALLOC_FAIL_SKIP_RANGES];
static uint32_t alloc_fail_skip_count;
static alloc_fail_range alloc_fail_passed[ALLOC_FAIL_PASSED_RANGES];
static uint32_t alloc_fail_passed_count;
static bool alloc_fail_prepared;
/* The region: open, counting, the hook inside itself, the count and the
 * target. */
static volatile bool alloc_fail_open;
static volatile bool alloc_fail_counting;
static volatile bool alloc_fail_inside;
static uint64_t alloc_fail_made;
static uint64_t alloc_fail_target;
static bool alloc_fail_hit;

static bool alloc_fail_in(const alloc_fail_range *ranges, uint32_t count, uintptr_t pc)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (pc >= ranges[i].start && pc < ranges[i].end)
            return true;
    }
    return false;
}

static void alloc_fail_keep(void *p)
{
    uintptr_t key = (uintptr_t)p;
    uint32_t at = (uint32_t)((key >> 4) * 2654435761u) & (ALLOC_FAIL_LIVE_SLOTS - 1);
    for (uint32_t probe = 0; probe < ALLOC_FAIL_LIVE_SLOTS; ++probe)
    {
        uintptr_t *slot = &alloc_fail_live_table[(at + probe) & (ALLOC_FAIL_LIVE_SLOTS - 1)];
        if (*slot == 0 || *slot == ALLOC_FAIL_TOMBSTONE)
        {
            *slot = key;
            ++alloc_fail_live_count;
            return;
        }
    }
}

static void alloc_fail_forget(void *p)
{
    uintptr_t key = (uintptr_t)p;
    uint32_t at = (uint32_t)((key >> 4) * 2654435761u) & (ALLOC_FAIL_LIVE_SLOTS - 1);
    for (uint32_t probe = 0; probe < ALLOC_FAIL_LIVE_SLOTS; ++probe)
    {
        uintptr_t *slot = &alloc_fail_live_table[(at + probe) & (ALLOC_FAIL_LIVE_SLOTS - 1)];
        if (*slot == 0)
            return;
        if (*slot == key)
        {
            *slot = ALLOC_FAIL_TOMBSTONE;
            --alloc_fail_live_count;
            return;
        }
    }
}

/* Decides for an allocation on the region's thread, from its stack (the
 * hook's own frames first): 0 not ours, 1 ours and made, 2 ours and to fail.
 * Counts it when ours. */
static int alloc_fail_decide(void *const *pcs, int depth)
{
    int asker = -1;
    for (int i = 0; i < depth; ++i)
    {
        const uintptr_t pc = (uintptr_t)pcs[i];
        if (alloc_fail_in(alloc_fail_passed, alloc_fail_passed_count, pc))
            return 0;
        if (asker < 0 && !alloc_fail_in(alloc_fail_skips, alloc_fail_skip_count, pc))
            asker = i;
    }
    if (asker < 0)
        return 0;
    const uintptr_t pc = (uintptr_t)pcs[asker];
    if (pc < alloc_fail_ours.start || pc >= alloc_fail_ours.end)
        return 0;
    ++alloc_fail_made;
    if (alloc_fail_target != 0 && alloc_fail_made == alloc_fail_target)
    {
        alloc_fail_hit = true;
        return 2;
    }
    return 1;
}

static void alloc_fail_made_now(int decided, void *p)
{
    if (decided == 1 && p)
    {
        alloc_fail_inside = true;
        alloc_fail_keep(p);
        alloc_fail_inside = false;
    }
}

/* ---- Platforms: the stack, the ranges and the hooks ---- */

#if defined(__APPLE__)

#include <errno.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <malloc/malloc.h>
#include <pthread.h>

static pthread_t alloc_fail_thread;

static bool alloc_fail_on_region_thread(void)
{
    return alloc_fail_open && pthread_equal(pthread_self(), alloc_fail_thread);
}
static void alloc_fail_platform_begin(void)
{
    alloc_fail_thread = pthread_self();
}
static void alloc_fail_platform_end(void)
{
}

/* An image's __TEXT in memory. */
static bool alloc_fail_image_text(const struct mach_header_64 *header, intptr_t slide, alloc_fail_range *out_text)
{
    const uint8_t *command = (const uint8_t *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        const struct load_command *lc = (const struct load_command *)command;
        if (lc->cmd == LC_SEGMENT_64)
        {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            if (strcmp(segment->segname, SEG_TEXT) == 0)
            {
                out_text->start = (uintptr_t)(segment->vmaddr + (uint64_t)slide);
                out_text->end = out_text->start + (uintptr_t)segment->vmsize;
                return true;
            }
        }
        command += lc->cmdsize;
    }
    return false;
}

static bool alloc_fail_named(const char *path, const char *suffix)
{
    const size_t length = strlen(path), tail = strlen(suffix);
    return length >= tail && strcmp(path + length - tail, suffix) == 0;
}

ALLOC_FAIL_API bool alloc_fail_available(void)
{
    return true;
}

ALLOC_FAIL_API bool alloc_fail_prepare(const void *ours)
{
    static const char *const allocators[] = {
        "/libsystem_malloc.dylib", "/libsystem_c.dylib", "/libc++abi.dylib", "/libc++.1.dylib", "/libc++.dylib",
    };
    const uintptr_t mine = (uintptr_t)(const void *)alloc_fail_decide;
    alloc_fail_skip_count = 0;
    alloc_fail_passed_count = 0;
    bool found_ours = false, found_self = false;
    const uint32_t images = _dyld_image_count();
    for (uint32_t i = 0; i < images; ++i)
    {
        const struct mach_header *header = _dyld_get_image_header(i);
        const char *name = _dyld_get_image_name(i);
        if (!header || header->magic != MH_MAGIC_64 || !name)
            continue;
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        alloc_fail_range text;
        if (!alloc_fail_image_text((const struct mach_header_64 *)header, slide, &text))
            continue;
        if ((uintptr_t)ours >= text.start && (uintptr_t)ours < text.end)
        {
            alloc_fail_ours = text;
            found_ours = true;
        }
        bool skip = mine >= text.start && mine < text.end;
        found_self = found_self || skip;
        for (size_t a = 0; a < sizeof(allocators) / sizeof(allocators[0]); ++a)
            skip = skip || alloc_fail_named(name, allocators[a]);
        if (skip && alloc_fail_skip_count < ALLOC_FAIL_SKIP_RANGES)
            alloc_fail_skips[alloc_fail_skip_count++] = text;
    }
    alloc_fail_prepared = found_ours && found_self;
    return alloc_fail_prepared;
}

/* The hooks: interposed, so calling malloc and the rest from inside them is
 * calling the real ones (dyld leaves the interposing image's own calls). */
static ALLOC_FAIL_NOINLINE int alloc_fail_note(void)
{
    if (!alloc_fail_on_region_thread() || !alloc_fail_counting || alloc_fail_inside)
        return 0;
    alloc_fail_inside = true;
    void *pcs[ALLOC_FAIL_DEPTH];
    const int depth = backtrace(pcs, ALLOC_FAIL_DEPTH);
    const int decided = alloc_fail_decide(pcs, depth);
    alloc_fail_inside = false;
    return decided;
}

static void alloc_fail_freed(void *p)
{
    if (p && alloc_fail_on_region_thread() && !alloc_fail_inside)
    {
        alloc_fail_inside = true;
        alloc_fail_forget(p);
        alloc_fail_inside = false;
    }
}

/* A refused allocation says ENOMEM, as a real allocator does (POSIX), so a
 * library that reads errno after a failed call sees it. */
static void *alloc_fail_malloc(size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = malloc(size);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *alloc_fail_calloc(size_t count, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = calloc(count, size);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *alloc_fail_realloc(void *old, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = realloc(old, size);
    if (p && old)
        alloc_fail_freed(old);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *alloc_fail_reallocf(void *old, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        alloc_fail_freed(old);
        free(old);
        errno = ENOMEM;
        return NULL;
    }
    void *p = reallocf(old, size);
    if (old)
        alloc_fail_freed(old);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *alloc_fail_valloc(size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = valloc(size);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *alloc_fail_aligned_alloc(size_t alignment, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = aligned_alloc(alignment, size);
    alloc_fail_made_now(decided, p);
    return p;
}
static int alloc_fail_posix_memalign(void **out, size_t alignment, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
        return ENOMEM;
    const int result = posix_memalign(out, alignment, size);
    alloc_fail_made_now(decided, result == 0 ? *out : NULL);
    return result;
}
static void alloc_fail_free(void *p)
{
    alloc_fail_freed(p);
    free(p);
}

typedef struct alloc_fail_interpose
{
    const void *replacement;
    const void *replacee;
} alloc_fail_interpose;

#if defined(ALLOC_FAIL_DYLIB)
#define ALLOC_FAIL_INTERPOSE(replacement, replacee)                                                                    \
    __attribute__((used)) static const alloc_fail_interpose alloc_fail_interpose_##replacee                            \
        __attribute__((section("__DATA,__interpose"))) = {(const void *)(replacement), (const void *)(replacee)}
ALLOC_FAIL_INTERPOSE(alloc_fail_malloc, malloc);
ALLOC_FAIL_INTERPOSE(alloc_fail_calloc, calloc);
ALLOC_FAIL_INTERPOSE(alloc_fail_realloc, realloc);
ALLOC_FAIL_INTERPOSE(alloc_fail_reallocf, reallocf);
ALLOC_FAIL_INTERPOSE(alloc_fail_valloc, valloc);
ALLOC_FAIL_INTERPOSE(alloc_fail_aligned_alloc, aligned_alloc);
ALLOC_FAIL_INTERPOSE(alloc_fail_posix_memalign, posix_memalign);
ALLOC_FAIL_INTERPOSE(alloc_fail_free, free);
#endif

#elif defined(__linux__) && defined(__GLIBC__)

/* Written, not compiled: against glibc's and the ELF specification's
 * documentation, and alloctrace.h's Linux path. Needs _GNU_SOURCE before any
 * system header (dl_iterate_phdr), which CMakeLists.txt sets on the test. */
#include <errno.h>
#include <execinfo.h>
#include <link.h>
#include <pthread.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);
extern void __libc_free(void *);

static pthread_t alloc_fail_thread;
/* The hooks below are in the executable, so their own frames are passed over
 * by count: the note's and the hook's. */
#define ALLOC_FAIL_OWN_FRAMES 2

static bool alloc_fail_on_region_thread(void)
{
    return alloc_fail_open && pthread_equal(pthread_self(), alloc_fail_thread);
}
static void alloc_fail_platform_begin(void)
{
    void *warm[4];
    backtrace(warm, 4); /* glibc's first backtrace loads libgcc_s */
    alloc_fail_thread = pthread_self();
}
static void alloc_fail_platform_end(void)
{
}

typedef struct alloc_fail_phdr_walk
{
    const void *ours;
    bool found;
} alloc_fail_phdr_walk;

static int alloc_fail_phdr(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    alloc_fail_phdr_walk *walk = (alloc_fail_phdr_walk *)data;
    static const char *const allocators[] = {"/libc.so", "/libstdc++.so", "/libgcc_s.so"};
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X))
            continue;
        alloc_fail_range text = {(uintptr_t)(info->dlpi_addr + ph->p_vaddr),
                                 (uintptr_t)(info->dlpi_addr + ph->p_vaddr + ph->p_memsz)};
        if ((uintptr_t)walk->ours >= text.start && (uintptr_t)walk->ours < text.end)
        {
            alloc_fail_ours = text;
            walk->found = true;
        }
        bool skip = false;
        for (size_t a = 0; a < sizeof(allocators) / sizeof(allocators[0]); ++a)
            skip = skip || (info->dlpi_name && strstr(info->dlpi_name, allocators[a]) != NULL);
        if (skip && alloc_fail_skip_count < ALLOC_FAIL_SKIP_RANGES)
            alloc_fail_skips[alloc_fail_skip_count++] = text;
    }
    return 0;
}

ALLOC_FAIL_API bool alloc_fail_available(void)
{
    return true;
}

ALLOC_FAIL_API bool alloc_fail_prepare(const void *ours)
{
    alloc_fail_skip_count = 0;
    alloc_fail_passed_count = 0;
    alloc_fail_phdr_walk walk = {ours, false};
    dl_iterate_phdr(alloc_fail_phdr, &walk);
    alloc_fail_prepared = walk.found;
    return alloc_fail_prepared;
}

static ALLOC_FAIL_NOINLINE int alloc_fail_note(void)
{
    if (!alloc_fail_on_region_thread() || !alloc_fail_counting || alloc_fail_inside)
        return 0;
    alloc_fail_inside = true;
    void *pcs[ALLOC_FAIL_DEPTH];
    const int depth = backtrace(pcs, ALLOC_FAIL_DEPTH);
    const int decided = depth > ALLOC_FAIL_OWN_FRAMES
                            ? alloc_fail_decide(pcs + ALLOC_FAIL_OWN_FRAMES, depth - ALLOC_FAIL_OWN_FRAMES)
                            : 0;
    alloc_fail_inside = false;
    return decided;
}
static void alloc_fail_freed(void *p)
{
    if (p && alloc_fail_on_region_thread() && !alloc_fail_inside)
    {
        alloc_fail_inside = true;
        alloc_fail_forget(p);
        alloc_fail_inside = false;
    }
}

/* Each hook calls the note itself, so the frames passed over are the note's
 * and the hook's. A refused allocation says ENOMEM, as a real allocator
 * does. */
void *malloc(size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = __libc_malloc(size);
    alloc_fail_made_now(decided, p);
    return p;
}
void *calloc(size_t count, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = __libc_calloc(count, size);
    alloc_fail_made_now(decided, p);
    return p;
}
void *realloc(void *old, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = __libc_realloc(old, size);
    if (p && old)
        alloc_fail_freed(old);
    alloc_fail_made_now(decided, p);
    return p;
}
void *memalign(size_t alignment, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = __libc_memalign(alignment, size);
    alloc_fail_made_now(decided, p);
    return p;
}
void *aligned_alloc(size_t alignment, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = __libc_memalign(alignment, size);
    alloc_fail_made_now(decided, p);
    return p;
}
int posix_memalign(void **out, size_t alignment, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
        return ENOMEM;
    *out = __libc_memalign(alignment, size);
    alloc_fail_made_now(decided, *out);
    return *out ? 0 : ENOMEM;
}
void free(void *p)
{
    alloc_fail_freed(p);
    __libc_free(p);
}

#elif defined(_WIN32)

/* Written, not compiled: against the PE/COFF specification, the UCRT's and
 * DbgHelp's documentation, and alloctrace.h's Windows path. Only the
 * executable's import slots are pointed at the hooks, so every call reaching
 * them is the executable's own (ours); a DLL's never is. */
/* windows.h first: dbghelp.h uses its types without including it. */
#include <windows.h>
#include <dbghelp.h>
#include <errno.h>
#include <malloc.h>
#pragma comment(lib, "dbghelp.lib")

typedef void *(__cdecl *alloc_fail_malloc_fn)(size_t);
typedef void *(__cdecl *alloc_fail_calloc_fn)(size_t, size_t);
typedef void *(__cdecl *alloc_fail_realloc_fn)(void *, size_t);
typedef void(__cdecl *alloc_fail_free_fn)(void *);
typedef void(__cdecl *alloc_fail_free_dbg_fn)(void *, int);
typedef void *(__cdecl *alloc_fail_aligned_malloc_fn)(size_t, size_t);
typedef void *(__cdecl *alloc_fail_aligned_realloc_fn)(void *, size_t, size_t);
static alloc_fail_malloc_fn alloc_fail_real_malloc;
static alloc_fail_calloc_fn alloc_fail_real_calloc;
static alloc_fail_realloc_fn alloc_fail_real_realloc;
static alloc_fail_free_fn alloc_fail_real_free;
static alloc_fail_free_dbg_fn alloc_fail_real_free_dbg;
static alloc_fail_aligned_malloc_fn alloc_fail_real_aligned_malloc;
static alloc_fail_aligned_realloc_fn alloc_fail_real_aligned_realloc;
static alloc_fail_free_fn alloc_fail_real_aligned_free;
static DWORD alloc_fail_thread;
/* The hook's and the note's frames, passed over by count. */
#define ALLOC_FAIL_OWN_FRAMES 2

static bool alloc_fail_on_region_thread(void)
{
    return alloc_fail_open && GetCurrentThreadId() == alloc_fail_thread;
}

typedef struct alloc_fail_slot
{
    void **slot;
    void *was;
} alloc_fail_slot;
static alloc_fail_slot alloc_fail_slots[64];
static unsigned alloc_fail_slot_count;

static void alloc_fail_set_slot(void **slot, void *to)
{
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
        return;
    *(void *volatile *)slot = to;
    VirtualProtect(slot, sizeof(void *), old, &old);
}

static ALLOC_FAIL_NOINLINE int alloc_fail_note(void)
{
    if (!alloc_fail_on_region_thread() || !alloc_fail_counting || alloc_fail_inside)
        return 0;
    alloc_fail_inside = true;
    void *pcs[ALLOC_FAIL_DEPTH];
    const USHORT depth = RtlCaptureStackBackTrace(ALLOC_FAIL_OWN_FRAMES, ALLOC_FAIL_DEPTH, pcs, NULL);
    const int decided = alloc_fail_decide(pcs, (int)depth);
    alloc_fail_inside = false;
    return decided;
}
static void alloc_fail_freed(void *p)
{
    if (p && alloc_fail_on_region_thread() && !alloc_fail_inside)
    {
        alloc_fail_inside = true;
        alloc_fail_forget(p);
        alloc_fail_inside = false;
    }
}

/* A refused allocation says ENOMEM, as the CRT's does. */
static void *__cdecl alloc_fail_malloc(size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = alloc_fail_real_malloc(size);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *__cdecl alloc_fail_calloc(size_t count, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = alloc_fail_real_calloc(count, size);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *__cdecl alloc_fail_realloc(void *old, size_t size)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = alloc_fail_real_realloc(old, size);
    if (p && old)
        alloc_fail_freed(old);
    alloc_fail_made_now(decided, p);
    return p;
}
static void __cdecl alloc_fail_free(void *p)
{
    alloc_fail_freed(p);
    alloc_fail_real_free(p);
}
/* The debug CRT's operator delete frees through _free_dbg, not free
 * (vcruntime's delete_scalar.cpp), so a Debug build's every delete comes
 * here; operator new still allocates through malloc. */
static void __cdecl alloc_fail_free_dbg(void *p, int block_use)
{
    alloc_fail_freed(p);
    alloc_fail_real_free_dbg(p, block_use);
}
static void *__cdecl alloc_fail_aligned_malloc(size_t size, size_t alignment)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = alloc_fail_real_aligned_malloc(size, alignment);
    alloc_fail_made_now(decided, p);
    return p;
}
static void *__cdecl alloc_fail_aligned_realloc(void *old, size_t size, size_t alignment)
{
    const int decided = alloc_fail_note();
    if (decided == 2)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = alloc_fail_real_aligned_realloc(old, size, alignment);
    if (p && old)
        alloc_fail_freed(old);
    alloc_fail_made_now(decided, p);
    return p;
}
static void __cdecl alloc_fail_aligned_free(void *p)
{
    alloc_fail_freed(p);
    alloc_fail_real_aligned_free(p);
}

/* The executable's import slots of the CRT's heap functions, pointed at the
 * hooks; the names are the UCRT's, whichever DLL (ucrtbase, ucrtbased,
 * api-ms-win-crt-heap) the slot imports them from. */
static void alloc_fail_patch(bool on)
{
    if (!on)
    {
        for (unsigned i = 0; i < alloc_fail_slot_count; ++i)
            alloc_fail_set_slot(alloc_fail_slots[i].slot, alloc_fail_slots[i].was);
        alloc_fail_slot_count = 0;
        return;
    }
    /* Each hook, and where the function its slot held is kept: the real
     * one, as the executable was linked (ucrtbase or ucrtbased). */
    static const struct
    {
        const char *name;
        void *hook;
        void **real;
    } hooks[] = {
        {"malloc", (void *)alloc_fail_malloc, (void **)&alloc_fail_real_malloc},
        {"calloc", (void *)alloc_fail_calloc, (void **)&alloc_fail_real_calloc},
        {"realloc", (void *)alloc_fail_realloc, (void **)&alloc_fail_real_realloc},
        {"free", (void *)alloc_fail_free, (void **)&alloc_fail_real_free},
        {"_free_dbg", (void *)alloc_fail_free_dbg, (void **)&alloc_fail_real_free_dbg},
        {"_aligned_malloc", (void *)alloc_fail_aligned_malloc, (void **)&alloc_fail_real_aligned_malloc},
        {"_aligned_realloc", (void *)alloc_fail_aligned_realloc, (void **)&alloc_fail_real_aligned_realloc},
        {"_aligned_free", (void *)alloc_fail_aligned_free, (void **)&alloc_fail_real_aligned_free},
    };
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0)
        return;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; ++d)
    {
        if (!d->OriginalFirstThunk)
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        void **slots = (void **)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char *name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (size_t h = 0; h < sizeof(hooks) / sizeof(hooks[0]); ++h)
            {
                if (strcmp(name, hooks[h].name) != 0)
                    continue;
                if (alloc_fail_slot_count == sizeof(alloc_fail_slots) / sizeof(alloc_fail_slots[0]))
                    continue;
                alloc_fail_slots[alloc_fail_slot_count].slot = slots;
                alloc_fail_slots[alloc_fail_slot_count].was = *slots;
                ++alloc_fail_slot_count;
                *hooks[h].real = *slots;
                alloc_fail_set_slot(slots, hooks[h].hook);
            }
        }
    }
}

static void alloc_fail_platform_begin(void)
{
    alloc_fail_thread = GetCurrentThreadId();
    alloc_fail_patch(true);
}
static void alloc_fail_platform_end(void)
{
    alloc_fail_patch(false);
}

static BOOL CALLBACK alloc_fail_symbol(PSYMBOL_INFO info, ULONG size, PVOID user)
{
    (void)user;
    if (alloc_fail_passed_count < ALLOC_FAIL_PASSED_RANGES && info->Size > 0)
    {
        alloc_fail_passed[alloc_fail_passed_count].start = (uintptr_t)info->Address;
        alloc_fail_passed[alloc_fail_passed_count].end = (uintptr_t)(info->Address + (size ? size : info->Size));
        ++alloc_fail_passed_count;
    }
    return TRUE;
}

ALLOC_FAIL_API bool alloc_fail_available(void)
{
    return true;
}

ALLOC_FAIL_API bool alloc_fail_prepare(const void *ours)
{
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)ours, &module))
        return false;
    if (module != GetModuleHandleW(NULL))
        return false;
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    alloc_fail_ours.start = (uintptr_t)base;
    alloc_fail_ours.end = (uintptr_t)base + nt->OptionalHeader.SizeOfImage;
    alloc_fail_skip_count = 0;
    alloc_fail_passed_count = 0;
    /* With the debug STL's iterator checks (_ITERATOR_DEBUG_LEVEL 2, MSVC's
     * Debug) every container allocates a proxy for them, even a vector's
     * noexcept default constructor, where a failure is std::terminate. The
     * proxies are the STL's checking, not ours, and a Release build has
     * none, so their allocators are passed over: never failed or counted. */
    SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(GetCurrentProcess(), NULL, TRUE))
        return false;
    SymEnumSymbols(GetCurrentProcess(), (ULONG64)(uintptr_t)base, "std::_Container_base12::_Alloc_proxy*",
                   alloc_fail_symbol, NULL);
    SymEnumSymbols(GetCurrentProcess(), (ULONG64)(uintptr_t)base, "std::_Container_proxy_ptr12*", alloc_fail_symbol,
                   NULL);
    SymCleanup(GetCurrentProcess());
    alloc_fail_prepared = true;
    return true;
}

#endif

/* ---- The region ---- */

ALLOC_FAIL_API void alloc_fail_begin(uint64_t fail_at)
{
    memset(alloc_fail_live_table, 0, sizeof(alloc_fail_live_table));
    alloc_fail_live_count = 0;
    alloc_fail_made = 0;
    alloc_fail_target = fail_at;
    alloc_fail_hit = false;
    alloc_fail_inside = false;
    if (!alloc_fail_prepared)
        return;
    alloc_fail_platform_begin();
    alloc_fail_counting = true;
    alloc_fail_open = true;
}

ALLOC_FAIL_API alloc_fail_counts alloc_fail_counts_now(void)
{
    alloc_fail_counts counts;
    counts.made = alloc_fail_made;
    counts.failed = alloc_fail_hit;
    counts.live = alloc_fail_live_count;
    return counts;
}

ALLOC_FAIL_API alloc_fail_counts alloc_fail_end(void)
{
    alloc_fail_counting = false;
    alloc_fail_open = false;
    if (alloc_fail_prepared)
        alloc_fail_platform_end();
    return alloc_fail_counts_now();
}

#else /* not hooked: under a sanitizer, or a platform with no hook */

ALLOC_FAIL_API bool alloc_fail_available(void)
{
    return false;
}
ALLOC_FAIL_API bool alloc_fail_prepare(const void *ours)
{
    (void)ours;
    return false;
}
ALLOC_FAIL_API void alloc_fail_begin(uint64_t fail_at)
{
    (void)fail_at;
}
ALLOC_FAIL_API alloc_fail_counts alloc_fail_counts_now(void)
{
    alloc_fail_counts counts = {0, false, 0};
    return counts;
}
ALLOC_FAIL_API alloc_fail_counts alloc_fail_end(void)
{
    return alloc_fail_counts_now();
}

#endif /* ALLOC_FAIL_HOOKED */

#endif /* !ALLOC_FAIL_DECLARATIONS_ONLY */

#ifdef __cplusplus
}
#endif

#endif /* RBTARCHIVE_ALLOCFAIL_H */
