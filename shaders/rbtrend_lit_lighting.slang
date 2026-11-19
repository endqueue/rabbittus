module rbtrend_lit_lighting;

// The lighting of Rend's lit surfaces (rbtrendlit.h): what a surface's
// reads hand it - its base color, opacity, shading normal, roughness,
// metalness, specular, occlusion, emission, clear coat, sheen,
// transmission and its answer to the scene's cover, in an LitSurface - lit
// under the scene's directional lights with cascaded or traced shadows, its
// point and spot lights with shadows from the atlas, a two-color
// hemisphere, an environment and ambient occlusion, then faded into the
// scene's air and fog, exposed and encoded. One lighting for every surface
// kind: the lit material's (rbtrend_lit_surface.slang) and the layered
// material's (rbtrend_lit_layered.slang) each read their own set 1 into a
// LitSurface and call lit_light(). It imports no material: a shader
// declares each set exactly as its layout holds it, and the two surfaces'
// sets 1 differ.
//
// Direct light: Cook-Torrance with GGX, Smith-Schlick (k = (r + 1)^2 / 8,
// for analytic lights) and Schlick's Fresnel, as Unreal's default lit
// shading and Hazel's. A clear coat is a second GGX lobe of F0 0.04 over the
// base, with Kelemen's visibility, taking its share of the light from it;
// sheen is the Charlie distribution with Neubelt's visibility (Estevez and
// Kulla; Filament's and glTF's KHR_materials_sheen). Point and spot lights
// fall off by the inverse square, windowed to 0 at their range (Karis, "Real
// Shading in Unreal Engine 4"); with the view's tiles they are read from the
// tile's list, not all of them. Ambient light: the hemisphere, plus the
// environment's irradiance and its prefiltered specular mips, the specular
// part weighted by the split sum's scale and bias from Rend's BRDF lookup
// texture; occlusion (the surface's and the view's) darkens it alone.
//
// Light is scaled so a white, matte face square to a light reads the
// light's color: the diffuse part is albedo times radiance times the cosine,
// and the specular parts the BRDF times pi to match.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_atmosphere;
import rbtrend_lit_fog;

// The position first, then the five inputs, in the order a vertex shader
// writes them (rbtrend_lit_vert.slang), so D3D12 matches each by register.
public struct LitFragmentInput {
    public float4 frag_coord : SV_Position;
    [[vk::location(0)]] public float3 position : TEXCOORD0;
    [[vk::location(1)]] public float3 normal : TEXCOORD1;
    [[vk::location(2)]] public float2 uv : TEXCOORD2;
    [[vk::location(3)]] public float4 tangent : TEXCOORD3;
    [[vk::location(4)]] public float4 color : TEXCOORD4;
};

static const float PI = 3.14159265359;
// Below this a GGX highlight is too small to sample; Filament's floor.
public static const float MIN_ROUGHNESS = 0.045;
// Below this the Charlie distribution's peak runs away.
static const float MIN_SHEEN_ROUGHNESS = 0.07;
// The share of light a sheen takes back from the layer under it, on
// average over its roughness: Charlie's directional albedo, about 0.18.
static const float SHEEN_ALBEDO = 0.18;

float3 safe_normalize(float3 v, float3 fallback) {
    float l = length(v);
    return l > 0.0 ? v / l : fallback;
}

float max3(float3 v) { return max(v.r, max(v.g, v.b)); }

