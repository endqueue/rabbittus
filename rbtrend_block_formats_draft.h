/*
 * rbtrend_block_formats_draft.h - Rend's textures in block-compressed
 * formats (BC1 to BC7 and ASTC LDR) and their levels: created, uploaded
 * and read back (card A54, which names the session that asked; over
 * carrottus/crgpu_astc_draft.h, card S48, which it needs approved first).
 *
 * DRAFT, 2026-11-16, version 8 (version 1, then seven critique passes,
 * each fixed before the next: the revision notes below), for the user's
 * approval. Written unattended
 * on the Mac (Apple M5 Pro) against include/render/rbtrendimages.h,
 * rbtrendcontext.h ("Texture updates and clears", "Results and errors"),
 * src/render/rbtrendimages.cpp and rbtrendimages_internal.hpp, as read on
 * 2026-11-16 (rabbittus 0.3), and Hazel's renderer
 * (../.refs/HazelRenderer2025/Hazel/Hazel: Renderer/Image.h, Texture.h and
 * .cpp, Asset/TextureImporter.cpp, and NVRHI under it,
 * vendor/nvrhi/include/nvrhi/nvrhi.h and src/vulkan/vulkan-texture.cpp).
 * Not built, not in any target, not compilable: it repeats declarations
 * of rbtrendimages.h to show them changed. Every declaration below is
 * marked [ADDED], [CHANGED] or [REMOVED]; nothing unmarked changes.
 * Nothing is built until it and S48's draft are approved; once approved it
 * is frozen (ROADMAP.md, section 1).
 *
 * ---------------------------------------------------------------------------
 * Revision notes (the seven passes, 2026-11-16; lenses as the workspace's
 * critique prompt's, named on card A54)
 * ---------------------------------------------------------------------------
 *
 * 2026-11-17: caller names removed (check.sh caller-names), meaning kept.
 *
 * Pass 1 (wrong, missing, more than the need). 1. The partial-update test
 * named "a 12 x 12 texture in ASTC 5x4", whose level 0 isn't whole blocks
 * (12 isn't a multiple of 5): S48's Q5 refuses it at creation. Changed: 20
 * x 8 (level 0 is 4 x 2 blocks; level 2, 5 x 2 texels, is one block whose
 * region reaches the bottom edge at a height of 2). 2. Checked the sizes:
 * a 2048 chain is 12 levels, 22.4 MB as RGBA8 and 5.6 MB as BC7 or ASTC
 * 4x4. 3. Checked: a block texture's views need nothing new (a shader
 * resource view is the only one its usage allows).
 *
 * Pass 2 (what breaks an existing caller). 1. The first caller's
 * renderer (being built tonight from its approved textures draft; card
 * A54 names it) uploads a texture level by level with
 * rbtrend_texture2d_update(), layer 0: unchanged by this draft, and the
 * first caller to move to rbtrend_texture2d_update_levels() once it is
 * approved (named under "Who else uses it"). 2. Searched: no test, in
 * rabbittustests, Rabbittus or any caller in the workspace, checks the message
 * that changes. 3. Checked: Rend's own uses of a texel's bytes (the
 * readback, the region check) take an uncompressed format's block, one
 * texel of its bytes: the same numbers as before.
 *
 * Pass 3 (CONVENTIONS.md and Rend's contract). 1. The new call didn't
 * say it opens with Rend's overlap and stale checks. Changed: RBT_STALE on
 * the texture after its null check, then RBTREND_ENTER on its instance, as
 * rbtrend_texture2d_update() does. 2. RBT_RESULT_OUT_OF_MEMORY was given
 * for "the levels' bytes overflow": the bytes Rend works out per level are
 * bounded by the device's limits (16384^2 texels, 2048 layers: under 2^40
 * bytes) and can't overflow, and a caller's data_size is compared with
 * them, never summed. Changed: the staging's size (with its padding) is
 * worked out with overflow checked, and the header says no input reaches
 * it; the test that claimed to reach it is gone (no test can). 3. The
 * level array is walked by the call: RBT_PLAIN_DATA beside it in src, so
 * check.sh plain-data lists it. 4. Each check has its own line and
 * message, in parameter order (one `if`, one condition, one result).
 *
 * Pass 4 (what a reviewer would refuse). 1. "How does a caller's cube or
 * array map onto `layer_count`?" A KTX2 level holds its layers, each of
 * its faces in the order +X, -X, +Y, -Y, +Z, -Z, one after another: the
 * order of Rend's cube-capable layers (rbtrendimages.h). Changed: said on
 * the struct, so a cube's KTX2 level is passed as it is, `layer_count` 6.
 * 2. Q3's example message named the format by name; Rend's messages give
 * a format's number ("format %d"), Carrottus having no name for a value.
 * Changed. 3. "Why copy the caller's data into staging rather than let the
 * caller read its file straight into a staging buffer?" A second shape
 * (map, fill, submit) for a copy of 5.6 MB a texture at load (about a
 * millisecond); kept out, measured when built, a card if the copy shows.
 *
 * Pass 5 (the comparison, honest). 1. Godot's RenderingDevice was cited
 * from memory, not read: removed. Unity's, Unreal's, bgfx's and Filament's
 * shapes are each API's documented signature; Hazel and NVRHI were read in
 * .refs tonight (files and lines named). 2. Checked: Hazel has no block
 * format and no ASTC anywhere in its renderer or importer; the bar it sets
 * is NVRHI's BC, which Rend passes.
 *
 * Pass 6 (tests). 1. BC1 with both endpoints equal is BC1's three-colour
 * mode (c0 <= c1), where index 3 is transparent black: the constant block
 * uses index 0 only, said. 2. Added: update_levels of a sub-range (levels
 * from 1, layers 1 and 2 of 3) leaves the other levels and layers as they
 * were. 3. Named the run that sees D3D12's padding (a level of 4 BC7 blocks
 * a row, 64 bytes, staged at 256), for the Windows session.
 *
 * Pass 7 (who else uses it, records, the card). 1. No "who else" section.
 * Changed: added. 2. Records when built named. 3. The card (A54) carries
 * Q1 to Q5, the Video line and these passes in a line.
 *
 * ---------------------------------------------------------------------------
 * Why
 * ---------------------------------------------------------------------------
 *
 * Rend's textures take any CRGPU_FORMAT at creation, but nothing can fill
 * one in a block-compressed format: rbtrend_texture2d_update() and
 * rbtrend_texture2d_get_data() refuse a format with no whole number of
 * bytes to a texel ("a block-compressed format has no update or readback
 * path", rbtrendimages.cpp), and their staging re-packs rows of texels. A
 * game's textures ship as BC7 or ASTC (card S48 names the caller), so every caller that
 * draws a cooked texture needs this: a game engine's renderer first (its
 * textures stage appends BC7 and ASTC once this exists; card A54).
 *
 * A block-compressed texture also differs in what it is: its levels can't
 * be rendered or generated on the device (no API renders into a block
 * format), so every level a sampler reaches is the caller's data, a whole
 * chain at once (a KTX2 file holds one), not a level 0 and a generation.
 *
 * ---------------------------------------------------------------------------
 * What changes, in one paragraph
 * ---------------------------------------------------------------------------
 *
 * rbtrend_texture2d_create() takes a BC or ASTC format on a device created
 * with its Carrottus feature, for a texture that is only sampled, whose
 * level 0 is whole blocks; Rend refuses the rest itself, with its own
 * message. rbtrend_texture2d_update() and rbtrend_texture2d_get_data()
 * take a block format: the region in texels, aligned to the block or
 * reaching the level's edge, the data in rows of blocks. A new call,
 * rbtrend_texture2d_update_levels(), writes whole levels of a range of
 * layers in one call, one staging and one recorded batch: what a cooked
 * texture's chain, a cube's faces or an array's layers are uploaded with,
 * in any format. Rend works out every row pitch and offset from Carrottus
 * GPU's block (crgpu_format_get_texel_block()) and its copy layout
 * (crgpu_image_get_buffer_copy_size()); the caller hands tightly packed
 * blocks.
 *
 * ---------------------------------------------------------------------------
 * What was compared
 * ---------------------------------------------------------------------------
 *
 *  - Hazel (read in .refs tonight): no block format at all. Its
 *    ImageFormat (Renderer/Image.h) lists RED8UN to RGBA32F, SRGB, SRGBA
 *    and three depth formats; TextureImporter decodes every file with
 *    stb_image to RGBA8 or RGBA32F; a texture's mips are generated on the
 *    device (TextureSpecification::GenerateMips, true by default).
 *    Image2D::SetData writes level 0 alone with NVRHI's writeTexture.
 *    What Hazel has is below it, in NVRHI: nvrhi::FormatInfo's
 *    bytesPerBlock and a square blockSize (BC1 to BC7, no ASTC; a 5x4
 *    block can't be said in it), and writeTexture(texture, arraySlice,
 *    mipLevel, data, rowPitch, depthPitch), one subresource a call, whose
 *    Vulkan side (vulkan-texture.cpp, 485) counts a level's columns and
 *    rows in blocks. So Hazel's bar here is "can carry BC through NVRHI";
 *    Rend's is past it (ASTC, whole chains, Rend's own pitch).
 *  - Engines: Unity's Texture2D.LoadRawTextureData takes a whole chain
 *    of a compressed format in one buffer, levels back to back; Unreal's
 *    RHIUpdateTexture2D a region of one level (FUpdateTextureRegion2D, in
 *    texels aligned to GPixelFormats' BlockSizeX and BlockSizeY), and its
 *    streaming one level at a time; bgfx's createTexture2D one memory
 *    block holding every level, laid out by bimg's ImageBlockInfo;
 *    Filament's Texture::setImage one level a call, a compressed
 *    PixelBufferDescriptor carrying the level's byte count (pass 5:
 *    Godot's was cited from memory and taken out).
 *    Two shapes, then: a region of a level, and a whole chain. Rend
 *    has the first; the second is what a cooked texture is.
 *  - Lower APIs: WebGPU's writeTexture and wgpu's Queue::write_texture
 *    (a region of a subresource, bytesPerRow a row of blocks,
 *    rowsPerImage rows of blocks); SDL_GPU's SDL_UploadToGPUTexture
 *    (pixels_per_row and rows_per_layer in texels, as Vulkan). Rend's
 *    region keeps texels, as its existing calls and Vulkan do, and its
 *    data tight, as WebGPU's writeTexture allows; the padding a backend
 *    needs is Rend's to add in staging, as today.
 *
 * ---------------------------------------------------------------------------
 * What Rend checks, and what it only reads
 * ---------------------------------------------------------------------------
 *
 * Rend reads the device's features (rbtrend_instance_t::features, as every
 * feature check in Rend; rabbittus/CLAUDE.md, "Rend") and never asks
 * Carrottus for one; the caller created the device with them. A format's
 * block comes from crgpu_format_get_texel_block(), a level's row pitch
 * from crgpu_image_get_buffer_copy_size(), and the copy's buffer offset
 * alignment from crgpu_image_get_buffer_copy_offset_alignment(): Rend
 * keeps no table of formats of its own. What it checks, each with its own
 * message, before anything is staged: the feature, the usage, the size in
 * blocks, the region's alignment, each level's data and size. Carrottus
 * GPU checks the same again (S48's draft) and Rend's checks come first so
 * the message is Rend's, naming its arguments.
 *
 * ---------------------------------------------------------------------------
 * The version and the breaks
 * ---------------------------------------------------------------------------
 *
 *  - Added: RBTREND_TEXTURE_LEVEL_DATA (a frozen struct) and
 *    rbtrend_texture2d_update_levels().
 *  - rbtrend_texture2d_create(): refuses with Rend's own result and message
 *    what Carrottus GPU would refuse after S48 (a block format without its
 *    feature, a usage but SHADER_RESOURCE, a size not whole blocks). Before
 *    S48 Carrottus took a BC texture on Vulkan and D3D12; no caller of Rend
 *    makes one (searched: no caller in the workspace names a BC
 *    format).
 *  - rbtrend_texture2d_update() and rbtrend_texture2d_get_data(): accept
 *    what they refused (a block format). A widening; a caller's code that
 *    relied on RBT_RESULT_UNSUPPORTED for one (none does) gets a copy.
 *  - rbtrend_texture2d_resize(): refuses a size not whole blocks for a
 *    block-format texture.
 *  - RABBITTUS_VERSION_MINOR up by one when built, unless the building
 *    session finds it already raised and untagged since (card X2; L66's
 *    Rend anchor may have raised it to 0.4 first). ./check.sh --update-abi
 *    lists the addition; rabbittustests' make_destroyed_tests.py is rerun.
 *
 * New handles: none. New struct types: none (the level struct is frozen,
 * an array element, with no base). Stored formats: none (Rend reads no
 * file; a KTX2 is the caller's to read).
 *
 * Thread: the new call keeps rbtrendimages.h's rule for a texture's
 * update: one caller at a time with everything else of the instance, not
 * overlapping a compile or submission of a list that uses the texture.
 *
 * Allocation: rbtrend_texture2d_update_levels() stages in the frame's
 * transient memory where the instance has room for every level at once,
 * and in one buffer of the call's own where it hasn't (as
 * rbtrend_texture2d_update() today): the buffer is the steady state's
 * only allocation, at a texture's upload, which a loading screen or a
 * stream does, not a frame. Its size is the levels' bytes plus each row's
 * padding to the backend's pitch (D3D12's 256 bytes) and each copy's
 * offset alignment (D3D12's 512): measured in Release when built, against
 * the chain's own bytes.
 *
 * ---------------------------------------------------------------------------
 * Who else uses it
 * ---------------------------------------------------------------------------
 *
 *  - A game engine's renderer (the first; card A54): its textures stage
 *    uploads RGBA8 level by level today; once this is approved it uploads
 *    a texture's chain with one rbtrend_texture2d_update_levels() and
 *    appends BC7 and ASTC 4x4, which the caller's cooked packs carry.
 *  - A CAD viewport, when a model carries an image (a site photograph,
 *    a material's texture): the same calls, its own formats.
 *  - Rend itself: a caller's prefiltered or HDR environment picture in a
 *    block format (BC6H) can be uploaded for RBTREND_COMMAND_LIT_ENVIRONMENT,
 *    which samples it as any texture; nothing in that command changes.
 *  - Other CAD callers: no textures yet; when they have them, the same.
 *
 * Video: not touched. Block formats aren't video formats; no queue,
 * sampler, colour space or layout frozen at 1.0 changes; a YUV texture,
 * when one comes, is a create of its own (card A28).
 *
 * ---------------------------------------------------------------------------
 * Questions (numbered; each with options and a recommendation)
 * ---------------------------------------------------------------------------
 *
 * Q1. A CALL FOR WHOLE LEVELS.
 *     (a) RECOMMENDED. rbtrend_texture2d_update_levels(): whole levels
 *         of a range of layers, one call, one staging allocation, one
 *         recorded batch with one layout transition for the range. A
 *         2048 x 2048 chain is 12 levels; through
 *         rbtrend_texture2d_update() that is 12 calls each staging and
 *         transitioning on its own, 72 for a cube. CONVENTIONS.md: "one
 *         call takes many items". Any format, uncompressed ones included
 *         (a cube's six faces of RGBA8 at once).
 *     (b) Only widen update() and get_data() to block formats; a caller
 *         loops over levels and layers.
 *
 * Q2. THE LEVELS' DATA.
 *     (a) RECOMMENDED. One RBTREND_TEXTURE_LEVEL_DATA per level, each its
 *         own pointer and size, holding the call's layers one after
 *         another. A KTX2 file names each level's offset and length in its
 *         level index (largest level first in the index, smallest first in
 *         the file), so the caller points into the file's bytes and copies
 *         nothing to concatenate them.
 *     (b) One pointer, every level back to back from level `base_mip`
 *         down: Unity's and bgfx's shape, one argument fewer, and a copy
 *         for every caller whose levels aren't already in that order (a
 *         KTX2 file's aren't).
 *
 * Q3. WHERE THE FEATURE IS CHECKED.
 *     (a) RECOMMENDED. rbtrend_texture2d_create() checks the device's
 *         feature for the format first and refuses with
 *         RBT_RESULT_UNSUPPORTED and a message naming the feature and the
 *         format ("format 99 needs
 *         CRGPU_FEATURE_TEXTURE_COMPRESSION_BC, which the instance's device
 *         was created without", the format given by its number, as
 *         Rend's messages give formats), as Rend refuses a cube array view without
 *         CRGPU_FEATURE_CUBE_ARRAY_VIEWS (rabbittus/CLAUDE.md: "A feature
 *         one call needs is named in that call's header comment and
 *         checked by the call, which refuses in Rend's way").
 *     (b) Pass Carrottus GPU's CR_RESULT_GPU_FEATURE_NOT_SUPPORTED on
 *         through rbtrend_fail_cr: one check fewer in Rend, a message that
 *         names the Carrottus call and not the caller's argument.
 *
 * Q4. READBACK OF A BLOCK FORMAT.
 *     (a) RECOMMENDED. rbtrend_texture2d_get_data() reads blocks back as
 *         update writes them. A test needs it (the bytes that went in come
 *         out), and a caller's tool (a texture viewer, an editor's
 *         thumbnail decoded on the CPU) can use it. It costs nothing
 *         beyond update's arithmetic.
 *     (b) Refuse it: an upload path only.
 *
 * Q5. THE REGION OF A PARTIAL UPDATE.
 *     (a) RECOMMENDED. In texels, as the call takes today: x_offset and
 *         y_offset multiples of the block's width and height, width and
 *         height multiples of them or reaching the level's right and
 *         bottom edges, so a level of 2 x 2 texels in a 4 x 4 block is
 *         written with width and height 2, its one block of data. The
 *         data is ceil(height / block height) rows of
 *         ceil(width / block width) blocks, tight. Vulkan's rule, and
 *         Carrottus's after S48.
 *     (b) In blocks: a second set of meanings for the same parameters,
 *         by format, which no other call of Rend has.
 *
 * ---------------------------------------------------------------------------
 * Tests (rabbittustests, tests/rbtrend_gpu_tests.c; under Metal's API and
 * shader validation, warnings fatal, on the Mac; D3D12 and Vulkan in
 * their own sessions)
 * ---------------------------------------------------------------------------
 *
 *  - test_block_texture_upload: for each block format the device has (BC1
 *    to BC7, UNORM, SNORM, UF16, SF16 and sRGB where each has them; the 28
 *    ASTC LDR), a 2D texture with a full chain, a 3-layer array and a
 *    cube, written whole with rbtrend_texture2d_update_levels() from known
 *    bytes (each block's a function of its level, layer and index) and
 *    read back level by level with rbtrend_texture2d_get_data(): equal,
 *    byte for byte. Then one level rewritten by rbtrend_texture2d_update()
 *    in four block-aligned regions, and a 20 x 8 texture in ASTC 5x4 (level
 *    0 is 4 x 2 blocks; level 2, 5 x 2 texels, one block, written with a
 *    height of 2 that reaches the bottom edge), each read back whole:
 *    equal. update_levels of a sub-range (levels from 1, layers 1 and 2 of
 *    3) leaves level 0 and layer 0 as they were (pass 6). On D3D12 (the
 *    Windows session) a level of 4 BC7 blocks a row, 64 bytes, is staged at
 *    the 256-byte pitch and read back equal.
 *  - test_block_texture_sampling: what the GPU decodes is what was
 *    copied. Per format family, blocks that decode to known colours
 *    exactly on every GPU (a constant block of each channel 0 or 255:
 *    BC1 with both endpoints 0x0000 or both 0xFFFF and every index 0 (equal
 *    endpoints are BC1's three-colour mode, whose index 3 is transparent
 *    black: not used), BC7 mode 6 with every
 *    endpoint 0 or 127 and its p-bit, BC4 and BC5 endpoints 0 and 255,
 *    an ASTC LDR void-extent block of 0x0000 or 0xFFFF per channel), a
 *    different colour per level, drawn into an RGBA8 target through a
 *    nearest sampler at each level's LOD and read back: each pixel exact.
 *    sRGB forms of the same give the same 0 and 255. Mid values are left
 *    to the byte test above, since BC1's and BC7's expansion to 8 bits may
 *    round differently between GPUs.
 *  - test_block_texture_contract: each refusal once, with its message and
 *    nothing recorded: a BC and an ASTC texture on an instance over a
 *    device created without each bit (a second device the test makes on
 *    the same adapter); RENDER_TARGET, DEPTH_STENCIL and STORAGE usages;
 *    a size not whole blocks; resize to one; generate_mips on a block
 *    texture (RBT_RESULT_UNSUPPORTED, its list); a region's offset, width
 *    and height off the block; update_levels with a level past the chain,
 *    a layer range past the texture, a level's data NULL or of the wrong
 *    size, a level count of 0, a destroyed texture (refused in Debug).
 *  - test_frame_allocations: rbtrend_texture2d_update_levels() of a small
 *    chain that fits the transient memory, in a frame: none of Rend's.
 *  - make_destroyed_tests.py regenerated: the new call with a destroyed
 *    texture.
 *  - Existing tests unchanged: test_texture_region_contract and every
 *    uncompressed update keep their messages and bytes, but the one
 *    message that named block formats ("a block-compressed format has no
 *    update or readback path"), which a format with no texel block now
 *    gives as "format %d has no texel block".
 *  - Measured, Release, the Mac: a 2048 x 2048 chain uploaded as RGBA8
 *    (22.4 MB) through 12 calls of rbtrend_texture2d_update() against one
 *    call of rbtrend_texture2d_update_levels(), and as BC7 (5.6 MB) and
 *    ASTC 4x4 (5.6 MB) through the new call: the call's CPU time, the
 *    batch's GPU time (timestamps), the staging's bytes.
 *
 * ---------------------------------------------------------------------------
 * rbtrendimages.h
 * ---------------------------------------------------------------------------
 */

