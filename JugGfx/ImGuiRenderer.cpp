#include "pch.h"
#include "ImGuiRenderer.h"

#include "Graphics.h"
#include "Shader.h"

#include <imgui.h>

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
    uint  g_textureType;   // 0: 2D, 1: 2D array, 2: cube, 3: cube array, 4: 3D
    float g_mip;
    float g_layer;         // array slice / cube index / 3D w coord
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

// uv [0, 1] -> cube face direction (D3D cube map convention)
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
        col.rgb = SrgbToLinear(col.rgb);   // sRGB 타겟: 정점 색만 선형화 (텍스처는 이미 선형)
    }
    else if (g_bTextureSrgb != 0)
    {
        tex.rgb = LinearToSrgb(tex.rgb);   // UNorm 타겟 + sRGB 텍스처: 샘플 결과(선형)를 다시 인코딩
    }
    return col * tex;
}
)";

    struct CB_IMGUI
    {
        float    proj[4][4];
        uint32_t bLinearize;
        uint32_t padding[3];
    };

    struct CB_IMGUI_TEXTURE
    {
        uint32_t textureType;
        float    mip;
        float    layer;
        uint32_t face;
        uint32_t bTextureSrgb;
        uint32_t padding[3];
    };

    // HLSL g_textureType / shader resource slot
    enum class eImGuiTextureType : uint32_t
    {
        Texture2D,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture3D,
        Count,
    };

    // ===========================================
    //  Texture ID packing
    // ===========================================

    constexpr uint32_t kTexIdMipShift   = 32;
    constexpr uint32_t kTexIdFaceShift  = 37;
    constexpr uint32_t kTexIdLayerShift = 40;
    constexpr uint64_t kTexIdMipMask    = (1ull << 5) - 1;
    constexpr uint64_t kTexIdFaceMask   = (1ull << 3) - 1;
    constexpr uint64_t kTexIdLayerMask  = (1ull << 23) - 1;
    constexpr uint64_t kTexIdValidBit   = 1ull << 63;

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
        return Graphics::GetInstance()->CreateShader(shader->GetByteCode());
    }

    // ===========================================
    //  Input Mapping
    // ===========================================

    [[nodiscard]] ImGuiKey ToImGuiKey_(
        const eKey _key)
    {
        switch (_key)
        {
            case eKey::A: return ImGuiKey_A;
            case eKey::B: return ImGuiKey_B;
            case eKey::C: return ImGuiKey_C;
            case eKey::D: return ImGuiKey_D;
            case eKey::E: return ImGuiKey_E;
            case eKey::F: return ImGuiKey_F;
            case eKey::G: return ImGuiKey_G;
            case eKey::H: return ImGuiKey_H;
            case eKey::I: return ImGuiKey_I;
            case eKey::J: return ImGuiKey_J;
            case eKey::K: return ImGuiKey_K;
            case eKey::L: return ImGuiKey_L;
            case eKey::M: return ImGuiKey_M;
            case eKey::N: return ImGuiKey_N;
            case eKey::O: return ImGuiKey_O;
            case eKey::P: return ImGuiKey_P;
            case eKey::Q: return ImGuiKey_Q;
            case eKey::R: return ImGuiKey_R;
            case eKey::S: return ImGuiKey_S;
            case eKey::T: return ImGuiKey_T;
            case eKey::U: return ImGuiKey_U;
            case eKey::V: return ImGuiKey_V;
            case eKey::W: return ImGuiKey_W;
            case eKey::X: return ImGuiKey_X;
            case eKey::Y: return ImGuiKey_Y;
            case eKey::Z: return ImGuiKey_Z;

            case eKey::Num1: return ImGuiKey_1;
            case eKey::Num2: return ImGuiKey_2;
            case eKey::Num3: return ImGuiKey_3;
            case eKey::Num4: return ImGuiKey_4;
            case eKey::Num5: return ImGuiKey_5;
            case eKey::Num6: return ImGuiKey_6;
            case eKey::Num7: return ImGuiKey_7;
            case eKey::Num8: return ImGuiKey_8;
            case eKey::Num9: return ImGuiKey_9;
            case eKey::Num0: return ImGuiKey_0;

            case eKey::Return: return ImGuiKey_Enter;
            case eKey::Escape: return ImGuiKey_Escape;
            case eKey::Backspace: return ImGuiKey_Backspace;
            case eKey::Tab: return ImGuiKey_Tab;
            case eKey::Space: return ImGuiKey_Space;

            case eKey::Minus: return ImGuiKey_Minus;
            case eKey::Equals: return ImGuiKey_Equal;
            case eKey::LeftBracket: return ImGuiKey_LeftBracket;
            case eKey::RightBracket: return ImGuiKey_RightBracket;
            case eKey::Backslash: return ImGuiKey_Backslash;
            case eKey::Semicolon: return ImGuiKey_Semicolon;
            case eKey::Apostrophe: return ImGuiKey_Apostrophe;
            case eKey::Grave: return ImGuiKey_GraveAccent;
            case eKey::Comma: return ImGuiKey_Comma;
            case eKey::Period: return ImGuiKey_Period;
            case eKey::Slash: return ImGuiKey_Slash;

            case eKey::CapsLock: return ImGuiKey_CapsLock;

            case eKey::F1: return ImGuiKey_F1;
            case eKey::F2: return ImGuiKey_F2;
            case eKey::F3: return ImGuiKey_F3;
            case eKey::F4: return ImGuiKey_F4;
            case eKey::F5: return ImGuiKey_F5;
            case eKey::F6: return ImGuiKey_F6;
            case eKey::F7: return ImGuiKey_F7;
            case eKey::F8: return ImGuiKey_F8;
            case eKey::F9: return ImGuiKey_F9;
            case eKey::F10: return ImGuiKey_F10;
            case eKey::F11: return ImGuiKey_F11;
            case eKey::F12: return ImGuiKey_F12;
            case eKey::F13: return ImGuiKey_F13;
            case eKey::F14: return ImGuiKey_F14;
            case eKey::F15: return ImGuiKey_F15;
            case eKey::F16: return ImGuiKey_F16;
            case eKey::F17: return ImGuiKey_F17;
            case eKey::F18: return ImGuiKey_F18;
            case eKey::F19: return ImGuiKey_F19;
            case eKey::F20: return ImGuiKey_F20;
            case eKey::F21: return ImGuiKey_F21;
            case eKey::F22: return ImGuiKey_F22;
            case eKey::F23: return ImGuiKey_F23;
            case eKey::F24: return ImGuiKey_F24;

            case eKey::PrintScreen: return ImGuiKey_PrintScreen;
            case eKey::ScrollLock: return ImGuiKey_ScrollLock;
            case eKey::Pause: return ImGuiKey_Pause;
            case eKey::Insert: return ImGuiKey_Insert;
            case eKey::Home: return ImGuiKey_Home;
            case eKey::PageUp: return ImGuiKey_PageUp;
            case eKey::Delete: return ImGuiKey_Delete;
            case eKey::End: return ImGuiKey_End;
            case eKey::PageDown: return ImGuiKey_PageDown;
            case eKey::Right: return ImGuiKey_RightArrow;
            case eKey::Left: return ImGuiKey_LeftArrow;
            case eKey::Down: return ImGuiKey_DownArrow;
            case eKey::Up: return ImGuiKey_UpArrow;

            case eKey::NumLock: return ImGuiKey_NumLock;
            case eKey::KpDivide: return ImGuiKey_KeypadDivide;
            case eKey::KpMultiply: return ImGuiKey_KeypadMultiply;
            case eKey::KpMinus: return ImGuiKey_KeypadSubtract;
            case eKey::KpPlus: return ImGuiKey_KeypadAdd;
            case eKey::KpEnter: return ImGuiKey_KeypadEnter;
            case eKey::Kp1: return ImGuiKey_Keypad1;
            case eKey::Kp2: return ImGuiKey_Keypad2;
            case eKey::Kp3: return ImGuiKey_Keypad3;
            case eKey::Kp4: return ImGuiKey_Keypad4;
            case eKey::Kp5: return ImGuiKey_Keypad5;
            case eKey::Kp6: return ImGuiKey_Keypad6;
            case eKey::Kp7: return ImGuiKey_Keypad7;
            case eKey::Kp8: return ImGuiKey_Keypad8;
            case eKey::Kp9: return ImGuiKey_Keypad9;
            case eKey::Kp0: return ImGuiKey_Keypad0;
            case eKey::KpPeriod: return ImGuiKey_KeypadDecimal;
            case eKey::KpEquals: return ImGuiKey_KeypadEqual;

            case eKey::Application: return ImGuiKey_Menu;
            case eKey::Menu: return ImGuiKey_Menu;

            case eKey::LCtrl: return ImGuiKey_LeftCtrl;
            case eKey::LShift: return ImGuiKey_LeftShift;
            case eKey::LAlt: return ImGuiKey_LeftAlt;
            case eKey::LGui: return ImGuiKey_LeftSuper;
            case eKey::RCtrl: return ImGuiKey_RightCtrl;
            case eKey::RShift: return ImGuiKey_RightShift;
            case eKey::RAlt: return ImGuiKey_RightAlt;
            case eKey::RGui: return ImGuiKey_RightSuper;

            default: return ImGuiKey_None;
        }
    }

    [[nodiscard]] int ToImGuiMouseButton_(
        const eMouse _button)
    {
        switch (_button)
        {
            case eMouse::Left: return ImGuiMouseButton_Left;
            case eMouse::Right: return ImGuiMouseButton_Right;
            case eMouse::Middle: return ImGuiMouseButton_Middle;
            case eMouse::X1: return 3;
            case eMouse::X2: return 4;
            default: return -1;
        }
    }

    void AddKeyModsEvent_(
        ImGuiIO&             _io,
        const Flags<eKeyMod> _mods)
    {
        _io.AddKeyEvent(ImGuiMod_Ctrl, _mods.Has(eKeyMod::Ctrl));
        _io.AddKeyEvent(ImGuiMod_Shift, _mods.Has(eKeyMod::Shift));
        _io.AddKeyEvent(ImGuiMod_Alt, _mods.Has(eKeyMod::Alt));
        _io.AddKeyEvent(ImGuiMod_Super, _mods.Has(eKeyMod::Gui));
    }

    void AddKeyEvent_(
        ImGuiIO&             _io,
        const eKey           _key,
        const Flags<eKeyMod> _mods,
        const bool           _bDown)
    {
        AddKeyModsEvent_(_io, _mods);

        const ImGuiKey key = ToImGuiKey_(_key);
        _io.AddKeyEvent(key, _bDown);
        _io.SetKeyEventNativeData(key, 0, static_cast<int>(_key));
    }

}   // namespace