float ggx(float n_dot_h, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float smith_schlick(float n_dot_l, float n_dot_v, float roughness) {
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return (n_dot_l / (n_dot_l * (1.0 - k) + k)) * (n_dot_v / (n_dot_v * (1.0 - k) + k));
}

float3 fresnel(float3 f0, float cos_theta) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

float charlie(float n_dot_h, float roughness) {
    float a     = roughness * roughness;
    float inv_a = 1.0 / a;
    float sin2  = max(1.0 - n_dot_h * n_dot_h, 0.0);
    return (2.0 + inv_a) * pow(sin2, inv_a * 0.5) / (2.0 * PI);
}

float neubelt(float n_dot_l, float n_dot_v) {
    return 1.0 / (4.0 * (n_dot_l + n_dot_v - n_dot_l * n_dot_v));
}

// The split sum's scale and bias at [n_dot_v] and [roughness].
float2 split_sum(float n_dot_v, float roughness) {
    return brdf_lut.SampleLevel(environment_sampler, float2(n_dot_v, roughness), 0.0).rg;
}

// What a surface is made of, worked out once for every light.
struct Surface {
    float3 n;          // shading normal, normal map applied
    float3 geometric;  // the face's own, turned to the eye
    float3 v;
    float n_dot_v;
    float3 diffuse;
    float3 f0;
    float roughness;
    float clearcoat;
    float clearcoat_roughness;
    float3 sheen;
    float sheen_roughness;
};

// What the surface sends towards the eye of light arriving from [l] with
// [radiance].
float3 shade(Surface s, float3 l, float3 radiance) {
    float n_dot_l = dot(s.n, l);
    float3 h      = normalize(l + s.v);
    float v_dot_h = max(dot(s.v, h), 0.0);
    float3 color  = float3(0.0);
    if (n_dot_l > 0.0) {
        float n_dot_h     = max(dot(s.n, h), 0.0);
        float3 f          = fresnel(s.f0, v_dot_h);
        float3 specular   = f * ggx(n_dot_h, s.roughness) * smith_schlick(n_dot_l, s.n_dot_v, s.roughness) / (4.0 * n_dot_l * s.n_dot_v);
        float sheen_scale = 1.0 - max3(s.sheen) * SHEEN_ALBEDO;
        color             = ((1.0 - f) * s.diffuse + PI * specular) * sheen_scale;
        if (max3(s.sheen) > 0.0)
            color += PI * s.sheen * charlie(n_dot_h, s.sheen_roughness) * neubelt(n_dot_l, s.n_dot_v);
        color *= n_dot_l;
    }
    if (s.clearcoat > 0.0) {
        // On the face's own normal: a coat is smooth over any bumps below.
        float c_dot_l = dot(s.geometric, l);
        if (c_dot_l > 0.0) {
            float c_dot_h = max(dot(s.geometric, h), 0.0);
            float fc      = fresnel(float3(0.04), v_dot_h).x * s.clearcoat;
            float coat    = fc * ggx(c_dot_h, s.clearcoat_roughness) * 0.25 / max(v_dot_h * v_dot_h, 1e-4);
            color         = color * (1.0 - fc) + float3(PI * coat * c_dot_l);
        } else {
            color *= 1.0 - fresnel(float3(0.04), v_dot_h).x * s.clearcoat;
        }
    }
    return color * radiance;
}

// How much of a light gets past view [view] of the atlas, with [params]
// (bias, normal offset, softness, strength): 1 outside the view, or with no
// atlas. The view was rendered through its matrix into its rectangle with
// clip space's +Y up its first row: its rows run down from clip y = +1, so
// y is negated into the rectangle. Every tap is clamped half a texel inside
// the rectangle, so the hardware's four-texel comparison reads nothing of a
// neighbouring view.
float shadow_visibility(uint view, float4 params, float3 position, float3 n) {
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_SHADOW_ATLAS) == 0u || view >= uint(RBTREND_LIT_MAX_SHADOW_VIEWS))
        return 1.0;
    RbtrendLitShadowView v = u_lit_scene.shadow_views[view];
    float4 clip = mul(v.view_projection, float4(position + n * params.y, 1.0));
    if (clip.w <= 0.0)
        return 1.0;
    float3 ndc = clip.xyz / clip.w;
    float2 uv  = float2(ndc.x, -ndc.y) * 0.5 + 0.5;
    if (any(uv < float2(0.0)) || any(uv > float2(1.0)) || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;
    bool reversed = (u_lit_scene.flags & RBTREND_LIT_SCENE_SHADOW_REVERSED) != 0u;
    float depth   = reversed ? ndc.z + params.x : ndc.z - params.x;
    float2 inv    = u_lit_scene.shadow_atlas_size.zw;
    float2 at     = v.rect.xy + uv * v.rect.zw;
    float2 lo     = v.rect.xy + 0.5;
    float2 hi     = v.rect.xy + v.rect.zw - 0.5;
    float lit     = 0.0;
    float taps    = params.z <= 0.0 ? 0.0 : 1.0;
    float count   = 0.0;
    // Each tap is the hardware's four-texel comparison, filtered; nine of
    // them [softness] texels apart soften the edge further.
    for (float y = -taps; y <= taps; y += 1.0) {
        for (float x = -taps; x <= taps; x += 1.0) {
            float2 tap = clamp(at + float2(x, y) * params.z, lo, max(lo, hi)) * inv;
            lit += reversed ? shadow_atlas.SampleCmpLevelZero(shadow_sampler_reversed, tap, depth)
                            : shadow_atlas.SampleCmpLevelZero(shadow_sampler, tap, depth);
            count += 1.0;
        }
    }
    return lerp(1.0, lit / count, params.w);
}

