# Vulkan Device-Generated Commands

In this document we discuss the additions to the sample to showcase the **VK_NVX_device_generated_commands** extension. For an overview on this extesnion, we recommend to have a look at this [article](https://developer.nvidia.com/device-generated-commands-vulkan).

## Highlighted Files

* vk_nvx_device_generated_commands.h/cpp: contains the additions to the Vulkan api, as well as some very basic comments. The official extension spec integration can be found at [khronos](https://www.khronos.org/registry/vulkan/specs/1.0-extensions/xhtml/vkspec.html#VK_NVX_device_generated_commands).
* renderer_vkgen.cpp: contains the new renderers
* resources_vkgen.cpp/hpp: contains the additions to resources handling

## What does vkCmdProcessCommandsNVX and vkCmdReserveSpaceForCommandsNVX do?

`vkCmdProcessCommandsNVX` command triggers the recording of command buffer data on the device by using content stored in `VkBuffer`,`VkObjectTableNVX` and `VkIndirectCommandsLayoutNVX` as input data. 

The recording space is typically reserved in a target command buffer via `vkCmdReserveSpaceForCommandsNVX`. Execution of such a command buffer is triggered as usual with  `VkCmdExecuteCommands`. The reserve command is basically the equivalent as if we were recording the commands on the CPU instead. It is important to provide good estimates on the maxSequencesCount as well as use the appropriate objectTable state, as the conservative command buffer memory allocation still happens on the CPU.

`VkIndirectCommandsLayoutNVX` encodes the command sequence we want to generate, as well as data that needs to be known at creation time. The creation and management of this object should be treated similar to `vkPipeline`.

``` cpp
typedef enum VkIndirectCommandsLayoutUsageFlagBitsNVX {
  // sequences can be processed in implementation dependent order
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_UNORDERED_SEQUENCES_BIT_NVX = 0x00000001,
  
  // likely generated with a high difference in actual sequencesCount and maxSequencesCount
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_SPARSE_SEQUENCES_BIT_NVX    = 0x00000002,
  
  // likely to contain many draw calls with instanceCount or vertex/indexCount set to zero.
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EMPTY_EXECUTIONS_BIT_NVX    = 0x00000004,
  
  // developer provides permutation
  VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_NVX       = 0x00000008,
} VkIndirectCommandsLayoutUsageFlagBitsNVX;

typedef struct VkIndirectCommandsLayoutTokenNVX {
  VkIndirectCommandsTokenTypeNVX  type;
  
  // binding unit for vertex attribute / descriptor set
  uint32_t                        bindingUnit;
  
  // number of variable dynamic values for descriptor set / pushconstants 
  // or 0/1 usage of offset for index/vertexbuffer
  uint32_t                        dynamicCount; 
  
  // how the data stream is accessed per sequence: tokendata[ sequence/divisor ]
  uint32_t                        divisor;
} VkIndirectCommandsLayoutTokenNVX;
typedef struct VkIndirectCommandsLayoutCreateInfoNVX {
  VkStructureType                             sType;
  const void*                                 pNext;

  VkPipelineBindPoint                         pipelineBindPoint;
  VkIndirectCommandsLayoutUsageFlagsNVX       flags;
  uint32_t                                    tokenCount;
  const VkIndirectCommandsLayoutTokenNVX*     pTokens;
} VkIndirectCommandsLayoutCreateInfoNVX;
```

Let's have a look in detail on the generation process by the following emulation code:

``` cpp
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

typedef struct VkIndirectCommandsTokenNVX {
  VkIndirectCommandsTokenTypeNVX  tokenType; 

  // buffer containing tableEntries and additional data for execution
  VkBuffer                        buffer;    
  VkDeviceSize                    offset;
} VkIndirectCommandsTokenNVX;

typedef struct VkCmdProcessCommandsInfoNVX
{
  VkStructureType                       sType;
  const void*                           pNext;

  // handle to the table which manages the VK objects/objects
  VkObjectTableNVX                      objectTable;

  // specify the layout of the inputs which will be converted into real draws
  VkIndirectCommandsLayoutNVX           indirectCommandsLayout;

  // number of inputs. has to match the number in the indirectCommandsLayout
  uint32_t                              indirectCommandsTokenCount;

  // specify the buffer+offset for each input
  const VkIndirectCommandsTokenNVX*     pIndirectCommandsTokens;

  // number of sequences (or max number of sequences if the actual count is sourced from sequenceCountBuffer)
  uint32_t                              maxSequencesCount;

  // will hold the actual commands for execution (must have called vkCmdReserveCommandsNV on it)
  // if NULL immediately executed as part of commandBuffer
  VkCommandBuffer                       targetCommandBuffer;

  // if not NULL, will hold the actual number of elements
  VkBuffer                              sequencesCountBuffer;
  // must be aligned to limits.minSequenceCountBufferOffsetAlignment
  VkDeviceSize                          sequencesCountOffset;

  // if not NULL, will hold the indices for sequences (mandatory for
  // VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_NVX, otherwise must be NULL)
  VkBuffer                              sequencesIndexBuffer;
  // must be aligned to limits.minSequenceIndexBufferOffsetAlignment
  VkDeviceSize                          sequencesIndexOffset;
}VkCmdProcessCommandsInfoNVX;


vkCmdProcessCommands(vkCommandBuffer commandBuffer, const VkCmdProcessCommandsInfoNVX* info)
{
  // VkCmdProcessCommandsInfoNVX members are used directly

  // For targetCommandBuffers the existing reservedSpace is reset & overwritten.
  vkCommandBuffer cmd = targetCommandBuffer ? targetCommandBuffer.reservedSpace : commandBuffer;

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
  
// The state of cmd after the generated commands should be considered undefined, as if
// an unknown indirectCommandsLayout as well as objectTable were used.
}
```

## Renderers

All renderers make use of the separate generation and execution model that the extension provides. The motivation is that we can recycle command buffer memory easier.

Be aware while the main goal of the extension is to do work reduction on the GPU (culling etc.), this sample is kept rather simple and renders from a static scene description.

- **vk generate cmd reset**
The command buffer in which the commands are recorded is reset every frame. To avoid synchronization overhead (cannot reset a command-buffer in flight) every frame another one is picked (there are ResourcesVK::MAX_BUFFERED_FRAMES many). 
- **vk generate cmd re-use**
The command buffer is re-used completely, we only reserve the space once and keep re-using a single command buffer every frame. 
- **vk generate cmd re-use seqidx**
Similar to the above case, but we provide our own (random) ordering of the drawcalls, by using the functionality provided by the `VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_NVX` flag. The random ordering for drawcalls creates lots of GPU state changes, which makes it very slow in execution for this particular "worst case" demo. 

## Setup

#### ObjectTable

In `ResourcesVKGen::initScene` you will see that after the regular scene data initialization (vertex buffers, memory etc.), we now create the object table in which we register all resources that we later want to use in the device generation process.

Ideally be as accurate as possible, as the more objects you will use, the more memory we need to allocate internally.

The Vulkan objects that the table references must be kept alive, just as if they were used directly in both generating or equivalent target command buffer. You can re-use indices in the table, when the command buffers that indirectly reference the object have completed execution on the device. If you are not modifying indices that are inflight, it is safe to register or unregister other indices.

#### IndirectCommandsLayout

In `RendererVKGen::InitGenerator` we are setting up the command sequence (one `VkIndirectCommandsLayoutTokenNVX` for each command in the sequence).

The object lifetime must be handled just like any other vulkan object referenced in command buffers.

#### IndirectCommandsToken

As was mentioned in the begging our scene is static, and we just re-generate the same commands every frame. We generate a single buffer that holds all our inputs and make use of the offsets, but you could use multiple buffers as well.

The creation of the input data is handled in `RendererVKGen::GenerateIndirectTokenData` and only done once at renderer initialization time.


## Rendering

#### Reservation

The `renderer_vkgen.cpp - RendererVKGen::setupTarget`function sets up the target's command buffer state and reserves the space. We still need to setup the regular state like render pass from the CPU. Depending on which commands our sequence would generate, we can rely on inheriting some state as well. For example the generated sequence in the sample only updates matrix/material descriptorset, while the view descriptorset is bound prior reservation and inherited. The re-use renderers call the setup only once, while the other renderers execute it every frame. Re-using works if you don't introduce new references in the object table.

#### Generation & Execution
Both actions are triggered in `RendererVKGen::draw`, although you will find more details on the generation process in `RendererVKGen::build`. 


## Performance

As the generation now happens on the GPU, the performance at which we can generate is relevant. The sample introduces two dedicated timers "Build" and "Exec", so you can see how fast each step is. Sometimes "Exec" can be faster than CPU-generated commands.

As the feature is still very new and experimental, if you run into performance bottlenecks regarding the command processing please give us your feedback. The generation is faster on GPUs with more Streaming Multiprocessors than on those with less.

### Results
Here are some preliminary timing results from a system with a first generation i7-860 CPU and a GP104-based graphics card (20 Streaming Multiprocessors). The numbers are provided to get a rough idea of the feature's performance. 

9 x GeForce model

> Note: The CPU time below normally happens asynchronously to rendering, and would not affect a frame's GPU time much.

#### Solid, Material-Grouped, 9 model copies
~ 100 000 draw calls (no pipeline switches, all use the same)

Sequence of 5 commands (inherits pipeline and one descriptorset with the scene ubo):

* `VK_INDIRECT_COMMANDS_TOKEN_DESCRIPTOR_SET_NVX` (1 ubo) + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_DESCRIPTOR_SET_NVX` (1 ubo) + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_INDEX_BUFFER_NVX` + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_VERTEX_BUFFER_NVX` + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_DRAW_INDEXED_NVX`

renderer                    | Build time | Exec time GPU [ms]| 
--------------------------- | ---------- | ------- |
vk cmd 1 worker thread      | 3.78 (CPU) |    1.82 |
                            |            |         |
vk generate cmd re-use      | 0.20       |    1.75 |

#### Solid with Edges, Material-Grouped, 9 model copies

~ 200 000 draw calls / ~ 45 000 effective pipeline switches (two pipelines used)

Sequence of 6 commands (inherits descriptorset with the scene ubo)

* `VK_INDIRECT_COMMANDS_TOKEN_PIPELINE_NVX`
* `VK_INDIRECT_COMMANDS_TOKEN_DESCRIPTOR_SET_NVX` (1 ubo) + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_DESCRIPTOR_SET_NVX` (1 ubo) + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_INDEX_BUFFER_NVX` + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_VERTEX_BUFFER_NVX` + 1 dynamic offset
* `VK_INDIRECT_COMMANDS_TOKEN_DRAW_INDEXED_NVX`

renderer                    | Build time | Exec time GPU [ms]| 
--------------------------- | ---------- | -------- |
vk cmd 1 worker thread      | 8.74 (CPU) |    14.74 |
                            |            |          |
vk generate cmd re-use      | 0.34 (GPU) |     8.12 |

Here the execution of the generated commands is faster than a traditional CPU recorded command-buffer.
This benefit, however, depends on the resources (pipeline, descriptorsets etc.) being used in the object table.
