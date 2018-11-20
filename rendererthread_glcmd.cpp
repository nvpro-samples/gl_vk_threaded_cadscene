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

/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#if HAS_OPENGL

#include <assert.h>
#include <algorithm>
#include <queue>
#include <main.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <nv_helpers/spin_mutex.hpp>

#include "renderer.hpp"
#include "resources_gl.hpp"

#include <nv_math/nv_math_glsltypes.h>

using namespace nv_math;
#include "common.h"

namespace csfthreaded
{

  //////////////////////////////////////////////////////////////////////////

  

  class RendererThreadedGLCMD: public Renderer {
  public:

    enum Mode {
      MODE_BUFFER_PERS,
    };

    class Type : public Renderer::Type 
    {
      bool isAvailable() const
      {
        return !!load_GL_NV_command_list(NVPWindow::sysGetProcAddressGL);
      }
      const char* name() const
      {
        return "GL MT nvcmd pers main process";
      }
      Renderer* create() const
      {
        RendererThreadedGLCMD* renderer = new RendererThreadedGLCMD();
        renderer->m_mode = MODE_BUFFER_PERS;
        return renderer;
      }

      Resources* resources()
      {
        return ResourcesGL::get();
      }
      
      unsigned int priority() const 
      {
        return 5;
      }
    };

  public:

    void init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config);
    void deinit();
    void draw(ShadeType shadetype, Resources*  NV_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler);

    void blit(ShadeType shadeType, Resources* NV_RESTRICT resources, const Resources::Global& global );


    Mode    m_mode;

    RendererThreadedGLCMD()
      : m_mode(MODE_BUFFER_PERS) 
    {

    }

  private:

    static const int NUM_FRAMES = 4;

    struct ShadeCommand {
      std::vector<GLintptr>   offsets;
      std::vector<GLsizei>    sizes;
      std::vector<GLuint>     states;
      std::vector<GLuint>     fbos;

      int                     subframe;
      GLuint                  buffer;
      size_t                  bufferOffset;
      size_t                  bufferSize;
      unsigned char* NV_RESTRICT bufferData;
    };

    struct ThreadJob {
      RendererThreadedGLCMD*      renderer;
      int                         index;

      GLuint                      m_buffers[NUM_FRAMES];
      PointerStream               m_streams[NUM_FRAMES];
      std::string                 m_tokens[NUM_FRAMES];

      int                           m_frame;
      std::condition_variable       m_hasWorkCond;
      std::mutex                    m_hasWorkMutex;
      volatile int                  m_hasWork;

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
    const ResourcesGL* NV_RESTRICT m_resources;
    int                             m_numThreads;
    ResourcesGL::StateIncarnation   m_state;

    int                             m_workingSet;
    ShadeType                       m_shade;
    int                             m_frame;
    GLsync                          m_syncs[NUM_FRAMES];

    ThreadJob*                      m_jobs;
    
    volatile int                    m_hadPrint;
    volatile int                    m_ready;
    volatile int                    m_stopThreads;
    volatile size_t                 m_numCurItems;

    std::condition_variable         m_readyCond;
    std::mutex                      m_readyMutex;

    size_t                          m_numEnqueues;
    std::queue<ShadeCommand*>       m_drawQueue;

    nv_helpers::spin_mutex           m_workMutex;
    nv_helpers::spin_mutex           m_drawMutex;

    static void threadMaster( void* arg  )
    {
      ThreadJob* job = (ThreadJob*) arg;
      job->renderer->RunThread( job->index );
    }

    bool getWork_ts( size_t &start, size_t &num )
    {
      std::lock_guard<nv_helpers::spin_mutex>  lock(m_workMutex);
      bool hasWork = false;

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

      return hasWork;
    }

    void          RunThread( int index );
    unsigned int  RunThreadFrame(ShadeType shadetype, ThreadJob& job);

    void enqueueShadeCommand_ts( ShadeCommand *sc );


