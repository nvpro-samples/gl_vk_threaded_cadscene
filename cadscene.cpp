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

#include "cadscene.hpp"
#include "cadscenefile.h"

#include <algorithm>
#include <assert.h>

#define USE_CACHECOMBINE  1


nv_math::vec4f randomVector(float from, float to)
{
  nv_math::vec4f vec;
  float width = to - from;
  for (int i = 0; i < 4; i++){
    vec.get_value()[i] = from + (float(rand())/float(RAND_MAX))*width;
  }
  return vec;
}


bool CadScene::loadCSF( const char* filename, int clones, int cloneaxis)
{
  CSFile* csf;
  CSFileMemoryPTR mem = CSFileMemory_new();
  if (CSFile_loadExt(&csf,filename,mem) != CADSCENEFILE_NOERROR || !(csf->fileFlags & CADSCENEFILE_FLAG_UNIQUENODES)){
    CSFileMemory_delete(mem);
    return false;
  }

  int copies = clones + 1;

  CSFile_transform(csf);

  srand(234525);



  // materials
  m_materials.resize( csf->numMaterials );
  for (int n = 0; n < csf->numMaterials; n++ )
  {
    CSFMaterial* csfmaterial = &csf->materials[n];
    Material& material = m_materials[n];

    for (int i = 0; i < 2; i++){
      material.sides[i].ambient = randomVector(0.0f,0.1f);
      material.sides[i].diffuse = nv_math::vec4f(csf->materials[n].color) + randomVector(0.0f,0.07f);
      material.sides[i].specular = randomVector(0.25f,0.55f);
      material.sides[i].emissive = randomVector(0.0f,0.05f);
    }

  }



  // geometry
  int numGeoms = csf->numGeometries;
  m_geometry.resize( csf->numGeometries * copies );
  m_geometryBboxes.resize( csf->numGeometries * copies );
  for (int n = 0; n < csf->numGeometries; n++ )
  {
    CSFGeometry* csfgeom = &csf->geometries[n];
    Geometry& geom = m_geometry[n];
    geom.cloneIdx = -1;

    geom.numVertices = csfgeom->numVertices;
    geom.numIndexSolid = csfgeom->numIndexSolid;
    geom.numIndexWire = csfgeom->numIndexWire;

    Vertex* vertices = new Vertex[csfgeom->numVertices];
    for (int i = 0; i < csfgeom->numVertices; i++){
      vertices[i].position[0] = csfgeom->vertex[3*i + 0];
      vertices[i].position[1] = csfgeom->vertex[3*i + 1];
      vertices[i].position[2] = csfgeom->vertex[3*i + 2];
      vertices[i].position[3] = 1.0f;
      if (csfgeom->normal){
        vertices[i].normal[0] = csfgeom->normal[3*i + 0];
        vertices[i].normal[1] = csfgeom->normal[3*i + 1];
        vertices[i].normal[2] = csfgeom->normal[3*i + 2];
        vertices[i].normal[3] = 0.0f;
      }
      else{
        vertices[i].normal = normalize(nv_math::vec3f(vertices[i].position));
      }
      
      
      m_geometryBboxes[n].merge( vertices[i].position );
    }

    geom.vertices = vertices;
    geom.vboSize = sizeof(Vertex) * csfgeom->numVertices;



    unsigned int* indices = new unsigned int [csfgeom->numIndexSolid + csfgeom->numIndexWire];
    memcpy(&indices[0],csfgeom->indexSolid, sizeof(unsigned int) * csfgeom->numIndexSolid);
    if (csfgeom->indexWire){
      memcpy(&indices[csfgeom->numIndexSolid],csfgeom->indexWire, sizeof(unsigned int) * csfgeom->numIndexWire);
    }

    geom.indices = indices;
    geom.iboSize = sizeof(unsigned int) * (csfgeom->numIndexSolid + csfgeom->numIndexWire);

   
    geom.parts.resize( csfgeom->numParts );
    
    size_t offsetSolid = 0;
    size_t offsetWire = csfgeom->numIndexSolid * sizeof(unsigned int);
    for (int i = 0; i < csfgeom->numParts; i++){
      geom.parts[i].indexWire.count   = csfgeom->parts[i].indexWire;
      geom.parts[i].indexSolid.count  = csfgeom->parts[i].indexSolid;

      geom.parts[i].indexWire.offset  = offsetWire;
      geom.parts[i].indexSolid.offset = offsetSolid;

      offsetWire  += csfgeom->parts[i].indexWire  * sizeof(unsigned int);
      offsetSolid += csfgeom->parts[i].indexSolid * sizeof(unsigned int);
    }
  }
  for (int c = 1; c <= clones; c++){
    for (int n = 0; n < numGeoms; n++ )
    {
      m_geometryBboxes[n + numGeoms * c] = m_geometryBboxes[n];

      const Geometry& geomorig  = m_geometry[n];
      Geometry& geom = m_geometry[n + numGeoms * c];

      geom = geomorig;
      geom.cloneIdx = n;
    }
  }


  // nodes
  int numObjects  = 0;
  m_matrices.resize( csf->numNodes * copies );

  for (int n = 0; n < csf->numNodes; n++){
    CSFNode*    csfnode  = &csf->nodes[n];

    memcpy( m_matrices[n].objectMatrix.get_value(), csfnode->objectTM, sizeof(float)*16 );
    memcpy( m_matrices[n].worldMatrix.get_value(),  csfnode->worldTM,  sizeof(float)*16 );
    
    m_matrices[n].objectMatrixIT  = nv_math::transpose( nv_math::invert(m_matrices[n].objectMatrix) );
    m_matrices[n].worldMatrixIT   = nv_math::transpose( nv_math::invert(m_matrices[n].worldMatrix) );

    if (csfnode->geometryIDX < 0) continue;
    
    numObjects++;
  }


  // objects
  m_objects.resize( numObjects * copies );
  m_objectAssigns.resize( numObjects * copies );
  numObjects = 0;
  for (int n = 0; n < csf->numNodes; n++){
    CSFNode*    csfnode  = &csf->nodes[n];

    if (csfnode->geometryIDX < 0) continue;
    
    Object& object = m_objects[numObjects];

    object.matrixIndex = n;
    object.geometryIndex = csfnode->geometryIDX;

    m_objectAssigns[numObjects] = nv_math::vec2i( object.matrixIndex, object.geometryIndex );

    object.parts.resize( csfnode->numParts );
    for (int i = 0; i < csfnode->numParts; i++){
      object.parts[i].active = 1;
      object.parts[i].matrixIndex   = csfnode->parts[i].nodeIDX < 0 ? object.matrixIndex : csfnode->parts[i].nodeIDX;
      object.parts[i].materialIndex = csfnode->parts[i].materialIDX;
#if 1
      if (csf->materials[csfnode->parts[i].materialIDX].color[3] < 0.9f){
        object.parts[i].active = 0;
      }
#endif
    }

    BBox bbox = m_geometryBboxes[object.geometryIndex].transformed( m_matrices[n].worldMatrix );
    m_bbox.merge( bbox );

    updateObjectDrawCache(object);

    numObjects++;
  }

  // compute clone move delta based on m_bbox;

  nv_math::vec4f dim = m_bbox.max - m_bbox.min;

  int sq = 1;
  int numAxis = 0;
  for (int i = 0; i < 3; i++){
    numAxis += (cloneaxis & (1<<i)) ? 1 : 0;
  }

  assert(numAxis);

  switch (numAxis)
  {
  case 1:
    sq = copies;
    break;
  case 2:
    while (sq * sq < copies){
      sq++;
    }
    break;
  case 3:
    while (sq * sq * sq < copies){
      sq++;
    }
    break;
  }
  

  for (int c = 1; c <= clones; c++){
    int numNodes = csf->numNodes;

    nv_math::vec4f shift = dim * 1.05f;

    float u = 0;
    float v = 0;
    float w = 0;

    switch (numAxis)
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
      w = float( c / (sq*sq));
      break;
    }

    float use = u;

    if (cloneaxis & (1<<0)){
      shift.x *= -use;
      if (numAxis > 1 ) use = v;
    }
    else {
      shift.x = 0;
    }

    if (cloneaxis & (1<<1)){
      shift.y *= use;
      if (numAxis > 2 )       use = w;
      else if (numAxis > 1 )  use = v;
    }
    else {
      shift.y = 0;
    }

    if (cloneaxis & (1<<2)){
      shift.z *= -use;
    }
    else {
      shift.z = 0;
    }

    shift.w = 0;

    // move all world matrices
    for (int n = 0; n < numNodes; n++ )
    {
      MatrixNode &node = m_matrices[n + numNodes * c];
      MatrixNode &nodeOrig = m_matrices[n];
      node = nodeOrig;
      node.worldMatrix.set_col(3,node.worldMatrix.col(3) + shift);
      node.worldMatrixIT   = nv_math::transpose( nv_math::invert(node.worldMatrix) );
    }

    {
      // patch object matrix of root
      MatrixNode &node = m_matrices[csf->rootIDX + numNodes * c];
      node.objectMatrix.set_col(3,node.objectMatrix.col(3) + shift);
      node.objectMatrixIT  = nv_math::transpose( nv_math::invert(node.objectMatrix) );
    }
    
    // clone objects
    for (int n = 0; n < numObjects; n++ )
    {
      const Object& objectorig  = m_objects[n];
      Object& object            = m_objects[ n + numObjects * c];
      
      object = objectorig;
      object.geometryIndex      += c * numGeoms;
      object.matrixIndex        += c * numNodes;
      for (size_t i = 0; i < object.parts.size(); i++){
        object.parts[i].matrixIndex += c * numNodes;
      }

      for (size_t i = 0; i < object.cacheSolid.state.size(); i++){
        object.cacheSolid.state[i].matrixIndex += c * numNodes;
      }
      for (size_t i = 0; i < object.cacheWire.state.size(); i++){
        object.cacheWire.state[i].matrixIndex += c * numNodes;
      }

      m_objectAssigns[n + numObjects * c] = nv_math::vec2i( object.matrixIndex, object.geometryIndex );
    }

  }

  CSFileMemory_delete(mem);
  return true;
}


