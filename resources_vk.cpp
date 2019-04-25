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

#include "resources_vk.hpp"

#include <imgui/imgui_impl_vk.h>

#include <algorithm>

#if HAS_OPENGL
#include <nvgl/extensions_gl.hpp>
#endif

extern bool vulkanInitLibrary();
using namespace nvgl;

namespace csfthreaded {

extern void setupVulkanContextInfo(nvvk::ContextInfoVK& info);

bool csfthreaded::ResourcesVK::isAvailable()
{
  static bool result = false;
  static bool s_init = false;

  if(s_init)
  {
    return result;
  }

  s_init = true;
  result = vulkanInitLibrary();

  return result;
}


/////////////////////////////////////////////////////////////////////////////////


void ResourcesVK::submissionExecute(VkFence fence, bool useImageReadWait, bool useImageWriteSignals)
{
  if(useImageReadWait && m_submissionWaitForRead)
  {
#if HAS_OPENGL
    VkSemaphore semRead = m_semImageRead;
#else
    VkSemaphore semRead    = m_swapChain->getActiveReadSemaphore();
#endif
    if(semRead)
    {
      m_submission.enqueueWait(semRead, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    m_submissionWaitForRead = false;
  }

  if(useImageWriteSignals)
  {
#if HAS_OPENGL
    VkSemaphore semWritten = m_semImageWritten;
#else
    VkSemaphore semWritten = m_swapChain->getActiveWrittenSemaphore();
#endif
    if(semWritten)
    {
      m_submission.enqueueSignal(semWritten);
    }
  }

  m_submission.execute(fence);
}

void ResourcesVK::beginFrame()
{
  m_submissionWaitForRead = true;
  m_ringFences.wait();
  m_ringCmdPool.setCycle(m_ringFences.getCycleIndex());
}

void ResourcesVK::endFrame()
{
  submissionExecute(m_ringFences.advanceCycle(), true, true);
#if HAS_OPENGL
  {
    // blit to gl backbuffer
    glDisable(GL_DEPTH_TEST);
    glWaitVkSemaphoreNV((GLuint64)m_semImageWritten);
    glDrawVkImageNV((GLuint64)(VkImage)(m_msaa ? images.scene_color_resolved : images.scene_color), 0, 0, 0, m_width,
                    m_height, 0, 0, 1, 1, 0);
    glEnable(GL_DEPTH_TEST);
    glSignalVkSemaphoreNV((GLuint64)m_semImageRead);
  }
#endif
}

void ResourcesVK::blitFrame(const Global& global)
{
  VkCommandBuffer cmd = createTempCmdBuffer();

  nvh::Profiler::SectionID sec = m_profilerVK.beginSection("BltUI", cmd);

  VkImage imageBlitRead = images.scene_color;

  if(m_msaa)
  {
    cmdImageTransition(cmd, images.scene_color, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageResolve region            = {0};
    region.extent.width              = m_width;
    region.extent.height             = m_height;
    region.extent.depth              = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;

    imageBlitRead = images.scene_color_resolved;

    vkCmdResolveImage(cmd, images.scene_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageBlitRead,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }


  if(global.imguiDrawData)
  {
    VkRenderPassBeginInfo renderPassBeginInfo    = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass               = passes.sceneUI;
    renderPassBeginInfo.framebuffer              = fbos.sceneUI;
    renderPassBeginInfo.renderArea.offset.x      = 0;
    renderPassBeginInfo.renderArea.offset.y      = 0;
    renderPassBeginInfo.renderArea.extent.width  = m_width;
    renderPassBeginInfo.renderArea.extent.height = m_height;
    renderPassBeginInfo.clearValueCount          = 0;
    renderPassBeginInfo.pClearValues             = nullptr;

    vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    cmdDynamicState(cmd);

    ImGui::RenderDrawDataVK(cmd, global.imguiDrawData);

    vkCmdEndRenderPass(cmd);

    // turns imageBlitRead to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  }
  else
  {
    if(m_msaa)
    {
      cmdImageTransition(cmd, images.scene_color_resolved, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    else
    {
      cmdImageTransition(cmd, images.scene_color, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
  }


#if !HAS_OPENGL
  {
    // blit to vk backbuffer
    VkImageBlit region               = {0};
    region.dstOffsets[1].x           = m_width;
    region.dstOffsets[1].y           = m_height;
    region.dstOffsets[1].z           = 1;
    region.srcOffsets[1].x           = m_width;
    region.srcOffsets[1].y           = m_height;
    region.srcOffsets[1].z           = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;

    cmdImageTransition(cmd, m_swapChain->getActiveImage(), VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdBlitImage(cmd, imageBlitRead, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapChain->getActiveImage(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);

    cmdImageTransition(cmd, m_swapChain->getActiveImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  }
#endif

  if(m_msaa)
  {
    cmdImageTransition(cmd, images.scene_color_resolved, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  }

  m_profilerVK.endSection(sec, cmd);

  vkEndCommandBuffer(cmd);
  submissionEnqueue(cmd);
}

bool ResourcesVK::init(ContextWindow* contextWindow, nvh::Profiler* profiler)
{
  m_msaa            = 0;
  m_fboIncarnation  = 0;
  m_pipeIncarnation = 0;

#if HAS_OPENGL
  {
    nvvk::ContextInfoVK info;
    info.device = Resources::s_vkDevice;
    setupVulkanContextInfo(info);

    if(!m_ctxContent.initContext(info))
    {
      LOGI("vulkan device create failed (use debug build for more information)\n");
      exit(-1);
      return false;
    }
    m_ctx         = &m_ctxContent;
    m_queueFamily = m_ctx->m_physicalInfo.getQueueFamily();
    vkGetDeviceQueue(m_ctx->m_device, m_queueFamily, 0, &m_queue);
  }

  {
    // OpenGL drawing
    VkSemaphoreCreateInfo semCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(m_ctx->m_device, &semCreateInfo, m_ctx->m_allocator, &m_semImageRead);
    vkCreateSemaphore(m_ctx->m_device, &semCreateInfo, m_ctx->m_allocator, &m_semImageWritten);

    // fire read to ensure queuesubmit never waits
    glSignalVkSemaphoreNV((GLuint64)m_semImageRead);
    glFlush();
  }
#else
  {
    m_ctx = &contextWindow->m_context;
    m_queue = contextWindow->m_presentQueue;
    m_queueFamily = contextWindow->m_presentQueueFamily;
    m_swapChain = &contextWindow->m_swapChain;
  }

#endif
  m_physical  = &m_ctx->m_physicalInfo;
  m_device    = m_ctx->m_device;
  m_allocator = m_ctx->m_allocator;

  initAlignedSizes((uint32_t)m_physical->properties.limits.minUniformBufferOffsetAlignment);

  // profiler
  m_profilerVK = nvvk::ProfilerVK(profiler);
  m_profilerVK.init(m_device, m_physical->physicalDevice, m_allocator);

  // submission queue
  m_submission.init(m_queue);

  // fences
  m_ringFences.init(m_device, m_allocator);

  // temp cmd pool
  m_ringCmdPool.init(m_device, m_queueFamily, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_allocator);

  // Create the render passes
  {
    passes.sceneClear    = createPass(true, m_msaa);
    passes.scenePreserve = createPass(false, m_msaa);
    passes.sceneUI       = createPassUI(m_msaa);
  }

  // animation
  {
    // UBO SCENE
    m_anim.init(m_device, m_allocator);
    m_anim.addBinding(ANIM_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0);
    m_anim.addBinding(ANIM_SSBO_MATRIXOUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0);
    m_anim.addBinding(ANIM_SSBO_MATRIXORIG, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0);
    m_anim.initLayout();
    m_anim.initPool(1);
    m_anim.initPipeLayout();
  }

  {
    m_drawing.init(m_device, m_allocator);

///////////////////////////////////////////////////////////////////////////////////////////
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC


    auto& bindingsScene = m_drawing.at(DRAW_UBO_SCENE);
    bindingsScene.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    bindingsScene.initLayout();

    auto& bindingsMatrix = m_drawing.at(DRAW_UBO_MATRIX);
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
    bindingsMatrix.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, 0);
#else
    bindingsMatrix.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, 0);
#endif
    bindingsMatrix.initLayout();

    auto& bindingsMaterial = m_drawing.at(DRAW_UBO_MATERIAL);
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
    bindingsMaterial.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
#else
    bindingsMaterial.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
#endif
    bindingsMaterial.initLayout();

    m_drawing.initPipeLayout(0);

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC

#if UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
    // "worst" case, we declare all as dynamic, and used in all stages
    // this will increase GPU time!
    m_drawing.addBinding(DRAW_UBO_SCENE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    m_drawing.addBinding(DRAW_UBO_MATRIX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    m_drawing.addBinding(DRAW_UBO_MATERIAL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
#elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
    m_drawing.addBinding(DRAW_UBO_SCENE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    m_drawing.addBinding(DRAW_UBO_MATRIX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, 0);
    m_drawing.addBinding(DRAW_UBO_MATERIAL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
#endif
    m_drawing.initLayout();
    m_drawing.initPipeLayout();

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW

    m_drawing.addBinding(DRAW_UBO_SCENE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    m_drawing.initLayout();

    assert(sizeof(ObjectData) + sizeof(MaterialData) <= m_physical->properties.limits.maxPushConstantsSize);

    // warning this will only work on NVIDIA as we support 256 Bytes push constants
    // minimum is 128 Bytes, which would not fit both data
    VkPushConstantRange pushRanges[2];
    pushRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRanges[0].size       = sizeof(ObjectData);
    pushRanges[0].offset     = 0;
    pushRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRanges[1].size       = sizeof(MaterialData);
    pushRanges[1].offset     = sizeof(ObjectData);

    m_drawing.initPipeLayout(NV_ARRAY_SIZE(pushRanges), pushRanges);

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX

    m_drawing.addBinding(DRAW_UBO_SCENE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    m_drawing.addBinding(DRAW_UBO_MATRIX, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, 0);
    m_drawing.addBinding(DRAW_UBO_MATERIAL, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    m_drawing.initLayout();

    VkPushConstantRange pushRanges[2];
    pushRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRanges[0].size       = sizeof(int);
    pushRanges[0].offset     = 0;
    pushRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRanges[1].size       = sizeof(int);
    pushRanges[1].offset     = sizeof(int);

    m_drawing.initPipeLayout(NV_ARRAY_SIZE(pushRanges), pushRanges);
///////////////////////////////////////////////////////////////////////////////////////////
#endif
  }


  {
    ImGui::InitVK(m_device, *m_physical, passes.sceneUI, m_queue, m_queueFamily);
  }

  return true;
}

void ResourcesVK::deinit()
{
  synchronize();

  ImGui::ShutdownVK();

  m_ringFences.deinit();
  m_ringCmdPool.deinit();

  deinitScene();
  deinitFramebuffer();
  deinitPipes();
  deinitPrograms();
  
  m_profilerVK.deinit();

  vkDestroyRenderPass(m_device, passes.sceneClear, NULL);
  vkDestroyRenderPass(m_device, passes.scenePreserve, NULL);
  vkDestroyRenderPass(m_device, passes.sceneUI, NULL);

  m_drawing.deinit();
  m_anim.deinit();

#if HAS_OPENGL
  vkDestroySemaphore(m_device, m_semImageRead, NULL);
  vkDestroySemaphore(m_device, m_semImageWritten, NULL);
  m_device = NULL;

  m_ctxContent.deinitContext();
#endif
}

bool ResourcesVK::initPrograms(const std::string& path, const std::string& prepend)
{
  m_shaderManager.init(m_device);
  m_shaderManager.m_useNVextension = false;
  m_shaderManager.m_filetype       = nvh::ShaderFileManager::FILETYPE_GLSL;

  m_shaderManager.addDirectory(path);
  m_shaderManager.addDirectory(std::string("GLSL_" PROJECT_NAME));
  m_shaderManager.addDirectory(path + std::string(PROJECT_RELDIRECTORY));
  //m_shaderManager.addDirectory(std::string(PROJECT_ABSDIRECTORY));

  m_shaderManager.registerInclude("common.h", "common.h");

  m_shaderManager.m_prepend =
      prepend + nvh::ShaderFileManager::format("#define UNIFORMS_ALLDYNAMIC %d\n", UNIFORMS_ALLDYNAMIC)
      + nvh::ShaderFileManager::format("#define UNIFORMS_SPLITDYNAMIC %d\n", UNIFORMS_SPLITDYNAMIC)
      + nvh::ShaderFileManager::format("#define UNIFORMS_MULTISETSDYNAMIC %d\n", UNIFORMS_MULTISETSDYNAMIC)
      + nvh::ShaderFileManager::format("#define UNIFORMS_MULTISETSSTATIC %d\n", UNIFORMS_MULTISETSSTATIC)
      + nvh::ShaderFileManager::format("#define UNIFORMS_PUSHCONSTANTS_RAW %d\n", UNIFORMS_PUSHCONSTANTS_RAW)
      + nvh::ShaderFileManager::format("#define UNIFORMS_PUSHCONSTANTS_INDEX %d\n", UNIFORMS_PUSHCONSTANTS_INDEX)
      + nvh::ShaderFileManager::format("#define UNIFORMS_TECHNIQUE %d\n", UNIFORMS_TECHNIQUE);

  ///////////////////////////////////////////////////////////////////////////////////////////
  moduleids.vertex_tris = m_shaderManager.createShaderModule(
      VK_SHADER_STAGE_VERTEX_BIT, "scene.vert.glsl", "#define WIREMODE 0\n");
  moduleids.fragment_tris = m_shaderManager.createShaderModule(
      VK_SHADER_STAGE_FRAGMENT_BIT, "scene.frag.glsl", "#define WIREMODE 0\n");

  moduleids.vertex_line = m_shaderManager.createShaderModule(
      VK_SHADER_STAGE_VERTEX_BIT, "scene.vert.glsl", "#define WIREMODE 1\n");
  moduleids.fragment_line = m_shaderManager.createShaderModule(
      VK_SHADER_STAGE_FRAGMENT_BIT, "scene.frag.glsl", "#define WIREMODE 1\n");

  ///////////////////////////////////////////////////////////////////////////////////////////
#if UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
  assert(sizeof(ObjectData) == 128);  // offset provided to material layout
#endif

  moduleids.compute_animation = m_shaderManager.createShaderModule(
      VK_SHADER_STAGE_COMPUTE_BIT, "animation.comp.glsl");

  bool valid = m_shaderManager.areShaderModulesValid();

  if(valid)
  {
    updatedPrograms();
  }

  return valid;
}

void ResourcesVK::reloadPrograms(const std::string& prepend)
{
  m_shaderManager.m_prepend = prepend;
  m_shaderManager.reloadShaderModules();
  updatedPrograms();
}

void ResourcesVK::updatedPrograms()
{
  shaders.fragment_tris     = m_shaderManager.get(moduleids.fragment_tris);
  shaders.vertex_tris       = m_shaderManager.get(moduleids.vertex_tris);
  shaders.fragment_line     = m_shaderManager.get(moduleids.fragment_line);
  shaders.vertex_line       = m_shaderManager.get(moduleids.vertex_line);
  shaders.compute_animation = m_shaderManager.get(moduleids.compute_animation);

  initPipes();
}

void ResourcesVK::deinitPrograms()
{
  m_shaderManager.deleteShaderModules();
}

static VkSampleCountFlagBits getSampleCountFlagBits(int msaa)
{
  switch(msaa)
  {
    case 2:
      return VK_SAMPLE_COUNT_2_BIT;
    case 4:
      return VK_SAMPLE_COUNT_4_BIT;
    case 8:
      return VK_SAMPLE_COUNT_8_BIT;
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
  VkAttachmentDescription attachments[2] = {};
  attachments[0].format                  = VK_FORMAT_R8G8B8A8_UNORM;
  attachments[0].samples                 = samplesUsed;
  attachments[0].loadOp                  = loadOp;
  attachments[0].storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].initialLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].finalLayout             = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].flags                   = 0;

  VkFormat depthStencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;
  m_physical->getOptimalDepthStencilFormat(depthStencilFormat);

  attachments[1].format              = depthStencilFormat;
  attachments[1].samples             = samplesUsed;
  attachments[1].loadOp              = loadOp;
  attachments[1].storeOp             = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[1].stencilLoadOp       = loadOp;
  attachments[1].stencilStoreOp      = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[1].initialLayout       = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachments[1].finalLayout         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachments[1].flags               = 0;
  VkSubpassDescription subpass       = {};
  subpass.pipelineBindPoint          = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.inputAttachmentCount       = 0;
  VkAttachmentReference colorRefs[1] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  subpass.colorAttachmentCount       = NV_ARRAY_SIZE(colorRefs);
  subpass.pColorAttachments          = colorRefs;
  VkAttachmentReference depthRefs[1] = {{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
  subpass.pDepthStencilAttachment    = depthRefs;
  VkRenderPassCreateInfo rpInfo      = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpInfo.attachmentCount             = NV_ARRAY_SIZE(attachments);
  rpInfo.pAttachments                = attachments;
  rpInfo.subpassCount                = 1;
  rpInfo.pSubpasses                  = &subpass;
  rpInfo.dependencyCount             = 0;

  VkRenderPass rp;
  result = vkCreateRenderPass(m_device, &rpInfo, NULL, &rp);
  assert(result == VK_SUCCESS);
  return rp;
}


VkRenderPass ResourcesVK::createPassUI(int msaa)
{
  // ui related
  // two cases:
  // if msaa we want to render into scene_color_resolved, which was DST_OPTIMAL
  // otherwise render into scene_color, which was VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  VkImageLayout uiTargetLayout = msaa ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  // Create the ui render pass
  VkAttachmentDescription attachments[1] = {};
  attachments[0].format                  = VK_FORMAT_R8G8B8A8_UNORM;
  attachments[0].samples                 = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp                  = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].initialLayout           = uiTargetLayout;
  attachments[0].finalLayout             = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;  // for blit operation
  attachments[0].flags                   = 0;

  VkSubpassDescription subpass       = {};
  subpass.pipelineBindPoint          = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.inputAttachmentCount       = 0;
  VkAttachmentReference colorRefs[1] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  subpass.colorAttachmentCount       = NV_ARRAY_SIZE(colorRefs);
  subpass.pColorAttachments          = colorRefs;
  subpass.pDepthStencilAttachment    = nullptr;
  VkRenderPassCreateInfo rpInfo      = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpInfo.attachmentCount             = NV_ARRAY_SIZE(attachments);
  rpInfo.pAttachments                = attachments;
  rpInfo.subpassCount                = 1;
  rpInfo.pSubpasses                  = &subpass;
  rpInfo.dependencyCount             = 0;

  VkRenderPass rp;
  VkResult     result = vkCreateRenderPass(m_device, &rpInfo, NULL, &rp);
  assert(result == VK_SUCCESS);
  return rp;
}


bool ResourcesVK::initFramebuffer(int width, int height, int msaa, bool vsync)
{
  VkResult result;

  m_fboIncarnation++;

  if(images.scene_color.m_value != 0)
  {
    deinitFramebuffer();
  }

  int oldmsaa = m_msaa;

  m_width  = width;
  m_height = height;
  m_msaa   = msaa;
  m_vsync  = vsync;

  if(oldmsaa != msaa)
  {
    vkDestroyRenderPass(m_device, passes.sceneClear, NULL);
    vkDestroyRenderPass(m_device, passes.scenePreserve, NULL);
    vkDestroyRenderPass(m_device, passes.sceneUI, NULL);

    // recreate the render passes with new msaa setting
    passes.sceneClear    = createPass(true, msaa);
    passes.scenePreserve = createPass(false, msaa);
    passes.sceneUI       = createPassUI(msaa);
  }


  VkSampleCountFlagBits samplesUsed = getSampleCountFlagBits(msaa);

  // color
  VkImageCreateInfo cbImageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  cbImageInfo.imageType         = VK_IMAGE_TYPE_2D;
  cbImageInfo.format            = VK_FORMAT_R8G8B8A8_UNORM;
  cbImageInfo.extent.width      = width;
  cbImageInfo.extent.height     = height;
  cbImageInfo.extent.depth      = 1;
  cbImageInfo.mipLevels         = 1;
  cbImageInfo.arrayLayers       = 1;
  cbImageInfo.samples           = samplesUsed;
  cbImageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
  cbImageInfo.usage             = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  cbImageInfo.flags             = 0;
  cbImageInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

  result = vkCreateImage(m_device, &cbImageInfo, NULL, &images.scene_color);
  assert(result == VK_SUCCESS);

  // depth stencil
  VkFormat depthStencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;
  m_physical->getOptimalDepthStencilFormat(depthStencilFormat);

  VkImageCreateInfo dsImageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  dsImageInfo.imageType         = VK_IMAGE_TYPE_2D;
  dsImageInfo.format            = depthStencilFormat;
  dsImageInfo.extent.width      = width;
  dsImageInfo.extent.height     = height;
  dsImageInfo.extent.depth      = 1;
  dsImageInfo.mipLevels         = 1;
  dsImageInfo.arrayLayers       = 1;
  dsImageInfo.samples           = samplesUsed;
  dsImageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
  dsImageInfo.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  dsImageInfo.flags             = 0;
  dsImageInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

  result = vkCreateImage(m_device, &dsImageInfo, NULL, &images.scene_depthstencil);
  assert(result == VK_SUCCESS);

  if(msaa)
  {
    // resolve image
    VkImageCreateInfo resImageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    resImageInfo.imageType         = VK_IMAGE_TYPE_2D;
    resImageInfo.format            = VK_FORMAT_R8G8B8A8_UNORM;
    resImageInfo.extent.width      = width;
    resImageInfo.extent.height     = height;
    resImageInfo.extent.depth      = 1;
    resImageInfo.mipLevels         = 1;
    resImageInfo.arrayLayers       = 1;
    resImageInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
    resImageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
    resImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    resImageInfo.flags         = 0;
    resImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = vkCreateImage(m_device, &resImageInfo, NULL, &images.scene_color_resolved);
    assert(result == VK_SUCCESS);
  }

  // handle allocation for all of them

  VkDeviceSize         cbImageOffset  = 0;
  VkDeviceSize         dsImageOffset  = 0;
  VkDeviceSize         resImageOffset = 0;
  VkMemoryAllocateInfo memInfo        = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  VkMemoryRequirements memReqs;
  bool                 valid;

  vkGetImageMemoryRequirements(m_device, images.scene_color, &memReqs);
  valid = m_physical->appendMemoryAllocationInfo(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo, cbImageOffset);
  assert(valid);
  vkGetImageMemoryRequirements(m_device, images.scene_depthstencil, &memReqs);
  valid = m_physical->appendMemoryAllocationInfo(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo, dsImageOffset);
  assert(valid);
  if(msaa)
  {
    vkGetImageMemoryRequirements(m_device, images.scene_color_resolved, &memReqs);
    valid = m_physical->appendMemoryAllocationInfo(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo, resImageOffset);
    assert(valid);
  }

  result = vkAllocateMemory(m_device, &memInfo, NULL, &mem.framebuffer);
  assert(result == VK_SUCCESS);
  vkBindImageMemory(m_device, images.scene_color, mem.framebuffer, cbImageOffset);
  vkBindImageMemory(m_device, images.scene_depthstencil, mem.framebuffer, dsImageOffset);
  if(msaa)
  {
    vkBindImageMemory(m_device, images.scene_color_resolved, mem.framebuffer, resImageOffset);
  }

  // views after allocation handling

  VkImageViewCreateInfo cbImageViewInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  cbImageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
  cbImageViewInfo.format                          = cbImageInfo.format;
  cbImageViewInfo.components.r                    = VK_COMPONENT_SWIZZLE_R;
  cbImageViewInfo.components.g                    = VK_COMPONENT_SWIZZLE_G;
  cbImageViewInfo.components.b                    = VK_COMPONENT_SWIZZLE_B;
  cbImageViewInfo.components.a                    = VK_COMPONENT_SWIZZLE_A;
  cbImageViewInfo.flags                           = 0;
  cbImageViewInfo.subresourceRange.levelCount     = 1;
  cbImageViewInfo.subresourceRange.baseMipLevel   = 0;
  cbImageViewInfo.subresourceRange.layerCount     = 1;
  cbImageViewInfo.subresourceRange.baseArrayLayer = 0;
  cbImageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;

  cbImageViewInfo.image = images.scene_color;
  result                = vkCreateImageView(m_device, &cbImageViewInfo, NULL, &views.scene_color);
  assert(result == VK_SUCCESS);

  if(msaa)
  {
    cbImageViewInfo.image = images.scene_color_resolved;
    result                = vkCreateImageView(m_device, &cbImageViewInfo, NULL, &views.scene_color_resolved);
    assert(result == VK_SUCCESS);
  }

  VkImageViewCreateInfo dsImageViewInfo           = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  dsImageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
  dsImageViewInfo.format                          = dsImageInfo.format;
  dsImageViewInfo.components.r                    = VK_COMPONENT_SWIZZLE_R;
  dsImageViewInfo.components.g                    = VK_COMPONENT_SWIZZLE_G;
  dsImageViewInfo.components.b                    = VK_COMPONENT_SWIZZLE_B;
  dsImageViewInfo.components.a                    = VK_COMPONENT_SWIZZLE_A;
  dsImageViewInfo.flags                           = 0;
  dsImageViewInfo.subresourceRange.levelCount     = 1;
  dsImageViewInfo.subresourceRange.baseMipLevel   = 0;
  dsImageViewInfo.subresourceRange.layerCount     = 1;
  dsImageViewInfo.subresourceRange.baseArrayLayer = 0;
  dsImageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

  dsImageViewInfo.image = images.scene_depthstencil;
  result                = vkCreateImageView(m_device, &dsImageViewInfo, NULL, &views.scene_depthstencil);
  assert(result == VK_SUCCESS);
  // initial resource transitions
  {
    VkCommandBuffer cmd = createTempCmdBuffer();

#if !HAS_OPENGL
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
                         m_swapChain->getImageCount(), m_swapChain->getImageMemoryBarriers());
#endif

    cmdImageTransition(cmd, images.scene_color, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_TRANSFER_READ_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    cmdImageTransition(cmd, images.scene_depthstencil, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0,
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    if(msaa)
    {
      cmdImageTransition(cmd, images.scene_color_resolved, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }

    vkEndCommandBuffer(cmd);

    submissionEnqueue(cmd);
    submissionExecute();
    synchronize();
    resetTempResources();
  }

  {
    // Create framebuffers
    VkImageView bindInfos[2];
    bindInfos[0] = views.scene_color;
    bindInfos[1] = views.scene_depthstencil;

    VkFramebuffer           fb;
    VkFramebufferCreateInfo fbInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbInfo.attachmentCount         = NV_ARRAY_SIZE(bindInfos);
    fbInfo.pAttachments            = bindInfos;
    fbInfo.width                   = width;
    fbInfo.height                  = height;
    fbInfo.layers                  = 1;

    fbInfo.renderPass = passes.sceneClear;
    result            = vkCreateFramebuffer(m_device, &fbInfo, NULL, &fb);
    assert(result == VK_SUCCESS);
    fbos.scene = fb;
  }


  // ui related
  {
    VkImageView uiTarget = msaa ? views.scene_color_resolved : views.scene_color;

    // Create framebuffers
    VkImageView bindInfos[1];
    bindInfos[0] = uiTarget;

    VkFramebuffer           fb;
    VkFramebufferCreateInfo fbInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbInfo.attachmentCount         = NV_ARRAY_SIZE(bindInfos);
    fbInfo.pAttachments            = bindInfos;
    fbInfo.width                   = width;
    fbInfo.height                  = height;
    fbInfo.layers                  = 1;

    fbInfo.renderPass = passes.sceneUI;
    result            = vkCreateFramebuffer(m_device, &fbInfo, NULL, &fb);
    assert(result == VK_SUCCESS);
    fbos.sceneUI = fb;
  }

  {
    // viewport
    int vpWidth  = width;
    int vpHeight = height;

    VkViewport vp;
    VkRect2D   sc;
    vp.x        = 0;
    vp.y        = 0;
    vp.height   = float(vpHeight);
    vp.width    = float(vpWidth);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    sc.offset.x      = 0;
    sc.offset.y      = 0;
    sc.extent.width  = vpWidth;
    sc.extent.height = vpHeight;

    states.viewport = vp;
    states.scissor  = sc;
  }

  if(msaa != oldmsaa)
  {
    ImGui::ReInitPipelinesVK(m_device, passes.sceneUI);
  }
  if(msaa != oldmsaa && hasPipes())
  {
    // reinit pipelines
    initPipes();
  }

  return true;
}

void ResourcesVK::deinitFramebuffer()
{
  synchronize();

  vkDestroyImageView(m_device, views.scene_color, NULL);
  vkDestroyImageView(m_device, views.scene_depthstencil, NULL);

  views.scene_color        = NULL;
  views.scene_depthstencil = NULL;

  vkDestroyImage(m_device, images.scene_color, NULL);
  vkDestroyImage(m_device, images.scene_depthstencil, NULL);
  images.scene_color        = NULL;
  images.scene_depthstencil = NULL;

  if(images.scene_color_resolved)
  {
    vkDestroyImageView(m_device, views.scene_color_resolved, NULL);
    views.scene_color_resolved = NULL;

    vkDestroyImage(m_device, images.scene_color_resolved, NULL);
    images.scene_color_resolved = NULL;
  }

  vkFreeMemory(m_device, mem.framebuffer, NULL);

  vkDestroyFramebuffer(m_device, fbos.scene, NULL);
  fbos.scene = NULL;

  vkDestroyFramebuffer(m_device, fbos.sceneUI, NULL);
  fbos.sceneUI = NULL;
}

void ResourcesVK::initPipes()
{
  VkResult result;

  m_pipeIncarnation++;

  if(hasPipes())
  {
    deinitPipes();
  }

  VkSampleCountFlagBits samplesUsed = getSampleCountFlagBits(m_msaa);

  // Create static state info for the pipeline.
  VkVertexInputBindingDescription vertexBinding;
  vertexBinding.stride                             = sizeof(CadScene::Vertex);
  vertexBinding.inputRate                          = VK_VERTEX_INPUT_RATE_VERTEX;
  vertexBinding.binding                            = 0;
  VkVertexInputAttributeDescription attributes[2]  = {};
  attributes[0].location                           = VERTEX_POS;
  attributes[0].binding                            = 0;
  attributes[0].format                             = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[0].offset                             = offsetof(CadScene::Vertex, position);
  attributes[1].location                           = VERTEX_NORMAL;
  attributes[1].binding                            = 0;
  attributes[1].format                             = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[1].offset                             = offsetof(CadScene::Vertex, normal);
  VkPipelineVertexInputStateCreateInfo viStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  viStateInfo.vertexBindingDescriptionCount        = 1;
  viStateInfo.pVertexBindingDescriptions           = &vertexBinding;
  viStateInfo.vertexAttributeDescriptionCount      = NV_ARRAY_SIZE(attributes);
  viStateInfo.pVertexAttributeDescriptions         = attributes;

  VkPipelineInputAssemblyStateCreateInfo iaStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  iaStateInfo.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  iaStateInfo.primitiveRestartEnable                 = VK_FALSE;

  VkPipelineViewportStateCreateInfo vpStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vpStateInfo.viewportCount                     = 1;
  vpStateInfo.scissorCount                      = 1;

  VkPipelineRasterizationStateCreateInfo rsStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};

  rsStateInfo.rasterizerDiscardEnable = VK_FALSE;
  rsStateInfo.polygonMode             = VK_POLYGON_MODE_FILL;
  rsStateInfo.cullMode                = VK_CULL_MODE_NONE;
  rsStateInfo.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rsStateInfo.depthClampEnable        = VK_TRUE;
  rsStateInfo.depthBiasEnable         = VK_FALSE;
  rsStateInfo.depthBiasConstantFactor = 0.0;
  rsStateInfo.depthBiasSlopeFactor    = 0.0f;
  rsStateInfo.depthBiasClamp          = 0.0f;
  rsStateInfo.lineWidth               = 1.0f;

  // create a color blend attachment that does blending
  VkPipelineColorBlendAttachmentState cbAttachmentState[1] = {};
  cbAttachmentState[0].blendEnable                         = VK_FALSE;
  cbAttachmentState[0].colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  // create a color blend state that does blending
  VkPipelineColorBlendStateCreateInfo cbStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbStateInfo.logicOpEnable                       = VK_FALSE;
  cbStateInfo.attachmentCount                     = 1;
  cbStateInfo.pAttachments                        = cbAttachmentState;
  cbStateInfo.blendConstants[0]                   = 1.0f;
  cbStateInfo.blendConstants[1]                   = 1.0f;
  cbStateInfo.blendConstants[2]                   = 1.0f;
  cbStateInfo.blendConstants[3]                   = 1.0f;

  VkPipelineDepthStencilStateCreateInfo dsStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  dsStateInfo.depthTestEnable                       = VK_TRUE;
  dsStateInfo.depthWriteEnable                      = VK_TRUE;
  dsStateInfo.depthCompareOp                        = VK_COMPARE_OP_LESS;
  dsStateInfo.depthBoundsTestEnable                 = VK_FALSE;
  dsStateInfo.stencilTestEnable                     = VK_FALSE;
  dsStateInfo.minDepthBounds                        = 0.0f;
  dsStateInfo.maxDepthBounds                        = 1.0f;

  VkPipelineMultisampleStateCreateInfo msStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msStateInfo.rasterizationSamples                 = samplesUsed;
  msStateInfo.sampleShadingEnable                  = VK_FALSE;
  msStateInfo.minSampleShading                     = 1.0f;
  uint32_t sampleMask                              = 0xFFFFFFFF;
  msStateInfo.pSampleMask                          = &sampleMask;

  VkPipelineTessellationStateCreateInfo tessStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
  tessStateInfo.patchControlPoints                    = 0;

  VkPipelineDynamicStateCreateInfo dynStateInfo = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  VkDynamicState                   dynStates[]  = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  dynStateInfo.dynamicStateCount                = NV_ARRAY_SIZE(dynStates);
  dynStateInfo.pDynamicStates                   = dynStates;

  {
    VkPipeline                   pipeline;
    VkGraphicsPipelineCreateInfo pipelineInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.layout                       = m_drawing.getPipeLayout();
    pipelineInfo.pVertexInputState            = &viStateInfo;
    pipelineInfo.pInputAssemblyState          = &iaStateInfo;
    pipelineInfo.pViewportState               = &vpStateInfo;
    pipelineInfo.pRasterizationState          = &rsStateInfo;
    pipelineInfo.pColorBlendState             = &cbStateInfo;
    pipelineInfo.pDepthStencilState           = &dsStateInfo;
    pipelineInfo.pMultisampleState            = &msStateInfo;
    pipelineInfo.pTessellationState           = &tessStateInfo;
    pipelineInfo.pDynamicState                = &dynStateInfo;

    pipelineInfo.renderPass = passes.scenePreserve;
    pipelineInfo.subpass    = 0;

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages    = stages;

    VkPipelineShaderStageCreateInfo& vsStageInfo = stages[0];
    vsStageInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsStageInfo.pNext                            = NULL;
    vsStageInfo.stage                            = VK_SHADER_STAGE_VERTEX_BIT;
    vsStageInfo.pName                            = "main";
    vsStageInfo.module                           = NULL;

    VkPipelineShaderStageCreateInfo& fsStageInfo = stages[1];
    fsStageInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fsStageInfo.pNext                            = NULL;
    fsStageInfo.stage                            = VK_SHADER_STAGE_FRAGMENT_BIT;
    fsStageInfo.pName                            = "main";
    fsStageInfo.module                           = NULL;

    vsStageInfo.module = shaders.vertex_tris;
    fsStageInfo.module = shaders.fragment_tris;
    
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
    assert(result == VK_SUCCESS);
    pipes.tris = pipeline;

    rsStateInfo.depthBiasEnable         = VK_TRUE;
    rsStateInfo.depthBiasConstantFactor = 1.0f;
    rsStateInfo.depthBiasSlopeFactor    = 1.0;

    vsStageInfo.module = shaders.vertex_tris;
    
    fsStageInfo.module = shaders.fragment_tris;
    
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
    assert(result == VK_SUCCESS);
    pipes.line_tris = pipeline;

    iaStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    vsStageInfo.module   = shaders.vertex_line;
    fsStageInfo.module   = shaders.fragment_line;
    result               = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
    assert(result == VK_SUCCESS);
    pipes.line = pipeline;
  }

  //////////////////////////////////////////////////////////////////////////

  {
    VkPipeline                      pipeline;
    VkComputePipelineCreateInfo     pipelineInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    VkPipelineShaderStageCreateInfo stageInfo    = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage                              = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.pName                              = "main";
    stageInfo.module                             = shaders.compute_animation;

    pipelineInfo.layout = m_anim.getPipeLayout();
    pipelineInfo.stage  = stageInfo;
    result              = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline);
    assert(result == VK_SUCCESS);
    pipes.compute_animation = pipeline;
  }
}

void ResourcesVK::deinitPipes()
{
  vkDestroyPipeline(m_device, pipes.line, NULL);
  vkDestroyPipeline(m_device, pipes.line_tris, NULL);
  vkDestroyPipeline(m_device, pipes.tris, NULL);
  vkDestroyPipeline(m_device, pipes.compute_animation, NULL);
  pipes.line              = NULL;
  pipes.line_tris         = NULL;
  pipes.tris              = NULL;
  pipes.compute_animation = NULL;
}

void ResourcesVK::cmdDynamicState(VkCommandBuffer cmd) const
{
  vkCmdSetViewport(cmd, 0, 1, &states.viewport);
  vkCmdSetScissor(cmd, 0, 1, &states.scissor);
}

void ResourcesVK::cmdBeginRenderPass(VkCommandBuffer cmd, bool clear, bool hasSecondary) const
{
  VkRenderPassBeginInfo renderPassBeginInfo    = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  renderPassBeginInfo.renderPass               = clear ? passes.sceneClear : passes.scenePreserve;
  renderPassBeginInfo.framebuffer              = fbos.scene;
  renderPassBeginInfo.renderArea.offset.x      = 0;
  renderPassBeginInfo.renderArea.offset.y      = 0;
  renderPassBeginInfo.renderArea.extent.width  = m_width;
  renderPassBeginInfo.renderArea.extent.height = m_height;
  renderPassBeginInfo.clearValueCount          = 2;
  VkClearValue clearValues[2];
  clearValues[0].color.float32[0]     = 0.2f;
  clearValues[0].color.float32[1]     = 0.2f;
  clearValues[0].color.float32[2]     = 0.2f;
  clearValues[0].color.float32[3]     = 0.0f;
  clearValues[1].depthStencil.depth   = 1.0f;
  clearValues[1].depthStencil.stencil = 0;
  renderPassBeginInfo.pClearValues    = clearValues;
  vkCmdBeginRenderPass(cmd, &renderPassBeginInfo,
                       hasSecondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);
}

void ResourcesVK::cmdPipelineBarrier(VkCommandBuffer cmd) const
{
  // color transition
  {
    VkImageSubresourceRange colorRange;
    memset(&colorRange, 0, sizeof(colorRange));
    colorRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.baseMipLevel   = 0;
    colorRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    colorRange.baseArrayLayer = 0;
    colorRange.layerCount     = 1;

    VkImageMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    memBarrier.srcAccessMask        = VK_ACCESS_TRANSFER_READ_BIT;
    memBarrier.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    memBarrier.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    memBarrier.newLayout            = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    memBarrier.image                = images.scene_color;
    memBarrier.subresourceRange     = colorRange;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_FALSE,
                         0, NULL, 0, NULL, 1, &memBarrier);
  }

#if 1
  // Prepare the depth+stencil for reading.
  {
    VkImageSubresourceRange depthStencilRange;
    memset(&depthStencilRange, 0, sizeof(depthStencilRange));
    depthStencilRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depthStencilRange.baseMipLevel   = 0;
    depthStencilRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    depthStencilRange.baseArrayLayer = 0;
    depthStencilRange.layerCount     = 1;

    VkImageMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    memBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    memBarrier.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    memBarrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    memBarrier.image         = images.scene_depthstencil;
    memBarrier.subresourceRange = depthStencilRange;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         VK_FALSE, 0, NULL, 0, NULL, 1, &memBarrier);
  }
#endif
}


void ResourcesVK::cmdImageTransition(VkCommandBuffer    cmd,
                                     VkImage            img,
                                     VkImageAspectFlags aspects,
                                     VkAccessFlags      src,
                                     VkAccessFlags      dst,
                                     VkImageLayout      oldLayout,
                                     VkImageLayout      newLayout) const
{

  VkPipelineStageFlags srcPipe = nvvk::makeAccessMaskPipelineStageFlags(src);
  VkPipelineStageFlags dstPipe = nvvk::makeAccessMaskPipelineStageFlags(dst);

  VkImageSubresourceRange range;
  memset(&range, 0, sizeof(range));
  range.aspectMask     = aspects;
  range.baseMipLevel   = 0;
  range.levelCount     = VK_REMAINING_MIP_LEVELS;
  range.baseArrayLayer = 0;
  range.layerCount     = VK_REMAINING_ARRAY_LAYERS;

  VkImageMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  memBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  memBarrier.dstAccessMask        = dst;
  memBarrier.srcAccessMask        = src;
  memBarrier.oldLayout            = oldLayout;
  memBarrier.newLayout            = newLayout;
  memBarrier.image                = img;
  memBarrier.subresourceRange     = range;

  vkCmdPipelineBarrier(cmd, srcPipe, dstPipe, VK_FALSE, 0, NULL, 0, NULL, 1, &memBarrier);
}

VkCommandBuffer ResourcesVK::createCmdBuffer(VkCommandPool pool, bool singleshot, bool primary, bool secondaryInClear) const
{
  VkResult result;
  bool     secondary = !primary;

  // Create the command buffer.
  VkCommandBufferAllocateInfo cmdInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cmdInfo.commandPool                 = pool;
  cmdInfo.level                       = primary ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  cmdInfo.commandBufferCount          = 1;
  VkCommandBuffer cmd;
  result = vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd);
  assert(result == VK_SUCCESS);

  cmdBegin(cmd, singleshot, primary, secondaryInClear);

  return cmd;
}

VkCommandBuffer ResourcesVK::createTempCmdBuffer(bool primary /*=true*/, bool secondaryInClear /*=false*/)
{
  VkCommandBuffer cmd =
      m_ringCmdPool.createCommandBuffer(primary ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY);
  cmdBegin(cmd, true, primary, secondaryInClear);
  return cmd;
}

void ResourcesVK::cmdBegin(VkCommandBuffer cmd, bool singleshot, bool primary, bool secondaryInClear) const
{
  VkResult result;
  bool     secondary = !primary;

  VkCommandBufferInheritanceInfo inheritInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
  if(secondary)
  {
    inheritInfo.renderPass  = secondaryInClear ? passes.sceneClear : passes.scenePreserve;
    inheritInfo.framebuffer = fbos.scene;
  }

  VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  // the sample is resubmitting re-use commandbuffers to the queue while they may still be executed by GPU
  // we only use fences to prevent deleting commandbuffers that are still in flight
  beginInfo.flags = singleshot ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  // the sample's secondary buffers always are called within passes as they contain drawcalls
  beginInfo.flags |= secondary ? VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : 0;
  beginInfo.pInheritanceInfo = &inheritInfo;

  result = vkBeginCommandBuffer(cmd, &beginInfo);
  assert(result == VK_SUCCESS);
}

void ResourcesVK::resetTempResources()
{
  synchronize();
  m_ringFences.reset();
  m_ringCmdPool.reset(VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
}

void ResourcesVK::flushStaging(nvvk::FixedSizeStagingBuffer& staging)
{
  if(staging.canFlush())
  {
    VkCommandBuffer cmd = createTempCmdBuffer();
    staging.flush(cmd);
    vkEndCommandBuffer(cmd);

    submissionEnqueue(cmd);
    submissionExecute();

    synchronize();
    resetTempResources();
  }
}

void ResourcesVK::fillBuffer(nvvk::FixedSizeStagingBuffer& staging, VkBuffer buffer, size_t offset, size_t size, const void* data)
{
  if(!size)
    return;

  if(staging.cannotEnqueue(size))
  {
    flushStaging(staging);
  }

  staging.enqueue(buffer, offset, size, data);
}

VkBuffer ResourcesVK::createAndFillBuffer(nvvk::FixedSizeStagingBuffer& staging,
                                          size_t                        size,
                                          const void*                   data,
                                          VkFlags                       usage,
                                          VkDeviceMemory&               bufferMem,
                                          VkFlags                       memProps)
{
  VkResult           result;
  VkBuffer           buffer;
  VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size               = size;
  bufferInfo.usage              = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.flags              = 0;

  result = vkCreateBuffer(m_device, &bufferInfo, NULL, &buffer);
  assert(result == VK_SUCCESS);

  result = allocMemAndBindBuffer(buffer, bufferMem, memProps);
  assert(result == VK_SUCCESS);

  if(data)
  {
    fillBuffer(staging, buffer, 0, size, data);
  }

  return buffer;
}

bool ResourcesVK::initScene(const CadScene& cadscene)
{
  VkResult result = VK_SUCCESS;

  m_numMatrices = uint(cadscene.m_matrices.size());

  m_geometry.resize(cadscene.m_geometry.size());

  if(m_geometry.empty())
    return true;


  nvvk::FixedSizeStagingBuffer staging;
  staging.init(m_device, &m_physical->memoryProperties);

#if USE_SINGLE_GEOMETRY_BUFFERS
  size_t vbosizeAligned = 0;
  size_t ibosizeAligned = 0;
#else
  VkMemoryAllocateInfo memInfoVbo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  VkMemoryRequirements memReqsVbo;
  VkMemoryAllocateInfo memInfoIbo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  VkMemoryRequirements memReqsIbo;
  bool valid;
#endif
  for(size_t i = 0; i < cadscene.m_geometry.size(); i++)
  {
    const CadScene::Geometry& cgeom = cadscene.m_geometry[i];
    Geometry&                 geom  = m_geometry[i];

    if(cgeom.cloneIdx < 0)
    {
      geom.vbo.size = cgeom.vboSize;
      geom.ibo.size = cgeom.iboSize;

#if USE_SINGLE_GEOMETRY_BUFFERS
      geom.vbo.offset = vbosizeAligned;
      geom.ibo.offset = ibosizeAligned;

      size_t vboAlignment = 16;
      size_t iboAlignment = 4;
      vbosizeAligned += ((cgeom.vboSize + vboAlignment - 1) / vboAlignment) * vboAlignment;
      ibosizeAligned += ((cgeom.iboSize + iboAlignment - 1) / iboAlignment) * iboAlignment;
#else
      geom.vbo.buffer = createBuffer(geom.vbo.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      vkGetBufferMemoryRequirements(m_device, geom.vbo.buffer, &memReqsVbo);
      valid =
          m_physical->appendMemoryAllocationInfo(memReqsVbo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfoVbo, geom.vbo.offset);
      assert(valid);

      geom.ibo.buffer = createBuffer(geom.ibo.size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
      vkGetBufferMemoryRequirements(m_device, geom.ibo.buffer, &memReqsIbo);
      valid =
          m_physical->appendMemoryAllocationInfo(memReqsIbo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfoIbo, geom.ibo.offset);
      assert(valid);
#endif
    }
  }

#if USE_SINGLE_GEOMETRY_BUFFERS
  // packs everything tightly in big buffes with a single allocation each.
  buffers.vbo = createAndFillBuffer(staging, vbosizeAligned, NULL, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mem.vbo);
  buffers.ibo = createAndFillBuffer(staging, ibosizeAligned, NULL, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mem.ibo);
#else
  result = vkAllocateMemory(m_device, &memInfoVbo, NULL, &mem.vbo);
  assert(result == VK_SUCCESS);
  result = vkAllocateMemory(m_device, &memInfoIbo, NULL, &mem.ibo);
  assert(result == VK_SUCCESS);
#endif

  for(size_t i = 0; i < cadscene.m_geometry.size(); i++)
  {
    const CadScene::Geometry& cgeom = cadscene.m_geometry[i];
    Geometry&                 geom  = m_geometry[i];

    if(cgeom.cloneIdx < 0)
    {
#if USE_SINGLE_GEOMETRY_BUFFERS
      geom.vbo.buffer = buffers.vbo;
      geom.ibo.buffer = buffers.ibo;
#else
      vkBindBufferMemory(m_device, geom.vbo.buffer, mem.vbo, geom.vbo.offset);
      vkBindBufferMemory(m_device, geom.ibo.buffer, mem.ibo, geom.ibo.offset);
      geom.vbo.offset = 0;
      geom.ibo.offset = 0;
#endif

      fillBuffer(staging, geom.vbo.buffer, geom.vbo.offset, geom.vbo.size, cgeom.vertices);
      fillBuffer(staging, geom.ibo.buffer, geom.ibo.offset, geom.ibo.size, cgeom.indices);
    }
    else
    {
      geom = m_geometry[cgeom.cloneIdx];
    }
    geom.cloneIdx = cgeom.cloneIdx;
  }

  buffers.scene = createAndFillBuffer(staging, sizeof(SceneData), NULL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.scene);
  views.scene   = {buffers.scene, 0, sizeof(SceneData)};

  buffers.anim = createAndFillBuffer(staging, sizeof(AnimationData), NULL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.anim);
  views.anim   = {buffers.anim, 0, sizeof(AnimationData)};

  // FIXME, atm sizes must match, but cleaner solution is creating a temp strided memory block for material & matrix
  assert(sizeof(CadScene::MatrixNode) == m_alignedMatrixSize);
  assert(sizeof(CadScene::Material) == m_alignedMaterialSize);

  buffers.materials =
      createAndFillBuffer(staging, cadscene.m_materials.size() * sizeof(CadScene::Material), cadscene.m_materials.data(),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.materials);
  views.materials     = {buffers.materials, 0, sizeof(CadScene::Material)};
  views.materialsFull = {buffers.materials, 0, sizeof(CadScene::Material) * cadscene.m_materials.size()};

  buffers.matrices =
      createAndFillBuffer(staging, cadscene.m_matrices.size() * sizeof(CadScene::MatrixNode), cadscene.m_matrices.data(),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mem.matrices);
  views.matrices     = {buffers.matrices, 0, sizeof(CadScene::MatrixNode)};
  views.matricesFull = {buffers.matrices, 0, sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size()};

  buffers.matricesOrig =
      createAndFillBuffer(staging, cadscene.m_matrices.size() * sizeof(CadScene::MatrixNode), cadscene.m_matrices.data(),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, mem.matricesOrig);
  views.matricesFullOrig = {buffers.matricesOrig, 0, sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size()};

  flushStaging(staging);
  staging.deinit();

  {
    //////////////////////////////////////////////////////////////////////////
    // Allocation phase
    //////////////////////////////////////////////////////////////////////////

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    m_drawing.at(DRAW_UBO_SCENE).initPool(1);

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
    m_drawing.at(DRAW_UBO_MATRIX).initPool(1);
    m_drawing.at(DRAW_UBO_MATERIAL).initPool(1);

#else
    m_drawing.at(DRAW_UBO_MATRIX).initPool(cadscene.m_matrices.size());
    m_drawing.at(DRAW_UBO_MATERIAL).initPool(cadscene.m_materials.size());

#endif

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
    m_drawing.initPool(1);

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
    m_drawing.initPool(1);

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
    m_drawing.initPool(1);

///////////////////////////////////////////////////////////////////////////////////////////
#endif


    //////////////////////////////////////////////////////////////////////////
    // Update phase
    //////////////////////////////////////////////////////////////////////////
    VkDescriptorBufferInfo descriptors[DRAW_UBOS_NUM] = {};
    descriptors[DRAW_UBO_SCENE]                       = views.scene;
    descriptors[DRAW_UBO_MATRIX]                      = views.matrices;
    descriptors[DRAW_UBO_MATERIAL]                    = views.materials;

    // There is utility functions inside the m_drawing container
    // that handle this in a simpler fashion using m_drawing.getWrite (...), 
    // but for educational purposes let's have a look at the details here.

///////////////////////////////////////////////////////////////////////////////////////////
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC

    VkWriteDescriptorSet updateDescriptors[DRAW_UBOS_NUM] = {};
    for(int i = 0; i < DRAW_UBOS_NUM; i++)
    {
      updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      updateDescriptors[i].pNext = NULL;
      updateDescriptors[i].descriptorType =
          (i == DRAW_UBO_SCENE) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      updateDescriptors[i].dstSet = m_drawing.at(i).getSets()[0];
      updateDescriptors[i].dstBinding      = 0;
      updateDescriptors[i].dstArrayElement = 0;
      updateDescriptors[i].descriptorCount = 1;
      updateDescriptors[i].pBufferInfo     = descriptors + i;
    }
    vkUpdateDescriptorSets(m_device, DRAW_UBOS_NUM, updateDescriptors, 0, 0);

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC

    std::vector<VkWriteDescriptorSet> queuedDescriptors;
    {
      VkWriteDescriptorSet updateDescriptor = {};
      updateDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      updateDescriptor.dstSet = m_drawing.at(DRAW_UBO_SCENE).getSet(0);
      updateDescriptor.dstBinding = 0;
      updateDescriptor.dstArrayElement = 0;
      updateDescriptor.descriptorCount = 1;
      updateDescriptor.pBufferInfo = &descriptors[DRAW_UBO_SCENE];
      queuedDescriptors.push_back(updateDescriptor);
    }

    // loop over rest individually
    std::vector<VkDescriptorBufferInfo> materialsInfo;
    materialsInfo.resize(m_drawing.at(DRAW_UBO_MATERIAL).getSetsCount());
    for(size_t i = 0; i < m_drawing.at(DRAW_UBO_MATERIAL).getSetsCount(); i++)
    {
      VkDescriptorBufferInfo info = {buffers.materials, m_alignedMaterialSize * i, sizeof(CadScene::Material)};
      materialsInfo[i] = info;

      VkWriteDescriptorSet updateDescriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      updateDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      updateDescriptor.dstSet = m_drawing.at(DRAW_UBO_MATERIAL).getSet(i);
      updateDescriptor.dstBinding = 0;
      updateDescriptor.dstArrayElement = 0;
      updateDescriptor.descriptorCount = 1;
      updateDescriptor.pBufferInfo = &materialsInfo[i];

      queuedDescriptors.push_back(updateDescriptor);
    }

    std::vector<VkDescriptorBufferInfo> matricesInfo;
    matricesInfo.resize(m_drawing.at(DRAW_UBO_MATRIX).getSetsCount());
    for(size_t i = 0; i < m_drawing.at(DRAW_UBO_MATRIX).getSetsCount(); i++)
    {
      VkDescriptorBufferInfo info = {buffers.matrices, m_alignedMatrixSize * i, sizeof(CadScene::MatrixNode)};
      matricesInfo[i] = info;

      VkWriteDescriptorSet updateDescriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      updateDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      updateDescriptor.dstSet = m_drawing.at(DRAW_UBO_MATRIX).getSet(i);
      updateDescriptor.dstBinding = 0;
      updateDescriptor.dstArrayElement = 0;
      updateDescriptor.descriptorCount = 1;
      updateDescriptor.pBufferInfo = &matricesInfo[i];

      queuedDescriptors.push_back(updateDescriptor);
    }

    vkUpdateDescriptorSets(m_device, queuedDescriptors.size(), &queuedDescriptors[0], 0, 0);
///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC

    VkWriteDescriptorSet updateDescriptors[DRAW_UBOS_NUM] = {};
    for(int i = 0; i < DRAW_UBOS_NUM; i++)
    {
      updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      updateDescriptors[i].pNext = NULL;
      updateDescriptors[i].descriptorType =
          UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || (UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC && i > DRAW_UBO_SCENE) ?
              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC :
              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      updateDescriptors[i].dstSet          = m_drawing.getSet(0);
      updateDescriptors[i].dstBinding      = i;
      updateDescriptors[i].dstArrayElement = 0;
      updateDescriptors[i].descriptorCount = 1;
      updateDescriptors[i].pBufferInfo     = descriptors + i;
    }
    vkUpdateDescriptorSets(m_device, DRAW_UBOS_NUM, updateDescriptors, 0, 0);

///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW

    VkWriteDescriptorSet updateDescriptors[1] = {};
    for(int i = 0; i < 1; i++)
    {
      updateDescriptors[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      updateDescriptors[i].pNext           = NULL;
      updateDescriptors[i].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      updateDescriptors[i].dstSet          = m_drawing.getSet(0);
      updateDescriptors[i].dstBinding      = i;
      updateDescriptors[i].dstArrayElement = 0;
      updateDescriptors[i].descriptorCount = 1;
      updateDescriptors[i].pBufferInfo     = descriptors + i;
    }
    vkUpdateDescriptorSets(m_device, 1, updateDescriptors, 0, 0);
///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX

    // slightly different
    descriptors[DRAW_UBO_MATRIX]   = views.matricesFull;
    descriptors[DRAW_UBO_MATERIAL] = views.materialsFull;

    VkWriteDescriptorSet updateDescriptors[DRAW_UBOS_NUM] = {};
    for(int i = 0; i < DRAW_UBOS_NUM; i++)
    {
      updateDescriptors[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      updateDescriptors[i].pNext = NULL;
      updateDescriptors[i].descriptorType =
          (i == DRAW_UBO_MATRIX || i == DRAW_UBO_MATERIAL) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      updateDescriptors[i].dstSet          = m_drawing.getSet(0);
      updateDescriptors[i].dstBinding      = i;
      updateDescriptors[i].dstArrayElement = 0;
      updateDescriptors[i].descriptorCount = 1;
      updateDescriptors[i].pBufferInfo     = descriptors + i;
    }
    vkUpdateDescriptorSets(m_device, DRAW_UBOS_NUM, updateDescriptors, 0, 0);

///////////////////////////////////////////////////////////////////////////////////////////
#endif
  }

  {
    // here we use the utilitys of the container class
    // animation
    VkWriteDescriptorSet updateDescriptors[] = {
      m_anim.getWrite(0, ANIM_UBO, &views.anim),
      m_anim.getWrite(0, ANIM_SSBO_MATRIXOUT, &views.matricesFull),
      m_anim.getWrite(0, ANIM_SSBO_MATRIXORIG, &views.matricesFullOrig),
    };
    vkUpdateDescriptorSets(m_device, NV_ARRAY_SIZE(updateDescriptors), updateDescriptors, 0, 0);
  }

  return true;
}

void ResourcesVK::deinitScene()
{
  // guard by synchronization as some stuff is unsafe to delete while in use
  synchronize();

  vkDestroyBuffer(m_device, buffers.scene, NULL);
  vkDestroyBuffer(m_device, buffers.anim, NULL);
  vkDestroyBuffer(m_device, buffers.matrices, NULL);
  vkDestroyBuffer(m_device, buffers.matricesOrig, NULL);
  vkDestroyBuffer(m_device, buffers.materials, NULL);

  vkFreeMemory(m_device, mem.anim, NULL);
  vkFreeMemory(m_device, mem.scene, NULL);
  vkFreeMemory(m_device, mem.matrices, NULL);
  vkFreeMemory(m_device, mem.matricesOrig, NULL);
  vkFreeMemory(m_device, mem.materials, NULL);

  for(size_t i = 0; i < m_geometry.size(); i++)
  {
    Geometry& geom = m_geometry[i];
    if(geom.cloneIdx < 0)
    {
#if !USE_SINGLE_GEOMETRY_BUFFERS
      vkDestroyBuffer(m_device, geom.vbo.buffer, NULL);
      vkDestroyBuffer(m_device, geom.ibo.buffer, NULL);
#endif
    }
  }
#if USE_SINGLE_GEOMETRY_BUFFERS
  vkDestroyBuffer(m_device, buffers.vbo, NULL);
  vkDestroyBuffer(m_device, buffers.ibo, NULL);
#endif
  vkFreeMemory(m_device, mem.vbo, NULL);
  vkFreeMemory(m_device, mem.ibo, NULL);

#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
  m_drawing.deinitPools();
#else
  m_drawing.deinitPool();
#endif
}

void ResourcesVK::synchronize()
{
  vkDeviceWaitIdle(m_device);
}

nvmath::mat4f ResourcesVK::perspectiveProjection(float fovy, float aspect, float nearPlane, float farPlane) const
{
  // vulkan uses DX style 0,1 z clipspace

  nvmath::mat4f M;
  float         r, l, b, t;
  float         f = farPlane;
  float         n = nearPlane;

  t = n * tanf(fovy * nv_to_rad * (0.5f));
  b = -t;

  l = b * aspect;
  r = t * aspect;

  M.a00 = (2.0f * n) / (r - l);
  M.a10 = 0.0f;
  M.a20 = 0.0f;
  M.a30 = 0.0f;

  M.a01 = 0.0f;
  M.a11 = -(2.0f * n) / (t - b);
  M.a21 = 0.0f;
  M.a31 = 0.0f;

  M.a02 = (r + l) / (r - l);
  M.a12 = (t + b) / (t - b);
  M.a22 = -(f) / (f - n);
  M.a32 = -1.0f;

  M.a03 = 0.0;
  M.a13 = 0.0;
  M.a23 = (f * n) / (n - f);
  M.a33 = 0.0;

  return M;
}

void ResourcesVK::animation(const Global& global)
{
  VkCommandBuffer cmd = createTempCmdBuffer();

  vkCmdUpdateBuffer(cmd, buffers.anim, 0, sizeof(AnimationData), (const uint32_t*)&global.animUbo);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipes.compute_animation);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_anim.getPipeLayout(), 0, 1, m_anim.getSets(), 0, 0);
  vkCmdDispatch(cmd, (m_numMatrices + ANIMATION_WORKGROUPSIZE - 1) / ANIMATION_WORKGROUPSIZE, 1, 1);

  VkBufferMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  memBarrier.dstAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
  memBarrier.srcAccessMask         = VK_ACCESS_UNIFORM_READ_BIT;
  memBarrier.buffer                = buffers.matrices;
  memBarrier.size                  = sizeof(AnimationData) * m_numMatrices;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_FALSE, 0, NULL,
                       1, &memBarrier, 0, NULL);

  vkEndCommandBuffer(cmd);

  submissionEnqueue(cmd);
}

void ResourcesVK::animationReset()
{
  VkCommandBuffer cmd = createTempCmdBuffer();
  VkBufferCopy    copy;
  copy.size      = sizeof(MatrixData) * m_numMatrices;
  copy.dstOffset = 0;
  copy.srcOffset = 0;
  vkCmdCopyBuffer(cmd, buffers.matricesOrig, buffers.matrices, 1, &copy);
  vkEndCommandBuffer(cmd);

  submissionEnqueue(cmd);
}
}  // namespace csfthreaded
