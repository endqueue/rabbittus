// Rend's particle vertex shader for meshes (rbtrendparticles.h,
// RBTREND_PARTICLES_SHADER_MESH_VERTEX): a mesh of RBTREND_LIT_VERTEX at
// vertex buffer slot 0 (rbtrend_get_lit_vertex_layout(), locations 0 to 3),
// drawn once a particle of the system's current alive list (an instance a
// particle, sorted where its effect sorts) from the MESH module's index
// range: scaled by the particle's size (times its size over life), turned
// about its space's z by its rotation and spin, placed at the particle by
// the system's last step's anchor (and in EMITTER space turned and scaled
// by the emitter). Writes locations 0 to 4 as a lit vertex shader does (the
// place, the mesh's normal and tangent turned with it, its texture
// coordinate at the flipbook's frame, the colour over life) and the
// flipbook's next frame at 5.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_draw_bindings;
import rbtrend_particles_draw;

struct ParticleMeshVertex {
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 tangent : TEXCOORD3;
};

// [v] turned about z by the angle whose cosine and sine are [c] and [s].
float3 turn_z(float3 v, float c, float s) {
    return float3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

[shader("vertex")]
ParticleVertexOutput rbtrend_particles_mesh_vert(ParticleMeshVertex input, uint instance : SV_InstanceID) {
    uint index   = ParticlesLists.Load((draw_counter(COUNTER_CURRENT) * u_particles.kind.w + instance) * 4u);
    Particle p   = draw_particle(index);
    uint modules = u_particles.kind.x;
    float t      = saturate(p.age / max(p.life, 1.0e-30));
    float size   = p.size;
    if ((modules & MODULE_SIZE_OVER_LIFE) != 0u)
        size *= particles_size_curve(u_particles.size_times, u_particles.size_values, u_particles.curves.y, t);
    float4 color = u_particles.color;
    if ((modules & MODULE_COLOR_OVER_LIFE) != 0u)
        color *= particles_curve(u_particles.color_times, u_particles.color_values, u_particles.curves.x, t);

    float4 rows[3];
    draw_emitter(rows);
    bool emitter  = u_particles.kind.z == SPACE_EMITTER;
    float angle   = p.rotation + p.spin * p.age;
    float c       = cos(angle);
    float s       = sin(angle);
    float3 local  = turn_z(input.position, c, s) * size;
    float3 normal = turn_z(input.normal, c, s);
    float3 tangent = turn_z(input.tangent.xyz, c, s);
    float3 center = draw_anchor() + (emitter ? particles_transform(rows, p.position) : p.position);
    float3 world  = center + (emitter ? particles_turn(rows, local) : local);
    float handed  = input.tangent.w;
    if (emitter) {
        // The emitter's cofactor matrix carries normals under any scale; a
        // mirror turns them, and the tangent's handedness, back.
        float3 a     = rows[0].xyz;
        float3 b     = rows[1].xyz;
        float3 d     = rows[2].xyz;
        float3 c0    = float3(a.x, b.x, d.x);
        float3 c1    = float3(a.y, b.y, d.y);
        float3 c2    = float3(a.z, b.z, d.z);
        float3 n2    = cross(c1, c2) * normal.x + cross(c2, c0) * normal.y + cross(c0, c1) * normal.z;
        float mirror = dot(c0, cross(c1, c2)) < 0.0 ? -1.0 : 1.0;
        normal       = n2 * mirror;
        tangent      = particles_turn(rows, tangent);
        handed *= mirror;
    }

    float2 uv   = input.uv;
    float4 next = float4(uv, 0.0, 0.0);
    if ((modules & MODULE_FLIPBOOK) != 0u) {
        uint columns = max(u_particles.flipbook.x, 1u);
        uint rows_   = max(u_particles.flipbook.y, 1u);
        uint frames  = max(u_particles.flipbook.z, 1u);
        float fps    = u_particles.draw.x;
        float at     = fps > 0.0 ? p.age * fps : t * float(frames);
        float whole  = floor(at);
        uint first   = fps > 0.0 ? uint(whole) % frames : min(uint(whole), frames - 1u);
        uint second  = fps > 0.0 ? (first + 1u) % frames : min(first + 1u, frames - 1u);
        float2 cell  = frac(uv);
        next.xy      = (float2(float(second % columns), float(second / columns)) + cell) / float2(float(columns), float(rows_));
        next.z       = u_particles.flipbook.w != 0u ? at - whole : 0.0;
        uv           = (float2(float(first % columns), float(first / columns)) + cell) / float2(float(columns), float(rows_));
    }

    ParticleVertexOutput output;
    output.position = mul(u_lit_scene.view_projection, float4(world, 1.0));
    output.world    = world;
    output.normal   = normal;
    output.uv       = uv;
    output.tangent  = float4(tangent, handed);
    output.color    = color;
    output.flipbook = next;
    return output;
}
