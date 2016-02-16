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

#include "nvdrawvulkanimage.h"

PFN_glGetVkProcAddrNV           __nvkglGetVkProcAddrNV = 0;
PFN_glWaitVkSemaphoreNV         __nvkglWaitVkSemaphoreNV = 0;
PFN_glSignalVkSemaphoreNV       __nvkglSignalVkSemaphoreNV = 0;
PFN_glSignalVkFenceNV           __nvkglSignalVkFenceNV = 0;
PFN_glDrawVkImageNV             __nvkglDrawVkImageNV = 0;


static int initedNVdrawvulkanimage = 0;
static int resultNVdrawvulkanimage = 0;

int init_NV_draw_vulkan_image(NVKGLPROC (*fnGetProc)(const char* name))
{
  if (initedNVdrawvulkanimage) return resultNVdrawvulkanimage;

  __nvkglGetVkProcAddrNV      = (PFN_glGetVkProcAddrNV)     fnGetProc("glGetVkProcAddrNV");
  __nvkglWaitVkSemaphoreNV    = (PFN_glWaitVkSemaphoreNV)   fnGetProc("glWaitVkSemaphoreNV");
  __nvkglSignalVkSemaphoreNV  = (PFN_glSignalVkSemaphoreNV) fnGetProc("glSignalVkSemaphoreNV");
  __nvkglSignalVkFenceNV      = (PFN_glSignalVkFenceNV)     fnGetProc("glSignalVkFenceNV");
  __nvkglDrawVkImageNV        = (PFN_glDrawVkImageNV)       fnGetProc("glDrawVkImageNV");

  initedNVdrawvulkanimage = 1;

  int success = 1;
  success = success && __nvkglGetVkProcAddrNV != 0;
  success = success && __nvkglWaitVkSemaphoreNV != 0;
  success = success && __nvkglSignalVkSemaphoreNV != 0;
  success = success && __nvkglSignalVkFenceNV != 0;
  success = success && __nvkglDrawVkImageNV != 0;

  resultNVdrawvulkanimage = success;
  return resultNVdrawvulkanimage; 
}
