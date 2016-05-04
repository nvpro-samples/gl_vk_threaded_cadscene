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


#include <assert.h>
#include <algorithm>
#include "renderer.hpp"
#include <main.h>
#include "resources_vk.hpp"

#include <nv_math/nv_math_glsltypes.h>

using namespace nv_math;
#include "common.h"


namespace csfthreaded
{

  //////////////////////////////////////////////////////////////////////////

  
  class RendererVK: public Renderer {
  public:
    enum Mode {
      MODE_CMD_SINGLE,
      MODE_CMD_MANY,
    };


    class TypeCmd : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVK::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan re-use cmd";
      }
      Renderer* create() const
      {
        RendererVK* renderer = new RendererVK();
        renderer->m_mode = MODE_CMD_SINGLE;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 8;
      }

      Resources* resources()
      {
        return ResourcesVK::get();
      }
    };

    class TypeCmdMany : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVK::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan re-use obj-level cmd";
      }
      Renderer* create() const
      {
        RendererVK* renderer = new RendererVK();
        renderer->m_mode = MODE_CMD_MANY;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 8;
      }

      Resources* resources()
      {
        return ResourcesVK::get();
      }
    };

  public:

    void init(const CadScene* NVP_RESTRICT scene, Resources* resources);
    void deinit();
    void draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager);

    void blit(ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global );

    Mode            m_mode;

    RendererVK()
      : m_mode(MODE_CMD_SINGLE) 
    {

    }

  private:

    struct ShadeCommand {
      std::vector<VkCommandBuffer>      cmdbuffers;
      size_t                            fboIncarnation;
      size_t                            pipeIncarnation;
    };

    std::vector<DrawItem>               m_drawItems;
    VkCommandPool                       m_cmdPool;

    // used for token or cmdbuffer
    ShadeCommand                    m_shades[NUM_SHADES];
    const ResourcesVK* NVP_RESTRICT m_resources;

    void GenerateCmdBuffers(ShadeCommand& sc, ShadeType shadetype, const DrawItem* NVP_RESTRICT drawItems, size_t num,  const ResourcesVK* NVP_RESTRICT res )
    {
      const CadScene* NVP_RESTRICT scene = m_scene;
      bool solidwire = (shadetype == SHADE_SOLIDWIRE);

      int lastMaterial = -1;
      int lastGeometry = -1;
      int lastMatrix   = -1;
      int lastObject   = -1;
      bool lastSolid   = true;

      sc.cmdbuffers.clear();

      VkCommandBuffer  cmd = NULL;
      VkRenderPass pass = NULL;

      bool first = true;
      for (unsigned int i = 0; i < num; i++){
        const DrawItem& di = drawItems[i];

        if (shadetype == SHADE_SOLID && !di.solid){
          if (res->m_sorted && (m_mode != MODE_CMD_MANY)) break;
          continue;
        }

        if ( !cmd || (m_mode == MODE_CMD_MANY && di.objectIndex != lastObject)){

          if (cmd){
            vkEndCommandBuffer(cmd);

            sc.cmdbuffers.push_back(cmd);
          }

          cmd  = res->createCmdBuffer(m_cmdPool, false, false, true);
          res->cmdDynamicState(cmd);

          lastMaterial = -1;
          lastGeometry = -1;
          lastMatrix   = -1;
          first = true;

          lastObject = di.objectIndex;
        }

        if (first || (di.solid != lastSolid))
        {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            di.solid ? ( solidwire ? res->pipes.line_tris : res->pipes.tris) : 
                        res->pipes.line);
        
          if (first){
            #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
              vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
                UBO_SCENE, 1, &res->m_descriptorSet[UBO_SCENE], 0, NULL);
            #endif
            #if UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW || UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
              vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
                UBO_SCENE, 1, &res->m_descriptorSet, 0, NULL);
            #endif
          }

          first = false;
        }

        if (lastGeometry != di.geometryIndex){
          const ResourcesVK::Geometry &glgeo = res->m_geometry[di.geometryIndex];

          vkCmdBindVertexBuffers(cmd, 0, 1, &glgeo.vbo, &glgeo.vboOffset);
          vkCmdBindIndexBuffer  (cmd, glgeo.ibo, glgeo.iboOffset, VK_INDEX_TYPE_UINT32);

          lastGeometry = di.geometryIndex;
        }

      ///////////////////////////////////////////////////////////////////////////////////////////
      #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
        if (lastMatrix != di.matrixIndex)
        {
        #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
          uint32_t offset = di.matrixIndex    * res->m_alignedMatrixSize;
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATRIX, 1, &res->m_descriptorSet[UBO_MATRIX], 1, &offset);
        #else
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATRIX, 1, &res->m_descriptorSetsMatrices[di.matrixIndex], 0, NULL);
        #endif
          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex)
        {
        #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
          uint32_t offset = di.materialIndex    * res->m_alignedMaterialSize;
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATERIAL, 1, &res->m_descriptorSet[UBO_MATERIAL], 1, &offset);
        #else
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATERIAL, 1, &res->m_descriptorSetsMaterials[di.materialIndex], 0, NULL);
        #endif
          lastMaterial = di.materialIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC

        if (lastMaterial != di.materialIndex ||
            lastMatrix   != di.matrixIndex)
        {
        #if UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
          uint32_t offsets[UBOS_NUM];
          offsets[UBO_SCENE]    = 0;
          offsets[UBO_MATRIX]   = di.matrixIndex    * res->m_alignedMatrixSize;
          offsets[UBO_MATERIAL] = di.materialIndex  * res->m_alignedMaterialSize;

        #elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
          uint32_t offsets[UBOS_NUM-1];
          offsets[UBO_MATRIX-1]   = di.matrixIndex    * res->m_alignedMatrixSize;
          offsets[UBO_MATERIAL-1] = di.materialIndex  * res->m_alignedMaterialSize;
        #endif
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            0, 1, &res->m_descriptorSet, sizeof(offsets)/sizeof(offsets[0]),offsets);

          lastMaterial = di.materialIndex;
          lastMatrix   = di.matrixIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
        if (lastMatrix != di.matrixIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(ObjectData), &scene->m_matrices[di.matrixIndex]);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(ObjectData),sizeof(MaterialData), &scene->m_materials[di.materialIndex]);

          lastMaterial = di.materialIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
        if (lastMatrix != di.matrixIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(int), &di.matrixIndex);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(int),sizeof(int), &di.materialIndex);

          lastMaterial = di.materialIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #endif
        // drawcall
        vkCmdDrawIndexed(cmd, di.range.count, 1, uint32_t(di.range.offset / sizeof(uint32_t)), 0, 0);

        lastSolid = di.solid;
      }

      if (cmd){
        vkEndCommandBuffer(cmd);

        sc.cmdbuffers.push_back(cmd);
      }

      sc.fboIncarnation = res->m_fboIncarnation;
      sc.pipeIncarnation = res->m_pipeIncarnation;
    }

    void DeleteCmdbuffers(ShadeType shadetype)
    {
      ShadeCommand& sc = m_shades[shadetype];
      vkFreeCommandBuffers( m_resources->m_device, m_cmdPool, (uint32_t)sc.cmdbuffers.size(), &sc.cmdbuffers[0]);
      sc.cmdbuffers.clear();
    }

   };


  static RendererVK::TypeCmd s_type_cmdbuffer_vk;
  static RendererVK::TypeCmdMany s_type_cmdbuffer_vkmany;

  void RendererVK::init(const CadScene* NVP_RESTRICT scene, Resources* resources)
  {
    const ResourcesVK* res = (const ResourcesVK*) resources;
    m_scene = scene;
    m_resources = res;

    VkResult result;
    VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmdPoolInfo.queueFamilyIndex = 0;
    result = vkCreateCommandPool(res->m_device, &cmdPoolInfo, NULL, &m_cmdPool);
    assert(result == VK_SUCCESS);

    fillDrawItems(m_drawItems,resources->m_percent, true, true);

    printf("drawitems: %d\n", uint32_t(m_drawItems.size()));

    if (resources->m_sorted && m_mode != MODE_CMD_MANY){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }

    for (int i = 0; i < NUM_SHADES; i++){
      GenerateCmdBuffers(m_shades[i], (ShadeType)i, &m_drawItems[0], m_drawItems.size(), res);
      printf("cmdbuffers %s: %7d\n",toString((ShadeType)i), m_shades[i].cmdbuffers.size());
    }
  }

  void RendererVK::deinit()
  {
    for (int i = 0; i < NUM_SHADES; i++){
      DeleteCmdbuffers((ShadeType)i);
    }
    vkDestroyCommandPool(m_resources->m_device, m_cmdPool, NULL);
  }

  void RendererVK::draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager)
  {
    const CadScene* NVP_RESTRICT scene = m_scene;
    ResourcesVK* res = (ResourcesVK*)resources;

    ShadeCommand &sc = m_shades[shadetype];

    if (sc.pipeIncarnation != res->m_pipeIncarnation ||
        sc.fboIncarnation  != res->m_fboIncarnation)
    {
      DeleteCmdbuffers(shadetype);
      GenerateCmdBuffers(sc, shadetype, &m_drawItems[0], m_drawItems.size(), res);
    }

    
    // generic state setup
    VkCommandBuffer primary = res->createTempCmdBuffer();
    vkCmdUpdateBuffer(primary, res->buffers.scene, 0, sizeof(SceneData), (const uint32_t*)&global.sceneUbo);
    res->cmdPipelineBarrier(primary);

    // clear via pass
    res->cmdBeginRenderPass(primary, true, true);
    if (!sc.cmdbuffers.empty()){
      vkCmdExecuteCommands(primary, uint32_t(sc.cmdbuffers.size()), &sc.cmdbuffers[0] );
    }
    vkCmdEndRenderPass(primary);
    vkEndCommandBuffer(primary);


    res->submissionEnqueue(primary);;
    res->tempdestroyEnqueue(primary);
  }

  void RendererVK::blit( ShadeType shadeType, Resources* resources, const Resources::Global& global )
  {
    ResourcesVK* res = (ResourcesVK*)resources;

    if (res->m_msaa){
      VkCommandBuffer cmd = res->createTempCmdBuffer();
      VkImageResolve region = {0};
      region.extent.width  = res->m_width;
      region.extent.height = res->m_height;
      region.extent.depth  = 1;
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.dstSubresource.layerCount = 1;
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.srcSubresource.layerCount = 1;

      vkCmdResolveImage(cmd,  res->images.scene_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                              res->images.scene_color_resolved, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      vkEndCommandBuffer(cmd);

      res->submissionEnqueue(cmd);
      res->tempdestroyEnqueue(cmd);
    }

    res->flushFrame();

    // blit to gl backbuffer
    glDisable(GL_DEPTH_TEST);
    glWaitVkSemaphoreNV((GLuint64)res->m_semImageWritten);
    glDrawVkImageNV((GLuint64)(VkImage)(res->m_msaa ? res->images.scene_color_resolved : res->images.scene_color), 0,
      0,0,res->m_width,res->m_height, 0,
      0,1,1,0);
    glEnable(GL_DEPTH_TEST);
    glSignalVkSemaphoreNV((GLuint64)res->m_semImageRead);
  }


}


