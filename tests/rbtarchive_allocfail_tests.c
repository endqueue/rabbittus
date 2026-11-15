// Rabbittus Archive's DEFLATE paths failing for want of memory (card A52,
// RBTARCHIVE_TODO.md, "A codec's failure says which"). Each of the four
// paths, whole and streamed, compressing and decompressing, is run once
// whole to count the allocations of ours it makes, then again failing its
// Nth allocation for every N: each run returns RBT_RESULT_OUT_OF_MEMORY,
// never RBT_RESULT_CORRUPT_DATA or another code, and leaves nothing of ours
// allocated. tests/rbtarchive_allocfail.h says how each platform's
// allocations are failed; under a sanitizer the test reports itself skipped.
//
// The archive's sink and source are fixed buffers, so nothing the test
// itself does inside a region allocates.
//
// Written in C, as the other tests of C headers are.

#include "archive/rbtarchive.h"
#include "rbtarchive_allocfail.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(condition)                                                                                              \
    do                                                                                                                \
    {                                                                                                                 \
        if (!(condition))                                                                                             \
        {                                                                                                             \
            printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #condition);                                            \
            failures++;                                                                                               \
        }                                                                                                             \
    } while (0)

// ---- Buffers ------------------------------------------------------------------

// More than a streamed entry's 64 KiB pieces, so each stream runs a while.
#define DATA_SIZE (200u * 1024u)
#define ARCHIVE_CAPACITY (1u << 20)

static uint8_t data[DATA_SIZE];
static uint8_t out[DATA_SIZE];

typedef struct buffer_t
{
    uint8_t bytes[ARCHIVE_CAPACITY];
    uint64_t size;
} buffer_t;

static buffer_t written; // what a compressing path writes
static buffer_t archive; // what a decompressing path reads

static bool buffer_write(void *user, const void *bytes, uint64_t count)
{
    buffer_t *buffer = (buffer_t *)user;
    if (count > ARCHIVE_CAPACITY - buffer->size)
        return false;
    memcpy(buffer->bytes + buffer->size, bytes, (size_t)count);
    buffer->size += count;
    return true;
}

static bool buffer_read(void *user, uint64_t offset, void *bytes, uint64_t count)
{
    const buffer_t *buffer = (const buffer_t *)user;
    if (offset > buffer->size || count > buffer->size - offset)
        return false;
    memcpy(bytes, buffer->bytes + offset, (size_t)count);
    return true;
}

// Text that compresses, but not to nothing: words from a small list, chosen
// by a fixed sequence.
static void make_data(void)
{
    static const char *const words[] = {"archive ", "entry ", "deflate ", "stream ", "codec ", "memory ", "data ",
                                        "\n"};
    uint32_t state = 12345;
    size_t at = 0;
    while (at < DATA_SIZE)
    {
        state = state * 1103515245u + 12345u;
        const char *word = words[(state >> 16) % 8];
        size_t length = strlen(word);
        if (length > DATA_SIZE - at)
            length = DATA_SIZE - at;
        memcpy(data + at, word, length);
        at += length;
    }
}

static RBTARCHIVE_ENTRY_DESC entry_desc(const char *name)
{
    RBTARCHIVE_ENTRY_DESC entry;
    memset(&entry, 0, sizeof(entry));
    entry.base.type = RBT_STRUCT_TYPE_ARCHIVE_ENTRY_DESC;
    entry.name = name;
    entry.method = RBTARCHIVE_METHOD_DEFLATE;
    entry.level = 6;
    entry.dos_date = 0x0021; // 1980-01-01
    return entry;
}

static RBT_RESULT make_writer(rbtarchive_codecs codecs, rbtarchive_writer *out_writer)
{
    RBTARCHIVE_WRITER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.base.type = RBT_STRUCT_TYPE_ARCHIVE_WRITER_DESC;
    desc.sink.user = &written;
    desc.sink.write = buffer_write;
    desc.codecs = codecs;
    written.size = 0;
    return rbtarchive_writer_create(&desc, out_writer);
}

