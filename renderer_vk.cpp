/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */


#include <algorithm>
#include <assert.h>

#include "renderer.hpp"
#include "resources_vk.hpp"

#include <nvh/nvprint.hpp>
#include <nvmath/nvmath_glsltypes.h>

#include "common.h"


namespace csfthreaded {

//////////////////////////////////////////////////////////////////////////


class RendererVK : public Renderer
{
public:
  enum Mode
  {
    // one secondary commandbuffer captures entire scene
    MODE_CMD_SINGLE,

    // one secondary commandbuffer per object
    // this technique is only here for comparison, the performance is expected to be really bad
    MODE_CMD_MANY,
  };


  class TypeCmd : public Renderer::Type
  {
    bool        isAvailable() const { return ResourcesVK::isAvailable(); }
    const char* name() const { return "Vulkan re-use cmd"; }
    Renderer*   create() const
    {
      RendererVK* renderer = new RendererVK();
      renderer->m_mode     = MODE_CMD_SINGLE;
      return renderer;
    }
    unsigned int priority() const { return 8; }

    Resources* resources() { return ResourcesVK::get(); }
  };

  class TypeCmdMany : public Renderer::Type
  {
    bool        isAvailable() const { return ResourcesVK::isAvailable(); }
    const char* name() const { return "Vulkan re-use obj-level cmd"; }
    Renderer*   create() const
    {
      RendererVK* renderer = new RendererVK();
      renderer->m_mode     = MODE_CMD_MANY;
      return renderer;
    }
    unsigned int priority() const { return 8; }

    Resources* resources() { return ResourcesVK::get(); }
  };

public:
  void init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config);
  void deinit();
  void draw(ShadeType shadetype, Resources* NV_RESTRICT resources, const Resources::Global& global);


  Mode m_mode;

  RendererVK()
      : m_mode(MODE_CMD_SINGLE)
  {
  }

private:
  struct ShadeCommand
  {
    std::vector<VkCommandBuffer> cmdbuffers;
    size_t                       fboChangeID;
    size_t                       pipeChangeID;
  };

  std::vector<DrawItem> m_drawItems;
  VkCommandPool         m_cmdPool;

  // used for token or cmdbuffer
  ShadeCommand       m_shades[NUM_SHADES];
  const ResourcesVK* NV_RESTRICT m_resources;

  void GenerateCmdBuffers(ShadeCommand& sc, ShadeType shadetype, const DrawItem* NV_RESTRICT drawItems, size_t num, const ResourcesVK* NV_RESTRICT res)
  {
    const CadScene* NV_RESTRICT scene   = m_scene;
    const CadSceneVK&           sceneVK = res->m_scene;

    bool solidwire = (shadetype == SHADE_SOLIDWIRE);

    int  lastMaterial = -1;
    int  lastGeometry = -1;
    int  lastMatrix   = -1;
    int  lastObject   = -1;
    bool lastSolid    = true;

    sc.cmdbuffers.clear();

    VkCommandBuffer cmd  = NULL;
    VkRenderPass    pass = NULL;

    bool first = true;
    for(unsigned int i = 0; i < num; i++)
    {
      const DrawItem& di = drawItems[i];

      if(shadetype == SHADE_SOLID && !di.solid)
      {
        if(m_config.sorted && (m_mode != MODE_CMD_MANY))
          break;
        continue;
      }

      if(!cmd || (m_mode == MODE_CMD_MANY && di.objectIndex != lastObject))
      {

        if(cmd)
        {
          vkEndCommandBuffer(cmd);

          sc.cmdbuffers.push_back(cmd);
        }

        cmd = res->createCmdBuffer(m_cmdPool, false, false, true);
        res->cmdDynamicState(cmd);

        lastMaterial = -1;
        lastGeometry = -1;
        lastMatrix   = -1;
        first        = true;

        lastObject = di.objectIndex;
      }

      if(first || (di.solid != lastSolid))
      {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          di.solid ? (solidwire ? res->m_pipes.line_tris : res->m_pipes.tris) : res->m_pipes.line);

        if(first)
        {
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), DRAW_UBO_SCENE,
                                  1, res->m_drawing.at(DRAW_UBO_SCENE).getSets(), 0, NULL);
#endif
#if UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW || UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), DRAW_UBO_SCENE,
                                  1, res->m_drawing.getSets(), 0, NULL);
#endif
        }

        first = false;
      }

      if(lastGeometry != di.geometryIndex)
      {
        const CadSceneVK::Geometry& vkgeo = sceneVK.m_geometry[di.geometryIndex];

        vkCmdBindVertexBuffers(cmd, 0, 1, &vkgeo.vbo.buffer, &vkgeo.vbo.offset);
        vkCmdBindIndexBuffer(cmd, vkgeo.ibo.buffer, vkgeo.ibo.offset, VK_INDEX_TYPE_UINT32);

        lastGeometry = di.geometryIndex;
      }

