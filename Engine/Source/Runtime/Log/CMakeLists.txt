nyx_require_package(spdlog
  TARGETS
    spdlog::spdlog
  CONFIG
)

nyx_add_module(Log
  PUBLIC_DEPS
    Miracle::Miracle
    spdlog::spdlog
)
