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


#include "resources_gl.hpp"

#include <imgui/backends/imgui_impl_gl.h>

#include <nvgl/contextwindow_gl.hpp>

namespace csfthreaded {

GLuint   ResourcesGL::s_token_headers[] = {0};
size_t   ResourcesGL::s_token_sizes[]   = {0};
GLushort ResourcesGL::s_token_stages[]  = {0};

bool ResourcesGL::initFramebuffer(int width, int height, int msaa, bool vsync)
{
  m_framebuffer.renderWidth  = width;
  m_framebuffer.renderHeight = height;
  m_framebuffer.msaa  = msaa;

  nvgl::newFramebuffer(m_framebuffer.fboScene);

  if (msaa) {
    nvgl::newTexture(m_framebuffer.texSceneColor, GL_TEXTURE_2D_MULTISAMPLE);
    nvgl::newTexture(m_framebuffer.texSceneDepthStencil, GL_TEXTURE_2D_MULTISAMPLE);

    glTextureStorage2DMultisample(m_framebuffer.texSceneColor, msaa, GL_RGBA8, width, height, GL_TRUE);
    glTextureStorage2DMultisample(m_framebuffer.texSceneDepthStencil, msaa, GL_DEPTH24_STENCIL8, width, height, GL_TRUE);

    glNamedFramebufferTexture(m_framebuffer.fboScene, GL_COLOR_ATTACHMENT0, m_framebuffer.texSceneColor, 0);
    glNamedFramebufferTexture(m_framebuffer.fboScene, GL_DEPTH_STENCIL_ATTACHMENT, m_framebuffer.texSceneDepthStencil, 0);
  }
  else {
    nvgl::newTexture(m_framebuffer.texSceneColor, GL_TEXTURE_2D);
    nvgl::newTexture(m_framebuffer.texSceneDepthStencil, GL_TEXTURE_2D);

    glTextureStorage2D(m_framebuffer.texSceneColor, 1, GL_RGBA8, width, height);
    glTextureStorage2D(m_framebuffer.texSceneDepthStencil, 1, GL_DEPTH24_STENCIL8, width, height);

    glNamedFramebufferTexture(m_framebuffer.fboScene, GL_COLOR_ATTACHMENT0, m_framebuffer.texSceneColor, 0);
    glNamedFramebufferTexture(m_framebuffer.fboScene, GL_DEPTH_STENCIL_ATTACHMENT, m_framebuffer.texSceneDepthStencil, 0);
  }


  return true;
}

void ResourcesGL::deinitFramebuffer()
{
  nvgl::deleteFramebuffer(m_framebuffer.fboScene);

  nvgl::deleteTexture(m_framebuffer.texSceneColor);
  nvgl::deleteTexture(m_framebuffer.texSceneDepthStencil);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ResourcesGL::deinit()
{
  deinitScene();
  deinitFramebuffer();
  deinitPrograms();

  if(m_cmdlist)
  {
    glDeleteStatesNV(1, &m_stateobjects.draw_line);
    glDeleteStatesNV(1, &m_stateobjects.draw_tris);
    glDeleteStatesNV(1, &m_stateobjects.draw_line_tris);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  m_common.view.destroy();
  m_common.anim.destroy();

  m_profilerGL.deinit();

  ImGui::ShutdownGL();
}

void ResourcesGL::deinitScene()
{
  m_scene.deinit();
  glFinish();
}

bool ResourcesGL::initScene(const CadScene& cadscene)
{
  m_scene.init(cadscene);

  m_numMatrices = (int32_t)cadscene.m_matrices.size();

  assert(sizeof(CadScene::MatrixNode) == m_alignedMatrixSize);
  assert(sizeof(CadScene::Material) == m_alignedMaterialSize);

  return true;
}

bool ResourcesGL::initPrograms(const std::string& path, const std::string& prepend)
{
  m_progManager.m_filetype = nvh::ShaderFileManager::FILETYPE_GLSL;
  m_progManager.m_prepend  = prepend;
  if(m_cmdlist)
  {
    m_progManager.m_prepend += std::string(
        "#extension GL_NV_gpu_shader5 : require\n#extension GL_NV_command_list : require \nlayout(commandBindableNV) "
        "uniform;\n");
  }

  m_progManager.addDirectory(path);
  m_progManager.addDirectory(std::string("GLSL_" PROJECT_NAME));
  m_progManager.addDirectory(path + std::string(PROJECT_RELDIRECTORY));

  m_progManager.registerInclude("common.h");

  m_programids.draw_object_tris = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "#define WIREMODE 0\n", "scene.vert.glsl"),
      nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "#define WIREMODE 0\n", "scene.frag.glsl"));

