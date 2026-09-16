#include "pch.h"

#include <Windows.h>
#include <dxgi1_6.h>

#include <JugX/CoreLogger.h>
#include <JugX/EnumRefl.h>
#include <JugX/Math.h>
#include <JugX/MemoryHasher.h>
#include <JugX/StringEncoder.h>

#include "Graphics.h"

#include <fmt/format.h>
#include <JugX/Align.h>
#include <JugX/SystemError.h>

#include "DxUtils.h"
#include "Shader.h"

namespace jug
{

namespace
{
    Graphics*          g_pSingleton         = nullptr;
    constexpr uint32_t kCBufferAlign        = 16;
    constexpr uint32_t kInstanceBufferAlign = 16;
    constexpr size_t   kMaxDebugNameLength  = 255;
    constexpr uint32_t kIndirectArgsStride  = 32;

    struct TextureFormatInfo
    {
        DXGI_FORMAT tex  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT rtv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT dsv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srgb = DXGI_FORMAT_UNKNOWN;
    };

    [[nodiscard]] String MakeHResultMessage(
        const HRESULT _hr)
    {
        return MakeSystemError(_hr, eSystemError::OS).MakeMessage();
    }

    bool IsDeviceLostError_(
        const HRESULT _hr)
    {
        return _hr == DXGI_ERROR_DEVICE_REMOVED
            || _hr == DXGI_ERROR_DEVICE_RESET
            || _hr == DXGI_ERROR_DEVICE_HUNG
            || _hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
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

    [[nodiscard]] TextureFormatInfo GetTextureFormatInfo_(
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

    [[nodiscard]] DXGI_SAMPLE_DESC MakeSampleDesc_(
        ID3D11Device*     _pD3d11Device,
        const DXGI_FORMAT _format,
        const eMSAA       _msaa)
    {
        JUG_ASSERT(_pD3d11Device && _format != DXGI_FORMAT_UNKNOWN, "Invalid device or format.");

        constexpr ENUM_ARRAY<eMSAA, uint32_t> kMsaaSamples = { 1, 2, 4, 8, 16 };
        constexpr ENUM_ARRAY<eMSAA, eMSAA>    kLowerMSAA   = { eMSAA::None, eMSAA::None, eMSAA::x2, eMSAA::x4, eMSAA::x8 };

        // 최대한 높은 샘플링 품질을 사용하도록 설정
        DXGI_SAMPLE_DESC sd = {};
        for (eMSAA msaa = _msaa; msaa != eMSAA::None; msaa = kLowerMSAA[msaa])
        {
            sd.Count       = kMsaaSamples[msaa];
            UINT numLevels = 0;
            if (SUCCEEDED(_pD3d11Device->CheckMultisampleQualityLevels(_format, sd.Count, &numLevels)) && numLevels > 0)
            {
                sd.Quality = numLevels - 1;
                return sd;
            }
        }

        sd.Count   = 1;
        sd.Quality = 0;
        return sd;
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

    // [AI] 디버그 레이어 메시지를 프레임마다 비운다. 비우지 않으면 큐가 차서 이후 메시지를 잃는다.
    //      -> 멤버함수로 이동
    // void LogInfoQueueMessages_(
    //     ID3D11InfoQueue* _pInfoQueueOrNull)
    // {
    //     if (_pInfoQueueOrNull == nullptr)
    //     {
    //         return;
    //     }

    //     const UINT64      numMessages = _pInfoQueueOrNull->GetNumStoredMessages();
    //     Vector<std::byte> buffer;

    //     for (UINT64 i = 0; i < numMessages; ++i)
    //     {
    //         SIZE_T byteWidth = 0;
    //         if (FAILED(_pInfoQueueOrNull->GetMessage(i, nullptr, &byteWidth)) || byteWidth == 0)
    //         {
    //             continue;
    //         }

    //         buffer.resize(byteWidth);
    //         auto* pMessage = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
    //         if (FAILED(_pInfoQueueOrNull->GetMessage(i, pMessage, &byteWidth)))
    //         {
    //             continue;
    //         }

    //         const StringView description { pMessage->pDescription, pMessage->DescriptionByteLength };
    //         switch (pMessage->Severity)   // NOLINT(clang-diagnostic-switch-enum)
    //         {
    //             case D3D11_MESSAGE_SEVERITY_CORRUPTION:
    //             case D3D11_MESSAGE_SEVERITY_ERROR: JUG_CORE_LOG_ERROR("[D3D11] {}", description); break;
    //             case D3D11_MESSAGE_SEVERITY_WARNING: JUG_CORE_LOG_WARN("[D3D11] {}", description); break;
    //             default: JUG_CORE_LOG_INFO("[D3D11] {}", description); break;
    //         }
    //     }

    //     _pInfoQueueOrNull->ClearStoredMessages();
    // }

}   // namespace

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
        if (frameBuffer.pDxgiSwapChain)
        {
            JUG_DISCARD_RETURN(frameBuffer.pDxgiSwapChain->SetFullscreenState(FALSE, nullptr));
        }
    }

    if (ReportLiveObjects() > 0)
    {
        JUG_ASSERT(false, "There are leaked graphics resources. Check the log for details.");
    }

    for (FrameBufferHandle fbh: m_frameBufferPool.GetHandles())
    {
        Destroy(fbh);
    }
    for (VertexBufferHandle vbh: m_vertexBufferPool.GetHandles())
    {
        Destroy(vbh);
    }
    for (IndexBufferHandle ibh: m_indexBufferPool.GetHandles())
    {
        Destroy(ibh);
    }
    for (ConstantBufferHandle cbh: m_constantBufferPool.GetHandles())
    {
        Destroy(cbh);
    }
    for (StorageBufferHandle sbh: m_storageBufferPool.GetHandles())
    {
        Destroy(sbh);
    }
    for (TextureHandle texh: m_texturePool.GetHandles())
    {
        Destroy(texh);
    }
    for (ShaderHandle sh: m_shaderPool.GetHandles())
    {
        Destroy(sh);
    }
    for (ProgramHandle ph: m_programPool.GetHandles())
    {
        Destroy(ph);
    }

    // vertex layouer은 graphics resource가 아니라서 해제할 필요 없음

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

Graphics* Graphics::GetInstance()
{
    JUG_ASSERT(g_pSingleton, "Graphics instance is not created yet.");
    return g_pSingleton;
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
        const VertexLayoutD3D11& vl = *it;
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

VertexBufferHandle Graphics::CreateVertexBuffer(
    const MemoryView    _vertexData,
    const VertexLayout& _vl)
{
    const uint32_t stride = _vl.GetStride();
    JUG_ASSERT(!_vertexData.IsEmpty(), "A static vertex buffer requires initial data. Use CreateDynamicVertexBuffer instead.");
    JUG_ASSERT(_vertexData.GetSize() % stride == 0, "Vertex data size is not a multiple of the vertex stride.");

    const uint32_t numVertices = static_cast<uint32_t>(_vertexData.GetSize()) / stride;
    return m_vertexBufferPool.Emplace(CreateVertexBuffer_(numVertices, _vl, false, _vertexData));
}

VertexBufferHandle Graphics::CreateDynamicVertexBuffer(
    const uint32_t      _numVertices,
    const VertexLayout& _vl)
{
    return m_vertexBufferPool.Emplace(CreateVertexBuffer_(_numVertices, _vl, true, {}));
}

VertexBufferHandle Graphics::CreateInstanceBuffer(
    const uint32_t _numInstances,
    const uint32_t _stride)
{
    return m_vertexBufferPool.Emplace(CreateInstanceBuffer_(_numInstances, _stride));
}

IndexBufferHandle Graphics::CreateIndexBuffer(
    const MemoryView _indexData,
    const bool       _bU32)
{
    const uint32_t stride = _bU32 ? sizeof(uint32_t) : sizeof(uint16_t);
    JUG_ASSERT(!_indexData.IsEmpty(), "A static index buffer requires initial data. Use CreateDynamicIndexBuffer instead.");
    JUG_ASSERT(_indexData.GetSize() % stride == 0, "Index data size is not a multiple of the index stride.");
    JUG_ASSERT(_indexData.GetSize() > 0, "Index data must not be empty.");
    const uint32_t numIndices = static_cast<uint32_t>(_indexData.GetSize()) / stride;
    return m_indexBufferPool.Emplace(CreateIndexBuffer_(numIndices, _bU32, false, _indexData));
}

IndexBufferHandle Graphics::CreateDynamicIndexBuffer(
    const uint32_t _numIndices,
    const bool     _bU32)
{
    return m_indexBufferPool.Emplace(CreateIndexBuffer_(_numIndices, _bU32, true, {}));
}

ConstantBufferHandle Graphics::CreateConstantBuffer(
    const uint32_t _byteWidth)
{
    return m_constantBufferPool.Emplace(CreateConstantBuffer_(_byteWidth));
}

StorageBufferHandle Graphics::CreateStructuredBuffer(
    const uint32_t                    _numElements,
    const uint32_t                    _stride,
    const Flags<eStorageBufferOption> _flags,
    const MemoryView                  _initDataOrEmpty)
{
    return m_storageBufferPool.Emplace(CreateStructuredBuffer_(_numElements, _stride, _flags, _initDataOrEmpty));
}

StorageBufferHandle Graphics::CreateReadbackBuffer(
    const uint32_t _byteWidth)
{
    return m_storageBufferPool.Emplace(CreateReadbackBuffer_(_byteWidth));
}

StorageBufferHandle Graphics::CreateIndirectBuffer(
    const uint32_t                    _numDraws,
    const Flags<eStorageBufferOption> _flags)
{
    JUG_ASSERT(_numDraws > 0, "An indirect buffer requires a non-zero number of draws.");
    return m_storageBufferPool.Emplace(CreateIndirectArgsBuffer_(_numDraws, _flags));
}

size_t Graphics::ReadBuffer(
    const StorageBufferHandle _readbackSbh,
    const MutableMemoryView   _dst)
{
    JUG_ASSERT(!_dst.IsEmpty(), "ReadBuffer requires a non-empty destination.");

    const StorageBufferD3D11& sb = m_storageBufferPool[_readbackSbh];
    JUG_ASSERT(sb.type == eStorageBuffer::Readback, "ReadBuffer can only be called on a readback storage buffer.");

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(sb.pBuffer, 0, D3D11_MAP_READ, 0, &mapped));
    const size_t read = Min(_dst.GetSize(), static_cast<size_t>(sb.byteWidth));
    std::memcpy(_dst.GetPtr(), mapped.pData, read);
    m_pD3d11DeviceContext->Unmap(sb.pBuffer, 0);
    return read;
}

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

    IAdapter*             pAdapterOrNull = m_dxgi.GetAdapterOrNull();
    const D3D_DRIVER_TYPE driverType     = pAdapterOrNull ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    // create device
    HRESULT hr = CreateD3d11Device_(pAdapterOrNull, driverType, flags, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);

    // Debug layer를 사용하지 못하는 경우. fallback.
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
    {
        JUG_CORE_LOG_WARN("D3D11 debug layer is not available. Retrying without it. ({})", MakeHResultMessage(hr));

        const UINT nonDebugFlags = flags & ~D3D11_CREATE_DEVICE_DEBUG;
        hr                       = CreateD3d11Device_(pAdapterOrNull, driverType, nonDebugFlags, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);
    }

    // 하드웨어 디바이스를 만들지 못하는 경우. fallback.
    bool bSoftwareRasterizer = false;
    if (FAILED(hr))
    {
        JUG_CORE_LOG_WARN("Failed to create a hardware D3D11 device. Falling back to the WARP software rasterizer. ({})", MakeHResultMessage(hr));

        hr = CreateD3d11Device_(nullptr, D3D_DRIVER_TYPE_WARP, flags, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);

        // Debug layer를 사용하지 못하는 경우. fallback.
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
        {
            JUG_CORE_LOG_WARN("D3D11 debug layer is not available. Retrying without it. ({})", MakeHResultMessage(hr));

            const UINT nonDebugFlags = flags & ~D3D11_CREATE_DEVICE_DEBUG;
            hr                       = CreateD3d11Device_(nullptr, D3D_DRIVER_TYPE_WARP, nonDebugFlags, &m_pD3d11Device, &m_d3dFeatureLevel, &m_pD3d11DeviceContext);
        }

        bSoftwareRasterizer = SUCCEEDED(hr);
    }
    JUG_DX_CHECK(hr);

    // 디바이스 만들기 성공
    m_caps.bDebugLayerEnabled  = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
    m_caps.bSoftwareRasterizer = bSoftwareRasterizer;

    if (bSoftwareRasterizer)
    {
        m_caps.vendorID           = 0;
        m_caps.deviceID           = 0;
        m_caps.videoMemory        = 0;
        m_caps.systemMemory       = 0;
        m_caps.sharedSystemMemory = 0;
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

    if (m_pD3d11InfoQueueOrNull)
    {
        JUG_DISCARD_RETURN(m_pD3d11InfoQueueOrNull->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE));
        JUG_DISCARD_RETURN(m_pD3d11InfoQueueOrNull->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE));
    }
}

void Graphics::InitTimerQueries_()
{
    D3D11_QUERY_DESC tsqd = {};
    tsqd.Query            = D3D11_QUERY_TIMESTAMP;

    D3D11_QUERY_DESC dqd = {};
    dqd.Query            = D3D11_QUERY_TIMESTAMP_DISJOINT;

    for (size_t i = 0; i < kNumInitTimerQueries; ++i)
    {
        TimerQuery query = {};
        JUG_DX_CHECK(m_pD3d11Device->CreateQuery(&dqd, &query.pDisjoint));
        JUG_DX_CHECK(m_pD3d11Device->CreateQuery(&tsqd, &query.pBegin));
        JUG_DX_CHECK(m_pD3d11Device->CreateQuery(&tsqd, &query.pEnd));
        m_timerQueries.Push(query);
    }
}

void Graphics::CleanUpTimerQueries_()
{
    while (!m_timerQueries.IsEmpty())
    {
        TimerQuery query = m_timerQueries.Front();
        m_timerQueries.Pop();
        JUG_DX_RELEASE(query.pDisjoint);
        JUG_DX_RELEASE(query.pBegin);
        JUG_DX_RELEASE(query.pEnd);
    }
}

Graphics::VertexBufferD3D11 Graphics::CreateVertexBuffer_(
    const uint32_t      _numElems,
    const VertexLayout& _vl,
    const bool          _bDynamic,
    const MemoryView    _initDataOrEmpty)
{
    JUG_ASSERT(_numElems > 0, "Vertex buffer must have at least one element.");
    JUG_ASSERT(_vl.GetNumAttribs() > 0, "Vertex layout must have at least one attribute.");

    VertexBufferD3D11 vb = {};
    vb.bDynamic          = _bDynamic;
    vb.stride            = _vl.GetStride();
    vb.byteWidth         = vb.stride * _numElems;
    vb.vlh               = GetOrAllocVertexLayoutHandle_(_vl);

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

Graphics::VertexBufferD3D11 Graphics::CreateInstanceBuffer_(
    const uint32_t _numInstances,
    const uint32_t _stride) const
{
    JUG_ASSERT(_numInstances > 0, "Instance buffer must have at least one instance.");
    JUG_ASSERT(_stride > 0 && _stride % kInstanceBufferAlign == 0, "Instance buffer stride must be a non-zero multiple of 16 bytes.");

    VertexBufferD3D11 vb = {};
    vb.bDynamic          = false;
    vb.stride            = _stride;
    vb.byteWidth         = vb.stride * _numInstances;
    vb.vlh               = kNullHandle;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = vb.stride * _numInstances;
    bd.Usage             = D3D11_USAGE_DEFAULT;
    bd.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags    = 0;
    bd.MiscFlags         = 0;

    JUG_DX_CHECK(m_pD3d11Device->CreateBuffer(&bd, nullptr, &vb.pBuffer));
    return vb;
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
    JUG_ASSERT(_byteWidth > 0 && _byteWidth % kCBufferAlign == 0, "Constant buffer size must be a non-zero multiple of 16 bytes.");

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

VertexLayoutHandle Graphics::GetOrAllocVertexLayoutHandle_(
    const VertexLayout& _vl)
{
    JUG_ASSERT(_vl.GetNumAttribs() > 0, "A vertex layout must have at least one attribute.");

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

void Graphics::ResolveFrameBuffer_(
    const FrameBufferHandle _fbh)
{
    JUG_ASSERT(_fbh, "Invalid frame buffer handle.");

    const FrameBufferD3D11& fb      = m_frameBufferPool.Get(_fbh);
    const uint32_t          numAtts = fb.numRts + (fb.bHasDepthStencil ? 1 : 0);
    for (uint32_t i = 0; i < numAtts; ++i)
    {
        const Attachment& att = fb.attachments[i];
        JUG_ASSERT(att.texh, "Invalid texture handle in frame buffer attachment.");

        // resolve
        TextureD3D11& texture = m_texturePool.Get(att.texh);
        if (texture.pMsaaRtResource && texture.pResource)
        {
            const TextureFormatInfo texInfo = GetTextureFormatInfo_(texture.format);
            m_pD3d11DeviceContext->ResolveSubresource(texture.pResource, 0, texture.pMsaaRtResource, 0, texInfo.srv);
            ++m_stats.numResolves;
        }

        // 렌더 타겟에 밉이 있으면 갱신해 준다.
        if (texture.numMips > 1 && texture.pSRV && texture.flags.Has(eTextureOption::RenderTarget))
        {
            m_pD3d11DeviceContext->GenerateMips(texture.pSRV);
        }
    }
}

// ===========================================
//  Buffer Utils
// ===========================================

VertexLayoutHandle Graphics::CreateVertexLayout(
    const VertexLayout& _vl)
{
    return GetOrAllocVertexLayoutHandle_(_vl);
}

ID3D11Buffer* Graphics::GetD3d11Buffer_(
    const AnyBufferHandle _abh) const
{
    switch (_abh.GetType())
    {
        case eBuffer::Vertex: return m_vertexBufferPool.Get(_abh.GetVertexBufferHandle()).pBuffer;
        case eBuffer::Index: return m_indexBufferPool.Get(_abh.GetIndexBufferHandle()).pBuffer;
        case eBuffer::Storage: return m_storageBufferPool.Get(_abh.GetStorageBufferHandle()).pBuffer;
        case eBuffer::Constant: return m_constantBufferPool.Get(_abh.GetConstantBufferHandle()).pBuffer;
        default: JUG_ASSERT(false, "Unknown buffer type."); return nullptr;
    }
}

uint32_t Graphics::GetBufferByteWidth_(
    const AnyBufferHandle _bh) const
{
    switch (_bh.GetType())
    {
        case eBuffer::Vertex: return m_vertexBufferPool.Get(_bh.GetVertexBufferHandle()).byteWidth;
        case eBuffer::Index: return m_indexBufferPool.Get(_bh.GetIndexBufferHandle()).byteWidth;
        case eBuffer::Storage: return m_storageBufferPool.Get(_bh.GetStorageBufferHandle()).byteWidth;
        case eBuffer::Constant: return m_constantBufferPool.Get(_bh.GetConstantBufferHandle()).byteWidth;
        default: JUG_ASSERT(false, "Unknown buffer type."); return 0;
    }
}

bool Graphics::IsDynamicBuffer_(
    const AnyBufferHandle _bh) const
{
    switch (_bh.GetType())
    {
        case eBuffer::Vertex: return m_vertexBufferPool.Get(_bh.GetVertexBufferHandle()).bDynamic;
        case eBuffer::Index: return m_indexBufferPool.Get(_bh.GetIndexBufferHandle()).bDynamic;
        case eBuffer::Storage: return m_storageBufferPool.Get(_bh.GetStorageBufferHandle()).flags.Has(eStorageBufferOption::Dynamic);
        case eBuffer::Constant: return true;   // 상수 버퍼는 항상 DYNAMIC 이다.
        default: JUG_ASSERT(false, "Unknown buffer type."); return false;
    }
}

void Graphics::UpdateBuffer(
    const AnyBufferHandle _abh,
    const MemoryView      _data,
    const uint32_t        _offset,
    const bool            _bDiscard)
{
    JUG_ASSERT(_abh, "Invalid buffer handle.");
    JUG_ASSERT(!_data.IsEmpty(), "UpdateBuffer requires non-empty data.");

    ID3D11Buffer*  pBuffer   = GetD3d11Buffer_(_abh);
    const uint32_t writeSize = static_cast<uint32_t>(_data.GetSize());
    JUG_ASSERT(_offset + writeSize <= GetBufferByteWidth_(_abh), "Buffer update range is out of bounds.");

    if (IsDynamicBuffer_(_abh))
    {
        // DISCARD 는 전체를 새로 쓸 때만 유효하다. 부분 갱신은 NO_OVERWRITE 로만 안전하다.
        const bool      bWholeBuffer = _offset == 0 && writeSize == GetBufferByteWidth_(_abh);
        const D3D11_MAP mapType      = (_bDiscard && bWholeBuffer) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        JUG_DX_CHECK(m_pD3d11DeviceContext->Map(pBuffer, 0, mapType, 0, &mapped));
        std::memcpy(static_cast<uint8_t*>(mapped.pData) + _offset, _data.GetPtr(), writeSize);
        m_pD3d11DeviceContext->Unmap(pBuffer, 0);
        return;
    }

    // [AI] DEFAULT 버퍼는 UpdateSubresource 로 바로 쓴다.
    //      기존 코드는 스테이징 버퍼에 Map/memcpy 한 뒤 CopySubresourceRegion 하는 2 패스였는데,
    //      드라이버가 알아서 하는 일을 손으로 한 번 더 하는 셈이라 느리기만 했다.
    D3D11_BOX box = {};
    box.left      = _offset;
    box.right     = _offset + writeSize;
    box.top       = 0;
    box.bottom    = 1;
    box.front     = 0;
    box.back      = 1;

    m_pD3d11DeviceContext->UpdateSubresource(pBuffer, 0, &box, _data.GetPtr(), 0, 0);
}

void Graphics::CopyBuffer(
    const AnyBufferHandle _dst,
    const uint32_t        _dstOffset,
    const AnyBufferHandle _src,
    const uint32_t        _srcOffset,
    const uint32_t        _byteWidth)
{
    JUG_ASSERT(_dst && _src, "Invalid buffer handle.");

    ID3D11Buffer* pDst = GetD3d11Buffer_(_dst);
    ID3D11Buffer* pSrc = GetD3d11Buffer_(_src);

    const uint32_t srcByteWidth = GetBufferByteWidth_(_src);
    const uint32_t copySize     = _byteWidth == kWholeSize ? srcByteWidth - _srcOffset : _byteWidth;

    JUG_ASSERT(copySize > 0, "Buffer copy size must be non-zero.");
    JUG_ASSERT(_srcOffset + copySize <= srcByteWidth, "Buffer copy source range is out of bounds.");
    JUG_ASSERT(_dstOffset + copySize <= GetBufferByteWidth_(_dst), "Buffer copy destination range is out of bounds.");

    D3D11_BOX box = {};
    box.left      = _srcOffset;
    box.right     = _srcOffset + copySize;
    box.top       = 0;
    box.bottom    = 1;
    box.front     = 0;
    box.back      = 1;

    m_pD3d11DeviceContext->CopySubresourceRegion(pDst, 0, _dstOffset, 0, 0, pSrc, 0, &box);
}

// ===========================================================================
//  Texture
//   [AI] 전부 재작성. 기존 코드는 정의조차 없는 TextureCreateParam 을 파라미터로 받고 있었고,
//        생성 실패마다 kNullHandle 을 돌려주는 방어적 분기가 깔려 있었다.
//        파라미터는 TextureDesc 로 통일했다. TextureD3D11 이 어차피 이걸 상속하므로 중간 구조체가 필요 없다.
// ===========================================================================

namespace
{
    [[nodiscard]] UINT MakeBindFlags_(
        const Flags<eTextureOption> _flags,
        const bool                  _bDepth)
    {
        // 리드백 전용 텍스처는 어디에도 바인딩되지 않는다.
        if (_flags.Has(eTextureOption::Readback))
        {
            return 0;
        }

        UINT bindFlags = 0;

        if (!_flags.Has(eTextureOption::ShaderWriteOnly))
        {
            bindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }
        if (_flags.Has(eTextureOption::RenderTarget) && !_bDepth)
        {
            bindFlags |= D3D11_BIND_RENDER_TARGET;
        }
        if (_flags.Has(eTextureOption::DepthStencil) || _bDepth)
        {
            bindFlags |= D3D11_BIND_DEPTH_STENCIL;
        }
        if (_flags.Has(eTextureOption::ShaderReadWrite))
        {
            bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        return bindFlags;
    }
}   // namespace

// 초기 데이터 배열은 CalcTextureIndex(mip, layer, numMips) 순서로 온다.
Vector<D3D11_SUBRESOURCE_DATA> Graphics::MakeInitData_(
    const TextureDesc&           _desc,
    const Span<const MemoryView> _initData) const
{
    Vector<D3D11_SUBRESOURCE_DATA> subresources;
    if (_initData.empty())
    {
        return subresources;
    }

    const uint32_t numSlices = _desc.numLayers * _desc.numMips;
    JUG_ASSERT(_initData.size() == numSlices, "Initial data count must be numLayers * numMips.");

    const int bitPerPixel = GetBitPerPixel(_desc.format);

    subresources.reserve(numSlices);
    for (uint32_t layer = 0; layer < _desc.numLayers; ++layer)
    {
        for (uint32_t mip = 0; mip < _desc.numMips; ++mip)
        {
            const VECTOR3I size = CalcTextureSize(
                static_cast<int>(_desc.width),
                static_cast<int>(_desc.height),
                static_cast<int>(_desc.depth),
                static_cast<int>(mip));

            const UINT rowPitch   = static_cast<UINT>(size.x * bitPerPixel / 8);
            const UINT slicePitch = rowPitch * static_cast<UINT>(size.y);
            const int  index      = CalcTextureIndex(static_cast<int>(mip), static_cast<int>(layer), static_cast<int>(_desc.numMips));

            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem                = _initData[static_cast<size_t>(index)].GetPtr();
            data.SysMemPitch            = rowPitch;
            data.SysMemSlicePitch       = slicePitch;
            subresources.push_back(data);
        }
    }

    return subresources;
}

void Graphics::CreateTextureViews_(
    TextureD3D11& _texture)
{
    const TextureFormatInfo info      = GetTextureFormatInfo_(_texture.format);
    const bool              bMsaa     = _texture.msaa != eMSAA::None;
    const UINT              bindFlags = MakeBindFlags_(_texture.flags, IsDepthFormat(_texture.format));

    if ((bindFlags & D3D11_BIND_SHADER_RESOURCE) != 0)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format                          = info.srv;

        switch (_texture.type)
        {
            case eTexture::Texture2D:
                if (bMsaa)
                {
                    desc.ViewDimension              = _texture.numLayers > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
                    desc.Texture2DMSArray.ArraySize = _texture.numLayers;
                }
                else if (_texture.numLayers > 1)
                {
                    desc.ViewDimension            = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    desc.Texture2DArray.MipLevels = _texture.numMips;
                    desc.Texture2DArray.ArraySize = _texture.numLayers;
                }
                else
                {
                    desc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
                    desc.Texture2D.MipLevels = _texture.numMips;
                }
                break;

            case eTexture::TextureCube:
                if (_texture.numLayers > 6)
                {
                    desc.ViewDimension              = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                    desc.TextureCubeArray.MipLevels = _texture.numMips;
                    desc.TextureCubeArray.NumCubes  = _texture.numLayers / 6;
                }
                else
                {
                    desc.ViewDimension         = D3D11_SRV_DIMENSION_TEXTURECUBE;
                    desc.TextureCube.MipLevels = _texture.numMips;
                }
                break;

            case eTexture::Texture3D:
                desc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE3D;
                desc.Texture3D.MipLevels = _texture.numMips;
                break;

            default:
                JUG_ASSERT(false, "Unknown texture type.");
                return;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateShaderResourceView(_texture.pResource, &desc, &_texture.pSRV));
    }

    if ((bindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format                           = info.srv;

        if (_texture.type == eTexture::Texture3D)
        {
            desc.ViewDimension   = D3D11_UAV_DIMENSION_TEXTURE3D;
            desc.Texture3D.WSize = _texture.depth;
        }
        else if (_texture.numLayers > 1)
        {
            desc.ViewDimension            = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.ArraySize = _texture.numLayers;
        }
        else
        {
            desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateUnorderedAccessView(_texture.pResource, &desc, &_texture.pUAV));
    }
}

TextureHandle Graphics::CreateTextureInternal_(
    const TextureDesc&           _desc,
    const Span<const MemoryView> _initDataOrEmpty)
{
    JUG_ASSERT(_desc.width > 0 && _desc.height > 0 && _desc.depth > 0, "A texture must have a non-zero size.");
    JUG_ASSERT(_desc.numLayers > 0 && _desc.numMips > 0, "A texture must have at least one layer and one mip.");

    const bool bDepth    = IsDepthFormat(_desc.format);
    const bool bMsaa     = _desc.msaa != eMSAA::None;
    const bool bReadback = _desc.flags.Has(eTextureOption::Readback);
    const UINT bindFlags = MakeBindFlags_(_desc.flags, bDepth);

    JUG_ASSERT(!bMsaa || _desc.numMips == 1, "An MSAA texture cannot have mip levels.");
    JUG_ASSERT(!bMsaa || _initDataOrEmpty.empty(), "An MSAA texture cannot be initialized with data.");

    const DXGI_FORMAT      resourceFormat = MakeResourceFormat_(_desc.format);
    const DXGI_SAMPLE_DESC sampleDesc     = MakeSampleDesc_(m_pD3d11Device, resourceFormat, _desc.msaa);

    const Vector<D3D11_SUBRESOURCE_DATA> subresources = MakeInitData_(_desc, _initDataOrEmpty);
    const D3D11_SUBRESOURCE_DATA*        pInit        = subresources.empty() ? nullptr : subresources.data();

    // 초기 데이터가 있고 이후 아무도 손대지 않는 텍스처만 IMMUTABLE 이다.
    // IMMUTABLE + pInitialData == nullptr 은 무조건 E_INVALIDARG 다.
    const bool bImmutable = pInit
                         && !bReadback
                         && (bindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS)) == 0
                         && _desc.numMips == 1;

    D3D11_USAGE usage          = D3D11_USAGE_DEFAULT;
    UINT        cpuAccessFlags = 0;
    if (bReadback)
    {
        usage          = D3D11_USAGE_STAGING;
        cpuAccessFlags = D3D11_CPU_ACCESS_READ;
    }
    else if (bImmutable)
    {
        usage = D3D11_USAGE_IMMUTABLE;
    }

    UINT miscFlags = 0;
    if (_desc.type == eTexture::TextureCube)
    {
        miscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    }
    // GenerateMips 는 RTV 바인딩과 MISC 플래그를 둘 다 요구한다.
    if (_desc.numMips > 1 && (bindFlags & D3D11_BIND_RENDER_TARGET) != 0 && (bindFlags & D3D11_BIND_SHADER_RESOURCE) != 0)
    {
        miscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }

    ID3D11Resource* pResource = nullptr;

    if (_desc.type == eTexture::Texture3D)
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width                = _desc.width;
        desc.Height               = _desc.height;
        desc.Depth                = _desc.depth;
        desc.MipLevels            = _desc.numMips;
        desc.Format               = resourceFormat;
        desc.Usage                = usage;
        desc.BindFlags            = bindFlags;
        desc.CPUAccessFlags       = cpuAccessFlags;
        desc.MiscFlags            = miscFlags;

        ID3D11Texture3D* pTexture3D = nullptr;
        JUG_DX_CHECK(m_pD3d11Device->CreateTexture3D(&desc, pInit, &pTexture3D));
        pResource = pTexture3D;
    }
    else
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width                = _desc.width;
        desc.Height               = _desc.height;
        desc.MipLevels            = _desc.numMips;
        desc.ArraySize            = _desc.numLayers;
        desc.Format               = resourceFormat;
        desc.SampleDesc           = sampleDesc;
        desc.Usage                = usage;
        desc.BindFlags            = bindFlags;
        desc.CPUAccessFlags       = cpuAccessFlags;
        desc.MiscFlags            = miscFlags;

        ID3D11Texture2D* pTexture2D = nullptr;
        JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&desc, pInit, &pTexture2D));
        pResource = pTexture2D;
    }

