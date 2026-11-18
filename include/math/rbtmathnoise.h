#ifndef RBTMATHNOISE_H
#define RBTMATHNOISE_H

/**
 * @file rbtmathnoise.h
 * @brief Rabbittus Math's coherent noise: gradient and simplex noise,
 * their fractal sums, cellular noise and its nearest features, and domain
 * warping, in 2D and 3D, evaluated at many places a call.
 *
 * Built in src/math/rbtmathnoise.cpp, with headless tests in
 * tests/rbtmathnoise_tests.c and tests/rbtmathnoise_bits_tests.c. Not part
 * of rbtrend.h, which is Rend's alone.
 *
 * A noise is described by an RBTMATH_NOISE_DESC: a kind (gradient,
 * simplex or cellular), a fractal sum of octaves, a seed, a frequency in
 * cells per unit of the caller's places, and the kind's parameters. It is
 * not an object: nothing is created, and the same desc may be used by any
 * number of calls at once. Places are in the caller's unit, whatever it
 * is, in doubles, to RBTMATH_NOISE_MAX_CELLS cells of the finest octave
 * from the origin; the noise is as exact far out as at the origin.
 *
 * General purpose: nothing here knows what a value becomes (a height, a
 * density, a colour, a moisture, a shake) or what a cell is (a crater, a
 * region, a rock).
 *
 * Out of scope: evaluating on the GPU (drawing's); 1D and 4D noise; value
 * noise; band-limited noise; composing layers (the caller's arithmetic
 * over these calls); erosion by simulation.
 *
 * @par Definition
 * Every call's results are defined below, enough for an independent copy
 * (another language, a GPU's) to reproduce them; a GPU copy is checked to
 * a tolerance, not bits, since GPUs may fuse a multiply and an add. A
 * kind's results never change once released: a new algorithm is a new
 * kind. Steps 1 to 10 make each octave's lattice coordinate; the
 * evaluation after them is written out in full.
 * @verbatim
 * 1. H(x), for a 32-bit unsigned x: Wellons' lowbias32 (hash-prospector,
 *    public domain): x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15;
 *    x *= 0x846ca68b; x ^= x >> 16. All arithmetic modulo 2^32.
 * 2. Octave seeds: s_i = H(seed ^ X_i), X_i = H(0x9E3779B9 * (i + 1))
 *    (modulo 2^32). A xor with a fixed hash, not an add, so a caller that
 *    makes its own seeds by adding any constant (a world's index times a
 *    step) never lands one seed's octave i on another's octave j. A warp's
 *    channel a (0, 1, 2) evaluates its field with the seed field.seed ^ Y_a,
 *    Y_a = H(0x632BE5AB * (a + 1)). No X_i ^ Y_a equals another for i < 32
 *    and a < 3 (with Y absent too), nor equals one xor FLIP (step 8):
 *    checked, and held by the bits test.
 * 3. Octave frequencies, in double: f_0 = frequency, f_i = f_{i-1} *
 *    lacunarity.
 * 4. Octave offsets, in cells, per axis a = 0, 1, 2:
 *    o_i[a] = (H(s_i ^ A[a]) >> 8) * 2^-24, with A = { 0x68E31DA4,
 *    0xB5297A4D, 0x1B56C4E9 }: each in [0, 1), exact.
 * 5. u[a] = p[a] * f_i + o_i[a], in double, the product rounded before
 *    the sum.
 * 6. Simplex only, in double (the constants below): 2D, the skew
 *    s = (u0 + u1) * F2, u[a] = u[a] + s; 3D, the orientation's turn
 *    (THE TURN, below).
 * 7. The split: c[a] = floor(u[a]) as int64, t[a] = floor((u[a] - c[a])
 *    * 2^24) * 2^-24, exact in double, then exact as float: a multiple of
 *    2^-24 in [0, 1).
 * 8. A lattice point's hash: h = H(s ^ P0(c0) ^ P1(c1) [^ P2(c2)]), with
 *    Pa(c) = ((uint32)c * K[a]) ^ ((uint32)((uint64)c >> 32) * K'[a]),
 *    K = { 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D }, K' = { 0x632BE5AB,
 *    0x27D4EB2F, 0x165667B1 } (odd constants: the golden ratio's and
 *    xxHash's); s is s_i, but for 3D simplex's second lattice s_i ^ FLIP,
 *    FLIP = 0xCC9E2D51 (MurmurHash3's). A period wraps c into
 *    [0, period) first (the Euclidean remainder, so -1 becomes
 *    period - 1), by the octave's period, period * lacunarity^i.
 * 9. A gradient: 3D gradient noise, h >> 28 into Perlin's sixteen; 2D
 *    both kinds, (h >> 8) % 24 into the 24 directions; 3D simplex,
 *    (h >> 8) % 48 into the 48 (TABLES, below). A cellular
 *    feature's place in its cell, per axis b = 0, 1, 2:
 *    0.5 + jitter * (((h >> (10 * b)) & 1023) * 2^-10 - 0.5), in float,
 *    the product and the sum each rounded. A feature's id:
 *    id = H(h ^ 0x5BD1E995). A cell's value: (id >> 8) * 2^-23 - 1, in
 *    [-1, 1), exact.
 * 10. Everything after step 7 in float (binary32, each operation rounded
 *    once: FLT_EVAL_METHOD 0), as written below, with no
 *    contraction: each kind's octave, its scale constant applied last,
 *    then clamped to its range (its gradient 0 where the clamp acts), then
 *    the fractal's sum in octave order from +0. Scale constants are found
 *    by search for the octave's maximum (as OpenSimplex2's were), recorded
 *    with the search's program, so the clamp never acts in 10^9 measured
 *    samples per kind and dimension; it is there so the range is a
 *    guarantee, not a measurement.
 *
 * Enough for an independent copy to give the same bits: every float
 * operation is binary32, rounded once, in the order the parentheses give
 * (a sum of three is ((x + y) + z)); constants are hex literals, each the
 * nearest double (D) or float (F) to the exact value named. Per octave i:
 * the split (c, t) of step 7, the seed s of step 8, F_i = (float)f_i.
 *
 * CONSTANTS.
 *   F2   = 0x1.76cf5d0b09954p-2  (D, (sqrt 3 - 1) / 2: 2D skew)
 *   G    = -0x1.b0cb18p-3        (F, -(3 - sqrt 3) / 6: 2D unskew; also
 *                                 the Y_UP and Z_UP turns' term, as F)
 *   GD   = -0x1.b0cb174df99c8p-3 (D, the same, for the turns in double)
 *   E1   = 0x1.93cd3ap-1         (F, 1 + G)
 *   E11  = 0x1.279a74p-1         (F, 1 + 2 G)
 *   T23  = 0x1.5555555555555p-1  (D, 2/3);  T23F = 0x1.555556p-1 (F)
 *   R3   = 0x1.279a74590331dp-1  (D, 1 / sqrt 3); R3F = 0x1.279a74p-1 (F)
 *   K2   = 0x1p-1 (F, 0.5);  K3 = 0x1.333334p-1 (F, 0.6): the kernels
 *   SCALE_GRADIENT_2D = 0x1.6cbdaap+0  (F, 1 / 0.70186810806957944)
 *   SCALE_GRADIENT_3D = 0x1.ee0a2ep-1  (F, 1 / 1.0363538112118027)
 *   SCALE_SIMPLEX_2D  = 0x1.8f58eep+6  (F, 1 / 0.010016341581612447)
 *   SCALE_SIMPLEX_3D  = 0x1.91838ap+3  (F, 1 / 0.07969837445285613)
 *   Each the float just below 1 over its octave's maximum, found by
 *   search (tests/rbtmathnoise_scale_search.cpp): each lattice point's
 *   gradient is hashed independently and every weight (a lerp's, a
 *   kernel's) is never negative, so the maximum at a place is the weights
 *   times each point's largest dot, searched over a cell.
 *
 * TABLES (index order).
 *   Perlin's sixteen (3D gradient noise), as his reference grad(): index
 *   0 to 15: (1,1,0) (-1,1,0) (1,-1,0) (-1,-1,0) (1,0,1) (-1,0,1)
 *   (1,0,-1) (-1,0,-1) (0,1,1) (0,-1,1) (0,1,-1) (0,-1,-1) (1,1,0)
 *   (0,-1,1) (-1,1,0) (0,-1,-1). Its dot with d is one sum of two signed
 *   components: (+-d_u) + (+-d_v), the two non-zero axes in axis order.
 *   The 24 directions (2D, both kinds): index k is the angle
 *   (7.5 + 15 k) degrees from +x towards +y, (cos, sin), each component
 *   +- one of the floats nearest sin 7.5 = 0x1.0b515p-3, sin 22.5 =
 *   0x1.87de2ap-2, sin 37.5 = 0x1.37af94p-1, sin 52.5 = 0x1.963268p-1,
 *   sin 67.5 = 0x1.d906bcp-1, sin 82.5 = 0x1.fb9eaap-1 (cos x = sin(90 - x)).
 *   The 48 (3D simplex), OpenSimplex2's set, with A = 0x1.1cc47p+1
 *   (1 + sqrt 1.5), B = 0x1.8b0ac8p+1 and C = 0x1.2c121cp+0 (OpenSimplex2's
 *   3.0862664687972017 and 1.1721513422464978), all of length about 3.30:
 *   index k = 0 to 23: the +-1 on axis m = k / 8, the other two axes in
 *   increasing order +-A, signs by bit 0 (the 1's), bit 1 (the first A's),
 *   bit 2 (the second A's) of k % 8, a set bit negative; k = 24 to 47, j =
 *   k - 24: 0 on axis m = j / 8, of the other two axes the lower takes B
 *   if bit 0 of j % 8 is clear, else C, the higher the other; bit 1 the
 *   lower's sign, bit 2 the higher's. Its dot: ((g0 d0) + (g1 d1)) + (g2 d2).
 *
 * THE TURN (3D simplex, in double, on u after step 5):
 *   EVEN:  r = ((u0 + u1) + u2) * T23; u'[a] = r - u[a].
 *   Z_UP:  w = u0 + u1; s = w * GD; z = u2 * R3;
 *          u'0 = (u0 + s) + z; u'1 = (u1 + s) + z; u'2 = (w * -R3) + z.
 *   Y_UP:  w = u0 + u2; s = w * GD; y = u1 * R3;
 *          u'0 = (u0 + s) + y; u'2 = (u2 + s) + y; u'1 = (w * -R3) + y.
 *   Each orthogonal; a gradient found in the turned frame, g', comes back
 *   by the transpose, in float: EVEN: r = ((g'0 + g'1) + g'2) * T23F,
 *   g[a] = r - g'[a]. Z_UP: w = g'0 + g'1; s = w * G; z = g'2 * R3F;
 *   g0 = (g'0 + s) - z; g1 = (g'1 + s) - z; g2 = (w * R3F) + z. Y_UP: w =
 *   g'0 + g'2; s = w * G; y = g'1 * R3F; g0 = (g'0 + s) - y; g2 =
 *   (g'2 + s) - y; g1 = (w * R3F) + y.
 *
 * A KERNEL TERM (both simplex kinds), for a lattice point at offset d
 * from the place with gradient q (the table's vector) and kernel K:
 *   a = ((K - (d0 d0)) - (d1 d1)) [- (d2 d2)]; only where a > 0:
 *   p = q . d; w = (a a) (a a); value += w p;
 *   m = ((a a) a) (p * -8); grad[x] += (w q_x) + (m d_x).
 * Every sum (a value, each gradient axis) starts at +0 and adds terms in
 * the order given; a point with a <= 0 adds nothing.
 *
 * SIMPLEX 2D. tau = (t0 + t1) * G; e0 = t0 + tau; e1 = t1 + tau.
 *   Points, in order, with their cells and offsets d:
 *   (c0, c1): (e0, e1);
 *   if e1 > e0, (c0, c1 + 1): (e0 - G, e1 - E1), else (c0 + 1, c1):
 *   (e0 - E1, e1 - G);
 *   (c0 + 1, c1 + 1): (e0 - E11, e1 - E11). Kernel K2. The three corners of
 *   the place's triangle; K2 is the triangle's height squared, so no point
 *   outside it reaches the place: exact. The gradient needs no turn back
 *   (the offsets are already the place's own axes).
 *
 * SIMPLEX 3D. Two cubes: lattice 0, (c, t) of the turned u'; lattice 1,
 *   the split of u' - 0.5 (from (c, t) exactly: t - 0.5 with c where
 *   t >= 0.5, else t + 0.5 with c - 1), its seed s_i ^ FLIP. For each, in
 *   that order, its 8 corners k in order (x fastest: (0,0,0), (1,0,0),
 *   (0,1,0), ... (1,1,1)): cell c + k, d = t - k, kernel K3. Every point of
 *   the body-centred lattice within the kernel is one of these 16
 *   (checked against a search of every point within 3 cells), at most 6
 *   at once. Gradient turned back as above.
 *
 * GRADIENT NOISE. Corners k in order (x fastest), d = t - k (exact),
 *   q_k their dots. Fade s(t) = ((t t) t) ((t ((t * 6) - 15)) + 10);
 *   its slope s'(t) = ((t t) * 30) ((t (t - 2)) + 1).
 *   lerp(A, B, s) = A + (s (B - A)), first along x (pairs differing in x),
 *   then y, then z: 2D l0 = lerp(q00, q10, sx), l1 = lerp(q01, q11, sx),
 *   n = lerp(l0, l1, sy); 3D the four x-lerps (yz = 00, 10, 01, 11), the
 *   two y-lerps (z = 0, 1), the z-lerp. Its gradient follows the same
 *   tree: a corner's is its table vector; a lerp's, along its own axis,
 *   (dA + (s (dB - dA))) + (s' (B - A)), along another, dA + (s (dB - dA)).
 *
 * CELLULAR. For each cell c + delta, delta in -2..2 per axis (5^d; a
 *   search may skip a cell it proves farther than F2: the result is the
 *   same), its feature's place f (step 9) and e[b] = ((float)delta[b] +
 *   f[b]) - t[b]; distance EUCLIDEAN sqrt(((e0 e0) + (e1 e1)) [+ (e2 e2)])
 *   (float sqrt, correctly rounded), MANHATTAN (|e0| + |e1|) [+ |e2|],
 *   CHEBYSHEV the largest |e_b|. F1, F2: the two smallest distances,
 *   ties to the earlier cell (z, then y, then x, ascending). F2 - F1 one
 *   subtraction. Gradient, per cell, of a feature at distance F:
 *   EUCLIDEAN (-e[b]) / F, MANHATTAN -sign(e[b]), CHEBYSHEV -sign(e[m])
 *   on the largest axis m (ties to the lower axis), 0 elsewhere; all 0
 *   where F is 0. No scale constant: the ranges are exact.
 *
 * AN OCTAVE'S VALUE: n = raw * SCALE, its per-cell gradient g = raw' *
 *   SCALE (none for cellular), then n clamped to its range (g 0 where it
 *   acts); per caller unit, gamma[x] = g[x] * F_i.
 *
 * FRACTALS, octave i in order, amplitude a_0 = 1, a_i = a_{i-1} * gain
 *   (float), v and G (each axis) from +0:
 *   FBM:    v = v + (a_i n); G = G + (a_i gamma).
 *   RIDGED: w_0 = 1, dw_0 = 0. sg = sign(n), 0 where |n| < 2^-100;
 *           r = 1 - |n|; t = (r r) w; v = v + (a_i t);
 *           dt = ((((r * -2) sg) w) gamma) + ((r r) dw);
 *           G = G + (a_i dt); x = t * ridge_feedback: above 1, w = 1 and
 *           dw = 0; else w = x and dw = dt * ridge_feedback (x is never
 *           below 0).
 *   ERODED: D = D + g (per cell, D from +0); q = ((D0 D0) + (D1 D1))
 *           [+ (D2 D2)]; v = v + ((a_i n) / (1 + (erosion q))). No G.
 *
 * The scale constants above, like everything here, are frozen with the
 * kinds.
 *
 * The calls, written out (what the steps above leave to a call):
 * - A 2D dot, of a gradient q with an offset d: (q0 d0) + (q1 d1).
 * - rbtmath_noise_compute_offsets(): the place origin[a] + (double)
 *   offset[a], one rounding per axis, then as a point.
 * - rbtmath_noise_compute_grid(): the place origin[a] + ((double)i * step[a]),
 *   the product rounded, then the sum, then as a point.
 * - rbtmath_noise_warp_points(): channel a's value v_a (the field with seed
 *   field.seed ^ Y_a, every other field the same), then p'[a] = p[a] +
 *   (amplitude * (double)v_a), in double; its Jacobian
 *   J[a][b] = (float)(I[a][b] + (amplitude * (double)G_a[b])), I the
 *   identity, G_a channel a's gradient per unit of the places.
 * - rbtmath_noise_compute_nearest_cells(): F1 and F2 as CELLULAR's search
 *   finds them, ties to the earlier cell; in the caller's unit, F / F_0 and
 *   e[b] / F_0 (float divisions, F_0 = (float)frequency); the ids of the
 *   two features (step 9).
 * - The range a place is checked against: |p[a]| * f_(n-1) at most
 *   RBTMATH_NOISE_MAX_CELLS on every axis, in double, f_(n-1) the finest
 *   octave's frequency; for a warp, (|p[a]| + (amplitude * M)) * f_(n-1),
 *   M the largest magnitude of the field's range (its octave's range times
 *   S, the sum of its amplitudes in double).
 * @endverbatim
 *
 * @par Same bits
 * Every call gives the same bits on every platform for the same input,
 * under IEEE 754's default environment: the source is built without
 * floating-point contraction, its only libm calls are floor and sqrt, and
 * its order of operations doesn't depend on how many places a call has
 * or where a batch is split. A thread that flushes subnormals to zero may
 * see a value or a gradient change by less than 2^-100.
 *
 * @par Threads
 * Every call reads only its arguments and writes only its outputs, so any
 * of them may run at once, from any thread, on any data, the same desc
 * included; each call holds nothing between calls. Every array is read or
 * written only during the call. No output may overlap an input or
 * another output, except rbtmath_noise_warp_points()'s places moved in
 * place.
 *
 * @par Results
 * Calls return RBT_RESULT (rbtcore.h): RBT_RESULT_OK; or
 * RBT_RESULT_STRUCT_WRONG_TYPE, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN,
 * RBT_RESULT_INVALID_ARGUMENT or RBT_RESULT_UNSUPPORTED (gradients or
 * Jacobians of an ERODED noise), with the reason in the RBTMATH_ERROR the
 * caller passes (NULL to skip it), nothing written. A desc is checked
 * field by field, in the struct's order, each its own check and message,
 * then the other arguments in parameter order, then every place, before
 * anything is written.
 *
 * @par Allocation
 * None: every call writes only into the caller's arrays and its own
 * stack.
 */

