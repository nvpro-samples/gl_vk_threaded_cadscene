/* Copyright (c) 2017-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
