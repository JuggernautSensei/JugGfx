#include "pch.h"
#include "VertexLayout.h"

#include <JugX/MemoryHasher.h>

namespace jug
{

VertexLayout& VertexLayout::Add(
    const eVertexAttribute       _attrib,
    const eVertexAttributeFormat _format,
    const uint32_t               _num,
    const bool                   _bNormalized)
{
    JUG_ASSERT(m_numAttribs < CountOf<eVertexAttribute>(), "VertexLayout::Add() - Too many attributes");
    JUG_ASSERT(_num > 0 && _num <= 4, "VertexLayout::Add() - Invalid number of components");
    JUG_ASSERT(!Has(_attrib), "VertexLayout::Add() - Attribute already exists");

    VertexAttribute attrib = {};
    attrib.attrib          = _attrib;
    attrib.format          = _format;
    attrib.num             = _num;
    attrib.bNormalized     = _bNormalized;
    attrib.byteWidth       = _num * 4;   // Assuming 4 bytes per component (float, int, uint)
    attrib.offset          = m_stride;

    m_attribs[m_numAttribs++] = attrib;
    m_stride += attrib.byteWidth;
    m_hash = kDirtyHash;   // mark dirty
    return *this;
}

bool VertexLayout::Has(
    const eVertexAttribute _attrib) const
{
    for (size_t i = 0; i < m_numAttribs; ++i)
    {
        if (m_attribs[i].attrib == _attrib)
        {
            return true;
        }
    }
    return false;
}

const VertexAttribute& VertexLayout::Get(
    const eVertexAttribute _attrib) const
{
    const auto it = std::find_if(m_attribs.begin(), m_attribs.begin() + static_cast<std::ptrdiff_t>(m_numAttribs), [_attrib](const VertexAttribute& a) { return a.attrib == _attrib; });
    JUG_ASSERT(it != m_attribs.begin() + m_numAttribs, "VertexLayout::Get() - Attribute not found");
    return *it;
}

uint64_t VertexLayout::GetHash() const
{
    if (m_hash == kDirtyHash)
    {
        m_hash = Hash<Murmur3>(MemoryView { m_attribs, m_numAttribs });
    }

    return m_hash;
}

uint32_t VertexLayout::GetStride() const
{
    return m_stride;
}

size_t VertexLayout::GetNumAttributes() const
{
    return m_numAttribs;
}

Span<const VertexAttribute> VertexLayout::GetAttributes() const
{
    return Span<const VertexAttribute> { m_attribs.data(), m_numAttribs };
}

}   // namespace jug