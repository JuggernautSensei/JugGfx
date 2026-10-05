#include "pch.h"
#include "ModelDetail.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ufbx/ufbx.h>

#include "Error.h"

namespace jug
{

namespace
{

    // ===========================================
    //  ufbx
    // ===========================================

    struct UfbxSceneDeleter
    {
        void operator()(
            ufbx_scene* _pScene) const
        {
            ufbx_free_scene(_pScene);
        }
    };
    using UfbxScenePtr = std::unique_ptr<ufbx_scene, UfbxSceneDeleter>;

    struct UfbxBakedAnimDeleter
    {
        void operator()(
            ufbx_baked_anim* _pAnim) const
        {
            ufbx_free_baked_anim(_pAnim);
        }
    };
    using UfbxBakedAnimPtr = std::unique_ptr<ufbx_baked_anim, UfbxBakedAnimDeleter>;

    constexpr float kIdentityEpsilon = 1e-6f;

    // 정점 하나의 모든 속성. 중복 제거(ufbx_generate_indices)는 메모리 비교를 하므로 패딩이 없어야 한다.
    struct FlatVertex
    {
        ARRAY<float, 3>                                         position    = {};
        ARRAY<float, 3>                                         normal      = {};
        ARRAY<float, 4>                                         tangent     = {};
        ARRAY<ARRAY<float, 4>, kNumMaxVertexColorAttributes>    colors      = {};
        ARRAY<ARRAY<float, 2>, kNumMaxVertexTexCoordAttributes> texCoords   = {};
        ARRAY<float, kNumMaxBoneInfluences>                     boneWeights = {};
        ARRAY<uint16_t, kNumMaxBoneInfluences>                  boneIndices = {};
    };
    static_assert(sizeof(FlatVertex) == sizeof(float) * (3 + 3 + 4 + 4 * kNumMaxVertexColorAttributes + 2 * kNumMaxVertexTexCoordAttributes + kNumMaxBoneInfluences) + sizeof(uint16_t) * kNumMaxBoneInfluences, "FlatVertex must not have padding.");

    struct MeshSource
    {
        const ufbx_mesh*          pMesh            = nullptr;
        const ufbx_skin_deformer* pSkinOrNull      = nullptr;
        size_t                    numColorSets     = 0;
        size_t                    numUvSets        = 0;
        bool                      bNormal          = false;
        bool                      bTangent         = false;
        Vector<uint16_t>          candidateIndices = {};
        Vector<float>             candidateWeights = {};
    };

    // 텍스처 로드 상태. ufbx_texture 단위로 중복을 제거한다.
    struct TextureSlot
    {
        uint32_t modelIndexOrInvalid = kInvalidIndex;
        bool     bTried           = false;
    };

    struct TextureLoader
    {
        Model*                    pModel         = nullptr;
        FilePath                  baseDirOrEmpty = {};
        Flags<eImageImportOption> imageFlags     = kNoneFlags;
        bool                      bSkipTextures  = false;
        Vector<TextureSlot>       slots          = {};
    };

    // ===========================================
    //  ufbx / Utils
    // ===========================================

    [[nodiscard]] float ToFloat_(
        const ufbx_real _value)
    {
        return static_cast<float>(_value);
    }

    [[nodiscard]] VEC3 ToVec3_(
        const ufbx_vec3& _v)
    {
        return VEC3 { ToFloat_(_v.x), ToFloat_(_v.y), ToFloat_(_v.z) };
    }

    [[nodiscard]] String UfbxToString_(
        const ufbx_string& _str)
    {
        return String { _str.data, _str.length };
    }

    // ufbx_matrix 는 3x4 열 우선. 행 벡터 행렬로 변환한다.
    [[nodiscard]] MAT4 ToMat4_(
        const ufbx_matrix& _m)
    {
        return MAT4 {
            ToFloat_(_m.cols[0].x), ToFloat_(_m.cols[0].y), ToFloat_(_m.cols[0].z), 0.f, ToFloat_(_m.cols[1].x), ToFloat_(_m.cols[1].y), ToFloat_(_m.cols[1].z), 0.f, ToFloat_(_m.cols[2].x), ToFloat_(_m.cols[2].y), ToFloat_(_m.cols[2].z), 0.f, ToFloat_(_m.cols[3].x), ToFloat_(_m.cols[3].y), ToFloat_(_m.cols[3].z), 1.f
        };
    }

