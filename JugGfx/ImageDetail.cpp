#include "pch.h"
#include "ImageDetail.h"

#include <basisu/encoder/basisu_enc.h>
#include <basisu/encoder/basisu_uastc_enc.h>
#include <basisu/transcoder/basisu_astc_hdr_core.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <bcdec/bcdec.h>
#include <OpenImageIO/imageio.h>

#include "Error.h"
#include "Texture.h"

namespace jug
{

namespace
{
    // ===========================================
    //  Codec
    // ===========================================

    constexpr uint32_t kNumMaxExpandChannels = 4;
    constexpr uint32_t kBlockDim             = 4;
    constexpr uint32_t kNumBlockPixels       = kBlockDim * kBlockDim;
    constexpr uint16_t kHalfOne              = 0x3C00;
    constexpr uint16_t kHalfSignMask         = 0x8000;
    constexpr uint16_t kHalfExpMask          = 0x7C00;
    constexpr uint16_t kHalfMantissaMask     = 0x03FF;
    constexpr uint16_t kHalfMaxFinite        = 0x7BFF;
    constexpr uint32_t kDdpfAlphaPixels      = 0x1;
    constexpr uint32_t kDdpfRGB              = 0x40;
    constexpr uint32_t kDdpfLuminance        = 0x20000;

    // legacy DDS FourCC
    constexpr uint32_t kFourCcDxt1 = JUG_FOURCC('D', 'X', 'T', '1');
    constexpr uint32_t kFourCcDxt2 = JUG_FOURCC('D', 'X', 'T', '2');
    constexpr uint32_t kFourCcDxt3 = JUG_FOURCC('D', 'X', 'T', '3');
    constexpr uint32_t kFourCcDxt4 = JUG_FOURCC('D', 'X', 'T', '4');
    constexpr uint32_t kFourCcDxt5 = JUG_FOURCC('D', 'X', 'T', '5');
    constexpr uint32_t kFourCcAti1 = JUG_FOURCC('A', 'T', 'I', '1');
    constexpr uint32_t kFourCcAti2 = JUG_FOURCC('A', 'T', 'I', '2');
    constexpr uint32_t kFourCcBc4U = JUG_FOURCC('B', 'C', '4', 'U');
    constexpr uint32_t kFourCcBc4S = JUG_FOURCC('B', 'C', '4', 'S');
    constexpr uint32_t kFourCcBc5U = JUG_FOURCC('B', 'C', '5', 'U');
    constexpr uint32_t kFourCcBc5S = JUG_FOURCC('B', 'C', '5', 'S');

    // legacy DDS FourCC 자리에 들어가는 D3DFORMAT 값
    constexpr uint32_t kD3dFmtA16B16G16R16  = 36;
    constexpr uint32_t kD3dFmtQ16W16V16U16  = 110;
    constexpr uint32_t kD3dFmtR16F          = 111;
    constexpr uint32_t kD3dFmtG16R16F       = 112;
    constexpr uint32_t kD3dFmtA16B16G16R16F = 113;
    constexpr uint32_t kD3dFmtR32F          = 114;
    constexpr uint32_t kD3dFmtG32R32F       = 115;
    constexpr uint32_t kD3dFmtA32B32G32R32F = 116;

    enum class eDxgiFormat : uint32_t
    {
        R32G32B32A32_Float = 2,
        R32G32B32A32_UInt  = 3,
        R32G32B32A32_SInt  = 4,
        R16G16B16A16_Float = 10,
        R16G16B16A16_UNorm = 11,
        R16G16B16A16_UInt  = 12,
        R16G16B16A16_SNorm = 13,
        R16G16B16A16_SInt  = 14,
        R32G32_Float       = 16,
        R32G32_UInt        = 17,
        R32G32_SInt        = 18,
        R10G10B10A2_UNorm  = 24,
        R11G11B10_Float    = 26,
        R8G8B8A8_UNorm     = 28,
        R8G8B8A8_UNormSRGB = 29,
        R8G8B8A8_UInt      = 30,
        R8G8B8A8_SNorm     = 31,
        R8G8B8A8_SInt      = 32,
        R16G16_Float       = 34,
        R16G16_UNorm       = 35,
        R16G16_UInt        = 36,
        R16G16_SNorm       = 37,
        R16G16_SInt        = 38,
        R32_Float          = 41,
        R32_UInt           = 42,
        R32_SInt           = 43,
        R8G8_UNorm         = 49,
        R8G8_UInt          = 50,
        R8G8_SNorm         = 51,
        R8G8_SInt          = 52,
        R16_Float          = 54,
        R16_UNorm          = 56,
        R16_UInt           = 57,
        R16_SNorm          = 58,
        R16_SInt           = 59,
        R8_UNorm           = 61,
        R8_UInt            = 62,
        R8_SNorm           = 63,
        R8_SInt            = 64,
        BC1_UNorm          = 71,
        BC1_UNormSRGB      = 72,
        BC2_UNorm          = 74,
        BC2_UNormSRGB      = 75,
        BC3_UNorm          = 77,
        BC3_UNormSRGB      = 78,
        BC4_UNorm          = 80,
        BC4_SNorm          = 81,
        BC5_UNorm          = 83,
        BC5_SNorm          = 84,
        B5G6R5_UNorm       = 85,
        B5G5R5A1_UNorm     = 86,
        B8G8R8A8_UNorm     = 87,
        B8G8R8A8_UNormSRGB = 91,
        BC6H_UF16          = 95,
        BC6H_SF16          = 96,
        BC7_UNorm          = 98,
        BC7_UNormSRGB      = 99,
        B4G4R4A4_UNorm     = 115,
    };

    enum class eVkFormat : uint32_t
    {
        R5G6B5_UNormPack16      = 4,
        A1R5G5B5_UNormPack16    = 8,
        R8_UNorm                = 9,
        R8_SNorm                = 10,
        R8_UInt                 = 13,
        R8_SInt                 = 14,
        R8G8_UNorm              = 16,
        R8G8_SNorm              = 17,
        R8G8_UInt               = 20,
        R8G8_SInt               = 21,
        R8G8B8A8_UNorm          = 37,
        R8G8B8A8_SNorm          = 38,
        R8G8B8A8_UInt           = 41,
        R8G8B8A8_SInt           = 42,
        R8G8B8A8_SRGB           = 43,
        B8G8R8A8_UNorm          = 44,
        B8G8R8A8_SRGB           = 50,
        A2B10G10R10_UNormPack32 = 64,
        R16_UNorm               = 70,
        R16_SNorm               = 71,
        R16_UInt                = 74,
        R16_SInt                = 75,
        R16_SFloat              = 76,
        R16G16_UNorm            = 77,
        R16G16_SNorm            = 78,
        R16G16_UInt             = 81,
        R16G16_SInt             = 82,
        R16G16_SFloat           = 83,
        R16G16B16A16_UNorm      = 91,
        R16G16B16A16_SNorm      = 92,
        R16G16B16A16_UInt       = 95,
        R16G16B16A16_SInt       = 96,
        R16G16B16A16_SFloat     = 97,
        R32_UInt                = 98,
        R32_SInt                = 99,
        R32_SFloat              = 100,
        R32G32_UInt             = 101,
        R32G32_SInt             = 102,
        R32G32_SFloat           = 103,
        R32G32B32A32_UInt       = 107,
        R32G32B32A32_SInt       = 108,
        R32G32B32A32_SFloat     = 109,
        B10G11R11_UFloatPack32  = 122,
        BC1_RGB_UNormBlock      = 131,
        BC1_RGB_SRGBBlock       = 132,
        BC1_RGBA_UNormBlock     = 133,
        BC1_RGBA_SRGBBlock      = 134,
        BC2_UNormBlock          = 135,
        BC2_SRGBBlock           = 136,
        BC3_UNormBlock          = 137,
        BC3_SRGBBlock           = 138,
        BC4_UNormBlock          = 139,
        BC4_SNormBlock          = 140,
        BC5_UNormBlock          = 141,
        BC5_SNormBlock          = 142,
        BC6H_UFloatBlock        = 143,
        BC6H_SFloatBlock        = 144,
        BC7_UNormBlock          = 145,
        BC7_SRGBBlock           = 146,
    };