    const TextureHandle texh    = m_texturePool.Emplace();
    TextureD3D11&       texture = m_texturePool.Get(texh);

    static_cast<TextureDesc&>(texture) = _desc;
    texture.refCount                   = 1;
    texture.bImmutable                 = bImmutable;
    texture.pResource                  = pResource;

    CreateTextureViews_(texture);
    return texh;
}

void Graphics::UpdateTextureInternal_(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _z,
    const uint32_t      _widthOrAll,
    const uint32_t      _heightOrAll,
    const uint32_t      _depthOrAll,
    const MemoryView    _data,
    const uint32_t      _rowPitch,
    const uint32_t      _depthPitch)
{
    const TextureD3D11& texture = m_texturePool.Get(_texh);
    JUG_ASSERT(!texture.bImmutable, "An immutable texture cannot be updated.");
    JUG_ASSERT(!_data.IsEmpty(), "UpdateTexture requires non-empty data.");
    JUG_ASSERT(_mip < texture.numMips, "Mip level is out of range.");
    JUG_ASSERT(_layer < texture.numLayers, "Array layer is out of range.");

    const VECTOR3I mipSize = CalcTextureSize(
        static_cast<int>(texture.width),
        static_cast<int>(texture.height),
        static_cast<int>(texture.depth),
        static_cast<int>(_mip));

    const uint32_t width  = _widthOrAll == kWholeSize ? static_cast<uint32_t>(mipSize.x) : _widthOrAll;
    const uint32_t height = _heightOrAll == kWholeSize ? static_cast<uint32_t>(mipSize.y) : _heightOrAll;
    const uint32_t depth  = _depthOrAll == kWholeSize ? static_cast<uint32_t>(mipSize.z) : _depthOrAll;

    JUG_ASSERT(_x + width <= static_cast<uint32_t>(mipSize.x)
                   && _y + height <= static_cast<uint32_t>(mipSize.y)
                   && _z + depth <= static_cast<uint32_t>(mipSize.z),
               "Texture update region is out of bounds.");

    D3D11_BOX box = {};
    box.left      = _x;
    box.right     = _x + width;
    box.top       = _y;
    box.bottom    = _y + height;
    box.front     = _z;
    box.back      = _z + depth;

    const UINT subresource = D3D11CalcSubresource(_mip, _layer, texture.numMips);
    m_pD3d11DeviceContext->UpdateSubresource(texture.pResource, subresource, &box, _data.GetPtr(), _rowPitch, _depthPitch);
}

