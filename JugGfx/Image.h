#pragma once
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>

#include "Texture.h"

namespace jug
{

using ChannelFormat = OIIO::TypeDesc;

// RGBA 순 채널로 표현 가능한 픽셀 포맷.
// formatOrUnknown 이 UNKNOWN 이면 표현 불가.
struct PixelFormat
{
    ChannelFormat formatOrUnknown = ChannelFormat::UNKNOWN;
    uint32_t      numChannels     = 0;
    bool          bSRGB           = false;
};

struct ImageDesc
{
    uint32_t                 width               = 0;
    uint32_t                 height              = 0;
    uint32_t                 depth               = 1;
    uint32_t                 numLayers           = 1;
    uint32_t                 numMips             = 1;
    PixelFormat              format              = {};
    Optional<eTextureFormat> gpuFormat           = {};   // gpu와 호환되는 format. 혹은 압축 포맷 표현. 없으면 formatOrUnknown 으로만 표현.
    bool                     bCubemap            = false;
    bool                     bPremultipliedAlpha = false;
};

enum class eImageFileFormat
{
    PNG,
    JPEG,
    BMP,
    TGA,
    HDR,
    EXR,
    TIFF,
    GIF,
    PNM,
    SGI,
    RLA,
    IFF,
    DPX,
    DDS,
    KTX,
    KTX2,
};

struct SubImage
{
    uint32_t   width      = 0;
    uint32_t   height     = 0;
    uint32_t   depth      = 1;
    uint32_t   rowPitch   = 0;
    uint32_t   slicePitch = 0;
    MemoryView pixels     = {};   // 픽셀에 대한 소유권은 Image가 가진다.
};

struct Image
{
    [[nodiscard]] SubImage GetSubImage(uint32_t _layer, uint32_t _mip) const;

    // 전체 픽셀 데이터.
    // 하나의 이미지는 다수의 sub image 로 구성되며
    // 이는 물리적으로 mip index + layer index * mip levels 로 정렬되어 있다.
    ByteBuffer pixels = {};
    ImageDesc  desc   = {};
};

}   // namespace jug
