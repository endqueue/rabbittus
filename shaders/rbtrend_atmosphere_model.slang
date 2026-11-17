module rbtrend_atmosphere_model;

// The model Rend's atmosphere draws (rbtrendatmosphere.h; Hillaire, "A
// Scalable and Production Ready Sky and Atmosphere Rendering Technique",
// EGSR 2020): a medium in the shell between two spheres round a centre, its
// three layers, their phase functions, where a ray meets a sphere, and how
// the tables are laid out. Rabbittus Math's math/rbtmathmedium.h is the
// same medium on the CPU; each function here names its counterpart there,
// and rabbittustests checks the tables against it. No bindings: the passes
// and the lit shaders each declare their own.

public static const float PI = 3.14159265359;

// A medium, as RBTMATH_SHELL_MEDIUM has it, in the caller's unit.
public struct AtmosphereMedium {
    public float bottom_radius;
    public float top_radius;
    public float3 rayleigh_scattering;
    public float rayleigh_scale_height;
    public float3 mie_scattering;
    public float3 mie_extinction;
    public float mie_scale_height;
    public float mie_anisotropy;
    public float3 absorption_extinction;
    public float absorption_center_height;
    public float absorption_half_width;
    public float3 ground_albedo;
};

// What the medium does at a point: its scattering by each layer and its
// extinction (per unit).
public struct AtmosphereSample {
    public float3 rayleigh_scattering;
    public float3 mie_scattering;
    public float3 extinction;
};

// rbtmathmedium.cpp's densities_at: each layer's density at [radius] from
// the centre, relative to its own at the bottom; 0 outside the shell.
public float3 atmosphere_densities(AtmosphereMedium m, float radius) {
    float h = radius - m.bottom_radius;
    if (h < 0.0 || radius > m.top_radius)
        return float3(0.0);
    float tent = 1.0 - abs(h - m.absorption_center_height) / m.absorption_half_width;
    return float3(exp(-h / m.rayleigh_scale_height), exp(-h / m.mie_scale_height), max(tent, 0.0));
}

public AtmosphereSample atmosphere_sample(AtmosphereMedium m, float radius) {
    float3 rho = atmosphere_densities(m, radius);
    AtmosphereSample s;
    s.rayleigh_scattering = m.rayleigh_scattering * rho.x;
    s.mie_scattering      = m.mie_scattering * rho.y;
    s.extinction          = m.rayleigh_scattering * rho.x + m.mie_extinction * rho.y + m.absorption_extinction * rho.z;
    return s;
}

// rbtmath_shell_medium_compute_phase's Rayleigh: 3 (1 + c^2) / (16 pi).
public float rayleigh_phase(float c) {
    return 3.0 / (16.0 * PI) * (1.0 + c * c);
}

// rbtmath_shell_medium_compute_phase's Mie layer: Cornette and Shanks with
// anisotropy g.
public float mie_phase(float g, float c) {
    float g2   = g * g;
    float base = max(1.0 + g2 - 2.0 * g * c, 1e-6);
    return 3.0 * (1.0 - g2) * (1.0 + c * c) / (8.0 * PI * (2.0 + g2) * base * sqrt(base));
}

// rbtmathmedium.cpp's sphere_hits: where the ray o + t d (d unit) crosses a
// sphere of [radius] round the origin, its near and far distances in xy; a
// negative discriminant (a miss) leaves z below 0.
public float3 sphere_hits(float3 o, float3 d, float radius) {
    float b    = dot(o, d);
    float c    = dot(o, o) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0)
        return float3(0.0, 0.0, -1.0);
    float root = sqrt(disc);
    return float3(-b - root, -b + root, 1.0);
}

// Whether a ray from [o] along [d] meets the ground ahead of it.
public bool hits_ground(AtmosphereMedium m, float3 o, float3 d) {
    float3 g = sphere_hits(o, d, m.bottom_radius);
    return g.z > 0.0 && g.y > 0.0;
}

// ---- Texel centres --------------------------------------------------------

