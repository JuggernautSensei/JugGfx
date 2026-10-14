#include "pch.h"
#include "Graphics_D3D11.h"

#include "DxMacro.h"
#include "Shader.h"

namespace jug
{

namespace
{
    constexpr size_t kMaxDebugNameLength = 255;

    constexpr uint32_t kDxbcMagic                = JUG_FOURCC('D', 'X', 'B', 'C');
    constexpr size_t   kShaderBytecodeHeaderSize = 32;

    struct TextureFormatInfo
    {
        DXGI_FORMAT tex  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT rtv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT dsv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srgb = DXGI_FORMAT_UNKNOWN;
    };

    struct VertexSemantic
    {
        const char* pName = nullptr;
        UINT        index = 0;
    };

    [[nodiscard]] VertexSemantic ToVertexSemantic_(
        const eVertexAttribute _attrib)
    {
        switch (_attrib)
        {
            case eVertexAttribute::Position: return { "POSITION", 0 };
            case eVertexAttribute::Normal: return { "NORMAL", 0 };
            case eVertexAttribute::Tangent: return { "TANGENT", 0 };
            case eVertexAttribute::Bitangent: return { "BINORMAL", 0 };

            case eVertexAttribute::Color0: return { "COLOR", 0 };
            case eVertexAttribute::Color1: return { "COLOR", 1 };
            case eVertexAttribute::Color2: return { "COLOR", 2 };
            case eVertexAttribute::Color3: return { "COLOR", 3 };

            case eVertexAttribute::TexCoord0: return { "TEXCOORD", 0 };
            case eVertexAttribute::TexCoord1: return { "TEXCOORD", 1 };
            case eVertexAttribute::TexCoord2: return { "TEXCOORD", 2 };
            case eVertexAttribute::TexCoord3: return { "TEXCOORD", 3 };

            case eVertexAttribute::BoneIndex: return { "BLENDINDICES", 0 };
            case eVertexAttribute::BlendWeight: return { "BLENDWEIGHT", 0 };

            default: JUG_ASSERT(false, "Unsupported vertex attribute."); return {};
        }
    }

    [[nodiscard]] DXGI_FORMAT ToDxgiVertexFormat_(
        const eVertexAttributeFormat _format,
        const uint32_t               _num)
    {
        switch (_format)
        {
            case eVertexAttributeFormat::Float:
                switch (_num)
                {
                    case 1: return DXGI_FORMAT_R32_FLOAT;
                    case 2: return DXGI_FORMAT_R32G32_FLOAT;
                    case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
                    case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
                    default: break;
                }
                break;

            case eVertexAttributeFormat::SInt:
                switch (_num)
                {
                    case 1: return DXGI_FORMAT_R32_SINT;
                    case 2: return DXGI_FORMAT_R32G32_SINT;
                    case 3: return DXGI_FORMAT_R32G32B32_SINT;
                    case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
                    default: break;
                }
                break;

            case eVertexAttributeFormat::UInt:
                switch (_num)
                {
                    case 1: return DXGI_FORMAT_R32_UINT;
                    case 2: return DXGI_FORMAT_R32G32_UINT;
                    case 3: return DXGI_FORMAT_R32G32B32_UINT;
                    case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
                    default: break;
                }
                break;
        }

        JUG_ASSERT(false, "Unsupported vertex attribute format.");
        return DXGI_FORMAT_UNKNOWN;
    }

    [[nodiscard]] const char* ToHlslTypeName_(
        const eVertexAttributeFormat _format,
        const uint32_t               _num)
    {
        switch (_format)
        {
            case eVertexAttributeFormat::Float:
                switch (_num)
                {
                    case 1: return "float";
                    case 2: return "float2";
                    case 3: return "float3";
                    case 4: return "float4";
                    default: break;
                }
                break;

            case eVertexAttributeFormat::SInt:
                switch (_num)
                {
                    case 1: return "int";
                    case 2: return "int2";
                    case 3: return "int3";
                    case 4: return "int4";
                    default: break;
                }
                break;

            case eVertexAttributeFormat::UInt:
                switch (_num)
                {
                    case 1: return "uint";
                    case 2: return "uint2";
                    case 3: return "uint3";
                    case 4: return "uint4";
                    default: break;
                }
                break;
        }

        JUG_ASSERT(false, "Unsupported vertex attribute format.");
        return "";
    }

    [[nodiscard]] String MakeErrorMessage_(
        const HRESULT _hr)
    {
        return MakeSystemError(_hr, eSystemError::OS).MakeMessage();
    }

    [[nodiscard]] StringView ToString_(
        const D3D_DRIVER_TYPE _type)
    {
        switch (_type)
        {
            case D3D_DRIVER_TYPE_UNKNOWN: return "adapter";
            case D3D_DRIVER_TYPE_HARDWARE: return "hardware";
            case D3D_DRIVER_TYPE_REFERENCE: return "reference";
            case D3D_DRIVER_TYPE_NULL: return "null";
            case D3D_DRIVER_TYPE_SOFTWARE: return "software";
            case D3D_DRIVER_TYPE_WARP: return "warp";
        }
        return "?";
    }

    [[nodiscard]] StringView ToString_(
        const D3D_FEATURE_LEVEL _level)
    {
        switch (_level)
        {
            case D3D_FEATURE_LEVEL_9_1: return "9_1";
            case D3D_FEATURE_LEVEL_9_2: return "9_2";
            case D3D_FEATURE_LEVEL_9_3: return "9_3";
            case D3D_FEATURE_LEVEL_10_0: return "10_0";
            case D3D_FEATURE_LEVEL_10_1: return "10_1";
            case D3D_FEATURE_LEVEL_11_0: return "11_0";
            case D3D_FEATURE_LEVEL_11_1: return "11_1";
            default: return "?";
        }
    }

    [[nodiscard]] StringView ToString_(
        const D3D11_MESSAGE_CATEGORY _category)
    {
        switch (_category)
        {
            case D3D11_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION";
            case D3D11_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISC";
            case D3D11_MESSAGE_CATEGORY_INITIALIZATION: return "INIT";
            case D3D11_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
            case D3D11_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
            case D3D11_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
            case D3D11_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
            case D3D11_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
            case D3D11_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE";
            case D3D11_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
            case D3D11_MESSAGE_CATEGORY_SHADER: return "SHADER";
        }
        return "?";
    }

    [[nodiscard]] spdlog::level::level_enum ToLogLevel_(
        const D3D11_MESSAGE_SEVERITY _severity)
    {
        switch (_severity)
        {
            case D3D11_MESSAGE_SEVERITY_CORRUPTION: return spdlog::level::critical;
            case D3D11_MESSAGE_SEVERITY_ERROR: return spdlog::level::err;
            case D3D11_MESSAGE_SEVERITY_WARNING: return spdlog::level::warn;
            case D3D11_MESSAGE_SEVERITY_INFO: return spdlog::level::info;
            case D3D11_MESSAGE_SEVERITY_MESSAGE: return spdlog::level::trace;
        }
        return spdlog::level::info;
    }

    void SetD3d11ObjectName_(
        ID3D11DeviceChild* _pD3d11DeviceChild,
        const StringView   _name)
    {
#ifdef JUG_DEBUG
        JUG_ASSERT(_pD3d11DeviceChild, "Null device child pointer.");
        const UINT len = static_cast<UINT>(Min(_name.size(), kMaxDebugNameLength));
        JUG_DX_CHECK(_pD3d11DeviceChild->SetPrivateData(WKPDID_D3DDebugObjectName, len, _name.data()));
#endif
    }

