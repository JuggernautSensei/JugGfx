#include "pch.h"
#include "ImageExporter.h"

#include <ktx.h>
#include <thread>
#include <OpenImageIO/imageio.h>

#include "Error.h"
#include "ImageDetail.h"
#include "Texture.h"

namespace jug
{

namespace
{
    // ===========================================
    //  Export
    // ===========================================

    struct KtxTextureDeleter
    {
        void operator()(
            ktxTexture* _pTexture) const
        {
            ktxTexture_Destroy(_pTexture);
        }
    };

    constexpr size_t kMinOutputCapacity    = 4096;
    constexpr size_t kKtxHeaderReserveSize = 4096;   // KTX 헤더/메타데이터 여유분

    // 출력 버퍼. GetSize() 는 실제 기록된 크기, GetCapacity() 는 용량 (배가 성장).
    void WriteOutput_(
        ByteBuffer&  _out,
        const size_t _offset,
        const void*  _pSrc,
        const size_t _size)
    {
        if (_size == 0)
        {
            return;
        }

        const size_t end = _offset + _size;
        if (end > _out.GetCapacity())
        {
            _out.Reserve(Max(end, _out.GetCapacity() * 2, kMinOutputCapacity));
        }

        if (end > _out.GetSize())
        {
            const size_t oldSize = _out.GetSize();
            _out.ResizeUninitialized(end);
            if (_offset > oldSize)
            {
                std::memset(_out.GetPtr() + oldSize, 0, _offset - oldSize);   // 건너뛴 구간은 0
            }
        }
        std::memcpy(_out.GetPtr() + _offset, _pSrc, _size);
    }

    // 기록된 범위 안에서 최대 _size 만큼 읽음. 읽은 바이트 수 반환.
    [[nodiscard]] size_t ReadOutput_(
        const ByteBuffer& _out,
        const size_t      _offset,
        void*             _pDst,
        const size_t      _size)
    {
        if (_offset >= _out.GetSize())
        {
            return 0;
        }

        const size_t readSize = Min(_size, _out.GetSize() - _offset);
        std::memcpy(_pDst, _out.GetPtr() + _offset, readSize);
        return readSize;
    }

    class ByteBufferOutput final : public OIIO::Filesystem::IOProxy
    {
    public:
        ByteBufferOutput()
            : IOProxy("", Write)
        {
        }

        const char* proxytype() const override
        {
            return "bytebufferoutput";
        }

        size_t read(
            void*        _pBuf,
            const size_t _size) override
        {
            const size_t readSize = pread(_pBuf, _size, m_pos);
            m_pos += static_cast<int64_t>(readSize);
            return readSize;
        }

        size_t write(
            const void*  _pBuf,
            const size_t _size) override
        {
            const size_t writeSize = pwrite(_pBuf, _size, m_pos);
            m_pos += static_cast<int64_t>(writeSize);
            return writeSize;
        }

        size_t pread(
            void*         _pBuf,
            const size_t  _size,
            const int64_t _offset) override
        {
            return ReadOutput_(m_output, static_cast<size_t>(_offset), _pBuf, _size);
        }

        size_t pwrite(
            const void*   _pBuf,
            const size_t  _size,
            const int64_t _offset) override
        {
            WriteOutput_(m_output, static_cast<size_t>(_offset), _pBuf, _size);
            return _size;
        }

        size_t size() const override
        {
            return m_output.GetSize();
        }

        // 기록된 데이터를 넘겨줌. close 이후 호출.
        [[nodiscard]] ByteBuffer Take()
        {
            return std::move(m_output);
        }

    private:
        ByteBuffer m_output = {};
    };

    // libktx 커스텀 스트림이 가리키는 상태
    struct KtxStreamState
    {
        ByteBuffer output   = {};
        size_t     position = 0;
    };

    [[nodiscard]] KtxStreamState& GetKtxStreamState_(
        ktxStream* _pStream)   // NOLINT
    {
        return *static_cast<KtxStreamState*>(_pStream->data.custom_ptr.address);
    }

    // 쓰기 전용 스트림. 읽기/skip 은 지원하지 않음.
    KTX_error_code KtxStreamRead_(
        ktxStream*,
        void*,
        const ktx_size_t)
    {
        return KTX_INVALID_OPERATION;
    }

    KTX_error_code KtxStreamSkip_(
        ktxStream*,
        const ktx_size_t)
    {
        return KTX_INVALID_OPERATION;
    }

    KTX_error_code KtxStreamWrite_(
        ktxStream*       _pStream,
        const void*      _pSrc,
        const ktx_size_t _size,
        const ktx_size_t _count)
    {
        KtxStreamState& state     = GetKtxStreamState_(_pStream);
        const size_t    totalSize = _size * _count;
        WriteOutput_(state.output, state.position, _pSrc, totalSize);
        state.position += totalSize;
        return KTX_SUCCESS;
    }

    KTX_error_code KtxStreamGetPos_(
        ktxStream*       _pStream,
        ktx_off_t* const _pOffset)
    {
        *_pOffset = GetKtxStreamState_(_pStream).position;
        return KTX_SUCCESS;
    }

    KTX_error_code KtxStreamSetPos_(
        ktxStream*      _pStream,
        const ktx_off_t _offset)
    {
        GetKtxStreamState_(_pStream).position = static_cast<size_t>(_offset);
        return KTX_SUCCESS;
    }

