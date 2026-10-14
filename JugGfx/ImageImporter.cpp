#include "pch.h"
#include "ImageImporter.h"

#define BCDEC_IMPLEMENTATION
#include <bcdec/bcdec.h>

#include <tinyexr.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include "Error.h"
#include "ImageDetail.h"

namespace jug
{

namespace
{
    // 포맷 판별용 magic
    constexpr StringView kMagicDds  = "DDS ";
    constexpr StringView kMagicGif  = "GIF8";
    constexpr StringView kMagicBmp  = "BM";
    constexpr StringView kMagicPng  = StringView { "\x89PNG\r\n\x1A\n", 8 };
    constexpr StringView kMagicJpeg = StringView { "\xFF\xD8\xFF", 3 };
    constexpr StringView kMagicExr  = StringView { "\x76\x2F\x31\x01", 4 };
    constexpr StringView kMagicHdr  = "#?RADIANCE";
    constexpr StringView kMagicRgbe = "#?RGBE";

    constexpr uint32_t kBlockSize         = 4;                   // BC 블록 한 변
    constexpr uint32_t kMaxBlockPixelSize = sizeof(float) * 3;   // BC6H RGB float 기준

    // DDS 한 변 상한 (메모리 안전용, GPU 와 무관)
    constexpr uint32_t kMaxDdsDimension = 1 << 16;

    // GPU 한계 (GpuCompatible)
    constexpr uint32_t kMaxGpuTextureSize   = 16384;
    constexpr uint32_t kMaxGpuTextureDepth  = 2048;
    constexpr uint32_t kMaxGpuTextureLayers = 2048;

    // 알파 1.0 의 비트 패턴. eChannelFormat 순서 (Compressed 는 안 씀). little endian 으로 채널 크기만큼 기록
    constexpr ARRAY<uint32_t, 5> kAlphaOnes = {
        { 0, 0xFF, 0xFFFF, 0x3C00, 0x3F800000 }
    };

    [[nodiscard]] bool StartsWith_(
        const MemoryView _mem,
        const StringView _magic)
    {
        return _mem.GetSize() >= _magic.size() && std::memcmp(_mem.GetPtr(), _magic.data(), _magic.size()) == 0;
    }

    // ===========================================
    //  stb (PNG / JPEG / BMP / TGA / HDR / GIF)
    // ===========================================

    [[nodiscard]] Result<Image> ImportStb_(
        const MemoryView                _mem,
        const Flags<eImageImportOption> _flags,
        const bool                      _bKnownFormat)
    {
        const stbi_uc* pData = reinterpret_cast<const stbi_uc*>(_mem.GetPtr());
        const int      size  = static_cast<int>(_mem.GetSize());

        int width;
        int height;
        int infoChannels;
        if (!stbi_info_from_memory(pData, size, &width, &height, &infoChannels))
        {
            if (!_bKnownFormat)
            {
                JUG_CORE_LOG_ERROR("ImportImage: unknown image format");
                return eGraphicsError::ImageUnsupportedFormat;
            }
            JUG_CORE_LOG_ERROR("ImportImage: failed to parse the image header - {}", stbi_failure_reason());
            return eGraphicsError::ImageParseFailed;
        }

        const bool bHdr = stbi_is_hdr_from_memory(pData, size) != 0;
        const bool b16  = stbi_is_16_bit_from_memory(pData, size) != 0;

        // 3 채널이면 로더가 알파까지 채워 4 채널로 반환. 팔레트 PNG 는 info 채널 수와 결과가 다를 수 있어 그 외엔 0
        const int requestChannels = (_flags.Has(eImageImportOption::GpuCompatible) && infoChannels == 3) ? 4 : 0;

        eChannelFormat channelFormat = eChannelFormat::UNorm8;
        int            fileChannels;
        void*          pPixels = nullptr;
        if (bHdr)
        {
            channelFormat = eChannelFormat::Float;
            pPixels       = stbi_loadf_from_memory(pData, size, &width, &height, &fileChannels, requestChannels);
        }
        else if (b16)
        {
            channelFormat = eChannelFormat::UNorm16;
            pPixels       = stbi_load_16_from_memory(pData, size, &width, &height, &fileChannels, requestChannels);
        }
        else
        {
            pPixels = stbi_load_from_memory(pData, size, &width, &height, &fileChannels, requestChannels);
        }

        if (!pPixels)
        {
            JUG_CORE_LOG_ERROR("ImportImage: failed to decode the image - {}", stbi_failure_reason());
            return eGraphicsError::ImageDecodeFailed;
        }

        ImageDesc desc     = {};
        desc.width         = static_cast<uint32_t>(width);
        desc.height        = static_cast<uint32_t>(height);
        desc.channelFormat = channelFormat;
        desc.numChannels   = static_cast<uint32_t>(requestChannels != 0 ? requestChannels : fileChannels);
        desc.bSRGB         = channelFormat == eChannelFormat::UNorm8 && desc.numChannels >= 3 && _flags.Has(eImageImportOption::SRGB);
        desc.gpuFormat     = DeriveGpuFormat(desc.channelFormat, desc.numChannels, desc.bSRGB);

        // stb 의 malloc 결과를 복사 없이 채택 (ImageAllocator 가 free)
        return Image(desc, PixelBuffer { static_cast<std::byte*>(pPixels), desc.CalcSize() });
    }

