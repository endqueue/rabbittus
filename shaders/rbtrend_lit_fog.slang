module rbtrend_lit_fog;

// What Rend's lit shaders read of a scene's height fog (rbtrendlit.h,
// rbtrend_lit_scene_set_fog()): its transmittance along a part of a view
// ray, worked out as Rabbittus Math's rbtmath_height_fog_compute_transmittance()
// (rbtmathfog.h's Definition: a plane's closed form, a sphere's by the
// Chapman function's large-x form), in float, so a copy to
// a tolerance; and the light it adds, its albedo times its suns' and the
// ambient light. Nothing where the scene has none.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_atmosphere;

static const float FOG_PI = 3.14159265359;

// Whether the scene has a fog.
public bool lit_fog() {
    return (u_lit_scene.flags & RBTREND_LIT_SCENE_FOG) != 0u;
}

// g(x) = (1 - exp(-x)) / x, its series below 2^-4 (rbtmathfog.h).
float fog_g(float x) {
    if (abs(x) >= 0.0625)
        return (1.0 - exp(-x)) / x;
    return ((((x / 120.0 - 1.0 / 24.0) * x + 1.0 / 6.0) * x - 0.5) * x) + 1.0;
}

// A plane's part above the base, from height y0 to y1 over length l.
float fog_plane_part(float y0, float y1, float l) {
    float s = u_lit_scene.fog_range.w;
    return u_lit_scene.fog_eye.w * l * exp(-(y0 / s)) * fog_g((y1 - y0) / s);
}

// A plane's optical depth between the ray's distances t0 and t1 from the
// eye along [dir] (unit).
float fog_plane_tau(float3 dir, float t0, float t1) {
    float3 up   = u_lit_scene.fog_up_base.xyz;
    float base  = u_lit_scene.fog_up_base.w;
    float L     = t1 - t0;
    float y_eye = dot(u_lit_scene.fog_eye.xyz, up) - base;
    float slope = dot(dir, up);
    float ya    = y_eye + slope * t0;
    float yb    = y_eye + slope * t1;
    float density = u_lit_scene.fog_eye.w;
    bool a_below = ya < 0.0;
    bool b_below = yb < 0.0;
    if (a_below && b_below)
        return density * L;
    if (!a_below && !b_below)
        return fog_plane_part(ya, yb, L);
    float la    = L * (ya / (ya - yb));
    float lb    = L - la;
    float tau_a = a_below ? density * la : fog_plane_part(ya, 0.0, la);
    float tau_b = b_below ? density * lb : fog_plane_part(0.0, yb, lb);
    return tau_a + tau_b;
}

// erfcx(y) = exp(y^2) erfc(y), y at least 0: rbtmathfog.h's function by a
// cheaper route a fragment can afford, within 3e-5 relative (measured
// against libm's erfc, 2026-11-18): below 2, Hastings' rational form
// (Abramowitz and Stegun 7.1.26) with its exp(-y^2) left off; from 2,
// Laplace's continued fraction to 8 terms, worked from the bottom.
float fog_erfcx(float y) {
    if (y < 2.0) {
        float t = 1.0 / (1.0 + 0.3275911 * y);
        return t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429))));
    }
    float f = y;
    for (int k = 8; k >= 1; --k)
        f = y + (float(k) * 0.5) / f;
    return 1.0 / (1.7724538509 * f);
}

// A sphere's optical depth from a point at radius r outwards to infinity,
// at the cosine mu between its radius and its way: the Chapman function's
// large-x form, sqrt(pi x / 2) erfcx(sqrt(x / 2) mu).
float fog_outward(float r, float mu) {
    float s    = u_lit_scene.fog_range.w;
    float base = u_lit_scene.fog_up_base.w;
    float x    = r / s;
    float ch   = sqrt(FOG_PI * x * 0.5) * fog_erfcx(sqrt(x * 0.5) * saturate(mu));
    return u_lit_scene.fog_eye.w * s * exp(-(r - base) / s) * ch;
}

// A sphere's part between the ray's distances t0 and t1, its radius
// monotonic and on one side of the base.
float fog_sphere_part(float3 dir, float t0, float t1) {
    float l = t1 - t0;
    if (!(l > 0.0))
        return 0.0;
    float3 e  = u_lit_scene.fog_eye.xyz;
    float base = u_lit_scene.fog_up_base.w;
    float3 p0 = e + dir * t0;
    float3 p1 = e + dir * t1;
    float3 pm = e + dir * (0.5 * (t0 + t1));
    if (length(pm) < base)
        return u_lit_scene.fog_eye.w * l;
    float r0 = length(p0);
    float r1 = length(p1);
    bool out = r0 <= r1;
    float3 w  = out ? dir : -dir;
    float3 lo = out ? p0 : p1;
    float3 hi = out ? p1 : p0;
    float rl  = out ? r0 : r1;
    float rh  = out ? r1 : r0;
    float mul = rl > 0.0 ? dot(lo, w) / rl : 1.0;
    float muh = rh > 0.0 ? dot(hi, w) / rh : 1.0;
    return max(fog_outward(rl, mul) - fog_outward(rh, muh), 0.0);
}