// ===========================================
//  ImGuiRenderer
// ===========================================

ImGuiRenderer::ImGuiRenderer(
    const SDL_WindowID _wndID)
    : m_wndID(_wndID)
{
    JUG_ASSERT(_wndID, "ImGuiRenderer requires a live window.");

    IMGUI_CHECKVERSION();
    m_pContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_pContext);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    InitPlatform_();
    InitRenderer_();
}

ImGuiRenderer::~ImGuiRenderer()
{
    ImGui::SetCurrentContext(m_pContext);
    ShutdownRenderer_();
    ShutdownPlatform_();
    ImGui::DestroyContext(m_pContext);
    m_pContext = nullptr;
}

void ImGuiRenderer::Begin()
{
    ImGui::SetCurrentContext(m_pContext);

    ImGuiIO& io  = ImGui::GetIO();
    io.DeltaTime = App::GetInstance()->GetDeltaTimeSec();

    UpdateDisplay_();
    UpdateMouseCursor_();

    ImGui::NewFrame();
}

void ImGuiRenderer::End(
    const FrameBufferHandle _fbh)
{
    JUG_ASSERT(_fbh, "ImGuiRenderer::End requires a target frame buffer.");

    ImGui::SetCurrentContext(m_pContext);
    ImGui::Render();
    RenderDrawData_(*ImGui::GetDrawData(), _fbh);
}

