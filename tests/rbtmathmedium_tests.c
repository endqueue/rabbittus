/*
 * Rabbittus Math's layered media in a spherical shell (rbtmathmedium.h):
 * densities by height, transmittance against a brute-force sum and the
 * closed form for an exponential layer, the phase functions, the same bits
 * alone and on a pool of threads, and every argument check.
 */

#include "jobs/rbtjobs.h"
#include "math/rbtmathmedium.h"

#include <math.h>
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

/* The earth's medium in kilometres (Hillaire 2020's and Bruneton's values):
 * a 6,360 km ground, 60 km of air, Rayleigh's 8 km, Mie's 1.2 km, ozone
 * peaking at 25 km, 15 km either side. */
static RBTMATH_SHELL_MEDIUM earth(void) {
	RBTMATH_SHELL_MEDIUM m;
	memset(&m, 0, sizeof(m));
	m.bottom_radius            = 6360.0f;
	m.top_radius               = 6420.0f;
	m.rayleigh_scattering[0]   = 5.802e-3f;
	m.rayleigh_scattering[1]   = 13.558e-3f;
	m.rayleigh_scattering[2]   = 33.1e-3f;
	m.rayleigh_scale_height    = 8.0f;
	m.mie_scattering[0]        = 3.996e-3f;
	m.mie_scattering[1]        = 3.996e-3f;
	m.mie_scattering[2]        = 3.996e-3f;
	m.mie_extinction[0]        = 4.4e-3f;
	m.mie_extinction[1]        = 4.4e-3f;
	m.mie_extinction[2]        = 4.4e-3f;
	m.mie_scale_height         = 1.2f;
	m.mie_anisotropy           = 0.8f;
	m.absorption_extinction[0] = 0.65e-3f;
	m.absorption_extinction[1] = 1.881e-3f;
	m.absorption_extinction[2] = 0.085e-3f;
	m.absorption_center_height = 25.0f;
	m.absorption_half_width    = 15.0f;
	m.ground_albedo[0]         = 0.3f;
	m.ground_albedo[1]         = 0.3f;
	m.ground_albedo[2]         = 0.3f;
	return m;
}

static int near_rel(
	double a,
	double b,
	double rel) {
	const double scale = fabs(b) > 1e-30 ? fabs(b) : 1.0;
	return fabs(a - b) <= rel * scale;
}

/* Densities: 1 at the bottom (the tent's peak there too), 1/e a scale
 * height up, 0 above the top and under the ground; the tent's peak at its
 * centre and nothing past its half-width. */
static void test_density(void) {
	RBTMATH_SHELL_MEDIUM m          = earth();
	m.absorption_center_height      = 0.0f;
	const float points[6 * 3]       = {0.0f, 6360.0f, 0.0f, 0.0f, 6368.0f, 0.0f, 6500.0f, 0.0f, 0.0f,
	                                   0.0f, 0.0f, 6000.0f, 0.0f, 6361.2f, 0.0f, 6375.0f, 0.0f, 0.0f};
	float out[6 * 3];
	RBTMATH_ERROR error;
	CHECK(rbtmath_shell_medium_compute_density(&m, points, 6, out, &error) == RBT_RESULT_OK);
	CHECK(near_rel(out[0], 1.0, 1e-6));
	CHECK(near_rel(out[1], 1.0, 1e-6));
	CHECK(near_rel(out[2], 1.0, 1e-6));
	CHECK(near_rel(out[3], exp(-1.0), 1e-5));
	for (int c = 0; c < 3; ++c) {
		CHECK(out[6 + c] == 0.0f);
		CHECK(out[9 + c] == 0.0f);
	}
	/* (6361.2 is the float 6361.2002, so its height is 1.2002 km: e^-1
	 * within that.) */
	CHECK(near_rel(out[13], exp(-1.0), 3e-4));
	CHECK(out[17] == 0.0f);
	/* The tent at 25 km: 1 at its peak, half at 7.5 km off, 0 past 15. */
	m                          = earth();
	const float tent[3 * 3]    = {0.0f, 6385.0f, 0.0f, 0.0f, 6392.5f, 0.0f, 0.0f, 6401.0f, 0.0f};
	CHECK(rbtmath_shell_medium_compute_density(&m, tent, 3, out, NULL) == RBT_RESULT_OK);
	CHECK(near_rel(out[2], 1.0, 1e-6));
	CHECK(near_rel(out[5], 0.5, 1e-4));
	CHECK(out[8] == 0.0f);
}

