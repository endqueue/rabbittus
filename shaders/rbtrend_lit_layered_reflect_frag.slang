// Rend's reflecting layered lit surface (rbtrendlit.h,
// RBTREND_LIT_SHADER_LAYERED_SURFACE_REFLECT_FRAGMENT): the three targets
// rbtrend_lit_surface_reflect_frag.slang writes, from the blended surface.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_layered_material;
import rbtrend_lit_lighting;
import rbtrend_lit_layered;

struct ReflectingOutput {
    float4 color : SV_Target0;
    float4 normal_roughness : SV_Target1;
    float4 specular : SV_Target2;
};

[shader("fragment")]
ReflectingOutput rbtrend_lit_layered_reflect_frag(LayeredFragmentInput input) {
    LitShaded shaded = lit_layered_shade(input);
    ReflectingOutput output;
    output.color            = shaded.color;
    output.normal_roughness = float4(shaded.normal, shaded.roughness);
    output.specular         = float4(shaded.specular, 0.0);
    return output;
}
