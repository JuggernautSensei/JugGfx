#include "pch.h"
#include "ModelDetail.h"

#define CGLTF_IMPLEMENTATION
#include <algorithm>
#include <cgltf.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "Error.h"

namespace jug
{

namespace
{

    // ===========================================
    //  glTF
    // ===========================================

    // 허용하는 필수 확장. 그 외 extensionsRequired 는 거부한다.
    constexpr ARRAY<StringView, 3> kAllowedRequiredExtensions = {
        "KHR_mesh_quantization",
        "KHR_texture_transform",
        "KHR_materials_emissive_strength",
    };

    // glTF sampler 상수
    constexpr cgltf_int kFilterNearest              = 9728;
    constexpr cgltf_int kFilterLinear               = 9729;
    constexpr cgltf_int kFilterNearestMipmapNearest = 9984;
    constexpr cgltf_int kFilterLinearMipmapNearest  = 9985;
    constexpr cgltf_int kFilterNearestMipmapLinear  = 9986;
    constexpr cgltf_int kWrapClampToEdge            = 33071;
    constexpr cgltf_int kWrapMirroredRepeat         = 33648;

    constexpr size_t kNumMaxBoneSets = 2;   // JOINTS_0/1, WEIGHTS_0/1

    struct CgltfDataDeleter
    {
        void operator()(
            cgltf_data* _pData) const
        {
            cgltf_free(_pData);
        }
    };
    using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDataDeleter>;

    // ===========================================
    //  glTF / Utils
    // ===========================================

    [[nodiscard]] String GltfToString_(
        const char* _pStrOrNull)
    {
        return _pStrOrNull != nullptr ? String { _pStrOrNull } : String {};
    }

    [[nodiscard]] const char* GltfToString_(
        const cgltf_result _result)
    {
        switch (_result)
        {
            case cgltf_result_success:
                return "success";
            case cgltf_result_data_too_short:
                return "data too short";
            case cgltf_result_unknown_format:
                return "unknown format";
            case cgltf_result_invalid_json:
                return "invalid json";
            case cgltf_result_invalid_gltf:
                return "invalid gltf";
            case cgltf_result_invalid_options:
                return "invalid options";
            case cgltf_result_file_not_found:
                return "file not found";
            case cgltf_result_io_error:
                return "io error";
            case cgltf_result_out_of_memory:
                return "out of memory";
            case cgltf_result_legacy_gltf:
                return "legacy gltf (1.0) is not supported";
            default:
                return "unknown";
        }
    }

    // glTF(우수, CCW, 열 벡터) -> 엔진(좌수, CW, 행 벡터). Z 를 반전하면 CCW 가 CW 가 되므로 인덱스 순서는 그대로 둔다.
    [[nodiscard]] VEC3 ConvertPosition_(
        const float* _p)
    {
        return VEC3 { _p[0], _p[1], -_p[2] };
    }

    [[nodiscard]] VEC4 ConvertTangent_(
        const float* _p)
    {
        return VEC4 { _p[0], _p[1], -_p[2], -_p[3] };
    }

    [[nodiscard]] QUAT ConvertRotation_(
        const float* _p)
    {
        return QUAT { -_p[0], -_p[1], _p[2], _p[3] };
    }

    // float[16] 은 열 우선 열 벡터 행렬이므로 순서대로 읽으면 행 벡터 행렬이 된다. 그 뒤 S * M * S (S = diag(1, 1, -1, 1)).
    [[nodiscard]] MAT4 ConvertMatrix_(
        const float* _p)
    {
        MAT4 mat {
            _p[0], _p[1], _p[2], _p[3], _p[4], _p[5], _p[6], _p[7], _p[8], _p[9], _p[10], _p[11], _p[12], _p[13], _p[14], _p[15]
        };
        mat.m20 = -mat.m20;
        mat.m21 = -mat.m21;
        mat.m23 = -mat.m23;
        mat.m02 = -mat.m02;
        mat.m12 = -mat.m12;
        mat.m32 = -mat.m32;
        return mat;
    }

    // ===========================================
    //  glTF / Buffers
    // ===========================================

    // base64 data URI 의 페이로드 디코딩 후 크기
    [[nodiscard]] size_t CalcBase64DecodedSize_(
        const char* _pBase64)
    {
        const size_t len = std::strlen(_pBase64);
        size_t       pad = 0;
        while (pad < len && pad < 2 && _pBase64[len - 1 - pad] == '=')
        {
            ++pad;
        }
        return len * 3 / 4 - pad;
    }

    // 실패하면 false. 호출자가 상황에 맞게 로그를 남긴다.
    [[nodiscard]] bool TryDecodeBase64Uri_(
        const char*  _pUri,
        const size_t _decodedSizeOrInvalid,
        ByteBuffer&  _out)
    {
        const char* pCommaOrNull = std::strchr(_pUri, ',');
        if (pCommaOrNull == nullptr || pCommaOrNull - _pUri < 7 || std::strncmp(pCommaOrNull - 7, ";base64", 7) != 0)
        {
            return false;
        }

        const size_t size = _decodedSizeOrInvalid != kInvalidIndex ? _decodedSizeOrInvalid : CalcBase64DecodedSize_(pCommaOrNull + 1);
        if (size == 0)
        {
            return false;
        }

        const cgltf_options options = {};
        void*               pRaw    = nullptr;
        if (cgltf_load_buffer_base64(&options, size, pCommaOrNull + 1, &pRaw) != cgltf_result_success)
        {
            return false;
        }

        _out.ResizeUninitialized(size);
        std::memcpy(_out.GetPtr(), pRaw, size);
        std::free(pRaw);   // cgltf 기본 할당자는 malloc/free
        return true;
    }

    // URI 의 퍼센트 인코딩을 풀어 경로로 만든다.
    [[nodiscard]] FilePath MakeFilePathFromUri_(
        const FilePath& _baseDir,
        const char*     _pUri)
    {
        String decoded = _pUri;
        decoded.resize(cgltf_decode_uri(decoded.data()));
        return _baseDir / FilePath { EncodeUtf16(decoded) };
    }

    [[nodiscard]] bool IsDataUri_(
        const char* _pUri)
    {
        return std::strncmp(_pUri, "data:", 5) == 0;
    }

    Error ValidateExtensions_(
        const cgltf_data* _pData)
    {
        for (size_t i = 0; i < _pData->extensions_required_count; ++i)
        {
            const StringView ext = _pData->extensions_required[i];

            bool bAllowed = false;
            for (const StringView allowed: kAllowedRequiredExtensions)
            {
                if (ext == allowed)
                {
                    bAllowed = true;
                    break;
                }
            }
            if (!bAllowed)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: required extension '{}' is not supported.", ext);
                return eGraphicsError::ModelUnsupportedFeature;
            }
        }
        return kOK;
    }

