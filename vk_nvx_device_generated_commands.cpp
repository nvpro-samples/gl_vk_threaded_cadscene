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

#include "vk_nvx_device_generated_commands.h"

PFN_vkCmdProcessCommandsNVX            pfn_vkCmdProcessCommandsNVX = 0;
PFN_vkCmdReserveSpaceForCommandsNVX    pfn_vkCmdReserveSpaceForCommandsNVX = 0;
PFN_vkCreateIndirectCommandsLayoutNVX  pfn_vkCreateIndirectCommandsLayoutNVX = 0;
PFN_vkDestroyIndirectCommandsLayoutNVX pfn_vkDestroyIndirectCommandsLayoutNVX = 0;
PFN_vkCreateObjectTableNVX             pfn_vkCreateObjectTableNVX = 0;
PFN_vkDestroyObjectTableNVX            pfn_vkDestroyObjectTableNVX = 0;
PFN_vkRegisterObjectsNVX               pfn_vkRegisterObjectsNVX = 0;
PFN_vkUnregisterObjectsNVX             pfn_vkUnregisterObjectsNVX = 0;
PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX  pfn_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX = 0;


static int s_VK_NVX_device_generated_commands = 0;
static int s_result_VK_NVX_device_generated_commands = 0;

int load_VK_NVX_device_generated_commands(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr)
{
  int success = 1;

#define GET_FUNC(name) \
  pfn_##name = (PFN_##name)getInstanceProcAddr(instance, #name); \
  success = success && pfn_##name != 0;


  GET_FUNC(vkCmdProcessCommandsNVX);
  GET_FUNC(vkCmdReserveSpaceForCommandsNVX);
  GET_FUNC(vkCreateIndirectCommandsLayoutNVX);
  GET_FUNC(vkDestroyIndirectCommandsLayoutNVX);
  GET_FUNC(vkCreateObjectTableNVX);
  GET_FUNC(vkDestroyObjectTableNVX);
  GET_FUNC(vkRegisterObjectsNVX);
  GET_FUNC(vkUnregisterObjectsNVX);
  GET_FUNC(vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX);

#undef GET_FUNC

  return success;
}
