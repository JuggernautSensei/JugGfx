#include "pch.h"
#include "Graphics_Noop.h"

namespace jug
{

// ===========================================
//  Graphics_Noop
// ===========================================

Graphics_Noop::Graphics_Noop(
    const Flags<eGraphicsOption> _flags)
    : Graphics(eGraphicsAPI::Noop, _flags)
{
    JUG_CORE_LOG_INFO("Noop graphics initialized");
}

Graphics_Noop::~Graphics_Noop() = default;

// ===========================================
//  Debug
// ===========================================

void Graphics_Noop::SetName(const VertexBufferHandle, const StringView)
{
}

void Graphics_Noop::SetName(const InstanceBufferHandle, const StringView)
{
}

void Graphics_Noop::SetName(const IndexBufferHandle, const StringView)
{
}

void Graphics_Noop::SetName(const ConstantBufferHandle, const StringView)
{
}

void Graphics_Noop::SetName(const StorageBufferHandle, const StringView)
{
}

void Graphics_Noop::SetName(const TextureHandle, const StringView)
{
}

void Graphics_Noop::SetName(const FrameBufferHandle, const StringView)
{
}

void Graphics_Noop::SetName(const ShaderHandle, const StringView)
{
}

void Graphics_Noop::PushDebugGroup(const StringView) const
{
}

void Graphics_Noop::PopDebugGroup() const
{
}

void Graphics_Noop::SetDebugMarker(const StringView) const
{
}

void Graphics_Noop::Touch()
{
}

// ===========================================
//  System
// ===========================================

void Graphics_Noop::Frame_()
{
}

void Graphics_Noop::CompleteReadback_(
    const ReadbackRequest& _request)
{
    // 읽을 GPU 데이터가 없으므로 0 으로 채운다.
    const size_t size = static_cast<size_t>(_request.rowPitch) * _request.numRows * _request.depth;
    std::memset(_request.dst.GetPtr(), 0, size);
}

// ===========================================
//  Create
// ===========================================

void Graphics_Noop::CreateVertexBuffer_(const VertexBufferHandle, const MemoryView)
{
}

void Graphics_Noop::CreateInstanceBuffer_(const InstanceBufferHandle)
{
}

void Graphics_Noop::CreateIndexBuffer_(const IndexBufferHandle, const MemoryView)
{
}

void Graphics_Noop::CreateConstantBuffer_(const ConstantBufferHandle)
{
}

void Graphics_Noop::CreateStorageBuffer_(const StorageBufferHandle, const MemoryView)
{
}

void Graphics_Noop::CreateTexture_(const TextureHandle, const MemoryView)
{
}

void Graphics_Noop::CreateFrameBuffer_(const FrameBufferHandle)
{
}

void Graphics_Noop::CreateSwapChain_(const FrameBufferHandle)
{
}

void Graphics_Noop::CreateShader_(const ShaderHandle, const ShaderBytecode&)
{
}

// ===========================================
//  Destroy
// ===========================================

void Graphics_Noop::Destroy_(const VertexBufferHandle _vbh)
{
    m_mapBuffers.erase(MakeMapKey_(_vbh));
}

void Graphics_Noop::Destroy_(const InstanceBufferHandle _instbh)
{
    m_mapBuffers.erase(MakeMapKey_(_instbh));
}

void Graphics_Noop::Destroy_(const IndexBufferHandle _ibh)
{
    m_mapBuffers.erase(MakeMapKey_(_ibh));
}

void Graphics_Noop::Destroy_(const ConstantBufferHandle _cbh)
{
    m_mapBuffers.erase(MakeMapKey_(_cbh));
}

void Graphics_Noop::Destroy_(const StorageBufferHandle _sbh)
{
    m_mapBuffers.erase(MakeMapKey_(_sbh));
}

void Graphics_Noop::Destroy_(const TextureHandle)
{
}

void Graphics_Noop::Destroy_(const FrameBufferHandle)
{
}

void Graphics_Noop::Destroy_(const ShaderHandle)
{
}

// ===========================================
//  Buffer
// ===========================================

void Graphics_Noop::UpdateBuffer_(const BufferRef, const MemoryView, const uint32_t)
{
}

MutableMemoryView Graphics_Noop::MapBuffer_(
    const BufferRef _buffer)
{
    Vector<std::byte>& memory = m_mapBuffers[MakeMapKey_(_buffer)];
    if (memory.empty())
    {
        memory.resize(ResolveBufferRef_(_buffer).byteWidth);
    }
    return { memory.data(), memory.size() };
}

void Graphics_Noop::UnmapBuffer_(const BufferRef)
{
}

void Graphics_Noop::CopyBuffer_(const BufferRef, const uint32_t, const BufferRef, const uint32_t, const uint32_t)
{
}

uint64_t Graphics_Noop::MakeMapKey_(
    const BufferRef _buffer)
{
    uint32_t value = 0;
    switch (_buffer.GetType())
    {
        case eBuffer::Vertex: value = _buffer.GetVertexBufferHandle().GetValue(); break;
        case eBuffer::Instance: value = _buffer.GetInstanceBufferHandle().GetValue(); break;
        case eBuffer::Index: value = _buffer.GetIndexBufferHandle().GetValue(); break;
        case eBuffer::Constant: value = _buffer.GetConstantBufferHandle().GetValue(); break;
        case eBuffer::Storage: value = _buffer.GetStorageBufferHandle().GetValue(); break;
    }
    return (static_cast<uint64_t>(_buffer.GetType()) << 32) | value;
}

// ===========================================
//  Texture
// ===========================================

void Graphics_Noop::UpdateTexture_(const TextureHandle, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const MemoryView)
{
}

void Graphics_Noop::CopyTexture_(const TextureHandle, const TextureHandle)
{
}

void Graphics_Noop::CopyTexture_(const TextureHandle, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const TextureHandle, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t)
{
}

// ===========================================
//  Frame Buffer
// ===========================================

void Graphics_Noop::ResizeSwapChain_(const FrameBufferHandle, const uint32_t, const uint32_t)
{
}

void Graphics_Noop::ClearRenderTarget_(const FrameBufferHandle, const RGBA, const uint32_t)
{
}

void Graphics_Noop::ClearDepthStencil_(const FrameBufferHandle, const bool, const bool, const float, const uint8_t)
{
}

// ===========================================
//  Execute
// ===========================================

void Graphics_Noop::Submit_()
{
}

void Graphics_Noop::Submit_(const StorageBufferHandle, const uint32_t, const uint32_t)
{
}

void Graphics_Noop::Dispatch_(const uint32_t, const uint32_t, const uint32_t)
{
}

void Graphics_Noop::Dispatch_(const StorageBufferHandle, const uint32_t)
{
}

}   // namespace jug
