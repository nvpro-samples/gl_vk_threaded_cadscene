/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */



#ifndef THREADPOOL_H__
#define THREADPOOL_H__

#include <vector>
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


