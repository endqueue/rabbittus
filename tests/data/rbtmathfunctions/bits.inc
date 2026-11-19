// Recorded on macOS 27 (MacBook, Apple M5 Pro, Apple clang, arm64),
// 2026-11-18, the first machine to run rbtmathfunctions_bits_tests
// (tests/rbtmathfunctions_bits_tests.c), Debug and Release alike, from
// rbtmathfunctions.h's Definition (card A61, the approved draft's version 1).
// These are worlds: a released function's record changes only with a reason
// named here. Owed: Windows (MSVC, x64) and Fedora (GCC and Clang, x64) to
// match every line. ("exp2 subnormal x" and "exp subnormal x" are one hash:
// every result is 1.)
    {"exp2 any encoding", 0xC5FDF0602BAD5FEBull},
    {"exp2 |x| < 2", 0x55EEA4196BC7D860ull},
    {"exp2 |x| < 2^11", 0xAA17D6BDBCC0C016ull},
    {"exp2 subnormal x", 0x313A04E3F5222325ull},
    {"exp any encoding", 0xD6053FFEE53A6255ull},
    {"exp |x| < 2", 0x61B8D508F2D840DBull},
    {"exp |x| < 2^10", 0x035F305B714404F0ull},
    {"exp subnormal x", 0x313A04E3F5222325ull},
    {"log2 any encoding", 0xE2C1CA89B5B23440ull},
    {"log2 any positive", 0xF59702D355CEE6B0ull},
    {"log2 [0.5, 2)", 0x8DA89C22E5ABD585ull},
    {"log2 subnormal x", 0x8371E6E604A14024ull},
    {"atan any encoding", 0x45793A804A845D86ull},
    {"atan |x| in [2^-30, 8)", 0x18A4158968B5B393ull},
    {"atan interval edges", 0x7DE8B94BF3E5EBE8ull},
    {"atan subnormal x", 0x0EB0DB33F91CDD3Bull},
    {"atan2 any encodings", 0xC25ABE0B7F03423Aull},
    {"atan2 four quadrants", 0xA417E9F6AEE63390ull},
    {"atan2 both about 2^960", 0xA49F43058A0CF8BAull},
    {"atan2 both about 2^-1000", 0xFACA5997AD1AFA21ull},
    {"atan2 one subnormal", 0x7F6ABCB154F17A52ull},
    {"atan2 ratios about 2^-27 and 2^66", 0x0332D7B5AAABBA0Full},
    {"tan any encoding", 0x46A4F09FA1489D17ull},
    {"tan |x| < 2", 0x866D6E8E594DB9C9ull},
    {"tan |x| in [1, 32)", 0x28AE799ECBF7944Bull},
    {"tan about its switch", 0xCE3CB0A0AD1FDE1Full},
    {"tan |x| < 2^21", 0xAC0E79DB7B36B5A6ull},
    {"tan about 2^20", 0xC00830BCDDE98AF9ull},
    {"tan subnormal x", 0x17C56907921205C2ull},
