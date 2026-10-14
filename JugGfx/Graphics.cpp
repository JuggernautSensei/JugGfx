#include "pch.h"
#include "Graphics.h"

#include "Shader.h"

namespace jug
{

namespace
{
    Graphics* g_pSingleton = nullptr;

    // 로그용 enum 이름 (EnumRefl). 범위 밖 값이어도 assert 하지 않는다.
    template<EnumT E>
    [[nodiscard]] StringView ToString_(
        const E _value)
    {
        return IsValid(_value) ? NameOf(_value) : StringView { "?" };
    }

}   // namespace

// ===========================================
//  Graphics
// ===========================================

Graphics::Graphics(
    const eGraphicsAPI           _api,
    const Flags<eGraphicsOption> _flags)
    : m_api(_api)
{
    JUG_ASSERT(!g_pSingleton, "Graphics instance already exists.");
    g_pSingleton = this;

    m_caps.flags = _flags;
}

Graphics::~Graphics()
{
    JUG_ASSERT(ReportLiveObjects() == 0, "There are leaked graphics resources. Check the log for details.");

    JUG_ASSERT(g_pSingleton == this, "Graphics instance is not the singleton.");
    g_pSingleton = nullptr;
}

// ===========================================
//  System
// ===========================================

Graphics& Graphics::GetSingleton()
{
    JUG_ASSERT(g_pSingleton, "Graphics instance is not created yet.");
    return *g_pSingleton;
}

eGraphicsAPI Graphics::GetAPI() const
{
    return m_api;
}

size_t Graphics::ReportLiveObjects()
{
    size_t num = 0;

    for (const VertexBufferHandle vbh: m_vertexBufferPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked VertexBuffer: Handle = {}", vbh);
        ++num;
    }

    for (const InstanceBufferHandle instbh: m_instanceBufferPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked InstanceBuffer: Handle = {}", instbh);
        ++num;
    }

    for (const IndexBufferHandle ibh: m_indexBufferPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked IndexBuffer: Handle = {}", ibh);
        ++num;
    }

    for (const StorageBufferHandle sbh: m_storageBufferPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked StorageBuffer: Handle = {}", sbh);
        ++num;
    }

    for (const ConstantBufferHandle cbh: m_constantBufferPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked ConstantBuffer: Handle = {}", cbh);
        ++num;
    }

    for (const TextureHandle texh: m_texturePool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked Texture: Handle = {}", texh);
        ++num;
    }

    for (const FrameBufferHandle fbh: m_frameBufferPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked FrameBuffer: Handle = {}", fbh);
        ++num;
    }

    for (const ShaderHandle sh: m_shaderPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked Shader: Handle = {}", sh);
        ++num;
    }

    for (const ProgramHandle ph: m_programPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked Program: Handle = {}", ph);
        ++num;
    }

    for (const VertexLayoutHandle vlh: m_vertexLayoutPool.GetHandles())
    {
        JUG_CORE_LOG_WARN("Leaked VertexLayout: Handle = {}", vlh);
        ++num;
    }

    if (num > 0)
    {
        JUG_CORE_LOG_WARN("Total leaked graphics resources: {}", num);
    }

    return num;
}

const GraphicsCaps& Graphics::GetCaps() const
{
    return m_caps;
}

const GraphicsStats& Graphics::GetStats() const
{
    return m_lastStats;
}

uint64_t Graphics::GetFrameIndex() const
{
    return m_frameIndex;
}

void Graphics::Frame()
{
    Frame_();
    m_bReadbackNeedFlush = false;

    // 이 Frame() 이 증가시킨 뒤의 인덱스가 readyFrame 에 도달하는 요청을 처리한다.
    const uint64_t nextFrameIndex = m_frameIndex + 1;
    for (size_t i = 0; i < m_readbackRequests.size();)
    {
        const ReadbackRequest& request = m_readbackRequests[i];
        if (request.readyFrame > nextFrameIndex)
        {
            ++i;
            continue;
        }
        CompleteReadback_(request);
        m_readbackRequests[i] = m_readbackRequests.back();
        m_readbackRequests.pop_back();
    }

    ++m_frameIndex;
    m_lastStats = m_stats;
    m_stats     = {};
}

// ===========================================
//  Vertex Stream
// ===========================================

VertexBufferHandle Graphics::CreateVertexBuffer(
    const MemoryView    _vertexData,
    const VertexLayout& _vl)
{
    const uint32_t stride = _vl.GetStride();
    JUG_ASSERT(!_vertexData.IsEmpty(), "A static vertex buffer requires initial data. Use CreateDynamicVertexBuffer instead.");
    JUG_ASSERT(_vertexData.GetSize() % stride == 0, "Vertex data size is not a multiple of the vertex stride.");

    const uint32_t           numVertices = static_cast<uint32_t>(_vertexData.GetSize()) / stride;
    const VertexBufferHandle vbh         = EmplaceVertexBuffer_(numVertices, _vl, false, _vertexData);
    JUG_CORE_LOG_TRACE("VertexBuffer created. handle = {}, numVertices = {}, stride = {}, bytes = {}, dynamic = false", vbh, numVertices, stride, _vertexData.GetSize());
    return vbh;
}

VertexBufferHandle Graphics::CreateDynamicVertexBuffer(
    const uint32_t      _numVertices,
    const VertexLayout& _vl)
{
    const VertexBufferHandle vbh = EmplaceVertexBuffer_(_numVertices, _vl, true, {});
    JUG_CORE_LOG_TRACE("VertexBuffer created. handle = {}, numVertices = {}, stride = {}, dynamic = true", vbh, _numVertices, _vl.GetStride());
    return vbh;
}

InstanceBufferHandle Graphics::CreateInstanceBuffer(
    const uint32_t _numInstances,
    const uint32_t _stride)
{
    JUG_ASSERT(_numInstances > 0, "Instance buffer must have at least one element.");
    JUG_ASSERT(_stride > 0 && _stride % kInstanceDataSizeAlign == 0, "Instance stride must be a non-zero multiple of the instance data alignment.");

    InstanceBufferDesc instb = {};
    instb.stride             = _stride;
    instb.numInstances       = _numInstances;
    instb.byteWidth          = _stride * _numInstances;

    const InstanceBufferHandle instbh = m_instanceBufferPool.Emplace(instb);
    CreateInstanceBuffer_(instbh);
    JUG_CORE_LOG_TRACE("InstanceBuffer created. handle = {}, numInstances = {}, stride = {}, dynamic = true", instbh, _numInstances, _stride);
    return instbh;
}

VertexBufferHandle Graphics::EmplaceVertexBuffer_(
    const uint32_t      _numElems,
    const VertexLayout& _vl,
    const bool          _bDynamic,
    const MemoryView    _initDataOrEmpty)
{
    JUG_ASSERT(_numElems > 0, "Vertex buffer must have at least one element.");
    JUG_ASSERT(_vl.GetNumAttributes() > 0, "Vertex layout must have at least one attribute.");

    VertexBufferDesc vb = {};
    vb.bDynamic         = _bDynamic;
    vb.stride           = _vl.GetStride();
    vb.numVertices      = _numElems;
    vb.byteWidth        = vb.stride * _numElems;
    vb.vlh              = GetOrCreateVertexLayoutHandle_(_vl);

    const VertexBufferHandle vbh = m_vertexBufferPool.Emplace(vb);
    CreateVertexBuffer_(vbh, _initDataOrEmpty);
    return vbh;
}

// ===========================================
//  Index Buffer
// ===========================================

IndexBufferHandle Graphics::CreateIndexBuffer(
    const MemoryView _indexData,
    const bool       _bU32)
{
    const uint32_t stride = _bU32 ? sizeof(uint32_t) : sizeof(uint16_t);
    JUG_ASSERT(!_indexData.IsEmpty(), "A static index buffer requires initial data. Use CreateDynamicIndexBuffer instead.");
    JUG_ASSERT(_indexData.GetSize() % stride == 0, "Index data size is not a multiple of the index stride.");
    JUG_ASSERT(_indexData.GetSize() > 0, "Index data must not be empty.");

    const uint32_t          numIndices = static_cast<uint32_t>(_indexData.GetSize()) / stride;
    const IndexBufferHandle ibh        = EmplaceIndexBuffer_(numIndices, _bU32, false, _indexData);
    JUG_CORE_LOG_TRACE("IndexBuffer created. handle = {}, numIndices = {}, u32 = {}, bytes = {}, dynamic = false", ibh, numIndices, _bU32, _indexData.GetSize());
    return ibh;
}

IndexBufferHandle Graphics::CreateDynamicIndexBuffer(
    const uint32_t _numIndices,
    const bool     _bU32)
{
    const IndexBufferHandle ibh = EmplaceIndexBuffer_(_numIndices, _bU32, true, {});
    JUG_CORE_LOG_TRACE("IndexBuffer created. handle = {}, numIndices = {}, u32 = {}, dynamic = true", ibh, _numIndices, _bU32);
    return ibh;
}

IndexBufferHandle Graphics::EmplaceIndexBuffer_(
    const uint32_t   _numElems,
    const bool       _bU32,
    const bool       _bDynamic,
    const MemoryView _initDataOrEmpty)
{
    JUG_ASSERT(_numElems > 0, "Index buffer must have at least one element.");

    IndexBufferDesc ib = {};
    ib.bDynamic        = _bDynamic;
    ib.bU32            = _bU32;
    ib.numIndices      = _numElems;
    ib.byteWidth       = (_bU32 ? sizeof(uint32_t) : sizeof(uint16_t)) * _numElems;

    const IndexBufferHandle ibh = m_indexBufferPool.Emplace(ib);
    CreateIndexBuffer_(ibh, _initDataOrEmpty);
    return ibh;
}

// ===========================================
//  Constant Buffer
// ===========================================

ConstantBufferHandle Graphics::CreateConstantBuffer(
    const uint32_t _byteWidth)
{
    JUG_ASSERT(_byteWidth > 0 && _byteWidth % kCBufferSizeAlign == 0, "Constant buffer size must be a non-zero multiple of 16 bytes.");

    ConstantBufferDesc cb = {};
    cb.byteWidth          = _byteWidth;

    const ConstantBufferHandle cbh = m_constantBufferPool.Emplace(cb);
    CreateConstantBuffer_(cbh);
    JUG_CORE_LOG_TRACE("ConstantBuffer created. handle = {}, bytes = {}", cbh, _byteWidth);
    return cbh;
}

// ===========================================
//  Storage Buffer
// ===========================================

StorageBufferHandle Graphics::CreateStructuredBuffer(
    const uint32_t                    _numElements,
    const uint32_t                    _stride,
    const Flags<eStorageBufferOption> _flags,
    const MemoryView                  _initDataOrEmpty)
{
    JUG_ASSERT(!_flags.HasAll({ eStorageBufferOption::Dynamic, eStorageBufferOption::ShaderReadWrite }), "A storage buffer cannot be both dynamic and shader read writable.");

    StorageBufferDesc sb = {};
    sb.byteWidth         = _stride * _numElements;
    sb.stride            = _stride;
    sb.flags             = _flags;
    sb.type              = eStorageBuffer::Structured;

    const StorageBufferHandle sbh = m_storageBufferPool.Emplace(sb);
    CreateStorageBuffer_(sbh, _initDataOrEmpty);
    JUG_CORE_LOG_TRACE("StorageBuffer created. handle = {}, m_type = Structured, numElements = {}, stride = {}, flags = {:#x}", sbh, _numElements, _stride, _flags.GetFlags());
    return sbh;
}

StorageBufferHandle Graphics::CreateReadbackBuffer(
    const uint32_t _byteWidth)
{
    JUG_ASSERT(_byteWidth > 0, "A readback buffer requires a non-zero size.");

    StorageBufferDesc sb = {};
    sb.byteWidth         = _byteWidth;
    sb.stride            = 0;
    sb.type              = eStorageBuffer::Readback;
    sb.flags             = {};

    const StorageBufferHandle sbh = m_storageBufferPool.Emplace(sb);
    CreateStorageBuffer_(sbh, {});
    JUG_CORE_LOG_TRACE("StorageBuffer created. handle = {}, m_type = Readback, bytes = {}", sbh, _byteWidth);
    return sbh;
}

StorageBufferHandle Graphics::CreateIndirectBuffer(
    const uint32_t                    _numDraws,
    const Flags<eStorageBufferOption> _flags)
{
    JUG_ASSERT(_numDraws > 0, "An indirect args buffer requires at least one draw.");
    JUG_ASSERT(!_flags.Has(eStorageBufferOption::Dynamic), "An indirect args buffer cannot be dynamic.");

    StorageBufferDesc sb = {};
    sb.stride            = kIndirectArgsStride;
    sb.byteWidth         = kIndirectArgsStride * _numDraws;
    sb.type              = eStorageBuffer::IndirectArgs;
    sb.flags             = _flags;

    const StorageBufferHandle sbh = m_storageBufferPool.Emplace(sb);
    CreateStorageBuffer_(sbh, {});
    JUG_CORE_LOG_TRACE("StorageBuffer created. handle = {}, m_type = IndirectArgs, numDraws = {}, flags = {:#x}", sbh, _numDraws, _flags.GetFlags());
    return sbh;
}

// ===========================================
//  Buffer Utils
// ===========================================

void Graphics::UpdateBuffer(
    const BufferRef  _buffer,
    const MemoryView _data,
    const uint32_t   _offset)
{
    JUG_ASSERT(!_data.IsEmpty(), "UpdateBuffer requires a non-empty data.");

    const ResolveBufferRefResult resolve = ResolveBufferRef_(_buffer);
    const uint32_t               size    = static_cast<uint32_t>(_data.GetSize());
    JUG_ASSERT(_offset + size <= resolve.byteWidth, "UpdateBuffer data size exceeds the buffer size.");
    JUG_ASSERT(!resolve.bDynamic || (_offset == 0 && size == resolve.byteWidth) || _buffer.GetType() != eBuffer::Constant, "A constant buffer cannot be partially updated.");

    UpdateBuffer_(_buffer, _data, _offset);
}

MutableMemoryView Graphics::MapBuffer(
    const BufferRef _buffer)
{
    JUG_ASSERT(ResolveBufferRef_(_buffer).bDynamic, "Only a dynamic buffer can be mapped.");
    return MapBuffer_(_buffer);
}

void Graphics::UnmapBuffer(
    const BufferRef _buffer)
{
    JUG_ASSERT(ResolveBufferRef_(_buffer).bDynamic, "Only a dynamic buffer can be unmapped.");
    UnmapBuffer_(_buffer);
}

void Graphics::CopyBuffer(
    const BufferRef _dst,
    const BufferRef _src)
{
    const ResolveBufferRefResult dst = ResolveBufferRef_(_dst);
    const ResolveBufferRefResult src = ResolveBufferRef_(_src);

    JUG_ASSERT(!dst.bDynamic, "A dynamic buffer cannot be a copy destination.");
    JUG_ASSERT(_dst.GetType() != eBuffer::Storage || !IsReadbackPending_(_dst.GetStorageBufferHandle()), "A readback buffer with a pending read cannot be a copy destination.");
    JUG_ASSERT(!(_dst == _src), "A buffer cannot be copied onto itself.");
    JUG_ASSERT(dst.byteWidth == src.byteWidth, "A whole buffer copy requires both buffers to have the same size.");

    CopyBuffer_(_dst, 0, _src, 0, src.byteWidth);
}

void Graphics::CopyBuffer(
    const BufferRef _dst,
    const uint32_t  _dstOffset,
    const BufferRef _src,
    const uint32_t  _srcOffset,
    const uint32_t  _byteWidthOrInvalid)
{
    const ResolveBufferRefResult dst = ResolveBufferRef_(_dst);
    const ResolveBufferRefResult src = ResolveBufferRef_(_src);

    JUG_ASSERT(!dst.bDynamic, "A dynamic buffer cannot be a copy destination.");
    JUG_ASSERT(_dst.GetType() != eBuffer::Storage || !IsReadbackPending_(_dst.GetStorageBufferHandle()), "A readback buffer with a pending read cannot be a copy destination.");
    JUG_ASSERT(_srcOffset < src.byteWidth, "Buffer copy source offset is out of bounds.");

    const uint32_t size = _byteWidthOrInvalid == kInvalidIndex ? src.byteWidth - _srcOffset : _byteWidthOrInvalid;

    JUG_ASSERT(_srcOffset + size <= src.byteWidth, "Buffer copy source range is out of bounds.");
    JUG_ASSERT(_dstOffset + size <= dst.byteWidth, "Buffer copy destination range is out of bounds.");
    JUG_ASSERT(!(_dst == _src) || _dstOffset + size <= _srcOffset || _srcOffset + size <= _dstOffset, "A buffer copy cannot overlap itself.");

    CopyBuffer_(_dst, _dstOffset, _src, _srcOffset, size);
}

uint64_t Graphics::ReadBuffer(
    const StorageBufferHandle _readbackSbh,
    const MutableMemoryView   _dst)
{
    const StorageBufferDesc& sb = m_storageBufferPool[_readbackSbh];
    JUG_ASSERT(sb.type == eStorageBuffer::Readback, "ReadBuffer can only be called on a readback storage buffer.");
    JUG_ASSERT(_dst.GetSize() >= sb.byteWidth, "ReadBuffer destination is smaller than the buffer.");

    ReadbackRequest request = {};
    request.resource        = _readbackSbh;
    request.index           = 0;
    request.rowPitch        = sb.byteWidth;
    request.numRows         = 1;
    request.depth           = 1;
    request.dst             = _dst;
    return RequestReadback_(request);
}

Graphics::ResolveBufferRefResult Graphics::ResolveBufferRef_(
    const BufferRef _buffer) const
{
    JUG_ASSERT(_buffer, "Invalid buffer handle.");

    switch (_buffer.GetType())
    {
        case eBuffer::Vertex:
        {
            const VertexBufferDesc& vb = m_vertexBufferPool[_buffer.GetVertexBufferHandle()];
            return { vb.byteWidth, vb.bDynamic };
        }
        case eBuffer::Instance:
        {
            const InstanceBufferDesc& instb = m_instanceBufferPool[_buffer.GetInstanceBufferHandle()];
            return { instb.byteWidth, true };
        }
        case eBuffer::Index:
        {
            const IndexBufferDesc& ib = m_indexBufferPool[_buffer.GetIndexBufferHandle()];
            return { ib.byteWidth, ib.bDynamic };
        }
        case eBuffer::Constant:
        {
            const ConstantBufferDesc& cb = m_constantBufferPool[_buffer.GetConstantBufferHandle()];
            return { cb.byteWidth, true };
        }
        case eBuffer::Storage:
        {
            const StorageBufferDesc& sb = m_storageBufferPool[_buffer.GetStorageBufferHandle()];
            return { sb.byteWidth, sb.flags.Has(eStorageBufferOption::Dynamic) };
        }
        default:
        {
            JUG_ASSERT(false, "Unsupported buffer m_type {}.", static_cast<uint32_t>(_buffer.GetType()));
            return {};
        }
    }
}

VertexLayoutHandle Graphics::GetOrCreateVertexLayoutHandle_(
    const VertexLayout& _vl)
{
    JUG_ASSERT(_vl.GetNumAttributes() > 0, "A vertex layout must have at least one attribute.");

    auto it = m_vlhCache.find(_vl.GetHash());
    if (it != m_vlhCache.end())
    {
        const VertexLayoutHandle vlh = it->second;
        ++m_vertexLayoutPool[vlh].refCount;
        return vlh;
    }

    const VertexLayoutHandle vlh     = m_vertexLayoutPool.Emplace(_vl);
    m_vertexLayoutPool[vlh].refCount = 1;
    m_vlhCache.emplace(_vl.GetHash(), vlh);
    return vlh;
}

void Graphics::ReleaseVertexLayout_(
    const VertexLayoutHandle _vlh)
{
    VertexLayoutDesc& vl = m_vertexLayoutPool[_vlh];

    JUG_ASSERT(vl.refCount > 0, "Vertex layout reference count underflow.");
    if (--vl.refCount > 0)
    {
        return;
    }

    // refcount == 0. erase from cache
    m_vlhCache.erase(vl.vl.GetHash());
    m_vertexLayoutPool.Erase(_vlh);
}

uint64_t Graphics::RequestReadback_(
    const ReadbackRequest& _request)
{
    JUG_ASSERT(!_request.dst.IsEmpty(), "A readback requires a non-empty destination.");
    JUG_ASSERT(!IsReadbackPending_(_request.resource), "A readback is already pending on this resource.");

    ReadbackRequest request = _request;
    request.readyFrame      = m_frameIndex + kReadbackLatency + 1;   // 처리하는 Frame() 이 증가시킨 뒤의 인덱스
    m_readbackRequests.push_back(request);
    m_bReadbackNeedFlush = true;
    return request.readyFrame;
}

bool Graphics::IsReadbackPending_(
    const ResourceRef _resource) const
{
    for (const ReadbackRequest& request: m_readbackRequests)
    {
        if (request.resource == _resource)
        {
            return true;
        }
    }
    return false;
}

// ===========================================
//  Texture
// ===========================================

TextureHandle Graphics::CreateTexture2D(
    const uint32_t              _width,
    const uint32_t              _height,
    const eTextureFormat        _format,
    const uint32_t              _numLayers,
    const eMSAA                 _msaa,
    const Flags<eTextureOption> _flags,
    const MemoryView            _initDataOrEmpty)
{
    const TextureHandle texh = EmplaceTexture_(_width, _height, 1, eTexture::Texture2D, _format, _numLayers, _msaa, _flags, _initDataOrEmpty);
    const TextureDesc&  td   = m_texturePool[texh];
    JUG_CORE_LOG_TRACE("Texture2D created. handle = {}, {}x{}, format = {}, layers = {}, mips = {}, msaa = {}, flags = {:#x}", texh, td.width, td.height, ToString_(td.format), td.numLayers, td.numMips, ToString_(td.msaa), td.flags.GetFlags());
    return texh;
}

TextureHandle Graphics::CreateTextureCube(
    const uint32_t              _width,
    const uint32_t              _height,
    const eTextureFormat        _format,
    const uint32_t              _numCubes,
    const Flags<eTextureOption> _flags,
    const MemoryView            _initDataOrEmpty)
{
    const TextureHandle texh = EmplaceTexture_(_width, _height, 1, eTexture::TextureCube, _format, _numCubes * 6, eMSAA::None, _flags, _initDataOrEmpty);
    const TextureDesc&  td   = m_texturePool[texh];
    JUG_CORE_LOG_TRACE("TextureCube created. handle = {}, {}x{}, format = {}, cubes = {}, mips = {}, flags = {:#x}", texh, td.width, td.height, ToString_(td.format), _numCubes, td.numMips, td.flags.GetFlags());
    return texh;
}

TextureHandle Graphics::CreateTexture3D(
    const uint32_t              _width,
    const uint32_t              _height,
    const uint32_t              _depth,
    const eTextureFormat        _format,
    const Flags<eTextureOption> _flags,
    const MemoryView            _initDataOrEmpty)
{
    const TextureHandle texh = EmplaceTexture_(_width, _height, _depth, eTexture::Texture3D, _format, 1, eMSAA::None, _flags, _initDataOrEmpty);
    const TextureDesc&  td   = m_texturePool[texh];
    JUG_CORE_LOG_TRACE("Texture3D created. handle = {}, {}x{}x{}, format = {}, mips = {}, flags = {:#x}", texh, td.width, td.height, td.depth, ToString_(td.format), td.numMips, td.flags.GetFlags());
    return texh;
}

TextureHandle Graphics::EmplaceTexture_(
    const uint32_t              _width,
    const uint32_t              _height,
    const uint32_t              _depth,
    const eTexture              _type,
    const eTextureFormat        _format,
    const uint32_t              _numLayers,
    const eMSAA                 _msaa,
    const Flags<eTextureOption> _flags,
    const MemoryView            _initDataOrEmpty)
{
    JUG_ASSERT(_width > 0 && _height > 0 && _depth > 0, "A texture must have a non-zero size.");
    JUG_ASSERT(_numLayers > 0, "A texture must have at least one layer.");

    const bool bDepth    = IsDepthFormat(_format);
    const bool bReadback = _flags.Has(eTextureOption::Readback);
    const bool b3D       = _type == eTexture::Texture3D;

    JUG_ASSERT(_type != eTexture::TextureCube || _numLayers % 6 == 0, "A cube texture requires a layer count that is a multiple of 6.");
    JUG_ASSERT(!b3D || _numLayers == 1, "A 3D texture cannot have layers. Use the depth instead.");
    JUG_ASSERT(b3D || _depth == 1, "Only a 3D texture can have a depth greater than 1.");
    JUG_ASSERT(!bDepth || !_flags.Has(eTextureOption::ShaderReadWrite), "A depth-stencil texture cannot be bound as an unordered access view.");
    JUG_ASSERT(!bReadback || (_flags.GetFlags() & ~(static_cast<uint32_t>(eTextureOption::Readback) | static_cast<uint32_t>(eTextureOption::HasMips))) == 0, "A readback texture cannot carry any option other than HasMips.");
    JUG_ASSERT(!bReadback || (_format != eTextureFormat::D24_UNorm_S8_UInt && _format != eTextureFormat::D32_Float_S8_UInt), "A readback texture cannot use a depth-stencil format with a stencil plane.");
    JUG_ASSERT(!bReadback || _initDataOrEmpty.IsEmpty(), "A readback texture cannot be initialized with data.");
    JUG_ASSERT(!bReadback || _msaa == eMSAA::None, "A readback texture cannot be multisampled.");
    JUG_ASSERT(!b3D || _msaa == eMSAA::None, "A 3D texture cannot be multisampled.");
    JUG_ASSERT(_msaa == eMSAA::None || _initDataOrEmpty.IsEmpty(), "An MSAA texture cannot be initialized with data.");
    JUG_ASSERT(!IsCompressedFormat(_format) || !_flags.Has(eTextureOption::RenderTarget) && !_flags.Has(eTextureOption::DepthStencil) && !_flags.Has(eTextureOption::ShaderReadWrite), "A block compressed texture cannot be a render target, depth-stencil or unordered access view.");
    JUG_ASSERT(!IsCompressedFormat(_format) || _width % 4 == 0 && _height % 4 == 0, "A block compressed texture requires a width and height that are multiples of 4.");

    TextureDesc texture = {};
    texture.width       = _width;
    texture.height      = _height;
    texture.depth       = _depth;
    texture.type        = _type;
    texture.format      = _format;
    texture.numLayers   = _numLayers;
    texture.numMips     = _flags & eTextureOption::HasMips ? CalcNumMips(_width, _height, _depth) : 1;
    texture.msaa        = _msaa;   // 백엔드가 지원하지 않으면 CreateTexture_ 에서 하향 기록한다.
    texture.refCount    = 1;
    texture.flags       = _flags;
    texture.bImmutable  = !_initDataOrEmpty.IsEmpty() && !_flags.HasAny({ eTextureOption::RenderTarget, eTextureOption::DepthStencil, eTextureOption::ShaderReadWrite });

    const TextureHandle texh = m_texturePool.Emplace(texture);
    CreateTexture_(texh, _initDataOrEmpty);
    return texh;
}

void Graphics::UpdateTextureChecked_(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const uint32_t      _offsetX,
    const uint32_t      _offsetY,
    const uint32_t      _offsetZ,
    const uint32_t      _width,
    const uint32_t      _height,
    const uint32_t      _depth,
    const MemoryView    _data)
{
    const TextureDesc& tex = m_texturePool[_texh];
    JUG_ASSERT(!tex.bImmutable, "An immutable texture cannot be updated.");
    JUG_ASSERT(_mip < tex.numMips, "Invalid mip level {} for texture with {} mips.", _mip, tex.numMips);
    JUG_ASSERT(_layer < tex.numLayers, "Invalid layer {} for texture with {} layers.", _layer, tex.numLayers);

    const VEC3U size = CalcTextureSize(tex.width, tex.height, tex.depth, _mip);
    JUG_ASSERT(_offsetX + _width <= size.width, "UpdateTexture width exceeds the mip {} size {}.", _mip, size.width);
    JUG_ASSERT(_offsetY + _height <= size.height, "UpdateTexture height exceeds the mip {} size {}.", _mip, size.height);
    JUG_ASSERT(_offsetZ + _depth <= size.depth, "UpdateTexture depth exceeds the mip {} size {}.", _mip, size.depth);

    const uint32_t byteWidth = CalcSlicePitch(tex.format, _width, _height) * _depth;
    JUG_ASSERT(_data.GetSize() == byteWidth, "UpdateTexture data size does not match the expected size for mip {} layer {}.", _mip, _layer);

    UpdateTexture_(_texh, _mip, _layer, _offsetX, _offsetY, _offsetZ, _width, _height, _depth, _data);
}

void Graphics::UpdateTexture2D(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _width,
    const uint32_t      _height,
    const MemoryView    _data)
{
    UpdateTextureChecked_(_texh, _mip, _layer, _x, _y, 0, _width, _height, 1, _data);
}

void Graphics::UpdateTextureCube(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const eCubeFace     _face,
    const uint32_t      _layer,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _width,
    const uint32_t      _height,
    const MemoryView    _data)
{
    const uint32_t layer = static_cast<uint32_t>(_face) + _layer * 6;
    UpdateTextureChecked_(_texh, _mip, layer, _x, _y, 0, _width, _height, 1, _data);
}

void Graphics::UpdateTexture3D(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _x,
    const uint32_t      _y,
    const uint32_t      _z,
    const uint32_t      _width,
    const uint32_t      _height,
    const uint32_t      _depth,
    const MemoryView    _data)
{
    UpdateTextureChecked_(_texh, _mip, 0, _x, _y, _z, _width, _height, _depth, _data);
}

void Graphics::CopyTexture(
    const TextureHandle _dstTexh,
    const TextureHandle _srcTexh)
{
    const TextureDesc& dst = m_texturePool[_dstTexh];
    const TextureDesc& src = m_texturePool[_srcTexh];

    JUG_ASSERT(!dst.bImmutable, "An immutable texture cannot be a copy destination.");
    JUG_ASSERT(!IsReadbackPending_(_dstTexh), "A readback texture with a pending read cannot be a copy destination.");
    JUG_ASSERT(_dstTexh != _srcTexh, "A texture cannot be copied onto itself.");
    JUG_ASSERT(dst.type == src.type && dst.width == src.width && dst.height == src.height && dst.depth == src.depth && dst.numMips == src.numMips && dst.numLayers == src.numLayers && dst.msaa == src.msaa, "A whole texture copy requires both textures to have the same shape.");

    CopyTexture_(_dstTexh, _srcTexh);
}

void Graphics::CopyTexture(
    const TextureHandle _dstTexh,
    const uint32_t      _dstMip,
    const uint32_t      _dstLayer,
    const uint32_t      _dstX,
    const uint32_t      _dstY,
    const uint32_t      _dstZ,
    const TextureHandle _srcTexh,
    const uint32_t      _srcMip,
    const uint32_t      _srcLayer,
    const uint32_t      _srcX,
    const uint32_t      _srcY,
    const uint32_t      _srcZ,
    const uint32_t      _widthOrInvalid,
    const uint32_t      _heightOrInvalid,
    const uint32_t      _depthOrInvalid)
{
    const TextureDesc& dst = m_texturePool[_dstTexh];
    const TextureDesc& src = m_texturePool[_srcTexh];

    const VEC3U dstSize = CalcTextureSize(dst.width, dst.height, dst.depth, _dstMip);
    const VEC3U srcSize = CalcTextureSize(src.width, src.height, src.depth, _srcMip);

    JUG_ASSERT(!dst.bImmutable, "An immutable texture cannot be a copy destination.");
    JUG_ASSERT(!IsReadbackPending_(_dstTexh), "A readback texture with a pending read cannot be a copy destination.");
    JUG_ASSERT(_dstMip < dst.numMips && _dstLayer < dst.numLayers, "Texture copy destination subresource is out of range.");
    JUG_ASSERT(_srcMip < src.numMips && _srcLayer < src.numLayers, "Texture copy source subresource is out of range.");
    JUG_ASSERT(_dstTexh != _srcTexh || CalcTextureIndex(_dstMip, _dstLayer, dst.numMips) != CalcTextureIndex(_srcMip, _srcLayer, src.numMips), "A texture cannot be copied onto itself.");
    JUG_ASSERT(_srcX < srcSize.width && _srcY < srcSize.height && _srcZ < srcSize.depth, "Texture copy source offset is out of bounds.");
    JUG_ASSERT(_dstX < dstSize.width && _dstY < dstSize.height && _dstZ < dstSize.depth, "Texture copy destination offset is out of bounds.");

    const uint32_t width  = _widthOrInvalid == kInvalidIndex ? srcSize.width - _srcX : _widthOrInvalid;
    const uint32_t height = _heightOrInvalid == kInvalidIndex ? srcSize.height - _srcY : _heightOrInvalid;
    const uint32_t depth  = _depthOrInvalid == kInvalidIndex ? srcSize.depth - _srcZ : _depthOrInvalid;

    JUG_ASSERT(_srcX + width <= srcSize.width && _srcY + height <= srcSize.height && _srcZ + depth <= srcSize.depth, "Texture copy source range is out of bounds.");
    JUG_ASSERT(_dstX + width <= dstSize.width && _dstY + height <= dstSize.height && _dstZ + depth <= dstSize.depth, "Texture copy destination range is out of bounds.");

    const bool bWholeSubresource = _srcX == 0 && _srcY == 0 && _srcZ == 0 && width == srcSize.width && height == srcSize.height && depth == srcSize.depth;
    JUG_ASSERT(!IsDepthFormat(src.format) || bWholeSubresource, "A depth-stencil texture can only be copied as a whole subresource.");

    CopyTexture_(_dstTexh, _dstMip, _dstLayer, _dstX, _dstY, _dstZ, _srcTexh, _srcMip, _srcLayer, _srcX, _srcY, _srcZ, width, height, depth);
}

uint64_t Graphics::ReadTexture(
    const TextureHandle     _readbackTexh,
    const MutableMemoryView _dst,
    const uint32_t          _layer,
    const uint32_t          _mip)
{
    const TextureDesc& texture = m_texturePool[_readbackTexh];
    JUG_ASSERT(texture.flags & eTextureOption::Readback, "ReadTexture requires a readback texture. Copy into one first.");
    JUG_ASSERT(_mip < texture.numMips && _layer < texture.numLayers, "Texture subresource is out of range.");

    const VEC3U size = CalcTextureSize(texture.width, texture.height, texture.depth, _mip);

    ReadbackRequest request = {};
    request.resource        = _readbackTexh;
    request.index           = CalcTextureIndex(_mip, _layer, texture.numMips);
    request.rowPitch        = CalcRowPitch(texture.format, size.width);
    request.numRows         = CalcNumRows(texture.format, size.height);   // compressed 면 블록 줄 수
    request.depth           = size.depth;
    request.dst             = _dst;
    JUG_ASSERT(_dst.GetSize() >= static_cast<size_t>(request.rowPitch) * request.numRows * request.depth, "ReadTexture destination is smaller than the subresource.");
    return RequestReadback_(request);
}

// ===========================================
//  Frame Buffer
// ===========================================

FrameBufferHandle Graphics::CreateFrameBuffer(
    const Span<const Attachment> _atts,
    const bool                   _bOwnership)
{
    JUG_ASSERT(!_atts.empty(), "A frame buffer requires at least one attachment.");
    JUG_ASSERT(_atts.size() <= kNumMaxAttachmentSlots, "Too many frame buffer attachments.");

    FrameBufferDesc fb       = {};
    Attachment      depthAtt = {};
    for (const Attachment& att: _atts)
    {
        JUG_ASSERT(att.texh, "Invalid texture handle in a frame buffer attachment.");

        TextureDesc& texture = m_texturePool[att.texh];
        JUG_ASSERT(att.mip < texture.numMips, "Frame buffer attachment mip is out of range.");
        JUG_ASSERT(att.numLayers > 0 && att.offset + att.numLayers <= texture.numLayers, "Frame buffer attachment layer range is out of bounds.");

        // 소유권을 넘겨받지 않으면 프레임버퍼가 참조를 하나 더 든다.
        if (!_bOwnership)
        {
            ++texture.refCount;
        }

        fb.bMSAA = fb.bMSAA || texture.msaa != eMSAA::None;

        if (IsDepthFormat(texture.format))
        {
            JUG_ASSERT(!fb.bHasDepth, "A frame buffer can have only one depth-stencil attachment.");
            JUG_ASSERT(texture.flags.Has(eTextureOption::DepthStencil), "The attachment texture was not created with eTextureOption::DepthStencil.");

            // 깊이는 컬러 뒤 슬롯에 놓는다. 컬러 개수가 확정된 뒤에 써야 하므로 여기서는 들고만 있는다.
            depthAtt     = att;
            fb.bHasDepth = true;
            continue;
        }

        JUG_ASSERT(fb.numRts < kNumMaxRenderTargetSlots, "Too many render target attachments.");
        JUG_ASSERT(texture.flags.Has(eTextureOption::RenderTarget), "The attachment texture was not created with eTextureOption::RenderTarget.");

        fb.atts[fb.numRts] = att;
        ++fb.numRts;
    }

    if (fb.bHasDepth)
    {
        fb.atts[fb.numRts] = depthAtt;
    }

    JUG_ASSERT(fb.numRts > 0 || fb.bHasDepth, "A frame buffer requires at least one render target or a depth-stencil.");
    const FrameBufferHandle fbh = m_frameBufferPool.Emplace(fb);
    CreateFrameBuffer_(fbh);
    JUG_CORE_LOG_TRACE("FrameBuffer created. handle = {}, numRts = {}, depth = {}, msaa = {}, ownership = {}", fbh, fb.numRts, fb.bHasDepth, fb.bMSAA, _bOwnership);
    return fbh;
}

FrameBufferHandle Graphics::CreateFrameBuffer(
    const TextureHandle _texh,
    const bool          _bOwnership)
{
    JUG_ASSERT(_texh, "A frame buffer requires a valid texture handle.");

    const ARRAY<Attachment, 1> atts = {
        Attachment { _texh, 0, 0, m_texturePool[_texh].numLayers }
    };
    return CreateFrameBuffer(atts, _bOwnership);
}

FrameBufferHandle Graphics::CreateFrameBuffer(
    const SDL_WindowID   _wndID,
    const uint32_t       _width,
    const uint32_t       _height,
    const eTextureFormat _format,
    const uint32_t       _numBuffers)
{
    JUG_ASSERT(_wndID, "A window frame buffer requires a live window.");
    JUG_ASSERT(_width > 0 && _height > 0, "A window frame buffer requires a non-zero size.");
    JUG_ASSERT(!IsDepthFormat(_format), "A swap chain requires a color format.");
    JUG_ASSERT(_numBuffers >= 2, "A flip model swap chain requires at least two back buffers.");

    // 백버퍼 텍스처. 실제 리소스는 CreateSwapChain_ 가 스왑체인에서 얻는다.
    TextureDesc texture      = {};
    texture.width            = _width;
    texture.height           = _height;
    texture.depth            = 1;
    texture.type             = eTexture::Texture2D;
    texture.format           = _format;
    texture.numLayers        = 1;
    texture.numMips          = 1;
    texture.msaa             = eMSAA::None;
    texture.refCount         = 1;
    texture.bImmutable       = false;
    texture.flags            = { eTextureOption::RenderTarget, eTextureOption::ShaderWriteOnly };
    const TextureHandle texh = m_texturePool.Emplace(texture);

    FrameBufferDesc fb = {};
    fb.atts[0]         = Attachment { texh, 0, 0, 1 };
    fb.numRts          = 1;
    fb.bHasDepth       = false;
    fb.bMSAA           = false;
    fb.wndID           = _wndID;
    fb.numBuffers      = _numBuffers;
    fb.bVSync          = true;

    const FrameBufferHandle fbh = m_frameBufferPool.Emplace(fb);
    CreateSwapChain_(fbh);
    m_swapChainFbhs.push_back(fbh);

    JUG_CORE_LOG_INFO("Swap chain frame buffer created. handle = {}, {}x{}, format = {}, buffers = {}, tearing = {}", fbh, _width, _height, ToString_(_format), _numBuffers, m_caps.bAllowTearing);
    return fbh;
}

FrameBufferHandle Graphics::FindFrameBufferOrInvalid(
    const SDL_WindowID _wndID)
{
    JUG_ASSERT(_wndID != 0, "FindFrameBufferOrInvalid requires a valid window ID.");

    for (const FrameBufferHandle fbh: m_swapChainFbhs)
    {
        const FrameBufferDesc& fb = m_frameBufferPool[fbh];
        if (fb.wndID == _wndID)
        {
            return fbh;
        }
    }
    return kInvalidHandle;
}

void Graphics::ResizeFrameBuffer(
    const FrameBufferHandle _swapChainFbh,
    const uint32_t          _width,
    const uint32_t          _height)
{
    const FrameBufferDesc& fb = m_frameBufferPool[_swapChainFbh];
    JUG_ASSERT(fb.wndID != 0, "ResizeFrameBuffer requires a window frame buffer.");

    // 윈도우 최소화는 무시
    if (_width == 0 || _height == 0)
    {
        return;
    }

    // 리사이즈 필요없음.
    TextureDesc& texture = m_texturePool[fb.atts[0].texh];
    if (texture.width == _width && texture.height == _height)
    {
        return;
    }

    // 컨텍스트가 프레임버퍼를 바인딩하고 있으면 리사이즈가 안되므로 바인딩 상태를 푼다.
    m_state.fbhOrInvalid     = kInvalidHandle;
    m_state.lastFbhOrInvalid = kInvalidHandle;
    m_state.dirtyFlags |= ePipelineDirty::FrameBuffer;

    ResizeSwapChain_(_swapChainFbh, _width, _height);

    texture.width  = _width;
    texture.height = _height;

    JUG_CORE_LOG_INFO("Swap chain frame buffer resized. handle = {}, {}x{}", _swapChainFbh, _width, _height);
}

void Graphics::SetVSync(
    const FrameBufferHandle _swapChainFbh,
    const bool              _bVSync)
{
    FrameBufferDesc& fb = m_frameBufferPool[_swapChainFbh];
    JUG_ASSERT(fb.wndID != 0, "SetVSync requires a window frame buffer.");
    fb.bVSync = _bVSync;

    JUG_CORE_LOG_TRACE("Swap chain vsync changed. handle = {}, vsync = {}", _swapChainFbh, _bVSync);
}

void Graphics::ClearRenderTarget(
    const FrameBufferHandle _fbh,
    const RGBA              _color,
    const uint32_t          _slot)
{
    const FrameBufferDesc& fb = m_frameBufferPool[_fbh];
    JUG_ASSERT(_slot < fb.numRts, "Render target slot is out of range.");
    ClearRenderTarget_(_fbh, _color, _slot);
}

void Graphics::ClearRenderTargets(
    const FrameBufferHandle _fbh,
    const RGBA              _color)
{
    const FrameBufferDesc& fb = m_frameBufferPool[_fbh];
    for (uint32_t i = 0; i < fb.numRts; ++i)
    {
        ClearRenderTarget_(_fbh, _color, i);
    }
}

void Graphics::ClearDepthStencil(
    const FrameBufferHandle _fbh,
    const bool              _bClearDepth,
    const bool              _bClearStencil,
    const float             _depth,
    const uint8_t           _stencil)
{
    if (!_bClearDepth && !_bClearStencil)
    {
        return;
    }

    const FrameBufferDesc& fb = m_frameBufferPool[_fbh];
    JUG_ASSERT(fb.bHasDepth, "The frame buffer has no depth stencil attachment.");
    ClearDepthStencil_(_fbh, _bClearDepth, _bClearStencil, _depth, _stencil);
}

// ===========================================
//  Shader & Program
// ===========================================

ShaderHandle Graphics::CreateShader(
    const ShaderBytecode& _bytecode)
{
    const MemoryView bytecode = _bytecode.GetBytecode();
    const uint64_t   hash     = Hash<Murmur3>(bytecode);
    const auto       it       = m_shaderCache.find(hash);
    if (it != m_shaderCache.end())
    {
        const ShaderHandle cached = it->second;
        ++m_shaderPool[cached].refCount;
        return cached;
    }

    JUG_ASSERT(m_api == eGraphicsAPI::Noop || _bytecode.target == GetShaderTarget(m_api), "Shader target does not match the graphics API.");

    ShaderDesc shader = {};
    shader.type       = _bytecode.type;
    shader.refCount   = 1;
    shader.hash       = hash;

    const ShaderHandle sh = m_shaderPool.Emplace(shader);
    CreateShader_(sh, _bytecode);
    m_shaderCache[hash] = sh;
    JUG_CORE_LOG_TRACE("Shader created. handle = {}, stage = {}, bytes = {}, hash = {:#x}", sh, ToString_(_bytecode.type), bytecode.GetSize(), hash);
    return sh;
}

ProgramHandle Graphics::CreateProgram(
    const ShaderHandle _vsh,
    const ShaderHandle _psh,
    const bool         _bOwnership)
{
    JUG_ASSERT(_vsh && _psh, "A graphics program requires both a vertex shader and a pixel shader.");
    JUG_ASSERT(m_shaderPool[_vsh].type == eShader::Vertex, "The first shader must be a vertex shader.");
    JUG_ASSERT(m_shaderPool[_psh].type == eShader::Pixel, "The second shader must be a pixel shader.");

    if (!_bOwnership)
    {
        ++m_shaderPool[_vsh].refCount;
        ++m_shaderPool[_psh].refCount;
    }

    ProgramDesc prog  = {};
    prog.vshOrInvalid = _vsh;
    prog.pshOrInvalid = _psh;
    prog.cshOrInvalid = kInvalidHandle;

    const ProgramHandle ph = m_programPool.Emplace(prog);
    JUG_CORE_LOG_TRACE("Program created. handle = {}, vsh = {}, psh = {}, ownership = {}", ph, _vsh, _psh, _bOwnership);
    return ph;
}

ProgramHandle Graphics::CreateComputeProgram(
    const ShaderHandle _csh,
    const bool         _bOwnership)
{
    JUG_ASSERT(_csh, "A compute program requires a valid compute shader.");
    JUG_ASSERT(m_shaderPool[_csh].type == eShader::Compute, "The shader must be a compute shader.");

    if (!_bOwnership)
    {
        ++m_shaderPool[_csh].refCount;
    }

    ProgramDesc prog  = {};
    prog.vshOrInvalid = kInvalidHandle;
    prog.pshOrInvalid = kInvalidHandle;
    prog.cshOrInvalid = _csh;

    const ProgramHandle ph = m_programPool.Emplace(prog);

    JUG_CORE_LOG_TRACE("ComputeProgram created. handle = {}, csh = {}, ownership = {}", ph, _csh, _bOwnership);
    return ph;
}

// ===========================================
//  Destroy
// ===========================================

void Graphics::Destroy(
    const VertexBufferHandle _vbh)
{
    const VertexBufferDesc& vb = m_vertexBufferPool[_vbh];

    for (uint32_t i = 0; i < kNumMaxStreams; ++i)
    {
        if (m_state.vbhs[i] == _vbh)
        {
            m_state.vbhs[i]          = kInvalidHandle;
            m_state.vlhs[i]          = kInvalidHandle;
            m_state.vertexStrides[i] = 0;
            m_state.vertexOffsets[i] = 0;
            m_state.dirtyFlags |= { ePipelineDirty::VertexBuffer, ePipelineDirty::InputLayout };
        }
    }

    // release vertex layout
    if (vb.vlh)
    {
        ReleaseVertexLayout_(vb.vlh);
    }

    Destroy_(_vbh);
    m_vertexBufferPool.Erase(_vbh);
}

void Graphics::Destroy(
    const InstanceBufferHandle _instbh)
{
    if (m_state.instbhOrInvalid == _instbh)
    {
        m_state.instbhOrInvalid = kInvalidHandle;
        m_state.instanceStride  = 0;
        m_state.instanceOffset  = 0;
        m_state.numInstances    = 0;
        m_state.dirtyFlags |= { ePipelineDirty::InstanceBuffer, ePipelineDirty::InputLayout };
    }

    Destroy_(_instbh);
    m_instanceBufferPool.Erase(_instbh);
}

void Graphics::Destroy(
    const IndexBufferHandle _ibh)
{
    if (m_state.ibhOrInvalid == _ibh)
    {
        m_state.ibhOrInvalid = kInvalidHandle;
        m_state.indexOffset  = 0;
        m_state.numIndices   = 0;
        m_state.dirtyFlags |= ePipelineDirty::IndexBuffer;
    }

    Destroy_(_ibh);
    m_indexBufferPool.Erase(_ibh);
}

void Graphics::Destroy(
    const ConstantBufferHandle _cbh)
{
    // unbind
    for (CBufferBind& bind: m_state.cbufferBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxCBufferSlots; ++slot)
        {
            if (bind.resources[slot] == _cbh)
            {
                bind.resources[slot] = kInvalidHandle;
                bind.MarkDirty(slot);
                m_state.dirtyFlags |= ePipelineDirty::ConstantBuffer;
            }
        }
    }