    [[nodiscard]] TextureFormatInfo MakeTextureFormatInfo_(
        const eTextureFormat _format)
    {
        switch (_format)
        {
            case eTextureFormat::R8_UNorm: return { DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R8_SNorm: return { DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R8_UInt: return { DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R8_Int: return { DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::R16_UNorm: return { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R16_SNorm: return { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R16_UInt: return { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R16_Int: return { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R16_Float: return { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::R32_UInt: return { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R32_Int: return { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::R32_Float: return { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::RG8_UNorm: return { DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG8_SNorm: return { DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG8_UInt: return { DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG8_Int: return { DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_SINT, DXGI_FORMAT_R8G8_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::RG16_UNorm: return { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG16_SNorm: return { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG16_UInt: return { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG16_Int: return { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG16_Float: return { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::RG32_UInt: return { DXGI_FORMAT_R32G32_TYPELESS, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG32_Int: return { DXGI_FORMAT_R32G32_TYPELESS, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG32_Float: return { DXGI_FORMAT_R32G32_TYPELESS, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::RGBA8_UNorm: return { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };
            case eTextureFormat::RGBA8_SNorm: return { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA8_UInt: return { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA8_Int: return { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::RGBA8_UNorm_SRGB: return { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };
            case eTextureFormat::BGRA8_UNorm: return { DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB };
            case eTextureFormat::BGRA8_UNorm_SRGB: return { DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB };

            case eTextureFormat::RGBA16_UNorm: return { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA16_SNorm: return { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA16_UInt: return { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA16_Int: return { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA16_Float: return { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::RGBA32_UInt: return { DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA32_Int: return { DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGBA32_Float: return { DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::B5G6R5_UNorm: return { DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BGRA4_UNorm: return { DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BGR5A1_UNorm: return { DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RGB10A2_UNorm: return { DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::RG11B10_Float: return { DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::D16_UNorm: return { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::D24_UNorm_S8_UInt: return { DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::D32_Float: return { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::D32_Float_S8_UInt: return { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_UNKNOWN };

            case eTextureFormat::BC1_UNorm: return { DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC1_UNORM_SRGB };
            case eTextureFormat::BC1_UNorm_SRGB: return { DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC1_UNORM_SRGB };
            case eTextureFormat::BC2_UNorm: return { DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC2_UNORM_SRGB };
            case eTextureFormat::BC2_UNorm_SRGB: return { DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC2_UNORM_SRGB };
            case eTextureFormat::BC3_UNorm: return { DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC3_UNORM_SRGB };
            case eTextureFormat::BC3_UNorm_SRGB: return { DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC3_UNORM_SRGB };
            case eTextureFormat::BC4_UNorm: return { DXGI_FORMAT_BC4_TYPELESS, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BC4_SNorm: return { DXGI_FORMAT_BC4_TYPELESS, DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BC5_UNorm: return { DXGI_FORMAT_BC5_TYPELESS, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BC5_SNorm: return { DXGI_FORMAT_BC5_TYPELESS, DXGI_FORMAT_BC5_SNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BC6H_UFloat: return { DXGI_FORMAT_BC6H_TYPELESS, DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BC6H_SFloat: return { DXGI_FORMAT_BC6H_TYPELESS, DXGI_FORMAT_BC6H_SF16, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
            case eTextureFormat::BC7_UNorm: return { DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC7_UNORM_SRGB };
            case eTextureFormat::BC7_UNorm_SRGB: return { DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC7_UNORM_SRGB };

            default: JUG_ASSERT(false, "Unsupported texture format."); return {};
        }
    }

    [[nodiscard]] D3D11_PRIMITIVE_TOPOLOGY MakeD3d11Topology_(
        const Flags<eRenderState> _flags)
    {
        const Flags<eRenderState> flags = FilterTopology(_flags);
        const eRenderState        top   = static_cast<eRenderState>(flags.GetFlags());
        switch (top)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eRenderState::Topology_TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case eRenderState::Topology_TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case eRenderState::Topology_LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case eRenderState::Topology_LineStrip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case eRenderState::Topology_PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            default: JUG_ASSERT(false, "Unsupported topology."); return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    [[nodiscard]] D3D11_RASTERIZER_DESC MakeRasterizerDesc_(
        const Flags<eRenderState> _flags)
    {

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode              = _flags.Has(eRenderState::Wireframe) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        rd.FrontCounterClockwise = _flags.Has(eRenderState::FrontCCW) ? TRUE : FALSE;
        rd.DepthBias             = 0;
        rd.DepthBiasClamp        = 0.f;
        rd.SlopeScaledDepthBias  = 0.f;
        rd.DepthClipEnable       = _flags.Has(eRenderState::DepthClamp) ? FALSE : TRUE;
        rd.ScissorEnable         = _flags.Has(eRenderState::Scissor) ? TRUE : FALSE;
        rd.MultisampleEnable     = _flags.Has(eRenderState::MultiSample) ? TRUE : FALSE;
        rd.AntialiasedLineEnable = _flags.Has(eRenderState::LineAA) ? TRUE : FALSE;

        // cull
        const Flags<eRenderState> cullFlags = FilterCullMode(_flags);
        const eRenderState        cull      = static_cast<eRenderState>(cullFlags.GetFlags());
        switch (cull)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eRenderState::Cull_Front: rd.CullMode = D3D11_CULL_FRONT; break;
            case eRenderState::Cull_Back: rd.CullMode = D3D11_CULL_BACK; break;
            case eRenderState::Cull_None: rd.CullMode = D3D11_CULL_NONE; break;
            default: JUG_ASSERT(false, "Unsupported cull mode."); break;
        }
        return rd;
    }

    [[nodiscard]] D3D11_BLEND_DESC MakeBlendDesc_(
        const Flags<eRenderState>                                  _stateFlags,
        const Span<const Flags<eBlend>, kNumMaxRenderTargetSlots>& _blends)
    {
        D3D11_BLEND_DESC bd       = {};
        bd.AlphaToCoverageEnable  = _stateFlags.Has(eRenderState::AlphaToCoverage) ? TRUE : FALSE;
        bd.IndependentBlendEnable = _stateFlags.Has(eRenderState::IndependentBlend) ? TRUE : FALSE;

        const size_t numLoops = bd.IndependentBlendEnable ? _blends.size() : 1;
        for (size_t i = 0; i < numLoops; ++i)
        {
            const Flags<eBlend> flags = _blends[i];

            D3D11_RENDER_TARGET_BLEND_DESC& rtbd = bd.RenderTarget[i];
            rtbd.BlendEnable                     = flags.Has(eBlend::Enable) ? TRUE : FALSE;

            // src blend
            const Flags<eBlend> srcFlags = FilterSrc(flags);
            const eBlend        src      = static_cast<eBlend>(srcFlags.GetFlags());
            switch (src)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eBlend::Src_Zero: rtbd.SrcBlend = D3D11_BLEND_ZERO; break;
                case eBlend::Src_One: rtbd.SrcBlend = D3D11_BLEND_ONE; break;
                case eBlend::Src_SrcColor: rtbd.SrcBlend = D3D11_BLEND_SRC_COLOR; break;
                case eBlend::Src_InvSrcColor: rtbd.SrcBlend = D3D11_BLEND_INV_SRC_COLOR; break;
                case eBlend::Src_SrcAlpha: rtbd.SrcBlend = D3D11_BLEND_SRC_ALPHA; break;
                case eBlend::Src_InvSrcAlpha: rtbd.SrcBlend = D3D11_BLEND_INV_SRC_ALPHA; break;
                case eBlend::Src_DstAlpha: rtbd.SrcBlend = D3D11_BLEND_DEST_ALPHA; break;
                case eBlend::Src_InvDstAlpha: rtbd.SrcBlend = D3D11_BLEND_INV_DEST_ALPHA; break;
                case eBlend::Src_DstColor: rtbd.SrcBlend = D3D11_BLEND_DEST_COLOR; break;
                case eBlend::Src_InvDstColor: rtbd.SrcBlend = D3D11_BLEND_INV_DEST_COLOR; break;
                case eBlend::Src_SrcAlphaSat: rtbd.SrcBlend = D3D11_BLEND_SRC_ALPHA_SAT; break;
                case eBlend::Src_BlendFactor: rtbd.SrcBlend = D3D11_BLEND_BLEND_FACTOR; break;
                case eBlend::Src_InvBlendFactor: rtbd.SrcBlend = D3D11_BLEND_INV_BLEND_FACTOR; break;
                default: JUG_ASSERT(false, "Unsupported source blend."); break;
            }

            // dst blend
            const Flags<eBlend> dstFlags = FilterDst(flags);
            const eBlend        dst      = static_cast<eBlend>(dstFlags.GetFlags());
            switch (dst)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eBlend::Dst_Zero: rtbd.DestBlend = D3D11_BLEND_ZERO; break;
                case eBlend::Dst_One: rtbd.DestBlend = D3D11_BLEND_ONE; break;
                case eBlend::Dst_SrcColor: rtbd.DestBlend = D3D11_BLEND_SRC_COLOR; break;
                case eBlend::Dst_InvSrcColor: rtbd.DestBlend = D3D11_BLEND_INV_SRC_COLOR; break;
                case eBlend::Dst_SrcAlpha: rtbd.DestBlend = D3D11_BLEND_SRC_ALPHA; break;
                case eBlend::Dst_InvSrcAlpha: rtbd.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; break;
                case eBlend::Dst_DstAlpha: rtbd.DestBlend = D3D11_BLEND_DEST_ALPHA; break;
                case eBlend::Dst_InvDstAlpha: rtbd.DestBlend = D3D11_BLEND_INV_DEST_ALPHA; break;
                case eBlend::Dst_DstColor: rtbd.DestBlend = D3D11_BLEND_DEST_COLOR; break;
                case eBlend::Dst_InvDstColor: rtbd.DestBlend = D3D11_BLEND_INV_DEST_COLOR; break;
                case eBlend::Dst_SrcAlphaSat: rtbd.DestBlend = D3D11_BLEND_SRC_ALPHA_SAT; break;
                case eBlend::Dst_BlendFactor: rtbd.DestBlend = D3D11_BLEND_BLEND_FACTOR; break;
                case eBlend::Dst_InvBlendFactor: rtbd.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR; break;
                default: JUG_ASSERT(false, "Unsupported destination blend."); break;
            }

            // blend op
            const Flags<eBlend> opFlags = FilterOp(flags);
            const eBlend        op      = static_cast<eBlend>(opFlags.GetFlags());
            switch (op)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eBlend::Op_Add: rtbd.BlendOp = D3D11_BLEND_OP_ADD; break;
                case eBlend::Op_Subtract: rtbd.BlendOp = D3D11_BLEND_OP_SUBTRACT; break;
                case eBlend::Op_RevSubtract: rtbd.BlendOp = D3D11_BLEND_OP_REV_SUBTRACT; break;
                case eBlend::Op_Min: rtbd.BlendOp = D3D11_BLEND_OP_MIN; break;
                case eBlend::Op_Max: rtbd.BlendOp = D3D11_BLEND_OP_MAX; break;
                default: JUG_ASSERT(false, "Unsupported blend op."); break;
            }

            // src blend alpha
            const Flags<eBlend> srcAlphaFlags = FilterSrcAlpha(flags);
            const eBlend        srcAlpha      = static_cast<eBlend>(srcAlphaFlags.GetFlags());
            switch (srcAlpha)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eBlend::SrcAlpha_Zero: rtbd.SrcBlendAlpha = D3D11_BLEND_ZERO; break;
                case eBlend::SrcAlpha_One: rtbd.SrcBlendAlpha = D3D11_BLEND_ONE; break;
                case eBlend::SrcAlpha_SrcAlpha: rtbd.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA; break;
                case eBlend::SrcAlpha_InvSrcAlpha: rtbd.SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; break;
                case eBlend::SrcAlpha_DstAlpha: rtbd.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA; break;
                case eBlend::SrcAlpha_InvDstAlpha: rtbd.SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA; break;
                case eBlend::SrcAlpha_BlendFactor: rtbd.SrcBlendAlpha = D3D11_BLEND_BLEND_FACTOR; break;
                case eBlend::SrcAlpha_InvBlendFactor: rtbd.SrcBlendAlpha = D3D11_BLEND_INV_BLEND_FACTOR; break;
                default: JUG_ASSERT(false, "Unsupported source alpha blend."); break;
            }

            // dst blend alpha
            const Flags<eBlend> dstAlphaFlags = FilterDstAlpha(flags);
            const eBlend        dstAlpha      = static_cast<eBlend>(dstAlphaFlags.GetFlags());
            switch (dstAlpha)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eBlend::DstAlpha_Zero: rtbd.DestBlendAlpha = D3D11_BLEND_ZERO; break;
                case eBlend::DstAlpha_One: rtbd.DestBlendAlpha = D3D11_BLEND_ONE; break;
                case eBlend::DstAlpha_SrcAlpha: rtbd.DestBlendAlpha = D3D11_BLEND_SRC_ALPHA; break;
                case eBlend::DstAlpha_InvSrcAlpha: rtbd.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; break;
                case eBlend::DstAlpha_DstAlpha: rtbd.DestBlendAlpha = D3D11_BLEND_DEST_ALPHA; break;
                case eBlend::DstAlpha_InvDstAlpha: rtbd.DestBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA; break;
                case eBlend::DstAlpha_BlendFactor: rtbd.DestBlendAlpha = D3D11_BLEND_BLEND_FACTOR; break;
                case eBlend::DstAlpha_InvBlendFactor: rtbd.DestBlendAlpha = D3D11_BLEND_INV_BLEND_FACTOR; break;
                default: JUG_ASSERT(false, "Unsupported destination alpha blend."); break;
            }

            // blend op alpha
            const Flags<eBlend> opAlphaFlags = FilterOpAlpha(flags);
            const eBlend        opAlpha      = static_cast<eBlend>(opAlphaFlags.GetFlags());
            switch (opAlpha)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eBlend::OpAlpha_Add: rtbd.BlendOpAlpha = D3D11_BLEND_OP_ADD; break;
                case eBlend::OpAlpha_Subtract: rtbd.BlendOpAlpha = D3D11_BLEND_OP_SUBTRACT; break;
                case eBlend::OpAlpha_RevSubtract: rtbd.BlendOpAlpha = D3D11_BLEND_OP_REV_SUBTRACT; break;
                case eBlend::OpAlpha_Min: rtbd.BlendOpAlpha = D3D11_BLEND_OP_MIN; break;
                case eBlend::OpAlpha_Max: rtbd.BlendOpAlpha = D3D11_BLEND_OP_MAX; break;
                default: JUG_ASSERT(false, "Unsupported blend op alpha."); break;
            }

            rtbd.RenderTargetWriteMask = 0;
            rtbd.RenderTargetWriteMask |= flags.Has(eBlend::Write_R) ? D3D11_COLOR_WRITE_ENABLE_RED : 0;
            rtbd.RenderTargetWriteMask |= flags.Has(eBlend::Write_G) ? D3D11_COLOR_WRITE_ENABLE_GREEN : 0;
            rtbd.RenderTargetWriteMask |= flags.Has(eBlend::Write_B) ? D3D11_COLOR_WRITE_ENABLE_BLUE : 0;
            rtbd.RenderTargetWriteMask |= flags.Has(eBlend::Write_A) ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0;
        }
        return bd;
    }

    [[nodiscard]] D3D11_DEPTH_STENCILOP_DESC MakeDepthStencilOpDesc_(
        const Flags<eStencil> _flags)
    {
        D3D11_DEPTH_STENCILOP_DESC dsd = {};

        // stencil (x) depth (o)
        const Flags<eStencil> depthPassFlags = FilterStencilFailDepthPass(_flags);
        const eStencil        depthPass      = static_cast<eStencil>(depthPassFlags.GetFlags());
        switch (depthPass)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eStencil::StencilFail_DepthPass_Keep: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP; break;
            case eStencil::StencilFail_DepthPass_Zero: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_ZERO; break;
            case eStencil::StencilFail_DepthPass_Replace: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE; break;
            case eStencil::StencilFail_DepthPass_IncrSat: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_INCR_SAT; break;
            case eStencil::StencilFail_DepthPass_DecrSat: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_DECR_SAT; break;
            case eStencil::StencilFail_DepthPass_Invert: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_INVERT; break;
            case eStencil::StencilFail_DepthPass_Incr: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_INCR; break;
            case eStencil::StencilFail_DepthPass_Decr: dsd.StencilDepthFailOp = D3D11_STENCIL_OP_DECR; break;
            default: JUG_ASSERT(false, "Unsupported depth pass stencil op."); break;
        }

        // stencil (o) depth (x)
        const Flags<eStencil> depthFailFlags = FilterStencilPassDepthFail(_flags);
        const eStencil        depthFail      = static_cast<eStencil>(depthFailFlags.GetFlags());
        switch (depthFail)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eStencil::StencilPass_DepthFail_Keep: dsd.StencilFailOp = D3D11_STENCIL_OP_KEEP; break;
            case eStencil::StencilPass_DepthFail_Zero: dsd.StencilFailOp = D3D11_STENCIL_OP_ZERO; break;
            case eStencil::StencilPass_DepthFail_Replace: dsd.StencilFailOp = D3D11_STENCIL_OP_REPLACE; break;
            case eStencil::StencilPass_DepthFail_IncrSat: dsd.StencilFailOp = D3D11_STENCIL_OP_INCR_SAT; break;
            case eStencil::StencilPass_DepthFail_DecrSat: dsd.StencilFailOp = D3D11_STENCIL_OP_DECR_SAT; break;
            case eStencil::StencilPass_DepthFail_Invert: dsd.StencilFailOp = D3D11_STENCIL_OP_INVERT; break;
            case eStencil::StencilPass_DepthFail_Incr: dsd.StencilFailOp = D3D11_STENCIL_OP_INCR; break;
            case eStencil::StencilPass_DepthFail_Decr: dsd.StencilFailOp = D3D11_STENCIL_OP_DECR; break;
            default: JUG_ASSERT(false, "Unsupported stencil fail op."); break;
        }

        // stencil (o) depth (o)
        const Flags<eStencil> bothPassFlags = FilterStencilPassDepthPass(_flags);
        const eStencil        bothPass      = static_cast<eStencil>(bothPassFlags.GetFlags());
        switch (bothPass)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eStencil::StencilPass_DepthPass_Keep: dsd.StencilPassOp = D3D11_STENCIL_OP_KEEP; break;
            case eStencil::StencilPass_DepthPass_Zero: dsd.StencilPassOp = D3D11_STENCIL_OP_ZERO; break;
            case eStencil::StencilPass_DepthPass_Replace: dsd.StencilPassOp = D3D11_STENCIL_OP_REPLACE; break;
            case eStencil::StencilPass_DepthPass_IncrSat: dsd.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT; break;
            case eStencil::StencilPass_DepthPass_DecrSat: dsd.StencilPassOp = D3D11_STENCIL_OP_DECR_SAT; break;
            case eStencil::StencilPass_DepthPass_Invert: dsd.StencilPassOp = D3D11_STENCIL_OP_INVERT; break;
            case eStencil::StencilPass_DepthPass_Incr: dsd.StencilPassOp = D3D11_STENCIL_OP_INCR; break;
            case eStencil::StencilPass_DepthPass_Decr: dsd.StencilPassOp = D3D11_STENCIL_OP_DECR; break;
            default: JUG_ASSERT(false, "Unsupported stencil pass op."); break;
        }

        // stencil compare
        const Flags<eStencil> compareFlags = FilterCompare(_flags);
        const eStencil        compare      = static_cast<eStencil>(compareFlags.GetFlags());
        switch (compare)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eStencil::Compare_None: dsd.StencilFunc = D3D11_COMPARISON_ALWAYS; break;
            case eStencil::Compare_Less: dsd.StencilFunc = D3D11_COMPARISON_LESS; break;
            case eStencil::Compare_LessEqual: dsd.StencilFunc = D3D11_COMPARISON_LESS_EQUAL; break;
            case eStencil::Compare_Greater: dsd.StencilFunc = D3D11_COMPARISON_GREATER; break;
            case eStencil::Compare_GreaterEqual: dsd.StencilFunc = D3D11_COMPARISON_GREATER_EQUAL; break;
            case eStencil::Compare_Equal: dsd.StencilFunc = D3D11_COMPARISON_EQUAL; break;
            case eStencil::Compare_NotEqual: dsd.StencilFunc = D3D11_COMPARISON_NOT_EQUAL; break;
            case eStencil::Compare_Always: dsd.StencilFunc = D3D11_COMPARISON_ALWAYS; break;
            case eStencil::Compare_Never: dsd.StencilFunc = D3D11_COMPARISON_NEVER; break;
            default: JUG_ASSERT(false, "Unsupported stencil compare func."); break;
        }

        return dsd;
    }

    [[nodiscard]] D3D11_DEPTH_STENCIL_DESC MakeDepthStencilDesc_(
        const Flags<eRenderState> _flags,
        const Flags<eStencil>     _frontFlags,
        const Flags<eStencil>     _backFlags)
    {
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthWriteMask           = _flags & eRenderState::DepthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;

        // depth test
        const Flags<eRenderState> depthTestFlags = FilterDepthTest(_flags);
        const eRenderState        depthTest      = static_cast<eRenderState>(depthTestFlags.GetFlags());
        dsd.DepthEnable                          = depthTest == eRenderState::DepthTest_None ? FALSE : TRUE;
        switch (depthTest)   // NOLINT(clang-diagnostic-switch-enum
        {
            case eRenderState::DepthTest_None: dsd.DepthFunc = D3D11_COMPARISON_ALWAYS; break;
            case eRenderState::DepthTest_Less: dsd.DepthFunc = D3D11_COMPARISON_LESS; break;
            case eRenderState::DepthTest_LessEqual: dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; break;
            case eRenderState::DepthTest_Greater: dsd.DepthFunc = D3D11_COMPARISON_GREATER; break;
            case eRenderState::DepthTest_GreaterEqual: dsd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL; break;
            case eRenderState::DepthTest_Equal: dsd.DepthFunc = D3D11_COMPARISON_EQUAL; break;
            case eRenderState::DepthTest_NotEqual: dsd.DepthFunc = D3D11_COMPARISON_NOT_EQUAL; break;
            case eRenderState::DepthTest_Always: dsd.DepthFunc = D3D11_COMPARISON_ALWAYS; break;
            case eRenderState::DepthTest_Never: dsd.DepthFunc = D3D11_COMPARISON_NEVER; break;
            default: JUG_ASSERT(false, "Unsupported depth test func."); break;
        }

        dsd.StencilEnable    = _frontFlags == eStencil::None && _backFlags == eStencil::None ? FALSE : TRUE;
        dsd.StencilReadMask  = D3D11_DEFAULT_STENCIL_READ_MASK;
        dsd.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        dsd.FrontFace        = MakeDepthStencilOpDesc_(_frontFlags);
        dsd.BackFace         = MakeDepthStencilOpDesc_(_backFlags);
        return dsd;
    }

    [[nodiscard]] D3D11_SAMPLER_DESC MakeSamplerDesc_(
        const Flags<eSampler> _flags,
        const RGBA            _borderColor)
    {
        D3D11_SAMPLER_DESC sd = {};

        // filter
        const Flags<eSampler> filterFlags  = FilterFilter(_flags);
        const eSampler        filter       = static_cast<eSampler>(filterFlags.GetFlags());
        const Flags<eSampler> compareFlags = FilterCompare(_flags);
        const eSampler        compare      = static_cast<eSampler>(compareFlags.GetFlags());
        const bool            bComparison  = compare != eSampler::Compare_None;
        const eSampler        aniso        = static_cast<eSampler>(filterFlags.GetFlags() & static_cast<uint32_t>(eSampler::Filter_Anisotropic16));
        if (aniso == eSampler::None)
        {
            // index: min(4) | mag(2) | mip(1)
            constexpr std::array<D3D11_FILTER, 8> kFilters           = std::to_array<D3D11_FILTER>({
                D3D11_FILTER_MIN_MAG_MIP_POINT,
                D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,
                D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
                D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,
                D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
                D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
                D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            });
            constexpr std::array<D3D11_FILTER, 8> kComparisonFilters = std::to_array<D3D11_FILTER>({
                D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
                D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR,
                D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
                D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR,
                D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,
                D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
                D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
            });

            // enum bit: min(1) | mag(2) | mip(4) -> table index: min(4) | mag(2) | mip(1)
            const uint32_t bits  = static_cast<uint32_t>(filter);
            const size_t   index = ((bits & 1u) << 2) | (bits & 2u) | ((bits >> 2) & 1u);
            sd.Filter            = bComparison ? kComparisonFilters[index] : kFilters[index];
            sd.MaxAnisotropy     = 1;
        }
        else
        {
            // anisotropic 은 min/mag/mip 과 함께 쓸 수 없음
            JUG_ASSERT(filter == aniso, "Anisotropic filter must not be combined with min/mag/mip.");
            sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            switch (aniso)   // NOLINT(clang-diagnostic-switch-enum)
            {
                case eSampler::Filter_Anisotropic4: sd.MaxAnisotropy = 4; break;
                case eSampler::Filter_Anisotropic8: sd.MaxAnisotropy = 8; break;
                case eSampler::Filter_Anisotropic16: sd.MaxAnisotropy = 16; break;
                default: JUG_ASSERT(false, "Unsupported anisotropic filter."); break;
            }
        }

        // address u
        const Flags<eSampler> uFlags = FilterU(_flags);
        const eSampler        u      = static_cast<eSampler>(uFlags.GetFlags());
        switch (u)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eSampler::U_Wrap: sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP; break;
            case eSampler::U_Mirror: sd.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR; break;
            case eSampler::U_Clamp: sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP; break;
            case eSampler::U_Border: sd.AddressU = D3D11_TEXTURE_ADDRESS_BORDER; break;
            case eSampler::U_MirrorOnce: sd.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE; break;
            default: JUG_ASSERT(false, "Unsupported address mode U."); break;
        }

        // address v
        const Flags<eSampler> vFlags = FilterV(_flags);
        const eSampler        v      = static_cast<eSampler>(vFlags.GetFlags());
        switch (v)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eSampler::V_Wrap: sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP; break;
            case eSampler::V_Mirror: sd.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR; break;
            case eSampler::V_Clamp: sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP; break;
            case eSampler::V_Border: sd.AddressV = D3D11_TEXTURE_ADDRESS_BORDER; break;
            case eSampler::V_MirrorOnce: sd.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE; break;
            default: JUG_ASSERT(false, "Unsupported address mode V."); break;
        }

        // address w
        const Flags<eSampler> wFlags = FilterW(_flags);
        const eSampler        w      = static_cast<eSampler>(wFlags.GetFlags());
        switch (w)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eSampler::W_Wrap: sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; break;
            case eSampler::W_Mirror: sd.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR; break;
            case eSampler::W_Clamp: sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; break;
            case eSampler::W_Border: sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER; break;
            case eSampler::W_MirrorOnce: sd.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE; break;
            default: JUG_ASSERT(false, "Unsupported address mode W."); break;
        }

        sd.MipLODBias = 0.f;

        // comparison func
        switch (compare)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eSampler::Compare_None: sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS; break;
            case eSampler::Compare_Less: sd.ComparisonFunc = D3D11_COMPARISON_LESS; break;
            case eSampler::Compare_LessEqual: sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL; break;
            case eSampler::Compare_Greater: sd.ComparisonFunc = D3D11_COMPARISON_GREATER; break;
            case eSampler::Compare_GreaterEqual: sd.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL; break;
            case eSampler::Compare_Equal: sd.ComparisonFunc = D3D11_COMPARISON_EQUAL; break;
            case eSampler::Compare_NotEqual: sd.ComparisonFunc = D3D11_COMPARISON_NOT_EQUAL; break;
            case eSampler::Compare_Always: sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS; break;
            case eSampler::Compare_Never: sd.ComparisonFunc = D3D11_COMPARISON_NEVER; break;
            default: JUG_ASSERT(false, "Unsupported comparison func."); break;
        }

        const VEC4 rgba   = _borderColor.ToLinear();
        sd.BorderColor[0] = rgba.x;
        sd.BorderColor[1] = rgba.y;
        sd.BorderColor[2] = rgba.z;
        sd.BorderColor[3] = rgba.w;
        sd.MinLOD         = 0.f;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;
        return sd;
    }

    [[nodiscard]] HRESULT CreateD3d11Device_(
        IAdapter*             _pAdapterOrNull,
        const D3D_DRIVER_TYPE _driverType,
        const UINT            _deviceFlags,
        ID3D11Device**        _ppOutDevice,
        D3D_FEATURE_LEVEL*    _pOutFeatureLevel,
        ID3D11DeviceContext** _ppOutDeviceContext)
    {
        constexpr ARRAY<D3D_FEATURE_LEVEL, 2> kD3d11FeatureLevels {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        return ::D3D11CreateDevice(
            _pAdapterOrNull,
            _driverType,
            nullptr,
            _deviceFlags,
            kD3d11FeatureLevels.data(),
            static_cast<UINT>(kD3d11FeatureLevels.size()),
            D3D11_SDK_VERSION,
            _ppOutDevice,
            _pOutFeatureLevel,
            _ppOutDeviceContext);
    }

}   // namespace

// ===========================================
//  Graphics_D3D11
// ===========================================

Graphics_D3D11::Graphics_D3D11(
    const Flags<eGraphicsOption> _flags)
    : Graphics(eGraphicsAPI::D3D11, _flags)
{
    Timer timer = {};
    timer.Start();

    // caps 초기화
    m_dxgi.FillCaps(m_caps);

    // device 생성
    InitDevice_();

    if (_flags & eGraphicsOption::Debug)
    {
        InitDebugInterfaces_();
        FlushDebugMessages_();   // 디바이스 생성 중 쌓인 메시지
    }

    if (_flags & eGraphicsOption::ProfileFrameTime)
    {
        InitTimerQueries_();
    }

    timer.Stop();
    const ReadableTimeF time = ToReadableTimeF(timer.GetTotalCount());
    JUG_CORE_LOG_INFO("Graphics initialized in {:.1f} {}", time.time, time.unitName);
}

Graphics_D3D11::~Graphics_D3D11()
{
    // 컨텍스트 초기화
    m_pD3d11DeviceContext->ClearState();
    m_pD3d11DeviceContext->Flush();

    // 타이머 파괴
    if (m_caps.flags & eGraphicsOption::ProfileFrameTime)
    {
        CleanUpTimerQueries_();
    }

    // 전체화면 상태로 스왑체인을 놓으면 즉시 크래시한다. 모두 창모드로 변경
    for (FrameBufferD3D11& frameBuffer: m_frameBuffers.GetValues())
    {
        if (frameBuffer.pSwapChainOrNull)
        {
            JUG_DX_CHECK(frameBuffer.pSwapChainOrNull->SetFullscreenState(FALSE, nullptr));
        }
    }

    for (FrameBufferD3D11& frameBuffer: m_frameBuffers.GetValues())
    {
        for (ID3D11RenderTargetView*& pRTV: frameBuffer.rtvs)
        {
            JUG_DX_RELEASE(pRTV);
        }
        JUG_DX_RELEASE(frameBuffer.pDsvOrNull);
        JUG_DX_RELEASE(frameBuffer.pSwapChainOrNull);
    }

    for (VertexBufferD3D11& vertexBuffer: m_vertexBuffers.GetValues())
    {
        JUG_DX_RELEASE(vertexBuffer.pBuffer);
    }

    for (InstanceBufferD3D11& instanceBuffer: m_instanceBuffers.GetValues())
    {
        JUG_DX_RELEASE(instanceBuffer.pBuffer);
    }

    for (IndexBufferD3D11& indexBuffer: m_indexBuffers.GetValues())
    {
        JUG_DX_RELEASE(indexBuffer.pBuffer);
    }

    for (ConstantBufferD3D11& constantBuffer: m_constantBuffers.GetValues())
    {
        JUG_DX_RELEASE(constantBuffer.pBuffer);
    }

    for (StorageBufferD3D11& storageBuffer: m_storageBuffers.GetValues())
    {
        JUG_DX_RELEASE(storageBuffer.pSrvOrNull);
        JUG_DX_RELEASE(storageBuffer.pUavOrNull);
        JUG_DX_RELEASE(storageBuffer.pBuffer);
    }

    for (TextureD3D11& texture: m_textures.GetValues())
    {
        JUG_DX_RELEASE(texture.pSrvOrNull);
        JUG_DX_RELEASE(texture.pUavOrNull);
        JUG_DX_RELEASE(texture.pMsaaRtResourceOrNull);
        JUG_DX_RELEASE(texture.pResource);
    }

    for (ShaderD3D11& shader: m_shaders.GetValues())
    {
        JUG_DX_RELEASE(shader.pVS);   // 유니온이라 아무 멤버나 놔도 된다.
    }

    m_frameBuffers.Clear();
    m_vertexBuffers.Clear();
    m_instanceBuffers.Clear();
    m_indexBuffers.Clear();
    m_constantBuffers.Clear();
    m_storageBuffers.Clear();
    m_textures.Clear();
    m_shaders.Clear();

    for (const auto& [key, pInputLayout]: m_d3d11InputLayoutCache)
    {
        if (pInputLayout->Release() != 0)
        {
            JUG_ASSERT(false, "Failed to release D3D11 input layout. There may be a resource leak.");
        }
    }

    for (const auto& [key, pState]: m_d3d11BlendStateCache)
    {
        if (pState->Release() != 0)
        {
            JUG_ASSERT(false, "Failed to release D3D11 blend state. There may be a resource leak.");
        }
    }

    for (const auto& [key, pState]: m_d3d11RasterizerStateCache)
    {
        if (pState->Release() != 0)
        {
            JUG_ASSERT(false, "Failed to release D3D11 rasterizer state. There may be a resource leak.");
        }
    }

    for (const auto& [key, pState]: m_d3d11DepthStencilStateCache)
    {
        if (pState->Release() != 0)
        {
            JUG_ASSERT(false, "Failed to release D3D11 depth stencil state. There may be a resource leak.");
        }
    }

    for (const auto& [key, pState]: m_d3d11SamplerStateCache)
    {
        if (pState->Release() != 0)
        {
            JUG_ASSERT(false, "Failed to release D3D11 sampler state. There may be a resource leak.");
        }
    }

    JUG_DX_RELEASE(m_pUploadBuffer);
    JUG_DX_RELEASE(m_pUserAnnotationOrNull);
    JUG_DX_RELEASE(m_pD3d11DeviceContext);
    JUG_DX_RELEASE(m_pD3d11Device);
    if (m_pD3d11DebugOrNull)
    {
        // info queue 를 아직 쥐고 있어야 리포트가 로그로 넘어온다. 그래서 디바이스 refcount 에 info queue 몫 1 이 더 보인다.
        JUG_DX_CHECK(m_pD3d11DebugOrNull->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL));
        JUG_DX_RELEASE(m_pD3d11DebugOrNull);
    }
    FlushDebugMessages_();
    JUG_DX_RELEASE(m_pD3d11InfoQueueOrNull);
}

// ===========================================
//  Init
// ===========================================

void Graphics_D3D11::InitDevice_()
{
    bool bDebug = m_caps.flags.Has(eGraphicsOption::Debug);

    // flags
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (bDebug)
    {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    IAdapter*                       pAdapterOrNull = m_dxgi.GetAdapterOrNull();
    const ARRAY<D3D_DRIVER_TYPE, 4> driverTypes    = { D3D_DRIVER_TYPE_UNKNOWN, D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_REFERENCE };

    size_t i = pAdapterOrNull ? 0 : 1;
    for (; i < driverTypes.size(); ++i)
    {
        const D3D_DRIVER_TYPE type = driverTypes[i];
        HRESULT               hr   = CreateD3d11Device_(pAdapterOrNull, type, flags, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);
        if (SUCCEEDED(hr))
        {
            break;
        }

        if (bDebug)
        {
            JUG_CORE_LOG_WARN("D3D11 debug layer is not available. Retrying without it. ({})", MakeErrorMessage_(hr));
            hr = CreateD3d11Device_(pAdapterOrNull, type, flags & ~D3D11_CREATE_DEVICE_DEBUG, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);
            if (SUCCEEDED(hr))
            {
                bDebug = false;
                break;
            }
        }

        JUG_CORE_LOG_WARN("Failed to create a D3D11 device with driver type '{}'. ({})", ToString_(type), MakeErrorMessage_(hr));
    }

    // debug layer
    m_caps.flags.Set(eGraphicsOption::Debug, bDebug);

    // graphics driver
    if (i == driverTypes.size())
    {
        JUG_FATAL("Failed to create a D3D11 device with any driver type.");
    }
    m_d3dDriverType = driverTypes[i];

    // is software rasterizer?
    m_caps.bSoftwareRasterizer = m_d3dDriverType == D3D_DRIVER_TYPE_WARP || m_d3dDriverType == D3D_DRIVER_TYPE_REFERENCE;

    if (m_caps.bSoftwareRasterizer)
    {
        m_caps.vendorID               = 0;
        m_caps.deviceID               = 0;
        m_caps.videoMemorySize        = 0;
        m_caps.systemMemorySize       = 0;
        m_caps.sharedSystemMemorySize = 0;
    }

    // name
    SetD3d11ObjectName_(m_pD3d11DeviceContext, "JugGfx.DirectX11.ImmediateContext");

    // log
    JUG_CORE_LOG_INFO("D3D11 device: feature level {}, driver {}{}, debug layer {}",
                      ToString_(m_d3dFeatureLevel),
                      ToString_(m_d3dDriverType),
                      m_caps.bSoftwareRasterizer ? " (software rasterizer)" : "",
                      bDebug ? "enabled" : "disabled");
}

void Graphics_D3D11::InitDebugInterfaces_()
{
    JUG_DX_CHECK(m_pD3d11DeviceContext->QueryInterface(IID_PPV_ARGS(&m_pUserAnnotationOrNull)));
    JUG_DX_CHECK(m_pD3d11Device->QueryInterface(IID_PPV_ARGS(&m_pD3d11DebugOrNull)));
    JUG_DX_CHECK(m_pD3d11Device->QueryInterface(IID_PPV_ARGS(&m_pD3d11InfoQueueOrNull)));

    if (m_pD3d11InfoQueueOrNull && ::IsDebuggerPresent())
    {
        JUG_DX_CHECK(m_pD3d11InfoQueueOrNull->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE));
        JUG_DX_CHECK(m_pD3d11InfoQueueOrNull->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE));
    }

    JUG_CORE_LOG_INFO("D3D11 debug: annotation {}, debug {}, info queue {}, break on error {}",
                      (m_pUserAnnotationOrNull != nullptr) ? "on" : "off",
                      (m_pD3d11DebugOrNull != nullptr) ? "on" : "off",
                      (m_pD3d11InfoQueueOrNull != nullptr) ? "on" : "off",
                      (m_pD3d11InfoQueueOrNull != nullptr && ::IsDebuggerPresent() != FALSE) ? "on" : "off");
}

// ===========================================
//  Diagnostics
// ===========================================

void Graphics_D3D11::FlushDebugMessages_()
{
    if (!m_pD3d11InfoQueueOrNull)
    {
        return;
    }

    // 매 프레임 호출되므로 빈 큐면 바로 나간다.
    const UINT64 numMsgs    = m_pD3d11InfoQueueOrNull->GetNumStoredMessagesAllowedByRetrievalFilter();
    const UINT64 numDropped = m_pD3d11InfoQueueOrNull->GetNumMessagesDiscardedByMessageCountLimit();
    if (numMsgs == 0 && numDropped == m_numDebugMsgsDropped)
    {
        return;
    }

    alignas(D3D11_MESSAGE) ARRAY<std::byte, 4096> buf;
    for (UINT64 i = 0; i < numMsgs; ++i)
    {
        size_t         size = buf.size();
        D3D11_MESSAGE* pMsg = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (FAILED(m_pD3d11InfoQueueOrNull->GetMessage(i, pMsg, &size)))
        {
            continue;
        }

        const StringView desc = { pMsg->pDescription, pMsg->DescriptionByteLength };
        JUG_CORE_LOG(ToLogLevel_(pMsg->Severity), "D3D11 {} #{} (frame {}): {}", ToString_(pMsg->Category), static_cast<int>(pMsg->ID), m_frameIndex, desc);
    }
    m_pD3d11InfoQueueOrNull->ClearStoredMessages();

    if (numDropped > m_numDebugMsgsDropped)
    {
        JUG_CORE_LOG_WARN("D3D11 debug layer dropped {} messages (queue limit {}).", numDropped - m_numDebugMsgsDropped, m_pD3d11InfoQueueOrNull->GetMessageCountLimit());
    }
    m_numDebugMsgsDropped = m_pD3d11InfoQueueOrNull->GetNumMessagesDiscardedByMessageCountLimit();
}

void Graphics_D3D11::InitTimerQueries_()
{
    D3D11_QUERY_DESC tsqd = {};
    tsqd.Query            = D3D11_QUERY_TIMESTAMP;

    D3D11_QUERY_DESC dqd = {};
    dqd.Query            = D3D11_QUERY_TIMESTAMP_DISJOINT;

    for (size_t i = 0; i < m_timerQueries.GetMaxSize(); ++i)
    {
        TimerQuery query = {};
        JUG_DX_CHECK(m_pD3d11Device->CreateQuery(&dqd, &query.pDisjoint));
        JUG_DX_CHECK(m_pD3d11Device->CreateQuery(&tsqd, &query.pBegin));
        JUG_DX_CHECK(m_pD3d11Device->CreateQuery(&tsqd, &query.pEnd));
        m_timerQueries.PushBack(query);
    }

    JUG_CORE_LOG_INFO("GPU timer queries created. count = {}", m_timerQueries.GetSize());
}

void Graphics_D3D11::CleanUpTimerQueries_()
{
    while (!m_timerQueries.IsEmpty())
    {
        TimerQuery query = m_timerQueries.Front();
        m_timerQueries.PopFront();
        JUG_DX_RELEASE(query.pDisjoint);
        JUG_DX_RELEASE(query.pBegin);
        JUG_DX_RELEASE(query.pEnd);
    }
}

// ===========================================
//  System
// ===========================================

void Graphics_D3D11::Frame_()
{
    // present
    for (const FrameBufferHandle fbh: m_swapChainFbhs)
    {
        ISwapChain* pSwapChain = m_frameBuffers[fbh].pSwapChainOrNull;
        if (m_frameBufferPool[fbh].bVSync)
        {
            JUG_DX_CHECK(pSwapChain->Present(1, 0));
        }
        else
        {
            JUG_DX_CHECK(pSwapChain->Present(0, m_caps.bAllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0));
        }
    }

    // timer query.
    if (m_caps.flags & eGraphicsOption::ProfileFrameTime)
    {
        // 가장 최근 쿼리 종료
        TimerQuery& last = m_timerQueries.Back();
        if (last.bIssued && !last.bEnded)
        {
            m_pD3d11DeviceContext->End(last.pEnd);
            m_pD3d11DeviceContext->End(last.pDisjoint);
            last.bEnded = true;
        }

        // 가장 오래된 쿼리 결과 가져오기.
        TimerQuery& front = m_timerQueries.Front();
        if (front.bIssued)
        {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
            UINT64                              begin;
            UINT64                              end;
            if (m_pD3d11DeviceContext->GetData(front.pDisjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
                && m_pD3d11DeviceContext->GetData(front.pBegin, &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
                && m_pD3d11DeviceContext->GetData(front.pEnd, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
            {
                if (!disjoint.Disjoint)   // 조회 성공
                {
                    m_stats.gpuTimerBegin   = begin;
                    m_stats.gpuTimerEnd     = end;
                    m_stats.gpuTimerFreq    = disjoint.Frequency;
                    m_stats.gpuTimerLatency = m_frameIndex - front.frameIndex;
                }
                front.bIssued = false;
            }
        }
        else
        {
            TimerQuery next = front;
            m_timerQueries.PopFront();

            m_pD3d11DeviceContext->Begin(next.pDisjoint);
            m_pD3d11DeviceContext->End(next.pBegin);
            next.frameIndex = m_frameIndex;
            next.bIssued    = true;
            next.bEnded     = false;
            m_timerQueries.PushBack(next);
        }
    }
    else
    {

        m_stats.gpuTimerBegin   = 0;
        m_stats.gpuTimerEnd     = 0;
        m_stats.gpuTimerFreq    = 0;
        m_stats.gpuTimerLatency = 0;
    }

    // GPU 메모리 사용량 갱신
    if (m_caps.flags & eGraphicsOption::ProfileGpuMemory && m_dxgi.GetAdapterOrNull())
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO vmi;
        if (SUCCEEDED(m_dxgi.GetAdapterOrNull()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi)))
        {
            m_stats.gpuMemoryUsed = vmi.CurrentUsage;
            m_stats.gpuMemorySize = vmi.Budget;
        }
    }

    // readback. 요청이 들어온 프레임에는 GPU 가 일찍 시작하도록 한 번 Flush 한다.
    if (m_bReadbackNeedFlush)
    {
        m_pD3d11DeviceContext->Flush();
    }

    // 이번 프레임에 쌓인 디버그 메시지를 출력
    FlushDebugMessages_();
}

void Graphics_D3D11::CompleteReadback_(
    const ReadbackRequest& _request)
{
    ID3D11Resource* pResource = nullptr;
    if (_request.resource.GetType() == eResource::Texture)
    {
        pResource = m_textures[_request.resource.GetTextureHandle()].pResource;
    }
    else
    {
        pResource = m_storageBuffers[_request.resource.GetStorageBufferHandle()].pBuffer;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(pResource, _request.index, D3D11_MAP_READ, 0, &mapped));

    const std::byte* pSrcBase = static_cast<const std::byte*>(mapped.pData);
    std::byte*       pDstBase = _request.dst.GetPtr();

    size_t written = 0;
    for (uint32_t z = 0; z < _request.depth; ++z)
    {
        for (uint32_t y = 0; y < _request.numRows; ++y)
        {
            const std::byte* pSrc = pSrcBase + static_cast<size_t>(z) * mapped.DepthPitch + static_cast<size_t>(y) * mapped.RowPitch;
            std::memcpy(pDstBase + written, pSrc, _request.rowPitch);
            written += _request.rowPitch;
        }
    }

    m_pD3d11DeviceContext->Unmap(pResource, _request.index);
}

// ===========================================
//  Create
// ===========================================

void Graphics_D3D11::CreateVertexBuffer_(
    const VertexBufferHandle _vbh,
    const MemoryView         _initDataOrEmpty)
{
    const VertexBufferDesc& vb = m_vertexBufferPool[_vbh];

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = vb.byteWidth;
    bd.Usage             = vb.bDynamic                ? D3D11_USAGE_DYNAMIC
                         : _initDataOrEmpty.IsEmpty() ? D3D11_USAGE_DEFAULT
                                                      : D3D11_USAGE_IMMUTABLE;
    bd.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags    = vb.bDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    bd.MiscFlags         = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem                = _initDataOrEmpty.GetPtr();

    VertexBufferD3D11& d3d11Vb = m_vertexBuffers.Emplace(_vbh);
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, _initDataOrEmpty.IsEmpty() ? nullptr : &initData, &d3d11Vb.pBuffer));
}

void Graphics_D3D11::CreateInstanceBuffer_(
    const InstanceBufferHandle _instbh)
{
    const InstanceBufferDesc& instb = m_instanceBufferPool[_instbh];

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = instb.byteWidth;
    bd.Usage             = D3D11_USAGE_DYNAMIC;
    bd.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags         = 0;

    InstanceBufferD3D11& d3d11Instb = m_instanceBuffers.Emplace(_instbh);
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &d3d11Instb.pBuffer));
}

void Graphics_D3D11::CreateIndexBuffer_(
    const IndexBufferHandle _ibh,
    const MemoryView        _initDataOrEmpty)
{
    const IndexBufferDesc& ib = m_indexBufferPool[_ibh];

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = ib.byteWidth;
    bd.Usage             = ib.bDynamic                ? D3D11_USAGE_DYNAMIC
                         : _initDataOrEmpty.IsEmpty() ? D3D11_USAGE_DEFAULT
                                                      : D3D11_USAGE_IMMUTABLE;
    bd.BindFlags         = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags    = ib.bDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    bd.MiscFlags         = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem                = _initDataOrEmpty.GetPtr();

    IndexBufferD3D11& d3d11Ib = m_indexBuffers.Emplace(_ibh);
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, _initDataOrEmpty.IsEmpty() ? nullptr : &initData, &d3d11Ib.pBuffer));
}

void Graphics_D3D11::CreateConstantBuffer_(
    const ConstantBufferHandle _cbh)
{
    const ConstantBufferDesc& cb = m_constantBufferPool[_cbh];

    D3D11_BUFFER_DESC bd   = {};
    bd.ByteWidth           = cb.byteWidth;
    bd.Usage               = D3D11_USAGE_DYNAMIC;
    bd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags           = 0;
    bd.StructureByteStride = 0;

    ConstantBufferD3D11& d3d11Cb = m_constantBuffers.Emplace(_cbh);
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &d3d11Cb.pBuffer));
}

void Graphics_D3D11::CreateStorageBuffer_(
    const StorageBufferHandle _sbh,
    const MemoryView          _initDataOrEmpty)
{
    const StorageBufferDesc& sb      = m_storageBufferPool[_sbh];
    StorageBufferD3D11&      d3d11Sb = m_storageBuffers.Emplace(_sbh);

    switch (sb.type)
    {
        case eStorageBuffer::Structured:
        {
            const uint32_t numElements = sb.byteWidth / sb.stride;

            D3D11_BUFFER_DESC bd   = {};
            bd.ByteWidth           = sb.byteWidth;
            bd.CPUAccessFlags      = sb.flags & eStorageBufferOption::Dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
            bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sb.stride;
            bd.Usage               = sb.flags & eStorageBufferOption::Dynamic ? D3D11_USAGE_DYNAMIC
                                   : _initDataOrEmpty.IsEmpty()               ? D3D11_USAGE_DEFAULT
                                                                              : D3D11_USAGE_IMMUTABLE;
            // uav의 경우 default usage만 가능.
            if (sb.flags & eStorageBufferOption::ShaderReadWrite)
            {
                bd.Usage = D3D11_USAGE_DEFAULT;
            }

            // bind flags
            bd.BindFlags = 0;
            if (sb.flags & eStorageBufferOption::ShaderRead)
            {
                bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            }
            if (sb.flags & eStorageBufferOption::ShaderReadWrite)
            {
                bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            }

            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem                = _initDataOrEmpty.GetPtr();
            JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, _initDataOrEmpty.IsEmpty() ? nullptr : &initData, &d3d11Sb.pBuffer));

            // create SRV
            if (sb.flags & eStorageBufferOption::ShaderRead)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
                srvd.Format                          = DXGI_FORMAT_UNKNOWN;
                srvd.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
                srvd.Buffer.FirstElement             = 0;
                srvd.Buffer.NumElements              = numElements;
                JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(d3d11Sb.pBuffer, &srvd, &d3d11Sb.pSrvOrNull));
            }

            // create UAV
            if (sb.flags & eStorageBufferOption::ShaderReadWrite)
            {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
                uavd.Format                           = DXGI_FORMAT_UNKNOWN;
                uavd.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
                uavd.Buffer.FirstElement              = 0;
                uavd.Buffer.NumElements               = numElements;
                JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(d3d11Sb.pBuffer, &uavd, &d3d11Sb.pUavOrNull));
            }
        }
        break;

        case eStorageBuffer::IndirectArgs:
        {
            D3D11_BUFFER_DESC bd   = {};
            bd.ByteWidth           = sb.byteWidth;
            bd.Usage               = D3D11_USAGE_DEFAULT;
            bd.MiscFlags           = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
            bd.StructureByteStride = 0;

            // bind flags
            bd.BindFlags = 0;
            if (sb.flags & eStorageBufferOption::ShaderRead)
            {
                bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            }
            if (sb.flags & eStorageBufferOption::ShaderReadWrite)
            {
                bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            }

            // create buffer
            JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &d3d11Sb.pBuffer));

            // use typed-buffer for indirect args
            const uint32_t numU32 = sb.byteWidth / 4;

            // create SRV
            if (sb.flags & eStorageBufferOption::ShaderRead)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
                srvd.Format                          = DXGI_FORMAT_R32_UINT;
                srvd.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
                srvd.Buffer.FirstElement             = 0;
                srvd.Buffer.NumElements              = numU32;
                JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(d3d11Sb.pBuffer, &srvd, &d3d11Sb.pSrvOrNull));
            }

            // create UAV
            if (sb.flags & eStorageBufferOption::ShaderReadWrite)
            {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
                uavd.Format                           = DXGI_FORMAT_R32_UINT;
                uavd.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
                uavd.Buffer.FirstElement              = 0;
                uavd.Buffer.NumElements               = numU32;
                uavd.Buffer.Flags                     = 0;
                JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(d3d11Sb.pBuffer, &uavd, &d3d11Sb.pUavOrNull));
            }
        }
        break;

        case eStorageBuffer::Readback:
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth         = sb.byteWidth;
            bd.Usage             = D3D11_USAGE_STAGING;
            bd.BindFlags         = 0;
            bd.CPUAccessFlags    = D3D11_CPU_ACCESS_READ;
            bd.MiscFlags         = 0;
            JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &d3d11Sb.pBuffer));
        }
        break;
    }
}

DXGI_SAMPLE_DESC Graphics_D3D11::MakeSampleDesc_(const DXGI_FORMAT _format, const eMSAA _msaa) const
{
    JUG_ASSERT(m_pD3d11Device && _format != DXGI_FORMAT_UNKNOWN, "Invalid device or format.");

    constexpr ENUM_ARRAY<eMSAA, uint32_t> kMsaaSamples = { 1, 2, 4, 8, 16 };
    constexpr ENUM_ARRAY<eMSAA, eMSAA>    kLowerMSAAs  = { eMSAA::None, eMSAA::None, eMSAA::x2, eMSAA::x4, eMSAA::x8 };

    // 최대한 높은 샘플링 품질을 사용하도록 설정
    DXGI_SAMPLE_DESC sd = {};
    for (eMSAA msaa = _msaa; msaa != eMSAA::None; msaa = kLowerMSAAs[msaa])
    {
        sd.Count = kMsaaSamples[msaa];

        UINT numLevels = 0;
        if (SUCCEEDED(m_pD3d11Device->CheckMultisampleQualityLevels(_format, sd.Count, &numLevels)) && numLevels > 0)
        {
            sd.Quality = numLevels - 1;
            return sd;
        }
    }

    sd.Count   = 1;
    sd.Quality = 0;
    return sd;
}

void Graphics_D3D11::CreateTexture_(
    const TextureHandle _texh,
    const MemoryView    _initDataOrEmpty)
{
    TextureDesc& desc = m_texturePool[_texh];

    const bool bDepth    = IsDepthFormat(desc.format);
    const bool bReadback = desc.flags.Has(eTextureOption::Readback);

    const TextureFormatInfo formatInfo = MakeTextureFormatInfo_(desc.format);
    DXGI_SAMPLE_DESC        sd         = MakeSampleDesc_(formatInfo.tex, desc.msaa);

    // 지원하지 않는 MSAA 는 하향된다. 최종 값을 desc 에 기록한다.
    switch (sd.Count)
    {
        case 1: desc.msaa = eMSAA::None; break;
        case 2: desc.msaa = eMSAA::x2; break;
        case 4: desc.msaa = eMSAA::x4; break;
        case 8: desc.msaa = eMSAA::x8; break;
        case 16: desc.msaa = eMSAA::x16; break;
        default: JUG_ASSERT(false, "Unsupported MSAA count: {}", sd.Count);
    }

    TextureD3D11& texture = m_textures.Emplace(_texh);

    const bool bMSAA = desc.msaa != eMSAA::None;

    // bind flags
    UINT bindFlags = 0;
    UINT miscFlags = 0;

    if (!desc.flags.Has(eTextureOption::ShaderWriteOnly))
    {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (desc.flags & eTextureOption::ShaderReadWrite)
    {
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    if (desc.flags & eTextureOption::RenderTarget)
    {
        bindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (desc.flags & eTextureOption::DepthStencil)
    {
        bindFlags |= D3D11_BIND_DEPTH_STENCIL;
    }
    if (desc.type == eTexture::TextureCube)
    {
        miscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    }

    constexpr UINT kGenMips = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (desc.numMips > 1 && !bDepth && (bindFlags & kGenMips) == kGenMips)
    {
        miscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }

    constexpr UINT kWritableBinds = D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS;
    desc.bImmutable               = !_initDataOrEmpty.IsEmpty() && (bindFlags & kWritableBinds) == 0 && (miscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) == 0;

    if (bReadback)
    {
        bindFlags = 0;
        miscFlags = 0;
    }

    const UINT        cpuAccessFlags = bReadback ? D3D11_CPU_ACCESS_READ : 0;
    const D3D11_USAGE usage          = bReadback       ? D3D11_USAGE_STAGING
                                     : desc.bImmutable ? D3D11_USAGE_IMMUTABLE
                                                       : D3D11_USAGE_DEFAULT;

    // init data
    D3D11_SUBRESOURCE_DATA* pInitDataOrNull = nullptr;
    if (!_initDataOrEmpty.IsEmpty())
    {
        const uint32_t numSubs = desc.numMips * desc.numLayers;
        pInitDataOrNull        = static_cast<D3D11_SUBRESOURCE_DATA*>(JUG_STACK_ALLOC(sizeof(D3D11_SUBRESOURCE_DATA) * numSubs));

        // [layer][mip] 연속 배치
        const std::byte* pBase  = _initDataOrEmpty.GetPtr();
        size_t           offset = 0;
        for (uint32_t layer = 0; layer < desc.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < desc.numMips; ++mip)
            {
                const uint32_t index      = mip + layer * desc.numMips;
                const VEC3U    size       = CalcTextureSize(desc.width, desc.height, desc.depth, mip);
                const uint32_t rowPitch   = CalcRowPitch(desc.format, size.x);
                const uint32_t slicePitch = CalcSlicePitch(desc.format, size.x, size.y);

                pInitDataOrNull[index].pSysMem          = pBase + offset;
                pInitDataOrNull[index].SysMemPitch      = rowPitch;
                pInitDataOrNull[index].SysMemSlicePitch = slicePitch;
                offset += static_cast<size_t>(slicePitch) * size.z;
            }
        }
        JUG_ASSERT(_initDataOrEmpty.GetSize() == offset, "Initial data size ({}) does not match the expected size ({}) for {} layers and {} mips.", _initDataOrEmpty.GetSize(), offset, desc.numLayers, desc.numMips);
    }

    // d3d11 texture
    switch (desc.type)
    {
        case eTexture::Texture2D:
        case eTexture::TextureCube:
        {
            D3D11_TEXTURE2D_DESC texd = {};
            texd.Width                = desc.width;
            texd.Height               = desc.height;
            texd.MipLevels            = desc.numMips;
            texd.ArraySize            = desc.numLayers;
            texd.Format               = formatInfo.tex;
            texd.SampleDesc.Count     = 1;
            texd.SampleDesc.Quality   = 0;
            texd.Usage                = usage;
            texd.BindFlags            = bindFlags;
            texd.CPUAccessFlags       = cpuAccessFlags;
            texd.MiscFlags            = miscFlags;

            JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&texd, pInitDataOrNull, &texture.pTexture2D));

            if (bMSAA)
            {
                D3D11_TEXTURE2D_DESC msaad = {};
                msaad.Width                = desc.width;
                msaad.Height               = desc.height;
                msaad.MipLevels            = 1;
                msaad.ArraySize            = desc.numLayers;
                msaad.Format               = formatInfo.tex;
                msaad.SampleDesc           = sd;
                msaad.Usage                = D3D11_USAGE_DEFAULT;
                msaad.BindFlags            = bindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL);
                msaad.CPUAccessFlags       = 0;
                msaad.MiscFlags            = miscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE;

                JUG_ASSERT(msaad.BindFlags != 0, "An MSAA texture must be a render target or a depth-stencil.");
                JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&msaad, nullptr, &texture.pMsaaRtTexture2DOrNull));
            }
        }
        break;

        case eTexture::Texture3D:
        {
            D3D11_TEXTURE3D_DESC texd = {};
            texd.Width                = desc.width;
            texd.Height               = desc.height;
            texd.Depth                = desc.depth;
            texd.MipLevels            = desc.numMips;
            texd.Format               = formatInfo.tex;
            texd.Usage                = usage;
            texd.BindFlags            = bindFlags;
            texd.CPUAccessFlags       = cpuAccessFlags;
            texd.MiscFlags            = miscFlags;

            JUG_DX_CHECK(m_pD3d11Device->CreateTexture3D(&texd, pInitDataOrNull, &texture.pTexture3D));
        }
        break;

        default:
            JUG_ASSERT(false, "Invalid texture m_type.");
            return;
    }

    if (bReadback)   // 스테이징 텍스처는 view를 가질 수 없다.
    {
        return;
    }

    // shader resource view
    if ((bindFlags & D3D11_BIND_SHADER_RESOURCE) != 0)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                          = formatInfo.srv;

        switch (desc.type)
        {
            case eTexture::Texture2D:
                if (desc.numLayers > 1)
                {
                    srvd.ViewDimension            = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    srvd.Texture2DArray.MipLevels = desc.numMips;
                    srvd.Texture2DArray.ArraySize = desc.numLayers;
                }
                else
                {
                    srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvd.Texture2D.MipLevels = desc.numMips;
                }
                break;

            case eTexture::TextureCube:
                if (desc.numLayers > 6)
                {
                    srvd.ViewDimension              = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                    srvd.TextureCubeArray.MipLevels = desc.numMips;
                    srvd.TextureCubeArray.NumCubes  = desc.numLayers / 6;
                }
                else
                {
                    srvd.ViewDimension         = D3D11_SRV_DIMENSION_TEXTURECUBE;
                    srvd.TextureCube.MipLevels = desc.numMips;
                }
                break;

            case eTexture::Texture3D:
                srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE3D;
                srvd.Texture3D.MipLevels = desc.numMips;
                break;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(texture.pResource, &srvd, &texture.pSrvOrNull));
    }

    // unordered access view
    if ((bindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format                           = formatInfo.srv;

        if (desc.type == eTexture::Texture3D)
        {
            uavd.ViewDimension   = D3D11_UAV_DIMENSION_TEXTURE3D;
            uavd.Texture3D.WSize = desc.depth;
        }
        else if (desc.numLayers > 1)
        {
            uavd.ViewDimension            = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavd.Texture2DArray.ArraySize = desc.numLayers;
        }
        else
        {
            uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(texture.pResource, &uavd, &texture.pUavOrNull));
    }

    return;
}

void Graphics_D3D11::CreateFrameBuffer_(
    const FrameBufferHandle _fbh)
{
    const FrameBufferDesc& desc = m_frameBufferPool[_fbh];
    FrameBufferD3D11&      fb   = m_frameBuffers.Emplace(_fbh);

    const uint32_t numAtts = desc.GetNumAttachments();
    for (uint32_t i = 0; i < numAtts; ++i)
    {
        const Attachment&   att     = desc.atts[i];
        const TextureDesc&  texDesc = m_texturePool[att.texh];
        const TextureD3D11& texture = m_textures[att.texh];

        // MSAA 텍스처는 멀티샘플 표면에 그리고 나중에 단일 샘플 쪽으로 리졸브한다.
        const bool              bMSAA      = texture.pMsaaRtResourceOrNull != nullptr;
        ID3D11Resource* const   pResource  = bMSAA ? texture.pMsaaRtResourceOrNull : texture.pResource;
        const TextureFormatInfo formatInfo = MakeTextureFormatInfo_(texDesc.format);
        const bool              bArray     = texDesc.type != eTexture::Texture3D && (texDesc.numLayers > 1 || att.numLayers > 1);

        // 깊이는 컬러 뒤 슬롯에 있다.
        if (i >= desc.numRts)
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
            dsvd.Format                        = formatInfo.dsv;
            if (bMSAA)
            {
                dsvd.ViewDimension                    = bArray ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS;
                dsvd.Texture2DMSArray.FirstArraySlice = att.offset;
                dsvd.Texture2DMSArray.ArraySize       = att.numLayers;
            }
            else if (bArray)
            {
                dsvd.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvd.Texture2DArray.MipSlice        = att.mip;
                dsvd.Texture2DArray.FirstArraySlice = att.offset;
                dsvd.Texture2DArray.ArraySize       = att.numLayers;
            }
            else
            {
                dsvd.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
                dsvd.Texture2D.MipSlice = att.mip;
            }
            JUG_DX_CHECK(m_pD3d11Device->CreateDepthStencilView(pResource, &dsvd, &fb.pDsvOrNull));
            continue;
        }

        D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
        rtvd.Format                        = formatInfo.rtv;
        if (texDesc.type == eTexture::Texture3D)
        {
            rtvd.ViewDimension         = D3D11_RTV_DIMENSION_TEXTURE3D;
            rtvd.Texture3D.MipSlice    = att.mip;
            rtvd.Texture3D.FirstWSlice = att.offset;
            rtvd.Texture3D.WSize       = att.numLayers;
        }
        else if (bMSAA)
        {
            rtvd.ViewDimension                    = bArray ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
            rtvd.Texture2DMSArray.FirstArraySlice = att.offset;
            rtvd.Texture2DMSArray.ArraySize       = att.numLayers;
        }
        else if (bArray)
        {
            rtvd.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvd.Texture2DArray.MipSlice        = att.mip;
            rtvd.Texture2DArray.FirstArraySlice = att.offset;
            rtvd.Texture2DArray.ArraySize       = att.numLayers;
        }
        else
        {
            rtvd.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvd.Texture2D.MipSlice = att.mip;
        }
        JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(pResource, &rtvd, &fb.rtvs[i]));
    }
}

void Graphics_D3D11::CreateSwapChain_(
    const FrameBufferHandle _fbh)
{
    const FrameBufferDesc& desc    = m_frameBufferPool[_fbh];
    const TextureHandle    texh    = desc.atts[0].texh;
    const TextureDesc&     texDesc = m_texturePool[texh];

    // create swap chain
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width                 = texDesc.width;
    scd.Height                = texDesc.height;
    scd.Format                = MakeTextureFormatInfo_(ToLinearFormat(texDesc.format)).srv;
    scd.Stereo                = FALSE;
    scd.SampleDesc.Count      = 1;
    scd.SampleDesc.Quality    = 0;
    scd.BufferCount           = desc.numBuffers;
    scd.Scaling               = DXGI_SCALING_NONE;
    scd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags                 = m_caps.bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    scd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    const HWND  hWnd       = static_cast<HWND>(GetNativeWindowHandle(desc.wndID));
    ISwapChain* pSwapChain = m_dxgi.CreateSwapChain(m_pD3d11Device, hWnd, true, scd);

    // alt + enter 금지.
    JUG_DX_CHECK(m_dxgi.GetFactory()->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));

    // back buffer texture
    TextureD3D11& texture = m_textures.Emplace(texh);
    JUG_DX_CHECK(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&texture.pTexture2D)));

    // create frame buffer
    FrameBufferD3D11& fb = m_frameBuffers.Emplace(_fbh);
    fb.pSwapChainOrNull  = pSwapChain;

    // create rtv
    D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format                        = MakeTextureFormatInfo_(texDesc.format).rtv;
    rtvd.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvd.Texture2D.MipSlice            = 0;
    JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(texture.pResource, &rtvd, fb.rtvs.data()));
}

void Graphics_D3D11::CreateShader_(
    const ShaderHandle    _sh,
    const ShaderBytecode& _bytecode)
{
    const MemoryView bytecode = _bytecode.GetBytecode();
    JUG_ASSERT(bytecode.GetSize() > kShaderBytecodeHeaderSize, "Shader bytecode is too small to be a DXBC container.");
    JUG_ASSERT(*reinterpret_cast<const uint32_t*>(bytecode.GetPtr()) == kDxbcMagic, "Shader bytecode is not a DXBC container.");

    ShaderD3D11& shader = m_shaders.Emplace(_sh);
    switch (m_shaderPool[_sh].type)
    {
        case eShader::Vertex:
            JUG_DX_CHECK(m_pD3d11Device->CreateVertexShader(bytecode.GetPtr(), bytecode.GetSize(), nullptr, &shader.pVS));
            break;

        case eShader::Pixel:
            JUG_DX_CHECK(m_pD3d11Device->CreatePixelShader(bytecode.GetPtr(), bytecode.GetSize(), nullptr, &shader.pPS));
            break;

        case eShader::Compute:
            JUG_DX_CHECK(m_pD3d11Device->CreateComputeShader(bytecode.GetPtr(), bytecode.GetSize(), nullptr, &shader.pCS));
            break;

        default:
            JUG_ASSERT(false, "Unknown shader stage.");
            break;
    }
}

// ===========================================
//  Destroy
// ===========================================

void Graphics_D3D11::Destroy_(
    const VertexBufferHandle _vbh)
{
    JUG_DX_RELEASE(m_vertexBuffers[_vbh].pBuffer);
    m_vertexBuffers.Erase(_vbh);
}

void Graphics_D3D11::Destroy_(
    const InstanceBufferHandle _instbh)
{
    JUG_DX_RELEASE(m_instanceBuffers[_instbh].pBuffer);
    m_instanceBuffers.Erase(_instbh);
}

void Graphics_D3D11::Destroy_(
    const IndexBufferHandle _ibh)
{
    JUG_DX_RELEASE(m_indexBuffers[_ibh].pBuffer);
    m_indexBuffers.Erase(_ibh);
}

void Graphics_D3D11::Destroy_(
    const ConstantBufferHandle _cbh)
{
    JUG_DX_RELEASE(m_constantBuffers[_cbh].pBuffer);
    m_constantBuffers.Erase(_cbh);
}

void Graphics_D3D11::Destroy_(
    const StorageBufferHandle _sbh)
{
    StorageBufferD3D11& sb = m_storageBuffers[_sbh];
    JUG_DX_RELEASE(sb.pSrvOrNull);
    JUG_DX_RELEASE(sb.pUavOrNull);
    JUG_DX_RELEASE(sb.pBuffer);
    m_storageBuffers.Erase(_sbh);
}

void Graphics_D3D11::Destroy_(
    const TextureHandle _texh)
{
    TextureD3D11& texture = m_textures[_texh];
    JUG_DX_RELEASE(texture.pSrvOrNull);
    JUG_DX_RELEASE(texture.pUavOrNull);
    JUG_DX_RELEASE(texture.pMsaaRtResourceOrNull);
    JUG_DX_RELEASE(texture.pResource);
    m_textures.Erase(_texh);
}

void Graphics_D3D11::Destroy_(
    const FrameBufferHandle _fbh)
{
    FrameBufferD3D11& fb = m_frameBuffers[_fbh];

    if (fb.pSwapChainOrNull)
    {
        // fullscreen 해제. 아니면 Destroy() 시 DXGI_ERROR_DEVICE_REMOVED 날 수 있음
        JUG_DX_CHECK(fb.pSwapChainOrNull->SetFullscreenState(FALSE, nullptr));
    }

    for (ID3D11RenderTargetView*& pRTV: fb.rtvs)
    {
        JUG_DX_RELEASE(pRTV);
    }
    JUG_DX_RELEASE(fb.pDsvOrNull);

    // swap chain은 가장 마지막에 해제.
    JUG_DX_RELEASE(fb.pSwapChainOrNull);
    m_frameBuffers.Erase(_fbh);
}

void Graphics_D3D11::Destroy_(
    const ShaderHandle _sh)
{
    JUG_DX_RELEASE(m_shaders[_sh].pVS);
    m_shaders.Erase(_sh);
}

// ===========================================
//  Buffer
// ===========================================

void Graphics_D3D11::UpdateBuffer_(
    const BufferRef  _buffer,
    const MemoryView _data,
    const uint32_t   _offset)
{
    ID3D11Buffer*                pBuffer = GetD3d11Buffer_(_buffer);
    const ResolveBufferRefResult resolve = ResolveBufferRef_(_buffer);
    const uint32_t               size    = static_cast<uint32_t>(_data.GetSize());

    if (resolve.bDynamic)
    {
        if (_offset == 0 && size == resolve.byteWidth)
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            JUG_DX_CHECK(m_pD3d11DeviceContext->Map(pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
            std::memcpy(mapped.pData, _data.GetPtr(), size);
            m_pD3d11DeviceContext->Unmap(pBuffer, 0);
        }
        else
        {
            // dyniamic buffer의 부분 업데이트는 staging buffer를 사용하여 copySubresourceRegion로 처리한다.
            if (size > m_uploadBufferByteWidth)
            {
                JUG_DX_RELEASE(m_pUploadBuffer);

                D3D11_BUFFER_DESC ud   = {};
                ud.ByteWidth           = size;
                ud.Usage               = D3D11_USAGE_STAGING;
                ud.BindFlags           = 0;
                ud.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
                ud.MiscFlags           = 0;
                ud.StructureByteStride = 0;
                JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&ud, nullptr, &m_pUploadBuffer));

                m_uploadBufferByteWidth = size;
            }

            D3D11_MAPPED_SUBRESOURCE mapped;
            JUG_DX_CHECK(m_pD3d11DeviceContext->Map(m_pUploadBuffer, 0, D3D11_MAP_WRITE, 0, &mapped));
            std::memcpy(mapped.pData, _data.GetPtr(), size);
            m_pD3d11DeviceContext->Unmap(m_pUploadBuffer, 0);

            D3D11_BOX box;
            box.left   = 0;
            box.right  = size;
            box.top    = 0;
            box.bottom = 1;
            box.front  = 0;
            box.back   = 1;
            m_pD3d11DeviceContext->CopySubresourceRegion(pBuffer, 0, _offset, 0, 0, m_pUploadBuffer, 0, &box);
        }
    }
    else   // default buffer
    {
        D3D11_BOX box;
        box.left   = _offset;
        box.right  = _offset + size;
        box.top    = 0;
        box.bottom = 1;
        box.front  = 0;
        box.back   = 1;
        m_pD3d11DeviceContext->UpdateSubresource(pBuffer, 0, &box, _data.GetPtr(), 0, 0);
    }
}

MutableMemoryView Graphics_D3D11::MapBuffer_(
    const BufferRef _buffer)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(GetD3d11Buffer_(_buffer), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    return { mapped.pData, mapped.RowPitch };
}

void Graphics_D3D11::UnmapBuffer_(
    const BufferRef _buffer)
{
    m_pD3d11DeviceContext->Unmap(GetD3d11Buffer_(_buffer), 0);
}

void Graphics_D3D11::CopyBuffer_(
    const BufferRef _dst,
    const uint32_t  _dstOffset,
    const BufferRef _src,
    const uint32_t  _srcOffset,
    const uint32_t  _byteWidth)
{
    ID3D11Buffer* pDst = GetD3d11Buffer_(_dst);
    ID3D11Buffer* pSrc = GetD3d11Buffer_(_src);

    const bool bWhole = _dstOffset == 0 && _srcOffset == 0 && _byteWidth == ResolveBufferRef_(_dst).byteWidth && _byteWidth == ResolveBufferRef_(_src).byteWidth;
    if (bWhole)
    {
        m_pD3d11DeviceContext->CopyResource(pDst, pSrc);
        return;
    }

    D3D11_BOX box;
    box.left   = _srcOffset;
    box.right  = _srcOffset + _byteWidth;
    box.top    = 0;
    box.bottom = 1;
    box.front  = 0;
    box.back   = 1;
    m_pD3d11DeviceContext->CopySubresourceRegion(pDst, 0, _dstOffset, 0, 0, pSrc, 0, &box);
}

ID3D11Buffer* Graphics_D3D11::GetD3d11Buffer_(
    const BufferRef _buffer) const
{
    JUG_ASSERT(_buffer, "Invalid buffer handle.");

    switch (_buffer.GetType())
    {
        case eBuffer::Vertex: return m_vertexBuffers[_buffer.GetVertexBufferHandle()].pBuffer;
        case eBuffer::Instance: return m_instanceBuffers[_buffer.GetInstanceBufferHandle()].pBuffer;
        case eBuffer::Index: return m_indexBuffers[_buffer.GetIndexBufferHandle()].pBuffer;
        case eBuffer::Constant: return m_constantBuffers[_buffer.GetConstantBufferHandle()].pBuffer;
        case eBuffer::Storage: return m_storageBuffers[_buffer.GetStorageBufferHandle()].pBuffer;
        default: JUG_ASSERT(false, "Unsupported buffer m_type {}.", static_cast<uint32_t>(_buffer.GetType())); return nullptr;
    }
}

// ===========================================
//  Texture
// ===========================================

void Graphics_D3D11::UpdateTexture_(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const uint32_t      _offsetX,
    const uint32_t      _offsetY,
    const uint32_t      _offsetZ,
    const uint32_t      _width,
    const uint32_t      _height,
    const uint32_t      _depth,
    const MemoryView    _data)
{
    const TextureDesc&  desc    = m_texturePool[_texh];
    const TextureD3D11& texture = m_textures[_texh];

    const uint32_t index      = CalcTextureIndex(_mip, _layer, desc.numMips);
    const uint32_t rowPitch   = CalcRowPitch(desc.format, _width);
    const uint32_t slicePitch = CalcSlicePitch(desc.format, _width, _height);

    D3D11_BOX box;
    box.left   = _offsetX;
    box.right  = _offsetX + _width;
    box.top    = _offsetY;
    box.bottom = _offsetY + _height;
    box.front  = _offsetZ;
    box.back   = _offsetZ + _depth;
    m_pD3d11DeviceContext->UpdateSubresource(texture.pResource, index, &box, _data.GetPtr(), rowPitch, slicePitch);
}

void Graphics_D3D11::CopyTexture_(
    const TextureHandle _dstTexh,
    const TextureHandle _srcTexh)
{
    JUG_ASSERT(MakeTextureFormatInfo_(m_texturePool[_dstTexh].format).tex == MakeTextureFormatInfo_(m_texturePool[_srcTexh].format).tex, "A texture copy requires both textures to share the same typeless format.");

    m_pD3d11DeviceContext->CopyResource(m_textures[_dstTexh].pResource, m_textures[_srcTexh].pResource);
}

void Graphics_D3D11::CopyTexture_(
    const TextureHandle _dstTexh,
    const uint32_t      _dstMip,
    const uint32_t      _dstLayer,
    const uint32_t      _dstX,
    const uint32_t      _dstY,
    const uint32_t      _dstZ,
    const TextureHandle _srcTexh,
    const uint32_t      _srcMip,
    const uint32_t      _srcLayer,
    const uint32_t      _srcX,
    const uint32_t      _srcY,
    const uint32_t      _srcZ,
    const uint32_t      _width,
    const uint32_t      _height,
    const uint32_t      _depth)
{
    const TextureDesc& dstDesc = m_texturePool[_dstTexh];
    const TextureDesc& srcDesc = m_texturePool[_srcTexh];
    JUG_ASSERT(MakeTextureFormatInfo_(dstDesc.format).tex == MakeTextureFormatInfo_(srcDesc.format).tex, "A texture copy requires both textures to share the same typeless format.");

    const uint32_t dstIndex = CalcTextureIndex(_dstMip, _dstLayer, dstDesc.numMips);
    const uint32_t srcIndex = CalcTextureIndex(_srcMip, _srcLayer, srcDesc.numMips);
    const VEC3U    srcSize  = CalcTextureSize(srcDesc.width, srcDesc.height, srcDesc.depth, _srcMip);

    const bool bWholeSubresource = _srcX == 0 && _srcY == 0 && _srcZ == 0 && _width == srcSize.width && _height == srcSize.height && _depth == srcSize.depth;

    D3D11_BOX box;
    box.left   = _srcX;
    box.right  = _srcX + _width;
    box.top    = _srcY;
    box.bottom = _srcY + _height;
    box.front  = _srcZ;
    box.back   = _srcZ + _depth;
    m_pD3d11DeviceContext->CopySubresourceRegion(m_textures[_dstTexh].pResource, dstIndex, _dstX, _dstY, _dstZ, m_textures[_srcTexh].pResource, srcIndex, bWholeSubresource ? nullptr : &box);
}

// ===========================================
//  Frame Buffer
// ===========================================

void Graphics_D3D11::ResizeSwapChain_(
    const FrameBufferHandle _swapChainFbh,
    const uint32_t          _width,
    const uint32_t          _height)
{
    FrameBufferD3D11&   fb      = m_frameBuffers[_swapChainFbh];
    const TextureHandle texh    = m_frameBufferPool[_swapChainFbh].atts[0].texh;
    TextureD3D11&       texture = m_textures[texh];

    // 컨텍스트가 프레임버퍼를 바인딩하고 있으면 리사이즈가 안됨
    m_pD3d11DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    // 리소스 해제. resize 시 DXGI_ERROR_DEVICE_REMOVED 가 날 수 있으므로 Release() 실패 시 assert.
    JUG_DX_RELEASE(fb.rtvs[0]);
    if (texture.pResource)
    {
        if (texture.pResource->Release() != 0)
        {
            JUG_ASSERT(false, "Texture resource is still referenced by other objects. Release all references before resizing the frame buffer.");
        }
        texture.pResource = nullptr;
    }

    // 리사이즈
    const UINT dxgiFlags = m_caps.bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    JUG_DX_CHECK(fb.pSwapChainOrNull->ResizeBuffers(0, _width, _height, DXGI_FORMAT_UNKNOWN, dxgiFlags));

    JUG_DX_CHECK(fb.pSwapChainOrNull->GetBuffer(0, IID_PPV_ARGS(&texture.pTexture2D)));

    D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format                        = MakeTextureFormatInfo_(m_texturePool[texh].format).rtv;
    rtvd.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvd.Texture2D.MipSlice            = 0;
    JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(texture.pResource, &rtvd, fb.rtvs.data()));
}

void Graphics_D3D11::ClearRenderTarget_(
    const FrameBufferHandle _fbh,
    const RGBA              _color,
    const uint32_t          _slot)
{
    const VEC4 color = _color.ToLinear();
    m_pD3d11DeviceContext->ClearRenderTargetView(m_frameBuffers[_fbh].rtvs[_slot], color.GetPtr());
}

void Graphics_D3D11::ClearDepthStencil_(
    const FrameBufferHandle _fbh,
    const bool              _bClearDepth,
    const bool              _bClearStencil,
    const float             _depth,
    const uint8_t           _stencil)
{
    UINT flags = 0;
    if (_bClearDepth)
    {
        flags |= D3D11_CLEAR_DEPTH;
    }
    if (_bClearStencil)
    {
        flags |= D3D11_CLEAR_STENCIL;
    }
    m_pD3d11DeviceContext->ClearDepthStencilView(m_frameBuffers[_fbh].pDsvOrNull, flags, _depth, _stencil);
}

// ===========================================
//  Input Layout
// ===========================================

ID3D11InputLayout* Graphics_D3D11::GetOrCreateD3d11InputLayoutOrNull_()
{
    if (m_state.numStreams == 0)
    {
        return nullptr;
    }

    Murmur3 hasher {};
    for (uint32_t slot = 0; slot < m_state.numStreams; ++slot)
    {
        hasher.Mix(m_state.vlhs[slot]);
    }
    hasher.Mix(m_state.instanceStride);
    const uint64_t key = hasher.Finalize();

    // 캐시 히트
    const auto it = m_d3d11InputLayoutCache.find(key);
    if (it != m_d3d11InputLayoutCache.end())
    {
        return it->second;
    }

    JUG_ASSERT(m_state.instanceStride % kInstanceDataSizeAlign == 0, "Instance stride must be a multiple of the instance data alignment.");
    const uint32_t numInstData = m_state.instanceStride / kInstanceDataSizeAlign;
    uint32_t       numElems    = numInstData;
    for (uint32_t slot = 0; slot < m_state.numStreams; ++slot)
    {
        if (m_state.vlhs[slot])
        {
            numElems += static_cast<uint32_t>(m_vertexLayoutPool[m_state.vlhs[slot]].vl.GetNumAttributes());
        }
    }

    if (numElems == 0)
    {
        m_d3d11InputLayoutCache.emplace(key, nullptr);
        return nullptr;
    }

    D3D11_INPUT_ELEMENT_DESC* pIed  = static_cast<D3D11_INPUT_ELEMENT_DESC*>(JUG_STACK_ALLOC(sizeof(D3D11_INPUT_ELEMENT_DESC) * numElems));
    uint32_t                  index = 0;

    String hlsl = "struct VSInput {";
    for (uint32_t slot = 0; slot < m_state.numStreams; ++slot)
    {
        const VertexLayoutHandle vlhOrInvalid = m_state.vlhs[slot];
        if (!vlhOrInvalid)
        {
            continue;
        }

        for (const VertexAttribute& attrib: m_vertexLayoutPool[vlhOrInvalid].vl.GetAttributes())
        {
            const VertexSemantic semantic = ToVertexSemantic_(attrib.attrib);

            D3D11_INPUT_ELEMENT_DESC& element = pIed[index];
            element.SemanticName              = semantic.pName;
            element.SemanticIndex             = semantic.index;
            element.Format                    = ToDxgiVertexFormat_(attrib.format, attrib.num);
            element.InputSlot                 = slot;
            element.AlignedByteOffset         = attrib.offset;
            element.InputSlotClass            = D3D11_INPUT_PER_VERTEX_DATA;
            element.InstanceDataStepRate      = 0;

            std::format_to(std::back_inserter(hlsl), "{} Elem{} : {}{};", ToHlslTypeName_(attrib.format, attrib.num), index, semantic.pName, semantic.index);
            ++index;
        }
    }

    for (uint32_t i = 0; i < numInstData; ++i)
    {
        D3D11_INPUT_ELEMENT_DESC& ied = pIed[index];
        ied.SemanticName              = "INSTANCE";
        ied.SemanticIndex             = i;
        ied.Format                    = DXGI_FORMAT_R32G32B32A32_FLOAT;
        ied.InputSlot                 = m_state.numStreams;
        ied.AlignedByteOffset         = i * kInstanceDataSizeAlign;
        ied.InputSlotClass            = D3D11_INPUT_PER_INSTANCE_DATA;
        ied.InstanceDataStepRate      = 1;
        std::format_to(std::back_inserter(hlsl), "float4 Inst{} : INSTANCE{};", i, i);
        ++index;
    }

    JUG_ASSERT(index == numElems, "Input element count does not match the counted size.");
    hlsl += "}; void VSMain(in VSInput _input) {}";

    ShaderCompileConfig compileConfig   = {};
    compileConfig.type                  = eShader::Vertex;
    compileConfig.target                = eShaderTarget::DXBC;
    compileConfig.entryPoint            = "VSMain";
    const Result<ShaderBytecode> shader = CompileShader(hlsl, compileConfig);
    JUG_ASSERT(shader, "Failed to compile the dummy vertex shader for an input layout.");
    const MemoryView bytecode = shader->GetBytecode();

    ID3D11InputLayout* pInputLayout;
    JUG_DX_CHECK(m_pD3d11Device->CreateInputLayout(pIed, numElems, bytecode.GetPtr(), bytecode.GetSize(), &pInputLayout));
    m_d3d11InputLayoutCache.emplace(key, pInputLayout);
    JUG_CORE_LOG_TRACE("InputLayout created. numElements = {}, numStreams = {}, instanceStride = {}, cacheSize = {}", numElems, m_state.numStreams, m_state.instanceStride, m_d3d11InputLayoutCache.size());
    return pInputLayout;
}

// ===========================================
//  Debug
// ===========================================

void Graphics_D3D11::SetName(
    const VertexBufferHandle _vbh,
    const StringView         _name)
{
    SetD3d11ObjectName_(m_vertexBuffers[_vbh].pBuffer, _name);
}

void Graphics_D3D11::SetName(
    const InstanceBufferHandle _instbh,
    const StringView           _name)
{
    SetD3d11ObjectName_(m_instanceBuffers[_instbh].pBuffer, _name);
}

void Graphics_D3D11::SetName(
    const IndexBufferHandle _ibh,
    const StringView        _name)
{
    SetD3d11ObjectName_(m_indexBuffers[_ibh].pBuffer, _name);
}

void Graphics_D3D11::SetName(
    const ConstantBufferHandle _cbh,
    const StringView           _name)
{
    SetD3d11ObjectName_(m_constantBuffers[_cbh].pBuffer, _name);
}

void Graphics_D3D11::SetName(
    const StorageBufferHandle _sbh,
    const StringView          _name)
{
    const StorageBufferD3D11& storageBuffer = m_storageBuffers[_sbh];
    SetD3d11ObjectName_(storageBuffer.pBuffer, _name);
    if (storageBuffer.pSrvOrNull)
    {
        SetD3d11ObjectName_(storageBuffer.pSrvOrNull, _name);
    }
    if (storageBuffer.pUavOrNull)
    {
        SetD3d11ObjectName_(storageBuffer.pUavOrNull, _name);
    }
}

void Graphics_D3D11::SetName(
    const TextureHandle _texh,
    const StringView    _name)
{
    const TextureD3D11& texture = m_textures[_texh];
    SetD3d11ObjectName_(texture.pResource, _name);
    if (texture.pMsaaRtResourceOrNull)
    {
        SetD3d11ObjectName_(texture.pMsaaRtResourceOrNull, _name);
    }
    if (texture.pSrvOrNull)
    {
        SetD3d11ObjectName_(texture.pSrvOrNull, _name);
    }
    if (texture.pUavOrNull)
    {
        SetD3d11ObjectName_(texture.pUavOrNull, _name);
    }
}

void Graphics_D3D11::SetName(
    const FrameBufferHandle _fbh,
    const StringView        _name)
{
    const FrameBufferD3D11& frameBuffer = m_frameBuffers[_fbh];
    const uint32_t          numRts      = m_frameBufferPool[_fbh].numRts;

    for (uint32_t i = 0; i < numRts; ++i)
    {
        SetD3d11ObjectName_(frameBuffer.rtvs[i], _name);
    }
    if (frameBuffer.pDsvOrNull)
    {
        SetD3d11ObjectName_(frameBuffer.pDsvOrNull, _name);
    }

#ifdef JUG_DEBUG
    if (frameBuffer.pSwapChainOrNull)
    {
        const UINT len = static_cast<UINT>(Min(_name.size(), kMaxDebugNameLength));
        JUG_DX_CHECK(frameBuffer.pSwapChainOrNull->SetPrivateData(WKPDID_D3DDebugObjectName, len, _name.data()));
    }
#endif
}

void Graphics_D3D11::SetName(
    const ShaderHandle _sh,
    const StringView   _name)
{
    const ShaderD3D11& shader = m_shaders[_sh];
    switch (m_shaderPool[_sh].type)
    {
        case eShader::Vertex: SetD3d11ObjectName_(shader.pVS, _name); break;
        case eShader::Pixel: SetD3d11ObjectName_(shader.pPS, _name); break;
        case eShader::Compute: SetD3d11ObjectName_(shader.pCS, _name); break;
        default: JUG_ASSERT(false, "Unknown shader stage."); break;
    }
}

void Graphics_D3D11::PushDebugGroup(
    const StringView _name) const
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        JUG_DX_CHECK(m_pUserAnnotationOrNull->BeginEvent(EncodeUtf16(_name).c_str()));
    }
#endif
}

void Graphics_D3D11::PopDebugGroup() const
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        JUG_DX_CHECK(m_pUserAnnotationOrNull->EndEvent());
    }
#endif
}

void Graphics_D3D11::SetDebugMarker(
    const StringView _name) const
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        m_pUserAnnotationOrNull->SetMarker(EncodeUtf16(_name).c_str());
    }
