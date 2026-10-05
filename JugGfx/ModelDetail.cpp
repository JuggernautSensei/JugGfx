#include "pch.h"
#include "ModelDetail.h"

#include <algorithm>

#include "Error.h"
#include "TangentFrame.h"

namespace jug
{

namespace
{

    // ===========================================
    //  Utils
    // ===========================================

    [[nodiscard]] bool IsPicked_(
        const ARRAY<size_t, kNumMaxBoneInfluences>& _picked,
        const size_t                                _numPicked,
        const size_t                                _candidate)
    {
        for (size_t i = 0; i < _numPicked; ++i)
        {
            if (_picked[i] == _candidate)
            {
                return true;
            }
        }
        return false;
    }

    // ===========================================
    //  Post process
    // ===========================================

    void ScaleVec3List_(
        Vector<VEC3>& _list,
        const float   _scale)
    {
        for (VEC3& v: _list)
        {
            v = v * _scale;
        }
    }

    void ScaleCurve_(
        AnimationCurve<VEC3>& _curve,
        const float           _scale)
    {
        ScaleVec3List_(_curve.values, _scale);
        ScaleVec3List_(_curve.inTangents, _scale);
        ScaleVec3List_(_curve.outTangents, _scale);
    }

    void ApplyUnitScale_(
        Model&      _model,
        const float _scale)
    {
        for (ModelNode& node: _model.nodes)
        {
            node.local.translation = node.local.translation * _scale;
        }

        for (ModelMeshGroup& group: _model.meshGroups)
        {
            for (ModelMesh& mesh: group.meshes)
            {
                ScaleVec3List_(mesh.positions, _scale);
                for (ModelMorphTarget& target: mesh.morphTargets)
                {
                    ScaleVec3List_(target.positionDeltas, _scale);
                }
            }
        }

        // 행 벡터 행렬이므로 이동은 3번째 행
        for (ModelSkin& skin: _model.skins)
        {
            for (MAT4& mat: skin.invBindMats)
            {
                mat.m30 *= _scale;
                mat.m31 *= _scale;
                mat.m32 *= _scale;
            }
        }

        for (Animation& animation: _model.animations)
        {
            for (AnimationTrack& track: animation.tracks)
            {
                ScaleCurve_(track.translation, _scale);
            }
        }
    }

    void GenerateNormals_(
        ModelMesh& _mesh)
    {
        if (!_mesh.normals.empty() || _mesh.positions.empty() || _mesh.indices.empty())
        {
            return;
        }

        const Buffer<VEC3> normals = CreateNormals(_mesh.positions, _mesh.indices);
        _mesh.normals.assign(normals.Begin(), normals.End());
    }

    void GenerateTangents_(
        ModelMesh& _mesh)
    {
        if (!_mesh.tangents.empty() || _mesh.positions.empty() || _mesh.indices.empty())
        {
            return;
        }
        if (_mesh.normals.empty() || _mesh.texCoords[0].empty())
        {
            JUG_CORE_LOG_WARN("Skipped tangent generation: the mesh needs normals and texCoord0.");
            return;
        }

        const TangentsBitangents frame = CreateTangentsAndBitangents(_mesh.positions, _mesh.texCoords[0], _mesh.indices);

        _mesh.tangents.reserve(_mesh.positions.size());
        for (size_t i = 0; i < _mesh.positions.size(); ++i)
        {
            const VEC3  tangent = frame.tangents[i];
            const float sign    = Dot(Cross(_mesh.normals[i], tangent), frame.bitangents[i]) < 0.f ? -1.f : 1.f;
            _mesh.tangents.push_back(VEC4 { tangent, sign });
        }
    }

