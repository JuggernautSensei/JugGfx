#pragma once
#include "Image.h"

namespace jug
{

enum class eImageImportOption
{
    None               = 0,
    Decompress         = 1 << 0,   // 압축 포맷이면 비압축 포맷으로 해제
    DropMips           = 1 << 1,   // mipmap 제거. GenerateMips 와 동시 사용 불가.
    GenerateMips       = 1 << 2,   // mipmap 생성. DropMips 와 동시 사용 불가.
    PremultipliedAlpha = 1 << 3,   // alpha 채널이 있으면 premultiplied alpha 로 보장.
    GpuCompatible      = 1 << 4,   // GPU 포맷이 없는 채널 구성/타입은 GPU 포맷이 있는 구성으로 조절.
};

[[nodiscard]] Result<Image> ImportImage(MemoryView _mem, Flags<eImageImportOption> _flags);
[[nodiscard]] Result<Image> ImportImageFromFile(const FilePath& _path, Flags<eImageImportOption> _flags);

}   // namespace jug
