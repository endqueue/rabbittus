// An atmosphere's transmittance table (RBTREND_COMMAND_ATMOSPHERE_TABLES):
// each texel the transmittance from its height up its direction to the top
// of the shell (Bruneton's parameterisation, rbtrend_atmosphere_model's
// transmittance_params), marched in the block's steps at their midpoints, as
// rbtmath_shell_medium_compute_transmittance marches.

import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;

[shader("fragment")]
float4 rbtrend_atmosphere_transmittance_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    AtmosphereMedium m = pass_medium();
    float2 size        = u_atmosphere.sizes.xy;
    float2 uv          = frag_coord.xy / size;
    uv                 = float2(sub_uv_to_unit(uv.x, size.x), sub_uv_to_unit(uv.y, size.y));
    float2 rm          = transmittance_params(m, clamp(uv, 0.0, 1.0));
    float3 o           = float3(0.0, rm.x, 0.0);
    float3 d           = float3(sqrt(max(1.0 - rm.y * rm.y, 0.0)), rm.y, 0.0);
    float3 top         = sphere_hits(o, d, m.top_radius);
    float end          = max(top.y, 0.0);
    uint steps         = u_atmosphere.steps.x;
    float dt           = end / float(steps);
    float3 depth       = float3(0.0);
    for (uint i = 0u; i < steps; ++i) {
        float3 p = o + d * ((float(i) + 0.5) * dt);
        depth += atmosphere_sample(m, length(p)).extinction * dt;
    }
    return float4(exp(-depth), 1.0);
}
