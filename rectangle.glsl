#version 430

// render as single triangle strip

#if _VERTEX_

layout(location=0) uniform vec4 rectangle;

out vec2  uv;

void main()
{
  int idx = gl_VertexID;
  uv =  vec2(
      (float( idx     &1U)),
      (step(2,idx)));
  
  vec4 pos = vec4(
    rectangle.x + uv.x * rectangle.z,
    rectangle.y + uv.y * rectangle.w,
    0, 1.0);
  
  gl_Position = pos;
}

#endif

#if _FRAGMENT_

in vec2 uv;
layout(location=0,index=0) out vec4 out_Color;

layout(location=1) uniform vec4  color;

#ifdef AUXILIARY
AUXILIARY
#else
vec4  getAuxiliary(vec2 uv){ return vec4(1); }
#endif

void main()
{
  out_Color = getAuxiliary(uv) * color;
}

#endif

/*-----------------------------------------------------------------------
  Copyright (c) 2016, NVIDIA. All rights reserved.

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