#include "pch.h"
#include "Graphics.h"

#include "DxMacro.h"
#include "Shader.h"

namespace jug
{

namespace
{
    Graphics*          g_pSingleton        = nullptr;
    constexpr size_t   kMaxDebugNameLength = 255;
    constexpr uint32_t kIndirectArgsStride = 32;

    constexpr uint32_t kDxbcMagic                = JUG_FOURCC('D', 'X', 'B', 'C');
    constexpr uint32_t kShdrMagic                = JUG_FOURCC('S', 'H', 'D', 'R');
    constexpr uint32_t kShexMagic                = JUG_FOURCC('S', 'H', 'E', 'X');
    constexpr size_t   kShaderBytecodeHeaderSize = 32;

    constexpr Flags<eRenderState> kTopologyStateMask {
        eRenderState::Topology_TriangleStrip,
        eRenderState::Topology_LineList,
        eRenderState::Topology_LineStrip,
        eRenderState::Topology_PointList,
    };

    constexpr Flags<eRenderState> kRasterizerStateMask {
        eRenderState::Cull_Front,
        eRenderState::Cull_Back,
        eRenderState::Wireframe,
        eRenderState::MultiSample,
        eRenderState::LineAA,
        eRenderState::Scissor,
        eRenderState::FrontCCW,
        eRenderState::DepthClamp,
    };

    constexpr Flags<eRenderState> kBlendStateMask {
        eRenderState::AlphaToCoverage,
        eRenderState::IndependentBlend,
    };

    constexpr Flags<eRenderState> kDepthStencilStateMask {
        eRenderState::DepthWrite,
        eRenderState::DepthTest_Less,
        eRenderState::DepthTest_LessEqual,
        eRenderState::DepthTest_Greater,
        eRenderState::DepthTest_GreaterEqual,
        eRenderState::DepthTest_Equal,
        eRenderState::DepthTest_NotEqual,
        eRenderState::DepthTest_Always,
        eRenderState::DepthTest_Never,
    };

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
            case eTextureFormat::Unknown: return {};

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
        const Flags<eRenderState>                                  _state,
        const Span<const Flags<eBlend>, kNumMaxRenderTargetSlots>& _blends)
    {
        D3D11_BLEND_DESC bd       = {};
        bd.AlphaToCoverageEnable  = _state.Has(eRenderState::AlphaToCoverage) ? TRUE : FALSE;
        bd.IndependentBlendEnable = _state.Has(eRenderState::IndependentBlend) ? TRUE : FALSE;

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
        const Flags<eSampler> _state,
        const RGBA            _borderColor)
    {
        D3D11_SAMPLER_DESC sd = {};

        // filter
        const Flags<eSampler> filterFlags  = FilterFilter(_state);
        const eSampler        filter       = static_cast<eSampler>(filterFlags.GetFlags());
        const Flags<eSampler> compareFlags = FilterCompare(_state);
        const eSampler        compare      = static_cast<eSampler>(compareFlags.GetFlags());
        const bool            bComparison  = compare != eSampler::Compare_None;
        switch (filter)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case eSampler::Filter_MinPoint_MagPoint_MipPoint: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT; break;
            case eSampler::Filter_MinPoint_MagPoint_MipLinear: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR; break;
            case eSampler::Filter_MinPoint_MagLinear_MipPoint: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT; break;
            case eSampler::Filter_MinPoint_MagLinear_MipLinear: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR : D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR; break;
            case eSampler::Filter_MinLinear_MagPoint_MipPoint: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT : D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT; break;
            case eSampler::Filter_MinLinear_MagPoint_MipLinear: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR; break;
            case eSampler::Filter_MinLinear_MagLinear_MipPoint: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT; break;
            case eSampler::Filter_MinLinear_MagLinear_MipLinear: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_LINEAR; break;
            case eSampler::Filter_Anisotropic4: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC; break;
            case eSampler::Filter_Anisotropic8: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC; break;
            case eSampler::Filter_Anisotropic16: sd.Filter = bComparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC; break;
            default: JUG_ASSERT(false, "Unsupported filter."); break;
        }

        // anisotropic
        sd.MaxAnisotropy = filter == eSampler::Filter_Anisotropic4  ? 4
                         : filter == eSampler::Filter_Anisotropic8  ? 8
                         : filter == eSampler::Filter_Anisotropic16 ? 16
                                                                    : 1;

        // address u
        const Flags<eSampler> uFlags = FilterU(_state);
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
        const Flags<eSampler> vFlags = FilterV(_state);
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
        const Flags<eSampler> wFlags = FilterW(_state);
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

        const VECTOR4 rgba = _borderColor.ToLinear();
        sd.BorderColor[0]  = rgba.x;
        sd.BorderColor[1]  = rgba.y;
        sd.BorderColor[2]  = rgba.z;
        sd.BorderColor[3]  = rgba.w;
        sd.MinLOD          = 0.f;
        sd.MaxLOD          = D3D11_FLOAT32_MAX;
        return sd;
    }

    [[nodiscard]] String QueryD3d11DebugNameOrEmpty_(
        ID3D11DeviceChild* _pD3dd11DeviceChild)
    {
        JUG_ASSERT(_pD3dd11DeviceChild, "Invalid D3D11 device child pointer.");

        ARRAY<char, 256> name;
        UINT             size = sizeof(name);
        if (FAILED(_pD3dd11DeviceChild->GetPrivateData(WKPDID_D3DDebugObjectName, &size, name.data())))
        {
            return {};
        }
        return { name.data(), size };
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
//  Graphics
// ===========================================

Graphics::Graphics(
    const bool _bEnableDebugLayer)
{
    JUG_ASSERT(!g_pSingleton, "Graphics instance already exists.");
    g_pSingleton = this;

    m_caps.bDebugLayerEnabled = _bEnableDebugLayer;
    m_dxgi.FillCaps(m_caps);
    InitDevice_(m_caps.bDebugLayerEnabled);
    InitTimerQueries_();
    InitDebugInterfacesIfNeed_();
}

Graphics::~Graphics()
{
    JUG_ASSERT(g_pSingleton == this, "Graphics instance is not the singleton.");
    g_pSingleton = nullptr;

    // 컨텍스트 초기화
    m_pD3d11DeviceContext->ClearState();
    m_pD3d11DeviceContext->Flush();

    // 타이머 파괴
    CleanUpTimerQueries_();

    // 전체화면 상태로 스왑체인을 놓으면 즉시 크래시한다. 모두 창모드로 변경
    for (FrameBufferD3D11& frameBuffer: m_frameBufferPool.GetResources())
    {
        if (frameBuffer.pSwapChain)
        {
            JUG_DISCARD_RETURN(frameBuffer.pSwapChain->SetFullscreenState(FALSE, nullptr));
        }
    }

    if (ReportLiveObjects() > 0)
    {
        JUG_ASSERT(false, "There are leaked graphics resources. Check the log for details.");
    }

    for (FrameBufferD3D11& frameBuffer: m_frameBufferPool.GetResources())
    {
        for (ID3D11RenderTargetView*& pRTV: frameBuffer.rtvs)
        {
            JUG_DX_RELEASE(pRTV);
        }
        JUG_DX_RELEASE(frameBuffer.pDSV);
        JUG_DX_RELEASE(frameBuffer.pSwapChain);
    }

    for (VertexBufferD3D11& vertexBuffer: m_vertexBufferPool.GetResources())
    {
        JUG_DX_RELEASE(vertexBuffer.pBuffer);
    }

    for (InstanceBufferD3D11& instanceBuffer: m_instanceBufferPool.GetResources())
    {
        JUG_DX_RELEASE(instanceBuffer.pBuffer);
    }

    for (IndexBufferD3D11& indexBuffer: m_indexBufferPool.GetResources())
    {
        JUG_DX_RELEASE(indexBuffer.pBuffer);
    }

    for (ConstantBufferD3D11& constantBuffer: m_constantBufferPool.GetResources())
    {
        JUG_DX_RELEASE(constantBuffer.pBuffer);
    }

    for (StorageBufferD3D11& storageBuffer: m_storageBufferPool.GetResources())
    {
        JUG_DX_RELEASE(storageBuffer.pSRV);
        JUG_DX_RELEASE(storageBuffer.pUAV);
        JUG_DX_RELEASE(storageBuffer.pBuffer);
    }

    for (TextureD3D11& texture: m_texturePool.GetResources())
    {
        JUG_DX_RELEASE(texture.pSRV);
        JUG_DX_RELEASE(texture.pUAV);
        JUG_DX_RELEASE(texture.pMsaaRtResource);
        JUG_DX_RELEASE(texture.pResource);
    }

    for (ShaderD3D11& shader: m_shaderPool.GetResources())
    {
        JUG_DX_RELEASE(shader.pVS);   // 유니온이라 아무 멤버나 놔도 된다.
    }

    m_frameBufferPool.Clear();
    m_vertexBufferPool.Clear();
    m_instanceBufferPool.Clear();
    m_indexBufferPool.Clear();
    m_constantBufferPool.Clear();
    m_storageBufferPool.Clear();
    m_texturePool.Clear();
    m_shaderPool.Clear();
    m_programPool.Clear();
    m_vertexLayoutPool.Clear();
    m_swapChainFbhs.clear();
    m_shaderCache.clear();
    m_vlhCache.clear();

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
    JUG_DX_RELEASE(m_pD3d11InfoQueueOrNull);
    JUG_DX_RELEASE(m_pD3d11DeviceContext);
    JUG_DX_RELEASE(m_pD3d11Device);
    if (m_pD3d11DebugOrNull)
    {
        JUG_DISCARD_RETURN(m_pD3d11DebugOrNull->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL));
        JUG_DX_RELEASE(m_pD3d11DebugOrNull);
    }
}

// ===========================================
//  System
// ===========================================

Graphics& Graphics::GetSingleton()
{
    JUG_ASSERT(g_pSingleton, "Graphics instance is not created yet.");
    return *g_pSingleton;
}

