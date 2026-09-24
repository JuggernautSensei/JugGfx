#pragma once
#include "Texture.h"
#include "VertexLayout.h"

namespace jug
{

// ===========================================
//  Contants
// ===========================================

// Limits
constexpr uint32_t kNumMaxRenderTargetSlots = 8;
constexpr uint32_t kNumMaxAttachmentSlots   = kNumMaxRenderTargetSlots + 1;
constexpr uint32_t kNumMaxSamplerSlots      = 16;
constexpr uint32_t kNumMaxReadSlots         = 16;
constexpr uint32_t kNumMaxReadWriteSlots    = 16;
constexpr uint32_t kNumMaxCBufferSlots      = 14;
constexpr uint32_t kNumMaxStreams           = 4;

constexpr uint32_t kCBufferSizeAlign      = 16;
constexpr uint32_t kInstanceDataSizeAlign = 16;

// ===========================================
//  Enums
// ===========================================

enum class eTextureOption : uint32_t
{
    None = 0,

    // attachment
    RenderTarget = 1 << 0,
    DepthStencil = 1 << 1,

    // access
    Readback        = 1 << 2,   // can be read by CPU.
    ShaderWriteOnly = 1 << 3,   // can not be read by shader.
    ShaderReadWrite = 1 << 4,   // can be read and written by shader.

    // misc
    HasMips = 1 << 5,   // alloc mips storage. (w render_graph_detail h render_graph_detail d) ~ (1 render_graph_detail 1 render_graph_detail 1)
};

enum class eMSAA
{
    None,
    x2,
    x4,
    x8,
    x16,
};

enum class eStorageBufferOption : uint32_t
{
    None = 0,

    ShaderRead      = 1 << 0,   // can be read by shader.
    ShaderReadWrite = 1 << 1,   // can be read and written by shader.
    Dynamic         = 1 << 2,   // can be updated by CPU.
};

enum class eStorageBuffer
{
    Structured,
    Readback,
    IndirectArgs
};

enum class eShader
{
    Vertex,
    Pixel,
    Compute,
};

enum class eShaderRW
{
    Pixel,
    Compute
};

enum class eTexture
{
    Texture2D,
    Texture3D,
    TextureCube,
};

enum class eBuffer
{
    Vertex,
    Instance,
    Index,
    Storage,
    Constant,
};

enum class eResource
{
    Buffer,
    Texture,
};

enum class eCubeFace
{
    PosX,
    NegX,
    PosY,
    NegY,
    PosZ,
    NegZ,
};

enum class eSubmitParam
{
    StartVertexLocation,
    StartIndexLocation,
    StartInstanceLocation,
    BaseVertexLocation,
};

// ===========================================
//  State
// ===========================================

enum class eRenderState : uint32_t
{
    None = 0,

    // topology
    Topology_TriangleList  = 0 << 0,   // default
    Topology_TriangleStrip = 1 << 0,
    Topology_LineList      = 2 << 0,
    Topology_LineStrip     = 3 << 0,
    Topology_PointList     = 4 << 0,

    // cull mode
    Cull_None  = 0 << 4,   // default
    Cull_Front = 1 << 4,
    Cull_Back  = 2 << 4,

    // misc
    Wireframe        = 1 << 8,
    MultiSample      = 1 << 9,
    LineAA           = 1 << 10,
    Scissor          = 1 << 11,
    FrontCCW         = 1 << 12,
    AlphaToCoverage  = 1 << 13,
    IndependentBlend = 1 << 14,
    DepthClamp       = 1 << 15,
    DepthWrite       = 1 << 16,

    // depth test
    DepthTest_None         = 0 << 17,   // default
    DepthTest_Less         = 1 << 17,
    DepthTest_LessEqual    = 2 << 17,
    DepthTest_Greater      = 3 << 17,
    DepthTest_GreaterEqual = 4 << 17,
    DepthTest_Equal        = 5 << 17,
    DepthTest_NotEqual     = 6 << 17,
    DepthTest_Always       = 7 << 17,
    DepthTest_Never        = 8 << 17,
};

[[nodiscard]] Flags<eRenderState> FilterTopology(Flags<eRenderState> _flags);
[[nodiscard]] Flags<eRenderState> FilterCullMode(Flags<eRenderState> _flags);
[[nodiscard]] Flags<eRenderState> FilterDepthTest(Flags<eRenderState> _flags);

enum class eBlend : uint32_t
{
    None   = 0,
    Enable = 1 << 0,

