#version 430
/**/

#include "common.h"

UBOBINDING(UBO_ANIM) uniform animBuffer {
  AnimationData   anim;
};


layout (local_size_x = ANIMATION_WORKGROUPSIZE) in;

#if USE_POINTERS
  #define animated anim.animatedMatrices
  #define original anim.originalMatrices
#else
SSBOBINDING(SSBO_MATRIXOUT) restrict buffer matricesBuffer {
  MatrixData animated[];
};

SSBOBINDING(SSBO_MATRIXORIG) restrict buffer matricesOrigBuffer {
  MatrixData original[];
};
#endif

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


/*-----------------------------------------------------------------------
  Copyright (c) 2015-2016, NVIDIA. All rights reserved.

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