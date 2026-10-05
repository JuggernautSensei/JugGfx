#pragma once
#include "Base.h"

namespace jug
{

// ===========================================
//  Shader
// ===========================================

enum class eShaderCompileOption
{
    None                           = 0,
    Debug                          = 1 << 0,    // Compile with debug information and skip optimizations
    SkipValidation                 = 1 << 1,    // Skip validation of the shader code
    SkipOptimization               = 1 << 2,    // Skip optimization of the shader code
    PackMatrixRowMajor             = 1 << 3,    // Pack matrices in row-major order
    PackMatrixColumnMajor          = 1 << 4,    // Pack matrices in column-major order
    PartialPrecision               = 1 << 5,    // Use partial precision for floating-point operations
    ForceVertexShaderSoftwareNoOpt = 1 << 6,    // Force the vertex shader to be compiled for software execution without optimizations
    ForcePixelShaderSoftwareNoOpt  = 1 << 7,    // Force the pixel shader to be compiled for software execution without optimizations
    NoPreshader                    = 1 << 8,    // Disable the preshader optimization
    AvoidFlowControl               = 1 << 9,    // Avoid flow control constructs in the shader code
    PreferFlowControl              = 1 << 10,   // Prefer flow control constructs in the shader code
    EnableStrictness               = 1 << 11,   // Enable strictness checks during shader compilation
    EnableBackwardsCompatibility   = 1 << 12,   // Enable backwards compatibility mode for shader compilation
    IeeeStrictness                 = 1 << 13,   // Enable IEEE strictness for floating-point operations
};

struct ShaderMacro
{
    [[nodiscard]] bool IsNull() const
    {
        return name.empty() && value.empty();
    }

    StringView name  = {};
    StringView value = {};
};

struct ShaderCompileConfig
{
    eShader                     type       = eShader::Vertex;
    StringView                  entryPoint = {};
    Span<const ShaderMacro>     macros     = {};
    Flags<eShaderCompileOption> flags      = eShaderCompileOption::None;
};

struct BlobDelete
{
    void operator()(void* _p) const
    {
        if (_p)
        {
            static_cast<ID3DBlob*>(_p)->Release();
        }
    }
};

struct ShaderBytecode
{
    [[nodiscard]] MemoryView GetBytecode() const
    {
        JUG_ASSERT(pBlob, "Shader pBlob is null.\n");
        return { pBlob->GetBufferPointer(), pBlob->GetBufferSize() };
    }

    Scoped<ID3DBlob, BlobDelete> pBlob = nullptr;
};

[[nodiscard]] Result<ShaderBytecode> CompileShader(StringView _source, const ShaderCompileConfig& _config);
[[nodiscard]] Result<ShaderBytecode> CompileShaderFromFile(const FilePath& _path, const ShaderCompileConfig& _config);

}   // namespace jug