module rbtrend_particles_control_bindings;

// Set 0 of a step's clear and arguments (rbtrendparticles.h, "A step on
// the GPU"): a system's own, made with it (its control material). Every
// buffer raw, as Carrottus GPU's D3D12 backend views every storage buffer.

import rbtrend_particles_common;

[[vk::binding(0, 0)]] public ConstantBuffer<RbtrendParticles> u_particles : register(b0, space0);
// The two alive lists and the dead list, `capacity` words each.
[[vk::binding(1, 0)]] public RWByteAddressBuffer ParticlesLists : register(u1, space0);
// The system's record (COUNTER_*).
[[vk::binding(2, 0)]] public RWByteAddressBuffer ParticlesCounters : register(u2, space0);
// The arguments (ARGS_*): read by the indirect dispatches and draws.
[[vk::binding(3, 0)]] public RWByteAddressBuffer ParticlesArguments : register(u3, space0);
