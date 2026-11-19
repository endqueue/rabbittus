// A step's spawn (RBTREND_COMMAND_PARTICLES_STEP, rbtrendparticles.h): a
// thread a particle the step asks for, each taking a dead slot (with none
// left, it spawns nothing; a group takes its slots together) and starting
// by the SPAWN module and
// random values hashed from the step's seed and its place in the spawn, so
// what it starts as doesn't depend on its slot; appended to this step's
// alive list after the simulation's. Set 0 is the step's scene's, declared
// whole.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;
import rbtrend_particles_step;

static const float PI = 3.14159265359;

// A unit direction within [spread] radians of [n] (unit), from two random
// values: uniform over the cap's solid angle.
float3 spread_from(float3 n, float spread, float u, float v) {
    float c   = 1.0 - u * (1.0 - cos(spread));
    float s   = sqrt(max(1.0 - c * c, 0.0));
    float phi = 2.0 * PI * v;
    float3 t  = abs(n.y) < 0.999 ? normalize(cross(float3(0.0, 1.0, 0.0), n)) : float3(1.0, 0.0, 0.0);
    float3 b  = cross(n, t);
    return normalize(t * (s * cos(phi)) + b * (s * sin(phi)) + n * c);
}

groupshared uint s_wanted;
groupshared int s_dead_before;
groupshared uint s_taken;
groupshared uint s_appended_from;
groupshared uint s_number_from;

[shader("compute")]
[numthreads(64, 1, 1)]
void rbtrend_particles_spawn_comp(uint3 id : SV_DispatchThreadID, uint3 thread : SV_GroupThreadID) {
    if (thread.x == 0u)
        s_wanted = 0u;
    GroupMemoryBarrierWithGroupSync();
    uint count = asuint(step.velocity_spawn.w);
    uint mine  = 0u;
    if (id.x < count)
        InterlockedAdd(s_wanted, 1u, mine);
    GroupMemoryBarrierWithGroupSync();
    // The group's dead slots, one atomic for all of them: the count goes
    // below 0 only while groups that found too few put back what they
    // took, so the slots a group sees are its own (the top ones from where
    // the count was).
    if (thread.x == 0u) {
        uint before = 0u;
        ParticlesCounters.InterlockedAdd(COUNTER_DEAD * 4u, 0u - s_wanted, before);
        int available = clamp(int(before), 0, int(s_wanted));
        uint ignored  = 0u;
        if (uint(available) < s_wanted)
            ParticlesCounters.InterlockedAdd(COUNTER_DEAD * 4u, s_wanted - uint(available), ignored);
        s_dead_before = int(before);
        s_taken       = uint(available);
        uint from     = 0u;
        if (available > 0) {
            ParticlesCounters.InterlockedAdd(COUNTER_APPENDED * 4u, uint(available), from);
            s_appended_from = from;
            from            = 0u;
            ParticlesCounters.InterlockedAdd(COUNTER_SPAWNED * 4u, uint(available), from);
            s_number_from = from;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (id.x >= count || mine >= s_taken)
        return;
    uint index = ParticlesLists.Load((dead_list() + uint(s_dead_before) - 1u - mine) * 4u);

    uint seed = asuint(step.moved_seed.w);
    uint n    = id.x;
    Particle p;
    p.age      = 0.0;
    p.life     = lerp(u_particles.life_size.x, u_particles.life_size.y, particles_random(seed, n, 0u));
    p.size     = lerp(u_particles.life_size.z, u_particles.life_size.w, particles_random(seed, n, 1u));
    p.rotation = lerp(u_particles.rotation.x, u_particles.rotation.y, particles_random(seed, n, 2u));
    p.spin     = lerp(u_particles.rotation.z, u_particles.rotation.w, particles_random(seed, n, 3u));
    // Its number in the system's spawn order since the last clear: its
    // group's numbers are taken together.
    p.number   = s_number_from + mine;

    float3 local     = float3(0.0);
    float3 direction = float3(0.0, 1.0, 0.0);
    float speed      = 0.0;
    float inherit    = 0.0;
    if ((u_particles.kind.x & MODULE_SPAWN) != 0u) {
        float3 size  = u_particles.spawn_size.xyz;
        bool surface = u_particles.options.y != 0u;
        float r0     = particles_random(seed, n, 4u);
        float r1     = particles_random(seed, n, 5u);
        float r2     = particles_random(seed, n, 6u);
        switch (u_particles.options.x) {
        case SHAPE_SPHERE: {
            float z   = 1.0 - 2.0 * r0;
            float phi = 2.0 * PI * r1;
            float s   = sqrt(max(1.0 - z * z, 0.0));
            direction = float3(s * cos(phi), z, s * sin(phi));
            local     = direction * (surface ? size.x : size.x * pow(r2, 1.0 / 3.0));
            break;
        }
        case SHAPE_BOX: {
            local = (float3(r0, r1, r2) * 2.0 - 1.0) * size;
            if (surface) {
                // A face by its area, then its side.
                float3 areas = float3(size.y * size.z, size.x * size.z, size.x * size.y);
                float pick   = particles_random(seed, n, 10u) * (areas.x + areas.y + areas.z);
                float side   = particles_random(seed, n, 11u) < 0.5 ? -1.0 : 1.0;
                if (pick < areas.x)
                    local.x = side * size.x;
                else if (pick < areas.x + areas.y)
                    local.y = side * size.y;
                else
                    local.z = side * size.z;
            }
            break;
        }
        case SHAPE_CONE: {
            float phi    = 2.0 * PI * r1;
            float height = size.y * (surface ? sqrt(r0) : pow(r0, 1.0 / 3.0));
            float across = size.y > 0.0 ? size.x * height / size.y : size.x;
            float radius = surface ? across : across * sqrt(r2);
            local        = float3(radius * cos(phi), height, radius * sin(phi));
            direction    = normalize(float3(size.x * cos(phi), max(size.y, 1.0e-30), size.x * sin(phi)));
            break;
        }
        case SHAPE_DISC: {
            float phi    = 2.0 * PI * r1;
            float radius = surface ? size.x : size.x * sqrt(r0);
            local        = float3(radius * cos(phi), 0.0, radius * sin(phi));
            break;
        }
        default:
            break;
        }
        if (u_particles.spawn_size.w > 0.0)
            direction = spread_from(direction, u_particles.spawn_size.w, particles_random(seed, n, 7u), particles_random(seed, n, 8u));
        speed   = lerp(u_particles.spawn_speed.x, u_particles.spawn_speed.y, particles_random(seed, n, 9u));
        inherit = u_particles.spawn_speed.z;
    }
    if (emitter_space()) {
        // Kept in the emitter's frame: it moves and turns with it.
        p.position = local;
        p.velocity = direction * speed;
    } else {
        float4 rows[3] = step.emitter;
        float3 turned  = particles_turn(rows, direction);
        p.position     = particles_transform(rows, local);
        p.velocity     = (dot(turned, turned) > 0.0 ? normalize(turned) : direction) * speed + step.velocity_spawn.xyz * inherit;
    }
    append_at(index, p, s_appended_from + mine);
}
