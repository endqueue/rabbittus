/*
 * rbtrend_large_worlds_draft.h - cascaded shadows that stay still in a
 * space that moves with the eye: one chained struct on
 * rbtrend_lit_fit_cascades() (card L66; its caller's half, drafted beside
 * the caller's headers, says why the caller draws relative to the eye).
 *
 * DRAFT, 2026-11-17, version 3 (version 1, the critique's seven passes,
 * and the third critique round's passes 8 to 14). APPROVED by the user
 * 2026-11-17 (Q3a of the card that carries it, and the same answer for a
 * second caller's), with one correction: no version is raised for it
 * while Rabbittus' 0.3 is untagged (card X2; written into the text by
 * pass 14); amended by the third critique round, 2026-11-17, at the
 * user's asking. Not built, not in any target, not compilable. Written on the MacBook (M5 Pro) against rbtrendlit.h
 * (RBTREND_LIT_CASCADE_FIT, rbtrend_lit_fit_cascades(), "Positions and
 * directions are in any one space of the caller's choosing"),
 * src/render/rbtrendlit.cpp (the fit: "Moved by whole texels: where the
 * scene's origin lands, rounded"), rbtcore.h ("Struct bases", struct types
 * to 5037), rbtmath.h (RbtDVec3) and rbtaudio.h (RBTAUDIO_SPATIAL_DESC
 * chained on RBTAUDIO_MIXER_DESC, the model for a chained struct), as read
 * 2026-11-16 (rabbittus ede3990, 0.3); and Hazel's
 * (../.refs/HazelRenderer2025, present: Renderer/SceneRenderer.cpp's
 * cascades).
 *
 * ===========================================================================
 * Revision notes (the critique's seven passes; each fixed before the next)
 * ===========================================================================
 *
 * Seven passes, 2026-11-16, over both halves together; the caller's half
 * holds the whole record. What they changed or checked here:
 *
 * Pass 1 (wrong, missing, more). Checked that the fit is the only piece
 * of the lit pipeline that reads the scene's origin (its snapping, in
 * src/render/rbtrendlit.cpp); the atmosphere, lights, point and spot
 * shadows, ray tracing and the view need nothing (section 3). No Rabbittus
 * Math type is added: RbtDVec3 exists.
 * Pass 2 (what breaks). Nothing chained, today's bits: test 1. No layout
 * changes.
 * Pass 3 (conventions). Checked: a based struct registered, chained once,
 * each field its own check and message, no caller named anywhere in this
 * file (searched), contraction off for the double sum.
 * Pass 4 (a reviewer). 1. A moving light's crawl is not this struct's to
 * fix. Changed: said in section 2.
 * Pass 5 (the maths). 1. The double snap's step rounded to about 1e-7 of
 * a texel. Changed. Checked: 1e7 m at 41 texels a metre is 4e8 texels,
 * where a float steps by 32.
 * Pass 6 (callers and tests). Checked: a caller that doesn't chain it is
 * untouched; tests 2 and 3 show the drift without and the same bits at
 * the origin with.
 * Pass 7 (records). RBTREND_TODO.md gains the item when it is built
 * (the caller's half, section 16).
 *
 * Third critique round, 2026-11-17, at the user's asking: passes 8 to 14
 * (the caller's half holds the round's whole record). Re-read for it:
 * rbtrendlit.h (RBTREND_LIT_CASCADE_FIT, rbtrend_lit_fit_cascades(), its
 * includes), src/render/rbtrendlit.cpp's fit (the snap: matrix times the
 * origin, rounded in float), rbtcore.h (struct types to 5037; 5038 still
 * free, the atmosphere-from-space draft taking none and the lit layers
 * draft taking 5039 after it), rbtmath.h (RbtDVec3), the
 * atmosphere-from-space draft (card A55) and RBTREND_TODO.md item 58 (card
 * L78, a directional light given a place).
 * Pass 8 (general purpose). 1. Section 1's example of a second caller (a
 * site in a national grid) named a domain. Changed: coordinates millions
 * of metres from their origin. Looked at: nothing names a caller.
 * Pass 9 (industry). Looked at and kept: section 4's comparison; added
 * that a light that moves is handled by engines above the fit (its
 * direction held and moved in steps), not by the snap.
 * Pass 10 (the C API). 1. RbtDVec3 is rbtmath.h's, and rbtrendlit.h
 * includes neither it nor anything that does. Changed: [CHANGED]
 * include, as rbtrendatmosphere.h includes ../math/rbtmathmedium.h. Looked
 * at and kept: a based struct, registered, chained once, the error last,
 * each axis its own check.
 * Pass 11 (data). Nothing allocated; the fit stays CPU arithmetic.
 * Pass 12 (threads, determinism, errors). Kept: any thread, no object; the
 * double part's bits the same everywhere (contraction off).
 * Pass 13 (correct against the code). 1. Test 3 said the chain with
 * scene_origin 0 gives today's matrices to the bit: today's snap is worked
 * out in float from the matrix, the chained one in double from the axes,
 * so the fraction differs in its last bits and the matrices with it.
 * Changed: the same whole-texel snap, the matrices within a millionth of a
 * texel. 2. "Any fixed point gives stable cascades": true under a still
 * light; under one that turns, the grid near the view slides by the view's
 * distance from the fixed point times the turn, every fit. Changed:
 * section 2 says it, with the numbers, and that the caller chooses the
 * fixed space (a fixed point near the view slides least); test 7 records
 * it. A light given a place (L78) turns as the view moves past it: the
 * same. The decided shape is kept; the caller's half reports the gap.
 * Pass 14 (tests, records). 1. Card X2: the "0.3 to 0.4" goes. Changed:
 * section 6. 2. Windows and Linux owed named (test 6's bits).
 *
 * Consistency pass, 2026-11-17 (after the third round, across the drafts;
 * no version raise): the turning light's slide (pass 13, section 2) is
 * card A60, open; this anchor is built as decided and test 7 records the
 * slide. Nothing changed here.
 *
 * ===========================================================================
 * 1. Why
 * ===========================================================================
 *
 * Rend draws in any one space of its caller's choosing. A caller with
 * places far from its origin (a world 10,000 km out; coordinates millions
 * of metres from their origin) draws in a space whose origin is the eye, recomputed every
 * frame: floats are then exact where the eye is, at any distance. Every
 * piece of Rend's lit pipeline works in such a space as it is (section 3),
 * but one: rbtrend_lit_fit_cascades() moves each cascade by whole texels
 * where the scene's origin lands, so that as the view moves the cascade's
 * texels stay put on the world and its shadows don't shimmer (Valient's
 * stable cascades). When the scene's origin is the eye, it moves with the
 * eye, and the cascades' texels slide over the world by a fraction of a
 * texel each frame: the shimmer stable cascades exist to remove.
 *
 * What is missing is only where to snap: a point that doesn't move. The
 * caller knows where the scene's origin is in its own fixed space (the
 * world's, in doubles); Rend needs to be told it, and to do the snapping
 * sum in double, since that point can be 1e7 m or more from the eye
 * (sections 2 and 4).
 *
 * ===========================================================================
 * 2. The shape
 * ===========================================================================
 *
 * A struct chained on RBTREND_LIT_CASCADE_FIT (rbtcore.h, "Struct bases":
 * the way a released struct grows; RBTAUDIO_SPATIAL_DESC chained on
 * RBTAUDIO_MIXER_DESC is the model), saying where the scene's origin is in
 * a space that doesn't move. Without it, the fit snaps where the scene's
 * origin lands, as today, and its results are the same bits as today.
 *
 * With it, each cascade is moved by whole texels where the fixed space's
 * origin lands: its light-space x and y, taken from the cascade's centre
 * in double (the centre's place in the fixed space is scene_origin plus the
 * centre, summed in double; dotted with the cascade's light-space axes in
 * double, scaled to texels, and the fraction of a texel worked out in
 * double), then the offset of under a texel applied to the float matrix as
 * today. Nothing else in the fit changes: its splits, spheres, depth and
 * returned matrices stay in the scene's (eye-relative) space.
 *
 * What it doesn't change: a light whose direction moves turns the
 * cascades' axes, and their texels move over the world with it, anchored or
 * not, as today.
 *
 * Why the fixed space's origin and not a point the caller picks: any fixed
 * point gives stable cascades under a still light, and the origin is the
 * one every caller already has; a caller that wants another makes it its
 * fixed space's origin (scene_origin is then the scene's origin's place
 * relative to that point).
 *
 * Which fixed point, when the light turns. The snap keeps the texel grid
 * where the fixed point lands; when the light's direction turns by an
 * angle a between fits, the grid near the view moves by the view's
 * distance from the fixed point times a, besides the cascade's turn about
 * itself (a fraction of a texel, as at any distance). A light turning as a
 * day of 24 hours does, fitted at 60 Hz (1.2e-6 radians a fit), with the
 * fixed point 250 km from the view and a cascade of 41 texels a metre:
 * about 12 texels a fit, a shimmer at every shadow's edge; within 200 m of
 * it, under a hundredth of a texel. A light given a source place (a
 * directional light whose direction is worked out from a place,
 * RBTREND_TODO.md item 58) turns as the view moves past its source, by the
 * view's speed across over the source's distance. So the fixed point a
 * caller gives is best near the view, and a caller whose lights move far
 * from it holds the shadows' direction still and moves it in steps, as
 * time-of-day systems commonly do; the fit itself snaps translation only,
 * as Valient's method does.
 *
 * Why double: the fixed space's origin may be 1e7 m from the eye; in float
 * its landing in a cascade 50 m across at 2048 texels is about 4e8 texels
 * out, where a float steps by 32 texels; in double, by about 1e-7 of a texel.
 *
 * ===========================================================================
 * 3. The rest of the lit pipeline, in an eye-relative space (no change)
 * ===========================================================================
 *
 * - The view (RBTREND_LIT_VIEW): its eye at the origin, its view-projection
 *   the camera's turn and projection. Clustered light culling, the sky,
 *   screen-space reflections and ambient occlusion are worked out in view
 *   or screen space: unchanged.
 * - Lights: their places relative to the eye, floats; a point or spot
 *   light's shadow is rendered from the light and not snapped: unchanged.
 * - The atmosphere (rbtrendatmosphere.h): its centre is given per view, in
 *   the scene's space: relative to the eye, a float the world's size (the
 *   atmosphere draft's own figure: half a metre of height at the earth's
 *   radius, which its tables don't see).
 * - Ray tracing: the top level is built from the instances given, each
 *   frame: eye-relative instances, an eye-relative scene.
 * - Depth: Rend takes the caller's projection; reversed float depth holds a
 *   centimetre's near plane and a far plane of 1e9 m (the caller's half,
 *   section 6, and its test 5).
 * - Nothing in Rend's shaders reads a world-anchored coordinate (no
 *   world-space texture coordinates, no world-space noise): what a caller's
 *   own shader anchors to the world is the caller's, with an offset it
 *   reduces on the CPU (the caller's half, section 12).
 *
 * ===========================================================================
 * 4. Compared
 * ===========================================================================
 *
 * - Hazel (Rabbittus' bar): cascades snapped where the world's origin lands
 *   (SceneRenderer.cpp: shadowOrigin, rounded), in floats, with places in
 *   glm::vec3: stable near the origin, no large worlds. This goes past it.
 * - Unreal 5: shadows set up on the CPU in double world space, rendered in
 *   translated world space (relative to the view); the same split: the
 *   snap in double, the matrices in floats relative to the view.
 * - Unity HDRP: camera-relative rendering, its cascades' stabilisation
 *   worked out with the camera's world position on the CPU; the same.
 * - Godot (precision=double): the renderer relative to the camera, the
 *   shadow fit fed the camera's offset; the same.
 *
 * ===========================================================================
 * 5. Rend's contract, kept
 * ===========================================================================
 *
 * A based struct, its type registered in rbtcore.h's Rend block (5038, or
 * the next free when built); chained once (twice is
 * RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN); taken only by
 * rbtrend_lit_fit_cascades() (elsewhere RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN);
 * each field checked, its own check and message, after the fit's own and
 * before anything is written; RBTREND_ERROR's message naming the field.
 * The fit is plain arithmetic on the CPU: no object, no allocation, no
 * GPU, any thread (as today). rbtrendlit.h includes ../rbtmath.h for
 * RbtDVec3 (Rend's headers took only float arrays until now;
 * rbtrendatmosphere.h already includes ../math/rbtmathmedium.h, so Rend's
 * umbrella already reaches Rabbittus Math's headers). Built with
 * contraction off, so the snap is
 * the same bits on every platform (the fit's float part's bits on Windows
 * and Linux are card A49's, unchanged by this). Doxygen on every item.
 *
 * ===========================================================================
 * 6. ABI
 * ===========================================================================
 *
 * Additions only: one struct type value and one chained struct.
 * rbtrend_lit_fit_cascades()'s contract gains a sentence. No existing
 * layout, enum or result changes; with nothing chained, the fit's results
 * are today's bits. No version raised (card X2: Rabbittus' 0.3 is
 * untagged); ./check.sh --update-abi lists the additions.
 *
 * ===========================================================================
 * 7. Tests (C, through the public API; Rabbittus' rbtrend_contract_tests,
 *    no GPU needed: the fit is CPU arithmetic)
 * ===========================================================================
 *
 * 1. Nothing chained: every existing fit case the same bits as before.
 * 2. Stable under a moving eye: the scene's origin at the eye; the eye
 *    moved 1 cm along the view for 600 fits, scene_origin following it, at
 *    (0,0,0), (1e7, 0, 1e7) and (4.5e12, 0, 0): each cascade's matrix
 *    applied to one fixed world point (its scene place being the world point
 *    less scene_origin, in double, then float) lands within 1e-3 texel of
 *    the same texel fraction every frame. Without the chain, the same run
 *    drifts by the eye's motion (shown, as the reason for the struct).
 * 3. The same snap as today's at the origin: scene_origin (0,0,0) with the
 *    chain moves each cascade by the same whole texels as today's (the
 *    rounded landing equal), its matrices within 1e-6 of a texel of
 *    today's (the chained fraction is worked out in double from the axes,
 *    today's in float from the matrix, so their last bits differ).
 * 4. Refused: a scene_origin not finite, each axis its own check; the
 *    struct twice; the struct chained on another call's struct.
 * 5. rabbittustests' make_destroyed_tests.py and the misuse harness
 *    regenerated (the fit takes no handle; its argument checks join the
 *    harness).
 * 6. Bit for bit on every platform for the snap's double part (the float
 *    part's differences are A49's): recorded on the Mac, matched on
 *    Windows and Linux (owed, card X25).
 * 7. Recorded, not asserted (section 2's turning light): the light turned
 *    1.2e-6 radians a fit for 600 fits, the view still, with the fixed
 *    point 0, 200 m, 250 km and 1e7 m from the view: each cascade's grid's
 *    movement a fit, in texels, against section 2's distance times angle.
 */

