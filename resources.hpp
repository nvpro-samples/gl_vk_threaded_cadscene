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

#include <platform.h>
#include "cadscene.hpp"
#include <nvgl/glsltypes_gl.hpp>
#include <nvh/profiler.hpp>
#if HAS_OPENGL
#include <nvgl/contextwindow_gl.hpp>
typedef nvgl::ContextWindowGL ContextWindow;
#else
#include <nvvk/contextwindow_vk.hpp>
typedef nvvk::ContextWindowVK ContextWindow;
#endif   

#include <algorithm>

struct ImDrawData;

using namespace nvmath;
#include "common.h"


namespace csfthreaded {
  
  enum ShadeType {
    SHADE_SOLID,
    SHADE_SOLIDWIRE,
    NUM_SHADES,
  };

  inline size_t alignedSize(size_t sz, size_t align){
    return ((sz + align-1)/align)*align;
  }

  struct PointerStream {
    unsigned char*  NV_RESTRICT  dataptr;
    size_t                        used;
    size_t                        allocated;

    void init(void* data, size_t datasize)
    {
      dataptr = (unsigned char*  NV_RESTRICT)data;
      allocated = datasize;
      used = 0;
    }

    void clear() {
      used = 0;
    }

    size_t size() {
      return used;
    }

  };

  class Resources {
  public:
    static uint32_t s_vkDevice;
    static uint32_t s_glDevice;


    struct Global {
      SceneData             sceneUbo;
      AnimationData         animUbo;
      int                   width;
      int                   height;
      int                   workingSet;
      const ImDrawData* imguiDrawData;
    };
    
    uint32_t    m_numMatrices;

    uint32_t    m_frame;

    uint32_t    m_alignedMatrixSize;
    uint32_t    m_alignedMaterialSize;

    Resources() : m_frame(0)
    { }
    
    virtual void synchronize() {}

    virtual bool init(ContextWindow* window, nvh::Profiler* profiler) { return false; }
    virtual void deinit() {}
    
    virtual bool initPrograms(const std::string& path, const std::string& prepend) { return true;}
    virtual void reloadPrograms(const std::string& prepend) {}

    virtual bool initFramebuffer(int width, int height, int msaa, bool vsync) { return true;}

    virtual bool initScene(const CadScene&) { return true; }
    virtual void deinitScene() {}

    virtual void animation(const Global& global) {}
    virtual void animationReset() {}

    virtual void beginFrame() {}
    virtual void blitFrame(const Global& global) {}
    virtual void endFrame() {}

    virtual nvmath::mat4f perspectiveProjection( float fovy, float aspect, float nearPlane, float farPlane) const {
      return nvmath::perspective(fovy, aspect, nearPlane, farPlane);
    }

    inline void initAlignedSizes(unsigned int alignment){
      m_alignedMatrixSize   = (uint32_t)(alignedSize(sizeof(CadScene::MatrixNode), alignment));
      m_alignedMaterialSize = (uint32_t)(alignedSize(sizeof(CadScene::Material), alignment));
    }
  };
}
