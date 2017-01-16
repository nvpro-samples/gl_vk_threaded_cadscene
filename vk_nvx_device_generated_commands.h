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

#ifndef VK_NVX_DEVICE_GENERATED_COMMANDS_H__
#define VK_NVX_DEVICE_GENERATED_COMMANDS_H__

#if USEVULKANSDK
#include <vulkan/vulkan.h>
#else
#include "vkfnptrinline.h"
#endif

#include <assert.h>

#  if defined(__MINGW32__) || defined(__CYGWIN__)
#    define GLEXT_APIENTRY __stdcall
#  elif (_MSC_VER >= 800) || defined(_STDCALL_SUPPORTED) || defined(__BORLANDC__)
#    define GLEXT_APIENTRY __stdcall
#  else
#    define GLEXT_APIENTRY
#  endif


#ifndef VK_NVX_device_generated_commands 
#define VK_NVX_device_generated_commands 1

#define VK_NVX_DEVICE_GENERATED_COMMANDS_SPEC_VERSION   1
#define VK_NVX_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME "VK_NVX_device_generated_commands"

VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkObjectTableNVX)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkIndirectCommandsLayoutNVX)

#define VK_STRUCTURE_TYPE_OBJECT_TABLE_CREATE_INFO_NVX                    ((VkStructureType)1000086000)
#define VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NVX        ((VkStructureType)1000086001)
#define VK_STRUCTURE_TYPE_CMD_PROCESS_COMMANDS_INFO_NVX                   ((VkStructureType)1000086002)
#define VK_STRUCTURE_TYPE_CMD_RESERVE_SPACE_FOR_COMMANDS_INFO_NVX         ((VkStructureType)1000086003)
#define VK_STRUCTURE_TYPE_DEVICE_GENERATED_COMMANDS_LIMITS_NVX            ((VkStructureType)1000086004)
#define VK_STRUCTURE_TYPE_DEVICE_GENERATED_COMMANDS_FEATURES_NVX          ((VkStructureType)1000086005)

#define VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX                         ((VkPipelineStageFlagBits)0x00020000)

#define VK_ACCESS_COMMAND_PROCESS_READ_BIT_NVX                            ((VkAccessFlagBits)0x00020000)
#define VK_ACCESS_COMMAND_PROCESS_WRITE_BIT_NVX                           ((VkAccessFlagBits)0x00040000)

enum VkIndirectCommandsLayoutUsageFlagBitsNVX {
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_UNORDERED_SEQUENCES_BIT_NVX = 0x00000001,  // sequences can be processed in implementation dependent order
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_SPARSE_SEQUENCES_BIT_NVX    = 0x00000002,  // likely generated with a high difference in actual sequencesCount and maxSequencesCount
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EMPTY_EXECUTIONS_BIT_NVX    = 0x00000004,  // likely to contain many draw calls with instanceCount or vertex/indexCount set to zero.
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_BIT_NVX       = 0x00000008,  // developer provides permutation
};
typedef VkFlags VkIndirectCommandsLayoutUsageFlagsNVX;

typedef enum VkIndirectCommandsTokenTypeNVX {
  VK_INDIRECT_COMMANDS_TOKEN_PIPELINE_NVX,       // an array of 32bit tableEntry in the object table                    
  VK_INDIRECT_COMMANDS_TOKEN_DESCRIPTOR_SET_NVX, // an array of (32 bit tableEntry + variable count 32bit offsets )
  VK_INDIRECT_COMMANDS_TOKEN_INDEX_BUFFER_NVX,   // an array of (32 bit tableEntry + optional 32bit offset)
  VK_INDIRECT_COMMANDS_TOKEN_VERTEX_BUFFER_NVX,  // an array of (32 bit tableEntry + optional 32bit offset)
  VK_INDIRECT_COMMANDS_TOKEN_PUSH_CONSTANT_NVX,  // an array of (32 bit tableEntry + variable count 32bit values)
  VK_INDIRECT_COMMANDS_TOKEN_DRAW_INDEXED_NVX,   // an array of VkDrawIndexedIndirectCommand
  VK_INDIRECT_COMMANDS_TOKEN_DRAW_NVX,           // an array of VkDrawIndirectCommand
  VK_INDIRECT_COMMANDS_TOKEN_DISPATCH_NVX,       // an array of VkDispatchIndirectCommand
} VkIndirectCommandsTokenTypeNVX;