// A sphere's optical depth between the ray's distances t0 and t1: split at
// the closest point to the centre and at each crossing of the base. A cut
// that doesn't fall strictly inside is t1, its part of no length.
float fog_sphere_tau(float3 dir, float t0, float t1) {
    float3 e   = u_lit_scene.fog_eye.xyz;
    float base = u_lit_scene.fog_up_base.w;
    float b    = dot(e, dir);
    float r    = length(e);
    float c    = (r - base) * (r + base);
    float tm   = -b;
    float k0   = (tm > t0 && tm < t1) ? tm : t1;
    float k1   = t1;
    float k2   = t1;
    float disc = b * b - c;
    if (disc > 0.0) {
        float root = sqrt(disc);
        float q    = -(b + (b >= 0.0 ? root : -root));
        float x2   = q != 0.0 ? c / q : q;
        float lo   = min(q, x2);
        float hi   = max(q, x2);
        k1         = (lo > t0 && lo < t1) ? lo : t1;
        k2         = (hi > t0 && hi < t1) ? hi : t1;
    }
    // The three in order.
    float a0 = min(k0, min(k1, k2));
    float a2 = max(k0, max(k1, k2));
    float a1 = k0 + k1 + k2 - a0 - a2;
    return fog_sphere_part(dir, t0, a0) + fog_sphere_part(dir, a0, a1) + fog_sphere_part(dir, a1, a2) + fog_sphere_part(dir, a2, t1);
}

// Henyey and Greenstein's phase at the cosine between the view and the
// way towards a light. The froxel grid's light too (rbtrend_fog_inject_comp).
public float fog_phase(float cos_theta) {
    float g     = u_lit_scene.fog_albedo.w;
    float denom = 1.0 + g * g - 2.0 * g * cos_theta;
    return (1.0 - g * g) / (4.0 * FOG_PI * denom * sqrt(denom));
}

// The fog along a view ray: its transmittance, and the light it adds.
public struct LitFog {
    public float transmittance;
    public float3 light;
};

// The fog seen from the eye along [dir] (unit) to [distance]: between its
// start and the nearer of the distance and its end, its transmittance
// (floored at 1 - its max opacity) and its light (rbtrendlit.h,
// RBTREND_LIT_FOG); a transmittance of 1 and no light with no fog or none
// of it seen.
public LitFog lit_fog_along(float3 dir, float distance) {
    LitFog f;
    f.transmittance = 1.0;
    f.light         = float3(0.0);
    if (!lit_fog())
        return f;
    float t0 = u_lit_scene.fog_range.x;
    float t1 = min(distance, u_lit_scene.fog_range.y);
    if (!(t1 > t0))
        return f;
    bool plane = any(u_lit_scene.fog_up_base.xyz != float3(0.0));
    float tau  = plane ? fog_plane_tau(dir, t0, t1) : fog_sphere_tau(dir, t0, t1);
    f.transmittance = max(exp(-tau), u_lit_scene.fog_range.z);
    // Its light: the suns' and the ambient light at the middle of the part
    // seen, from its up there.
    float3 sun = float3(0.0);
    uint suns  = min(uint(u_lit_scene.fog_light.x), min(u_lit_scene.directional_count, uint(RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS)));
    for (uint d = 0u; d < suns; ++d) {
        float3 toward = u_lit_scene.directional[d].direction_versine.xyz;
        float3 through = lit_atmosphere_sun_transmittance(d, u_lit_scene.eye_position.xyz);
        sun += FOG_PI * u_lit_scene.directional[d].radiance_disc.rgb * fog_phase(dot(dir, toward)) * through;
    }
    float3 middle = u_lit_scene.fog_eye.xyz + dir * (0.5 * (t0 + t1));
    f.light = u_lit_scene.fog_albedo.rgb * (sun + u_lit_scene.fog_light.y * lit_fog_ambient(lit_fog_up(middle)));
    return f;
}

// The fog's up at [p], a point less the fog's origin: a plane's own, or
// away from a sphere's centre.
public float3 lit_fog_up(float3 p) {
    bool plane = any(u_lit_scene.fog_up_base.xyz != float3(0.0));
    return plane ? u_lit_scene.fog_up_base.xyz : normalize(p);
}

// The scene's ambient light's irradiance / pi from [up]: its hemisphere
// and, where it has one, its environment's irradiance.
public float3 lit_fog_ambient(float3 up) {
    float3 ambient = hemisphere(up);
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_ENVIRONMENT) != 0u)
        ambient += u_lit_scene.environment_intensity * environment_irradiance.SampleLevel(environment_sampler, to_environment(up), 0.0).rgb;
    return ambient;
}

// The fog's density at [p], a point less the fog's origin
// (rbtmathfog.h, DENSITY): its base's below the base, thinning by 1/e a
// scale height above.
public float lit_fog_density(float3 p) {
    bool plane = any(u_lit_scene.fog_up_base.xyz != float3(0.0));
    float y    = (plane ? dot(p, u_lit_scene.fog_up_base.xyz) : length(p)) - u_lit_scene.fog_up_base.w;
    if (y < 0.0)
        return u_lit_scene.fog_eye.w;
    return u_lit_scene.fog_eye.w * exp(-(y / u_lit_scene.fog_range.w));
}

// [color] (linear, before exposure) seen through the fog along [dir] to
// [distance]: color x T + light x (1 - T).
public float3 lit_fog_apply(float3 color, LitFog f) {
    return color * f.transmittance + f.light * (1.0 - f.transmittance);
}
