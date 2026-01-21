#pragma once

#include "render/rg/RGResources.h"
#include <functional>
#include <string>
#include <vector>

namespace Nyx {

struct RGPass {
  std::string name;
  std::function<void(RGResources &)> exec;
};

class RenderGraph final {
public:
  void reset() { m_passes.clear(); }

  void addPass(std::string name, std::function<void(RGResources &)> fn) {
    m_passes.push_back(RGPass{std::move(name), std::move(fn)});
  }

  void execute(RGResources &r) {
    for (auto &p : m_passes) {
      p.exec(r);
    }
  }

private:
  std::vector<RGPass> m_passes;
};

} // namespace Nyx