#include "../rbtcore.h"
#include "rbtmatherror.h"

#include <stdint.h>

#if __cplusplus
extern "C" {
#endif

/** @brief The largest |coordinate| x frequency of the finest octave a
 * place may have on any axis, in cells: 2^36. */
#define RBTMATH_NOISE_MAX_CELLS 68719476736.0

/** @brief The most octaves a desc may sum. */
#define RBTMATH_NOISE_MAX_OCTAVES 32u

/** @brief The kind of a noise's octaves. */
typedef enum RBTMATH_NOISE_KIND RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_NOISE_KIND_UNKNOWN = 0,
	/** Perlin's improved gradient noise: square or cubic cells, a quintic
	 * fade. One octave in [-1, 1]. Tiles by RBTMATH_NOISE_DESC::period. */
	RBTMATH_NOISE_KIND_GRADIENT = 1,
	/** OpenSimplex2: the triangular lattice in 2D, the body-centred cubic
	 * lattice in 3D, turned by RBTMATH_NOISE_DESC::orientation. One octave
	 * in [-1, 1]. Doesn't tile. */
	RBTMATH_NOISE_KIND_SIMPLEX = 2,
	/** Worley's cellular noise: one feature per cell, the distances to the
	 * nearest two. Its value by RBTMATH_NOISE_DESC::cell_return. Tiles by
	 * RBTMATH_NOISE_DESC::period. */
	RBTMATH_NOISE_KIND_CELLULAR = 3,
} RBTMATH_NOISE_KIND;

