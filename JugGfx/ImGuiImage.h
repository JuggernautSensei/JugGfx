#pragma once
#include "ImGuiInclude.h"

namespace jug
{

struct ImGuiTextureRef
{
    ImGuiTextureRef() = default;
    /* implicit */ ImGuiTextureRef(TextureHandle _texh, uint32_t _mip = 0, uint32_t _layer = 0, eCubeFace _face = eCubeFace::PosX);
    /* implicit */ ImGuiTextureRef(ImTextureID _id);

    [[nodiscard]] ImTextureID ToImTextureID() const;

    TextureHandle texh  = kNullHandle;
    uint32_t      mip   = 0;
    uint32_t      layer = 0;
    eCubeFace     face  = eCubeFace::PosX;
};

}   // namespace jug

namespace ImGui
{

// ===========================================
//  Public API
// ===========================================

void Image(
    jug::ImGuiTextureRef _texture,
    ImVec2               _size,
    ImVec2               _uv0 = ImVec2(0, 0),
    ImVec2               _uv1 = ImVec2(1, 1));
void ImageWithBg(
    jug::ImGuiTextureRef _texture,
    ImVec2               _size,
    ImVec2               _uv0     = ImVec2(0, 0),
    ImVec2               _uv1     = ImVec2(1, 1),
    ImVec4               _bgCol   = ImVec4(0, 0, 0, 0),
    ImVec4               _tintCol = ImVec4(1, 1, 1, 1));
bool ImageButton(
    const char*          _str,
    jug::ImGuiTextureRef _texture,
    ImVec2               _size,
    ImVec2               _uv0     = ImVec2(0, 0),
    ImVec2               _uv1     = ImVec2(1, 1),
    ImVec4               _bgCol   = ImVec4(0, 0, 0, 0),
    ImVec4               _tintCol = ImVec4(1, 1, 1, 1));

// ===========================================
//  Low Level
// ===========================================

void AddImage(
    ImDrawList*          _pDrawList,
    jug::ImGuiTextureRef _texture,
    ImVec2               _pMin,
    ImVec2               _pMax,
    ImVec2               _uvMin = ImVec2(0, 0),
    ImVec2               _uvMax = ImVec2(1, 1),
    ImU32                _col   = IM_COL32_WHITE);

void AddImageQuad(
    ImDrawList*          _pDrawList,
    jug::ImGuiTextureRef _texture,
    ImVec2               _p1,
    ImVec2               _p2,
    ImVec2               _p3,
    ImVec2               _p4,
    ImVec2               _uv1 = ImVec2(0, 0),
    ImVec2               _uv2 = ImVec2(1, 0),
    ImVec2               _uv3 = ImVec2(1, 1),
    ImVec2               _uv4 = ImVec2(0, 1),
    ImU32                _col = IM_COL32_WHITE);

void AddImageRounded(
    ImDrawList*          _pDrawList,
    jug::ImGuiTextureRef _texture,
    ImVec2               _pMin,
    ImVec2               _pMax,
    ImVec2               _uvMin,
    ImVec2               _uvMax,
    ImU32                _col,
    float                _rounding,
    ImDrawFlags          _flags = 0);

}   // namespace ImGui