size_t Graphics::ReportLiveObjects()
{
    size_t num = 0;

    for (auto it = m_vertexBufferPool.Begin(); it != m_vertexBufferPool.End(); ++it)
    {
        const VertexBufferD3D11& vb          = *it;
        const String             nameOrEmpty = QueryD3d11DebugNameOrEmpty_(vb.pBuffer);
        JUG_CORE_LOG_WARN("Leaked VertexBuffer: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_instanceBufferPool.Begin(); it != m_instanceBufferPool.End(); ++it)
    {
        const InstanceBufferD3D11& instb       = *it;
        const String               nameOrEmpty = QueryD3d11DebugNameOrEmpty_(instb.pBuffer);
        JUG_CORE_LOG_WARN("Leaked InstanceBuffer: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_indexBufferPool.Begin(); it != m_indexBufferPool.End(); ++it)
    {
        const IndexBufferD3D11& ib          = *it;
        const String            nameOrEmpty = QueryD3d11DebugNameOrEmpty_(ib.pBuffer);
        JUG_CORE_LOG_WARN("Leaked IndexBuffer: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_storageBufferPool.Begin(); it != m_storageBufferPool.End(); ++it)
    {
        const StorageBufferD3D11& sb          = *it;
        const String              nameOrEmpty = QueryD3d11DebugNameOrEmpty_(sb.pBuffer);
        JUG_CORE_LOG_WARN("Leaked StorageBuffer: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_constantBufferPool.Begin(); it != m_constantBufferPool.End(); ++it)
    {
        const ConstantBufferD3D11& cb          = *it;
        const String               nameOrEmpty = QueryD3d11DebugNameOrEmpty_(cb.pBuffer);
        JUG_CORE_LOG_WARN("Leaked ConstantBuffer: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_texturePool.Begin(); it != m_texturePool.End(); ++it)
    {
        const TextureD3D11& texture     = *it;
        const String        nameOrEmpty = QueryD3d11DebugNameOrEmpty_(texture.pResource);
        JUG_CORE_LOG_WARN("Leaked Texture: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_frameBufferPool.Begin(); it != m_frameBufferPool.End(); ++it)
    {
        ID3D11DeviceChild* pRes = it->pDSV;
        if (!pRes)
        {
            pRes = it->rtvs[0];
            JUG_ASSERT(pRes, "Invalid frame buffer resource.");
        }
        const String nameOrEmpty = QueryD3d11DebugNameOrEmpty_(pRes);
        JUG_CORE_LOG_WARN("Leaked FrameBuffer: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_shaderPool.Begin(); it != m_shaderPool.End(); ++it)
    {
        const ShaderD3D11& shader      = *it;
        const String       nameOrEmpty = QueryD3d11DebugNameOrEmpty_(shader.pVS);
        JUG_CORE_LOG_WARN("Leaked Shader: Handle = {}, Name = '{}')", it.GetHandle(), nameOrEmpty);
        ++num;
    }

    for (auto it = m_programPool.Begin(); it != m_programPool.End(); ++it)
    {
        const ProgramDesc& program = *it;
        JUG_CORE_LOG_WARN("Leaked Program: Handle = {})", it.GetHandle());
        ++num;
    }

    for (auto it = m_vertexLayoutPool.Begin(); it != m_vertexLayoutPool.End(); ++it)
    {
        const VertexLayoutDesc& vl = *it;
        JUG_CORE_LOG_WARN("Leaked VertexLayout: Handle = {}", it.GetHandle());
        ++num;
    }

    if (num > 0)
    {
        JUG_CORE_LOG_WARN("Total leaked graphics resources: {}", num);
    }

    return num;
}

const GraphicsCaps& Graphics::GetCaps() const
{
    return m_caps;
}

const GraphicsStats& Graphics::GetStats() const
{
    return m_lastStats;
}

// ===========================================
//  Vertex Stream
// ===========================================

VertexBufferHandle Graphics::CreateVertexBuffer(
    const MemoryView    _vertexData,
    const VertexLayout& _vl)
{
    const uint32_t stride = _vl.GetStride();
    JUG_ASSERT(!_vertexData.IsEmpty(), "A static vertex buffer requires initial data. Use CreateDynamicVertexBuffer instead.");
    JUG_ASSERT(_vertexData.GetSize() % stride == 0, "Vertex data size is not a multiple of the vertex stride.");

    const uint32_t           numVertices = static_cast<uint32_t>(_vertexData.GetSize()) / stride;
    const VertexBufferHandle vbh         = m_vertexBufferPool.Emplace(CreateVertexBuffer_(numVertices, _vl, false, _vertexData));
    JUG_CORE_LOG_TRACE("VertexBuffer created. handle = {}, numVertices = {}, stride = {}, bytes = {}, dynamic = false", vbh, numVertices, stride, _vertexData.GetSize());
    return vbh;
}

VertexBufferHandle Graphics::CreateDynamicVertexBuffer(
    const uint32_t      _numVertices,
    const VertexLayout& _vl)
{
    const VertexBufferHandle vbh = m_vertexBufferPool.Emplace(CreateVertexBuffer_(_numVertices, _vl, true, {}));
    JUG_CORE_LOG_TRACE("VertexBuffer created. handle = {}, numVertices = {}, stride = {}, dynamic = true", vbh, _numVertices, _vl.GetStride());
    return vbh;
}

InstanceBufferHandle Graphics::CreateInstanceBuffer(
    const uint32_t _numInstances,
    const uint32_t _stride)
{
    const InstanceBufferHandle instbh = m_instanceBufferPool.Emplace(CreateInstanceBuffer_(_numInstances, _stride));
    JUG_CORE_LOG_TRACE("InstanceBuffer created. handle = {}, numInstances = {}, stride = {}, dynamic = true", instbh, _numInstances, _stride);
    return instbh;
}

// ===========================================
//  Index Buffer
// ===========================================

IndexBufferHandle Graphics::CreateIndexBuffer(
    const MemoryView _indexData,
    const bool       _bU32)
{
    const uint32_t stride = _bU32 ? sizeof(uint32_t) : sizeof(uint16_t);
    JUG_ASSERT(!_indexData.IsEmpty(), "A static index buffer requires initial data. Use CreateDynamicIndexBuffer instead.");
    JUG_ASSERT(_indexData.GetSize() % stride == 0, "Index data size is not a multiple of the index stride.");
    JUG_ASSERT(_indexData.GetSize() > 0, "Index data must not be empty.");

    const uint32_t          numIndices = static_cast<uint32_t>(_indexData.GetSize()) / stride;
    const IndexBufferHandle ibh        = m_indexBufferPool.Emplace(CreateIndexBuffer_(numIndices, _bU32, false, _indexData));
    JUG_CORE_LOG_TRACE("IndexBuffer created. handle = {}, numIndices = {}, u32 = {}, bytes = {}, dynamic = false", ibh, numIndices, _bU32, _indexData.GetSize());
    return ibh;
}

IndexBufferHandle Graphics::CreateDynamicIndexBuffer(
    const uint32_t _numIndices,
    const bool     _bU32)
{
    const IndexBufferHandle ibh = m_indexBufferPool.Emplace(CreateIndexBuffer_(_numIndices, _bU32, true, {}));
    JUG_CORE_LOG_TRACE("IndexBuffer created. handle = {}, numIndices = {}, u32 = {}, dynamic = true", ibh, _numIndices, _bU32);
    return ibh;
}

// ===========================================
//  Constant Buffer
// ===========================================

ConstantBufferHandle Graphics::CreateConstantBuffer(
    const uint32_t _byteWidth)
{
    const ConstantBufferHandle cbh = m_constantBufferPool.Emplace(CreateConstantBuffer_(_byteWidth));
    JUG_CORE_LOG_TRACE("ConstantBuffer created. handle = {}, bytes = {}", cbh, _byteWidth);
    return cbh;
}

// ===========================================
//  Storage Buffer
// ===========================================

StorageBufferHandle Graphics::CreateStructuredBuffer(
    const uint32_t                    _numElements,
    const uint32_t                    _stride,
    const Flags<eStorageBufferOption> _flags,
    const MemoryView                  _initDataOrEmpty)
{
    const StorageBufferHandle sbh = m_storageBufferPool.Emplace(CreateStructuredBuffer_(_numElements, _stride, _flags, _initDataOrEmpty));
    JUG_CORE_LOG_TRACE("StorageBuffer created. handle = {}, m_type = Structured, numElements = {}, stride = {}, flags = {:#x}", sbh, _numElements, _stride, _flags.GetFlags());
    return sbh;
}

StorageBufferHandle Graphics::CreateReadbackBuffer(
    const uint32_t _byteWidth)
{
    const StorageBufferHandle sbh = m_storageBufferPool.Emplace(CreateReadbackBuffer_(_byteWidth));
    JUG_CORE_LOG_TRACE("StorageBuffer created. handle = {}, m_type = Readback, bytes = {}", sbh, _byteWidth);
    return sbh;
}

StorageBufferHandle Graphics::CreateIndirectBuffer(
    const uint32_t                    _numDraws,
    const Flags<eStorageBufferOption> _flags)
{
    const StorageBufferHandle sbh = m_storageBufferPool.Emplace(CreateIndirectArgsBuffer_(_numDraws, _flags));
    JUG_CORE_LOG_TRACE("StorageBuffer created. handle = {}, m_type = IndirectArgs, numDraws = {}, flags = {:#x}", sbh, _numDraws, _flags.GetFlags());
    return sbh;
}

// ===========================================
//  Buffer Utils
// ===========================================

void Graphics::UpdateBuffer(
    const BufferRef  _buffer,
    const MemoryView _data,
    const uint32_t   _offset)
{
    JUG_ASSERT(!_data.IsEmpty(), "UpdateBuffer requires a non-empty data.");

    const ResolveBufferRefResult resolve = ResolveBufferRef_(_buffer);
    const uint32_t               size    = static_cast<uint32_t>(_data.GetSize());
    JUG_ASSERT(_offset + size <= resolve.byteWidth, "UpdateBuffer data size exceeds the buffer size.");

    if (resolve.bDynamic)
    {
        if (_offset == 0 && size == resolve.byteWidth)
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            JUG_DX_CHECK(m_pD3d11DeviceContext->Map(resolve.pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
            std::memcpy(mapped.pData, _data.GetPtr(), size);
            m_pD3d11DeviceContext->Unmap(resolve.pBuffer, 0);
        }
        else
        {
            // dyniamic buffer의 부분 업데이트는 staging buffer를 사용하여 copySubresourceRegion로 처리한다.
            JUG_ASSERT(_buffer.GetType() != eBuffer::Constant, "A constant buffer cannot be partially updated.");

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
            m_pD3d11DeviceContext->CopySubresourceRegion(resolve.pBuffer, 0, _offset, 0, 0, m_pUploadBuffer, 0, &box);
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
        m_pD3d11DeviceContext->UpdateSubresource(resolve.pBuffer, 0, &box, _data.GetPtr(), 0, 0);
    }
}

MutableMemoryView Graphics::MapBuffer(
    const BufferRef _buffer)
{
    const ResolveBufferRefResult resolve = ResolveBufferRef_(_buffer);
    JUG_ASSERT(resolve.bDynamic, "Only a dynamic buffer can be mapped.");

    D3D11_MAPPED_SUBRESOURCE mapped;
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(resolve.pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    return { mapped.pData, mapped.RowPitch };
}

void Graphics::UnmapBuffer(
    const BufferRef _buffer)
{
    const ResolveBufferRefResult ret = ResolveBufferRef_(_buffer);
    JUG_ASSERT(ret.bDynamic, "Only a dynamic buffer can be unmapped.");
    m_pD3d11DeviceContext->Unmap(ret.pBuffer, 0);
}

void Graphics::CopyBuffer(
    const BufferRef _dst,
    const BufferRef _src)
{
    const ResolveBufferRefResult dst = ResolveBufferRef_(_dst);
    const ResolveBufferRefResult src = ResolveBufferRef_(_src);

    JUG_ASSERT(!dst.bDynamic, "A dynamic buffer cannot be a copy destination.");
    JUG_ASSERT(dst.pBuffer != src.pBuffer, "A buffer cannot be copied onto itself.");
    JUG_ASSERT(dst.byteWidth == src.byteWidth, "A whole buffer copy requires both buffers to have the same size.");

    m_pD3d11DeviceContext->CopyResource(dst.pBuffer, src.pBuffer);
}

void Graphics::CopyBuffer(
    const BufferRef _dst,
    const uint32_t  _dstOffset,
    const BufferRef _src,
    const uint32_t  _srcOffset,
    const uint32_t  _byteWidthOrZero)
{
    const ResolveBufferRefResult dst = ResolveBufferRef_(_dst);
    const ResolveBufferRefResult src = ResolveBufferRef_(_src);

    JUG_ASSERT(!dst.bDynamic, "A dynamic buffer cannot be a copy destination.");
    JUG_ASSERT(_srcOffset < src.byteWidth, "Buffer copy source offset is out of bounds.");

    const uint32_t size = _byteWidthOrZero == 0 ? src.byteWidth - _srcOffset : _byteWidthOrZero;

    JUG_ASSERT(_srcOffset + size <= src.byteWidth, "Buffer copy source range is out of bounds.");
    JUG_ASSERT(_dstOffset + size <= dst.byteWidth, "Buffer copy destination range is out of bounds.");
    JUG_ASSERT(dst.pBuffer != src.pBuffer || _dstOffset + size <= _srcOffset || _srcOffset + size <= _dstOffset, "A buffer copy cannot overlap itself.");

    D3D11_BOX box;
    box.left   = _srcOffset;
    box.right  = _srcOffset + size;
    box.top    = 0;
    box.bottom = 1;
    box.front  = 0;
    box.back   = 1;
    m_pD3d11DeviceContext->CopySubresourceRegion(dst.pBuffer, 0, _dstOffset, 0, 0, src.pBuffer, 0, &box);
}

size_t Graphics::ReadBuffer(
    const StorageBufferHandle _readbackSbh,
    const MutableMemoryView   _dst)
{
    JUG_ASSERT(!_dst.IsEmpty(), "ReadBuffer requires a non-empty destination buffer.");

    const StorageBufferD3D11& sb = m_storageBufferPool[_readbackSbh];
    JUG_ASSERT(sb.type == eStorageBuffer::Readback, "ReadBuffer can only be called on a readback storage buffer.");

    D3D11_MAPPED_SUBRESOURCE mapped;
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(sb.pBuffer, 0, D3D11_MAP_READ, 0, &mapped));
    const size_t read = Min(_dst.GetSize(), static_cast<size_t>(sb.byteWidth));
    std::memcpy(_dst.GetPtr(), mapped.pData, read);
    m_pD3d11DeviceContext->Unmap(sb.pBuffer, 0);
    return read;
}

// ===========================================
//  Texture
// ===========================================

TextureHandle Graphics::CreateTexture2D(
    const uint32_t               _width,
    const uint32_t               _height,
    const eTextureFormat         _format,
    const uint32_t               _numLayers,
    const eMSAA                  _msaa,
    const Flags<eTextureOption>  _flags,
    const Span<const MemoryView> _initDataOrEmpty)
{
    const TextureHandle texh = m_texturePool.Emplace(CreateTexture_(_width, _height, 1, eTexture::Texture2D, _format, _numLayers, _msaa, _flags, _initDataOrEmpty));
    const TextureDesc&  td   = m_texturePool[texh];
    JUG_CORE_LOG_TRACE("Texture2D created. handle = {}, {}x{}, format = {}, layers = {}, mips = {}, msaa = {}, flags = {:#x}", texh, td.width, td.height, static_cast<uint32_t>(td.format), td.numLayers, td.numMips, static_cast<uint32_t>(td.msaa), td.flags.GetFlags());
    return texh;
}

TextureHandle Graphics::CreateTextureCube(
    const uint32_t               _width,
    const uint32_t               _height,
    const eTextureFormat         _format,
    const uint32_t               _numCubes,
    const Flags<eTextureOption>  _flags,
    const Span<const MemoryView> _initDataOrEmpty)
{
    const TextureHandle texh = m_texturePool.Emplace(CreateTexture_(_width, _height, 1, eTexture::TextureCube, _format, _numCubes * 6, eMSAA::None, _flags, _initDataOrEmpty));
    const TextureDesc&  td   = m_texturePool[texh];
    JUG_CORE_LOG_TRACE("TextureCube created. handle = {}, {}x{}, format = {}, cubes = {}, mips = {}, flags = {:#x}", texh, td.width, td.height, static_cast<uint32_t>(td.format), _numCubes, td.numMips, td.flags.GetFlags());
    return texh;
}

TextureHandle Graphics::CreateTexture3D(
    const uint32_t               _width,
    const uint32_t               _height,
    const uint32_t               _depth,
    const eTextureFormat         _format,
    const Flags<eTextureOption>  _flags,
    const Span<const MemoryView> _initDataOrEmpty)
{
    const TextureHandle texh = m_texturePool.Emplace(CreateTexture_(_width, _height, _depth, eTexture::Texture3D, _format, 1, eMSAA::None, _flags, _initDataOrEmpty));
    const TextureDesc&  td   = m_texturePool[texh];
    JUG_CORE_LOG_TRACE("Texture3D created. handle = {}, {}x{}x{}, format = {}, mips = {}, flags = {:#x}", texh, td.width, td.height, td.depth, static_cast<uint32_t>(td.format), td.numMips, td.flags.GetFlags());
    return texh;
}

// ===========================================
//  Init
// ===========================================

void Graphics::InitDevice_(
    const bool _bEnableDebugLayer)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef JUG_DEBUG
    if (_bEnableDebugLayer)
    {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif
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
#ifdef JUG_DEBUG
        if (_bEnableDebugLayer)
        {
            JUG_CORE_LOG_WARN("D3D11 debug layer is not available. Retrying without it. ({})", MakeErrorMessage_(hr));
            hr = CreateD3d11Device_(pAdapterOrNull, type, flags & ~D3D11_CREATE_DEVICE_DEBUG, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);
            if (SUCCEEDED(hr))
            {
                break;
            }
        }
#endif
        JUG_CORE_LOG_WARN("Failed to create a D3D11 device with driver m_type {}. ({})", static_cast<uint32_t>(type), MakeErrorMessage_(hr));
    }

    if (i == driverTypes.size())
    {
        JUG_FATAL("Failed to create a D3D11 device with any driver m_type.");
    }

    m_d3dDriverType             = driverTypes[i];
    m_caps.bDebugLayerEnabled   = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
    m_caps.bSoftwareRasterizer  = m_d3dDriverType == D3D_DRIVER_TYPE_WARP || m_d3dDriverType == D3D_DRIVER_TYPE_REFERENCE;
    const uint32_t featureLevel = static_cast<uint32_t>(m_d3dFeatureLevel);
    JUG_CORE_LOG_INFO("D3D11 device created. driverType = {}, featureLevel = {}.{}, debugLayer = {}, softwareRasterizer = {}", NameOf(m_d3dDriverType), (featureLevel >> 12) & 0xF, (featureLevel >> 8) & 0xF, m_caps.bDebugLayerEnabled, m_caps.bSoftwareRasterizer);

    if (m_caps.bSoftwareRasterizer)
    {
        m_caps.vendorID               = 0;
        m_caps.deviceID               = 0;
        m_caps.videoMemorySize        = 0;
        m_caps.systemMemorySize       = 0;
        m_caps.sharedSystemMemorySize = 0;
    }

    SetD3d11ObjectName_(m_pD3d11DeviceContext, "JugGfx.DirectX11.ImmediateContext");
}

void Graphics::InitDebugInterfacesIfNeed_()
{
    if (!m_caps.bDebugLayerEnabled)
    {
        return;
    }

    JUG_DISCARD_RETURN(m_pD3d11DeviceContext->QueryInterface(IID_PPV_ARGS(&m_pUserAnnotationOrNull)));
    JUG_DISCARD_RETURN(m_pD3d11Device->QueryInterface(IID_PPV_ARGS(&m_pD3d11DebugOrNull)));
    JUG_DISCARD_RETURN(m_pD3d11Device->QueryInterface(IID_PPV_ARGS(&m_pD3d11InfoQueueOrNull)));

    if (m_pD3d11InfoQueueOrNull && ::IsDebuggerPresent())
    {
        JUG_DISCARD_RETURN(m_pD3d11InfoQueueOrNull->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE));
        JUG_DISCARD_RETURN(m_pD3d11InfoQueueOrNull->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE));
    }

    JUG_CORE_LOG_INFO("D3D11 debug interfaces: annotation = {}, debug = {}, infoQueue = {}, breakOnError = {}", m_pUserAnnotationOrNull != nullptr, m_pD3d11DebugOrNull != nullptr, m_pD3d11InfoQueueOrNull != nullptr, m_pD3d11InfoQueueOrNull != nullptr && ::IsDebuggerPresent() != FALSE);
}

void Graphics::InitTimerQueries_()
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

void Graphics::CleanUpTimerQueries_()
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
//  Buffer Internal
// ===========================================

Graphics::VertexBufferD3D11 Graphics::CreateVertexBuffer_(
    const uint32_t      _numElems,
    const VertexLayout& _vl,
    const bool          _bDynamic,
    const MemoryView    _initDataOrEmpty)
{
    JUG_ASSERT(_numElems > 0, "Vertex buffer must have at least one element.");
    JUG_ASSERT(_vl.GetNumAttributes() > 0, "Vertex layout must have at least one attribute.");

    VertexBufferD3D11 vb = {};
    vb.bDynamic          = _bDynamic;
    vb.stride            = _vl.GetStride();
    vb.numVertices       = _numElems;
    vb.byteWidth         = vb.stride * _numElems;
    vb.vlh               = GetOrCreateVertexLayoutHandle_(_vl);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = vb.stride * _numElems;
    bd.Usage             = _bDynamic                  ? D3D11_USAGE_DYNAMIC
                         : _initDataOrEmpty.IsEmpty() ? D3D11_USAGE_DEFAULT
                                                      : D3D11_USAGE_IMMUTABLE;
    bd.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags    = _bDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    bd.MiscFlags         = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem                = _initDataOrEmpty.GetPtr();
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, _initDataOrEmpty.IsEmpty() ? nullptr : &initData, &vb.pBuffer));
    return vb;
}

Graphics::InstanceBufferD3D11 Graphics::CreateInstanceBuffer_(
    const uint32_t _numElems,
    const uint32_t _stride) const
{
    JUG_ASSERT(_numElems > 0, "Instance buffer must have at least one element.");
    JUG_ASSERT(_stride > 0 && _stride % kInstanceDataSizeAlign == 0, "Instance stride must be a non-zero multiple of the instance data alignment.");

    InstanceBufferD3D11 instb = {};
    instb.stride              = _stride;
    instb.numInstances        = _numElems;
    instb.byteWidth           = _stride * _numElems;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = instb.byteWidth;
    bd.Usage             = D3D11_USAGE_DYNAMIC;
    bd.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags         = 0;

    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &instb.pBuffer));
    return instb;
}

Graphics::IndexBufferD3D11 Graphics::CreateIndexBuffer_(
    const uint32_t   _numElems,
    const bool       _bU32,
    const bool       _bDynamic,
    const MemoryView _initDataOrEmpty) const
{
    JUG_ASSERT(_numElems > 0, "Index buffer must have at least one element.");

    IndexBufferD3D11 ib = {};
    ib.bDynamic         = _bDynamic;
    ib.format           = _bU32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    ib.bU32             = _bU32;
    ib.numIndices       = _numElems;
    ib.byteWidth        = (_bU32 ? sizeof(uint32_t) : sizeof(uint16_t)) * _numElems;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = ib.byteWidth;
    bd.Usage             = _bDynamic                  ? D3D11_USAGE_DYNAMIC
                         : _initDataOrEmpty.IsEmpty() ? D3D11_USAGE_DEFAULT
                                                      : D3D11_USAGE_IMMUTABLE;
    bd.BindFlags         = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags    = _bDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    bd.MiscFlags         = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem                = _initDataOrEmpty.GetPtr();
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, _initDataOrEmpty.IsEmpty() ? nullptr : &initData, &ib.pBuffer));
    return ib;
}

Graphics::ConstantBufferD3D11 Graphics::CreateConstantBuffer_(
    const uint32_t _byteWidth) const
{
    JUG_ASSERT(_byteWidth > 0 && _byteWidth % kCBufferSizeAlign == 0, "Constant buffer size must be a non-zero multiple of 16 bytes.");

    ConstantBufferD3D11 cb = {};
    cb.byteWidth           = _byteWidth;

    D3D11_BUFFER_DESC bd   = {};
    bd.ByteWidth           = cb.byteWidth;
    bd.Usage               = D3D11_USAGE_DYNAMIC;
    bd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags           = 0;
    bd.StructureByteStride = 0;

    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &cb.pBuffer));
    return cb;
}

Graphics::StorageBufferD3D11 Graphics::CreateStructuredBuffer_(
    const uint32_t                    _numElements,
    const uint32_t                    _stride,
    const Flags<eStorageBufferOption> _flags,
    const MemoryView                  _initDataOrEmpty) const
{
    JUG_ASSERT(!_flags.HasAll({ eStorageBufferOption::Dynamic, eStorageBufferOption::ShaderReadWrite }), "A storage buffer cannot be both dynamic and shader read writable.");

    StorageBufferD3D11 sb = {};
    sb.byteWidth          = _stride * _numElements;
    sb.stride             = _stride;
    sb.flags              = _flags;
    sb.type               = eStorageBuffer::Structured;

    D3D11_BUFFER_DESC bd   = {};
    bd.ByteWidth           = _stride * _numElements;
    bd.CPUAccessFlags      = _flags & eStorageBufferOption::Dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = _stride;
    bd.Usage               = _flags & eStorageBufferOption::Dynamic ? D3D11_USAGE_DYNAMIC
                           : _initDataOrEmpty.IsEmpty()             ? D3D11_USAGE_DEFAULT
                                                                    : D3D11_USAGE_IMMUTABLE;
    // uav의 경우 default usage만 가능.
    if (_flags & eStorageBufferOption::ShaderReadWrite)
    {
        bd.Usage = D3D11_USAGE_DEFAULT;
    }

    // bind flags
    bd.BindFlags = 0;
    if (_flags & eStorageBufferOption::ShaderRead)
    {
        bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (_flags & eStorageBufferOption::ShaderReadWrite)
    {
        bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem                = _initDataOrEmpty.GetPtr();
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, _initDataOrEmpty.IsEmpty() ? nullptr : &initData, &sb.pBuffer));

    // create SRV
    if (_flags & eStorageBufferOption::ShaderRead)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                          = DXGI_FORMAT_UNKNOWN;
        srvd.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Buffer.FirstElement             = 0;
        srvd.Buffer.NumElements              = _numElements;
        JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(sb.pBuffer, &srvd, &sb.pSRV));
    }

    // create UAV
    if (_flags & eStorageBufferOption::ShaderReadWrite)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format                           = DXGI_FORMAT_UNKNOWN;
        uavd.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Buffer.FirstElement              = 0;
        uavd.Buffer.NumElements               = _numElements;
        JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(sb.pBuffer, &uavd, &sb.pUAV));
    }

    return sb;
}

Graphics::StorageBufferD3D11 Graphics::CreateIndirectArgsBuffer_(
    const uint32_t                    _numDraws,
    const Flags<eStorageBufferOption> _flags) const
{
    JUG_ASSERT(_numDraws > 0, "An indirect args buffer requires at least one draw.");
    JUG_ASSERT(!_flags.Has(eStorageBufferOption::Dynamic), "An indirect args buffer cannot be dynamic.");

    StorageBufferD3D11 sb = {};
    sb.stride             = kIndirectArgsStride;
    sb.byteWidth          = kIndirectArgsStride * _numDraws;
    sb.type               = eStorageBuffer::IndirectArgs;
    sb.flags              = _flags;

    D3D11_BUFFER_DESC bd   = {};
    bd.ByteWidth           = sb.byteWidth;
    bd.Usage               = D3D11_USAGE_DEFAULT;
    bd.MiscFlags           = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    bd.StructureByteStride = 0;

    // bind flags
    bd.BindFlags = 0;
    if (_flags & eStorageBufferOption::ShaderRead)
    {
        bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (_flags & eStorageBufferOption::ShaderReadWrite)
    {
        bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    // create buffer
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &sb.pBuffer));

    // use typed-buffer for indirect args
    const uint32_t numU32 = sb.byteWidth / 4;

    // create SRV
    if (_flags & eStorageBufferOption::ShaderRead)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                          = DXGI_FORMAT_R32_UINT;
        srvd.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Buffer.FirstElement             = 0;
        srvd.Buffer.NumElements              = numU32;
        JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(sb.pBuffer, &srvd, &sb.pSRV));
    }

    // create UAV
    if (_flags & eStorageBufferOption::ShaderReadWrite)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format                           = DXGI_FORMAT_R32_UINT;
        uavd.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Buffer.FirstElement              = 0;
        uavd.Buffer.NumElements               = numU32;
        uavd.Buffer.Flags                     = 0;
        JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(sb.pBuffer, &uavd, &sb.pUAV));
    }

    return sb;
}