    // src blend
    Src_Zero           = 0 << 1,   // default
    Src_One            = 1 << 1,
    Src_SrcColor       = 2 << 1,
    Src_InvSrcColor    = 3 << 1,
    Src_SrcAlpha       = 4 << 1,
    Src_InvSrcAlpha    = 5 << 1,
    Src_DstAlpha       = 6 << 1,
    Src_InvDstAlpha    = 7 << 1,
    Src_DstColor       = 8 << 1,
    Src_InvDstColor    = 9 << 1,
    Src_SrcAlphaSat    = 10 << 1,
    Src_BlendFactor    = 11 << 1,
    Src_InvBlendFactor = 12 << 1,

    // dst blend
    Dst_Zero           = 0 << 5,   // default
    Dst_One            = 1 << 5,
    Dst_SrcColor       = 2 << 5,
    Dst_InvSrcColor    = 3 << 5,
    Dst_SrcAlpha       = 4 << 5,
    Dst_InvSrcAlpha    = 5 << 5,
    Dst_DstAlpha       = 6 << 5,
    Dst_InvDstAlpha    = 7 << 5,
    Dst_DstColor       = 8 << 5,
    Dst_InvDstColor    = 9 << 5,
    Dst_SrcAlphaSat    = 10 << 5,
    Dst_BlendFactor    = 11 << 5,
    Dst_InvBlendFactor = 12 << 5,

    // blend op
    Op_Add         = 0 << 9,   // default
    Op_Subtract    = 1 << 9,
    Op_RevSubtract = 2 << 9,
    Op_Min         = 3 << 9,
    Op_Max         = 4 << 9,

    // src blend alpha
    SrcAlpha_Zero           = 0 << 12,   // default
    SrcAlpha_One            = 1 << 12,
    SrcAlpha_SrcAlpha       = 4 << 12,
    SrcAlpha_InvSrcAlpha    = 5 << 12,
    SrcAlpha_DstAlpha       = 6 << 12,
    SrcAlpha_InvDstAlpha    = 7 << 12,
    SrcAlpha_BlendFactor    = 11 << 12,
    SrcAlpha_InvBlendFactor = 12 << 12,

    // dst blend alpha
    DstAlpha_Zero           = 0 << 16,   // default
    DstAlpha_One            = 1 << 16,
    DstAlpha_SrcAlpha       = 4 << 16,
    DstAlpha_InvSrcAlpha    = 5 << 16,
    DstAlpha_DstAlpha       = 6 << 16,
    DstAlpha_InvDstAlpha    = 7 << 16,
    DstAlpha_BlendFactor    = 11 << 16,
    DstAlpha_InvBlendFactor = 12 << 16,

    // blend op alpha
    OpAlpha_Add         = 0 << 20,   // default
    OpAlpha_Subtract    = 1 << 20,
    OpAlpha_RevSubtract = 2 << 20,
    OpAlpha_Min         = 3 << 20,
    OpAlpha_Max         = 4 << 20,

    // write
    Write_R   = 1 << 23,
    Write_G   = 1 << 24,
    Write_B   = 1 << 25,
    Write_A   = 1 << 26,
    Write_All = Write_R | Write_G | Write_B | Write_A,
};

[[nodiscard]] Flags<eBlend> FilterSrc(Flags<eBlend> _flags);
[[nodiscard]] Flags<eBlend> FilterDst(Flags<eBlend> _flags);
[[nodiscard]] Flags<eBlend> FilterOp(Flags<eBlend> _flags);
[[nodiscard]] Flags<eBlend> FilterSrcAlpha(Flags<eBlend> _flags);
[[nodiscard]] Flags<eBlend> FilterDstAlpha(Flags<eBlend> _flags);
[[nodiscard]] Flags<eBlend> FilterOpAlpha(Flags<eBlend> _flags);

enum class eStencil : uint32_t
{
    None = 0,

