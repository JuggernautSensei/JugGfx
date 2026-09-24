#pragma once
#include "Base.h"
#include "DXGI.h"

namespace jug
{

class Graphics
{
    JUG_CLASS(Graphics, NO_COPY, NO_MOVE)

    // ===========================================
    //  Extended Resource
    // ===========================================

    struct VertexBufferD3D11 : public VertexBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
        bool          bMapped = false;
    };

    struct InstanceBufferD3D11 : public InstanceBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
        bool          bMapped = false;
    };

    struct IndexBufferD3D11 : public IndexBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
        DXGI_FORMAT   format  = DXGI_FORMAT_UNKNOWN;
        bool          bMapped = false;
    };

    struct StorageBufferD3D11 : public StorageBufferDesc
    {
        ID3D11Buffer*              pBuffer = nullptr;
        ID3D11ShaderResourceView*  pSRV    = nullptr;
        ID3D11UnorderedAccessView* pUAV    = nullptr;
        bool                       bMapped = false;
    };

    struct ConstantBufferD3D11 : public ConstantBufferDesc
    {
        ID3D11Buffer* pBuffer = nullptr;
        bool          bMapped = false;
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
        ARRAY<ID3D11RenderTargetView*, kNumMaxRenderTargetSlots> rtvs       = {};
        ID3D11DepthStencilView*                                  pDSV       = nullptr;
        ISwapChain*                                              pSwapChain = nullptr;
    };

    struct ShaderD3D11 : public ShaderDesc
    {
        JUG_DISABLE_ANON_WARNING_BEGIN
        union
        {
            ID3D11VertexShader*  pVS = nullptr;
            ID3D11PixelShader*   pPS;
            ID3D11ComputeShader* pCS;
        };
        JUG_DISABLE_ANON_WARNING_END
    };

    // ===========================================
    //  Typedef
    // ===========================================

    using VertexBufferPool   = ResourcePool<VertexBufferHandle, VertexBufferD3D11>;
    using InstanceBufferPool = ResourcePool<InstanceBufferHandle, InstanceBufferD3D11>;
    using IndexBufferPool    = ResourcePool<IndexBufferHandle, IndexBufferD3D11>;
    using StorageBufferPool  = ResourcePool<StorageBufferHandle, StorageBufferD3D11>;
    using ConstantBufferPool = ResourcePool<ConstantBufferHandle, ConstantBufferD3D11>;
    using TexturePool        = ResourcePool<TextureHandle, TextureD3D11>;
    using FrameBufferPool    = ResourcePool<FrameBufferHandle, FrameBufferD3D11>;
    using ShaderPool         = ResourcePool<ShaderHandle, ShaderD3D11>;
    using ProgramPool        = ResourcePool<ProgramHandle, ProgramDesc>;
    using VertexLayoutPool   = ResourcePool<VertexLayoutHandle, VertexLayoutDesc>;

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

        ConstantBuffer      = 1 << 7,
        ShaderResourceView  = 1 << 8,
        UnorderedAccessView = 1 << 9,
        SamplerState        = 1 << 10,

        RasterizerState   = 1 << 11,
        BlendState        = 1 << 12,
        DepthStencilState = 1 << 13,
        Viewport          = 1 << 14,
        ScissorRect       = 1 << 15,
        FrameBuffer       = 1 << 16,
    };

    struct Sampler
    {
        [[nodiscard]] bool operator==(const Sampler& _other) const = default;

        Flags<eSampler> flags  = {};
        RGBA            border = {};
    };

    template<typename T, typename U, uint32_t kSize>
    struct ResourceBind
    {
        void MarkDirty(
            const uint32_t _slot)
        {
            dirtyBegin = Min(dirtyBegin, _slot);
            dirtyEnd   = Max(dirtyEnd, _slot + 1);
        }

        void ClearDirty()
        {
            dirtyBegin = Max<uint32_t>();
            dirtyEnd   = 0;
        }

        [[nodiscard]] bool IsDirty() const
        {
            return dirtyBegin < dirtyEnd;
        }

        [[nodiscard]] uint32_t NumDirties() const
        {
            return dirtyEnd - dirtyBegin;
        }

        ARRAY<T, kSize> d3d11Resources = {};
        ARRAY<U, kSize> resources      = {};

        uint32_t dirtyBegin = 0;
        uint32_t dirtyEnd   = kSize;
    };

    using ReadBind      = ResourceBind<ID3D11ShaderResourceView*, ResourceRef, kNumMaxReadSlots>;
    using ReadWriteBind = ResourceBind<ID3D11UnorderedAccessView*, ResourceRef, kNumMaxReadWriteSlots>;
    using CBufferBind   = ResourceBind<ID3D11Buffer*, ConstantBufferHandle, kNumMaxCBufferSlots>;
    using SamplerBind   = ResourceBind<ID3D11SamplerState*, Sampler, kNumMaxSamplerSlots>;

    // ===========================================
    //  Misc
    // ===========================================

    struct TimerQuery
    {
        ID3D11Query* pBegin     = nullptr;
        ID3D11Query* pEnd       = nullptr;
        ID3D11Query* pDisjoint  = nullptr;
        uint64_t     frameIndex = 0;
        bool         bIssued    = false;
    };

    struct ResolveBufferRefResult
    {
        ID3D11Buffer* pBuffer   = nullptr;
        uint32_t      byteWidth = 0;
        bool          bDynamic  = false;
    };