    enum class eGlInternalFormat : uint32_t
    {
        RGBA8                     = 0x8058,
        RGBA16                    = 0x805B,
        R8                        = 0x8229,
        R16                       = 0x822A,
        RG8                       = 0x822B,
        RG16                      = 0x822C,
        R16F                      = 0x822D,
        R32F                      = 0x822E,
        RG16F                     = 0x822F,
        RG32F                     = 0x8230,
        RGBA32F                   = 0x8814,
        RGBA16F                   = 0x881A,
        SRGB8_Alpha8              = 0x8C43,
        CompressedRGB_DXT1        = 0x83F0,
        CompressedRGBA_DXT1       = 0x83F1,
        CompressedRGBA_DXT3       = 0x83F2,
        CompressedRGBA_DXT5       = 0x83F3,
        CompressedSRGB_DXT1       = 0x8C4C,
        CompressedSRGBAlpha_DXT1  = 0x8C4D,
        CompressedSRGBAlpha_DXT3  = 0x8C4E,
        CompressedSRGBAlpha_DXT5  = 0x8C4F,
        CompressedRed_RGTC1       = 0x8DBB,
        CompressedSignedRed_RGTC1 = 0x8DBC,
        CompressedRG_RGTC2        = 0x8DBD,
        CompressedSignedRG_RGTC2  = 0x8DBE,
        CompressedRGBA_BPTC       = 0x8E8C,
        CompressedSRGBAlpha_BPTC  = 0x8E8D,
        CompressedRGB_BPTC_SFloat = 0x8E8E,
        CompressedRGB_BPTC_UFloat = 0x8E8F,
    };

    void DecompressBlock_(
        const eTextureFormat _bcFormat,
        const std::byte*     _pBlock,
        std::byte*           _pOut,
        const uint32_t       _pixelSize)
    {
        const int pitch = static_cast<int>(kBlockDim * _pixelSize);
        switch (_bcFormat)
        {
            case eTextureFormat::BC1_UNorm:
            case eTextureFormat::BC1_UNorm_SRGB:
                bcdec_bc1(_pBlock, _pOut, pitch);
                break;

            case eTextureFormat::BC2_UNorm:
            case eTextureFormat::BC2_UNorm_SRGB:
                bcdec_bc2(_pBlock, _pOut, pitch);
                break;

            case eTextureFormat::BC3_UNorm:
            case eTextureFormat::BC3_UNorm_SRGB:
                bcdec_bc3(_pBlock, _pOut, pitch);
                break;

            case eTextureFormat::BC7_UNorm:
            case eTextureFormat::BC7_UNorm_SRGB:
                bcdec_bc7(_pBlock, _pOut, pitch);
                break;

            case eTextureFormat::BC4_UNorm:
                bcdec_bc4(_pBlock, _pOut, pitch);
                break;

            case eTextureFormat::BC5_UNorm:
                bcdec_bc5(_pBlock, _pOut, pitch);
                break;

            case eTextureFormat::BC6H_UFloat:
            case eTextureFormat::BC6H_SFloat:
            {
                ARRAY<uint16_t, kNumBlockPixels * 3> rgb;
                bcdec_bc6h_half(_pBlock, rgb.data(), static_cast<int>(kBlockDim * 3), _bcFormat == eTextureFormat::BC6H_SFloat);
                for (uint32_t i = 0; i < kNumBlockPixels; ++i)
                {
                    std::memcpy(_pOut + i * _pixelSize, rgb.data() + i * 3, sizeof(uint16_t) * 3);
                    std::memcpy(_pOut + i * _pixelSize + sizeof(uint16_t) * 3, &kHalfOne, sizeof(uint16_t));
                }
                break;
            }
            default:
                JUG_ASSERT(false, "Unsupported BC format.\n");
                break;
        }
    }

    void CompressBlock_(
        const eTextureFormat _bcFormat,
        const std::byte*     _pIn,
        std::byte*           _pBlock)
    {
        const uint8_t* pPixels = reinterpret_cast<const uint8_t*>(_pIn);
        switch (_bcFormat)
        {
            case eTextureFormat::BC1_UNorm:
            case eTextureFormat::BC1_UNorm_SRGB:
                basist::encode_bc1(_pBlock, pPixels, basist::cEncodeBC1HighQuality | basist::cEncodeBC1HigherQuality);
                break;

            case eTextureFormat::BC4_UNorm:
                basist::encode_bc4(_pBlock, pPixels, 4);
                break;

            case eTextureFormat::BC5_UNorm:
                basist::encode_bc4(_pBlock, pPixels, 4);
                basist::encode_bc4(_pBlock + 8, pPixels + 1, 4);
                break;

            case eTextureFormat::BC3_UNorm:
            case eTextureFormat::BC3_UNorm_SRGB:
            {
                basist::uastc_block uastc;
                basisu::encode_uastc(pPixels, uastc, basisu::cPackUASTCLevelDefault);
                basist::transcode_uastc_to_bc3(uastc, _pBlock, true);
                break;
            }

            case eTextureFormat::BC7_UNorm:
            case eTextureFormat::BC7_UNorm_SRGB:
            {
                basist::uastc_block uastc;
                basisu::encode_uastc(pPixels, uastc, basisu::cPackUASTCLevelDefault);
                basist::transcode_uastc_to_bc7(uastc, _pBlock);
                break;
            }

            case eTextureFormat::BC6H_UFloat:
            {
                ARRAY<basist::half_float, kNumBlockPixels * 3> rgb;
                for (uint32_t i = 0; i < kNumBlockPixels; ++i)
                {
                    for (uint32_t c = 0; c < 3; ++c)
                    {
                        uint16_t value;
                        std::memcpy(&value, _pIn + (i * 4 + c) * sizeof(uint16_t), sizeof(uint16_t));

                        const bool bNegative = (value & kHalfSignMask) != 0;
                        const bool bInfOrNaN = (value & kHalfExpMask) == kHalfExpMask;
                        const bool bNaN      = bInfOrNaN && (value & kHalfMantissaMask) != 0;
                        if (bNegative || bNaN)
                        {
                            value = 0;
                        }
                        else if (bInfOrNaN)
                        {
                            value = kHalfMaxFinite;
                        }
                        rgb[i * 3 + c] = value;
                    }
                }

                const basist::astc_6x6_hdr::fast_bc6h_params params = {};
                basist::astc_6x6_hdr::fast_encode_bc6h(rgb.data(), reinterpret_cast<basist::bc6h_block*>(_pBlock), params);
                break;
            }
            default:
                JUG_ASSERT(false, "Unsupported BC format.\n");
                break;
        }
    }