void ImGuiRenderer::OnEvent(
    Event& _event) const
{
    ImGui::SetCurrentContext(m_pContext);

    EventDispatcher dispatcher { _event };
    dispatcher.Dispatch<MouseMovedEvent>(this, &ImGuiRenderer::OnMouseMovedEvent_);
    dispatcher.Dispatch<MouseButtonDownEvent>(this, &ImGuiRenderer::OnMouseButtonDownEvent_);
    dispatcher.Dispatch<MouseButtonUpEvent>(this, &ImGuiRenderer::OnMouseButtonUpEvent_);
    dispatcher.Dispatch<MouseWheelEvent>(this, &ImGuiRenderer::OnMouseWheelEvent_);
    dispatcher.Dispatch<KeyDownEvent>(this, &ImGuiRenderer::OnKeyDownEvent_);
    dispatcher.Dispatch<KeyUpEvent>(this, &ImGuiRenderer::OnKeyUpEvent_);
    dispatcher.Dispatch<TextInputEvent>(this, &ImGuiRenderer::OnTextInputEvent_);
    dispatcher.Dispatch<WindowMouseLeaveEvent>(this, &ImGuiRenderer::OnWindowMouseLeaveEvent_);
    dispatcher.Dispatch<WindowFocusGainedEvent>(this, &ImGuiRenderer::OnWindowFocusGainedEvent_);
    dispatcher.Dispatch<WindowFocusLostEvent>(this, &ImGuiRenderer::OnWindowFocusLostEvent_);
}

