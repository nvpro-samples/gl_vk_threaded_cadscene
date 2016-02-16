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


#include <assert.h>
#include <main.h>

#include "nvdrawvulkanimage.h"

#include <vulkan/vulkan.h>

#if VK_EXT_debug_report && defined(_DEBUG)

VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugReportCallback(
  VkDebugReportFlagsEXT               msgFlags,
  VkDebugReportObjectTypeEXT          objType,
  uint64_t                            object,
  size_t                              location,
  int32_t                             msgCode,
  const char*                         pLayerPrefix,
  const char*                         pMsg,
  void*                               pUserData)
{
  const char* messageCodeString = "UNKNOWN";
  bool isError = false;
  if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
  {
    messageCodeString = "INFO";
  }
  else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
  {
    // We know that we're submitting queues without fences, ignore this warning
    if (strstr(pMsg, "vkQueueSubmit parameter, VkFence fence, is null pointer")){
      return false;
    }

    messageCodeString = "WARN";
  }
  else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
  {
    messageCodeString = "PERF";
  }
  else if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
  {
    messageCodeString = "ERROR";
    isError = true;
  }
  else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
  {
    messageCodeString = "DEBUG";
  }

  char *message = (char *)malloc(strlen(pMsg) + 100);
  sprintf(message, "%s: [%s] Code %d : %s", messageCodeString,  pLayerPrefix, msgCode, pMsg);

  LOGW("%s\n",message);

  free(message);

  /*
  * false indicates that layer should not bail-out of an
  * API call that had validation failures. This may mean that the
  * app dies inside the driver due to invalid parameter(s).
  * That's what would happen without validation layers, so we'll
  * keep that behavior here.
  */
  return false;
}

// not exactly clean

static PFN_vkCreateDebugReportCallbackEXT   p_vkCreateDebugReportCallbackEXT;
static PFN_vkDestroyDebugReportCallbackEXT  p_vkDestroyDebugReportCallbackEXT;
static VkDebugReportCallbackEXT             s_dbgMsgCallback = NULL;

static void initDebugCallback(VkInstance instance)
{
  p_vkCreateDebugReportCallbackEXT =
    (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
    instance, "vkCreateDebugReportCallbackEXT");
  p_vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
    instance, "vkDestroyDebugReportCallbackEXT");
  s_dbgMsgCallback = NULL;
  if (!p_vkCreateDebugReportCallbackEXT || !p_vkDestroyDebugReportCallbackEXT) {
    return;
  }

  VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
  dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
  dbgCreateInfo.pNext = NULL;
  dbgCreateInfo.pfnCallback = vulkanDebugReportCallback;
  dbgCreateInfo.pUserData = NULL;
  dbgCreateInfo.flags =
    VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
  VkResult res = p_vkCreateDebugReportCallbackEXT(instance, &dbgCreateInfo, NULL,
    &s_dbgMsgCallback);
  assert(res == VK_SUCCESS);
}

void vulkanContextCleanup(VkDevice device, VkPhysicalDevice physicalDevice,  VkInstance instance)
{
  if (s_dbgMsgCallback){
    p_vkDestroyDebugReportCallbackEXT(instance, s_dbgMsgCallback, NULL);
  }
}

#else

static void initDebugCallback(VkInstance instance)
{

}

void vulkanContextCleanup(VkDevice device, VkPhysicalDevice physicalDevice,  VkInstance instance)
{
  
}

#endif

bool vulkanInitLibrary()
{
  if (!init_NV_draw_vulkan_image(NVPWindow::sysGetProcAddress)) return false;

#if USEVULKANSDK
  return true;
#else
  if (__nvkglGetVkProcAddrNV){
    vkLoadProcs( __nvkglGetVkProcAddrNV );
  }
  
  if (pfn_vkCreateDevice != NULL)
    return true;

  return false;
#endif
}

#if !USEVULKANSDK
bool vulkanCreateContext(VkDevice &device, VkPhysicalDevice& physicalDevice,  VkInstance &instance, const char* appTitle, const char* engineName)
{
  VkResult result;
  VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
  uint32_t physicalDeviceCount = 1;
  VkDeviceCreateInfo devInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

  appInfo.pApplicationName = appTitle;
  appInfo.pEngineName = engineName;
  appInfo.apiVersion = VK_API_VERSION;
  instanceInfo.pApplicationInfo = &appInfo;

#if defined(_DEBUG) && VK_EXT_debug_report
  const char* extensions[] = { VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

  instanceInfo.enabledExtensionCount = 1;
  instanceInfo.ppEnabledExtensionNames = extensions;
#endif

  result = vkCreateInstance(&instanceInfo, NULL, &instance);
  if (result != VK_SUCCESS) {
    return false;
  }

  result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, &physicalDevice);
  if (result != VK_SUCCESS) {
    return false;
  }

  queueInfo.queueFamilyIndex = 0;
  queueInfo.queueCount = 1;
  devInfo.queueCreateInfoCount = 1;
  devInfo.pQueueCreateInfos = &queueInfo;
  result = vkCreateDevice(physicalDevice, &devInfo, NULL, &device);
  if (result != VK_SUCCESS) {
    return false;
  }

#if defined(_DEBUG) && VK_EXT_debug_report
  initDebugCallback(instance);
#endif

  return true;

}
#else

