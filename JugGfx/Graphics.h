#pragma once
// ===========================================================================
//  [AI] 2026-09-16 헤더 변경 요약  (검색: "[AI]")
//    1. 정의가 없던 BufferCreateParam / TextureCreateParam / SwapChainDesc 를 쓰던 private 선언 정리.
//       (버퍼 생성은 CreateXxxBuffer_ 들이 이미 직접 하고, 텍스처는 TextureDesc 를 그대로 받게 했다)
//    2. 삭제된 디버그 그룹 API 복구. m_pUserAnnotationOrNull 멤버만 남아 있어서 되살렸다.
//    3. InitDebugInterfacesIfNeed_ 추가. 디버그/인포큐/어노테이션 인터페이스를 채우는 곳이 없었다.
//    4. 업로드 스테이징 캐시(m_pUploadBuffer, GetOrCreateUploadBuffer_/GetOrCreateReadbackBuffer_) 제거.
//    5. TimerQuery::bIssued 추가. 발급 안 된 쿼리를 회수에서 걸러내야 한다.
//    6. CreateFrameBufferViews_ / FillAttachments_ 의 bool 반환 제거(실패하면 어차피 크래시).
// ===========================================================================

#include <JugX/EnumArray.h>
#include <JugX/MemoryView.h>
#include <JugX/NoopHasher.h>
#include <JugX/ResourcePool.h>
#include <JugX/RGBA.h>
#include <JugX/RingBuffer.h>
#include <JugX/Vector3I.h>

#include "Base.h"
#include "DXGI.h"

namespace jug
{

// ===========================================
//  Graphics
// ===========================================

class Graphics
{
    JUG_CLASS(Graphics, NO_COPY, NO_MOVE)

    // ===========================================
    //  Extended Resource
    // ===========================================

    struct VertexBufferD3D11 : public VertexBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
    };

