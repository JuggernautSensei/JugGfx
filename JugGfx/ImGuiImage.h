#pragma once
#include "ImGuiInclude.h"

namespace jug
{

struct ImGuiTexture
{
    TextureHandle texh  = kNullHandle;
    uint32_t      mip   = 0;
    uint32_t      layer = 0;
    eCubeFace     face  = eCubeFace::PosX;
};

[[nodiscard]] uint64_t     ToTextureID(TextureHandle _texh);
[[nodiscard]] uint64_t     ToTextureID(ImGuiTexture _texture);
[[nodiscard]] ImGuiTexture FromTextureID(uint64_t _id);

}

namespace ImGui
{

// ===========================================
//  public api
// ===========================================

void Image(
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _size,
    const ImVec2&            _uv0 = ImVec2(0, 0),
    const ImVec2&            _uv1 = ImVec2(1, 1));

void ImageWithBg(
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _size,
    const ImVec2&            _uv0     = ImVec2(0, 0),
    const ImVec2&            _uv1     = ImVec2(1, 1),
    const ImVec4&            _bgCol   = ImVec4(0, 0, 0, 0),
    const ImVec4&            _tintCol = ImVec4(1, 1, 1, 1));

bool ImageButton(
    const char*              _strID,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _size,
    const ImVec2&            _uv0     = ImVec2(0, 0),
    const ImVec2&            _uv1     = ImVec2(1, 1),
    const ImVec4&            _bgCol   = ImVec4(0, 0, 0, 0),
    const ImVec4&            _tintCol = ImVec4(1, 1, 1, 1));

// ===========================================
//  low level
// ===========================================

void AddImage(
    ImDrawList*              _pDrawList,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _pMin,
    const ImVec2&            _pMax,
    const ImVec2&            _uvMin = ImVec2(0, 0),
    const ImVec2&            _uvMax = ImVec2(1, 1),
    ImU32                    _col   = IM_COL32_WHITE);

void AddImageQuad(
    ImDrawList*              _pDrawList,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _p1,
    const ImVec2&            _p2,
    const ImVec2&            _p3,
    const ImVec2&            _p4,
    const ImVec2&            _uv1 = ImVec2(0, 0),
    const ImVec2&            _uv2 = ImVec2(1, 0),
    const ImVec2&            _uv3 = ImVec2(1, 1),
    const ImVec2&            _uv4 = ImVec2(0, 1),
    ImU32                    _col = IM_COL32_WHITE);

void AddImageRounded(
    ImDrawList*              _pDrawList,
    const jug::ImGuiTexture& _texture,
    const ImVec2&            _pMin,
    const ImVec2&            _pMax,
    const ImVec2&            _uvMin,
    const ImVec2&            _uvMax,
    ImU32                    _col,
    float                    _rounding,
    ImDrawFlags              _flags = 0);

}   // namespace ImGui
