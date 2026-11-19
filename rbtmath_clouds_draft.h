/*
 * rbtmath_clouds_draft.h - card A57's Rabbittus Math half: a cloud layer's
 * density (coverage from a map, height profiles by type, erosion over two
 * tiling noise volumes) as a function any caller can ask on the CPU, and
 * the tiling noise volumes themselves, baked from rbtmathnoise.h's noise in
 * slices on the caller's jobs (RBTMATH_TODO.md, "Cloud density"; Rend's
 * half, rbtrend_clouds_draft.h, draws what this defines; card A57 in
 * DECISIONS.md, "Decided", 2026-11-17).
 *
 * APPROVED 2026-11-18 with the user, every question as recommended.
 * DRAFT, 2026-11-18, version 1 (written, then seven critique passes, each
 * fixed before the next: the revision notes below), for the user's
 * approval. Nothing built yet, not in any target, not
 * compilable. Written on the MacBook (M5 Pro) against what is built:
 * include/math/rbtmathnoise.h (card A56, built 2026-11-17: its kinds,
 * period, the Definition's steps 1 to 10, rbtmath_noise_compute_grid),
 * include/math/rbtmathmedium.h (the shell medium: the model of "same bits",
 * no libm but floor and sqrt), include/math/rbtmatherror.h, include/rbtcore.h
 * (struct types 4000 to 4008 for Math); include/math/rbtmathfunctions.h
 * (card A61, built on the Mac 2026-11-18 while this was drafted: tan,
 * atan, atan2, exp2, exp, log2 in double, every result within 1 ulp,
 * rbtmath_compute_atan at its line 288, and internally rbtfn::atan in
 * src/math/rbtmathfunctions_internal.hpp, line 292, which
 * rbtmathmedium.cpp's rbtfn::exp shows a sibling source may call);
 * and the measured costs in RBTMATH_TODO.md ("Measured": 3D cellular 374
 * ns a place, a 4-octave 3D gradient fBm 92 ns, one core).
 *
 * The decided lines (DECISIONS.md, "Decided", 2026-11-17, card A57):
 * "Nubis's model (Q1a); density in Math, noise baked from A56, the march
 * Rend's (Q2a) ... a cube-map coverage map (Q5a) ... Q3a reshaped:
 * rbtrend_clouds its own object, bounded by a shell or a flat slab, lit
 * through an atmosphere when given one else by the scene's lights, several
 * layers a frame, its look the caller's data."
 *
 * ===========================================================================
 * Revision notes (seven passes, 2026-11-18, each fixed before the next; the
 * lenses are the workspace's critique prompt's)
 * ===========================================================================
 *
 * Pass 1 (wrong, missing, more than the need).
 * 1. Wrong: version 0 put the bake in rbtmathclouds.h as
 *    rbtmath_cloud_noise_bake. A tiling volume of noise packed in bytes is
 *    not a cloud's: card A59's fog volumes read the same Worley volume, and
 *    any caller may want one (smoke, a nebula, a dissolve). Moved into
 *    rbtmathnoise.h as rbtmath_noise_volume_bake(), with the read that
 *    every density in this draft and the fog's is defined by,
 *    rbtmath_noise_volume_sample() (a public call: a caller checking a baked
 *    volume, or reading it as the GPU does, needs the same rule).
 * 2. Missing: what a coverage map's fourth channel means. Version 0 left it
 *    "unused"; a byte nobody defines is a byte someone will start using.
 *    Now: never read, any value, said on the struct.
 * 3. More than the need: version 0 had an anvil term (Schneider's, pushing
 *    coverage up near the top for storm clouds). It needs pow(), which no
 *    libm may give here, and a storm's anvil is a profile (rising late,
 *    holding to the top) and a coverage the caller writes. Removed; a
 *    storm stays a setting, as the card asks.
 * 4. More than the need: version 0 had a transmittance call (a ray through
 *    a layer, marched). Not decided, no caller named: the decided line is
 *    "a caller can ask the density at a place". A caller integrates
 *    densities itself if it wants a ray. Removed.
 * 5. Missing: mips. Rend reads level 0 only (its half, section 6), so the
 *    bake writes one level and the CPU reads level 0: the CPU and the GPU
 *    agree at every distance. Said.
 * 6. Missing: one map under several layers. A caller's weather (the
 *    caller's own weather draft, written beside this one the same morning)
 *    gives a world one field of coverage and type; its low deck and its
 *    high cirrus answer it differently. Added RBTMATH_CLOUD_SHAPE's
 *    coverage_scale and coverage_bias (step 2: c = sat(R x scale + bias)),
 *    so one map drives each layer by its look's numbers, and a layer with
 *    scale 0 is the bias everywhere (a map-free layer).
 *
 * Pass 2 (industry standard, compared).
 * 1. Nubis (Schneider, SIGGRAPH 2015 "The Real-time Volumetric
 *    Cloudscapes of Horizon: Zero Dawn", and GPU Pro 7's chapter, as
 *    remembered, not read in this workspace): a 128^3 RGBA base (Perlin-
 *    Worley in R, Worley fBms in GBA), a 32^3 RGB Worley detail, a weather
 *    map (coverage, precipitation, type), a height gradient per type,
 *    remap()-based shaping and erosion, the detail inverted from wispy at
 *    the bottom to billowy at the top. This draft is that model, each
 *    constant written out.
 * 2. Hillaire's TileableVolumeNoise (Frostbite, 2016; its Perlin-Worley is
 *    remap(perlin, 0, 1, worleyFBM, 1), as remembered) and Unreal's
 *    Volumetric Cloud (density from a material the user writes, over
 *    noise textures it ships): a ready-made recipe is what the industry
 *    gives. Unreal's freedom (any material) is a shader graph Rend doesn't
 *    have (item 22: precompiled shaders only); here the look is data
 *    within one model, as decided.
 * 3. Weather maps: Nubis's is 2D in a world of one plane; on a sphere the
 *    card's Q5a decided a cube map. Equi-angular cube maps (Google's EAC,
 *    the same tan warp a caller's cube-sphere terrain uses) keep texels
 *    within 1.19 of each other in linear size where a plain cube map's
 *    vary by 2.28 (measured in the caller's terrain draft); offered as a
 *    choice (QM3), not imposed.
 *
 * Pass 3 (the C API to standard).
 * 1. Every struct here is a value read by a call: frozen, as
 *    RBTMATH_SHELL_MEDIUM is, with a "Frozen:" line; none based, so no
 *    struct type is taken (4009 stays free).
 * 2. Enums as rbtmathnoise.h's: `typedef enum NAME RBT_ENUM_BASE`, 0 "not
 *    chosen" and refused.
 * 3. "Fields not read must be 0" (rbtmathnoise.h's rule) for the bounds'
 *    slab-only and shell-only fields, so a value set by mistake is caught,
 *    not ignored; each its own check and message.
 * 4. The profiles were `float profiles[4][4]`: a 2D array binds badly in
 *    some languages; now `float profiles[16]`, four per type, the layout
 *    written on the member.
 *
 * Pass 4 (data-oriented design and allocation).
 * 1. The bake allocated a slice's grid in version 0 (rbtmath_noise_compute_
 *    grid into a malloc'd scratch). Math calls hold nothing and allocate
 *    nothing on a steady path; a bake is a load-time call, but there is no
 *    need: a row at a time (at most RBTMATH_NOISE_VOLUME_MAX_SIZE places,
 *    six values each: 6 KiB on the stack), as the caller's atmosphere
 *    queries are chunked. No allocation in any call here.
 * 2. Batch-shaped: density over an array of points, one call; the bake
 *    over a run of slices, so a caller splits a volume over its jobs, each
 *    job its own slices, no two writing one byte.
 * 3. The density's reads walk two volumes and a map per point: the caller
 *    orders its points as it likes; the call keeps no cache.
 *
 * Pass 5 (threads, determinism, errors).
 * 1. Every call reads only its arguments and writes only its outputs:
 *    any number at once, from any thread (rbtmathmedium.h's rule). The
 *    bake's slices are independent: the bytes don't depend on how a
 *    volume is split over calls (test 2).
 * 2. Same bits: the bake by rbtmathnoise.h's definition (the same bits
 *    everywhere) and float steps written out; the density by float steps
 *    written out, floor and sqrt (IEEE's, correctly rounded), and, for the
 *    equi-angular warp, A61's atan in double (rbtfn::atan, within 1 ulp everywhere by
 *    a written evaluation, so the same bits everywhere), rounded to
 *    float. No libm call. Built without contraction (card A63's flags).
 * 3. Errors: each field its own check and message, in the struct's order,
 *    the structs in parameter order; a point not finite named by index.
 *    Nothing written on failure.
 *
 * Pass 6 (correctness against the platforms and the code as built).
 * 1. rbtmathnoise.h (built): the GRADIENT and CELLULAR kinds tile by
 *    `period` (lines 290 and 297), SIMPLEX doesn't (line 294), so the base's Perlin
 *    part is GRADIENT, not SIMPLEX. `lacunarity` must be a whole number
 *    where a period is set (line 387): 2. A period is at most
 *    RBTMATH_NOISE_MAX_CELLS at the last octave (line 410): 32 here. The
 *    FBM range is the kind's times S = sum(a_i) (line 309): 1.875 for four
 *    octaves at gain 0.5, so the Perlin part's map to [0, 1] uses 4/15.
 * 2. CELLULAR's F1 is 0 to sqrt(3) in 3D, Euclidean (lines 335 to 340: D the diagonal, sqrt(dimensions));
 *    Worley's inverted value 1 - min(F1, 1), clamped, as Nubis's.
 * 3. A place's cells: frequency f cells a tile, the period f: the noise
 *    tiles at 1 tile unit (the Definition's step 8 wraps c into
 *    [0, period)). Checked against the Definition: o_i (step 4) is an
 *    offset in cells, the same at c and c + period, so the wrap is exact.
 * 4. A61's atan in double: written first as "not yet built", PLAIN alone
 *    accepted until it was, an RBT_RESULT_UNSUPPORTED on the call. Re-read
 *    at this pass: A61 was built on the Mac on 2026-11-18
 *    (include/math/rbtmathfunctions.h; rbtmathmedium.cpp already calls its
 *    internal rbtfn::exp). The refusal and its note removed: both warps
 *    from the first build.
 * 5. Rend's 3D textures are R8G8B8A8_UNORM-capable on every backend
 *    (rbtrendimages.h, line 534: any format but block formats), sampled
 *    with repeat: a byte b reads b / 255, which the definition uses.
 * 6. The cube face table is the GPU's (Vulkan's "Cube Map Face Selection"
 *    table, which D3D12 and Metal share): written out, since Rend reads the
 *    map by hand from a 2D array (its half, section 5), not as a cube.
 *
 * Pass 7 (tests, formats, records, the build).
 * 1. Tests named below, each a claim of the definition, brute force where
 *    one exists (the bake against rbtmath_noise_compute_points at each
 *    texel; the sample against a double-precision trilinear).
 * 2. The bake's bits recorded as CRCs on the Mac, matched on Windows and
 *    Linux (card X25), as rbtmathnoise_bits_tests does.
 * 3. The bake's cost estimated from the measured noise (section 4) and to
 *    be measured, the estimate replaced in RBTMATH_TODO.md.
 * 4. Versions: no raise while Rabbittus 0.3 is untagged (card X2). ABI:
 *    additions only.
 * 5. Test 5 couldn't name its cases: step 3's profile index and step 7's
 *    density factor were both k. The index is i now. And the cube table
 *    checked against Vulkan's "Cube Map Face Selection" row by row (test
 *    7 holds it): unchanged.
 *
 * ===========================================================================
 * 1. What this is, and the line with Rend
 * ===========================================================================
 *
 * A cloud layer's density at a place is a pure function of: the layer's
 * bounds (a spherical shell or a flat slab), its shape (the look's numbers:
 * height profiles by type, the noise's tile sizes, erosion, a density
 * scale), two tiling noise volumes, a coverage map, and where the noise and
 * the map sit this moment (offsets: wind, the caller's place in the
 * tiling). This draft defines that function and computes it on the CPU,
 * the same bits on every platform. Rend's march (rbtrend_clouds_draft.h)
 * computes the same function in its shader, to a tolerance (a GPU filters
 * texels in its own fixed point and may fuse a multiply and an add), and
 * adds what drawing needs: light.
 *
 * The decided split (Q2a): density Math's, so "a caller can ask the
 * density at a place on the CPU and agree with what Rend draws"; the noise
 * volumes baked from rbtmathnoise on the caller's jobs and uploaded; the
 * march, its light and its reprojection Rend's.
 *
 * General purpose: nothing here knows what a layer is in the caller's
 * world (weather, a fog bank, dust, a nebula's sheet) or what an answer
 * becomes (a sensor, a sound, an aircraft's icing).
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * In math/rbtmathnoise.h, [ADDED]:
 * - RBTMATH_NOISE_VOLUME_KIND, two kinds of tiling volume packed in bytes:
 *   PERLIN_WORLEY (four channels: Nubis's base) and WORLEY (three
 *   channels: Nubis's detail, and A59's fog volumes' noise);
 * - rbtmath_noise_volume_bake(): slices of one, into the caller's bytes;
 * - rbtmath_noise_volume_sample(): the trilinear, repeating read every
 *   density defined over a baked volume uses;
 * - RBTMATH_NOISE_VOLUME_MAX_SIZE.
 *
 * A new header, math/rbtmathclouds.h, [ADDED]:
 * - RBTMATH_CLOUD_SHAPE (frozen): a layer's look as density;
 * - RBTMATH_CLOUD_BOUNDS (frozen) and its enums: a shell or a slab, and how
 *   the coverage map lies on it;
 * - RBTMATH_CLOUD_TEXELS (frozen): the baked volumes and the coverage map,
 *   borrowed for a call;
 * - RBTMATH_CLOUD_OFFSETS (frozen): where the tiling sits;
 * - rbtmath_cloud_compute_density();
 * - two limits.
 *
 * No struct type, no result code, no change to anything built.
 *
 * ===========================================================================
 * 3. The definition
 * ===========================================================================
 *
 * Every float operation binary32, rounded once, in the order the
 * parentheses give; no contraction; constants hex where not exact. "sat(x)"
 * is min(max(x, 0), 1), NaN never arising (every input checked finite).
 *
 * 3a. A BAKED VOLUME (rbtmath_noise_volume_bake). A cube of N^3 texels,
 * x fastest, then y, then z, each four bytes (RGBA; WORLEY's fourth 0).
 * Texel (i, j, k)'s place, in tile units, in double: ((i + 0.5) / N,
 * (j + 0.5) / N, (k + 0.5) / N). With the volume's seed s:
 *   P   = rbtmath_noise_compute_points with { dimensions 3, GRADIENT, FBM,
 *         seed s ^ 0x243F6A88, octaves 4, frequency 4, lacunarity 2,
 *         gain 0.5, period {4, 4, 4} }: in [-1.875, 1.875];
 *         p = sat((P * 0x1.111112p-2) + 0.5f)   (4/15, F: P / 3.75 + 0.5).
 *   W_f = 1 - min(F1, 1), F1 from { dimensions 3, CELLULAR, SINGLE,
 *         seed s ^ 0x85A308D3 ^ f, octaves 1, frequency f, cell_distance
 *         EUCLIDEAN, cell_return F1, cell_jitter 1, period {f, f, f} }, for
 *         f a power of two: in [0, 1], 1 at a feature.
 *   V_f = ((W_f * 0.625f) + (W_2f * 0.25f)) + (W_4f * 0.125f).
 * PERLIN_WORLEY: R = sat(V_4 + (p * (1 - V_4))) (Perlin dilated by Worley:
 *   remap(p, 0, 1, V_4, 1)); G = V_4; B = V_8; A = V_16.
 * WORLEY: R = V_2; G = V_4; B = V_8; A = 0.
 * A channel's byte: (uint8_t)((v * 255.0f) + 0.5f), v in [0, 1], the
 * conversion truncating.
 * Every W_f and P tiles at 1 (frequency f, period f), so the volume tiles.
 * A size N below the finest frequency (64 for PERLIN_WORLEY, 32 for
 * WORLEY) aliases it: allowed, the bytes still defined.
 *
 * 3b. READING A VOLUME (rbtmath_noise_volume_sample). At q (tile units,
 * three floats), each channel c: per axis a, u_a = (q_a * N) - 0.5f;
 * i_a = floor(u_a) (an int64 from the float), t_a = u_a - (float)i_a; the
 * two texels on the axis (i_a mod N) and ((i_a + 1) mod N), the Euclidean
 * remainder. Values b / 255.0f (correctly rounded division). Lerp
 * lerp(x, y, t) = x + (t * (y - x)), along x first, then y, then z. Every
 * |q_a| at most RBTMATH_NOISE_VOLUME_MAX_COORDINATE (2^16 tiles), so q_a * N
 * keeps 1/128 of a texel at the largest size: refused past it.
 * The GPU's hardware trilinear filter with REPEAT addressing reads the same
 * function, its weights in the GPU's fixed point (commonly 8 fractional
 * bits; Vulkan's guaranteed least is 4, subTexelPrecisionBits; none
 * measured here yet): a GPU copy
 * agrees to a tolerance, measured (Rend's half, its test 2).
 *
 * 3c. A LAYER'S DENSITY (rbtmath_cloud_compute_density). For a point p,
 * three floats relative to the bounds' origin (a shell's centre; a slab's
 * point on its base plane, at height 0):
 * 1. HEIGHT. SHELL: r = sqrt(((p0 * p0) + (p1 * p1)) + (p2 * p2)),
 *    y = r - bottom. SLAB: y = ((p0 * up0) + (p1 * up1)) + (p2 * up2) -
 *    bottom. h = y / (top - bottom). h < 0 or h > 1: density 0, stop.
 * 2. COVERAGE. SHELL (r > 0; r 0 is density 0): the face by the largest
 *    |p_a|, ties to x, then y, then z; (sc, tc, ma) from the table below;
 *    x_g = sc / |ma|, y_g = tc / |ma|; PLAIN: a = x_g, b = y_g;
 *    EQUI_ANGULAR: a = (float)(atan((double)x_g) * 0x1.45f306dc9c883p+0)
 *    (4/pi, D), b alike, A61's atan; s = (a + 1) * 0.5f, t = (b + 1) *
 *    0.5f. The face's texels: u = (s * M) - 0.5f, v = (t * M) - 0.5f,
 *    bilinear (the lerp above, x then y), each index clamped to 0 to M - 1
 *    (no face reads another's texels). SLAB: v_axis = up x map_axis
 *    (((up1 * m2) - (up2 * m1)), ...); s = (((p . map_axis) / extent) +
 *    off0), t = (((p . v_axis) / extent) + off1), dots as in step 1;
 *    REPEAT: as 3b's wrap; CLAMP: indices clamped. Read bytes as 3b.
 *    The coverage c = sat((R x coverage_scale) + coverage_bias), the type
 *    g = G, the density factor k = B; A is never read.
 *
 *    | face | ma  | sc  | tc  |      (the order +X, -X, +Y, -Y, +Z, -Z:
 *    | +X   | +x  | -z  | -y  |       the GPU's cube faces, a layer each,
 *    | -X   | -x  | +z  | -y  |       face f at texel offset f * M * M * 4)
 *    | +Y   | +y  | +x  | +z  |
 *    | -Y   | -y  | +x  | -z  |
 *    | +Z   | +z  | +x  | -y  |
 *    | -Z   | -z  | -x  | -y  |
 *
 * 3. PROFILE. x = g * (float)(type_count - 1); i = min(floor(x),
 *    type_count - 2) (0 when type_count is 1); f = x - i. Profile n's
 *    four heights (a, b, c, d) = profiles[4n .. 4n + 3]:
 *    P_n(h) = min(sat((h - a) / (b - a)), sat((d - h) / (d - c))).
 *    P = P_i(h) + (f * (P_(i+1)(h) - P_i(h))), or P_0(h) for one type.
 * 4. BASE. (R, G, B, A) = sample(base, (p / base_tile) + offsets.base),
 *    the division per component; l = ((G * 0.625f) + (B * 0.25f)) +
 *    (A * 0.125f); e = sat((R - (l - 1)) / (1 - (l - 1)))
 *    (Nubis's remap(R, l - 1, 1, 0, 1); its denominator 2 - l is at
 *    least 1). shaped = e * P.
 * 5. COVERAGE APPLIED. c 0: density 0, stop. d0 = sat((shaped - (1 - c)) /
 *    c) * c (remap(shaped, 1 - c, 1, 0, 1) * c). d0 0: density 0, stop
 *    (the detail isn't read: the GPU's early out too).
 * 6. EROSION. (R, G, B, _) = sample(detail, (p / detail_tile) +
 *    offsets.detail); w = ((R * 0.625f) + (G * 0.25f)) + (B * 0.125f);
 *    m = w + (sat(h / billow_height) * ((1 - w) - w)) (wisps low, billows
 *    high: lerp(w, 1 - w, ...)); e2 = m * erosion; e2 1: density 0, stop;
 *    d1 = sat((d0 - e2) / (1 - e2)).
 * 7. DENSITY = (d1 * density_scale) * k.
 *
 * A density is unitless: Rend multiplies it by its look's extinction (per
 * scene unit) to get the medium's extinction.
 *
 * ===========================================================================
 * 4. Cost and memory (estimates from RBTMATH_TODO.md's measurements; the
 *    method: per texel, the noise calls the definition makes at their
 *    measured one-core cost)
 * ===========================================================================
 *
 * - PERLIN_WORLEY 128^3 (8 MiB): per texel one 4-octave 3D gradient fBm
 *   (92 ns) and five single 3D cellular octaves (W_4 to W_64, 374 ns
 *   each): about 1.96 us, 2,097,152 texels, about 4.1 s on one core; 0.4
 *   to 0.6 s over the M5 Pro's cores on a caller's pool. The card's "cheap"
 *   ("2 million texels on jobs at load is cheap") was an estimate without
 *   the cellular cost: corrected here; its Q2b (a GPU copy of the noise)
 *   is asked again if the measured bake is too slow for a caller's start
 *   (a caller can also bake once and keep the bytes: they are the same
 *   bits everywhere).
 * - WORLEY 32^3 (128 KiB): five cellular octaves a texel, about 1.9 us,
 *   32,768 texels: 61 ms on one core.
 * - A density: two volume reads (8 texels each), a map read (4 texels),
 *   about 100 float operations, one sqrt (shell), two atan (equi-angular,
 *   4.7 to 7.4 ns each, A61's draft's measurement): estimated 60 to 120 ns a point.
 * - Memory: none of the library's. The caller holds the bytes: 8 MiB +
 *   128 KiB at Nubis's sizes, and its map (6 M^2 x 4 bytes a shell, M^2 x 4
 *   a slab).
 *
 * ===========================================================================
 * 5. Compared (pass 2) and alternatives
 * ===========================================================================
 *
 * - The noise analytic on the CPU (no bytes held there; QM2's b): the
 *   density would differ from what is drawn by the bake's trilinear
 *   sampling (a texel's worth of detail), so "agree with what Rend draws"
 *   would hold only roughly; and 8 MiB on the CPU is cheap beside a
 *   world. Not chosen.
 * - A caller's own recipe (any channels from any rbtmath_noise call; QM1's
 *   b): the density's steps 4 and 6 give each channel a meaning, so a
 *   free recipe is a free model: Unreal's material, which Rend can't
 *   compile. Kept to Nubis's recipe, every constant written; a second
 *   recipe is a new kind.
 * - Hazel: no clouds and no volume noise (../.refs/HazelRenderer2025,
 *   present: nothing under src/Hazel; its one fog is a distance fade in
 *   Hazelnut's SceneComposite.glsl, disabled by a define).
 *
 * ===========================================================================
 * 6. ABI, versions, allocation, threads
 * ===========================================================================
 *
 * Additions only: one enum, two calls and two macros in rbtmathnoise.h;
 * a new header with four frozen structs, three enums, two macros and one
 * call. rbtmath.h's map of headers gains it (it includes none: Math's
 * headers are included one by one). No version raise while 0.3 is untagged
 * (card X2). No allocation in any call. Threads: as rbtmathmedium.h, every
 * call reads its arguments and writes its outputs only.
 *
 * ===========================================================================
 * 7. Tests (C, tests/rbtmathclouds_tests.c and tests/rbtmathclouds_bits_tests.c)
 * ===========================================================================
 *
 * 1. test_bake_against_points: every texel of a PERLIN_WORLEY 16^3 and a
 *    WORLEY 16^3, against the definition evaluated by
 *    rbtmath_noise_compute_points at the texel's place in the test: the
 *    same bytes.
 * 2. test_bake_slices: a 64^3 baked whole, and in runs of 1, 7 and 64
 *    slices, and on 1 and 4 threads of a test pool: the same bytes.
 * 3. test_bake_tiles: each channel's noise at the place and at the place +
 *    1 tile on each axis: the same value (the volume tiles).
 * 4. test_sample_against_double: 10^5 random q (within the limit, near
 *    texel centres, edges and the wrap), against a trilinear in double:
 *    within 2^-20; at a texel's centre, exactly its byte / 255.
 * 5. test_density_steps: hand-built 2^3 volumes and 1 x 1 maps, each step
 *    of 3c alone (a profile's four corners; c 0; d0 0; e2 1; h outside),
 *    against values worked in the test.
 * 6. test_density_bounds: a shell and a slab of the same thickness, the
 *    same shape and map and offsets: at points of equal height and map
 *    coordinate, the same density (the bounds change only height and
 *    coverage); a slab's REPEAT tiling at s + 1, CLAMP's edge.
 * 7. test_cube_faces: points along each face's axis and at every edge
 *    and corner: the face the table gives, ties as written; PLAIN and
 *    EQUI_ANGULAR agree at a face's centre and its edges' midpoints.
 * 8. test_refusals: every check of every call, in order, each its message;
 *    nothing written.
 * 9. Bits (rbtmathclouds_bits_tests): CRCs of a 32^3 bake of each kind and
 *    of 4,096 densities (both bounds, both warps, both addresses),
 *    recorded on the Mac, the same in Debug and Release and with
 *    flush-to-zero set, matched on Windows and Linux (card X25).
 * 10. rbtnoalloc_tests: a bake of a slice and 4,096 densities, none of
 *    ours. rbt_misuse_tests regenerated from abi.txt.
 * 11. rbtmathclouds_bench (Release, not a ctest): the bake's ns a texel of
 *    each kind and a density's ns a point; numbers into RBTMATH_TODO.md.
 *
 * ===========================================================================
 * 8. Records when built
 * ===========================================================================
 *
 * RBTMATH_TODO.md's "Cloud density": built, where, the bits' CRCs, the
 * measured bake and density, the platforms not yet run. rabbittus/CLAUDE.md's
 * module map row for Rabbittus Math gains rbtmathclouds.h. The ABI
 * snapshot updated (additions). check.sh's doxygen, struct-bases,
 * plain-data, caller-names.
 *
 * ---------------------------------------------------------------------------
 * Questions for the user (card A57's Math half; each with a recommendation)
 * ---------------------------------------------------------------------------
 *
 * QM1. The noise recipe: a, Nubis's, every constant written (PERLIN_WORLEY,
 *      WORLEY; recommended); b, a free recipe of the caller's (see section
 *      5).
 * QM2. The CPU's density: a, from the baked bytes, as drawn (recommended);
 *      b, from the noise itself, no bytes held on the CPU.
 * QM3. The coverage map on a sphere: a, both warps offered, PLAIN and
 *      EQUI_ANGULAR, the caller's choice per layer (recommended: a caller
 *      with a cube-sphere ground chooses its warp, so a map texel lies over
 *      the same ground; A61's atan, built); b,
 *      EQUI_ANGULAR alone; c, PLAIN alone (the GPU's cube sampling).
 * QM4. Profiles: a, a trapezoid of four heights a type, up to four types
 *      blended by the map's type (recommended: Nubis's gradients, data);
 *      b, curves of more points.
 * QM5. Where the bake and the volume read live: a, rbtmathnoise.h (any
 *      caller's tiling volume; A59's fog takes WORLEY; recommended); b,
 *      rbtmathclouds.h.
 */