    // 모든 버퍼를 읽어 _storage 에 보관하고 cgltf 가 가리키게 한다. cgltf 는 이 메모리를 해제하지 않는다.
    Error ResolveBuffers_(
        const cgltf_data*   _pData,
        Vector<ByteBuffer>& _storage,
        const FilePath&     _baseDirOrEmpty)
    {
        _storage = Vector<ByteBuffer>(_pData->buffers_count);

        for (size_t i = 0; i < _pData->buffers_count; ++i)
        {
            cgltf_buffer& buffer    = _pData->buffers[i];
            buffer.data_free_method = cgltf_data_free_method_none;

            if (buffer.uri == nullptr)
            {
                // GLB 의 BIN 청크
                if (i != 0 || _pData->bin == nullptr || _pData->bin_size < buffer.size)
                {
                    JUG_CORE_LOG_ERROR("Failed to import glTF: buffer {} has no uri and no matching GLB binary chunk.", i);
                    return eGraphicsError::ModelMissingResource;
                }
                buffer.data = const_cast<void*>(_pData->bin);
                continue;
            }

            if (IsDataUri_(buffer.uri))
            {
                if (!TryDecodeBase64Uri_(buffer.uri, buffer.size, _storage[i]))
                {
                    JUG_CORE_LOG_ERROR("Failed to import glTF: buffer {} has an invalid base64 data uri.", i);
                    return eGraphicsError::ModelInvalidData;
                }
            }
            else if (std::strstr(buffer.uri, "://") != nullptr)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: buffer {} uses an unsupported uri '{}'.", i, buffer.uri);
                return eGraphicsError::ModelMissingResource;
            }
            else
            {
                if (_baseDirOrEmpty.empty())
                {
                    JUG_CORE_LOG_ERROR("Failed to import glTF: buffer {} references an external file '{}', which cannot be resolved from memory.", i, buffer.uri);
                    return eGraphicsError::ModelMissingResource;
                }

                const FilePath path  = MakeFilePathFromUri_(_baseDirOrEmpty, buffer.uri);
                const Error    error = model_detail::ReadFile(path, _storage[i]);
                if (error)
                {
                    JUG_CORE_LOG_ERROR("Failed to import glTF: cannot read buffer {} '{}': {}", i, EncodeUtf8(path.wstring()), error.MakeMessage());
                    return eGraphicsError::ModelMissingResource;
                }
            }

            if (_storage[i].GetSize() < buffer.size)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: buffer {} is too short ({} < {} bytes).", i, _storage[i].GetSize(), buffer.size);
                return eGraphicsError::ModelInvalidData;
            }
            buffer.data = _storage[i].GetPtr();
        }
        return kOK;
    }

    // ===========================================
    //  glTF / Accessor
    // ===========================================

    Error CheckAccessor_(
        const cgltf_accessor* _pAccessor,
        const cgltf_type      _expectedType,
        const size_t          _expectedCount,
        const char*           _pWhat,
        const size_t          _meshIndex,
        const size_t          _primitiveIndex)
    {
        if (_pAccessor->type != _expectedType)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} {} accessor has an unexpected type.", _meshIndex, _primitiveIndex, _pWhat);
            return eGraphicsError::ModelInvalidData;
        }
        if (_pAccessor->count != _expectedCount)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} {} accessor count {} != vertex count {}.", _meshIndex, _primitiveIndex, _pWhat, _pAccessor->count, _expectedCount);
            return eGraphicsError::ModelInvalidData;
        }
        return kOK;
    }

    // sparse/normalized 정수까지 float 로 풀어서 읽는다.
    Error ReadFloats_(
        const cgltf_accessor* _pAccessor,
        Buffer<float>&        _out,
        const char*           _pWhat)
    {
        const size_t numFloats = cgltf_accessor_unpack_floats(_pAccessor, nullptr, 0);
        _out.ResizeUninitialized(numFloats);
        if (numFloats != 0 && cgltf_accessor_unpack_floats(_pAccessor, _out.GetPtr(), numFloats) != numFloats)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: cannot read {} accessor data.", _pWhat);
            return eGraphicsError::ModelInvalidData;
        }
        return kOK;
    }

    Error ReadVec3List_(
        const cgltf_accessor* _pAccessor,
        Vector<VEC3>&         _out,
        const char*           _pWhat)
    {
        Buffer<float> floats = {};
        JUG_RETURN_IF_ERROR(ReadFloats_(_pAccessor, floats, _pWhat));

        _out.reserve(_pAccessor->count);
        for (size_t i = 0; i < _pAccessor->count; ++i)
        {
            _out.push_back(ConvertPosition_(floats.GetPtr() + i * 3));
        }
        return kOK;
    }

    Error ReadTangentList_(
        const cgltf_accessor* _pAccessor,
        Vector<VEC4>&         _out)
    {
        Buffer<float> floats = {};
        JUG_RETURN_IF_ERROR(ReadFloats_(_pAccessor, floats, "TANGENT"));

        _out.reserve(_pAccessor->count);
        for (size_t i = 0; i < _pAccessor->count; ++i)
        {
            _out.push_back(ConvertTangent_(floats.GetPtr() + i * 4));
        }
        return kOK;
    }

    Error ReadColorList_(
        const cgltf_accessor* _pAccessor,
        Vector<VEC4>&         _out)
    {
        Buffer<float> floats = {};
        JUG_RETURN_IF_ERROR(ReadFloats_(_pAccessor, floats, "COLOR"));

        const size_t numComponents = cgltf_num_components(_pAccessor->type);
        _out.reserve(_pAccessor->count);
        for (size_t i = 0; i < _pAccessor->count; ++i)
        {
            const float* p = floats.GetPtr() + i * numComponents;
            _out.push_back(VEC4 { p[0], p[1], p[2], numComponents == 4 ? p[3] : 1.f });
        }
        return kOK;
    }

    Error ReadTexCoordList_(
        const cgltf_accessor* _pAccessor,
        Vector<VEC2>&         _out)
    {
        Buffer<float> floats = {};
        JUG_RETURN_IF_ERROR(ReadFloats_(_pAccessor, floats, "TEXCOORD"));

        _out.reserve(_pAccessor->count);
        for (size_t i = 0; i < _pAccessor->count; ++i)
        {
            const float* p = floats.GetPtr() + i * 2;
            _out.push_back(VEC2 { p[0], p[1] });   // glTF 의 UV 원점(좌상단)은 D3D 와 같다.
        }
        return kOK;
    }

    // ===========================================
    //  glTF / Mesh
    // ===========================================

    struct PrimitiveAttributes
    {
        const cgltf_accessor*                                         position      = nullptr;
        const cgltf_accessor*                                         normalOrNull  = nullptr;
        const cgltf_accessor*                                         tangentOrNull = nullptr;
        ARRAY<const cgltf_accessor*, kNumMaxVertexColorAttributes>    colors        = {};
        ARRAY<const cgltf_accessor*, kNumMaxVertexTexCoordAttributes> texCoords     = {};
        ARRAY<const cgltf_accessor*, kNumMaxBoneSets>                 joints        = {};
        ARRAY<const cgltf_accessor*, kNumMaxBoneSets>                 weights       = {};
    };

    PrimitiveAttributes CollectAttributes_(
        const cgltf_attribute* _pAttributes,
        const size_t           _numAttributes,
        const size_t           _meshIndex,
        const size_t           _primitiveIndex)
    {
        PrimitiveAttributes out = {};
        for (size_t i = 0; i < _numAttributes; ++i)
        {
            const cgltf_attribute& attrib = _pAttributes[i];
            const size_t           index  = attrib.index >= 0 ? static_cast<size_t>(attrib.index) : 0;

            switch (attrib.type)
            {
                case cgltf_attribute_type_position:
                    out.position = attrib.data;
                    break;
                case cgltf_attribute_type_normal:
                    out.normalOrNull = attrib.data;
                    break;
                case cgltf_attribute_type_tangent:
                    out.tangentOrNull = attrib.data;
                    break;
                case cgltf_attribute_type_texcoord:
                    if (index < kNumMaxVertexTexCoordAttributes)
                    {
                        out.texCoords[index] = attrib.data;
                    }
                    else
                    {
                        JUG_CORE_LOG_WARN("glTF mesh {} primitive {}: TEXCOORD_{} is ignored (max {} sets).", _meshIndex, _primitiveIndex, index, kNumMaxVertexTexCoordAttributes);
                    }
                    break;
                case cgltf_attribute_type_color:
                    if (index < kNumMaxVertexColorAttributes)
                    {
                        out.colors[index] = attrib.data;
                    }
                    else
                    {
                        JUG_CORE_LOG_WARN("glTF mesh {} primitive {}: COLOR_{} is ignored (max {} sets).", _meshIndex, _primitiveIndex, index, kNumMaxVertexColorAttributes);
                    }
                    break;
                case cgltf_attribute_type_joints:
                    if (index < kNumMaxBoneSets)
                    {
                        out.joints[index] = attrib.data;
                    }
                    break;
                case cgltf_attribute_type_weights:
                    if (index < kNumMaxBoneSets)
                    {
                        out.weights[index] = attrib.data;
                    }
                    break;
                default:
                    break;
            }
        }
        return out;
    }

    // 속성 accessor 들의 타입/개수를 검증한다.
    Error CheckAttributes_(
        const PrimitiveAttributes& _attribs,
        const size_t               _vertexCount,
        const size_t               _meshIndex,
        const size_t               _primitiveIndex)
    {
        if (_attribs.normalOrNull != nullptr)
        {
            JUG_RETURN_IF_ERROR(CheckAccessor_(_attribs.normalOrNull, cgltf_type_vec3, _vertexCount, "NORMAL", _meshIndex, _primitiveIndex));
        }
        if (_attribs.tangentOrNull != nullptr)
        {
            JUG_RETURN_IF_ERROR(CheckAccessor_(_attribs.tangentOrNull, cgltf_type_vec4, _vertexCount, "TANGENT", _meshIndex, _primitiveIndex));
        }
        for (const cgltf_accessor* pColorOrNull: _attribs.colors)
        {
            if (pColorOrNull == nullptr)
            {
                continue;
            }
            if (pColorOrNull->type != cgltf_type_vec3 && pColorOrNull->type != cgltf_type_vec4)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} COLOR accessor must be VEC3 or VEC4.", _meshIndex, _primitiveIndex);
                return eGraphicsError::ModelInvalidData;
            }
            if (pColorOrNull->count != _vertexCount)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} COLOR accessor count {} != vertex count {}.", _meshIndex, _primitiveIndex, pColorOrNull->count, _vertexCount);
                return eGraphicsError::ModelInvalidData;
            }
        }
        for (const cgltf_accessor* pTexCoordOrNull: _attribs.texCoords)
        {
            if (pTexCoordOrNull != nullptr)
            {
                JUG_RETURN_IF_ERROR(CheckAccessor_(pTexCoordOrNull, cgltf_type_vec2, _vertexCount, "TEXCOORD", _meshIndex, _primitiveIndex));
            }
        }
        for (size_t i = 0; i < kNumMaxBoneSets; ++i)
        {
            if (_attribs.joints[i] != nullptr)
            {
                JUG_RETURN_IF_ERROR(CheckAccessor_(_attribs.joints[i], cgltf_type_vec4, _vertexCount, "JOINTS", _meshIndex, _primitiveIndex));
            }
            if (_attribs.weights[i] != nullptr)
            {
                JUG_RETURN_IF_ERROR(CheckAccessor_(_attribs.weights[i], cgltf_type_vec4, _vertexCount, "WEIGHTS", _meshIndex, _primitiveIndex));
            }
        }
        return kOK;
    }

    // 삼각형 인덱스 리스트를 만든다. 순서는 glTF 그대로 유지한다. (Z 반전이 CCW 를 CW 로 바꿈)
    Error BuildTriangleIndices_(
        const cgltf_primitive& _primitive,
        const size_t           _vertexCount,
        const size_t           _meshIndex,
        const size_t           _primitiveIndex,
        Vector<uint32_t>&      _out)
    {
        // 원본 인덱스 읽기
        Buffer<uint32_t> src = {};
        if (_primitive.indices != nullptr)
        {
            const size_t count = _primitive.indices->count;
            src.ResizeUninitialized(count);
            if (count != 0 && cgltf_accessor_unpack_indices(_primitive.indices, src.GetPtr(), sizeof(uint32_t), count) != count)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} cannot read the index accessor.", _meshIndex, _primitiveIndex);
                return eGraphicsError::ModelInvalidData;
            }
        }
        else
        {
            src.ResizeUninitialized(_vertexCount);
            for (size_t i = 0; i < _vertexCount; ++i)
            {
                src[i] = static_cast<uint32_t>(i);
            }
        }

        for (size_t i = 0; i < src.GetSize(); ++i)
        {
            if (src[i] >= _vertexCount)
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} index {} is out of range ({} >= {}).", _meshIndex, _primitiveIndex, i, src[i], _vertexCount);
                return eGraphicsError::ModelInvalidData;
            }
        }

        const size_t count = src.GetSize();
        switch (_primitive.type)
        {
            case cgltf_primitive_type_triangles:
            {
                if (count % 3 != 0)
                {
                    JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} index count {} is not a multiple of 3.", _meshIndex, _primitiveIndex, count);
                    return eGraphicsError::ModelInvalidData;
                }
                _out.assign(src.Begin(), src.End());
                break;
            }
            case cgltf_primitive_type_triangle_strip:
            {
                if (count >= 3)
                {
                    _out.reserve((count - 2) * 3);
                    for (size_t i = 0; i + 2 < count; ++i)
                    {
                        _out.push_back(src[i]);
                        _out.push_back(src[i + 1 + i % 2]);
                        _out.push_back(src[i + 2 - i % 2]);
                    }
                }
                break;
            }
            case cgltf_primitive_type_triangle_fan:
            {
                if (count >= 3)
                {
                    _out.reserve((count - 2) * 3);
                    for (size_t i = 0; i + 2 < count; ++i)
                    {
                        _out.push_back(src[i + 1]);
                        _out.push_back(src[i + 2]);
                        _out.push_back(src[0]);
                    }
                }
                break;
            }
            default:
                JUG_ASSERT(false, "Not a triangle primitive");
                break;
        }
        return kOK;
    }

    // JOINTS/WEIGHTS 를 읽어 정점당 상위 4개 영향으로 정리한다.
    Error ReadBoneInfluences_(
        const PrimitiveAttributes& _attribs,
        const size_t               _vertexCount,
        ModelMesh&                 _mesh)
    {
        size_t numSets = 0;
        for (size_t i = 0; i < kNumMaxBoneSets; ++i)
        {
            if (_attribs.joints[i] != nullptr && _attribs.weights[i] != nullptr)
            {
                ++numSets;
            }
            else
            {
                if (_attribs.joints[i] != nullptr || _attribs.weights[i] != nullptr)
                {
                    JUG_CORE_LOG_WARN("glTF skin set {} has only one of JOINTS/WEIGHTS; it is ignored.", i);
                }
                break;   // 세트는 0 부터 연속이어야 한다.
            }
        }
        if (numSets == 0)
        {
            return kOK;
        }

        ARRAY<Buffer<float>, kNumMaxBoneSets> joints  = {};
        ARRAY<Buffer<float>, kNumMaxBoneSets> weights = {};
        for (size_t i = 0; i < numSets; ++i)
        {
            JUG_RETURN_IF_ERROR(ReadFloats_(_attribs.joints[i], joints[i], "JOINTS"));
            JUG_RETURN_IF_ERROR(ReadFloats_(_attribs.weights[i], weights[i], "WEIGHTS"));
        }

        ARRAY<uint16_t, kNumMaxBoneSets * 4> candidateIndices = {};
        ARRAY<float, kNumMaxBoneSets * 4>    candidateWeights = {};

        _mesh.boneIndices.reserve(_vertexCount);
        _mesh.boneWeights.reserve(_vertexCount);
        for (size_t v = 0; v < _vertexCount; ++v)
        {
            for (size_t set = 0; set < numSets; ++set)
            {
                for (size_t k = 0; k < 4; ++k)
                {
                    const float joint             = joints[set][v * 4 + k];
                    candidateIndices[set * 4 + k] = static_cast<uint16_t>(joint + 0.5f);
                    candidateWeights[set * 4 + k] = weights[set][v * 4 + k];
                }
            }

            ARRAY<uint16_t, kNumMaxBoneInfluences> outIndices = {};
            ARRAY<float, kNumMaxBoneInfluences>    outWeights = {};
            model_detail::SelectBoneInfluences(Span<const uint16_t> { candidateIndices.data(), numSets * 4 }, Span<const float> { candidateWeights.data(), numSets * 4 }, outIndices, outWeights);
            _mesh.boneIndices.push_back(outIndices);
            _mesh.boneWeights.push_back(outWeights);
        }
        return kOK;
    }

    Error ReadMorphTargets_(
        const cgltf_primitive& _primitive,
        const size_t           _vertexCount,
        const size_t           _meshIndex,
        const size_t           _primitiveIndex,
        ModelMesh&             _mesh)
    {
        _mesh.morphTargets.reserve(_primitive.targets_count);
        for (size_t t = 0; t < _primitive.targets_count; ++t)
        {
            const cgltf_morph_target& target = _primitive.targets[t];

            ModelMorphTarget morph = {};
            for (size_t a = 0; a < target.attributes_count; ++a)
            {
                const cgltf_attribute& attrib = target.attributes[a];
                if (attrib.type != cgltf_attribute_type_position && attrib.type != cgltf_attribute_type_normal && attrib.type != cgltf_attribute_type_tangent)
                {
                    continue;
                }

                JUG_RETURN_IF_ERROR(CheckAccessor_(attrib.data, cgltf_type_vec3, _vertexCount, "morph target", _meshIndex, _primitiveIndex));
                if (attrib.type == cgltf_attribute_type_position)
                {
                    JUG_RETURN_IF_ERROR(ReadVec3List_(attrib.data, morph.positionDeltas, "morph POSITION"));
                }
                else if (attrib.type == cgltf_attribute_type_normal)
                {
                    JUG_RETURN_IF_ERROR(ReadVec3List_(attrib.data, morph.normalDeltas, "morph NORMAL"));
                }
                else
                {
                    JUG_RETURN_IF_ERROR(ReadVec3List_(attrib.data, morph.tangentDeltas, "morph TANGENT"));
                }
            }
            _mesh.morphTargets.push_back(std::move(morph));
        }
        return kOK;
    }

    // 변환한 primitive 가 있으면 _bOutConverted = true
    Error ConvertPrimitive_(
        const cgltf_data*        _pData,
        const cgltf_primitive&   _primitive,
        const size_t             _meshIndex,
        const size_t             _primitiveIndex,
        const ModelImportConfig& _config,
        ModelMesh&               _mesh,
        bool&                    _bOutConverted)
    {
        _bOutConverted = false;

        if (_primitive.type != cgltf_primitive_type_triangles && _primitive.type != cgltf_primitive_type_triangle_strip && _primitive.type != cgltf_primitive_type_triangle_fan)
        {
            JUG_CORE_LOG_WARN("glTF mesh {} primitive {}: only triangle primitives are supported; skipped.", _meshIndex, _primitiveIndex);
            return kOK;
        }

        const PrimitiveAttributes attribs = CollectAttributes_(_primitive.attributes, _primitive.attributes_count, _meshIndex, _primitiveIndex);
        if (attribs.position == nullptr)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} has no POSITION attribute.", _meshIndex, _primitiveIndex);
            return eGraphicsError::ModelInvalidData;
        }
        if (attribs.position->type != cgltf_type_vec3)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} POSITION accessor must be VEC3.", _meshIndex, _primitiveIndex);
            return eGraphicsError::ModelInvalidData;
        }

        const size_t vertexCount = attribs.position->count;
        if (vertexCount > Max<uint32_t>())
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: mesh {} primitive {} has too many vertices ({}).", _meshIndex, _primitiveIndex, vertexCount);
            return eGraphicsError::ModelLimitExceeded;
        }
        JUG_RETURN_IF_ERROR(CheckAttributes_(attribs, vertexCount, _meshIndex, _primitiveIndex));

        JUG_RETURN_IF_ERROR(BuildTriangleIndices_(_primitive, vertexCount, _meshIndex, _primitiveIndex, _mesh.indices));
        if (_mesh.indices.empty())
        {
            JUG_CORE_LOG_WARN("glTF mesh {} primitive {}: no triangles; skipped.", _meshIndex, _primitiveIndex);
            return kOK;
        }

        JUG_RETURN_IF_ERROR(ReadVec3List_(attribs.position, _mesh.positions, "POSITION"));
        if (attribs.normalOrNull != nullptr)
        {
            JUG_RETURN_IF_ERROR(ReadVec3List_(attribs.normalOrNull, _mesh.normals, "NORMAL"));
        }
        if (attribs.tangentOrNull != nullptr)
        {
            JUG_RETURN_IF_ERROR(ReadTangentList_(attribs.tangentOrNull, _mesh.tangents));
        }
        for (size_t i = 0; i < kNumMaxVertexColorAttributes; ++i)
        {
            if (attribs.colors[i] != nullptr)
            {
                JUG_RETURN_IF_ERROR(ReadColorList_(attribs.colors[i], _mesh.colors[i]));
            }
        }
        for (size_t i = 0; i < kNumMaxVertexTexCoordAttributes; ++i)
        {
            if (attribs.texCoords[i] != nullptr)
            {
                JUG_RETURN_IF_ERROR(ReadTexCoordList_(attribs.texCoords[i], _mesh.texCoords[i]));
            }
        }

        if (!_config.flags.Has(eModelImportOption::SkipSkins))
        {
            JUG_RETURN_IF_ERROR(ReadBoneInfluences_(attribs, vertexCount, _mesh));
        }
        if (!_config.flags.Has(eModelImportOption::SkipMorphTargets))
        {
            JUG_RETURN_IF_ERROR(ReadMorphTargets_(_primitive, vertexCount, _meshIndex, _primitiveIndex, _mesh));
        }

        if (_primitive.material != nullptr && !_config.flags.Has(eModelImportOption::SkipMaterials))
        {
            _mesh.materialIndexOrInvalid = static_cast<uint32_t>(cgltf_material_index(_pData, _primitive.material));
        }

        _bOutConverted = true;
        return kOK;
    }

    Error GltfConvertMeshes_(
        const cgltf_data*        _pData,
        const ModelImportConfig& _config,
        Model&                   _model)
    {
        _model.meshGroups.reserve(_pData->meshes_count);
        for (size_t m = 0; m < _pData->meshes_count; ++m)
        {
            const cgltf_mesh& mesh = _pData->meshes[m];

            ModelMeshGroup group = {};
            group.name           = GltfToString_(mesh.name);
            group.meshes.reserve(mesh.primitives_count);
            for (size_t p = 0; p < mesh.primitives_count; ++p)
            {
                ModelMesh converted  = {};
                bool      bConverted = false;
                JUG_RETURN_IF_ERROR(ConvertPrimitive_(_pData, mesh.primitives[p], m, p, _config, converted, bConverted));
                if (bConverted)
                {
                    group.meshes.push_back(std::move(converted));
                }
            }

            if (!_config.flags.Has(eModelImportOption::SkipMorphTargets) && mesh.weights != nullptr)
            {
                group.morphWeights.assign(mesh.weights, mesh.weights + mesh.weights_count);
            }
            _model.meshGroups.push_back(std::move(group));
        }
        return kOK;
    }

    // ===========================================
    //  glTF / Texture / Material
    // ===========================================

    [[nodiscard]] Flags<eSampler> MakeSamplerFlags_(
        const cgltf_sampler* _pSamplerOrNull)
    {
        cgltf_int magFilter = 0;
        cgltf_int minFilter = 0;
        cgltf_int wrapS     = 0;
        cgltf_int wrapT     = 0;
        if (_pSamplerOrNull != nullptr)
        {
            magFilter = _pSamplerOrNull->mag_filter;
            minFilter = _pSamplerOrNull->min_filter;
            wrapS     = _pSamplerOrNull->wrap_s;
            wrapT     = _pSamplerOrNull->wrap_t;
        }

        Flags<eSampler> flags = kNoneFlags;

        // mag: NEAREST 만 point, 미지정은 linear
        if (magFilter != kFilterNearest)
        {
            flags.Enable(eSampler::Filter_MagLinear);
        }

        // min/mip: 미지정과 LINEAR_MIPMAP_LINEAR 는 둘 다 linear
        bool bMinLinear = true;
        bool bMipLinear = true;
        if (minFilter == kFilterNearest || minFilter == kFilterNearestMipmapNearest)
        {
            bMinLinear = false;
            bMipLinear = false;
        }
        else if (minFilter == kFilterLinear || minFilter == kFilterLinearMipmapNearest)
        {
            bMipLinear = false;
        }
        else if (minFilter == kFilterNearestMipmapLinear)
        {
            bMinLinear = false;
        }
        flags.Set(eSampler::Filter_MinLinear, bMinLinear);
        flags.Set(eSampler::Filter_MipLinear, bMipLinear);

        if (wrapS == kWrapClampToEdge)
        {
            flags.Enable(eSampler::U_Clamp);
        }
        else if (wrapS == kWrapMirroredRepeat)
        {
            flags.Enable(eSampler::U_Mirror);
        }
        if (wrapT == kWrapClampToEdge)
        {
            flags.Enable(eSampler::V_Clamp);
        }
        else if (wrapT == kWrapMirroredRepeat)
        {
            flags.Enable(eSampler::V_Mirror);
        }
        return flags;
    }

    // 이미지 하나를 읽는다. 실패해도 치명적이지 않으므로 호출자가 경고를 남긴다.
    Result<Image> LoadImage_(
        const cgltf_image&              _image,
        const FilePath&                 _baseDirOrEmpty,
        const Flags<eImageImportOption> _imageFlags)
    {
        // bufferView
        if (_image.buffer_view != nullptr)
        {
            const cgltf_buffer_view& view        = *_image.buffer_view;
            const void*              pBaseOrNull = view.data != nullptr ? view.data : view.buffer->data;
            if (pBaseOrNull == nullptr)
            {
                return eGraphicsError::ModelMissingResource;
            }
            return ImportImage(MemoryView { static_cast<const std::byte*>(pBaseOrNull) + view.offset, view.size }, _imageFlags);
        }

        if (_image.uri == nullptr)
        {
            return eGraphicsError::ModelMissingResource;
        }

        // data uri
        if (IsDataUri_(_image.uri))
        {
            ByteBuffer bytes = {};
            if (!TryDecodeBase64Uri_(_image.uri, kInvalidIndex, bytes))
            {
                return eGraphicsError::ModelInvalidData;
            }
            return ImportImage(bytes, _imageFlags);
        }

        // 파일
        if (_baseDirOrEmpty.empty() || std::strstr(_image.uri, "://") != nullptr)
        {
            return eGraphicsError::ModelMissingResource;
        }
        return ImportImageFromFile(MakeFilePathFromUri_(_baseDirOrEmpty, _image.uri), _imageFlags);
    }

    // glTF image 인덱스 -> Model::textures 인덱스. 실패한 이미지는 kInvalidIndex.
    void LoadImages_(
        const cgltf_data*        _pData,
        const FilePath&          _baseDirOrEmpty,
        const ModelImportConfig& _config,
        Model&                   _model,
        Vector<uint32_t>&        _outTextureOfImage)   // 원소는 kInvalidIndex 가능
    {
        _outTextureOfImage.assign(_pData->images_count, kInvalidIndex);
        if (_config.flags.Has(eModelImportOption::SkipTextures))
        {
            return;
        }

        for (size_t i = 0; i < _pData->images_count; ++i)
        {
            const cgltf_image& image  = _pData->images[i];
            Result<Image>      loaded = LoadImage_(image, _baseDirOrEmpty, _config.imageFlags);
            if (loaded.HasError())
            {
                JUG_CORE_LOG_WARN("glTF image {} '{}' could not be loaded: {}", i, GltfToString_(image.name), loaded.GetError().MakeMessage());
                continue;
            }
            _outTextureOfImage[i] = static_cast<uint32_t>(_model.textures.size());
            _model.textures.push_back(loaded.Take());
        }
    }

    ModelTextureDesc GltfMakeTextureDesc_(
        const cgltf_data*            _pData,
        const cgltf_texture_view&    _view,
        const Vector<uint32_t>&      _textureOfImage,   // 원소는 kInvalidIndex 가능
        const Flags<eTextureChannel> _channelFlags)
    {
        ModelTextureDesc desc = {};
        if (_view.texture == nullptr)
        {
            return desc;
        }

        const cgltf_image* pImageOrNull = _view.texture->image != nullptr ? _view.texture->image : _view.texture->basisu_image;
        if (pImageOrNull == nullptr)
        {
            return desc;
        }

        const uint32_t textureIndexOrInvalid = _textureOfImage[cgltf_image_index(_pData, pImageOrNull)];
        if (!IsValid(textureIndexOrInvalid))
        {
            return desc;
        }

        desc.textureIndexOrInvalid = textureIndexOrInvalid;
        desc.channelFlags       = _channelFlags;
        desc.samplerFlags       = MakeSamplerFlags_(_view.texture->sampler);
        if (_view.texcoord >= 0 && static_cast<size_t>(_view.texcoord) < kNumMaxVertexTexCoordAttributes)
        {
            desc.texCoordSet = static_cast<uint32_t>(_view.texcoord);
        }
        else
        {
            JUG_CORE_LOG_WARN("glTF texCoord set {} is not supported (max {}); using set 0.", _view.texcoord, kNumMaxVertexTexCoordAttributes);
        }
        if (_view.has_transform)
        {
            JUG_CORE_LOG_WARN("glTF KHR_texture_transform is not applied.");
        }
        return desc;
    }

    ModelMaterial GltfConvertMaterial_(
        const cgltf_data*       _pData,
        const cgltf_material&   _material,
        const Vector<uint32_t>& _textureOfImage)   // 원소는 kInvalidIndex 가능
    {
        const Flags<eTextureChannel> kRgbFlags = { eTextureChannel::R, eTextureChannel::G, eTextureChannel::B };

        ModelMaterial out = {};
        out.name          = GltfToString_(_material.name);

        // pbrMetallicRoughness 가 없으면 glTF 기본값 (흰색, metallic 1, roughness 1)
        if (_material.has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness& pbr = _material.pbr_metallic_roughness;
            out.baseColor                           = VEC4 { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
            out.metallic                            = pbr.metallic_factor;
            out.roughness                           = pbr.roughness_factor;

            out.textures[eModelTexture::Albedo] = GltfMakeTextureDesc_(_pData, pbr.base_color_texture, _textureOfImage, eTextureChannel::RGBA);

            // metallicRoughness 텍스처: G = roughness, B = metallic
            out.textures[eModelTexture::Metallic]  = GltfMakeTextureDesc_(_pData, pbr.metallic_roughness_texture, _textureOfImage, eTextureChannel::B);
            out.textures[eModelTexture::Roughness] = GltfMakeTextureDesc_(_pData, pbr.metallic_roughness_texture, _textureOfImage, eTextureChannel::G);
        }
        else
        {
            out.baseColor = One<VEC4>();
            out.metallic  = 1.f;
            out.roughness = 1.f;
        }

        out.textures[eModelTexture::Normal] = GltfMakeTextureDesc_(_pData, _material.normal_texture, _textureOfImage, kRgbFlags);
        out.normalScale                     = _material.normal_texture.texture != nullptr ? _material.normal_texture.scale : 1.f;

        out.textures[eModelTexture::AmbientOcclusion] = GltfMakeTextureDesc_(_pData, _material.occlusion_texture, _textureOfImage, eTextureChannel::R);
        out.occlusionStrength                         = _material.occlusion_texture.texture != nullptr ? _material.occlusion_texture.scale : 1.f;

        const float emissiveStrength          = _material.has_emissive_strength ? _material.emissive_strength.emissive_strength : 1.f;
        out.emissive                          = VEC3 { _material.emissive_factor[0], _material.emissive_factor[1], _material.emissive_factor[2] } * emissiveStrength;
        out.textures[eModelTexture::Emissive] = GltfMakeTextureDesc_(_pData, _material.emissive_texture, _textureOfImage, kRgbFlags);

        switch (_material.alpha_mode)
        {
            case cgltf_alpha_mode_mask:
                out.alphaMode = eModelAlphaMode::AlphaCutOff;
                break;
            case cgltf_alpha_mode_blend:
                out.alphaMode = eModelAlphaMode::Blend;
                break;
            default:
                out.alphaMode = eModelAlphaMode::Opaque;
                break;
        }
        out.alphaCutOffThreshold = _material.alpha_cutoff;
        out.bDoubleSided         = _material.double_sided != 0;
        return out;
    }

    // ===========================================
    //  glTF / Node / Skin
    // ===========================================

    void GltfConvertNodes_(
        const cgltf_data*        _pData,
        const ModelImportConfig& _config,
        Model&                   _model)
    {
        const bool bSkipMeshes = _config.flags.Has(eModelImportOption::SkipMeshes);
        const bool bSkipSkins  = _config.flags.Has(eModelImportOption::SkipSkins);

        _model.nodes.reserve(_pData->nodes_count);
        for (size_t i = 0; i < _pData->nodes_count; ++i)
        {
            const cgltf_node& node = _pData->nodes[i];

            ModelNode out = {};
            out.name      = GltfToString_(node.name);

            if (node.has_matrix)
            {
                out.local = TRANSFORM::MakeFromMatrix(ConvertMatrix_(node.matrix));
            }
            else
            {
                if (node.has_translation)
                {
                    out.local.translation = ConvertPosition_(node.translation);
                }
                if (node.has_rotation)
                {
                    out.local.rotation = ConvertRotation_(node.rotation);
                }
                if (node.has_scale)
                {
                    out.local.scale = VEC3 { node.scale[0], node.scale[1], node.scale[2] };
                }
            }

            if (node.parent != nullptr)
            {
                out.parentIndexOrInvalid = static_cast<uint32_t>(cgltf_node_index(_pData, node.parent));
            }
            if (node.mesh != nullptr && !bSkipMeshes)
            {
                out.meshGroupIndexOrInvalid = static_cast<uint32_t>(cgltf_mesh_index(_pData, node.mesh));
            }
            if (node.skin != nullptr && !bSkipMeshes && !bSkipSkins)
            {
                out.skinIndexOrInvalid = static_cast<uint32_t>(cgltf_skin_index(_pData, node.skin));
            }
            _model.nodes.push_back(std::move(out));
        }
    }

    Error GltfConvertSkins_(
        const cgltf_data* _pData,
        Model&            _model)
    {
        _model.skins.reserve(_pData->skins_count);
        for (size_t s = 0; s < _pData->skins_count; ++s)
        {
            const cgltf_skin& skin = _pData->skins[s];
            if (skin.joints_count > Max<uint16_t>())
            {
                JUG_CORE_LOG_ERROR("Failed to import glTF: skin {} has too many joints ({} > {}).", s, skin.joints_count, Max<uint16_t>());
                return eGraphicsError::ModelLimitExceeded;
            }

            ModelSkin out = {};
            out.name      = GltfToString_(skin.name);
            out.nodeIndices.reserve(skin.joints_count);
            for (size_t j = 0; j < skin.joints_count; ++j)
            {
                out.nodeIndices.push_back(static_cast<uint32_t>(cgltf_node_index(_pData, skin.joints[j])));
            }

            // inverse bind matrices. 없으면 단위 행렬
            out.invBindMats.reserve(skin.joints_count);
            if (skin.inverse_bind_matrices != nullptr)
            {
                const cgltf_accessor* pAccessor = skin.inverse_bind_matrices;
                if (pAccessor->type != cgltf_type_mat4 || pAccessor->count < skin.joints_count)
                {
                    JUG_CORE_LOG_ERROR("Failed to import glTF: skin {} inverseBindMatrices accessor is invalid.", s);
                    return eGraphicsError::ModelInvalidData;
                }

                Buffer<float> floats = {};
                JUG_RETURN_IF_ERROR(ReadFloats_(pAccessor, floats, "inverseBindMatrices"));
                for (size_t j = 0; j < skin.joints_count; ++j)
                {
                    out.invBindMats.push_back(ConvertMatrix_(floats.GetPtr() + j * 16));
                }
            }
            else
            {
                out.invBindMats.assign(skin.joints_count, Identity<MAT4>());
            }

            if (skin.skeleton != nullptr)
            {
                out.rootNodeIndexOrInvalid = static_cast<uint32_t>(cgltf_node_index(_pData, skin.skeleton));
            }
            _model.skins.push_back(std::move(out));
        }
        return kOK;
    }

    // ===========================================
    //  glTF / Animation
    // ===========================================

    struct SamplerData
    {
        eAnimationInterpolation interpolation = eAnimationInterpolation::Linear;
        size_t                  numKeys       = 0;
        size_t                  numFloats     = 0;   // 값 하나의 float 수
        Buffer<float>           times         = {};
        Buffer<float>           outputs       = {};   // CubicSpline 이면 키마다 [in, value, out]
    };

    [[nodiscard]] size_t GetNumMorphTargets_(
        const cgltf_mesh& _mesh)
    {
        size_t numTargets = _mesh.weights_count;
        for (size_t i = 0; i < _mesh.primitives_count; ++i)
        {
            numTargets = Max(numTargets, _mesh.primitives[i].targets_count);
        }
        return numTargets;
    }

    Error ReadSampler_(
        const cgltf_animation_sampler& _sampler,
        const size_t                   _numFloatsPerValue,
        const size_t                   _animationIndex,
        SamplerData&                   _out)
    {
        switch (_sampler.interpolation)
        {
            case cgltf_interpolation_type_step:
                _out.interpolation = eAnimationInterpolation::Step;
                break;
            case cgltf_interpolation_type_cubic_spline:
                _out.interpolation = eAnimationInterpolation::CubicSpline;
                break;
            default:
                _out.interpolation = eAnimationInterpolation::Linear;
                break;
        }

        _out.numKeys   = _sampler.input->count;
        _out.numFloats = _numFloatsPerValue;

        const size_t numValuesPerKey = _out.interpolation == eAnimationInterpolation::CubicSpline ? 3 : 1;
        const size_t expectedFloats  = _out.numKeys * _numFloatsPerValue * numValuesPerKey;
        if (_sampler.input->type != cgltf_type_scalar || cgltf_accessor_unpack_floats(_sampler.output, nullptr, 0) != expectedFloats)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: animation {} sampler input/output sizes do not match.", _animationIndex);
            return eGraphicsError::ModelInvalidData;
        }

        JUG_RETURN_IF_ERROR(ReadFloats_(_sampler.input, _out.times, "animation input"));
        JUG_RETURN_IF_ERROR(ReadFloats_(_sampler.output, _out.outputs, "animation output"));
        return kOK;
    }

    void GltfFillVec3Curve_(
        const SamplerData&    _data,
        AnimationCurve<VEC3>& _curve)
    {
        _curve.interpolation = _data.interpolation;
        _curve.times.assign(_data.times.Begin(), _data.times.End());

        const float* pOut = _data.outputs.GetPtr();
        if (_data.interpolation == eAnimationInterpolation::CubicSpline)
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                _curve.inTangents.push_back(ConvertPosition_(pOut + (k * 3 + 0) * 3));
                _curve.values.push_back(ConvertPosition_(pOut + (k * 3 + 1) * 3));
                _curve.outTangents.push_back(ConvertPosition_(pOut + (k * 3 + 2) * 3));
            }
        }
        else
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                _curve.values.push_back(ConvertPosition_(pOut + k * 3));
            }
        }
    }

    void FillScaleCurve_(
        const SamplerData&    _data,
        AnimationCurve<VEC3>& _curve)
    {
        _curve.interpolation = _data.interpolation;
        _curve.times.assign(_data.times.Begin(), _data.times.End());

        const float* pOut = _data.outputs.GetPtr();
        if (_data.interpolation == eAnimationInterpolation::CubicSpline)
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                const float* pIn    = pOut + (k * 3 + 0) * 3;
                const float* pValue = pOut + (k * 3 + 1) * 3;
                const float* pOutT  = pOut + (k * 3 + 2) * 3;
                _curve.inTangents.push_back(VEC3 { pIn[0], pIn[1], pIn[2] });
                _curve.values.push_back(VEC3 { pValue[0], pValue[1], pValue[2] });
                _curve.outTangents.push_back(VEC3 { pOutT[0], pOutT[1], pOutT[2] });
            }
        }
        else
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                const float* pValue = pOut + k * 3;
                _curve.values.push_back(VEC3 { pValue[0], pValue[1], pValue[2] });
            }
        }
    }

    void GltfFillQuatCurve_(
        const SamplerData&    _data,
        AnimationCurve<QUAT>& _curve)
    {
        _curve.interpolation = _data.interpolation;
        _curve.times.assign(_data.times.Begin(), _data.times.End());

        const float* pOut = _data.outputs.GetPtr();
        if (_data.interpolation == eAnimationInterpolation::CubicSpline)
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                _curve.inTangents.push_back(ConvertRotation_(pOut + (k * 3 + 0) * 4));
                _curve.values.push_back(ConvertRotation_(pOut + (k * 3 + 1) * 4));
                _curve.outTangents.push_back(ConvertRotation_(pOut + (k * 3 + 2) * 4));
            }
        }
        else
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                _curve.values.push_back(ConvertRotation_(pOut + k * 4));
            }
        }
    }

    // 키당 타깃 수만큼 평탄화
    void FillWeightCurve_(
        const SamplerData&     _data,
        AnimationCurve<float>& _curve)
    {
        _curve.interpolation = _data.interpolation;
        _curve.times.assign(_data.times.Begin(), _data.times.End());

        const float* pOut       = _data.outputs.GetPtr();
        const size_t numTargets = _data.numFloats;
        if (_data.interpolation == eAnimationInterpolation::CubicSpline)
        {
            for (size_t k = 0; k < _data.numKeys; ++k)
            {
                const float* pIn    = pOut + (k * 3 + 0) * numTargets;
                const float* pValue = pOut + (k * 3 + 1) * numTargets;
                const float* pOutT  = pOut + (k * 3 + 2) * numTargets;
                _curve.inTangents.insert(_curve.inTangents.end(), pIn, pIn + numTargets);
                _curve.values.insert(_curve.values.end(), pValue, pValue + numTargets);
                _curve.outTangents.insert(_curve.outTangents.end(), pOutT, pOutT + numTargets);
            }
        }
        else
        {
            _curve.values.assign(pOut, pOut + _data.numKeys * numTargets);
        }
    }

    Error ConvertAnimation_(
        const cgltf_data*        _pData,
        const cgltf_animation&   _animation,
        const size_t             _animationIndex,
        const ModelImportConfig& _config,
        Animation&               _out)
    {
        _out.name = GltfToString_(_animation.name);

        Vector<uint32_t> trackOfNode(_pData->nodes_count, kInvalidIndex);   // 원소는 kInvalidIndex 가능
        for (size_t c = 0; c < _animation.channels_count; ++c)
        {
            const cgltf_animation_channel& channel = _animation.channels[c];
            if (channel.target_node == nullptr || channel.sampler == nullptr)
            {
                continue;
            }

            const cgltf_animation_path_type path = channel.target_path;
            if (path != cgltf_animation_path_type_translation && path != cgltf_animation_path_type_rotation && path != cgltf_animation_path_type_scale && path != cgltf_animation_path_type_weights)
            {
                continue;
            }

            // weights 는 대상 메시의 타깃 수가 필요하다.
            size_t numTargets = 0;
            if (path == cgltf_animation_path_type_weights)
            {
                if (_config.flags.Has(eModelImportOption::SkipMorphTargets))
                {
                    continue;
                }
                if (channel.target_node->mesh == nullptr || _config.flags.Has(eModelImportOption::SkipMeshes))
                {
                    continue;
                }
                numTargets = GetNumMorphTargets_(*channel.target_node->mesh);
                if (numTargets == 0)
                {
                    JUG_CORE_LOG_WARN("glTF animation {}: weights channel targets a mesh without morph targets; skipped.", _animationIndex);
                    continue;
                }
            }

            size_t numFloats = 0;
            switch (path)
            {
                case cgltf_animation_path_type_rotation:
                    numFloats = 4;
                    break;
                case cgltf_animation_path_type_weights:
                    numFloats = numTargets;
                    break;
                default:
                    numFloats = 3;
                    break;
            }

            SamplerData data = {};
            JUG_RETURN_IF_ERROR(ReadSampler_(*channel.sampler, numFloats, _animationIndex, data));
            if (data.numKeys == 0)
            {
                continue;
            }

            const size_t nodeIndex = cgltf_node_index(_pData, channel.target_node);
            if (!IsValid(trackOfNode[nodeIndex]))
            {
                trackOfNode[nodeIndex] = static_cast<uint32_t>(_out.tracks.size());
                AnimationTrack track   = {};
                track.nodeIndexOrInvalid  = static_cast<uint32_t>(nodeIndex);
                _out.tracks.push_back(std::move(track));
            }
            AnimationTrack& track = _out.tracks[trackOfNode[nodeIndex]];

            bool bDuplicated = false;
            switch (path)
            {
                case cgltf_animation_path_type_translation:
                    bDuplicated = !track.translation.times.empty();
                    if (!bDuplicated)
                    {
                        GltfFillVec3Curve_(data, track.translation);
                    }
                    break;
                case cgltf_animation_path_type_rotation:
                    bDuplicated = !track.rotation.times.empty();
                    if (!bDuplicated)
                    {
                        GltfFillQuatCurve_(data, track.rotation);
                    }
                    break;
                case cgltf_animation_path_type_scale:
                    bDuplicated = !track.scale.times.empty();
                    if (!bDuplicated)
                    {
                        FillScaleCurve_(data, track.scale);
                    }
                    break;
                default:
                    bDuplicated = !track.morphWeights.times.empty();
                    if (!bDuplicated)
                    {
                        FillWeightCurve_(data, track.morphWeights);
                    }
                    break;
            }
            if (bDuplicated)
            {
                JUG_CORE_LOG_WARN("glTF animation {}: node {} has duplicated channels for the same path; the later one is ignored.", _animationIndex, nodeIndex);
            }
        }
        return kOK;
    }
}   // namespace

