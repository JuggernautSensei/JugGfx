#pragma once
#include "Base.h"

namespace jug
{

enum class ePass
{
    Render,
    Compute,
    Utility,
};

class RenderGraph
{
    JUG_CLASS(RenderGraph, NO_COPY, NO_MOVE)

    template<HandleT H>
    struct Resource
    {
        using Type = H;

        H    handle     = kNullHandle;
        bool bOwnership = false;
    };

    struct Sampler
    {
        [[nodiscard]] bool operator==(const Sampler& _other) const = default;

        Flags<eSampler> flags  = {};
        RGBA            border = {};
    };

    struct BindDecl
    {
        String   name = {};
        uint32_t slot = 0;
    };

    struct SamplerDecl
    {
        Sampler  state = {};
        uint32_t slot  = 0;
    };

    struct ClearDecl
    {
        RGBA     color = {};
        uint32_t slot  = 0;
    };

    struct BlendDecl
    {
        Flags<eBlend> flags = {};
        uint32_t      slot  = 0;
    };

    struct PassDesc
    {
        String name        = {};
        ePass  type        = ePass::Render;
        bool   bEnable     = true;
        bool   bSideEffect = false;
        bool   bFinalPass  = false;

        String programName     = {};
        String frameBufferName = {};

        bool  bViewportFromFrameBuffer = true;
        float viewportX                = 0.f;
        float viewportY                = 0.f;
        float viewportW                = 0.f;
        float viewportH                = 0.f;

        int scissorX = 0;
        int scissorY = 0;
        int scissorW = 0;
        int scissorH = 0;

        Flags<eRenderState> renderState     = {};
        Flags<eStencil>     frontStencil    = {};
        Flags<eStencil>     backStencil     = {};
        uint8_t             stencilRef      = 0;
        RGBA                blendFactor     = {};
        bool                bHasBlendFactor = false;

        Vector<BlendDecl> blends = {};

        Vector<ClearDecl> renderTargetClears = {};
        bool              bClearDepth        = false;
        bool              bClearStencil      = false;
        float             depthClearValue    = 1.f;
        uint8_t           stencilClearValue  = 0;

        Vector<BindDecl>    reads      = {};
        Vector<BindDecl>    readWrites = {};
        Vector<BindDecl>    cbuffers   = {};
        Vector<SamplerDecl> samplers   = {};

        Callable<void()> executor = {};
    };

    struct ResolvedPass
    {
        ProgramHandle     ph  = kNullHandle;
        FrameBufferHandle fbh = kNullHandle;

        Vector<ResourceRef> readRefs      = {};
        Vector<ResourceRef> readWriteRefs = {};
        Vector<ResourceRef> writeRefs     = {};

        Vector<ConstantBufferHandle> cbufferHandles = {};

        float viewportX = 0.f;
        float viewportY = 0.f;
        float viewportW = 0.f;
        float viewportH = 0.f;
    };

    struct ResourceBind
    {
        ResourceRef resource = {};
        uint32_t    slot     = 0;
    };

    struct CBufferBind
    {
        ConstantBufferHandle cbh  = kNullHandle;
        uint32_t             slot = 0;
    };

    struct SamplerBind
    {
        Sampler  state = {};
        uint32_t slot  = 0;
    };

    struct CompiledPass
    {
        size_t descIndex = kInvalidIndex;

        uint64_t readUnbindMask     = 0;
        uint64_t rwUnbindMask       = 0;
        bool     bUnbindFrameBuffer = false;

        Vector<ResourceBind> reads      = {};
        Vector<ResourceBind> readWrites = {};
        Vector<CBufferBind>  cbuffers   = {};
        Vector<SamplerBind>  samplers   = {};
    };

public:
    // ===========================================
    //  Pass Builder
    // ===========================================

    class UtilityPassBuilder
    {
    public:
        UtilityPassBuilder(RenderGraph* _pGraph, size_t _index);

        UtilityPassBuilder& Enable(bool _bEnable = true);
        UtilityPassBuilder& SetExecutor(const Callable<void()>& _executor);