// ===========================================
//  Create
// ===========================================

TextureHandle Graphics::CreateTexture2D(
    const uint32_t               _width,
    const uint32_t               _height,
    const eTextureFormat         _format,
    const bool                   _bHasMips,
    const uint32_t               _numLayers,
    const eMSAA                  _msaa,
    const Flags<eTextureOption>  _flags,
    const Span<const MemoryView> _initDataOrEmpty)
{
    TextureDesc desc = {};
    desc.width       = _width;
    desc.height      = _height;
    desc.depth       = 1;
    desc.type        = eTexture::Texture2D;
    desc.format      = _format;
    desc.numLayers   = _numLayers;
    desc.numMips     = _bHasMips ? static_cast<uint32_t>(CalcNumMips(static_cast<int>(_width), static_cast<int>(_height), 1)) : 1;
    desc.msaa        = _msaa;
    desc.flags       = _flags;

    return CreateTextureInternal_(desc, _initDataOrEmpty);
}

TextureHandle Graphics::CreateTextureCube(
    const uint32_t               _width,
    const uint32_t               _height,
    const eTextureFormat         _format,
    const bool                   _bHasMips,
    const uint32_t               _numCubes,
    const Flags<eTextureOption>  _flags,
    const Span<const MemoryView> _initDataOrEmpty)
{
    JUG_ASSERT(_numCubes > 0, "A cube texture requires at least one cube.");

    TextureDesc desc = {};
    desc.width       = _width;
    desc.height      = _height;
    desc.depth       = 1;
    desc.type        = eTexture::TextureCube;
    desc.format      = _format;
    desc.numLayers   = _numCubes * 6;
    desc.numMips     = _bHasMips ? static_cast<uint32_t>(CalcNumMips(static_cast<int>(_width), static_cast<int>(_height), 1)) : 1;
    desc.msaa        = eMSAA::None;
    desc.flags       = _flags;

    return CreateTextureInternal_(desc, _initDataOrEmpty);
}

TextureHandle Graphics::CreateTexture3D(
    const uint32_t               _width,
    const uint32_t               _height,
    const uint32_t               _depth,
    const eTextureFormat         _format,
    const bool                   _bHasMips,
    const Flags<eTextureOption>  _flags,
    const Span<const MemoryView> _initDataOrEmpty)
{
    TextureDesc desc = {};
    desc.width       = _width;
    desc.height      = _height;
    desc.depth       = _depth;
    desc.type        = eTexture::Texture3D;
    desc.format      = _format;
    desc.numLayers   = 1;
    desc.numMips     = _bHasMips ? static_cast<uint32_t>(CalcNumMips(static_cast<int>(_width), static_cast<int>(_height), static_cast<int>(_depth))) : 1;
    desc.msaa        = eMSAA::None;
    desc.flags       = _flags;

    return CreateTextureInternal_(desc, _initDataOrEmpty);
}

// ===========================================
//  Update
// ===========================================

void Graphics::UpdateTexture2D(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _width,
    const uint32_t      _height,
    const MemoryView    _data,
    const uint32_t      _rowPitch)
{
    JUG_ASSERT(m_texturePool.Get(_texh).type == eTexture::Texture2D, "UpdateTexture2D requires a 2D texture.");
    UpdateTextureInternal_(_texh, _mip, _layer, _x, _y, 0, _width, _height, 1, _data, _rowPitch, 0);
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
    const MemoryView    _data,
    const uint32_t      _rowPitch)
{
    JUG_ASSERT(m_texturePool.Get(_texh).type == eTexture::TextureCube, "UpdateTextureCube requires a cube texture.");

    // 큐브는 레이어 하나가 면 6 개다. 배열 인덱스로 펼친다.
    const uint32_t arrayLayer = _layer * 6 + static_cast<uint32_t>(_face);
    UpdateTextureInternal_(_texh, _mip, arrayLayer, _x, _y, 0, _width, _height, 1, _data, _rowPitch, 0);
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
    const MemoryView    _data,
    const uint32_t      _rowPitch,
    const uint32_t      _depthPitch)
{
    JUG_ASSERT(m_texturePool.Get(_texh).type == eTexture::Texture3D, "UpdateTexture3D requires a 3D texture.");
    UpdateTextureInternal_(_texh, _mip, 0, _x, _y, _z, _width, _height, _depth, _data, _rowPitch, _depthPitch);
}

// ===========================================
//  Copy & Read
// ===========================================

void Graphics::CopyTexture(
    const Subresource _dst,
    const uint32_t    _dstX,
    const uint32_t    _dstY,
    const uint32_t    _dstZ,
    const Subresource _src,
    const uint32_t    _srcX,
    const uint32_t    _srcY,
    const uint32_t    _srcZ,
    const uint32_t    _width,
    const uint32_t    _height,
    const uint32_t    _depth)
{
    const TextureD3D11& dstTexture = m_texturePool.Get(_dst.texh);
    const TextureD3D11& srcTexture = m_texturePool.Get(_src.texh);

    JUG_ASSERT(_dst.mip < dstTexture.numMips && _dst.layer < dstTexture.numLayers, "Destination subresource is out of range.");
    JUG_ASSERT(_src.mip < srcTexture.numMips && _src.layer < srcTexture.numLayers, "Source subresource is out of range.");
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "Texture copy region must be non-empty.");

    D3D11_BOX box = {};
    box.left      = _srcX;
    box.right     = _srcX + _width;
    box.top       = _srcY;
    box.bottom    = _srcY + _height;
    box.front     = _srcZ;
    box.back      = _srcZ + _depth;

    m_pD3d11DeviceContext->CopySubresourceRegion(
        dstTexture.pResource,
        D3D11CalcSubresource(_dst.mip, _dst.layer, dstTexture.numMips),
        _dstX,
        _dstY,
        _dstZ,
        srcTexture.pResource,
        D3D11CalcSubresource(_src.mip, _src.layer, srcTexture.numMips),
        &box);
}

size_t Graphics::ReadTexture(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    MutableMemoryView   _dst)
{
    const TextureD3D11& texture = m_texturePool.Get(_texh);
    JUG_ASSERT(_mip < texture.numMips && _layer < texture.numLayers, "Texture subresource is out of range.");
    JUG_ASSERT(!_dst.IsEmpty(), "ReadTexture requires a non-empty destination.");

    const VECTOR3I mipSize = CalcTextureSize(
        static_cast<int>(texture.width),
        static_cast<int>(texture.height),
        static_cast<int>(texture.depth),
        static_cast<int>(_mip));

    const int      bitPerPixel = GetBitPerPixel(texture.format);
    const uint32_t rowPitch    = static_cast<uint32_t>(mipSize.x * bitPerPixel / 8);
    const uint32_t byteWidth   = rowPitch * static_cast<uint32_t>(mipSize.y) * static_cast<uint32_t>(mipSize.z);

    // 스테이징 텍스처 하나를 만들어 해당 서브리소스만 복사한 뒤 읽는다.
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width                = static_cast<UINT>(mipSize.x);
    stagingDesc.Height               = static_cast<UINT>(mipSize.y);
    stagingDesc.MipLevels            = 1;
    stagingDesc.ArraySize            = 1;
    stagingDesc.Format               = MakeResourceFormat_(texture.format);
    stagingDesc.SampleDesc.Count     = 1;
    stagingDesc.Usage                = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags       = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* pStaging = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&stagingDesc, nullptr, &pStaging));

    m_pD3d11DeviceContext->CopySubresourceRegion(
        pStaging,
        0,
        0,
        0,
        0,
        texture.pResource,
        D3D11CalcSubresource(_mip, _layer, texture.numMips),
        nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    JUG_DX_CHECK(m_pD3d11DeviceContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped));

    const size_t readSize = Min(_dst.GetSize(), static_cast<size_t>(byteWidth));

    // D3D 가 돌려주는 RowPitch 는 우리 계산값보다 클 수 있다. 줄 단위로 옮긴다.
    auto*       pDstBytes = reinterpret_cast<uint8_t*>(_dst.GetPtr());
    const auto* pSrcBytes = static_cast<const uint8_t*>(mapped.pData);

    size_t written = 0;
    for (int y = 0; y < mipSize.y && written < readSize; ++y)
    {
        const size_t copySize = Min(static_cast<size_t>(rowPitch), readSize - written);
        std::memcpy(pDstBytes + written, pSrcBytes + static_cast<size_t>(y) * mapped.RowPitch, copySize);
        written += copySize;
    }

    m_pD3d11DeviceContext->Unmap(pStaging, 0);
    JUG_DX_RELEASE(pStaging);
    return written;
}

// ===========================================================================
//  FrameBuffer & SwapChain & Present & Clear
//   [AI] 전부 재작성. 기존 코드는 정의 없는 SwapChainDesc 를 쓰고, 스왑체인을 m_pDxgiFactory 로 직접 만들고,
//        m_primaryFbh / bNeedPresent / bTearing 처럼 헤더에서 사라진 멤버를 참조했다.
//        스왑체인 생성은 DXGI 클래스에 위임하고, tearing 여부는 dxgiFlags 에서 그때그때 뽑는다.
//        Present 는 스왑체인 프레임버퍼를 전부 내보낸다(단순함 우선).
// ===========================================================================

namespace
{
    [[nodiscard]] bool IsRenderTargetAttachment_(
        const eTextureFormat _format)
    {
        return !IsDepthFormat(_format);
    }
}   // namespace