/*
 * [CHANGED] rbtrend_texture2d_create(): RBTREND_TEXTURE2D_DESC::format's
 * comment gains "A block-compressed format (BC1 to BC7, ASTC; a block
 * larger than one texel, crgpu_format_get_texel_block()) needs its feature
 * on the instance's device (CRGPU_FEATURE_TEXTURE_COMPRESSION_BC or
 * _ASTC_LDR), RBTREND_TEXTURE_USAGE_BIT_SHADER_RESOURCE as its only usage,
 * #width and #height whole blocks, #sample_count 1 and #cube_capable as
 * any 2D texture. Every level a sampler reaches is the caller's to write
 * (rbtrend_texture2d_update_levels()): none can be generated." The call's
 * results gain, after the format's check and before the sample count's:
 *
 *   RBT_RESULT_UNSUPPORTED if `format` is block-compressed and the
 *   instance's device was created without its feature (Q3 a);
 *   RBT_RESULT_INVALID_ARGUMENT if `format` is block-compressed and
 *   `usage` is anything but SHADER_RESOURCE, or `width` or `height`
 *   isn't whole blocks.
 *
 * Why: Rend's message names the caller's field; Carrottus GPU would
 * refuse the same after S48 with a result that names its own call. ABI:
 * refusals of what reached Carrottus GPU (above). Tests:
 * test_block_texture_contract.
 *
 * [CHANGED] rbtrend_texture2d_resize(): its results gain
 * RBT_RESULT_INVALID_ARGUMENT for a block-format texture's new size that
 * isn't whole blocks. Why and tests as above.
 *
 * [CHANGED] rbtrend_texture2d_update(): its comment's @p data and
 * @p data_size, and its results:
 *
 *   @param[in] x_offset Left edge of the region, in texels of the level: a
 *                       multiple of the format's block width (1 for an
 *                       uncompressed format).
 *   @param[in] y_offset Top edge, a multiple of the block's height.
 *   @param[in] width    Width of the region. Never 0. A multiple of the
 *                       block's width, or reaching the level's right edge
 *                       (x_offset + width is the level's width).
 *   @param[in] height   Height of the region. Never 0. A multiple of the
 *                       block's height, or reaching the level's bottom
 *                       edge.
 *   @param[in] data     The region's blocks: ceil(height / block height)
 *                       rows, from the top, of ceil(width / block width)
 *                       blocks, from the left, tightly packed, in the
 *                       texture's format (for an uncompressed format, a
 *                       block is a texel, so: height rows of width
 *                       texels, as before). Never NULL.
 *   @param[in] data_size The bytes at @p data: exactly the region's,
 *                       its blocks times the block's bytes.
 *
 *   @return RBT_RESULT_OK if the copy was recorded;
 *           RBT_RESULT_INVALID_ARGUMENT as before, and for a region's
 *           offset, width or height off the block (Q5 a);
 *           RBT_RESULT_UNSUPPORTED if the texture's format has no texel
 *           block (crgpu_format_get_texel_block(): R1_UNORM and the video
 *           formats), where it was "no whole number of bytes to a texel";
 *           RBT_RESULT_OUT_OF_MEMORY; or the code for Carrottus GPU's
 *           result if the copy couldn't be staged or recorded.
 *
 * Why: the region shape the call has, given the block's rules. ABI: a
 * widening (above). Tests: test_block_texture_upload,
 * test_block_texture_contract.
 *
 * [CHANGED] rbtrend_texture2d_get_data(): the same for @p out_data and
 * @p data_size (Q4 a): the region's blocks written as update reads them.
 *
 * [CHANGED] rbtrend_texture2d_generate_mips(): its comment gains "A
 * block-compressed texture is refused (RBT_RESULT_UNSUPPORTED): no API
 * renders into one. Its levels are each written, by
 * rbtrend_texture2d_update_levels() or rbtrend_texture2d_update()." It
 * already refuses every format outside its list; the sentence says why for
 * block formats.
 *
 * [CHANGED] rbtrend_texture3d_create(): its comment gains "A block-
 * compressed format is refused (RBT_RESULT_UNSUPPORTED): Carrottus GPU
 * makes block formats 2D only." (S48's Q7.) Checked by Rend before
 * Carrottus GPU, with that message.
 */