    private:
        RenderGraph* m_pGraph = nullptr;
        size_t       m_index  = kInvalidIndex;
    };

    class RenderPassBuilder
    {
    public:
        RenderPassBuilder(RenderGraph* _pGraph, size_t _index);

        RenderPassBuilder& Enable(bool _bEnable = true);
        RenderPassBuilder& AsFinalPass(bool _bEnable = true);
        RenderPassBuilder& AsSideEffect(bool _bEnable = true);

        RenderPassBuilder& SetProgram(StringView _name);
        RenderPassBuilder& SetFrameBuffer(StringView _name);

        RenderPassBuilder& SetViewport(float _x, float _y, float _width, float _height);
        RenderPassBuilder& SetScissor(int _x, int _y, int _width, int _height);

        RenderPassBuilder& SetRenderState(Flags<eRenderState> _flags);
        RenderPassBuilder& SetStencil(Flags<eStencil> _front, Flags<eStencil> _back, uint8_t _ref = 0);
        RenderPassBuilder& SetBlend(Flags<eBlend> _flags, uint32_t _slot = 0);
        RenderPassBuilder& SetBlendFactor(RGBA _factor);

        RenderPassBuilder& ClearRenderTarget(RGBA _color, uint32_t _slot = 0);
        RenderPassBuilder& ClearDepthStencil(bool _bDepth, bool _bStencil, float _depth = 1.f, uint8_t _stencil = 0);

        RenderPassBuilder& SetTexture(StringView _name, eShader _shader, uint32_t _slot);
        RenderPassBuilder& SetTextureRW(StringView _name, eShaderRW _shader, uint32_t _slot);
        RenderPassBuilder& SetBuffer(StringView _name, eShader _shader, uint32_t _slot);
        RenderPassBuilder& SetBufferRW(StringView _name, eShaderRW _shader, uint32_t _slot);
        RenderPassBuilder& SetConstantBuffer(StringView _name, eShader _shader, uint32_t _slot);
        RenderPassBuilder& SetSampler(Flags<eSampler> _flags, eShader _shader, uint32_t _slot);
        RenderPassBuilder& SetSampler(Flags<eSampler> _flags, RGBA _border, eShader _shader, uint32_t _slot);

        RenderPassBuilder& SetExecutor(const Callable<void()>& _executor);

    private:
        RenderGraph* m_pGraph = nullptr;
        size_t       m_index  = kInvalidIndex;
    };

    class ComputePassBuilder
    {
    public:
        ComputePassBuilder(RenderGraph* _pGraph, size_t _index);

        ComputePassBuilder& Enable(bool _bEnable = true);
        ComputePassBuilder& AsFinalPass(bool _bEnable = true);
        ComputePassBuilder& AsSideEffect(bool _bEnable = true);

        ComputePassBuilder& SetProgram(StringView _name);

        ComputePassBuilder& SetTexture(StringView _name, uint32_t _slot);
        ComputePassBuilder& SetTextureRW(StringView _name, uint32_t _slot);
        ComputePassBuilder& SetBuffer(StringView _name, uint32_t _slot);
        ComputePassBuilder& SetBufferRW(StringView _name, uint32_t _slot);
        ComputePassBuilder& SetConstantBuffer(StringView _name, uint32_t _slot);
        ComputePassBuilder& SetSampler(Flags<eSampler> _flags, uint32_t _slot);
        ComputePassBuilder& SetSampler(Flags<eSampler> _flags, RGBA _border, uint32_t _slot);

        ComputePassBuilder& SetExecutor(const Callable<void()>& _executor);

    private:
        RenderGraph* m_pGraph = nullptr;
        size_t       m_index  = kInvalidIndex;
    };

    RenderGraph() = default;
    ~RenderGraph();

    // ===========================================
    //  Pass
    // ===========================================

    [[nodiscard]] RenderPassBuilder  AddRenderPass(StringView _name);
    [[nodiscard]] RenderPassBuilder  ModifyRenderPass(StringView _name);
    [[nodiscard]] ComputePassBuilder AddComputePass(StringView _name);
    [[nodiscard]] ComputePassBuilder ModifyComputePass(StringView _name);
    [[nodiscard]] UtilityPassBuilder AddUtilityPass(StringView _name);
    [[nodiscard]] UtilityPassBuilder ModifyUtilityPass(StringView _name);

