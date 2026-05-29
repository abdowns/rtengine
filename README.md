# rtengine

`rtengine` is a lightweight C++20 OpenGL 4.1 engine with PBR, real time ray tracing, global illumination, asset importing, rigid body physics, and an interactive demo.

## Setup

Requirements: CMake 3.24+, OpenGL 4.1

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/rtengine_demo
```

On Windows, run `build/Release/rtengine_demo.exe`. On headless Linux, run tests with `xvfb-run -a ctest --test-dir build --output-on-failure`

To use the library from CMake:

```cmake
add_subdirectory(path/to/rtengine)
target_link_libraries(my_app PRIVATE rtengine::renderer rtengine::importers rtengine::physics)
```

Available targets are `rtengine::core`, `rtengine::renderer`, `rtengine::importers`, and `rtengine::physics`. Disable unneeded modules with `RTENGINE_BUILD_RENDERER`, `RTENGINE_BUILD_IMPORTERS`, `RTENGINE_BUILD_PHYSICS`, `RTENGINE_BUILD_DEMO`, and `RTENGINE_BUILD_TESTS`.

## Resources

The rendering approach is based on [LearnOpenGL](https://learnopengl.com/). Pinned dependencies: GLFW, GLAD, GLM, stb_image, cgltf, tinyobjloader, Jolt, and Dear ImGui for the demo. License notices and LearnOpenGL attribution are in [THIRD_PARTY.md](THIRD_PARTY.md)
