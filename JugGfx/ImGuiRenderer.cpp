#include "pch.h"
#include "ImGuiRenderer.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "Graphics.h"
#include "ImGuiImage.h"
#include "Shader.h"

namespace jug
{
namespace
{
    // ===========================================
    //  Shader
    // ===========================================

    constexpr StringView kShaderSource = R"(
      cbuffer CB_IMGUI : register(b0)
      {
          float4x4 g_proj;
          uint     g_bLinearize;
      };
      
      cbuffer CB_IMGUI_TEXTURE : register(b1)
      {
          uint  g_textureType; 
          float g_mip;
          float g_layer;      
          uint  g_face;
          uint  g_bTextureSrgb;
      };
      
      struct VS_INPUT
      {
          float2 pos : POSITION;
          float2 uv  : TEXCOORD0;
          uint   col : COLOR0;
      };
      
      struct PS_INPUT
      {
          float4 pos : SV_POSITION;
          float4 col : COLOR0;
          float2 uv  : TEXCOORD0;
      };
      
      Texture2D        t_texture2D        : register(t0);
      Texture2DArray   t_texture2DArray   : register(t1);
      TextureCube      t_textureCube      : register(t2);
      TextureCubeArray t_textureCubeArray : register(t3);
      Texture3D        t_texture3D        : register(t4);
      SamplerState     s_sampler          : register(s0);
      
      PS_INPUT VSMain(VS_INPUT _input)
      {
          PS_INPUT output;
          output.pos = mul(g_proj, float4(_input.pos, 0.f, 1.f));
          output.col = float4(_input.col & 0xFF, (_input.col >> 8) & 0xFF, (_input.col >> 16) & 0xFF, (_input.col >> 24) & 0xFF) / 255.f;
          output.uv  = _input.uv;
          return output;
      }
      
      float3 SrgbToLinear(float3 _c)
      {
          const float3 lo = _c / 12.92f;
          const float3 hi = pow(max((_c + 0.055f) / 1.055f, 0.f), 2.4f);
          return (_c <= 0.04045f) ? lo : hi;
      }
      
      float3 LinearToSrgb(float3 _c)
      {
          const float3 lo = _c * 12.92f;
          const float3 hi = 1.055f * pow(max(_c, 0.f), 1.f / 2.4f) - 0.055f;
          return (_c <= 0.0031308f) ? lo : hi;
      }
      
      float3 CubeFaceDirection(float2 _uv, uint _face)
      {
          const float2 st = _uv * 2.f - 1.f;
          switch (_face)
          {
              case 0: return float3(1.f, -st.y, -st.x);    // +X
              case 1: return float3(-1.f, -st.y, st.x);    // -X
              case 2: return float3(st.x, 1.f, st.y);      // +Y
              case 3: return float3(st.x, -1.f, -st.y);    // -Y
              case 4: return float3(st.x, -st.y, 1.f);     // +Z
              default: return float3(-st.x, -st.y, -1.f);  // -Z
          }
      }
      
      float4 SampleTexture(float2 _uv)
      {
          switch (g_textureType)
          {
              case 1:
              {
                  const float3 coord = float3(_uv, g_layer);
                  return t_texture2DArray.SampleLevel(s_sampler, coord, g_mip);
              }
              case 2:
              {
                  const float3 coord = CubeFaceDirection(_uv, g_face);
                  return t_textureCube.SampleLevel(s_sampler, coord, g_mip);
              }
              case 3:
              {
                  const float4 coord = float4(CubeFaceDirection(_uv, g_face), g_layer);
                  return t_textureCubeArray.SampleLevel(s_sampler, coord, g_mip);
              }
              case 4:
              {
                  const float3 coord = float3(_uv, g_layer);
                  return t_texture3D.SampleLevel(s_sampler, coord, g_mip);
              }
              default:
              {
                  return t_texture2D.SampleLevel(s_sampler, _uv, g_mip);
              }
          }
      }
      
      float4 PSMain(PS_INPUT _input) : SV_Target
      {
          float4 col = _input.col;
          float4 tex = SampleTexture(_input.uv);
      
          if (g_bLinearize != 0)
          {
              col.rgb = SrgbToLinear(col.rgb);
          }
          else if (g_bTextureSrgb != 0)
          {
              tex.rgb = LinearToSrgb(tex.rgb);  
          }
          return col * tex;
      }
)";

