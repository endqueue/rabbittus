// A sort pass's scatter (rbtrend_particles_sort.slang): each group of 256
// puts each key and value where its digit's run starts for the group, plus
// how many before it in the group share its digit, so the pass is stable:
// that rank from an inclusive scan across the group of sixteen 16-bit
// counters, two to a word.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_particles_common;
import rbtrend_particles_sim_bindings;
import rbtrend_particles_sort;

groupshared uint s_ranks[8 * SORT_GROUP];

[shader("compute")]
[numthreads(256, 1, 1)]
void rbtrend_particles_sort_scatter_comp(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    uint i       = group.x * SORT_GROUP + thread.x;
    bool valid   = i < counter(COUNTER_ALIVE);
    uint values  = alive_list(counter(COUNTER_CURRENT));
    uint key     = 0u;
    uint value   = 0u;
    uint digit   = 0u;
    if (valid) {
        key   = ParticlesSort.Load((keys_in() + i) * 4u);
        value = values_in_lists() ? ParticlesLists.Load((values + i) * 4u) : ParticlesSort.Load((second_values() + i) * 4u);
        digit = digit_of(key);
    }
    uint word  = digit / 2u;
    uint shift = (digit & 1u) * 16u;
    for (uint w = 0u; w < 8u; ++w)
        s_ranks[w * SORT_GROUP + thread.x] = valid && w == word ? (1u << shift) : 0u;
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 1u; offset < SORT_GROUP; offset *= 2u) {
        uint before[8];
        for (uint w = 0u; w < 8u; ++w)
            before[w] = thread.x >= offset ? s_ranks[w * SORT_GROUP + thread.x - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        for (uint w = 0u; w < 8u; ++w)
            s_ranks[w * SORT_GROUP + thread.x] += before[w];
        GroupMemoryBarrierWithGroupSync();
    }
    if (!valid)
        return;
    uint rank = ((s_ranks[word * SORT_GROUP + thread.x] >> shift) & 0xFFFFu) - 1u;
    uint to   = ParticlesSort.Load((counts() + digit * counter(COUNTER_GROUPS) + group.x) * 4u) + rank;
    ParticlesSort.Store((keys_out() + to) * 4u, key);
    if (values_in_lists())
        ParticlesSort.Store((second_values() + to) * 4u, value);
    else
        ParticlesLists.Store((values + to) * 4u, value);
}
