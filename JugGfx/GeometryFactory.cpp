#include "pch.h"
#include "GeometryFactory.h"

#include <fmt/base.h>

namespace jug
{

namespace
{

    struct Vertex
    {
        VECTOR3 position = VECTOR3::kZero;
        VECTOR3 normal   = VECTOR3::kUp;
        VECTOR3 tangent  = VECTOR3::kRight;
        VECTOR2 texCoord = VECTOR2::kZero;
    };

    QUATERNION MakeAxisRotation_(
        const VECTOR3 _normal)
    {
        JUG_ASSERT(!IsZeroApprox(_normal), "GeometryFactory - Axis normal must not be zero");
        return QUATERNION::MakeFromTo(VECTOR3::kUp, Normalize(_normal));
    }

    GeometryFactoryResult Pack_(
        const Span<Vertex>&   _vertices,
        const Span<uint32_t>& _indices,
        const VertexLayout&   _vl,
        const QUATERNION      _rotation)
    {
        JUG_ASSERT(_vl.Has(eVertexAttribute::Position), "GeometryFactory - VertexLayout must have a position attribute");

        const uint32_t stride      = _vl.GetStride();
        const size_t   numVertices = _vertices.size();
        const size_t   numIndices  = _indices.size();

        GeometryFactoryResult result = {};
        result.bU32                  = numVertices > 0xFFFF;
        result.vertexData.ResizeUninitialized(numVertices * stride);
        result.indexData.ResizeUninitialized(numIndices * (result.bU32 ? sizeof(uint32_t) : sizeof(uint16_t)));

        for (size_t i = 0; i < numVertices; ++i)
        {
            const Vertex& vertex = _vertices[i];

            const VECTOR3 position  = Rotate(vertex.position, _rotation);
            const VECTOR3 normal    = Rotate(vertex.normal, _rotation);
            const VECTOR3 tangent   = Rotate(vertex.tangent, _rotation);
            const VECTOR3 bitangent = Cross(normal, tangent);

            std::byte* pDst = result.vertexData.data() + i * stride;
            for (const VertexAttribute& attrib: _vl.GetAttributes())
            {
                if (attrib.format != eVertexAttributeFormat::Float)
                {
                    std::memset(pDst + attrib.offset, 0, attrib.num * sizeof(float));   // 정수 채널은 지원하지 않아 0으로 채운다.
                    continue;
                }

                const float* pSrc   = nullptr;
                uint32_t     numSrc = 0;

                switch (attrib.attrib)   // NOLINT
                {
                    case eVertexAttribute::Position:
                        pSrc   = position.GetPtr();
                        numSrc = 3;
                        break;

                    case eVertexAttribute::Normal:
                        pSrc   = normal.GetPtr();
                        numSrc = 3;
                        break;

                    case eVertexAttribute::Tangent:
                        pSrc   = tangent.GetPtr();
                        numSrc = 3;
                        break;

                    case eVertexAttribute::Bitangent:
                        pSrc   = bitangent.GetPtr();
                        numSrc = 3;
                        break;

                    case eVertexAttribute::TexCoord0:
                    case eVertexAttribute::TexCoord1:
                    case eVertexAttribute::TexCoord2:
                    case eVertexAttribute::TexCoord3:
                        pSrc   = vertex.texCoord.GetPtr();
                        numSrc = 2;
                        break;

                    default:
                        break;   // Color0~3 은 지원하지 않는다.
                }

                if (pSrc == nullptr)
                {
                    std::memset(pDst + attrib.offset, 0, attrib.num * sizeof(float));   // 미지원 attrib 는 0으로 채운다.
                    continue;
                }

                const uint32_t numCopy = Min(attrib.num, numSrc);
                std::memcpy(pDst + attrib.offset, pSrc, numCopy * sizeof(float));
                if (numCopy < attrib.num)
                {
                    std::memset(pDst + attrib.offset + numCopy * sizeof(float), 0, (attrib.num - numCopy) * sizeof(float));   // numSrc 가 모자란 나머지는 0으로 채운다.
                }
            }
        }

        if (result.bU32)
        {
            std::memcpy(result.indexData.data(), _indices.data(), numIndices * sizeof(uint32_t));
        }
        else
        {
            uint16_t* pDst = reinterpret_cast<uint16_t*>(result.indexData.data());
            for (size_t i = 0; i < numIndices; ++i)
            {
                pDst[i] = static_cast<uint16_t>(_indices[i]);
            }
        }

        return result;
    }

}   // namespace

GeometryFactoryResult CreateBox(
    const VECTOR3       _size,
    const VECTOR3       _normal,
    const VertexLayout& _vl)
{
    constexpr ARRAY<VECTOR3, 6> kFaceNormals = {
        VECTOR3 {  1.f,  0.f,  0.f },
        VECTOR3 { -1.f,  0.f,  0.f },
        VECTOR3 {  0.f,  1.f,  0.f },
        VECTOR3 {  0.f, -1.f,  0.f },
        VECTOR3 {  0.f,  0.f,  1.f },
        VECTOR3 {  0.f,  0.f, -1.f }
    };

    constexpr ARRAY<VECTOR3, 6> kFaceTangents = {
        VECTOR3 {  0.f, 0.f,  1.f },
        VECTOR3 {  0.f, 0.f, -1.f },
        VECTOR3 {  1.f, 0.f,  0.f },
        VECTOR3 {  1.f, 0.f,  0.f },
        VECTOR3 { -1.f, 0.f,  0.f },
        VECTOR3 {  1.f, 0.f,  0.f }
    };

    constexpr ARRAY<VECTOR2, 4> kCornerUVs = {
        VECTOR2 { 0.f, 0.f },
        VECTOR2 { 1.f, 0.f },
        VECTOR2 { 1.f, 1.f },
        VECTOR2 { 0.f, 1.f }
    };

    Buffer<Vertex>   vertices { kNoInit, 24 };
    Buffer<uint32_t> indices { kNoInit, 36 };

    for (uint32_t face = 0; face < 6; ++face)
    {
        const VECTOR3 n = kFaceNormals[face];
        const VECTOR3 t = kFaceTangents[face];
        const VECTOR3 b = Cross(n, t);

        const VECTOR3 center = n * (_size * 0.5f);
        const float   sizeT  = Abs(Dot(t, _size));
        const float   sizeB  = Abs(Dot(b, _size));

        const uint32_t base = static_cast<uint32_t>(vertices.GetSize());
        for (uint32_t corner = 0; corner < 4; ++corner)
        {
            const VECTOR2 uv = kCornerUVs[corner];

            Vertex vertex           = {};
            vertex.position         = center + t * (sizeT * (uv.x - 0.5f)) + b * (sizeB * (uv.y - 0.5f));
            vertex.normal           = n;
            vertex.tangent          = t;
            vertex.texCoord         = uv;
            vertices[base + corner] = vertex;
        }

        indices[face * 6 + 0] = base + 0;
        indices[face * 6 + 1] = base + 1;
        indices[face * 6 + 2] = base + 2;
        indices[face * 6 + 3] = base + 0;
        indices[face * 6 + 4] = base + 2;
        indices[face * 6 + 5] = base + 3;
    }

    return Pack_(vertices, indices, _vl, MakeAxisRotation_(_normal));
}

GeometryFactoryResult CreateSphere(
    const float         _radius,
    const uint32_t      _numSubdivision,
    const VertexLayout& _vl)
{
    JUG_ASSERT(_numSubdivision >= 2, "GeometryFactory::CreateSphere() - Too few subdivisions");

    const uint32_t numStacks = _numSubdivision;
    const uint32_t numSlices = _numSubdivision * 2;

    Buffer<Vertex>   vertices { kNoInit, static_cast<size_t>(numStacks + 1) * (numSlices + 1) };
    Buffer<uint32_t> indices { kNoInit, static_cast<size_t>(numStacks) * numSlices * 6 };

    size_t vIndex = 0;
    for (uint32_t stack = 0; stack <= numStacks; ++stack)
    {
        const float v     = static_cast<float>(stack) / static_cast<float>(numStacks);
        const float theta = v * kPI;
        const float sinT  = Sin(theta);
        const float cosT  = Cos(theta);

        for (uint32_t slice = 0; slice <= numSlices; ++slice)
        {
            const float u    = static_cast<float>(slice) / static_cast<float>(numSlices);
            const float phi  = u * k2PI;
            const float sinP = Sin(phi);
            const float cosP = Cos(phi);

            Vertex vertex      = {};
            vertex.normal      = { sinT * cosP, cosT, sinT * sinP };
            vertex.position    = vertex.normal * _radius;
            vertex.tangent     = { -sinP, 0.f, cosP };
            vertex.texCoord    = { u, v };
            vertices[vIndex++] = vertex;
        }
    }

    size_t iIndex = 0;
    for (uint32_t stack = 0; stack < numStacks; ++stack)
    {
        for (uint32_t slice = 0; slice < numSlices; ++slice)
        {
            const uint32_t i0 = stack * (numSlices + 1) + slice;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + numSlices + 1;
            const uint32_t i3 = i2 + 1;

            if (stack != 0)
            {
                indices[iIndex++] = i0;
                indices[iIndex++] = i1;
                indices[iIndex++] = i3;
            }

            if (stack + 1 != numStacks)
            {
                indices[iIndex++] = i0;
                indices[iIndex++] = i3;
                indices[iIndex++] = i2;
            }
        }
    }
    indices.ResizeUninitialized(iIndex);

    return Pack_(vertices, indices, _vl, QUATERNION::kIdentity);
}

GeometryFactoryResult CreateCylinder(
    const float         _radius,
    const float         _height,
    const uint32_t      _numTri,
    const VECTOR3       _normal,
    const VertexLayout& _vl)
{
    JUG_ASSERT(_numTri >= 3, "GeometryFactory::CreateCylinder() - Too few segments");

    const float half = _height * 0.5f;

    Buffer<Vertex>   vertices { kNoInit, static_cast<size_t>(_numTri + 1) * 2 + static_cast<size_t>(_numTri + 1) * 2 };
    Buffer<uint32_t> indices { kNoInit, static_cast<size_t>(_numTri) * 12 };

    size_t vIndex = 0;
    size_t iIndex = 0;

    // 옆면
    for (uint32_t row = 0; row < 2; ++row)
    {
        for (uint32_t seg = 0; seg <= _numTri; ++seg)
        {
            const float u    = static_cast<float>(seg) / static_cast<float>(_numTri);
            const float phi  = u * k2PI;
            const float sinP = Sin(phi);
            const float cosP = Cos(phi);

            Vertex vertex      = {};
            vertex.position    = { _radius * cosP, row == 0 ? half : -half, _radius * sinP };
            vertex.normal      = { cosP, 0.f, sinP };
            vertex.tangent     = { -sinP, 0.f, cosP };
            vertex.texCoord    = { u, static_cast<float>(row) };
            vertices[vIndex++] = vertex;
        }
    }

    for (uint32_t seg = 0; seg < _numTri; ++seg)
    {
        const uint32_t i0 = seg;
        const uint32_t i1 = i0 + 1;
        const uint32_t i2 = i0 + _numTri + 1;
        const uint32_t i3 = i2 + 1;
        indices[iIndex++] = i0;
        indices[iIndex++] = i1;
        indices[iIndex++] = i3;
        indices[iIndex++] = i0;
        indices[iIndex++] = i3;
        indices[iIndex++] = i2;
    }

    for (uint32_t cap = 0; cap < 2; ++cap)
    {
        const bool     bTop  = cap == 0;
        const VECTOR3  n     = bTop ? VECTOR3 { 0.f, 1.f, 0.f } : VECTOR3 { 0.f, -1.f, 0.f };
        const float    y     = bTop ? half : -half;
        const float    vSign = bTop ? -0.5f : 0.5f;
        const uint32_t base  = static_cast<uint32_t>(vIndex);

        Vertex center      = {};
        center.position    = { 0.f, y, 0.f };
        center.normal      = n;
        center.tangent     = { 1.f, 0.f, 0.f };
        center.texCoord    = { 0.5f, 0.5f };
        vertices[vIndex++] = center;

        for (uint32_t seg = 0; seg < _numTri; ++seg)
        {
            const float phi  = static_cast<float>(seg) / static_cast<float>(_numTri) * k2PI;
            const float sinP = Sin(phi);
            const float cosP = Cos(phi);

            Vertex vertex      = {};
            vertex.position    = { _radius * cosP, y, _radius * sinP };
            vertex.normal      = n;
            vertex.tangent     = { 1.f, 0.f, 0.f };
            vertex.texCoord    = { 0.5f + 0.5f * cosP, 0.5f + vSign * sinP };
            vertices[vIndex++] = vertex;
        }

        for (uint32_t seg = 0; seg < _numTri; ++seg)
        {
            const uint32_t i0 = base + 1 + seg;
            const uint32_t i1 = base + 1 + (seg + 1) % _numTri;

            if (bTop)
            {
                indices[iIndex++] = base;
                indices[iIndex++] = i1;
                indices[iIndex++] = i0;
            }
            else
            {
                indices[iIndex++] = base;
                indices[iIndex++] = i0;
                indices[iIndex++] = i1;
            }
        }
    }

    return Pack_(vertices, indices, _vl, MakeAxisRotation_(_normal));
}

GeometryFactoryResult CreateCone(
    const float         _radius,
    const float         _height,
    const uint32_t      _numTri,
    const VECTOR3       _normal,
    const VertexLayout& _vl)
{
    JUG_ASSERT(_numTri >= 3, "GeometryFactory::CreateCone() - Too few segments");

    const float half     = _height * 0.5f;
    const float invSlant = RSqrt(_radius * _radius + _height * _height);

    Buffer<Vertex>   vertices { kNoInit, static_cast<size_t>(_numTri + 1) * 2 + _numTri + 1 };
    Buffer<uint32_t> indices { kNoInit, static_cast<size_t>(_numTri) * 6 };

    size_t vIndex = 0;
    size_t iIndex = 0;

    for (uint32_t row = 0; row < 2; ++row)
    {
        for (uint32_t seg = 0; seg <= _numTri; ++seg)
        {
            const float u    = static_cast<float>(seg) / static_cast<float>(_numTri);
            const float phi  = u * k2PI;
            const float sinP = Sin(phi);
            const float cosP = Cos(phi);

            Vertex vertex      = {};
            vertex.position    = row == 0 ? VECTOR3 { 0.f, half, 0.f } : VECTOR3 { _radius * cosP, -half, _radius * sinP };
            vertex.normal      = VECTOR3 { _height * cosP, _radius, _height * sinP } * invSlant;
            vertex.tangent     = { -sinP, 0.f, cosP };
            vertex.texCoord    = { u, static_cast<float>(row) };
            vertices[vIndex++] = vertex;
        }
    }

    for (uint32_t seg = 0; seg < _numTri; ++seg)
    {
        const uint32_t i0 = seg;
        const uint32_t i2 = i0 + _numTri + 1;
        const uint32_t i3 = i2 + 1;
        indices[iIndex++] = i0;
        indices[iIndex++] = i3;
        indices[iIndex++] = i2;
    }

    // 밑면
    {
        const uint32_t base = static_cast<uint32_t>(vIndex);

        Vertex center      = {};
        center.position    = { 0.f, -half, 0.f };
        center.normal      = { 0.f, -1.f, 0.f };
        center.tangent     = { 1.f, 0.f, 0.f };
        center.texCoord    = { 0.5f, 0.5f };
        vertices[vIndex++] = center;

        for (uint32_t seg = 0; seg < _numTri; ++seg)
        {
            const float phi  = static_cast<float>(seg) / static_cast<float>(_numTri) * k2PI;
            const float sinP = Sin(phi);
            const float cosP = Cos(phi);

            Vertex vertex      = {};
            vertex.position    = { _radius * cosP, -half, _radius * sinP };
            vertex.normal      = { 0.f, -1.f, 0.f };
            vertex.tangent     = { 1.f, 0.f, 0.f };
            vertex.texCoord    = { 0.5f + 0.5f * cosP, 0.5f + 0.5f * sinP };
            vertices[vIndex++] = vertex;
        }

        for (uint32_t seg = 0; seg < _numTri; ++seg)
        {
            indices[iIndex++] = base;
            indices[iIndex++] = base + 1 + seg;
            indices[iIndex++] = base + 1 + (seg + 1) % _numTri;
        }
    }

    return Pack_(vertices, indices, _vl, MakeAxisRotation_(_normal));
}

GeometryFactoryResult CreatePlane(
    const float         _width,
    const float         _height,
    const uint32_t      _numSegX,
    const uint32_t      _numSegY,
    const VECTOR3       _normal,
    const VertexLayout& _vl)
{
    JUG_ASSERT(_numSegX >= 1 && _numSegY >= 1, "GeometryFactory::CreatePlane() - Too few segments");

    constexpr VECTOR3 kNormal    = { 0.f, 1.f, 0.f };
    constexpr VECTOR3 kTangent   = { 1.f, 0.f, 0.f };
    constexpr VECTOR3 kBitangent = { 0.f, 0.f, -1.f };

    Buffer<Vertex>   vertices { kNoInit, static_cast<size_t>(_numSegX + 1) * (_numSegY + 1) };
    Buffer<uint32_t> indices { kNoInit, static_cast<size_t>(_numSegX) * _numSegY * 6 };

    size_t vIndex = 0;
    for (uint32_t y = 0; y <= _numSegY; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(_numSegY);
        for (uint32_t x = 0; x <= _numSegX; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(_numSegX);

            Vertex vertex      = {};
            vertex.position    = kTangent * (_width * (u - 0.5f)) + kBitangent * (_height * (v - 0.5f));
            vertex.normal      = kNormal;
            vertex.tangent     = kTangent;
            vertex.texCoord    = { u, v };
            vertices[vIndex++] = vertex;
        }
    }

    size_t iIndex = 0;
    for (uint32_t y = 0; y < _numSegY; ++y)
    {
        for (uint32_t x = 0; x < _numSegX; ++x)
        {
            const uint32_t i0 = y * (_numSegX + 1) + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + _numSegX + 1;
            const uint32_t i3 = i2 + 1;
            indices[iIndex++] = i0;
            indices[iIndex++] = i1;
            indices[iIndex++] = i3;
            indices[iIndex++] = i0;
            indices[iIndex++] = i3;
            indices[iIndex++] = i2;
        }
    }

    return Pack_(vertices, indices, _vl, MakeAxisRotation_(_normal));
}

GeometryFactoryResult CreateCapsule(
    const float         _radius,
    const float         _height,
    const uint32_t      _numSegHeight,
    const uint32_t      _numSegCircle,
    const VertexLayout& _vl)
{
    JUG_ASSERT(_numSegHeight >= 1, "GeometryFactory::CreateCapsule() - Too few height segments");
    JUG_ASSERT(_numSegCircle >= 3, "GeometryFactory::CreateCapsule() - Too few circle segments");

    const uint32_t numCapRings = Max(1u, _numSegCircle / 4);

    const float half        = _height * 0.5f;
    const float capArc      = kHalfPI * _radius;
    const float invTotalArc = 1.f / (capArc * 2.f + _height);

    struct Ring
    {
        float radius   = 0.f;
        float y        = 0.f;
        float normalXZ = 0.f;
        float normalY  = 0.f;
        float v        = 0.f;
    };

    Vector<Ring> rings;
    rings.reserve(static_cast<size_t>(numCapRings) * 2 + _numSegHeight + 1);

    for (uint32_t i = 0; i <= numCapRings; ++i)   // 윗 반구
    {
        const float t     = static_cast<float>(i) / static_cast<float>(numCapRings);
        const float theta = t * kHalfPI;
        rings.push_back(Ring { _radius * Sin(theta), half + _radius * Cos(theta), Sin(theta), Cos(theta), t * capArc * invTotalArc });
    }

    for (uint32_t i = 1; i <= _numSegHeight; ++i)   // 원기둥부
    {
        const float t = static_cast<float>(i) / static_cast<float>(_numSegHeight);
        rings.push_back(Ring { _radius, half - t * _height, 1.f, 0.f, (capArc + t * _height) * invTotalArc });
    }

    for (uint32_t i = 1; i <= numCapRings; ++i)   // 아랫 반구
    {
        const float t     = static_cast<float>(i) / static_cast<float>(numCapRings);
        const float theta = kHalfPI + t * kHalfPI;
        rings.push_back(Ring { _radius * Sin(theta), -half + _radius * Cos(theta), Sin(theta), Cos(theta), (capArc + _height + t * capArc) * invTotalArc });
    }

    const uint32_t numRings = static_cast<uint32_t>(rings.size());

    Buffer<Vertex>   vertices { kNoInit, static_cast<size_t>(numRings) * (_numSegCircle + 1) };
    Buffer<uint32_t> indices { kNoInit, static_cast<size_t>(numRings - 1) * _numSegCircle * 6 };

    size_t vIndex = 0;
    for (const Ring& ring: rings)
    {
        for (uint32_t seg = 0; seg <= _numSegCircle; ++seg)
        {
            const float u    = static_cast<float>(seg) / static_cast<float>(_numSegCircle);
            const float phi  = u * k2PI;
            const float sinP = Sin(phi);
            const float cosP = Cos(phi);

            Vertex vertex      = {};
            vertex.position    = { ring.radius * cosP, ring.y, ring.radius * sinP };
            vertex.normal      = { ring.normalXZ * cosP, ring.normalY, ring.normalXZ * sinP };
            vertex.tangent     = { -sinP, 0.f, cosP };
            vertex.texCoord    = { u, ring.v };
            vertices[vIndex++] = vertex;
        }
    }

    size_t iIndex = 0;
    for (uint32_t row = 0; row + 1 < numRings; ++row)
    {
        for (uint32_t seg = 0; seg < _numSegCircle; ++seg)
        {
            const uint32_t i0 = row * (_numSegCircle + 1) + seg;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + _numSegCircle + 1;
            const uint32_t i3 = i2 + 1;

            if (row != 0)   // 북극에서는 첫 삼각형이 축퇴한다.
            {
                indices[iIndex++] = i0;
                indices[iIndex++] = i1;
                indices[iIndex++] = i3;
            }

            if (row + 2 != numRings)   // 남극에서는 두 번째 삼각형이 축퇴한다.
            {
                indices[iIndex++] = i0;
                indices[iIndex++] = i3;
                indices[iIndex++] = i2;
            }
        }
    }
    indices.ResizeUninitialized(iIndex);

    return Pack_(vertices, indices, _vl, QUATERNION::kIdentity);
}

}   // namespace jug
