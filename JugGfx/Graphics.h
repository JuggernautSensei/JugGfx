#pragma once
#include "Base.h"

namespace jug
{

struct ShaderBytecode;

class Graphics
{
    JUG_CLASS(Graphics, NO_COPY, NO_MOVE)

protected:
    // ===========================================
    //  Typedef
    // ===========================================

    using VertexBufferPool   = ResourcePool<VertexBufferHandle, VertexBufferDesc>;
    using InstanceBufferPool = ResourcePool<InstanceBufferHandle, InstanceBufferDesc>;
    using IndexBufferPool    = ResourcePool<IndexBufferHandle, IndexBufferDesc>;
    using StorageBufferPool  = ResourcePool<StorageBufferHandle, StorageBufferDesc>;
    using ConstantBufferPool = ResourcePool<ConstantBufferHandle, ConstantBufferDesc>;
    using TexturePool        = ResourcePool<TextureHandle, TextureDesc>;
    using FrameBufferPool    = ResourcePool<FrameBufferHandle, FrameBufferDesc>;
    using ShaderPool         = ResourcePool<ShaderHandle, ShaderDesc>;
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

        Flags<eSampler> flags  = eSampler::None;
        RGBA            border = {};
    };

    // 슬롯 바인딩. 핸들(값)과 dirty 범위만 든다. 백엔드 객체 포인터는 백엔드가 Apply 에서 변환한다.
    template<typename T, uint32_t kSize>
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

        ARRAY<T, kSize> resources = {};

        uint32_t dirtyBegin = 0;
        uint32_t dirtyEnd   = kSize;
    };

    using ReadBind      = ResourceBind<ResourceRef, kNumMaxReadSlots>;
    using ReadWriteBind = ResourceBind<ResourceRef, kNumMaxReadWriteSlots>;
    using CBufferBind   = ResourceBind<ConstantBufferHandle, kNumMaxCBufferSlots>;
    using SamplerBind   = ResourceBind<Sampler, kNumMaxSamplerSlots>;

    // 백엔드 중립 파이프라인 상태. 기본값이 Reset() 직후 상태다.
    struct PipelineState
    {
        // vertex stream
        ARRAY<VertexBufferHandle, kNumMaxStreams> vbhs          = {};
        ARRAY<VertexLayoutHandle, kNumMaxStreams> vlhs          = {};
        ARRAY<uint32_t, kNumMaxStreams>           vertexStrides = {};
        ARRAY<uint32_t, kNumMaxStreams>           vertexOffsets = {};   // byte
        uint32_t                                  numStreams    = 0;
        uint32_t                                  numVertices   = 0;

        // index
        IndexBufferHandle ibhOrInvalid = kInvalidHandle;
        uint32_t          indexOffset  = 0;
        uint32_t          numIndices   = 0;

        // instance
        InstanceBufferHandle instbhOrInvalid = kInvalidHandle;
        uint32_t             instanceStride  = 0;
        uint32_t             instanceOffset  = 0;   // byte
        uint32_t             numInstances    = 0;

        // resource bind
        ENUM_ARRAY<eShader, ReadBind>        readBind      = {};
        ENUM_ARRAY<eShader, CBufferBind>     cbufferBind   = {};
        ENUM_ARRAY<eShader, SamplerBind>     samplerBind   = {};
        ENUM_ARRAY<eShaderRW, ReadWriteBind> readWriteBind = {};

        // program
        ProgramHandle                      phOrInvalid        = kInvalidHandle;
        ProgramHandle                      computePhOrInvalid = kInvalidHandle;
        ENUM_ARRAY<eSubmitParam, uint32_t> submitParams       = {};

        // render state
        Flags<eRenderState>                            renderStateFlags = eRenderState::None;
        ARRAY<Flags<eBlend>, kNumMaxRenderTargetSlots> blendFlags       = {};
        RGBA                                           blendFactor      = {};
        Flags<eStencil>                                fstencilFlags    = kNoneFlags;
        Flags<eStencil>                                bstencilFlags    = kNoneFlags;
        uint8_t                                        stencilRef       = 0;

        // output
        FrameBufferHandle fbhOrInvalid     = kInvalidHandle;
        FrameBufferHandle lastFbhOrInvalid = kInvalidHandle;   // 마지막으로 적용된 FB. 리졸브용. 백엔드 Apply 가 갱신

        float viewportX = 0.f;
        float viewportY = 0.f;
        float viewportW = 0.f;
        float viewportH = 0.f;

        int scissorX = 0;
        int scissorY = 0;
        int scissorW = 0;
        int scissorH = 0;

        Flags<ePipelineDirty> dirtyFlags = kAllFlags;
    };

    // ===========================================
    //  Misc
    // ===========================================

    struct ResolveBufferRefResult
    {
        uint32_t byteWidth = 0;
        bool     bDynamic  = false;
    };

    // 스테이징 읽기 요청. readyFrame 에 도달한 Frame() 에서 CompleteReadback_ 가 호출된다.
    struct ReadbackRequest
    {
        ResourceRef       resource   = {};
        uint32_t          index      = 0;
        uint32_t          rowPitch   = 0;   // tight 줄 바이트 수
        uint32_t          numRows    = 0;   // 슬라이스당 줄 수(compressed 면 블록 줄)
        uint32_t          depth      = 0;
        MutableMemoryView dst        = {};
        uint64_t          readyFrame = 0;
    };

