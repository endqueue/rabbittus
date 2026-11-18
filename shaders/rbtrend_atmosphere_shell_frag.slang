// An atmosphere's shell drawn over the picture from wherever the eye is
// (RBTREND_COMMAND_LIT_ATMOSPHERE_SHELLS, rbtrendcommands.h): Hillaire's
// per-pixel march (EGSR 2020, his path for a camera in space) through one
// shell, against the depth the lit surfaces were drawn with, blended over
// the picture by dual-source blending: the first output the light the air
// adds (X), the second what of the texel behind gets through (T), per
// channel, so the texel becomes texel x T + X (colour ONE, SRC1_COLOR;
// alpha ONE, SRC1_ALPHA, the first output's alpha 0 and the second's 1).
//
// Set 0 is the lit scene's own set, declared whole (rbtrend_lit_bindings):
// its block gives the view, the eye, the target's size, the exposure and
// the scene's own atmosphere; its environment_sampler reads every table.
// Set 1 is the shell's atmosphere's block (rbtrend_atmosphere_pass): its
// medium and its tables' sizes. Set 2 the depth (read by texel) and the
// atmosphere's two tables. The shell's own values are push constants.
//
// The view ray is marched from where it enters the shell's top sphere
// (put back on that sphere, so its height is exact far from the eye) or
// from the eye inside it, to where it leaves, meets the ground sphere, or
// meets the drawn surface; a texel whose ray misses the shell, or whose
// surface is nearer than the shell, is left as it was (discarded). Where
// the ray ends at the ground sphere with no surface drawn, the medium's
// ground is drawn there, opaque, lit by the lights through the air. With
// the scene's own air in front (a scene atmosphere with a sky), the
// shell's light comes through that air and the air is kept in front of it:
// X = L_h (1 - T) + T_h L, from the scene's sky-view and transmittance for
// a texel with nothing drawn, its aerial volume at a surface's distance
// otherwise (the pair the lit pass applied there).

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_atmosphere;
import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;

[[vk::binding(0, 1)]] ConstantBuffer<RbtrendAtmospherePass> u_shell_medium : register(b0, space1);

[[vk::binding(0, 2)]] Texture2D<float4> shell_depth : register(t0, space2);
[[vk::binding(1, 2)]] Texture2D<float4> shell_transmittance : register(t1, space2);
[[vk::binding(2, 2)]] Texture2D<float4> shell_scattering : register(t2, space2);

// rbtrend_shell_params in rbtrendatmosphere_internal.hpp: 96 bytes.
struct ShellParams {
    float4 center_lights;        // xyz the centre in the scene's space; w how many lights
    float4 light_direction[2];   // xyz towards each light, unit; w 1 where it lights the shell
    float4 light_illuminance[2]; // rgb pi times its radiance
    uint4 steps;                 // x the least, y the most; z the scene's own air in front; w the depth reversed (near at 1)
};
[[vk::push_constant]] ConstantBuffer<ShellParams> params : register(b0, space4);

struct FragmentOut {
    [[vk::location(0)]]               float4 color : SV_Target0;
    [[vk::location(0), vk::index(1)]] float4 blend : SV_Target1;
};

// How far along the ray from the eye [e] along [d] the depth [z] is: from
// the view-projection's last two rows, where z (row 2 over row 3) of
// e + t d is [z]; the view's linear depth over the ray's cosine with its
// axis, for a perspective or an orthographic projection alike.
float surface_distance(float3 e, float3 d, float z) {
    float4 row2 = u_lit_scene.view_projection[2];
    float4 row3 = u_lit_scene.view_projection[3];
    float4 e4   = float4(e, 1.0);
    float4 d4   = float4(d, 0.0);
    return (dot(row2, e4) - z * dot(row3, e4)) / (z * dot(row3, d4) - dot(row2, d4));
}

