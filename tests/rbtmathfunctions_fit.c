// The Remez fitter behind rbtmathfunctions.h's polynomials (card A61, the
// approved draft's section 10): each fit's function, interval and degree,
// fitted for least absolute error by Remez's exchange in MPFR at 400 bits,
// then each coefficient rounded to the nearest double and printed in hex
// beside the Definition's, so a coefficient can be checked or a fit redone.
//
// Run by hand, not built: it needs MPFR, which isn't a dependency of
// Rabbittus and won't become one for a program run once. On macOS with
// Homebrew's MPFR (brew install mpfr), from rabbittus/:
//
//   cc -O2 -std=c11 -I/opt/homebrew/include tests/rbtmathfunctions_fit.c \
//       -L/opt/homebrew/lib -lmpfr -lgmp -o /tmp/rbtmathfunctions_fit
//   /tmp/rbtmathfunctions_fit            (every fit, at the Definition's bounds)
//   /tmp/rbtmathfunctions_fit --exact    (each bound the exact value it rounds)
//
// On Linux the same without the Homebrew paths (Fedora: mpfr-devel). It
// prints each fit's minimax error (before rounding to doubles) as a power of
// 2, for each coefficient the fitted double and whether it is the
// Definition's, and the error of both polynomials as doubles on a grid.
//
// Run on the MacBook (MPFR 4.2.2) on 2026-11-18, when the functions were
// built: every fit's minimax error is the draft's (2^-56.18, -55.65, -52.54,
// -57.94, -57.39), and each polynomial's first three to six coefficients
// are the Definition's; the higher ones differ in their last bits (35 of
// 56), at the Definition's bounds and at the exact ones alike, and stay put
// from a grid of 4,000 to 16,000: the draft's own fitter (a throwaway,
// deleted) stopped at a nearby polynomial of the same error. The
// Definition's coefficients are the normative ones; this fit is the check
// that they are near-minimax.
//
// The fits (v the polynomial's variable):
//   E2: (2^f - 1) / f,              v = f in [-0.5, 0.5],      degree 10
//   EE: (e^r - 1) / r,              v = r in [-0.34658, 0.34658], degree 10
//   LG: (atanh(s)/s - 1) / z,       v = z = s^2 in [0, 0.02944],  degree 6
//   AT: (atan(t)/t - 1) / z,        v = z = t^2 in [0, 0.19142],  degree 11
//   TN: (tan(r)/r - 1) / z,         v = z = r^2 in [0, 0.6169],   degree 14
// With --exact the bounds are ln 2 / 2, ((sqrt 2 - 1)/(sqrt 2 + 1))^2,
// (7/16)^2 and (pi/4)^2, the values the Definition's decimals round.

#include <gmp.h>
#include <mpfr.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PREC = 400, MAX_TERMS = 16, GRID = 4000 };

typedef enum { FIT_E2, FIT_EE, FIT_LG, FIT_AT, FIT_TN } fit_kind;

typedef struct fit
{
    fit_kind kind;
    const char *name;
    int degree;
    const char *lo;
    const char *hi;
    const double *definition;
} fit;

static const double E2[11] = {0x1.62e42fefa39efp-1,  0x1.ebfbdff82c598p-3,  0x1.c6b08d704a0cbp-5, 0x1.3b2ab6fba1dd9p-7,
                              0x1.5d87fe78a2ccp-10,  0x1.4309130970158p-13, 0x1.ffcbfc7440db3p-17, 0x1.62bfd476d463cp-20,
                              0x1.b524dcba88458p-24, 0x1.e606408c6f3c5p-28, 0x1.ea04441abc917p-32};
static const double EE[11] = {0x1p+0,                0x1.0000000000005p-1,  0x1.555555555555ep-3,  0x1.55555555520acp-5,
                              0x1.111111110e367p-7,  0x1.6c16c17f44742p-10, 0x1.a01a01b6a20fdp-13, 0x1.a019a66801012p-16,
                              0x1.71dde8aed8788p-19, 0x1.28a2c32005f09p-22, 0x1.af78618388f85p-26};
static const double LG[7] = {0x1.5555555555558p-2, 0x1.9999999995224p-3, 0x1.2492492e03d78p-3, 0x1.c71c62d050743p-4,
                             0x1.7462ba224a45p-4,  0x1.39fdba11582f2p-4, 0x1.2b5fb4b104032p-4};
