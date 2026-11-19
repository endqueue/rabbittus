// The clouds' march (RBTREND_COMMAND_LIT_CLOUDS, rbtrendclouds.h): a thread
// a march texel, a quarter of the target each way. Its ray goes through
// the centre of this frame's target texel of its 4 x 4 block (a fixed order
// of the sixteen, rbtrendhistory.hpp), from the eye to the nearest of the
// max distance and the surface the depth holds.
// Each layer's bounds meet it in at most two segments, sorted by their
// start (ties by the layer's order); the samples, at most the max steps,
// spread over their whole length (each segment at least one), the first of
// each offset along it by the history texel's visit's offset, rotated by a
// noise of the march texel. At a sample: the layer's
// density (rbtrend_clouds_model, Math's definition), its extinction and
// scattering; each of its lights marched towards over the light steps
// (spaced growing by 2 over the layer's thickness, every layer's density
// summed), dimmed by its atmosphere's transmittance where it names one,
// through Wrenninge's octaves and the look's two Henyey-Greenstein lobes;
// the scene's ambient light from the layer's down to its up by the
// sample's height. Integrated by Hillaire's energy-conserving step; the
// march stops where the transmittance is below 2^-10. Written: the light
// and the transmittance, and the transmittance-weighted mean distance of
// what was gathered (the segments' first start where nothing was; the max
// distance where there are none).
//
// Set 0 is the lit scene's own set, declared whole; this shader is among
// the shaders the scene's layout is made from, so its bindings are seen by
// compute. Set 1 the input (rbtrend_clouds_input), set 2 the clouds' own and
// set 3 the view record's (rbtrend_clouds_bindings).

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_fog;
import rbtrend_atmosphere_model;
import rbtrend_clouds_model;
import rbtrend_clouds_input;
import rbtrend_clouds_bindings;

static const float CLOUDS_PI = 3.14159265359;
static const uint CLOUDS_MAX_SEGMENTS = 8u;

float clouds_hg(float g, float c) {
    float d = 1.0 + g * g - 2.0 * g * c;
    return (1.0 - g * g) / (4.0 * CLOUDS_PI * d * sqrt(max(d, 1e-8)));
}

float clouds_phase(CloudLook k, float c, float scale) {
    return k.phase.z * clouds_hg(k.phase.x * scale, c) + (1.0 - k.phase.z) * clouds_hg(k.phase.y * scale, c);
}

// Layer [i]'s atmosphere's transmittance from [p] (the scene's space)
// towards [toward].
float3 clouds_air(uint i, CloudLayer l, float3 p, float3 toward) {
    if (l.atmosphere.w < 0.5)
        return float3(1.0);
    AtmosphereMedium m;
    m.bottom_radius            = l.atmosphere_radii.x;
    m.top_radius               = l.atmosphere_radii.y;
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
    float3 q    = p - l.atmosphere.xyz;
    float2 size = l.atmosphere_radii.zw;
    if (i == 0u)
        return transmittance_towards(m, clouds_table0, clouds_clamp, size, q, toward);
    if (i == 1u)
        return transmittance_towards(m, clouds_table1, clouds_clamp, size, q, toward);
    if (i == 2u)
        return transmittance_towards(m, clouds_table2, clouds_clamp, size, q, toward);
    return transmittance_towards(m, clouds_table3, clouds_clamp, size, q, toward);
}

// Every layer's extinction at [p] (the scene's space).
float clouds_extinction_at(float3 p) {
    float sum = 0.0;
    for (uint i = 0u; i < u_clouds_march.counts.x; ++i) {
        CloudLayer l = u_clouds_march.layers[i];
        CloudLook k  = u_clouds_looks.looks[uint(l.up_look.w)];
        float rho    = cloud_density(l, k, cloud_local(l, p), clouds_base, clouds_detail, clouds_maps, clouds_repeat);
        sum += rho * k.albedo.w;
    }
    return sum;
}

