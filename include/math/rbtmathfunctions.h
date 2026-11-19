#ifndef RBTMATHFUNCTIONS_H
#define RBTMATHFUNCTIONS_H

/**
 * @file rbtmathfunctions.h
 * @brief Rabbittus Math's elementary functions with the same bits on every
 * platform: tan, atan, atan2, exp2, exp and log2 in double precision,
 * over arrays.
 *
 * Built in src/math/rbtmathfunctions.cpp, with headless tests in
 * tests/rbtmathfunctions_tests.c and tests/rbtmathfunctions_bits_tests.c.
 * Not part of rbtrend.h, which is Rend's alone.
 *
 * Each function is defined by its evaluation, written below, not by the
 * platform's libm, whose results differ between platforms in their last
 * bits. Each result is within 1 ulp of the exact value (measured, not
 * proven). Values are never refused: special values give IEEE 754's and
 * C's Annex F answers, as below.
 *
 * General purpose: nothing here knows what an angle or a power is for.
 *
 * Out of scope: float versions; sin, cos and the other inverse functions;
 * ln and log10 (log2(x) times the double nearest ln 2 or log10 2,
 * composed by the caller, within 1.6 ulps);
 * pow (x^y = exp2(y log2 x) for x > 0, composed by the caller, its error
 * growing with |y log2 x|); a GPU's copy.
 *
 * @par Same bits
 * Every call gives the same bits on every platform for the same input, in
 * the default floating-point environment (round to nearest, no trap
 * enabled). With the rounding mode changed the results are undefined. On
 * a thread that flushes subnormals to zero, exp2, exp, log2 and atan2
 * read a subnormal input as a zero of its sign and give a zero of its
 * sign where the result would be subnormal: log2 of a subnormal input is
 * -infinity, and atan2 with a subnormal input gives the angle for that
 * zero (0, +-pi/2 or +-pi), which is within an ulp of the default's only
 * where the other argument is far larger; tan and atan return a subnormal
 * input as it came; nothing else changes (measured on arm64; x86's FTZ and
 * DAZ owed). The status flags after a call are unspecified. A function's
 * results never change once released.
 *
 * @par Definition
 * Every call's results are defined below, enough for an independent copy
 * (another language, a GPU's) to give the same bits; a GPU copy is checked
 * to a tolerance, not bits, since GPUs may fuse a multiply and an add.
 * Every NaN written is the one below (QNAN), whatever NaN came in.
 * @verbatim
 * Every operation is one IEEE 754 binary64 operation, rounded to nearest
 * (ties to even), in the order the parentheses give; a + b + c is
 * ((a + b) + c). bits(v) is v's 64-bit encoding, from(u) the double
 * encoded by u. Constants are hex literals, each the nearest double to the
 * value named unless a number of significant bits is named.
 *
 * CONSTANTS.
 *   SHIFT       = 0x1.8p52
 *   QNAN        = from(0x7FF8000000000000)
 *   SPLITTER    = 0x1.0000002p+27              (2^27 + 1)
 *   LOG2E       = 0x1.71547652b82fep+0         (1 / ln 2)
 *   LN2_HI      = 0x1.62e42ffp-1               (ln 2 to 32 bits)
 *   LN2_LO      = -0x1.718432a1b0e26p-35       (ln 2 - LN2_HI)
 *   IVLN2_HI    = 0x1.71547652p+0              (1 / ln 2 to 32 bits)
 *   IVLN2_LO    = 0x1.705fc2eefa2p-33          (1 / ln 2 - IVLN2_HI)
 *   TWO_OVER_PI = 0x1.45f306dc9c883p-1         (2 / pi)
 *   PIO2_1      = 0x1.921fb544p+0              (pi/2 to 33 bits)
 *   PIO2_2      = 0x1.0b4611a6p-34             (pi/2 - PIO2_1, to 33 bits)
 *   PIO2_3      = 0x1.3198a2ep-69              (the rest, to 33 bits)
 *   PIO2_4      = 0x1.b839a252049c1p-104       (the rest)
 *   PIO4_HI     = 0x1.921fb54442d18p-1;   PIO4_LO = 0x1.1a62633145c07p-55
 *   PIO2_HI     = 0x1.921fb54442d18p+0;   PIO2_LO = 0x1.1a62633145c07p-54
 *   PI_HI       = 0x1.921fb54442d18p+1;   PI_LO   = 0x1.1a62633145c07p-53
 *   THREE_PIO4  = 0x1.2d97c7f3321d2p+1
 *   ATAN_HI[4]  = { 0x1.dac670561bb4fp-2, 0x1.921fb54442d18p-1,
 *                   0x1.f730bd281f69bp-1, 0x1.921fb54442d18p+0 }
 *                 (atan 1/2, atan 1, atan 3/2, pi/2)
 *   ATAN_LO[4]  = { 0x1.a2b7f222f65e2p-56, 0x1.1a62633145c07p-55,
 *                   0x1.007887af0cbbdp-56, 0x1.1a62633145c07p-54 }
 *                 (each the exact value minus its ATAN_HI)
 *
 * POLYNOMIALS (minimax fits of absolute error, Remez in MPFR at 400
 * bits, rounded to double; c[0] first):
 *   E2[11] = { 0x1.62e42fefa39efp-1, 0x1.ebfbdff82c598p-3,
 *     0x1.c6b08d704a0cbp-5, 0x1.3b2ab6fba1dd9p-7, 0x1.5d87fe78a2ccp-10,
 *     0x1.4309130970158p-13, 0x1.ffcbfc7440db3p-17, 0x1.62bfd476d463cp-20,
 *     0x1.b524dcba88458p-24, 0x1.e606408c6f3c5p-28, 0x1.ea04441abc917p-32 }
 *     ((2^f - 1)/f, f in [-0.5, 0.5]; fit error 2^-56.2)
 *   EE[11] = { 0x1p+0, 0x1.0000000000005p-1, 0x1.555555555555ep-3,
 *     0x1.55555555520acp-5, 0x1.111111110e367p-7, 0x1.6c16c17f44742p-10,
 *     0x1.a01a01b6a20fdp-13, 0x1.a019a66801012p-16, 0x1.71dde8aed8788p-19,
 *     0x1.28a2c32005f09p-22, 0x1.af78618388f85p-26 }
 *     ((e^r - 1)/r, r in [-0.34658, 0.34658]; fit error 2^-55.7)
 *   LG[7] = { 0x1.5555555555558p-2, 0x1.9999999995224p-3,
 *     0x1.2492492e03d78p-3, 0x1.c71c62d050743p-4, 0x1.7462ba224a45p-4,
 *     0x1.39fdba11582f2p-4, 0x1.2b5fb4b104032p-4 }
 *     ((atanh(s)/s - 1)/z, z = s^2 in [0, 0.02944]; fit error 2^-52.5,
 *     entering the result times z, so below 2^-57 of it)
 *   AT[12] = { -0x1.5555555555555p-2, 0x1.99999999998d2p-3,
 *     -0x1.2492492486002p-3, 0x1.c71c71bd9b2adp-4, -0x1.745d155eaf558p-4,
 *     0x1.3b137787f7a4cp-4, -0x1.110cb70db121cp-4, 0x1.e1727d35b3b72p-5,
 *     -0x1.ab6624fd547eap-5, 0x1.7031d4e6f303dp-5, -0x1.0fccd31bca1abp-5,
 *     0x1.e5a8c9a296d27p-7 }
 *     ((atan(t)/t - 1)/z, z = t^2 in [0, 0.19142]; fit error 2^-57.9)
 *   TN[15] = { 0x1.5555555555555p-2, 0x1.1111111111088p-3,
 *     0x1.ba1ba1ba2b85p-5, 0x1.664f487cef92fp-6, 0x1.226e367bb3c9p-7,
 *     0x1.d6d38f33aa6cdp-9, 0x1.7da85001f510cp-10, 0x1.3517af0a9b129p-11,
 *     0x1.fa202d204f4f1p-13, 0x1.781816cdeab9cp-14, 0x1.d78aa3f38b737p-15,
 *     -0x1.8dd7c60a09eafp-17, 0x1.38903e6ff4c13p-15, -0x1.39c55cf2fc233p-16,
 *     0x1.1faa6f4f45aecp-17 }
 *     ((tan(r)/r - 1)/z, z = r^2 in [0, 0.6169]; fit error 2^-57.4)
 *
 * HELPERS.
 *   rint(v) = (v + SHIFT) - SHIFT, for |v| < 2^51 (round to nearest,
 *     ties to even). k below is that double: used as a double in
 *     products, and as the integer it holds in scale() and for its
 *     parity in TAN.
 *   horner(c[n], v) = (...((c[n-1] v + c[n-2]) v + c[n-3]) ...) v + c[0].
 *   pow2(k) = from((k + 1023) << 52), for k in [-1022, 1023].
 *   (EXP2's and EXP's k lie in [-1076, 1024] after their step 1, so
 *   scale()'s k + 600 is always a normal exponent.)
 *   scale(y, k): k > 1023: (y pow2(1023)) 2; k >= -1022: y pow2(k);
 *     k < -1022: (y pow2(k + 600)) 0x1p-600 (one rounding, to a
 *     subnormal or 0).
 *   high26(v) = from(bits(v) & 0xFFFFFFFFF8000000).
 *   split(a): c = SPLITTER a; a_hi = c - (c - a); a_lo = a - a_hi.
 *   two_prod(a, b): p = a b; e = (((a_hi b_hi - p) + a_hi b_lo) +
 *     a_lo b_hi) + a_lo b_lo  (p + e = a b exactly, |a|, |b| < 2^996).
 *   two_sum(a, b): s = a + b; v = s - a; e = (a - (s - v)) + (b - v).
 *   core(c, f, f_err), 1 + f c[0] + f^2 (c[1] + ...), f_err the error
 *   already known in f (0 for exp2):
 *     c_hi = high26(c[0]); c_lo = c[0] - c_hi;
 *     f_hi = high26(f); f_lo = f - f_hi;
 *     a = f_hi c_hi                                  (exact)
 *     (s, e) = two_sum(1, a)
 *     tail = (((f_lo c_hi) + (f c_lo)) + (f_err c[0])) +
 *            (f (f horner(c[1..n-1], f)))
 *     return s + (e + tail)
 *
 * EXP2(x).
 *   1. x a NaN: QNAN. x > 1024: +infinity. x < -1076: +0.
 *   2. k = rint(x); f = x - k.
 *   3. return scale(core(E2, f, 0), k).
 *
 * EXP(x).
 *   1. x a NaN: QNAN. x > 710: +infinity. x < -746: +0.
 *   2. k = rint(x LOG2E); a = x - k LN2_HI; b = k LN2_LO;
 *      r = a - b; r_lo = (a - r) - b.
 *   3. return scale(core(EE, r, r_lo), k).
 *
 * LOG2(x).
 *   1. x a NaN: QNAN. x = +0 or -0: -infinity. x's sign set: QNAN.
 *      x = +infinity: +infinity.
 *   2. u = bits(x). u < 2^52 (subnormal): sh = the shift that sets bit
 *      52 of u << sh; e = -1022 - sh; u = ((u << sh) & 0x000FFFFFFFFFFFFF)
 *      | 0x3FF0000000000000. Otherwise e = (u >> 52) - 1023; u = (u &
 *      0x000FFFFFFFFFFFFF) | 0x3FF0000000000000.
 *   3. u > 0x3FF6A09E667F3BCD (m above sqrt 2): u = u - 2^52; e = e + 1.
 *   4. m = from(u); f = m - 1; hfsq = (0.5 f) f; s = f / (2 + f);
 *      z = s s; R = (2 z) horner(LG, z).
 *   5. hi = from(bits(f - hfsq) & 0xFFFFFFFF00000000);
 *      lo = ((f - hi) - hfsq) + s (hfsq + R).
 *   6. v_hi = hi IVLN2_HI; v_lo = ((lo + hi) IVLN2_LO) + (lo IVLN2_HI).
 *   7. w = e + v_hi; v_lo = v_lo + ((e - w) + v_hi); return v_lo + w.
 *
 * ATAN, its core: atan_parts(a, corr) for a in [2^-27, 2^66):
 *   a < 7/16: id = none, t = a.  a < 11/16: id 0, t = ((2 a) - 1)/(2 + a).
 *   a < 19/16: id 1, t = (a - 1)/(a + 1).
 *   a < 39/16: id 2, t = (a - 1.5)/(1 + (1.5 a)).  Else id 3, t = -1/a.
 *   z = t t; w = t (z horner(AT, z)).
 *   id none: head = t, tail = w + corr; else head = ATAN_HI[id],
 *   tail = ((w + ATAN_LO[id]) + corr) + t.
 *
 * ATAN(x).
 *   1. x a NaN: QNAN. a = |x| (sign cleared).
 *   2. a < 2^-27: return x. a >= 2^66 (infinity included): r = PIO2_HI.
 *      Otherwise (head, tail) = atan_parts(a, 0); r = head + tail.
 *   3. x's sign set: -r; else r.
 *
 * ATAN2(y, x).
 *   1. y or x a NaN: QNAN. a_y = |y|, a_x = |x|, s_y and s_x their signs.
 *   2. a_y = 0: r = s_x ? PI_HI : +0.
 *      Else a_x = 0: r = PIO2_HI.
 *      Else a_x infinite: r = a_y infinite ? (s_x ? THREE_PIO4 : PIO4_HI)
 *        : (s_x ? PI_HI : +0).
 *      Else a_y infinite: r = PIO2_HI.
 *      Else: q = a_y / a_x;
 *        q >= 2^66: head = PIO2_HI, tail = PIO2_LO;
 *        q < 2^-27: head = q, tail = +0;
 *        else: (b_y, b_x) = (a_y, a_x), but (a_y 0x1p-600, a_x 0x1p-600)
 *          if a_y > 2^900 or a_x > 2^900, else (a_y 0x1p600, a_x 0x1p600)
 *          if a_y < 2^-900 or a_x < 2^-900 (exact);
 *          (p, pe) = two_prod(q, b_x); corr = (((b_y - p) - pe) / b_x) /
 *          (1 + (q q)); (head, tail) = atan_parts(q, corr).
 *        s_x clear: r = head + tail. Set: (d, e) = two_sum(PI_HI, -head);
 *        r = d + ((e + PI_LO) - tail).
 *   3. s_y set: -r; else r.
 *
 * TAN(x).
 *   1. x a NaN, or |x| > 2^20 (infinity included): QNAN. |x| < 2^-27:
 *      return x.
 *   2. k = rint(x TWO_OVER_PI); a = x - k PIO2_1 (exact); b = k PIO2_2;
 *      (r1, e1) = two_sum(a, -b); c = k PIO2_3; (r2, e2) = two_sum(r1, -c);
 *      lo = (e1 + e2) - (k PIO2_4); r_hi = r2 + lo; r_lo = (r2 - r_hi) + lo.
 *   3. |r_hi| >= 0x1.58p-1 (0.671875): sg = +1 or -1, r_hi's sign;
 *      y = (PIO4_HI - (sg r_hi)) + (PIO4_LO - (sg r_lo)); z = y y;
 *      ty = y + (y (z horner(TN, z)));
 *      k even: v = (ty + ty) / (1 + ty); return sg (1 - v).
 *      k odd:  v = (ty + ty) / (1 - ty); return (-sg) (1 + v).
 *   4. Otherwise: z = r_hi r_hi; w = r_hi (z horner(TN, z));
 *      t_n = r_hi + w; t_l = ((r_hi - t_n) + w) + (r_lo (1 + (t_n t_n)));
 *      t = t_n + t_l. k even: return t.
 *      k odd: t_lo = (t_n - t) + t_l; q = -1 / t; (p, pe) = two_prod(q, t);
 *      d = ((1 + p) + pe) + (q t_lo); return q + (q d).
 *
 * Exactness the definition relies on, each from Sterbenz's lemma or the
 * bit counts named: f = x - k in exp2 (|x - k| <= 1/2, Sterbenz); a = x - k
 * LN2_HI in exp and x - k PIO2_1 in tan (the product exact, the
 * difference exact by Sterbenz's lemma); m - 1 in log2; f_hi c_hi in core
 * (26 + 26 bits); hi IVLN2_HI in log2 (21 + 32 bits); PIO4_HI - |r_hi| in
 * tan's identity.
 * @endverbatim
 *
 * @par Threads
 * Every call reads only its arguments and writes only its output, so any
 * of them may run at once, from any thread, on any data; nothing is held
 * between calls.
 *
 * @par Results
 * Calls return RBT_RESULT (rbtcore.h): RBT_RESULT_OK, or
 * RBT_RESULT_INVALID_ARGUMENT with the reason in the RBTMATH_ERROR the
 * caller passes (NULL to skip it), nothing written; on success its
 * message is empty. Checked in order, one check and one message each:
 * each array NULL with a count above 0, in parameter order (the inputs,
 * then the output); then the output overlapping each input, in parameter
 * order, without being that input (address ranges compared as integers,
 * count x 8 bytes each). A count of 0 is OK with any pointers and writes
 * nothing.
 */