    void               RemovePass(StringView _name);
    [[nodiscard]] bool HasPass(StringView _name) const;

    // ===========================================
    //  Resource
    // ===========================================

    void Insert(StringView _name, TextureHandle _texh, bool _bOwnership = false);
    void Insert(StringView _name, FrameBufferHandle _fbh, bool _bOwnership = false);
    void Insert(StringView _name, ConstantBufferHandle _cbh, bool _bOwnership = false);
    void Insert(StringView _name, StorageBufferHandle _sbh, bool _bOwnership = false);
    void Insert(StringView _name, ShaderHandle _sh, bool _bOwnership = false);
    void Insert(StringView _name, ProgramHandle _ph, bool _bOwnership = false);

    void InsertOrReplace(StringView _name, TextureHandle _texh);
    void InsertOrReplace(StringView _name, FrameBufferHandle _fbh);
    void InsertOrReplace(StringView _name, ConstantBufferHandle _cbh);
    void InsertOrReplace(StringView _name, StorageBufferHandle _sbh);
    void InsertOrReplace(StringView _name, ShaderHandle _sh);
    void InsertOrReplace(StringView _name, ProgramHandle _ph);

    void RemoveTexture(StringView _name);
    void RemoveFrameBuffer(StringView _name);
    void RemoveConstantBuffer(StringView _name);
    void RemoveStorageBuffer(StringView _name);
    void RemoveShader(StringView _name);
    void RemoveProgram(StringView _name);

    [[nodiscard]] TextureHandle        GetTexture(StringView _name) const;
    [[nodiscard]] FrameBufferHandle    GetFrameBuffer(StringView _name) const;
    [[nodiscard]] ConstantBufferHandle GetConstantBuffer(StringView _name) const;
    [[nodiscard]] StorageBufferHandle  GetStorageBuffer(StringView _name) const;
    [[nodiscard]] ShaderHandle         GetShader(StringView _name) const;
    [[nodiscard]] ProgramHandle        GetProgram(StringView _name) const;

    [[nodiscard]] bool HasTexture(StringView _name) const;
    [[nodiscard]] bool HasFrameBuffer(StringView _name) const;
    [[nodiscard]] bool HasConstantBuffer(StringView _name) const;
    [[nodiscard]] bool HasStorageBuffer(StringView _name) const;
    [[nodiscard]] bool HasShader(StringView _name) const;
    [[nodiscard]] bool HasProgram(StringView _name) const;

    // ===========================================
    //  Runtime
    // ===========================================

    void Execute();
    void Clear();

private:
    [[nodiscard]] size_t    GetOrAddPass_(StringView _name, ePass _type, bool _bMustExist);
    [[nodiscard]] size_t    FindPassIndexOrInvalid_(StringView _name) const;
    [[nodiscard]] PassDesc& GetPassDesc_(size_t _index);

    [[nodiscard]] ResourceRef ResolveResource_(StringView _passName, StringView _name) const;

    void CompileIfNeed_();
    void ResolveNames_();
    void BuildCompiledPasses_();

    void DestroyOwned_();

    constexpr static size_t kInvalidIndex = Max<size_t>();

    Vector<PassDesc>     m_descs          = {};
    Vector<ResolvedPass> m_resolved       = {};
    Vector<CompiledPass> m_compiledPasses = {};

    HashMap<String, Resource<StorageBufferHandle>>  m_storageBuffers  = {};
    HashMap<String, Resource<ConstantBufferHandle>> m_constantBuffers = {};
    HashMap<String, Resource<TextureHandle>>        m_textures        = {};
    HashMap<String, Resource<FrameBufferHandle>>    m_frameBuffers    = {};
    HashMap<String, Resource<ShaderHandle>>         m_shaders         = {};
    HashMap<String, Resource<ProgramHandle>>        m_programs        = {};

    bool m_bDirty = true;
};

}   // namespace jug
