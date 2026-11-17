// A table copied texel for texel into the top left of a layer of a scene's
// atmosphere array (RBTREND_COMMAND_LIT_ATMOSPHERE: the atmosphere's
// transmittance table into layer 1), so the lit shaders read every table
// through one binding. Its set 0 is a view's (a texture and a sampler, as
// the lit passes' layout has it).

[[vk::binding(0, 0)]] Texture2D<float4> source_table : register(t0, space0);
[[vk::binding(1, 0)]] SamplerState source_sampler : register(s1, space0);

[shader("fragment")]
float4 rbtrend_atmosphere_copy_frag(float4 frag_coord : SV_Position) : SV_Target0 {
    return source_table.Load(int3(int2(frag_coord.xy), 0));
}
