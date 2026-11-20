module rbtrend_lit_layered;

// Rend's layered lit surface (rbtrendlit.h, "Layered materials";
// RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT and its reflecting twin): up
// to four of eight layers a pixel, blended by the weights the caller's
// vertex shader hands it and by the layers' heights, each read along the
// texture coordinate or on three planes, into the surface the lit
// surface's one lighting takes (rbtrend_lit_lighting.slang's lit_light()).
// The steps are rbtrendlit.h's, numbered as there.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_layered_material;
import rbtrend_lit_lighting;

// The position first, then every location in order, as the caller's vertex
// shader writes them, so D3D12 matches each by register.
public struct LayeredFragmentInput {
    public float4 frag_coord : SV_Position;
    [[vk::location(0)]] public float3 position : TEXCOORD0;
    [[vk::location(1)]] public float3 normal : TEXCOORD1;
    [[vk::location(2)]] public float2 uv : TEXCOORD2;
    [[vk::location(3)]] public float4 tangent : TEXCOORD3;
    [[vk::location(4)]] public float4 color : TEXCOORD4;
    [[vk::location(5)]] public float4 weights_0 : TEXCOORD5;
    [[vk::location(6)]] public float4 weights_1 : TEXCOORD6;
    [[vk::location(7)]] public nointerpolation uint2 layers : TEXCOORD7;
    [[vk::location(8)]] public float3 triplanar : TEXCOORD8;
    [[vk::location(9)]] public nointerpolation float4 turn : TEXCOORD9;
};

