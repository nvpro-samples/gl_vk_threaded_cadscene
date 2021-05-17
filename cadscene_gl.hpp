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


#pragma once

#include "cadscene.hpp"
#include <include_gl.h>
#include <nvgl/base_gl.hpp>


class GeometryMemoryGL
{
public:
  typedef size_t Index;

  struct Allocation
  {
    Index  chunkIndex;
    size_t vboOffset;
    size_t iboOffset;
  };

  struct Chunk
  {
    GLuint vboGL;
    GLuint iboGL;

    size_t vboSize;
    size_t iboSize;

    uint64_t vboADDR;
    uint64_t iboADDR;
  };

  void init(size_t vboStride, size_t maxChunk, bool bindless);
  void deinit();
  void alloc(size_t vboSize, size_t iboSize, Allocation& allocation);
  void finalize();

  size_t getVertexSize() const
  {
    size_t size = 0;
    for(size_t i = 0; i < m_chunks.size(); i++)
    {
      size += m_chunks[i].vboSize;
    }
    return size;
  }

  size_t getIndexSize() const
  {
    size_t size = 0;
    for(size_t i = 0; i < m_chunks.size(); i++)
    {
      size += m_chunks[i].iboSize;
    }
    return size;
  }

  const Chunk& getChunk(const Allocation& allocation) const { return m_chunks[allocation.chunkIndex]; }

  const Chunk& getChunk(Index index) const { return m_chunks[index]; }

  size_t getChunkCount() const { return m_chunks.size(); }

private:
  size_t m_alignment;
  size_t m_vboAlignment;
  size_t m_maxChunk;
  size_t m_maxVboChunk;
  size_t m_maxIboChunk;
  bool   m_bindless;

  std::vector<Chunk> m_chunks;

  Index getActiveIndex() { return (m_chunks.size() - 1); }

  Chunk& getActiveChunk()
  {
    assert(!m_chunks.empty());
    return m_chunks[getActiveIndex()];
  }
};

class CadSceneGL
{
public:
  struct Geometry
  {
    GeometryMemoryGL::Allocation mem;

    nvgl::BufferBinding vbo;
    nvgl::BufferBinding ibo;
  };

  struct Buffers
  {
    nvgl::Buffer materials;
    nvgl::Buffer matrices;
    nvgl::Buffer matricesOrig;
  };

  Buffers               m_buffers;
  std::vector<Geometry> m_geometry;
  GeometryMemoryGL      m_geometryMem;


  void init(const CadScene& cadscene);
  void deinit();
};