// Hillaire's sub-uvs: a lookup at 0 or 1 lands on a texel's centre, so the
// table's edges hold its ends exactly.
public float unit_to_sub_uv(float u, float resolution) {
    return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}

public float sub_uv_to_unit(float u, float resolution) {
    return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
}

// ---- The transmittance table (Bruneton's parameterisation) ----------------

// The texel of the transmittance from [radius] at a direction whose cosine
// with the vertical is [mu], before the sub-uvs: x the distance to the top
// along the ray between its least and most at that height, y the height.
public float2 transmittance_uv(AtmosphereMedium m, float radius, float mu) {
    float H    = sqrt(max(m.top_radius * m.top_radius - m.bottom_radius * m.bottom_radius, 0.0));
    float rho  = sqrt(max(radius * radius - m.bottom_radius * m.bottom_radius, 0.0));
    float disc = radius * radius * (mu * mu - 1.0) + m.top_radius * m.top_radius;
    float d    = max(0.0, -radius * mu + sqrt(max(disc, 0.0)));
    float dmin = m.top_radius - radius;
    float dmax = rho + H;
    float xmu  = (d - dmin) / max(dmax - dmin, 1e-12);
    float xr   = rho / max(H, 1e-12);
    return float2(xmu, xr);
}

// The height and direction a texel of the transmittance table stands for:
// x the radius, y the cosine with the vertical.
public float2 transmittance_params(AtmosphereMedium m, float2 uv) {
    float H    = sqrt(max(m.top_radius * m.top_radius - m.bottom_radius * m.bottom_radius, 0.0));
    float rho  = H * uv.y;
    float r    = sqrt(rho * rho + m.bottom_radius * m.bottom_radius);
    float dmin = m.top_radius - r;
    float dmax = rho + H;
    float d    = dmin + uv.x * (dmax - dmin);
    float mu   = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    return float2(r, clamp(mu, -1.0, 1.0));
}

// The table's uv for a lookup of [radius] and [mu], its size [size].
public float2 transmittance_lookup_uv(AtmosphereMedium m, float radius, float mu, float2 size) {
    float2 uv = transmittance_uv(m, clamp(radius, m.bottom_radius, m.top_radius), mu);
    return float2(unit_to_sub_uv(uv.x, size.x), unit_to_sub_uv(uv.y, size.y));
}

// ---- The multiple-scattering table ----------------------------------------

// Its uv for a height and the sun's cosine with the vertical there: x the
// cosine from -1 to 1, y the height from the bottom to the top.
public float2 multiple_scattering_uv(AtmosphereMedium m, float radius, float mu_sun, float size) {
    float u = clamp(0.5 + 0.5 * mu_sun, 0.0, 1.0);
    float v = clamp((radius - m.bottom_radius) / (m.top_radius - m.bottom_radius), 0.0, 1.0);
    return float2(unit_to_sub_uv(u, size), unit_to_sub_uv(v, size));
}

// ---- The sky-view table ---------------------------------------------------

// A direction of the sky-view table: its cosine with the eye's up and its
// angle round the up from the frame's x (-pi to pi: the frame's x, the
// first sun's bearing, at the table's middle column, so the seam where its
// columns wrap is opposite the sun, where the sky is smoothest). The
// latitude is
// Hillaire's: half the table above the horizon the eye sees (the ground's
// tangent, below the flat one by the ground's curve), half below, each
// half with its texels drawn towards the horizon.
public float2 sky_view_params(AtmosphereMedium m, float eye_radius, float2 uv) {
    float vhorizon       = sqrt(max(eye_radius * eye_radius - m.bottom_radius * m.bottom_radius, 0.0));
    float beta           = acos(clamp(vhorizon / eye_radius, -1.0, 1.0));
    float zenith_horizon = PI - beta;
    float zenith;
    if (uv.y < 0.5) {
        float coord = 1.0 - 2.0 * uv.y;
        coord       = 1.0 - coord * coord;
        zenith      = zenith_horizon * coord;
    } else {
        float coord = 2.0 * uv.y - 1.0;
        zenith      = zenith_horizon + beta * coord * coord;
    }
    return float2(cos(zenith), (uv.x * 2.0 - 1.0) * PI);
}