static const double AT[12] = {-0x1.5555555555555p-2, 0x1.99999999998d2p-3, -0x1.2492492486002p-3, 0x1.c71c71bd9b2adp-4,
                              -0x1.745d155eaf558p-4, 0x1.3b137787f7a4cp-4, -0x1.110cb70db121cp-4, 0x1.e1727d35b3b72p-5,
                              -0x1.ab6624fd547eap-5, 0x1.7031d4e6f303dp-5, -0x1.0fccd31bca1abp-5, 0x1.e5a8c9a296d27p-7};
static const double TN[15] = {0x1.5555555555555p-2,  0x1.1111111111088p-3,  0x1.ba1ba1ba2b85p-5,   0x1.664f487cef92fp-6,
                              0x1.226e367bb3c9p-7,   0x1.d6d38f33aa6cdp-9,  0x1.7da85001f510cp-10, 0x1.3517af0a9b129p-11,
                              0x1.fa202d204f4f1p-13, 0x1.781816cdeab9cp-14, 0x1.d78aa3f38b737p-15, -0x1.8dd7c60a09eafp-17,
                              0x1.38903e6ff4c13p-15, -0x1.39c55cf2fc233p-16, 0x1.1faa6f4f45aecp-17};

static const fit fits[] = {
    {FIT_E2, "E2", 10, "-0.5", "0.5", E2},
    {FIT_EE, "EE", 10, "-0.34658", "0.34658", EE},
    {FIT_LG, "LG", 6, "0", "0.02944", LG},
    {FIT_AT, "AT", 11, "0", "0.19142", AT},
    {FIT_TN, "TN", 14, "0", "0.6169", TN},
};

// The function fitted, at v; its limit at v = 0.
static void target(fit_kind kind, mpfr_t out, const mpfr_t v)
{
    mpfr_t s, t;
    mpfr_inits2(PREC, s, t, (mpfr_ptr)0);
    if (mpfr_zero_p(v))
    {
        switch (kind)
        {
        case FIT_E2: mpfr_const_log2(out, MPFR_RNDN); break;
        case FIT_EE: mpfr_set_ui(out, 1, MPFR_RNDN); break;
        case FIT_LG:
        case FIT_TN: mpfr_set_ui(out, 1, MPFR_RNDN); mpfr_div_ui(out, out, 3, MPFR_RNDN); break;
        case FIT_AT: mpfr_set_si(out, -1, MPFR_RNDN); mpfr_div_ui(out, out, 3, MPFR_RNDN); break;
        }
        mpfr_clears(s, t, (mpfr_ptr)0);
        return;
    }
    switch (kind)
    {
    case FIT_E2:
        mpfr_exp2(t, v, MPFR_RNDN);
        mpfr_sub_ui(t, t, 1, MPFR_RNDN);
        mpfr_div(out, t, v, MPFR_RNDN);
        break;
    case FIT_EE:
        mpfr_expm1(t, v, MPFR_RNDN);
        mpfr_div(out, t, v, MPFR_RNDN);
        break;
    case FIT_LG:
    case FIT_AT:
    case FIT_TN:
        mpfr_sqrt(s, v, MPFR_RNDN);
        if (kind == FIT_LG)
            mpfr_atanh(t, s, MPFR_RNDN);
        else if (kind == FIT_AT)
            mpfr_atan(t, s, MPFR_RNDN);
        else
            mpfr_tan(t, s, MPFR_RNDN);
        mpfr_div(t, t, s, MPFR_RNDN);
        mpfr_sub_ui(t, t, 1, MPFR_RNDN);
        mpfr_div(out, t, v, MPFR_RNDN);
        break;
    }
    mpfr_clears(s, t, (mpfr_ptr)0);
}

// p(v) - g(v) for the coefficients c[0..n].
static void error_at(fit_kind kind, mpfr_t out, const mpfr_t v, mpfr_t *c, int n)
{
    mpfr_t p, g;
    mpfr_inits2(PREC, p, g, (mpfr_ptr)0);
    mpfr_set(p, c[n], MPFR_RNDN);
    for (int i = n - 1; i >= 0; i--)
    {
        mpfr_mul(p, p, v, MPFR_RNDN);
        mpfr_add(p, p, c[i], MPFR_RNDN);
    }
    target(kind, g, v);
    mpfr_sub(out, p, g, MPFR_RNDN);
    mpfr_clears(p, g, (mpfr_ptr)0);
}

