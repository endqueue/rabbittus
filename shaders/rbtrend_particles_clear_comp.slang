// A step's clear (RBTREND_COMMAND_PARTICLES_STEP with `_clear`, and every
// system's first step): every slot on the dead list, nothing alive, the
// record and the arguments as for an empty system (the spawn count kept,
// but on the first step, and a ribbon starting from it). A thread a slot.

import rbtrend_particles_common;
import rbtrend_particles_control_bindings;

// rbtrend_particles_step_push in src/render/rbtrendparticles_internal.hpp:
// read for its flags.
[[vk::push_constant]] ConstantBuffer<ParticlesStep> step : register(b0, space4);

[shader("compute")]
[numthreads(64, 1, 1)]
void rbtrend_particles_clear_comp(uint3 id : SV_DispatchThreadID) {
    uint capacity = u_particles.kind.w;
    if (id.x < capacity)
        ParticlesLists.Store((2u * capacity + id.x) * 4u, capacity - 1u - id.x);
    if (id.x != 0u)
        return;
    // The spawn count goes on from where it was but on the first step
    // ever, and a ribbon starts at it.
    uint spawned = (asuint(step.wind_flags.w) & STEP_FIRST) != 0u ? 0u : ParticlesCounters.Load(COUNTER_SPAWNED * 4u);
    for (uint w = 0u; w < COUNTER_WORDS; ++w)
        ParticlesCounters.Store(w * 4u, 0u);
    ParticlesCounters.Store(COUNTER_DEAD * 4u, capacity);
    ParticlesCounters.Store(COUNTER_SPAWNED * 4u, spawned);
    ParticlesCounters.Store(COUNTER_RIBBON_FIRST * 4u, spawned);
    for (uint w = 0u; w < ARGS_WORDS; ++w)
        ParticlesArguments.Store(w * 4u, 0u);
    // A word at a time: stores of one width to a buffer (SPIR-V declares a
    // byte address buffer once for each width a shader stores).
    ParticlesArguments.Store((ARGS_SIMULATE + 1u) * 4u, 1u);
    ParticlesArguments.Store((ARGS_SIMULATE + 2u) * 4u, 1u);
    ParticlesArguments.Store(ARGS_DRAW * 4u, 6u);
    ParticlesArguments.Store((ARGS_SORT + 1u) * 4u, 1u);
    ParticlesArguments.Store((ARGS_SORT + 2u) * 4u, 1u);
}
