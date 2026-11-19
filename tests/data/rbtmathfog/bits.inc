// Recorded on macOS 27 (MacBook, Apple M5 Pro, Apple clang, arm64),
// 2026-11-18, the first machine to run rbtmathfog_bits_tests
// (tests/rbtmathfog_bits_tests.c), Debug and Release alike: card A59's fog
// (rbtmathfog.h) and the baked noise volumes it reads (rbtmathnoise.h, card
// A57's Math half). Owed: Windows (MSVC, x64) and Fedora (GCC and Clang,
// x64) to match every line (card X25).
    {"plane density", 0x5901D185F315E96Aull},
    {"plane transmittance", 0x5CF2AB0AE38B31FFull},
    {"sphere density", 0xF8335396A0C1AE0Full},
// Re-recorded 2026-11-18, once, by name: "sphere transmittance" was
// 0x0A9590D0AF0C2690 under the first form of a sphere's outward depth
// (c / ((c - 1) mu + 1), 14% off the Chapman function), now the Chapman
// function's large-x form through erfcx (rbtmathfog.h, "Accuracy").
    {"sphere transmittance", 0xB1EBDB4C0C75E869ull},
    {"box", 0xCA4A6752A519DE75ull},
    {"box with noise", 0x6084B5344D47667Cull},
    {"sphere volume", 0x7D85F5964CCE5CC3ull},
    {"sphere volume with noise", 0xB9DF8E0DCCB319EEull},
    {"PERLIN_WORLEY 32^3 bake", 0xA8AE8B33C540728Cull},
    {"WORLEY 32^3 bake", 0x9261D2991F4D3A28ull},
