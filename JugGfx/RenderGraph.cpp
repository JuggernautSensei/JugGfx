#include "pch.h"
#include "RenderGraph.h"

#include "Graphics.h"

#include <bit>
#include <ranges>

namespace jug
{
namespace
{
    // (스테이지, 슬롯) 을 평탄 인덱스 하나로 접는다. 상위 비트 = 스테이지, 하위 kSlotShift 비트 = 슬롯.
    // 이 평탄 인덱스가 곧 시뮬레이션 배열의 첨자이자 언바인드 마스크의 비트 위치다.
    constexpr uint32_t kSlotShift = 4;
    constexpr uint32_t kSlotMask  = (1u << kSlotShift) - 1u;

    constexpr uint32_t kNumReadSlots      = static_cast<uint32_t>(CountOf<eShader>()) * (1u << kSlotShift);
    constexpr uint32_t kNumReadWriteSlots = static_cast<uint32_t>(CountOf<eShaderRW>()) * (1u << kSlotShift);
    constexpr uint32_t kNumCBufferSlots   = static_cast<uint32_t>(CountOf<eShader>()) * (1u << kSlotShift);
    constexpr uint32_t kNumSamplerSlots   = static_cast<uint32_t>(CountOf<eShader>()) * (1u << kSlotShift);

    // 스테이지당 슬롯 수가 stride 를 넘으면 슬롯이 스테이지 비트를 침범해 서로 다른 바인딩이 같은 평탄 인덱스가 된다.
    static_assert(kNumMaxReadSlots <= (1u << kSlotShift), "Too many read slots.");
    static_assert(kNumMaxReadWriteSlots <= (1u << kSlotShift), "Too many read-write slots.");
    static_assert(kNumMaxCBufferSlots <= (1u << kSlotShift), "Too many constant buffer slots.");
    static_assert(kNumMaxSamplerSlots <= (1u << kSlotShift), "Too many sampler slots.");

    // 언바인드 마스크가 uint64_t 라 평탄 슬롯이 64 개를 넘으면 1ull << s 가 깨진다.
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

    // 텍스처와 스토리지 버퍼는 핸들 값이 겹칠 수 있다. 타입을 상위 32 비트에 얹어 충돌을 막는다.
    [[nodiscard]] uint64_t MakeResourceKey_(
        const ResourceRef _ref)
    {
        return (static_cast<uint64_t>(_ref.GetType()) << 32) | _ref.GetTextureHandle().GetValue();
    }

    template<typename Map, typename H = typename Map::mapped_type::Type>
    void InsertResource_(
        Map&             _map,
        const StringView _name,
        const H          _handle,
        const bool       _bOwnership)
    {
        JUG_ASSERT(!_name.empty(), "Resource name must not be empty.");
        JUG_ASSERT(_handle, "Cannot register a null handle. name = '{}'", _name);
        JUG_ASSERT(!_map.contains(_name), "Resource '{}' is already registered.", _name);

        _map.emplace(String { _name }, typename Map::mapped_type { _handle, _bOwnership });
    }

