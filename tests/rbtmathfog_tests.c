/*
 * Rabbittus Math's fog (rbtmathfog.h; card A59's Math half,
 * rbtmath_fog_draft.h, section 6, tests 1 to 6): a height fog's density as
 * worked by hand, a plane's transmittance against a dense march in double,
 * its small slopes, a sphere's approximation against the march (its largest
 * error printed and held to the bound rbtmathfog.h's "Accuracy" points to),
 * a fog volume's density, and every argument check.
 *
 * Written in C, as the other tests of a C header are.
 */

#include "math/rbtmathfog.h"
#include "math/rbtmathnoise.h"

#include <float.h>
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

/* The largest relative error in tau a sphere's transmittance may have over
 * test 4's range, for each of its two scale heights on a 150 km sphere:
 * x = r / H about 1,500 (100 m) and 75 (2 km). The Chapman function's
 * large-x form (rbtmathfog.h, "Accuracy") is low by about 1 / x straight up
 * and less towards the horizon; measured on the Mac 2026-11-18 at 0.00067
 * (100 m) and 0.0129 (2 km, on steep segments), each rounded up. The method
 * is the limit here: at x = 75 the form itself is 1.28% low at mu 1. */
#define SPHERE_TAU_BOUND_100M 0.001
#define SPHERE_TAU_BOUND_2KM 0.015

static uint32_t lcg = 2026u;
static float uniform(float lo, float hi) {
	lcg = lcg * 1664525u + 1013904223u;
	return lo + (hi - lo) * ((float)(lcg >> 8) / 16777216.0f);
}

static RBTMATH_HEIGHT_FOG plane_fog(float density, float scale_height, float base) {
	RBTMATH_HEIGHT_FOG f;
	memset(&f, 0, sizeof(f));
	f.kind         = RBTMATH_FOG_HEIGHT_KIND_PLANE;
	f.up[0]        = 0.0f;
	f.up[1]        = 1.0f;
	f.up[2]        = 0.0f;
	f.base         = base;
	f.density      = density;
	f.scale_height = scale_height;
	return f;
}

static RBTMATH_HEIGHT_FOG sphere_fog(float density, float scale_height, float radius) {
	RBTMATH_HEIGHT_FOG f;
	memset(&f, 0, sizeof(f));
	f.kind         = RBTMATH_FOG_HEIGHT_KIND_SPHERE;
	f.base         = radius;
	f.density      = density;
	f.scale_height = scale_height;
	return f;
}

/* ---- 1. Density, as worked ---- */

static void test_density_exact(void) {
	const float s           = 50.0f;
	RBTMATH_HEIGHT_FOG fog  = plane_fog(0.02f, s, 10.0f);
	const float heights[5]  = {-10.0f * s, 0.0f, s, 10.0f * s, 100.0f * s};
	float points[15];
	for (int i = 0; i < 5; ++i) {
		points[i * 3 + 0] = 3.0f;
		points[i * 3 + 1] = 10.0f + heights[i];
		points[i * 3 + 2] = -7.0f;
	}
	float density[5];
	const RBT_RESULT r = rbtmath_height_fog_compute_density(&fog, points, 5, density, NULL);
	if (pending(r, "test_density_exact"))
		return;
	CHECK(r == RBT_RESULT_OK);
	CHECK(density[0] == 0.02f);                                  /* held below the base */
	CHECK(density[1] == 0.02f);                                  /* at the base */
	CHECK(fabs(density[2] - 0.02 * exp(-1.0)) <= 0.02 * 1e-6);   /* 1/e a scale height up */
	CHECK(fabs(density[3] - 0.02 * exp(-10.0)) <= 0.02 * exp(-10.0) * 1e-6);
	CHECK(density[4] >= 0.0f);
	CHECK(density[4] <= 1e-40f);

	RBTMATH_HEIGHT_FOG ball = sphere_fog(0.5f, 2.0f, 100.0f);
	const float sp[6]       = {0.0f, 0.0f, 50.0f, 0.0f, 102.0f, 0.0f};
	CHECK(rbtmath_height_fog_compute_density(&ball, sp, 2, density, NULL) == RBT_RESULT_OK);
	CHECK(density[0] == 0.5f);
	CHECK(fabs(density[1] - 0.5 * exp(-1.0)) <= 0.5 * 1e-6);
}