    template <class T, ShadeType shade, bool sorted>
    void GenerateTokens(T& stream, ShadeCommand& sc, const DrawItem* NV_RESTRICT drawItems, size_t numItems, const ResourcesGL* NV_RESTRICT res )
    {
      const CadScene* NV_RESTRICT scene = m_scene;
      int lastMaterial = -1;
      int lastGeometry = -1;
      int lastMatrix   = -1;
      bool lastSolid   = true;

      sc.fbos.clear();
      sc.offsets.clear();
      sc.sizes.clear();
      sc.states.clear();

      size_t begin = stream.size();
      {
        ResourcesGL::tokenUbo ubo;
        ubo.cmd.index   = UBO_SCENE;
        ubo.cmd.stage   = UBOSTAGE_VERTEX;
        ResourcesGL::encodeAddress(&ubo.cmd.addressLo,res->m_buffers.scene.bufferADDR);
        ubo.enqueue(stream);

        ubo.cmd.stage   = UBOSTAGE_FRAGMENT;
        ubo.enqueue(stream);

        ResourcesGL::tokenPolyOffset offset;
        offset.cmd.bias = 1;
        offset.cmd.scale = 1;
        offset.enqueue(stream);
      }

      for (int i = 0; i < numItems; i++){
        const DrawItem& di = drawItems[i];

        if (shade == SHADE_SOLID && !di.solid){
          if (sorted) break;
          continue;
        }

        if (shade == SHADE_SOLIDWIRE && di.solid != lastSolid){
          sc.offsets.push_back( begin );
          sc.sizes.  push_back( GLsizei((stream.size()-begin)) );
          sc.states. push_back( lastSolid ? res->m_stateobjects.draw_line_tris : res->m_stateobjects.draw_line );
          sc.fbos.   push_back( res->m_fbos.scene );

          begin = stream.size();

          lastSolid = di.solid;
        }

        if (lastGeometry != di.geometryIndex){
          const CadScene::Geometry &geo = scene->m_geometry[di.geometryIndex];
          const ResourcesGL::Geometry &geogl = res->m_geometry[di.geometryIndex];

          ResourcesGL::tokenVbo vbo;
          vbo.cmd.index = 0;
          ResourcesGL::encodeAddress(&vbo.cmd.addressLo, geogl.vbo.bufferADDR);
          vbo.enqueue(stream);

          ResourcesGL::tokenIbo ibo;
          ResourcesGL::encodeAddress(&ibo.cmd.addressLo, geogl.ibo.bufferADDR);
          ibo.cmd.typeSizeInByte = 4;
          ibo.enqueue(stream);

          lastGeometry = di.geometryIndex;
        }

        if (lastMatrix != di.matrixIndex){

          ResourcesGL::tokenUbo ubo;
          ubo.cmd.index   = UBO_MATRIX;
          ubo.cmd.stage   = UBOSTAGE_VERTEX;
          ResourcesGL::encodeAddress(&ubo.cmd.addressLo, res->m_buffers.matrices.bufferADDR + res->m_alignedMatrixSize * di.matrixIndex);
          ubo.enqueue(stream);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex){

          ResourcesGL::tokenUbo ubo;
          ubo.cmd.index   = UBO_MATERIAL;
          ubo.cmd.stage   = UBOSTAGE_FRAGMENT;
          ResourcesGL::encodeAddress(&ubo.cmd.addressLo, res->m_buffers.materials.bufferADDR + res->m_alignedMaterialSize * di.materialIndex);
          ubo.enqueue(stream);

          lastMaterial = di.materialIndex;
        }

        ResourcesGL::tokenDrawElems drawelems;
        drawelems.cmd.baseVertex = 0;
        drawelems.cmd.count = di.range.count;
        drawelems.cmd.firstIndex = GLuint((di.range.offset )/sizeof(GLuint));
        drawelems.enqueue(stream);
      }

      sc.offsets.push_back( begin );
      sc.sizes.  push_back( GLsizei((stream.size()-begin)) );
      if (shade == SHADE_SOLID){
        sc.states. push_back( res->m_stateobjects.draw_tris );
      }
      else{
        sc.states. push_back( lastSolid ? res->m_stateobjects.draw_line_tris : res->m_stateobjects.draw_line );
      }
      sc.fbos. push_back( res->m_fbos.scene );

    }

    template <class T>
    void GenerateTokens(T& stream, ShadeCommand& sc, ShadeType shade, const DrawItem* NV_RESTRICT drawItems, size_t numItems, const ResourcesGL* NV_RESTRICT res, bool sorted ){
      if (sorted){
        switch(shade){
        case SHADE_SOLID:
          GenerateTokens<T,SHADE_SOLID,true>(stream,sc,drawItems,numItems,res);
          break;
        case SHADE_SOLIDWIRE:
          GenerateTokens<T,SHADE_SOLIDWIRE,true>(stream,sc,drawItems,numItems,res);
          break;
        }
      }
      else{
        switch(shade){
        case SHADE_SOLID:
          GenerateTokens<T,SHADE_SOLID,false>(stream,sc,drawItems,numItems,res);
          break;
        case SHADE_SOLIDWIRE:
          GenerateTokens<T,SHADE_SOLIDWIRE,false>(stream,sc,drawItems,numItems,res);
          break;
        }
      }
    }
  };

