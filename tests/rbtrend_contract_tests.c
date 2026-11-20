// Headless tests of Rend's contract (rbtrendcontext.h, "Results and
// errors"): what each converted call refuses before it touches a device,
// with its code, its message, and its handle written NULL. Calls are added
// here as their headers are converted (RBTREND_TODO.md, "The contract
// conversion"); what needs a device is tested in a caller's GPU tests.
//
// Written in C, to prove the headers compile and link as C.

#include "rbtrend.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static const char *current_test = "";

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("FAIL %s (%s:%d): %s\n", current_test, __FILE__, __LINE__, #condition);                           \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

// A device the checks never reach: each refusal below comes before Rend
// calls Carrottus GPU, so the handle is only compared with NULL.
#define UNREACHED_DEVICE ((crgpu_device)(uintptr_t)0x10)

// An error struct holding text, so a call that leaves it alone is seen.
static void dirty(RBTREND_ERROR *error)
{
    memset(error->message, 'x', sizeof(error->message) - 1);
    error->message[sizeof(error->message) - 1] = '\0';
}

static RBTREND_INSTANCE_DESC valid_instance_desc(void)
{
    RBTREND_INSTANCE_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.base.type = RBT_STRUCT_TYPE_REND_INSTANCE_DESC;
    desc.device = UNREACHED_DEVICE;
    desc.frames_in_flight = 2;
    return desc;
}

// [desc] refused with [result] and [message], the instance NULL.
static void instance_create_refused(const RBTREND_INSTANCE_DESC *desc, RBT_RESULT result, const char *message)
{
    RBTREND_ERROR error;
    dirty(&error);
    rbtrend_instance instance = (rbtrend_instance)(uintptr_t)0x20;
    CHECK(rbtrend_instance_create(desc, &instance, &error) == result);
    CHECK(instance == NULL);
    CHECK(strcmp(error.message, message) == 0);
    if (strcmp(error.message, message) != 0)
        printf("  got \"%s\"\n", error.message);
    // The same without an error struct: the code alone.
    instance = (rbtrend_instance)(uintptr_t)0x20;
    CHECK(rbtrend_instance_create(desc, &instance, NULL) == result);
    CHECK(instance == NULL);
}

static void test_instance_create(void)
{
    current_test = "instance_create";
    instance_create_refused(NULL, RBT_RESULT_INVALID_ARGUMENT, "desc is NULL");

    RBTREND_INSTANCE_DESC desc = valid_instance_desc();
    desc.base.type = RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC;
    instance_create_refused(&desc, RBT_RESULT_STRUCT_WRONG_TYPE, "desc has a base.type other than its own");

    RBT_STRUCT_BASE chained;
    chained.type = RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC;
    chained.next = NULL;
    desc = valid_instance_desc();
    desc.base.next = &chained;
    instance_create_refused(&desc, RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN, "desc has a struct chained on, and nothing can be chained onto it in this version");

    desc = valid_instance_desc();
    desc.device = NULL;
    instance_create_refused(&desc, RBT_RESULT_INVALID_ARGUMENT, "device is NULL");

    desc = valid_instance_desc();
    desc.frames_in_flight = 0;
    instance_create_refused(&desc, RBT_RESULT_INVALID_ARGUMENT, "frames_in_flight is 0");

    desc = valid_instance_desc();
    desc.frames_in_flight = RBTREND_MAX_FRAMES_IN_FLIGHT + 1;
    instance_create_refused(&desc, RBT_RESULT_INVALID_ARGUMENT, "frames_in_flight 5 is above RBTREND_MAX_FRAMES_IN_FLIGHT (4)");

    desc = valid_instance_desc();
    desc.transient_memory_limit = 1024;
    instance_create_refused(&desc, RBT_RESULT_INVALID_ARGUMENT, "transient_memory_limit is set and transient_chunk_size is 0");

    desc = valid_instance_desc();
    desc.transient_chunk_size = 2048;
    desc.transient_memory_limit = 1024;
    instance_create_refused(&desc, RBT_RESULT_INVALID_ARGUMENT, "transient_memory_limit 1024 is below transient_chunk_size 2048");

    // Last in parameter order: every other argument is right.
    desc = valid_instance_desc();
    RBTREND_ERROR error;
    dirty(&error);
    CHECK(rbtrend_instance_create(&desc, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "out_instance is NULL") == 0);
}

// The calls that take an instance refuse a NULL one, and write what they
// promise to on failure.
static void test_null_instance(void)
{
    current_test = "null_instance";
    RBTREND_ERROR error;

    dirty(&error);
    CHECK(rbtrend_instance_wait_idle(NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(rbtrend_instance_wait_idle(NULL, NULL) == RBT_RESULT_INVALID_ARGUMENT);

    RBTREND_INSTANCE_DESC desc;
    CHECK(rbtrend_instance_get_desc(NULL, &desc) == RBT_RESULT_INVALID_ARGUMENT);

    RBTREND_RENDER_TARGET_POOL_INFO info;
    memset(&info, 0, sizeof(info));
    info.base.type = RBT_STRUCT_TYPE_REND_RENDER_TARGET_POOL_INFO;
    CHECK(rbtrend_instance_get_render_target_pool(NULL, &info) == RBT_RESULT_INVALID_ARGUMENT);

    uint32_t trimmed = 7;
    CHECK(rbtrend_instance_trim_render_targets(NULL, 0, &trimmed) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(trimmed == 0);
    CHECK(rbtrend_instance_trim_render_targets(NULL, 0, NULL) == RBT_RESULT_INVALID_ARGUMENT);

    dirty(&error);
    CHECK(rbtrend_instance_set_render_target_retirement(NULL, 1, 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);

    uint32_t index = 7;
    dirty(&error);
    CHECK(rbtrend_instance_next_frame(NULL, UINT64_MAX, &index, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(index == 7);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(rbtrend_instance_get_frame_index(NULL) == 0);
    // The submissions' serials (card A62): 0 for no instance.
    CHECK(rbtrend_instance_get_last_submission(NULL) == 0);
    CHECK(rbtrend_instance_get_completed_submission(NULL) == 0);
    rbtrend_instance_destroy(NULL);
}

// Every image call refuses a NULL handle with its code (and its message,
// where it takes an error struct), and writes NULL to a handle it was to
// make; the getters write nothing.
static void test_images_null(void)
{
    current_test = "images_null";
    RBTREND_ERROR error;
    uint8_t texels[4] = {0};

    RBTREND_TEXTURE2D_DESC desc2d;
    memset(&desc2d, 0, sizeof(desc2d));
    desc2d.base.type = RBT_STRUCT_TYPE_REND_TEXTURE2D_DESC;
    rbtrend_texture2d texture2d = (rbtrend_texture2d)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_texture2d_create(NULL, &desc2d, &texture2d, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(texture2d == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(rbtrend_texture2d_get_desc(NULL, &desc2d) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture2d_update(NULL, NULL, 0, 0, 0, 0, 1, 1, texels, 4, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "texture is NULL") == 0);
    CHECK(rbtrend_texture2d_get_data(NULL, NULL, 0, 0, 0, 0, 1, 1, texels, 4, &error) == RBT_RESULT_INVALID_ARGUMENT);
    // The whole-level call (card A54): the texture is checked first, before
    // every other argument, NULL or not.
    RBTREND_TEXTURE_LEVEL_DATA level = {texels, 4};
    dirty(&error);
    CHECK(rbtrend_texture2d_update_levels(NULL, NULL, 0, 0, 1, &level, 1, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "texture is NULL") == 0);
    CHECK(rbtrend_texture2d_update_levels(NULL, NULL, 0, 0, 0, NULL, 0, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture2d_generate_mips(NULL, NULL, 0, CRGPU_FILTER_LINEAR, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture2d_resize(NULL, 1, 1, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture2d_get_crgpu_image(NULL) == NULL);
    rbtrend_texture2d_destroy(NULL);

    RBTREND_TEXTURE3D_DESC desc3d;
    memset(&desc3d, 0, sizeof(desc3d));
    desc3d.base.type = RBT_STRUCT_TYPE_REND_TEXTURE3D_DESC;
    rbtrend_texture3d texture3d = (rbtrend_texture3d)(uintptr_t)0x20;
    CHECK(rbtrend_texture3d_create(NULL, &desc3d, &texture3d, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(texture3d == NULL);
    CHECK(rbtrend_texture3d_get_desc(NULL, &desc3d) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture3d_update(NULL, NULL, 0, 0, 0, 0, 1, 1, 1, texels, 4, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture3d_get_data(NULL, NULL, 0, 0, 0, 0, 1, 1, 1, texels, 4, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture3d_resize(NULL, 1, 1, 1, &error) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_texture3d_destroy(NULL);

    RBTREND_TEXTURE_VIEW_DESC view_desc;
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.base.type = RBT_STRUCT_TYPE_REND_TEXTURE_VIEW_DESC;
    rbtrend_texture_view view = (rbtrend_texture_view)(uintptr_t)0x20;
    CHECK(rbtrend_texture_view_create_2d(NULL, &view_desc, &view, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(view == NULL);
    view = (rbtrend_texture_view)(uintptr_t)0x20;
    CHECK(rbtrend_texture_view_create_3d(NULL, &view_desc, &view, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(view == NULL);
    CHECK(rbtrend_texture_view_get_desc(NULL, &view_desc) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_texture_view_get_type(NULL) == RBTREND_TEXTURE_VIEW_TYPE_NONE);
    CHECK(rbtrend_texture_view_get_texture2d(NULL) == NULL);
    rbtrend_texture_view_destroy(NULL);

    RBTREND_SAMPLER_DESC sampler_desc;
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.base.type = RBT_STRUCT_TYPE_REND_SAMPLER_DESC;
    rbtrend_sampler sampler = (rbtrend_sampler)(uintptr_t)0x20;
    CHECK(rbtrend_sampler_create(NULL, &sampler_desc, &sampler, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(sampler == NULL);
    CHECK(rbtrend_sampler_get_desc(NULL, &sampler_desc) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_sampler_destroy(NULL);
}

// The shader and pipeline calls refuse a NULL handle or desc with their
// code and message, and write NULL to the handle they were to make; the
// lookups refuse a NULL layout.
static void test_pipelines_null(void)
{
    current_test = "pipelines_null";
    RBTREND_ERROR error;
    const uint32_t words[4] = {0};

    // rbtrend_shader_create's first refusal, the only one before a device:
    // the rest, in the header's order, are ../rabbittustests'
    // test_shader_create_refusals.
    RBTREND_SHADER_PACKAGE package;
    memset(&package, 0, sizeof(package));
    package.base.type = RBT_STRUCT_TYPE_REND_SHADER_PACKAGE;
    package.stage = CRGPU_SHADER_STAGE_VERTEX;
    package.entry_point = "main_vert";
    package.spirv = words;
    package.spirv_size = sizeof(words);
    rbtrend_shader shader = (rbtrend_shader)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_shader_create(NULL, &package, &shader, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(shader == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    shader = (rbtrend_shader)(uintptr_t)0x20;
    CHECK(rbtrend_shader_create(NULL, NULL, &shader, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(shader == NULL);
    shader = (rbtrend_shader)(uintptr_t)0x20;
    CHECK(rbtrend_instance_get_fullscreen_vertex_shader(NULL, &shader, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(shader == NULL);
    CHECK(rbtrend_shader_get_reflection(NULL) == NULL);
    CHECK(rbtrend_shader_find_resource(NULL, "x") == NULL);
    rbtrend_shader_destroy(NULL);

    RBTREND_BINDING_DESC binding;
    memset(&binding, 0, sizeof(binding));
    rbtrend_binding_layout layout = (rbtrend_binding_layout)(uintptr_t)0x20;
    CHECK(rbtrend_binding_layout_create(NULL, &binding, 1, &layout, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(layout == NULL);
    layout = (rbtrend_binding_layout)(uintptr_t)0x20;
    CHECK(rbtrend_binding_layout_create_from_shaders(NULL, &shader, 1, 0, &layout, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(layout == NULL);
    uint32_t number = 0;
    CHECK(rbtrend_binding_layout_find(NULL, "x", &number, &binding) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_binding_layout_get_binding(NULL, 0, &binding) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_binding_layout_get_block(NULL, 0, NULL, NULL, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_binding_layout_get_binding_count(NULL) == 0);
    rbtrend_binding_layout_destroy(NULL);

    RBTREND_GRAPHICS_PIPELINE_DESC graphics;
    memset(&graphics, 0, sizeof(graphics));
    graphics.base.type = RBT_STRUCT_TYPE_REND_GRAPHICS_PIPELINE_DESC;
    rbtrend_graphics_pipeline graphics_pipeline = (rbtrend_graphics_pipeline)(uintptr_t)0x20;
    CHECK(rbtrend_graphics_pipeline_create(NULL, &graphics, &graphics_pipeline, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(graphics_pipeline == NULL);
    CHECK(rbtrend_graphics_pipeline_get_desc(NULL, &graphics) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_graphics_pipeline_destroy(NULL);

    RBTREND_COMPUTE_PIPELINE_DESC compute;
    memset(&compute, 0, sizeof(compute));
    compute.base.type = RBT_STRUCT_TYPE_REND_COMPUTE_PIPELINE_DESC;
    rbtrend_compute_pipeline compute_pipeline = (rbtrend_compute_pipeline)(uintptr_t)0x20;
    CHECK(rbtrend_compute_pipeline_create(NULL, &compute, &compute_pipeline, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(compute_pipeline == NULL);
    CHECK(rbtrend_compute_pipeline_get_desc(NULL, &compute) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_compute_pipeline_destroy(NULL);

    RBTREND_BINDING_SET_ENTRY entry;
    memset(&entry, 0, sizeof(entry));
    rbtrend_binding_set set = (rbtrend_binding_set)(uintptr_t)0x20;
    CHECK(rbtrend_binding_set_create(NULL, &entry, 1, &set, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(set == NULL);
    CHECK(strcmp(error.message, "layout is NULL") == 0);
    CHECK(rbtrend_binding_set_update(NULL, &entry, 1, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "set is NULL") == 0);
    rbtrend_binding_set_destroy(NULL);
}

// Framebuffers, passes, timers and transient memory refuse a NULL handle
// with their code, writing NULL to a handle they were to make.
static void test_frames_and_passes_null(void)
{
    current_test = "frames_and_passes_null";
    RBTREND_ERROR error;

    RBTREND_FRAMEBUFFER_DESC framebuffer_desc;
    memset(&framebuffer_desc, 0, sizeof(framebuffer_desc));
    framebuffer_desc.base.type = RBT_STRUCT_TYPE_REND_FRAMEBUFFER_DESC;
    rbtrend_framebuffer framebuffer = (rbtrend_framebuffer)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_framebuffer_create(NULL, &framebuffer_desc, &framebuffer, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(framebuffer == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(rbtrend_framebuffer_get_desc(NULL, &framebuffer_desc) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_framebuffer_resize(NULL, 1, 1, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_framebuffer_clear_colors(NULL, NULL, 0.0f, 0.0f, 0.0f, 1.0f, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_framebuffer_clear_depth(NULL, NULL, true, false, 1.0f, 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_framebuffer_get_color_attachment(NULL, 0) == NULL);
    CHECK(rbtrend_framebuffer_get_depth_attachment(NULL) == NULL);
    rbtrend_framebuffer_destroy(NULL);

    RBTREND_RENDER_PASS_DESC render_desc;
    memset(&render_desc, 0, sizeof(render_desc));
    render_desc.base.type = RBT_STRUCT_TYPE_REND_RENDER_PASS_DESC;
    rbtrend_render_pass render_pass = (rbtrend_render_pass)(uintptr_t)0x20;
    CHECK(rbtrend_render_pass_create(NULL, &render_desc, &render_pass, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(render_pass == NULL);
    CHECK(rbtrend_render_pass_set_binding_set(NULL, 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "pass is NULL") == 0);
    uint32_t width = 7;
    CHECK(rbtrend_render_pass_get_size(NULL, &width, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(width == 7);
    CHECK(rbtrend_render_pass_get_pipeline(NULL) == NULL);
    rbtrend_render_pass_destroy(NULL);

    RBTREND_COMPUTE_PASS_DESC compute_desc;
    memset(&compute_desc, 0, sizeof(compute_desc));
    compute_desc.base.type = RBT_STRUCT_TYPE_REND_COMPUTE_PASS_DESC;
    rbtrend_compute_pass compute_pass = (rbtrend_compute_pass)(uintptr_t)0x20;
    CHECK(rbtrend_compute_pass_create(NULL, &compute_desc, &compute_pass, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(compute_pass == NULL);
    CHECK(rbtrend_compute_pass_set_binding_set(NULL, 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_compute_pass_destroy(NULL);

    RBTREND_GPU_TIMER_DESC timer_desc;
    memset(&timer_desc, 0, sizeof(timer_desc));
    timer_desc.base.type = RBT_STRUCT_TYPE_REND_GPU_TIMER_DESC;
    rbtrend_gpu_timer timer = (rbtrend_gpu_timer)(uintptr_t)0x20;
    CHECK(rbtrend_gpu_timer_create(NULL, &timer_desc, &timer, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(timer == NULL);
    double ms = -1.0;
    CHECK(rbtrend_gpu_timer_get_ms(NULL, 0, &ms, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(ms == -1.0);
    rbtrend_gpu_timer_destroy(NULL);

    RBTREND_TRANSIENT_SPAN span;
    memset(&span, 0x5a, sizeof(span));
    CHECK(rbtrend_transient_allocate(NULL, 16, 16, &span, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(span.data == NULL && span.size == 0);
    CHECK(rbtrend_transient_get_frame_usage(NULL) == 0);
}

// Materials, the lit calls and exposures refuse a NULL handle with their
// code, writing NULL to a handle they were to make.
static void test_materials_and_lit_null(void)
{
    current_test = "materials_and_lit_null";
    RBTREND_ERROR error;
    const float values[16] = {0};

    RBTREND_MATERIAL_DESC material_desc;
    memset(&material_desc, 0, sizeof(material_desc));
    material_desc.base.type = RBT_STRUCT_TYPE_REND_MATERIAL_DESC;
    rbtrend_material material = (rbtrend_material)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_material_create(NULL, &material_desc, &material, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(material == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    material = (rbtrend_material)(uintptr_t)0x20;
    CHECK(rbtrend_material_copy(NULL, &material, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(material == NULL);
    CHECK(rbtrend_material_set_uniform(NULL, "x", values, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "material is NULL") == 0);
    RBTREND_MATERIAL_UNIFORM uniform;
    memset(&uniform, 0, sizeof(uniform));
    CHECK(rbtrend_material_find_uniform(NULL, "x", &uniform, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_material_set_uniform_value(NULL, uniform, values, &error) == RBT_RESULT_INVALID_ARGUMENT);
    float out[16];
    CHECK(rbtrend_material_get_uniform_value(NULL, uniform, out, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_material_set_uniform_bytes(NULL, "x", 0, 4, values, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_material_set_uniform_buffer(NULL, "x", 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_material_set_storage_buffer(NULL, "x", 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_material_set_texture(NULL, "x", 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_material_set_sampler(NULL, "x", 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(!rbtrend_material_is_complete(NULL));
    CHECK(rbtrend_material_get_layout(NULL) == NULL);
    rbtrend_material_destroy(NULL);

    rbtrend_shader shader = (rbtrend_shader)(uintptr_t)0x20;
    CHECK(rbtrend_lit_get_shader(NULL, RBTREND_LIT_SHADER_SURFACE_VERTEX, &shader, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(shader == NULL);
    rbtrend_binding_layout layout = (rbtrend_binding_layout)(uintptr_t)0x20;
    CHECK(rbtrend_lit_get_scene_layout(NULL, &layout, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(layout == NULL);
    CHECK(rbtrend_lit_get_shadow_layout(NULL, &layout, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_lit_get_material_layout(NULL, &layout, &error) == RBT_RESULT_INVALID_ARGUMENT);
    material = (rbtrend_material)(uintptr_t)0x20;
    CHECK(rbtrend_lit_material_create(NULL, &material, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(material == NULL);
    RBTREND_LIT_MATERIAL_VALUES material_values;
    memset(&material_values, 0, sizeof(material_values));
    CHECK(rbtrend_lit_material_set_values(NULL, &material_values, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "material is NULL") == 0);
    CHECK(rbtrend_lit_material_get_values(NULL, &material_values, &error) == RBT_RESULT_INVALID_ARGUMENT);
    // A scene's cover (card L74): a NULL scene refused before the cover is read.
    RBTREND_LIT_COVER cover;
    memset(&cover, 0, sizeof(cover));
    CHECK(rbtrend_lit_scene_set_cover(NULL, &cover, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "scene is NULL") == 0);
    CHECK(rbtrend_lit_scene_set_cover(NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_lit_material_set_map(NULL, RBTREND_LIT_MAP_BASE_COLOR, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_lit_material_set_sampler(NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);

    RBTREND_LIT_ENVIRONMENT_DESC environment_desc;
    memset(&environment_desc, 0, sizeof(environment_desc));
    environment_desc.base.type = RBT_STRUCT_TYPE_REND_LIT_ENVIRONMENT_DESC;
    rbtrend_lit_environment environment = (rbtrend_lit_environment)(uintptr_t)0x20;
    CHECK(rbtrend_lit_environment_create(NULL, &environment_desc, &environment, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(environment == NULL);
    rbtrend_lit_environment_destroy(NULL);

    rbtrend_lit_scene scene = (rbtrend_lit_scene)(uintptr_t)0x20;
    RBTREND_LIT_SCENE_DESC scene_desc;
    memset(&scene_desc, 0, sizeof(scene_desc));
    scene_desc.base.type = RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC;
    CHECK(rbtrend_lit_scene_create(NULL, &scene_desc, &scene, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(scene == NULL);
    CHECK(rbtrend_lit_scene_set_lights(NULL, NULL, 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_lit_scene_set_bent_normals(NULL, true, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_lit_scene_get_material(NULL) == NULL);
    rbtrend_lit_scene_destroy(NULL);

    float matrix[16];
    CHECK(rbtrend_lit_spot_shadow_view_projection(NULL, 0.1f, false, matrix, &error) == RBT_RESULT_INVALID_ARGUMENT);

    RBTREND_EXPOSURE_DESC exposure_desc;
    memset(&exposure_desc, 0, sizeof(exposure_desc));
    exposure_desc.base.type = RBT_STRUCT_TYPE_REND_EXPOSURE_DESC;
    rbtrend_exposure exposure = (rbtrend_exposure)(uintptr_t)0x20;
    CHECK(rbtrend_exposure_create(NULL, &exposure_desc, &exposure, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(exposure == NULL);
    rbtrend_exposure_destroy(NULL);
}

// Draw lists and swap chains refuse a NULL handle with their code, writing
// NULL to a handle they were to make.
static void test_draw_lists_and_swap_chains_null(void)
{
    current_test = "draw_lists_and_swap_chains_null";
    RBTREND_ERROR error;

    RBTREND_DRAW_LIST_DESC list_desc;
    memset(&list_desc, 0, sizeof(list_desc));
    list_desc.base.type = RBT_STRUCT_TYPE_REND_DRAW_LIST_DESC;
    rbtrend_draw_list list = (rbtrend_draw_list)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_draw_list_create(NULL, &list_desc, &list, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(list == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    RBTREND_DRAW_MESH mesh;
    memset(&mesh, 0, sizeof(mesh));
    CHECK(rbtrend_draw_list_add(NULL, &mesh, NULL, 1, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "list is NULL") == 0);
    CHECK(rbtrend_draw_list_build(NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    uint32_t draws = 7;
    CHECK(rbtrend_draw_list_get_counts(NULL, &draws, NULL, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(draws == 7);
    rbtrend_draw_list_reset(NULL);
    rbtrend_draw_list_destroy(NULL);

    RBTREND_SWAP_CHAIN_DESC swap_desc;
    memset(&swap_desc, 0, sizeof(swap_desc));
    swap_desc.base.type = RBT_STRUCT_TYPE_REND_SWAP_CHAIN_DESC;
    rbtrend_swap_chain swap_chain = (rbtrend_swap_chain)(uintptr_t)0x20;
    CHECK(rbtrend_swap_chain_create(NULL, &swap_desc, &swap_chain, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(swap_chain == NULL);
    CHECK(rbtrend_swap_chain_resize(NULL, 1, 1, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_swap_chain_get_desc(NULL, &swap_desc) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_swap_chain_acquire(NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(rbtrend_swap_chain_get_back_buffer_size(NULL, &width, &height) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_swap_chain_present(NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "swap_chain is NULL") == 0);
    rbtrend_swap_chain_destroy(NULL);
}

// Command lists refuse a NULL handle with their code.
static void test_command_lists_null(void)
{
    current_test = "command_lists_null";
    RBTREND_ERROR error;
    rbtrend_command_list list = (rbtrend_command_list)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_command_list_create(NULL, &list, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(list == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(rbtrend_command_list_compile(NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "cmd_list is NULL") == 0);
    CHECK(rbtrend_command_list_submit(NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    RBTREND_COMMAND command;
    memset(&command, 0, sizeof(command));
    rbtrend_command_list_send(NULL, command);
    rbtrend_command_list_reset(NULL);
    rbtrend_command_list_destroy(NULL);
}

// rbtrendbuffers.h: every call refuses a NULL handle, writing its out_
// parameter NULL, and the uniform layout calls, which take no device, work
// on their own.
static void test_buffers(void)
{
    current_test = "buffers";
    RBTREND_ERROR error;

    RBTREND_BUFFER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.base.type = RBT_STRUCT_TYPE_REND_BUFFER_DESC;
    desc.size = 64;
    desc.usage = RBTREND_BUFFER_USAGE_VERTEX;
    desc.writes = RBTREND_BUFFER_WRITES_ONCE;
    rbtrend_buffer buffer = (rbtrend_buffer)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_buffer_create(NULL, &desc, &buffer, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(buffer == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    rbtrend_buffer_destroy(NULL);
    CHECK(rbtrend_buffer_get_desc(NULL, &desc) == RBT_RESULT_INVALID_ARGUMENT);
    const uint8_t bytes[4] = {0};
    dirty(&error);
    CHECK(rbtrend_buffer_write(NULL, 0, bytes, sizeof(bytes), &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "buffer is NULL") == 0);
    CHECK(rbtrend_buffer_write(NULL, 0, bytes, sizeof(bytes), NULL) == RBT_RESULT_INVALID_ARGUMENT);
    // A write by its bytes (card A62): the buffer first, as the plain write.
    dirty(&error);
    CHECK(rbtrend_buffer_write_after(NULL, 0, bytes, sizeof(bytes), 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "buffer is NULL") == 0);
    CHECK(rbtrend_buffer_write_after(NULL, 0, bytes, sizeof(bytes), 0, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(RBT_RESULT_REND_IN_USE == 5005);

    // A block of a vec4, a float, a mat3 and an array of two vec2s, as
    // std140 lays them out: 16, 4, 48 (three columns of 16) and 32 (16 an
    // element).
    RBTREND_UNIFORM uniforms[4];
    memset(uniforms, 0, sizeof(uniforms));
    uniforms[0].name = "color";
    uniforms[0].type = RBTREND_UNIFORM_TYPE_VEC4;
    uniforms[0].offset = 0;
    uniforms[1].name = "strength";
    uniforms[1].type = RBTREND_UNIFORM_TYPE_FLOAT;
    uniforms[1].offset = 16;
    uniforms[2].name = "basis";
    uniforms[2].type = RBTREND_UNIFORM_TYPE_MAT3;
    uniforms[2].offset = 32;
    uniforms[3].name = "points";
    uniforms[3].type = RBTREND_UNIFORM_TYPE_VEC2;
    uniforms[3].array_count = 2;
    uniforms[3].offset = 80;
    RBTREND_UNIFORM_LAYOUT layout;
    layout.count = 4;
    layout.uniforms = uniforms;
    layout.size = 0;

    uint32_t index = 7;
    CHECK(rbtrend_uniform_layout_get_uniform_index(&layout, "basis", &index) == RBT_RESULT_OK);
    CHECK(index == 2);
    CHECK(rbtrend_uniform_layout_get_uniform_index(&layout, "nothing", &index) == RBT_RESULT_NOT_FOUND);
    CHECK(index == UINT32_MAX);
    CHECK(rbtrend_uniform_layout_get_uniform_index(NULL, "color", &index) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_uniform_layout_get_uniform_index(&layout, NULL, &index) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_uniform_layout_get_uniform_index(&layout, "color", NULL) == RBT_RESULT_INVALID_ARGUMENT);

    // The points end at 80 + 32 = 112, a multiple of 16 already.
    uint32_t size = 1;
    CHECK(rbtrend_uniform_layout_get_size(&layout, &size, &error) == RBT_RESULT_OK);
    CHECK(size == 112);
    CHECK(error.message[0] == '\0');
    // A declared size past the furthest uniform wins, rounded up to 16.
    layout.size = 120;
    CHECK(rbtrend_uniform_layout_get_size(&layout, &size, NULL) == RBT_RESULT_OK);
    CHECK(size == 128);
    layout.size = 0;
    uniforms[1].type = 99;
    size = 1;
    CHECK(rbtrend_uniform_layout_get_size(&layout, &size, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(size == 0);
    CHECK(strcmp(error.message, "uniform 1 has type 99, which is not an RBTREND_UNIFORM_TYPE") == 0);
    uniforms[1].type = RBTREND_UNIFORM_TYPE_FLOAT;
    RBTREND_UNIFORM_LAYOUT empty;
    memset(&empty, 0, sizeof(empty));
    CHECK(rbtrend_uniform_layout_get_size(&empty, &size, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "the layout has no uniforms and a size of 0") == 0);
    // Only the declared size: a block of members with no uniform type.
    empty.size = 40;
    CHECK(rbtrend_uniform_layout_get_size(&empty, &size, NULL) == RBT_RESULT_OK);
    CHECK(size == 48);
    RBTREND_UNIFORM_LAYOUT far;
    far.count = 1;
    far.uniforms = uniforms;
    far.size = UINT32_MAX;
    CHECK(rbtrend_uniform_layout_get_size(&far, &size, &error) == RBT_RESULT_LIMIT);
    CHECK(rbtrend_uniform_layout_get_size(NULL, &size, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_uniform_layout_get_size(&layout, NULL, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    layout.uniforms = NULL;
    CHECK(rbtrend_uniform_layout_get_size(&layout, &size, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "layout's uniforms is NULL with a count of 4") == 0);
    layout.uniforms = uniforms;

    const float color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    dirty(&error);
    CHECK(rbtrend_buffer_write_uniform(NULL, &layout, 0, color, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "buffer is NULL") == 0);
    CHECK(rbtrend_uniform_type_get_size(RBTREND_UNIFORM_TYPE_MAT3) == 36);
    CHECK(rbtrend_uniform_type_get_size(99) == 0);

    // The pools.
    RBTREND_VERTEX_POOL_DESC vertex_desc;
    memset(&vertex_desc, 0, sizeof(vertex_desc));
    vertex_desc.base.type = RBT_STRUCT_TYPE_REND_VERTEX_POOL_DESC;
    vertex_desc.vertex_stride = 12;
    vertex_desc.block_vertex_count = 1024;
    rbtrend_vertex_pool vertex_pool = (rbtrend_vertex_pool)(uintptr_t)0x20;
    dirty(&error);
    CHECK(rbtrend_vertex_pool_create(NULL, &vertex_desc, &vertex_pool, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(vertex_pool == NULL);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    RBTREND_VERTEX_RANGE vertex_range;
    memset(&vertex_range, 0xFF, sizeof(vertex_range));
    CHECK(rbtrend_vertex_pool_allocate(NULL, 3, bytes, &vertex_range, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(vertex_range.buffer == NULL);
    CHECK(vertex_range.vertex_count == 0);
    CHECK(strcmp(error.message, "pool is NULL") == 0);
    CHECK(rbtrend_vertex_pool_free(NULL, &vertex_range, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_vertex_pool_get_usage(NULL, NULL, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_vertex_pool_trim(NULL);
    rbtrend_vertex_pool_destroy(NULL);

    RBTREND_INDEX_POOL_DESC index_desc;
    memset(&index_desc, 0, sizeof(index_desc));
    index_desc.base.type = RBT_STRUCT_TYPE_REND_INDEX_POOL_DESC;
    index_desc.stride = RBTREND_INDEX_STRIDE_UINT32;
    index_desc.block_index_count = 1024;
    rbtrend_index_pool index_pool = (rbtrend_index_pool)(uintptr_t)0x20;
    CHECK(rbtrend_index_pool_create(NULL, &index_desc, &index_pool, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(index_pool == NULL);
    RBTREND_INDEX_RANGE index_range;
    memset(&index_range, 0xFF, sizeof(index_range));
    CHECK(rbtrend_index_pool_allocate(NULL, 3, bytes, &index_range, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(index_range.buffer == NULL);
    CHECK(rbtrend_index_pool_free(NULL, &index_range, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(rbtrend_index_pool_get_usage(NULL, NULL, NULL) == RBT_RESULT_INVALID_ARGUMENT);
    rbtrend_index_pool_trim(NULL);
    rbtrend_index_pool_destroy(NULL);
}

// ---- Lit: vertex colors, cascades and the light helpers (cards A35, A46) ----

// The bits rbtrend_lit_fit_cascades() gives for three framings: each row a
// cascade's matrix, column after column, then its split. First captured
// 2026-11-01 from rbtrend_lit_fit_sun_cascades() before card A35 renamed it;
// recorded anew 2026-11-20 on the Mac (macOS 27, Apple M5 Pro) with
// --print-fits when card L82 made each cascade's texel a whole fraction of
// a power of two of the units and its view's forward the light's direction
// (every matrix moved). Another platform's libm rounds std::pow and
// std::tan differently, so a value may lie a few ULPs off and is compared
// within FIT_ULPS (card A49).
static const uint32_t s_fit_perspective_4[][17] = {
    {0x3d7e6646u, 0xbdb811c9u, 0x3bfbde12u, 0x00000000u, 0x00000000u, 0xbd4cc6fdu, 0xbc9d6acbu, 0x00000000u, 0x3dcb8504u, 0x3d66163cu, 0xbb9d6acbu, 0x00000000u, 0xbe5e0000u, 0x3e8a0000u, 0x3f532bc6u, 0x3f800000u, 0x40feb9c1u},
    {0x3cfe6646u, 0xbd3811c9u, 0x3bb8d7aau, 0x00000000u, 0x00000000u, 0xbcccc6fdu, 0xbc670d94u, 0x00000000u, 0x3d4b8504u, 0x3ce6163cu, 0xbb670d94u, 0x00000000u, 0xbf128000u, 0xbca00000u, 0x3f3b0d25u, 0x3f800000u, 0x418ce276u},
    {0x3c6d7085u, 0xbcabcc55u, 0x3b65ddebu, 0x00000000u, 0x00000000u, 0xbc3f2020u, 0xbc0faab2u, 0x00000000u, 0x3cbdf39eu, 0x3c56bf6bu, 0xbb0faab2u, 0x00000000u, 0xbf270000u, 0xbe0c0000u, 0x3f22865au, 0x3f800000u, 0x421730cbu},
    {0x3b87ae03u, 0xbbc4573du, 0x3aa5cc4eu, 0x00000000u, 0x00000000u, 0xbb5a6ddbu, 0xbb4f3f61u, 0x00000000u, 0x3bd9166bu, 0x3b756d0du, 0xba4f3f61u, 0x00000000u, 0xbf0f8000u, 0xbe280000u, 0x3f09bff9u, 0x3f800000u, 0x42f00000u},
};
static const uint32_t s_fit_parallel_3[][17] = {
    {0x3c87ae03u, 0xbcc4573du, 0x3b7c3986u, 0x00000000u, 0x00000000u, 0xbc5a6ddbu, 0xbc1da3f3u, 0x00000000u, 0x3cd9166bu, 0x3c756d0du, 0xbb1da3f3u, 0x00000000u, 0xbf080000u, 0xbdb00000u, 0x3f270ddeu, 0x3f800000u, 0x42200000u},
    {0x3c87ae03u, 0xbcc4573du, 0x3b7c3986u, 0x00000000u, 0x00000000u, 0xbc5a6ddbu, 0xbc1da3f3u, 0x00000000u, 0x3cd9166bu, 0x3c756d0du, 0xbb1da3f3u, 0x00000000u, 0xbfd40000u, 0xbeeb0000u, 0x3f20e401u, 0x3f800000u, 0x42a00000u},
    {0x3c87ae03u, 0xbcc4573du, 0x3b7c3986u, 0x00000000u, 0x00000000u, 0xbc5a6ddbu, 0xbc1da3f3u, 0x00000000u, 0x3cd9166bu, 0x3c756d0du, 0xbb1da3f3u, 0x00000000u, 0xc035c000u, 0xbf5a8000u, 0x3f1a6717u, 0x3f800000u, 0x42f00000u},
};
static const uint32_t s_fit_reversed_2[][17] = {
    {0x3cdc7ac5u, 0xbd1f86e2u, 0xbbaada57u, 0x00000000u, 0x00000000u, 0xbcb17942u, 0x3c5590ecu, 0x00000000u, 0x3d306237u, 0x3cc7689bu, 0x3b5590ecu, 0x00000000u, 0xbea70000u, 0x3d200000u, 0x3e914892u, 0x3f800000u, 0x418ce276u},
    {0x3b87ae03u, 0xbbc4573du, 0xbaa5cc4eu, 0x00000000u, 0x00000000u, 0xbb5a6ddbu, 0x3b4f3f61u, 0x00000000u, 0x3bd9166bu, 0x3b756d0du, 0x3a4f3f61u, 0x00000000u, 0xbefa0000u, 0xbe0e0000u, 0x3eeb7012u, 0x3f800000u, 0x42f00000u},
};

// The framings those bits are of: [orthographic], [cascades], [reversed].
static RBTREND_LIT_CASCADE_FIT fit_of(int orthographic, uint32_t cascades, int reversed)
{
    RBTREND_LIT_CASCADE_FIT fit;
    memset(&fit, 0, sizeof(fit));
    fit.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_FIT;
    const float eye[3] = {1.5f, 2.0f, -3.25f}, dir[3] = {0.3f, -0.2f, 1.0f}, up[3] = {0.0f, 1.0f, 0.0f}, light[3] = {-0.4f, 1.0f, 0.25f};
    memcpy(fit.eye_position, eye, sizeof(eye));
    memcpy(fit.view_direction, dir, sizeof(dir));
    memcpy(fit.view_up, up, sizeof(up));
    memcpy(fit.light_direction, light, sizeof(light));
    fit.orthographic = orthographic != 0;
    fit.fov_y = 0.9f;
    fit.half_height = 12.0f;
    fit.aspect = 1.6f;
    fit.near_depth = orthographic ? 0.0f : 0.1f;
    fit.far_depth = 120.0f;
    fit.cascade_count = cascades;
    fit.split_lambda = 0.75f;
    fit.caster_distance = 30.0f;
    fit.map_size = 1024;
    fit.reversed_depth = reversed != 0;
    return fit;
}

// How far each value of a fit may lie from the captured bits, in ULPs (card
// A49, option b): std::pow and std::tan aren't correctly rounded, and their
// last bit differs between Apple's libm, glibc and the UCRT. GCC on x86-64
// lay 1 ULP off in one value of each framing (2026-11-12); a change to the
// fit itself moves values by far more. The matrices are no saved or synced
// data, so they need no bit-equality across platforms.
#define FIT_ULPS 8

// The distance between two floats in ULPs, across zero too: each mapped onto
// a line where adjacent floats are adjacent integers.
static int64_t ulps_apart(uint32_t a_bits, uint32_t b_bits)
{
    int64_t a = (int32_t)a_bits;
    int64_t b = (int32_t)b_bits;
    if (a < 0)
        a = (int64_t)INT32_MIN - a;
    if (b < 0)
        b = (int64_t)INT32_MIN - b;
    return a > b ? a - b : b - a;
}

// --print-fits: each recorded fit's bits printed in its table's form (to
// record them anew after a change to the fit), nothing checked.
static int print_fits = 0;

// The fit of [fit] into maps whose own fields hold markers: the framing is
// [expected]'s bits within FIT_ULPS, the views and splits past the count are 0, and the
// rectangles, biases and softness are the markers still.
static void check_fit(const RBTREND_LIT_CASCADE_FIT *fit, const uint32_t (*expected)[17])
{
    RBTREND_LIT_SHADOW_MAPS maps;
    memset(&maps, 0x7f, sizeof(maps));
    maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
    maps.base.next = NULL;
    maps._rects[2]._x = 77;
    maps._depth_bias = 0.25f;
    maps._normal_offset = 0.5f;
    maps._softness = 1.5f;
    RBTREND_ERROR error;
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(fit, &maps, &error) == RBT_RESULT_OK);
    CHECK(maps._view_count == fit->cascade_count);
    for (uint32_t c = 0; c < fit->cascade_count; ++c)
    {
        uint32_t bits[17];
        memcpy(bits, maps._view_projection[c], sizeof(float) * 16);
        memcpy(&bits[16], &maps._split_depths[c], sizeof(float));
        if (print_fits)
        {
            printf("    {");
            for (int k = 0; k < 17; ++k)
                printf("0x%08xu%s", bits[k], k < 16 ? ", " : "},\n");
            continue;
        }
        for (int k = 0; k < 17; ++k)
            CHECK(ulps_apart(bits[k], expected[c][k]) <= FIT_ULPS);
    }
    for (uint32_t c = fit->cascade_count; c < 6; ++c)
        for (int i = 0; i < 16; ++i)
            CHECK(maps._view_projection[c][i] == 0.0f);
    for (uint32_t c = fit->cascade_count; c < RBTREND_LIT_MAX_CASCADES; ++c)
        CHECK(maps._split_depths[c] == 0.0f);
    CHECK(maps._rects[2]._x == 77);
    CHECK(maps._depth_bias == 0.25f);
    CHECK(maps._normal_offset == 0.5f);
    CHECK(maps._softness == 1.5f);
}

// rbtrend_lit_fit_cascades() frames what rbtrend_lit_fit_sun_cascades() did,
// within FIT_ULPS of its bits, into RBTREND_LIT_SHADOW_MAPS, and refuses what
// it did.
static void test_lit_fit_cascades(void)
{
    current_test = "lit_fit_cascades";
    RBTREND_LIT_CASCADE_FIT fit = fit_of(0, 4, 0);
    check_fit(&fit, s_fit_perspective_4);
    fit = fit_of(1, 3, 0);
    check_fit(&fit, s_fit_parallel_3);
    fit = fit_of(0, 2, 1);
    check_fit(&fit, s_fit_reversed_2);

    RBTREND_LIT_SHADOW_MAPS maps;
    memset(&maps, 0, sizeof(maps));
    maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
    RBTREND_ERROR error;
    fit = fit_of(0, 4, 0);
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(NULL, &maps, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "fit is NULL") == 0);
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(&fit, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "out_maps is NULL") == 0);
    maps.base.type = RBT_STRUCT_TYPE_REND_LIT_LIGHT_SHADOW;
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_STRUCT_WRONG_TYPE);
    CHECK(strcmp(error.message, "out_maps has a base.type other than its own") == 0);
    maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
    fit.cascade_count = 0;
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "cascade_count is 0") == 0);
    CHECK(maps._view_count == 0);
    fit = fit_of(0, 4, 0);
    fit.light_direction[0] = fit.light_direction[1] = fit.light_direction[2] = 0.0f;
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "light_direction is 0") == 0);
    fit = fit_of(0, 4, 0);
    fit.map_size = 0;
    dirty(&error);
    CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "map_size is 0") == 0);
}

// ---- The cascade anchor (RBTREND_LIT_CASCADE_ANCHOR; card L66) ------------------
//
// Tests 2 to 4, 6 and 7 of its approved draft's section 7 (test 1, nothing
// chained, is test_lit_fit_cascades above; test 5 is the misuse harness).
// The fixed world point's texel worked out in double from the fit's float
// matrix, as the GPU applies it.

// The texel coordinate (x, y) a scene place lands on through cascade [c].
static void texel_of(const RBTREND_LIT_SHADOW_MAPS *maps, uint32_t c, uint32_t map_size, const double scene[3], double out[2])
{
    const float *m = maps->_view_projection[c];
    const double half = (double)map_size * 0.5;
    for (int r = 0; r < 2; ++r)
        out[r] = ((double)m[0 * 4 + r] * scene[0] + (double)m[1 * 4 + r] * scene[1] + (double)m[2 * 4 + r] * scene[2] +
                  (double)m[3 * 4 + r]) * half;
}

static double wrapped(double a)
{
    a = a - floor(a + 0.5);
    return a;
}

// A fit whose scene's origin is the eye: the eye at (0, 0, 0).
static RBTREND_LIT_CASCADE_FIT eye_fit(void)
{
    RBTREND_LIT_CASCADE_FIT fit = fit_of(0, 4, 1);
    fit.eye_position[0] = fit.eye_position[1] = fit.eye_position[2] = 0.0f;
    fit.far_depth = 400.0f;
    fit.map_size = 2048;
    return fit;
}

static double fit_drift(const double origin0[3], int anchored, uint32_t frames)
{
    RBTREND_LIT_CASCADE_FIT fit = eye_fit();
    RBTREND_LIT_CASCADE_ANCHOR anchor;
    memset(&anchor, 0, sizeof(anchor));
    anchor.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR;
    if (anchored)
        fit.base.next = &anchor.base;
    const double len = sqrt((double)fit.view_direction[0] * fit.view_direction[0] + (double)fit.view_direction[1] * fit.view_direction[1] +
                            (double)fit.view_direction[2] * fit.view_direction[2]);
    double step[3];
    for (int a = 0; a < 3; ++a)
        step[a] = 0.01 * (double)fit.view_direction[a] / len;
    // One fixed world point per cascade, inside it at the first frame.
    const double depth[4] = {3.0, 20.0, 80.0, 250.0};
    double world[4][3];
    for (int c = 0; c < 4; ++c)
        for (int a = 0; a < 3; ++a)
            world[c][a] = origin0[a] + depth[c] * (double)fit.view_direction[a] / len + (a == 1 ? 0.75 : 0.0);
    double first[4][2], worst = 0.0;
    for (uint32_t k = 0; k < frames; ++k)
    {
        double o[3];
        for (int a = 0; a < 3; ++a)
            o[a] = origin0[a] + step[a] * (double)k;
        anchor.scene_origin.x = o[0];
        anchor.scene_origin.y = o[1];
        anchor.scene_origin.z = o[2];
        RBTREND_LIT_SHADOW_MAPS maps;
        memset(&maps, 0, sizeof(maps));
        maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
        CHECK(rbtrend_lit_fit_cascades(&fit, &maps, NULL) == RBT_RESULT_OK);
        for (uint32_t c = 0; c < 4; ++c)
        {
            double scene[3], t[2];
            for (int a = 0; a < 3; ++a)
                scene[a] = (double)(float)(world[c][a] - o[a]);
            texel_of(&maps, c, fit.map_size, scene, t);
            for (int r = 0; r < 2; ++r)
            {
                if (k == 0)
                    first[c][r] = t[r];
                const double d = fabs(wrapped(t[r] - first[c][r]));
                if (d > worst)
                    worst = d;
            }
        }
    }
    return worst;
}

static const uint32_t s_fit_anchor_far_4[][17] = {
#include "data/rbtrend_contract/anchor_far_4.inc"
};

static void test_lit_cascade_anchor(void)
{
    current_test = "lit_cascade_anchor";
    // 2. Stable under a moving eye, at the origin, 1e7 out and 4.5e12 out:
    // a fixed world point's texel fraction the same every frame, within 1e-3.
    const double origins[3][3] = {{0.0, 0.0, 0.0}, {1e7, 0.0, 1e7}, {4.5e12, 0.0, 0.0}};
    for (int o = 0; o < 3; ++o)
    {
        const double anchored = fit_drift(origins[o], 1, 600);
        const double loose = fit_drift(origins[o], 0, 600);
        printf("rbtrend_contract_tests: cascades under an eye moving 1 cm a fit, scene origin %g out: a fixed point's texel "
               "drifts %.3g of a texel anchored, %.3g not\n",
               origins[o][0] + origins[o][2], anchored, loose);
        CHECK(anchored <= 1e-3);
    }
    // 3. At the origin, the same whole-texel snap as today's, the matrices
    // within 1e-6 of a texel.
    {
        RBTREND_LIT_CASCADE_FIT fit = fit_of(0, 4, 0);
        RBTREND_LIT_SHADOW_MAPS plain, chained;
        memset(&plain, 0, sizeof(plain));
        plain.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
        chained = plain;
        CHECK(rbtrend_lit_fit_cascades(&fit, &plain, NULL) == RBT_RESULT_OK);
        RBTREND_LIT_CASCADE_ANCHOR anchor;
        memset(&anchor, 0, sizeof(anchor));
        anchor.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR;
        fit.base.next = &anchor.base;
        CHECK(rbtrend_lit_fit_cascades(&fit, &chained, NULL) == RBT_RESULT_OK);
        const double half = (double)fit.map_size * 0.5;
        for (uint32_t c = 0; c < fit.cascade_count; ++c)
            for (int i = 0; i < 16; ++i)
                CHECK(fabs((double)plain._view_projection[c][i] - (double)chained._view_projection[c][i]) * half <= 1e-6);
    }
    // 4. Refused: each axis not finite, its own message; the anchor twice;
    // another struct chained on the fit; the anchor on the out struct.
    {
        RBTREND_LIT_CASCADE_FIT fit = fit_of(0, 4, 0);
        RBTREND_LIT_SHADOW_MAPS maps;
        memset(&maps, 0, sizeof(maps));
        maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
        RBTREND_LIT_CASCADE_ANCHOR anchor, second;
        memset(&anchor, 0, sizeof(anchor));
        anchor.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR;
        fit.base.next = &anchor.base;
        RBTREND_ERROR error;
        const char *axes[3] = {"scene_origin.x is not finite", "scene_origin.y is not finite", "scene_origin.z is not finite"};
        for (int a = 0; a < 3; ++a)
        {
            anchor.scene_origin.x = anchor.scene_origin.y = anchor.scene_origin.z = 0.0;
            if (a == 0)
                anchor.scene_origin.x = NAN;
            if (a == 1)
                anchor.scene_origin.y = INFINITY;
            if (a == 2)
                anchor.scene_origin.z = -INFINITY;
            maps._view_count = 77;
            dirty(&error);
            CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_INVALID_ARGUMENT);
            CHECK(strstr(error.message, axes[a]) != NULL);
            CHECK(maps._view_count == 77);
        }
        anchor.scene_origin.x = anchor.scene_origin.y = anchor.scene_origin.z = 0.0;
        second = anchor;
        anchor.base.next = &second.base;
        dirty(&error);
        CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_STRUCT_DUPLICATE_IN_CHAIN);
        anchor.base.next = NULL;
        RBT_STRUCT_BASE other = {RBT_STRUCT_TYPE_REND_LIT_ATMOSPHERE, NULL};
        fit.base.next = &other;
        dirty(&error);
        CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN);
        fit.base.next = NULL;
        maps.base.next = &anchor.base;
        dirty(&error);
        CHECK(rbtrend_lit_fit_cascades(&fit, &maps, &error) == RBT_RESULT_STRUCT_UNKNOWN_IN_CHAIN);
    }
    // 6. Recorded on the Mac (owed: Windows and Linux to match, within the
    // fit's FIT_ULPS for its float part, card A49): an anchored fit 1e7 out.
    {
        RBTREND_LIT_CASCADE_FIT fit = eye_fit();
        RBTREND_LIT_CASCADE_ANCHOR anchor;
        memset(&anchor, 0, sizeof(anchor));
        anchor.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR;
        anchor.scene_origin.x = 6.5e6 + 0.37;
        anchor.scene_origin.y = 1.5e5 - 2.25;
        anchor.scene_origin.z = -1e7 + 11.125;
        fit.base.next = &anchor.base;
        check_fit(&fit, s_fit_anchor_far_4);
    }
    // 7. Recorded, not asserted: a light turning 1.2e-6 radians a fit (a day
    // of 24 hours at 60 Hz), the view still, the fixed point 0, 200 m, 250 km
    // and 1e7 m from it: the grid's slide a fit near the view, in texels,
    // against the distance times the turn (card A60).
    {
        const double distances[4] = {0.0, 200.0, 2.5e5, 1e7};
        for (int d = 0; d < 4; ++d)
        {
            RBTREND_LIT_CASCADE_FIT fit = eye_fit();
            RBTREND_LIT_CASCADE_ANCHOR anchor;
            memset(&anchor, 0, sizeof(anchor));
            anchor.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR;
            anchor.scene_origin.x = distances[d];
            fit.base.next = &anchor.base;
            const double angle0 = atan2((double)fit.light_direction[1], (double)fit.light_direction[0]);
            const double r = sqrt((double)fit.light_direction[0] * fit.light_direction[0] +
                                  (double)fit.light_direction[1] * fit.light_direction[1]);
            double last = 0.0, sum = 0.0, most = 0.0;
            for (int k = 0; k < 600; ++k)
            {
                fit.light_direction[0] = (float)(r * cos(angle0 + 1.2e-6 * k));
                fit.light_direction[1] = (float)(r * sin(angle0 + 1.2e-6 * k));
                RBTREND_LIT_SHADOW_MAPS maps;
                memset(&maps, 0, sizeof(maps));
                maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
                CHECK(rbtrend_lit_fit_cascades(&fit, &maps, NULL) == RBT_RESULT_OK);
                const double scene[3] = {0.0, 0.0, 5.0};
                double t[2];
                texel_of(&maps, 0, fit.map_size, scene, t);
                if (k > 0)
                {
                    const double moved = fabs(wrapped(t[0] - last));
                    sum += moved;
                    if (moved > most)
                        most = moved;
                }
                last = t[0];
            }
            printf("rbtrend_contract_tests: a light turning 1.2e-6 rad a fit, the fixed point %g m from the view: the nearest "
                   "cascade's grid moves %.3g texels a fit on average, %.3g at most (recorded, card A60)\n",
                   distances[d], sum / 599.0, most);
        }
    }
    // 8. Card L82: the same view anchored 100 units and (1e7, 0, -1e7) along
    // the fixed space's axes from the first, under a light straight down
    // (whose view's axes are the fixed space's x and z): a scene moved with
    // it lands on each cascade's texels as it did, within 1e-3 of a texel
    // (each cascade's texel a whole fraction of a power of two of the
    // units). Seen failing first: the texel any size, 0.481 of a texel off.
    {
        const double offsets[3][3] = {{0.0, 0.0, 0.0}, {100.0, 0.0, 0.0}, {1e7, 0.0, -1e7}};
        const double scenes[3][3] = {{0.5, -1.0, 2.0}, {-30.0, -4.0, 60.0}, {120.0, -10.0, 300.0}};
        double first[RBTREND_LIT_MAX_CASCADES][3][2], worst = 0.0;
        for (int o = 0; o < 3; ++o)
        {
            RBTREND_LIT_CASCADE_FIT fit = eye_fit();
            fit.light_direction[0] = 0.0f;
            fit.light_direction[1] = 1.0f;
            fit.light_direction[2] = 0.0f;
            RBTREND_LIT_CASCADE_ANCHOR anchor;
            memset(&anchor, 0, sizeof(anchor));
            anchor.base.type = RBT_STRUCT_TYPE_REND_LIT_CASCADE_ANCHOR;
            anchor.scene_origin.x = 3.0 + offsets[o][0];
            anchor.scene_origin.y = 1.75 + offsets[o][1];
            anchor.scene_origin.z = -2.0 + offsets[o][2];
            fit.base.next = &anchor.base;
            RBTREND_LIT_SHADOW_MAPS maps;
            memset(&maps, 0, sizeof(maps));
            maps.base.type = RBT_STRUCT_TYPE_REND_LIT_SHADOW_MAPS;
            CHECK(rbtrend_lit_fit_cascades(&fit, &maps, NULL) == RBT_RESULT_OK);
            for (uint32_t c = 0; c < fit.cascade_count; ++c)
            {
                for (int p = 0; p < 3; ++p)
                {
                    double t[2];
                    texel_of(&maps, c, fit.map_size, scenes[p], t);
                    for (int r = 0; r < 2; ++r)
                    {
                        if (o == 0)
                            first[c][p][r] = t[r];
                        const double d = fabs(wrapped(t[r] - first[c][p][r]));
                        if (d > worst)
                            worst = d;
                    }
                }
            }
        }
        printf("rbtrend_contract_tests: a view anchored 100 units and 1e7 along x and z from another, a light straight down: a scene "
               "place's texel %.3g of a texel apart at most\n",
               worst);
        CHECK(worst <= 1e-3);
    }
}

// rbtrend_get_lit_vertex_color_layout(): one VEC4 named color at 0, a stride of
// 16 (card A46).
static void test_lit_vertex_color_layout(void)
{
    current_test = "lit_vertex_color_layout";
    const RBTREND_VERTEX_LAYOUT layout = rbtrend_get_lit_vertex_color_layout();
    CHECK(layout.count == 1);
    CHECK(layout.stride == 16);
    CHECK(layout.elements != NULL);
    if (layout.elements)
    {
        CHECK(layout.elements[0].type == RBTREND_VERTEX_ELEMENT_TYPE_VEC4);
        CHECK(layout.elements[0].offset == 0);
    }
    CHECK(rbtrend_vertex_layout_get_element_index(&layout, "color") == 0);
}

// A light the view-projection helpers refuse, as rbtrend_lit_scene_set_lights()
// does (they share its check): [light] refused by [call] with [message].
static void light_refused(const RBTREND_LIT_SCENE_LIGHT *light, int spot, const char *message)
{
    RBTREND_ERROR error;
    float matrix[16];
    dirty(&error);
    const RBT_RESULT result = spot ? rbtrend_lit_spot_shadow_view_projection(light, 0.1f, false, matrix, &error)
                                 : rbtrend_lit_point_shadow_view_projection(light, 0, 0.1f, false, matrix, &error);
    CHECK(result == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, message) == 0);
    if (strcmp(error.message, message) != 0)
        printf("  got \"%s\"\n", error.message);
}

// The light checks set_lights and the helpers share, through the helpers,
// which need no scene: a radius on a point light, a disc on a spot, a
// directional light where a spot is wanted. A scene's own refusals (a fifth
// directional light, a 129th point or spot light, the shadows') need a
// device, and are rabbittustests' (test_lit_light_refusals).
static void test_lit_light_checks(void)
{
    current_test = "lit_light_checks";
    RBTREND_LIT_SCENE_LIGHT point;
    memset(&point, 0, sizeof(point));
    point._type = RBTREND_LIT_LIGHT_TYPE_POINT;
    point._radiance[0] = point._radiance[1] = point._radiance[2] = 1.0f;
    point._range = 10.0f;
    RBTREND_ERROR error;
    float matrix[16];
    dirty(&error);
    CHECK(rbtrend_lit_point_shadow_view_projection(&point, 0, 0.1f, false, matrix, &error) == RBT_RESULT_OK);
    point._radius = 0.1f;
    light_refused(&point, 0, "light 0, a point or spot light, has a _radius that is not 0 (card A38)");

    RBTREND_LIT_SCENE_LIGHT spot = point;
    spot._type = RBTREND_LIT_LIGHT_TYPE_SPOT;
    spot._radius = 0.0f;
    spot._direction[1] = -1.0f;
    spot._inner_angle = 0.2f;
    spot._outer_angle = 0.4f;
    dirty(&error);
    CHECK(rbtrend_lit_spot_shadow_view_projection(&spot, 0.1f, false, matrix, &error) == RBT_RESULT_OK);
    spot._sky_disc = true;
    light_refused(&spot, 1, "light 0, a point or spot light, has _sky_disc set");
    spot._sky_disc = false;
    spot._outer_angle = 2.0f;
    light_refused(&spot, 1, "spot light 0's _outer_angle is past pi / 2");

    RBTREND_LIT_SCENE_LIGHT directional = spot;
    directional._type = RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL;
    directional._outer_angle = 0.4f;
    directional._radius = 0.00465f;
    directional._sky_disc = true;
    light_refused(&directional, 1, "the light is not a spot");
    directional._type = 3;
    light_refused(&directional, 1, "the light is not a spot");
    // A placed directional light is type 3 (rbtrend_placed_light_draft.h,
    // card L78); what set_lights takes and refuses of it needs a scene, so a
    // device: rabbittustests' test_lit_placed_refusals.
    CHECK(RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED == 3);
    directional._type = RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED;
    light_refused(&directional, 1, "the light is not a spot");

    // The scene's calls refuse a NULL scene first.
    dirty(&error);
    CHECK(rbtrend_lit_scene_set_shadow_atlas(NULL, NULL, false, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "scene is NULL") == 0);
    dirty(&error);
    CHECK(rbtrend_lit_scene_set_light_shadow(NULL, 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "scene is NULL") == 0);
    CHECK(rbtrend_lit_scene_get_shadow_pass_material(NULL, 0, 0) == NULL);
}

// The reflection commands' parameters fit RBTREND_COMMAND's 120 bytes, at
// the sizes the approved draft gives on a 64-bit build, and the command
// stays 128 bytes; RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT is 8. Every
// refusal of the three reads a view, so needs a device: rabbittustests'
// test_lit_reflection_refusals.
static void test_reflection_commands(void)
{
    current_test = "reflection_commands";
    RBTREND_COMMAND command;
    CHECK(sizeof(RBTREND_COMMAND) == 128);
    CHECK(RBTREND_COMMAND_LIT_SSR == 45);
    CHECK(RBTREND_COMMAND_LIT_TRACE_REFLECTIONS == 46);
    CHECK(RBTREND_COMMAND_LIT_REFLECT == 47);
    CHECK(RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT == 8);
    if (sizeof(void *) == 8)
    {
        CHECK(sizeof(command._params._lit_ssr) == 64);
        CHECK(sizeof(command._params._lit_trace_reflections) == 56);
        CHECK(sizeof(command._params._lit_reflect) == 48);
    }
    CHECK(sizeof(command._params._lit_ssr) <= sizeof(command._params._padding));
    CHECK(sizeof(command._params._lit_trace_reflections) <= sizeof(command._params._padding));
    CHECK(sizeof(command._params._lit_reflect) <= sizeof(command._params._padding));
}

// The atmosphere's shell seen from outside (rbtrend_atmosphere_space_draft.h,
// card A55): command 51, its parameters within RBTREND_COMMAND's 120 bytes
// (48 on a 64-bit build), the frozen 32-byte shell and its two limits. Its
// refusals read views and a scene's lights, so need a device:
// rabbittustests' test_atmosphere_shell_refusals.
static void test_atmosphere_shell_command(void)
{
    current_test = "atmosphere_shell_command";
    RBTREND_COMMAND command;
    CHECK(RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS == 51);
    CHECK(sizeof(RBTREND_COMMAND) == 128);
    if (sizeof(void *) == 8)
        CHECK(sizeof(command._params._lit_atmosphere_shells) == 48);
    CHECK(sizeof(command._params._lit_atmosphere_shells) <= sizeof(command._params._padding));
    CHECK(sizeof(RBTREND_LIT_ATMOSPHERE_SHELL) == (sizeof(void *) == 8 ? 32 : 28));
    CHECK(RBTREND_LIT_ATMOSPHERE_MAX_SHELL_STEPS == 128u);
    CHECK(RBTREND_LIT_ATMOSPHERE_MAX_SHELL_LIGHTS == 2u);
    RBTREND_LIT_ATMOSPHERE_SHELL shell;
    CHECK(sizeof(shell._lights) / sizeof(shell._lights[0]) == RBTREND_LIT_ATMOSPHERE_MAX_SHELL_LIGHTS);
}

// A lit scene's desc (card A67, rbtrend_view_sets_draft.h): its struct
// type, the records' maximum, and its layout. Its refusals past the
// instance need a device (rabbittustests' test_view_sets_refusals).
static void test_lit_scene_desc(void)
{
    current_test = "lit_scene_desc";
    CHECK(RBT_STRUCT_TYPE_REND_LIT_SCENE_DESC == 5040);
    CHECK(RBTREND_LIT_SCENE_MAX_VIEW_SETS == 256u);
    CHECK(offsetof(RBTREND_LIT_SCENE_DESC, base) == 0);
    CHECK(offsetof(RBTREND_LIT_SCENE_DESC, max_view_sets) == sizeof(RBT_STRUCT_BASE));
    CHECK(sizeof(RBTREND_LIT_SCENE_DESC) == (sizeof(void *) == 8 ? 24 : 12));
}

// Card A59's stage 2 (rbtrend_fog_draft.h, section 11): command 52, its
// parameters within RBTREND_COMMAND's 120 bytes (72 on a 64-bit build), the
// desc's struct type and layout, the frozen volume and the four limits; the
// fog's calls with NULL, each its own message. Its refusals past these read
// views and a scene, so need a device: rabbittustests' test_fog_records.
static void test_fog_grid_contract(void)
{
    current_test = "fog_grid_contract";
    RBTREND_COMMAND command;
    CHECK(RBTREND_COMMAND_LIT_FOG == 52);
    CHECK(sizeof(RBTREND_COMMAND) == 128);
    if (sizeof(void *) == 8)
        CHECK(sizeof(command._params._lit_fog) == 72);
    CHECK(sizeof(command._params._lit_fog) <= sizeof(command._params._padding));
    CHECK(RBT_STRUCT_TYPE_REND_FOG_DESC == 5042);
    CHECK(offsetof(RBTREND_FOG_DESC, base) == 0);
    CHECK(sizeof(RBTREND_FOG_DESC) == (sizeof(void *) == 8 ? 48 : 32));
    CHECK(sizeof(RBTREND_FOG_VOLUME_INSTANCE) == sizeof(RBTMATH_FOG_VOLUME) + 18 * sizeof(float));
    CHECK(RBTREND_FOG_MAX_VOLUMES == 64u);
    CHECK(RBTREND_FOG_MAX_TILE_VOLUMES == 15u);
    CHECK(RBTREND_FOG_MAX_VIEWS == 16u);
    CHECK(RBTREND_FOG_MAX_GRID_SIDE == 512u);
    CHECK(RBTREND_FOG_MAX_GRID_SLICES == 256u);
    RBTREND_ERROR error;
    rbtrend_fog fog = (rbtrend_fog)(uintptr_t)1;
    dirty(&error);
    CHECK(rbtrend_fog_create(NULL, NULL, &fog, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(fog == NULL);
    dirty(&error);
    CHECK(rbtrend_fog_write_noise(NULL, NULL, 0, 1, NULL, 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "fog is NULL") == 0);
    rbtrend_fog_destroy(NULL);
}

// Card A57's stage 1 (rbtrend_clouds_draft.h, section 11, test 14's headless
// half): command 53, its parameters within RBTREND_COMMAND's 120 bytes (72 on
// a 64-bit build), the two based structs' types and layouts, the frozen
// layer and the limits; the clouds' calls with NULL, each its own message.
// Its refusals past these need a device: rabbittustests' test_clouds_refusals.
static void test_clouds_contract(void)
{
    current_test = "clouds_contract";
    RBTREND_COMMAND command;
    CHECK(RBTREND_COMMAND_LIT_CLOUDS == 53);
    if (sizeof(void *) == 8)
        CHECK(sizeof(command._params._lit_clouds) == 72);
    CHECK(sizeof(command._params._lit_clouds) <= sizeof(command._params._padding));
    CHECK(RBT_STRUCT_TYPE_REND_CLOUDS_DESC == 5043);
    CHECK(RBT_STRUCT_TYPE_REND_CLOUD_LOOK == 5044);
    CHECK(offsetof(RBTREND_CLOUDS_DESC, base) == 0);
    CHECK(sizeof(RBTREND_CLOUDS_DESC) == (sizeof(void *) == 8 ? 56 : 44));
    CHECK(offsetof(RBTREND_CLOUD_LOOK, base) == 0);
    CHECK(offsetof(RBTREND_CLOUD_LOOK, shape) == sizeof(RBT_STRUCT_BASE));
    CHECK(offsetof(RBTREND_CLOUD_LAYER, _bounds) == 8);
    CHECK(offsetof(RBTREND_CLOUD_LAYER, _offsets) == 8 + sizeof(RBTMATH_CLOUD_BOUNDS) + 12 * sizeof(float));
    CHECK(RBTREND_CLOUDS_MAX_LAYERS == 4u);
    CHECK(RBTREND_CLOUDS_MAX_LOOKS == 16u);
    CHECK(RBTREND_CLOUDS_MAX_COVERAGE_MAPS == 8u);
    CHECK(RBTREND_CLOUDS_MAX_VIEWS == 16u);
    CHECK(RBTREND_CLOUDS_MAX_STEPS == 256u);
    CHECK(RBTREND_CLOUDS_MAX_LIGHT_STEPS == 16u);
    CHECK(RBTREND_CLOUDS_MAX_LAYER_LIGHTS == 2u);
    CHECK(RBTREND_CLOUDS_MAX_SCATTERING_OCTAVES == 4u);
    RBTREND_ERROR error;
    rbtrend_clouds clouds = (rbtrend_clouds)(uintptr_t)1;
    dirty(&error);
    CHECK(rbtrend_clouds_create(NULL, NULL, &clouds, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(clouds == NULL);
    dirty(&error);
    CHECK(rbtrend_clouds_write_noise(NULL, NULL, RBTMATH_NOISE_VOLUME_KIND_WORLEY, 0, 1, NULL, 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "clouds is NULL") == 0);
    dirty(&error);
    CHECK(rbtrend_clouds_write_coverage(NULL, NULL, 0, 0, 0, 0, 1, 1, NULL, 0, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "clouds is NULL") == 0);
    dirty(&error);
    CHECK(rbtrend_clouds_set_look(NULL, 0, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "clouds is NULL") == 0);
    rbtrend_clouds_destroy(NULL);
}

// Card L73's Rend half (rbtrend_particles_draft.h, section 9, T1's
// headless half): commands 54 and 55, their parameters within
// RBTREND_COMMAND's 120 bytes, the struct types and layouts, the frozen
// step and curve, the limits and the shader numbers; every call with NULL,
// each its own message. Its refusals past these need a device:
// rabbittustests' test_particles_refusals.
static void test_particles_contract(void)
{
    current_test = "particles_contract";
    RBTREND_COMMAND command;
    CHECK(RBTREND_COMMAND_PARTICLES_STEP == 54);
    CHECK(RBTREND_COMMAND_DRAW_PARTICLES == 55);
    if (sizeof(void *) == 8)
    {
        CHECK(sizeof(command._params._particles_step) == 32);
        CHECK(sizeof(command._params._draw_particles) == 8);
        CHECK(sizeof(RBTREND_PARTICLES_DESC) == 32);
        CHECK(sizeof(RBTREND_PARTICLES_STEP) == 120);
        CHECK(offsetof(RBTREND_PARTICLES_STEP, _anchor) == 24);
        CHECK(offsetof(RBTREND_PARTICLES_STEP, _wind) == 108);
    }
    CHECK(sizeof(command._params._particles_step) <= sizeof(command._params._padding));
    CHECK(sizeof(RBTREND_PARTICLE_CURVE) == 4 + 8 * 4 + 8 * 16);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLES_DESC == 5047);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_EFFECT == 5048);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_SPAWN == 5049);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_FORCES == 5050);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_NOISE == 5051);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_COLLISION == 5052);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_WRAP == 5053);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_COLOR_OVER_LIFE == 5054);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_SIZE_OVER_LIFE == 5055);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_FLIPBOOK == 5056);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_SOFT == 5057);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_STREAK == 5058);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_MESH == 5059);
    CHECK(RBT_STRUCT_TYPE_REND_PARTICLE_RIBBON == 5060);
    CHECK(offsetof(RBTREND_PARTICLES_DESC, base) == 0);
    CHECK(offsetof(RBTREND_PARTICLE_EFFECT, base) == 0);
    CHECK(offsetof(RBTREND_PARTICLE_EFFECT, space) == sizeof(RBT_STRUCT_BASE));
    CHECK(RBTREND_PARTICLES_MAX_CAPACITY == 1048576u);
    CHECK(RBTREND_PARTICLE_CURVE_MAX_KEYS == 8u);
    CHECK(RBTREND_PARTICLES_SET == 2u);
    CHECK(RBTREND_PARTICLES_MAX_STEPS == 4096u);
    CHECK(RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX == 0);
    CHECK(RBTREND_PARTICLES_SHADER_MESH_VERTEX == 1);
    CHECK(RBTREND_PARTICLES_SHADER_LIT_FRAGMENT == 2);
    CHECK(RBTREND_PARTICLES_SHADER_UNLIT_FRAGMENT == 3);
    RBTREND_ERROR error;
    rbtrend_particles particles = (rbtrend_particles)(uintptr_t)1;
    dirty(&error);
    CHECK(rbtrend_particles_create(NULL, NULL, &particles, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(particles == NULL);
    dirty(&error);
    CHECK(rbtrend_particles_set_effect(NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "particles is NULL") == 0);
    dirty(&error);
    CHECK(rbtrend_particles_set_noise(NULL, NULL, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "particles is NULL") == 0);
    dirty(&error);
    CHECK(rbtrend_particles_set_depth(NULL, NULL, false, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "particles is NULL") == 0);
    uint32_t alive = 7;
    uint64_t step = 7;
    dirty(&error);
    CHECK(rbtrend_particles_get_alive_count(NULL, &alive, &step, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "particles is NULL") == 0);
    rbtrend_binding_layout layout = (rbtrend_binding_layout)(uintptr_t)1;
    dirty(&error);
    CHECK(rbtrend_particles_get_layout(NULL, &layout, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(layout == NULL);
    rbtrend_shader shader = (rbtrend_shader)(uintptr_t)1;
    dirty(&error);
    CHECK(rbtrend_particles_get_shader(NULL, RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX, &shader, &error) == RBT_RESULT_INVALID_ARGUMENT);
    CHECK(strcmp(error.message, "instance is NULL") == 0);
    CHECK(shader == NULL);
    rbtrend_particles_destroy(NULL);
}

int main(int argc, char **argv)
{
    print_fits = argc > 1 && strcmp(argv[1], "--print-fits") == 0;
    test_instance_create();
    test_atmosphere_shell_command();
    test_fog_grid_contract();
    test_clouds_contract();
    test_particles_contract();
    test_lit_scene_desc();
    test_null_instance();
    test_images_null();
    test_pipelines_null();
    test_frames_and_passes_null();
    test_materials_and_lit_null();
    test_draw_lists_and_swap_chains_null();
    test_command_lists_null();
    test_buffers();
    test_lit_fit_cascades();
    test_lit_cascade_anchor();
    test_lit_vertex_color_layout();
    test_lit_light_checks();
    test_reflection_commands();
    if (failures)
    {
        printf("rbtrend_contract_tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("rbtrend_contract_tests: all passed\n");
    return 0;
}
