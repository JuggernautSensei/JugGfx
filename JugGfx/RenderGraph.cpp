#include "pch.h"
#include "RenderGraph.h"

#include "Graphics.h"

#include <algorithm>
#include <bit>
#include <ranges>

namespace jug
{
namespace
{
    constexpr uint32_t kSlotShift = 4;
    constexpr uint32_t kSlotMask  = (1u << kSlotShift) - 1u;

    constexpr uint32_t kNumReadSlots      = static_cast<uint32_t>(CountOf<eShader>()) * (1u << kSlotShift);
    constexpr uint32_t kNumReadWriteSlots = static_cast<uint32_t>(CountOf<eShaderRW>()) * (1u << kSlotShift);

    static_assert(kNumMaxReadSlots <= (1u << kSlotShift), "Too many read slots.");
    static_assert(kNumMaxReadWriteSlots <= (1u << kSlotShift), "Too many read-write slots.");
    static_assert(kNumMaxCBufferSlots <= (1u << kSlotShift), "Too many constant buffer slots.");
    static_assert(kNumMaxSamplerSlots <= (1u << kSlotShift), "Too many sampler slots.");
    static_assert(kNumReadSlots <= 64, "Read unbind mask does not fit in uint64_t.");
    static_assert(kNumReadWriteSlots <= 64, "Read-write unbind mask does not fit in uint64_t.");

    [[nodiscard]] uint32_t MakeSlot_(
        const eShader  _shader,
        const uint32_t _slot,
        const uint32_t _numPerStage)
    {
        JUG_ASSERT(_slot < _numPerStage, "Slot out of range.");
        return (static_cast<uint32_t>(_shader) << kSlotShift) | _slot;
    }

    [[nodiscard]] uint32_t MakeSlotRW_(
        const eShaderRW _shader,
        const uint32_t  _slot,
        const uint32_t  _numPerStage)
    {
        JUG_ASSERT(_slot < _numPerStage, "Slot out of range.");
        return (static_cast<uint32_t>(_shader) << kSlotShift) | _slot;
    }

    template<typename Map, typename Mapped = typename Map::mapped_type, typename Handle = typename Mapped::Type>
    void InsertResource_(
        Map&             _map,
        const StringView _name,
        const Handle     _handle,
        const bool       _bOwnership)
    {
        JUG_ASSERT(!_name.empty(), "Resource name must not be empty.");
        JUG_ASSERT(_handle, "Cannot register a null handle. name = '{}'", _name);
        JUG_ASSERT(!_map.contains(_name), "Resource '{}' is already registered.", _name);

        _map.emplace(String { _name }, Mapped { _handle, _bOwnership });
    }

    template<typename Map, typename Mapped = typename Map::mapped_type, typename Handle = typename Mapped::Type>
    void InsertOrReplaceResource_(
        Map&             _map,
        const StringView _name,
        const Handle     _handle)
    {
        JUG_ASSERT(!_name.empty(), "Resource name must not be empty.");
        JUG_ASSERT(_handle, "Cannot replace with a null handle. name = '{}'", _name);

        const auto it = _map.find(_name);
        if (it == _map.end())
        {
            _map.emplace(String { _name }, Mapped { _handle, false });
            return;
        }

        if (it->second.bOwnership && it->second.handle != _handle)
        {
            Graphics::GetSingleton().Destroy(it->second.handle);
        }

        it->second.handle = _handle;
    }

    template<typename Map>
    void RemoveResource_(
        Map&             _map,
        const StringView _name)
    {
        const auto it = _map.find(_name);
        if (it == _map.end())
        {
            return;
        }

        if (it->second.bOwnership)
        {
            Graphics::GetSingleton().Destroy(it->second.handle);
        }

        _map.erase(it);
    }

    template<typename Map>
    [[nodiscard]] auto GetResource_(
        const Map&       _map,
        const StringView _name)
    {
        const auto it = _map.find(_name);
        JUG_ASSERT(it != _map.end(), "Resource '{}' is not registered.", _name);
        return it->second.handle;
    }