    [[nodiscard]] bool IsIdentityTransform_(
        const ufbx_transform& _t)
    {
        return std::abs(_t.translation.x) < kIdentityEpsilon && std::abs(_t.translation.y) < kIdentityEpsilon && std::abs(_t.translation.z) < kIdentityEpsilon
            && std::abs(_t.rotation.x) < kIdentityEpsilon && std::abs(_t.rotation.y) < kIdentityEpsilon && std::abs(_t.rotation.z) < kIdentityEpsilon && std::abs(std::abs(_t.rotation.w) - 1.0) < kIdentityEpsilon
            && std::abs(_t.scale.x - 1.0) < kIdentityEpsilon && std::abs(_t.scale.y - 1.0) < kIdentityEpsilon && std::abs(_t.scale.z - 1.0) < kIdentityEpsilon;
    }

    Error MakeLoadError_(
        const ufbx_error& _error)
    {
        ARRAY<char, 1024> message = {};
        ufbx_format_error(message.data(), message.size(), &_error);
        JUG_CORE_LOG_ERROR("Failed to import model with ufbx: {}", message.data());

        if (_error.type == UFBX_ERROR_FILE_NOT_FOUND || _error.type == UFBX_ERROR_EXTERNAL_FILE_NOT_FOUND)
        {
            return eGraphicsError::ModelMissingResource;
        }
        return eGraphicsError::ModelParseFailed;
    }

    ufbx_load_opts MakeLoadOpts_(
        const eModelFileFormat   _format,
        const ModelImportConfig& _config,
        const bool               _bLoadExternalFiles)
    {
        ufbx_load_opts opts = {};

        opts.file_format = _format == eModelFileFormat::Obj ? UFBX_FILE_FORMAT_OBJ : UFBX_FILE_FORMAT_FBX;

        // 좌표계: 왼손 Y-up, 미터. 우수 -> 좌수 변환은 Z 축 미러로 한다.
        opts.target_axes                = ufbx_axes_left_handed_y_up;
        opts.target_unit_meters         = 1.0;
        opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Z;
        opts.space_conversion           = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;   // 노드 변환 대신 지오메트리에 변환을 구워 노드 로컬을 T/R/S 로 단순하게 유지

        // 원본(FBX/OBJ)의 앞면은 CCW. 좌표를 Z 미러하면 같은 인덱스 순서가 CW 가 되어 엔진의 앞면(CW)이 된다.
        // 따라서 미러 시 ufbx 가 기본으로 뒤집는 winding 을 유지하고(retain), 전체 reverse 는 하지 않는다.
        opts.handedness_conversion_retain_winding = true;
        opts.reverse_winding                      = false;

        // 지오메트리 변환은 메시 정점에 구워 넣는다. (인스턴스 등으로 불가능하면 ufbx 가 헬퍼 노드를 추가)
        opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
        // 비표준 스케일 상속은 보정/헬퍼 노드로 표준 상속으로 바꾼다.
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
        // 회전 피벗으로 이동시켜 피벗 없는 T/R/S 로 만든다. (남는 오프셋은 위 지오메트리 변환이 처리)
        opts.pivot_handling = UFBX_PIVOT_HANDLING_ADJUST_TO_ROTATION_PIVOT;

        // OBJ 는 좌표계/단위 정보가 없으므로 우수 Y-up, 미터로 가정한다.
        opts.obj_axes        = ufbx_axes_right_handed_y_up;
        opts.obj_unit_meters = 1.0;

        opts.normalize_normals        = true;
        opts.normalize_tangents       = true;
        opts.clean_skin_weights       = true;
        opts.use_blender_pbr_material = true;

        opts.ignore_geometry  = _config.flags.Has(eModelImportOption::SkipMeshes);
        opts.ignore_animation = _config.flags.Has(eModelImportOption::SkipAnimations);
        opts.ignore_embedded  = _config.flags.Has(eModelImportOption::SkipTextures);

        // .mtl 등 암묵적 외부 파일. 없어도 실패하지 않는다.
        opts.load_external_files           = _bLoadExternalFiles;
        opts.ignore_missing_external_files = true;
        return opts;
    }

    // ===========================================
    //  ufbx / Texture / Material
    // ===========================================

    // 레이어/셰이더 텍스처는 대표 파일 텍스처로 풀어낸다.
    [[nodiscard]] const ufbx_texture* FindFileTextureOrNull_(
        const ufbx_texture* _pTexture)
    {
        if (_pTexture->type == UFBX_TEXTURE_FILE)
        {
            return _pTexture;
        }
        if (_pTexture->file_textures.count > 0)
        {
            return _pTexture->file_textures.data[0];
        }
        return nullptr;
    }

