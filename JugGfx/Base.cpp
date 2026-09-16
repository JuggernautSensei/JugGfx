#include "pch.h"

#include <bit>

#include "Base.h"

namespace jug
{

namespace
{
    template<EnumT E, typename U = UnderlyingT<E>>
    [[nodiscard]] constexpr U MakeFieldMask_(
        const E _lowestNotZero,
        const E _highest)
    {
        JUG_ASSERT(static_cast<U>(_lowestNotZero) > 0, "_lowestNotZero must be greater than zero");
        JUG_ASSERT(static_cast<U>(_lowestNotZero) <= static_cast<U>(_highest), "_lowestNotZero must not exceed _highest");

        const U high = static_cast<U>(std::bit_ceil(static_cast<U>(static_cast<U>(_highest) + 1)) - 1);
        const U low  = static_cast<U>(std::bit_floor(static_cast<U>(_lowestNotZero)) - 1);
        return static_cast<U>(high ^ low);
    }

    template<EnumT E>
    [[nodiscard]] constexpr uint32_t ExtractField_(
        const Flags<E> _state,
        const Flags<E> _mask,
        const uint32_t _shift)
    {
        return static_cast<uint32_t>((_state & _mask).GetFlags()) >> _shift;
    }

    constexpr uint32_t kTopologyShift  = 0;
    constexpr uint32_t kCullModeShift  = 4;
    constexpr uint32_t kDepthTestShift = 17;

    constexpr Flags<eRenderState> kTopologyMask { MakeFieldMask_(eRenderState::Topology_TriangleStrip, eRenderState::Topology_PointList) };
    constexpr Flags<eRenderState> kCullModeMask { MakeFieldMask_(eRenderState::Cull_Front, eRenderState::Cull_Back) };
    constexpr Flags<eRenderState> kDepthTestMask { MakeFieldMask_(eRenderState::DepthTest_Less, eRenderState::DepthTest_Never) };

    constexpr uint32_t kSrcShift       = 1;
    constexpr uint32_t kDstShift       = 5;
    constexpr uint32_t kOpShift        = 9;
    constexpr uint32_t kSrcAlphaShift  = 12;
    constexpr uint32_t kDstAlphaShift  = 16;
    constexpr uint32_t kOpAlphaShift   = 20;
    constexpr uint32_t kWriteMaskShift = 23;

    constexpr Flags<eBlend> kSrcMask { MakeFieldMask_(eBlend::Src_One, eBlend::Src_InvBlendFactor) };
    constexpr Flags<eBlend> kDstMask { MakeFieldMask_(eBlend::Dst_One, eBlend::Dst_InvBlendFactor) };
    constexpr Flags<eBlend> kOpMask { MakeFieldMask_(eBlend::Op_Subtract, eBlend::Op_Max) };
    constexpr Flags<eBlend> kSrcAlphaMask { MakeFieldMask_(eBlend::SrcAlpha_One, eBlend::SrcAlpha_InvBlendFactor) };
    constexpr Flags<eBlend> kDstAlphaMask { MakeFieldMask_(eBlend::DstAlpha_One, eBlend::DstAlpha_InvBlendFactor) };
    constexpr Flags<eBlend> kOpAlphaMask { MakeFieldMask_(eBlend::OpAlpha_Subtract, eBlend::OpAlpha_Max) };
    constexpr Flags<eBlend> kWriteMaskMask { MakeFieldMask_(eBlend::Write_R, eBlend::Write_A) };

    constexpr uint32_t kStencilFailDepthPassShift = 0;
    constexpr uint32_t kStencilPassDepthFailShift = 3;
    constexpr uint32_t kStencilFailDepthFailShift = 6;
    constexpr uint32_t kStencilCompareShift       = 9;

    constexpr Flags<eStencil> kStencilFailDepthPassMask { MakeFieldMask_(eStencil::StencilFail_DepthPass_Zero, eStencil::StencilFail_DepthPass_Decr) };
    constexpr Flags<eStencil> kStencilPassDepthFailMask { MakeFieldMask_(eStencil::StencilPass_DepthFail_Zero, eStencil::StencilPass_DepthFail_Decr) };
    // [AI] eStencil 에 StencilFail_DepthFail_* 은 없다. shift 6 자리의 실제 이름은 StencilPass_DepthPass_* 다.
    constexpr Flags<eStencil> kStencilFailDepthFailMask { MakeFieldMask_(eStencil::StencilPass_DepthPass_Zero, eStencil::StencilPass_DepthPass_Decr) };
    constexpr Flags<eStencil> kStencilCompareMask { MakeFieldMask_(eStencil::Compare_Less, eStencil::Compare_Never) };

    constexpr uint32_t kFilterShift         = 0;
    constexpr uint32_t kUShift              = 4;
    constexpr uint32_t kVShift              = 7;
    constexpr uint32_t kWShift              = 10;
    constexpr uint32_t kSamplerCompareShift = 13;