#endif
}

// ===========================================
//  State Cache
// ===========================================

ID3D11SamplerState* Graphics_D3D11::GetOrCreateSamplerState_(
    const Flags<eSampler> _flags,
    const RGBA            _border)
{
    // hash
    Murmur3 hasher {};
    hasher.Mix(_flags);
    hasher.Mix(_border);
    const uint64_t hash = hasher.Finalize();

    // find
    const auto it = m_d3d11SamplerStateCache.find(hash);
    if (it != m_d3d11SamplerStateCache.end())
    {
        return it->second;
    }

    // create if not found
    const D3D11_SAMPLER_DESC sd = MakeSamplerDesc_(_flags, _border);
    ID3D11SamplerState*      pState;
    JUG_DX_CHECK(m_pD3d11Device->CreateSamplerState(&sd, &pState));
    m_d3d11SamplerStateCache.emplace(hash, pState);
    JUG_CORE_LOG_TRACE("SamplerState created. flags = {:#x}, cacheSize = {}", _flags.GetFlags(), m_d3d11SamplerStateCache.size());
    return pState;
}

ID3D11RasterizerState* Graphics_D3D11::GetOrCreateRasterizerState_()
{
    const Flags<eRenderState> flags = m_state.renderStateFlags & kRasterizerStateMask;
    const uint64_t            key   = flags.GetFlags();
    const auto                it    = m_d3d11RasterizerStateCache.find(key);
    if (it != m_d3d11RasterizerStateCache.end())
    {
        return it->second;
    }

    const D3D11_RASTERIZER_DESC rd = MakeRasterizerDesc_(m_state.renderStateFlags);
    ID3D11RasterizerState*      pState;
    JUG_DX_CHECK(m_pD3d11Device->CreateRasterizerState(&rd, &pState));
    m_d3d11RasterizerStateCache.emplace(key, pState);
    JUG_CORE_LOG_TRACE("RasterizerState created. flags = {:#x}, cacheSize = {}", flags.GetFlags(), m_d3d11RasterizerStateCache.size());
    return pState;
}