/* ========================================================================= */
/* math/rbtmathnoise.h                                           [ADDED]     */
/* ========================================================================= */

/** @brief The largest size of a baked noise volume's side, in texels. */
#define RBTMATH_NOISE_VOLUME_MAX_SIZE 256u
/* [ADDED] Why: the bake works a row on the stack (pass 4.1), and 256^3
 * RGBA is 64 MiB, past any use named. */

/** @brief The largest |coordinate| rbtmath_noise_volume_sample() takes, in
 * tiles: 2^16, so a coordinate times the largest size keeps 1/128 of a
 * texel. */
#define RBTMATH_NOISE_VOLUME_MAX_COORDINATE 65536.0f
/* [ADDED] Why: past it a float can't place a texel; a caller wraps its
 * offsets (they tile). */

/** @brief The kinds of tiling noise volume rbtmath_noise_volume_bake()
 * makes: four bytes a texel, RGBA, each channel's meaning the kind's
 * (the Definition's "Baked volumes"). */
typedef enum RBTMATH_NOISE_VOLUME_KIND RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_NOISE_VOLUME_KIND_UNKNOWN = 0,
	/** R: Perlin-Worley (gradient fBm dilated by Worley's); G, B, A:
	 * Worley fBms at 4, 8 and 16 cells a tile. A cloud's base shape. */
	RBTMATH_NOISE_VOLUME_KIND_PERLIN_WORLEY = 1,
	/** R, G, B: Worley fBms at 2, 4 and 8 cells a tile; A: 0. Erosion
	 * detail, and the noise of a fog's volume. */
	RBTMATH_NOISE_VOLUME_KIND_WORLEY = 2,
} RBTMATH_NOISE_VOLUME_KIND;
/* [ADDED] Why: two recipes, each defined bit for bit (section 3a), so a
 * volume baked anywhere is the same bytes and what reads it agrees. */

