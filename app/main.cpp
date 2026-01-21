#include "core/Log.h"

#include "app/AppContext.h"
#include "app/Application.h"
#include "app/EngineContext.h"
#include "platform/GLFWWindow.h"
#include "core/Paths.h"
#include "editor/EditorLayer.h"

#include <filesystem>
#include <memory>

int main(int argc, char** argv) {
  Nyx::Paths::init((argc > 0 && argv && argv[0]) ? argv[0] : ".");
  Nyx::Log::Init();

  Nyx::WindowDesc desc{};
  desc.width = 1600;
  desc.height = 900;
  desc.title = "Nyx Engine";
  desc.vsync = true;

  std::filesystem::current_path("../..");
  auto window = std::make_unique<Nyx::GLFWWindow>(desc);
  auto app = std::make_unique<Nyx::AppContext>(std::move(window));
  auto engine = std::make_unique<Nyx::EngineContext>();

  Nyx::Application application(std::move(app), std::move(engine));
  return application.run();
}
