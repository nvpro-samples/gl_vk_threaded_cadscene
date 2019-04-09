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

/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#include <assert.h>
#include <algorithm>
#include "renderer.hpp"
#include "resources_gl.hpp"

#include <nvmath/nvmath_glsltypes.h>

#include <nvgl/contextwindow_gl.hpp>

using namespace nvmath;
#include "common.h"

namespace csfthreaded
{

  //////////////////////////////////////////////////////////////////////////

  class RendererGLCMD: public Renderer {
  public:

    enum Mode {
      MODE_BUFFER,
      MODE_LIST,
      MODE_LIST_RECOMPILE,
    };

    class Type : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return !!load_GL_NV_command_list(nvgl::ContextWindowGL::sysGetProcAddress);
      }
      const char* name() const
      {
        return "GL re-use nvcmd buffer";
      }
      Renderer* create() const
      {
        RendererGLCMD* renderer = new RendererGLCMD();
        renderer->m_mode = MODE_BUFFER;
        return renderer;
      }

      Resources* resources()
      {
        return ResourcesGL::get();
      }
      
      unsigned int priority() const 
      {
        return 4;
      }
    };
    class TypeVbum : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return !!load_GL_NV_command_list(nvgl::ContextWindowGL::sysGetProcAddress);
      }
      const char* name() const
      {
        return "GL re-use nvcmd compiled list";
      }
      Renderer* create() const
      {
        RendererGLCMD* renderer = new RendererGLCMD();
        renderer->m_mode = MODE_LIST;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 4;
      }

      Resources* resources()
      {
        return ResourcesGL::get();
      }
    };
    class TypeRecompile : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return !!load_GL_NV_command_list(nvgl::ContextWindowGL::sysGetProcAddress);
      }
      const char* name() const
      {
        return "GL nvcmd compiled list";
      }
      Renderer* create() const
      {
        RendererGLCMD* renderer = new RendererGLCMD();
        renderer->m_mode = MODE_LIST_RECOMPILE;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 4;
      }

      Resources* resources()
      {
        return ResourcesGL::get();
      }
    };

  public:

    void init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config);
    void deinit();
    void draw(ShadeType shadetype, Resources*  NV_RESTRICT resources, const Resources::Global& global);
    
    Mode    m_mode;

    RendererGLCMD()
      : m_mode(MODE_BUFFER) 
    {

    }

  private:

    struct ShadeCommand {
      std::vector<GLintptr>   offsets;
      std::vector<GLsizei>    sizes;
      std::vector<GLuint>     states;
      std::vector<GLuint>     fbos;
      std::vector<void*>      ptrs;

      std::string             tokens;
      ResourcesGL::StateIncarnation state;
    };

    std::vector<DrawItem> m_drawItems;

    ResourcesGL::StateIncarnation m_state;
    ShadeCommand          m_shades[NUM_SHADES];
    GLuint                m_commandLists[NUM_SHADES];
    GLuint                m_tokenBuffers[NUM_SHADES];


    void GenerateTokens(std::vector<DrawItem>& drawItems, ShadeType shade, const CadScene* NV_RESTRICT scene, const ResourcesGL* NV_RESTRICT res )
    {
      int lastMaterial = -1;
      int lastGeometry = -1;
      int lastMatrix   = -1;
      bool lastSolid   = true;

      ShadeCommand& sc = m_shades[shade];
      sc.fbos.clear();
      sc.offsets.clear();
      sc.sizes.clear();
      sc.states.clear();
      sc.tokens.clear();

      size_t begin = 0;

      {
        ResourcesGL::tokenUbo ubo;
        ubo.cmd.index   = DRAW_UBO_SCENE;
        ubo.cmd.stage   = UBOSTAGE_VERTEX;
        ResourcesGL::encodeAddress(&ubo.cmd.addressLo,res->m_buffers.scene.bufferADDR);
        ubo.enqueue(sc.tokens);

        ubo.cmd.stage   = UBOSTAGE_FRAGMENT;
        ubo.enqueue(sc.tokens);

        ResourcesGL::tokenPolyOffset offset;
        offset.cmd.bias = 1;
        offset.cmd.scale = 1;
        offset.enqueue(sc.tokens);
      }

      for (int i = 0; i < drawItems.size(); i++){
        const DrawItem& di = drawItems[i];

        if (shade == SHADE_SOLID && !di.solid){
          if (m_config.sorted) break;
          continue;
        }

        if ( ( shade == SHADE_SOLIDWIRE ) && di.solid != lastSolid){
          sc.offsets.push_back( begin );
          sc.sizes.  push_back( GLsizei((sc.tokens.size()-begin)) );
          sc.states. push_back( lastSolid ? res->m_stateobjects.draw_line_tris : res->m_stateobjects.draw_line );
          sc.fbos.   push_back( res->m_fbos.scene );

          begin = sc.tokens.size();
        }

        if (lastGeometry != di.geometryIndex){
          const CadScene::Geometry &geo = scene->m_geometry[di.geometryIndex];
          const ResourcesGL::Geometry &geogl = res->m_geometry[di.geometryIndex];

          ResourcesGL::tokenVbo vbo;
          vbo.cmd.index = 0;
          ResourcesGL::encodeAddress(&vbo.cmd.addressLo, geogl.vbo.bufferADDR);
          vbo.enqueue(sc.tokens);

          ResourcesGL::tokenIbo ibo;
          ResourcesGL::encodeAddress(&ibo.cmd.addressLo, geogl.ibo.bufferADDR);
          ibo.cmd.typeSizeInByte = 4;
          ibo.enqueue(sc.tokens);

          lastGeometry = di.geometryIndex;
        }

        if (lastMatrix != di.matrixIndex){

          ResourcesGL::tokenUbo ubo;
          ubo.cmd.index   = DRAW_UBO_MATRIX;
          ubo.cmd.stage   = UBOSTAGE_VERTEX;
          ResourcesGL::encodeAddress(&ubo.cmd.addressLo, res->m_buffers.matrices.bufferADDR + res->m_alignedMatrixSize * di.matrixIndex);
          ubo.enqueue(sc.tokens);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex){

          ResourcesGL::tokenUbo ubo;
          ubo.cmd.index   = DRAW_UBO_MATERIAL;
          ubo.cmd.stage   = UBOSTAGE_FRAGMENT;
          ResourcesGL::encodeAddress(&ubo.cmd.addressLo, res->m_buffers.materials.bufferADDR + res->m_alignedMaterialSize * di.materialIndex);
          ubo.enqueue(sc.tokens);

          lastMaterial = di.materialIndex;
        }

        ResourcesGL::tokenDrawElems drawelems;
        drawelems.cmd.baseVertex = 0;
        drawelems.cmd.count = di.range.count;
        drawelems.cmd.firstIndex = GLuint((di.range.offset )/sizeof(GLuint));
        drawelems.enqueue(sc.tokens);

        lastSolid = di.solid;
      }

      sc.offsets.push_back( begin );
      sc.sizes.  push_back( GLsizei((sc.tokens.size()-begin)) );
      if (shade == SHADE_SOLID){
        sc.states. push_back( res->m_stateobjects.draw_tris );
      }
      else{
        sc.states. push_back( lastSolid ? res->m_stateobjects.draw_line_tris : res->m_stateobjects.draw_line );
      }
      sc.fbos. push_back( res->m_fbos.scene );

      sc.ptrs.reserve(sc.offsets.size());
      for (size_t p = 0; p < sc.offsets.size(); p++){
        sc.ptrs.push_back(&sc.tokens[sc.offsets[p]]);
      }
    }

    void GenerateCommandLists( ShadeType shadetype)
    {
      ShadeCommand& shade = m_shades[shadetype];
      shade.state = m_state;

      glCommandListSegmentsNV(m_commandLists[shadetype],1);
      glListDrawCommandsStatesClientNV(m_commandLists[shadetype],0, (const void**)&shade.ptrs[0], &shade.sizes[0], &shade.states[0], &shade.fbos[0], int(shade.states.size()) );
      glCompileCommandListNV(m_commandLists[shadetype]);
    }

  };

  static RendererGLCMD::Type s_uborange;
  static RendererGLCMD::TypeVbum s_uborange_list;