/**
 * @brief [ADDED] One level's data for rbtrend_texture2d_update_levels().
 *
 * Why: Q2 a. Each level of a cooked file (a KTX2's level index) is at its
 * own offset, so each is a pointer and a size, and no caller concatenates
 * levels to call. ABI: a struct added. Tests: test_block_texture_upload.
 */
/* Frozen: an array element passed to rbtrend_texture2d_update_levels(). It never changes; a new need gets a new type. */
typedef struct RBTREND_TEXTURE_LEVEL_DATA {
	/**
	 * The level's blocks, for every layer the call writes, first layer
	 * first: each layer is the whole level, ceil(level height / block
	 * height) rows of ceil(level width / block width) blocks, top row
	 * first, tightly packed, in the texture's format; the next layer
	 * follows at once. A cube-capable texture's faces are its layers, +X,
	 * -X, +Y, -Y, +Z, -Z in each six: a KTX2 file's level holds its layers
	 * and faces in that order, so its bytes are passed as they are (pass
	 * 4). Read during the call only. Never NULL.
	 */
	const void *data;
	/** The bytes at #data: exactly the level's bytes times the call's
	 * layer count. */
	uint64_t data_size;
} RBTREND_TEXTURE_LEVEL_DATA;

/**
 * @brief [ADDED] Writes whole mip levels of a range of a 2D texture's
 * layers, in any format the texture can have: the way a texture's
 * prepared chain is put on the device (a cooked file's levels, a cube's
 * six faces, an array's layers).
 *
 * Level `base_mip + i` of each layer from @p base_layer to
 * `base_layer + layer_count - 1` is written from `levels[i]`. Recorded into
 * the frame's batch of updates and clears, and not waited for, as
 * rbtrend_texture2d_update() is (rbtrendcontext.h, "Texture updates and
 * clears"): every level is staged at once, in the frame's transient memory
 * where the instance has room for all of it, and in one buffer of the
 * call's own where it hasn't, each row padded to the backend's row pitch
 * (crgpu_image_get_buffer_copy_size()) by Rend; then one copy per level
 * and layer is recorded, between one transition of the whole range into
 * CRGPU_IMAGE_LAYOUT_TRANSFER_DST and one back. Each level is left in the
 * image layout it was found in, as rbtrend_texture2d_update() leaves it;
 * a level nothing had written ends in
 * CRGPU_IMAGE_LAYOUT_SHADER_READ_ONLY, and a list compiled against it
 * before the call is recompiled before it is submitted.
 *
 * A block-compressed texture's levels are only ever written this way or
 * by rbtrend_texture2d_update(), never generated; each level a sampler
 * reaches must be written before it is sampled, as every level of every
 * texture must.
 *
 * Every argument is checked before anything is staged, each its own check
 * and message in parameter order (the levels in index order), after the
 * texture's null and destroyed checks and Rend's overlap check on its
 * instance (RBTREND_ENTER), as rbtrend_texture2d_update(); a refused call
 * records and stages nothing. The staging's size, each level's rows padded
 * to the pitch and each copy's offset aligned, is worked out with overflow
 * checked; no texture the device can make reaches the bound (its levels
 * are under 2^40 bytes), so the check is a guard no input can trip.
 *
 * Why: Q1 a. A texture prepared off the device arrives as whole levels;
 * this uploads them in one call, one staging and one transition, where
 * rbtrend_texture2d_update() would take one each per level and layer.
 * Unity's LoadRawTextureData and bgfx's createTexture2D take a whole
 * chain the same way. ABI: a call added. Tests: test_block_texture_upload,
 * test_block_texture_contract, test_frame_allocations.
 *
 * Thread: as rbtrend_texture2d_update() (rbtrendimages.h's opening).
 *
 * @param[in] texture     Texture to write. Never NULL.
 * @param[in] queue       Queue the copies are submitted on: the instance's
 *                        one queue, as for rbtrend_texture2d_update(). Never
 *                        NULL.
 * @param[in] base_mip    The first level written, 0 the largest. Below the
 *                        texture's mip count.
 * @param[in] base_layer  The first layer written. Below the texture's
 *                        layer count.
 * @param[in] layer_count How many layers, from @p base_layer: at least 1,
 *                        and not past the texture's last layer.
 * @param[in] levels      @p level_count levels' data, the first for
 *                        @p base_mip. Read during the call only. Never
 *                        NULL.
 * @param[in] level_count How many levels: at least 1, and not past the
 *                        texture's last level.
 * @param[out] out_error  Why it failed, when it did. May be NULL.
 *
 * @return RBT_RESULT_OK if every copy was recorded;
 *         RBT_RESULT_INVALID_ARGUMENT if @p texture, @p queue or @p levels
 *         is NULL, @p queue is not the instance's, @p base_mip or
 *         @p base_layer doesn't exist, @p layer_count or @p level_count is
 *         0 or runs past the texture, a level's `data` is NULL, or a
 *         level's `data_size` isn't its bytes times @p layer_count (the
 *         message names the level and both sizes); RBT_RESULT_UNSUPPORTED
 *         if the texture's format has no texel block;
 *         RBT_RESULT_OUT_OF_MEMORY if no staging can be had; or the code for Carrottus GPU's result if a
 *         copy couldn't be staged or recorded. Checked in that order, the
 *         levels in index order.
 */
