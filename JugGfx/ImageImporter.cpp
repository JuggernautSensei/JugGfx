#include "pch.h"
#include "ImageImporter.h"

#include <cmath>
#include <ktx.h>
#include <OpenImageIO/imageio.h>

#include "Error.h"
#include "ImageDetail.h"
#include "Texture.h"

namespace jug
{

namespace
{
    // KTX identifier
    constexpr ARRAY<uint8_t, 12> kKtx1Identifier = { 0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n' };
    constexpr ARRAY<uint8_t, 12> kKtx2Identifier = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };

    // ===========================================
    //  Import
    // ===========================================

    [[nodiscard]] Optional<eImageFileFormat> ToImageFormat_(
        const StringView _oiioFormatName)
    {
        if (_oiioFormatName == "png")
        {
            return eImageFileFormat::PNG;
        }
        if (_oiioFormatName == "jpeg")
        {
            return eImageFileFormat::JPEG;
        }
        if (_oiioFormatName == "bmp")
        {
            return eImageFileFormat::BMP;
        }
        if (_oiioFormatName == "targa")
        {
            return eImageFileFormat::TGA;
        }
        if (_oiioFormatName == "hdr")
        {
            return eImageFileFormat::HDR;
        }
        if (_oiioFormatName == "openexr")
        {
            return eImageFileFormat::EXR;
        }
        if (_oiioFormatName == "tiff")
        {
            return eImageFileFormat::TIFF;
        }
        if (_oiioFormatName == "gif")
        {
            return eImageFileFormat::GIF;
        }
        if (_oiioFormatName == "pnm")
        {
            return eImageFileFormat::PNM;
        }
        if (_oiioFormatName == "sgi")
        {
            return eImageFileFormat::SGI;
        }
        if (_oiioFormatName == "rla")
        {
            return eImageFileFormat::RLA;
        }
        if (_oiioFormatName == "iff")
        {
            return eImageFileFormat::IFF;
        }
        if (_oiioFormatName == "dpx")
        {
            return eImageFileFormat::DPX;
        }
        return kNullopt;
    }

    [[nodiscard]] const char* GuessOiioName_(
        const MemoryView _mem)
    {
        const uint8_t* pData = reinterpret_cast<const uint8_t*>(_mem.GetPtr());
        const size_t   size  = _mem.GetSize();
        if (size >= 4 && pData[0] == 0x89 && pData[1] == 'P' && pData[2] == 'N' && pData[3] == 'G')
        {
            return "memory.png";
        }
        if (size >= 3 && pData[0] == 0xFF && pData[1] == 0xD8 && pData[2] == 0xFF)
        {
            return "memory.jpg";
        }
        if (size >= 4 && pData[0] == 'G' && pData[1] == 'I' && pData[2] == 'F' && pData[3] == '8')
        {
            return "memory.gif";
        }
        if (size >= 4 && pData[0] == 0x76 && pData[1] == 0x2F && pData[2] == 0x31 && pData[3] == 0x01)
        {
            return "memory.exr";
        }
        if (size >= 4 && ((pData[0] == 'I' && pData[1] == 'I' && pData[2] == 42 && pData[3] == 0) || (pData[0] == 'M' && pData[1] == 'M' && pData[2] == 0 && pData[3] == 42)))
        {
            return "memory.tif";
        }
        if (size >= 2 && pData[0] == '#' && pData[1] == '?')
        {
            return "memory.hdr";
        }
        if (size >= 2 && pData[0] == 'B' && pData[1] == 'M')
        {
            return "memory.bmp";
        }
        if (size >= 2 && pData[0] == 0x01 && pData[1] == 0xDA)
        {
            return "memory.sgi";
        }
        if (size >= 4 && pData[0] == 'F' && pData[1] == 'O' && pData[2] == 'R' && (pData[3] == '4' || pData[3] == 'M'))
        {
            return "memory.iff";
        }
        if (size >= 4 && ((pData[0] == 'S' && pData[1] == 'D' && pData[2] == 'P' && pData[3] == 'X') || (pData[0] == 'X' && pData[1] == 'P' && pData[2] == 'D' && pData[3] == 'S')))
        {
            return "memory.dpx";
        }
        if (size >= 2 && pData[0] == 'P' && ((pData[1] >= '1' && pData[1] <= '6') || pData[1] == 'f' || pData[1] == 'F'))
        {
            return "memory.pnm";
        }
        return "memory";
    }

    [[nodiscard]] bool IsDDS_(
        const MemoryView _mem)
    {
        if (_mem.GetSize() < sizeof(uint32_t))
        {
            return false;
        }

        uint32_t magic;
        std::memcpy(&magic, _mem.GetPtr(), sizeof(uint32_t));
        return magic == image_detail::kDdsMagic;
    }

    [[nodiscard]] bool IsKTX_(
        const MemoryView _mem)
    {
        if (_mem.GetSize() < kKtx1Identifier.size())
        {
            return false;
        }

        return std::memcmp(_mem.GetPtr(), kKtx1Identifier.data(), kKtx1Identifier.size()) == 0
            || std::memcmp(_mem.GetPtr(), kKtx2Identifier.data(), kKtx2Identifier.size()) == 0;
    }

