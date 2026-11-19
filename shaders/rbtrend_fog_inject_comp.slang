// The froxel grid's injection (RBTREND_COMMAND_LIT_FOG, rbtrendfog.h): a
// thread a froxel. At a point of the froxel (its column's ray, the frame's
// jitter of the way across its slice by distance), the extinction is the scene's
// height fog's density (rbtmathfog.h, DENSITY) plus each of its tile's
// volumes' (A FOG VOLUME'S DENSITY, its noise read repeating from the fog's
// WORLEY volume); what scatters, each medium's density times its albedo;
// its light, each of the scene fog's suns (pi x radiance x the fog's phase
// x its cascaded shadow there, one comparison, x the scene atmosphere's
// transmittance there) and the ambient light from the fog's up, times the
// fog's ambient share. Stored as the scattered light (RGB) and the
// extinction (A), blended with last frame's grid where the froxel's middle,
// moved by the space's shift through last frame's view-projection, lands
// inside it.
//
// Set 0 is the lit scene's own set, declared whole; this shader is among
// the shaders the scene's layout is made from, so its bindings are seen by
// compute. Set 1 the view record's (rbtrend_fog_bindings).

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_atmosphere;
import rbtrend_lit_fog;
import rbtrend_fog_grid;
import rbtrend_fog_bindings;

static const float INJECT_PI = 3.14159265359;

// A volume's density at [q], a point of its own frame (rbtmathfog.h).
float fog_volume_density(RbtrendFogVolume v, float3 q) {
    float3 h = v.half_shape.xyz;
    float inside;
    if (v.half_shape.w < 1.5)
        inside = min(min(h.x - abs(q.x), h.y - abs(q.y)), h.z - abs(q.z));
    else
        inside = h.x - length(q);
    if (inside <= 0.0)
        return 0.0;
    float edge = v.params.x;
    float fade = edge > 0.0 ? saturate(inside / edge) : 1.0;
    float n    = 1.0;
    if (v.params.z > 0.0) {
        float3 c = q / v.params.w + v.noise_offset.xyz;
        float4 w = fog_noise.SampleLevel(fog_noise_sampler, c, 0.0);
        n        = 1.0 + v.params.z * (((w.r * 0.625) + (w.g * 0.25)) + (w.b * 0.125) - 1.0);
    }
    return (v.params.y * fade) * n;
}

