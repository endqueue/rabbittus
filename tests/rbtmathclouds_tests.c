/*
 * Rabbittus Math's cloud layers (rbtmathclouds.h; card A57's Math half,
 * rbtmath_clouds_draft.h, section 7, tests 5 to 8): each step of the density
 * worked by hand over constant volumes and maps, the density against a
 * reference of the header's Definition written out again here (random
 * volumes and maps, both bounds, both warps, both addresses: the same bits),
 * a shell against a slab, the cube's face table, and every argument check.
 * (Tests 1 to 4, the bake and the read, are rbtmathnoise_volume_tests.c's.)
 *
 * Written in C, as the other tests of a C header are; built with
 * contraction off, as the library is, so the reference's steps round as the
 * Definition says.
 */

#include "math/rbtmathclouds.h"
#include "math/rbtmathfunctions.h"
#include "math/rbtmathnoise.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                              \
	do {                                                                              \
		if (!(condition)) {                                                           \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			++failures;                                                               \
		}                                                                             \
	} while (0)

static int pending(RBT_RESULT r, const char *what) {
	if (r == RBT_RESULT_NOT_IMPLEMENTED) {
		fprintf(stderr, "%s: NOT IMPLEMENTED\n", what);
		++failures;
		return 1;
	}
	return 0;
}

static uint32_t lcg = 57u;
static uint32_t next_u32(void) {
	lcg = lcg * 1664525u + 1013904223u;
	return lcg;
}
static float uniform(float lo, float hi) {
	return lo + (hi - lo) * ((float)(next_u32() >> 8) / 16777216.0f);
}

/* ---- Fixtures -------------------------------------------------------------- */

enum { VOL = 8, MAP = 4 };

static uint8_t base_vol[VOL * VOL * VOL * 4];
static uint8_t detail_vol[VOL * VOL * VOL * 4];
static uint8_t map6[6 * MAP * MAP * 4];

static void fill_constant(uint8_t *bytes, size_t texels, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	for (size_t i = 0; i < texels; i++) {
		bytes[i * 4 + 0] = r;
		bytes[i * 4 + 1] = g;
		bytes[i * 4 + 2] = b;
		bytes[i * 4 + 3] = a;
	}
}

static void fill_random(uint8_t *bytes, size_t size) {
	for (size_t i = 0; i < size; i++)
		bytes[i] = (uint8_t)(next_u32() >> 24);
}

static RBTMATH_CLOUD_SHAPE full_shape(void) {
	RBTMATH_CLOUD_SHAPE s;
	memset(&s, 0, sizeof(s));
	s.type_count     = 1;
	s.profiles[0]    = 0.0f;
	s.profiles[1]    = 0.25f;
	s.profiles[2]    = 0.75f;
	s.profiles[3]    = 1.0f;
	s.base_tile      = 10.0f;
	s.detail_tile    = 3.0f;
	s.erosion        = 0.0f;
	s.billow_height  = 0.5f;
	s.density_scale  = 1.0f;
	s.coverage_scale = 1.0f;
	s.coverage_bias  = 0.0f;
	return s;
}

static RBTMATH_CLOUD_BOUNDS slab_bounds(float bottom, float top) {
	RBTMATH_CLOUD_BOUNDS b;
	memset(&b, 0, sizeof(b));
	b.kind        = RBTMATH_CLOUD_BOUNDS_KIND_SLAB;
	b.bottom      = bottom;
	b.top         = top;
	b.up[1]       = 1.0f;
	b.map_axis[0] = 1.0f;
	b.map_extent  = 100.0f;
	b.map_address = RBTMATH_CLOUD_MAP_ADDRESS_REPEAT;
	return b;
}

static RBTMATH_CLOUD_BOUNDS shell_bounds(float bottom, float top, RBTMATH_CLOUD_CUBE_WARP warp) {
	RBTMATH_CLOUD_BOUNDS b;
	memset(&b, 0, sizeof(b));
	b.kind      = RBTMATH_CLOUD_BOUNDS_KIND_SHELL;
	b.bottom    = bottom;
	b.top       = top;
	b.cube_warp = warp;
	return b;
}

static RBTMATH_CLOUD_TEXELS texels_of(uint32_t map_size) {
	RBTMATH_CLOUD_TEXELS t;
	memset(&t, 0, sizeof(t));
	t.base          = base_vol;
	t.base_size     = VOL;
	t.detail        = detail_vol;
	t.detail_size   = VOL;
	t.coverage      = map6;
	t.coverage_size = map_size;
	return t;
}

static RBTMATH_CLOUD_OFFSETS zero_offsets(void) {
	RBTMATH_CLOUD_OFFSETS o;
	memset(&o, 0, sizeof(o));
	return o;
}

static float density_at(const RBTMATH_CLOUD_SHAPE *s, const RBTMATH_CLOUD_BOUNDS *b, const RBTMATH_CLOUD_TEXELS *t,
                        const RBTMATH_CLOUD_OFFSETS *o, float x, float y, float z) {
	const float p[3] = {x, y, z};
	float out        = -1.0f;
	RBTMATH_ERROR error;
	const RBT_RESULT r = rbtmath_cloud_compute_density(s, b, t, o, p, 1, &out, &error);
	if (r != RBT_RESULT_OK) {
		fprintf(stderr, "density refused (%d): %s\n", (int)r, error.message);
		++failures;
	}
	return out;
}

/* ---- The reference: the Definition again, step by step ---------------------- */

static float sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

static void ref_sample(const uint8_t *v, uint32_t n, const float q[3], float out[4]) {
	CHECK(rbtmath_noise_volume_sample(v, n, q, 1, out, NULL) == RBT_RESULT_OK);
}

