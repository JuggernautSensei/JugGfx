#pragma once
#include "Base.h"

struct ImDrawData;
struct ImGuiContext;
struct ImGuiViewport;
struct ImVec2;

namespace jug
{

class ImGuiRenderer
{
    JUG_CLASS(ImGuiRenderer, NO_COPY)

public:
    explicit ImGuiRenderer(SDL_WindowID _wndID, const FilePath& _iniPathOrEmpty = {}, bool _bEnableViewports = false);
    ~ImGuiRenderer();

    void Begin() const;
    void End();
    void OnEvent(Event& _event) const;

private:
    // ===========================================
    //  Event
    // ===========================================

    void OnSystemEvent_(const SystemEvent& _event) const;

    // ===========================================
    //  Renderer
    // ===========================================

    void InitRenderer_();
    void ShutdownRenderer_();
    void CreateFontTexture_();
    void ReserveBuffers_(uint32_t _numVertices, uint32_t _numIndices);
    void SetupRenderState_(const ImDrawData& _drawData, FrameBufferHandle _fbh) const;
    void BindTexture_(uint64_t _id) const;
    void RenderDrawData_(const ImDrawData& _drawData, FrameBufferHandle _fbh);

    static void RendererCreateWindow_(ImGuiViewport* _pViewport);
    static void RendererDestroyWindow_(ImGuiViewport* _pViewport);
    static void RendererSetWindowSize_(ImGuiViewport* _pViewport, ImVec2 _size);
    static void RendererRenderWindow_(ImGuiViewport* _pViewport, void* _pRenderArg);

    ImGuiContext*     m_pImguiCtx = nullptr;
    String            m_iniPath   = {};
    FrameBufferHandle m_fbh       = kNullHandle;

    // renderer
    VertexLayout         m_vl          = {};
    ProgramHandle        m_ph          = kNullHandle;
    ConstantBufferHandle m_frameCbh    = kNullHandle;   // b0: projection, linearize
    ConstantBufferHandle m_textureCbh  = kNullHandle;   // b1: texture view (type, mip, layer, face)
    TextureHandle        m_fontTexh    = kNullHandle;
    VertexBufferHandle   m_vbh         = kNullHandle;
    IndexBufferHandle    m_ibh         = kNullHandle;
    uint32_t             m_numVertices = 0;   // vertex buffer capacity
    uint32_t             m_numIndices  = 0;   // index buffer capacity
};

}   // namespace jug