  m_programids.draw_object_line = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "#define WIREMODE 1\n", "scene.vert.glsl"),
      nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "#define WIREMODE 1\n", "scene.frag.glsl"));

  m_programids.compute_animation =
      m_progManager.createProgram(nvgl::ProgramManager::Definition(GL_COMPUTE_SHADER, "animation.comp.glsl"));

  bool valid = m_progManager.areProgramsValid();

  if(valid)
  {
    updatedPrograms();
  }

  return valid;
}

void ResourcesGL::reloadPrograms(const std::string& prepend)
{
  m_progManager.m_prepend = prepend;
  if(m_cmdlist)
  {
    m_progManager.m_prepend += std::string(
        "#extension GL_NV_gpu_shader5 : require\n#extension GL_NV_command_list : require \nlayout(commandBindableNV) "
        "uniform;\n");
  }
  m_progManager.reloadPrograms();
  updatedPrograms();
}

void ResourcesGL::updatedPrograms()
{
  m_programs.draw_line         = m_progManager.get(m_programids.draw_object_line);
  m_programs.draw_solid        = m_progManager.get(m_programids.draw_object_tris);
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
static void registerSize(GLuint type)
{
  ResourcesGL::s_token_sizes[type] = sizeof(T);
}

bool ResourcesGL::init(nvgl::ContextWindow* contextWindowGL, nvh::Profiler* profiler)
{
  ImGui::InitGL();

  GLint uboAlignment;
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlignment);
  initAlignedSizes(uboAlignment);

  m_profilerGL = nvgl::ProfilerGL(profiler);
  m_profilerGL.init();

  m_bindless_ubo = !!contextWindowGL->extensionSupported("GL_NV_uniform_buffer_unified_memory");
  m_cmdlist      = !!load_GL_NV_command_list(nvgl::ContextWindow::sysGetProcAddress);

  m_common.anim.create(sizeof(AnimationData), nullptr, GL_DYNAMIC_STORAGE_BIT, 0);
  m_common.view.create(sizeof(SceneData), nullptr, GL_DYNAMIC_STORAGE_BIT, 0);

  if(m_cmdlist)
  {
    registerSize<TerminateSequenceCommandNV>(GL_TERMINATE_SEQUENCE_COMMAND_NV);
    registerSize<NOPCommandNV>(GL_NOP_COMMAND_NV);
    registerSize<DrawElementsCommandNV>(GL_DRAW_ELEMENTS_COMMAND_NV);
    registerSize<DrawArraysCommandNV>(GL_DRAW_ARRAYS_COMMAND_NV);
    registerSize<DrawElementsCommandNV>(GL_DRAW_ELEMENTS_STRIP_COMMAND_NV);
    registerSize<DrawArraysCommandNV>(GL_DRAW_ARRAYS_STRIP_COMMAND_NV);
    registerSize<DrawElementsInstancedCommandNV>(GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV);
    registerSize<DrawArraysInstancedCommandNV>(GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV);
    registerSize<ElementAddressCommandNV>(GL_ELEMENT_ADDRESS_COMMAND_NV);
    registerSize<AttributeAddressCommandNV>(GL_ATTRIBUTE_ADDRESS_COMMAND_NV);
    registerSize<UniformAddressCommandNV>(GL_UNIFORM_ADDRESS_COMMAND_NV);
    registerSize<BlendColorCommandNV>(GL_BLEND_COLOR_COMMAND_NV);
    registerSize<StencilRefCommandNV>(GL_STENCIL_REF_COMMAND_NV);
    registerSize<LineWidthCommandNV>(GL_LINE_WIDTH_COMMAND_NV);
    registerSize<PolygonOffsetCommandNV>(GL_POLYGON_OFFSET_COMMAND_NV);
    registerSize<AlphaRefCommandNV>(GL_ALPHA_REF_COMMAND_NV);
    registerSize<ViewportCommandNV>(GL_VIEWPORT_COMMAND_NV);
    registerSize<ScissorCommandNV>(GL_SCISSOR_COMMAND_NV);
    registerSize<FrontFaceCommandNV>(GL_FRONT_FACE_COMMAND_NV);

    for(int i = 0; i < GL_TOKENS; i++)
    {
      assert(s_token_sizes[i]);
      s_token_headers[i] = glGetCommandHeaderNV(i, GLuint(s_token_sizes[i]));
    }
    s_token_stages[NVTOKEN_STAGE_VERTEX]          = glGetStageIndexNV(GL_VERTEX_SHADER);
    s_token_stages[NVTOKEN_STAGE_TESS_CONTROL]    = glGetStageIndexNV(GL_TESS_CONTROL_SHADER);
    s_token_stages[NVTOKEN_STAGE_TESS_EVALUATION] = glGetStageIndexNV(GL_TESS_EVALUATION_SHADER);
    s_token_stages[NVTOKEN_STAGE_GEOMETRY]        = glGetStageIndexNV(GL_GEOMETRY_SHADER);
    s_token_stages[NVTOKEN_STAGE_FRAGMENT]        = glGetStageIndexNV(GL_FRAGMENT_SHADER);

    glCreateStatesNV(1, &m_stateobjects.draw_tris);
    glCreateStatesNV(1, &m_stateobjects.draw_line_tris);
    glCreateStatesNV(1, &m_stateobjects.draw_line);
  }

  glDepthFunc(GL_LESS);

  return true;
}

