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


#version 440 core
/**/

//#extension GL_ARB_shading_language_include : enable
#include "common.h"

#ifdef VULKAN

  #if UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_MULTISETSSTATIC
  
    layout(set=DRAW_UBO_SCENE, binding=0, std140) uniform sceneBuffer {
      SceneData       scene;
    };
    layout(set=DRAW_UBO_MATERIAL, binding=0, std140) uniform materialBuffer {
      MaterialData    material;
    };
    
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_ALLDYNAMIC || UNIFORMS_TECHNIQUE == UNIFORMS_SPLITDYNAMIC
  
    layout(set=0, binding=DRAW_UBO_SCENE, std140) uniform sceneBuffer {
      SceneData       scene;
    };
    layout(set=0, binding=DRAW_UBO_MATERIAL, std140) uniform materialBuffer {
      MaterialData    material;
    };
    
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_RAW
  
    layout(set=0, binding=DRAW_UBO_SCENE, std140) uniform sceneBuffer {
      SceneData       scene;
    };
    layout(std140, push_constant) uniform materialBuffer {
      layout(offset = 128)
      MaterialData    material;
    };
    
  #elif UNIFORMS_TECHNIQUE == UNIFORMS_PUSHCONSTANTS_INDEX
  
    #define USE_INDEXING 1
  
    layout(std140, push_constant) uniform indexSetup {
      int matrixIndex;
      int materialIndex;
    };  
    layout(set=0, binding=DRAW_UBO_SCENE, std140) uniform sceneBuffer {
      SceneData   scene;
    };
    layout(set=0, binding=DRAW_UBO_MATERIAL, std430) readonly buffer materialBuffer {
      MaterialData    materials[];
    };
    
  #endif

#else
  layout(binding=DRAW_UBO_SCENE, std140) uniform sceneBuffer {
    SceneData       scene;
  };
  layout(binding=DRAW_UBO_MATERIAL, std140) uniform materialBuffer {
    MaterialData    material;
  };
#endif

layout(location=0) in Interpolants {
  vec3 wPos;
  vec3 wNormal;
} IN;

layout(location=0,index=0) out vec4 out_Color;

void main()
{

#if USE_INDEXING

  int mi = materialIndex * 2; // due to ubo-256 byte padding * 2, FIXME should use cleaner approach
  
  if (WIREMODE != 0){
    out_Color = materials[mi].sides[1].diffuse*1.5 + 0.3;
  }
  else {
    MaterialSide side = materials[mi].sides[gl_FrontFacing ? 1 : 0];
    
#else

  if (WIREMODE != 0){
    out_Color = material.sides[1].diffuse*1.5 + 0.3;
  }
  else {
    MaterialSide side = material.sides[gl_FrontFacing ? 1 : 0];
    
#endif

    vec4 color = side.ambient + side.emissive;
  
    vec3 eyePos = vec3(scene.viewMatrixIT[0].w,scene.viewMatrixIT[1].w,scene.viewMatrixIT[2].w);

    vec3 lightDir = normalize( scene.wLightPos.xyz - IN.wPos);
    vec3 viewDir  = normalize( eyePos - IN.wPos);
    vec3 halfDir  = normalize(lightDir + viewDir);
    vec3 normal   = normalize(IN.wNormal) * (gl_FrontFacing ? 1 : -1);
  
    float ldot = dot(normal,lightDir);
    normal *= sign(ldot);
    ldot   *= sign(ldot);
  
    color += side.diffuse * ldot;
    color += side.specular * pow(max(0,dot(normal,halfDir)),16);

    out_Color = color;
  }
}