///////////////////////////////////////////////////////////////////////////////////////////
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
      if(lastMatrix != di.matrixIndex)
      {
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
        uint32_t offset = di.matrixIndex * res->m_alignedMatrixSize;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), DRAW_UBO_MATRIX,
                                1, res->m_drawing.at(DRAW_UBO_MATRIX).getSets(), 1, &offset);
#else
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), DRAW_UBO_MATRIX,
                                1, res->m_drawing.at(DRAW_UBO_MATRIX).getSets() + di.matrixIndex, 0, NULL);
#endif
        lastMatrix = di.matrixIndex;
      }

      if(lastMaterial != di.materialIndex)
      {
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
        uint32_t offset = di.materialIndex * res->m_alignedMaterialSize;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), DRAW_UBO_MATERIAL,
                                1, res->m_drawing.at(DRAW_UBO_MATERIAL).getSets(), 1, &offset);
#else
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), DRAW_UBO_MATERIAL,
                                1, res->m_drawing.at(DRAW_UBO_MATERIAL).getSets() + di.materialIndex, 0, NULL);
#endif
        lastMaterial = di.materialIndex;
      }
///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC

      if(lastMaterial != di.materialIndex || lastMatrix != di.matrixIndex)
      {
#if UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
        uint32_t offsets[DRAW_UBOS_NUM];
        offsets[DRAW_UBO_SCENE]    = 0;
        offsets[DRAW_UBO_MATRIX]   = di.matrixIndex * res->m_alignedMatrixSize;
        offsets[DRAW_UBO_MATERIAL] = di.materialIndex * res->m_alignedMaterialSize;

#elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
        uint32_t offsets[DRAW_UBOS_NUM - 1];
        offsets[DRAW_UBO_MATRIX - 1]   = di.matrixIndex * res->m_alignedMatrixSize;
        offsets[DRAW_UBO_MATERIAL - 1] = di.materialIndex * res->m_alignedMaterialSize;
#endif
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_drawing.getPipeLayout(), 0, 1,
                                res->m_drawing.getSets(), sizeof(offsets) / sizeof(offsets[0]), offsets);

        lastMaterial = di.materialIndex;
        lastMatrix   = di.matrixIndex;
      }
///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
      if(lastMatrix != di.matrixIndex)
      {
        vkCmdPushConstants(cmd, res->m_drawing.getPipeLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ObjectData),
                           &scene->m_matrices[di.matrixIndex]);

        lastMatrix = di.matrixIndex;
      }

      if(lastMaterial != di.materialIndex)
      {
        vkCmdPushConstants(cmd, res->m_drawing.getPipeLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ObjectData),
                           sizeof(MaterialData), &scene->m_materials[di.materialIndex]);

        lastMaterial = di.materialIndex;
      }
///////////////////////////////////////////////////////////////////////////////////////////
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
      if(lastMatrix != di.matrixIndex)
      {
        vkCmdPushConstants(cmd, res->m_drawing.getPipeLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &di.matrixIndex);

        lastMatrix = di.matrixIndex;
      }

      if(lastMaterial != di.materialIndex)
      {
        vkCmdPushConstants(cmd, res->m_drawing.getPipeLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(int), sizeof(int),
                           &di.materialIndex);

        lastMaterial = di.materialIndex;
      }
