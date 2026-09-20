#pragma once
#include <JugX/OS.h>

#ifdef JUG_OS_WINDOWS
#    if NTDDI_VERSION >= NTDDI_WIN7 && WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_APP)
#        include <d3d11shader.h>
#        include <d3d11_4.h>
#        include <d3dcommon.h>
#        include <d3dcompiler.h>
#        include <d3dcompiler.h>
#        include <d3dcompiler.inl>
#        include <dxgi1_6.h>
#        pragma comment(lib, "d3d11.lib")
#        pragma comment(lib, "dxgi.lib")
#        pragma comment(lib, "dxguid.lib")
#        pragma comment(lib, "d3dcompiler.lib")
#        define JUG_GFX_DIRECTX11 1
#    else
#        error "DirectX 11 requires Windows 7 or later."
#    endif
#endif