#pragma once
#include "Image.h"

namespace jug::image_detail
{

constexpr uint32_t kNumRgbaChannels = 4;
constexpr uint32_t kAlphaChannel    = 3;

// DDS header flags
constexpr uint32_t kDdsdCaps        = 0x1;
constexpr uint32_t kDdsdHeight      = 0x2;
constexpr uint32_t kDdsdWidth       = 0x4;
constexpr uint32_t kDdsdPitch       = 0x8;
constexpr uint32_t kDdsdPixelFormat = 0x1000;
constexpr uint32_t kDdsdMipMapCount = 0x20000;
constexpr uint32_t kDdsdLinearSize  = 0x80000;
constexpr uint32_t kDdsdDepth       = 0x800000;

// DDS pixel format flags
constexpr uint32_t kDdpfFourCC = 0x4;

// DDS caps
constexpr uint32_t kDdsCapsComplex = 0x8;
constexpr uint32_t kDdsCapsTexture = 0x1000;
constexpr uint32_t kDdsCapsMipMap  = 0x400000;

// DDS caps2
constexpr uint32_t kDdsCaps2Cubemap  = 0x200;
constexpr uint32_t kDdsCaps2AllFaces = 0xFC00;
constexpr uint32_t kDdsCaps2Volume   = 0x200000;

// DDS dx10
constexpr uint32_t kDdsDimensionTexture2D     = 3;
constexpr uint32_t kDdsDimensionTexture3D     = 4;
constexpr uint32_t kDdsMiscTextureCube        = 0x4;
constexpr uint32_t kDdsAlphaModeMask          = 0x7;
constexpr uint32_t kDdsAlphaModePremultiplied = 2;

// DDS header
constexpr uint32_t kDdsMagic      = JUG_FOURCC('D', 'D', 'S', ' ');
constexpr uint32_t kDdsFourCcDx10 = JUG_FOURCC('D', 'X', '1', '0');

struct DDS_PIXEL_FORMAT
{
    [[nodiscard]] Optional<eTextureFormat> ToTextureFormat() const;

    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};
static_assert(sizeof(DDS_PIXEL_FORMAT) == 32);

struct DDS_HEADER
{
    uint32_t            size;
    uint32_t            flags;
    uint32_t            height;
    uint32_t            width;
    uint32_t            pitchOrLinearSize;
    uint32_t            depth;
    uint32_t            mipMapCount;
    ARRAY<uint32_t, 11> reserved1;
    DDS_PIXEL_FORMAT    ddspf;
    uint32_t            caps;
    uint32_t            caps2;
    uint32_t            caps3;
    uint32_t            caps4;
    uint32_t            reserved2;
};
static_assert(sizeof(DDS_HEADER) == 124);

struct DDS_HEADER_DX10
{
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
static_assert(sizeof(DDS_HEADER_DX10) == 20);


[[nodiscard]] Optional<eTextureFormat> ToTextureFormatFromDXGI(
    uint32_t _dxgiFormat);

[[nodiscard]] Optional<uint32_t> ToDxgiFormat(
    eTextureFormat _format);

[[nodiscard]] Optional<eTextureFormat> ToTextureFormatFromVK(
    uint32_t _vkFormat);

[[nodiscard]] Optional<uint32_t> ToVkFormat(
    eTextureFormat _format);

[[nodiscard]] Optional<eTextureFormat> ToTextureFormatFromGL(
    uint32_t _glInternalFormat);

[[nodiscard]] Optional<uint32_t> ToGlFormat(
    eTextureFormat _format);

// 로그용 텍스처 포맷 이름. 값이 없으면 "none".
[[nodiscard]] StringView ToLogName(
    Optional<eTextureFormat> _format);

// texture format 을 PixelFormat 으로 변환. 압축/packed/BGRA 포맷은 unknown. bSRGB 는 항상 포맷의 sRGB 여부.
[[nodiscard]] PixelFormat ToPixelFormatOrUnknown(
    eTextureFormat _format);

// 픽셀 단위 채널 축소/확장. 확장 시 r.g.b는 0, a는 1.
void ConvertChannels(
    MemoryView        _src,
    MutableMemoryView _dst,
    size_t            _numPixels,
    uint32_t          _numSrcChannels,
    uint32_t          _numDstChannels,
    ChannelFormat     _type);

// _bcFormat으로 입축시 원하는 포맷
[[nodiscard]] Optional<eTextureFormat> ToCompressInputFormat(
    eTextureFormat _bcFormat);

// 2D slice 하나 압축. _src 는 ToCompressInputFormat 포맷.
void CompressSlice(
    eTextureFormat    _srcBcFormat,
    MemoryView        _src,
    MutableMemoryView _dst,
    uint32_t          _width,
    uint32_t          _height);

// 압축 해제시 텍스처 포맷
[[nodiscard]] Optional<eTextureFormat> ToDecompressedFormat(
    eTextureFormat _bcFormat);

// 2D slice 하나 압축 해제. _dst 는 ToDecompressedFormat 포맷.
void DecompressSlice(
    eTextureFormat    _srcBcFormat,
    MemoryView        _src,
    MutableMemoryView _dst,
    uint32_t          _width,
    uint32_t          _height);

[[nodiscard]] bool IsRawFormat(
    const PixelFormat& _format);

[[nodiscard]] PixelFormat GetPixelFormat(
    const ImageDesc& _desc);

[[nodiscard]] uint32_t CalcRowPitch(
    const Image& _image,
    uint32_t     _width);

[[nodiscard]] uint32_t CalcSlicePitch(
    const Image& _image,
    uint32_t     _width,
    uint32_t     _height);

[[nodiscard]] size_t CalcMipSize(
    const Image& _image,
    uint32_t     _mip);

[[nodiscard]] size_t CalcLayerSize(
    const Image& _image);

[[nodiscard]] size_t CalcTotalSize(
    const Image& _image);

[[nodiscard]] size_t CalcOffset(
    const Image& _image,
    uint32_t     _layer,
    uint32_t     _mip);

}   // namespace jug::image_detail