uint64_t ToTextureID(
    const TextureHandle _texh)
{
    ImGuiTexture texture = {};
    texture.texh         = _texh;
    return ToTextureID(texture);
}

uint64_t ToTextureID(
    ImGuiTexture _texture)
{
    static_assert(sizeof(ImTextureID) >= sizeof(uint64_t), "ImGuiRenderer requires a 64-bit ImTextureID.");
    JUG_ASSERT(_texture.texh, "ToTextureID requires a valid texture handle.");

    const uint64_t mip = _texture.mip;
    JUG_ASSERT(mip <= kTexIdMipMask, "ImGuiTexture::mip is out of the packable range.");
    JUG_ASSERT(_texture.layer <= kTexIdLayerMask, "ImGuiTexture::layer is out of the packable range.");

    uint64_t id = kTexIdValidBit;
    id |= static_cast<uint64_t>(_texture.texh.GetValue());
    id |= (mip & kTexIdMipMask) << kTexIdMipShift;
    id |= (static_cast<uint64_t>(_texture.face) & kTexIdFaceMask) << kTexIdFaceShift;
    id |= (static_cast<uint64_t>(_texture.layer) & kTexIdLayerMask) << kTexIdLayerShift;
    return id;
}

ImGuiTexture FromTextureID(
    const uint64_t _id)
{
    JUG_ASSERT(_id & kTexIdValidBit, "FromTextureID requires an id made by ToTextureID.");

    ImGuiTexture texture = {};
    texture.texh         = TextureHandle { static_cast<uint32_t>(_id) };
    texture.mip          = (_id >> kTexIdMipShift) & kTexIdMipMask;
    texture.face         = static_cast<eCubeFace>((_id >> kTexIdFaceShift) & kTexIdFaceMask);
    texture.layer        = static_cast<uint32_t>((_id >> kTexIdLayerShift) & kTexIdLayerMask);
    return texture;
}

// ===========================================
//  Event
// ===========================================

void ImGuiRenderer::OnMouseMovedEvent_(
    const MouseMovedEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(_event.GetX(), _event.GetY());
}

void ImGuiRenderer::OnMouseButtonDownEvent_(
    const MouseButtonDownEvent& _event) const
{
    const int button = ToImGuiMouseButton_(_event.GetButton());
    if (_event.GetWindow() != m_wndID || button < 0)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseButtonEvent(button, true);
}

void ImGuiRenderer::OnMouseButtonUpEvent_(
    const MouseButtonUpEvent& _event) const
{
    const int button = ToImGuiMouseButton_(_event.GetButton());
    if (_event.GetWindow() != m_wndID || button < 0)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseButtonEvent(button, false);
}

void ImGuiRenderer::OnMouseWheelEvent_(
    const MouseWheelEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseWheelEvent(-_event.GetScrollX(), _event.GetScrollY());
}

void ImGuiRenderer::OnKeyDownEvent_(
    const KeyDownEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    AddKeyEvent_(ImGui::GetIO(), _event.GetKey(), _event.GetMods(), true);
}

void ImGuiRenderer::OnKeyUpEvent_(
    const KeyUpEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    AddKeyEvent_(ImGui::GetIO(), _event.GetKey(), _event.GetMods(), false);
}

void ImGuiRenderer::OnTextInputEvent_(
    const TextInputEvent& _event) const
{
    if (_event.GetWindow() != m_wndID || _event.GetText().empty())
    {
        return;
    }

    const String text { _event.GetText() };
    ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
}

