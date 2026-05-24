cmake_minimum_required(VERSION 3.24)
project(rtengine VERSION 0.1.0 LANGUAGES C CXX)

# cmake 4 compatibility for pinned dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(RTENGINE_BUILD_RENDERER "Build OpenGL renderer" ON)
option(RTENGINE_BUILD_IMPORTERS "Build asset importers" ON)
option(RTENGINE_BUILD_PHYSICS "Build Jolt integration" ON)
option(RTENGINE_BUILD_DEMO "Build interactive demo" ON)
option(RTENGINE_BUILD_TESTS "Build tests" ON)
include(FetchContent)
include(GNUInstallDirs)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

add_library(rtengine_core src/scene.cpp src/bvh.cpp src/camera_controller.cpp)
add_library(rtengine::core ALIAS rtengine_core)
set_target_properties(rtengine_core PROPERTIES EXPORT_NAME core)
target_include_directories(rtengine_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(rtengine_core PUBLIC glm::glm)
target_compile_features(rtengine_core PUBLIC cxx_std_20)
set(rt_targets rtengine_core)

if(RTENGINE_BUILD_RENDERER)
  set(GLAD_PROFILE core CACHE STRING "" FORCE)
  set(GLAD_API "gl=4.1" CACHE STRING "" FORCE)
  set(GLAD_REPRODUCIBLE ON CACHE BOOL "" FORCE)
  set(GLAD_INSTALL ON CACHE BOOL "" FORCE)

  FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v0.1.36 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glad)
  file(READ shaders/common.glsl RT_COMMON)
  foreach(shader geometry.vert geometry.frag shadow.vert shadow.frag fullscreen.vert lighting.frag ray.frag temporal.frag filter.frag transparent.frag present.frag environment.frag)
    file(READ "shaders/${shader}" RT_SOURCE)
    string(REPLACE "#include \"common.glsl\"" "${RT_COMMON}" RT_SOURCE "${RT_SOURCE}")
    string(REPLACE "." "_" RT_NAME "${shader}")
    string(APPEND RT_SHADERS "inline constexpr const char* ${RT_NAME} = R\"RTGLSL(${RT_SOURCE})RTGLSL\";\n")
  endforeach()

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders.hpp "#pragma once\nnamespace rt::shaders {\n${RT_SHADERS}}\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/shaders/common.glsl)
  file(GLOB RT_SHADER_FILES CONFIGURE_DEPENDS shaders/*)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RT_SHADER_FILES})

  add_library(rtengine_renderer src/renderer.cpp)
  add_library(rtengine::renderer ALIAS rtengine_renderer)
  set_target_properties(rtengine_renderer PROPERTIES EXPORT_NAME renderer)
  target_include_directories(rtengine_renderer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_link_libraries(rtengine_renderer PUBLIC rtengine_core PRIVATE glad)
  list(APPEND rt_targets rtengine_renderer)
endif()

if(RTENGINE_BUILD_IMPORTERS)

  FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG f0569113c93ad095470c54bf34a17b36646bbbb5)
  FetchContent_Declare(cgltf GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git GIT_TAG v1.15 GIT_SHALLOW TRUE)
  FetchContent_Declare(tinyobj GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git GIT_TAG v2.0.0rc13 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(stb cgltf tinyobj)

  add_library(rtengine_importers src/assets.cpp)
  add_library(rtengine::importers ALIAS rtengine_importers)
  set_target_properties(rtengine_importers PROPERTIES EXPORT_NAME importers)
  target_include_directories(rtengine_importers PRIVATE ${stb_SOURCE_DIR} ${cgltf_SOURCE_DIR} ${tinyobj_SOURCE_DIR})
  target_link_libraries(rtengine_importers PUBLIC rtengine_core)
  target_compile_definitions(rtengine_importers PRIVATE GLM_ENABLE_EXPERIMENTAL)
  list(APPEND rt_targets rtengine_importers)
endif()

if(RTENGINE_BUILD_PHYSICS)
  set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
  set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
  set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
  set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
  set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
  set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
  set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
  set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
  set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
  set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
  set(ENABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
  foreach(isa USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
    set(${isa} OFF CACHE BOOL "Portable baseline" FORCE)
  endforeach()

  FetchContent_Declare(jolt GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git GIT_TAG v5.3.0 GIT_SHALLOW TRUE SOURCE_SUBDIR Build)
  FetchContent_MakeAvailable(jolt)

  add_library(rtengine_physics src/physics.cpp)
  add_library(rtengine::physics ALIAS rtengine_physics)
  set_target_properties(rtengine_physics PROPERTIES EXPORT_NAME physics)
  target_link_libraries(rtengine_physics PUBLIC rtengine_core PRIVATE Jolt)
  list(APPEND rt_targets rtengine_physics)
endif()

if(RTENGINE_BUILD_DEMO)

  if(NOT RTENGINE_BUILD_RENDERER OR NOT RTENGINE_BUILD_IMPORTERS OR NOT RTENGINE_BUILD_PHYSICS)
    message(FATAL_ERROR "Demo requires renderer, importers and physics; disable RTENGINE_BUILD_DEMO for a reduced build")
  endif()

  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw imgui)

  add_library(rt_demo_ui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(rt_demo_ui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(rt_demo_ui PRIVATE glfw)

  add_executable(rtengine_demo demo/main.cpp demo/camera_input.cpp)
  target_link_libraries(rtengine_demo PRIVATE rtengine_renderer rtengine_importers rtengine_physics glfw rt_demo_ui)

  if(WIN32)
    target_link_libraries(rtengine_demo PRIVATE Psapi)
  endif()

  target_compile_definitions(rtengine_demo PRIVATE RT_DEMO_ASSET_DIR="${PROJECT_SOURCE_DIR}/demo/assets")
endif()

if(RTENGINE_BUILD_TESTS)
  enable_testing()

  add_executable(rtengine_tests tests/tests.cpp)
  target_link_libraries(rtengine_tests PRIVATE rtengine_core)

  if(RTENGINE_BUILD_IMPORTERS)
    target_link_libraries(rtengine_tests PRIVATE rtengine_importers)
    target_compile_definitions(rtengine_tests PRIVATE RT_TEST_IMPORTERS RT_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/demo/assets")
  endif()

  if(RTENGINE_BUILD_PHYSICS)
    target_link_libraries(rtengine_tests PRIVATE rtengine_physics)
    target_compile_definitions(rtengine_tests PRIVATE RT_TEST_PHYSICS)
  endif()

  add_test(NAME rtengine_tests COMMAND rtengine_tests)

  if(RTENGINE_BUILD_DEMO)

    add_executable(rtengine_render_tests tests/render_tests.cpp)
    target_link_libraries(rtengine_render_tests PRIVATE rtengine_renderer glfw)
    add_test(NAME rtengine_render_tests COMMAND rtengine_render_tests)
    set_tests_properties(rtengine_render_tests PROPERTIES SKIP_RETURN_CODE 77)
  endif()

endif()

install(TARGETS ${rt_targets} EXPORT rtengineTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rtengineTargets NAMESPACE rtengine:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtengine)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/rtengineConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/rtengineConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtengine)

write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/rtengineConfigVersion.cmake VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rtengineConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/rtengineConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtengine)
install(FILES LICENSE THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/rtengine)
install(DIRECTORY third_party/licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/rtengine/licenses)
