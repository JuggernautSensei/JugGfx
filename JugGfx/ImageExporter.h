#pragma once
#include "Image.h"

namespace jug
{

struct ImageExportConfig
{
    eImageFileFormat format  = eImageFileFormat::PNG;
    uint32_t         quality = 90;   // JPEG 전용 1~100, 그 외 무시
    uint32_t         layer   = 0;    // layer, mip: 단일 이미지 포맷(PNG/JPEG/BMP/TGA/HDR/EXR) 출력 전용
    uint32_t         mip     = 0;
};

[[nodiscard]] Result<ByteBuffer> ExportImage(const Image& _image, const ImageExportConfig& _config);
[[nodiscard]] Error              ExportImageToFile(const FilePath& _path, const Image& _image, const ImageExportConfig& _config);

}   // namespace jug