/**
 * @brief Bakes slices of a tiling noise volume: texels of a cube of @p size
 * texels a side, slices @p first_slice to @p first_slice + @p slice_count
 * - 1, x fastest, then y, then each slice, four bytes a texel. The volume
 * tiles: its texel (size, j, k) would be (0, j, k).
 *
 * The bytes are the same on every platform, however a volume is split
 * over calls. A volume is baked by any number of calls at once, each its
 * own slices (a caller's jobs).
 *
 * @param[in]  kind        Its recipe.
 * @param[in]  seed        Any value; each seed a different volume. Two
 *                         volumes read together are given different seeds.
 * @param[in]  size        1 to RBTMATH_NOISE_VOLUME_MAX_SIZE.
 * @param[in]  first_slice Below @p size.
 * @param[in]  slice_count 1 to @p size - @p first_slice.
 * @param[out] out_texels  size x size x slice_count x 4 bytes.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a kind not one of
 *         RBTMATH_NOISE_VOLUME_KIND's, a size, slice or count out of range,
 *         or a NULL @p out_texels, each its own check and message. Nothing
 *         is written on failure.
 */
RBT_RESULT rbtmath_noise_volume_bake(
	RBTMATH_NOISE_VOLUME_KIND kind,
	uint32_t seed,
	uint32_t size,
	uint32_t first_slice,
	uint32_t slice_count,
	uint8_t *out_texels,
	RBTMATH_ERROR *error);
