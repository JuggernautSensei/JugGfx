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

    constexpr Flags<eRenderState> kTopologyMask { MakeFieldMask_(eRenderState::Topology_TriangleStrip, eRenderState::Topology_PointList) };
    constexpr Flags<eRenderState> kCullModeMask { MakeFieldMask_(eRenderState::Cull_Front, eRenderState::Cull_Back) };
    constexpr Flags<eRenderState> kDepthTestMask { MakeFieldMask_(eRenderState::DepthTest_Less, eRenderState::DepthTest_Never) };

    constexpr Flags<eBlend> kSrcMask { MakeFieldMask_(eBlend::Src_One, eBlend::Src_InvBlendFactor) };
    constexpr Flags<eBlend> kDstMask { MakeFieldMask_(eBlend::Dst_One, eBlend::Dst_InvBlendFactor) };
    constexpr Flags<eBlend> kOpMask { MakeFieldMask_(eBlend::Op_Subtract, eBlend::Op_Max) };
    constexpr Flags<eBlend> kSrcAlphaMask { MakeFieldMask_(eBlend::SrcAlpha_One, eBlend::SrcAlpha_InvBlendFactor) };
    constexpr Flags<eBlend> kDstAlphaMask { MakeFieldMask_(eBlend::DstAlpha_One, eBlend::DstAlpha_InvBlendFactor) };
    constexpr Flags<eBlend> kOpAlphaMask { MakeFieldMask_(eBlend::OpAlpha_Subtract, eBlend::OpAlpha_Max) };

    constexpr Flags<eStencil> kStencilFailDepthPassMask { MakeFieldMask_(eStencil::StencilFail_DepthPass_Zero, eStencil::StencilFail_DepthPass_Decr) };
    constexpr Flags<eStencil> kStencilPassDepthFailMask { MakeFieldMask_(eStencil::StencilPass_DepthFail_Zero, eStencil::StencilPass_DepthFail_Decr) };
    constexpr Flags<eStencil> kStencilFailDepthFailMask { MakeFieldMask_(eStencil::StencilPass_DepthPass_Zero, eStencil::StencilPass_DepthPass_Decr) };
    constexpr Flags<eStencil> kStencilCompareMask { MakeFieldMask_(eStencil::Compare_Less, eStencil::Compare_Never) };

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

// ===========================================
//  Buffer Ref
// ===========================================

BufferRef::BufferRef()
    : m_vbh(kNullHandle)
    , m_type(eBuffer::Vertex)
{
}

BufferRef::BufferRef(
    const VertexBufferHandle _vbh)
    : m_vbh(_vbh)
    , m_type(eBuffer::Vertex)
{
}

BufferRef::BufferRef(
    const InstanceBufferHandle _instbh)
    : m_instbh(_instbh)
    , m_type(eBuffer::Instance)
{
}

BufferRef::BufferRef(
    const IndexBufferHandle _ibh)
    : m_ibh(_ibh)
    , m_type(eBuffer::Index)
{
}

BufferRef::BufferRef(
    const ConstantBufferHandle _cbh)
    : m_cbh(_cbh)
    , m_type(eBuffer::Constant)
{
}

BufferRef::BufferRef(
    const StorageBufferHandle _sbh)
    : m_sbh(_sbh)
    , m_type(eBuffer::Storage)
{
}

eBuffer BufferRef::GetType() const
{
    return m_type;
}

bool BufferRef::IsNull() const
{
    // 모든 핸들이 같은 레이아웃이라 어느 멤버로 봐도 결과가 같다.
    return m_vbh.IsNull();
}

BufferRef::operator bool() const
{
    return !IsNull();
}

VertexBufferHandle BufferRef::GetVertexBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Vertex, "The buffer is not a vertex buffer.\n");
    return m_vbh;
}

InstanceBufferHandle BufferRef::GetInstanceBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Instance, "The buffer is not an instance buffer.\n");
    return m_instbh;
}

IndexBufferHandle BufferRef::GetIndexBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Index, "The buffer is not an index buffer.\n");
    return m_ibh;
}

ConstantBufferHandle BufferRef::GetConstantBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Constant, "The buffer is not a constant buffer.\n");
    return m_cbh;
}

StorageBufferHandle BufferRef::GetStorageBufferHandle() const
{
    JUG_ASSERT(m_type == eBuffer::Storage, "The buffer is not a storage buffer.\n");
    return m_sbh;
}

}   // namespace jug
