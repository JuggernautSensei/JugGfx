#include "pch.h"
#include "TangentNormal.h"

namespace jug
{

namespace
{

    template<typename Index>
    [[nodiscard]] Buffer<VECTOR3> CreateNormals_(
        const Span<const VECTOR3> _positions,
        const Span<const Index>   _indices)
    {
        JUG_ASSERT(_indices.size() % 3 == 0, "CreateNormals() - Index count must be a multiple of 3");

        Buffer<VECTOR3> normals { kNoInit, _positions.size() };

        for (size_t i = 0; i < _indices.size(); i += 3)
        {
            const size_t i0 = _indices[i];
            const size_t i1 = _indices[i + 1];
            const size_t i2 = _indices[i + 2];
            JUG_ASSERT(i0 < _positions.size() && i1 < _positions.size() && i2 < _positions.size(), "CreateNormals() - Index out of range");

            const VECTOR3 faceNormal = Cross(_positions[i1] - _positions[i0], _positions[i2] - _positions[i0]);
            normals[i0] += faceNormal;
            normals[i1] += faceNormal;
            normals[i2] += faceNormal;
        }

        for (VECTOR3& normal: normals)
        {
            const float lenSq = LengthSq(normal);
            if (lenSq > 0.f)
            {
                normal = normal * RSqrt(lenSq);
            }
        }

        return normals;
    }

    template<typename Index>
    [[nodiscard]] CreateTangentBitangentResult CreateTangentsAndBitangents_(
        const Span<const VECTOR3> _positions,
        const Span<const VECTOR2> _texCoords,
        const Span<const Index>   _indices)
    {
        JUG_ASSERT(_indices.size() % 3 == 0, "CreateTangentsAndBitangents() - Index count must be a multiple of 3");
        JUG_ASSERT(_texCoords.size() == _positions.size(), "CreateTangentsAndBitangents() - Texcoord count must match position count");

        CreateTangentBitangentResult result = {};
        result.tangents.ResizeUninitialized(_positions.size());
        result.bitangents.ResizeUninitialized(_positions.size());

        for (size_t i = 0; i < _indices.size(); i += 3)
        {
            const size_t i0 = _indices[i];
            const size_t i1 = _indices[i + 1];
            const size_t i2 = _indices[i + 2];
            JUG_ASSERT(i0 < _positions.size() && i1 < _positions.size() && i2 < _positions.size(), "CreateTangentsAndBitangents() - Index out of range");

            const VECTOR3 e1 = _positions[i1] - _positions[i0];
            const VECTOR3 e2 = _positions[i2] - _positions[i0];
            const VECTOR2 d1 = _texCoords[i1] - _texCoords[i0];
            const VECTOR2 d2 = _texCoords[i2] - _texCoords[i0];

            const float det = Cross(d1, d2);
            if (det == 0.f)   // NOLINT
            {
                continue;   // UV 가 접혔거나 축퇴한 삼각형.
            }

            const float   invDet    = 1.f / det;
            const VECTOR3 tangent   = (e1 * d2.y - e2 * d1.y) * invDet;
            const VECTOR3 bitangent = (e2 * d1.x - e1 * d2.x) * invDet;

            result.tangents[i0] += tangent;
            result.tangents[i1] += tangent;
            result.tangents[i2] += tangent;
            result.bitangents[i0] += bitangent;
            result.bitangents[i1] += bitangent;
            result.bitangents[i2] += bitangent;
        }

        for (size_t i = 0; i < _positions.size(); ++i)
        {
            const float tanLenSq = LengthSq(result.tangents[i]);
            const float bitLenSq = LengthSq(result.bitangents[i]);
            if (tanLenSq > 0.f)
            {
                result.tangents[i] = result.tangents[i] * RSqrt(tanLenSq);
            }
            if (bitLenSq > 0.f)
            {
                result.bitangents[i] = result.bitangents[i] * RSqrt(bitLenSq);
            }
        }

        return result;
    }

}   // namespace

Buffer<VECTOR3> CreateNormals(
    const Span<const VECTOR3>  _positions,
    const Span<const uint32_t> _indices)
{
    return CreateNormals_(_positions, _indices);
}

Buffer<VECTOR3> CreateNormals(
    const Span<const VECTOR3>  _positions,
    const Span<const uint16_t> _indices)
{
    return CreateNormals_(_positions, _indices);
}

CreateTangentBitangentResult CreateTangentsAndBitangents(
    const Span<const VECTOR3>  _positions,
    const Span<const VECTOR2>  _texCoords,
    const Span<const uint32_t> _indices)
{
    return CreateTangentsAndBitangents_(_positions, _texCoords, _indices);
}

CreateTangentBitangentResult CreateTangentsAndBitangents(
    const Span<const VECTOR3>  _positions,
    const Span<const VECTOR2>  _texCoords,
    const Span<const uint16_t> _indices)
{
    return CreateTangentsAndBitangents_(_positions, _texCoords, _indices);
}

}   // namespace jug