    Destroy_(_cbh);
    m_constantBufferPool.Erase(_cbh);
}

void Graphics::Destroy(
    const StorageBufferHandle _sbh)
{
    JUG_ASSERT(!IsReadbackPending_(_sbh), "A buffer with a pending readback cannot be destroyed.");

    UnbindResource_(_sbh);
    Destroy_(_sbh);
    m_storageBufferPool.Erase(_sbh);
}

void Graphics::Destroy(
    const TextureHandle _texh)
{
    TextureDesc& texture = m_texturePool[_texh];

    JUG_ASSERT(texture.refCount > 0, "Texture reference count underflow.");
    if (--texture.refCount > 0)
    {
        return;
    }

    // refcount == 0.
    JUG_ASSERT(!IsReadbackPending_(_texh), "A texture with a pending readback cannot be destroyed.");

    UnbindResource_(_texh);
    Destroy_(_texh);
    m_texturePool.Erase(_texh);
}

void Graphics::Destroy(
    const FrameBufferHandle _fbh)
{
    const FrameBufferDesc& fb = m_frameBufferPool[_fbh];

    if (fb.wndID != 0)
    {
        std::erase(m_swapChainFbhs, _fbh);
    }

    // frame buffer 해제
    if (m_state.fbhOrInvalid == _fbh)
    {
        m_state.fbhOrInvalid = kInvalidHandle;
        m_state.dirtyFlags |= ePipelineDirty::FrameBuffer;
    }

    if (m_state.lastFbhOrInvalid == _fbh)
    {
        m_state.lastFbhOrInvalid = kInvalidHandle;
    }

    const uint32_t numAtts = fb.GetNumAttachments();
    for (uint32_t i = 0; i < numAtts; ++i)
    {
        Destroy(fb.atts[i].texh);
    }

    // 백엔드는 뷰를 해제하고, swap chain 은 가장 마지막에 해제한다.
    Destroy_(_fbh);
    m_frameBufferPool.Erase(_fbh);
}

