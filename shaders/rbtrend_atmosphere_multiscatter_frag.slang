// An atmosphere's multiple-scattering table (RBTREND_COMMAND_ATMOSPHERE_TABLES,
// after its transmittance table): Hillaire's term for light scattered more
// than once, each texel a height (y) and the sun's cosine with the vertical
// there (x). From the texel's point, rays in RBTREND_ATMOSPHERE_DIRECTIONS
// directions spread evenly over the sphere each gather the light the sun
// sends once scattered isotropically (and off the ground), and how much the
// medium scatters along them; the first summed is the light of the second
// order, the second the share each order passes to the next, so every
// order together is the second over one minus the share. Per unit of the
// sun's illuminance.

import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;

// The technique's sphere of directions: 8 by 8 (the paper's), a constant
// of the method, not a cost the caller trades.
static const uint RBTREND_ATMOSPHERE_DIRECTIONS_SIDE = 8u;

[[vk::binding(0, 1)]] Texture2D<float4> transmittance_table : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState table_sampler : register(s1, space1);

[shader("fragment")]
float4 rbtrend_atmosphere_multiscatter_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    AtmosphereMedium m = pass_medium();
    float side         = u_atmosphere.more_sizes.x;
    float2 uv          = frag_coord.xy / side;
    uv                 = clamp(float2(sub_uv_to_unit(uv.x, side), sub_uv_to_unit(uv.y, side)), 0.0, 1.0);
    float mu_sun       = uv.x * 2.0 - 1.0;
    float thickness    = m.top_radius - m.bottom_radius;
    float radius       = m.bottom_radius + clamp(uv.y, 1e-3, 1.0 - 1e-3) * thickness;
    float3 o           = float3(0.0, radius, 0.0);
    float3 sun         = float3(sqrt(max(1.0 - mu_sun * mu_sun, 0.0)), mu_sun, 0.0);
    float2 tsize       = u_atmosphere.sizes.xy;
    uint steps         = u_atmosphere.steps.y;
    float isotropic    = 1.0 / (4.0 * PI);

    float3 second = float3(0.0);
    float3 share  = float3(0.0);
    uint count    = RBTREND_ATMOSPHERE_DIRECTIONS_SIDE * RBTREND_ATMOSPHERE_DIRECTIONS_SIDE;
    for (uint k = 0u; k < count; ++k) {
        float a     = (float(k % RBTREND_ATMOSPHERE_DIRECTIONS_SIDE) + 0.5) / float(RBTREND_ATMOSPHERE_DIRECTIONS_SIDE);
        float b     = (float(k / RBTREND_ATMOSPHERE_DIRECTIONS_SIDE) + 0.5) / float(RBTREND_ATMOSPHERE_DIRECTIONS_SIDE);
        float theta = 2.0 * PI * a;
        float phi   = acos(1.0 - 2.0 * b);
        float3 d    = float3(cos(theta) * sin(phi), cos(phi), sin(theta) * sin(phi));

        float3 top    = sphere_hits(o, d, m.top_radius);
        float end     = max(top.y, 0.0);
        bool ground   = hits_ground(m, o, d);
        if (ground)
            end = sphere_hits(o, d, m.bottom_radius).x;
        float dt          = end / float(steps);
        float3 throughput = float3(1.0);
        float3 light      = float3(0.0);
        float3 scattered  = float3(0.0);
        for (uint i = 0u; i < steps; ++i) {
            float3 p             = o + d * ((float(i) + 0.5) * dt);
            float r              = length(p);
            AtmosphereSample smp = atmosphere_sample(m, r);
            float3 scattering    = smp.rayleigh_scattering + smp.mie_scattering;
            float3 to_sun        = transmittance_towards(m, transmittance_table, table_sampler, tsize, p, sun);
            float3 source        = scattering * isotropic * to_sun;
            float3 step_t        = exp(-smp.extinction * dt);
            float3 ext           = max(smp.extinction, float3(1e-12));
            light += throughput * (source - source * step_t) / ext;
            scattered += throughput * (scattering - scattering * step_t) / ext;
            throughput *= step_t;
        }
        if (ground) {
            float3 pg   = o + d * end;
            float3 n    = normalize(pg);
            float3 lit  = transmittance_towards(m, transmittance_table, table_sampler, tsize, pg, sun);
            light += throughput * lit * saturate(dot(n, sun)) * m.ground_albedo / PI;
        }
        second += light;
        share += scattered;
    }
    // Each direction's weight: the sphere's solid angle over the count,
    // times the isotropic phase (one over that solid angle) at this point:
    // one over the count, for both sums.
    second /= float(count);
    share /= float(count);
    float3 psi = second / max(float3(1.0) - share, float3(1e-3));
    return float4(psi, 1.0);
}
