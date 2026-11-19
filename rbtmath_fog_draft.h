/*
 * rbtmath_fog_draft.h - card A59's Rabbittus Math half: the density of a
 * height fog (a plane's or a sphere's) and its transmittance between two
 * places in closed form, and the density of a local fog volume (a box or a
 * sphere, its edge faded, its density broken up by a baked noise volume),
 * as functions any caller can ask on the CPU (RBTMATH_TODO.md, "Cloud
 * density"'s note that A59 may add a height fog's and a fog volume's
 * density; RBTREND_TODO.md item 56; card A59 in DECISIONS.md, "Decided",
 * 2026-11-17). Rend's half, rbtrend_fog_draft.h, draws what this defines.
 *
 * APPROVED 2026-11-18 with the user, every question as recommended.
 * DRAFT, 2026-11-18, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built yet, not in any target, not
 * compilable. Written on the MacBook (M5 Pro) against what is built:
 * include/math/rbtmathmedium.h (its "Same bits": no libm but floor and
 * sqrt, its exponential its own), include/math/rbtmathnoise.h,
 * include/rbtcore.h; and the drafts it stands on: rbtmath_clouds_draft.h
 * (version 1, this session: rbtmath_noise_volume_bake and _sample, the
 * WORLEY volume a fog volume reads) and include/math/rbtmathfunctions.h
 * (card A61, built on the Mac 2026-11-18 while this was drafted: exp and
 * log2 in double within 1 ulp everywhere; internally rbtfn::exp,
 * src/math/rbtmathfunctions_internal.hpp line 176, which rbtmathmedium.cpp
 * now calls).
 *
 * The decided line (DECISIONS.md, "Decided", 2026-11-17, card A59): "a
 * froxel grid of height fog and local volumes lit by every light and its
 * shadows, staged from analytic height fog; density in Math, the grid in
 * Rend; internals shared with A57's clouds, each its own object."
 * RBTREND_TODO.md item 56: "local fog volumes (boxes, spheres, their
 * density from Math's noise and a falloff) ... analytic height fog first
 * (its density in Rabbittus Math)". And card L74's correction: "haze
 * through A59's height fog where there is no atmosphere", on "a round world
 * and a flat level alike".
 *
 * ===========================================================================
 * Revision notes (seven passes, 2026-11-18, each fixed before the next)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Missing: a round world. Version 0 had a plane's height fog alone
 *    (Unreal's Exponential Height Fog). Card L74's correction puts haze
 *    on round worlds with no atmosphere through this fog, so a sphere's
 *    height is needed: SPHERE added, its transmittance not closed (the
 *    Chapman function), its rule QF1.
 * 2. Wrong: version 0's density grew without bound below the base (Unreal's
 *    exp(-falloff (z - height)) for z below the height), and overflowed a
 *    float 88 scale heights down: a deep valley's fog infinite. Now held at
 *    its base value below the base (a fog lying on the ground), the
 *    transmittance's integral split at the base.
 * 3. More than the need: version 0 had a second fog layer (Unreal's
 *    "Second Fog Data"). One fog a call; a caller wanting two sums two
 *    calls' optical depths. Removed.
 * 4. More than the need: a fog volume's ellipsoid. Decided "boxes,
 *    spheres". Removed.
 *
 * Pass 2 (industry standard, compared).
 * 1. Unreal's Exponential Height Fog (documentation, as remembered):
 *    density x exp(-falloff x (height - fog height)), integrated in closed
 *    form along a view ray, a start distance, a cutoff, a max opacity,
 *    directional in-scattering, a second layer. The density's closed form
 *    here is that, the light's numbers Rend's (rbtrend_fog_draft.h).
 * 2. Unreal's Local Fog Volumes and HDRP's Local Volumetric Fog (as
 *    remembered): boxes and spheres, an edge fade, a 3D noise texture
 *    scrolled for motion, a density each. Here the same, its noise the
 *    WORLEY volume (shared with clouds, as decided: "internals shared").
 * 3. Spherical height fog has no closed transmittance; sky renderers
 *    integrate the Chapman function by approximation (Schüler, "An
 *    Approximation to the Chapman Grazing-Incidence Function for
 *    Atmospheric Scattering", GPU Pro 3, 2012, as remembered) or by tables
 *    (Bruneton). QF1.
 *
 * Pass 3 (the C API to standard).
 * 1. Frozen values with "Frozen:" lines (RBTMATH_HEIGHT_FOG,
 *    RBTMATH_FOG_VOLUME), enums with a refused 0, fields unread by a kind
 *    0 (rbtmathnoise.h's rule).
 * 2. The transmittance takes segments (origins and ends), not rays to
 *    infinity: a fog's light is applied between an eye and a surface;
 *    rbtmath_shell_medium_compute_transmittance takes rays to the shell's
 *    edge because a shell ends. Said why on the call.
 * 3. One volume a call over many points (the batch shape); a caller with
 *    many volumes calls once each, as Rend's grid injects each in turn.
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. No allocation, nothing held: every call writes its outputs only.
 * 2. A volume's density takes points in the volume's own frame (the caller
 *    carries them there once, a 3 x 4 a volume): no matrix in the struct,
 *    whose meaning (which way, scaled or not) would be a convention.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Same bits: A61's exp in double for the exponentials (version 0 used
 *    rbtmathmedium.cpp's own portable_exp, which A61 replaces), rounded to
 *    float; the small-argument series written out; no libm. Contraction
 *    off (card A63).
 * 2. Errors as rbtmathmedium.h's: each field its own check in order, a
 *    point named by index, nothing written on failure.
 *
 * Pass 6 (correctness against the platforms and the code as built).
 * 1. Version 0 named A61 as a draft; re-read at this pass, it is built
 *    (include/math/rbtmathfunctions.h, 2026-11-18), and the shell medium
 *    already takes its exp (src/math/rbtmathmedium.cpp, lines 13 and 139:
 *    rbtfn::exp): this draft calls the same internal function, so the fog
 *    and the medium agree on every exponential.
 * 2. The plane's closed form: tau = rho0 x s x (e^(-h0/s) - e^(-h1/s)) x L /
 *    (h1 - h0) loses every digit as h1 - h0 goes to 0 (two near-equal
 *    exponentials subtracted). Written instead through
 *    g(x) = (1 - e^-x) / x, its series below |x| < 2^-4: no cancellation.
 * 3. A sphere's height along a segment isn't monotonic (it dips to the
 *    closest point): the integral is split at the closest point and at
 *    each crossing of the base radius.
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Each closed form against a dense march in double in the test (2^16
 *    steps); the sphere's approximation's error measured over the test's
 *    range and recorded (QF1's bound).
 * 2. Bits recorded on the Mac, matched on Windows and Linux (card X25).
 * 3. Built after the clouds' Math half (the volume read); A61 is built. No version raise while 0.3 is untagged (card X2).
 *
 * ===========================================================================
 * 1. What this is
 * ===========================================================================
 *
 * Fog near the eye, as decided, is a grid of Rend's fed by two kinds of
 * medium whose density is this header's: a height fog, filling space and
 * thinning with height above a base (a plane's, or a sphere's on a round
 * world), and local volumes, boxes and spheres a caller places (a valley's
 * pool of mist, steam over a vent). Its stage 1 is the height fog alone,
 * analytic: its transmittance between the eye and a surface in closed form
 * (a plane's) or by an approximation (a sphere's), with no grid. The
 * density is the extinction itself, per unit length, one channel: fog is
 * grey in extinction; its colour is its light's (Rend's).
 *
 * General purpose: nothing here knows what the fog is (mist, haze, dust,
 * smoke, a gas giant's deck) or what an answer becomes (a draw distance, an
 * AI's sight, a sensor).
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * A new header, math/rbtmathfog.h, [ADDED]: RBTMATH_FOG_HEIGHT_KIND,
 * RBTMATH_HEIGHT_FOG (frozen), rbtmath_height_fog_compute_density(),
 * rbtmath_height_fog_compute_transmittance(); RBTMATH_FOG_VOLUME_SHAPE,
 * RBTMATH_FOG_VOLUME (frozen), rbtmath_fog_volume_compute_density(). No
 * struct type, no result code, nothing built changed.
 *
 * ===========================================================================
 * 3. The definition (binary32 unless written, rounded once, no contraction)
 * ===========================================================================
 *
 * 3a. HEIGHT. For a point p relative to the fog's origin: PLANE: y =
 * ((p0 up0 + p1 up1) + p2 up2) - base (the parentheses as written); SPHERE:
 * y = sqrt((p0 p0 + p1 p1) + p2 p2) - base.
 *
 * 3b. DENSITY. y >= 0: rho = density x E(-y / scale_height); y < 0: rho =
 * density. E(x) = (float)exp((double)x), A61's exp.
 *
 * 3c. TRANSMITTANCE OF A SEGMENT, from a to b (L = |b - a| in double from
 * the floats). T = E(-tau). PLANE: the heights y_a, y_b; if they lie either
 * side of 0, the segment is split at the crossing, t_c = y_a / (y_a - y_b)
 * of the way; a part below the base contributes density x its length; a
 * part above from height y0 to y1 over length l: with x = (y1 - y0) /
 * scale_height (signed), tau = density x l x E(-y0 / scale_height) x g(x),
 * g(x) = (1 - E(-x)) / x for |x| >= 2^-4, else the series
 * 1 - x/2 + x^2/6 - x^3/24 + x^4/120 (Horner's, in double, from the highest
 * term), every sum and product in double, the result rounded to float.
 * SPHERE: the segment split at its closest point to the centre (if within
 * it) and at each crossing of the base radius; a part below the base as
 * the plane's; a part above by QF1's rule (recommended: Schüler's
 * approximation of the Chapman function, its formula written here from
 * the paper at the build, in double, through A61's exp; its error against
 * a dense march recorded by test 4 and stated on the call).
 *
 * 3d. A FOG VOLUME'S DENSITY at p in its own frame (centred, axes its
 * box's): inside distance i: BOX: min over axes of (half_extent_a - |p_a|);
 * SPHERE: radius - sqrt((p0 p0 + p1 p1) + p2 p2). i <= 0: 0. Fade f =
 * sat(i / edge), or 1 with edge 0. Noise n: with noise_strength 0, 1, the
 * volume not read; else (R, G, B, _) = rbtmath_noise_volume_sample of a
 * WORLEY volume at (p / noise_tile) + offset; w = ((R x 0.625f) +
 * (G x 0.25f)) + (B x 0.125f); n = 1 + (noise_strength x (w - 1)). rho =
 * (density x f) x n.
 *
 * ===========================================================================
 * 4. Cost (estimates; measured by the bench, numbers into RBTMATH_TODO.md)
 * ===========================================================================
 *
 * A height fog's density: one exp (A61's draft measures 3.5 to 4 ns an
 * element, batch calls), 10 to 20 ns a point with the checks. A plane's segment: two exp and a split, 30 to 50 ns. A sphere's:
 * up to three parts, each the approximation (two exp and a sqrt or two),
 * 80 to 150 ns. A volume's density with noise: a volume read (8 texels),
 * 40 to 70 ns. None allocates.
 *
 * ===========================================================================
 * 5. ABI, versions, threads
 * ===========================================================================
 *
 * Additions only: a new header, two frozen structs, two enums, three calls.
 * No version raise while 0.3 is untagged (card X2). Threads: every call
 * reads its arguments and writes its outputs only (rbtmathmedium.h's).
 *
 * ===========================================================================
 * 6. Tests (C, tests/rbtmathfog_tests.c, tests/rbtmathfog_bits_tests.c)
 * ===========================================================================
 *
 * 1. test_density_exact: points at heights -10 s, 0, s, 10 s, 100 s: the
 *    density as worked in the test (held below the base, 1/e a scale
 *    height up).
 * 2. test_plane_against_march: 10^4 segments (level, steep, crossing the
 *    base, long and grazing, 1e-6 s long), each against a 2^16-step
 *    midpoint march in double: within 1e-5 relative in tau.
 * 3. test_plane_small_slope: segments of x across 2^-4 either side: g
 *    continuous within 2^-22, no cancellation (a level segment's tau
 *    exactly density x L x E(-y/s) within an ulp).
 * 4. test_sphere_against_march: segments on a 150 km sphere (fog of 100 m
 *    and 2 km scale heights), from the ground to the horizon, grazing,
 *    crossing the closest point and the base: against the march; the
 *    approximation's largest relative error recorded (QF1's bound).
 * 5. test_volume_density: a box and a sphere: 0 outside, the fade's ramp,
 *    edge 0 a step; noise_strength 0 never reads the texels (NULL allowed
 *    then); with noise, against rbtmath_noise_volume_sample by hand.
 * 6. test_refusals: every check, in order, each its message, nothing
 *    written.
 * 7. Bits: CRCs of 4,096 densities and transmittances of each kind and of
 *    volumes, the Mac's recorded, Windows' and Linux's to match (card X25).
 * 8. rbtnoalloc_tests: each call over 4,096 points, none of ours; the
 *    misuse harness regenerated.
 *
 * ===========================================================================
 * 7. Records when built
 * ===========================================================================
 *
 * RBTMATH_TODO.md: a section "Fog density (card A59)": built, where, the
 * sphere's measured error bound, the bench's numbers, the CRCs, the
 * platforms owed. rabbittus/CLAUDE.md's Math row gains rbtmathfog.h.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card A59's Math half; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * QF1. A sphere's height fog's transmittance: a, Schüler's approximation of
 *      the Chapman function, closed and cheap, its error measured and
 *      stated (recommended: what sky renderers do in a shader; the same
 *      formula on the GPU); b, a fixed quadrature (16 Gauss-Legendre points
 *      a part: exact enough near the eye, poor at the horizon where the
 *      density spans many scale heights); c, the plane tangent below the
 *      eye (wrong by d^2 / 2R in height: 333 m at 10 km on a 150 km world).
 * QF2. Below the base: a, held at the base's density (recommended: bounded,
 *      a fog lying in a valley); b, growing as Unreal's (a deeper valley
 *      denser without end).
 * QF3. A volume's noise: a, the WORLEY volume of the clouds' Math half,
 *      shared, with a strength and a tile (recommended: the decided
 *      "internals shared", one bake); b, a volume of its own kind.
 */