// RTV/DSV 는 MSAA 리소스가 있으면 그쪽에 건다. 프레임 끝에서 단일 샘플 리소스로 resolve 된다.
ID3D11Resource* Graphics::GetViewTarget_(
    const TextureD3D11& _texture) const
{
    return _texture.pMsaaRtResource ? _texture.pMsaaRtResource : _texture.pResource;
}

void Graphics::CreateFrameBufferViews_(
    FrameBufferD3D11& _frameBuffer)
{
    const uint32_t numAttachments = _frameBuffer.numRts + (_frameBuffer.bHasDepthStencil ? 1 : 0);

    for (uint32_t i = 0; i < numAttachments; ++i)
    {
        const Attachment&   attachment = _frameBuffer.attachments[i];
        const TextureD3D11& texture    = m_texturePool.Get(attachment.texh);

        ID3D11Resource*         pTarget = GetViewTarget_(texture);
        const TextureFormatInfo info    = GetTextureFormatInfo_(texture.format);
        const bool              bMsaa   = texture.msaa != eMSAA::None;

        if (IsDepthFormat(texture.format))
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
            desc.Format                        = info.dsv;

            if (bMsaa)
            {
                desc.ViewDimension                    = texture.numLayers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS;
                desc.Texture2DMSArray.FirstArraySlice = attachment.offset;
                desc.Texture2DMSArray.ArraySize       = attachment.numLayers;
            }
            else if (texture.numLayers > 1)
            {
                desc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice        = attachment.mip;
                desc.Texture2DArray.FirstArraySlice = attachment.offset;
                desc.Texture2DArray.ArraySize       = attachment.numLayers;
            }
            else
            {
                desc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = attachment.mip;
            }

            JUG_DX_CHECK(m_pD3d11Device->CreateDepthStencilView(pTarget, &desc, &_frameBuffer.pDSV));
            continue;
        }

        D3D11_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format                        = info.rtv;

        switch (texture.type)
        {
            case eTexture::Texture3D:
                desc.ViewDimension         = D3D11_RTV_DIMENSION_TEXTURE3D;
                desc.Texture3D.MipSlice    = attachment.mip;
                desc.Texture3D.FirstWSlice = attachment.offset;
                desc.Texture3D.WSize       = attachment.numLayers;
                break;

            default:
                if (bMsaa)
                {
                    desc.ViewDimension                    = texture.numLayers > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
                    desc.Texture2DMSArray.FirstArraySlice = attachment.offset;
                    desc.Texture2DMSArray.ArraySize       = attachment.numLayers;
                }
                else if (texture.numLayers > 1)
                {
                    desc.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                    desc.Texture2DArray.MipSlice        = attachment.mip;
                    desc.Texture2DArray.FirstArraySlice = attachment.offset;
                    desc.Texture2DArray.ArraySize       = attachment.numLayers;
                }
                else
                {
                    desc.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
                    desc.Texture2D.MipSlice = attachment.mip;
                }
                break;
        }

        JUG_DX_CHECK(m_pD3d11Device->CreateRenderTargetView(pTarget, &desc, &_frameBuffer.rtvs[i]));
    }
}

// 어태치먼트를 정렬한다. 렌더 타겟이 앞, 깊이-스텐실이 마지막 한 칸.
void Graphics::FillAttachments_(
    FrameBufferD3D11&            _frameBuffer,
    const Span<const Attachment> _attachments) const
{
    uint32_t numRts = 0;

    for (const Attachment& attachment: _attachments)
    {
        const TextureD3D11& texture = m_texturePool.Get(attachment.texh);

        if (texture.msaa != eMSAA::None)
        {
            _frameBuffer.bMSAA = true;
        }

        if (IsRenderTargetAttachment_(texture.format))
        {
            JUG_ASSERT(numRts < kNumMaxRenderTargetSlots, "Too many render target attachments.");
            _frameBuffer.attachments[numRts] = attachment;
            ++numRts;
            continue;
        }

        JUG_ASSERT(!_frameBuffer.bHasDepthStencil, "A frame buffer can have only one depth stencil attachment.");
        _frameBuffer.bHasDepthStencil = true;
    }

    _frameBuffer.numRts = numRts;

    // 깊이-스텐실은 렌더 타겟 뒤에 놓는다.
    if (_frameBuffer.bHasDepthStencil)
    {
        for (const Attachment& attachment: _attachments)
        {
            if (!IsRenderTargetAttachment_(m_texturePool.Get(attachment.texh).format))
            {
                _frameBuffer.attachments[numRts] = attachment;
                break;
            }
        }
    }
}

// kNullHandle 은 "지금 바인딩된 프레임버퍼" 로 읽는다. Clear / GetDesc 가 매번 핸들을 들고 다니지 않아도 된다.
FrameBufferHandle Graphics::ResolveFrameBufferHandle_(
    const FrameBufferHandle _fbh) const
{
    return _fbh ? _fbh : m_fbh;
}

UINT Graphics::MakeSwapChainFlags_() const
{
    UINT flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (m_caps.bAllowTearing)
    {
        flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    return flags;
}

// 백버퍼 / MSAA 렌더 타겟과 그 뷰를 만든다.
// 리사이즈 때도 같은 풀 슬롯을 다시 채우므로 텍스처 핸들은 그대로 유지된다.
void Graphics::CreateSwapChainTargets_(
    FrameBufferD3D11& _frameBuffer,
    const uint32_t    _width,
    const uint32_t    _height)
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    JUG_DX_CHECK(_frameBuffer.pDxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)));

    TextureD3D11& colorTexture = m_texturePool.Get(_frameBuffer.attachments[0].texh);
    colorTexture.width         = _width;
    colorTexture.height        = _height;
    colorTexture.depth         = 1;
    colorTexture.numLayers     = 1;
    colorTexture.numMips       = 1;
    colorTexture.type          = eTexture::Texture2D;
    colorTexture.refCount      = 1;
    colorTexture.flags         = eTextureOption::RenderTarget;
    colorTexture.pResource     = pBackBuffer;

    // 플립 모델은 SampleDesc.Count 가 1 이어야 한다. MSAA 는 별도 리소스에 그린 뒤 백버퍼로 resolve 한다.
    // sRGB RTV 를 씌우려면 이 리소스는 TYPELESS 여야 한다. (백버퍼는 DXGI 가 예외적으로 허용해 준다)
    if (colorTexture.msaa != eMSAA::None)
    {
        const TextureFormatInfo info          = GetTextureFormatInfo_(colorTexture.format);
        const DXGI_FORMAT       resolveFormat = MakeBackBufferFormat_(colorTexture.format);
        const DXGI_SAMPLE_DESC  sampleDesc    = MakeSampleDesc_(m_pD3d11Device, resolveFormat, colorTexture.msaa);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width                = _width;
        desc.Height               = _height;
        desc.MipLevels            = 1;
        desc.ArraySize            = 1;
        desc.Format               = info.typeless;
        desc.SampleDesc           = sampleDesc;
        desc.Usage                = D3D11_USAGE_DEFAULT;
        desc.BindFlags            = D3D11_BIND_RENDER_TARGET;

        ID3D11Texture2D* pMsaaRt = nullptr;
        JUG_DX_CHECK(m_pD3d11Device->CreateTexture2D(&desc, nullptr, &pMsaaRt));
        colorTexture.pMsaaRtResource = pMsaaRt;
    }

    CreateFrameBufferViews_(_frameBuffer);
}

void Graphics::ReleaseSwapChainTargets_(
    FrameBufferD3D11& _frameBuffer)
{
    ReleaseFrameBufferViews_(_frameBuffer);

    TextureD3D11& colorTexture = m_texturePool.Get(_frameBuffer.attachments[0].texh);
    JUG_DX_RELEASE(colorTexture.pMsaaRtResource);

    // 백버퍼는 GetBuffer 로 얻은 참조다. ResizeBuffers 전에 반드시 놓아야 한다.
    JUG_DX_RELEASE(colorTexture.pResource);
}