/** @brief How a noise's octaves are summed. */
typedef enum RBTMATH_NOISE_FRACTAL RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_NOISE_FRACTAL_UNKNOWN = 0,
	/** One octave (RBTMATH_NOISE_DESC::octaves 1). */
	RBTMATH_NOISE_FRACTAL_SINGLE = 1,
	/** Fractional Brownian motion: sum(a_i n_i), a_0 = 1,
	 * a_i = a_{i-1} * gain; in the kind's range times S = sum(a_i). */
	RBTMATH_NOISE_FRACTAL_FBM = 2,
	/** Musgrave's ridged multifractal, one octave in [0, 1], 1 on a crest:
	 * t_i = (1 - |n_i|)^2 w_i, w_0 = 1,
	 * w_{i+1} = clamp(t_i * ridge_feedback, 0, 1), sum(a_i t_i), in
	 * [0, S]. Gradient and simplex kinds only. */
	RBTMATH_NOISE_FRACTAL_RIDGED = 3,
	/** Derivative-damped fBm: sum(a_i n_i / (1 + erosion |D_i|^2)), D_i
	 * the sum of octaves 0 to i's gradients in each octave's own cells; in
	 * [-S, S]. Gradient and simplex kinds only; no gradient output. */
	RBTMATH_NOISE_FRACTAL_ERODED = 4,
} RBTMATH_NOISE_FRACTAL;

