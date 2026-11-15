/*
 * rbtarchive_allocfail_interpose.c - rbtarchive_allocfail.h's Apple half, as
 * a test-only dylib: its hooks in the __DATA,__interpose section, which dyld
 * applies to every image loaded at launch, and the region's state and calls,
 * which the test executable reaches through it. Built and linked on Apple
 * only (CMakeLists.txt); never shipped. Under a sanitizer it holds the
 * header's "not available" calls and hooks nothing.
 */

#define ALLOC_FAIL_DYLIB 1
#include "rbtarchive_allocfail.h"
