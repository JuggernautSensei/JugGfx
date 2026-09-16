#pragma once
#include <JugX/OS.h>

#include "Base.h"

namespace jug
{

using IAdapter   = IDXGIAdapter3;
using IFactory   = IDXGIFactory5;
using ISwapChain = IDXGISwapChain3;
using IOutput    = IDXGIOutput;

class DXGI
{
    JUG_CLASS(DXGI, NO_COPY)

public:
    DXGI();
    ~DXGI();

    DXGI(DXGI&& _other) noexcept;
    DXGI& operator=(DXGI&& _other) noexcept;

    ISwapChain* CreateSwapChain(IUnknown* _pDevice, void* _pWindow, bool _bWindowed, const DXGI_SWAP_CHAIN_DESC1& _desc) const;
    void        UpdateCaps(GraphicsCaps& _inoutCaps) const;

    [[nodiscard]] IAdapter* GetAdapterOrNull() const;
    [[nodiscard]] IFactory* GetFactory() const;
    [[nodiscard]] IOutput*  GetOutput() const;

private:
    void Reset_();

    SDL_SharedObject* m_pDxgiDll          = nullptr;
    SDL_SharedObject* m_pDxgiDbgDllOrNull = nullptr;

    IAdapter* m_pAdapterOrNull = nullptr;
    IFactory* m_pFactory       = nullptr;
    IOutput*  m_pOutput        = nullptr;
};

}   // namespace jug