/* ========================================================================= */
/* rbtcore.h                                                     [ADDED]     */
/* ========================================================================= */

/*
 * In RBT_STRUCT_TYPE's Rend block, after RBT_STRUCT_TYPE_REND_LIT_ATMOSPHERE:
 *
 *	RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR = 5038, / **< RBTREND_LIT_CASCADE_ANCHOR, chained onto RBTREND_LIT_CASCADE_FIT. * /
 *
 * Why it earns its place: every based struct's type is registered.
 */

/* ========================================================================= */
/* render/rbtrendlit.h's includes                                [CHANGED]   */
/* ========================================================================= */

/*
 * After #include "rbtrendmaterial.h":
 *
 *	#include "../rbtmath.h"
 *
 * Why: RBTREND_LIT_CASCADE_ANCHOR::scene_origin is an RbtDVec3 (third
 * round, pass 10).
 */

/* ========================================================================= */
/* render/rbtrendlit.h                                           [ADDED]     */
/* ========================================================================= */

/**
 * @brief Where a cascade fit's scene space is, in a space that doesn't
 * move, so that its cascades are snapped there (rbtrend_lit_fit_cascades()).
 *
 * [ADDED] Why it earns its place: a caller drawing relative to the eye (so
 * that places far from its origin stay exact) has a scene space that moves
 * every frame; snapped there, cascades shimmer as the view moves. With
 * this chained on, they are snapped where a fixed space's origin lands, and
 * stay still at any distance from it.
 *
 * Chained onto RBTREND_LIT_CASCADE_FIT, once.
 */
