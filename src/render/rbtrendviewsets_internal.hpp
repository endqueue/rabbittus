#ifndef RBTREND_VIEWSETS_INTERNAL_HPP
#define RBTREND_VIEWSETS_INTERNAL_HPP

/*
 * A lit scene's view-set records (rbtrendlit.h,
 * RBTREND_LIT_SCENE_DESC::max_view_sets; card A67): the binding sets Rend
 * makes over the views LIT_SSR, LIT_TRACE_REFLECTIONS, LIT_REFLECT and
 * LIT_ATMOSPHERE_SHELLS name, kept for the next compile that names the
 * same ones. Plain records and the choice of which one a new set goes
 * into, with no device in it, so the choice is tested alone
 * (tests/rbtrendviewsets_tests.cpp). The hand-out over them is
 * rbtrendcommands.cpp's rbtrend_view_set_get().
 */

#include "../rbtplaindata.hpp"

#include <cstdint>

typedef struct rbtrend_binding_set_t *rbtrend_binding_set;

/* The most views one of the four commands names. */
#define RBTREND_VIEW_SET_VIEWS 6u

/* A record: the set, the layout it is of, and the views and their serials
 * it holds (a view destroyed and another made at its address has another
 * serial); `used` the scene's hand-out counter when it was last handed
 * out. 120 bytes on a 64-bit build, as rbtrendlit.h states. */
struct rbtrend_view_set {
	rbtrend_binding_set set;
	const void *layout;
	const void *views[RBTREND_VIEW_SET_VIEWS];
	uint64_t serials[RBTREND_VIEW_SET_VIEWS];
	uint64_t used;
};
RBT_PLAIN_DATA(rbtrend_view_set);

/* Whether [set] is held: bound by the compile in progress or by a list
 * compiled and not yet submitted. [user] is the caller's. */
typedef bool (*rbtrend_view_set_held)(const void *user, rbtrend_binding_set set);

/* The record of [records] (of [count]) a new set goes into: the first
 * empty one, else the one handed out least recently (the lowest `used`,
 * the first of equals) whose set [held] says is not held; UINT32_MAX when
 * every record holds a held set, or there are none. */
inline uint32_t rbtrend_view_set_choose(const rbtrend_view_set *records, uint32_t count, rbtrend_view_set_held held, const void *user) {
	for (uint32_t i = 0; i < count; ++i)
		if (!records[i].set)
			return i;
	uint32_t chosen = UINT32_MAX;
	for (uint32_t i = 0; i < count; ++i) {
		if (chosen != UINT32_MAX && records[i].used >= records[chosen].used)
			continue;
		if (held(user, records[i].set))
			continue;
		chosen = i;
	}
	return chosen;
}

#endif // RBTREND_VIEWSETS_INTERNAL_HPP
