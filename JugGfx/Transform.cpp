#include "pch.h"
#include "Transform.h"

namespace jug
{

void Transform::SetTransform(
    const AFFINE_TRANSFORM& _transform)
{
    m_tranform     = _transform;
    m_bSrtDirty    = true;
    m_bInvSrtDirty = true;
}

const AFFINE_TRANSFORM& Transform::GetTransform() const
{
    return m_tranform;
}

void Transform::SetSRT(
    const MATRIX& _srt)
{
    m_tranform     = AFFINE_TRANSFORM::MakeFromMatrix(_srt);
    m_srt          = _srt;
    m_bSrtDirty    = false;
    m_bInvSrtDirty = true;
}

const MATRIX& Transform::GetSRT() const
{
    if (m_bSrtDirty)
    {
        m_srt       = m_tranform.ToSRT();
        m_bSrtDirty = false;
    }
    return m_srt;
}

const MATRIX& Transform::GetInvSRT() const
{
    if (m_bInvSrtDirty)
    {
        m_invSrt       = m_tranform.ToInvSRT();
        m_bInvSrtDirty = false;
    }
    return m_invSrt;
}

}   // namespace jug