  static RendererThreadedGLCMD::Type s_uborange;

  void RendererThreadedGLCMD::init(const CadScene* NV_RESTRICT scene, Resources* resources, const Renderer::Config& config)
  {
    m_scene = scene;
    const ResourcesGL* NV_RESTRICT res = (const ResourcesGL*)resources;

    fillDrawItems(m_drawItems, config, true, true);

    if (config.sorted){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }


    size_t worstCaseSize;

    {
      std::string dummy;
      ShadeCommand sc;
      GenerateTokens<std::string>(dummy,sc,SHADE_SOLIDWIRE,&m_drawItems[0],m_drawItems.size(),res,config.sorted);
      worstCaseSize = (dummy.size()*4)/3;

      LOGI("buffer size: %d\n", uint32_t(worstCaseSize));
    }

    res->rebuildStateObjects();
    m_state = res->m_state;

    m_resources  = (const ResourcesGL*) resources;
    m_numThreads = config.threads;

    // make jobs
    m_ready = 0;
    m_jobs = new ThreadJob[m_numThreads];
    m_stopThreads = 0;

    for (int f = 0; f < NUM_FRAMES; f++)
    {
      m_syncs[f] = 0;
    }

    for (int i = 0; i < m_numThreads; i++)
    {
      ThreadJob& job = m_jobs[i];
      job.index = i;
      job.renderer = this;
      job.m_hasWork = -1;
      job.m_frame = 0;

      if (m_mode == MODE_BUFFER_PERS){
        glCreateBuffers(NUM_FRAMES,m_jobs[i].m_buffers);
        for (int f = 0; f < NUM_FRAMES; f++){
          glNamedBufferStorage(job.m_buffers[f],worstCaseSize,0, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_DYNAMIC_STORAGE_BIT);
          job.m_streams[f].init( glMapNamedBufferRange(job.m_buffers[f],0,worstCaseSize, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT), worstCaseSize);
        }
      }

      s_threadpool.activateJob( i, threadMaster, &m_jobs[i]);
    }

    m_frame = 0;
  }

  void RendererThreadedGLCMD::deinit()
  {
    m_stopThreads = 1;
    m_ready = 0;

    NV_BARRIER();
    for (int i = 0; i < m_numThreads; i++){
      std::unique_lock<std::mutex> lock(m_jobs[i].m_hasWorkMutex);
      m_jobs[i].m_hasWork = m_frame;
      m_jobs[i].m_hasWorkCond.notify_one();
    }

    std::this_thread::yield();

    {
     std::unique_lock<std::mutex> lock(m_readyMutex);
      while (m_ready < m_numThreads){
        m_readyCond.wait(lock);
      }
    }

    NV_BARRIER();

    for (int f = 0; f < NUM_FRAMES; f++)
    {
      if (m_syncs[f]){
        glDeleteSync(m_syncs[f]);
      }
    }

    for (int i = 0; i < m_numThreads; i++)
    {
      if (m_mode == MODE_BUFFER_PERS){
        for (int f = 0; f < NUM_FRAMES; f++ ){
          glUnmapNamedBuffer(m_jobs[i].m_buffers[f]);
        }
        glDeleteBuffers(NUM_FRAMES,m_jobs[i].m_buffers);
      }
      
      for (size_t s = 0; s < m_jobs[i].m_scs.size(); s++ ){
        delete m_jobs[i].m_scs[s];
      }
    }

    delete [] m_jobs;

    m_drawItems.clear();
  }

  void RendererThreadedGLCMD::enqueueShadeCommand_ts( ShadeCommand *sc )
  {
    std::lock_guard<nv_helpers::spin_mutex>  lock(m_drawMutex);
    m_drawQueue.push(sc);
  }

  unsigned int RendererThreadedGLCMD::RunThreadFrame(ShadeType shadetype, ThreadJob& job) 
  {
    unsigned int dispatches = 0;

    bool   first = true;
    size_t tnum  = 0;
    size_t begin = 0;
    size_t num   = 0;

    size_t offset  = 0;
    job.resetFrame();

    int subframe = job.m_frame % NUM_FRAMES;
    job.m_streams[subframe].clear();

    while (getWork_ts(begin,num))
    {
      ShadeCommand* sc = job.getFrameCommand();
      sc->bufferData = job.m_streams[subframe].dataptr;

      if (m_mode == MODE_BUFFER_PERS){
        sc->bufferOffset = job.m_streams[subframe].size();
      }

      GenerateTokens<PointerStream>(job.m_streams[subframe],*sc, shadetype, &m_drawItems[begin], num, m_resources, m_config.sorted);
      sc->bufferSize = job.m_streams[subframe].size() - sc->bufferOffset;

      if (m_mode == MODE_BUFFER_PERS){
        sc->buffer = job.m_buffers[subframe];
      }

      enqueueShadeCommand_ts(sc);
      dispatches += 1;
      
      tnum += num;
    }

    // NULL signals we are done
    enqueueShadeCommand_ts(NULL);

    return dispatches;
  }

