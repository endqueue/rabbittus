// The clouds put over the picture (RBTREND_COMMAND_LIT_CLOUDS,
// rbtrendclouds.h): at each texel the history (half the target each way)
// upsampled by depth, its four nearest texels weighted bilinearly and down
// to a thousandth where a texel's cloud lies past the surface drawn here;
// the scene atmosphere's air between the eye and the clouds' depth in front
// (its aerial volume: L' = L_air x (1 - T) + T_air x L, per channel), then
// the scene's fog the same way; out as colour times the exposure and the
// transmittance as alpha: blended colour ONE, SRC_ALPHA (picture x T + L'),
// alpha ZERO, ONE.
//
// Set 0 is the lit scene's own set, declared whole; set 1 the input
// (rbtrend_clouds_input: the depth); set 2 the record's history this frame
// wrote.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_atmosphere;
import rbtrend_lit_fog;
import rbtrend_clouds_input;

[[vk::binding(0, 2)]] Texture2D<float4> clouds_history : register(t0, space2);
[[vk::binding(1, 2)]] Texture2D<float4> clouds_history_depth : register(t1, space2);

// rbtrend_clouds_composite_params in src/render/rbtrendclouds_internal.hpp.
struct CloudsCompositeParams {
    uint4 sizes; // x, y the history's texels; z 1 where depth is reversed (near at 1)
};
[[vk::push_constant]] ConstantBuffer<CloudsCompositeParams> params : register(b0, space4);

[shader("fragment")]
float4 rbtrend_clouds_composite_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    float2 pixel = frag_coord.xy;
    float2 uv    = pixel * u_lit_scene.target_size.zw;
    float2 ndc   = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 eye   = u_lit_scene.eye_position.xyz;
    float4 a     = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 b     = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    float3 dir   = normalize(b.xyz * a.w - a.xyz * b.w);
    if (dot(dir, u_lit_scene.view_direction.xyz) < 0.0)
        dir = -dir;
    float z          = clouds_depth.Load(int3(int2(pixel), 0)).r;
    float far_value  = params.sizes.z != 0u ? 0.0 : 1.0;
    bool surface     = false;
    float surface_at = 0.0;
    if (z != far_value) {
        float4 w = mul(u_lit_scene.inverse_view_projection, float4(ndc, z, 1.0));
        if (w.w != 0.0) {
            surface    = true;
            surface_at = length(w.xyz / w.w - eye);
        }
    }
    float2 h  = pixel * 0.5 - 0.5;
    float2 i0 = floor(h);
    float2 f  = h - i0;
    int2 last = int2(params.sizes.xy) - 1;
    float4 sum    = float4(0.0);
    float depth   = 0.0;
    float weights = 0.0;
    for (int dy = 0; dy <= 1; ++dy)
        for (int dx = 0; dx <= 1; ++dx) {
            int2 c      = clamp(int2(i0) + int2(dx, dy), int2(0), last);
            float4 v    = clouds_history.Load(int3(c, 0));
            float d     = clouds_history_depth.Load(int3(c, 0)).r;
            float w     = (dx == 0 ? 1.0 - f.x : f.x) * (dy == 0 ? 1.0 - f.y : f.y);
            if (surface && v.a < 0.999 && d > surface_at * 1.02)
                w *= 0.001;
            w = max(w, 1e-6);
            sum += v * w;
            depth += d * w;
            weights += w;
        }
    float4 v  = sum / weights;
    depth     = depth / weights;
    float3 L  = v.rgb;
    float T   = saturate(v.a);
    LitAir air = lit_atmosphere_air(depth, pixel);
    L          = air.light * (1.0 - T) + air.transmittance * L;
    LitFog fog = lit_fog_along(dir, depth);
    L          = fog.light * (1.0 - fog.transmittance) * (1.0 - T) + fog.transmittance * L;
    return float4(L * u_lit_scene.exposure, T);
}
