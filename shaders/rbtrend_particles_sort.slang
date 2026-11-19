module rbtrend_particles_sort;

// The radix sort of a sorted system's alive list (rbtrendparticles.h, "A
// step on the GPU"): AMD's FidelityFX Parallel Sort's reduce-then-scan
// shape, four passes of 4 bits over a 16-bit key, each a count per group of
// 256, one group's scan of the counts, and a stable scatter. Pass 0 reads
// the keys the step wrote (the first key list) and the alive list as the
// values; the passes go back and forth between them and the second lists,
// so pass 3 leaves the sorted values in the alive list itself.

import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;

// rbtrend_particles_sort_push in src/render/rbtrendparticles_internal.hpp:
// x the pass, 0 to 3.
public struct ParticlesSortPass {
    public uint4 pass;
};
[[vk::push_constant]] public ConstantBuffer<ParticlesSortPass> sort_pass : register(b0, space4);

// Where the keys a pass reads (or writes) start, in words.
public uint keys_in() {
    return (sort_pass.pass.x & 1u) == 0u ? 0u : u_particles.kind.w;
}
public uint keys_out() {
    return (sort_pass.pass.x & 1u) == 0u ? u_particles.kind.w : 0u;
}

// Where the values a pass reads (or writes) start: the alive list in the
// lists buffer, or the second values in the sort buffer.
public bool values_in_lists() {
    return (sort_pass.pass.x & 1u) == 0u;
}
public uint second_values() {
    return 2u * u_particles.kind.w;
}

// Where the counts start: after the three lists of the sort buffer.
public uint counts() {
    return 3u * u_particles.kind.w;
}

public uint digit_of(uint key) {
    return (key >> (4u * sort_pass.pass.x)) & 15u;
}