/* ========================================================================= */
/* math/rbtmathfog.h                                       [ADDED: new file] */
/* ========================================================================= */

/*
 * @file rbtmathfog.h
 * @brief Rabbittus Math's fog: a height fog's density and transmittance
 * (over a plane or round a sphere), and a local fog volume's density (a box
 * or a sphere, edge faded, broken by a baked noise volume, rbtmathnoise.h).
 * (Opening comment as rbtmathmedium.h's: general purpose; out of scope:
 * light, which is drawing's; "Same bits" through A61's exp; "Threads";
 * "Results". The definition, section 3 above, in its @verbatim.)
 */

/** @brief What a height fog's height is measured from. */
typedef enum RBTMATH_FOG_HEIGHT_KIND RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_FOG_HEIGHT_KIND_UNKNOWN = 0,
	/** Height along #RBTMATH_HEIGHT_FOG::up above a plane through the
	 * origin. A flat level. */
	RBTMATH_FOG_HEIGHT_KIND_PLANE = 1,
	/** Height above a sphere round the origin. A round world. */
	RBTMATH_FOG_HEIGHT_KIND_SPHERE = 2,
} RBTMATH_FOG_HEIGHT_KIND;
/* [ADDED] Why: card L74's "round and flat worlds alike". */

/**
 * @brief A height fog: extinction per unit length at its base, thinning
 * by 1/e every scale height above, held below. A value.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_HEIGHT_FOG {
	/** A plane or a sphere. */
	RBTMATH_FOG_HEIGHT_KIND kind;
	/** A plane's up: finite, its length within 1e-4 of 1. Read by PLANE;
	 * 0 for a sphere. */
	float up[3];
	/** A plane's base height above the origin along #up, or a sphere's
	 * base radius: finite; a sphere's above 0. */
	float base;
	/** The extinction at the base and below, per unit length: finite, at
	 * least 0. */
	float density;
	/** How far above the base it falls by 1/e: finite, above 0. */
	float scale_height;
} RBTMATH_HEIGHT_FOG;
/* [ADDED] Why: the decided "analytic height fog first (its density in
 * Rabbittus Math)". */

