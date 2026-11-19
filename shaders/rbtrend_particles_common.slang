module rbtrend_particles_common;

// What every shader of Rend's particles shares (rbtrendparticles.h): the
// system's block, as each of its three sets holds it (mirrored by
// rbtrend_particles_block in src/render/rbtrendparticles_internal.hpp and
// checked member by member against this reflection when the layouts are
// made); a step's push constants; where each word of the system's record
// and arguments is; and the arithmetic the stages share. No bindings.

// RbtrendParticles.kind.x: which modules the effect has.
public static const uint MODULE_SPAWN           = 1u;
public static const uint MODULE_FORCES          = 2u;
public static const uint MODULE_NOISE           = 4u;
public static const uint MODULE_COLLISION       = 8u;
public static const uint MODULE_WRAP            = 16u;
public static const uint MODULE_COLOR_OVER_LIFE = 32u;
public static const uint MODULE_SIZE_OVER_LIFE  = 64u;
public static const uint MODULE_FLIPBOOK        = 128u;
public static const uint MODULE_SOFT            = 256u;
public static const uint MODULE_STREAK          = 512u;
public static const uint MODULE_MESH            = 1024u;
public static const uint MODULE_RIBBON          = 2048u;

// RBTREND_PARTICLE_DRAW, RBTREND_PARTICLE_SPACE and RBTREND_PARTICLE_SHAPE.
public static const uint DRAW_BILLBOARD = 1u;
public static const uint DRAW_STREAK    = 2u;
public static const uint DRAW_MESH      = 3u;
public static const uint DRAW_RIBBON    = 4u;
public static const uint SPACE_WORLD    = 1u;
public static const uint SPACE_EMITTER  = 2u;
public static const uint SHAPE_POINT    = 1u;
public static const uint SHAPE_SPHERE   = 2u;
public static const uint SHAPE_BOX      = 3u;
public static const uint SHAPE_CONE     = 4u;
public static const uint SHAPE_DISC     = 5u;

// The effect, written by rbtrend_particles_set_effect() (and the depth's
// direction by rbtrend_particles_set_depth()).
public struct RbtrendParticles {
    public uint4 kind;           // x the modules, y the draw, z the space, w the capacity
    public uint4 options;        // x the spawn's shape, y on its surface, z sorted, w the depth reversed
    public uint4 flipbook;       // x columns, y rows, z frames, w blended
    public float4 life_size;     // life min and max, size min and max
    public float4 rotation;      // rotation min and max, spin min and max
    public float4 color;         // linear RGBA at birth
    public float4 spawn_size;    // xyz the shape's sizes, w the spread
    public float4 spawn_speed;   // speed min and max, the emitter velocity inherited
    public float4 forces;        // xyz the acceleration, w the drag
    public float4 noise;         // x strength, y frequency
    public float4 noise_scroll;  // xyz tiles a second
    public float4 collision;     // restitution, friction, life loss, thickness
    public float4 wrap;          // xyz half extents
    public float4 draw;          // x flipbook frames a second, y soft distance, z streak seconds, w ribbon repeats
    public float4 streak;        // x length min, y length max
    public uint4 mesh;           // x index count, y first index, z vertex offset
    public float4 color_times[2];   // the colour curve's eight times
    public float4 color_values[8];  // its eight values
    public float4 size_times[2];    // the size curve's eight times
    public float4 size_values[2];   // its eight values
    public uint4 curves;            // x the colour curve's keys, y the size curve's
};

// A step's values (RBTREND_PARTICLES_STEP), the push constants of every
// stage of a step but the sort's: rbtrend_particles_step_push.
public struct ParticlesStep {
    public float4 anchor_dt;      // xyz the anchor in the scene's space, w the step's seconds
    public float4 moved_seed;     // xyz the anchor's move, w the seed's bits
    public float4 emitter[3];     // the emitter from the anchor: three rows
    public float4 velocity_spawn; // xyz the emitter's velocity, w the spawn count's bits
    public float4 wind_flags;     // xyz the wind, w the flags' bits (STEP_CLEAR)
};
public static const uint STEP_CLEAR = 1u;
// The system's first step ever: its record holds nothing yet, and its spawn
// count starts at 0 (a later clear keeps counting, so a ribbon's numbers
// never repeat).
public static const uint STEP_FIRST = 2u;

// The system's record, a word each (raw, as Carrottus GPU's D3D12 backend
// views every storage buffer): what its last step left.
public static const uint COUNTER_ALIVE    = 0u;  // alive after the last step
public static const uint COUNTER_CURRENT  = 1u;  // which alive list holds them, 0 or 1
public static const uint COUNTER_DEAD     = 2u;  // the dead list's length (an int while a spawn runs)
public static const uint COUNTER_APPENDED = 3u;  // this step's alive list's length so far
public static const uint COUNTER_SPAWNED  = 4u;  // particles spawned since the first step: the next one's number (taken by the spawn)
public static const uint COUNTER_GROUPS   = 5u;  // the sort's groups of 256 for the alive count
public static const uint COUNTER_TIME     = 6u;  // seconds since the last clear (a float's bits)
public static const uint COUNTER_RIBBON_FIRST = 7u; // the first number spawned since the last clear: where a ribbon starts
public static const uint COUNTER_ANCHOR   = 8u;  // four words: the last step's anchor
public static const uint COUNTER_EMITTER  = 12u; // twelve words: its emitter's three rows
public static const uint COUNTER_WORDS    = 24u;

