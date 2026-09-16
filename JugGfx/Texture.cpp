#include "pch.h"
#include "Texture.h"

#include <JugX/Math.h>

namespace jug
{

bool IsDepthFormat(
    const eTextureFormat _format)
{
    switch (_format)   // NOLINT
    {
        case eTextureFormat::D16_UNorm:
        case eTextureFormat::D24_UNorm_S8_UInt:
        case eTextureFormat::D32_Float:
        case eTextureFormat::D32_Float_S8_UInt:
            return true;

        default:
            return false;
    }
}

bool IsSRGB(
    const eTextureFormat _format)
{
    switch (_format)   // NOLINT
    {
        case eTextureFormat::RGBA8_UNorm_SRGB:
        case eTextureFormat::BGRA8_UNorm_SRGB:
            return true;
        default:
            return false;
    }
}

eTextureFormat ToSrgbOrUnknown(
    const eTextureFormat _format)
{
    switch (_format)   // NOLINT
    {
        case eTextureFormat::RGBA8_UNorm:
            return eTextureFormat::RGBA8_UNorm_SRGB;
        case eTextureFormat::BGRA8_UNorm:
            return eTextureFormat::BGRA8_UNorm_SRGB;
        default:
            return eTextureFormat::Unknown;
    }
}

int GetBitPerPixel(
    const eTextureFormat _format)
{
    switch (_format)
    {
        case eTextureFormat::Unknown:
            return 0;

        case eTextureFormat::R8_UNorm:
        case eTextureFormat::R8_SNorm:
        case eTextureFormat::R8_UInt:
        case eTextureFormat::R8_Int:
            return 8;

        case eTextureFormat::R16_UNorm:
        case eTextureFormat::R16_SNorm:
        case eTextureFormat::R16_UInt:
        case eTextureFormat::R16_Int:
        case eTextureFormat::R16_Float:
            return 16;

        case eTextureFormat::R32_UInt:
        case eTextureFormat::R32_Int:
        case eTextureFormat::R32_Float:
            return 32;

        case eTextureFormat::RG8_UNorm:
        case eTextureFormat::RG8_SNorm:
        case eTextureFormat::RG8_UInt:
        case eTextureFormat::RG8_Int:
            return 16;

        case eTextureFormat::RG16_UNorm:
        case eTextureFormat::RG16_SNorm:
        case eTextureFormat::RG16_UInt:
        case eTextureFormat::RG16_Int:
        case eTextureFormat::RG16_Float:
            return 32;

        case eTextureFormat::RG32_UInt:
        case eTextureFormat::RG32_Int:
        case eTextureFormat::RG32_Float:
            return 64;

        case eTextureFormat::RGBA8_UNorm:
        case eTextureFormat::RGBA8_SNorm:
        case eTextureFormat::RGBA8_UInt:
        case eTextureFormat::RGBA8_Int:
        case eTextureFormat::RGBA8_UNorm_SRGB:
        case eTextureFormat::BGRA8_UNorm:
        case eTextureFormat::BGRA8_UNorm_SRGB:
            return 32;

        case eTextureFormat::RGBA16_UNorm:
        case eTextureFormat::RGBA16_SNorm:
        case eTextureFormat::RGBA16_UInt:
        case eTextureFormat::RGBA16_Int:
        case eTextureFormat::RGBA16_Float:
            return 64;

        case eTextureFormat::RGBA32_UInt:
        case eTextureFormat::RGBA32_Int:
        case eTextureFormat::RGBA32_Float:
            return 128;

        case eTextureFormat::D16_UNorm:
            return 16;

        case eTextureFormat::D24_UNorm_S8_UInt:
            return 32;

        case eTextureFormat::D32_Float:
            return 32;

        case eTextureFormat::D32_Float_S8_UInt:
            return 64;

        case eTextureFormat::B5G6R5_UNorm:
        case eTextureFormat::BGRA4_UNorm:
        case eTextureFormat::BGR5A1_UNorm:
            return 16;

        case eTextureFormat::RGB10A2_UNorm:
        case eTextureFormat::RG11B10_Float:
            return 32;

        default:
            JUG_ASSERT(false, "Unknown texture format");
            return 0;
    }
}

int CalcNumMips(
    const int _width,
    const int _height,
    const int _depth)
{
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "Width, height and depth must be greater than zero");
    return 1 + static_cast<int>(Log2(static_cast<float>(Max(_width, _height, _depth))));
}

VECTOR3I CalcTextureSize(
    const int _width,
    const int _height,
    const int _depth,
    const int _mip)
{
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "Width, height and depth must be greater than zero");
    JUG_ASSERT(_mip >= 0, "Mip level must be greater than or equal to zero");

    VECTOR3I size = { _width, _height, _depth };
    for (int i = 0; i < _mip; ++i)
    {
        size.x = Max(1, size.x >> 1);
        size.y = Max(1, size.y >> 1);
        size.z = Max(1, size.z >> 1);
    }
    return size;
}

int CalcTextureIndex(
    const int _mip,
    const int _layer,
    const int _numMips)
{
    JUG_ASSERT(_mip >= 0 && _layer >= 0 && _numMips > 0, "Mip, layer and numMips must be greater than or equal to zero");
    return _layer * _numMips + _mip;
}

}   // namespace jug