#include "pch.h"
#include "ResourceUtils.h"

#include "Graphics.h"
#include "Shader.h"

namespace jug
{

namespace
{
    [[nodiscard]] Result<TextureHandle> LoadTexture_(
        const Image&                _image,
        const Flags<eTextureOption> _texFlags)
    {
        Graphics&        gfx  = Graphics::GetSingleton();
        const ImageDesc& desc = _image.desc;
        JUG_ASSERT(desc.gpuFormat.HasValue(), "A gpu compatible image must have a gpu format.\n");
        const eTextureFormat format = *desc.gpuFormat;
        if (desc.depth > 1)
        {
            return gfx.CreateTexture3D(desc.width, desc.height, desc.depth, format, _texFlags, _image.pixels);
        }
        else if (desc.bCubemap)
        {
            const uint32_t numCubes = desc.numLayers / 6;
            return gfx.CreateTextureCube(desc.width, desc.height, format, numCubes, _texFlags, _image.pixels);
        }
        else
        {
            return gfx.CreateTexture2D(desc.width, desc.height, format, desc.numLayers, eMSAA::None, _texFlags, _image.pixels);
        }
    }
}   // namespace

Result<ShaderHandle> LoadShader(
    const StringView           _source,
    const ShaderCompileConfig& _config)
{
    const Result<ShaderBytecode> shader = CompileShader(_source, _config);
    JUG_RETURN_IF_ERROR(shader);

    Graphics& gfx = Graphics::GetSingleton();
    return gfx.CreateShader(shader->GetBytecode());
}

Result<ShaderHandle> LoadShaderFromFile(
    const FilePath& _path)
{
    const Result<ShaderBytecode> shader = CompileShaderFromFile(_path, {});
    JUG_RETURN_IF_ERROR(shader);

    Graphics& gfx = Graphics::GetSingleton();
    return gfx.CreateShader(shader->GetBytecode());
}

Result<TextureHandle> LoadTexture(
    const MemoryView            _mem,
    Flags<eImageImportOption>   _loadFlags,
    const Flags<eTextureOption> _texFlags)
{
    _loadFlags |= eImageImportOption::GpuCompatible;
    Result<Image> image = ImportImage(_mem, _loadFlags);
    JUG_RETURN_IF_ERROR(image);
    return LoadTexture_(*image, _texFlags);
}

Result<TextureHandle> LoadTextureFromFile(
    const FilePath&             _path,
    Flags<eImageImportOption>   _loadFlags,
    const Flags<eTextureOption> _texFlags)
{
    _loadFlags |= eImageImportOption::GpuCompatible;
    Result<Image> image = ImportImageFromFile(_path, _loadFlags);
    JUG_RETURN_IF_ERROR(image);
    return LoadTexture_(*image, _texFlags);
}

}   // namespace jug