void Graphics::Destroy(
    const ShaderHandle _sh)
{
    ShaderDesc& shader = m_shaderPool[_sh];

    JUG_ASSERT(shader.refCount > 0, "Shader reference count underflow.");
    if (--shader.refCount > 0)
    {
        return;
    }

    m_shaderCache.erase(shader.hash);

    Destroy_(_sh);
    m_shaderPool.Erase(_sh);
}

void Graphics::Destroy(
    const ProgramHandle _ph)
{
    const ProgramDesc& prog = m_programPool[_ph];

    if (m_state.phOrInvalid == _ph)
    {
        m_state.phOrInvalid = kInvalidHandle;
        m_state.dirtyFlags |= { ePipelineDirty::Program, ePipelineDirty::InputLayout };
    }

    if (m_state.computePhOrInvalid == _ph)
    {
        m_state.computePhOrInvalid = kInvalidHandle;
        m_state.dirtyFlags |= ePipelineDirty::ComputeProgram;
    }

    if (prog.vshOrInvalid)
    {
        Destroy(prog.vshOrInvalid);
    }
    if (prog.pshOrInvalid)
    {
        Destroy(prog.pshOrInvalid);
    }
    if (prog.cshOrInvalid)
    {
        Destroy(prog.cshOrInvalid);
    }
    m_programPool.Erase(_ph);
}

