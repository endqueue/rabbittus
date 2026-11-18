/*
 * rbtmath_functions_draft.h - deterministic elementary functions in
 * Rabbittus Math: tan, atan, atan2, exp2, exp and log2 in double
 * precision, defined by their written evaluation, so every platform gives
 * the same bits (card A61, which names the session that asked).
 *
 * DRAFT, 2026-11-17, version 1 (seven critique passes).
 * APPROVED 2026-11-17 with the user, every question as recommended.
 * Card A61 is still open: this draft writes its recommendation (option a,
 * a small public draft of its own) so the card can be decided against a
 * text; its own questions are section 12's, Q1 to Q8, each with a
 * recommendation. No version is raised for it while Rabbittus' 0.3 is
 * untagged (card X2). Not built, not in any target, not compilable: one
 * new header, include/math/rbtmathfunctions.h, every declaration in it
 * [ADDED]; comments changed in math/rbtmatherror.h and
 * math/rbtmathmedium.h; the shell medium's private exponential replaced
 * by this header's (its bits change; section 6). No struct type is
 * added (4009 stays free). Written on the MacBook (M5 Pro, macOS, Apple
 * clang 21) against Rabbittus as checked out today: rbtcore.h, rbtmath.h,
 * math/rbtmatherror.h, math/rbtmathmedium.h and src/math/rbtmathmedium.cpp
 * (portable_exp, the exponential this moves), math/rbtmathnoise.h and
 * src/math/rbtmathnoise_internal.hpp (built today: the model for "Same
 * bits", the FLT_EVAL_METHOD assertion and the subnormal test),
 * tests/rbtmathnoise_bits_tests.c (the bits test's pattern),
 * tests/rbtmathmedium_tests.c, CMakeLists.txt (RABBITTUS_NO_CONTRACT, by
 * compiler ID, and the older "if (NOT MSVC)" block card A63 covers), and
 * check_naming.py (the naming rule); against what the card's first
 * caller asks (a draft of round worlds' ground, which card A61 names:
 * a cube-to-sphere warp's tan and its inverse's atan, and a size law's
 * powers, all in double); and against Hazel (.refs/HazelRenderer2025,
 * present: its maths is glm's and the platform's libm, with nothing
 * deterministic to compare).
 *
 * Every number below marked "measured" was measured on the MacBook by
 * throwaway programs in the temporary directory, deleted after: the
 * coefficients fitted by a Remez exchange in MPFR 4 at 400 bits; every
 * function's error against MPFR at 300 bits (correctly rounded), 10^7
 * inputs per range; the bits under different optimisation and
 * contraction settings; the subnormal-flushing behaviour; the cost.
 *
 * Every declaration below is marked [ADDED] or [CHANGED], with why it
 * earns its place. The real header would carry the same Doxygen contract.
 *
 * ===========================================================================
 * Revision notes
 * ===========================================================================
 *
 * Seven passes, 2026-11-17, over version 1 as first written, each with one
 * lens, each fixed before the next.
 *
 * Pass 1 (wrong, missing, more than the need). 1. Section 6 gave
 * portable_exp's cost as 9 ns, never measured. Changed: measured, 5.0 ns
 * against exp's 3.7 over 2^20 arguments in [-30, 0]. 2. core()'s
 * argument f_lo and its local f_lo2 read as one thing. Changed: the
 * argument is f_err (the error already known in f), the local f_lo, and
 * core()'s value said in one line. 3. k was an integer in scale() and a
 * double in products, unsaid. Changed: rint's line says both. 4. scale()'s
 * two-step branch needs k + 600 to be a normal exponent, unchecked.
 * Changed: k's range after step 1 stated, [-1076, 1024]. 5. The header's
 * @par Definition was a placeholder. Changed: it carries section 5
 * verbatim, as rbtmathnoise.h's does. 6. "Exactness ... checked by the
 * bits test's reference" claimed a check nothing does. Changed: each
 * exactness named with its reason (Sterbenz's lemma or bit counts).
 * Looked at and kept: RBTMATH_TAN_MAX_ARGUMENT (a limit a caller checks
 * against, as RBTMATH_NOISE_MAX_CELLS is); no struct, so 4009 stays free.
 *
 * Pass 2 (general purpose, any game, industry compared). 1. ln and log10,
 * which games use more than log2, weren't said. Changed: composed from
 * log2 and a constant, measured 1.59 and 1.34 ulps, no amplification
 * (unlike exp from exp2), so no function of their own; in "Out of scope"
 * too. 2. Q2's float caller was told only to "convert". Changed: widening
 * and rounding back gives the correctly rounded float but about one input
 * in 2^28, the same bits everywhere either way. 3. glm and DirectXMath,
 * the inline libraries most engines use, weren't compared. Changed:
 * section 8 (inline, so their bits are the caller's build's). Looked at:
 * every caller named in section 1 is a kind of use, not one game.
 *
 * Pass 3 (the C API). 1. @par Results didn't say the error's message is
 * emptied on success (rbtmatherror.h's rule), or the check order for
 * overlap. Changed: inputs' NULLs, the output's, then overlap per input,
 * one check and message each, ranges compared as integers. 2. No
 * parameter said its range. Changed: "any double". 3. Names checked
 * against check_naming.py: rbtmath_compute_* is <module>_<verb>_<detail>
 * with no handle, allowed; no new object or verb. Looked at: out_ last
 * before error; uint32_t counts as the neighbours; no struct, enum,
 * callback or handle; the macro's value a double literal.
 *
 * Pass 4 (data and allocation). 1. In place was allowed, but nothing said
 * each element is read before its output is written, which is what makes
 * it safe (and atan2's output may alias either input). Changed: section
 * 7. 2. Where the constants live wasn't said. Changed: constexpr arrays in
 * the internal header, read-only, nothing writable (check.sh
 * mutable-globals). Looked at: no allocation, no stack scratch, no table
 * beyond the constants (the table-driven alternative in section 9).
 *
 * Pass 5 (threads, determinism, errors). 1. The flushing behaviour was
 * stated for "a thread that flushes" but measured on arm64's FPCR.FZ
 * only. Changed: said so, x86's FTZ and DAZ owed with the Windows and
 * Fedora runs, and atan2's subnormal inputs (read as 0, the same angle to
 * an ulp) added. 2. The compiler's rewrite of log2's bit test into a
 * float comparison was asserted, not shown. Changed: read in Apple
 * clang's output ("fcmp d0, #0.0"), cited. 3. Signalling NaNs and the
 * targets (x64 and arm64; x87 refused by the assertion) weren't said.
 * Changed: section 4. Kept: values never refused (IEEE's answers are
 * what a caller of an elementary function expects, and a batch from data
 * may hold an infinity on purpose); no print, assert or abort; the same
 * bits at any split of an array.
 *
 * Pass 6 (correctness against the code as built and the measurements). 1.
 * The 10^7 run over any doubles found atan2 at 1.36 ulps near 2^964,
 * where version 1 skipped the division's carry outside [2^-900, 2^900].
 * Changed: both arguments scaled by 2^-600 or 2^600 exactly for the
 * carry (in that branch q is within [2^-27, 2^66], so they can't straddle
 * the window); measured 0.83 and 0.85 at both ends, 0.78 over any
 * doubles. 2. Section 6 said rbtmathnoise_internal.hpp #errors under fast
 * math; it doesn't (it asserts FLT_EVAL_METHOD only). Changed: this
 * internal header adds the #error, and the noise's could take it with
 * card A63. 3. Every figure in section 3 replaced by the 10^7 run's.
 * 4. Three claims not shown: "auto-vectorised" (not checked in the output;
 * dropped, the equal bits at -O0 and -O2 kept), tan's reduction "far
 * above" with no figure (now the 2^-61 worst case over every double, from
 * Muller's tables, against r's error), and ln's constant named LN2, a name
 * section 5 gives nothing (now "the double nearest ln 2").
 * Checked and kept: portable_exp's form, its clamp at 709 and std::ldexp
 * (src/math/rbtmathmedium.cpp); the medium's arguments all non-positive;
 * the CRC 7d88a849 in RBTREND_TODO.md item 52 and claude_made_decisions.md;
 * RABBITTUS_NO_CONTRACT defined before the medium's line in CMakeLists.txt;
 * rbtmatherror.h's list of headers; the medium's tests' tolerances.
 *
 * Pass 7 (tests and the build). 1. CMake's change was a sentence.
 * Changed: section 11 names each line, and that rbtmathmedium.cpp's old
 * line in "if (NOT MSVC)" goes (two settings, the later winning
 * silently). 2. The accuracy test needed a high-precision ulp count.
 * Changed: checked as integers against a correctly rounded table (a
 * faithful result's encoding differs from it by at most 1). 3. The fitter
 * and the reference generator need MPFR: kept in tests/, run by hand, not
 * built, unlike the noise's scale search; said why. 4. #pragma STDC
 * FP_CONTRACT OFF considered: GCC ignores it in C++ with a warning, so
 * the flag by compiler ID stays the one mechanism and the bits test the
 * guard. 5. The bits test's inputs now name the paths they must reach
 * (interval edges, tan's switch and parity, atan2's quadrants and scaled
 * carry, subnormals); test 1 adds atan2's scaled carry and signalling
 * NaNs.
 *
 * ===========================================================================
 * 1. What is missing, and on which side of the line it goes
 * ===========================================================================
 *
 * Rabbittus Math promises the same bits on every platform for what it
 * computes (its BVHs, polygons, shell media and noise), and allows itself
 * no libm call but floor and sqrt: IEEE 754 makes those exact or
 * correctly rounded everywhere, and nothing else in libm is. tan, atan,
 * exp, exp2, log and pow are each rounded by the platform's own
 * implementation: Apple's libm, glibc, musl and the MSVC runtime (UCRT)
 * each give a result within an ulp or so, but not the same one, and the
 * UCRT chooses at run time between an FMA3 path and another
 * (_set_FMA3_enable), so two Windows machines can disagree with each
 * other. Measured here, Apple's libm against MPFR: exp2 0.51 ulp, exp
 * 0.51, log2 0.51, atan 0.83, atan2 0.73, tan 2.84 (its worst over
 * 3 x 10^5 inputs per function); a function within half an ulp most of
 * the time and above it sometimes is a function whose bits differ from
 * another's on some inputs.
 *
 * What depends on those bits: anything a seed generates and many machines
 * must agree on. The card's first caller warps a cube onto a sphere by a
 * tangent (and its inverse by atan) and sizes craters by a power law
 * (r = (a - u b)^(-1/s): exp2 and log2); if a vertex's place differed by
 * an ulp between two machines, a ground generated on both would differ,
 * and so would a collision against it or a lock-step simulation over it.
 * Every later caller meets the same need: clouds' and fog's densities
 * (Beer-Lambert's exp, cards A57 and A59), a game's own curves (an
 * exponential decay, a decibel's log2, a heading's atan2), a camera's
 * smoothing replayed from a recording. The shell medium already carries a
 * private exponential (portable_exp) for this reason; the card's
 * alternative was for every caller to write its own.
 *
 * The user's line (2026-11-16): "What is pure general purpose math versus
 * what is rendered graphics? That's the distinction I want between
 * Rabbittus Math and Rend." An elementary function is pure maths:
 * Rabbittus Math's. A shader's tan or exp is Rend's and its GPU's (fast
 * math, fused multiply-adds), never these bits; a GPU result is compared
 * with a tolerance, as the noise's are.
 *
 * ===========================================================================
 * 2. The design in brief
 * ===========================================================================
 *
 * - SIX FUNCTIONS, DOUBLE ONLY: tan, atan, atan2, exp2, exp, log2, each
 *   from double to double. The card's four (tan, atan, exp2, log2), atan2
 *   (atan's quadrant form, which every caller with a direction needs and
 *   would otherwise write itself, with its own bits; Q5), and exp (the
 *   medium's exponential, and the one every density and decay needs; Q4).
 *   Double, because every caller named computes in double (Q2).
 * - BATCH CALLS INTO THE CALLER'S ARRAYS: rbtmath_compute_exp2(x, count,
 *   out_y, error), and so on: arrays in, arrays out, in place allowed, as
 *   the medium's and the noise's calls are; nothing allocated. No scalar
 *   call crosses the boundary, and none is inline in the header (Q3):
 *   an inline body is compiled by the caller's compiler with the caller's
 *   flags, and a caller that contracts gets other bits.
 * - DEFINED BY THEIR WRITTEN EVALUATION: each function is a range
 *   reduction, a fixed polynomial with its coefficients in hex, and a
 *   fixed reconstruction, every operation one IEEE 754 binary64 operation
 *   in a written order, no contraction, no libm call at all (not even
 *   floor: a rounding constant does it). The header carries the whole
 *   definition (@par Definition), so another language or a GPU's copy can
 *   reproduce it (Q1).
 * - UNDER ONE ULP, MEASURED: every result within 1 ulp of the exact
 *   value (faithful), measured over 10^7 inputs per range against MPFR;
 *   not proven (section 3).
 * - IEEE'S ANSWERS FOR SPECIAL VALUES: a NaN in gives a NaN out; an
 *   infinity, a zero of either sign and an overflow give C's Annex F
 *   results; a NaN out is always the same bits. tan past 2^20 radians
 *   gives a NaN (Q6). Values are never refused: only the arrays are
 *   checked.
 * - FROZEN FROM THE FIRST RELEASE: a function's bits never change once
 *   released (a world is its seed and its generator); a better one would
 *   be a new function (Q8).
 *
 * ===========================================================================
 * 3. The functions, their accuracy, and why each evaluation
 * ===========================================================================
 *
 * Every evaluation is written out in section 5 (normative). Here, what
 * each does, its measured error, and why it is shaped so.
 *
 * Error is in ulps of the exact result: |computed - exact| / ulp(exact),
 * ulp(v) = 2^(e - 52) for v in [2^e, 2^(e+1)) and 2^-1074 below 2^-1022.
 * Measured over 10^7 inputs per range (uniform, and uniform over every
 * double's bits where a range says "any"), against MPFR at 300 bits:
 *
 * | Function | Range                              | Largest  | > 0.5 ulp |
 * | -------- | ---------------------------------- | -------- | --------  |
 * | exp2     | [-1, 1]                            | 0.736    | 3.77%     |
 * | exp2     | [-1022, 1024] (normal results)     | 0.744    | 3.77%     |
 * | exp2     | [-1075, -1022] (subnormal)         | 0.808    | 0.94%     |
 * | exp      | [-1, 1]                            | 0.687    | 1.90%     |
 * | exp      | [-708, 709.78] (normal)            | 0.713    | 2.07%     |
 * | exp      | [-745.1, -708.4] (subnormal)       | 0.790    | 0.93%     |
 * | log2     | any positive double                | 0.695    | 0.008%    |
 * | log2     | [0.5, 2]                           | 0.836    | 2.00%     |
 * | log2     | subnormal inputs                   | 0.500    | 0.001%    |
 * | atan     | [-4, 4]                            | 0.752    | 7.42%     |
 * | atan     | any double                         | 0.790    | 0.019%    |
 * | atan2    | [-10, 10] x [-10, 10]              | 0.849    | 3.75%     |
 * | atan2    | any doubles (3 x 10^6)             | 0.780    | 0.04%     |
 * | atan2    | both about 2^960 (3 x 10^6)        | 0.834    |           |
 * | atan2    | both about 2^-1000 (3 x 10^6)      | 0.847    |           |
 * | tan      | (-pi/2, pi/2)                      | 0.944    | 3.88%     |
 * | tan      | [-2^20, 2^20]                      | 0.972    | 3.71%     |
 * | tan      | within 512 ulps of k pi/2          | 0.500    | 0.000%    |
 *
 * The stated bound is 1 ulp: faithful rounding, every result one of the
 * two doubles either side of the exact value. It is a measurement, not a
 * proof (no Gappa or Sollya certificate; Q1's option b is the proven
 * kind). Each evaluation was shaped until its measurement sat clearly
 * below 1 ulp; what each shape fixed is said below.
 *
 * Exact where exactness is expected: exp2(n) = 2^n for every whole n in
 * range; log2(2^n) = n; exp(0) = exp2(0) = 1; log2(1) = +0; tan, atan,
 * atan2 of a zero are that zero; tan and atan are odd bit for bit
 * (f(-x) is -f(x) with the sign bit flipped, measured over 2^20 inputs).
 * Not promised: monotonicity (a faithful function can step back by one
 * ulp between neighbouring inputs; no caller named needs it).
 *
 * EXP2 (2^x). x = k + f, k the nearest integer (ties to even), f in
 * [-1/2, 1/2] exact; 2^f by 1 + f P(f), P of degree 10, a minimax fit of
 * (2^f - 1) / f; the result scaled by 2^k exactly. The plain sum
 * 1 + f P(f) measured 1.12 ulp: its two roundings (of f P and of the sum)
 * and P's own add up past an ulp where the result is under 1. So the
 * first term, f c0, is formed exactly (f and c0 split at 26 bits, their
 * high halves' product exact) and added to 1 with its rounding error
 * kept (Knuth's two-sum), the rest added to that error before the last
 * rounding: measured 0.74. A result below 2^-1022 is scaled in two steps
 * so it is rounded once, to a subnormal.
 *
 * EXP (e^x). x = k ln 2 + r, k = rint(x log2 e), r = (x - k LN2_HI) -
 * k LN2_LO, ln 2 split so k LN2_HI is exact (LN2_HI has 32 significant
 * bits, k at most 11 bits); r's rounding error kept (r_lo, one fast two-sum);
 * e^r by the same exact-first-term form as exp2, its polynomial a minimax
 * fit of (e^r - 1) / r on |r| <= 0.34658, r_lo entering the tail.
 * Measured 0.71. Why its own and not exp2(x log2 e): the product
 * x log2 e rounds by up to half an ulp of a number up to 1075, an error
 * of 2^-53 x 1075 in the exponent, so e^x would be off by up to 0.8 x
 * 10^-13 relatively: measured 438 ulps. Q4.
 *
 * LOG2. x = 2^e m, m in [sqrt(1/2), sqrt 2) taken from x's bits (a
 * subnormal x normalised by shifting its significand, so its e is right);
 * f = m - 1 exact; log(1 + f) as f - f^2/2 + s (f^2/2 + R(s^2)),
 * s = f / (2 + f), R a minimax fit of the atanh series' tail (degree 6 in
 * s^2), the form fdlibm's log uses because f - f^2/2 carries the result's
 * leading bits with one rounding; then times 1/ln 2 with both split in two
 * (f - f^2/2's high 21 bits times 1/ln 2's high 32 bits exact), and e
 * added with its rounding error carried. Measured 0.84. The method is
 * fdlibm's (FreeBSD's e_log2.c, after Sun's e_log.c): a technique, not
 * copied code; the constants and coefficients here are this draft's own
 * fits.
 *
 * ATAN. Odd: atan(-x) = -atan(x). |x| below 2^-27: x (the cubic term is
 * under half an ulp). |x| at or above 2^66: the double nearest pi/2.
 * Otherwise five intervals, fdlibm's: below 7/16 directly; up to 11/16
 * about 1/2, t = (2x - 1)/(2 + x); up to 19/16 about 1, t = (x - 1)/(x +
 * 1); up to 39/16 about 3/2, t = (x - 3/2)/(1 + 3x/2); above, t = -1/x
 * about infinity: each t within |t| <= 7/16, and atan(x) = atan(c) +
 * atan(t), atan(c) in two parts. atan(t) = t + t (t^2 Q(t^2)), Q a minimax
 * fit of (atan(t)/t - 1)/t^2, degree 11. Measured 0.79.
 *
 * ATAN2. C's Annex F for zeros and infinities (section 5). Otherwise
 * q = |y| / |x| and atan(q) as above, then the quadrant: x positive, the
 * angle; x negative, pi minus it, worked as (PI_HI - head) exactly, with
 * its error, plus (PI_LO - tail), so pi - atan(q) is rounded once. The
 * division's rounding error is carried too: q_lo = (|y| - q|x|) / |x| with
 * q|x| exact (Dekker's product), entering atan as q_lo / (1 + q^2). A
 * plain atan(q) and pi - z measured 1.41 ulp; with both carried, 0.85.
 * Where |x| or |y| is outside [2^-900, 2^900] (where the exact product's
 * halves would overflow or underflow), both are first scaled by 2^-600
 * or 2^600, exactly, for the carry alone: in this branch q is within
 * [2^-27, 2^66], so the two can't be on opposite sides, and the scaled
 * pair lands inside. (Version 1 skipped the carry there instead: 1.36
 * ulps measured near 2^964; pass 6.)
 *
 * TAN. Odd. |x| below 2^-27: x. |x| above 2^20 (and infinity): a NaN
 * (Q6). x = k pi/2 + r, k = rint(x 2/pi), |r| <= pi/4, pi/2 in four
 * parts (three of 33 bits, so k times each is exact for k below 2^20, and
 * the rest), each subtraction's rounding error kept (two-sums), r carried
 * as r_hi + r_lo. tan(r) = r + r (r^2 T(r^2)), T a minimax fit of
 * (tan(r)/r - 1)/r^2 to degree 14 on r^2 <= 0.6169; for |r| at or above
 * 0.671875, tan(r) = (1 - tan y)/(1 + tan y) with y = pi/4 - |r| (fdlibm's
 * kernel_tan's identity), which shrinks the polynomial's share of the
 * result where tan is near 1. k odd: -1/tan(r), its rounding corrected
 * by one step of Newton's iteration with an exact product. Measured
 * 0.97. The plain forms (no compensation, no identity) measured 1.52.
 * Why 2^20: the three exact parts reach k below 2^20, and the closest any
 * double comes to a multiple of pi/2 is about 2^-61 (Muller's tables, for
 * every double), far above the absolute error the four parts and the
 * two-sums leave in r (about 2^-130 for k below 2^20), so r keeps its
 * relative accuracy everywhere (measured near k pi/2: 0.500 ulp); past it a
 * reduction needs Payne and Hanek's method and a table of 2/pi's bits
 * (Q6). An angle in radians past a million is no angle a caller means.
 *
 * Natural and decimal logarithms (not functions here): ln x = log2(x) c
 * and log10 x = log2(x) c', composed by the caller, c and c' the doubles
 * nearest ln 2 and log10 2: one more rounding and the constant's,
 * measured 1.59 and 1.34 ulps over 10^6 inputs. Unlike exp from exp2, no
 * error is amplified, so neither earns a function until a caller needs
 * the last half ulp.
 *
 * Powers (not a function here): x^y = exp2(y log2 x) for x > 0, composed
 * by the caller. Its error grows with |y log2 x|: log2's relative error
 * becomes an absolute error in the exponent; about 1 + 2 |y log2 x| ulps.
 * A size law over radii of a few to a few hundred cells is within some
 * 30 ulps (10^-14 relatively). A pow with an extended-precision log is
 * not drafted: no caller needs it (section 9).
 *
 * ===========================================================================
 * 4. The same bits everywhere
 * ===========================================================================
 *
 * The definition (section 5) gives every operation and its order; what
 * makes a build follow it:
 *
 * - No contraction. A fused multiply-add rounds once where the
 *   definition rounds twice. src/math/rbtmathfunctions.cpp is built with
 *   RABBITTUS_NO_CONTRACT (CMakeLists.txt's, chosen by the compiler's ID:
 *   -ffp-contract=off for Clang and GCC, /clang:-ffp-contract=off for
 *   clang-cl, /fp:precise on MSVC 17.0 or later, which contracts only
 *   when asked). Measured here: the same source with Apple clang's
 *   default (-ffp-contract=on) gives other bits for every function.
 * - No fast math: the internal header refuses to build under it (#error
 *   where __FAST_MATH__ or _M_FP_FAST is defined); measured,
 *   -ffast-math breaks the odd symmetry on 5,299 of 2^20 inputs.
 * - Every double operation rounded once to binary64: FLT_EVAL_METHOD 0,
 *   static_asserted (x87's extended registers would round twice).
 * - No libm call at all. rint is (v + 0x1.8p52) - 0x1.8p52 for |v| <
 *   2^51; 2^k is built from its bits; floor, ldexp and fma are not used.
 * - No reassociation: the element loop does the same operations per
 *   element whatever the compiler's vectorisation; measured, -O0 and -O2
 *   give the same bits.
 * - The default environment: round to nearest, no trap enabled. A
 *   caller that changes the rounding mode gets undefined results (the
 *   rounding constant itself depends on it). The status flags after a
 *   call are unspecified.
 * - NaN's bits: every NaN written is 0x7FF8000000000000 (the quiet NaN,
 *   sign clear), written as a constant, never made by arithmetic: x86's
 *   default NaN has its sign set and arm64's hasn't, and a NaN input's
 *   payload is propagated differently between them.
 * - Subnormals and flushing (FTZ, DAZ; arm64's FPCR.FZ). The bits are
 *   defined in the default environment, which keeps subnormals. A thread
 *   that flushes them gets, measured on arm64 with FPCR.FZ set (2 x 10^5
 *   inputs a function, half of them subnormal or giving subnormals): atan
 *   and tan unchanged (a subnormal x is returned as it came, no
 *   arithmetic on it); exp2 and exp 0 where the result would be
 *   subnormal (a difference below 2^-1022); atan2 0 where its result
 *   would be subnormal, and a subnormal input read as 0, which gives the
 *   same angle to within an ulp (atan2(tiny, -1) is pi either way);
 *   log2 of a subnormal input -infinity (the input read as 0). The last
 *   cannot be avoided by testing x's bits: the compiler may turn an
 *   integer test of x's bits into a float comparison, which flushing then
 *   answers (Apple clang compiles step 1's test of bits(x) << 1 to
 *   "fcmp d0, #0.0": read in its output). x86's FTZ and DAZ are separate
 *   controls and weren't measured (no x86 machine here, and no Rosetta):
 *   owed with the bits test on Windows and Fedora. Said in the header.
 * - Signalling NaNs: step 1's x != x catches them as any NaN; the quiet
 *   NaN is written. Whether the invalid flag is raised is unspecified.
 * - Targets: x64 and arm64 only (SSE2 and NEON doubles, one rounding an
 *   operation); 32-bit x86's x87 is refused by the FLT_EVAL_METHOD
 *   assertion.
 *
 * What a GPU copy can't do: Metal compiles with fast math and every GPU
 * may fuse; a shader's copy is checked to a tolerance (a few ulps), never
 * bits. Nothing in Rend uses these yet.
 *
 * ===========================================================================
 * 5. The definition (normative; the header carries it verbatim)
 * ===========================================================================
 *
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
 *
 * ===========================================================================
 * 6. The medium's exponential moved onto it
 * ===========================================================================
 *
 * src/math/rbtmathmedium.cpp's portable_exp (a Taylor series to the 13th
 * power, term by term with a division each, then std::ldexp) is removed;
 * the medium calls this header's exp through the internal header below.
 * What changes:
 * - Accuracy: portable_exp measured 4.30 ulps (and std::ldexp, a libm
 *   call, went where Rabbittus Math allowed only floor and sqrt); exp
 *   here, 0.71.
 * - Bits: every density and transmittance changes in its last bits, so
 *   the medium's recorded CRC (7d88a849, RBTREND_TODO.md item 52's record
 *   and claude_made_decisions.md's entry) is recorded anew, with why.
 *   Rabbittus 0.3 is unreleased: no world is built on it.
 * - Meaning: none. portable_exp clamped x above 709 to e^709 where exp
 *   gives +infinity; the medium only takes negative arguments (minus a
 *   height over a scale height, minus an optical depth), so no call sees
 *   the difference.
 * - Cost: 13 divisions an exponential before, none now (measured over
 *   2^20 arguments in [-30, 0]: exp 3.7 ns an element, portable_exp 5.0;
 *   not material to the medium's ray march).
 *
 * How it reaches the medium: src/math/rbtmathfunctions_internal.hpp holds
 * the six evaluations as inline functions (namespace rbtfn), included by
 * rbtmathfunctions.cpp (the batch calls' loops) and rbtmathmedium.cpp.
 * Both are built with RABBITTUS_NO_CONTRACT, so rbtmathmedium.cpp moves
 * from the "if (NOT MSVC)" block to it in the same change (card A63's
 * fix, for this one file, ahead of the rest of A63). The internal header
 * static_asserts FLT_EVAL_METHOD 0, as rbtmathnoise_internal.hpp does,
 * and #errors under fast math (__FAST_MATH__ for Clang and GCC,
 * _M_FP_FAST for MSVC), which rbtmathnoise_internal.hpp doesn't yet: a
 * line card A63's change may give it too.
 *
 * ===========================================================================
 * 7. Batch shape, layout and cost
 * ===========================================================================
 *
 * CONVENTIONS.md: "Whole arrays in one pass, batch-shaped calls ...
 * never a call per sample, per instance or per frame across a boundary."
 * A function here costs 3 to 20 ns; a call across a library boundary
 * (not inlined, arguments checked, the error cleared) a few ns more, so
 * a scalar call would cost up to twice the work for the cheap ones. A
 * caller's use is an array: a chunk's coordinates through the warp, a
 * field's craters' radii, a column of densities. One call takes the
 * array, checks it once, and runs a loop the compiler may vectorise.
 *
 * Why not inline in the header (as glm, DirectXMath and Unreal's FMath
 * are): an inline body is compiled in the caller's translation unit under
 * the caller's flags. Apple clang contracts by default, so a caller who
 * didn't know would get other bits, silently; the bits would be the
 * caller's build's, not Rabbittus'. Compiled once, in Rabbittus, under
 * RABBITTUS_NO_CONTRACT, they are Rabbittus'.
 *
 * Inside: for each element in order, its input read into a local, the
 * evaluation of section 5, its output written (so in place is safe, and
 * atan2's output may be either input); no allocation, no state, no
 * scratch on the stack, no table but the constants (constexpr arrays in
 * the internal header: read-only data, nothing writable, as check.sh
 * mutable-globals asks). An output may be its input's own array (in
 * place); any other overlap is refused, so the result never depends on
 * the order of the loop. Elements are independent, so a caller's jobs
 * may split an array anywhere and get the same bits.
 *
 * Cost (measured, M5 Pro, one core, Release-like -O2, 2^20 elements, the
 * loop as the batch call runs it):
 *
 * | Function | ns an element | Apple libm |
 * |----------|---------------|------------|
 * | exp2     | 3 to 4        | 1.9        |
 * | exp      | 3.5 to 4      | 5.0        |
 * | log2     | 3.3 to 5.3    | 2.2        |
 * | atan     | 4.7 to 7.4    | 7.2        |
 * | atan2    | 10 to 15      | 16         |
 * | tan      | 12 to 19      | 3.0        |
 *
 * (The ranges are two runs' spread.) tan is the dearest: its reduction's
 * two-sums, a degree-14 polynomial and a division; a cube-sphere warp
 * calls it once per vertex coordinate, under 40 ns a vertex against the
 * hundreds its noise costs.
 *
 * ===========================================================================
 * 8. Compared
 * ===========================================================================
 *
 * - Apple's libm, glibc, musl, the UCRT: each within about an ulp, none
 *   the same as another. glibc's exp, exp2 and log2 are Arm's optimized
 *   routines (since 2.28), table-driven and about half an ulp; its atan
 *   and tan the IBM Accurate Mathematical Library, no longer correctly
 *   rounded since its slow paths were removed. The UCRT picks an FMA3
 *   path at run time (_set_FMA3_enable). None promises bits across
 *   versions either: a libm update can change a world.
 * - CORE-MATH (Zimmermann and others, MIT licence): correctly rounded
 *   functions, so every correct implementation agrees by definition, the
 *   strongest answer there is. Costs: vendored C (tables, a fast path and
 *   an accurate one, written for GCC and Clang; the double functions use
 *   128-bit integers in places, which MSVC hasn't, to be confirmed
 *   against the release taken), each function's accurate path slower,
 *   and no GPU copy can follow its accurate path. glibc has begun taking
 *   its binary32 functions. Q1's option b.
 * - CRlibm (correctly rounded, 2004 to 2010): LGPL, unmaintained, and it
 *   asks the caller to set x87 precision: no.
 * - musl's libm, and Rust's libm crate (a port of it, used by Rapier's
 *   "enhanced-determinism" feature): one portable C source, so the same
 *   bits on every platform when built alike, which is this draft's idea
 *   with someone else's source; fdlibm's methods, about 1 ulp. Q1's
 *   option c.
 * - SLEEF (Boost licence): vectorised functions with stated bounds (1.0
 *   or 3.5 ulps), generated per instruction set; a large dependency for
 *   six functions, and its bits follow its FMA and non-FMA builds.
 * - C23 reserves the cr_ prefix (cr_exp, cr_log2, ...) for correctly
 *   rounded functions; none of the three platforms ships them.
 * - Jolt Physics: its own float sin, cos, tan, asin, acos, atan and
 *   atan2 (Cephes' polynomials, SIMD), for its cross-platform
 *   determinism mode, built without contraction: the same idea, in
 *   float. Box2D v3: its own atan2 and cosine-sine approximations, for
 *   cross-platform determinism. Unreal's FMath: its own Atan2 polynomial,
 *   the rest the CRT's (not deterministic across platforms). Unity's
 *   Burst: FloatMode.Deterministic is reserved, not implemented.
 *   Photon Quantum and others: fixed-point maths, a different trade.
 * - glm and DirectXMath: inline in the caller's build. glm calls the
 *   platform's libm; DirectXMath's XMScalarSin and its kin are its own
 *   float polynomials, inline, so their bits follow the caller's
 *   compiler and flags.
 * - Hazel: glm and the platform's libm; nothing deterministic.
 * Where this sits: the engines' own approach (a written evaluation,
 * contraction off), at double precision and under one ulp, with the
 * definition published so a second implementation can match it.
 *
 * ===========================================================================
 * 9. Alternatives, and why not
 * ===========================================================================
 *
 * - Each caller its own polynomial (the card's option c): every caller
 *   writes, fits and tests its own, with its own bits.
 * - Inside the noise header (the card's option b): these aren't noise.
 * - Float versions: no caller named computes in float (Q2).
 * - A table-driven exp2 (Arm's, 2^(j/128) in a table, degree 5): half an
 *   ulp and faster, but 128 or 256 more constants in the definition, and
 *   a GPU copy then needs the table; the compensated polynomial reaches
 *   0.74 with none.
 * - pow: x^y with an extended-precision log is a page of its own and no
 *   caller needs more than exp2(y log2 x)'s accuracy (section 3).
 * - sin and cos: tan's reduction is theirs too, so they cost two
 *   polynomials and a quadrant table; not asked for by any caller named
 *   (Q7).
 *
 * ===========================================================================
 * 10. Tests
 * ===========================================================================
 *
 * tests/rbtmathfunctions_tests.c (C, through the public API):
 * 1. Special values: every case of section 5's step 1 and atan2's step
 *    2 (NaNs of both signs and payloads in, signalling ones too, the one
 *    NaN out; zeros of both signs; infinities; the overflow and underflow
 *    edges: exp2(1024) +infinity, exp2(-1074) 2^-1074, exp2(-1075) +0,
 *    exp's largest finite and smallest non-zero inputs; tan at 2^20 and
 *    the next double), each against its expected bits; and atan2's scaled
 *    carry (both arguments about 2^960, both about 2^-1000, one
 *    subnormal) against the reference table's results.
 * 2. Exact cases: exp2(n) for every whole n in [-1074, 1023], log2(2^n)
 *    for every n in [-1074, 1023], exp(0), log2(1).
 * 3. Odd symmetry, bit for bit, for tan and atan over 2^20 inputs.
 * 4. Accuracy: a table of 4096 inputs a function and range (section 3's
 *    ranges, and near tan's poles and atan's interval edges), each with
 *    its correctly rounded result, generated once by
 *    tests/rbtmathfunctions_reference.c (MPFR at 300 bits; run by hand,
 *    not built, its output tests/data/rbtmathfunctions/reference.inc):
 *    every result within 1 ulp of it, checked as integers (a faithful
 *    result's encoding is the reference's or a neighbour's: the two
 *    encodings, same sign, differ by at most 1), so the test needs no
 *    high precision of its own.
 * 5. Arrays: in place equal to apart; one call against 16 slices; a count
 *    of 0 writes nothing and returns OK.
 * 6. Refusals: NULL arrays with a count above 0, an output overlapping an
 *    input without being it, each RBT_RESULT_INVALID_ARGUMENT with a
 *    message naming the argument, and the output untouched (filled with a
 *    pattern first).
 * 7. Two calls at once from two threads (so -tsan runs it).
 * tests/rbtmathfunctions_bits_tests.c: 2^20 inputs a function and range
 *    from a fixed integer generator (bits, not arithmetic, so the test's
 *    own build can't change them), FNV-1a hashes of the outputs against
 *    tests/data/rbtmathfunctions/bits.inc, recorded on the Mac; --print
 *    to record a meant change, with what changed and why (refused for a
 *    released function: section 2). Built with RABBITTUS_NO_CONTRACT
 *    itself. The inputs include each function's interval edges, tan's
 *    switch at 0.671875 and its odd and even k, atan2's four quadrants
 *    and its scaled carry, and subnormals. Also run with subnormals
 *    flushed, through the control the platform's headers offer (as the
 *    noise's), checking section 4's
 *    stated differences and nothing else. Owed: Windows (MSVC, x64) and
 *    Fedora (GCC and Clang, x64) against the Mac's record; until then the
 *    definition is checked on one machine, and RBTMATH_TODO.md says so.
 * tests/rbtmathmedium_tests.c: unchanged but for its printed CRC, recorded
 *    anew (section 6); its tolerances against the platform's exp hold
 *    (they are 1e-5 and wider, the change some 10^-16).
 * tests/rbtmathfunctions_fit.c: the Remez fitter that made section 5's
 *    polynomials (MPFR; run by hand, not built), with each fit's function,
 *    interval and degree, so a coefficient can be checked or a fit
 *    redone. Unlike the noise's scale search, which CMake builds, these
 *    two need MPFR, which isn't a dependency of Rabbittus and won't become
 *    one for programs run once; each says in its first lines how to build
 *    and run it.
 * Every test runs under the -asan and -tsan presets as the others do.
 * tests/rbtnoalloc_tests.c: each call, 10,000 elements: no allocation.
 * tests/rbt_misuse_tests.c: regenerated from abi.txt.
 * No fuzz target: no bytes are read.
 *
 * ===========================================================================
 * 11. ABI and version
 * ===========================================================================
 *
 * No break: one new header, six new calls and one macro; no struct, no
 * struct type, no enum. ./check.sh --update-abi lists the additions; the
 * misuse generator is rerun. No version is raised while Rabbittus' 0.3 is
 * untagged (card X2). rbtmathfunctions.h is not in rbtrend.h (Rend's
 * alone); a caller includes it. Names follow check_naming.py's
 * <module>_<verb>_<detail> (no handle is taken): rbtmath_compute_exp2 and
 * so on.
 *
 * CMakeLists.txt: src/math/rbtmathfunctions.cpp in the library's list,
 * with COMPILE_OPTIONS "${RABBITTUS_NO_CONTRACT}" as rbtmathnoise.cpp has;
 * rbtmathmedium.cpp given the same and taken out of the "if (NOT MSVC)"
 * block (setting it in both, the later would win silently); the two test
 * targets, each with target_compile_options(... ${RABBITTUS_NO_CONTRACT})
 * as rbtmathnoise_tests and rbtmathnoise_bits_tests have. Considered and
 * not taken: #pragma STDC FP_CONTRACT OFF in the internal header, which
 * Clang honours but GCC ignores in C++ with a -Wunknown-pragmas warning
 * (the presets build warnings-clean); the flag by compiler ID is the one
 * mechanism, and the bits test catches a build that contracts (measured:
 * every function's hash moves).
 *
 * Records when built: RBTMATH_TODO.md (the A61 section: built, what ran
 * where, the bits owed), rabbittus/CLAUDE.md's module map (the Rabbittus
 * Math row), CMakeLists.txt (the new source and rbtmathmedium.cpp under
 * RABBITTUS_NO_CONTRACT; the two test targets, the bits test with it
 * too), RBTREND_TODO.md (the medium's new CRC), and
 * claude_made_decisions.md for what the build chooses alone.
 *
 * ===========================================================================
 * 12. Questions for card A61
 * ===========================================================================
 *
 * Q1. What defines the bits. a. This draft's written evaluation, under 1
 *     ulp measured (recommended: no dependency, a page of definition a
 *     GPU or another language can follow). b. CORE-MATH's correctly
 *     rounded functions, vendored (the bits defined by mathematics; a
 *     port for MSVC and a larger, slower source). c. musl's sources,
 *     vendored (someone else's definition, about 1 ulp, no gain over a).
 * Q2. Precision. a. Double only (recommended: every caller named computes
 *     in double; a float caller widens its floats and rounds the results
 *     back, which gives the correctly rounded float but where the exact
 *     value lies within 2^-29 of a float's ulp of a halfway point, about
 *     one input in 2^28; the same bits everywhere either way). b. Also
 *     float calls, defined as the double result rounded to float. c.
 *     Float calls with their own polynomials (cheaper, a second
 *     definition).
 * Q3. Shape. a. Batch calls only; the scalar evaluations internal to
 *     Rabbittus (recommended). b. Also exported scalar calls. c. Inline in
 *     the header (refused: the caller's flags decide the bits).
 * Q4. exp. a. Its own evaluation, public, the medium moved onto it
 *     (recommended). b. exp as exp2(x log2 e) (measured 438 ulps). c. No
 *     public exp; the medium keeps a private one.
 * Q5. atan2. a. Included (recommended). b. atan only.
 * Q6. tan's range. a. |x| up to 2^20, a NaN past it (recommended). b.
 *     Every double, by Payne and Hanek's reduction. c. Past 2^20 refused.
 * Q7. sin and cos. a. Not now; a card when a caller's draft needs them
 *     (recommended). b. Now, on tan's reduction.
 * Q8. Change after release. a. Bits frozen from the first release, as the
 *     noise's (recommended). b. Only the bound promised.
 */