typedef enum VkObjectEntryUsageFlagBitsNVX {
  VK_OBJECT_ENTRY_USAGE_GRAPHICS_BIT_NVX = 0x00000001,
  VK_OBJECT_ENTRY_USAGE_COMPUTE_BIT_NVX  = 0x00000002,
} VkObjectEntryUsageFlagBitsNVX;
typedef VkFlags VkObjectEntryUsageFlagsNVX;

typedef enum VkObjectEntryTypeNVX {
  VK_OBJECT_ENTRY_DESCRIPTOR_SET_NVX,
  VK_OBJECT_ENTRY_PIPELINE_NVX,
  VK_OBJECT_ENTRY_INDEX_BUFFER_NVX,
  VK_OBJECT_ENTRY_VERTEX_BUFFER_NVX,
  VK_OBJECT_ENTRY_PUSH_CONSTANT_NVX,
} VkObjectEntryTypeNVX;

typedef struct VkDeviceGeneratedCommandsFeaturesNVX {
  VkStructureType sType;                            // VK_STRUCTURE_TYPE_DEVICE_GENERATED_COMMANDS_FEATURES_NVX
  const void*     pNext;

  VkBool32        computeBindingPointSupport;       // FALSE for early drivers TRUE later
} VkDeviceGeneratedCommandsFeaturesNVX;

typedef struct VkDeviceGeneratedCommandsLimitsNVX {
  VkStructureType sType;                            // VK_STRUCTURE_TYPE_DEVICE_GENERATED_COMMANDS_LIMITS_NVX
  const void*     pNext;

  uint32_t        maxIndirectCommandsLayoutTokenCount;    // 32
  uint32_t        maxObjectEntryCounts;                   // int32_t max

  VkDeviceSize    minSequenceCountBufferOffsetAlignment;  // 256
  VkDeviceSize    minSequenceIndexBufferOffsetAlignment;  // 32
  VkDeviceSize    minCommandsTokenBufferOffsetAlignment;  // 32
} VkDeviceGeneratedCommandsLimitsNVX;

typedef struct VkIndirectCommandsTokenNVX {
  VkIndirectCommandsTokenTypeNVX  tokenType; // 
  VkBuffer                        buffer;    // buffer containing tableEntries and additional data for execution
  VkDeviceSize                    offset;    // offset from the base address of the buffer
} VkIndirectCommandsTokenNVX;

typedef struct VkIndirectCommandsLayoutTokenNVX {
  VkIndirectCommandsTokenTypeNVX  tokenType;    // type of this input
  uint32_t                        bindingUnit;  // binding unit for vertex attribute / descriptor set
  uint32_t                        dynamicCount; // number of variable dynamic values for descriptor set / pushconstants
  uint32_t                        divisor;      // at which rate the array is advanced per element (must be 1 if VK_INDIRECT_COMMANDS_LAYOUT_USAGE_PACKED_TOKENS_NVX set)
} VkIndirectCommandsLayoutTokenNVX;

typedef struct VkIndirectCommandsLayoutCreateInfoNVX {
  VkStructureType                             sType;
  const void*                                 pNext;

  VkPipelineBindPoint                         pipelineBindPoint;
  VkIndirectCommandsLayoutUsageFlagsNVX       flags;
  uint32_t                                    tokenCount;    // number of inputs
  const VkIndirectCommandsLayoutTokenNVX*     pTokens;
} VkIndirectCommandsLayoutCreateInfoNVX;

typedef struct VkObjectTableCreateInfoNVX {
  VkStructureType               sType;
  const void*                   pNext;

  uint32_t                      objectCount;        // number of objects types
  const VkObjectEntryTypeNVX*   pObjectEntryTypes;  // type of each object array
  const uint32_t*               pObjectEntryCounts; // size of the array for each type
  const VkObjectEntryUsageFlagsNVX*  pObjectEntryUsageFlags;
  uint32_t                      maxUniformBuffersPerDescriptor;
  uint32_t                      maxStorageBuffersPerDescriptor;
  uint32_t                      maxStorageImagesPerDescriptor;
  uint32_t                      maxSampledImagesPerDescriptor;
  uint32_t                      maxPipelineLayouts;
} VkObjectTableCreateInfoNVX;

