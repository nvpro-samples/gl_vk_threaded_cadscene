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

#include "resources_vk.hpp"
#include <algorithm>

extern bool vulkanInitLibrary();
extern bool vulkanCreateContext(VkDevice &device, VkPhysicalDevice& physicalDevice,  VkInstance &instance, const char* appTitle, const char* engineName);
extern void vulkanContextCleanup(VkDevice device, VkPhysicalDevice physicalDevice,  VkInstance instance);

using namespace nv_helpers_gl;

namespace csfthreaded {


  bool csfthreaded::ResourcesVK::isAvailable()
  {
    static bool result = false;
    static bool s_init = false;

    if (s_init){
      return result;
    }

    s_init = true;
    result = vulkanInitLibrary();

    return result;
  }

  //////////////////////////////////////////////////////////////////////////

  VkDevice g_vkDevice;
  VkPhysicalDevice g_vkPhysicalDevice;


  static bool  getMemoryAllocationInfo(const VkMemoryRequirements &memReqs, VkFlags memProps, const VkPhysicalDeviceMemoryProperties  &memoryProperties, VkMemoryAllocateInfo &memInfo)
  {
    memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memInfo.pNext = NULL;

    if (!memReqs.size){
      memInfo.allocationSize  = 0;
      memInfo.memoryTypeIndex = ~0;
      return true;
    }

    // Find an available memory type that satifies the requested properties.
    uint32_t memoryTypeIndex;
    for (memoryTypeIndex = 0; memoryTypeIndex < memoryProperties.memoryTypeCount; ++memoryTypeIndex) {
      if (( memReqs.memoryTypeBits & (1<<memoryTypeIndex)) &&
        ( memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memProps) == memProps) 
      {
        break;
      }
    }
    if (memoryTypeIndex >= memoryProperties.memoryTypeCount) {
      assert(0 && "memorytypeindex not found");
      return false;
    }

    memInfo.allocationSize  = memReqs.size;
    memInfo.memoryTypeIndex = memoryTypeIndex;

    return true;
  }

  static bool appendMemoryAllocationInfo(const VkMemoryRequirements &memReqs, VkFlags memProps, const VkPhysicalDeviceMemoryProperties  &memoryProperties, VkMemoryAllocateInfo &memInfoAppended, VkDeviceSize &offset)
  {
    VkMemoryAllocateInfo memInfo;
    if (!getMemoryAllocationInfo(memReqs, memProps, memoryProperties, memInfo)){
      return false;
    }
    if (memInfoAppended.allocationSize == 0){
      memInfoAppended = memInfo;
      offset  = 0;
      return true;
    }
    else if (memInfoAppended.memoryTypeIndex != memInfo.memoryTypeIndex){
      return false;
    }
    else{
      offset = (memInfoAppended.allocationSize + memReqs.alignment - 1) & ~(memReqs.alignment - 1);
      memInfoAppended.allocationSize = offset + memInfo.allocationSize;
      return true;
    }
  }

  //////////////////////////////////////////////////////////////////////////

