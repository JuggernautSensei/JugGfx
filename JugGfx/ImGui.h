#pragma once
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_stdlib.h>

#include "ImGuiRenderer.h"

// ===========================================
//  JugGfx texture 용 ImGui 확장
//   jug::ImGuiTexture 를 받아 ImGuiRenderer::ToTextureID 로 패킹한 뒤 기존 ImGui API 로 전달.
//
//   ImGui::Image({ texh }, size);                                        // 2D
//   ImGui::Image({ .texh = arrayTexh, .layer = 3 }, size);                // 2D array slice
//   ImGui::Image({ .texh = cubeTexh, .face = jug::eCubeFace::NegY }, size); // cube face
//   ImGui::Image({ .texh = texh, .mip = 2 }, size);                       // mip 고정
// ===========================================

namespace ImGui
{

// ===========================================
//  Widget
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
//  Draw List (low level)
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