public:
    explicit Graphics(bool _bEnableDebugLayer);
    ~Graphics();

    [[nodiscard]] static Graphics& GetSingleton();

    // ===========================================
    //  System
    // ===========================================

    void Frame();

    size_t                             ReportLiveObjects();
    [[nodiscard]] const GraphicsCaps&  GetCaps() const;
    [[nodiscard]] const GraphicsStats& GetStats() const;

    // ===========================================
    //  Vertex Stream
    // ===========================================

    [[nodiscard]] VertexBufferHandle CreateVertexBuffer(
        MemoryView          _vertexData,
        const VertexLayout& _vl);

    [[nodiscard]] VertexBufferHandle CreateDynamicVertexBuffer(
        uint32_t            _numVertices,
        const VertexLayout& _vl);

    [[nodiscard]] InstanceBufferHandle CreateInstanceBuffer(
        uint32_t _numInstances,
        uint32_t _stride);

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

    // ===========================================
    //  Buffer Utils
    // ===========================================

    void UpdateBuffer(
        BufferRef  _buffer,
        MemoryView _data,
        uint32_t   _offset = 0);

    [[nodiscard]] MutableMemoryView MapBuffer(
        BufferRef _buffer);

    void UnmapBuffer(
        BufferRef _buffer);

    void CopyBuffer(
        BufferRef _dst,
        BufferRef _src);

    void CopyBuffer(
        BufferRef _dst,
        uint32_t  _dstOffset,
        BufferRef _src,
        uint32_t  _srcOffset,
        uint32_t  _byteWidthOrZero = 0);

    [[nodiscard]] size_t ReadBuffer(
        StorageBufferHandle _readbackSbh,
        MutableMemoryView   _dst);

    // ===========================================
    //  Texture
    // ===========================================

    [[nodiscard]] TextureHandle CreateTexture2D(
        uint32_t               _width,
        uint32_t               _height,
        eTextureFormat         _format,
        uint32_t               _numLayers       = 1,
        eMSAA                  _msaa            = eMSAA::None,
        Flags<eTextureOption>  _flags           = eTextureOption::None,
        Span<const MemoryView> _initDataOrEmpty = {});

    [[nodiscard]] TextureHandle CreateTextureCube(
        uint32_t               _width,
        uint32_t               _height,
        eTextureFormat         _format,
        uint32_t               _numCubes        = 1,
        Flags<eTextureOption>  _flags           = eTextureOption::None,
        Span<const MemoryView> _initDataOrEmpty = {});

    [[nodiscard]] TextureHandle CreateTexture3D(
        uint32_t               _width,
        uint32_t               _height,
        uint32_t               _depth,
        eTextureFormat         _format,
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
        MemoryView    _data);

    void UpdateTextureCube(
        TextureHandle _texh,
        uint32_t      _mip,
        eCubeFace     _face,
        uint32_t      _layer,
        uint32_t      _x,
        uint32_t      _y,
        uint32_t      _width,
        uint32_t      _height,
        MemoryView    _data);

    void UpdateTexture3D(
        TextureHandle _texh,
        uint32_t      _mip,
        uint32_t      _x,
        uint32_t      _y,
        uint32_t      _z,
        uint32_t      _width,
        uint32_t      _height,
        uint32_t      _depth,
        MemoryView    _data);

    void CopyTexture(
        TextureHandle _dstTexh,
        TextureHandle _srcTexh);

    void CopyTexture(
        TextureHandle _dstTexh,
        uint32_t      _dstMip,
        uint32_t      _dstLayer,
        uint32_t      _dstX,
        uint32_t      _dstY,
        uint32_t      _dstZ,
        TextureHandle _srcTexh,
        uint32_t      _srcMip,
        uint32_t      _srcLayer,
        uint32_t      _srcX,
        uint32_t      _srcY,
        uint32_t      _srcZ,
        uint32_t      _widthOrZero,
        uint32_t      _heightOrZero,
        uint32_t      _depthOrZero);

    [[nodiscard]] size_t ReadTexture(
        TextureHandle     _readbackTexh,
        uint32_t          _mip,
        uint32_t          _layer,
        MutableMemoryView _dst);

    // ===========================================
    //  Frame Buffer
    // ===========================================

    [[nodiscard]] FrameBufferHandle CreateFrameBuffer(
        Span<const Attachment> _atts,
        bool                   _bOwnership = false);

    [[nodiscard]] FrameBufferHandle CreateFrameBuffer(
        TextureHandle _texh,
        bool          _bOwnership = false);

    [[nodiscard]] FrameBufferHandle CreateFrameBuffer(
        SDL_WindowID   _wndID,
        uint32_t       _width,
        uint32_t       _height,
        eTextureFormat _format,
        uint32_t       _numBuffers);

    [[nodiscard]] FrameBufferHandle FindFrameBufferOrNull(
        SDL_WindowID _wndID);

    void ResizeFrameBuffer(
        FrameBufferHandle _swapChainFbh,
        uint32_t          _width,
        uint32_t          _height);

    void SetVSync(
        FrameBufferHandle _swapChainFbh,
        bool              _bVSync);

    void ClearRenderTarget(
        FrameBufferHandle _fbh,
        RGBA              _color,
        uint32_t          _slot = 0);

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
    //  Shader & Program
    // ===========================================

    [[nodiscard]] ShaderHandle CreateShader(
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
    void Destroy(InstanceBufferHandle _instbh);
    void Destroy(IndexBufferHandle _ibh);
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
    void SetName(InstanceBufferHandle _instbh, StringView _name);
    void SetName(IndexBufferHandle _ibh, StringView _name);
    void SetName(ConstantBufferHandle _cbh, StringView _name);
    void SetName(StorageBufferHandle _sbh, StringView _name);
    void SetName(TextureHandle _texh, StringView _name);
    void SetName(FrameBufferHandle _fbh, StringView _name);
    void SetName(ShaderHandle _sh, StringView _name);

    void PushDebugGroup(StringView _name) const;
    void PopDebugGroup() const;
    void SetDebugMarker(StringView _name) const;

    // ===========================================
    //  Getter
    // ===========================================

    [[nodiscard]] const VertexBufferDesc&   GetDesc(VertexBufferHandle _vbh) const;
    [[nodiscard]] const InstanceBufferDesc& GetDesc(InstanceBufferHandle _instbh) const;
    [[nodiscard]] const IndexBufferDesc&    GetDesc(IndexBufferHandle _ibh) const;
    [[nodiscard]] const ConstantBufferDesc& GetDesc(ConstantBufferHandle _cbh) const;
    [[nodiscard]] const StorageBufferDesc&  GetDesc(StorageBufferHandle _sbh) const;
    [[nodiscard]] const TextureDesc&        GetDesc(TextureHandle _texh) const;
    [[nodiscard]] const ShaderDesc&         GetDesc(ShaderHandle _sh) const;
    [[nodiscard]] const ProgramDesc&        GetDesc(ProgramHandle _ph) const;
    [[nodiscard]] const FrameBufferDesc&    GetDesc(FrameBufferHandle _fbh) const;

    // ===========================================
    //  Bind
    // ===========================================

    void SetVertexBuffer(
        VertexBufferHandle _vbh,
        uint32_t           _offset            = 0,
        uint32_t           _numVerticesOrZero = 0);

    void SetVertexBuffers(
        Span<const VertexStream> _streams,
        uint32_t                 _numVerticesOrZero = 0);

    void SetVertexBuffer(
        VertexBufferHandle   _vbh,
        uint32_t             _offset,
        uint32_t             _numVerticesOrZero,
        InstanceBufferHandle _instbh,
        uint32_t             _instanceOffset     = 0,
        uint32_t             _numInstancesOrZero = 0);

    void SetVertexBuffers(
        Span<const VertexStream> _streams,
        uint32_t                 _numVerticesOrZero,
        InstanceBufferHandle     _instbh,
        uint32_t                 _instanceOffset     = 0,
        uint32_t                 _numInstancesOrZero = 0);

    void SetIndexBuffer(
        IndexBufferHandle _ibh,
        uint32_t          _offset           = 0,
        uint32_t          _numIndicesOrZero = 0);

    void SetConstantBuffer(
        ConstantBufferHandle _cbh,
        eShader              _shader,
        uint32_t             _slot);

    void SetTexture(
        TextureHandle _texh,
        eShader       _shader,
        uint32_t      _slot);

    void SetTextureRW(
        TextureHandle _texh,
        eShaderRW     _shader,
        uint32_t      _slot);

    void SetBuffer(
        StorageBufferHandle _sbh,
        eShader             _shader,
        uint32_t            _slot);

    void SetBufferRW(
        StorageBufferHandle _sbh,
        eShaderRW           _shader,
        uint32_t            _slot);

    void SetSampler(
        Flags<eSampler> _flags,
        eShader         _shader,
        uint32_t        _slot);

    void SetSampler(
        Flags<eSampler> _flags,
        RGBA            _border,
        eShader         _shader,
        uint32_t        _slot);

    void SetRenderState(
        Flags<eRenderState> _flags);

    void SetBlend(
        Flags<eBlend> _flags,
        uint32_t      _slot = 0);

    void SetBlendFactor(
        RGBA _factor);

    void SetStencil(
        Flags<eStencil> _front,
        Flags<eStencil> _back,
        uint8_t         _stencilRef = 0);

    void SetProgram(
        ProgramHandle _ph);

    void SetComputeProgram(
        ProgramHandle _ph);

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
        uint32_t            _offset         = 0,
        uint32_t            _numDrawsOrZero = 0);

    void Dispatch(
        uint32_t _numGroupsX,
        uint32_t _numGroupsY,
        uint32_t _numGroupsZ);

    void Dispatch(
        StorageBufferHandle _indirectSbh,
        uint32_t            _offset = 0);

