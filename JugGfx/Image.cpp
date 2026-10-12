#include "pch.h"
#include "Image.h"

namespace jug
{

namespace
{
    constexpr ARRAY<eTextureFormat, 3> kUNorm8Formats  = { eTextureFormat::R8_UNorm, eTextureFormat::RG8_UNorm, eTextureFormat::RGBA8_UNorm };
    constexpr ARRAY<eTextureFormat, 3> kUNorm16Formats = { eTextureFormat::R16_UNorm, eTextureFormat::RG16_UNorm, eTextureFormat::RGBA16_UNorm };
    constexpr ARRAY<eTextureFormat, 3> kHalfFormats    = { eTextureFormat::R16_Float, eTextureFormat::RG16_Float, eTextureFormat::RGBA16_Float };
    constexpr ARRAY<eTextureFormat, 3> kFloatFormats   = { eTextureFormat::R32_Float, eTextureFormat::RG32_Float, eTextureFormat::RGBA32_Float };

    void AssertDesc_(
        const ImageDesc& _desc)
    {
        JUG_ASSERT(_desc.width > 0 && _desc.height > 0 && _desc.depth > 0, "Image size must be greater than zero.\n");
        JUG_ASSERT(_desc.numLayers > 0 && _desc.numMips > 0, "Image layers and mips must be greater than zero.\n");
        JUG_ASSERT(_desc.numMips <= CalcNumMips(_desc.width, _desc.height, _desc.depth), "Image has too many mips.\n");
        JUG_ASSERT(_desc.depth == 1 || _desc.numLayers == 1, "Volume image can not have layers.\n");
        JUG_ASSERT(!_desc.bCubemap || _desc.numLayers % 6 == 0, "Cubemap layers must be a multiple of 6.\n");

        if (_desc.channelFormat == eChannelFormat::Compressed)
        {
            JUG_ASSERT(_desc.gpuFormat.HasValue() && IsCompressedFormat(_desc.gpuFormat.GetValue()), "Compressed image must have a BC gpuFormat.\n");
            JUG_ASSERT(_desc.numChannels == 0, "Compressed image must have zero channels.\n");
            JUG_ASSERT(_desc.bSRGB == IsSrgbFormat(_desc.gpuFormat.GetValue()), "Compressed image bSRGB must match gpuFormat.\n");
            return;
        }

        JUG_ASSERT(_desc.numChannels > 0, "Image must have channels.\n");
        JUG_ASSERT(!_desc.bSRGB || (_desc.channelFormat == eChannelFormat::UNorm8 && _desc.numChannels >= 3), "bSRGB is only valid for 8 bit images with 3 or 4 channels.\n");

        if (_desc.gpuFormat.HasValue())
        {
            const eTextureFormat gpuFormat = _desc.gpuFormat.GetValue();
            const bool           bBgra     = _desc.channelFormat == eChannelFormat::UNorm8 && _desc.numChannels == 4 && gpuFormat == (_desc.bSRGB ? eTextureFormat::BGRA8_UNorm_SRGB : eTextureFormat::BGRA8_UNorm);
            JUG_ASSERT(DeriveGpuFormat(_desc.channelFormat, _desc.numChannels, _desc.bSRGB) == gpuFormat || bBgra, "Image gpuFormat is inconsistent with its channels.\n");
        }
    }

