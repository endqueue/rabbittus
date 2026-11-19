module rbtrend_clouds_bindings;

// Sets 2 and 3 of the clouds' march (RBTREND_COMMAND_LIT_CLOUDS,
// rbtrendclouds.h). Set 2 is the clouds' own, made with them: their looks'
// block, the two noise volumes, the coverage maps' array (six layers a map)
// and a repeating linear sampler. Set 3 is a view record's: the frame's
// block (the march's sizes, steps and jitter, the layers resolved) and the
// march's two outputs. The blocks mirror rbtrend_clouds_looks_block and
// rbtrend_clouds_march_block (src/render/rbtrendclouds_internal.hpp),
// checked member by member against this reflection when the layouts are
// made.

import rbtrend_clouds_model;

public struct RbtrendCloudsLooks {
    public CloudLook looks[CLOUDS_MAX_LOOKS];
};

public struct RbtrendCloudsMarch {
    public uint4 sizes;   // x, y the march's texels; z, w the history's
    public float4 target; // xy the target's texels; z the max distance; w 1 where depth is reversed (near at 1)
    public uint4 counts;  // x layers, y max steps, z light steps, w this frame's texel of each 2 x 2 block (0 to 3)
    public float4 jitter; // xy within the history texel, z along the march, 0 to 1
    public CloudLayer layers[CLOUDS_MAX_LAYERS];
};

[[vk::binding(0, 2)]] public ConstantBuffer<RbtrendCloudsLooks> u_clouds_looks : register(b0, space2);
[[vk::binding(1, 2)]] public Texture3D<float4> clouds_base : register(t1, space2);
[[vk::binding(2, 2)]] public Texture3D<float4> clouds_detail : register(t2, space2);
[[vk::binding(3, 2)]] public Texture2DArray<float4> clouds_maps : register(t3, space2);
[[vk::binding(4, 2)]] public SamplerState clouds_repeat : register(s4, space2);

[[vk::binding(0, 3)]] public ConstantBuffer<RbtrendCloudsMarch> u_clouds_march : register(b0, space3);
[[vk::binding(1, 3)]] [[vk::image_format("rgba16f")]] public RWTexture2D<float4> clouds_march_out : register(u1, space3);
[[vk::binding(2, 3)]] [[vk::image_format("r32f")]] public RWTexture2D<float> clouds_march_depth_out : register(u2, space3);
