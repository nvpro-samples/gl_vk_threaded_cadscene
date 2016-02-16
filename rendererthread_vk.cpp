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


#include <assert.h>
#include <algorithm>
#include <queue>
#include "renderer.hpp"
#include <main.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fast_mutex.h>
#include "resources_vk.hpp"

#include <nv_math/nv_math_glsltypes.h>

using namespace nv_math;
#include "common.h"


namespace csfthreaded
{

  //////////////////////////////////////////////////////////////////////////

  
  class RendererThreadedVK: public Renderer {
  public:
    enum Mode {
      MODE_CMD_MAINSUBMIT,
      MODE_CMD_WORKERSUBMIT,
    };


    class TypeCmd : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVK::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan MT cmd main process";
      }
      Renderer* create() const
      {
        RendererThreadedVK* renderer = new RendererThreadedVK();
        renderer->m_mode = MODE_CMD_MAINSUBMIT;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 10;
      }

      Resources* resources()
      {
        return ResourcesVK::get();
      }
    };

    class TypeCmdSlave : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return ResourcesVK::isAvailable();
      }
      const char* name() const
      {
        return "Vulkan MT cmd worker process";
      }
      Renderer* create() const
      {
        RendererThreadedVK* renderer = new RendererThreadedVK();
        renderer->m_mode = MODE_CMD_WORKERSUBMIT;
        return renderer;
      }
      unsigned int priority() const 
      {
        return 10;
      }

      Resources* resources()
      {
        return ResourcesVK::get();
      }
    };

  public:

    void init(const CadScene* NVP_RESTRICT scene, Resources* resources);
    void deinit();
    void draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager);

    void blit(ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global );


    Mode            m_mode;

    RendererThreadedVK()
      : m_mode(MODE_CMD_MAINSUBMIT) 
    {

    }

  private:

    static const int NUM_FRAMES = 4;

    struct ShadeCommand {
      std::vector<VkCommandBuffer>     cmdbuffers;
    };

    struct ThreadJob {
      RendererThreadedVK*         renderer;
      int                         index;

      VkCommandPool                   m_pool[ResourcesVK::MAX_BUFFERED_FRAMES];

      int                         m_frame;
      tthread::condition_variable m_hasWorkCond;
      tthread::mutex              m_hasWorkMutex;
      volatile int                m_hasWork;

      size_t                      m_scIdx;
      std::vector<ShadeCommand*>  m_scs;


      void resetFrame(){
        m_scIdx = 0;
      }

      ShadeCommand* getFrameCommand() {
        if (m_scIdx+1 > m_scs.size()){
          ShadeCommand* sc = new ShadeCommand;
          m_scIdx++;
          m_scs.push_back(sc);
          return sc;
        }
        else{
          return m_scs[m_scIdx++];
        }
      }
    };


    std::vector<DrawItem>           m_drawItems;
    ResourcesVK* NVP_RESTRICT       m_resources;
    int                             m_numThreads;

    int                             m_workingSet;
    ShadeType                       m_shade;
    int                             m_frame;

    ThreadJob*                      m_jobs;

    volatile int                    m_ready;
    volatile int                    m_stopThreads;
    volatile size_t                 m_numCurItems;

    tthread::condition_variable     m_readyCond;
    tthread::mutex                  m_readyMutex;

    size_t                          m_numEnqueues;
    std::queue<ShadeCommand*>       m_drawQueue;

    tthread::fast_mutex             m_workMutex;
    tthread::fast_mutex             m_drawMutex;
    tthread::condition_variable     m_drawMutexCondition;

#if USE_THREADED_SECONDARIES
    VkCommandBuffer                     m_primary;
