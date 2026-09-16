#include "pch.h"
#include "Image.h"

#include <JugX/CoreLogger.h>
#include <JugX/Error.h>
#include <JugX/Vector3I.h>

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

Result<Memory> Image::Read(
    const int _layer,
    const int _mip) const
{
    JUG_ASSERT(0 <= _layer && _layer < m_numLayers, "Layer index out of bounds.\n");
    JUG_ASSERT(0 <= _mip && _mip < m_numMips, "Mip index out of bounds.\n");

    const OIIO::ImageSpec& spec        = m_pImage->spec();
    const VECTOR3I         subSize     = CalcTextureSize(spec.width, spec.height, spec.depth, _mip);
    const int              numPixels   = subSize.x * subSize.y * subSize.z;
    const int              numChannels = spec.nchannels == 3 ? 4 : spec.nchannels;
    const size_t           byteWidth   = static_cast<size_t>(numPixels) * static_cast<size_t>(numChannels) * spec.format.size();

    Memory mem = AllocMemory(byteWidth);
    if (!m_pImage->read_image(_layer, _mip, 0, numChannels, spec.format, mem.GetPtr()))
    {
        JUG_CORE_LOG_ERROR("Failed to read image layer {} mip {}: {}", _layer, _mip, OIIO::geterror());
        return eGraphicsError::ImageReadFailed;
    }
    return mem;
}

int Image::GetNumLayers() const
{
    return m_numLayers;
}

int Image::GetNumMips() const
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
    while (m_pImage->seek_subimage(m_numLayers, 0))
    {
        m_numLayers++;
    }

    while (m_pImage->seek_subimage(0, m_numMips))
    {
        m_numMips++;
    }

    m_pImage->seek_subimage(0, 0);
}

}   // namespace jug