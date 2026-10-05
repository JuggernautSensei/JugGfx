#pragma once
#include "Image.h"
#include "ImageImporter.h"
#include "Shader.h"

namespace jug
{

// ===========================================
//  Shader
// ===========================================

[[nodiscard]] Result<ShaderHandle> LoadShader(StringView _source, const ShaderCompileConfig& _config);
[[nodiscard]] Result<ShaderHandle> LoadShaderFromFile(const FilePath& _path);

// ===========================================
//  Texture
// ===========================================

[[nodiscard]] Result<TextureHandle> LoadTexture(MemoryView _mem, Flags<eImageImportOption> _loadFlags, Flags<eTextureOption> _texFlags);
[[nodiscard]] Result<TextureHandle> LoadTextureFromFile(const FilePath& _path, Flags<eImageImportOption> _loadFlags, Flags<eTextureOption> _texFlags);

}   // namespace jug