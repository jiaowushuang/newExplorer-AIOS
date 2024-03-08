# The newExplorer-AIOS
#

cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# ---- Project ----

project(Playground LANGUAGES C)

# ---- Fetch CPM ----

#set(CPM_DOWNLOAD_VERSION 0.32.0) 
#set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
#if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  #message(STATUS "Downloading CPM.cmake v${CPM_DOWNLOAD_VERSION}")
  #file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
#endif()
#include(${CPM_DOWNLOAD_LOCATION})

# ---- Add dependencies via CPM ----
# See https://github.com/TheLartians/CPM.cmake for details and examples

# the ModernCppStarter defines an example project called 'Greeter'
# CPMAddPackage("gh:TheLartians/ModernCppStarter@0.17.2")

# ---- Create executable ----
SET(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_C_FLAGS_DEBUG "$ENV{CMAKE_CXX_FLAGS} -O0 -Wall -g")

# add your source files here

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c")
add_library(Playground_Lib ${headers} ${sources})
target_include_directories(
  Playground_Lib PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/Playground_Lib-${PROJECT_VERSION}>
)

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/standalone/source/*.c)

add_executable(Playground_Bin ${sources})
# link to your dependencies' targets here
target_link_libraries(Playground_Bin Playground_Lib)
# setup your target's properties
set_target_properties(Playground_Bin PROPERTIES C_STANDARD 11)
