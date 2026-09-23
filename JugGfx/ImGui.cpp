#include "pch.h"
#include "ImGui.h"

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
