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


#ifndef THREADPOOL_H__
#define THREADPOOL_H__

#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <thread>
#include <mutex>
#include <condition_variable>

class ThreadPool {

public:
  typedef void (*WorkerFunc)(void* arg);

  void  init( unsigned int numThreads);
  void  deinit();

  void  activateJob( unsigned int thread, WorkerFunc fn, void* arg );

  static unsigned int sysGetNumCores();

  unsigned int getNumThreads() {
    return m_numThreads;
  }


private:

  struct ThreadEntry {
    ThreadPool*               m_origin;
    std::thread               m_thread;
    unsigned int              m_id;
    WorkerFunc                m_fn;
    void*                     m_fnArg;
    std::mutex                m_commMutex;
    std::condition_variable   m_commCond;
  };
  
  unsigned int                m_numThreads;
  ThreadEntry*                m_pool;

  volatile unsigned int       m_globalInit;

  std::mutex                  m_globalMutex;
  std::condition_variable     m_globalCond;

  static void threadKicker( void* arg );
  void threadProcess(ThreadEntry& entry);

};


#endif


