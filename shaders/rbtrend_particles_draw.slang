module rbtrend_particles_draw;

// What Rend's particle vertex shader hands its fragment shaders
// (rbtrendparticles.h, "Drawing"): locations 0 to 4 as a lit vertex
// shader writes them (rbtrend_lit_vertex.slang's LitVertexOutput), then
// the flipbook's next frame and blend at 5; and what the two fragment
// shaders share: the flipbook's sample of the base color map, and the
// SOFT module's fade.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_material;
import rbtrend_particles_common;
import rbtrend_particles_draw_bindings;

// The position first, then every location in order, so D3D12 matches each
// by register.
public struct ParticleVertexOutput {
    public float4 position : SV_Position;
    [[vk::location(0)]] public float3 world : TEXCOORD0;
    [[vk::location(1)]] public float3 normal : TEXCOORD1;
    [[vk::location(2)]] public float2 uv : TEXCOORD2;
    [[vk::location(3)]] public float4 tangent : TEXCOORD3;
    [[vk::location(4)]] public float4 color : TEXCOORD4;
    // xy the next frame's texture coordinate, z how far into it (0 for no
    // blend).
    [[vk::location(5)]] public float4 flipbook : TEXCOORD5;
};

public struct ParticleFragmentInput {
    public float4 frag_coord : SV_Position;
    [[vk::location(0)]] public float3 position : TEXCOORD0;
    [[vk::location(1)]] public float3 normal : TEXCOORD1;
    [[vk::location(2)]] public float2 uv : TEXCOORD2;
    [[vk::location(3)]] public float4 tangent : TEXCOORD3;
    [[vk::location(4)]] public float4 color : TEXCOORD4;
    [[vk::location(5)]] public float4 flipbook : TEXCOORD5;
};

// The base color map at [input]'s frame, blended into its next where the
// flipbook blends; 1 where the material has no map.
public float4 particles_base_map(ParticleFragmentInput input) {
    if ((u_lit_material.maps & MAP_BASE_COLOR) == 0u)
        return float4(1.0);
    float4 a = base_color_map.Sample(material_sampler, map_uv(material_uv(input.uv), MAP_INDEX_BASE_COLOR));
    if (input.flipbook.z <= 0.0)
        return a;
    float4 b = base_color_map.Sample(material_sampler, map_uv(material_uv(input.flipbook.xy), MAP_INDEX_BASE_COLOR));
    return lerp(a, b, input.flipbook.z);
}

// The SOFT module's fade at [input]: how far the depth's surface is behind
// the fragment along its view ray, over the module's distance, 0 to 1; 1
// with no SOFT or where the depth holds nothing.
public float particles_soft_fade(ParticleFragmentInput input) {
    if ((u_particles.kind.x & MODULE_SOFT) == 0u)
        return 1.0;
    uint width = 0u, height = 0u;
    particles_draw_depth.GetDimensions(width, height);
    int2 texel = clamp(int2(input.frag_coord.xy), int2(0), int2(width, height) - 1);
    float d    = particles_draw_depth.Load(int3(texel, 0)).r;
    if (u_particles.options.w != 0u ? d <= 0.0 : d >= 1.0)
        return 1.0;
    float2 uv  = input.frag_coord.xy / float2(width, height);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 q   = mul(u_lit_scene.inverse_view_projection, float4(ndc, d, 1.0));
    float4 a   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 1.0, 1.0));
    float4 b   = mul(u_lit_scene.inverse_view_projection, float4(ndc, 0.0, 1.0));
    float3 ray = normalize(b.xyz * a.w - a.xyz * b.w);
    if (dot(ray, u_lit_scene.view_direction.xyz) < 0.0)
        ray = -ray;
    float behind = dot(q.xyz / q.w - input.position, ray);
    return saturate(behind / u_particles.draw.y);
}
