#include "pch.h"
#include "ModelImporterDetail.h"

#include <ufbx/ufbx.h>

#include "Error.h"

namespace jug::model_importer_detail
{

namespace
{
    constexpr size_t kNumMaxBones      = std::numeric_limits<uint16_t>::max();
    constexpr size_t kNumMaxCandidates = 64;                   // SelectBoneInfluences 후보 상한. ufbx 가중치는 큰 순서라 앞쪽만 씀
    constexpr size_t kNumMaxElements   = size_t { 1 } << 26;   // 메시 하나의 삼각형 모서리 수 / 샘플 수 상한. 깨진 파일이 메모리를 터뜨리지 않게 함
    constexpr size_t kNumMaxSlots      = 4 + kNumMaxVertexTexCoordAttributes + kNumMaxVertexColorAttributes;

    // 아직 디코드 안 한 texture file (실패는 kInvalidIndex)
    constexpr uint32_t kNotLoaded = std::numeric_limits<uint32_t>::max() - 1;

    constexpr Flags<eTextureChannel> kChannelsRgb = Flags<eTextureChannel> { eTextureChannel::R } | eTextureChannel::G | eTextureChannel::B;

    struct FbxContext
    {
        const ufbx_scene&        scene;
        const ModelImportConfig& config;
        const FilePath&          baseDir;   // 비어 있으면 외부 파일 참조 불가
        Model&                   model;
        Vector<uint32_t>         fileTextures     = {};   // ufbx texture file -> Model::textures. 쓰일 때 한 번만 디코드
        Vector<uint32_t>         skinIndices      = {};   // ufbx skin deformer -> Model::skins (본이 없는 deformer 는 kInvalidIndex)
        bool                     bWarnedMultiSkin = false;
        bool                     bWarnedInBetween = false;
    };

    // 정점 하나를 이루는 속성별 값 인덱스 묶음에서 각 속성의 위치. 없으면 kInvalidIndex. 위치 0 은 항상 position
    struct VertexSlots
    {
        uint32_t                                         count     = 1;
        uint32_t                                         normal    = kInvalidIndex;
        uint32_t                                         tangent   = kInvalidIndex;
        uint32_t                                         bitangent = kInvalidIndex;
        ARRAY<uint32_t, kNumMaxVertexTexCoordAttributes> uvs       = {};
        ARRAY<uint32_t, kNumMaxVertexColorAttributes>    colors    = {};
    };

    // ===========================================
    //  Convert
    //   ufbx 가 로드할 때 이미 엔진 규약(왼손 +Y up, 미터 * unitScale)으로 바꿔 줌
    //   여기서는 V 원점(아래 -> 위)과 행 벡터 행렬만 바꿈
    // ===========================================

    [[nodiscard]] StringView ToView_(
        const ufbx_string _s)
    {
        return StringView { _s.data, _s.length };
    }

    [[nodiscard]] String ToName_(
        const ufbx_string _s)
    {
        return String { _s.data, _s.length };
    }

    [[nodiscard]] VEC3 ToVec3_(
        const ufbx_vec3 _v)
    {
        return VEC3 { static_cast<float>(_v.x), static_cast<float>(_v.y), static_cast<float>(_v.z) };
    }

    // 열 벡터용 4x3 행렬의 열(기저, 이동)이 행 벡터용 행렬의 행
    [[nodiscard]] MAT4 ToMat4_(
        const ufbx_matrix& _m)
    {
        MAT4 mat = Identity<MAT4>();
        for (size_t r = 0; r < 4; ++r)
        {
            mat(r, 0) = static_cast<float>(_m.cols[r].x);
            mat(r, 1) = static_cast<float>(_m.cols[r].y);
            mat(r, 2) = static_cast<float>(_m.cols[r].z);
        }
        return mat;
    }

    [[nodiscard]] Error ToError_(
        const ufbx_error& _error)
    {
        JUG_CORE_LOG_ERROR("ImportModel: ufbx - {} {}", ToView_(_error.description), StringView { _error.info, _error.info_length });
        switch (_error.type)   // NOLINT(clang-diagnostic-switch-enum)
        {
            case UFBX_ERROR_FILE_NOT_FOUND:
            case UFBX_ERROR_EXTERNAL_FILE_NOT_FOUND:
                return eGraphicsError::ModelMissingResource;
            case UFBX_ERROR_OUT_OF_MEMORY:
            case UFBX_ERROR_MEMORY_LIMIT:
            case UFBX_ERROR_ALLOCATION_LIMIT:
            case UFBX_ERROR_NODE_DEPTH_LIMIT:
                return eGraphicsError::ModelLimitExceeded;
            case UFBX_ERROR_BAD_INDEX:
                return eGraphicsError::ModelInvalidData;
            case UFBX_ERROR_UNSUPPORTED_VERSION:
            case UFBX_ERROR_FEATURE_DISABLED:
            case UFBX_ERROR_BAD_NURBS:
                return eGraphicsError::ModelUnsupportedFeature;
            case UFBX_ERROR_UNKNOWN:
            case UFBX_ERROR_EMPTY_FILE:
            case UFBX_ERROR_TRUNCATED_FILE:
            case UFBX_ERROR_IO:
            case UFBX_ERROR_UNRECOGNIZED_FILE_FORMAT:
            case UFBX_ERROR_INVALID_UTF8:
            case UFBX_ERROR_THREADED_ASCII_PARSE:
                return eGraphicsError::ModelParseFailed;
            default:
                JUG_ASSERT(false, "Unexpected ufbx error type");
                return eGraphicsError::ModelParseFailed;
        }
    }

    // 루트 노드는 Model::nodes 에 없으므로 한 칸 당김
    [[nodiscard]] uint32_t ToNodeIndex_(
        const ufbx_node& _node)
    {
        JUG_ASSERT(!_node.is_root && _node.typed_id > 0, "ToNodeIndex_: the root node has no Model node");
        return _node.typed_id - 1;
    }

    // ===========================================
    //  Texture
    // ===========================================

    [[nodiscard]] StringView GetBaseName_(
        const StringView _path)
    {
        const size_t pos = _path.find_last_of("\\/");
        return pos == StringView::npos ? _path : _path.substr(pos + 1);
    }