    struct IndexBufferD3D11 : public IndexBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
        DXGI_FORMAT   format  = DXGI_FORMAT_UNKNOWN;
    };

    struct StorageBufferD3D11 : public StorageBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;

        ID3D11ShaderResourceView*  pSRV = nullptr;
        ID3D11UnorderedAccessView* pUAV = nullptr;
    };

    struct ConstantBufferD3D11 : public ConstantBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
    };

    struct TextureD3D11 : public TextureDesc
    {
        JUG_DISABLE_ANON_WARNING_BEGIN
        union
        {
            ID3D11Resource*  pResource = nullptr;
            ID3D11Texture2D* pTexture2D;
            ID3D11Texture3D* pTexture3D;
        };
        union
        {
            ID3D11Resource*  pMsaaRtResource = nullptr;
            ID3D11Texture2D* pMsaaRtTexture2D;
        };
        JUG_DISABLE_ANON_WARNING_END

        ID3D11ShaderResourceView*  pSRV = nullptr;
        ID3D11UnorderedAccessView* pUAV = nullptr;
    };

    struct FrameBufferD3D11 : public FrameBufferDesc
    {
        ARRAY<ID3D11RenderTargetView*, kNumMaxRenderTargetSlots> rtvs = {};
        ID3D11DepthStencilView*                                  pDSV = nullptr;

        bool             bOwnership     = false;
        IDXGISwapChain3* pDxgiSwapChain = nullptr;
        uint32_t         dxgiFlags      = 0;
        bool             bVSync         = true;
    };

    struct ShaderD3D11 : public ShaderDesc
    {
        JUG_DISABLE_ANON_WARNING_BEGIN
        union
        {
            ID3D11VertexShader*  pVS;
            ID3D11PixelShader*   pPS;
            ID3D11ComputeShader* pCS;
        };
        JUG_DISABLE_ANON_WARNING_END
    };

    struct VertexLayoutD3D11
    {
        VertexLayout vertexLayout = {};
        int          refCount     = 0;
    };

    // ===========================================
    //  Typedef
    // ===========================================

    using VertexBufferPool   = ResourcePool<VertexBufferHandle, VertexBufferD3D11>;
    using IndexBufferPool    = ResourcePool<IndexBufferHandle, IndexBufferD3D11>;
    using StorageBufferPool  = ResourcePool<StorageBufferHandle, StorageBufferD3D11>;
    using ConstantBufferPool = ResourcePool<ConstantBufferHandle, ConstantBufferD3D11>;
    using TexturePool        = ResourcePool<TextureHandle, TextureD3D11>;
    using FrameBufferPool    = ResourcePool<FrameBufferHandle, FrameBufferD3D11>;
    using ShaderPool         = ResourcePool<ShaderHandle, ShaderD3D11>;
    using ProgramPool        = ResourcePool<ProgramHandle, ProgramDesc>;
    using VertexLayoutPool   = ResourcePool<VertexLayoutHandle, VertexLayoutD3D11>;

    // ===========================================
    //  Pipeline
    // ===========================================

    enum class ePipelineDirty : uint32_t
    {
        None = 0,

        PrimitiveTopology = 1 << 0,
        VertexBuffer      = 1 << 1,
        IndexBuffer       = 1 << 2,
        InstanceBuffer    = 1 << 3,
        InputLayout       = 1 << 4,

        Program        = 1 << 5,
        ComputeProgram = 1 << 6,

        VS_ShaderResourceView = 1 << 7,
        PS_ShaderResourceView = 1 << 8,
        CS_ShaderResourceView = 1 << 9,

        VS_ConstantBuffer = 1 << 10,
        PS_ConstantBuffer = 1 << 11,
        CS_ConstantBuffer = 1 << 12,

        PS_UnorderedAccessView = 1 << 13,
        CS_UnorderedAccessView = 1 << 14,

        VS_SamplerState = 1 << 15,
        PS_SamplerState = 1 << 16,
        CS_SamplerState = 1 << 17,

        RasterizerState   = 1 << 18,
        BlendState        = 1 << 19,
        DepthStencilState = 1 << 20,
        Viewport          = 1 << 21,
        ScissorRect       = 1 << 22,
        FrameBuffer       = 1 << 23,
    };

    enum class eResource
    {
        Buffer,
        Texture,
    };

    enum class eShaderRW
    {
        Pixel,
        Compute
    };

    struct Resource
    {
        [[nodiscard]] bool operator==(const Resource&) const = default;

        uint32_t  handle = 0xFFFF'FFFF;
        eResource type   = eResource::Texture;
    };

    struct Sampler
    {
        [[nodiscard]] bool operator==(const Sampler& _other) const = default;

        Flags<eSampler> flags  = {};
        RGBA            border = {};
    };

    template<typename T, typename U, size_t kSize>
    struct ResourceBind
    {
        void MarkDirty(
            const int _slot)
        {
            dirtyBegin = Min(dirtyBegin, _slot);
            dirtyEnd   = Max(dirtyEnd, _slot + 1);
        }

        void ClearDirty()
        {
            dirtyBegin = Max<int>();
            dirtyEnd   = 0;
        }

        [[nodiscard]] bool IsDirty() const
        {
            return dirtyBegin < dirtyEnd;
        }

        [[nodiscard]] int NumDirties() const
        {
            return dirtyEnd - dirtyBegin;
        }

        ARRAY<T, kSize> d3d11Resources = {};
        ARRAY<U, kSize> resources      = {};

        int dirtyBegin = 0;
        int dirtyEnd   = 0;
    };

    using ReadBind      = ResourceBind<ID3D11ShaderResourceView*, Resource, kNumMaxReadSlots>;
    using ReadWriteBind = ResourceBind<ID3D11UnorderedAccessView*, Resource, kNumMaxReadWriteSlots>;
    using CBufferBind   = ResourceBind<ID3D11Buffer*, ConstantBufferHandle, kNumMaxCBufferSlots>;
    using SamplerBind   = ResourceBind<ID3D11SamplerState*, Sampler, kNumMaxSamplerSlots>;

    struct TimerQuery
    {
        ID3D11Query* pBegin    = nullptr;
        ID3D11Query* pEnd      = nullptr;
        ID3D11Query* pDisjoint = nullptr;
        bool         bIssued   = false;
    };