    // stencil (render_graph_detail) depth (o)
    StencilFail_DepthPass_Keep    = 0 << 0,   // default
    StencilFail_DepthPass_Zero    = 1 << 0,
    StencilFail_DepthPass_Replace = 2 << 0,
    StencilFail_DepthPass_IncrSat = 3 << 0,
    StencilFail_DepthPass_DecrSat = 4 << 0,
    StencilFail_DepthPass_Invert  = 5 << 0,
    StencilFail_DepthPass_Incr    = 6 << 0,
    StencilFail_DepthPass_Decr    = 7 << 0,

    // stencil (o) depth (render_graph_detail)
    StencilPass_DepthFail_Keep    = 0 << 3,   // default
    StencilPass_DepthFail_Zero    = 1 << 3,
    StencilPass_DepthFail_Replace = 2 << 3,
    StencilPass_DepthFail_IncrSat = 3 << 3,
    StencilPass_DepthFail_DecrSat = 4 << 3,
    StencilPass_DepthFail_Invert  = 5 << 3,
    StencilPass_DepthFail_Incr    = 6 << 3,
    StencilPass_DepthFail_Decr    = 7 << 3,

    // stencil (o) depth (o)
    StencilPass_DepthPass_Keep    = 0 << 6,   // default
    StencilPass_DepthPass_Zero    = 1 << 6,
    StencilPass_DepthPass_Replace = 2 << 6,
    StencilPass_DepthPass_IncrSat = 3 << 6,
    StencilPass_DepthPass_DecrSat = 4 << 6,
    StencilPass_DepthPass_Invert  = 5 << 6,
    StencilPass_DepthPass_Incr    = 6 << 6,
    StencilPass_DepthPass_Decr    = 7 << 6,

    // compare
    Compare_None         = 0 << 9,   // default
    Compare_Less         = 1 << 9,
    Compare_LessEqual    = 2 << 9,
    Compare_Greater      = 3 << 9,
    Compare_GreaterEqual = 4 << 9,
    Compare_Equal        = 5 << 9,
    Compare_NotEqual     = 6 << 9,
    Compare_Always       = 7 << 9,
    Compare_Never        = 8 << 9,
};

[[nodiscard]] Flags<eStencil> FilterStencilFailDepthPass(Flags<eStencil> _flags);
[[nodiscard]] Flags<eStencil> FilterStencilPassDepthFail(Flags<eStencil> _flags);
[[nodiscard]] Flags<eStencil> FilterStencilPassDepthPass(Flags<eStencil> _flags);
[[nodiscard]] Flags<eStencil> FilterCompare(Flags<eStencil> _flags);

enum class eSampler : uint32_t
{
    None = 0,

    // filter
    Filter_MinPoint_MagPoint_MipPoint    = 0 << 0,   // default
    Filter_MinPoint_MagPoint_MipLinear   = 1 << 0,
    Filter_MinPoint_MagLinear_MipPoint   = 2 << 0,
    Filter_MinPoint_MagLinear_MipLinear  = 3 << 0,
    Filter_MinLinear_MagPoint_MipPoint   = 4 << 0,
    Filter_MinLinear_MagPoint_MipLinear  = 5 << 0,
    Filter_MinLinear_MagLinear_MipPoint  = 6 << 0,
    Filter_MinLinear_MagLinear_MipLinear = 7 << 0,
    Filter_Anisotropic4                  = 8 << 0,
    Filter_Anisotropic8                  = 9 << 0,
    Filter_Anisotropic16                 = 10 << 0,

    // u
    U_Wrap       = 0 << 4,   // default
    U_Mirror     = 1 << 4,
    U_Clamp      = 2 << 4,
    U_Border     = 3 << 4,
    U_MirrorOnce = 4 << 4,

    // v
    V_Wrap       = 0 << 7,   // default
    V_Mirror     = 1 << 7,
    V_Clamp      = 2 << 7,
    V_Border     = 3 << 7,
    V_MirrorOnce = 4 << 7,

