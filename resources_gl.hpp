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
#include <nv_helpers_gl/glresources.hpp>
#include <nv_helpers_gl/profilertimersgl.hpp>
#include "nvcommandlist.h"
#include <main.h>


namespace csfthreaded {  
  class ResourcesGL : public Resources 
  {
  public:

    typedef Token<GLuint,UniformAddressCommandNV,GL_UNIFORM_ADDRESS_COMMAND_NV,ResourcesGL>     tokenUbo;
    typedef Token<GLuint,AttributeAddressCommandNV,GL_ATTRIBUTE_ADDRESS_COMMAND_NV,ResourcesGL> tokenVbo;
    typedef Token<GLuint,ElementAddressCommandNV,GL_ELEMENT_ADDRESS_COMMAND_NV,ResourcesGL>     tokenIbo;
    typedef Token<GLuint,PolygonOffsetCommandNV,GL_POLYGON_OFFSET_COMMAND_NV,ResourcesGL>       tokenPolyOffset;
    typedef Token<GLuint,DrawElementsCommandNV,GL_DRAW_ELEMENTS_COMMAND_NV,ResourcesGL>         tokenDrawElems;

    enum NVTokenShaderStage {
      NVTOKEN_STAGE_VERTEX,
      NVTOKEN_STAGE_TESS_CONTROL,
      NVTOKEN_STAGE_TESS_EVALUATION,
      NVTOKEN_STAGE_GEOMETRY,
      NVTOKEN_STAGE_FRAGMENT,
      NVTOKEN_STAGES,
    };

    static const int GL_TOKENS = GL_FRONT_FACE_COMMAND_NV+1;
    static GLuint  s_token_headers[GL_TOKENS];
    static size_t  s_token_sizes[GL_TOKENS];
    static GLushort s_token_stages[NVTOKEN_STAGES];

    static inline void encodeAddress(GLuint* low, GLuint64 address){
      low[0] = GLuint(address & 0xFFFFFFFF);
      low[1] = GLuint(address >> 32);
    }


    static inline GLenum nvtokenHeaderCommand(GLuint header)
    {
      for (int i = 0; i < GL_TOKENS; i++){
        if (header == s_token_headers[i]) return i;
      }

      assert(0 && "can't find header");
      return -1;
    }

    static void         nvtokenGetStats( const void* NVP_RESTRICT stream, size_t streamSize, int stats[GL_TOKENS] );
    static const char*  nvtokenCommandToString(GLenum type);

#define UBOSTAGE_VERTEX     (ResourcesGL::s_token_stages[ResourcesGL::NVTOKEN_STAGE_VERTEX])
#define UBOSTAGE_FRAGMENT   (ResourcesGL::s_token_stages[ResourcesGL::NVTOKEN_STAGE_FRAGMENT])

    struct {
      nv_helpers_gl::ProgramManager::ProgramID
        draw_object_tris,
        draw_object_line,
        compute_animation;
    } programids;

    struct {
      Nulled<GLuint>
        draw_solid,
        draw_line,
        compute_animation;
    } programs;

    struct {
      Nulled<GLuint>
        scene;
    } fbos;

    struct {
      Nulled<GLuint>
        scene,
        anim,
        materials,
        matrices,
        matricesOrig;
    } buffers;

    struct {
      GLuint64
        scene,
        materials,
        matrices,
        matricesOrig;
    } addresses;

    struct {
      Nulled<GLuint>
        scene_color,
        scene_depthstencil;
    } textures;

    struct {
      GLuint
        draw_tris,
        draw_line_tris,
        draw_line;
    } stateobjects;

    struct Geometry {
      GLuint    vboGL;
      GLuint    iboGL;
      GLuint64  vboADDR;
      GLuint64  iboADDR;
      GLsizei   vboSize;
      GLsizei   iboSize;
      int       cloneIdx;
    };

    struct StateIncarnation {
      GLuint    programs;
      GLuint    fbos;

      StateIncarnation() {
        programs = 0;
        fbos = 0;
      }
    };

    int       m_numMatrices;

    std::vector<Geometry> m_geometry;

    bool      m_bindless_ubo;
    bool      m_cmdlist;

    nv_helpers_gl::ProfilerTimersGL m_gltimers;

    StateIncarnation  m_state;

    void synchronize()
    {
      glFinish();
    }

    void init();
    void deinit(nv_helpers_gl::ProgramManager &mgr);
    
    bool initPrograms(nv_helpers_gl::ProgramManager &mgr);
    void updatedPrograms(nv_helpers_gl::ProgramManager &mgr);
    void deinitPrograms(nv_helpers_gl::ProgramManager &mgr);
    
    bool initFramebuffer(int width, int height, int msaa);
    void deinitFramebuffer();

    bool initScene(const CadScene&);
    void deinitScene();

    void animation(Global& global);
    void animationReset();

    void rebuildStateObjects() const;

    nv_helpers::Profiler::GPUInterface*  getTimerInterface() { return &m_gltimers; }


    static void enableVertexFormat(int attrPos, int attrNormal)
    {
      glVertexAttribFormat(attrPos,    3,GL_FLOAT,GL_FALSE,offsetof(CadScene::Vertex,position));
      glVertexAttribFormat(attrNormal, 3,GL_FLOAT,GL_FALSE,offsetof(CadScene::Vertex,normal));
      glVertexAttribBinding(attrPos,0);
      glVertexAttribBinding(attrNormal,0);
      glEnableVertexAttribArray(attrPos);
      glEnableVertexAttribArray(attrNormal);
      glBindVertexBuffer(0,0,0,sizeof(CadScene::Vertex));
    }

    static void disableVertexFormat(int attrPos, int attrNormal)
    {
      glDisableVertexAttribArray(attrPos);
      glDisableVertexAttribArray(attrNormal);
      glBindVertexBuffer(0,0,0,16);
    }

    static ResourcesGL* get() {
      static ResourcesGL resGL;

      return &resGL;
    }
    
  };
  
}