ID3D11BlendState* Graphics_D3D11::GetOrCreateBlendState_()
{
    const Flags<eRenderState> flags = m_state.renderStateFlags & kBlendStateMask;

    Murmur3 hasher {};
    hasher.Mix(flags);
    if (m_state.renderStateFlags & eRenderState::IndependentBlend)
    {
        for (uint32_t i = 0; i < kNumMaxRenderTargetSlots; ++i)
        {
            hasher.Mix(m_state.blendFlags[i]);
        }
    }
    else
    {
        hasher.Mix(m_state.blendFlags[0]);
    }
    const uint64_t key = hasher.Finalize();

    // find
    const auto it = m_d3d11BlendStateCache.find(key);
    if (it != m_d3d11BlendStateCache.end())
    {
        return it->second;
    }

    const D3D11_BLEND_DESC bd     = MakeBlendDesc_(m_state.renderStateFlags, m_state.blendFlags);
    ID3D11BlendState*      pState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateBlendState(&bd, &pState));
    m_d3d11BlendStateCache.emplace(key, pState);

    JUG_CORE_LOG_TRACE("BlendState created. state = {:#x}, blend0 = {:#x}, cacheSize = {}", flags.GetFlags(), m_state.blendFlags[0].GetFlags(), m_d3d11BlendStateCache.size());
    return pState;
}