static int64_t wrap_index(int64_t i, int64_t m) {
	int64_t r = i % m;
	return r < 0 ? r + m : r;
}

static int64_t clamp_index(int64_t i, int64_t m) { return i < 0 ? 0 : (i > m - 1 ? m - 1 : i); }

static void ref_map(const uint8_t *face, uint32_t m, float s, float t, int repeat, float out[4]) {
	const float u = (s * (float)m) - 0.5f, v = (t * (float)m) - 0.5f;
	const float fu = floorf(u), fv = floorf(v);
	const int64_t iu = (int64_t)fu, iv = (int64_t)fv;
	const float tu = u - (float)iu, tv = v - (float)iv;
	int64_t x0, x1, y0, y1;
	if (repeat) {
		x0 = wrap_index(iu, m); x1 = wrap_index(iu + 1, m);
		y0 = wrap_index(iv, m); y1 = wrap_index(iv + 1, m);
	} else {
		x0 = clamp_index(iu, m); x1 = clamp_index(iu + 1, m);
		y0 = clamp_index(iv, m); y1 = clamp_index(iv + 1, m);
	}
	for (int c = 0; c < 4; c++) {
		const float a = (float)face[(y0 * m + x0) * 4 + c] / 255.0f;
		const float b = (float)face[(y0 * m + x1) * 4 + c] / 255.0f;
		const float d = (float)face[(y1 * m + x0) * 4 + c] / 255.0f;
		const float e = (float)face[(y1 * m + x1) * 4 + c] / 255.0f;
		const float r0 = a + (tu * (b - a));
		const float r1 = d + (tu * (e - d));
		out[c] = r0 + (tv * (r1 - r0));
	}
}

static float warp_coordinate(float g, int equi) {
	if (!equi)
		return g;
	double x = (double)g, y = 0.0;
	CHECK(rbtmath_compute_atan(&x, 1, &y, NULL) == RBT_RESULT_OK);
	return (float)(y * 0x1.45f306dc9c883p+0);
}

static float profile(const float *p4, float h) {
	const float a = p4[0], b = p4[1], c = p4[2], d = p4[3];
	const float up = sat((h - a) / (b - a));
	const float dn = sat((d - h) / (d - c));
	return up < dn ? up : dn;
}

static float ref_density(const RBTMATH_CLOUD_SHAPE *s, const RBTMATH_CLOUD_BOUNDS *b, const RBTMATH_CLOUD_TEXELS *t,
                         const RBTMATH_CLOUD_OFFSETS *o, const float p[3]) {
	float y, r = 0.0f;
	if (b->kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL) {
		r = sqrtf(((p[0] * p[0]) + (p[1] * p[1])) + (p[2] * p[2]));
		y = r - b->bottom;
	} else {
		y = (((p[0] * b->up[0]) + (p[1] * b->up[1])) + (p[2] * b->up[2])) - b->bottom;
	}
	const float h = y / (b->top - b->bottom);
	if (h < 0.0f || h > 1.0f)
		return 0.0f;
	float cov[4];
	const uint32_t m = t->coverage_size;
	if (b->kind == RBTMATH_CLOUD_BOUNDS_KIND_SHELL) {
		if (r == 0.0f)
			return 0.0f;
		const float ax = fabsf(p[0]), ay = fabsf(p[1]), az = fabsf(p[2]);
		int face;
		float sc, tc, ma;
		if (ax >= ay && ax >= az) {
			face = p[0] >= 0.0f ? 0 : 1;
			ma   = p[0];
			sc   = p[0] >= 0.0f ? -p[2] : p[2];
			tc   = -p[1];
		} else if (ay >= az) {
			face = p[1] >= 0.0f ? 2 : 3;
			ma   = p[1];
			sc   = p[0];
			tc   = p[1] >= 0.0f ? p[2] : -p[2];
		} else {
			face = p[2] >= 0.0f ? 4 : 5;
			ma   = p[2];
			sc   = p[2] >= 0.0f ? p[0] : -p[0];
			tc   = -p[1];
		}
		const float am = fabsf(ma);
		const int equi = b->cube_warp == RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR;
		const float a  = warp_coordinate(sc / am, equi);
		const float bb = warp_coordinate(tc / am, equi);
		const float ss = (a + 1.0f) * 0.5f, tt = (bb + 1.0f) * 0.5f;
		ref_map(t->coverage + (size_t)face * m * m * 4, m, ss, tt, 0, cov);
	} else {
		const float *u = b->up, *ma = b->map_axis;
		const float v[3] = {(u[1] * ma[2]) - (u[2] * ma[1]), (u[2] * ma[0]) - (u[0] * ma[2]), (u[0] * ma[1]) - (u[1] * ma[0])};
		const float dm = ((p[0] * ma[0]) + (p[1] * ma[1])) + (p[2] * ma[2]);
		const float dv = ((p[0] * v[0]) + (p[1] * v[1])) + (p[2] * v[2]);
		const float ss = (dm / b->map_extent) + o->coverage[0];
		const float tt = (dv / b->map_extent) + o->coverage[1];
		ref_map(t->coverage, m, ss, tt, b->map_address == RBTMATH_CLOUD_MAP_ADDRESS_REPEAT, cov);
	}
	const float c = sat((cov[0] * s->coverage_scale) + s->coverage_bias);
	const float g = cov[1], k = cov[2];
	float P;
	if (s->type_count == 1) {
		P = profile(s->profiles, h);
	} else {
		const float x = g * (float)(s->type_count - 1);
		float fi      = floorf(x);
		if (fi > (float)(s->type_count - 2))
			fi = (float)(s->type_count - 2);
		const int i   = (int)fi;
		const float f = x - fi;
		const float p0 = profile(s->profiles + 4 * i, h);
		const float p1 = profile(s->profiles + 4 * (i + 1), h);
		P = p0 + (f * (p1 - p0));
	}
	float q[3], v4[4];
	q[0] = (p[0] / s->base_tile) + o->base[0];
	q[1] = (p[1] / s->base_tile) + o->base[1];
	q[2] = (p[2] / s->base_tile) + o->base[2];
	ref_sample(t->base, t->base_size, q, v4);
	const float l      = ((v4[1] * 0.625f) + (v4[2] * 0.25f)) + (v4[3] * 0.125f);
	const float e      = sat((v4[0] - (l - 1.0f)) / (1.0f - (l - 1.0f)));
	const float shaped = e * P;
	if (c == 0.0f)
		return 0.0f;
	const float d0 = sat((shaped - (1.0f - c)) / c) * c;
	if (d0 == 0.0f)
		return 0.0f;
	q[0] = (p[0] / s->detail_tile) + o->detail[0];
	q[1] = (p[1] / s->detail_tile) + o->detail[1];
	q[2] = (p[2] / s->detail_tile) + o->detail[2];
	ref_sample(t->detail, t->detail_size, q, v4);
	const float w  = ((v4[0] * 0.625f) + (v4[1] * 0.25f)) + (v4[2] * 0.125f);
	const float mm = w + (sat(h / s->billow_height) * ((1.0f - w) - w));
	const float e2 = mm * s->erosion;
	if (e2 >= 1.0f)
		return 0.0f;
	const float d1 = sat((d0 - e2) / (1.0f - e2));
	return (d1 * s->density_scale) * k;
}