/**
 * @brief A height fog's density (extinction per unit length) at each
 * point.
 *
 * @param[in]  fog         The fog. Only read during the call.
 * @param[in]  points      @p count points relative to its origin, three
 *                         floats each, each finite. NULL with a count of 0.
 * @param[in]  count       How many; 0 writes nothing.
 * @param[out] out_density @p count floats.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL fog, a NULL
 *         array with a count above 0, a field out of its range or set where
 *         unread, or a point not finite (its index named). Nothing is
 *         written on failure.
 */
RBT_RESULT rbtmath_height_fog_compute_density(
	const RBTMATH_HEIGHT_FOG *fog,
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);
/* [ADDED] */

/**
 * @brief How much light gets through a height fog along each segment, from
 * its start to its end: exp(-tau), tau its extinction summed between. A
 * plane's in closed form; a sphere's by the definition's approximation,
 * within the bound its tests record (stated here at the build). Segments,
 * not rays: a fog has no edge (a ray to infinity through a plane's fog
 * from below is 0).
 *
 * @param[in]  fog               The fog. Only read during the call.
 * @param[in]  starts            @p count points relative to its origin,
 *                               three floats each, each finite.
 * @param[in]  ends              @p count points, each finite.
 * @param[in]  count             How many; 0 writes nothing.
 * @param[out] out_transmittance @p count floats, 0 to 1.
 * @param[out] error             Why it failed, when it did. May be NULL.
 *
 * @return As rbtmath_height_fog_compute_density().
 */