void Graphics::PresentSwapChain_(
    FrameBufferD3D11& _frameBuffer)
{
    const UINT syncInterval = _frameBuffer.bVSync ? 1u : 0u;
    const bool bTearing     = (_frameBuffer.dxgiFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;

    // ALLOW_TEARING 으로 만든 스왑체인만 tearing present 를 받는다. vsync 가 켜져 있으면 금지된다.
    const UINT presentFlags = (syncInterval == 0 && bTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    JUG_DX_CHECK(_frameBuffer.pDxgiSwapChain->Present(syncInterval, presentFlags));

    // 플립 모델은 Present 후 백버퍼 RTV 를 떼어낸다. 반드시 다시 바인딩해야 한다.
    m_dirtyFlags |= ePipelineDirty::FrameBuffer;
}

// ===========================================
//  Frame Buffer
// ===========================================

FrameBufferHandle Graphics::CreateFrameBuffer(
    const Span<const Attachment> _attachments,
    const bool                   _bOwnership)
{
    JUG_ASSERT(!_attachments.empty(), "A frame buffer requires at least one attachment.");

    const FrameBufferHandle fbh         = m_frameBufferPool.Emplace();
    FrameBufferD3D11&       frameBuffer = m_frameBufferPool.Get(fbh);
    frameBuffer.bOwnership              = _bOwnership;

    FillAttachments_(frameBuffer, _attachments);
    CreateFrameBufferViews_(frameBuffer);

    // 소유권을 넘기지 않으면 프레임버퍼가 참조를 하나 더 든다.
    if (!_bOwnership)
    {
        const uint32_t numAttachments = frameBuffer.numRts + (frameBuffer.bHasDepthStencil ? 1 : 0);
        for (uint32_t i = 0; i < numAttachments; ++i)
        {
            ++m_texturePool.Get(frameBuffer.attachments[i].texh).refCount;
        }
    }

    return fbh;
}

FrameBufferHandle Graphics::CreateFrameBuffer(
    const TextureHandle _texh,
    const bool          _bOwnership)
{
    const ARRAY<Attachment, 1> attachments {
        Attachment { _texh, 0, 0, 1 }
    };
    return CreateFrameBuffer(Span<const Attachment> { attachments }, _bOwnership);
}

FrameBufferHandle Graphics::CreateFrameBuffer(
    void* const          _pWindow,
    const uint32_t       _width,
    const uint32_t       _height,
    const eTextureFormat _format,
    const eMSAA          _msaa)
{
    JUG_ASSERT(_pWindow, "A window frame buffer requires a native window handle.");
    JUG_ASSERT(_width > 0 && _height > 0, "A window frame buffer requires a non-zero size.");
    JUG_ASSERT(!IsDepthFormat(_format), "A swap chain requires a color format.");

    const UINT dxgiFlags = MakeSwapChainFlags_();

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width                 = _width;
    desc.Height                = _height;

    // 플립 모델 스왑체인은 _SRGB 백버퍼 포맷을 거부한다. 감마 변환은 RTV 에 sRGB 포맷을 씌워서 처리한다.
    desc.Format             = MakeBackBufferFormat_(_format);
    desc.Stereo             = FALSE;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount        = kNumBackBuffers;
    desc.Scaling            = DXGI_SCALING_NONE;
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags              = dxgiFlags;

    ISwapChain* pSwapChain = m_dxgi.CreateSwapChain(m_pD3d11Device, _pWindow, true, desc);

    // 이걸 하지 않으면 DXGI 가 Alt+Enter 를 가로채 독점 전체화면으로 바꿔 버린다.
    JUG_DISCARD_RETURN(m_dxgi.GetFactory()->MakeWindowAssociation(static_cast<HWND>(_pWindow), DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));

    // 색상 텍스처 슬롯을 먼저 잡아 둔다. 리사이즈해도 이 핸들은 바뀌지 않는다.
    const TextureHandle texh    = m_texturePool.Emplace();
    TextureD3D11&       texture = m_texturePool.Get(texh);
    texture.format              = _format;
    texture.msaa                = _msaa;

    const FrameBufferHandle fbh         = m_frameBufferPool.Emplace();
    FrameBufferD3D11&       frameBuffer = m_frameBufferPool.Get(fbh);
    frameBuffer.bOwnership              = true;
    frameBuffer.pDxgiSwapChain          = pSwapChain;
    frameBuffer.dxgiFlags               = dxgiFlags;
    frameBuffer.bVSync                  = true;
    frameBuffer.numRts                  = 1;
    frameBuffer.bHasDepthStencil        = false;
    frameBuffer.bMSAA                   = _msaa != eMSAA::None;
    frameBuffer.pWindow                 = _pWindow;
    frameBuffer.attachments[0]          = Attachment { texh, 0, 0, 1 };

    CreateSwapChainTargets_(frameBuffer, _width, _height);

    JUG_CORE_LOG_INFO("Swap chain created. ({}x{}, buffers = {}, msaa = {}, tearing = {})",
                      _width,
                      _height,
                      kNumBackBuffers,
                      NameOf(_msaa),
                      (dxgiFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0);

    return fbh;
}

void Graphics::ResizeFrameBuffer(
    const FrameBufferHandle _fbh,
    const uint32_t          _width,
    const uint32_t          _height)
{
    FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(_fbh);
    JUG_ASSERT(frameBuffer.pDxgiSwapChain, "ResizeFrameBuffer requires a window frame buffer.");

    // 최소화하면 WM_SIZE 가 0x0 으로 온다. ResizeBuffers 는 그 크기를 거부한다.
    if (_width == 0 || _height == 0)
    {
        return;
    }

    const TextureD3D11& colorTexture = m_texturePool.Get(frameBuffer.attachments[0].texh);
    if (colorTexture.width == _width && colorTexture.height == _height)
    {
        return;
    }

    // 컨텍스트가 백버퍼 RTV 를 붙들고 있으면 ResizeBuffers 가 실패한다.
    m_pD3d11DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    UnbindFrameBuffer_(_fbh);
    ReleaseSwapChainTargets_(frameBuffer);

#ifdef JUG_DEBUG
    // 백버퍼 참조가 하나라도 남아 있으면 ResizeBuffers 는 조용히 실패한다. 여기서 잡는다.
    {
        ID3D11Texture2D* pProbe = nullptr;
        if (SUCCEEDED(frameBuffer.pDxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(&pProbe))))
        {
            const ULONG refCount = pProbe->Release();
            JUG_ASSERT(refCount == 0, "The swap chain back buffer is still referenced. ResizeBuffers will fail.");
        }
    }
#endif

    // 생성 플래그를 그대로 넘겨야 한다. 0 을 넘기면 ALLOW_TEARING 이 사라져 이후 present 가 실패한다.
    JUG_DX_CHECK(frameBuffer.pDxgiSwapChain->ResizeBuffers(0, _width, _height, DXGI_FORMAT_UNKNOWN, frameBuffer.dxgiFlags));

    CreateSwapChainTargets_(frameBuffer, _width, _height);

    m_dirtyFlags |= { ePipelineDirty::FrameBuffer, ePipelineDirty::Viewport, ePipelineDirty::ScissorRect };
}

void Graphics::SetVSync(
    const FrameBufferHandle _fbh,
    const bool              _bVSync)
{
    m_frameBufferPool.Get(_fbh).bVSync = _bVSync;
}

// ===========================================
//  Present
// ===========================================

void Graphics::Present()
{
    // 아직 바인딩된 프레임버퍼가 있으면 present 전에 MSAA 를 내리고 밉을 갱신한다.
    // 순서가 바뀌면 resolve 결과가 이번 프레임 화면에 나가지 않는다.
    if (m_lastFbh)
    {
        ResolveFrameBuffer_(m_lastFbh);
    }

    // 스왑체인 프레임버퍼는 전부 내보낸다.
    for (FrameBufferD3D11& frameBuffer: m_frameBufferPool.GetResources())
    {
        if (frameBuffer.pDxgiSwapChain == nullptr)
        {
            continue;
        }
        PresentSwapChain_(frameBuffer);
    }

    // [AI] GPU 프레임 타이밍. 링의 back 이 지금 열려 있는 쿼리, front 가 가장 오래된 쿼리다.
    //      kNumInitTimerQueries 프레임 뒤에 회수하므로 GetData 가 스톨하지 않는다.
    {
        TimerQuery& open = m_timerQueries.Back();
        if (open.bIssued)
        {
            m_pD3d11DeviceContext->End(open.pEnd);
            m_pD3d11DeviceContext->End(open.pDisjoint);
        }

        TimerQuery query = m_timerQueries.Front();
        m_timerQueries.Pop();

        if (query.bIssued)
        {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
            if (m_pD3d11DeviceContext->GetData(query.pDisjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !disjoint.Disjoint)
            {
                UINT64 begin = 0;
                UINT64 end   = 0;
                if (m_pD3d11DeviceContext->GetData(query.pBegin, &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
                    && m_pD3d11DeviceContext->GetData(query.pEnd, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
                {
                    m_stats.gpuTimerBegin = static_cast<int64_t>(begin);
                    m_stats.gpuTimerEnd   = static_cast<int64_t>(end);
                    m_stats.gpuTimerFreq  = static_cast<int64_t>(disjoint.Frequency);
                }
            }
        }

        m_pD3d11DeviceContext->Begin(query.pDisjoint);
        m_pD3d11DeviceContext->End(query.pBegin);
        query.bIssued = true;
        m_timerQueries.Push(query);
    }

    if (m_caps.videoMemory > 0 && m_dxgi.GetAdapterOrNull())
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo = {};
        if (SUCCEEDED(m_dxgi.GetAdapterOrNull()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo)))
        {
            m_stats.gpuMemoryUsage = static_cast<int64_t>(memoryInfo.CurrentUsage);
            m_stats.gpuMaxMemory   = static_cast<int64_t>(memoryInfo.Budget);
        }
    }

    LogInfoQueueMessages_(m_pD3d11InfoQueueOrNull);

    m_lastStats = m_stats;
    m_stats     = {};
}

// ===========================================
//  Clear
// ===========================================

void Graphics::ClearRenderTarget(
    const FrameBufferHandle _fbh,
    const RGBA              _color,
    const int               _slot)
{
    const FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(ResolveFrameBufferHandle_(_fbh));
    JUG_ASSERT(_slot >= 0 && static_cast<uint32_t>(_slot) < frameBuffer.numRts, "Render target slot is out of range.");

    const VECTOR4 color = _color.ToLinear();
    m_pD3d11DeviceContext->ClearRenderTargetView(frameBuffer.rtvs[_slot], color.e.data());
}

void Graphics::ClearRenderTargets(
    const FrameBufferHandle _fbh,
    const RGBA              _color)
{
    const FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(ResolveFrameBufferHandle_(_fbh));
    const VECTOR4           color       = _color.ToLinear();

    for (uint32_t i = 0; i < frameBuffer.numRts; ++i)
    {
        m_pD3d11DeviceContext->ClearRenderTargetView(frameBuffer.rtvs[i], color.e.data());
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

    const FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(ResolveFrameBufferHandle_(_fbh));
    JUG_ASSERT(frameBuffer.pDSV, "The frame buffer has no depth stencil attachment.");

    UINT clearFlags = 0;
    if (_bClearDepth)
    {
        clearFlags |= D3D11_CLEAR_DEPTH;
    }
    if (_bClearStencil)
    {
        clearFlags |= D3D11_CLEAR_STENCIL;
    }

    m_pD3d11DeviceContext->ClearDepthStencilView(frameBuffer.pDSV, clearFlags, _depth, _stencil);
}

// ===========================================================================
//  VertexLayout & InputLayout & Shader & Program
//   [AI] 전부 재작성.
//        입력 레이아웃은 레거시 JamEngine 방식으로 바꿨다. 레이아웃에서 더미 VS 를 만들어 컴파일하고
//        그 시그니처로 CreateInputLayout 한다. 기존 코드는 ShaderD3D11 에 VS 바이트코드 사본을 통째로
//        들고 있었는데(셰이더마다 수 KB), 새 헤더에는 그 필드가 없고 애초에 들고 있을 이유도 없다.
//        캐시 키도 VS 해시를 빼고 (스트림별 레이아웃 해시 + 인스턴스 stride) 만 쓴다.
//        같은 레이아웃이면 어떤 VS 든 같은 입력 레이아웃을 재사용할 수 있어 캐시 적중률이 올라간다.
// ===========================================================================

namespace
{
    struct VertexSemantic
    {
        const char* pName = "";
        UINT        index = 0;
    };

    // eVertexAttribute -> HLSL 시맨틱. 순서는 eVertexAttribute 와 1:1 이다.
    constexpr ARRAY<VertexSemantic, CountOf<eVertexAttribute>()> kVertexSemanticTable {
        VertexSemantic {     "POSITION", 0 },
        VertexSemantic {       "NORMAL", 0 },
        VertexSemantic {      "TANGENT", 0 },
        VertexSemantic {     "BINORMAL", 0 },
        VertexSemantic {        "COLOR", 0 },
        VertexSemantic {        "COLOR", 1 },
        VertexSemantic {        "COLOR", 2 },
        VertexSemantic {        "COLOR", 3 },
        VertexSemantic {     "TEXCOORD", 0 },
        VertexSemantic {     "TEXCOORD", 1 },
        VertexSemantic {     "TEXCOORD", 2 },
        VertexSemantic {     "TEXCOORD", 3 },
        VertexSemantic { "BLENDINDICES", 0 },
        VertexSemantic {  "BLENDWEIGHT", 0 },
    };

    // 버텍스 속성은 항상 32 비트 성분이다(VertexLayout::Add 가 num * 4 로 고정한다).
    [[nodiscard]] DXGI_FORMAT ToDxgiVertexFormat_(
        const eVertexAttributeFormat _format,
        const int                    _num)
    {
        JUG_ASSERT(_num >= 1 && _num <= 4, "A vertex attribute must have 1 to 4 components.");

        constexpr ENUM_ARRAY<eVertexAttributeFormat, ARRAY<DXGI_FORMAT, 4>> kFormatTable {
            ARRAY<DXGI_FORMAT, 4> { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT },
            ARRAY<DXGI_FORMAT, 4> {  DXGI_FORMAT_R32_SINT,  DXGI_FORMAT_R32G32_SINT,  DXGI_FORMAT_R32G32B32_SINT,  DXGI_FORMAT_R32G32B32A32_SINT },
            ARRAY<DXGI_FORMAT, 4> {  DXGI_FORMAT_R32_UINT,  DXGI_FORMAT_R32G32_UINT,  DXGI_FORMAT_R32G32B32_UINT,  DXGI_FORMAT_R32G32B32A32_UINT },
        };

        return kFormatTable[_format][static_cast<size_t>(_num) - 1];
    }

    // 더미 VS 입력 구조체에 쓸 HLSL 타입 이름.
    [[nodiscard]] StringView ToHlslTypeName_(
        const eVertexAttributeFormat _format,
        const int                    _num)
    {
        JUG_ASSERT(_num >= 1 && _num <= 4, "A vertex attribute must have 1 to 4 components.");

        constexpr ENUM_ARRAY<eVertexAttributeFormat, ARRAY<StringView, 4>> kNameTable {
            ARRAY<StringView, 4> { "float", "float2", "float3", "float4" },
            ARRAY<StringView, 4> {   "int",   "int2",   "int3",   "int4" },
            ARRAY<StringView, 4> {  "uint",  "uint2",  "uint3",  "uint4" },
        };

        return kNameTable[_format][static_cast<size_t>(_num) - 1];
    }
}   // namespace

// ===========================================
//  Input Layout
// ===========================================

ID3D11InputLayout* Graphics::GetOrCreateD3d11InputLayout_()
{
    if (m_numVertexBuffers == 0)
    {
        return nullptr;
    }

    // 키는 (스트림별 레이아웃 해시, 인스턴스 stride) 다.
    ARRAY<uint64_t, kNumMaxVertexSlots + 1> keyParts = {};
    for (uint32_t i = 0; i < m_numVertexBuffers; ++i)
    {
        keyParts[i] = m_vlhs[i] ? m_vertexLayoutPool.Get(m_vlhs[i]).vertexLayout.GetHash() : 0;
    }
    keyParts[kNumMaxVertexSlots] = m_instanceStride;

    const uint64_t key = Hash<Murmur3>(MemoryView { keyParts });

    if (const auto it = m_d3d11InputLayoutCache.find(key); it != m_d3d11InputLayoutCache.end())
    {
        return it->second;
    }

    // 캐시 미스일 때만 도는 경로다. 여기서만 문자열을 만든다.
    Vector<D3D11_INPUT_ELEMENT_DESC> elements;
    String                           hlsl = "struct VSInput {";

    for (uint32_t slot = 0; slot < m_numVertexBuffers; ++slot)
    {
        const VertexLayoutHandle vlh = m_vlhs[slot];
        if (!vlh)
        {
            continue;
        }

        const VertexLayout& vl = m_vertexLayoutPool.Get(vlh).vertexLayout;
        for (const VertexAttribute& attribute: vl.GetAttributs())
        {
            const VertexSemantic& semantic = kVertexSemanticTable[static_cast<size_t>(attribute.attrib)];

            D3D11_INPUT_ELEMENT_DESC element = {};
            element.SemanticName             = semantic.pName;
            element.SemanticIndex            = semantic.index;
            element.Format                   = ToDxgiVertexFormat_(attribute.format, attribute.num);
            element.InputSlot                = slot;
            element.AlignedByteOffset        = static_cast<UINT>(attribute.offset);
            element.InputSlotClass           = D3D11_INPUT_PER_VERTEX_DATA;
            element.InstanceDataStepRate     = 0;
            elements.push_back(element);

            std::format_to(std::back_inserter(hlsl),
                           "{} Elem{} : {}{};",
                           ToHlslTypeName_(attribute.format, attribute.num),
                           elements.size() - 1,
                           semantic.pName,
                           semantic.index);
        }
    }

    // 인스턴스 버퍼는 마지막 슬롯에 붙고 float4 N 줄로 들어온다.
    if (m_instanceStride > 0)
    {
        const UINT     instanceSlot = static_cast<UINT>(m_numVertexBuffers);
        const uint32_t numVec4      = m_instanceStride / 16u;

        for (uint32_t i = 0; i < numVec4; ++i)
        {
            D3D11_INPUT_ELEMENT_DESC element = {};
            element.SemanticName             = "INSTANCE";
            element.SemanticIndex            = i;
            element.Format                   = DXGI_FORMAT_R32G32B32A32_FLOAT;
            element.InputSlot                = instanceSlot;
            element.AlignedByteOffset        = i * 16u;
            element.InputSlotClass           = D3D11_INPUT_PER_INSTANCE_DATA;
            element.InstanceDataStepRate     = 1;
            elements.push_back(element);

            std::format_to(std::back_inserter(hlsl), "float4 Inst{} : INSTANCE{};", i, i);
        }
    }

    if (elements.empty())
    {
        m_d3d11InputLayoutCache.emplace(key, nullptr);
        return nullptr;
    }

    hlsl += "}; void VSMain(in VSInput _input) {}";

    // 입력 레이아웃은 VS 시그니처만 있으면 된다. 레이아웃으로 만든 더미 VS 는 항상 정확히 들어맞는다.
    ShaderCompileDesc compileDesc = {};
    compileDesc.type              = eShader::Vertex;
    compileDesc.entryPoint        = "VSMain";

    const Result<Shader> dummyVs = Shader::Compile(hlsl, compileDesc);
    JUG_ASSERT(dummyVs, "Failed to compile the dummy vertex shader for an input layout.");

    const MemoryView bytecode = dummyVs->GetByteCode();

    ID3D11InputLayout* pInputLayout = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateInputLayout(
        elements.data(),
        static_cast<UINT>(elements.size()),
        bytecode.GetPtr(),
        bytecode.GetSize(),
        &pInputLayout));

    m_d3d11InputLayoutCache.emplace(key, pInputLayout);
    return pInputLayout;
}

// ===========================================
//  Shader
// ===========================================

ShaderHandle Graphics::FindShaderByHash_(
    const uint64_t _hash) const
{
    const auto it = m_shCache.find(_hash);
    if (it == m_shCache.end())
    {
        return kNullHandle;
    }

    // 캐시에 남아 있어도 이미 파괴된 핸들일 수 있다.
    return m_shaderPool.IsValid(it->second) ? it->second : kNullHandle;
}

ShaderHandle Graphics::CreateShader(
    const eShader    _stage,
    const MemoryView _bytecode)
{
    JUG_ASSERT(!_bytecode.IsEmpty(), "Shader bytecode is empty.");

    const uint64_t hash = Hash<Murmur3>(_bytecode);

    // 같은 바이트코드는 하나만 두고 참조 카운트로 공유한다.
    if (const ShaderHandle cached = FindShaderByHash_(hash))
    {
        JUG_ASSERT(m_shaderPool.Get(cached).type == _stage, "The same bytecode was already created with a different shader stage.");
        ++m_shaderPool.Get(cached).refCount;
        return cached;
    }

    const void*  pBytes    = _bytecode.GetPtr();
    const size_t byteWidth = _bytecode.GetSize();

    const ShaderHandle sh     = m_shaderPool.Emplace();
    ShaderD3D11&       shader = m_shaderPool.Get(sh);
    shader.type               = _stage;
    shader.refCount           = 1;
    shader.hash               = hash;

    switch (_stage)
    {
        case eShader::Vertex: JUG_DX_CHECK(m_pD3d11Device->CreateVertexShader(pBytes, byteWidth, nullptr, &shader.pVS)); break;
        case eShader::Pixel: JUG_DX_CHECK(m_pD3d11Device->CreatePixelShader(pBytes, byteWidth, nullptr, &shader.pPS)); break;
        case eShader::Compute: JUG_DX_CHECK(m_pD3d11Device->CreateComputeShader(pBytes, byteWidth, nullptr, &shader.pCS)); break;
        default: JUG_ASSERT(false, "Unknown shader stage."); break;
    }

    m_shCache.insert_or_assign(hash, sh);
    return sh;
}

// ===========================================
//  Program
// ===========================================

ProgramHandle Graphics::CreateProgram(
    const ShaderHandle _vsh,
    const ShaderHandle _psh,
    const bool         _bOwnership)
{
    JUG_ASSERT(_vsh, "A program requires a valid vertex shader.");
    JUG_ASSERT(m_shaderPool.Get(_vsh).type == eShader::Vertex, "The first shader must be a vertex shader.");
    JUG_ASSERT(!_psh || m_shaderPool.Get(_psh).type == eShader::Pixel, "The second shader must be a pixel shader.");

    // 소유권을 넘겨받지 않으면 프로그램이 참조를 하나 더 든다.
    if (!_bOwnership)
    {
        ++m_shaderPool.Get(_vsh).refCount;
        if (_psh)
        {
            ++m_shaderPool.Get(_psh).refCount;
        }
    }

    const ARRAY<uint64_t, 2> hashes {
        m_shaderPool.Get(_vsh).hash,
        _psh ? m_shaderPool.Get(_psh).hash : 0,
    };

    const ProgramHandle ph      = m_programPool.Emplace();
    ProgramDesc&        program = m_programPool.Get(ph);
    program.vsh                 = _vsh;
    program.psh                 = _psh;
    program.csh                 = kNullHandle;
    program.hash                = Hash<Murmur3>(MemoryView { hashes });
    return ph;
}

ProgramHandle Graphics::CreateComputeProgram(
    const ShaderHandle _csh,
    const bool         _bOwnership)
{
    JUG_ASSERT(_csh, "A compute program requires a valid compute shader.");
    JUG_ASSERT(m_shaderPool.Get(_csh).type == eShader::Compute, "The shader must be a compute shader.");

    if (!_bOwnership)
    {
        ++m_shaderPool.Get(_csh).refCount;
    }

    const ProgramHandle ph      = m_programPool.Emplace();
    ProgramDesc&        program = m_programPool.Get(ph);
    program.vsh                 = kNullHandle;
    program.psh                 = kNullHandle;
    program.csh                 = _csh;
    program.hash                = m_shaderPool.Get(_csh).hash;
    return ph;
}

// ===========================================================================
//  Unbind & Destroy & Debug & Getter
//   [AI] 전부 재작성. Destroy / SetName / GetDesc 가 타입별 함수(DestroyVertexBuffer, SetTextureName ...)에서
//        오버로드로 바뀌었다. Unbind 계열은 선언만 있고 정의가 아예 없었다.
// ===========================================================================

// ===========================================
//  Unbind
// ===========================================

void Graphics::UnbindResource_(
    const Resource _resource)
{
    for (size_t stage = 0; stage < m_readBind.GetSize(); ++stage)
    {
        ReadBind& bind = m_readBind[stage];
        for (int slot = 0; slot < static_cast<int>(kNumMaxReadSlots); ++slot)
        {
            if (bind.resources[slot] != _resource)
            {
                continue;
            }

            bind.resources[slot]      = {};
            bind.d3d11Resources[slot] = nullptr;
            bind.MarkDirty(slot);
            m_dirtyFlags |= ToSrvDirty_(static_cast<eShader>(stage));
        }
    }

    for (size_t stage = 0; stage < m_readWriteBind.GetSize(); ++stage)
    {
        ReadWriteBind& bind = m_readWriteBind[stage];
        for (int slot = 0; slot < static_cast<int>(kNumMaxReadWriteSlots); ++slot)
        {
            if (bind.resources[slot] != _resource)
            {
                continue;
            }

            bind.resources[slot]      = {};
            bind.d3d11Resources[slot] = nullptr;
            bind.MarkDirty(slot);
            m_dirtyFlags |= static_cast<eShaderRW>(stage) == eShaderRW::Compute
                              ? ePipelineDirty::CS_UnorderedAccessView
                              : ePipelineDirty::PS_UnorderedAccessView;
        }
    }
}

void Graphics::UnbindConstantBuffer_(
    const ConstantBufferHandle _cbh)
{
    for (size_t stage = 0; stage < m_cbufferBind.GetSize(); ++stage)
    {
        CBufferBind& bind = m_cbufferBind[stage];
        for (int slot = 0; slot < static_cast<int>(kNumMaxCBufferSlots); ++slot)
        {
            if (bind.resources[slot] != _cbh)
            {
                continue;
            }

            bind.resources[slot]      = kNullHandle;
            bind.d3d11Resources[slot] = nullptr;
            bind.MarkDirty(slot);
            m_dirtyFlags |= ToCBufferDirty_(static_cast<eShader>(stage));
        }
    }
}

void Graphics::UnbindVertexBuffer_(
    const VertexBufferHandle _vbh)
{
    for (uint32_t i = 0; i < kNumMaxVertexSlots; ++i)
    {
        if (m_vbhs[i] != _vbh)
        {
            continue;
        }

        m_vbhs[i]               = kNullHandle;
        m_vlhs[i]               = kNullHandle;
        m_d3d11VertexBuffers[i] = nullptr;
        m_vertexStrides[i]      = 0;
        m_vertexOffsets[i]      = 0;
        m_dirtyFlags |= { ePipelineDirty::VertexBuffer, ePipelineDirty::InputLayout };
    }

    if (m_instanceVbh == _vbh)
    {
        m_instanceVbh          = kNullHandle;
        m_pD3d11InstanceBuffer = nullptr;
        m_instanceStride       = 0;
        m_instanceOffset       = 0;
        m_numInstances         = 0;
        m_dirtyFlags |= { ePipelineDirty::InstanceBuffer, ePipelineDirty::InputLayout };
    }
}

void Graphics::UnbindIndexBuffer_(
    const IndexBufferHandle _ibh)
{
    if (m_ibh != _ibh)
    {
        return;
    }

    m_ibh               = kNullHandle;
    m_pD3d11IndexBuffer = nullptr;
    m_dxgiIndexFormat   = DXGI_FORMAT_UNKNOWN;
    m_indexOffset       = 0;
    m_numIndices        = 0;
    m_dirtyFlags |= ePipelineDirty::IndexBuffer;
}

void Graphics::UnbindFrameBuffer_(
    const FrameBufferHandle _fbh)
{
    if (m_lastFbh == _fbh)
    {
        m_lastFbh = kNullHandle;
    }

    if (m_fbh == _fbh)
    {
        m_fbh = kNullHandle;
        m_dirtyFlags |= ePipelineDirty::FrameBuffer;
    }
}

// ===========================================
//  Destroy
// ===========================================

void Graphics::Destroy(
    const VertexBufferHandle _vbh)
{
    VertexBufferD3D11& vertexBuffer = m_vertexBufferPool.Get(_vbh);

    UnbindVertexBuffer_(_vbh);
    if (vertexBuffer.vlh)
    {
        Destroy(vertexBuffer.vlh);
    }
    JUG_DX_RELEASE(vertexBuffer.pBuffer);

    m_vertexBufferPool.Erase(_vbh);
}

void Graphics::Destroy(
    const IndexBufferHandle _ibh)
{
    UnbindIndexBuffer_(_ibh);
    JUG_DX_RELEASE(m_indexBufferPool.Get(_ibh).pBuffer);

    m_indexBufferPool.Erase(_ibh);
}

void Graphics::Destroy(
    const VertexLayoutHandle _vlh)
{
    VertexLayoutD3D11& vertexLayout = m_vertexLayoutPool.Get(_vlh);

    JUG_ASSERT(vertexLayout.refCount > 0, "Vertex layout reference count underflow.");
    if (--vertexLayout.refCount > 0)
    {
        return;
    }

    m_vlhCache.erase(vertexLayout.vertexLayout.GetHash());
    m_vertexLayoutPool.Erase(_vlh);
}

void Graphics::Destroy(
    const ConstantBufferHandle _cbh)
{
    UnbindConstantBuffer_(_cbh);
    JUG_DX_RELEASE(m_constantBufferPool.Get(_cbh).pBuffer);

    m_constantBufferPool.Erase(_cbh);
}

void Graphics::Destroy(
    const StorageBufferHandle _sbh)
{
    StorageBufferD3D11& storageBuffer = m_storageBufferPool.Get(_sbh);

    UnbindResource_(Resource { _sbh.GetValue(), eResource::Buffer });
    JUG_DX_RELEASE(storageBuffer.pSRV);
    JUG_DX_RELEASE(storageBuffer.pUAV);
    JUG_DX_RELEASE(storageBuffer.pBuffer);

    m_storageBufferPool.Erase(_sbh);
}

void Graphics::Destroy(
    const TextureHandle _texh)
{
    TextureD3D11& texture = m_texturePool.Get(_texh);

    // 프레임버퍼가 참조를 들고 있을 수 있으므로 참조 카운트로 관리한다.
    JUG_ASSERT(texture.refCount > 0, "Texture reference count underflow.");
    if (--texture.refCount > 0)
    {
        return;
    }

    UnbindResource_(Resource { _texh.GetValue(), eResource::Texture });

    JUG_DX_RELEASE(texture.pSRV);
    JUG_DX_RELEASE(texture.pUAV);
    JUG_DX_RELEASE(texture.pMsaaRtResource);
    JUG_DX_RELEASE(texture.pResource);

    m_texturePool.Erase(_texh);
}

void Graphics::Destroy(
    const FrameBufferHandle _fbh)
{
    FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(_fbh);

    // 전체화면 상태로 스왑체인을 놓으면 즉시 크래시한다.
    if (frameBuffer.pDxgiSwapChain)
    {
        JUG_DISCARD_RETURN(frameBuffer.pDxgiSwapChain->SetFullscreenState(FALSE, nullptr));
    }

    UnbindFrameBuffer_(_fbh);
    ReleaseFrameBufferViews_(frameBuffer);

    // 어태치먼트 참조를 놓는다. 소유권을 받았다면 텍스처도 함께 사라진다.
    const uint32_t numAttachments = frameBuffer.numRts + (frameBuffer.bHasDepthStencil ? 1 : 0);
    for (uint32_t i = 0; i < numAttachments; ++i)
    {
        Destroy(frameBuffer.attachments[i].texh);
    }

    JUG_DX_RELEASE(frameBuffer.pDxgiSwapChain);

    m_frameBufferPool.Erase(_fbh);
}

void Graphics::Destroy(
    const ShaderHandle _sh)
{
    ShaderD3D11& shader = m_shaderPool.Get(_sh);

    JUG_ASSERT(shader.refCount > 0, "Shader reference count underflow.");
    if (--shader.refCount > 0)
    {
        return;
    }

    m_shCache.erase(shader.hash);
    JUG_DX_RELEASE(shader.pVS);

    m_shaderPool.Erase(_sh);
}

void Graphics::Destroy(
    const ProgramHandle _ph)
{
    const ProgramDesc program = m_programPool.Get(_ph);

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

    if (program.vsh)
    {
        Destroy(program.vsh);
    }
    if (program.psh)
    {
        Destroy(program.psh);
    }
    if (program.csh)
    {
        Destroy(program.csh);
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
    SetD3d11ObjectName_(m_vertexBufferPool.Get(_vbh).pBuffer, _name);
}

void Graphics::SetName(
    const IndexBufferHandle _ibh,
    const StringView        _name)
{
    SetD3d11ObjectName_(m_indexBufferPool.Get(_ibh).pBuffer, _name);
}

void Graphics::SetName(
    const ConstantBufferHandle _cbh,
    const StringView           _name)
{
    SetD3d11ObjectName_(m_constantBufferPool.Get(_cbh).pBuffer, _name);
}

void Graphics::SetName(
    const StorageBufferHandle _sbh,
    const StringView          _name)
{
    const StorageBufferD3D11& storageBuffer = m_storageBufferPool.Get(_sbh);
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
    const TextureD3D11& texture = m_texturePool.Get(_texh);
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
    const FrameBufferHandle           _fbh,
    [[maybe_unused]] const StringView _name)
{
    const FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(_fbh);

    for (uint32_t i = 0; i < frameBuffer.numRts; ++i)
    {
        SetD3d11ObjectName_(frameBuffer.rtvs[i], _name);
    }
    if (frameBuffer.pDSV)
    {
        SetD3d11ObjectName_(frameBuffer.pDSV, _name);
    }

#ifdef JUG_DEBUG
    if (frameBuffer.pDxgiSwapChain)
    {
        const UINT len = static_cast<UINT>(Min(_name.size(), kMaxDebugNameLength));
        JUG_DISCARD_RETURN(frameBuffer.pDxgiSwapChain->SetPrivateData(WKPDID_D3DDebugObjectName, len, _name.data()));
    }
#endif
}

void Graphics::SetName(
    const ShaderHandle _sh,
    const StringView   _name)
{
    const ShaderD3D11& shader = m_shaderPool.Get(_sh);

    // 셰이더 오브젝트는 익명 유니온에 들어 있다. 타입으로 골라서 꺼낸다.
    switch (shader.type)
    {
        case eShader::Vertex: SetD3d11ObjectName_(shader.pVS, _name); break;
        case eShader::Pixel: SetD3d11ObjectName_(shader.pPS, _name); break;
        case eShader::Compute: SetD3d11ObjectName_(shader.pCS, _name); break;
        default: JUG_ASSERT(false, "Unknown shader stage."); break;
    }
}

void Graphics::PushDebugGroup(
    [[maybe_unused]] const StringView _name)
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        JUG_DISCARD_RETURN(m_pUserAnnotationOrNull->BeginEvent(ToUtf16(_name).c_str()));
    }
#endif
}

void Graphics::PopDebugGroup()
{
#ifdef JUG_DEBUG
    if (m_pUserAnnotationOrNull)
    {
        JUG_DISCARD_RETURN(m_pUserAnnotationOrNull->EndEvent());
    }
#endif
}

void Graphics::SetDebugMarker(
    [[maybe_unused]] const StringView _name)
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
    return m_vertexBufferPool.Get(_vbh);
}

const IndexBufferDesc& Graphics::GetDesc(
    const IndexBufferHandle _ibh) const
{
    return m_indexBufferPool.Get(_ibh);
}

const ConstantBufferDesc& Graphics::GetDesc(
    const ConstantBufferHandle _cbh) const
{
    return m_constantBufferPool.Get(_cbh);
}

const StorageBufferDesc& Graphics::GetDesc(
    const StorageBufferHandle _sbh) const
{
    return m_storageBufferPool.Get(_sbh);
}

const TextureDesc& Graphics::GetDesc(
    const TextureHandle _texh) const
{
    return m_texturePool.Get(_texh);
}

const ShaderDesc& Graphics::GetDesc(
    const ShaderHandle _sh) const
{
    return m_shaderPool.Get(_sh);
}

const ProgramDesc& Graphics::GetDesc(
    const ProgramHandle _ph) const
{
    return m_programPool.Get(_ph);
}

const FrameBufferDesc& Graphics::GetDesc(
    const FrameBufferHandle _fbh) const
{
    return m_frameBufferPool.Get(ResolveFrameBufferHandle_(_fbh));
}

const VertexLayout& Graphics::GetVertexLayout(
    const VertexLayoutHandle _vlh) const
{
    return m_vertexLayoutPool.Get(_vlh).vertexLayout;
}

// ===========================================================================
//  Pipeline State & Bind & Submit
//   [AI] 전부 재작성.
//        UAV 섀도 배열이 m_psReadWriteBind / m_csReadWriteBind 두 멤버에서
//        ENUM_ARRAY<eShaderRW, ReadWriteBind> 하나로 합쳐져서 그에 맞게 고쳤다.
//        device lost 조기 반환과 "슬롯 범위를 벗어나면 조용히 return" 하는 방어 코드는 전부 ASSERT 로 바꿨다.
// ===========================================================================

Flags<Graphics::ePipelineDirty> Graphics::ToSrvDirty_(
    const eShader _shader) const
{
    switch (_shader)
    {
        case eShader::Vertex: return ePipelineDirty::VS_ShaderResourceView;
        case eShader::Pixel: return ePipelineDirty::PS_ShaderResourceView;
        default: return ePipelineDirty::CS_ShaderResourceView;
    }
}

Flags<Graphics::ePipelineDirty> Graphics::ToCBufferDirty_(
    const eShader _shader) const
{
    switch (_shader)
    {
        case eShader::Vertex: return ePipelineDirty::VS_ConstantBuffer;
        case eShader::Pixel: return ePipelineDirty::PS_ConstantBuffer;
        default: return ePipelineDirty::CS_ConstantBuffer;
    }
}

Flags<Graphics::ePipelineDirty> Graphics::ToSamplerDirty_(
    const eShader _shader) const
{
    switch (_shader)
    {
        case eShader::Vertex: return ePipelineDirty::VS_SamplerState;
        case eShader::Pixel: return ePipelineDirty::PS_SamplerState;
        default: return ePipelineDirty::CS_SamplerState;
    }
}

// ===========================================
//  State Cache
// ===========================================

ID3D11SamplerState* Graphics::GetOrCreateSamplerState_(
    const Flags<eSampler> _flags,
    const RGBA            _borderColor)
{
    const ARRAY<uint64_t, 2> keyParts { _flags.GetFlags(), _borderColor.rgba };
    const uint64_t           key = Hash<Murmur3>(MemoryView { keyParts });

    if (const auto it = m_d3d11SamplerStateCache.find(key); it != m_d3d11SamplerStateCache.end())
    {
        return it->second;
    }

    const D3D11_SAMPLER_DESC desc = MakeSamplerDesc_(_flags, _borderColor);

    ID3D11SamplerState* pSamplerState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateSamplerState(&desc, &pSamplerState));

    m_d3d11SamplerStateCache.emplace(key, pSamplerState);
    return pSamplerState;
}

ID3D11RasterizerState* Graphics::GetOrCreateRasterizerState_()
{
    const uint64_t key = m_renderStateFlags.GetFlags();

    if (const auto it = m_d3d11RasterizerStateCache.find(key); it != m_d3d11RasterizerStateCache.end())
    {
        return it->second;
    }

    const D3D11_RASTERIZER_DESC desc = MakeRasterizerDesc_(m_renderStateFlags);

    ID3D11RasterizerState* pRasterizerState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateRasterizerState(&desc, &pRasterizerState));

    m_d3d11RasterizerStateCache.emplace(key, pRasterizerState);
    return pRasterizerState;
}

