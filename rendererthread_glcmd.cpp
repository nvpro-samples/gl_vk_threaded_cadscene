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
        return !!init_NV_command_list(NVPWindow::sysGetProcAddress);
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

    void init(const CadScene* NVP_RESTRICT scene, Resources* resources);
    void deinit();
    void draw(ShadeType shadetype, Resources*  NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager);

    void blit(ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global );


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
      unsigned char* NVP_RESTRICT bufferData;
    };

    struct ThreadJob {
      RendererThreadedGLCMD*      renderer;
      int                         index;

      GLuint                      m_buffers[NUM_FRAMES];
      PointerStream               m_streams[NUM_FRAMES];
      std::string                 m_tokens[NUM_FRAMES];

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
    const ResourcesGL* NVP_RESTRICT   m_resources;
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

    tthread::condition_variable     m_readyCond;
    tthread::mutex                  m_readyMutex;

    size_t                          m_numEnqueues;
    std::queue<ShadeCommand*>       m_drawQueue;

    tthread::fast_mutex             m_workMutex;
    tthread::fast_mutex             m_drawMutex;

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


    template <class T, ShadeType shade, bool sorted>
    void GenerateTokens(T& stream, ShadeCommand& sc, const DrawItem* NVP_RESTRICT drawItems, size_t numItems, const ResourcesGL* NVP_RESTRICT res )
    {
      const CadScene* NVP_RESTRICT scene = m_scene;
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
        ResourcesGL::encodeAddress(&ubo.cmd.addressLo,res->addresses.scene);
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
          sc.states. push_back( lastSolid ? res->stateobjects.draw_line_tris : res->stateobjects.draw_line );
          sc.fbos.   push_back( res->fbos.scene );

          begin = stream.size();

          lastSolid = di.solid;
        }

        if (lastGeometry != di.geometryIndex){
          const CadScene::Geometry &geo = scene->m_geometry[di.geometryIndex];
          const ResourcesGL::Geometry &geogl = res->m_geometry[di.geometryIndex];

          ResourcesGL::tokenVbo vbo;
          vbo.cmd.index = 0;
          ResourcesGL::encodeAddress(&vbo.cmd.addressLo, geogl.vboADDR);
          vbo.enqueue(stream);

          ResourcesGL::tokenIbo ibo;
          ResourcesGL::encodeAddress(&ibo.cmd.addressLo, geogl.iboADDR);
          ibo.cmd.typeSizeInByte = 4;
          ibo.enqueue(stream);

          lastGeometry = di.geometryIndex;
        }

        if (lastMatrix != di.matrixIndex){

          ResourcesGL::tokenUbo ubo;
          ubo.cmd.index   = UBO_MATRIX;
          ubo.cmd.stage   = UBOSTAGE_VERTEX;
          ResourcesGL::encodeAddress(&ubo.cmd.addressLo, res->addresses.matrices + res->m_alignedMatrixSize * di.matrixIndex);
          ubo.enqueue(stream);

          lastMatrix = di.matrixIndex;
        }

        if (lastMaterial != di.materialIndex){

          ResourcesGL::tokenUbo ubo;
          ubo.cmd.index   = UBO_MATERIAL;
          ubo.cmd.stage   = UBOSTAGE_FRAGMENT;
          ResourcesGL::encodeAddress(&ubo.cmd.addressLo, res->addresses.materials + res->m_alignedMaterialSize * di.materialIndex);
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
        sc.states. push_back( res->stateobjects.draw_tris );
      }
      else{
        sc.states. push_back( lastSolid ? res->stateobjects.draw_line_tris : res->stateobjects.draw_line );
      }
      sc.fbos. push_back( res->fbos.scene );

    }

    template <class T>
    void GenerateTokens(T& stream, ShadeCommand& sc, ShadeType shade, const DrawItem* NVP_RESTRICT drawItems, size_t numItems, const ResourcesGL* NVP_RESTRICT res ){
      if (res->m_sorted){
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

  void RendererThreadedGLCMD::init(const CadScene* NVP_RESTRICT scene, Resources* resources)
  {
    m_scene = scene;
    const ResourcesGL* NVP_RESTRICT res = (const ResourcesGL*)resources;

    fillDrawItems(m_drawItems,resources->m_percent, true, true);

    if (resources->m_sorted){
      std::sort(m_drawItems.begin(),m_drawItems.end(),DrawItem_compare_groups);
    }


    size_t worstCaseSize;

    {
      std::string dummy;
      ShadeCommand sc;
      GenerateTokens<std::string>(dummy,sc,SHADE_SOLIDWIRE,&m_drawItems[0],m_drawItems.size(),res);
      worstCaseSize = (dummy.size()*4)/3;

      printf("buffer size: %d\n",worstCaseSize);
    }

    res->rebuildStateObjects();
    m_state = res->m_state;

    m_resources  = (const ResourcesGL*) resources;
    m_numThreads = resources->m_threads;

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
        glGenBuffers(NUM_FRAMES,m_jobs[i].m_buffers);
        for (int f = 0; f < NUM_FRAMES; f++){
          glNamedBufferStorageEXT(job.m_buffers[f],worstCaseSize,0, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_DYNAMIC_STORAGE_BIT);
          job.m_streams[f].init( glMapNamedBufferRangeEXT(job.m_buffers[f],0,worstCaseSize, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT), worstCaseSize);
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

    NVP_BARRIER();
    for (int i = 0; i < m_numThreads; i++){
      tthread::lock_guard<tthread::mutex> lock(m_jobs[i].m_hasWorkMutex);
      m_jobs[i].m_hasWork = m_frame;
      m_jobs[i].m_hasWorkCond.notify_one();
    }

    tthread::this_thread::yield();

    {
      tthread::lock_guard<tthread::mutex> lock(m_readyMutex);
      while (m_ready < m_numThreads){
        m_readyCond.wait(m_readyMutex);
      }
    }

    NVP_BARRIER();

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
    m_drawMutex.lock();
    m_drawQueue.push(sc);
    m_drawMutex.unlock();
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

      GenerateTokens<PointerStream>(job.m_streams[subframe],*sc, shadetype, &m_drawItems[begin], num, m_resources);
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

  void RendererThreadedGLCMD::RunThread( NVPWindow& window, int tid )
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

  void RendererThreadedGLCMD::draw(ShadeType shadetype, Resources* NVP_RESTRICT resources, const Resources::Global& global, nv_helpers::Profiler& profiler, nv_helpers_gl::ProgramManager &progManager)
  {
    const CadScene* NVP_RESTRICT scene = m_scene;
    const ResourcesGL* NVP_RESTRICT res = (ResourcesGL*)resources;

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

    glBindFramebuffer(GL_FRAMEBUFFER, res->fbos.scene);
    glClearColor(0.2f,0.2f,0.2f,0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glNamedBufferSubDataEXT(res->buffers.scene,0,sizeof(SceneData),&global.sceneUbo);

    m_workingSet   = res->m_workingSet;
    m_shade        = shadetype;
    m_numCurItems  = 0;
    m_numEnqueues  = 0;

    // generate & tokens/cmdbuffers in parallel

    NVP_BARRIER();

    // start to dispatch threads
    for (int i = 0; i < m_numThreads; i++){
      {
        tthread::lock_guard<tthread::mutex> lock(m_jobs[i].m_hasWorkMutex);
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

        m_drawMutex.lock();
        if (!m_drawQueue.empty()){

          sc = m_drawQueue.front();
          m_drawQueue.pop();

          hadEntry = true;
        }
        m_drawMutex.unlock();

        if (hadEntry){
          if (sc){
            NVP_BARRIER();
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
        tthread::this_thread::yield();
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

  void RendererThreadedGLCMD::blit( ShadeType shadeType, Resources* NVP_RESTRICT resources, const Resources::Global& global )
  {
    ResourcesGL* res = (ResourcesGL*)resources;

    int width   = global.width;
    int height  = global.height;

    // blit to background
    glBindFramebuffer(GL_READ_FRAMEBUFFER, res->fbos.scene);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,width,height,
      0,0,width,height,GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

}