public:
    virtual ~Graphics();

    [[nodiscard]] static Graphics& GetSingleton();
    [[nodiscard]] eGraphicsAPI     GetAPI() const;

    // ===========================================
    //  System
    // ===========================================

    void Frame();

    size_t                             ReportLiveObjects();
    [[nodiscard]] const GraphicsCaps&  GetCaps() const;
    [[nodiscard]] const GraphicsStats& GetStats() const;
    [[nodiscard]] uint64_t             GetFrameIndex() const;

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
        Flags<eStorageBufferOption> _flags = eStorageBufferOption::None);

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
        uint32_t  _byteWidthOrInvalid);   // kInvalidIndex 이면 src 끝까지

    // GetFrameIndex() >= 반환값 이면 _dst 가 채워져 있다. 그때까지 _dst 는 호출자가 유지해야 한다.
    [[nodiscard]] uint64_t ReadBuffer(
        StorageBufferHandle _readbackSbh,
        MutableMemoryView   _dst);

    // ===========================================
    //  Texture
    // ===========================================

    [[nodiscard]] TextureHandle CreateTexture2D(
        uint32_t              _width,
        uint32_t              _height,
        eTextureFormat        _format,
        uint32_t              _numLayers       = 1,
        eMSAA                 _msaa            = eMSAA::None,
        Flags<eTextureOption> _flags           = eTextureOption::None,
        MemoryView            _initDataOrEmpty = {});

    [[nodiscard]] TextureHandle CreateTextureCube(
        uint32_t              _width,
        uint32_t              _height,
        eTextureFormat        _format,
        uint32_t              _numCubes        = 1,
        Flags<eTextureOption> _flags           = eTextureOption::None,
        MemoryView            _initDataOrEmpty = {});

    [[nodiscard]] TextureHandle CreateTexture3D(
        uint32_t              _width,
        uint32_t              _height,
        uint32_t              _depth,
        eTextureFormat        _format,
        Flags<eTextureOption> _flags           = eTextureOption::None,
        MemoryView            _initDataOrEmpty = {});

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
        uint32_t      _widthOrInvalid,   // kInvalidIndex 이면 src 끝까지
        uint32_t      _heightOrInvalid,
        uint32_t      _depthOrInvalid);

    // GetFrameIndex() >= 반환값 이면 _dst 가 채워져 있다. 그때까지 _dst 는 호출자가 유지해야 한다.
    [[nodiscard]] uint64_t ReadTexture(
        TextureHandle     _readbackTexh,
        MutableMemoryView _dst,
        uint32_t          _layer = 0,
        uint32_t          _mip   = 0);

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

    [[nodiscard]] FrameBufferHandle FindFrameBufferOrInvalid(
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
        const ShaderBytecode& _bytecode);

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

    virtual void SetName(VertexBufferHandle _vbh, StringView _name)      = 0;
    virtual void SetName(InstanceBufferHandle _instbh, StringView _name) = 0;
    virtual void SetName(IndexBufferHandle _ibh, StringView _name)       = 0;
    virtual void SetName(ConstantBufferHandle _cbh, StringView _name)    = 0;
    virtual void SetName(StorageBufferHandle _sbh, StringView _name)     = 0;
    virtual void SetName(TextureHandle _texh, StringView _name)          = 0;
    virtual void SetName(FrameBufferHandle _fbh, StringView _name)       = 0;
    virtual void SetName(ShaderHandle _sh, StringView _name)             = 0;

    virtual void PushDebugGroup(StringView _name) const = 0;
    virtual void PopDebugGroup() const                  = 0;
    virtual void SetDebugMarker(StringView _name) const = 0;

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
        VertexBufferHandle _vbhOrInvalid,
        uint32_t           _offset               = 0,
        uint32_t           _numVerticesOrInvalid = kInvalidIndex);   // kInvalidIndex 이면 버퍼 끝까지

    void SetVertexBuffers(
        Span<const VertexStream> _streams,
        uint32_t                 _numVerticesOrInvalid = kInvalidIndex);   // kInvalidIndex 이면 스트림 전체

    void SetVertexBuffer(
        VertexBufferHandle   _vbhOrInvalid,
        uint32_t             _offset,
        uint32_t             _numVerticesOrInvalid,
        InstanceBufferHandle _instbhOrInvalid,
        uint32_t             _instanceOffset        = 0,
        uint32_t             _numInstancesOrInvalid = kInvalidIndex);   // kInvalidIndex 이면 버퍼 끝까지

    void SetVertexBuffers(
        Span<const VertexStream> _streams,
        uint32_t                 _numVerticesOrInvalid,
        InstanceBufferHandle     _instbhOrInvalid,
        uint32_t                 _instanceOffset        = 0,
        uint32_t                 _numInstancesOrInvalid = kInvalidIndex);   // kInvalidIndex 이면 버퍼 끝까지

    void SetIndexBuffer(
        IndexBufferHandle _ibhOrInvalid,
        uint32_t          _offset              = 0,
        uint32_t          _numIndicesOrInvalid = kInvalidIndex);   // kInvalidIndex 이면 버퍼 끝까지

    void SetConstantBuffer(
        ConstantBufferHandle _cbhOrInvalid,
        eShader              _shader,
        uint32_t             _slot);

    void SetTexture(
        TextureHandle _texhOrInvalid,
        eShader       _shader,
        uint32_t      _slot);

    void SetTextureRW(
        TextureHandle _texhOrInvalid,
        eShaderRW     _shader,
        uint32_t      _slot);

    void SetBuffer(
        StorageBufferHandle _sbhOrInvalid,
        eShader             _shader,
        uint32_t            _slot);

    void SetBufferRW(
        StorageBufferHandle _sbhOrInvalid,
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
        Flags<eStencil> _frontFlags,
        Flags<eStencil> _backFlags,
        uint8_t         _stencilRef = 0);

    void SetProgram(
        ProgramHandle _phOrInvalid);

    void SetComputeProgram(
        ProgramHandle _phOrInvalid);

    void SetFrameBuffer(
        FrameBufferHandle _fbhOrInvalid);

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

    virtual void Touch() = 0;
    void         Reset();

    void Submit();

    void Submit(
        StorageBufferHandle _indirectSbh,
        uint32_t            _offset            = 0,
        uint32_t            _numDrawsOrInvalid = kInvalidIndex);   // kInvalidIndex 이면 버퍼 끝까지

    void Dispatch(
        uint32_t _numGroupsX,
        uint32_t _numGroupsY,
        uint32_t _numGroupsZ);

    void Dispatch(
        StorageBufferHandle _indirectSbh,
        uint32_t            _offset = 0);

