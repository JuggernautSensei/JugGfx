#pragma once
#include "Matrix.h"

namespace jug
{

struct SPHERE
{
    JUG_MATH_API  SPHERE() = default;

    JUG_MATH_API constexpr SPHERE(
        const VECTOR3 _center,
        const float   _radius)
        : center(_center)
        , radius(_radius)
    {
    }

    const static SPHERE kZero;
    const static SPHERE kUnit;

    VECTOR3 center;
    float   radius;
};

static_assert(PodT<SPHERE>, "SPHERE must be POD type.");

// ========================================================
//  Constants
// ========================================================

inline constexpr SPHERE SPHERE::kZero = SPHERE { Zero<VECTOR3>(), 0.f };
inline constexpr SPHERE SPHERE::kUnit = SPHERE { Zero<VECTOR3>(), 1.f };

template<>
struct MathConstants<SPHERE>
{
    static constexpr SPHERE kZero = SPHERE::kZero;
    static constexpr SPHERE kOne  = SPHERE::kUnit;
};

// ========================================================
//  Method
// ========================================================

[[nodiscard]] JUG_MATH_API constexpr SPHERE Xform(
    const SPHERE& _sphere,
    const MATRIX& _mtx)
{
    const VECTOR3 p  = XformPoint(_sphere.center, _mtx);
    const float   sx = Length(XformVector(UnitX<VECTOR3>(), _mtx));
    const float   sy = Length(XformVector(UnitY<VECTOR3>(), _mtx));
    const float   sz = Length(XformVector(UnitZ<VECTOR3>(), _mtx));
    return SPHERE { p, _sphere.radius * Max(sx, sy, sz) };
}

}   // namespace jug
