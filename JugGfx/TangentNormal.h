    #pragma once

namespace jug
{

struct CreateTangentBitangentResult
{
    Buffer<VECTOR3> tangents   = {};
    Buffer<VECTOR3> bitangents = {};
};

[[nodiscard]] Buffer<VECTOR3> CreateNormals(Span<const VECTOR3> _positions, Span<const uint32_t> _indices);
[[nodiscard]] Buffer<VECTOR3> CreateNormals(Span<const VECTOR3> _positions, Span<const uint16_t> _indices);

[[nodiscard]] CreateTangentBitangentResult CreateTangentsAndBitangents(Span<const VECTOR3> _positions, Span<const VECTOR2> _texCoords, Span<const uint32_t> _indices);
[[nodiscard]] CreateTangentBitangentResult CreateTangentsAndBitangents(Span<const VECTOR3> _positions, Span<const VECTOR2> _texCoords, Span<const uint16_t> _indices);

}   // namespace jug