struct ListItem{
  CadScene::DrawStateInfo   state;
  CadScene::DrawRange       range;
};

static bool ListItem_compare(const ListItem &a, const ListItem &b)
{
  int diff = 0;
  diff = diff != 0 ? diff : (a.state.materialIndex - b.state.materialIndex);
  diff = diff != 0 ? diff : (a.state.matrixIndex - b.state.matrixIndex);
  diff = diff != 0 ? diff : int(a.range.offset - b.range.offset);

  return diff < 0;
}

static void fillCache(CadScene::DrawRangeCache &cache, const std::vector<ListItem> &list )
{
  cache = CadScene::DrawRangeCache();

  if (!list.size()) return;

  CadScene::DrawStateInfo state = list[0].state;
  CadScene::DrawRange range = list[0].range;

  int stateCount = 0;

  for (size_t i = 1; i < list.size()+1; i++)
  {
    bool newrange = false;
    if (i == list.size() || list[i].state != state){
      // push range
      if (range.count){
        stateCount++;
        cache.offsets.push_back( range.offset );
        cache.counts.push_back( range.count );
      }

      // emit
      if (stateCount){
        cache.state.push_back(state);
        cache.stateCount.push_back(stateCount);
      }

      stateCount = 0;

      if (i == list.size())
      {
        break;
      }
      else
      {
        state = list[i].state;
        range.offset = list[i].range.offset;
        range.count  = 0;
        newrange = true;
      }
    }

    const CadScene::DrawRange& currange = list[i].range;
    if (newrange || (USE_CACHECOMBINE && currange.offset == (range.offset + sizeof(unsigned int)*range.count ))){
      // merge
      range.count += currange.count;
    }
    else{
      // push
      if (range.count){
        stateCount++;
        cache.offsets.push_back( range.offset );
        cache.counts.push_back( range.count );
      }

      range = currange;
    }
  }
  
}