/** @brief How cellular noise measures the distance to a feature. */
typedef enum RBTMATH_NOISE_CELL_DISTANCE RBT_ENUM_BASE {
	/** Not chosen: refused (with a cellular kind). */
	RBTMATH_NOISE_CELL_DISTANCE_UNKNOWN = 0,
	/** Straight-line distance: round cells. */
	RBTMATH_NOISE_CELL_DISTANCE_EUCLIDEAN = 1,
	/** The sum of the axes' distances: diamond-edged cells. */
	RBTMATH_NOISE_CELL_DISTANCE_MANHATTAN = 2,
	/** The largest axis's distance: square-edged cells. */
	RBTMATH_NOISE_CELL_DISTANCE_CHEBYSHEV = 3,
} RBTMATH_NOISE_CELL_DISTANCE;

/** @brief What a cellular noise's octave gives, in cells of the octave.
 * D below is a cell's diagonal in the distance (sqrt(dimensions)
 * Euclidean, dimensions Manhattan, 1 Chebyshev). */
typedef enum RBTMATH_NOISE_CELL_RETURN RBT_ENUM_BASE {
	/** Not chosen: refused (with a cellular kind). */
	RBTMATH_NOISE_CELL_RETURN_UNKNOWN = 0,
	/** The nearest feature's distance, F1: 0 to D. */
	RBTMATH_NOISE_CELL_RETURN_F1 = 1,
	/** The second nearest's, F2: F1 to 2 D. */
	RBTMATH_NOISE_CELL_RETURN_F2 = 2,
	/** F2 - F1, 0 on the cells' borders: 0 to 2 D. */
	RBTMATH_NOISE_CELL_RETURN_F2_MINUS_F1 = 3,
	/** The nearest feature's value, the same over its cell: -1 to 1. Its
	 * gradient is 0. */
	RBTMATH_NOISE_CELL_RETURN_CELL_VALUE = 4,
} RBTMATH_NOISE_CELL_RETURN;