void Graphics::UnbindResource_(
    const ResourceRef _resource)
{
    for (ReadBind& bind: m_state.readBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxReadSlots; ++slot)
        {
            if (bind.resources[slot] == _resource)
            {
                bind.resources[slot] = {};
                bind.MarkDirty(slot);
                m_state.dirtyFlags |= ePipelineDirty::ShaderResourceView;
            }
        }
    }

    for (ReadWriteBind& bind: m_state.readWriteBind)
    {
        for (uint32_t slot = 0; slot < kNumMaxReadWriteSlots; ++slot)
        {
            if (bind.resources[slot] == _resource)
            {
                bind.resources[slot] = {};
                bind.MarkDirty(slot);
                m_state.dirtyFlags |= ePipelineDirty::UnorderedAccessView;
            }
        }
    }
}

// ===========================================
//  Getter
// ===========================================

const VertexBufferDesc& Graphics::GetDesc(
    const VertexBufferHandle _vbh) const
{
    return m_vertexBufferPool[_vbh];
}

const InstanceBufferDesc& Graphics::GetDesc(
    const InstanceBufferHandle _instbh) const
{
    return m_instanceBufferPool[_instbh];
}

const IndexBufferDesc& Graphics::GetDesc(
    const IndexBufferHandle _ibh) const
{
    return m_indexBufferPool[_ibh];
}

