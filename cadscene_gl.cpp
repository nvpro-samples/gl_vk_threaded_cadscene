/*
 * Copyright (c) 2017-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2017-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */



#include "cadscene_gl.hpp"
#include <inttypes.h>
#include <nvgl/glsltypes_gl.hpp>
#include <nvh/nvprint.hpp>

#include "common.h"


//////////////////////////////////////////////////////////////////////////


static size_t alignedSize(size_t sz, size_t align)
{
  return ((sz + align - 1) / (align)) * align;
}

//////////////////////////////////////////////////////////////////////////

void GeometryMemoryGL::alloc(size_t vboSize, size_t iboSize, GeometryMemoryGL::Allocation& allocation)
{
  vboSize = alignedSize(vboSize, m_vboAlignment);
  iboSize = alignedSize(iboSize, m_alignment);

  if(m_chunks.empty() || getActiveChunk().vboSize + vboSize > m_maxVboChunk || getActiveChunk().iboSize + iboSize > m_maxIboChunk)
  {
    finalize();
    Chunk chunk = {};
    m_chunks.push_back(chunk);
  }

  Chunk& chunk = getActiveChunk();

  allocation.chunkIndex = getActiveIndex();
  allocation.vboOffset  = chunk.vboSize;
  allocation.iboOffset  = chunk.iboSize;

  chunk.vboSize += vboSize;
  chunk.iboSize += iboSize;
}

void GeometryMemoryGL::finalize()
{
  if(m_chunks.empty())
  {
    return;
  }

  Chunk& chunk = getActiveChunk();
  glCreateBuffers(1, &chunk.vboGL);
  glNamedBufferStorage(chunk.vboGL, chunk.vboSize, 0, GL_DYNAMIC_STORAGE_BIT);

  glCreateBuffers(1, &chunk.iboGL);
  glNamedBufferStorage(chunk.iboGL, chunk.iboSize, 0, GL_DYNAMIC_STORAGE_BIT);


  if(m_bindless)
  {
    glGetNamedBufferParameterui64vNV(chunk.vboGL, GL_BUFFER_GPU_ADDRESS_NV, &chunk.vboADDR);
    glMakeNamedBufferResidentNV(chunk.vboGL, GL_READ_ONLY);

    glGetNamedBufferParameterui64vNV(chunk.iboGL, GL_BUFFER_GPU_ADDRESS_NV, &chunk.iboADDR);
    glMakeNamedBufferResidentNV(chunk.iboGL, GL_READ_ONLY);
  }
}

void GeometryMemoryGL::init(size_t vboStride, size_t maxChunk, bool bindless)
{
  m_alignment    = 16;
  m_vboAlignment = 16;

  m_maxVboChunk = maxChunk;
  m_maxIboChunk = maxChunk;

  m_maxChunk = maxChunk;
  m_bindless = bindless;
}

void GeometryMemoryGL::deinit()
{
  for(size_t i = 0; i < m_chunks.size(); i++)
  {
    if(m_bindless)
    {
      glMakeNamedBufferNonResidentNV(m_chunks[i].vboGL);
      glMakeNamedBufferNonResidentNV(m_chunks[i].iboGL);
    }

    glDeleteBuffers(1, &m_chunks[i].vboGL);
    glDeleteBuffers(1, &m_chunks[i].iboGL);
  }

  m_chunks.clear();
}

//////////////////////////////////////////////////////////////////////////

void CadSceneGL::init(const CadScene& cadscene)
{
  m_geometry.resize(cadscene.m_geometry.size());

  {
    m_geometryMem.init(sizeof(CadScene::Vertex), 128 * 1024 * 1024, has_GL_NV_vertex_buffer_unified_memory != 0);

    for(size_t i = 0; i < cadscene.m_geometry.size(); i++)
    {
      const CadScene::Geometry& cadgeom = cadscene.m_geometry[i];
      Geometry&                 geom    = m_geometry[i];

      m_geometryMem.alloc(cadgeom.vboSize, cadgeom.iboSize, geom.mem);
    }

    m_geometryMem.finalize();

    LOGI("Size of vertex data: %11" PRId64 "\n", uint64_t(m_geometryMem.getVertexSize()));
    LOGI("Size of index data:  %11" PRId64 "\n", uint64_t(m_geometryMem.getIndexSize()));
    LOGI("Size of data:        %11" PRId64 "\n", uint64_t(m_geometryMem.getVertexSize() + m_geometryMem.getIndexSize()));
    LOGI("Chunks:              %11d\n", uint32_t(m_geometryMem.getChunkCount()));
  }

  for(size_t i = 0; i < cadscene.m_geometry.size(); i++)
  {
    const CadScene::Geometry& cadgeom = cadscene.m_geometry[i];
    Geometry&                 geom    = m_geometry[i];

    const GeometryMemoryGL::Chunk& chunk = m_geometryMem.getChunk(geom.mem);

    glNamedBufferSubData(chunk.vboGL, geom.mem.vboOffset, cadgeom.vboSize, cadgeom.vboData);
    glNamedBufferSubData(chunk.iboGL, geom.mem.iboOffset, cadgeom.iboSize, cadgeom.iboData);

    geom.vbo = nvgl::BufferBinding(chunk.vboGL, geom.mem.vboOffset, cadgeom.vboSize, chunk.vboADDR);
    geom.ibo = nvgl::BufferBinding(chunk.iboGL, geom.mem.iboOffset, cadgeom.iboSize, chunk.iboADDR);
  }

  m_buffers.materials.create(sizeof(CadScene::Material) * cadscene.m_materials.size(), cadscene.m_materials.data(), 0, 0);
  m_buffers.matrices.create(sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size(), cadscene.m_matrices.data(), 0, 0);
  m_buffers.matricesOrig.create(sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size(), cadscene.m_matrices.data(), 0, 0);
}

void CadSceneGL::deinit()
{
  if(m_geometry.empty())
    return;

  m_buffers.matrices.destroy();
  m_buffers.matricesOrig.destroy();
  m_buffers.materials.destroy();

  m_geometryMem.deinit();

  m_geometry.clear();
}