// The tenth of each cascade's stretch, at its far end, over which it is
// blended into the next (the last into no shadow).
static const float CASCADE_BLEND = 0.1;

// How much of directional light [slot] reaches [position], at [frag] on the
// target: its traced mask there, if its shadow is one; or the cascade its
// distance along the view falls in, blended into the next over the last
// tenth of the cascade's stretch so no seam shows between them, the last
// faded out to no shadow over its own.
float directional_visibility(uint slot, float3 position, float3 n, float2 frag) {
    RbtrendLitDirectional d = u_lit_scene.directional[slot];
    uint kind = d.shadow_views.x;
    if (kind == RBTREND_LIT_DIRECTIONAL_MASK) {
        float m = directional_mask(slot, int2(frag));
        return 1.0 - d.shadow_params.w * (1.0 - m);
    }
    uint count = min(d.shadow_views.z, uint(RBTREND_LIT_MAX_CASCADES));
    if (kind != RBTREND_LIT_DIRECTIONAL_CASCADES || count == 0u)
        return 1.0;
    uint first   = d.shadow_views.y;
    float along  = dot(position - u_lit_scene.eye_position.xyz, u_lit_scene.view_direction.xyz);
    uint cascade = count;
    for (uint i = 0u; i < count; ++i) {
        if (along <= d.splits[i]) {
            cascade = i;
            break;
        }
    }
    if (cascade == count)
        return 1.0;
    float lit   = shadow_visibility(first + cascade, d.shadow_params, position, n);
    float begin = cascade == 0u ? 0.0 : d.splits[cascade - 1u];
    float end   = d.splits[cascade];
    float blend = smoothstep(end - CASCADE_BLEND * (end - begin), end, along);
    if (blend > 0.0) {
        float next = 1.0;
        if (cascade + 1u < count)
            next = shadow_visibility(first + cascade + 1u, d.shadow_params, position, n);
        lit = lerp(lit, next, blend);
    }
    return lit;
}