ID3D11DepthStencilState* Graphics_D3D11::GetOrCreateDepthStencilState_()
{
    const Flags<eRenderState> flags = m_state.renderStateFlags & kDepthStencilStateMask;
    const uint64_t            key   = static_cast<uint64_t>(flags.GetFlags() | m_state.fstencilFlags.GetFlags()) | (static_cast<uint64_t>(m_state.bstencilFlags.GetFlags()) << 32);

    // find
    const auto it = m_d3d11DepthStencilStateCache.find(key);
    if (it != m_d3d11DepthStencilStateCache.end())
    {
        return it->second;
    }

    const D3D11_DEPTH_STENCIL_DESC dsd                = MakeDepthStencilDesc_(m_state.renderStateFlags, m_state.fstencilFlags, m_state.bstencilFlags);
    ID3D11DepthStencilState*       pDepthStencilState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateDepthStencilState(&dsd, &pDepthStencilState));
    m_d3d11DepthStencilStateCache.emplace(key, pDepthStencilState);

    JUG_CORE_LOG_TRACE("DepthStencilState created. depth = {:#x}, fstencil = {:#x}, bstencil = {:#x}, cacheSize = {}", flags.GetFlags(), m_state.fstencilFlags.GetFlags(), m_state.bstencilFlags.GetFlags(), m_d3d11DepthStencilStateCache.size());
    return pDepthStencilState;
}