    // 파일에 적힌 상대 경로 -> 이름만 -> 절대 경로의 이름만 순으로 모델 폴더에서 찾음
    [[nodiscard]] FilePath FindTextureFileOrEmpty_(
        const FilePath&          _baseDir,
        const ufbx_texture_file& _file)
    {
        const StringView           relative = ToView_(_file.relative_filename);
        const ARRAY<StringView, 3> names    = {
            { relative, GetBaseName_(relative), GetBaseName_(ToView_(_file.absolute_filename)) }
        };
        for (const StringView name: names)
        {
            if (name.empty())
            {
                continue;
            }

            String normalized = String { name };
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            const FilePath  path = _baseDir / FilePath { EncodeUtf16(normalized) };
            std::error_code ec;
            if (std::filesystem::is_regular_file(path, ec))
            {
                return path;
            }
        }
        return FilePath {};
    }

    // 이미지를 디코드해 Model::textures 에 추가하고 인덱스를 반환. 실패하면 경고를 남기고 kInvalidIndex
    [[nodiscard]] uint32_t ImportTextureFileOrInvalid_(
        FbxContext&              _ctx,
        const ufbx_texture_file& _file)
    {
        const String name = ToName_(_file.relative_filename.length > 0 ? _file.relative_filename : _file.filename);
        if (_file.content.size > 0)
        {
            return AddTextureOrInvalid(_ctx.model, ImportImage(MemoryView { _file.content.data, _file.content.size }, _ctx.config.imageFlags), name);
        }
        if (_ctx.baseDir.empty())
        {
            JUG_CORE_LOG_WARN("ImportModel: cannot load the external texture '{}' without a file path", name);
            return kInvalidIndex;
        }

        const FilePath path = FindTextureFileOrEmpty_(_ctx.baseDir, _file);
        if (path.empty())
        {
            JUG_CORE_LOG_WARN("ImportModel: cannot find the texture file '{}'", name);
            return kInvalidIndex;
        }
        return AddTextureOrInvalid(_ctx.model, ImportImageFromFile(path, _ctx.config.imageFlags), name);
    }

    // ufbx 의 uv_to_texture(V 원점 아래) 를 F * M * F (F(x, y) = (x, 1 - y)) 로 위쪽 원점으로 옮긴 뒤
    // KHR_texture_transform 의 T * R * S 로 분해 (R = [[cos, -sin], [sin, cos]], 전단 없음 가정)
    void ConvertUvTransform_(
        const ufbx_texture& _texture,
        ModelTextureDesc&   _outDesc)
    {
        if (!_texture.has_uv_transform)
        {
            return;
        }

        const ufbx_matrix& m   = _texture.uv_to_texture;
        const float        a00 = static_cast<float>(m.m00);
        const float        a01 = -static_cast<float>(m.m01);
        const float        a10 = -static_cast<float>(m.m10);
        const float        a11 = static_cast<float>(m.m11);

        const float scaleX = std::sqrt(a00 * a00 + a10 * a10);
        const float cos    = scaleX > 0.f ? a00 / scaleX : 1.f;
        const float sin    = scaleX > 0.f ? a10 / scaleX : 0.f;

        _outDesc.uvOffset   = VEC2 { static_cast<float>(m.m03 + m.m01), static_cast<float>(1.0 - m.m11 - m.m13) };
        _outDesc.uvRotation = std::atan2(sin, cos);
        _outDesc.uvScale    = VEC2 { scaleX, a11 * cos - a01 * sin };
    }

    [[nodiscard]] ModelTextureDesc ConvertMap_(
        FbxContext&                  _ctx,
        const ufbx_material_map&     _map,
        const Flags<eTextureChannel> _channelFlags)
    {
        ModelTextureDesc desc = {};
        if (_map.texture == nullptr || !_map.texture_enabled)
        {
            return desc;
        }

        // 레이어/셰이더 텍스처는 첫 파일 텍스처로 대신함
        const ufbx_texture* const pFile = _map.texture->type == UFBX_TEXTURE_FILE ? _map.texture : (_map.texture->file_textures.count > 0 ? _map.texture->file_textures.data[0] : nullptr);
        if (pFile == nullptr || !pFile->has_file)
        {
            JUG_CORE_LOG_WARN("ImportModel: texture '{}' has no image file - ignored", ToView_(_map.texture->name));
            return desc;
        }

        desc.channelFlags = _channelFlags;
        desc.samplerFlags.Set(eSampler::U_Clamp, pFile->wrap_u == UFBX_WRAP_CLAMP);
        desc.samplerFlags.Set(eSampler::V_Clamp, pFile->wrap_v == UFBX_WRAP_CLAMP);
        ConvertUvTransform_(*pFile, desc);

        if (!_ctx.config.flags.Has(eModelImportOption::SkipTextures))
        {
            uint32_t& texture = _ctx.fileTextures[pFile->file_index];
            if (texture == kNotLoaded)
            {
                texture = ImportTextureFileOrInvalid_(_ctx, _ctx.scene.texture_files.data[pFile->file_index]);
            }
            desc.textureIndexOrInvalid = texture;
        }
        return desc;
    }

    // ===========================================
    //  Material
    // ===========================================

    // 스칼라 값만 읽음. 텍스처 전용 맵(법선 등)은 값이 (0, 0, 0) 같은 의미 없는 벡터로 들어옴
    [[nodiscard]] float ToFloatOrDefault_(
        const ufbx_material_map& _map,
        const float              _default)
    {
        return _map.has_value && _map.value_components == 1 ? static_cast<float>(_map.value_real) : _default;
    }

