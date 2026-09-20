#include <cmath>
#include "testutil.h"
#include "render/opengl/openglrenderer.h"
#include "node/generator/solid/solid.h"
#include "node/generator/testsignal/testsignal.h"
#include "node/math/merge/merge.h"
namespace olive {
OLIVE_ADD_TEST(OpenGLSolidAndAlphaOver)
{
  OpenGLRenderer renderer;
  OLIVE_ASSERT(renderer.Init());
  renderer.PostInit();
  VideoParams params(8, 8, PixelFormat::F32, 4);
  auto base = renderer.CreateTexture(params);
  auto blend = renderer.CreateTexture(params);
  auto result = renderer.CreateTexture(params);
  renderer.ClearDestination(base.get(), 0.0, 0.0, 1.0, 1.0);
  renderer.ClearDestination(blend.get(), 0.5, 0.0, 0.0, 0.5);
  MergeNode merge;
  auto shader = renderer.CreateNativeShader(merge.GetShaderCode(Node::ShaderRequest(QString())));
  OLIVE_ASSERT(!shader.isNull());
  NodeValueRow row;
  row.insert(MergeNode::kBaseIn, NodeValue(NodeValue::kTexture, base));
  row.insert(MergeNode::kBlendIn, NodeValue(NodeValue::kTexture, blend));
  renderer.BlitToTexture(shader, ShaderJob(row), result.get());
  float pixels[8 * 8 * 4];
  result->Download(pixels, 8);
  for (int i = 0; i < 64; i++) {
    OLIVE_ASSERT(std::fabs(pixels[i*4] - 0.5f) < 1e-5f);
    OLIVE_ASSERT(std::fabs(pixels[i*4+1]) < 1e-5f);
    OLIVE_ASSERT(std::fabs(pixels[i*4+2] - 0.5f) < 1e-5f);
    OLIVE_ASSERT(std::fabs(pixels[i*4+3] - 1.0f) < 1e-5f);
  }
  renderer.DestroyNativeShader(shader);
  OLIVE_TEST_END;
}
OLIVE_ADD_TEST(OpenGLColorBars)
{
  OpenGLRenderer renderer;
  OLIVE_ASSERT(renderer.Init());
  renderer.PostInit();
  VideoParams params(8, 2, PixelFormat::F32, 4);
  auto result = renderer.CreateTexture(params);
  BarsGenerator bars;
  auto shader = renderer.CreateNativeShader(bars.GetShaderCode(Node::ShaderRequest(QString())));
  OLIVE_ASSERT(!shader.isNull());
  renderer.BlitToTexture(shader, ShaderJob(), result.get());
  float pixels[8 * 2 * 4];
  result->Download(pixels, 8);
  const float expected[8][3] = {{1,1,1},{1,1,0},{0,1,1},{0,1,0},
                               {1,0,1},{1,0,0},{0,0,1},{0,0,0}};
  for (int x = 0; x < 8; x++) {
    for (int c = 0; c < 3; c++) OLIVE_ASSERT(std::fabs(pixels[x*4+c] - expected[x][c]) < 1e-5f);
    OLIVE_ASSERT(pixels[x*4+3] == 1.0f);
  }
  renderer.DestroyNativeShader(shader);
  OLIVE_TEST_END;
}
}