/* ---- Test 5: each step alone, worked by hand -------------------------------- */

static void test_density_steps(void) {
	fill_constant(base_vol, VOL * VOL * VOL, 255, 0, 0, 0); /* e = 1 */
	fill_constant(detail_vol, VOL * VOL * VOL, 0, 0, 0, 0); /* w = 0 */
	fill_constant(map6, 6 * MAP * MAP, 255, 0, 255, 0);     /* c = 1, type 0, k = 1 */
	RBTMATH_CLOUD_SHAPE s     = full_shape();
	s.profiles[0]             = 0.1f;
	s.profiles[1]             = 0.3f;
	s.profiles[2]             = 0.6f;
	s.profiles[3]             = 0.9f;
	RBTMATH_CLOUD_BOUNDS b    = slab_bounds(100.0f, 200.0f);
	RBTMATH_CLOUD_TEXELS t    = texels_of(1);
	RBTMATH_CLOUD_OFFSETS o   = zero_offsets();
	{
		const float p[3] = {0.0f, 150.0f, 0.0f};
		float out        = -1.0f;
		if (pending(rbtmath_cloud_compute_density(&s, &b, &t, &o, p, 1, &out, NULL), "rbtmath_cloud_compute_density"))
			return;
	}
	/* A profile's four corners and between: density = P(h). */
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 110.0f, 0.0f) == 0.0f);  /* h at a */
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 130.0f, 0.0f) == 1.0f);  /* h at b */
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 160.0f, 0.0f) == 1.0f);  /* h at c */
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 190.0f, 0.0f) == 0.0f);  /* h at d */
	{
		const float h = 20.0f / 100.0f;
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 120.0f, 0.0f) == sat((h - 0.1f) / (0.3f - 0.1f)));
		const float h2 = 75.0f / 100.0f;
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 175.0f, 0.0f) == sat((0.9f - h2) / (0.9f - 0.6f)));
	}
	/* h outside: 0, below and above. */
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 99.0f, 0.0f) == 0.0f);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 201.0f, 0.0f) == 0.0f);
	/* density_scale and k. */
	s.density_scale = 3.0f;
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 150.0f, 0.0f) == 3.0f);
	fill_constant(map6, 1, 255, 0, 51, 0);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 150.0f, 0.0f) == (1.0f * 3.0f) * (51.0f / 255.0f));
	s.density_scale = 1.0f;
	fill_constant(map6, 1, 255, 0, 255, 0);
	/* c 0: 0, by the map and by the scale. */
	s.coverage_scale = 0.0f;
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 150.0f, 0.0f) == 0.0f);
	s.coverage_bias = 0.25f; /* a map-free layer: c = 0.25 everywhere */
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 150.0f, 0.0f) == 0.25f);
	s.coverage_scale = 1.0f;
	s.coverage_bias  = 0.0f;
	/* d0 0: coverage 0.5 over a shaped value below 1 - c. */
	fill_constant(map6, 1, 128, 0, 255, 0);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 115.0f, 0.0f) == 0.0f); /* P = 0.25 */
	{
		const float c = 128.0f / 255.0f;
		const float P = sat((0.25f - 0.1f) / (0.3f - 0.1f));
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 125.0f, 0.0f) == sat((P - (1.0f - c)) / c) * c);
	}
	fill_constant(map6, 1, 255, 0, 255, 0);
	/* The base's remap: G, B, A at 255 make l 1, e = R. */
	fill_constant(base_vol, VOL * VOL * VOL, 102, 255, 255, 255);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 150.0f, 0.0f) == sat(((102.0f / 255.0f) - 0.0f) / 1.0f));
	fill_constant(base_vol, VOL * VOL * VOL, 255, 0, 0, 0);
	/* e2 1: erosion 1, the detail all 255 (w = 1, m = 1 at h 0, 1 - h/b above). */
	fill_constant(detail_vol, VOL * VOL * VOL, 255, 255, 255, 0);
	s.erosion       = 1.0f;
	s.profiles[0]   = 0.0f;
	s.profiles[1]   = 0.0001f;
	s.profiles[2]   = 0.9999f;
	s.profiles[3]   = 1.0f;
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 100.0f, 0.0f) == 0.0f); /* m = 1 at h 0: e2 1 */
	{
		/* At h 0.25, billow 0.5: m = 1 + 0.5 * (0 - 1) = 0.5; e2 0.5; d1 = sat((1 - 0.5) / 0.5) = 1. */
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 125.0f, 0.0f) == 1.0f);
		/* Above the billow height m = 0: no erosion; d1 = d0 = 1. */
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 175.0f, 0.0f) == 1.0f);
		/* Coverage 0.8 at h 0.25: d0 = sat((1 - 0.2) / 0.8) * 0.8 = 0.8; d1 = sat((0.8 - 0.5) / 0.5). */
		fill_constant(map6, 1, 204, 0, 255, 0);
		const float c  = 204.0f / 255.0f;
		const float d0 = sat((1.0f - (1.0f - c)) / c) * c;
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 125.0f, 0.0f) == sat((d0 - 0.5f) / (1.0f - 0.5f)));
		fill_constant(map6, 1, 255, 0, 255, 0);
	}
	s.erosion = 0.0f;
	fill_constant(detail_vol, VOL * VOL * VOL, 0, 0, 0, 0);
	/* Types: two profiles blended by the map's G. */
	s.type_count  = 2;
	s.profiles[0] = 0.0f;
	s.profiles[1] = 0.1f;
	s.profiles[2] = 0.2f;
	s.profiles[3] = 0.3f; /* low */
	s.profiles[4] = 0.5f;
	s.profiles[5] = 0.6f;
	s.profiles[6] = 0.8f;
	s.profiles[7] = 0.9f; /* high */
	fill_constant(map6, 1, 255, 0, 255, 0);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 115.0f, 0.0f) == 1.0f);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 170.0f, 0.0f) == 0.0f);
	fill_constant(map6, 1, 255, 255, 255, 0);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 115.0f, 0.0f) == 0.0f);
	CHECK(density_at(&s, &b, &t, &o, 0.0f, 170.0f, 0.0f) == 1.0f);
	fill_constant(map6, 1, 255, 51, 255, 0);
	{
		const float f = 51.0f / 255.0f;
		CHECK(density_at(&s, &b, &t, &o, 0.0f, 115.0f, 0.0f) == 1.0f + (f * (0.0f - 1.0f)));
	}
}