    [[nodiscard]] Result<Image> ImportGif_(
        const MemoryView                _mem,
        const Flags<eImageImportOption> _flags)
    {
        int      width;
        int      height;
        int      numFrames;
        int      fileChannels;
        stbi_uc* pPixels = stbi_load_gif_from_memory(reinterpret_cast<const stbi_uc*>(_mem.GetPtr()), static_cast<int>(_mem.GetSize()), nullptr, &width, &height, &numFrames, &fileChannels, 4);
        if (!pPixels)
        {
            JUG_CORE_LOG_ERROR("ImportImage: failed to decode the GIF - {}", stbi_failure_reason());
            return eGraphicsError::ImageDecodeFailed;
        }

        // 프레임이 전부 한 버퍼로 옴. 프레임 = 레이어
        ImageDesc desc     = {};
        desc.width         = static_cast<uint32_t>(width);
        desc.height        = static_cast<uint32_t>(height);
        desc.numLayers     = static_cast<uint32_t>(numFrames);
        desc.channelFormat = eChannelFormat::UNorm8;
        desc.numChannels   = 4;
        desc.bSRGB         = _flags.Has(eImageImportOption::SRGB);
        desc.gpuFormat     = DeriveGpuFormat(desc.channelFormat, desc.numChannels, desc.bSRGB);

        return Image(desc, PixelBuffer { reinterpret_cast<std::byte*>(pPixels), desc.CalcSize() });
    }

    // ===========================================
    //  EXR (tinyexr)
    // ===========================================

    // R, G, B, A, Y -> 0, 1, 2, 3, 0. 그 외 이름은 kInvalidIndex.
    [[nodiscard]] uint32_t ToExrSlotOrInvalid_(
        const StringView _name)
    {
        if (_name == "Y")
        {
            return 0;
        }

        const size_t slot = _name.size() == 1 ? StringView { "RGBA" }.find(_name[0]) : StringView::npos;
        return slot == StringView::npos ? kInvalidIndex : static_cast<uint32_t>(slot);
    }