ID3D11BlendState* Graphics::GetOrCreateBlendState_()
{
    ARRAY<uint64_t, kNumMaxRenderTargetSlots + 1> keyParts = {};
    keyParts[0]                                            = m_renderStateFlags.GetFlags();
    for (size_t i = 0; i < kNumMaxRenderTargetSlots; ++i)
    {
        keyParts[i + 1] = m_blendFlags[i].GetFlags();
    }

    const uint64_t key = Hash<Murmur3>(MemoryView { keyParts });

    if (const auto it = m_d3d11BlendStateCache.find(key); it != m_d3d11BlendStateCache.end())
    {
        return it->second;
    }

    const D3D11_BLEND_DESC desc = MakeBlendDesc_(m_renderStateFlags, Span<const Flags<eBlend>, kNumMaxRenderTargetSlots> { m_blendFlags });

    ID3D11BlendState* pBlendState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateBlendState(&desc, &pBlendState));

    m_d3d11BlendStateCache.emplace(key, pBlendState);
    return pBlendState;
}

ID3D11DepthStencilState* Graphics::GetOrCreateDepthStencilState_()
{
    const ARRAY<uint64_t, 3> keyParts {
        m_renderStateFlags.GetFlags(),
        m_fstencilFlags.GetFlags(),
        m_bstencilFlags.GetFlags(),
    };

    const uint64_t key = Hash<Murmur3>(MemoryView { keyParts });

    if (const auto it = m_d3d11DepthStencilStateCache.find(key); it != m_d3d11DepthStencilStateCache.end())
    {
        return it->second;
    }

    const D3D11_DEPTH_STENCIL_DESC desc = MakeDepthStencilDesc_(m_renderStateFlags, m_fstencilFlags, m_bstencilFlags);

    ID3D11DepthStencilState* pDepthStencilState = nullptr;
    JUG_DX_CHECK(m_pD3d11Device->CreateDepthStencilState(&desc, &pDepthStencilState));

    m_d3d11DepthStencilStateCache.emplace(key, pDepthStencilState);
    return pDepthStencilState;
}