    void ConvertMaterial_(
        FbxContext&          _ctx,
        const ufbx_material& _src,
        ModelMaterial&       _dst)
    {
        const ufbx_material_pbr_maps& pbr = _src.pbr;

        _dst.name = ToName_(_src.name);

        // 색은 factor 와 곱함. 텍스처가 있으면 색은 텍스처에 곱해지는 tint
        _dst.baseColor = One<VEC4>();
        if (pbr.base_color.has_value)
        {
            const VEC3 color = ToVec3_(pbr.base_color.value_vec3) * ToFloatOrDefault_(pbr.base_factor, 1.f);
            _dst.baseColor   = VEC4 { color.x, color.y, color.z, 1.f };
        }
        _dst.baseColor.w = ToFloatOrDefault_(pbr.opacity, 1.f);
        if (_dst.baseColor.w < 1.f || pbr.opacity.texture != nullptr)
        {
            _dst.alphaMode = eModelAlphaMode::Blend;
        }

        // glossiness 로 들어온 값은 거칠기로 뒤집음 (텍스처는 뒤집지 못함)
        _dst.metallic  = ToFloatOrDefault_(pbr.metalness, 0.f);
        _dst.roughness = ToFloatOrDefault_(pbr.roughness, 1.f);
        if (_src.features.roughness_as_glossiness.enabled && pbr.roughness.has_value)
        {
            _dst.roughness = 1.f - _dst.roughness;
            if (pbr.roughness.texture != nullptr)
            {
                JUG_CORE_LOG_WARN("ImportModel: material '{}' has a glossiness texture - it is used as a roughness texture as is", _dst.name);
            }
        }

        if (pbr.emission_color.has_value)
        {
            _dst.emissive = ToVec3_(pbr.emission_color.value_vec3) * ToFloatOrDefault_(pbr.emission_factor, 1.f);
        }

        // texture 가 없으면 scale / strength 는 의미 없음
        _dst.normalScale       = pbr.normal_map.texture != nullptr ? ToFloatOrDefault_(pbr.normal_map, 1.f) : 1.f;
        _dst.occlusionStrength = pbr.ambient_occlusion.texture != nullptr ? ToFloatOrDefault_(pbr.ambient_occlusion, 1.f) : 1.f;
        _dst.bDoubleSided      = _src.features.double_sided.enabled;

        // 단일 채널 맵은 R 에서 읽음 (회색조 이미지는 모든 채널이 같음)
        _dst.textures[eModelTexture::Albedo]           = ConvertMap_(_ctx, pbr.base_color, eTextureChannel::RGBA);
        _dst.textures[eModelTexture::Normal]           = ConvertMap_(_ctx, pbr.normal_map, kChannelsRgb);
        _dst.textures[eModelTexture::Metallic]         = ConvertMap_(_ctx, pbr.metalness, eTextureChannel::R);
        _dst.textures[eModelTexture::Roughness]        = ConvertMap_(_ctx, pbr.roughness, eTextureChannel::R);
        _dst.textures[eModelTexture::AmbientOcclusion] = ConvertMap_(_ctx, pbr.ambient_occlusion, eTextureChannel::R);
        _dst.textures[eModelTexture::Emissive]         = ConvertMap_(_ctx, pbr.emission_color, kChannelsRgb);
        _dst.textures[eModelTexture::Opacity]          = ConvertMap_(_ctx, pbr.opacity, eTextureChannel::R);
    }

    // ===========================================
    //  Node
    // ===========================================

    // 스킨이 있는 메시면 첫 skin deformer. 여럿이면 첫 것만 쓰고 한 번 경고
    [[nodiscard]] const ufbx_skin_deformer* FindSkinOrNull_(
        FbxContext&      _ctx,
        const ufbx_mesh& _mesh)
    {
        if (_ctx.config.flags.Has(eModelImportOption::SkipSkins) || _mesh.skin_deformers.count == 0 || _mesh.skin_deformers.data[0]->clusters.count == 0)
        {
            return nullptr;
        }
        if (_mesh.skin_deformers.count > 1 && !_ctx.bWarnedMultiSkin)
        {
            _ctx.bWarnedMultiSkin = true;
            JUG_CORE_LOG_WARN("ImportModel: mesh '{}' has {} skin deformers - only the first is used", ToView_(_mesh.name), _mesh.skin_deformers.count);
        }
        return _mesh.skin_deformers.data[0];
    }

    void ConvertNodes_(
        FbxContext& _ctx)
    {
        const ufbx_scene& scene   = _ctx.scene;
        const bool        bMeshes = !_ctx.config.flags.Has(eModelImportOption::SkipMeshes);

        JUG_ASSERT(scene.nodes.count > 0 && scene.nodes.data[0] == scene.root_node, "ConvertNodes_: the root node must be the first node");
        _ctx.model.nodes.resize(scene.nodes.count - 1);
        for (size_t i = 1; i < scene.nodes.count; ++i)
        {
            const ufbx_node& src = *scene.nodes.data[i];
            ModelNode&       dst = _ctx.model.nodes[i - 1];

            dst.name = ToName_(src.name);
            if (src.parent != nullptr && !src.parent->is_root)
            {
                dst.parentIndexOrInvalid = ToNodeIndex_(*src.parent);
            }
            if (bMeshes && src.mesh != nullptr)
            {
                dst.meshGroupIndexOrInvalid = src.mesh->typed_id;
            }

            const ufbx_transform& local = src.local_transform;
            dst.local.translation       = ToVec3_(local.translation);
            dst.local.rotation          = NormalizeRotation(QUAT { static_cast<float>(local.rotation.x), static_cast<float>(local.rotation.y), static_cast<float>(local.rotation.z), static_cast<float>(local.rotation.w) });
            dst.local.scale             = ToVec3_(local.scale);
        }
    }

    // ===========================================
    //  Skin
    // ===========================================

    // 본이 있는 skin deformer 만 ModelSkin 이 되고, 메시를 가진 노드가 그 스킨을 가리킴
    [[nodiscard]] Error ConvertSkins_(
        FbxContext& _ctx)
    {
        _ctx.skinIndices.assign(_ctx.scene.skin_deformers.count, kInvalidIndex);
        for (size_t i = 0; i < _ctx.scene.skin_deformers.count; ++i)
        {
            const ufbx_skin_deformer& src = *_ctx.scene.skin_deformers.data[i];
            if (src.clusters.count == 0)
            {
                continue;
            }
            if (src.clusters.count > kNumMaxBones)
            {
                JUG_CORE_LOG_ERROR("ImportModel: skin {} has {} bones (1 ~ {})", i, src.clusters.count, kNumMaxBones);
                return eGraphicsError::ModelLimitExceeded;
            }

            _ctx.skinIndices[i] = static_cast<uint32_t>(_ctx.model.skins.size());

            ModelSkin& dst = _ctx.model.skins.emplace_back();
            dst.name       = ToName_(src.name);
            dst.nodeIndices.resize(src.clusters.count);
            dst.invBindMats.resize(src.clusters.count);
            for (size_t j = 0; j < src.clusters.count; ++j)
            {
                const ufbx_skin_cluster& cluster = *src.clusters.data[j];
                JUG_ASSERT(cluster.bone_node != nullptr, "ConvertSkins_: broken clusters are not connected");

                dst.nodeIndices[j] = ToNodeIndex_(*cluster.bone_node);
                dst.invBindMats[j] = ToMat4_(cluster.geometry_to_bone);   // 메시 공간 -> 본 공간 = 인버스 바인드
            }
            dst.rootNodeIndexOrInvalid = FindSkinRoot(_ctx.model.nodes, dst.nodeIndices, kInvalidIndex);
        }

        for (size_t i = 1; i < _ctx.scene.nodes.count; ++i)
        {
            const ufbx_mesh* const pMesh = _ctx.scene.nodes.data[i]->mesh;
            if (pMesh == nullptr)
            {
                continue;
            }

            const ufbx_skin_deformer* const pSkin = FindSkinOrNull_(_ctx, *pMesh);
            if (pSkin != nullptr)
            {
                _ctx.model.nodes[i - 1].skinIndexOrInvalid = _ctx.skinIndices[pSkin->typed_id];
            }
        }
        return kOK;
    }