/** @brief How 3D simplex noise's lattice is turned against the places'
 * axes (the Definition's turns). */
typedef enum RBTMATH_NOISE_ORIENTATION RBT_ENUM_BASE {
	/** Not chosen: refused with 3D simplex; must be this for every other
	 * kind and for 2D. */
	RBTMATH_NOISE_ORIENTATION_UNKNOWN = 0,
	/** No preferred direction: a sphere's surface, a volume. */
	RBTMATH_NOISE_ORIENTATION_EVEN = 1,
	/** y is up: slices across y (the x-z planes) look their best. */
	RBTMATH_NOISE_ORIENTATION_Y_UP = 2,
	/** z is up: slices across z (the x-y planes) look their best. */
	RBTMATH_NOISE_ORIENTATION_Z_UP = 3,
} RBTMATH_NOISE_ORIENTATION;

/** @brief A noise: what it is made of and how its octaves sum. Only read
 * during a call. Fields marked "Read by" must be 0 where not read. */
typedef struct RBTMATH_NOISE_DESC {
	/** RBT_STRUCT_TYPE_MATH_NOISE_DESC; `next` NULL (nothing chains on
	 * yet). */
	RBT_STRUCT_BASE base;
	/** 2 or 3: how many coordinates a place has. */
	uint32_t dimensions;
	/** The octaves' kind. */
	RBTMATH_NOISE_KIND kind;
	/** How octaves sum. */
	RBTMATH_NOISE_FRACTAL fractal;
	/** Any value; each seed is a different noise. */
	uint32_t seed;
	/** 1 with RBTMATH_NOISE_FRACTAL_SINGLE; else 1 to
	 * RBTMATH_NOISE_MAX_OCTAVES. */
	uint32_t octaves;
	/** The first octave's cells per unit of the caller's places: finite,
	 * above 0. */
	double frequency;
	/** Each octave's frequency over the one before: finite, above 1, with
	 * the last octave's frequency (#frequency x lacunarity^(octaves - 1))
	 * finite, checked on its own; a whole number where any #period is set.
	 * Read by every fractal but SINGLE. */
	double lacunarity;
	/** Each octave's amplitude over the one before: finite, above 0. Read
	 * by every fractal but SINGLE. */
	float gain;
	/** How much a ridge's strength carries into the next octave's weight:
	 * finite, above 0 (libnoise's 2 is usual). Read by RIDGED. */
	float ridge_feedback;
	/** How strongly slope damps finer octaves: finite, above 0 (1 is
	 * Quilez's). Read by ERODED. */
	float erosion;
	/** Read by CELLULAR. */
	RBTMATH_NOISE_CELL_DISTANCE cell_distance;
	/** Read by CELLULAR in the compute calls; must be 0 for
	 * rbtmath_noise_compute_nearest_cells(), which gives every return. */
	RBTMATH_NOISE_CELL_RETURN cell_return;
	/** How far a feature may stray from its cell's centre, as a fraction
	 * of the cell: finite, above 0, at most 1. Read by CELLULAR. */
	float cell_jitter;
	/** The lattice's turn. Read by SIMPLEX with #dimensions 3 only. */
	RBTMATH_NOISE_ORIENTATION orientation;
	/** Per axis, the first octave's period in cells (octave i's is
	 * period * lacunarity^i), or 0 for none; at most RBTMATH_NOISE_MAX_CELLS
	 * at the last octave; the third 0 in 2D. Read by GRADIENT and
	 * CELLULAR; all 0 for SIMPLEX. */
	uint32_t period[3];
} RBTMATH_NOISE_DESC;

