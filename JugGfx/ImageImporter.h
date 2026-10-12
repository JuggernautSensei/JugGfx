#pragma once
#include "Image.h"

namespace jug
{

enum class eImageImportOption : uint32_t
{
    None = 0,

    SRGB          = 1 << 0,   // 색공간 정보 없는 8비트 3/4채널을 sRGB 로 해석. DDS/EXR/HDR 은 무시
    Decompress    = 1 << 1,   // BC -> 비압축
    GpuCompatible = 1 << 2,   // CreateTexture 에 바로 쓸 수 있게: 3채널은 4채널로 패딩(알파 최대), GPU 한계 초과는 거부
};

[[nodiscard]] Result<Image> ImportImage(MemoryView _mem, Flags<eImageImportOption> _flags);
[[nodiscard]] Result<Image> ImportImageFromFile(const FilePath& _path, Flags<eImageImportOption> _flags);

}   // namespace jug
