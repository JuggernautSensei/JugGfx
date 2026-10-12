#include "pch.h"
#include "ModelImporter.h"

#include "Error.h"
#include "ModelImporterDetail.h"

namespace jug
{

namespace model_importer_detail
{

    bool SelectBoneInfluences(
        const Span<const uint16_t>              _indices,
        const Span<const float>                 _weights,
        ARRAY<uint16_t, kNumMaxBoneInfluences>& _outIndices,
        ARRAY<float, kNumMaxBoneInfluences>&    _outWeights)
    {
        constexpr size_t kNumMaxCandidates = 64;   // pickedMask 비트 수
        JUG_ASSERT(!_indices.empty() && _indices.size() == _weights.size(), "SelectBoneInfluences: indices and weights must have the same non-zero size");
        JUG_ASSERT(_indices.size() <= kNumMaxCandidates, "SelectBoneInfluences: too many candidates");

        _outIndices = {};
        _outWeights = {};

        // 0 이하와 NaN 은 건너뛰고 큰 순서대로 고름
        uint64_t pickedMask = 0;
        float    sum        = 0.f;
        for (size_t k = 0; k < kNumMaxBoneInfluences; ++k)
        {
            size_t best       = _indices.size();
            float  bestWeight = 0.f;
            for (size_t i = 0; i < _indices.size(); ++i)
            {
                if ((pickedMask >> i) & 1)
                {
                    continue;
                }
                if (!(_weights[i] > bestWeight))
                {
                    continue;
                }
                best       = i;
                bestWeight = _weights[i];
            }
            if (best == _indices.size())
            {
                break;
            }

            pickedMask |= uint64_t { 1 } << best;
            _outIndices[k] = _indices[best];
            _outWeights[k] = bestWeight;
            sum += bestWeight;
        }

        bool bDropped = false;
        for (size_t i = 0; i < _indices.size(); ++i)
        {
            if (((pickedMask >> i) & 1) == 0 && _weights[i] > 0.f)
            {
                bDropped = true;
            }
        }

        // 전부 0 이거나 inf/NaN 이면 첫 영향 하나에 몰아줌
        if (!(sum > 0.f) || !std::isfinite(sum))
        {
            _outIndices    = {};
            _outWeights    = {};
            _outIndices[0] = _indices[0];
            _outWeights[0] = 1.f;
            return bDropped;
        }

        // 1 / sum 은 sum 이 아주 작으면 inf 가 되므로 나눗셈
        for (float& weight: _outWeights)
        {
            weight /= sum;
        }
        return bDropped;
    }

    QUAT NormalizeRotation(
        const QUAT _q)
    {
        const float length = Length(_q);
        return length > 0.f ? _q / length : QUAT::Identity();
    }

    namespace
    {
        [[nodiscard]] size_t GetDepth_(
            const Span<const ModelNode> _nodes,
            uint32_t                    _index)
        {
            size_t depth = 0;
            while (_nodes[_index].parentIndexOrInvalid != kInvalidIndex)
            {
                _index = _nodes[_index].parentIndexOrInvalid;
                ++depth;
            }
            return depth;
        }
    }   // namespace

    uint32_t FindSkinRoot(
        const Span<const ModelNode> _nodes,
        const Span<const uint32_t>  _jointNodeIndices,
        const uint32_t              _skeletonOrInvalid)
    {
        JUG_ASSERT(!_jointNodeIndices.empty(), "FindSkinRoot: a skin needs at least one joint");
        if (_skeletonOrInvalid != kInvalidIndex)
        {
            return _skeletonOrInvalid;
        }

        uint32_t top = _jointNodeIndices[0];
        for (const uint32_t joint: _jointNodeIndices)
        {
            if (GetDepth_(_nodes, joint) < GetDepth_(_nodes, top))
            {
                top = joint;
            }
        }

        // 가장 얕은 관절이 모든 관절의 조상이 아니면 첫 관절의 최상위 조상
        for (const uint32_t joint: _jointNodeIndices)
        {
            uint32_t node = joint;
            while (node != kInvalidIndex && node != top)
            {
                node = _nodes[node].parentIndexOrInvalid;
            }
            if (node == kInvalidIndex)
            {
                top = _jointNodeIndices[0];
                while (_nodes[top].parentIndexOrInvalid != kInvalidIndex)
                {
                    top = _nodes[top].parentIndexOrInvalid;
                }
                break;
            }
        }
        return top;
    }