RBT_RESULT rbtmath_height_fog_compute_transmittance(
	const RBTMATH_HEIGHT_FOG *fog,
	const float *starts,
	const float *ends,
	uint32_t count,
	float *out_transmittance,
	RBTMATH_ERROR *error);
/* [ADDED] Why: what stage 1 draws (between the eye and a surface), and what
 * a caller's sight asks; the same function. */

/** @brief A fog volume's shape. */
typedef enum RBTMATH_FOG_VOLUME_SHAPE RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_FOG_VOLUME_SHAPE_UNKNOWN = 0,
	/** A box of #RBTMATH_FOG_VOLUME::half_extents. */
	RBTMATH_FOG_VOLUME_SHAPE_BOX = 1,
	/** A sphere of radius #RBTMATH_FOG_VOLUME::half_extents[0] (the others 0). */
	RBTMATH_FOG_VOLUME_SHAPE_SPHERE = 2,
} RBTMATH_FOG_VOLUME_SHAPE;
/* [ADDED] Why: the decided "boxes, spheres". */

/**
 * @brief A local fog volume, in its own frame (centred on its origin, its
 * box along its axes). A value.
 */
/* Frozen: a value, read by the call below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_FOG_VOLUME {
	/** A box or a sphere. */
	RBTMATH_FOG_VOLUME_SHAPE shape;
	/** A box's half sizes, or a sphere's radius then two 0s: each finite,
	 * above 0 where read. */
	float half_extents[3];
	/** How far in from its boundary its density fades from 0 to full:
	 * finite, 0 (a hard edge) to its smallest half size (or radius). */
	float edge;
	/** Its extinction where full, per unit length: finite, at least 0. */
	float density;
	/** How much the noise breaks it up: 0 (even; the noise not read) to 1
	 * (its density times the noise's value). */
	float noise_strength;
	/** The noise volume's tile, in the caller's unit: finite, above 0;
	 * read with #noise_strength above 0, else must be 0. */
	float noise_tile;
} RBTMATH_FOG_VOLUME;
/* [ADDED] Why: the decided local volumes, "their density from Math's noise
 * and a falloff". */