    struct CB_IMGUI
    {
        MATRIX   proj;
        uint32_t bLinearize;
        uint32_t padding[3];
    };

    struct CB_IMGUI_TEXTURE
    {
        uint32_t type;
        float    mip;
        float    layer;
        uint32_t face;
        uint32_t bTextureSRGB;
        uint32_t padding[3];
    };

    enum class eImGuiTexture
    {
        Texture2D,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture3D,
    };

    static_assert(sizeof(ImDrawVert) == 20, "ImGuiRenderer expects the default ImDrawVert layout (pos, uv, col).");
    static_assert(sizeof(ImDrawIdx) == 2 || sizeof(ImDrawIdx) == 4);

    constexpr uint32_t kVertexBufferSlack = 5000;
    constexpr uint32_t kIndexBufferSlack  = 10000;

    [[nodiscard]] ShaderHandle CompileShader_(
        const eShader    _type,
        const StringView _entryPoint)
    {
        ShaderCompileDesc desc = {};
        desc.type              = _type;
        desc.entryPoint        = _entryPoint;

        const Result<Shader> shader = Shader::Compile(kShaderSource, desc);
        if (!shader)
        {
            JUG_FATAL("ImGuiRenderer: failed to compile {}.", _entryPoint);
        }
        return Graphics::GetSingleton().CreateShader(shader->GetByteCode());
    }

    [[nodiscard]] SDL_Window* ToSdlWindow_(
        const ImGuiViewport* _pViewport)
    {
        return SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(_pViewport->PlatformHandle)));
    }

    [[nodiscard]] FrameBufferHandle ToFrameBufferHandle_(
        const ImGuiViewport* _pViewport)
    {
        return FrameBufferHandle { static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_pViewport->RendererUserData)) };
    }

}   // namespace

ImGuiRenderer::ImGuiRenderer(
    const SDL_WindowID _wndID,
    const FilePath&    _iniPathOrEmpty,
    const bool         _bEnableViewports)
    : m_iniPath(_iniPathOrEmpty.string())
{
    JUG_ASSERT(_wndID, "ImGuiRenderer requires a live window.");

    // init imgui context
    IMGUI_CHECKVERSION();
    m_pImguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_pImguiCtx);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = m_iniPath.empty() ? nullptr : m_iniPath.c_str();
    if (_bEnableViewports)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }

    // find framebuffer
    Graphics& gfx = Graphics::GetSingleton();
    m_fbh         = gfx.FindFrameBufferOrNull(_wndID);
    JUG_ASSERT(m_fbh, "ImGuiRenderer requires a framebuffer for the window.");

    // init backend
    if (!ImGui_ImplSDL3_InitForD3D(SDL_GetWindowFromID(_wndID)))
    {
        JUG_FATAL("ImGuiRenderer: ImGui_ImplSDL3_InitForD3D failed.");
    }
    InitRenderer_();
}

ImGuiRenderer::~ImGuiRenderer()
{
    ImGui::SetCurrentContext(m_pImguiCtx);
    ImGui::DestroyPlatformWindows();
    ShutdownRenderer_();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_pImguiCtx);
    m_pImguiCtx = nullptr;
}