    // 실패해도 치명적이지 않으므로 호출자가 경고를 남긴다.
    Result<Image> LoadTextureImage_(
        const ufbx_texture&             _texture,
        const FilePath&                 _baseDirOrEmpty,
        const Flags<eImageImportOption> _imageFlags)
    {
        if (_texture.content.size > 0)
        {
            return ImportImage(MemoryView { _texture.content.data, _texture.content.size }, _imageFlags);
        }
        if (_baseDirOrEmpty.empty())
        {
            return eGraphicsError::ModelMissingResource;
        }

        // 1. filename  2. 모델 디렉터리 + relative_filename  3. 모델 디렉터리 + 파일 이름만
        const FilePath filename = FilePath { EncodeUtf16(StringView { _texture.filename.data, _texture.filename.length }) };
        const FilePath relative = FilePath { EncodeUtf16(StringView { _texture.relative_filename.data, _texture.relative_filename.length }) };

        ARRAY<FilePath, 3> candidates    = {};
        size_t             numCandidates = 0;
        if (!filename.empty())
        {
            candidates[numCandidates++] = filename;
        }
        if (!relative.empty())
        {
            candidates[numCandidates++] = _baseDirOrEmpty / relative;
        }
        if (filename.has_filename())
        {
            candidates[numCandidates++] = _baseDirOrEmpty / filename.filename();
        }

        for (size_t i = 0; i < numCandidates; ++i)
        {
            std::error_code error;
            if (fs::is_regular_file(candidates[i], error))
            {
                return ImportImageFromFile(candidates[i], _imageFlags);
            }
        }
        return eGraphicsError::ModelMissingResource;
    }

    [[nodiscard]] uint32_t FindOrLoadTextureOrInvalid_(
        TextureLoader&      _loader,
        const ufbx_texture* _pTexture)
    {
        if (_loader.bSkipTextures)
        {
            return kInvalidIndex;
        }

        const ufbx_texture* pFileOrNull = FindFileTextureOrNull_(_pTexture);
        if (pFileOrNull == nullptr)
        {
            return kInvalidIndex;
        }

        TextureSlot& slot = _loader.slots[pFileOrNull->typed_id];
        if (slot.bTried)
        {
            return slot.modelIndexOrInvalid;
        }
        slot.bTried = true;

        Result<Image> loaded = LoadTextureImage_(*pFileOrNull, _loader.baseDirOrEmpty, _loader.imageFlags);
        if (loaded.HasError())
        {
            JUG_CORE_LOG_WARN("FBX/OBJ texture '{}' could not be loaded: {}", UfbxToString_(pFileOrNull->filename), loaded.GetError().MakeMessage());
            return kInvalidIndex;
        }

        slot.modelIndexOrInvalid = static_cast<uint32_t>(_loader.pModel->textures.size());
        _loader.pModel->textures.push_back(loaded.Take());
        return slot.modelIndexOrInvalid;
    }

    ModelTextureDesc UfbxMakeTextureDesc_(
        TextureLoader&               _loader,
        const ufbx_material_map&     _map,
        const Flags<eTextureChannel> _channelFlags)
    {
        ModelTextureDesc desc = {};
        if (_map.texture == nullptr || !_map.texture_enabled)
        {
            return desc;
        }

        const uint32_t textureIndexOrInvalid = FindOrLoadTextureOrInvalid_(_loader, _map.texture);
        if (!IsValid(textureIndexOrInvalid))
        {
            return desc;
        }

        const ufbx_texture* pFile = FindFileTextureOrNull_(_map.texture);
        desc.textureIndexOrInvalid   = textureIndexOrInvalid;
        desc.channelFlags         = _channelFlags;

        Flags<eSampler> samplerFlags = eSampler::Filter_MinMagMipLinear;
        if (pFile->wrap_u == UFBX_WRAP_CLAMP)
        {
            samplerFlags.Enable(eSampler::U_Clamp);
        }
        if (pFile->wrap_v == UFBX_WRAP_CLAMP)
        {
            samplerFlags.Enable(eSampler::V_Clamp);
        }
        desc.samplerFlags = samplerFlags;
        return desc;
    }

