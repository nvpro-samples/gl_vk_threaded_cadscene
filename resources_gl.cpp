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

#include "resources_gl.hpp"

#include <imgui/imgui_impl_gl.h>

#include <main.h>

using namespace nv_helpers_gl;

namespace csfthreaded {

  GLuint ResourcesGL::s_token_headers[] = {0};
  size_t ResourcesGL::s_token_sizes[] = {0};
  GLushort ResourcesGL::s_token_stages[] = {0};

  bool ResourcesGL::initFramebuffer(int width, int height, int msaa, bool vsync)
  {
    m_width = width;
    m_height = height;

    newTexture(m_textures.scene_color, msaa ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D);
    newTexture(m_textures.scene_depthstencil, msaa ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D);
    newFramebuffer(m_fbos.scene);

    if (msaa){
      glTextureStorage2DMultisample(m_textures.scene_color,         msaa, GL_RGBA8, width, height, GL_FALSE);
      glTextureStorage2DMultisample(m_textures.scene_depthstencil,  msaa, GL_DEPTH24_STENCIL8, width, height, GL_FALSE);

      glNamedFramebufferTexture(m_fbos.scene, GL_COLOR_ATTACHMENT0,         m_textures.scene_color, 0);
      glNamedFramebufferTexture(m_fbos.scene, GL_DEPTH_STENCIL_ATTACHMENT,  m_textures.scene_depthstencil, 0);
    }
    else{
      glTextureStorage2D(m_textures.scene_color,  1, GL_RGBA8, width, height);
      glTextureStorage2D(m_textures.scene_depthstencil,  1, GL_DEPTH24_STENCIL8, width, height);

      glNamedFramebufferTexture(m_fbos.scene, GL_COLOR_ATTACHMENT0,         m_textures.scene_color, 0);
      glNamedFramebufferTexture(m_fbos.scene, GL_DEPTH_STENCIL_ATTACHMENT,  m_textures.scene_depthstencil, 0);
    }

    m_state.fbos++;

    return true;
  }

  void ResourcesGL::deinitFramebuffer()
  {
    deleteFramebuffer(m_fbos.scene);

    deleteTexture(m_textures.scene_color);
    deleteTexture(m_textures.scene_depthstencil);

    glBindFramebuffer(GL_FRAMEBUFFER,0);
  }

