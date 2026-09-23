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

}   // namespace

Shader::~Shader()
{
    if (m_pBlob)
    {
        m_pBlob->Release();
        m_pBlob = nullptr;
    }
}

Shader::Shader(
    Shader&& _other) noexcept
    : m_pBlob(std::exchange(_other.m_pBlob, nullptr))
{
}

Shader& Shader::operator=(
    Shader&& _other) noexcept
{
    if (this != &_other)
    {
        if (m_pBlob)
        {
            m_pBlob->Release();
        }
        m_pBlob = std::exchange(_other.m_pBlob, nullptr);
    }
    return *this;
}

Result<Shader> Shader::Compile(
    const StringView         _source,
    const ShaderCompileDesc& _desc)
{
    Shader      shader = {};
    const Error err    = shader.CompileFromSource_(_source, _desc);
    if (err.IsError())
    {
        return err;
    }
    return shader;
}

Result<Shader> Shader::CompileFromFile(
    const FilePath&          _filePath,
    const ShaderCompileDesc& _desc)
{
    Shader      shader = {};
    const Error err    = shader.CompileFromFile_(_filePath, _desc);
    if (err.IsError())
    {
        return err;
    }
    return shader;
}

Error Shader::SaveToFile(
    const FilePath& _filePath) const
{
    Result<FileWriter> writer = FileWriter::Open(_filePath);
    JUG_RETURN_IF_ERROR(writer);
    JUG_RETURN_IF_ERROR(writer->Write(GetByteCode()));
    return kOK;
}

MemoryView Shader::GetByteCode() const
{
    return { static_cast<std::byte*>(m_pBlob->GetBufferPointer()), static_cast<size_t>(m_pBlob->GetBufferSize()) };
}

Error Shader::CompileFromFile_(
    const FilePath&          _filePath,
    const ShaderCompileDesc& _desc) const
{
    Result<FileReader> reader = FileReader::Open(_filePath);
    JUG_RETURN_IF_ERROR(reader);

    String str(reader->GetSize(), '\0');
    JUG_RETURN_IF_ERROR(reader->Read(str));
    return CompileFromFile_(str, _desc);
}

Error Shader::CompileFromSource_(
    const StringView         _source,
    const ShaderCompileDesc& _desc)
{
    // make d3d macro array
    const bool   bNeedNullTerm = !_desc.macros.empty() && !_desc.macros.back().IsNull();
    const size_t numMacros     = _desc.macros.empty() ? 0 : _desc.macros.size() + (bNeedNullTerm ? 1 : 0);

    D3D_SHADER_MACRO* pMacro = nullptr;
    if (numMacros > 0)
    {
        pMacro = static_cast<D3D_SHADER_MACRO*>(JUG_STACK_ALLOC(sizeof(D3D_SHADER_MACRO) * numMacros));
        for (size_t i = 0; i < _desc.macros.size(); ++i)
        {
            pMacro[i].Name       = _desc.macros[i].name.data();
            pMacro[i].Definition = _desc.macros[i].value.data();
        }

        if (bNeedNullTerm)
        {
            pMacro[numMacros - 1].Name       = nullptr;
            pMacro[numMacros - 1].Definition = nullptr;
        }
    }

    // make flag
    uint32_t flags = 0;
    if (_desc.flags & eShaderCompileOption::Debug)
    {
        flags |= D3DCOMPILE_DEBUG;
    }

    if (_desc.flags & eShaderCompileOption::SkipValidation)
    {
        flags |= D3DCOMPILE_SKIP_VALIDATION;
    }

    if (_desc.flags & eShaderCompileOption::SkipOptimization)
    {
        flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
    }

    if (_desc.flags & eShaderCompileOption::PackMatrixRowMajor)
    {
        flags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
    }

    if (_desc.flags & eShaderCompileOption::PackMatrixColumnMajor)
    {
        flags |= D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
    }

    if (_desc.flags & eShaderCompileOption::PartialPrecision)
    {
        flags |= D3DCOMPILE_PARTIAL_PRECISION;
    }

    if (_desc.flags & eShaderCompileOption::ForceVertexShaderSoftwareNoOpt)
    {
        flags |= D3DCOMPILE_FORCE_VS_SOFTWARE_NO_OPT;
    }

    if (_desc.flags & eShaderCompileOption::ForcePixelShaderSoftwareNoOpt)
    {
        flags |= D3DCOMPILE_FORCE_PS_SOFTWARE_NO_OPT;
    }

    if (_desc.flags & eShaderCompileOption::NoPreshader)
    {
        flags |= D3DCOMPILE_NO_PRESHADER;
    }

    if (_desc.flags & eShaderCompileOption::AvoidFlowControl)
    {
        flags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
    }

    if (_desc.flags & eShaderCompileOption::PreferFlowControl)
    {
        flags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }

    if (_desc.flags & eShaderCompileOption::EnableStrictness)
    {
        flags |= D3DCOMPILE_ENABLE_STRICTNESS;
    }

    if (_desc.flags & eShaderCompileOption::EnableBackwardsCompatibility)
    {
        flags |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
    }

    if (_desc.flags & eShaderCompileOption::IeeeStrictness)
    {
        flags |= D3DCOMPILE_IEEE_STRICTNESS;
    }

    ID3DBlob*     pErrorBlob = nullptr;
    const HRESULT hr         = ::D3DCompile(_source.data(),_source.size(),
        nullptr,
        pMacro,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        _desc.entryPoint.data(),
        GetModelName_(_desc.type).data(),
        flags,
        0,
        &m_pBlob,
        &pErrorBlob);

    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            const char* msg = static_cast<const char*>(pErrorBlob->GetBufferPointer());
            JUG_CORE_LOG_ERROR("Shader compilation failed: {}", msg);
            pErrorBlob->Release();
        }
        else
        {
            JUG_CORE_LOG_ERROR("Shader compilation failed: {}", MakeSystemError(hr, eSystemError::OS).MakeMessage());
        }

        return eGraphicsError::ShaderCompileFailed;
    }

    return kOK;
}

}   // namespace jug