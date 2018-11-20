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
#include <nv_helpers/tnulled.hpp>
#include <nv_helpers_gl/base_gl.hpp>
#include <nv_helpers_gl/profilertimers_gl.hpp>
#include <nv_helpers_gl/programmanager_gl.hpp>

namespace csfthreaded {


  template <typename T>
  using TNulled = nv_helpers::TNulled<T>;
  using GLBuffer = nv_helpers_gl::GLBuffer;

  template<class Theader, class Tcontent, int idname, class Tdef>
  class Token {
  public:
    static const int ID = idname;

    union {
      Theader   header;
      Tcontent  cmd;
    };

    Token() {
      header = Tdef::s_token_headers[idname];
    }

    void enqueue(std::string& stream)
    {
      std::string item = std::string((const char*)this, sizeof(Token));
      stream += item;
    }

    void enqueue(PointerStream &stream)
    {
      assert(sizeof(Token) + stream.used <= stream.allocated);
      memcpy(stream.dataptr + stream.used, this, sizeof(Token));
      stream.used += sizeof(Token);
    }
  };

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

    static void         nvtokenGetStats( const void* NV_RESTRICT stream, size_t streamSize, int stats[GL_TOKENS] );
    static const char*  nvtokenCommandToString(GLenum type);

#define UBOSTAGE_VERTEX     (ResourcesGL::s_token_stages[ResourcesGL::NVTOKEN_STAGE_VERTEX])
#define UBOSTAGE_FRAGMENT   (ResourcesGL::s_token_stages[ResourcesGL::NVTOKEN_STAGE_FRAGMENT])


    struct {
      nv_helpers_gl::ProgramManager::ProgramID
        draw_object_tris,
        draw_object_line,
        compute_animation;
    } m_programids;

    struct {
      TNulled<GLuint>
        draw_solid,
        draw_line,
        compute_animation;
    } m_programs;

    struct {
      TNulled<GLuint>
        scene;
    } m_fbos;

    struct {
      GLBuffer  scene;
      GLBuffer  anim;
      GLBuffer  matrices;
      GLBuffer  matricesOrig;
      GLBuffer  materials;
    } m_buffers;


    struct {
      TNulled<GLuint>
        scene_color,
        scene_depthstencil;
    } m_textures;

    struct {
      GLuint
        draw_tris,
        draw_line_tris,
        draw_line;
    } m_stateobjects;

    struct Geometry {
      GLBuffer  vbo;
      GLBuffer  ibo;
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

    nv_helpers_gl::ProgramManager   m_progManager;

    bool  m_useResolve;
    int   m_width;
    int   m_height;

    std::vector<Geometry> m_geometry;

    bool      m_bindless_ubo;
    bool      m_cmdlist;

    nv_helpers_gl::ProfilerTimersGL m_gltimers;

    StateIncarnation  m_state;

    void synchronize()
    {
      glFinish();
    }

    bool init(NVPWindow *window);
    void deinit();
    
    bool initPrograms(const std::string& path, const std::string& prepend);
    void reloadPrograms(const std::string& prepend);
    void updatedPrograms();
    void deinitPrograms();
    
    bool initFramebuffer(int width, int height, int msaa, bool vsync);
    void deinitFramebuffer();

    bool initScene(const CadScene&);
    void deinitScene();

    void animation(const Global& global);
    void animationReset();

    void blitFrame(const Global& global);

    void rebuildStateObjects() const;

    nv_helpers::Profiler::GPUInterface*  getTimerInterface() { return &m_gltimers; }

    uvec2 storeU64(GLuint64 address) {
      return uvec2(address & 0xFFFFFFFF, address >> 32);
    }

    void enableVertexFormat() const;

    void disableVertexFormat() const;

    static ResourcesGL* get() {
      static ResourcesGL resGL;

      return &resGL;
    }
    
  };
  
}
