# Rabbittus Math TODO

Rabbittus Math is two headers: `include/math/rbtmathbvh.h` (ray casting against meshes,
double precision) and `include/math/rbtmathpoly.h` (plane polygons with holes on
an integer grid: booleans that carry each edge's tag through, a validity check,
an offset that moves each edge by its own distance, a weighted straight skeleton,
and triangulation).

## What has run where

- **2026-10-31, macOS (Apple M5 Pro):** volume tests in `rbtmathbvh.h`
  (card T21; a plane, a convex volume with clip planes, a box's three
  answers, a mesh BVH's overlaps and within): `rbtmathbvh_tests` (400
  random volumes against a brute force), `rbtnoalloc_tests` (no
  allocation) and `rbtoverlap_tests` (two threads, every build) pass in
  Debug, Release, `-asan` and `-tsan`, built inside a caller's presets.
  Card T22 holds one sentence of the contract: triangles with no area,
  which the BVH drops. (This said "Windows and Linux not built" until
  2026-11-05: see the Windows and Linux entries below.)
- **2026-11-05, Windows 11** (ThinkPad P1 Gen 5, MSVC 14.51.36231):
  `rabbittus-windows-d3d12`, Debug and Release, no warning:
  `rbtmathbvh_tests` (the volume tests among them), `rbtmathpoly_tests` and
  `rbtmathpoly_bits_tests` pass, the bits test's hashes macOS's (36 of 37
  ctests, `rbtrend_contract_tests`' `lit_fit_cascades` the one, card A49).
- **2026-11-04, Linux** (Fedora 44, GCC 16.2.1, and clang 22.1.8 under
  the sanitizers): Rabbittus' ctests on `rabbittus-linux-vulkan`,
  Debug and Release, `-asan` and `-tsan`, 36 of 37, the same one failing
  (`../ROADMAP.md`, section 2): Rabbittus Math's three, the bits test with
  macOS's hashes, pass.
- **2026-10-30, Windows 11** (ThinkPad P1 Gen 5, MSVC 14.51): the first
  Windows build of the polygons. `rbtmathpoly_tests`, `rbtmathpoly_bits_tests`
  and `rbtmathbvh_tests` pass on `windows-d3d12` and `windows-vulkan` in
  Debug, Release and RelWithDebInfo, and under both `-asan` presets; the
  bits test's hashes are macOS's in every configuration, so the polygons'
  results are the same bits on both platforms. The test needed
  `_USE_MATH_DEFINES` for MSVC's `M_PI`. Rabbittus Math's ray casts make no
  allocation there (`rbtnoalloc_tests`). Linux built 2026-11-04 (above).
- **rbtmathbvh**: built and tested (`tests/rbtmathbvh_tests.c`) on macOS.
- **rbtmathpoly**: written 2026-10-15; reported the same day to build and
  pass its tests (`tests/rbtmathpoly_tests.c`) - on one platform, which wasn't
  recorded (macOS is the one in use). Windows built 2026-10-30 and
  Linux 2026-11-04 (above). Clipper2 1.5.4 and earcut.hpp v2.2.4 are fetched at configure time unless
  vendored under `../.deps/Clipper2` and `../.deps/earcut.hpp`.
  `rbtmath_poly_offset_edges` and its tests, added after, likewise build and
  pass (reported 2026-10-15). `rbtmath_poly_skeleton` and its tests were added
  after that.
- **2026-10-30, macOS (Apple M5 Pro), Debug and Release:** the polygon
  work of card A21 and the autonomous list (below): `rbtmathpoly_tests`
  with its new skeleton, location and round-join tests, and
  `rbtmathpoly_bits_tests`, whose hashes Debug and Release both match;
  plain and under ASan. A caller's model tests (its roofs use the offset
  and the skeleton) pass over it. Windows and Linux weren't built then;
  the bits test has passed on both since (2026-10-30 and 2026-11-05 on
  Windows, 2026-11-04 on Linux, above).
- **2026-10-17, macOS (Apple M5 Pro), Debug and Release:** all nine of
  Rabbittus' headless tests pass, `rbtmathpoly_tests` and the skeleton's
  among them, and a caller's model tests (its roofs use the offset) pass over
  it. Before that day's fix `rbtmathpoly_tests` failed one check,
  `offset_moves`: a square moved in past half its width came out as a 2x2
  square, not nothing (see "Offset: edges that go", below).

## rbtmathnoise: noise from double places (card A56, decided 2026-11-17)

Decided 2026-11-17 with you: `rbtmath_noise_draft.h` approved, every
question as recommended: OpenSimplex2 for simplex (Q1a); gradient
(improved Perlin) kept beside it, the kind that tiles (Q2a); an optional
analytic gradient on every kind, refused for ERODED (Q3a); warping a call
of its own with Jacobians (Q4a); 64-bit cells and a 24-bit fraction (Q5a);
a kind's bits frozen once released, a new algorithm a new kind (Q6a);
cellular exact at any jitter (Q7a); no 4D now (Q8a). No version raise
while 0.3 is untagged (card X2). Built as `math/rbtmathnoise.h` (the
meta repo's `ROADMAP.md` names the session), its bits recorded on the M5
Pro for Windows and Linux to match. Third-party
code it is compared with (FastNoise Lite and the like) is cloned, if at
all, into the workspace's `.deps/`, never into a project (the user,
2026-11-17).