/* ---- Against the reference: random volumes and maps, the same bits ----------- */

static void test_density_against_reference(void) {
	fill_random(base_vol, sizeof(base_vol));
	fill_random(detail_vol, sizeof(detail_vol));
	fill_random(map6, sizeof(map6));
	enum { N = 4096 };
	static float points[N * 3], got[N];
	for (int config = 0; config < 4; config++) {
		RBTMATH_CLOUD_SHAPE s = full_shape();
		s.type_count          = 3;
		const float prof[12]  = {0.0f, 0.1f, 0.3f, 0.5f, 0.1f, 0.3f, 0.6f, 0.9f, 0.4f, 0.5f, 0.7f, 1.0f};
		memcpy(s.profiles, prof, sizeof(prof));
		s.erosion        = 0.6f;
		s.billow_height  = 0.7f;
		s.density_scale  = 2.0f;
		s.coverage_scale = 1.3f;
		s.coverage_bias  = -0.1f;
		RBTMATH_CLOUD_BOUNDS b;
		RBTMATH_CLOUD_OFFSETS o = zero_offsets();
		o.base[0] = 0.3f; o.base[1] = -12.25f; o.base[2] = 7.5f;
		o.detail[0] = -0.75f; o.detail[1] = 3.0f; o.detail[2] = 0.125f;
		const int shell = config < 2;
		if (shell) {
			b = shell_bounds(60.0f, 75.0f, config == 0 ? RBTMATH_CLOUD_CUBE_WARP_PLAIN : RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR);
		} else {
			b = slab_bounds(10.0f, 30.0f);
			const float n = 1.0f / sqrtf(2.0f);
			b.up[0] = 0.0f; b.up[1] = n; b.up[2] = n;
			b.map_axis[0] = 1.0f;
			b.map_address = config == 2 ? RBTMATH_CLOUD_MAP_ADDRESS_REPEAT : RBTMATH_CLOUD_MAP_ADDRESS_CLAMP;
			o.coverage[0] = 0.4f;
			o.coverage[1] = -1.7f;
		}
		const RBTMATH_CLOUD_TEXELS t = texels_of(MAP);
		for (int i = 0; i < N; i++) {
			if (shell) {
				float d[3], len;
				do {
					d[0] = uniform(-1.0f, 1.0f); d[1] = uniform(-1.0f, 1.0f); d[2] = uniform(-1.0f, 1.0f);
					len  = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				} while (len < 0.1f);
				const float r = uniform(58.0f, 77.0f);
				for (int a = 0; a < 3; a++)
					points[i * 3 + a] = d[a] / len * r;
			} else {
				/* Across the plane, at heights from below the layer to above. */
				const float across = uniform(-300.0f, 300.0f), along = uniform(-300.0f, 300.0f), height = uniform(5.0f, 35.0f);
				const float n = 1.0f / sqrtf(2.0f);
				points[i * 3 + 0] = along;
				points[i * 3 + 1] = (height * n) + (across * n);
				points[i * 3 + 2] = (height * n) - (across * n);
			}
		}
		const RBT_RESULT r = rbtmath_cloud_compute_density(&s, &b, &t, &o, points, N, got, NULL);
		if (pending(r, "rbtmath_cloud_compute_density (reference)"))
			return;
		CHECK(r == RBT_RESULT_OK);
		int mismatches = 0, inside = 0;
		for (int i = 0; i < N; i++) {
			const float want = ref_density(&s, &b, &t, &o, points + i * 3);
			if (memcmp(&want, &got[i], sizeof(float)) != 0)
				mismatches++;
			if (got[i] > 0.0f)
				inside++;
		}
		if (mismatches)
			fprintf(stderr, "config %d: %d of %d densities differ from the reference\n", config, mismatches, N);
		CHECK(mismatches == 0);
		CHECK(inside > N / 20); /* the run reaches past the early outs */
	}
}

