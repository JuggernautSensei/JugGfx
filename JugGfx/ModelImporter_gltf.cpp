#include "pch.h"
#include "ModelImporterDetail.h"

#include <numeric>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "Error.h"

namespace jug::model_importer_detail
{

namespace
{
    static_assert(sizeof(VEC2) == sizeof(float) * 2 && sizeof(VEC3) == sizeof(float) * 3 && sizeof(VEC4) == sizeof(float) * 4);
    static_assert(sizeof(QUAT) == sizeof(float) * 4 && sizeof(MAT4) == sizeof(float) * 16);

    constexpr size_t kNumMaxJoints          = std::numeric_limits<uint16_t>::max();
    constexpr size_t kNumMaxJointSets       = 2;   // JOINTS_0, JOINTS_1. 후보 영향 최대 8 개
    constexpr size_t kNumMaxJointCandidates = kNumMaxJointSets * 4;
    constexpr size_t kNumMaxElements        = size_t { 1 } << 26;   // 접근자 하나의 원소 수 상한. 깨진 파일이 메모리를 터뜨리지 않게 함

    // 아직 디코드 안 한 image (실패는 kInvalidIndex)
    constexpr uint32_t kNotLoaded = std::numeric_limits<uint32_t>::max() - 1;

    // glTF sampler 상수
    constexpr cgltf_int kFilterNearest              = 9728;
    constexpr cgltf_int kFilterLinear               = 9729;
    constexpr cgltf_int kFilterNearestMipmapNearest = 9984;
    constexpr cgltf_int kFilterLinearMipmapNearest  = 9985;
    constexpr cgltf_int kFilterNearestMipmapLinear  = 9986;
    constexpr cgltf_int kWrapClampToEdge            = 33071;
    constexpr cgltf_int kWrapMirroredRepeat         = 33648;

    // KHR_materials_* 는 전부 무시하고 넘어감
    constexpr StringView kExtensionMaterialsPrefix = "KHR_materials_";

    constexpr ARRAY<StringView, 5> kSupportedExtensions = {
        {
         "KHR_texture_transform",
         "KHR_mesh_quantization",
         "KHR_texture_basisu",
         "KHR_lights_punctual",
         "KHR_xmp_json_ld",
         }
    };

    constexpr Flags<eTextureChannel> kChannelsRgb = Flags<eTextureChannel> { eTextureChannel::R } | eTextureChannel::G | eTextureChannel::B;

    struct GltfContext
    {
        const cgltf_data&        data;
        const ModelImportConfig& config;
        const FilePath&          baseDir;   // 비어 있으면 외부 파일 참조 불가
        Model&                   model;
        Vector<ByteBuffer>       bufferFiles   = {};   // 외부 .bin. cgltf 가 이 메모리를 직접 가리키므로 import 가 끝날 때까지 유지
        Vector<uint32_t>         imageTextures = {};   // glTF image -> Model::textures. 쓰일 때 한 번만 디코드
    };

    // ===========================================
    //  Engine convention
    //   glTF: 오른손, +Y up, 반시계 앞면, 열 벡터, 미터
    //   엔진: 왼손, +Y up, 시계 앞면, 행 벡터
    //   Z 를 뒤집고 삼각형 감김을 반대로 함
    // ===========================================

    // 위치는 _scale = unitScale, 방향은 1
    void FlipZ_(
        Vector<VEC3>& _vectors,
        const float   _scale)
    {
        for (VEC3& v: _vectors)
        {
            v.x *= _scale;
            v.y *= _scale;
            v.z *= -_scale;
        }
    }

    // Z 반사로 Cross 의 방향이 뒤집히므로 bitangent 를 유지하려면 w 도 뒤집어야 함
    void ConvertTangents_(
        Vector<VEC4>& _tangents)
    {
        for (VEC4& t: _tangents)
        {
            t.z = -t.z;
            t.w = -t.w;
        }
    }

    void ConvertRotations_(
        Vector<QUAT>& _rotations)
    {
        for (QUAT& q: _rotations)
        {
            q.x = -q.x;
            q.y = -q.y;
        }
    }