void ImGuiRenderer::Begin() const
{
    ImGui::SetCurrentContext(m_pImguiCtx);
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiRenderer::End()
{
    ImGui::SetCurrentContext(m_pImguiCtx);
    ImGui::Render();
    RenderDrawData_(*ImGui::GetDrawData(), m_fbh);

    // 멀티 뷰포트 렌더링.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiRenderer::OnEvent(
    Event& _event) const
{
    ImGui::SetCurrentContext(m_pImguiCtx);

    EventDispatcher dispatcher { _event };
    dispatcher.Dispatch<SystemEvent>(this, &ImGuiRenderer::OnSystemEvent_);
}

void ImGuiRenderer::OnSystemEvent_(
    const SystemEvent& _event) const
{
    ImGui_ImplSDL3_ProcessEvent(&_event.GetEvent());
}

void ImGuiRenderer::InitRenderer_()
{
    Graphics& gfx = Graphics::GetSingleton();

    ImGuiIO& io                = ImGui::GetIO();
    io.BackendRendererName     = "ImGui.Impl.JugGfx";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

    ImGuiPlatformIO& platformIO       = ImGui::GetPlatformIO();
    platformIO.Renderer_CreateWindow  = &ImGuiRenderer::RendererCreateWindow_;
    platformIO.Renderer_DestroyWindow = &ImGuiRenderer::RendererDestroyWindow_;
    platformIO.Renderer_SetWindowSize = &ImGuiRenderer::RendererSetWindowSize_;
    platformIO.Renderer_RenderWindow  = &ImGuiRenderer::RendererRenderWindow_;

    m_vl.Add(eVertexAttribute::Position, eVertexAttributeFormat::Float, 2)
        .Add(eVertexAttribute::TexCoord0, eVertexAttributeFormat::Float, 2)
        .Add(eVertexAttribute::Color0, eVertexAttributeFormat::UInt, 1);

    JUG_ASSERT(m_vl.GetStride() == sizeof(ImDrawVert), "ImGuiRenderer vertex layout does not match ImDrawVert.");
    JUG_ASSERT(m_vl.Get(eVertexAttribute::TexCoord0).offset == offsetof(ImDrawVert, uv), "ImDrawVert::uv offset mismatch.");
    JUG_ASSERT(m_vl.Get(eVertexAttribute::Color0).offset == offsetof(ImDrawVert, col), "ImDrawVert::col offset mismatch.");

    const ShaderHandle vsh = CompileShader_(eShader::Vertex, "VSMain");
    const ShaderHandle psh = CompileShader_(eShader::Pixel, "PSMain");
    m_ph                   = gfx.CreateProgram(vsh, psh, true);

    m_frameCbh = gfx.CreateConstantBuffer(sizeof(CB_IMGUI));
    gfx.SetName(m_frameCbh, "ImGui.CB_IMGUI");

    m_textureCbh = gfx.CreateConstantBuffer(sizeof(CB_IMGUI_TEXTURE));
    gfx.SetName(m_textureCbh, "ImGui.CB_IMGUI_TEXTURE");

    CreateFontTexture_();
}

void ImGuiRenderer::ShutdownRenderer_()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->SetTexID(ImTextureID {});
    io.BackendRendererName     = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);

    JUG_GFX_DESTROY(m_fontTexh);
    JUG_GFX_DESTROY(m_vbh);
    JUG_GFX_DESTROY(m_ibh);
    JUG_GFX_DESTROY(m_frameCbh);
    JUG_GFX_DESTROY(m_textureCbh);
    JUG_GFX_DESTROY(m_ph);
    m_numVertices = 0;
    m_numIndices  = 0;
}

void ImGuiRenderer::CreateFontTexture_()
{
    ImGuiIO&  io  = ImGui::GetIO();
    Graphics& gfx = Graphics::GetSingleton();

    unsigned char* pPixels = nullptr;
    int            width   = 0;
    int            height  = 0;
    io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);

    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);

    // init data
    ARRAY<MemoryView, 1> data;
    data[0] = { pPixels, w * h * 4 };

    // create
    m_fontTexh = gfx.CreateTexture2D(w, h, eTextureFormat::RGBA8_UNorm, 1, eMSAA::None, eTextureOption::None, data);
    gfx.SetName(m_fontTexh, "ImGui.FontAtlas");

    // set id
    ImGuiTextureRef texture = m_fontTexh;
    io.Fonts->SetTexID(texture.ToImTextureID());
}

