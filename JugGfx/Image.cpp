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
        return _desc.CalcSlicePitch(size.x, size.y) * size.z;
    }
}   // namespace

Optional<eTextureFormat> DeriveGpuFormat(
    const eChannelFormat _format,
    const uint32_t       _numChannels,
    const bool           _bSRGB)
{
    JUG_ASSERT(_format != eChannelFormat::Compressed, "Compressed image has no derived gpuFormat.\n");

    uint32_t index = 0;
    switch (_numChannels)
    {
        case 1: index = 0; break;
        case 2: index = 1; break;
        case 4: index = 2; break;
        default: return kNullopt;
    }

    switch (_format)
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
    const eChannelFormat _format)
{
    switch (_format)
    {
        case eChannelFormat::UNorm8:
            return 1;

        case eChannelFormat::UNorm16:
        case eChannelFormat::Half:
            return 2;

        case eChannelFormat::Float:
            return 4;

        default:
            return 0;
    }
}

size_t ImageDesc::CalcRowPitch(
    const uint32_t _width) const
{
    if (channelFormat == eChannelFormat::Compressed)
    {
        return jug::CalcRowPitch(gpuFormat.GetValue(), _width);
    }
    return static_cast<size_t>(_width) * numChannels * GetChannelSize(channelFormat);
}

size_t ImageDesc::CalcSlicePitch(
    const uint32_t _width,
    const uint32_t _height) const
{
    if (channelFormat == eChannelFormat::Compressed)
    {
        return jug::CalcSlicePitch(gpuFormat.GetValue(), _width, _height);
    }
    return CalcRowPitch(_width) * _height;
}

size_t ImageDesc::CalcLayerSize() const
{
    AssertDesc_(*this);

    size_t size = 0;
    for (uint32_t mip = 0; mip < numMips; ++mip)
    {
        size += CalcMipSize_(*this, mip);
    }
    return size;
}

size_t ImageDesc::CalcSize() const
{
    return CalcLayerSize() * numLayers;
}

Image::Image(
    const ImageDesc& _desc,
    PixelBuffer&&    _pixels)
    : m_pixels(std::move(_pixels))
    , m_desc(_desc)
{
    JUG_ASSERT(m_pixels.GetSize() == m_desc.CalcSize(), "Image pixels size does not match its desc.\n");
}

const ImageDesc& Image::GetDesc() const
{
    return m_desc;
}

MemoryView Image::GetPixels() const
{
    return { m_pixels.GetPtr(), m_pixels.GetSize() };
}

SubImage Image::GetSubImage(
    const uint32_t _layer,
    const uint32_t _mip) const
{
    JUG_ASSERT(_layer < m_desc.numLayers, "Layer index out of bounds.\n");
    JUG_ASSERT(_mip < m_desc.numMips, "Mip index out of bounds.\n");

    size_t offset = m_desc.CalcLayerSize() * _layer;
    for (uint32_t mip = 0; mip < _mip; ++mip)
    {
        offset += CalcMipSize_(m_desc, mip);
    }

    const VEC3U  size       = CalcTextureSize(m_desc.width, m_desc.height, m_desc.depth, _mip);
    const size_t rowPitch   = m_desc.CalcRowPitch(size.x);
    const size_t slicePitch = m_desc.CalcSlicePitch(size.x, size.y);
    JUG_ASSERT(slicePitch <= Max<uint32_t>(), "Image slice is too large.\n");

    SubImage sub   = {};
    sub.width      = size.x;
    sub.height     = size.y;
    sub.depth      = size.z;
    sub.rowPitch   = static_cast<uint32_t>(rowPitch);
    sub.slicePitch = static_cast<uint32_t>(slicePitch);
    sub.pixels     = { m_pixels.GetPtr() + offset, slicePitch * size.z };
    return sub;
}

}   // namespace jug