#define TOSTRING(a)                                                                                                    \
  case a:                                                                                                              \
    return #a;
const char* ResourcesGL::nvtokenCommandToString(GLenum type)
{
  switch(type)
  {
    TOSTRING(GL_TERMINATE_SEQUENCE_COMMAND_NV);
    TOSTRING(GL_NOP_COMMAND_NV);

    TOSTRING(GL_DRAW_ELEMENTS_COMMAND_NV);
    TOSTRING(GL_DRAW_ARRAYS_COMMAND_NV);
    TOSTRING(GL_DRAW_ELEMENTS_STRIP_COMMAND_NV);
    TOSTRING(GL_DRAW_ARRAYS_STRIP_COMMAND_NV);
    TOSTRING(GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV);
    TOSTRING(GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV);

    TOSTRING(GL_ELEMENT_ADDRESS_COMMAND_NV);
    TOSTRING(GL_ATTRIBUTE_ADDRESS_COMMAND_NV);
    TOSTRING(GL_UNIFORM_ADDRESS_COMMAND_NV);

    TOSTRING(GL_BLEND_COLOR_COMMAND_NV);
    TOSTRING(GL_STENCIL_REF_COMMAND_NV);
    TOSTRING(GL_LINE_WIDTH_COMMAND_NV);
    TOSTRING(GL_POLYGON_OFFSET_COMMAND_NV);
    TOSTRING(GL_ALPHA_REF_COMMAND_NV);
    TOSTRING(GL_VIEWPORT_COMMAND_NV);
    TOSTRING(GL_SCISSOR_COMMAND_NV);
    TOSTRING(GL_FRONT_FACE_COMMAND_NV);
  }
  return NULL;
}
#undef TOSTRING

void ResourcesGL::nvtokenGetStats(const void* NV_RESTRICT stream, size_t streamSize, int stats[GL_TOKENS])
{
  const GLubyte* NV_RESTRICT current   = (GLubyte*)stream;
  const GLubyte*             streamEnd = current + streamSize;

  while(current < streamEnd)
  {
    const GLuint* header = (const GLuint*)current;

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
  glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0, 0, 0);
  glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV, 0, 0, 0);
  glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_MATERIAL, 0, 0);
  glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_MATRIX, 0, 0);
  glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, DRAW_UBO_SCENE, 0, 0);

  // we will do a series of state captures
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer.fboScene);
  glUseProgram(m_programs.draw_solid);
  glStateCaptureNV(m_stateobjects.draw_tris, GL_TRIANGLES);

  glEnable(GL_POLYGON_OFFSET_FILL);
  glStateCaptureNV(m_stateobjects.draw_line_tris, GL_TRIANGLES);

  glUseProgram(m_programs.draw_line);
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

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_common.anim.buffer);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(AnimationData), &global.animUbo);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ANIM_SSBO_MATRIXOUT, m_scene.m_buffers.matrices.buffer);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ANIM_SSBO_MATRIXORIG, m_scene.m_buffers.matricesOrig.buffer);

  glDispatchCompute((m_numMatrices + ANIMATION_WORKGROUPSIZE - 1) / ANIMATION_WORKGROUPSIZE, 1, 1);

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(0);
}

void ResourcesGL::animationReset()
{
  glCopyNamedBufferSubData(m_scene.m_buffers.matricesOrig, m_scene.m_buffers.matrices, 0, 0, sizeof(MatrixData) * m_numMatrices);
}

void ResourcesGL::blitFrame(const Global& global)
{
  int width  = global.winWidth;
  int height = global.winHeight;

  // blit to background
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer.fboScene);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if(global.imguiDrawData)
  {
    glViewport(0, 0, width, height);
    ImGui::RenderDrawDataGL(global.imguiDrawData);
  }
}

void ResourcesGL::enableVertexFormat() const
{
  glVertexAttribBinding(VERTEX_POS_OCTNORMAL, 0);
  glEnableVertexAttribArray(VERTEX_POS_OCTNORMAL);

  glVertexAttribFormat(VERTEX_POS_OCTNORMAL, 4, GL_FLOAT, GL_FALSE, 0);
  glBindVertexBuffer(0, 0, 0, sizeof(CadScene::Vertex));
}


void ResourcesGL::disableVertexFormat() const
{
  glDisableVertexAttribArray(VERTEX_POS_OCTNORMAL);
  glBindVertexBuffer(0, 0, 0, 16);
  glBindVertexBuffer(1, 0, 0, 16);
}
}  // namespace csfthreaded
