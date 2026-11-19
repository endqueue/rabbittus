module rbtrend_clouds_input;

// Set 1 of the clouds' march and composite (RBTREND_COMMAND_LIT_CLOUDS,
// rbtrendclouds.h): a set from the scene's view-set records over the depth
// the surfaces were drawn with and each layer's atmosphere's transmittance
// table (a layer with none bound to another's, unread), and a clamping
// linear sampler.

[[vk::binding(0, 1)]] public Texture2D<float4> clouds_depth : register(t0, space1);
[[vk::binding(1, 1)]] public Texture2D<float4> clouds_table0 : register(t1, space1);
[[vk::binding(2, 1)]] public Texture2D<float4> clouds_table1 : register(t2, space1);
[[vk::binding(3, 1)]] public Texture2D<float4> clouds_table2 : register(t3, space1);
[[vk::binding(4, 1)]] public Texture2D<float4> clouds_table3 : register(t4, space1);
[[vk::binding(5, 1)]] public SamplerState clouds_clamp : register(s5, space1);