#include <assert.h>

#include <vector>
#include <string>
#include <set>
#include <algorithm>

using std::vector;
using std::string;
using std::set;



#define CHECK_VK_RESULT() assert(result == VK_SUCCESS)

std::vector<VkLayerProperties> GetGlobalLayerProperties()
{
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;

  uint32_t count = 0;
  result =  vkEnumerateInstanceLayerProperties(&count, NULL);
  CHECK_VK_RESULT();

  std::vector<VkLayerProperties> layers(count);

  result = vkEnumerateInstanceLayerProperties(&count, layers.data());
  CHECK_VK_RESULT();
  return layers;
}


std::vector<VkExtensionProperties> GetGlobalExtensionProperties()
{
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;

  uint32_t count = 0;
  result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
  CHECK_VK_RESULT();

  std::vector<VkExtensionProperties> extensions(count);

  result = vkEnumerateInstanceExtensionProperties(NULL, &count, extensions.data());
  CHECK_VK_RESULT();
  return extensions;
}

std::vector<VkExtensionProperties> GetPhysicalDeviceExtensionProperties(VkPhysicalDevice physicalDevice)
{
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;

  uint32_t count = 0;
  result = vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, NULL);
  CHECK_VK_RESULT();

  std::vector<VkExtensionProperties> extensions(count);

  result = vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, extensions.data());
  CHECK_VK_RESULT();
  return extensions;
}

std::vector<VkLayerProperties> GetPhysicalDeviceLayerProperties(VkPhysicalDevice physicalDevice)
{
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;

  uint32_t count = 0;
  result = vkEnumerateDeviceLayerProperties(physicalDevice, &count, NULL);
  CHECK_VK_RESULT();

  std::vector<VkLayerProperties> layers(count);

  result = vkEnumerateDeviceLayerProperties(physicalDevice, &count, layers.data());
  CHECK_VK_RESULT();
  return layers;
}


// TODO, put this elsewhere
#ifdef NDEBUG
#define USE_VALIDATION 0
#else
// disable for now, opengl context interaction breaks validation layers
// need clean vulkan-only version of sample to make use of it
#define USE_VALIDATION 0
#endif

std::vector<std::string> requestedLayers(bool global)
{
  vector<string> layers;
#if USE_VALIDATION
  //layers.push_back("VK_LAYER_LUNARG_api_dump");

  //layers.push_back("VK_LAYER_LUNARG_draw_state");    // doesn't like non-SPIR-V shader
  //layers.push_back("VK_LAYER_LUNARG_param_checker"); // crashing in SDK 1.0.2
  //layers.push_back("VK_LAYER_LUNARG_device_limits");
  //layers.push_back("VK_LAYER_LUNARG_image");

  //layers.push_back("VK_LAYER_LUNARG_object_tracker");
  //layers.push_back("VK_LAYER_LUNARG_mem_tracker");

  //layers.push_back("VK_LAYER_LUNARG_threading");
#endif

  return layers;
}


std::vector<std::string> requestedExtensions(bool global)
{
  vector<string> extensions;
  if (global)
  {
#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif
  }
  else
  {
    extensions.push_back("VK_NV_glsl_shader");
  }

  return extensions;
}


std::vector<std::string> filterLayers(const std::vector<VkLayerProperties>& supportedLayers, const std::vector<std::string>& layersToEnable)
{
  vector<string> supportedLayerNames;

  for (size_t i = 0; i < layersToEnable.size(); ++i)
  {
    for (size_t j = 0; j < supportedLayers.size(); ++j)
    {
      if (layersToEnable[i] == supportedLayers[j].layerName)
      {
        supportedLayerNames.push_back(layersToEnable[i]);
        break;
      }
    }
  }
  return supportedLayerNames;
}

