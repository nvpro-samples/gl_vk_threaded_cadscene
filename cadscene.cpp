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


/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#include "cadscene.hpp"
#include <fileformats/cadscenefile.h>

#include <algorithm>
#include <assert.h>

#define USE_CACHECOMBINE 1


nvmath::vec4f randomVector(float from, float to)
{
  nvmath::vec4f vec;
  float         width = to - from;
  for(int i = 0; i < 4; i++)
  {
    vec.get_value()[i] = from + (float(rand()) / float(RAND_MAX)) * width;
  }
  return vec;
}

// all oct functions derived from "A Survey of Efficient Representations for Independent Unit Vectors"
// http://jcgt.org/published/0003/02/01/paper.pdf
// Returns +/- 1
inline nvmath::vec3f oct_signNotZero(nvmath::vec3f v) {
  // leaves z as is
  return nvmath::vec3f((v.x >= 0.0f) ? +1.0f : -1.0f, (v.y >= 0.0f) ? +1.0f : -1.0f, 1.0f);
}

// Assume normalized input. Output is on [-1, 1] for each component.
inline nvmath::vec3f float32x3_to_oct(nvmath::vec3f v) {
  // Project the sphere onto the octahedron, and then onto the xy plane
  nvmath::vec3f p = nvmath::vec3f(v.x, v.y, 0) * (1.0f / (fabsf(v.x) + fabsf(v.y) + fabsf(v.z)));
  // Reflect the folds of the lower hemisphere over the diagonals
  return (v.z <= 0.0f) ? nvmath::vec3f(1.0f - fabsf(p.y), 1.0f - fabsf(p.x), 0.0f) * oct_signNotZero(p) : p;
}

inline nvmath::vec3f oct_to_float32x3(nvmath::vec3f e) {
  nvmath::vec3f v = nvmath::vec3f(e.x, e.y, 1.0f - fabsf(e.x) - fabsf(e.y));
  if (v.z < 0.0f) {
    v = nvmath::vec3f(1.0f - fabs(v.y), 1.0f - fabs(v.x), v.z) * oct_signNotZero(v);
  }
  return nvmath::normalize(v);
}

inline nvmath::vec3f float32x3_to_octn_precise(nvmath::vec3f v, const int n) {
  nvmath::vec3f s = float32x3_to_oct(v);  // Remap to the square
                                // Each snorm's max value interpreted as an integer,
                                // e.g., 127.0 for snorm8
  float M = float(1 << ((n / 2) - 1)) - 1.0;
  // Remap components to snorm(n/2) precision...with floor instead
  // of round (see equation 1)
  s = nvmath::nv_floor(nvmath::nv_clamp(s, -1.0f, +1.0f) * M) * (1.0f / M);
  nvmath::vec3f bestRepresentation = s;
  float highestCosine = nvmath::dot(oct_to_float32x3(s), v);
  // Test all combinations of floor and ceil and keep the best.
  // Note that at +/- 1, this will exit the square... but that
  // will be a worse encoding and never win.
  for(int i = 0; i <= 1; ++i)
  {
    for(int j = 0; j <= 1; ++j)
    {
      // This branch will be evaluated at compile time
      if((i != 0) || (j != 0))
      {
        // Offset the bit pattern (which is stored in floating
        // point!) to effectively change the rounding mode
        // (when i or j is 0: floor, when it is one: ceiling)
        nvmath::vec3f candidate = nvmath::vec3f(i, j, 0) * (1 / M) + s;
        float         cosine    = nvmath::dot(oct_to_float32x3(candidate), v);
        if(cosine > highestCosine)
        {
          bestRepresentation = candidate;
          highestCosine      = cosine;
        }
      }
    }
  }
  return bestRepresentation;
}

