#pragma once
#include "Image.h"
#include "ImageImporter.h"

namespace jug
{

constexpr uint32_t kNumMaxBoneInfluences = 4;

// ===========================================
//  Mesh
// ===========================================

struct ModelMorphTarget
{
    Vector<VEC3> positionDeltas = {};
    Vector<VEC3> normalDeltas   = {};
    Vector<VEC3> tangentDeltas  = {};
};

struct ModelMesh
{
    IndexedFlags<eVertexAttribute>                       attribFlags            = kNoneFlags;
    Vector<VEC3>                                         positions              = {};
    Vector<VEC3>                                         normals                = {};
    Vector<VEC4>                                         tangents               = {};   // .w: bitangent sign
    ARRAY<Vector<VEC4>, kNumMaxVertexColorAttributes>    colors                 = {};
    ARRAY<Vector<VEC2>, kNumMaxVertexTexCoordAttributes> texCoords              = {};
    Vector<ARRAY<uint16_t, kNumMaxBoneInfluences>>       boneIndices            = {};
    Vector<ARRAY<float, kNumMaxBoneInfluences>>          boneWeights            = {};
    Vector<ModelMorphTarget>                             morphTargets           = {};
    Vector<uint32_t>                                     indices                = {};
    uint32_t                                             materialIndexOrInvalid = kInvalidIndex;
    AABB                                                 bounds                 = Zero<AABB>();
};

struct ModelMeshGroup
{
    String            name         = {};
    Vector<ModelMesh> meshes       = {};
    Vector<float>     morphWeights = {};
};

// ===========================================
//  Texture
// ===========================================

enum class eTextureChannel : uint32_t
{
    None = 0,
    R    = 1 << 0,
    G    = 1 << 1,
    B    = 1 << 2,
    A    = 1 << 3,
    RGBA = R | G | B | A
};

struct ModelTextureDesc
{
    uint32_t               textureIndexOrInvalid = kInvalidIndex;
    uint32_t               texCoordSet           = 0;                                  // 사용하는 텍스처 좌표 세트 (0~3)
    Flags<eTextureChannel> channelFlags          = eTextureChannel::RGBA;              // 사용하는 채널
    Flags<eSampler>        samplerFlags          = eSampler::Filter_MinMagMipLinear;   // 샘플러 옵션. 기본값은 선형 보간
};

// 표준 외 임의의 텍스처
struct ModelExtraTexture
{
    String           semantic = {};   // 이름 키
    ModelTextureDesc desc     = {};   // 텍스처 정보
};

// ===========================================
//  Material
// ===========================================

enum class eModelTexture
{
    Albedo,
    Normal,
    Metallic,
    Roughness,
    AmbientOcclusion,
    Emissive,
    Opacity
};

enum class eModelAlphaMode
{
    Opaque,
    AlphaCutOff,   // alphaCutOffThreshold 미만 discard
    Blend
};

struct ModelMaterial
{
    String                                      name          = {};
    ENUM_ARRAY<eModelTexture, ModelTextureDesc> textures      = {};
    Vector<ModelExtraTexture>                   extraTextures = {};

    VEC4  baseColor         = One<VEC4>();
    float metallic          = 0.f;
    float roughness         = 1.f;
    float normalScale       = 1.f;
    float occlusionStrength = 1.f;
    VEC3  emissive          = Zero<VEC3>();

    eModelAlphaMode alphaMode            = eModelAlphaMode::Opaque;
    float           alphaCutOffThreshold = 0.5f;
    bool            bDoubleSided         = false;
};

// ===========================================
//  Skinning
// ===========================================

struct ModelSkin
{
    String           name                   = {};
    uint32_t         rootNodeIndexOrInvalid = kInvalidIndex;   // 스킨 본 계층의 최상위 본
    Vector<uint32_t> nodeIndices            = {};
    Vector<MAT4>     invBindMats            = {};
};

// ===========================================
//  Animation
// ===========================================

enum class eAnimationInterpolation
{
    Step,
    Linear,
    CubicSpline
};

template<typename T>
struct AnimationCurve
{
    eAnimationInterpolation interpolation = eAnimationInterpolation::Linear;
    Vector<float>           times         = {};
    Vector<T>               values        = {};
    Vector<T>               inTangents    = {};   // CubicSpline 일 때만 사용
    Vector<T>               outTangents   = {};   // CubicSpline 일 때만 사용
};

struct AnimationTrack
{
    uint32_t              nodeIndexOrInvalid = kInvalidIndex;
    AnimationCurve<VEC3>  translation        = {};
    AnimationCurve<QUAT>  rotation           = {};
    AnimationCurve<VEC3>  scale              = {};
    AnimationCurve<float> morphWeights       = {};
};

struct Animation
{
    String                 name     = {};
    float                  duration = 0.f;   // 모든 커브 times 의 최댓값
    Vector<AnimationTrack> tracks   = {};
};

// ===========================================
//  Model
// ===========================================

struct ModelNode
{
    String    name                    = {};
    TRANSFORM local                   = Identity<TRANSFORM>();
    uint32_t  parentIndexOrInvalid    = kInvalidIndex;   // 없으면 루트
    uint32_t  meshGroupIndexOrInvalid = kInvalidIndex;
    uint32_t  skinIndexOrInvalid      = kInvalidIndex;
};

struct Model
{
    Vector<ModelNode>      nodes      = {};
    Vector<ModelSkin>      skins      = {};
    Vector<ModelMeshGroup> meshGroups = {};
    Vector<ModelMaterial>  materials  = {};
    Vector<Image>          textures   = {};
    Vector<Animation>      animations = {};
};

}   // namespace jug