/* The transmittance along a ray by a plain Riemann sum in doubles with the
 * platform's exp, 100,000 steps: the reference. */
static void brute_transmittance(
	const RBTMATH_SHELL_MEDIUM *m,
	const double o[3],
	const double dir[3],
	double out[3]) {
	const double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
	const double d[3] = {dir[0] / len, dir[1] / len, dir[2] / len};
	const double b    = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
	const double oo   = o[0] * o[0] + o[1] * o[1] + o[2] * o[2];
	out[0] = out[1] = out[2] = 1.0;
	double disc = b * b - (oo - (double)m->top_radius * m->top_radius);
	if (disc < 0.0)
		return;
	const double far_t = -b + sqrt(disc);
	if (far_t <= 0.0)
		return;
	double near_t = -b - sqrt(disc);
	if (near_t < 0.0)
		near_t = 0.0;
	const double g = b * b - (oo - (double)m->bottom_radius * m->bottom_radius);
	if (g >= 0.0 && -b + sqrt(g) > 0.0) {
		out[0] = out[1] = out[2] = 0.0;
		return;
	}
	const int steps   = 100000;
	const double step = (far_t - near_t) / steps;
	double depth[3]   = {0.0, 0.0, 0.0};
	for (int i = 0; i < steps; ++i) {
		const double t    = near_t + (i + 0.5) * step;
		const double p[3] = {o[0] + t * d[0], o[1] + t * d[1], o[2] + t * d[2]};
		const double h    = sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - m->bottom_radius;
		const double r    = exp(-h / m->rayleigh_scale_height);
		const double mi   = exp(-h / m->mie_scale_height);
		double tent       = 1.0 - fabs(h - m->absorption_center_height) / m->absorption_half_width;
		if (tent < 0.0)
			tent = 0.0;
		for (int c = 0; c < 3; ++c)
			depth[c] += (m->rayleigh_scattering[c] * r + m->mie_extinction[c] * mi + m->absorption_extinction[c] * tent) * step;
	}
	for (int c = 0; c < 3; ++c)
		out[c] = exp(-depth[c]);
}

/* Against the brute force: straight up from the ground, along the horizon
 * from 1 km up, a ray into the ground (0), a ray from space through the
 * shell (above the ground), and one that misses it (1). */
static void test_transmittance(void) {
	const RBTMATH_SHELL_MEDIUM m = earth();
	const float origins[5 * 3]   = {0.0f, 6360.0f, 0.0f, 0.0f, 6361.0f, 0.0f, 0.0f, 6365.0f, 0.0f, -7000.0f, 6370.0f, 0.0f, 0.0f, 9000.0f, 0.0f};
	const float dirs[5 * 3]      = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.3f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
	float out[5 * 3];
	CHECK(rbtmath_shell_medium_compute_transmittance(&m, origins, dirs, 5, 1024, out, NULL) == RBT_RESULT_OK);
	for (int i = 0; i < 5; ++i) {
		const double o[3] = {origins[i * 3], origins[i * 3 + 1], origins[i * 3 + 2]};
		const double d[3] = {dirs[i * 3], dirs[i * 3 + 1], dirs[i * 3 + 2]};
		double want[3];
		brute_transmittance(&m, o, d, want);
		for (int c = 0; c < 3; ++c) {
			const int ok = fabs(out[i * 3 + c] - want[c]) <= 1e-4 + 2e-4 * want[c];
			if (!ok)
				fprintf(stderr, "ray %d channel %d: %.7f, the brute force %.7f\n", i, c, out[i * 3 + c], want[c]);
			CHECK(ok);
		}
	}
	CHECK(out[2 * 3 + 0] == 0.0f);
	CHECK(out[4 * 3 + 0] == 1.0f);
	CHECK(out[1 * 3 + 2] < out[1 * 3 + 0]); /* along the horizon, blue goes first */
	printf("rbtmathmedium_tests: straight up at sea level (%.4f, %.4f, %.4f), along the horizon 1 km up (%.4f, %.4f, %.4f)\n", out[0], out[1], out[2],
	       out[3], out[4], out[5]);
}

/* Straight up through Rayleigh's layer alone (no Mie, no absorber): the
 * optical depth's closed form, beta H (1 - e^(-60/8)). */
