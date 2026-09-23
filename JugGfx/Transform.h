#pragma once

namespace jug
{

class Transform
{
public:
    Transform() = default;

    // ===========================================
    //  Matrix
    // ===========================================

    void                        SetSRT(const MATRIX& _srt);
    [[nodiscard]] const MATRIX& GetSRT() const;
    [[nodiscard]] const MATRIX& GetInvSRT() const;

    // ===========================================
    //  Affine Transform
    // ===========================================

    void                                  SetTransform(const AFFINE_TRANSFORM& _transform);
    [[nodiscard]] const AFFINE_TRANSFORM& GetTransform() const;

    // ===========================================
    //  Translate
    // ===========================================

private:
    AFFINE_TRANSFORM m_tranform     = Identity<AFFINE_TRANSFORM>();
    mutable MATRIX   m_srt          = Identity<MATRIX>();
    mutable MATRIX   m_invSrt       = Identity<MATRIX>();
    mutable bool     m_bSrtDirty    = true;
    mutable bool     m_bInvSrtDirty = true;
};

}   // namespace jug