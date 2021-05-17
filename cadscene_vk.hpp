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

#include <nvvk/buffers_vk.hpp>
#include <nvvk/commands_vk.hpp>
#include <nvvk/stagingmemorymanager_vk.hpp>
#include <nvvk/memorymanagement_vk.hpp>

// ScopeStaging handles uploads and other staging operations.
// not efficient because it blocks/syncs operations

struct ScopeStaging
{
  ScopeStaging(nvvk::MemAllocator* memAllocator, VkQueue queue_, uint32_t queueFamily, VkDeviceSize size = 128 * 1024 * 1024)
      : staging(memAllocator, size)
      , cmdPool(memAllocator->getDevice(), queueFamily)
      , queue(queue_)
      , cmd(VK_NULL_HANDLE)
  {
    staging.setFreeUnusedOnRelease(false);
  }

  VkCommandBuffer            cmd;
  nvvk::StagingMemoryManager staging;
  nvvk::CommandPool          cmdPool;
  VkQueue                    queue;

  VkCommandBuffer getCmd()
  {
    cmd = cmd ? cmd : cmdPool.createCommandBuffer();
    return cmd;
  }

  void submit()
  {
    if(cmd)
    {
      cmdPool.submitAndWait(cmd, queue);
      cmd = VK_NULL_HANDLE;
    }
  }

  void upload(const VkDescriptorBufferInfo& binding, const void* data)
  {
    if(cmd && (data == nullptr || !staging.fitsInAllocated(binding.range)))
    {
      submit();
      staging.releaseResources();
    }
    if(data && binding.range)
    {
      staging.cmdToBuffer(getCmd(), binding.buffer, binding.offset, binding.range, data);
    }
  }
};


// GeometryMemoryVK manages vbo/ibo etc. in chunks
// allows to reduce number of bindings and be more memory efficient

struct GeometryMemoryVK
{
  typedef size_t Index;


  struct Allocation
  {
    Index        chunkIndex;
    VkDeviceSize vboOffset;
    VkDeviceSize iboOffset;
  };

  struct Chunk
  {
    VkBuffer vbo;
    VkBuffer ibo;

    VkDeviceSize vboSize;
    VkDeviceSize iboSize;

    nvvk::AllocationID vboAID;
    nvvk::AllocationID iboAID;
  };


  VkDevice                     m_device = VK_NULL_HANDLE;
  nvvk::DeviceMemoryAllocator* m_memoryAllocator;
  std::vector<Chunk>           m_chunks;

  void init(VkDevice device, VkPhysicalDevice physicalDevice, nvvk::DeviceMemoryAllocator* deviceAllocator, VkDeviceSize vboStride, VkDeviceSize maxChunk);
  void deinit();
  void alloc(VkDeviceSize vboSize, VkDeviceSize iboSize, Allocation& allocation);
  void finalize();

  const Chunk& getChunk(const Allocation& allocation) const { return m_chunks[allocation.chunkIndex]; }

  const Chunk& getChunk(Index index) const { return m_chunks[index]; }

  VkDeviceSize getVertexSize() const
  {
    VkDeviceSize size = 0;
    for(size_t i = 0; i < m_chunks.size(); i++)
    {
      size += m_chunks[i].vboSize;
    }
    return size;
  }

  VkDeviceSize getIndexSize() const
  {
    VkDeviceSize size = 0;
    for(size_t i = 0; i < m_chunks.size(); i++)
    {
      size += m_chunks[i].iboSize;
    }
    return size;
  }

  VkDeviceSize getChunkCount() const { return m_chunks.size(); }

private:
  VkDeviceSize m_alignment;
  VkDeviceSize m_vboAlignment;
  VkDeviceSize m_maxVboChunk;
  VkDeviceSize m_maxIboChunk;

  Index getActiveIndex() { return (m_chunks.size() - 1); }

  Chunk& getActiveChunk()
  {
    assert(!m_chunks.empty());
    return m_chunks[getActiveIndex()];
  }
};


class CadSceneVK
{
public:
  struct Geometry
  {
    GeometryMemoryVK::Allocation allocation;

    VkDescriptorBufferInfo vbo;
    VkDescriptorBufferInfo ibo;
  };

  struct Buffers
  {
    VkBuffer materials    = VK_NULL_HANDLE;
    VkBuffer matrices     = VK_NULL_HANDLE;
    VkBuffer matricesOrig = VK_NULL_HANDLE;

    nvvk::AllocationID materialsAID;
    nvvk::AllocationID matricesAID;
    nvvk::AllocationID matricesOrigAID;
  };

  struct Infos
  {
    VkDescriptorBufferInfo materialsSingle;
    VkDescriptorBufferInfo materials;
    VkDescriptorBufferInfo matricesSingle;
    VkDescriptorBufferInfo matrices;
    VkDescriptorBufferInfo matricesOrig;
  };


  VkDevice                    m_device = VK_NULL_HANDLE;
  nvvk::DeviceMemoryAllocator m_memAllocator;

  Buffers m_buffers;
  Infos   m_infos;

  std::vector<Geometry> m_geometry;
  GeometryMemoryVK      m_geometryMem;


  void init(const CadScene& cadscene, VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue, uint32_t queueFamilyIndex);
  void deinit();
};