protected:
    Graphics(eGraphicsAPI _api, Flags<eGraphicsOption> _flags);

    [[nodiscard]] ResolveBufferRefResult ResolveBufferRef_(
        BufferRef _buffer) const;

    // ===========================================
    //  Backend : System
    // ===========================================

    virtual void Frame_()                                           = 0;   // present, 타이머, 메모리, 리드백 Flush, 디버그 메시지
    virtual void CompleteReadback_(const ReadbackRequest& _request) = 0;   // _request.dst 를 채운다

    // ===========================================
    //  Backend : Create
    //   기반이 검증하고 Desc 를 풀에 넣어 핸들을 발급한 뒤 호출한다. 백엔드는 풀의 Desc 를 읽고 자기 저장소에 넣는다.
    // ===========================================

    virtual void CreateVertexBuffer_(VertexBufferHandle _vbh, MemoryView _initDataOrEmpty)   = 0;
    virtual void CreateInstanceBuffer_(InstanceBufferHandle _instbh)                         = 0;
    virtual void CreateIndexBuffer_(IndexBufferHandle _ibh, MemoryView _initDataOrEmpty)     = 0;
    virtual void CreateConstantBuffer_(ConstantBufferHandle _cbh)                            = 0;
    virtual void CreateStorageBuffer_(StorageBufferHandle _sbh, MemoryView _initDataOrEmpty) = 0;   // desc.type 으로 구분
    virtual void CreateTexture_(TextureHandle _texh, MemoryView _initDataOrEmpty)            = 0;   // 지원 안 되는 msaa 는 desc 에 하향 기록
    virtual void CreateFrameBuffer_(FrameBufferHandle _fbh)                                  = 0;   // desc.atts 로 뷰 생성
    virtual void CreateSwapChain_(FrameBufferHandle _fbh)                                    = 0;   // atts[0] 텍스처도 함께 만든다
    virtual void CreateShader_(ShaderHandle _sh, const ShaderBytecode& _bytecode)            = 0;

    // ===========================================
    //  Backend : Destroy
    //   기반이 refCount, 바인딩 해제, 검사를 마친 뒤 풀에서 지우기 직전에 호출한다.
    // ===========================================

    virtual void Destroy_(VertexBufferHandle _vbh)      = 0;
    virtual void Destroy_(InstanceBufferHandle _instbh) = 0;
    virtual void Destroy_(IndexBufferHandle _ibh)       = 0;
    virtual void Destroy_(ConstantBufferHandle _cbh)    = 0;
    virtual void Destroy_(StorageBufferHandle _sbh)     = 0;
    virtual void Destroy_(TextureHandle _texh)          = 0;
    virtual void Destroy_(FrameBufferHandle _fbh)       = 0;
    virtual void Destroy_(ShaderHandle _sh)             = 0;

    // ===========================================
    //  Backend : Buffer
    // ===========================================

    virtual void                            UpdateBuffer_(BufferRef _buffer, MemoryView _data, uint32_t _offset)                                       = 0;
    [[nodiscard]] virtual MutableMemoryView MapBuffer_(BufferRef _buffer)                                                                              = 0;
    virtual void                            UnmapBuffer_(BufferRef _buffer)                                                                            = 0;
    virtual void                            CopyBuffer_(BufferRef _dst, uint32_t _dstOffset, BufferRef _src, uint32_t _srcOffset, uint32_t _byteWidth) = 0;

    // ===========================================
    //  Backend : Texture
    // ===========================================

    virtual void UpdateTexture_(
        TextureHandle _texh,
        uint32_t      _mip,
        uint32_t      _layer,
        uint32_t      _offsetX,
        uint32_t      _offsetY,
        uint32_t      _offsetZ,
        uint32_t      _width,
        uint32_t      _height,
        uint32_t      _depth,
        MemoryView    _data) = 0;

    virtual void CopyTexture_(   // 모든 서브리소스
        TextureHandle _dstTexh,
        TextureHandle _srcTexh) = 0;

    virtual void CopyTexture_(   // 크기는 기반이 해석을 마친 값이다
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
        uint32_t      _depth) = 0;

    // ===========================================
    //  Backend : Frame Buffer
    // ===========================================

    virtual void ResizeSwapChain_(FrameBufferHandle _swapChainFbh, uint32_t _width, uint32_t _height)                               = 0;
    virtual void ClearRenderTarget_(FrameBufferHandle _fbh, RGBA _color, uint32_t _slot)                                            = 0;
    virtual void ClearDepthStencil_(FrameBufferHandle _fbh, bool _bClearDepth, bool _bClearStencil, float _depth, uint8_t _stencil) = 0;

    // ===========================================
    //  Backend : Execute
    // ===========================================

    virtual void Submit_()                                                                       = 0;
    virtual void Submit_(StorageBufferHandle _indirectSbh, uint32_t _offset, uint32_t _numDraws) = 0;
    virtual void Dispatch_(uint32_t _numGroupsX, uint32_t _numGroupsY, uint32_t _numGroupsZ)     = 0;
    virtual void Dispatch_(StorageBufferHandle _indirectSbh, uint32_t _offset)                   = 0;

    // ===========================================
    //  Data
    // ===========================================

    eGraphicsAPI m_api = eGraphicsAPI::Noop;

    GraphicsCaps  m_caps       = {};
    GraphicsStats m_stats      = {};
    uint64_t      m_frameIndex = 0;

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

    Vector<FrameBufferHandle> m_swapChainFbhs = {};

    PipelineState m_state = {};

    bool m_bReadbackNeedFlush = false;

