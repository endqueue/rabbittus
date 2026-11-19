module rbtrend_lit_scene;

// The scene set of Rend's lit shaders (rbtrendlit.h), set 0. Imported by
// every shader that reads it, so they declare it alike; rbtrendlit.cpp's
// rbtrend_lit_scene_block mirrors the block and checks every member's offset
// against the shader's reflection when the layout is made.

public static const int RBTREND_LIT_MAX_LIGHTS = 128;
public static const int RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS = 4;
public static const int RBTREND_LIT_MAX_CASCADES = 4;
public static const int RBTREND_LIT_MAX_SHADOW_VIEWS = 64;
// Lights a tile of the view lists at most; one more is its count.
public static const int RBTREND_LIT_MAX_TILE_LIGHTS = 63;

// A point or spot light, culled per tile.
public struct RbtrendLitLight {
    public float4 position_range;  // xyz: where it is. w: its range
    public float4 radiance_type;   // rgb: color times strength. w: 0 point, 1 spot
    public float4 direction_inner; // xyz: a spot's axis, unit. w: cos of its inner angle
    public float4 outer_shadow;    // x: cos of a spot's outer angle. y: its shadow in shadows[], or -1
};

// Kinds of a directional light's shadow (RbtrendLitDirectional.shadow_views.x).
public static const uint RBTREND_LIT_DIRECTIONAL_NO_SHADOW = 0u;
public static const uint RBTREND_LIT_DIRECTIONAL_MASK      = 1u; // its slot's mask texture
public static const uint RBTREND_LIT_DIRECTIONAL_CASCADES  = 2u; // views in the atlas

// A directional light, in the order the scene's list has them. For a placed
// one (RBTREND_LIT_LIGHT_TYPE_DIRECTIONAL_PLACED) direction_versine and
// radiance_disc's rgb hold its values at the scene's eye; directional_at()
// gives them at any point.
public struct RbtrendLitDirectional {
    public float4 direction_versine;      // xyz unit, towards the light. w: 1 - cos of its radius, 0 for none
    public float4 radiance_disc;          // rgb: color times strength. w: 1 if the sky draws its disc
    public float4 splits;                 // how far along view_direction each cascade reaches
    public float4 shadow_params;          // x depth bias, y normal offset, z softness, w strength
    public uint4 shadow_views;            // x kind, y first view, z view count
    public float4 place_reference;        // a placed light's: xyz its source's place, w its reference distance; w 0 for one from one direction
    public float4 placed_radiance_radius; // a placed light's: rgb its radiance at the reference, w its source's radius
};

// A point or spot light's map shadow.
public struct RbtrendLitShadow {
    public float4 params;            // x depth bias, y normal offset, z softness, w strength
    public uint4 views;              // x first view, y view count
};

// One view of a shadow: a cascade, a spot's map or a point light's face.
public struct RbtrendLitShadowView {
    public float4x4 view_projection; // the scene's space to its clip space
    public float4 rect;              // its rectangle of the atlas, in texels: x, y, width, height
};