    ModelMaterial UfbxConvertMaterial_(
        TextureLoader&       _loader,
        const ufbx_material& _material)
    {
        const Flags<eTextureChannel> kRgbFlags = { eTextureChannel::R, eTextureChannel::G, eTextureChannel::B };

        const ufbx_material_pbr_maps& pbr = _material.pbr;

        ModelMaterial out = {};
        out.name          = UfbxToString_(_material.name);

        // base color = color * factor
        VEC3 baseColor = One<VEC3>();
        if (pbr.base_color.value_components >= 3)
        {
            baseColor = ToVec3_(pbr.base_color.value_vec3);
        }
        if (pbr.base_factor.has_value)
        {
            baseColor = baseColor * ToFloat_(pbr.base_factor.value_real);
        }
        const float opacity = pbr.opacity.has_value ? ToFloat_(pbr.opacity.value_real) : 1.f;
        out.baseColor       = VEC4 { baseColor, opacity };

        if (pbr.metalness.has_value)
        {
            out.metallic = ToFloat_(pbr.metalness.value_real);
        }
        if (pbr.roughness.has_value)
        {
            out.roughness = ToFloat_(pbr.roughness.value_real);
        }
        if (pbr.ambient_occlusion.has_value)
        {
            out.occlusionStrength = ToFloat_(pbr.ambient_occlusion.value_real);
        }

        // emissive = color * factor
        if (pbr.emission_color.value_components >= 3)
        {
            const float factor = pbr.emission_factor.has_value ? ToFloat_(pbr.emission_factor.value_real) : 1.f;
            out.emissive       = ToVec3_(pbr.emission_color.value_vec3) * factor;
        }

        out.textures[eModelTexture::Albedo]           = UfbxMakeTextureDesc_(_loader, pbr.base_color, eTextureChannel::RGBA);
        out.textures[eModelTexture::Normal]           = UfbxMakeTextureDesc_(_loader, pbr.normal_map, kRgbFlags);
        out.textures[eModelTexture::Metallic]         = UfbxMakeTextureDesc_(_loader, pbr.metalness, eTextureChannel::R);
        out.textures[eModelTexture::Roughness]        = UfbxMakeTextureDesc_(_loader, pbr.roughness, eTextureChannel::R);
        out.textures[eModelTexture::AmbientOcclusion] = UfbxMakeTextureDesc_(_loader, pbr.ambient_occlusion, eTextureChannel::R);
        out.textures[eModelTexture::Emissive]         = UfbxMakeTextureDesc_(_loader, pbr.emission_color, kRgbFlags);
        out.textures[eModelTexture::Opacity]          = UfbxMakeTextureDesc_(_loader, pbr.opacity, eTextureChannel::R);

        const bool bOpacityTexture = IsValid(out.textures[eModelTexture::Opacity].textureIndexOrInvalid);
        out.alphaMode              = (opacity < 1.f || bOpacityTexture) ? eModelAlphaMode::Blend : eModelAlphaMode::Opaque;
        out.bDoubleSided           = _material.features.double_sided.enabled;
        return out;
    }

    // ===========================================
    //  ufbx / Node / Skin
    // ===========================================

    // ufbx typed_id -> Model::nodes 인덱스. 단위 변환인 루트는 제거한다. (자식들이 루트가 됨)
    void UfbxConvertNodes_(
        const ufbx_scene*        _pScene,
        const ModelImportConfig& _config,
        Model&                   _model,
        Vector<uint32_t>&        _outNodeMap)   // 원소는 kInvalidIndex 가능
    {
        const bool bSkipMeshes = _config.flags.Has(eModelImportOption::SkipMeshes);
        const bool bSkipSkins  = _config.flags.Has(eModelImportOption::SkipSkins);

        _outNodeMap.assign(_pScene->nodes.count, kInvalidIndex);
        _model.nodes.reserve(_pScene->nodes.count);

        for (size_t i = 0; i < _pScene->nodes.count; ++i)
        {
            const ufbx_node* pNode = _pScene->nodes.data[i];
            if (pNode->is_root && IsIdentityTransform_(pNode->local_transform))
            {
                continue;
            }

            ModelNode out = {};
            out.name      = UfbxToString_(pNode->name);

            const ufbx_transform& t = pNode->local_transform;
            out.local.translation   = ToVec3_(t.translation);
            out.local.rotation      = QUAT { ToFloat_(t.rotation.x), ToFloat_(t.rotation.y), ToFloat_(t.rotation.z), ToFloat_(t.rotation.w) };
            out.local.scale         = ToVec3_(t.scale);

            if (pNode->mesh != nullptr && !bSkipMeshes)
            {
                out.meshGroupIndexOrInvalid = static_cast<uint32_t>(pNode->mesh->typed_id);
                if (!bSkipSkins && pNode->mesh->skin_deformers.count > 0)
                {
                    out.skinIndexOrInvalid = static_cast<uint32_t>(pNode->mesh->skin_deformers.data[0]->typed_id);
                }
            }

            _outNodeMap[i] = static_cast<uint32_t>(_model.nodes.size());
            _model.nodes.push_back(std::move(out));
        }

        // 부모 연결
        for (size_t i = 0; i < _pScene->nodes.count; ++i)
        {
            const ufbx_node* pNode = _pScene->nodes.data[i];
            if (!IsValid(_outNodeMap[i]) || pNode->parent == nullptr)
            {
                continue;
            }
            _model.nodes[_outNodeMap[i]].parentIndexOrInvalid = _outNodeMap[pNode->parent->typed_id];
        }
    }

