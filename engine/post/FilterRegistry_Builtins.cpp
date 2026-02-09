#include "FilterRegistry.h"

#include "core/Assert.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nyx {

static std::string toLowerBuiltins(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s)
    out.push_back((char)std::tolower((unsigned char)c));
  return out;
}

static void uniquePushBuiltins(std::vector<std::string> &v, std::string s) {
  for (auto &x : v) {
    if (x == s)
      return;
  }
  v.push_back(std::move(s));
}

static FilterTypeId nextId(uint32_t &counter) {
  return static_cast<FilterTypeId>(counter++);
}

static constexpr uint32_t kMaxParams = FilterNode::kMaxParams;

struct ParamSpec final {
  const char *name = "";
  float def = 0.0f;
  float minv = 0.0f;
  float maxv = 1.0f;
  float step = 0.01f;
  FilterParamUI ui = FilterParamUI::Slider;
};

static void fillDefaults(FilterTypeInfo &t, std::initializer_list<ParamSpec> specs) {
  NYX_ASSERT(specs.size() <= kMaxParams, "Too many params in filter");
  t.paramCount = (uint32_t)specs.size();

  uint32_t i = 0;
  for (const ParamSpec &p : specs) {
    t.params[i].name = p.name;
    t.params[i].defaultValue = p.def;
    t.params[i].minValue = p.minv;
    t.params[i].maxValue = p.maxv;
    t.params[i].step = p.step;
    t.params[i].ui = p.ui;
    ++i;
  }

  for (; i < kMaxParams; ++i)
    t.params[i] = FilterParamDesc{};
}

static void makeKeywords(FilterTypeInfo &t) {
  t.keywords.clear();
  uniquePushBuiltins(t.keywords, toLowerBuiltins(t.name));
  uniquePushBuiltins(t.keywords, toLowerBuiltins(t.category));

  for (uint32_t i = 0; i < t.paramCount; ++i) {
    if (t.params[i].name && t.params[i].name[0])
      uniquePushBuiltins(t.keywords, toLowerBuiltins(t.params[i].name));
  }
  for (const std::string &a : t.aliases)
    uniquePushBuiltins(t.keywords, toLowerBuiltins(a));
}