const ConstantBufferDesc& Graphics::GetDesc(
    const ConstantBufferHandle _cbh) const
{
    return m_constantBufferPool[_cbh];
}

const StorageBufferDesc& Graphics::GetDesc(
    const StorageBufferHandle _sbh) const
{
    return m_storageBufferPool[_sbh];
}

const TextureDesc& Graphics::GetDesc(
    const TextureHandle _texh) const
{
    return m_texturePool[_texh];
}

const ShaderDesc& Graphics::GetDesc(
    const ShaderHandle _sh) const
{
    return m_shaderPool[_sh];
}

const ProgramDesc& Graphics::GetDesc(
    const ProgramHandle _ph) const
{
    return m_programPool[_ph];
}

const FrameBufferDesc& Graphics::GetDesc(
    const FrameBufferHandle _fbh) const
{
    return m_frameBufferPool[_fbh];
}

// ===========================================
//  Bind
// ===========================================

void Graphics::SetVertexBuffer(
    const VertexBufferHandle _vbhOrInvalid,
    const uint32_t           _offset,
    const uint32_t           _numVerticesOrInvalid)
{
    const ARRAY<VertexStream, 1> streams = {
        VertexStream { _vbhOrInvalid, _offset }
    };
    SetVertexBuffers(streams, _numVerticesOrInvalid, kInvalidHandle, 0, kInvalidIndex);
}