    // ===========================================
    //  Mesh
    // ===========================================

    // position 외에 있는 속성에 순서대로 위치를 줌. 늘 같은 순서라 같은 속성은 같은 위치
    [[nodiscard]] VertexSlots MakeSlots_(
        const ufbx_mesh&                              _mesh,
        ARRAY<const ufbx_uint32_list*, kNumMaxSlots>& _outIndices)
    {
        VertexSlots slots = {};
        slots.uvs.fill(kInvalidIndex);
        slots.colors.fill(kInvalidIndex);

        _outIndices[0] = &_mesh.vertex_position.indices;
        if (_mesh.vertex_normal.exists)
        {
            slots.normal               = slots.count;
            _outIndices[slots.count++] = &_mesh.vertex_normal.indices;
        }
        if (_mesh.uv_sets.count > 0 && _mesh.uv_sets.data[0].vertex_tangent.exists)
        {
            slots.tangent              = slots.count;
            _outIndices[slots.count++] = &_mesh.uv_sets.data[0].vertex_tangent.indices;
            if (_mesh.uv_sets.data[0].vertex_bitangent.exists)
            {
                slots.bitangent            = slots.count;
                _outIndices[slots.count++] = &_mesh.uv_sets.data[0].vertex_bitangent.indices;
            }
        }
        for (size_t s = 0; s < _mesh.uv_sets.count && s < slots.uvs.size(); ++s)
        {
            if (_mesh.uv_sets.data[s].vertex_uv.exists)
            {
                slots.uvs[s]               = slots.count;
                _outIndices[slots.count++] = &_mesh.uv_sets.data[s].vertex_uv.indices;
            }
        }
        for (size_t s = 0; s < _mesh.color_sets.count && s < slots.colors.size(); ++s)
        {
            if (_mesh.color_sets.data[s].vertex_color.exists)
            {
                slots.colors[s]            = slots.count;
                _outIndices[slots.count++] = &_mesh.color_sets.data[s].vertex_color.indices;
            }
        }
        return slots;
    }

    // 면을 삼각형으로 나눈 모서리 인덱스(ufbx_mesh 의 index). 구멍 면과 3 모서리 미만 면은 건너뜀
    void CollectCorners_(
        const ufbx_mesh&      _mesh,
        const ufbx_mesh_part& _part,
        Vector<uint32_t>&     _outCorners)
    {
        Vector<uint32_t> faceCorners(_mesh.max_face_triangles * 3);
        _outCorners.reserve(_part.num_triangles * 3);
        for (size_t i = 0; i < _part.num_faces; ++i)
        {
            const uint32_t  faceIndex = _part.face_indices.data[i];
            const ufbx_face face      = _mesh.faces.data[faceIndex];
            if (face.num_indices < 3 || (_mesh.face_hole.count > 0 && _mesh.face_hole.data[faceIndex]))
            {
                continue;
            }

            const uint32_t numTriangles = ufbx_triangulate_face(faceCorners.data(), faceCorners.size(), &_mesh, face);
            _outCorners.insert(_outCorners.end(), faceCorners.begin(), faceCorners.begin() + numTriangles * 3);
        }
    }

    // 접선의 w: Cross(N, T) 가 bitangent 와 같은 방향이면 +1. FBX 의 bitangent(V 증가 방향)는 glTF 의 bitangent(이미지 위쪽)와 같은 방향이라 V 를 뒤집어도 그대로 비교
    [[nodiscard]] float CalcTangentSign_(
        const VEC3& _normal,
        const VEC3& _tangent,
        const VEC3& _bitangent)
    {
        return Dot(Cross(_normal, _tangent), _bitangent) < 0.f ? -1.f : 1.f;
    }

    // 합친 정점의 속성을 채움. 정점의 속성 인덱스 묶음은 _tuples[v * count ..]
    void GatherVertices_(
        const ufbx_mesh&        _mesh,
        const VertexSlots&      _slots,
        const Vector<uint32_t>& _tuples,
        const size_t            _numVertices,
        ModelMesh&              _outMesh)
    {
        const size_t count = _slots.count;

        _outMesh.positions.resize(_numVertices);
        for (size_t v = 0; v < _numVertices; ++v)
        {
            _outMesh.positions[v] = ToVec3_(_mesh.vertex_position.values.data[_tuples[v * count]]);
        }
        if (_slots.normal != kInvalidIndex)
        {
            _outMesh.normals.resize(_numVertices);
            for (size_t v = 0; v < _numVertices; ++v)
            {
                _outMesh.normals[v] = ToVec3_(_mesh.vertex_normal.values.data[_tuples[v * count + _slots.normal]]);
            }
        }
        if (_slots.tangent != kInvalidIndex)
        {
            const ufbx_uv_set& set = _mesh.uv_sets.data[0];
            _outMesh.tangents.resize(_numVertices);
            for (size_t v = 0; v < _numVertices; ++v)
            {
                const VEC3 tangent = ToVec3_(set.vertex_tangent.values.data[_tuples[v * count + _slots.tangent]]);
                float      sign    = 1.f;
                if (_slots.normal != kInvalidIndex && _slots.bitangent != kInvalidIndex)
                {
                    sign = CalcTangentSign_(_outMesh.normals[v], tangent, ToVec3_(set.vertex_bitangent.values.data[_tuples[v * count + _slots.bitangent]]));
                }
                _outMesh.tangents[v] = VEC4 { tangent.x, tangent.y, tangent.z, sign };
            }
        }
        for (size_t s = 0; s < _slots.uvs.size(); ++s)
        {
            if (_slots.uvs[s] == kInvalidIndex)
            {
                continue;
            }

            const ufbx_vec2_list& values = _mesh.uv_sets.data[s].vertex_uv.values;
            _outMesh.texCoords[s].resize(_numVertices);
            for (size_t v = 0; v < _numVertices; ++v)
            {
                const ufbx_vec2 uv       = values.data[_tuples[v * count + _slots.uvs[s]]];
                _outMesh.texCoords[s][v] = VEC2 { static_cast<float>(uv.x), static_cast<float>(1.0 - uv.y) };   // V 원점: 아래 -> 위
            }
        }
        for (size_t s = 0; s < _slots.colors.size(); ++s)
        {
            if (_slots.colors[s] == kInvalidIndex)
            {
                continue;
            }

            const ufbx_vec4_list& values = _mesh.color_sets.data[s].vertex_color.values;
            _outMesh.colors[s].resize(_numVertices);
            for (size_t v = 0; v < _numVertices; ++v)
            {
                const ufbx_vec4 color = values.data[_tuples[v * count + _slots.colors[s]]];
                _outMesh.colors[s][v] = VEC4 { static_cast<float>(color.x), static_cast<float>(color.y), static_cast<float>(color.z), static_cast<float>(color.w) };
            }
        }
    }