    Error UfbxConvertSkins_(
        const ufbx_scene*       _pScene,
        const Vector<uint32_t>& _nodeMap,   // 원소는 kInvalidIndex 가능
        Model&                  _model)
    {
        _model.skins.reserve(_pScene->skin_deformers.count);
        for (size_t s = 0; s < _pScene->skin_deformers.count; ++s)
        {
            const ufbx_skin_deformer* pSkin = _pScene->skin_deformers.data[s];
            if (pSkin->clusters.count > Max<uint16_t>())
            {
                JUG_CORE_LOG_ERROR("Failed to import FBX: skin {} has too many bones ({} > {}).", s, pSkin->clusters.count, Max<uint16_t>());
                return eGraphicsError::ModelLimitExceeded;
            }

            ModelSkin out = {};
            out.name      = UfbxToString_(pSkin->name);
            out.nodeIndices.reserve(pSkin->clusters.count);
            out.invBindMats.reserve(pSkin->clusters.count);
            for (size_t c = 0; c < pSkin->clusters.count; ++c)
            {
                const ufbx_skin_cluster* pCluster = pSkin->clusters.data[c];

                uint32_t nodeIndexOrInvalid = kInvalidIndex;
                if (pCluster->bone_node != nullptr)
                {
                    nodeIndexOrInvalid = _nodeMap[pCluster->bone_node->typed_id];
                }
                if (!IsValid(nodeIndexOrInvalid))
                {
                    JUG_CORE_LOG_ERROR("Failed to import FBX: skin {} cluster {} has no valid bone node.", s, c);
                    return eGraphicsError::ModelInvalidData;
                }
                out.nodeIndices.push_back(nodeIndexOrInvalid);
                out.invBindMats.push_back(ToMat4_(pCluster->geometry_to_bone));
            }
            _model.skins.push_back(std::move(out));
        }
        return kOK;
    }

    // ===========================================
    //  ufbx / Mesh
    // ===========================================

    FlatVertex MakeFlatVertex_(
        MeshSource&    _src,
        const uint32_t _corner)
    {
        const ufbx_mesh& mesh = *_src.pMesh;

        FlatVertex v = {};

        const ufbx_vec3 position = ufbx_get_vertex_vec3(&mesh.vertex_position, _corner);
        v.position               = { ToFloat_(position.x), ToFloat_(position.y), ToFloat_(position.z) };

        VEC3 normal = Zero<VEC3>();
        if (_src.bNormal)
        {
            normal   = ToVec3_(ufbx_get_vertex_vec3(&mesh.vertex_normal, _corner));
            v.normal = { normal.x, normal.y, normal.z };
        }

        if (_src.bTangent)
        {
            const VEC3 tangent = ToVec3_(ufbx_get_vertex_vec3(&mesh.vertex_tangent, _corner));

            // w: 쌍접선 방향의 부호
            float sign = 1.f;
            if (mesh.vertex_bitangent.exists)
            {
                const VEC3 bitangent = ToVec3_(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, _corner));
                sign                 = Dot(Cross(normal, tangent), bitangent) < 0.f ? -1.f : 1.f;
            }
            v.tangent = { tangent.x, tangent.y, tangent.z, sign };
        }

        for (size_t i = 0; i < _src.numColorSets; ++i)
        {
            const ufbx_vec4 color = ufbx_get_vertex_vec4(&mesh.color_sets.data[i].vertex_color, _corner);
            v.colors[i]           = { ToFloat_(color.x), ToFloat_(color.y), ToFloat_(color.z), ToFloat_(color.w) };
        }

        for (size_t i = 0; i < _src.numUvSets; ++i)
        {
            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.uv_sets.data[i].vertex_uv, _corner);
            v.texCoords[i]     = { ToFloat_(uv.x), 1.f - ToFloat_(uv.y) };   // FBX/OBJ 의 UV 원점은 좌하단
        }

