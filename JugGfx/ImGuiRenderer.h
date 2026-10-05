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
    void OnSystemEvent_(const SDL_Event& _event) const;

private:
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
    FrameBufferHandle m_fbh       = kInvalidHandle;

    VertexLayout         m_vl          = {};
    ProgramHandle        m_ph          = kInvalidHandle;
    ConstantBufferHandle m_frameCbh    = kInvalidHandle;
    ConstantBufferHandle m_textureCbh  = kInvalidHandle;
    TextureHandle        m_fontTexh    = kInvalidHandle;
    VertexBufferHandle   m_vbh         = kInvalidHandle;
    IndexBufferHandle    m_ibh         = kInvalidHandle;
    uint32_t             m_numVertices = 0;
    uint32_t             m_numIndices  = 0;
};

}   // namespace jug