// [position]: the fragment's, in_position as the vertex shader wrote it.
float3 point_or_spot(Surface s, RbtrendLitLight light, float3 position) {
    float3 to_light = light.position_range.xyz - position;
    float distance2 = max(dot(to_light, to_light), 1e-8);
    float distance  = sqrt(distance2);
    float range     = light.position_range.w;
    if (distance >= range)
        return float3(0.0);
    float3 l     = to_light / distance;
    float ratio  = distance / range;
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    float fall   = window * window / distance2;
    bool spot    = light.radiance_type.w > 0.5;
    if (spot) {
        float cos_axis = dot(-l, light.direction_inner.xyz);
        float cos_in   = light.direction_inner.w;
        float cos_out  = light.outer_shadow.x;
        float cone     = clamp((cos_axis - cos_out) / max(cos_in - cos_out, 1e-4), 0.0, 1.0);
        fall *= cone * cone;
    }
    int shadow = int(light.outer_shadow.y);
    if (fall > 0.0 && shadow >= 0 && shadow < RBTREND_LIT_MAX_SHADOW_VIEWS) {
        RbtrendLitShadow record = u_lit_scene.shadows[shadow];
        uint view              = record.views.x;
        if (!spot) {
            // A point light's shadow: the cube face the light looks along
            // to reach this fragment, by the largest of the offset's axes.
            float3 d = position - light.position_range.xyz;
            float3 a = abs(d);
            view += a.x >= a.y && a.x >= a.z ? (d.x >= 0.0 ? 0u : 1u) : (a.y >= a.z ? (d.y >= 0.0 ? 2u : 3u) : (d.z >= 0.0 ? 4u : 5u));
        }
        fall *= shadow_visibility(view, record.params, position, s.geometric);
    }
    return fall > 0.0 ? shade(s, l, light.radiance_type.rgb * fall) : float3(0.0);
}

// What the opaque picture shows through the surface at [position]: the
// view ray bent into the surface by its index of refraction, followed
// through its thickness, and looked up where that point falls on the
// screen, blurrier (a lower mip) the rougher the surface; tinted by the
// distance travelled (Beer's law, glTF's KHR_materials_volume). Unexposed,
// as every other light here is until the end. [frag_coord]: the
// fragment's SV_Position.
float3 transmitted(Surface s, LitSurface m, float3 position, float4 frag_coord) {
    float ior       = max(m.ior, 1.0);
    float thickness = max(m.thickness, 0.0);
    float3 inside   = refract(-s.v, s.n, 1.0 / ior);
    float3 exit     = position + inside * thickness;
    float4 clip     = mul(u_lit_scene.view_projection, float4(exit, 1.0));
    float2 uv       = frag_coord.xy * u_lit_scene.target_size.zw;
    if (clip.w > 0.0) {
        // Carrottus GPU's clip space: +Y up the target, so y negated into uv,
        // whose rows run down as SV_Position's do.
        float2 ndc = clip.xy / clip.w;
        uv         = float2(ndc.x, -ndc.y) * 0.5 + 0.5;
    }
    // three.js's choice of mip: the roughness, sharpened as the index nears 1.
    float rough   = s.roughness * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
    float lod     = rough * max(u_lit_scene.scene_color_mips - 1.0, 0.0);
    float3 behind = scene_color.SampleLevel(environment_sampler, uv, lod).rgb;
    if (u_lit_scene.output_encoding == 1u) // written encoded: back to linear
        behind = select(behind <= float3(0.04045), behind / 12.92, pow((behind + 0.055) / 1.055, float3(2.4)));
    behind /= max(u_lit_scene.exposure, 1e-20);
    float distance = m.attenuation_distance;
    if (distance > 0.0)
        behind *= pow(max(m.attenuation, float3(0.0)), float3(thickness / distance));
    return behind;
}

float cover_smoothstep(float t) {
    return t * t * (3.0 - 2.0 * t);
}