  void ResourcesGL::deinit()
  {
    deinitScene();
    deinitFramebuffer();
    deinitPrograms();

    if (m_cmdlist){
      glDeleteStatesNV(1, &m_stateobjects.draw_line);
      glDeleteStatesNV(1, &m_stateobjects.draw_tris);
      glDeleteStatesNV(1, &m_stateobjects.draw_line_tris);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_gltimers.deinit();

    ImGui::ShutdownGL();
  }

  void ResourcesGL::deinitScene()
  {
    if (m_geometry.empty()) return;

    m_buffers.scene.destroy();
    m_buffers.anim.destroy();
    m_buffers.matrices.destroy();
    m_buffers.materials.destroy();
    m_buffers.matricesOrig.destroy();

    for (size_t i = 0 ; i < m_geometry.size(); i++)
    {
      if (m_geometry[i].cloneIdx >= 0) continue;

      m_geometry[i].vbo.destroy();
      m_geometry[i].ibo.destroy();
    }

    m_geometry.clear();

    glFinish();
  }

  bool ResourcesGL::initScene( const CadScene& cadscene)
  {
    m_numMatrices = (int32_t)cadscene.m_matrices.size();

    m_buffers.scene.create(sizeof(SceneData), nullptr, GL_DYNAMIC_STORAGE_BIT, 0);
    m_buffers.anim.create(sizeof(AnimationData), nullptr, GL_DYNAMIC_STORAGE_BIT, 0);

    m_geometry.resize(cadscene.m_geometry.size());
    
    for (size_t i = 0; i < cadscene.m_geometry.size(); i++){
      const CadScene::Geometry & cgeom = cadscene.m_geometry[i];
      Geometry&                   geom = m_geometry[i];

      if (cgeom.cloneIdx < 0) {
        geom.vbo.create(cgeom.vboSize, cgeom.vertices, 0, 0);
        geom.ibo.create(cgeom.iboSize, cgeom.indices, 0, 0);
      }
      else{
        geom = m_geometry[cgeom.cloneIdx];
      }
      geom.cloneIdx = cgeom.cloneIdx;
    }

    // FIXME, atm sizes must match, but cleaner solution is creating a temp strided memory block for material & matrix
    assert(sizeof(CadScene::MatrixNode) == m_alignedMatrixSize);
    assert(sizeof(CadScene::Material)   == m_alignedMaterialSize);

    m_buffers.materials.create(sizeof(CadScene::Material) * cadscene.m_materials.size(), cadscene.m_materials.data(), 0, 0);
    m_buffers.matrices.create(sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size(), cadscene.m_matrices.data(), 0, 0);
    m_buffers.matricesOrig.create(sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size(), cadscene.m_matrices.data(), 0, 0);
    m_state.fbos++;

    return true;
  }

  bool ResourcesGL::initPrograms(const std::string& path, const std::string &prepend)
  {
    m_progManager.m_filetype = nv_helpers::ShaderFileManager::FILETYPE_GLSL;
    m_progManager.m_prepend = prepend;
    if (m_cmdlist) {
      m_progManager.m_prepend += std::string("#extension GL_NV_gpu_shader5 : require\n#extension GL_NV_command_list : require \nlayout(commandBindableNV) uniform;\n");
    }

    m_progManager.addDirectory(path);
    m_progManager.addDirectory(std::string("GLSL_" PROJECT_NAME));
    m_progManager.addDirectory(path + std::string(PROJECT_RELDIRECTORY));
    m_progManager.addDirectory(std::string(PROJECT_ABSDIRECTORY));
    
    m_progManager.registerInclude("common.h", "common.h");

    m_programids.draw_object_tris = m_progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define WIREMODE 0\n",  "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define WIREMODE 0\n",  "scene.frag.glsl"));

    m_programids.draw_object_line = m_progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define WIREMODE 1\n",  "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define WIREMODE 1\n",  "scene.frag.glsl"));

    m_programids.compute_animation = m_progManager.createProgram(
      ProgramManager::Definition(GL_COMPUTE_SHADER,  "animation.comp.glsl"));

    bool valid = m_progManager.areProgramsValid();

    if (valid) {
      updatedPrograms();
    }

    return valid;
  }

  void ResourcesGL::reloadPrograms(const std::string& prepend)
  {
    m_progManager.m_prepend = prepend;
    if (m_cmdlist) {
      m_progManager.m_prepend += std::string("#extension GL_NV_gpu_shader5 : require\n#extension GL_NV_command_list : require \nlayout(commandBindableNV) uniform;\n");
    }
    m_progManager.reloadPrograms();
    updatedPrograms();
  }

  void ResourcesGL::updatedPrograms()
  {
    m_programs.draw_line = m_progManager.get(m_programids.draw_object_line);
    m_programs.draw_solid = m_progManager.get(m_programids.draw_object_tris);
    m_programs.compute_animation = m_progManager.get(m_programids.compute_animation);

    // rebuild stateobjects

    m_state.programs++;
  }

  void ResourcesGL::deinitPrograms()
  {
    m_progManager.destroyProgram(m_programids.draw_object_line);
    m_progManager.destroyProgram(m_programids.draw_object_tris);
    m_progManager.destroyProgram(m_programids.compute_animation);

    glUseProgram(0);
  }

  template <class T>
  static void registerSize( GLuint type )
  {
    ResourcesGL::s_token_sizes[type] = sizeof(T);
  }

  bool ResourcesGL::init(NVPWindow *window)
  {
    ImGui::InitGL();

    GLint uboAlignment;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&uboAlignment);
    initAlignedSizes(uboAlignment);

    m_gltimers.init( nv_helpers::Profiler::START_TIMERS);

    m_bindless_ubo = !!NVPWindow::sysExtensionSupportedGL("GL_NV_uniform_buffer_unified_memory");
    m_cmdlist      = !!load_GL_NV_command_list(NVPWindow::sysGetProcAddressGL);

    if (m_cmdlist){
      registerSize<TerminateSequenceCommandNV>(GL_TERMINATE_SEQUENCE_COMMAND_NV          );
      registerSize<NOPCommandNV>(GL_NOP_COMMAND_NV          );
      registerSize<DrawElementsCommandNV>(GL_DRAW_ELEMENTS_COMMAND_NV          );
      registerSize<DrawArraysCommandNV>(GL_DRAW_ARRAYS_COMMAND_NV            );
      registerSize<DrawElementsCommandNV>(GL_DRAW_ELEMENTS_STRIP_COMMAND_NV    );
      registerSize<DrawArraysCommandNV>(GL_DRAW_ARRAYS_STRIP_COMMAND_NV      );
      registerSize<DrawElementsInstancedCommandNV>(GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV);
      registerSize<DrawArraysInstancedCommandNV>(GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV  );
      registerSize<ElementAddressCommandNV>(GL_ELEMENT_ADDRESS_COMMAND_NV        );
      registerSize<AttributeAddressCommandNV>(GL_ATTRIBUTE_ADDRESS_COMMAND_NV      );
      registerSize<UniformAddressCommandNV>(GL_UNIFORM_ADDRESS_COMMAND_NV        );
      registerSize<BlendColorCommandNV>(GL_BLEND_COLOR_COMMAND_NV            );
      registerSize<StencilRefCommandNV>(GL_STENCIL_REF_COMMAND_NV            );
      registerSize<LineWidthCommandNV>(GL_LINE_WIDTH_COMMAND_NV             );
      registerSize<PolygonOffsetCommandNV>(GL_POLYGON_OFFSET_COMMAND_NV         );
      registerSize<AlphaRefCommandNV>(GL_ALPHA_REF_COMMAND_NV              );
      registerSize<ViewportCommandNV>(GL_VIEWPORT_COMMAND_NV               );
      registerSize<ScissorCommandNV>(GL_SCISSOR_COMMAND_NV                );
      registerSize<FrontFaceCommandNV>(GL_FRONT_FACE_COMMAND_NV              );

      for (int i = 0; i < GL_TOKENS; i++){
        assert(s_token_sizes[i]);
        s_token_headers[i] = glGetCommandHeaderNV(i,GLuint(s_token_sizes[i]));
      }
      s_token_stages[NVTOKEN_STAGE_VERTEX] = glGetStageIndexNV(GL_VERTEX_SHADER);
      s_token_stages[NVTOKEN_STAGE_TESS_CONTROL] = glGetStageIndexNV(GL_TESS_CONTROL_SHADER);
      s_token_stages[NVTOKEN_STAGE_TESS_EVALUATION] = glGetStageIndexNV(GL_TESS_EVALUATION_SHADER);
      s_token_stages[NVTOKEN_STAGE_GEOMETRY] = glGetStageIndexNV(GL_GEOMETRY_SHADER);
      s_token_stages[NVTOKEN_STAGE_FRAGMENT] = glGetStageIndexNV(GL_FRAGMENT_SHADER);

      glCreateStatesNV(1, &m_stateobjects.draw_tris);
      glCreateStatesNV(1, &m_stateobjects.draw_line_tris);
      glCreateStatesNV(1, &m_stateobjects.draw_line);
    }

    glDepthFunc(GL_LESS);

    return true;
  }

#define TOSTRING(a)  case a: return #a;
  const char* ResourcesGL::nvtokenCommandToString(GLenum type){
    switch  (type){
      TOSTRING(GL_TERMINATE_SEQUENCE_COMMAND_NV    );
      TOSTRING(GL_NOP_COMMAND_NV                   );
      
      TOSTRING(GL_DRAW_ELEMENTS_COMMAND_NV         );
      TOSTRING(GL_DRAW_ARRAYS_COMMAND_NV           );
      TOSTRING(GL_DRAW_ELEMENTS_STRIP_COMMAND_NV   );
      TOSTRING(GL_DRAW_ARRAYS_STRIP_COMMAND_NV     );
      TOSTRING(GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV);
      TOSTRING(GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV  );

      TOSTRING(GL_ELEMENT_ADDRESS_COMMAND_NV       );
      TOSTRING(GL_ATTRIBUTE_ADDRESS_COMMAND_NV     );
      TOSTRING(GL_UNIFORM_ADDRESS_COMMAND_NV       );

      TOSTRING(GL_BLEND_COLOR_COMMAND_NV           );
      TOSTRING(GL_STENCIL_REF_COMMAND_NV           );
      TOSTRING(GL_LINE_WIDTH_COMMAND_NV            );
      TOSTRING(GL_POLYGON_OFFSET_COMMAND_NV        );
      TOSTRING(GL_ALPHA_REF_COMMAND_NV             );
      TOSTRING(GL_VIEWPORT_COMMAND_NV              );
      TOSTRING(GL_SCISSOR_COMMAND_NV               );
      TOSTRING(GL_FRONT_FACE_COMMAND_NV            );
    }
    return NULL;
  }
#undef TOSTRING

