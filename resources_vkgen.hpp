/*-----------------------------------------------------------------------
  Copyright (c) 2014-2016, NVIDIA. All rights reserved.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Neither the name of its contributors may be used to endorse 
     or promote products derived from this software without specific
     prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/
/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */


#pragma once

#include "resources_vk.hpp"


namespace csfthreaded {

class ResourcesVKGen : public ResourcesVK
{
public:
  // To use the extension, we extend the resources for vulkan
  // by the object table. In this table we will register all the
  // binding/resources we need for rendering the scene.
  // When it comes to resources, that is the only difference to
  // unextended Vulkan.

  // The sample uses 3 pipelines, which we will need to
  // register in the object table at a user-specifed index
  // (which this enum is for).

  enum TablePipelines
  {
    TABLE_PIPE_TRIANGLES,
    TABLE_PIPE_LINES,
    TABLE_PIPE_LINES_TRIANGLES,
    NUM_TABLE_PIPES,
  };

  struct TableContent
  {
    size_t           resourceChangeID;
    VkObjectTableNVX objectTable = VK_NULL_HANDLE;
    // we keep all used registered indices around
    std::vector<uint32_t> vertexBuffers;
    std::vector<uint32_t> indexBuffers;
    std::vector<uint32_t> pushConstants;
    std::vector<uint32_t> pipelines;
    std::vector<uint32_t> matrixDescriptorSets;
    std::vector<uint32_t> materialDescriptorSets;
  };

  bool         m_generatedCommandsSupport;
  TableContent m_table;

  bool init(
#if HAS_OPENGL
      nvgl::ContextWindow* contextWindow,
#else
      nvvk::Context*   deviceInstance,
      nvvk::SwapChain* swapChain,
#endif
      nvh::Profiler* profiler);

  bool initScene(const CadScene&);
  void deinitScene();

  void reloadPrograms(const std::string& prepend);

  void initObjectTable();
  void updateObjectTablePipelines();
  void deinitObjectTable();


  ResourcesVKGen() {}

  static ResourcesVKGen* get()
  {
    static ResourcesVKGen res;

    return &res;
  }
  static bool ResourcesVKGen::isAvailable();
};
}  // namespace csfthreaded