    uint32_t AddTextureOrInvalid(
        Model&           _model,
        Result<Image>    _image,
        const StringView _nameForLog)
    {
        if (_image.HasError())
        {
            JUG_CORE_LOG_WARN("ImportModel: failed to import the image '{}' - {}", _nameForLog, _image.GetError().MakeMessage());
            return kInvalidIndex;
        }

        _model.textures.push_back(_image.Take());
        return static_cast<uint32_t>(_model.textures.size() - 1);
    }

    // ===========================================
    //  Validate
    // ===========================================

#if JUG_ENABLE_ASSERT

    namespace
    {
        constexpr float kWeightSumTolerance = 1e-3f;

        void ValidateTextureDesc_(
            const Model&            _model,
            const ModelTextureDesc& _desc)
        {
            JUG_ASSERT(_desc.textureIndexOrInvalid == kInvalidIndex || _desc.textureIndexOrInvalid < _model.textures.size(), "Model: texture index out of range");
            JUG_ASSERT(_desc.texCoordSet < kNumMaxVertexTexCoordAttributes, "Model: texCoord set out of range");
        }

        void ValidateMaterials_(
            const Model& _model)
        {
            for (const ModelMaterial& material: _model.materials)
            {
                for (const ModelTextureDesc& desc: material.textures)
                {
                    ValidateTextureDesc_(_model, desc);
                }
                for (const ModelExtraTexture& extra: material.extraTextures)
                {
                    ValidateTextureDesc_(_model, extra.desc);
                }
            }
        }

        void ValidateMesh_(
            const Model&     _model,
            const ModelMesh& _mesh)
        {
            const size_t numVertices = _mesh.positions.size();

            JUG_ASSERT(_mesh.normals.empty() || _mesh.normals.size() == numVertices, "Model: normals size mismatch");
            JUG_ASSERT(_mesh.tangents.empty() || _mesh.tangents.size() == numVertices, "Model: tangents size mismatch");
            for (const Vector<VEC4>& colors: _mesh.colors)
            {
                JUG_ASSERT(colors.empty() || colors.size() == numVertices, "Model: colors size mismatch");
            }
            for (const Vector<VEC2>& texCoords: _mesh.texCoords)
            {
                JUG_ASSERT(texCoords.empty() || texCoords.size() == numVertices, "Model: texCoords size mismatch");
            }

            JUG_ASSERT(_mesh.boneIndices.size() == _mesh.boneWeights.size(), "Model: boneIndices and boneWeights must exist together");
            JUG_ASSERT(_mesh.boneIndices.empty() || _mesh.boneIndices.size() == numVertices, "Model: bone data size mismatch");
            for (const ARRAY<float, kNumMaxBoneInfluences>& weights: _mesh.boneWeights)
            {
                float sum = 0.f;
                for (const float weight: weights)
                {
                    JUG_ASSERT(weight >= 0.f, "Model: negative bone weight");
                    sum += weight;
                }
                JUG_ASSERT(std::abs(sum - 1.f) < kWeightSumTolerance, "Model: bone weights must sum to 1");
            }

            for (const ModelMorphTarget& target: _mesh.morphTargets)
            {
                JUG_ASSERT(target.positionDeltas.empty() || target.positionDeltas.size() == numVertices, "Model: morph position deltas size mismatch");
                JUG_ASSERT(target.normalDeltas.empty() || target.normalDeltas.size() == numVertices, "Model: morph normal deltas size mismatch");
                JUG_ASSERT(target.tangentDeltas.empty() || target.tangentDeltas.size() == numVertices, "Model: morph tangent deltas size mismatch");
            }

            JUG_ASSERT(_mesh.indices.size() % 3 == 0, "Model: indices must be triangles");
            for (const uint32_t index: _mesh.indices)
            {
                JUG_ASSERT(index < numVertices, "Model: vertex index out of range");
            }

            JUG_ASSERT(_mesh.materialIndexOrInvalid == kInvalidIndex || _mesh.materialIndexOrInvalid < _model.materials.size(), "Model: material index out of range");
        }

