#pragma once
#include "Base.h"

struct ImDrawData;
struct ImGuiContext;
struct ImGuiViewport;
struct ImGuiPlatformImeData;
struct SDL_Cursor;
struct SDL_Window;

namespace jug
{

struct ImGuiTexture
{
    TextureHandle texh  = kNullHandle;
    uint32_t      mip   = 0;
    uint32_t      layer = 0;
    eCubeFace     face  = eCubeFace::PosX;
};

class ImGuiRenderer
{
    JUG_CLASS(ImGuiRenderer, NO_COPY, NO_MOVE)

public:
    explicit ImGuiRenderer(SDL_WindowID _wndID);
    ~ImGuiRenderer();

    void Begin();
    void End(FrameBufferHandle _fbh);
    void OnEvent(Event& _event) const;

private:
    // ===========================================
    //  Platform
    // ===========================================

    void InitPlatform_();
    void ShutdownPlatform_();
    void UpdateDisplay_() const;
    void UpdateMouseCursor_();

    [[nodiscard]] static const char* GetClipboardText_(ImGuiContext* _pContext);
    static void                      SetClipboardText_(ImGuiContext* _pContext, const char* _pText);
    static void                      SetImeData_(ImGuiContext* _pContext, ImGuiViewport* _pViewport, ImGuiPlatformImeData* _pData);

    // ===========================================
    //  Event
    // ===========================================

    void OnMouseMovedEvent_(const MouseMovedEvent& _event) const;
    void OnMouseButtonDownEvent_(const MouseButtonDownEvent& _event) const;
    void OnMouseButtonUpEvent_(const MouseButtonUpEvent& _event) const;
    void OnMouseWheelEvent_(const MouseWheelEvent& _event) const;
    void OnKeyDownEvent_(const KeyDownEvent& _event) const;
    void OnKeyUpEvent_(const KeyUpEvent& _event) const;
    void OnTextInputEvent_(const TextInputEvent& _event) const;
    void OnWindowMouseLeaveEvent_(const WindowMouseLeaveEvent& _event) const;
    void OnWindowFocusGainedEvent_(const WindowFocusGainedEvent& _event) const;
    void OnWindowFocusLostEvent_(const WindowFocusLostEvent& _event) const;

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

    static constexpr size_t kNumCursors = 11;   // ImGuiMouseCursor_COUNT

    ImGuiContext* m_pContext = nullptr;
    SDL_WindowID  m_wndID    = 0;

    // platform
    ARRAY<SDL_Cursor*, kNumCursors> m_pCursors             = {};
    SDL_Cursor*                     m_pLastCursorOrNull    = nullptr;
    SDL_Window*                     m_pImeWindowOrNull     = nullptr;
    char*                           m_pClipboardTextOrNull = nullptr;

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

// ===========================================
//  Texture Extensions
// ===========================================

[[nodiscard]] uint64_t     ToTextureID(TextureHandle _texh);
[[nodiscard]] uint64_t     ToTextureID(ImGuiTexture _texture);
[[nodiscard]] ImGuiTexture FromTextureID(uint64_t _id);

}   // namespace jug