        if (_src.pSkinOrNull != nullptr)
        {
            const ufbx_skin_deformer& skin   = *_src.pSkinOrNull;
            const uint32_t            vertex = mesh.vertex_indices.data[_corner];
            if (vertex < skin.vertices.count)
            {
                const ufbx_skin_vertex& skinVertex = skin.vertices.data[vertex];

                _src.candidateIndices.clear();
                _src.candidateWeights.clear();
                for (uint32_t w = 0; w < skinVertex.num_weights; ++w)
                {
                    const ufbx_skin_weight& weight = skin.weights.data[skinVertex.weight_begin + w];
                    _src.candidateIndices.push_back(static_cast<uint16_t>(weight.cluster_index));
                    _src.candidateWeights.push_back(ToFloat_(weight.weight));
                }
                model_detail::SelectBoneInfluences(_src.candidateIndices, _src.candidateWeights, v.boneIndices, v.boneWeights);
            }
        }
        return v;
    }

    // 머티리얼 파트 하나 -> ModelMesh 하나. 삼각형이 없으면 _bOutConverted = false
    Error ConvertMeshPart_(
        MeshSource&           _src,
        const ufbx_mesh_part& _part,
        const size_t          _meshIndex,
        ModelMesh&            _out,
        bool&                 _bOutConverted)
    {
        _bOutConverted = false;

        const ufbx_mesh& mesh = *_src.pMesh;
        if (_part.num_triangles == 0)
        {
            return kOK;
        }
        if (_part.num_triangles * 3 > Max<uint32_t>())
        {
            JUG_CORE_LOG_ERROR("Failed to import FBX: mesh {} '{}' part {} has too many vertices.", _meshIndex, UfbxToString_(mesh.name), _part.index);
            return eGraphicsError::ModelLimitExceeded;
        }

        // 모든 면을 삼각형으로 나누어 코너마다 정점을 만든다.
        Vector<FlatVertex> flat = {};
        flat.reserve(_part.num_triangles * 3);

        Vector<uint32_t> triangleCorners(mesh.max_face_triangles * 3);
        for (size_t f = 0; f < _part.face_indices.count; ++f)
        {
            const ufbx_face face = mesh.faces.data[_part.face_indices.data[f]];
            if (face.num_indices < 3)
            {
                continue;
            }

            const uint32_t numTriangles = ufbx_triangulate_face(triangleCorners.data(), triangleCorners.size(), &mesh, face);
            for (uint32_t i = 0; i < numTriangles * 3; ++i)
            {
                flat.push_back(MakeFlatVertex_(_src, triangleCorners[i]));
            }
        }
        if (flat.empty())
        {
            return kOK;
        }

        // 중복 정점 제거
        Vector<uint32_t>   indices(flat.size());
        ufbx_vertex_stream stream = { flat.data(), flat.size(), sizeof(FlatVertex) };
        ufbx_error         error;
        const size_t       numUnique = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, &error);
        if (error.type != UFBX_ERROR_NONE)
        {
            JUG_CORE_LOG_ERROR("Failed to import FBX: mesh {} '{}' part {} index generation failed ({}).", _meshIndex, UfbxToString_(mesh.name), _part.index, UfbxToString_(error.description));
            return eGraphicsError::ModelParseFailed;
        }

        _out.indices = std::move(indices);
        _out.positions.reserve(numUnique);
        for (size_t i = 0; i < numUnique; ++i)
        {
            const FlatVertex& v = flat[i];
            _out.positions.push_back(VEC3 { v.position[0], v.position[1], v.position[2] });
        }
        if (_src.bNormal)
        {
            _out.normals.reserve(numUnique);
            for (size_t i = 0; i < numUnique; ++i)
            {
                _out.normals.push_back(VEC3 { flat[i].normal[0], flat[i].normal[1], flat[i].normal[2] });
            }
        }
        if (_src.bTangent)
        {
            _out.tangents.reserve(numUnique);
            for (size_t i = 0; i < numUnique; ++i)
            {
                const ARRAY<float, 4>& t = flat[i].tangent;
                _out.tangents.push_back(VEC4 { t[0], t[1], t[2], t[3] });
            }
        }
        for (size_t c = 0; c < _src.numColorSets; ++c)
        {
            _out.colors[c].reserve(numUnique);
            for (size_t i = 0; i < numUnique; ++i)
            {
                const ARRAY<float, 4>& color = flat[i].colors[c];
                _out.colors[c].push_back(VEC4 { color[0], color[1], color[2], color[3] });
            }
        }
        for (size_t c = 0; c < _src.numUvSets; ++c)
        {
            _out.texCoords[c].reserve(numUnique);
            for (size_t i = 0; i < numUnique; ++i)
            {
                const ARRAY<float, 2>& uv = flat[i].texCoords[c];
                _out.texCoords[c].push_back(VEC2 { uv[0], uv[1] });
            }
        }
        if (_src.pSkinOrNull != nullptr)
        {
            _out.boneIndices.reserve(numUnique);
            _out.boneWeights.reserve(numUnique);
            for (size_t i = 0; i < numUnique; ++i)
            {
                _out.boneIndices.push_back(flat[i].boneIndices);
                _out.boneWeights.push_back(flat[i].boneWeights);
            }
        }

        _bOutConverted = true;
        return kOK;
    }

    Error UfbxConvertMeshes_(
        const ufbx_scene*        _pScene,
        const ModelImportConfig& _config,
        Model&                   _model)
    {
        const bool bSkipMaterials = _config.flags.Has(eModelImportOption::SkipMaterials);
        const bool bSkipSkins     = _config.flags.Has(eModelImportOption::SkipSkins);
        const bool bSkipMorphs    = _config.flags.Has(eModelImportOption::SkipMorphTargets);
        bool       bWarnedMorph   = false;

        _model.meshGroups.reserve(_pScene->meshes.count);
        for (size_t m = 0; m < _pScene->meshes.count; ++m)
        {
            const ufbx_mesh* pMesh = _pScene->meshes.data[m];

            ModelMeshGroup group = {};
            group.name           = UfbxToString_(pMesh->name);

            if (!pMesh->vertex_position.exists)
            {
                JUG_CORE_LOG_WARN("FBX/OBJ mesh {} '{}' has no positions; skipped.", m, group.name);
                _model.meshGroups.push_back(std::move(group));
                continue;
            }

            if (!bSkipMorphs && !bWarnedMorph && pMesh->blend_deformers.count > 0)
            {
                JUG_CORE_LOG_WARN("FBX blend shapes (morph targets) are not supported in this version; they are ignored.");
                bWarnedMorph = true;
            }

            MeshSource src   = {};
            src.pMesh        = pMesh;
            src.numColorSets = std::min<size_t>(pMesh->color_sets.count, kNumMaxVertexColorAttributes);
            src.numUvSets    = std::min<size_t>(pMesh->uv_sets.count, kNumMaxVertexTexCoordAttributes);
            src.bNormal      = pMesh->vertex_normal.exists;
            src.bTangent     = pMesh->vertex_tangent.exists;
            if (!bSkipSkins && pMesh->skin_deformers.count > 0)
            {
                src.pSkinOrNull = pMesh->skin_deformers.data[0];
                if (pMesh->skin_deformers.count > 1)
                {
                    JUG_CORE_LOG_WARN("FBX mesh {} '{}' has {} skin deformers; only the first is used.", m, group.name, pMesh->skin_deformers.count);
                }
            }

            group.meshes.reserve(pMesh->material_parts.count);
            for (size_t p = 0; p < pMesh->material_parts.count; ++p)
            {
                const ufbx_mesh_part& part = pMesh->material_parts.data[p];

                ModelMesh converted  = {};
                bool      bConverted = false;
                JUG_RETURN_IF_ERROR(ConvertMeshPart_(src, part, m, converted, bConverted));
                if (!bConverted)
                {
                    continue;
                }

                if (!bSkipMaterials && part.index < pMesh->materials.count)
                {
                    converted.materialIndexOrInvalid = static_cast<uint32_t>(pMesh->materials.data[part.index]->typed_id);
                }
                group.meshes.push_back(std::move(converted));
            }
            _model.meshGroups.push_back(std::move(group));
        }
        return kOK;
    }

    // ===========================================
    //  ufbx / Animation
    // ===========================================

    void UfbxFillVec3Curve_(
        const ufbx_baked_vec3_list& _keys,
        AnimationCurve<VEC3>&       _curve)
    {
        _curve.interpolation = eAnimationInterpolation::Linear;
        _curve.times.reserve(_keys.count);
        _curve.values.reserve(_keys.count);
        for (size_t i = 0; i < _keys.count; ++i)
        {
            _curve.times.push_back(static_cast<float>(_keys.data[i].time));
            _curve.values.push_back(ToVec3_(_keys.data[i].value));
        }
    }

    void UfbxFillQuatCurve_(
        const ufbx_baked_quat_list& _keys,
        AnimationCurve<QUAT>&       _curve)
    {
        _curve.interpolation = eAnimationInterpolation::Linear;
        _curve.times.reserve(_keys.count);
        _curve.values.reserve(_keys.count);
        for (size_t i = 0; i < _keys.count; ++i)
        {
            const ufbx_quat& q = _keys.data[i].value;
            _curve.times.push_back(static_cast<float>(_keys.data[i].time));
            _curve.values.push_back(QUAT { ToFloat_(q.x), ToFloat_(q.y), ToFloat_(q.z), ToFloat_(q.w) });
        }
    }

    Error ConvertAnimations_(
        const ufbx_scene*        _pScene,
        const Vector<uint32_t>&  _nodeMap,   // 원소는 kInvalidIndex 가능
        const ModelImportConfig& _config,
        Model&                   _model)
    {
        _model.animations.reserve(_pScene->anim_stacks.count);
        for (size_t s = 0; s < _pScene->anim_stacks.count; ++s)
        {
            const ufbx_anim_stack* pStack = _pScene->anim_stacks.data[s];

            // 키 시간은 스택 시작 시각 기준 0 부터. 샘플링은 sampleRate 로 resample.
            ufbx_bake_opts opts  = {};
            opts.resample_rate   = _config.sampleRate;
            opts.trim_start_time = true;

            ufbx_error             error;
            const UfbxBakedAnimPtr pBaked(ufbx_bake_anim(_pScene, pStack->anim, &opts, &error));
            if (!pBaked)
            {
                ARRAY<char, 1024> message = {};
                ufbx_format_error(message.data(), message.size(), &error);
                JUG_CORE_LOG_ERROR("Failed to import FBX: baking animation stack '{}' failed: {}", UfbxToString_(pStack->name), message.data());
                return eGraphicsError::ModelParseFailed;
            }

            Animation animation = {};
            animation.name      = UfbxToString_(pStack->name);
            for (size_t n = 0; n < pBaked->nodes.count; ++n)
            {
                const ufbx_baked_node& baked           = pBaked->nodes.data[n];
                const uint32_t         nodeIndexOrInvalid = _nodeMap[baked.typed_id];
                if (!IsValid(nodeIndexOrInvalid))
                {
                    continue;
                }
                if (baked.translation_keys.count == 0 && baked.rotation_keys.count == 0 && baked.scale_keys.count == 0)
                {
                    continue;
                }

                AnimationTrack track  = {};
                track.nodeIndexOrInvalid = nodeIndexOrInvalid;
                UfbxFillVec3Curve_(baked.translation_keys, track.translation);
                UfbxFillQuatCurve_(baked.rotation_keys, track.rotation);
                UfbxFillVec3Curve_(baked.scale_keys, track.scale);
                animation.tracks.push_back(std::move(track));
            }
            _model.animations.push_back(std::move(animation));
        }
        return kOK;
    }

    // ===========================================
    //  ufbx / Scene
    // ===========================================

    Error ImportScene_(
        Model&                   _model,
        const ufbx_scene*        _pScene,
        const FilePath&          _baseDirOrEmpty,
        const ModelImportConfig& _config)
    {
        const bool bSkipMeshes     = _config.flags.Has(eModelImportOption::SkipMeshes);
        const bool bSkipMaterials  = _config.flags.Has(eModelImportOption::SkipMaterials);
        const bool bSkipSkins      = _config.flags.Has(eModelImportOption::SkipSkins);
        const bool bSkipAnimations = _config.flags.Has(eModelImportOption::SkipAnimations);

        Vector<uint32_t> nodeMap = {};   // 원소는 kInvalidIndex 가능
        UfbxConvertNodes_(_pScene, _config, _model, nodeMap);

        if (!bSkipMeshes && !bSkipSkins)
        {
            JUG_RETURN_IF_ERROR(UfbxConvertSkins_(_pScene, nodeMap, _model));
        }
        if (!bSkipMeshes)
        {
            JUG_RETURN_IF_ERROR(UfbxConvertMeshes_(_pScene, _config, _model));
        }

        if (!bSkipMaterials)
        {
            TextureLoader loader  = {};
            loader.pModel         = &_model;
            loader.baseDirOrEmpty = _baseDirOrEmpty;
            loader.imageFlags     = _config.imageFlags;
            loader.bSkipTextures  = _config.flags.Has(eModelImportOption::SkipTextures);
            loader.slots.assign(_pScene->textures.count, TextureSlot {});

            _model.materials.reserve(_pScene->materials.count);
            for (size_t i = 0; i < _pScene->materials.count; ++i)
            {
                _model.materials.push_back(UfbxConvertMaterial_(loader, *_pScene->materials.data[i]));
            }
        }

        if (!bSkipAnimations)
        {
            JUG_RETURN_IF_ERROR(ConvertAnimations_(_pScene, nodeMap, _config, _model));
        }
        return kOK;
    }

}   // namespace

