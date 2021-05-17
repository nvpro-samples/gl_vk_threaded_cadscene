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


#version 440
/**/

#include "common.h"

layout (local_size_x = ANIMATION_WORKGROUPSIZE) in;

layout(binding=ANIM_UBO, std140) uniform animBuffer {
  AnimationData   anim;
};

layout(binding=ANIM_SSBO_MATRIXOUT, std430) restrict buffer matricesBuffer {
  MatrixData animated[];
};

layout(binding=ANIM_SSBO_MATRIXORIG, std430) restrict buffer matricesOrigBuffer {
  MatrixData original[];
};

void main()
{
  int self = int(gl_GlobalInvocationID.x);
  if (gl_GlobalInvocationID.x >= anim.numMatrices){
    return;
  }
  
  float s = 1-(float(self)/float(anim.numMatrices));
  float movement = 4;             // time until all objects done with moving (<= sequence*0.5)
  float sequence = movement*2+3;  // time for sequence
  
  float timeS = fract(anim.time / sequence) * sequence;
  float time  = clamp(timeS - s*movement,0,1) - clamp(timeS - (1-s)*movement - sequence*0.5, 0, 1);
  
  float scale         = smoothstep(0,1,time);
  
  mat4 matrixOrig     = original[self].worldMatrix;
  vec3 pos  = matrixOrig[3].xyz;
  vec3 away = (pos - anim.sceneCenter );
  
  float diridx  = float(self % 3);
  float sidx    = float(self % 6);

  vec3 delta;
  #if 1
  #pragma optionNV(ifcvt 16)
  delta.x = diridx == 0 ? 1 : 0;
  delta.y = diridx == 1 ? 1 : 0;
  delta.z = diridx == 2 ? 1 : 0;
  #else
  delta.x = step(diridx,0.5);
  delta.y = step(abs(diridx-1),0.5);
  delta.z = step(abs(diridx-2),0.5);
  #endif
  
  delta *= -sign(sidx-2.5);
  delta *= sign(dot(away,delta));
  
  delta = normalize(delta);
  pos += delta * scale * anim.sceneDimension;
  
  animated[self].worldMatrix = mat4(matrixOrig[0], matrixOrig[1], matrixOrig[2], vec4(pos,1));
}
