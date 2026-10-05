#pragma once
#include "Image.h"

namespace jug
{

enum class eImageCompression
{
    // 원본 포맷을 최대한 유지.
    // DDS/KTX/KTX2 출력: 원본 포맷 그대로 기록 (압축된 픽셀이면 압축 유지).
    // 그 외 포맷 출력: 압축 포맷을 담을 수 없으므로 압축 해제됨.
    None = 0,

    // DDS/KTX/KTX2
    BC1,
    BC3,
    BC4,
    BC5,
    BC6H,
    BC7,

    // KTX2 전용
    ETC1S,
    UASTC,
};

struct ImageExportConfig
{
    eImageFileFormat  format      = eImageFileFormat::PNG;
    eImageCompression compression = eImageCompression::None;   // format 이 DDS/KTX/KTX2 일 때만 유효. 그 외 포맷은 무시됨.
    uint32_t          layer       = 0;                         // 단일 이미지 포맷 출력 전용
    uint32_t          mip         = 0;                         // 단일 이미지 포맷 출력 전용
};

[[nodiscard]] Result<ByteBuffer> ExportImage(MemoryView _pixels, const ImageDesc& _desc, const ImageExportConfig& _config);
[[nodiscard]] Result<ByteBuffer> ExportImage(const Image& _image, const ImageExportConfig& _config);
[[nodiscard]] Error              ExportImageToFile(const FilePath& _path, MemoryView _pixels, const ImageDesc& _desc, const ImageExportConfig& _config);
[[nodiscard]] Error              ExportImageToFile(const FilePath& _path, const Image& _image, const ImageExportConfig& _config);

}   // namespace jug