typedef struct VkCmdProcessCommandsInfoNVX
{
  VkStructureType                       sType;
  const void*                           pNext;

  VkObjectTableNVX                      objectTable;                 // handle to the table which manages the VK objects/objects
  VkIndirectCommandsLayoutNVX           indirectCommandsLayout;      // specify the layout of the inputs which will be converted into real draws
  uint32_t                              indirectCommandsTokenCount;  // number of inputs. has to match the number in the indirectCommandsLayout
  const VkIndirectCommandsTokenNVX*     pIndirectCommandsTokens;     // specify the base+offset for each input
  uint32_t                              maxSequencesCount;           // number of elements in each input. (or max number of elements if the actual count is sourced from sequenceCountBuffer)
  VkCommandBuffer                       targetCommandBuffer;         // will hold the actual commands for execution (must have called vkCmdReserveCommandsNV on it)
  // if NULL immediately executed as part of commandBuffer
  VkBuffer                              sequencesCountBuffer;        // if not NULL, will hold the actual number of elements
  VkDeviceSize                          sequencesCountOffset;        // offset from the base address specified by sequenceCountBuffer, must be UBO aligned

  VkBuffer                              sequencesIndexBuffer;        // if not NULL, will hold the indices for sequences (mandatory for
  VkDeviceSize                          sequencesIndexOffset;        // VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_NVX, otherwise must be NULL)
}VkCmdProcessCommandsInfoNVX;

typedef struct VkCmdReserveSpaceForCommandsInfoNVX
{
  VkStructureType                       sType;
  const void*                           pNext;

  VkObjectTableNVX                      objectTable;
  VkIndirectCommandsLayoutNVX           indirectCommandsLayout;
  uint32_t                              maxSequencesCount;
}VkCmdReserveSpaceForCommandsInfoNVX;

typedef struct VkObjectTableEntryNVX {
  VkObjectEntryTypeNVX        sType;
  VkObjectEntryUsageFlagsNVX  flags;
} VkObjectTableEntryNVX;

typedef struct VkObjectTablePipelineEntryNVX {
  VkObjectEntryTypeNVX        sType;
  VkObjectEntryUsageFlagsNVX  flags;
  VkPipeline                  pipeline;
} VkObjectTablePipelineEntryNVX;

typedef struct VkObjectTableDescriptorSetEntryNVX {
  VkObjectEntryTypeNVX        sType;
  VkObjectEntryUsageFlagsNVX  flags;
  VkPipelineLayout            pipelineLayout;
  VkDescriptorSet             descriptorSet;
} VkObjectTableDescriptorSetEntryNVX;

typedef struct VkObjectTableVertexBufferEntryNVX {
  VkObjectEntryTypeNVX        sType;
  VkObjectEntryUsageFlagsNVX  flags;
  VkBuffer                    buffer;
} VkObjectTableVertexBufferEntryNVX;

typedef struct VkObjectTableIndexBufferEntryNVX {
  VkObjectEntryTypeNVX        sType;
  VkObjectEntryUsageFlagsNVX  flags;
  VkBuffer                    buffer;
  VkIndexType                 indexType;
} VkObjectTableIndexBufferEntryNVX;

typedef struct VkObjectTablePushConstantEntryNVX {
  VkObjectEntryTypeNVX        sType;
  VkObjectEntryUsageFlagsNVX  flags;
  VkPipelineLayout            pipelineLayout;
  VkShaderStageFlags          stageFlags;
} VkObjectTablePushConstantEntryNVX;

typedef void (VKAPI_PTR *PFN_vkCmdProcessCommandsNVX)(
  VkCommandBuffer                           commandBuffer,
  const VkCmdProcessCommandsInfoNVX*        processCommandsInfo);

typedef void (VKAPI_PTR *PFN_vkCmdReserveSpaceForCommandsNVX)(
  VkCommandBuffer                             commandBuffer,
  const VkCmdReserveSpaceForCommandsInfoNVX*  reserveSpaceInfo);

