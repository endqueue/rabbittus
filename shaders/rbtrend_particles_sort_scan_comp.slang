// A sort pass's scan (rbtrend_particles_sort.slang): one group turns the
// counts (16 rows of the groups) into where each group's digit starts, an
// exclusive sum in place: each thread a run of the counts, then the runs'
// sums scanned across the group.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;
import rbtrend_particles_sort;

groupshared uint s_sums[SORT_GROUP];

[shader("compute")]
[numthreads(256, 1, 1)]
void rbtrend_particles_sort_scan_comp(uint3 thread : SV_GroupThreadID) {
    uint total = 16u * counter(COUNTER_GROUPS);
    uint run   = (total + SORT_GROUP - 1u) / SORT_GROUP;
    uint first = thread.x * run;
    uint last  = min(first + run, total);
    uint sum   = 0u;
    for (uint i = first; i < last; ++i)
        sum += ParticlesSort.Load((counts() + i) * 4u);
    s_sums[thread.x] = sum;
    GroupMemoryBarrierWithGroupSync();
    // Hillis and Steele's inclusive scan of the runs' sums.
    for (uint offset = 1u; offset < SORT_GROUP; offset *= 2u) {
        uint before = thread.x >= offset ? s_sums[thread.x - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        s_sums[thread.x] += before;
        GroupMemoryBarrierWithGroupSync();
    }
    uint at = s_sums[thread.x] - sum;
    for (uint i = first; i < last; ++i) {
        uint count = ParticlesSort.Load((counts() + i) * 4u);
        ParticlesSort.Store((counts() + i) * 4u, at);
        at += count;
    }
}
