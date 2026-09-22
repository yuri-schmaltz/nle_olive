#include <cmath>
#include "testutil.h"
#include "render/opengl/openglrenderer.h"
#include "node/generator/solid/solid.h"
#include "node/generator/testsignal/testsignal.h"
#include "node/math/merge/merge.h"
#include "node/color/colorwheels/colorwheels.h"
namespace olive {
OLIVE_ADD_TEST(OpenGLColorWheelsPixelsAndAlpha)
{
  OpenGLRenderer renderer;
  OLIVE_ASSERT(renderer.Init());
  renderer.PostInit();
  VideoParams params(8, 8, PixelFormat::F32, 4);
  auto input = renderer.CreateTexture(params);
  auto output = renderer.CreateTexture(params);
  ColorWheelsNode wheels;
  auto shader = renderer.CreateNativeShader(wheels.GetShaderCode(Node::ShaderRequest(QString())));
  OLIVE_ASSERT(!shader.isNull());
  for (bool graded : {false, true}) {
    for (float alpha : {1.0f, 0.5f, 0.0f}) {
      renderer.ClearDestination(input.get(), 0.25f * alpha, 0.5f * alpha, 0.75f * alpha, alpha);
      NodeValueRow row;
      row.insert(ColorWheelsNode::kTextureInput, NodeValue(NodeValue::kTexture, input));
      row.insert(ColorWheelsNode::kLiftInput, NodeValue(NodeValue::kVec3, QVariant::fromValue(QVector3D(graded ? 0.1f : 0.0f, 0, 0))));
      row.insert(ColorWheelsNode::kGammaInput, NodeValue(NodeValue::kVec3, QVariant::fromValue(QVector3D(1, graded ? 2.0f : 1.0f, 1))));
      row.insert(ColorWheelsNode::kGainInput, NodeValue(NodeValue::kVec3, QVariant::fromValue(QVector3D(1, 1, graded ? 0.5f : 1.0f))));
      row.insert(ColorWheelsNode::kOffsetInput, NodeValue(NodeValue::kVec3, QVariant::fromValue(QVector3D(0, 0, graded ? 0.125f : 0.0f))));
      renderer.BlitToTexture(shader, ShaderJob(row), output.get());
      float pixels[8 * 8 * 4];
      output->Download(pixels, 8);
      const float expected[] = {graded ? 0.325f : 0.25f,
                                graded ? std::sqrt(0.5f) : 0.5f,
                                graded ? 0.5f : 0.75f};
      for (int i = 0; i < 64; ++i) {
        for (int c = 0; c < 3; ++c) {
          OLIVE_ASSERT(std::fabs(pixels[i * 4 + c] - expected[c] * alpha) < 1e-5f);
        }
        OLIVE_ASSERT(std::fabs(pixels[i * 4 + 3] - alpha) < 1e-6f);
      }
    }
  }
  renderer.DestroyNativeShader(shader);
  OLIVE_TEST_END;
}

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