// Solves sum_j c_j x_i^j + (-1)^i E = g(x_i) for the m = n + 2 points.
static void solve(fit_kind kind, mpfr_t *x, int n, mpfr_t *c, mpfr_t e)
{
    const int m = n + 2;
    mpfr_t a[MAX_TERMS + 1][MAX_TERMS + 2];
    for (int i = 0; i < m; i++)
        for (int j = 0; j <= m; j++)
            mpfr_init2(a[i][j], PREC);
    for (int i = 0; i < m; i++)
    {
        mpfr_set_ui(a[i][0], 1, MPFR_RNDN);
        for (int j = 1; j <= n; j++)
            mpfr_mul(a[i][j], a[i][j - 1], x[i], MPFR_RNDN);
        mpfr_set_si(a[i][n + 1], (i & 1) ? -1 : 1, MPFR_RNDN);
        target(kind, a[i][m], x[i]);
    }
    mpfr_t f, tmp;
    mpfr_inits2(PREC, f, tmp, (mpfr_ptr)0);
    for (int col = 0; col < m; col++)
    {
        int pivot = col;
        for (int r = col + 1; r < m; r++)
            if (mpfr_cmpabs(a[r][col], a[pivot][col]) > 0)
                pivot = r;
        if (pivot != col)
            for (int j = 0; j <= m; j++)
                mpfr_swap(a[col][j], a[pivot][j]);
        for (int r = 0; r < m; r++)
        {
            if (r == col)
                continue;
            mpfr_div(f, a[r][col], a[col][col], MPFR_RNDN);
            for (int j = col; j <= m; j++)
            {
                mpfr_mul(tmp, f, a[col][j], MPFR_RNDN);
                mpfr_sub(a[r][j], a[r][j], tmp, MPFR_RNDN);
            }
        }
    }
    for (int i = 0; i <= n; i++)
        mpfr_div(c[i], a[i][m], a[i][i], MPFR_RNDN);
    mpfr_div(e, a[n + 1][m], a[n + 1][n + 1], MPFR_RNDN);
    mpfr_clears(f, tmp, (mpfr_ptr)0);
    for (int i = 0; i < m; i++)
        for (int j = 0; j <= m; j++)
            mpfr_clear(a[i][j]);
}

// The extremum of |error| near v (between lo and hi), by golden section.
static void refine(fit_kind kind, mpfr_t v, const mpfr_t lo, const mpfr_t hi, mpfr_t *c, int n)
{
    mpfr_t a, b, x1, x2, e1, e2, gr, w;
    mpfr_inits2(PREC, a, b, x1, x2, e1, e2, gr, w, (mpfr_ptr)0);
    mpfr_set(a, lo, MPFR_RNDN);
    mpfr_set(b, hi, MPFR_RNDN);
    mpfr_sqrt_ui(gr, 5, MPFR_RNDN);
    mpfr_sub_ui(gr, gr, 1, MPFR_RNDN);
    mpfr_div_2ui(gr, gr, 1, MPFR_RNDN); // 0.618...
    for (int it = 0; it < 200; it++)
    {
        mpfr_sub(w, b, a, MPFR_RNDN);
        mpfr_mul(w, w, gr, MPFR_RNDN);
        mpfr_sub(x1, b, w, MPFR_RNDN);
        mpfr_add(x2, a, w, MPFR_RNDN);
        error_at(kind, e1, x1, c, n);
        error_at(kind, e2, x2, c, n);
        mpfr_abs(e1, e1, MPFR_RNDN);
        mpfr_abs(e2, e2, MPFR_RNDN);
        if (mpfr_cmp(e1, e2) > 0)
            mpfr_set(b, x2, MPFR_RNDN);
        else
            mpfr_set(a, x1, MPFR_RNDN);
    }
    mpfr_add(v, a, b, MPFR_RNDN);
    mpfr_div_2ui(v, v, 1, MPFR_RNDN);
    mpfr_clears(a, b, x1, x2, e1, e2, gr, w, (mpfr_ptr)0);
}