void ImGuiRenderer::OnWindowMouseLeaveEvent_(
    const WindowMouseLeaveEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

void ImGuiRenderer::OnWindowFocusGainedEvent_(
    const WindowFocusGainedEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    ImGui::GetIO().AddFocusEvent(true);
}

void ImGuiRenderer::OnWindowFocusLostEvent_(
    const WindowFocusLostEvent& _event) const
{
    if (_event.GetWindow() != m_wndID)
    {
        return;
    }

    ImGui::GetIO().AddFocusEvent(false);
}

// ===========================================
//  Platform
// ===========================================

void ImGuiRenderer::InitPlatform_()
{
    static_assert(kNumCursors == static_cast<size_t>(ImGuiMouseCursor_COUNT), "ImGuiMouseCursor_COUNT changed.");

    ImGuiIO& io                = ImGui::GetIO();
    io.BackendPlatformName     = "imgui_impl_jugx";
    io.BackendPlatformUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    ImGuiPlatformIO& platformIO            = ImGui::GetPlatformIO();
    platformIO.Platform_GetClipboardTextFn = &ImGuiRenderer::GetClipboardText_;
    platformIO.Platform_SetClipboardTextFn = &ImGuiRenderer::SetClipboardText_;
    platformIO.Platform_SetImeDataFn       = &ImGuiRenderer::SetImeData_;

    ImGuiViewport* pMainViewport     = ImGui::GetMainViewport();
    pMainViewport->PlatformHandle    = reinterpret_cast<void*>(static_cast<uintptr_t>(m_wndID));
    pMainViewport->PlatformHandleRaw = GetNativeWindowHandle(m_wndID);

    m_pCursors[ImGuiMouseCursor_Arrow]      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    m_pCursors[ImGuiMouseCursor_TextInput]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    m_pCursors[ImGuiMouseCursor_ResizeAll]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    m_pCursors[ImGuiMouseCursor_ResizeNS]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    m_pCursors[ImGuiMouseCursor_ResizeEW]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    m_pCursors[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
    m_pCursors[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    m_pCursors[ImGuiMouseCursor_Hand]       = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    m_pCursors[ImGuiMouseCursor_Wait]       = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    m_pCursors[ImGuiMouseCursor_Progress]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_PROGRESS);
    m_pCursors[ImGuiMouseCursor_NotAllowed] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);
}

void ImGuiRenderer::ShutdownPlatform_()
{
    if (m_pImeWindowOrNull)
    {
        SDL_StopTextInput(m_pImeWindowOrNull);
        m_pImeWindowOrNull = nullptr;
    }

    if (m_pClipboardTextOrNull)
    {
        SDL_free(m_pClipboardTextOrNull);
        m_pClipboardTextOrNull = nullptr;
    }

    for (SDL_Cursor*& pCursor: m_pCursors)
    {
        if (pCursor)
        {
            SDL_DestroyCursor(pCursor);
            pCursor = nullptr;
        }
    }
    m_pLastCursorOrNull = nullptr;

    ImGuiIO& io                = ImGui::GetIO();
    io.BackendPlatformName     = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;

    ImGuiPlatformIO& platformIO            = ImGui::GetPlatformIO();
    platformIO.Platform_GetClipboardTextFn = nullptr;
    platformIO.Platform_SetClipboardTextFn = nullptr;
    platformIO.Platform_SetImeDataFn       = nullptr;
}

void ImGuiRenderer::UpdateDisplay_() const
{
    ImGuiIO& io = ImGui::GetIO();

    SDL_Window* pWindow = SDL_GetWindowFromID(m_wndID);
    if (!pWindow || (SDL_GetWindowFlags(pWindow) & SDL_WINDOW_MINIMIZED))
    {
        io.DisplaySize = ImVec2 { 0.f, 0.f };
        return;
    }

    int width       = 0;
    int height      = 0;
    int pixelWidth  = 0;
    int pixelHeight = 0;
    SDL_GetWindowSize(pWindow, &width, &height);
    SDL_GetWindowSizeInPixels(pWindow, &pixelWidth, &pixelHeight);

    io.DisplaySize = ImVec2 { static_cast<float>(width), static_cast<float>(height) };
    if (width > 0 && height > 0)
    {
        io.DisplayFramebufferScale = ImVec2 { static_cast<float>(pixelWidth) / static_cast<float>(width), static_cast<float>(pixelHeight) / static_cast<float>(height) };
    }
}

void ImGuiRenderer::UpdateMouseCursor_()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
    {
        return;
    }

    const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor || cursor == ImGuiMouseCursor_None)
    {
        SDL_HideCursor();
        return;
    }

    SDL_Cursor* pExpected = m_pCursors[cursor] ? m_pCursors[cursor] : m_pCursors[ImGuiMouseCursor_Arrow];
    if (m_pLastCursorOrNull != pExpected)
    {
        SDL_SetCursor(pExpected);
        m_pLastCursorOrNull = pExpected;
    }
    SDL_ShowCursor();
}

const char* ImGuiRenderer::GetClipboardText_(
    ImGuiContext*)
{
    ImGuiRenderer* pThis = static_cast<ImGuiRenderer*>(ImGui::GetIO().BackendPlatformUserData);
    if (pThis->m_pClipboardTextOrNull)
    {
        SDL_free(pThis->m_pClipboardTextOrNull);
    }
    pThis->m_pClipboardTextOrNull = SDL_GetClipboardText();   // SDL3: caller owns the returned string.
    return pThis->m_pClipboardTextOrNull;
}

void ImGuiRenderer::SetClipboardText_(
    ImGuiContext*,
    const char* _pText)
{
    SDL_SetClipboardText(_pText);
}

