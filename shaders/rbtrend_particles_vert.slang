// Rend's particle vertex shader for billboards, streaks and ribbons
// (rbtrendparticles.h, RBTREND_PARTICLES_SHADER_BILLBOARD_VERTEX): no
// vertex buffer; six vertices an instance, an instance a particle of the
// system's current alive list (sorted where its effect sorts), placed by
// the system's last step's anchor (and emitter, in EMITTER space). A
// billboard is a square of the particle's size facing the eye, upright as
// the view draws it, turned by its rotation and spin; a streak runs from
// the particle back along its velocity as the eye sees it, its length the
// STREAK module's, its width the particle's size; a ribbon's segment joins
// a particle to the one spawned before it (ribbon_vertex()). Writes
// locations 0 to 4 as a lit vertex shader does (the place, a normal facing
// the eye, the flipbook frame's texture coordinate, no tangent, the colour
// over life) and the flipbook's next frame at 5.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_draw_bindings;
import rbtrend_particles_draw;

static const float2 CORNERS[6] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)};

// [v] made unit, or [fallback] where it has no length.
float3 unit_or(float3 v, float3 fallback) {
    float l = dot(v, v);
    return l > 1.0e-24 ? v * rsqrt(l) : fallback;
}

// Frame [frame]'s texture coordinate at [corner] (0 to 1, the top left
// first) in a grid of [columns] by [rows].
float2 frame_uv(uint frame, float2 corner, uint columns, uint rows) {
    return (float2(float(frame % columns), float(frame / columns)) + corner) / float2(float(columns), float(rows));
}

// The way to the eye at [center], along the view's ray through it (its
// points at depths 0 and 1, taken homogeneously, turned towards the eye):
// the eye's place in a perspective view, the view's direction in an
// orthographic one, where every ray is parallel; and right and up across
// it, up as the view draws it at [center] (what moves it up the target at
// its depth).
void particles_facing(float3 center, out float3 toward, out float3 right, out float3 up) {
    float4 clip    = mul(u_lit_scene.view_projection, float4(center, 1.0));
    float2 ndc     = clip.w != 0.0 ? clip.xy / clip.w : float2(0.0);
    float4 far_    = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 near_   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    toward         = unit_or(near_.xyz * far_.w - far_.xyz * near_.w, float3(0.0, 0.0, 1.0));
    if (dot(toward, u_lit_scene.eye_position.xyz - center) < 0.0)
        toward = -toward;
    float4 above   = mul(u_lit_scene.inverse_view_projection, clip + float4(0.0, 0.01 * abs(clip.w), 0.0, 0.0));
    float3 up_hint = unit_or(above.xyz / above.w - center, float3(0.0, 1.0, 0.0));
    right          = unit_or(cross(up_hint, toward), unit_or(cross(float3(0.0, 0.0, 1.0), toward), float3(1.0, 0.0, 0.0)));
    up             = cross(toward, right);
}

// A particle's size and colour over its life.
float particles_size_of(Particle p) {
    float t    = saturate(p.age / max(p.life, 1.0e-30));
    float size = p.size;
    if ((u_particles.kind.x & MODULE_SIZE_OVER_LIFE) != 0u)
        size *= particles_size_curve(u_particles.size_times, u_particles.size_values, u_particles.curves.y, t);
    return size;
}

float4 particles_color_of(Particle p) {
    float t      = saturate(p.age / max(p.life, 1.0e-30));
    float4 color = u_particles.color;
    if ((u_particles.kind.x & MODULE_COLOR_OVER_LIFE) != 0u)
        color *= particles_curve(u_particles.color_times, u_particles.color_values, u_particles.curves.x, t);
    return color;
}

// A particle's place in the scene's space.
float3 particles_place(Particle p) {
    float4 rows[3];
    draw_emitter(rows);
    return draw_anchor() + (u_particles.kind.z == SPACE_EMITTER ? particles_transform(rows, p.position) : p.position);
}

