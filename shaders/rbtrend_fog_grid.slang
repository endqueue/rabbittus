module rbtrend_fog_grid;

// What the froxel grid's passes share (rbtrendfog.h, RBTREND_COMMAND_LIT_FOG):
// a grid of W x H columns over the view, each the ray through its middle,
// and S slices along each ray by distance from the eye, spaced
// exponentially from the near distance n to the far distance f: slice i
// from n (f / n)^(i / S) to n (f / n)^((i + 1) / S), the first reaching
// back to the eye. A distance d is at slice coordinate k(d) = S ln(d / n) /
// ln(f / n), so slice i's far side is k = i + 1.

// The distance at slice coordinate [k] of a grid from [near] with
// [log_ratio] = ln(far / near) over [slices].
public float fog_slice_distance(float k, float near, float log_ratio, float slices) {
    return near * exp(log_ratio * (k / slices));
}

// The slice coordinate of distance [d].
public float fog_slice_of(float d, float near, float log_ratio, float slices) {
    return slices * log(max(d, 1e-20) / near) / log_ratio;
}
