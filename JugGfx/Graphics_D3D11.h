#pragma once
#include "DXGI.h"
#include "Graphics.h"

namespace jug
{

class Graphics_D3D11 final : public Graphics
{
    JUG_CLASS(Graphics_D3D11, NO_COPY, NO_MOVE)

    // ===========================================
    //  D3D11 Resource
    //   Desc 는 기반의 풀이 갖고 있다. 여기에는 D3D 객체만 둔다.
    // ===========================================

    struct VertexBufferD3D11
    {
        ID3D11Buffer* pBuffer = nullptr;
    };

    struct InstanceBufferD3D11
    {
        ID3D11Buffer* pBuffer = nullptr;
    };

    struct IndexBufferD3D11
    {
        ID3D11Buffer* pBuffer = nullptr;
    };

    struct StorageBufferD3D11
    {
        ID3D11Buffer*              pBuffer    = nullptr;
        ID3D11ShaderResourceView*  pSrvOrNull = nullptr;
        ID3D11UnorderedAccessView* pUavOrNull = nullptr;
    };

    struct ConstantBufferD3D11
    {
        ID3D11Buffer* pBuffer = nullptr;
    };

    struct TextureD3D11
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
            ID3D11Resource*  pMsaaRtResourceOrNull = nullptr;
            ID3D11Texture2D* pMsaaRtTexture2DOrNull;
        };
        JUG_DISABLE_ANON_WARNING_END

        ID3D11ShaderResourceView*  pSrvOrNull = nullptr;
        ID3D11UnorderedAccessView* pUavOrNull = nullptr;
    };

    struct FrameBufferD3D11
    {
        ARRAY<ID3D11RenderTargetView*, kNumMaxRenderTargetSlots> rtvs             = {};
        ID3D11DepthStencilView*                                  pDsvOrNull       = nullptr;
        ISwapChain*                                              pSwapChainOrNull = nullptr;
    };

    struct ShaderD3D11
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

    using VertexBufferStorage   = HandleStorage<VertexBufferHandle, VertexBufferD3D11>;
    using InstanceBufferStorage = HandleStorage<InstanceBufferHandle, InstanceBufferD3D11>;
    using IndexBufferStorage    = HandleStorage<IndexBufferHandle, IndexBufferD3D11>;
    using StorageBufferStorage  = HandleStorage<StorageBufferHandle, StorageBufferD3D11>;
    using ConstantBufferStorage = HandleStorage<ConstantBufferHandle, ConstantBufferD3D11>;
    using TextureStorage        = HandleStorage<TextureHandle, TextureD3D11>;
    using FrameBufferStorage    = HandleStorage<FrameBufferHandle, FrameBufferD3D11>;
    using ShaderStorage         = HandleStorage<ShaderHandle, ShaderD3D11>;

    // ===========================================
    //  Misc
    // ===========================================

    struct TimerQuery
    {
        ID3D11Query* pBegin     = nullptr;
        ID3D11Query* pEnd       = nullptr;
        ID3D11Query* pDisjoint  = nullptr;
        uint64_t     frameIndex = 0;
        bool         bIssued    = false;   // Begin_ 호출됨. 결과를 모두 읽기 전까지 재사용 금지.
        bool         bEnded     = false;   // End 호출됨.
    };

public:
    explicit Graphics_D3D11(Flags<eGraphicsOption> _flags);
    ~Graphics_D3D11() override;

    // ===========================================
    //  Debug
    // ===========================================

    void SetName(VertexBufferHandle _vbh, StringView _name) override;
    void SetName(InstanceBufferHandle _instbh, StringView _name) override;
    void SetName(IndexBufferHandle _ibh, StringView _name) override;
    void SetName(ConstantBufferHandle _cbh, StringView _name) override;
    void SetName(StorageBufferHandle _sbh, StringView _name) override;
    void SetName(TextureHandle _texh, StringView _name) override;
    void SetName(FrameBufferHandle _fbh, StringView _name) override;
    void SetName(ShaderHandle _sh, StringView _name) override;

    void PushDebugGroup(StringView _name) const override;
    void PopDebugGroup() const override;
    void SetDebugMarker(StringView _name) const override;

    // ===========================================
    //  Submit
    // ===========================================

    void Touch() override;

