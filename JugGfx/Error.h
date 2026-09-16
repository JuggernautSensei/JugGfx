#pragma once
#include <JugX/Error.h>

namespace jug
{

enum class eGraphicsError
{
    None,

    // shader
    ShaderCompileFailed,

    // image
    ImageLoadFailed,
    ImageSaveFailed,
    ImageReadFailed
};

class GraphicsCategory : public IErrorCategory
{
public:
    [[nodiscard]] StringView GetName() const noexcept override;
    [[nodiscard]] String     MakeMessage(int _err) const override;
};

JUG_DEFINE_ERROR_ENUM(eGraphicsError, GraphicsCategory);

}   // namespace jug