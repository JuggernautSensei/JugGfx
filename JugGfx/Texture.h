#pragma once

namespace jug
{

enum class eTextureFormat
{
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

    BC1_UNorm,
    BC1_UNorm_SRGB,
    BC2_UNorm,
    BC2_UNorm_SRGB,
    BC3_UNorm,
    BC3_UNorm_SRGB,
    BC4_UNorm,
    BC4_SNorm,
    BC5_UNorm,
    BC5_SNorm,
    BC6H_UFloat,
    BC6H_SFloat,
    BC7_UNorm,
    BC7_UNorm_SRGB,
};

[[nodiscard]] bool                     IsDepthFormat(eTextureFormat _format);
[[nodiscard]] bool                     IsCompressedFormat(eTextureFormat _format);
[[nodiscard]] bool                     IsSrgbFormat(eTextureFormat _format);
[[nodiscard]] eTextureFormat           ToLinearFormat(eTextureFormat _format);
[[nodiscard]] Optional<eTextureFormat> ToSrgbFormat(eTextureFormat _format);
[[nodiscard]] uint32_t                 GetBitPerPixel(eTextureFormat _format);
[[nodiscard]] uint32_t                 GetNumChannels(eTextureFormat _format);   // 포맷이 표현하는 채널 수. depth/stencil 포함.

[[nodiscard]] uint32_t CalcRowPitch(eTextureFormat _format, uint32_t _width);   // compressed textureFormat 이면, 블록 한 줄의 pitch
[[nodiscard]] uint32_t CalcNumRows(eTextureFormat _format, uint32_t _height);   // compressed textureFormat 이면, 블록이 총 몇 줄있는지.
[[nodiscard]] uint32_t CalcSlicePitch(eTextureFormat _format, uint32_t _width, uint32_t _height);

[[nodiscard]] uint32_t CalcNumMips(uint32_t _width, uint32_t _height, uint32_t _depth);
[[nodiscard]] VEC3U    CalcTextureSize(uint32_t _width, uint32_t _height, uint32_t _depth, uint32_t _mip);
[[nodiscard]] uint32_t CalcTextureIndex(uint32_t _mip, uint32_t _layer, uint32_t _numMips);

}   // namespace jug