    void FinalizeMesh_(
        ModelMesh& _mesh)
    {
        // bounds
        if (_mesh.positions.empty())
        {
            _mesh.bounds = Zero<AABB>();
        }
        else
        {
            VEC3 min = _mesh.positions[0];
            VEC3 max = _mesh.positions[0];
            for (const VEC3& position: _mesh.positions)
            {
                min = Min(min, position);
                max = Max(max, position);
            }
            _mesh.bounds = AABB::MakeFromMinMax(min, max);
        }

        // attribute flags (Bitangent 는 tangent.w 가 부호를 가지므로 사용하지 않음)
        _mesh.attribFlags = kNoneFlags;
        if (!_mesh.positions.empty())
        {
            _mesh.attribFlags.Enable(eVertexAttribute::Position);
        }
        if (!_mesh.normals.empty())
        {
            _mesh.attribFlags.Enable(eVertexAttribute::Normal);
        }
        if (!_mesh.tangents.empty())
        {
            _mesh.attribFlags.Enable(eVertexAttribute::Tangent);
        }
        for (size_t i = 0; i < kNumMaxVertexColorAttributes; ++i)
        {
            if (!_mesh.colors[i].empty())
            {
                _mesh.attribFlags.Enable(static_cast<eVertexAttribute>(static_cast<size_t>(eVertexAttribute::Color0) + i));
            }
        }
        for (size_t i = 0; i < kNumMaxVertexTexCoordAttributes; ++i)
        {
            if (!_mesh.texCoords[i].empty())
            {
                _mesh.attribFlags.Enable(static_cast<eVertexAttribute>(static_cast<size_t>(eVertexAttribute::TexCoord0) + i));
            }
        }
        if (!_mesh.boneIndices.empty())
        {
            _mesh.attribFlags.Enable(eVertexAttribute::BoneIndex);
        }
        if (!_mesh.boneWeights.empty())
        {
            _mesh.attribFlags.Enable(eVertexAttribute::BlendWeight);
        }
    }

    template<typename T>
    float FindMaxTime_(
        const AnimationCurve<T>& _curve,
        const float              _maxTime)
    {
        float maxTime = _maxTime;
        for (const float time: _curve.times)
        {
            maxTime = Max(maxTime, time);
        }
        return maxTime;
    }

    void FinalizeAnimation_(
        Animation& _animation)
    {
        float duration = 0.f;
        for (const AnimationTrack& track: _animation.tracks)
        {
            duration = FindMaxTime_(track.translation, duration);
            duration = FindMaxTime_(track.rotation, duration);
            duration = FindMaxTime_(track.scale, duration);
            duration = FindMaxTime_(track.morphWeights, duration);
        }
        _animation.duration = duration;
    }

    // 루트에서 _nodeIndex 까지의 경로 (루트부터)
    Vector<uint32_t> BuildRootPath_(
        const Vector<ModelNode>& _nodes,
        const uint32_t           _nodeIndex)
    {
        Vector<uint32_t> path      = {};
        uint32_t         curOrInvalid = _nodeIndex;
        while (IsValid(curOrInvalid) && path.size() <= _nodes.size())   // 순환 방지
        {
            path.push_back(curOrInvalid);
            curOrInvalid = _nodes[curOrInvalid].parentIndexOrInvalid;
        }
        std::ranges::reverse(path);
        return path;
    }

    // 스킨 본 계층의 최상위 본: 부모가 본이 아닌 본. 여러 개면 그 공통 조상 노드.
    void ComputeSkinRoot_(
        const Vector<ModelNode>& _nodes,
        ModelSkin&               _skin)
    {
        Vector<bool> bJoints(_nodes.size(), false);
        for (const uint32_t nodeIndex: _skin.nodeIndices)
        {
            if (nodeIndex < _nodes.size())
            {
                bJoints[nodeIndex] = true;
            }
        }

        Vector<uint32_t> tops = {};
        for (const uint32_t nodeIndex: _skin.nodeIndices)
        {
            if (nodeIndex >= _nodes.size())
            {
                continue;
            }
            const uint32_t parentOrInvalid = _nodes[nodeIndex].parentIndexOrInvalid;
            if (!IsValid(parentOrInvalid) || !bJoints[parentOrInvalid])
            {
                if (std::ranges::find(tops, nodeIndex) == tops.end())
                {
                    tops.push_back(nodeIndex);
                }
            }
        }

        if (tops.empty())
        {
            JUG_CORE_LOG_WARN("Skin '{}' has no root joint.", _skin.name);
            return;
        }
        if (tops.size() == 1)
        {
            _skin.rootNodeIndexOrInvalid = tops[0];
            return;
        }

        // 공통 조상: 루트 경로들의 공통 접두사의 마지막 노드
        Vector<uint32_t> common = BuildRootPath_(_nodes, tops[0]);
        for (size_t i = 1; i < tops.size(); ++i)
        {
            const Vector<uint32_t> path   = BuildRootPath_(_nodes, tops[i]);
            const size_t           maxLen = std::min(common.size(), path.size());
            size_t                 len    = 0;
            while (len < maxLen && common[len] == path[len])
            {
                ++len;
            }
            common.resize(len);
        }

        if (common.empty())
        {
            JUG_CORE_LOG_WARN("Skin '{}' has joints in disconnected hierarchies; root joint is unknown.", _skin.name);
            return;
        }
        _skin.rootNodeIndexOrInvalid = common.back();
    }
}   // namespace

namespace model_detail
{

