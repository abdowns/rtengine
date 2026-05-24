# rtengine

`rtengine` is a lightweight C++20 OpenGL 4.1 engine with PBR, real time ray tracing, global illumination, asset importing, rigid body physics, and an interactive demo.

## Setup

Requirements: CMake 3.24+, OpenGL 4.1

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/rtengine_demo
```

On Windows, run `build/Release/rtengine_demo.exe`.

To use the library from CMake:

```cmake
add_subdirectory(path/to/rtengine)
target_link_libraries(my_app PRIVATE rtengine::rtengine)
```

## Resources

Pinned dependencies: GLFW, GLAD, GLM, stb_image, cgltf, tinyobjloader, Jolt, and Dear ImGui for the demo. License notices are in [THIRD_PARTY.md](THIRD_PARTY.md)