/* ========================================================================= */
/* math/rbtmatherror.h                                                       */
/* ========================================================================= */

/*
 * [CHANGED, a comment] Its file comment names rbtmathfunctions.h among
 * the headers that include it.
 */

/* ========================================================================= */
/* math/rbtmathmedium.h                                                      */
/* ========================================================================= */

/*
 * [CHANGED, a comment] @par Same bits: "its exponential is Rabbittus
 * Math's own (rbtmathfunctions.h's exp, a range reduction and a fixed
 * polynomial in doubles), not the platform's libm".
 */

/* ========================================================================= */
/* math/rbtmathfunctions.h                                                   */
/* ========================================================================= */

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
 * a thread that flushes subnormals to zero, results that would be
 * subnormal become 0 (exp2, exp, atan2), a subnormal input to atan2 is
 * read as 0, and log2 of a subnormal input is -infinity; nothing else
 * changes (measured on arm64; x86's FTZ and DAZ owed). The status
 * flags after a call are unspecified. A function's results never change
 * once released.
 *
 * @par Definition
 * The header carries section 5 of the draft here in full, in a
 * @verbatim block, as rbtmathnoise.h carries its own: the constants and
 * polynomials in hex, the helpers, and each function's steps, enough for
 * an independent copy to give the same bits.
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
 * [ADDED] Why: tan's argument reduction is exact only below it (section
 * 3); a caller checking its angles, or a GPU copy's tests, read it here
 * rather than from a comment.
 *
 * @brief The largest |x| rbtmath_compute_tan() evaluates: 2^20 radians.
 * Past it, and at infinity, the result is a NaN.
 */