Graphics::StorageBufferD3D11 Graphics::CreateReadbackBuffer_(
    const uint32_t _byteWidth) const
{
    JUG_ASSERT(_byteWidth > 0, "A readback buffer requires a non-zero size.");

    StorageBufferD3D11 sb = {};
    sb.byteWidth          = _byteWidth;
    sb.stride             = 0;
    sb.type               = eStorageBuffer::Readback;
    sb.flags              = {};

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = _byteWidth;
    bd.Usage             = D3D11_USAGE_STAGING;
    bd.BindFlags         = 0;
    bd.CPUAccessFlags    = D3D11_CPU_ACCESS_READ;
    bd.MiscFlags         = 0;
    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &sb.pBuffer));
    return sb;
}

void Graphics::ReleaseVertexLayout_(
    const VertexLayoutHandle _vlh)
{
    VertexLayoutDesc& vl = m_vertexLayoutPool[_vlh];

    JUG_ASSERT(vl.refCount > 0, "Vertex layout reference count underflow.");
    if (--vl.refCount > 0)
    {
        return;
    }

    // refcount == 0. erase from cache
    m_vlhCache.erase(vl.vl.GetHash());
    m_vertexLayoutPool.Erase(_vlh);
}

VertexLayoutHandle Graphics::GetOrCreateVertexLayoutHandle_(
    const VertexLayout& _vl)
{
    JUG_ASSERT(_vl.GetNumAttributes() > 0, "A vertex layout must have at least one attribute.");

    auto it = m_vlhCache.find(_vl.GetHash());
    if (it != m_vlhCache.end())
    {
        const VertexLayoutHandle vlh = it->second;
        ++m_vertexLayoutPool[vlh].refCount;
        return vlh;
    }

    const VertexLayoutHandle vlh     = m_vertexLayoutPool.Emplace(_vl);
    m_vertexLayoutPool[vlh].refCount = 1;
    m_vlhCache.emplace(_vl.GetHash(), vlh);
    return vlh;
}

Graphics::ResolveBufferRefResult Graphics::ResolveBufferRef_(
    const BufferRef _buffer)
{
    JUG_ASSERT(_buffer, "Invalid buffer handle.");

    switch (_buffer.GetType())
    {
        case eBuffer::Vertex:
        {
            VertexBufferD3D11& vb = m_vertexBufferPool[_buffer.GetVertexBufferHandle()];
            return { vb.pBuffer, vb.byteWidth, vb.bDynamic };
        }
        case eBuffer::Instance:
        {
            InstanceBufferD3D11& instb = m_instanceBufferPool[_buffer.GetInstanceBufferHandle()];
            return { instb.pBuffer, instb.byteWidth, true };
        }
        case eBuffer::Index:
        {
            IndexBufferD3D11& ib = m_indexBufferPool[_buffer.GetIndexBufferHandle()];
            return { ib.pBuffer, ib.byteWidth, ib.bDynamic };
        }
        case eBuffer::Constant:
        {
            ConstantBufferD3D11& cb = m_constantBufferPool[_buffer.GetConstantBufferHandle()];
            return { cb.pBuffer, cb.byteWidth, true };
        }
        case eBuffer::Storage:
        {
            StorageBufferD3D11& sb = m_storageBufferPool[_buffer.GetStorageBufferHandle()];
            return { sb.pBuffer, sb.byteWidth, sb.flags.Has(eStorageBufferOption::Dynamic) };
        }
        default:
        {
            JUG_ASSERT(false, "Unsupported buffer m_type {}.", static_cast<uint32_t>(_buffer.GetType()));
            return {};
        }
    }
}

// ===========================================
//  Texture Internal
// ===========================================

DXGI_SAMPLE_DESC Graphics::MakeSampleDesc_(const DXGI_FORMAT _format, const eMSAA _msaa) const
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

Graphics::TextureD3D11 Graphics::CreateTexture_(
    uint32_t               _width,
    uint32_t               _height,
    uint32_t               _depth,
    eTexture               _type,
    eTextureFormat         _format,
    uint32_t               _numLayers,
    eMSAA                  _msaa,
    Flags<eTextureOption>  _flags,
    Span<const MemoryView> _initData) const
{
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "A texture must have a non-zero size.");
    JUG_ASSERT(_numLayers > 0, "A texture must have at least one layer.");
    JUG_ASSERT(_format != eTextureFormat::Unknown, "A texture must have a known format.");

    const bool bDepth    = IsDepthFormat(_format);
    const bool bReadback = _flags.Has(eTextureOption::Readback);
    const bool b3D       = _type == eTexture::Texture3D;

    JUG_ASSERT(_type != eTexture::TextureCube || _numLayers % 6 == 0, "A cube texture requires a layer count that is a multiple of 6.");
    JUG_ASSERT(!b3D || _numLayers == 1, "A 3D texture cannot have layers. Use the depth instead.");
    JUG_ASSERT(b3D || _depth == 1, "Only a 3D texture can have a depth greater than 1.");
    JUG_ASSERT(!bDepth || !_flags.Has(eTextureOption::ShaderReadWrite), "A depth-stencil texture cannot be bound as an unordered access view.");
    JUG_ASSERT(!bReadback || _flags.GetFlags() == static_cast<uint32_t>(eTextureOption::Readback), "A readback texture cannot carry any other option.");
    JUG_ASSERT(!bReadback || _initData.empty(), "A readback texture cannot be initialized with data.");
    JUG_ASSERT(!bReadback || _msaa == eMSAA::None, "A readback texture cannot be multisampled.");
    JUG_ASSERT(!b3D || _msaa == eMSAA::None, "A 3D texture cannot be multisampled.");

    const TextureFormatInfo formatInfo = MakeTextureFormatInfo_(_format);
    DXGI_SAMPLE_DESC        sd         = MakeSampleDesc_(formatInfo.tex, _msaa);

    TextureD3D11 texture = {};
    texture.width        = _width;
    texture.height       = _height;
    texture.depth        = _depth;
    texture.type         = _type;
    texture.format       = _format;
    texture.numLayers    = _numLayers;
    texture.numMips      = _flags & eTextureOption::HasMips ? CalcNumMips(_width, _height, _depth) : 1;
    texture.refCount     = 1;
    texture.flags        = _flags;
    switch (sd.Count)
    {
        case 1: texture.msaa = eMSAA::None; break;
        case 2: texture.msaa = eMSAA::x2; break;
        case 4: texture.msaa = eMSAA::x4; break;
        case 8: texture.msaa = eMSAA::x8; break;
        case 16: texture.msaa = eMSAA::x16; break;
        default: JUG_ASSERT(false, "Unsupported MSAA count: {}", sd.Count);
    }

    const bool bMSAA = texture.msaa != eMSAA::None;
    JUG_ASSERT(!bMSAA || _initData.empty(), "An MSAA texture cannot be initialized with data.");

    // bind flags
    UINT bindFlags = 0;
    UINT miscFlags = 0;

    if (!_flags.Has(eTextureOption::ShaderWriteOnly))
    {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (_flags & eTextureOption::ShaderReadWrite)
    {
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    if (_flags & eTextureOption::RenderTarget)
    {
        bindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (_flags & eTextureOption::DepthStencil)
    {
        bindFlags |= D3D11_BIND_DEPTH_STENCIL;
    }
    if (_type == eTexture::TextureCube)
    {
        miscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    }

    constexpr UINT kGenMips = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (texture.numMips > 1 && !bDepth && (bindFlags & kGenMips) == kGenMips)
    {
        miscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }

    constexpr UINT kWritableBinds = D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS;
    texture.bImmutable            = !_initData.empty() && (bindFlags & kWritableBinds) == 0 && (miscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) == 0;

    if (bReadback)
    {
        bindFlags = 0;
        miscFlags = 0;
    }

    const UINT        cpuAccessFlags = bReadback ? D3D11_CPU_ACCESS_READ : 0;
    const D3D11_USAGE usage          = bReadback          ? D3D11_USAGE_STAGING
                                     : texture.bImmutable ? D3D11_USAGE_IMMUTABLE
                                                          : D3D11_USAGE_DEFAULT;

    // init data
    D3D11_SUBRESOURCE_DATA* pInitData = nullptr;
    if (!_initData.empty())
    {
        const uint32_t bpp     = GetBitPerPixel(_format);
        const uint32_t numSubs = texture.numMips * texture.numLayers;
        JUG_ASSERT(_initData.size() == numSubs, "Initial data size does not match the number of mips and layers.");

        pInitData = static_cast<D3D11_SUBRESOURCE_DATA*>(JUG_STACK_ALLOC(sizeof(D3D11_SUBRESOURCE_DATA) * numSubs));
        for (uint32_t layer = 0; layer < texture.numLayers; ++layer)
        {
            for (uint32_t mip = 0; mip < texture.numMips; ++mip)
            {
                const uint32_t index      = mip + layer * texture.numMips;
                const uint32_t w          = Max(1u, _width >> mip);
                const uint32_t h          = Max(1u, _height >> mip);
                const uint32_t d          = Max(1u, _depth >> mip);
                const uint32_t rowPitch   = (w * bpp + 7) / 8;
                const uint32_t slicePitch = rowPitch * h;
                const uint32_t byteWidth  = slicePitch * d;
                const uint32_t size       = static_cast<uint32_t>(_initData[index].GetSize());

                JUG_ASSERT(size >= byteWidth, "Initial data size does not match the expected size for mip {} layer {}.", mip, layer);
                if (size > byteWidth)
                {
                    JUG_CORE_LOG_WARN("Initial data size is larger than the expected size for mip {} layer {}. Extra data will be ignored.", mip, layer);
                }

                pInitData[index].pSysMem          = _initData[index].GetPtr();
                pInitData[index].SysMemPitch      = rowPitch;
                pInitData[index].SysMemSlicePitch = slicePitch;
            }
        }
    }

    // d3d11 texture
    switch (texture.type)
    {
        case eTexture::Texture2D:
        case eTexture::TextureCube:
        {
            D3D11_TEXTURE2D_DESC texd = {};
            texd.Width                = _width;
            texd.Height               = _height;
            texd.MipLevels            = texture.numMips;
            texd.ArraySize            = _numLayers;
            texd.Format               = formatInfo.tex;
            texd.SampleDesc.Count     = 1;
            texd.SampleDesc.Quality   = 0;
            texd.Usage                = usage;
            texd.BindFlags            = bindFlags;
            texd.CPUAccessFlags       = cpuAccessFlags;
            texd.MiscFlags            = miscFlags;

            JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&texd, pInitData, &texture.pTexture2D));

            if (bMSAA)
            {
                D3D11_TEXTURE2D_DESC msaad = {};
                msaad.Width                = _width;
                msaad.Height               = _height;
                msaad.MipLevels            = 1;
                msaad.ArraySize            = _numLayers;
                msaad.Format               = formatInfo.tex;
                msaad.SampleDesc           = sd;
                msaad.Usage                = D3D11_USAGE_DEFAULT;
                msaad.BindFlags            = bindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL);
                msaad.CPUAccessFlags       = 0;
                msaad.MiscFlags            = miscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE;

                JUG_ASSERT(msaad.BindFlags != 0, "An MSAA texture must be a render target or a depth-stencil.");
                JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&msaad, nullptr, &texture.pMsaaRtTexture2D));
            }
        }
        break;

        case eTexture::Texture3D:
        {
            D3D11_TEXTURE3D_DESC texd = {};
            texd.Width                = _width;
            texd.Height               = _height;
            texd.Depth                = _depth;
            texd.MipLevels            = texture.numMips;
            texd.Format               = formatInfo.tex;
            texd.Usage                = usage;
            texd.BindFlags            = bindFlags;
            texd.CPUAccessFlags       = cpuAccessFlags;
            texd.MiscFlags            = miscFlags;

            JUG_DX_CHECK(m_pD3d11Device->CreateTexture3D(&texd, pInitData, &texture.pTexture3D));
        }
        break;

        default:
            JUG_ASSERT(false, "Invalid texture m_type.");
            return texture;
    }

    if (bReadback)   // 스테이징 텍스처는 view를 가질 수 없다.
    {
        return texture;
    }

    // shader resource view
    if ((bindFlags & D3D11_BIND_SHADER_RESOURCE) != 0)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                          = formatInfo.srv;

        switch (texture.type)
        {
            case eTexture::Texture2D:
                if (texture.numLayers > 1)
                {
                    srvd.ViewDimension            = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    srvd.Texture2DArray.MipLevels = texture.numMips;
                    srvd.Texture2DArray.ArraySize = texture.numLayers;
                }
                else
                {
                    srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvd.Texture2D.MipLevels = texture.numMips;
                }
                break;

            case eTexture::TextureCube:
                if (texture.numLayers > 6)
                {
                    srvd.ViewDimension              = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                    srvd.TextureCubeArray.MipLevels = texture.numMips;
                    srvd.TextureCubeArray.NumCubes  = texture.numLayers / 6;
                }
                else
                {
                    srvd.ViewDimension         = D3D11_SRV_DIMENSION_TEXTURECUBE;
                    srvd.TextureCube.MipLevels = texture.numMips;
                }
                break;

            case eTexture::Texture3D:
                srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE3D;
                srvd.Texture3D.MipLevels = texture.numMips;
                break;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(texture.pResource, &srvd, &texture.pSRV));
    }

    // unordered access view
    if ((bindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format                           = formatInfo.srv;

        if (texture.type == eTexture::Texture3D)
        {
            uavd.ViewDimension   = D3D11_UAV_DIMENSION_TEXTURE3D;
            uavd.Texture3D.WSize = texture.depth;
        }
        else if (texture.numLayers > 1)
        {
            uavd.ViewDimension            = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavd.Texture2DArray.ArraySize = texture.numLayers;
        }
        else
        {
            uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(texture.pResource, &uavd, &texture.pUAV));
    }

    return texture;
}

