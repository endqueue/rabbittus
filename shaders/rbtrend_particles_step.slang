module rbtrend_particles_step;

// What a step's simulation and spawn share (rbtrendparticles.h, "A step on
// the GPU"): the step's values as push constants, and a particle's end of
// the stage, appended to this step's alive list (with its sort key for a
// sorted effect, or its place in a ribbon's table) at the place its group
// counted for it; and the collision
// with the system's depth through the step's scene's view. A group counts
// its appends (and its kills, and a spawn its slots) in its own memory and
// takes them from the system's record with one atomic, so a stage costs
// an atomic on the record a group, not a particle.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;

// rbtrend_particles_step_push in src/render/rbtrendparticles_internal.hpp.
[[vk::push_constant]] public ConstantBuffer<ParticlesStep> step : register(b0, space4);

public bool emitter_space() {
    return u_particles.kind.z == SPACE_EMITTER;
}

// Where [p] (a particle's place, in its space) is in the scene's space.
public float3 scene_place(float3 p) {
    float4 rows[3] = step.emitter;
    return step.anchor_dt.xyz + (emitter_space() ? particles_transform(rows, p) : p);
}

// [p] written to slot [index], and [index] put at place [at] of this
// step's alive list (the one the last step didn't fill), with its key for a
// sorted effect. [at] is the place an append counted on
// COUNTER_APPENDED gave it.
public void append_at(uint index, Particle p, uint at) {
    store_particle(index, p);
    ParticlesLists.Store((alive_list(counter(COUNTER_CURRENT) ^ 1u) + at) * 4u, index);
    if (u_particles.options.z != 0u)
        ParticlesSort.Store(at * 4u, particles_key(length(scene_place(p.position) - u_lit_scene.eye_position.xyz)));
    if (u_particles.kind.y == DRAW_RIBBON) {
        uint entry = ribbon_entry(p.number, u_particles.kind.w);
        ParticlesSort.Store(entry * 4u, p.number);
        ParticlesSort.Store((entry + 1u) * 4u, index);
    }
}

// The scene's place at [ndc] (x and y) and depth [d].
float3 unproject(float2 ndc, float d) {
    float4 q = mul(u_lit_scene.inverse_view_projection, float4(ndc, d, 1.0));
    return q.xyz / q.w;
}

// The view ray through [ndc], unit, away from the eye: between its points
// at depths 1 and 0 taken homogeneously, so a far plane at infinity gives
// a direction, and an orthographic view its one direction.
float3 view_ray(float2 ndc) {
    float4 a = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 b = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    float3 d = normalize(b.xyz * a.w - a.xyz * b.w);
    if (dot(d, u_lit_scene.view_direction.xyz) < 0.0)
        d = -d;
    return d;
}

// Whether depth [d] holds a surface: anything but the far plane.
bool depth_holds(float d) {
    return u_particles.options.w != 0u ? d > 0.0 : d < 1.0;
}

// The surface's place at [texel] of a depth [size], or false where there is
// none.
bool surface_at(int2 texel, uint2 size, out float3 place) {
    place = float3(0.0);
    float d = particles_depth.Load(int3(texel, 0)).r;
    if (!depth_holds(d))
        return false;
    float2 uv  = (float2(texel) + 0.5) / float2(size);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    place      = unproject(ndc, d);
    return true;
}

// COLLISION: [p] (its place and velocity in its space) against the
// system's depth: a particle behind the surface its pixel holds, within the
// thickness, is put back on it (a thousandth of the thickness in front),
// its velocity into the surface reflected by the restitution and along it
// slowed by the friction, its age raised by the life loss's share of its
// life. False if that kills it.
public bool collide(inout Particle p) {
    float3 s     = scene_place(p.position);
    float4 clip  = mul(u_lit_scene.view_projection, float4(s, 1.0));
    if (clip.w <= 0.0)
        return true;
    float3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0)
        return true;
    uint width = 0u, height = 0u;
    particles_depth.GetDimensions(width, height);
    uint2 size  = uint2(width, height);
    float2 uv   = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    int2 texel  = clamp(int2(uv * float2(size)), int2(0), int2(size) - 1);
    float d     = particles_depth.Load(int3(texel, 0)).r;
    if (!depth_holds(d))
        return true;
    float3 surface = unproject(ndc.xy, d);
    float3 ray     = view_ray(ndc.xy);
    float behind   = dot(s - surface, ray);
    float thickness = u_particles.collision.w;
    if (behind <= 0.0 || behind > thickness)
        return true;
    // The surface's normal from its neighbours' places, towards the eye.
    int2 dx = int2(texel.x + 1 < int(size.x) ? 1 : -1, 0);
    int2 dy = int2(0, texel.y + 1 < int(size.y) ? 1 : -1);
    float3 here = float3(0.0), right = float3(0.0), down = float3(0.0);
    float3 n = -ray;
    if (surface_at(texel, size, here) && surface_at(texel + dx, size, right) && surface_at(texel + dy, size, down)) {
        float3 c = cross(right - here, down - here);
        if (dot(c, c) > 0.0)
            n = normalize(c);
    }
    if (dot(n, ray) > 0.0)
        n = -n;
    float4 rows[3] = step.emitter;
    float3 v = emitter_space() ? particles_turn(rows, p.velocity) : p.velocity;
    float vn = dot(v, n);
    if (vn < 0.0)
        v = (v - vn * n) * (1.0 - u_particles.collision.y) - vn * u_particles.collision.x * n;
    s = surface - ray * (thickness * 1.0e-3);
    if (emitter_space()) {
        float3x3 inverse = particles_inverse3(rows);
        p.position       = mul(inverse, s - step.anchor_dt.xyz - float3(rows[0].w, rows[1].w, rows[2].w));
        p.velocity       = mul(inverse, v);
    } else {
        p.position = s - step.anchor_dt.xyz;
        p.velocity = v;
    }
    p.age += u_particles.collision.z * p.life;
    return p.age < p.life;
}