/* [ADDED] Why: the decided bake (Q2a: "noise baked from A56 ... on the
 * caller's jobs"), in slices so the jobs are the caller's. */

/**
 * @brief Reads a baked volume at places, trilinear, repeating: four floats
 * a place, each channel's byte / 255 interpolated (the Definition's
 * "Reading a volume"), as a GPU's linear filter with repeat reads it, to
 * its filter's precision.
 *
 * @param[in]  texels      The volume: size^3 x 4 bytes, as
 *                         rbtmath_noise_volume_bake() writes. Only read
 *                         during the call.
 * @param[in]  size        Its side: 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE.
 * @param[in]  coordinates @p count places in tiles, three floats each,
 *                         each finite, |each| at most
 *                         RBTMATH_NOISE_VOLUME_MAX_COORDINATE.
 * @param[in]  count       How many; 0 writes nothing.
 * @param[out] out_values  4 x @p count floats, each 0 to 1.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL array with a
 *         count above 0, a size out of range, or a coordinate not finite or
 *         past the limit (its index named). Nothing is written on failure.
 */
RBT_RESULT rbtmath_noise_volume_sample(
	const uint8_t *texels,
	uint32_t size,
	const float *coordinates,
	uint32_t count,
	float *out_values,
	RBTMATH_ERROR *error);
