#include "PassPostFilters.h"
#include "render/rg/RenderGraph.h"
#include "app/EngineContext.h"

#include <glad/glad.h>
#include <vector>

namespace Nyx {

static bool isResampleFilter(uint32_t type) {
  switch (type) {
  case 6u:  // Sharpen
  case 14u: // Chromatic Aberration
  case 15u: // Lens Distortion
  case 16u: // Glitch
  case 17u: // Pixelate
  case 19u: // Blur
  case 20u: // Emboss
  case 21u: // Glow
  case 22u: // Bloom
  case 23u: // Tilt Shift
  case 25u: // Fisheye
  case 26u: // Swirl
  case 28u: // Pixel Sort
  case 29u: // Motion Tile
    return true;
  default:
    return false;
  }
}

PassPostFilters::~PassPostFilters() {
  if (m_prog) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassPostFilters::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("passes/post_filters.comp");
  NYX_ASSERT(m_prog != 0, "PassPostFilters: shader build failed");
}

void PassPostFilters::setup(RenderGraph &graph, const RenderPassContext &ctx,
                            const RenderableRegistry &registry,
                            EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "PostFilters",
      [&](RenderPassBuilder &b) {
        b.readTexture("Post.In", RenderAccess::SampledRead);
        b.readBuffer("Post.Filters", RenderAccess::SSBORead);
        b.readTexture("LDR.Color", RenderAccess::SampledRead);
        b.readTexture("LDR.Temp", RenderAccess::SampledRead);
        b.writeTexture("LDR.Color", RenderAccess::ImageWrite);
        b.writeTexture("LDR.Temp", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &in = tex(bb, rg, "Post.In");
        const auto &out = tex(bb, rg, "LDR.Color");
        const auto &tmp = tex(bb, rg, "LDR.Temp");
        const auto &filters = buf(bb, rg, "Post.Filters");

        NYX_ASSERT(in.tex != 0 && out.tex != 0 && tmp.tex != 0,
                   "PassPostFilters: missing textures");
        NYX_ASSERT(filters.buf != 0, "PassPostFilters: missing filter SSBO");

        glUseProgram(m_prog);

        // uniforms
        const GLint locTime = glGetUniformLocation(m_prog, "u_Time");
        const GLint locStart = glGetUniformLocation(m_prog, "u_StartIndex");
        const GLint locEnd = glGetUniformLocation(m_prog, "u_EndIndex");
        if (locTime >= 0)
          glUniform1f(locTime, engine.time());

        const uint32_t lutCount = engine.postLUTCount();
        const uint32_t maxLuts = 8;
        const uint32_t bindCount = (lutCount < maxLuts) ? lutCount : maxLuts;
        for (uint32_t i = 0; i < bindCount; ++i) {
          glBindTextureUnit(2 + i, engine.postLUTTexture(i));
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, filters.buf);

        const uint32_t gx = (ctx.fbWidth + 15u) / 16u;
        const uint32_t gy = (ctx.fbHeight + 15u) / 16u;

        const auto &nodes = engine.filterGraph().nodes();
        const uint32_t filterCount = (uint32_t)nodes.size();
        if (filterCount == 0u) {
          if (locStart >= 0)
            glUniform1ui(locStart, 0u);
          if (locEnd >= 0)
            glUniform1ui(locEnd, 0xFFFFFFFFu);
          glBindTextureUnit(0, in.tex);
          glBindImageTexture(1, out.tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                             GL_RGBA8);
          glDispatchCompute(gx, gy, 1);
          glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                          GL_TEXTURE_FETCH_BARRIER_BIT);
          return;
        }

        struct Segment {
          uint32_t start = 0;
          uint32_t end = 0;
        };
        std::vector<Segment> segments;
        segments.reserve(filterCount);

        bool rangeActive = false;
        uint32_t rangeStart = 0;
        uint32_t rangeEnd = 0;

        for (uint32_t i = 0u; i < filterCount; ++i) {
          if (!nodes[i].enabled)
            continue;

          if (isResampleFilter(nodes[i].type)) {
            if (rangeActive) {
              segments.push_back({rangeStart, rangeEnd});
              rangeActive = false;
            }
            segments.push_back({i, i});
          } else {
            if (!rangeActive) {
              rangeStart = i;
              rangeEnd = i;
              rangeActive = true;
            } else {
              rangeEnd = i;
            }
          }
        }

        if (rangeActive)
          segments.push_back({rangeStart, rangeEnd});

        if (segments.empty()) {
          if (locStart >= 0)
            glUniform1ui(locStart, 0u);
          if (locEnd >= 0)
            glUniform1ui(locEnd, 0xFFFFFFFFu);
          glBindTextureUnit(0, in.tex);
          glBindImageTexture(1, out.tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                             GL_RGBA8);
          glDispatchCompute(gx, gy, 1);
          glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                          GL_TEXTURE_FETCH_BARRIER_BIT);
          return;
        }

        bool writeToColor = (segments.size() % 2u) == 1u;
        const GLTexture2D *input = &in;
        const GLTexture2D *output = writeToColor ? &out : &tmp;

        for (const Segment &seg : segments) {
          if (locStart >= 0)
            glUniform1ui(locStart, seg.start);
          if (locEnd >= 0)
            glUniform1ui(locEnd, seg.end);

          glBindTextureUnit(0, input->tex);
          glBindImageTexture(1, output->tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                             GL_RGBA8);
          glDispatchCompute(gx, gy, 1);
          glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                          GL_TEXTURE_FETCH_BARRIER_BIT);

          input = output;
          output = (output == &out) ? &tmp : &out;
        }
      });
}

} // namespace Nyx