    [[nodiscard]] bool InitBasisu_()
    {
        return basisu::basisu_encoder_init();
    }
}   // namespace

namespace image_detail
{
    [[nodiscard]] Optional<eTextureFormat> ToTextureFormatFromDXGI(
        const uint32_t _dxgiFormat)
    {
        switch (static_cast<eDxgiFormat>(_dxgiFormat))
        {
            case eDxgiFormat::R32G32B32A32_Float: return eTextureFormat::RGBA32_Float;
            case eDxgiFormat::R32G32B32A32_UInt: return eTextureFormat::RGBA32_UInt;
            case eDxgiFormat::R32G32B32A32_SInt: return eTextureFormat::RGBA32_Int;
            case eDxgiFormat::R16G16B16A16_Float: return eTextureFormat::RGBA16_Float;
            case eDxgiFormat::R16G16B16A16_UNorm: return eTextureFormat::RGBA16_UNorm;
            case eDxgiFormat::R16G16B16A16_UInt: return eTextureFormat::RGBA16_UInt;
            case eDxgiFormat::R16G16B16A16_SNorm: return eTextureFormat::RGBA16_SNorm;
            case eDxgiFormat::R16G16B16A16_SInt: return eTextureFormat::RGBA16_Int;
            case eDxgiFormat::R32G32_Float: return eTextureFormat::RG32_Float;
            case eDxgiFormat::R32G32_UInt: return eTextureFormat::RG32_UInt;
            case eDxgiFormat::R32G32_SInt: return eTextureFormat::RG32_Int;
            case eDxgiFormat::R10G10B10A2_UNorm: return eTextureFormat::RGB10A2_UNorm;
            case eDxgiFormat::R11G11B10_Float: return eTextureFormat::RG11B10_Float;
            case eDxgiFormat::R8G8B8A8_UNorm: return eTextureFormat::RGBA8_UNorm;
            case eDxgiFormat::R8G8B8A8_UNormSRGB: return eTextureFormat::RGBA8_UNorm_SRGB;
            case eDxgiFormat::R8G8B8A8_UInt: return eTextureFormat::RGBA8_UInt;
            case eDxgiFormat::R8G8B8A8_SNorm: return eTextureFormat::RGBA8_SNorm;
            case eDxgiFormat::R8G8B8A8_SInt: return eTextureFormat::RGBA8_Int;
            case eDxgiFormat::R16G16_Float: return eTextureFormat::RG16_Float;
            case eDxgiFormat::R16G16_UNorm: return eTextureFormat::RG16_UNorm;
            case eDxgiFormat::R16G16_UInt: return eTextureFormat::RG16_UInt;
            case eDxgiFormat::R16G16_SNorm: return eTextureFormat::RG16_SNorm;
            case eDxgiFormat::R16G16_SInt: return eTextureFormat::RG16_Int;
            case eDxgiFormat::R32_Float: return eTextureFormat::R32_Float;
            case eDxgiFormat::R32_UInt: return eTextureFormat::R32_UInt;
            case eDxgiFormat::R32_SInt: return eTextureFormat::R32_Int;
            case eDxgiFormat::R8G8_UNorm: return eTextureFormat::RG8_UNorm;
            case eDxgiFormat::R8G8_UInt: return eTextureFormat::RG8_UInt;
            case eDxgiFormat::R8G8_SNorm: return eTextureFormat::RG8_SNorm;
            case eDxgiFormat::R8G8_SInt: return eTextureFormat::RG8_Int;
            case eDxgiFormat::R16_Float: return eTextureFormat::R16_Float;
            case eDxgiFormat::R16_UNorm: return eTextureFormat::R16_UNorm;
            case eDxgiFormat::R16_UInt: return eTextureFormat::R16_UInt;
            case eDxgiFormat::R16_SNorm: return eTextureFormat::R16_SNorm;
            case eDxgiFormat::R16_SInt: return eTextureFormat::R16_Int;
            case eDxgiFormat::R8_UNorm: return eTextureFormat::R8_UNorm;
            case eDxgiFormat::R8_UInt: return eTextureFormat::R8_UInt;
            case eDxgiFormat::R8_SNorm: return eTextureFormat::R8_SNorm;
            case eDxgiFormat::R8_SInt: return eTextureFormat::R8_Int;
            case eDxgiFormat::BC1_UNorm: return eTextureFormat::BC1_UNorm;
            case eDxgiFormat::BC1_UNormSRGB: return eTextureFormat::BC1_UNorm_SRGB;
            case eDxgiFormat::BC2_UNorm: return eTextureFormat::BC2_UNorm;
            case eDxgiFormat::BC2_UNormSRGB: return eTextureFormat::BC2_UNorm_SRGB;
            case eDxgiFormat::BC3_UNorm: return eTextureFormat::BC3_UNorm;
            case eDxgiFormat::BC3_UNormSRGB: return eTextureFormat::BC3_UNorm_SRGB;
            case eDxgiFormat::BC4_UNorm: return eTextureFormat::BC4_UNorm;
            case eDxgiFormat::BC4_SNorm: return eTextureFormat::BC4_SNorm;
            case eDxgiFormat::BC5_UNorm: return eTextureFormat::BC5_UNorm;
            case eDxgiFormat::BC5_SNorm: return eTextureFormat::BC5_SNorm;
            case eDxgiFormat::B5G6R5_UNorm: return eTextureFormat::B5G6R5_UNorm;
            case eDxgiFormat::B5G5R5A1_UNorm: return eTextureFormat::BGR5A1_UNorm;
            case eDxgiFormat::B8G8R8A8_UNorm: return eTextureFormat::BGRA8_UNorm;
            case eDxgiFormat::B8G8R8A8_UNormSRGB: return eTextureFormat::BGRA8_UNorm_SRGB;
            case eDxgiFormat::BC6H_UF16: return eTextureFormat::BC6H_UFloat;
            case eDxgiFormat::BC6H_SF16: return eTextureFormat::BC6H_SFloat;
            case eDxgiFormat::BC7_UNorm: return eTextureFormat::BC7_UNorm;
            case eDxgiFormat::BC7_UNormSRGB: return eTextureFormat::BC7_UNorm_SRGB;
            case eDxgiFormat::B4G4R4A4_UNorm: return eTextureFormat::BGRA4_UNorm;
            default: return kNullopt;
        }
    }

    [[nodiscard]] Optional<uint32_t> ToDxgiFormat(
        const eTextureFormat _format)
    {
        Optional<eDxgiFormat> dxgi = kNullopt;
        switch (_format)
        {
            case eTextureFormat::RGBA32_Float: dxgi = eDxgiFormat::R32G32B32A32_Float; break;
            case eTextureFormat::RGBA32_UInt: dxgi = eDxgiFormat::R32G32B32A32_UInt; break;
            case eTextureFormat::RGBA32_Int: dxgi = eDxgiFormat::R32G32B32A32_SInt; break;
            case eTextureFormat::RGBA16_Float: dxgi = eDxgiFormat::R16G16B16A16_Float; break;
            case eTextureFormat::RGBA16_UNorm: dxgi = eDxgiFormat::R16G16B16A16_UNorm; break;
            case eTextureFormat::RGBA16_UInt: dxgi = eDxgiFormat::R16G16B16A16_UInt; break;
            case eTextureFormat::RGBA16_SNorm: dxgi = eDxgiFormat::R16G16B16A16_SNorm; break;
            case eTextureFormat::RGBA16_Int: dxgi = eDxgiFormat::R16G16B16A16_SInt; break;
            case eTextureFormat::RG32_Float: dxgi = eDxgiFormat::R32G32_Float; break;
            case eTextureFormat::RG32_UInt: dxgi = eDxgiFormat::R32G32_UInt; break;
            case eTextureFormat::RG32_Int: dxgi = eDxgiFormat::R32G32_SInt; break;
            case eTextureFormat::RGB10A2_UNorm: dxgi = eDxgiFormat::R10G10B10A2_UNorm; break;
            case eTextureFormat::RG11B10_Float: dxgi = eDxgiFormat::R11G11B10_Float; break;
            case eTextureFormat::RGBA8_UNorm: dxgi = eDxgiFormat::R8G8B8A8_UNorm; break;
            case eTextureFormat::RGBA8_UNorm_SRGB: dxgi = eDxgiFormat::R8G8B8A8_UNormSRGB; break;
            case eTextureFormat::RGBA8_UInt: dxgi = eDxgiFormat::R8G8B8A8_UInt; break;
            case eTextureFormat::RGBA8_SNorm: dxgi = eDxgiFormat::R8G8B8A8_SNorm; break;
            case eTextureFormat::RGBA8_Int: dxgi = eDxgiFormat::R8G8B8A8_SInt; break;
            case eTextureFormat::RG16_Float: dxgi = eDxgiFormat::R16G16_Float; break;
            case eTextureFormat::RG16_UNorm: dxgi = eDxgiFormat::R16G16_UNorm; break;
            case eTextureFormat::RG16_UInt: dxgi = eDxgiFormat::R16G16_UInt; break;
            case eTextureFormat::RG16_SNorm: dxgi = eDxgiFormat::R16G16_SNorm; break;
            case eTextureFormat::RG16_Int: dxgi = eDxgiFormat::R16G16_SInt; break;
            case eTextureFormat::R32_Float: dxgi = eDxgiFormat::R32_Float; break;
            case eTextureFormat::R32_UInt: dxgi = eDxgiFormat::R32_UInt; break;
            case eTextureFormat::R32_Int: dxgi = eDxgiFormat::R32_SInt; break;
            case eTextureFormat::RG8_UNorm: dxgi = eDxgiFormat::R8G8_UNorm; break;
            case eTextureFormat::RG8_UInt: dxgi = eDxgiFormat::R8G8_UInt; break;
            case eTextureFormat::RG8_SNorm: dxgi = eDxgiFormat::R8G8_SNorm; break;
            case eTextureFormat::RG8_Int: dxgi = eDxgiFormat::R8G8_SInt; break;
            case eTextureFormat::R16_Float: dxgi = eDxgiFormat::R16_Float; break;
            case eTextureFormat::R16_UNorm: dxgi = eDxgiFormat::R16_UNorm; break;
            case eTextureFormat::R16_UInt: dxgi = eDxgiFormat::R16_UInt; break;
            case eTextureFormat::R16_SNorm: dxgi = eDxgiFormat::R16_SNorm; break;
            case eTextureFormat::R16_Int: dxgi = eDxgiFormat::R16_SInt; break;
            case eTextureFormat::R8_UNorm: dxgi = eDxgiFormat::R8_UNorm; break;
            case eTextureFormat::R8_UInt: dxgi = eDxgiFormat::R8_UInt; break;
            case eTextureFormat::R8_SNorm: dxgi = eDxgiFormat::R8_SNorm; break;
            case eTextureFormat::R8_Int: dxgi = eDxgiFormat::R8_SInt; break;
            case eTextureFormat::BC1_UNorm: dxgi = eDxgiFormat::BC1_UNorm; break;
            case eTextureFormat::BC1_UNorm_SRGB: dxgi = eDxgiFormat::BC1_UNormSRGB; break;
            case eTextureFormat::BC2_UNorm: dxgi = eDxgiFormat::BC2_UNorm; break;
            case eTextureFormat::BC2_UNorm_SRGB: dxgi = eDxgiFormat::BC2_UNormSRGB; break;
            case eTextureFormat::BC3_UNorm: dxgi = eDxgiFormat::BC3_UNorm; break;
            case eTextureFormat::BC3_UNorm_SRGB: dxgi = eDxgiFormat::BC3_UNormSRGB; break;
            case eTextureFormat::BC4_UNorm: dxgi = eDxgiFormat::BC4_UNorm; break;
            case eTextureFormat::BC4_SNorm: dxgi = eDxgiFormat::BC4_SNorm; break;
            case eTextureFormat::BC5_UNorm: dxgi = eDxgiFormat::BC5_UNorm; break;
            case eTextureFormat::BC5_SNorm: dxgi = eDxgiFormat::BC5_SNorm; break;
            case eTextureFormat::B5G6R5_UNorm: dxgi = eDxgiFormat::B5G6R5_UNorm; break;
            case eTextureFormat::BGR5A1_UNorm: dxgi = eDxgiFormat::B5G5R5A1_UNorm; break;
            case eTextureFormat::BGRA8_UNorm: dxgi = eDxgiFormat::B8G8R8A8_UNorm; break;
            case eTextureFormat::BGRA8_UNorm_SRGB: dxgi = eDxgiFormat::B8G8R8A8_UNormSRGB; break;
            case eTextureFormat::BC6H_UFloat: dxgi = eDxgiFormat::BC6H_UF16; break;
            case eTextureFormat::BC6H_SFloat: dxgi = eDxgiFormat::BC6H_SF16; break;
            case eTextureFormat::BC7_UNorm: dxgi = eDxgiFormat::BC7_UNorm; break;
            case eTextureFormat::BC7_UNorm_SRGB: dxgi = eDxgiFormat::BC7_UNormSRGB; break;
            case eTextureFormat::BGRA4_UNorm: dxgi = eDxgiFormat::B4G4R4A4_UNorm; break;
            default: break;
        }
        if (!dxgi)
        {
            return kNullopt;
        }
        return static_cast<uint32_t>(*dxgi);
    }

