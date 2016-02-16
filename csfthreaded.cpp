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
/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#define DEBUG_FILTER     1

#ifndef HAS_TWEAKBAR
#define HAS_TWEAKBAR 1
#endif

#include <sstream>

#include <GL/glew.h>
#if HAS_TWEAKBAR
#include <nv_helpers/anttweakbar.hpp>
#endif
#include <nv_helpers_gl/WindowProfiler.hpp>
#include <nv_math/nv_math_glsltypes.h>

#include <nv_helpers_gl/error.hpp>
#include <nv_helpers_gl/programmanager.hpp>
#include <nv_helpers/geometry.hpp>
#include <nv_helpers/misc.hpp>
#include <nv_helpers_gl/glresources.hpp>
#include <nv_helpers/cameracontrol.hpp>

#include <nv_helpers_gl/OpenGLText/OpenGLText.h>
#include <nv_helpers_gl/OpenGLText/arial_10.h>
#include <nv_helpers_gl/OpenGLText/arial_10_bitmap.h>
#include <nv_helpers_gl/OpenGLText/baub_16.h>
#include <nv_helpers_gl/OpenGLText/baub_16_bitmap.h>

#include "renderer.hpp"


using namespace nv_helpers;
using namespace nv_helpers_gl;


namespace csfthreaded
{
  int const SAMPLE_SIZE_WIDTH(800);
  int const SAMPLE_SIZE_HEIGHT(600);
  int const SAMPLE_MAJOR_VERSION(4);
  int const SAMPLE_MINOR_VERSION(3);

  static ProgramManager ProgManager;

  
  class Sample : public nv_helpers_gl::WindowProfiler {

  public:

    struct Tweak {
      int           renderer;
      ShadeType     shade;
      Strategy      strategy;
      int           msaa;
      int           copies;
      int           threads;
      int           workingSet;
      bool          sorted;
      bool          animation;
      bool          animationSpin;
      int           cloneaxisX;
      int           cloneaxisY;
      int           cloneaxisZ;
      float         percent;

      Tweak() 
        : renderer(0)
        , shade(SHADE_SOLID)
        , strategy(STRATEGY_GROUPS)
        , sorted(false)
        , copies(1)
        , percent(1.001f)
        , cloneaxisX(1)
        , cloneaxisY(1)
        , cloneaxisZ(0)
        , threads(1)
        , msaa(0)
        , workingSet(4096)
        , animation(false)
        , animationSpin(false)
      {}
    };

    bool                  useTweakBar;
    bool                  useRenderGraph;

    Tweak                 tweak;
    Tweak                 lastTweak;

    CadScene              cadscene;
    std::vector<unsigned int>  sortedRenderers;

    Renderer* NVP_RESTRICT  renderer;
    Resources* NVP_RESTRICT resources;
    Resources::Global     shared;

    std::string           filename;
    double                animBeginTime;
    bool                  useSharedContext;

    ProgramManager::ProgramID drawRectangleProgram;

    OpenGLText            text;
    OpenGLText            textfps;

    double lastFrameTime;
    double frames;

    double  frameTime;
    double  cpuTime;
    double  gpuTime;

    bool initProgram();
    bool initScene(const char *filename, int clones, int cloneaxis);
    bool initFramebuffers(int width, int height);
    void initRenderer(int type, Strategy strategy, int threads, bool sorted, float percent);
    void deinitRenderer();

    Sample() 
      : WindowProfiler(false)
      , useTweakBar(true)
      , useSharedContext(false)
      , useRenderGraph(true)
      , cpuTime(0)
      , gpuTime(0)
      , frameTime(0)
      , lastFrameTime(0)
      , frames(0)
    {
    }

  public:

    void parse(int argc, const char**argv);

    bool begin();
    void think(double time);
    void resize(int width, int height);

    CameraControl m_control;

    void end() {
      deinitRenderer();
      Renderer::s_threadpool.deinit();
#if HAS_TWEAKBAR
      if (useTweakBar){
        TwTerminate();
      }
#endif
    }
#if HAS_TWEAKBAR
    // return true to prevent m_window updates
    bool mouse_pos    (int x, int y) {
      if (!useTweakBar) return false;

      return !!TwEventMousePosGLFW(x,y); 
    }
    bool mouse_button (int button, int action) {
      if (!useTweakBar) return false;

      return !!TwEventMouseButtonGLFW(button, action);
    }
    bool mouse_wheel  (int wheel) {
      if (!useTweakBar) return false;

      return !!TwEventMouseWheelGLFW(wheel); 
    }
    bool key_button   (int button, int action, int mods) {
      if (!useTweakBar) return false;

      return handleTwKeyPressed(button,action,mods);
    }
#endif
  };