[shader("fragment")]
FragmentOut rbtrend_atmosphere_shell_frag(float4 frag_coord : SV_Position) {
    // The texel's ray, the first row at the top, through the inverse
    // view-projection: the direction between its points at depths 1 and 0
    // taken homogeneously, so a point at infinity (an infinite far plane)
    // gives a direction, not a division by 0.
    float2 ndc = frag_coord.xy * u_lit_scene.target_size.zw * 2.0 - 1.0;
    ndc.y      = -ndc.y;
    float4 a   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 b   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    float3 d   = normalize(b.xyz * a.w - a.xyz * b.w);
    // Near at 0 or at 1: either way the ray runs away from the eye.
    if (dot(d, u_lit_scene.view_direction.xyz) < 0.0)
        d = -d;
    float3 eye         = u_lit_scene.eye_position.xyz;
    float3 o           = eye - params.center_lights.xyz;
    AtmosphereMedium m = block_medium(u_shell_medium);

    float3 top = sphere_hits_stable(o, d, m.top_radius);
    if (top.z < 0.0 || top.y <= 0.0)
        discard;
    // Outside: from the entry, put back on the top sphere, to the exit
    // (the chord from a point on a sphere, -2 dot(p, d)). Inside: from the
    // eye.
    float3 origin = o;
    float entry   = 0.0;
    float end     = top.y;
    if (top.x > 0.0) {
        origin = normalize(o + d * top.x) * m.top_radius;
        entry  = top.x;
        end    = max(-2.0 * dot(origin, d), 0.0);
    }

    float z        = shell_depth.Load(int3(int2(frag_coord.xy), 0)).r;
    float far      = params.steps.w != 0u ? 0.0 : 1.0;
    bool surface   = z != far;
    float distance = 0.0;
    bool ground    = false;
    if (surface) {
        distance = surface_distance(eye, d, z);
        if (distance <= entry)
            discard;
        end = min(end, distance - entry);
    } else {
        float3 g = sphere_hits_stable(origin, d, m.bottom_radius);
        if (g.z > 0.0 && g.x > 0.0 && g.x < end) {
            end    = g.x;
            ground = true;
        }
    }

    // More steps for a longer part of the ray in the shell, the most at its
    // longest chord that misses the ground.
    float longest = 2.0 * sqrt(max(m.top_radius * m.top_radius - m.bottom_radius * m.bottom_radius, 0.0));
    float least   = float(params.steps.x);
    float most    = float(params.steps.y);
    uint steps    = uint(max(floor(least + (most - least) * saturate(end / longest)), 1.0));
    float2 tsize  = u_shell_medium.sizes.xy;

    AtmosphereMarch march = atmosphere_march(m, shell_transmittance, environment_sampler, shell_scattering, environment_sampler, tsize,
                                             u_shell_medium.more_sizes.x, params.light_direction, params.light_illuminance, origin, d, 0.0, end,
                                             steps, true);
    float3 light         = march.light;
    float3 transmittance = march.transmittance;
    if (ground) {
        // The ground's light, its transmittance read at the ground's radius
        // (rbtrend_atmosphere_model's ground_transmittance).
        float3 n = normalize(origin + d * end);
        for (uint k = 0u; k < 2u; ++k) {
            if (params.light_direction[k].w < 0.5)
                continue;
            float3 sun = params.light_direction[k].xyz;
            float3 lit = ground_transmittance(m, shell_transmittance, environment_sampler, tsize, n, sun);
            light += transmittance * params.light_illuminance[k].rgb * lit * saturate(dot(n, sun)) * m.ground_albedo / PI;
        }
        transmittance = float3(0.0);
    }

    // Behind the scene's own air: L_h (1 - T) + T_h L.
    float3 added = light;
    if (params.steps.z != 0u) {
        float3 air_light, air_through;
        if (surface) {
            LitAir air  = lit_atmosphere_air(distance, frag_coord.xy);
            air_light   = air.light;
            air_through = air.transmittance;
        } else {
            air_light   = lit_atmosphere_sky(d);
            air_through = lit_atmosphere_transmittance(eye, d);
        }
        added = air_light * (float3(1.0) - transmittance) + air_through * light;
    }

    FragmentOut result;
    result.color = float4(added * u_lit_scene.exposure, 0.0);
    result.blend = float4(transmittance, 1.0);
    return result;
}