// ===========================================
//  Apply
// ===========================================

ID3D11ShaderResourceView* Graphics_D3D11::GetSrvOrNull_(
    const ResourceRef _resource) const
{
    if (!_resource)
    {
        return nullptr;
    }

    if (_resource.GetType() == eResource::Texture)
    {
        return m_textures[_resource.GetTextureHandle()].pSrvOrNull;
    }
    return m_storageBuffers[_resource.GetStorageBufferHandle()].pSrvOrNull;
}

ID3D11UnorderedAccessView* Graphics_D3D11::GetUavOrNull_(
    const ResourceRef _resource) const
{
    if (!_resource)
    {
        return nullptr;
    }

    if (_resource.GetType() == eResource::Texture)
    {
        return m_textures[_resource.GetTextureHandle()].pUavOrNull;
    }
    return m_storageBuffers[_resource.GetStorageBufferHandle()].pUavOrNull;
}

void Graphics_D3D11::SyncCBuffers_(
    const eShader _shader)
{
    const CBufferBind&                         bind   = m_state.cbufferBind[_shader];
    ARRAY<ID3D11Buffer*, kNumMaxCBufferSlots>& mirror = m_d3d11CBuffers[_shader];
    for (uint32_t slot = bind.dirtyBegin; slot < bind.dirtyEnd; ++slot)
    {
        const ConstantBufferHandle cbh = bind.resources[slot];
        mirror[slot]                   = cbh ? m_constantBuffers[cbh].pBuffer : nullptr;
    }
}