    [[nodiscard]] size_t CalcMipSize_(
        const ImageDesc& _desc,
        const uint32_t   _mip)
    {
        const VEC3U size = CalcTextureSize(_desc.width, _desc.height, _desc.depth, _mip);
        return CalcImageSlicePitch(_desc, size.x, size.y) * size.z;
    }
}   // namespace

Optional<eTextureFormat> DeriveGpuFormat(
    const eChannelFormat _channelFormat,
    const uint32_t       _numChannels,
    const bool           _bSRGB)
{
    JUG_ASSERT(_channelFormat != eChannelFormat::Compressed, "Compressed image has no derived gpuFormat.\n");

    uint32_t index = 0;
    switch (_numChannels)
    {
        case 1: index = 0; break;
        case 2: index = 1; break;
        case 4: index = 2; break;
        default: return kNullopt;
    }

    switch (_channelFormat)
    {
        case eChannelFormat::UNorm8:
            if (_bSRGB && index == 2)
            {
                return eTextureFormat::RGBA8_UNorm_SRGB;
            }
            return kUNorm8Formats[index];

        case eChannelFormat::UNorm16: return kUNorm16Formats[index];
        case eChannelFormat::Half: return kHalfFormats[index];
        case eChannelFormat::Float: return kFloatFormats[index];

        default:
            JUG_ASSERT(false, "Unknown channel format.\n");
            return kNullopt;
    }
}

uint32_t GetChannelSize(
    const eChannelFormat _channelFormat)
{
    switch (_channelFormat)
    {
        case eChannelFormat::UNorm8: return 1;
        case eChannelFormat::UNorm16:
        case eChannelFormat::Half: return 2;
        case eChannelFormat::Float: return 4;

        default:
            JUG_ASSERT(false, "Compressed image has no channel size.\n");
            return 0;
    }
}

size_t CalcImageRowPitch(
    const ImageDesc& _desc,
    const uint32_t   _width)
{
    if (_desc.channelFormat == eChannelFormat::Compressed)
    {
        return CalcRowPitch(_desc.gpuFormat.GetValue(), _width);
    }
    return static_cast<size_t>(_width) * _desc.numChannels * GetChannelSize(_desc.channelFormat);
}

size_t CalcImageSlicePitch(
    const ImageDesc& _desc,
    const uint32_t   _width,
    const uint32_t   _height)
{
    if (_desc.channelFormat == eChannelFormat::Compressed)
    {
        return CalcSlicePitch(_desc.gpuFormat.GetValue(), _width, _height);
    }
    return CalcImageRowPitch(_desc, _width) * _height;
}

size_t CalcImageLayerSize(
    const ImageDesc& _desc)
{
    AssertDesc_(_desc);

    size_t size = 0;
    for (uint32_t mip = 0; mip < _desc.numMips; ++mip)
    {
        size += CalcMipSize_(_desc, mip);
    }
    return size;
}

size_t CalcImageSize(
    const ImageDesc& _desc)
{
    return CalcImageLayerSize(_desc) * _desc.numLayers;
}

SubImage Image::GetSubImage(
    const uint32_t _layer,
    const uint32_t _mip) const
{
    JUG_ASSERT(_layer < desc.numLayers, "Layer index out of bounds.\n");
    JUG_ASSERT(_mip < desc.numMips, "Mip index out of bounds.\n");

    size_t offset = CalcImageLayerSize(desc) * _layer;
    for (uint32_t mip = 0; mip < _mip; ++mip)
    {
        offset += CalcMipSize_(desc, mip);
    }

    const VEC3U  size       = CalcTextureSize(desc.width, desc.height, desc.depth, _mip);
    const size_t rowPitch   = CalcImageRowPitch(desc, size.x);
    const size_t slicePitch = CalcImageSlicePitch(desc, size.x, size.y);
    JUG_ASSERT(slicePitch <= Max<uint32_t>(), "Image slice is too large.\n");
    JUG_ASSERT(offset + slicePitch * size.z <= pixels.GetSize(), "Image pixels are smaller than its desc.\n");

    SubImage sub   = {};
    sub.width      = size.x;
    sub.height     = size.y;
    sub.depth      = size.z;
    sub.rowPitch   = static_cast<uint32_t>(rowPitch);
    sub.slicePitch = static_cast<uint32_t>(slicePitch);
    sub.pixels     = { pixels.GetPtr() + offset, slicePitch * size.z };
    return sub;
}

}   // namespace jug
