cmake_minimum_required(VERSION 3.24)
project(rtengine VERSION 0.1.0 LANGUAGES C CXX)

# cmake 4 compatibility for pinned dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RTENGINE_BUILD_TESTS "Build tests" ON)
include(FetchContent)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLAD_PROFILE core CACHE STRING "" FORCE)
set(GLAD_API "gl=4.1" CACHE STRING "" FORCE)
set(GLAD_REPRODUCIBLE ON CACHE BOOL "" FORCE)

FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v0.1.36 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm glad)

add_library(rtengine_core src/scene.cpp src/bvh.cpp)
target_include_directories(rtengine_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(rtengine_core PUBLIC glm::glm)
target_compile_features(rtengine_core PUBLIC cxx_std_20)

file(READ shaders/common.glsl RT_COMMON)
foreach(shader geometry.vert geometry.frag shadow.vert shadow.frag fullscreen.vert lighting.frag ray.frag temporal.frag filter.frag transparent.frag present.frag environment.frag)
  file(READ "shaders/${shader}" RT_SOURCE)
  string(REPLACE "#include \"common.glsl\"" "${RT_COMMON}" RT_SOURCE "${RT_SOURCE}")
  string(REPLACE "." "_" RT_NAME "${shader}")
  string(APPEND RT_SHADERS "inline constexpr const char* ${RT_NAME} = R\"RTGLSL(${RT_SOURCE})RTGLSL\";\n")
endforeach()

file(WRITE ${CMAKE_BINARY_DIR}/shaders.hpp "#pragma once\nnamespace rt::shaders {\n${RT_SHADERS}}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/common.glsl)
file(GLOB RT_SHADER_FILES CONFIGURE_DEPENDS shaders/*)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RT_SHADER_FILES})

add_library(rtengine_renderer src/renderer.cpp)
target_include_directories(rtengine_renderer PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(rtengine_renderer PUBLIC rtengine_core PRIVATE glad)

if(RTENGINE_BUILD_TESTS)
  enable_testing()

  add_executable(rtengine_tests tests/tests.cpp)
  target_link_libraries(rtengine_tests PRIVATE rtengine_core)
  add_test(NAME rtengine_tests COMMAND rtengine_tests)
endif()