#endif

    static void threadMaster( NVPWindow& window, void* arg  )
    {
      ThreadJob* job = (ThreadJob*) arg;
      job->renderer->RunThread( window, job->index );
    }

    bool getWork_ts( size_t &start, size_t &num )
    {
      bool hasWork = false;
      m_workMutex.lock();

      const size_t chunkSize = m_workingSet;
      size_t total = m_drawItems.size();

      if (m_numCurItems < total){
        size_t batch = std::min(total-m_numCurItems,chunkSize);
        start = m_numCurItems;
        num   = batch;
        m_numCurItems += batch;
        hasWork = true;
      }
      else{
        hasWork = false;
        start = 0;
        num = 0;
      }

      m_workMutex.unlock();

      return hasWork;
    }

    void          RunThread( NVPWindow& window, int index );
    unsigned int  RunThreadFrame(ShadeType shadetype, ThreadJob& job);

    void enqueueShadeCommand_ts( ShadeCommand *sc );
    void submitShadeCommand_ts( ShadeCommand *sc );

    template <ShadeType shadetype, bool sorted>
    void GenerateCmdBuffers(ShadeCommand& sc, VkCommandPool pool, const DrawItem* NVP_RESTRICT drawItems, size_t num, const ResourcesVK* NVP_RESTRICT res )
    {
      const CadScene* NVP_RESTRICT scene = m_scene;
      bool solidwire = (shadetype == SHADE_SOLIDWIRE);

      int lastMaterial = -1;
      int lastGeometry = -1;
      int lastMatrix   = -1;
      bool lastSolid   = true;

      sc.cmdbuffers.clear();

#if USE_THREADED_SECONDARIES
      VkCommandBuffer  cmd = res->createCmdBuffer(pool,true,false,true);
#else
      VkCommandBuffer  cmd = res->createCmdBuffer(pool,true,true,false);
      res->cmdPipelineBarrier(cmd);
      res->cmdBeginRenderPass(cmd, false);
#endif
      res->cmdDynamicState(cmd);

      VkPipeline solidPipeline = solidwire ? res->pipes.line_tris : res->pipes.tris;
      VkPipeline nonSolidPipeline = res->pipes.line;

      if (num) {
        bool solid = shadetype == SHADE_SOLID ? true : drawItems[0].solid;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
          solid ? solidPipeline : nonSolidPipeline);
#if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
          UBO_SCENE, 1, &res->m_descriptorSet[UBO_SCENE], 0, NULL);
#endif
#if UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW || UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
          UBO_SCENE, 1, &res->m_descriptorSet, 0, NULL);