void Graphics::UpdateTexture_(
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
    const TextureD3D11& tex = m_texturePool[_texh];
    JUG_ASSERT(!tex.bImmutable, "An immutable texture cannot be updated.");
    JUG_ASSERT(_mip < tex.numMips, "Invalid mip level {} for texture with {} mips.", _mip, tex.numMips);
    JUG_ASSERT(_layer < tex.numLayers, "Invalid layer {} for texture with {} layers.", _layer, tex.numLayers);

    const VECTOR3U size = CalcTextureSize(tex.width, tex.height, tex.depth, _mip);
    JUG_ASSERT(_offsetX + _width <= size.width, "UpdateTexture width exceeds the mip {} size {}.", _mip, size.width);
    JUG_ASSERT(_offsetY + _height <= size.height, "UpdateTexture height exceeds the mip {} size {}.", _mip, size.height);
    JUG_ASSERT(_offsetZ + _depth <= size.depth, "UpdateTexture depth exceeds the mip {} size {}.", _mip, size.depth);

    const uint32_t index      = CalcTextureIndex(_mip, _layer, tex.numMips);
    const uint32_t bpp        = GetBitPerPixel(tex.format);
    const uint32_t rowPitch   = (_width * bpp + 7) / 8;
    const uint32_t slicePitch = rowPitch * _height;
    const uint32_t byteWidth  = slicePitch * _depth;
    JUG_ASSERT(_data.GetSize() == byteWidth, "UpdateTexture data size does not match the expected size for mip {} layer {}.", _mip, _layer);

    D3D11_BOX box;
    box.left   = _offsetX;
    box.right  = _offsetX + _width;
    box.top    = _offsetY;
    box.bottom = _offsetY + _height;
    box.front  = _offsetZ;
    box.back   = _offsetZ + _depth;
    m_pD3d11DeviceContext->UpdateSubresource(tex.pResource, index, &box, _data.GetPtr(), rowPitch, slicePitch);
}

// ===========================================
//  Texture Update
// ===========================================

void Graphics::UpdateTexture2D(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _width,
    const uint32_t      _height,
    const MemoryView    _data)
{
    UpdateTexture_(_texh, _mip, _layer, _x, _y, 0, _width, _height, 1, _data);
}

void Graphics::UpdateTextureCube(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const eCubeFace     _face,
    const uint32_t      _layer,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _width,
    const uint32_t      _height,
    const MemoryView    _data)
{
    const uint32_t layer = static_cast<uint32_t>(_face) + _layer * 6;
    UpdateTexture_(_texh, _mip, layer, _x, _y, 0, _width, _height, 1, _data);
}

void Graphics::UpdateTexture3D(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _z,
    const uint32_t      _width,
    const uint32_t      _height,
    const uint32_t      _depth,
    const MemoryView    _data)
{
    UpdateTexture_(_texh, _mip, 0, _x, _y, _z, _width, _height, _depth, _data);
}

// ===========================================
//  Texture Copy & Read
// ===========================================

void Graphics::CopyTexture(
    const TextureHandle _dstTexh,
    const TextureHandle _srcTexh)
{
    const TextureD3D11& dst = m_texturePool[_dstTexh];
    const TextureD3D11& src = m_texturePool[_srcTexh];

    JUG_ASSERT(!dst.bImmutable, "An immutable texture cannot be a copy destination.");
    JUG_ASSERT(dst.pResource != src.pResource, "A texture cannot be copied onto itself.");
    JUG_ASSERT(dst.type == src.type && dst.width == src.width && dst.height == src.height && dst.depth == src.depth && dst.numMips == src.numMips && dst.numLayers == src.numLayers && dst.msaa == src.msaa, "A whole texture copy requires both textures to have the same shape.");
    JUG_ASSERT(MakeTextureFormatInfo_(dst.format).tex == MakeTextureFormatInfo_(src.format).tex, "A texture copy requires both textures to share the same typeless format.");

    m_pD3d11DeviceContext->CopyResource(dst.pResource, src.pResource);
}

void Graphics::CopyTexture(
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
    const uint32_t      _widthOrZero,
    const uint32_t      _heightOrZero,
    const uint32_t      _depthOrZero)
{
    const TextureD3D11& dst = m_texturePool[_dstTexh];
    const TextureD3D11& src = m_texturePool[_srcTexh];

    const uint32_t dstIndex = CalcTextureIndex(_dstMip, _dstLayer, dst.numMips);
    const uint32_t srcIndex = CalcTextureIndex(_srcMip, _srcLayer, src.numMips);
    const VECTOR3U dstSize  = CalcTextureSize(dst.width, dst.height, dst.depth, _dstMip);
    const VECTOR3U srcSize  = CalcTextureSize(src.width, src.height, src.depth, _srcMip);

    JUG_ASSERT(!dst.bImmutable, "An immutable texture cannot be a copy destination.");
    JUG_ASSERT(_dstMip < dst.numMips && _dstLayer < dst.numLayers, "Texture copy destination subresource is out of range.");
    JUG_ASSERT(_srcMip < src.numMips && _srcLayer < src.numLayers, "Texture copy source subresource is out of range.");
    JUG_ASSERT(dst.pResource != src.pResource || dstIndex != srcIndex, "A texture cannot be copied onto itself.");
    JUG_ASSERT(MakeTextureFormatInfo_(dst.format).tex == MakeTextureFormatInfo_(src.format).tex, "A texture copy requires both textures to share the same typeless format.");
    JUG_ASSERT(_srcX < dstSize.width && _srcY < srcSize.height && _srcZ < srcSize.depth, "Texture copy source offset is out of bounds.");

    const uint32_t width  = _widthOrZero == 0 ? srcSize.width - _srcX : _widthOrZero;
    const uint32_t height = _heightOrZero == 0 ? srcSize.height - _srcY : _heightOrZero;
    const uint32_t depth  = _depthOrZero == 0 ? srcSize.depth - _srcZ : _depthOrZero;

    JUG_ASSERT(_srcX + width <= srcSize.width && _srcY + height <= srcSize.height && _srcZ + depth <= srcSize.depth, "Texture copy source range is out of bounds.");
    JUG_ASSERT(_dstX + width <= dstSize.width && _dstY + height <= dstSize.height && _dstZ + depth <= dstSize.depth, "Texture copy destination range is out of bounds.");

    const bool bWholeSubresource = _srcX == 0 && _srcY == 0 && _srcZ == 0 && width == srcSize.width && height == srcSize.height && depth == srcSize.depth;
    JUG_ASSERT(!IsDepthFormat(src.format) || bWholeSubresource, "A depth-stencil texture can only be copied as a whole subresource.");

    D3D11_BOX box;
    box.left   = _srcX;
    box.right  = _srcX + width;
    box.top    = _srcY;
    box.bottom = _srcY + height;
    box.front  = _srcZ;
    box.back   = _srcZ + depth;
    m_pD3d11DeviceContext->CopySubresourceRegion(dst.pResource, dstIndex, _dstX, _dstY, _dstZ, src.pResource, srcIndex, bWholeSubresource ? nullptr : &box);
}

size_t Graphics::ReadTexture(
    const TextureHandle     _readbackTexh,
    const uint32_t          _mip,
    const uint32_t          _layer,
    const MutableMemoryView _dst)
{
    JUG_ASSERT(!_dst.IsEmpty(), "ReadTexture requires a non-empty destination.");

    const TextureD3D11& texture = m_texturePool[_readbackTexh];
    JUG_ASSERT(texture.flags & eTextureOption::Readback, "ReadTexture requires a readback texture. Copy into one first.");
    JUG_ASSERT(_mip < texture.numMips && _layer < texture.numLayers, "Texture subresource is out of range.");

    const uint32_t index      = CalcTextureIndex(_mip, _layer, texture.numMips);
    const uint32_t bpp        = GetBitPerPixel(texture.format);
    const VECTOR3U size       = CalcTextureSize(texture.width, texture.height, texture.depth, _mip);
    const uint32_t rowPitch   = (size.width * bpp + 7) / 8;
    const uint32_t slicePitch = rowPitch * size.height;
    const uint32_t byteWidth  = slicePitch * size.depth;
    const uint32_t read       = Min<uint32_t>(static_cast<uint32_t>(_dst.GetSize()), byteWidth);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(texture.pResource, index, D3D11_MAP_READ, 0, &mapped));

    const std::byte* pSrcBase = static_cast<const std::byte*>(mapped.pData);
    std::byte*       pDstBase = _dst.GetPtr();

    size_t written = 0;
    for (uint32_t z = 0; z < size.depth && written < read; ++z)
    {
        for (uint32_t y = 0; y < size.height && written < read; ++y)
        {
            const std::byte* pSrc = pSrcBase + z * mapped.DepthPitch + y * mapped.RowPitch;
            const uint32_t   copy = Min<uint32_t>(rowPitch, read - written);
            std::memcpy(pDstBase + written, pSrc, copy);
            written += copy;
        }
    }

    m_pD3d11DeviceContext->Unmap(texture.pResource, index);
    return written;
}

// ===========================================
//  Frame Buffer
// ===========================================

FrameBufferHandle Graphics::CreateFrameBuffer(
    const Span<const Attachment> _atts,
    const bool                   _bOwnership)
{
    JUG_ASSERT(!_atts.empty(), "A frame buffer requires at least one attachment.");
    JUG_ASSERT(_atts.size() <= kNumMaxAttachmentSlots, "Too many frame buffer attachments.");

    FrameBufferD3D11 fb       = {};
    Attachment       depthAtt = {};
    for (const Attachment& att: _atts)
    {
        JUG_ASSERT(att.texh, "Invalid texture handle in a frame buffer attachment.");

        TextureD3D11& texture = m_texturePool[att.texh];
        JUG_ASSERT(att.mip < texture.numMips, "Frame buffer attachment mip is out of range.");
        JUG_ASSERT(att.numLayers > 0 && att.offset + att.numLayers <= texture.numLayers, "Frame buffer attachment layer range is out of bounds.");

        // 소유권을 넘겨받지 않으면 프레임버퍼가 참조를 하나 더 든다.
        if (!_bOwnership)
        {
            ++texture.refCount;
        }

        // MSAA 텍스처는 멀티샘플 표면에 그리고 나중에 단일 샘플 쪽으로 리졸브한다.
        const bool              bMSAA      = texture.pMsaaRtResource != nullptr;
        ID3D11Resource* const   pResource  = bMSAA ? texture.pMsaaRtResource : texture.pResource;
        const TextureFormatInfo formatInfo = MakeTextureFormatInfo_(texture.format);
        const bool              bArray     = texture.type != eTexture::Texture3D && (texture.numLayers > 1 || att.numLayers > 1);

        fb.bMSAA = fb.bMSAA || bMSAA;

        if (IsDepthFormat(texture.format))
        {
            JUG_ASSERT(!fb.bHasDepth, "A frame buffer can have only one depth-stencil attachment.");
            JUG_ASSERT(texture.flags.Has(eTextureOption::DepthStencil), "The attachment texture was not created with eTextureOption::DepthStencil.");

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
            JUG_DX_CHECK(m_pD3d11Device->CreateDepthStencilView(pResource, &dsvd, &fb.pDSV));

            // 깊이는 컬러 뒤 슬롯에 놓는다. 컬러 개수가 확정된 뒤에 써야 하므로 여기서는 들고만 있는다.
            depthAtt     = att;
            fb.bHasDepth = true;
            continue;
        }

        JUG_ASSERT(fb.numRts < kNumMaxRenderTargetSlots, "Too many render target attachments.");
        JUG_ASSERT(texture.flags.Has(eTextureOption::RenderTarget), "The attachment texture was not created with eTextureOption::RenderTarget.");

        D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
        rtvd.Format                        = formatInfo.rtv;
        if (texture.type == eTexture::Texture3D)
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
        JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(pResource, &rtvd, &fb.rtvs[fb.numRts]));

        fb.atts[fb.numRts] = att;
        ++fb.numRts;
    }

    if (fb.bHasDepth)
    {
        fb.atts[fb.numRts] = depthAtt;
    }

    JUG_ASSERT(fb.numRts > 0 || fb.bHasDepth, "A frame buffer requires at least one render target or a depth-stencil.");
    const FrameBufferHandle fbh = m_frameBufferPool.Emplace(fb);
    JUG_CORE_LOG_TRACE("FrameBuffer created. handle = {}, numRts = {}, depth = {}, msaa = {}, ownership = {}", fbh, fb.numRts, fb.bHasDepth, fb.bMSAA, _bOwnership);
    return fbh;
}