    // 정점의 position 인덱스(= ufbx 의 논리 정점)로 스킨 가중치를 찾아 큰 4 개만 남김
    void GatherBoneInfluences_(
        const ufbx_skin_deformer& _skin,
        const Vector<uint32_t>&   _tuples,
        const size_t              _count,
        const size_t              _numVertices,
        ModelMesh&                _outMesh)
    {
        _outMesh.boneIndices.resize(_numVertices);
        _outMesh.boneWeights.resize(_numVertices);

        ARRAY<uint16_t, kNumMaxCandidates> candidateIndices = {};
        ARRAY<float, kNumMaxCandidates>    candidateWeights = {};

        size_t numTruncated = 0;
        for (size_t v = 0; v < _numVertices; ++v)
        {
            const ufbx_skin_vertex& skinVertex   = _skin.vertices.data[_tuples[v * _count]];
            const size_t            numCandidate = std::min<size_t>(skinVertex.num_weights, kNumMaxCandidates);
            if (numCandidate == 0)
            {
                // 가중치가 없는 정점은 첫 본에 몰아줌
                _outMesh.boneIndices[v]    = {};
                _outMesh.boneWeights[v]    = {};
                _outMesh.boneWeights[v][0] = 1.f;
                continue;
            }

            for (size_t k = 0; k < numCandidate; ++k)
            {
                const ufbx_skin_weight& weight = _skin.weights.data[skinVertex.weight_begin + k];
                candidateIndices[k]            = static_cast<uint16_t>(weight.cluster_index);
                candidateWeights[k]            = static_cast<float>(weight.weight);
            }

            const bool bDropped = SelectBoneInfluences(Span<const uint16_t> { candidateIndices.data(), numCandidate }, Span<const float> { candidateWeights.data(), numCandidate }, _outMesh.boneIndices[v], _outMesh.boneWeights[v]);
            if (bDropped || skinVertex.num_weights > numCandidate)
            {
                ++numTruncated;
            }
        }

        if (numTruncated > 0)
        {
            JUG_CORE_LOG_WARN("ImportModel: {} vertices have more than {} bone influences - kept the largest ones", numTruncated, kNumMaxBoneInfluences);
        }
    }

    // 논리 정점별 모프 오프셋을 펼침 (채널마다 하나, 채널 순서가 morphWeights 순서)
    void ExpandMorphSources_(
        FbxContext&               _ctx,
        const ufbx_mesh&          _mesh,
        Vector<ModelMorphTarget>& _outSources)
    {
        for (size_t d = 0; d < _mesh.blend_deformers.count; ++d)
        {
            const ufbx_blend_deformer& deformer = *_mesh.blend_deformers.data[d];
            for (size_t c = 0; c < deformer.channels.count; ++c)
            {
                const ufbx_blend_channel& channel = *deformer.channels.data[c];
                ModelMorphTarget&         target  = _outSources.emplace_back();
                if (channel.keyframes.count > 1 && !_ctx.bWarnedInBetween)
                {
                    _ctx.bWarnedInBetween = true;
                    JUG_CORE_LOG_WARN("ImportModel: blend channel '{}' has in-between shapes - only the full-weight shape is used", ToView_(channel.name));
                }
                if (channel.target_shape == nullptr)
                {
                    continue;
                }

                const ufbx_blend_shape& shape = *channel.target_shape;
                target.positionDeltas.assign(_mesh.num_vertices, Zero<VEC3>());
                if (shape.normal_offsets.count > 0)
                {
                    target.normalDeltas.assign(_mesh.num_vertices, Zero<VEC3>());
                }
                for (size_t i = 0; i < shape.num_offsets; ++i)
                {
                    const uint32_t vertex = shape.offset_vertices.data[i];
                    if (vertex >= _mesh.num_vertices)
                    {
                        continue;
                    }
                    target.positionDeltas[vertex] = ToVec3_(shape.position_offsets.data[i]);
                    if (!target.normalDeltas.empty())
                    {
                        target.normalDeltas[vertex] = ToVec3_(shape.normal_offsets.data[i]);
                    }
                }
            }
        }
    }

    void GatherMorphTargets_(
        const Vector<ModelMorphTarget>& _sources,
        const Vector<uint32_t>&         _tuples,
        const size_t                    _count,
        const size_t                    _numVertices,
        ModelMesh&                      _outMesh)
    {
        _outMesh.morphTargets.resize(_sources.size());
        for (size_t t = 0; t < _sources.size(); ++t)
        {
            const ModelMorphTarget& src = _sources[t];
            ModelMorphTarget&       dst = _outMesh.morphTargets[t];
            if (!src.positionDeltas.empty())
            {
                dst.positionDeltas.resize(_numVertices);
                for (size_t v = 0; v < _numVertices; ++v)
                {
                    dst.positionDeltas[v] = src.positionDeltas[_tuples[v * _count]];
                }
            }
            if (!src.normalDeltas.empty())
            {
                dst.normalDeltas.resize(_numVertices);
                for (size_t v = 0; v < _numVertices; ++v)
                {
                    dst.normalDeltas[v] = src.normalDeltas[_tuples[v * _count]];
                }
            }
        }
    }

