#include "pch.h"
#include "ImageDetail.h"

namespace jug::image_detail
{

namespace
{
    constexpr eChannelFormat kBlock = eChannelFormat::Compressed;
    constexpr eChannelFormat kU8    = eChannelFormat::UNorm8;
    constexpr eChannelFormat kU16   = eChannelFormat::UNorm16;
    constexpr eChannelFormat kHalf  = eChannelFormat::Half;
    constexpr eChannelFormat kFloat = eChannelFormat::Float;

    // 행: DXGI, gpuFormat, 채널 형식, 채널 수(BC 는 0), sRGB
    constexpr ARRAY<ImageFormatEntry, 30> kFormatEntries = {
        {
         { 2, eTextureFormat::RGBA32_Float, kFloat, 4, false },
         { 6, kNullopt, kFloat, 3, false },
         { 10, eTextureFormat::RGBA16_Float, kHalf, 4, false },
         { 11, eTextureFormat::RGBA16_UNorm, kU16, 4, false },
         { 16, eTextureFormat::RG32_Float, kFloat, 2, false },
         { 28, eTextureFormat::RGBA8_UNorm, kU8, 4, false },
         { 29, eTextureFormat::RGBA8_UNorm_SRGB, kU8, 4, true },
         { 34, eTextureFormat::RG16_Float, kHalf, 2, false },
         { 35, eTextureFormat::RG16_UNorm, kU16, 2, false },
         { 41, eTextureFormat::R32_Float, kFloat, 1, false },
         { 49, eTextureFormat::RG8_UNorm, kU8, 2, false },
         { 54, eTextureFormat::R16_Float, kHalf, 1, false },
         { 56, eTextureFormat::R16_UNorm, kU16, 1, false },
         { 61, eTextureFormat::R8_UNorm, kU8, 1, false },
         { 71, eTextureFormat::BC1_UNorm, kBlock, 0, false },
         { 72, eTextureFormat::BC1_UNorm_SRGB, kBlock, 0, true },
         { 74, eTextureFormat::BC2_UNorm, kBlock, 0, false },
         { 75, eTextureFormat::BC2_UNorm_SRGB, kBlock, 0, true },
         { 77, eTextureFormat::BC3_UNorm, kBlock, 0, false },
         { 78, eTextureFormat::BC3_UNorm_SRGB, kBlock, 0, true },
         { 80, eTextureFormat::BC4_UNorm, kBlock, 0, false },
         { 81, eTextureFormat::BC4_SNorm, kBlock, 0, false },
         { 83, eTextureFormat::BC5_UNorm, kBlock, 0, false },
         { 84, eTextureFormat::BC5_SNorm, kBlock, 0, false },
         { 87, eTextureFormat::BGRA8_UNorm, kU8, 4, false },
         { 91, eTextureFormat::BGRA8_UNorm_SRGB, kU8, 4, true },
         { 95, eTextureFormat::BC6H_UFloat, kBlock, 0, false },
         { 96, eTextureFormat::BC6H_SFloat, kBlock, 0, false },
         { 98, eTextureFormat::BC7_UNorm, kBlock, 0, false },
         { 99, eTextureFormat::BC7_UNorm_SRGB, kBlock, 0, true },
         }
    };
}   // namespace

const ImageFormatEntry* FindFormatByDxgiOrNull(
    const uint32_t _dxgiFormat)
{
    for (const ImageFormatEntry& entry: kFormatEntries)
    {
        if (entry.dxgiFormat == _dxgiFormat)
        {
            return &entry;
        }
    }
    return nullptr;
}

const ImageFormatEntry* FindFormatByGpuFormatOrNull(
    const eTextureFormat _gpuFormat)
{
    for (const ImageFormatEntry& entry: kFormatEntries)
    {
        if (entry.gpuFormat == _gpuFormat)
        {
            return &entry;
        }
    }
    return nullptr;
}

}   // namespace jug::image_detail
