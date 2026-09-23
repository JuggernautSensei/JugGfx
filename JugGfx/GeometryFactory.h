#pragma once

namespace jug
{

struct GeometryFactoryResult
{
    Buffer<std::byte> vertexData = {};
    Buffer<std::byte> indexData  = {};
    bool              bU32       = false;
};

[[nodiscard]] GeometryFactoryResult CreateBox(VECTOR3 _size, VECTOR3 _normal, const VertexLayout& _vl);
[[nodiscard]] GeometryFactoryResult CreateSphere(float _radius, uint32_t _numSubdivision, const VertexLayout& _vl);
[[nodiscard]] GeometryFactoryResult CreateCylinder(float _radius, float _height, uint32_t _numTri, VECTOR3 _normal, const VertexLayout& _vl);
[[nodiscard]] GeometryFactoryResult CreateCone(float _radius, float _height, uint32_t _numTri, VECTOR3 _normal, const VertexLayout& _vl);
[[nodiscard]] GeometryFactoryResult CreatePlane(float _width, float _height, uint32_t _numSegX, uint32_t _numSegY, VECTOR3 _normal, const VertexLayout& _vl);
[[nodiscard]] GeometryFactoryResult CreateCapsule(float _radius, float _height, uint32_t _numSegHeight, uint32_t _numSegCircle, const VertexLayout& _vl);

}   // namespace jug