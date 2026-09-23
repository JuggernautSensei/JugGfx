#pragma once
#include <concepts>

#include "SIMD.h"
#include "Vector3.h"

namespace jug
{

#ifdef JUG_SIMD_AVAILABLE
#    define JUG_VECTOR4_SIMD_PATH(_expr)       \
        JUG_BEGIN_MACRO_BLOCK                  \
        if constexpr (std::same_as<T, float>)  \
        {                                      \
            if (!std::is_constant_evaluated()) \
            {                                  \
                return (_expr);                \
            }                                  \
        }                                      \
        JUG_END_MACRO_BLOCK
#else
#    define JUG_VECTOR4_SIMD_PATH(_expr)
#endif

template<VectorScalarT T>
struct alignas(16) VECTOR<T, 4>
{
    static_assert(std::is_integral_v<T> || std::is_same_v<T, float>, "Scalar must be integral or float type.");

    JUG_MATH_API VECTOR() = default;

    JUG_MATH_API constexpr VECTOR(
        const T _x,
        const T _y,
        const T _z,
        const T _w)
        : e { _x, _y, _z, _w }
    {
    }

    JUG_MATH_API constexpr VECTOR(
        const VECTOR<T, 2> _xy,
        const T            _z,
        const T            _w)
        : e { _xy.e[0], _xy.e[1], _z, _w }
    {
    }

    JUG_MATH_API constexpr VECTOR(
        const VECTOR<T, 3> _xyz,
        const T            _w)
        : e { _xyz.e[0], _xyz.e[1], _xyz.e[2], _w }
    {
    }

    // Broadcast
    explicit JUG_MATH_API constexpr VECTOR(
        const T _value)
        : e { _value, _value, _value, _value }
    {
    }

#ifdef JUG_SIMD_AVAILABLE

    // =======================================================
    //  SIMD
    // =======================================================

    JUG_MATH_API /* implicit */ VECTOR(
        const simd::M128 _value)
        requires std::same_as<T, float>
    {
        simd::StoreAligned(e.data(), _value);
    }

    [[nodiscard]] simd::M128 ToSIMD() const
        requires std::same_as<T, float>
    {
        return simd::LoadAligned(e.data());
    }

#endif

    // =======================================================
    //  Operators
    // =======================================================

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator-() const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Negate(ToSIMD()));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] = -v.e[i];
        }
        return v;
    }

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator+(
        const VECTOR _other) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Add(ToSIMD(), _other.ToSIMD()));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] += _other.e[i];
        }
        return v;
    }

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator-(
        const VECTOR _other) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Sub(ToSIMD(), _other.ToSIMD()));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] -= _other.e[i];
        }
        return v;
    }

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator*(
        const T _scalar) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Scale(ToSIMD(), _scalar));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] *= _scalar;
        }
        return v;
    }

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator/(
        const T _scalar) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Div(ToSIMD(), simd::SetAll(_scalar)));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] /= _scalar;
        }
        return v;
    }

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator*(
        const VECTOR _other) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Mul(ToSIMD(), _other.ToSIMD()));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] *= _other.e[i];
        }
        return v;
    }

    [[nodiscard]] JUG_MATH_API constexpr VECTOR operator/(
        const VECTOR _other) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::Div(ToSIMD(), _other.ToSIMD()));

        VECTOR v = *this;
        for (size_t i = 0; i < kDim; ++i)
        {
            v.e[i] /= _other.e[i];
        }
        return v;
    }

    // =======================================================
    //  Assignment
    // =======================================================

    JUG_MATH_API constexpr VECTOR& operator+=(
        const VECTOR _other)
    {
        *this = *this + _other;
        return *this;
    }

    JUG_MATH_API constexpr VECTOR& operator-=(
        const VECTOR _other)
    {
        *this = *this - _other;
        return *this;
    }

    JUG_MATH_API constexpr VECTOR& operator*=(
        const T _scalar)
    {
        *this = *this * _scalar;
        return *this;
    }

    JUG_MATH_API constexpr VECTOR& operator/=(
        const T _scalar)
    {
        *this = *this / _scalar;
        return *this;
    }

    JUG_MATH_API constexpr VECTOR& operator*=(
        const VECTOR _other)
    {
        *this = *this * _other;
        return *this;
    }

    JUG_MATH_API constexpr VECTOR& operator/=(
        const VECTOR _other)
    {
        *this = *this / _other;
        return *this;
    }

    [[nodiscard]] JUG_MATH_API constexpr bool operator==(
        const VECTOR _other) const
    {
        JUG_VECTOR4_SIMD_PATH(simd::AllTrue(simd::CmpEq(ToSIMD(), _other.ToSIMD())));

        for (size_t i = 0; i < kDim; ++i)
        {
            if (e[i] != _other.e[i])   // NOLINT
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] JUG_MATH_API constexpr bool operator!=(
        const VECTOR _other) const
    {
        return !(*this == _other);
    }

    // =======================================================
    //  Access
    // =======================================================

    [[nodiscard]] JUG_MATH_API constexpr T& operator[](
        const size_t _index)
    {
        return e[_index];
    }

    [[nodiscard]] JUG_MATH_API constexpr const T& operator[](
        const size_t _index) const
    {
        return e[_index];
    }

    [[nodiscard]] JUG_MATH_API constexpr T* GetPtr()
    {
        return e.data();
    }

    [[nodiscard]] JUG_MATH_API constexpr const T* GetPtr() const
    {
        return e.data();
    }

    // =======================================================
    //  Fields
    // =======================================================

    const static VECTOR kZero;
    const static VECTOR kOne;
    const static VECTOR kRight;
    const static VECTOR kUp;
    const static VECTOR kForward;
    const static VECTOR kUnitX;
    const static VECTOR kUnitY;
    const static VECTOR kUnitZ;
    const static VECTOR kUnitW;
    const static VECTOR kMax;
    const static VECTOR kMin;

    constexpr static size_t kDim = 4;

    JUG_DISABLE_ANON_WARNING_BEGIN
    union
    {
        struct
        {
            T x;
            T y;
            T z;
            T w;
        };
        ARRAY<T, 4> e;
    };
    JUG_DISABLE_ANON_WARNING_END
};