static void test_zenith_closed_form(void) {
	RBTMATH_SHELL_MEDIUM m = earth();
	for (int c = 0; c < 3; ++c) {
		m.mie_scattering[c]        = 0.0f;
		m.mie_extinction[c]        = 0.0f;
		m.absorption_extinction[c] = 0.0f;
	}
	const float origin[3] = {0.0f, 6360.0f, 0.0f};
	const float up[3]     = {0.0f, 1.0f, 0.0f};
	float out[3];
	CHECK(rbtmath_shell_medium_compute_transmittance(&m, origin, up, 1, 1024, out, NULL) == RBT_RESULT_OK);
	for (int c = 0; c < 3; ++c) {
		const double tau = (double)m.rayleigh_scattering[c] * 8.0 * (1.0 - exp(-60.0 / 8.0));
		CHECK(near_rel(out[c], exp(-tau), 2e-5));
	}
}

/* Each phase function integrates to 1 over the sphere; Rayleigh's is the
 * same forward and back; the Mie layer's, with g 0.8, far more forward. */
static void test_phase(void) {
	const RBTMATH_SHELL_MEDIUM m = earth();
	enum { N = 20000 };
	static float cosines[N];
	static float out[N * 2];
	for (int i = 0; i < N; ++i)
		cosines[i] = (float)(-1.0 + (i + 0.5) * 2.0 / N);
	CHECK(rbtmath_shell_medium_compute_phase(&m, cosines, N, out, NULL) == RBT_RESULT_OK);
	double ray = 0.0, mie = 0.0;
	for (int i = 0; i < N; ++i) {
		/* d(omega) = 2 pi d(cos) */
		ray += out[i * 2] * 2.0 * 3.14159265358979 * (2.0 / N);
		mie += out[i * 2 + 1] * 2.0 * 3.14159265358979 * (2.0 / N);
	}
	CHECK(near_rel(ray, 1.0, 1e-4));
	CHECK(near_rel(mie, 1.0, 2e-3));
	CHECK(out[0] == out[(N - 1) * 2]);
	CHECK(out[(N - 1) * 2 + 1] > 100.0f * out[1]);
}

/* ---- The same bits ---- */

enum { RAYS = 10000 };
static float bit_origins[RAYS * 3];
static float bit_dirs[RAYS * 3];
static float bit_alone[RAYS * 3];
static float bit_sliced[RAYS * 3];

typedef struct slice_data {
	RBTMATH_SHELL_MEDIUM medium;
	int failed;
} slice_data;

static void slice(
	uint32_t first,
	uint32_t end,
	uint32_t worker,
	void *data) {
	(void)worker;
	slice_data *s = (slice_data *)data;
	if (rbtmath_shell_medium_compute_transmittance(&s->medium, bit_origins + first * 3, bit_dirs + first * 3, end - first, 64, bit_sliced + first * 3,
	                                       NULL) != RBT_RESULT_OK)
		s->failed = 1;
}

static uint32_t crc32_of(
	const void *data,
	size_t size) {
	const unsigned char *p = (const unsigned char *)data;
	uint32_t crc           = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; ++i) {
		crc ^= p[i];
		for (int k = 0; k < 8; ++k)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

/* 10,000 rays from round the shell, worked out at once and in slices on a
 * pool of 1 and 4 threads: the same bytes. The CRC is printed for another
 * platform's run to compare (rbtmathmedium.h, "Same bits"). */
static void test_same_bits(void) {
	uint32_t seed = 12345u;
	for (int i = 0; i < RAYS * 3; ++i) {
		seed           = seed * 1664525u + 1013904223u;
		const float u  = (float)(seed >> 8) / 16777216.0f;
		bit_dirs[i]    = u * 2.0f - 1.0f;
		seed           = seed * 1664525u + 1013904223u;
		bit_origins[i] = ((float)(seed >> 8) / 16777216.0f) * 13000.0f - 6500.0f;
	}
	for (int i = 0; i < RAYS; ++i) {
		if (bit_dirs[i * 3] == 0.0f && bit_dirs[i * 3 + 1] == 0.0f && bit_dirs[i * 3 + 2] == 0.0f)
			bit_dirs[i * 3 + 1] = 1.0f;
	}
	slice_data data;
	data.medium = earth();
	data.failed = 0;
	CHECK(rbtmath_shell_medium_compute_transmittance(&data.medium, bit_origins, bit_dirs, RAYS, 64, bit_alone, NULL) == RBT_RESULT_OK);
	const uint32_t crc = crc32_of(bit_alone, sizeof(bit_alone));
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
		memset(bit_sliced, 0, sizeof(bit_sliced));
		CHECK(rbtjobs_run(&scheduler, slice, RAYS, RAYS / 4, &data) == RBT_RESULT_OK);
		CHECK(!data.failed);
		CHECK(memcmp(bit_alone, bit_sliced, sizeof(bit_alone)) == 0);
		rbtjobs_pool_destroy(pool);
	}
	printf("rbtmathmedium_tests: 10,000 rays' transmittance, CRC-32 %08x alone and on 1 and 4 threads\n", crc);
}

