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



#include "renderer.hpp"
#include <algorithm>
#include <assert.h>
#include <nvpwindow.hpp>

#include <nvmath/nvmath_glsltypes.h>

#include "common.h"

#pragma pack(1)


namespace csfthreaded {

uint32_t Resources::s_vkDevice = 0;
uint32_t Resources::s_glDevice = 0;

//////////////////////////////////////////////////////////////////////////

const char* toString(enum ShadeType st)
{
  switch(st)
  {
    case SHADE_SOLID:
      return "solid";
    case SHADE_SOLIDWIRE:
      return "solid w edges";
  }

  return NULL;
}

static void AddItem(std::vector<Renderer::DrawItem>& drawItems, const Renderer::Config& config, const Renderer::DrawItem& di)
{
  if(di.range.count)
  {
    drawItems.push_back(di);
  }
}

static void FillCache(std::vector<Renderer::DrawItem>& drawItems,
                      const Renderer::Config&          config,
                      const CadScene::Object&          obj,
                      const CadScene::Geometry&        geo,
                      bool                             solid,
                      int                              objectIndex)
{
  int                             begin = 0;
  const CadScene::DrawRangeCache& cache = solid ? obj.cacheSolid : obj.cacheWire;

  for(size_t s = 0; s < cache.state.size(); s++)
  {
    const CadScene::DrawStateInfo& state = cache.state[s];
    for(int d = 0; d < cache.stateCount[s]; d++)
    {
      // evict
      Renderer::DrawItem di;
      di.geometryIndex = obj.geometryIndex;
      di.matrixIndex   = state.matrixIndex;
      di.materialIndex = state.materialIndex;
      di.objectIndex   = objectIndex;

      di.solid        = solid;
      di.range.offset = cache.offsets[begin + d];
      di.range.count  = cache.counts[begin + d];

      AddItem(drawItems, config, di);
    }
    begin += cache.stateCount[s];
  }
}

static void FillJoin(std::vector<Renderer::DrawItem>& drawItems,
                     const Renderer::Config&          config,
                     const CadScene::Object&          obj,
                     const CadScene::Geometry&        geo,
                     bool                             solid,
                     int                              objectIndex)
{
  CadScene::DrawRange range;

  int lastMaterial = -1;
  int lastMatrix   = -1;

  for(size_t p = 0; p < obj.parts.size(); p++)
  {
    const CadScene::ObjectPart&   part = obj.parts[p];
    const CadScene::GeometryPart& mesh = geo.parts[p];

    if(!part.active)
      continue;

    if(part.materialIndex != lastMaterial || part.matrixIndex != lastMatrix)
    {

      if(range.count)
      {
        // evict
        Renderer::DrawItem di;
        di.geometryIndex = obj.geometryIndex;
        di.matrixIndex   = lastMatrix;
        di.materialIndex = lastMaterial;
        di.objectIndex   = objectIndex;

        di.solid = solid;
        di.range = range;

        AddItem(drawItems, config, di);
      }

      range = CadScene::DrawRange();

      lastMaterial = part.materialIndex;
      lastMatrix   = part.matrixIndex;
    }

    if(!range.count)
    {
      range.offset = solid ? mesh.indexSolid.offset : mesh.indexWire.offset;
    }

    range.count += solid ? mesh.indexSolid.count : mesh.indexWire.count;
  }

  // evict
  Renderer::DrawItem di;
  di.geometryIndex = obj.geometryIndex;
  di.matrixIndex   = lastMatrix;
  di.materialIndex = lastMaterial;
  di.objectIndex   = objectIndex;

  di.solid = solid;
  di.range = range;

  AddItem(drawItems, config, di);
}

static void FillIndividual(std::vector<Renderer::DrawItem>& drawItems,
                           const Renderer::Config&          config,
                           const CadScene::Object&          obj,
                           const CadScene::Geometry&        geo,
                           bool                             solid,
                           int                              objectIndex)
{
  for(size_t p = 0; p < obj.parts.size(); p++)
  {
    const CadScene::ObjectPart&   part = obj.parts[p];
    const CadScene::GeometryPart& mesh = geo.parts[p];

    if(!part.active)
      continue;

    Renderer::DrawItem di;
    di.geometryIndex = obj.geometryIndex;
    di.matrixIndex   = part.matrixIndex;
    di.materialIndex = part.materialIndex;
    di.objectIndex   = objectIndex;

    di.solid = solid;
    di.range = mesh.indexSolid;

    AddItem(drawItems, config, di);
  }
}

void Renderer::fillDrawItems(std::vector<DrawItem>& drawItems, const Config& config, bool solid, bool wire)
{
  const CadScene* NV_RESTRICT scene = m_scene;
  m_config                          = config;

  size_t maxObjects = scene->m_objects.size();
  size_t from       = std::min(maxObjects - 1, size_t(config.objectFrom));
  maxObjects        = std::min(maxObjects, from + size_t(config.objectNum));

  for(size_t i = from; i < maxObjects; i++)
  {
    const CadScene::Object&   obj = scene->m_objects[i];
    const CadScene::Geometry& geo = scene->m_geometry[obj.geometryIndex];

    if(config.strategy == STRATEGY_GROUPS)
    {
      if(solid)
        FillCache(drawItems, config, obj, geo, true, int(i));
      if(wire)
        FillCache(drawItems, config, obj, geo, false, int(i));
    }
    else if(config.strategy == STRATEGY_JOIN)
    {
      if(solid)
        FillJoin(drawItems, config, obj, geo, true, int(i));
      if(wire)
        FillJoin(drawItems, config, obj, geo, false, int(i));
    }
    else if(config.strategy == STRATEGY_INDIVIDUAL)
    {
      if(solid)
        FillIndividual(drawItems, config, obj, geo, true, int(i));
      if(wire)
        FillIndividual(drawItems, config, obj, geo, false, int(i));
    }
  }

  uint32_t sumTriangles = 0;
  for(size_t i = 0; i < drawItems.size(); i++)
  {
    sumTriangles += drawItems[i].range.count / 3;
  }
  LOGI("draw calls:      %9d\n", uint32_t(drawItems.size()));
  LOGI("triangles total: %9d\n", sumTriangles);
}

ThreadPool Renderer::s_threadpool;
}  // namespace csfthreaded
