#include "pch.h"
#include "Shader.h"

#include "Error.h"

#include <slang-com-ptr.h>
#include <slang.h>

namespace jug
{

namespace
{
    constexpr Flags<eShaderCompileOption> kMatrixLayoutMask { MakeFieldMask(eShaderCompileOption::MatrixLayout_Row, eShaderCompileOption::MatrixLayout_Row) };
    constexpr Flags<eShaderCompileOption> kOptimizationMask { MakeFieldMask(eShaderCompileOption::Optimization_None, eShaderCompileOption::Optimization_Maximal) };
    constexpr Flags<eShaderCompileOption> kDebugInfoMask { MakeFieldMask(eShaderCompileOption::DebugInfo_Minimal, eShaderCompileOption::DebugInfo_Maximal) };

    // 택일 그룹 값(ExtractField 번호) -> Slang 값
    constexpr ARRAY<SlangOptimizationLevel, 4> kOptimizationLevels = {
        { SLANG_OPTIMIZATION_LEVEL_DEFAULT, SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_HIGH, SLANG_OPTIMIZATION_LEVEL_MAXIMAL }
    };
    constexpr ARRAY<SlangDebugInfoLevel, 4> kDebugInfoLevels = {
        { SLANG_DEBUG_INFO_LEVEL_NONE, SLANG_DEBUG_INFO_LEVEL_MINIMAL, SLANG_DEBUG_INFO_LEVEL_STANDARD, SLANG_DEBUG_INFO_LEVEL_MAXIMAL }
    };

    // 단일 비트 옵션. 택일 그룹은 FillOptionEntries_ 에서 따로 처리
    struct BoolOption
    {
        eShaderCompileOption      flag = eShaderCompileOption::None;
        slang::CompilerOptionName name = slang::CompilerOptionName::MacroDefine;
    };

    constexpr ARRAY<BoolOption, 12> kBoolOptions = {
        {
         { eShaderCompileOption::ZeroInitialize, slang::CompilerOptionName::ZeroInitialize },
         { eShaderCompileOption::UnscopedEnum, slang::CompilerOptionName::UnscopedEnum },
         { eShaderCompileOption::DisableShortCircuit, slang::CompilerOptionName::DisableShortCircuit },
         { eShaderCompileOption::IgnoreCapabilities, slang::CompilerOptionName::IgnoreCapabilities },
         { eShaderCompileOption::RestrictiveCapabilityCheck, slang::CompilerOptionName::RestrictiveCapabilityCheck },
         { eShaderCompileOption::MinimumSlangOptimization, slang::CompilerOptionName::MinimumSlangOptimization },
         { eShaderCompileOption::DisableSpecialization, slang::CompilerOptionName::DisableSpecialization },
         { eShaderCompileOption::PreserveParameters, slang::CompilerOptionName::PreserveParameters },
         { eShaderCompileOption::Obfuscate, slang::CompilerOptionName::Obfuscate },
         { eShaderCompileOption::NoMangle, slang::CompilerOptionName::NoMangle },
         { eShaderCompileOption::NoHlslBinding, slang::CompilerOptionName::NoHLSLBinding },
         { eShaderCompileOption::NoHlslPackConstantBufferElements, slang::CompilerOptionName::NoHLSLPackConstantBufferElements },
         }
    };

    // 택일 그룹 3개 + 단일 비트 옵션
    constexpr uint32_t kNumMaxOptionEntries = 3 + static_cast<uint32_t>(kBoolOptions.size());

    [[nodiscard]] slang::CompilerOptionEntry MakeOptionEntry_(
        const slang::CompilerOptionName _name,
        const int32_t                   _value)
    {
        slang::CompilerOptionEntry entry = {};
        entry.name                       = _name;
        entry.value.kind                 = slang::CompilerOptionValueKind::Int;
        entry.value.intValue0            = _value;
        return entry;
    }

    // _pEntries 는 kNumMaxOptionEntries 개 이상. 채운 개수 반환.
    [[nodiscard]] uint32_t FillOptionEntries_(
        const Flags<eShaderCompileOption> _flags,
        slang::CompilerOptionEntry*       _pEntries)
    {
        uint32_t numEntries = 0;

        const bool bRow         = _flags.Has(eShaderCompileOption::MatrixLayout_Row);
        _pEntries[numEntries++] = MakeOptionEntry_(bRow ? slang::CompilerOptionName::MatrixLayoutRow : slang::CompilerOptionName::MatrixLayoutColumn, 1);
        _pEntries[numEntries++] = MakeOptionEntry_(slang::CompilerOptionName::Optimization, kOptimizationLevels[ExtractField(_flags, kOptimizationMask)]);
        _pEntries[numEntries++] = MakeOptionEntry_(slang::CompilerOptionName::DebugInformation, kDebugInfoLevels[ExtractField(_flags, kDebugInfoMask)]);

        // single bit options
        for (const BoolOption option: kBoolOptions)
        {
            if (_flags.Has(option.flag))
            {
                _pEntries[numEntries++] = MakeOptionEntry_(option.name, 1);
            }
        }
        return numEntries;
    }