void ImGuiRenderer::ReserveBuffers_(
    const uint32_t _numVertices,
    const uint32_t _numIndices)
{
    Graphics& gfx = Graphics::GetSingleton();

    if (!m_vbh || m_numVertices < _numVertices)
    {
        JUG_GFX_DESTROY(m_vbh);
        m_numVertices = _numVertices + kVertexBufferSlack;
        m_vbh         = gfx.CreateDynamicVertexBuffer(m_numVertices, m_vl);
        gfx.SetName(m_vbh, "ImGui.VertexBuffer");
    }

    if (!m_ibh || m_numIndices < _numIndices)
    {
        JUG_GFX_DESTROY(m_ibh);
        m_numIndices = _numIndices + kIndexBufferSlack;
        m_ibh        = gfx.CreateDynamicIndexBuffer(m_numIndices, sizeof(ImDrawIdx) == 4);
        gfx.SetName(m_ibh, "ImGui.IndexBuffer");
    }
}

void ImGuiRenderer::SetupRenderState_(
    const ImDrawData&       _drawData,
    const FrameBufferHandle _fbh) const
{
    Graphics& gfx = Graphics::GetSingleton();

    gfx.SetFrameBuffer(_fbh);
    gfx.SetViewport(0.f, 0.f, _drawData.DisplaySize.x * _drawData.FramebufferScale.x, _drawData.DisplaySize.y * _drawData.FramebufferScale.y);

    gfx.SetProgram(m_ph);
    gfx.SetVertexBuffer(m_vbh);
    gfx.SetConstantBuffer(m_frameCbh, eShader::Vertex, 0);
    gfx.SetConstantBuffer(m_frameCbh, eShader::Pixel, 0);
    gfx.SetConstantBuffer(m_textureCbh, eShader::Pixel, 1);
    gfx.SetSampler({ eSampler::Filter_MinLinear_MagLinear_MipLinear, eSampler::U_Clamp, eSampler::V_Clamp, eSampler::W_Clamp }, eShader::Pixel, 0);

    gfx.SetRenderState({ eRenderState::Topology_TriangleList, eRenderState::Cull_None, eRenderState::Scissor });
    gfx.SetBlend({ eBlend::Enable, eBlend::Src_SrcAlpha, eBlend::Dst_InvSrcAlpha, eBlend::Op_Add, eBlend::SrcAlpha_One, eBlend::DstAlpha_InvSrcAlpha, eBlend::OpAlpha_Add, eBlend::Write_All });
    gfx.SetStencil(eStencil::None, eStencil::None);

    gfx.SetSubmitParam(eSubmitParam::StartIndexLocation, 0);
    gfx.SetSubmitParam(eSubmitParam::BaseVertexLocation, 0);
}

void ImGuiRenderer::BindTexture_(
    const uint64_t _id) const
{
    Graphics&             gfx     = Graphics::GetSingleton();
    const ImGuiTextureRef texture = _id;
    const TextureDesc&    desc    = gfx.GetDesc(texture.texh);

    JUG_ASSERT(texture.mip < desc.numMips, "ImGuiTextureRef::mip is out of range.");
    JUG_ASSERT(!desc.flags.Has(eTextureOption::Readback), "A readback texture cannot be drawn by ImGui.");

    CB_IMGUI_TEXTURE constants;
    constants.mip          = static_cast<float>(texture.mip);
    constants.face         = static_cast<uint32_t>(texture.face);
    constants.bTextureSRGB = IsSRGB(desc.format) ? 1u : 0u;

    eImGuiTexture type = eImGuiTexture::Texture2D;
    switch (desc.type)
    {
        case eTexture::Texture2D:
        {
            JUG_ASSERT(texture.layer < desc.numLayers, "ImGuiTextureRef::layer is out of range.");
            type            = desc.numLayers > 1 ? eImGuiTexture::Texture2DArray : eImGuiTexture::Texture2D;
            constants.layer = static_cast<float>(texture.layer);
        }
        break;

        case eTexture::TextureCube:
        {
            const uint32_t numCubes = desc.numLayers / 6;
            JUG_ASSERT(texture.layer < numCubes, "ImGuiTextureRef::layer (cube index) is out of range.");
            type            = numCubes > 1 ? eImGuiTexture::TextureCubeArray : eImGuiTexture::TextureCube;
            constants.layer = static_cast<float>(texture.layer);
        }
        break;

        case eTexture::Texture3D:
        {
            const uint32_t depth = CalcTextureSize(desc.width, desc.height, desc.depth, texture.mip).depth;
            JUG_ASSERT(texture.layer < depth, "ImGuiTextureRef::layer (depth slice) is out of range.");
            type            = eImGuiTexture::Texture3D;
            constants.layer = (static_cast<float>(texture.layer) + 0.5f) / static_cast<float>(depth);
        }
        break;
    }
    constants.type = static_cast<uint32_t>(type);

    // udpate
    gfx.UpdateBuffer(m_textureCbh, constants);

    // bind
    for (size_t slot = 0; slot < CountOf<eImGuiTexture>(); ++slot)
    {
        const TextureHandle texh = (slot == static_cast<size_t>(type)) ? texture.texh : kNullHandle;
        gfx.SetTexture(texh, eShader::Pixel, static_cast<uint32_t>(slot));
    }
}