typedef VkResult(VKAPI_PTR *PFN_vkCreateIndirectCommandsLayoutNVX)(
  VkDevice                                    device,
  const VkIndirectCommandsLayoutCreateInfoNVX* pCreateInfo,
  const VkAllocationCallbacks*                pAllocator,
  VkIndirectCommandsLayoutNVX*                pIndirectCommandsLayout);

typedef void (VKAPI_PTR *PFN_vkDestroyIndirectCommandsLayoutNVX)(
  VkDevice                              device,
  VkIndirectCommandsLayoutNVX           indirectCommandsLayout,
  const VkAllocationCallbacks*          pAllocator);


typedef VkResult(VKAPI_PTR *PFN_vkCreateObjectTableNVX)(
  VkDevice                              device,
  const VkObjectTableCreateInfoNVX*     pCreateInfo,
  const VkAllocationCallbacks*          pAllocator,
  VkObjectTableNVX*                     pObjectTable);

typedef void (VKAPI_PTR *PFN_vkDestroyObjectTableNVX)(
  VkDevice                              device,
  VkObjectTableNVX                      objectTable,
  const VkAllocationCallbacks*          pAllocator);

typedef VkResult(VKAPI_PTR *PFN_vkRegisterObjectsNVX)(
  VkDevice                              device,
  VkObjectTableNVX                      objectTable,
  uint32_t                              objectCount,
  const VkObjectTableEntryNVX* const*   ppObjectTableEntries,
  const uint32_t*                       pObjectIndices);

typedef VkResult(VKAPI_PTR *PFN_vkUnregisterObjectsNVX)(
  VkDevice                              device,
  VkObjectTableNVX                      objectTable,
  uint32_t                              objectCount,
  const VkObjectEntryTypeNVX*           pObjectEntryTypes,
  const uint32_t*                       pObjectIndices);

typedef void(VKAPI_PTR *PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX)(
  VkPhysicalDevice                       physicalDevice,
  VkDeviceGeneratedCommandsFeaturesNVX*  pFeatures,
  VkDeviceGeneratedCommandsLimitsNVX*    pLimits);