    KTX_error_code KtxStreamGetSize_(
        ktxStream*        _pStream,
        ktx_size_t* const _pSize)
    {
        *_pSize = GetKtxStreamState_(_pStream).output.GetSize();
        return KTX_SUCCESS;
    }

    void KtxStreamDestruct_(
        ktxStream*)
    {
    }

    [[nodiscard]] const char* ToOiioFormatNameOrNull_(
        const eImageFileFormat _format)
    {
        switch (_format)
        {
            case eImageFileFormat::PNG: return "png";
            case eImageFileFormat::JPEG: return "jpeg";
            case eImageFileFormat::BMP: return "bmp";
            case eImageFileFormat::TGA: return "targa";
            case eImageFileFormat::HDR: return "hdr";
            case eImageFileFormat::EXR: return "openexr";
            case eImageFileFormat::TIFF: return "tiff";
            case eImageFileFormat::GIF: return "gif";
            case eImageFileFormat::PNM: return "pnm";
            case eImageFileFormat::SGI: return "sgi";
            case eImageFileFormat::RLA: return "rla";
            case eImageFileFormat::IFF: return "iff";
            case eImageFileFormat::DPX: return "dpx";
            default: return nullptr;
        }
    }

    // 압축 옵션 -> BC 포맷. BC 옵션이 아니면 값 없음.
    [[nodiscard]] Optional<eTextureFormat> ToBcFormat_(
        const eImageCompression _compression,
        const bool              _bSRGB)
    {
        switch (_compression)
        {
            case eImageCompression::BC1: return _bSRGB ? eTextureFormat::BC1_UNorm_SRGB : eTextureFormat::BC1_UNorm;
            case eImageCompression::BC3: return _bSRGB ? eTextureFormat::BC3_UNorm_SRGB : eTextureFormat::BC3_UNorm;
            case eImageCompression::BC4: return eTextureFormat::BC4_UNorm;
            case eImageCompression::BC5: return eTextureFormat::BC5_UNorm;
            case eImageCompression::BC6H: return eTextureFormat::BC6H_UFloat;
            case eImageCompression::BC7: return _bSRGB ? eTextureFormat::BC7_UNorm_SRGB : eTextureFormat::BC7_UNorm;
            default: return kNullopt;
        }
    }

    [[nodiscard]] size_t CalcMipSize(
        const ImageDesc&     _desc,
        const eTextureFormat _format,
        const uint32_t       _mip)
    {
        const VEC3U size = CalcTextureSize(_desc.width, _desc.height, _desc.depth, _mip);
        return static_cast<size_t>(CalcSlicePitch(_format, size.x, size.y)) * size.z;
    }

    [[nodiscard]] size_t CalcTotalSize(
        const ImageDesc&     _desc,
        const eTextureFormat _format)
    {
        size_t layerSize = 0;
        for (uint32_t mip = 0; mip < _desc.numMips; ++mip)
        {
            layerSize += CalcMipSize(_desc, _format, mip);
        }
        return layerSize * _desc.numLayers;
    }

    // 원본 2D slice 크기. 텍스처 포맷이 없으면 픽셀 포맷 기준.
    [[nodiscard]] uint32_t CalcSourceSlicePitch_(
        const ImageDesc& _desc,
        const uint32_t   _width,
        const uint32_t   _height)
    {
        const Optional<eTextureFormat> textureFormat = _desc.gpuFormat;
        if (!textureFormat)
        {
            const PixelFormat pixelFormat = image_detail::GetPixelFormat(_desc);
            return _width * _height * pixelFormat.numChannels * static_cast<uint32_t>(pixelFormat.formatOrUnknown.size());
        }
        return CalcSlicePitch(*textureFormat, _width, _height);
    }

    [[nodiscard]] size_t CalcSourceMipSize_(
        const ImageDesc& _desc,
        const uint32_t   _mip)
    {
        const VEC3U size = CalcTextureSize(_desc.width, _desc.height, _desc.depth, _mip);
        return static_cast<size_t>(CalcSourceSlicePitch_(_desc, size.x, size.y)) * size.z;
    }

    [[nodiscard]] size_t CalcSourceTotalSize_(
        const ImageDesc& _desc)
    {
        size_t layerSize = 0;
        for (uint32_t mip = 0; mip < _desc.numMips; ++mip)
        {
            layerSize += CalcSourceMipSize_(_desc, mip);
        }
        return layerSize * _desc.numLayers;
    }

    [[nodiscard]] MemoryView GetSourceMipPixels_(
        const MemoryView _pixels,
        const ImageDesc& _desc,
        const uint32_t   _layer,
        const uint32_t   _mip)
    {
        size_t layerSize = 0;
        size_t mipOffset = 0;
        for (uint32_t mip = 0; mip < _desc.numMips; ++mip)
        {
            if (mip == _mip)
            {
                mipOffset = layerSize;
            }
            layerSize += CalcSourceMipSize_(_desc, mip);
        }
        return { _pixels.GetPtr() + _layer * layerSize + mipOffset, CalcSourceMipSize_(_desc, _mip) };
    }