/* [ADDED] Why: the one read every density over a baked volume is defined
 * by (this draft's and card A59's), public so a caller can check a volume,
 * or read one, by the rule the GPU follows. */

/* ========================================================================= */
/* math/rbtmathclouds.h                                    [ADDED: new file] */
/* ========================================================================= */

/*
 * @file rbtmathclouds.h
 * @brief Rabbittus Math's cloud layers: the density of a layer of cloud
 * bounded by a spherical shell or a flat slab, from its shape, two baked
 * noise volumes (rbtmathnoise.h) and a coverage map, at many points a call.
 *
 * (The file's opening comment, as a new header's: built in
 * src/math/rbtmathclouds.cpp, tests named; general purpose (nothing knows
 * what the layer is or what an answer becomes); out of scope (light, which
 * is drawing's; a ray's transmittance, which a caller sums from densities;
 * other models); "Same bits" as rbtmathmedium.h's, with A61's atan for the
 * equi-angular warp; "Threads": every call reads its arguments and writes
 * its outputs only; "Results": RBT_RESULT_INVALID_ARGUMENT with the
 * reason, each field its own check in the struct's order, nothing written
 * on failure. The definition, section 3c above, in its @verbatim.)
 */

/** @brief The most cloud types a shape blends (RBTMATH_CLOUD_SHAPE::type_count). */
#define RBTMATH_CLOUD_MAX_TYPES 4u
/* [ADDED] Why: the profiles' frozen array is sized by it (Nubis's three or
 * four: stratus to cumulonimbus). */