// The other way: [cos_zenith] and [azimuth] (-pi to pi) into the table's
// uv before the sub-uvs.
public float2 sky_view_uv(AtmosphereMedium m, float eye_radius, float cos_zenith, float azimuth) {
    float vhorizon       = sqrt(max(eye_radius * eye_radius - m.bottom_radius * m.bottom_radius, 0.0));
    float beta           = acos(clamp(vhorizon / eye_radius, -1.0, 1.0));
    float zenith_horizon = PI - beta;
    float zenith         = acos(clamp(cos_zenith, -1.0, 1.0));
    float v;
    if (zenith < zenith_horizon) {
        float coord = zenith / zenith_horizon;
        v           = (1.0 - sqrt(max(1.0 - coord, 0.0))) * 0.5;
    } else {
        float coord = (zenith - zenith_horizon) / max(beta, 1e-6);
        v           = sqrt(max(coord, 0.0)) * 0.5 + 0.5;
    }
    return float2(azimuth / (2.0 * PI) + 0.5, v);
}

// The sky-view table's uv of a direction [d] in the scene's space, seen
// from an eye at [eye_radius] whose frame is [up], [x] and [y], after the
// sub-uvs for a table of [size] texels.
public float2 sky_view_lookup_uv(AtmosphereMedium m, float eye_radius, float3 up, float3 x, float3 y, float3 d, float2 size) {
    float2 uv = sky_view_uv(m, eye_radius, dot(d, up), atan2(dot(d, y), dot(d, x)));
    return float2(unit_to_sub_uv(uv.x, size.x), unit_to_sub_uv(uv.y, size.y));
}

// A direction [d] of the sky-view table in the scene's space, from its
// cosine with [up] and its angle round it from [x] towards [y].
public float3 sky_view_direction(float3 up, float3 x, float3 y, float2 params) {
    float s = sqrt(max(1.0 - params.x * params.x, 0.0));
    return up * params.x + s * (x * cos(params.y) + y * sin(params.y));
}

// What the transmittance from a point [p] (relative to the centre) towards
// [dir] is: 0, none (into the ground); 1, all (from outside the shell along
// a ray that misses it); 2, the table's at [out_r] and [out_mu] (from where
// the ray enters the shell, for a point above it; a point under the ground
// taken at it).
public uint transmittance_ray(AtmosphereMedium m, float3 p, float3 dir, out float out_r, out float out_mu) {
    out_r    = 0.0;
    out_mu   = 0.0;
    float r  = length(p);
    if (r > m.top_radius) {
        float3 h = sphere_hits(p, dir, m.top_radius);
        if (h.z < 0.0 || h.y < 0.0)
            return 1u;
        p = p + dir * max(h.x, 0.0);
        r = m.top_radius;
    }
    if (r < m.bottom_radius) {
        p = p * (m.bottom_radius / max(r, 1e-12));
        r = m.bottom_radius;
    }
    if (hits_ground(m, p, dir))
        return 0u;
    out_r  = r;
    out_mu = dot(p / r, dir);
    return 2u;
}

// The transmittance from a point [p] (relative to the centre) towards
// [dir], read from [table] (transmittance_ray's cases).
public float3 transmittance_towards(AtmosphereMedium m, Texture2D<float4> table, SamplerState s, float2 size, float3 p, float3 dir) {
    float r, mu;
    uint kind = transmittance_ray(m, p, dir, r, mu);
    if (kind == 0u)
        return float3(0.0);
    if (kind == 1u)
        return float3(1.0);
    return table.SampleLevel(s, transmittance_lookup_uv(m, r, mu, size), 0.0).rgb;
}

// Where the eye is taken for the sky-view, as a radius from the centre:
// itself, inside the shell or above it (a march then starts where its ray
// enters the shell, so the shell's ring stays where it is seen from); under
// the ground, just above it.
public float sky_view_eye_radius(AtmosphereMedium m, float radius) {
    float thickness = m.top_radius - m.bottom_radius;
    return max(radius, m.bottom_radius + thickness * 1e-4);
}
