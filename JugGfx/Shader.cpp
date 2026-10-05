#include "pch.h"
#include "Shader.h"

#include "Error.h"

namespace jug
{

namespace
{

    [[nodiscard]] StringView GetModelName_(
        const eShader _type)
    {
        switch (_type)
        {
            case eShader::Vertex:
                return "vs_5_0";
            case eShader::Pixel:
                return "ps_5_0";
            case eShader::Compute:
                return "cs_5_0";
            default:
                JUG_ASSERT(false, "Unrecognized shader m_type");
                return "UnknownShader";
        }
    }

    [[nodiscard]] uint32_t ToD3dCompileFlags_(
        const Flags<eShaderCompileOption> _flags)
    {
        struct Mapping
        {
            eShaderCompileOption option = eShaderCompileOption::None;
            uint32_t             flag   = 0;
        };

        constexpr ARRAY<Mapping, 14> kTable = {
            {
             { eShaderCompileOption::Debug, D3DCOMPILE_DEBUG },
             { eShaderCompileOption::SkipValidation, D3DCOMPILE_SKIP_VALIDATION },
             { eShaderCompileOption::SkipOptimization, D3DCOMPILE_SKIP_OPTIMIZATION },
             { eShaderCompileOption::PackMatrixRowMajor, D3DCOMPILE_PACK_MATRIX_ROW_MAJOR },
             { eShaderCompileOption::PackMatrixColumnMajor, D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR },
             { eShaderCompileOption::PartialPrecision, D3DCOMPILE_PARTIAL_PRECISION },
             { eShaderCompileOption::ForceVertexShaderSoftwareNoOpt, D3DCOMPILE_FORCE_VS_SOFTWARE_NO_OPT },
             { eShaderCompileOption::ForcePixelShaderSoftwareNoOpt, D3DCOMPILE_FORCE_PS_SOFTWARE_NO_OPT },
             { eShaderCompileOption::NoPreshader, D3DCOMPILE_NO_PRESHADER },
             { eShaderCompileOption::AvoidFlowControl, D3DCOMPILE_AVOID_FLOW_CONTROL },
             { eShaderCompileOption::PreferFlowControl, D3DCOMPILE_PREFER_FLOW_CONTROL },
             { eShaderCompileOption::EnableStrictness, D3DCOMPILE_ENABLE_STRICTNESS },
             { eShaderCompileOption::EnableBackwardsCompatibility, D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY },
             { eShaderCompileOption::IeeeStrictness, D3DCOMPILE_IEEE_STRICTNESS },
             }
        };

        uint32_t flags = 0;
        for (const Mapping mapping: kTable)
        {
            if (_flags & mapping.option)
            {
                flags |= mapping.flag;
            }
        }
        return flags;
    }

    // d3d macro must be null-terminated.
    void FillMacroBuffer(
        const Span<const ShaderMacro> _macros,
        D3D_SHADER_MACRO*             _pMacro)
    {
        JUG_ASSERT(!_macros.empty(), "Macro buffer must not be empty");

        for (size_t i = 0; i < _macros.size(); ++i)
        {
            _pMacro[i] = { _macros[i].name.data(), _macros[i].value.data() };
        }
        _pMacro[_macros.size()] = { nullptr, nullptr };
    }

