// A face of an environment's radiance cube from a scene's sky-view table
// (RBTREND_COMMAND_LIT_ATMOSPHERE_ENVIRONMENT): each texel the sky along its
// direction, the cube in the frame the scene's ambient light names (the
// block's environment_rotation, the scene's space into the environment's),
// read from the table as the sky shader reads it. Then prefiltered as any
// environment's picture is.

import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;
import rbtrend_atmosphere_pass_block;
import rbtrend_environment;

[[vk::binding(0, 1)]] Texture2D<float4> sky_view_table : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState sky_view_sampler : register(s1, space1);

[shader("fragment")]
float4 rbtrend_atmosphere_environment_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    AtmosphereMedium m = pass_medium();
    float3 e           = cube_direction(frag_coord.xy, params.face);
    // The rows turn the scene's space into the environment's, so their
    // transpose turns back.
    float3 d = normalize(u_atmosphere.environment_rotation[0].xyz * e.x + u_atmosphere.environment_rotation[1].xyz * e.y +
                         u_atmosphere.environment_rotation[2].xyz * e.z);
    float eye_radius = sky_view_eye_radius(m, u_atmosphere.frame_up.w);
    float2 uv        = sky_view_lookup_uv(m, eye_radius, u_atmosphere.frame_up.xyz, u_atmosphere.frame_x.xyz, u_atmosphere.frame_y.xyz, d,
                                          u_atmosphere.sizes.zw);
    // The sky-view table is the top left of its layer (the scene's array).
    float2 table = u_atmosphere.sizes.zw;
    float2 texel = clamp(uv * table, float2(0.5), table - 0.5);
    return float4(sky_view_table.SampleLevel(sky_view_sampler, texel / u_atmosphere.array_sizes.xy, 0.0).rgb, 1.0);
}
