#pragma once
#include "Texture.h"

namespace jug
{

// ===========================================
//  Image
// ===========================================

enum class eChannelFormat
{
    Compressed,
    UNorm8,
    UNorm16,
    Half,
    Float,
};

using PixelBuffer = Buffer<std::byte, MallocAllocator<std::byte>>;

struct ImageDesc
{
    // 한 행의 바이트/한 슬라이스의 바이트 수
    // 압축 포맷이면 블록 한 줄/한 블록 슬라이스의 바이트 수를 리턴함
    [[nodiscard]] size_t CalcRowPitch(uint32_t _width) const;
    [[nodiscard]] size_t CalcSlicePitch(uint32_t _width, uint32_t _height) const;

    // 한 레이어(모든 mip) / 전체 이미지의 바이트 수.
    [[nodiscard]] size_t CalcLayerSize() const;
    [[nodiscard]] size_t CalcSize() const;

    uint32_t                 width         = 0;
    uint32_t                 height        = 0;
    uint32_t                 depth         = 1;   // depth > 1 이면 numLayers 는 1
    uint32_t                 numLayers     = 1;   // cubemap 이면 6 의 배수
    uint32_t                 numMips       = 1;
    eChannelFormat           channelFormat = eChannelFormat::UNorm8;
    uint32_t                 numChannels   = 0;
    bool                     bSRGB         = false;
    Optional<eTextureFormat> gpuFormat     = kNullopt;
    bool                     bCubemap      = false;
};

struct SubImage
{
    uint32_t   width      = 0;
    uint32_t   height     = 0;
    uint32_t   depth      = 1;
    uint32_t   rowPitch   = 0;
    uint32_t   slicePitch = 0;
    MemoryView pixels     = {};   // 소유권은 Image
};

class Image
{
    JUG_CLASS(Image, NO_COPY, DEFAULT_MOVE)

public:
    Image() = default;
    Image(const ImageDesc& _desc, PixelBuffer&& _pixels);
    ~Image() = default;

    [[nodiscard]] const ImageDesc& GetDesc() const;
    [[nodiscard]] MemoryView       GetPixels() const;
    [[nodiscard]] SubImage         GetSubImage(uint32_t _layer, uint32_t _mip) const;

private:
    PixelBuffer m_pixels = {};   // layer * numMips + mip
    ImageDesc   m_desc   = {};
};

// ===========================================
//  Utils
// ===========================================

// 픽셀 포맷에서 GPU 포맷으로 파생하는 함수.
// 불가할 경우 nullopt
[[nodiscard]] Optional<eTextureFormat> DeriveGpuFormat(eChannelFormat _format, uint32_t _numChannels, bool _bSRGB);

// 채널 하나의 바이트 수.
// 압축 채널의 경우 채널이 아닌 블록 단위로 처리되므로, 해당 함수로 올바른 값을 구할 수 없음. 0을 리턴함
[[nodiscard]] uint32_t GetChannelSize(eChannelFormat _format);

}   // namespace jug