// One fit: returns the minimax error's log2 and fills c[0..n].
static double remez(fit_kind kind, int n, const mpfr_t lo, const mpfr_t hi, mpfr_t *c)
{
    const int m = n + 2;
    mpfr_t x[MAX_TERMS + 2], e, t, mid, half, grid[GRID + 1], gerr[GRID + 1];
    for (int i = 0; i < m; i++)
        mpfr_init2(x[i], PREC);
    for (int i = 0; i <= GRID; i++)
    {
        mpfr_init2(grid[i], PREC);
        mpfr_init2(gerr[i], PREC);
    }
    mpfr_inits2(PREC, e, t, mid, half, (mpfr_ptr)0);
    // Chebyshev's extrema to start.
    mpfr_add(mid, lo, hi, MPFR_RNDN);
    mpfr_div_2ui(mid, mid, 1, MPFR_RNDN);
    mpfr_sub(half, hi, lo, MPFR_RNDN);
    mpfr_div_2ui(half, half, 1, MPFR_RNDN);
    for (int i = 0; i < m; i++)
    {
        mpfr_const_pi(t, MPFR_RNDN);
        mpfr_mul_ui(t, t, (unsigned long)(m - 1 - i), MPFR_RNDN);
        mpfr_div_ui(t, t, (unsigned long)(m - 1), MPFR_RNDN);
        mpfr_cos(t, t, MPFR_RNDN);
        mpfr_mul(t, t, half, MPFR_RNDN);
        mpfr_add(x[i], mid, t, MPFR_RNDN);
    }
    double result = 0.0;
    for (int iteration = 0; iteration < 60; iteration++)
    {
        solve(kind, x, n, c, e);
        // The error on a grid, then its alternating extrema.
        for (int i = 0; i <= GRID; i++)
        {
            mpfr_sub(t, hi, lo, MPFR_RNDN);
            mpfr_mul_ui(t, t, (unsigned long)i, MPFR_RNDN);
            mpfr_div_ui(t, t, GRID, MPFR_RNDN);
            mpfr_add(grid[i], lo, t, MPFR_RNDN);
            error_at(kind, gerr[i], grid[i], c, n);
        }
        int idx[GRID + 1], count = 0;
        for (int i = 0; i <= GRID; i++)
        {
            const bool left = i == 0 || mpfr_cmpabs(gerr[i], gerr[i - 1]) >= 0;
            const bool right = i == GRID || mpfr_cmpabs(gerr[i], gerr[i + 1]) >= 0;
            if (!(left && right))
                continue;
            // Same sign as the last kept: keep the larger.
            if (count > 0 && mpfr_sgn(gerr[idx[count - 1]]) == mpfr_sgn(gerr[i]))
            {
                if (mpfr_cmpabs(gerr[i], gerr[idx[count - 1]]) > 0)
                    idx[count - 1] = i;
                continue;
            }
            idx[count++] = i;
        }
        // Too many: drop the smaller end.
        while (count > m)
        {
            if (mpfr_cmpabs(gerr[idx[0]], gerr[idx[count - 1]]) < 0)
                memmove(idx, idx + 1, (size_t)(count - 1) * sizeof(int));
            count--;
        }
        if (count < m)
        {
            fprintf(stderr, "remez: %d alternations, %d needed\n", count, m);
            exit(1);
        }
        // Refine each extremum between its grid neighbours.
        mpfr_t worst;
        mpfr_init2(worst, PREC);
        mpfr_set_ui(worst, 0, MPFR_RNDN);
        for (int k = 0; k < m; k++)
        {
            const int i = idx[k];
            if (i == 0 || i == GRID)
                mpfr_set(x[k], grid[i], MPFR_RNDN);
            else
                refine(kind, x[k], grid[i - 1], grid[i + 1], c, n);
            error_at(kind, t, x[k], c, n);
            if (mpfr_cmpabs(t, worst) > 0)
                mpfr_abs(worst, t, MPFR_RNDN);
        }
        // Converged when the largest error is the levelled one.
        mpfr_abs(t, e, MPFR_RNDN);
        mpfr_div(t, worst, t, MPFR_RNDN);
        mpfr_sub_ui(t, t, 1, MPFR_RNDN);
        result = mpfr_get_d(worst, MPFR_RNDN);
        const double rel = mpfr_get_d(t, MPFR_RNDN);
        mpfr_clear(worst);
        if (rel < 1e-30 && iteration > 3)
            break;
    }
    solve(kind, x, n, c, e);
    for (int i = 0; i < m; i++)
        mpfr_clear(x[i]);
    for (int i = 0; i <= GRID; i++)
    {
        mpfr_clear(grid[i]);
        mpfr_clear(gerr[i]);
    }
    mpfr_clears(e, t, mid, half, (mpfr_ptr)0);
    return result;
}

