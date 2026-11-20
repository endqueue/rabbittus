module rbtrend_fog_bindings;

// Set 1 of the froxel grid's cull and inject passes (rbtrendfog.h): a view
// record's set, one for each way its history runs, made with the fog. Its
// block mirrors rbtrend_fog_block (src/render/rbtrendfog_internal.hpp),
// checked member by member against this reflection when the layout is made.

import rbtrend_lit_scene;

// The most volumes a command takes, and a tile's list: a count, then up to
// RBTREND_FOG_MAX_TILE_VOLUMES indices into the block's volumes.
public static const uint FOG_MAX_VOLUMES = 64u;
public static const uint FOG_MAX_TILE_VOLUMES = 15u;
public static const uint FOG_TILE_WORDS = 16u;

// A volume of the command, resolved at the compile.
public struct RbtrendFogVolume {
    public float4 to_volume[3];   // rows: the scene's space into the volume's frame
    public float4 half_shape;     // xyz its half sizes (a sphere's radius in x); w 1 box, 2 sphere
    public float4 params;         // x edge, y density, z noise strength, w noise tile
    public float4 noise_offset;   // xyz in tiles
    public float4 albedo;         // rgb
    public float4 sphere;         // xyz its bounds' centre in the scene's space; w their radius
};

public struct RbtrendFog {
    public float4x4 last_view_projection; // last frame's, column-major as the CPU writes it
    public float4 last_eye;              // xyz last frame's eye in its space; w the history's weight, 0 for none
    public float4 shift;                 // xyz this frame's origin in last frame's space; w this frame's jitter, 0 to 1
    public float4 range;                 // x near, y far, z ln(far / near), w slices
    public float4 last_range;            // the same, last frame's
    public uint4 grid;                   // x W, y H, z S, w the volumes
    public float4 history;               // x the clip's gamma (RBTREND_HISTORY_CLIP_GAMMA, rbtrendhistory.hpp)
    public RbtrendFogVolume volumes[FOG_MAX_VOLUMES];
};

[[vk::binding(0, 1)]] public ConstantBuffer<RbtrendFog> u_fog : register(b0, space1);
// The fog's WORLEY volume, read repeating (rbtmathnoise.h, READING A VOLUME).
[[vk::binding(1, 1)]] public Texture3D<float4> fog_noise : register(t1, space1);
[[vk::binding(2, 1)]] public SamplerState fog_noise_sampler : register(s2, space1);
// Last frame's grid of media and light, read clamped and linear.
[[vk::binding(3, 1)]] public Texture3D<float4> fog_history : register(t3, space1);
[[vk::binding(4, 1)]] public SamplerState fog_history_sampler : register(s4, space1);
// This frame's, written.
[[vk::binding(5, 1)]] [[vk::image_format("rgba16f")]] public RWTexture3D<float4> fog_written : register(u5, space1);
// The tiles' lists, FOG_TILE_WORDS words a tile, row after row; raw, as
// Carrottus GPU's D3D12 backend views every storage buffer.
[[vk::binding(6, 1)]] public RWByteAddressBuffer FogTiles : register(u6, space1);

// The view ray through [uv] (0 to 1 across the view, the first row at the
// top), unit, away from the eye: between the texel's points at depths 1 and
// 0 taken homogeneously, as the sky shader takes it, so a far plane at
// infinity gives a direction.
public float3 fog_view_ray(float2 uv) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 a   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 b   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    float3 d   = normalize(b.xyz * a.w - a.xyz * b.w);
    if (dot(d, u_lit_scene.view_direction.xyz) < 0.0)
        d = -d;
    return d;
}
