#include "pch.h"

#include "Base.h"

namespace jug
{

namespace
{
    constexpr Flags<eRenderState> kTopologyMask { MakeFieldMask(eRenderState::Topology_TriangleStrip, eRenderState::Topology_PointList) };
    constexpr Flags<eRenderState> kCullModeMask { MakeFieldMask(eRenderState::Cull_Front, eRenderState::Cull_Back) };
    constexpr Flags<eRenderState> kDepthTestMask { MakeFieldMask(eRenderState::DepthTest_Less, eRenderState::DepthTest_Never) };

    constexpr Flags<eBlend> kSrcMask { MakeFieldMask(eBlend::Src_One, eBlend::Src_InvBlendFactor) };
    constexpr Flags<eBlend> kDstMask { MakeFieldMask(eBlend::Dst_One, eBlend::Dst_InvBlendFactor) };
    constexpr Flags<eBlend> kOpMask { MakeFieldMask(eBlend::Op_Subtract, eBlend::Op_Max) };
    constexpr Flags<eBlend> kSrcAlphaMask { MakeFieldMask(eBlend::SrcAlpha_One, eBlend::SrcAlpha_InvBlendFactor) };
    constexpr Flags<eBlend> kDstAlphaMask { MakeFieldMask(eBlend::DstAlpha_One, eBlend::DstAlpha_InvBlendFactor) };
    constexpr Flags<eBlend> kOpAlphaMask { MakeFieldMask(eBlend::OpAlpha_Subtract, eBlend::OpAlpha_Max) };

    constexpr Flags<eStencil> kStencilFailDepthPassMask { MakeFieldMask(eStencil::StencilFail_DepthPass_Zero, eStencil::StencilFail_DepthPass_Decr) };
    constexpr Flags<eStencil> kStencilPassDepthFailMask { MakeFieldMask(eStencil::StencilPass_DepthFail_Zero, eStencil::StencilPass_DepthFail_Decr) };
    constexpr Flags<eStencil> kStencilFailDepthFailMask { MakeFieldMask(eStencil::StencilPass_DepthPass_Zero, eStencil::StencilPass_DepthPass_Decr) };
    constexpr Flags<eStencil> kStencilCompareMask { MakeFieldMask(eStencil::Compare_Less, eStencil::Compare_Never) };

    constexpr Flags<eSampler> kFilterMask { MakeFieldMask(eSampler::Filter_MinLinear, eSampler::Filter_Anisotropic16) };
    constexpr Flags<eSampler> kUMask { MakeFieldMask(eSampler::U_Mirror, eSampler::U_MirrorOnce) };
    constexpr Flags<eSampler> kVMask { MakeFieldMask(eSampler::V_Mirror, eSampler::V_MirrorOnce) };
    constexpr Flags<eSampler> kWMask { MakeFieldMask(eSampler::W_Mirror, eSampler::W_MirrorOnce) };
    constexpr Flags<eSampler> kSamplerCompareMask { MakeFieldMask(eSampler::Compare_Less, eSampler::Compare_Never) };
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

ResourceRef::ResourceRef(
    const TextureHandle _texh)
    : m_texh(_texh)
    , m_type(eResource::Texture)
{
}

ResourceRef::ResourceRef(
    const StorageBufferHandle _sbh)
    : m_sbh(_sbh)
    , m_type(eResource::Buffer)
{
}

eResource ResourceRef::GetType() const
{
    return m_type;
}

bool ResourceRef::IsInvalid() const
{
    return m_texh.IsInvalid();
}

bool ResourceRef::operator==(const ResourceRef _other) const
{
    return m_type == _other.m_type && m_texh.GetValue() == _other.m_texh.GetValue();
}

ResourceRef::operator bool() const
{
    return !IsInvalid();
}

TextureHandle ResourceRef::GetTextureHandle() const
{
    return m_texh;
}

StorageBufferHandle ResourceRef::GetStorageBufferHandle() const
{
    return m_sbh;
}

BufferRef::BufferRef()
    : m_vbh(kInvalidHandle)
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

bool BufferRef::IsInvalid() const
{
    return m_vbh.IsInvalid();
}

bool BufferRef::operator==(
    const BufferRef _other) const
{
    return m_type == _other.m_type && m_vbh.GetValue() == _other.m_vbh.GetValue();
}

BufferRef::operator bool() const
{
    return !IsInvalid();
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