    // ===========================================
    //  Utils
    // ===========================================

    void SelectBoneInfluences(
        const Span<const uint16_t>              _candidateIndices,
        const Span<const float>                 _candidateWeights,
        ARRAY<uint16_t, kNumMaxBoneInfluences>& _outIndices,
        ARRAY<float, kNumMaxBoneInfluences>&    _outWeights)
    {
        _outIndices = {};
        _outWeights = {};

        const size_t                         numCandidates = std::min(_candidateIndices.size(), _candidateWeights.size());
        ARRAY<size_t, kNumMaxBoneInfluences> picked        = {};
        size_t                               numPicked     = 0;
        float                                sum           = 0.f;

        for (size_t slot = 0; slot < kNumMaxBoneInfluences; ++slot)
        {
            size_t bestIndexOrInvalid = kInvalidIndex;
            float  bestWeight      = 0.f;
            for (size_t i = 0; i < numCandidates; ++i)
            {
                if (_candidateWeights[i] > bestWeight && !IsPicked_(picked, numPicked, i))
                {
                    bestIndexOrInvalid = i;
                    bestWeight      = _candidateWeights[i];
                }
            }
            if (!IsValid(bestIndexOrInvalid))
            {
                break;
            }

            picked[numPicked] = bestIndexOrInvalid;
            _outIndices[slot] = _candidateIndices[bestIndexOrInvalid];
            _outWeights[slot] = bestWeight;
            sum += bestWeight;
            ++numPicked;
        }

        if (sum > 0.f)
        {
            const float invSum = 1.f / sum;
            for (float& weight: _outWeights)
            {
                weight *= invSum;
            }
        }
    }

    [[nodiscard]] Error ReadFile(
        const FilePath& _path,
        ByteBuffer&     _out)
    {
        Result<FileReader> reader = FileReader::Open(_path);
        JUG_RETURN_IF_ERROR(reader);

        const Result<size_t> fileSize = reader->GetSize();
        JUG_RETURN_IF_ERROR(fileSize);

        _out.ResizeUninitialized(*fileSize);
        JUG_RETURN_IF_ERROR(reader->Read(_out));
        return kOK;
    }

    // ===========================================
    //  Post process
    // ===========================================

    void PostProcess(
        Model&                   _model,
        const ModelImportConfig& _config)
    {
        // 1. 단위 배율
        if (_config.unitScale != 1.f)   // NOLINT
        {
            ApplyUnitScale_(_model, _config.unitScale);
        }

        // 2~3. 법선/탄젠트 생성, bounds, attribute flags
        for (ModelMeshGroup& group: _model.meshGroups)
        {
            for (ModelMesh& mesh: group.meshes)
            {
                if (_config.flags.Has(eModelImportOption::GenerateNormals))
                {
                    GenerateNormals_(mesh);
                }
                if (_config.flags.Has(eModelImportOption::GenerateTangents))
                {
                    GenerateTangents_(mesh);
                }
                FinalizeMesh_(mesh);
            }
        }

        for (Animation& animation: _model.animations)
        {
            FinalizeAnimation_(animation);
        }

        for (ModelSkin& skin: _model.skins)
        {
            if (!IsValid(skin.rootNodeIndexOrInvalid))
            {
                ComputeSkinRoot_(_model.nodes, skin);
            }
        }
    }

}   // namespace model_detail

}   // namespace jug