    [[nodiscard]] Optional<eTextureFormat> ToTextureFormatFromVK(
        const uint32_t _vkFormat)
    {
        switch (static_cast<eVkFormat>(_vkFormat))
        {
            case eVkFormat::R5G6B5_UNormPack16: return eTextureFormat::B5G6R5_UNorm;
            case eVkFormat::A1R5G5B5_UNormPack16: return eTextureFormat::BGR5A1_UNorm;
            case eVkFormat::R8_UNorm: return eTextureFormat::R8_UNorm;
            case eVkFormat::R8_SNorm: return eTextureFormat::R8_SNorm;
            case eVkFormat::R8_UInt: return eTextureFormat::R8_UInt;
            case eVkFormat::R8_SInt: return eTextureFormat::R8_Int;
            case eVkFormat::R8G8_UNorm: return eTextureFormat::RG8_UNorm;
            case eVkFormat::R8G8_SNorm: return eTextureFormat::RG8_SNorm;
            case eVkFormat::R8G8_UInt: return eTextureFormat::RG8_UInt;
            case eVkFormat::R8G8_SInt: return eTextureFormat::RG8_Int;
            case eVkFormat::R8G8B8A8_UNorm: return eTextureFormat::RGBA8_UNorm;
            case eVkFormat::R8G8B8A8_SNorm: return eTextureFormat::RGBA8_SNorm;
            case eVkFormat::R8G8B8A8_UInt: return eTextureFormat::RGBA8_UInt;
            case eVkFormat::R8G8B8A8_SInt: return eTextureFormat::RGBA8_Int;
            case eVkFormat::R8G8B8A8_SRGB: return eTextureFormat::RGBA8_UNorm_SRGB;
            case eVkFormat::B8G8R8A8_UNorm: return eTextureFormat::BGRA8_UNorm;
            case eVkFormat::B8G8R8A8_SRGB: return eTextureFormat::BGRA8_UNorm_SRGB;
            case eVkFormat::A2B10G10R10_UNormPack32: return eTextureFormat::RGB10A2_UNorm;
            case eVkFormat::R16_UNorm: return eTextureFormat::R16_UNorm;
            case eVkFormat::R16_SNorm: return eTextureFormat::R16_SNorm;
            case eVkFormat::R16_UInt: return eTextureFormat::R16_UInt;
            case eVkFormat::R16_SInt: return eTextureFormat::R16_Int;
            case eVkFormat::R16_SFloat: return eTextureFormat::R16_Float;
            case eVkFormat::R16G16_UNorm: return eTextureFormat::RG16_UNorm;
            case eVkFormat::R16G16_SNorm: return eTextureFormat::RG16_SNorm;
            case eVkFormat::R16G16_UInt: return eTextureFormat::RG16_UInt;
            case eVkFormat::R16G16_SInt: return eTextureFormat::RG16_Int;
            case eVkFormat::R16G16_SFloat: return eTextureFormat::RG16_Float;
            case eVkFormat::R16G16B16A16_UNorm: return eTextureFormat::RGBA16_UNorm;
            case eVkFormat::R16G16B16A16_SNorm: return eTextureFormat::RGBA16_SNorm;
            case eVkFormat::R16G16B16A16_UInt: return eTextureFormat::RGBA16_UInt;
            case eVkFormat::R16G16B16A16_SInt: return eTextureFormat::RGBA16_Int;
            case eVkFormat::R16G16B16A16_SFloat: return eTextureFormat::RGBA16_Float;
            case eVkFormat::R32_UInt: return eTextureFormat::R32_UInt;
            case eVkFormat::R32_SInt: return eTextureFormat::R32_Int;
            case eVkFormat::R32_SFloat: return eTextureFormat::R32_Float;
            case eVkFormat::R32G32_UInt: return eTextureFormat::RG32_UInt;
            case eVkFormat::R32G32_SInt: return eTextureFormat::RG32_Int;
            case eVkFormat::R32G32_SFloat: return eTextureFormat::RG32_Float;
            case eVkFormat::R32G32B32A32_UInt: return eTextureFormat::RGBA32_UInt;
            case eVkFormat::R32G32B32A32_SInt: return eTextureFormat::RGBA32_Int;
            case eVkFormat::R32G32B32A32_SFloat: return eTextureFormat::RGBA32_Float;
            case eVkFormat::B10G11R11_UFloatPack32: return eTextureFormat::RG11B10_Float;
            case eVkFormat::BC1_RGB_UNormBlock:
            case eVkFormat::BC1_RGBA_UNormBlock: return eTextureFormat::BC1_UNorm;
            case eVkFormat::BC1_RGB_SRGBBlock:
            case eVkFormat::BC1_RGBA_SRGBBlock: return eTextureFormat::BC1_UNorm_SRGB;
            case eVkFormat::BC2_UNormBlock: return eTextureFormat::BC2_UNorm;
            case eVkFormat::BC2_SRGBBlock: return eTextureFormat::BC2_UNorm_SRGB;
            case eVkFormat::BC3_UNormBlock: return eTextureFormat::BC3_UNorm;
            case eVkFormat::BC3_SRGBBlock: return eTextureFormat::BC3_UNorm_SRGB;
            case eVkFormat::BC4_UNormBlock: return eTextureFormat::BC4_UNorm;
            case eVkFormat::BC4_SNormBlock: return eTextureFormat::BC4_SNorm;
            case eVkFormat::BC5_UNormBlock: return eTextureFormat::BC5_UNorm;
            case eVkFormat::BC5_SNormBlock: return eTextureFormat::BC5_SNorm;
            case eVkFormat::BC6H_UFloatBlock: return eTextureFormat::BC6H_UFloat;
            case eVkFormat::BC6H_SFloatBlock: return eTextureFormat::BC6H_SFloat;
            case eVkFormat::BC7_UNormBlock: return eTextureFormat::BC7_UNorm;
            case eVkFormat::BC7_SRGBBlock: return eTextureFormat::BC7_UNorm_SRGB;
            default: return kNullopt;
        }
    }