// A ribbon's segment [instance] (RBTREND_PARTICLE_DRAW_RIBBON): from the
// particle numbered the newest less [instance] back to the one spawned
// before it, while both are alive (all six vertices at one point
// otherwise: nothing drawn), each end as wide as its particle's size,
// across the strip's direction there (from the particle's neighbours,
// where they are alive) as the eye sees it; u along the strip, 0 at the
// newest and RIBBON's repeats at the end of the window.
ParticleVertexOutput ribbon_vertex(uint vertex, uint instance) {
    ParticleVertexOutput output;
    output.position = float4(0.0);
    output.world    = float3(0.0);
    output.normal   = float3(0.0, 0.0, 1.0);
    output.uv       = float2(0.0);
    output.tangent  = float4(0.0);
    output.color    = float4(0.0);
    output.flipbook = float4(0.0);
    uint spawned = draw_counter(COUNTER_SPAWNED);
    uint newest  = spawned - 1u;
    uint a       = newest - instance;
    Particle pa, pb;
    if (!ribbon_particle(a, pa) || !ribbon_particle(a - 1u, pb))
        return output;
    float3 head = particles_place(pa);
    float3 tail = particles_place(pb);
    Particle neighbour;
    float3 after  = ribbon_particle(a + 1u, neighbour) ? particles_place(neighbour) : head;
    float3 before = ribbon_particle(a - 2u, neighbour) ? particles_place(neighbour) : tail;
    float2 corner = CORNERS[vertex % 6u];
    bool at_head  = corner.x > 0.0;
    Particle p    = at_head ? pa : pb;
    float3 place  = at_head ? head : tail;
    float3 along  = at_head ? after - tail : head - before;
    float3 toward, right, up;
    particles_facing(place, toward, right, up);
    float3 side   = unit_or(cross(along, toward), up);
    float3 world  = place + side * (corner.y * particles_size_of(p) * 0.5);
    uint window   = min(spawned - draw_counter(COUNTER_RIBBON_FIRST), u_particles.kind.w);
    float u       = float(newest - p.number) / float(max(window, 2u) - 1u) * u_particles.draw.w;
    output.position = mul(u_lit_scene.view_projection, float4(world, 1.0));
    output.world    = world;
    output.normal   = toward;
    output.uv       = float2(u, 0.5 - corner.y * 0.5);
    output.color    = particles_color_of(p);
    output.flipbook = float4(output.uv, 0.0, 0.0);
    return output;
}

[shader("vertex")]
ParticleVertexOutput rbtrend_particles_vert(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    if (u_particles.kind.y == DRAW_RIBBON)
        return ribbon_vertex(vertex, instance);
    uint index   = ParticlesLists.Load((draw_counter(COUNTER_CURRENT) * u_particles.kind.w + instance) * 4u);
    Particle p   = draw_particle(index);
    uint modules = u_particles.kind.x;
    float t      = saturate(p.age / max(p.life, 1.0e-30));
    float size   = particles_size_of(p);
    float4 color = particles_color_of(p);

    float4 rows[3];
    draw_emitter(rows);
    bool emitter   = u_particles.kind.z == SPACE_EMITTER;
    float3 center  = particles_place(p);
    float3 toward, right, up;
    particles_facing(center, toward, right, up);

    float2 corner = CORNERS[vertex % 6u];
    float2 uv     = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    float3 world;
    if (u_particles.kind.y == DRAW_STREAK) {
        float3 velocity = emitter ? particles_turn(rows, p.velocity) : p.velocity;
        float speed     = length(velocity);
        float length_   = clamp(speed * u_particles.draw.z, u_particles.streak.x, u_particles.streak.y);
        float3 axis     = speed > 0.0 ? velocity / speed : up;
        float3 side     = unit_or(cross(axis, toward), right);
        world           = center + axis * (length_ * (corner.y * 0.5 - 0.5)) + side * (corner.x * size * 0.5);
    } else {
        float angle = p.rotation + p.spin * p.age;
        float c     = cos(angle);
        float s     = sin(angle);
        float2 r    = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
        world       = center + (right * r.x + up * r.y) * (size * 0.5);
    }

    float4 next = float4(uv, 0.0, 0.0);
    if ((modules & MODULE_FLIPBOOK) != 0u) {
        uint columns = max(u_particles.flipbook.x, 1u);
        uint rows_   = max(u_particles.flipbook.y, 1u);
        uint frames  = max(u_particles.flipbook.z, 1u);
        float fps    = u_particles.draw.x;
        float at     = fps > 0.0 ? p.age * fps : t * float(frames);
        float whole  = floor(at);
        uint first   = 0u;
        uint second  = 0u;
        if (fps > 0.0) {
            first  = uint(whole) % frames;
            second = (first + 1u) % frames;
        } else {
            first  = min(uint(whole), frames - 1u);
            second = min(first + 1u, frames - 1u);
        }
        next.xy = frame_uv(second, uv, columns, rows_);
        next.z  = u_particles.flipbook.w != 0u ? at - whole : 0.0;
        uv      = frame_uv(first, uv, columns, rows_);
    }

    ParticleVertexOutput output;
    output.position = mul(u_lit_scene.view_projection, float4(world, 1.0));
    output.world    = world;
    output.normal   = toward;
    output.uv       = uv;
    output.tangent  = float4(0.0);
    output.color    = color;
    output.flipbook = next;
    return output;
}