FrameBufferHandle Graphics::CreateFrameBuffer(
    const TextureHandle _texh,
    const bool          _bOwnership)
{
    JUG_ASSERT(_texh, "A frame buffer requires a valid texture handle.");

    const ARRAY<Attachment, 1> atts {
        Attachment { _texh, 0, 0, m_texturePool[_texh].numLayers }
    };
    return CreateFrameBuffer(Span<const Attachment> { atts }, _bOwnership);
}

FrameBufferHandle Graphics::CreateFrameBuffer(
    const SDL_WindowID   _wndID,
    const uint32_t       _width,
    const uint32_t       _height,
    const eTextureFormat _format,
    const uint32_t       _numBuffers)
{
    JUG_ASSERT(_wndID, "A window frame buffer requires a live window.");
    JUG_ASSERT(_width > 0 && _height > 0, "A window frame buffer requires a non-zero size.");
    JUG_ASSERT(!IsDepthFormat(_format), "A swap chain requires a color format.");
    JUG_ASSERT(_numBuffers >= 2, "A flip model swap chain requires at least two back buffers.");

    // create swap chain
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width                 = _width;
    scd.Height                = _height;
    scd.Format                = MakeTextureFormatInfo_(ToNonSRGB(_format)).srv;
    scd.Stereo                = FALSE;
    scd.SampleDesc.Count      = 1;
    scd.SampleDesc.Quality    = 0;
    scd.BufferCount           = _numBuffers;
    scd.Scaling               = DXGI_SCALING_NONE;
    scd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags                 = m_caps.bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    scd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    const HWND  hWnd       = static_cast<HWND>(GetNativeWindowHandle(_wndID));
    ISwapChain* pSwapChain = m_dxgi.CreateSwapChain(m_pD3d11Device, hWnd, true, scd);

    // alt + enter 금지.
    JUG_DX_CHECK(m_dxgi.GetFactory()->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));

    // create texture
    TextureD3D11 texture = {};
    texture.width        = _width;
    texture.height       = _height;
    texture.depth        = 1;
    texture.type         = eTexture::Texture2D;
    texture.format       = _format;
    texture.numLayers    = 1;
    texture.numMips      = 1;
    texture.msaa         = eMSAA::None;
    texture.refCount     = 1;
    texture.bImmutable   = false;
    texture.flags        = { eTextureOption::RenderTarget, eTextureOption::ShaderWriteOnly };
    JUG_DX_CHECK(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&texture.pTexture2D)));
    const TextureHandle texh = m_texturePool.Emplace(texture);

    // create frame buffer
    FrameBufferD3D11 fb = {};
    fb.atts[0]          = Attachment { texh, 0, 0, 1 };
    fb.numRts           = 1;
    fb.bHasDepth        = false;
    fb.bMSAA            = false;
    fb.wndID            = _wndID;
    fb.numBuffers       = _numBuffers;
    fb.pSwapChain       = pSwapChain;
    fb.bVSync           = true;

    // create rtv
    D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format                        = MakeTextureFormatInfo_(_format).rtv;
    rtvd.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvd.Texture2D.MipSlice            = 0;
    JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(texture.pResource, &rtvd, fb.rtvs.data()));

    const FrameBufferHandle fbh = m_frameBufferPool.Emplace(fb);
    m_swapChainFbhs.push_back(fbh);

    JUG_CORE_LOG_INFO("Swap chain frame buffer created. handle = {}, {}x{}, format = {}, buffers = {}, tearing = {}", fbh, _width, _height, static_cast<uint32_t>(_format), _numBuffers, m_caps.bAllowTearing);
    return fbh;
}

FrameBufferHandle Graphics::FindFrameBufferOrNull(
    const SDL_WindowID _wndID)
{
    JUG_ASSERT(_wndID != 0, "FindFrameBufferOrNull requires a valid window ID.");

    for (const FrameBufferHandle fbh: m_swapChainFbhs)
    {
        const FrameBufferD3D11& fb = m_frameBufferPool[fbh];
        if (fb.wndID == _wndID)
        {
            return fbh;
        }
    }
    return kNullHandle;
}

void Graphics::ResizeFrameBuffer(
    const FrameBufferHandle _swapChainFbh,
    const uint32_t          _width,
    const uint32_t          _height)
{
    FrameBufferD3D11& fb = m_frameBufferPool[_swapChainFbh];
    JUG_ASSERT(fb.pSwapChain, "ResizeFrameBuffer requires a window frame buffer.");

    // 윈도우 최소화는 무시
    if (_width == 0 || _height == 0)
    {
        return;
    }

    // 리사이즈 필요없음.
    TextureD3D11& texture = m_texturePool[fb.atts[0].texh];
    if (texture.width == _width && texture.height == _height)
    {
        return;
    }

    // 컨텍스트가 프레임버퍼를 바인딩하고 있으면 리사이즈가 안됨
    m_pD3d11DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    m_fbh     = kNullHandle;
    m_lastFbh = kNullHandle;

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
    JUG_DX_CHECK(fb.pSwapChain->ResizeBuffers(0, _width, _height, DXGI_FORMAT_UNKNOWN, dxgiFlags));

    texture.width  = _width;
    texture.height = _height;
    JUG_DX_CHECK(fb.pSwapChain->GetBuffer(0, IID_PPV_ARGS(&texture.pTexture2D)));

    D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format                        = MakeTextureFormatInfo_(texture.format).rtv;
    rtvd.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvd.Texture2D.MipSlice            = 0;
    JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(texture.pResource, &rtvd, fb.rtvs.data()));
    m_dirtyFlags |= ePipelineDirty::FrameBuffer;

    JUG_CORE_LOG_INFO("Swap chain frame buffer resized. handle = {}, {}x{}", _swapChainFbh, _width, _height);
}

void Graphics::SetVSync(
    const FrameBufferHandle _swapChainFbh,
    const bool              _bVSync)
{
    FrameBufferD3D11& fb = m_frameBufferPool[_swapChainFbh];
    JUG_ASSERT(fb.pSwapChain, "SetVSync requires a window frame buffer.");
    fb.bVSync = _bVSync;

    JUG_CORE_LOG_TRACE("Swap chain vsync changed. handle = {}, vsync = {}", _swapChainFbh, _bVSync);
}

// ===========================================
//  Frame
// ===========================================

void Graphics::Frame()
{
    // present
    for (const FrameBufferHandle fbh: m_swapChainFbhs)
    {
        const FrameBufferD3D11& fb       = m_frameBufferPool[fbh];
        const UINT              interval = fb.bVSync ? 1 : 0;
        const UINT              flags    = m_caps.bAllowTearing && !fb.bVSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
        JUG_DX_CHECK(fb.pSwapChain->Present(interval, flags));
    }

    // timer query.
    {
        // 가장 최근 쿼리 종료
        TimerQuery& last = m_timerQueries.Back();
        if (last.bIssued)
        {
            m_pD3d11DeviceContext->End(last.pEnd);
            m_pD3d11DeviceContext->End(last.pDisjoint);
        }

        // 가장 오래된 쿼리 결과 가져오기.
        TimerQuery pop = m_timerQueries.Front();
        m_timerQueries.PopFront();

        if (pop.bIssued)
        {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
            if (m_pD3d11DeviceContext->GetData(pop.pDisjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !disjoint.Disjoint)
            {
                UINT64 begin;
                UINT64 end;
                if (m_pD3d11DeviceContext->GetData(pop.pBegin, &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
                    && m_pD3d11DeviceContext->GetData(pop.pEnd, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
                {
                    m_stats.gpuTimerBegin   = begin;
                    m_stats.gpuTimerEnd     = end;
                    m_stats.gpuTimerFreq    = disjoint.Frequency;
                    m_stats.gpuTimerLatency = m_frameIndex - pop.frameIndex;
                }
            }
        }

        // 다음 쿼리 시작
        m_pD3d11DeviceContext->Begin(pop.pDisjoint);
        m_pD3d11DeviceContext->End(pop.pBegin);
        pop.frameIndex = m_frameIndex;
        pop.bIssued    = true;
        m_timerQueries.PushBack(pop);
    }

    // GPU 메모리 사용량 갱신
    if (m_caps.videoMemorySize > 0 && m_dxgi.GetAdapterOrNull())
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO vmi;
        if (SUCCEEDED(m_dxgi.GetAdapterOrNull()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi)))
        {
            m_stats.gpuMemoryUsed = vmi.CurrentUsage;
            m_stats.gpuMemorySize = vmi.Budget;
        }
    }

    ++m_frameIndex;

    m_lastStats = m_stats;
    m_stats     = {};
}

// ===========================================
//  Clear
// ===========================================

void Graphics::ClearRenderTarget(
    const FrameBufferHandle _fbh,
    const RGBA              _color,
    const uint32_t          _slot)
{
    const FrameBufferD3D11& fb = m_frameBufferPool[_fbh];
    JUG_ASSERT(_slot < fb.numRts, "Render target slot is out of range.");
    const VECTOR4 color = _color.ToLinear();
    m_pD3d11DeviceContext->ClearRenderTargetView(fb.rtvs[_slot], color.e.data());
}

void Graphics::ClearRenderTargets(
    const FrameBufferHandle _fbh,
    const RGBA              _color)
{
    const FrameBufferD3D11& fb    = m_frameBufferPool[_fbh];
    const VECTOR4           color = _color.ToLinear();
    for (uint32_t i = 0; i < fb.numRts; ++i)
    {
        m_pD3d11DeviceContext->ClearRenderTargetView(fb.rtvs[i], color.e.data());
    }
}

void Graphics::ClearDepthStencil(
    const FrameBufferHandle _fbh,
    const bool              _bClearDepth,
    const bool              _bClearStencil,
    const float             _depth,
    const uint8_t           _stencil)
{
    if (!_bClearDepth && !_bClearStencil)
    {
        return;
    }

    const FrameBufferD3D11& fb = m_frameBufferPool[_fbh];
    JUG_ASSERT(fb.pDSV, "The frame buffer has no depth stencil attachment.");

    UINT flags = 0;
    if (_bClearDepth)
    {
        flags |= D3D11_CLEAR_DEPTH;
    }
    if (_bClearStencil)
    {
        flags |= D3D11_CLEAR_STENCIL;
    }
    m_pD3d11DeviceContext->ClearDepthStencilView(fb.pDSV, flags, _depth, _stencil);
}

// ===========================================
//  Input Layout
// ===========================================

ID3D11InputLayout* Graphics::GetOrCreateD3d11InputLayout_()
{
    if (m_numStreams == 0)
    {
        return nullptr;
    }

    Murmur3 hasher {};
    for (uint32_t slot = 0; slot < m_numStreams; ++slot)
    {
        hasher.Mix(m_vlhs[slot]);
    }
    hasher.Mix(m_instanceDataStride);
    const uint64_t key = hasher.Finalize();

    // 캐시 히트
    const auto it = m_d3d11InputLayoutCache.find(key);
    if (it != m_d3d11InputLayoutCache.end())
    {
        return it->second;
    }

    JUG_ASSERT(m_instanceDataStride % kInstanceDataSizeAlign == 0, "Instance stride must be a multiple of the instance data alignment.");
    const uint32_t numInstData = m_instanceDataStride / kInstanceDataSizeAlign;
    uint32_t       numElems    = numInstData;
    for (uint32_t slot = 0; slot < m_numStreams; ++slot)
    {
        if (m_vlhs[slot])
        {
            numElems += static_cast<uint32_t>(m_vertexLayoutPool[m_vlhs[slot]].vl.GetNumAttributes());
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
    for (uint32_t slot = 0; slot < m_numStreams; ++slot)
    {
        const VertexLayoutHandle vlh = m_vlhs[slot];
        if (!vlh)
        {
            continue;
        }

        for (const VertexAttribute& attrib: m_vertexLayoutPool[vlh].vl.GetAttributes())
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
        ied.InputSlot                 = m_numStreams;
        ied.AlignedByteOffset         = i * kInstanceDataSizeAlign;
        ied.InputSlotClass            = D3D11_INPUT_PER_INSTANCE_DATA;
        ied.InstanceDataStepRate      = 1;
        std::format_to(std::back_inserter(hlsl), "float4 Inst{} : INSTANCE{};", i, i);
        ++index;
    }

    JUG_ASSERT(index == numElems, "Input element count does not match the counted size.");
    hlsl += "}; void VSMain(in VSInput _input) {}";

    ShaderCompileDesc compileDesc = {};
    compileDesc.type              = eShader::Vertex;
    compileDesc.entryPoint        = "VSMain";
    const Result<Shader> shader   = Shader::Compile(hlsl, compileDesc);
    JUG_ASSERT(shader, "Failed to compile the dummy vertex shader for an input layout.");
    const MemoryView bytecode = shader->GetByteCode();

    ID3D11InputLayout* pInputLayout;
    JUG_DX_CHECK(m_pD3d11Device->CreateInputLayout(pIed, numElems, bytecode.GetPtr(), bytecode.GetSize(), &pInputLayout));
    m_d3d11InputLayoutCache.emplace(key, pInputLayout);
    JUG_CORE_LOG_TRACE("InputLayout created. numElements = {}, numStreams = {}, instanceStride = {}, cacheSize = {}", numElems, m_numStreams, m_instanceDataStride, m_d3d11InputLayoutCache.size());
    return pInputLayout;
}

// ===========================================
//  Shader & Program
// ===========================================

ShaderHandle Graphics::CreateShader(
    const MemoryView _bytecode)
{
    const uint64_t hash = Hash<Murmur3>(_bytecode);
    const auto     it   = m_shaderCache.find(hash);
    if (it != m_shaderCache.end())
    {
        const ShaderHandle cached = it->second;
        ++m_shaderPool[cached].refCount;
        return cached;
    }

    JUG_ASSERT(_bytecode.GetSize() > kShaderBytecodeHeaderSize, "Shader bytecode is too small to be a DXBC container.");

    const uint32_t* pHeader = reinterpret_cast<const uint32_t*>(_bytecode.GetPtr());
    JUG_ASSERT(pHeader[0] == kDxbcMagic, "Shader bytecode is not a DXBC container.");

    const uint32_t  numChunks     = pHeader[7];
    const uint32_t* pChunkOffsets = pHeader + 8;

    // find shader stage
    eShader  stage = eShader::Vertex;
    uint32_t chunk = 0;
    for (; chunk < numChunks; ++chunk)
    {
        const uint32_t* pChunk = reinterpret_cast<const uint32_t*>(_bytecode.GetPtr() + pChunkOffsets[chunk]);
        if (pChunk[0] != kShdrMagic && pChunk[0] != kShexMagic)
        {
            continue;
        }

        switch (pChunk[2] >> 16)
        {
            case 0: stage = eShader::Pixel; break;
            case 1: stage = eShader::Vertex; break;
            case 5: stage = eShader::Compute; break;
            default: JUG_ASSERT(false, "Unsupported shader stage in the bytecode."); break;
        }
        break;
    }
    JUG_ASSERT(chunk < numChunks, "The DXBC container has no shader code chunk.");

    // make shader
    ShaderD3D11 shader = {};
    shader.type        = stage;
    shader.refCount    = 1;
    shader.hash        = hash;
    switch (stage)
    {
        case eShader::Vertex:
            JUG_DX_CHECK(m_pD3d11Device->CreateVertexShader(_bytecode.GetPtr(), _bytecode.GetSize(), nullptr, &shader.pVS));
            break;

        case eShader::Pixel:
            JUG_DX_CHECK(m_pD3d11Device->CreatePixelShader(_bytecode.GetPtr(), _bytecode.GetSize(), nullptr, &shader.pPS));
            break;

        case eShader::Compute:
            JUG_DX_CHECK(m_pD3d11Device->CreateComputeShader(_bytecode.GetPtr(), _bytecode.GetSize(), nullptr, &shader.pCS));
            break;

        default:
            JUG_ASSERT(false, "Unknown shader stage.");
            break;
    }

    const ShaderHandle sh = m_shaderPool.Emplace(shader);
    m_shaderCache[hash]   = sh;
    JUG_CORE_LOG_TRACE("Shader created. handle = {}, stage = {}, bytes = {}, hash = {:#x}", sh, static_cast<uint32_t>(stage), _bytecode.GetSize(), hash);
    return sh;
}

ProgramHandle Graphics::CreateProgram(
    const ShaderHandle _vsh,
    const ShaderHandle _psh,
    const bool         _bOwnership)
{
    JUG_ASSERT(_vsh && _psh, "A graphics program requires both a vertex shader and a pixel shader.");
    JUG_ASSERT(m_shaderPool[_vsh].type == eShader::Vertex, "The first shader must be a vertex shader.");
    JUG_ASSERT(m_shaderPool[_psh].type == eShader::Pixel, "The second shader must be a pixel shader.");

    if (!_bOwnership)
    {
        ++m_shaderPool[_vsh].refCount;
        ++m_shaderPool[_psh].refCount;
    }

    ProgramDesc prog = {};
    prog.vsh         = _vsh;
    prog.psh         = _psh;
    prog.csh         = kNullHandle;

    const ProgramHandle ph = m_programPool.Emplace(prog);
    JUG_CORE_LOG_TRACE("Program created. handle = {}, vsh = {}, psh = {}, ownership = {}", ph, _vsh, _psh, _bOwnership);
    return ph;
}

ProgramHandle Graphics::CreateComputeProgram(
    const ShaderHandle _csh,
    const bool         _bOwnership)
{
    JUG_ASSERT(_csh, "A compute program requires a valid compute shader.");
    JUG_ASSERT(m_shaderPool[_csh].type == eShader::Compute, "The shader must be a compute shader.");

    if (!_bOwnership)
    {
        ++m_shaderPool[_csh].refCount;
    }

    ProgramDesc prog = {};
    prog.vsh         = kNullHandle;
    prog.psh         = kNullHandle;
    prog.csh         = _csh;

    const ProgramHandle ph = m_programPool.Emplace(prog);

    JUG_CORE_LOG_TRACE("ComputeProgram created. handle = {}, csh = {}, ownership = {}", ph, _csh, _bOwnership);
    return ph;
}

// ===========================================
//  Destroy
// ===========================================

void Graphics::Destroy(
    const VertexBufferHandle _vbh)
{
    VertexBufferD3D11& vb = m_vertexBufferPool[_vbh];

    for (uint32_t i = 0; i < kNumMaxStreams; ++i)
    {
        if (m_vbhs[i] == _vbh)
        {
            m_vbhs[i]               = kNullHandle;
            m_vlhs[i]               = kNullHandle;
            m_d3d11VertexBuffers[i] = nullptr;
            m_vertexStrides[i]      = 0;
            m_vertexOffsets[i]      = 0;
            m_dirtyFlags |= { ePipelineDirty::VertexBuffer, ePipelineDirty::InputLayout };
        }
    }

    // release vertex layout
    if (vb.vlh)
    {
        ReleaseVertexLayout_(vb.vlh);
    }

    // release
    JUG_DX_RELEASE(vb.pBuffer);
    m_vertexBufferPool.Erase(_vbh);
}

void Graphics::Destroy(
    const InstanceBufferHandle _instbh)
{
    if (m_instbh == _instbh)
    {
        m_instbh               = kNullHandle;
        m_pD3d11InstanceBuffer = nullptr;
        m_instanceDataStride   = 0;
        m_instanceOffset       = 0;
        m_numInstances         = 0;
        m_dirtyFlags |= { ePipelineDirty::InstanceBuffer, ePipelineDirty::InputLayout };
    }

    JUG_DX_RELEASE(m_instanceBufferPool[_instbh].pBuffer);
    m_instanceBufferPool.Erase(_instbh);
}

void Graphics::Destroy(
    const IndexBufferHandle _ibh)
{
    if (m_ibh == _ibh)
    {
        m_ibh               = kNullHandle;
        m_pD3d11IndexBuffer = nullptr;
        m_dxgiIndexFormat   = DXGI_FORMAT_UNKNOWN;
        m_indexOffset       = 0;
        m_numIndices        = 0;
        m_dirtyFlags |= ePipelineDirty::IndexBuffer;
    }

    JUG_DX_RELEASE(m_indexBufferPool[_ibh].pBuffer);
    m_indexBufferPool.Erase(_ibh);
}

void Graphics::Destroy(
    const ConstantBufferHandle _cbh)
{
    // unbind
    for (CBufferBind& bind: m_cbufferBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxCBufferSlots; ++slot)
        {
            if (bind.resources[slot] == _cbh)
            {
                bind.resources[slot]      = kNullHandle;
                bind.d3d11Resources[slot] = nullptr;
                bind.MarkDirty(slot);
                m_dirtyFlags |= ePipelineDirty::ConstantBuffer;
            }
        }
    }

    // release
    JUG_DX_RELEASE(m_constantBufferPool[_cbh].pBuffer);
    m_constantBufferPool.Erase(_cbh);
}

void Graphics::Destroy(
    const StorageBufferHandle _sbh)
{
    StorageBufferD3D11& sb = m_storageBufferPool[_sbh];

    const ResourceRef resource = _sbh;
    for (ReadBind& bind: m_readBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxReadSlots; ++slot)
        {
            if (bind.resources[slot] == resource)
            {
                bind.resources[slot]      = {};
                bind.d3d11Resources[slot] = nullptr;
                bind.MarkDirty(slot);
                m_dirtyFlags |= ePipelineDirty::ShaderResourceView;
            }
        }
    }

    for (ReadWriteBind& bind: m_readWriteBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxReadWriteSlots; ++slot)
        {
            if (bind.resources[slot] == resource)
            {
                bind.resources[slot]      = {};
                bind.d3d11Resources[slot] = nullptr;
                bind.MarkDirty(slot);
                m_dirtyFlags |= ePipelineDirty::UnorderedAccessView;
            }
        }
    }

    JUG_DX_RELEASE(sb.pSRV);
    JUG_DX_RELEASE(sb.pUAV);
    JUG_DX_RELEASE(sb.pBuffer);
    m_storageBufferPool.Erase(_sbh);
}

void Graphics::Destroy(
    const TextureHandle _texh)
{
    TextureD3D11& texture = m_texturePool[_texh];

    JUG_ASSERT(texture.refCount > 0, "Texture reference count underflow.");
    if (--texture.refCount > 0)
    {
        return;
    }

    // refcount == 0.
    const ResourceRef resource = _texh;
    for (ReadBind& bind: m_readBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxReadSlots; ++slot)
        {
            if (bind.resources[slot] == resource)
            {
                bind.resources[slot]      = {};
                bind.d3d11Resources[slot] = nullptr;
                bind.MarkDirty(slot);
                m_dirtyFlags |= ePipelineDirty::ShaderResourceView;
            }
        }
    }
    for (ReadWriteBind& bind: m_readWriteBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxReadWriteSlots; ++slot)
        {
            if (bind.resources[slot] == resource)
            {
                bind.resources[slot]      = {};
                bind.d3d11Resources[slot] = nullptr;
                bind.MarkDirty(slot);
                m_dirtyFlags |= ePipelineDirty::UnorderedAccessView;
            }
        }
    }

    JUG_DX_RELEASE(texture.pSRV);
    JUG_DX_RELEASE(texture.pUAV);
    JUG_DX_RELEASE(texture.pMsaaRtResource);
    JUG_DX_RELEASE(texture.pResource);
    m_texturePool.Erase(_texh);
}