    [[nodiscard]] bool IsBgraFormat_(
        const eTextureFormat _format)
    {
        return _format == eTextureFormat::BGRA8_UNorm || _format == eTextureFormat::BGRA8_UNorm_SRGB;
    }

    // 압축 해제 / BGRA 교환이 필요한 포맷인가
    [[nodiscard]] bool NeedsDecode_(
        const eTextureFormat _format)
    {
        return IsCompressedFormat(_format) || IsBgraFormat_(_format);
    }

    // 디코드 후 픽셀 포맷. 디코드가 필요 없으면 _pixelFormat 그대로. 디코드 불가면 unknown.
    [[nodiscard]] PixelFormat DecodedPixelFormat_(
        const Optional<eTextureFormat> _format,
        const PixelFormat              _pixelFormat)   // 비압축 원본의 픽셀 포맷 (텍스처 포맷 없는 경우 포함)
    {
        if (!_format)
        {
            return _pixelFormat;
        }

        if (IsCompressedFormat(*_format))
        {
            const Optional<eTextureFormat> decompressedFormat = image_detail::ToDecompressedFormat(*_format);
            if (!decompressedFormat)
            {
                return {};
            }
            return image_detail::ToPixelFormatOrUnknown(*decompressedFormat);
        }

        if (IsBgraFormat_(*_format))
        {
            return PixelFormat { ChannelFormat::UINT8, image_detail::kNumRgbaChannels };
        }
        return _pixelFormat;
    }

    // 2D slice 를 RGBA 순 비압축 픽셀로 _pDst 에 디코드. 압축이면 해제, BGRA 면 R/B 교환. NeedsDecode_ 인 포맷만 호출.
    // _dstSize 는 DecodedPixelFormat_ 기준 slice 크기.
    void DecodeSlice_(
        const eTextureFormat _format,
        const MemoryView     _src,
        const uint32_t       _width,
        const uint32_t       _height,
        std::byte*           _pDst,
        const size_t         _dstSize)
    {
        if (IsCompressedFormat(_format))
        {
            image_detail::DecompressSlice(_format, { _src.GetPtr(), CalcSlicePitch(_format, _width, _height) }, { _pDst, _dstSize }, _width, _height);
            return;
        }

        const size_t     numPixels = static_cast<size_t>(_width) * _height;
        const std::byte* pSrc      = _src.GetPtr();
        for (size_t i = 0; i < numPixels; ++i)
        {
            const size_t offset = i * image_detail::kNumRgbaChannels;
            _pDst[offset + 0]   = pSrc[offset + 2];
            _pDst[offset + 1]   = pSrc[offset + 1];
            _pDst[offset + 2]   = pSrc[offset + 0];
            _pDst[offset + 3]   = pSrc[offset + 3];
        }
    }

    // 변환 대상이 정해지면 불변인 변환 계획. [디코드] -> [채널 확장] -> [타입 변환] 중 필요한 단계만 수행.
    struct ConvertPlan
    {
        Optional<eTextureFormat> srcFormat      = {};
        eTextureFormat           targetFormat   = {};   // BC 포맷 또는 RGBA8_UNorm(_SRGB) / RGBA16_Float
        eTextureFormat           rgbaFormat     = {};   // 압축 전 RGBA 포맷. 압축 대상이 아니면 targetFormat 과 같음.
        PixelFormat              srcPixelFormat = {};   // 디코드 후
        PixelFormat              dstPixelFormat = {};   // rgbaFormat 의 픽셀 포맷
        bool                     bCompress      = false;
        bool                     bDecode        = false;
        bool                     bExpand        = false;
        bool                     bConvert       = false;
    };

    // 변환 중 재사용하는 임시 버퍼
    struct ConvertScratch
    {
        ByteBuffer decoded  = {};
        ByteBuffer expanded = {};
        ByteBuffer rgba     = {};   // 압축 입력
    };

    // 컨테이너에 쓸 픽셀 정보. bConvert 가 false 면 원본 픽셀을 그대로 사용, true 면 plan 대로 변환하며 기록. format 은 컨테이너에 기록될 포맷.
    struct ExportSource
    {
        MemoryView               pixels   = {};
        const ImageDesc*         pDesc    = nullptr;
        Optional<eTextureFormat> format   = {};
        bool                     bConvert = false;
        ConvertPlan              plan     = {};
    };

    [[nodiscard]] Result<ConvertPlan> BuildConvertPlan_(
        const ImageDesc&     _desc,
        const eTextureFormat _targetFormat)
    {
        ConvertPlan plan  = {};
        plan.srcFormat    = _desc.gpuFormat;
        plan.targetFormat = _targetFormat;
        plan.bCompress    = IsCompressedFormat(_targetFormat);

        const Optional<eTextureFormat> rgbaFormat = plan.bCompress ? image_detail::ToCompressInputFormat(_targetFormat) : Optional<eTextureFormat> { _targetFormat };
        if (!rgbaFormat)
        {
            JUG_CORE_LOG_ERROR("Failed to export image: unsupported target format {}.", NameOf(_targetFormat));
            return eGraphicsError::ImageUnsupportedFormat;
        }
        plan.rgbaFormat = *rgbaFormat;

        plan.srcPixelFormat = DecodedPixelFormat_(plan.srcFormat, image_detail::GetPixelFormat(_desc));
        plan.dstPixelFormat = image_detail::ToPixelFormatOrUnknown(plan.rgbaFormat);
        if (!image_detail::IsRawFormat(plan.srcPixelFormat) || !image_detail::IsRawFormat(plan.dstPixelFormat))
        {
            JUG_CORE_LOG_ERROR("Failed to export image: cannot convert format {} to {}.", image_detail::ToLogName(plan.srcFormat), NameOf(plan.rgbaFormat));
            return eGraphicsError::ImageUnsupportedFormat;
        }

        plan.bDecode  = plan.srcFormat && NeedsDecode_(*plan.srcFormat);
        plan.bExpand  = plan.srcPixelFormat.numChannels != image_detail::kNumRgbaChannels;
        plan.bConvert = plan.srcPixelFormat.formatOrUnknown != plan.dstPixelFormat.formatOrUnknown;
        return plan;
    }

