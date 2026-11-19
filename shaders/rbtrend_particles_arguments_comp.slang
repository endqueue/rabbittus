// A step's arguments (RBTREND_COMMAND_PARTICLES_STEP): one thread a system,
// after its simulation and spawn. This step's alive list becomes the
// current one; the record takes its count, the step's anchor and emitter
// (what the draw places the particles by) and its time; the arguments the
// next simulation's dispatch, the count read back, this step's draw and
// the sort's groups.

import rbtrend_particles_common;
import rbtrend_particles_control_bindings;

// rbtrend_particles_step_push in src/render/rbtrendparticles_internal.hpp.
[[vk::push_constant]] ConstantBuffer<ParticlesStep> step : register(b0, space4);

[shader("compute")]
[numthreads(1, 1, 1)]
void rbtrend_particles_arguments_comp(uint3 id : SV_DispatchThreadID) {
    uint alive   = ParticlesCounters.Load(COUNTER_APPENDED * 4u);
    uint current = ParticlesCounters.Load(COUNTER_CURRENT * 4u) ^ 1u;
    uint groups  = (alive + SORT_GROUP - 1u) / SORT_GROUP;
    ParticlesCounters.Store(COUNTER_ALIVE * 4u, alive);
    ParticlesCounters.Store(COUNTER_CURRENT * 4u, current);
    ParticlesCounters.Store(COUNTER_APPENDED * 4u, 0u);
    ParticlesCounters.Store(COUNTER_GROUPS * 4u, groups);
    ParticlesCounters.Store(COUNTER_TIME * 4u, asuint(asfloat(ParticlesCounters.Load(COUNTER_TIME * 4u)) + step.anchor_dt.w));
    // A word at a time: stores of one width to a buffer (SPIR-V declares a
    // byte address buffer once for each width a shader stores).
    for (uint c = 0u; c < 3u; ++c)
        ParticlesCounters.Store((COUNTER_ANCHOR + c) * 4u, asuint(step.anchor_dt[c]));
    ParticlesCounters.Store((COUNTER_ANCHOR + 3u) * 4u, 0u);
    for (uint r = 0u; r < 3u; ++r)
        for (uint c = 0u; c < 4u; ++c)
            ParticlesCounters.Store((COUNTER_EMITTER + 4u * r + c) * 4u, asuint(step.emitter[r][c]));
    // A ribbon draws a segment between each number spawned since the last
    // clear, at most the capacity back, and the one before it.
    uint drawn = alive;
    if (u_particles.kind.y == DRAW_RIBBON) {
        uint spawned = ParticlesCounters.Load(COUNTER_SPAWNED * 4u);
        uint window  = min(spawned - ParticlesCounters.Load(COUNTER_RIBBON_FIRST * 4u), u_particles.kind.w);
        drawn        = window > 1u ? window - 1u : 0u;
    }
    uint words[ARGS_WORDS] = {(alive + STEP_GROUP - 1u) / STEP_GROUP, 1u, 1u, alive, 6u, drawn, 0u, 0u, groups, 1u, 1u, 0u,
                              u_particles.mesh.x, alive, u_particles.mesh.y, u_particles.mesh.z, 0u, 0u, 0u, 0u};
    for (uint w = 0u; w < ARGS_WORDS; ++w)
        ParticlesArguments.Store(w * 4u, words[w]);
}
