#include "pch.h"
#include "ImageExporter.h"

#include <tinyexr.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "Error.h"
#include "ImageDetail.h"

namespace jug
{

namespace
{
    constexpr uint32_t kMaxChannels = 4;   // stb / EXR 슬롯이 다룰 수 있는 최대 채널 수

    // stb_image_write 출력 콜백
    void AppendToBuffer_(
        void*     _pContext,
        void*     _pData,
        const int _size)
    {
        ByteBuffer&  buffer  = *static_cast<ByteBuffer*>(_pContext);
        const size_t oldSize = buffer.GetSize();
        const size_t newSize = oldSize + static_cast<size_t>(_size);
        if (newSize > buffer.GetCapacity())
        {
            buffer.Reserve(Max(newSize, buffer.GetCapacity() * 2));   // 콜백이 잘게 자주 불림
        }
        buffer.ResizeUninitialized(newSize);
        std::memcpy(buffer.GetPtr() + oldSize, _pData, static_cast<size_t>(_size));
    }

    // 단일 이미지 포맷 출력 대상 sub image
    [[nodiscard]] Result<SubImage> GetSingleSubImage_(
        const Image&             _image,
        const ImageExportConfig& _config)
    {
        JUG_ASSERT(_config.layer < _image.desc.numLayers, "Export layer index out of bounds.\n");
        JUG_ASSERT(_config.mip < _image.desc.numMips, "Export mip index out of bounds.\n");

        if (_image.desc.depth > 1)
        {
            JUG_CORE_LOG_ERROR("ExportImage: a volume image can not be written to a single image format");
            return eGraphicsError::ImageUnsupportedFeature;
        }
        return _image.GetSubImage(_config.layer, _config.mip);
    }

    [[nodiscard]] bool IsBgra8_(
        const ImageDesc& _desc)
    {
        return _desc.gpuFormat == eTextureFormat::BGRA8_UNorm || _desc.gpuFormat == eTextureFormat::BGRA8_UNorm_SRGB;
    }

    // PNG / JPEG / BMP / TGA
    [[nodiscard]] Error EncodeLdr_(
        const Image&             _image,
        const SubImage&          _sub,
        const ImageExportConfig& _config,
        ByteBuffer&              _out)
    {
        const ImageDesc& desc = _image.desc;
        if (desc.channelFormat != eChannelFormat::UNorm8)
        {
            JUG_CORE_LOG_ERROR("ExportImage: only 8 bit images can be written to this format");
            return eGraphicsError::ImageUnsupportedFormat;
        }

        // 파일 포맷은 RGBA 순서
        ByteBuffer       swizzled = {};
        const std::byte* pPixels  = _sub.pixels.GetPtr();
        if (IsBgra8_(desc))
        {
            swizzled.ResizeUninitialized(_sub.pixels.GetSize());
            std::byte* pDst = swizzled.GetPtr();
            for (size_t i = 0; i < _sub.pixels.GetSize(); i += 4)
            {
                pDst[i + 0] = pPixels[i + 2];
                pDst[i + 1] = pPixels[i + 1];
                pDst[i + 2] = pPixels[i + 0];
                pDst[i + 3] = pPixels[i + 3];
            }
            pPixels = swizzled.GetPtr();
        }

        const int width       = static_cast<int>(_sub.width);
        const int height      = static_cast<int>(_sub.height);
        const int numChannels = static_cast<int>(desc.numChannels);

        int result = 0;
        switch (_config.format)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eImageFileFormat::PNG:
                result = stbi_write_png_to_func(AppendToBuffer_, &_out, width, height, numChannels, pPixels, static_cast<int>(_sub.rowPitch));
                break;

            case eImageFileFormat::JPEG:
                JUG_ASSERT(_config.quality >= 1 && _config.quality <= 100, "JPEG quality must be in [1, 100].\n");
                result = stbi_write_jpg_to_func(AppendToBuffer_, &_out, width, height, numChannels, pPixels, static_cast<int>(_config.quality));
                break;

            case eImageFileFormat::BMP:
                result = stbi_write_bmp_to_func(AppendToBuffer_, &_out, width, height, numChannels, pPixels);
                break;

            case eImageFileFormat::TGA:
                result = stbi_write_tga_to_func(AppendToBuffer_, &_out, width, height, numChannels, pPixels);
                break;

            default:
                JUG_ASSERT(false, "Unsupported LDR export format.\n");
                break;
        }

        if (!result)
        {
            JUG_CORE_LOG_ERROR("ExportImage: failed to encode the image");
            return eGraphicsError::ImageEncodeFailed;
        }
        return kOK;
    }