  void ResourcesGL::nvtokenGetStats( const void* NV_RESTRICT stream, size_t streamSize, int stats[GL_TOKENS] )
  {
    const GLubyte* NV_RESTRICT current = (GLubyte*)stream;
    const GLubyte* streamEnd = current + streamSize;

    while (current < streamEnd){
      const GLuint*             header  = (const GLuint*)current;

      GLenum type = nvtokenHeaderCommand(*header);
      stats[type]++;

      current += s_token_sizes[type];
    }
  }

  void ResourcesGL::rebuildStateObjects() const
  {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    enableVertexFormat();

    // temp workaround
    glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV,0,0,0);
    glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV,0,0,0);
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATERIAL,0,0);
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATRIX,0,0);
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_SCENE,0,0);

    // we will do a series of state captures
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbos.scene);
    glUseProgram(m_programs.draw_solid );
    glStateCaptureNV( m_stateobjects.draw_tris, GL_TRIANGLES);
    
    glEnable(GL_POLYGON_OFFSET_FILL);
    glStateCaptureNV(m_stateobjects.draw_line_tris, GL_TRIANGLES);

    glUseProgram(m_programs.draw_line );
    glStateCaptureNV(m_stateobjects.draw_line, GL_LINES);

    disableVertexFormat();

    // reset, stored in stateobjects
    glUseProgram(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void ResourcesGL::animation(const Global& global)
  {
    glUseProgram(m_programs.compute_animation);
    
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_buffers.anim.buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(AnimationData), &global.animUbo);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO_MATRIXOUT, m_buffers.matrices.buffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO_MATRIXORIG, m_buffers.matricesOrig.buffer);

    glDispatchCompute((m_numMatrices + ANIMATION_WORKGROUPSIZE-1) / ANIMATION_WORKGROUPSIZE,1,1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);
  }

  void ResourcesGL::animationReset()
  {
    glCopyNamedBufferSubData(m_buffers.matricesOrig, m_buffers.matrices, 0, 0, sizeof(MatrixData) * m_numMatrices);
  }
  
  void ResourcesGL::blitFrame(const Global& global)
  {
    int width = global.width;
    int height = global.height;

    // blit to background
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbos.scene);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height,
      0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (global.imguiDrawData) {
      glViewport(0, 0, width, height);
      ImGui::RenderDrawDataGL(global.imguiDrawData);
    }

  }

  void ResourcesGL::enableVertexFormat() const
  {
    glVertexAttribBinding(VERTEX_POS, 0);
    glVertexAttribBinding(VERTEX_NORMAL, 0);
    glEnableVertexAttribArray(VERTEX_POS);
    glEnableVertexAttribArray(VERTEX_NORMAL);
    
    glVertexAttribFormat(VERTEX_POS, 3, GL_FLOAT, GL_FALSE, offsetof(CadScene::Vertex, position));
    glVertexAttribFormat(VERTEX_NORMAL, 3, GL_FLOAT, GL_FALSE, offsetof(CadScene::Vertex, normal));
    glBindVertexBuffer(0, 0, 0, sizeof(CadScene::Vertex));
  }


  void ResourcesGL::disableVertexFormat() const
  {
    glDisableVertexAttribArray(VERTEX_POS);
    glDisableVertexAttribArray(VERTEX_NORMAL);
    glBindVertexBuffer(0, 0, 0, 16);
    glBindVertexBuffer(1, 0, 0, 16);
  }
}