typedef struct RBTREND_LIT_CASCADE_ANCHOR {
	/** This struct's type, RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR, and its chain (rbtcore.h, "Struct bases"). */
	RBT_STRUCT_BASE base;
	/**
	 * Where the scene's origin is in the fixed space, in the scene's units:
	 * for a scene whose origin is the eye, the eye's place in the fixed
	 * space. Finite: x, then y, then z, each its own check. The fixed
	 * space's origin is where the cascades are snapped; under a light that
	 * turns, the nearer it is to the view the less they slide (section 2's
	 * "Which fixed point").
	 */
	RbtDVec3 scene_origin;
} RBTREND_LIT_CASCADE_ANCHOR;

/*
 * [CHANGED] rbtrend_lit_fit_cascades()'s comment: after "and is moved by
 * whole texels of its rectangle, so as the view moves its shadows stay
 * still", add: "Moved where the scene's origin lands; or, with an
 * RBTREND_LIT_CASCADE_ANCHOR chained on @p fit, where the origin of the
 * space it names lands, worked out in double: what a caller drawing
 * relative to the eye chains, so its shadows stay still as the eye moves
 * under a still light. A light that turns between fits moves the grid near
 * the view by the view's distance from the snapped origin times the turn."
 * Its @return gains: "a chained anchor whose scene_origin is not finite".
 *
 * [CHANGED] rbtrendlit.h's file comment, after "Positions and directions
 * are in any one space of the caller's choosing - world space, the eye at
 * the origin, any other": "A caller whose places are far from its origin
 * draws with the eye at the origin, each place taken from the eye's in its
 * own precision before Rend sees it, and chains an
 * RBTREND_LIT_CASCADE_ANCHOR onto its cascade fits."
 */