    // 없으면 삽입, 있으면 핸들만 교체한다. 소유권은 기존 항목의 것을 유지한다.
    template<typename Map, typename H = typename Map::mapped_type::Type>
    void InsertOrReplaceResource_(
        Map&             _map,
        const StringView _name,
        const H          _handle)
    {
        JUG_ASSERT(!_name.empty(), "Resource name must not be empty.");
        JUG_ASSERT(_handle, "Cannot replace with a null handle. name = '{}'", _name);

        const auto it = _map.find(_name);
        if (it == _map.end())
        {
            _map.emplace(String { _name }, typename Map::mapped_type { _handle, false });
            return;
        }

        // 소유 중인 옛 핸들은 버려지므로 여기서 파괴한다. 같은 핸들이면 파괴하면 안 된다.
        if (it->second.bOwnership && it->second.handle != _handle)
        {
            Graphics::GetInstance()->Destroy(it->second.handle);
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
            Graphics::GetInstance()->Destroy(it->second.handle);
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
        Graphics* pGfx = Graphics::GetInstance();
        for (auto& res: _map | std::views::values)
        {
            if (res.bOwnership && res.handle)
            {
                pGfx->Destroy(res.handle);
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

RenderGraph::RenderPassBuilder& RenderGraph::RenderPassBuilder::AsFinalPass(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bFinalPass = _bEnable;
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

RenderGraph::ComputePassBuilder& RenderGraph::ComputePassBuilder::AsFinalPass(
    const bool _bEnable)
{
    m_pGraph->GetPassDesc_(m_index).bFinalPass = _bEnable;
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
    return static_cast<uint32_t>(m_descs.size()) - 1;
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

// 하나의 이름이 텍스처일 수도 스토리지 버퍼일 수도 있다. 양쪽 레지스트리를 뒤져 타입을 붙인 참조로 만든다.
ResourceRef RenderGraph::ResolveResource_(
    const StringView _passName,
    const StringView _name) const
{
    const auto texIt = m_textures.find(_name);
    const auto sbIt  = m_storageBuffers.find(_name);
    JUG_ASSERT(texIt == m_textures.end() || sbIt == m_storageBuffers.end(), "Pass '{}': resource name '{}' is registered as both a texture and a storage buffer.", _passName, _name);

    if (texIt != m_textures.end())
    {
        return ResourceRef { texIt->second.handle };
    }

    JUG_ASSERT(sbIt != m_storageBuffers.end(), "Pass '{}': resource '{}' is not registered.", _passName, _name);
    return ResourceRef { sbIt->second.handle };
}

// 컴파일 1 단계. 저작 구조(PassDesc)의 문자열을 전부 핸들로 푼다.
// 이 단계가 끝나면 이후 검증·컬링·해저드는 문자열 근처도 가지 않는다.
void RenderGraph::ResolveNames_()
{
    Graphics* pGfx = Graphics::GetInstance();

    m_resolved.resize(m_descs.size());
    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        const PassDesc& desc     = m_descs[i];
        ResolvedPass&   resolved = m_resolved[i];

        // 유틸리티 패스는 프로그램도 프레임버퍼도 없다. executor 만 부른다.
        if (desc.type != ePass::Utility)
        {
            JUG_ASSERT(!desc.programName.empty(), "Pass '{}' has no program.", desc.name);
            resolved.ph = GetProgram(desc.programName);
        }

        if (desc.type == ePass::Render)
        {
            JUG_ASSERT(!desc.frameBufferName.empty(), "Render pass '{}' has no frame buffer.", desc.name);
        }

        if (!desc.frameBufferName.empty())
        {
            resolved.fbh = GetFrameBuffer(desc.frameBufferName);
        }

        // 선언 순서를 그대로 유지한다. 뒤에서 desc.reads[d].slot 과 readRefs[d] 를 인덱스로 짝지어 쓴다.
        resolved.readRefs.reserve(desc.reads.size());
        for (const BindDecl& decl: desc.reads)
        {
            resolved.readRefs.push_back(ResolveResource_(desc.name, decl.name));
        }

        resolved.readWriteRefs.reserve(desc.readWrites.size());
        for (const BindDecl& decl: desc.readWrites)
        {
            resolved.readWriteRefs.push_back(ResolveResource_(desc.name, decl.name));
        }

        resolved.cbufferHandles.reserve(desc.cbuffers.size());
        for (const BindDecl& decl: desc.cbuffers)
        {
            resolved.cbufferHandles.push_back(GetConstantBuffer(decl.name));
        }

        // write 집합은 선언하는 게 아니라 프레임버퍼 어태치먼트에서 끌어낸다.
        // 이 집합의 소비자가 둘(해저드 언바인드 + 컬링 의존성 간선)이라 여기서 뭘 빼면 컬링이 조용히 망가진다.
        // MSAA 는 RTV 가 MSAA 리소스, SRV 가 리졸브 리소스라 실제 해저드가 없지만 그래도 빼지 않는다.
        // 남겨서 생기는 비용은 불필요한 언바인드 한 번, 빼서 생기는 비용은 패스 소멸이다.
        resolved.writeRefs.clear();
        if (resolved.fbh)
        {
            const FrameBufferDesc& fb = pGfx->GetDesc(resolved.fbh);
            for (uint32_t att = 0; att < fb.GetNumAttachments(); ++att)
            {
                if (fb.atts[att].texh)
                {
                    resolved.writeRefs.emplace_back(fb.atts[att].texh);
                }
            }

            // 뷰포트를 명시하지 않았으면 첫 어태치먼트 크기를 쓴다.
            if (desc.bViewportFromFrameBuffer && fb.GetNumAttachments() > 0 && fb.atts[0].texh)
            {
                const TextureDesc& tex = pGfx->GetDesc(fb.atts[0].texh);
                resolved.viewportX     = 0.f;
                resolved.viewportY     = 0.f;
                resolved.viewportW     = static_cast<float>(tex.width);
                resolved.viewportH     = static_cast<float>(tex.height);
            }
        }

        if (!desc.bViewportFromFrameBuffer)
        {
            resolved.viewportX = desc.viewportX;
            resolved.viewportY = desc.viewportY;
            resolved.viewportW = desc.viewportW;
            resolved.viewportH = desc.viewportH;
        }
    }
}

// Execute 진입점. dirty 일 때만 전체 재컴파일한다.
// 부분 갱신(핸들만 교체 등)은 지원하지 않는다. 해저드 판정이 이름이 아니라 핸들 값으로 돌아서,
// 핸들 하나만 바뀌어도 서로 다른 이름이 같은 리소스를 가리키게 되면 해저드 집합 자체가 달라지기 때문.
void RenderGraph::CompileIfNeed_()
{
    if (!m_bDirty)
    {
        return;
    }

    // 컴파일 정보 초기화
    m_resolved.clear();
    m_compiledPasses.clear();

    // 활성화된 패스가 하나도 없으면 컴파일을 건너뛴다.
    // 여기서 막지 않고 이름 해석까지 가면, 꺼진 패스가 참조하던 리소스가 이미 제거됐을 때 엉뚱한 assert 로 죽는다.
    {
        bool bAnyEnabled = false;
        for (const PassDesc& desc: m_descs)
        {
            if (desc.bEnable)
            {
                bAnyEnabled = true;
                break;
            }
        }

        if (!bAnyEnabled)
        {
            m_bDirty = false;
            JUG_CORE_LOG_TRACE("RenderGraph compiled. no enabled pass. passes = 0 / {}", m_descs.size());
            return;
        }
    }

    ResolveNames_();
    BuildCompiledPasses_();
    m_bDirty = false;
}

// 컴파일 2 단계. 검증 -> final pass 결정 -> 컬링 -> 해저드 해결 + 중복 바인드 제거.
// 결과물인 CompiledPass 에는 문자열도 해시맵도 없다. 핸들, 평탄 슬롯, 비트마스크뿐.
void RenderGraph::BuildCompiledPasses_()
{
#ifdef _DEBUG
    // 디버그 검증.
    // 1. final pass는 최대 1개만 존재해야 함.
    // 2. read와 read-write, write가 겹치면 안됨.
    // 3. ps 에 바인딩하는 read-write 리소스는 슬롯이 numRts 이상이어야함. (render target과 slot을 공유하기 때문에)
    {
        uint32_t numFinalMarks = 0;
        for (size_t i = 0; i < m_descs.size(); ++i)
        {
            const PassDesc&     desc     = m_descs[i];
            const ResolvedPass& resolved = m_resolved[i];

            if (desc.bFinalPass && desc.bEnable)
            {
                ++numFinalMarks;
            }

            for (const ResourceRef& read: resolved.readRefs)
            {
                for (const ResourceRef& rw: resolved.readWriteRefs)
                {
                    JUG_ASSERT(read != rw, "Pass '{}' declares the same resource as both read and read-write.", desc.name);
                }
                for (const ResourceRef& write: resolved.writeRefs)
                {
                    JUG_ASSERT(read != write, "Pass '{}' reads a resource that is an attachment of its own frame buffer.", desc.name);
                }
            }

            if (resolved.fbh)
            {
                const FrameBufferDesc& fb = Graphics::GetInstance()->GetDesc(resolved.fbh);
                for (const BindDecl& decl: desc.readWrites)
                {
                    const uint32_t stage = decl.slot >> kSlotShift;
                    const uint32_t local = decl.slot & kSlotMask;
                    JUG_ASSERT(static_cast<eShaderRW>(stage) != eShaderRW::Pixel || local >= fb.numRts, "Pass '{}': pixel UAV slot {} is below the render target count {}.", desc.name, local, fb.numRts);
                }
            }
        }
        JUG_ASSERT(numFinalMarks <= 1, "More than one pass is marked as the final pass.");
    }
#endif

    // final pass 결정. 마킹이 있으면 그 패스, 없으면 마지막 활성 패스.
    size_t finalIndex = kInvalidIndex;
    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        if (!m_descs[i].bEnable)
        {
            continue;
        }
        if (m_descs[i].bFinalPass)
        {
            finalIndex = i;
            break;
        }
        finalIndex = i;
    }
    JUG_ASSERT(finalIndex != kInvalidIndex, "No enabled pass exists.");

    // 리소스별 writer 패스 집합. 프레임버퍼 어태치먼트와 read-write 둘 다 write 로 친다.
    HashMap<uint64_t, Vector<size_t>> writers = {};
    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        if (!m_descs[i].bEnable)
        {
            continue;
        }

        const ResolvedPass& resolved = m_resolved[i];
        for (const ResourceRef& ref: resolved.writeRefs)
        {
            writers[MakeResourceKey_(ref)].push_back(i);
        }
        for (const ResourceRef& ref: resolved.readWriteRefs)
        {
            writers[MakeResourceKey_(ref)].push_back(i);
        }
    }

    // 컬링. 최종 출력에 영향을 미치지 못하는 패스를 잘라낸다.
    // 뿌리 = { 사이드 이펙트 패스 } U { 모든 유틸리티 패스 }. 그래프 밖으로 결과가 새는 패스들이라
    // 그래프 안에서 읽는 사람이 없어도 살려둔다.
    // final pass 를 뿌리에 자동으로 넣지 않는다. 스왑체인에 그리는 것도 결국 그래프 밖으로 새는 출력이므로
    // 저작자가 AsSideEffect 로 명시하는 게 맞다. 빠뜨리면 아래 assert 가 Debug 에서 잡는다.
    Vector<bool>   bAlive = {};
    Vector<size_t> stack  = {};
    bAlive.resize(m_descs.size(), false);

    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        if (!m_descs[i].bEnable)
        {
            continue;
        }
        if (m_descs[i].bSideEffect || m_descs[i].type == ePass::Utility)
        {
            if (!bAlive[i])
            {
                bAlive[i] = true;
                stack.push_back(i);
            }
        }
    }

    // 뿌리에서 역방향 BFS. "내가 읽는 리소스를 쓴 패스" 를 전부 살린다.
    // 실행 순서가 등록 순이라 뒤 패스가 앞 패스에게 다음 프레임으로 먹이는 구성도 가능하므로
    // 위치를 따지지 않고 모든 writer 에 간선을 건다. 과대근사라 더 많이 살아남는 쪽으로 틀린다.
    while (!stack.empty())
    {
        const size_t        index    = stack.back();
        const ResolvedPass& resolved = m_resolved[index];
        stack.pop_back();

        for (const ResourceRef& ref: resolved.readRefs)
        {
            const auto it = writers.find(MakeResourceKey_(ref));
            if (it == writers.end())
            {
                continue;
            }

            for (const size_t writer: it->second)
            {
                if (writer != index && !bAlive[writer])
                {
                    bAlive[writer] = true;
                    stack.push_back(writer);
                }
            }
        }
        for (const ResourceRef& ref: resolved.readWriteRefs)
        {
            const auto it = writers.find(MakeResourceKey_(ref));
            if (it == writers.end())
            {
                continue;
            }

            for (const size_t writer: it->second)
            {
                if (writer != index && !bAlive[writer])
                {
                    bAlive[writer] = true;
                    stack.push_back(writer);
                }
            }
        }
    }

    // BFS 가 끝난 뒤에 검사한다. final pass 가 쓴 걸 다른 뿌리가 읽어서 간접적으로 살아나는 경우가 있어
    // 시딩 직후에 검사하면 멀쩡한 그래프에서 오탐이 난다.
    JUG_ASSERT(bAlive[finalIndex], "Final pass '{}' is culled. Mark it with AsSideEffect.", m_descs[finalIndex].name);

    // 살아남은 패스만 등록 순서대로 싣는다.
    m_compiledPasses.clear();
    for (size_t i = 0; i < m_descs.size(); ++i)
    {
        const PassDesc& desc = m_descs[i];

        if (!desc.bEnable)
        {
            JUG_CORE_LOG_TRACE("RenderGraph: pass '{}' culled. reason = disabled", desc.name);
            continue;
        }

        if (!bAlive[i])
        {
            JUG_CORE_LOG_TRACE("RenderGraph: pass '{}' culled. reason = does not affect the final pass", desc.name);
            continue;
        }

        CompiledPass& pass = m_compiledPasses.emplace_back();
        pass.descIndex     = i;

        JUG_CORE_LOG_TRACE("RenderGraph: pass '{}' kept. order = {}", desc.name, m_compiledPasses.size() - 1);
    }

    JUG_CORE_LOG_TRACE("RenderGraph compiled. passes = {} / {}, final = '{}'", m_compiledPasses.size(), m_descs.size(), m_descs[finalIndex].name);

    // 해저드 해결 + 바인드 최적화.
    // 파이프라인의 바인딩 상태를 슬롯 배열로 미러링하며 패스를 순서대로 흘려본다.
    // 리소스 -> 슬롯 역인덱스를 따로 두지 않는 이유: 슬롯 공간이 스테이지 x 16 뿐이라 선형 스캔이 충분하고,
    // 축출이 그냥 덮어쓰기가 돼서 "같은 슬롯에 다른 리소스" / "read -> write -> 같은 슬롯 read" / ABA 가 저절로 풀린다.
    ARRAY<ResourceRef, kNumReadSlots>             liveRead    = {};
    ARRAY<ResourceRef, kNumReadWriteSlots>        liveRW      = {};
    ARRAY<ConstantBufferHandle, kNumCBufferSlots> liveCBuffer = {};
    ARRAY<Sampler, kNumSamplerSlots>              liveSampler = {};
    Vector<ResourceRef>                           liveWrites  = {};

    // 두 바퀴 도는 이유 -> 마지막 패스의 종료 상태가 다음 프레임 첫 패스의 시작 상태라 거기서도 충돌할 수 있다.
    // 1 회차는 상태가 비어 있어(= 보수적) 바인드 커맨드만 걷고, 2 회차는 상태가 수렴한 뒤라 언바인드만 기록한다.
    // 각 슬롯의 최종 상태는 그 슬롯을 마지막으로 건드린 패스가 정하므로 한 바퀴면 고정점에 도달한다.
    const size_t numPasses = m_compiledPasses.size();
    for (size_t step = 0; step < numPasses * 2; ++step)
    {
        const bool    bFirstLap = step < numPasses;
        CompiledPass& pass      = m_compiledPasses[step % numPasses];

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
        for (const ResourceRef& ref: resolved.readRefs)
        {
            for (uint32_t s = 0; s < kNumReadWriteSlots; ++s)
            {
                if (liveRW[s] && liveRW[s] == ref)
                {
                    rwUnbindMask |= 1ull << s;
                }
            }
            for (const ResourceRef& write: liveWrites)
            {
                if (write == ref)
                {
                    bUnbindFrameBuffer = true;
                    break;
                }
            }
        }

        // read-write 하려는 리소스가 SRV 로 살아 있으면 그 SRV 를, RTV 로 살아 있으면 프레임버퍼를 끊는다.
        for (const ResourceRef& ref: resolved.readWriteRefs)
        {
            for (uint32_t s = 0; s < kNumReadSlots; ++s)
            {
                if (liveRead[s] && liveRead[s] == ref)
                {
                    readUnbindMask |= 1ull << s;
                }
            }
            for (const ResourceRef& write: liveWrites)
            {
                if (write == ref)
                {
                    bUnbindFrameBuffer = true;
                    break;
                }
            }
        }

        // 어태치먼트로 write 하려는 리소스가 SRV/UAV 로 살아 있으면 그 슬롯을 끊는다.
        for (const ResourceRef& ref: resolved.writeRefs)
        {
            for (uint32_t s = 0; s < kNumReadSlots; ++s)
            {
                if (liveRead[s] && liveRead[s] == ref)
                {
                    readUnbindMask |= 1ull << s;
                }
            }
            for (uint32_t s = 0; s < kNumReadWriteSlots; ++s)
            {
                if (liveRW[s] && liveRW[s] == ref)
                {
                    rwUnbindMask |= 1ull << s;
                }
            }
        }

        // 2회차 것만 최종 결과로 남긴다.
        if (!bFirstLap)
        {
            pass.readUnbindMask     = readUnbindMask;
            pass.rwUnbindMask       = rwUnbindMask;
            pass.bUnbindFrameBuffer = bUnbindFrameBuffer;
        }

        // 방금 정한 언바인드를 시뮬레이션 상태에도 그대로 반영한다.
        // 1 회차에도 반영해야 한다. 안 그러면 2 회차 시작 상태가 실제 GPU 상태와 어긋난다.
        // 마스크 순회는 최하위 선 비트를 훑고(std::countr_zero) mask &= mask - 1 로 지운다.
        {
            uint64_t mask = readUnbindMask;
            while (mask != 0)
            {
                const uint64_t s = std::countr_zero(mask);
                mask &= mask - 1;
                liveRead[s] = ResourceRef {};
            }

            mask = rwUnbindMask;
            while (mask != 0)
            {
                const uint64_t s = std::countr_zero(mask);
                mask &= mask - 1;
                liveRW[s] = ResourceRef {};
            }

            if (bUnbindFrameBuffer)
            {
                liveWrites.clear();
            }
        }

        // 바인드 커맨드 수집 + 가상 바인드. 같은 슬롯에 이미 같은 값이 있으면 커맨드를 내지 않는다 (중복 바인드 제거).
        // 수집은 1 회차에서만 한다. 2 회차는 같은 패스를 다시 도는 거라 또 담으면 커맨드가 두 배가 된다.
        // 상태 갱신(live* 대입)은 두 회차 모두 한다.
        for (size_t d = 0; d < resolved.cbufferHandles.size(); ++d)
        {
            const uint32_t             slot = desc.cbuffers[d].slot;
            const ConstantBufferHandle cbh  = resolved.cbufferHandles[d];

            if (bFirstLap && liveCBuffer[slot] != cbh)
            {
                CBufferBind& bind = pass.cbuffers.emplace_back();
                bind.cbh          = cbh;
                bind.slot         = slot;
            }
            liveCBuffer[slot] = cbh;
        }

        for (size_t d = 0; d < desc.samplers.size(); ++d)
        {
            const SamplerDecl& decl = desc.samplers[d];

            if (bFirstLap && !(liveSampler[decl.slot] == decl.state))
            {
                SamplerBind& bind = pass.samplers.emplace_back();
                bind.state        = decl.state;
                bind.slot         = decl.slot;
            }
            liveSampler[decl.slot] = decl.state;
        }

        for (size_t d = 0; d < resolved.readRefs.size(); ++d)
        {
            const uint32_t     slot = desc.reads[d].slot;
            const ResourceRef& ref  = resolved.readRefs[d];

            if (bFirstLap && liveRead[slot] != ref)
            {
                ResourceBind& bind = pass.reads.emplace_back();
                bind.ref           = ref;
                bind.slot          = slot;
            }
            liveRead[slot] = ref;
        }

        for (size_t d = 0; d < resolved.readWriteRefs.size(); ++d)
        {
            const uint32_t     slot = desc.readWrites[d].slot;
            const ResourceRef& ref  = resolved.readWriteRefs[d];

            if (bFirstLap && liveRW[slot] != ref)
            {
                ResourceBind& bind = pass.readWrites.emplace_back();
                bind.ref           = ref;
                bind.slot          = slot;
            }
            liveRW[slot] = ref;
        }

        // 프레임버퍼를 가진 패스만 write 상태를 갈아끼운다.
        // 컴퓨트 패스는 OM 을 건드리지 않으므로 직전 렌더 패스의 프레임버퍼가 그대로 물려 있다.
        if (resolved.fbh)
        {
            liveWrites = resolved.writeRefs;
        }
    }
}

// 컴파일된 목록을 순서대로 재생한다. 여기서는 판단을 하지 않는다 -- 전부 컴파일 때 정해져 있다.
// 전제: 이 구간 동안 그래프 밖에서 바인딩을 직접 건드리지 않는다. 건드리면 컴파일된 언바인드 목록이 그 프레임만 어긋난다.
void RenderGraph::Execute()
{
    CompileIfNeed_();

    Graphics* pGfx = Graphics::GetInstance();

    for (const CompiledPass& pass: m_compiledPasses)
    {
        const PassDesc&     desc     = m_descs[pass.descIndex];
        const ResolvedPass& resolved = m_resolved[pass.descIndex];

        pGfx->PushDebugGroup(desc.name);

        if (desc.type == ePass::Utility)
        {
            if (desc.executor)
            {
                desc.executor();
            }
        }
        else
        {
            // 이 패스가 바인딩하려는 리소스가 직전 패스에 다른 용도로 물려 있으면 먼저 끊는다.
            // 끊을 게 하나도 없으면 분기 하나로 통째 건너뛴다. Touch 도 안 나간다.
            if (pass.readUnbindMask != 0 || pass.rwUnbindMask != 0 || pass.bUnbindFrameBuffer)
            {
                uint64_t mask = pass.readUnbindMask;
                while (mask != 0)
                {
                    const uint32_t s = static_cast<uint32_t>(std::countr_zero(mask));
                    mask &= mask - 1;
                    pGfx->SetTexture(kNullHandle, static_cast<eShader>(s >> kSlotShift), s & kSlotMask);
                }

                mask = pass.rwUnbindMask;
                while (mask != 0)
                {
                    const uint32_t s = static_cast<uint32_t>(std::countr_zero(mask));
                    mask &= mask - 1;
                    pGfx->SetTextureRW(kNullHandle, static_cast<eShaderRW>(s >> kSlotShift), s & kSlotMask);
                }

                if (pass.bUnbindFrameBuffer)
                {
                    pGfx->SetFrameBuffer(kNullHandle);
                }

                // Set* 은 셰도우 캐시에만 쓰고 끝난다. 여기서 Touch 로 한 번 플러시해야
                // 다음 바인드보다 먼저 언바인드가 GPU 에 나간다. 이게 빠지면 해저드 해결이 통째로 무의미해진다.
                pGfx->Touch();
            }

            // 컴퓨트는 프로그램만. 래스터 상태와 OM 은 렌더 패스에서만 세팅한다.
            if (desc.type == ePass::Compute)
            {
                pGfx->SetComputeProgram(resolved.ph);
            }
            else
            {
                pGfx->SetProgram(resolved.ph);
                pGfx->SetRenderState(desc.renderState);
                pGfx->SetStencil(desc.frontStencil, desc.backStencil, desc.stencilRef);

                for (const BlendDecl& blend: desc.blends)
                {
                    pGfx->SetBlend(blend.flags, blend.slot);
                }

                if (desc.bHasBlendFactor)
                {
                    pGfx->SetBlendFactor(desc.blendFactor);
                }

                pGfx->SetFrameBuffer(resolved.fbh);
                pGfx->SetViewport(resolved.viewportX, resolved.viewportY, resolved.viewportW, resolved.viewportH);

                if (desc.renderState & eRenderState::Scissor)
                {
                    pGfx->SetScissor(desc.scissorX, desc.scissorY, desc.scissorW, desc.scissorH);
                }

                for (const ClearDecl& clear: desc.renderTargetClears)
                {
                    pGfx->ClearRenderTarget(resolved.fbh, clear.color, clear.slot);
                }

                if (desc.bClearDepth || desc.bClearStencil)
                {
                    pGfx->ClearDepthStencil(resolved.fbh, desc.bClearDepth, desc.bClearStencil, desc.depthClearValue, desc.stencilClearValue);
                }
            }

            // 컴파일 때 중복이 제거된 목록이다. 평탄 슬롯을 다시 (스테이지, 슬롯) 으로 편다.
            for (const CBufferBind& bind: pass.cbuffers)
            {
                pGfx->SetConstantBuffer(bind.cbh, static_cast<eShader>(bind.slot >> kSlotShift), bind.slot & kSlotMask);
            }

            for (const SamplerBind& bind: pass.samplers)
            {
                pGfx->SetSampler(bind.state.flags, bind.state.border, static_cast<eShader>(bind.slot >> kSlotShift), bind.slot & kSlotMask);
            }

            // SRV 바인드. 참조가 텍스처냐 스토리지 버퍼냐에 따라 호출이 갈린다.
            for (const ResourceBind& bind: pass.reads)
            {
                const eShader  shader = static_cast<eShader>(bind.slot >> kSlotShift);
                const uint32_t slot   = bind.slot & kSlotMask;

                if (bind.ref.GetType() == eResource::Texture)
                {
                    pGfx->SetTexture(bind.ref.GetTextureHandle(), shader, slot);
                }
                else
                {
                    pGfx->SetBuffer(bind.ref.GetStorageBufferHandle(), shader, slot);
                }
            }

            // UAV 바인드. PS 단계는 OM 슬롯 공간을 렌더 타겟과 나눠 쓰므로 슬롯이 numRts 이상이어야 한다 (디버그에서 검증).
            for (const ResourceBind& bind: pass.readWrites)
            {
                const eShaderRW shader = static_cast<eShaderRW>(bind.slot >> kSlotShift);
                const uint32_t  slot   = bind.slot & kSlotMask;

                if (bind.ref.GetType() == eResource::Texture)
                {
                    pGfx->SetTextureRW(bind.ref.GetTextureHandle(), shader, slot);
                }
                else
                {
                    pGfx->SetBufferRW(bind.ref.GetStorageBufferHandle(), shader, slot);
                }
            }

            if (desc.executor)
            {
                desc.executor();
            }
        }

        pGfx->PopDebugGroup();
    }
}

}   // namespace jug