static RBT_RESULT open_reader(rbtarchive_codecs codecs, rbtarchive_reader *out_reader)
{
    RBTARCHIVE_READER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.base.type = RBT_STRUCT_TYPE_ARCHIVE_READER_DESC;
    desc.source.user = &archive;
    desc.source.size = archive.size;
    desc.source.read = buffer_read;
    desc.codecs = codecs;
    desc.max_entry_count = UINT32_MAX;
    desc.max_central_directory_size = UINT64_MAX;
    desc.max_entry_uncompressed_size = UINT64_MAX;
    desc.max_total_uncompressed_size = UINT64_MAX;
    desc.max_compression_ratio = UINT32_MAX;
    return rbtarchive_reader_open(&desc, out_reader, NULL);
}

// ---- The four paths -------------------------------------------------------------
//
// Each makes what it needs, runs, destroys it all and returns the first
// failure, or RBT_RESULT_OK.

// Whole, compressing (libdeflate): an entry added to a writer, and the same
// data through rbtarchive_codecs_compress().
static RBT_RESULT path_whole_compress(rbtarchive_codecs codecs)
{
    rbtarchive_writer writer = NULL;
    RBT_RESULT result = make_writer(codecs, &writer);
    if (result != RBT_RESULT_OK)
        return result;
    RBTARCHIVE_ENTRY_DESC entry = entry_desc("whole.txt");
    result = rbtarchive_writer_add_entry(writer, &entry, data, DATA_SIZE, NULL);
    if (result == RBT_RESULT_OK)
        result = rbtarchive_writer_finish(writer, NULL);
    rbtarchive_writer_destroy(writer);
    if (result != RBT_RESULT_OK)
        return result;
    uint64_t size = 0;
    return rbtarchive_codecs_compress(codecs, RBTARCHIVE_METHOD_DEFLATE, 6, data, DATA_SIZE, written.bytes,
                                      ARCHIVE_CAPACITY, &size, NULL);
}

// Streamed, compressing (zlib-ng): an entry in three pieces.
static RBT_RESULT path_streamed_compress(rbtarchive_codecs codecs)
{
    rbtarchive_writer writer = NULL;
    RBT_RESULT result = make_writer(codecs, &writer);
    if (result != RBT_RESULT_OK)
        return result;
    RBTARCHIVE_ENTRY_DESC entry = entry_desc("streamed.txt");
    result = rbtarchive_writer_begin_entry(writer, &entry, NULL);
    const uint64_t piece = DATA_SIZE / 3;
    for (uint64_t at = 0; result == RBT_RESULT_OK && at < DATA_SIZE; at += piece)
    {
        uint64_t size = DATA_SIZE - at < piece ? DATA_SIZE - at : piece;
        result = rbtarchive_writer_write_entry_data(writer, data + at, size, NULL);
    }
    if (result == RBT_RESULT_OK)
        result = rbtarchive_writer_end_entry(writer, NULL);
    if (result == RBT_RESULT_OK)
        result = rbtarchive_writer_finish(writer, NULL);
    rbtarchive_writer_destroy(writer);
    return result;
}

// Whole, decompressing (libdeflate): both entries of the archive read whole.
static RBT_RESULT path_whole_decompress(rbtarchive_codecs codecs)
{
    rbtarchive_reader reader = NULL;
    RBT_RESULT result = open_reader(codecs, &reader);
    if (result != RBT_RESULT_OK)
        return result;
    for (uint32_t i = 0; result == RBT_RESULT_OK && i < 2; i++)
    {
        result = rbtarchive_reader_read_entry(reader, i, out, DATA_SIZE, NULL);
        if (result == RBT_RESULT_OK && memcmp(out, data, DATA_SIZE) != 0)
            result = RBT_RESULT_CORRUPT_DATA;
    }
    rbtarchive_reader_destroy(reader);
    return result;
}

