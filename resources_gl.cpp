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

#include "resources_gl.hpp"

using namespace nv_helpers_gl;

namespace csfthreaded {

  GLuint ResourcesGL::s_token_headers[] = {0};
  size_t ResourcesGL::s_token_sizes[] = {0};
  GLushort ResourcesGL::s_token_stages[] = {0};

  bool ResourcesGL::initFramebuffer(int width, int height, int msaa)
  {
    newTexture(textures.scene_color);
    newTexture(textures.scene_depthstencil);
    newFramebuffer(fbos.scene);

    if (msaa){
      glTextureStorage2DMultisampleEXT(textures.scene_color, GL_TEXTURE_2D_MULTISAMPLE,        msaa, GL_RGBA8, width, height, GL_FALSE);
      glTextureStorage2DMultisampleEXT(textures.scene_depthstencil, GL_TEXTURE_2D_MULTISAMPLE, msaa, GL_DEPTH24_STENCIL8, width, height, GL_FALSE);

      glNamedFramebufferTexture2DEXT(fbos.scene, GL_COLOR_ATTACHMENT0,        GL_TEXTURE_2D_MULTISAMPLE, textures.scene_color, 0);
      glNamedFramebufferTexture2DEXT(fbos.scene, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, textures.scene_depthstencil, 0);
    }
    else{
      glTextureStorage2DEXT(textures.scene_color, GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
      glTextureStorage2DEXT(textures.scene_depthstencil, GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, width, height);

      glNamedFramebufferTexture2DEXT(fbos.scene, GL_COLOR_ATTACHMENT0,        GL_TEXTURE_2D, textures.scene_color, 0);
      glNamedFramebufferTexture2DEXT(fbos.scene, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, textures.scene_depthstencil, 0);
    }

    m_state.fbos++;

    return true;
  }

  void ResourcesGL::deinitFramebuffer()
  {
    deleteFramebuffer(fbos.scene);
    deleteTexture(textures.scene_color);
    deleteTexture(textures.scene_depthstencil);

    glBindFramebuffer(GL_FRAMEBUFFER,0);
  }

  void ResourcesGL::deinit(nv_helpers_gl::ProgramManager &mgr)
  {
    deinitScene();
    deinitFramebuffer();
    deinitPrograms(mgr);

    if (m_cmdlist){
      glDeleteStatesNV(1,&stateobjects.draw_line);
      glDeleteStatesNV(1,&stateobjects.draw_tris);
      glDeleteStatesNV(1,&stateobjects.draw_line_tris);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_gltimers.deinit();

    mgr.m_prepend = std::string();
  }

  void ResourcesGL::deinitScene()
  {
    if (buffers.scene && GLEW_NV_shader_buffer_load){
      glMakeNamedBufferNonResidentNV(buffers.scene);
    }
    deleteBuffer(buffers.scene);
    deleteBuffer(buffers.anim);

    if (m_geometry.empty()) return;

    if (GLEW_NV_vertex_buffer_unified_memory){
      glMakeNamedBufferNonResidentNV(buffers.matrices);
      glMakeNamedBufferNonResidentNV(buffers.matricesOrig);
      glMakeNamedBufferNonResidentNV(buffers.materials);
    }
    
    glDeleteBuffers(1,&buffers.matrices);
    glDeleteBuffers(1,&buffers.matricesOrig);
    glDeleteBuffers(1,&buffers.materials);

    for (size_t i = 0 ; i < m_geometry.size(); i++)
    {
      if (m_geometry[i].cloneIdx >= 0) continue;

      if (GLEW_NV_vertex_buffer_unified_memory){
        glMakeNamedBufferNonResidentNV(m_geometry[i].iboGL);
        glMakeNamedBufferNonResidentNV(m_geometry[i].vboGL);
      }
      glDeleteBuffers(1,&m_geometry[i].iboGL);
      glDeleteBuffers(1,&m_geometry[i].vboGL);
    }

    m_geometry.clear();

    glFinish();
  }

  bool ResourcesGL::initScene( const CadScene& cadscene)
  {
    m_numMatrices = cadscene.m_matrices.size();

    newBuffer(buffers.scene);
    glNamedBufferStorageEXT(buffers.scene, sizeof(SceneData), NULL, GL_DYNAMIC_STORAGE_BIT);

    newBuffer(buffers.anim);
    glNamedBufferStorageEXT(buffers.anim, sizeof(AnimationData), NULL, GL_DYNAMIC_STORAGE_BIT);

    if (GLEW_NV_shader_buffer_load){
      glGetNamedBufferParameterui64vNV(buffers.scene, GL_BUFFER_GPU_ADDRESS_NV, &addresses.scene);
      glMakeNamedBufferResidentNV(buffers.scene,GL_READ_ONLY);
    }

    m_geometry.resize( cadscene.m_geometry.size() );
    
    for (size_t i = 0; i < cadscene.m_geometry.size(); i++){
      const CadScene::Geometry & cgeom = cadscene.m_geometry[i];
      Geometry&                   geom = m_geometry[i];

      if (cgeom.cloneIdx < 0) {
        geom.vboSize = (GLsizei)cgeom.vboSize;
        geom.iboSize = (GLsizei)cgeom.iboSize;

        glGenBuffers(1,&geom.vboGL);
        glNamedBufferStorageEXT(geom.vboGL, cgeom.vboSize, &cgeom.vertices[0], 0);

        glGenBuffers(1,&geom.iboGL);
        glNamedBufferStorageEXT(geom.iboGL, cgeom.iboSize, &cgeom.indices[0], 0);

        if (GLEW_NV_vertex_buffer_unified_memory){
          glGetNamedBufferParameterui64vNV(geom.vboGL, GL_BUFFER_GPU_ADDRESS_NV, &geom.vboADDR);
          glMakeNamedBufferResidentNV(geom.vboGL, GL_READ_ONLY);

          glGetNamedBufferParameterui64vNV(geom.iboGL, GL_BUFFER_GPU_ADDRESS_NV, &geom.iboADDR);
          glMakeNamedBufferResidentNV(geom.iboGL, GL_READ_ONLY);
        }
      }
      else{
        geom = m_geometry[cgeom.cloneIdx];
      }
      geom.cloneIdx = cgeom.cloneIdx;
    }

    // FIXME, atm sizes must match, but cleaner solution is creating a temp strided memory block for material & matrix
    assert(sizeof(CadScene::MatrixNode) == m_alignedMatrixSize);
    assert(sizeof(CadScene::Material)   == m_alignedMaterialSize);

    glGenBuffers(1,&buffers.materials);
    glNamedBufferStorageEXT(buffers.materials, sizeof(CadScene::Material) * cadscene.m_materials.size(), &cadscene.m_materials[0], 0);

    glGenBuffers(1,&buffers.matrices);
    glNamedBufferStorageEXT(buffers.matrices, sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size(), &cadscene.m_matrices[0], 0);

    glGenBuffers(1,&buffers.matricesOrig);
    glNamedBufferStorageEXT(buffers.matricesOrig, sizeof(CadScene::MatrixNode) * cadscene.m_matrices.size(), &cadscene.m_matrices[0], 0);

    if (GLEW_NV_vertex_buffer_unified_memory){
      glGetNamedBufferParameterui64vNV(buffers.materials, GL_BUFFER_GPU_ADDRESS_NV, &addresses.materials);
      glMakeNamedBufferResidentNV(buffers.materials, GL_READ_ONLY);

      glGetNamedBufferParameterui64vNV(buffers.matrices, GL_BUFFER_GPU_ADDRESS_NV, &addresses.matrices);
      glMakeNamedBufferResidentNV(buffers.matrices, GL_READ_WRITE);

      glGetNamedBufferParameterui64vNV(buffers.matricesOrig, GL_BUFFER_GPU_ADDRESS_NV, &addresses.matricesOrig);
      glMakeNamedBufferResidentNV(buffers.matricesOrig, GL_READ_ONLY);
    }

    m_state.fbos++;

    return true;
  }

  bool ResourcesGL::initPrograms( nv_helpers_gl::ProgramManager &mgr)
  {
    mgr.m_preprocessOnly = false;

    if (m_cmdlist){
      mgr.m_prepend = std::string("#extension GL_NV_gpu_shader5 : require\n#extension GL_NV_command_list : require \nlayout(commandBindableNV) uniform;\n");
    }

    programids.draw_object_tris = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define WIREMODE 0\n",  "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define WIREMODE 0\n",  "scene.frag.glsl"));

    programids.draw_object_line = mgr.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define WIREMODE 1\n",  "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define WIREMODE 1\n",  "scene.frag.glsl"));

    programids.compute_animation = mgr.createProgram(
      ProgramManager::Definition(GL_COMPUTE_SHADER,   "#define USE_POINTERS 1\n", "animation.comp.glsl"));

    updatedPrograms(mgr);

    return mgr.areProgramsValid();
  }

  void ResourcesGL::updatedPrograms( nv_helpers_gl::ProgramManager &mgr)
  {
    programs.draw_line         = mgr.get(programids.draw_object_line);
    programs.draw_solid        = mgr.get(programids.draw_object_tris);
    programs.compute_animation = mgr.get(programids.compute_animation);

    // rebuild stateobjects

    m_state.programs++;
  }

  void ResourcesGL::deinitPrograms( nv_helpers_gl::ProgramManager &mgr)
  {
    mgr.destroyProgram(programids.draw_object_line);
    mgr.destroyProgram(programids.draw_object_tris);
    mgr.destroyProgram(programids.compute_animation);

    glUseProgram(0);
  }

  template <class T>
  static void registerSize( GLuint type )
  {
    ResourcesGL::s_token_sizes[type] = sizeof(T);
  }

  void ResourcesGL::init()
  {
    GLint uboAlignment;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&uboAlignment);
    initAlignedSizes(uboAlignment);

    m_gltimers.init( nv_helpers::Profiler::START_TIMERS);

    m_bindless_ubo = !!NVPWindow::sysExtensionSupported("GL_NV_uniform_buffer_unified_memory");
    m_cmdlist      = !!init_NV_command_list(NVPWindow::sysGetProcAddress);

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

      glCreateStatesNV(1,&stateobjects.draw_tris);
      glCreateStatesNV(1,&stateobjects.draw_line_tris);
      glCreateStatesNV(1,&stateobjects.draw_line);
    }

    glDepthFunc(GL_LESS);
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

  void ResourcesGL::nvtokenGetStats( const void* NVP_RESTRICT stream, size_t streamSize, int stats[GL_TOKENS] )
  {
    const GLubyte* NVP_RESTRICT current = (GLubyte*)stream;
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

    enableVertexFormat(VERTEX_POS,VERTEX_NORMAL);

    // temp workaround
    glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV,0,0,0);
    glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV,0,0,0);
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATERIAL,0,0);
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_MATRIX,0,0);
    glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV,UBO_SCENE,0,0);

    // we will do a series of state captures
    glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene);
    glUseProgram( programs.draw_solid );
    glStateCaptureNV( stateobjects.draw_tris, GL_TRIANGLES);
    
    glEnable(GL_POLYGON_OFFSET_FILL);
    glStateCaptureNV( stateobjects.draw_line_tris, GL_TRIANGLES);

    glUseProgram( programs.draw_line );
    glStateCaptureNV( stateobjects.draw_line, GL_LINES);

    disableVertexFormat(VERTEX_POS,VERTEX_NORMAL);

    // reset, stored in stateobjects
    glUseProgram(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void ResourcesGL::animation(Global& global)
  {
    glUseProgram(programs.compute_animation);

    global.animUbo.animatedMatrices = addresses.matrices;
    global.animUbo.originalMatrices = addresses.matricesOrig;

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffers.anim);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(AnimationData), &global.animUbo);

    if (!m_cmdlist){
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO_MATRIXOUT, buffers.matrices);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO_MATRIXORIG, buffers.matricesOrig);
    }

    glDispatchCompute((m_numMatrices + ANIMATION_WORKGROUPSIZE-1) / ANIMATION_WORKGROUPSIZE,1,1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);
  }

  void ResourcesGL::animationReset()
  {
    glNamedCopyBufferSubDataEXT(buffers.matricesOrig, buffers.matrices, 0, 0, m_numMatrices * sizeof(MatrixData) );
  }
}




