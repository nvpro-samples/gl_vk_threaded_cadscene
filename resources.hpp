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

#include <platform.h>
#include "cadscene.hpp"
#include <nv_math/nv_math_glsltypes.h>
#include <nv_helpers_gl/programmanager.hpp>
#include <nv_helpers/profiler.hpp>
#include <algorithm>


using namespace nv_math;
#include "common.h"

namespace csfthreaded {
  
  enum ShadeType {
    SHADE_SOLID,
    SHADE_SOLIDWIRE,
    NUM_SHADES,
  };

  inline size_t alignedSize(size_t align, size_t sz){
    return ((sz + align-1)/align)*align;
  }

  struct PointerStream {
    unsigned char*  NVP_RESTRICT  dataptr;
    size_t                        used;
    size_t                        allocated;

    void init(void* data, size_t datasize)
    {
      dataptr = (unsigned char*  NVP_RESTRICT)data;
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
  
  template<class Theader, class Tcontent, int idname, class Tdef>
  class Token {
  public:
    static const int ID = idname;

    union{
      Theader   header;
      Tcontent  cmd;
    };

    Token() {
      header = Tdef::s_token_headers[idname];
    }

    void enqueue( std::string& stream )
    {
      std::string item = std::string((const char*)this,sizeof(Token));
      stream += item;
    }

    void enqueue( PointerStream &stream)
    {
      assert( sizeof(Token)+stream.used <= stream.allocated );
      memcpy(stream.dataptr + stream.used, this, sizeof(Token));
      stream.used += sizeof(Token);
    }
  };

  template<class T>
  class Nulled {
  public:
    T  m_value;
    Nulled() : m_value(0) {}

    Nulled( T b) : m_value(b) {}
    operator T() const { return m_value; }
    operator T&() { return m_value; }
    T * operator &() { return &m_value; }
    Nulled& operator=( T b) { m_value = b; return *this; }
  };

  class Resources {
  public:
    struct Global {
      SceneData             sceneUbo;
      AnimationData         animUbo;
      int                   width;
      int                   height;
    };

    int     m_threads;
    int     m_workingSet;
    bool    m_sorted;
    double  m_percent;

    unsigned int   m_frame;

    unsigned int       m_alignedMatrixSize;
    unsigned int       m_alignedMaterialSize;

    Resources() : m_frame(0)
    { }

    virtual void synchronize() {}

    virtual void init() {}
    virtual void deinit(nv_helpers_gl::ProgramManager &mgr) {}
    
    virtual bool initPrograms(nv_helpers_gl::ProgramManager &mgr) { return true;}
    virtual void updatedPrograms(nv_helpers_gl::ProgramManager &mgr) {}

    virtual bool initFramebuffer(int width, int height, int msaa) { return true;}

    virtual bool initScene(const CadScene&) { return true; }
    virtual void deinitScene() {}

    virtual void animation(Global& global) {}
    virtual void animationReset() {}

    virtual void beginFrame() {}
    virtual void endFrame() {}

    virtual nv_math::mat4f perspectiveProjection( float fovy, float aspect, float nearPlane, float farPlane) const {
      return nv_math::perspective(fovy, aspect, nearPlane, farPlane);
    }

    virtual nv_helpers::Profiler::GPUInterface*  getTimerInterface() { return NULL; }

    inline void initAlignedSizes(unsigned int alignment){
      m_alignedMatrixSize   = (unsigned int)(alignedSize(alignment, sizeof(CadScene::MatrixNode)));
      m_alignedMaterialSize = (unsigned int)(alignedSize(alignment, sizeof(CadScene::Material)));
    }
  };
}
