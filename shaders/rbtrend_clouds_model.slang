module rbtrend_clouds_model;

// The density Rend's clouds march (rbtrendclouds.h): Rabbittus Math's
// definition (math/rbtmathclouds.h, its steps 1 to 7) worked out in a
// shader, to a tolerance: the noise volumes read by the GPU's trilinear
// filter with repeat (rbtmathnoise.h, "Reading a volume"), the coverage map
// read by hand texel by texel (Math's face table, each face's indices
// clamped, a slab's wrapped or clamped), the equi-angular warp by the GPU's
// atan. A layer and a look as the compile resolves them: the march's block
// (rbtrend_clouds_bindings) mirrors rbtrend_clouds_layer_block and
// rbtrend_clouds_look_block (src/render/rbtrendclouds_internal.hpp). No
// bindings: the march and rabbittustests' density probe each declare their
// own.

// A look, resolved: Math's shape and Rend's light.
public struct CloudLook {
    public float4 profiles[4]; // a type's (a, b, c, d) each
    public float4 tiles;       // x base tile, y detail tile, z erosion, w billow height
    public float4 shape;       // x density scale, y coverage scale, z coverage bias, w type count
    public float4 albedo;      // rgb the albedo, w the extinction at density 1
    public float4 phase;       // x forward g, y backward g, z the forward's weight, w octaves
    public float4 octave;      // x a (extinction), y b (light), z c (phase), w the ambient light's share
    public float4 spare;
};

// A layer, resolved for the frame.
public struct CloudLayer {
    public float4 axes[3];           // rows: the scene's space into the layer's frame
    public float4 origin_kind;       // xyz the bounds' origin in the scene's space; w 1 shell, 2 slab
    public float4 bounds;            // x bottom, y top, z a slab's map extent, w a slab's address (1 repeat, 2 clamp) or a shell's warp (1 plain, 2 equi-angular)
    public float4 up_look;           // xyz a slab's up (its frame's); w the look's slot
    public float4 map_axis_layer;    // xyz a slab's map axis (its frame's); w the map's first layer of the maps' array
    public float4 base_offset;       // xyz the base volume's offset; w a slab's map offset x
    public float4 detail_offset;     // xyz the detail's; w a slab's map offset y
    public float4 light_direction[2];// xyz towards the light (the scene's space), w 1 where lit
    public float4 light_illuminance[2]; // rgb pi x its radiance
    public float4 atmosphere;        // xyz its centre (the scene's space), w 1 where read
    public float4 atmosphere_radii;  // x bottom, y top, zw its transmittance table's size
    public float4 spare;
};

public static const uint CLOUDS_MAX_LAYERS = 4u;
public static const uint CLOUDS_MAX_LOOKS = 16u;

// A point of the scene's space in the layer's frame, relative to its origin.
public float3 cloud_local(CloudLayer l, float3 p) {
    float3 d = p - l.origin_kind.xyz;
    return float3(dot(l.axes[0].xyz, d), dot(l.axes[1].xyz, d), dot(l.axes[2].xyz, d));
}

// A direction of the scene's space in the layer's frame.
public float3 cloud_local_direction(CloudLayer l, float3 v) {
    return float3(dot(l.axes[0].xyz, v), dot(l.axes[1].xyz, v), dot(l.axes[2].xyz, v));
}

// Step 1: the height as a fraction of the thickness (a point of the layer's
// frame).
public float cloud_height(CloudLayer l, float3 p) {
    float y = l.origin_kind.w < 1.5 ? length(p) - l.bounds.x : dot(p, l.up_look.xyz) - l.bounds.x;
    return y / (l.bounds.y - l.bounds.x);
}

int cloud_wrap(int i, int m) {
    int r = i % m;
    return r < 0 ? r + m : r;
}

// A map face's bilinear read by hand (step 2): x then y.
float3 cloud_map_read(Texture2DArray<float4> maps, float layer, float s, float t, bool repeat) {
    uint w, h, n;
    maps.GetDimensions(w, h, n);
    int m   = int(w);
    float u = s * float(m) - 0.5;
    float v = t * float(m) - 0.5;
    float fu = floor(u), fv = floor(v);
    int iu = int(fu), iv = int(fv);
    float tu = u - fu, tv = v - fv;
    int x0, x1, y0, y1;
    if (repeat) {
        x0 = cloud_wrap(iu, m);
        x1 = cloud_wrap(iu + 1, m);
        y0 = cloud_wrap(iv, m);
        y1 = cloud_wrap(iv + 1, m);
    } else {
        x0 = clamp(iu, 0, m - 1);
        x1 = clamp(iu + 1, 0, m - 1);
        y0 = clamp(iv, 0, m - 1);
        y1 = clamp(iv + 1, 0, m - 1);
    }
    int z    = int(layer);
    float3 a = maps.Load(int4(x0, y0, z, 0)).rgb;
    float3 b = maps.Load(int4(x1, y0, z, 0)).rgb;
    float3 c = maps.Load(int4(x0, y1, z, 0)).rgb;
    float3 d = maps.Load(int4(x1, y1, z, 0)).rgb;
    float3 r0 = a + tu * (b - a);
    float3 r1 = c + tu * (d - c);
    return r0 + tv * (r1 - r0);
}