void Graphics::SetVertexBuffers(
    const Span<const VertexStream> _streams,
    const uint32_t                 _numVerticesOrInvalid)
{
    SetVertexBuffers(_streams, _numVerticesOrInvalid, kInvalidHandle, 0, kInvalidIndex);
}

void Graphics::SetVertexBuffer(
    const VertexBufferHandle   _vbhOrInvalid,
    const uint32_t             _offset,
    const uint32_t             _numVerticesOrInvalid,
    const InstanceBufferHandle _instbhOrInvalid,
    const uint32_t             _instanceOffset,
    const uint32_t             _numInstancesOrInvalid)
{
    const ARRAY<VertexStream, 1> streams = {
        VertexStream { _vbhOrInvalid, _offset }
    };
    SetVertexBuffers(streams, _numVerticesOrInvalid, _instbhOrInvalid, _instanceOffset, _numInstancesOrInvalid);
}

void Graphics::SetVertexBuffers(
    const Span<const VertexStream> _streams,
    const uint32_t                 _numVerticesOrInvalid,
    const InstanceBufferHandle     _instbhOrInvalid,
    const uint32_t                 _instanceOffset,
    const uint32_t                 _numInstancesOrInvalid)
{
    JUG_ASSERT(_streams.size() <= kNumMaxStreams, "Too many vertex streams.");

    const uint32_t numStreams        = static_cast<uint32_t>(_streams.size());
    const bool     bNumStreamChanged = numStreams != m_state.numStreams;
    bool           bLayoutChanged    = bNumStreamChanged;
    bool           bBufferChanged    = bNumStreamChanged;

    uint32_t numVertices = Max<uint32_t>();
    for (uint32_t i = 0; i < numStreams; ++i)
    {
        VertexLayoutHandle vlhOrInvalid = kInvalidHandle;
        uint32_t           stride       = 0;

        const VertexStream& stream = _streams[i];
        if (stream.vbhOrInvalid)
        {
            const VertexBufferDesc& vb = m_vertexBufferPool[stream.vbhOrInvalid];

            JUG_ASSERT(stream.offset < vb.numVertices, "Vertex stream offset is out of range.");

            vlhOrInvalid = vb.vlh;
            stride       = vb.stride;
            numVertices  = Min(numVertices, vb.numVertices - stream.offset);
        }

        const uint32_t offset = stream.offset * stride;
        bLayoutChanged        = bLayoutChanged || m_state.vlhs[i] != vlhOrInvalid;
        bBufferChanged        = bBufferChanged || m_state.vbhs[i] != stream.vbhOrInvalid || m_state.vertexOffsets[i] != offset;

        m_state.vbhs[i]          = stream.vbhOrInvalid;
        m_state.vlhs[i]          = vlhOrInvalid;
        m_state.vertexStrides[i] = stride;
        m_state.vertexOffsets[i] = offset;
    }

    for (uint32_t i = numStreams; i < m_state.numStreams; ++i)
    {
        m_state.vbhs[i]          = kInvalidHandle;
        m_state.vlhs[i]          = kInvalidHandle;
        m_state.vertexStrides[i] = 0;
        m_state.vertexOffsets[i] = 0;
    }

    // 유효한 스트림이 하나도 없으면 그릴 게 없다.
    if (numVertices == Max<uint32_t>())
    {
        numVertices = 0;
    }

    JUG_ASSERT(_numVerticesOrInvalid == kInvalidIndex || _numVerticesOrInvalid <= numVertices, "Vertex range exceeds the bound vertex streams.");

    m_state.numStreams  = numStreams;
    m_state.numVertices = _numVerticesOrInvalid == kInvalidIndex ? numVertices : _numVerticesOrInvalid;

    uint32_t instanceStride = 0;
    uint32_t numInstances   = 0;
    if (_instbhOrInvalid)
    {
        const InstanceBufferDesc& instb              = m_instanceBufferPool[_instbhOrInvalid];
        const uint32_t            numBufferInstances = instb.numInstances;

        JUG_ASSERT(_instanceOffset < numBufferInstances, "Instance stream offset is out of range.");
        JUG_ASSERT(_numInstancesOrInvalid == kInvalidIndex || _numInstancesOrInvalid <= numBufferInstances - _instanceOffset, "Instance range exceeds the instance buffer.");

        instanceStride = instb.stride;
        numInstances   = numBufferInstances - _instanceOffset;
    }

    const uint32_t instanceOffset = _instanceOffset * instanceStride;
    if (bNumStreamChanged || m_state.instbhOrInvalid != _instbhOrInvalid
        || m_state.instanceStride != instanceStride || m_state.instanceOffset != instanceOffset)
    {
        m_state.dirtyFlags |= ePipelineDirty::InstanceBuffer;
    }

    bLayoutChanged          = bLayoutChanged || m_state.instanceStride != instanceStride;
    m_state.instbhOrInvalid = _instbhOrInvalid;
    m_state.instanceStride  = instanceStride;
    m_state.instanceOffset  = instanceOffset;
    m_state.numInstances    = _numInstancesOrInvalid == kInvalidIndex ? numInstances : _numInstancesOrInvalid;

    if (bBufferChanged)
    {
        m_state.dirtyFlags |= ePipelineDirty::VertexBuffer;
    }

    if (bLayoutChanged)
    {
        m_state.dirtyFlags |= ePipelineDirty::InputLayout;
    }
}