namespace model_detail
{

    Error ImportGLTF(
        Model&                   _model,
        const MemoryView         _mem,
        const FilePath&          _baseDirOrEmpty,
        const ModelImportConfig& _config)
    {
        // 파일 IO(cgltf_parse_file 등)는 사용하지 않는다. (Windows 비ASCII 경로)
        const cgltf_options options     = {};
        cgltf_data*         pRawData    = nullptr;
        const cgltf_result  parseResult = cgltf_parse(&options, _mem.GetPtr(), _mem.GetSize(), &pRawData);
        if (parseResult != cgltf_result_success)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: parse failed ({}).", GltfToString_(parseResult));
            return eGraphicsError::ModelParseFailed;
        }
        const CgltfDataPtr pData(pRawData);

        JUG_RETURN_IF_ERROR(ValidateExtensions_(pData.get()));

        Vector<ByteBuffer> bufferStorage = {};
        JUG_RETURN_IF_ERROR(ResolveBuffers_(pData.get(), bufferStorage, _baseDirOrEmpty));

        const cgltf_result validateResult = cgltf_validate(pData.get());
        if (validateResult != cgltf_result_success)
        {
            JUG_CORE_LOG_ERROR("Failed to import glTF: validation failed ({}).", GltfToString_(validateResult));
            return eGraphicsError::ModelInvalidData;
        }