#define RBTMATH_TAN_MAX_ARGUMENT 1048576.0

/**
 * [ADDED] Why: the card's tan (a cube-to-sphere warp's, and any caller's
 * projection or slope that must agree across machines).
 *
 * @brief tan(x) for each element.
 *
 * @param[in]  x      @p count angles in radians, any double. Only
 *                    read during the call. NULL with a count of 0.
 * @param[in]  count  How many; 0 writes nothing.
 * @param[out] out_y  @p count results: tan(x) within 1 ulp; x itself for a
 *                    zero; a NaN for a NaN, an infinity, or |x| above
 *                    RBTMATH_TAN_MAX_ARGUMENT. May be @p x itself (in
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
 * [ADDED] Why: the card's atan (an inverse warp's, a slope's angle).
 *
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
 * [ADDED] Why: a direction's angle, the quadrant included (Q5); every
 * caller with a heading would otherwise write the quadrant logic and its
 * bits itself.
 *
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
 * [ADDED] Why: the card's exp2 (a power's, with log2).
 *
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
 * [ADDED] Why: e^x, the most used of all (a density's fall-off, a decay,
 * Beer-Lambert's transmittance), and the shell medium's; exp2(x log2 e)
 * loses up to 438 ulps (Q4).
 *
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
 * [ADDED] Why: the card's log2 (a power's, with exp2; a level's octave
 * count; a decibel).
 *
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

#endif /* RBTMATHFUNCTIONS_H */
