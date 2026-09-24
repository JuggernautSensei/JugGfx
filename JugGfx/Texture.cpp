#include "pch.h"
#include "Texture.h"

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

eTextureFormat ToNonSRGB(
    const eTextureFormat _format)
{
    switch (_format)   // NOLINT
    {
        case eTextureFormat::RGBA8_UNorm_SRGB:
            return eTextureFormat::RGBA8_UNorm;
        case eTextureFormat::BGRA8_UNorm_SRGB:
            return eTextureFormat::BGRA8_UNorm;
        default:
            return _format;
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

uint32_t GetBitPerPixel(
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

uint32_t CalcNumMips(
    const uint32_t _width,
    const uint32_t _height,
    const uint32_t _depth)
{
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "Width, height and depth must be greater than zero");
    return std::bit_width(Max(_width, _height, _depth));
}

VECTOR3U CalcTextureSize(
    const uint32_t _width,
    const uint32_t _height,
    const uint32_t _depth,
    const uint32_t _mip)
{
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "Width, height and depth must be greater than zero");
    return { Max(1u, _width >> _mip), Max(1u, _height >> _mip), Max(1u, _depth >> _mip) };
}

uint32_t CalcTextureIndex(
    const uint32_t _mip,
    const uint32_t _layer,
    const uint32_t _numMips)
{
    return _layer * _numMips + _mip;
}

}   // namespace jug