  bool Sample::initProgram()
  {
    bool validated(true);
    ProgManager.addDirectory( std::string(PROJECT_NAME));
    ProgManager.addDirectory( sysExePath() + std::string(PROJECT_RELDIRECTORY));
    ProgManager.addDirectory( std::string(PROJECT_ABSDIRECTORY));

    ProgManager.registerInclude("common.h", "common.h");

    drawRectangleProgram = ProgManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,    "#define _VERTEX_ 1\n",    "rectangle.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER,  "#define _FRAGMENT_ 1\n",  "rectangle.glsl"));

    return true;
  }

  bool Sample::initScene(const char* filename, int clones, int cloneaxis)
  {
    cadscene.unload();

    bool status = cadscene.loadCSF(filename, clones, cloneaxis);

    printf("\nscene %s\n", filename);
    printf("geometries: %6d\n", cadscene.m_geometry.size());
    printf("materials:  %6d\n", cadscene.m_materials.size());
    printf("nodes:      %6d\n", cadscene.m_matrices.size());
    printf("objects:    %6d\n", cadscene.m_objects.size());
    printf("\n");

    shared.animUbo.numMatrices      = uint(cadscene.m_matrices.size());

    return status;
  }

  bool Sample::initFramebuffers(int width, int height)
  {
    return resources->initFramebuffer(width,height,tweak.msaa);
  }

  void Sample::deinitRenderer()
  {
    if (renderer){
      resources->synchronize();
      renderer->deinit();
      delete renderer;
      renderer = NULL;
    }
  }

  void Sample::initRenderer(int typesort, Strategy strategy, int threads, bool sorted, float percent)
  {
    int type = sortedRenderers[typesort];

    deinitRenderer();

    if (Renderer::getRegistry()[type]->resources() != resources){
      if (resources){
        resources->synchronize();
        resources->deinit(ProgManager);
      }
      resources = Renderer::getRegistry()[type]->resources();
      resources->init();
      resources->initFramebuffer(m_window.m_viewsize[0],m_window.m_viewsize[1], tweak.msaa);
      resources->initPrograms(ProgManager);
      resources->initScene(cadscene);
      resources->m_frame = 0;
    }

    resources->m_threads = threads;
    resources->m_sorted  = sorted;
    resources->m_percent = double(percent);

    Renderer::getRegistry()[type]->updatedPrograms( ProgManager );
    renderer = Renderer::getRegistry()[type]->create();
    renderer->m_strategy = strategy;
    renderer->init(&cadscene,resources);
  }

