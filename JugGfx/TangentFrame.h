#pragma once

namespace jug
{

struct TangentsBitangents
{
    Buffer<VEC3> tangents   = {};
    Buffer<VEC3> bitangents = {};
};

[[nodiscard]] Buffer<VEC3> CreateNormals(Span<const VEC3> _positions, Span<const uint32_t> _indices);
[[nodiscard]] Buffer<VEC3> CreateNormals(Span<const VEC3> _positions, Span<const uint16_t> _indices);

[[nodiscard]] TangentsBitangents CreateTangentsAndBitangents(Span<const VEC3> _positions, Span<const VEC2> _texCoords, Span<const uint32_t> _indices);
[[nodiscard]] TangentsBitangents CreateTangentsAndBitangents(Span<const VEC3> _positions, Span<const VEC2> _texCoords, Span<const uint16_t> _indices);

}   // namespace jug