void ImGuiRenderer::SetImeData_(
    ImGuiContext*,
    ImGuiViewport*        _pViewport,
    ImGuiPlatformImeData* _pData)
{
    ImGuiRenderer*     pThis   = static_cast<ImGuiRenderer*>(ImGui::GetIO().BackendPlatformUserData);
    const SDL_WindowID wndID   = static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(_pViewport->PlatformHandle));
    SDL_Window*        pWindow = SDL_GetWindowFromID(wndID);

    if ((!_pData->WantVisible || pThis->m_pImeWindowOrNull != pWindow) && pThis->m_pImeWindowOrNull)
    {
        SDL_StopTextInput(pThis->m_pImeWindowOrNull);
        pThis->m_pImeWindowOrNull = nullptr;
    }

    if (_pData->WantVisible && pWindow)
    {
        SDL_Rect rect;
        rect.x = static_cast<int>(_pData->InputPos.x - _pViewport->Pos.x);
        rect.y = static_cast<int>(_pData->InputPos.y - _pViewport->Pos.y + _pData->InputLineHeight);
        rect.w = 1;
        rect.h = static_cast<int>(_pData->InputLineHeight);
        SDL_SetTextInputArea(pWindow, &rect, 0);
        SDL_StartTextInput(pWindow);
        pThis->m_pImeWindowOrNull = pWindow;
    }
}

// ===========================================
//  Renderer
// ===========================================

void ImGuiRenderer::InitRenderer_()
{
    Graphics* pGfx = Graphics::GetInstance();

    ImGuiIO& io            = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_juggfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    // ImDrawVert { ImVec2 pos; ImVec2 uv; ImU32 col; } - col 은 R32_UINT 로 받아 VS 에서 unpack.
    m_vl.Add(eVertexAttribute::Position, eVertexAttributeFormat::Float, 2)
        .Add(eVertexAttribute::TexCoord0, eVertexAttributeFormat::Float, 2)
        .Add(eVertexAttribute::Color0, eVertexAttributeFormat::UInt, 1);
    JUG_ASSERT(m_vl.GetStride() == sizeof(ImDrawVert), "ImGuiRenderer vertex layout does not match ImDrawVert.");
    JUG_ASSERT(m_vl.Get(eVertexAttribute::TexCoord0).offset == offsetof(ImDrawVert, uv), "ImDrawVert::uv offset mismatch.");
    JUG_ASSERT(m_vl.Get(eVertexAttribute::Color0).offset == offsetof(ImDrawVert, col), "ImDrawVert::col offset mismatch.");

    const ShaderHandle vsh = CompileShader_(eShader::Vertex, "VSMain");
    const ShaderHandle psh = CompileShader_(eShader::Pixel, "PSMain");
    m_ph                   = pGfx->CreateProgram(vsh, psh, true);

    m_frameCbh = pGfx->CreateConstantBuffer(sizeof(CB_IMGUI));
    pGfx->SetName(m_frameCbh, "ImGui.CB_IMGUI");

    m_textureCbh = pGfx->CreateConstantBuffer(sizeof(CB_IMGUI_TEXTURE));
    pGfx->SetName(m_textureCbh, "ImGui.CB_IMGUI_TEXTURE");

    CreateFontTexture_();
}

void ImGuiRenderer::ShutdownRenderer_()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->SetTexID(ImTextureID {});
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;

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
    ImGuiIO& io = ImGui::GetIO();

    unsigned char* pPixels = nullptr;
    int            width   = 0;
    int            height  = 0;
    io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);

    const MemoryView data { pPixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4 };
    m_fontTexh = Graphics::GetInstance()->CreateTexture2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height), eTextureFormat::RGBA8_UNorm, 1, eMSAA::None, eTextureOption::None, Span<const MemoryView> { &data, 1 });
    Graphics::GetInstance()->SetName(m_fontTexh, "ImGui.FontAtlas");

    io.Fonts->SetTexID(ToTextureID(m_fontTexh));
}

void ImGuiRenderer::ReserveBuffers_(
    const uint32_t _numVertices,
    const uint32_t _numIndices)
{
    Graphics* pGfx = Graphics::GetInstance();

    if (!m_vbh || m_numVertices < _numVertices)
    {
        JUG_GFX_DESTROY(m_vbh);
        m_numVertices = _numVertices + kVertexBufferSlack;
        m_vbh         = pGfx->CreateDynamicVertexBuffer(m_numVertices, m_vl);
        pGfx->SetName(m_vbh, "ImGui.VertexBuffer");
    }

    if (!m_ibh || m_numIndices < _numIndices)
    {
        JUG_GFX_DESTROY(m_ibh);
        m_numIndices = _numIndices + kIndexBufferSlack;
        m_ibh        = pGfx->CreateDynamicIndexBuffer(m_numIndices, sizeof(ImDrawIdx) == 4);
        pGfx->SetName(m_ibh, "ImGui.IndexBuffer");
    }
}

