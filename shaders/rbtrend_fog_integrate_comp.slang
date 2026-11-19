// The froxel grid's integration (RBTREND_COMMAND_LIT_FOG, rbtrendfog.h): a
// thread a column, front to back from the eye, Hillaire's energy-conserving
// step over each slice's length l (the first from the eye): with the
// slice's extinction s and scattered light S, light += T S (1 - exp(-s l))
// / s, T *= exp(-s l); each slice's far side written as the light gathered
// (RGB) and the transmittance (A).

import rbtrend_fog_grid;

[[vk::binding(0, 0)]] Texture3D<float4> fog_media : register(t0, space0);
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> fog_integrated : register(u1, space0);

// rbtrend_fog_integrate_params in src/render/rbtrendfog_internal.hpp.
struct FogIntegrateParams {
    uint4 grid;   // x W, y H, z S
    float4 range; // x near, y far, z ln(far / near), w S
};
[[vk::push_constant]] ConstantBuffer<FogIntegrateParams> params : register(b0, space4);

[shader("compute")]
[numthreads(8, 8, 1)]
void rbtrend_fog_integrate_comp(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.grid.x || id.y >= params.grid.y)
        return;
    float t      = 1.0;
    float3 light = float3(0.0);
    float before = 0.0;
    for (uint i = 0u; i < params.grid.z; ++i) {
        float after      = fog_slice_distance(float(i + 1u), params.range.x, params.range.z, params.range.w);
        float l          = after - before;
        before           = after;
        float4 v         = fog_media.Load(int4(int(id.x), int(id.y), int(i), 0));
        float extinction = max(v.a, 0.0);
        float through    = exp(-extinction * l);
        float3 gathered  = extinction > 1e-7 ? v.rgb * ((1.0 - through) / extinction) : v.rgb * l;
        light += t * gathered;
        t *= through;
        fog_integrated[uint3(id.x, id.y, i)] = float4(light, t);
    }
}
