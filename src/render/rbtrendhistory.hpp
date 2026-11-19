#ifndef RBTREND_HISTORY_INTERNAL_HPP
#define RBTREND_HISTORY_INTERNAL_HPP

/*
 * The history records Rend's volumetric objects keep, a record a view,
 * made with the object (rbtrend_fog_draft.h section 5 and
 * rbtrend_clouds_draft.h section 4: card A59's fog grid and card A57's
 * clouds share this module, each object its own records): which record a
 * scene's command takes, when its history starts again, the fixed jitter
 * sequence its passes step through a frame at a time, the share a history
 * keeps of a frame (the fog's by its frames, the clouds' by a texel's
 * visits), the clouds' order through each march texel's block (card A70),
 * and a place carried from this frame's scene space into last frame's clip
 * space through the space's shift. Plain records and plain functions with no device in them,
 * so they are tested alone (tests/rbtrendhistory_tests.cpp); the textures a
 * record names are its object's.
 *
 * Which record a scene takes: the record that drew it before (its own even
 * while a list holds it: a scene draws once a frame, and its command
 * refuses a second); else the first empty one; else the one used least
 * recently whose GPU work no compiled, unsubmitted list holds; else none
 * (the command is refused with RBT_RESULT_LIMIT, naming its desc's
 * max_views). A record changing hands, or never yet written, starts its
 * history again.
 */

#include "../rbtplaindata.hpp"

#include <cstdint>

/* How many jitters the sequence holds before it repeats: a frame's place
 * in depth (or along a march) is jitter(frame % 16). */
#define RBTREND_HISTORY_JITTERS 16u

/* A view's record: the scene it draws (by its handle, compared only, never
 * dereferenced here), when it was last used (the object's own clock), last
 * frame's view-projection (column-major), and whether its history holds a
 * frame. 88 bytes on a 64-bit build. */
struct rbtrend_history_record {
	const void *scene;
	uint64_t used;
	float view_projection[16];
	uint32_t written;
	uint32_t padding;
};
RBT_PLAIN_DATA(rbtrend_history_record);

/* Whether record [record] is held: bound by the compile in progress or by a
 * list compiled and not yet submitted. [user] is the caller's. */
typedef bool (*rbtrend_history_held)(const void *user, uint32_t record);

/* The record of [records] (of [count]) [scene] draws with this frame, its
 * `used` set to [clock], and through [out_reset] whether its history starts
 * again; UINT32_MAX (nothing changed) when no record can be had. */
inline uint32_t rbtrend_history_take(rbtrend_history_record *records, uint32_t count, const void *scene, uint64_t clock, rbtrend_history_held held,
                                     const void *user, bool *out_reset) {
	*out_reset = false;
	for (uint32_t i = 0; i < count; ++i) {
		if (records[i].scene == scene) {
			records[i].used = clock;
			*out_reset      = records[i].written == 0;
			return i;
		}
	}
	uint32_t chosen = UINT32_MAX;
	for (uint32_t i = 0; i < count; ++i) {
		if (!records[i].scene) {
			chosen = i;
			break;
		}
	}
	if (chosen == UINT32_MAX) {
		for (uint32_t i = 0; i < count; ++i) {
			if (chosen != UINT32_MAX && records[i].used >= records[chosen].used)
				continue;
			if (held(user, i))
				continue;
			chosen = i;
		}
	}
	if (chosen == UINT32_MAX)
		return UINT32_MAX;
	records[chosen].scene   = scene;
	records[chosen].used    = clock;
	records[chosen].written = 0;
	*out_reset              = true;
	return chosen;
}

/* A frame has been written into [record]'s history. */
inline void rbtrend_history_mark_written(rbtrend_history_record *record) {
	record->written = 1;
}

/* Frame [frame]'s jitter, in (0, 1): van der Corput's sequence in base 2
 * over RBTREND_HISTORY_JITTERS, each at the middle of its step, so the
 * sequence's mean is a half and none is 0 or 1. */
inline float rbtrend_history_jitter(uint32_t frame) {
	uint32_t i        = frame % RBTREND_HISTORY_JITTERS;
	uint32_t reversed = 0;
	for (uint32_t bit = 1, out = RBTREND_HISTORY_JITTERS >> 1; bit < RBTREND_HISTORY_JITTERS; bit <<= 1, out >>= 1)
		if (i & bit)
			reversed |= out;
	return ((float)reversed + 0.5f) / (float)RBTREND_HISTORY_JITTERS;
}