    template<typename Map>
    void DestroyOwnedResources_(
        Map& _map)
    {
        Graphics& gfx = Graphics::GetSingleton();
        for (auto& res: _map | std::views::values)
        {
            if (res.bOwnership && res.handle)
            {
                gfx.Destroy(res.handle);
            }
        }
        _map.clear();
    }

}   // namespace

RenderGraph::UtilityPassBuilder::UtilityPassBuilder(
    RenderGraph* _pGraph,
    const size_t _index)
    : m_pGraph(_pGraph)
    , m_index(_index)
{
}

RenderGraph::UtilityPassBuilder& RenderGraph::UtilityPassBuilder::Enable(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bEnable = _bEnable;
    return *this;
}

RenderGraph::UtilityPassBuilder& RenderGraph::UtilityPassBuilder::SetExecutor(
    const Callable<void()>& _executor)
{
    m_pGraph->GetPassDesc_(m_index).executor = _executor;
    return *this;
}

RenderGraph::RenderPassBuilder::RenderPassBuilder(
    RenderGraph* _pGraph,
    const size_t _index)
    : m_pGraph(_pGraph)
    , m_index(_index)
{
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::Enable(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bEnable = _bEnable;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::AsFinalOutput(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bFinalOutput = _bEnable;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::AsSideEffect(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bSideEffect = _bEnable;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetProgram(
    const StringView _name)
{
    m_pGraph->GetPassDesc_(m_index).programName = _name;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetFrameBuffer(
    const StringView _name)
{
    m_pGraph->GetPassDesc_(m_index).frameBufferName = _name;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetViewport(
    const float _x,
    const float _y,
    const float _width,
    const float _height)
{
    PassDesc& desc                = m_pGraph->GetPassDesc_(m_index);
    desc.bViewportFromFrameBuffer = false;
    desc.viewportX                = _x;
    desc.viewportY                = _y;
    desc.viewportW                = _width;
    desc.viewportH                = _height;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetScissor(
    const int _x,
    const int _y,
    const int _width,
    const int _height)
{
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    desc.scissorX  = _x;
    desc.scissorY  = _y;
    desc.scissorW  = _width;
    desc.scissorH  = _height;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetRenderState(
    const Flags<eRenderState> _flags)
{
    m_pGraph->GetPassDesc_(m_index).renderState = _flags;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetStencil(
    const Flags<eStencil> _front,
    const Flags<eStencil> _back,
    const uint8_t         _stencilRef)
{
    PassDesc& desc    = m_pGraph->GetPassDesc_(m_index);
    desc.frontStencil = _front;
    desc.backStencil  = _back;
    desc.stencilRef   = _stencilRef;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetBlend(
    const Flags<eBlend> _flags,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxRenderTargetSlots, "Blend slot out of range.");
    PassDesc&  desc = m_pGraph->GetPassDesc_(m_index);
    BlendDecl& decl = desc.blends.emplace_back();
    decl.flags      = _flags;
    decl.slot       = _slot;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetBlendFactor(
    const RGBA _factor)
{
    PassDesc& desc       = m_pGraph->GetPassDesc_(m_index);
    desc.blendFactor     = _factor;
    desc.bHasBlendFactor = true;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::ClearRenderTarget(
    const RGBA     _color,
    const uint32_t _slot)
{
    JUG_ASSERT(_slot < kNumMaxRenderTargetSlots, "Render target slot out of range.");
    PassDesc&  desc = m_pGraph->GetPassDesc_(m_index);
    ClearDecl& decl = desc.renderTargetClears.emplace_back();
    decl.color      = _color;
    decl.slot       = _slot;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::ClearDepthStencil(
    const bool    _bDepth,
    const bool    _bStencil,
    const float   _depth,
    const uint8_t _stencil)
{
    PassDesc& desc         = m_pGraph->GetPassDesc_(m_index);
    desc.bClearDepth       = _bDepth;
    desc.bClearStencil     = _bStencil;
    desc.depthClearValue   = _depth;
    desc.stencilClearValue = _stencil;
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetTexture(
    const StringView _name,
    const eShader    _shader,
    const uint32_t   _slot)
{
    JUG_ASSERT(_shader != eShader::Compute, "Render pass cannot bind to the compute stage.");
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    BindDecl& decl = desc.reads.emplace_back();
    decl.name      = String { _name };
    decl.slot      = MakeSlot_(_shader, _slot, kNumMaxReadSlots);
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetTextureRW(
    const StringView _name,
    const eShaderRW  _shader,
    const uint32_t   _slot)
{
    JUG_ASSERT(_shader == eShaderRW::Pixel, "Render pass cannot bind to the compute stage.");
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    BindDecl& decl = desc.readWrites.emplace_back();
    decl.name      = String { _name };
    decl.slot      = MakeSlotRW_(_shader, _slot, kNumMaxReadWriteSlots);
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetBuffer(
    const StringView _name,
    const eShader    _shader,
    const uint32_t   _slot)
{
    return SetTexture(_name, _shader, _slot);
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetBufferRW(
    const StringView _name,
    const eShaderRW  _shader,
    const uint32_t   _slot)
{
    return SetTextureRW(_name, _shader, _slot);
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetConstantBuffer(
    const StringView _name,
    const eShader    _shader,
    const uint32_t   _slot)
{
    JUG_ASSERT(_shader != eShader::Compute, "Render pass cannot bind to the compute stage.");
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    BindDecl& decl = desc.cbuffers.emplace_back();
    decl.name      = String { _name };
    decl.slot      = MakeSlot_(_shader, _slot, kNumMaxCBufferSlots);
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetSampler(
    const Flags<eSampler> _flags,
    const eShader         _shader,
    const uint32_t        _slot)
{
    return SetSampler(_flags, RGBA {}, _shader, _slot);
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetSampler(
    const Flags<eSampler> _flags,
    const RGBA            _border,
    const eShader         _shader,
    const uint32_t        _slot)
{
    JUG_ASSERT(_shader != eShader::Compute, "Render pass cannot bind to the compute stage.");
    PassDesc&    desc = m_pGraph->GetPassDesc_(m_index);
    SamplerDecl& decl = desc.samplers.emplace_back();
    decl.state.flags  = _flags;
    decl.state.border = _border;
    decl.slot         = MakeSlot_(_shader, _slot, kNumMaxSamplerSlots);
    return *this;
}

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::SetExecutor(
    const Callable<void()>& _executor)
{
    m_pGraph->GetPassDesc_(m_index).executor = _executor;
    return *this;
}

RenderGraph::ComputePassBuilder::ComputePassBuilder(
    RenderGraph* _pGraph,
    const size_t _index)
    : m_pGraph(_pGraph)
    , m_index(_index)
{
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::Enable(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bEnable = _bEnable;
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::AsFinalOutput(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bFinalOutput = _bEnable;
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::AsSideEffect(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bSideEffect = _bEnable;
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetProgram(
    const StringView _name)
{
    m_pGraph->GetPassDesc_(m_index).programName = _name;
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetTexture(
    const StringView _name,
    const uint32_t   _slot)
{
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    BindDecl& decl = desc.reads.emplace_back();
    decl.name      = String { _name };
    decl.slot      = MakeSlot_(eShader::Compute, _slot, kNumMaxReadSlots);
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetTextureRW(
    const StringView _name,
    const uint32_t   _slot)
{
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    BindDecl& decl = desc.readWrites.emplace_back();
    decl.name      = String { _name };
    decl.slot      = MakeSlotRW_(eShaderRW::Compute, _slot, kNumMaxReadWriteSlots);
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetBuffer(
    const StringView _name,
    const uint32_t   _slot)
{
    return SetTexture(_name, _slot);
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetBufferRW(
    const StringView _name,
    const uint32_t   _slot)
{
    return SetTextureRW(_name, _slot);
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetConstantBuffer(
    const StringView _name,
    const uint32_t   _slot)
{
    PassDesc& desc = m_pGraph->GetPassDesc_(m_index);
    BindDecl& decl = desc.cbuffers.emplace_back();
    decl.name      = String { _name };
    decl.slot      = MakeSlot_(eShader::Compute, _slot, kNumMaxCBufferSlots);
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetSampler(
    const Flags<eSampler> _flags,
    const uint32_t        _slot)
{
    return SetSampler(_flags, RGBA {}, _slot);
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetSampler(
    const Flags<eSampler> _flags,
    const RGBA            _border,
    const uint32_t        _slot)
{
    PassDesc&    desc = m_pGraph->GetPassDesc_(m_index);
    SamplerDecl& decl = desc.samplers.emplace_back();
    decl.state.flags  = _flags;
    decl.state.border = _border;
    decl.slot         = MakeSlot_(eShader::Compute, _slot, kNumMaxSamplerSlots);
    return *this;
}

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::SetExecutor(
    const Callable<void()>& _executor)
{
    m_pGraph->GetPassDesc_(m_index).executor = _executor;
    return *this;
}

RenderGraph::~RenderGraph()
{
    DestroyOwned_();
}

void RenderGraph::DestroyOwned_()
{
    DestroyOwnedResources_(m_programs);
    DestroyOwnedResources_(m_shaders);
    DestroyOwnedResources_(m_frameBuffers);
    DestroyOwnedResources_(m_textures);
    DestroyOwnedResources_(m_constantBuffers);
    DestroyOwnedResources_(m_storageBuffers);
}

size_t RenderGraph::FindPassIndexOrInvalid_(
    const StringView _name) const
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_descs.size()); ++i)
    {
        if (m_descs[i].name == _name)
        {
            return i;
        }
    }
    return kInvalidIndex;
}

RenderGraph::PassDesc& RenderGraph::GetPassDesc_(
    const size_t _index)
{
    JUG_ASSERT(_index < m_descs.size(), "Invalid pass index.");
    m_bDirty = true;
    return m_descs[_index];
}

size_t RenderGraph::GetOrAddPass_(
    const StringView _name,
    const ePass      _type,
    const bool       _bMustExist)
{
    JUG_ASSERT(!_name.empty(), "Pass name must not be empty.");

    const size_t index = FindPassIndexOrInvalid_(_name);

    if (_bMustExist)
    {
        JUG_ASSERT(index != kInvalidIndex, "Pass '{}' does not exist.", _name);
        JUG_ASSERT(m_descs[index].type == _type, "Pass '{}' was declared with a different pass type.", _name);
        return index;
    }

    JUG_ASSERT(index == kInvalidIndex, "Pass '{}' is already registered.", _name);

    PassDesc& desc = m_descs.emplace_back();
    desc.name      = _name;
    desc.type      = _type;

    m_bDirty = true;
    return m_descs.size() - 1;
}

RenderGraph::RenderPassBuilder RenderGraph::AddRenderPass(
    const StringView _name)
{
    return RenderPassBuilder { this, GetOrAddPass_(_name, ePass::Render, false) };
}

RenderGraph::RenderPassBuilder RenderGraph::ModifyRenderPass(
    const StringView _name)
{
    return RenderPassBuilder { this, GetOrAddPass_(_name, ePass::Render, true) };
}

RenderGraph::ComputePassBuilder RenderGraph::AddComputePass(
    const StringView _name)
{
    return ComputePassBuilder { this, GetOrAddPass_(_name, ePass::Compute, false) };
}

RenderGraph::ComputePassBuilder RenderGraph::ModifyComputePass(
    const StringView _name)
{
    return ComputePassBuilder { this, GetOrAddPass_(_name, ePass::Compute, true) };
}

RenderGraph::UtilityPassBuilder RenderGraph::AddUtilityPass(
    const StringView _name)
{
    return UtilityPassBuilder { this, GetOrAddPass_(_name, ePass::Utility, false) };
}

RenderGraph::UtilityPassBuilder RenderGraph::ModifyUtilityPass(
    const StringView _name)
{
    return UtilityPassBuilder { this, GetOrAddPass_(_name, ePass::Utility, true) };
}

void RenderGraph::RemovePass(
    const StringView _name)
{
    const size_t index = FindPassIndexOrInvalid_(_name);
    if (index == kInvalidIndex)
    {
        return;
    }

    m_descs.erase(m_descs.begin() + static_cast<ptrdiff_t>(index));
    m_bDirty = true;
}

bool RenderGraph::HasPass(
    const StringView _name) const
{
    return FindPassIndexOrInvalid_(_name) != kInvalidIndex;
}

void RenderGraph::Insert(
    const StringView    _name,
    const TextureHandle _texh,
    const bool          _bOwnership)
{
    InsertResource_(m_textures, _name, _texh, _bOwnership);
    m_bDirty = true;
}

void RenderGraph::Insert(
    const StringView        _name,
    const FrameBufferHandle _fbh,
    const bool              _bOwnership)
{
    InsertResource_(m_frameBuffers, _name, _fbh, _bOwnership);
    m_bDirty = true;
}

void RenderGraph::Insert(
    const StringView           _name,
    const ConstantBufferHandle _cbh,
    const bool                 _bOwnership)
{
    InsertResource_(m_constantBuffers, _name, _cbh, _bOwnership);
    m_bDirty = true;
}

void RenderGraph::Insert(
    const StringView          _name,
    const StorageBufferHandle _sbh,
    const bool                _bOwnership)
{
    InsertResource_(m_storageBuffers, _name, _sbh, _bOwnership);
    m_bDirty = true;
}

void RenderGraph::Insert(
    const StringView   _name,
    const ShaderHandle _sh,
    const bool         _bOwnership)
{
    InsertResource_(m_shaders, _name, _sh, _bOwnership);
    m_bDirty = true;
}

void RenderGraph::Insert(
    const StringView    _name,
    const ProgramHandle _ph,
    const bool          _bOwnership)
{
    InsertResource_(m_programs, _name, _ph, _bOwnership);
    m_bDirty = true;
}

void RenderGraph::InsertOrReplace(
    const StringView    _name,
    const TextureHandle _texh)
{
    InsertOrReplaceResource_(m_textures, _name, _texh);
    m_bDirty = true;
}

void RenderGraph::InsertOrReplace(
    const StringView        _name,
    const FrameBufferHandle _fbh)
{
    InsertOrReplaceResource_(m_frameBuffers, _name, _fbh);
    m_bDirty = true;
}

void RenderGraph::InsertOrReplace(
    const StringView           _name,
    const ConstantBufferHandle _cbh)
{
    InsertOrReplaceResource_(m_constantBuffers, _name, _cbh);
    m_bDirty = true;
}

void RenderGraph::InsertOrReplace(
    const StringView          _name,
    const StorageBufferHandle _sbh)
{
    InsertOrReplaceResource_(m_storageBuffers, _name, _sbh);
    m_bDirty = true;
}

void RenderGraph::InsertOrReplace(
    const StringView   _name,
    const ShaderHandle _sh)
{
    InsertOrReplaceResource_(m_shaders, _name, _sh);
    m_bDirty = true;
}

void RenderGraph::InsertOrReplace(
    const StringView    _name,
    const ProgramHandle _ph)
{
    InsertOrReplaceResource_(m_programs, _name, _ph);
    m_bDirty = true;
}

void RenderGraph::RemoveTexture(
    const StringView _name)
{
    RemoveResource_(m_textures, _name);
    m_bDirty = true;
}

void RenderGraph::RemoveFrameBuffer(
    const StringView _name)
{
    RemoveResource_(m_frameBuffers, _name);
    m_bDirty = true;
}

void RenderGraph::RemoveConstantBuffer(
    const StringView _name)
{
    RemoveResource_(m_constantBuffers, _name);
    m_bDirty = true;
}

void RenderGraph::RemoveStorageBuffer(
    const StringView _name)
{
    RemoveResource_(m_storageBuffers, _name);
    m_bDirty = true;
}

void RenderGraph::RemoveShader(
    const StringView _name)
{
    RemoveResource_(m_shaders, _name);
    m_bDirty = true;
}

void RenderGraph::RemoveProgram(
    const StringView _name)
{
    RemoveResource_(m_programs, _name);
    m_bDirty = true;
}

TextureHandle RenderGraph::GetTexture(
    const StringView _name) const
{
    return GetResource_(m_textures, _name);
}

FrameBufferHandle RenderGraph::GetFrameBuffer(
    const StringView _name) const
{
    return GetResource_(m_frameBuffers, _name);
}

ConstantBufferHandle RenderGraph::GetConstantBuffer(
    const StringView _name) const
{
    return GetResource_(m_constantBuffers, _name);
}

StorageBufferHandle RenderGraph::GetStorageBuffer(
    const StringView _name) const
{
    return GetResource_(m_storageBuffers, _name);
}

ShaderHandle RenderGraph::GetShader(
    const StringView _name) const
{
    return GetResource_(m_shaders, _name);
}

ProgramHandle RenderGraph::GetProgram(
    const StringView _name) const
{
    return GetResource_(m_programs, _name);
}

bool RenderGraph::HasTexture(
    const StringView _name) const
{
    return m_textures.contains(_name);
}

bool RenderGraph::HasFrameBuffer(
    const StringView _name) const
{
    return m_frameBuffers.contains(_name);
}

bool RenderGraph::HasConstantBuffer(
    const StringView _name) const
{
    return m_constantBuffers.contains(_name);
}

bool RenderGraph::HasStorageBuffer(
    const StringView _name) const
{
    return m_storageBuffers.contains(_name);
}

bool RenderGraph::HasShader(
    const StringView _name) const
{
    return m_shaders.contains(_name);
}

bool RenderGraph::HasProgram(
    const StringView _name) const
{
    return m_programs.contains(_name);
}

void RenderGraph::Clear()
{
    DestroyOwned_();

    m_descs.clear();
    m_resolved.clear();
    m_compiledPasses.clear();

    m_bDirty = true;
}

void RenderGraph::Compile_()
{
    if (!m_bDirty)
    {
        return;
    }

    Graphics& gfx = Graphics::GetSingleton();

    m_resolved.clear();
    m_resolved.resize(m_descs.size());
    m_compiledPasses.clear();

    Vector<bool>   alives(m_descs.size(), false);
    Vector<size_t> pending = {};   // 임시 패스
    pending.reserve(m_descs.size());

    size_t finalIndex = kInvalidIndex;
    size_t lastIndex  = kInvalidIndex;
    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        const PassDesc& desc = m_descs[i];
        if (!desc.bEnable)   // 사용하지 않는 패스
        {
            continue;
        }

        // 유틸리티 패스는 프로그램도 프레임버퍼도 바인딩도 없다. executor 만 부른다.
        if (desc.type == ePass::Utility)
        {
            alives[i] = true;
            pending.push_back(i);
            continue;
        }

        if (desc.bSideEffect)
        {
            alives[i] = true;
            pending.push_back(i);
        }

        // final pass 는 마킹이 있으면 그 패스, 없으면 마지막 활성 렌더/컴퓨트 패스.
        lastIndex = i;
        if (desc.bFinalOutput)
        {
            JUG_ASSERT(finalIndex == kInvalidIndex, "More than one pass is marked as the final output. '{}' and '{}'", m_descs[finalIndex].name, desc.name);
            finalIndex = i;
        }

        // 이름 기반 리소스를 실제 리소스 핸들로 리졸브
        ResolvedPass& resolved = m_resolved[i];

        // 프로그램
        JUG_ASSERT(!desc.programName.empty(), "Pass '{}' has no program.", desc.name);
        resolved.ph = GetProgram(desc.programName);

        // 프레임 버퍼
        if (desc.type == ePass::Render)
        {
            JUG_ASSERT(!desc.frameBufferName.empty(), "Render pass '{}' has no frame buffer.", desc.name);
        }

        if (!desc.frameBufferName.empty())
        {
            resolved.fbh = GetFrameBuffer(desc.frameBufferName);
        }

        // read 리소스 수집
        resolved.readResources.reserve(desc.reads.size());
        for (const BindDecl& decl: desc.reads)
        {
            const auto texIt = m_textures.find(decl.name);
            if (texIt != m_textures.end())
            {
                JUG_ASSERT(!m_storageBuffers.contains(decl.name), "Pass '{}': resource name '{}' is registered as both a texture and a storage buffer.", desc.name, decl.name);
                resolved.readResources.emplace_back(texIt->second.handle);
            }
            else
            {
                const auto sbIt = m_storageBuffers.find(decl.name);
                JUG_ASSERT(sbIt != m_storageBuffers.end(), "Pass '{}': resource '{}' is not registered.", desc.name, decl.name);
                resolved.readResources.emplace_back(sbIt->second.handle);
            }
        }

        // rw 리소스 수집
        resolved.rwResources.reserve(desc.readWrites.size());
        for (const BindDecl& decl: desc.readWrites)
        {
            const auto texIt = m_textures.find(decl.name);
            if (texIt != m_textures.end())
            {
                JUG_ASSERT(!m_storageBuffers.contains(decl.name), "Pass '{}': resource name '{}' is registered as both a texture and a storage buffer.", desc.name, decl.name);
                resolved.rwResources.emplace_back(texIt->second.handle);
            }
            else
            {
                const auto sbIt = m_storageBuffers.find(decl.name);
                JUG_ASSERT(sbIt != m_storageBuffers.end(), "Pass '{}': resource '{}' is not registered.", desc.name, decl.name);
                resolved.rwResources.emplace_back(sbIt->second.handle);
            }
        }

        // cbuffer 수집
        resolved.cbufferHandles.reserve(desc.cbuffers.size());
        for (const BindDecl& decl: desc.cbuffers)
        {
            resolved.cbufferHandles.push_back(GetConstantBuffer(decl.name));
        }

        // write 리소스는 프레임 버퍼에서 유도
        if (resolved.fbh)
        {
            const FrameBufferDesc& fb = gfx.GetDesc(resolved.fbh);
            JUG_ASSERT(fb.GetNumAttachments() > 0, "Render pass '{}' has no attachments in its frame buffer '{}'.", desc.name, desc.frameBufferName);

            JUG_ASSERT(!desc.bViewportFromFrameBuffer || fb.atts[0].texh, "Render pass '{}': viewport comes from the first attachment of frame buffer '{}', but it is empty.", desc.name, desc.frameBufferName);

            for (uint32_t att = 0; att < fb.GetNumAttachments(); ++att)
            {
                if (fb.atts[att].texh)
                {
                    resolved.writeResources.emplace_back(fb.atts[att].texh);
                }
            }
        }

        // 패스 안의 자기 해저드. 파이프라인 적용이 SRV -> UAV -> OM 순이라 한 패스가 같은 리소스를 두 용도로 물리면 런타임이 앞의 것을 조용히 null 로 만든다.
        for (size_t d = 0; d < resolved.readResources.size(); ++d)
        {
            JUG_ASSERT(std::ranges::find(resolved.writeResources, resolved.readResources[d]) == resolved.writeResources.end(), "Pass '{}': resource '{}' is read while it is an attachment of the pass's own frame buffer '{}'.", desc.name, desc.reads[d].name, desc.frameBufferName);
            JUG_ASSERT(std::ranges::find(resolved.rwResources, resolved.readResources[d]) == resolved.rwResources.end(), "Pass '{}': resource '{}' is declared as both read and read-write.", desc.name, desc.reads[d].name);
        }

        for (size_t d = 0; d < resolved.rwResources.size(); ++d)
        {
            JUG_ASSERT(std::ranges::find(resolved.writeResources, resolved.rwResources[d]) == resolved.writeResources.end(), "Pass '{}': resource '{}' is read-write while it is an attachment of the pass's own frame buffer '{}'.", desc.name, desc.readWrites[d].name, desc.frameBufferName);
        }
    }

    // 유틸리티 패스만 있는 그래프는 final pass 가 없어도 된다.
    if (finalIndex == kInvalidIndex && lastIndex != kInvalidIndex)
    {
        JUG_CORE_LOG_WARN("RenderGraph has no final output pass. Mark the last pass '{}' as the final output.", m_descs[lastIndex].name);
        finalIndex = lastIndex;
    }

    if (finalIndex != kInvalidIndex && !alives[finalIndex])
    {
        alives[finalIndex] = true;
        pending.push_back(finalIndex);
    }

    // 컬링. 뿌리에서 역방향 탐색해 "내가 사용하는 read, read-write 리소스를 write한 패스" 를 살린다.
    // 실행 순서가 등록 순이라 뒤 패스가 앞 패스에게 다음 프레임으로 먹이는 구성도 가능하므로 위치를 따지지 않고 모든 writer 에 간선을 건다.
    while (!pending.empty())
    {
        const ResolvedPass& reader = m_resolved[pending.back()];
        pending.pop_back();

        for (const Vector<ResourceRef>* pResources: { &reader.readResources, &reader.rwResources })
        {
            for (const ResourceRef resource: *pResources)
            {
                for (size_t w = 0; w < m_resolved.size(); ++w)
                {
                    if (alives[w])
                    {
                        continue;
                    }

                    const ResolvedPass& writer = m_resolved[w];
                    if (std::ranges::find(writer.writeResources, resource) != writer.writeResources.end()
                        || std::ranges::find(writer.rwResources, resource) != writer.rwResources.end())
                    {
                        alives[w] = true;
                        pending.push_back(w);
                    }
                }
            }
        }
    }

    // 살아남은 패스만 등록 순서대로 싣는다.
    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        const PassDesc& desc = m_descs[i];
        if (!desc.bEnable)
        {
            JUG_CORE_LOG_TRACE("RenderGraph: pass '{}' culled. reason = disabled", desc.name);
            continue;
        }

        if (!alives[i])
        {
            JUG_CORE_LOG_TRACE("RenderGraph: pass '{}' culled. reason = does not affect the final output", desc.name);
            continue;
        }

        m_compiledPasses.emplace_back().descIndex = i;
        JUG_CORE_LOG_TRACE("RenderGraph: pass '{}' kept. order = {}", desc.name, m_compiledPasses.size() - 1);
    }

    JUG_CORE_LOG_TRACE("RenderGraph compiled. passes = {} / {}", m_compiledPasses.size(), m_descs.size());

    // 해저드 해결. 파이프라인의 read / read-write / 프레임버퍼 바인딩 상태만 흉내 낸다.
    // 중복 바인드 제거는 여기서 하지 않는다. executor 가 같은 슬롯을 덮어쓸 수 있어 그래프는 슬롯의 주인이 아니고, Graphics::Set* 가 실제 상태와 비교해 이미 거른다.
    ARRAY<ResourceRef, kNumReadSlots>      liveRead   = {};
    ARRAY<ResourceRef, kNumReadWriteSlots> liveRW     = {};
    Span<const ResourceRef>                liveWrites = {};

    // 두 바퀴 도는 이유 -> 마지막 패스의 종료 상태가 다음 프레임 첫 패스의 시작 상태라 거기서도 충돌할 수 있다.
    const size_t numPasses = m_compiledPasses.size();
    for (size_t step = 0; step < numPasses * 2; ++step)
    {
        CompiledPass& pass = m_compiledPasses[step % numPasses];

        const PassDesc&     desc     = m_descs[pass.descIndex];
        const ResolvedPass& resolved = m_resolved[pass.descIndex];

        // 유틸리티 패스는 아무것도 바인딩하지 않으므로 상태를 건드리지 않고 지나간다.
        if (desc.type == ePass::Utility)
        {
            continue;
        }

        uint64_t readUnbindMask     = 0;
        uint64_t rwUnbindMask       = 0;
        bool     bUnbindFrameBuffer = false;

        // read 하려는 리소스가 UAV 로 살아 있으면 그 UAV 를, RTV 로 살아 있으면 프레임버퍼를 끊는다.
        for (const ResourceRef resource: resolved.readResources)
        {
            for (uint32_t s = 0; s < kNumReadWriteSlots; ++s)
            {
                if (liveRW[s] == resource)
                {
                    rwUnbindMask |= 1ull << s;
                }
            }
            bUnbindFrameBuffer |= std::ranges::find(liveWrites, resource) != liveWrites.end();
        }

        // read-write 하려는 리소스가 SRV 로 살아 있으면 그 SRV 를, RTV 로 살아 있으면 프레임버퍼를 끊는다.
        for (const ResourceRef resource: resolved.rwResources)
        {
            for (uint32_t s = 0; s < kNumReadSlots; ++s)
            {
                if (liveRead[s] == resource)
                {
                    readUnbindMask |= 1ull << s;
                }
            }
            bUnbindFrameBuffer |= std::ranges::find(liveWrites, resource) != liveWrites.end();
        }

        // 어태치먼트로 write 하려는 리소스가 SRV/UAV 로 살아 있으면 그 슬롯을 끊는다.
        for (const ResourceRef resource: resolved.writeResources)
        {
            for (uint32_t s = 0; s < kNumReadSlots; ++s)
            {
                if (liveRead[s] == resource)
                {
                    readUnbindMask |= 1ull << s;
                }
            }
            for (uint32_t s = 0; s < kNumReadWriteSlots; ++s)
            {
                if (liveRW[s] == resource)
                {
                    rwUnbindMask |= 1ull << s;
                }
            }
        }

        // 2 회차가 1 회차 값을 덮어써서 수렴한 상태 기준 값만 남는다.
        pass.readUnbindMask     = readUnbindMask;
        pass.rwUnbindMask       = rwUnbindMask;
        pass.bUnbindFrameBuffer = bUnbindFrameBuffer;

        // 방금 정한 언바인드를 시뮬레이션 상태에도 그대로 반영한다.
        for (uint64_t mask = readUnbindMask; mask != 0; mask &= mask - 1)
        {
            liveRead[std::countr_zero(mask)] = ResourceRef {};
        }
        for (uint64_t mask = rwUnbindMask; mask != 0; mask &= mask - 1)
        {
            liveRW[std::countr_zero(mask)] = ResourceRef {};
        }
        if (bUnbindFrameBuffer)
        {
            liveWrites = {};
        }

        // 가상 바인드
        for (size_t d = 0; d < resolved.readResources.size(); ++d)
        {
            liveRead[desc.reads[d].slot] = resolved.readResources[d];
        }

        for (size_t d = 0; d < resolved.rwResources.size(); ++d)
        {
            liveRW[desc.readWrites[d].slot] = resolved.rwResources[d];
        }

        // 프레임버퍼를 가진 패스만 write 상태를 갈아끼운다.
        if (resolved.fbh)
        {
            liveWrites = resolved.writeResources;
        }
    }

    m_bDirty = false;
}

void RenderGraph::Execute()
{
    Compile_();

    Graphics& gfx = Graphics::GetSingleton();

    for (const CompiledPass& pass: m_compiledPasses)
    {
        const PassDesc&     desc     = m_descs[pass.descIndex];
        const ResolvedPass& resolved = m_resolved[pass.descIndex];

        gfx.PushDebugGroup(desc.name);

        if (desc.type == ePass::Utility)
        {
            if (desc.executor)
            {
                desc.executor();
            }
        }
        else
        {
            // 바인딩하려는 리소스가 직전 패스에 다른 용도로 물려 있으면 먼저 끊는다.
            if (pass.readUnbindMask != 0 || pass.rwUnbindMask != 0 || pass.bUnbindFrameBuffer)
            {
                uint64_t mask = pass.readUnbindMask;
                while (mask != 0)
                {
                    const uint32_t s = static_cast<uint32_t>(std::countr_zero(mask));
                    mask &= mask - 1;
                    gfx.SetTexture(kNullHandle, static_cast<eShader>(s >> kSlotShift), s & kSlotMask);
                }

                mask = pass.rwUnbindMask;
                while (mask != 0)
                {
                    const uint32_t s = static_cast<uint32_t>(std::countr_zero(mask));
                    mask &= mask - 1;
                    gfx.SetTextureRW(kNullHandle, static_cast<eShaderRW>(s >> kSlotShift), s & kSlotMask);
                }

                if (pass.bUnbindFrameBuffer)
                {
                    gfx.SetFrameBuffer(kNullHandle);
                }

                gfx.Touch();
            }

            if (desc.type == ePass::Compute)   // 컴퓨트는 프로그램만 바인딩
            {
                gfx.SetComputeProgram(resolved.ph);
            }
            else
            {
                gfx.SetProgram(resolved.ph);
                gfx.SetRenderState(desc.renderState);
                gfx.SetStencil(desc.frontStencil, desc.backStencil, desc.stencilRef);

                for (const BlendDecl& blend: desc.blends)
                {
                    gfx.SetBlend(blend.flags, blend.slot);
                }

                if (desc.bHasBlendFactor)
                {
                    gfx.SetBlendFactor(desc.blendFactor);
                }

                gfx.SetFrameBuffer(resolved.fbh);

                // 프레임버퍼 크기는 컴파일 뒤에도 바뀐다 (스왑체인 리사이즈는 핸들이 그대로라 그래프가 모른다). 그래서 매 프레임 읽는다.
                if (desc.bViewportFromFrameBuffer)
                {
                    const TextureDesc& tex = gfx.GetDesc(gfx.GetDesc(resolved.fbh).atts[0].texh);
                    gfx.SetViewport(0.f, 0.f, static_cast<float>(tex.width), static_cast<float>(tex.height));
                }
                else
                {
                    gfx.SetViewport(desc.viewportX, desc.viewportY, desc.viewportW, desc.viewportH);
                }

                if (desc.renderState & eRenderState::Scissor)
                {
                    gfx.SetScissor(desc.scissorX, desc.scissorY, desc.scissorW, desc.scissorH);
                }

                for (const ClearDecl& clear: desc.renderTargetClears)
                {
                    gfx.ClearRenderTarget(resolved.fbh, clear.color, clear.slot);
                }

                if (desc.bClearDepth || desc.bClearStencil)
                {
                    gfx.ClearDepthStencil(resolved.fbh, desc.bClearDepth, desc.bClearStencil, desc.depthClearValue, desc.stencilClearValue);
                }
            }

            // 선언한 바인드는 매 패스 전부 낸다. 같은 값이면 Graphics::Set* 가 비교 한 번으로 거른다.
            for (size_t d = 0; d < resolved.cbufferHandles.size(); ++d)
            {
                const uint32_t slot = desc.cbuffers[d].slot;
                gfx.SetConstantBuffer(resolved.cbufferHandles[d], static_cast<eShader>(slot >> kSlotShift), slot & kSlotMask);
            }

            for (const SamplerDecl& decl: desc.samplers)
            {
                gfx.SetSampler(decl.state.flags, decl.state.border, static_cast<eShader>(decl.slot >> kSlotShift), decl.slot & kSlotMask);
            }

            for (size_t d = 0; d < resolved.readResources.size(); ++d)
            {
                const ResourceRef resource = resolved.readResources[d];
                const eShader     shader   = static_cast<eShader>(desc.reads[d].slot >> kSlotShift);
                const uint32_t    slot     = desc.reads[d].slot & kSlotMask;

                if (resource.GetType() == eResource::Texture)
                {
                    gfx.SetTexture(resource.GetTextureHandle(), shader, slot);
                }
                else
                {
                    gfx.SetBuffer(resource.GetStorageBufferHandle(), shader, slot);
                }
            }

            for (size_t d = 0; d < resolved.rwResources.size(); ++d)
            {
                const ResourceRef resource = resolved.rwResources[d];
                const eShaderRW   shader   = static_cast<eShaderRW>(desc.readWrites[d].slot >> kSlotShift);
                const uint32_t    slot     = desc.readWrites[d].slot & kSlotMask;

                if (resource.GetType() == eResource::Texture)
                {
                    gfx.SetTextureRW(resource.GetTextureHandle(), shader, slot);
                }
                else
                {
                    gfx.SetBufferRW(resource.GetStorageBufferHandle(), shader, slot);
                }
            }

            if (desc.executor)
            {
                desc.executor();
            }
        }

        gfx.PopDebugGroup();
    }
}

}   // namespace jug