    [[nodiscard]] Optional<eTextureFormat> ToTextureFormat_(
        const ChannelFormat _type,
        const uint32_t       _numChannels,
        const bool           _bSRGB)
    {
        if (_bSRGB && _numChannels == 4 && _type == ChannelFormat::UINT8)
        {
            return eTextureFormat::RGBA8_UNorm_SRGB;
        }

        switch (_type.basetype)
        {
            case ChannelFormat::UINT8:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R8_UNorm;
                    case 2: return eTextureFormat::RG8_UNorm;
                    case 4: return eTextureFormat::RGBA8_UNorm;
                    default: return kNullopt;
                }
            case ChannelFormat::INT8:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R8_SNorm;
                    case 2: return eTextureFormat::RG8_SNorm;
                    case 4: return eTextureFormat::RGBA8_SNorm;
                    default: return kNullopt;
                }
            case ChannelFormat::UINT16:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R16_UNorm;
                    case 2: return eTextureFormat::RG16_UNorm;
                    case 4: return eTextureFormat::RGBA16_UNorm;
                    default: return kNullopt;
                }
            case ChannelFormat::INT16:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R16_SNorm;
                    case 2: return eTextureFormat::RG16_SNorm;
                    case 4: return eTextureFormat::RGBA16_SNorm;
                    default: return kNullopt;
                }
            case ChannelFormat::UINT32:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R32_UInt;
                    case 2: return eTextureFormat::RG32_UInt;
                    case 4: return eTextureFormat::RGBA32_UInt;
                    default: return kNullopt;
                }
            case ChannelFormat::INT32:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R32_Int;
                    case 2: return eTextureFormat::RG32_Int;
                    case 4: return eTextureFormat::RGBA32_Int;
                    default: return kNullopt;
                }
            case ChannelFormat::HALF:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R16_Float;
                    case 2: return eTextureFormat::RG16_Float;
                    case 4: return eTextureFormat::RGBA16_Float;
                    default: return kNullopt;
                }
            case ChannelFormat::FLOAT:
                switch (_numChannels)
                {
                    case 1: return eTextureFormat::R32_Float;
                    case 2: return eTextureFormat::RG32_Float;
                    case 4: return eTextureFormat::RGBA32_Float;
                    default: return kNullopt;
                }
            default:
                return kNullopt;
        }
    }

    // mip 생성/premultiply 는 채널 순서와 무관하므로 BGRA 도 허용.
    [[nodiscard]] PixelFormat ToMipPixelFormat_(
        const eTextureFormat _format)
    {
        if (_format == eTextureFormat::BGRA8_UNorm || _format == eTextureFormat::BGRA8_UNorm_SRGB)
        {
            return PixelFormat { ChannelFormat::UINT8, 4 };
        }
        return image_detail::ToPixelFormatOrUnknown(_format);
    }

    // 채널 포맷으로 desc 를 채움. GPU 포맷은 채널 구성에서 유도.
    void SetPixelFormat_(
        ImageDesc&                       _desc,
        const PixelFormat& _format)
    {
        _desc.format = _format;
        _desc.gpuFormat   = ToTextureFormat_(_format.formatOrUnknown, _format.numChannels, _format.bSRGB);
    }

    // texture format 으로 desc 를 채움. 압축/packed/BGRA 는 채널 포맷 unknown.
    void SetTextureFormat_(
        ImageDesc&           _desc,
        const eTextureFormat _format)
    {
        _desc.format = image_detail::ToPixelFormatOrUnknown(_format);
        _desc.gpuFormat   = _format;
    }

    [[nodiscard]] float SrgbToLinear_(
        const float _value)
    {
        return _value <= 0.04045f ? _value / 12.92f : Pow((_value + 0.055f) / 1.055f, 2.4f);
    }

    [[nodiscard]] float LinearToSrgb_(
        const float _value)
    {
        return _value <= 0.0031308f ? _value * 12.92f : 1.055f * Pow(_value, 1.f / 2.4f) - 0.055f;
    }

    // sRGB 는 RGB 채널만 변환 (alpha 제외)
    void ApplyColorSpace_(
        Buffer<float>& _pixels,
        const uint32_t _numChannels,
        const bool     _bToLinear)
    {
        const uint32_t numColorChannels = Min(_numChannels, 3u);
        for (size_t i = 0; i < _pixels.GetSize(); i += _numChannels)
        {
            for (uint32_t c = 0; c < numColorChannels; ++c)
            {
                _pixels[i + c] = _bToLinear ? SrgbToLinear_(_pixels[i + c]) : LinearToSrgb_(_pixels[i + c]);
            }
        }
    }

    // rgb *= alpha. alpha 는 채널 3. sRGB 면 linear 공간에서 곱함.
    void MultiplyAlpha_(
        Buffer<float>& _pixels,
        const uint32_t _numChannels,
        const bool     _bSRGB)
    {
        for (size_t i = 0; i < _pixels.GetSize(); i += _numChannels)
        {
            const float alpha = _pixels[i + image_detail::kAlphaChannel];
            for (uint32_t c = 0; c < image_detail::kAlphaChannel; ++c)
            {
                _pixels[i + c] = _bSRGB ? LinearToSrgb_(SrgbToLinear_(_pixels[i + c]) * alpha) : _pixels[i + c] * alpha;
            }
        }
    }

    // 2x2x2 box filter. 홀수 크기는 경계 픽셀을 반복 사용.
    void Downsample_(
        const Buffer<float>& _src,
        const VEC3U          _srcSize,
        Buffer<float>&       _dst,
        const VEC3U          _dstSize,
        const uint32_t       _numChannels)
    {
        _dst.ResizeUninitialized(static_cast<size_t>(_dstSize.x) * _dstSize.y * _dstSize.z * _numChannels);
        for (uint32_t z = 0; z < _dstSize.z; ++z)
        {
            const uint32_t z0 = Min(z * 2, _srcSize.z - 1);
            const uint32_t z1 = Min(z * 2 + 1, _srcSize.z - 1);
            for (uint32_t y = 0; y < _dstSize.y; ++y)
            {
                const uint32_t y0 = Min(y * 2, _srcSize.y - 1);
                const uint32_t y1 = Min(y * 2 + 1, _srcSize.y - 1);
                for (uint32_t x = 0; x < _dstSize.x; ++x)
                {
                    const uint32_t x0 = Min(x * 2, _srcSize.x - 1);
                    const uint32_t x1 = Min(x * 2 + 1, _srcSize.x - 1);

                    const ARRAY<size_t, 8> taps = {
                        { (static_cast<size_t>(z0) * _srcSize.y + y0) * _srcSize.x + x0,
                         (static_cast<size_t>(z0) * _srcSize.y + y0) * _srcSize.x + x1,
                         (static_cast<size_t>(z0) * _srcSize.y + y1) * _srcSize.x + x0,
                         (static_cast<size_t>(z0) * _srcSize.y + y1) * _srcSize.x + x1,
                         (static_cast<size_t>(z1) * _srcSize.y + y0) * _srcSize.x + x0,
                         (static_cast<size_t>(z1) * _srcSize.y + y0) * _srcSize.x + x1,
                         (static_cast<size_t>(z1) * _srcSize.y + y1) * _srcSize.x + x0,
                         (static_cast<size_t>(z1) * _srcSize.y + y1) * _srcSize.x + x1 }
                    };

                    const size_t dstIndex = ((static_cast<size_t>(z) * _dstSize.y + y) * _dstSize.x + x) * _numChannels;
                    for (uint32_t c = 0; c < _numChannels; ++c)
                    {
                        float sum = 0.f;
                        for (const size_t tap: taps)
                        {
                            sum += _src[tap * _numChannels + c];
                        }
                        _dst[dstIndex + c] = sum / static_cast<float>(taps.size());
                    }
                }
            }
        }
    }

    Error LoadDDS_(
        Image&           _image,
        const MemoryView _mem,
        const StringView _debugHint)
    {
        const std::byte* pData  = _mem.GetPtr();
        const size_t     size   = _mem.GetSize();
        size_t           offset = sizeof(uint32_t) + sizeof(image_detail::DDS_HEADER);   // after header
        if (size < offset)
        {
            JUG_CORE_LOG_ERROR("Failed to load DDS '{}': file is too small.", _debugHint);
            return eGraphicsError::ImageParseFailed;
        }

        image_detail::DDS_HEADER header;
        std::memcpy(&header, pData + sizeof(uint32_t), sizeof(image_detail::DDS_HEADER));

        Optional<eTextureFormat> format         = kNullopt;
        bool                     bPremultiplied = false;
        uint32_t                 arraySize      = 1;
        uint32_t                 depth          = (header.flags & image_detail::kDdsdDepth) ? Max(1u, header.depth) : 1;
        bool                     bCube          = (header.caps2 & image_detail::kDdsCaps2Cubemap) != 0;
        if ((header.ddspf.flags & image_detail::kDdpfFourCC) && header.ddspf.fourCC == image_detail::kDdsFourCcDx10)
        {
            if (size < offset + sizeof(image_detail::DDS_HEADER_DX10))
            {
                JUG_CORE_LOG_ERROR("Failed to load DDS '{}': file is too small.", _debugHint);
                return eGraphicsError::ImageParseFailed;
            }

            image_detail::DDS_HEADER_DX10 dx10;
            std::memcpy(&dx10, pData + offset, sizeof(image_detail::DDS_HEADER_DX10));
            offset += sizeof(image_detail::DDS_HEADER_DX10);

            format         = image_detail::ToTextureFormatFromDXGI(dx10.dxgiFormat);
            bPremultiplied = (dx10.miscFlags2 & image_detail::kDdsAlphaModeMask) == image_detail::kDdsAlphaModePremultiplied;
            arraySize      = Max(1u, dx10.arraySize);
            bCube          = (dx10.miscFlag & image_detail::kDdsMiscTextureCube) != 0;
            depth          = dx10.resourceDimension == image_detail::kDdsDimensionTexture3D ? depth : 1;
        }
        else
        {
            format         = header.ddspf.ToTextureFormat();
            bPremultiplied = false;
        }

        if (!format)
        {
            JUG_CORE_LOG_ERROR("Failed to load DDS '{}': unsupported pixel format.", _debugHint);
            return eGraphicsError::ImageUnsupportedFormat;
        }

        _image.desc.width     = Max(1u, header.width);
        _image.desc.height    = Max(1u, header.height);
        _image.desc.depth     = depth;
        _image.desc.numLayers = arraySize * (bCube ? 6 : 1);
        _image.desc.numMips   = (header.flags & image_detail::kDdsdMipMapCount) ? Max(1u, header.mipMapCount) : 1;
        SetTextureFormat_(_image.desc, *format);
        _image.desc.bCubemap            = bCube;
        _image.desc.bPremultipliedAlpha = bPremultiplied && GetNumChannels(*format) >= image_detail::kNumRgbaChannels;

        const size_t totalSize = image_detail::CalcTotalSize(_image);
        if (size - offset < totalSize)
        {
            JUG_CORE_LOG_ERROR("Failed to load DDS '{}': pixel data is truncated.", _debugHint);
            return eGraphicsError::ImageParseFailed;
        }

        _image.pixels.ResizeUninitialized(totalSize);
        std::memcpy(_image.pixels.GetPtr(), pData + offset, totalSize);
        return kOK;
    }

    Error LoadKTX_(
        Image&                          _image,
        const MemoryView                _mem,
        const Flags<eImageImportOption> _flags,
        const StringView                _debugHint)
    {
        ktxTexture*          pTexture;
        const KTX_error_code result = ktxTexture_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(_mem.GetPtr()), _mem.GetSize(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &pTexture);
        if (result != KTX_SUCCESS)
        {
            JUG_CORE_LOG_ERROR("Failed to load KTX '{}': {}", _debugHint, ktxErrorString(result));
            return eGraphicsError::ImageParseFailed;
        }

        // gpu format
        Optional<eTextureFormat> format         = kNullopt;
        bool                     bPremultiplied = false;
        if (pTexture->classId == ktxTexture2_c)
        {
            ktxTexture2* pTexture2 = reinterpret_cast<ktxTexture2*>(pTexture);

            // Basis 는 GPU 포맷으로 트랜스코딩
            if (ktxTexture2_NeedsTranscoding(pTexture2))
            {
                const ktx_transcode_fmt_e target          = (_flags & eImageImportOption::Decompress) ? KTX_TTF_RGBA32 : KTX_TTF_BC7_RGBA;
                const KTX_error_code      transcodeResult = ktxTexture2_TranscodeBasis(pTexture2, target, 0);
                if (transcodeResult != KTX_SUCCESS)
                {
                    JUG_CORE_LOG_ERROR("Failed to transcode KTX2 '{}': {}", _debugHint, ktxErrorString(transcodeResult));
                    ktxTexture_Destroy(pTexture);
                    return eGraphicsError::ImageDecodeFailed;
                }
            }

            format         = image_detail::ToTextureFormatFromVK(pTexture2->vkFormat);
            bPremultiplied = ktxTexture2_GetPremultipliedAlpha(pTexture2);
        }
        else
        {
            // KTX1 은 alpha 모드 정보 없음
            format         = image_detail::ToTextureFormatFromGL(reinterpret_cast<ktxTexture1*>(pTexture)->glInternalformat);
            bPremultiplied = false;
        }

        if (!format)
        {
            JUG_CORE_LOG_ERROR("Failed to load KTX '{}': unsupported pixel format.", _debugHint);
            ktxTexture_Destroy(pTexture);
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const uint32_t numFaces = pTexture->numFaces;
        _image.desc.width       = pTexture->baseWidth;
        _image.desc.height      = pTexture->baseHeight;
        _image.desc.depth       = pTexture->baseDepth;
        _image.desc.numLayers   = pTexture->numLayers * numFaces;
        _image.desc.numMips     = pTexture->numLevels;
        SetTextureFormat_(_image.desc, *format);
        _image.desc.bCubemap            = pTexture->isCubemap;
        _image.desc.bPremultipliedAlpha = bPremultiplied && GetNumChannels(*format) >= image_detail::kNumRgbaChannels;

        // DDS는 mip index + layer index * mip levels 로 배치되지만,
        // KTX는 layer index + mip index * layer count 로 배치됨.
        _image.pixels.ResizeUninitialized(image_detail::CalcTotalSize(_image));
        std::byte*       pDst     = _image.pixels.GetPtr();
        const std::byte* pSrcBase = reinterpret_cast<const std::byte*>(ktxTexture_GetData(pTexture));
        for (uint32_t layer = 0; layer < _image.desc.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < _image.desc.numMips; ++mip)
            {
                const VEC3U    size       = CalcTextureSize(_image.desc.width, _image.desc.height, _image.desc.depth, mip);
                const uint32_t slicePitch = CalcSlicePitch(*format, size.x, size.y);
                for (uint32_t z = 0; z < size.z; ++z)
                {
                    // cube 면 faceSlice = face, 3D 면 faceSlice = z
                    ktx_size_t           offset;
                    const KTX_error_code offsetResult = ktxTexture_GetImageOffset(pTexture, mip, layer / numFaces, layer % numFaces + z, &offset);
                    if (offsetResult != KTX_SUCCESS)
                    {
                        JUG_CORE_LOG_ERROR("Failed to load KTX '{}': {}", _debugHint, ktxErrorString(offsetResult));
                        ktxTexture_Destroy(pTexture);
                        return eGraphicsError::ImageInvalidData;
                    }

                    std::memcpy(pDst, pSrcBase + offset, slicePitch);
                    pDst += slicePitch;
                }
            }
        }
        ktxTexture_Destroy(pTexture);
        return kOK;
    }

    Error LoadOIIO_(
        Image&                          _image,
        MemoryView                      _mem,
        const Flags<eImageImportOption> _flags,
        const StringView                _debugHint)
    {
        // read memory
        OIIO::Filesystem::IOMemReader reader  = { _mem.GetPtr(), _mem.GetSize() };
        OIIO::Filesystem::IOProxy*    pReader = &reader;

        // make config
        OIIO::ImageSpec config = {};
        config.attribute("oiio:UnassociatedAlpha", 1);
        config.attribute("oiio:ioproxy", ChannelFormat::PTR, &pReader);

        // load
        std::unique_ptr<OIIO::ImageInput> pInput = OIIO::ImageInput::open(GuessOiioName_(_mem), &config, &reader);
        if (!pInput)
        {
            JUG_CORE_LOG_ERROR("Failed to load image '{}': {}", _debugHint, OIIO::geterror());
            return eGraphicsError::ImageParseFailed;
        }

        // 읽기/쓰기 모두 지원하는 포맷만 허용
        const Optional<eImageFileFormat> imgFormat = ToImageFormat_(pInput->format_name());
        if (!imgFormat.HasValue())
        {
            JUG_CORE_LOG_ERROR("Failed to load image '{}': unsupported image format '{}'.", _debugHint, pInput->format_name());
            return eGraphicsError::ImageUnsupportedFormat;
        }

        // calc layers
        uint32_t numLayers = 0;
        while (pInput->seek_subimage(static_cast<int>(numLayers), 0))
        {
            numLayers++;
        }

        // calc mips
        uint32_t numMips = 0;
        while (pInput->seek_subimage(0, static_cast<int>(numMips)))
        {
            numMips++;
        }

        const OIIO::ImageSpec spec = pInput->spec(0, 0);
        if (spec.deep)
        {
            JUG_CORE_LOG_ERROR("Failed to load image '{}': deep images are not supported.", _debugHint);
            return eGraphicsError::ImageUnsupportedFeature;
        }

        // GPU 3D 텍스처는 배열이 없으므로 볼륨은 layer 1 개만 허용
        const bool bGpuCompatible = _flags.Has(eImageImportOption::GpuCompatible);
        if (bGpuCompatible && spec.depth != 1 && numLayers != 1)
        {
            JUG_CORE_LOG_ERROR("Failed to load image '{}': volume image arrays are not GPU compatible.", _debugHint);
            return eGraphicsError::ImageUnsupportedFeature;
        }

        // Image 레이아웃 제약. 모든 layer/mip 은 layer 0 mip 0 과 같은 채널/타입이고 크기는 mip 규칙을 따라야 함.
        for (uint32_t layer = 0; layer < numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < numMips; ++mip)
            {
                const VEC3U           size = CalcTextureSize(static_cast<uint32_t>(spec.width), static_cast<uint32_t>(spec.height), static_cast<uint32_t>(spec.depth), mip);
                const OIIO::ImageSpec dims = pInput->spec_dimensions(static_cast<int>(layer), static_cast<int>(mip));
                if (static_cast<uint32_t>(dims.width) != size.x
                    || static_cast<uint32_t>(dims.height) != size.y
                    || static_cast<uint32_t>(dims.depth) != size.z
                    || dims.deep
                    || dims.nchannels != spec.nchannels
                    || dims.format != spec.format
                    || dims.channelformats != spec.channelformats)
                {
                    JUG_CORE_LOG_ERROR("Failed to load image '{}': layer {} mip {} does not match layer 0 mip 0.", _debugHint, layer, mip);
                    return eGraphicsError::ImageInvalidData;
                }
            }
        }

        // CpuCompatible 이면 double 타입은 float 로 변환해 읽음.
        ChannelFormat channelType = spec.format;
        const uint32_t numChannels = static_cast<uint32_t>(spec.nchannels);
        const bool     bSRGB       = OIIO::equivalent_colorspace(spec.get_string_attribute("oiio:ColorSpace"), "sRGB");
        if (bGpuCompatible && !ToTextureFormat_(channelType, image_detail::kNumRgbaChannels, bSRGB))
        {
            channelType = ChannelFormat::FLOAT;
        }

        _image.desc.width     = static_cast<uint32_t>(spec.width);
        _image.desc.height    = static_cast<uint32_t>(spec.height);
        _image.desc.depth     = static_cast<uint32_t>(spec.depth);
        _image.desc.numLayers = numLayers;
        _image.desc.numMips   = numMips;
        SetPixelFormat_(_image.desc, PixelFormat { channelType, numChannels, bSRGB });
        _image.desc.bCubemap            = false;
        _image.desc.bPremultipliedAlpha = spec.alpha_channel >= 0 && spec.get_int_attribute("oiio:UnassociatedAlpha") == 0;

        // 전부 디코드
        _image.pixels.ResizeUninitialized(image_detail::CalcLayerSize(_image) * numLayers);
        std::byte* pDst = _image.pixels.GetPtr();
        for (uint32_t layer = 0; layer < numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < numMips; ++mip)
            {
                if (!pInput->read_image(static_cast<int>(layer), static_cast<int>(mip), 0, spec.nchannels, channelType, pDst))
                {
                    JUG_CORE_LOG_ERROR("Failed to decode image '{}' layer {} mip {}: {}", _debugHint, layer, mip, pInput->geterror());
                    return eGraphicsError::ImageDecodeFailed;
                }
                pDst += image_detail::CalcMipSize(_image, mip);
            }
        }

        JUG_CORE_LOG_TRACE(
            "Image loaded: '{}' ({}) {}x{}x{}, format = {}, channels = {}, layers = {}, mips = {}",
            _debugHint,
            pInput->format_name(),
            _image.desc.width,
            _image.desc.height,
            _image.desc.depth,
            image_detail::ToLogName(_image.desc.gpuFormat),
            numChannels,
            numLayers,
            numMips);

        return kOK;
    }

    Error Decompress_(
        Image&           _image,
        const StringView _debugHint)
    {
        const Optional<eTextureFormat> gpuFormat = _image.desc.gpuFormat;
        if (!gpuFormat || !IsCompressedFormat(*gpuFormat))   // 압축된 포맷이 아닌경우 리턴
        {
            return kOK;
        }

        const eTextureFormat           srcFormat          = *gpuFormat;
        const Optional<eTextureFormat> decompressedFormat = image_detail::ToDecompressedFormat(srcFormat);
        if (!decompressedFormat)   // 압축 해제 불가.
        {
            JUG_CORE_LOG_ERROR("Failed to decompress image '{}': unsupported compressed format.", _debugHint);
            return eGraphicsError::ImageProcessFailed;
        }

        const eTextureFormat dstFormat = *decompressedFormat;

        const ByteBuffer src = std::move(_image.pixels);
        SetTextureFormat_(_image.desc, dstFormat);
        _image.pixels.ResizeUninitialized(image_detail::CalcTotalSize(_image));

        const std::byte* pSrc = src.GetPtr();
        std::byte*       pDst = _image.pixels.GetPtr();
        for (uint32_t layer = 0; layer < _image.desc.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < _image.desc.numMips; ++mip)
            {
                const VEC3U    size          = CalcTextureSize(_image.desc.width, _image.desc.height, _image.desc.depth, mip);
                const uint32_t srcSlicePitch = CalcSlicePitch(srcFormat, size.x, size.y);
                const uint32_t dstSlicePitch = CalcSlicePitch(dstFormat, size.x, size.y);
                for (uint32_t z = 0; z < size.z; ++z)
                {
                    image_detail::DecompressSlice(srcFormat, { pSrc, srcSlicePitch }, { pDst, dstSlicePitch }, size.x, size.y);
                    pSrc += srcSlicePitch;
                    pDst += dstSlicePitch;
                }
            }
        }
        return kOK;
    }

    void MakeGpuCompatible_(
        Image& _image)
    {
        // GPU 포맷이 있으면 이미 호환
        if (_image.desc.gpuFormat.HasValue())
        {
            return;
        }

        // 여기서 GPU 포맷 없음은 3채널 또는 5채널 이상.
        const PixelFormat srcPixelFormat = image_detail::GetPixelFormat(_image.desc);
        const bool        bSRGB          = _image.desc.format.bSRGB;
        JUG_ASSERT(ToTextureFormat_(srcPixelFormat.formatOrUnknown, image_detail::kNumRgbaChannels, bSRGB).HasValue(), "Unknown channel type has no 4 channel format.\n");

        // 3채널은 alpha 1 로 확장, 5채널 이상은 앞 4채널만 유지.
        const ChannelFormat channelType    = srcPixelFormat.formatOrUnknown;
        const uint32_t       numSrcChannels = srcPixelFormat.numChannels;
        const size_t         channelSize    = channelType.size();
        const size_t         numPixels      = _image.pixels.GetSize() / (numSrcChannels * channelSize);
        ByteBuffer           dst            = { kNoInit, numPixels * image_detail::kNumRgbaChannels * channelSize };
        image_detail::ConvertChannels(_image.pixels, dst, numPixels, numSrcChannels, image_detail::kNumRgbaChannels, channelType);

        _image.pixels = std::move(dst);
        SetPixelFormat_(_image.desc, PixelFormat { channelType, image_detail::kNumRgbaChannels, bSRGB });
    }

    void DropMips_(
        Image& _image)
    {
        if (_image.desc.numMips <= 1)
        {
            return;
        }

        const size_t mip0Size = image_detail::CalcMipSize(_image, 0);
        ByteBuffer   dst      = { kNoInit, mip0Size * _image.desc.numLayers };
        for (uint32_t layer = 0; layer < _image.desc.numLayers; ++layer)
        {
            std::memcpy(dst.GetPtr() + layer * mip0Size, _image.pixels.GetPtr() + image_detail::CalcOffset(_image, layer, 0), mip0Size);
        }

        _image.pixels       = std::move(dst);
        _image.desc.numMips = 1;
    }

    Error PremultiplyAlpha_(
        Image&           _image,
        const StringView _debugHint)
    {
        const Optional<eTextureFormat> format      = _image.desc.gpuFormat;
        const bool                     bCompressed = format && IsCompressedFormat(*format);

        // 이미 premultiplied 거나 alpha 가 없으면 스킵.
        const uint32_t numSrcChannels = format ? GetNumChannels(*format) : _image.desc.format.numChannels;
        if (_image.desc.bPremultipliedAlpha || numSrcChannels < image_detail::kNumRgbaChannels)
        {
            return kOK;
        }

        const Optional<eTextureFormat> workFormat = bCompressed ? image_detail::ToDecompressedFormat(*format) : format;

        // 압축 포맷은 slice 단위로 해제 -> premultiply -> 재압축
        if (bCompressed && (!workFormat || !image_detail::ToCompressInputFormat(*format)))
        {
            JUG_CORE_LOG_ERROR("Failed to premultiply alpha for image '{}': compressed format {} cannot be re-encoded.", _debugHint, image_detail::ToLogName(format));
            return eGraphicsError::ImageProcessFailed;
        }

        // GPU 포맷 없는 채널 구성은 desc 의 채널 타입 사용. packed 포맷은 미지원.
        const PixelFormat pixelFormat = !workFormat ? image_detail::GetPixelFormat(_image.desc) : ToMipPixelFormat_(*workFormat);
        if (!image_detail::IsRawFormat(pixelFormat))
        {
            JUG_CORE_LOG_ERROR("Failed to premultiply alpha for image '{}': unsupported format {}.", _debugHint, image_detail::ToLogName(format));
            return eGraphicsError::ImageProcessFailed;
        }
        const uint32_t       numChannels = pixelFormat.numChannels;
        const ChannelFormat type        = pixelFormat.formatOrUnknown;
        const bool           bSRGB       = _image.desc.format.bSRGB;

        // 비압축은 픽셀 단위 변환이므로 layer/mip 구분 없이 버퍼 전체를 한 번에 변환
        Buffer<float> values = {};
        if (!bCompressed)
        {
            const size_t numValues = _image.pixels.GetSize() / type.size();
            values.ResizeUninitialized(numValues);
            OIIO::convert_pixel_values(type, _image.pixels.GetPtr(), OIIO::TypeFloat, values.GetPtr(), static_cast<int>(numValues));
            MultiplyAlpha_(values, numChannels, bSRGB);
            OIIO::convert_pixel_values(OIIO::TypeFloat, values.GetPtr(), type, _image.pixels.GetPtr(), static_cast<int>(numValues));

            _image.desc.bPremultipliedAlpha = true;
            return kOK;
        }

        ByteBuffer work  = {};
        std::byte* pData = _image.pixels.GetPtr();
        for (uint32_t layer = 0; layer < _image.desc.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < _image.desc.numMips; ++mip)
            {
                const VEC3U    size           = CalcTextureSize(_image.desc.width, _image.desc.height, _image.desc.depth, mip);
                const size_t   numValues      = static_cast<size_t>(size.x) * size.y * numChannels;
                const uint32_t srcSlicePitch  = CalcSlicePitch(*format, size.x, size.y);
                const uint32_t workSlicePitch = CalcSlicePitch(*workFormat, size.x, size.y);
                work.ResizeUninitialized(workSlicePitch);
                values.ResizeUninitialized(numValues);
                for (uint32_t z = 0; z < size.z; ++z)
                {
                    image_detail::DecompressSlice(*format, { pData, srcSlicePitch }, { work.GetPtr(), workSlicePitch }, size.x, size.y);
                    OIIO::convert_pixel_values(type, work.GetPtr(), OIIO::TypeFloat, values.GetPtr(), static_cast<int>(numValues));
                    MultiplyAlpha_(values, numChannels, bSRGB);
                    OIIO::convert_pixel_values(OIIO::TypeFloat, values.GetPtr(), type, work.GetPtr(), static_cast<int>(numValues));
                    image_detail::CompressSlice(*format, { work.GetPtr(), workSlicePitch }, { pData, srcSlicePitch }, size.x, size.y);
                    pData += srcSlicePitch;
                }
            }
        }

        _image.desc.bPremultipliedAlpha = true;
        return kOK;
    }

    Error GenerateMips_(
        Image&           _image,
        const StringView _debugHint)
    {
        // 완전한 mip 체인이면 스킵. 일부만 있으면 mip 0 으로부터 재생성.
        const uint32_t numFullMips = CalcNumMips(_image.desc.width, _image.desc.height, _image.desc.depth);
        if (_image.desc.numMips == numFullMips)
        {
            return kOK;
        }

        const Optional<eTextureFormat> format      = _image.desc.gpuFormat;
        const bool                     bCompressed = format && IsCompressedFormat(*format);
        const Optional<eTextureFormat> workFormat  = bCompressed ? image_detail::ToDecompressedFormat(*format) : format;

        // 압축 포맷은 해제 -> 생성 -> 재압축. mip 0 은 원본 블록 유지.
        if (bCompressed && (!workFormat || !image_detail::ToCompressInputFormat(*format)))
        {
            JUG_CORE_LOG_ERROR("Failed to generate mips for image '{}': compressed format {} cannot be re-encoded.", _debugHint, image_detail::ToLogName(format));
            return eGraphicsError::ImageProcessFailed;
        }

        // GPU 포맷 없는 채널 구성/타입은 desc 의 채널 타입 사용
        const PixelFormat pixelFormat = !workFormat ? image_detail::GetPixelFormat(_image.desc) : ToMipPixelFormat_(*workFormat);
        if (!image_detail::IsRawFormat(pixelFormat))
        {
            JUG_CORE_LOG_ERROR("Failed to generate mips for image '{}': unsupported format {}.", _debugHint, image_detail::ToLogName(format));
            return eGraphicsError::ImageProcessFailed;
        }
        const uint32_t       numChannels = pixelFormat.numChannels;
        const ChannelFormat type        = pixelFormat.formatOrUnknown;

        // 새 레이아웃으로 교체. 기존 mip 은 버리고 mip 0 으로부터 전체 체인 재생성.
        const ByteBuffer src          = std::move(_image.pixels);
        const size_t     srcLayerSize = image_detail::CalcLayerSize(_image);
        const size_t     mip0Size     = image_detail::CalcMipSize(_image, 0);
        _image.desc.numMips           = numFullMips;
        _image.pixels.ResizeUninitialized(image_detail::CalcTotalSize(_image));

        const bool                     bSRGB          = _image.desc.format.bSRGB;
        const Optional<eTextureFormat> compressSource = bCompressed ? image_detail::ToCompressInputFormat(*format) : kNullopt;

        Buffer<float> cur     = {};
        Buffer<float> next    = {};
        Buffer<float> encoded = {};
        ByteBuffer    work    = {};
        ByteBuffer    rgba    = {};
        for (uint32_t layer = 0; layer < _image.desc.numLayers; ++layer)
        {
            // mip 0 은 그대로 복사
            const std::byte* pSrcMip0 = src.GetPtr() + layer * srcLayerSize;
            std::memcpy(_image.pixels.GetPtr() + image_detail::CalcOffset(_image, layer, 0), pSrcMip0, mip0Size);

            // mip 0 -> float
            const VEC3U      mip0Dim       = CalcTextureSize(_image.desc.width, _image.desc.height, _image.desc.depth, 0);
            const size_t     mip0NumPixels = static_cast<size_t>(mip0Dim.x) * mip0Dim.y * mip0Dim.z;
            const std::byte* pWork         = pSrcMip0;
            if (bCompressed)
            {
                const uint32_t srcSlicePitch  = CalcSlicePitch(*format, mip0Dim.x, mip0Dim.y);
                const uint32_t workSlicePitch = CalcSlicePitch(*workFormat, mip0Dim.x, mip0Dim.y);
                work.ResizeUninitialized(static_cast<size_t>(workSlicePitch) * mip0Dim.z);
                for (uint32_t z = 0; z < mip0Dim.z; ++z)
                {
                    image_detail::DecompressSlice(*format, { pSrcMip0 + z * srcSlicePitch, srcSlicePitch }, { work.GetPtr() + z * workSlicePitch, workSlicePitch }, mip0Dim.x, mip0Dim.y);
                }
                pWork = work.GetPtr();
            }

            cur.ResizeUninitialized(mip0NumPixels * numChannels);
            OIIO::convert_pixel_values(type, pWork, OIIO::TypeFloat, cur.GetPtr(), static_cast<int>(cur.GetSize()));
            if (bSRGB)
            {
                ApplyColorSpace_(cur, numChannels, true);
            }

            VEC3U curDim = mip0Dim;
            for (uint32_t mip = 1; mip < _image.desc.numMips; ++mip)
            {
                const VEC3U  mipDim       = CalcTextureSize(_image.desc.width, _image.desc.height, _image.desc.depth, mip);
                const size_t mipNumPixels = static_cast<size_t>(mipDim.x) * mipDim.y * mipDim.z;
                Downsample_(cur, curDim, next, mipDim, numChannels);
                std::swap(cur, next);
                curDim = mipDim;

                // float -> work 포맷 (sRGB 면 되돌린 값으로)
                encoded.ResizeUninitialized(cur.GetSize());
                std::memcpy(encoded.GetPtr(), cur.GetPtr(), cur.GetSize() * sizeof(float));
                if (bSRGB)
                {
                    ApplyColorSpace_(encoded, numChannels, false);
                }
                work.ResizeUninitialized(mipNumPixels * numChannels * type.size());
                OIIO::convert_pixel_values(OIIO::TypeFloat, encoded.GetPtr(), type, work.GetPtr(), static_cast<int>(encoded.GetSize()));

                std::byte* pDst = _image.pixels.GetPtr() + image_detail::CalcOffset(_image, layer, mip);
                if (!bCompressed)
                {
                    std::memcpy(pDst, work.GetPtr(), work.GetSize());
                    continue;
                }

                // 재압축. 압축 입력은 RGBA 이므로 R/RG(BC4/BC5) 는 채널 확장.
                const std::byte* pRGBA = work.GetPtr();
                if (numChannels != image_detail::kNumRgbaChannels)
                {
                    rgba.ResizeUninitialized(mipNumPixels * image_detail::kNumRgbaChannels * type.size());
                    image_detail::ConvertChannels(work, rgba, mipNumPixels, numChannels, image_detail::kNumRgbaChannels, type);
                    pRGBA = rgba.GetPtr();
                }

                const uint32_t rgbaSlicePitch = CalcSlicePitch(*compressSource, mipDim.x, mipDim.y);
                const uint32_t dstSlicePitch  = CalcSlicePitch(*format, mipDim.x, mipDim.y);
                for (uint32_t z = 0; z < mipDim.z; ++z)
                {
                    image_detail::CompressSlice(*format, { pRGBA + z * rgbaSlicePitch, rgbaSlicePitch }, { pDst + z * dstSlicePitch, dstSlicePitch }, mipDim.x, mipDim.y);
                }
            }
        }
        return kOK;
    }

    Error PostProcess_(
        Image&                          _image,
        const Flags<eImageImportOption> _flags,
        const StringView                _debugHint)
    {
        JUG_ASSERT(!((_flags & eImageImportOption::DropMips) && (_flags & eImageImportOption::GenerateMips)), "DropMips and GenerateMips cannot be used together.\n");

        if (_flags & eImageImportOption::Decompress)
        {
            JUG_RETURN_IF_ERROR(Decompress_(_image, _debugHint));
        }

        if (_flags & eImageImportOption::GpuCompatible)
        {
            MakeGpuCompatible_(_image);
        }

        if (_flags & eImageImportOption::DropMips)
        {
            DropMips_(_image);
        }

        // mip 생성 전에 premultiply
        if (_flags & eImageImportOption::PremultipliedAlpha)
        {
            JUG_RETURN_IF_ERROR(PremultiplyAlpha_(_image, _debugHint));
        }

        if (_flags & eImageImportOption::GenerateMips)
        {
            JUG_RETURN_IF_ERROR(GenerateMips_(_image, _debugHint));
        }

        return kOK;
    }

    Error Load_(
        Image&                          _image,
        const MemoryView                _mem,
        const Flags<eImageImportOption> _flags,
        const StringView                _debugHint)
    {
        JUG_ASSERT(!_mem.IsEmpty(), "Image data is empty.\n");

        if (IsDDS_(_mem))
        {
            JUG_RETURN_IF_ERROR(LoadDDS_(_image, _mem, _debugHint));
        }
        else if (IsKTX_(_mem))
        {
            JUG_RETURN_IF_ERROR(LoadKTX_(_image, _mem, _flags, _debugHint));
        }
        else
        {
            JUG_RETURN_IF_ERROR(LoadOIIO_(_image, _mem, _flags, _debugHint));
        }

        return PostProcess_(_image, _flags, _debugHint);
    }

    Error LoadFromFile_(
        Image&                          _image,
        const FilePath&                 _path,
        const Flags<eImageImportOption> _flags)
    {
        Result<FileReader> reader = FileReader::Open(_path);
        JUG_RETURN_IF_ERROR(reader);

        const Result<size_t> fileSize = reader->GetSize();
        JUG_RETURN_IF_ERROR(fileSize);

        ByteBuffer buf = { kNoInit, *fileSize };
        JUG_RETURN_IF_ERROR(reader->Read(buf));
        return Load_(_image, buf, _flags, _path.string());
    }
}   // namespace

Result<Image> ImportImage(
    const MemoryView                _mem,
    const Flags<eImageImportOption> _flags)
{
    Image image = {};
    JUG_RETURN_IF_ERROR(Load_(image, _mem, _flags, "<memory>"));
    return std::move(image);
}

Result<Image> ImportImageFromFile(
    const FilePath&                 _path,
    const Flags<eImageImportOption> _flags)
{
    Image image = {};
    JUG_RETURN_IF_ERROR(LoadFromFile_(image, _path, _flags));
    return std::move(image);
}

}   // namespace jug
