#include "pch.h"
#include "Application.h"

#include "CoreLogger.h"
#include "EventDispatcher.h"
#include "Fatal.h"
#include "OS.h"

namespace jug
{

namespace
{
    App* g_pSingleton = nullptr;

    constexpr float kGamepadAxisScale = 1.f / 32767.f;

    [[nodiscard]] Flags<eKeyMod> MakeKeyMods_(
        const SDL_Keymod _mod)
    {
        Flags<eKeyMod> mods = kZeroFlag;
        if (_mod & SDL_KMOD_SHIFT)
        {
            mods |= eKeyMod::Shift;
        }
        if (_mod & SDL_KMOD_CTRL)
        {
            mods |= eKeyMod::Ctrl;
        }
        if (_mod & SDL_KMOD_ALT)
        {
            mods |= eKeyMod::Alt;
        }
        if (_mod & SDL_KMOD_GUI)
        {
            mods |= eKeyMod::Gui;
        }
        if (_mod & SDL_KMOD_NUM)
        {
            mods |= eKeyMod::Num;
        }
        if (_mod & SDL_KMOD_CAPS)
        {
            mods |= eKeyMod::Caps;
        }
        if (_mod & SDL_KMOD_SCROLL)
        {
            mods |= eKeyMod::Scroll;
        }
        if (_mod & SDL_KMOD_MODE)
        {
            mods |= eKeyMod::Mode;
        }
        return mods;
    }
}   // namespace

App::App(
    const AppDesc& _desc)
    : m_desc(_desc)
{
    JUG_ASSERT(!g_pSingleton, "App instance already exists.");
    g_pSingleton = this;
}

void App::Init()
{
}

void App::Shutdown()
{
}

void App::OnEvent(Event& _event)
{
}

void App::Update(
    const float _deltaTimeSec)
{
}

App::~App()
{
    JUG_ASSERT(g_pSingleton == this, "App instance mismatch.");
    g_pSingleton = nullptr;
}

App* App::GetInstance()
{
    JUG_ASSERT(g_pSingleton, "App instance is not created yet.");
    return g_pSingleton;
}

int App::Run(
    const int _argc,
    char**    _argv)
{
    m_cmdArgs.reserve(static_cast<size_t>(_argc));
    for (int i = 0; i < _argc; ++i)
    {
        m_cmdArgs.emplace_back(_argv[i]);
    }

    InitSystems_();
    Init();

    m_bRunning   = true;
    m_returnCode = 0;
    m_timer.Start();

    while (m_bRunning)
    {
        PollEvents_();
        if (!m_bRunning)
        {
            break;
        }

        m_timer.Lap();
        m_deltaTimeSec = TimeCastF(m_timer.GetElapsedCount(), eTimeUnit::Sec);
        Update(m_deltaTimeSec);
        UpdateInputState_();
    }

    Shutdown();
    ShutdownSystems_();
    return m_returnCode;
}

void App::Quit()
{
    m_bRunning = false;
}

// ===========================================
//  Time
// ===========================================

float App::GetDeltaTimeSec() const
{
    return m_deltaTimeSec;
}

// ===========================================
//  Keyboard
// ===========================================

bool App::IsKeyDown(
    const eKey _key) const
{
    return m_bKeyDowns[static_cast<size_t>(_key)];
}

bool App::IsKeyDown(
    const eKey           _key,
    const Flags<eKeyMod> _mods,
    const bool           _bExactMods) const
{
    return IsKeyDown(_key) && (_bExactMods ? m_keyMods == _mods : m_keyMods.HasAll(_mods));
}

bool App::IsKeyPressed(
    const eKey _key) const
{
    const size_t index = static_cast<size_t>(_key);
    return m_bKeyDowns[index] && !m_bPrevKeyDowns[index];
}

bool App::IsKeyPressed(
    const eKey           _key,
    const Flags<eKeyMod> _mods,
    const bool           _bExactMods) const
{
    return IsKeyPressed(_key) && (_bExactMods ? m_keyMods == _mods : m_keyMods.HasAll(_mods));
}

bool App::IsKeyReleased(
    const eKey _key) const
{
    const size_t index = static_cast<size_t>(_key);
    return !m_bKeyDowns[index] && m_bPrevKeyDowns[index];
}

bool App::IsKeyReleased(
    const eKey           _key,
    const Flags<eKeyMod> _mods,
    const bool           _bExactMods) const
{
    return IsKeyReleased(_key) && (_bExactMods ? m_keyMods == _mods : m_keyMods.HasAll(_mods));
}

Flags<eKeyMod> App::GetKeyMods() const
{
    return m_keyMods;
}

// ===========================================
//  Mouse
// ===========================================

bool App::IsMouseDown(
    const eMouse _button) const
{
    return m_bMouseDowns[static_cast<size_t>(_button)];
}

bool App::IsMousePressed(
    const eMouse _button) const
{
    const size_t index = static_cast<size_t>(_button);
    return m_bMouseDowns[index] && !m_bPrevMouseDowns[index];
}

bool App::IsMouseReleased(
    const eMouse _button) const
{
    const size_t index = static_cast<size_t>(_button);
    return !m_bMouseDowns[index] && m_bPrevMouseDowns[index];
}

VECTOR2 App::GetMousePos() const
{
    return m_mousePos;
}

VECTOR2 App::GetMouseDelta() const
{
    return m_mouseDelta;
}

VECTOR2 App::GetMouseWheelDelta() const
{
    return m_wheelDelta;
}

// ===========================================
//  Gamepad
// ===========================================

bool App::IsGamepadConnected() const
{
    return m_pGamepadOrNull != nullptr;
}

bool App::IsGamepadButtonDown(
    const eGamepadButton _button) const
{
    return m_bPadDowns[static_cast<size_t>(_button)];
}

bool App::IsGamepadButtonPressed(
    const eGamepadButton _button) const
{
    const size_t index = static_cast<size_t>(_button);
    return m_bPadDowns[index] && !m_bPrevPadDowns[index];
}

bool App::IsGamepadButtonReleased(
    const eGamepadButton _button) const
{
    const size_t index = static_cast<size_t>(_button);
    return !m_bPadDowns[index] && m_bPrevPadDowns[index];
}

float App::GetGamepadAxis(
    const eGamepadAxis _axis) const
{
    return m_padAxes[static_cast<size_t>(_axis)];
}

Span<const String> App::GetCommandLineArgs() const
{
    return m_cmdArgs;
}

const AppDesc& App::GetDesc() const
{
    return m_desc;
}

void App::SetReturnCode(
    const int _code)
{
    m_returnCode = _code;
}

void App::InitSystems_() const
{
    const String appName    = String { m_desc.appName };
    const String appVersion = String { m_desc.appVersion };
    const String appIdent   = String { m_desc.appIdentifier };
    if (!SDL_SetAppMetadata(appName.c_str(), appVersion.c_str(), appIdent.empty() ? nullptr : appIdent.c_str()))
    {
        JUG_CORE_LOG_WARN("SDL_SetAppMetadata failed: {}", SDL_GetError());
    }

    SDL_InitFlags flags = SDL_INIT_VIDEO;
    if (m_desc.bInitGamepad)
    {
        flags |= SDL_INIT_GAMEPAD;
    }

    if (!SDL_Init(flags))
    {
        JUG_FATAL("SDL_Init failed: {}", SDL_GetError());
    }
}

void App::ShutdownSystems_()
{
    if (m_pGamepadOrNull)
    {
        SDL_CloseGamepad(m_pGamepadOrNull);
        m_pGamepadOrNull = nullptr;
    }

    SDL_Quit();
}

void App::PollEvents_()
{
    SDL_Event msg;
    while (SDL_PollEvent(&msg))
    {
        switch (msg.type)
        {
            case SDL_EVENT_QUIT:
            {
                QuitEvent event;
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_LOW_MEMORY:
            {
                LowMemoryEvent event;
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_SHOWN:
            {
                WindowShownEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_HIDDEN:
            {
                WindowHiddenEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_EXPOSED:
            {
                WindowExposedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_OCCLUDED:
            {
                WindowOccludedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_MINIMIZED:
            {
                WindowMinimizedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_MAXIMIZED:
            {
                WindowMaximizedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_RESTORED:
            {
                WindowRestoredEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            {
                WindowMouseEnterEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            {
                WindowMouseLeaveEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            {
                WindowFocusGainedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
            {
                WindowFocusLostEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            {
                WindowCloseRequestedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_DESTROYED:
            {
                WindowDestroyedEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            {
                WindowEnterFullscreenEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            {
                WindowLeaveFullscreenEvent event { msg.window.windowID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_MOVED:
            {
                WindowMovedEvent event { msg.window.windowID, msg.window.data1, msg.window.data2 };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_RESIZED:
            {
                WindowResizedEvent event { msg.window.windowID, msg.window.data1, msg.window.data2 };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                WindowPixelSizeChangedEvent event { msg.window.windowID, msg.window.data1, msg.window.data2 };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            {
                const SDL_Window*              pWindow = SDL_GetWindowFromID(msg.window.windowID);
                const float                    scale   = pWindow ? SDL_GetWindowDisplayScale(SDL_GetWindowFromID(msg.window.windowID)) : 1.f;
                WindowDisplayScaleChangedEvent event { msg.window.windowID, scale };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            {
                WindowDisplayChangedEvent event { msg.window.windowID, static_cast<uint32_t>(msg.window.data1) };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DISPLAY_ADDED:
            {
                DisplayAddedEvent event { msg.display.displayID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DISPLAY_REMOVED:
            {
                DisplayRemovedEvent event { msg.display.displayID };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DISPLAY_ORIENTATION:
            {
                DisplayOrientationChangedEvent event { msg.display.displayID, msg.display.data1 };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
            {
                DisplayContentScaleChangedEvent event { msg.display.displayID, SDL_GetDisplayContentScale(msg.display.displayID) };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_KEY_DOWN:
            {
                KeyDownEvent event { msg.key.windowID, static_cast<eKey>(msg.key.scancode), MakeKeyMods_(msg.key.mod), msg.key.repeat };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_KEY_UP:
            {
                KeyUpEvent event { msg.key.windowID, static_cast<eKey>(msg.key.scancode), MakeKeyMods_(msg.key.mod) };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_TEXT_INPUT:
            {
                TextInputEvent event { msg.text.windowID, msg.text.text ? StringView { msg.text.text } : StringView {} };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_TEXT_EDITING:
            {
                TextEditingEvent event { msg.edit.windowID, msg.edit.text ? StringView { msg.edit.text } : StringView {}, msg.edit.start, msg.edit.length };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_MOUSE_MOTION:
            {
                MouseMovedEvent event { msg.motion.windowID, msg.motion.x, msg.motion.y, msg.motion.xrel, msg.motion.yrel };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                MouseButtonDownEvent event { msg.button.windowID, static_cast<eMouse>(msg.button.button), msg.button.x, msg.button.y, msg.button.clicks };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                MouseButtonUpEvent event { msg.button.windowID, static_cast<eMouse>(msg.button.button), msg.button.x, msg.button.y };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_MOUSE_WHEEL:
            {
                MouseWheelEvent event { msg.wheel.windowID, msg.wheel.x, msg.wheel.y, msg.wheel.mouse_x, msg.wheel.mouse_y };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_GAMEPAD_ADDED:
            {
                GamepadAddedEvent event { msg.gdevice.which };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_GAMEPAD_REMOVED:
            {
                GamepadRemovedEvent event { msg.gdevice.which };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            {
                GamepadButtonDownEvent event { msg.gbutton.which, static_cast<eGamepadButton>(msg.gbutton.button) };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                GamepadButtonUpEvent event { msg.gbutton.which, static_cast<eGamepadButton>(msg.gbutton.button) };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                GamepadAxisMotionEvent event { msg.gaxis.which, static_cast<eGamepadAxis>(msg.gaxis.axis), Max(static_cast<float>(msg.gaxis.value) * kGamepadAxisScale, -1.f) };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DROP_BEGIN:
            {
                DropBeginEvent event { msg.drop.windowID, msg.drop.x, msg.drop.y };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DROP_COMPLETE:
            {
                DropCompleteEvent event { msg.drop.windowID, msg.drop.x, msg.drop.y };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DROP_FILE:
            {
                DropFileEvent event { msg.drop.windowID, msg.drop.x, msg.drop.y, msg.drop.data ? StringView { msg.drop.data } : StringView {} };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_DROP_TEXT:
            {
                DropTextEvent event { msg.drop.windowID, msg.drop.x, msg.drop.y, msg.drop.data ? StringView { msg.drop.data } : StringView {} };
                DispatchEvent(event);
            }
            break;

            case SDL_EVENT_CLIPBOARD_UPDATE:
            {
                ClipboardUpdatedEvent event;
                DispatchEvent(event);
            }
            break;

            default:
                break;
        }
    }
}

void App::DispatchEvent(
    Event& _event)
{
    EventDispatcher dispatcher { _event };
    dispatcher.Dispatch<QuitEvent>(this, &App::OnQuitEvent_);
    dispatcher.Dispatch<KeyDownEvent>(this, &App::OnKeyDownEvent_);
    dispatcher.Dispatch<KeyUpEvent>(this, &App::OnKeyUpEvent_);
    dispatcher.Dispatch<MouseMovedEvent>(this, &App::OnMouseMovedEvent_);
    dispatcher.Dispatch<MouseButtonDownEvent>(this, &App::OnMouseButtonDownEvent_);
    dispatcher.Dispatch<MouseButtonUpEvent>(this, &App::OnMouseButtonUpEvent_);
    dispatcher.Dispatch<MouseWheelEvent>(this, &App::OnMouseWheelEvent_);
    dispatcher.Dispatch<WindowFocusLostEvent>(this, &App::OnWindowFocusLostEvent_);
    dispatcher.Dispatch<GamepadAddedEvent>(this, &App::OnGamepadAddedEvent_);
    dispatcher.Dispatch<GamepadRemovedEvent>(this, &App::OnGamepadRemovedEvent_);
    dispatcher.Dispatch<GamepadButtonDownEvent>(this, &App::OnGamepadButtonDownEvent_);
    dispatcher.Dispatch<GamepadButtonUpEvent>(this, &App::OnGamepadButtonUpEvent_);
    dispatcher.Dispatch<GamepadAxisMotionEvent>(this, &App::OnGamepadAxisMotionEvent_);
    OnEvent(_event);
}

void App::UpdateInputState_()
{
    m_bPrevKeyDowns   = m_bKeyDowns;
    m_bPrevMouseDowns = m_bMouseDowns;
    m_bPrevPadDowns   = m_bPadDowns;

    m_mouseDelta = VECTOR2 { 0.f, 0.f };
    m_wheelDelta = VECTOR2 { 0.f, 0.f };
}

void App::OnQuitEvent_(
    const QuitEvent& _event)
{
    Quit();
}

void App::OnKeyDownEvent_(
    const KeyDownEvent& _event)
{
    m_bKeyDowns[static_cast<size_t>(_event.GetKey())] = true;
    m_keyMods                                         = _event.GetMods();
}

void App::OnKeyUpEvent_(
    const KeyUpEvent& _event)
{
    m_bKeyDowns[static_cast<size_t>(_event.GetKey())] = false;
    m_keyMods                                         = _event.GetMods();
}

void App::OnMouseMovedEvent_(
    const MouseMovedEvent& _event)
{
    m_mousePos = VECTOR2 { _event.GetX(), _event.GetY() };
    m_mouseDelta += VECTOR2 { _event.GetDeltaX(), _event.GetDeltaY() };
}

void App::OnMouseButtonDownEvent_(
    const MouseButtonDownEvent& _event)
{
    m_bMouseDowns[static_cast<size_t>(_event.GetButton())] = true;
    m_mousePos                                             = VECTOR2 { _event.GetX(), _event.GetY() };
}

void App::OnMouseButtonUpEvent_(
    const MouseButtonUpEvent& _event)
{
    m_bMouseDowns[static_cast<size_t>(_event.GetButton())] = false;
    m_mousePos                                             = VECTOR2 { _event.GetX(), _event.GetY() };
}

void App::OnMouseWheelEvent_(
    const MouseWheelEvent& _event)
{
    m_wheelDelta += VECTOR2 { _event.GetScrollX(), _event.GetScrollY() };
}

void App::OnWindowFocusLostEvent_(
    const WindowFocusLostEvent& _event)
{
    m_bKeyDowns.fill(false);
    m_bMouseDowns.fill(false);
    m_keyMods = {};
}

void App::OnGamepadAddedEvent_(
    const GamepadAddedEvent& _event)
{
    if (m_pGamepadOrNull)
    {
        return;
    }

    m_pGamepadOrNull = SDL_OpenGamepad(_event.GetJoystickID());
    if (!m_pGamepadOrNull)
    {
        JUG_CORE_LOG_WARN("SDL_OpenGamepad failed: {}", SDL_GetError());
        return;
    }

    m_gamepadID = _event.GetJoystickID();
}

void App::OnGamepadRemovedEvent_(
    const GamepadRemovedEvent& _event)
{
    if (!m_pGamepadOrNull || m_gamepadID != _event.GetJoystickID())
    {
        return;
    }

    SDL_CloseGamepad(m_pGamepadOrNull);
    m_pGamepadOrNull = nullptr;
    m_gamepadID      = 0;

    m_bPadDowns.fill(false);
    m_padAxes.fill(0.f);
}

void App::OnGamepadButtonDownEvent_(
    const GamepadButtonDownEvent& _event)
{
    if (_event.GetJoystickID() != m_gamepadID)
    {
        return;
    }

    m_bPadDowns[static_cast<size_t>(_event.GetButton())] = true;
}

void App::OnGamepadButtonUpEvent_(
    const GamepadButtonUpEvent& _event)
{
    if (_event.GetJoystickID() != m_gamepadID)
    {
        return;
    }

    m_bPadDowns[static_cast<size_t>(_event.GetButton())] = false;
}

void App::OnGamepadAxisMotionEvent_(
    const GamepadAxisMotionEvent& _event)
{
    if (_event.GetJoystickID() != m_gamepadID)
    {
        return;
    }

    m_padAxes[static_cast<size_t>(_event.GetAxis())] = _event.GetValue();
}

}   // namespace jug