private:
    // ===========================================
    //  Buffer
    // ===========================================

    [[nodiscard]] VertexBufferHandle EmplaceVertexBuffer_(
        uint32_t            _numElems,
        const VertexLayout& _vl,
        bool                _bDynamic,
        MemoryView          _initDataOrEmpty);

    [[nodiscard]] IndexBufferHandle EmplaceIndexBuffer_(
        uint32_t   _numElems,
        bool       _bU32,
        bool       _bDynamic,
        MemoryView _initDataOrEmpty);

    [[nodiscard]] VertexLayoutHandle GetOrCreateVertexLayoutHandle_(
        const VertexLayout& _vl);

    void ReleaseVertexLayout_(
        VertexLayoutHandle _vlh);

    [[nodiscard]] uint64_t RequestReadback_(
        const ReadbackRequest& _request);

    [[nodiscard]] bool IsReadbackPending_(
        ResourceRef _resource) const;

    // ===========================================
    //  Texture
    // ===========================================

    [[nodiscard]] TextureHandle EmplaceTexture_(
        uint32_t              _width,
        uint32_t              _height,
        uint32_t              _depth,
        eTexture              _type,
        eTextureFormat        _format,
        uint32_t              _numLayers,
        eMSAA                 _msaa,
        Flags<eTextureOption> _flags,
        MemoryView            _initDataOrEmpty);

    void UpdateTextureChecked_(
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
    //  Pipeline
    // ===========================================

    void UnbindResource_(
        ResourceRef _resource);

    // ===========================================
    //  Data
    // ===========================================

    GraphicsStats m_lastStats = {};

    NoopHashMap<uint64_t, VertexLayoutHandle> m_vlhCache    = {};
    NoopHashMap<uint64_t, ShaderHandle>       m_shaderCache = {};

    static constexpr uint64_t kReadbackLatency = 1;

    Vector<ReadbackRequest> m_readbackRequests = {};
};

}   // namespace jug

#define JUG_GFX_DESTROY(_handle)                        \
    JUG_BEGIN_MACRO_BLOCK                               \
    if (_handle != jug::kInvalidHandle)                 \
    {                                                   \
        jug::Graphics::GetSingleton().Destroy(_handle); \
        _handle = jug::kInvalidHandle;                  \
    }                                                   \
    JUG_END_MACRO_BLOCK