std::vector<std::string> filterExtensions(const std::vector<VkExtensionProperties>& supportedExtensions, const std::vector<std::string>& extensionsToEnable)
{
  vector<string> supportedExtensionNames;

  for (size_t i = 0; i < extensionsToEnable.size(); ++i)
  {
    for (size_t j = 0; j < supportedExtensions.size(); ++j)
    {
      if (extensionsToEnable[i] == supportedExtensions[j].extensionName)
      {
        supportedExtensionNames.push_back(extensionsToEnable[i]);
        break;
      }
    }
  }
  return supportedExtensionNames;
}


bool vulkanCreateContext(VkDevice &device, VkPhysicalDevice& physicalDevice,  VkInstance &instance, const char* appTitle, const char* engineName)
{
  VkResult result;
  VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  applicationInfo.pApplicationName = appTitle;
  applicationInfo.pEngineName = engineName;
  applicationInfo.apiVersion = VK_API_VERSION;

  // global layers and extensions
  const vector<VkLayerProperties> globalLayerProperties = GetGlobalLayerProperties();
  const vector<VkExtensionProperties> globalExtentionsProperties = GetGlobalExtensionProperties();

  const vector<string> globalLayersToEnable = filterLayers(globalLayerProperties, requestedLayers(true));

  vector<const char*> globalLayerNames;
  for (size_t i = 0; i < globalLayersToEnable.size(); ++i)
    globalLayerNames.push_back(globalLayersToEnable[i].c_str());

  const vector<string> globlalExtensionsToEnable = filterExtensions(globalExtentionsProperties, requestedExtensions(true));
  
  bool useDebugExt = false;
  vector<const char*> globalExtensionNames;
  for (size_t i = 0; i < globlalExtensionsToEnable.size(); ++i){
    globalExtensionNames.push_back(globlalExtensionsToEnable[i].c_str());
    if ( strcmp(VK_EXT_DEBUG_REPORT_EXTENSION_NAME, globlalExtensionsToEnable[i].c_str()) == 0 ){
      useDebugExt = true;
    }
  }

  
  VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  instanceCreateInfo.pApplicationInfo = &applicationInfo;

  instanceCreateInfo.enabledExtensionCount   = globalExtensionNames.size();
  instanceCreateInfo.ppEnabledExtensionNames = globalExtensionNames.data();

  instanceCreateInfo.enabledLayerCount   = globalLayerNames.size();
  instanceCreateInfo.ppEnabledLayerNames = globalLayerNames.data();


  result = vkCreateInstance(&instanceCreateInfo, NULL, &instance);
  if (result != VK_SUCCESS) {
    return false;
  }

  if (useDebugExt){
    initDebugCallback(instance);
  }

  uint32_t physicalDeviceCount = 1;
  result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, &physicalDevice);
  if (result != VK_SUCCESS) {
    return false;
  }




  VkDeviceQueueCreateInfo queueCreateInfo;
  memset(&queueCreateInfo, 0, sizeof(queueCreateInfo));
  queueCreateInfo.queueFamilyIndex = 0;
  queueCreateInfo.queueCount = 1;

  VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

  // physical device layers and extensions
  const vector<VkLayerProperties> physicalDeviceLayerProperties = GetPhysicalDeviceLayerProperties(physicalDevice);
  const vector<VkExtensionProperties> physicalDeviceExtentionsProperties = GetPhysicalDeviceExtensionProperties(physicalDevice);

  const vector<string> physicalDeviceLayersToEnable = filterLayers(physicalDeviceLayerProperties, requestedLayers(false));

  vector<const char*> physicalDeviceLayerNames;
  for (size_t i = 0; i < physicalDeviceLayersToEnable.size(); ++i)
    physicalDeviceLayerNames.push_back(physicalDeviceLayersToEnable[i].c_str());

  const vector<string> physicalDeviceExtensionsToEnable = filterExtensions(physicalDeviceExtentionsProperties, requestedExtensions(false));

  vector<const char*> physicalDeviceExtensionNames;
  for (size_t i = 0; i < physicalDeviceExtensionsToEnable.size(); ++i)
    physicalDeviceExtensionNames.push_back(physicalDeviceExtensionsToEnable[i].c_str());

  deviceCreateInfo.enabledExtensionCount   = physicalDeviceExtensionNames.size();
  deviceCreateInfo.ppEnabledExtensionNames = physicalDeviceExtensionNames.data();

  deviceCreateInfo.enabledLayerCount   = physicalDeviceLayerNames.size();
  deviceCreateInfo.ppEnabledLayerNames = physicalDeviceLayerNames.data();

  //VkPhysicalDeviceFeatures  deviceFeatures;
  //vkGetPhysicalDeviceFeatures( physicalDevice, &deviceFeatures );
  //deviceFeatures.robustBufferAccess = 0;
  //deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

  result = vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device);
  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}
#endif

