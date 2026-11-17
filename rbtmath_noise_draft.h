/*
 * rbtmath_noise_draft.h - coherent noise in Rabbittus Math: gradient and
 * simplex noise, fractal sums of them (fBm, ridged, eroded), cellular
 * noise and its nearest features, and domain warping, in 2D and 3D, from
 * double-precision places, the same bits on every platform
 * (card A56, which names the session that asked).
 *
 * DRAFT, 2026-11-17, version 3 (version 1, the critique's seven passes,
 * and the third round's passes 8 to 14, below). APPROVED by the user
 * 2026-11-17 (card A56, every question as recommended: Q1a to Q8a), with
 * one correction: no version is raised for it while Rabbittus' 0.3 is
 * untagged (card X2); amended by the third critique round, 2026-11-17,
 * at the user's asking. Not built, not in any
 * target, not compilable: one new header, include/math/rbtmathnoise.h,
 * every declaration in it [ADDED], and two struct types added to
 * rbtcore.h. Nothing existing changes meaning. Written on the MacBook (M5
 * Pro) against Rabbittus ede3990 (0.3.0): rbtcore.h (results, struct
 * types 4000+, enums), rbtmath.h, math/rbtmatherror.h,
 * math/rbtmathmedium.h (the model for a pure batch call: a desc, arrays
 * in, arrays out, RBTMATH_ERROR last, "Same bits", "Threads"),
 * math/rbtmathbvh.h, tests/rbtmathbvh_bits_tests.c and
 * tests/rbtmathbvh_bench.c (the bits test's and the bench's pattern),
 * CMakeLists.txt (contraction off per source); against
 * what the first caller's large-worlds draft asks of noise (card A56
 * names it): round worlds of hundreds of km with 0.5 m detail and relief
 * of kilometres, and flat levels; and against Hazel
 * (.refs/HazelRenderer2025, present: Hazel/src/Hazel/Core/Math/Noise.h and
 * .cpp over vendor/FastNoise, and the C# script binding).
 *
 * Every declaration below is marked [ADDED], with why it earns its place.
 * The real header would carry the same Doxygen contract.
 *
 * ===========================================================================
 * Revision notes
 * ===========================================================================
 *
 * Seven passes, 2026-11-17, over version 1, with the lenses of the
 * workspace's critique prompt, each fixed before the next.
 *
 * 2026-11-17: caller names removed (check.sh caller-names), meaning kept.
 *
 * Pass 1 (wrong, missing, more than the need). 1. Section 3 gave the 2D
 * gradient's index two ways ("top 24 bits modulo 24" against section 5's
 * (h >> 8) % 24). Changed: one, section 5's. 2. Cellular noise's
 * gradient, and ridged's at a crest (|n| has no derivative at 0), were
 * never said. Changed: section 3 says both (a feature's direction; sign
 * of 0 taken as 0). 3. The feature placement read "(h >> 10b & 1023)".
 * Changed: written out. 4. More than the need: RBTMATH_NOISE_FRACTION_BITS
 * was public for a GPU copy, which is Rend's, inside Rabbittus, and reads
 * the definition, not a macro. Changed: removed; the 24 bits are in the
 * definition's text. 5. The header's @par Definition was a placeholder.
 * Changed: said plainly that it carries section 5's ten steps verbatim.
 *
 * Pass 2 (what breaks a caller or a neighbour). 1. rbtmatherror.h's
 * comment lists the headers that include it; it would go stale. Changed:
 * [CHANGED, a comment] there too. 2. Struct types 4007 and 4008: no other
 * draft claims them (searched every draft in rabbittus/ and its callers').
 * 3. Rabbittus' CLAUDE.md module map and RBTMATH_TODO.md would not know
 * the header. Changed: section 11's records. Looked at and kept: no Rend
 * header changes, rbtrend.h's umbrella untouched, no existing call's
 * meaning changes; the medium's float points (which keep floats relative
 * to the medium's centre, the caller subtracting the centre in double)
 * are not this draft's.
 *
 * Pass 3 (what CONVENTIONS.md asks). 1. The check order said "desc, then
 * every place", skipping the pointers. Changed: desc field by field, then
 * the pointers in parameter order, then every place. 2. Mixed double and
 * float fields in the desc, unexplained. Changed: said why (frequency and
 * lacunarity multiply places in double; gain, feedback and erosion act on
 * float values). 3. The stack records had no plain-data or hot-record
 * line. Changed: RBT_PLAIN_DATA on both, the octave record
 * RBT_HOT_RECORD of one line. 4. The subnormal test set the CPU's mode with
 * no way that keeps platform names out of a test. Changed: through the
 * control the platform's own headers offer, found by that control's own
 * macro (FE_DFL_DISABLE_DENORMS_ENV, _MCW_DN, _MM_FLUSH_ZERO_ON), skipped
 * with a message where none is. 5. A Doxygen reference to #cell_return
 * outside its struct. Changed: RBTMATH_NOISE_DESC::cell_return. Looked at:
 * no handles, so no overlap check, stale tag or quarantine; every enum's 0
 * refused; no default (period's 0 and the unread fields' 0 say what they
 * mean); no allocation, print or global.
 *
 * Pass 4 (what a reviewer would refuse). 1. The patent's expiry was
 * "remembered". Changed: checked (Google Patents: US 6,867,776, expired;
 * its expiry 2022-01-08 as Wikipedia's simplex noise article gives it).
 * 2. Two units: the field's cellular values in cells, the nearest cells'
 * distances in the caller's unit. Kept, with why in section 3: a field's
 * octaves must sum scale-free; a crater is shaped in metres. 3. ERODED
 * refusing gradients weakens the normals argument for the very layer No
 * Man's Sky's terrain leans on. Kept as the recommendation, exact
 * gradients (second derivatives, about twice the cost) put to the card as
 * an option, never an approximation called a gradient. 4. The hash's
 * xor of per-axis terms could collide along diagonals. Changed: test 8
 * adds the diagonal and the planes x = y, x = -y.
 *
 * Pass 5 (the maths). 1. fBm, ridged and eroded were divided by the sum
 * of their amplitudes, so a layer with one octave fewer (a coarser level
 * of detail) rescaled every value, not only dropped detail: terrain's
 * levels would disagree everywhere. Changed: sums unnormalised, a_0 = 1,
 * range +-S with S = sum(a_i) stated; one octave more only adds its term
 * (test 12, bit for bit). Each octave is clamped to its range, so the sum
 * keeps its bound. 2. RBTMATH_NOISE_MAX_CELLS' reason ("a cell past 2^39
 * would overflow the split") was wrong: the split forms u - c, never
 * u * 2^24, so no overflow before 2^63. Changed: the limit is the
 * fraction's precision (2^-16 of a cell there, 2^-15 after simplex's turn,
 * which can triple a coordinate). 3. The GPU tolerance per octave scales
 * by its amplitude now the sum isn't normalised. Changed. Checked and
 * kept: the precision table's steps, the cellular bounds (F1 <= D, F2 <=
 * 2 D, from the place's own cell and the neighbour across its nearer
 * face), OpenSimplex2's kernels (0.5 in 2D, 0.6 in 3D) and turn, the
 * warp's Jacobian and curl.
 *
 * Pass 6 (who else; tests). 1. The hash and the octave table, the parts a
 * GPU copy must match exactly, had no golden values of their own.
 * Changed: the bits test records them apart. 2. No test held "an octave
 * more only adds its term". Changed: test 12. 3. No word on fuzzing.
 * Changed: no target, with why (no bytes are read; a desc from a game's
 * data is checked field by field, the misuse test covering each). Looked
 * at: terrain, flat terrain, biomes, clouds, textures, motion, CAD sites.
 *
 * Pass 7 (records and the end). 1. The build's records weren't named.
 * Changed: section 11. 2. Section 12's ranges assumed normalised sums.
 * Changed. 3. The card's questions were checked against the text: each
 * option is described here (sections 3, 4, 5, 9).
 *
 * Third critique round, 2026-11-17, at the user's asking (this draft's
 * second): passes 8 to 14, one lens each, each fixed before the next.
 * Version 2 to 3. Every decided answer kept.
 *
 * Pass 8 (general purpose, not one game's). 1. Section 1 and the head
 * named one game's world count and sizes. Changed: said generally. 2.
 * 3D simplex had one fixed turn (OpenSimplex2's "fallback", even in every
 * direction), right on a sphere and wrong for a flat world with an up
 * axis, whose horizontal slices then show the lattice's diagonal (the
 * reason OpenSimplex2 offers its "improve XY / XZ" turns). Flat worlds
 * are as first-class as round ones. Changed: RBTMATH_NOISE_ORIENTATION
 * (EVEN, Y_UP, Z_UP), a field read by 3D simplex only (section 3). 3.
 * Section 7 called clouds "a card tonight". Changed: A57's decision (tiling
 * volumes baked with periods) and the particles' baked curl texture.
 * Looked at: nothing else names a game; a period under 3 cells (a small
 * tiling volume) gives the same feature twice as F1 and F2: said.
 *
 * Pass 9 (industry standard, compared). 1. "Simplex's skewed lattice has
 * no square period" was overstated: Gustavson and McEwan's psrdnoise
 * (JCGT 2022) tiles 2D and 3D simplex noise, on lattices of its own with
 * constrained periods. Changed: said, and why gradient noise stays the
 * tiling kind (any whole period per axis, as Nubis, Unity's pnoise and
 * FastNoise bake it). 2. Unity.Mathematics' noise (cnoise, snoise,
 * pnoise, srdnoise with derivatives, cellular over 3x3 or 2x2) wasn't
 * compared. Changed: section 8. 3. FastNoise Lite's Billow and PingPong
 * weren't said not taken. Changed: said, with why. Kept: a 32-bit seed
 * (FastNoise Lite's, FastNoise2's, Unity's, Unreal's), a caller with
 * 64-bit seeds hashing them down.
 *
 * Pass 10 (the C API). 1. @par Results left out RBT_RESULT_UNSUPPORTED.
 * Changed. 2. No word on overlapping arrays. Changed: no output may
 * overlap an input or another output, but the warp's in place. 3. The
 * warp refused "a moved place past range" after moving places, which in
 * place would already have overwritten some. Changed: refused before
 * anything is written, by the bound |p| + amplitude x the field's range
 * (conservative, said). 4. The finest octave's frequency (frequency x
 * lacunarity^(octaves - 1)) could overflow to infinity with every field
 * in range. Changed: its own check. 5. RIDGED's enumerator said "in
 * [0, 1]" and then "[0, S]". Changed: one octave in [0, 1], the sum in
 * [0, S]. 6. "NULL to skip (and then free)" read as a memory rule.
 * Changed: "costs nothing". 7. Each array's lifetime ("only read during
 * the call") said on every input.
 *
 * Pass 11 (data-oriented design, allocation). 1. The octave record's 48
 * bytes didn't hold its per-octave periods. Changed: seed, amplitude,
 * frequency, offsets as floats (exact: multiples of 2^-24) and the three
 * periods as 64-bit: one cache line at most, the table 2 KiB. 2. The grid
 * call's place was said "one rounding each"; it is two (the product, then
 * the sum), as its call's comment says. Changed. 3. 3D simplex's exact
 * search (pass 13) looks at 16 corners, not OpenSimplex2's 4: cost
 * re-estimated (section 6). Kept: no allocation anywhere, the no-allocation
 * test named.
 *
 * Pass 12 (threads, determinism, errors). 1. "A flushed subnormal changes
 * a result by less than 2^-100" was false for RIDGED's gradient: the sign
 * of a subnormal n is +-1 and of a flushed one 0, a whole octave's term.
 * Changed: the sign taken as 0 where |n| < 2^-100, a normal float, so no
 * mode moves a branch; every other comparison checked (kernels, distances,
 * ties: no subnormal reaches one). 2. The definition assumed float
 * arithmetic is binary32 per operation. Changed: FLT_EVAL_METHOD 0
 * required, asserted in the source. 3. CMake's "if (NOT MSVC)" leaves
 * clang-cl (MSVC true, contraction on by default) contracting. Changed:
 * section 11 asks for the compiler's ID, not MSVC, and MSVC 17.0 or later
 * (earlier versions contracted under /fp:precise). Kept: per-call
 * developer English, no jobs inside, the same bits at 0, 1 and 4 workers.
 *
 * Pass 13 (correctness; the algorithms and the bits). 1. Step 10's "in
 * the order the source writes it" made the source, not the header, the
 * definition: no independent copy could match bits. Changed: section 5a,
 * each kind's evaluation in written order, every constant as a hex
 * literal, the gradient tables in index order; only the four scale
 * constants wait on the build's search (written into the header before
 * the bits are recorded). 2. The 2D directions' phase wasn't given;
 * OpenSimplex2's are at 7.5 + 15 k degrees. Changed: so. 3. OpenSimplex2's
 * 3D evaluates two points per cubic lattice, but up to 4 of one lattice
 * (6 in all) lie inside its kernel (0.6 - r^2): it drops kernel tails, and
 * the value jumps where its choice of point changes. Measured (a throwaway
 * program in the temporary directory, deleted): up to 0.0046 off the full
 * sum and steps of 0.0055 across the planes |x| = |y| (on a range of
 * +-1), a cliff of tens of metres in a kilometres-high layer, and a seam
 * in analytic normals. With 0.5 for 0.6 two points are exact, but the
 * look changes. Changed: OpenSimplex2's lattice, turn, gradients and
 * kernel kept, every point inside the kernel summed (the 8 corners of
 * each lattice's cube, 16 in all, checked against a search of every point
 * within 3 cells over 3 million places). 4. Octave seeds s_i =
 * H(seed + 0x9E3779B9 i): any caller mixing seeds by adding that constant
 * (the terrain draft does, per world) gets world w + 1's octave i equal to
 * world w's octave i + 1, scaled; warp channels had the same shape.
 * Changed: s_i = H(seed ^ X_i), the channel's seed seed ^ Y_a, X_i and Y_a
 * fixed hashes (none equal another, nor with the lattice flip, for 32
 * octaves and 3 channels: checked). 5. A cell's value took h's top 24
 * bits, which hold the feature's z placement (bits 20 to 29): value and
 * place correlated by about 0.25. Changed: from the feature's id. 6.
 * Pa's precedence written with parentheses. Checked and kept: the
 * precision table and MAX_CELLS (2^-16 of a cell at 2^36, 2^-15 after a
 * turn: every turn grows a coordinate by 3 at most); the cellular bounds
 * and the 5^d search (the Manhattan case is tight, exact up to ties of
 * measure zero that the cell order breaks); the 2D kernel (0.5, the
 * triangle's height: exact with three corners); lowbias32's constants.
 *
 * Pass 14 (tests, records, the build). 1. No test held simplex's sum
 * whole. Changed: test 13 (against a double-precision search of every
 * lattice point, and continuity across OpenSimplex2's cut planes). 2. The
 * constants' distinctness and the orientation's checks untested. Changed:
 * bits test and test 10. 3. The warp's refusal in place untested.
 * Changed: test 9. 4. Section 11 still spoke of raising to 0.4.0.
 * Changed: no raise while 0.3 is untagged (X2). 5. What the build must
 * write before recording bits (the scale constants, in hex, in the
 * header) named for the session that builds it; Windows (MSVC) and
 * Fedora (GCC and Clang) runs of the bits test owed and said.
 *
 * Consistency pass, 2026-11-17 (after the third round, across the
 * drafts; no version raise): 1. Pass 2's note said the medium's float
 * points were moved to doubles by L66; L66 keeps them floats relative to
 * the medium's centre, the caller subtracting the centre in double.
 * Corrected. 2. Test 12 said "the finer minus its last term equal to the
 * coarser": a float subtraction isn't exact, so it could fail on a right
 * build (L67's third round). Changed: the coarser is the finer's sum
 * stopped early, tested as the coarser plus the last term, added as the
 * definition adds it, equal to the finer. 3. Card A61 (Rabbittus Math's
 * deterministic tan, atan, exp2 and log2, which L67's terrain needs) is
 * opened apart; this header doesn't take them (section 12's callers
 * name it). 4. Card A63: the contraction guard pass 12 asks for is also
 * owed to Rabbittus' existing maths sources, carded apart.
 *
 * ===========================================================================
 * 1. What is missing, and on which side of the line it goes
 * ===========================================================================
 *
 * Nothing in Rabbittus makes noise. Every procedural thing a game or a
 * tool generates rests on it: terrain's heights (round worlds of
 * hundreds of km, flat levels of a few), biomes' climate fields, craters,
 * clouds' density textures, a procedural texture, a camera's shake, a
 * creature's wander. Each would otherwise write its own, and each copy
 * would give other bits on another machine.
 *
 * The user's line (2026-11-16): "What is pure general purpose math versus
 * what is rendered graphics? That's the distinction I want between
 * Rabbittus Math and Rend." Noise is a function from a place and a seed to
 * a number: pure maths, so Rabbittus Math's, beside the shell medium. A
 * GPU copy (a shader module that evaluates the same functions in a pass,
 * for detail too fine to mesh or for clouds marched per pixel) is
 * drawing's, Rend's, carded later when a design needs one; this header
 * defines the bits it is checked against (section 5).
 *
 * ===========================================================================
 * 2. The design in brief
 * ===========================================================================
 *
 * - A NOISE IS A DESC, NOT AN OBJECT. RBTMATH_NOISE_DESC (based) names a
 *   kind (gradient, simplex, cellular), a fractal (one octave, fBm,
 *   ridged, eroded), its seed, frequency, octaves and the kind's own
 *   parameters. No handle, no create, no state: the lattice's randomness
 *   is an integer hash of the cell and the seed, so a seed costs nothing
 *   to change and two calls with one desc may run at once.
 * - BATCH CALLS INTO THE CALLER'S BUFFERS. A call evaluates one desc at
 *   many places: a list of double places (rbtmath_noise_compute_points),
 *   float offsets about a double origin (rbtmath_noise_compute_offsets), or
 *   a regular grid (rbtmath_noise_compute_grid). Values are floats; the
 *   gradient (the derivative by each axis, per caller unit) is written
 *   too where the caller passes an array for it. Nothing is allocated.
 * - DOMAIN WARPING IS A CALL OF ITS OWN: rbtmath_noise_warp_points moves
 *   places by a vector of noise (one channel per axis) and can give the
 *   move's Jacobian, so a caller nests warps (Quilez's f(p + q(p + r(p))))
 *   and carries gradients through them.
 * - CELLULAR NOISE HAS A SECOND QUESTION: rbtmath_noise_compute_nearest_cells
 *   gives each place's two nearest features (distance, a 32-bit id, the
 *   offset to it), for what is placed by cell: craters, regions, scatter.
 * - LARGE WORLDS: each octave's coordinate is split in double into a
 *   64-bit cell and a fraction of 24 bits, so a place 1e7 m out is as exact
 *   as one at the origin, and everything after the split is float
 *   arithmetic on small numbers (section 4).
 * - THE SAME BITS EVERYWHERE, BY DEFINITION: the split, the hash and the
 *   per-octave seeds and offsets are written in the header; the float
 *   arithmetic after them is built without contraction; no libm call but
 *   sqrt and floor, both exact. A kind's bits never change once released:
 *   a better algorithm is a new kind (section 5).
 *
 * ===========================================================================
 * 3. The algorithms, and why each
 * ===========================================================================
 *
 * GRADIENT NOISE (RBTMATH_NOISE_KIND_GRADIENT). Perlin's improved noise
 * (SIGGRAPH 2002): a gradient at every corner of a square or cubic cell,
 * blended by the quintic fade 6t^5 - 15t^4 + 10t^3, 4 corners in 2D and 8
 * in 3D. It earns its place beside simplex by two things simplex can't do
 * cheaply: it tiles by any whole period per axis (a modulo of the cell
 * index), which baked textures need (clouds' tiling volumes, a material's
 * tiling detail, a particle system's curl texture); and it is the
 * cheapest per octave (about 8 ns a 3D sample, section 6). Simplex noise
 * can be tiled too (Gustavson and McEwan's psrdnoise, JCGT 2022), but on
 * a lattice of its own with periods constrained by the skew, not
 * OpenSimplex2's; the industry's baked volumes are gradient and cellular
 * (Nubis, Unity's pnoise, FastNoise's seamless images by blending). Its
 * cost: a visible alignment to the axes, so isotropic uses take simplex.
 * Gradient sets (section 5a): in 3D, Perlin's twelve cube edges padded to
 * sixteen, in his reference grad()'s order (index: the hash's top four
 * bits); in 2D, the 24 unit directions at 7.5 + 15 k degrees, as
 * OpenSimplex2's (none along an axis or a diagonal; index: section 5, step
 * 9). Range: one octave in [-1, 1].
 *
 * SIMPLEX NOISE (RBTMATH_NOISE_KIND_SIMPLEX). OpenSimplex2 (K. Spencer,
 * "KdotJPG", 2019 to 2022; released into the public domain, CC0 and the
 * Unlicense, so no licence binds what is written from it): in 2D the
 * triangular lattice of classic simplex noise with a radial kernel
 * (0.5 - r^2)^4; in 3D the body-centred cubic lattice, as two cubic
 * lattices offset by half a cell, reached after an orthogonal turn of the
 * place (section 5a), kernel (0.6 - r^2)^4, gradients from
 * OpenSimplex2's 48.
 * The turn is the caller's choice (RBTMATH_NOISE_ORIENTATION), as
 * OpenSimplex2 offers it: EVEN (its "fallback", r = (x + y + z) * 2/3,
 * x' = r - x, ...: no preferred direction, for a sphere's surface or a
 * volume), Y_UP or Z_UP (its "improve XZ / XY": the planes across that
 * axis kept far from the lattice's alignment, for a flat world's 3D
 * ground, caves or slab of cloud with that axis up). 2D has no turn.
 * One departure from OpenSimplex2's reference, a correction: it sums two
 * points of each cubic lattice, but up to four of one lattice (six in
 * all) lie inside the kernel, so it drops kernel tails and its value
 * steps, by up to 0.0055 of its range, where its choice of point changes
 * (across the planes |x| = |y| of the turned frame; measured, revision
 * notes, pass 13). Here every lattice point inside the kernel is summed
 * (the 8 corners of each lattice's cube, 16 in all), so the field and
 * its gradient are continuous; the look is OpenSimplex2's. Why it, and
 * not classic simplex:
 * - Isotropy. Classic 3D simplex shows directional artefacts in planar
 *   slices and along the skew's diagonal; the BCC lattice is the most
 *   isotropic of the cheap lattices, which a sphere's surface (no
 *   preferred plane) shows.
 * - Patents and licences: classic simplex noise in 3D and above was US
 *   6,867,776 (Perlin, "Standard for perlin noise"), which OpenSimplex was
 *   made to avoid; it has expired (Google Patents: "Expired - Lifetime";
 *   2022-01-08), so either is free now. Recorded so the choice is on
 *   quality, not on the patent.
 * - Written here from the algorithm's description and the papers, not
 *   vendored: no third-party file enters Rabbittus, and the bits are this
 *   header's definition, not a library's version.
 * OpenSimplex2S (the smoother variant, a larger kernel and more points:
 * about 1.5 to 2 times the cost) is not taken; an enumerator later if a
 * caller needs its smoothness. Range: one octave in [-1, 1].
 *
 * CELLULAR NOISE (RBTMATH_NOISE_KIND_CELLULAR). Worley's (SIGGRAPH 1996):
 * one feature point per cell, placed by the hash within the cell by a
 * jitter (0: every feature at its cell's centre; 1: anywhere in the
 * cell); F1 and F2, the distances to the nearest and second nearest
 * feature, under a distance kind (Euclidean, Manhattan, Chebyshev). As a
 * field it returns F1, F2, F2 - F1 (the cells' borders) or the nearest
 * feature's value (flat cells), in cells of its octave. The nearest two
 * are exact at any jitter: the 3^d cells around the place are searched,
 * then any cell of the 5^d around it whose nearest possible feature is
 * nearer than F2 (rare: measured, below). Searching 3^d alone (FastNoise
 * Lite's, Blender's, Unreal's lower qualities) misses a nearer feature
 * outside: worked out by placing every feature at its worst, it is exact
 * only to a jitter of 0.49 in 3D and 0.66 in 2D; at jitter 1, 3 in 2
 * million random places get the wrong F1 and 56 the wrong F2 (3D; a
 * throwaway program in the temporary directory, deleted). 5^d is exact to
 * jitter 1 by the same worst-case check, so the definition is "the true
 * two nearest", which any correct search, a GPU's included, reproduces.
 * Ties go to the earlier cell (z, then y, then x, ascending), whatever
 * order a search visits cells in. With a period under 3 cells (a small
 * tiling volume) the search reaches the same cell twice across the wrap,
 * so F1 and F2 may be two images of one feature, the same id: a tiling
 * field's truth, said. A cell's value (CELL_VALUE) is drawn from its
 * feature's id, so a caller reading ids gets the same value.
 * Its gradient: for F1 and F2, the feature's direction (Euclidean: the
 * unit vector from the feature to the place; Manhattan: each axis's sign;
 * Chebyshev: the largest axis's sign, ties to the lower axis; 0 at the
 * feature itself), times the frequency; F2 - F1 their difference;
 * CELL_VALUE 0. It jumps where the nearest feature changes, as the value's
 * slope does. As a field its values are in cells of the octave, so
 * octaves sum scale-free; rbtmath_noise_compute_nearest_cells() gives
 * distances and offsets in the caller's unit, since what is placed by
 * cell (a crater's rim) is shaped in the world's.
 *
 * FRACTALS (RBTMATH_NOISE_FRACTAL). Each sums octaves i = 0 to n - 1, the
 * frequency multiplied by the lacunarity and the amplitude by the gain
 * each octave, each octave with its own seed and offset (section 5), so
 * that no two octaves share a lattice point at the origin (where every
 * gradient octave is 0 at once: the "pinch" seen at the origin of
 * unshifted fBm).
 * Sums are not normalised: a_0 = 1, a_i = a_{i-1} * gain, and the range
 * is the octave's times S = sum(a_i) (stated per fractal), so a caller
 * that drops the finest octaves (a coarser level of detail) loses only
 * their terms: every value of the coarser sum is the finer one's partial
 * sum, bit for bit (summed in octave order). FastNoise Lite and libnoise
 * divide by S, which rescales everything when the octave count changes.
 * - SINGLE: one octave, n = 1.
 * - FBM: sum(a_i n_i), in [-S, S] (the kind's octave range times S).
 * - RIDGED: Musgrave's ridged multifractal ("Texturing and Modeling", 3rd
 *   ed., ch. 16; libnoise's RidgedMulti): t_i = (1 - |n_i|)^2 w_i,
 *   w_0 = 1, w_{i+1} = clamp(t_i * ridge_feedback, 0, 1); sum(a_i t_i), in
 *   [0, S], S on a crest every octave agrees on. The feedback makes
 *   detail gather on ridges and fade in valleys: mountains. Its gradient
 *   carries the weights' (forward, as each octave is summed), the sign
 *   of n taken as 0 where |n| < 2^-100 (a crest's top; a normal float, so
 *   a thread flushing subnormals takes the same branch).
 * - Not taken: FastNoise Lite's Billow (sum of 2|n_i| - 1, ridged's
 *   inverse without the feedback) and PingPong (each octave folded by a
 *   triangle wave). Each is an append to the enum when a caller needs one;
 *   neither can be made from these sums by the caller's arithmetic, since
 *   both fold every octave, so the need would be real.
 * - ERODED: Quilez's derivative-damped fBm ("morenoise", 2008; the slope
 *   erosion of No Man's Sky's terrain, GDC 2017): each octave divided by
 *   1 + erosion |D_i|^2, D_i the sum of the octaves' gradients so far (in
 *   each octave's own cells, octave i's included). Steep ground gets less
 *   fine detail, as erosion leaves it: eroded-looking terrain at the cost
 *   of the gradients, with no simulation. In [-S, S].
 * Ridged and eroded take a gradient or simplex base only (a cellular
 * distance has no sign to fold, and no gradient worth damping by).
 *
 * DERIVATIVES. Every kind gives its analytic gradient, per caller unit,
 * where the caller asks (out_gradients not NULL); it costs about half
 * again a value's time. They earn their place three ways:
 * - Terrain normals that are a function of the place alone: a vertex's
 *   normal from the gradient is the same in every chunk that shares the
 *   vertex, at any level, so lighting has no seam where chunks meet; a
 *   normal from the mesh needs the neighbours' heights, which a chunk's
 *   border doesn't have (a ring of extra samples, 4 times the work per
 *   normal in 3D by central differences).
 * - ERODED needs them inside the sum.
 * - Warps carried through: the warp's Jacobian and the field's gradient
 *   give the warped field's gradient (chain rule), and a curl (curl noise
 *   for particles or wind) is read off the Jacobian.
 * The one exception: ERODED's own gradient would need each octave's second
 * derivatives (the damping depends on the gradients so far), so it is
 * refused (RBT_RESULT_UNSUPPORTED, the reason in the message); a caller
 * wanting eroded normals samples neighbours. An approximate gradient
 * (Quilez's, which drops the damping's derivative) is not offered as a
 * gradient: it isn't one.
 *
 * DOMAIN WARPING. p' = p + amplitude (V_x(p), V_y(p)[, V_z(p)]), each
 * channel the warp's field with its own derived seed (section 5). A call
 * of its own, places in and places out, so warps nest and a caller
 * warps once and samples many fields at the warped places. FastNoise
 * Lite's "progressive" warp (each octave warping the next) is a warp per
 * octave, nested; its "independent" warp is one warp whose field is
 * fractal. Its Jacobian (d p' / d p, row-major, per place) is written
 * where asked; the warped field's gradient is then J^T times the field's
 * gradient at p', and a 3D warp's curl is (J[2][1] - J[1][2],
 * J[0][2] - J[2][0], J[1][0] - J[0][1]) over the amplitude.
 *
 * NOT HERE, each with why:
 * - 1D: a 2D field read along a line (y fixed) is the same function.
 * - 4D: its one use is a 3D field looping in time, or a 2D one tiling
 *   through a torus; tiling is gradient and cellular noise's period
 *   here, and no caller loops a 3D field in time yet. An enumerator and a
 *   dimension later, carded when one does.
 * - Value noise (cheaper, blockier) and OpenSimplex2S: no caller needs
 *   them; enumerators later.
 * - Band-limited noise (Cook and DeRose's wavelet noise, Gabor noise) for
 *   anti-aliased textures: drawing's concern when a GPU copy exists.
 * - Composing layers (sums, masks, curves, selects: libnoise's modules,
 *   FastNoise2's node graph): the caller's. A terrain generator is the
 *   game's data (the terrain draft's), made of these calls and its own
 *   arithmetic; a graph here would be a second language for it.
 * - Erosion by simulation (water, thermal): the terrain's, over a
 *   heightfield.
 *
 * ===========================================================================
 * 4. Large worlds: the lattice coordinate
 * ===========================================================================
 *
 * Card L66 (section 12) asks: double inputs in a world's own space, to at
 * least 1e7 m; cells as 64-bit integers; the fraction within a cell as a
 * float; outputs float. At 250 km a float place steps by 16 mm, which a
 * 10 cm octave would show as terraces; FastNoise Lite's double build still
 * floors to a 32-bit int, which wraps past 2^31 cells (2.1e9: at 1 cm
 * cells, 21,000 km).
 *
 * Each octave's coordinate u = p * f_i + o_i is formed in double (a
 * product, then a sum: two roundings, never fused), then split:
 * - the cell c = floor(u), a 64-bit integer (exact: |u| is far below 2^53);
 * - the fraction t = floor((u - c) * 2^24) * 2^-24, exact in double and
 *   then exact in float: a multiple of 2^-24 in [0, 1).
 * Everything after the split is float arithmetic on small numbers (each
 * below 4 in magnitude).
 * So the noise's precision within a cell is the same everywhere: 2^-24 of
 * a cell, wherever double resolves it, which is to 2^28 cells from the
 * origin (2.7e8: 1e7 m at 26 cells a metre, a 3.8 cm octave), and coarser
 * past it: 2^-16 of a cell at RBTMATH_NOISE_MAX_CELLS, 2^36 cells (6.9e10:
 * 1e7 m at 6,900 cells a metre, a 0.15 mm octave), 2^-15 after simplex's
 * turn, which can triple a coordinate. That is the limit's reason: past
 * it the fraction keeps fewer than 15 bits and the noise would step
 * visibly within a cell. The 64-bit cell itself would hold to 2^63 (the
 * split forms u - c, never u * 2^24). Past the limit a place is refused,
 * its index named.
 *
 * | Place from origin | Finest octave | Cells there | Double's step, in cells | Fraction's step |
 * |-------------------|---------------|-------------|-------------------------|-----------------|
 * | 250 km (a world)  | 0.5 m         | 5e5         | 1e-10                   | 2^-24 (6e-8)    |
 * | 250 km            | 1 cm          | 2.5e7       | 4e-9                    | 2^-24           |
 * | 1e7 m             | 1 cm          | 1e9         | 1.2e-7                  | 2^-23           |
 * | 6.5e7 m (a system)| 1 m           | 6.5e7       | 7e-9                    | 2^-24           |
 *
 * The 24 bits match float's own resolution of a number in [0.5, 1), so the
 * quantisation costs nothing the float arithmetic after it could keep,
 * and it does three more things: no fraction is a subnormal (the smallest
 * is 2^-24), a GPU copy is handed the same fraction exactly (it is a
 * float), and the fraction of a float input converted to double is the
 * same bits as the double input's.
 *
 * ===========================================================================
 * 5. The same bits everywhere
 * ===========================================================================
 *
 * The definition, which the header carries in full (@par Definition) so a
 * GPU copy or another language can reproduce it:
 *
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
 * 6. Simplex only, in double (section 5a's constants): 2D, the skew
 *    s = (u0 + u1) * F2, u[a] = u[a] + s; 3D, the orientation's turn of
 *    section 5a.
 * 7. The split of section 4: c[a] = floor(u[a]) as int64, t[a] the
 *    fraction to 24 bits, as float.
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
 *    (h >> 8) % 48 into the 48 (tables in section 5a). A cellular
 *    feature's place in its cell, per axis b = 0, 1, 2:
 *    0.5 + jitter * (((h >> (10 * b)) & 1023) * 2^-10 - 0.5), in float,
 *    the product and the sum each rounded. A feature's id:
 *    id = H(h ^ 0x5BD1E995). A cell's value: (id >> 8) * 2^-23 - 1, in
 *    [-1, 1), exact.
 * 10. Everything after step 7 in float (binary32, each operation rounded
 *    once: FLT_EVAL_METHOD 0), as section 5a writes it, with no
 *    contraction: each kind's octave, its scale constant applied last,
 *    then clamped to its range (its gradient 0 where the clamp acts), then
 *    the fractal's sum in octave order from +0. Scale constants are found
 *    by search for the octave's maximum (as OpenSimplex2's were), recorded
 *    with the search's program, so the clamp never acts in 10^9 measured
 *    samples per kind and dimension; it is there so the range is a
 *    guarantee, not a measurement.
 *
 * How the build keeps it: src/math/rbtmathnoise.cpp is built with
 * floating-point contraction off (-ffp-contract=off on Clang and GCC, as
 * its neighbours are, chosen by the compiler's ID so clang-cl, which CMake
 * counts as MSVC and which contracts by default, gets it too; MSVC 17.0
 * and later contract only under /fp:contract or /fp:fast), never with
 * fast-math, on x64 and arm64 (no x87; the source static_asserts
 * FLT_EVAL_METHOD == 0). No libm call but floor
 * (exact) and sqrt (correctly rounded by IEEE 754 everywhere). No
 * reassociation: a block of points is processed lane by lane in the same
 * order of operations as one point, so the compiler's vectorisation (or
 * none) can't change a bit. Constant tables (gradients) are const, in
 * read-only data: no writable static.
 *
 * Subnormals: the bits are defined under IEEE 754's default environment
 * (round to nearest, subnormals kept), which is every platform's default.
 * A caller whose thread flushes subnormals to zero (x86's FTZ and DAZ,
 * arm64's FPCR.FZ, set by some audio and game code) can see a result
 * change only where a subnormal arises (a product of tiny values: a
 * near-cancelled octave times a small amplitude); the change, in a value
 * or a gradient, is below 2^-100 in absolute value. No branch reads a
 * value that can be subnormal: kernels' a and cellular distances (sums
 * and differences of squares of offsets that are 0 or at least about
 * 2^-30, so 0 or normal), the clamp's bounds, and RIDGED's sign, taken as
 * 0 below 2^-100 for this reason. Said in the header; tested with the mode set
 * through whatever control the platform's own headers offer, found by
 * that control's macro, never a platform's name (FE_DFL_DISABLE_DENORMS_ENV
 * in fenv.h, _MCW_DN in float.h, _MM_FLUSH_ZERO_ON in xmmintrin.h), the
 * test skipping with a message where none is.
 *
 * Frozen per kind: a world is its seed and its generator, so a kind whose
 * bits changed would change every world made with it, and a saved game's
 * terrain under the player. From the first release a kind's bits never
 * change; a better algorithm is a new enumerator, the old one kept. The
 * bits test's recorded values are the record (as Rabbittus Math's BVHs'
 * are). This is what makes the card hard to reverse.
 *
 * A GPU copy (Rend's, later) is checked against these calls with a
 * tolerance, not bits: Metal compiles shaders with fast math and every GPU
 * may fuse a multiply and an add. What is exact on a GPU as here: the
 * cells, the hashes, every gradient, feature and offset choice (32-bit
 * integer arithmetic), and the fraction where the GPU is handed it or
 * makes it from a float offset within a chunk whose split was made here.
 * What differs: the float arithmetic after the split, by a few ulps an
 * operation; the tolerance it is checked to, 2^-18 times an octave's
 * amplitude, summed over the octaves (about 64 float ulps of 1 an octave,
 * against some 40 operations), is stated with the GPU copy's card.
 *
 * ===========================================================================
 * 5a. The evaluation, written out (normative; the header carries it)
 * ===========================================================================
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
 *   SCALE_GRADIENT_2D, SCALE_GRADIENT_3D, SCALE_SIMPLEX_2D,
 *   SCALE_SIMPLEX_3D (F): the search's, written here by the build before
 *   the bits are first recorded.
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
 * Only the scale constants are left to the build: written into this
 * section of the header, as hex, before the bits test is first recorded,
 * and frozen with the kinds.
 *
 * ===========================================================================
 * 6. Batch shapes, layout and cost
 * ===========================================================================
 *
 * One call evaluates one desc at many places, never one place across the
 * boundary. Inside, the call:
 * 1. checks the desc (once) and every place (finite, within range: one
 *    pass of compares, before anything is written, so a failed call writes
 *    nothing; a warp checks each place against the most its field can move
 *    it, so in place too nothing is written before a refusal);
 * 2. builds the octave table on the stack: per octave its seed and
 *    amplitude, its frequency (double), its offsets (three floats, exact)
 *    and its periods (three 64-bit integers): 56 bytes, one cache line at
 *    most, 32 octaves at most: under 2 KiB;
 * 3. walks the places in blocks of 16, structure of arrays on the stack
 *    (each axis's cells, int64, and fractions, float: 576 bytes), octave
 *    by octave, accumulating each place's sum and gradient in float.
 * Nothing allocates, on any path. The arrays it walks are the caller's
 * (places in, values and gradients out) and its own stack arrays, plain
 * data: the octave record and the block's arrays each with RBT_PLAIN_DATA,
 * the octave record RBT_HOT_RECORD of one cache line (56 bytes in 64).
 * The values array and the gradients array are separate (structure of
 * arrays): a caller often wants values alone, and a gradient's three
 * floats are read together, so they are interleaved per place.
 *
 * The grid call needs no array of places (a 4096^2 texture is 400 MB of
 * double places otherwise) and lets the implementation reuse a cell's
 * corner hashes along a row; its bits are the points call's at
 * origin + (double)i * step (the product rounded, then the sum), tested.
 *
 * No jobs inside: each place is independent, so a caller splits a batch
 * across its scheduler's tasks (card X3) and the result is the same bits
 * whatever the split, tested at no scheduler and 1 and 4 workers.
 *
 * Cost, estimated (a throwaway C program in the temporary directory,
 * scalar, one core of the M5 Pro, -O2 with contraction off, deleted): one
 * octave of 3D gradient noise 7.9 ns; of classic 3D simplex 10.3 ns (a
 * proxy for OpenSimplex2's 3D: two lattices' 8 corners each, a distance
 * test per corner and at most 6 kernel terms, about 20 to 25 ns expected;
 * OpenSimplex2's reference, 4 points, would be about 15); 3D cellular
 * F2 - F1 over 27 cells 43 ns (branchless minimum; 178 ns with branches);
 * 8 octaves of classic simplex fBm 126 ns (of the exact OpenSimplex2,
 * about 160 to 200 expected). Places at 1e7 m
 * cost the same as at the origin. Gradients add about half. Blocks of 16
 * that the compiler vectorises should give 2 to 4 times these (to be
 * measured). Measured when built, in Release, by rbtmathnoise_bench
 * (section 10); the numbers go in RBTMATH_TODO.md.
 *
 * ===========================================================================
 * 7. Who else uses it
 * ===========================================================================
 *
 * Written for any caller, named in no caller's terms:
 * - Round-world terrain (the terrain draft): heights from layers on a
 *   cube-sphere, points in double in the body's space, gradients for
 *   normals, craters from the nearest cells.
 * - Flat terrain and 2D maps: the grid call, 2D; a flat world's 3D ground
 *   (caves, overhangs) with simplex turned Y_UP or Z_UP.
 * - Biomes: a climate field (moisture, temperature's noise) on the same
 *   points; regions as cells (the nearest cells' ids).
 * - Clouds (card A57, decided): Perlin-Worley tiling volumes baked by the
 *   grid call with periods on the caller's jobs, as Schneider's Nubis
 *   bakes them; a cloud density model on the CPU reading the same values.
 * - Fog volumes (card A59) and GPU particles' curl texture: tiling 3D
 *   grids, the curl from three gradient fields' gradients.
 * - Procedural textures (a cook stage, later): the grid call, 2D, tiled.
 * - Motion: a camera's shake or a creature's wander, a 2D field read
 *   along time; curl noise for particles and wind, from a warp's
 *   Jacobian.
 * - A CAD caller: procedural materials and a site's surroundings, if it wants
 *   them; nothing here is a game's.
 *
 * ===========================================================================
 * 8. Compared
 * ===========================================================================
 *
 * - FastNoise Lite (J. Peck, MIT; Godot 4's FastNoiseLite resource wraps
 *   it): one state struct, a call per sample (GetNoise(x, y[, z])),
 *   OpenSimplex2 and 2S, Perlin, cellular (Euclidean, squared, Manhattan,
 *   hybrid; cell value, distance, distance 2 and their combinations;
 *   jitter), value and cubic value noise; fractals FBm, Ridged, PingPong;
 *   domain warp progressive and independent. Its double build floors to
 *   32-bit ints; a 3^d cellular search; no derivatives; bits depend on the
 *   compiler (contraction, its own fast floor). Taken from it: the kinds
 *   and fractals worth having, the prime-and-xor lattice hash's shape
 *   (with a stronger finaliser here), the warp's two modes as composition.
 *   Not taken: a call per sample, state in an object, 32-bit cells.
 *   Godot adds get_image (a grid, per pixel through the same call) and
 *   seamless images by blending, where this has periods.
 * - FastNoise2 (SIMD, a node graph): batch evaluation over a grid or a
 *   position list is its shape too; its graph is composition, which here
 *   is the caller's.
 * - libnoise (Bevins, LGPL): a graph of modules (Perlin, RidgedMulti,
 *   Billow, Voronoi, Turbulence, Select) with virtual calls per sample,
 *   double inputs but 32-bit cells. RidgedMulti is the ridged here. Not
 *   taken: the module graph, the LGPL.
 * - Unreal: the material Noise node (simplex, Perlin, gradient, value,
 *   Voronoi with a search "quality" from 2^3 to 4^3 cells that trades
 *   exactness for cost; turbulence, ridged) on the GPU; FMath::PerlinNoise
 *   1D to 3D on the CPU, float, a permutation table; Niagara's curl noise.
 *   With Large World Coordinates (5.0), materials take a world position as
 *   a tile and an offset (FLWCVector3) and its noise functions are given
 *   LWC variants: the same split as section 4, a tile where this has a
 *   cell.
 * - Unity.Mathematics' noise (a port of Gustavson and McEwan's webgl-noise):
 *   cnoise (classic Perlin), pnoise (Perlin with a period: the tiling
 *   kind), snoise (simplex, and a variant giving its gradient), srdnoise
 *   (2D simplex with rotating gradients and derivatives), cellular (F1 and
 *   F2 over 3x3 cells, or 2x2 "fast", both inexact at full jitter); float
 *   only, a call per sample, Burst-vectorised by the caller. Confirms the
 *   set (periodic gradient noise, simplex, cellular, derivatives); not
 *   taken: float places, the inexact searches.
 * - psrdnoise (Gustavson and McEwan, JCGT 2022): simplex noise tiled with
 *   periods and its analytic derivatives, on GLSL; its tiling needs its own
 *   lattice orientation and constrains the periods, so here the tiling
 *   kind stays gradient noise (section 3).
 * - OpenSimplex2's reference (Java, C#, Rust ports; FastNoise Lite's
 *   OpenSimplex2 is the same algorithm): its 3D sums two points a cubic
 *   lattice and steps where its choice changes (section 3); kept here:
 *   its lattice, turns, gradients and kernel; changed: every point inside
 *   the kernel summed, and the hash (section 5) for 64-bit cells.
 * - Inigo Quilez: analytic derivatives of value and gradient noise and
 *   the derivative-damped fBm (ERODED); domain warping f(p + 4 q(p)),
 *   nested; a rotation between octaves to break alignment (here an
 *   offset per octave; a rotation costs a 3x3 product in double per
 *   octave, and simplex has no alignment to break).
 * - Hazel: Hazel/Core/Math/Noise wraps the original FastNoise (Auburns,
 *   MIT): an object per noise, 2D, float, one sample a call, simplex fixed
 *   at construction; a static PerlinNoise over a static FastNoise whose
 *   seed is global state (SetSeed); exposed to C# scripts (a sandbox's
 *   MapGenerator). No 3D, no batch, no determinism stated. Nothing of its
 *   shape is taken: no object, no global seed, batch calls.
 * - No Man's Sky (S. Murray, "Building Worlds Using Math(s)", GDC 2017):
 *   one "uber noise" with domain warping, slope erosion from analytic
 *   derivatives, ridges and plateaus, a universe from 64-bit seeds that
 *   must be the same on every platform: the case for derivatives, ERODED
 *   and frozen bits.
 * - Space Engine (V. Romanyuk): planets on the GPU from fBm, ridged and
 *   "iqTurbulence" with craters from cellular layers on a cube-sphere
 *   quadtree, places in double on the CPU and floats per node on the GPU.
 *   Outerra: real elevation refined by fractal detail on the GPU, places
 *   in double, rendering relative to the camera. Both: the CPU in double
 *   and the GPU in float per patch, as section 5 allows for a GPU copy.
 *
 * ===========================================================================
 * 9. Alternatives, and why not
 * ===========================================================================
 *
 * - A noise object (create with a seed, permutation tables inside: Perlin's
 *   original, libnoise, FastNoise): per-seed state of 512 bytes or more,
 *   a handle and a lifetime for what is a pure function. An integer hash
 *   gives the same randomness with no state; its cost is a few integer
 *   multiplies per lattice point.
 * - 32-bit cells (FastNoise Lite's): refuse past 2^31 cells, 21,000 km at
 *   1 cm octaves; 64-bit costs two more multiplies per axis per octave.
 * - An unquantised float fraction: subnormal fractions near a lattice
 *   point, and a fraction a GPU can't be handed exactly.
 * - The warp inside the desc (fused, FastNoise Lite's): no nesting, no
 *   warping once for many fields, and a desc that grows a desc.
 * - Float-only places: card L66 rules it out (16 mm steps at 250 km).
 * - A Rend shader first: CPU work (meshing, collision, placing) needs the
 *   values on the CPU; Rend's copy is checked against this.
 *
 * ===========================================================================
 * 10. Tests
 * ===========================================================================
 *
 * tests/rbtmathnoise_tests.c (C, through the public API, no framework):
 * 1. Ranges: 10^8 random places per kind, fractal and dimension: every
 *    value within the documented range; the largest seen reported, and the
 *    clamp never acting (a counter in the test: values equal to a bound).
 * 2. Gradients against central differences in double (a step of 1e-4 of a
 *    cell), at the origin and at 1e7 m, every kind and fractal that gives
 *    one, within 1e-3 relative.
 * 3. Continuity across cell borders: at c - 2^-24 and c, the values within
 *    the gradient's bound; at the 64-bit cells -1 and 0 and 2^32 - 1 and
 *    2^32; and across the octave offsets.
 * 4. Periods: a grid of one period and the next, the same bits (a
 *    frequency of 1/8 and periods of 64 cells, so the places are exact);
 *    a period with simplex, or with a lacunarity not a whole number,
 *    refused.
 * 5. The shapes agree: offsets about an origin equal points at
 *    origin + offset; a grid equals points at origin + i * step; bit for
 *    bit; a float place widened gives the double place's bits.
 * 6. The split of a batch: one call against the same places in 16 calls,
 *    and on Rabbittus Jobs' pool at 1 and 4 workers: bit for bit.
 * 7. Cellular against brute force: 10^6 random places at jitters 0.25,
 *    0.5, 0.75 and 1, F1, F2, their ids and offsets against a search of
 *    the 7^d cells, bit for bit; ties ordered as stated.
 * 8. Seeds and octaves: correlation between seeds s and s + 1, and between
 *    octaves, below 0.01 over 10^6 samples; the hash's choices uniform
 *    (chi-square of the gradient index over a 64^3 block at the origin,
 *    at 1e9 cells and across 2^32, and along the diagonal and the planes
 *    x = y and x = -y, under the 99% bound). A throwaway check of the hash
 *    tonight (temporary directory, deleted): chi-square 9 to 19 on 15
 *    degrees of freedom (99% bound 30.6) at the origin, 1e9 cells, 2^32
 *    and 2^38; one input bit flipped flips 16.03 output bits on average;
 *    seeds 5 and 6 pick the same gradient 6.21% of the time (6.25% ideal).
 * 9. Warps: a warp's places equal three channels computed and added by
 *    the test, bit for bit; its Jacobian against central differences; in
 *    place (out_points == points) equal to apart; a place whose bound
 *    (|p| + amplitude x the field's range) passes the limit refused in
 *    place with the array unchanged.
 * 10. Misuse: every desc field out of range, each refused with
 *    RBT_RESULT_INVALID_ARGUMENT and a message naming the field, and
 *    nothing written (outputs filled with a pattern and checked after); a
 *    field the kind or fractal doesn't read set non-zero; a place not
 *    finite or past range (its index named); NULL arrays with a count; a
 *    count of 0 (OK, nothing written); ERODED with gradients
 *    (RBT_RESULT_UNSUPPORTED); a wrong base type; 3D simplex with
 *    orientation 0, and an orientation set on any other kind or in 2D; a
 *    lacunarity whose finest octave's frequency overflows.
 * 11. Two calls at once on one desc, from two threads, separate outputs,
 *    in every build (so -tsan runs it).
 * 12. Levels of detail: every fractal with n octaves against n + 1, the
 *    coarser equal to the finer's sum stopped early: the finer equal, bit
 *    for bit, to the coarser plus its last term, added as section 5a's sum
 *    adds it (never a subtraction from the finer, which float doesn't
 *    make exact) (the last
 *    term worked out by the test from a SINGLE call with that octave's seed,
 *    seed ^ X_n ^ X_0, and frequency, and section 5a's formula for the
 *    fractal; RIDGED's weight from the coarser call's own octaves).
 * 13. Simplex whole: 10^6 places per dimension and orientation against a
 *    double-precision sum over every lattice point within 3 cells, within
 *    2^-20; and 10^5 places on the planes where OpenSimplex2's reference
 *    changes its point (|x| = |y| in the turned frame), the values at
 *    +-2^-24 of a cell across within the gradient's bound.
 * No fuzz target: nothing here reads bytes. A desc from a game's data is a
 *    struct the caller filled, checked field by field (test 10, and the
 *    misuse harness).
 * tests/rbtmathnoise_bits_tests.c: the same bits on every platform, as
 *    rbtmathbvh_bits_tests.c: a fixed run of every kind, fractal,
 *    dimension, distance and return, values, gradients, warps and nearest
 *    cells, at places near the origin, negative, on cell borders and at
 *    1e7 m, and apart from them H, the Pa terms, the octave table, and
 *    the tables and constants of section 5a (the parts a GPU copy must
 *    match exactly), with X_i, Y_a and FLIP checked pairwise distinct as
 *    step 2 says, hashed and compared against tests/data/rbtmathnoise/bits.inc
 *    recorded on the first machine (with --print to record a meant change,
 *    naming what changed and why: a released kind's change is refused at
 *    review, section 5). Built with contraction off itself. Also run on
 *    arm64 with FPCR.FZ set (values and gradients within 2^-100, RIDGED's
 *    near a crest included) and owed on x86 with MXCSR's FTZ and DAZ.
 *    Owed: the bits test on Windows (MSVC, x64) and Fedora (GCC and Clang,
 *    x64), against the Mac's record; until then the definition is checked
 *    on one machine only, and the TODO says so.
 * tests/rbtnoalloc_tests.c: a scenario per call, 10,000 places, every
 *    kind, plain and on the pool: zero allocations.
 * tests/rbt_misuse_tests.c: regenerated from abi.txt, every new call with
 *    zeroes.
 * tests/rbtmathnoise_bench.c (not a ctest, Release, by hand): samples a
 *    second, one core and on the pool, per kind x dimension x fractal (1
 *    and 8 octaves) x with and without gradients; points against offsets
 *    against a grid; a warp; nearest cells; places at the origin and at
 *    1e7 m; and a terrain chunk's proxy (a 33 x 33 grid of a sphere's
 *    places, four layers and a warp: milliseconds a chunk). Into
 *    RBTMATH_TODO.md with the machine named.
 * Doxygen (check.sh doxygen), ABI (check.sh --update-abi), struct bases
 * (check.sh struct-bases), conventions.
 *
 * ===========================================================================
 * 11. ABI and version
 * ===========================================================================
 *
 * No break: a new header, two new struct types (4007, 4008, after
 * RBT_STRUCT_TYPE_MATH_BOX_BVH_DESC), new enums and calls.
 * ./check.sh --update-abi lists the additions; the misuse generator is
 * rerun. No version is raised while Rabbittus' 0.3 is untagged (card X2):
 * the additions are listed against the ABI snapshot. rbtmathnoise.h is not
 * in rbtrend.h (Rend's alone); a caller includes it.
 *
 * Records when built: RBTMATH_TODO.md (a section: what has run where, the
 * bench's numbers, the scale constants' search), rabbittus/CLAUDE.md's
 * module map (the Rabbittus Math row), CMakeLists.txt (the source with
 * contraction off, chosen by the compiler's ID, not "NOT MSVC", so clang-cl
 * is covered; the three test targets), and the workspace's
 * claude_made_decisions.md for what the build chooses alone (the four
 * scale constants, written as hex into the header's definition before the
 * bits test is first recorded; the tables are this draft's, section 5a).
 *
 * ===========================================================================
 * 12. What the terrain and biome drafts take from it
 * ===========================================================================
 *
 * - A layer of heights is a desc and a call: rbtmath_noise_compute_points
 *   over a chunk's vertices, in double, in the body's space, in metres
 *   (direction times the radius, before any height), values in [-S, S]
 *   ([0, S] ridged; S = sum of the octaves' amplitudes, 1 for one octave)
 *   that the generator scales to metres.
 * - Normals: out_gradients on the same call, projected on the sphere's
 *   tangent plane by the terrain (n = normalize(r_hat - grad_t(h) ...),
 *   its draft's arithmetic), so neighbouring chunks agree at shared
 *   vertices. ERODED layers' normals by neighbours, or the layer without
 *   ERODED.
 * - Warps: rbtmath_noise_warp_points once per chunk into a scratch array
 *   of the chunk's places (24 bytes each), the warped places then used by
 *   every warped layer; Jacobians where normals must follow.
 * - Craters: rbtmath_noise_compute_nearest_cells (Euclidean, jitter up to
 *   1), a crater's radius and depth from its id by the generator.
 * - Orientation: a 3D simplex layer on a sphere is EVEN; on a flat world's
 *   3D ground, the world's up (Y_UP or Z_UP). A generator sets it from its
 *   shape, as it sets dimensions.
 * - Seeds: a generator may mix a world's seed into each noise's however
 *   it likes (an add included): octave seeds are a xor with fixed hashes
 *   (section 5, step 2), so no mix lines one noise's octaves up with
 *   another's.
 * - Precision: no place past 1e7 m in a world's space is needed (worlds
 *   of 250 km); octaves finer than 1 cm are not needed at 0.5 m vertices.
 * - Cost per vertex, from section 6 (scalar; vectorised blocks should
 *   halve it or better): a layer of 8 octaves about 160 to 200 ns
 *   (simplex, its 3D sum exact: section 6) or 65 ns (gradient); a 3D warp
 *   of 4 octaves 3 x 60 to 80 ns; a cellular layer 45 to 60 ns; a
 *   generator of four layers and a warp about 0.6 to 0.8 us a vertex, so
 *   a 33 x 33 chunk 0.65 to 0.9 ms on a core and a 65 x 65 one 2.6 to
 *   3.4 ms, gradients half again. Measured
 *   by the bench's chunk proxy before the terrain draft's budgets are
 *   set.
 * - Per level of detail, the generator chooses the octaves (the finest
 *   octave below a vertex's spacing adds only aliasing); a coarser level's
 *   heights are the finer's without their finest terms (section 3), so the
 *   terrain's morphing between levels moves only that detail.
 */

