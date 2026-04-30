# rtengine

`rtengine` is a C++20 OpenGL 4.1 engine with PBR, ray tracing, asset importing, and physics.

## Setup

Requirements: CMake 3.24+, OpenGL 4.1

```sh
cmake -S . -B build
cmake --build build
./build/rtengine_demo
```

Dependencies are fetched automatically with CMake FetchContent.