private:
    // ===========================================
    //  Init
    // ===========================================

    void InitDevice_();
    void InitDebugInterfaces_();
    void InitTimerQueries_();
    void CleanUpTimerQueries_();

    // ===========================================
    //  Diagnostics
    // ===========================================

    void FlushDebugMessages_();

    // ===========================================
    //  System
    // ===========================================

    void Frame_() override;
    void CompleteReadback_(const ReadbackRequest& _request) override;

    // ===========================================
    //  Create
    // ===========================================

    void CreateVertexBuffer_(VertexBufferHandle _vbh, MemoryView _initDataOrEmpty) override;
    void CreateInstanceBuffer_(InstanceBufferHandle _instbh) override;
    void CreateIndexBuffer_(IndexBufferHandle _ibh, MemoryView _initDataOrEmpty) override;
    void CreateConstantBuffer_(ConstantBufferHandle _cbh) override;
    void CreateStorageBuffer_(StorageBufferHandle _sbh, MemoryView _initDataOrEmpty) override;
    void CreateTexture_(TextureHandle _texh, MemoryView _initDataOrEmpty) override;
    void CreateFrameBuffer_(FrameBufferHandle _fbh) override;
    void CreateSwapChain_(FrameBufferHandle _fbh) override;
    void CreateShader_(ShaderHandle _sh, const ShaderBytecode& _bytecode) override;

    // ===========================================
    //  Destroy
    // ===========================================

    void Destroy_(VertexBufferHandle _vbh) override;
    void Destroy_(InstanceBufferHandle _instbh) override;
    void Destroy_(IndexBufferHandle _ibh) override;
    void Destroy_(ConstantBufferHandle _cbh) override;
    void Destroy_(StorageBufferHandle _sbh) override;
    void Destroy_(TextureHandle _texh) override;
    void Destroy_(FrameBufferHandle _fbh) override;
    void Destroy_(ShaderHandle _sh) override;

    // ===========================================
    //  Buffer
    // ===========================================

    void                            UpdateBuffer_(BufferRef _buffer, MemoryView _data, uint32_t _offset) override;
    [[nodiscard]] MutableMemoryView MapBuffer_(BufferRef _buffer) override;
    void                            UnmapBuffer_(BufferRef _buffer) override;
    void                            CopyBuffer_(BufferRef _dst, uint32_t _dstOffset, BufferRef _src, uint32_t _srcOffset, uint32_t _byteWidth) override;

    [[nodiscard]] ID3D11Buffer* GetD3d11Buffer_(
        BufferRef _buffer) const;

    // ===========================================
    //  Texture
    // ===========================================

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
        MemoryView    _data) override;

    void CopyTexture_(
        TextureHandle _dstTexh,
        TextureHandle _srcTexh) override;

    void CopyTexture_(
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
        uint32_t      _width,
        uint32_t      _height,
        uint32_t      _depth) override;

    [[nodiscard]] DXGI_SAMPLE_DESC MakeSampleDesc_(
        DXGI_FORMAT _format,
        eMSAA       _msaa) const;

    // ===========================================
    //  Frame Buffer
    // ===========================================

    void ResizeSwapChain_(FrameBufferHandle _swapChainFbh, uint32_t _width, uint32_t _height) override;
    void ClearRenderTarget_(FrameBufferHandle _fbh, RGBA _color, uint32_t _slot) override;
    void ClearDepthStencil_(FrameBufferHandle _fbh, bool _bClearDepth, bool _bClearStencil, float _depth, uint8_t _stencil) override;

    // ===========================================
    //  Execute
    // ===========================================

    void Submit_() override;
    void Submit_(StorageBufferHandle _indirectSbh, uint32_t _offset, uint32_t _numDraws) override;
    void Dispatch_(uint32_t _numGroupsX, uint32_t _numGroupsY, uint32_t _numGroupsZ) override;
    void Dispatch_(StorageBufferHandle _indirectSbh, uint32_t _offset) override;

    // ===========================================
    //  Resources
    // ===========================================

    [[nodiscard]] ID3D11InputLayout*       GetOrCreateD3d11InputLayoutOrNull_();
    [[nodiscard]] ID3D11SamplerState*      GetOrCreateSamplerState_(Flags<eSampler> _flags, RGBA _border);
    [[nodiscard]] ID3D11RasterizerState*   GetOrCreateRasterizerState_();
    [[nodiscard]] ID3D11BlendState*        GetOrCreateBlendState_();
    [[nodiscard]] ID3D11DepthStencilState* GetOrCreateDepthStencilState_();

    // 핸들 -> D3D 뷰. 핸들이 비어 있으면 nullptr.
    [[nodiscard]] ID3D11ShaderResourceView*  GetSrvOrNull_(ResourceRef _resource) const;
    [[nodiscard]] ID3D11UnorderedAccessView* GetUavOrNull_(ResourceRef _resource) const;

    // 바인딩의 dirty 범위를 D3D 포인터 배열(미러)로 변환한다.
    void SyncCBuffers_(eShader _shader);
    void SyncSrvs_(eShader _shader);
    void SyncSamplers_(eShader _shader);
    void SyncUavs_(eShaderRW _shader);

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
    uint64_t                   m_numDebugMsgsDropped   = 0;

    // ===========================================
    //  Resource Storage
    // ===========================================

    VertexBufferStorage   m_vertexBuffers   = {};
    InstanceBufferStorage m_instanceBuffers = {};
    IndexBufferStorage    m_indexBuffers    = {};
    StorageBufferStorage  m_storageBuffers  = {};
    ConstantBufferStorage m_constantBuffers = {};
    TextureStorage        m_textures        = {};
    FrameBufferStorage    m_frameBuffers    = {};
    ShaderStorage         m_shaders         = {};

    NoopHashMap<uint64_t, ID3D11InputLayout*>       m_d3d11InputLayoutCache       = {};
    NoopHashMap<uint64_t, ID3D11BlendState*>        m_d3d11BlendStateCache        = {};
    NoopHashMap<uint64_t, ID3D11RasterizerState*>   m_d3d11RasterizerStateCache   = {};
    NoopHashMap<uint64_t, ID3D11DepthStencilState*> m_d3d11DepthStencilStateCache = {};
    NoopHashMap<uint64_t, ID3D11SamplerState*>      m_d3d11SamplerStateCache      = {};

    ID3D11Buffer* m_pUploadBuffer         = nullptr;
    uint32_t      m_uploadBufferByteWidth = 0;

    // ===========================================
    //  Pipeline State Mirror
    //   기반의 PipelineState 는 핸들만 갖는다. Set*Resources 가 연속 포인터 배열을 요구하므로 변환한 포인터를 여기에 둔다.
    // ===========================================

    ARRAY<ID3D11Buffer*, kNumMaxStreams>                                            m_d3d11VertexBuffers = {};
    ENUM_ARRAY<eShader, ARRAY<ID3D11Buffer*, kNumMaxCBufferSlots>>                  m_d3d11CBuffers      = {};
    ENUM_ARRAY<eShader, ARRAY<ID3D11ShaderResourceView*, kNumMaxReadSlots>>         m_d3d11Srvs          = {};
    ENUM_ARRAY<eShader, ARRAY<ID3D11SamplerState*, kNumMaxSamplerSlots>>            m_d3d11Samplers      = {};
    ENUM_ARRAY<eShaderRW, ARRAY<ID3D11UnorderedAccessView*, kNumMaxReadWriteSlots>> m_d3d11Uavs          = {};

    // ===========================================
    //  Timer
    // ===========================================

    static constexpr size_t kNumTimerQueries = 16;
    RingBuffer<TimerQuery>  m_timerQueries { kNumTimerQueries };
};

}   // namespace jug