// How much of directional light [slot] reaches [position]: its cascade's
// one comparison there (no softness, no normal offset), 1 outside its
// cascades, with no atlas, or for a light whose shadow isn't cascades (a
// traced mask is the screen's, not a volume's).
float fog_sun_shadow(uint slot, float3 position) {
    RbtrendLitDirectional d = u_lit_scene.directional[slot];
    uint count              = min(d.shadow_views.z, uint(RBTREND_LIT_MAX_CASCADES));
    if (d.shadow_views.x != RBTREND_LIT_DIRECTIONAL_CASCADES || count == 0u)
        return 1.0;
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_SHADOW_ATLAS) == 0u)
        return 1.0;
    float along  = dot(position - u_lit_scene.eye_position.xyz, u_lit_scene.view_direction.xyz);
    uint cascade = count;
    for (uint i = 0u; i < count; ++i) {
        if (along <= d.splits[i]) {
            cascade = i;
            break;
        }
    }
    uint view = d.shadow_views.y + cascade;
    if (cascade == count || view >= uint(RBTREND_LIT_MAX_SHADOW_VIEWS))
        return 1.0;
    RbtrendLitShadowView sv = u_lit_scene.shadow_views[view];
    float4 clip             = mul(sv.view_projection, float4(position, 1.0));
    if (clip.w <= 0.0)
        return 1.0;
    float3 ndc = clip.xyz / clip.w;
    float2 uv  = float2(ndc.x, -ndc.y) * 0.5 + 0.5;
    if (any(uv < float2(0.0)) || any(uv > float2(1.0)) || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;
    bool reversed = (u_lit_scene.flags & RBTREND_LIT_SCENE_SHADOW_REVERSED) != 0u;
    float depth   = reversed ? ndc.z + d.shadow_params.x : ndc.z - d.shadow_params.x;
    float2 lo     = sv.rect.xy + 0.5;
    float2 hi     = sv.rect.xy + sv.rect.zw - 0.5;
    float2 tap    = clamp(sv.rect.xy + uv * sv.rect.zw, lo, max(lo, hi)) * u_lit_scene.shadow_atlas_size.zw;
    float lit     = reversed ? shadow_atlas.SampleCmpLevelZero(shadow_sampler_reversed, tap, depth) : shadow_atlas.SampleCmpLevelZero(shadow_sampler, tap, depth);
    return lerp(1.0, lit, d.shadow_params.w);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void rbtrend_fog_inject_comp(uint3 id : SV_DispatchThreadID) {
    uint3 size = u_fog.grid.xyz;
    if (any(id >= size))
        return;
    float2 uv     = (float2(id.xy) + 0.5) / float2(size.xy);
    float3 dir    = fog_view_ray(uv);
    // The point: the jitter's share of the way across the slice, by
    // distance (an even share of its length each), the first slice from
    // the eye.
    float before  = id.z == 0u ? 0.0 : fog_slice_distance(float(id.z), u_fog.range.x, u_fog.range.z, u_fog.range.w);
    float after   = fog_slice_distance(float(id.z + 1u), u_fog.range.x, u_fog.range.z, u_fog.range.w);
    float d       = lerp(before, after, u_fog.shift.w);
    float3 p      = u_lit_scene.eye_position.xyz + dir * d;
    float3 fogged = u_lit_scene.fog_eye.xyz + dir * d;

    float extinction = 0.0;
    float3 scatter   = float3(0.0);
    if (lit_fog()) {
        float rho = lit_fog_density(fogged);
        extinction += rho;
        scatter += rho * u_lit_scene.fog_albedo.rgb;
    }
    uint base  = (id.y * size.x + id.x) * FOG_TILE_WORDS;
    uint count = min(FogTiles.Load(base * 4u), FOG_MAX_TILE_VOLUMES);
    for (uint i = 0u; i < count; ++i) {
        RbtrendFogVolume v = u_fog.volumes[FogTiles.Load((base + 1u + i) * 4u)];
        float4 p1          = float4(p, 1.0);
        float3 q           = float3(dot(v.to_volume[0], p1), dot(v.to_volume[1], p1), dot(v.to_volume[2], p1));
        float rho          = fog_volume_density(v, q);
        extinction += rho;
        scatter += rho * v.albedo.rgb;
    }

    float3 light = float3(0.0);
    uint suns    = min(uint(u_lit_scene.fog_light.x), min(u_lit_scene.directional_count, uint(RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS)));
    for (uint s = 0u; s < suns; ++s) {
        float3 toward = u_lit_scene.directional[s].direction_versine.xyz;
        light += INJECT_PI * u_lit_scene.directional[s].radiance_disc.rgb * fog_phase(dot(dir, toward)) * fog_sun_shadow(s, p) *
                 lit_atmosphere_sun_transmittance(s, p);
    }
    light += u_lit_scene.fog_light.y * lit_fog_ambient(lit_fog_up(fogged));
    float4 value = float4(scatter * light, extinction);

    // Last frame's grid at the froxel's middle (its slice coordinate i +
    // 1/2, a texel's middle; not the jittered point, which would blend the
    // slices beside it in), where it holds one.
    float weight = u_fog.last_eye.w;
    if (weight > 0.0) {
        float middle      = fog_slice_distance(float(id.z) + 0.5, u_fog.range.x, u_fog.range.z, u_fog.range.w);
        float3 last_place = u_lit_scene.eye_position.xyz + dir * middle + u_fog.shift.xyz;
        float4 clip       = mul(u_fog.last_view_projection, float4(last_place, 1.0));
        if (clip.w > 0.0) {
            float2 ndc   = clip.xy / clip.w;
            float2 at    = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
            float k      = fog_slice_of(length(last_place - u_fog.last_eye.xyz), u_fog.last_range.x, u_fog.last_range.z, u_fog.last_range.w);
            float w      = k / u_fog.last_range.w;
            if (all(at >= float2(0.0)) && all(at <= float2(1.0)) && w >= 0.0 && w <= 1.0)
                value = lerp(value, fog_history.SampleLevel(fog_history_sampler, float3(at, w), 0.0), weight);
        }
    }
    fog_written[id] = value;
}