  void RendererThreadedGLCMD::RunThread(int tid )
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
      //NV_BARRIER();

      double beginFrame = NVPWindow::sysGetTime();
      timeFrame -= NVPWindow::sysGetTime();

      {
        std::unique_lock<std::mutex> lock(job.m_hasWorkMutex);
        while(job.m_hasWork != job.m_frame){
          job.m_hasWorkCond.wait(lock);
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
        LOGI("thread %d: work %6d [us] dispatches %5d\n", tid, GLuint(timeWork), GLuint(avgdispatch));
#endif
        timeFrame = 0;
        timeWork = 0;

        timerFrames = 0;
        dispatches = 0;
      }
    }

    {
      std::unique_lock<std::mutex> lock(m_readyMutex);
      m_ready++;
      m_readyCond.notify_all();
    }
  }

  void RendererThreadedGLCMD::draw(ShadeType shadetype, Resources* NV_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler)
  {
    const CadScene* NV_RESTRICT scene = m_scene;
    const ResourcesGL* NV_RESTRICT res = (ResourcesGL*)resources;

    // generic state setup
    glViewport(0, 0, global.width, global.height);

    // workaround
    glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glEnableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);

    if (m_state.programs != res->m_state.programs ||
        m_state.fbos != res->m_state.fbos)
    {
      res->rebuildStateObjects();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, res->m_fbos.scene);
    glClearColor(0.2f,0.2f,0.2f,0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glNamedBufferSubData(res->m_buffers.scene,0,sizeof(SceneData),&global.sceneUbo);

    m_workingSet   = global.workingSet;
    m_shade        = shadetype;
    m_numCurItems  = 0;
    m_numEnqueues  = 0;

    // generate & tokens/cmdbuffers in parallel

    NV_BARRIER();

    // start to dispatch threads
    for (int i = 0; i < m_numThreads; i++){
      {
        std::unique_lock<std::mutex> lock(m_jobs[i].m_hasWorkMutex);
        m_jobs[i].m_hasWork = m_frame;
      }
      m_jobs[i].m_hasWorkCond.notify_one();
    }

    
    int subframe = m_frame % NUM_FRAMES;

    if (m_mode == MODE_BUFFER_PERS){
      if (m_syncs[subframe]){
        GLenum ret = glClientWaitSync(m_syncs[subframe],GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
        glDeleteSync(m_syncs[subframe]);
        m_syncs[subframe] = 0;
      }
    }
    // dispatch drawing here
    {
      int numTerminated = 0;
      while (true){
        bool hadEntry = false;
        ShadeCommand* sc = NULL;

        {
          std::lock_guard<nv_helpers::spin_mutex>  lock(m_drawMutex);
          if (!m_drawQueue.empty()) {

            sc = m_drawQueue.front();
            m_drawQueue.pop();

            hadEntry = true;
          }
        }

        if (hadEntry){
          if (sc){
            NV_BARRIER();
            m_numEnqueues++;
            glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
            glDrawCommandsStatesNV(sc->buffer, &sc->offsets[0], &sc->sizes[0], &sc->states[0], &sc->fbos[0], (uint32_t)sc->sizes.size());
          }
          else{
            numTerminated++;
          }
        }

        if (numTerminated == m_numThreads){
          break;
        }
        std::this_thread::yield();
      }
    }

    if (m_mode == MODE_BUFFER_PERS){
      m_syncs[subframe] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    }
    

    m_frame++;

    glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glDisableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_state = res->m_state;
  }

  void RendererThreadedGLCMD::blit( ShadeType shadeType, Resources* NV_RESTRICT resources, const Resources::Global& global )
  {
    ResourcesGL* res = (ResourcesGL*)resources;

    int width   = global.width;
    int height  = global.height;

    // blit to background
    glBindFramebuffer(GL_READ_FRAMEBUFFER, res->m_fbos.scene);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,width,height,
      0,0,width,height,GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

}

#endif