    // 열 우선 배열을 그대로 읽은 행렬(= 행 벡터용 행렬)을 Z 반전 행렬 S 로 S * M * S.
    // 이동은 마지막 행
    [[nodiscard]] MAT4 ConvertMatrix_(
        const MAT4& _mat,
        const float _unitScale)
    {
        MAT4 m = _mat;
        for (size_t r = 0; r < 4; ++r)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                if ((r == 2) != (c == 2))
                {
                    m(r, c) = -m(r, c);
                }
            }
        }
        for (size_t c = 0; c < 3; ++c)
        {
            m(3, c) *= _unitScale;
        }
        return m;
    }

    // ===========================================
    //  Accessor
    // ===========================================

    // 타입과 개수를 검사하고 float 으로 풀어 _out 에 담음. 개수가 상한을 넘으면 할당 전에 거절
    template<typename T>
    [[nodiscard]] Error ReadAccessor_(
        const cgltf_accessor* _pAccessor,
        const cgltf_type      _type,
        const size_t          _count,
        Vector<T>&            _out)
    {
        JUG_ASSERT(sizeof(T) == cgltf_num_components(_type) * sizeof(float), "ReadAccessor_: T does not match the accessor type");
        if (_pAccessor->type != _type || _pAccessor->count != _count)
        {
            JUG_CORE_LOG_ERROR("ImportModel: accessor type or count mismatch");
            return eGraphicsError::ModelInvalidData;
        }
        if (_count > kNumMaxElements)
        {
            JUG_CORE_LOG_ERROR("ImportModel: accessor count {} exceeds the limit {}", _count, kNumMaxElements);
            return eGraphicsError::ModelLimitExceeded;
        }

        _out.resize(_count);
        const size_t numFloats = _count * cgltf_num_components(_type);
        if (cgltf_accessor_unpack_floats(_pAccessor, reinterpret_cast<float*>(_out.data()), numFloats) != numFloats)
        {
            JUG_CORE_LOG_ERROR("ImportModel: failed to read an accessor (missing buffer data or sparse index error)");
            return eGraphicsError::ModelInvalidData;
        }
        return kOK;
    }

    [[nodiscard]] String ToName_(
        const char* const _pNameOrNull)
    {
        return _pNameOrNull ? String { _pNameOrNull } : String {};
    }

    // ===========================================
    //  File / Buffer
    // ===========================================

    [[nodiscard]] bool IsDataUri_(
        const char* const _pUri)
    {
        return std::strncmp(_pUri, "data:", 5) == 0;
    }

    // % 인코딩을 풀고 UTF-8 -> 경로. 폴더는 _baseDir
    [[nodiscard]] FilePath ResolveUri_(
        const FilePath&   _baseDir,
        const char* const _pUri)
    {
        String uri = _pUri;
        uri.resize(cgltf_decode_uri(uri.data()));
        return _baseDir / FilePath { EncodeUtf16(uri) };
    }

    // 외부 .bin 은 읽은 메모리를 그대로 cgltf 에 넘김 (복사 없음, cgltf 가 해제하지 않음).
    // GLB 내장 / data URI 는 cgltf 가 처리. 경로는 유니코드로 직접 처리
    [[nodiscard]] Error LoadBuffers_(
        cgltf_data&         _data,
        const FilePath&     _baseDir,
        Vector<ByteBuffer>& _outFiles)
    {
        for (cgltf_size i = 0; i < _data.buffers_count; ++i)
        {
            cgltf_buffer& buffer = _data.buffers[i];
            if (buffer.data != nullptr || buffer.uri == nullptr || IsDataUri_(buffer.uri))
            {
                continue;
            }
            if (_baseDir.empty())
            {
                JUG_CORE_LOG_ERROR("ImportModel: cannot resolve the external buffer '{}' without a file path", buffer.uri);
                return eGraphicsError::ModelMissingResource;
            }

            const FilePath     path = ResolveUri_(_baseDir, buffer.uri);
            Result<ByteBuffer> file = ReadFile(path);
            if (file.HasError())
            {
                JUG_CORE_LOG_ERROR("ImportModel: failed to read the buffer file '{}' - {}", path.string(), file.GetError().MakeMessage());
                return eGraphicsError::ModelMissingResource;
            }
            if (buffer.size == 0 || file->GetSize() < buffer.size)
            {
                JUG_CORE_LOG_ERROR("ImportModel: buffer file '{}' is empty or smaller ({}) than declared ({})", path.string(), file->GetSize(), buffer.size);
                return eGraphicsError::ModelInvalidData;
            }

            _outFiles.push_back(file.Take());
            buffer.data             = _outFiles.back().GetPtr();
            buffer.data_free_method = cgltf_data_free_method_none;
        }

        const cgltf_options options = {};
        switch (cgltf_load_buffers(&options, &_data, nullptr))
        {
            case cgltf_result_success:
                return kOK;
            case cgltf_result_unknown_format:
                JUG_CORE_LOG_ERROR("ImportModel: unsupported buffer URI");
                return eGraphicsError::ModelUnsupportedFeature;
            case cgltf_result_file_not_found:
            case cgltf_result_io_error:
                JUG_CORE_LOG_ERROR("ImportModel: failed to load a buffer");
                return eGraphicsError::ModelMissingResource;
            default:
                JUG_CORE_LOG_ERROR("ImportModel: invalid buffer data");
                return eGraphicsError::ModelInvalidData;
        }
    }

    [[nodiscard]] Error CheckRequiredExtensions_(
        const cgltf_data& _data)
    {
        for (cgltf_size i = 0; i < _data.extensions_required_count; ++i)
        {
            const StringView name = _data.extensions_required[i];
            if (!name.starts_with(kExtensionMaterialsPrefix) && std::find(kSupportedExtensions.begin(), kSupportedExtensions.end(), name) == kSupportedExtensions.end())
            {
                JUG_CORE_LOG_ERROR("ImportModel: required glTF extension '{}' is not supported", name);
                return eGraphicsError::ModelUnsupportedFeature;
            }
        }
        return kOK;
    }

    // ===========================================
    //  Node
    // ===========================================

    [[nodiscard]] Error ConvertLocal_(
        const cgltf_node& _src,
        const float       _unitScale,
        TRANSFORM&        _outLocal)
    {
        _outLocal = Identity<TRANSFORM>();
        if (_src.has_matrix)
        {
            MAT4 mat;
            std::memcpy(&mat, _src.matrix, sizeof(mat));
            mat = ConvertMatrix_(mat, _unitScale);

            // Decompose 는 0 스케일을 허용하지 않음
            for (size_t r = 0; r < 3; ++r)
            {
                const float length = Length(VEC3 { mat(r, 0), mat(r, 1), mat(r, 2) });
                if (!(length >= kEpsilon) || !std::isfinite(length))
                {
                    JUG_CORE_LOG_ERROR("ImportModel: node '{}' has a matrix with zero or invalid scale", ToName_(_src.name));
                    return eGraphicsError::ModelInvalidData;
                }
            }
            _outLocal = TRANSFORM::MakeFromMatrix(mat);
            return kOK;
        }

        if (_src.has_translation)
        {
            _outLocal.translation = VEC3 { _src.translation[0] * _unitScale, _src.translation[1] * _unitScale, -_src.translation[2] * _unitScale };
        }
        if (_src.has_rotation)
        {
            _outLocal.rotation = NormalizeRotation(QUAT { -_src.rotation[0], -_src.rotation[1], _src.rotation[2], _src.rotation[3] });
        }
        if (_src.has_scale)
        {
            _outLocal.scale = VEC3 { _src.scale[0], _src.scale[1], _src.scale[2] };
        }
        return kOK;
    }

    [[nodiscard]] Error ConvertNodes_(
        GltfContext& _ctx)
    {
        const cgltf_data& data      = _ctx.data;
        const bool        bMeshes   = !_ctx.config.flags.Has(eModelImportOption::SkipMeshes);
        const bool        bSkins    = bMeshes && !_ctx.config.flags.Has(eModelImportOption::SkipSkins);
        const float       unitScale = _ctx.config.unitScale;

        _ctx.model.nodes.resize(data.nodes_count);
        for (cgltf_size i = 0; i < data.nodes_count; ++i)
        {
            const cgltf_node& src = data.nodes[i];
            ModelNode&        dst = _ctx.model.nodes[i];

            dst.name = ToName_(src.name);
            if (src.parent != nullptr)
            {
                dst.parentIndexOrInvalid = static_cast<uint32_t>(cgltf_node_index(&data, src.parent));
            }
            if (bMeshes && src.mesh != nullptr)
            {
                dst.meshGroupIndexOrInvalid = static_cast<uint32_t>(cgltf_mesh_index(&data, src.mesh));
            }
            if (bSkins && src.skin != nullptr)
            {
                dst.skinIndexOrInvalid = static_cast<uint32_t>(cgltf_skin_index(&data, src.skin));
            }
            JUG_RETURN_IF_ERROR(ConvertLocal_(src, unitScale, dst.local));
        }
        return kOK;
    }

    // ===========================================
    //  Material / Texture
    // ===========================================

    [[nodiscard]] Flags<eSampler> ConvertWrap_(
        const cgltf_int _wrap,
        const eSampler  _clamp,
        const eSampler  _mirror)
    {
        if (_wrap == kWrapClampToEdge)
        {
            return _clamp;
        }
        return _wrap == kWrapMirroredRepeat ? _mirror : eSampler::None;
    }

    [[nodiscard]] Flags<eSampler> ConvertSampler_(
        const cgltf_sampler* const _pSamplerOrNull)
    {
        // 지정 안 한 값(0)은 선형 + 반복. sampler 가 없는 것과 같음
        const cgltf_sampler sampler = _pSamplerOrNull != nullptr ? *_pSamplerOrNull : cgltf_sampler {};
        const cgltf_int     min     = sampler.min_filter;

        Flags<eSampler> flags = ConvertWrap_(sampler.wrap_s, eSampler::U_Clamp, eSampler::U_Mirror) | ConvertWrap_(sampler.wrap_t, eSampler::V_Clamp, eSampler::V_Mirror);
        flags.Set(eSampler::Filter_MagLinear, sampler.mag_filter != kFilterNearest);
        flags.Set(eSampler::Filter_MinLinear, min != kFilterNearest && min != kFilterNearestMipmapNearest && min != kFilterNearestMipmapLinear);
        flags.Set(eSampler::Filter_MipLinear, min != kFilterNearest && min != kFilterLinear && min != kFilterNearestMipmapNearest && min != kFilterLinearMipmapNearest);
        return flags;
    }

    // 이미지를 디코드해 Model::textures 에 추가하고 인덱스를 반환. 실패하면 경고를 남기고 kInvalidIndex
    // data URI(base64) 이미지는 풀어서 디코드
    [[nodiscard]] uint32_t LoadImageOrInvalid_(
        GltfContext&       _ctx,
        const cgltf_image& _image,
        const size_t       _imageIndex)
    {
        const String name = std::format("image[{}] {}", _imageIndex, ToName_(_image.name));

        // GLB / 내장 bufferView
        if (_image.buffer_view != nullptr)
        {
            const uint8_t* const pData = cgltf_buffer_view_data(_image.buffer_view);
            if (pData == nullptr)
            {
                JUG_CORE_LOG_WARN("ImportModel: image '{}' has no buffer data", name);
                return kInvalidIndex;
            }
            return AddTextureOrInvalid(_ctx.model, ImportImage(MemoryView { pData, _image.buffer_view->size }, _ctx.config.imageFlags), name);
        }

        if (_image.uri == nullptr)
        {
            JUG_CORE_LOG_WARN("ImportModel: image '{}' has no data", name);
            return kInvalidIndex;
        }

        if (!IsDataUri_(_image.uri))
        {
            if (_ctx.baseDir.empty() || std::strstr(_image.uri, "://") != nullptr)
            {
                JUG_CORE_LOG_WARN("ImportModel: cannot load the external image '{}' (no file path or remote uri)", name);
                return kInvalidIndex;
            }
            return AddTextureOrInvalid(_ctx.model, ImportImageFromFile(ResolveUri_(_ctx.baseDir, _image.uri), _ctx.config.imageFlags), name);
        }

        const char* const pComma = std::strchr(_image.uri, ',');
        if (pComma == nullptr || pComma - _image.uri < 7 || std::strncmp(pComma - 7, ";base64", 7) != 0)
        {
            JUG_CORE_LOG_WARN("ImportModel: image '{}' has an unsupported data URI (base64 only)", name);
            return kInvalidIndex;
        }

        const StringView base64 = pComma + 1;
        size_t           size   = base64.size() * 3 / 4;
        for (size_t i = base64.size(); i > 0 && base64[i - 1] == '='; --i)
        {
            --size;
        }

        const cgltf_options options = {};
        void*               pData   = nullptr;
        if (cgltf_load_buffer_base64(&options, size, base64.data(), &pData) != cgltf_result_success)
        {
            JUG_CORE_LOG_WARN("ImportModel: image '{}' has invalid base64 data", name);
            return kInvalidIndex;
        }

        Result<Image> image = ImportImage(MemoryView { pData, size }, _ctx.config.imageFlags);
        std::free(pData);
        return AddTextureOrInvalid(_ctx.model, std::move(image), name);
    }

    [[nodiscard]] uint32_t ResolveTextureOrInvalid_(
        GltfContext&         _ctx,
        const cgltf_texture& _texture)
    {
        const cgltf_image* const pImage = _texture.image != nullptr ? _texture.image : _texture.basisu_image;
        if (pImage == nullptr)
        {
            JUG_CORE_LOG_WARN("ImportModel: a texture has no image source");
            return kInvalidIndex;
        }

        const size_t imageIndex = cgltf_image_index(&_ctx.data, pImage);
        if (_ctx.imageTextures[imageIndex] == kNotLoaded)
        {
            _ctx.imageTextures[imageIndex] = LoadImageOrInvalid_(_ctx, *pImage, imageIndex);
        }
        return _ctx.imageTextures[imageIndex];
    }

    [[nodiscard]] ModelTextureDesc ConvertTextureView_(
        GltfContext&                 _ctx,
        const cgltf_texture_view&    _view,
        const Flags<eTextureChannel> _channelFlags)
    {
        ModelTextureDesc desc = {};
        if (_view.texture == nullptr)
        {
            return desc;
        }

        desc.channelFlags = _channelFlags;
        desc.samplerFlags = ConvertSampler_(_view.texture->sampler);

        // 엔진 한계(texCoord 세트 수)를 넘으면 0
        cgltf_int texCoord = _view.texcoord;
        if (_view.has_transform)
        {
            desc.uvOffset   = VEC2 { _view.transform.offset[0], _view.transform.offset[1] };
            desc.uvRotation = _view.transform.rotation;
            desc.uvScale    = VEC2 { _view.transform.scale[0], _view.transform.scale[1] };
            if (_view.transform.has_texcoord)
            {
                texCoord = _view.transform.texcoord;
            }
        }
        if (texCoord >= 0 && static_cast<size_t>(texCoord) < kNumMaxVertexTexCoordAttributes)
        {
            desc.texCoordSet = static_cast<uint32_t>(texCoord);
        }

        if (!_ctx.config.flags.Has(eModelImportOption::SkipTextures))
        {
            desc.textureIndexOrInvalid = ResolveTextureOrInvalid_(_ctx, *_view.texture);
        }
        return desc;
    }

    void ConvertMaterial_(
        GltfContext&          _ctx,
        const cgltf_material& _src,
        ModelMaterial&        _dst)
    {
        _dst.name = ToName_(_src.name);

        if (_src.has_pbr_specular_glossiness)
        {
            JUG_CORE_LOG_WARN("ImportModel: material '{}' uses the deprecated KHR_materials_pbrSpecularGlossiness - falling back to the base PBR values", _dst.name);
        }

        // pbrMetallicRoughness 가 없으면 glTF 기본값
        _dst.baseColor = One<VEC4>();
        _dst.metallic  = 1.f;
        _dst.roughness = 1.f;
        if (_src.has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness& pbr = _src.pbr_metallic_roughness;
            _dst.baseColor                          = VEC4 { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
            _dst.metallic                           = pbr.metallic_factor;
            _dst.roughness                          = pbr.roughness_factor;

            _dst.textures[eModelTexture::Albedo]   = ConvertTextureView_(_ctx, pbr.base_color_texture, eTextureChannel::RGBA);
            _dst.textures[eModelTexture::Metallic] = ConvertTextureView_(_ctx, pbr.metallic_roughness_texture, eTextureChannel::B);

            // 같은 텍스처에서 채널만 다르게 읽음
            _dst.textures[eModelTexture::Roughness]              = _dst.textures[eModelTexture::Metallic];
            _dst.textures[eModelTexture::Roughness].channelFlags = eTextureChannel::G;
        }

        _dst.textures[eModelTexture::Normal]           = ConvertTextureView_(_ctx, _src.normal_texture, kChannelsRgb);
        _dst.textures[eModelTexture::AmbientOcclusion] = ConvertTextureView_(_ctx, _src.occlusion_texture, eTextureChannel::R);
        _dst.textures[eModelTexture::Emissive]         = ConvertTextureView_(_ctx, _src.emissive_texture, kChannelsRgb);

        // texture 가 없으면 scale / strength 는 정의되지 않은 값
        _dst.normalScale       = _src.normal_texture.texture != nullptr ? _src.normal_texture.scale : 1.f;
        _dst.occlusionStrength = _src.occlusion_texture.texture != nullptr ? _src.occlusion_texture.scale : 1.f;
        _dst.emissive          = VEC3 { _src.emissive_factor[0], _src.emissive_factor[1], _src.emissive_factor[2] };

        if (_src.alpha_mode == cgltf_alpha_mode_mask)
        {
            _dst.alphaMode            = eModelAlphaMode::AlphaCutOff;
            _dst.alphaCutOffThreshold = _src.alpha_cutoff;
        }
        else if (_src.alpha_mode == cgltf_alpha_mode_blend)
        {
            _dst.alphaMode = eModelAlphaMode::Blend;
        }
        _dst.bDoubleSided = _src.double_sided != 0;
    }

    // ===========================================
    //  Mesh
    // ===========================================

    // 인덱스를 엔진 규약의 삼각형 리스트로 만듦
    [[nodiscard]] Error ConvertIndices_(
        const cgltf_primitive& _src,
        const size_t           _numVertices,
        Vector<uint32_t>&      _outIndices)
    {
        if (_src.indices != nullptr)
        {
            const size_t count = _src.indices->count;
            if (count > kNumMaxElements)
            {
                JUG_CORE_LOG_ERROR("ImportModel: index count {} exceeds the limit {}", count, kNumMaxElements);
                return eGraphicsError::ModelLimitExceeded;
            }

            _outIndices.resize(count);
            if (cgltf_accessor_unpack_indices(_src.indices, _outIndices.data(), sizeof(uint32_t), count) != count)
            {
                JUG_CORE_LOG_ERROR("ImportModel: failed to read the index accessor");
                return eGraphicsError::ModelInvalidData;
            }
        }
        else
        {
            _outIndices.resize(_numVertices);
            std::iota(_outIndices.begin(), _outIndices.end(), uint32_t { 0 });
        }

        if (_outIndices.size() % 3 != 0)
        {
            JUG_CORE_LOG_ERROR("ImportModel: triangle index count {} is not a multiple of 3", _outIndices.size());
            return eGraphicsError::ModelInvalidData;
        }

        for (size_t i = 0; i < _outIndices.size(); i += 3)
        {
            if (_outIndices[i] >= _numVertices || _outIndices[i + 1] >= _numVertices || _outIndices[i + 2] >= _numVertices)
            {
                JUG_CORE_LOG_ERROR("ImportModel: vertex index out of range");
                return eGraphicsError::ModelInvalidData;
            }
            std::swap(_outIndices[i + 1], _outIndices[i + 2]);
        }
        return kOK;
    }

    // JOINTS_0/1 + WEIGHTS_0/1 의 후보(최대 8 개)에서 큰 4 개만 남김
    [[nodiscard]] Error ConvertBoneInfluences_(
        const ARRAY<const cgltf_accessor*, kNumMaxJointSets>& _joints,
        const ARRAY<const cgltf_accessor*, kNumMaxJointSets>& _weights,
        const size_t                                          _numVertices,
        ModelMesh&                                            _outMesh)
    {
        bool bAny = false;
        for (size_t s = 0; s < kNumMaxJointSets; ++s)
        {
            if (_joints[s] == nullptr && _weights[s] == nullptr)
            {
                continue;
            }

            const bool bOk = _joints[s] != nullptr && _weights[s] != nullptr && _joints[s]->type == cgltf_type_vec4 && _weights[s]->type == cgltf_type_vec4
                          && (_joints[s]->component_type == cgltf_component_type_r_8u || _joints[s]->component_type == cgltf_component_type_r_16u);
            if (!bOk)
            {
                JUG_CORE_LOG_ERROR("ImportModel: invalid JOINTS_{} / WEIGHTS_{} accessor", s, s);
                return eGraphicsError::ModelInvalidData;
            }
            bAny = true;
        }
        if (!bAny)
        {
            return kOK;
        }

        _outMesh.boneIndices.resize(_numVertices);
        _outMesh.boneWeights.resize(_numVertices);

        ARRAY<uint16_t, kNumMaxJointCandidates> candidateIndices = {};
        ARRAY<float, kNumMaxJointCandidates>    candidateWeights = {};
        ARRAY<cgltf_uint, 4>                    joints           = {};
        ARRAY<float, 4>                         weights          = {};

        size_t numTruncated = 0;
        for (size_t v = 0; v < _numVertices; ++v)
        {
            candidateIndices = {};
            candidateWeights = {};
            for (size_t s = 0; s < kNumMaxJointSets; ++s)
            {
                if (_joints[s] == nullptr)
                {
                    continue;
                }
                if (!cgltf_accessor_read_uint(_joints[s], v, joints.data(), 4) || !cgltf_accessor_read_float(_weights[s], v, weights.data(), 4))
                {
                    JUG_CORE_LOG_ERROR("ImportModel: failed to read the joints / weights accessor");
                    return eGraphicsError::ModelInvalidData;
                }
                for (size_t k = 0; k < 4; ++k)
                {
                    candidateIndices[s * 4 + k] = static_cast<uint16_t>(joints[k]);
                    candidateWeights[s * 4 + k] = weights[k];
                }
            }

            if (SelectBoneInfluences(candidateIndices, candidateWeights, _outMesh.boneIndices[v], _outMesh.boneWeights[v]))
            {
                ++numTruncated;
            }
        }

        if (numTruncated > 0)
        {
            JUG_CORE_LOG_WARN("ImportModel: {} vertices have more than {} bone influences - kept the largest ones", numTruncated, kNumMaxBoneInfluences);
        }
        return kOK;
    }

    [[nodiscard]] Error ConvertMorphTargets_(
        const cgltf_primitive& _src,
        const size_t           _numVertices,
        const float            _unitScale,
        ModelMesh&             _outMesh)
    {
        _outMesh.morphTargets.resize(_src.targets_count);
        for (cgltf_size t = 0; t < _src.targets_count; ++t)
        {
            const cgltf_morph_target& target = _src.targets[t];
            ModelMorphTarget&         dst    = _outMesh.morphTargets[t];
            for (cgltf_size a = 0; a < target.attributes_count; ++a)
            {
                const cgltf_attribute& attribute = target.attributes[a];

                Vector<VEC3>* pOut  = nullptr;
                float         scale = 1.f;
                if (attribute.type == cgltf_attribute_type_position)
                {
                    pOut  = &dst.positionDeltas;
                    scale = _unitScale;
                }
                else if (attribute.type == cgltf_attribute_type_normal)
                {
                    pOut = &dst.normalDeltas;
                }
                else if (attribute.type == cgltf_attribute_type_tangent)
                {
                    pOut = &dst.tangentDeltas;
                }
                else
                {
                    continue;
                }

                JUG_RETURN_IF_ERROR(ReadAccessor_(attribute.data, cgltf_type_vec3, _numVertices, *pOut));
                FlipZ_(*pOut, scale);
            }
        }
        return kOK;
    }

    [[nodiscard]] Error ConvertColors_(
        const cgltf_accessor* const _pAccessor,
        const size_t                _numVertices,
        Vector<VEC4>&               _outColors)
    {
        if (_pAccessor->type == cgltf_type_vec4)
        {
            return ReadAccessor_(_pAccessor, cgltf_type_vec4, _numVertices, _outColors);
        }

        // VEC3 는 알파 1 로 넓힘
        Vector<VEC3> rgb = {};
        JUG_RETURN_IF_ERROR(ReadAccessor_(_pAccessor, cgltf_type_vec3, _numVertices, rgb));
        _outColors.resize(_numVertices);
        for (size_t v = 0; v < _numVertices; ++v)
        {
            _outColors[v] = VEC4 { rgb[v].x, rgb[v].y, rgb[v].z, 1.f };
        }
        return kOK;
    }

    [[nodiscard]] Error ConvertPrimitive_(
        const GltfContext&     _ctx,
        const cgltf_primitive& _src,
        ModelMesh&             _outMesh)
    {
        // cgltf_validate: 접근자가 하나 이상이고 개수가 전부 같음
        const size_t numVertices = _src.attributes[0].data->count;
        const float  unitScale   = _ctx.config.unitScale;
        const bool   bSkins      = !_ctx.config.flags.Has(eModelImportOption::SkipSkins);

        ARRAY<const cgltf_accessor*, kNumMaxJointSets> joints            = {};
        ARRAY<const cgltf_accessor*, kNumMaxJointSets> weights           = {};
        bool                                           bHasPosition      = false;
        bool                                           bIgnoredJointSets = false;

        for (cgltf_size i = 0; i < _src.attributes_count; ++i)
        {
            const cgltf_attribute& attribute = _src.attributes[i];
            const size_t           index     = static_cast<size_t>(attribute.index);   // 음수면 아주 큰 값이라 아래 범위 검사에서 걸러짐
            if (attribute.type == cgltf_attribute_type_position)
            {
                if (_src.has_draco_mesh_compression && attribute.data->buffer_view == nullptr)
                {
                    JUG_CORE_LOG_ERROR("ImportModel: KHR_draco_mesh_compression is not supported");
                    return eGraphicsError::ModelUnsupportedFeature;
                }
                bHasPosition = true;
                JUG_RETURN_IF_ERROR(ReadAccessor_(attribute.data, cgltf_type_vec3, numVertices, _outMesh.positions));
                FlipZ_(_outMesh.positions, unitScale);
            }
            else if (attribute.type == cgltf_attribute_type_normal)
            {
                JUG_RETURN_IF_ERROR(ReadAccessor_(attribute.data, cgltf_type_vec3, numVertices, _outMesh.normals));
                FlipZ_(_outMesh.normals, 1.f);
            }
            else if (attribute.type == cgltf_attribute_type_tangent)
            {
                JUG_RETURN_IF_ERROR(ReadAccessor_(attribute.data, cgltf_type_vec4, numVertices, _outMesh.tangents));
                ConvertTangents_(_outMesh.tangents);
            }
            else if (attribute.type == cgltf_attribute_type_color && index < _outMesh.colors.size())
            {
                JUG_RETURN_IF_ERROR(ConvertColors_(attribute.data, numVertices, _outMesh.colors[index]));
            }
            else if (attribute.type == cgltf_attribute_type_texcoord && index < _outMesh.texCoords.size())
            {
                JUG_RETURN_IF_ERROR(ReadAccessor_(attribute.data, cgltf_type_vec2, numVertices, _outMesh.texCoords[index]));
            }
            else if (attribute.type == cgltf_attribute_type_joints || attribute.type == cgltf_attribute_type_weights)
            {
                if (index >= kNumMaxJointSets)
                {
                    bIgnoredJointSets = true;
                }
                else if (attribute.type == cgltf_attribute_type_joints)
                {
                    joints[index] = attribute.data;
                }
                else
                {
                    weights[index] = attribute.data;
                }
            }
        }

        if (!bHasPosition)
        {
            JUG_CORE_LOG_ERROR("ImportModel: a primitive has no POSITION");
            return eGraphicsError::ModelInvalidData;
        }

        if (bSkins)
        {
            if (bIgnoredJointSets)
            {
                JUG_CORE_LOG_WARN("ImportModel: JOINTS_2 and above are not supported - ignored");
            }
            JUG_RETURN_IF_ERROR(ConvertBoneInfluences_(joints, weights, numVertices, _outMesh));
        }
        if (!_ctx.config.flags.Has(eModelImportOption::SkipMorphTargets))
        {
            JUG_RETURN_IF_ERROR(ConvertMorphTargets_(_src, numVertices, unitScale, _outMesh));
        }

        if (_src.material != nullptr && !_ctx.model.materials.empty())
        {
            _outMesh.materialIndexOrInvalid = static_cast<uint32_t>(cgltf_material_index(&_ctx.data, _src.material));
        }
        return ConvertIndices_(_src, numVertices, _outMesh.indices);
    }

    [[nodiscard]] Error ConvertMeshGroups_(
        GltfContext& _ctx)
    {
        _ctx.model.meshGroups.resize(_ctx.data.meshes_count);
        for (cgltf_size i = 0; i < _ctx.data.meshes_count; ++i)
        {
            const cgltf_mesh& src = _ctx.data.meshes[i];
            ModelMeshGroup&   dst = _ctx.model.meshGroups[i];

            dst.name = ToName_(src.name);
            if (!_ctx.config.flags.Has(eModelImportOption::SkipMorphTargets))
            {
                dst.morphWeights.assign(src.weights, src.weights + src.weights_count);
            }

            for (cgltf_size p = 0; p < src.primitives_count; ++p)
            {
                // 삼각형 리스트만 지원 (점, 선, strip, fan 은 건너뜀)
                if (src.primitives[p].type != cgltf_primitive_type_triangles)
                {
                    JUG_CORE_LOG_WARN("ImportModel: mesh '{}' primitive {} is not a triangle list - skipped", dst.name, p);
                    continue;
                }

                dst.meshes.emplace_back();
                JUG_RETURN_IF_ERROR(ConvertPrimitive_(_ctx, src.primitives[p], dst.meshes.back()));
            }
        }
        return kOK;
    }

    // ===========================================
    //  Skin
    // ===========================================

    [[nodiscard]] Error ConvertSkins_(
        GltfContext& _ctx)
    {
        _ctx.model.skins.resize(_ctx.data.skins_count);
        for (cgltf_size i = 0; i < _ctx.data.skins_count; ++i)
        {
            const cgltf_skin& src = _ctx.data.skins[i];
            ModelSkin&        dst = _ctx.model.skins[i];

            if (src.joints_count == 0 || src.joints_count > kNumMaxJoints)
            {
                JUG_CORE_LOG_ERROR("ImportModel: skin {} has {} joints (1 ~ {})", i, src.joints_count, kNumMaxJoints);
                return src.joints_count == 0 ? eGraphicsError::ModelInvalidData : eGraphicsError::ModelLimitExceeded;
            }

            dst.name = ToName_(src.name);
            dst.nodeIndices.resize(src.joints_count);
            for (cgltf_size j = 0; j < src.joints_count; ++j)
            {
                dst.nodeIndices[j] = static_cast<uint32_t>(cgltf_node_index(&_ctx.data, src.joints[j]));
            }

            const uint32_t skeletonOrInvalid = src.skeleton != nullptr ? static_cast<uint32_t>(cgltf_node_index(&_ctx.data, src.skeleton)) : kInvalidIndex;
            dst.rootNodeIndexOrInvalid       = FindSkinRoot(_ctx.model.nodes, dst.nodeIndices, skeletonOrInvalid);

            if (src.inverse_bind_matrices == nullptr)
            {
                dst.invBindMats.assign(src.joints_count, Identity<MAT4>());
                continue;
            }

            // 열 우선 배열을 그대로 읽으면 행 벡터용 행렬
            JUG_RETURN_IF_ERROR(ReadAccessor_(src.inverse_bind_matrices, cgltf_type_mat4, src.joints_count, dst.invBindMats));
            for (MAT4& mat: dst.invBindMats)
            {
                mat = ConvertMatrix_(mat, _ctx.config.unitScale);
            }
        }
        return kOK;
    }

    // 스킨 없이는 boneIndices 가 어느 관절 수에 맞아야 하는지 알 수 없어서, 스킨이 정해진 뒤에 한 번 검사
    [[nodiscard]] Error CheckBoneIndices_(
        const Model& _model)
    {
        // 메시 그룹이 요구하는 최소 관절 수 (최대 인덱스 + 1)
        Vector<size_t> numRequiredJoints(_model.meshGroups.size(), 0);
        for (size_t g = 0; g < _model.meshGroups.size(); ++g)
        {
            for (const ModelMesh& mesh: _model.meshGroups[g].meshes)
            {
                for (const ARRAY<uint16_t, kNumMaxBoneInfluences>& indices: mesh.boneIndices)
                {
                    for (const uint16_t index: indices)
                    {
                        numRequiredJoints[g] = std::max(numRequiredJoints[g], size_t { index } + 1);
                    }
                }
            }
        }

        for (const ModelNode& node: _model.nodes)
        {
            if (node.meshGroupIndexOrInvalid == kInvalidIndex || node.skinIndexOrInvalid == kInvalidIndex)
            {
                continue;
            }
            if (numRequiredJoints[node.meshGroupIndexOrInvalid] > _model.skins[node.skinIndexOrInvalid].nodeIndices.size())
            {
                JUG_CORE_LOG_ERROR("ImportModel: node '{}' mesh uses joint indices beyond its skin's joint count", node.name);
                return eGraphicsError::ModelInvalidData;
            }
        }
        return kOK;
    }

    // ===========================================
    //  Animation
    // ===========================================

    // 키 시간과 값을 읽음. _numPerKey: 키 하나가 가진 값 개수 (모프 타깃 수)
    template<typename T>
    [[nodiscard]] Error ReadCurve_(
        const cgltf_animation_sampler& _sampler,
        const cgltf_type               _type,
        const size_t                   _numPerKey,
        AnimationCurve<T>&             _curve)
    {
        _curve.interpolation = eAnimationInterpolation::Linear;
        if (_sampler.interpolation == cgltf_interpolation_type_step)
        {
            _curve.interpolation = eAnimationInterpolation::Step;
        }
        else if (_sampler.interpolation == cgltf_interpolation_type_cubic_spline)
        {
            _curve.interpolation = eAnimationInterpolation::CubicSpline;
        }

        JUG_RETURN_IF_ERROR(ReadAccessor_(_sampler.input, cgltf_type_scalar, _sampler.input->count, _curve.times));
        for (size_t i = 0; i < _curve.times.size(); ++i)
        {
            if (!std::isfinite(_curve.times[i]) || (i > 0 && _curve.times[i] < _curve.times[i - 1]))
            {
                JUG_CORE_LOG_ERROR("ImportModel: animation key times must be finite and non-decreasing");
                return eGraphicsError::ModelInvalidData;
            }
        }

        // 큐빅은 키마다 [in 탄젠트, 값, out 탄젠트] 순으로 묶여 있어서 분리
        const size_t numKeys   = _curve.times.size();
        const size_t numValues = numKeys * _numPerKey;
        const bool   bCubic    = _curve.interpolation == eAnimationInterpolation::CubicSpline;

        JUG_RETURN_IF_ERROR(ReadAccessor_(_sampler.output, _type, bCubic ? numValues * 3 : numValues, _curve.values));
        if (!bCubic)
        {
            return kOK;
        }

        // 값은 앞쪽으로 제자리 압축 (쓰는 위치가 항상 아직 안 읽은 위치보다 앞)
        _curve.inTangents.resize(numValues);
        _curve.outTangents.resize(numValues);
        for (size_t k = 0; k < numKeys; ++k)
        {
            for (size_t j = 0; j < _numPerKey; ++j)
            {
                const size_t src = k * _numPerKey * 3 + j;
                const size_t dst = k * _numPerKey + j;

                _curve.inTangents[dst]  = _curve.values[src];
                _curve.outTangents[dst] = _curve.values[src + _numPerKey * 2];
                _curve.values[dst]      = _curve.values[src + _numPerKey];
            }
        }
        _curve.values.resize(numValues);
        return kOK;
    }

    [[nodiscard]] Error ConvertChannel_(
        const GltfContext&             _ctx,
        const cgltf_animation_channel& _src,
        AnimationTrack&                _outTrack)
    {
        const cgltf_animation_sampler& sampler = *_src.sampler;
        switch (_src.target_path)
        {
            case cgltf_animation_path_type_translation:
            {
                AnimationCurve<VEC3>& curve = _outTrack.translation;
                JUG_RETURN_IF_ERROR(ReadCurve_(sampler, cgltf_type_vec3, 1, curve));
                FlipZ_(curve.values, _ctx.config.unitScale);
                FlipZ_(curve.inTangents, _ctx.config.unitScale);
                FlipZ_(curve.outTangents, _ctx.config.unitScale);
                return kOK;
            }
            case cgltf_animation_path_type_rotation:
            {
                AnimationCurve<QUAT>& curve = _outTrack.rotation;
                JUG_RETURN_IF_ERROR(ReadCurve_(sampler, cgltf_type_vec4, 1, curve));
                ConvertRotations_(curve.values);
                ConvertRotations_(curve.inTangents);
                ConvertRotations_(curve.outTangents);
                for (QUAT& q: curve.values)
                {
                    q = NormalizeRotation(q);
                }
                return kOK;
            }
            case cgltf_animation_path_type_scale:
                return ReadCurve_(sampler, cgltf_type_vec3, 1, _outTrack.scale);
            case cgltf_animation_path_type_weights:
                return ReadCurve_(sampler, cgltf_type_scalar, _src.target_node->mesh->primitives[0].targets_count, _outTrack.morphWeights);
            default:
                return kOK;
        }
    }

    [[nodiscard]] bool HasCurve_(
        const cgltf_animation_path_type _path,
        const AnimationTrack&           _track)
    {
        switch (_path)
        {
            case cgltf_animation_path_type_translation:
                return !_track.translation.times.empty();
            case cgltf_animation_path_type_rotation:
                return !_track.rotation.times.empty();
            case cgltf_animation_path_type_scale:
                return !_track.scale.times.empty();
            case cgltf_animation_path_type_weights:
                return !_track.morphWeights.times.empty();
            default:
                return false;
        }
    }

    [[nodiscard]] Error ConvertAnimations_(
        GltfContext& _ctx)
    {
        const bool bMorph = !_ctx.config.flags.Has(eModelImportOption::SkipMorphTargets) && !_ctx.config.flags.Has(eModelImportOption::SkipMeshes);

        Vector<uint32_t> trackIndicesOrInvalid(_ctx.data.nodes_count, kInvalidIndex);   // 노드 -> 현재 애니메이션의 트랙

        _ctx.model.animations.resize(_ctx.data.animations_count);
        for (cgltf_size a = 0; a < _ctx.data.animations_count; ++a)
        {
            const cgltf_animation& src = _ctx.data.animations[a];
            Animation&             dst = _ctx.model.animations[a];

            dst.name = ToName_(src.name);
            std::fill(trackIndicesOrInvalid.begin(), trackIndicesOrInvalid.end(), kInvalidIndex);
            for (cgltf_size c = 0; c < src.channels_count; ++c)
            {
                const cgltf_animation_channel& channel = src.channels[c];
                if (channel.target_node == nullptr)
                {
                    continue;
                }
                if (channel.target_path == cgltf_animation_path_type_weights && (!bMorph || channel.target_node->mesh->primitives[0].targets_count == 0))
                {
                    continue;
                }

                const size_t nodeIndex = cgltf_node_index(&_ctx.data, channel.target_node);
                if (trackIndicesOrInvalid[nodeIndex] == kInvalidIndex)
                {
                    trackIndicesOrInvalid[nodeIndex] = static_cast<uint32_t>(dst.tracks.size());
                    dst.tracks.emplace_back();
                    dst.tracks.back().nodeIndexOrInvalid = static_cast<uint32_t>(nodeIndex);
                }

                AnimationTrack& track = dst.tracks[trackIndicesOrInvalid[nodeIndex]];
                if (HasCurve_(channel.target_path, track))
                {
                    JUG_CORE_LOG_WARN("ImportModel: animation '{}' has more than one channel for the same node and path - ignored", dst.name);
                    continue;
                }
                JUG_RETURN_IF_ERROR(ConvertChannel_(_ctx, channel, track));
            }

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

    [[nodiscard]] Result<Model> ConvertGltf_(
        cgltf_data&              _data,
        const ModelImportConfig& _config,
        const FilePath&          _baseDir)
    {
        Model       model = {};
        GltfContext ctx { _data, _config, _baseDir, model };

        JUG_RETURN_IF_ERROR(CheckRequiredExtensions_(_data));
        JUG_RETURN_IF_ERROR(LoadBuffers_(_data, _baseDir, ctx.bufferFiles));
        if (cgltf_validate(&_data) != cgltf_result_success)
        {
            JUG_CORE_LOG_ERROR("ImportModel: glTF validation failed (invalid accessor, index or structure)");
            return eGraphicsError::ModelInvalidData;
        }

        JUG_RETURN_IF_ERROR(ConvertNodes_(ctx));
        if (!_config.flags.Has(eModelImportOption::SkipMeshes))
        {
            if (!_config.flags.Has(eModelImportOption::SkipMaterials))
            {
                ctx.imageTextures.assign(_data.images_count, kNotLoaded);
                model.materials.resize(_data.materials_count);
                for (cgltf_size i = 0; i < _data.materials_count; ++i)
                {
                    ConvertMaterial_(ctx, _data.materials[i], model.materials[i]);
                }
            }
            JUG_RETURN_IF_ERROR(ConvertMeshGroups_(ctx));
            if (!_config.flags.Has(eModelImportOption::SkipSkins))
            {
                JUG_RETURN_IF_ERROR(ConvertSkins_(ctx));
                JUG_RETURN_IF_ERROR(CheckBoneIndices_(model));
            }
        }
        if (!_config.flags.Has(eModelImportOption::SkipAnimations))
        {
            JUG_RETURN_IF_ERROR(ConvertAnimations_(ctx));
        }
        return std::move(model);
    }
}   // namespace

Result<Model> ImportGltf(
    const MemoryView         _mem,
    const ModelImportConfig& _config,
    const FilePath&          _baseDirOrEmpty)
{
    const cgltf_options options = {};
    cgltf_data*         pData   = nullptr;

    switch (cgltf_parse(&options, _mem.GetPtr(), _mem.GetSize(), &pData))
    {
        case cgltf_result_success:
            break;
        case cgltf_result_legacy_gltf:
            JUG_CORE_LOG_ERROR("ImportModel: glTF 1.0 is not supported");
            return eGraphicsError::ModelUnsupportedFeature;
        case cgltf_result_invalid_gltf:
            JUG_CORE_LOG_ERROR("ImportModel: the glTF structure is invalid (bad index or missing field)");
            return eGraphicsError::ModelInvalidData;
        default:
            JUG_CORE_LOG_ERROR("ImportModel: failed to parse the glTF");
            return eGraphicsError::ModelParseFailed;
    }

    Result<Model> result = ConvertGltf_(*pData, _config, _baseDirOrEmpty);
    cgltf_free(pData);
    return result;
}

}   // namespace jug::model_importer_detail
