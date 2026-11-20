module rbtrend_lit_surface;

// Rend's lit surface (rbtrendlit.h, RBTREND_LIT_SHADER_SURFACE_FRAGMENT and
// RBTREND_LIT_SHADER_SURFACE_REFLECT_FRAGMENT): one lit material's reads
// (set 1, rbtrend_lit_material.slang) - its values and its base color,
// normal, ORM and emission maps - into the surface the one lighting takes
// (rbtrend_lit_lighting.slang's lit_light()). The vertex shader is the
// caller's or Rend's; it writes the five inputs of LitFragmentInput in any
// one space, which the scene's positions and directions are given in; the
// fifth, a color, multiplies the base color and the opacity (glTF's
// COLOR_0). The two fragment shaders (rbtrend_lit_frag.slang,
// rbtrend_lit_surface_reflect_frag.slang) are this module's lit_shade() and
// what each writes of it. The material's alpha mode: OPAQUE takes the
// opacity as 1; MASK discards below its cutoff, after every map's read (a
// discard that ends an invocation leaves its quad's later derivatives
// undefined), the rest opaque; BLEND keeps the opacity and reads none of the
// view's screen-space inputs (LitSurface.screen_space 0).

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_lit_material;
import rbtrend_lit_lighting;

// The surface at [input], as its material's alpha mode says; discards a
// cut-out's hole.
public LitShaded lit_shade(LitFragmentInput input) {
    float4 base_map = float4(1.0);
    if ((u_lit_material.maps & MAP_BASE_COLOR) != 0u)
        base_map = base_color_map.Sample(material_sampler, map_uv(material_uv(input.uv), MAP_INDEX_BASE_COLOR));
    return lit_shade_with_base_map(input, base_map);
}

// The same, with what the base color map gives at [input] read by the
// caller ([base_map]; 1 where the material has none): a particle's
// flipbook blends two frames of it (rbtrend_particles_lit_frag.slang).
public LitShaded lit_shade_with_base_map(LitFragmentInput input, float4 base_map) {
    return lit_shade_as(input, base_map, u_lit_material.alpha_mode);
}

// The same, as alpha mode [mode] (an ALPHA_MODE_; any other OPAQUE) in
// place of the material's: a particle shades as BLEND whatever its
// material's (rbtrend_particles_lit_frag.slang).
public LitShaded lit_shade_as(LitFragmentInput input, float4 base_map, uint mode) {
    float2 uv = material_uv(input.uv);
    uint maps = u_lit_material.maps;

    float4 base = u_lit_material.base_color * input.color;
    if ((maps & MAP_BASE_COLOR) != 0u)
        base *= base_map;
    float opacity = clamp(base.a, 0.0, 1.0);

    float3 orm = float3(1.0);
    if ((maps & MAP_ORM) != 0u)
        orm = orm_map.Sample(material_sampler, map_uv(uv, MAP_INDEX_ORM)).rgb;
    float3 emission = max(u_lit_material.emission.rgb, float3(0.0));
    if ((maps & MAP_EMISSION) != 0u)
        emission *= emission_map.Sample(material_sampler, map_uv(uv, MAP_INDEX_EMISSION)).rgb;

    LitFrame frame = lit_frame(input);
    LitSurface s;
    s.v         = frame.v;
    s.geometric = frame.geometric;
    s.n         = frame.geometric;
    // The normal map, in the frame of the tangent, the bitangent its sign
    // makes (MikkTSpace's), and the normal.
    if ((maps & MAP_NORMAL) != 0u && dot(input.tangent.xyz, input.tangent.xyz) > 0.0) {
        float3 t = input.tangent.xyz - s.n * dot(s.n, input.tangent.xyz);
        if (dot(t, t) > 0.0) {
            float3 tn = normal_map.Sample(material_sampler, map_uv(uv, MAP_INDEX_NORMAL)).xyz * 2.0 - 1.0;
            tn.xy *= u_lit_material.normal_scale;
            s.n = lit_tangent_normal(s.n, input.tangent, tn);
        }
    }
    // Every map is read: a cut-out's hole is discarded now, and only a
    // blended surface keeps its opacity. The branches are on a value
    // uniform over a draw.
    if (mode == ALPHA_MODE_MASK && opacity < u_lit_material.alpha_cutoff)
        discard;
    if (mode != ALPHA_MODE_BLEND)
        opacity = 1.0;
    s.screen_space        = mode == ALPHA_MODE_BLEND ? 0u : 1u;
    s.albedo              = max(base.rgb, float3(0.0));
    s.opacity             = opacity;
    s.roughness           = clamp(u_lit_material.roughness * orm.g, MIN_ROUGHNESS, 1.0);
    s.metalness           = clamp(u_lit_material.metalness * orm.b, 0.0, 1.0);
    s.specular            = u_lit_material.specular;
    s.occlusion           = lerp(1.0, orm.r, clamp(u_lit_material.occlusion_strength, 0.0, 1.0));
    s.emission            = emission;
    s.clearcoat           = u_lit_material.clearcoat;
    s.clearcoat_roughness = u_lit_material.clearcoat_roughness;
    s.sheen               = u_lit_material.sheen.rgb;
    s.sheen_roughness     = u_lit_material.sheen.a;
    s.transmission        = u_lit_material.transmission.x;
    s.ior                 = u_lit_material.transmission.y;
    s.thickness           = u_lit_material.transmission.z;
    s.attenuation_distance = u_lit_material.transmission.w;
    s.attenuation         = u_lit_material.attenuation;
    s.cover_amount        = u_lit_material.cover_amount;
    s.cover_porosity      = u_lit_material.cover_porosity;
    s.cover_responds      = u_lit_material.cover_responds;
    return lit_light(input, s);
}