  bool Sample::begin()
  {
    // smoother CPU timings for GL vs Vulkan comparison, don't do this in actual applications!!
    // it will disable optimizations
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

#if !PRINT_TIMER_STATS
    m_profilerPrint = false;
    m_timeInTitle   = true;
#else
    m_profilerPrint = true;
    m_timeInTitle   = true;
#endif

    text.init(arial_10::image,  (OpenGLText::FileHeader*) &arial_10::font, 128,128);
    textfps.init(baub_16::image,  (OpenGLText::FileHeader*) &baub_16::font, 128,128);

    m_profiler.setDefaultGPUInterface(NULL);

    renderer  = NULL;
    resources = NULL;

    m_debugFilter = GL_DEBUG_SEVERITY_HIGH;

    int maxthreads = ThreadPool::sysGetNumCores();

    makeContextNonCurrent();
    Renderer::s_threadpool.init( maxthreads, useSharedContext ? (NVPWindow*)this : THREADPOOL_NO_CONTEXT );
    makeContextCurrent();

#if HAS_TWEAKBAR
    if (useTweakBar){
      glPushAttrib(GL_ALL_ATTRIB_BITS);
      glPushClientAttrib(GL_ALL_ATTRIB_BITS);

      TwInit(TW_OPENGL_CORE,NULL);
      TwWindowSize(m_window.m_viewsize[0],m_window.m_viewsize[1]);

      glPopAttrib();
      glPopClientAttrib();
    }
#endif

#if defined (NDEBUG)
    vsync(false);
#endif

    bool validated(true);

    GLuint defaultVAO;
    glGenVertexArrays(1, &defaultVAO);
    glBindVertexArray(defaultVAO);

    validated = validated && initProgram();
    validated = validated && initScene(filename.c_str(), tweak.copies-1, (tweak.cloneaxisX<<0) | (tweak.cloneaxisY<<1) | (tweak.cloneaxisZ<<2));

    const Renderer::Registry registry = Renderer::getRegistry();
    for (size_t i = 0; i < registry.size(); i++)
    {
      if (registry[i]->isAvailable())
      {
        if (!registry[i]->loadPrograms(ProgManager)){
          fprintf(stderr,"Failed to load resources for renderer %s\n",registry[i]->name());
          return false;
        }

        uint sortkey = uint(i);
        sortkey |= registry[i]->priority() << 16;
        sortedRenderers.push_back( sortkey );
      }
    }

    std::sort(sortedRenderers.begin(),sortedRenderers.end());

    for (size_t i = 0; i < sortedRenderers.size(); i++){
      sortedRenderers[i] &= 0xFFFF;
    }
#if HAS_TWEAKBAR
    if (useTweakBar){
      TwBar *bar;
      if (useTweakBar){
        bar = TwNewBar("mainbar");
        TwDefine(" GLOBAL contained=true help='OpenGL samples.\nCopyright NVIDIA Corporation 2015-2016' ");
        TwDefine(" mainbar position='0 0' size='350 210' color='0 0 0' alpha=128 valueswidth=200 ");
        TwDefine((std::string(" mainbar label='") + PROJECT_NAME + "'").c_str());
      }

      std::vector<TwEnumVal>  rendererVals;
      for (size_t i = 0; i < sortedRenderers.size(); i++){
        TwEnumVal eval;
        eval.Value = int(i);
        eval.Label = registry[sortedRenderers[i]]->name();
        rendererVals.push_back(eval);
      }

      TwType rendererType = TwDefineEnum("renderer", &rendererVals[0], int(rendererVals.size()));
      TwEnumVal strategyVals[] = {
        {STRATEGY_INDIVIDUAL, "drawcall individual"},
        {STRATEGY_GROUPS,     "material groups"},
      };
      TwType strategyType = TwDefineEnum("strategy", strategyVals, sizeof(strategyVals)/sizeof(strategyVals[0]));
      TwEnumVal shadeVals[] = {
        {SHADE_SOLID,toString(SHADE_SOLID)},
        {SHADE_SOLIDWIRE,toString(SHADE_SOLIDWIRE)},
      };
      TwType shadeType = TwDefineEnum("shade", shadeVals, sizeof(shadeVals)/sizeof(shadeVals[0]));

      std::stringstream ss;
      ss << " label='workerthreads' min=1 max=" << maxthreads << " ";

      TwEnumVal msaaVals[] = {
        {0,"none"},
        {2,"2x"},
        {4,"4x"},
        {8,"8x"},
      };
      TwType msaaType = TwDefineEnum("msaa", msaaVals, sizeof(msaaVals)/sizeof(msaaVals[0]));
      TwAddVarRW(bar, "renderer", rendererType,  &tweak.renderer,  " label='renderer' ");
      TwAddVarRW(bar, "strategy", strategyType,  &tweak.strategy,  " label='strategy' ");
      TwAddVarRW(bar, "shademode", shadeType,    &tweak.shade,  " label='shademode' ");
      TwAddVarRW(bar, "msaa", msaaType,  &tweak.msaa,  " label='msaa' ");
      TwAddVarRW(bar, "copies", TW_TYPE_INT32,   &tweak.copies,  " label='copies' min=1 max=256 ");
      TwAddVarRW(bar, "pct", TW_TYPE_FLOAT,   &tweak.percent,  " label='pct visible' min=0 max=1 step=0.001 precision=3 ");
      TwAddVarRW(bar, "workerthreads", TW_TYPE_INT32,   &tweak.threads,  ss.str().c_str());
      TwAddVarRW(bar, "workingset", TW_TYPE_INT32,   &tweak.workingSet,  " label='workingset' min=128");
      TwAddVarRW(bar, "sorted", TW_TYPE_BOOLCPP, &tweak.sorted, " label='min statechanges' ");
      TwAddVarRW(bar, "animation", TW_TYPE_BOOLCPP, &tweak.animation, " label='animation' ");
      TwAddVarRW(bar, "graph", TW_TYPE_BOOLCPP, &useRenderGraph, " label='relative time graph' ");
    }
#endif
    m_control.m_sceneOrbit =     nv_math::vec3f(cadscene.m_bbox.max+cadscene.m_bbox.min)*0.5f;
    m_control.m_sceneDimension = nv_math::length((cadscene.m_bbox.max-cadscene.m_bbox.min));
    m_control.m_viewMatrix =     nv_math::look_at(m_control.m_sceneOrbit - (-vec3(1,1,1)*m_control.m_sceneDimension*0.5f), m_control.m_sceneOrbit, vec3(0,1,0));

    shared.animUbo.sceneCenter      = m_control.m_sceneOrbit;
    shared.animUbo.sceneDimension   = m_control.m_sceneDimension * 0.2f;
    shared.animUbo.numMatrices      = uint(cadscene.m_matrices.size());
    shared.sceneUbo.wLightPos = (cadscene.m_bbox.max+cadscene.m_bbox.min)*0.5f + m_control.m_sceneDimension;
    shared.sceneUbo.wLightPos.w = 1.0;

    initRenderer(tweak.renderer, tweak.strategy, tweak.threads, tweak.sorted, tweak.percent);

    lastTweak = tweak;

    return validated;
  }