/*

  vkCmdProcessCommandsNVX(procCmd,..) behaves as follows 
  (with most variables taken from VkCmdProcessCommandsInfoNVX):

  
    cmd = targetCommandBuffer ? targetCommandBuffer.reservedSpace : procCmd;

    uint32_t sequencesCount = sequencesCountBuffer ?
          min(maxSequencesCount, sequencesCountBuffer.load_uint32(sequencesCountOffset) :
          maxSequencesCount;

    for (uint32_t sequence = 0; sequence < sequencesCount; sequence++){
      uint32_t s;
      if (indirectCommandsLayout.flags & VK_INDIRECT_COMMANDS_LAYOUT_USAGE_UNORDERED_SEQUENCES_BIT) {
        s = implementation dependent non-coherent permutation[sequence];
      }
      else {
        s = sequence;
      }
      
      // use provided index
      if (indirectCommandsLayout.flags & VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_BIT) {
        s = sequencesIndexBuffer.load_uint32(sequencesIndexOffset + (s * sizeof(uint32_t)));
      }

      for (uint32_t c = 0; c < indirectCommandsLayout.tokenCount; c++){
        input   = pIndirectCommandsTokens[c];
        i       = s / indirectCommandsLayout.pTokens[c].divisor;

        switch(input.type){
          VK_INDIRECT_COMMANDS_TOKEN_PIPELINE_NVX:
          size_t    stride  = sizeof(uint32_t);
          uint32_t* data    = input.buffer.pointer( input.offset + stride * i );
          uint32_t  object  = data[0];
          
          vkCmdBindPipeline(cmd, indirectCommandsLayout.pipelineBindPoint,
            objectTable.pipelines[ object ].pipeline);
          break;

          VK_INDIRECT_COMMANDS_TOKEN_DESCRIPTOR_SET_NVX:
          size_t    stride  = sizeof(uint32_t) + sizeof(uint32_t) * indirectCommandsLayout.pTokens[c].dynamicCount;
          uint32_t* data    = input.buffer.pointer( input.offset + stride * i);
          uint32_t  object  = data[0];

          vkCmdBindDescriptorSets(cmd, indirectCommandsLayout.pipelineBindPoint,
            objectTable.descriptorsets[ object ].layout,
            indirectCommandsLayout.pTokens[ c ].bindingUnit,
            1, &objectTable.descriptorsets[ object ].descriptorSet,
            indirectCommandsLayout.pTokens[ c ].dynamicCount, data + 1);
          break;

          VK_INDIRECT_COMMANDS_TOKEN_PUSH_CONSTANT_NVX:
          size_t    stride  = sizeof(uint32_t) + sizeof(uint32_t) * indirectCommandsLayout.pTokens[c].dynamicCount;
          uint32_t* data    = input.buffer.pointer( input.offset + stride * i );
          uint32_t  object  = data[0];

          vkCmdPushConstants(cmd,
            objectTable.pushconstants[ object ].layout,
            objectTable.pushconstants[ object ].stageFlags,
            indirectCommandsLayout.pTokens[ c ].bindingUnit, indirectCommandsLayout.pTokens[c].dynamicCount, data + 1);
          break;

          VK_INDIRECT_COMMANDS_TOKEN_INDEX_BUFFER_NVX:
          size_t   s tride  = sizeof(uint32_t) + sizeof(uint32_t) * indirectCommandsLayout.pTokens[c].dynamicCount;
          uint32_t* data    = input.buffer.pointer( input.offset + stride * i );
          uint32_t  object  = data[0];

          vkCmdBindIndexBuffer(cmd,
            objectTable.vertexbuffers[ object ].buffer,
            indirectCommandsLayout.pTokens[ c ].dynamicCount ? data[1] : 0,
            objectTable.vertexbuffers[ object ].indexType);
          break;

          VK_INDIRECT_COMMANDS_TOKEN_VERTEX_BUFFER_NVX:
          size_t    stride  = sizeof(uint32_t) + sizeof(uint32_t) * indirectCommandsLayout.pTokens[c].dynamicCount;
          uint32_t* data    = input.buffer.pointer( input.offset + stride * i );
          uint32_t  object  = data[0];

          vkCmdBindVertexBuffers(cmd,
            indirectCommandsLayout.pTokens[ c ].bindingUnit, 1,
            &objectTable.vertexbuffers[ object ].buffer,
            indirectCommandsLayout.pTokens[ c ].dynamicCount ? data + 1 : {0}); // device size handled as uint32_t
          break;

          VK_INDIRECT_COMMANDS_TOKEN_DRAW_INDEXED_NVX:
          vkCmdDrawIndexedIndirect(cmd,
            input.buffer,
            sizeof(VkDrawIndexedIndirectCommand) * i + input.offset, 1, 0);
          break;

          VK_INDIRECT_COMMANDS_TOKEN_DRAW_NVX:
          vkCmdDrawIndirect(cmd,
            input.buffer,
            sizeof(VkDrawIndirectCommand) * i  + input.offset, 1, 0);
          break;
          
          VK_INDIRECT_COMMANDS_TOKEN_DISPATCH_NVX:
          vkCmdDispatchIndirect(cmd,
            input.buffer,
            sizeof(VkDispatchIndirectCommand) * i  + input.offset);
          break;
        }
      }
    }

*/  

#define VK_NVX_DEVICE_GENERATED_COMMANDS_LOCAL  1

#else

#define VK_NVX_DEVICE_GENERATED_COMMANDS_LOCAL  0

#endif

extern PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX  pfn_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX;
extern PFN_vkCmdProcessCommandsNVX             pfn_vkCmdProcessCommandsNVX;
extern PFN_vkCmdReserveSpaceForCommandsNVX     pfn_vkCmdReserveSpaceForCommandsNVX;
extern PFN_vkCreateIndirectCommandsLayoutNVX   pfn_vkCreateIndirectCommandsLayoutNVX;
extern PFN_vkDestroyIndirectCommandsLayoutNVX  pfn_vkDestroyIndirectCommandsLayoutNVX;
extern PFN_vkCreateObjectTableNVX              pfn_vkCreateObjectTableNVX;
extern PFN_vkDestroyObjectTableNVX             pfn_vkDestroyObjectTableNVX;
extern PFN_vkRegisterObjectsNVX                pfn_vkRegisterObjectsNVX;
extern PFN_vkUnregisterObjectsNVX              pfn_vkUnregisterObjectsNVX;

