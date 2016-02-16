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

#ifndef THREADPOOL_H__
#define THREADPOOL_H__

#include <vector>
#include <main.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <tinythread.h>

class ThreadPool {

public:
  typedef void (*WorkerFunc)( NVPWindow& window, void* arg);

  // pass as share if threads should not have gl contexts
#define THREADPOOL_NO_CONTEXT  ((NVPWindow*)1)

  void  init( unsigned int numThreads, NVPWindow* share);
  void  deinit();

  void  activateJob( unsigned int thread, WorkerFunc fn, void* arg );
  void  deactivate();

  bool  hasSharedContext(){
    return m_hasSharedContext;
  }

  static unsigned int sysGetNumCores();


private:

  struct ThreadEntry {
    ThreadPool*       m_origin;
    NVPWindow         m_window;
    NVPWindow*        m_share;
    tthread::thread*  m_thread;
    unsigned int      m_id;
    WorkerFunc        m_fn;
    void*             m_fnArg;
    tthread::mutex              m_commMutex;
    tthread::condition_variable m_commCond;
  };
  
  unsigned int                m_numThreads;
  ThreadEntry*                m_pool;

  volatile unsigned int       m_globalInit;

  tthread::mutex              m_globalMutex;
  tthread::condition_variable m_globalCond;

  bool                        m_hasSharedContext;

  static void threadKicker( void* arg );
  void threadProcess(ThreadEntry& entry);

};


#endif