    // 2D slice 하나를 RGBA 비압축 포맷(_plan.rgbaFormat)으로 변환해 _pDst 에 기록. 마지막 단계가 _pDst 에 직접 쓰고, 앞 단계만 임시 버퍼를 사용.
    // 필요한 단계가 없으면 단순 복사.
    [[nodiscard]] bool ToRGBA_(
        const ConvertPlan& _plan,
        const MemoryView   _src,
        const uint32_t     _width,
        const uint32_t     _height,
        std::byte*         _pDst,
        ConvertScratch&    _scratch)
    {
        const size_t numPixels    = static_cast<size_t>(_width) * _height;
        const size_t channelSize  = _plan.srcPixelFormat.formatOrUnknown.size();
        const size_t decodedSize  = numPixels * _plan.srcPixelFormat.numChannels * channelSize;
        const size_t expandedSize = numPixels * image_detail::kNumRgbaChannels * channelSize;

        const std::byte* pCur = _src.GetPtr();

        // 압축 해제 / BGRA 교환
        if (_plan.bDecode)
        {
            std::byte* pOut = _pDst;
            if (_plan.bExpand || _plan.bConvert)
            {
                _scratch.decoded.ResizeUninitialized(decodedSize);
                pOut = _scratch.decoded.GetPtr();
            }
            DecodeSlice_(*_plan.srcFormat, _src, _width, _height, pOut, decodedSize);
            pCur = pOut;
        }

        // 채널 확장
        if (_plan.bExpand)
        {
            std::byte* pOut = _pDst;
            if (_plan.bConvert)
            {
                _scratch.expanded.ResizeUninitialized(expandedSize);
                pOut = _scratch.expanded.GetPtr();
            }
            image_detail::ConvertChannels({ pCur, decodedSize }, { pOut, expandedSize }, numPixels, _plan.srcPixelFormat.numChannels, image_detail::kNumRgbaChannels, _plan.srcPixelFormat.formatOrUnknown);
            pCur = pOut;
        }

        // 타입 변환. 항상 마지막 단계.
        if (_plan.bConvert)
        {
            return OIIO::convert_pixel_values(_plan.srcPixelFormat.formatOrUnknown, pCur, _plan.dstPixelFormat.formatOrUnknown, _pDst, static_cast<int>(numPixels * image_detail::kNumRgbaChannels));
        }

        if (!_plan.bDecode && !_plan.bExpand)
        {
            std::memcpy(_pDst, pCur, expandedSize);
        }
        return true;
    }

    // 2D slice 하나를 변환 대상 포맷(_plan.targetFormat)으로 _pDst 에 기록. 압축 대상이면 RGBA 변환 후 압축.
    [[nodiscard]] bool ConvertSlice_(
        const ConvertPlan& _plan,
        const MemoryView   _src,
        const uint32_t     _width,
        const uint32_t     _height,
        std::byte*         _pDst,
        const uint32_t     _dstSlicePitch,
        ConvertScratch&    _scratch)
    {
        if (!_plan.bCompress)
        {
            return ToRGBA_(_plan, _src, _width, _height, _pDst, _scratch);
        }

        // 변환 단계가 없으면 원본을 그대로 압축 입력으로 사용
        const uint32_t rgbaSlicePitch = CalcSlicePitch(_plan.rgbaFormat, _width, _height);
        MemoryView     rgba           = { _src.GetPtr(), rgbaSlicePitch };
        if (_plan.bDecode || _plan.bExpand || _plan.bConvert)
        {
            _scratch.rgba.ResizeUninitialized(rgbaSlicePitch);
            if (!ToRGBA_(_plan, _src, _width, _height, _scratch.rgba.GetPtr(), _scratch))
            {
                return false;
            }
            rgba = { _scratch.rgba.GetPtr(), rgbaSlicePitch };
        }

        image_detail::CompressSlice(_plan.targetFormat, rgba, { _pDst, _dstSlicePitch }, _width, _height);
        return true;
    }

