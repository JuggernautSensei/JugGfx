#include "pch.h"
#include "Error.h"

namespace jug
{

StringView GraphicsCategory::GetName() const noexcept
{
    return "Graphics";
}

String GraphicsCategory::MakeMessage(
    const int _err) const
{
    switch (static_cast<eGraphicsError>(_err))
    {
        case eGraphicsError::None:
            return "No error";
        case eGraphicsError::ShaderCompileFailed:
            return "Shader compilation failed";
        case eGraphicsError::ImageParseFailed:
            return "Image parsing failed";
        case eGraphicsError::ImageUnsupportedFormat:
            return "Image format is not supported";
        case eGraphicsError::ImageUnsupportedFeature:
            return "Image uses an unsupported feature";
        case eGraphicsError::ImageInvalidData:
            return "Image data is invalid";
        case eGraphicsError::ImageDecodeFailed:
            return "Image decoding failed";
        case eGraphicsError::ImageEncodeFailed:
            return "Image encoding failed";
        case eGraphicsError::ImageProcessFailed:
            return "Image processing failed";
        case eGraphicsError::ModelUnsupportedFormat:
            return "Model format is not supported";
        case eGraphicsError::ModelParseFailed:
            return "Model parsing failed";
        case eGraphicsError::ModelInvalidData:
            return "Model data is invalid";
        case eGraphicsError::ModelUnsupportedFeature:
            return "Model uses an unsupported feature";
        case eGraphicsError::ModelMissingResource:
            return "Model resource is missing";
        case eGraphicsError::ModelLimitExceeded:
            return "Model exceeds engine limits";
        default:
            JUG_ASSERT(false, "Unrecognized error code");
            return "Unrecognized error code";
    }
}

}   // namespace jug