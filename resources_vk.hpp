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

#include "resources.hpp"
#include "nvdrawvulkanimage.h"

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

#define NV_ARRAYSIZE(arr) (sizeof(arr)/sizeof(arr[0]))

  template<class T>
  class NulledVk {
  public:
      T  m_value;
      NulledVk() : m_value(VK_NULL_HANDLE) {}

      NulledVk(T b) : m_value(b) {}
      operator T() const { return m_value; }
      operator T&() { return m_value; }
      T * operator &() { return &m_value; }
      NulledVk& operator=(T b) { m_value = b; return *this; }
  };

  bool getMemoryAllocationInfo(const VkMemoryRequirements &memReqs, VkFlags memProps, const VkPhysicalDeviceMemoryProperties  &memoryProperties, VkMemoryAllocateInfo &memInfo);
  bool appendMemoryAllocationInfo(const VkMemoryRequirements &memReqs, VkFlags memProps, const VkPhysicalDeviceMemoryProperties  &memoryProperties, VkMemoryAllocateInfo &memInfoAppended, VkDeviceSize &offset);

  struct NVkPhysical {
    VkPhysicalDevice                  physicalDevice;
    VkPhysicalDeviceMemoryProperties  memoryProperties;
    VkPhysicalDeviceProperties        properties;
    VkPhysicalDeviceFeatures          features;
    std::vector<VkQueueFamilyProperties>  queueProperties;

    void init(VkPhysicalDevice physicalDeviceIn)
    {
      physicalDevice = physicalDeviceIn;

      vkGetPhysicalDeviceProperties(physicalDevice, &properties);
      vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
      vkGetPhysicalDeviceFeatures(physicalDevice, &features);

      uint32_t count;
      vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, NULL);
      queueProperties.resize(count);
      vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, &queueProperties[0]);
    }


    bool getOptimalDepthStencilFormat( VkFormat &depthStencilFormat )
    {
      VkFormat depthStencilFormats[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
      };

      for (size_t i = 0 ; i < NV_ARRAYSIZE(depthStencilFormats); i++)
      {
        VkFormat format = depthStencilFormats[i];
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        // Format must support depth stencil attachment for optimal tiling
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
          depthStencilFormat = format;
          return true;
        }
      }

      return false;
    }
  };



  class ResourcesVK : public Resources, public nv_helpers::Profiler::GPUInterface
  {
  public:

    static const int MAX_BUFFERED_FRAMES = 3;

    struct CommandBufferEntry {
      VkCommandBuffer   buffer;
      VkCommandPool     pool;

      CommandBufferEntry(VkCommandBuffer buffer, VkCommandPool     pool) : buffer(buffer) , pool(pool) {}
    };

    class StagingBuffer {
    private:
      VkBuffer                  m_buffer;
      char*                     m_mapping;
      size_t                    m_used;
      size_t                    m_allocated;
      VkDeviceMemory            m_mem;
      VkDevice                  m_device;

    public:
      VkBuffer  getBuffer(){
        return m_buffer;
      }
      bool      needSync(size_t sz) {
        return (m_allocated && m_used+sz > m_allocated);
      }
      size_t    append(size_t sz, const void* data, ResourcesVK& res);

      void      reset(){
        m_used = 0;
      }

      void      init(VkDevice dev){
        m_device = dev;
        m_allocated = 0;
        m_used = 0;
      }

      void      deinit();

      StagingBuffer() : m_allocated(0), m_used(0), m_device(VK_NULL_HANDLE), m_buffer(VK_NULL_HANDLE), m_mem(VK_NULL_HANDLE) {}
      ~StagingBuffer() {
        deinit();
      }
    };

    struct {
      VkRenderPass
        sceneClear,
        scenePreserve;
    } passes;

    struct {
      VkFramebuffer
        scene;
    } fbos;

    struct {
      NulledVk<VkImage>
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
      NulledVk<VkDeviceMemory>
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
      nv_helpers_gl::ProgramManager::ProgramID
        draw_object_tris,
        draw_object_line,
        compute_animation;
    } programids;

    struct {
      NulledVk<VkShaderModule>
        vertex_tris,
        vertex_line,
        fragment_tris,
        fragment_line,
        compute_animation;
    } shaders;

    struct {
      NulledVk<VkPipeline>
        tris,
        line_tris,
        line,
        compute_animation;
    } pipes;

    struct {
      VkViewport        viewport;
      VkRect2D          scissor;
    } states;


    struct Geometry {
      VkBuffer          vbo;
      VkBuffer          ibo;

      VkDeviceSize      vboOffset;
      VkDeviceSize      iboOffset;

      VkDeviceSize      vboSize;
      VkDeviceSize      iboSize;

      int               cloneIdx;
    };

    VkInstance                m_instance;

    VkDevice                  m_device;
    VkPhysicalDevice          m_physicalDevice;
    NVkPhysical               m_physical;
    VkQueue                   m_queue;
    
    VkPipelineLayout          m_pipelineLayout;
    VkCommandPool             m_tempCmdPool;

    VkSemaphore               m_semImageWritten;
    VkSemaphore               m_semImageRead;

    bool                          m_submissionWaitForRead;
    std::vector<VkCommandBuffer>  m_submissions;

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
    VkDescriptorSetLayout     m_descriptorSetLayout[UBOS_NUM];
    VkDescriptorPool          m_descriptorPools[UBOS_NUM];
    VkDescriptorSet           m_descriptorSet[UBOS_NUM];
#elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    VkDescriptorSetLayout         m_descriptorSetLayout[UBOS_NUM];
    VkDescriptorPool              m_descriptorPools[UBOS_NUM];
    VkDescriptorSet               m_descriptorSet[1];
    std::vector<VkDescriptorSet>  m_descriptorSetsMatrices;
    std::vector<VkDescriptorSet>  m_descriptorSetsMaterials;
#else
    VkDescriptorSetLayout     m_descriptorSetLayout;
    VkDescriptorPool          m_descriptorPool;
    VkDescriptorSet           m_descriptorSet;
#endif

    std::vector<Geometry>     m_geometry;
    
    uint                      m_numMatrices;
    VkPipelineLayout          m_animPipelineLayout;
    VkDescriptorSetLayout     m_animDescriptorSetLayout;
    VkDescriptorPool          m_animDescriptorPool;
    VkDescriptorSet           m_animDescriptorSet;

    NulledVk<VkQueryPool>     m_timePool;
    double                    m_timeStampFrequency;
    VkBool32                  m_timeStampsSupported;

    size_t                    m_pipeIncarnation;

    size_t                    m_fboIncarnation;
    int                       m_width;
    int                       m_height;
    int                       m_msaa;

    VkFence                       m_nukemFences[MAX_BUFFERED_FRAMES];
    std::vector<VkCommandBuffer>  m_doomedCmdBuffers[MAX_BUFFERED_FRAMES];

    void init();
    void deinit(nv_helpers_gl::ProgramManager &mgr);

    void initPipes(int msaa);
    void deinitPipes();
    bool hasPipes(){
      return pipes.compute_animation != 0;
    }

    bool initPrograms   (nv_helpers_gl::ProgramManager &mgr);
    void updatedPrograms(nv_helpers_gl::ProgramManager &mgr);
    void deinitPrograms (nv_helpers_gl::ProgramManager &mgr);

    void initShaders(nv_helpers_gl::ProgramManager &mgr);
    void deinitShaders();

    bool initFramebuffer(int width, int height, int msaa);
    void deinitFramebuffer();

    bool initScene(const CadScene&);
    void deinitScene();

    void synchronize();
    void initTimers(unsigned int n);
    void deinitTimers();

    void beginFrame();
    void flushFrame();
    void endFrame();

    void animation(Global& global);
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


    VkResult        allocMemAndBindBuffer(VkBuffer obj, VkDeviceMemory &gpuMem, VkFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VkRenderPass    createPass(bool clear, int msaa);
    VkShaderModule  createShader( nv_helpers_gl::ProgramManager &mgr, nv_helpers_gl::ProgramManager::ProgramID pid, GLenum what);
    VkBuffer        createBuffer(size_t size, VkFlags usage);
    VkBuffer        createAndFillBuffer(StagingBuffer& staging, size_t size, const void* data, VkFlags usage, VkDeviceMemory &bufferMem, 
                                        VkFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDescriptorBufferInfo  createBufferInfo(VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset=0);

    VkResult      fillBuffer( StagingBuffer& staging, VkBuffer buffer, size_t offset, size_t size,  const void* data );


    VkCommandBuffer   createCmdBuffer(VkCommandPool pool, bool singleshot, bool primary, bool secondaryInClear) const;

    VkCommandBuffer   createTempCmdBuffer(bool primary=true, bool secondaryInClear=false) const
    {
      return createCmdBuffer(m_tempCmdPool, true, primary, secondaryInClear);
    }

    void      submissionEnqueue(size_t num, const VkCommandBuffer* cmdbuffers)
    {
      m_submissions.reserve( m_submissions.size() + num);
      for (size_t i = 0; i < num; i++){
        m_submissions.push_back(cmdbuffers[i]);
      }
    }

    // submit for batched execution
    void      submissionEnqueue(VkCommandBuffer cmdbuffer)
    {
      m_submissions.push_back(cmdbuffer);
    }
    // perform queue submit
    void      submissionExecute(VkFence fence=NULL, bool useImageReadWait=false, bool useImageWriteSignals=false);

    // only for temporary command buffers!
    // puts in list of current frame
    void          tempdestroyEnqueue( VkCommandBuffer cmdbuffer );
    // synchronizes to queue
    void          tempdestroyAll(); 
    // deletes past buffers, only call once per frame!
    void          tempdestroyPastFrame(int past);

    void          cmdBeginRenderPass(VkCommandBuffer cmd, bool clear, bool hasSecondary=false) const;
    void          cmdPipelineBarrier(VkCommandBuffer cmd) const;
    void          cmdDynamicState   (VkCommandBuffer cmd) const;
    void          cmdImageTransition(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspects, VkAccessFlags src, VkAccessFlags dst, VkImageLayout oldLayout, VkImageLayout newLayout) const;
    void          cmdBegin(VkCommandBuffer cmd, bool singleshot, bool primary, bool secondaryInClear) const;
  };

}