void Graphics::Destroy(
    const FrameBufferHandle _fbh)
{
    FrameBufferD3D11& fb = m_frameBufferPool[_fbh];

    if (fb.pSwapChain)
    {
        // fullscreen 해제. 아니면 Destroy() 시 DXGI_ERROR_DEVICE_REMOVED 날 수 있음
        JUG_DISCARD_RETURN(fb.pSwapChain->SetFullscreenState(FALSE, nullptr));
        std::erase(m_swapChainFbhs, _fbh);
    }

    // frame buffer 해제
    if (m_fbh == _fbh)
    {
        m_fbh = kNullHandle;
        m_dirtyFlags |= ePipelineDirty::FrameBuffer;
    }

    if (m_lastFbh == _fbh)
    {
        m_lastFbh = kNullHandle;
    }

    for (ID3D11RenderTargetView*& pRTV: fb.rtvs)
    {
        JUG_DX_RELEASE(pRTV);
    }
    JUG_DX_RELEASE(fb.pDSV);

    const uint32_t numAtts = fb.numRts + (fb.bHasDepth ? 1 : 0);
    for (uint32_t i = 0; i < numAtts; ++i)
    {
        Destroy(fb.atts[i].texh);
    }

    // swap chain은 가장 마지막에 해제.
    JUG_DX_RELEASE(fb.pSwapChain);
    m_frameBufferPool.Erase(_fbh);
}

void Graphics::Destroy(
    const ShaderHandle _sh)
{
    ShaderD3D11& shader = m_shaderPool[_sh];

    JUG_ASSERT(shader.refCount > 0, "Shader reference count underflow.");
    if (--shader.refCount > 0)
    {
        return;
    }

    m_shaderCache.erase(shader.hash);
    JUG_DX_RELEASE(shader.pVS);

    m_shaderPool.Erase(_sh);
}

void Graphics::Destroy(
    const ProgramHandle _ph)
{
    const ProgramDesc& prog = m_programPool[_ph];

    if (m_ph == _ph)
    {
        m_ph = kNullHandle;
        m_dirtyFlags |= { ePipelineDirty::Program, ePipelineDirty::InputLayout };
    }

    if (m_computePh == _ph)
    {
        m_computePh = kNullHandle;
        m_dirtyFlags |= ePipelineDirty::ComputeProgram;
    }

    if (prog.vsh)
    {
        Destroy(prog.vsh);
    }
    if (prog.psh)
    {
        Destroy(prog.psh);
    }
    if (prog.csh)
    {
        Destroy(prog.csh);
    }
    m_programPool.Erase(_ph);
}

// ===========================================
//  Debug
// ===========================================

void Graphics::SetName(
    const VertexBufferHandle _vbh,
    const StringView         _name)
{
    SetD3d11ObjectName_(m_vertexBufferPool[_vbh].pBuffer, _name);
}

void Graphics::SetName(
    const InstanceBufferHandle _instbh,
    const StringView           _name)
{
    SetD3d11ObjectName_(m_instanceBufferPool[_instbh].pBuffer, _name);
}

void Graphics::SetName(
    const IndexBufferHandle _ibh,
    const StringView        _name)
{
    SetD3d11ObjectName_(m_indexBufferPool[_ibh].pBuffer, _name);
}

void Graphics::SetName(
    const ConstantBufferHandle _cbh,
    const StringView           _name)
{
    SetD3d11ObjectName_(m_constantBufferPool[_cbh].pBuffer, _name);
}

void Graphics::SetName(
    const StorageBufferHandle _sbh,
    const StringView          _name)
{
    const StorageBufferD3D11& storageBuffer = m_storageBufferPool[_sbh];
    SetD3d11ObjectName_(storageBuffer.pBuffer, _name);
    if (storageBuffer.pSRV)
    {
        SetD3d11ObjectName_(storageBuffer.pSRV, _name);
    }
    if (storageBuffer.pUAV)
    {
        SetD3d11ObjectName_(storageBuffer.pUAV, _name);
    }
}

void Graphics::SetName(
    const TextureHandle _texh,
    const StringView    _name)
{
    const TextureD3D11& texture = m_texturePool[_texh];
    SetD3d11ObjectName_(texture.pResource, _name);
    if (texture.pMsaaRtResource)
    {
        SetD3d11ObjectName_(texture.pMsaaRtResource, _name);
    }
    if (texture.pSRV)
    {
        SetD3d11ObjectName_(texture.pSRV, _name);
    }
    if (texture.pUAV)
    {
        SetD3d11ObjectName_(texture.pUAV, _name);
    }
}

void Graphics::SetName(
    const FrameBufferHandle _fbh,
    const StringView        _name)
{
    const FrameBufferD3D11& frameBuffer = m_frameBufferPool[_fbh];

    for (uint32_t i = 0; i < frameBuffer.numRts; ++i)
    {
        SetD3d11ObjectName_(frameBuffer.rtvs[i], _name);
    }
    if (frameBuffer.pDSV)
    {
        SetD3d11ObjectName_(frameBuffer.pDSV, _name);
    }

#ifdef JUG_DEBUG
    if (frameBuffer.pSwapChain)
    {
        const UINT len = static_cast<UINT>(Min(_name.size(), kMaxDebugNameLength));
        JUG_DISCARD_RETURN(frameBuffer.pSwapChain->SetPrivateData(WKPDID_D3DDebugObjectName, len, _name.data()));
    }
#endif
}

void Graphics::SetName(
    const ShaderHandle _sh,
    const StringView   _name)
{
    const ShaderD3D11& shader = m_shaderPool[_sh];
    switch (shader.type)
    {
        case eShader::Vertex: SetD3d11ObjectName_(shader.pVS, _name); break;
        case eShader::Pixel: SetD3d11ObjectName_(shader.pPS, _name); break;
        case eShader::Compute: SetD3d11ObjectName_(shader.pCS, _name); break;
        default: JUG_ASSERT(false, "Unknown shader stage."); break;
    }
}

void Graphics::PushDebugGroup(
    const StringView _name) const
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        JUG_DISCARD_RETURN(m_pUserAnnotationOrNull->BeginEvent(ToUtf16(_name).c_str()));
    }
#endif
}

void Graphics::PopDebugGroup() const
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        JUG_DISCARD_RETURN(m_pUserAnnotationOrNull->EndEvent());
    }
#endif
}

void Graphics::SetDebugMarker(
    const StringView _name) const
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        m_pUserAnnotationOrNull->SetMarker(ToUtf16(_name).c_str());
    }
#endif
}

// ===========================================
//  Getter
// ===========================================

const VertexBufferDesc& Graphics::GetDesc(
    const VertexBufferHandle _vbh) const
{
    return m_vertexBufferPool[_vbh];
}

const InstanceBufferDesc& Graphics::GetDesc(
    const InstanceBufferHandle _instbh) const
{
    return m_instanceBufferPool[_instbh];
}

const IndexBufferDesc& Graphics::GetDesc(
    const IndexBufferHandle _ibh) const
{
    return m_indexBufferPool[_ibh];
}

const ConstantBufferDesc& Graphics::GetDesc(
    const ConstantBufferHandle _cbh) const
{
    return m_constantBufferPool[_cbh];
}

const StorageBufferDesc& Graphics::GetDesc(
    const StorageBufferHandle _sbh) const
{
    return m_storageBufferPool[_sbh];
}

const TextureDesc& Graphics::GetDesc(
    const TextureHandle _texh) const
{
    return m_texturePool[_texh];
}

const ShaderDesc& Graphics::GetDesc(
    const ShaderHandle _sh) const
{
    return m_shaderPool[_sh];
}

const ProgramDesc& Graphics::GetDesc(
    const ProgramHandle _ph) const
{
    return m_programPool[_ph];
}

const FrameBufferDesc& Graphics::GetDesc(
    const FrameBufferHandle _fbh) const
{
    return m_frameBufferPool[_fbh];
}

