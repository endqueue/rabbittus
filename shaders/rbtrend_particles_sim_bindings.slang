module rbtrend_particles_sim_bindings;

// Set 1 of a step's simulation, spawn and sort (rbtrendparticles.h, "A step
// on the GPU"): a system's own, made with it (its sim material). Set 0 is
// the step's scene's (rbtrend_lit_bindings.slang). Every buffer raw, as
// Carrottus GPU's D3D12 backend views every storage buffer.

import rbtrend_particles_common;

[[vk::binding(0, 1)]] public ConstantBuffer<RbtrendParticles> u_particles : register(b0, space1);
// The particles, 48 bytes each (Particle).
[[vk::binding(1, 1)]] public RWByteAddressBuffer ParticlesState : register(u1, space1);
// The two alive lists and the dead list, `capacity` words each.
[[vk::binding(2, 1)]] public RWByteAddressBuffer ParticlesLists : register(u2, space1);
// The system's record (COUNTER_*).
[[vk::binding(3, 1)]] public RWByteAddressBuffer ParticlesCounters : register(u3, space1);
// The sort's keys (two lists) and values (one; the other is the alive
// list), `capacity` words each, then its counts, 16 a group of 256.
[[vk::binding(4, 1)]] public RWByteAddressBuffer ParticlesSort : register(u4, space1);
// The NOISE module's volume (rbtrend_particles_set_noise()), read
// repeating and linear; a placeholder while there is none.
[[vk::binding(5, 1)]] public Texture3D<float4> particles_noise : register(t5, space1);
[[vk::binding(6, 1)]] public SamplerState particles_noise_sampler : register(s6, space1);
// The COLLISION module's depth (rbtrend_particles_set_depth()), read by
// its texels; a placeholder while there is none.
[[vk::binding(7, 1)]] public Texture2D<float4> particles_depth : register(t7, space1);

public uint counter(uint word) {
    return ParticlesCounters.Load(word * 4u);
}

public Particle load_particle(uint index) {
    uint base = index * 48u;
    return particle_from_words(ParticlesState.Load4(base), ParticlesState.Load4(base + 16u), ParticlesState.Load4(base + 32u));
}

public void store_particle(uint index, Particle p) {
    uint base = index * 48u;
    ParticlesState.Store4(base, uint4(asuint(p.position), asuint(p.age)));
    ParticlesState.Store4(base + 16u, uint4(asuint(p.velocity), asuint(p.life)));
    ParticlesState.Store4(base + 32u, uint4(asuint(p.size), asuint(p.rotation), asuint(p.spin), p.number));
}

// Where alive list [which]'s first word is, in words.
public uint alive_list(uint which) {
    return which * u_particles.kind.w;
}

// Where the dead list's first word is, in words.
public uint dead_list() {
    return 2u * u_particles.kind.w;
}
