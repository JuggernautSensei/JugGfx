#pragma once
#include "Model.h"
#include "ModelImporter.h"

namespace jug::model_detail
{

// ===========================================
//  Utils
// ===========================================

void SelectBoneInfluences(
    Span<const uint16_t>                    _candidateIndices,
    Span<const float>                       _candidateWeights,
    ARRAY<uint16_t, kNumMaxBoneInfluences>& _outIndices,
    ARRAY<float, kNumMaxBoneInfluences>&    _outWeights);

void PostProcess(
    Model&                   _model,
    const ModelImportConfig& _config);

[[nodiscard]] Error ReadFile(
    const FilePath& _path,
    ByteBuffer&     _out);

// ===========================================
//  glTF
// ===========================================

Error ImportGLTF(
    Model&                   _model,
    MemoryView               _mem,
    const FilePath&          _baseDirOrEmpty,
    const ModelImportConfig& _config);

// ===========================================
//  ufbx
// ===========================================

Error ImportUfbxFromMemory(
    Model&                   _model,
    MemoryView               _mem,
    eModelFileFormat         _format,
    const ModelImportConfig& _config);

Error ImportUfbxFromFile(
    Model&                   _model,
    const FilePath&          _path,
    eModelFileFormat         _format,
    const ModelImportConfig& _config);

}   // namespace jug::model_detail