    // w
    W_Wrap       = 0 << 10,   // default
    W_Mirror     = 1 << 10,
    W_Clamp      = 2 << 10,
    W_Border     = 3 << 10,
    W_MirrorOnce = 4 << 10,

    // compare
    Compare_None         = 0 << 13,   // default
    Compare_Less         = 1 << 13,
    Compare_LessEqual    = 2 << 13,
    Compare_Greater      = 3 << 13,
    Compare_GreaterEqual = 4 << 13,
    Compare_Equal        = 5 << 13,
    Compare_NotEqual     = 6 << 13,
    Compare_Always       = 7 << 13,
    Compare_Never        = 8 << 13,
};

[[nodiscard]] Flags<eSampler> FilterFilter(Flags<eSampler> _flags);
[[nodiscard]] Flags<eSampler> FilterU(Flags<eSampler> _flags);
[[nodiscard]] Flags<eSampler> FilterV(Flags<eSampler> _flags);
[[nodiscard]] Flags<eSampler> FilterW(Flags<eSampler> _flags);
[[nodiscard]] Flags<eSampler> FilterCompare(Flags<eSampler> _flags);

// ===========================================
//  Handle
// ===========================================

struct VertexBufferDesc;
struct InstanceBufferDesc;
struct IndexBufferDesc;
struct StorageBufferDesc;
struct ConstantBufferDesc;
struct TextureDesc;
struct FrameBufferDesc;
struct ShaderDesc;
struct ProgramDesc;

using VertexLayoutHandle   = Handle<VertexLayout>;
using VertexBufferHandle   = Handle<VertexBufferDesc>;
using InstanceBufferHandle = Handle<InstanceBufferDesc>;
using IndexBufferHandle    = Handle<IndexBufferDesc>;
using StorageBufferHandle  = Handle<StorageBufferDesc>;
using ConstantBufferHandle = Handle<ConstantBufferDesc>;
using TextureHandle        = Handle<TextureDesc>;
using FrameBufferHandle    = Handle<FrameBufferDesc>;
using ShaderHandle         = Handle<ShaderDesc>;
using ProgramHandle        = Handle<ProgramDesc>;

// ===========================================
//  ResourceRef
// ===========================================

struct VertexLayoutDesc
{
    VertexLayout vl       = {};
    uint32_t     refCount = 0;
};

struct VertexBufferDesc
{
    uint32_t           byteWidth   = 0;
    uint32_t           stride      = 0;
    uint32_t           numVertices = 0;
    VertexLayoutHandle vlh         = kNullHandle;
    bool               bDynamic    = false;
};

struct InstanceBufferDesc
{
    uint32_t byteWidth    = 0;
    uint32_t stride       = 0;
    uint32_t numInstances = 0;
};

struct IndexBufferDesc
{
    uint32_t byteWidth  = 0;
    uint32_t numIndices = 0;
    bool     bU32       = false;
    bool     bDynamic   = false;
};

struct StorageBufferDesc
{
    uint32_t                    byteWidth = 0;
    uint32_t                    stride    = 0;
    eStorageBuffer              type      = eStorageBuffer::Structured;
    Flags<eStorageBufferOption> flags     = eStorageBufferOption::None;
};

struct ConstantBufferDesc
{
    uint32_t byteWidth = 0;
};

struct TextureDesc
{
    uint32_t              width      = 0;
    uint32_t              height     = 0;
    uint32_t              depth      = 0;
    eTexture              type       = eTexture::Texture2D;
    eTextureFormat        format     = eTextureFormat::Unknown;
    uint32_t              numLayers  = 1;
    uint32_t              numMips    = 1;
    eMSAA                 msaa       = eMSAA::None;
    uint32_t              refCount   = 0;
    bool                  bImmutable = false;
    Flags<eTextureOption> flags      = eTextureOption::None;
};

struct Attachment
{
    TextureHandle texh      = kNullHandle;
    uint32_t      mip       = 0;
    uint32_t      offset    = 0;
    uint32_t      numLayers = 1;   // [offset, offset + numLayers)
};

struct FrameBufferDesc
{
    [[nodiscard]] uint32_t GetNumAttachments() const
    {
        return numRts + (bHasDepth ? 1 : 0);
    }

