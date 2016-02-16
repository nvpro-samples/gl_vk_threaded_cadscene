/*-----------------------------------------------------------------------
  Copyright (c) 2016, NVIDIA. All rights reserved.

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

#ifndef NV_VULKANGL_H__
#define NV_VULKANGL_H__

#include <GL/glew.h>
#include <vulkan/vulkan.h>

#  if defined(__MINGW32__) || defined(__CYGWIN__)
#    define GLEXT_APIENTRY __stdcall
#  elif (_MSC_VER >= 800) || defined(_STDCALL_SUPPORTED) || defined(__BORLANDC__)
#    define GLEXT_APIENTRY __stdcall
#  else
#    define GLEXT_APIENTRY
#  endif


typedef PFN_vkVoidFunction (GLEXT_APIENTRY * PFN_glGetVkProcAddrNV) (const GLchar *name);
typedef void (GLEXT_APIENTRY * PFN_glWaitVkSemaphoreNV) (GLuint64 vkSemaphore);
typedef void (GLEXT_APIENTRY * PFN_glSignalVkSemaphoreNV) (GLuint64 vkSemaphore);
typedef void (GLEXT_APIENTRY * PFN_glSignalVkFenceNV) (GLuint64 vkFence);
typedef void (GLEXT_APIENTRY * PFN_glDrawVkImageNV) (GLuint64 vkImage, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1);

extern PFN_glGetVkProcAddrNV          __nvkglGetVkProcAddrNV;
extern PFN_glWaitVkSemaphoreNV        __nvkglWaitVkSemaphoreNV;
extern PFN_glSignalVkSemaphoreNV      __nvkglSignalVkSemaphoreNV;
extern PFN_glSignalVkFenceNV          __nvkglSignalVkFenceNV;
extern PFN_glDrawVkImageNV            __nvkglDrawVkImageNV;

inline PFN_vkVoidFunction  glGetVkProcAddrNV (const GLchar *name)
{
  return __nvkglGetVkProcAddrNV(name);
}
inline void glWaitVkSemaphoreNV (GLuint64 vkSemaphore)
{
  __nvkglWaitVkSemaphoreNV(vkSemaphore);
}
inline void glSignalVkSemaphoreNV (GLuint64 vkSemaphore)
{
  __nvkglSignalVkSemaphoreNV(vkSemaphore);
}
inline void glSignalVkFenceNV (GLuint64 vkFence)
{
  __nvkglSignalVkFenceNV(vkFence);
}
inline void glDrawVkImageNV (GLuint64 vkImage, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1)
{
  __nvkglDrawVkImageNV(vkImage, sampler, x0, y0, x1, y1, z, s0, t0, s1, t1);
}

typedef void (*NVKGLPROC)(void);
int init_NV_draw_vulkan_image(NVKGLPROC (*fnGetProc)(const char* name));


#endif