    [[nodiscard]] Optional<uint32_t> ToVkFormat(
        const eTextureFormat _format)
    {
        Optional<eVkFormat> vk = kNullopt;
        switch (_format)
        {
            case eTextureFormat::B5G6R5_UNorm: vk = eVkFormat::R5G6B5_UNormPack16; break;
            case eTextureFormat::BGR5A1_UNorm: vk = eVkFormat::A1R5G5B5_UNormPack16; break;
            case eTextureFormat::R8_UNorm: vk = eVkFormat::R8_UNorm; break;
            case eTextureFormat::R8_SNorm: vk = eVkFormat::R8_SNorm; break;
            case eTextureFormat::R8_UInt: vk = eVkFormat::R8_UInt; break;
            case eTextureFormat::R8_Int: vk = eVkFormat::R8_SInt; break;
            case eTextureFormat::RG8_UNorm: vk = eVkFormat::R8G8_UNorm; break;
            case eTextureFormat::RG8_SNorm: vk = eVkFormat::R8G8_SNorm; break;
            case eTextureFormat::RG8_UInt: vk = eVkFormat::R8G8_UInt; break;
            case eTextureFormat::RG8_Int: vk = eVkFormat::R8G8_SInt; break;
            case eTextureFormat::RGBA8_UNorm: vk = eVkFormat::R8G8B8A8_UNorm; break;
            case eTextureFormat::RGBA8_SNorm: vk = eVkFormat::R8G8B8A8_SNorm; break;
            case eTextureFormat::RGBA8_UInt: vk = eVkFormat::R8G8B8A8_UInt; break;
            case eTextureFormat::RGBA8_Int: vk = eVkFormat::R8G8B8A8_SInt; break;
            case eTextureFormat::RGBA8_UNorm_SRGB: vk = eVkFormat::R8G8B8A8_SRGB; break;
            case eTextureFormat::BGRA8_UNorm: vk = eVkFormat::B8G8R8A8_UNorm; break;
            case eTextureFormat::BGRA8_UNorm_SRGB: vk = eVkFormat::B8G8R8A8_SRGB; break;
            case eTextureFormat::RGB10A2_UNorm: vk = eVkFormat::A2B10G10R10_UNormPack32; break;
            case eTextureFormat::R16_UNorm: vk = eVkFormat::R16_UNorm; break;
            case eTextureFormat::R16_SNorm: vk = eVkFormat::R16_SNorm; break;
            case eTextureFormat::R16_UInt: vk = eVkFormat::R16_UInt; break;
            case eTextureFormat::R16_Int: vk = eVkFormat::R16_SInt; break;
            case eTextureFormat::R16_Float: vk = eVkFormat::R16_SFloat; break;
            case eTextureFormat::RG16_UNorm: vk = eVkFormat::R16G16_UNorm; break;
            case eTextureFormat::RG16_SNorm: vk = eVkFormat::R16G16_SNorm; break;
            case eTextureFormat::RG16_UInt: vk = eVkFormat::R16G16_UInt; break;
            case eTextureFormat::RG16_Int: vk = eVkFormat::R16G16_SInt; break;
            case eTextureFormat::RG16_Float: vk = eVkFormat::R16G16_SFloat; break;
            case eTextureFormat::RGBA16_UNorm: vk = eVkFormat::R16G16B16A16_UNorm; break;
            case eTextureFormat::RGBA16_SNorm: vk = eVkFormat::R16G16B16A16_SNorm; break;
            case eTextureFormat::RGBA16_UInt: vk = eVkFormat::R16G16B16A16_UInt; break;
            case eTextureFormat::RGBA16_Int: vk = eVkFormat::R16G16B16A16_SInt; break;
            case eTextureFormat::RGBA16_Float: vk = eVkFormat::R16G16B16A16_SFloat; break;
            case eTextureFormat::R32_UInt: vk = eVkFormat::R32_UInt; break;
            case eTextureFormat::R32_Int: vk = eVkFormat::R32_SInt; break;
            case eTextureFormat::R32_Float: vk = eVkFormat::R32_SFloat; break;
            case eTextureFormat::RG32_UInt: vk = eVkFormat::R32G32_UInt; break;
            case eTextureFormat::RG32_Int: vk = eVkFormat::R32G32_SInt; break;
            case eTextureFormat::RG32_Float: vk = eVkFormat::R32G32_SFloat; break;
            case eTextureFormat::RGBA32_UInt: vk = eVkFormat::R32G32B32A32_UInt; break;
            case eTextureFormat::RGBA32_Int: vk = eVkFormat::R32G32B32A32_SInt; break;
            case eTextureFormat::RGBA32_Float: vk = eVkFormat::R32G32B32A32_SFloat; break;
            case eTextureFormat::RG11B10_Float: vk = eVkFormat::B10G11R11_UFloatPack32; break;
            case eTextureFormat::BC1_UNorm: vk = eVkFormat::BC1_RGBA_UNormBlock; break;
            case eTextureFormat::BC1_UNorm_SRGB: vk = eVkFormat::BC1_RGBA_SRGBBlock; break;
            case eTextureFormat::BC2_UNorm: vk = eVkFormat::BC2_UNormBlock; break;
            case eTextureFormat::BC2_UNorm_SRGB: vk = eVkFormat::BC2_SRGBBlock; break;
            case eTextureFormat::BC3_UNorm: vk = eVkFormat::BC3_UNormBlock; break;
            case eTextureFormat::BC3_UNorm_SRGB: vk = eVkFormat::BC3_SRGBBlock; break;
            case eTextureFormat::BC4_UNorm: vk = eVkFormat::BC4_UNormBlock; break;
            case eTextureFormat::BC4_SNorm: vk = eVkFormat::BC4_SNormBlock; break;
            case eTextureFormat::BC5_UNorm: vk = eVkFormat::BC5_UNormBlock; break;
            case eTextureFormat::BC5_SNorm: vk = eVkFormat::BC5_SNormBlock; break;
            case eTextureFormat::BC6H_UFloat: vk = eVkFormat::BC6H_UFloatBlock; break;
            case eTextureFormat::BC6H_SFloat: vk = eVkFormat::BC6H_SFloatBlock; break;
            case eTextureFormat::BC7_UNorm: vk = eVkFormat::BC7_UNormBlock; break;
            case eTextureFormat::BC7_UNorm_SRGB: vk = eVkFormat::BC7_SRGBBlock; break;
            default: break;
        }
        if (!vk)
        {
            return kNullopt;
        }
        return static_cast<uint32_t>(*vk);
    }