    [[nodiscard]] Error EncodeHdr_(
        const Image&    _image,
        const SubImage& _sub,
        ByteBuffer&     _out)
    {
        const ImageDesc& desc = _image.desc;
        if (desc.channelFormat != eChannelFormat::Float)
        {
            JUG_CORE_LOG_ERROR("ExportImage: only float images can be written to HDR");
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const float* pPixels = reinterpret_cast<const float*>(_sub.pixels.GetPtr());
        if (!stbi_write_hdr_to_func(AppendToBuffer_, &_out, static_cast<int>(_sub.width), static_cast<int>(_sub.height), static_cast<int>(desc.numChannels), pPixels))
        {
            JUG_CORE_LOG_ERROR("ExportImage: failed to encode the image");
            return eGraphicsError::ImageEncodeFailed;
        }
        return kOK;
    }

    [[nodiscard]] Error EncodeExr_(
        const Image&    _image,
        const SubImage& _sub,
        ByteBuffer&     _out)
    {
        const ImageDesc& desc = _image.desc;
        if (desc.channelFormat != eChannelFormat::Half && desc.channelFormat != eChannelFormat::Float)
        {
            JUG_CORE_LOG_ERROR("ExportImage: only half / float images can be written to EXR");
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const uint32_t numChannels = desc.numChannels;
        const uint32_t channelSize = GetChannelSize(desc.channelFormat);
        const size_t   numPixels   = static_cast<size_t>(_sub.width) * _sub.height;
        const int      pixelType   = desc.channelFormat == eChannelFormat::Half ? TINYEXR_PIXELTYPE_HALF : TINYEXR_PIXELTYPE_FLOAT;

        // EXR 은 planar 채널 요구. (A)BGR 순으로 기록 (이름 오름차순 규칙)
        ByteBuffer planes = { kNoInit, numChannels * numPixels * channelSize };

        ARRAY<EXRChannelInfo, 4>        channels      = {};
        ARRAY<int, 4>                   pixelTypes    = {};
        ARRAY<unsigned char*, 4>        planePointers = {};
        constexpr ARRAY<const char*, 4> kSlotNames    = { "R", "G", "B", "A" };

        const std::byte* pSrc = _sub.pixels.GetPtr();
        for (uint32_t c = 0; c < numChannels; ++c)
        {
            const uint32_t slot = numChannels - 1 - c;
            strcpy_s(channels[c].name, numChannels == 1 ? "Y" : kSlotNames[slot]);
            pixelTypes[c] = pixelType;

            std::byte* pPlane = planes.GetPtr() + c * numPixels * channelSize;
            for (size_t i = 0; i < numPixels; ++i)
            {
                std::memcpy(pPlane + i * channelSize, pSrc + (i * numChannels + slot) * channelSize, channelSize);
            }
            planePointers[c] = reinterpret_cast<unsigned char*>(pPlane);
        }

        EXRHeader header;
        InitEXRHeader(&header);
        header.compression_type      = (_sub.width < 16 && _sub.height < 16) ? TINYEXR_COMPRESSIONTYPE_NONE : TINYEXR_COMPRESSIONTYPE_ZIP;
        header.num_channels          = static_cast<int>(numChannels);
        header.channels              = channels.data();
        header.pixel_types           = pixelTypes.data();
        header.requested_pixel_types = pixelTypes.data();   // 입력 타입 그대로

        EXRImage image;
        InitEXRImage(&image);
        image.num_channels = static_cast<int>(numChannels);
        image.images       = planePointers.data();
        image.width        = static_cast<int>(_sub.width);
        image.height       = static_cast<int>(_sub.height);

        unsigned char* pMemory = nullptr;
        const char*    pError  = nullptr;
        const size_t   size    = SaveEXRImageToMemory(&image, &header, &pMemory, &pError);
        if (size == 0)
        {
            JUG_CORE_LOG_ERROR("ExportImage: failed to encode the EXR - {}", pError ? pError : "unknown error");
            FreeEXRErrorMessage(pError);
            return eGraphicsError::ImageEncodeFailed;
        }

        _out.ResizeUninitialized(size);
        std::memcpy(_out.GetPtr(), pMemory, size);
        std::free(pMemory);
        return kOK;
    }

    [[nodiscard]] Error EncodeDds_(
        const Image& _image,
        ByteBuffer&  _out)
    {
        const ImageDesc& desc = _image.desc;

        const image_detail::ImageFormatEntry* pFormatOrNull = nullptr;
        if (desc.gpuFormat.HasValue())
        {
            pFormatOrNull = image_detail::FindFormatByGpuFormatOrNull(desc.gpuFormat.GetValue());
        }
        else if (desc.channelFormat == eChannelFormat::Float && desc.numChannels == 3)
        {
            pFormatOrNull = image_detail::FindFormatByDxgiOrNull(image_detail::kDxgiFormatR32G32B32Float);
        }
        if (!pFormatOrNull)
        {
            JUG_CORE_LOG_ERROR("ExportImage: the image format can not be written to DDS");
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const bool bCompressed = desc.channelFormat == eChannelFormat::Compressed;
        const bool bVolume     = desc.depth > 1;
        const bool bComplex    = desc.numMips > 1 || desc.numLayers > 1 || bVolume;

        image_detail::DDS_HEADER header = {};
        header.size                     = sizeof(header);
        header.flags                    = image_detail::kDdsdCaps | image_detail::kDdsdHeight | image_detail::kDdsdWidth | image_detail::kDdsdPixelFormat | image_detail::kDdsdMipMapCount | (bCompressed ? image_detail::kDdsdLinearSize : image_detail::kDdsdPitch) | (bVolume ? image_detail::kDdsdDepth : 0);
        header.height                   = desc.height;
        header.width                    = desc.width;
        header.pitchOrLinearSize        = static_cast<uint32_t>(bCompressed ? CalcImageSlicePitch(desc, desc.width, desc.height) : CalcImageRowPitch(desc, desc.width));
        header.depth                    = desc.depth;
        header.mipMapCount              = desc.numMips;
        header.ddspf.size               = sizeof(header.ddspf);
        header.ddspf.flags              = image_detail::kDdpfFourCc;
        header.ddspf.fourCC             = image_detail::kDdsFourCcDx10;
        header.caps                     = image_detail::kDdsCapsTexture | (bComplex ? image_detail::kDdsCapsComplex : 0) | (desc.numMips > 1 ? image_detail::kDdsCapsMipMap : 0);
        header.caps2                    = (desc.bCubemap ? (image_detail::kDdsCaps2Cubemap | image_detail::kDdsCaps2AllFaces) : 0) | (bVolume ? image_detail::kDdsCaps2Volume : 0);

        image_detail::DDS_HEADER_DX10 dx10 = {};
        dx10.dxgiFormat                    = pFormatOrNull->dxgiFormat;
        dx10.resourceDimension             = bVolume ? image_detail::kDdsDimensionTexture3D : image_detail::kDdsDimensionTexture2D;
        dx10.miscFlag                      = desc.bCubemap ? image_detail::kDdsMiscTextureCube : 0;
        dx10.arraySize                     = desc.bCubemap ? desc.numLayers / 6 : desc.numLayers;   // 큐브 개수

        // 레이어, mip 전부 그대로
        const size_t pixelsSize = CalcImageSize(desc);
        JUG_ASSERT(_image.pixels.GetSize() >= pixelsSize, "Image pixels are smaller than its desc.\n");

        constexpr uint32_t kMagic = image_detail::kDdsMagic;

        _out.ResizeUninitialized(sizeof(kMagic) + sizeof(header) + sizeof(dx10) + pixelsSize);
        std::byte* pDst = _out.GetPtr();
        std::memcpy(pDst, &kMagic, sizeof(kMagic));
        pDst += sizeof(kMagic);
        std::memcpy(pDst, &header, sizeof(header));
        pDst += sizeof(header);
        std::memcpy(pDst, &dx10, sizeof(dx10));
        pDst += sizeof(dx10);
        std::memcpy(pDst, _image.pixels.GetPtr(), pixelsSize);
        return kOK;
    }
}   // namespace

Result<ByteBuffer> ExportImage(
    const Image&             _image,
    const ImageExportConfig& _config)
{
    JUG_ASSERT(_config.format != eImageFileFormat::GIF, "GIF is import only.\n");
    JUG_ASSERT(_image.pixels.GetSize() >= CalcImageSize(_image.desc), "Image pixels are smaller than its desc.\n");
    JUG_ASSERT(_image.desc.numChannels <= kMaxChannels, "Image can not have more than 4 channels.\n");

    ByteBuffer out = {};
    if (_config.format == eImageFileFormat::DDS)
    {
        JUG_RETURN_IF_ERROR(EncodeDds_(_image, out));
        return std::move(out);
    }

    const Result<SubImage> sub = GetSingleSubImage_(_image, _config);
    JUG_RETURN_IF_ERROR(sub);

    Error error = kOK;
    switch (_config.format)   // NOLINT(clang-diagnostic-switch-enum)
    {
        case eImageFileFormat::PNG:
        case eImageFileFormat::JPEG:
        case eImageFileFormat::BMP:
        case eImageFileFormat::TGA: error = EncodeLdr_(_image, sub.GetValue(), _config, out); break;
        case eImageFileFormat::HDR: error = EncodeHdr_(_image, sub.GetValue(), out); break;
        case eImageFileFormat::EXR: error = EncodeExr_(_image, sub.GetValue(), out); break;
        default: JUG_ASSERT(false, "Unsupported export format.\n"); break;
    }
    JUG_RETURN_IF_ERROR(error);
    return std::move(out);
}

Error ExportImageToFile(
    const FilePath&          _path,
    const Image&             _image,
    const ImageExportConfig& _config)
{
    const Result<ByteBuffer> encoded = ExportImage(_image, _config);
    JUG_RETURN_IF_ERROR(encoded);

    return WriteFile(_path, MemoryView { encoded.GetValue().GetPtr(), encoded.GetValue().GetSize() });
}

}   // namespace jug