public:
    explicit Graphics(bool _bEnableDebugLayer);
    ~Graphics();
    [[nodiscard]] static Graphics* GetInstance();

    // ===========================================
    //  System
    // ===========================================

    void Present();

    size_t                             ReportLiveObjects();
    [[nodiscard]] const GraphicsCaps&  GetCaps() const;
    [[nodiscard]] const GraphicsStats& GetStats() const;

    // ===========================================
    //  Vertex Buffer
    // ===========================================

    [[nodiscard]] VertexBufferHandle CreateVertexBuffer(
        MemoryView          _vertexData,
        const VertexLayout& _vl);

    [[nodiscard]] VertexBufferHandle CreateDynamicVertexBuffer(
        uint32_t            _numVertices,
        const VertexLayout& _vl);

    [[nodiscard]] VertexBufferHandle CreateInstanceBuffer(
        uint32_t _numInstances,
        uint32_t _stride);

    [[nodiscard]] VertexLayoutHandle CreateVertexLayout(
        const VertexLayout& _vl);

    // ===========================================
    //  Index Buffer
    // ===========================================

    [[nodiscard]] IndexBufferHandle CreateIndexBuffer(
        MemoryView _indexData,
        bool       _bU32 = false);

    [[nodiscard]] IndexBufferHandle CreateDynamicIndexBuffer(
        uint32_t _numIndices,
        bool     _bU32 = false);

    // ===========================================
    //  Constant Buffer
    // ===========================================

    [[nodiscard]] ConstantBufferHandle CreateConstantBuffer(
        uint32_t _byteWidth);

    // ===========================================
    //  Storage Buffer
    // ===========================================

    [[nodiscard]] StorageBufferHandle CreateStructuredBuffer(
        uint32_t                    _numElements,
        uint32_t                    _stride,
        Flags<eStorageBufferOption> _flags           = eStorageBufferOption::None,
        MemoryView                  _initDataOrEmpty = {});

    [[nodiscard]] StorageBufferHandle CreateReadbackBuffer(
        uint32_t _byteWidth);

    [[nodiscard]] StorageBufferHandle CreateIndirectBuffer(
        uint32_t                    _numDraws,
        Flags<eStorageBufferOption> _flags);

    [[nodiscard]] size_t ReadBuffer(
        StorageBufferHandle _readbackSbh,
        MutableMemoryView   _dst);

    // ===========================================
    //  Buffer Utils
    // ===========================================

    void UpdateBuffer(
        AnyBufferHandle _abh,
        MemoryView      _data,
        uint32_t        _offset   = 0,
        bool            _bDiscard = true);

    void CopyBuffer(
        AnyBufferHandle _dst,
        uint32_t        _dstOffset,
        AnyBufferHandle _src,
        uint32_t        _srcOffset,
        uint32_t        _byteWidth = kWholeSize);

    // ===========================================
    //  Texture
    // ===========================================

    [[nodiscard]] TextureHandle CreateTexture2D(
        uint32_t               _width,
        uint32_t               _height,
        eTextureFormat         _format,
        bool                   _bHasMips        = false,
        uint32_t               _numLayers       = 1,
        eMSAA                  _msaa            = eMSAA::None,
        Flags<eTextureOption>  _flags           = eTextureOption::None,
        Span<const MemoryView> _initDataOrEmpty = {});

    [[nodiscard]] TextureHandle CreateTextureCube(
        uint32_t               _width,
        uint32_t               _height,
        eTextureFormat         _format,
        bool                   _bHasMips        = false,
        uint32_t               _numCubes        = 1,
        Flags<eTextureOption>  _flags           = eTextureOption::None,
        Span<const MemoryView> _initDataOrEmpty = {});

    [[nodiscard]] TextureHandle CreateTexture3D(
        uint32_t               _width,
        uint32_t               _height,
        uint32_t               _depth,
        eTextureFormat         _format,
        bool                   _bHasMips        = false,
        Flags<eTextureOption>  _flags           = eTextureOption::None,
        Span<const MemoryView> _initDataOrEmpty = {});

    void UpdateTexture2D(
        TextureHandle _texh,
        uint32_t      _mip,
        uint32_t      _layer,
        uint32_t      _x,
        uint32_t      _y,
        uint32_t      _width,
        uint32_t      _height,
        MemoryView    _data,
        uint32_t      _rowPitch);

    void UpdateTextureCube(
        TextureHandle _texh,
        uint32_t      _mip,
        eCubeFace     _face,
        uint32_t      _layer,
        uint32_t      _x,
        uint32_t      _y,
        uint32_t      _width,
        uint32_t      _height,
        MemoryView    _data,
        uint32_t      _rowPitch);

    void UpdateTexture3D(
        TextureHandle _texh,
        uint32_t      _mip,
        uint32_t      _x,
        uint32_t      _y,
        uint32_t      _z,
        uint32_t      _width,
        uint32_t      _height,
        uint32_t      _depth,
        MemoryView    _data,
        uint32_t      _rowPitch,
        uint32_t      _depthPitch);

    void CopyTexture(
        Subresource _dst,
        uint32_t    _dstX,
        uint32_t    _dstY,
        uint32_t    _dstZ,
        Subresource _src,
        uint32_t    _srcX,
        uint32_t    _srcY,
        uint32_t    _srcZ,
        uint32_t    _width,
        uint32_t    _height,
        uint32_t    _depth);

    [[nodiscard]] size_t ReadTexture(
        TextureHandle     _texh,
        uint32_t          _mip,
        uint32_t          _layer,
        MutableMemoryView _dst);

    // ===========================================
    //  Frame Buffer
    // ===========================================

    [[nodiscard]] FrameBufferHandle CreateFrameBuffer(
        Span<const Attachment> _attachments,
        bool                   _bOwnership);

    [[nodiscard]] FrameBufferHandle CreateFrameBuffer(
        TextureHandle _texh,
        bool          _bOwnership);

    [[nodiscard]] FrameBufferHandle CreateFrameBuffer(
        void*          _pWindow,
        uint32_t       _width,
        uint32_t       _height,
        eTextureFormat _format,
        eMSAA          _msaa);

    void ResizeFrameBuffer(
        FrameBufferHandle _fbh,
        uint32_t          _width,
        uint32_t          _height);

    void SetVSync(
        FrameBufferHandle _fbh,
        bool              _bVSync);

    // ===========================================
    //  Shader & Program
    // ===========================================

    [[nodiscard]] ShaderHandle CreateShader(
        eShader    _stage,
        MemoryView _bytecode);

    [[nodiscard]] ProgramHandle CreateProgram(
        ShaderHandle _vsh,
        ShaderHandle _psh,
        bool         _bOwnership = false);

    [[nodiscard]] ProgramHandle CreateComputeProgram(
        ShaderHandle _csh,
        bool         _bOwnership = false);

    // ===========================================
    //  Destroy
    // ===========================================

    void Destroy(VertexBufferHandle _vbh);
    void Destroy(IndexBufferHandle _ibh);
    void Destroy(VertexLayoutHandle _vlh);
    void Destroy(ConstantBufferHandle _cbh);
    void Destroy(StorageBufferHandle _sbh);
    void Destroy(TextureHandle _texh);
    void Destroy(FrameBufferHandle _fbh);
    void Destroy(ShaderHandle _sh);
    void Destroy(ProgramHandle _ph);

    // ===========================================
    //  Debug
    // ===========================================

    void SetName(VertexBufferHandle _vbh, StringView _name);
    void SetName(IndexBufferHandle _ibh, StringView _name);
    void SetName(ConstantBufferHandle _cbh, StringView _name);
    void SetName(StorageBufferHandle _sbh, StringView _name);
    void SetName(TextureHandle _texh, StringView _name);
    void SetName(FrameBufferHandle _fbh, StringView _name);
    void SetName(ShaderHandle _sh, StringView _name);

    // [AI] 디버그 그룹 API. 헤더에서 선언이 빠져 있었는데 m_pUserAnnotationOrNull 멤버가 남아 있어 되살렸다.
    void PushDebugGroup(StringView _name);
    void PopDebugGroup();
    void SetDebugMarker(StringView _name);

    // ===========================================
    //  Getter
    // ===========================================

    [[nodiscard]] const VertexBufferDesc&   GetDesc(VertexBufferHandle _vbh) const;
    [[nodiscard]] const IndexBufferDesc&    GetDesc(IndexBufferHandle _ibh) const;
    [[nodiscard]] const ConstantBufferDesc& GetDesc(ConstantBufferHandle _cbh) const;
    [[nodiscard]] const StorageBufferDesc&  GetDesc(StorageBufferHandle _sbh) const;
    [[nodiscard]] const TextureDesc&        GetDesc(TextureHandle _texh) const;
    [[nodiscard]] const ShaderDesc&         GetDesc(ShaderHandle _sh) const;
    [[nodiscard]] const ProgramDesc&        GetDesc(ProgramHandle _ph) const;
    [[nodiscard]] const FrameBufferDesc&    GetDesc(FrameBufferHandle _fbh) const;
    [[nodiscard]] const VertexLayout&       GetVertexLayout(VertexLayoutHandle _vlh) const;

    // ===========================================
    //  Clear
    // ===========================================

    void ClearRenderTarget(
        FrameBufferHandle _fbh,
        RGBA              _color,
        int               _slot = 0);

    void ClearRenderTargets(
        FrameBufferHandle _fbh,
        RGBA              _color);

    void ClearDepthStencil(
        FrameBufferHandle _fbh,
        bool              _bClearDepth,
        bool              _bClearStencil,
        float             _depth   = 1.f,
        uint8_t           _stencil = 0);

    // ===========================================
    //  Bind
    // ===========================================

    void SetVertexBuffer(
        VertexBufferHandle _vbh,
        uint32_t           _offset      = 0,
        uint32_t           _numVertices = kWholeSize);

    void SetVertexBuffer(
        VertexBufferHandle _vbh,
        uint32_t           _offset,
        uint32_t           _numVertices,
        VertexBufferHandle _instanceVbh,
        uint32_t           _instanceOffset = 0,
        uint32_t           _numInstances   = kWholeSize);

    void SetVertexBuffers(
        Span<const VertexStream> _streams,
        uint32_t                 _numVertices = kWholeSize);

    void SetVertexBuffers(
        Span<const VertexStream> _streams,
        uint32_t                 _numVertices,
        VertexBufferHandle       _instanceVbh,
        uint32_t                 _instanceOffset = 0,
        uint32_t                 _numInstances   = kWholeSize);

    void SetIndexBuffer(
        IndexBufferHandle _ibh,
        uint32_t          _offset     = 0,
        uint32_t          _numIndices = kWholeSize);

    void SetConstantBuffer(
        ConstantBufferHandle _cbh,
        eShader              _shader,
        int                  _slot);

    void SetTexture(
        TextureHandle _texh,
        eShader       _shader,
        int           _slot);

    void SetTextureRW(
        TextureHandle _texh,
        eShader       _shader,
        int           _slot);

    void SetBuffer(
        StorageBufferHandle _sbh,
        eShader             _shader,
        int                 _slot);

    void SetBufferRW(
        StorageBufferHandle _sbh,
        eShader             _shader,
        int                 _slot);

    void SetSampler(
        Flags<eSampler> _flags,
        eShader         _shader,
        int             _slot);

    void SetSampler(
        Flags<eSampler> _flags,
        RGBA            _borderColor,
        eShader         _shader,
        int             _slot);

    void SetRenderState(
        Flags<eRenderState> _flags);

    void SetBlend(
        Flags<eBlend> _flags,
        int           _slot = 0);

    void SetBlendFactor(
        RGBA _factor);

    void SetStencil(
        Flags<eStencil> _frontFace,
        Flags<eStencil> _backFace,
        uint8_t         _stencilRef = 0);

    void SetProgram(
        ProgramHandle _phOrNull);

    void SetComputeProgram(
        ProgramHandle _phOrNull);

    void SetFrameBuffer(
        FrameBufferHandle _fbh);

    void SetViewport(
        float _x,
        float _y,
        float _width,
        float _height);

    void SetScissor(
        int _x,
        int _y,
        int _width,
        int _height);

    void SetSubmitParam(
        eSubmitParam _param,
        uint32_t     _value);

    // ===========================================
    //  Submit
    // ===========================================

    void Touch();
    void Reset();

    void Submit();

    void Submit(
        StorageBufferHandle _indirectSbh,
        uint32_t            _offset   = 0,
        uint32_t            _numDraws = kWholeSize);

    void Dispatch(
        uint32_t _numGroupsX,
        uint32_t _numGroupsY,
        uint32_t _numGroupsZ);

    void Dispatch(
        StorageBufferHandle _indirectSbh,
        uint32_t            _offset = 0);

