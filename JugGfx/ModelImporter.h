#pragma once
#include "Model.h"
#include "ImageImporter.h"

namespace jug
{

enum class eModelFileFormat
{
    Obj,
    Fbx,
    Gltf,
    Glb
};

enum class eModelImportOption
{
    None             = 0,
    SkipMeshes       = 1 << 0,   // 메시/스킨/머티리얼/텍스처 제외. 노드와 애니메이션만 로드
    SkipMaterials    = 1 << 1,   // 머티리얼/텍스처 제외. 메시의 materialIndexOrInvalid 은 kInvalidIndex
    SkipTextures     = 1 << 2,   // 텍스처 이미지 로드 안 함. 머티리얼의 텍스처 슬롯의 textureIndexOrInvalid 은 kInvalidIndex
    SkipSkins        = 1 << 3,   // 스킨과 정점 본 가중치 제외
    SkipMorphTargets = 1 << 4,   // 모프 타깃/가중치 제외
    SkipAnimations   = 1 << 5,   // 애니메이션 제외
    GenerateNormals  = 1 << 6,   // 법선이 없는 메시에만 생성
    GenerateTangents = 1 << 7,   // 탄젠트가 없는 메시에만 생성. 법선 + texCoord0 필요, 없으면 경고 후 건너뜀
};

struct ModelImportConfig
{
    Flags<eModelImportOption>  flags      = kNoneFlags;
    Flags<eImageImportOption>  imageFlags = kNoneFlags;   // 텍스처 이미지 import 옵션
    float                      unitScale  = 1.f;          // 미터 단위 정규화 후 추가 배율
    float                      sampleRate = 30.f;         // FBX 애니메이션 bake 샘플링 레이트 (Hz). glTF 는 원본 키 유지
    Optional<eModelFileFormat> formatHint = kNullopt;     // 파일 내용을 보고 판별할 수 없는 경우 확장자 힌트를 사용. 지정하지 않으면 내용으로만 판별.
};

[[nodiscard]] Result<Model> ImportModel(MemoryView _mem, const ModelImportConfig& _config);
[[nodiscard]] Result<Model> ImportModelFromFile(const FilePath& _path, const ModelImportConfig& _config);   // 포맷은 내용으로 판별하고 확장자는 힌트로 사용 (.obj .fbx .gltf .glb, 대소문자 무시)

}   // namespace jug