/**
 * @brief A fog volume's density (extinction per unit length) at each
 * point of its own frame.
 *
 * @param[in]  volume       The volume. Only read during the call.
 * @param[in]  noise        A WORLEY volume (rbtmath_noise_volume_bake()),
 *                          @p noise_size^3 x 4 bytes; NULL where
 *                          `volume->noise_strength` is 0.
 * @param[in]  noise_size   Its side, 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE; 0
 *                          with no noise.
 * @param[in]  noise_offset The noise's coordinate at the volume's origin,
 *                          in tiles (wind moves it): three floats, each
 *                          finite and within
 *                          RBTMATH_NOISE_VOLUME_MAX_COORDINATE.
 * @param[in]  points       @p count points in the volume's frame, three
 *                          floats each, each finite.
 * @param[in]  count        How many; 0 writes nothing.
 * @param[out] out_density  @p count floats.
 * @param[out] error        Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL volume, a
 *         NULL array with a count above 0, noise given or not as the
 *         strength asks, a field out of its range, or a point not finite
 *         (its index named). Nothing is written on failure.
 */
RBT_RESULT rbtmath_fog_volume_compute_density(
	const RBTMATH_FOG_VOLUME *volume,
	const uint8_t *noise,
	uint32_t noise_size,
	const float noise_offset[3],
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);
/* [ADDED] */
