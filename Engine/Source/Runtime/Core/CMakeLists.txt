nyx_add_module(Core
  PRIVATE
    Error.cxx
    Fs.cxx
    Profiling.cxx
  PUBLIC_DEPS
    Nyx::Build
)

nyx_add_platform_sources(Core
  LINUX
    Linux/Memory.cxx
  WINDOWS
    Windows/Memory.cxx
  UNSUPPORTED
    Unsupported/Memory.cxx
)
