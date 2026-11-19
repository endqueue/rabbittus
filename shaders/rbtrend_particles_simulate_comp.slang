// A step's simulation (RBTREND_COMMAND_PARTICLES_STEP, rbtrendparticles.h):
// a thread a particle the last step left alive, dispatched indirectly from
// the count the last step wrote. Each is aged, killed at the end of its
// life, or moved (its forces, the drag towards the wind, the noise, the
// anchor's move in WORLD space, the wrap), collided, and appended to this
// step's alive list. A step of 0 seconds moves and ages nothing. Set 0 is
// the step's scene's, declared whole.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;
import rbtrend_particles_step;

groupshared uint s_kept;
groupshared uint s_killed;
groupshared uint s_kept_from;
groupshared uint s_killed_from;

[shader("compute")]
[numthreads(64, 1, 1)]
void rbtrend_particles_simulate_comp(uint3 id : SV_DispatchThreadID, uint3 thread : SV_GroupThreadID) {
    if (thread.x == 0u) {
        s_kept   = 0u;
        s_killed = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    // 0 nothing (past the alive count), 1 kept, 2 killed.
    uint outcome = 0u;
    uint index   = 0u;
    Particle p;
    p.position = float3(0.0);
    p.age      = 0.0;
    p.velocity = float3(0.0);
    p.life     = 0.0;
    p.size     = 0.0;
    p.rotation = 0.0;
    p.spin     = 0.0;
    p.number   = 0u;
    if (id.x < counter(COUNTER_ALIVE)) {
        index        = ParticlesLists.Load((alive_list(counter(COUNTER_CURRENT)) + id.x) * 4u);
        p            = load_particle(index);
        outcome      = 1u;
        float dt     = step.anchor_dt.w;
        uint modules = u_particles.kind.x;
        if (dt > 0.0) {
            p.age += dt;
            if (p.age >= p.life)
                outcome = 2u;
        }
        if (outcome == 1u && dt > 0.0) {
            float3 acceleration = float3(0.0);
            float drag          = 0.0;
            float3 wind         = step.wind_flags.xyz;
            if ((modules & MODULE_FORCES) != 0u) {
                acceleration = u_particles.forces.xyz;
                drag         = u_particles.forces.w;
            }
            if (emitter_space()) {
                // Gravity and the air turned into the emitter's frame, the
                // air as the moving emitter meets it.
                float4 rows[3]   = step.emitter;
                float3x3 inverse = particles_inverse3(rows);
                acceleration     = mul(inverse, acceleration);
                wind             = mul(inverse, wind - step.velocity_spawn.xyz);
            }
            // Semi-implicit Euler: the velocity first, then the place by it.
            p.velocity += acceleration * dt;
            if (drag > 0.0)
                p.velocity = wind + (p.velocity - wind) * exp(-drag * dt);
            float3 motion = p.velocity;
            if ((modules & MODULE_NOISE) != 0u) {
                float time   = asfloat(counter(COUNTER_TIME)) + dt;
                float3 where = p.position * u_particles.noise.y + u_particles.noise_scroll.xyz * time;
                motion += particles_noise.SampleLevel(particles_noise_sampler, where, 0.0).xyz * u_particles.noise.x;
            }
            p.position += motion * dt;
        }
        // The anchor's move in WORLD space, and the wrap, even for a step of
        // 0 seconds: a particle held still stays where it is in the world.
        if (outcome == 1u) {
            if (!emitter_space())
                p.position -= step.moved_seed.xyz;
            if ((modules & MODULE_WRAP) != 0u) {
                float3 h   = u_particles.wrap.xyz;
                p.position = p.position - 2.0 * h * floor((p.position + h) / (2.0 * h));
            }
            if (dt > 0.0 && (modules & MODULE_COLLISION) != 0u && !collide(p))
                outcome = 2u;
        }
    }
    uint mine = 0u;
    if (outcome == 1u)
        InterlockedAdd(s_kept, 1u, mine);
    else if (outcome == 2u)
        InterlockedAdd(s_killed, 1u, mine);
    GroupMemoryBarrierWithGroupSync();
    if (thread.x == 0u) {
        uint from = 0u;
        if (s_kept > 0u)
            ParticlesCounters.InterlockedAdd(COUNTER_APPENDED * 4u, s_kept, from);
        s_kept_from = from;
        from        = 0u;
        if (s_killed > 0u)
            ParticlesCounters.InterlockedAdd(COUNTER_DEAD * 4u, s_killed, from);
        s_killed_from = from;
    }
    GroupMemoryBarrierWithGroupSync();
    if (outcome == 1u)
        append_at(index, p, s_kept_from + mine);
    else if (outcome == 2u) {
        ParticlesLists.Store((dead_list() + s_killed_from + mine) * 4u, index);
        // A ribbon's end: its life marked ended, so the draw sees it dead.
        if (u_particles.kind.y == DRAW_RIBBON) {
            p.life = -1.0;
            store_particle(index, p);
        }
    }
}