    [[nodiscard]] Optional<eTextureFormat> ToTextureFormatFromGL(
        const uint32_t _glInternalFormat)
    {
        switch (static_cast<eGlInternalFormat>(_glInternalFormat))
        {
            case eGlInternalFormat::RGBA8: return eTextureFormat::RGBA8_UNorm;
            case eGlInternalFormat::RGBA16: return eTextureFormat::RGBA16_UNorm;
            case eGlInternalFormat::R8: return eTextureFormat::R8_UNorm;
            case eGlInternalFormat::R16: return eTextureFormat::R16_UNorm;
            case eGlInternalFormat::RG8: return eTextureFormat::RG8_UNorm;
            case eGlInternalFormat::RG16: return eTextureFormat::RG16_UNorm;
            case eGlInternalFormat::R16F: return eTextureFormat::R16_Float;
            case eGlInternalFormat::R32F: return eTextureFormat::R32_Float;
            case eGlInternalFormat::RG16F: return eTextureFormat::RG16_Float;
            case eGlInternalFormat::RG32F: return eTextureFormat::RG32_Float;
            case eGlInternalFormat::RGBA32F: return eTextureFormat::RGBA32_Float;
            case eGlInternalFormat::RGBA16F: return eTextureFormat::RGBA16_Float;
            case eGlInternalFormat::SRGB8_Alpha8: return eTextureFormat::RGBA8_UNorm_SRGB;
            case eGlInternalFormat::CompressedRGB_DXT1:
            case eGlInternalFormat::CompressedRGBA_DXT1: return eTextureFormat::BC1_UNorm;
            case eGlInternalFormat::CompressedRGBA_DXT3: return eTextureFormat::BC2_UNorm;
            case eGlInternalFormat::CompressedRGBA_DXT5: return eTextureFormat::BC3_UNorm;
            case eGlInternalFormat::CompressedSRGB_DXT1:
            case eGlInternalFormat::CompressedSRGBAlpha_DXT1: return eTextureFormat::BC1_UNorm_SRGB;
            case eGlInternalFormat::CompressedSRGBAlpha_DXT3: return eTextureFormat::BC2_UNorm_SRGB;
            case eGlInternalFormat::CompressedSRGBAlpha_DXT5: return eTextureFormat::BC3_UNorm_SRGB;
            case eGlInternalFormat::CompressedRed_RGTC1: return eTextureFormat::BC4_UNorm;
            case eGlInternalFormat::CompressedSignedRed_RGTC1: return eTextureFormat::BC4_SNorm;
            case eGlInternalFormat::CompressedRG_RGTC2: return eTextureFormat::BC5_UNorm;
            case eGlInternalFormat::CompressedSignedRG_RGTC2: return eTextureFormat::BC5_SNorm;
            case eGlInternalFormat::CompressedRGBA_BPTC: return eTextureFormat::BC7_UNorm;
            case eGlInternalFormat::CompressedSRGBAlpha_BPTC: return eTextureFormat::BC7_UNorm_SRGB;
            case eGlInternalFormat::CompressedRGB_BPTC_SFloat: return eTextureFormat::BC6H_SFloat;
            case eGlInternalFormat::CompressedRGB_BPTC_UFloat: return eTextureFormat::BC6H_UFloat;
            default: return kNullopt;
        }
    }

    [[nodiscard]] Optional<uint32_t> ToGlFormat(
        const eTextureFormat _format)
    {
        Optional<eGlInternalFormat> gl = kNullopt;
        switch (_format)
        {
            case eTextureFormat::RGBA8_UNorm: gl = eGlInternalFormat::RGBA8; break;
            case eTextureFormat::RGBA16_UNorm: gl = eGlInternalFormat::RGBA16; break;
            case eTextureFormat::R8_UNorm: gl = eGlInternalFormat::R8; break;
            case eTextureFormat::R16_UNorm: gl = eGlInternalFormat::R16; break;
            case eTextureFormat::RG8_UNorm: gl = eGlInternalFormat::RG8; break;
            case eTextureFormat::RG16_UNorm: gl = eGlInternalFormat::RG16; break;
            case eTextureFormat::R16_Float: gl = eGlInternalFormat::R16F; break;
            case eTextureFormat::R32_Float: gl = eGlInternalFormat::R32F; break;
            case eTextureFormat::RG16_Float: gl = eGlInternalFormat::RG16F; break;
            case eTextureFormat::RG32_Float: gl = eGlInternalFormat::RG32F; break;
            case eTextureFormat::RGBA32_Float: gl = eGlInternalFormat::RGBA32F; break;
            case eTextureFormat::RGBA16_Float: gl = eGlInternalFormat::RGBA16F; break;
            case eTextureFormat::RGBA8_UNorm_SRGB: gl = eGlInternalFormat::SRGB8_Alpha8; break;
            case eTextureFormat::BC1_UNorm: gl = eGlInternalFormat::CompressedRGBA_DXT1; break;
            case eTextureFormat::BC2_UNorm: gl = eGlInternalFormat::CompressedRGBA_DXT3; break;
            case eTextureFormat::BC3_UNorm: gl = eGlInternalFormat::CompressedRGBA_DXT5; break;
            case eTextureFormat::BC1_UNorm_SRGB: gl = eGlInternalFormat::CompressedSRGBAlpha_DXT1; break;
            case eTextureFormat::BC2_UNorm_SRGB: gl = eGlInternalFormat::CompressedSRGBAlpha_DXT3; break;
            case eTextureFormat::BC3_UNorm_SRGB: gl = eGlInternalFormat::CompressedSRGBAlpha_DXT5; break;
            case eTextureFormat::BC4_UNorm: gl = eGlInternalFormat::CompressedRed_RGTC1; break;
            case eTextureFormat::BC4_SNorm: gl = eGlInternalFormat::CompressedSignedRed_RGTC1; break;
            case eTextureFormat::BC5_UNorm: gl = eGlInternalFormat::CompressedRG_RGTC2; break;
            case eTextureFormat::BC5_SNorm: gl = eGlInternalFormat::CompressedSignedRG_RGTC2; break;
            case eTextureFormat::BC7_UNorm: gl = eGlInternalFormat::CompressedRGBA_BPTC; break;
            case eTextureFormat::BC7_UNorm_SRGB: gl = eGlInternalFormat::CompressedSRGBAlpha_BPTC; break;
            case eTextureFormat::BC6H_SFloat: gl = eGlInternalFormat::CompressedRGB_BPTC_SFloat; break;
            case eTextureFormat::BC6H_UFloat: gl = eGlInternalFormat::CompressedRGB_BPTC_UFloat; break;
            default: break;
        }
        if (!gl)
        {
            return kNullopt;
        }
        return static_cast<uint32_t>(*gl);
    }

    Optional<eTextureFormat> DDS_PIXEL_FORMAT::ToTextureFormat() const
    {
        if (flags & kDdpfFourCC)
        {
            switch (fourCC)
            {
                case kFourCcDxt1: return eTextureFormat::BC1_UNorm;
                case kFourCcDxt2:
                case kFourCcDxt3: return eTextureFormat::BC2_UNorm;
                case kFourCcDxt4:
                case kFourCcDxt5: return eTextureFormat::BC3_UNorm;
                case kFourCcAti1:
                case kFourCcBc4U: return eTextureFormat::BC4_UNorm;
                case kFourCcBc4S: return eTextureFormat::BC4_SNorm;
                case kFourCcAti2:
                case kFourCcBc5U: return eTextureFormat::BC5_UNorm;
                case kFourCcBc5S: return eTextureFormat::BC5_SNorm;
                case kD3dFmtA16B16G16R16: return eTextureFormat::RGBA16_UNorm;
                case kD3dFmtQ16W16V16U16: return eTextureFormat::RGBA16_SNorm;
                case kD3dFmtR16F: return eTextureFormat::R16_Float;
                case kD3dFmtG16R16F: return eTextureFormat::RG16_Float;
                case kD3dFmtA16B16G16R16F: return eTextureFormat::RGBA16_Float;
                case kD3dFmtR32F: return eTextureFormat::R32_Float;
                case kD3dFmtG32R32F: return eTextureFormat::RG32_Float;
                case kD3dFmtA32B32G32R32F: return eTextureFormat::RGBA32_Float;
                default: return kNullopt;
            }
        }

        const bool bHasAlpha = (flags & kDdpfAlphaPixels) != 0;
        if (flags & kDdpfRGB)
        {
            if (rgbBitCount == 32)
            {
                if (rBitMask == 0x000000ff && gBitMask == 0x0000ff00
                    && bBitMask == 0x00ff0000 && bHasAlpha && aBitMask == 0xff000000)
                {
                    return eTextureFormat::RGBA8_UNorm;
                }
                if (rBitMask == 0x00ff0000 && gBitMask == 0x0000ff00
                    && bBitMask == 0x000000ff && bHasAlpha && aBitMask == 0xff000000)
                {
                    return eTextureFormat::BGRA8_UNorm;
                }
                if (rBitMask == 0x000003ff && gBitMask == 0x000ffc00
                    && bBitMask == 0x3ff00000 && bHasAlpha && aBitMask == 0xc0000000)
                {
                    return eTextureFormat::RGB10A2_UNorm;
                }
                if (rBitMask == 0x0000ffff && gBitMask == 0xffff0000 && bBitMask == 0)
                {
                    return eTextureFormat::RG16_UNorm;
                }
                if (rBitMask == 0xffffffff && gBitMask == 0 && bBitMask == 0)
                {
                    return eTextureFormat::R32_Float;
                }
            }
            else if (rgbBitCount == 16)
            {
                if (rBitMask == 0xf800 && gBitMask == 0x07e0
                    && bBitMask == 0x001f && !bHasAlpha)
                {
                    return eTextureFormat::B5G6R5_UNorm;
                }
                if (rBitMask == 0x7c00 && gBitMask == 0x03e0
                    && bBitMask == 0x001f && bHasAlpha && aBitMask == 0x8000)
                {
                    return eTextureFormat::BGR5A1_UNorm;
                }
                if (rBitMask == 0x0f00 && gBitMask == 0x00f0
                    && bBitMask == 0x000f && bHasAlpha && aBitMask == 0xf000)
                {
                    return eTextureFormat::BGRA4_UNorm;
                }
            }
            return kNullopt;
        }

        if (flags & kDdpfLuminance)
        {
            if (rgbBitCount == 8 && rBitMask == 0xff)
            {
                return eTextureFormat::R8_UNorm;
            }
            if (rgbBitCount == 16 && rBitMask == 0xffff)
            {
                return eTextureFormat::R16_UNorm;
            }
            if (rgbBitCount == 16 && rBitMask == 0x00ff && bHasAlpha && aBitMask == 0xff00)
            {
                return eTextureFormat::RG8_UNorm;
            }
        }
        return kNullopt;
    }

