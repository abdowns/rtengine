cmake_minimum_required(VERSION 3.24)
project(rtengine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RTENGINE_BUILD_TESTS "Build tests" ON)
include(FetchContent)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

add_library(rtengine_core src/scene.cpp src/bvh.cpp)
target_include_directories(rtengine_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(rtengine_core PUBLIC glm::glm)
target_compile_features(rtengine_core PUBLIC cxx_std_20)

if(RTENGINE_BUILD_TESTS)
  enable_testing()

  add_executable(rtengine_tests tests/tests.cpp)
  target_link_libraries(rtengine_tests PRIVATE rtengine_core)
  add_test(NAME rtengine_tests COMMAND rtengine_tests)
endif()
