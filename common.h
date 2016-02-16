#ifndef CSFTHREADED_COMMON_H
#define CSFTHREADED_COMMON_H

#define VERTEX_POS      0
#define VERTEX_NORMAL   1
#define VERTEX_ASSIGNS  2

// changing these orders may break a lot of things ;)
#define UBO_ANIM      0
#define UBO_SCENE     0
#define UBO_MATRIX    1
#define UBO_MATERIAL  2

#define SSBO_MATRIXOUT  1
#define SSBO_MATRIXORIG 2

#define ANIMATION_WORKGROUPSIZE 256

#ifndef USE_POINTERS 
#define USE_POINTERS 0
#endif

#ifndef USE_INDEXING
#define USE_INDEXING 0
#endif

#ifndef WIREMODE
#define WIREMODE 0
#endif

#ifdef __cplusplus
namespace csfthreaded
{
#endif

struct SceneData {
  mat4  viewProjMatrix;
  mat4  viewMatrix;
  mat4  viewMatrixIT;

  vec4  viewPos;
  vec4  viewDir;
  
  vec4  wLightPos;
  
  ivec2 viewport;
  ivec2 _pad;
};

// keep compatible to cadscene!
struct ObjectData {
  mat4 worldMatrix;
  mat4 worldMatrixIT;
};

// must match cadscene
struct MaterialSide {
  vec4 ambient;
  vec4 diffuse;
  vec4 specular;
  vec4 emissive;
};

struct MaterialData {
  MaterialSide sides[2];
};

// must match cadscene!
struct MatrixData {
  mat4 worldMatrix;
  mat4 worldMatrixIT;
  mat4 objectMatrix;
  mat4 objectMatrixIT;
};

struct AnimationData {
  uint    numMatrices;
  float   time;
  vec2   _pad0;

  vec3    sceneCenter;
  float   sceneDimension;

#ifdef __cplusplus
  uint64_t  animatedMatrices;
  uint64_t  originalMatrices;
#else
#if USE_POINTERS
  MatrixData* animatedMatrices;
  MatrixData* originalMatrices;
#else
  uvec2   _pad1;
  uvec2   _pad2;
#endif
#endif
};

#ifdef __cplusplus
}
#endif


#if defined(GL_core_profile) || defined(GL_compatibility_profile) || defined(GL_es_profile)
// prevent this to be used by c++

#ifndef UBOBINDING
#define UBOBINDING(ubo)     layout(std140, binding= (ubo))
#endif

#ifndef SSBOBINDING
#define SSBOBINDING(ssbo)   layout(std430, binding= (ssbo))
#endif

#ifndef MATERIAL_LAYOUT
#define MATERIAL_LAYOUT
#endif

#ifndef MATRIX_LAYOUT
#define MATRIX_LAYOUT
#endif

#ifndef MATERIAL_BINDING
#define MATERIAL_BINDING    UBOBINDING(UBO_MATERIAL)
#endif

#ifndef MATRIX_BINDING
#define MATRIX_BINDING      UBOBINDING(UBO_MATRIX)
#endif

#if USE_INDEXING
  #ifdef INDEXING_SETUP
    INDEXING_SETUP
  #else
    // debug
    int matrixIndex   = 0;
    int materialIndex = 0;
  #endif
#endif

#endif

#endif
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