// The scene's cover over a surface that answers it (rbtrendlit.h,
// RBTREND_LIT_COVER): wet first (Lagarde's porous model: the albedo
// darkened by the porosity, the roughness lowered by the water's film, on
// the share [responds] of the surface that answers), then snow over it by
// the geometric normal's slope and the height against the snow line,
// blending the albedo, roughness, metalness and shading normal towards the
// snow's by [amount]. [geometric] is the face's normal turned to the eye.
// A material answers whole ([responds] 1); a layered surface's share is
// its layers' that answer, by their weights.
void lit_cover(float3 position, float3 geometric, float amount, float porosity, float responds, inout float3 albedo, inout float roughness,
               inout float metalness, inout float3 n) {
    float4 frame = u_lit_scene.cover_frame;
    float4 snow  = u_lit_scene.cover_snow;
    float4 wet   = u_lit_scene.cover_wet;
    albedo *= lerp(1.0, 0.2, wet.x * porosity);
    roughness = lerp(roughness, wet.y, wet.x * responds);
    float3 up;
    float height;
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_COVER_ROUND) != 0u) {
        float3 d = position - frame.xyz;
        height   = length(d);
        up       = height > 0.0 ? d / height : float3(0.0);
    } else {
        up     = frame.xyz;
        height = dot(position, up);
    }
    float slope = cover_smoothstep(saturate((dot(geometric, up) - snow.x) * snow.y));
    float above = cover_smoothstep(saturate((height - frame.w) * snow.z));
    float s     = snow.w * slope * above * amount;
    if (s > 0.0) {
        uint rg          = asuint(wet.z);
        uint ba          = asuint(wet.w);
        float3 color     = float3(f16tof32(rg & 0xffffu), f16tof32(rg >> 16), f16tof32(ba & 0xffffu));
        albedo           = lerp(albedo, color, s);
        roughness        = lerp(roughness, f16tof32(ba >> 16), s);
        metalness        = metalness * (1.0 - s);
        n                = safe_normalize(lerp(n, geometric, s), geometric);
    }
    roughness = clamp(roughness, MIN_ROUGHNESS, 1.0);
}

// What lit_shade() works out for a fragment: the picture's color
// (premultiplied by the opacity), and for the reflecting variant the
// shading normal, the roughness, and the weight the environment's specular
// light reaches the picture with (premultiplied likewise).
public struct LitShaded {
    public float4 color;
    public float3 normal;
    public float roughness;
    public float3 specular;
};


// What a surface's reads hand the lighting, worked out from its material
// (or its layers) at the fragment. Values are as the material holds them
// where the lighting clamps them itself (specular, the clear coat, the
// sheen, the transmission), already clamped where the reads do (roughness
// and metalness, the albedo at 0).
public struct LitSurface {
    public float3 albedo;              // the base color, linear, at least 0
    public float opacity;              // 0 to 1
    public float3 v;                   // towards the eye (lit_frame())
    public float3 geometric;           // the face's own normal, turned to the eye (lit_frame())
    public float3 n;                   // the shading normal, normal map applied
    public float roughness;            // MIN_ROUGHNESS to 1
    public float metalness;            // 0 to 1
    public float specular;             // a dielectric's reflectance, 0.5 is F0 0.04
    public float occlusion;            // the surface's own, 0 to 1
    public float3 emission;            // linear, at least 0
    public float clearcoat;
    public float clearcoat_roughness;
    public float3 sheen;
    public float sheen_roughness;
    public float transmission;         // the share of light that passes through
    public float ior;
    public float thickness;
    public float attenuation_distance; // 0 for never
    public float3 attenuation;
    public float cover_amount;         // how much of the scene's snow lies on it
    public float cover_porosity;       // how porous it is, for the scene's wetness
    public float cover_responds;       // the share of it that answers the cover; 0: none
};

// The eye's direction and the face's own normal turned to it, for
// [input]: what both surfaces' normal maps start from. A face seen from
// behind is lit as its front, and one with no normal faces the eye.
public struct LitFrame {
    public float3 v;
    public float3 geometric;
};

public LitFrame lit_frame(LitFragmentInput input) {
    LitFrame f;
    f.v         = safe_normalize(u_lit_scene.eye_position.xyz - input.position, safe_normalize(input.normal, float3(0.0, 0.0, 1.0)));
    f.geometric = safe_normalize(input.normal, f.v);
    if (dot(f.geometric, f.v) < 0.0)
        f.geometric = -f.geometric;
    return f;
}

