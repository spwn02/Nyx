nyx_require_package(SDL3
  TARGETS
    SDL3::SDL3
)

nyx_add_module(Platform
  PUBLIC_DEPS
    Nyx::Core
    Nyx::Event
    SDL3::SDL3
)
