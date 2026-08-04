<h1 align="center">
  Nyx Engine
</h1>
<p align="center">
  Nyx Engine is a modular game engine written in C++26 and modern Vulkan.
</p>

---
## Design
What does **modular** stand for?

Adopting C++20 macros was one of the first and primary design choices for Nyx Engine. Each directory under `Engine/Source/Runtime/` represents an individual engine module, all of which follow strong compilation order (see [[RuntimeREADME]] for more details).

Each module is split into visibility layers:
- **Public** visibility layer contains source files that are available to all dependent modules, they usually export declarations.
- **Internal** visibility layer contains source files that are available only to the scope of the module, and cannot be exported globally. They also cannot be imported into public sources. They usually export declarations or provide utilities for module implementation units.
- **Private** visibility layer contains implementation units of the module's declarations.
> [!Interface modules]
> A module with only **Public** visibility layer referred to as interface module.

This model is heavily inspired by [Unreal Engine](https://www.unrealengine.com/). Style guidelines are centered around using all the new features of latest C++ standards, algorithms, facilities, etc.

---
## Requirements
- **OS:** Linux
  Other OS currently not supported. Developed and tested on Arch Linux
- **Compiler:** Clang with C++26 support
  Developed and tested with an experimental Bloomberg's Clang fork and custom std headers
- **Graphics:** Vulkan
  Required support of dynamic rendering and synchronization2 features by the driver
- **Build system:** CMake + Ninja
  Makefile is an orchestrator over common commands and can be avoided
---
## Dependencies
Nyx Engine vendors vcpkg, everything else is managed through it. See [[vcpkg.json]]:

- **Graphics:** [Vulkan](https://vulkan.org), [vulkan-memory-allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (VMA), [volk](https://github.com/zeux/volk), [vulkan-loader](https://github.com/KhronosGroup/Vulkan-Loader) (Wayland support), [SDL3](https://wiki.libsdl.org/SDL3/FrontPage)
- **Testing:** [catch2](https://github.com/catchorg/Catch2)
- **Math:** [glm](https://github.com/g-truc/glm)
- **Utility:** [spdlog](https://github.com/gabime/spdlog), [platform-folders](https://github.com/sago007/PlatformFolders)
- **Scripting:** [python3](https://www.python.org/) (hosted)
- **Shaders:** [shaderc](https://github.com/google/shaderc) (glsl), [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) (wgsl)

This project is created for learning purposes, and it's planned to minimize the amount of dependencies to as low as possible, implementing the functionality from scratch, where reasonable.
