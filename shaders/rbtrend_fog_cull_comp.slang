// The froxel grid's cull (RBTREND_COMMAND_LIT_FOG, rbtrendfog.h): a thread a
// tile (a column of the grid), its list the volumes whose bounding sphere
// meets the tile's frustum from the eye to the grid's far distance, in the
// volumes' order, the first RBTREND_FOG_MAX_TILE_VOLUMES kept (as the lit
// scene's tiles keep their lights). Set 0 is the lit scene's, declared whole.

import rbtrend_lit_scene;
import rbtrend_lit_bindings;
import rbtrend_fog_grid;
import rbtrend_fog_bindings;

// Whether a sphere of [radius] round [c] (from the eye) is on the inner
// side of the plane through the eye and the rays [a] and [b], [inner] a
// direction inside.
bool fog_inside_plane(float3 a, float3 b, float3 inner, float3 c, float radius) {
    float3 n = normalize(cross(a, b));
    if (dot(n, inner) < 0.0)
        n = -n;
    return dot(n, c) >= -radius;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rbtrend_fog_cull_comp(uint3 id : SV_DispatchThreadID) {
    uint2 size = u_fog.grid.xy;
    if (id.x >= size.x || id.y >= size.y)
        return;
    float2 lo     = float2(id.xy) / float2(size);
    float2 hi     = float2(id.xy + 1u) / float2(size);
    float3 c00    = fog_view_ray(lo);
    float3 c10    = fog_view_ray(float2(hi.x, lo.y));
    float3 c11    = fog_view_ray(hi);
    float3 c01    = fog_view_ray(float2(lo.x, hi.y));
    float3 middle = fog_view_ray((lo + hi) * 0.5);
    float far     = u_fog.range.y;
    uint base     = (id.y * size.x + id.x) * FOG_TILE_WORDS;
    uint count    = 0u;
    for (uint v = 0u; v < min(u_fog.grid.w, FOG_MAX_VOLUMES) && count < FOG_MAX_TILE_VOLUMES; ++v) {
        float4 s = u_fog.volumes[v].sphere;
        float3 c = s.xyz - u_lit_scene.eye_position.xyz;
        if (length(c) - s.w > far)
            continue;
        if (!fog_inside_plane(c00, c10, middle, c, s.w) || !fog_inside_plane(c10, c11, middle, c, s.w) || !fog_inside_plane(c11, c01, middle, c, s.w) ||
            !fog_inside_plane(c01, c00, middle, c, s.w))
            continue;
        FogTiles.Store((base + 1u + count) * 4u, v);
        ++count;
    }
    FogTiles.Store(base * 4u, count);
}
