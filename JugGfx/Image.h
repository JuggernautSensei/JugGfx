#pragma once
#include <JugX/Memory.h>
#include <JugX/MemoryView.h>
#include <JugX/Result.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>

namespace jug
{

class Image
{
    JUG_CLASS(Image, NO_COPY, DEFAULT_MOVE)

public:
    ~Image() = default;
    [[nodiscard]] static Result<Image> Load(MemoryView _mem);
    [[nodiscard]] static Result<Image> LoadFromFile(const FilePath& _path);

    [[nodiscard]] const OIIO::ImageSpec& GetSpec() const;
    [[nodiscard]] Result<Memory>         Read(int _layer, int _mip) const;
    [[nodiscard]] int                    GetNumLayers() const;
    [[nodiscard]] int                    GetNumMips() const;

private:
    Image() = default;
    Error Load_(MemoryView _mem);
    Error LoadFromFile_(const FilePath& _path);
    void  PostLoad_();

    std::unique_ptr<OIIO::ImageInput> m_pImage    = nullptr;
    int                               m_numLayers = 0;
    int                               m_numMips   = 0;
};

}   // namespace jug