void Graphics::SetVertexBuffer(
    const VertexBufferHandle _vbh,
    const uint32_t           _offset,
    const uint32_t           _numVerticesOrZero)
{
    const ARRAY<VertexStream, 1> streams = {
        VertexStream { _vbh, _offset }
    };
    SetVertexBuffers(streams, _numVerticesOrZero, kNullHandle, 0, 0);
}

void Graphics::SetVertexBuffers(
    const Span<const VertexStream> _streams,
    const uint32_t                 _numVerticesOrZero)
{
    SetVertexBuffers(_streams, _numVerticesOrZero, kNullHandle, 0, 0);
}

// ===========================================
//  State Cache
// ===========================================

ID3D11SamplerState* Graphics::GetOrCreateSamplerState_(
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

ID3D11RasterizerState* Graphics::GetOrCreateRasterizerState_()
{
    const Flags<eRenderState> flags = m_renderStateFlags & kRasterizerStateMask;
    const uint64_t            key   = flags.GetFlags();
    const auto                it    = m_d3d11RasterizerStateCache.find(key);
    if (it != m_d3d11RasterizerStateCache.end())
    {
        return it->second;
    }

    const D3D11_RASTERIZER_DESC rd = MakeRasterizerDesc_(m_renderStateFlags);
    ID3D11RasterizerState*      pState;
    JUG_DX_CHECK(m_pD3d11Device->CreateRasterizerState(&rd, &pState));
    m_d3d11RasterizerStateCache.emplace(key, pState);
    JUG_CORE_LOG_TRACE("RasterizerState created. flags = {:#x}, cacheSize = {}", flags.GetFlags(), m_d3d11RasterizerStateCache.size());
    return pState;
}

ID3D11BlendState* Graphics::GetOrCreateBlendState_()
{
    const Flags<eRenderState> flags = m_renderStateFlags & kBlendStateMask;

    Murmur3 hasher {};
    hasher.Mix(flags);
    if (m_renderStateFlags & eRenderState::IndependentBlend)
    {
        for (uint32_t i = 0; i < kNumMaxRenderTargetSlots; ++i)
        {
            hasher.Mix(m_blendFlags[i]);
        }
    }
    else
    {
        hasher.Mix(m_blendFlags[0]);
    }
    const uint64_t key = hasher.Finalize();

    // find
    const auto it = m_d3d11BlendStateCache.find(key);
    if (it != m_d3d11BlendStateCache.end())
    {
        return it->second;
    }

    const D3D11_BLEND_DESC bd     = MakeBlendDesc_(m_renderStateFlags, m_blendFlags);
    ID3D11BlendState*      pState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateBlendState(&bd, &pState));
    m_d3d11BlendStateCache.emplace(key, pState);

    JUG_CORE_LOG_TRACE("BlendState created. state = {:#x}, blend0 = {:#x}, cacheSize = {}", flags.GetFlags(), m_blendFlags[0].GetFlags(), m_d3d11BlendStateCache.size());
    return pState;
}

ID3D11DepthStencilState* Graphics::GetOrCreateDepthStencilState_()
{
    const Flags<eRenderState> flags = m_renderStateFlags & kDepthStencilStateMask;
    const uint64_t            key   = static_cast<uint64_t>(flags.GetFlags() | m_fstencilFlags.GetFlags()) | (static_cast<uint64_t>(m_bstencilFlags.GetFlags()) << 32);

    // find
    const auto it = m_d3d11DepthStencilStateCache.find(key);
    if (it != m_d3d11DepthStencilStateCache.end())
    {
        return it->second;
    }

    const D3D11_DEPTH_STENCIL_DESC dsd                = MakeDepthStencilDesc_(m_renderStateFlags, m_fstencilFlags, m_bstencilFlags);
    ID3D11DepthStencilState*       pDepthStencilState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateDepthStencilState(&dsd, &pDepthStencilState));
    m_d3d11DepthStencilStateCache.emplace(key, pDepthStencilState);

    JUG_CORE_LOG_TRACE("DepthStencilState created. depth = {:#x}, fstencil = {:#x}, bstencil = {:#x}, cacheSize = {}", flags.GetFlags(), m_fstencilFlags.GetFlags(), m_bstencilFlags.GetFlags(), m_d3d11DepthStencilStateCache.size());
    return pDepthStencilState;
}

// ===========================================
//  Apply
// ===========================================