/* ---- Argument checks ---- */

static void test_refusals(void) {
	RBTMATH_SHELL_MEDIUM m = earth();
	const float p[3]       = {0.0f, 6361.0f, 0.0f};
	const float d[3]       = {0.0f, 1.0f, 0.0f};
	float out[3]           = {7.0f, 7.0f, 7.0f};
	RBTMATH_ERROR error;
	CHECK(rbtmath_shell_medium_compute_density(NULL, p, 1, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(strstr(error.message, "medium") != NULL);
	CHECK(rbtmath_shell_medium_compute_density(&m, NULL, 1, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(rbtmath_shell_medium_compute_density(&m, p, 1, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(rbtmath_shell_medium_compute_density(&m, NULL, 0, NULL, &error) == RBT_RESULT_OK);
	CHECK(rbtmath_shell_medium_compute_transmittance(&m, p, d, 1, 0, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(rbtmath_shell_medium_compute_transmittance(&m, p, d, 1, RBTMATH_SHELL_MEDIUM_MAX_STEPS + 1, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	CHECK(rbtmath_shell_medium_compute_transmittance(&m, p, zero, 1, 8, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(strstr(error.message, "direction 0 is 0") != NULL);
	const float bad[3] = {0.0f, NAN, 0.0f};
	CHECK(rbtmath_shell_medium_compute_density(&m, bad, 1, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(strstr(error.message, "point 0") != NULL);
	const float cosine = 1.5f;
	CHECK(rbtmath_shell_medium_compute_phase(&m, &cosine, 1, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
	CHECK(out[0] == 7.0f && out[1] == 7.0f && out[2] == 7.0f);

	/* Each field out of its range, each named. */
	struct {
		const char *name;
		RBTMATH_SHELL_MEDIUM medium;
	} broken[12];
	for (int k = 0; k < 12; ++k)
		broken[k].medium = earth();
	broken[0].name                            = "bottom_radius";
	broken[0].medium.bottom_radius            = 0.0f;
	broken[1].name                            = "top_radius";
	broken[1].medium.top_radius               = 6360.0f;
	broken[2].name                            = "rayleigh_scattering";
	broken[2].medium.rayleigh_scattering[1]   = -1.0f;
	broken[3].name                            = "rayleigh_scale_height";
	broken[3].medium.rayleigh_scale_height    = 0.0f;
	broken[4].name                            = "mie_scattering";
	broken[4].medium.mie_scattering[0]        = INFINITY;
	broken[5].name                            = "mie_extinction";
	broken[5].medium.mie_extinction[2]        = 1e-3f;
	broken[6].name                            = "mie_scale_height";
	broken[6].medium.mie_scale_height         = -1.0f;
	broken[7].name                            = "mie_anisotropy";
	broken[7].medium.mie_anisotropy           = 1.0f;
	broken[8].name                            = "absorption_extinction";
	broken[8].medium.absorption_extinction[0] = -1e-3f;
	broken[9].name                            = "absorption_center_height";
	broken[9].medium.absorption_center_height = NAN;
	broken[10].name                           = "absorption_half_width";
	broken[10].medium.absorption_half_width   = 0.0f;
	broken[11].name                           = "ground_albedo";
	broken[11].medium.ground_albedo[1]        = 1.5f;
	for (int k = 0; k < 12; ++k) {
		CHECK(rbtmath_shell_medium_compute_density(&broken[k].medium, p, 1, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
		const int named = strstr(error.message, broken[k].name) != NULL;
		if (!named)
			fprintf(stderr, "the message '%s' doesn't name %s\n", error.message, broken[k].name);
		CHECK(named);
	}
	CHECK(rbtmath_shell_medium_compute_density(&m, p, 1, out, &error) == RBT_RESULT_OK);
	CHECK(error.message[0] == '\0');
}

int main(void) {
	test_density();
	test_transmittance();
	test_zenith_closed_form();
	test_phase();
	test_same_bits();
	test_refusals();
	if (failures) {
		printf("rbtmathmedium_tests: %d failure(s)\n", failures);
		return 1;
	}
	printf("rbtmathmedium_tests: all passed\n");
	return 0;
}