**Amended by the third critique round, 2026-11-17 (the draft's version
3, approval kept):**
- **Section 5a is the definition:** every kind's evaluation in written,
  parenthesised float order, every constant a hex literal, the gradient
  tables in index order, the turns, kernels, cellular search and fractal
  sums; only the four scale constants are left for the build to find and
  write into the header, in hex, before any bits are recorded.
- **`RBTMATH_NOISE_ORIENTATION`** (UNKNOWN 0 refused for 3D simplex, EVEN
  1, Y_UP 2, Z_UP 3), the desc's last field, read by 3D simplex only: EVEN
  on a sphere or in a volume, the up axis for a flat world's 3D noise.
- **A finding against OpenSimplex2's reference:** its 3D sums two points
  of each cubic lattice, but up to four of one lattice (six in all) lie
  inside its kernel, so it drops kernel tails and steps by up to 0.0055
  (0.0046 off the full sum) where its choice of point changes (measured
  in a throwaway program). The draft sums every point inside the kernel
  (the 8 corners of each lattice's cube, 16 candidates), so its bits are
  deliberately **not** OpenSimplex2's reference bits; lattice, turn,
  gradients and kernel are kept. Its test 13 confirms the finding first
  (a brute-force sum over every lattice point within 3 cells, and
  continuity across the reference's cut planes); if the finding does not
  hold, the build stops and cards it.
- Octave seeds `s_i = H(seed ^ X_i)` and a warp channel's `seed ^ Y_a`
  (fixed hashes; the old additive seeds lined one world's octaves up with
  another's); a cell's value from its feature's id; RIDGED's sign 0 below
  2^-100; `FLT_EVAL_METHOD == 0` asserted; the warp's refusal before
  anything is written; the finest octave's frequency checked finite; the
  octave record 56 bytes with three 64-bit periods; 3D simplex about 20
  to 25 ns an octave, a 33 x 33 chunk of four layers and a warp 0.65 to
  0.9 ms a core (estimates). Test 12 holds the coarser as the finer's sum
  stopped early (the coarser plus the last term equal to the finer),
  never a subtraction.
- **Contraction:** the guard must be the compiler's ID, not `MSVC`
  (clang-cl contracts by default; MSVC 17.0 or later): card A63, which
  also covers this repository's existing bit-exact sources.

**Built 2026-11-17 on the Mac (the session card A56 names, its Part 1), from the approved
draft's version 3:** `include/math/rbtmathnoise.h` (the definition in its
`@verbatim`, the four scale constants found and written in hex:
gradient 2D `0x1.6cbdaap+0`, 3D `0x1.ee0a2ep-1`, simplex 2D
`0x1.8f58eep+6`, 3D `0x1.91838ap+3`, each the float just below 1 over the
octave's exact maximum, the clamp never acting in 10^9 samples a kind),
`src/math/rbtmathnoise.cpp` and `rbtmathnoise_internal.hpp`, struct types
4007 and 4008; built with contraction off by the compiler's ID
(`RABBITTUS_NO_CONTRACT`, card A63's rule), `FLT_EVAL_METHOD == 0`
asserted. Test 13's finding confirmed first against OpenSimplex2's
reference (cloned into `../.deps/OpenSimplex2-4cd120d` for a scratch
program only): the reference is up to 0.0041 off the full sum and steps
by up to 0.0054 where its point changes; the full sum is continuous.
Tests: `rbtmathnoise_tests` (1 to 13; ctest at reduced counts, `--full
[name]` at the draft's, every test passing at full counts in Release),
`rbtmathnoise_bits_tests` (88 records, `tests/data/rbtmathnoise/bits.inc`,
the same in Debug and Release and with FZ set), `rbtnoalloc_tests`' noise
scenario (none of ours), the misuse harness. Three of the draft's test
bounds changed by what full counts measured (`claude_made_decisions.md`,
2026-11-17, "Noise in Rabbittus Math as built"): test 13 within 2^-19,
not 2^-20 (the library's float sum measures 1.06e-6 off the double one);
continuity's resolution taken where the library rounds; test 2's kinks
found by second differences too, and RIDGED's clamp kink skipped. Every
Rabbittus ctest in Debug and under ASan and TSan. Windows (MSVC) and
Linux (GCC, clang) owed: the bits test's flush controls for x86 are
written, not compiled (card X25).

**Measured** (`rbtmathnoise_bench`, Release, the M5 Pro, one core, ns a
place, best of 5 x 65,536; a gradient costs about nothing more, and 1e7 m
out the same as near): 2D gradient 12 a single octave, 73 an 8-octave
FBM; 2D simplex 12 and 91; 2D cellular 89 and 711; 3D gradient 26 and
181; 3D simplex 39 and 285 (about 36 an octave, **the draft estimated 20
to 25**); 3D cellular 374 and 3,030; points, offsets and a grid alike
(92 for a 4-octave 3D gradient FBM); a 3D warp of a 4-octave simplex FBM
450 (443 with Jacobians); nearest cells 92 (2D) and 380 (3D). **A
terrain chunk's proxy (33 x 33 places, four layers and a warp, with
gradients) 2.73 ms on one core, against the draft's 0.65 to 0.9 ms
estimate:** L67's chunk budget is to be set from this, not from the
estimate (a caller's chunk budget is to be set from this).

## Deterministic elementary functions (card A61, decided 2026-11-17)

Found 2026-11-17 by a caller's terrain draft's third critique round: a
tangent warp needs tan and atan, a crater's size law exp2 and log2, and
libm's bits differ between Apple's libm, glibc and the MSVC runtime.

Drafted 2026-11-17, not approved: `rbtmath_functions_draft.h` (version
1, seven critique passes), the card's recommendation written out. A new
header, math/rbtmathfunctions.h: tan, atan, atan2, exp2, exp and log2 in
double, batch calls (arrays in, arrays out, in place allowed), each
defined by a written evaluation (range reduction, Remez polynomials in
hex, compensated last steps, no contraction, no libm call), every result
within 1 ulp of the exact value (measured against MPFR over 10^7 inputs
a range, not proven; largest 0.97, tan). The shell medium's
portable_exp moves onto the new exp (4.30 ulps to 0.71; its CRC
7d88a849 recorded anew), and rbtmathmedium.cpp onto
RABBITTUS_NO_CONTRACT. Questions for the card: Q1 own evaluation, not
CORE-MATH or musl vendored; Q2 double only; Q3 batch calls only, scalar
internal, nothing inline; Q4 exp public, not exp2(x log2 e); Q5 atan2
included; Q6 tan to 2^20, a NaN past it; Q7 sin and cos later; Q8 bits
frozen from the first release (each recommended a). **Approved
2026-11-17 with the user, every question as recommended.** Built
2026-11-18 on the Mac (below); Windows and Linux owed. Card A63, decided a the same day, applies with it: the bit-exact
sources take their contraction flags by compiler ID (MSVC `/fp:precise`
with Visual Studio 2022 17.0 or later, clang-cl
`/clang:-ffp-contract=off`, other Clang and GCC `-ffp-contract=off`), one
CMake function, before A56's bits are recorded on Windows.

**Card A63 built 2026-11-18 on the Mac** (the session the meta repo's `ROADMAP.md` names, first):
`cmake/rabbittus_no_contract.cmake`, `rabbittus_no_contract(TARGETS ...
SOURCES ...)`, the flag chosen per language by `CMAKE_<LANG>_COMPILER_ID`
and `_FRONTEND_VARIANT` (clang-cl `/clang:-ffp-contract=off`, other Clang
and GCC `-ffp-contract=off`, MSVC `/fp:precise` refusing cl below 19.30,
any other compiler refused), appended to a source's options. It replaces
`RABBITTUS_NO_CONTRACT` and the "if (NOT MSVC)" block: the noise, the
functions, the polygons and Clipper2, the BVHs, the shell media, Rabbittus
Text's three sources and its four dependencies, Rend's `rbtrendlit.cpp`,
and the noise's, the functions' and the BVH bits tests' targets. On the
Mac every one is `-ffp-contract=off` as before (read in the generated
build files) and every hash held; the MSVC and clang-cl branches are
written, not compiled (Windows owed, with A56's bits there). No carrottus
source set contraction flags.

**A61 built 2026-11-18 on the Mac** (MacBook, M5 Pro, macOS 27, Apple
clang; the session the meta repo's `ROADMAP.md` names), from the approved draft's version 1:
`include/math/rbtmathfunctions.h` (the six batch calls, the macro, the
Definition in its `@verbatim`, section 5 word for word),
`src/math/rbtmathfunctions.cpp` and `rbtmathfunctions_internal.hpp`
(namespace `rbtfn`, constexpr constants, `FLT_EVAL_METHOD` asserted, fast
math an `#error`), under `rabbittus_no_contract`. No ABI break: seven
additions (`check.sh --update-abi`), the misuse harness regenerated (the
six calls in its `ZEROES_SUCCEED`: a count of 0 is OK with any pointers).
- **Tests first, seen failing** against NOT_IMPLEMENTED stubs:
  `rbtmathfunctions_tests` 49 failures and 6 tests pending,
  `rbtmathfunctions_bits_tests` every record refused; then all passing.
  Tests 1 to 7 as the draft's section 10 (special values against their
  bits, exp's edges and tan's at 2^20 MPFR's correctly rounded values,
  exact exactly; exp2(n) and log2(2^n) for every n in [-1074, 1023]; odd
  bit for bit over 2^20; the reference table; arrays, slices, a count of
  0; refusals in order with their messages; two threads on a pool),
  `rbtnoalloc_tests` (each call over 10,000 elements, 100 frames: none of
  ours). Every Rabbittus ctest (46) passes, Debug and Release.
- **The accuracy reference:** `tests/rbtmathfunctions_reference.c`, run by
  hand with Homebrew's MPFR 4.2.2 (its first lines have the commands),
  wrote `tests/data/rbtmathfunctions/reference.inc`: 21 ranges of 4,096
  (the draft's section 3, atan's interval edges, atan2's one subnormal,
  tan within 64 ulps of its switch), 3.6 MB, the same file on a second
  run. Every result within 1 ulp of it. A throwaway run over 2 x 10^5
  inputs a range against MPFR gave the draft's figures (tan 0.914 over
  [-2^20, 2^20], 0.954 about its switch; atan2 0.840; exp2 0.782; the
  shares above half an ulp within a tenth of a percent of the draft's).
- **The fitter:** `tests/rbtmathfunctions_fit.c` (Remez in MPFR at 400
  bits, run by hand): every fit's minimax error is the draft's, and the
  first three to six coefficients of each polynomial are the
  Definition's; the higher ones (35 of 56) differ in their last bits, at
  the Definition's bounds and at the exact ones, and don't move between a
  4,000- and a 16,000-point grid. As doubles both polynomials have the
  same error on a grid (E2 2^-54.45 both, TN 2^-54.81 against 2^-54.83):
  the draft's throwaway fitter stopped at a nearby polynomial. The
  Definition's coefficients stay the normative ones.
- **Bits recorded on the Mac** (`tests/data/rbtmathfunctions/bits.inc`,
  29 records of 2^20 inputs, Debug and Release alike). A build of the
  same source with `-ffp-contract=on` moves 19 of them, `fast` 24.
  Windows (MSVC, x64) and Fedora (GCC and Clang, x64) owed against them,
  with x86's FTZ and DAZ (written, not compiled).
- **A finding against the draft, flushed subnormals:** the draft said a
  subnormal input to atan2 read as 0 gives "the same angle to within an
  ulp". It does only where the other argument is far larger: with both
  small, flushing gives the angle for a zero (-pi/2 against -pi/2 +
  0.002, 0 against a normal 2^-282). The header's "Same bits" now says
  what happens (exp2, exp, log2 and atan2 read a subnormal input as a
  zero of its sign and give a zero where a result would be subnormal;
  tan and atan return a subnormal input as it came), and the bits test
  checks exactly that against the default environment's results on the
  zeroed inputs. The default environment's bits are untouched.
- **The shell medium moved onto it:** `portable_exp` and its `std::ldexp`
  are gone; `rbtmathmedium.cpp` calls `rbtfn::exp`. Its CRC is **still
  7d88a849**: the medium writes floats, and over 10^8 arguments in [-30,
  0] the two exponentials' doubles differ on 59.8% and their floats on
  none, so no recorded figure that rests on it moved (rabbittustests' and
  the caller's; RBTREND_TODO.md item 52).
- **Measured** (`tests/rbtmathfunctions_bench.c`, Release, the M5 Pro, one
  core, 2^20 elements, best of 7, two runs): ns an element, ours against
  Apple's libm: exp2 3.0 / 1.3, exp 2.2 to 2.3 / 4.1 to 4.3, log2 3.0 to
  3.2 / 1.5 to 1.7, atan 6.7 to 6.9 / 8.5, atan2 13.2 to 13.3 / 16.7 to
  16.8, tan 9.9 to 10.4 / 4.7 to 4.9. The medium (the earth's, 10,000
  rays of 64 steps; 262,144 densities), portable_exp then this exp:
  transmittance 9.74 to 9.84 ms (15.2 to 15.4 ns a step) then 11.46 to
  11.53 ms (17.9 to 18.0), density 11.9 to 12.1 ns a point then 9.8 to
  10.0: the density faster, the march about 18% slower (the draft
  measured the exponential alone faster, 3.7 ns against 5.0).
- **Why the march was slower, and the fix (2026-11-18, the Mac):** not
  the exponential. With two of `rbtfn::exp` inlined into it,
  `densities_at` costs clang 710 against its threshold of 250, so it
  stopped being inlined into the march's loop and the density's (clang's
  `-Rpass-missed=inline`: "too costly to inline"); with `portable_exp`,
  itself left out of line (cost 250), `densities_at` was 160 and went
  inline. Each step then made a call, its densities through memory and
  the medium's coefficients loaded again after it. `densities_at` is now
  forced inline (`always_inline`, MSVC's `__forceinline`), which moves no
  operation: the CRC is **still 7d88a849**, Debug and Release, and every
  test passes. Release, the M5 Pro, best of 7, three runs each, before
  then after: transmittance 11.94 to 11.99 ms (18.7 ns a step) then 7.48
  to 7.60 ms (11.7 to 11.9), now 23% faster than `portable_exp`'s 9.8;
  density 10.20 to 10.28 ns a point then 9.69 to 10.00. Windows and
  Linux owed (MSVC's `__forceinline` not compiled).

## Cloud density (card A57, decided 2026-11-17)

Decided 2026-11-17 with you: a cloud layer's density (coverage from a
map, types' height profiles, erosion over the baked noise) is Rabbittus
Math's, beside `rbtmathmedium.h`, so a caller can ask the density at a
place on the CPU and agree with what Rend draws; the noise volumes are
baked from `rbtmathnoise` on the caller's jobs. Its layers' bounds a
spherical shell or a flat slab. To be drafted with Rend's clouds (its
item 55). Card A59 (fog near the eye) may add a height fog's and a fog
volume's density here too.

Drafted 2026-11-18: `rbtmath_clouds_draft.h` (version 1, seven critique
passes), approved 2026-11-18 with the user, every question as recommended; built 2026-11-18 (below). A new `math/rbtmathclouds.h` (the
density over two baked volumes and a coverage map, the same bits
everywhere, A61's atan for the equi-angular warp) and two additions to
`rbtmathnoise.h` (`rbtmath_noise_volume_bake`, `_sample`: tiling volumes of
noise in bytes, PERLIN_WORLEY and WORLEY). The bake estimated at about 4.1
s a 128^3 volume on one core (five cellular octaves a texel at the
measured 374 ns), not the card's "cheap"; measured at the build.

Card A59's half drafted 2026-11-18: `rbtmath_fog_draft.h` (version 1,
seven critique passes), approved 2026-11-18 with the user, every question as recommended; built 2026-11-18 (below). A new
`math/rbtmathfog.h`: a height fog's density and its transmittance over a
segment (a plane's closed form; a sphere's by an approximation, question
QF1), and a fog volume's density over the WORLEY volume.

**The noise volumes, built 2026-11-18 on the Mac (M5 Pro, macOS 27), for
card A59** (the clouds' density, `rbtmathclouds.h`, built later the same
day, below):
`rbtmath_noise_volume_bake()` and `_sample()` in `rbtmathnoise.h`, exactly
as `rbtmath_clouds_draft.h` describes them (its section 3a and 3b written
into the header's Definition), so card A57 takes them unchanged. Each
texel made a row at a time on the stack (12 KiB) through the noise's own
per-place evaluation; the read shared with the fog
(`rbtnoise::volume_read`, `src/math/rbtmathnoise_internal.hpp`).
`tests/rbtmathnoise_volume_tests.c` holds the clouds draft's tests 1 to 4
and the refusals (every texel of a 16^3 of each kind against
`rbtmath_noise_compute_points`; a 64^3 whole, in runs of 1, 7 and 64
slices and on 1 and 4 threads, the same bytes; the tiling; 10^5 reads
against a trilinear in double, worst 1.32e-7 against the 2^-20 bound),
seen failing first (21 failures against stubs returning
NOT_IMPLEMENTED). Measured (Release, one core, `rbtmathfog_bench`): a
texel 1.96 us of either kind, a WORLEY 32^3 64 ms, a PERLIN_WORLEY 128^3
4.1 s at that rate (the draft's estimate of 4.1 s held). Windows and
Linux never built.

**The density, built 2026-11-18 on the Mac (M5 Pro, macOS 27), Debug and
Release** (card A57's stage 1 build, `rbtmath_clouds_draft.h` as approved,
every question as recommended: QM1 a, QM2 a, QM3 a, QM4 a, QM5 a):
`math/rbtmathclouds.h` (`RBTMATH_CLOUD_SHAPE`, `_BOUNDS`, `_TEXELS`,
`_OFFSETS`, frozen; `RBTMATH_CLOUD_BOUNDS_KIND`, `_CUBE_WARP`,
`_MAP_ADDRESS`; `RBTMATH_CLOUD_MAX_TYPES`, `_MAX_COVERAGE_SIZE`;
`rbtmath_cloud_compute_density()`), `src/math/rbtmathclouds.cpp`
(contraction off, its read of the volumes rbtmathnoise's
`rbtnoise::volume_read`, the equi-angular warp's arctangent A61's
`rbtfn::atan`). No struct type, no result code; ABI additions only. One
shape chosen alone: a point inside the layer whose base or detail
coordinate passes `RBTMATH_NOISE_VOLUME_MAX_COORDINATE` is refused (its
index named), as `rbtmath_fog_volume_compute_density` refuses one, so the
volume read's contract holds; a point outside the layer isn't read and
isn't refused. Tests, seen failing first (452 failed checks in
`rbtmathclouds_tests` and 12 in the bits test against a stub returning
NOT_IMPLEMENTED):
- `rbtmathclouds_tests`: each step of the Definition worked by hand over
  constant volumes and maps (a profile's corners, c 0 by the map and by
  the scale, a map-free layer's bias, d0 0, e2 1, the type blend, the
  density factor, h outside); the density against a reference of the
  Definition written again in the test over random volumes and maps,
  4,096 points each of a shell of each warp and a slab of each address:
  the same bits; a shell and a slab of the same thickness the same within
  1e-5 at equal heights; a slab's REPEAT at s + 1 and CLAMP's edge; the
  cube's face table along each axis, at edges and corners (ties to x,
  then y), each face's sc and tc signs, PLAIN and EQUI_ANGULAR equal at a
  face's centre and its edges' midpoints and different between; every
  refusal of every check in order, its message, nothing written.
- `rbtmathclouds_bits_tests`: FNV-1a hashes of 4,096 densities of each of
  four layers (shells of both warps 6,361 to 6,365 km, slabs of both
  addresses) over a 32^3 bake of each kind and a random map, recorded on
  the Mac (`tests/data/rbtmathclouds/bits.inc`), the same in Debug and
  Release and with subnormals flushed (FPCR.FZ); Windows and Linux to
  match (card X25). The bakes' own hashes are `rbtmathfog_bits_tests'`.
- `rbtnoalloc_tests`: 4,096 densities of a shell and a slab and a base
  slice baked a frame, 100 frames: none of ours.
- `rbt_misuse_tests` regenerated (425 calls with zeroes).
Measured (Release, one core, `rbtmathclouds_bench`, 65,536 points over
Nubis's sizes and a 512 map): a density 56.8 ns a point (shell, PLAIN),
79.1 (shell, EQUI_ANGULAR), 53.6 (slab), against the draft's 60 to 120
ns; the bake 1.90 us a texel, a PERLIN_WORLEY 128^3 3.99 s, a WORLEY 32^3
60 ms (the draft's 4.1 s and 61 ms held). Windows and Linux never built.

## Fog density (card A59, decided 2026-11-17)

**Built 2026-11-18 on the Mac (M5 Pro, macOS 27), Debug and Release**:
`math/rbtmathfog.h`, `src/math/rbtmathfog.cpp` (contraction off), every
question as recommended (QF1 a, a closed outward depth: built as Schüler's, then fixed to the Chapman function's large-x form, below; QF2 a: held
below the base; QF3 a: the shared WORLEY volume). Tests
(`rbtmathfog_tests`, `rbtmathfog_bits_tests`), seen failing first (56
failures against stubs; the bits test with nothing recorded):
- A plane's transmittance within 1e-5 relative in tau of a 2^16-step
  march over 10^4 segments (level, steep, crossing the base, long and
  grazing, 1e-4 long); g continuous across 2^-4; a level segment exact.
- **A sphere's: the Chapman function's large-x form** (fixed 2026-11-18
  the same day; QF1's intent, a closed outward depth, its error measured).
  The first build used c / ((c - 1) mu + 1), c = sqrt(pi x / 2), written
  from memory as Schüler's (GPU Pro 3, 2012): measured against the Chapman
  function itself (its integral, numerically, x from 20 to 10,000, mu 0
  to 1) it was up to 14.8% low at mid angles (x = 10,000, mu 0.01; 13% at
  x = 1,500, mu 0.05), which the segments' difference of two outward
  depths carried into tau as 0.225; the segment logic was not at fault
  (with an exact Chapman function the same segments are within the
  form's own error). The paper couldn't be read here (its publisher's and
  Shadertoy's pages refused the fetch), so the formula was never checked
  against it. Now C(x, mu) = sqrt(pi x / 2) erfcx(sqrt(x / 2) mu)
  (Wikipedia's "Chapman function", read 2026-11-18: 2% at x = 50, 0.1% at
  x = 800), erfcx by erf's series below 2 and Laplace's continued fraction
  from 2, within 1e-8 (the header's Definition). The form is low by about
  1 / x straight up: 4.4% at x = 20, 1.9% at x = 50, 1.3% at x = 75,
  0.07% at x = 1,500; that is the method's limit, for fogs of few scale
  heights in radius. Against the march over test 4's segments: worst
  **0.00067** at a 100 m scale height (x 1,500; bound
  SPHERE_TAU_BOUND_100M 0.001) and **0.0129** at 2 km (x 75, on steep
  segments: the form's 1.28% at mu 1; bound SPHERE_TAU_BOUND_2KM 0.015).
  The first build's test bound of 0.25 is gone.
- Bits: CRCs of 4,096 densities and transmittances of each kind, volumes
  of each shape with and without noise, and a 32^3 bake of each kind
  (`tests/data/rbtmathfog/bits.inc`), Debug and Release alike ("sphere
  transmittance" re-recorded once with the fix, 0x0A9590D0AF0C2690 to
  0xB1EBDB4C0C75E869; the rest unchanged); Windows
  and Linux owed (card X25).
- `rbtnoalloc_tests`: each call over 4,096 points a frame, a slice baked
  and read: none of ours.
- Measured (Release, one core, `rbtmathfog_bench`): a plane's density 5.2
  ns a point (estimate 10 to 20), a plane's segment 20.5 ns (30 to 50), a
  sphere's 137 ns (80 to 150; 49.8 ns with the first build's form), a box
  volume with noise 18.5 ns (40 to 70), a volume's read 14.0 ns.
Not built: Windows and Linux.

## rbtmathbvh: shapes and an instance BVH changed in place

**Drafted 2026-11-05, revised 2026-11-06 after two reviews, approved by the
user 2026-11-06:** `rbtmath_bvh_queries_draft.h`. Spheres, capsules and boxes
(`RBTMATH_SHAPE`) tested against a mesh or an instance BVH (overlaps) and
moved along a line to their first touch (sweeps), by one GJK algorithm,
a sweep walking the tree as a ray against boxes grown by the shape's;
an instance BVH's instances set in place, refitted and rebuilt with no
allocation, its surface-area cost read to choose between them; a box BVH
over solid boxes the caller gives, with the same queries and upkeep;
`RBTMATH_ERROR` moved to `math/rbtmatherror.h`; `rbtmathbvh.cpp` built
without floating-point contraction. No ABI break.

**Built 2026-11-06 on Windows 11** (ThinkPad P1 Gen 5, MSVC 14.51;
`rabbittus-windows-d3d12` and `-vulkan`, Debug and Release; macOS and
Linux not built, so the contraction flag and the bits test's hashes on
Clang and GCC are still to see). Every ctest passes on both presets in
both configurations but `rbtrend_contract_tests` (card A49's
`lit_fit_cascades`, as before), and under both `-asan` presets (from a
Visual Studio developer prompt) the same, no sanitizer report. New or grown: `rbtmathbvh_tests` (exact
cases; 3,000 overlaps and 3,000 sweeps against brute force - exact
point-, segment- and separating-axis distances, a march of them bisected
to 1e-9 - with no near touch to skip; 1,000 sliver triangles grazed by
capsules, GJK's capped cases; sweeps of INFINITY through 100,000
instances against each instance's own; tree independence - created, set
and refitted, rebuilt, created fresh: the same bits; the in-place calls'
refusals in parameter order, each changing nothing; the box BVH against
brute force; the cast's early stop), `rbtmathbvh_bits_tests` (new: casts,
overlaps, sweeps, a refit, a rebuild's cost and the box BVH hashed,
recorded here, Debug and Release alike, `tests/data/rbtmathbvh/bits.inc`;
the same on macOS, Apple M5 Pro, 2026-11-09, once the test itself was
built with no floating-point contraction: clang had fused its inputs'
multiply-adds on arm64, so the inputs, not the BVH, differed),
`rbtnoalloc_tests` (an instance BVH of 10,000 moving, set, refit or
rebuilt, 8 shapes overlapped and swept a frame; a box BVH of 100,000, every
call: none of ours), `rbtoverlap_tests` (queries, overlaps and sweeps
from two threads; sets on disjoint instances from two threads, then a
refit; in Debug, the six overlaps the rule forbids reported and the two
it allows not), and the misuse harness (140 calls with a destroyed handle,
ten times in Debug, no failure). The Debug check of shared roles is new
(`rbt_shared_overlap`, `src/rbtoverlap.hpp`). How the build differs from
the draft, none in a contract: `../claude_made_decisions.md`, 2026-11-06.

Numbers, Release, the ThinkPad (`tests/rbtmathbvh_bench.c`, a 224-triangle
ball placed 100, 10,000 and 100,000 times at the density of 10,000 in a
100 m cube). Casts before and after (`--casts`, best of three runs each):
nearest 1.42-1.50 M / 367-376 k / 225-234 k rays a second before, 1.43-1.50
M / 365-379 k / 217-227 k after - unchanged; the early stop of a cast that
asks only whether is worth little here (the scene's first hit is near the
nearest); create 4.8 and 60 ms before at 10,000 and 100,000, 6.0 and 70-78
ms after (each instance is placed twice: checked before anything is
allocated, then filled). Shapes and sweeps (100 / 10,000 / 100,000):
overlaps_shape 5.6 M / 3.1 M / 1.9 M a second; overlap_all 5.4 M / 2.1 M
/ 1.4 M; a sweep of 10 m 259 k / 119 k / 115 k; of INFINITY 220 k / 70 k /
54 k, beside rays along the same lines 1.31 M / 249 k / 159 k (the
sweep's walk is a ray's: within 5 times). Upkeep a step, set and refit
against set and rebuild against a fresh create:

| instances | moving | set + refit | set + rebuild | create | cost: built, refitted 60 steps, rebuilt |
|---|---|---|---|---|---|
| 100 | all | 15 us | 40 us | 44 us | 8.6, 11.3, 7.0 |
| 100 | 1% | 0.4 us | 21 us | 41 us | 7.0, 7.0, 7.0 |
| 10,000 | all | 2.07 ms | 6.62 ms | 7.74 ms | 55.4, 104.3, 52.2 |
| 10,000 | 1% | 52 us | 5.51 ms | 8.76 ms | 52.2, 49.9, 48.7 |
| 100,000 | all | 22.3 ms | 91.3 ms | 101 ms | 127.6, 257.5, 122.8 |
| 100,000 | 1% | 0.75 ms | 73.1 ms | 101 ms | 122.8, 120.2, 117.3 |

Refitting all of them for 60 steps of random drift doubles the cost a ray
pays; with 1% moving it doesn't move. The ratio to rebuild at is the
caller's. A set computes each instance twice (checked, then placed, so a
refused set changes nothing): about half of an all-moving step's set.

- [ ] Build and run on macOS and Linux: the bits test against these
  hashes, `-tsan` on `rbtoverlap_tests`.

## rbtmathpoly

### Before it's relied on

- [x] **Build and run the tests** on Windows (MSVC: the 128-bit products are
      written out for it, and `/fp:contract` must stay off) and Linux:
      Windows 2026-10-30 and 2026-11-05 (MSVC 14.51), Linux 2026-11-04
      (GCC 16.2.1, clang 22.1.8), every Rabbittus Math test passing.
- [x] **Same bits everywhere** (2026-10-30): `tests/rbtmathpoly_bits_tests.c`
      runs a fixed set of booleans (every operation and fill), point
      locations, triangulations, offsets (mitred and round, out and in, a
      hole) and skeletons (a hole, mixed weights, a merge on one line), and
      compares an FNV-1a hash of every output with the 107 values recorded
      on macOS (`tests/data/rbtmathpoly/bits.inc`), which Debug and Release
      both give. `--print` writes them out. The same values on Windows
      (MSVC, 2026-10-30 and 2026-11-05) and Linux (GCC 16.2.1, 2026-11-04).
- [ ] **Clipper2 under this repo's warnings.** Its sources are built with this
      repo's flags, not its own `-Werror`; they build with C++26 on the one
      platform reported, so check GCC and MSVC.

### Cost

- [ ] **Tagging a boolean's result** compares every result edge with every input
      edge: fine for outlines of tens or hundreds of edges, not for thousands.
      A uniform grid or a sort by x over the input edges would make it close to
      linear. Measure first.
- [ ] **`rbtmath_poly_validate`** tests every pair of edges, likewise. A sweep
      line would make it n log n.
- [ ] **`rbtmath_poly_skeleton`** looks for the next event from scratch after
      every event: about n^3 for n edges. A priority queue of events,
      invalidated as vertices end, is the standard n^2 log n; worth it only if
      a caller brings polygons of hundreds of edges.

### Offset: edges that go

Fixed 2026-10-17. Each edge was moved and its corners re-met, and the union
was left to remove what a large inward move turned over. A ring moved in
past its width comes out turned through half a turn (a convex ring's moved
corners each land past the opposite one), which winds the same way as the
ring, so the union kept it. `move_edges` now runs the move from 0 to 1 and
takes out each edge whose ends meet, earliest first, so its neighbours meet
directly; `test_offset_collapses` covers a square, a thin bar and a triangle
shrunk and shrunk away, and a notch grown shut. Left:

- [ ] **Splits are still the union's.** A ring pinched in two by an inward
      move (a narrow waist) crosses itself, and the union takes the crossed
      part out, which is right for a waist and has no test. A pinch where a
      reflex corner meets an edge, then an edge of one half goes later, is
      worked out on the unsplit ring: no test either. The skeleton handles
      splits properly; an offset from it (the wavefront at a time) would be
      the exact version, once it takes holes.
- [ ] **Bevels and the mitre geometry.** Which edges go is worked out from
      the mitred corners, even where a corner is then drawn bevelled: an
      edge beside a sharp corner can go a little early or late against the
      bevel drawn. Untested.
- [ ] **Cost**: each edge taken out looks at every edge left, so about n^2
      for n edges where many go; none at all where none does.

### Skeleton: degenerate cases

The simulation is in doubles with a tolerance of 1e-9 of the polygon's size,
and these are the cases it meets least gracefully - none breaks it, but each is
where a result could be less than exact. A test for each, from real footprints,
before relying on them:

- [x] Several reflex corners reaching one edge at once, or one reaching a
      vertex exactly (a "vertex event"): handled as a split at the edge's end,
      then a collapse of the empty edge that leaves. Tested 2026-10-30
      (`test_skeleton_degenerate`): a comb whose two notches reach the
      bottom at once, a chevron whose notch reaches the opposite tip, a
      plus whose four reflex corners meet in the middle, an octagon closing
      at one point; every face closes and they cover the polygon.
- [x] Two edges that come to follow each other on one line mid-way with
      different weights: the faster leads, which is a guess, not a rule.
      **Decided 2026-10-28 with the user (card A21): the rule.** The
      larger weight (the faster edge) wins the shared line; on equal
      weights, the edge earlier in the input. Written in the header and
      tested with a footprint that reaches it, so every platform builds
      the same skeleton. **Built 2026-10-30** (`merge_on_line`): the
      loser's face is closed along the line from the event to its other
      end, whose vertex goes on beside the winner. Tested
      (`test_skeleton_on_one_line`) with a step whose low edge at weight 2
      catches the high one at 1, and a bump whose halves meet at equal
      weight; before the rule both footprints failed outright ("never
      closes").
- [x] A face walk through a node with more than two traces meeting in one
      face: the first unused trace is taken. Since 2026-10-30 the walk takes
      the trace that turns most to the right (the face is on its left),
      ordered by a pseudo-angle from a division, not `atan2`, so it is the
      same everywhere; the plus and the octagon reach it.

### Not built

- [x] **Round joins** for offsets (2026-10-30): `RBTMATH_POLY_OFFSET_DESC`'s
      `join` (`RBTMATH_POLY_JOIN_MITER`, 0, as before, or `_ROUND`) and
      `arc_tolerance`. A corner the move opens is an arc about the original
      corner, the radius going from one edge's distance to the other's,
      halved in angle as often as the tolerance needs with arithmetic and
      square roots only (the same on every platform); a corner the move
      closes is mitred either way, and which edges go is still worked out
      from mitred corners. Tested (`test_offset_round`): every point of a
      square grown by 1000 within a unit of 1000 from it, its area that of
      the rounded square less the chords, finer tolerances giving more
      points, an inward move identical to the mitred one, a hole, uneven
      distances, and the refusals.
- [x] **Skeletons of polygons with holes** (2026-10-30): the set is polygons
      with holes; each ring is a loop of the wavefront, and a reflex corner
      may reach an edge of any loop, which splits its own loop or joins two.
      Tested (`test_skeleton_holes`): a centred hole (every face a
      trapezoid), one off to a side, and two holes, the faces covering the
      polygon round them.
- [x] **Point in polygon** (2026-10-30): `rbtmath_poly_locate_point(set,
      fill, point)` gives inside, outside or on an edge, from an exact
      winding number over any rings. Tested against a slow angle sum in
      doubles at 8,000 random points of random crossing rings, under every
      fill rule, and on edges, corners and level with a vertex.