/* ---- Test 6: a shell and a slab of the same thickness ------------------------ */

static void test_density_bounds(void) {
	/* A constant map: the bounds change height only; the noise read at the
	 * same coordinate by offsets that put both points at one place in the
	 * tiling (a shell point straight up the y axis at radius R + y). */
	fill_random(base_vol, sizeof(base_vol));
	fill_random(detail_vol, sizeof(detail_vol));
	fill_constant(map6, 6 * MAP * MAP, 200, 80, 255, 0);
	RBTMATH_CLOUD_SHAPE s = full_shape();
	s.base_tile           = 8.0f;
	s.detail_tile         = 2.0f;
	s.erosion             = 0.4f;
	const float R         = 1024.0f;
	RBTMATH_CLOUD_BOUNDS shell = shell_bounds(R, R + 16.0f, RBTMATH_CLOUD_CUBE_WARP_PLAIN);
	RBTMATH_CLOUD_BOUNDS slab  = slab_bounds(0.0f, 16.0f);
	const RBTMATH_CLOUD_TEXELS t = texels_of(MAP);
	RBTMATH_CLOUD_OFFSETS o_slab  = zero_offsets();
	RBTMATH_CLOUD_OFFSETS o_shell = zero_offsets();
	/* R / tile is a whole number of tiles (128 and 512): the same place. */
	int checked = 0, differ = 0;
	for (int i = 0; i < 64; i++) {
		const float y  = 0.25f * (float)i;
		const float x  = 0.0f;
		const float ds = density_at(&s, &slab, &t, &o_slab, x, y, 0.0f);
		const float dh = density_at(&s, &shell, &t, &o_shell, x, R + y, 0.0f);
		if (fabsf(ds - dh) > 1e-5f)
			differ++;
		checked++;
	}
	CHECK(checked == 64);
	CHECK(differ == 0);
	/* A slab's REPEAT: the map at s + 1 is the map at s. */
	fill_random(map6, sizeof(map6));
	fill_constant(base_vol, VOL * VOL * VOL, 255, 0, 0, 0);
	fill_constant(detail_vol, VOL * VOL * VOL, 0, 0, 0, 0);
	RBTMATH_CLOUD_SHAPE flat = full_shape();
	for (int i = 0; i < 32; i++) {
		const float px = uniform(0.0f, 100.0f), pz = uniform(0.0f, 100.0f);
		const float a  = density_at(&flat, &slab, &t, &o_slab, px, 8.0f, pz);
		const float b2 = density_at(&flat, &slab, &t, &o_slab, px + 100.0f, 8.0f, pz);
		const float c2 = density_at(&flat, &slab, &t, &o_slab, px, 8.0f, pz - 100.0f);
		CHECK(fabsf(a - b2) <= 1e-5f);
		CHECK(fabsf(a - c2) <= 1e-5f);
	}
	/* CLAMP: past the edge, the edge texel's value. */
	RBTMATH_CLOUD_BOUNDS clamp = slab;
	clamp.map_address          = RBTMATH_CLOUD_MAP_ADDRESS_CLAMP;
	fill_constant(map6, MAP * MAP, 0, 0, 255, 0);
	for (int row = 0; row < MAP; row++)
		map6[(row * MAP + (MAP - 1)) * 4 + 0] = 153; /* the last column */
	/* s far past 1 along the map axis: the last column, c 0.6. */
	CHECK(density_at(&flat, &clamp, &t, &o_slab, 1000.0f, 8.0f, 50.0f) == 153.0f / 255.0f);
	CHECK(density_at(&flat, &clamp, &t, &o_slab, -1000.0f, 8.0f, 50.0f) == 0.0f);
	/* A shell's r 0 is density 0 (a layer from 0 holds the centre). */
	RBTMATH_CLOUD_BOUNDS centre = shell_bounds(1.0f, 2.0f, RBTMATH_CLOUD_CUBE_WARP_PLAIN);
	centre.bottom               = 0.0f;
	(void)centre;
}

/* ---- Test 7: the cube's faces ------------------------------------------------ */

/* With e and P 1 and no erosion, a coverage c gives step 5's d0. */
static float coverage_density(float c) { return sat((1.0f - (1.0f - c)) / c) * c; }

