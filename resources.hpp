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


#pragma once

#include "cadscene.hpp"
#include <nvgl/glsltypes_gl.hpp>
#include <nvh/nvprint.hpp>
#include <nvh/profiler.hpp>
#include <platform.h>
#if HAS_OPENGL
#include <nvgl/contextwindow_gl.hpp>
#else
#include <nvvk/context_vk.hpp>
#include <nvvk/swapchain_vk.hpp>
#endif


#include <algorithm>

struct ImDrawData;

#include "common.h"

namespace csfthreaded {

enum ShadeType
{
  SHADE_SOLID,
  SHADE_SOLIDWIRE,
  NUM_SHADES,
};

inline size_t alignedSize(size_t sz, size_t align)
{
  return ((sz + align - 1) / align) * align;
}

struct PointerStream
{
  unsigned char* NV_RESTRICT dataptr;
  size_t                     used;
  size_t                     allocated;

  void init(void* data, size_t datasize)
  {
    dataptr   = (unsigned char* NV_RESTRICT)data;
    allocated = datasize;
    used      = 0;
  }

  void clear() { used = 0; }

  size_t size() { return used; }
};

class Resources
{
public:
  static uint32_t s_vkDevice;
  static uint32_t s_glDevice;


  struct Global
  {
    SceneData     sceneUbo;
    AnimationData animUbo;
    int           winWidth;
    int           winHeight;
    int           workingSet;
    bool          batchedSubmit;
    ImDrawData*   imguiDrawData;
  };

  uint32_t m_numMatrices;

  uint32_t m_frame;

  uint32_t m_alignedMatrixSize;
  uint32_t m_alignedMaterialSize;

  Resources()
      : m_frame(0)
  {
  }

  virtual void synchronize() {}

  // Can't virtualize it anymore :-(
#if HAS_OPENGL
  virtual bool init(nvgl::ContextWindow* window, nvh::Profiler* profiler) { return false; }
#else
  virtual bool init(nvvk::Context* deviceInstance, nvvk::SwapChain* swapChain, nvh::Profiler* profiler)
  {
    return false;
  }
#endif
  virtual void deinit() {}

  virtual bool initPrograms(const std::string& path, const std::string& prepend) { return true; }
  virtual void reloadPrograms(const std::string& prepend) {}

  virtual bool initFramebuffer(int width, int height, int msaa, bool vsync) { return true; }

  virtual bool initScene(const CadScene&) { return true; }
  virtual void deinitScene() {}

  virtual void animation(const Global& global) {}
  virtual void animationReset() {}

  virtual void beginFrame() {}
  virtual void blitFrame(const Global& global) {}
  virtual void endFrame() {}

  virtual nvmath::mat4f perspectiveProjection(float fovy, float aspect, float nearPlane, float farPlane) const
  {
    return nvmath::perspective(fovy, aspect, nearPlane, farPlane);
  }

  inline void initAlignedSizes(unsigned int alignment)
  {
    m_alignedMatrixSize   = (uint32_t)(alignedSize(sizeof(CadScene::MatrixNode), alignment));
    m_alignedMaterialSize = (uint32_t)(alignedSize(sizeof(CadScene::Material), alignment));
  }
};
}  // namespace csfthreaded