// Bits of RbtrendLitScene.flags.
public static const uint RBTREND_LIT_SCENE_SHADOW_ATLAS     = 1u;  // shadow_atlas holds the scene's map shadows
public static const uint RBTREND_LIT_SCENE_SHADOW_REVERSED  = 2u;  // the atlas was rendered near at 1
public static const uint RBTREND_LIT_SCENE_ENVIRONMENT      = 4u;  // the environment cubes hold light
public static const uint RBTREND_LIT_SCENE_TILES            = 8u;  // tile_lights holds this view's lists
public static const uint RBTREND_LIT_SCENE_OCCLUSION        = 16u; // ambient_occlusion is this view's
public static const uint RBTREND_LIT_SCENE_BENT_NORMALS     = 64u; // ambient_occlusion's gba is a bent normal, read for diffuse ambient light
public static const uint RBTREND_LIT_SCENE_SCENE_COLOR      = 256u; // scene_color holds the opaque picture behind
public static const uint RBTREND_LIT_SCENE_ATMOSPHERE       = 512u; // the atmosphere_* members and textures hold the scene's atmosphere (rbtrendatmosphere.h)
public static const uint RBTREND_LIT_SCENE_AERIAL           = 1024u; // atmosphere_aerial holds its aerial volume
public static const uint RBTREND_LIT_SCENE_SKY              = 2048u; // the atmosphere's sky-view table, layer 0, holds its sky (none with no sky)
public static const uint RBTREND_LIT_SCENE_FOG              = 4096u; // the fog_* members hold the scene's height fog (rbtrend_lit_scene_set_fog())
public static const uint RBTREND_LIT_SCENE_COVER            = 8192u; // the cover_* members hold the scene's cover (rbtrend_lit_scene_set_cover())
public static const uint RBTREND_LIT_SCENE_COVER_ROUND      = 16384u; // the cover's frame is round: cover_frame.xyz its centre

// Every matrix is column-major in memory, as the CPU writes it.
public struct RbtrendLitScene {
    public float4x4 view_projection;
    public float4x4 inverse_view_projection;
    public RbtrendLitShadowView shadow_views[RBTREND_LIT_MAX_SHADOW_VIEWS];
    public RbtrendLitDirectional directional[RBTREND_LIT_MAX_DIRECTIONAL_LIGHTS];
    public RbtrendLitShadow shadows[RBTREND_LIT_MAX_SHADOW_VIEWS];
    public float4 eye_position;
    public float4 view_direction;          // xyz unit: the way the view looks
    public float4 ambient_up;              // xyz unit
    public float4 ambient_sky;
    public float4 ambient_ground;
    public float4 environment_rotation[3]; // rows, scene space into the environment's
    public float4 target_size;             // xy: the view's target in texels; zw: their reciprocals
    public float4 shadow_atlas_size;       // xy: the atlas in texels; zw: their reciprocals
    public float environment_intensity;
    public float environment_mips;         // the specular chain's mip count
    public float exposure;
    public uint output_encoding;           // RBTREND_COLOR_ENCODING
    public uint flags;
    public uint light_count;               // point and spot lights
    public uint directional_count;
    public uint tile_size;                 // in texels
    public uint tiles_x;
    public uint tiles_y;
    public float occlusion_strength;
    public float scene_color_mips;         // scene_color's mip count
    public RbtrendLitLight lights[RBTREND_LIT_MAX_LIGHTS];
    // The scene's atmosphere, where RBTREND_LIT_SCENE_ATMOSPHERE is set
    // (rbtrend_lit_scene_set_atmosphere()).
    public float4 atmosphere_center;       // xyz its centre; w the bottom's radius
    public float4 atmosphere_up;           // xyz the eye's up, unit; w the top's radius
    public float4 atmosphere_x;            // xyz the sky-view's x, towards the first sun's bearing; w the eye's distance from the centre
    public float4 atmosphere_y;            // xyz; w how many of the directional lights, the first, are its suns
    public float4 atmosphere_sizes;        // xy the transmittance table; zw the sky-view table, in texels
    public float4 atmosphere_aerial;       // x the aerial volume's slices; y how far its last is; zw its width and height
    public float4 atmosphere_array;        // xy the width and height of atmosphere_tables' layers, in texels
    // The scene's height fog, where RBTREND_LIT_SCENE_FOG is set
    // (rbtrend_lit_scene_set_fog(); rbtrend_lit_fog.slang reads it).
    public float4 fog_up_base;             // xyz a plane's up (0 for a sphere); w its base
    public float4 fog_eye;                 // xyz the eye less the fog's origin; w its density
    public float4 fog_albedo;              // rgb its albedo; w its phase's g
    public float4 fog_range;               // x its start, y its end, z its least transmittance, w its scale height
    public float4 fog_light;               // x how many of the directional lights, the first, light it; y its ambient share
    // The scene's cover, where RBTREND_LIT_SCENE_COVER is set
    // (rbtrend_lit_scene_set_cover(); rbtrend_lit_surface.slang reads it).
    public float4 cover_frame;             // xyz a round frame's centre or a flat one's unit up; w the snow line's height along it
    public float4 cover_snow;              // x slope_begin, y 1 / (slope_full - slope_begin); z 1 / the band; w the amount
    public float4 cover_wet;               // x the wetness, y its roughness; z, w half floats: the snow's red and green, its blue and roughness
};
[[vk::binding(0, 0)]] public ConstantBuffer<RbtrendLitScene> u_lit_scene : register(b0, space0);

