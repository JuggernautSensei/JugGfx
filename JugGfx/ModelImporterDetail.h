#pragma once
#include "ModelImporter.h"

namespace jug::model_importer_detail
{

// 포맷별 import. _baseDirOrEmpty 가 비어 있으면 외부 파일 참조를 풀 수 없음
[[nodiscard]] Result<Model> ImportGltf(MemoryView _mem, const ModelImportConfig& _config, const FilePath& _baseDirOrEmpty);
[[nodiscard]] Result<Model> ImportFbx(MemoryView _mem, const ModelImportConfig& _config, const FilePath& _baseDirOrEmpty);
[[nodiscard]] Result<Model> ImportObj(MemoryView _mem, const ModelImportConfig& _config, const FilePath& _baseDirOrEmpty);

// 정점 하나의 본 영향에서 가중치가 큰 kNumMaxBoneInfluences 개를 골라 합이 1 이 되게 정규화. 빈 칸은 (0, 0.f). 0 이 아닌 가중치를 버렸으면 true
[[nodiscard]] bool SelectBoneInfluences(Span<const uint16_t> _indices, Span<const float> _weights, ARRAY<uint16_t, kNumMaxBoneInfluences>& _outIndices, ARRAY<float, kNumMaxBoneInfluences>& _outWeights);

// 길이 0 이면 단위 쿼터니언
[[nodiscard]] QUAT NormalizeRotation(QUAT _q);

// 스킨 관절 노드들의 최상위 본. _skeletonOrInvalid 가 있으면 그것, 없으면 모든 관절의 공통 조상인 관절, 그것도 없으면 첫 관절의 최상위 조상
[[nodiscard]] uint32_t FindSkinRoot(Span<const ModelNode> _nodes, Span<const uint32_t> _jointNodeIndices, uint32_t _skeletonOrInvalid);

// 디코드한 이미지를 _model.textures 에 추가하고 인덱스를 반환. 실패한 결과면 경고를 남기고 kInvalidIndex
[[nodiscard]] uint32_t AddTextureOrInvalid(Model& _model, Result<Image> _image, StringView _nameForLog);

// 구조 불변식 검사. 위반은 importer 버그이므로 JUG_ASSERT
void ValidateModel(const Model& _model);

}   // namespace jug::model_importer_detail
