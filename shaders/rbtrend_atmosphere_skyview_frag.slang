// A scene's sky-view table (RBTREND_COMMAND_LIT_ATMOSPHERE): each texel the
// sky's radiance along its direction from the scene's eye (Hillaire's), in
// the eye's frame (rbtrend_atmosphere_model's sky_view_params). The ray is
// marched from the eye, or from where it enters the shell for an eye above
// it, to the top or the ground, in more steps for a longer ray, spaced
// closer near the eye. At each step each sun's light scattered once
// (through its transmittance, none where the ground hides it, by each
// layer's phase) and the multiple-scattering table's term; the light
// gathered and dimmed by the transmittance back to the eye. Where the ray
// meets the ground, the ground's light too (lit by each sun, its albedo,
// diffuse). In absolute radiance: each sun's illuminance is the block's.

import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;

[[vk::binding(0, 1)]] Texture2D<float4> transmittance_table : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState transmittance_sampler : register(s1, space1);
[[vk::binding(0, 2)]] Texture2D<float4> multiple_scattering_table : register(t0, space2);
[[vk::binding(1, 2)]] SamplerState multiple_scattering_sampler : register(s1, space2);

[shader("fragment")]
float4 rbtrend_atmosphere_skyview_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    AtmosphereMedium m = pass_medium();
    float2 size        = u_atmosphere.sizes.zw;
    float2 uv          = frag_coord.xy / size;
    uv                 = clamp(float2(sub_uv_to_unit(uv.x, size.x), sub_uv_to_unit(uv.y, size.y)), 0.0, 1.0);
    float3 up          = u_atmosphere.frame_up.xyz;
    float eye_radius   = sky_view_eye_radius(m, u_atmosphere.frame_up.w);
    float3 d           = sky_view_direction(up, u_atmosphere.frame_x.xyz, u_atmosphere.frame_y.xyz, sky_view_params(m, eye_radius, uv));
    float3 o           = up * eye_radius;
    float2 tsize       = u_atmosphere.sizes.xy;

    // The part of the ray inside the shell: none for an eye above it whose
    // ray misses it.
    float3 top = sphere_hits(o, d, m.top_radius);
    if (top.z < 0.0 || top.y <= 0.0)
        return float4(0.0, 0.0, 0.0, 1.0);
    float start = max(top.x, 0.0);
    float end   = top.y;
    bool ground = hits_ground(m, o, d);
    if (ground)
        end = sphere_hits(o, d, m.bottom_radius).x;
    float length_in = max(end - start, 0.0);

    float thickness  = m.top_radius - m.bottom_radius;
    float least      = float(u_atmosphere.steps.z);
    float most       = float(u_atmosphere.steps.w);
    uint steps       = uint(max(floor(lerp(least, most, saturate(length_in / thickness))), 1.0));

    AtmosphereMarch march = atmosphere_march(m, transmittance_table, transmittance_sampler, multiple_scattering_table, multiple_scattering_sampler, o,
                                             d, start, end, steps, true);
    float3 light      = march.light;
    float3 throughput = march.transmittance;
    if (ground) {
        float3 pg = o + d * end;
        float3 n  = normalize(pg);
        for (uint k = 0u; k < 2u; ++k) {
            if (u_atmosphere.sun_direction[k].w < 0.5)
                continue;
            float3 sun = u_atmosphere.sun_direction[k].xyz;
            float3 lit = transmittance_towards(m, transmittance_table, transmittance_sampler, tsize, pg, sun);
            light += throughput * u_atmosphere.sun_illuminance[k].rgb * lit * saturate(dot(n, sun)) * m.ground_albedo / PI;
        }
    }
    return float4(light, 1.0);
}
