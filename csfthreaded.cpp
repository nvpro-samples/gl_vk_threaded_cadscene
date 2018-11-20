/* Copyright (c) 2014-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#define DEBUG_FILTER     1

#include <imgui/imgui_helper.h>

#if HAS_OPENGL
  #include <include_gl.h>
  #include <nv_helpers_gl/appwindowprofiler_gl.hpp>
  #include <nv_helpers_gl/error_gl.hpp>
  #include <nv_helpers_gl/base_gl.hpp>
  #include <nv_helpers_gl/glsltypes_gl.hpp>
#else
  #include <nv_helpers/appwindowprofiler.hpp>
#endif

#include <nv_helpers/geometry.hpp>
#include <nv_helpers/misc.hpp>
#include <nv_helpers/cameracontrol.hpp>

#include "renderer.hpp"


using namespace nv_helpers;
using namespace nv_helpers_gl;


namespace csfthreaded
{
  int const SAMPLE_SIZE_WIDTH(800);
  int const SAMPLE_SIZE_HEIGHT(600);
  int const SAMPLE_MAJOR_VERSION(4);
  int const SAMPLE_MINOR_VERSION(5);


  
  class Sample 
  #if HAS_OPENGL
  : public nv_helpers_gl::AppWindowProfilerGL 
  #else
  : public nv_helpers::AppWindowProfiler
  #endif
  
  {

    enum GuiEnums {
      GUI_SHADE,
      GUI_RENDERER,
      GUI_STRATEGY,
      GUI_MSAA,
    };

  public:

    struct Tweak {
      int           renderer = 0;
      ShadeType     shade = SHADE_SOLID;
      Strategy      strategy = STRATEGY_GROUPS;
      int           msaa = 0;
      int           copies = 1;
      int           threads = 1;
      int           workingSet = 4096;
      bool          sorted = false;
      bool          animation = false;
      bool          animationSpin = false;
      int           cloneaxisX = 1;
      int           cloneaxisY = 1;
      int           cloneaxisZ = 1;
      float         percent = 1.001f;
    };


    bool                        m_useUI = true;

    ImGuiH::Registry            m_ui;
    double                      m_uiTime = 0;

    Tweak                       m_tweak;
    Tweak                       m_lastTweak;
    bool                        m_lastVsync;

    CadScene                    m_scene;
    std::vector<unsigned int>   m_renderersSorted;

    Renderer* NV_RESTRICT      m_renderer;
    Resources* NV_RESTRICT     m_resources;
    Resources::Global           m_shared;

    std::string                 m_modelFilename = "geforce.csf.gz";
    double                      m_animBeginTime;

    double                      m_lastFrameTime = 0;
    double                      m_frames = 0;
    
    double  m_statsFrameTime = 0;
    double  m_statsCpuTime = 0;
    double  m_statsGpuTime = 0;

    bool initProgram();
    bool initScene(const char *filename, int clones, int cloneaxis);
    bool initFramebuffers(int width, int height);
    void initRenderer(int type, Strategy strategy, int threads, bool sorted, float percent);
    void deinitRenderer();

    void setupConfigParameters();

    Sample() 
    #if HAS_OPENGL
      : AppWindowProfilerGL(false)
    #else
      : AppWindowProfiler(NVPWindow::WINDOW_API_VULKAN, false, true)
    #endif
    {
      setupConfigParameters();
    }

  public:

    void parseConfig(int argc, const char**argv, const std::string& path) override;

    bool begin() override;
    void think(double time) override;
    void resize(int width, int height) override;

    void processUI(int width, int height, double time);

    CameraControl m_control;

    void end() override;

    // return true to prevent m_window updates
    bool mouse_pos    (int x, int y) override
    {
      if (!m_useUI) return false;

      return ImGuiH::mouse_pos(x, y);
    }
    bool mouse_button (int button, int action) override
    {
      if (!m_useUI) return false;

      return ImGuiH::mouse_button(button, action);
    }
    bool mouse_wheel  (int wheel) override
    {
      if (!m_useUI) return false;

      return ImGuiH::mouse_wheel(wheel);
    }
    bool key_char(int key) override
    {
      if (!m_useUI) return false;

      return ImGuiH::key_char(key);
    }
    bool key_button   (int button, int action, int mods) override
    {
      if (!m_useUI) return false;

      return ImGuiH::key_button(button, action, mods);
    }

#if HAS_OPENGL
    const ContextFlagsBase* preWindowContext(int apiMajor, int apiMinor) {
      static ContextFlagsGL info;
      info.major = apiMajor;
      info.minor = apiMinor;
      info.device = Resources::s_glDevice;

      return (const ContextFlagsBase*)&info;
    }
#else
    const ContextFlagsBase* preWindowContext(int apiMajor, int apiMinor) {
      static ContextFlagsVK info;
      info.device = Resources::s_vkDevice;
    #if _DEBUG && 1
      info.addInstanceLayer("VK_LAYER_LUNARG_standard_validation");
    #endif
      info.addDeviceExtension(VK_NVX_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME,true);

      return (const ContextFlagsBase*)&info;
    }
#endif
  };


  bool Sample::initProgram()
  {
    return true;
  }

  bool Sample::initScene(const char* filename, int clones, int cloneaxis)
  {
    std::string modelFilename(filename);

    if (!nv_helpers::fileExists(filename)) {
      modelFilename = nv_helpers::getFileName(filename);
      std::vector<std::string>  searchPaths;
      searchPaths.push_back("./");
      searchPaths.push_back(sysExePath() + PROJECT_RELDIRECTORY + "/");
      searchPaths.push_back(PROJECT_DOWNLOAD_ABSDIRECTORY);
      modelFilename = nv_helpers::findFile(modelFilename, searchPaths);
    }

    m_scene.unload();

    bool status = m_scene.loadCSF(modelFilename.c_str(), clones, cloneaxis);

    LOGI("\nscene %s\n", filename);
    LOGI("geometries: %6d\n", uint32_t(m_scene.m_geometry.size()));
    LOGI("materials:  %6d\n", uint32_t(m_scene.m_materials.size()));
    LOGI("nodes:      %6d\n", uint32_t(m_scene.m_matrices.size()));
    LOGI("objects:    %6d\n", uint32_t(m_scene.m_objects.size()));
    LOGI("\n");

    m_shared.animUbo.numMatrices      = uint(m_scene.m_matrices.size());

    return status;
  }

  bool Sample::initFramebuffers(int width, int height)
  {
    return m_resources->initFramebuffer(width,height,m_tweak.msaa, getVsync());
  }

  void Sample::deinitRenderer()
  {
    if (m_renderer){
      m_resources->synchronize();
      m_renderer->deinit();
      delete m_renderer;
      m_renderer = NULL;
    }
  }

  void Sample::initRenderer(int typesort, Strategy strategy, int threads, bool sorted, float percent)
  {
    int type = m_renderersSorted[typesort];

    deinitRenderer();

    if (Renderer::getRegistry()[type]->resources() != m_resources){
      if (m_resources){
        m_resources->synchronize();
        m_resources->deinit();
      }
      m_resources = Renderer::getRegistry()[type]->resources();
      bool valid = m_resources->init(this);
      valid = valid && m_resources->initFramebuffer(m_window.m_viewsize[0],m_window.m_viewsize[1],m_tweak.msaa, getVsync());
      valid = valid && m_resources->initPrograms(sysExePath(), std::string());
      valid = valid && m_resources->initScene(m_scene);
      m_resources->m_frame = 0;

      if (!valid) {
        LOGE("resource initialization failed for renderer: %s\n", Renderer::getRegistry()[type]->name());
        exit(-1);
      }

      m_lastVsync = getVsync();
    }

    Renderer::Config config;
    config.objectFrom = 0;
    config.objectNum = uint32_t(double(m_scene.m_objects.size()) * double(m_tweak.percent));
    config.strategy = strategy;
    config.threads = threads;
    config.sorted = sorted;
    
    LOGI("renderer: %s\n", Renderer::getRegistry()[type]->name());
    m_renderer = Renderer::getRegistry()[type]->create();
    m_renderer->init(&m_scene,m_resources, config);
  }


  void Sample::end()
  {
    deinitRenderer();
    if (m_resources) {
      m_resources->deinit();
    }
  }


  bool Sample::begin()
  {

#if !PRINT_TIMER_STATS
    m_profilerPrint = false;
    m_timeInTitle = true;
#else
    m_profilerPrint = true;
    m_timeInTitle = true;
#endif

    m_profiler.setDefaultGPUInterface(NULL);

    m_renderer = NULL;
    m_resources = NULL;

    int maxthreads = ThreadPool::sysGetNumCores();

    Renderer::s_threadpool.init(maxthreads);

    ImGuiH::Init(m_window.m_viewsize[0], m_window.m_viewsize[1], this);

#if HAS_OPENGL
    {
      // smoother CPU timings for GL vs Vulkan comparison, don't do this in actual applications!!
      // it will disable optimizations
      glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
      glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

      m_debugFilter = GL_DEBUG_SEVERITY_HIGH;

      GLuint defaultVAO;
      glGenVertexArrays(1, &defaultVAO);
      glBindVertexArray(defaultVAO);
    }
#endif

#if defined (NDEBUG)
    vsync(false);
#endif

    bool validated(true);
    validated = validated && initProgram();
    validated = validated && initScene(m_modelFilename.c_str(), m_tweak.copies-1, (m_tweak.cloneaxisX<<0) | (m_tweak.cloneaxisY<<1) | (m_tweak.cloneaxisZ<<2));

    const Renderer::Registry registry = Renderer::getRegistry();
    for (size_t i = 0; i < registry.size(); i++)
    {
      if (registry[i]->isAvailable())
      {
        uint sortkey = uint(i);
        sortkey |= registry[i]->priority() << 16;
        m_renderersSorted.push_back( sortkey );
      }
    }

    if (m_renderersSorted.empty()) {
      LOGE("No renderers available\n");
      return false;
    }

    std::sort(m_renderersSorted.begin(),m_renderersSorted.end());

    for (size_t i = 0; i < m_renderersSorted.size(); i++){
      m_renderersSorted[i] &= 0xFFFF;
    }
    
    for (size_t i = 0; i < m_renderersSorted.size(); i++) {
      LOGI("renderers found: %d %s\n", uint32_t(i), registry[m_renderersSorted[i]]->name());
    }

  
    if (m_useUI){
      auto &imgui_io = ImGui::GetIO();
      imgui_io.IniFilename = nullptr;

      for (size_t i = 0; i < m_renderersSorted.size(); i++) {
        m_ui.enumAdd(GUI_RENDERER, int(i), registry[m_renderersSorted[i]]->name());
      }

      m_ui.enumAdd(GUI_STRATEGY, STRATEGY_INDIVIDUAL, "drawcall individual");
      m_ui.enumAdd(GUI_STRATEGY, STRATEGY_GROUPS, "material groups");

      m_ui.enumAdd(GUI_SHADE, SHADE_SOLID, toString(SHADE_SOLID));
      m_ui.enumAdd(GUI_SHADE, SHADE_SOLIDWIRE, toString(SHADE_SOLIDWIRE));

      m_ui.enumAdd(GUI_MSAA, 0, "none");
      m_ui.enumAdd(GUI_MSAA, 2, "2x");
      m_ui.enumAdd(GUI_MSAA, 4, "4x");
      m_ui.enumAdd(GUI_MSAA, 8, "8x");
    }

    m_control.m_sceneOrbit =     nv_math::vec3f(m_scene.m_bbox.max+m_scene.m_bbox.min)*0.5f;
    m_control.m_sceneDimension = nv_math::length((m_scene.m_bbox.max-m_scene.m_bbox.min));
    m_control.m_viewMatrix =     nv_math::look_at(m_control.m_sceneOrbit - (-vec3(1,1,1)*m_control.m_sceneDimension*0.5f), m_control.m_sceneOrbit, vec3(0,1,0));

    m_shared.animUbo.sceneCenter      = m_control.m_sceneOrbit;
    m_shared.animUbo.sceneDimension   = m_control.m_sceneDimension * 0.2f;
    m_shared.animUbo.numMatrices      = uint(m_scene.m_matrices.size());
    m_shared.sceneUbo.wLightPos = (m_scene.m_bbox.max+m_scene.m_bbox.min)*0.5f + m_control.m_sceneDimension;
    m_shared.sceneUbo.wLightPos.w = 1.0;

    initRenderer(m_tweak.renderer, m_tweak.strategy, m_tweak.threads, m_tweak.sorted, m_tweak.percent);

    m_lastTweak = m_tweak;

    return validated;
  }


  void Sample::processUI(int width, int height, double time)
  {
    // Update imgui configuration
    auto &imgui_io = ImGui::GetIO();
    imgui_io.DeltaTime = static_cast<float>(time - m_uiTime);
    imgui_io.DisplaySize = ImVec2(width, height);
    
    m_uiTime = time;

    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("NVIDIA " PROJECT_NAME, nullptr)) {
      //ImGui::PushItemWidth(200);
#if HAS_OPENGL
      ImGui::Text("gl and vk version");
#else
      ImGui::Text("vk only version");
#endif
      ImGui::Separator();

      m_ui.enumCombobox(GUI_RENDERER, "renderer", &m_tweak.renderer);
      m_ui.enumCombobox(GUI_STRATEGY, "strategy", &m_tweak.strategy);
      m_ui.enumCombobox(GUI_SHADE, "shademode", &m_tweak.shade);
      m_ui.enumCombobox(GUI_MSAA, "msaa", &m_tweak.msaa);
      //guiRegistry.enumCombobox(GUI_SUPERSAMPLE, "supersample", &tweak.supersample);
      ImGuiH::InputIntClamped("copies", &m_tweak.copies, 1, 16);
      ImGui::SliderFloat("pct visible", &m_tweak.percent, 0.0f, 1.001f);
      ImGuiH::InputIntClamped("workerthreads", &m_tweak.threads, 1, Renderer::s_threadpool.getNumThreads());
      ImGuiH::InputIntClamped("workingset", &m_tweak.workingSet, 128, 16 * 1024);
      ImGui::Checkbox("sorted", &m_tweak.sorted);
      ImGui::Checkbox("animation", &m_tweak.animation);

      ImGui::Separator();

      {
        int avg = 50;

        if (m_lastFrameTime == 0) {
          m_lastFrameTime = time;
          m_frames = -1;
        }

        if (m_frames > 4) {
          double curavg = (time - m_lastFrameTime) / m_frames;
          if (curavg > 1.0 / 30.0) {
            avg = 10;
          }
        }

        if (m_profiler.getAveragedFrames() % avg == avg - 1) {
          m_profiler.getAveragedValues("Render", m_statsCpuTime, m_statsGpuTime);
          m_statsFrameTime = (time - m_lastFrameTime) / m_frames;
          m_lastFrameTime = time;
          m_frames = -1;
        }

        m_frames++;

        float gpuTimeF = float(m_statsGpuTime);
        float cpuTimeF = float(m_statsCpuTime);
        float maxTimeF = std::max(std::max(cpuTimeF, gpuTimeF), 0.0001f);

        ImGui::Text("Frame     [ms]: %2.1f", m_statsFrameTime*1000.0f);
        ImGui::Text("Scene GPU [ms]: %2.3f", gpuTimeF / 1000.0f);
        ImGui::ProgressBar(gpuTimeF/maxTimeF, ImVec2(0.0f, 0.0f));
        ImGui::Text("Scene CPU [ms]: %2.3f", cpuTimeF / 1000.0f);
        ImGui::ProgressBar(cpuTimeF / maxTimeF, ImVec2(0.0f, 0.0f));
        
        
      }

    }
    ImGui::End();
  }

  void Sample::think(double time)
  {
    int width   = m_window.m_viewsize[0];
    int height  = m_window.m_viewsize[1];
    
    if (m_useUI) {
      processUI(width, height, time);
    }

    m_control.processActions(m_window.m_viewsize,
      nv_math::vec2f(m_window.m_mouseCurrent[0],m_window.m_mouseCurrent[1]),
      m_window.m_mouseButtonFlags, m_window.m_wheel);

    if (m_window.onPress(KEY_R)){
      m_resources->synchronize();
      m_resources->reloadPrograms(std::string());
    }

    if (m_tweak.msaa != m_lastTweak.msaa ||
        getVsync() != m_lastVsync)
    {
      m_lastVsync = getVsync();
      m_resources->initFramebuffer(width, height, m_tweak.msaa, getVsync());
    }

    bool sceneChanged = false;
    if (m_tweak.copies      != m_lastTweak.copies ||
        m_tweak.cloneaxisX  != m_lastTweak.cloneaxisX ||
        m_tweak.cloneaxisY  != m_lastTweak.cloneaxisY ||
        m_tweak.cloneaxisZ  != m_lastTweak.cloneaxisZ)
    {
      sceneChanged = true;
      m_resources->synchronize();
      deinitRenderer();
      m_resources->deinitScene();
      initScene( m_modelFilename.c_str(), m_tweak.copies-1, (m_tweak.cloneaxisX<<0) | (m_tweak.cloneaxisY<<1) | (m_tweak.cloneaxisZ<<2));
      m_resources->initScene(m_scene);
    }

    if (sceneChanged ||
	    m_tweak.renderer != m_lastTweak.renderer ||
        m_tweak.strategy != m_lastTweak.strategy ||
        m_tweak.threads != m_lastTweak.threads ||
        m_tweak.sorted  != m_lastTweak.sorted ||
        m_tweak.percent != m_lastTweak.percent)
    {
      m_resources->synchronize();
      initRenderer(m_tweak.renderer,m_tweak.strategy, m_tweak.threads, m_tweak.sorted, m_tweak.percent);
    }

    m_resources->beginFrame();

    if (m_tweak.animation != m_lastTweak.animation){
      m_resources->synchronize();
      m_resources->animationReset();

      m_animBeginTime  = time;
    }

    {
      m_shared.width  = width;
      m_shared.height = height;

      SceneData& sceneUbo = m_shared.sceneUbo;

      sceneUbo.viewport = ivec2(width,height);

      nv_math::mat4 projection = m_resources->perspectiveProjection((45.f), float(width)/float(height), m_control.m_sceneDimension*0.001f, m_control.m_sceneDimension*10.0f);
      nv_math::mat4 view = m_control.m_viewMatrix;

      if (m_tweak.animation && m_tweak.animationSpin){
        double animTime = (time - m_animBeginTime) * 0.3 + nv_pi*0.2;
        vec3 dir = vec3(cos(animTime),1, sin(animTime));
        view = nv_math::look_at(m_control.m_sceneOrbit - (-dir*m_control.m_sceneDimension*0.5f), m_control.m_sceneOrbit, vec3(0,1,0));
      }

      sceneUbo.viewProjMatrix = projection * view;
      sceneUbo.viewMatrix = view;
      sceneUbo.viewMatrixIT = nv_math::transpose(nv_math::invert(view));

      sceneUbo.viewPos = sceneUbo.viewMatrixIT.row(3);;
      sceneUbo.viewDir = -view.row(2);

      sceneUbo.wLightPos = sceneUbo.viewMatrixIT.row(3);
      sceneUbo.wLightPos.w = 1.0;

      m_shared.workingSet = m_tweak.workingSet;
    }


    // We use the timer sections that force flushes, to get more accurate CPU costs
    // per section. In shipping application you would not want to do this

    if (m_tweak.animation)
    {
      NV_PROFILE_SECTION_EX("Anim.", m_resources->getTimerInterface(), true );

      AnimationData& animUbo = m_shared.animUbo;
      animUbo.time = float(time - m_animBeginTime);

      m_resources->animation(m_shared);
    }

    {
      NV_PROFILE_SECTION_EX("Render", m_resources->getTimerInterface(), true );
      m_renderer->draw(m_tweak.shade,m_resources,m_shared,m_profiler);
    }

    {
      NV_PROFILE_SECTION_EX("BltUI", m_resources->getTimerInterface(), true);

      if (m_useUI) {
        ImGui::Render();
        m_shared.imguiDrawData = ImGui::GetDrawData();
      }
      else {
        m_shared.imguiDrawData = nullptr;
      }

      m_resources->blitFrame(m_shared);
    }
    
    m_resources->endFrame();
    m_resources->m_frame++;

    if (m_useUI) {
      ImGui::EndFrame();
    }

    m_lastTweak = m_tweak;
  }

  void Sample::resize(int width, int height)
  {
    initFramebuffers(width,height);
  }

  static std::string addPath(std::string const &defaultPath, std::string const &filename)
  {
    if (
#ifdef _WIN32
      filename.find(':') != std::string::npos
#else
      !filename.empty() && filename[0] == '/'
#endif
      )
    {
      return filename;
    }
    else {
      return defaultPath + "/" + filename;
    }
  }

  static bool endsWith(std::string const &s, std::string const &end) {
    if (s.length() >= end.length()) {
      return (0 == s.compare(s.length() - end.length(), end.length(), end));
    }
    else {
      return false;
    }

  }

  void Sample::setupConfigParameters()
  {
    m_parameterList.add("vkdevice", &Resources::s_vkDevice);
    m_parameterList.add("gldevice", &Resources::s_glDevice);

    m_parameterList.add("noui", &m_useUI, false);

    m_parameterList.add("renderer", (uint32_t*)&m_tweak.renderer);
    m_parameterList.add("strategy", (uint32_t*)&m_tweak.strategy);
    m_parameterList.add("shademode", (uint32_t*)&m_tweak.shade);
    m_parameterList.add("workerthreads", &m_tweak.threads);
    m_parameterList.add("msaa", &m_tweak.msaa);
    m_parameterList.add("copies", &m_tweak.copies);
    m_parameterList.add("animation", &m_tweak.animation);
    m_parameterList.add("animationspin", &m_tweak.animationSpin);
    m_parameterList.add("minstatechanges", &m_tweak.sorted);
    m_parameterList.add("workingset", &m_tweak.workingSet);
  }

  void Sample::parseConfig(int argc, const char**argv, const std::string& defaultPath)
  {
    for (uint32_t i = 0; i < uint32_t(argc); i++) {
      std::string argstr = std::string(argv[i]);
      if (endsWith(argstr, ".exe")) {}
      else if (endsWith(argstr, ".cfg") || endsWith(argstr, ".bat")) {
        parseConfigFile(argv[i]);
      }
      else if (strstr(argv[i], ".csf")) {
        m_modelFilename = addPath(defaultPath, argstr);
      }
      else if (m_parameterList.applyParameters(argc, argv, i, "-", defaultPath.c_str())) {
        int t = 0;
      }
      else {
        LOGOK("  unhandled argument %s\n", argv[i]);
      }
    }
  }
  
}

using namespace csfthreaded;

int sample_main(int argc, const char** argv)
{
  SETLOGFILENAME();
#if defined(_WIN32) && defined(NDEBUG)
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
  Sample sample;
  sample.parseConfig(argc,argv, ".");
  if (sample.m_modelFilename.empty())
  {
    LOGI("exe <filename.csf/cfg> parameters...\n");
    sample.m_parameterList.print();
    return 0;
  }

  return sample.run(
    PROJECT_NAME,
    argc, argv,
    SAMPLE_SIZE_WIDTH, SAMPLE_SIZE_HEIGHT,
    SAMPLE_MAJOR_VERSION, SAMPLE_MINOR_VERSION);
}

void sample_print(int level, const char * fmt)
{

}


