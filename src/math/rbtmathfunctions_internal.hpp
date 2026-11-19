#pragma once

// RbtMath's elementary functions, one element each (rbtmathfunctions.h,
// "Definition"): the constants, the polynomials and the six evaluations,
// written in the Definition's order of operations, each double operation
// rounded once. Shared by rbtmathfunctions.cpp (the batch calls' loops) and
// rbtmathmedium.cpp (the shell medium's exponential).
//
// Every source that includes it is built with floating-point contraction
// off (CMakeLists.txt, rabbittus_no_contract): a fused multiply-add would
// round once where the Definition rounds twice. FLT_EVAL_METHOD is asserted
// 0, and fast math refused. No libm call: rint is the Definition's rounding
// constant, 2^k is made from its bits. Nothing here is writable: the
// constants are constexpr, in read-only data.

#include <bit>
#include <cfloat>
#include <cstdint>

static_assert(FLT_EVAL_METHOD == 0, "rbtmathfunctions' bits need every double operation rounded to binary64 once");
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error "rbtmathfunctions' bits need IEEE 754 arithmetic: built with fast math, they would differ between builds"
#endif

namespace rbtfn
{

inline uint64_t bits(double v) { return std::bit_cast<uint64_t>(v); }
inline double from(uint64_t u) { return std::bit_cast<double>(u); }

// ---- CONSTANTS -------------------------------------------------------------------

constexpr double SHIFT       = 0x1.8p52;
constexpr uint64_t QNAN_BITS = 0x7FF8000000000000ull;
constexpr double SPLITTER    = 0x1.0000002p+27;
constexpr double LOG2E       = 0x1.71547652b82fep+0;
constexpr double LN2_HI      = 0x1.62e42ffp-1;
constexpr double LN2_LO      = -0x1.718432a1b0e26p-35;
constexpr double IVLN2_HI    = 0x1.71547652p+0;
constexpr double IVLN2_LO    = 0x1.705fc2eefa2p-33;
constexpr double TWO_OVER_PI = 0x1.45f306dc9c883p-1;
constexpr double PIO2_1      = 0x1.921fb544p+0;
constexpr double PIO2_2      = 0x1.0b4611a6p-34;
constexpr double PIO2_3      = 0x1.3198a2ep-69;
constexpr double PIO2_4      = 0x1.b839a252049c1p-104;
constexpr double PIO4_HI     = 0x1.921fb54442d18p-1;
constexpr double PIO4_LO     = 0x1.1a62633145c07p-55;
constexpr double PIO2_HI     = 0x1.921fb54442d18p+0;
constexpr double PIO2_LO     = 0x1.1a62633145c07p-54;
constexpr double PI_HI       = 0x1.921fb54442d18p+1;
constexpr double PI_LO       = 0x1.1a62633145c07p-53;
constexpr double THREE_PIO4  = 0x1.2d97c7f3321d2p+1;
constexpr double ATAN_HI[4]  = {0x1.dac670561bb4fp-2, 0x1.921fb54442d18p-1, 0x1.f730bd281f69bp-1, 0x1.921fb54442d18p+0};
constexpr double ATAN_LO[4]  = {0x1.a2b7f222f65e2p-56, 0x1.1a62633145c07p-55, 0x1.007887af0cbbdp-56, 0x1.1a62633145c07p-54};

// Bounds the steps compare with, each exactly the power of two or the
// fraction the Definition names.
constexpr double TWO_M27     = 0x1p-27;
constexpr double TWO_66      = 0x1p66;
constexpr double TWO_900     = 0x1p900;
constexpr double TWO_M900    = 0x1p-900;
constexpr double TWO_600     = 0x1p600;
constexpr double TWO_M600    = 0x1p-600;
constexpr double TAN_MAX     = 0x1p20;
constexpr double TAN_SWITCH  = 0x1.58p-1;
constexpr double INF         = std::bit_cast<double>(0x7FF0000000000000ull);

// ---- POLYNOMIALS -----------------------------------------------------------------

constexpr double E2[11] = {0x1.62e42fefa39efp-1,  0x1.ebfbdff82c598p-3,  0x1.c6b08d704a0cbp-5, 0x1.3b2ab6fba1dd9p-7,
                           0x1.5d87fe78a2ccp-10,  0x1.4309130970158p-13, 0x1.ffcbfc7440db3p-17, 0x1.62bfd476d463cp-20,
                           0x1.b524dcba88458p-24, 0x1.e606408c6f3c5p-28, 0x1.ea04441abc917p-32};
constexpr double EE[11] = {0x1p+0,                0x1.0000000000005p-1,  0x1.555555555555ep-3,  0x1.55555555520acp-5,
                           0x1.111111110e367p-7,  0x1.6c16c17f44742p-10, 0x1.a01a01b6a20fdp-13, 0x1.a019a66801012p-16,
                           0x1.71dde8aed8788p-19, 0x1.28a2c32005f09p-22, 0x1.af78618388f85p-26};
constexpr double LG[7]  = {0x1.5555555555558p-2, 0x1.9999999995224p-3, 0x1.2492492e03d78p-3, 0x1.c71c62d050743p-4,
                           0x1.7462ba224a45p-4,  0x1.39fdba11582f2p-4, 0x1.2b5fb4b104032p-4};
constexpr double AT[12] = {-0x1.5555555555555p-2, 0x1.99999999998d2p-3,  -0x1.2492492486002p-3, 0x1.c71c71bd9b2adp-4,
                           -0x1.745d155eaf558p-4, 0x1.3b137787f7a4cp-4,  -0x1.110cb70db121cp-4, 0x1.e1727d35b3b72p-5,
                           -0x1.ab6624fd547eap-5, 0x1.7031d4e6f303dp-5,  -0x1.0fccd31bca1abp-5, 0x1.e5a8c9a296d27p-7};
constexpr double TN[15] = {0x1.5555555555555p-2,  0x1.1111111111088p-3,  0x1.ba1ba1ba2b85p-5,   0x1.664f487cef92fp-6,
                           0x1.226e367bb3c9p-7,   0x1.d6d38f33aa6cdp-9,  0x1.7da85001f510cp-10, 0x1.3517af0a9b129p-11,
                           0x1.fa202d204f4f1p-13, 0x1.781816cdeab9cp-14, 0x1.d78aa3f38b737p-15, -0x1.8dd7c60a09eafp-17,
                           0x1.38903e6ff4c13p-15, -0x1.39c55cf2fc233p-16, 0x1.1faa6f4f45aecp-17};

// ---- HELPERS ---------------------------------------------------------------------

inline double qnan() { return from(QNAN_BITS); }

// rint(v) for |v| < 2^51: round to nearest, ties to even.
inline double rint_shift(double v) { return (v + SHIFT) - SHIFT; }

// horner(c[first..n-1], v) = (...(c[n-1] v + c[n-2]) v + ...) v + c[first].
template <int N>
inline double horner(const double (&c)[N], int first, double v)
{
    double r = c[N - 1];
    for (int i = N - 2; i >= first; i--)
        r = r * v + c[i];
    return r;
}

// 2^k for k in [-1022, 1023].
inline double pow2(int64_t k) { return from((uint64_t)(k + 1023) << 52); }

// y 2^k for k in [-1076, 1024], rounded once.
inline double scale(double y, int64_t k)
{
    if (k > 1023)
        return (y * pow2(1023)) * 2.0;
    if (k >= -1022)
        return y * pow2(k);
    return (y * pow2(k + 600)) * TWO_M600;
}

inline double high26(double v) { return from(bits(v) & 0xFFFFFFFFF8000000ull); }

inline void split(double a, double *a_hi, double *a_lo)
{
    const double c = SPLITTER * a;
    *a_hi          = c - (c - a);
    *a_lo          = a - *a_hi;
}

// p + e = a b exactly, |a|, |b| < 2^996.
inline void two_prod(double a, double b, double *p, double *e)
{
    double a_hi, a_lo, b_hi, b_lo;
    split(a, &a_hi, &a_lo);
    split(b, &b_hi, &b_lo);
    *p = a * b;
    *e = (((a_hi * b_hi - *p) + a_hi * b_lo) + a_lo * b_hi) + a_lo * b_lo;
}

// s + e = a + b exactly.
inline void two_sum(double a, double b, double *s, double *e)
{
    *s             = a + b;
    const double v = *s - a;
    *e             = (a - (*s - v)) + (b - v);
}

// 1 + f c[0] + f^2 (c[1] + ...), the first term formed exactly; f_err is the
// error already known in f (0 for exp2).
template <int N>
inline double core(const double (&c)[N], double f, double f_err)
{
    const double c_hi = high26(c[0]);
    const double c_lo = c[0] - c_hi;
    const double f_hi = high26(f);
    const double f_lo = f - f_hi;
    const double a    = f_hi * c_hi;
    double s, e;
    two_sum(1.0, a, &s, &e);
    const double tail = (((f_lo * c_hi) + (f * c_lo)) + (f_err * c[0])) + (f * (f * horner(c, 1, f)));
    return s + (e + tail);
}

// ---- EXP2 ------------------------------------------------------------------------

inline double exp2(double x)
{
    if (x != x)
        return qnan();
    if (x > 1024.0)
        return INF;
    if (x < -1076.0)
        return 0.0;
    const double k = rint_shift(x);
    const double f = x - k;
    return scale(core(E2, f, 0.0), (int64_t)k);
}

// ---- EXP -------------------------------------------------------------------------

inline double exp(double x)
{
    if (x != x)
        return qnan();
    if (x > 710.0)
        return INF;
    if (x < -746.0)
        return 0.0;
    const double k    = rint_shift(x * LOG2E);
    const double a    = x - k * LN2_HI;
    const double b    = k * LN2_LO;
    const double r    = a - b;
    const double r_lo = (a - r) - b;
    return scale(core(EE, r, r_lo), (int64_t)k);
}

// ---- LOG2 ------------------------------------------------------------------------

inline double log2(double x)
{
    // Step 1.
    if (x != x)
        return qnan();
    if (x == 0.0)
        return -INF;
    uint64_t u = bits(x);
    if (u >> 63)
        return qnan();
    if (x == INF)
        return INF;
    // Step 2.
    int64_t e;
    if (u < (1ull << 52))
    {
        const int sh = std::countl_zero(u) - 11;
        e            = -1022 - sh;
        u            = ((u << sh) & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    }
    else
    {
        e = (int64_t)(u >> 52) - 1023;
        u = (u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    }
    // Step 3.
    if (u > 0x3FF6A09E667F3BCDull)
    {
        u = u - (1ull << 52);
        e = e + 1;
    }
    // Step 4.
    const double m    = from(u);
    const double f    = m - 1.0;
    const double hfsq = (0.5 * f) * f;
    const double s    = f / (2.0 + f);
    const double z    = s * s;
    const double R    = (2.0 * z) * horner(LG, 0, z);
    // Step 5.
    const double hi = from(bits(f - hfsq) & 0xFFFFFFFF00000000ull);
    const double lo = ((f - hi) - hfsq) + s * (hfsq + R);
    // Step 6.
    const double v_hi = hi * IVLN2_HI;
    double v_lo        = ((lo + hi) * IVLN2_LO) + (lo * IVLN2_HI);
    // Step 7.
    const double ed = (double)e;
    const double w  = ed + v_hi;
    v_lo            = v_lo + ((ed - w) + v_hi);
    return v_lo + w;
}

// ---- ATAN ------------------------------------------------------------------------

// atan(a) as head + tail, a in [2^-27, 2^66); corr the error already known in
// a, divided by 1 + a^2.
inline void atan_parts(double a, double corr, double *head, double *tail)
{
    int id;
    double t;
    if (a < 0x1.cp-2) // 7/16
    {
        id = -1;
        t  = a;
    }
    else if (a < 0x1.6p-1) // 11/16
    {
        id = 0;
        t  = ((2.0 * a) - 1.0) / (2.0 + a);
    }
    else if (a < 0x1.3p+0) // 19/16
    {
        id = 1;
        t  = (a - 1.0) / (a + 1.0);
    }
    else if (a < 0x1.38p+1) // 39/16
    {
        id = 2;
        t  = (a - 1.5) / (1.0 + (1.5 * a));
    }
    else
    {
        id = 3;
        t  = -1.0 / a;
    }
    const double z = t * t;
    const double w = t * (z * horner(AT, 0, z));
    if (id < 0)
    {
        *head = t;
        *tail = w + corr;
    }
    else
    {
        *head = ATAN_HI[id];
        *tail = ((w + ATAN_LO[id]) + corr) + t;
    }
}

inline double atan(double x)
{
    if (x != x)
        return qnan();
    const double a = from(bits(x) & 0x7FFFFFFFFFFFFFFFull);
    if (a < TWO_M27)
        return x;
    double r;
    if (a >= TWO_66)
        r = PIO2_HI;
    else
    {
        double head, tail;
        atan_parts(a, 0.0, &head, &tail);
        r = head + tail;
    }
    return (bits(x) >> 63) ? -r : r;
}

// ---- ATAN2 -----------------------------------------------------------------------

inline double atan2(double y, double x)
{
    // Step 1.
    if (y != y)
        return qnan();
    if (x != x)
        return qnan();
    const double a_y = from(bits(y) & 0x7FFFFFFFFFFFFFFFull);
    const double a_x = from(bits(x) & 0x7FFFFFFFFFFFFFFFull);
    const bool s_y   = (bits(y) >> 63) != 0;
    const bool s_x   = (bits(x) >> 63) != 0;
    // Step 2.
    double r;
    if (a_y == 0.0)
        r = s_x ? PI_HI : 0.0;
    else if (a_x == 0.0)
        r = PIO2_HI;
    else if (a_x == INF)
    {
        if (a_y == INF)
            r = s_x ? THREE_PIO4 : PIO4_HI;
        else
            r = s_x ? PI_HI : 0.0;
    }
    else if (a_y == INF)
        r = PIO2_HI;
    else
    {
        const double q = a_y / a_x;
        double head, tail;
        if (q >= TWO_66)
        {
            head = PIO2_HI;
            tail = PIO2_LO;
        }
        else if (q < TWO_M27)
        {
            head = q;
            tail = 0.0;
        }
        else
        {
            double b_y = a_y;
            double b_x = a_x;
            if (a_y > TWO_900 || a_x > TWO_900)
            {
                b_y = a_y * TWO_M600;
                b_x = a_x * TWO_M600;
            }
            else if (a_y < TWO_M900 || a_x < TWO_M900)
            {
                b_y = a_y * TWO_600;
                b_x = a_x * TWO_600;
            }
            double p, pe;
            two_prod(q, b_x, &p, &pe);
            const double corr = (((b_y - p) - pe) / b_x) / (1.0 + (q * q));
            atan_parts(q, corr, &head, &tail);
        }
        if (!s_x)
            r = head + tail;
        else
        {
            double d, e;
            two_sum(PI_HI, -head, &d, &e);
            r = d + ((e + PI_LO) - tail);
        }
    }
    // Step 3.
    return s_y ? -r : r;
}

// ---- TAN -------------------------------------------------------------------------

inline double tan(double x)
{
    // Step 1.
    if (x != x)
        return qnan();
    const double ax = from(bits(x) & 0x7FFFFFFFFFFFFFFFull);
    if (ax > TAN_MAX)
        return qnan();
    if (ax < TWO_M27)
        return x;
    // Step 2.
    const double k = rint_shift(x * TWO_OVER_PI);
    const double a = x - k * PIO2_1;
    const double b = k * PIO2_2;
    double r1, e1, r2, e2;
    two_sum(a, -b, &r1, &e1);
    const double c = k * PIO2_3;
    two_sum(r1, -c, &r2, &e2);
    const double lo   = (e1 + e2) - (k * PIO2_4);
    const double r_hi = r2 + lo;
    const double r_lo = (r2 - r_hi) + lo;
    const bool k_odd  = (((int64_t)k) & 1) != 0;
    // Step 3.
    const double a_r = from(bits(r_hi) & 0x7FFFFFFFFFFFFFFFull);
    if (a_r >= TAN_SWITCH)
    {
        const double sg = (bits(r_hi) >> 63) ? -1.0 : 1.0;
        const double yy = (PIO4_HI - (sg * r_hi)) + (PIO4_LO - (sg * r_lo));
        const double z  = yy * yy;
        const double ty = yy + (yy * (z * horner(TN, 0, z)));
        if (!k_odd)
        {
            const double v = (ty + ty) / (1.0 + ty);
            return sg * (1.0 - v);
        }
        const double v = (ty + ty) / (1.0 - ty);
        return (-sg) * (1.0 + v);
    }
    // Step 4.
    const double z   = r_hi * r_hi;
    const double w   = r_hi * (z * horner(TN, 0, z));
    const double t_n = r_hi + w;
    const double t_l = ((r_hi - t_n) + w) + (r_lo * (1.0 + (t_n * t_n)));
    const double t   = t_n + t_l;
    if (!k_odd)
        return t;
    const double t_lo = (t_n - t) + t_l;
    const double q    = -1.0 / t;
    double p, pe;
    two_prod(q, t, &p, &pe);
    const double d = ((1.0 + p) + pe) + (q * t_lo);
    return q + (q * d);
}

} // namespace rbtfn