// The layer's up at [p] (the scene's space): a shell's away from its
// centre, a slab's own.
float3 clouds_up(CloudLayer l, float3 p) {
    if (l.origin_kind.w < 1.5)
        return normalize(p - l.origin_kind.xyz);
    float3 u = l.up_look.xyz;
    return normalize(l.axes[0].xyz * u.x + l.axes[1].xyz * u.y + l.axes[2].xyz * u.z);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rbtrend_clouds_march_comp(uint3 id : SV_DispatchThreadID) {
    if (id.x >= u_clouds_march.sizes.x || id.y >= u_clouds_march.sizes.y)
        return;
    // Through this frame's target texel of the 4 x 4 block (its centre,
    // clamped to the target's last).
    float2 size  = u_clouds_march.target.xy;
    float2 pixel = min(float2(id.xy * 4u) + u_clouds_march.jitter.xy, size - 0.5);
    // The visit's step offset rotated by the march texel's interleaved
    // gradient noise (Jimenez 2014), so neighbouring texels are offset
    // differently within a frame.
    float2 m       = float2(id.xy);
    float rotation = frac(52.9829189 * frac(0.06711056 * m.x + 0.00583715 * m.y));
    float offset   = frac(u_clouds_march.jitter.z + rotation);
    float2 uv    = pixel / size;
    float2 ndc   = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 a     = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 b     = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    float3 dir   = normalize(b.xyz * a.w - a.xyz * b.w);
    if (dot(dir, u_lit_scene.view_direction.xyz) < 0.0)
        dir = -dir;
    float3 eye = u_lit_scene.eye_position.xyz;

    // The nearest of the max distance and the surface.
    float reach     = u_clouds_march.target.z;
    float z         = clouds_depth.Load(int3(int2(pixel), 0)).r;
    float far_value = u_clouds_march.target.w > 0.5 ? 0.0 : 1.0;
    if (z != far_value) {
        float4 w = mul(u_lit_scene.inverse_view_projection, float4(ndc, z, 1.0));
        if (w.w != 0.0)
            reach = min(reach, length(w.xyz / w.w - eye));
    }

    // The segments, sorted by their start.
    float2 seg[CLOUDS_MAX_SEGMENTS];
    uint seg_layer[CLOUDS_MAX_SEGMENTS];
    for (uint k = 0u; k < CLOUDS_MAX_SEGMENTS; ++k) {
        seg[k]       = float2(0.0);
        seg_layer[k] = 0u;
    }
    uint count = 0u;
    for (uint i = 0u; i < u_clouds_march.counts.x; ++i) {
        CloudLayer l = u_clouds_march.layers[i];
        float3 o     = cloud_local(l, eye);
        float3 d     = cloud_local_direction(l, dir);
        float2 found[2];
        found[0] = float2(0.0);
        found[1] = float2(0.0);
        uint n = 0u;
        if (l.origin_kind.w < 1.5) {
            float3 outer = sphere_hits_stable(o, d, l.bounds.y);
            if (outer.z >= 0.0) {
                float3 inner = sphere_hits_stable(o, d, l.bounds.x);
                if (inner.z >= 0.0 && inner.y > outer.x && inner.x < outer.y) {
                    found[0] = float2(outer.x, inner.x);
                    found[1] = float2(inner.y, outer.y);
                    n        = 2u;
                } else {
                    found[0] = outer.xy;
                    n        = 1u;
                }
            }
        } else {
            float y  = dot(o, l.up_look.xyz);
            float dy = dot(d, l.up_look.xyz);
            if (abs(dy) < 1e-8) {
                if (y >= l.bounds.x && y <= l.bounds.y) {
                    found[0] = float2(0.0, 1e30);
                    n        = 1u;
                }
            } else {
                float t0 = (l.bounds.x - y) / dy;
                float t1 = (l.bounds.y - y) / dy;
                found[0] = float2(min(t0, t1), max(t0, t1));
                n        = 1u;
            }
        }
        for (uint j = 0u; j < n; ++j) {
            float2 s = float2(max(found[j].x, 0.0), min(found[j].y, reach));
            if (!(s.y > s.x))
                continue;
            uint at = count;
            while (at > 0u && seg[at - 1u].x > s.x) {
                seg[at]       = seg[at - 1u];
                seg_layer[at] = seg_layer[at - 1u];
                --at;
            }
            seg[at]       = s;
            seg_layer[at] = i;
            ++count;
        }
    }

    float3 light  = float3(0.0);
    float through = 1.0;
    float depth   = count > 0u ? seg[0].x : u_clouds_march.target.z;
    float gathered_distance = 0.0;
    float gathered          = 0.0;
    float total = 0.0;
    for (uint s = 0u; s < count; ++s)
        total += seg[s].y - seg[s].x;
    uint max_steps   = u_clouds_march.counts.y;
    uint light_steps = u_clouds_march.counts.z;
    uint taken       = 0u;
    for (uint s = 0u; s < count && through >= 0.0009765625 && taken < max_steps; ++s) {
        CloudLayer l = u_clouds_march.layers[seg_layer[s]];
        CloudLook k  = u_clouds_looks.looks[uint(l.up_look.w)];
        float len    = seg[s].y - seg[s].x;
        uint n       = max(1u, uint(float(max_steps) * len / max(total, 1e-30)));
        n            = min(n, max_steps - taken);
        float dt     = len / float(n);
        float thick  = l.bounds.y - l.bounds.x;
        for (uint q = 0u; q < n && through >= 0.0009765625; ++q) {
            ++taken;
            float t  = seg[s].x + (float(q) + offset) * dt;
            float3 p = eye + dir * t;
            float3 lp = cloud_local(l, p);
            float rho = cloud_density(l, k, lp, clouds_base, clouds_detail, clouds_maps, clouds_repeat);
            if (rho <= 0.0)
                continue;
            float sigma_t   = rho * k.albedo.w;
            float3 sigma_s  = sigma_t * k.albedo.rgb;
            float3 sun      = float3(0.0);
            for (uint j = 0u; j < 2u; ++j) {
                if (l.light_direction[j].w < 0.5)
                    continue;
                float3 toward = l.light_direction[j].xyz;
                float tau     = 0.0;
                if (light_steps > 0u) {
                    float step0 = thick / (float((1u << light_steps) - 1u));
                    float along = 0.0;
                    for (uint m = 0u; m < light_steps; ++m) {
                        float ls = step0 * float(1u << m);
                        tau += clouds_extinction_at(p + toward * (along + 0.5 * ls)) * ls;
                        along += ls;
                    }
                }
                float c       = dot(dir, toward);
                float3 lit    = l.light_illuminance[j].rgb * clouds_air(seg_layer[s], l, p, toward);
                float octaves = 0.0;
                float an = 1.0, bn = 1.0, cn = 1.0;
                for (uint o = 0u; o < uint(k.phase.w); ++o) {
                    octaves += bn * exp(-an * tau) * clouds_phase(k, c, cn);
                    an *= k.octave.x;
                    bn *= k.octave.y;
                    cn *= k.octave.z;
                }
                sun += lit * octaves;
            }
            float hgt     = saturate(cloud_height(l, lp));
            float3 up     = clouds_up(l, p);
            float3 amb    = lerp(lit_fog_ambient(-up), lit_fog_ambient(up), hgt) * k.octave.w;
            float3 S      = sigma_s * (sun + amb);
            float step_t  = exp(-sigma_t * dt);
            light += through * S * ((1.0 - step_t) / sigma_t);
            float lost = through * (1.0 - step_t);
            gathered_distance += lost * t;
            gathered += lost;
            through *= step_t;
        }
    }
    if (through < 0.0009765625)
        through = 0.0;
    if (gathered > 0.0)
        depth = gathered_distance / gathered;
    clouds_march_out[id.xy]       = float4(light, through);
    clouds_march_depth_out[id.xy] = depth;
}