/* ========================================================================= */
/* rbtcore.h                                                                 */
/* ========================================================================= */

/*
 * [CHANGED, a comment] math/rbtmatherror.h's file comment names
 * rbtmathnoise.h among the headers that include it.
 *
 * [CHANGED, a comment] The struct types' block comment for Rabbittus Math
 * names math/rbtmathnoise.h beside math/rbtmathbvh.h and
 * math/rbtmathpoly.h.
 *
 * [ADDED] Two struct types, in Rabbittus Math's block, after
 * RBT_STRUCT_TYPE_MATH_BOX_BVH_DESC = 4006:
 */
	RBT_STRUCT_TYPE_MATH_NOISE_DESC = 4007, /**< RBTMATH_NOISE_DESC. */
	RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC = 4008, /**< RBTMATH_NOISE_WARP_DESC. */

/* ========================================================================= */
/* math/rbtmathnoise.h                                                       */
/* ========================================================================= */

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
 * Out of scope: evaluating on the GPU (drawing's, Rend's); 1D and 4D
 * noise; value noise; band-limited noise; composing layers (the caller's
 * arithmetic over these calls); erosion by simulation.
 *
 * @par Definition
 * [In the real header: the draft's section 5, steps 1 to 10, and section
 * 5a, verbatim, with the four scale constants filled in.]
 * A kind's results never change once released: a new algorithm is a new
 * kind.
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