void CadScene::updateObjectDrawCache( Object& object )
{
  Geometry& geom = m_geometry[object.geometryIndex];

  std::vector<ListItem>  listSolid;
  std::vector<ListItem>  listWire;

  listSolid.reserve(geom.parts.size());
  listWire.reserve(geom.parts.size());

  for (size_t i = 0; i < geom.parts.size(); i++)
  {
    if (!object.parts[i].active) continue;

    ListItem item;
    item.state.materialIndex = object.parts[i].materialIndex;

    item.range = geom.parts[i].indexSolid;
    item.state.matrixIndex = object.parts[i].matrixIndex;
    listSolid.push_back(item);

    item.range = geom.parts[i].indexWire;
    item.state.matrixIndex = object.parts[i].matrixIndex;
    listWire.push_back(item);
  }

  std::sort( listSolid.begin(), listSolid.end(), ListItem_compare );
  std::sort( listWire.begin(),  listWire.end(),  ListItem_compare );

  fillCache(object.cacheSolid, listSolid);
  fillCache(object.cacheWire,  listWire);
}

void CadScene::unload()
{
  if (m_geometry.empty()) return;

  
  for (size_t i = 0 ; i < m_geometry.size(); i++)
  {
    if (m_geometry[i].cloneIdx >= 0) continue;

    delete [] m_geometry[i].vertices;
    delete [] m_geometry[i].indices;
  }

  m_matrices.clear();
  m_geometryBboxes.clear();
  m_geometry.clear();
  m_objectAssigns.clear();
  m_objects.clear();
  m_geometryBboxes.clear();
}