RBT_RESULT rbtrend_texture2d_update_levels(
	rbtrend_texture2d texture,
	crgpu_command_queue queue,
	uint32_t base_mip,
	uint32_t base_layer,
	uint32_t layer_count,
	const RBTREND_TEXTURE_LEVEL_DATA *levels,
	uint32_t level_count,
	RBTREND_ERROR *out_error);

/*
 * ---------------------------------------------------------------------------
 * Not in a header: what changes in src/render
 * ---------------------------------------------------------------------------
 *
 * - rbtrendimages.cpp: rbtrend_texel_bytes() gives way to the block
 *   (crgpu_format_get_texel_block()); the region check takes the block's
 *   alignment; the staging re-packs rows of blocks to the backend's pitch;
 *   `buffer_row_length` is `row_pitch / block bytes * block width` texels
 *   (S48's rule); the readback the same the other way.
 * - rbtrendimages.cpp: rbtrend_texture2d_update_levels() over the same
 *   staging (rbtrend_upload_staging_begin, sized for every level at once)
 *   and one rbtrend_immediate_on_texture() over the range.
 * - rbtrendimages.cpp: RBT_PLAIN_DATA(RBTREND_TEXTURE_LEVEL_DATA) beside
 *   the call, the array it walks (check.sh plain-data's list).
 * - rbtrendimages_internal.hpp: rbtrend_format_scalar_type() names every
 *   BC and ASTC format a float format (they are sampled as floats) before
 *   its range test, which takes everything from CRGPU_FORMAT_AYUV up for
 *   a video format and would otherwise take ASTC's 1000 and on for one
 *   (S48's pass 2).
  *
 * ---------------------------------------------------------------------------
 * Records when built (pass 7)
 * ---------------------------------------------------------------------------
 *
 * RBTREND_TODO.md: an item for A54, its runs and its measurements.
 * rabbittus/CLAUDE.md, "Rend": a line on block-format textures (the
 * feature from the device, levels written never generated, the whole-level
 * call). rabbittustests' README: the three new tests. The workspace's
 * DECISIONS.md card A54 moves to "Decided".
 */
