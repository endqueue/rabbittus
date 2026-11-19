// Rend's unlit particle (rbtrendparticles.h, RBTREND_PARTICLES_SHADER_UNLIT_FRAGMENT):
// the material's base color times its map at the flipbook's frame times the
// color over life, plus its emission (times its map), exposed and encoded
// as the scene's surfaces are, premultiplied by its opacity, faded by the
// SOFT module. No light, no air, no fog: sparks, rain, glows.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_material;
import rbtrend_particles_common;
import rbtrend_particles_draw_bindings;
import rbtrend_particles_draw;

[shader("fragment")]
float4 rbtrend_particles_unlit_frag(ParticleFragmentInput input) : SV_Target0 {
    float4 base     = u_lit_material.base_color * input.color * particles_base_map(input);
    float3 emission = max(u_lit_material.emission.rgb, float3(0.0));
    if ((u_lit_material.maps & MAP_EMISSION) != 0u)
        emission *= emission_map.Sample(material_sampler, map_uv(material_uv(input.uv), MAP_INDEX_EMISSION)).rgb;
    float3 color = (max(base.rgb, float3(0.0)) + emission) * u_lit_scene.exposure;
    if (u_lit_scene.output_encoding == 1u) {
        float3 c = clamp(color, 0.0, 1.0);
        color    = select(c <= float3(0.0031308), c * 12.92, 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055);
    }
    float opacity = clamp(base.a, 0.0, 1.0) * particles_soft_fade(input);
    return float4(color * opacity, opacity);
}
