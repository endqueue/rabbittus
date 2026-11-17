module rbtrend_atmosphere_pass;

// The block Rend's atmosphere passes read at set 0 (rbtrendatmosphere.h):
// the medium, the marches' steps, the tables' sizes, and for a scene's
// passes its eye's frame, its suns and its environment's frame.
// rbtrend_atmosphere_pass_block in src/render/rbtrendatmosphere_internal.hpp
// mirrors it, its members' offsets checked against this shader's reflection
// when the layout is made.

import rbtrend_atmosphere_model;

public struct RbtrendAtmospherePass {
    public float4 radii;                   // x bottom, y top, z Rayleigh's scale height, w Mie's
    public float4 rayleigh;                // rgb Rayleigh's scattering; w Mie's anisotropy
    public float4 mie_scattering;          // rgb; w the absorbing layer's centre height
    public float4 mie_extinction;          // rgb; w its half width
    public float4 absorption;              // rgb the absorbing layer's extinction at its peak
    public float4 ground_albedo;           // rgb
    public uint4 steps;                    // x transmittance, y multiple scattering, z the sky-view's least, w its most
    public float4 sizes;                   // xy the transmittance table; zw the sky-view table
    public float4 more_sizes;              // x the multiple-scattering table's side
    public float4 frame_up;                // xyz the eye's up, unit; w the eye's distance from the centre
    public float4 frame_x;                 // xyz the sky-view's x: towards the first sun's bearing
    public float4 frame_y;                 // xyz
    public float4 sun_direction[2];        // xyz towards it, unit; w 1 if it lights the medium
    public float4 sun_illuminance[2];      // rgb pi times its radiance
    public float4 environment_rotation[3]; // rows, the scene's space into the environment's
    public float4 array_sizes;             // xy the scene's atmosphere array's layers, in texels (a sky-view table read from layer 0 there)
    public float4x4 inverse_view_projection; // the scene's view's, for the aerial volume's rays
    public float4 view_direction;            // xyz unit: the way the scene's view looks
};
[[vk::binding(0, 0)]] public ConstantBuffer<RbtrendAtmospherePass> u_atmosphere : register(b0, space0);

// What a march gathers along a ray: the light scattered into it towards
// its start, and the transmittance along it.
public struct AtmosphereMarch {
    public float3 light;
    public float3 transmittance;
};

// The light the suns send into a ray from [o] along [d] (both relative to
// the centre) between [start] and [end], in [steps] steps, each sun's
// light scattered once (through its transmittance, by each layer's phase)
// and the multiple-scattering table's term, dimmed back to [o]; the
// steps spaced closer near [o] where [squared] (the sky-view's), evenly
// otherwise (the aerial volume's). Rabbittus Math's medium, but for a point
// under the ground sphere, which is read at it, as an eye there is taken
// (the sky-view's rays end at the ground, so only the aerial volume's
// reach it: a surface below that sphere, or the volume's row half a texel
// under a level view, is in the densest air, not in none); the tables are
// the atmosphere's.
public AtmosphereMarch atmosphere_march(AtmosphereMedium m, Texture2D<float4> transmittance_table, SamplerState transmittance_sampler,
                                        Texture2D<float4> scattering_table, SamplerState scattering_sampler, float3 o, float3 d, float start,
                                        float end, uint steps, bool squared) {
    AtmosphereMarch result;
    result.light         = float3(0.0);
    result.transmittance = float3(1.0);
    float2 tsize         = u_atmosphere.sizes.xy;
    float msize          = u_atmosphere.more_sizes.x;
    float length_in      = max(end - start, 0.0);
    for (uint i = 0u; i < steps; ++i) {
        float a              = float(i) / float(steps);
        float b              = float(i + 1u) / float(steps);
        float t0             = start + length_in * (squared ? a * a : a);
        float t1             = start + length_in * (squared ? b * b : b);
        float dt             = t1 - t0;
        float3 p             = o + d * (0.5 * (t0 + t1));
        float r              = max(length(p), m.bottom_radius);
        float3 p_up          = normalize(p);
        AtmosphereSample smp = atmosphere_sample(m, r);
        float3 scattering    = smp.rayleigh_scattering + smp.mie_scattering;
        float3 source        = float3(0.0);
        for (uint k = 0u; k < 2u; ++k) {
            if (u_atmosphere.sun_direction[k].w < 0.5)
                continue;
            float3 sun   = u_atmosphere.sun_direction[k].xyz;
            float c      = dot(d, sun);
            float3 once  = transmittance_towards(m, transmittance_table, transmittance_sampler, tsize, p, sun) *
                          (smp.rayleigh_scattering * rayleigh_phase(c) + smp.mie_scattering * mie_phase(m.mie_anisotropy, c));
            float3 multi = scattering_table.SampleLevel(scattering_sampler, multiple_scattering_uv(m, r, dot(p_up, sun), msize), 0.0).rgb * scattering;
            source += u_atmosphere.sun_illuminance[k].rgb * (once + multi);
        }
        float3 step_t = exp(-smp.extinction * dt);
        float3 ext    = max(smp.extinction, float3(1e-12));
        result.light += result.transmittance * (source - source * step_t) / ext;
        result.transmittance *= step_t;
    }
    return result;
}

// The medium the block holds.
public AtmosphereMedium pass_medium() {
    AtmosphereMedium m;
    m.bottom_radius            = u_atmosphere.radii.x;
    m.top_radius               = u_atmosphere.radii.y;
    m.rayleigh_scale_height    = u_atmosphere.radii.z;
    m.mie_scale_height         = u_atmosphere.radii.w;
    m.rayleigh_scattering      = u_atmosphere.rayleigh.rgb;
    m.mie_anisotropy           = u_atmosphere.rayleigh.w;
    m.mie_scattering           = u_atmosphere.mie_scattering.rgb;
    m.absorption_center_height = u_atmosphere.mie_scattering.w;
    m.mie_extinction           = u_atmosphere.mie_extinction.rgb;
    m.absorption_half_width    = u_atmosphere.mie_extinction.w;
    m.absorption_extinction    = u_atmosphere.absorption.rgb;
    m.ground_albedo            = u_atmosphere.ground_albedo.rgb;
    return m;
}