#include "../rbtcore.h"
#include "rbtmatherror.h"

#include <stdint.h>

#if __cplusplus
extern "C" {
#endif

/**
 * @brief The largest |x| rbtmath_compute_tan() evaluates: 2^20 radians.
 * Past it, and at infinity, the result is a NaN.
 */
#define RBTMATH_TAN_MAX_ARGUMENT 1048576.0

/**
 * @brief tan(x) for each element.
 *
 * @param[in]  x      @p count angles in radians, any double. Only
 *                    read during the call. NULL with a count of 0.
 * @param[in]  count  How many; 0 writes nothing.
 * @param[out] out_y  @p count results: tan(x) within 1 ulp; x itself for a
 *                    zero; a NaN for a NaN, an infinity, or |x| above
 *                    #RBTMATH_TAN_MAX_ARGUMENT. May be @p x itself (in
 *                    place); no other overlap.
 * @param[out] error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL array with
 *         a count above 0, or @p out_y overlapping @p x without being it.
 *         Nothing is written on failure.
 */
RBT_RESULT rbtmath_compute_tan(
	const double *x,
	uint32_t count,
	double *out_y,
	RBTMATH_ERROR *error);

/**
 * @brief atan(x) for each element, in [-pi/2, pi/2].
 *
 * @param[in]  x      @p count values, any double. Only read during the
 *                    call. NULL with a count of 0.
 * @param[in]  count  How many; 0 writes nothing.
 * @param[out] out_y  @p count results within 1 ulp: x itself for a zero;
 *                    +-(the double nearest pi/2) for +-infinity; a NaN for
 *                    a NaN. May be @p x itself; no other overlap.
 * @param[out] error  Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_compute_tan().
 */
RBT_RESULT rbtmath_compute_atan(
	const double *x,
	uint32_t count,
	double *out_y,
	RBTMATH_ERROR *error);

/**
 * @brief atan2(y, x) for each pair: the angle of (x, y) from +x, in
 * [-pi, pi].
 *
 * @param[in]  y          @p count values, any double. Only read during
 *                        the call. NULL with a count of 0.
 * @param[in]  x          @p count values, any double, paired with @p y
 *                        by index. Only read during the call. NULL with
 *                        a count of 0.
 * @param[in]  count      How many; 0 writes nothing.
 * @param[out] out_angle  @p count results within 1 ulp; zeros and
 *                        infinities as C's Annex F (atan2(+-0, -0) is
 *                        +-pi, atan2(+-0, +0) +-0, atan2(+-inf, -inf)
 *                        +-3pi/4, ...); a NaN where either is a NaN. May
 *                        be @p y or @p x itself; no other overlap.
 * @param[out] error      Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_compute_tan(), for either input.
 */
RBT_RESULT rbtmath_compute_atan2(
	const double *y,
	const double *x,
	uint32_t count,
	double *out_angle,
	RBTMATH_ERROR *error);

/**
 * @brief 2^x for each element.
 *
 * @param[in]  x      @p count values, any double. Only read during the
 *                    call. NULL with a count of 0.
 * @param[in]  count  How many; 0 writes nothing.
 * @param[out] out_y  @p count results within 1 ulp, exact for whole x:
 *                    +infinity above 1024, +0 below -1076, subnormal
 *                    between; +infinity for +infinity, +0 for -infinity, a
 *                    NaN for a NaN. May be @p x itself; no other overlap.
 * @param[out] error  Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_compute_tan().
 */
RBT_RESULT rbtmath_compute_exp2(
	const double *x,
	uint32_t count,
	double *out_y,
	RBTMATH_ERROR *error);

/**
 * @brief e^x for each element.
 *
 * @param[in]  x      @p count values, any double. Only read during the
 *                    call. NULL with a count of 0.
 * @param[in]  count  How many; 0 writes nothing.
 * @param[out] out_y  @p count results within 1 ulp: exactly 1 for a zero;
 *                    +infinity where e^x overflows, +0 or a subnormal
 *                    where it underflows; +infinity for +infinity, +0 for
 *                    -infinity, a NaN for a NaN. May be @p x itself; no
 *                    other overlap.
 * @param[out] error  Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_compute_tan().
 */
RBT_RESULT rbtmath_compute_exp(
	const double *x,
	uint32_t count,
	double *out_y,
	RBTMATH_ERROR *error);

/**
 * @brief log2(x) for each element.
 *
 * @param[in]  x      @p count values, any double. Only read during the
 *                    call. NULL with a count of 0.
 * @param[in]  count  How many; 0 writes nothing.
 * @param[out] out_y  @p count results within 1 ulp, exact for powers of 2:
 *                    -infinity for +-0, +infinity for +infinity, a NaN for
 *                    a negative value (-infinity included) or a NaN. May be
 *                    @p x itself; no other overlap.
 * @param[out] error  Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_compute_tan().
 */
RBT_RESULT rbtmath_compute_log2(
	const double *x,
	uint32_t count,
	double *out_y,
	RBTMATH_ERROR *error);

#if __cplusplus
}
#endif

#endif // RBTMATHFUNCTIONS_H