// ===========================================
//  Apply
// ===========================================

void Graphics::BindFrameBuffer_()
{
    // 이전 프레임버퍼의 MSAA 를 먼저 내리고 밉을 갱신한다.
    if (m_lastFbh && m_lastFbh != m_fbh)
    {
        ResolveFrameBuffer_(m_lastFbh);
    }

    if (!m_fbh)
    {
        m_pD3d11DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_lastFbh = kNullHandle;
        return;
    }

    const FrameBufferD3D11& frameBuffer = m_frameBufferPool.Get(m_fbh);
    ReadWriteBind&          psUavBind   = m_readWriteBind[eShaderRW::Pixel];

    // PS UAV 는 렌더 타겟과 같은 호출로 묶어야 한다. D3D11 이 한 슬롯 공간을 공유한다.
    if (psUavBind.IsDirty() || m_dirtyFlags.Has(ePipelineDirty::PS_UnorderedAccessView))
    {
        // PS UAV 는 렌더 타겟 슬롯 뒤에서 시작한다.
        // 섀도 배열은 사용자 슬롯 기준이므로 시작 슬롯만큼 포인터도 같이 밀어야 한다.
        const UINT uavStartSlot = frameBuffer.numRts;
        const UINT numUavs      = static_cast<UINT>(kNumMaxReadWriteSlots) > uavStartSlot
                                    ? static_cast<UINT>(kNumMaxReadWriteSlots) - uavStartSlot
                                    : 0u;

        m_pD3d11DeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(
            frameBuffer.numRts,
            frameBuffer.rtvs.data(),
            frameBuffer.pDSV,
            uavStartSlot,
            numUavs,
            psUavBind.d3d11Resources.data() + uavStartSlot,
            nullptr);

        psUavBind.ClearDirty();
    }
    else
    {
        m_pD3d11DeviceContext->OMSetRenderTargets(
            frameBuffer.numRts,
            frameBuffer.rtvs.data(),
            frameBuffer.pDSV);
    }

    m_lastFbh = m_fbh;
}