    // _header 는 파싱 완료 상태. 해제는 호출자 몫
    [[nodiscard]] Result<Image> ConvertExr_(
        EXRHeader&       _header,
        const MemoryView _mem)
    {
        // 채널 분석: R,G,B,A 앞쪽부터 연속된 부분집합 또는 단일 채널(R/G/B/A/Y)만 지원
        ARRAY<int, 4> slotChannels = { -1, -1, -1, -1 };   // 슬롯 -> header 채널 인덱스
        bool          bAllHalf     = true;
        for (int c = 0; c < _header.num_channels; ++c)
        {
            const EXRChannelInfo& channel = _header.channels[c];

            const uint32_t slot = ToExrSlotOrInvalid_(channel.name);
            if (!IsValid(slot) || slotChannels[slot] != -1 || channel.pixel_type == TINYEXR_PIXELTYPE_UINT || channel.x_sampling != 1 || channel.y_sampling != 1)
            {
                JUG_CORE_LOG_ERROR("ImportImage: unsupported EXR channel '{}' (only R, G, B, A, Y channels of half/float type are supported)", channel.name);
                return eGraphicsError::ImageUnsupportedFeature;
            }
            slotChannels[slot] = c;
            bAllHalf           = bAllHalf && channel.pixel_type == TINYEXR_PIXELTYPE_HALF;
        }

        uint32_t numChannels = 0;
        if (_header.num_channels == 1)
        {
            // 단일 채널은 이름과 상관없이 1 채널 이미지
            numChannels     = 1;
            slotChannels[0] = 0;
        }
        else
        {
            while (numChannels < slotChannels.size() && slotChannels[numChannels] != -1)
            {
                ++numChannels;
            }
            if (numChannels == 0 || numChannels != static_cast<uint32_t>(_header.num_channels))
            {
                JUG_CORE_LOG_ERROR("ImportImage: unsupported EXR channel layout (channels must be R, RG, RGB or RGBA)");
                return eGraphicsError::ImageUnsupportedFeature;
            }
        }

        // 전부 half 면 half, 아니면 float 로 읽음
        for (int c = 0; c < _header.num_channels; ++c)
        {
            _header.requested_pixel_types[c] = bAllHalf ? TINYEXR_PIXELTYPE_HALF : TINYEXR_PIXELTYPE_FLOAT;
        }

        EXRImage exrImage;
        InitEXRImage(&exrImage);
        const char* pError = nullptr;
        if (LoadEXRImageFromMemory(&exrImage, &_header, reinterpret_cast<const unsigned char*>(_mem.GetPtr()), _mem.GetSize(), &pError) != TINYEXR_SUCCESS)
        {
            JUG_CORE_LOG_ERROR("ImportImage: failed to decode the EXR - {}", pError ? pError : "unknown error");
            FreeEXRErrorMessage(pError);   // 실패 시 tinyexr 가 이미지를 직접 해제함. FreeEXRImage 하면 이중 해제
            return eGraphicsError::ImageDecodeFailed;
        }

        ImageDesc desc     = {};
        desc.width         = static_cast<uint32_t>(exrImage.width);
        desc.height        = static_cast<uint32_t>(exrImage.height);
        desc.channelFormat = bAllHalf ? eChannelFormat::Half : eChannelFormat::Float;
        desc.numChannels   = numChannels;
        desc.gpuFormat     = DeriveGpuFormat(desc.channelFormat, desc.numChannels, false);

        const uint32_t channelSize = GetChannelSize(desc.channelFormat);
        const size_t   numPixels   = static_cast<size_t>(desc.width) * desc.height;

        PixelBuffer pixels = { kNoInit, desc.CalcSize() };

        // planar 채널을 interleave
        std::byte* pDst = pixels.GetPtr();
        for (uint32_t slot = 0; slot < numChannels; ++slot)
        {
            const std::byte* pSrc = reinterpret_cast<const std::byte*>(exrImage.images[slotChannels[slot]]);
            for (size_t i = 0; i < numPixels; ++i)
            {
                std::memcpy(pDst + (i * numChannels + slot) * channelSize, pSrc + i * channelSize, channelSize);
            }
        }

        FreeEXRImage(&exrImage);
        return Image(desc, std::move(pixels));
    }

    [[nodiscard]] Result<Image> ImportExr_(
        const MemoryView _mem)
    {
        const unsigned char* pData = reinterpret_cast<const unsigned char*>(_mem.GetPtr());

        EXRVersion version;
        if (ParseEXRVersionFromMemory(&version, pData, _mem.GetSize()) != TINYEXR_SUCCESS)
        {
            JUG_CORE_LOG_ERROR("ImportImage: failed to parse the EXR version");
            return eGraphicsError::ImageParseFailed;
        }
        if (version.multipart || version.non_image || version.tiled)
        {
            JUG_CORE_LOG_ERROR("ImportImage: multipart / deep / tiled EXR is not supported");
            return eGraphicsError::ImageUnsupportedFeature;
        }

        EXRHeader header;
        InitEXRHeader(&header);
        const char* pError = nullptr;
        if (ParseEXRHeaderFromMemory(&header, &version, pData, _mem.GetSize(), &pError) != TINYEXR_SUCCESS)
        {
            JUG_CORE_LOG_ERROR("ImportImage: failed to parse the EXR header - {}", pError ? pError : "unknown error");
            FreeEXRErrorMessage(pError);
            FreeEXRHeader(&header);   // 파싱 중 실패해도 일부 할당이 남음
            return eGraphicsError::ImageParseFailed;
        }

        Result<Image> result = ConvertExr_(header, _mem);
        FreeEXRHeader(&header);
        return result;
    }