  void Sample::think(double time)
  {
    int width   = m_window.m_viewsize[0];
    int height  = m_window.m_viewsize[1];

    m_control.processActions(m_window.m_viewsize,
      nv_math::vec2f(m_window.m_mouseCurrent[0],m_window.m_mouseCurrent[1]),
      m_window.m_mouseButtonFlags, m_window.m_wheel);

    if (m_window.onPress(KEY_R)){
      ProgManager.reloadPrograms();
      resources->synchronize();
      resources->updatedPrograms( ProgManager );
      Renderer::getRegistry()[tweak.renderer]->updatedPrograms( ProgManager );
    }

    if (tweak.msaa != lastTweak.msaa){
      resources->initFramebuffer(width,height,tweak.msaa);
    }

    if (tweak.copies      != lastTweak.copies ||
        tweak.cloneaxisX  != lastTweak.cloneaxisX ||
        tweak.cloneaxisY  != lastTweak.cloneaxisY ||
        tweak.cloneaxisZ  != lastTweak.cloneaxisZ)
    {
      resources->synchronize();
      deinitRenderer();
      resources->deinitScene();
      initScene( filename.c_str(), tweak.copies-1, (tweak.cloneaxisX<<0) | (tweak.cloneaxisY<<1) | (tweak.cloneaxisZ<<2));
      resources->initScene(cadscene);
    }

    if (tweak.renderer != lastTweak.renderer ||
        tweak.strategy != lastTweak.strategy ||
        tweak.copies   != lastTweak.copies ||
        tweak.cloneaxisX  != lastTweak.cloneaxisX ||
        tweak.cloneaxisY  != lastTweak.cloneaxisY ||
        tweak.cloneaxisZ  != lastTweak.cloneaxisZ ||
        tweak.threads != lastTweak.threads ||
        tweak.sorted  != lastTweak.sorted ||
        tweak.percent != lastTweak.percent)
    {
      resources->synchronize();
      initRenderer(tweak.renderer,tweak.strategy, tweak.threads, tweak.sorted, tweak.percent);
    }

    resources->beginFrame();

    if (tweak.animation != lastTweak.animation){
      resources->synchronize();
      resources->animationReset();

      animBeginTime  = time;
    }

    {
      shared.width  = width;
      shared.height = height;

      SceneData& sceneUbo = shared.sceneUbo;

      sceneUbo.viewport = ivec2(width,height);

      nv_math::mat4 projection = resources->perspectiveProjection((45.f), float(width)/float(height), m_control.m_sceneDimension*0.001f, m_control.m_sceneDimension*10.0f);
      nv_math::mat4 view = m_control.m_viewMatrix;

      if (tweak.animation && tweak.animationSpin){
        double animTime = (time - animBeginTime) * 0.3 + nv_pi*0.2;
        vec3 dir = vec3(cos(animTime),1, sin(animTime));
        view = nv_math::look_at(m_control.m_sceneOrbit - (-dir*m_control.m_sceneDimension*0.5f), m_control.m_sceneOrbit, vec3(0,1,0));
      }

      sceneUbo.viewProjMatrix = projection * view;
      sceneUbo.viewMatrix = view;
      sceneUbo.viewMatrixIT = nv_math::transpose(nv_math::invert(view));

      sceneUbo.viewPos = -view.col(3);
      sceneUbo.viewDir = -view.row(2);

      sceneUbo.wLightPos = sceneUbo.viewMatrixIT.row(3);
      sceneUbo.wLightPos.w = 1.0;

      resources->m_workingSet = tweak.workingSet;
    }


    // We use the timer sections that force flushes, to get more accurate CPU costs
    // per section. In shipping application you would not want to do this

    if (tweak.animation)
    {
      NV_PROFILE_SECTION_EX("Anim.", resources->getTimerInterface(), true );

      AnimationData& animUbo = shared.animUbo;
      animUbo.time = float(time - animBeginTime);

      resources->animation(shared);
    }

    {
      NV_PROFILE_SECTION_EX("Render", resources->getTimerInterface(), true );
      renderer->draw(tweak.shade,resources,shared,m_profiler,ProgManager);
    }

    {
      NV_PROFILE_SECTION("Blit");
      renderer->blit(tweak.shade,resources,shared);
    }

    resources->endFrame();
    resources->m_frame++;

    glUseProgram(0);

    {
      NV_PROFILE_SECTION("UI");
      if (useRenderGraph){
        int avg = 50;

        if (lastFrameTime == 0){
          lastFrameTime = time;
          frames = -1;
        }

        if (frames > 4){
          double curavg = (time - lastFrameTime)  / frames;
          if (curavg > 1.0/30.0){
            avg = 10;
          }
        }

        if (m_profiler.getAveragedFrames() % avg == avg-1 ){
          m_profiler.getAveragedValues("Render",cpuTime, gpuTime);
          frameTime = (time - lastFrameTime) / frames;
          lastFrameTime = time;
          frames = -1;
        }

        frames++;

        float gpuTimeF = float(gpuTime);
        float cpuTimeF = float(cpuTime);
        float maxTimeF = std::max(std::max(cpuTimeF, gpuTimeF),0.0001f);

        glViewport(0,0,width,height);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(ProgManager.get(drawRectangleProgram));


        int   gapPixel = 20;
        float gap = float(gapPixel*2)/float(height);

        int   labelPixel = 160;
        float label      = float(labelPixel*2)/float(width);

        float barwidth   = 2.0 - label;

        glUniform4f(0, -1+label, -1 + gap*1, (gpuTimeF/maxTimeF)*barwidth, gap);
        glUniform4f(1, 118.0f/255.0f, 185.0f/255.0f, 0 ,1);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);

        glUniform4f(0, -1+label, -1, (cpuTimeF/maxTimeF)*barwidth, gap);
        glUniform4f(1, float(0x0F)/255.0f,float(0x7D)/255.0f,float(0xC2)/255.0f, 1.0f);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glUniform4f(0, -1, -1, label, gap*2);
        glUniform4f(1, 0,0,0, 1.f);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        glDisable(GL_BLEND);

        glUseProgram(0);
        glEnable(GL_DEPTH_TEST);

        text.changeCanvas(width,height);
        text.changeSize(width,height);
        textfps.changeCanvas(width,height);
        textfps.changeSize(width,height);

        text.BackupStates();

        float white[4] = {1,1,1,1};
        float black[4] = {0,0,0,1};
        std::string cputext = nv_helpers_gl::ProgramManager::format( "Scene CPU [ms]: %2.3f", cpuTimeF/1000.0f );
        std::string gputext = nv_helpers_gl::ProgramManager::format( "Scene GPU [ms]: %2.3f", gpuTimeF/1000.0f );
        std::string frametext = nv_helpers_gl::ProgramManager::format( "Frame [ms] %.1f", frameTime*1000.0f );

        textfps.beginString();
        textfps.drawString(24,7+gapPixel*2,frametext.c_str(),1, white);
        textfps.endString();
#if 1
        text.beginString();
        text.drawString(24,7,cputext.c_str(),1, white);
        text.drawString(24,7+gapPixel,gputext.c_str(),1, white);
        text.endString();
#endif
        text.RestoreStates();
      }

  #if HAS_TWEAKBAR
      if (useTweakBar){
        TwDraw();
      }
  #endif
    }