void ImGuiRenderer::RenderDrawData_(
    const ImDrawData&       _drawData,
    const FrameBufferHandle _fbh)
{
    // 최소화 상태
    if (_drawData.DisplaySize.x <= 0.f || _drawData.DisplaySize.y <= 0.f)
    {
        return;
    }

    if (_drawData.TotalVtxCount <= 0 || _drawData.TotalIdxCount <= 0)
    {
        return;
    }

    Graphics& gfx = Graphics::GetSingleton();
    ReserveBuffers_(static_cast<uint32_t>(_drawData.TotalVtxCount), static_cast<uint32_t>(_drawData.TotalIdxCount));

    // upload
    {
        const MutableMemoryView vtxMem = gfx.MapBuffer(m_vbh);
        const MutableMemoryView idxMem = gfx.MapBuffer(m_ibh);

        ImDrawVert* pVtxDst = reinterpret_cast<ImDrawVert*>(vtxMem.GetPtr());
        ImDrawIdx*  pIdxDst = reinterpret_cast<ImDrawIdx*>(idxMem.GetPtr());
        for (const ImDrawList* pList: _drawData.CmdLists)
        {
            std::memcpy(pVtxDst, pList->VtxBuffer.Data, static_cast<size_t>(pList->VtxBuffer.Size) * sizeof(ImDrawVert));
            std::memcpy(pIdxDst, pList->IdxBuffer.Data, static_cast<size_t>(pList->IdxBuffer.Size) * sizeof(ImDrawIdx));
            pVtxDst += pList->VtxBuffer.Size;
            pIdxDst += pList->IdxBuffer.Size;
        }

        gfx.UnmapBuffer(m_vbh);
        gfx.UnmapBuffer(m_ibh);
    }

    // constants
    {
        CB_IMGUI constants;

        // proj
        const float l = _drawData.DisplayPos.x;
        const float r = _drawData.DisplayPos.x + _drawData.DisplaySize.x;
        const float t = _drawData.DisplayPos.y;
        const float b = _drawData.DisplayPos.y + _drawData.DisplaySize.y;

        constants.proj = {
            {     2.f / (r - l),               0.f,  0.f, 0.f },
            {               0.f,     2.f / (t - b),  0.f, 0.f },
            {               0.f,               0.f, 0.5f, 0.f },
            { (r + l) / (l - r), (t + b) / (b - t), 0.5f, 1.f }
        };

        // linearize
        const TextureHandle texh = gfx.GetDesc(_fbh).atts[0].texh;
        constants.bLinearize     = IsSRGB(gfx.GetDesc(texh).format) ? 1u : 0u;

        // update
        gfx.UpdateBuffer(m_frameCbh, constants);
    }

    SetupRenderState_(_drawData, _fbh);

    // draw
    const ImVec2 clipOffset = _drawData.DisplayPos;
    const ImVec2 clipScale  = _drawData.FramebufferScale;

    uint32_t    globalVtxOffset = 0;
    uint32_t    globalIdxOffset = 0;
    ImTextureID lastTexID       = ImTextureID {};   // 0 = 바인딩 없음
    for (const ImDrawList* pList: _drawData.CmdLists)
    {
        for (const ImDrawCmd& cmd: pList->CmdBuffer)
        {
            if (cmd.UserCallback)
            {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
                {
                    SetupRenderState_(_drawData, _fbh);
                }
                else
                {
                    cmd.UserCallback(pList, &cmd);
                }
                lastTexID = ImTextureID {};   // 콜백이 바인딩을 바꿨을 수 있음
                continue;
            }

            const ImVec2 clipMin { (cmd.ClipRect.x - clipOffset.x) * clipScale.x, (cmd.ClipRect.y - clipOffset.y) * clipScale.y };
            const ImVec2 clipMax { (cmd.ClipRect.z - clipOffset.x) * clipScale.x, (cmd.ClipRect.w - clipOffset.y) * clipScale.y };
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
            {
                continue;
            }

            gfx.SetScissor(static_cast<int>(clipMin.x), static_cast<int>(clipMin.y), static_cast<int>(clipMax.x - clipMin.x), static_cast<int>(clipMax.y - clipMin.y));
            if (cmd.GetTexID() != lastTexID)
            {
                BindTexture_(cmd.GetTexID());
                lastTexID = cmd.GetTexID();
            }
            gfx.SetIndexBuffer(m_ibh, globalIdxOffset + cmd.IdxOffset, cmd.ElemCount);
            gfx.SetSubmitParam(eSubmitParam::BaseVertexLocation, globalVtxOffset + cmd.VtxOffset);
            gfx.Submit();
        }

        globalVtxOffset += static_cast<uint32_t>(pList->VtxBuffer.Size);
        globalIdxOffset += static_cast<uint32_t>(pList->IdxBuffer.Size);
    }

    gfx.SetSubmitParam(eSubmitParam::BaseVertexLocation, 0);
}