    // 재질 하나가 쓰는 면 -> ModelMesh 하나. 정점은 속성 인덱스 묶음이 같으면 합침 (스킨/모프는 position 에 딸려 있음)
    [[nodiscard]] Error ConvertPart_(
        const FbxContext&               _ctx,
        const ufbx_mesh&                _mesh,
        const ufbx_mesh_part&           _part,
        const ufbx_skin_deformer* const _pSkinOrNull,
        const Vector<ModelMorphTarget>& _morphSources,
        ModelMesh&                      _outMesh)
    {
        if (_part.num_triangles * 3 > kNumMaxElements)
        {
            JUG_CORE_LOG_ERROR("ImportModel: mesh '{}' has {} triangles which exceeds the limit", ToView_(_mesh.name), _part.num_triangles);
            return eGraphicsError::ModelLimitExceeded;
        }

        Vector<uint32_t> corners = {};
        CollectCorners_(_mesh, _part, corners);
        if (corners.empty())
        {
            return kOK;
        }

        ARRAY<const ufbx_uint32_list*, kNumMaxSlots> attributes = {};

        const VertexSlots slots = MakeSlots_(_mesh, attributes);
        const size_t      count = slots.count;

        Vector<uint32_t> tuples(corners.size() * count);
        for (size_t c = 0; c < corners.size(); ++c)
        {
            for (size_t s = 0; s < count; ++s)
            {
                tuples[c * count + s] = attributes[s]->data[corners[c]];
            }
        }

        // tuples 는 제자리에서 합쳐진 정점 목록이 되고 indices 가 삼각형 리스트가 됨
        const ufbx_vertex_stream stream = { tuples.data(), corners.size(), count * sizeof(uint32_t) };
        ufbx_error               error  = {};
        _outMesh.indices.resize(corners.size());
        const size_t numVertices = ufbx_generate_indices(&stream, 1, _outMesh.indices.data(), corners.size(), nullptr, &error);
        if (error.type != UFBX_ERROR_NONE)
        {
            return ToError_(error);
        }

        GatherVertices_(_mesh, slots, tuples, numVertices, _outMesh);
        if (_pSkinOrNull != nullptr)
        {
            GatherBoneInfluences_(*_pSkinOrNull, tuples, count, numVertices, _outMesh);
        }
        if (!_morphSources.empty())
        {
            GatherMorphTargets_(_morphSources, tuples, count, numVertices, _outMesh);
        }

        if (_part.index < _mesh.materials.count && !_ctx.config.flags.Has(eModelImportOption::SkipMaterials))
        {
            _outMesh.materialIndexOrInvalid = _mesh.materials.data[_part.index]->typed_id;
        }
        return kOK;
    }

    [[nodiscard]] Error ConvertMeshGroups_(
        FbxContext& _ctx)
    {
        const bool bMorph = !_ctx.config.flags.Has(eModelImportOption::SkipMorphTargets);

        _ctx.model.meshGroups.resize(_ctx.scene.meshes.count);
        for (size_t i = 0; i < _ctx.scene.meshes.count; ++i)
        {
            const ufbx_mesh& src = *_ctx.scene.meshes.data[i];
            ModelMeshGroup&  dst = _ctx.model.meshGroups[i];

            dst.name = ToName_(src.name);

            Vector<ModelMorphTarget> morphSources = {};
            if (bMorph)
            {
                ExpandMorphSources_(_ctx, src, morphSources);
                for (size_t d = 0; d < src.blend_deformers.count; ++d)
                {
                    for (size_t c = 0; c < src.blend_deformers.data[d]->channels.count; ++c)
                    {
                        dst.morphWeights.push_back(static_cast<float>(src.blend_deformers.data[d]->channels.data[c]->weight));
                    }
                }
            }

            const ufbx_skin_deformer* const pSkin = FindSkinOrNull_(_ctx, src);
            for (size_t p = 0; p < src.material_parts.count; ++p)
            {
                const ufbx_mesh_part& part = src.material_parts.data[p];
                if (part.num_triangles == 0)
                {
                    continue;
                }

                dst.meshes.emplace_back();
                JUG_RETURN_IF_ERROR(ConvertPart_(_ctx, src, part, pSkin, morphSources, dst.meshes.back()));
                if (dst.meshes.back().indices.empty())
                {
                    dst.meshes.pop_back();   // 구멍 면뿐인 파트
                }
            }
        }
        return kOK;
    }

    // ===========================================
    //  Animation
    // ===========================================

    [[nodiscard]] AnimationTrack& GetTrack_(
        Animation&        _animation,
        Vector<uint32_t>& _trackIndicesOrInvalid,
        const uint32_t    _nodeIndex)
    {
        if (_trackIndicesOrInvalid[_nodeIndex] == kInvalidIndex)
        {
            _trackIndicesOrInvalid[_nodeIndex] = static_cast<uint32_t>(_animation.tracks.size());
            _animation.tracks.emplace_back();
            _animation.tracks.back().nodeIndexOrInvalid = _nodeIndex;
        }
        return _animation.tracks[_trackIndicesOrInvalid[_nodeIndex]];
    }

    void ConvertBakedNode_(
        const ufbx_baked_node& _src,
        AnimationTrack&        _outTrack)
    {
        AnimationCurve<VEC3>& translation = _outTrack.translation;
        for (size_t k = 0; k < _src.translation_keys.count; ++k)
        {
            const ufbx_baked_vec3& key = _src.translation_keys.data[k];
            translation.times.push_back(static_cast<float>(key.time));
            translation.values.push_back(ToVec3_(key.value));
        }

        // q 와 -q 는 같은 회전이라 선형 보간이 반대로 돌지 않게 앞 키와 같은 쪽으로 맞춤
        AnimationCurve<QUAT>& rotation = _outTrack.rotation;
        for (size_t k = 0; k < _src.rotation_keys.count; ++k)
        {
            const ufbx_baked_quat& key = _src.rotation_keys.data[k];

            QUAT q = NormalizeRotation(QUAT { static_cast<float>(key.value.x), static_cast<float>(key.value.y), static_cast<float>(key.value.z), static_cast<float>(key.value.w) });
            if (k > 0)
            {
                const QUAT prev = rotation.values.back();
                if (prev.x * q.x + prev.y * q.y + prev.z * q.z + prev.w * q.w < 0.f)
                {
                    q = QUAT { -q.x, -q.y, -q.z, -q.w };
                }
            }
            rotation.times.push_back(static_cast<float>(key.time));
            rotation.values.push_back(q);
        }

        AnimationCurve<VEC3>& scale = _outTrack.scale;
        for (size_t k = 0; k < _src.scale_keys.count; ++k)
        {
            const ufbx_baked_vec3& key = _src.scale_keys.data[k];
            scale.times.push_back(static_cast<float>(key.time));
            scale.values.push_back(ToVec3_(key.value));
        }
    }

