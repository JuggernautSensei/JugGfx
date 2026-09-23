#pragma once

namespace jug
{

enum class eVertexAttribute
{
    Position,
    Normal,
    Tangent,
    Bitangent,
    Color0,
    Color1,
    Color2,
    Color3,
    TexCoord0,
    TexCoord1,
    TexCoord2,
    TexCoord3,
    BoneIndex,
    BlendWeight,
};

enum class eVertexAttributeFormat
{
    Float,
    SInt,
    UInt,
};

struct VertexAttribute
{
    [[nodiscard]] bool IsValid() const
    {
        return num > 0;
    }

    eVertexAttribute       attrib      = eVertexAttribute::Position;
    eVertexAttributeFormat format      = eVertexAttributeFormat::Float;
    uint32_t               num         = 0;
    bool                   bNormalized = false;
    uint32_t               byteWidth   = 0;
    uint32_t               offset      = 0;
};

class VertexLayout
{
public:
    VertexLayout&      Add(eVertexAttribute _attrib, eVertexAttributeFormat _format, uint32_t _num, bool _bNormalized = false);
    [[nodiscard]] bool Has(eVertexAttribute _attrib) const;

    [[nodiscard]] const VertexAttribute&      Get(eVertexAttribute _attrib) const;
    [[nodiscard]] uint64_t                    GetHash() const;
    [[nodiscard]] uint32_t                    GetStride() const;
    [[nodiscard]] size_t                      GetNumAttributes() const;
    [[nodiscard]] Span<const VertexAttribute> GetAttributes() const;

private:
    constexpr static uint64_t kDirtyHash = 0;

    ARRAY<VertexAttribute, CountOf<eVertexAttribute>()> m_attribs    = {};
    size_t                                              m_numAttribs = 0;
    uint32_t                                            m_stride     = 0;
    mutable uint64_t                                    m_hash       = 0;
};

}   // namespace jug