void ImGuiRenderer::RendererCreateWindow_(
    ImGuiViewport* _pViewport)
{
    const ImGuiRenderer* pThis = static_cast<const ImGuiRenderer*>(ImGui::GetIO().BackendRendererUserData);
    JUG_ASSERT(pThis->m_fbh, "ImGuiRenderer: a viewport was created before the first End().");

    Graphics&            gfx     = Graphics::GetSingleton();
    const eTextureFormat format  = gfx.GetDesc(gfx.GetDesc(pThis->m_fbh).atts[0].texh).format;
    SDL_Window*          pWindow = ToSdlWindow_(_pViewport);

    int width  = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(pWindow, &width, &height);

    const uint32_t          w   = static_cast<uint32_t>(width);
    const uint32_t          h   = static_cast<uint32_t>(height);
    const FrameBufferHandle fbh = gfx.CreateFrameBuffer(SDL_GetWindowID(pWindow), w, h, format, 2);
    gfx.SetVSync(fbh, false);   // 창마다 vsync 로 Present 하면 창 수만큼 vblank 를 기다린다. 메인만 동기화.
    gfx.SetName(fbh, "ImGui.Viewport");

    _pViewport->RendererUserData = reinterpret_cast<void*>(static_cast<uintptr_t>(fbh.GetValue()));
}

void ImGuiRenderer::RendererDestroyWindow_(
    ImGuiViewport* _pViewport)
{
    if (_pViewport != ImGui::GetMainViewport())
    {
        Graphics::GetSingleton().Destroy(ToFrameBufferHandle_(_pViewport));
        _pViewport->RendererUserData = nullptr;
    }
}

void ImGuiRenderer::RendererSetWindowSize_(
    ImGuiViewport* _pViewport,   // NOLINT
    ImVec2)
{
    int width  = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(ToSdlWindow_(_pViewport), &width, &height);

    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);
    Graphics::GetSingleton().ResizeFrameBuffer(ToFrameBufferHandle_(_pViewport), w, h);
}

void ImGuiRenderer::RendererRenderWindow_(
    ImGuiViewport* _pViewport,   // NOLINT
    void*)
{
    ImGuiRenderer*          pThis = static_cast<ImGuiRenderer*>(ImGui::GetIO().BackendRendererUserData);
    const FrameBufferHandle fbh   = ToFrameBufferHandle_(_pViewport);
    if (!(_pViewport->Flags & ImGuiViewportFlags_NoRendererClear))
    {
        Graphics::GetSingleton().ClearRenderTarget(fbh, RGBA { 0, 0, 0, 255 });
    }
    pThis->RenderDrawData_(*_pViewport->DrawData, fbh);
}

}   // namespace jug
