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


#pragma once

#include "cadscene_gl.hpp"
#include "resources.hpp"
#include <nvgl/base_gl.hpp>
#include <nvgl/profiler_gl.hpp>
#include <nvgl/programmanager_gl.hpp>

namespace csfthreaded {

template <class Theader, class Tcontent, int idname, class Tdef>
class Token
{
public:
  static const int ID = idname;

  union
  {
    Theader  header;
    Tcontent cmd;
  };

  Token() { header = Tdef::s_token_headers[idname]; }

  void enqueue(std::string& stream)
  {
    std::string item = std::string((const char*)this, sizeof(Token));
    stream += item;
  }

  void enqueue(PointerStream& stream)
  {
    assert(sizeof(Token) + stream.used <= stream.allocated);
    memcpy(stream.dataptr + stream.used, this, sizeof(Token));
    stream.used += sizeof(Token);
  }
};

class ResourcesGL : public Resources
{
public:
  typedef Token<GLuint, UniformAddressCommandNV, GL_UNIFORM_ADDRESS_COMMAND_NV, ResourcesGL>     tokenUbo;
  typedef Token<GLuint, AttributeAddressCommandNV, GL_ATTRIBUTE_ADDRESS_COMMAND_NV, ResourcesGL> tokenVbo;
  typedef Token<GLuint, ElementAddressCommandNV, GL_ELEMENT_ADDRESS_COMMAND_NV, ResourcesGL>     tokenIbo;
  typedef Token<GLuint, PolygonOffsetCommandNV, GL_POLYGON_OFFSET_COMMAND_NV, ResourcesGL>       tokenPolyOffset;
  typedef Token<GLuint, DrawElementsCommandNV, GL_DRAW_ELEMENTS_COMMAND_NV, ResourcesGL>         tokenDrawElems;

  enum NVTokenShaderStage
  {
    NVTOKEN_STAGE_VERTEX,
    NVTOKEN_STAGE_TESS_CONTROL,
    NVTOKEN_STAGE_TESS_EVALUATION,
    NVTOKEN_STAGE_GEOMETRY,
    NVTOKEN_STAGE_FRAGMENT,
    NVTOKEN_STAGES,
  };

  static const int GL_TOKENS = GL_FRONT_FACE_COMMAND_NV + 1;
  static GLuint    s_token_headers[GL_TOKENS];
  static size_t    s_token_sizes[GL_TOKENS];
  static GLushort  s_token_stages[NVTOKEN_STAGES];

  static inline void encodeAddress(GLuint* low, GLuint64 address)
  {
    low[0] = GLuint(address & 0xFFFFFFFF);
    low[1] = GLuint(address >> 32);
  }


  static inline GLenum nvtokenHeaderCommand(GLuint header)
  {
    for(int i = 0; i < GL_TOKENS; i++)
    {
      if(header == s_token_headers[i])
        return i;
    }

    assert(0 && "can't find header");
    return -1;
  }

  static void        nvtokenGetStats(const void* NV_RESTRICT stream, size_t streamSize, int stats[GL_TOKENS]);
  static const char* nvtokenCommandToString(GLenum type);

#define UBOSTAGE_VERTEX (ResourcesGL::s_token_stages[ResourcesGL::NVTOKEN_STAGE_VERTEX])
#define UBOSTAGE_FRAGMENT (ResourcesGL::s_token_stages[ResourcesGL::NVTOKEN_STAGE_FRAGMENT])


  struct ProgramIDs
  {
    nvgl::ProgramID draw_object_tris;
    nvgl::ProgramID draw_object_line;
    nvgl::ProgramID compute_animation;
  };

  struct Programs
  {
    GLuint draw_solid        = 0;
    GLuint draw_line         = 0;
    GLuint compute_animation = 0;
  };

  struct FrameBuffer
  {
    bool useResolve;
    int  renderWidth;
    int  renderHeight;
    int  msaa;

    GLuint fboScene              = 0;
    GLuint texSceneColor         = 0;
    GLuint texSceneDepthStencil  = 0;
  };

  struct Common
  {
    nvgl::Buffer view;
    nvgl::Buffer anim;
  };

  struct StateObjects
  {
    GLuint draw_tris      = 0;
    GLuint draw_line_tris = 0;
    GLuint draw_line      = 0;
  };

  struct StateChangeID
  {
    GLuint programs = 0;
    GLuint fbos     = 0;
  };

  nvgl::ProgramManager m_progManager;
  ProgramIDs           m_programids;
  Programs             m_programs;

  FrameBuffer m_framebuffer;
  Common      m_common;
  StateObjects m_stateobjects;
  
  CadSceneGL m_scene;

  bool m_bindless_ubo;
  bool m_cmdlist;

  StateChangeID m_state;
  nvgl::ProfilerGL m_profilerGL;

  void synchronize() { glFinish(); }

  bool init(nvgl::ContextWindow* window, nvh::Profiler* profiler);
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

  uvec2 storeU64(GLuint64 address) { return uvec2(address & 0xFFFFFFFF, address >> 32); }

  void enableVertexFormat() const;

  void disableVertexFormat() const;

  static ResourcesGL* get()
  {
    static ResourcesGL resGL;

    return &resGL;
  }
};

}  // namespace csfthreaded