    // 모든 layer/mip/slice 를 _plan.targetFormat 으로 변환. [layer][mip] 연속 배치된 원본을 순서대로 읽음.
    // _pKtxTextureOrNull 가 null 이면 _pDstOrNull 에 [layer][mip][z] 연속 기록, 아니면 KTX 저장소의 각 image 위치에 직접 기록.
    [[nodiscard]] Error ConvertAll_(
        const MemoryView   _pixels,
        const ImageDesc&   _desc,
        const ConvertPlan& _plan,
        ktxTexture*        _pKtxTextureOrNull,
        std::byte*         _pDstOrNull)
    {
        const uint32_t numFaces = _desc.bCubemap ? 6 : 1;

        ConvertScratch   scratch = {};
        const std::byte* pSrc    = _pixels.GetPtr();
        std::byte*       pDst    = _pDstOrNull;
        for (uint32_t layer = 0; layer < _desc.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < _desc.numMips; ++mip)
            {
                const VEC3U    size          = CalcTextureSize(_desc.width, _desc.height, _desc.depth, mip);
                const uint32_t srcSlicePitch = CalcSourceSlicePitch_(_desc, size.x, size.y);
                const uint32_t dstSlicePitch = CalcSlicePitch(_plan.targetFormat, size.x, size.y);
                if (_pKtxTextureOrNull && ktxTexture_GetImageSize(_pKtxTextureOrNull, mip) != dstSlicePitch)
                {
                    JUG_CORE_LOG_ERROR("Failed to export image: KTX image size mismatch at mip {}.", mip);
                    return eGraphicsError::ImageEncodeFailed;
                }

                for (uint32_t z = 0; z < size.z; ++z)
                {
                    // cube 면 faceSlice = face, 3D 면 faceSlice = z
                    if (_pKtxTextureOrNull)
                    {
                        ktx_size_t           offset;
                        const KTX_error_code offsetResult = ktxTexture_GetImageOffset(_pKtxTextureOrNull, mip, layer / numFaces, layer % numFaces + z, &offset);
                        if (offsetResult != KTX_SUCCESS)
                        {
                            JUG_CORE_LOG_ERROR("Failed to get KTX image offset layer {} mip {}: {}", layer, mip, ktxErrorString(offsetResult));
                            return eGraphicsError::ImageEncodeFailed;
                        }
                        pDst = reinterpret_cast<std::byte*>(_pKtxTextureOrNull->pData) + offset;
                    }

                    if (!ConvertSlice_(_plan, { pSrc, srcSlicePitch }, size.x, size.y, pDst, dstSlicePitch, scratch))
                    {
                        JUG_CORE_LOG_ERROR("Failed to export image: cannot convert format {} to {}.", image_detail::ToLogName(_plan.srcFormat), NameOf(_plan.rgbaFormat));
                        return eGraphicsError::ImageUnsupportedFormat;
                    }

                    pSrc += srcSlicePitch;
                    if (!_pKtxTextureOrNull)
                    {
                        pDst += dstSlicePitch;
                    }
                }
            }
        }
        return kOK;
    }

