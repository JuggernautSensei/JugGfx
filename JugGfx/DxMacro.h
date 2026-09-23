#pragma once

#define JUG_DX_CHECK(_func)                                                                                      \
    JUG_BEGIN_MACRO_BLOCK                                                                                        \
    if (const HRESULT _hr_ = _func; FAILED(_hr_))                                                                \
    {                                                                                                            \
        JUG_FATAL("DirectX call failed: {}, {}", #_func, MakeSystemError(_hr_, eSystemError::OS).MakeMessage()); \
    }                                                                                                            \
    JUG_END_MACRO_BLOCK

#define JUG_DX_RELEASE(_obj) \
    JUG_BEGIN_MACRO_BLOCK    \
    if (_obj)                \
    {                        \
        _obj->Release();     \
        _obj = nullptr;      \
    }                        \
    JUG_END_MACRO_BLOCK