// [v] turned by the unit quaternion [q] (x, y, z, w).
float3 turn_by(float4 q, float3 v) {
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// The gradients every read samples with (step 0).
struct Gradients {
    float2 uv_dx;
    float2 uv_dy;
    float3 q_dx;
    float3 q_dy;
};

// Layer [layer] of [map] at the texture coordinate [uv] times [scale].
float4 read_uv(Texture2DArray<float4> map, uint layer, float2 uv, float scale, Gradients g) {
    return map.SampleGrad(layered_sampler, float3(uv * scale, float(layer)), g.uv_dx * scale, g.uv_dy * scale);
}

// Layer [layer] of [map] on the plane [plane] (0: q.zy, 1: q.xz, 2: q.xy)
// of the triplanar place [q] times [scale].
float4 read_plane(Texture2DArray<float4> map, uint layer, float3 q, uint plane, float scale, Gradients g) {
    float2 at, dx, dy;
    if (plane == 0u) {
        at = q.zy;
        dx = g.q_dx.zy;
        dy = g.q_dy.zy;
    } else if (plane == 1u) {
        at = q.xz;
        dx = g.q_dx.xz;
        dy = g.q_dy.xz;
    } else {
        at = q.xy;
        dx = g.q_dx.xy;
        dy = g.q_dy.xy;
    }
    return map.SampleGrad(layered_sampler, float3(at * scale, float(layer)), dx * scale, dy * scale);
}

// Layer [layer] of [map] by its projection: one read along the coordinate,
// or the three planes' blended by [planes].
float4 read_layer(Texture2DArray<float4> map, uint layer, bool triplanar, LayeredFragmentInput input, float3 planes, float scale,
                  Gradients g) {
    if (!triplanar)
        return read_uv(map, layer, input.uv, scale, g);
    float4 sum = float4(0.0);
    for (uint p = 0u; p < 3u; ++p)
        if (planes[p] > 0.0)
            sum += planes[p] * read_plane(map, layer, input.triplanar, p, scale, g);
    return sum;
}

// A tangent-space normal map's texel as -1 to 1, its xy times [scale].
float3 unpack_normal(float4 texel, float scale) {
    float3 tn = texel.xyz * 2.0 - 1.0;
    tn.xy *= scale;
    return tn;
}

public struct LayeredSurface {
    public LitFragmentInput lit;
    public LitSurface surface;
};

// Steps 0 to 4: the blended surface at [input].
public LayeredSurface lit_layered_surface(LayeredFragmentInput input) {
    LayeredSurface out;
    out.lit.frag_coord = input.frag_coord;
    out.lit.position   = input.position;
    out.lit.normal     = input.normal;
    out.lit.uv         = input.uv;
    out.lit.tangent    = input.tangent;
    out.lit.color      = input.color;

    // 0. Gradients, while every pixel of the quad runs the same code.
    Gradients g;
    g.uv_dx = ddx(input.uv);
    g.uv_dy = ddy(input.uv);
    g.q_dx  = ddx(input.triplanar);
    g.q_dy  = ddy(input.triplanar);

    // 1. The cut: the four largest weights above 0 that name a layer, ties
    // to the lower entry; an index at or past the layer count names none.
    uint count = min(u_lit_layered.layer_count, LAYERED_MAX_LAYERS);
    float w[8];
    uint ids[8];
    for (uint e = 0u; e < 8u; ++e) {
        uint id = (input.layers[e / 4u] >> (8u * (e % 4u))) & 0xFFu;
        ids[e]  = id < count ? id : LAYERED_NO_LAYER;
        float x = e < 4u ? input.weights_0[e] : input.weights_1[e - 4u];
        w[e]    = ids[e] != LAYERED_NO_LAYER && x > 0.0 ? x : 0.0;
    }
    uint kept_layer[4];
    float kept_w[4];
    uint kept = 0u;
    uint taken = 0u;
    for (uint k = 0u; k < 4u; ++k) {
        float best_w = 0.0;
        uint best    = 8u;
        for (uint e = 0u; e < 8u; ++e) {
            if ((taken & (1u << e)) == 0u && w[e] > best_w) {
                best_w = w[e];
                best   = e;
            }
        }
        kept_layer[k] = 0u;
        kept_w[k]     = 0.0;
        if (best < 8u) {
            taken |= 1u << best;
            kept_layer[k] = ids[best];
            kept_w[k]     = best_w;
            kept          = k + 1u;
        }
    }
    if (kept == 0u) {
        kept_layer[0] = ids[0] != LAYERED_NO_LAYER ? ids[0] : 0u;
        kept_w[0]     = 1.0;
        kept          = 1u;
    }
    float sum = 0.0;
    for (uint k = 0u; k < 4u; ++k)
        sum += k < kept ? kept_w[k] : 0.0;
    for (uint k = 0u; k < 4u; ++k)
        kept_w[k] = k < kept ? kept_w[k] / sum : 0.0;

    // The normal in the projection's frame, for the triplanar planes:
    // location 1's, turned to the eye, turned back by the frame's turn
    // (rotation commutes with interpolation). Where it is 0, the q.xz plane
    // alone.
    LitFrame frame = lit_frame(out.lit);
    float4 turn    = input.turn;
    float3 m       = float3(0.0, 1.0, 0.0);
    if (dot(input.normal, input.normal) > 0.0)
        m = turn_by(float4(-turn.xyz, turn.w), frame.geometric);
    float3 m4     = m * m * m * m;
    float3 planes = m4 / max(m4.x + m4.y + m4.z, 1e-20);

    // 2. Heights: a = w + c h; w' = w max(a - max(a) + d, 0), renormalised;
    // where nothing is left (d = 0), the largest a whole, ties to the lower.
    float4 ormh[4];
    float a[4];
    float top = -1e30;
    for (uint k = 0u; k < 4u; ++k) {
        ormh[k] = float4(1.0);
        a[k]    = -1e30;
        if (k < kept) {
            RbtrendLitLayer l = u_lit_layered.layers[kept_layer[k]];
            ormh[k]           = read_layer(layered_ormh, kept_layer[k], l.projection == PROJECTION_TRIPLANAR, input, planes, l.uv_scale, g);
            a[k]              = kept_w[k] + l.height_contrast * ormh[k].a;
            top               = max(top, a[k]);
        }
    }
    float d = max(u_lit_layered.blend_depth, 0.0);
    float blended[4];
    float total = 0.0;
    for (uint k = 0u; k < 4u; ++k) {
        blended[k] = k < kept ? kept_w[k] * max(a[k] - top + d, 0.0) : 0.0;
        total += blended[k];
    }
    if (total > 0.0) {
        for (uint k = 0u; k < 4u; ++k)
            blended[k] /= total;
    } else {
        uint winner = 0u;
        for (uint k = 1u; k < 4u; ++k)
            if (k < kept && a[k] > a[winner])
                winner = k;
        for (uint k = 0u; k < 4u; ++k)
            blended[k] = k == winner ? 1.0 : 0.0;
    }

    // 3 and 4. Reads by projection, and the blend: each layer's normal in
    // the positions' space first, then the weighted sum.
    float3 albedo    = float3(0.0);
    float roughness  = 0.0;
    float metalness  = 0.0;
    float specular   = 0.0;
    float occlusion  = 0.0;
    float3 normal    = float3(0.0);
    float cover      = 0.0;
    float porosity   = 0.0;
    float responds   = 0.0;
    for (uint k = 0u; k < 4u; ++k) {
        float wk = blended[k];
        if (wk <= 0.0)
            continue;
        uint id           = kept_layer[k];
        RbtrendLitLayer l = u_lit_layered.layers[id];
        bool triplanar    = l.projection == PROJECTION_TRIPLANAR;
        float3 color      = read_layer(layered_base_color, id, triplanar, input, planes, l.uv_scale, g).rgb;
        float3 n;
        if (!triplanar) {
            float3 tn = unpack_normal(read_uv(layered_normal, id, input.uv, l.uv_scale, g), l.normal_scale);
            n         = lit_tangent_normal(frame.geometric, input.tangent, tn);
        } else {
            // The whiteout blend on each plane (Golus, 2017), swizzled into
            // the projection's frame, then turned into the positions' space.
            float3 sum3 = float3(0.0);
            if (planes.x > 0.0) {
                float3 t = unpack_normal(read_plane(layered_normal, id, input.triplanar, 0u, l.uv_scale, g), l.normal_scale);
                t        = float3(t.xy + m.zy, abs(t.z) * m.x);
                sum3 += t.zyx * planes.x;
            }
            if (planes.y > 0.0) {
                float3 t = unpack_normal(read_plane(layered_normal, id, input.triplanar, 1u, l.uv_scale, g), l.normal_scale);
                t        = float3(t.xy + m.xz, abs(t.z) * m.y);
                sum3 += t.xzy * planes.y;
            }
            if (planes.z > 0.0) {
                float3 t = unpack_normal(read_plane(layered_normal, id, input.triplanar, 2u, l.uv_scale, g), l.normal_scale);
                t        = float3(t.xy + m.xy, abs(t.z) * m.z);
                sum3 += t * planes.z;
            }
            float len = length(sum3);
            n         = len > 0.0 ? turn_by(turn, sum3 / len) : frame.geometric;
        }
        albedo += wk * max(l.base_color * color, float3(0.0));
        roughness += wk * l.roughness * ormh[k].g;
        metalness += wk * l.metalness * ormh[k].b;
        specular += wk * l.specular;
        occlusion += wk * lerp(1.0, ormh[k].r, clamp(l.occlusion_strength, 0.0, 1.0));
        normal += wk * n;
        if ((l.cover & 0x80000000u) != 0u) {
            cover += wk * float(l.cover & 0x7FFFu) / 32767.0;
            porosity += wk * float((l.cover >> 15) & 0x7FFFu) / 32767.0;
            responds += wk;
        }
    }

    float4 base = float4(albedo, 1.0) * input.color;
    LitSurface s;
    s.v                    = frame.v;
    s.geometric            = frame.geometric;
    float len              = length(normal);
    s.n                    = len > 0.0 ? normal / len : frame.geometric;
    s.albedo               = max(base.rgb, float3(0.0));
    s.opacity              = clamp(base.a, 0.0, 1.0);
    s.roughness            = clamp(roughness, MIN_ROUGHNESS, 1.0);
    s.metalness            = clamp(metalness, 0.0, 1.0);
    s.specular             = specular;
    s.occlusion            = occlusion;
    s.emission             = float3(0.0);
    s.clearcoat            = 0.0;
    s.clearcoat_roughness  = 0.0;
    s.sheen                = float3(0.0);
    s.sheen_roughness      = 0.0;
    s.transmission         = 0.0;
    s.ior                  = 1.0;
    s.thickness            = 0.0;
    s.attenuation_distance = 0.0;
    s.attenuation          = float3(1.0);
    s.cover_amount         = cover;
    s.cover_porosity       = porosity;
    s.cover_responds       = responds;
    s.screen_space         = 1u; // no alpha mode: opaque
    out.surface            = s;
    return out;
}

// Step 5: the blend lit.
public LitShaded lit_layered_shade(LayeredFragmentInput input) {
    LayeredSurface blended = lit_layered_surface(input);
    return lit_light(blended.lit, blended.surface);
}