float cloud_warp(float g, bool equi) {
    return equi ? atan(g) * 1.27323954474 : g;
}

// Step 2: the map's coverage, type and density factor at p (the layer's frame).
public float3 cloud_coverage(CloudLayer l, Texture2DArray<float4> maps, float3 p) {
    float first = l.map_axis_layer.w;
    if (l.origin_kind.w < 1.5) {
        float3 a = abs(p);
        float face, sc, tc, ma;
        if (a.x >= a.y && a.x >= a.z) {
            face = p.x >= 0.0 ? 0.0 : 1.0;
            ma   = a.x;
            sc   = p.x >= 0.0 ? -p.z : p.z;
            tc   = -p.y;
        } else if (a.y >= a.z) {
            face = p.y >= 0.0 ? 2.0 : 3.0;
            ma   = a.y;
            sc   = p.x;
            tc   = p.y >= 0.0 ? p.z : -p.z;
        } else {
            face = p.z >= 0.0 ? 4.0 : 5.0;
            ma   = a.z;
            sc   = p.z >= 0.0 ? p.x : -p.x;
            tc   = -p.y;
        }
        bool equi = l.bounds.w > 1.5;
        float s   = (cloud_warp(sc / ma, equi) + 1.0) * 0.5;
        float t   = (cloud_warp(tc / ma, equi) + 1.0) * 0.5;
        return cloud_map_read(maps, first + face, s, t, false);
    }
    float3 v = cross(l.up_look.xyz, l.map_axis_layer.xyz);
    float s  = dot(p, l.map_axis_layer.xyz) / l.bounds.z + l.base_offset.w;
    float t  = dot(p, v) / l.bounds.z + l.detail_offset.w;
    return cloud_map_read(maps, first, s, t, l.bounds.w < 1.5);
}

float cloud_profile(float4 q, float h) {
    return min(saturate((h - q.x) / (q.y - q.x)), saturate((q.w - h) / (q.w - q.z)));
}

// Steps 1 to 7: the layer's density at p (the layer's frame), unitless; 0
// outside it. The noise volumes read through [repeat], a linear sampler
// with repeat addressing.
public float cloud_density(CloudLayer l, CloudLook k, float3 p, Texture3D<float4> base, Texture3D<float4> detail, Texture2DArray<float4> maps,
                           SamplerState repeat) {
    float h = cloud_height(l, p);
    if (!(h >= 0.0 && h <= 1.0))
        return 0.0;
    float3 m = cloud_coverage(l, maps, p);
    float c  = saturate(m.r * k.shape.y + k.shape.z);
    if (c <= 0.0)
        return 0.0;
    // Step 3.
    uint types = uint(k.shape.w);
    float P;
    if (types <= 1u) {
        P = cloud_profile(k.profiles[0], h);
    } else {
        float x  = m.g * float(types - 1u);
        float fi = min(floor(x), float(types - 2u));
        uint i   = uint(fi);
        float f  = x - fi;
        float p0 = cloud_profile(k.profiles[i], h);
        float p1 = cloud_profile(k.profiles[i + 1u], h);
        P        = p0 + f * (p1 - p0);
    }
    // Step 4.
    float4 b     = base.SampleLevel(repeat, p / k.tiles.x + l.base_offset.xyz, 0.0);
    float lo     = b.g * 0.625 + b.b * 0.25 + b.a * 0.125;
    float e      = saturate((b.r - (lo - 1.0)) / (1.0 - (lo - 1.0)));
    float shaped = e * P;
    // Step 5.
    float d0 = saturate((shaped - (1.0 - c)) / c) * c;
    if (d0 <= 0.0)
        return 0.0;
    // Step 6.
    float4 n  = detail.SampleLevel(repeat, p / k.tiles.y + l.detail_offset.xyz, 0.0);
    float w   = n.r * 0.625 + n.g * 0.25 + n.b * 0.125;
    float mm  = w + saturate(h / k.tiles.w) * ((1.0 - w) - w);
    float e2  = mm * k.tiles.z;
    if (e2 >= 1.0)
        return 0.0;
    float d1 = saturate((d0 - e2) / (1.0 - e2));
    // Step 7.
    return d1 * k.shape.x * m.b;
}
