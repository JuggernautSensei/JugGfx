#include "pch.h"
#include "Image.h"

#include "Error.h"
#include "Texture.h"

namespace jug
{

Result<Image> Image::Load(
    const MemoryView _mem)
{
    Image image = {};
    JUG_RETURN_IF_ERROR(image.Load_(_mem));
    return std::move(image);
}

Result<Image> Image::LoadFromFile(
    const FilePath& _path)
{
    Image image = {};
    JUG_RETURN_IF_ERROR(image.LoadFromFile_(_path));
    return std::move(image);
}

const OIIO::ImageSpec& Image::GetSpec() const
{
    return m_pImage->spec();
}

Result<Buffer<std::byte>> Image::Read(
    const uint32_t _layer,
    const uint32_t _mip) const
{
    JUG_ASSERT(_layer < m_numLayers, "Layer index out of bounds.\n");
    JUG_ASSERT(_mip < m_numMips, "Mip index out of bounds.\n");

    const OIIO::ImageSpec& spec = m_pImage->spec();
    const auto [w, h, d]        = CalcTextureSize(spec.width, spec.height, spec.depth, _mip);
    const uint32_t numPixels    = w * h * d;
    const uint32_t byteWidth    = spec.nchannels * numPixels * static_cast<uint32_t>(spec.format.size());

    Buffer<std::byte> buf(byteWidth);
    if (!m_pImage->read_image(static_cast<int>(_layer), static_cast<int>(_mip), 0, spec.nchannels, spec.format, buf.GetPtr()))
    {
        JUG_CORE_LOG_ERROR("Failed to read image layer {} mip {}: {}", _layer, _mip, OIIO::geterror());
        return eGraphicsError::ImageReadFailed;
    }
    return buf;
}

uint32_t Image::GetNumLayers() const
{
    return m_numLayers;
}

uint32_t Image::GetNumMips() const
{
    return m_numMips;
}

Error Image::Load_(
    const MemoryView _mem)
{
    OIIO::Filesystem::IOMemReader reader { _mem.GetPtr(), _mem.GetSize() };
    m_pImage = OIIO::ImageInput::open("", nullptr, &reader);
    if (!m_pImage)
    {
        JUG_CORE_LOG_ERROR("Failed to load image from memory: {}", OIIO::geterror());
        return eGraphicsError::ImageLoadFailed;
    }
    PostLoad_();
    return kOK;
}

Error Image::LoadFromFile_(
    const FilePath& _path)
{
    m_pImage = OIIO::ImageInput::open(_path.native());
    if (!m_pImage)
    {
        JUG_CORE_LOG_ERROR("Failed to load image from file '{}': {}", _path.string(), OIIO::geterror());
        return eGraphicsError::ImageLoadFailed;
    }
    PostLoad_();
    return kOK;
}

void Image::PostLoad_()
{
    while (m_pImage->seek_subimage(static_cast<int>(m_numLayers), 0))
    {
        m_numLayers++;
    }

    while (m_pImage->seek_subimage(0, static_cast<int>(m_numMips)))
    {
        m_numMips++;
    }

    m_pImage->seek_subimage(0, 0);
}

}   // namespace jug