namespace model_detail
{

    Error ImportUfbxFromMemory(
        Model&                   _model,
        const MemoryView         _mem,
        const eModelFileFormat   _format,
        const ModelImportConfig& _config)
    {
        // 외부 파일 참조는 해석하지 않는다.
        const ufbx_load_opts opts = MakeLoadOpts_(_format, _config, false);

        ufbx_error         error;
        const UfbxScenePtr pScene(ufbx_load_memory(_mem.GetPtr(), _mem.GetSize(), &opts, &error));
        if (!pScene)
        {
            return MakeLoadError_(error);
        }
        return ImportScene_(_model, pScene.get(), FilePath {}, _config);
    }

    Error ImportUfbxFromFile(
        Model&                   _model,
        const FilePath&          _path,
        const eModelFileFormat   _format,
        const ModelImportConfig& _config)
    {
        const ufbx_load_opts opts = MakeLoadOpts_(_format, _config, true);

        const String       path = _path.string();
        ufbx_error         error;
        const UfbxScenePtr pScene(ufbx_load_file_len(path.data(), path.size(), &opts, &error));
        if (!pScene)
        {
            return MakeLoadError_(error);
        }

        const FilePath baseDir = _path.has_parent_path() ? _path.parent_path() : FilePath { "." };   // 빈 경로는 '미지정' 이므로 현재 디렉터리로 대체
        return ImportScene_(_model, pScene.get(), baseDir, _config);
    }

}   // namespace model_detail

}   // namespace jug