using VECTOR4  = VECTOR<float, 4>;
using VECTOR4S = VECTOR<int, 4>;
using VECTOR4U = VECTOR<uint32_t, 4>;

static_assert(sizeof(VECTOR4) == 16, "VECTOR4 must be tightly packed");
static_assert(alignof(VECTOR4) == 16, "VECTOR4 must be 16-byte aligned for SIMD");
static_assert(PodT<VECTOR4>, "VECTOR4 must be POD type.");

// =======================================================
//  Constants
// =======================================================

template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kZero { T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kOne { T { 1 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kRight = { T { 1 }, T { 0 }, T { 0 }, T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kUp = { T { 0 }, T { 1 }, T { 0 }, T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kForward = { T { 0 }, T { 0 }, T { 1 }, T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kUnitX = { T { 1 }, T { 0 }, T { 0 }, T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kUnitY = { T { 0 }, T { 1 }, T { 0 }, T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kUnitZ = { T { 0 }, T { 0 }, T { 1 }, T { 0 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kUnitW = { T { 0 }, T { 0 }, T { 0 }, T { 1 } };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kMax { MathConstants<T>::kMax };
template<VectorScalarT T>
inline constexpr VECTOR<T, 4> VECTOR<T, 4>::kMin { MathConstants<T>::kMin };

template<>
struct MathConstants<VECTOR4>
{
    constexpr static VECTOR4 kZero    = VECTOR4::kZero;
    constexpr static VECTOR4 kOne     = VECTOR4::kOne;
    constexpr static VECTOR4 kRight   = VECTOR4::kRight;
    constexpr static VECTOR4 kUp      = VECTOR4::kUp;
    constexpr static VECTOR4 kForward = VECTOR4::kForward;
    constexpr static VECTOR4 kUnitX   = VECTOR4::kUnitX;
    constexpr static VECTOR4 kUnitY   = VECTOR4::kUnitY;
    constexpr static VECTOR4 kUnitZ   = VECTOR4::kUnitZ;
    constexpr static VECTOR4 kUnitW   = VECTOR4::kUnitW;
    constexpr static VECTOR4 kMax     = VECTOR4::kMax;
    constexpr static VECTOR4 kMin     = VECTOR4::kMin;
};

}   // namespace jug
