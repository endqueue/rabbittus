// The clouds' history rebuilt (RBTREND_COMMAND_LIT_CLOUDS, rbtrendclouds.h;
// rbtrend_clouds_draft.h section 4, card A70): a thread a texel of the
// history, half the target each way. Each texel takes last frame's history
// where it lands: the cloud at the march's depth, moved by the space's
// shift into last frame's space and through last frame's view-projection,
// read bilinearly where it falls inside last frame's history, accepted
// where its depth there agrees with the march's within a fifth (or both
// are clear), and clipped towards the mean of the march's 3 x 3 texels
// round it to the box gamma of their standard deviations wide each way.
// The texel the march went through this frame blends its march into that
// by the frame's weight (its depth by each side's weight and opacity); the
// others keep it. Where the history is refused, or at a reset, the march's
// value.
//
// Set 0 is the lit scene's own set, declared whole; this shader is among
// the shaders the scene's layout is made from. Set 1 the view record's, one
// for each way its history runs.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;

// rbtrend_clouds_history_block in src/render/rbtrendclouds_internal.hpp.
struct RbtrendCloudsHistory {
    float4x4 last_view_projection; // last frame's, column-major as the CPU writes it
    float4 shift;                  // xyz this frame's origin in last frame's space; w 1 to start again
    uint4 sizes;                   // x, y the march's texels; z, w the history's
    uint4 frame;                   // x this frame's texel of each 2 x 2 block (0 to 3); y the marched texel's weight, z the clip's gamma (a float's bits)
};

[[vk::binding(0, 1)]] ConstantBuffer<RbtrendCloudsHistory> u_clouds_history : register(b0, space1);
[[vk::binding(1, 1)]] Texture2D<float4> clouds_march : register(t1, space1);
[[vk::binding(2, 1)]] Texture2D<float4> clouds_march_depth : register(t2, space1);
[[vk::binding(3, 1)]] Texture2D<float4> clouds_last : register(t3, space1);
[[vk::binding(4, 1)]] Texture2D<float4> clouds_last_depth : register(t4, space1);
[[vk::binding(5, 1)]] SamplerState clouds_history_sampler : register(s5, space1);
[[vk::binding(6, 1)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> clouds_written : register(u6, space1);
[[vk::binding(7, 1)]] [[vk::image_format("r32f")]] RWTexture2D<float> clouds_written_depth : register(u7, space1);

[shader("compute")]
[numthreads(8, 8, 1)]
void rbtrend_clouds_reconstruct_comp(uint3 id : SV_DispatchThreadID) {
    uint4 sizes = u_clouds_history.sizes;
    if (id.x >= sizes.z || id.y >= sizes.w)
        return;
    uint2 block  = min(id.xy / 2u, sizes.xy - 1u);
    uint here    = (id.x & 1u) + (id.y & 1u) * 2u;
    float4 fresh = clouds_march.Load(int3(int2(block), 0));
    float fresh_depth = clouds_march_depth.Load(int3(int2(block), 0)).r;
    float4 value = fresh;
    float depth  = fresh_depth;
    if (u_clouds_history.shift.w < 0.5) {
        float2 size  = u_lit_scene.target_size.xy;
        float2 pixel = min((float2(id.xy) + 0.5) * 2.0, size - 0.5);
        float2 uv    = pixel / size;
        float2 ndc   = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float4 a     = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
        float4 b     = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
        float3 dir   = normalize(b.xyz * a.w - a.xyz * b.w);
        if (dot(dir, u_lit_scene.view_direction.xyz) < 0.0)
            dir = -dir;
        float3 place = u_lit_scene.eye_position.xyz + dir * fresh_depth + u_clouds_history.shift.xyz;
        float4 clip  = mul(u_clouds_history.last_view_projection, float4(place, 1.0));
        if (clip.w > 0.0) {
            float2 at = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
            if (all(at >= float2(0.0)) && all(at <= float2(1.0))) {
                uint w, h;
                clouds_last.GetDimensions(w, h);
                float2 coordinate = at * float2(sizes.zw) / float2(w, h);
                float4 last       = clouds_last.SampleLevel(clouds_history_sampler, coordinate, 0.0);
                float last_depth  = clouds_last_depth.SampleLevel(clouds_history_sampler, coordinate, 0.0).r;
                bool clear        = fresh.a > 0.999 && last.a > 0.999;
                bool agrees       = abs(last_depth - fresh_depth) <= 0.2 * max(last_depth, fresh_depth);
                if (clear || agrees) {
                    // The march's 3 x 3 neighbourhood's mean and standard
                    // deviation, each channel as stored.
                    float4 sum = float4(0.0), squares = float4(0.0);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            int2 n   = clamp(int2(block) + int2(dx, dy), int2(0), int2(sizes.xy) - 1);
                            float4 v = clouds_march.Load(int3(n, 0));
                            sum += v;
                            squares += v * v;
                        }
                    float4 mean  = sum / 9.0;
                    float4 sigma = sqrt(max(squares / 9.0 - mean * mean, float4(0.0)));
                    // Clipped towards the mean to the box (Karis's clip,
                    // Salvi's box).
                    float4 extent = asfloat(u_clouds_history.frame.z) * sigma + 1e-5;
                    float4 away   = last - mean;
                    float4 units  = abs(away) / extent;
                    float most    = max(max(units.x, units.y), max(units.z, units.w));
                    float4 kept   = most > 1.0 ? mean + away / most : last;
                    value         = kept;
                    depth         = last_depth;
                    if (here == u_clouds_history.frame.x) {
                        // Blended: the march by the weight, its depth by
                        // each side's weight and opacity.
                        float alpha = asfloat(u_clouds_history.frame.y);
                        value       = alpha >= 1.0 ? fresh : kept + alpha * (fresh - kept);
                        float wf    = alpha * (1.0 - fresh.a);
                        float wh    = (1.0 - alpha) * (1.0 - kept.a);
                        bool seen   = (1.0 - fresh.a) >= 0.0009765625 || (1.0 - kept.a) >= 0.0009765625;
                        depth       = seen && wf + wh > 0.0 ? (wf * fresh_depth + wh * last_depth) / (wf + wh) : fresh_depth;
                    }
                }
            }
        }
    }
    clouds_written[id.xy]       = value;
    clouds_written_depth[id.xy] = depth;
}
