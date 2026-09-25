#include "pch.h"
#include "ImGuiImage.h"

namespace jug
{

namespace
{
    constexpr uint32_t kTexIdMipShift   = 32;
    constexpr uint32_t kTexIdFaceShift  = 37;
    constexpr uint32_t kTexIdLayerShift = 40;
    constexpr uint64_t kTexIdMipMask    = (1ull << 5) - 1;
    constexpr uint64_t kTexIdFaceMask   = (1ull << 3) - 1;
    constexpr uint64_t kTexIdLayerMask  = (1ull << 23) - 1;
    constexpr uint64_t kTexIdValidBit   = 1ull << 63;
}   // namespace

ImGuiTextureRef::ImGuiTextureRef(
    const TextureHandle _texh,
    const uint32_t      _mip,
    const uint32_t      _layer,
    const eCubeFace     _face)
    : texh(_texh)
    , mip(_mip)
    , layer(_layer)
    , face(_face)
{
}

ImGuiTextureRef::ImGuiTextureRef(
    const ImTextureID _id)
    : texh(TextureHandle { static_cast<uint32_t>(_id) })
    , mip((_id >> kTexIdMipShift) & kTexIdMipMask)
    , layer(static_cast<uint32_t>((_id >> kTexIdLayerShift) & kTexIdLayerMask))
    , face(static_cast<eCubeFace>((_id >> kTexIdFaceShift) & kTexIdFaceMask))
{
}

ImTextureID ImGuiTextureRef::ToImTextureID() const
{
    static_assert(sizeof(ImTextureID) >= sizeof(uint64_t), "ImGuiRenderer requires a 64-bit ImTextureID.");
    JUG_ASSERT(texh, "ToImTextureID requires a valid texture handle.");

    JUG_ASSERT(mip <= kTexIdMipMask, "ImGuiTextureRef::mip is out of the packable range.");
    JUG_ASSERT(layer <= kTexIdLayerMask, "ImGuiTextureRef::layer is out of the packable range.");

    ImTextureID id = kTexIdValidBit;
    id |= static_cast<ImTextureID>(texh.GetValue());
    id |= (mip & kTexIdMipMask) << kTexIdMipShift;
    id |= (static_cast<ImTextureID>(face) & kTexIdFaceMask) << kTexIdFaceShift;
    id |= (static_cast<ImTextureID>(layer) & kTexIdLayerMask) << kTexIdLayerShift;
    return id;
}

}   // namespace jug

namespace ImGui
{

// ===========================================
//  Widget
// ===========================================

void Image(
    const jug::ImGuiTextureRef _texture,
    const ImVec2               _size,
    const ImVec2               _uv0,
    const ImVec2               _uv1)
{
    Image(_texture.ToImTextureID(), _size, _uv0, _uv1);
}

void ImageWithBg(
    const jug::ImGuiTextureRef _texture,
    const ImVec2               _size,
    const ImVec2               _uv0,
    const ImVec2               _uv1,
    const ImVec4               _bgCol,
    const ImVec4               _tintCol)
{
    ImageWithBg(_texture.ToImTextureID(), _size, _uv0, _uv1, _bgCol, _tintCol);
}

bool ImageButton(
    const char*                _str,
    const jug::ImGuiTextureRef _texture,
    const ImVec2               _size,
    const ImVec2               _uv0,
    const ImVec2               _uv1,
    const ImVec4               _bgCol,
    const ImVec4               _tintCol)
{
    return ImageButton(_str, _texture.ToImTextureID(), _size, _uv0, _uv1, _bgCol, _tintCol);
}

void AddImage(
    ImDrawList*                _pDrawList,
    const jug::ImGuiTextureRef _texture,
    const ImVec2               _pMin,
    const ImVec2               _pMax,
    const ImVec2               _uvMin,
    const ImVec2               _uvMax,
    const ImU32                _col)
{
    _pDrawList->AddImage(_texture.ToImTextureID(), _pMin, _pMax, _uvMin, _uvMax, _col);
}

void AddImageQuad(
    ImDrawList*                _pDrawList,
    const jug::ImGuiTextureRef _texture,
    const ImVec2               _p1,
    const ImVec2               _p2,
    const ImVec2               _p3,
    const ImVec2               _p4,
    const ImVec2               _uv1,
    const ImVec2               _uv2,
    const ImVec2               _uv3,
    const ImVec2               _uv4,
    const ImU32                _col)
{
    _pDrawList->AddImageQuad(_texture.ToImTextureID(), _p1, _p2, _p3, _p4, _uv1, _uv2, _uv3, _uv4, _col);
}

void AddImageRounded(
    ImDrawList*                _pDrawList,
    const jug::ImGuiTextureRef _texture,
    const ImVec2               _pMin,
    const ImVec2               _pMax,
    const ImVec2               _uvMin,
    const ImVec2               _uvMax,
    const ImU32                _col,
    const float                _rounding,
    const ImDrawFlags          _flags)
{
    _pDrawList->AddImageRounded(_texture.ToImTextureID(), _pMin, _pMax, _uvMin, _uvMax, _col, _rounding, _flags);
}

}   // namespace ImGui
