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
        case eGraphicsError::ImageLoadFailed:
            return "Image loading failed";
        case eGraphicsError::ImageSaveFailed:
            return "Image saving failed";
        case eGraphicsError::ImageReadFailed:
            return "Image reading failed";
        default:
            JUG_ASSERT(false, "Unrecognized error code");
            return "Unrecognized error code";
    }
}

}   // namespace jug