/** @brief The largest coverage map side, in texels. */
#define RBTMATH_CLOUD_MAX_COVERAGE_SIZE 4096u
/* [ADDED] Why: the map's side is checked against it; 4096^2 x 6 x 4 bytes
 * (384 MiB) is past any use named. */

/** @brief What bounds a cloud layer. */
typedef enum RBTMATH_CLOUD_BOUNDS_KIND RBT_ENUM_BASE {
	/** Not chosen: refused. */
	RBTMATH_CLOUD_BOUNDS_KIND_UNKNOWN = 0,
	/** Between two spheres round the origin, radii #RBTMATH_CLOUD_BOUNDS::bottom
	 * and #RBTMATH_CLOUD_BOUNDS::top; its coverage map a cube's six faces. */
	RBTMATH_CLOUD_BOUNDS_KIND_SHELL = 1,
	/** Between two planes across #RBTMATH_CLOUD_BOUNDS::up, at heights
	 * bottom and top above the origin; endless across, its coverage map one
	 * face laid along the plane. */
	RBTMATH_CLOUD_BOUNDS_KIND_SLAB = 2,
} RBTMATH_CLOUD_BOUNDS_KIND;
/* [ADDED] Why: the decided "a spherical shell or a flat slab, the same
 * object for a round world and a flat level". */