        const bool bSkipMeshes     = _config.flags.Has(eModelImportOption::SkipMeshes);
        const bool bSkipMaterials  = _config.flags.Has(eModelImportOption::SkipMaterials);
        const bool bSkipSkins      = _config.flags.Has(eModelImportOption::SkipSkins);
        const bool bSkipAnimations = _config.flags.Has(eModelImportOption::SkipAnimations);

        GltfConvertNodes_(pData.get(), _config, _model);

        if (!bSkipMeshes)
        {
            JUG_RETURN_IF_ERROR(GltfConvertMeshes_(pData.get(), _config, _model));
        }
        if (!bSkipMeshes && !bSkipSkins)
        {
            JUG_RETURN_IF_ERROR(GltfConvertSkins_(pData.get(), _model));
        }

        if (!bSkipMaterials)
        {
            Vector<uint32_t> textureOfImage = {};   // 원소는 kInvalidIndex 가능
            LoadImages_(pData.get(), _baseDirOrEmpty, _config, _model, textureOfImage);

            _model.materials.reserve(pData->materials_count);
            for (size_t i = 0; i < pData->materials_count; ++i)
            {
                _model.materials.push_back(GltfConvertMaterial_(pData.get(), pData->materials[i], textureOfImage));
            }
        }

        if (!bSkipAnimations)
        {
            _model.animations.reserve(pData->animations_count);
            for (size_t i = 0; i < pData->animations_count; ++i)
            {
                Animation animation = {};
                JUG_RETURN_IF_ERROR(ConvertAnimation_(pData.get(), pData->animations[i], i, _config, animation));
                _model.animations.push_back(std::move(animation));
            }
        }
        return kOK;
    }

}   // namespace model_detail

}   // namespace jug
