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

#include "resources.hpp"
#include "vulkan/vulkan.h"

#include <nv_helpers/tnulled.hpp>
#include <nv_helpers_vk/base_vk.hpp>
#include <nv_helpers_vk/swapchain_vk.hpp>
#include <nv_helpers_vk/shadermodulemanager_vk.hpp>
#include <nv_helpers_vk/window_vk.hpp>

class NVPWindow;

// single set, all UBOs use dynamic offsets and are used in all stages (slowest for gpu)
#define UNIFORMS_ALLDYNAMIC         0
// single set, more accurate stage assignments, only matrix & material are dynamic (slower for gpu)
#define UNIFORMS_SPLITDYNAMIC       1
// multiple sets, only one descrset allocated, matrix & material are dynamic (fastest)
#define UNIFORMS_MULTISETSDYNAMIC   2
// multiple sets, many descrsets allocated, one for each matrix & material (slower for cpu)
#define UNIFORMS_MULTISETSSTATIC    3
// matrix & material data for rendering fits in the 256 bytes, so use that instead of descriptors
// only works on nvidia (256 bytes push data)
// slowest method, given all data is sent every frame
// !animation will show no effect!
#define UNIFORMS_PUSHCONSTANTS_RAW  4
// pass indices for matrix & material in large buffers
// GPU slower in this scene, as it has so many tiny drawcalls
// other scenes with more triangles per draw would not suffer as much
// CPU-wise faster given less data
#define UNIFORMS_PUSHCONSTANTS_INDEX  5

#define UNIFORMS_TECHNIQUE   UNIFORMS_MULTISETSDYNAMIC

#define UBOS_NUM  3

// cmdbuffers in worker threads are secondary, otherwise primary
#define USE_THREADED_SECONDARIES    1
// only use one big buffer for all geometries, otherwise individual
#define USE_SINGLE_GEOMETRY_BUFFERS 1

namespace csfthreaded {
  template <typename T>
  using TNulled = nv_helpers::TNulled<T>;
 

  class ResourcesVK : public Resources, public nv_helpers::Profiler::GPUInterface, public nv_helpers_vk::DeviceUtils
  {
  public:

    struct {
      VkRenderPass
        sceneClear,
        scenePreserve,
        sceneUI;
    } passes;

    struct {
      TNulled<VkFramebuffer>
        scene,
        sceneUI;
    } fbos;

    struct {
      TNulled<VkImage>
        scene_color,
        scene_depthstencil,
        scene_color_resolved;
    } images;

    struct {
      VkBuffer
        scene,
        anim,
#if USE_SINGLE_GEOMETRY_BUFFERS
        vbo,
        ibo,
#endif
        materials,
        matrices,
        matricesOrig;
    }buffers;

    struct {
      VkImageView
        scene_color,
        scene_color_resolved,
        scene_depthstencil;

      VkDescriptorBufferInfo
        scene,
        anim,
        materials,
        materialsFull,
        matrices,
        matricesFull,
        matricesFullOrig;
    }views;

    struct {
      TNulled<VkDeviceMemory>
        framebuffer,

        scene,
        anim,
        vbo,
        ibo,

        materials,
        matrices,
        matricesOrig;
    }mem;
    
    struct {
      nv_helpers_vk::ShaderModuleManager::ShaderModuleID
        vertex_tris,
        vertex_line,
        fragment_tris,
        fragment_line,
        compute_animation;
    } moduleids;

    struct {
      TNulled<VkShaderModule>
        vertex_tris,
        vertex_line,
        fragment_tris,
        fragment_line,
        compute_animation;
    } shaders;


    struct {
      TNulled<VkPipeline>
        tris,
        line_tris,
        line,
        compute_animation;
    } pipes;

    struct {
      VkViewport        viewport;
      VkRect2D          scissor;
    } states;

    struct BufferBinding {
      VkBuffer          buffer;
      VkDeviceSize      offset;
      VkDeviceSize      size;
    };

    struct Geometry {
      BufferBinding     vbo;
      BufferBinding     ibo;
      int               cloneIdx;
    };

    nv_helpers_vk::ShaderModuleManager            m_shaderManager;

  #if HAS_OPENGL
    nv_helpers_vk::InstanceDeviceContext          m_ctxContent;
    VkSemaphore                                   m_semImageWritten;
    VkSemaphore                                   m_semImageRead;
  #else
    const nv_helpers_vk::SwapChain*               m_swapChain;
  #endif
    const nv_helpers_vk::InstanceDeviceContext*   m_ctx;
    const nv_helpers_vk::PhysicalInfo*            m_physical;
    VkQueue                                       m_queue;
    uint32_t                                      m_queueFamily;
    
