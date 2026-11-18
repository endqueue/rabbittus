module rbtrend_lit_atmosphere;

// What Rend's lit shaders read of a scene's atmosphere (rbtrendatmosphere.h):
// its suns' light dimmed by the medium on the way to a point, and the sky
// along a ray, from the scene block's atmosphere_* members and set 0's
// atmosphere tables. Nothing where the scene has none.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_atmosphere_model;

// The medium's radii, all a lookup needs.
AtmosphereMedium lit_atmosphere_radii() {
    AtmosphereMedium m;
    m.bottom_radius            = u_lit_scene.atmosphere_center.w;
    m.top_radius               = u_lit_scene.atmosphere_up.w;
    m.rayleigh_scattering      = float3(0.0);
    m.rayleigh_scale_height    = 1.0;
    m.mie_scattering           = float3(0.0);
    m.mie_extinction           = float3(0.0);
    m.mie_scale_height         = 1.0;
    m.mie_anisotropy           = 0.0;
    m.absorption_extinction    = float3(0.0);
    m.absorption_center_height = 0.0;
    m.absorption_half_width    = 1.0;
    m.ground_albedo            = float3(0.0);
    return m;
}

// Whether the scene draws an atmosphere: its suns reach surfaces through
// its transmittance.
public bool lit_atmosphere() {
    return (u_lit_scene.flags & RBTREND_LIT_SCENE_ATMOSPHERE) != 0u;
}

// Whether the scene's atmosphere draws a sky: its sky-view table holds it.
// With no sky (rbtrendlit.h, sky-view sizes 0) the sky is drawn as with no
// atmosphere, the suns' discs undimmed.
public bool lit_atmosphere_sky_drawn() {
    return (u_lit_scene.flags & RBTREND_LIT_SCENE_SKY) != 0u;
}

// A table of atmosphere_tables' layer [layer], [size] texels in the top
// left of it, at [uv] (0 to 1 over the table, its sub-uvs applied).
float3 lit_atmosphere_table(float layer, float2 size, float2 uv) {
    float2 texel = clamp(uv * size, float2(0.5), size - 0.5);
    return atmosphere_tables.SampleLevel(environment_sampler, float3(texel / u_lit_scene.atmosphere_array.xy, layer), 0.0).rgb;
}

// What reaches [position] (the scene's space) of directional light [d]:
// the transmittance through the medium towards it for one of the scene's
// suns, 1 for any other light or no atmosphere.
public float3 lit_atmosphere_sun_transmittance(uint d, float3 position) {
    if (!lit_atmosphere() || float(d) >= u_lit_scene.atmosphere_y.w)
        return float3(1.0);
    AtmosphereMedium m = lit_atmosphere_radii();
    float r, mu;
    uint kind = surface_transmittance_ray(m, position - u_lit_scene.atmosphere_center.xyz, u_lit_scene.directional[d].direction_versine.xyz, r, mu);
    if (kind == 0u)
        return float3(0.0);
    if (kind == 1u)
        return float3(1.0);
    float2 size = u_lit_scene.atmosphere_sizes.xy;
    return lit_atmosphere_table(1.0, size, transmittance_lookup_uv(m, r, mu, size));
}

// The transmittance from [position] (the scene's space) along
// [direction] (unit) out of the scene's atmosphere: 0 into its ground, 1
// with none.
public float3 lit_atmosphere_transmittance(float3 position, float3 direction) {
    if (!lit_atmosphere())
        return float3(1.0);
    AtmosphereMedium m = lit_atmosphere_radii();
    float r, mu;
    uint kind = surface_transmittance_ray(m, position - u_lit_scene.atmosphere_center.xyz, direction, r, mu);
    if (kind == 0u)
        return float3(0.0);
    if (kind == 1u)
        return float3(1.0);
    float2 size = u_lit_scene.atmosphere_sizes.xy;
    return lit_atmosphere_table(1.0, size, transmittance_lookup_uv(m, r, mu, size));
}

// The sky along [direction] (unit, the scene's space) from the scene's eye:
// the sky-view table's.
public float3 lit_atmosphere_sky(float3 direction) {
    AtmosphereMedium m = lit_atmosphere_radii();
    float eye_radius   = sky_view_eye_radius(m, u_lit_scene.atmosphere_x.w);
    float2 size        = u_lit_scene.atmosphere_sizes.zw;
    float2 uv = sky_view_lookup_uv(m, eye_radius, u_lit_scene.atmosphere_up.xyz, u_lit_scene.atmosphere_x.xyz, u_lit_scene.atmosphere_y.xyz, direction, size);
    return lit_atmosphere_table(0.0, size, uv);
}

// The scene's aerial volume at a distance at a texel: the light the air
// adds, and what of a surface behind gets through, per channel.
public struct LitAir {
    public float3 light;
    public float3 transmittance;
};

// [color] (linear, before exposure) of a lit surface at [position] seen from
// the eye, at the texel [frag] of the view: faded into the air by the
// scene's aerial volume, color * T + L channel by channel, the volume read between the two
// slices either side of the surface's distance (nearer than the first
// slice's end, between none and it), at the volume's texel under the
// surface. Unchanged with no volume.
public float3 lit_atmosphere_aerial(float3 color, float3 position, float2 frag) {
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_AERIAL) == 0u)
        return color;
    LitAir air = lit_atmosphere_air(length(position - u_lit_scene.eye_position.xyz), frag);
    return color * air.transmittance + air.light;
}

// The scene's aerial volume at [distance] from the eye at the texel [frag]
// of the view: the light the air adds and its transmittance, each RGB, as
// lit_atmosphere_aerial() applies them; no light and a transmittance of 1
// with no volume. The light's slices are layers 2 on of the scene's
// array, the transmittance's the same number after them.
public LitAir lit_atmosphere_air(float distance, float2 frag) {
    LitAir air;
    air.light         = float3(0.0);
    air.transmittance = float3(1.0);
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_AERIAL) == 0u)
        return air;
    float slices   = u_lit_scene.atmosphere_aerial.x;
    float s        = distance / max(u_lit_scene.atmosphere_aerial.y, 1e-6) * slices - 1.0;
    float2 size    = u_lit_scene.atmosphere_aerial.zw;
    float2 uv      = frag * u_lit_scene.target_size.zw;
    float2 texel   = clamp(uv * size, float2(0.5), size - 0.5) / u_lit_scene.atmosphere_array.xy;
    float lower    = clamp(floor(s), 0.0, slices - 1.0);
    float upper    = clamp(lower + 1.0, 0.0, slices - 1.0);
    float3 la      = atmosphere_tables.SampleLevel(environment_sampler, float3(texel, 2.0 + lower), 0.0).rgb;
    float3 lb      = atmosphere_tables.SampleLevel(environment_sampler, float3(texel, 2.0 + upper), 0.0).rgb;
    float3 ta      = atmosphere_tables.SampleLevel(environment_sampler, float3(texel, 2.0 + slices + lower), 0.0).rgb;
    float3 tb      = atmosphere_tables.SampleLevel(environment_sampler, float3(texel, 2.0 + slices + upper), 0.0).rgb;
    if (s < 0.0) {
        // Nearer than the first slice's end: between no air and it.
        float f           = saturate(s + 1.0);
        air.light         = la * f;
        air.transmittance = lerp(float3(1.0), ta, f);
        return air;
    }
    float f           = saturate(s - lower);
    air.light         = lerp(la, lb, f);
    air.transmittance = lerp(ta, tb, f);
    return air;
}