void ImGuiRenderer::SetupRenderState_(
    const ImDrawData&       _drawData,
    const FrameBufferHandle _fbh) const
{
    Graphics* pGfx = Graphics::GetInstance();

    pGfx->SetFrameBuffer(_fbh);
    pGfx->SetViewport(0.f, 0.f, _drawData.DisplaySize.x * _drawData.FramebufferScale.x, _drawData.DisplaySize.y * _drawData.FramebufferScale.y);

    pGfx->SetProgram(m_ph);
    pGfx->SetVertexBuffer(m_vbh);
    pGfx->SetConstantBuffer(m_frameCbh, eShader::Vertex, 0);
    pGfx->SetConstantBuffer(m_frameCbh, eShader::Pixel, 0);
    pGfx->SetConstantBuffer(m_textureCbh, eShader::Pixel, 1);
    pGfx->SetSampler({ eSampler::Filter_MinLinear_MagLinear_MipLinear, eSampler::U_Clamp, eSampler::V_Clamp, eSampler::W_Clamp }, eShader::Pixel, 0);

    pGfx->SetRenderState({ eRenderState::Topology_TriangleList, eRenderState::Cull_None, eRenderState::Scissor });
    pGfx->SetBlend({ eBlend::Enable,
                     eBlend::Src_SrcAlpha,
                     eBlend::Dst_InvSrcAlpha,
                     eBlend::Op_Add,
                     eBlend::SrcAlpha_One,
                     eBlend::DstAlpha_InvSrcAlpha,
                     eBlend::OpAlpha_Add,
                     eBlend::Write_All });
    pGfx->SetStencil(eStencil::None, eStencil::None);

    pGfx->SetSubmitParam(eSubmitParam::StartIndexLocation, 0);
    pGfx->SetSubmitParam(eSubmitParam::BaseVertexLocation, 0);
}

