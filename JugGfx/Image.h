#pragma once
#include "Texture.h"

namespace jug
{

// malloc/free 할당자. stb / tinyexr 가 malloc 한 결과를 PixelBuffer 가 그대로 채택하려고 씀
struct ImageAllocator
{
    using value_type      = std::byte;
    using is_always_equal = std::true_type;

    [[nodiscard]] std::byte* allocate(
        const size_t _size) const
    {
        std::byte* pMem = static_cast<std::byte*>(std::malloc(_size));
        JUG_ASSERT(pMem, "ImageAllocator: out of memory.\n");
        return pMem;
    }

    void deallocate(
        std::byte* _pMem,
        size_t) const
    {
        std::free(_pMem);
    }

    [[nodiscard]] bool operator==(
        const ImageAllocator&) const
    {
        return true;
    }
};

using PixelBuffer = Buffer<std::byte, ImageAllocator>;

enum class eChannelFormat
{
    Compressed,
    UNorm8,
    UNorm16,
    Half,
    Float,
};

// Compressed 는 BC 블록 압축. 레이아웃은 gpuFormat 이 정하고 numChannels 는 0.
// 채널 순서는 RGBA, gpuFormat 이 BGRA8 이면 BGRA.
// bSRGB 는 UNorm8 3/4 채널과 BC sRGB 에서만 true.
// gpuFormat 이 없으면 GPU 비호환 (3 채널 등).
struct ImageDesc
{
    uint32_t                 width         = 0;
    uint32_t                 height        = 0;
    uint32_t                 depth         = 1;   // depth > 1 이면 numLayers 는 1
    uint32_t                 numLayers     = 1;   // cubemap 이면 6 의 배수
    uint32_t                 numMips       = 1;
    eChannelFormat           channelFormat = eChannelFormat::UNorm8;
    uint32_t                 numChannels   = 0;
    bool                     bSRGB         = false;
    Optional<eTextureFormat> gpuFormat     = {};
    bool                     bCubemap      = false;
};

enum class eImageFileFormat
{
    PNG,
    JPEG,
    BMP,
    TGA,
    HDR,
    EXR,
    GIF,   // import 전용
    DDS,
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

struct Image
{
    [[nodiscard]] SubImage GetSubImage(uint32_t _layer, uint32_t _mip) const;

    // sub image 는 layer * numMips + mip 순으로 이어 붙어 있음
    PixelBuffer pixels = {};
    ImageDesc   desc   = {};
};

// 3 채널이나 4 초과 채널은 없음
[[nodiscard]] Optional<eTextureFormat> DeriveGpuFormat(eChannelFormat _channelFormat, uint32_t _numChannels, bool _bSRGB);

// 비압축 채널 하나의 바이트 수.
[[nodiscard]] uint32_t GetChannelSize(eChannelFormat _channelFormat);

// 한 행(압축이면 블록 한 줄) / 한 슬라이스의 바이트 수.
[[nodiscard]] size_t CalcImageRowPitch(const ImageDesc& _desc, uint32_t _width);
[[nodiscard]] size_t CalcImageSlicePitch(const ImageDesc& _desc, uint32_t _width, uint32_t _height);

// 한 레이어(모든 mip) / 전체 이미지의 바이트 수.
[[nodiscard]] size_t CalcImageLayerSize(const ImageDesc& _desc);
[[nodiscard]] size_t CalcImageSize(const ImageDesc& _desc);

}   // namespace jug