#if VK_NVX_DEVICE_GENERATED_COMMANDS_LOCAL || defined(VK_NO_PROTOTYPES)

inline void vkCmdProcessCommandsNVX(
  VkCommandBuffer                       commandBuffer,
  const VkCmdProcessCommandsInfoNVX*    info)
{
  assert(pfn_vkCmdProcessCommandsNVX);
  pfn_vkCmdProcessCommandsNVX(commandBuffer, info);
}

inline void vkCmdReserveSpaceForCommandsNVX(
  VkCommandBuffer                             commandBuffer,
  const VkCmdReserveSpaceForCommandsInfoNVX*  reserveInfo)
{
  assert(pfn_vkCmdProcessCommandsNVX);
  pfn_vkCmdReserveSpaceForCommandsNVX(commandBuffer, reserveInfo);
}

inline VkResult vkCreateIndirectCommandsLayoutNVX(
  VkDevice                                      device,
  const VkIndirectCommandsLayoutCreateInfoNVX*  pCreateInfo,
  const VkAllocationCallbacks*                  pAllocator,
  VkIndirectCommandsLayoutNVX*                  pIndirectCommandsLayout)
{
  assert(pfn_vkCreateIndirectCommandsLayoutNVX);
  return pfn_vkCreateIndirectCommandsLayoutNVX(device, pCreateInfo, pAllocator, pIndirectCommandsLayout);
}

inline void vkDestroyIndirectCommandsLayoutNVX(
  VkDevice                            device,
  VkIndirectCommandsLayoutNVX         indirectCommandsLayout,
  const VkAllocationCallbacks*        pAllocator)
{
  assert(pfn_vkDestroyIndirectCommandsLayoutNVX);
  pfn_vkDestroyIndirectCommandsLayoutNVX(device, indirectCommandsLayout, pAllocator);
}


inline VkResult vkCreateObjectTableNVX(
  VkDevice                            device,
  const VkObjectTableCreateInfoNVX*   pCreateInfo,
  const VkAllocationCallbacks*        pAllocator,
  VkObjectTableNVX*                   pObjectTable)
{
  assert(pfn_vkCreateObjectTableNVX);
  return pfn_vkCreateObjectTableNVX(device, pCreateInfo, pAllocator, pObjectTable);
}

inline void vkDestroyObjectTableNVX(
  VkDevice                            device,
  VkObjectTableNVX                    resourceTable,
  const VkAllocationCallbacks*        pAllocator)
{
  assert(pfn_vkDestroyObjectTableNVX);
  pfn_vkDestroyObjectTableNVX(device, resourceTable, pAllocator);
}

inline VkResult vkRegisterObjectsNVX(
  VkDevice                              device,
  VkObjectTableNVX                      objectTable,
  uint32_t                              objectCount,
  const VkObjectTableEntryNVX* const*   ppObjectTableEntries,
  const uint32_t*                       pObjectIndices)
{
  assert(pfn_vkRegisterObjectsNVX);
  return pfn_vkRegisterObjectsNVX(device, objectTable, objectCount, ppObjectTableEntries, pObjectIndices);
}

inline VkResult vkUnregisterObjectsNVX(
  VkDevice                              device,
  VkObjectTableNVX                      objectTable,
  uint32_t                              objectCount,
  const VkObjectEntryTypeNVX*           pObjectEntryTypes,
  const uint32_t*                       pObjectIndices)
{
  assert(pfn_vkUnregisterObjectsNVX);
  return pfn_vkUnregisterObjectsNVX(device, objectTable, objectCount, pObjectEntryTypes, pObjectIndices);
}

inline void vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX(
  VkPhysicalDevice                       physicalDevice,
  VkDeviceGeneratedCommandsFeaturesNVX*  pFeatures,
  VkDeviceGeneratedCommandsLimitsNVX*    pLimits)
{
  assert(pfn_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX);
  pfn_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX(physicalDevice, pFeatures, pLimits);
}

#endif

int load_VK_NVX_device_generated_commands(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr);

#endif