void Graphics_D3D11::SyncSrvs_(
    const eShader _shader)
{
    const ReadBind&                                     bind   = m_state.readBind[_shader];
    ARRAY<ID3D11ShaderResourceView*, kNumMaxReadSlots>& mirror = m_d3d11Srvs[_shader];
    for (uint32_t slot = bind.dirtyBegin; slot < bind.dirtyEnd; ++slot)
    {
        mirror[slot] = GetSrvOrNull_(bind.resources[slot]);
    }
}

void Graphics_D3D11::SyncSamplers_(
    const eShader _shader)
{
    const SamplerBind&                               bind   = m_state.samplerBind[_shader];
    ARRAY<ID3D11SamplerState*, kNumMaxSamplerSlots>& mirror = m_d3d11Samplers[_shader];
    for (uint32_t slot = bind.dirtyBegin; slot < bind.dirtyEnd; ++slot)
    {
        // 지정하지 않은 슬롯은 기존처럼 null (D3D 기본 샘플러)
        const Sampler& sampler = bind.resources[slot];
        mirror[slot]           = sampler == Sampler {} ? nullptr : GetOrCreateSamplerState_(sampler.flags, sampler.border);
    }
}

void Graphics_D3D11::SyncUavs_(
    const eShaderRW _shader)
{
    const ReadWriteBind&                                      bind   = m_state.readWriteBind[_shader];
    ARRAY<ID3D11UnorderedAccessView*, kNumMaxReadWriteSlots>& mirror = m_d3d11Uavs[_shader];
    for (uint32_t slot = bind.dirtyBegin; slot < bind.dirtyEnd; ++slot)
    {
        mirror[slot] = GetUavOrNull_(bind.resources[slot]);
    }
}

