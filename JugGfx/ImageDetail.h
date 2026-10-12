#pragma once
#include "Image.h"

namespace jug::image_detail
{

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
constexpr uint32_t kDdpfFourCc = 0x4;

// DDS caps
constexpr uint32_t kDdsCapsComplex = 0x8;
constexpr uint32_t kDdsCapsTexture = 0x1000;
constexpr uint32_t kDdsCapsMipMap  = 0x400000;

// DDS caps2
constexpr uint32_t kDdsCaps2Cubemap  = 0x200;
constexpr uint32_t kDdsCaps2AllFaces = 0xFC00;
constexpr uint32_t kDdsCaps2Volume   = 0x200000;

// DDS dx10
constexpr uint32_t kDdsDimensionTexture1D = 2;
constexpr uint32_t kDdsDimensionTexture2D = 3;
constexpr uint32_t kDdsDimensionTexture3D = 4;
constexpr uint32_t kDdsMiscTextureCube    = 0x4;

// DDS magic / FourCC
constexpr uint32_t kDdsMagic      = JUG_FOURCC('D', 'D', 'S', ' ');
constexpr uint32_t kDdsFourCcDx10 = JUG_FOURCC('D', 'X', '1', '0');

struct DDS_PIXEL_FORMAT
{
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

// R32G32B32_FLOAT. eTextureFormat 에 없는 유일한 지원 포맷
constexpr uint32_t kDxgiFormatR32G32B32Float = 6;

// DXGI 포맷 <-> Image 포맷 대응표의 한 행
struct ImageFormatEntry
{
    uint32_t                 dxgiFormat    = 0;
    Optional<eTextureFormat> gpuFormat     = {};   // R32G32B32_FLOAT 만 없음
    eChannelFormat           channelFormat = eChannelFormat::UNorm8;
    uint32_t                 numChannels   = 0;
    bool                     bSRGB         = false;
};

// 미지원이면 nullptr
[[nodiscard]] const ImageFormatEntry* FindFormatByDxgiOrNull(uint32_t _dxgiFormat);
[[nodiscard]] const ImageFormatEntry* FindFormatByGpuFormatOrNull(eTextureFormat _gpuFormat);

}   // namespace jug::image_detail
