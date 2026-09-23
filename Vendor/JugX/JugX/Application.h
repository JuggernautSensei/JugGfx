#pragma once
#include "ApplicationEvent.h"
#include "Timer.h"
#include "Vector2.h"

struct SDL_Gamepad;

namespace jug
{

struct AppDesc
{
    StringView appName       = "Application";
    StringView appVersion    = "1.0.0";
    StringView appIdentifier = {};
    bool       bInitGamepad  = true;
};

class App
{
    JUG_CLASS(App, NO_COPY, NO_MOVE)

public:
    virtual ~App();
    [[nodiscard]] static App* GetInstance();

    int  Run(int _argc, char** _argv);
    void Quit();
    void DispatchEvent(Event& _event);

    // ===========================================
    //  Time
    // ===========================================

    [[nodiscard]] float GetDeltaTimeSec() const;

    // ===========================================
    //  Keyboard
    // ===========================================

    [[nodiscard]] bool           IsKeyDown(eKey _key) const;
    [[nodiscard]] bool           IsKeyPressed(eKey _key) const;
    [[nodiscard]] bool           IsKeyReleased(eKey _key) const;
    [[nodiscard]] bool           IsKeyDown(eKey _key, Flags<eKeyMod> _mods, bool _bExactMods = false) const;
    [[nodiscard]] bool           IsKeyPressed(eKey _key, Flags<eKeyMod> _mods, bool _bExactMods = false) const;
    [[nodiscard]] bool           IsKeyReleased(eKey _key, Flags<eKeyMod> _mods, bool _bExactMods = false) const;
    [[nodiscard]] Flags<eKeyMod> GetKeyMods() const;

    // ===========================================
    //  Mouse
    // ===========================================

    [[nodiscard]] bool IsMouseDown(eMouse _button) const;
    [[nodiscard]] bool IsMousePressed(eMouse _button) const;
    [[nodiscard]] bool IsMouseReleased(eMouse _button) const;

    [[nodiscard]] VECTOR2 GetMousePos() const;
    [[nodiscard]] VECTOR2 GetMouseDelta() const;
    [[nodiscard]] VECTOR2 GetMouseWheelDelta() const;

    // ===========================================
    //  Gamepad
    // ===========================================

    [[nodiscard]] bool  IsGamepadConnected() const;
    [[nodiscard]] bool  IsGamepadButtonDown(eGamepadButton _button) const;
    [[nodiscard]] bool  IsGamepadButtonPressed(eGamepadButton _button) const;
    [[nodiscard]] bool  IsGamepadButtonReleased(eGamepadButton _button) const;
    [[nodiscard]] float GetGamepadAxis(eGamepadAxis _axis) const;

    [[nodiscard]] Span<const String> GetCommandLineArgs() const;
    [[nodiscard]] const AppDesc&     GetDesc() const;

protected:
    explicit App(const AppDesc& _desc);

    virtual void Init();
    virtual void Shutdown();
    virtual void OnEvent(Event& _event);
    virtual void Update(float _deltaTimeSec);

    void SetReturnCode(int _code);

private:
    void InitSystems_() const;
    void ShutdownSystems_();
    void PollEvents_();
    void UpdateInputState_();

    void OnQuitEvent_(const QuitEvent& _event);
    void OnKeyDownEvent_(const KeyDownEvent& _event);
    void OnKeyUpEvent_(const KeyUpEvent& _event);
    void OnMouseMovedEvent_(const MouseMovedEvent& _event);
    void OnMouseButtonDownEvent_(const MouseButtonDownEvent& _event);
    void OnMouseButtonUpEvent_(const MouseButtonUpEvent& _event);
    void OnMouseWheelEvent_(const MouseWheelEvent& _event);
    void OnWindowFocusLostEvent_(const WindowFocusLostEvent& _event);
    void OnGamepadAddedEvent_(const GamepadAddedEvent& _event);
    void OnGamepadRemovedEvent_(const GamepadRemovedEvent& _event);
    void OnGamepadButtonDownEvent_(const GamepadButtonDownEvent& _event);
    void OnGamepadButtonUpEvent_(const GamepadButtonUpEvent& _event);
    void OnGamepadAxisMotionEvent_(const GamepadAxisMotionEvent& _event);

    AppDesc m_desc = {};

    // ===========================================
    //  Frame
    // ===========================================

    Timer m_timer        = {};
    float m_deltaTimeSec = 0.f;

    // ===========================================
    //  Input State
    // ===========================================

    static constexpr size_t kNumKeys = static_cast<size_t>(eKey::Count);

    ARRAY<bool, kNumKeys>                  m_bKeyDowns       = {};
    ARRAY<bool, kNumKeys>                  m_bPrevKeyDowns   = {};
    ARRAY<bool, CountOf<eMouse>()>         m_bMouseDowns     = {};
    ARRAY<bool, CountOf<eMouse>()>         m_bPrevMouseDowns = {};
    ARRAY<bool, CountOf<eGamepadButton>()> m_bPadDowns       = {};
    ARRAY<bool, CountOf<eGamepadButton>()> m_bPrevPadDowns   = {};
    ARRAY<float, CountOf<eGamepadAxis>()>  m_padAxes         = {};

    Flags<eKeyMod> m_keyMods    = {};
    VECTOR2        m_mousePos   = {};
    VECTOR2        m_mouseDelta = {};
    VECTOR2        m_wheelDelta = {};

    SDL_Gamepad* m_pGamepadOrNull = nullptr;
    uint32_t     m_gamepadID      = 0;

    Vector<String> m_cmdArgs    = {};
    bool           m_bRunning   = false;
    int            m_returnCode = 0;
};

}   // namespace jug