    [[nodiscard]] SlangStage ToSlangStage_(
        const eShader _type)
    {
        switch (_type)
        {
            case eShader::Vertex:
                return SLANG_STAGE_VERTEX;
            case eShader::Pixel:
                return SLANG_STAGE_PIXEL;
            case eShader::Compute:
                return SLANG_STAGE_COMPUTE;
            default:
                JUG_ASSERT(false, "Unrecognized shader type");
                return SLANG_STAGE_NONE;
        }
    }

    // IGlobalSession 은 thread-safe 하지 않음. 컴파일은 단일 스레드에서만.
    [[nodiscard]] slang::IGlobalSession* GetGlobalSession_()
    {
        static Slang::ComPtr<slang::IGlobalSession> s_pGlobalSession;
        if (!s_pGlobalSession && SLANG_FAILED(::slang_createGlobalSession(SLANG_API_VERSION, s_pGlobalSession.writeRef())))
        {
            JUG_FATAL("Failed to create the Slang global session.");
        }
        return s_pGlobalSession.get();
    }

    // 단계별 결과 확인 + 진단 로깅 (실패 error, 경고 warn)
    [[nodiscard]] Error CheckStep_(
        const bool                 _bSucceeded,
        slang::IBlob*              _pDiagnosticsOrNull,
        const ShaderCompileConfig& _config,
        const char*                _pPath)
    {
        StringView diagnostics = {};
        if (_pDiagnosticsOrNull)
        {
            diagnostics = { static_cast<const char*>(_pDiagnosticsOrNull->getBufferPointer()), _pDiagnosticsOrNull->getBufferSize() };
        }

        if (!_bSucceeded)
        {
            JUG_CORE_LOG_ERROR(
                "Shader compilation failed: {} '{}' ({} macros)\n"
                "{}",
                _pPath,
                _config.entryPoint,
                _config.macros.size(),
                diagnostics);
            return eGraphicsError::ShaderCompileFailed;
        }

        if (!diagnostics.empty())
        {
            JUG_CORE_LOG_WARN(
                "Shader compiled with warnings: {} '{}'\n{}",
                _pPath,
                _config.entryPoint,
                diagnostics);
        }
        return kOK;
    }