/* ---- The march: a 2^16-step midpoint sum in double ---- */

#define MARCH_STEPS 65536

/* A plane's: the height linear between the endpoints' heights, as the
 * call makes them (in float, by the Definition's HEIGHT). */
static float plane_height(const RBTMATH_HEIGHT_FOG *f, const float *p) {
	return ((p[0] * f->up[0]) + (p[1] * f->up[1])) + (p[2] * f->up[2]) - f->base;
}

static double length_between(const float *a, const float *b) {
	const double d[3] = {(double)b[0] - a[0], (double)b[1] - a[1], (double)b[2] - a[2]};
	return sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

static double density_at(const RBTMATH_HEIGHT_FOG *f, double y) {
	return y < 0.0 ? (double)f->density : (double)f->density * exp(-y / (double)f->scale_height);
}

static double march_plane(const RBTMATH_HEIGHT_FOG *f, const float *a, const float *b) {
	const double ya = plane_height(f, a), yb = plane_height(f, b);
	const double L  = length_between(a, b);
	double tau      = 0.0;
	for (int i = 0; i < MARCH_STEPS; ++i) {
		const double t = ((double)i + 0.5) / MARCH_STEPS;
		tau += density_at(f, ya + t * (yb - ya));
	}
	return tau * L / MARCH_STEPS;
}

static double march_sphere(const RBTMATH_HEIGHT_FOG *f, const float *a, const float *b) {
	const double L = length_between(a, b);
	double tau     = 0.0;
	for (int i = 0; i < MARCH_STEPS; ++i) {
		const double t    = ((double)i + 0.5) / MARCH_STEPS;
		const double p[3] = {a[0] + t * ((double)b[0] - a[0]), a[1] + t * ((double)b[1] - a[1]), a[2] + t * ((double)b[2] - a[2])};
		tau += density_at(f, sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - (double)f->base);
	}
	return tau * L / MARCH_STEPS;
}

/* Whether T agrees with exp(-tau): within `rel` of tau, or within T's own
 * rounding (a float's ulp at T's size, twice). */
static int agrees(float T, double tau, double rel, double *worst) {
	const double want = exp(-tau);
	const double ulp2 = 2.0 * (double)FLT_EPSILON * (want > 1e-30 ? want : 1e-30);
	const double got_tau = T > 0.0f ? -log((double)T) : INFINITY;
	const double e       = fabs(got_tau - tau) / (tau > 0.0 ? tau : 1.0);
	const int ok         = fabs((double)T - want) <= ulp2 || fabs(got_tau - tau) <= rel * tau;
	if (!ok && worst && e > *worst)
		*worst = e;
	return ok;
}

/* ---- 2. A plane against the march ---- */

static void test_plane_against_march(void) {
	enum { COUNT = 10000 };
	const float s             = 100.0f;
	RBTMATH_HEIGHT_FOG fog    = plane_fog(0.004f, s, 30.0f);
	/* a tilted up, its length 1 within the check's 1e-4 */
	fog.up[0]                 = 0.28f;
	fog.up[1]                 = 0.96f;
	fog.up[2]                 = 0.0f;
	static float starts[COUNT * 3], ends[COUNT * 3], T[COUNT];
	for (int i = 0; i < COUNT; ++i) {
		float *a = starts + i * 3, *b = ends + i * 3;
		for (int c = 0; c < 3; ++c)
			a[c] = uniform(-2000.0f, 2000.0f);
		switch (i % 5) {
		case 0: /* level-ish */
			b[0] = a[0] + uniform(-1500.0f, 1500.0f); b[2] = a[2] + uniform(-1500.0f, 1500.0f);
			b[1] = a[1] - (b[0] - a[0]) * (0.28f / 0.96f);
			break;
		case 1: /* steep */
			b[0] = a[0] + uniform(-10.0f, 10.0f); b[1] = uniform(-2000.0f, 2000.0f); b[2] = a[2];
			break;
		case 2: /* crossing the base */
			a[1] = uniform(-500.0f, -10.0f); b[0] = a[0] + uniform(-200.0f, 200.0f); b[1] = uniform(100.0f, 800.0f); b[2] = a[2];
			break;
		case 3: /* long and grazing */
			b[0] = a[0] + uniform(-4000.0f, 4000.0f); b[1] = a[1] + uniform(-20.0f, 20.0f); b[2] = a[2] + uniform(-4000.0f, 4000.0f);
			break;
		default: /* very short */
			b[0] = a[0] + 1e-4f; b[1] = a[1]; b[2] = a[2];
			break;
		}
	}
	const RBT_RESULT r = rbtmath_height_fog_compute_transmittance(&fog, starts, ends, COUNT, T, NULL);
	if (pending(r, "test_plane_against_march"))
		return;
	CHECK(r == RBT_RESULT_OK);
	int bad      = 0;
	double worst = 0.0;
	for (int i = 0; i < COUNT; ++i) {
		const double tau = march_plane(&fog, starts + i * 3, ends + i * 3);
		if (tau > 80.0)
			continue; /* past a float's T */
		if (!agrees(T[i], tau, 1e-5, &worst))
			++bad;
	}
	CHECK(bad == 0);
	if (bad)
		fprintf(stderr, "test_plane_against_march: %d of %d segments past 1e-5 (worst %.3g)\n", bad, COUNT, worst);
}

/* ---- 3. Small slopes: g continuous across 2^-4, a level segment exact ---- */

static void test_plane_small_slope(void) {
	const float s          = 10.0f;
	RBTMATH_HEIGHT_FOG fog = plane_fog(0.1f, s, 0.0f);
	/* A segment of length about 10 rising x scale heights: tau about 1. */
	const double xs[4] = {0x1p-4 * (1.0 - 0x1p-20), 0x1p-4 * (1.0 + 0x1p-20), -0x1p-4 * (1.0 - 0x1p-20), -0x1p-4 * (1.0 + 0x1p-20)};
	float T[4];
	for (int k = 0; k < 4; ++k) {
		const float a[3] = {0.0f, 5.0f, 0.0f};
		const float b[3] = {10.0f, (float)(5.0 + xs[k] * s), 0.0f};
		const RBT_RESULT r = rbtmath_height_fog_compute_transmittance(&fog, a, b, 1, &T[k], NULL);
		if (pending(r, "test_plane_small_slope"))
			return;
		CHECK(r == RBT_RESULT_OK);
	}
	CHECK(fabs(log((double)T[0]) - log((double)T[1])) <= 0x1p-22 * -log((double)T[0]) + 4.0 * FLT_EPSILON);
	CHECK(fabs(log((double)T[2]) - log((double)T[3])) <= 0x1p-22 * -log((double)T[2]) + 4.0 * FLT_EPSILON);
	/* level: tau exactly density x L x E(-y/s) */
	const float a[3] = {0.0f, 7.0f, 0.0f}, b[3] = {12.0f, 7.0f, 0.0f};
	float level      = 0.0f;
	CHECK(rbtmath_height_fog_compute_transmittance(&fog, a, b, 1, &level, NULL) == RBT_RESULT_OK);
	const double tau  = 0.1 * 12.0 * (double)(float)exp(-0.7);
	const float want  = (float)exp(-tau);
	CHECK(fabsf(level - want) <= 2.0f * FLT_EPSILON * want);
	/* a zero-length segment lets everything through */
	CHECK(rbtmath_height_fog_compute_transmittance(&fog, a, a, 1, &level, NULL) == RBT_RESULT_OK);
	CHECK(level == 1.0f);
}

/* ---- 4. A sphere against the march ---- */

static void test_sphere_against_march(void) {
	enum { COUNT = 2000 };
	const float R              = 150000.0f;
	const float scales[2]      = {100.0f, 2000.0f};
	static float starts[COUNT * 3], ends[COUNT * 3], T[COUNT];
	const double bounds[2]     = {SPHERE_TAU_BOUND_100M, SPHERE_TAU_BOUND_2KM};
	for (int k = 0; k < 2; ++k) {
		double worst           = 0.0;
		const float s          = scales[k];
		RBTMATH_HEIGHT_FOG fog = sphere_fog(1.0f / s, s, R);
		for (int i = 0; i < COUNT; ++i) {
			float *a = starts + i * 3, *b = ends + i * 3;
			const float h0 = uniform(0.0f, 3.0f * s);
			a[0] = 0.0f; a[1] = R + h0; a[2] = 0.0f;
			const float horizon = (float)sqrt(2.0 * R * (h0 + 6.0 * s));
			switch (i % 5) {
			case 0: /* from the ground towards the horizon, level */
				b[0] = uniform(0.1f, 1.0f) * horizon; b[1] = a[1]; b[2] = uniform(-0.1f, 0.1f) * horizon;
				break;
			case 1: /* upwards, steep */
				b[0] = uniform(-0.3f, 0.3f) * s; b[1] = a[1] + uniform(0.1f, 10.0f) * s; b[2] = 0.0f;
				break;
			case 2: /* grazing: rising slowly, past the closest point */
				a[0] = -uniform(0.1f, 1.0f) * horizon; b[0] = uniform(0.1f, 1.0f) * horizon; b[1] = a[1] + uniform(-0.5f, 0.5f) * s; b[2] = 0.0f;
				break;
			case 3: /* down through the base, below it */
				b[0] = uniform(0.0f, 2.0f) * s; b[1] = R - uniform(0.1f, 3.0f) * s; b[2] = uniform(-1.0f, 1.0f) * s;
				break;
			default: /* from above, down past the base and out again */
				a[1] = R + uniform(1.0f, 5.0f) * s; a[0] = -uniform(1.0f, 4.0f) * (float)sqrt(2.0 * R * s);
				b[0] = -a[0]; b[1] = a[1]; b[2] = 0.0f;
				break;
			}
		}
		const RBT_RESULT r = rbtmath_height_fog_compute_transmittance(&fog, starts, ends, COUNT, T, NULL);
		if (pending(r, "test_sphere_against_march"))
			return;
		CHECK(r == RBT_RESULT_OK);
		for (int i = 0; i < COUNT; ++i) {
			const double tau = march_sphere(&fog, starts + i * 3, ends + i * 3);
			if (tau > 80.0 || tau < 1e-6)
				continue;
			const double got = -log((double)T[i]);
			const double e   = fabs(got - tau) / tau;
			if (e > worst)
				worst = e;
		}
		printf("rbtmathfog_tests: a sphere's transmittance, scale height %g: largest relative error in tau against the march %.4g (bound %.3g)\n",
		       scales[k], worst, bounds[k]);
		CHECK(worst <= bounds[k]);
	}

}

/* ---- 5. A fog volume's density ---- */

static void test_volume_density(void) {
	RBTMATH_FOG_VOLUME box;
	memset(&box, 0, sizeof(box));
	box.shape           = RBTMATH_FOG_VOLUME_SHAPE_BOX;
	box.half_extents[0] = 4.0f;
	box.half_extents[1] = 2.0f;
	box.half_extents[2] = 3.0f;
	box.edge            = 1.0f;
	box.density         = 0.25f;
	const float points[18] = {
		0.0f, 0.0f, 0.0f,  /* the middle: full */
		0.0f, 1.5f, 0.0f,  /* half way into the fade: half */
		0.0f, 2.5f, 0.0f,  /* outside */
		3.75f, 0.0f, 0.0f, /* a quarter in */
		0.0f, 0.0f, -3.0f, /* on the boundary: 0 */
		-3.0f, -1.0f, 2.0f /* at the fade's end on every axis: full */
	};
	float d[6];
	RBT_RESULT r = rbtmath_fog_volume_compute_density(&box, NULL, 0, NULL, points, 6, d, NULL);
	if (pending(r, "test_volume_density"))
		return;
	CHECK(r == RBT_RESULT_OK);
	CHECK(d[0] == 0.25f);
	CHECK(d[1] == 0.125f);
	CHECK(d[2] == 0.0f);
	CHECK(d[3] == 0.0625f);
	CHECK(d[4] == 0.0f);
	CHECK(d[5] == 0.25f);
	box.edge = 0.0f; /* a hard edge: a step */
	CHECK(rbtmath_fog_volume_compute_density(&box, NULL, 0, NULL, points, 6, d, NULL) == RBT_RESULT_OK);
	CHECK(d[1] == 0.25f);
	CHECK(d[2] == 0.0f);
	CHECK(d[3] == 0.25f);
	CHECK(d[4] == 0.0f);

	RBTMATH_FOG_VOLUME ball;
	memset(&ball, 0, sizeof(ball));
	ball.shape           = RBTMATH_FOG_VOLUME_SHAPE_SPHERE;
	ball.half_extents[0] = 5.0f;
	ball.edge            = 2.0f;
	ball.density         = 1.0f;
	const float sp[9]    = {0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 3.0f, 0.0f, 4.0f};
	CHECK(rbtmath_fog_volume_compute_density(&ball, NULL, 0, NULL, sp, 3, d, NULL) == RBT_RESULT_OK);
	CHECK(d[0] == 1.0f);
	CHECK(d[1] == 0.5f);
	CHECK(d[2] == 0.0f);

	/* With noise: against the volume's read by hand. */
	enum { N = 8 };
	static uint8_t noise[N * N * N * 4];
	CHECK(rbtmath_noise_volume_bake(RBTMATH_NOISE_VOLUME_KIND_WORLEY, 3u, N, 0, N, noise, NULL) == RBT_RESULT_OK);
	ball.noise_strength      = 0.75f;
	ball.noise_tile          = 2.5f;
	const float offset[3]    = {0.125f, -3.0f, 7.5f};
	float np[64 * 3];
	for (int i = 0; i < 64 * 3; ++i)
		np[i] = uniform(-3.0f, 3.0f);
	float nd[64];
	CHECK(rbtmath_fog_volume_compute_density(&ball, noise, N, offset, np, 64, nd, NULL) == RBT_RESULT_OK);
	for (int i = 0; i < 64; ++i) {
		const float *p   = np + i * 3;
		const float q[3] = {(p[0] / 2.5f) + offset[0], (p[1] / 2.5f) + offset[1], (p[2] / 2.5f) + offset[2]};
		float v[4];
		CHECK(rbtmath_noise_volume_sample(noise, N, q, 1, v, NULL) == RBT_RESULT_OK);
		const float w    = ((v[0] * 0.625f) + (v[1] * 0.25f)) + (v[2] * 0.125f);
		const float n    = 1.0f + (0.75f * (w - 1.0f));
		const float in   = 5.0f - sqrtf(((p[0] * p[0]) + (p[1] * p[1])) + (p[2] * p[2]));
		float f          = in / 2.0f;
		f                = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
		const float want = in <= 0.0f ? 0.0f : (1.0f * f) * n;
		CHECK(nd[i] == want);
	}
}

/* ---- 6. Every check, in order, each its message, nothing written ---- */

static int refused(RBT_RESULT r, const RBTMATH_ERROR *error, const char *needle) {
	if (r == RBT_RESULT_INVALID_ARGUMENT && strstr(error->message, needle) != NULL)
		return 1;
	fprintf(stderr, "refusal for '%s': result %d, message '%s'\n", needle, (int)r, error->message);
	return 0;
}

static void test_refusals(void) {
	RBTMATH_ERROR error;
	float p[6]   = {0, 0, 0, 1, 1, 1};
	float out[2] = {-1.0f, -1.0f};
	RBTMATH_HEIGHT_FOG f;

	CHECK(refused(rbtmath_height_fog_compute_density(NULL, p, 2, out, &error), &error, "fog is NULL"));
	CHECK(refused(rbtmath_height_fog_compute_density(&(RBTMATH_HEIGHT_FOG){0}, p, 2, out, &error), &error, "kind"));
	f = plane_fog(1, 1, 0); f.kind = (RBTMATH_FOG_HEIGHT_KIND)3;
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "kind"));
	f = plane_fog(1, 1, 0); f.up[1] = INFINITY;
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "up is not finite"));
	f = plane_fog(1, 1, 0); f.up[1] = 0.999f;
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "up's length"));
	f = sphere_fog(1, 1, 5); f.up[2] = 1.0f;
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "up is not 0"));
	f = plane_fog(1, 1, NAN);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "base is not finite"));
	f = sphere_fog(1, 1, 0);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "base"));
	f = plane_fog(NAN, 1, 0);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "density is not finite"));
	f = plane_fog(-1, 1, 0);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "density"));
	f = plane_fog(1, INFINITY, 0);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "scale_height is not finite"));
	f = plane_fog(1, 0, 0);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "scale_height"));
	f = plane_fog(1, 1, 0);
	CHECK(refused(rbtmath_height_fog_compute_density(&f, NULL, 2, out, &error), &error, "points is NULL"));
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, NULL, &error), &error, "out_density is NULL"));
	p[4] = NAN;
	CHECK(refused(rbtmath_height_fog_compute_density(&f, p, 2, out, &error), &error, "point 1"));
	CHECK(refused(rbtmath_height_fog_compute_transmittance(NULL, p, p, 2, out, &error), &error, "fog is NULL"));
	CHECK(refused(rbtmath_height_fog_compute_transmittance(&f, NULL, p, 2, out, &error), &error, "starts is NULL"));
	CHECK(refused(rbtmath_height_fog_compute_transmittance(&f, p, NULL, 2, out, &error), &error, "ends is NULL"));
	CHECK(refused(rbtmath_height_fog_compute_transmittance(&f, p, p, 2, NULL, &error), &error, "out_transmittance is NULL"));
	CHECK(refused(rbtmath_height_fog_compute_transmittance(&f, p, p, 2, out, &error), &error, "start 1"));
	{
		const float good[6] = {0, 0, 0, 1, 1, 1};
		CHECK(refused(rbtmath_height_fog_compute_transmittance(&f, good, p, 2, out, &error), &error, "end 1"));
	}
	CHECK(out[0] == -1.0f && out[1] == -1.0f);
	p[4] = 1.0f;
	CHECK(rbtmath_height_fog_compute_density(&f, NULL, 0, NULL, &error) == RBT_RESULT_OK);
	CHECK(rbtmath_height_fog_compute_transmittance(&f, NULL, NULL, 0, NULL, &error) == RBT_RESULT_OK);

	RBTMATH_FOG_VOLUME v, v0;
	memset(&v0, 0, sizeof(v0));
	v0.shape           = RBTMATH_FOG_VOLUME_SHAPE_BOX;
	v0.half_extents[0] = 1.0f;
	v0.half_extents[1] = 2.0f;
	v0.half_extents[2] = 3.0f;
	v0.edge            = 0.5f;
	v0.density         = 1.0f;
	uint8_t noise[4 * 4 * 4 * 4];
	memset(noise, 128, sizeof(noise));
	const float off[3] = {0, 0, 0};
	CHECK(refused(rbtmath_fog_volume_compute_density(NULL, NULL, 0, NULL, p, 2, out, &error), &error, "volume is NULL"));
	v = v0; v.shape = RBTMATH_FOG_VOLUME_SHAPE_UNKNOWN;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "shape"));
	v = v0; v.half_extents[1] = NAN;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "half_extents[1] is not finite"));
	v = v0; v.half_extents[2] = 0.0f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "half_extents[2]"));
	v = v0; v.shape = RBTMATH_FOG_VOLUME_SHAPE_SPHERE;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "half_extents[1] is not 0"));
	v = v0; v.edge = INFINITY;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "edge is not finite"));
	v = v0; v.edge = -0.5f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "edge"));
	v = v0; v.edge = 1.5f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "edge"));
	v = v0; v.density = NAN;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "density is not finite"));
	v = v0; v.density = -1.0f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "density"));
	v = v0; v.noise_strength = NAN;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "noise_strength is not finite"));
	v = v0; v.noise_strength = 1.5f; v.noise_tile = 1.0f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, off, p, 2, out, &error), &error, "noise_strength"));
	v = v0; v.noise_strength = 0.5f; v.noise_tile = 0.0f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, off, p, 2, out, &error), &error, "noise_tile"));
	v = v0; v.noise_tile = 1.0f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 0, NULL, p, 2, out, &error), &error, "noise_tile is not 0"));
	v = v0; v.noise_strength = 0.5f; v.noise_tile = 1.0f;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, NULL, 4, off, p, 2, out, &error), &error, "noise is NULL"));
	CHECK(refused(rbtmath_fog_volume_compute_density(&v0, noise, 0, NULL, p, 2, out, &error), &error, "noise is given"));
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 0, off, p, 2, out, &error), &error, "noise_size"));
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 257, off, p, 2, out, &error), &error, "noise_size"));
	CHECK(refused(rbtmath_fog_volume_compute_density(&v0, NULL, 4, NULL, p, 2, out, &error), &error, "noise_size"));
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, NULL, p, 2, out, &error), &error, "noise_offset is NULL"));
	{
		const float bad[3] = {0.0f, NAN, 0.0f};
		CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, bad, p, 2, out, &error), &error, "noise_offset[1]"));
		const float far[3] = {0.0f, 0.0f, 70000.0f};
		CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, far, p, 2, out, &error), &error, "noise_offset[2]"));
	}
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, off, NULL, 2, out, &error), &error, "points is NULL"));
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, off, p, 2, NULL, &error), &error, "out_density is NULL"));
	p[4] = NAN;
	CHECK(refused(rbtmath_fog_volume_compute_density(&v, noise, 4, off, p, 2, out, &error), &error, "point 1"));
	p[4] = 0.5f;
	{
		/* inside, its noise coordinate past the limit: refused; outside, never read */
		RBTMATH_FOG_VOLUME tiny = v;
		tiny.noise_tile         = 1e-5f;
		const float inside[6]   = {0.1f, 1.0f, 0.0f, 0.9f, 0.0f, 0.0f};
		CHECK(refused(rbtmath_fog_volume_compute_density(&tiny, noise, 4, off, inside, 2, out, &error), &error, "point 0"));
		const float outside[3] = {5.0f, 0.0f, 0.0f};
		CHECK(rbtmath_fog_volume_compute_density(&tiny, noise, 4, off, outside, 1, out, &error) == RBT_RESULT_OK);
		CHECK(out[0] == 0.0f);
		out[0] = -1.0f;
	}
	CHECK(out[0] == -1.0f && out[1] == -1.0f);
}

int main(void) {
	test_density_exact();
	test_plane_against_march();
	test_plane_small_slope();
	test_sphere_against_march();
	test_volume_density();
	test_refusals();
	if (failures) {
		fprintf(stderr, "rbtmathfog_tests: %d failure(s)\n", failures);
		return 1;
	}
	printf("rbtmathfog_tests: all passed\n");
	return 0;
}