///////////////////////////////////////////////////////////////////////////////////////////
#endif
      // drawcall
      vkCmdDrawIndexed(cmd, di.range.count, 1, uint32_t(di.range.offset / sizeof(uint32_t)), 0, 0);

      lastSolid = di.solid;
    }

    if(cmd)
    {
      vkEndCommandBuffer(cmd);

      sc.cmdbuffers.push_back(cmd);
    }

    sc.fboChangeID  = res->m_fboChangeID;
    sc.pipeChangeID = res->m_pipeChangeID;
  }

  void DeleteCmdbuffers(ShadeType shadetype)
  {
    ShadeCommand& sc = m_shades[shadetype];
    vkFreeCommandBuffers(m_resources->m_device, m_cmdPool, (uint32_t)sc.cmdbuffers.size(), &sc.cmdbuffers[0]);
    sc.cmdbuffers.clear();
  }
};


static RendererVK::TypeCmd     s_type_cmdbuffer_vk;
static RendererVK::TypeCmdMany s_type_cmdbuffer_vkmany;

void RendererVK::init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config)
{
  const ResourcesVK* res = (const ResourcesVK*)resources;
  m_scene                = scene;
  m_resources            = res;

  VkResult                result;
  VkCommandPoolCreateInfo cmdPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cmdPoolInfo.queueFamilyIndex        = 0;
  result                              = vkCreateCommandPool(res->m_device, &cmdPoolInfo, NULL, &m_cmdPool);
  assert(result == VK_SUCCESS);

  fillDrawItems(m_drawItems, config, true, true);

  LOGI("drawitems: %d\n", uint32_t(m_drawItems.size()));

  if(config.sorted && m_mode != MODE_CMD_MANY)
  {
    std::sort(m_drawItems.begin(), m_drawItems.end(), DrawItem_compare_groups);
  }

  for(int i = 0; i < NUM_SHADES; i++)
  {
    GenerateCmdBuffers(m_shades[i], (ShadeType)i, m_drawItems.data(), m_drawItems.size(), res);
    LOGI("cmdbuffers %s: %9d\n", toString((ShadeType)i), uint32_t(m_shades[i].cmdbuffers.size()));
  }
}

void RendererVK::deinit()
{
  for(int i = 0; i < NUM_SHADES; i++)
  {
    DeleteCmdbuffers((ShadeType)i);
  }
  vkDestroyCommandPool(m_resources->m_device, m_cmdPool, NULL);
}

void RendererVK::draw(ShadeType shadetype, Resources* NV_RESTRICT resources, const Resources::Global& global)
{
  const CadScene* NV_RESTRICT scene = m_scene;
  ResourcesVK*                res   = (ResourcesVK*)resources;

  ShadeCommand& sc = m_shades[shadetype];

  if(sc.pipeChangeID != res->m_pipeChangeID || sc.fboChangeID != res->m_fboChangeID)
  {
    DeleteCmdbuffers(shadetype);
    GenerateCmdBuffers(sc, shadetype, m_drawItems.data(), m_drawItems.size(), res);
  }


  VkCommandBuffer primary = res->createTempCmdBuffer();

  {
    nvvk::ProfilerVK::Section profile(res->m_profilerVK, "Render", primary);

    vkCmdUpdateBuffer(primary, res->m_common.viewBuffer, 0, sizeof(SceneData), (const uint32_t*)&global.sceneUbo);
    res->cmdPipelineBarrier(primary);

    // clear via pass
    res->cmdBeginRenderPass(primary, true, true);
    if(!sc.cmdbuffers.empty())
    {
      vkCmdExecuteCommands(primary, uint32_t(sc.cmdbuffers.size()), sc.cmdbuffers.data());
    }
    vkCmdEndRenderPass(primary);
  }
  vkEndCommandBuffer(primary);
  res->submissionEnqueue(primary);
}

}  // namespace csfthreaded