static void test_cube_faces(void) {
	fill_constant(base_vol, VOL * VOL * VOL, 255, 0, 0, 0);
	fill_constant(detail_vol, VOL * VOL * VOL, 0, 0, 0, 0);
	for (int f = 0; f < 6; f++)
		fill_constant(map6 + (size_t)f * MAP * MAP * 4, MAP * MAP, (uint8_t)(30 * (f + 1)), 0, 255, 0);
	RBTMATH_CLOUD_SHAPE s       = full_shape();
	const RBTMATH_CLOUD_TEXELS t = texels_of(MAP);
	const RBTMATH_CLOUD_OFFSETS o = zero_offsets();
	for (int w = 0; w < 2; w++) {
		RBTMATH_CLOUD_BOUNDS b = shell_bounds(50.0f, 150.0f, w ? RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR : RBTMATH_CLOUD_CUBE_WARP_PLAIN);
#define FACE_IS(x, y, z, f) CHECK(density_at(&s, &b, &t, &o, (x), (y), (z)) == coverage_density((float)(30 * ((f) + 1)) / 255.0f))
		/* Along each axis. */
		FACE_IS(100.0f, 0.0f, 0.0f, 0);
		FACE_IS(-100.0f, 0.0f, 0.0f, 1);
		FACE_IS(0.0f, 100.0f, 0.0f, 2);
		FACE_IS(0.0f, -100.0f, 0.0f, 3);
		FACE_IS(0.0f, 0.0f, 100.0f, 4);
		FACE_IS(0.0f, 0.0f, -100.0f, 5);
		/* Edges: ties to x, then y. */
		FACE_IS(70.0f, 70.0f, 0.0f, 0);
		FACE_IS(-70.0f, 70.0f, 0.0f, 1);
		FACE_IS(70.0f, 0.0f, -70.0f, 0);
		FACE_IS(-70.0f, 0.0f, 70.0f, 1);
		FACE_IS(0.0f, 70.0f, 70.0f, 2);
		FACE_IS(0.0f, -70.0f, -70.0f, 3);
		/* Corners: x. */
		FACE_IS(60.0f, 60.0f, 60.0f, 0);
		FACE_IS(-60.0f, -60.0f, -60.0f, 1);
		/* Off the ties. */
		FACE_IS(60.0f, 61.0f, 60.0f, 2);
		FACE_IS(60.0f, 60.0f, -61.0f, 5);
#undef FACE_IS
	}
	/* Within a face: sc and tc's signs, by a map whose texels differ. For +Z,
	 * sc = +x, tc = -y: the point (+x, -y) reads the face's right, bottom row
	 * (s near 1, t near 1, texel (M-1, M-1)). */
	{
		RBTMATH_CLOUD_BOUNDS b = shell_bounds(50.0f, 150.0f, RBTMATH_CLOUD_CUBE_WARP_PLAIN);
		const struct { int face; float x, y, z; int tx, ty; } cases[] = {
			{0, 60.0f, -54.0f, -54.0f, MAP - 1, MAP - 1}, /* +X: sc -z, tc -y */
			{1, -60.0f, -54.0f, 54.0f, MAP - 1, MAP - 1}, /* -X: sc +z, tc -y */
			{2, 54.0f, 60.0f, 54.0f, MAP - 1, MAP - 1},   /* +Y: sc +x, tc +z */
			{3, 54.0f, -60.0f, -54.0f, MAP - 1, MAP - 1}, /* -Y: sc +x, tc -z */
			{4, 54.0f, -54.0f, 60.0f, MAP - 1, MAP - 1},  /* +Z: sc +x, tc -y */
			{5, -54.0f, -54.0f, -60.0f, MAP - 1, MAP - 1},/* -Z: sc -x, tc -y */
		};
		for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
			fill_constant(map6, 6 * MAP * MAP, 0, 0, 255, 0);
			uint8_t *face = map6 + (size_t)cases[c].face * MAP * MAP * 4;
			face[(cases[c].ty * MAP + cases[c].tx) * 4] = 255;
			const float d = density_at(&s, &b, &t, &o, cases[c].x, cases[c].y, cases[c].z);
			/* 0.9 of the way to the edge, u = 0.95 * 4 - 0.5 = 3.3: clamped to the last texel. */
			if (d != 1.0f) fprintf(stderr, "face %d: %g\n", cases[c].face, (double)d);
			CHECK(d == 1.0f);
		}
	}
	/* PLAIN and EQUI_ANGULAR agree at a face's centre and its edges' midpoints. */
	fill_random(map6, sizeof(map6));
	{
		RBTMATH_CLOUD_BOUNDS plain = shell_bounds(50.0f, 150.0f, RBTMATH_CLOUD_CUBE_WARP_PLAIN);
		RBTMATH_CLOUD_BOUNDS equi  = shell_bounds(50.0f, 150.0f, RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR);
		const float pts[][3] = {{100, 0, 0}, {0, 100, 0}, {0, 0, -100}, {70, 70, 0}, {70, -70, 0}, {70, 0, 70}, {0, 70, -70}, {-70, 0, -70}};
		for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
			CHECK(density_at(&s, &plain, &t, &o, pts[i][0], pts[i][1], pts[i][2]) ==
			      density_at(&s, &equi, &t, &o, pts[i][0], pts[i][1], pts[i][2]));
		/* And differ between (the warps are different maps). */
		int differ = 0;
		for (int i = 0; i < 64; i++) {
			const float y = uniform(-90.0f, 90.0f), z = uniform(-90.0f, 90.0f);
			if (density_at(&s, &plain, &t, &o, 100.0f, y, z) != density_at(&s, &equi, &t, &o, 100.0f, y, z))
				differ++;
		}
		CHECK(differ > 0);
	}
}

/* ---- Test 8: every check, in order, nothing written --------------------------- */

typedef struct refusal {
	const char *expect; /* a piece of the message */
	RBTMATH_CLOUD_SHAPE shape;
	RBTMATH_CLOUD_BOUNDS bounds;
	RBTMATH_CLOUD_TEXELS texels;
	RBTMATH_CLOUD_OFFSETS offsets;
	float point[3];
} refusal;

static void expect_refused(const refusal *r) {
	float out = 7.0f;
	RBTMATH_ERROR error;
	error.message[0] = '\0';
	const RBT_RESULT res = rbtmath_cloud_compute_density(&r->shape, &r->bounds, &r->texels, &r->offsets, r->point, 1, &out, &error);
	if (res != RBT_RESULT_INVALID_ARGUMENT || out != 7.0f || strstr(error.message, r->expect) == NULL) {
		fprintf(stderr, "refusal '%s': result %d, out %g, message '%s'\n", r->expect, (int)res, (double)out, error.message);
		++failures;
	}
}