// The arguments, a word each: the next simulation's dispatch, the alive
// count read back, the draw's and the sort's.
public static const uint ARGS_SIMULATE     = 0u;
public static const uint ARGS_ALIVE        = 3u;
public static const uint ARGS_DRAW         = 4u;
public static const uint ARGS_SORT         = 8u;
public static const uint ARGS_DRAW_INDEXED = 12u;
public static const uint ARGS_WORDS        = 20u;

// The threads of a simulation's, a spawn's and a clear's group; of a sort's.
public static const uint STEP_GROUP = 64u;
public static const uint SORT_GROUP = 256u;

// A particle: 48 bytes, three uint4 (raw words). w of the first its age,
// of the second its life; the third its size, rotation, spin and number.
public struct Particle {
    public float3 position;
    public float age;
    public float3 velocity;
    public float life;
    public float size;
    public float rotation;
    public float spin;
    public uint number;
};

public Particle particle_from_words(uint4 a, uint4 b, uint4 c) {
    Particle p;
    p.position = asfloat(a.xyz);
    p.age      = asfloat(a.w);
    p.velocity = asfloat(b.xyz);
    p.life     = asfloat(b.w);
    p.size     = asfloat(c.x);
    p.rotation = asfloat(c.y);
    p.spin     = asfloat(c.z);
    p.number   = c.w;
    return p;
}

// PCG's output function (Jarzynski and Olano, "Hash Functions for GPU
// Rendering", 2020): a 32-bit hash.
public uint particles_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Random value [k] (0 to 1, below 1) of the particle that is [n]th in the
// spawn of a step seeded [seed]: the same whatever slot it takes.
public float particles_random(uint seed, uint n, uint k) {
    return float(particles_hash(seed ^ particles_hash(n * 32u + k)) >> 8) * (1.0 / 16777216.0);
}

// A curve's value at [t] (0 to 1): its keys' times [times], values
// [values], linear between, the first before and the last after.
public float4 particles_curve(float4 times[2], float4 values[8], uint count, float t) {
    if (count == 0u)
        return float4(1.0);
    float4 v = values[0];
    if (t <= times[0].x)
        return v;
    for (uint i = 1u; i < min(count, 8u); ++i) {
        float t0 = times[(i - 1u) / 4u][(i - 1u) % 4u];
        float t1 = times[i / 4u][i % 4u];
        if (t <= t1) {
            float f = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0;
            return lerp(values[i - 1u], values[i], f);
        }
        v = values[i];
    }
    return v;
}

// The size curve's (its values one float each).
public float particles_size_curve(float4 times[2], float4 values[2], uint count, float t) {
    if (count == 0u)
        return 1.0;
    float v = values[0].x;
    if (t <= times[0].x)
        return v;
    for (uint i = 1u; i < min(count, 8u); ++i) {
        float t0 = times[(i - 1u) / 4u][(i - 1u) % 4u];
        float t1 = times[i / 4u][i % 4u];
        float v0 = values[(i - 1u) / 4u][(i - 1u) % 4u];
        float v1 = values[i / 4u][i % 4u];
        if (t <= t1) {
            float f = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0;
            return lerp(v0, v1, f);
        }
        v = v1;
    }
    return v;
}

// A ribbon's table (RBTREND_PARTICLE_DRAW_RIBBON), in the sort buffer's
// key lists (a ribbon is never sorted): for each number modulo the
// capacity, two words, the number and the slot of the particle that has it.
public uint ribbon_entry(uint number, uint capacity) {
    return 2u * (number % capacity);
}

// The sort's key of a particle [distance] from the eye: its log2 in 1/2048
// steps from 2^-16 to 2^16, inverted so the farthest sorts first.
public uint particles_key(float distance) {
    float l = clamp((log2(max(distance, 1.0e-30)) + 16.0) * 2048.0, 0.0, 65535.0);
    return 65535u - uint(l);
}

// The 3x3 part of [rows] inverted (its rows' cofactors over the
// determinant); the identity where it can't be.
public float3x3 particles_inverse3(float4 rows[3]) {
    float3 a  = rows[0].xyz;
    float3 b  = rows[1].xyz;
    float3 c  = rows[2].xyz;
    float3 r0 = cross(b, c);
    float3 r1 = cross(c, a);
    float3 r2 = cross(a, b);
    float det = dot(a, r0);
    if (abs(det) < 1.0e-30)
        return float3x3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    // The inverse's columns are r0, r1, r2 over det: built from its rows,
    // so transposed.
    return transpose(float3x3(r0, r1, r2)) / det;
}

// [rows] (a 3x4 matrix) times the point [p].
public float3 particles_transform(float4 rows[3], float3 p) {
    return float3(dot(rows[0].xyz, p) + rows[0].w, dot(rows[1].xyz, p) + rows[1].w, dot(rows[2].xyz, p) + rows[2].w);
}

// [rows]' 3x3 part times the direction [d].
public float3 particles_turn(float4 rows[3], float3 d) {
    return float3(dot(rows[0].xyz, d), dot(rows[1].xyz, d), dot(rows[2].xyz, d));
}