void FilterRegistry::registerBuiltins() {
  clear();

  uint32_t idCounter = 1;
  auto bindType = [this](FilterTypeInfo t) {
    NYX_ASSERT(t.name[0] != 0, "Filter type missing name");
    NYX_ASSERT(t.category[0] != 0, "Filter type missing category");

    makeKeywords(t);
    m_types.push_back(std::move(t));
  };

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Exposure";
    t.category = "Tone";
    t.defaultLabel = "Exposure";
    t.aliases = {"EV", "Exposure EV", "ExposureComp"};

    fillDefaults(t, {
      ParamSpec{"EV", 0.0f, -10.0f, 10.0f, 0.05f, FilterParamUI::Drag},
    });

    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Contrast";
    t.category = "Color";
    t.defaultLabel = "Contrast";

    fillDefaults(t, {
      ParamSpec{"Amount", 1.0f, 0.0f, 2.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Saturation";
    t.category = "Color";
    t.defaultLabel = "Saturation";

    fillDefaults(t, {
      ParamSpec{"Amount", 1.0f, 0.0f, 2.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Gamma";
    t.category = "Color";
    t.defaultLabel = "Gamma";

    fillDefaults(t, {
      ParamSpec{"Gamma", 1.0f, 0.1f, 3.0f, 0.01f, FilterParamUI::Drag},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Vignette";
    t.category = "Lens";
    t.defaultLabel = "Vignette";

    fillDefaults(t, {
      ParamSpec{"Strength", 0.25f, 0.0f, 2.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Radius",   0.75f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Softness", 0.35f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });

    t.gpuParamCount = 3;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Sharpen";
    t.category = "Lens";
    t.defaultLabel = "Sharpen";

    fillDefaults(t, {
      ParamSpec{"Amount", 0.0f, 0.0f, 2.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Radius", 1.0f, 0.5f, 3.0f, 0.01f, FilterParamUI::Drag},
    });

    t.gpuParamCount = 2;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Invert";
    t.category = "Utility";
    t.defaultLabel = "Invert";
    t.aliases = {"InvertColor"};

    fillDefaults(t, {
      ParamSpec{"Enabled", 1.0f, 0.0f, 1.0f, 1.0f, FilterParamUI::Checkbox},
    });

    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Grayscale";
    t.category = "Utility";
    t.defaultLabel = "Grayscale";

    fillDefaults(t, {
      ParamSpec{"Amount", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });

    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Brightness";
    t.category = "Color";
    t.defaultLabel = "Brightness";

    fillDefaults(t, {
      ParamSpec{"Amount", 0.0f, -1.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Hue";
    t.category = "Color";
    t.defaultLabel = "Hue";

    fillDefaults(t, {
      ParamSpec{"Degrees", 0.0f, -180.0f, 180.0f, 1.0f, FilterParamUI::Drag},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Tint";
    t.category = "Color";
    t.defaultLabel = "Tint";

    fillDefaults(t, {
      ParamSpec{"Strength", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Color", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
      ParamSpec{"", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
      ParamSpec{"", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
    });
    t.gpuParamCount = 4;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Sepia";
    t.category = "Color";
    t.defaultLabel = "Sepia";

    fillDefaults(t, {
      ParamSpec{"Amount", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "LUT";
    t.category = "Color";
    t.defaultLabel = "LUT";

    fillDefaults(t, {
      ParamSpec{"Intensity", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"LUT Index", 0.0f, 0.0f, 7.0f, 1.0f, FilterParamUI::Drag},
    });
    t.gpuParamCount = 2;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Chromatic Aberration";
    t.category = "Lens";
    t.defaultLabel = "Chromatic Aberration";

    fillDefaults(t, {
      ParamSpec{"Amount", 0.002f, 0.0f, 0.05f, 0.0005f, FilterParamUI::Drag},
      ParamSpec{"Dispersion", 1.0f, 0.0f, 3.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Radius", 0.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Angle", 0.0f, -3.14f, 3.14f, 0.01f, FilterParamUI::Drag},
    });
    t.gpuParamCount = 4;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Lens Distortion";
    t.category = "Lens";
    t.defaultLabel = "Lens Distortion";

    fillDefaults(t, {
      ParamSpec{"Strength", 0.0f, -1.5f, 1.5f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Strength2", 0.0f, -2.0f, 2.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Zoom", 1.0f, 0.5f, 1.5f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Chromatic", 0.002f, 0.0f, 0.05f, 0.0005f,
                FilterParamUI::Drag},
      ParamSpec{"Center X", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Center Y", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 6;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Glitch";
    t.category = "Stylize";
    t.defaultLabel = "Glitch";

    fillDefaults(t, {
      ParamSpec{"Amount", 0.25f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"BlockSize", 32.0f, 2.0f, 128.0f, 1.0f, FilterParamUI::Drag},
      ParamSpec{"Speed", 1.0f, 0.0f, 5.0f, 0.05f, FilterParamUI::Slider},
      ParamSpec{"Mode", 0.0f, 0.0f, 2.0f, 1.0f, FilterParamUI::Drag},
      ParamSpec{"Scanline", 0.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Jitter", 0.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 6;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Pixelate";
    t.category = "Stylize";
    t.defaultLabel = "Pixelate";

    fillDefaults(t, {
      ParamSpec{"Size", 8.0f, 1.0f, 256.0f, 1.0f, FilterParamUI::Drag},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Noise";
    t.category = "Stylize";
    t.defaultLabel = "Noise";

    fillDefaults(t, {
      ParamSpec{"Amount", 0.05f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Color", 0.0f, 0.0f, 1.0f, 1.0f, FilterParamUI::Checkbox},
    });
    t.gpuParamCount = 2;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Blur";
    t.category = "Stylize";
    t.defaultLabel = "Blur";

    fillDefaults(t, {
      ParamSpec{"Radius", 1.0f, 0.0f, 6.0f, 0.05f, FilterParamUI::Drag},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Emboss";
    t.category = "Stylize";
    t.defaultLabel = "Emboss";

    fillDefaults(t, {
      ParamSpec{"Amount", 1.0f, 0.0f, 2.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 1;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Glow";
    t.category = "Stylize";
    t.defaultLabel = "Glow";

    fillDefaults(t, {
      ParamSpec{"Strength", 0.5f, 0.0f, 3.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Radius", 2.0f, 0.0f, 12.0f, 0.05f, FilterParamUI::Drag},
      ParamSpec{"Threshold", 0.0f, 0.0f, 2.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Tint", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
      ParamSpec{"", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
      ParamSpec{"", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
    });
    t.gpuParamCount = 6;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Bloom";
    t.category = "Stylize";
    t.defaultLabel = "Bloom";

    fillDefaults(t, {
      ParamSpec{"Strength", 0.6f, 0.0f, 3.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Threshold", 0.8f, 0.0f, 3.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Soft Knee", 0.3f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Radius", 2.5f, 0.0f, 12.0f, 0.05f, FilterParamUI::Drag},
      ParamSpec{"Tint", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
      ParamSpec{"", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
      ParamSpec{"", 1.0f, 0.0f, 1.0f, 0.01f, FilterParamUI::Color3},
    });
    t.gpuParamCount = 7;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Tilt Shift";
    t.category = "Stylize";
    t.defaultLabel = "Tilt Shift";

    fillDefaults(t, {
      ParamSpec{"Center", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Range", 0.2f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Radius", 3.0f, 0.0f, 12.0f, 0.05f, FilterParamUI::Drag},
      ParamSpec{"Angle", 0.0f, -3.14f, 3.14f, 0.01f, FilterParamUI::Drag},
      ParamSpec{"Falloff", 1.0f, 0.1f, 4.0f, 0.05f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 5;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Film Grain";
    t.category = "Stylize";
    t.defaultLabel = "Film Grain";

    fillDefaults(t, {
      ParamSpec{"Amount", 0.06f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Color", 0.0f, 0.0f, 1.0f, 1.0f, FilterParamUI::Checkbox},
      ParamSpec{"Size", 1.0f, 0.1f, 4.0f, 0.05f, FilterParamUI::Slider},
      ParamSpec{"Speed", 1.0f, 0.0f, 4.0f, 0.05f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 4;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Fisheye";
    t.category = "Lens";
    t.defaultLabel = "Fisheye";

    fillDefaults(t, {
      ParamSpec{"Strength", 0.25f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Zoom", 1.0f, 0.5f, 1.5f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Chromatic", 0.0f, 0.0f, 0.05f, 0.0005f,
                FilterParamUI::Drag},
    });
    t.gpuParamCount = 3;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Swirl";
    t.category = "Stylize";
    t.defaultLabel = "Swirl";

    fillDefaults(t, {
      ParamSpec{"Angle", 1.0f, -6.28f, 6.28f, 0.01f, FilterParamUI::Drag},
      ParamSpec{"Radius", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Center X", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Center Y", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
    });
    t.gpuParamCount = 4;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Halftone";
    t.category = "Stylize";
    t.defaultLabel = "Halftone";

    fillDefaults(t, {
      ParamSpec{"Scale", 120.0f, 10.0f, 400.0f, 1.0f, FilterParamUI::Drag},
      ParamSpec{"Intensity", 0.8f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Angle", 0.0f, -3.14f, 3.14f, 0.01f, FilterParamUI::Drag},
      ParamSpec{"Invert", 0.0f, 0.0f, 1.0f, 1.0f, FilterParamUI::Checkbox},
    });
    t.gpuParamCount = 4;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Pixel Sort";
    t.category = "Stylize";
    t.defaultLabel = "Pixel Sort";

    fillDefaults(t, {
      ParamSpec{"Threshold", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Strength", 0.5f, 0.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Direction", 0.0f, -1.0f, 1.0f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"BlockSize", 64.0f, 4.0f, 512.0f, 1.0f,
                FilterParamUI::Drag},
    });
    t.gpuParamCount = 4;
    bindType(std::move(t));
  }

  {
    FilterTypeInfo t{};
    t.id = nextId(idCounter);
    t.name = "Motion Tile";
    t.category = "Stylize";
    t.defaultLabel = "Motion Tile";

    fillDefaults(t, {
      ParamSpec{"Expand X %", 0.0f, -100.0f, 200.0f, 0.1f, FilterParamUI::Drag},
      ParamSpec{"Expand Y %", 0.0f, -100.0f, 200.0f, 0.1f, FilterParamUI::Drag},
      ParamSpec{"Wrap Mode", 0.0f, 0.0f, 2.0f, 1.0f, FilterParamUI::Drag},
      ParamSpec{"Resize", 0.0f, 0.0f, 1.0f, 1.0f, FilterParamUI::Checkbox},
      ParamSpec{"Spacing", 0.0f, 0.0f, 0.45f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Roundness", 0.0f, 0.0f, 0.5f, 0.01f, FilterParamUI::Slider},
      ParamSpec{"Trail Strength", 0.0f, 0.0f, 1.0f, 0.01f,
                FilterParamUI::Slider},
      ParamSpec{"Trail Count", 4.0f, 1.0f, 16.0f, 1.0f,
                FilterParamUI::Drag},
      ParamSpec{"Trail Angle", 0.0f, -3.14f, 3.14f, 0.01f,
                FilterParamUI::Drag},
      ParamSpec{"Trail Distance", 0.02f, 0.0f, 0.2f, 0.005f,
                FilterParamUI::Drag},
    });
    t.gpuParamCount = 10;
    bindType(std::move(t));
  }

  finalize();
}

} // namespace Nyx
