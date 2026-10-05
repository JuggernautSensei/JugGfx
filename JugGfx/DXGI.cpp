#include "pch.h"
#include "DXGI.h"

#include <utility>

#include "DxMacro.h"

namespace jug
{

namespace
{
    using CreateDxgiFactoryFn      = HRESULT(WINAPI*)(REFIID _riid, void** _ppFactory);
    using GetDxgiDebugInterfaceFn  = HRESULT(WINAPI*)(REFIID _riid, void** _ppDebug);
    using GetDxgiDebugInterface1Fn = HRESULT(WINAPI*)(UINT _flags, REFIID _riid, void** _ppDebug);

    constexpr size_t         kMaxDebugNameLength               = 255;
    CreateDxgiFactoryFn      g_pCreateDxgiFactoryFn            = nullptr;
    GetDxgiDebugInterfaceFn  g_pGetDxgiDebugInterfaceFnOrNull  = nullptr;
    GetDxgiDebugInterface1Fn g_pGetDxgiDebugInterface1FnOrNull = nullptr;

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
        g_pGetDxgiDebugInterfaceFnOrNull  = reinterpret_cast<GetDxgiDebugInterfaceFn>(SDL_LoadFunction(m_pDxgiDbgDllOrNull, "DXGIGetDebugInterface"));     // NOLINT
        g_pGetDxgiDebugInterface1FnOrNull = reinterpret_cast<GetDxgiDebugInterface1Fn>(SDL_LoadFunction(m_pDxgiDbgDllOrNull, "DXGIGetDebugInterface1"));   // NOLINT
        if (!g_pGetDxgiDebugInterfaceFnOrNull && !g_pGetDxgiDebugInterface1FnOrNull)
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
    IDXGIFactory1* pFactory1;
    JUG_DX_CHECK(g_pCreateDxgiFactoryFn(IID_IDXGIFactory1, reinterpret_cast<void**>(&pFactory1)));
    if (pFactory1)
    {
        JUG_DX_CHECK(pFactory1->QueryInterface(IID_PPV_ARGS(&m_pFactory)));
        JUG_DX_RELEASE(pFactory1);
    }

    SetDxgiObjectName_(m_pFactory, "JugGfx.DXGI.Factory");

    // vram이 가장 많은 어댑터를 선택한다.
    constexpr size_t kMiB = 1024ull * 1024ull;