/* The history's share of a frame's value where [held] frames (or visits)
 * are already in it: an even share while the history holds fewer than its
 * weight [most] averages (n held: n / (n + 1)), then [most]. The fog's
 * integrate takes it of its frames and its caller's weight; the clouds'
 * reconstruct of a texel's visits and one less their weight's floor. */
inline float rbtrend_history_share(uint32_t held, float most) {
	const float even = (float)held / (float)(held + 1);
	return most < even ? most : even;
}

/* The least weight the clouds' marched texel takes its march by: once
 * settled, an exponential average of (2 - w) / w = 7 marches. */
#define RBTREND_HISTORY_CLOUDS_WEIGHT_FLOOR 0.25f

/* How many standard deviations of the march's 3 x 3 neighbourhood round
 * its mean the clouds' reprojected history is clipped to. */
#define RBTREND_HISTORY_CLOUDS_CLIP_GAMMA 1.5f

/* The clouds' march (rbtrend_clouds_draft.h section 4, card A70): each
 * march texel covers 4 x 4 target texels and 2 x 2 history texels. In a
 * record's frame [frame] since its reset, every march texel's ray goes
 * through the centre of one target texel of its 4 x 4 block, by the 4 x 4
 * ordered-dither order over sixteen frames, its place given in target
 * texels from the block's corner. Its history texel runs (0, 0), (1, 1),
 * (1, 0), (0, 1) in every four frames, and each history texel is marched
 * through each of its own four target texels once in its four visits. */
inline void rbtrend_history_clouds_place(uint32_t frame, float out_place[2]) {
	static const uint8_t order[16][2] = {{0, 0}, {2, 2}, {2, 0}, {0, 2}, {1, 1}, {3, 3}, {3, 1}, {1, 3},
	                                     {1, 0}, {3, 2}, {3, 0}, {1, 2}, {0, 1}, {2, 3}, {2, 1}, {0, 3}};
	const uint8_t *at = order[frame % 16u];
	out_place[0]      = (float)at[0] + 0.5f;
	out_place[1]      = (float)at[1] + 0.5f;
}

/* The history texel of each march texel's 2 x 2 marched in [frame]: bit 0
 * its x, bit 1 its y. */
inline uint32_t rbtrend_history_clouds_texel(uint32_t frame) {
	float place[2];
	rbtrend_history_clouds_place(frame, place);
	return (uint32_t)(place[0] * 0.5f) + 2u * (uint32_t)(place[1] * 0.5f);
}

/* The march's step offset in [frame], in (0, 1) of a step, before the
 * march's per-texel rotation: the jitter of the visit, frame / 4, so each
 * history texel takes all of the sequence's offsets in its first
 * RBTREND_HISTORY_JITTERS visits. */
inline float rbtrend_history_clouds_step(uint32_t frame) {
	return rbtrend_history_jitter(frame / 4u);
}

/* The weight the texel marched in [frame] takes its march by, the rest its
 * history's: 1 / (v + 1) on its visit v = frame / 4, never below
 * RBTREND_HISTORY_CLOUDS_WEIGHT_FLOOR. */
inline float rbtrend_history_clouds_weight(uint32_t frame) {
	return 1.0f - rbtrend_history_share(frame / 4u, 1.0f - RBTREND_HISTORY_CLOUDS_WEIGHT_FLOOR);
}

/* [place] (this frame's scene space) carried into last frame's clip space
 * by its [view_projection] (column-major) after the space's [shift] (this
 * frame's origin in last frame's space), its normalized device coordinates
 * into [out_ndc]; false where it lies on or behind last frame's eye (w at
 * most 0), where there is no history. The shaders do the same. */
inline bool rbtrend_history_reproject(const float view_projection[16], const float shift[3], const float place[3], float out_ndc[3]) {
	const float p[4] = {place[0] + shift[0], place[1] + shift[1], place[2] + shift[2], 1.0f};
	float clip[4];
	for (int r = 0; r < 4; ++r)
		clip[r] = view_projection[0 * 4 + r] * p[0] + view_projection[1 * 4 + r] * p[1] + view_projection[2 * 4 + r] * p[2] +
		          view_projection[3 * 4 + r] * p[3];
	if (!(clip[3] > 0.0f))
		return false;
	for (int a = 0; a < 3; ++a)
		out_ndc[a] = clip[a] / clip[3];
	return true;
}

#endif // RBTREND_HISTORY_INTERNAL_HPP