void Graphics::SetIndexBuffer(
    const IndexBufferHandle _ibhOrInvalid,
    const uint32_t          _offset,
    const uint32_t          _numIndicesOrInvalid)
{
    uint32_t numIndices = 0;
    if (_ibhOrInvalid)
    {
        numIndices = m_indexBufferPool[_ibhOrInvalid].numIndices;
    }

    if (m_state.ibhOrInvalid != _ibhOrInvalid)
    {
        m_state.dirtyFlags |= ePipelineDirty::IndexBuffer;
    }

    m_state.ibhOrInvalid = _ibhOrInvalid;
    m_state.indexOffset  = _offset;
    m_state.numIndices   = _numIndicesOrInvalid == kInvalidIndex ? numIndices - _offset : _numIndicesOrInvalid;
    JUG_ASSERT(m_state.indexOffset + m_state.numIndices <= numIndices, "Index buffer range is out of bounds.");
}

void Graphics::SetConstantBuffer(
    const ConstantBufferHandle _cbhOrInvalid,
    const eShader              _shader,
    const uint32_t             _slot)
{
    JUG_ASSERT(_slot < kNumMaxCBufferSlots, "Constant buffer slot is out of range.");

    CBufferBind& bind = m_state.cbufferBind[_shader];
    if (bind.resources[_slot] == _cbhOrInvalid)
    {
        return;
    }

    bind.resources[_slot] = _cbhOrInvalid;
    bind.MarkDirty(_slot);

    m_state.dirtyFlags |= ePipelineDirty::ConstantBuffer;
}

void Graphics::SetTexture(
    const TextureHandle _texhOrInvalid,
    const eShader       _shader,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadSlots, "Shader resource slot is out of range.");

    ReadBind& bind = m_state.readBind[_shader];
    if (bind.resources[_slot] == _texhOrInvalid)
    {
        return;
    }

    bind.resources[_slot] = _texhOrInvalid;
    bind.MarkDirty(_slot);

    m_state.dirtyFlags |= ePipelineDirty::ShaderResourceView;
}