/**
 * [ADDED] Why: past 2^36 cells a double resolves less than 2^-16 of a
 * cell (2^-15 after simplex's turn), and the noise would step within a
 * cell (section 4). 1e7 m at 6,900 cells a metre.
 */
/** @brief The largest |coordinate| x frequency of the finest octave a
 * place may have on any axis, in cells: 2^36. */
#define RBTMATH_NOISE_MAX_CELLS 68719476736.0

/**
 * [ADDED] Why: the octave table is on the stack, a fixed bound; 32
 * octaves at a lacunarity of 2 span 2^31 in scale, from a continent to
 * past any detail a float's sum can still show (at a gain of 0.5 the 25th
 * octave is below a float ulp of the first).
 */
/** @brief The most octaves a desc may sum. */
#define RBTMATH_NOISE_MAX_OCTAVES 32u

/**
 * [ADDED] What a noise is made of. Append-only: a released kind's results
 * never change.
 */
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

/** [ADDED] How octaves are summed. Append-only. */
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

/** [ADDED] Cellular noise's distance. Append-only. */
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

/** [ADDED] Cellular noise's value as a field. Append-only. */
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

/**
 * [ADDED] Why: 3D simplex's lattice has a diagonal; a sphere's surface
 * wants no preferred direction, a flat world's 3D ground wants its
 * horizontal slices free of it (OpenSimplex2's three turns; section 3).
 * Append-only.
 */