    // _bFromFile 이면 _pPath 파일에서 읽고(_source 무시), 아니면 _source 를 컴파일 (비어 있으면 안 됨).
    // _pFileDirOrNull 은 include 탐색 경로에 추가.
    // entryPoint, macro, includePath, _pPath 는 null-terminated 여야 함.
    [[nodiscard]] Result<ShaderBytecode> Compile_(
        const bool                 _bFromFile,
        const StringView           _source,
        const char*                _pPath,
        const char*                _pFileDirOrNull,
        const ShaderCompileConfig& _config)
    {
        slang::IGlobalSession* pGlobalSession = GetGlobalSession_();

        // search paths
        const size_t numSearchPaths = _config.includePaths.size() + (_pFileDirOrNull ? 1 : 0);
        const char** ppSearchPaths  = static_cast<const char**>(JUG_STACK_ALLOC(sizeof(const char*) * numSearchPaths));
        for (size_t i = 0; i < _config.includePaths.size(); ++i)
        {
            ppSearchPaths[i] = _config.includePaths[i].data();
        }
        if (_pFileDirOrNull)
        {
            ppSearchPaths[numSearchPaths - 1] = _pFileDirOrNull;
        }

        // macros
        slang::PreprocessorMacroDesc* pMacros = static_cast<slang::PreprocessorMacroDesc*>(JUG_STACK_ALLOC(sizeof(slang::PreprocessorMacroDesc) * _config.macros.size()));
        for (size_t i = 0; i < _config.macros.size(); ++i)
        {
            pMacros[i] = { _config.macros[i].name.data(), _config.macros[i].value.data() };
        }

        // options
        slang::CompilerOptionEntry* pEntries   = static_cast<slang::CompilerOptionEntry*>(JUG_STACK_ALLOC(sizeof(slang::CompilerOptionEntry) * kNumMaxOptionEntries));
        const uint32_t              numEntries = FillOptionEntries_(_config.flags, pEntries);

        // session
        slang::TargetDesc targetDesc = {};
        targetDesc.format            = SLANG_DXBC;
        targetDesc.profile           = pGlobalSession->findProfile("sm_5_0");
        JUG_ASSERT(targetDesc.profile != SLANG_PROFILE_UNKNOWN, "Slang does not know the sm_5_0 profile.");

        slang::SessionDesc sessionDesc       = {};
        sessionDesc.targets                  = &targetDesc;
        sessionDesc.targetCount              = 1;
        sessionDesc.searchPaths              = ppSearchPaths;
        sessionDesc.searchPathCount          = static_cast<SlangInt>(numSearchPaths);
        sessionDesc.preprocessorMacros       = pMacros;
        sessionDesc.preprocessorMacroCount   = static_cast<SlangInt>(_config.macros.size());
        sessionDesc.compilerOptionEntries    = pEntries;
        sessionDesc.compilerOptionEntryCount = numEntries;

        Slang::ComPtr<slang::ISession> pSession;
        const SlangResult              sessionResult = pGlobalSession->createSession(sessionDesc, pSession.writeRef());
        JUG_RETURN_IF_ERROR(CheckStep_(SLANG_SUCCEEDED(sessionResult), nullptr, _config, _pPath));

        // module
        constexpr const char*       kModuleName = "ShaderModule";
        Slang::ComPtr<slang::IBlob> pDiagnostics;
        slang::IModule*             pModuleOrNull = nullptr;
        if (_bFromFile)
        {
            pModuleOrNull = pSession->loadModuleFromSource(kModuleName, _pPath, nullptr, pDiagnostics.writeRef());
        }
        else
        {
            pModuleOrNull = ::slang_loadModuleFromSource(pSession.get(), kModuleName, _pPath, _source.data(), _source.size(), pDiagnostics.writeRef());
        }
        JUG_RETURN_IF_ERROR(CheckStep_(pModuleOrNull != nullptr, pDiagnostics.get(), _config, _pPath));

        // entry point
        Slang::ComPtr<slang::IEntryPoint> pEntryPoint;
        const SlangResult                 entryPointResult = pModuleOrNull->findAndCheckEntryPoint(_config.entryPoint.data(), ToSlangStage_(_config.type), pEntryPoint.writeRef(), pDiagnostics.writeRef());
        JUG_RETURN_IF_ERROR(CheckStep_(SLANG_SUCCEEDED(entryPointResult), pDiagnostics.get(), _config, _pPath));

        // compose & link
        const ARRAY<slang::IComponentType*, 2> components = { pModuleOrNull, pEntryPoint.get() };

        Slang::ComPtr<slang::IComponentType> pComposite;
        const SlangResult                    compositeResult = pSession->createCompositeComponentType(components.data(), components.size(), pComposite.writeRef(), pDiagnostics.writeRef());
        JUG_RETURN_IF_ERROR(CheckStep_(SLANG_SUCCEEDED(compositeResult), pDiagnostics.get(), _config, _pPath));

        Slang::ComPtr<slang::IComponentType> pLinked;
        const SlangResult                    linkResult = pComposite->link(pLinked.writeRef(), pDiagnostics.writeRef());
        JUG_RETURN_IF_ERROR(CheckStep_(SLANG_SUCCEEDED(linkResult), pDiagnostics.get(), _config, _pPath));

        // code
        Slang::ComPtr<slang::IBlob> pCode;
        const SlangResult           codeResult = pLinked->getEntryPointCode(0, 0, pCode.writeRef(), pDiagnostics.writeRef());
        JUG_RETURN_IF_ERROR(CheckStep_(SLANG_SUCCEEDED(codeResult), pDiagnostics.get(), _config, _pPath));

        JUG_CORE_LOG_TRACE(
            "Shader compiled: {} '{}', {} bytes",
            _pPath,
            _config.entryPoint,
            pCode->getBufferSize());

        // blob 소유권 이전
        ShaderBytecode shader = {};
        shader.pBlob.Reset(pCode.detach());
        return std::move(shader);
    }

}   // namespace

// ===========================================
//  Compile Options
// ===========================================

Flags<eShaderCompileOption> FilterMatrixLayout(
    const Flags<eShaderCompileOption> _flags)
{
    return _flags & kMatrixLayoutMask;
}

Flags<eShaderCompileOption> FilterOptimization(
    const Flags<eShaderCompileOption> _flags)
{
    return _flags & kOptimizationMask;
}

Flags<eShaderCompileOption> FilterDebugInfo(
    const Flags<eShaderCompileOption> _flags)
{
    return _flags & kDebugInfoMask;
}

// ===========================================
//  Shader
// ===========================================

void BlobDelete::operator()(
    ISlangBlob* _pBlob) const
{
    _pBlob->release();
}

MemoryView ShaderBytecode::GetBytecode() const
{
    JUG_ASSERT(pBlob, "Shader pBlob is null.\n");
    return { pBlob->getBufferPointer(), pBlob->getBufferSize() };
}

Result<ShaderBytecode> CompileShader(
    const StringView           _source,
    const ShaderCompileConfig& _config)
{
    // Slang 은 빈 소스를 "소스 없음" 으로 보고 파일을 열려 하므로 직접 거절
    if (_source.empty())
    {
        JUG_CORE_LOG_ERROR("Shader compilation failed: the source is empty '{}'", _config.entryPoint);
        return eGraphicsError::ShaderCompileFailed;
    }
    return Compile_(false, _source, "<memory>.slang", nullptr, _config);
}

Result<ShaderBytecode> CompileShaderFromFile(
    const FilePath&            _path,
    const ShaderCompileConfig& _config)
{
    const String path = _path.string();
    const String dir  = _path.parent_path().string();
    return Compile_(true, {}, path.c_str(), dir.c_str(), _config);
}

}   // namespace jug