    // ===========================================
    //  DDS
    // ===========================================

    // DX10 헤더가 없는 DDS 가 쓰는 FourCC. 그 외 legacy 픽셀 포맷(비트마스크 등)은 미지원
    struct LegacyFourCc
    {
        uint32_t       fourCC = 0;
        eTextureFormat format = eTextureFormat::BC1_UNorm;
    };

    constexpr ARRAY<LegacyFourCc, 7> kLegacyFourCcs = {
        {
         { JUG_FOURCC('D', 'X', 'T', '1'), eTextureFormat::BC1_UNorm },
         { JUG_FOURCC('D', 'X', 'T', '3'), eTextureFormat::BC2_UNorm },
         { JUG_FOURCC('D', 'X', 'T', '5'), eTextureFormat::BC3_UNorm },
         { JUG_FOURCC('A', 'T', 'I', '1'), eTextureFormat::BC4_UNorm },
         { JUG_FOURCC('B', 'C', '4', 'U'), eTextureFormat::BC4_UNorm },
         { JUG_FOURCC('A', 'T', 'I', '2'), eTextureFormat::BC5_UNorm },
         { JUG_FOURCC('B', 'C', '5', 'U'), eTextureFormat::BC5_UNorm },
         }
    };

    [[nodiscard]] const image_detail::ImageFormatEntry* FindLegacyFormatOrNull_(
        const image_detail::DDS_PIXEL_FORMAT& _pf)
    {
        if (_pf.flags & image_detail::kDdpfFourCc)
        {
            for (const LegacyFourCc& legacy: kLegacyFourCcs)
            {
                if (legacy.fourCC == _pf.fourCC)
                {
                    return image_detail::FindFormatByGpuFormatOrNull(legacy.format);
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] Result<Image> ImportDds_(
        const MemoryView _mem)
    {
        constexpr size_t kHeaderSize = sizeof(uint32_t) + sizeof(image_detail::DDS_HEADER);
        if (_mem.GetSize() < kHeaderSize)
        {
            JUG_CORE_LOG_ERROR("ImportImage: DDS file is too small to hold a header");
            return eGraphicsError::ImageParseFailed;
        }

        image_detail::DDS_HEADER header;
        std::memcpy(&header, _mem.GetPtr() + sizeof(uint32_t), sizeof(header));
        if (header.size != sizeof(header) || header.ddspf.size != sizeof(header.ddspf))
        {
            JUG_CORE_LOG_ERROR("ImportImage: invalid DDS header size");
            return eGraphicsError::ImageInvalidData;
        }

        size_t                        offset = kHeaderSize;
        image_detail::DDS_HEADER_DX10 dx10   = {};
        const bool                    bDx10  = (header.ddspf.flags & image_detail::kDdpfFourCc) && header.ddspf.fourCC == image_detail::kDdsFourCcDx10;
        if (bDx10)
        {
            if (_mem.GetSize() < offset + sizeof(dx10))
            {
                JUG_CORE_LOG_ERROR("ImportImage: DDS file is too small to hold the DX10 header");
                return eGraphicsError::ImageParseFailed;
            }
            std::memcpy(&dx10, _mem.GetPtr() + offset, sizeof(dx10));
            offset += sizeof(dx10);
        }

        // 크기 / mip
        ImageDesc desc = {};
        desc.width     = header.width;
        desc.height    = header.height;
        desc.numMips   = (header.flags & image_detail::kDdsdMipMapCount) ? Max(1u, header.mipMapCount) : 1u;
        if (desc.width == 0 || desc.height == 0)
        {
            JUG_CORE_LOG_ERROR("ImportImage: DDS has zero size");
            return eGraphicsError::ImageInvalidData;
        }

        // 포맷
        const image_detail::ImageFormatEntry* pFormatOrNull = bDx10 ? image_detail::FindFormatByDxgiOrNull(dx10.dxgiFormat) : FindLegacyFormatOrNull_(header.ddspf);
        if (!pFormatOrNull)
        {
            JUG_CORE_LOG_ERROR("ImportImage: unsupported DDS pixel format (DXGI {}, flags {:#x}, fourCC {:#x})", bDx10 ? dx10.dxgiFormat : 0, header.ddspf.flags, header.ddspf.fourCC);
            return eGraphicsError::ImageUnsupportedFormat;
        }
        desc.channelFormat = pFormatOrNull->channelFormat;
        desc.numChannels   = pFormatOrNull->numChannels;
        desc.bSRGB         = pFormatOrNull->bSRGB;
        desc.gpuFormat     = pFormatOrNull->gpuFormat;

        // 레이어 / 볼륨 / 큐브맵
        bool bVolume = false;
        if (bDx10)
        {
            if (dx10.arraySize == 0)
            {
                JUG_CORE_LOG_ERROR("ImportImage: DDS array size is zero");
                return eGraphicsError::ImageInvalidData;
            }

            switch (dx10.resourceDimension)
            {
                case image_detail::kDdsDimensionTexture1D:
                    if (desc.height != 1)
                    {
                        JUG_CORE_LOG_ERROR("ImportImage: DDS 1D texture must have height 1");
                        return eGraphicsError::ImageInvalidData;
                    }
                    break;
                case image_detail::kDdsDimensionTexture2D: break;
                case image_detail::kDdsDimensionTexture3D: bVolume = true; break;
                default:
                    JUG_CORE_LOG_ERROR("ImportImage: invalid DDS resource dimension {}", dx10.resourceDimension);
                    return eGraphicsError::ImageInvalidData;
            }

            if (bVolume)
            {
                if (dx10.arraySize != 1)
                {
                    JUG_CORE_LOG_ERROR("ImportImage: DDS volume texture can not be an array");
                    return eGraphicsError::ImageInvalidData;
                }
            }
            else if (dx10.miscFlag & image_detail::kDdsMiscTextureCube)
            {
                if (dx10.resourceDimension != image_detail::kDdsDimensionTexture2D || dx10.arraySize > Max<uint32_t>() / 6)
                {
                    JUG_CORE_LOG_ERROR("ImportImage: invalid DDS cubemap");
                    return eGraphicsError::ImageInvalidData;
                }
                desc.numLayers = dx10.arraySize * 6;   // 큐브 개수
                desc.bCubemap  = true;
            }
            else
            {
                desc.numLayers = dx10.arraySize;
            }
        }
        else if (header.caps2 & image_detail::kDdsCaps2Cubemap)
        {
            if ((header.caps2 & image_detail::kDdsCaps2AllFaces) != image_detail::kDdsCaps2AllFaces)
            {
                JUG_CORE_LOG_ERROR("ImportImage: DDS cubemap with missing faces is not supported");
                return eGraphicsError::ImageUnsupportedFeature;
            }
            desc.numLayers = 6;   // +X, -X, +Y, -Y, +Z, -Z
            desc.bCubemap  = true;
        }
        else if ((header.caps2 & image_detail::kDdsCaps2Volume) && (header.flags & image_detail::kDdsdDepth))
        {
            bVolume = true;
        }

        if (bVolume)
        {
            desc.depth = header.depth;
            if (desc.depth == 0)
            {
                JUG_CORE_LOG_ERROR("ImportImage: DDS volume texture has zero depth");
                return eGraphicsError::ImageInvalidData;
            }
        }

        // 검증 (이후 크기 계산이 오버플로 / assert 나지 않게)
        if (desc.width > kMaxDdsDimension || desc.height > kMaxDdsDimension || desc.depth > kMaxDdsDimension)
        {
            JUG_CORE_LOG_ERROR("ImportImage: DDS size {}x{}x{} exceeds the supported limit {}", desc.width, desc.height, desc.depth, kMaxDdsDimension);
            return eGraphicsError::ImageUnsupportedFeature;
        }
        if (desc.numMips > CalcNumMips(desc.width, desc.height, desc.depth))
        {
            JUG_CORE_LOG_ERROR("ImportImage: DDS has too many mips ({})", desc.numMips);
            return eGraphicsError::ImageInvalidData;
        }

        // 파일 데이터 배치(레이어 -> mip 큰 것부터)가 Image 와 같음
        const size_t layerSize = desc.CalcLayerSize();
        const size_t remaining = _mem.GetSize() - offset;
        if (remaining % layerSize != 0 || remaining / layerSize != desc.numLayers)
        {
            JUG_CORE_LOG_ERROR("ImportImage: DDS pixel data size {} does not match the header (layer size {}, layers {})", remaining, layerSize, desc.numLayers);
            return eGraphicsError::ImageInvalidData;
        }

        PixelBuffer pixels = { kNoInit, remaining };
        std::memcpy(pixels.GetPtr(), _mem.GetPtr() + offset, remaining);
        return Image(desc, std::move(pixels));
    }

    // ===========================================
    //  Decompress (BC -> uncompressed)
    // ===========================================

    void DecodeBlock_(
        const eTextureFormat _bcFormat,
        const std::byte*     _pBlock,
        std::byte*           _pOut,
        const uint32_t       _pixelSize)
    {
        const int pitch = static_cast<int>(kBlockSize * _pixelSize);
        switch (_bcFormat)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eTextureFormat::BC1_UNorm:
            case eTextureFormat::BC1_UNorm_SRGB: bcdec_bc1(_pBlock, _pOut, pitch); break;

            case eTextureFormat::BC2_UNorm:
            case eTextureFormat::BC2_UNorm_SRGB: bcdec_bc2(_pBlock, _pOut, pitch); break;

            case eTextureFormat::BC3_UNorm:
            case eTextureFormat::BC3_UNorm_SRGB: bcdec_bc3(_pBlock, _pOut, pitch); break;

            case eTextureFormat::BC4_UNorm: bcdec_bc4(_pBlock, _pOut, pitch); break;
            case eTextureFormat::BC5_UNorm: bcdec_bc5(_pBlock, _pOut, pitch); break;

            case eTextureFormat::BC6H_UFloat:
            case eTextureFormat::BC6H_SFloat: bcdec_bc6h_float(_pBlock, _pOut, static_cast<int>(kBlockSize * 3), _bcFormat == eTextureFormat::BC6H_SFloat); break;   // pitch 단위는 float 개수

            case eTextureFormat::BC7_UNorm:
            case eTextureFormat::BC7_UNorm_SRGB: bcdec_bc7(_pBlock, _pOut, pitch); break;

            default: JUG_ASSERT(false, "Unsupported BC format.\n"); break;
        }
    }

    // 2D slice 하나 해제. 가장자리 블록은 임시 블록에 풀고 유효한 부분만 복사
    void DecompressSlice_(
        const eTextureFormat _bcFormat,
        const std::byte*     _pSrc,
        std::byte*           _pDst,
        const uint32_t       _width,
        const uint32_t       _height,
        const uint32_t       _pixelSize)
    {
        const uint32_t numBlocksX = (_width + kBlockSize - 1) / kBlockSize;
        const uint32_t numBlocksY = (_height + kBlockSize - 1) / kBlockSize;
        const uint32_t blockSize  = GetBitPerPixel(_bcFormat) * kBlockSize * kBlockSize / 8;
        const size_t   dstPitch   = static_cast<size_t>(_width) * _pixelSize;

        alignas(4) ARRAY<std::byte, kBlockSize * kBlockSize * kMaxBlockPixelSize> block = {};
        for (uint32_t by = 0; by < numBlocksY; ++by)
        {
            for (uint32_t bx = 0; bx < numBlocksX; ++bx)
            {
                DecodeBlock_(_bcFormat, _pSrc + (static_cast<size_t>(by) * numBlocksX + bx) * blockSize, block.data(), _pixelSize);

                const uint32_t numCols = Min(kBlockSize, _width - bx * kBlockSize);
                const uint32_t numRows = Min(kBlockSize, _height - by * kBlockSize);
                for (uint32_t row = 0; row < numRows; ++row)
                {
                    std::memcpy(_pDst + (static_cast<size_t>(by) * kBlockSize + row) * dstPitch + static_cast<size_t>(bx) * kBlockSize * _pixelSize, block.data() + static_cast<size_t>(row) * kBlockSize * _pixelSize, static_cast<size_t>(numCols) * _pixelSize);
                }
            }
        }
    }

    // BC 포맷 -> 해제 결과. 목록에 없으면 (SNorm 등) 미지원
    struct DecompressedFormat
    {
        eTextureFormat bcFormat      = eTextureFormat::BC1_UNorm;
        eChannelFormat channelFormat = eChannelFormat::UNorm8;
        uint32_t       numChannels   = 0;
    };

    constexpr ARRAY<DecompressedFormat, 12> kDecompressedFormats = {
        {
         { eTextureFormat::BC1_UNorm, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC1_UNorm_SRGB, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC2_UNorm, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC2_UNorm_SRGB, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC3_UNorm, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC3_UNorm_SRGB, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC4_UNorm, eChannelFormat::UNorm8, 1 },
         { eTextureFormat::BC5_UNorm, eChannelFormat::UNorm8, 2 },
         { eTextureFormat::BC6H_UFloat, eChannelFormat::Float, 3 },
         { eTextureFormat::BC6H_SFloat, eChannelFormat::Float, 3 },
         { eTextureFormat::BC7_UNorm, eChannelFormat::UNorm8, 4 },
         { eTextureFormat::BC7_UNorm_SRGB, eChannelFormat::UNorm8, 4 },
         }
    };

    [[nodiscard]] Error Decompress_(
        Image& _image)
    {
        const ImageDesc      srcDesc  = _image.GetDesc();
        const eTextureFormat bcFormat = srcDesc.gpuFormat.GetValue();

        const DecompressedFormat* pDecompressedOrNull = nullptr;
        for (const DecompressedFormat& decompressed: kDecompressedFormats)
        {
            if (decompressed.bcFormat == bcFormat)
            {
                pDecompressedOrNull = &decompressed;
                break;
            }
        }
        if (!pDecompressedOrNull)
        {
            JUG_CORE_LOG_ERROR("ImportImage: decompressing the BC format {} is not supported", static_cast<int>(bcFormat));
            return eGraphicsError::ImageUnsupportedFormat;
        }

        ImageDesc dstDesc     = srcDesc;
        dstDesc.channelFormat = pDecompressedOrNull->channelFormat;
        dstDesc.numChannels   = pDecompressedOrNull->numChannels;
        dstDesc.gpuFormat     = DeriveGpuFormat(dstDesc.channelFormat, dstDesc.numChannels, dstDesc.bSRGB);

        const uint32_t pixelSize = dstDesc.numChannels * GetChannelSize(dstDesc.channelFormat);

        PixelBuffer      dstPixels = { kNoInit, dstDesc.CalcSize() };
        const std::byte* pSrc      = _image.GetPixels().GetPtr();
        std::byte*       pDst      = dstPixels.GetPtr();
        for (uint32_t layer = 0; layer < srcDesc.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < srcDesc.numMips; ++mip)
            {
                const VEC3U size = CalcTextureSize(srcDesc.width, srcDesc.height, srcDesc.depth, mip);
                for (uint32_t slice = 0; slice < size.z; ++slice)
                {
                    DecompressSlice_(bcFormat, pSrc, pDst, size.x, size.y, pixelSize);
                    pSrc += srcDesc.CalcSlicePitch(size.x, size.y);
                    pDst += dstDesc.CalcSlicePitch(size.x, size.y);
                }
            }
        }

        _image = Image(dstDesc, std::move(dstPixels));
        return kOK;
    }

    // ===========================================
    //  GpuCompatible
    // ===========================================

    // 3 -> 4 채널 패딩. 비압축은 픽셀이 연속이라 한 번에 처리
    void PadToFourChannels_(
        Image& _image)
    {
        const ImageDesc desc = _image.GetDesc();
        JUG_ASSERT(desc.channelFormat != eChannelFormat::Compressed && desc.numChannels == 3, "Only 3 channel images can be padded.\n");

        const uint32_t channelSize = GetChannelSize(desc.channelFormat);
        const size_t   numPixels   = desc.CalcSize() / (3 * channelSize);

        ImageDesc paddedDesc   = desc;
        paddedDesc.numChannels = 4;
        paddedDesc.gpuFormat   = DeriveGpuFormat(paddedDesc.channelFormat, paddedDesc.numChannels, paddedDesc.bSRGB);

        const uint32_t alpha = kAlphaOnes[static_cast<size_t>(desc.channelFormat)];

        PixelBuffer      padded = { kNoInit, paddedDesc.CalcSize() };
        const std::byte* pSrc   = _image.GetPixels().GetPtr();
        std::byte*       pDst   = padded.GetPtr();
        for (size_t i = 0; i < numPixels; ++i)
        {
            std::memcpy(pDst + i * 4 * channelSize, pSrc + i * 3 * channelSize, 3 * channelSize);
            std::memcpy(pDst + (i * 4 + 3) * channelSize, &alpha, channelSize);
        }

        _image = Image(paddedDesc, std::move(padded));
    }

    // 디코딩 후 옵션 적용
    [[nodiscard]] Error Finalize_(
        Image&                          _image,
        const Flags<eImageImportOption> _flags)
    {
        if (_flags.Has(eImageImportOption::Decompress) && _image.GetDesc().channelFormat == eChannelFormat::Compressed)
        {
            JUG_RETURN_IF_ERROR(Decompress_(_image));
        }

        if (_flags.Has(eImageImportOption::GpuCompatible))
        {
            const ImageDesc& desc = _image.GetDesc();
            if (desc.width > kMaxGpuTextureSize || desc.height > kMaxGpuTextureSize || desc.depth > kMaxGpuTextureDepth || desc.numLayers > kMaxGpuTextureLayers)
            {
                JUG_CORE_LOG_ERROR("ImportImage: image {}x{}x{} (layers {}) exceeds the GPU limits", desc.width, desc.height, desc.depth, desc.numLayers);
                return eGraphicsError::ImageUnsupportedFeature;
            }
            if (!desc.gpuFormat.HasValue())
            {
                PadToFourChannels_(_image);
            }
            JUG_ASSERT(_image.GetDesc().gpuFormat.HasValue(), "GpuCompatible image must have a gpuFormat.\n");
        }
        return kOK;
    }

    // ===========================================
    //  Dispatch
    // ===========================================

    [[nodiscard]] Result<Image> Decode_(
        const MemoryView                _mem,
        const Flags<eImageImportOption> _flags)
    {
        if (StartsWith_(_mem, kMagicDds))
        {
            return ImportDds_(_mem);
        }
        if (StartsWith_(_mem, kMagicExr))
        {
            return ImportExr_(_mem);
        }

        // 이하 stb (크기를 int 로 받음)
        if (_mem.GetSize() > static_cast<size_t>(Max<int>()))
        {
            JUG_CORE_LOG_ERROR("ImportImage: image file is too large ({} bytes)", _mem.GetSize());
            return eGraphicsError::ImageUnsupportedFeature;
        }
        if (StartsWith_(_mem, kMagicGif))
        {
            return ImportGif_(_mem, _flags);
        }

        // TGA 는 magic 이 없어서 마지막에 stb 에게 맡긴다.
        const bool bKnownFormat = StartsWith_(_mem, kMagicPng) || StartsWith_(_mem, kMagicJpeg) || StartsWith_(_mem, kMagicBmp) || StartsWith_(_mem, kMagicHdr) || StartsWith_(_mem, kMagicRgbe);
        return ImportStb_(_mem, _flags, bKnownFormat);
    }
}   // namespace

Result<Image> ImportImage(
    const MemoryView                _mem,
    const Flags<eImageImportOption> _flags)
{
    Result<Image> result = Decode_(_mem, _flags);
    JUG_RETURN_IF_ERROR(result);
    JUG_RETURN_IF_ERROR(Finalize_(result.GetValue(), _flags));
    return result;
}

Result<Image> ImportImageFromFile(
    const FilePath&                 _path,
    const Flags<eImageImportOption> _flags)
{
    const Result<ByteBuffer> file = ReadFile(_path);
    JUG_RETURN_IF_ERROR(file);
    if (file->GetSize() == 0)
    {
        JUG_CORE_LOG_ERROR("ImportImageFromFile: file '{}' is empty", _path.string());
        return eGraphicsError::ImageParseFailed;
    }

    return ImportImage(MemoryView { file->GetPtr(), file->GetSize() }, _flags);
}

}   // namespace jug
