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

uint64_t ToTextureID(
    const TextureHandle _texh)
{
    ImGuiTexture texture = {};
    texture.texh         = _texh;
    return ToTextureID(texture);
}

uint64_t ToTextureID(
    ImGuiTexture _texture)
{
    static_assert(sizeof(ImTextureID) >= sizeof(uint64_t), "ImGuiRenderer requires a 64-bit ImTextureID.");
    JUG_ASSERT(_texture.texh, "ToTextureID requires a valid texture handle.");

    const uint64_t mip = _texture.mip;
    JUG_ASSERT(mip <= kTexIdMipMask, "ImGuiTexture::mip is out of the packable range.");
    JUG_ASSERT(_texture.layer <= kTexIdLayerMask, "ImGuiTexture::layer is out of the packable range.");

    uint64_t id = kTexIdValidBit;
    id |= static_cast<uint64_t>(_texture.texh.GetValue());
    id |= (mip & kTexIdMipMask) << kTexIdMipShift;
    id |= (static_cast<uint64_t>(_texture.face) & kTexIdFaceMask) << kTexIdFaceShift;
    id |= (static_cast<uint64_t>(_texture.layer) & kTexIdLayerMask) << kTexIdLayerShift;
    return id;
}

ImGuiTexture FromTextureID(
    const uint64_t _id)
{
    JUG_ASSERT(_id & kTexIdValidBit, "FromTextureID requires an id made by ToTextureID.");

    ImGuiTexture texture = {};
    texture.texh         = TextureHandle { static_cast<uint32_t>(_id) };
    texture.mip          = (_id >> kTexIdMipShift) & kTexIdMipMask;
    texture.face         = static_cast<eCubeFace>((_id >> kTexIdFaceShift) & kTexIdFaceMask);
    texture.layer        = static_cast<uint32_t>((_id >> kTexIdLayerShift) & kTexIdLayerMask);
    return texture;
}

}   // namespace jug

namespace ImGui
{

namespace
{
    [[nodiscard]] ImTextureID ToImTextureID_(
        const jug::ImGuiTexture& _texture)
    {
        return static_cast<ImTextureID>(jug::ToTextureID(_texture));
    }
}   // namespace

// ===========================================
//  Widget
// ===========================================

void Image(
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _size,
    const ImVec2&            _uv0,
    const ImVec2&            _uv1)
{
    Image(ToImTextureID_(_texture), _size, _uv0, _uv1);
}

void ImageWithBg(
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _size,
    const ImVec2&            _uv0,
    const ImVec2&            _uv1,
    const ImVec4&            _bgCol,
    const ImVec4&            _tintCol)
{
    ImageWithBg(ToImTextureID_(_texture), _size, _uv0, _uv1, _bgCol, _tintCol);
}

bool ImageButton(
    const char*              _strID,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _size,
    const ImVec2&            _uv0,
    const ImVec2&            _uv1,
    const ImVec4&            _bgCol,
    const ImVec4&            _tintCol)
{
    return ImageButton(_strID, ToImTextureID_(_texture), _size, _uv0, _uv1, _bgCol, _tintCol);
}

// ===========================================
//  Draw List (low level)
// ===========================================

void AddImage(
    ImDrawList*              _pDrawList,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _pMin,
    const ImVec2&            _pMax,
    const ImVec2&            _uvMin,
    const ImVec2&            _uvMax,
    const ImU32              _col)
{
    JUG_ASSERT(_pDrawList, "ImGui::AddImage: _pDrawList is nullptr.");
    _pDrawList->AddImage(ToImTextureID_(_texture), _pMin, _pMax, _uvMin, _uvMax, _col);
}

void AddImageQuad(
    ImDrawList*              _pDrawList,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _p1,
    const ImVec2&            _p2,
    const ImVec2&            _p3,
    const ImVec2&            _p4,
    const ImVec2&            _uv1,
    const ImVec2&            _uv2,
    const ImVec2&            _uv3,
    const ImVec2&            _uv4,
    const ImU32              _col)
{
    JUG_ASSERT(_pDrawList, "ImGui::AddImageQuad: _pDrawList is nullptr.");
    _pDrawList->AddImageQuad(ToImTextureID_(_texture), _p1, _p2, _p3, _p4, _uv1, _uv2, _uv3, _uv4, _col);
}

void AddImageRounded(
    ImDrawList*              _pDrawList,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _pMin,
    const ImVec2&            _pMax,
    const ImVec2&            _uvMin,
    const ImVec2&            _uvMax,
    const ImU32              _col,
    const float              _rounding,
    const ImDrawFlags        _flags)
{
    JUG_ASSERT(_pDrawList, "ImGui::AddImageRounded: _pDrawList is nullptr.");
    _pDrawList->AddImageRounded(ToImTextureID_(_texture), _pMin, _pMax, _uvMin, _uvMax, _col, _rounding, _flags);
}

}   // namespace ImGui
