#pragma once

namespace jug
{

struct Geometry
{
    ByteBuffer vertexData = {};
    ByteBuffer indexData  = {};
    bool       bU32       = false;
};

[[nodiscard]] Geometry CreateBox(VEC3 _size, VEC3 _normal, const VertexLayout& _vl);
[[nodiscard]] Geometry CreateSphere(float _radius, uint32_t _numSubdivision, const VertexLayout& _vl);
[[nodiscard]] Geometry CreateCylinder(float _radius, float _height, uint32_t _numTri, VEC3 _normal, const VertexLayout& _vl);
[[nodiscard]] Geometry CreateCone(float _radius, float _height, uint32_t _numTri, VEC3 _normal, const VertexLayout& _vl);
[[nodiscard]] Geometry CreatePlane(float _width, float _height, uint32_t _numSegX, uint32_t _numSegY, VEC3 _normal, const VertexLayout& _vl);
[[nodiscard]] Geometry CreateCapsule(float _radius, float _height, uint32_t _numSegHeight, uint32_t _numSegCircle, const VertexLayout& _vl);

}   // namespace jug