#endif
        lastSolid = solid;
      }

      bool first = true;
      const DrawItem* drawItemsEnd = drawItems + num;
      while (drawItems != drawItemsEnd) {
        const DrawItem& di = *drawItems++;

        if (shadetype == SHADE_SOLID && !di.solid){
          if (sorted) break;
          continue;
        }

        if (shadetype == SHADE_SOLIDWIRE && di.solid != lastSolid) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            di.solid ? solidPipeline : nonSolidPipeline);

          lastSolid = di.solid;
        }

        if (lastGeometry != di.geometryIndex){
          const ResourcesVK::Geometry &glgeo = res->m_geometry[di.geometryIndex];

          vkCmdBindVertexBuffers(cmd, 0, 1, &glgeo.vbo, &glgeo.vboOffset);
          vkCmdBindIndexBuffer  (cmd, glgeo.ibo, glgeo.iboOffset, VK_INDEX_TYPE_UINT32);

          lastGeometry = di.geometryIndex;
        }

      ///////////////////////////////////////////////////////////////////////////////////////////
      #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
        if (lastMatrix != di.matrixIndex)
        {
        #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
          uint32_t offset = di.matrixIndex    * res->m_alignedMatrixSize;
          assert(offset % res->m_physical.properties.limits.minUniformBufferOffsetAlignment == 0);
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATRIX, 1, &res->m_descriptorSet[UBO_MATRIX], 1, &offset);
        #else
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATRIX, 1, &res->m_descriptorSetsMatrices[di.matrixIndex], 0, NULL);
        #endif
          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex)
        {
        #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC
          uint32_t offset = di.materialIndex    * res->m_alignedMaterialSize;
          assert(offset % res->m_physical.properties.limits.minUniformBufferOffsetAlignment == 0);
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATERIAL, 1, &res->m_descriptorSet[UBO_MATERIAL], 1, &offset);
        #else
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            UBO_MATERIAL, 1, &res->m_descriptorSetsMaterials[di.materialIndex], 0, NULL);
        #endif
          lastMaterial = di.materialIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
      
        if (lastMaterial != di.materialIndex ||
            lastMatrix   != di.matrixIndex)
        {
        #if UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC
          uint32_t offsets[UBOS_NUM];
          offsets[UBO_SCENE]    = 0;
          offsets[UBO_MATRIX]   = di.matrixIndex    * res->m_alignedMatrixSize;
          offsets[UBO_MATERIAL] = di.materialIndex  * res->m_alignedMaterialSize;

        #elif UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
          uint32_t offsets[UBOS_NUM-1];
          offsets[UBO_MATRIX-1]   = di.matrixIndex    * res->m_alignedMatrixSize;
          offsets[UBO_MATERIAL-1] = di.materialIndex  * res->m_alignedMaterialSize;
        #endif
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            0, 1, &res->m_descriptorSet, sizeof(offsets)/sizeof(offsets[0]),offsets);

          lastMaterial = di.materialIndex;
          lastMatrix   = di.matrixIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
        if (lastMatrix != di.matrixIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(ObjectData), &scene->m_matrices[di.matrixIndex]);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(ObjectData),sizeof(MaterialData), &scene->m_materials[di.materialIndex]);

          lastMaterial = di.materialIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
        if (lastMatrix != di.matrixIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(int), &di.matrixIndex);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex)
        {
          vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(int),sizeof(int), &di.materialIndex);

          lastMaterial = di.materialIndex;
        }
      ///////////////////////////////////////////////////////////////////////////////////////////
      #endif
      
        // drawcall
        vkCmdDrawIndexed(cmd, di.range.count, 1, (uint32_t)(di.range.offset / sizeof(uint32_t)), 0, 0);
      }

      if (cmd){
#if !USE_THREADED_SECONDARIES
        vkCmdEndRenderPass(cmd);
#endif
        vkEndCommandBuffer(cmd);
        sc.cmdbuffers.push_back(cmd);
      }
    }

    void GenerateCmdBuffers(ShadeCommand& sc, ShadeType shadeType, VkCommandPool pool, const DrawItem* NVP_RESTRICT drawItems, size_t num, const ResourcesVK* NVP_RESTRICT res)
    {
      if (res->m_sorted)
      {
        switch (shadeType) {
        case SHADE_SOLID:
          GenerateCmdBuffers<SHADE_SOLID, true>(sc, pool, drawItems, num, res);
          break;
        case SHADE_SOLIDWIRE:
          GenerateCmdBuffers<SHADE_SOLIDWIRE, true>(sc, pool, drawItems, num, res);
          break;
        }
      }
      else {
        switch (shadeType) {
        case SHADE_SOLID:
          GenerateCmdBuffers<SHADE_SOLID, false>(sc, pool, drawItems, num, res);
          break;
        case SHADE_SOLIDWIRE:
          GenerateCmdBuffers<SHADE_SOLIDWIRE, false>(sc, pool, drawItems, num, res);
          break;
        }
      }
    }

   };


  static RendererThreadedVK::TypeCmd s_type_cmdmain_xgl;
  static RendererThreadedVK::TypeCmdSlave s_type_cmdslave_xgl;

  void RendererThreadedVK::init(const CadScene* NVP_RESTRICT scene, Resources* resources)
  {
    const ResourcesVK* res = (const ResourcesVK*) resources;
    m_scene = scene;

    fillDrawItems(m_drawItems,resources->m_percent, true, true);

    printf("drawitems: %d\n", uint32_t(m_drawItems.size()));

    if (resources->m_sorted){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }

    m_resources  = (ResourcesVK*) resources;
    m_numThreads = resources->m_threads;

    // make jobs
    m_ready = 0;
    m_jobs = new ThreadJob[m_numThreads];
    m_stopThreads = 0;

    for (int i = 0; i < m_numThreads; i++)
    {
      ThreadJob& job = m_jobs[i];
      job.index = i;
      job.renderer = this;
      job.m_hasWork = -1;
      job.m_frame = 0;

      VkResult result;
      VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      cmdPoolInfo.queueFamilyIndex = 0;
      for (int n=0; n < ResourcesVK::MAX_BUFFERED_FRAMES; n++){
        result = vkCreateCommandPool(res->m_device, &cmdPoolInfo, NULL, &job.m_pool[n]);
        assert(result == VK_SUCCESS);
      }

      s_threadpool.activateJob( i, threadMaster, &m_jobs[i]);
    }

    m_frame = 0;
  }

  void RendererThreadedVK::deinit()
  {
    m_stopThreads = 1;
    m_ready = 0;

    NVP_BARRIER();
    for (int i = 0; i < m_numThreads; i++){
      tthread::lock_guard<tthread::mutex> lock(m_jobs[i].m_hasWorkMutex);
      m_jobs[i].m_hasWork = m_frame;
      m_jobs[i].m_hasWorkCond.notify_one();
    }
    m_drawMutexCondition.notify_all();

    tthread::this_thread::yield();

    {
      tthread::lock_guard<tthread::mutex> lock(m_readyMutex);
      while (m_ready < m_numThreads){
        m_readyCond.wait(m_readyMutex);
      }
    }

    NVP_BARRIER();

    for (int i = 0; i < m_numThreads; i++)
    {
      for (size_t s = 0; s < m_jobs[i].m_scs.size(); s++ ){
        delete m_jobs[i].m_scs[s];
      }
      for (int n=0; n < ResourcesVK::MAX_BUFFERED_FRAMES; n++){
        vkDestroyCommandPool(m_resources->m_device, m_jobs[i].m_pool[n], NULL);
      }
    }

    delete [] m_jobs;

    m_drawItems.clear();
  }

  void RendererThreadedVK::enqueueShadeCommand_ts( ShadeCommand *sc )
  {
    m_drawMutex.lock();

    m_drawQueue.push(sc);
    m_drawMutexCondition.notify_one();

    m_drawMutex.unlock();
  }


  void RendererThreadedVK::submitShadeCommand_ts( ShadeCommand *sc )
  {
    m_drawMutex.lock();
#if USE_THREADED_SECONDARIES
    vkCmdExecuteCommands(m_primary, sc->cmdbuffers.size(), sc->cmdbuffers.data());
#else
    m_resources->submissionEnqueue(sc->cmdbuffers.size(), sc->cmdbuffers.data());
#endif
    NVP_BARRIER();
    m_drawMutex.unlock();

    sc->cmdbuffers.clear();
  }


  unsigned int RendererThreadedVK::RunThreadFrame(ShadeType shadetype, ThreadJob& job) 
  {
    unsigned int dispatches = 0;

    bool   first = true;
    size_t tnum  = 0;
    size_t begin = 0;
    size_t num   = 0;

    size_t offset  = 0;

    int subframe = m_frame % ResourcesVK::MAX_BUFFERED_FRAMES;

    job.resetFrame();
    vkResetCommandPool( m_resources->m_device, job.m_pool[ subframe ], 0 );
    while (getWork_ts(begin,num))
    {
      ShadeCommand* sc = job.getFrameCommand();
      GenerateCmdBuffers(*sc, shadetype, job.m_pool[ subframe ], &m_drawItems[begin], num, m_resources);
      if (!sc->cmdbuffers.empty()){
        if (m_mode == MODE_CMD_MAINSUBMIT){
          enqueueShadeCommand_ts(sc);
        }
        else if (m_mode == MODE_CMD_WORKERSUBMIT){
          submitShadeCommand_ts(sc);
        }
        dispatches += 1;
      }
      tnum += num;
    }

    // NULL signals we are done
    enqueueShadeCommand_ts(NULL);

    return dispatches;
  }

  void RendererThreadedVK::RunThread( NVPWindow& window, int tid )
  {
    ThreadJob & job  = m_jobs[tid];
    ShadeType shadetype;

    double timeWork = 0;
    double timeFrame = 0;
    int timerFrames = 0;
    size_t dispatches = 0;

    double timePrint = NVPWindow::sysGetTime();

    while(!m_stopThreads)
    {
      //NVP_BARRIER();

      double beginFrame = NVPWindow::sysGetTime();
      timeFrame -= NVPWindow::sysGetTime();

      {
        tthread::lock_guard<tthread::mutex> lock(job.m_hasWorkMutex);
        while(job.m_hasWork != job.m_frame){
          job.m_hasWorkCond.wait(job.m_hasWorkMutex);
        }

        shadetype = m_shade;
      }

      if (m_stopThreads){
        break;
      }

      double beginWork = NVPWindow::sysGetTime();
      timeWork -= NVPWindow::sysGetTime();

      dispatches += RunThreadFrame(shadetype,job);

      job.m_frame++;

      timeWork += NVPWindow::sysGetTime();

      double currentTime = NVPWindow::sysGetTime();
      timeFrame += currentTime;

      timerFrames++;

      if (timerFrames && (currentTime - timePrint) > 2.0){
        timeFrame /= double(timerFrames);
        timeWork  /= double(timerFrames);

        timeFrame *= 1000000.0;
        timeWork  *= 1000000.0;

        timePrint = currentTime;

        GLuint avgdispatch = GLuint(double(dispatches)/double(timerFrames));

#if PRINT_TIMER_STATS
        printf("thread %d: work %6d [us] dispatches %5d\n", tid, GLuint(timeWork), GLuint(avgdispatch));
#endif
        timeFrame = 0;
        timeWork = 0;

        timerFrames = 0;
        dispatches = 0;
      }
    }

    {
      tthread::lock_guard<tthread::mutex> lock(m_readyMutex);
      m_ready++;
      m_readyCond.notify_all();
    }
  }

  void RendererThreadedVK::draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager)
  {
    const CadScene* NVP_RESTRICT scene = m_scene;
    ResourcesVK* res = (ResourcesVK*)resources;

    // generic state setup
    {
      VkCommandBuffer cmd = res->createTempCmdBuffer();
      vkCmdUpdateBuffer(cmd, res->buffers.scene, 0, sizeof(SceneData), (const uint32_t*)&global.sceneUbo);

      res->cmdPipelineBarrier(cmd);
      res->cmdBeginRenderPass(cmd, true, USE_THREADED_SECONDARIES ? true : false);
      
#if USE_THREADED_SECONDARIES
      m_primary = cmd;
#else
      vkCmdEndRenderPass(cmd);
      vkEndCommandBuffer(cmd);

      res->submissionEnqueue(cmd);
      res->tempdestroyEnqueue( cmd );
#endif
    }

    m_workingSet   = res->m_workingSet;
    m_shade        = shadetype;
    m_numCurItems  = 0;
    m_numEnqueues  = 0;

    // generate & cmdbuffers in parallel

    NVP_BARRIER();

    // start to dispatch threads
    for (int i = 0; i < m_numThreads; i++){
      {
        tthread::lock_guard<tthread::mutex> lock(m_jobs[i].m_hasWorkMutex);
        m_jobs[i].m_hasWork = m_frame;
      }
      m_jobs[i].m_hasWorkCond.notify_one();
    }

    // dispatch drawing here
    {
      int numTerminated = 0;
      while (true){
        bool hadEntry = false;
        ShadeCommand* sc = NULL;

        m_drawMutex.lock();
        if (m_drawQueue.empty()) {
          m_drawMutexCondition.wait(m_drawMutex);
        }
        if (!m_drawQueue.empty()){

          sc = m_drawQueue.front();
          m_drawQueue.pop();

          hadEntry = true;
        }
        m_drawMutex.unlock();

        if (hadEntry){
          if (sc){
            m_numEnqueues++;
#if USE_THREADED_SECONDARIES
            vkCmdExecuteCommands(m_primary, sc->cmdbuffers.size(), &sc->cmdbuffers[0]);
#else
            res->submissionEnqueue(sc->cmdbuffers.size(), sc->cmdbuffers.data());
#endif
            sc->cmdbuffers.clear();
          }
          else{
            numTerminated++;
          }
        }

        if (numTerminated == m_numThreads){
          break;
        }
        tthread::this_thread::yield();
      }
    }

    NVP_BARRIER();

#if USE_THREADED_SECONDARIES
    {
      vkCmdEndRenderPass(m_primary);
      vkEndCommandBuffer(m_primary);

      res->submissionEnqueue(m_primary);
      res->tempdestroyEnqueue( m_primary );
    }
#endif

    m_frame++;
  }

  void RendererThreadedVK::blit( ShadeType shadeType, Resources* resources, const Resources::Global& global )
  {
    ResourcesVK* res = (ResourcesVK*)resources;

    if (res->m_msaa){
      VkCommandBuffer cmd = res->createTempCmdBuffer();
      VkImageResolve region = {0};
      region.extent.width  = res->m_width;
      region.extent.height = res->m_height;
      region.extent.depth  = 1;
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.dstSubresource.layerCount = 1;
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.srcSubresource.layerCount = 1;

      vkCmdResolveImage(cmd,  res->images.scene_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                              res->images.scene_color_resolved, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      vkEndCommandBuffer(cmd);

      res->submissionEnqueue(cmd);
      res->tempdestroyEnqueue(cmd);
    }

    res->flushFrame();

    // blit to gl backbuffer
    glDisable(GL_DEPTH_TEST);
    glWaitVkSemaphoreNV((GLuint64)res->m_semImageWritten);
    glDrawVkImageNV((GLuint64)(VkImage)(res->m_msaa ? res->images.scene_color_resolved : res->images.scene_color), 0,
      0,0,res->m_width,res->m_height, 0,
      0,1,1,0);
    glEnable(GL_DEPTH_TEST);
    glSignalVkSemaphoreNV((GLuint64)res->m_semImageRead);
  }


}