void ImGuiRenderer::BindTexture_(
    const uint64_t _id) const
{
    Graphics*          pGfx    = Graphics::GetInstance();
    const ImGuiTexture texture = FromTextureID(_id);
    const TextureDesc& desc    = pGfx->GetDesc(texture.texh);

    JUG_ASSERT(texture.mip < desc.numMips, "ImGuiTexture::mip is out of range.");
    JUG_ASSERT(!desc.flags.Has(eTextureOption::Readback), "A readback texture cannot be drawn by ImGui.");

    CB_IMGUI_TEXTURE constants = {};
    constants.mip              = static_cast<float>(texture.mip);
    constants.face             = static_cast<uint32_t>(texture.face);
    constants.bTextureSrgb     = IsSRGB(desc.format) ? 1u : 0u;

    eImGuiTextureType type = eImGuiTextureType::Texture2D;
    switch (desc.type)
    {
        case eTexture::Texture2D:
        {
            JUG_ASSERT(texture.layer < desc.numLayers, "ImGuiTexture::layer is out of range.");
            type            = desc.numLayers > 1 ? eImGuiTextureType::Texture2DArray : eImGuiTextureType::Texture2D;
            constants.layer = static_cast<float>(texture.layer);
        }
        break;

        case eTexture::TextureCube:
        {
            const uint32_t numCubes = desc.numLayers / 6;
            JUG_ASSERT(texture.layer < numCubes, "ImGuiTexture::layer (cube index) is out of range.");
            type            = numCubes > 1 ? eImGuiTextureType::TextureCubeArray : eImGuiTextureType::TextureCube;
            constants.layer = static_cast<float>(texture.layer);
        }
        break;

        case eTexture::Texture3D:
        {
            // w 좌표는 [0, 1] 정규화. 지정 mip 의 depth 기준으로 slice 중심을 샘플.
            const uint32_t depth = CalcTextureSize(desc.width, desc.height, desc.depth, texture.mip).depth;
            JUG_ASSERT(texture.layer < depth, "ImGuiTexture::layer (depth slice) is out of range.");
            type            = eImGuiTextureType::Texture3D;
            constants.layer = (static_cast<float>(texture.layer) + 0.5f) / static_cast<float>(depth);
        }
        break;
    }
    constants.textureType = static_cast<uint32_t>(type);

    pGfx->UpdateBuffer(m_textureCbh, MemoryView { &constants, sizeof(constants) });

    // 셰이더가 선언한 모든 SRV 슬롯의 차원이 검증되므로, 사용하지 않는 슬롯은 null 로 비운다.
    for (uint32_t slot = 0; slot < static_cast<uint32_t>(eImGuiTextureType::Count); ++slot)
    {
        pGfx->SetTexture(slot == static_cast<uint32_t>(type) ? texture.texh : kNullHandle, eShader::Pixel, slot);
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

    Graphics* pGfx = Graphics::GetInstance();
    ReserveBuffers_(static_cast<uint32_t>(_drawData.TotalVtxCount), static_cast<uint32_t>(_drawData.TotalIdxCount));

    // upload vertex / index
    {
        const MutableMemoryView vtxMem = pGfx->MapBuffer(m_vbh);
        const MutableMemoryView idxMem = pGfx->MapBuffer(m_ibh);

        ImDrawVert* pVtxDst = reinterpret_cast<ImDrawVert*>(vtxMem.GetPtr());
        ImDrawIdx*  pIdxDst = reinterpret_cast<ImDrawIdx*>(idxMem.GetPtr());
        for (const ImDrawList* pList: _drawData.CmdLists)
        {
            std::memcpy(pVtxDst, pList->VtxBuffer.Data, static_cast<size_t>(pList->VtxBuffer.Size) * sizeof(ImDrawVert));
            std::memcpy(pIdxDst, pList->IdxBuffer.Data, static_cast<size_t>(pList->IdxBuffer.Size) * sizeof(ImDrawIdx));
            pVtxDst += pList->VtxBuffer.Size;
            pIdxDst += pList->IdxBuffer.Size;
        }

        pGfx->UnmapBuffer(m_vbh);
        pGfx->UnmapBuffer(m_ibh);
    }

    // constants
    {
        const float l = _drawData.DisplayPos.x;
        const float r = _drawData.DisplayPos.x + _drawData.DisplaySize.x;
        const float t = _drawData.DisplayPos.y;
        const float b = _drawData.DisplayPos.y + _drawData.DisplaySize.y;

        CB_IMGUI constants   = {};
        constants.proj[0][0] = 2.f / (r - l);
        constants.proj[1][1] = 2.f / (t - b);
        constants.proj[2][2] = 0.5f;
        constants.proj[3][0] = (r + l) / (l - r);
        constants.proj[3][1] = (t + b) / (b - t);
        constants.proj[3][2] = 0.5f;
        constants.proj[3][3] = 1.f;

        const TextureHandle targetTexh = pGfx->GetDesc(_fbh).atts[0].texh;
        constants.bLinearize           = IsSRGB(pGfx->GetDesc(targetTexh).format) ? 1u : 0u;

        pGfx->UpdateBuffer(m_frameCbh, MemoryView { &constants, sizeof(constants) });
    }

    SetupRenderState_(_drawData, _fbh);

    // draw
    const ImVec2 clipOffset = _drawData.DisplayPos;
    const ImVec2 clipScale  = _drawData.FramebufferScale;

    uint32_t    globalVtxOffset = 0;
    uint32_t    globalIdxOffset = 0;
    ImTextureID lastTexId       = ImTextureID {};   // 0 = 바인딩 없음
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
                lastTexId = ImTextureID {};   // 콜백이 바인딩을 바꿨을 수 있음
                continue;
            }

            const ImVec2 clipMin { (cmd.ClipRect.x - clipOffset.x) * clipScale.x, (cmd.ClipRect.y - clipOffset.y) * clipScale.y };
            const ImVec2 clipMax { (cmd.ClipRect.z - clipOffset.x) * clipScale.x, (cmd.ClipRect.w - clipOffset.y) * clipScale.y };
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
            {
                continue;
            }

            pGfx->SetScissor(static_cast<int>(clipMin.x), static_cast<int>(clipMin.y), static_cast<int>(clipMax.x - clipMin.x), static_cast<int>(clipMax.y - clipMin.y));
            if (cmd.GetTexID() != lastTexId)
            {
                BindTexture_(cmd.GetTexID());
                lastTexId = cmd.GetTexID();
            }
            pGfx->SetIndexBuffer(m_ibh, globalIdxOffset + cmd.IdxOffset, cmd.ElemCount);
            pGfx->SetSubmitParam(eSubmitParam::BaseVertexLocation, globalVtxOffset + cmd.VtxOffset);
            pGfx->Submit();
        }

        globalVtxOffset += static_cast<uint32_t>(pList->VtxBuffer.Size);
        globalIdxOffset += static_cast<uint32_t>(pList->IdxBuffer.Size);
    }

    pGfx->SetSubmitParam(eSubmitParam::BaseVertexLocation, 0);
}

}   // namespace jug
