#include "pch.h"
#include "ModelImporter.h"

#include <algorithm>
#include <cctype>

#include "Error.h"
#include "ModelDetail.h"

namespace jug
{

namespace
{

    // ===========================================
    //  Config
    // ===========================================

    Flags<eModelImportOption> ResolveFlags_(
        const Flags<eModelImportOption> _flags)
    {
        Flags<eModelImportOption> flags = _flags;

        // 상위 옵션이 하위 옵션을 포함한다.
        if (flags.Has(eModelImportOption::SkipMeshes))
        {
            flags.Enable(eModelImportOption::SkipMaterials);
            flags.Enable(eModelImportOption::SkipSkins);
            flags.Enable(eModelImportOption::SkipMorphTargets);
        }
        if (flags.Has(eModelImportOption::SkipMaterials))
        {
            flags.Enable(eModelImportOption::SkipTextures);
        }
        return flags;
    }

    Optional<eModelFileFormat> FormatFromExtension_(
        const FilePath& _path)
    {
        // compare ignoring case
        String ext = _path.extension().string();
        for (char& c: ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (ext == ".obj")
        {
            return eModelFileFormat::Obj;
        }
        if (ext == ".fbx")
        {
            return eModelFileFormat::Fbx;
        }
        if (ext == ".gltf")
        {
            return eModelFileFormat::Gltf;
        }
        if (ext == ".glb")
        {
            return eModelFileFormat::Glb;
        }
        return kNullopt;
    }

    struct FormatDetection
    {
        Optional<eModelFileFormat> format   = kNullopt;
        bool                       bCertain = false;   // 시그니처로 확정되었는가 (false 면 휴리스틱)
    };

    constexpr size_t kFormatSniffSize = 4096;

    [[nodiscard]] bool StartsWith_(
        const StringView _text,
        const StringView _prefix)
    {
        return _text.size() >= _prefix.size() && _text.substr(0, _prefix.size()) == _prefix;
    }

    [[nodiscard]] bool IsAsciiSpace_(
        const char _c)
    {
        return _c == ' ' || _c == '\t' || _c == '\r' || _c == '\n' || _c == '\f' || _c == '\v';
    }

    [[nodiscard]] StringView SkipLeadingBlanks_(
        const StringView _text)
    {
        size_t i = 0;
        while (i < _text.size() && (_text[i] == ' ' || _text[i] == '\t'))
        {
            ++i;
        }
        return _text.substr(i);
    }

    // 텍스트의 각 줄을 검사한다. 마지막 줄은 잘렸을 수 있으나 접두사 검사에는 무방하다.
    [[nodiscard]] bool HasAsciiFbxMarker_(
        const StringView _text)
    {
        if (_text.find("FBXHeaderExtension") != StringView::npos)
        {
            return true;
        }

        size_t pos = 0;
        while (pos < _text.size())
        {
            const size_t     end  = _text.find('\n', pos);
            const size_t     len  = (end == StringView::npos ? _text.size() : end) - pos;
            const StringView line = SkipLeadingBlanks_(_text.substr(pos, len));
            if (StartsWith_(line, "; FBX"))
            {
                return true;
            }
            if (end == StringView::npos)
            {
                break;
            }
            pos = end + 1;
        }
        return false;
    }

    [[nodiscard]] bool LooksLikeObj_(
        const StringView _text)
    {
        constexpr ARRAY<StringView, 8> kObjLinePrefixes = {
            "v ", "vn ", "vt ", "f ", "o ", "g ", "mtllib", "usemtl"
        };

        size_t pos = 0;
        while (pos < _text.size())
        {
            const size_t     end  = _text.find('\n', pos);
            const size_t     len  = (end == StringView::npos ? _text.size() : end) - pos;
            const StringView line = SkipLeadingBlanks_(_text.substr(pos, len));
            for (const StringView prefix: kObjLinePrefixes)
            {
                if (StartsWith_(line, prefix))
                {
                    return true;
                }
            }
            if (end == StringView::npos)
            {
                break;
            }
            pos = end + 1;
        }
        return false;
    }

    FormatDetection DetectFormatFromContent_(
        const MemoryView _mem)
    {
        FormatDetection result = {};

        const char* const pBytes = reinterpret_cast<const char*>(_mem.GetPtr());
        const StringView  all(pBytes, _mem.GetSize());

        if (StartsWith_(all, "glTF"))
        {
            result.format   = eModelFileFormat::Glb;
            result.bCertain = true;
            return result;
        }
        if (StartsWith_(all, "Kaydara FBX Binary"))
        {
            result.format   = eModelFileFormat::Fbx;
            result.bCertain = true;
            return result;
        }

        // UTF-8 BOM 과 공백을 건너뛴 첫 문자가 '{' 이면 glTF JSON
        StringView body = all;
        if (StartsWith_(body, "\xEF\xBB\xBF"))
        {
            body.remove_prefix(3);
        }
        size_t first = 0;
        while (first < body.size() && IsAsciiSpace_(body[first]))
        {
            ++first;
        }
        if (first < body.size() && body[first] == '{')
        {
            result.format   = eModelFileFormat::Gltf;
            result.bCertain = true;
            return result;
        }

        const StringView sniff = body.substr(0, std::min(body.size(), kFormatSniffSize));
        if (HasAsciiFbxMarker_(sniff))
        {
            result.format   = eModelFileFormat::Fbx;
            result.bCertain = true;
            return result;
        }
        if (LooksLikeObj_(sniff))
        {
            result.format   = eModelFileFormat::Obj;
            result.bCertain = false;
            return result;
        }
        return result;
    }

    // 내용으로 확정 > 힌트 > OBJ 휴리스틱 순으로 포맷을 결정한다.
    Result<eModelFileFormat> ResolveFormat_(
        const MemoryView                 _mem,
        const Optional<eModelFileFormat> _formatHint,
        const char* const                _pDebugHint)
    {
        const FormatDetection detection = DetectFormatFromContent_(_mem);

        if (detection.bCertain)
        {
            if (_formatHint.HasValue() && *_formatHint != *detection.format)
            {
                JUG_CORE_LOG_WARN("Model format hint mismatch ({}): content is {} but hint is {}, using content.",
                                  _pDebugHint,
                                  NameOf(*detection.format),
                                  NameOf(*_formatHint));
            }
            return *detection.format;
        }

        if (_formatHint.HasValue())
        {
            return *_formatHint;
        }
        if (detection.format.HasValue())
        {
            return *detection.format;
        }

        JUG_CORE_LOG_ERROR("Failed to import model: unable to determine format ({}).", _pDebugHint);
        return eGraphicsError::ModelUnsupportedFormat;
    }

    [[nodiscard]] bool IsGltfFormat_(
        const eModelFileFormat _format)
    {
        return _format == eModelFileFormat::Gltf || _format == eModelFileFormat::Glb;
    }

}   // namespace

// ===========================================
//  Import
// ===========================================

Result<Model> ImportModel(
    const MemoryView         _mem,
    const ModelImportConfig& _config)
{
    JUG_ASSERT(_config.unitScale > 0.f, "ModelImportConfig::unitScale must be finite and > 0");
    JUG_ASSERT(_config.sampleRate > 0.f, "ModelImportConfig::sampleRate must be finite and > 0");
    JUG_ASSERT(!_config.formatHint.HasValue() || IsValid(*_config.formatHint), "Invalid eModelFileFormat hint");

    const Result<eModelFileFormat> format = ResolveFormat_(_mem, _config.formatHint, "<memory>");
    JUG_RETURN_IF_ERROR(format);

    ModelImportConfig config = _config;
    config.flags             = ResolveFlags_(_config.flags);

    Model model = {};
    if (IsGltfFormat_(*format))
    {
        JUG_RETURN_IF_ERROR(model_detail::ImportGLTF(model, _mem, FilePath {}, config));
    }
    else
    {
        JUG_RETURN_IF_ERROR(model_detail::ImportUfbxFromMemory(model, _mem, *format, config));
    }

    model_detail::PostProcess(model, config);
    return std::move(model);
}

Result<Model> ImportModelFromFile(
    const FilePath&          _path,
    const ModelImportConfig& _config)
{
    JUG_ASSERT(_config.unitScale > 0.f, "ModelImportConfig::unitScale must be finite and > 0");
    JUG_ASSERT(_config.sampleRate > 0.f, "ModelImportConfig::sampleRate must be finite and > 0");

    ByteBuffer buf = {};
    JUG_RETURN_IF_ERROR(model_detail::ReadFile(_path, buf));

    const String                   pathString = _path.string();
    const Result<eModelFileFormat> format     = ResolveFormat_(buf, _config.formatHint.HasValue() ? _config.formatHint : FormatFromExtension_(_path), pathString.c_str());
    JUG_RETURN_IF_ERROR(format);

    ModelImportConfig config = _config;
    config.flags             = ResolveFlags_(_config.flags);

    Model model = {};
    if (IsGltfFormat_(*format))
    {
        const FilePath baseDir = _path.has_parent_path() ? _path.parent_path() : FilePath { "." };   // 빈 경로는 '미지정' 이므로 현재 디렉터리로 대체
        JUG_RETURN_IF_ERROR(model_detail::ImportGLTF(model, buf, baseDir, config));
    }
    else
    {
        // ufbx 는 외부 .mtl/텍스처 해석을 위해 경로가 필요하다. 해석된 포맷으로 파싱한다.
        JUG_RETURN_IF_ERROR(model_detail::ImportUfbxFromFile(model, _path, *format, config));
    }

    model_detail::PostProcess(model, config);
    return std::move(model);
}

}   // namespace jug