    [[nodiscard]] Result<ByteBuffer> WriteDDS_(
        const ExportSource& _src)
    {
        const ImageDesc& desc = *_src.pDesc;
        if (!_src.format)
        {
            JUG_CORE_LOG_ERROR("Failed to export DDS: image has no texture format.");
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const eTextureFormat     format = *_src.format;
        const Optional<uint32_t> dxgi   = image_detail::ToDxgiFormat(format);
        if (!dxgi)
        {
            JUG_CORE_LOG_ERROR("Failed to export DDS: format {} is not representable.", NameOf(format));
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const bool     bCompressed = IsCompressedFormat(format);
        const bool     bVolume     = desc.depth > 1;
        const uint32_t numArrays   = desc.bCubemap ? desc.numLayers / 6 : desc.numLayers;

        image_detail::DDS_HEADER header        = {};
        header.size              = sizeof(image_detail::DDS_HEADER);
        header.flags             = image_detail::kDdsdCaps | image_detail::kDdsdHeight | image_detail::kDdsdWidth | image_detail::kDdsdPixelFormat | (bCompressed ? image_detail::kDdsdLinearSize : image_detail::kDdsdPitch);
        header.height            = desc.height;
        header.width             = desc.width;
        header.pitchOrLinearSize = bCompressed ? CalcSlicePitch(format, desc.width, desc.height) : CalcRowPitch(format, desc.width);
        header.depth             = desc.depth;
        header.mipMapCount       = desc.numMips;
        header.ddspf.size        = sizeof(image_detail::DDS_PIXEL_FORMAT);
        header.ddspf.flags       = image_detail::kDdpfFourCC;
        header.ddspf.fourCC      = image_detail::kDdsFourCcDx10;
        header.caps              = image_detail::kDdsCapsTexture;
        if (desc.numMips > 1)
        {
            header.flags |= image_detail::kDdsdMipMapCount;
            header.caps |= image_detail::kDdsCapsComplex | image_detail::kDdsCapsMipMap;
        }
        if (bVolume)
        {
            header.flags |= image_detail::kDdsdDepth;
            header.caps |= image_detail::kDdsCapsComplex;
            header.caps2 |= image_detail::kDdsCaps2Volume;
        }
        if (desc.bCubemap)
        {
            header.caps |= image_detail::kDdsCapsComplex;
            header.caps2 |= image_detail::kDdsCaps2Cubemap | image_detail::kDdsCaps2AllFaces;
        }

        image_detail::DDS_HEADER_DX10 dx10   = {};
        dx10.dxgiFormat        = *dxgi;
        dx10.resourceDimension = bVolume ? image_detail::kDdsDimensionTexture3D : image_detail::kDdsDimensionTexture2D;
        dx10.miscFlag          = desc.bCubemap ? image_detail::kDdsMiscTextureCube : 0;
        dx10.arraySize         = numArrays;

        // magic + header + dx10 + [layer][mip] 픽셀
        const size_t headerSize = sizeof(image_detail::kDdsMagic) + sizeof(image_detail::DDS_HEADER) + sizeof(image_detail::DDS_HEADER_DX10);
        const size_t pixelsSize = CalcTotalSize(desc, format);
        ByteBuffer   out        = { kNoInit, headerSize + pixelsSize };
        std::byte*   pDst       = out.GetPtr();
        std::memcpy(pDst, &image_detail::kDdsMagic, sizeof(image_detail::kDdsMagic));
        pDst += sizeof(image_detail::kDdsMagic);
        std::memcpy(pDst, &header, sizeof(image_detail::DDS_HEADER));
        pDst += sizeof(image_detail::DDS_HEADER);
        std::memcpy(pDst, &dx10, sizeof(image_detail::DDS_HEADER_DX10));
        pDst += sizeof(image_detail::DDS_HEADER_DX10);

        if (_src.bConvert)
        {
            JUG_RETURN_IF_ERROR(ConvertAll_(_src.pixels, desc, _src.plan, nullptr, pDst));
        }
        else
        {
            std::memcpy(pDst, _src.pixels.GetPtr(), pixelsSize);
        }
        return out;
    }

    [[nodiscard]] Result<ByteBuffer> WriteKTX_(
        const ExportSource&     _src,
        const bool              _bKtx2,
        const eImageCompression _basis)
    {
        const ImageDesc& desc     = *_src.pDesc;
        const uint32_t   numFaces = desc.bCubemap ? 6 : 1;
        if (!_src.format)
        {
            JUG_CORE_LOG_ERROR("Failed to export KTX: image has no texture format.");
            return eGraphicsError::ImageUnsupportedFormat;
        }

        const eTextureFormat     format         = *_src.format;
        const Optional<uint32_t> internalFormat = _bKtx2 ? image_detail::ToVkFormat(format) : image_detail::ToGlFormat(format);
        if (!internalFormat)
        {
            JUG_CORE_LOG_ERROR("Failed to export KTX: format {} is not representable.", NameOf(format));
            return eGraphicsError::ImageUnsupportedFormat;
        }

        ktxTextureCreateInfo createInfo = {};
        createInfo.glInternalformat     = _bKtx2 ? 0 : *internalFormat;
        createInfo.vkFormat             = _bKtx2 ? *internalFormat : 0;
        createInfo.baseWidth            = desc.width;
        createInfo.baseHeight           = desc.height;
        createInfo.baseDepth            = desc.depth;
        createInfo.numDimensions        = desc.depth > 1 ? 3 : 2;
        createInfo.numLevels            = desc.numMips;
        createInfo.numLayers            = desc.numLayers / numFaces;
        createInfo.numFaces             = numFaces;
        createInfo.isArray              = createInfo.numLayers > 1;
        createInfo.generateMipmaps      = false;

        // create
        ktxTexture*    pRawTexture;
        KTX_error_code result;
        if (_bKtx2)
        {
            result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, reinterpret_cast<ktxTexture2**>(&pRawTexture));
        }
        else
        {
            result = ktxTexture1_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, reinterpret_cast<ktxTexture1**>(&pRawTexture));
        }

        if (result != KTX_SUCCESS)
        {
            JUG_CORE_LOG_ERROR("Failed to create KTX texture: {}", ktxErrorString(result));
            return eGraphicsError::ImageEncodeFailed;
        }
        const std::unique_ptr<ktxTexture, KtxTextureDeleter> pTexture(pRawTexture);

        // [layer][mip] -> KTX. cube 면 faceSlice = face, 3D 면 faceSlice = z
        if (_src.bConvert)
        {
            // 변환 대상은 KTX1 row padding 이 필요 없으므로 KTX 저장소에 직접 변환
            JUG_RETURN_IF_ERROR(ConvertAll_(_src.pixels, *_src.pDesc, _src.plan, pTexture.get(), nullptr));
        }
        else
        {
            // 원본 그대로. 임의 포맷의 KTX1 row padding 은 SetImageFromMemory 가 처리
            const std::byte* pSrc = _src.pixels.GetPtr();
            for (uint32_t layer = 0; layer < desc.numLayers; ++layer)
            {
                for (uint32_t mip = 0; mip < desc.numMips; ++mip)
                {
                    const VEC3U    size       = CalcTextureSize(desc.width, desc.height, desc.depth, mip);
                    const uint32_t slicePitch = CalcSlicePitch(format, size.x, size.y);
                    for (uint32_t z = 0; z < size.z; ++z)
                    {
                        const KTX_error_code setResult = ktxTexture_SetImageFromMemory(pTexture.get(), mip, layer / numFaces, layer % numFaces + z, reinterpret_cast<const ktx_uint8_t*>(pSrc), slicePitch);
                        if (setResult != KTX_SUCCESS)
                        {
                            JUG_CORE_LOG_ERROR("Failed to set KTX image layer {} mip {}: {}", layer, mip, ktxErrorString(setResult));
                            return eGraphicsError::ImageEncodeFailed;
                        }
                        pSrc += slicePitch;
                    }
                }
            }
        }

        // Basis 압축
        if (_basis == eImageCompression::ETC1S || _basis == eImageCompression::UASTC)
        {
            ktxBasisParams params   = {};
            params.structSize       = sizeof(params);
            params.uastc            = _basis == eImageCompression::UASTC;
            params.threadCount      = Max(1u, std::thread::hardware_concurrency());
            params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
            params.uastcFlags       = KTX_PACK_UASTC_LEVEL_DEFAULT;

            const KTX_error_code compressResult = ktxTexture2_CompressBasisEx(reinterpret_cast<ktxTexture2*>(pTexture.get()), &params);
            if (compressResult != KTX_SUCCESS)
            {
                JUG_CORE_LOG_ERROR("Failed to compress KTX2 with Basis: {}", ktxErrorString(compressResult));
                return eGraphicsError::ImageEncodeFailed;
            }
        }

        // write. 결과 버퍼에 직접 기록. 헤더 여유분을 포함해 미리 확보.
        KtxStreamState state = {};
        state.output.Reserve(pTexture->dataSize + kKtxHeaderReserveSize);

        ktxStream stream               = {};
        stream.read                    = KtxStreamRead_;
        stream.skip                    = KtxStreamSkip_;
        stream.write                   = KtxStreamWrite_;
        stream.getpos                  = KtxStreamGetPos_;
        stream.setpos                  = KtxStreamSetPos_;
        stream.getsize                 = KtxStreamGetSize_;
        stream.destruct                = KtxStreamDestruct_;
        stream.type                    = eStreamTypeCustom;
        stream.data.custom_ptr.address = &state;
        stream.closeOnDestruct         = KTX_FALSE;

        const KTX_error_code writeResult = ktxTexture_WriteToStream(pTexture.get(), &stream);
        if (writeResult != KTX_SUCCESS)
        {
            JUG_CORE_LOG_ERROR("Failed to write KTX: {}", ktxErrorString(writeResult));
            return eGraphicsError::ImageEncodeFailed;
        }
        return std::move(state.output);
    }

