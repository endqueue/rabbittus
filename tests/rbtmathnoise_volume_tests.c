/*
 * Rabbittus Math's baked noise volumes (rbtmathnoise.h, "Baked volumes" and
 * "Reading a volume"; card A57's Math half, rbtmath_clouds_draft.h, tests 1
 * to 4 and its refusals, built first for card A59's fog volumes): every
 * texel against the definition worked by rbtmath_noise_compute_points(), a
 * volume baked whole and in slices on 1 and 4 threads, its tiling, the read
 * against a trilinear in double, and every argument check.
 *
 * Written in C, as the other tests of a C header are; built with contraction
 * off, as the library is, so the test's own evaluation of the definition
 * rounds as written.
 */

#include "jobs/rbtjobs.h"
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

/* Whether a call was refused for want of an implementation: the contract's
 * tests stop there, pending (rabbittus/CLAUDE.md, "Tests"). */
static int pending(RBT_RESULT r, const char *what) {
	if (r == RBT_RESULT_NOT_IMPLEMENTED) {
		fprintf(stderr, "%s: NOT IMPLEMENTED\n", what);
		++failures;
		return 1;
	}
	return 0;
}

/* ---- The definition, worked in the test ---- */

static float sat(float x) {
	return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static RBTMATH_NOISE_DESC gradient_desc(uint32_t seed) {
	RBTMATH_NOISE_DESC d;
	memset(&d, 0, sizeof(d));
	d.base.type   = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
	d.dimensions  = 3;
	d.kind        = RBTMATH_NOISE_KIND_GRADIENT;
	d.fractal     = RBTMATH_NOISE_FRACTAL_FBM;
	d.seed        = seed ^ 0x243F6A88u;
	d.octaves     = 4;
	d.frequency   = 4.0;
	d.lacunarity  = 2.0;
	d.gain        = 0.5f;
	d.period[0]   = 4;
	d.period[1]   = 4;
	d.period[2]   = 4;
	return d;
}

static RBTMATH_NOISE_DESC cellular_desc(uint32_t seed, uint32_t f) {
	RBTMATH_NOISE_DESC d;
	memset(&d, 0, sizeof(d));
	d.base.type     = RBT_STRUCT_TYPE_MATH_NOISE_DESC;
	d.dimensions    = 3;
	d.kind          = RBTMATH_NOISE_KIND_CELLULAR;
	d.fractal       = RBTMATH_NOISE_FRACTAL_SINGLE;
	d.seed          = (seed ^ 0x85A308D3u) ^ f;
	d.octaves       = 1;
	d.frequency     = (double)f;
	d.cell_distance = RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN;
	d.cell_return   = RBTMATH_NOISE_CELL_RETURN_F1;
	d.cell_jitter   = 1.0f;
	d.period[0]     = f;
	d.period[1]     = f;
	d.period[2]     = f;
	return d;
}

static float worley(uint32_t seed, uint32_t f, const double p[3]) {
	RBTMATH_NOISE_DESC d = cellular_desc(seed, f);
	float F1             = 0.0f;
	CHECK(rbtmath_noise_compute_points(&d, p, 1, &F1, NULL, NULL) == RBT_RESULT_OK);
	return 1.0f - (F1 < 1.0f ? F1 : 1.0f);
}

static float worley_fbm(uint32_t seed, uint32_t f, const double p[3]) {
	return ((worley(seed, f, p) * 0.625f) + (worley(seed, f * 2u, p) * 0.25f)) + (worley(seed, f * 4u, p) * 0.125f);
}

static uint8_t to_byte(float v) {
	return (uint8_t)((v * 255.0f) + 0.5f);
}

/* The definition's four channels at a place (tile units), as values. */
static void channels_at(RBTMATH_NOISE_VOLUME_KIND kind, uint32_t seed, const double p[3], float out[4]) {
	if (kind == RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY) {
		RBTMATH_NOISE_DESC g = gradient_desc(seed);
		float P              = 0.0f;
		CHECK(rbtmath_noise_compute_points(&g, p, 1, &P, NULL, NULL) == RBT_RESULT_OK);
		const float pp = sat((P * 0x1.111112p-2f) + 0.5f);
		const float v4 = worley_fbm(seed, 4, p);
		out[0]         = sat(v4 + (pp * (1.0f - v4)));
		out[1]         = v4;
		out[2]         = worley_fbm(seed, 8, p);
		out[3]         = worley_fbm(seed, 16, p);
	} else {
		out[0] = worley_fbm(seed, 2, p);
		out[1] = worley_fbm(seed, 4, p);
		out[2] = worley_fbm(seed, 8, p);
		out[3] = 0.0f;
	}
}

/* ---- 1. Every texel against the definition ---- */

static void test_bake_against_points(void) {
	enum { N = 16 };
	static uint8_t texels[N * N * N * 4];
	const RBTMATH_NOISE_VOLUME_KIND kinds[2] = {RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY, RBTMATH_NOISE_VOLUME_KIND_WORLEY};
	for (int k = 0; k < 2; ++k) {
		RBTMATH_ERROR error;
		const RBT_RESULT r = rbtmath_noise_volume_bake(kinds[k], 77u, N, 0, N, texels, &error);
		if (pending(r, "test_bake_against_points"))
			return;
		CHECK(r == RBT_RESULT_OK);
		uint32_t mismatches = 0;
		for (uint32_t z = 0; z < N; ++z)
			for (uint32_t y = 0; y < N; ++y)
				for (uint32_t x = 0; x < N; ++x) {
					const double p[3] = {((double)x + 0.5) / N, ((double)y + 0.5) / N, ((double)z + 0.5) / N};
					float v[4];
					channels_at(kinds[k], 77u, p, v);
					const uint8_t *t = texels + (((size_t)z * N + y) * N + x) * 4;
					for (int c = 0; c < 4; ++c)
						if (t[c] != to_byte(v[c]))
							++mismatches;
				}
		CHECK(mismatches == 0);
	}
}

/* ---- 2. Whole, in runs of slices, and on 1 and 4 threads: the same bytes ---- */

enum { BIG = 64 };
static uint8_t whole[BIG * BIG * BIG * 4];
static uint8_t parts[BIG * BIG * BIG * 4];

typedef struct slice_job {
	RBTMATH_NOISE_VOLUME_KIND kind;
	int failed;
} slice_job;

static void bake_slices(uint32_t first, uint32_t end, uint32_t worker, void *data) {
	(void)worker;
	slice_job *job = (slice_job *)data;
	if (rbtmath_noise_volume_bake(job->kind, 5u, BIG, first, end - first, parts + (size_t)first * BIG * BIG * 4, NULL) != RBT_RESULT_OK)
		job->failed = 1;
}

static void test_bake_slices(void) {
	const RBTMATH_NOISE_VOLUME_KIND kind = RBTMATH_NOISE_VOLUME_KIND_WORLEY;
	const RBT_RESULT r                   = rbtmath_noise_volume_bake(kind, 5u, BIG, 0, BIG, whole, NULL);
	if (pending(r, "test_bake_slices"))
		return;
	CHECK(r == RBT_RESULT_OK);
	const uint32_t runs[3] = {1, 7, 64};
	for (int k = 0; k < 3; ++k) {
		memset(parts, 0, sizeof(parts));
		for (uint32_t first = 0; first < BIG; first += runs[k]) {
			const uint32_t n = BIG - first < runs[k] ? BIG - first : runs[k];
			CHECK(rbtmath_noise_volume_bake(kind, 5u, BIG, first, n, parts + (size_t)first * BIG * BIG * 4, NULL) == RBT_RESULT_OK);
		}
		CHECK(memcmp(whole, parts, sizeof(whole)) == 0);
	}
	static const uint32_t threads[2] = {1, 4};
	for (int k = 0; k < 2; ++k) {
		RBTJOBS_POOL_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.base.type    = RBT_STRUCT_TYPE_JOBS_POOL_DESC;
		desc.thread_count = threads[k];
		desc.max_tasks    = 4;
		rbtjobs_pool pool = NULL;
		CHECK(rbtjobs_pool_create(&desc, &pool, NULL) == RBT_RESULT_OK);
		if (!pool)
			continue;
		RBTJOBS_SCHEDULER scheduler;
		memset(&scheduler, 0, sizeof(scheduler));
		CHECK(rbtjobs_pool_get_scheduler(pool, &scheduler) == RBT_RESULT_OK);
		memset(parts, 0, sizeof(parts));
		slice_job job = {kind, 0};
		CHECK(rbtjobs_run(&scheduler, bake_slices, BIG, 3, &job) == RBT_RESULT_OK);
		CHECK(!job.failed);
		CHECK(memcmp(whole, parts, sizeof(whole)) == 0);
		rbtjobs_pool_destroy(pool);
	}
}

/* ---- 3. The volume tiles: each channel at a place and a tile on ---- */

static void test_bake_tiles(void) {
	const RBTMATH_NOISE_VOLUME_KIND kinds[2] = {RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY, RBTMATH_NOISE_VOLUME_KIND_WORLEY};
	uint32_t seed                            = 12345u;
	for (int k = 0; k < 2; ++k)
		for (int i = 0; i < 64; ++i) {
			double p[3];
			for (int a = 0; a < 3; ++a) {
				seed = seed * 1664525u + 1013904223u;
				p[a] = (double)(seed >> 8) / 16777216.0;
			}
			float v[4];
			channels_at(kinds[k], 9u, p, v);
			for (int a = 0; a < 3; ++a) {
				double q[3] = {p[0], p[1], p[2]};
				q[a]        = q[a] + 1.0;
				float w[4];
				channels_at(kinds[k], 9u, q, w);
				for (int c = 0; c < 4; ++c)
					CHECK(fabsf(v[c] - w[c]) <= 1e-6f);
			}
		}
}

/* ---- 4. The read against a trilinear in double ---- */

static double texel_value(const uint8_t *texels, int64_t n, int64_t x, int64_t y, int64_t z, int c) {
	x = ((x % n) + n) % n;
	y = ((y % n) + n) % n;
	z = ((z % n) + n) % n;
	return texels[(((size_t)z * (size_t)n + (size_t)y) * (size_t)n + (size_t)x) * 4 + (size_t)c] / 255.0;
}

static void test_sample_against_double(void) {
	enum { N = 16, COUNT = 100000 };
	static uint8_t texels[N * N * N * 4];
	static float q[COUNT * 3];
	static float values[COUNT * 4];
	uint32_t seed = 99u;
	for (size_t i = 0; i < sizeof(texels); ++i) {
		seed      = seed * 1664525u + 1013904223u;
		texels[i] = (uint8_t)(seed >> 24);
	}
	for (int i = 0; i < COUNT; ++i)
		for (int a = 0; a < 3; ++a) {
			seed = seed * 1664525u + 1013904223u;
			const float u = (float)(seed >> 8) / 16777216.0f;
			switch (i % 4) {
			case 0: q[i * 3 + a] = (u - 0.5f) * 8.0f; break;                                    /* anywhere near the origin */
			case 1: q[i * 3 + a] = (floorf(u * 64.0f) + 0.5f) / N; break;                       /* texel centres */
			case 2: q[i * 3 + a] = floorf(u * 64.0f) / N - 1.0f; break;                         /* texel edges, the wrap */
			default: q[i * 3 + a] = (u - 0.5f) * 2.0f * RBTMATH_NOISE_VOLUME_MAX_COORDINATE; break; /* to the limit */
			}
		}
	const RBT_RESULT r = rbtmath_noise_volume_sample(texels, N, q, COUNT, values, NULL);
	if (pending(r, "test_sample_against_double"))
		return;
	CHECK(r == RBT_RESULT_OK);
	double worst = 0.0;
	for (int i = 0; i < COUNT; ++i) {
		double u[3], t[3];
		int64_t c[3];
		for (int a = 0; a < 3; ++a) {
			u[a] = (double)q[i * 3 + a] * N - 0.5;
			c[a] = (int64_t)floor(u[a]);
			t[a] = u[a] - (double)c[a];
		}
		for (int ch = 0; ch < 4; ++ch) {
			double v = 0.0;
			for (int k = 0; k < 8; ++k) {
				const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
				const double w = (dx ? t[0] : 1.0 - t[0]) * (dy ? t[1] : 1.0 - t[1]) * (dz ? t[2] : 1.0 - t[2]);
				v += w * texel_value(texels, N, c[0] + dx, c[1] + dy, c[2] + dz, ch);
			}
			const double e = fabs(v - (double)values[i * 4 + ch]);
			if (e > worst)
				worst = e;
			/* a texel's centre reads its own byte exactly */
			if (i % 4 == 1 && t[0] == 0.0 && t[1] == 0.0 && t[2] == 0.0)
				CHECK(values[i * 4 + ch] == (float)texel_value(texels, N, c[0], c[1], c[2], ch));
		}
	}
	CHECK(worst <= ldexp(1.0, -20));
	printf("rbtmathnoise_volume_tests: the read's largest error against a trilinear in double, %.3g (bound 2^-20)\n", worst);
}

/* ---- 5. Every check, in order, each its message, nothing written ---- */

static int refused(RBT_RESULT r, const RBTMATH_ERROR *error, const char *needle) {
	return r == RBT_RESULT_INVALID_ARGUMENT && strstr(error->message, needle) != NULL;
}

static void test_refusals(void) {
	uint8_t texels[2 * 2 * 2 * 4];
	memset(texels, 0xAB, sizeof(texels));
	RBTMATH_ERROR error;
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_UNKNOWN, 0, 2, 0, 2, texels, &error), &error, "kind"));
	CHECK(refused(rbtmath_noise_volume_bake((RBTMATH_NOISE_VOLUME_KIND)3, 0, 2, 0, 2, texels, &error), &error, "kind"));
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 0, 0, 1, texels, &error), &error, "size"));
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 257, 0, 1, texels, &error), &error, "size"));
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 2, 2, 1, texels, &error), &error, "first_slice"));
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 2, 0, 0, texels, &error), &error, "slice_count"));
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 2, 1, 2, texels, &error), &error, "slice_count"));
	CHECK(refused(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 2, 0, 2, NULL, &error), &error, "out_texels"));
	for (size_t i = 0; i < sizeof(texels); ++i)
		CHECK(texels[i] == 0xAB);

	float q[6]      = {0.25f, 0.25f, 0.25f, 0.5f, 0.5f, 0.5f};
	float values[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	CHECK(refused(rbtmath_noise_volume_sample(NULL, 2, q, 2, values, &error), &error, "texels"));
	CHECK(refused(rbtmath_noise_volume_sample(texels, 0, q, 2, values, &error), &error, "size"));
	CHECK(refused(rbtmath_noise_volume_sample(texels, 257, q, 2, values, &error), &error, "size"));
	CHECK(refused(rbtmath_noise_volume_sample(texels, 2, NULL, 2, values, &error), &error, "coordinates"));
	CHECK(refused(rbtmath_noise_volume_sample(texels, 2, q, 2, NULL, &error), &error, "out_values"));
	q[4] = NAN;
	CHECK(refused(rbtmath_noise_volume_sample(texels, 2, q, 2, values, &error), &error, "coordinate 1"));
	q[4] = RBTMATH_NOISE_VOLUME_MAX_COORDINATE * 2.0f;
	CHECK(refused(rbtmath_noise_volume_sample(texels, 2, q, 2, values, &error), &error, "coordinate 1"));
	for (int i = 0; i < 8; ++i)
		CHECK(values[i] == -1.0f);
	q[4] = -RBTMATH_NOISE_VOLUME_MAX_COORDINATE;
	CHECK(rbtmath_noise_volume_sample(texels, 2, q, 2, values, &error) == RBT_RESULT_OK);
	CHECK(error.message[0] == '\0');
	CHECK(rbtmath_noise_volume_sample(NULL, 2, NULL, 0, NULL, &error) == RBT_RESULT_OK);
}

int main(void) {
	test_bake_against_points();
	test_bake_slices();
	test_bake_tiles();
	test_sample_against_double();
	test_refusals();
	if (failures) {
		fprintf(stderr, "rbtmathnoise_volume_tests: %d failure(s)\n", failures);
		return 1;
	}
	printf("rbtmathnoise_volume_tests: all passed\n");
	return 0;
}
