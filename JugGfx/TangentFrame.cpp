#include "pch.h"
#include "TangentFrame.h"

namespace jug
{

namespace
{

    template<typename Index>
    [[nodiscard]] Buffer<VEC3> CreateNormals_(
        const Span<const VEC3>  _positions,
        const Span<const Index> _indices)
    {
        JUG_ASSERT(_indices.size() % 3 == 0, "CreateNormals() - Index count must be a multiple of 3");

        Buffer<VEC3> normals { _positions.size(), Zero<VEC3>() };   // 누적하므로 0 으로 초기화

        for (size_t i = 0; i < _indices.size(); i += 3)
        {
            const size_t i0 = _indices[i];
            const size_t i1 = _indices[i + 1];
            const size_t i2 = _indices[i + 2];
            JUG_ASSERT(i0 < _positions.size() && i1 < _positions.size() && i2 < _positions.size(), "CreateNormals() - Index out of range");

            const VEC3 faceNormal = Cross(_positions[i1] - _positions[i0], _positions[i2] - _positions[i0]);
            normals[i0] += faceNormal;
            normals[i1] += faceNormal;
            normals[i2] += faceNormal;
        }

        for (VEC3& normal: normals)
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
    [[nodiscard]] TangentsBitangents CreateTangentsAndBitangents_(
        const Span<const VEC3>  _positions,
        const Span<const VEC2>  _texCoords,
        const Span<const Index> _indices)
    {
        JUG_ASSERT(_indices.size() % 3 == 0, "CreateTangentsAndBitangents() - Index count must be a multiple of 3");
        JUG_ASSERT(_texCoords.size() == _positions.size(), "CreateTangentsAndBitangents() - Texcoord count must match position count");

        TangentsBitangents result = {};
        result.tangents.Resize(_positions.size());     // 누적하므로 0 으로 초기화
        result.bitangents.Resize(_positions.size());

        for (size_t i = 0; i < _indices.size(); i += 3)
        {
            const size_t i0 = _indices[i];
            const size_t i1 = _indices[i + 1];
            const size_t i2 = _indices[i + 2];
            JUG_ASSERT(i0 < _positions.size() && i1 < _positions.size() && i2 < _positions.size(), "CreateTangentsAndBitangents() - Index out of range");

            const VEC3 e1 = _positions[i1] - _positions[i0];
            const VEC3 e2 = _positions[i2] - _positions[i0];
            const VEC2 d1 = _texCoords[i1] - _texCoords[i0];
            const VEC2 d2 = _texCoords[i2] - _texCoords[i0];

            const float det = Cross(d1, d2);
            if (det == 0.f)   // NOLINT
            {
                continue;   // UV 가 접혔거나 축퇴한 삼각형.
            }

            const float invDet    = 1.f / det;
            const VEC3  tangent   = (e1 * d2.y - e2 * d1.y) * invDet;
            const VEC3  bitangent = (e2 * d1.x - e1 * d2.x) * invDet;

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

Buffer<VEC3> CreateNormals(
    const Span<const VEC3>     _positions,
    const Span<const uint32_t> _indices)
{
    return CreateNormals_(_positions, _indices);
}

Buffer<VEC3> CreateNormals(
    const Span<const VEC3>     _positions,
    const Span<const uint16_t> _indices)
{
    return CreateNormals_(_positions, _indices);
}

TangentsBitangents CreateTangentsAndBitangents(
    const Span<const VEC3>     _positions,
    const Span<const VEC2>     _texCoords,
    const Span<const uint32_t> _indices)
{
    return CreateTangentsAndBitangents_(_positions, _texCoords, _indices);
}

TangentsBitangents CreateTangentsAndBitangents(
    const Span<const VEC3>     _positions,
    const Span<const VEC2>     _texCoords,
    const Span<const uint16_t> _indices)
{
    return CreateTangentsAndBitangents_(_positions, _texCoords, _indices);
}

}   // namespace jug