/** @brief How a shell's cube faces map directions to texels. */
typedef enum RBTMATH_CLOUD_CUBE_WARP RBT_ENUM_BASE {
	/** Not chosen: refused with a shell; must be this with a slab. */
	RBTMATH_CLOUD_CUBE_WARP_UNKNOWN = 0,
	/** Gnomonic: a texel's place on its face proportional to the
	 * direction's tangent (a GPU's cube map). Texels at a face's centre 2.28
	 * times the linear size of those at its corners. */
	RBTMATH_CLOUD_CUBE_WARP_PLAIN = 1,
	/** Equi-angular: proportional to the angle, (4 / pi) atan; texels
	 * within 1.19 of each other in linear size. */
	RBTMATH_CLOUD_CUBE_WARP_EQUI_ANGULAR = 2,
} RBTMATH_CLOUD_CUBE_WARP;
/* [ADDED] Why: QM3; a caller's map is written over its own grid. */

/** @brief What a slab's coverage map reads past its edges. */
typedef enum RBTMATH_CLOUD_MAP_ADDRESS RBT_ENUM_BASE {
	/** Not chosen: refused with a slab; must be this with a shell. */
	RBTMATH_CLOUD_MAP_ADDRESS_UNKNOWN = 0,
	/** The map tiles: an endless sky of a repeating weather. */
	RBTMATH_CLOUD_MAP_ADDRESS_REPEAT = 1,
	/** Its edge texels carry on: a map over a level, its rim's weather
	 * beyond it. */
	RBTMATH_CLOUD_MAP_ADDRESS_CLAMP = 2,
} RBTMATH_CLOUD_MAP_ADDRESS;
/* [ADDED] Why: a slab is endless and its map isn't; which way the caller
 * fills the rest is its choice, not a default. */

/**
 * @brief A cloud layer's shape: how its density rises and falls with
 * height by cloud type, its noise's scale and its erosion. A value; the
 * calls copy what they read.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_SHAPE {
	/** How many types the map's type channel blends between: 1 to
	 * RBTMATH_CLOUD_MAX_TYPES. Type 0 at the channel's 0, the last at its
	 * 1, evenly between. */
	uint32_t type_count;
	/**
	 * Each type's height profile, four floats a type, #type_count types
	 * from index 0 (the rest must be 0): heights as fractions of the layer's
	 * thickness, a < b <= c < d, each 0 to 1. The density's scale rises
	 * from 0 at a to 1 at b, holds to c, and falls to 0 at d (Nubis's
	 * height gradients: stratus low and thin, cumulus tall).
	 */
	float profiles[16];
	/** The base volume's tile, in the caller's unit: finite, above 0. */
	float base_tile;
	/** The detail volume's tile: finite, above 0. */
	float detail_tile;
	/** How much the detail erodes the base's edges: 0 (none) to 1. */
	float erosion;
	/** Up to this fraction of the thickness the detail turns from wisps
	 * (low) to billows (high): finite, above 0, at most 1. */
	float billow_height;
	/** What a full density is multiplied by: finite, at least 0. */
	float density_scale;
	/** What the map's coverage is multiplied by for this layer: finite (0:
	 * the map's coverage unread, #coverage_bias everywhere). */
	float coverage_scale;
	/** What is added to it, the sum clamped to 0 to 1: finite. One map
	 * drives several layers, each by its own scale and bias. */
	float coverage_bias;
} RBTMATH_CLOUD_SHAPE;
/* [ADDED] Why: "its look entirely the caller's data through Math's density
 * model (coverage, types, profiles, erosion)": overcast, a storm, dust or a
 * fog bank are these numbers and a map. */

