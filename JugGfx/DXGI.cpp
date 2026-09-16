#include "pch.h"
#include "DXGI.h"

#include <JugX/CoreLogger.h>
#include <JugX/StringEncoder.h>

#include "DxUtils.h"

namespace jug
{

namespace
{
    using CreateDxgiFactoryFn      = HRESULT(WINAPI*)(REFIID _riid, void** _ppFactory);
    using GetDxgiDebugInterfaceFn  = HRESULT(WINAPI*)(REFIID _riid, void** _ppDebug);
    using GetDxgiDebugInterface1Fn = HRESULT(WINAPI*)(UINT _flags, REFIID _riid, void** _ppDebug);

    constexpr size_t         kMaxDebugNameLength         = 255;
    CreateDxgiFactoryFn      g_pCreateDxgiFactoryFn      = nullptr;
    GetDxgiDebugInterfaceFn  g_pGetDxgiDebugInterfaceFn  = nullptr;
    GetDxgiDebugInterface1Fn g_pGetDxgiDebugInterface1Fn = nullptr;

    void SetDxgiObjectName_(
        IDXGIObject*     _pDxgiObject,
        const StringView _name)
    {
#ifdef JUG_DEBUG
        JUG_ASSERT(_pDxgiObject, "Null DXGI object pointer.");
        const UINT len = static_cast<UINT>(Min(_name.size(), kMaxDebugNameLength));
        JUG_DISCARD_RETURN(_pDxgiObject->SetPrivateData(WKPDID_D3DDebugObjectName, len, _name.data()));
#endif
    }
}   // namespace

DXGI::DXGI()
{
    // load dll
    m_pDxgiDll = SDL_LoadObject("dxgi.dll");
    if (!m_pDxgiDll)
    {
        JUG_FATAL("Failed to load dxgi.dll");
    }

    // load debug dll
    m_pDxgiDbgDllOrNull = SDL_LoadObject("dxgidebug.dll");
    if (m_pDxgiDbgDllOrNull)
    {
        g_pGetDxgiDebugInterfaceFn  = reinterpret_cast<GetDxgiDebugInterfaceFn>(SDL_LoadFunction(m_pDxgiDbgDllOrNull, "DXGIGetDebugInterface"));     // NOLINT
        g_pGetDxgiDebugInterface1Fn = reinterpret_cast<GetDxgiDebugInterface1Fn>(SDL_LoadFunction(m_pDxgiDbgDllOrNull, "DXGIGetDebugInterface1"));   // NOLINT
        if (!g_pGetDxgiDebugInterfaceFn && !g_pGetDxgiDebugInterface1Fn)
        {
            JUG_CORE_LOG_WARN("Failed to load dxgidebug.dll functions. Debugging features will be unavailable.");
            SDL_UnloadObject(m_pDxgiDbgDllOrNull);
            m_pDxgiDbgDllOrNull = nullptr;
        }
    }
    else
    {
        JUG_CORE_LOG_WARN("Failed to load dxgidebug.dll. Debugging features will be unavailable.");
    }

    // get factory creation function
    g_pCreateDxgiFactoryFn = reinterpret_cast<CreateDxgiFactoryFn>(SDL_LoadFunction(m_pDxgiDll, "CreateDXGIFactory1"));   // NOLINT
    if (!g_pCreateDxgiFactoryFn)                                                                                          // fallback
    {
        g_pCreateDxgiFactoryFn = reinterpret_cast<CreateDxgiFactoryFn>(SDL_LoadFunction(m_pDxgiDll, "CreateDXGIFactory"));   // NOLINT
        if (!g_pCreateDxgiFactoryFn)
        {
            JUG_FATAL("Failed to load CreateDXGIFactory1 or CreateDXGIFactory from dxgi.dll");
        }
    }

    // get factory
    IDXGIFactory1* pFactory1 = nullptr;
    JUG_DX_CHECK(g_pCreateDxgiFactoryFn(IID_IDXGIFactory1, reinterpret_cast<void**>(&pFactory1)));
    if (pFactory1)
    {
        JUG_DX_CHECK(pFactory1->QueryInterface(IID_PPV_ARGS(&m_pFactory)));
        JUG_DX_RELEASE(pFactory1);
    }

    SetDxgiObjectName_(m_pFactory, "JugGfx.DXGI.Factory");

    // vram이 가장 많은 어댑터를 선택한다.
    IDXGIAdapter1* pAdapter1    = nullptr;
    IDXGIAdapter1* pBestAdapter = nullptr;
    size_t         bestMemory   = 0;
    for (uint32_t i = 0; m_pFactory->EnumAdapters1(i, &pAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 ad = {};
        if (FAILED(pAdapter1->GetDesc1(&ad)))
        {
            JUG_DX_RELEASE(pAdapter1);
            continue;
        }

        const bool bSoftware = (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

        String description;
        AppendUtf16(description, WStringView { ad.Description });

        JUG_CORE_LOG_INFO("DXGI adapter #{}: {}{}", i, description, bSoftware ? " (software)" : "");

        JUG_CORE_LOG_INFO("  VendorId: 0x{:04X}, DeviceId: 0x{:04X}, SubSysId: 0x{:08X}, Revision: 0x{:02X}",
                          ad.VendorId,
                          ad.DeviceId,
                          ad.SubSysId,
                          ad.Revision);

        JUG_CORE_LOG_INFO("  Memory: {} MiB (video), {} MiB (system), {} MiB (shared)",
                          ad.DedicatedVideoMemory / (1024ull * 1024ull),
                          ad.DedicatedSystemMemory / (1024ull * 1024ull),
                          ad.SharedSystemMemory / (1024ull * 1024ull));

        IDXGIOutput* pOutput = nullptr;
        for (UINT j = 0; SUCCEEDED(pAdapter1->EnumOutputs(j, &pOutput)); ++j)
        {
            DXGI_OUTPUT_DESC outputDesc = {};
            if (SUCCEEDED(pOutput->GetDesc(&outputDesc)))
            {
                String deviceName;
                AppendUtf16(deviceName, WStringView { outputDesc.DeviceName });
                JUG_CORE_LOG_INFO("  Output #{}: {}", j, deviceName);

                IDXGIOutput6* pOutput6 = nullptr;
                if (SUCCEEDED(pOutput->QueryInterface(IID_PPV_ARGS(&pOutput6))))
                {
                    DXGI_OUTPUT_DESC1 outputDesc1 = {};
                    if (SUCCEEDED(pOutput6->GetDesc1(&outputDesc1)))
                    {
                        JUG_CORE_LOG_INFO("    HDR support: {}", outputDesc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "true" : "false");
                    }
                    JUG_DX_RELEASE(pOutput6);
                }
            }
            JUG_DX_RELEASE(pOutput);
        }

        // 소프트웨어 어댑터는 제외함.
        if (!bSoftware && (!pBestAdapter || ad.DedicatedVideoMemory > bestMemory))
        {
            JUG_DX_RELEASE(pBestAdapter);
            bestMemory = ad.DedicatedVideoMemory;

            pBestAdapter = pAdapter1;
            pAdapter1    = nullptr;
        }

        JUG_DX_RELEASE(pAdapter1);
    }

    if (pBestAdapter)
    {
        JUG_DX_CHECK(pBestAdapter->QueryInterface(IID_PPV_ARGS(&m_pAdapterOrNull)));
        JUG_DISCARD_RETURN(pBestAdapter->EnumOutputs(0, &m_pOutput));
        JUG_DX_RELEASE(pBestAdapter);

        SetDxgiObjectName_(m_pAdapterOrNull, "JugGfx.DXGI.Adapter");
        SetDxgiObjectName_(m_pOutput, "JugGfx.DXGI.Output");
    }
    else
    {
        JUG_CORE_LOG_WARN("No suitable DXGI adapter found.");
    }
}

DXGI::~DXGI()
{
    Reset_();
}

DXGI::DXGI(
    DXGI&& _other) noexcept
    : m_pDxgiDll(std::exchange(_other.m_pDxgiDll, nullptr))
    , m_pDxgiDbgDllOrNull(std::exchange(_other.m_pDxgiDbgDllOrNull, nullptr))
    , m_pAdapterOrNull(std::exchange(_other.m_pAdapterOrNull, nullptr))
    , m_pFactory(std::exchange(_other.m_pFactory, nullptr))
    , m_pOutput(std::exchange(_other.m_pOutput, nullptr))
{
}

DXGI& DXGI::operator=(
    DXGI&& _other) noexcept
{
    if (this != &_other)
    {
        Reset_();
        m_pDxgiDll          = std::exchange(_other.m_pDxgiDll, nullptr);
        m_pDxgiDbgDllOrNull = std::exchange(_other.m_pDxgiDbgDllOrNull, nullptr);
        m_pAdapterOrNull    = std::exchange(_other.m_pAdapterOrNull, nullptr);
        m_pFactory          = std::exchange(_other.m_pFactory, nullptr);
        m_pOutput           = std::exchange(_other.m_pOutput, nullptr);
    }
    return *this;
}

ISwapChain* DXGI::CreateSwapChain(
    IUnknown*                    _pDevice,
    void*                        _pWindow,
    const bool                   _bWindowed,
    const DXGI_SWAP_CHAIN_DESC1& _desc) const
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC scfd;
    scfd.RefreshRate.Numerator   = 1;
    scfd.RefreshRate.Denominator = 60;
    scfd.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    scfd.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
    scfd.Windowed                = _bWindowed;

    ISwapChain* pSwapChain;
    JUG_DX_CHECK(m_pFactory->CreateSwapChainForHwnd(_pDevice, static_cast<HWND>(_pWindow), &_desc, &scfd, nullptr, reinterpret_cast<IDXGISwapChain1**>(&pSwapChain)));
    return pSwapChain;
}

void DXGI::UpdateCaps(
    GraphicsCaps& _inoutCaps) const
{
    if (m_pAdapterOrNull)
    {
        DXGI_ADAPTER_DESC1 ad = {};
        if (SUCCEEDED(m_pAdapterOrNull->GetDesc1(&ad)))
        {
            _inoutCaps.vendorID           = ad.VendorId;
            _inoutCaps.deviceID           = ad.DeviceId;
            _inoutCaps.videoMemory        = ad.DedicatedVideoMemory;
            _inoutCaps.systemMemory       = ad.DedicatedSystemMemory;
            _inoutCaps.sharedSystemMemory = ad.SharedSystemMemory;
        }
    }

    // check tearing support
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(m_pFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
    {
        _inoutCaps.bAllowTearing = allowTearing != FALSE;
    }
}

IAdapter* DXGI::GetAdapterOrNull() const
{
    return m_pAdapterOrNull;
}

IFactory* DXGI::GetFactory() const
{
    return m_pFactory;
}

IOutput* DXGI::GetOutput() const
{
    return m_pOutput;
}

void DXGI::Reset_()
{
    JUG_DX_RELEASE(m_pOutput);
    JUG_DX_RELEASE(m_pAdapterOrNull);
    JUG_DX_RELEASE(m_pFactory);

    if (m_pDxgiDbgDllOrNull)
    {
        SDL_UnloadObject(m_pDxgiDbgDllOrNull);
        m_pDxgiDbgDllOrNull = nullptr;
    }

    if (m_pDxgiDll)
    {
        SDL_UnloadObject(m_pDxgiDll);
        m_pDxgiDll = nullptr;
    }
}

}   // namespace jug