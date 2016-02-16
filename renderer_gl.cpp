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
#include "resources_gl.hpp"

#include <nv_math/nv_math_glsltypes.h>

using namespace nv_math;
#include "common.h"

namespace csfthreaded
{

  //////////////////////////////////////////////////////////////////////////

  

  class RendererGL: public Renderer {
  public:
    class Type : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return true;
      }
      const char* name() const
      {
        return "GL core";
      }
      Renderer* create() const
      {
        RendererGL* renderer = new RendererGL();
        return renderer;
      }

      Resources* resources()
      {
        return ResourcesGL::get();
      }
      
      unsigned int priority() const 
      {
        return 0;
      }
    };
    class TypeVbum : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return !!GLEW_NV_vertex_buffer_unified_memory;
      }
      const char* name() const
      {
        return "GL nvbindless";
      }
      Renderer* create() const
      {
        RendererGL* renderer = new RendererGL();
        renderer->m_vbum = true;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 0;
      }

      Resources* resources()
      {
        return ResourcesGL::get();
      }
    };

  public:

    void init(const CadScene* NVP_RESTRICT scene, Resources* resources);
    void deinit();
    void draw(ShadeType shadetype, Resources*  NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager);

    void blit(ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global );


    bool                        m_vbum;
    bool                        m_bindless_ubo;

    RendererGL()
      : m_vbum(false) 
      , m_bindless_ubo(false)
    {

    }

  private:

    std::vector<DrawItem> m_drawItems;

    void SetWireMode( bool state, const ResourcesGL* res, ShadeType shadeType)
    {
      glUseProgram( state ? res->programs.draw_line : res->programs.draw_solid);
    }

  };

  static RendererGL::Type s_uborange;
  static RendererGL::TypeVbum s_uborange_vbum;

  void RendererGL::init(const CadScene* NVP_RESTRICT scene, Resources* resources)
  {
    m_scene = scene;
    m_bindless_ubo = ((ResourcesGL*)resources)->m_bindless_ubo;

    fillDrawItems(m_drawItems,resources->m_percent, true, true);

    if (resources->m_sorted){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }
  }

  void RendererGL::deinit()
  {

  }

  void RendererGL::draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager)
  {
    const CadScene* NVP_RESTRICT scene = m_scene;
    const ResourcesGL* NVP_RESTRICT res = (ResourcesGL*)resources;

    bool vbum = m_vbum;

    // generic state setup
    glViewport(0, 0, global.width, global.height);

    glBindFramebuffer(GL_FRAMEBUFFER, res->fbos.scene);
    glClearColor(0.2f,0.2f,0.2f,0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    SetWireMode(false,res,shadetype);

    glNamedBufferSubDataEXT(res->buffers.scene,0,sizeof(SceneData),&global.sceneUbo);

    res->enableVertexFormat(VERTEX_POS,VERTEX_NORMAL);

    if (shadetype == SHADE_SOLIDWIRE){
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1,1);
    }

    if (vbum){
      glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
      glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);

      glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV,0,0,0);
      glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV,0,0,0);

      if (m_bindless_ubo){
        glEnableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
        glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_SCENE,0,0);
        glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATRIX,0,0);
        glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATERIAL,0,0);
      }
    }

    if (vbum && m_bindless_ubo){
      glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_SCENE, res->addresses.scene,sizeof(SceneData));
    }
    else{
      glBindBufferBase(GL_UNIFORM_BUFFER,UBO_SCENE, res->buffers.scene);
    }

#if USE_INDEXING
    // dor debugging purposes only
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,UBO_MATERIAL, res->buffers.materials);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,UBO_MATRIX,   res->buffers.matrices);
#endif

    {
      int lastMaterial = -1;
      int lastGeometry = -1;
      int lastMatrix   = -1;
      bool lastSolid   = true;

      int statsGeometry = 0;
      int statsMatrix   = 0;
      int statsMaterial = 0;
      int statsDraw     = 0;
      int statsWireMode = 0;

      GLenum mode = GL_TRIANGLES;

      for (int i = 0; i < m_drawItems.size(); i++){
        const DrawItem& di = m_drawItems[i];

        if (shadetype == SHADE_SOLID && !di.solid){
          if (res->m_sorted) break;
          continue;
        }

        if (lastSolid != di.solid){
          SetWireMode( di.solid ? false : true, res, shadetype );
          statsWireMode++;
        }

        if (lastGeometry != di.geometryIndex){
          const ResourcesGL::Geometry &geogl = res->m_geometry[di.geometryIndex];

          if (vbum){
            glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0,  geogl.vboADDR, geogl.vboSize);
            glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV,0,         geogl.iboADDR, geogl.iboSize);
          }
          else{
            glBindVertexBuffer(0, geogl.vboGL, 0, sizeof(CadScene::Vertex));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geogl.iboGL);
          }

          lastGeometry = di.geometryIndex;

          statsGeometry++;
        }

        if (lastMatrix != di.matrixIndex){

          if (vbum && m_bindless_ubo){
            glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATRIX, res->addresses.matrices + res->m_alignedMatrixSize * di.matrixIndex, sizeof(CadScene::MatrixNode));
          }
          else{
            glBindBufferRange(GL_UNIFORM_BUFFER,UBO_MATRIX, res->buffers.matrices, res->m_alignedMatrixSize * di.matrixIndex, sizeof(CadScene::MatrixNode));
          }

          lastMatrix = di.matrixIndex;

          statsMatrix++;
        }

        if (lastMaterial != di.materialIndex){

          if (m_vbum && m_bindless_ubo){
            glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATERIAL, res->addresses.materials + res->m_alignedMaterialSize * di.materialIndex, sizeof(CadScene::Material));
          }
          else{
            glBindBufferRange(GL_UNIFORM_BUFFER,UBO_MATERIAL, res->buffers.materials, res->m_alignedMaterialSize * di.materialIndex, sizeof(CadScene::Material));
          }

          lastMaterial = di.materialIndex;

          statsMaterial++;
        }

        glDrawElements( di.solid ? GL_TRIANGLES : GL_LINES, di.range.count, GL_UNSIGNED_INT, (void*) di.range.offset);

        lastSolid = di.solid;

        statsDraw++;
      }

      statsGeometry;
      statsMatrix;
      statsMaterial;
      statsDraw;
      statsWireMode = statsWireMode;
    }

    glBindBufferBase(GL_UNIFORM_BUFFER,UBO_SCENE, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER,UBO_MATRIX, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER,UBO_MATERIAL, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glBindVertexBuffer(0,0,0,0);

    if (m_vbum){
      glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
      glDisableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
      if (m_bindless_ubo){
        glDisableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
      }
    }

    if (shadetype == SHADE_SOLIDWIRE){
      glDisable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(0,0);
    }

    res->disableVertexFormat(VERTEX_POS,VERTEX_NORMAL);

    glBindFramebuffer(GL_FRAMEBUFFER,0);
  }

  void RendererGL::blit( ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global )
  {
    ResourcesGL* res = (ResourcesGL*)resources;

    int width   = global.width;
    int height  = global.height;

    // blit to background
    glBindFramebuffer(GL_READ_FRAMEBUFFER, res->fbos.scene);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,width,height,
      0,0,width,height,GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

}