/** @brief A regular grid of places: x fastest, then y, then z. */
/* Frozen: a value. It never changes; a new need gets a new type. */
typedef struct RBTMATH_NOISE_GRID {
	/** The first place, each finite; the third 0 in 2D. */
	double origin[3];
	/** The step between places along each axis, each finite (any sign, 0
	 * allowed); the third 0 in 2D. */
	double step[3];
	/** Places along each axis, each at least 1; the third 1 in 2D; their
	 * product at most UINT32_MAX. */
	uint32_t count[3];
} RBTMATH_NOISE_GRID;

/** @brief A domain warp: places moved by a vector of noise. Only read
 * during a call. */
typedef struct RBTMATH_NOISE_WARP_DESC {
	/** RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC; `next` NULL. */
	RBT_STRUCT_BASE base;
	/** The field each axis's displacement is drawn from; channel a
	 * evaluates it with the seed field->seed ^ Y_a (the Definition, step 2).
	 * Any kind and fractal a compute call takes. Only read during the
	 * call. */
	const RBTMATH_NOISE_DESC *field;
	/** The displacement for a value of 1, in the caller's unit: finite,
	 * above 0. */
	double amplitude;
} RBTMATH_NOISE_WARP_DESC;

/**
 * @brief The noise at each place, and its gradient where asked.
 *
 * @param[in]  desc          The noise. Only read during the call.
 * @param[in]  points        @p count places, desc->dimensions doubles
 *                           each, each finite, within
 *                           RBTMATH_NOISE_MAX_CELLS cells of the finest
 *                           octave. NULL with a count of 0. Only read during
 *                           the call.
 * @param[in]  count         How many places; 0 writes nothing.
 * @param[out] out_values    @p count floats, in the range the kind and
 *                           fractal state.
 * @param[out] out_gradients desc->dimensions x @p count floats, each
 *                           place's derivative by each axis per unit of
 *                           the places; NULL to skip (it then costs
 *                           nothing).
 * @param[out] error         Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_STRUCT_WRONG_TYPE or
 *         RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN for the desc's base;
 *         RBT_RESULT_INVALID_ARGUMENT for a NULL desc, a NULL array with a
 *         count above 0, a desc field out of its range or set where unread,
 *         or a place not finite or past range (its index named);
 *         RBT_RESULT_UNSUPPORTED for gradients of an ERODED noise. Nothing
 *         is written on failure.
 */
