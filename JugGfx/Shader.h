#pragma once
#include "Base.h"

struct ISlangBlob;

namespace jug
{

// ===========================================
//  Shader
// ===========================================

// 컴파일 결과물의 형식. 그래픽스 API 마다 받아들이는 타깃이 정해져 있다.
enum class eShaderTarget
{
    DXBC,   // D3D11, sm_5_0
};

// 해당 API 가 받아들이는 셰이더 타깃. Noop 은 검사하지 않으므로 DXBC 로 컴파일한다.
[[nodiscard]] eShaderTarget GetShaderTarget(eGraphicsAPI _api);

// Slang compiler options
enum class eShaderCompileOption : uint32_t
{
    None = 0,

    // matrix layout (default: column-major, same as fxc)
    MatrixLayout_Column = 0 << 0,   // default
    MatrixLayout_Row    = 1 << 0,

    // optimization
    Optimization_Default = 0 << 1,   // default
    Optimization_None    = 1 << 1,
    Optimization_High    = 2 << 1,
    Optimization_Maximal = 3 << 1,

    // debug information
    DebugInfo_None     = 0 << 3,   // default
    DebugInfo_Minimal  = 1 << 3,
    DebugInfo_Standard = 2 << 3,
    DebugInfo_Maximal  = 3 << 3,

    // language
    ZeroInitialize      = 1 << 5,
    UnscopedEnum        = 1 << 6,
    DisableShortCircuit = 1 << 7,

    // capability
    IgnoreCapabilities         = 1 << 8,
    RestrictiveCapabilityCheck = 1 << 9,

    // codegen
    MinimumSlangOptimization = 1 << 10,
    DisableSpecialization    = 1 << 11,
    PreserveParameters       = 1 << 12,
    Obfuscate                = 1 << 13,
    NoMangle                 = 1 << 14,

    // layout
    NoHlslBinding                    = 1 << 15,
    NoHlslPackConstantBufferElements = 1 << 16,
};

[[nodiscard]] Flags<eShaderCompileOption> FilterMatrixLayout(Flags<eShaderCompileOption> _flags);
[[nodiscard]] Flags<eShaderCompileOption> FilterOptimization(Flags<eShaderCompileOption> _flags);
[[nodiscard]] Flags<eShaderCompileOption> FilterDebugInfo(Flags<eShaderCompileOption> _flags);

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
    eShader                     type         = eShader::Vertex;
    eShaderTarget               target       = eShaderTarget::DXBC;
    StringView                  entryPoint   = {};
    Span<const ShaderMacro>     macros       = {};
    Span<const StringView>      includePaths = {};
    Flags<eShaderCompileOption> flags        = eShaderCompileOption::None;
};

struct BlobDelete
{
    void operator()(ISlangBlob* _pBlob) const;
};

struct ShaderBytecode
{
    [[nodiscard]] MemoryView GetBytecode() const;

    Scoped<ISlangBlob, BlobDelete> pBlob  = nullptr;
    eShader                        type   = eShader::Vertex;       // 컴파일한 스테이지
    eShaderTarget                  target = eShaderTarget::DXBC;   // 컴파일한 타깃
};

[[nodiscard]] Result<ShaderBytecode> CompileShader(StringView _source, const ShaderCompileConfig& _config);
[[nodiscard]] Result<ShaderBytecode> CompileShaderFromFile(const FilePath& _path, const ShaderCompileConfig& _config);

}   // namespace jug