#pragma once

namespace jug
{

enum class eTextureFormat
{
    Unknown = 0,

    R8_UNorm,
    R8_SNorm,
    R8_UInt,
    R8_Int,

    R16_UNorm,
    R16_SNorm,
    R16_UInt,
    R16_Int,
    R16_Float,

    R32_UInt,
    R32_Int,
    R32_Float,

    RG8_UNorm,
    RG8_SNorm,
    RG8_UInt,
    RG8_Int,

    RG16_UNorm,
    RG16_SNorm,
    RG16_UInt,
    RG16_Int,
    RG16_Float,

    RG32_UInt,
    RG32_Int,
    RG32_Float,

    RGBA8_UNorm,
    RGBA8_SNorm,
    RGBA8_UInt,
    RGBA8_Int,
    RGBA8_UNorm_SRGB,

    BGRA8_UNorm,
    BGRA8_UNorm_SRGB,

    RGBA16_UNorm,
    RGBA16_SNorm,
    RGBA16_UInt,
    RGBA16_Int,
    RGBA16_Float,

    RGBA32_UInt,
    RGBA32_Int,
    RGBA32_Float,

    B5G6R5_UNorm,
    BGRA4_UNorm,
    BGR5A1_UNorm,
    RGB10A2_UNorm,
    RG11B10_Float,

    D16_UNorm,
    D24_UNorm_S8_UInt,
    D32_Float,
    D32_Float_S8_UInt,
};

struct CalcTextureSizeResult
{
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t depth  = 0;
};

[[nodiscard]] bool           IsDepthFormat(eTextureFormat _format);
[[nodiscard]] bool           IsSRGB(eTextureFormat _format);
[[nodiscard]] eTextureFormat ToNonSRGB(eTextureFormat _format);
[[nodiscard]] eTextureFormat ToSrgbOrUnknown(eTextureFormat _format);
[[nodiscard]] uint32_t       GetBitPerPixel(eTextureFormat _format);

[[nodiscard]] uint32_t              CalcNumMips(uint32_t _width, uint32_t _height, uint32_t _depth);
[[nodiscard]] CalcTextureSizeResult CalcTextureSize(uint32_t _width, uint32_t _height, uint32_t _depth, uint32_t _mip);
[[nodiscard]] uint32_t              CalcTextureIndex(uint32_t _mip, uint32_t _layer, uint32_t _numMips);

}   // namespace jug