module rbtrend_particles_draw_bindings;

// Set RBTREND_PARTICLES_SET (2) of a particle pipeline (rbtrendparticles.h,
// "Drawing"): a system's own (its draw material), bound by
// RBTREND_COMMAND_DRAW_PARTICLES. Sets 0 and 1 are the lit scene's and the
// lit material's. The buffers are read only here, and declared `readonly`
// (NonWritable in the SPIR-V), which Vulkan requires of a vertex or
// fragment shader's storage buffer on a device without its stores and
// atomics; raw, as Carrottus GPU's D3D12 backend views every storage buffer.

import rbtrend_particles_common;

[[vk::binding(0, 2)]] public ConstantBuffer<RbtrendParticles> u_particles : register(b0, space2);
[[vk::binding(1, 2)]] public readonly RWByteAddressBuffer ParticlesState : register(u1, space2);
[[vk::binding(2, 2)]] public readonly RWByteAddressBuffer ParticlesLists : register(u2, space2);
[[vk::binding(3, 2)]] public readonly RWByteAddressBuffer ParticlesCounters : register(u3, space2);
// The SOFT module's depth (rbtrend_particles_set_depth()) for an effect
// with SOFT, read by its texels; a placeholder otherwise.
[[vk::binding(4, 2)]] public Texture2D<float4> particles_draw_depth : register(t4, space2);
// The sort buffer, whose key lists hold a ribbon's table (ribbon_entry()).
[[vk::binding(5, 2)]] public readonly RWByteAddressBuffer ParticlesSort : register(u5, space2);

public uint draw_counter(uint word) {
    return ParticlesCounters.Load(word * 4u);
}

public Particle draw_particle(uint index) {
    uint base = index * 48u;
    return particle_from_words(ParticlesState.Load4(base), ParticlesState.Load4(base + 16u), ParticlesState.Load4(base + 32u));
}

// Particle [number] of a ribbon into [p], if it is alive: its table's entry
// holds the number, the slot it names still holds that number, and its
// life is not ended (a particle killed is marked so); and it was spawned
// since the last clear and not before [capacity] spawns ago.
public bool ribbon_particle(uint number, out Particle p) {
    p                = draw_particle(0u);
    uint capacity    = u_particles.kind.w;
    uint first       = draw_counter(COUNTER_RIBBON_FIRST);
    uint spawned     = draw_counter(COUNTER_SPAWNED);
    if (number - first >= spawned - first || spawned - number > capacity)
        return false;
    uint entry = ribbon_entry(number, capacity);
    if (ParticlesSort.Load(entry * 4u) != number)
        return false;
    p = draw_particle(ParticlesSort.Load((entry + 1u) * 4u));
    return p.number == number && p.life > 0.0;
}

// The last step's anchor and emitter rows, from the system's record.
public float3 draw_anchor() {
    return asfloat(ParticlesCounters.Load3(COUNTER_ANCHOR * 4u));
}

public void draw_emitter(out float4 rows[3]) {
    for (uint r = 0u; r < 3u; ++r)
        rows[r] = asfloat(ParticlesCounters.Load4((COUNTER_EMITTER + 4u * r) * 4u));
}