    [[nodiscard]] Error CheckCompileResult_(
        const HRESULT              _hr,
        ID3DBlob*                  _pBlobOrNull,
        ID3DBlob*                  _pErrorOrNull,
        const ShaderCompileConfig& _config,
        const StringView           _debugHint)
    {
        const StringView target = GetModelName_(_config.type);

        StringView errorMsg = {};
        if (_pErrorOrNull)
        {
            errorMsg = { static_cast<const char*>(_pErrorOrNull->GetBufferPointer()), static_cast<size_t>(_pErrorOrNull->GetBufferSize()) };
        }

        Error error = kOK;
        if (FAILED(_hr))
        {
            // 실패
            JUG_CORE_LOG_ERROR(
                "Shader compilation failed: {} '{}' ({}, {} macros)\n"
                "{}",
                _debugHint,
                _config.entryPoint,
                target,
                _config.macros.size(),
                _pErrorOrNull ? errorMsg : MakeSystemError(_hr, eSystemError::OS).MakeMessage());

            error = eGraphicsError::ShaderCompileFailed;
        }
        else
        {
            // 실패는 안했으나 경고
            if (_pErrorOrNull)
            {
                JUG_CORE_LOG_WARN(
                    "Shader compiled with warnings: {} '{}' ({})\n{}",
                    _debugHint,
                    _config.entryPoint,
                    target,
                    errorMsg);
            }

            // 성공 로깅
            JUG_CORE_LOG_TRACE(
                "Shader compiled: {} '{}' ({}), {} bytes",
                _debugHint,
                _config.entryPoint,
                target,
                _pBlobOrNull->GetBufferSize());
        }

        if (_pErrorOrNull)
        {
            _pErrorOrNull->Release();
        }

        return error;
    }

    // 컴파일 결과를 ShaderBytecode 로 전달 (blob 소유권 이전)
    [[nodiscard]] Result<ShaderBytecode> MakeShaderBytecode_(
        const HRESULT              _hr,
        ID3DBlob*                  _pBlobOrNull,
        ID3DBlob*                  _pErrorOrNull,
        const ShaderCompileConfig& _config,
        const StringView           _debugHint)
    {
        const Error error = CheckCompileResult_(_hr, _pBlobOrNull, _pErrorOrNull, _config, _debugHint);
        if (error.IsError())
        {
            if (_pBlobOrNull)
            {
                _pBlobOrNull->Release();
            }
            return error;
        }

        ShaderBytecode shader = {};
        shader.pBlob.Reset(_pBlobOrNull);
        return std::move(shader);
    }

}   // namespace

Result<ShaderBytecode> CompileShader(
    const StringView           _source,
    const ShaderCompileConfig& _config)
{
    constexpr StringView kDebugHint = "<memory>";

    D3D_SHADER_MACRO* pMacroOrNull = nullptr;
    if (!_config.macros.empty())
    {
        pMacroOrNull = static_cast<D3D_SHADER_MACRO*>(JUG_STACK_ALLOC(sizeof(D3D_SHADER_MACRO) * (_config.macros.size() + 1)));
        FillMacroBuffer(_config.macros, pMacroOrNull);
    }

    ID3DBlob*     pBlobOrNull = nullptr;
    ID3DBlob*     pErrorOrNull;
    const HRESULT hr = ::D3DCompile(
        _source.data(),
        _source.size(),
        kDebugHint.data(),
        pMacroOrNull,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        _config.entryPoint.data(),
        GetModelName_(_config.type).data(),
        ToD3dCompileFlags_(_config.flags),
        0,
        &pBlobOrNull,
        &pErrorOrNull);

    return MakeShaderBytecode_(hr, pBlobOrNull, pErrorOrNull, _config, kDebugHint.data());
}

Result<ShaderBytecode> CompileShaderFromFile(
    const FilePath&            _path,
    const ShaderCompileConfig& _config)
{
    D3D_SHADER_MACRO* pMacroOrNull = nullptr;
    if (!_config.macros.empty())
    {
        pMacroOrNull = static_cast<D3D_SHADER_MACRO*>(JUG_STACK_ALLOC(sizeof(D3D_SHADER_MACRO) * (_config.macros.size() + 1)));
        FillMacroBuffer(_config.macros, pMacroOrNull);
    }

    ID3DBlob*     pBlobOrNull = nullptr;
    ID3DBlob*     pErrorOrNull;
    const HRESULT hr = ::D3DCompileFromFile(
        _path.c_str(),
        pMacroOrNull,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        _config.entryPoint.data(),
        GetModelName_(_config.type).data(),
        ToD3dCompileFlags_(_config.flags),
        0,
        &pBlobOrNull,
        &pErrorOrNull);

    return MakeShaderBytecode_(hr, pBlobOrNull, pErrorOrNull, _config, _path.string());
}

}   // namespace jug