    // 움직이는 채널의 DeformPercent 키 시간을 모음
    void AppendWeightTimes_(
        const ufbx_baked_anim&    _baked,
        const ufbx_blend_channel& _channel,
        Vector<double>&           _outTimes)
    {
        for (size_t e = 0; e < _baked.elements.count; ++e)
        {
            const ufbx_baked_element& element = _baked.elements.data[e];
            if (element.element_id != _channel.element_id)
            {
                continue;
            }
            for (size_t p = 0; p < element.props.count; ++p)
            {
                const ufbx_baked_prop& prop = element.props.data[p];
                if (prop.constant_value || ToView_(prop.name) != "DeformPercent")
                {
                    continue;
                }
                for (size_t k = 0; k < prop.keys.count; ++k)
                {
                    _outTimes.push_back(prop.keys.data[k].time);
                }
            }
        }
    }

    // 블렌드 채널은 bake 가 채널마다 키 시간이 달라서, 키 시간을 합쳐서 메시의 모든 채널을 그 시간들에서 평가
    void ConvertMorphAnimation_(
        const ufbx_anim_stack& _stack,
        const ufbx_baked_anim& _baked,
        const ufbx_mesh&       _mesh,
        Animation&             _animation,
        Vector<uint32_t>&      _trackIndicesOrInvalid)
    {
        Vector<const ufbx_blend_channel*> channels = {};
        Vector<double>                    times    = {};
        for (size_t d = 0; d < _mesh.blend_deformers.count; ++d)
        {
            for (size_t c = 0; c < _mesh.blend_deformers.data[d]->channels.count; ++c)
            {
                channels.push_back(_mesh.blend_deformers.data[d]->channels.data[c]);
                AppendWeightTimes_(_baked, *channels.back(), times);
            }
        }
        if (times.empty() || _mesh.instances.count == 0)
        {
            return;
        }
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());

        // bake 의 시간은 애니메이션 시작을 0 으로 옮긴 것
        const double beginTime = _stack.anim->time_begin > 0.0 ? _stack.anim->time_begin : 0.0;

        AnimationCurve<float>& curve = GetTrack_(_animation, _trackIndicesOrInvalid, ToNodeIndex_(*_mesh.instances.data[0])).morphWeights;
        for (const double time: times)
        {
            curve.times.push_back(static_cast<float>(time));
            for (const ufbx_blend_channel* const pChannel: channels)
            {
                curve.values.push_back(static_cast<float>(ufbx_evaluate_blend_weight(_stack.anim, pChannel, beginTime + time)));
            }
        }
    }

    [[nodiscard]] Error ConvertAnimations_(
        FbxContext& _ctx)
    {
        const ufbx_scene& scene  = _ctx.scene;
        const bool        bMorph = !_ctx.config.flags.Has(eModelImportOption::SkipMorphTargets) && !_ctx.config.flags.Has(eModelImportOption::SkipMeshes);

        ufbx_bake_opts bakeOpts  = {};
        bakeOpts.resample_rate   = _ctx.config.sampleRate;
        bakeOpts.trim_start_time = true;

        Vector<uint32_t> trackIndicesOrInvalid(_ctx.model.nodes.size(), kInvalidIndex);   // 노드 -> 현재 애니메이션의 트랙

        _ctx.model.animations.resize(scene.anim_stacks.count);
        for (size_t a = 0; a < scene.anim_stacks.count; ++a)
        {
            const ufbx_anim_stack& stack = *scene.anim_stacks.data[a];
            Animation&             dst   = _ctx.model.animations[a];

            ufbx_error             error  = {};
            ufbx_baked_anim* const pBaked = ufbx_bake_anim(&scene, stack.anim, &bakeOpts, &error);
            if (pBaked == nullptr)
            {
                return ToError_(error);
            }

            dst.name = ToName_(stack.name);
            std::fill(trackIndicesOrInvalid.begin(), trackIndicesOrInvalid.end(), kInvalidIndex);
            for (size_t n = 0; n < pBaked->nodes.count; ++n)
            {
                const ufbx_node& node = *scene.nodes.data[pBaked->nodes.data[n].typed_id];
                if (!node.is_root)
                {
                    ConvertBakedNode_(pBaked->nodes.data[n], GetTrack_(dst, trackIndicesOrInvalid, ToNodeIndex_(node)));
                }
            }
            for (size_t m = 0; m < scene.meshes.count && bMorph; ++m)
            {
                ConvertMorphAnimation_(stack, *pBaked, *scene.meshes.data[m], dst, trackIndicesOrInvalid);
            }
            ufbx_free_baked_anim(pBaked);

            for (const AnimationTrack& track: dst.tracks)
            {
                const ARRAY<const Vector<float>*, 4> allTimes = { &track.translation.times, &track.rotation.times, &track.scale.times, &track.morphWeights.times };
                for (const Vector<float>* pTimes: allTimes)
                {
                    if (!pTimes->empty())
                    {
                        dst.duration = std::max(dst.duration, pTimes->back());
                    }
                }
            }
        }
        return kOK;
    }

    // ===========================================
    //  Import
    // ===========================================

    void WarnUnsupported_(
        const ufbx_scene& _scene)
    {
        const size_t numCurves = _scene.nurbs_curves.count + _scene.nurbs_surfaces.count + _scene.line_curves.count;
        if (_scene.lights.count + _scene.cameras.count + numCurves + _scene.constraints.count > 0)
        {
            JUG_CORE_LOG_WARN("ImportModel: ignored unsupported elements - {} lights, {} cameras, {} curves/surfaces, {} constraints", _scene.lights.count, _scene.cameras.count, numCurves, _scene.constraints.count);
        }
        for (size_t i = 0; i < _scene.metadata.warnings.count; ++i)
        {
            JUG_CORE_LOG_WARN("ImportModel: ufbx - {} ({} times)", ToView_(_scene.metadata.warnings.data[i].description), _scene.metadata.warnings.data[i].count);
        }
    }