void Graphics::SetTextureRW(
    const TextureHandle _texhOrInvalid,
    const eShaderRW     _shader,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadWriteSlots, "Unordered access slot is out of range.");

    ReadWriteBind& bind = m_state.readWriteBind[_shader];
    if (bind.resources[_slot] == _texhOrInvalid)
    {
        return;
    }

    bind.resources[_slot] = _texhOrInvalid;
    bind.MarkDirty(_slot);
    m_state.dirtyFlags |= ePipelineDirty::UnorderedAccessView;
}

void Graphics::SetBuffer(
    const StorageBufferHandle _sbhOrInvalid,
    const eShader             _shader,
    const uint32_t            _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadSlots, "Shader resource slot is out of range.");

    ReadBind& bind = m_state.readBind[_shader];
    if (bind.resources[_slot] == _sbhOrInvalid)
    {
        return;
    }

    bind.resources[_slot] = _sbhOrInvalid;
    bind.MarkDirty(_slot);
    m_state.dirtyFlags |= ePipelineDirty::ShaderResourceView;
}

void Graphics::SetBufferRW(
    const StorageBufferHandle _sbhOrInvalid,
    const eShaderRW           _shader,
    const uint32_t            _slot)
{
    JUG_ASSERT(_slot < kNumMaxReadWriteSlots, "Unordered access slot is out of range.");

    ReadWriteBind& bind = m_state.readWriteBind[_shader];
    if (bind.resources[_slot] == _sbhOrInvalid)
    {
        return;
    }

    bind.resources[_slot] = _sbhOrInvalid;
    bind.MarkDirty(_slot);
    m_state.dirtyFlags |= ePipelineDirty::UnorderedAccessView;
}

void Graphics::SetSampler(
    const Flags<eSampler> _flags,
    const eShader         _shader,
    const uint32_t        _slot)
{
    SetSampler(_flags, RGBA::Zero(), _shader, _slot);
}

void Graphics::SetSampler(
    const Flags<eSampler> _flags,
    const RGBA            _border,
    const eShader         _shader,
    const uint32_t        _slot)
{
    JUG_ASSERT(_slot < kNumMaxSamplerSlots, "Sampler slot is out of range.");

    const Sampler sampler = { _flags, _border };
    SamplerBind&  bind    = m_state.samplerBind[_shader];
    if (bind.resources[_slot] == sampler)
    {
        return;
    }

    bind.resources[_slot] = sampler;
    bind.MarkDirty(_slot);
    m_state.dirtyFlags |= ePipelineDirty::SamplerState;
}

// ===========================================
//  Render State
// ===========================================

void Graphics::SetRenderState(
    const Flags<eRenderState> _flags)
{
    if (m_state.renderStateFlags == _flags)
    {
        return;
    }

    const Flags<eRenderState> changedFlags = m_state.renderStateFlags ^ _flags;
    m_state.renderStateFlags               = _flags;

    if (changedFlags.HasAny(kTopologyStateMask))
    {
        m_state.dirtyFlags |= ePipelineDirty::PrimitiveTopology;
    }
    if (changedFlags.HasAny(kRasterizerStateMask))
    {
        m_state.dirtyFlags |= ePipelineDirty::RasterizerState;
    }
    if (changedFlags.HasAny(kBlendStateMask))
    {
        m_state.dirtyFlags |= ePipelineDirty::BlendState;
    }
    if (changedFlags.HasAny(kDepthStencilStateMask))
    {
        m_state.dirtyFlags |= ePipelineDirty::DepthStencilState;
    }
}

void Graphics::SetBlend(
    const Flags<eBlend> _flags,
    const uint32_t      _slot)
{
    JUG_ASSERT(_slot < kNumMaxRenderTargetSlots, "Render target slot is out of range.");

    if (m_state.blendFlags[_slot] == _flags)
    {
        return;
    }

    m_state.blendFlags[_slot] = _flags;
    m_state.dirtyFlags |= ePipelineDirty::BlendState;
}

void Graphics::SetBlendFactor(
    const RGBA _factor)
{
    if (m_state.blendFactor.rgba == _factor.rgba)
    {
        return;
    }

    m_state.blendFactor = _factor;
    m_state.dirtyFlags |= ePipelineDirty::BlendState;
}

void Graphics::SetStencil(
    const Flags<eStencil> _frontFlags,
    const Flags<eStencil> _backFlags,
    const uint8_t         _stencilRef)
{
    if (m_state.fstencilFlags == _frontFlags
        && m_state.bstencilFlags == _backFlags
        && m_state.stencilRef == _stencilRef)
    {
        return;
    }

    m_state.fstencilFlags = _frontFlags;
    m_state.bstencilFlags = _backFlags;
    m_state.stencilRef    = _stencilRef;
    m_state.dirtyFlags |= ePipelineDirty::DepthStencilState;
}

// ===========================================
//  Program & Output
// ===========================================

void Graphics::SetProgram(
    const ProgramHandle _phOrInvalid)
{
    if (m_state.phOrInvalid == _phOrInvalid)
    {
        return;
    }

    m_state.phOrInvalid = _phOrInvalid;
    m_state.dirtyFlags |= ePipelineDirty::Program;
}

void Graphics::SetComputeProgram(
    const ProgramHandle _phOrInvalid)
{
    if (m_state.computePhOrInvalid == _phOrInvalid)
    {
        return;
    }

    m_state.computePhOrInvalid = _phOrInvalid;
    m_state.dirtyFlags |= ePipelineDirty::ComputeProgram;
}

void Graphics::SetFrameBuffer(
    const FrameBufferHandle _fbhOrInvalid)
{
    if (m_state.fbhOrInvalid == _fbhOrInvalid)
    {
        return;
    }

    m_state.fbhOrInvalid = _fbhOrInvalid;
    m_state.dirtyFlags |= ePipelineDirty::FrameBuffer;
}

void Graphics::SetViewport(
    const float _x,
    const float _y,
    const float _width,
    const float _height)
{
    if (m_state.viewportX == _x && m_state.viewportY == _y && m_state.viewportW == _width && m_state.viewportH == _height)   // NOLINT
    {
        return;
    }

    m_state.viewportX = _x;
    m_state.viewportY = _y;
    m_state.viewportW = _width;
    m_state.viewportH = _height;
    m_state.dirtyFlags |= ePipelineDirty::Viewport;
}

void Graphics::SetScissor(
    const int _x,
    const int _y,
    const int _width,
    const int _height)
{
    if (m_state.scissorX == _x && m_state.scissorY == _y && m_state.scissorW == _width && m_state.scissorH == _height)
    {
        return;
    }

    m_state.scissorX = _x;
    m_state.scissorY = _y;
    m_state.scissorW = _width;
    m_state.scissorH = _height;
    m_state.dirtyFlags |= ePipelineDirty::ScissorRect;
}

void Graphics::SetSubmitParam(
    const eSubmitParam _param,
    const uint32_t     _value)
{
    m_state.submitParams[_param] = _value;
}

// ===========================================
//  Submit
// ===========================================

void Graphics::Reset()
{
    m_state = PipelineState {};
}

void Graphics::Submit()
{
    const bool bInstanced = static_cast<bool>(m_state.instbhOrInvalid);
    if (bInstanced && m_state.numInstances == 0)
    {
        return;
    }

    Submit_();
    ++m_stats.numDrawCalls;
}

void Graphics::Submit(
    const StorageBufferHandle _indirectSbh,
    const uint32_t            _offset,
    const uint32_t            _numDrawsOrInvalid)
{
    const StorageBufferDesc& indirectBuffer = m_storageBufferPool[_indirectSbh];
    JUG_ASSERT(indirectBuffer.type == eStorageBuffer::IndirectArgs, "Indirect submit requires an indirect args buffer.");
    JUG_ASSERT(_offset % kIndirectArgsStride == 0, "Indirect args offset must be a multiple of the args stride.");

    const uint32_t maxDraws = (indirectBuffer.byteWidth - _offset) / kIndirectArgsStride;
    const uint32_t numDraws = _numDrawsOrInvalid == kInvalidIndex ? maxDraws : _numDrawsOrInvalid;
    JUG_ASSERT(numDraws <= maxDraws, "Indirect draw count exceeds the buffer capacity.");

    Submit_(_indirectSbh, _offset, numDraws);
    m_stats.numDrawCalls += static_cast<int>(numDraws);
}

void Graphics::Dispatch(
    const uint32_t _numGroupsX,
    const uint32_t _numGroupsY,
    const uint32_t _numGroupsZ)
{
    JUG_ASSERT(m_state.computePhOrInvalid, "Dispatch requires a compute program.");

    Dispatch_(_numGroupsX, _numGroupsY, _numGroupsZ);
    ++m_stats.numDispatchCalls;
}

void Graphics::Dispatch(
    const StorageBufferHandle _indirectSbh,
    const uint32_t            _offset)
{
    JUG_ASSERT(m_state.computePhOrInvalid, "Dispatch requires a compute program.");
    JUG_ASSERT(m_storageBufferPool[_indirectSbh].type == eStorageBuffer::IndirectArgs, "Indirect dispatch requires an indirect args buffer.");

    Dispatch_(_indirectSbh, _offset);
    ++m_stats.numDispatchCalls;
}

}   // namespace jug
