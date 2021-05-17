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


#include "renderer.hpp"
#include "resources_gl.hpp"
#include <algorithm>
#include <assert.h>

#include <nvmath/nvmath_glsltypes.h>

#include "common.h"

namespace csfthreaded {

//////////////////////////////////////////////////////////////////////////


class RendererGL : public Renderer
{
public:
  class Type : public Renderer::Type
  {
    bool        isAvailable() const { return true; }
    const char* name() const { return "GL core"; }
    Renderer*   create() const
    {
      RendererGL* renderer = new RendererGL();
      return renderer;
    }

    Resources* resources() { return ResourcesGL::get(); }

    unsigned int priority() const { return 0; }
  };
  class TypeVbum : public Renderer::Type
  {
    bool        isAvailable() const { return !!has_GL_NV_vertex_buffer_unified_memory; }
    const char* name() const { return "GL nvbindless"; }
    Renderer*   create() const
    {
      RendererGL* renderer = new RendererGL();
      renderer->m_vbum     = true;
      return renderer;
    }
    unsigned int priority() const { return 0; }

    Resources* resources() { return ResourcesGL::get(); }
  };

public:
  void init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config);
  void deinit();
  void draw(ShadeType shadetype, Resources* NV_RESTRICT resources, const Resources::Global& global);

  bool m_vbum;
  bool m_bindless_ubo;

  RendererGL()
      : m_vbum(false)
      , m_bindless_ubo(false)
  {
  }

private:
  std::vector<DrawItem> m_drawItems;

  void SetWireMode(bool state, const ResourcesGL* res, ShadeType shadeType)
  {
    glUseProgram(state ? res->m_programs.draw_line : res->m_programs.draw_solid);
  }
};

static RendererGL::Type     s_uborange;
static RendererGL::TypeVbum s_uborange_vbum;

void RendererGL::init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config)
{
  m_scene        = scene;
  m_bindless_ubo = ((ResourcesGL*)resources)->m_bindless_ubo;

  fillDrawItems(m_drawItems, config, true, true);

  if(config.sorted)
  {
    std::sort(m_drawItems.begin(), m_drawItems.end(), DrawItem_compare_groups);
  }
}

void RendererGL::deinit() {}

void RendererGL::draw(ShadeType shadetype, Resources* NV_RESTRICT resources, const Resources::Global& global)
{
  ResourcesGL* NV_RESTRICT res      = (ResourcesGL*)resources;
  const CadSceneGL&        sceneGL  = res->m_scene;

  const nvgl::ProfilerGL::Section profile(res->m_profilerGL, "Render");

  bool vbum = m_vbum;

  // generic state setup
  glViewport(0, 0, global.winWidth, global.winHeight);

  glBindFramebuffer(GL_FRAMEBUFFER, res->m_framebuffer.fboScene);
  glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  SetWireMode(false, res, shadetype);

  glNamedBufferSubData(res->m_common.view.buffer, 0, sizeof(SceneData), &global.sceneUbo);

  res->enableVertexFormat();

  if(shadetype == SHADE_SOLIDWIRE)
  {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1, 1);
  }

  if(vbum)
  {
    glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);

    glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0, 0, 0);
    glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV, 0, 0, 0);

    if(m_bindless_ubo)
    {
      glEnableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
      glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_SCENE, 0, 0);
      glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_MATRIX, 0, 0);
      glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_MATERIAL, 0, 0);
    }
  }

  if(vbum && m_bindless_ubo)
  {
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_SCENE, res->m_common.view.bufferADDR, sizeof(SceneData));
  }
  else
  {
    glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_UBO_SCENE, res->m_common.view.buffer);
  }

  {
    int  lastMaterial = -1;
    int  lastGeometry = -1;
    int  lastMatrix   = -1;
    bool lastSolid    = true;

    int statsGeometry = 0;
    int statsMatrix   = 0;
    int statsMaterial = 0;
    int statsDraw     = 0;
    int statsWireMode = 0;

    GLenum mode = GL_TRIANGLES;

    for(int i = 0; i < m_drawItems.size(); i++)
    {
      const DrawItem& di = m_drawItems[i];

      if(shadetype == SHADE_SOLID && !di.solid)
      {
        if(m_config.sorted)
          break;
        continue;
      }

      if(lastSolid != di.solid)
      {
        SetWireMode(di.solid ? false : true, res, shadetype);
        statsWireMode++;
      }

      const CadSceneGL::Geometry& geo = sceneGL.m_geometry[di.geometryIndex];
      size_t iboOffset = vbum ? 0 : geo.ibo.offset;

      if(lastGeometry != di.geometryIndex)
      {
        if(vbum)
        {
          glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0, geo.vbo.bufferADDR, geo.vbo.size);
          glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV, 0, geo.ibo.bufferADDR, geo.ibo.size);
        }
        else
        {
          glBindVertexBuffer(0, geo.vbo.buffer, geo.vbo.offset, sizeof(CadScene::Vertex));
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geo.ibo.buffer);
        }

        lastGeometry = di.geometryIndex;

        statsGeometry++;
      }

      if(lastMatrix != di.matrixIndex)
      {

        if(vbum && m_bindless_ubo)
        {
          glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_MATRIX,
                                 sceneGL.m_buffers.matrices.bufferADDR + res->m_alignedMatrixSize * di.matrixIndex,
                                 sizeof(CadScene::MatrixNode));
        }
        else
        {
          glBindBufferRange(GL_UNIFORM_BUFFER, DRAW_UBO_MATRIX, sceneGL.m_buffers.matrices.buffer,
                            res->m_alignedMatrixSize * di.matrixIndex, sizeof(CadScene::MatrixNode));
        }

        lastMatrix = di.matrixIndex;

        statsMatrix++;
      }

      if(lastMaterial != di.materialIndex)
      {

        if(m_vbum && m_bindless_ubo)
        {
          glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_MATERIAL,
                                 sceneGL.m_buffers.materials.bufferADDR + res->m_alignedMaterialSize * di.materialIndex,
                                 sizeof(CadScene::Material));
        }
        else
        {
          glBindBufferRange(GL_UNIFORM_BUFFER, DRAW_UBO_MATERIAL, sceneGL.m_buffers.materials.buffer,
                            res->m_alignedMaterialSize * di.materialIndex, sizeof(CadScene::Material));
        }

        lastMaterial = di.materialIndex;

        statsMaterial++;
      }

      glDrawElements(di.solid ? GL_TRIANGLES : GL_LINES, di.range.count, GL_UNSIGNED_INT, (void*)(di.range.offset + iboOffset));

      lastSolid = di.solid;

      statsDraw++;
    }

    (void)statsGeometry;
    (void)statsMatrix;
    (void)statsMaterial;
    (void)statsDraw;
//    statsWireMode = statsWireMode;
  }

  glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_UBO_SCENE, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_UBO_MATRIX, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, DRAW_UBO_MATERIAL, 0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindVertexBuffer(0, 0, 0, 0);

  if(m_vbum)
  {
    glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glDisableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
    if(m_bindless_ubo)
    {
      glDisableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
    }
  }

  if(shadetype == SHADE_SOLIDWIRE)
  {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0, 0);
  }

  res->disableVertexFormat();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace csfthreaded
