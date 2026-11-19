// A sort pass's count (rbtrend_particles_sort.slang): each group of 256 of
// the alive list counts its keys' digits; its 16 counts written digit by
// digit, a row of all the groups for each digit.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;
import rbtrend_particles_sort;

groupshared uint s_counts[16];

[shader("compute")]
[numthreads(256, 1, 1)]
void rbtrend_particles_sort_count_comp(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    if (thread.x < 16u)
        s_counts[thread.x] = 0u;
    GroupMemoryBarrierWithGroupSync();
    uint i = group.x * SORT_GROUP + thread.x;
    if (i < counter(COUNTER_ALIVE))
        InterlockedAdd(s_counts[digit_of(ParticlesSort.Load((keys_in() + i) * 4u))], 1u);
    GroupMemoryBarrierWithGroupSync();
    if (thread.x < 16u)
        ParticlesSort.Store((counts() + thread.x * counter(COUNTER_GROUPS) + group.x) * 4u, s_counts[thread.x]);
}
