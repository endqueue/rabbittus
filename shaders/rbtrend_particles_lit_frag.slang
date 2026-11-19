// Rend's lit particle (rbtrendparticles.h, RBTREND_PARTICLES_SHADER_LIT_FRAGMENT):
// the lit surface's shading (rbtrend_lit_surface.slang) of the particle's
// material, its base color map read at the flipbook's frame, the result
// (premultiplied by its opacity) faded by the SOFT module.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_material;
import rbtrend_lit_surface;
import rbtrend_particles_common;
import rbtrend_particles_draw_bindings;
import rbtrend_particles_draw;

[shader("fragment")]
float4 rbtrend_particles_lit_frag(ParticleFragmentInput input) : SV_Target0 {
    LitFragmentInput lit;
    lit.frag_coord = input.frag_coord;
    lit.position   = input.position;
    lit.normal     = input.normal;
    lit.uv         = input.uv;
    lit.tangent    = input.tangent;
    lit.color      = input.color;
    return lit_shade_with_base_map(lit, particles_base_map(input)).color * particles_soft_fade(input);
}