bool CadScene::loadCSF(const char* filename, int clones, int cloneaxis)
{
  CSFile*         csf;
  CSFileMemoryPTR mem = CSFileMemory_new();
  if(CSFile_loadExt(&csf, filename, mem) != CADSCENEFILE_NOERROR || !(csf->fileFlags & CADSCENEFILE_FLAG_UNIQUENODES))
  {
    CSFileMemory_delete(mem);
    return false;
  }

  int copies = clones + 1;

  CSFile_transform(csf);

  srand(234525);


  // materials
  m_materials.resize(csf->numMaterials);
  for(int n = 0; n < csf->numMaterials; n++)
  {
    CSFMaterial* csfmaterial = &csf->materials[n];
    Material&    material    = m_materials[n];

    for(int i = 0; i < 2; i++)
    {
      material.sides[i].ambient  = randomVector(0.0f, 0.1f);
      material.sides[i].diffuse  = nvmath::vec4f(csf->materials[n].color) + randomVector(0.0f, 0.07f);
      material.sides[i].specular = randomVector(0.25f, 0.55f);
      material.sides[i].emissive = randomVector(0.0f, 0.05f);
    }
  }


  // geometry
  int numGeoms = csf->numGeometries;
  m_geometry.resize(csf->numGeometries * copies);
  m_geometryBboxes.resize(csf->numGeometries * copies);
  for(int n = 0; n < csf->numGeometries; n++)
  {
    CSFGeometry* csfgeom = &csf->geometries[n];
    Geometry&    geom    = m_geometry[n];
    geom.cloneIdx        = -1;

    geom.numVertices   = csfgeom->numVertices;
    geom.numIndexSolid = csfgeom->numIndexSolid;
    geom.numIndexWire  = csfgeom->numIndexWire;

    Vertex* vertices = new Vertex[csfgeom->numVertices];
    for(uint32_t i = 0; i < csfgeom->numVertices; i++)
    {
      vertices[i].position[0] = csfgeom->vertex[3 * i + 0];
      vertices[i].position[1] = csfgeom->vertex[3 * i + 1];
      vertices[i].position[2] = csfgeom->vertex[3 * i + 2];
      vertices[i].position[3] = 1.0f;

      nvmath::vec3f normal;
      if(csfgeom->normal)
      {
        normal.x = csfgeom->normal[3 * i + 0];
        normal.y = csfgeom->normal[3 * i + 1];
        normal.z = csfgeom->normal[3 * i + 2];
      }
      else
      {
        normal = normalize(nvmath::vec3f(vertices[i].position));
      }

      nvmath::vec3f packed   = float32x3_to_octn_precise(normal, 16);
      vertices[i].normalOctX = std::min(32767, std::max(-32767, int32_t(packed.x * 32767.0f)));
      vertices[i].normalOctY = std::min(32767, std::max(-32767, int32_t(packed.y * 32767.0f)));

      m_geometryBboxes[n].merge(vertices[i].position);
    }

    geom.vboData = vertices;
    geom.vboSize = sizeof(Vertex) * csfgeom->numVertices;


    unsigned int* indices = new unsigned int[csfgeom->numIndexSolid + csfgeom->numIndexWire];
    memcpy(&indices[0], csfgeom->indexSolid, sizeof(unsigned int) * csfgeom->numIndexSolid);
    if(csfgeom->indexWire)
    {
      memcpy(&indices[csfgeom->numIndexSolid], csfgeom->indexWire, sizeof(unsigned int) * csfgeom->numIndexWire);
    }

    geom.iboData = indices;
    geom.iboSize = sizeof(unsigned int) * (csfgeom->numIndexSolid + csfgeom->numIndexWire);


    geom.parts.resize(csfgeom->numParts);

    size_t offsetSolid = 0;
    size_t offsetWire  = csfgeom->numIndexSolid * sizeof(unsigned int);
    for(uint32_t i = 0; i < csfgeom->numParts; i++)
    {
      geom.parts[i].indexWire.count  = csfgeom->parts[i].numIndexWire;
      geom.parts[i].indexSolid.count = csfgeom->parts[i].numIndexSolid;

      geom.parts[i].indexWire.offset  = offsetWire;
      geom.parts[i].indexSolid.offset = offsetSolid;

      offsetWire += csfgeom->parts[i].numIndexWire * sizeof(unsigned int);
      offsetSolid += csfgeom->parts[i].numIndexSolid * sizeof(unsigned int);
    }
  }
  for(int c = 1; c <= clones; c++)
  {
    for(int n = 0; n < numGeoms; n++)
    {
      m_geometryBboxes[n + numGeoms * c] = m_geometryBboxes[n];

      const Geometry& geomorig = m_geometry[n];
      Geometry&       geom     = m_geometry[n + numGeoms * c];

      geom          = geomorig;
      geom.cloneIdx = n;
    }
  }


  // nodes
  int numObjects = 0;
  m_matrices.resize(csf->numNodes * copies);

  for(int n = 0; n < csf->numNodes; n++)
  {
    CSFNode* csfnode = &csf->nodes[n];

    memcpy(m_matrices[n].objectMatrix.get_value(), csfnode->objectTM, sizeof(float) * 16);
    memcpy(m_matrices[n].worldMatrix.get_value(), csfnode->worldTM, sizeof(float) * 16);

    m_matrices[n].objectMatrixIT = nvmath::transpose(nvmath::invert(m_matrices[n].objectMatrix));
    m_matrices[n].worldMatrixIT  = nvmath::transpose(nvmath::invert(m_matrices[n].worldMatrix));

    if(csfnode->geometryIDX < 0)
      continue;

    numObjects++;
  }


  // objects
  m_objects.resize(numObjects * copies);
  m_objectAssigns.resize(numObjects * copies);
  numObjects = 0;
  for(int n = 0; n < csf->numNodes; n++)
  {
    CSFNode* csfnode = &csf->nodes[n];

    if(csfnode->geometryIDX < 0)
      continue;

    Object& object = m_objects[numObjects];

    object.matrixIndex   = n;
    object.geometryIndex = csfnode->geometryIDX;

    m_objectAssigns[numObjects] = nvmath::vec2i(object.matrixIndex, object.geometryIndex);

    object.parts.resize(csfnode->numParts);
    for(uint32_t i = 0; i < csfnode->numParts; i++)
    {
      object.parts[i].active        = 1;
      object.parts[i].matrixIndex   = csfnode->parts[i].nodeIDX < 0 ? object.matrixIndex : csfnode->parts[i].nodeIDX;
      object.parts[i].materialIndex = csfnode->parts[i].materialIDX;
#if 1
      if(csf->materials[csfnode->parts[i].materialIDX].color[3] < 0.9f)
      {
        object.parts[i].active = 0;
      }
#endif
    }

    BBox bbox = m_geometryBboxes[object.geometryIndex].transformed(m_matrices[n].worldMatrix);
    m_bbox.merge(bbox);

    updateObjectDrawCache(object);

    numObjects++;
  }

  // compute clone move delta based on m_bbox;

  nvmath::vec4f dim = m_bbox.max - m_bbox.min;

  int sq      = 1;
  int numAxis = 0;
  for(int i = 0; i < 3; i++)
  {
    numAxis += (cloneaxis & (1 << i)) ? 1 : 0;
  }

  assert(numAxis);

  switch(numAxis)
  {
    case 1:
      sq = copies;
      break;
    case 2:
      while(sq * sq < copies)
      {
        sq++;
      }
      break;
    case 3:
      while(sq * sq * sq < copies)
      {
        sq++;
      }
      break;
  }


  for(int c = 1; c <= clones; c++)
  {
    int numNodes = csf->numNodes;

    nvmath::vec4f shift = dim * 1.05f;

    float u = 0;
    float v = 0;
    float w = 0;

    switch(numAxis)
    {
      case 1:
        u = float(c);
        break;
      case 2:
        u = float(c % sq);
        v = float(c / sq);
        break;
      case 3:
        u = float(c % sq);
        v = float((c / sq) % sq);
        w = float(c / (sq * sq));
        break;
    }

    float use = u;

    if(cloneaxis & (1 << 0))
    {
      shift.x *= -use;
      if(numAxis > 1)
        use = v;
    }
    else
    {
      shift.x = 0;
    }

    if(cloneaxis & (1 << 1))
    {
      shift.y *= use;
      if(numAxis > 2)
        use = w;
      else if(numAxis > 1)
        use = v;
    }
    else
    {
      shift.y = 0;
    }

    if(cloneaxis & (1 << 2))
    {
      shift.z *= -use;
    }
    else
    {
      shift.z = 0;
    }

    shift.w = 0;

    // move all world matrices
    for(int n = 0; n < numNodes; n++)
    {
      MatrixNode& node     = m_matrices[n + numNodes * c];
      MatrixNode& nodeOrig = m_matrices[n];
      node                 = nodeOrig;
      node.worldMatrix.set_col(3, node.worldMatrix.col(3) + shift);
      node.worldMatrixIT = nvmath::transpose(nvmath::invert(node.worldMatrix));
    }

    {
      // patch object matrix of root
      MatrixNode& node = m_matrices[csf->rootIDX + numNodes * c];
      node.objectMatrix.set_col(3, node.objectMatrix.col(3) + shift);
      node.objectMatrixIT = nvmath::transpose(nvmath::invert(node.objectMatrix));
    }

    // clone objects
    for(int n = 0; n < numObjects; n++)
    {
      const Object& objectorig = m_objects[n];
      Object&       object     = m_objects[n + numObjects * c];

      object = objectorig;
      object.geometryIndex += c * numGeoms;
      object.matrixIndex += c * numNodes;
      for(size_t i = 0; i < object.parts.size(); i++)
      {
        object.parts[i].matrixIndex += c * numNodes;
      }

      for(size_t i = 0; i < object.cacheSolid.state.size(); i++)
      {
        object.cacheSolid.state[i].matrixIndex += c * numNodes;
      }
      for(size_t i = 0; i < object.cacheWire.state.size(); i++)
      {
        object.cacheWire.state[i].matrixIndex += c * numNodes;
      }

      m_objectAssigns[n + numObjects * c] = nvmath::vec2i(object.matrixIndex, object.geometryIndex);
    }
  }

  CSFileMemory_delete(mem);
  return true;
}