    [[nodiscard]] StringView ToLogName(
        const Optional<eTextureFormat> _format)
    {
        return _format ? NameOf(*_format) : StringView { "none" };
    }

    // texture format 을 PixelFormat 으로 변환. 압축/packed/BGRA 포맷은 unknown.
    [[nodiscard]] PixelFormat ToPixelFormatOrUnknown(
        const eTextureFormat _format)
    {
        switch (_format)
        {
            case eTextureFormat::R8_UNorm:
            case eTextureFormat::R8_UInt:
                return PixelFormat { ChannelFormat::UINT8, 1 };
            case eTextureFormat::R8_SNorm:
            case eTextureFormat::R8_Int:
                return PixelFormat { ChannelFormat::INT8, 1 };
            case eTextureFormat::R16_UNorm:
            case eTextureFormat::R16_UInt:
                return PixelFormat { ChannelFormat::UINT16, 1 };
            case eTextureFormat::R16_SNorm:
            case eTextureFormat::R16_Int:
                return PixelFormat { ChannelFormat::INT16, 1 };
            case eTextureFormat::R16_Float:
                return PixelFormat { ChannelFormat::HALF, 1 };
            case eTextureFormat::R32_UInt:
                return PixelFormat { ChannelFormat::UINT32, 1 };
            case eTextureFormat::R32_Int:
                return PixelFormat { ChannelFormat::INT32, 1 };
            case eTextureFormat::R32_Float:
                return PixelFormat { ChannelFormat::FLOAT, 1 };

            case eTextureFormat::RG8_UNorm:
            case eTextureFormat::RG8_UInt:
                return PixelFormat { ChannelFormat::UINT8, 2 };
            case eTextureFormat::RG8_SNorm:
            case eTextureFormat::RG8_Int:
                return PixelFormat { ChannelFormat::INT8, 2 };
            case eTextureFormat::RG16_UNorm:
            case eTextureFormat::RG16_UInt:
                return PixelFormat { ChannelFormat::UINT16, 2 };
            case eTextureFormat::RG16_SNorm:
            case eTextureFormat::RG16_Int:
                return PixelFormat { ChannelFormat::INT16, 2 };
            case eTextureFormat::RG16_Float:
                return PixelFormat { ChannelFormat::HALF, 2 };
            case eTextureFormat::RG32_UInt:
                return PixelFormat { ChannelFormat::UINT32, 2 };
            case eTextureFormat::RG32_Int:
                return PixelFormat { ChannelFormat::INT32, 2 };
            case eTextureFormat::RG32_Float:
                return PixelFormat { ChannelFormat::FLOAT, 2 };

            case eTextureFormat::RGBA8_UNorm:
            case eTextureFormat::RGBA8_UInt:
                return PixelFormat { ChannelFormat::UINT8, 4 };
            case eTextureFormat::RGBA8_UNorm_SRGB:
                return PixelFormat { ChannelFormat::UINT8, 4, true };
            case eTextureFormat::RGBA8_SNorm:
            case eTextureFormat::RGBA8_Int:
                return PixelFormat { ChannelFormat::INT8, 4 };
            case eTextureFormat::RGBA16_UNorm:
            case eTextureFormat::RGBA16_UInt:
                return PixelFormat { ChannelFormat::UINT16, 4 };
            case eTextureFormat::RGBA16_SNorm:
            case eTextureFormat::RGBA16_Int:
                return PixelFormat { ChannelFormat::INT16, 4 };
            case eTextureFormat::RGBA16_Float:
                return PixelFormat { ChannelFormat::HALF, 4 };
            case eTextureFormat::RGBA32_UInt:
                return PixelFormat { ChannelFormat::UINT32, 4 };
            case eTextureFormat::RGBA32_Int:
                return PixelFormat { ChannelFormat::INT32, 4 };
            case eTextureFormat::RGBA32_Float:
                return PixelFormat { ChannelFormat::FLOAT, 4 };

            default:
                return PixelFormat { ChannelFormat::UNKNOWN, 0, IsSrgbFormat(_format) };
        }
    }

    // 픽셀 단위 채널 축소/확장. 확장 시 r.g.b는 0, a는 1.
    void ConvertChannels(
        const MemoryView        _src,
        const MutableMemoryView _dst,
        const size_t            _numPixels,
        const uint32_t          _numSrcChannels,
        const uint32_t          _numDstChannels,
        const ChannelFormat    _type)
    {
        const size_t channelSize = _type.size();
        JUG_ASSERT(_src.GetSize() >= _numPixels * _numSrcChannels * channelSize, "Source is too small.\n");
        JUG_ASSERT(_dst.GetSize() >= _numPixels * _numDstChannels * channelSize, "Destination is too small.\n");

        const std::byte* pSrc = _src.GetPtr();
        std::byte*       pDst = _dst.GetPtr();

        // 채울 값. 빈 채널은 0. alpha 는 1.
        ARRAY<std::byte, kNumMaxExpandChannels * sizeof(double)> fillPixel = {};
        if (_numDstChannels > kAlphaChannel)
        {
            const float one = 1.f;
            OIIO::convert_pixel_values(OIIO::TypeFloat, &one, _type, fillPixel.data() + kAlphaChannel * channelSize, 1);
        }

        const size_t srcStride = _numSrcChannels * channelSize;
        const size_t dstStride = _numDstChannels * channelSize;
        const size_t copySize  = Min(_numSrcChannels, _numDstChannels) * channelSize;
        for (size_t i = 0; i < _numPixels; ++i)
        {
            std::memcpy(pDst, pSrc, copySize);
            std::memcpy(pDst + copySize, fillPixel.data() + copySize, dstStride - copySize);
            pDst += dstStride;
            pSrc += srcStride;
        }
    }

    // _bcFormat으로 입축시 원하는 포맷
    [[nodiscard]] Optional<eTextureFormat> ToCompressInputFormat(
        const eTextureFormat _bcFormat)
    {
        JUG_ASSERT(IsCompressedFormat(_bcFormat), "Not a BC format.\n");

        switch (_bcFormat)
        {
            case eTextureFormat::BC1_UNorm:
            case eTextureFormat::BC3_UNorm:
            case eTextureFormat::BC4_UNorm:
            case eTextureFormat::BC5_UNorm:
            case eTextureFormat::BC7_UNorm:
                return eTextureFormat::RGBA8_UNorm;

            case eTextureFormat::BC1_UNorm_SRGB:
            case eTextureFormat::BC3_UNorm_SRGB:
            case eTextureFormat::BC7_UNorm_SRGB:
                return eTextureFormat::RGBA8_UNorm_SRGB;

            case eTextureFormat::BC6H_UFloat:
                return eTextureFormat::RGBA16_Float;

            default:
                return kNullopt;
        }
    }