    [[nodiscard]] Attachment GetDepthAttachment() const
    {
        return atts[numRts];
    }

    ARRAY<Attachment, kNumMaxAttachmentSlots> atts      = {};
    uint32_t                                  numRts    = 0;
    bool                                      bHasDepth = false;
    bool                                      bMSAA     = false;

    // swap chain
    SDL_WindowID wndID      = {};
    uint32_t     numBuffers = 0;
    bool         bVSync     = false;
};

struct ShaderDesc
{
    eShader  type     = eShader::Vertex;
    uint32_t refCount = 0;
    uint64_t hash     = 0;
};

struct ProgramDesc
{
    ShaderHandle vsh = kNullHandle;
    ShaderHandle psh = kNullHandle;
    ShaderHandle csh = kNullHandle;
};

// ===========================================
//  Misc
// ===========================================

class ResourceRef
{
public:
    ResourceRef() = default;
    /* implicit */ ResourceRef(TextureHandle _texh);
    /* implicit */ ResourceRef(StorageBufferHandle _sbh);

    [[nodiscard]] eResource GetType() const;
    [[nodiscard]] bool      IsNull() const;

    [[nodiscard]] bool operator==(ResourceRef _other) const;
    explicit           operator bool() const;

    [[nodiscard]] TextureHandle       GetTextureHandle() const;
    [[nodiscard]] StorageBufferHandle GetStorageBufferHandle() const;

private:
    union
    {
        TextureHandle       m_texh = kNullHandle;
        StorageBufferHandle m_sbh;
    };
    eResource m_type = eResource::Texture;
};

class BufferRef
{
public:
    BufferRef();
    /* implicit */ BufferRef(VertexBufferHandle _vbh);
    /* implicit */ BufferRef(InstanceBufferHandle _instbh);
    /* implicit */ BufferRef(IndexBufferHandle _ibh);
    /* implicit */ BufferRef(ConstantBufferHandle _cbh);
    /* implicit */ BufferRef(StorageBufferHandle _sbh);

    [[nodiscard]] eBuffer GetType() const;
    [[nodiscard]] bool    IsNull() const;

    [[nodiscard]] bool operator==(BufferRef _other) const;
    explicit           operator bool() const;

    [[nodiscard]] VertexBufferHandle   GetVertexBufferHandle() const;
    [[nodiscard]] InstanceBufferHandle GetInstanceBufferHandle() const;
    [[nodiscard]] IndexBufferHandle    GetIndexBufferHandle() const;
    [[nodiscard]] ConstantBufferHandle GetConstantBufferHandle() const;
    [[nodiscard]] StorageBufferHandle  GetStorageBufferHandle() const;

private:
    JUG_DISABLE_ANON_WARNING_BEGIN
    union
    {
        VertexBufferHandle   m_vbh;
        InstanceBufferHandle m_instbh;
        IndexBufferHandle    m_ibh;
        ConstantBufferHandle m_cbh;
        StorageBufferHandle  m_sbh;
    };
    JUG_DISABLE_ANON_WARNING_END

    eBuffer m_type = eBuffer::Vertex;
};

struct VertexStream
{
    VertexBufferHandle vbh    = kNullHandle;
    uint32_t           offset = 0;   // 버텍스 단위 오프셋
};

struct GraphicsCaps
{
    // gpu
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;

    // memory
    uint64_t videoMemorySize        = 0;   // byte
    uint64_t systemMemorySize       = 0;   // byte
    uint64_t sharedSystemMemorySize = 0;   // byte

    // rendering spec
    bool bAllowTearing       = false;
    bool bSoftwareRasterizer = false;

    // runtime option
    bool bDebugLayerEnabled = false;
};

struct GraphicsStats
{
    int numDrawCalls     = 0;
    int numDispatchCalls = 0;
    int numResolves      = 0;

    uint64_t gpuTimerBegin   = 0;
    uint64_t gpuTimerEnd     = 0;
    uint64_t gpuTimerFreq    = 0;
    uint64_t gpuTimerLatency = 0;   // frame

    uint64_t gpuMemoryUsed = 0;   // byte
    uint64_t gpuMemorySize = 0;   // byte
};

}   // namespace jug