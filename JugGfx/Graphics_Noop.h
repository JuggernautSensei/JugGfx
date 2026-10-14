#pragma once
#include "Graphics.h"

namespace jug
{

// 아무것도 그리지 않는 백엔드. 핸들은 기반이 발급하고 검증도 기반이 하므로 D3D11 과 같은 오류를 잡는다.
// 헤드리스 실행, 로직 테스트용.
class Graphics_Noop final : public Graphics
{
    JUG_CLASS(Graphics_Noop, NO_COPY, NO_MOVE)

public:
    explicit Graphics_Noop(Flags<eGraphicsOption> _flags);
    ~Graphics_Noop() override;

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

    [[nodiscard]] static uint64_t MakeMapKey_(
        BufferRef _buffer);

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

    // 버퍼마다 CPU 메모리를 따로 둔다. ImGui 처럼 두 버퍼를 동시에 Map 해도 서로 덮어쓰지 않는다.
    NoopHashMap<uint64_t, Vector<std::byte>> m_mapBuffers = {};
};

}   // namespace jug