// What directional light [slot] is at [position] (the scene's space): the
// way towards it (unit), 1 - cos of its disc's angular radius there, its
// radiance there, and how far a shadow ray from there towards it goes.
public struct DirectionalAt {
    public float3 direction;
    public float versine;
    public float3 radiance;
    public float reach;
};

// The rule's disc is at most 0.5 radians across from its centre:
// 1 - cos(0.5).
public static const float RBTREND_LIT_PLACED_MAX_VERSINE = 0.12241743810962728;
// A shadow ray towards a light from one direction: no bound of the light's.
public static const float RBTREND_LIT_UNBOUNDED_REACH = 3.0e38;

// Directional light [slot] at [position], by rbtrendlit.h's rule: a light
// from one direction its stored values, bit for bit, and no reach of its
// own; a placed one's worked out from its source's place (towards it; its
// radiance by the inverse square from its reference distance, the distance
// no less than its source's radius; its disc's angular radius
// asin(R / d') as a versine, at most 0.5; its reach to its source's
// surface). A point at the source's place takes the eye's direction. The
// branch is the light's, the same for every invocation.
public DirectionalAt directional_at(uint slot, float3 position) {
    RbtrendLitDirectional d = u_lit_scene.directional[slot];
    DirectionalAt at;
    at.direction = d.direction_versine.xyz;
    at.versine   = d.direction_versine.w;
    at.radiance  = d.radiance_disc.rgb;
    at.reach     = RBTREND_LIT_UNBOUNDED_REACH;
    if (d.place_reference.w > 0.0) {
        // The distance scaled by the largest part first, so no square
        // overflows however far the source is.
        float3 v       = d.place_reference.xyz - position;
        float largest  = max(max(abs(v.x), abs(v.y)), abs(v.z));
        float distance = largest > 0.0 ? largest * length(v / largest) : 0.0;
        if (distance > 0.0)
            at.direction = v / distance;
        float radius  = d.placed_radiance_radius.w;
        float nearest = max(distance, radius);
        float k       = d.place_reference.w / nearest;
        at.radiance   = d.placed_radiance_radius.rgb * (k * k);
        float q       = radius / nearest;
        at.versine    = min(q * q / (1.0 + sqrt(max(1.0 - q * q, 0.0))), RBTREND_LIT_PLACED_MAX_VERSINE);
        at.reach      = max(distance - radius, 0.0);
    }
    return at;
}

// The ambient light from [direction]: the sky above, the ground below, mixed
// across the horizon.
public float3 hemisphere(float3 direction) {
    float k = dot(direction, u_lit_scene.ambient_up.xyz) * 0.5 + 0.5;
    return lerp(u_lit_scene.ambient_ground.rgb, u_lit_scene.ambient_sky.rgb, k);
}

// [direction] in the scene's space turned into the environment's.
public float3 to_environment(float3 direction) {
    return float3(dot(u_lit_scene.environment_rotation[0].xyz, direction), dot(u_lit_scene.environment_rotation[1].xyz, direction),
                  dot(u_lit_scene.environment_rotation[2].xyz, direction));
}
