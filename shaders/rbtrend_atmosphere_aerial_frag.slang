// A slice of a scene's aerial volume (RBTREND_COMMAND_LIT_ATMOSPHERE, stage
// 3 of the atmosphere): a froxel volume over the view's frustum, each texel
// a ray from the eye through it (the block's inverse_view_projection), each
// slice the light scattered into that ray up to its distance (the slice's
// end, slices even in distance to the block's aerial_distance) in RGB and
// its transmittance there in A (the mean of the three channels), marched
// in the block's steps a slice. A lit surface reads it at its own distance
// (rbtrend_lit_atmosphere.slang): its colour times the transmittance, plus
// the light. The ray stops where it leaves the shell, never at the ground
// sphere: whatever a surface there stands on is the caller's geometry, not
// that sphere, and its path is the slice's whole distance, a point of it
// under the sphere read at the sphere (atmosphere_march). An eye above the
// shell starts where the ray enters it.

import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;

[[vk::binding(0, 1)]] Texture2D<float4> transmittance_table : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState transmittance_sampler : register(s1, space1);
[[vk::binding(0, 2)]] Texture2D<float4> multiple_scattering_table : register(t0, space2);
[[vk::binding(1, 2)]] SamplerState multiple_scattering_sampler : register(s1, space2);

// RbtrendAerialParams in rbtrendatmosphere_internal.hpp: the slice, and the
// volume's texels across and down.
struct AerialParams {
    uint slice;
    uint padding;
    float2 size;
};
[[vk::push_constant]] ConstantBuffer<AerialParams> params : register(b0, space4);

float3 unproject(float2 ndc, float depth) {
    float4 p = mul(u_atmosphere.inverse_view_projection, float4(ndc, depth, 1.0));
    return p.xyz / p.w;
}

[shader("fragment")]
float4 rbtrend_atmosphere_aerial_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    AtmosphereMedium m = pass_medium();
    // The texel's ray, the first row at the top, as the sky shader casts.
    float2 ndc = frag_coord.xy / params.size * 2.0 - 1.0;
    ndc.y      = -ndc.y;
    float3 d   = normalize(unproject(ndc, 1.0) - unproject(ndc, 0.0));
    // Near at 0 or at 1: either way the ray runs away from the eye.
    if (dot(d, u_atmosphere.view_direction.xyz) < 0.0)
        d = -d;
    // The eye as the sky-view takes it (under the ground, just above it).
    float3 o = u_atmosphere.frame_up.xyz * sky_view_eye_radius(m, u_atmosphere.frame_up.w);
    float slices = u_atmosphere.more_sizes.y;
    float t_end  = u_atmosphere.more_sizes.z * (float(params.slice) + 1.0) / slices;
    float3 top   = sphere_hits(o, d, m.top_radius);
    if (top.z < 0.0 || top.y <= 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);
    float start = max(top.x, 0.0);
    float end   = min(top.y, start + t_end);
    uint steps = max(uint(u_atmosphere.more_sizes.w) * (params.slice + 1u), 1u);
    AtmosphereMarch march = atmosphere_march(m, transmittance_table, transmittance_sampler, multiple_scattering_table, multiple_scattering_sampler, o, d,
                                             start, max(end, start), steps, false);
    return float4(march.light, dot(march.transmittance, float3(1.0 / 3.0)));
}