void Graphics::ApplyPipeline_()
{
    if (m_dirtyFlags == kZeroFlag)
    {
        return;
    }

    if (m_dirtyFlags & ePipelineDirty::VertexBuffer)
    {
        m_pD3d11DeviceContext->IASetVertexBuffers(0, m_numStreams, m_d3d11VertexBuffers.data(), m_vertexStrides.data(), m_vertexOffsets.data());
    }

    if (m_dirtyFlags & ePipelineDirty::InstanceBuffer)
    {
        m_pD3d11DeviceContext->IASetVertexBuffers(m_numStreams, 1, &m_pD3d11InstanceBuffer, &m_instanceDataStride, &m_instanceOffset);
    }

    if (m_dirtyFlags & ePipelineDirty::IndexBuffer)
    {
        m_pD3d11DeviceContext->IASetIndexBuffer(m_pD3d11IndexBuffer, m_dxgiIndexFormat, 0);
    }

    if (m_dirtyFlags & ePipelineDirty::PrimitiveTopology)
    {
        m_pD3d11DeviceContext->IASetPrimitiveTopology(MakeD3d11Topology_(m_renderStateFlags));
    }

    if (m_dirtyFlags & ePipelineDirty::InputLayout)
    {
        m_pD3d11DeviceContext->IASetInputLayout(GetOrCreateD3d11InputLayout_());
    }

    if (m_dirtyFlags & ePipelineDirty::Program)
    {
        ID3D11VertexShader* pVS = nullptr;
        ID3D11PixelShader*  pPS = nullptr;

        if (m_ph)
        {
            const ProgramDesc& program = m_programPool[m_ph];
            pVS                        = program.vsh ? m_shaderPool[program.vsh].pVS : nullptr;
            pPS                        = program.psh ? m_shaderPool[program.psh].pPS : nullptr;
        }

        m_pD3d11DeviceContext->VSSetShader(pVS, nullptr, 0);
        m_pD3d11DeviceContext->PSSetShader(pPS, nullptr, 0);
    }

    if (m_dirtyFlags & ePipelineDirty::ComputeProgram)
    {
        ID3D11ComputeShader* pCS = nullptr;
        if (m_computePh)
        {
            const ProgramDesc& program = m_programPool[m_computePh];
            pCS                        = program.csh ? m_shaderPool[program.csh].pCS : nullptr;
        }

        m_pD3d11DeviceContext->CSSetShader(pCS, nullptr, 0);
    }

    if (m_dirtyFlags & ePipelineDirty::ConstantBuffer)
    {
        if (CBufferBind& bind = m_cbufferBind[eShader::Vertex]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->VSSetConstantBuffers(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (CBufferBind& bind = m_cbufferBind[eShader::Pixel]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->PSSetConstantBuffers(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (CBufferBind& bind = m_cbufferBind[eShader::Compute]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->CSSetConstantBuffers(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }
    }

    if (m_dirtyFlags & ePipelineDirty::ShaderResourceView)
    {
        if (ReadBind& bind = m_readBind[eShader::Vertex]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->VSSetShaderResources(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (ReadBind& bind = m_readBind[eShader::Pixel]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->PSSetShaderResources(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (ReadBind& bind = m_readBind[eShader::Compute]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->CSSetShaderResources(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }
    }

    if (m_dirtyFlags & ePipelineDirty::SamplerState)
    {
        if (SamplerBind& bind = m_samplerBind[eShader::Vertex]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->VSSetSamplers(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (SamplerBind& bind = m_samplerBind[eShader::Pixel]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->PSSetSamplers(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }

        if (SamplerBind& bind = m_samplerBind[eShader::Compute]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->CSSetSamplers(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin);
            bind.ClearDirty();
        }
    }

    // ps의 rw bind는 OMSetRenderTargetsAndUnorderedAccessViews() 호출에서 처리됨
    if (m_dirtyFlags & ePipelineDirty::UnorderedAccessView)
    {
        if (ReadWriteBind& bind = m_readWriteBind[eShaderRW::Compute]; bind.IsDirty())
        {
            m_pD3d11DeviceContext->CSSetUnorderedAccessViews(bind.dirtyBegin, bind.NumDirties(), bind.d3d11Resources.data() + bind.dirtyBegin, nullptr);
            bind.ClearDirty();
        }
    }

    if (m_dirtyFlags & ePipelineDirty::BlendState)
    {
        const VECTOR4 blendFactor = m_blendFactor.ToLinear();
        m_pD3d11DeviceContext->OMSetBlendState(GetOrCreateBlendState_(), blendFactor.e.data(), 0xFFFF'FFFF);
    }

    if (m_dirtyFlags & ePipelineDirty::DepthStencilState)
    {
        m_pD3d11DeviceContext->OMSetDepthStencilState(GetOrCreateDepthStencilState_(), m_stencilRef);
    }

    if (m_dirtyFlags & ePipelineDirty::RasterizerState)
    {
        m_pD3d11DeviceContext->RSSetState(GetOrCreateRasterizerState_());
    }

    if (m_dirtyFlags & ePipelineDirty::FrameBuffer || m_readWriteBind[eShaderRW::Pixel].IsDirty())
    {
        // 이전 프레임버퍼의 write 작업이 끝났기에 resolve를 수행한다.
        if (m_lastFbh && m_lastFbh != m_fbh)
        {
            const FrameBufferD3D11& fb = m_frameBufferPool[m_lastFbh];
            for (uint32_t i = 0; i < fb.numRts; ++i)
            {
                const Attachment& att = fb.atts[i];
                JUG_ASSERT(att.texh, "Invalid texture handle in frame buffer attachment.");

                TextureD3D11& texture = m_texturePool[att.texh];
                if (fb.bMSAA)   // resolve
                {
                    JUG_ASSERT(texture.pMsaaRtResource, "Invalid MSAA render target resource for texture in frame buffer attachment.");
                    const TextureFormatInfo formatInfo = MakeTextureFormatInfo_(texture.format);
                    for (uint32_t layer = 0; layer < texture.numLayers; ++layer)
                    {
                        const uint32_t dstIndex = CalcTextureIndex(att.mip, layer, texture.numMips);
                        m_pD3d11DeviceContext->ResolveSubresource(texture.pResource, dstIndex, texture.pMsaaRtResource, layer, formatInfo.srv);
                        ++m_stats.numResolves;
                    }
                }

                // 렌더 타겟에 밉이 있으면 갱신해 준다.
                if (texture.numMips > 1 && texture.pSRV && texture.flags.Has(eTextureOption::RenderTarget))
                {
                    m_pD3d11DeviceContext->GenerateMips(texture.pSRV);
                }
            }
        }

        // 언바인딩
        if (m_fbh)
        {
            const FrameBufferD3D11& fb   = m_frameBufferPool[m_fbh];
            ReadWriteBind&          bind = m_readWriteBind[eShaderRW::Pixel];

            // PS UAV 는 렌더 타겟과 같은 호출로 묶어야 한다.
            if (bind.IsDirty())
            {
                const UINT startSlot = fb.numRts;
                const UINT numUavs   = kNumMaxReadWriteSlots > startSlot ? kNumMaxReadWriteSlots - startSlot : 0u;
                m_pD3d11DeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(
                    fb.numRts,
                    fb.rtvs.data(),
                    fb.pDSV,
                    startSlot,
                    numUavs,
                    bind.d3d11Resources.data() + startSlot,
                    nullptr);

                bind.ClearDirty();
            }
            else
            {
                m_pD3d11DeviceContext->OMSetRenderTargets(fb.numRts, fb.rtvs.data(), fb.pDSV);
            }

            m_lastFbh = m_fbh;
        }
        else
        {
            ReadWriteBind& bind = m_readWriteBind[eShaderRW::Pixel];
            if (bind.IsDirty())
            {
                m_pD3d11DeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(
                    0,
                    nullptr,
                    nullptr,
                    0,
                    kNumMaxReadWriteSlots,
                    bind.d3d11Resources.data(),
                    nullptr);

                bind.ClearDirty();
            }
            else
            {
                m_pD3d11DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
            }

            m_lastFbh = kNullHandle;
        }
    }

    if (m_dirtyFlags & ePipelineDirty::Viewport)
    {
        D3D11_VIEWPORT viewport;
        viewport.TopLeftX = m_viewportX;
        viewport.TopLeftY = m_viewportY;
        viewport.Width    = m_viewportW;
        viewport.Height   = m_viewportH;
        viewport.MinDepth = 0.f;
        viewport.MaxDepth = 1.f;

        m_pD3d11DeviceContext->RSSetViewports(1, &viewport);
    }

    if (m_dirtyFlags & ePipelineDirty::ScissorRect)
    {
        D3D11_RECT rect;
        rect.left   = m_scissorX;
        rect.top    = m_scissorY;
        rect.right  = m_scissorX + m_scissorW;
        rect.bottom = m_scissorY + m_scissorH;

        m_pD3d11DeviceContext->RSSetScissorRects(1, &rect);
    }

    m_dirtyFlags = kZeroFlag;
}

// ===========================================
//  Bind
// ===========================================

void Graphics::SetVertexBuffer(
    const VertexBufferHandle   _vbh,
    const uint32_t             _offset,
    const uint32_t             _numVerticesOrZero,
    const InstanceBufferHandle _instbh,
    const uint32_t             _instanceOffset,
    const uint32_t             _numInstancesOrZero)
{
    const ARRAY<VertexStream, 1> streams {
        VertexStream { _vbh, _offset }
    };
    SetVertexBuffers(streams, _numVerticesOrZero, _instbh, _instanceOffset, _numInstancesOrZero);
}

void Graphics::SetVertexBuffers(
    const Span<const VertexStream> _streams,
    const uint32_t                 _numVerticesOrZero,
    const InstanceBufferHandle     _instbh,
    const uint32_t                 _instanceOffset,
    const uint32_t                 _numInstancesOrZero)
{
    JUG_ASSERT(_streams.size() <= kNumMaxStreams, "Too many vertex streams.");

    const uint32_t numStreams        = static_cast<uint32_t>(_streams.size());
    const bool     bNumStreamChanged = numStreams != m_numStreams;
    bool           bLayoutChanged    = bNumStreamChanged;
    bool           bBufferChanged    = bNumStreamChanged;

    uint32_t numVertices = Max<uint32_t>();
    for (uint32_t i = 0; i < numStreams; ++i)
    {

        ID3D11Buffer*      pBuffer = nullptr;
        VertexLayoutHandle vlh     = kNullHandle;
        uint32_t           stride  = 0;

        const VertexStream& stream = _streams[i];
        if (stream.vbh)
        {
            const VertexBufferD3D11& vb = m_vertexBufferPool[stream.vbh];

            JUG_ASSERT(stream.offset < vb.numVertices, "Vertex stream offset is out of range.");

            pBuffer     = vb.pBuffer;
            vlh         = vb.vlh;
            stride      = vb.stride;
            numVertices = Min(numVertices, vb.numVertices - stream.offset);
        }

        const uint32_t offset = stream.offset * stride;
        bLayoutChanged        = bLayoutChanged || m_vlhs[i] != vlh;
        bBufferChanged        = bBufferChanged || m_vbhs[i] != stream.vbh || m_vertexOffsets[i] != offset;

        m_vbhs[i]               = stream.vbh;
        m_vlhs[i]               = vlh;
        m_d3d11VertexBuffers[i] = pBuffer;
        m_vertexStrides[i]      = stride;
        m_vertexOffsets[i]      = offset;
    }

    for (uint32_t i = numStreams; i < m_numStreams; ++i)
    {
        m_vbhs[i]               = kNullHandle;
        m_vlhs[i]               = kNullHandle;
        m_d3d11VertexBuffers[i] = nullptr;
        m_vertexStrides[i]      = 0;
        m_vertexOffsets[i]      = 0;
    }

    // 유효한 스트림이 하나도 없으면 그릴 게 없다.
    if (numVertices == Max<uint32_t>())
    {
        numVertices = 0;
    }

    JUG_ASSERT(_numVerticesOrZero <= numVertices, "Vertex range exceeds the bound vertex streams.");

    m_numStreams  = numStreams;
    m_numVertices = _numVerticesOrZero == 0 ? numVertices : _numVerticesOrZero;

    ID3D11Buffer* pInstanceBuffer = nullptr;
    uint32_t      instanceStride  = 0;
    uint32_t      numInstances    = 0;
    if (_instbh)
    {
        const InstanceBufferD3D11& instb              = m_instanceBufferPool[_instbh];
        const uint32_t             numBufferInstances = instb.numInstances;

        JUG_ASSERT(_instanceOffset < numBufferInstances, "Instance stream offset is out of range.");
        JUG_ASSERT(_numInstancesOrZero <= numBufferInstances - _instanceOffset, "Instance range exceeds the instance buffer.");

        pInstanceBuffer = instb.pBuffer;
        instanceStride  = instb.stride;
        numInstances    = numBufferInstances - _instanceOffset;
    }

    const uint32_t instanceOffset = _instanceOffset * instanceStride;
    if (bNumStreamChanged || m_pD3d11InstanceBuffer != pInstanceBuffer
        || m_instanceDataStride != instanceStride || m_instanceOffset != instanceOffset)
    {
        m_dirtyFlags |= ePipelineDirty::InstanceBuffer;
    }

    bLayoutChanged         = bLayoutChanged || m_instanceDataStride != instanceStride;
    m_instbh               = _instbh;
    m_pD3d11InstanceBuffer = pInstanceBuffer;
    m_instanceDataStride   = instanceStride;
    m_instanceOffset       = instanceOffset;
    m_numInstances         = _numInstancesOrZero == 0 ? numInstances : _numInstancesOrZero;

    if (bBufferChanged)
    {
        m_dirtyFlags |= ePipelineDirty::VertexBuffer;
    }

    if (bLayoutChanged)
    {
        m_dirtyFlags |= ePipelineDirty::InputLayout;
    }
}

void Graphics::SetIndexBuffer(
    const IndexBufferHandle _ibh,
    const uint32_t          _offset,
    const uint32_t          _numIndicesOrZero)
{
    ID3D11Buffer* pBuffer    = nullptr;
    DXGI_FORMAT   format     = DXGI_FORMAT_UNKNOWN;
    uint32_t      numIndices = 0;

    if (_ibh)
    {
        const IndexBufferD3D11& ib = m_indexBufferPool[_ibh];
        pBuffer                    = ib.pBuffer;
        format                     = ib.format;
        numIndices                 = ib.numIndices;
    }

    if (m_ibh != _ibh)
    {
        m_dirtyFlags |= ePipelineDirty::IndexBuffer;
    }

    m_ibh               = _ibh;
    m_pD3d11IndexBuffer = pBuffer;
    m_dxgiIndexFormat   = format;
    m_indexOffset       = _offset;
    m_numIndices        = _numIndicesOrZero == 0 ? numIndices - _offset : _numIndicesOrZero;
    JUG_ASSERT(m_indexOffset + m_numIndices <= numIndices, "Index buffer range is out of bounds.");
}

void Graphics::SetConstantBuffer(
    const ConstantBufferHandle _cbh,
    const eShader              _shader,
    const uint32_t             _slot)
{
    JUG_ASSERT(_slot < kNumMaxCBufferSlots, "Constant buffer slot is out of range.");

    CBufferBind& bind = m_cbufferBind[_shader];
    if (bind.resources[_slot] == _cbh)
    {
        return;
    }

    bind.resources[_slot]      = _cbh;
    bind.d3d11Resources[_slot] = _cbh ? m_constantBufferPool[_cbh].pBuffer : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= ePipelineDirty::ConstantBuffer;
}

void Graphics::SetTexture(
    const TextureHandle _texh,
    const eShader       _shader,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadSlots, "Shader resource slot is out of range.");

    ReadBind& bind = m_readBind[_shader];
    if (bind.resources[_slot] == _texh)
    {
        return;
    }

    bind.resources[_slot]      = _texh;
    bind.d3d11Resources[_slot] = _texh ? m_texturePool[_texh].pSRV : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= ePipelineDirty::ShaderResourceView;
}

void Graphics::SetTextureRW(
    const TextureHandle _texh,
    const eShaderRW     _shader,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadWriteSlots, "Unordered access slot is out of range.");

    ReadWriteBind& bind = m_readWriteBind[_shader];
    if (bind.resources[_slot] == _texh)
    {
        return;
    }

    bind.resources[_slot]      = _texh;
    bind.d3d11Resources[_slot] = _texh ? m_texturePool[_texh].pUAV : nullptr;
    bind.MarkDirty(_slot);
    m_dirtyFlags |= ePipelineDirty::UnorderedAccessView;
}

void Graphics::SetBuffer(
    const StorageBufferHandle _sbh,
    const eShader             _shader,
    const uint32_t            _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadSlots, "Shader resource slot is out of range.");

    ReadBind& bind = m_readBind[_shader];
    if (bind.resources[_slot] == _sbh)
    {
        return;
    }

    bind.resources[_slot]      = _sbh;
    bind.d3d11Resources[_slot] = _sbh ? m_storageBufferPool[_sbh].pSRV : nullptr;
    bind.MarkDirty(_slot);
    m_dirtyFlags |= ePipelineDirty::ShaderResourceView;
}

void Graphics::SetBufferRW(
    const StorageBufferHandle _sbh,
    const eShaderRW           _shader,
    const uint32_t            _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadWriteSlots, "Unordered access slot is out of range.");

    ReadWriteBind& bind = m_readWriteBind[_shader];
    if (bind.resources[_slot] == _sbh)
    {
        return;
    }

    bind.resources[_slot]      = _sbh;
    bind.d3d11Resources[_slot] = _sbh ? m_storageBufferPool[_sbh].pUAV : nullptr;
    bind.MarkDirty(_slot);
    m_dirtyFlags |= ePipelineDirty::UnorderedAccessView;
}

void Graphics::SetSampler(
    const Flags<eSampler> _flags,
    const eShader         _shader,
    const uint32_t        _slot)
{
    SetSampler(_flags, RGBA::kZero, _shader, _slot);
}

void Graphics::SetSampler(
    const Flags<eSampler> _flags,
    const RGBA            _border,
    const eShader         _shader,
    const uint32_t        _slot)
{
    JUG_ASSERT(_slot < kNumMaxSamplerSlots, "Sampler slot is out of range.");

    const Sampler sampler = { _flags, _border };
    SamplerBind&  bind    = m_samplerBind[_shader];
    if (bind.resources[_slot] == sampler)
    {
        return;
    }

    bind.resources[_slot]      = sampler;
    bind.d3d11Resources[_slot] = GetOrCreateSamplerState_(_flags, _border);
    bind.MarkDirty(_slot);
    m_dirtyFlags |= ePipelineDirty::SamplerState;
}

// ===========================================
//  Render State
// ===========================================

void Graphics::SetRenderState(
    const Flags<eRenderState> _flags)
{
    if (m_renderStateFlags == _flags)
    {
        return;
    }

    const Flags<eRenderState> changed = m_renderStateFlags ^ _flags;
    m_renderStateFlags                = _flags;

    if (changed.HasAny(kTopologyStateMask))
    {
        m_dirtyFlags |= ePipelineDirty::PrimitiveTopology;
    }
    if (changed.HasAny(kRasterizerStateMask))
    {
        m_dirtyFlags |= ePipelineDirty::RasterizerState;
    }
    if (changed.HasAny(kBlendStateMask))
    {
        m_dirtyFlags |= ePipelineDirty::BlendState;
    }
    if (changed.HasAny(kDepthStencilStateMask))
    {
        m_dirtyFlags |= ePipelineDirty::DepthStencilState;
    }
}

void Graphics::SetBlend(
    const Flags<eBlend> _flags,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxRenderTargetSlots, "Render target slot is out of range.");

    if (m_blendFlags[_slot] == _flags)
    {
        return;
    }

    m_blendFlags[_slot] = _flags;
    m_dirtyFlags |= ePipelineDirty::BlendState;
}

void Graphics::SetBlendFactor(
    const RGBA _factor)
{
    if (m_blendFactor.rgba == _factor.rgba)
    {
        return;
    }

    m_blendFactor = _factor;
    m_dirtyFlags |= ePipelineDirty::BlendState;
}

void Graphics::SetStencil(
    const Flags<eStencil> _frontFlags,
    const Flags<eStencil> _backFlags,
    const uint8_t         _stencilRef)
{
    if (m_fstencilFlags == _frontFlags
        && m_bstencilFlags == _backFlags
        && m_stencilRef == _stencilRef)
    {
        return;
    }

    m_fstencilFlags = _frontFlags;
    m_bstencilFlags = _backFlags;
    m_stencilRef    = _stencilRef;
    m_dirtyFlags |= ePipelineDirty::DepthStencilState;
}

// ===========================================
//  Program & Output
// ===========================================

void Graphics::SetProgram(
    const ProgramHandle _ph)
{
    if (m_ph == _ph)
    {
        return;
    }

    m_ph = _ph;
    m_dirtyFlags |= ePipelineDirty::Program;
}

void Graphics::SetComputeProgram(
    const ProgramHandle _ph)
{
    if (m_computePh == _ph)
    {
        return;
    }

    m_computePh = _ph;
    m_dirtyFlags |= ePipelineDirty::ComputeProgram;
}

void Graphics::SetFrameBuffer(
    const FrameBufferHandle _fbh)
{
    if (m_fbh == _fbh)
    {
        return;
    }

    m_fbh = _fbh;
    m_dirtyFlags |= ePipelineDirty::FrameBuffer;
}

void Graphics::SetViewport(
    const float _x,
    const float _y,
    const float _width,
    const float _height)
{
    if (m_viewportX == _x && m_viewportY == _y && m_viewportW == _width && m_viewportH == _height)   // NOLINT
    {
        return;
    }

    m_viewportX = _x;
    m_viewportY = _y;
    m_viewportW = _width;
    m_viewportH = _height;
    m_dirtyFlags |= ePipelineDirty::Viewport;
}

void Graphics::SetScissor(
    const int _x,
    const int _y,
    const int _width,
    const int _height)
{
    if (m_scissorX == _x && m_scissorY == _y && m_scissorW == _width && m_scissorH == _height)
    {
        return;
    }

    m_scissorX = _x;
    m_scissorY = _y;
    m_scissorW = _width;
    m_scissorH = _height;
    m_dirtyFlags |= ePipelineDirty::ScissorRect;
}

void Graphics::SetSubmitParam(
    const eSubmitParam _param,
    const uint32_t     _value)
{
    m_submitParams[_param] = _value;
}

// ===========================================
//  Submit
// ===========================================

void Graphics::Touch()
{
    ApplyPipeline_();
}

void Graphics::Reset()
{
    m_d3d11VertexBuffers = {};
    m_vbhs               = {};
    m_vlhs               = {};
    m_vertexStrides      = {};
    m_vertexOffsets      = {};
    m_numStreams         = 0;
    m_numVertices        = 0;

    m_pD3d11IndexBuffer = nullptr;
    m_dxgiIndexFormat   = DXGI_FORMAT_UNKNOWN;
    m_indexOffset       = 0;
    m_numIndices        = 0;
    m_ibh               = kNullHandle;

    m_pD3d11InstanceBuffer = nullptr;
    m_instanceDataStride   = 0;
    m_instanceOffset       = 0;
    m_instbh               = kNullHandle;
    m_numInstances         = 0;

    m_readBind      = {};
    m_cbufferBind   = {};
    m_samplerBind   = {};
    m_readWriteBind = {};

    m_ph           = kNullHandle;
    m_computePh    = kNullHandle;
    m_submitParams = {};

    m_renderStateFlags = eRenderState::None;
    m_blendFlags       = {};
    m_blendFactor      = {};
    m_fstencilFlags    = eStencil::None;
    m_bstencilFlags    = eStencil::None;
    m_stencilRef       = 0;

    m_fbh     = kNullHandle;
    m_lastFbh = kNullHandle;

    m_viewportX = 0.f;
    m_viewportY = 0.f;
    m_viewportW = 0.f;
    m_viewportH = 0.f;

    m_scissorX = 0;
    m_scissorY = 0;
    m_scissorW = 0;
    m_scissorH = 0;

    m_dirtyFlags = kAllFlag;
}

void Graphics::Submit()
{
    ApplyPipeline_();

    const UINT startVertex   = m_submitParams[eSubmitParam::StartVertexLocation];
    const UINT startIndex    = m_submitParams[eSubmitParam::StartIndexLocation];
    const UINT startInstance = m_submitParams[eSubmitParam::StartInstanceLocation];
    const INT  baseVertex    = static_cast<INT>(m_submitParams[eSubmitParam::BaseVertexLocation]);

    const bool bIndexed   = m_pD3d11IndexBuffer != nullptr;
    const bool bInstanced = m_pD3d11InstanceBuffer != nullptr && m_numInstances > 0;

    if (bIndexed && bInstanced)
    {
        m_pD3d11DeviceContext->DrawIndexedInstanced(m_numIndices, m_numInstances, startIndex + m_indexOffset, baseVertex, startInstance);
    }
    else if (bIndexed)
    {
        m_pD3d11DeviceContext->DrawIndexed(m_numIndices, startIndex + m_indexOffset, baseVertex);
    }
    else if (bInstanced)
    {
        m_pD3d11DeviceContext->DrawInstanced(m_numVertices, m_numInstances, startVertex, startInstance);
    }
    else
    {
        m_pD3d11DeviceContext->Draw(m_numVertices, startVertex);
    }

    ++m_stats.numDrawCalls;
}

void Graphics::Submit(
    const StorageBufferHandle _indirectSbh,
    const uint32_t            _offset,
    const uint32_t            _numDrawsOrZero)
{
    const StorageBufferD3D11& indirectBuffer = m_storageBufferPool[_indirectSbh];
    JUG_ASSERT(indirectBuffer.type == eStorageBuffer::IndirectArgs, "Indirect submit requires an indirect args buffer.");
    JUG_ASSERT(_offset % kIndirectArgsStride == 0, "Indirect args offset must be a multiple of the args stride.");

    ApplyPipeline_();

    const uint32_t maxDraws = (indirectBuffer.byteWidth - _offset) / kIndirectArgsStride;
    const uint32_t numDraws = _numDrawsOrZero == 0 ? maxDraws : _numDrawsOrZero;
    JUG_ASSERT(numDraws <= maxDraws, "Indirect draw count exceeds the buffer capacity.");

    const bool bIndexed = m_pD3d11IndexBuffer != nullptr;

    for (uint32_t i = 0; i < numDraws; ++i)
    {
        const UINT argOffset = _offset + i * kIndirectArgsStride;

        if (bIndexed)
        {
            m_pD3d11DeviceContext->DrawIndexedInstancedIndirect(indirectBuffer.pBuffer, argOffset);
        }
        else
        {
            m_pD3d11DeviceContext->DrawInstancedIndirect(indirectBuffer.pBuffer, argOffset);
        }
    }

    m_stats.numDrawCalls += static_cast<int>(numDraws);
}

void Graphics::Dispatch(
    const uint32_t _numGroupsX,
    const uint32_t _numGroupsY,
    const uint32_t _numGroupsZ)
{
    JUG_ASSERT(m_computePh, "Dispatch requires a compute program.");

    ApplyPipeline_();

    m_pD3d11DeviceContext->Dispatch(_numGroupsX, _numGroupsY, _numGroupsZ);
    ++m_stats.numDispatchCalls;
}

void Graphics::Dispatch(
    const StorageBufferHandle _indirectSbh,
    const uint32_t            _offset)
{
    JUG_ASSERT(m_computePh, "Dispatch requires a compute program.");

    const StorageBufferD3D11& indirectBuffer = m_storageBufferPool[_indirectSbh];
    JUG_ASSERT(indirectBuffer.type == eStorageBuffer::IndirectArgs, "Indirect dispatch requires an indirect args buffer.");

    ApplyPipeline_();

    m_pD3d11DeviceContext->DispatchIndirect(indirectBuffer.pBuffer, _offset);
    ++m_stats.numDispatchCalls;
}

}   // namespace jug
