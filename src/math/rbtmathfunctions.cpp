#include "math/rbtmathfunctions.h"
#include "rbtmathfunctions_internal.hpp"
#include <cstdarg>
#include <stdio.h>

// RbtMath's elementary functions (rbtmathfunctions.h): each batch call
// checks its arrays, then runs the Definition's evaluation over them, one
// element at a time in order, its input read into a local before its output
// is written, so in place is safe and the bits don't depend on where a
// caller splits an array. No allocation, no state.
//
// Built without floating-point contraction (CMakeLists.txt,
// rabbittus_no_contract); rbtmathfunctions_internal.hpp asserts
// FLT_EVAL_METHOD 0 and refuses fast math.

namespace
{

// ---- Errors -----------------------------------------------------------------

RBT_RESULT fail(RBTMATH_ERROR *error, RBT_RESULT result, const char *format, ...)
{
    if (error)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return result;
}

void clear(RBTMATH_ERROR *error)
{
    if (error)
        error->message[0] = '\0';
}

// True if out's count elements overlap in's without being them: address
// ranges compared as integers.
bool overlaps_apart(const double *out, const double *in, uint32_t count)
{
    const uintptr_t o    = (uintptr_t)out;
    const uintptr_t i    = (uintptr_t)in;
    const uintptr_t size = (uintptr_t)count * sizeof(double);
    if (o == i)
        return false;
    return o < i + size && i < o + size;
}

// The checks every one-input call makes, in the header's order.
RBT_RESULT check_one(const double *x, uint32_t count, const double *out_y, RBTMATH_ERROR *error)
{
    if (!x)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "x is NULL with a count of %u", count);
    if (!out_y)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_y is NULL with a count of %u", count);
    if (overlaps_apart(out_y, x, count))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_y overlaps x without being it");
    return RBT_RESULT_OK;
}

template <double (*F)(double)>
RBT_RESULT compute_one(const double *x, uint32_t count, double *out_y, RBTMATH_ERROR *error)
{
    clear(error);
    if (count == 0)
        return RBT_RESULT_OK;
    const RBT_RESULT checked = check_one(x, count, out_y, error);
    if (checked != RBT_RESULT_OK)
        return checked;
    for (uint32_t i = 0; i < count; i++)
    {
        const double v = x[i];
        out_y[i]       = F(v);
    }
    return RBT_RESULT_OK;
}

} // namespace

extern "C" RBT_RESULT rbtmath_compute_tan(const double *x, uint32_t count, double *out_y, RBTMATH_ERROR *error)
{
    return compute_one<rbtfn::tan>(x, count, out_y, error);
}

extern "C" RBT_RESULT rbtmath_compute_atan(const double *x, uint32_t count, double *out_y, RBTMATH_ERROR *error)
{
    return compute_one<rbtfn::atan>(x, count, out_y, error);
}

extern "C" RBT_RESULT rbtmath_compute_atan2(const double *y, const double *x, uint32_t count, double *out_angle, RBTMATH_ERROR *error)
{
    clear(error);
    if (count == 0)
        return RBT_RESULT_OK;
    if (!y)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "y is NULL with a count of %u", count);
    if (!x)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "x is NULL with a count of %u", count);
    if (!out_angle)
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_angle is NULL with a count of %u", count);
    if (overlaps_apart(out_angle, y, count))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_angle overlaps y without being it");
    if (overlaps_apart(out_angle, x, count))
        return fail(error, RBT_RESULT_INVALID_ARGUMENT, "out_angle overlaps x without being it");
    for (uint32_t i = 0; i < count; i++)
    {
        const double vy = y[i];
        const double vx = x[i];
        out_angle[i]    = rbtfn::atan2(vy, vx);
    }
    return RBT_RESULT_OK;
}

extern "C" RBT_RESULT rbtmath_compute_exp2(const double *x, uint32_t count, double *out_y, RBTMATH_ERROR *error)
{
    return compute_one<rbtfn::exp2>(x, count, out_y, error);
}

extern "C" RBT_RESULT rbtmath_compute_exp(const double *x, uint32_t count, double *out_y, RBTMATH_ERROR *error)
{
    return compute_one<rbtfn::exp>(x, count, out_y, error);
}

extern "C" RBT_RESULT rbtmath_compute_log2(const double *x, uint32_t count, double *out_y, RBTMATH_ERROR *error)
{
    return compute_one<rbtfn::log2>(x, count, out_y, error);
}