// The largest |p(v) - g(v)| over a grid of 20,000 points, for the
// coefficients given as doubles.
static double grid_error(fit_kind kind, int n, const mpfr_t lo, const mpfr_t hi, const double *coefficients)
{
    mpfr_t c[MAX_TERMS], v, t, e;
    for (int i = 0; i <= n; i++)
    {
        mpfr_init2(c[i], PREC);
        mpfr_set_d(c[i], coefficients[i], MPFR_RNDN);
    }
    mpfr_inits2(PREC, v, t, e, (mpfr_ptr)0);
    double worst = 0.0;
    for (int i = 0; i <= 20000; i++)
    {
        mpfr_sub(t, hi, lo, MPFR_RNDN);
        mpfr_mul_ui(t, t, (unsigned long)i, MPFR_RNDN);
        mpfr_div_ui(t, t, 20000, MPFR_RNDN);
        mpfr_add(v, lo, t, MPFR_RNDN);
        error_at(kind, e, v, c, n);
        const double d = fabs(mpfr_get_d(e, MPFR_RNDN));
        if (d > worst)
            worst = d;
    }
    for (int i = 0; i <= n; i++)
        mpfr_clear(c[i]);
    mpfr_clears(v, t, e, (mpfr_ptr)0);
    return worst;
}

static void exact_bound(fit_kind kind, mpfr_t hi, mpfr_t lo)
{
    mpfr_t t;
    mpfr_init2(t, PREC);
    switch (kind)
    {
    case FIT_E2: mpfr_set_str(hi, "0.5", 10, MPFR_RNDN); break;
    case FIT_EE: mpfr_const_log2(hi, MPFR_RNDN); mpfr_div_2ui(hi, hi, 1, MPFR_RNDN); break;
    case FIT_LG:
        mpfr_sqrt_ui(t, 2, MPFR_RNDN);
        mpfr_sub_ui(hi, t, 1, MPFR_RNDN);
        mpfr_add_ui(t, t, 1, MPFR_RNDN);
        mpfr_div(hi, hi, t, MPFR_RNDN);
        mpfr_sqr(hi, hi, MPFR_RNDN);
        break;
    case FIT_AT: mpfr_set_d(hi, 0.4375 * 0.4375, MPFR_RNDN); break;
    case FIT_TN: mpfr_const_pi(hi, MPFR_RNDN); mpfr_div_2ui(hi, hi, 2, MPFR_RNDN); mpfr_sqr(hi, hi, MPFR_RNDN); break;
    }
    if (kind == FIT_E2 || kind == FIT_EE)
        mpfr_neg(lo, hi, MPFR_RNDN);
    else
        mpfr_set_ui(lo, 0, MPFR_RNDN);
    mpfr_clear(t);
}

int main(int argc, char **argv)
{
    const bool exact = argc > 1 && strcmp(argv[1], "--exact") == 0;
    int differ_total = 0;
    for (size_t f = 0; f < sizeof(fits) / sizeof(fits[0]); f++)
    {
        const fit *ft = &fits[f];
        mpfr_t lo, hi, c[MAX_TERMS];
        mpfr_inits2(PREC, lo, hi, (mpfr_ptr)0);
        for (int i = 0; i <= ft->degree; i++)
            mpfr_init2(c[i], PREC);
        if (exact)
            exact_bound(ft->kind, hi, lo);
        else
        {
            mpfr_set_str(lo, ft->lo, 10, MPFR_RNDN);
            mpfr_set_str(hi, ft->hi, 10, MPFR_RNDN);
        }
        const double err = remez(ft->kind, ft->degree, lo, hi, c);
        int differ = 0;
        printf("%s: degree %d on [%.10g, %.10g], minimax error 2^%.2f\n", ft->name, ft->degree, mpfr_get_d(lo, MPFR_RNDN),
               mpfr_get_d(hi, MPFR_RNDN), err > 0 ? log2(err) : 0.0);
        for (int i = 0; i <= ft->degree; i++)
        {
            const double d = mpfr_get_d(c[i], MPFR_RNDN);
            const bool same = d == ft->definition[i];
            differ += !same;
            printf("  c[%2d] = %-24a %s\n", i, d, same ? "the Definition's" : "differs");
            if (!same)
                printf("          Definition %a\n", ft->definition[i]);
        }
        double fitted[MAX_TERMS];
        for (int i = 0; i <= ft->degree; i++)
            fitted[i] = mpfr_get_d(c[i], MPFR_RNDN);
        printf("  as doubles, on a grid of 20,000: this fit's error 2^%.3f, the Definition's 2^%.3f\n",
               log2(grid_error(ft->kind, ft->degree, lo, hi, fitted)), log2(grid_error(ft->kind, ft->degree, lo, hi, ft->definition)));
        differ_total += differ;
        for (int i = 0; i <= ft->degree; i++)
            mpfr_clear(c[i]);
        mpfr_clears(lo, hi, (mpfr_ptr)0);
    }
    printf("%d coefficients differ from the Definition's\n", differ_total);
    return 0;
}