    nv_helpers_vk::RingFences                     m_ringFences;
    nv_helpers_vk::RingCmdPool                    m_ringCmdPool;

    bool                                          m_submissionWaitForRead;
    nv_helpers_vk::Submission                     m_submission;
    

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    nv_helpers_vk::DescriptorPipelineContainer<UBOS_NUM>  m_drawing;
#else
    nv_helpers_vk::DescriptorPipelineContainer<1>         m_drawing;
#endif
    nv_helpers_vk::DescriptorPipelineContainer<1>         m_anim;

    uint32_t                  m_numMatrices;
    std::vector<Geometry>     m_geometry;

    TNulled<VkQueryPool>      m_timePool;
    double                    m_timeStampFrequency;
    VkBool32                  m_timeStampsSupported;

    size_t                    m_pipeIncarnation;

    size_t                    m_fboIncarnation;
    int                       m_width;
    int                       m_height;
    int                       m_msaa;
    bool                      m_vsync;

    bool init(NVPWindow *window);
    void deinit();

    void initPipes();
    void deinitPipes();
    bool hasPipes(){
      return pipes.compute_animation != 0;
    }

    bool initPrograms(const std::string& path, const std::string& prepend);
    void reloadPrograms(const std::string& prepend);

    void updatedPrograms();
    void deinitPrograms();

    bool initFramebuffer(int width, int height, int msaa, bool vsync);
    void deinitFramebuffer();

    bool initScene(const CadScene&);
    void deinitScene();

    void synchronize();
    void initTimers(unsigned int n);
    void deinitTimers();

    void beginFrame();
    void blitFrame(const Global& global);
    void endFrame();

    void animation(const Global& global);
    void animationReset();

    

    nv_math::mat4f perspectiveProjection( float fovy, float aspect, float nearPlane, float farPlane) const;

    nv_helpers::Profiler::GPUInterface*  getTimerInterface();

    const char* TimerTypeName();
    bool    TimerAvailable(nv_helpers::Profiler::TimerIdx idx);
    void    TimerSetup(nv_helpers::Profiler::TimerIdx idx);
    unsigned long long  TimerResult(nv_helpers::Profiler::TimerIdx idxBegin, nv_helpers::Profiler::TimerIdx idxEnd);
    void    TimerEnsureSize(unsigned int slots);
    void    TimerFlush();

    ResourcesVK() {}

    static ResourcesVK* get() {
      static ResourcesVK res;

      return &res;
    }
    static bool ResourcesVK::isAvailable();


    VkRenderPass    createPass(bool clear, int msaa);
    VkRenderPass    createPassUI(int msaa);
    
    void            flushStaging(nv_helpers_vk::BasicStagingBuffer& staging);
    void            fillBuffer(nv_helpers_vk::BasicStagingBuffer& staging, VkBuffer buffer, size_t offset, size_t size, const void* data);
    VkBuffer        createAndFillBuffer(nv_helpers_vk::BasicStagingBuffer& staging, size_t size, const void* data, VkFlags usage, VkDeviceMemory &bufferMem,
                                        VkFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkCommandBuffer   createCmdBuffer(VkCommandPool pool, bool singleshot, bool primary, bool secondaryInClear) const;
    VkCommandBuffer   createTempCmdBuffer(bool primary=true, bool secondaryInClear=false);

    VkResult allocMemAndBindBuffer(VkBuffer obj, VkDeviceMemory &gpuMem, VkFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
      return DeviceUtils::allocMemAndBindBuffer(obj, m_physical->memoryProperties, gpuMem, memProps);
    }

    // submit for batched execution
    void      submissionEnqueue(VkCommandBuffer cmdbuffer)
    {
      m_submission.enqueue(cmdbuffer);
    }
    void      submissionEnqueue(uint32_t num, const VkCommandBuffer* cmdbuffers)
    {
      m_submission.enqueue(num, cmdbuffers);
    }
    // perform queue submit
    void      submissionExecute(VkFence fence=NULL, bool useImageReadWait=false, bool useImageWriteSignals=false);

    // synchronizes to queue
    void          resetTempResources(); 

    void          cmdBeginRenderPass(VkCommandBuffer cmd, bool clear, bool hasSecondary=false) const;
    void          cmdPipelineBarrier(VkCommandBuffer cmd) const;
    void          cmdDynamicState   (VkCommandBuffer cmd) const;
    void          cmdImageTransition(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspects, VkAccessFlags src, VkAccessFlags dst, VkImageLayout oldLayout, VkImageLayout newLayout) const;
    void          cmdBegin          (VkCommandBuffer cmd, bool singleshot, bool primary, bool secondaryInClear) const;
  };

}

