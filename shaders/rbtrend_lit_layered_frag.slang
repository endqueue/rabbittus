// Rend's layered lit surface (rbtrendlit.h,
// RBTREND_LIT_SHADER_LAYERED_SURFACE_FRAGMENT): the picture's color alone.
// rbtrend_lit_layered.slang has the blend, rbtrend_lit_lighting.slang the
// lighting.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_layered_material;
import rbtrend_lit_lighting;
import rbtrend_lit_layered;

[shader("fragment")]
float4 rbtrend_lit_layered_frag(LayeredFragmentInput input) : SV_Target0 {
    return lit_layered_shade(input).color;
}