    IDXGIAdapter1* pAdapter1          = nullptr;
    IDXGIAdapter1* pBestAdapterOrNull = nullptr;
    size_t         bestMemory         = 0;
    uint32_t       bestIndex          = 0;
    String         bestName           = {};
    for (uint32_t i = 0; m_pFactory->EnumAdapters1(i, &pAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 ad;
        if (FAILED(pAdapter1->GetDesc1(&ad)))
        {
            JUG_DX_RELEASE(pAdapter1);
            continue;
        }

        const bool bSoftware = (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

        String desc = {};
        AppendUtf16(desc, ad.Description);

        // UMD 드라이버 버전.
        String        driverVersion = "?";
        LARGE_INTEGER umdVersion;
        if (SUCCEEDED(pAdapter1->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umdVersion)))
        {
            driverVersion = Format("{}.{}.{}.{}", HIWORD(umdVersion.HighPart), LOWORD(umdVersion.HighPart), HIWORD(umdVersion.LowPart), LOWORD(umdVersion.LowPart));
        }

        if (bSoftware)
        {
            JUG_CORE_LOG_INFO("GPU[{}]: {} [software]", i, desc);
        }
        else
        {
            JUG_CORE_LOG_INFO("GPU[{}]: {} ({:04X}:{:04X}), driver {}, VRAM {} MiB, shared {} MiB", i, desc, ad.VendorId, ad.DeviceId, driverVersion, ad.DedicatedVideoMemory / kMiB, ad.SharedSystemMemory / kMiB);
        }
        JUG_CORE_LOG_TRACE("GPU[{}]: subsys 0x{:08X}, rev 0x{:02X}, dedicated system {} MiB", i, ad.SubSysId, ad.Revision, ad.DedicatedSystemMemory / kMiB);

        IDXGIOutput* pOutput;
        for (UINT j = 0; SUCCEEDED(pAdapter1->EnumOutputs(j, &pOutput)); ++j)
        {
            DXGI_OUTPUT_DESC outputDesc = {};
            if (SUCCEEDED(pOutput->GetDesc(&outputDesc)))
            {
                String deviceName = {};
                AppendUtf16(deviceName, WStringView { outputDesc.DeviceName });

                const RECT& rc = outputDesc.DesktopCoordinates;

                // 현재 출력 색 공간이 HDR10인지. 지원 여부가 아니라 윈도우 HDR 켜짐 여부다.
                String        colorInfo = {};
                IDXGIOutput6* pOutput6;
                if (SUCCEEDED(pOutput->QueryInterface(IID_PPV_ARGS(&pOutput6))))
                {
                    DXGI_OUTPUT_DESC1 outputDesc1 = {};
                    if (SUCCEEDED(pOutput6->GetDesc1(&outputDesc1)))
                    {
                        const bool bHdr = outputDesc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                        colorInfo       = Format(", {} bpc, HDR {}, luminance {:.0f}-{:.0f} nits", outputDesc1.BitsPerColor, bHdr ? "on" : "off", outputDesc1.MinLuminance, outputDesc1.MaxLuminance);
                    }
                    JUG_DX_RELEASE(pOutput6);
                }

                JUG_CORE_LOG_INFO("    output[{}] {} {}x{} at ({}, {}){}", i, j, deviceName, rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top, colorInfo);
            }
            JUG_DX_RELEASE(pOutput);
        }

        // 소프트웨어 어댑터는 제외함.
        if (!bSoftware && (!pBestAdapterOrNull || ad.DedicatedVideoMemory > bestMemory))
        {
            JUG_DX_RELEASE(pBestAdapterOrNull);
            bestMemory = ad.DedicatedVideoMemory;
            bestIndex  = i;
            bestName   = std::move(desc);

            pBestAdapterOrNull = pAdapter1;
            pAdapter1          = nullptr;
        }

        JUG_DX_RELEASE(pAdapter1);
    }

    if (pBestAdapterOrNull)
    {
        JUG_CORE_LOG_INFO("Using GPU[{}]: {}", bestIndex, bestName);

        JUG_DX_CHECK(pBestAdapterOrNull->QueryInterface(IID_PPV_ARGS(&m_pAdapterOrNull)));
        JUG_DISCARD_RETURN(pBestAdapterOrNull->EnumOutputs(0, &m_pOutputOrNull));
        JUG_DX_RELEASE(pBestAdapterOrNull);

        SetDxgiObjectName_(m_pAdapterOrNull, "JugGfx.DXGI.Adapter");
        SetDxgiObjectName_(m_pOutputOrNull, "JugGfx.DXGI.Output");
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
    , m_pOutputOrNull(std::exchange(_other.m_pOutputOrNull, nullptr))
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
        m_pOutputOrNull     = std::exchange(_other.m_pOutputOrNull, nullptr);
    }
    return *this;
}

ISwapChain* DXGI::CreateSwapChain(
    IUnknown*                    _pDevice,
    const HWND                   _hWnd,
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
    JUG_DX_CHECK(m_pFactory->CreateSwapChainForHwnd(_pDevice, _hWnd, &_desc, &scfd, nullptr, reinterpret_cast<IDXGISwapChain1**>(&pSwapChain)));
    return pSwapChain;
}

void DXGI::FillCaps(
    GraphicsCaps& _inoutCaps) const
{
    if (m_pAdapterOrNull)
    {
        DXGI_ADAPTER_DESC1 ad = {};
        if (SUCCEEDED(m_pAdapterOrNull->GetDesc1(&ad)))
        {
            _inoutCaps.vendorID               = ad.VendorId;
            _inoutCaps.deviceID               = ad.DeviceId;
            _inoutCaps.videoMemorySize        = ad.DedicatedVideoMemory;
            _inoutCaps.systemMemorySize       = ad.DedicatedSystemMemory;
            _inoutCaps.sharedSystemMemorySize = ad.SharedSystemMemory;
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

IOutput* DXGI::GetOutputOrNull() const
{
    return m_pOutputOrNull;
}

void DXGI::Reset_()
{
    JUG_DX_RELEASE(m_pOutputOrNull);
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