  VkResult ResourcesVK::allocMemAndBindBuffer(VkBuffer obj, VkDeviceMemory &gpuMem, VkFlags memProps)
  {
    VkResult result;

    VkMemoryRequirements  memReqs;
    vkGetBufferMemoryRequirements(m_device, obj, &memReqs);

    VkMemoryAllocateInfo  memInfo;
    if (!getMemoryAllocationInfo(memReqs, memProps, m_physical.memoryProperties, memInfo)){
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    result = vkAllocateMemory(m_device, &memInfo, NULL, &gpuMem);
    if (result != VK_SUCCESS) {
      return result;
    }

    result = vkBindBufferMemory(m_device, obj, gpuMem, 0);
    if (result != VK_SUCCESS) {
      return result;
    }

    return VK_SUCCESS;
  }

  void ResourcesVK::init()
  {
    VkResult result;

    m_msaa = 0;
    m_fboIncarnation = 0;
    m_pipeIncarnation = 0;

    if (!vulkanCreateContext(g_vkDevice, g_vkPhysicalDevice, m_instance, "csfthreaded", "csfthreaded")){
      printf("vulkan device create failed (use debug build for more information)\n");
      exit(-1);
      return;
    }
    m_device = g_vkDevice;
    m_physical.init(g_vkPhysicalDevice);

    vkGetDeviceQueue(m_device, 0, 0, &m_queue);
    initAlignedSizes((uint32_t)m_physical.properties.limits.minUniformBufferOffsetAlignment);

    
    // fences
    for (int i = 0; i < MAX_BUFFERED_FRAMES; i++){
      VkFenceCreateInfo info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      info.flags = 0;
      result = vkCreateFence( m_device, &info, NULL, &m_nukemFences[i]);
      assert(result == VK_SUCCESS);
    }

    // temp cmd pool
    {
      VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      cmdPoolInfo.queueFamilyIndex = 0;
      result = vkCreateCommandPool(m_device, &cmdPoolInfo, NULL, &m_tempCmdPool);
      assert(result == VK_SUCCESS);
    }

    // Create the render passes
    {
      passes.sceneClear     = createPass( true,   m_msaa);
      passes.scenePreserve  = createPass( false,  m_msaa);
    }

    initTimers(nv_helpers::Profiler::START_TIMERS);

    // animation
    {
      VkDescriptorSetLayoutBinding bindings[3] = { };
      bindings[UBO_ANIM].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_ANIM].descriptorCount = 1;
      bindings[UBO_ANIM].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[UBO_ANIM].binding = UBO_ANIM;
      bindings[SSBO_MATRIXOUT].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[SSBO_MATRIXOUT].descriptorCount = 1;
      bindings[SSBO_MATRIXOUT].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[SSBO_MATRIXOUT].binding = SSBO_MATRIXOUT;
      bindings[SSBO_MATRIXORIG].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[SSBO_MATRIXORIG].descriptorCount = 1;
      bindings[SSBO_MATRIXORIG].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[SSBO_MATRIXORIG].binding = SSBO_MATRIXORIG;

      // Create descriptor layout to match the shader resources
      VkDescriptorSetLayoutCreateInfo descriptorSetEntry = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      descriptorSetEntry.bindingCount =  NV_ARRAYSIZE(bindings);
      descriptorSetEntry.pBindings = bindings;

      VkDescriptorSetLayout descriptorSetLayout;
      result = vkCreateDescriptorSetLayout(
        m_device,
        &descriptorSetEntry,
        NULL,
        &descriptorSetLayout
        );
      assert(result == VK_SUCCESS);

      m_animDescriptorSetLayout = descriptorSetLayout;

      VkDescriptorSetLayout setLayouts[] = {m_animDescriptorSetLayout};

      VkPipelineLayoutCreateInfo layoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      layoutCreateInfo.setLayoutCount = NV_ARRAYSIZE(setLayouts);
      layoutCreateInfo.pSetLayouts = setLayouts;

      VkPipelineLayout pipelineLayout;
      result = vkCreatePipelineLayout(
        m_device,
        &layoutCreateInfo,
        NULL,
        &pipelineLayout
        );
      assert(result == VK_SUCCESS);

      m_animPipelineLayout = pipelineLayout;

      // Create descriptor pool and set
      VkDescriptorPoolSize descriptorTypes[3] = {};
      descriptorTypes[UBO_ANIM].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorTypes[UBO_ANIM].descriptorCount = 1;
      descriptorTypes[SSBO_MATRIXOUT].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptorTypes[SSBO_MATRIXOUT].descriptorCount = 1;
      descriptorTypes[SSBO_MATRIXORIG].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptorTypes[SSBO_MATRIXORIG].descriptorCount = 1;

      VkDescriptorPoolCreateInfo descrPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
      descrPoolInfo.poolSizeCount = 3;
      descrPoolInfo.pPoolSizes = descriptorTypes;
      descrPoolInfo.maxSets = 1;
      descrPoolInfo.flags = 0;
      VkDescriptorPool descrPool;
      result = vkCreateDescriptorPool(g_vkDevice, &descrPoolInfo, NULL, &descrPool);
      assert(result == VK_SUCCESS);
      
      m_animDescriptorPool = descrPool;

      VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
      allocInfo.descriptorPool = descrPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts    = &descriptorSetLayout;
      
      VkDescriptorSet descriptorSet;
      result = vkAllocateDescriptorSets(g_vkDevice, &allocInfo, &descriptorSet);
      assert(result == VK_SUCCESS);

      m_animDescriptorSet = descriptorSet;
    }

    {
      VkPipelineLayoutCreateInfo layoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

  ///////////////////////////////////////////////////////////////////////////////////////////
  #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC

      VkDescriptorSetLayoutBinding bindings[UBOS_NUM] = { };
      bindings[UBO_SCENE].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_SCENE].descriptorCount = 1;
      bindings[UBO_SCENE].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_SCENE].binding = 0;
      bindings[UBO_MATRIX].descriptorType = (UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_MATRIX].descriptorCount = 1;
      bindings[UBO_MATRIX].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      bindings[UBO_MATRIX].binding = 0;
      bindings[UBO_MATERIAL].descriptorType = (UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_MATERIAL].descriptorCount = 1;
      bindings[UBO_MATERIAL].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_MATERIAL].binding = 0;

      for (int i = 0; i < UBOS_NUM; i++){
        // Create descriptor layout to match the shader resources
        VkDescriptorSetLayoutCreateInfo descriptorSetEntry = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetEntry.bindingCount = 1;
        descriptorSetEntry.pBindings = bindings + i;

        VkDescriptorSetLayout descriptorSetLayout;
        result = vkCreateDescriptorSetLayout(
          m_device,
          &descriptorSetEntry,
          NULL,
          &descriptorSetLayout
          );
        assert(result == VK_SUCCESS);

        m_descriptorSetLayout[i] = descriptorSetLayout;
      }

      VkDescriptorSetLayout setLayouts[] = {m_descriptorSetLayout[0],m_descriptorSetLayout[1],m_descriptorSetLayout[2]};
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC

    #if UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
      // "worst" case, we declare all as dynamic, and used in all stages
      // this will increase GPU time!
      VkDescriptorSetLayoutBinding bindings[UBOS_NUM] = { };
      bindings[UBO_SCENE].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      bindings[UBO_SCENE].descriptorCount = 1;
      bindings[UBO_SCENE].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_SCENE].binding = UBO_SCENE;
      bindings[UBO_MATRIX].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      bindings[UBO_MATRIX].descriptorCount = 1;
      bindings[UBO_MATRIX].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_MATRIX].binding = UBO_MATRIX;
      bindings[UBO_MATERIAL].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      bindings[UBO_MATERIAL].descriptorCount = 1;
      bindings[UBO_MATERIAL].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_MATERIAL].binding = UBO_MATERIAL;
    #elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
      // much better, based on actual use and frequencies
      VkDescriptorSetLayoutBinding bindings[UBOS_NUM] = { };
      bindings[UBO_SCENE].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_SCENE].descriptorCount = 1;
      bindings[UBO_SCENE].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_SCENE].binding = UBO_SCENE;
      bindings[UBO_MATRIX].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      bindings[UBO_MATRIX].descriptorCount = 1;
      bindings[UBO_MATRIX].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      bindings[UBO_MATRIX].binding = UBO_MATRIX;
      bindings[UBO_MATERIAL].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      bindings[UBO_MATERIAL].descriptorCount = 1;
      bindings[UBO_MATERIAL].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_MATERIAL].binding = UBO_MATERIAL;
    #endif

      // Create descriptor layout to match the shader resources
      VkDescriptorSetLayoutCreateInfo descriptorSetEntry = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      descriptorSetEntry.bindingCount =  NV_ARRAYSIZE(bindings);
      descriptorSetEntry.pBindings = bindings;

      VkDescriptorSetLayout descriptorSetLayout;
      result = vkCreateDescriptorSetLayout(
        m_device,
        &descriptorSetEntry,
        NULL,
        &descriptorSetLayout
        );
      assert(result == VK_SUCCESS);

      m_descriptorSetLayout = descriptorSetLayout;

      VkDescriptorSetLayout setLayouts[] = {m_descriptorSetLayout};
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW

      VkDescriptorSetLayoutBinding bindings[1] = { };
      bindings[UBO_SCENE].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_SCENE].descriptorCount = 1;
      bindings[UBO_SCENE].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_SCENE].binding = UBO_SCENE;

      // Create descriptor layout to match the shader resources
      VkDescriptorSetLayoutCreateInfo descriptorSetEntry = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      descriptorSetEntry.bindingCount =  NV_ARRAYSIZE(bindings);
      descriptorSetEntry.pBindings = bindings;

      VkDescriptorSetLayout descriptorSetLayout;
      result = vkCreateDescriptorSetLayout(
        m_device,
        &descriptorSetEntry,
        NULL,
        &descriptorSetLayout
        );
      assert(result == VK_SUCCESS);

      m_descriptorSetLayout = descriptorSetLayout;

      VkDescriptorSetLayout setLayouts[] = {m_descriptorSetLayout};
      VkPushConstantRange   pushRanges[2];

      assert( sizeof(ObjectData) + sizeof(MaterialData) <= m_physical.properties.limits.maxPushConstantsSize );

      // warning this will only work on NVIDIA as we support 256 Bytes push constants
      // minimum is 128 Bytes, which would not fit both data
      pushRanges[0].stageFlags  = VK_SHADER_STAGE_VERTEX_BIT;
      pushRanges[0].size        = sizeof(ObjectData);
      pushRanges[0].offset      = 0;
      pushRanges[1].stageFlags  = VK_SHADER_STAGE_FRAGMENT_BIT;
      pushRanges[1].size        = sizeof(MaterialData);
      pushRanges[1].offset      = sizeof(ObjectData);

      layoutCreateInfo.pPushConstantRanges = pushRanges;
      layoutCreateInfo.pushConstantRangeCount = NV_ARRAYSIZE(pushRanges);;

  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX

      VkDescriptorSetLayoutBinding bindings[UBOS_NUM] = { };
      bindings[UBO_SCENE].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[UBO_SCENE].descriptorCount = 1;
      bindings[UBO_SCENE].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_SCENE].binding = UBO_SCENE;
      bindings[UBO_MATRIX].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[UBO_MATRIX].descriptorCount = 1;
      bindings[UBO_MATRIX].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      bindings[UBO_MATRIX].binding = UBO_MATRIX;
      bindings[UBO_MATERIAL].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[UBO_MATERIAL].descriptorCount = 1;
      bindings[UBO_MATERIAL].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[UBO_MATERIAL].binding = UBO_MATERIAL;
      // Create descriptor layout to match the shader resources
      VkDescriptorSetLayoutCreateInfo descriptorSetEntry = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      descriptorSetEntry.bindingCount =  NV_ARRAYSIZE(bindings);
      descriptorSetEntry.pBindings = bindings;

      VkDescriptorSetLayout descriptorSetLayout;
      result = vkCreateDescriptorSetLayout(
        m_device,
        &descriptorSetEntry,
        NULL,
        &descriptorSetLayout
        );
      assert(result == VK_SUCCESS);

      m_descriptorSetLayout = descriptorSetLayout;

      VkDescriptorSetLayout setLayouts[] = {m_descriptorSetLayout};
      VkPushConstantRange   pushRanges[2];

      pushRanges[0].stageFlags  = VK_SHADER_STAGE_VERTEX_BIT;
      pushRanges[0].size        = sizeof(int);
      pushRanges[0].offset      = 0;
      pushRanges[1].stageFlags  = VK_SHADER_STAGE_FRAGMENT_BIT;
      pushRanges[1].size        = sizeof(int);
      pushRanges[1].offset      = sizeof(int);

      layoutCreateInfo.pPushConstantRanges = pushRanges;
      layoutCreateInfo.pushConstantRangeCount = NV_ARRAYSIZE(pushRanges);
  ///////////////////////////////////////////////////////////////////////////////////////////
  #endif

      layoutCreateInfo.setLayoutCount = NV_ARRAYSIZE(setLayouts);
      layoutCreateInfo.pSetLayouts = setLayouts;

      VkPipelineLayout pipelineLayout;
      result = vkCreatePipelineLayout(
        m_device,
        &layoutCreateInfo,
        NULL,
        &pipelineLayout
        );
      assert(result == VK_SUCCESS);
      
      m_pipelineLayout = pipelineLayout;
    }

    {
      // OpenGL drawing
      VkSemaphoreCreateInfo semCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
      vkCreateSemaphore(m_device, &semCreateInfo, NULL, &m_semImageRead);
      vkCreateSemaphore(m_device, &semCreateInfo, NULL, &m_semImageWritten);

      // fire read to ensure queuesubmit never waits
      glSignalVkSemaphoreNV((GLuint64)m_semImageRead);
      glFlush();
    }
  }

  void ResourcesVK::deinit(nv_helpers_gl::ProgramManager &mgr)
  {
    synchronize();
    tempdestroyAll();

    for (int i = 0; i < MAX_BUFFERED_FRAMES; i++){
      vkDestroyFence( m_device, m_nukemFences[i], NULL);
    }
    vkDestroyCommandPool(m_device, m_tempCmdPool, NULL);

    deinitScene();
    deinitFramebuffer();
    deinitPipes();
    deinitPrograms(mgr);
    deinitTimers();

    vkDestroyRenderPass(m_device, passes.sceneClear, NULL);
    vkDestroyRenderPass(m_device, passes.scenePreserve, NULL);

    vkDestroyPipelineLayout( m_device, m_pipelineLayout, NULL );
  #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    for (int i = 0; i < UBOS_NUM; i++){
      vkDestroyDescriptorSetLayout( m_device, m_descriptorSetLayout[i], NULL );
    }
  #else
    vkDestroyDescriptorSetLayout( m_device, m_descriptorSetLayout, NULL );
  #endif


    vkDestroyPipelineLayout( m_device, m_animPipelineLayout, NULL );
    vkDestroyDescriptorSetLayout( m_device, m_animDescriptorSetLayout, NULL );
    vkDestroyDescriptorPool( m_device, m_animDescriptorPool, NULL );

    vkDestroySemaphore(m_device, m_semImageRead, NULL);
    vkDestroySemaphore(m_device, m_semImageWritten, NULL);

    vulkanContextCleanup(m_device, m_gpu, m_instance);

    vkDestroyDevice(m_device, NULL);
    g_vkDevice = NULL;

    vkDestroyInstance(m_instance, NULL);
  }

  bool ResourcesVK::initPrograms( nv_helpers_gl::ProgramManager &mgr )
  {
    mgr.m_preprocessOnly = true;
    mgr.m_prepend = std::string("#extension GL_KHR_vulkan_glsl : require \n");

  ///////////////////////////////////////////////////////////////////////////////////////////
  #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    programids.draw_object_tris = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, set=(ubo), binding=0)\n#define WIREMODE 0\n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, set=(ubo), binding=0)\n#define WIREMODE 0\n",
                                                      "scene.frag.glsl"));

    programids.draw_object_line = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, set=(ubo), binding=0)\n#define WIREMODE 1\n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, set=(ubo), binding=0)\n#define WIREMODE 1\n",
                                                      "scene.frag.glsl"));
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
    programids.draw_object_tris = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 0\n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 0\n",
                                                      "scene.frag.glsl"));

    programids.draw_object_line = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 1\n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 1\n",
                                                      "scene.frag.glsl"));
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW

    assert( sizeof(ObjectData) == 128 ); // offset provided to material layout

    programids.draw_object_tris = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 0\n"
                                                      "#define MATERIAL_BINDING   layout(std140, push_constant)\n#define MATERIAL_LAYOUT layout(offset = 128)\n"
                                                      "#define MATRIX_BINDING     layout(std140, push_constant)\n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 0\n"
                                                      "#define MATERIAL_BINDING   layout(std140, push_constant)\n#define MATERIAL_LAYOUT layout(offset = 128)\n"
                                                      "#define MATRIX_BINDING     layout(std140, push_constant)\n",
                                                      "scene.frag.glsl"));

    programids.draw_object_line = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 1\n"
                                                      "#define MATERIAL_BINDING   layout(std140, push_constant)\n#define MATERIAL_LAYOUT layout(offset = 128)\n"
                                                      "#define MATRIX_BINDING     layout(std140, push_constant)\n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 1\n"
                                                      "#define MATERIAL_BINDING   layout(std140, push_constant)\n#define MATERIAL_LAYOUT layout(offset = 128)\n"
                                                      "#define MATRIX_BINDING     layout(std140, push_constant)\n",
                                                      "scene.frag.glsl"));  
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
    programids.draw_object_tris = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 0\n#define USE_INDEXING 1\n"
                                                      "#define SSBOBINDING(ubo)   layout(std430, binding=(ubo), set=0)\n"
                                                      "#define INDEXING_SETUP     layout(std140, push_constant) uniform indexSetup { int matrixIndex; int materialIndex; }; \n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 0\n#define USE_INDEXING 1\n"
                                                      "#define SSBOBINDING(ubo)   layout(std430, binding=(ubo), set=0)\n"
                                                      "#define INDEXING_SETUP     layout(std140, push_constant) uniform indexSetup { int matrixIndex; int materialIndex; }; \n",
                                                      "scene.frag.glsl"));

    programids.draw_object_line = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 1\n#define USE_INDEXING 1\n"
                                                      "#define SSBOBINDING(ubo)   layout(std430, binding=(ubo), set=0)\n"
                                                      "#define INDEXING_SETUP     layout(std140, push_constant) uniform indexSetup { int matrixIndex; int materialIndex; }; \n",
                                                      "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n#define WIREMODE 1\n#define USE_INDEXING 1\n"
                                                      "#define SSBOBINDING(ubo)   layout(std430, binding=(ubo), set=0)\n"
                                                      "#define INDEXING_SETUP     layout(std140, push_constant) uniform indexSetup { int matrixIndex; int materialIndex; }; \n",
                                                      "scene.frag.glsl"));
  ///////////////////////////////////////////////////////////////////////////////////////////
  #endif

    programids.compute_animation = mgr.createProgram(
      ProgramManager::Definition(GL_COMPUTE_SHADER,   "#define UBOBINDING(ubo)    layout(std140, binding=(ubo), set=0)\n"
                                                      "#define SSBOBINDING(ssbo)  layout(std430, binding=(ssbo))\n",
                                                      "animation.comp.glsl"));

    updatedPrograms(mgr);

    return mgr.areProgramsValid();
  }
  void ResourcesVK::initShaders(nv_helpers_gl::ProgramManager &mgr)
  {
    if (shaders.vertex_line.m_value != 0){
      deinitShaders();
    }
    shaders.vertex_tris   = createShader(mgr, programids.draw_object_tris, GL_VERTEX_SHADER);
    shaders.fragment_tris = createShader(mgr, programids.draw_object_tris, GL_FRAGMENT_SHADER);
    shaders.vertex_line   = createShader(mgr, programids.draw_object_line, GL_VERTEX_SHADER);
    shaders.fragment_line = createShader(mgr, programids.draw_object_line, GL_FRAGMENT_SHADER);
    shaders.compute_animation = createShader(mgr, programids.compute_animation, GL_COMPUTE_SHADER);
  }

  void ResourcesVK::deinitShaders()
  {
    vkDestroyShaderModule(m_device, shaders.vertex_tris, NULL);
    vkDestroyShaderModule(m_device, shaders.fragment_tris, NULL);
    vkDestroyShaderModule(m_device, shaders.vertex_line, NULL);
    vkDestroyShaderModule(m_device, shaders.fragment_line, NULL);
    vkDestroyShaderModule(m_device, shaders.compute_animation, NULL);
    shaders.vertex_line = NULL;
    shaders.fragment_tris = NULL;
    shaders.vertex_line = NULL;
    shaders.fragment_line = NULL;
    shaders.compute_animation = NULL;
  }

  void ResourcesVK::updatedPrograms( nv_helpers_gl::ProgramManager &mgr )
  {
    initShaders(mgr);
    initPipes(m_msaa);
  }

  void ResourcesVK::deinitPrograms( nv_helpers_gl::ProgramManager &mgr )
  {
    mgr.destroyProgram(programids.draw_object_line);
    mgr.destroyProgram(programids.draw_object_tris);
    mgr.destroyProgram(programids.compute_animation);

    deinitShaders();

    mgr.m_rawOnly = false;
  }

  static VkSampleCountFlagBits getSampleCountFlagBits(int msaa){
    switch(msaa){
    case 2: return VK_SAMPLE_COUNT_2_BIT;
    case 4: return VK_SAMPLE_COUNT_4_BIT;
    case 8: return VK_SAMPLE_COUNT_8_BIT;
    default:
      return VK_SAMPLE_COUNT_1_BIT;
    }
  }

  VkRenderPass ResourcesVK::createPass(bool clear, int msaa)
  {
    VkResult result;

    VkAttachmentLoadOp loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

    VkSampleCountFlagBits samplesUsed = getSampleCountFlagBits(msaa);

    // Create the render pass
    VkAttachmentDescription attachments[2] = { };
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = samplesUsed;
    attachments[0].loadOp = loadOp;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].flags = 0;

    attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
    attachments[1].samples = samplesUsed;
    attachments[1].loadOp = loadOp;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = loadOp;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].flags = 0;
    VkSubpassDescription subpass = {  };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.inputAttachmentCount = 0;
    VkAttachmentReference colorRefs[1] = { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
    subpass.colorAttachmentCount = NV_ARRAYSIZE(colorRefs);
    subpass.pColorAttachments = colorRefs;
    VkAttachmentReference depthRefs[1] = { {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} };
    subpass.pDepthStencilAttachment = depthRefs;
    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = NV_ARRAYSIZE(attachments);
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 0;

    VkRenderPass rp;
    result = vkCreateRenderPass(m_device, &rpInfo, NULL, &rp);
    assert(result == VK_SUCCESS);
    return rp;
  }

  bool ResourcesVK::initFramebuffer( int width, int height, int msaa )
  {
    VkResult result;

    m_fboIncarnation++;

    if (images.scene_color.m_value != 0){
      deinitFramebuffer();
    }

    int oldmsaa = m_msaa;

    m_width   = width;
    m_height  = height;
    m_msaa    = msaa;

    if (oldmsaa != msaa){
      vkDestroyRenderPass(m_device, passes.sceneClear,    NULL);
      vkDestroyRenderPass(m_device, passes.scenePreserve, NULL);

      // recreate the render passes with new msaa setting
      passes.sceneClear     = createPass( true,  msaa);
      passes.scenePreserve  = createPass( false, msaa);
    }


    VkSampleCountFlagBits samplesUsed = getSampleCountFlagBits(msaa);

    // color
    VkImageCreateInfo cbImageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    cbImageInfo.imageType = VK_IMAGE_TYPE_2D;
    cbImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    cbImageInfo.extent.width = width;
    cbImageInfo.extent.height = height;
    cbImageInfo.extent.depth = 1;
    cbImageInfo.mipLevels = 1;
    cbImageInfo.arrayLayers = 1;
    cbImageInfo.samples = samplesUsed;
    cbImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    cbImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    cbImageInfo.flags = 0;
    cbImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = vkCreateImage(m_device, &cbImageInfo, NULL, &images.scene_color);
    assert(result == VK_SUCCESS);

    // depth stencil
    VkImageCreateInfo dsImageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    dsImageInfo.imageType = VK_IMAGE_TYPE_2D;
    dsImageInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
    dsImageInfo.extent.width = width;
    dsImageInfo.extent.height = height;
    dsImageInfo.extent.depth = 1;
    dsImageInfo.mipLevels = 1;
    dsImageInfo.arrayLayers = 1;
    dsImageInfo.samples = samplesUsed;
    dsImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    dsImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dsImageInfo.flags = 0;
    dsImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = vkCreateImage(m_device, &dsImageInfo, NULL, &images.scene_depthstencil);
    assert(result == VK_SUCCESS);

    if (msaa) {
      // resolve image
      VkImageCreateInfo resImageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      resImageInfo.imageType = VK_IMAGE_TYPE_2D;
      resImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
      resImageInfo.extent.width = width;
      resImageInfo.extent.height = height;
      resImageInfo.extent.depth = 1;
      resImageInfo.mipLevels = 1;
      resImageInfo.arrayLayers = 1;
      resImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      resImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      resImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      resImageInfo.flags = 0;
      resImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      result = vkCreateImage(m_device, &resImageInfo, NULL, &images.scene_color_resolved);
      assert(result == VK_SUCCESS);
    }

    // handle allocation for all of them

    VkDeviceSize    cbImageOffset = 0;
    VkDeviceSize    dsImageOffset = 0;
    VkDeviceSize    resImageOffset = 0;
    VkMemoryAllocateInfo    memInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements    memReqs;
    bool valid;

    vkGetImageMemoryRequirements(m_device, images.scene_color, &memReqs);
    valid = appendMemoryAllocationInfo(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_physical.memoryProperties, memInfo, cbImageOffset);
    assert(valid);
    vkGetImageMemoryRequirements(m_device, images.scene_depthstencil, &memReqs);
    valid = appendMemoryAllocationInfo(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_physical.memoryProperties, memInfo, dsImageOffset);
    assert(valid);
    if (msaa){
      vkGetImageMemoryRequirements(m_device, images.scene_color_resolved, &memReqs);
      valid = appendMemoryAllocationInfo(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_physical.memoryProperties, memInfo, resImageOffset);
      assert(valid);
    }

    result = vkAllocateMemory(m_device, &memInfo, NULL, &mem.framebuffer);
    assert(result == VK_SUCCESS);
    vkBindImageMemory(m_device, images.scene_color,           mem.framebuffer, cbImageOffset);
    vkBindImageMemory(m_device, images.scene_depthstencil,    mem.framebuffer, dsImageOffset);
    if (msaa){
      vkBindImageMemory(m_device, images.scene_color_resolved,  mem.framebuffer, resImageOffset);
    }

    // views after allocation handling

    VkImageViewCreateInfo cbImageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    cbImageViewInfo.format = cbImageInfo.format;
    cbImageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    cbImageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    cbImageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    cbImageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    cbImageViewInfo.flags = 0;
    cbImageViewInfo.subresourceRange.levelCount = 1;
    cbImageViewInfo.subresourceRange.baseMipLevel = 0;
    cbImageViewInfo.subresourceRange.layerCount = 1;
    cbImageViewInfo.subresourceRange.baseArrayLayer = 0;
    cbImageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    cbImageViewInfo.image = images.scene_color;
    result = vkCreateImageView(m_device, &cbImageViewInfo, NULL, &views.scene_color);
    assert(result == VK_SUCCESS);

    VkImageViewCreateInfo dsImageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    dsImageViewInfo.format = dsImageInfo.format;
    dsImageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    dsImageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    dsImageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    dsImageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    dsImageViewInfo.flags = 0;
    dsImageViewInfo.subresourceRange.levelCount = 1;
    dsImageViewInfo.subresourceRange.baseMipLevel = 0;
    dsImageViewInfo.subresourceRange.layerCount = 1;
    dsImageViewInfo.subresourceRange.baseArrayLayer = 0;
    dsImageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

    dsImageViewInfo.image = images.scene_depthstencil;
    result = vkCreateImageView(m_device, &dsImageViewInfo, NULL, &views.scene_depthstencil);
    assert(result == VK_SUCCESS);
    // initial resource transitions
    {
      VkCommandBuffer cmd = createTempCmdBuffer();

      cmdImageTransition(cmd, images.scene_color, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_TRANSFER_READ_BIT, 
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      cmdImageTransition(cmd, images.scene_depthstencil, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

      if (msaa) {
        cmdImageTransition(cmd, images.scene_color_resolved, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, 
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
      }

      vkEndCommandBuffer(cmd);

      submissionEnqueue(cmd);
      tempdestroyEnqueue(cmd);

      submissionExecute();
      synchronize();
      tempdestroyAll();
    }

    {
      // Create framebuffers
      VkImageView bindInfos[2];
      bindInfos[0] = views.scene_color;
      bindInfos[1] = views.scene_depthstencil;

      VkFramebuffer fb;
      VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
      fbInfo.attachmentCount = NV_ARRAYSIZE(bindInfos);
      fbInfo.pAttachments = bindInfos;
      fbInfo.width = width;
      fbInfo.height = height;
      fbInfo.layers = 1;

      fbInfo.renderPass = passes.sceneClear;
      result = vkCreateFramebuffer(m_device, &fbInfo, NULL, &fb);
      assert(result == VK_SUCCESS);
      fbos.scene = fb;
    }

    {
      // viewport
      int vpWidth = width;
      int vpHeight = height;

      VkViewport vp;
      VkRect2D sc;
      vp.x = 0;
      vp.y = 0;
      vp.height = float(vpHeight);
      vp.width  = float(vpWidth);
      vp.minDepth = 0.0f;
      vp.maxDepth = 1.0f;

      sc.offset.x     = 0;
      sc.offset.y     = 0;
      sc.extent.width  = vpWidth;
      sc.extent.height = vpHeight;

      states.viewport = vp;
      states.scissor  = sc;
    }

    if (msaa != oldmsaa && hasPipes()){
      // reinit pipelines
      initPipes(msaa);
    }

    return true;
  }

  void ResourcesVK::deinitFramebuffer()
  {
    synchronize();

    vkDestroyImageView(m_device,  views.scene_color, NULL);
    vkDestroyImageView(m_device,  views.scene_depthstencil, NULL);
    views.scene_color = NULL;
    views.scene_depthstencil = NULL;

    vkDestroyImage(m_device,  images.scene_color, NULL);
    vkDestroyImage(m_device,  images.scene_depthstencil, NULL);
    images.scene_color = NULL;
    images.scene_depthstencil = NULL;

    if (images.scene_color_resolved){
      vkDestroyImage(m_device,  images.scene_color_resolved, NULL);
      images.scene_color_resolved = NULL;
    }

    vkFreeMemory(m_device, mem.framebuffer, NULL);

    vkDestroyFramebuffer(m_device, fbos.scene, NULL);
    fbos.scene = NULL;
  }

  VkShaderModule ResourcesVK::createShader( nv_helpers_gl::ProgramManager &mgr, nv_helpers_gl::ProgramManager::ProgramID pid, GLenum what)
  {
    const nv_helpers_gl::ProgramManager::Program& prog = mgr.getProgram(pid);
    
    for (size_t i = 0; i < prog.definitions.size(); i++){
      if (prog.definitions[i].type == what){
        VkResult result;
        VkShaderModuleCreateInfo shaderModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        shaderModuleInfo.codeSize = prog.definitions[i].preprocessed.size();
        shaderModuleInfo.pCode    = (const uint32_t*)prog.definitions[i].preprocessed.c_str();

        // we are using VK_NV_glsl_shader
        // TODO replace this by SPIR-V generating library in future

        VkShaderModule shaderModule;
        result = vkCreateShaderModule(m_device, &shaderModuleInfo, NULL, &shaderModule);
        assert(result == VK_SUCCESS);
        if (result != VK_SUCCESS) {
          return NULL;
        }
        return shaderModule;
      }
    }
    return NULL;
  }

  void ResourcesVK::initPipes(int msaa)
  {
    VkResult result;

    m_pipeIncarnation++;

    if (hasPipes()){
      deinitPipes();
    }

    VkSampleCountFlagBits samplesUsed = getSampleCountFlagBits(msaa);
    
    // Create static state info for the pipeline.
    VkVertexInputBindingDescription vertexBinding;
    vertexBinding.stride = sizeof(CadScene::Vertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBinding.binding  = 0;
    VkVertexInputAttributeDescription attributes[2] = { };
    attributes[0].location = VERTEX_POS;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(CadScene::Vertex, position);
    attributes[1].location = VERTEX_NORMAL;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(CadScene::Vertex, normal);
    VkPipelineVertexInputStateCreateInfo viStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    viStateInfo.vertexBindingDescriptionCount = 1;
    viStateInfo.pVertexBindingDescriptions = &vertexBinding;
    viStateInfo.vertexAttributeDescriptionCount = NV_ARRAYSIZE(attributes);
    viStateInfo.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo iaStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    iaStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    iaStateInfo.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo vpStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpStateInfo.viewportCount = 1;
    vpStateInfo.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    
    rsStateInfo.rasterizerDiscardEnable = VK_FALSE;
    rsStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rsStateInfo.cullMode = VK_CULL_MODE_NONE;
    rsStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsStateInfo.depthClampEnable = VK_TRUE;
    rsStateInfo.depthBiasEnable = VK_FALSE;
    rsStateInfo.depthBiasConstantFactor = 0.0;
    rsStateInfo.depthBiasSlopeFactor = 0.0f;
    rsStateInfo.depthBiasClamp = 0.0f;
    rsStateInfo.lineWidth = 1.0f;

    // create a color blend attachment that does blending
    VkPipelineColorBlendAttachmentState cbAttachmentState[1] = { };
    cbAttachmentState[0].blendEnable = VK_FALSE;
    cbAttachmentState[0].colorWriteMask = ~0;

    // create a color blend state that does blending
    VkPipelineColorBlendStateCreateInfo cbStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cbStateInfo.logicOpEnable = VK_FALSE;
    cbStateInfo.attachmentCount = 1;
    cbStateInfo.pAttachments = cbAttachmentState;
    cbStateInfo.blendConstants[0] = 1.0f;
    cbStateInfo.blendConstants[1] = 1.0f;
    cbStateInfo.blendConstants[2] = 1.0f;
    cbStateInfo.blendConstants[3] = 1.0f;

    VkPipelineDepthStencilStateCreateInfo dsStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dsStateInfo.depthTestEnable = VK_TRUE;
    dsStateInfo.depthWriteEnable = VK_TRUE;
    dsStateInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    dsStateInfo.depthBoundsTestEnable = VK_FALSE;
    dsStateInfo.stencilTestEnable = VK_FALSE;
    dsStateInfo.minDepthBounds = 0.0f;
    dsStateInfo.maxDepthBounds = 1.0f;

    VkPipelineMultisampleStateCreateInfo msStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msStateInfo.rasterizationSamples = samplesUsed;
    msStateInfo.sampleShadingEnable = VK_FALSE;
    msStateInfo.minSampleShading = 1.0f;
    uint32_t sampleMask = 0xFFFFFFFF;
    msStateInfo.pSampleMask = &sampleMask;

    VkPipelineTessellationStateCreateInfo tessStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
    tessStateInfo.patchControlPoints = 0;

    VkPipelineDynamicStateCreateInfo dynStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    dynStateInfo.dynamicStateCount = NV_ARRAYSIZE(dynStates);
    dynStateInfo.pDynamicStates = dynStates;

    {
      VkPipeline pipeline;
      VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
      pipelineInfo.layout = m_pipelineLayout;
      pipelineInfo.pVertexInputState = &viStateInfo;
      pipelineInfo.pInputAssemblyState = &iaStateInfo;
      pipelineInfo.pViewportState = &vpStateInfo;
      pipelineInfo.pRasterizationState = &rsStateInfo;
      pipelineInfo.pColorBlendState = &cbStateInfo;
      pipelineInfo.pDepthStencilState = &dsStateInfo;
      pipelineInfo.pMultisampleState = &msStateInfo;
      pipelineInfo.pTessellationState = &tessStateInfo;
      pipelineInfo.pDynamicState = &dynStateInfo;

      pipelineInfo.renderPass = passes.scenePreserve;
      pipelineInfo.subpass    = 0;

      VkPipelineShaderStageCreateInfo stages[2];
      memset(stages, 0, sizeof stages);
      pipelineInfo.stageCount = 2;
      pipelineInfo.pStages = stages;

      VkPipelineShaderStageCreateInfo& vsStageInfo = stages[0];
      vsStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      vpStateInfo.pNext = NULL;
      vsStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
      vsStageInfo.pName = "main";
      vsStageInfo.module = NULL;

      VkPipelineShaderStageCreateInfo& fsStageInfo = stages[1];
      fsStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      fsStageInfo.pNext = NULL;
      fsStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      fsStageInfo.pName = "main";
      fsStageInfo.module = NULL;

      vsStageInfo.module = shaders.vertex_tris;
      fsStageInfo.module = shaders.fragment_tris;;
      result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
      assert(result == VK_SUCCESS);
      pipes.tris = pipeline;

      rsStateInfo.depthBiasEnable = VK_TRUE;
      rsStateInfo.depthBiasConstantFactor = 1.0f;
      rsStateInfo.depthBiasSlopeFactor = 1.0;

      vsStageInfo.module = shaders.vertex_tris;;
      fsStageInfo.module = shaders.fragment_tris;;
      result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
      assert(result == VK_SUCCESS);
      pipes.line_tris = pipeline;

      iaStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      vsStageInfo.module = shaders.vertex_line;
      fsStageInfo.module = shaders.fragment_line;
      result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
      assert(result == VK_SUCCESS);
      pipes.line = pipeline;
    }
    
    //////////////////////////////////////////////////////////////////////////

    {
      VkPipeline pipeline;
      VkComputePipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
      VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
      stageInfo.pName = "main";
      stageInfo.module = shaders.compute_animation;

      pipelineInfo.layout = m_animPipelineLayout;
      pipelineInfo.stage  = stageInfo;
      result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
      assert(result == VK_SUCCESS);
      pipes.compute_animation = pipeline;
    }

  }


  nv_helpers::Profiler::GPUInterface* ResourcesVK::getTimerInterface()
  {
#if 1
    if (m_timeStampsSupported) return this;
#endif
    return 0;
  }

  const char* ResourcesVK::TimerTypeName()
  {
    return "VK ";
  }

  bool ResourcesVK::TimerAvailable(nv_helpers::Profiler::TimerIdx idx)
  {
    return true; // let's hope 8 frames are enough to avoid syncs for now
  }

  void ResourcesVK::TimerSetup(nv_helpers::Profiler::TimerIdx idx)
  {
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;

    VkCommandBuffer timerCmd = createTempCmdBuffer();

    vkCmdResetQueryPool(timerCmd, m_timePool, idx, 1); // not ideal to do this per query
    vkCmdWriteTimestamp(timerCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, m_timePool, idx);

    result = vkEndCommandBuffer(timerCmd);
    assert(result == VK_SUCCESS);
    
    submissionEnqueue(timerCmd);
    tempdestroyEnqueue(timerCmd);
  }

  unsigned long long ResourcesVK::TimerResult(nv_helpers::Profiler::TimerIdx idxBegin, nv_helpers::Profiler::TimerIdx idxEnd)
  {
    uint64_t end = 0;
    uint64_t begin = 0;
    vkGetQueryPoolResults(m_device, m_timePool, idxEnd,   1, sizeof(uint64_t), &end,   0, VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT);
    vkGetQueryPoolResults(m_device, m_timePool, idxBegin, 1, sizeof(uint64_t), &begin, 0, VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT);

    return uint64_t(double(end - begin) * m_timeStampFrequency);
  }

  void ResourcesVK::TimerEnsureSize(unsigned int slots)
  {
    
  }


  void ResourcesVK::TimerFlush()
  {
    // execute what we have gathered so far
    submissionExecute(NULL,true,false);
  }

  void ResourcesVK::initTimers(unsigned int numEntries)
  {
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    m_timeStampsSupported = m_physical.queueProperties[0].timestampValidBits;

    if (m_timeStampsSupported)
    {
      m_timeStampFrequency = double(m_physical.properties.limits.timestampPeriod);
    }
    else
    {
      return;
    }

    if (m_timePool){
      deinitTimers();
    }

    VkQueryPoolCreateInfo queryInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    queryInfo.queryCount = numEntries;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    result = vkCreateQueryPool(m_device, &queryInfo, NULL, &m_timePool);
  }

  void ResourcesVK::deinitTimers()
  {
    if (!m_timeStampsSupported) return;

    vkDestroyQueryPool(m_device, m_timePool, NULL);
    m_timePool = NULL;
  }



  void ResourcesVK::deinitPipes()
  {
    vkDestroyPipeline(m_device, pipes.line, NULL);
    vkDestroyPipeline(m_device, pipes.line_tris, NULL);
    vkDestroyPipeline(m_device, pipes.tris, NULL);
    vkDestroyPipeline(m_device, pipes.compute_animation, NULL);
    pipes.line = NULL;
    pipes.line_tris = NULL;
    pipes.tris = NULL;
    pipes.compute_animation = NULL;
  }

  void ResourcesVK::cmdDynamicState(VkCommandBuffer cmd) const
  {
    vkCmdSetViewport(cmd,0,1,&states.viewport);
    vkCmdSetScissor (cmd,0,1,&states.scissor);
  }

  void ResourcesVK::cmdBeginRenderPass( VkCommandBuffer cmd, bool clear, bool hasSecondary ) const
  {
    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    renderPassBeginInfo.renderPass  = clear ? passes.sceneClear : passes.scenePreserve;
    renderPassBeginInfo.framebuffer = fbos.scene;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width  = m_width;
    renderPassBeginInfo.renderArea.extent.height = m_height;
    renderPassBeginInfo.clearValueCount = 2;
    VkClearValue clearValues[2];
    clearValues[0].color.float32[0] = 0.2f;
    clearValues[0].color.float32[1] = 0.2f;
    clearValues[0].color.float32[2] = 0.2f;
    clearValues[0].color.float32[3] = 0.0f;
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;
    renderPassBeginInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, hasSecondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);

  }

  void ResourcesVK::cmdPipelineBarrier(VkCommandBuffer cmd) const
  {
    // color transition
    {
      VkImageSubresourceRange colorRange;
      memset(&colorRange,0,sizeof(colorRange));
      colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      colorRange.baseMipLevel = 0;
      colorRange.levelCount = VK_REMAINING_MIP_LEVELS;
      colorRange.baseArrayLayer = 0;
      colorRange.layerCount = 1;

      VkImageMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      memBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      memBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      memBarrier.image = images.scene_color;
      memBarrier.subresourceRange = colorRange;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_FALSE, 
        0, NULL, 0, NULL, 1, &memBarrier);
    }

    // Prepare the depth+stencil for reading.

    {
      VkImageSubresourceRange depthStencilRange;
      memset(&depthStencilRange,0,sizeof(depthStencilRange));
      depthStencilRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
      depthStencilRange.baseMipLevel = 0;
      depthStencilRange.levelCount = VK_REMAINING_MIP_LEVELS;
      depthStencilRange.baseArrayLayer = 0;
      depthStencilRange.layerCount = 1;

      VkImageMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      memBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      memBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      memBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      memBarrier.image = images.scene_depthstencil;
      memBarrier.subresourceRange = depthStencilRange;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_FALSE, 
        0, NULL, 0, NULL, 1, &memBarrier);
    }
  }


  void ResourcesVK::cmdImageTransition( VkCommandBuffer cmd, 
    VkImage img,
    VkImageAspectFlags aspects,
    VkAccessFlags src,
    VkAccessFlags dst,
    VkImageLayout oldLayout,
    VkImageLayout newLayout) const
  {
    VkImageSubresourceRange range;
    memset(&range,0,sizeof(range));
    range.aspectMask = aspects;
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkImageMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    memBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    memBarrier.dstAccessMask = dst;
    memBarrier.srcAccessMask = src;
    memBarrier.oldLayout = oldLayout;
    memBarrier.newLayout = newLayout;
    memBarrier.image = img;
    memBarrier.subresourceRange = range;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_FALSE, 
      0, NULL, 0, NULL, 1, &memBarrier);
  }

  VkCommandBuffer ResourcesVK::createCmdBuffer(VkCommandPool pool, bool singleshot, bool primary, bool secondaryInClear) const
  {
    VkResult result;
    bool secondary = !primary;

    // Create the command buffer.
    VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdInfo.commandPool = pool;
    assert(cmdInfo.commandPool);
    cmdInfo.level = primary ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmdInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    result = vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd);
    assert(result == VK_SUCCESS);

    // Record the commands.
    VkCommandBufferInheritanceInfo inheritInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
    if (secondary){
      inheritInfo.renderPass  = secondaryInClear ? passes.sceneClear : passes.scenePreserve;
      inheritInfo.framebuffer = fbos.scene;
    }

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    // the sample is resubmitting re-use commandbuffers to the queue while they may still be executed by GPU
    // we only use fences to prevent deleting commandbuffers that are still in flight
    beginInfo.flags = singleshot ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    // the sample's secondary buffers always are called within passes as they contain drawcalls
    beginInfo.flags |= secondary ? VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : 0;
    beginInfo.pInheritanceInfo = &inheritInfo;
    
    result = vkBeginCommandBuffer(cmd, &beginInfo);
    assert(result == VK_SUCCESS);

    return cmd;
  }

  void ResourcesVK::beginFrame()
  {
    m_submissionWaitForRead = true;
  }

  void ResourcesVK::flushFrame()
  {
    int current  = m_frame % MAX_BUFFERED_FRAMES;
    submissionExecute(m_nukemFences[current], true, true);

#if 0
    synchronize();
#endif
  }

  void ResourcesVK::endFrame()
  {
    int current  = m_frame % MAX_BUFFERED_FRAMES;
    int past     = (m_frame + 1) % MAX_BUFFERED_FRAMES;

    if (m_frame < MAX_BUFFERED_FRAMES){
      return;
    }
    tempdestroyPastFrame(past);
  }

  void ResourcesVK::tempdestroyEnqueue( VkCommandBuffer cmdbuffer )
  {
    int current = m_frame % MAX_BUFFERED_FRAMES;
    m_doomedCmdBuffers[current].push_back( cmdbuffer );
  }
  void ResourcesVK::tempdestroyAll()
  {
    synchronize();
    for (int f = 0; f < MAX_BUFFERED_FRAMES; f++){
      if (!m_doomedCmdBuffers[f].empty()){
        vkFreeCommandBuffers( m_device, m_tempCmdPool, (uint32_t)m_doomedCmdBuffers[f].size(), &m_doomedCmdBuffers[f][0]);
        m_doomedCmdBuffers[f].clear();
      }
    }
    vkResetFences(m_device, MAX_BUFFERED_FRAMES, m_nukemFences);
    m_frame = 0;
  }
  void ResourcesVK::tempdestroyPastFrame(int past)
  {
#if 0
    VkResult result = vkGetFenceStatus(m_device, m_nukemFences[past]);
    if (result != VK_SUCCESS){
      printf("WAIT FOR FENCE\n");
    }
#endif
    vkWaitForFences(m_device, 1, &m_nukemFences[past], true, ~0ULL);
    vkResetFences  (m_device, 1, &m_nukemFences[past]);
    
    // not exactly efficient, should do per pool processing
    vkFreeCommandBuffers( m_device, m_tempCmdPool, (uint32_t)m_doomedCmdBuffers[past].size(), m_doomedCmdBuffers[past].data());
    m_doomedCmdBuffers[past].clear();
  }

  void ResourcesVK::submissionExecute(VkFence fence, bool useImageReadSignals, bool useImageWriteSignals)
  {
    if (!m_submissions.empty() || fence || (useImageReadSignals && m_submissionWaitForRead) || useImageWriteSignals){
      VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submitInfo.commandBufferCount = m_submissions.size();
      submitInfo.pCommandBuffers = m_submissions.data();

      if (useImageReadSignals && m_submissionWaitForRead){
        VkPipelineStageFlags  flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores   = &m_semImageRead;
        submitInfo.pWaitDstStageMask = &flags;
        m_submissionWaitForRead = false;
      }
      if (useImageWriteSignals){
        submitInfo.pSignalSemaphores = &m_semImageWritten;
        submitInfo.signalSemaphoreCount = 1;
      }

      vkQueueSubmit(m_queue, 1, &submitInfo, fence);

      m_submissions.clear();
    }
  }

  size_t ResourcesVK::StagingBuffer::append( size_t sz, const void* data, ResourcesVK& res )
  {
    if (m_used+sz > m_allocated){
      size_t oldsize = m_allocated;

      if (m_allocated){
        deinit();
      }

      m_allocated = std::max(sz, std::min(oldsize * 2, size_t(1024*1024*32)));

      VkResult result;
      // Create staging buffer
      VkBufferCreateInfo bufferStageInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufferStageInfo.size  = m_allocated;
      bufferStageInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      bufferStageInfo.flags = 0;

      result = vkCreateBuffer(m_device, &bufferStageInfo, NULL, &m_buffer);
      assert(result == VK_SUCCESS);

      result = res.allocMemAndBindBuffer(m_buffer, m_mem, (VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
      assert(result == VK_SUCCESS);

      result = vkMapMemory(m_device, m_mem, 0, m_allocated, 0, (void**)&m_mapping);
      assert(result == VK_SUCCESS);

      m_used = 0;
    }

    size_t offset = m_used;
    memcpy(&m_mapping[offset], data, sz);
    m_used += sz;

    return offset;
  }

  void ResourcesVK::StagingBuffer::deinit()
  {
    if (m_allocated){
      vkUnmapMemory(m_device,m_mem);
      vkDestroyBuffer(m_device, m_buffer, NULL);
      vkFreeMemory(m_device, m_mem, NULL);
      m_buffer = NULL;
      m_mapping = NULL;
      m_allocated = 0;
    }
  }

  VkResult ResourcesVK::fillBuffer( StagingBuffer& staging, VkBuffer buffer, size_t offset, size_t size,  const void* data )
  {
    if (staging.needSync(size)){
      submissionExecute();
      synchronize();
      tempdestroyAll();
    }

    size_t offsetsrc = staging.append( size, data, *this);

    VkCommandBuffer cmd = createTempCmdBuffer();

    VkBufferCopy copy;
    copy.size   = size;
    copy.dstOffset = offset;
    copy.srcOffset  = offsetsrc;
    vkCmdCopyBuffer(cmd, staging.getBuffer(), buffer, 1, &copy);

    VkResult result;
    result = vkEndCommandBuffer(cmd);
    assert(result == VK_SUCCESS);

    submissionEnqueue(cmd);
    tempdestroyEnqueue(cmd);
    
    return result;
  }

  VkBuffer ResourcesVK::createBuffer(size_t size, VkFlags usage)
  {
    VkResult result;
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size  = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.flags = 0;

    result = vkCreateBuffer(m_device, &bufferInfo, NULL, &buffer);
    assert(result == VK_SUCCESS);

    return buffer;
  }

  VkBuffer ResourcesVK::createAndFillBuffer(StagingBuffer& staging, size_t size, const void* data, VkFlags usage, VkDeviceMemory &bufferMem, VkFlags memProps)
  {
    VkResult result;
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size  = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.flags = 0;

    result = vkCreateBuffer(m_device, &bufferInfo, NULL, &buffer);
    assert(result == VK_SUCCESS);
    
    result = allocMemAndBindBuffer(buffer, bufferMem, memProps);
    assert(result == VK_SUCCESS);

    if (data){
      result = fillBuffer(staging, buffer, 0, size, data);
    }

    return buffer;
  }


  VkDescriptorBufferInfo ResourcesVK::createBufferInfo( VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset )
  {
    VkDescriptorBufferInfo info;
    info.buffer = buffer;
    info.offset = offset;
    info.range  = size;

    return info;
  }

  bool ResourcesVK::initScene( const CadScene& cadscene)
  {
    m_numMatrices = uint(cadscene.m_matrices.size());

    m_geometry.resize( cadscene.m_geometry.size() );

    if (m_geometry.empty()) return true;

    VkResult result;

    StagingBuffer staging;
    staging.init(m_device);


  #if USE_SINGLE_GEOMETRY_BUFFERS
    size_t vbosizeAligned = 0;
    size_t ibosizeAligned = 0;
  #else
    VkMemoryAllocateInfo    memInfoVbo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements    memReqsVbo;
    VkMemoryAllocateInfo    memInfoIbo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements    memReqsIbo;
    bool valid;
  #endif
    for (size_t i = 0; i < cadscene.m_geometry.size(); i++){
      const CadScene::Geometry & cgeom = cadscene.m_geometry[i];
      Geometry&                   geom = m_geometry[i];

      if (cgeom.cloneIdx < 0) {
        geom.vboSize = cgeom.vboSize;
        geom.iboSize = cgeom.iboSize;

      #if USE_SINGLE_GEOMETRY_BUFFERS
        geom.vboOffset = vbosizeAligned;
        geom.iboOffset = ibosizeAligned;

        size_t vboAlignment = 16;
        size_t iboAlignment = 4;
        vbosizeAligned += ((cgeom.vboSize+vboAlignment-1)/vboAlignment)*vboAlignment;
        ibosizeAligned += ((cgeom.iboSize+iboAlignment-1)/iboAlignment)*iboAlignment;
      #else
        geom.vbo = createBuffer(geom.vboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        vkGetBufferMemoryRequirements(m_device, geom.vbo, &memReqsVbo);
        valid = appendMemoryAllocationInfo(memReqsVbo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_physical.memoryProperties, memInfoVbo, geom.vboOffset);
        assert(valid);

        geom.ibo = createBuffer(geom.iboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        vkGetBufferMemoryRequirements(m_device, geom.ibo, &memReqsIbo);
        valid = appendMemoryAllocationInfo(memReqsIbo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_physical.memoryProperties, memInfoIbo, geom.iboOffset);
        assert(valid);
      #endif
      }
    }

  #if USE_SINGLE_GEOMETRY_BUFFERS
    // packs everything tightly in big buffes with a single allocation each.
    buffers.vbo = createAndFillBuffer(staging,vbosizeAligned, NULL, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mem.vbo);
    buffers.ibo = createAndFillBuffer(staging,ibosizeAligned, NULL, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  mem.ibo);
  #else
    result = vkAllocateMemory(m_device, &memInfoVbo, NULL, &mem.vbo);
    assert(result == VK_SUCCESS);
    result = vkAllocateMemory(m_device, &memInfoIbo, NULL, &mem.ibo);
    assert(result == VK_SUCCESS);
  #endif

    for (size_t i = 0; i < cadscene.m_geometry.size(); i++){
      const CadScene::Geometry & cgeom = cadscene.m_geometry[i];
      Geometry&                   geom = m_geometry[i];

      if (cgeom.cloneIdx < 0) {
      #if USE_SINGLE_GEOMETRY_BUFFERS
        geom.vbo = buffers.vbo;
        geom.ibo = buffers.ibo;
        fillBuffer(staging,buffers.vbo, geom.vboOffset, geom.vboSize, &cgeom.vertices[0]);
        fillBuffer(staging,buffers.ibo, geom.iboOffset, geom.iboSize, &cgeom.indices[0]);
      #else
        result = vkBindBufferMemory(m_device, geom.vbo, mem.vbo, geom.vboOffset);
        assert(result == VK_SUCCESS);
        result = vkBindBufferMemory(m_device, geom.ibo, mem.ibo, geom.iboOffset);
        assert(result == VK_SUCCESS);
        geom.vboOffset = 0;
        geom.iboOffset = 0;

        fillBuffer(staging, geom.vbo, 0, geom.vboSize, &cgeom.vertices[0]);
        fillBuffer(staging, geom.ibo, 0, geom.iboSize, &cgeom.indices[0]);
      #endif
      }
      else{
        geom = m_geometry[cgeom.cloneIdx];
      }
      geom.cloneIdx = cgeom.cloneIdx;
    }

    buffers.scene       = createAndFillBuffer(staging,sizeof(SceneData), NULL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.scene);
    views.scene         = createBufferInfo(buffers.scene, sizeof(SceneData));

    buffers.anim        = createAndFillBuffer(staging,sizeof(AnimationData), NULL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.anim);
    views.anim          = createBufferInfo(buffers.anim, sizeof(AnimationData));

    // FIXME, atm sizes must match, but cleaner solution is creating a temp strided memory block for material & matrix
    assert(sizeof(CadScene::MatrixNode) == m_alignedMatrixSize);
    assert(sizeof(CadScene::Material)   == m_alignedMaterialSize);

    buffers.materials   = createAndFillBuffer(staging,cadscene.m_materials.size() * sizeof(CadScene::Material), &cadscene.m_materials[0], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.materials);
    views.materials     = createBufferInfo(buffers.materials, sizeof(CadScene::Material));
    views.materialsFull = createBufferInfo(buffers.materials, sizeof(CadScene::Material) * cadscene.m_materials.size());

    buffers.matrices    = createAndFillBuffer (staging,cadscene.m_matrices.size() * sizeof(CadScene::MatrixNode), &cadscene.m_matrices[0], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.matrices);
    views.matrices      = createBufferInfo(buffers.matrices, sizeof(CadScene::MatrixNode));
    views.matricesFull  = createBufferInfo(buffers.matrices, sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size());

    buffers.matricesOrig   = createAndFillBuffer (staging,cadscene.m_matrices.size() * sizeof(CadScene::MatrixNode), &cadscene.m_matrices[0], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, mem.matricesOrig);
    views.matricesFullOrig = createBufferInfo(buffers.matricesOrig, sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size());

    submissionExecute();
    synchronize();
    tempdestroyAll();
    staging.deinit();

    {
  //////////////////////////////////////////////////////////////////////////
  // Allocation phase
  //////////////////////////////////////////////////////////////////////////
  #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
      // multiple descriptorsets
      VkDescriptorPoolSize type_counts[UBOS_NUM];
      type_counts[UBO_SCENE].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      type_counts[UBO_SCENE].descriptorCount = 1;
      type_counts[UBO_MATRIX].type = (UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      type_counts[UBO_MATRIX].descriptorCount = 1;
      type_counts[UBO_MATERIAL].type = (UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      type_counts[UBO_MATERIAL].descriptorCount = 1;

      // create multiple pools for simplicity (could do a single if all have same type)
      uint32_t maxcounts[UBOS_NUM];
      VkDescriptorSet* setstores[UBOS_NUM];

    #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
      maxcounts[UBO_SCENE]    = 1;
      maxcounts[UBO_MATRIX]   = 1;
      maxcounts[UBO_MATERIAL] = 1;

      setstores[UBO_SCENE]     = &m_descriptorSet[UBO_SCENE];
      setstores[UBO_MATRIX]    = &m_descriptorSet[UBO_MATRIX];
      setstores[UBO_MATERIAL]  = &m_descriptorSet[UBO_MATERIAL];
    #else
      maxcounts[UBO_SCENE]    = 1;
      maxcounts[UBO_MATRIX]   = cadscene.m_matrices.size();
      maxcounts[UBO_MATERIAL] = cadscene.m_materials.size();

      m_descriptorSetsMaterials.resize( cadscene.m_materials.size() );
      m_descriptorSetsMatrices. resize( cadscene.m_matrices.size() );

      setstores[UBO_SCENE]     = &m_descriptorSet[UBO_SCENE];
      setstores[UBO_MATRIX]    = &m_descriptorSetsMatrices[0];
      setstores[UBO_MATERIAL]  = &m_descriptorSetsMaterials[0];
    #endif

      for (int i = 0; i < UBOS_NUM; i++){
        VkDescriptorPool descrPool;
        VkDescriptorPoolCreateInfo descrPoolInfo = { };
        descrPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descrPoolInfo.pNext = NULL;
        descrPoolInfo.maxSets =  maxcounts[i];
        descrPoolInfo.poolSizeCount = 1;
        descrPoolInfo.pPoolSizes = &type_counts[i];

        // scene pool
        result = vkCreateDescriptorPool(m_device, &descrPoolInfo, NULL, &descrPool);
        assert(result == VK_SUCCESS);
        m_descriptorPools[i] = descrPool;

        for (uint32_t n = 0; n < maxcounts[i]; n++){

          VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
          allocInfo.descriptorPool = descrPool;
          allocInfo.descriptorSetCount = 1;
          allocInfo.pSetLayouts    = m_descriptorSetLayout + i;

          // do one at a time, as we don't have layouts in maxcounts-many pointer array
          result = vkAllocateDescriptorSets(m_device, &allocInfo, setstores[i] + n);
          assert(result == VK_SUCCESS);
        }
      }

  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
      // single descritptorset 
      VkDescriptorPoolSize type_counts[UBOS_NUM];
      type_counts[UBO_SCENE].type = UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      type_counts[UBO_SCENE].descriptorCount = 1;
      type_counts[UBO_MATRIX].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      type_counts[UBO_MATRIX].descriptorCount = 1;
      type_counts[UBO_MATERIAL].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      type_counts[UBO_MATERIAL].descriptorCount = 1;

      VkDescriptorPool descrPool;
      VkDescriptorPoolCreateInfo descrPoolInfo;
      memset(&descrPoolInfo,0,sizeof(descrPoolInfo));

      descrPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      descrPoolInfo.pNext = NULL;
      descrPoolInfo.poolSizeCount = NV_ARRAYSIZE(type_counts);
      descrPoolInfo.pPoolSizes = type_counts;
      descrPoolInfo.maxSets = 1;
      
      result = vkCreateDescriptorPool(m_device, &descrPoolInfo, NULL, &descrPool);
      assert(result == VK_SUCCESS);
      m_descriptorPool = descrPool;

      VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
      allocInfo.descriptorPool = descrPool;
      allocInfo.setLayoutCount = 1;
      allocInfo.pSetLayouts    = &m_descriptorSetLayout;

      result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet);
      assert(result == VK_SUCCESS);

  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
      // single descritptorset 
      VkDescriptorPoolSize type_counts[1];
      type_counts[UBO_SCENE].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      type_counts[UBO_SCENE].descriptorCount = 1;

      VkDescriptorPool descrPool;
      VkDescriptorPoolCreateInfo descrPoolInfo;
      memset(&descrPoolInfo,0,sizeof(descrPoolInfo));
      descrPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      descrPoolInfo.pNext = NULL;
      descrPoolInfo.poolSizeCount = NV_ARRAYSIZE(type_counts);
      descrPoolInfo.pPoolSizes = type_counts;
      descrPoolInfo.maxSets = 1;
      
      result = vkCreateDescriptorPool(m_device, &descrPoolInfo, NULL, &descrPool);
      assert(result == VK_SUCCESS);
      m_descriptorPool = descrPool;

      VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
      allocInfo.descriptorPool = descrPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts    = &m_descriptorSetLayout;

      result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet);
      assert(result == VK_SUCCESS);

  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
      // single descritptorset 
      VkDescriptorPoolSize type_counts[UBOS_NUM];
      type_counts[UBO_SCENE].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      type_counts[UBO_SCENE].descriptorCount = 1;
      type_counts[UBO_MATRIX].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      type_counts[UBO_MATRIX].descriptorCount = 1;
      type_counts[UBO_MATERIAL].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      type_counts[UBO_MATERIAL].descriptorCount = 1;

      VkDescriptorPool descrPool;
      VkDescriptorPoolCreateInfo descrPoolInfo;
      memset(&descrPoolInfo,0,sizeof(descrPoolInfo));

      descrPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      descrPoolInfo.pNext = NULL;
      descrPoolInfo.poolSizeCount = NV_ARRAYSIZE(type_counts);
      descrPoolInfo.pPoolSizes = type_counts;
      descrPoolInfo.maxSets = 1;

      result = vkCreateDescriptorPool(m_device, &descrPoolInfo, NULL, &descrPool);
      assert(result == VK_SUCCESS);
      m_descriptorPool = descrPool;

      VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
      allocInfo.descriptorPool = descrPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts    = &m_descriptorSetLayout;

      result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet);
      assert(result == VK_SUCCESS);

  ///////////////////////////////////////////////////////////////////////////////////////////
  #endif


  //////////////////////////////////////////////////////////////////////////
  // Update phase
  //////////////////////////////////////////////////////////////////////////
      VkDescriptorBufferInfo descriptors[UBOS_NUM] = {};
      descriptors[UBO_SCENE] = views.scene;
      descriptors[UBO_MATRIX] = views.matrices;
      descriptors[UBO_MATERIAL] = views.materials;
  ///////////////////////////////////////////////////////////////////////////////////////////
  #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC

      VkWriteDescriptorSet updateDescriptors[UBOS_NUM] = { };
      for (int i = 0; i < UBOS_NUM; i++){
        updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        updateDescriptors[i].pNext = NULL;
        updateDescriptors[i].descriptorType = (i == UBO_SCENE) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        updateDescriptors[i].dstSet = m_descriptorSet[i];
        updateDescriptors[i].dstBinding = 0;
        updateDescriptors[i].dstArrayElement = 0;
        updateDescriptors[i].descriptorCount = 1;
        updateDescriptors[i].pBufferInfo  = descriptors+i;
      }
      vkUpdateDescriptorSets(m_device, UBOS_NUM, updateDescriptors, 0, 0);

  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
      
      std::vector<VkWriteDescriptorSet> queuedDescriptors;
      {
        VkWriteDescriptorSet updateDescriptor = {};
        updateDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateDescriptor.dstSet = m_descriptorSet[UBO_SCENE];
        updateDescriptor.dstBinding = 0;
        updateDescriptor.dstArrayElement = 0;
        updateDescriptor.descriptorCount = 1;
        updateDescriptor.pBufferInfo = &descriptors[UBO_SCENE];
        queuedDescriptors.push_back( updateDescriptor );
      }

      // loop over rest individually
      std::vector<VkDescriptorBufferInfo>     materialsInfo;
      materialsInfo.resize(m_descriptorSetsMaterials.size());
      for (size_t i = 0; i < m_descriptorSetsMaterials.size(); i++){
        VkDescriptorBufferInfo info = createBufferInfo( buffers.materials, sizeof(CadScene::Material), m_alignedMaterialSize * i);
        materialsInfo[i] = info;

        VkWriteDescriptorSet updateDescriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        updateDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateDescriptor.dstSet = m_descriptorSetsMaterials[i];
        updateDescriptor.dstBinding = 0;
        updateDescriptor.dstArrayElement = 0;
        updateDescriptor.descriptorCount = 1;
        updateDescriptor.pBufferInfo = &materialsInfo[i];

        queuedDescriptors.push_back( updateDescriptor );
      }

      std::vector<VkDescriptorBufferInfo>     matricesInfo;
      matricesInfo.resize(m_descriptorSetsMatrices.size());
      for (size_t i = 0; i < m_descriptorSetsMatrices.size(); i++){
        VkDescriptorBufferInfo info = createBufferInfo( buffers.matrices, sizeof(CadScene::MatrixNode), m_alignedMatrixSize * i);
        matricesInfo[i] = info;

        VkWriteDescriptorSet updateDescriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        updateDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateDescriptor.dstSet = m_descriptorSetsMatrices[i];
        updateDescriptor.dstBinding = 0;
        updateDescriptor.dstArrayElement = 0;
        updateDescriptor.descriptorCount = 1;
        updateDescriptor.pBufferInfo = &matricesInfo[i];

        queuedDescriptors.push_back( updateDescriptor );
      }

      vkUpdateDescriptorSets(m_device, queuedDescriptors.size(), &queuedDescriptors[0], 0, 0);
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC

      VkWriteDescriptorSet updateDescriptors[UBOS_NUM] = { };
      for (int i = 0; i < UBOS_NUM; i++){
        updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        updateDescriptors[i].pNext = NULL;
        updateDescriptors[i].descriptorType = UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || (UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC && i > UBO_SCENE ) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateDescriptors[i].dstSet = m_descriptorSet;
        updateDescriptors[i].dstBinding = i;
        updateDescriptors[i].dstArrayElement = 0;
        updateDescriptors[i].descriptorCount = 1;
        updateDescriptors[i].pBufferInfo  = descriptors+i;
      }
      vkUpdateDescriptorSets(m_device, UBOS_NUM, updateDescriptors, 0, 0);

  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW

      VkWriteDescriptorSet updateDescriptors[1] = { };
      for (int i = 0; i < 1; i++){
        updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        updateDescriptors[i].pNext = NULL;
        updateDescriptors[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateDescriptors[i].dstSet = m_descriptorSet;
        updateDescriptors[i].dstBinding = i;
        updateDescriptors[i].dstArrayElement = 0;
        updateDescriptors[i].descriptorCount = 1;
        updateDescriptors[i].pBufferInfo  = descriptors+i;
      }
      vkUpdateDescriptorSets(m_device, 1, updateDescriptors, 0, 0);
  ///////////////////////////////////////////////////////////////////////////////////////////
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX

      // slightly different
      descriptors[UBO_MATRIX]    = views.matricesFull;
      descriptors[UBO_MATERIAL]  = views.materialsFull;

      VkWriteDescriptorSet updateDescriptors[UBOS_NUM] = {};
      for (int i = 0; i < UBOS_NUM; i++){
        updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        updateDescriptors[i].pNext = NULL;
        updateDescriptors[i].descriptorType = (i == UBO_MATRIX || i == UBO_MATERIAL ) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateDescriptors[i].dstSet = m_descriptorSet;
        updateDescriptors[i].dstBinding = i;
        updateDescriptors[i].dstArrayElement = 0;
        updateDescriptors[i].descriptorCount = 1;
        updateDescriptors[i].pBufferInfo  = descriptors+i;
      }
      vkUpdateDescriptorSets(m_device, UBOS_NUM, updateDescriptors, 0, 0);

  ///////////////////////////////////////////////////////////////////////////////////////////
  #endif
    }

    {
      // animation
      VkDescriptorBufferInfo descriptors[3] = {};
      descriptors[UBO_ANIM] = views.anim;
      descriptors[SSBO_MATRIXOUT]  = views.matricesFull;
      descriptors[SSBO_MATRIXORIG] = views.matricesFullOrig;
      VkWriteDescriptorSet updateDescriptors[3] = { };
      for (int i = 0; i < 3; i++){
        updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        updateDescriptors[i].pNext = NULL;
        updateDescriptors[i].descriptorType = i == UBO_ANIM ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        updateDescriptors[i].dstSet = m_animDescriptorSet;
        updateDescriptors[i].dstBinding = i;
        updateDescriptors[i].dstArrayElement = 0;
        updateDescriptors[i].descriptorCount = 1;
        updateDescriptors[i].pBufferInfo  = descriptors+i;
      }
      vkUpdateDescriptorSets(m_device, 3, updateDescriptors, 0, 0);
    }

    return true;
  }

  void ResourcesVK::deinitScene()
  {
    // guard by synchronization as some stuff is unsafe to delete while in use
    synchronize();

    vkDestroyBuffer( m_device, buffers.scene, NULL);
    vkDestroyBuffer( m_device, buffers.anim, NULL);
    vkDestroyBuffer( m_device, buffers.matrices, NULL);
    vkDestroyBuffer( m_device, buffers.matricesOrig, NULL);
    vkDestroyBuffer( m_device, buffers.materials, NULL);

    vkFreeMemory(m_device, mem.anim, NULL);
    vkFreeMemory(m_device, mem.scene, NULL);
    vkFreeMemory(m_device, mem.matrices, NULL);
    vkFreeMemory(m_device, mem.matricesOrig, NULL);
    vkFreeMemory(m_device, mem.materials, NULL);

    for (size_t i = 0; i < m_geometry.size(); i++){
      Geometry&                   geom = m_geometry[i];
      if (geom.cloneIdx < 0){
#if !USE_SINGLE_GEOMETRY_BUFFERS
        vkDestroyBuffer(m_device, geom.vbo, NULL);
        vkDestroyBuffer(m_device, geom.ibo, NULL);
#endif
      }
    }
#if USE_SINGLE_GEOMETRY_BUFFERS
    vkDestroyBuffer(m_device, buffers.vbo, NULL);
    vkDestroyBuffer(m_device, buffers.ibo, NULL);
#endif
    vkFreeMemory(m_device, mem.vbo, NULL);
    vkFreeMemory(m_device, mem.ibo, NULL);

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
    
    for (size_t i = 0; i < UBOS_NUM; i++){
      vkDestroyDescriptorPool( m_device, m_descriptorPools[i], NULL );
    }
#elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    for (size_t i = 0; i < UBOS_NUM; i++){
      vkDestroyDescriptorPool( m_device, m_descriptorPools[i], NULL );
    }
#else
    vkDestroyDescriptorPool( m_device, m_descriptorPool, NULL );
#endif

  }

  void ResourcesVK::synchronize()
  {
    vkDeviceWaitIdle(m_device);
    //vkQueueWaitIdle(m_queue);
  }

  nv_math::mat4f ResourcesVK::perspectiveProjection( float fovy, float aspect, float nearPlane, float farPlane) const
  {
    // vulkan uses DX style 0,1 z clipspace

    nv_math::mat4f M;
    float r, l, b, t;
    float f = farPlane;
    float n = nearPlane;

    t = n * tanf(fovy * nv_to_rad * (0.5f));
    b = -t;

    l = b * aspect;
    r = t * aspect;

    M.a00 = (2.0f*n) / (r-l);
    M.a10 = 0.0f;
    M.a20 = 0.0f;
    M.a30 = 0.0f;

    M.a01 = 0.0f;
    M.a11 = -(2.0f*n) / (t-b);
    M.a21 = 0.0f;
    M.a31 = 0.0f;

    M.a02 = (r+l) / (r-l);
    M.a12 = (t+b) / (t-b);
    M.a22 = -(f) / (f-n);
    M.a32 = -1.0f;

    M.a03 = 0.0;
    M.a13 = 0.0;
    M.a23 = (f*n) / (n-f);
    M.a33 = 0.0;

    return M;
  }

  void ResourcesVK::animation( Global& global )
  {
    VkCommandBuffer cmd = createTempCmdBuffer();

    vkCmdUpdateBuffer(cmd, buffers.anim, 0, sizeof(AnimationData), (uint32_t*)&global.animUbo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipes.compute_animation);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_animPipelineLayout, 0, 1, &m_animDescriptorSet, 0, 0);
    vkCmdDispatch( cmd, (m_numMatrices + ANIMATION_WORKGROUPSIZE - 1)/ANIMATION_WORKGROUPSIZE, 1, 1 );

    VkBufferMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    memBarrier.buffer = buffers.matrices;
    memBarrier.size   = sizeof(AnimationData) * m_numMatrices;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_FALSE, 
      0, NULL, 1, &memBarrier, 0, NULL);

    vkEndCommandBuffer(cmd);

    submissionEnqueue(cmd);
    tempdestroyEnqueue(cmd);
  }

  void ResourcesVK::animationReset()
  {
    VkCommandBuffer cmd = createTempCmdBuffer();
    VkBufferCopy copy;
    copy.size   = sizeof(MatrixData) * m_numMatrices;
    copy.dstOffset = 0;
    copy.srcOffset = 0;
    vkCmdCopyBuffer(cmd, buffers.matricesOrig, buffers.matrices, 1, &copy);
    vkEndCommandBuffer(cmd);

#if 0
    // directly execute the reset
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &submitInfo, NULL);
#endif
    submissionEnqueue(cmd);
    tempdestroyEnqueue(cmd);
  }



}


