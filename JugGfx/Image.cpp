#include "pch.h"
#include "Image.h"

#include "Error.h"
#include "ImageDetail.h"
#include "Texture.h"

namespace jug
{

SubImage Image::GetSubImage(
    const uint32_t _layer,
    const uint32_t _mip) const
{
    JUG_ASSERT(_layer < desc.numLayers, "Layer index out of bounds.\n");
    JUG_ASSERT(_mip < desc.numMips, "Mip index out of bounds.\n");

    const VEC3U size = CalcTextureSize(desc.width, desc.height, desc.depth, _mip);

    SubImage sub   = {};
    sub.width      = size.x;
    sub.height     = size.y;
    sub.depth      = size.z;
    sub.rowPitch   = image_detail::CalcRowPitch(*this, size.x);
    sub.slicePitch = image_detail::CalcSlicePitch(*this, size.x, size.y);
    sub.pixels     = { pixels.GetPtr() + image_detail::CalcOffset(*this, _layer, _mip), static_cast<size_t>(sub.slicePitch) * size.z };
    return sub;
}

}   // namespace jug