    [[nodiscard]] Result<ByteBuffer> ExportContainer_(
        const MemoryView         _pixels,
        const ImageDesc&         _desc,
        const ImageExportConfig& _config)
    {
        const bool bBasis = _config.compression == eImageCompression::ETC1S || _config.compression == eImageCompression::UASTC;
        if (bBasis && _config.format != eImageFileFormat::KTX2)
        {
            JUG_CORE_LOG_ERROR("Failed to export image: ETC1S/UASTC compression is only supported for KTX2.");
            return eGraphicsError::ImageUnsupportedFeature;
        }

        // 원본 그대로 or 변환
        ExportSource src = {};
        src.pixels       = _pixels;
        src.pDesc        = &_desc;
        src.format       = _desc.gpuFormat;
        if (_config.compression != eImageCompression::None)
        {
            // Basis 는 RGBA8 을 입력으로 받음
            const bool bSRGB = _desc.format.bSRGB;

            const Optional<eTextureFormat> targetFormat = bBasis ? Optional<eTextureFormat> { bSRGB ? eTextureFormat::RGBA8_UNorm_SRGB : eTextureFormat::RGBA8_UNorm } : ToBcFormat_(_config.compression, bSRGB);
            if (!targetFormat)
            {
                JUG_CORE_LOG_ERROR("Failed to export image: unsupported compression {}.", NameOf(_config.compression));
                return eGraphicsError::ImageUnsupportedFormat;
            }
            src.format = *targetFormat;

            Result<ConvertPlan> plan = BuildConvertPlan_(_desc, *targetFormat);
            JUG_RETURN_IF_ERROR(plan);
            src.bConvert = true;
            src.plan     = *plan;
        }

        if (_config.format == eImageFileFormat::DDS)
        {
            return WriteDDS_(src);
        }
        return WriteKTX_(src, _config.format == eImageFileFormat::KTX2, _config.compression);
    }

