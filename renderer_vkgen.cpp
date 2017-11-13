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


//#undef NDEBUG

#include <assert.h>
#include <algorithm>
#include "renderer.hpp"
#include <main.h>
#include "resources_vkgen.hpp"

#include <nv_math/nv_math_glsltypes.h>

using namespace nv_math;
#include "common.h"


namespace csfthreaded
{

  //////////////////////////////////////////////////////////////////////////
  
  class RendererVKGen: public Renderer {
  public:
    enum Mode {
      MODE_RESET,         // reset commandbuffer every frame, allocation may be re-used if new frame is similar sized
      MODE_RESET_RELEASE, // also release the resources (not recommended, slower)
      MODE_REUSE,         // reuse the commandbuffer without reseting it (refilling the reserved space)
      MODE_REUSE_IDXSEQ,     // similar as above but this time the ordering of sequences is provided by another buffer
    };

    Mode m_mode;

    class TypeReset : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVKGen::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan generate cmd reset";
      }
      Renderer* create() const
      {
        RendererVKGen* renderer = new RendererVKGen();
        renderer->m_mode = MODE_RESET;

        return renderer;
      }
      unsigned int priority() const 
      {
        return 18;
      }

      Resources* resources()
      {
        return ResourcesVKGen::get();
      }
    };

    class TypeResetFull : public Renderer::Type 
    {


      bool isAvailable() const
      {
        return ResourcesVKGen::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan generate cmd reset & release";
      }
      Renderer* create() const
      {
        RendererVKGen* renderer = new RendererVKGen();
        renderer->m_mode = MODE_RESET_RELEASE;

        return renderer;
      }
      unsigned int priority() const 
      {
        return 18;
      }

      Resources* resources()
      {
        return ResourcesVKGen::get();
      }
    };

    class TypeReuse : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVKGen::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan generate cmd re-use";
      }
      Renderer* create() const
      {
        RendererVKGen* renderer = new RendererVKGen();
        renderer->m_mode = MODE_REUSE;

        return renderer;
      }
      unsigned int priority() const 
      {
        return 18;
      }

      Resources* resources()
      {
        return ResourcesVKGen::get();
      }
    };

    class TypeReuseSeq : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVKGen::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan generate cmd re-use seqidx";
      }
      Renderer* create() const
      {
        RendererVKGen* renderer = new RendererVKGen();
        renderer->m_mode = MODE_REUSE_IDXSEQ;

        return renderer;
      }
      unsigned int priority() const 
      {
        return 18;
      }

      Resources* resources()
      {
        return ResourcesVKGen::get();
      }
    };

  public:

    void init(const CadScene* NVP_RESTRICT scene, Resources* resources);
    void deinit();
    
    void build(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager);
    void draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager);

    void blit(ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global );

    RendererVKGen()
    {

    }

  private:
    size_t                              m_pipeIncarnation;

    struct ShadeCommand {
      std::vector<VkIndirectCommandsTokenNVX>  inputs;

      VkIndirectCommandsLayoutNVX       indirectCmdsLayout;
      

      VkDeviceMemory                    inputMemory;
      VkBuffer                          inputBuffer;
      size_t                            inputSequenceIndexOffset;

      uint32_t                          sequencesCount;

      VkCommandBuffer                   cmdBuffer[ResourcesVK::MAX_BUFFERED_FRAMES];
    };

    std::vector<DrawItem>               m_drawItems;
    VkCommandBuffer                     m_targetCommandBuffer;
    VkCommandPool                       m_cmdPool;

    // used for token or cmdbuffer
    ShadeCommand                        m_shades[NUM_SHADES];
    ResourcesVKGen* NVP_RESTRICT        m_resources;

    void setupTarget(ShadeType shadetype, VkCommandBuffer target, ResourcesVKGen* res, uint32_t maxCount);

    void GenerateIndirectTokenData(ShadeType shadetype, const DrawItem* NVP_RESTRICT drawItems, size_t num,  ResourcesVKGen* NVP_RESTRICT res )
    {
      m_resources->synchronize();

      ShadeCommand& sc = m_shades[shadetype];
      const CadScene* NVP_RESTRICT scene = m_scene;
      bool solidwire = (shadetype == SHADE_SOLIDWIRE);

      // All token data is uint32_t based
      std::vector<uint32_t> pipelines;
      std::vector<uint32_t> vertexbuffers;
      std::vector<uint32_t> indexbuffers;
      std::vector<uint32_t> matrixsets;
      std::vector<uint32_t> materialsets;
      std::vector<VkDrawIndexedIndirectCommand> draws;

      // let's record all token inputs for every drawcall
      for (unsigned int i = 0; i < num; i++){
        const DrawItem& di = drawItems[i];
        const ResourcesVK::Geometry &glgeo = res->m_geometry[di.geometryIndex];

        if (shadetype == SHADE_SOLID && !di.solid){
          continue;
        }

        // we only make use of pipeline changes in solidwire mode
        if (shadetype == SHADE_SOLIDWIRE){
          pipelines.push_back(di.solid ? ResourcesVKGen::TABLE_PIPE_LINES_TRIANGLES : ResourcesVKGen::TABLE_PIPE_LINES);
        }

        {
          indexbuffers.push_back(USE_SINGLE_GEOMETRY_BUFFERS ? 0 : di.geometryIndex );
          indexbuffers.push_back(glgeo.iboOffset);

          vertexbuffers.push_back( USE_SINGLE_GEOMETRY_BUFFERS ? 0 : di.geometryIndex );
          vertexbuffers.push_back(glgeo.vboOffset);
        }

      ///////////////////////////////////////////////////////////////////////////////////////////
      #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
        {
          uint32_t matrixoffset   = di.matrixIndex    * res->m_alignedMatrixSize;
          uint32_t materialoffset = di.materialIndex  * res->m_alignedMaterialSize;

          matrixsets.push_back(0);
          matrixsets.push_back(matrixoffset);

          materialsets.push_back(1);
          materialsets.push_back(materialoffset);
        }
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
        {
          matrixsets.push_back(di.matrixIndex);
          materialsets.push_back(di.materialIndex + res->m_descriptorSetsMatrices.size());
        }
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
        {
          matrixsets.push_back(0);
          matrixsets.push_back(di.matrixIndex);

          materialsets.push_back(1);
          materialsets.push_back(di.materialIndex);
        }
      #endif

        {
          VkDrawIndexedIndirectCommand drawcmd;
          drawcmd.indexCount = di.range.count;
          drawcmd.firstIndex = uint32_t(di.range.offset / sizeof(uint32_t));
          drawcmd.instanceCount = 1;
          drawcmd.firstInstance = 0;
          drawcmd.vertexOffset  = 0;
          draws.push_back(drawcmd);
        }
      }

      sc.sequencesCount = (uint32_t)draws.size();

      
      std::vector<uint32_t> permutation;
      {
        srand(634523);
        permutation.resize(sc.sequencesCount );
        for (uint32_t i = 0; i < sc.sequencesCount ; i++){
          permutation[i] = i;
        }
        // not exactly a good way to generate random 32bit ;)
        for (uint32_t i = sc.sequencesCount-1; i > 0 ; i--){
          uint32_t r = 0;
          r |= (rand() & 0xFF) << 0;
          r |= (rand() & 0xFF) << 8;
          r |= (rand() & 0xFF) << 16;
          r |= (rand() & 0xFF) << 24;

          uint32_t other = r % (i+1);
          std::swap(permutation[i],permutation[other]);
        }
      }

      size_t totalSize = 0;
      size_t pipeOffset = totalSize;
      totalSize += sizeof(uint32_t) * pipelines.size();
      size_t indexbufferOffset = totalSize;
      totalSize += sizeof(uint32_t) * indexbuffers.size();
      size_t vertexbufferOffset = totalSize;
      totalSize += sizeof(uint32_t) * vertexbuffers.size();
      size_t matrixOffset = totalSize;
      totalSize += sizeof(uint32_t) * matrixsets.size();
      size_t materialOffset = totalSize;
      totalSize += sizeof(uint32_t) * materialsets.size();
      size_t drawOffset = totalSize;
      totalSize += sizeof(VkDrawIndexedIndirectCommand) * draws.size();
      size_t indexOffset = totalSize;
      totalSize += sizeof(uint32_t) * permutation.size();

      sc.inputSequenceIndexOffset = indexOffset;

      // A new VkBufferUsageFlagBit was introduced for the input buffers to command generation
      sc.inputBuffer = m_resources->createBuffer(totalSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
      m_resources->allocMemAndBindBuffer(sc.inputBuffer, sc.inputMemory);

      ResourcesVK::StagingBuffer staging;
      staging.init(m_resources->m_device);
      m_resources->fillBuffer(staging, sc.inputBuffer, pipeOffset, sizeof(uint32_t) * pipelines.size(), pipelines.data());
      m_resources->fillBuffer(staging, sc.inputBuffer, indexbufferOffset, sizeof(uint32_t) * indexbuffers.size(), indexbuffers.data());
      m_resources->fillBuffer(staging, sc.inputBuffer, vertexbufferOffset, sizeof(uint32_t) * vertexbuffers.size(), vertexbuffers.data());
      m_resources->fillBuffer(staging, sc.inputBuffer, matrixOffset, sizeof(uint32_t) * matrixsets.size(), matrixsets.data());
      m_resources->fillBuffer(staging, sc.inputBuffer, materialOffset, sizeof(uint32_t) * materialsets.size(), materialsets.data());
      m_resources->fillBuffer(staging, sc.inputBuffer, drawOffset, sizeof(VkDrawIndexedIndirectCommand) * draws.size(), draws.data());
      m_resources->fillBuffer(staging, sc.inputBuffer, indexOffset, sizeof(uint32_t) * permutation.size(), permutation.data());

      m_resources->submissionExecute();
      m_resources->synchronize();
      m_resources->tempdestroyAll();
      staging.deinit();


      VkIndirectCommandsTokenNVX input;
      input.buffer = sc.inputBuffer;

      if (shadetype == SHADE_SOLIDWIRE){
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PIPELINE_NVX;
        input.offset = pipeOffset;
        sc.inputs.push_back(input);
      }
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NVX;
        input.offset = indexbufferOffset;
        sc.inputs.push_back(input);
      }
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NVX;
        input.offset = vertexbufferOffset;
        sc.inputs.push_back(input);
      }
      {
      #if UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
        input.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NVX;
        input.offset = matrixOffset;
        sc.inputs.push_back(input);

        input.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NVX;
        input.offset = materialOffset;
        sc.inputs.push_back(input);
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DESCRIPTOR_SET_NVX;
        input.offset = matrixOffset;
        sc.inputs.push_back(input);

        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DESCRIPTOR_SET_NVX;
        input.offset = materialOffset;
        sc.inputs.push_back(input);
      #endif
      }
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NVX;
        input.offset = drawOffset;
        sc.inputs.push_back(input);
      }
    }

    void DeleteData(ShadeType shadetype)
    {
      ShadeCommand& sc = m_shades[shadetype];
      vkDestroyBuffer( m_resources->m_device, sc.inputBuffer, NULL);
      vkFreeMemory( m_resources->m_device, sc.inputMemory, NULL);
    }

    void InitGenerator(ShadeType shadetype, ResourcesVK* res)
    {
      std::vector<VkIndirectCommandsLayoutTokenNVX> inputInfos;
      VkIndirectCommandsLayoutTokenNVX  input;

      if (shadetype == SHADE_SOLIDWIRE)
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PIPELINE_NVX;
        input.bindingUnit = 0;
        input.dynamicCount = 0;
        input.divisor = 1;
        inputInfos.push_back(input);
      }
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NVX;
        input.bindingUnit = 0;
        input.dynamicCount = 1;
        input.divisor = 1;
        inputInfos.push_back(input);
      }
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NVX;
        input.bindingUnit = 0;
        input.dynamicCount = 1;
        input.divisor = 1;
        inputInfos.push_back(input);
      }
      {
#if UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NVX;
        input.bindingUnit = 0;
        input.dynamicCount = 1;
        input.divisor = 1;
        inputInfos.push_back(input);

        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NVX;
        input.bindingUnit = 1;
        input.dynamicCount = 1;
        input.divisor = 1;
        inputInfos.push_back(input);
#elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DESCRIPTOR_SET_NVX;
        input.bindingUnit = UBO_MATRIX;
        input.dynamicCount = 1;
        input.divisor = 1;
        inputInfos.push_back(input);

        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DESCRIPTOR_SET_NVX;
        input.bindingUnit = UBO_MATERIAL;
        input.dynamicCount = 1;
        input.divisor = 1;
        inputInfos.push_back(input);
#elif UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DESCRIPTOR_SET_NVX;
        input.bindingUnit = UBO_MATRIX;
        input.dynamicCount = 0;
        input.divisor = 1;
        inputInfos.push_back(input);

        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DESCRIPTOR_SET_NVX;
        input.bindingUnit = UBO_MATERIAL;
        input.dynamicCount = 0;
        input.divisor = 1;
        inputInfos.push_back(input);
#endif
      }
      {
        input.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NVX;
        input.bindingUnit = 0;
        input.dynamicCount = 0;
        input.divisor = 1;
        inputInfos.push_back(input);
      }

      VkIndirectCommandsLayoutCreateInfoNVX genInfo = {VkStructureType( VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NVX )};
      genInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      genInfo.tokenCount = (uint32_t)inputInfos.size();
      genInfo.pTokens    = inputInfos.data();
      if (false){ // let's preserve order for now, for more stable result (z-flickering...)
        genInfo.flags |= VK_INDIRECT_COMMANDS_LAYOUT_USAGE_UNORDERED_SEQUENCES_BIT_NVX;
      }
      if (m_mode == MODE_REUSE_IDXSEQ){
        genInfo.flags |= VK_INDIRECT_COMMANDS_LAYOUT_USAGE_INDEXED_SEQUENCES_BIT_NVX;
      }

      VkResult result;
      ShadeCommand& sc = m_shades[shadetype];
      result = vkCreateIndirectCommandsLayoutNVX(res->m_device, &genInfo, NULL, &sc.indirectCmdsLayout);
      assert(result == VK_SUCCESS);
    }

    void DeinitGenerator(ShadeType shadetype)
    {
      ShadeCommand& sc = m_shades[shadetype];
      vkDestroyIndirectCommandsLayoutNVX(m_resources->m_device, sc.indirectCmdsLayout, NULL);
    }

   };


  static RendererVKGen::TypeReset s_type_cmdbuffergen_vk;
  //static RendererVKGen::TypeResetFull s_type_cmdbuffergen1_vk;
  static RendererVKGen::TypeReuse s_type_cmdbuffergen2_vk;
  static RendererVKGen::TypeReuseSeq s_type_cmdbuffergen3_vk;

  void RendererVKGen::init(const CadScene* NVP_RESTRICT scene, Resources* resources)
  {
    ResourcesVKGen* res = (ResourcesVKGen*) resources;
    m_scene = scene;
    m_resources = res;

    fillDrawItems(m_drawItems,resources->m_percent, true, true);

    //printf("drawitems: %d\n", uint32_t(m_drawItems.size()));

    if (resources->m_sorted){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }

    for (int i = 0; i < NUM_SHADES; i++){
      InitGenerator((ShadeType)i, res);

      GenerateIndirectTokenData((ShadeType)i, &m_drawItems[0], m_drawItems.size(), res);
      printf("%d sequence count: %7d\n", i, uint32_t(m_shades[i].sequencesCount));
    }
    printf("\n");

    VkResult result;
    VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmdPoolInfo.queueFamilyIndex = 0;
    result = vkCreateCommandPool(res->m_device, &cmdPoolInfo, NULL, &m_cmdPool);
    assert(result == VK_SUCCESS);

    if (m_mode == MODE_RESET || m_mode == MODE_RESET_RELEASE) {
      for (int i = 0; i < NUM_SHADES; i++){
        // we will cycle through different pools every frame, to avoid synchronization locks
        for (int n = 0; n < ResourcesVK::MAX_BUFFERED_FRAMES; n++){
          m_shades[i].cmdBuffer[n] = res->createCmdBuffer(m_cmdPool, true, false, false);
        }
      }
    }
    else if (m_mode == MODE_REUSE || m_mode == MODE_REUSE_IDXSEQ){
      for (int i = 0; i < NUM_SHADES; i++){
        m_shades[i].cmdBuffer[0] = res->createCmdBuffer(m_cmdPool, false, false, false);
        // reserve space only once and re-use the commandbuffer
        setupTarget((ShadeType)i, m_shades[i].cmdBuffer[0], res, m_shades[i].sequencesCount);
      }
    }

    m_targetCommandBuffer = NULL;
  }

  void RendererVKGen::deinit()
  {
    if (m_mode == MODE_RESET || m_mode == MODE_RESET_RELEASE) {
      for (int i = 0; i < NUM_SHADES; i++){
        vkFreeCommandBuffers(m_resources->m_device, m_cmdPool, ResourcesVK::MAX_BUFFERED_FRAMES, m_shades[i].cmdBuffer);
      }
    }
    else if (m_mode == MODE_REUSE || m_mode == MODE_REUSE_IDXSEQ) {
      for (int i = 0; i < NUM_SHADES; i++){
        vkFreeCommandBuffers(m_resources->m_device, m_cmdPool, 1, m_shades[i].cmdBuffer);
      }
    }

    vkDestroyCommandPool(m_resources->m_device, m_cmdPool, NULL);

    for (int i = 0; i < NUM_SHADES; i++){
      DeleteData((ShadeType)i);
      DeinitGenerator((ShadeType)i);
    }
  }

  void RendererVKGen::setupTarget(ShadeType shadetype, VkCommandBuffer target, ResourcesVKGen* res, uint32_t maxCount)
  {
    res->cmdDynamicState(target);

    if (shadetype == SHADE_SOLID){
      vkCmdBindPipeline(target, VK_PIPELINE_BIND_POINT_GRAPHICS, res->pipes.tris);
    }
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
    vkCmdBindDescriptorSets(target, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
      UBO_SCENE, 1, &res->m_descriptorSet[UBO_SCENE], 0, NULL);
#elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
    vkCmdBindDescriptorSets(target, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
      UBO_SCENE, 1, &res->m_descriptorSet, 0, NULL);
#endif

    // The previously generated commands will be executed here.
    // The current state of the command buffer is inherited just like a usual work provoking command.
    VkCmdReserveSpaceForCommandsInfoNVX reserveInfo = {(VkStructureType)VK_STRUCTURE_TYPE_CMD_RESERVE_SPACE_FOR_COMMANDS_INFO_NVX};
    reserveInfo.indirectCommandsLayout = m_shades[shadetype].indirectCmdsLayout;
    reserveInfo.objectTable = res->m_table.objectTable;
    reserveInfo.maxSequencesCount = maxCount;
    vkCmdReserveSpaceForCommandsNVX(target, &reserveInfo);

    vkEndCommandBuffer(target);
  }

  void RendererVKGen::build(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager)
  {
    const CadScene* NVP_RESTRICT scene = m_scene;
    ResourcesVKGen* res = (ResourcesVKGen*)resources;
    ShadeCommand &sc = m_shades[shadetype];
    VkCommandBuffer target;

    if (m_mode == MODE_RESET || m_mode == MODE_RESET_RELEASE){
      // For some variants of this renderer we pick a fresh command buffer every frame.
      // Release will cause reallocation of command-buffer space, which is more costly.
      // Without release we will be able to re-use command buffer space from the pool from previous frames.
      target = sc.cmdBuffer[ res->m_frame % ResourcesVK::MAX_BUFFERED_FRAMES ];
      vkResetCommandBuffer(target, m_mode == MODE_RESET_RELEASE ? VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT : 0);
      res->cmdBegin(target, true, false, false);
      setupTarget(shadetype, target, res, sc.sequencesCount);
    }
    else if (m_mode == MODE_REUSE || m_mode == MODE_REUSE_IDXSEQ){
      // even faster is directly re-using the previous frame command-buffer, if our reservation state hasn't changed.
      target = sc.cmdBuffer[0];
    }

    m_targetCommandBuffer = target;

    VkCommandBuffer primary = res->createTempCmdBuffer();
    VkCmdProcessCommandsInfoNVX info = { VkStructureType(VK_STRUCTURE_TYPE_CMD_PROCESS_COMMANDS_INFO_NVX) };
    info.indirectCommandsLayout = sc.indirectCmdsLayout;
    info.objectTable = res->m_table.objectTable;
    info.maxSequencesCount = sc.sequencesCount;
    info.indirectCommandsTokenCount = (uint32_t)sc.inputs.size();
    info.pIndirectCommandsTokens = sc.inputs.data();
    info.targetCommandBuffer = target;
    if (m_mode == MODE_REUSE_IDXSEQ){
      info.sequencesIndexBuffer = sc.inputBuffer;
      info.sequencesIndexOffset = sc.inputSequenceIndexOffset;
    }
    
    // If we were regenerating commands into the same targetCommandBuffer in the same sequence
    // then we would have to insert a barrier that ensures rendering of the targetCommandBuffer
    // had completed.
    // Similar applies, if were modifying the input buffers, appropriate barriers would have to
    // be set here.
    //
    // vkCmdPipelineBarrier(primary, whateverModifiedInputs, VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX,  ...);
    //  barrier.dstAccessMask = VK_ACCESS_COMMAND_PROCESS_READ_BIT_NVX;
    //
    // It is not required in this sample, as the blitting synchronizes each frame, and we 
    // do not actually modify the input tokens dynamically.
    //
    vkCmdProcessCommandsNVX(primary, &info );
    vkEndCommandBuffer(primary);

    res->submissionEnqueue(primary);
    res->tempdestroyEnqueue(primary);
  }

  void RendererVKGen::draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager)
  {
    {
      nv_helpers::Profiler::Section _tempTimer(profiler, "Build", resources->getTimerInterface(), false );
      build(shadetype,resources,global,profiler,progManager);
    }

    {
      nv_helpers::Profiler::Section _tempTimer(profiler, "Exec", resources->getTimerInterface(), false );

      const CadScene* NVP_RESTRICT scene = m_scene;
      ResourcesVK* res = (ResourcesVK*)resources;
      ShadeCommand &sc = m_shades[shadetype];

      // generic state setup
      VkCommandBuffer primary = res->createTempCmdBuffer();
      vkCmdUpdateBuffer(primary, res->buffers.scene, 0, sizeof(SceneData), (const uint32_t*)&global.sceneUbo);
      res->cmdPipelineBarrier(primary);

      // clear via pass
      res->cmdBeginRenderPass(primary, true, true);
      if (m_targetCommandBuffer){
        // we need to ensure the processing of commands has completed, before we can execute them
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_COMMAND_PROCESS_WRITE_BIT_NVX;
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(primary, VK_PIPELINE_STAGE_COMMAND_PROCESS_BIT_NVX, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
        vkCmdExecuteCommands(primary, 1, &m_targetCommandBuffer);
      }
      vkCmdEndRenderPass(primary);
      vkEndCommandBuffer(primary);

      res->submissionEnqueue(primary);;
      res->tempdestroyEnqueue(primary);
    }
    m_targetCommandBuffer = NULL;
  }

  void RendererVKGen::blit( ShadeType shadeType, Resources* resources, const Resources::Global& global )
  {
    ResourcesVK* res = (ResourcesVK*)resources;
    VkCommandBuffer cmd = res->createTempCmdBuffer();

    res->cmdImageTransition(cmd, res->images.scene_color, VK_IMAGE_ASPECT_COLOR_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (res->m_msaa){
      VkImageResolve region = {0};
      region.extent.width  = res->m_width;
      region.extent.height = res->m_height;
      region.extent.depth  = 1;
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.dstSubresource.layerCount = 1;
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.srcSubresource.layerCount = 1;

      vkCmdResolveImage(cmd,  res->images.scene_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                              res->images.scene_color_resolved, VK_IMAGE_LAYOUT_GENERAL,
                              1, &region);
    }

    vkEndCommandBuffer(cmd);
    res->submissionEnqueue(cmd);
    res->flushFrame();


    // blit to gl backbuffer
    glDisable(GL_DEPTH_TEST);
    glWaitVkSemaphoreNV((GLuint64)res->m_semImageWritten);
    glDrawVkImageNV((GLuint64)(VkImage)(res->m_msaa ? res->images.scene_color_resolved : res->images.scene_color), 0,
      0,0,res->m_width,res->m_height, 0,
      0,1,1,0);
    glEnable(GL_DEPTH_TEST);
    glSignalVkSemaphoreNV((GLuint64)res->m_semImageRead);

    res->tempdestroyEnqueue(cmd);
  }
}


