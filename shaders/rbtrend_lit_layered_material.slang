module rbtrend_lit_layered_material;

// The material set of Rend's layered lit surface (rbtrendlit.h, "Layered
// materials"), set 1, for its two fragment shaders: a block of the layers'
// values and the blend depth, three 2D texture arrays (layer i of a map at
// array layer i) and a sampler. rbtrendlit.cpp's rbtrend_lit_layered_block
// mirrors the block; its layout is the same in std140, std430 and HLSL's
// constant buffer packing (no member crosses a 16-byte boundary): the blend
// depth, the layer count and two of 0 at offset 0, then a layer at
// 16 + 48 i, three float4 each.

public static const uint LAYERED_MAX_LAYERS = 16u;
public static const uint LAYERED_NO_LAYER   = 0xFFu;
public static const uint PROJECTION_UV        = 1u;
public static const uint PROJECTION_TRIPLANAR = 2u;

public struct RbtrendLitLayer {
    public float3 base_color;      // linear, times the base color map
    public float roughness;        // times the ORMH map's green
    public float metalness;        // times its blue
    public float specular;         // a dielectric's reflectance: 0.5 is F0 0.04
    public float normal_scale;
    public float occlusion_strength;
    public float height_contrast;  // how much its height (alpha) shifts its share
    public float uv_scale;         // repeats a unit of the coordinate or place
    public uint projection;        // PROJECTION_
    // Its RBTREND_LIT_COVER_RESPONSE, packed (the row's spare lane): the
    // cover in bits 0 to 14 and the porosity in 15 to 29, each over 32767,
    // bit 31 set where it answers the cover at all.
    public uint cover;
};

public struct RbtrendLitLayered {
    public float blend_depth;
    public uint layer_count;
    public uint reserved0;
    public uint reserved1;
    public RbtrendLitLayer layers[LAYERED_MAX_LAYERS];
};

[[vk::binding(0, 1)]] public ConstantBuffer<RbtrendLitLayered> u_lit_layered : register(b0, space1);
[[vk::binding(1, 1)]] public Texture2DArray<float4> layered_base_color : register(t1, space1);
[[vk::binding(2, 1)]] public Texture2DArray<float4> layered_normal : register(t2, space1);
[[vk::binding(3, 1)]] public Texture2DArray<float4> layered_ormh : register(t3, space1);
[[vk::binding(4, 1)]] public SamplerState layered_sampler : register(s4, space1);
