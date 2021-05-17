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


#include <platform.h>

#include "threadpool.hpp"
#include "nvh/nvprint.hpp"
#include <assert.h>

#define THREADPOOL_TERMINATE_FUNC  ((ThreadPool::WorkerFunc)1)

#if _WIN32

#include <windows.h>

typedef BOOL (WINAPI *LPFN_GLPI)(
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, 
  PDWORD);


// Helper function to count set bits in the processor mask.
static DWORD CountSetBits(ULONG_PTR bitMask)
{
  DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
  DWORD bitSetCount = 0;
  ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
  DWORD i;

  for (i = 0; i <= LSHIFT; ++i)
  {
    bitSetCount += ((bitMask & bitTest)?1:0);
    bitTest/=2;
  }

  return bitSetCount;
}

unsigned int ThreadPool::sysGetNumCores()
{
  LPFN_GLPI glpi;
  BOOL done = FALSE;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
  DWORD returnLength = 0;
  DWORD logicalProcessorCount = 0;
  DWORD numaNodeCount = 0;
  DWORD processorCoreCount = 0;
  DWORD processorL1CacheCount = 0;
  DWORD processorL2CacheCount = 0;
  DWORD processorL3CacheCount = 0;
  DWORD processorPackageCount = 0;
  DWORD byteOffset = 0;
  PCACHE_DESCRIPTOR Cache;

  glpi = (LPFN_GLPI) GetProcAddress(
    GetModuleHandleA("kernel32"),
    "GetLogicalProcessorInformation");
  if (NULL == glpi) 
  {
    return std::thread::hardware_concurrency();
  }

  while (!done)
  {
    DWORD rc = glpi(buffer, &returnLength);

    if (FALSE == rc) 
    {
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) 
      {
        if (buffer) 
          free(buffer);

        buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
          returnLength);

        if (NULL == buffer) 
        {
          return std::thread::hardware_concurrency();
        }
      } 
      else 
      {
        return std::thread::hardware_concurrency();
      }
    } 
    else
    {
      done = TRUE;
    }
  }

  ptr = buffer;

  while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength) 
  {
    switch (ptr->Relationship) 
    {
    case RelationNumaNode:
      // Non-NUMA systems report a single record of this type.
      numaNodeCount++;
      break;

    case RelationProcessorCore:
      processorCoreCount++;

      // A hyperthreaded core supplies more than one logical processor.
      logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
      break;

    case RelationCache:
      // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
      Cache = &ptr->Cache;
      if (Cache->Level == 1)
      {
        processorL1CacheCount++;
      }
      else if (Cache->Level == 2)
      {
        processorL2CacheCount++;
      }
      else if (Cache->Level == 3)
      {
        processorL3CacheCount++;
      }
      break;

    case RelationProcessorPackage:
      // Logical processors share a physical package.
      processorPackageCount++;
      break;

    default:
      break;
    }
    byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    ptr++;
  }

#if 0
  LOGI(TEXT("\nGetLogicalProcessorInformation results:\n"));
  LOGI(TEXT("Number of NUMA nodes: %d\n"),
    numaNodeCount);
  LOGI(TEXT("Number of physical processor packages: %d\n"),
    processorPackageCount);
  LOGI(TEXT("Number of processor cores: %d\n"),
    processorCoreCount);
  LOGI(TEXT("Number of logical processors: %d\n"),
    logicalProcessorCount);
  LOGI(TEXT("Number of processor L1/L2/L3 caches: %d/%d/%d\n"),
    processorL1CacheCount,
    processorL2CacheCount,
    processorL3CacheCount);
#endif

  free(buffer);

  return processorCoreCount;
}

#else

unsigned int ThreadPool::sysGetNumCores()
{
  return std::thread::hardware_concurrency();
}

#endif


void ThreadPool::threadKicker( void* arg )
{
  ThreadEntry* thread = (ThreadEntry*) arg;
  thread->m_origin->threadProcess(*thread);
}

void ThreadPool::threadProcess( ThreadEntry& entry )
{
  {
    std::unique_lock<std::mutex> lock(m_globalMutex);

    LOGI("%d created...\n", entry.m_id);

    m_globalInit++;
    m_globalCond.notify_all();
  }

#if _WIN32
  // assume hyperthreading, move to n physical cores
  unsigned int cpuCore = entry.m_id*2 + 1;
  SetThreadAffinityMask(GetCurrentThread(), uint64_t(1) << cpuCore);
#endif

  while (true)
  {
    {
      std::unique_lock<std::mutex> lock(entry.m_commMutex);
      while(!entry.m_fn){
        entry.m_commCond.wait(lock);
      }
    }

    if (entry.m_fn == THREADPOOL_TERMINATE_FUNC) break;

    NV_BARRIER();

    LOGI("%d started job\n", entry.m_id);

    entry.m_fn(entry.m_fnArg);
    entry.m_fn = 0;
    
    LOGI("%d finished job\n", entry.m_id);
  }

  LOGI("%d exiting...\n", entry.m_id);

  {
    std::unique_lock<std::mutex> lock(m_globalMutex);
    LOGI("%d shutdown\n", entry.m_id);
  }
  
}

void ThreadPool::init( unsigned int numThreads)
{
  m_numThreads  = numThreads;
  m_globalInit = 0;

  m_pool = new ThreadEntry[numThreads];

  for (unsigned int i = 0; i < numThreads; i++){
    ThreadEntry& entry = m_pool[i];
    entry.m_id = numThreads - i - 1;
    entry.m_origin = this;
    entry.m_fn = 0;
    entry.m_fnArg = 0;
  }

  NV_BARRIER();

  for (unsigned int i = 0; i < numThreads; i++){
    ThreadEntry& entry = m_pool[i];
    entry.m_thread = std::thread( threadKicker, &m_pool[i]);
  }

  {
    std::unique_lock<std::mutex> lock(m_globalMutex);
    while (m_globalInit < numThreads){
      m_globalCond.wait(lock);
    }
  }

#if _WIN32
  // pin the main thread to core 0
  SetThreadAffinityMask(GetCurrentThread(), 1);
#endif
}

void ThreadPool::deinit()
{
  NV_BARRIER();

  for (unsigned int i = 0; i < m_numThreads; i++){
    ThreadEntry& entry = m_pool[i];

    {
      std::unique_lock<std::mutex> lock(entry.m_commMutex);
      entry.m_fn = THREADPOOL_TERMINATE_FUNC;
      entry.m_fnArg = 0;
      entry.m_commCond.notify_all();
    }

    std::this_thread::yield();

    entry.m_thread.join();
  }

  delete [] m_pool;
  m_pool = 0;
  m_numThreads = 0;
}

void ThreadPool::activateJob( unsigned int tid, WorkerFunc fn, void* arg )
{
  assert( tid < m_numThreads);

  ThreadEntry& entry = m_pool[tid];

  assert( entry.m_fn == 0 );

  {
    std::unique_lock<std::mutex> lock(entry.m_commMutex);
    entry.m_fn = fn;
    entry.m_fnArg = arg;
    entry.m_commCond.notify_all();
  }

}