        void ValidateNodes_(
            const Model& _model)
        {
            const size_t numNodes = _model.nodes.size();
            for (size_t i = 0; i < numNodes; ++i)
            {
                const ModelNode& node = _model.nodes[i];
                JUG_ASSERT(node.parentIndexOrInvalid == kInvalidIndex || (node.parentIndexOrInvalid < numNodes && node.parentIndexOrInvalid != i), "Model: parent index out of range");
                JUG_ASSERT(node.meshGroupIndexOrInvalid == kInvalidIndex || node.meshGroupIndexOrInvalid < _model.meshGroups.size(), "Model: mesh group index out of range");
                JUG_ASSERT(node.skinIndexOrInvalid == kInvalidIndex || node.skinIndexOrInvalid < _model.skins.size(), "Model: skin index out of range");
            }

            // 부모를 따라 올라갔을 때 노드 수보다 오래 걸리면 순환
            for (size_t i = 0; i < numNodes; ++i)
            {
                size_t   numSteps = 0;
                uint32_t current  = static_cast<uint32_t>(i);
                while (current != kInvalidIndex && numSteps <= numNodes)
                {
                    current = _model.nodes[current].parentIndexOrInvalid;
                    ++numSteps;
                }
                JUG_ASSERT(current == kInvalidIndex, "Model: parent cycle");
            }

            // boneIndices 는 노드의 스킨 관절 수 안이어야 함
            for (const ModelNode& node: _model.nodes)
            {
                if (node.meshGroupIndexOrInvalid == kInvalidIndex || node.skinIndexOrInvalid == kInvalidIndex)
                {
                    continue;
                }

                const size_t numJoints = _model.skins[node.skinIndexOrInvalid].nodeIndices.size();
                for (const ModelMesh& mesh: _model.meshGroups[node.meshGroupIndexOrInvalid].meshes)
                {
                    for (const ARRAY<uint16_t, kNumMaxBoneInfluences>& indices: mesh.boneIndices)
                    {
                        for (const uint16_t index: indices)
                        {
                            JUG_ASSERT(index < numJoints, "Model: bone index out of range");
                        }
                    }
                }
            }
        }

        void ValidateSkins_(
            const Model& _model)
        {
            for (const ModelSkin& skin: _model.skins)
            {
                JUG_ASSERT(skin.invBindMats.size() == skin.nodeIndices.size(), "Model: invBindMats size mismatch");
                JUG_ASSERT(skin.rootNodeIndexOrInvalid == kInvalidIndex || skin.rootNodeIndexOrInvalid < _model.nodes.size(), "Model: skin root out of range");
                for (const uint32_t nodeIndex: skin.nodeIndices)
                {
                    JUG_ASSERT(nodeIndex < _model.nodes.size(), "Model: joint node index out of range");
                }
            }
        }

        template<typename T>
        void ValidateCurve_(
            const AnimationCurve<T>& _curve,
            const float              _duration)
        {
            const size_t numKeys = _curve.times.size();
            if (numKeys == 0)
            {
                JUG_ASSERT(_curve.values.empty() && _curve.inTangents.empty() && _curve.outTangents.empty(), "Model: curve has values without times");
                return;
            }

            JUG_ASSERT(_curve.values.size() % numKeys == 0, "Model: curve values size mismatch");
            if (_curve.interpolation == eAnimationInterpolation::CubicSpline)
            {
                JUG_ASSERT(_curve.inTangents.size() == _curve.values.size() && _curve.outTangents.size() == _curve.values.size(), "Model: curve tangents size mismatch");
            }
            else
            {
                JUG_ASSERT(_curve.inTangents.empty() && _curve.outTangents.empty(), "Model: only cubic curves have tangents");
            }
            for (size_t i = 1; i < numKeys; ++i)
            {
                JUG_ASSERT(_curve.times[i] >= _curve.times[i - 1], "Model: curve times must not decrease");
            }
            JUG_ASSERT(_curve.times.back() <= _duration, "Model: animation duration is shorter than a curve");
        }