    // 2D slice 하나 압축. _src 는 ToCompressInputFormat 포맷.
    void CompressSlice(
        const eTextureFormat    _srcBcFormat,
        const MemoryView        _src,
        const MutableMemoryView _dst,
        const uint32_t          _width,
        const uint32_t          _height)
    {
        static const bool s_bBasisuInit = InitBasisu_();
        JUG_ASSERT(s_bBasisuInit, "Failed to initialize basisu encoder.\n");

        const Optional<eTextureFormat> compressInputFormat = ToCompressInputFormat(_srcBcFormat);
        JUG_ASSERT(compressInputFormat.HasValue(), "Unsupported BC format.\n");

        const eTextureFormat srcFormat   = *compressInputFormat;
        const uint32_t       pixelSize   = GetBitPerPixel(srcFormat) / 8;
        const uint32_t       blockSize   = CalcRowPitch(_srcBcFormat, 1);   // 블록 1개
        const uint32_t       numBlocksX  = (_width + kBlockDim - 1) / kBlockDim;
        const uint32_t       numBlocksY  = (_height + kBlockDim - 1) / kBlockDim;
        const size_t         srcRowPitch = static_cast<size_t>(_width) * pixelSize;
        JUG_ASSERT(_src.GetSize() >= CalcSlicePitch(srcFormat, _width, _height), "Source slice is too small.\n");
        JUG_ASSERT(_dst.GetSize() >= CalcSlicePitch(_srcBcFormat, _width, _height), "Destination slice is too small.\n");

        const std::byte* pSrc = _src.GetPtr();
        std::byte*       pDst = _dst.GetPtr();

        ARRAY<std::byte, kNumBlockPixels * sizeof(uint16_t) * 4> block = {};
        for (uint32_t by = 0; by < numBlocksY; ++by)
        {
            for (uint32_t bx = 0; bx < numBlocksX; ++bx)
            {
                // 4x4 수집. 가장자리 블록은 경계 픽셀 반복.
                for (uint32_t y = 0; y < kBlockDim; ++y)
                {
                    for (uint32_t x = 0; x < kBlockDim; ++x)
                    {
                        const uint32_t sx = Min(bx * kBlockDim + x, _width - 1);
                        const uint32_t sy = Min(by * kBlockDim + y, _height - 1);
                        std::memcpy(block.data() + (y * kBlockDim + x) * pixelSize, pSrc + sy * srcRowPitch + sx * pixelSize, pixelSize);
                    }
                }

                CompressBlock_(_srcBcFormat, block.data(), pDst);
                pDst += blockSize;
            }
        }
    }

    // 압축 해제시 텍스처 포맷
    [[nodiscard]] Optional<eTextureFormat> ToDecompressedFormat(
        const eTextureFormat _bcFormat)
    {
        JUG_ASSERT(IsCompressedFormat(_bcFormat), "Not a BC format.\n");

        switch (_bcFormat)
        {
            case eTextureFormat::BC1_UNorm:
            case eTextureFormat::BC2_UNorm:
            case eTextureFormat::BC3_UNorm:
            case eTextureFormat::BC7_UNorm:
                return eTextureFormat::RGBA8_UNorm;

            case eTextureFormat::BC1_UNorm_SRGB:
            case eTextureFormat::BC2_UNorm_SRGB:
            case eTextureFormat::BC3_UNorm_SRGB:
            case eTextureFormat::BC7_UNorm_SRGB:
                return eTextureFormat::RGBA8_UNorm_SRGB;

            case eTextureFormat::BC4_UNorm:
                return eTextureFormat::R8_UNorm;

            case eTextureFormat::BC5_UNorm:
                return eTextureFormat::RG8_UNorm;

            case eTextureFormat::BC6H_UFloat:
            case eTextureFormat::BC6H_SFloat:
                return eTextureFormat::RGBA16_Float;

            default:
                return kNullopt;
        }
    }

    // 2D slice 하나 압축 해제. _dst 는 ToDecompressedFormat 포맷.
    void DecompressSlice(
        const eTextureFormat    _srcBcFormat,
        const MemoryView        _src,
        const MutableMemoryView _dst,
        const uint32_t          _width,
        const uint32_t          _height)
    {
        const Optional<eTextureFormat> decompressedFormat = ToDecompressedFormat(_srcBcFormat);
        JUG_ASSERT(decompressedFormat.HasValue(), "Unsupported BC format.\n");

        const eTextureFormat dstFormat   = *decompressedFormat;
        const uint32_t       pixelSize   = GetBitPerPixel(dstFormat) / 8;
        const uint32_t       blockSize   = CalcRowPitch(_srcBcFormat, 1);   // 블록 1개
        const uint32_t       numBlocksX  = (_width + kBlockDim - 1) / kBlockDim;
        const uint32_t       numBlocksY  = (_height + kBlockDim - 1) / kBlockDim;
        const size_t         dstRowPitch = static_cast<size_t>(_width) * pixelSize;
        JUG_ASSERT(_src.GetSize() >= CalcSlicePitch(_srcBcFormat, _width, _height), "Source slice is too small.\n");
        JUG_ASSERT(_dst.GetSize() >= CalcSlicePitch(dstFormat, _width, _height), "Destination slice is too small.\n");

        const std::byte* pSrc = _src.GetPtr();
        std::byte*       pDst = _dst.GetPtr();

        ARRAY<std::byte, kNumBlockPixels * sizeof(uint16_t) * 4> block = {};
        for (uint32_t by = 0; by < numBlocksY; ++by)
        {
            for (uint32_t bx = 0; bx < numBlocksX; ++bx)
            {
                DecompressBlock_(_srcBcFormat, pSrc, block.data(), pixelSize);
                pSrc += blockSize;

                // 가장자리 블록은 이미지 밖 픽셀 버림
                const uint32_t x0    = bx * kBlockDim;
                const uint32_t y0    = by * kBlockDim;
                const uint32_t copyW = Min(kBlockDim, _width - x0);
                const uint32_t copyH = Min(kBlockDim, _height - y0);
                for (uint32_t y = 0; y < copyH; ++y)
                {
                    std::memcpy(pDst + (y0 + y) * dstRowPitch + x0 * pixelSize, block.data() + y * kBlockDim * pixelSize, copyW * pixelSize);
                }
            }
        }
    }

    [[nodiscard]] bool IsRawFormat(
        const PixelFormat& _format)
    {
        return _format.formatOrUnknown != ChannelFormat::UNKNOWN;
    }

    [[nodiscard]] PixelFormat GetPixelFormat(
        const ImageDesc& _desc)
    {
        return _desc.format;
    }

    [[nodiscard]] uint32_t CalcRowPitch(
        const Image&   _image,
        const uint32_t _width)
    {
        // gpuFormat 이 없는(GPU 포맷 없는 채널 구성/타입) 경우는 채널 수 * 채널 타입 기준
        const Optional<eTextureFormat> textureFormat = _image.desc.gpuFormat;
        if (!textureFormat)
        {
            const PixelFormat pixelFormat = GetPixelFormat(_image.desc);
            JUG_ASSERT(IsRawFormat(pixelFormat), "Unknown format requires a channel type.\n");
            return _width * pixelFormat.numChannels * static_cast<uint32_t>(pixelFormat.formatOrUnknown.size());
        }
        return CalcRowPitch(*textureFormat, _width);
    }

    [[nodiscard]] uint32_t CalcSlicePitch(
        const Image&   _image,
        const uint32_t _width,
        const uint32_t _height)
    {
        const Optional<eTextureFormat> textureFormat = _image.desc.gpuFormat;
        if (!textureFormat)
        {
            return CalcRowPitch(_image, _width) * _height;
        }
        return CalcSlicePitch(*textureFormat, _width, _height);
    }

    [[nodiscard]] size_t CalcMipSize(
        const Image&   _image,
        const uint32_t _mip)
    {
        const VEC3U size = CalcTextureSize(_image.desc.width, _image.desc.height, _image.desc.depth, _mip);
        return static_cast<size_t>(CalcSlicePitch(_image, size.x, size.y)) * size.z;
    }

    [[nodiscard]] size_t CalcLayerSize(
        const Image& _image)
    {
        size_t size = 0;
        for (uint32_t mip = 0; mip < _image.desc.numMips; ++mip)
        {
            size += CalcMipSize(_image, mip);
        }
        return size;
    }

    [[nodiscard]] size_t CalcTotalSize(
        const Image& _image)
    {
        return CalcLayerSize(_image) * _image.desc.numLayers;
    }

    [[nodiscard]] size_t CalcOffset(
        const Image&   _image,
        const uint32_t _layer,
        const uint32_t _mip)
    {
        JUG_ASSERT(_layer < _image.desc.numLayers, "Layer index out of bounds.\n");
        JUG_ASSERT(_mip < _image.desc.numMips, "Mip index out of bounds.\n");

        size_t offset = _layer * CalcLayerSize(_image);
        for (uint32_t mip = 0; mip < _mip; ++mip)
        {
            offset += CalcMipSize(_image, mip);
        }
        return offset;
    }

}   // namespace image_detail

}   // namespace jug