RBT_RESULT rbtmath_noise_compute_points(
	const RBTMATH_NOISE_DESC *desc,
	const double *points,
	uint32_t count,
	float *out_values,
	float *out_gradients,
	RBTMATH_ERROR *error);

/**
 * @brief The noise at origin + each offset (the sum in double, one
 * rounding per axis): the bits rbtmath_noise_compute_points() gives at
 * those places.
 *
 * @param[in]  desc          The noise. Only read during the call.
 * @param[in]  origin        desc->dimensions doubles, each finite.
 * @param[in]  offsets       @p count offsets, desc->dimensions floats
 *                           each, each finite; every origin + offset
 *                           within RBTMATH_NOISE_MAX_CELLS.
 * @param[in]  count         How many places; 0 writes nothing.
 * @param[out] out_values    @p count floats.
 * @param[out] out_gradients desc->dimensions x @p count floats, or NULL.
 * @param[out] error         Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_noise_compute_points(), and
 *         RBT_RESULT_INVALID_ARGUMENT for a NULL or non-finite origin.
 */
RBT_RESULT rbtmath_noise_compute_offsets(
	const RBTMATH_NOISE_DESC *desc,
	const double *origin,
	const float *offsets,
	uint32_t count,
	float *out_values,
	float *out_gradients,
	RBTMATH_ERROR *error);