// [tn], a tangent-space normal read from a map (z up, -1 to 1, its xy
// already scaled), into the frame of [tangent] (xyz along the texture's u,
// w the bitangent's sign: MikkTSpace's) about the eye-facing normal [n]:
// [n] itself where the tangent has no length or lies along it.
public float3 lit_tangent_normal(float3 n, float4 tangent, float3 tn) {
    if (dot(tangent.xyz, tangent.xyz) > 0.0) {
        float3 t = tangent.xyz - n * dot(n, tangent.xyz);
        if (dot(t, t) > 0.0) {
            t        = normalize(t);
            float3 b = cross(n, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
            return safe_normalize(t * tn.x + b * tn.y + n * tn.z, n);
        }
    }
    return n;
}

// [s] lit at [input], as both lit surface fragment shaders write it.
public LitShaded lit_light(LitFragmentInput input, LitSurface m) {
    float3 in_position = input.position;
    // The view's bent normal, if the scene reads it: 0 for none.
    float occlusion    = m.occlusion;
    float3 bent_normal = float3(0.0);
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_OCCLUSION) != 0u) {
        float4 view_occlusion = ambient_occlusion.SampleLevel(environment_sampler, input.frag_coord.xy * u_lit_scene.target_size.zw, 0.0);
        occlusion *= lerp(1.0, view_occlusion.r, clamp(u_lit_scene.occlusion_strength, 0.0, 1.0));
        if ((u_lit_scene.flags & RBTREND_LIT_SCENE_BENT_NORMALS) != 0u)
            bent_normal = view_occlusion.gba * 2.0 - 1.0;
    }
    float3 emission = m.emission;
    float opacity   = m.opacity;

    Surface s;
    s.v               = m.v;
    s.n               = m.n;
    s.geometric       = m.geometric;
    float metalness   = m.metalness;
    float3 albedo     = m.albedo;
    s.roughness       = m.roughness;
    // The scene's cover, where it has one and the surface answers it: the
    // branch the same for every invocation of a draw of a material.
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_COVER) != 0u && m.cover_responds > 0.0)
        lit_cover(in_position, s.geometric, m.cover_amount, m.cover_porosity, m.cover_responds, albedo, s.roughness, metalness, s.n);
    s.f0                  = lerp(float3(0.08 * clamp(m.specular, 0.0, 1.0)), albedo, metalness);
    s.diffuse             = albedo * (1.0 - metalness);
    // Transmission takes that share of the diffuse lobe: the light that
    // would scatter back passes through instead (glTF's transmission).
    float transmission = 0.0;
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_SCENE_COLOR) != 0u)
        transmission = clamp(m.transmission, 0.0, 1.0) * (1.0 - metalness);
    s.diffuse *= 1.0 - transmission;
    s.n_dot_v             = max(dot(s.n, s.v), 1e-4);
    s.clearcoat           = clamp(m.clearcoat, 0.0, 1.0);
    s.clearcoat_roughness = clamp(m.clearcoat_roughness, MIN_ROUGHNESS, 1.0);
    s.sheen               = max(m.sheen, float3(0.0));
    s.sheen_roughness     = clamp(m.sheen_roughness, MIN_SHEEN_ROUGHNESS, 1.0);
    float3 color = float3(0.0);
    for (uint d = 0u; d < min(u_lit_scene.directional_count, uint(RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS)); ++d) {
        // Its direction and light at the fragment (a placed light's from
        // its source), through the scene's atmosphere for its suns (1
        // otherwise).
        DirectionalAt at = directional_at(d, in_position);
        float3 radiance  = at.radiance * lit_atmosphere_sun_transmittance_toward(d, in_position, at.direction);
        if (any(radiance > float3(0.0)))
            color += shade(s, at.direction, radiance * directional_visibility(d, in_position, s.geometric, input.frag_coord.xy));
    }

    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_TILES) != 0u) {
        uint2 tile = min(uint2(input.frag_coord.xy) / max(u_lit_scene.tile_size, 1u), uint2(u_lit_scene.tiles_x, u_lit_scene.tiles_y) - 1u);
        uint first = (tile.y * u_lit_scene.tiles_x + tile.x) * uint(RBTREND_LIT_MAX_TILE_LIGHTS + 1);
        uint count = min(tile_lights(first), uint(RBTREND_LIT_MAX_TILE_LIGHTS));
        for (uint i = 0u; i < count; ++i)
            color += point_or_spot(s, u_lit_scene.lights[min(tile_lights(first + 1u + i), uint(RBTREND_LIT_MAX_LIGHTS - 1))], in_position);
    } else {
        for (uint i = 0u; i < min(u_lit_scene.light_count, uint(RBTREND_LIT_MAX_LIGHTS)); ++i)
            color += point_or_spot(s, u_lit_scene.lights[i], in_position);
    }

    float3 r                = reflect(-s.v, s.n);
    // Diffuse ambient light along the bent normal, where the surface is
    // open, if there is one.
    float3 diffuse_normal   = safe_normalize(bent_normal, s.n);
    float3 ambient_diffuse  = hemisphere(diffuse_normal);
    float3 ambient_specular = hemisphere(r);
    float3 r_coat           = reflect(-s.v, s.geometric);
    float3 ambient_coat     = hemisphere(r_coat);
    if ((u_lit_scene.flags & RBTREND_LIT_SCENE_ENVIRONMENT) != 0u) {
        float intensity = u_lit_scene.environment_intensity;
        float top       = max(u_lit_scene.environment_mips - 1.0, 0.0);
        ambient_diffuse += intensity * environment_irradiance.SampleLevel(environment_sampler, to_environment(diffuse_normal), 0.0).rgb;
        ambient_specular += intensity * environment_specular.SampleLevel(environment_sampler, to_environment(r), s.roughness * top).rgb;
        ambient_coat += intensity * environment_specular.SampleLevel(environment_sampler, to_environment(r_coat), s.clearcoat_roughness * top).rgb;
    }
    float2 ab      = split_sum(s.n_dot_v, s.roughness);
    float3 ambient = s.diffuse * ambient_diffuse + (s.f0 * ab.x + ab.y) * ambient_specular;
    // What ambient_specular is multiplied by on its way to the picture, for
    // the reflecting variant's specular target: each factor below that
    // scales it, in the same order.
    float3 specular_weight = s.f0 * ab.x + ab.y;
    float sheen_scale      = 1.0 - max3(s.sheen) * SHEEN_ALBEDO;
    ambient        = ambient * sheen_scale + s.sheen * SHEEN_ALBEDO * ambient_diffuse;
    specular_weight *= sheen_scale;
    if (s.clearcoat > 0.0) {
        float c_dot_v = max(dot(s.geometric, s.v), 1e-4);
        float2 abc    = split_sum(c_dot_v, s.clearcoat_roughness);
        float fc      = fresnel(float3(0.04), c_dot_v).x * s.clearcoat;
        ambient       = ambient * (1.0 - fc) + (0.04 * abc.x + abc.y) * s.clearcoat * ambient_coat;
        specular_weight *= 1.0 - fc;
    }
    color += occlusion * ambient;
    specular_weight *= occlusion;
    if (transmission > 0.0)
        color += transmission * albedo * (1.0 - (s.f0 * ab.x + ab.y)) * transmitted(s, m, in_position, input.frag_coord);
    color += emission;
    // Distant surfaces fade into the air (the scene's aerial volume).
    color = lit_atmosphere_aerial(color, in_position, input.frag_coord.xy);
    // Then the scene's height fog, in front of the air; what the specular
    // target's reflections add later is seen through it too.
    if (lit_fog()) {
        float3 to_surface = in_position - u_lit_scene.eye_position.xyz;
        float distance    = length(to_surface);
        LitFog fog        = lit_fog_along(distance > 0.0 ? to_surface / distance : u_lit_scene.view_direction.xyz, distance);
        color             = lit_fog_apply(color, fog);
        specular_weight *= fog.transmittance;
    }

    color *= u_lit_scene.exposure;
    if (u_lit_scene.output_encoding == 1u) {
        float3 c = clamp(color, 0.0, 1.0);
        color    = select(c <= float3(0.0031308), c * 12.92, 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055);
    }
    LitShaded shaded;
    shaded.color     = float4(color * opacity, opacity);
    shaded.normal    = s.n;
    shaded.roughness = s.roughness;
    shaded.specular  = specular_weight * opacity;
    return shaded;
}