    constexpr Flags<eSampler> kFilterMask { MakeFieldMask_(eSampler::Filter_MinPoint_MagPoint_MipLinear, eSampler::Filter_Anisotropic16) };
    constexpr Flags<eSampler> kUMask { MakeFieldMask_(eSampler::U_Mirror, eSampler::U_MirrorOnce) };
    constexpr Flags<eSampler> kVMask { MakeFieldMask_(eSampler::V_Mirror, eSampler::V_MirrorOnce) };
    constexpr Flags<eSampler> kWMask { MakeFieldMask_(eSampler::W_Mirror, eSampler::W_MirrorOnce) };
    constexpr Flags<eSampler> kSamplerCompareMask { MakeFieldMask_(eSampler::Compare_Less, eSampler::Compare_Never) };
}   // namespace

// ===========================================
//  Render State
// ===========================================

Flags<eRenderState> FilterTopology(
    const Flags<eRenderState> _flags)
{
    return _flags & kTopologyMask;
}

Flags<eRenderState> FilterCullMode(
    const Flags<eRenderState> _flags)
{
    return _flags & kCullModeMask;
}

Flags<eRenderState> FilterDepthTest(
    const Flags<eRenderState> _flags)
{
    return _flags & kDepthTestMask;
}

// ===========================================
//  Blend
// ===========================================

Flags<eBlend> FilterSrc(
    const Flags<eBlend> _flags)
{
    return _flags & kSrcMask;
}

Flags<eBlend> FilterDst(
    const Flags<eBlend> _flags)
{
    return _flags & kDstMask;
}

Flags<eBlend> FilterOp(
    const Flags<eBlend> _flags)
{
    return _flags & kOpMask;
}

Flags<eBlend> FilterSrcAlpha(
    const Flags<eBlend> _flags)
{
    return _flags & kSrcAlphaMask;
}

Flags<eBlend> FilterDstAlpha(
    const Flags<eBlend> _flags)
{
    return _flags & kDstAlphaMask;
}

Flags<eBlend> FilterOpAlpha(
    const Flags<eBlend> _flags)
{
    return _flags & kOpAlphaMask;
}

// ===========================================
//  Stencil
// ===========================================

Flags<eStencil> FilterStencilFailDepthPass(
    const Flags<eStencil> _flags)
{
    return _flags & kStencilFailDepthPassMask;
}

Flags<eStencil> FilterStencilPassDepthFail(
    const Flags<eStencil> _flags)
{
    return _flags & kStencilPassDepthFailMask;
}

Flags<eStencil> FilterStencilPassDepthPass(
    const Flags<eStencil> _flags)
{
    return _flags & kStencilFailDepthFailMask;
}

Flags<eStencil> FilterCompare(
    const Flags<eStencil> _flags)
{
    return _flags & kStencilCompareMask;
}

// ===========================================
//  Sampler
// ===========================================

Flags<eSampler> FilterFilter(
    const Flags<eSampler> _flags)
{
    return _flags & kFilterMask;
}

Flags<eSampler> FilterU(
    const Flags<eSampler> _flags)
{
    return _flags & kUMask;
}

Flags<eSampler> FilterV(
    const Flags<eSampler> _flags)
{
    return _flags & kVMask;
}

Flags<eSampler> FilterW(
    const Flags<eSampler> _flags)
{
    return _flags & kWMask;
}

Flags<eSampler> FilterCompare(
    const Flags<eSampler> _flags)
{
    return _flags & kSamplerCompareMask;
}

AnyBufferHandle::AnyBufferHandle()
    : m_vbh(kNullHandle)
    , m_type(eBuffer::Vertex)
{
}

AnyBufferHandle::AnyBufferHandle(
    const NullHandleType)
    : m_vbh(kNullHandle)
    , m_type(eBuffer::Vertex)
{
}

AnyBufferHandle::AnyBufferHandle(
    const VertexBufferHandle _vbh)
    : m_vbh(_vbh)
    , m_type(eBuffer::Vertex)
{
}

AnyBufferHandle::AnyBufferHandle(
    const IndexBufferHandle _ibh)
    : m_ibh(_ibh)
    , m_type(eBuffer::Index)
{
}

AnyBufferHandle::AnyBufferHandle(
    const StorageBufferHandle _sbh)
    : m_sbh(_sbh)
    , m_type(eBuffer::Storage)
{
}

AnyBufferHandle::AnyBufferHandle(
    const ConstantBufferHandle _cbh)
    : m_cbh(_cbh)
    , m_type(eBuffer::Constant)
{
}

bool AnyBufferHandle::IsNull() const
{
    return m_vbh == kNullHandle;
}

AnyBufferHandle::operator bool() const
{
    return !IsNull();
}

eBuffer AnyBufferHandle::GetType() const
{
    return m_type;
}

VertexBufferHandle AnyBufferHandle::GetVertexBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Vertex, "AnyBufferHandle is not a VertexBufferHandle");
    return m_vbh;
}

IndexBufferHandle AnyBufferHandle::GetIndexBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Index, "AnyBufferHandle is not a IndexBufferHandle");
    return m_ibh;
}

StorageBufferHandle AnyBufferHandle::GetStorageBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Storage, "AnyBufferHandle is not a StorageBufferHandle");
    return m_sbh;
}

ConstantBufferHandle AnyBufferHandle::GetConstantBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Constant, "AnyBufferHandle is not a ConstantBufferHandle");
    return m_cbh;
}

}   // namespace jug