        void ValidateAnimations_(
            const Model& _model)
        {
            for (const Animation& animation: _model.animations)
            {
                for (const AnimationTrack& track: animation.tracks)
                {
                    JUG_ASSERT(track.nodeIndexOrInvalid < _model.nodes.size(), "Model: track node index out of range");
                    ValidateCurve_(track.translation, animation.duration);
                    ValidateCurve_(track.rotation, animation.duration);
                    ValidateCurve_(track.scale, animation.duration);
                    ValidateCurve_(track.morphWeights, animation.duration);
                }
            }
        }
    }   // namespace

#endif

    void ValidateModel(
        const Model& _model)
    {
#if JUG_ENABLE_ASSERT
        ValidateNodes_(_model);
        ValidateSkins_(_model);
        ValidateMaterials_(_model);
        ValidateAnimations_(_model);
        for (const ModelMeshGroup& group: _model.meshGroups)
        {
            for (const ModelMesh& mesh: group.meshes)
            {
                ValidateMesh_(_model, mesh);
            }
        }
#else
        (void)_model;
#endif
    }

}   // namespace model_importer_detail

namespace
{
    // 포맷 판별용 magic (OBJ 는 magic 이 없어 키워드 휴리스틱)
    constexpr StringView kMagicGlb      = "glTF";
    constexpr StringView kMagicFbxBin   = StringView { "Kaydara FBX Binary  ", 20 };
    constexpr StringView kMagicFbxAscii = "; FBX";
    constexpr StringView kTagFbxAscii   = "FBXHeaderExtension";
    constexpr StringView kUtf8Bom       = "\xEF\xBB\xBF";

    constexpr size_t kNumFbxAsciiScanBytes = 1024;
    constexpr size_t kNumObjScanBytes      = 64 * 1024;

    constexpr ARRAY<StringView, 13> kObjKeywords = {
        {
         "v",
         "vt",
         "vn",
         "vp",
         "f",
         "l",
         "p",
         "o",
         "g",
         "s",
         "mg",
         "mtllib",
         "usemtl",
         }
    };

    [[nodiscard]] bool IsObjSpace_(
        const char _ch)
    {
        return _ch == ' ' || _ch == '\t' || _ch == '\r' || _ch == '\n';
    }

    // OBJ 는 magic 이 없어서, 앞부분의 모든 줄이 OBJ 키워드(또는 주석)로 시작하고 정점 줄이 있을 때만 OBJ 로 봄
    [[nodiscard]] bool LooksLikeObj_(
        const StringView _text)
    {
        const bool       bTruncated = _text.size() > kNumObjScanBytes;
        const StringView window     = _text.substr(0, kNumObjScanBytes);

        size_t pos        = window.starts_with(kUtf8Bom) ? kUtf8Bom.size() : 0;
        bool   bHasVertex = false;
        while (pos < window.size())
        {
            size_t lineEnd = window.find('\n', pos);
            if (lineEnd == StringView::npos)
            {
                if (bTruncated)
                {
                    break;   // 창 끝에서 잘린 줄은 검사하지 않음
                }
                lineEnd = window.size();
            }

            StringView line = window.substr(pos, lineEnd - pos);
            pos             = lineEnd + 1;
            while (!line.empty() && IsObjSpace_(line.front()))
            {
                line.remove_prefix(1);
            }
            if (line.empty() || line.front() == '#')
            {
                continue;
            }

            size_t tokenEnd = 0;
            while (tokenEnd < line.size() && !IsObjSpace_(line[tokenEnd]))
            {
                ++tokenEnd;
            }
            const StringView token = line.substr(0, tokenEnd);
            if (std::find(kObjKeywords.begin(), kObjKeywords.end(), token) == kObjKeywords.end())
            {
                return false;
            }
            bHasVertex = bHasVertex || token == "v";
        }
        return bHasVertex;
    }

