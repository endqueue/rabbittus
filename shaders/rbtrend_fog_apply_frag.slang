// The froxel grid put over the picture (RBTREND_COMMAND_LIT_FOG,
// rbtrendfog.h): each texel's distance from its depth through the scene's
// inverse view-projection (a texel at the depth's far value at the grid's
// far distance, and none past it), the integrated grid read there
// (trilinear: its slice coordinate less a half, as slice i's far side is
// its texel's middle; nearer than the first slice's far side, that slice's
// in proportion), its light times the exposure out as colour and its
// transmittance as alpha: blended colour ONE, SRC_ALPHA (picture x T + L),
// alpha ZERO, ONE.
//
// Set 0 is the lit scene's own set, declared whole; set 1 the depth, the
// record's integrated grid and a clamping linear sampler.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_fog_grid;

[[vk::binding(0, 1)]] Texture2D<float4> fog_depth : register(t0, space1);
[[vk::binding(1, 1)]] Texture3D<float4> fog_integrated : register(t1, space1);
[[vk::binding(2, 1)]] SamplerState fog_sampler : register(s2, space1);

// rbtrend_fog_apply_params in src/render/rbtrendfog_internal.hpp.
struct FogApplyParams {
    float4 range; // x near, y far, z ln(far / near), w S
    uint4 flags;  // x 1 where depth is reversed (near at 1)
};
[[vk::push_constant]] ConstantBuffer<FogApplyParams> params : register(b0, space4);

[shader("fragment")]
float4 rbtrend_fog_apply_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    float2 uv       = frag_coord.xy * u_lit_scene.target_size.zw;
    float z         = fog_depth.Load(int3(int2(frag_coord.xy), 0)).r;
    float far_value = params.flags.x != 0u ? 0.0 : 1.0;
    float far       = params.range.y;
    float d         = far;
    if (z != far_value) {
        float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float4 w   = mul(u_lit_scene.inverse_view_projection, float4(ndc, z, 1.0));
        if (w.w != 0.0)
            d = min(length(w.xyz / w.w - u_lit_scene.eye_position.xyz), far);
    }
    float slices = params.range.w;
    float k      = fog_slice_of(d, params.range.x, params.range.z, slices);
    float4 v;
    if (k >= 1.0) {
        v = fog_integrated.SampleLevel(fog_sampler, float3(uv, (k - 0.5) / slices), 0.0);
    } else {
        float4 first = fog_integrated.SampleLevel(fog_sampler, float3(uv, 0.5 / slices), 0.0);
        float share  = saturate(d / fog_slice_distance(1.0, params.range.x, params.range.z, slices));
        v            = float4(first.rgb * share, lerp(1.0, first.a, share));
    }
    return float4(v.rgb * u_lit_scene.exposure, v.a);
}