/** @brief How 3D simplex noise's lattice is turned against the places'
 * axes (section 5a's turns). */
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

/**
 * [ADDED] A noise. Based (4007): a later kind's parameters chain on. Every
 * field the kind and fractal don't read must be 0, refused otherwise, so a
 * caller never believes a field acts that doesn't (and a later kind may
 * give it a meaning). Not frozen like RBTMATH_SHELL_MEDIUM: nothing on the
 * GPU reads it by layout today, and its kinds will grow. Frequency and
 * lacunarity are doubles because they multiply places in double (section
 * 5, steps 3 and 5); gain, feedback and erosion act on float values.
 */
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

/**
 * [ADDED] A regular grid of places. Frozen: a value, an origin, a step
 * and counts, which is all a grid is.
 */
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

/**
 * [ADDED] Why its own struct and call: a warp maps places to places, so
 * it nests, and one warp serves many fields (section 3).
 */
/** @brief A domain warp: places moved by a vector of noise. Only read
 * during a call. */
typedef struct RBTMATH_NOISE_WARP_DESC {
	/** RBT_STRUCT_TYPE_MATH_NOISE_WARP_DESC; `next` NULL. */
	RBT_STRUCT_BASE base;
	/** The field each axis's displacement is drawn from; channel a
	 * evaluates it with the seed field->seed ^ Y_a (section 5, step 2).
	 * Any kind and fractal a compute call takes. Only read during the
	 * call. */
	const RBTMATH_NOISE_DESC *field;
	/** The displacement for a value of 1, in the caller's unit: finite,
	 * above 0. */
	double amplitude;
} RBTMATH_NOISE_WARP_DESC;

/**
 * [ADDED] The general call: places in double, as card L66 asks for a
 * world's own space.
 */
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
 * [ADDED] Why beside the points call: a caller's floats (a mesh's
 * vertices in its own frame, a chunk's grid about its centre) are used
 * without widening them into a scratch array of doubles, and the origin
 * keeps them exact far out. Card L66's "float-input variants for local
 * work" is this with an origin of 0.
 */
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
 * [ADDED] Why: a texture, a heightmap or a baked volume is a grid; no
 * array of places (a 4096^2 grid's would be 400 MB), and rows share
 * cells' hashes.
 */
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
 * [ADDED] Domain warping (section 3).
 */
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
 * [ADDED] Why beside cellular as a field: what is placed by cell (a
 * crater, a region, a rock) needs which feature is nearest, not only how
 * far: its id to draw its size and kind from, and its offset to shape it
 * round its centre.
 */
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