void Graphics::ApplyPipeline_()
{
    if (m_dirtyFlags == kZeroFlag)
    {
        return;
    }

    ID3D11DeviceContext* pContext = m_pD3d11DeviceContext;

    if (m_dirtyFlags.HasAny({ ePipelineDirty::VertexBuffer, ePipelineDirty::InstanceBuffer }))
    {
        ARRAY<ID3D11Buffer*, kNumMaxVertexSlots + 1> buffers = {};
        ARRAY<UINT, kNumMaxVertexSlots + 1>          strides = {};
        ARRAY<UINT, kNumMaxVertexSlots + 1>          offsets = {};

        UINT numBuffers = 0;
        for (uint32_t i = 0; i < m_numVertexBuffers; ++i)
        {
            buffers[numBuffers] = m_d3d11VertexBuffers[i];
            strides[numBuffers] = m_vertexStrides[i];
            offsets[numBuffers] = m_vertexOffsets[i];
            ++numBuffers;
        }

        if (m_pD3d11InstanceBuffer)
        {
            buffers[numBuffers] = m_pD3d11InstanceBuffer;
            strides[numBuffers] = m_instanceStride;
            offsets[numBuffers] = m_instanceOffset;
            ++numBuffers;
        }

        pContext->IASetVertexBuffers(0, numBuffers, buffers.data(), strides.data(), offsets.data());
    }

    if (m_dirtyFlags.Has(ePipelineDirty::IndexBuffer))
    {
        pContext->IASetIndexBuffer(m_pD3d11IndexBuffer, m_dxgiIndexFormat, 0);
    }

    if (m_dirtyFlags.Has(ePipelineDirty::PrimitiveTopology))
    {
        pContext->IASetPrimitiveTopology(MakeD3d11Topology_(m_renderStateFlags));
    }

    if (m_dirtyFlags.Has(ePipelineDirty::InputLayout))
    {
        m_pD3d11InputLayout = GetOrCreateD3d11InputLayout_();
        pContext->IASetInputLayout(m_pD3d11InputLayout);
    }

    if (m_dirtyFlags.Has(ePipelineDirty::Program))
    {
        ID3D11VertexShader* pVS = nullptr;
        ID3D11PixelShader*  pPS = nullptr;

        if (m_ph)
        {
            const ProgramDesc& program = m_programPool.Get(m_ph);
            pVS                        = program.vsh ? m_shaderPool.Get(program.vsh).pVS : nullptr;
            pPS                        = program.psh ? m_shaderPool.Get(program.psh).pPS : nullptr;
        }

        pContext->VSSetShader(pVS, nullptr, 0);
        pContext->PSSetShader(pPS, nullptr, 0);
    }

    if (m_dirtyFlags.Has(ePipelineDirty::ComputeProgram))
    {
        ID3D11ComputeShader* pCS = nullptr;
        if (m_computePh)
        {
            const ProgramDesc& program = m_programPool.Get(m_computePh);
            pCS                        = program.csh ? m_shaderPool.Get(program.csh).pCS : nullptr;
        }

        pContext->CSSetShader(pCS, nullptr, 0);
    }

    // 상수 버퍼 / SRV / 샘플러는 더티 구간만 한 번에 바인딩한다. 슬롯마다 부르지 않는다.
    for (size_t stage = 0; stage < m_cbufferBind.GetSize(); ++stage)
    {
        CBufferBind& bind = m_cbufferBind[stage];
        if (!bind.IsDirty())
        {
            continue;
        }

        const UINT start = static_cast<UINT>(bind.dirtyBegin);
        const UINT count = static_cast<UINT>(bind.NumDirties());

        switch (static_cast<eShader>(stage))
        {
            case eShader::Vertex: pContext->VSSetConstantBuffers(start, count, bind.d3d11Resources.data() + start); break;
            case eShader::Pixel: pContext->PSSetConstantBuffers(start, count, bind.d3d11Resources.data() + start); break;
            default: pContext->CSSetConstantBuffers(start, count, bind.d3d11Resources.data() + start); break;
        }

        bind.ClearDirty();
    }

    for (size_t stage = 0; stage < m_readBind.GetSize(); ++stage)
    {
        ReadBind& bind = m_readBind[stage];
        if (!bind.IsDirty())
        {
            continue;
        }

        const UINT start = static_cast<UINT>(bind.dirtyBegin);
        const UINT count = static_cast<UINT>(bind.NumDirties());

        switch (static_cast<eShader>(stage))
        {
            case eShader::Vertex: pContext->VSSetShaderResources(start, count, bind.d3d11Resources.data() + start); break;
            case eShader::Pixel: pContext->PSSetShaderResources(start, count, bind.d3d11Resources.data() + start); break;
            default: pContext->CSSetShaderResources(start, count, bind.d3d11Resources.data() + start); break;
        }

        bind.ClearDirty();
    }

    for (size_t stage = 0; stage < m_samplerBind.GetSize(); ++stage)
    {
        SamplerBind& bind = m_samplerBind[stage];
        if (!bind.IsDirty())
        {
            continue;
        }

        const UINT start = static_cast<UINT>(bind.dirtyBegin);
        const UINT count = static_cast<UINT>(bind.NumDirties());

        switch (static_cast<eShader>(stage))
        {
            case eShader::Vertex: pContext->VSSetSamplers(start, count, bind.d3d11Resources.data() + start); break;
            case eShader::Pixel: pContext->PSSetSamplers(start, count, bind.d3d11Resources.data() + start); break;
            default: pContext->CSSetSamplers(start, count, bind.d3d11Resources.data() + start); break;
        }

        bind.ClearDirty();
    }

    if (ReadWriteBind& csUavBind = m_readWriteBind[eShaderRW::Compute]; csUavBind.IsDirty())
    {
        const UINT start = static_cast<UINT>(csUavBind.dirtyBegin);
        const UINT count = static_cast<UINT>(csUavBind.NumDirties());

        pContext->CSSetUnorderedAccessViews(start, count, csUavBind.d3d11Resources.data() + start, nullptr);
        csUavBind.ClearDirty();
    }

    if (m_dirtyFlags.Has(ePipelineDirty::BlendState))
    {
        const VECTOR4 blendFactor = m_blendFactor.ToLinear();
        pContext->OMSetBlendState(GetOrCreateBlendState_(), blendFactor.e.data(), 0xFFFF'FFFF);
    }

    if (m_dirtyFlags.Has(ePipelineDirty::DepthStencilState))
    {
        pContext->OMSetDepthStencilState(GetOrCreateDepthStencilState_(), m_stencilRef);
    }

    if (m_dirtyFlags.Has(ePipelineDirty::RasterizerState))
    {
        pContext->RSSetState(GetOrCreateRasterizerState_());
    }

    if (m_dirtyFlags.Has(ePipelineDirty::FrameBuffer) || m_readWriteBind[eShaderRW::Pixel].IsDirty())
    {
        BindFrameBuffer_();
    }

    if (m_dirtyFlags.Has(ePipelineDirty::Viewport))
    {
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX       = m_viewportX;
        viewport.TopLeftY       = m_viewportY;
        viewport.Width          = m_viewportW;
        viewport.Height         = m_viewportH;
        viewport.MinDepth       = 0.f;
        viewport.MaxDepth       = 1.f;

        pContext->RSSetViewports(1, &viewport);
    }

    if (m_dirtyFlags.Has(ePipelineDirty::ScissorRect))
    {
        D3D11_RECT rect = {};
        rect.left       = m_scissorX;
        rect.top        = m_scissorY;
        rect.right      = m_scissorX + m_scissorW;
        rect.bottom     = m_scissorY + m_scissorH;

        pContext->RSSetScissorRects(1, &rect);
    }

    m_dirtyFlags = kZeroFlag;
}

// ===========================================
//  Input Assembler
// ===========================================

void Graphics::SetVertexBuffer(
    const VertexBufferHandle _vbh,
    const uint32_t           _offset,
    const uint32_t           _numVertices)
{
    const ARRAY<VertexStream, 1> streams {
        VertexStream { _vbh, _offset }
    };
    SetVertexBuffers(Span<const VertexStream> { streams }, _numVertices);
}

void Graphics::SetVertexBuffer(
    const VertexBufferHandle _vbh,
    const uint32_t           _offset,
    const uint32_t           _numVertices,
    const VertexBufferHandle _instanceVbh,
    const uint32_t           _instanceOffset,
    const uint32_t           _numInstances)
{
    const ARRAY<VertexStream, 1> streams {
        VertexStream { _vbh, _offset }
    };
    SetVertexBuffers(Span<const VertexStream> { streams }, _numVertices, _instanceVbh, _instanceOffset, _numInstances);
}

void Graphics::SetVertexBuffers(
    const Span<const VertexStream> _streams,
    const uint32_t                 _numVertices)
{
    JUG_ASSERT(_streams.size() <= kNumMaxVertexSlots, "Too many vertex streams.");

    const uint32_t numStreams = static_cast<uint32_t>(_streams.size());

    // 스트림 개수가 바뀌면 입력 레이아웃도 달라진다.
    bool bLayoutChanged = numStreams != m_numVertexBuffers;
    bool bBufferChanged = bLayoutChanged;

    uint32_t numVertices = 0;

    for (uint32_t i = 0; i < numStreams; ++i)
    {
        const VertexStream& stream = _streams[i];

        ID3D11Buffer*      pBuffer = nullptr;
        VertexLayoutHandle vlh     = kNullHandle;
        uint32_t           stride  = 0;

        if (stream.vbh)
        {
            const VertexBufferD3D11& vertexBuffer = m_vertexBufferPool.Get(stream.vbh);
            pBuffer                               = vertexBuffer.pBuffer;
            vlh                                   = vertexBuffer.vlh;
            stride                                = vertexBuffer.stride;
            numVertices                           = Max(numVertices, vertexBuffer.byteWidth / stride);
        }

        const uint32_t offset = stream.offset * stride;

        bLayoutChanged = bLayoutChanged || m_vlhs[i] != vlh;
        bBufferChanged = bBufferChanged
                      || m_vbhs[i] != stream.vbh
                      || m_vertexOffsets[i] != offset
                      || m_vertexStrides[i] != stride;

        m_vbhs[i]               = stream.vbh;
        m_vlhs[i]               = vlh;
        m_d3d11VertexBuffers[i] = pBuffer;
        m_vertexStrides[i]      = stride;
        m_vertexOffsets[i]      = offset;
    }

    for (uint32_t i = numStreams; i < kNumMaxVertexSlots; ++i)
    {
        m_vbhs[i]               = kNullHandle;
        m_vlhs[i]               = kNullHandle;
        m_d3d11VertexBuffers[i] = nullptr;
        m_vertexStrides[i]      = 0;
        m_vertexOffsets[i]      = 0;
    }

    m_numVertexBuffers = numStreams;
    m_numVertices      = _numVertices == kWholeSize ? numVertices : _numVertices;

    if (bBufferChanged)
    {
        m_dirtyFlags |= ePipelineDirty::VertexBuffer;
    }
    if (bLayoutChanged)
    {
        m_dirtyFlags |= ePipelineDirty::InputLayout;
    }
}

void Graphics::SetVertexBuffers(
    const Span<const VertexStream> _streams,
    const uint32_t                 _numVertices,
    const VertexBufferHandle       _instanceVbh,
    const uint32_t                 _instanceOffset,
    const uint32_t                 _numInstances)
{
    SetVertexBuffers(_streams, _numVertices);

    ID3D11Buffer* pBuffer      = nullptr;
    uint32_t      stride       = 0;
    uint32_t      numInstances = 0;

    if (_instanceVbh)
    {
        const VertexBufferD3D11& instanceBuffer = m_vertexBufferPool.Get(_instanceVbh);
        pBuffer                                 = instanceBuffer.pBuffer;
        stride                                  = instanceBuffer.stride;
        numInstances                            = instanceBuffer.byteWidth / stride;
    }

    const bool bLayoutChanged = m_instanceStride != stride;

    m_instanceVbh          = _instanceVbh;
    m_pD3d11InstanceBuffer = pBuffer;
    m_instanceStride       = stride;
    m_instanceOffset       = _instanceOffset * stride;
    m_numInstances         = _numInstances == kWholeSize ? numInstances : _numInstances;

    m_dirtyFlags |= ePipelineDirty::InstanceBuffer;

    if (bLayoutChanged)
    {
        m_dirtyFlags |= ePipelineDirty::InputLayout;
    }
}

void Graphics::SetIndexBuffer(
    const IndexBufferHandle _ibh,
    const uint32_t          _offset,
    const uint32_t          _numIndices)
{
    ID3D11Buffer* pBuffer    = nullptr;
    DXGI_FORMAT   format     = DXGI_FORMAT_UNKNOWN;
    uint32_t      numIndices = 0;

    if (_ibh)
    {
        const IndexBufferD3D11& indexBuffer = m_indexBufferPool.Get(_ibh);
        pBuffer                             = indexBuffer.pBuffer;
        format                              = indexBuffer.format;
        numIndices                          = indexBuffer.byteWidth / (indexBuffer.bU32 ? 4u : 2u);
    }

    if (m_pD3d11IndexBuffer != pBuffer || m_dxgiIndexFormat != format)
    {
        m_dirtyFlags |= ePipelineDirty::IndexBuffer;
    }

    m_ibh               = _ibh;
    m_pD3d11IndexBuffer = pBuffer;
    m_dxgiIndexFormat   = format;
    m_indexOffset       = _offset;
    m_numIndices        = _numIndices == kWholeSize ? numIndices : _numIndices;
}

// ===========================================
//  Resource Binding
// ===========================================

void Graphics::SetConstantBuffer(
    const ConstantBufferHandle _cbh,
    const eShader              _shader,
    const int                  _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxCBufferSlots, "Constant buffer slot is out of range.");

    CBufferBind& bind = m_cbufferBind[_shader];
    if (bind.resources[_slot] == _cbh)
    {
        return;
    }

    bind.resources[_slot]      = _cbh;
    bind.d3d11Resources[_slot] = _cbh ? m_constantBufferPool.Get(_cbh).pBuffer : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= ToCBufferDirty_(_shader);
}

void Graphics::SetTexture(
    const TextureHandle _texh,
    const eShader       _shader,
    const int           _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxReadSlots, "Shader resource slot is out of range.");

    const Resource resource { _texh.GetValue(), eResource::Texture };

    ReadBind& bind = m_readBind[_shader];
    if (bind.resources[_slot] == resource)
    {
        return;
    }

    bind.resources[_slot]      = resource;
    bind.d3d11Resources[_slot] = _texh ? m_texturePool.Get(_texh).pSRV : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= ToSrvDirty_(_shader);
}

void Graphics::SetTextureRW(
    const TextureHandle _texh,
    const eShader       _shader,
    const int           _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxReadWriteSlots, "Unordered access slot is out of range.");

    JUG_ASSERT(_shader == eShader::Pixel || _shader == eShader::Compute, "Only pixel and compute shaders can bind UAVs.");

    const eShaderRW shaderRW = _shader == eShader::Compute ? eShaderRW::Compute : eShaderRW::Pixel;
    const Resource  resource { _texh.GetValue(), eResource::Texture };

    ReadWriteBind& bind = m_readWriteBind[shaderRW];
    if (bind.resources[_slot] == resource)
    {
        return;
    }

    bind.resources[_slot]      = resource;
    bind.d3d11Resources[_slot] = _texh ? m_texturePool.Get(_texh).pUAV : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= shaderRW == eShaderRW::Compute ? ePipelineDirty::CS_UnorderedAccessView : ePipelineDirty::PS_UnorderedAccessView;
}

void Graphics::SetBuffer(
    const StorageBufferHandle _sbh,
    const eShader             _shader,
    const int                 _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxReadSlots, "Shader resource slot is out of range.");

    const Resource resource { _sbh.GetValue(), eResource::Buffer };

    ReadBind& bind = m_readBind[_shader];
    if (bind.resources[_slot] == resource)
    {
        return;
    }

    bind.resources[_slot]      = resource;
    bind.d3d11Resources[_slot] = _sbh ? m_storageBufferPool.Get(_sbh).pSRV : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= ToSrvDirty_(_shader);
}

void Graphics::SetBufferRW(
    const StorageBufferHandle _sbh,
    const eShader             _shader,
    const int                 _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxReadWriteSlots, "Unordered access slot is out of range.");

    JUG_ASSERT(_shader == eShader::Pixel || _shader == eShader::Compute, "Only pixel and compute shaders can bind UAVs.");

    const eShaderRW shaderRW = _shader == eShader::Compute ? eShaderRW::Compute : eShaderRW::Pixel;
    const Resource  resource { _sbh.GetValue(), eResource::Buffer };

    ReadWriteBind& bind = m_readWriteBind[shaderRW];
    if (bind.resources[_slot] == resource)
    {
        return;
    }

    bind.resources[_slot]      = resource;
    bind.d3d11Resources[_slot] = _sbh ? m_storageBufferPool.Get(_sbh).pUAV : nullptr;
    bind.MarkDirty(_slot);

    m_dirtyFlags |= shaderRW == eShaderRW::Compute ? ePipelineDirty::CS_UnorderedAccessView : ePipelineDirty::PS_UnorderedAccessView;
}

void Graphics::SetSampler(
    const Flags<eSampler> _flags,
    const eShader         _shader,
    const int             _slot)
{
    SetSampler(_flags, RGBA::kZero, _shader, _slot);
}

void Graphics::SetSampler(
    const Flags<eSampler> _flags,
    const RGBA            _borderColor,
    const eShader         _shader,
    const int             _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxSamplerSlots, "Sampler slot is out of range.");

    const Sampler sampler { _flags, _borderColor };

    SamplerBind& bind = m_samplerBind[_shader];
    if (bind.resources[_slot] == sampler)
    {
        return;
    }

    bind.resources[_slot]      = sampler;
    bind.d3d11Resources[_slot] = GetOrCreateSamplerState_(_flags, _borderColor);
    bind.MarkDirty(_slot);

    m_dirtyFlags |= ToSamplerDirty_(_shader);
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

    const Flags<eRenderState> before = m_renderStateFlags;
    m_renderStateFlags               = _flags;

    if (FilterTopology(before) != FilterTopology(_flags))
    {
        m_dirtyFlags |= ePipelineDirty::PrimitiveTopology;
    }

    m_dirtyFlags |= { ePipelineDirty::RasterizerState, ePipelineDirty::BlendState, ePipelineDirty::DepthStencilState };
}

void Graphics::SetBlend(
    const Flags<eBlend> _flags,
    const int           _slot)
{
    JUG_ASSERT(_slot >= 0 && _slot < kNumMaxRenderTargetSlots, "Render target slot is out of range.");

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
    const Flags<eStencil> _frontFace,
    const Flags<eStencil> _backFace,
    const uint8_t         _stencilRef)
{
    if (m_fstencilFlags == _frontFace && m_bstencilFlags == _backFace && m_stencilRef == _stencilRef)
    {
        return;
    }

    m_fstencilFlags = _frontFace;
    m_bstencilFlags = _backFace;
    m_stencilRef    = _stencilRef;
    m_dirtyFlags |= ePipelineDirty::DepthStencilState;
}

// ===========================================
//  Program & Output
// ===========================================

void Graphics::SetProgram(
    const ProgramHandle _phOrNull)
{
    if (m_ph == _phOrNull)
    {
        return;
    }

    m_ph = _phOrNull;
    m_dirtyFlags |= ePipelineDirty::Program;
}

void Graphics::SetComputeProgram(
    const ProgramHandle _phOrNull)
{
    if (m_computePh == _phOrNull)
    {
        return;
    }

    m_computePh = _phOrNull;
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
    if (m_viewportX == _x && m_viewportY == _y && m_viewportW == _width && m_viewportH == _height)
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
    m_numVertexBuffers   = 0;
    m_numVertices        = 0;

    m_pD3d11IndexBuffer = nullptr;
    m_dxgiIndexFormat   = DXGI_FORMAT_UNKNOWN;
    m_indexOffset       = 0;
    m_numIndices        = 0;
    m_ibh               = kNullHandle;

    m_pD3d11InstanceBuffer = nullptr;
    m_instanceStride       = 0;
    m_instanceOffset       = 0;
    m_instanceVbh          = kNullHandle;
    m_numInstances         = 0;

    m_pD3d11InputLayout = nullptr;

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
    const uint32_t            _numDraws)
{
    const StorageBufferD3D11& indirectBuffer = m_storageBufferPool.Get(_indirectSbh);
    JUG_ASSERT(indirectBuffer.type == eStorageBuffer::IndirectArgs, "Indirect submit requires an indirect args buffer.");
    JUG_ASSERT(_offset % kIndirectArgsStride == 0, "Indirect args offset must be a multiple of the args stride.");

    ApplyPipeline_();

    const uint32_t maxDraws = (indirectBuffer.byteWidth - _offset) / kIndirectArgsStride;
    const uint32_t numDraws = _numDraws == kWholeSize ? maxDraws : _numDraws;
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

    const StorageBufferD3D11& indirectBuffer = m_storageBufferPool.Get(_indirectSbh);
    JUG_ASSERT(indirectBuffer.type == eStorageBuffer::IndirectArgs, "Indirect dispatch requires an indirect args buffer.");

    ApplyPipeline_();

    m_pD3d11DeviceContext->DispatchIndirect(indirectBuffer.pBuffer, _offset);
    ++m_stats.numDispatchCalls;
}

}   // namespace jug
