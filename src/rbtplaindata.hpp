#ifndef RBTPLAINDATA_HPP
#define RBTPLAINDATA_HPP

// Data is plain structs, checked by the compiler. The element of an array
// that holds per-command, per-draw or per-element data is trivially
// copyable and standard layout: the array is bytes, copied, moved and
// walked as bytes, with no constructor, destructor or hidden pointer per
// element. RBT_PLAIN_DATA(T) goes beside each such array's element type;
// the workspace's ./check.sh plain-data finds the arrays and fails on one
// whose element has no assert and no check.baseline entry.
//
// RBT_HOT_RECORD(T, bytes) is a size budget on a record a per-frame loop
// reads, in cache lines or a fraction of one, so a field added to it is a
// decision made by changing the budget, not a side effect.
//
// RBT_ROLE_GAP keeps two roles' data apart: a reader's fields from a
// writer's, two threads' counters. A type that separates roles is aligned
// to it and starts each role's first field on it, and RBT_ROLE_START(T,
// field) asserts both beside the type.

#include <cstddef>
#include <type_traits>

// What a record costs to read, for RBT_HOT_RECORD's budgets: 64 bytes, the
// smallest line of the targets, so a budget measures density the same way
// everywhere.
#define RBT_CACHE_LINE 64

// The distance between two roles' data: 128 bytes. Apple silicon's line is
// 128 (`sysctl hw.cachelinesize` on the M5 Pro), and x86's adjacent-line
// prefetcher fetches 64-byte lines in 128-byte-aligned pairs, so two roles
// 64 bytes apart still share what one fetch brings. Not
// std::hardware_destructive_interference_size: Apple clang reports 256 on
// arm64, and the value differs by compiler.
#define RBT_ROLE_GAP 128

#define RBT_PLAIN_DATA(T)                                                        \
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>, \
                  #T " is an array's element: data is plain structs (trivially copyable, standard layout)")

#define RBT_HOT_RECORD(T, BYTES)                                                  \
    static_assert(sizeof(T) <= (BYTES), #T " is a hot record over its size budget of " #BYTES \
                                        " bytes: growing it is a decision, made by changing the budget")

// FIELD starts a role of T on a block of its own: T is standard layout (so
// offsetof is defined), aligned to RBT_ROLE_GAP, and FIELD's offset is a
// multiple of it. Not a difference of two offsets: fields 128 bytes apart in
// a type aligned to 64 can still share a 128-byte block. The memory must
// keep the alignment too: whatever allocates T is checked for it.
#define RBT_ROLE_START(T, FIELD)                                                                 \
    static_assert(std::is_standard_layout_v<T>, #T " separates roles: offsetof needs it standard layout"); \
    static_assert(alignof(T) % RBT_ROLE_GAP == 0, #T " separates roles: aligned to RBT_ROLE_GAP"); \
    static_assert(offsetof(T, FIELD) % RBT_ROLE_GAP == 0, #T "::" #FIELD " starts a role: on an RBT_ROLE_GAP boundary")

#endif // RBTPLAINDATA_HPP