struct ListItem
{
  CadScene::DrawStateInfo state;
  CadScene::DrawRange     range;
};

static bool ListItem_compare(const ListItem& a, const ListItem& b)
{
  int diff = 0;
  diff     = diff != 0 ? diff : (a.state.materialIndex - b.state.materialIndex);
  diff     = diff != 0 ? diff : (a.state.matrixIndex - b.state.matrixIndex);
  diff     = diff != 0 ? diff : int(a.range.offset - b.range.offset);

  return diff < 0;
}

static void fillCache(CadScene::DrawRangeCache& cache, const std::vector<ListItem>& list)
{
  cache = CadScene::DrawRangeCache();

  if(!list.size())
    return;

  CadScene::DrawStateInfo state = list[0].state;
  CadScene::DrawRange     range = list[0].range;

  int stateCount = 0;

  for(size_t i = 1; i < list.size() + 1; i++)
  {
    bool newrange = false;
    if(i == list.size() || list[i].state != state)
    {
      // push range
      if(range.count)
      {
        stateCount++;
        cache.offsets.push_back(range.offset);
        cache.counts.push_back(range.count);
      }

      // emit
      if(stateCount)
      {
        cache.state.push_back(state);
        cache.stateCount.push_back(stateCount);
      }

      stateCount = 0;

      if(i == list.size())
      {
        break;
      }
      else
      {
        state        = list[i].state;
        range.offset = list[i].range.offset;
        range.count  = 0;
        newrange     = true;
      }
    }

    const CadScene::DrawRange& currange = list[i].range;
    if(newrange || (USE_CACHECOMBINE && currange.offset == (range.offset + sizeof(unsigned int) * range.count)))
    {
      // merge
      range.count += currange.count;
    }
    else
    {
      // push
      if(range.count)
      {
        stateCount++;
        cache.offsets.push_back(range.offset);
        cache.counts.push_back(range.count);
      }

      range = currange;
    }
  }
}

