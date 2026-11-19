#ifndef RBTREND_COPIES_INTERNAL_HPP
#define RBTREND_COPIES_INTERNAL_HPP

/* Internal header - never included outside rabbittus/src/render/, but for
 * tests/rbtrendcopies_tests.cpp.
 *
 * The copies a staged MANY buffer store has in flight (card A62;
 * render/rbtrendbuffers.h, "Writing while the buffer is in use"): the
 * ranges of its staging buffer that submissions not known to have finished
 * copy to the device, each with that submission's serial. A write by its
 * bytes (rbtrend_buffer_write_after()) is refused over one of them, since
 * the copy reads those staging bytes until its submission finishes.
 *
 * Appended in serial order at each submission that copies the store's
 * waiting uploads, so the list is sorted by serial and is emptied from the
 * front as the fence passes. The room for an append is reserved by the
 * write that makes the upload, so the submission never allocates for it.
 * Plain arithmetic, with no device in it, so it is tested alone; it
 * includes nothing of Rend or Carrottus GPU for that reason. */

#include "../rbtplaindata.hpp"

#include <eqstd.hpp>

#include <cstddef>
#include <cstdint>

/* Bytes [offset, offset + size) of a store's staging buffer, copied to the
 * device by submission [serial]. */
struct rbtrend_copy_in_flight {
	uint64_t offset;
	uint64_t size;
	uint64_t serial;
};
RBT_PLAIN_DATA(rbtrend_copy_in_flight);

/* Room for [more] appends past what [copies] holds. False, with nothing
 * changed, if there is no memory for it. */
inline bool rbtrend_copies_reserve(eq::array<rbtrend_copy_in_flight> &copies, size_t more) {
	if (copies.capacity - copies.size() >= more)
		return true;
	return copies.reserve(copies.size() + more) == EQ_SUCCESS;
}

/* Appends a range copied by [serial], at or after every serial the list
 * holds. Never allocates: false, with nothing appended, if no room was
 * reserved for it. */
inline bool rbtrend_copies_append(eq::array<rbtrend_copy_in_flight> &copies, uint64_t offset, uint64_t size, uint64_t serial) {
	if (copies.size() == copies.capacity)
		return false;
	return copies.push({offset, size, serial}) == EQ_SUCCESS;
}

/* Drops every range whose submission has finished: [completed] is the
 * latest serial finished, with every one before it, so those are a run at
 * the front. */
inline void rbtrend_copies_prune(eq::array<rbtrend_copy_in_flight> &copies, uint64_t completed) {
	size_t done = 0;
	while (done < copies.size() && copies[done].serial <= completed)
		++done;
	if (done > 0)
		(void)copies.remove(0, done);
}

/* The index of the first range that shares a byte with [offset, offset +
 * size), or SIZE_MAX. Touching is not overlapping. [size] is not 0; no sum
 * is formed that could wrap. */
inline size_t rbtrend_copies_find_overlap(const eq::array<rbtrend_copy_in_flight> &copies, uint64_t offset, uint64_t size) {
	for (size_t i = 0; i < copies.size(); ++i) {
		const rbtrend_copy_in_flight &copy = copies[i];
		// The range ends at or before the write begins.
		if (copy.offset <= offset && offset - copy.offset >= copy.size)
			continue;
		// The range begins at or after the write ends.
		if (copy.offset > offset && copy.offset - offset >= size)
			continue;
		return i;
	}
	return SIZE_MAX;
}

#endif // RBTREND_COPIES_INTERNAL_HPP
