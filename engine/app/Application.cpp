#include "Application.h"

#include "AppContext.h"
#include "EngineContext.h"

#include "core/Assert.h"

namespace Nyx {

Application::Application(std::unique_ptr<AppContext> app,
                         std::unique_ptr<EngineContext> engine)
    : m_app(std::move(app)), m_engine(std::move(engine)) {
  NYX_ASSERT(m_app != nullptr, "Application requires AppContext");
  NYX_ASSERT(m_engine != nullptr, "Application requires EngineContext");
  setupKeybinds();
}

Application::~Application() = default;

} // namespace Nyx