    [[nodiscard]] Result<ByteBuffer> ExportOIIO_(
        const MemoryView         _pixels,
        const ImageDesc&         _desc,
        const ImageExportConfig& _config)
    {
        const char* pFormatNameOrNull = ToOiioFormatNameOrNull_(_config.format);
        if (!pFormatNameOrNull)
        {
            JUG_CORE_LOG_ERROR("Failed to export image: unknown image format {}.", static_cast<int>(_config.format));
            return eGraphicsError::ImageUnsupportedFormat;
        }

        // 압축 해제 / BGRA 교환이 필요할 때만 디코드. 3D 는 첫 slice 만.
        const VEC3U                    size          = CalcTextureSize(_desc.width, _desc.height, _desc.depth, _config.mip);
        const MemoryView               mipPixels     = GetSourceMipPixels_(_pixels, _desc, _config.layer, _config.mip);
        const Optional<eTextureFormat> textureFormat = _desc.gpuFormat;
        const PixelFormat              pixelFormat   = DecodedPixelFormat_(textureFormat, image_detail::GetPixelFormat(_desc));
        if (!image_detail::IsRawFormat(pixelFormat))
        {
            JUG_CORE_LOG_ERROR("Failed to export image: format {} is not supported.", image_detail::ToLogName(textureFormat));
            return eGraphicsError::ImageUnsupportedFormat;
        }

        ByteBuffer       decoded = {};
        const std::byte* pPixels = mipPixels.GetPtr();
        if (textureFormat && NeedsDecode_(*textureFormat))
        {
            decoded.ResizeUninitialized(static_cast<size_t>(size.x) * size.y * pixelFormat.numChannels * pixelFormat.formatOrUnknown.size());
            DecodeSlice_(*textureFormat, mipPixels, size.x, size.y, decoded.GetPtr(), decoded.GetSize());
            pPixels = decoded.GetPtr();
        }

        uint32_t             numChannels = pixelFormat.numChannels;   // 알파 제거 시 3 으로 변경
        const ChannelFormat type        = pixelFormat.formatOrUnknown;

        // create writer
        ByteBufferOutput                   proxy   = {};
        std::unique_ptr<OIIO::ImageOutput> pOutput = OIIO::ImageOutput::create(pFormatNameOrNull, &proxy);
        if (!pOutput)
        {
            JUG_CORE_LOG_ERROR("Failed to create image writer '{}': {}", pFormatNameOrNull, OIIO::geterror());
            return eGraphicsError::ImageEncodeFailed;
        }

        // HDR 등 알파 미지원 writer 는 RGBA 입력 시 open 실패 -> 알파 제거. 복사 없이 앞 3채널만 읽도록 xstride 지정.
        OIIO::stride_t xstride = OIIO::AutoStride;
        if (numChannels == image_detail::kNumRgbaChannels && !pOutput->supports("alpha"))
        {
            xstride     = static_cast<OIIO::stride_t>(image_detail::kNumRgbaChannels * type.size());
            numChannels = 3;
        }

        // write
        OIIO::ImageSpec spec = { static_cast<int>(size.x), static_cast<int>(size.y), static_cast<int>(numChannels), type };
        spec.attribute("oiio:UnassociatedAlpha", 1);   // premultiply 되지 않은 값 그대로 저장
        if (_desc.format.bSRGB)
        {
            spec.set_colorspace("sRGB");
        }

        if (!pOutput->open("", spec) || !pOutput->write_image(type, pPixels, xstride) || !pOutput->close())
        {
            JUG_CORE_LOG_ERROR("Failed to export image ({}): {}", pFormatNameOrNull, pOutput->geterror());
            return eGraphicsError::ImageEncodeFailed;
        }
        return proxy.Take();
    }

    [[nodiscard]] Result<ByteBuffer> Export_(
        const MemoryView         _pixels,
        const ImageDesc&         _desc,
        const ImageExportConfig& _config)
    {
        // 내보낼 포맷 기준으로 분기
        switch (_config.format)
        {
            case eImageFileFormat::DDS:
            case eImageFileFormat::KTX:
            case eImageFileFormat::KTX2:
                return ExportContainer_(_pixels, _desc, _config);
            default:
                return ExportOIIO_(_pixels, _desc, _config);
        }
    }
}   // namespace

Result<ByteBuffer> ExportImage(
    const MemoryView         _pixels,
    const ImageDesc&         _desc,
    const ImageExportConfig& _config)
{
    if (!_desc.gpuFormat && _desc.format.formatOrUnknown == ChannelFormat::UNKNOWN)
    {
        JUG_CORE_LOG_ERROR("Failed to export image: both texture format and pixel format are unknown.");
        return eGraphicsError::ImageUnsupportedFormat;
    }

    // 단일 이미지 포맷은 layer/mip 하나만 내보냄
    const bool bIsContainer = _config.format == eImageFileFormat::DDS || _config.format == eImageFileFormat::KTX || _config.format == eImageFileFormat::KTX2;
    if (!bIsContainer && (_config.layer >= _desc.numLayers || _config.mip >= _desc.numMips))
    {
        JUG_CORE_LOG_ERROR("Failed to export image: layer {} mip {} is out of range ({} layers, {} mips).", _config.layer, _config.mip, _desc.numLayers, _desc.numMips);
        return eGraphicsError::ImageInvalidData;
    }

    const size_t required = CalcSourceTotalSize_(_desc);
    if (_pixels.GetSize() < required)
    {
        JUG_CORE_LOG_ERROR("Failed to export image: pixel data is too small ({} < {} bytes).", _pixels.GetSize(), required);
        return eGraphicsError::ImageInvalidData;
    }
    return Export_(_pixels, _desc, _config);
}

Result<ByteBuffer> ExportImage(
    const Image&             _image,
    const ImageExportConfig& _config)
{
    return ExportImage(_image.pixels, _image.desc, _config);
}

Error ExportImageToFile(
    const FilePath&          _path,
    const MemoryView         _pixels,
    const ImageDesc&         _desc,
    const ImageExportConfig& _config)
{
    Result<ByteBuffer> data = ExportImage(_pixels, _desc, _config);
    JUG_RETURN_IF_ERROR(data);

    Result<FileWriter> writer = FileWriter::Open(_path);
    JUG_RETURN_IF_ERROR(writer);
    JUG_RETURN_IF_ERROR(writer->Write(*data));
    return kOK;
}

Error ExportImageToFile(
    const FilePath&          _path,
    const Image&             _image,
    const ImageExportConfig& _config)
{
    return ExportImageToFile(_path, _image.pixels, _image.desc, _config);
}

}   // namespace jug