    lastTweak = tweak;
  }

  void Sample::resize(int width, int height)
  {
#if HAS_TWEAKBAR
    if (useTweakBar){
      TwWindowSize(width,height);
    }
#endif
    initFramebuffers(width,height);
  }

  void Sample::parse( int argc, const char**argv )
  {
    // check some filepaths
    std::string searchpaths[] = {
      "",
      sysExePath() + std::string(PROJECT_RELDIRECTORY) + "/",
      sysExePath() + std::string(PROJECT_RELDIRECTORY) + "/../csfviewer/",
    };
    
    for (size_t p = 0; p < sizeof(searchpaths)/sizeof(searchpaths[0]); p++){
      std::string check = searchpaths[p] + "geforce.csf.gz";
      FILE* testfile = NULL;
      testfile = fopen( check.c_str(), "rb" );
      if (testfile){
        fclose(testfile);
        filename = check;
        break;
      }
    }

    for (int i = 0; i < argc; i++){
      if (strstr(argv[i],".csf")){
        filename = std::string(argv[i]);
      }
      if (strcmp(argv[i],"-renderer")==0 && i+1<argc){
        tweak.renderer = atoi(argv[i+1]);
        i++;
      }
      if (strcmp(argv[i],"-strategy")==0 && i+1<argc){
        tweak.strategy = (Strategy)atoi(argv[i+1]);
        i++;
      }
      if (strcmp(argv[i],"-shademode")==0 && i+1<argc){
        tweak.shade = (ShadeType)atoi(argv[i+1]);
        i++;
      }
      if (strcmp(argv[i],"-workerthreads")==0 && i+1<argc){
        tweak.threads = atoi(argv[i+1]);
        i++;
      }
      if (strcmp(argv[i],"-sharedcontext")==0 && i+1<argc){
        useSharedContext = atoi(argv[i+1]) ? true : false;
      }
      if (strcmp(argv[i],"-copies")==0 && i+1<argc){
        tweak.copies = std::max(1,atoi(argv[i+1]));
        i++;
      }
      if (strcmp(argv[i],"-msaa")==0 && i+1<argc){
        tweak.msaa = std::min(8,std::max(0,atoi(argv[i+1])));
        i++;
      }
      if (strcmp(argv[i],"-tweakbar")==0 && i+1<argc){
        useTweakBar= atoi(argv[i+1]) ? true : false;
        i++;
      }
      if (strcmp(argv[i],"-rendergraph")==0 && i+1<argc){
        useRenderGraph= atoi(argv[i+1]) ? true : false;
        i++;
      }
      if (strcmp(argv[i],"-animation")==0 && i+1<argc){
        tweak.animation= atoi(argv[i+1]) ? true : false;
        i++;
      }
      if (strcmp(argv[i],"-animationspin")==0 && i+1<argc){
        tweak.animationSpin= atoi(argv[i+1]) ? true : false;
        i++;
      }
      if (strcmp(argv[i],"-minstatechanges")==0 && i+1<argc){
        tweak.sorted = atoi(argv[i+1]) ? true : false;
        i++;
      }
      if (strcmp(argv[i],"-workingset")==0 && i+1<argc){
        tweak.workingSet = std::max(128,atoi(argv[i+1]));
        i++;
      }
    }

    if (filename.empty())
    {
      fprintf(stderr,"no .csf file specified\n");
      exit(EXIT_FAILURE);
    }
  }

}

using namespace csfthreaded;

int sample_main(int argc, const char** argv)
{
#if defined(_WIN32) && defined(NDEBUG)
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
  Sample sample;
  sample.parse(argc,argv);

  return sample.run(
    PROJECT_NAME,
    argc, argv,
    SAMPLE_SIZE_WIDTH, SAMPLE_SIZE_HEIGHT,
    SAMPLE_MAJOR_VERSION, SAMPLE_MINOR_VERSION);
}

void sample_print(int level, const char * fmt)
{

}