private:
    // ===========================================
    //  Init
    // ===========================================

    void InitDevice_(bool _bEnableDebugLayer);
    void InitDebugInterfacesIfNeed_();
    void InitTimerQueries_();
    void CleanUpTimerQueries_();

    // ===========================================
    //  Buffer
    // ===========================================

    [[nodiscard]] VertexBufferD3D11 CreateVertexBuffer_(
        uint32_t            _numElems,
        const VertexLayout& _vl,
        bool                _bDynamic,
        MemoryView          _initDataOrEmpty);

    [[nodiscard]] InstanceBufferD3D11 CreateInstanceBuffer_(
        uint32_t _numElems,
        uint32_t _stride) const;

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

    [[nodiscard]] VertexLayoutHandle GetOrCreateVertexLayoutHandle_(
        const VertexLayout& _vl);

    void ReleaseVertexLayout_(
        VertexLayoutHandle _vlh);

    [[nodiscard]] ResolveBufferRefResult ResolveBufferRef_(
        BufferRef _buffer);

    // ===========================================
    //  Texture
    // ===========================================

    [[nodiscard]] DXGI_SAMPLE_DESC MakeSampleDesc_(
        DXGI_FORMAT _format,
        eMSAA       _msaa) const;

    [[nodiscard]] TextureD3D11 CreateTexture_(
        uint32_t               _width,
        uint32_t               _height,
        uint32_t               _depth,
        eTexture               _type,
        eTextureFormat         _format,
        uint32_t               _numLayers,
        eMSAA                  _msaa,
        Flags<eTextureOption>  _flags,
        Span<const MemoryView> _initData) const;

    void UpdateTexture_(
        TextureHandle _texh,
        uint32_t      _mip,
        uint32_t      _layer,
        uint32_t      _offsetX,
        uint32_t      _offsetY,
        uint32_t      _offsetZ,
        uint32_t      _width,
        uint32_t      _height,
        uint32_t      _depth,
        MemoryView    _data);

    // ===========================================
    //  Resources
    // ===========================================

    [[nodiscard]] ID3D11InputLayout*       GetOrCreateD3d11InputLayout_();
    [[nodiscard]] ID3D11SamplerState*      GetOrCreateSamplerState_(Flags<eSampler> _flags, RGBA _border);
    [[nodiscard]] ID3D11RasterizerState*   GetOrCreateRasterizerState_();
    [[nodiscard]] ID3D11BlendState*        GetOrCreateBlendState_();
    [[nodiscard]] ID3D11DepthStencilState* GetOrCreateDepthStencilState_();

    void ApplyPipeline_();

    // ===========================================
    //  Device
    // ===========================================

    ID3D11Device*        m_pD3d11Device        = nullptr;
    ID3D11DeviceContext* m_pD3d11DeviceContext = nullptr;
    D3D_FEATURE_LEVEL    m_d3dFeatureLevel     = D3D_FEATURE_LEVEL_11_0;
    D3D_DRIVER_TYPE      m_d3dDriverType       = D3D_DRIVER_TYPE_UNKNOWN;
    DXGI                 m_dxgi                = {};

    ID3D11Debug*               m_pD3d11DebugOrNull     = nullptr;
    ID3D11InfoQueue*           m_pD3d11InfoQueueOrNull = nullptr;
    ID3DUserDefinedAnnotation* m_pUserAnnotationOrNull = nullptr;

    GraphicsCaps m_caps = {};

    // ===========================================
    //  ResourceRef
    // ===========================================

    VertexLayoutPool   m_vertexLayoutPool   = {};
    VertexBufferPool   m_vertexBufferPool   = {};
    InstanceBufferPool m_instanceBufferPool = {};
    IndexBufferPool    m_indexBufferPool    = {};
    StorageBufferPool  m_storageBufferPool  = {};
    ConstantBufferPool m_constantBufferPool = {};
    TexturePool        m_texturePool        = {};
    FrameBufferPool    m_frameBufferPool    = {};
    ShaderPool         m_shaderPool         = {};
    ProgramPool        m_programPool        = {};

    NoopHashMap<uint64_t, VertexLayoutHandle> m_vlhCache    = {};
    NoopHashMap<uint64_t, ShaderHandle>       m_shaderCache = {};

    NoopHashMap<uint64_t, ID3D11InputLayout*>       m_d3d11InputLayoutCache       = {};
    NoopHashMap<uint64_t, ID3D11BlendState*>        m_d3d11BlendStateCache        = {};
    NoopHashMap<uint64_t, ID3D11RasterizerState*>   m_d3d11RasterizerStateCache   = {};
    NoopHashMap<uint64_t, ID3D11DepthStencilState*> m_d3d11DepthStencilStateCache = {};
    NoopHashMap<uint64_t, ID3D11SamplerState*>      m_d3d11SamplerStateCache      = {};

    Vector<FrameBufferHandle> m_swapChainFbhs = {};

    ID3D11Buffer* m_pUploadBuffer         = nullptr;
    uint32_t      m_uploadBufferByteWidth = 0;

    // ===========================================
    //  Pipeline State
    // ===========================================

    Flags<ePipelineDirty> m_dirtyFlags = kAllFlag;

    ARRAY<ID3D11Buffer*, kNumMaxStreams>      m_d3d11VertexBuffers = {};
    ARRAY<VertexBufferHandle, kNumMaxStreams> m_vbhs               = {};
    ARRAY<VertexLayoutHandle, kNumMaxStreams> m_vlhs               = {};
    ARRAY<uint32_t, kNumMaxStreams>           m_vertexStrides      = {};
    ARRAY<uint32_t, kNumMaxStreams>           m_vertexOffsets      = {};
    uint32_t                                  m_numStreams         = 0;
    uint32_t                                  m_numVertices        = 0;

    ID3D11Buffer*     m_pD3d11IndexBuffer = nullptr;
    DXGI_FORMAT       m_dxgiIndexFormat   = DXGI_FORMAT_UNKNOWN;
    uint32_t          m_indexOffset       = 0;
    uint32_t          m_numIndices        = 0;
    IndexBufferHandle m_ibh               = kNullHandle;

    ID3D11Buffer*        m_pD3d11InstanceBuffer = nullptr;
    uint32_t             m_instanceDataStride   = 0;
    uint32_t             m_instanceOffset       = 0;
    InstanceBufferHandle m_instbh               = kNullHandle;
    uint32_t             m_numInstances         = 0;

    ENUM_ARRAY<eShader, ReadBind>        m_readBind      = {};
    ENUM_ARRAY<eShader, CBufferBind>     m_cbufferBind   = {};
    ENUM_ARRAY<eShader, SamplerBind>     m_samplerBind   = {};
    ENUM_ARRAY<eShaderRW, ReadWriteBind> m_readWriteBind = {};

    FrameBufferHandle m_fbh     = kNullHandle;
    FrameBufferHandle m_lastFbh = kNullHandle;   // for resolve

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

    uint64_t m_frameIndex = 0;

    static constexpr size_t kNumInitTimerQueries = 8;
    RingBuffer<TimerQuery>  m_timerQueries { kNumInitTimerQueries };
};

}   // namespace jug

#define JUG_GFX_DESTROY(_handle)                       \
    JUG_BEGIN_MACRO_BLOCK                              \
    if (_handle != jug::kNullHandle)                   \
    {                                                  \
        jug::Graphics::GetSingleton().Destroy(_handle); \
        _handle = jug::kNullHandle;                    \
    }                                                  \
    JUG_END_MACRO_BLOCK
