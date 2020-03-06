# Vulkan CAD Scene Uniform Handling

In this file we discuss the various uniform passing methods that the
"Threaded CAD Scene" sample implements. For an overview on Vulkan's shader resource
binding, we recommend to have a look at this [article](https://developer.nvidia.com/vulkan-shader-resource-binding).

## Vulkan Uniform Handling

Vulkan provides various ways to pass uniform data. In this sample we have two
uniforms that we pass at high frequency, material and matrix data and one that
is common to all (scene data). The sample allows five different ways to handle
this by setting the ```UNIFORMS_TECHNIQUE``` define in resources_vk.hpp.
They affect both the GLSL code and the Vulkan API usage.

``` cpp
#define UBO_SCENE     0
#define UBO_MATRIX    1
#define UBO_MATERIAL  2
```

#### UNIFORMS_ALLDYNAMIC
A single DescriptorSet is used and all three uniform buffers use a dynamic offset
and are made available to all stages.

``` glsl
layout(binding=UBO_SCENE, set=0) uniform sceneBuffer {
  SceneData     scene;
};
layout(binding=UBO_MATRIX, set=0) uniform matrixBuffer {
  MatrixData    matrix;
};
layout(binding=UBO_MATERIAL, set=0) uniform materialBuffer {
  MaterialData  material;
};
```

At render time we therefore update the binding every time a material or matrix changes:

``` cpp
uint32_t offsets[UBOS_NUM];
offsets[UBO_SCENE]    = 0;
offsets[UBO_MATRIX]   = di.matrixIndex    * res->m_alignedMatrixSize;
offsets[UBO_MATERIAL] = di.materialIndex  * res->m_alignedMaterialSize;

vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            0, 1, &res->m_descriptorSet, sizeof(offsets)/sizeof(offsets[0]),offsets);
```

This is not great for the GPU. Effectively only the vertex shader needs the matrix,
and only the fragment shader needs the material update. However in this approach
we update three addresses for both stages, even if their addresses remained the same.
This adds extra costs for the GPU processing.


#### UNIFORMS_SPLITDYNAMIC
Improving the above method we use more accurate stage assignment when we create
the VkDescriptorSetLayoutBinding, and only flag matrix and material using dynamic offset.

``` cpp
uint32_t offsets[2];
offsets[UBO_MATRIX-1]   = di.matrixIndex    * res->m_alignedMatrixSize;
offsets[UBO_MATERIAL-1] = di.materialIndex  * res->m_alignedMaterialSize;

vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
            0, 1, &res->m_descriptorSet, sizeof(offsets)/sizeof(offsets[0]),offsets);
```

This is much better for the GPU, but the downside is that we still have to
update both addresses at once, even if only one changes.

#### UNIFORMS_MULTISETSDYNAMIC (sample default)
To prevent redundant updates we use multiple DescriptorSets instead of one, as
we can update each set independently.

``` glsl
// compared to the above each UBO has its own set
layout(binding=0, set=UBO_SCENE) uniform sceneBuffer {
  SceneData     scene;
};
layout(binding=0, set=UBO_MATRIX) uniform matrixBuffer {
  MatrixData    matrix;
};
layout(binding=0, set=UBO_MATERIAL) uniform materialBuffer {
  MaterialData  material;
};
```

During CommandBuffer creation the appropriate set will get bound with an offset.

``` cpp
if (lastMatrix != di.matrixIndex)
{
  uint32_t offset = di.matrixIndex    * res->m_alignedMatrixSize;
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
    UBO_MATRIX, 1, &res->m_descriptorSet[UBO_MATRIX], 1, &offset);
}

if (lastMaterial != di.materialIndex)
{
  uint32_t offset = di.materialIndex    * res->m_alignedMaterialSize;
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
    UBO_MATERIAL, 1, &res->m_descriptorSet[UBO_MATERIAL], 1, &offset);
}  
```

For this sample this approach is the best in terms of GPU and CPU performance.
For GPU we only send exactly what is being changed, and for CPU binding with
offsets is very cache friendly. The offsets can be computed from data that we
stream over, and we always use the same DescriptorSet for each stage, therefore
also very likely in the CPU cache.

#### UNIFORMS_MULTISETSSTATIC
As experiment we do not use the dynamic offset, but create one DescriptorSet for
each offset in advance. This is possible as the number of matrices and materials
in the scene is fixed. 

At CommandBuffer generation time we directly reference those DescriptorSets.

``` cpp
if (lastMatrix != di.matrixIndex)
{
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
    UBO_MATRIX, 1, &res->m_descriptorSetsMatrices[di.matrixIndex], 0, NULL);
}

if (lastMaterial != di.materialIndex)
{  
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->m_pipelineLayout,
    UBO_MATERIAL, 1, &res->m_descriptorSetsMaterials[di.materialIndex], 0, NULL);
}
```

Our CPU performance suffers in this approach, as the different DescriptorSets will
cause more cache misses than the single set and dynamic offsets we had before.

#### UNIFORMS_PUSHCONSTANTS_RAW

PushConstants are a new feature in Vulkan. They work like a small UBO whose
content can be updated directly with the CommandBuffer. The amount of memory
available is device dependent. At the time of the article being written NVIDIA
hardware provides 256 Bytes. That is just enough for our material and matrix
data in this sample.

``` glsl
// compared to the above each UBO has its own set
layout(binding=0, set=0) uniform sceneBuffer {
  SceneData     scene;
};
layout(push_constant) uniform matrixBuffer {
  MatrixData    matrix;
};
layout(push_constant) uniform materialBuffer {
  layout(offset = 128)      // matrices took first 128 bytes from push-constants
  MaterialData  material;   // therefore we offset the material data
};
```

The CommandBuffer now gets a copy of the data directly. That is why the animations
will not work when compiled in this mode, as the GPU animated data isn't used here,
but purely the CPU provided original data.

``` cpp
if (lastMatrix != di.matrixIndex)
{
  vkCmdPushConstants(cmd, res->m_pipelineLayout, 
    VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(ObjectData), &scene->m_matrices[di.matrixIndex]);
}

if (lastMaterial != di.materialIndex)
{
  vkCmdPushConstants(cmd, res->m_pipelineLayout, 
    VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(ObjectData),sizeof(MaterialData), &scene->m_materials[di.materialIndex]);
}
```

This technique is both slower on CPU (raw data being copied) and GPU (raw data
being set for every draw-call).Note however that the CAD model used is
particularly bad in the GPU case because of its few triangles per draw-call.
If you have bigger draw-calls (1000s of triangles) then the overhead of these
raw values will not be an issue.


#### UNIFORMS_PUSHCONSTANTS_INDEX

Last but not least another approach to uniforms is to provide a lookup index into
the big array of matrices and materials. This will add a little bit of GPU
overhead in terms of indirection, depending on the amount of data that may be
negligable. Similar can be achieved in OpenGL by using ```gl_BaseInstanceARB```
or by setting up a constant vertex-attribute via glVertexBindingDivisor and 
indirectly using the base instance (which also still works in Vulkan).

``` glsl
// A single descriptor set now olds the scene UBO, and the rest of the data as SSBO
layout(binding=UBO_SCENE, set=0) uniform sceneBuffer {
  SceneData     scene;
};
layout(binding=UBO_MATERIAL, set=0) buffer matrixBuffer {
  MatrixData    matrices[];
};
layout(binding=UBO_SCENE, set=0) buffer materialBuffer {
  MaterialData  materials[];
};

layout(push_constant) uniform indexSetup {
  int matrixIndex; 
  int materialIndex;
};
```

On the CPU side we just pass the indices. This still allows animation, as the
actual data is fetched via the SSBO binding.

``` cpp
if (lastMatrix != di.matrixIndex)
{
  vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(int32_t), &di.matrixIndex);
}

if (lastMaterial != di.materialIndex)
{
  vkCmdPushConstants(cmd, res->m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(int32_t),sizeof(int32_t), &di.materialIndex);
}
```

Our CPU performance is very fast again with this approach, but GPU wise we still
suffer from the constant update between tiny draw-calls and a little bit of the indirection.


### Uniform Handling Conclusion

Similar to OpenGL there is many ways to pass your data to a shader, we suggest
where it makes sense to make use of ```VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC```.
Passing the offsets is cheap on our hardware and CPU cache-friendly as a single
DescriptorSet can be used. Compared to push constants, you can update the data
within buffers independently of the command buffers.

Be aware that the CAD model used here can be on the extreme when it comes to
triangles per draw-call so time in the front-end processor of a GPU can become
critical. In the past this was often hidden by CPU bottlenecks, but now it becomes
visible.

What we have not touched on is actually creating DescriptorSets. The scene here
was rather static and didn't contain texture assignments.

#### UBO vs SSBO

On NVIDIA hardware UBOs are best for uniform data access. While you can store small
arrays within them, the arrays should be accessed using the same index within 
a subgroup (aka as warp, 32 threads for our hardware), otherwise there is a
performance penalty.

SSBOs are used whenever you need to store data, but also when you anticipate
indexing freely into larger arrays.

#### Vulkan 1.2 Features

Using Vulkan 1.2 or [VK_EXT_descriptor_indexing](https://github.com/KhronosGroup/Vulkan-Docs/blob/master/appendices/VK_EXT_descriptor_indexing.txt)
and [VK_EXT_buffer_device_address](https://github.com/KhronosGroup/Vulkan-Docs/blob/master/appendices/VK_EXT_buffer_device_address.txt)
you can encode resources more easily within buffer data directly. This allows
to avoid binding and updating the descriptor sets a lot, because now the important
access to our buffers or images are device addresses or indices into large tables of
descriptorsets.

This greatly simplifies our binding management, as we can foremost focus on updating
raw buffer data that contains these addresses or indices (for example by using
```VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC``` or push constants).

Have a look at [this project](https://github.com/nvpro-samples/glsl_indexed_types_generator)
to see how it can look on the shader side.