void CadScene::updateObjectDrawCache(Object& object)
{
  Geometry& geom = m_geometry[object.geometryIndex];

  std::vector<ListItem> listSolid;
  std::vector<ListItem> listWire;

  listSolid.reserve(geom.parts.size());
  listWire.reserve(geom.parts.size());

  for(size_t i = 0; i < geom.parts.size(); i++)
  {
    if(!object.parts[i].active)
      continue;

    ListItem item;
    item.state.materialIndex = object.parts[i].materialIndex;

    item.range             = geom.parts[i].indexSolid;
    item.state.matrixIndex = object.parts[i].matrixIndex;
    listSolid.push_back(item);

    item.range             = geom.parts[i].indexWire;
    item.state.matrixIndex = object.parts[i].matrixIndex;
    listWire.push_back(item);
  }

  std::sort(listSolid.begin(), listSolid.end(), ListItem_compare);
  std::sort(listWire.begin(), listWire.end(), ListItem_compare);

  fillCache(object.cacheSolid, listSolid);
  fillCache(object.cacheWire, listWire);
}

void CadScene::unload()
{
  if(m_geometry.empty())
    return;


  for(size_t i = 0; i < m_geometry.size(); i++)
  {
    if(m_geometry[i].cloneIdx >= 0)
      continue;

    delete[] m_geometry[i].vboData;
    delete[] m_geometry[i].iboData;
  }

  m_matrices.clear();
  m_geometryBboxes.clear();
  m_geometry.clear();
  m_objectAssigns.clear();
  m_objects.clear();
  m_geometryBboxes.clear();
}