private:
    void InitDevice_(bool _bEnableDebugLayer);
    void InitDebugInterfacesIfNeed_();
    void InitTimerQueries_();
    void CleanUpTimerQueries_();

    [[nodiscard]] VertexBufferD3D11 CreateVertexBuffer_(
        uint32_t            _numElems,
        const VertexLayout& _vl,
        bool                _bDynamic,
        MemoryView          _initDataOrEmpty);

    [[nodiscard]] VertexBufferD3D11 CreateInstanceBuffer_(
        uint32_t   _numInstances,
        uint32_t   _stride) const;

    [[nodiscard]] IndexBufferD3D11 CreateIndexBuffer_(
        uint32_t   _numElems,
        bool       _bU32,
        bool       _bDynamic,
        MemoryView _initDataOrEmpty) const;

    [[nodiscard]] ConstantBufferD3D11 CreateConstantBuffer_(
        uint32_t _byteWidth) const;

    [[nodiscard]] StorageBufferD3D11 CreateStructuredBuffer_(
        uint32_t                    _numElements,
        uint32_t                    _stride,
        Flags<eStorageBufferOption> _flags,
        MemoryView                  _initDataOrEmpty) const;

    [[nodiscard]] StorageBufferD3D11 CreateIndirectArgsBuffer_(
        uint32_t                    _numDraws,
        Flags<eStorageBufferOption> _flags) const;

    [[nodiscard]] StorageBufferD3D11 CreateReadbackBuffer_(
        uint32_t _byteWidth) const;

    [[nodiscard]] VertexLayoutHandle GetOrAllocVertexLayoutHandle_(
        const VertexLayout& _vl);

    // ===========================================
    //  Resource Lookup / Unbind
    // ===========================================

    [[nodiscard]] FrameBufferHandle ResolveFrameBufferHandle_(FrameBufferHandle _fbh) const;
    void                            ResolveFrameBuffer_(FrameBufferHandle _fbh);

    [[nodiscard]] ID3D11Buffer* GetD3d11Buffer_(AnyBufferHandle _abh) const;
    [[nodiscard]] uint32_t      GetBufferByteWidth_(AnyBufferHandle _bh) const;
    [[nodiscard]] bool          IsDynamicBuffer_(AnyBufferHandle _bh) const;

    void UnbindResource_(Resource _resource);
    void UnbindConstantBuffer_(ConstantBufferHandle _cbh);
    void UnbindVertexBuffer_(VertexBufferHandle _vbh);
    void UnbindIndexBuffer_(IndexBufferHandle _ibh);
    void UnbindFrameBuffer_(FrameBufferHandle _fbh);

    // ===========================================
    //  Texture Create Helper
    // ===========================================

    // [AI] BufferCreateParam / TextureCreateParam / SwapChainDesc 는 어디에도 정의가 없었고 쓰지 않기로 해서 걷어냈다.
    //      버퍼 생성은 이미 CreateXxxBuffer_ 들이 직접 처리하므로 CreateD3d11Buffer_ / CreateStorageViews_ 도 함께 제거.
    //      텍스처 쪽은 TextureDesc 를 그대로 파라미터로 쓴다. TextureD3D11 이 어차피 이걸 상속한다.
    [[nodiscard]] Vector<D3D11_SUBRESOURCE_DATA> MakeInitData_(const TextureDesc& _desc, Span<const MemoryView> _initData) const;

    void                        CreateTextureViews_(TextureD3D11& _texture);
    [[nodiscard]] TextureHandle CreateTextureInternal_(const TextureDesc& _desc, Span<const MemoryView> _initDataOrEmpty);
    void                        UpdateTextureInternal_(TextureHandle _texh, uint32_t _mip, uint32_t _layer, uint32_t _x, uint32_t _y, uint32_t _z, uint32_t _widthOrAll, uint32_t _heightOrAll, uint32_t _depthOrAll, MemoryView _data, uint32_t _rowPitch, uint32_t _depthPitch);

    // ===========================================
    //  FrameBufferDesc / SwapChain Helper
    // ===========================================

    // [AI] 실패하면 JUG_DX_CHECK 가 크래시시키므로 bool 반환을 없앴다. (방어적 분기 제거)
    void                          CreateFrameBufferViews_(FrameBufferD3D11& _frameBuffer);
    void                          FillAttachments_(FrameBufferD3D11& _frameBuffer, Span<const Attachment> _attachments) const;
    [[nodiscard]] ID3D11Resource* GetViewTarget_(const TextureD3D11& _texture) const;

    [[nodiscard]] UINT MakeSwapChainFlags_() const;

    // [AI] SwapChainDesc 가 사라져서 백버퍼 크기만 넘긴다. 포맷/MSAA 는 이미 색상 텍스처에 들어 있다.
    void CreateSwapChainTargets_(FrameBufferD3D11& _frameBuffer, uint32_t _width, uint32_t _height);
    void ReleaseSwapChainTargets_(FrameBufferD3D11& _frameBuffer);
    void PresentSwapChain_(FrameBufferD3D11& _frameBuffer);

    // ===========================================
    //  Shader / Input Layout Helper
    // ===========================================

    [[nodiscard]] ShaderHandle       FindShaderByHash_(uint64_t _hash) const;
    [[nodiscard]] ID3D11InputLayout* GetOrCreateD3d11InputLayout_();

    // ===========================================
    //  Pipeline State Helper
    // ===========================================

    [[nodiscard]] ID3D11SamplerState*      GetOrCreateSamplerState_(Flags<eSampler> _flags, RGBA _borderColor);
    [[nodiscard]] ID3D11RasterizerState*   GetOrCreateRasterizerState_();
    [[nodiscard]] ID3D11BlendState*        GetOrCreateBlendState_();
    [[nodiscard]] ID3D11DepthStencilState* GetOrCreateDepthStencilState_();

    [[nodiscard]] Flags<ePipelineDirty> ToSrvDirty_(eShader _shader) const;
    [[nodiscard]] Flags<ePipelineDirty> ToCBufferDirty_(eShader _shader) const;
    [[nodiscard]] Flags<ePipelineDirty> ToSamplerDirty_(eShader _shader) const;

    void BindFrameBuffer_();
    void ApplyPipeline_();

    // ===========================================
    //  Device
    // ===========================================

    ID3D11Device*        m_pD3d11Device        = nullptr;
    ID3D11DeviceContext* m_pD3d11DeviceContext = nullptr;
    D3D_FEATURE_LEVEL    m_d3dFeatureLevel     = D3D_FEATURE_LEVEL_11_0;
    DXGI                 m_dxgi                = {};

    ID3D11Debug*               m_pD3d11DebugOrNull     = nullptr;
    ID3D11InfoQueue*           m_pD3d11InfoQueueOrNull = nullptr;
    ID3DUserDefinedAnnotation* m_pUserAnnotationOrNull = nullptr;

    GraphicsCaps m_caps = {};

    // ===========================================
    //  Resource
    // ===========================================

    VertexBufferPool   m_vertexBufferPool   = {};
    IndexBufferPool    m_indexBufferPool    = {};
    StorageBufferPool  m_storageBufferPool  = {};
    ConstantBufferPool m_constantBufferPool = {};
    TexturePool        m_texturePool        = {};
    FrameBufferPool    m_frameBufferPool    = {};
    ShaderPool         m_shaderPool         = {};
    ProgramPool        m_programPool        = {};
    VertexLayoutPool   m_vertexLayoutPool   = {};

    NoopHashMap<uint64_t, VertexLayoutHandle> m_vlhCache = {};
    NoopHashMap<uint64_t, ShaderHandle>       m_shCache  = {};

    NoopHashMap<uint64_t, ID3D11InputLayout*>       m_d3d11InputLayoutCache       = {};
    NoopHashMap<uint64_t, ID3D11BlendState*>        m_d3d11BlendStateCache        = {};
    NoopHashMap<uint64_t, ID3D11RasterizerState*>   m_d3d11RasterizerStateCache   = {};
    NoopHashMap<uint64_t, ID3D11DepthStencilState*> m_d3d11DepthStencilStateCache = {};
    NoopHashMap<uint64_t, ID3D11SamplerState*>      m_d3d11SamplerStateCache      = {};

    // ===========================================
    //  Pipeline State
    // ===========================================

    Flags<ePipelineDirty> m_dirtyFlags = kAllFlag;

    ARRAY<ID3D11Buffer*, kNumMaxVertexSlots>      m_d3d11VertexBuffers = {};
    ARRAY<VertexBufferHandle, kNumMaxVertexSlots> m_vbhs               = {};
    ARRAY<VertexLayoutHandle, kNumMaxVertexSlots> m_vlhs               = {};
    ARRAY<uint32_t, kNumMaxVertexSlots>           m_vertexStrides      = {};
    ARRAY<uint32_t, kNumMaxVertexSlots>           m_vertexOffsets      = {};
    uint32_t                                      m_numVertexBuffers   = 0;
    uint32_t                                      m_numVertices        = 0;

    ID3D11Buffer*     m_pD3d11IndexBuffer = nullptr;
    DXGI_FORMAT       m_dxgiIndexFormat   = DXGI_FORMAT_UNKNOWN;
    uint32_t          m_indexOffset       = 0;
    uint32_t          m_numIndices        = 0;
    IndexBufferHandle m_ibh               = kNullHandle;

    ID3D11Buffer*      m_pD3d11InstanceBuffer = nullptr;
    uint32_t           m_instanceStride       = 0;
    uint32_t           m_instanceOffset       = 0;
    VertexBufferHandle m_instanceVbh          = kNullHandle;
    uint32_t           m_numInstances         = 0;

    ID3D11InputLayout* m_pD3d11InputLayout = nullptr;

    ENUM_ARRAY<eShader, ReadBind>        m_readBind      = {};
    ENUM_ARRAY<eShader, CBufferBind>     m_cbufferBind   = {};
    ENUM_ARRAY<eShader, SamplerBind>     m_samplerBind   = {};
    ENUM_ARRAY<eShaderRW, ReadWriteBind> m_readWriteBind = {};

    FrameBufferHandle m_fbh     = kNullHandle;
    FrameBufferHandle m_lastFbh = kNullHandle;

    float m_viewportX = 0.f;
    float m_viewportY = 0.f;
    float m_viewportW = 0.f;
    float m_viewportH = 0.f;

    int m_scissorX = 0;
    int m_scissorY = 0;
    int m_scissorW = 0;
    int m_scissorH = 0;

    ProgramHandle                      m_ph           = kNullHandle;
    ProgramHandle                      m_computePh    = kNullHandle;
    ENUM_ARRAY<eSubmitParam, uint32_t> m_submitParams = {};

    Flags<eRenderState>                            m_renderStateFlags = eRenderState::None;
    ARRAY<Flags<eBlend>, kNumMaxRenderTargetSlots> m_blendFlags       = {};
    RGBA                                           m_blendFactor      = {};
    Flags<eStencil>                                m_fstencilFlags    = {};
    Flags<eStencil>                                m_bstencilFlags    = {};
    uint8_t                                        m_stencilRef       = 0;

    GraphicsStats m_stats     = {};
    GraphicsStats m_lastStats = {};

    static constexpr size_t kNumInitTimerQueries = 16;
    RingBuffer<TimerQuery>  m_timerQueries { kNumInitTimerQueries };
};

template<typename H, typename F>
void DestroyAndNull(
    H& _handle,
    F  _destroy)
{
    if (_handle)
    {
        _destroy(_handle);
        _handle = kNullHandle;
    }
}

}   // namespace jug