    [[nodiscard]] Optional<eModelFileFormat> DetectFormat_(
        const MemoryView                 _mem,
        const Optional<eModelFileFormat> _hint)
    {
        const StringView text { reinterpret_cast<const char*>(_mem.GetPtr()), _mem.GetSize() };
        if (text.starts_with(kMagicGlb))
        {
            return eModelFileFormat::Glb;
        }
        if (text.starts_with(kMagicFbxBin))
        {
            return eModelFileFormat::Fbx;
        }

        // BOM 과 공백 뒤 첫 글자로 판별
        size_t pos = text.starts_with(kUtf8Bom) ? kUtf8Bom.size() : 0;
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
        {
            ++pos;
        }
        if (pos < text.size() && text[pos] == '{')
        {
            return eModelFileFormat::Gltf;
        }
        if (text.substr(pos).starts_with(kMagicFbxAscii) || text.substr(0, kNumFbxAsciiScanBytes).find(kTagFbxAscii) != StringView::npos)
        {
            return eModelFileFormat::Fbx;
        }
        if (LooksLikeObj_(text))
        {
            return eModelFileFormat::Obj;
        }
        return _hint;
    }

    [[nodiscard]] Optional<eModelFileFormat> ToFormatFromExtension_(
        const FilePath& _path)
    {
        String ext = _path.extension().string();
        for (char& ch: ext)
        {
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }

        if (ext == ".gltf")
        {
            return eModelFileFormat::Gltf;
        }
        if (ext == ".glb")
        {
            return eModelFileFormat::Glb;
        }
        if (ext == ".fbx")
        {
            return eModelFileFormat::Fbx;
        }
        if (ext == ".obj")
        {
            return eModelFileFormat::Obj;
        }
        return kNullopt;
    }

    // 포맷이 늘면 case 와 함수가 하나씩 늘어남
    [[nodiscard]] Result<Model> ImportByFormat_(
        const MemoryView         _mem,
        const ModelImportConfig& _config,
        const FilePath&          _baseDirOrEmpty)
    {
        const Optional<eModelFileFormat> format = DetectFormat_(_mem, _config.formatHint);
        if (!format.HasValue())
        {
            JUG_CORE_LOG_ERROR("ImportModel: unknown model format");
            return eGraphicsError::ModelUnsupportedFormat;
        }

        switch (format.GetValue())
        {
            case eModelFileFormat::Gltf:
            case eModelFileFormat::Glb:
                return model_importer_detail::ImportGltf(_mem, _config, _baseDirOrEmpty);
            case eModelFileFormat::Fbx:
                return model_importer_detail::ImportFbx(_mem, _config, _baseDirOrEmpty);
            case eModelFileFormat::Obj:
                return model_importer_detail::ImportObj(_mem, _config, _baseDirOrEmpty);
        }

        JUG_ASSERT(false, "Unsupported eModelFileFormat");
        return eGraphicsError::ModelUnsupportedFormat;
    }

    [[nodiscard]] Result<Model> ImportModel_(
        const MemoryView         _mem,
        const ModelImportConfig& _config,
        const FilePath&          _baseDirOrEmpty)
    {
        JUG_ASSERT(_config.unitScale > 0.f && std::isfinite(_config.unitScale), "ImportModel: unitScale must be positive and finite");

        Result<Model> result = ImportByFormat_(_mem, _config, _baseDirOrEmpty);
        JUG_RETURN_IF_ERROR(result);
        model_importer_detail::ValidateModel(result.GetValue());
        return result;
    }
}   // namespace

Result<Model> ImportModel(
    const MemoryView         _mem,
    const ModelImportConfig& _config)
{
    return ImportModel_(_mem, _config, FilePath {});
}

Result<Model> ImportModelFromFile(
    const FilePath&          _path,
    const ModelImportConfig& _config)
{
    const Result<ByteBuffer> file = ReadFile(_path);
    JUG_RETURN_IF_ERROR(file);
    if (file->GetSize() == 0)
    {
        JUG_CORE_LOG_ERROR("ImportModelFromFile: file '{}' is empty", _path.string());
        return eGraphicsError::ModelParseFailed;
    }

    // 사용자가 힌트를 주지 않았을 때만 확장자를 힌트로 씀
    ModelImportConfig config = _config;
    if (!config.formatHint.HasValue())
    {
        config.formatHint = ToFormatFromExtension_(_path);
    }

    // 빈 경로는 "외부 참조를 못 푼다"는 뜻이라 현재 폴더는 "." 로 표시
    FilePath baseDir = _path.parent_path();
    if (baseDir.empty())
    {
        baseDir = ".";
    }

    return ImportModel_(MemoryView { file->GetPtr(), file->GetSize() }, config, baseDir);
}

}   // namespace jug