#if 0
  static RendererGLCMD::TypeRecompile s_uborange_recompile;
#endif
  void RendererGLCMD::init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config)
  {
    m_scene = scene;
    const ResourcesGL* NV_RESTRICT res = (const ResourcesGL*)resources;

    fillDrawItems(m_drawItems, config, true, true);

    if (config.sorted){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }

    for (int i = 0 ; i < NUM_SHADES; i++){
      GenerateTokens(m_drawItems, (ShadeType)i, scene, res);

      LOGI("stats: %s\n",toString((ShadeType)i));
      int stats[ResourcesGL::GL_TOKENS] = { 0 };
      ResourcesGL::nvtokenGetStats( &m_shades[i].tokens[0], m_shades[i].tokens.size(), stats );
      for (int t = 0 ; t < ResourcesGL::GL_TOKENS; t++){
        if (stats[t]){
          LOGI("%s\t: %7d\n", ResourcesGL::nvtokenCommandToString(t), stats[t] );
        }
      }
      LOGI("stateobject sequences: %7d\n\n", uint32_t(m_shades[i].states.size()));
    }

    res->rebuildStateObjects();
    m_state = res->m_state;

    if (m_mode == MODE_LIST || m_mode == MODE_LIST_RECOMPILE){
      glCreateCommandListsNV(NUM_SHADES,m_commandLists);
      for (int i = 0 ; i < NUM_SHADES; i++){
        GenerateCommandLists(ShadeType(i));
      }
    }
    else{
      glCreateBuffers(NUM_SHADES,m_tokenBuffers);
      for (int i = 0 ;i < NUM_SHADES; i++){
        glNamedBufferData(m_tokenBuffers[i], m_shades[i].tokens.size(), &m_shades[i].tokens[0], GL_STATIC_DRAW);
      }
    }
  }

  void RendererGLCMD::deinit()
  {
    if (m_mode == MODE_LIST || m_mode == MODE_LIST_RECOMPILE){
      glDeleteCommandListsNV(NUM_SHADES,m_commandLists);
    }
    else{
      glDeleteBuffers(NUM_SHADES,m_tokenBuffers);
    }
  }

  void RendererGLCMD::draw(ShadeType shadetype, Resources* NV_RESTRICT resources, const Resources::Global& global)
  {
    const CadScene* NV_RESTRICT scene = m_scene;
    ResourcesGL* NV_RESTRICT res = (ResourcesGL*)resources;

    const nvgl::ProfilerGL::Section profile(res->m_profilerGL, "Render");

    // generic state setup
    glViewport(0, 0, global.width, global.height);

    if (m_state.programs != res->m_state.programs ||
        m_state.fbos != res->m_state.fbos)
    {
      res->rebuildStateObjects();
    }
    m_state = res->m_state;

    glBindFramebuffer(GL_FRAMEBUFFER, res->m_fbos.scene);
    glClearColor(0.2f,0.2f,0.2f,0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glNamedBufferSubData(res->m_buffers.scene.buffer,0,sizeof(SceneData),&global.sceneUbo);

    if (m_mode == MODE_LIST || m_mode == MODE_LIST_RECOMPILE){
      if (m_shades[shadetype].state.programs != m_state.programs ||
          m_shades[shadetype].state.fbos     != m_state.fbos ||
          m_mode == MODE_LIST_RECOMPILE)
      {
        GenerateCommandLists(shadetype);
      }
      glCallCommandListNV(m_commandLists[shadetype]);
    }
    else{
      ShadeCommand & shade =  m_shades[shadetype];
#if 1
      glDrawCommandsStatesNV(m_tokenBuffers[shadetype], &shade.offsets[0], &shade.sizes[0], &shade.states[0], &shade.fbos[0], int(shade.states.size()) );
#else
      // bug behavior test for "shaded & edges"
      for (size_t i = 0; i < shade.states.size()/16; i++){
        glDrawCommandsStatesNV(m_tokenBuffers[shadetype], &shade.offsets[i], &shade.sizes[i], &shade.states[i], &shade.fbos[i], 1 );
      }
#endif
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  
}