// Streamed, decompressing (zlib-ng): both entries read in pieces.
static RBT_RESULT path_streamed_decompress(rbtarchive_codecs codecs)
{
    rbtarchive_reader reader = NULL;
    RBT_RESULT result = open_reader(codecs, &reader);
    if (result != RBT_RESULT_OK)
        return result;
    for (uint32_t i = 0; result == RBT_RESULT_OK && i < 2; i++)
    {
        rbtarchive_entry_reader entry = NULL;
        result = rbtarchive_reader_open_entry(reader, i, &entry, NULL);
        uint64_t total = 0;
        while (result == RBT_RESULT_OK)
        {
            uint64_t got = 0;
            result = rbtarchive_entry_reader_read(entry, out + total, 50000 < DATA_SIZE - total ? 50000 : DATA_SIZE - total,
                                                  &got, NULL);
            total += got;
            if (got == 0)
                break;
        }
        if (result == RBT_RESULT_OK && (total != DATA_SIZE || memcmp(out, data, DATA_SIZE) != 0))
            result = RBT_RESULT_CORRUPT_DATA;
        rbtarchive_entry_reader_destroy(entry);
    }
    rbtarchive_reader_destroy(reader);
    return result;
}

// ---- Running them -----------------------------------------------------------------

typedef RBT_RESULT (*path_fn)(rbtarchive_codecs codecs);

// The path once whole, then failing each of its allocations in turn.
static void run_path(const char *name, path_fn path, rbtarchive_codecs codecs)
{
    alloc_fail_begin(0);
    RBT_RESULT result = path(codecs);
    alloc_fail_counts counts = alloc_fail_end();
    CHECK(result == RBT_RESULT_OK);
    CHECK(counts.live == 0);
    const uint64_t total = counts.made;
    CHECK(total > 0);
    uint64_t out_of_memory = 0;
    for (uint64_t n = 1; n <= total; n++)
    {
        alloc_fail_begin(n);
        result = path(codecs);
        counts = alloc_fail_end();
        CHECK(counts.failed);
        CHECK(counts.live == 0);
        if (result == RBT_RESULT_OUT_OF_MEMORY)
            out_of_memory++;
        else
            printf("FAIL %s: allocation %llu of %llu failed, and the path gave result %d\n", name,
                   (unsigned long long)n, (unsigned long long)total, (int)result);
        CHECK(result == RBT_RESULT_OUT_OF_MEMORY);
    }
    printf("  %s: %llu allocations of ours, each failed in turn: %llu gave RBT_RESULT_OUT_OF_MEMORY\n", name,
           (unsigned long long)total, (unsigned long long)out_of_memory);
}

int main(void)
{
    if (!alloc_fail_available())
    {
        printf("rbtarchive_allocfail_tests: skipped, no allocation can be failed in this build (a sanitizer's heap)\n");
        return 0;
    }
    if (!alloc_fail_prepare((const void *)(uintptr_t)&run_path))
    {
        printf("FAIL: the test executable's image wasn't found\n");
        return 1;
    }
    make_data();
    rbtarchive_codecs codecs = NULL;
    CHECK(rbtarchive_codecs_create(&codecs) == RBT_RESULT_OK);
    CHECK(rbtarchive_codecs_add_deflate(codecs) == RBT_RESULT_OK);

    // The archive the decompressing paths read: one entry written whole, one
    // streamed.
    rbtarchive_writer writer = NULL;
    CHECK(make_writer(codecs, &writer) == RBT_RESULT_OK);
    RBTARCHIVE_ENTRY_DESC whole = entry_desc("whole.txt");
    CHECK(rbtarchive_writer_add_entry(writer, &whole, data, DATA_SIZE, NULL) == RBT_RESULT_OK);
    RBTARCHIVE_ENTRY_DESC streamed = entry_desc("streamed.txt");
    CHECK(rbtarchive_writer_begin_entry(writer, &streamed, NULL) == RBT_RESULT_OK);
    CHECK(rbtarchive_writer_write_entry_data(writer, data, DATA_SIZE, NULL) == RBT_RESULT_OK);
    CHECK(rbtarchive_writer_end_entry(writer, NULL) == RBT_RESULT_OK);
    CHECK(rbtarchive_writer_finish(writer, NULL) == RBT_RESULT_OK);
    rbtarchive_writer_destroy(writer);
    memcpy(archive.bytes, written.bytes, (size_t)written.size);
    archive.size = written.size;

    run_path("whole, compressing", path_whole_compress, codecs);
    run_path("streamed, compressing", path_streamed_compress, codecs);
    run_path("whole, decompressing", path_whole_decompress, codecs);
    run_path("streamed, decompressing", path_streamed_decompress, codecs);

    rbtarchive_codecs_destroy(codecs);
    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All rbtarchive allocation-failure tests passed\n");
    return 0;
}