void Graphics_D3D11::ApplyPipeline_()
{
    const Flags<ePipelineDirty> dirtyFlags = m_state.dirtyFlags;
    if (dirtyFlags == kNoneFlags)
    {
        return;
    }

    if (dirtyFlags & ePipelineDirty::VertexBuffer)
    {
        for (uint32_t i = 0; i < m_state.numStreams; ++i)
        {
            const VertexBufferHandle vbhOrInvalid = m_state.vbhs[i];
            m_d3d11VertexBuffers[i]               = vbhOrInvalid ? m_vertexBuffers[vbhOrInvalid].pBuffer : nullptr;
        }
        m_pD3d11DeviceContext->IASetVertexBuffers(0, m_state.numStreams, m_d3d11VertexBuffers.data(), m_state.vertexStrides.data(), m_state.vertexOffsets.data());
    }

    if (dirtyFlags & ePipelineDirty::InstanceBuffer)
    {
        ID3D11Buffer* pInstanceBufferOrNull = m_state.instbhOrInvalid ? m_instanceBuffers[m_state.instbhOrInvalid].pBuffer : nullptr;
        m_pD3d11DeviceContext->IASetVertexBuffers(m_state.numStreams, 1, &pInstanceBufferOrNull, &m_state.instanceStride, &m_state.instanceOffset);
    }

    if (dirtyFlags & ePipelineDirty::IndexBuffer)
    {
        ID3D11Buffer* pIndexBufferOrNull = nullptr;
        DXGI_FORMAT   format             = DXGI_FORMAT_UNKNOWN;
        if (m_state.ibhOrInvalid)
        {
            pIndexBufferOrNull = m_indexBuffers[m_state.ibhOrInvalid].pBuffer;
            format             = m_indexBufferPool[m_state.ibhOrInvalid].bU32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        }
        m_pD3d11DeviceContext->IASetIndexBuffer(pIndexBufferOrNull, format, 0);
    }

    if (dirtyFlags & ePipelineDirty::PrimitiveTopology)
    {
        m_pD3d11DeviceContext->IASetPrimitiveTopology(MakeD3d11Topology_(m_state.renderStateFlags));
    }

    if (dirtyFlags & ePipelineDirty::InputLayout)
    {
        m_pD3d11DeviceContext->IASetInputLayout(GetOrCreateD3d11InputLayoutOrNull_());
    }

    if (dirtyFlags & ePipelineDirty::Program)
    {
        ID3D11VertexShader* pVsOrNull = nullptr;
        ID3D11PixelShader*  pPsOrNull = nullptr;

        if (m_state.phOrInvalid)
        {
            const ProgramDesc& program = m_programPool[m_state.phOrInvalid];
            pVsOrNull                  = program.vshOrInvalid ? m_shaders[program.vshOrInvalid].pVS : nullptr;
            pPsOrNull                  = program.pshOrInvalid ? m_shaders[program.pshOrInvalid].pPS : nullptr;
        }

        m_pD3d11DeviceContext->VSSetShader(pVsOrNull, nullptr, 0);
        m_pD3d11DeviceContext->PSSetShader(pPsOrNull, nullptr, 0);
    }

    if (dirtyFlags & ePipelineDirty::ComputeProgram)
    {
        ID3D11ComputeShader* pCsOrNull = nullptr;
        if (m_state.computePhOrInvalid)
        {
            const ProgramDesc& program = m_programPool[m_state.computePhOrInvalid];
            pCsOrNull                  = program.cshOrInvalid ? m_shaders[program.cshOrInvalid].pCS : nullptr;
        }

        m_pD3d11DeviceContext->CSSetShader(pCsOrNull, nullptr, 0);
    }

    if (dirtyFlags & ePipelineDirty::ConstantBuffer)
    {
        if (CBufferBind& bind = m_state.cbufferBind[eShader::Vertex]; bind.IsDirty())
        {
            SyncCBuffers_(eShader::Vertex);
            m_pD3d11DeviceContext->VSSetConstantBuffers(bind.dirtyBegin, bind.NumDirties(), m_d3d11CBuffers[eShader::Vertex].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (CBufferBind& bind = m_state.cbufferBind[eShader::Pixel]; bind.IsDirty())
        {
            SyncCBuffers_(eShader::Pixel);
            m_pD3d11DeviceContext->PSSetConstantBuffers(bind.dirtyBegin, bind.NumDirties(), m_d3d11CBuffers[eShader::Pixel].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (CBufferBind& bind = m_state.cbufferBind[eShader::Compute]; bind.IsDirty())
        {
            SyncCBuffers_(eShader::Compute);
            m_pD3d11DeviceContext->CSSetConstantBuffers(bind.dirtyBegin, bind.NumDirties(), m_d3d11CBuffers[eShader::Compute].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }
    }

    if (dirtyFlags & ePipelineDirty::ShaderResourceView)
    {
        if (ReadBind& bind = m_state.readBind[eShader::Vertex]; bind.IsDirty())
        {
            SyncSrvs_(eShader::Vertex);
            m_pD3d11DeviceContext->VSSetShaderResources(bind.dirtyBegin, bind.NumDirties(), m_d3d11Srvs[eShader::Vertex].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (ReadBind& bind = m_state.readBind[eShader::Pixel]; bind.IsDirty())
        {
            SyncSrvs_(eShader::Pixel);
            m_pD3d11DeviceContext->PSSetShaderResources(bind.dirtyBegin, bind.NumDirties(), m_d3d11Srvs[eShader::Pixel].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (ReadBind& bind = m_state.readBind[eShader::Compute]; bind.IsDirty())
        {
            SyncSrvs_(eShader::Compute);
            m_pD3d11DeviceContext->CSSetShaderResources(bind.dirtyBegin, bind.NumDirties(), m_d3d11Srvs[eShader::Compute].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }
    }

    if (dirtyFlags & ePipelineDirty::SamplerState)
    {
        if (SamplerBind& bind = m_state.samplerBind[eShader::Vertex]; bind.IsDirty())
        {
            SyncSamplers_(eShader::Vertex);
            m_pD3d11DeviceContext->VSSetSamplers(bind.dirtyBegin, bind.NumDirties(), m_d3d11Samplers[eShader::Vertex].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (SamplerBind& bind = m_state.samplerBind[eShader::Pixel]; bind.IsDirty())
        {
            SyncSamplers_(eShader::Pixel);
            m_pD3d11DeviceContext->PSSetSamplers(bind.dirtyBegin, bind.NumDirties(), m_d3d11Samplers[eShader::Pixel].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (SamplerBind& bind = m_state.samplerBind[eShader::Compute]; bind.IsDirty())
        {
            SyncSamplers_(eShader::Compute);
            m_pD3d11DeviceContext->CSSetSamplers(bind.dirtyBegin, bind.NumDirties(), m_d3d11Samplers[eShader::Compute].data() + bind.dirtyBegin);
            bind.ClearDirty();
        }
    }

    // ps의 rw bind는 OMSetRenderTargetsAndUnorderedAccessViews() 호출에서 처리됨
    if (dirtyFlags & ePipelineDirty::UnorderedAccessView)
    {
        if (ReadWriteBind& bind = m_state.readWriteBind[eShaderRW::Compute]; bind.IsDirty())
        {
            SyncUavs_(eShaderRW::Compute);
            m_pD3d11DeviceContext->CSSetUnorderedAccessViews(bind.dirtyBegin, bind.NumDirties(), m_d3d11Uavs[eShaderRW::Compute].data() + bind.dirtyBegin, nullptr);
            bind.ClearDirty();
        }
    }

    if (dirtyFlags & ePipelineDirty::BlendState)
    {
        const VEC4 blendFactor = m_state.blendFactor.ToLinear();
        m_pD3d11DeviceContext->OMSetBlendState(GetOrCreateBlendState_(), blendFactor.GetPtr(), 0xFFFF'FFFF);
    }

    if (dirtyFlags & ePipelineDirty::DepthStencilState)
    {
        m_pD3d11DeviceContext->OMSetDepthStencilState(GetOrCreateDepthStencilState_(), m_state.stencilRef);
    }

    if (dirtyFlags & ePipelineDirty::RasterizerState)
    {
        m_pD3d11DeviceContext->RSSetState(GetOrCreateRasterizerState_());
    }

    if (dirtyFlags & ePipelineDirty::FrameBuffer || m_state.readWriteBind[eShaderRW::Pixel].IsDirty())
    {
        // 이전 프레임버퍼의 write 작업이 끝났기에 resolve를 수행한다.
        if (m_state.lastFbhOrInvalid && m_state.lastFbhOrInvalid != m_state.fbhOrInvalid)
        {
            const FrameBufferDesc& fb = m_frameBufferPool[m_state.lastFbhOrInvalid];
            for (uint32_t i = 0; i < fb.numRts; ++i)
            {
                const Attachment& att = fb.atts[i];
                JUG_ASSERT(att.texh, "Invalid texture handle in frame buffer attachment.");

                const TextureDesc&  texDesc = m_texturePool[att.texh];
                const TextureD3D11& texture = m_textures[att.texh];
                if (fb.bMSAA)   // resolve
                {
                    JUG_ASSERT(texture.pMsaaRtResourceOrNull, "Invalid MSAA render target resource for texture in frame buffer attachment.");
                    const TextureFormatInfo formatInfo = MakeTextureFormatInfo_(texDesc.format);
                    for (uint32_t layer = 0; layer < texDesc.numLayers; ++layer)
                    {
                        const uint32_t dstIndex = CalcTextureIndex(att.mip, layer, texDesc.numMips);
                        m_pD3d11DeviceContext->ResolveSubresource(texture.pResource, dstIndex, texture.pMsaaRtResourceOrNull, layer, formatInfo.srv);
                        ++m_stats.numResolves;
                    }
                }

                // 렌더 타겟에 밉이 있으면 갱신해 준다.
                if (texDesc.numMips > 1 && texture.pSrvOrNull && texDesc.flags.Has(eTextureOption::RenderTarget))
                {
                    m_pD3d11DeviceContext->GenerateMips(texture.pSrvOrNull);
                }
            }
        }

        // 언바인딩
        if (m_state.fbhOrInvalid)
        {
            const FrameBufferDesc&  fbDesc = m_frameBufferPool[m_state.fbhOrInvalid];
            const FrameBufferD3D11& fb     = m_frameBuffers[m_state.fbhOrInvalid];
            ReadWriteBind&          bind   = m_state.readWriteBind[eShaderRW::Pixel];

            // PS UAV 는 렌더 타겟과 같은 호출로 묶어야 한다.
            if (bind.IsDirty())
            {
                SyncUavs_(eShaderRW::Pixel);

                const UINT startSlot = fbDesc.numRts;
                const UINT numUavs   = kNumMaxReadWriteSlots > startSlot ? kNumMaxReadWriteSlots - startSlot : 0u;
                m_pD3d11DeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(
                    fbDesc.numRts,
                    fb.rtvs.data(),
                    fb.pDsvOrNull,
                    startSlot,
                    numUavs,
                    m_d3d11Uavs[eShaderRW::Pixel].data() + startSlot,
                    nullptr);

                bind.ClearDirty();
            }
            else
            {
                m_pD3d11DeviceContext->OMSetRenderTargets(fbDesc.numRts, fb.rtvs.data(), fb.pDsvOrNull);
            }

            m_state.lastFbhOrInvalid = m_state.fbhOrInvalid;
        }
        else
        {
            ReadWriteBind& bind = m_state.readWriteBind[eShaderRW::Pixel];
            if (bind.IsDirty())
            {
                SyncUavs_(eShaderRW::Pixel);

                m_pD3d11DeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(
                    0,
                    nullptr,
                    nullptr,
                    0,
                    kNumMaxReadWriteSlots,
                    m_d3d11Uavs[eShaderRW::Pixel].data(),
                    nullptr);

                bind.ClearDirty();
            }
            else
            {
                m_pD3d11DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
            }

            m_state.lastFbhOrInvalid = kInvalidHandle;
        }
    }

    if (dirtyFlags & ePipelineDirty::Viewport)
    {
        D3D11_VIEWPORT viewport;
        viewport.TopLeftX = m_state.viewportX;
        viewport.TopLeftY = m_state.viewportY;
        viewport.Width    = m_state.viewportW;
        viewport.Height   = m_state.viewportH;
        viewport.MinDepth = 0.f;
        viewport.MaxDepth = 1.f;

        m_pD3d11DeviceContext->RSSetViewports(1, &viewport);
    }

    if (dirtyFlags & ePipelineDirty::ScissorRect)
    {
        D3D11_RECT rect;
        rect.left   = m_state.scissorX;
        rect.top    = m_state.scissorY;
        rect.right  = m_state.scissorX + m_state.scissorW;
        rect.bottom = m_state.scissorY + m_state.scissorH;

        m_pD3d11DeviceContext->RSSetScissorRects(1, &rect);
    }

    m_state.dirtyFlags = kNoneFlags;
}

// ===========================================
//  Execute
// ===========================================

void Graphics_D3D11::Touch()
{
    ApplyPipeline_();
}

void Graphics_D3D11::Submit_()
{
    ApplyPipeline_();

    const UINT startVertex   = m_state.submitParams[eSubmitParam::StartVertexLocation];
    const UINT startIndex    = m_state.submitParams[eSubmitParam::StartIndexLocation];
    const UINT startInstance = m_state.submitParams[eSubmitParam::StartInstanceLocation];
    const INT  baseVertex    = static_cast<INT>(m_state.submitParams[eSubmitParam::BaseVertexLocation]);

    const bool bIndexed   = static_cast<bool>(m_state.ibhOrInvalid);
    const bool bInstanced = static_cast<bool>(m_state.instbhOrInvalid);

    if (bIndexed && bInstanced)
    {
        m_pD3d11DeviceContext->DrawIndexedInstanced(m_state.numIndices, m_state.numInstances, startIndex + m_state.indexOffset, baseVertex, startInstance);
    }
    else if (bIndexed)
    {
        m_pD3d11DeviceContext->DrawIndexed(m_state.numIndices, startIndex + m_state.indexOffset, baseVertex);
    }
    else if (bInstanced)
    {
        m_pD3d11DeviceContext->DrawInstanced(m_state.numVertices, m_state.numInstances, startVertex, startInstance);
    }
    else
    {
        m_pD3d11DeviceContext->Draw(m_state.numVertices, startVertex);
    }
}

void Graphics_D3D11::Submit_(
    const StorageBufferHandle _indirectSbh,
    const uint32_t            _offset,
    const uint32_t            _numDraws)
{
    ApplyPipeline_();

    ID3D11Buffer* pIndirectBuffer = m_storageBuffers[_indirectSbh].pBuffer;
    const bool    bIndexed        = static_cast<bool>(m_state.ibhOrInvalid);

    for (uint32_t i = 0; i < _numDraws; ++i)
    {
        const UINT argOffset = _offset + i * kIndirectArgsStride;

        if (bIndexed)
        {
            m_pD3d11DeviceContext->DrawIndexedInstancedIndirect(pIndirectBuffer, argOffset);
        }
        else
        {
            m_pD3d11DeviceContext->DrawInstancedIndirect(pIndirectBuffer, argOffset);
        }
    }
}

void Graphics_D3D11::Dispatch_(
    const uint32_t _numGroupsX,
    const uint32_t _numGroupsY,
    const uint32_t _numGroupsZ)
{
    ApplyPipeline_();

    m_pD3d11DeviceContext->Dispatch(_numGroupsX, _numGroupsY, _numGroupsZ);
}

void Graphics_D3D11::Dispatch_(
    const StorageBufferHandle _indirectSbh,
    const uint32_t            _offset)
{
    ApplyPipeline_();

    m_pD3d11DeviceContext->DispatchIndirect(m_storageBuffers[_indirectSbh].pBuffer, _offset);
}

}   // namespace jug
