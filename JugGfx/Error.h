#pragma once

namespace jug
{

enum class eGraphicsError
{
    None,

    // shader
    ShaderCompileFailed,

    // image
    ImageParseFailed,          // 파일이 깨졌거나 잘림
    ImageUnsupportedFormat,
    ImageUnsupportedFeature,   // GPU 한계 초과 포함
    ImageInvalidData,          // 헤더 값끼리 모순
    ImageDecodeFailed,
    ImageEncodeFailed,         // 파일 쓰기 실패 포함

    // model
    ModelUnsupportedFormat,    // 확장자/포맷 판별 불가
    ModelParseFailed,
    ModelInvalidData,          // 내용이 규격과 모순 (범위 초과 accessor, 잘못된 인덱스 등)
    ModelUnsupportedFeature,   // 미지원 필수 확장 (Draco, meshopt 등)
    ModelMissingResource,      // 필수 외부 리소스 누락 (.bin 버퍼 등)
    ModelLimitExceeded,        // 엔진 한계 초과 (joint 수, 정점 수)
};

class GraphicsCategory : public IErrorCategory
{
public:
    [[nodiscard]] StringView GetName() const noexcept override;
    [[nodiscard]] String     MakeMessage(int _err) const override;
};

JUG_DEFINE_ERROR_ENUM(eGraphicsError, GraphicsCategory);

}   // namespace jug