    // OBJ 파서는 모르는 줄을 건너뛰므로, 메시가 없는데 모르는 줄이 있으면 OBJ 가 아닌 파일로 봄
    [[nodiscard]] bool IsNotObj_(
        const ufbx_scene& _scene)
    {
        for (size_t i = 0; i < _scene.metadata.warnings.count; ++i)
        {
            if (_scene.metadata.warnings.data[i].type == UFBX_WARNING_UNKNOWN_OBJ_DIRECTIVE && _scene.meshes.count == 0)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] Result<Model> ConvertScene_(
        const ufbx_scene&        _scene,
        const ModelImportConfig& _config,
        const FilePath&          _baseDir,
        const bool               _bObj)
    {
        Model      model = {};
        FbxContext ctx { _scene, _config, _baseDir, model };

        WarnUnsupported_(_scene);
        ConvertNodes_(ctx);
        if (!_config.flags.Has(eModelImportOption::SkipMeshes))
        {
            if (!_config.flags.Has(eModelImportOption::SkipMaterials))
            {
                ctx.fileTextures.assign(_scene.texture_files.count, kNotLoaded);
                model.materials.resize(_scene.materials.count);
                for (size_t i = 0; i < _scene.materials.count; ++i)
                {
                    ConvertMaterial_(ctx, *_scene.materials.data[i], model.materials[i]);
                }
                if (_bObj && _baseDir.empty() && _scene.materials.count > 0)
                {
                    JUG_CORE_LOG_WARN("ImportModel: the .mtl file cannot be loaded without a file path - materials have default values");
                }
            }
            if (!_config.flags.Has(eModelImportOption::SkipSkins))
            {
                JUG_RETURN_IF_ERROR(ConvertSkins_(ctx));
            }
            JUG_RETURN_IF_ERROR(ConvertMeshGroups_(ctx));
        }
        if (!_config.flags.Has(eModelImportOption::SkipAnimations))
        {
            JUG_RETURN_IF_ERROR(ConvertAnimations_(ctx));
        }
        return std::move(model);
    }

    [[nodiscard]] Result<Model> ImportUfbx_(
        const MemoryView         _mem,
        const ModelImportConfig& _config,
        const FilePath&          _baseDirOrEmpty,
        const bool               _bObj)
    {
        JUG_ASSERT(_config.sampleRate > 0.f && std::isfinite(_config.sampleRate), "ImportModel: sampleRate must be positive and finite");

        const Flags<eModelImportOption> flags       = _config.flags;
        const bool                      bSkipMeshes = flags.Has(eModelImportOption::SkipMeshes);

        // 엔진 규약: 왼손 +Y up. 오른손 -> 왼손은 Z 반사로 하고, 반사하면 ufbx 가 감김을 뒤집으므로 그대로 시계 방향 앞면이 됨
        ufbx_load_opts opts              = {};
        opts.file_format                 = _bObj ? UFBX_FILE_FORMAT_OBJ : UFBX_FILE_FORMAT_FBX;
        opts.target_axes                 = ufbx_axes_left_handed_y_up;
        opts.target_unit_meters          = 1.0 / _config.unitScale;
        opts.space_conversion            = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
        opts.handedness_conversion_axis  = UFBX_MIRROR_AXIS_Z;
        opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
        opts.inherit_mode_handling       = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
        opts.pivot_handling              = UFBX_PIVOT_HANDLING_ADJUST_TO_PIVOT;
        opts.clean_skin_weights          = true;
        opts.use_blender_pbr_material    = true;
        opts.ignore_geometry             = bSkipMeshes;
        opts.ignore_embedded             = bSkipMeshes || flags.Has(eModelImportOption::SkipMaterials) || flags.Has(eModelImportOption::SkipTextures);
        opts.ignore_animation            = flags.Has(eModelImportOption::SkipAnimations);
        opts.skip_skin_vertices          = bSkipMeshes || flags.Has(eModelImportOption::SkipSkins);

        // OBJ 는 단위와 축 정보가 없으므로 glTF 와 같은 미터 / 오른손 +Y up 으로 가정
        opts.obj_unit_meters = 1.0;
        opts.obj_axes        = ufbx_axes_right_handed_y_up;

        // .mtl 은 모델 폴더에서 찾음 (ufbx 가 UTF-8 경로를 유니코드로 엶). 없으면 경고만 남음. FBX 의 외부 파일(텍스처, 캐시)은 직접 처리하거나 무시
        String filename = {};
        if (_bObj && !_baseDirOrEmpty.empty())
        {
            filename                           = EncodeUtf8((_baseDirOrEmpty / "model.obj").wstring());
            opts.filename                      = ufbx_string { filename.c_str(), filename.size() };
            opts.load_external_files           = true;
            opts.ignore_missing_external_files = true;
        }

        ufbx_error        error  = {};
        ufbx_scene* const pScene = ufbx_load_memory(_mem.GetPtr(), _mem.GetSize(), &opts, &error);
        if (pScene == nullptr)
        {
            return ToError_(error);
        }

        if (_bObj && IsNotObj_(*pScene))
        {
            ufbx_free_scene(pScene);
            JUG_CORE_LOG_ERROR("ImportModel: the data is not an OBJ file");
            return eGraphicsError::ModelParseFailed;
        }

        Result<Model> result = ConvertScene_(*pScene, _config, _baseDirOrEmpty, _bObj);
        ufbx_free_scene(pScene);
        return result;
    }
}   // namespace

Result<Model> ImportFbx(
    const MemoryView         _mem,
    const ModelImportConfig& _config,
    const FilePath&          _baseDirOrEmpty)
{
    return ImportUfbx_(_mem, _config, _baseDirOrEmpty, false);
}

Result<Model> ImportObj(
    const MemoryView         _mem,
    const ModelImportConfig& _config,
    const FilePath&          _baseDirOrEmpty)
{
    return ImportUfbx_(_mem, _config, _baseDirOrEmpty, true);
}

}   // namespace jug::model_importer_detail