/**
 * @brief What bounds a layer, relative to an origin the caller places, and
 * how its coverage map lies on it. A value; the calls copy what they read.
 * Fields a kind doesn't read must be 0.
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_BOUNDS {
	/** A shell or a slab. */
	RBTMATH_CLOUD_BOUNDS_KIND kind;
	/** A shell's inner radius, or a slab's lower height above its origin:
	 * finite; a shell's above 0. */
	float bottom;
	/** A shell's outer radius, or a slab's upper height: finite, above
	 * #bottom. */
	float top;
	/** A slab's up: finite, its length within 1e-4 of 1. Read by SLAB. */
	float up[3];
	/** The direction a slab's map's first axis runs: finite, its length
	 * within 1e-4 of 1, square to #up within 1e-4 (its dot). Its second
	 * axis is up x map_axis. Read by SLAB. */
	float map_axis[3];
	/** How far one map spans along each axis, in the caller's unit:
	 * finite, above 0. Read by SLAB. */
	float map_extent;
	/** Past a slab's map. Read by SLAB. */
	RBTMATH_CLOUD_MAP_ADDRESS map_address;
	/** A shell's faces' warp. Read by SHELL. */
	RBTMATH_CLOUD_CUBE_WARP cube_warp;
} RBTMATH_CLOUD_BOUNDS;
/* [ADDED] Why: the decided bounds, and the map's lie on them, each the
 * caller's (no slab map axis is assumed, no warp chosen for it). */

/**
 * @brief The texels a density reads, borrowed for one call: the two
 * baked volumes and the coverage map.
 */
/* Frozen: a value, read by the calls below. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_TEXELS {
	/** A PERLIN_WORLEY volume, #base_size^3 x 4 bytes (rbtmathnoise.h). */
	const uint8_t *base;
	/** Its side: 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE. */
	uint32_t base_size;
	/** A WORLEY volume, #detail_size^3 x 4 bytes. */
	const uint8_t *detail;
	/** Its side: 1 to RBTMATH_NOISE_VOLUME_MAX_SIZE. */
	uint32_t detail_size;
	/**
	 * The coverage map, four bytes a texel, rows of #coverage_size, x
	 * fastest: a shell's six faces one after another (+X, -X, +Y, -Y, +Z,
	 * -Z), a slab's one. R: coverage, 0 none to 1 overcast. G: cloud type,
	 * 0 the first to 1 the last (RBTMATH_CLOUD_SHAPE::type_count). B: what
	 * the density is multiplied by (a storm's darker core; 255 for 1). A:
	 * never read, any value.
	 */
	const uint8_t *coverage;
	/** Its side: 1 to RBTMATH_CLOUD_MAX_COVERAGE_SIZE. */
	uint32_t coverage_size;
} RBTMATH_CLOUD_TEXELS;
/* [ADDED] Why: the calls hold nothing (pass 4), so the bytes are named for
 * each call; the caller keeps them, as it keeps what it uploaded. */

/**
 * @brief Where the tiling sits at the points' origin this moment: the noise
 * volumes' coordinates, in tiles, and a slab's map's, in maps. Wind is a
 * caller moving these; a caller whose origin is far out puts its place in
 * the tiling here (each wraps at 1).
 */
/* Frozen: a value, read by the calls below and by Rend. It never changes; a new need gets a new type. */
typedef struct RBTMATH_CLOUD_OFFSETS {
	/** The base volume's coordinate at the origin: each finite, |each| at
	 * most RBTMATH_NOISE_VOLUME_MAX_COORDINATE. */
	float base[3];
	/** The detail volume's, as #base. */
	float detail[3];
	/** A slab's map's coordinate at the origin, in maps: finite; 0 for a
	 * shell. */
	float coverage[2];
} RBTMATH_CLOUD_OFFSETS;
/* [ADDED] Why: wind and a far origin, given as the place in the tiling, so
 * the CPU and the GPU take the same numbers (Rend's layer holds this very
 * struct). */

/**
 * @brief A cloud layer's density at each point: unitless, 0 outside the
 * layer, at most `shape->density_scale` (the definition, section 3c).
 *
 * @param[in]  shape       The shape. Only read during the call.
 * @param[in]  bounds      The bounds and the map's lie.
 * @param[in]  texels      The volumes and the map: a shell's map six
 *                         faces, a slab's one.
 * @param[in]  offsets     Where the tiling sits at the origin.
 * @param[in]  points      @p count points relative to the bounds' origin,
 *                         three floats each, each finite. NULL with a count
 *                         of 0.
 * @param[in]  count       How many; 0 writes nothing.
 * @param[out] out_density @p count floats.
 * @param[out] error       Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK; RBT_RESULT_INVALID_ARGUMENT for a NULL struct, a
 *         NULL array with a count above 0, a field out of its range or set
 *         where unread (each its own check and message, the structs in
 *         parameter order, each in its members' order), or a point not
 *         finite (its index named). Nothing is written on failure.
 */
RBT_RESULT rbtmath_cloud_compute_density(
	const RBTMATH_CLOUD_SHAPE *shape,
	const RBTMATH_CLOUD_BOUNDS *bounds,
	const RBTMATH_CLOUD_TEXELS *texels,
	const RBTMATH_CLOUD_OFFSETS *offsets,
	const float *points,
	uint32_t count,
	float *out_density,
	RBTMATH_ERROR *error);
/* [ADDED] Why: the decided CPU query, the same function Rend marches. */
