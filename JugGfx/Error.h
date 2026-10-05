#pragma once

namespace jug
{

enum class eGraphicsError
{
    None,

    // shader
    ShaderCompileFailed,

    // image
    ImageParseFailed,          // 파일이 깨졌거나 헤더/데이터가 잘림
    ImageUnsupportedFormat,    // 지원하지 않는 픽셀/파일 포맷 (변환, 인코딩 불가 포함)
    ImageUnsupportedFeature,   // 지원하지 않는 기능 (deep image, GPU 비호환, Basis 비KTX2 등)
    ImageInvalidData,          // 내용이 규격과 모순 (레이어/밉 불일치, 크기, 오프셋 오류)
    ImageDecodeFailed,         // 픽셀 디코딩 실패 (OIIO read, KTX2 transcode)
    ImageEncodeFailed,         // 인코딩/쓰기 실패 (KTX 생성, Basis 압축, 파일 쓰기)
    ImageProcessFailed,        // 이미지 후처리 실패 (알파 프리멀티플라이, 밉 생성, 압축 해제)

    // model
    ModelUnsupportedFormat,    // 확장자/포맷 판별 불가
    ModelParseFailed,          // 파일 파싱 실패
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