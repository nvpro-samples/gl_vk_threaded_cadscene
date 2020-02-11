/* Copyright (c) 2014-2018, NVIDIA CORPORATION. All rights reserved.
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

// single set, all UBOs use dynamic offsets and are used in all stages (slowest for gpu)
#define UNIFORMS_ALLDYNAMIC 0
// single set, more accurate stage assignments, only matrix & material are dynamic (slower for gpu)
#define UNIFORMS_SPLITDYNAMIC 1
// multiple sets, only one descrset allocated, matrix & material are dynamic (fastest)
#define UNIFORMS_MULTISETSDYNAMIC 2
// multiple sets, many descrsets allocated, one for each matrix & material (slower for cpu)
#define UNIFORMS_MULTISETSSTATIC 3
// matrix & material data for rendering fits in the 256 bytes, so use that instead of descriptors
// only works on nvidia (256 bytes push data)
// slowest method, given all data is sent every frame
// !animation will show no effect!
#define UNIFORMS_PUSHCONSTANTS_RAW 4
// pass indices for matrix & material in large buffers
// GPU slower in this scene, as it has so many tiny drawcalls
// other scenes with more triangles per draw would not suffer as much
// CPU-wise faster given less data
#define UNIFORMS_PUSHCONSTANTS_INDEX 5

#define UNIFORMS_TECHNIQUE UNIFORMS_MULTISETSDYNAMIC

#define DRAW_UBOS_NUM 3


#include "cadscene_vk.hpp"
#include "resources.hpp"

#include <nvvk/context_vk.hpp>
#include <nvvk/profiler_vk.hpp>
#include <nvvk/shadermodulemanager_vk.hpp>
#include <nvvk/swapchain_vk.hpp>

#include <nvvk/buffers_vk.hpp>
#include <nvvk/commands_vk.hpp>
#include <nvvk/descriptorsets_vk.hpp>
#include <nvvk/error_vk.hpp>
#include <nvvk/memorymanagement_vk.hpp>
#include <nvvk/renderpasses_vk.hpp>

namespace csfthreaded {

class ResourcesVK : public Resources
{
public:
  ResourcesVK() {}

  static ResourcesVK* get()
  {
    static ResourcesVK res;

    return &res;
  }
  static bool isAvailable();


  struct FrameBuffer
  {
    int  renderWidth  = 0;
    int  renderHeight = 0;
    int  supersample  = 0;
    bool useResolved  = false;
    bool vsync        = false;
    int  msaa         = 0;

    VkViewport viewport;
    VkViewport viewportUI;
    VkRect2D   scissor;
    VkRect2D   scissorUI;

    VkRenderPass passClear    = VK_NULL_HANDLE;
    VkRenderPass passPreserve = VK_NULL_HANDLE;
    VkRenderPass passUI       = VK_NULL_HANDLE;

    VkFramebuffer fboScene = VK_NULL_HANDLE;
    VkFramebuffer fboUI    = VK_NULL_HANDLE;

    VkImage imgColor         = VK_NULL_HANDLE;
    VkImage imgColorResolved = VK_NULL_HANDLE;
    VkImage imgDepthStencil  = VK_NULL_HANDLE;

    VkImageView viewColor         = VK_NULL_HANDLE;
    VkImageView viewColorResolved = VK_NULL_HANDLE;
    VkImageView viewDepthStencil  = VK_NULL_HANDLE;

    nvvk::DeviceMemoryAllocator memAllocator;
  };

  struct Common
  {
    nvvk::AllocationID     viewAID;
    VkBuffer               viewBuffer;
    VkDescriptorBufferInfo viewInfo;

    nvvk::AllocationID     animAID;
    VkBuffer               animBuffer;
    VkDescriptorBufferInfo animInfo;
  };

  struct ShaderModuleIDs
  {
    nvvk::ShaderModuleID vertex_tris;
    nvvk::ShaderModuleID vertex_line;
    nvvk::ShaderModuleID fragment_tris;
    nvvk::ShaderModuleID fragment_line;
    nvvk::ShaderModuleID compute_animation;
  };

  struct Shaders
  {
    VkShaderModule vertex_tris;
    VkShaderModule vertex_line;
    VkShaderModule fragment_tris;
    VkShaderModule fragment_line;
    VkShaderModule compute_animation;
  };

  struct Pipelines
  {
    VkPipeline tris              = VK_NULL_HANDLE;
    VkPipeline line_tris         = VK_NULL_HANDLE;
    VkPipeline line              = VK_NULL_HANDLE;
    VkPipeline compute_animation = VK_NULL_HANDLE;
  };

  bool m_withinFrame = false;

  nvvk::ShaderModuleManager m_shaderManager;
  ShaderModuleIDs           m_moduleids;
  Shaders                   m_shaders;
  Pipelines                 m_pipes;

  FrameBuffer m_framebuffer;
  Common      m_common;

#if HAS_OPENGL
  //nvvk::DeviceInstance m_ctxContent;
  VkSemaphore   m_semImageWritten;
  VkSemaphore   m_semImageRead;
  nvvk::Context m_contextInstance;
#else
  const nvvk::SwapChain*       m_swapChain;
#endif
  nvvk::Context* m_context;

  VkDevice                     m_device    = VK_NULL_HANDLE;
  VkPhysicalDevice             m_physical;
  VkQueue                      m_queue;
  uint32_t                     m_queueFamily;

  nvvk::DeviceMemoryAllocator m_memAllocator;
  nvvk::RingFences            m_ringFences;
  nvvk::RingCmdPool           m_ringCmdPool;
  nvvk::BatchSubmission       m_submission;
  bool                        m_submissionWaitForRead;

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
  nvvk::TDescriptorSetContainer<DRAW_UBOS_NUM> m_drawing;
#else
  nvvk::DescriptorSetContainer m_drawing;
#endif
  nvvk::DescriptorSetContainer m_anim;

  uint32_t   m_numMatrices;
  CadSceneVK m_scene;

  nvvk::ProfilerVK m_profilerVK;

  size_t m_pipeChangeID;
  size_t m_fboChangeID;


#if HAS_OPENGL
  bool init(nvgl::ContextWindow* window, nvh::Profiler* profiler);
#else
  bool                         init(nvvk::Context* context, nvvk::SwapChain* swapChain, nvh::Profiler* profiler);
#endif

  void deinit();

  void initPipes();
  void deinitPipes();
  bool hasPipes() { return m_pipes.compute_animation != 0; }

  bool initPrograms(const std::string& path, const std::string& prepend);
  void reloadPrograms(const std::string& prepend);

  void updatedPrograms();
  void deinitPrograms();

  bool initFramebuffer(int width, int height, int msaa, bool vsync);
  void deinitFramebuffer();

  bool initScene(const CadScene&);
  void deinitScene();

  void synchronize();

  void beginFrame();
  void blitFrame(const Global& global);
  void endFrame();

  void animation(const Global& global);
  void animationReset();

  nvmath::mat4f perspectiveProjection(float fovy, float aspect, float nearPlane, float farPlane) const override;

  //////////////////////////////////////////////////////////////////////////

  VkRenderPass createPass(bool clear, int msaa);
  VkRenderPass createPassUI(int msaa);

  VkCommandBuffer createCmdBuffer(VkCommandPool pool, bool singleshot, bool primary, bool secondaryInClear) const;
  VkCommandBuffer createTempCmdBuffer(bool primary = true, bool secondaryInClear = false);

  // submit for batched execution
  void submissionEnqueue(VkCommandBuffer cmdbuffer) { m_submission.enqueue(cmdbuffer); }
  void submissionEnqueue(uint32_t num, const VkCommandBuffer* cmdbuffers) { m_submission.enqueue(num, cmdbuffers); }
  // perform queue submit
  void submissionExecute(VkFence fence = NULL, bool useImageReadWait = false, bool useImageWriteSignals = false);

  // synchronizes to queue
  void resetTempResources();

  void cmdBeginRenderPass(VkCommandBuffer cmd, bool clear, bool hasSecondary = false) const;
  void cmdPipelineBarrier(VkCommandBuffer cmd, bool isOptimal = false) const;
  void cmdDynamicState(VkCommandBuffer cmd) const;
  void cmdImageTransition(VkCommandBuffer    cmd,
                          VkImage            img,
                          VkImageAspectFlags aspects,
                          VkAccessFlags      src,
                          VkAccessFlags      dst,
                          VkImageLayout      oldLayout,
                          VkImageLayout      newLayout) const;
  void cmdBegin(VkCommandBuffer cmd, bool singleshot, bool primary, bool secondaryInClear) const;
};

}  // namespace csfthreaded