static void test_refusals(void) {
	fill_random(base_vol, sizeof(base_vol));
	fill_random(detail_vol, sizeof(detail_vol));
	fill_random(map6, sizeof(map6));
	refusal good;
	memset(&good, 0, sizeof(good));
	good.shape    = full_shape();
	good.bounds   = slab_bounds(0.0f, 10.0f);
	good.texels   = texels_of(MAP);
	good.offsets  = zero_offsets();
	good.point[1] = 5.0f;
	{
		float out = 0.0f;
		const RBT_RESULT r = rbtmath_cloud_compute_density(&good.shape, &good.bounds, &good.texels, &good.offsets, good.point, 1, &out, NULL);
		if (pending(r, "rbtmath_cloud_compute_density (refusals)"))
			return;
		CHECK(r == RBT_RESULT_OK);
	}
	refusal shell_good = good;
	shell_good.bounds  = shell_bounds(50.0f, 60.0f, RBTMATH_CLOUD_CUBE_WARP_PLAIN);
	shell_good.point[1] = 55.0f;
	const float inf = INFINITY, nan = NAN;
	refusal r;
	/* NULLs. */
	{
		float out = 7.0f;
		RBTMATH_ERROR e;
		CHECK(rbtmath_cloud_compute_density(NULL, &good.bounds, &good.texels, &good.offsets, good.point, 1, &out, &e) == RBT_RESULT_INVALID_ARGUMENT);
		CHECK(strstr(e.message, "shape is NULL") != NULL);
		CHECK(rbtmath_cloud_compute_density(&good.shape, NULL, &good.texels, &good.offsets, good.point, 1, &out, &e) == RBT_RESULT_INVALID_ARGUMENT);
		CHECK(strstr(e.message, "bounds is NULL") != NULL);
		CHECK(rbtmath_cloud_compute_density(&good.shape, &good.bounds, NULL, &good.offsets, good.point, 1, &out, &e) == RBT_RESULT_INVALID_ARGUMENT);
		CHECK(strstr(e.message, "texels is NULL") != NULL);
		CHECK(rbtmath_cloud_compute_density(&good.shape, &good.bounds, &good.texels, NULL, good.point, 1, &out, &e) == RBT_RESULT_INVALID_ARGUMENT);
		CHECK(strstr(e.message, "offsets is NULL") != NULL);
		CHECK(rbtmath_cloud_compute_density(&good.shape, &good.bounds, &good.texels, &good.offsets, NULL, 1, &out, &e) == RBT_RESULT_INVALID_ARGUMENT);
		CHECK(strstr(e.message, "points is NULL") != NULL);
		CHECK(rbtmath_cloud_compute_density(&good.shape, &good.bounds, &good.texels, &good.offsets, good.point, 1, NULL, &e) == RBT_RESULT_INVALID_ARGUMENT);
		CHECK(strstr(e.message, "out_density is NULL") != NULL);
		CHECK(out == 7.0f);
		CHECK(rbtmath_cloud_compute_density(&good.shape, &good.bounds, &good.texels, &good.offsets, NULL, 0, NULL, &e) == RBT_RESULT_OK);
	}
	/* The shape, in its members' order. */
	r = good; r.shape.type_count = 5; r.expect = "type_count"; expect_refused(&r);
	r = good; r.shape.type_count = 0; r.expect = "type_count"; expect_refused(&r);
	r = good; r.shape.profiles[0] = nan; r.expect = "profiles[0] is not finite"; expect_refused(&r);
	r = good; r.shape.profiles[0] = -0.1f; r.expect = "profiles[0]"; expect_refused(&r);
	r = good; r.shape.profiles[3] = 1.5f; r.expect = "profiles[3]"; expect_refused(&r);
	r = good; r.shape.profiles[1] = 0.0f; r.expect = "profile 0's a"; expect_refused(&r);
	r = good; r.shape.profiles[2] = 0.2f; r.expect = "profile 0's b"; expect_refused(&r);
	r = good; r.shape.profiles[2] = 1.0f; r.expect = "profile 0's c"; expect_refused(&r);
	r = good; r.shape.profiles[4] = 0.5f; r.expect = "profiles[4]"; expect_refused(&r);
	r = good; r.shape.base_tile = 0.0f; r.expect = "base_tile"; expect_refused(&r);
	r = good; r.shape.base_tile = inf; r.expect = "base_tile is not finite"; expect_refused(&r);
	r = good; r.shape.detail_tile = -1.0f; r.expect = "detail_tile"; expect_refused(&r);
	r = good; r.shape.erosion = 1.5f; r.expect = "erosion"; expect_refused(&r);
	r = good; r.shape.erosion = nan; r.expect = "erosion is not finite"; expect_refused(&r);
	r = good; r.shape.billow_height = 0.0f; r.expect = "billow_height"; expect_refused(&r);
	r = good; r.shape.billow_height = 1.5f; r.expect = "billow_height"; expect_refused(&r);
	r = good; r.shape.density_scale = -1.0f; r.expect = "density_scale"; expect_refused(&r);
	r = good; r.shape.coverage_scale = inf; r.expect = "coverage_scale is not finite"; expect_refused(&r);
	r = good; r.shape.coverage_bias = nan; r.expect = "coverage_bias is not finite"; expect_refused(&r);
	/* The bounds. */
	r = good; r.bounds.kind = (RBTMATH_CLOUD_BOUNDS_KIND)0; r.expect = "bounds' kind"; expect_refused(&r);
	r = good; r.bounds.kind = (RBTMATH_CLOUD_BOUNDS_KIND)3; r.expect = "bounds' kind"; expect_refused(&r);
	r = good; r.bounds.bottom = nan; r.expect = "bottom is not finite"; expect_refused(&r);
	r = shell_good; r.bounds.bottom = 0.0f; r.expect = "bottom"; expect_refused(&r);
	r = good; r.bounds.top = 0.0f; r.expect = "top"; expect_refused(&r);
	r = good; r.bounds.top = inf; r.expect = "top is not finite"; expect_refused(&r);
	r = good; r.bounds.up[1] = 0.5f; r.expect = "up's length"; expect_refused(&r);
	r = good; r.bounds.up[2] = nan; r.expect = "up is not finite"; expect_refused(&r);
	r = shell_good; r.bounds.up[1] = 1.0f; r.expect = "up is not 0"; expect_refused(&r);
	r = good; r.bounds.map_axis[0] = 0.0f; r.bounds.map_axis[2] = 2.0f; r.expect = "map_axis's length"; expect_refused(&r);
	r = good; r.bounds.map_axis[0] = 0.0f; r.bounds.map_axis[1] = 1.0f; r.expect = "square to up"; expect_refused(&r);
	r = good; r.bounds.map_axis[0] = nan; r.expect = "map_axis is not finite"; expect_refused(&r);
	r = shell_good; r.bounds.map_axis[0] = 1.0f; r.expect = "map_axis is not 0"; expect_refused(&r);
	r = good; r.bounds.map_extent = 0.0f; r.expect = "map_extent"; expect_refused(&r);
	r = good; r.bounds.map_extent = inf; r.expect = "map_extent is not finite"; expect_refused(&r);
	r = shell_good; r.bounds.map_extent = 1.0f; r.expect = "map_extent is not 0"; expect_refused(&r);
	r = good; r.bounds.map_address = (RBTMATH_CLOUD_MAP_ADDRESS)0; r.expect = "map_address"; expect_refused(&r);
	r = good; r.bounds.map_address = (RBTMATH_CLOUD_MAP_ADDRESS)3; r.expect = "map_address"; expect_refused(&r);
	r = shell_good; r.bounds.map_address = RBTMATH_CLOUD_MAP_ADDRESS_REPEAT; r.expect = "map_address is not UNKNOWN"; expect_refused(&r);
	r = good; r.bounds.cube_warp = RBTMATH_CLOUD_CUBE_WARP_PLAIN; r.expect = "cube_warp is not UNKNOWN"; expect_refused(&r);
	r = shell_good; r.bounds.cube_warp = (RBTMATH_CLOUD_CUBE_WARP)0; r.expect = "cube_warp"; expect_refused(&r);
	r = shell_good; r.bounds.cube_warp = (RBTMATH_CLOUD_CUBE_WARP)3; r.expect = "cube_warp"; expect_refused(&r);
	/* The texels. */
	r = good; r.texels.base = NULL; r.expect = "texels' base is NULL"; expect_refused(&r);
	r = good; r.texels.base_size = 0; r.expect = "base_size"; expect_refused(&r);
	r = good; r.texels.base_size = 257; r.expect = "base_size"; expect_refused(&r);
	r = good; r.texels.detail = NULL; r.expect = "texels' detail is NULL"; expect_refused(&r);
	r = good; r.texels.detail_size = 0; r.expect = "detail_size"; expect_refused(&r);
	r = good; r.texels.coverage = NULL; r.expect = "texels' coverage is NULL"; expect_refused(&r);
	r = good; r.texels.coverage_size = 0; r.expect = "coverage_size"; expect_refused(&r);
	r = good; r.texels.coverage_size = 4097; r.expect = "coverage_size"; expect_refused(&r);
	/* The offsets. */
	r = good; r.offsets.base[1] = nan; r.expect = "offsets' base[1] is not finite"; expect_refused(&r);
	r = good; r.offsets.base[2] = 70000.0f; r.expect = "offsets' base[2]"; expect_refused(&r);
	r = good; r.offsets.detail[0] = -70000.0f; r.expect = "offsets' detail[0]"; expect_refused(&r);
	r = good; r.offsets.coverage[1] = inf; r.expect = "offsets' coverage[1] is not finite"; expect_refused(&r);
	r = shell_good; r.offsets.coverage[0] = 0.5f; r.expect = "coverage[0] is not 0"; expect_refused(&r);
	/* A point. */
	r = good; r.point[0] = nan; r.expect = "point 0 is not finite"; expect_refused(&r);
	r = good; r.shape.base_tile = 1e-6f; r.point[0] = 100.0f; r.expect = "point 0 is read at a base"; expect_refused(&r);
	r = good; r.shape.detail_tile = 1e-6f; r.point[0] = 100.0f; r.expect = "point 0 is read at a detail"; expect_refused(&r);
	/* Outside the layer, the noise isn't read: accepted. */
	{
		refusal far = good;
		far.shape.base_tile = 1e-6f;
		far.point[0]        = 100.0f;
		far.point[1]        = 50.0f;
		float out           = -1.0f;
		CHECK(rbtmath_cloud_compute_density(&far.shape, &far.bounds, &far.texels, &far.offsets, far.point, 1, &out, NULL) == RBT_RESULT_OK);
		CHECK(out == 0.0f);
	}
	/* The order: two faults, the earlier named. */
	r = good; r.shape.erosion = 2.0f; r.bounds.top = -1.0f; r.expect = "erosion"; expect_refused(&r);
	r = good; r.bounds.top = -1.0f; r.texels.base = NULL; r.expect = "top"; expect_refused(&r);
	r = good; r.texels.base = NULL; r.offsets.base[0] = nan; r.expect = "base is NULL"; expect_refused(&r);
}

int main(void) {
	test_density_steps();
	test_density_against_reference();
	test_density_bounds();
	test_cube_faces();
	test_refusals();
	if (failures) {
		fprintf(stderr, "rbtmathclouds_tests: %d failure(s)\n", failures);
		return 1;
	}
	printf("rbtmathclouds_tests: all passed\n");
	return 0;
}