/**
 * @brief The noise at every place of a grid, x fastest: the bits
 * rbtmath_noise_compute_points() gives at origin[a] + (double)i * step[a].
 *
 * @param[in]  desc          The noise. Only read during the call.
 * @param[in]  grid          The grid; both corners within
 *                           RBTMATH_NOISE_MAX_CELLS.
 * @param[out] out_values    count[0] x count[1] x count[2] floats.
 * @param[out] out_gradients desc->dimensions times as many, or NULL.
 * @param[out] error         Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_noise_compute_points(), and
 *         RBT_RESULT_INVALID_ARGUMENT for a NULL grid or a grid field out
 *         of its range.
 */
RBT_RESULT rbtmath_noise_compute_grid(
	const RBTMATH_NOISE_DESC *desc,
	const RBTMATH_NOISE_GRID *grid,
	float *out_values,
	float *out_gradients,
	RBTMATH_ERROR *error);

/**
 * @brief Each place moved by the warp: p + amplitude (V_0(p), V_1(p)
 * [, V_2(p)]), each V_a the warp's field with channel a's seed, the sum
 * in double; and where asked, the move's Jacobian.
 *
 * @param[in]  desc          The warp. Only read during the call.
 * @param[in]  points        @p count places, field->dimensions doubles
 *                           each; as rbtmath_noise_compute_points() takes.
 * @param[in]  count         How many; 0 writes nothing.
 * @param[out] out_points    @p count moved places; may be @p points itself
 *                           (moved in place), never partly overlapping it.
 * @param[out] out_jacobians dimensions^2 x @p count floats, row-major,
 *                           d out / d in per place (the identity plus
 *                           amplitude times the channels' gradients); NULL
 *                           to skip.
 * @param[out] error         Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_noise_compute_points() for the field (ERODED refused
 *         with Jacobians), and RBT_RESULT_INVALID_ARGUMENT for a NULL
 *         field or an amplitude out of range, or a place that the warp
 *         could move past range: each place is checked, before anything is
 *         written, at |p| + amplitude x the largest magnitude of the field's
 *         range on every axis, so a place refused may be one the field
 *         would not in fact have moved that far (its index named). Nothing
 *         written on failure, in place included.
 */
RBT_RESULT rbtmath_noise_warp_points(
	const RBTMATH_NOISE_WARP_DESC *desc,
	const double *points,
	uint32_t count,
	double *out_points,
	float *out_jacobians,
	RBTMATH_ERROR *error);

/**
 * @brief Each place's two nearest features of a cellular noise: exact at
 * any jitter, ties to the earlier cell.
 *
 * The desc is RBTMATH_NOISE_KIND_CELLULAR, RBTMATH_NOISE_FRACTAL_SINGLE,
 * RBTMATH_NOISE_DESC::cell_return 0.
 *
 * @param[in]  desc          The noise. Only read during the call.
 * @param[in]  points        @p count places, as
 *                           rbtmath_noise_compute_points() takes.
 * @param[in]  count         How many; 0 writes nothing.
 * @param[out] out_distances 2 x @p count floats, F1 then F2 per place, in
 *                           the caller's unit (cells over the frequency);
 *                           NULL to skip.
 * @param[out] out_ids       2 x @p count ids, the nearest's then the
 *                           second's: 32 bits each, uniform, the same for
 *                           a feature from any place; NULL to skip.
 * @param[out] out_offsets   2 x dimensions x @p count floats, from the
 *                           place to each feature, in the caller's unit;
 *                           NULL to skip.
 * @param[out] error         Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_noise_compute_points(), and
 *         RBT_RESULT_INVALID_ARGUMENT for a desc not as above or all three
 *         outputs NULL with a count above 0.
 */
RBT_RESULT rbtmath_noise_compute_nearest_cells(
	const RBTMATH_NOISE_DESC *desc,
	const double *points,
	uint32_t count,
	float *out_distances,
	uint32_t *out_ids,
	float *out_offsets,
	RBTMATH_ERROR *error);

#if __cplusplus
}
#endif

#endif // RBTMATHNOISE_H
