cmake_minimum_required(VERSION 3.5)
get_filename_component(PROJNAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
Project(${PROJNAME})
Message(STATUS "-------------------------------")
Message(STATUS "Processing Project ${PROJNAME}:")

#####################################################################################
# look for shared_sources 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find setup.cmake yet
#
if(NOT BASE_DIRECTORY)

  find_path(BASE_DIRECTORY
    NAMES shared_sources/cmake/setup.cmake
    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../.. 
    REQUIRED
    DOC "Directory containing shared_sources"
    )
endif()
if(EXISTS ${BASE_DIRECTORY}/shared_sources/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/shared_sources/cmake/setup.cmake)
  include(${BASE_DIRECTORY}/shared_sources/cmake/utilities.cmake)
else()
  message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing shared_sources")
endif()
    
_add_project_definitions(${PROJNAME})

set( BUILD_${PROJNAME}_VULKAN_ONLY FALSE CACHE BOOL "Avoids OpenGL in samples that support dual use" )

#--------------------------------------------------------------------------------------------------
# Resources
#
download_files(FILENAMES geforce.csf.gz)

#####################################################################################
# additions from packages needed for this sample
# add refs  in LIBRARIES_OPTIMIZED
# add refs  in LIBRARIES_DEBUG
# add files in PACKAGE_SOURCE_FILES
#
set( VK_EXENAME "vk_threaded_cadscene" )
set( GL_VK_EXENAME "gl_vk_threaded_cadscene" )

if(NOT BUILD_${PROJNAME}_VULKAN_ONLY)
  _add_package_OpenGL()
endif()
_add_package_VulkanSDK()
_add_package_ShaderC()
_add_package_ZLIB()
_add_package_ImGUI()

#####################################################################################
# process the rest of some cmake code that needs to be done *after* the packages add
_add_shared_sources_lib()

add_definitions(-DCSF_ZIP_SUPPORT=1)

#####################################################################################
# Source files for this project
#
file(GLOB GL_VK_SOURCE_FILES *.cpp *.hpp *.inl *.h *.c)
file(GLOB VK_SOURCE_FILES *.cpp *.hpp *.inl *.h *.c)
file(GLOB GLSL_FILES *.glsl)

list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/cadscene_gl.cpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/cadscene_gl.hpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources_gl.cpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources_gl.hpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/renderer_gl.cpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/renderer_glcmd.cpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/rendererthread_glcmd.cpp)



#####################################################################################
# Executable
#
if(WIN32 AND NOT GLUT_FOUND)
  add_definitions(/wd4267) #remove size_t to int warning
  add_definitions(/wd4996) #remove printf warning
  add_definitions(/wd4244) #remove double to float conversion warning
  add_definitions(/wd4305) #remove double to float truncation warning
else()
  add_definitions(-fpermissive)
endif()

add_executable(${VK_EXENAME} ${VK_SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PACKAGE_SOURCE_FILES} ${GLSL_FILES})

if(NOT BUILD_${PROJNAME}_VULKAN_ONLY)
  add_executable(${GL_VK_EXENAME} ${GL_VK_SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PACKAGE_SOURCE_FILES} ${GLSL_FILES})
  target_compile_definitions(${GL_VK_EXENAME} PRIVATE -DHAS_OPENGL=1)
endif()

#####################################################################################
# common source code needed for this sample
#
source_group(common FILES 
  ${COMMON_SOURCE_FILES}
  ${PACKAGE_SOURCE_FILES}
)
source_group(shaders FILES 
  ${GLSL_FILES}
)
source_group("Source Files" FILES ${GL_VK_SOURCE_FILES})

if(UNIX)
  set(UNIXLINKLIBS dl pthread)
else()
  set(UNIXLINKLIBS)
endif()

#####################################################################################
# Linkage
#
target_link_libraries(${VK_EXENAME} ${PLATFORM_LIBRARIES} shared_sources)

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
  target_link_libraries(${VK_EXENAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)

foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
  target_link_libraries(${VK_EXENAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)

if(NOT BUILD_${PROJNAME}_VULKAN_ONLY)
  target_link_libraries(${GL_VK_EXENAME} ${PLATFORM_LIBRARIES} shared_sources)

  foreach(DEBUGLIB ${LIBRARIES_DEBUG})
    target_link_libraries(${GL_VK_EXENAME} debug ${DEBUGLIB})
  endforeach(DEBUGLIB)

  foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
    target_link_libraries(${GL_VK_EXENAME} optimized ${RELEASELIB})
  endforeach(RELEASELIB)
endif()

#####################################################################################
# copies binaries that need to be put next to the exe files (ZLib, etc.)
#

_finalize_target( ${VK_EXENAME} )
if(NOT BUILD_${PROJNAME}_VULKAN_ONLY)
  _finalize_target( ${GL_VK_EXENAME} )
endif()

LIST(APPEND GLSL_FILES "common.h")
install(FILES ${GLSL_FILES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/GLSL_${PROJNAME}")
install(FILES ${GLSL_FILES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/GLSL_${PROJNAME}")

