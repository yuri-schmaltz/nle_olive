/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "testutil.h"
#include <cmath>
#include "render/colorprocessor.h"

#include "node/distort/crop/cropdistortnode.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/generator/solid/solid.h"
#include "node/math/merge/merge.h"
#include "node/project.h"
#include "render/rendermanager.h"

namespace olive {
OLIVE_ADD_TEST(ColorDisplayRoundTrip)
{
  ColorManager::SetUpDefaultConfig();
  Project project;
  auto *manager = project.color_manager();
  const ColorTransform transform(manager->GetDefaultDisplay(),
                                 manager->GetDefaultView(manager->GetDefaultDisplay()), QString());
  auto forward = ColorProcessor::Create(manager, manager->GetReferenceColorSpace(), transform);
  auto inverse = ColorProcessor::Create(manager, manager->GetReferenceColorSpace(), transform, ColorProcessor::kInverse);
  for (float value : {0.01f, 0.18f, 0.5f, 0.9f}) {
    const Color source(value, value * 0.5f, value * 0.25f, 0.5f);
    const Color result = inverse->ConvertColor(forward->ConvertColor(source));
    OLIVE_ASSERT(std::fabs(result.red() - source.red()) < 1e-4);
    OLIVE_ASSERT(std::fabs(result.green() - source.green()) < 1e-4);
    OLIVE_ASSERT(std::fabs(result.blue() - source.blue()) < 1e-4);
    OLIVE_ASSERT(std::fabs(result.alpha() - source.alpha()) < 1e-6);
  }
  OLIVE_TEST_END;
}
OLIVE_ADD_TEST(InvalidColorSpaceReportsError)
{
  ColorManager::SetUpDefaultConfig();
  Project project;
  bool caught = false;
  try {
    ColorProcessor::Create(project.color_manager(), QStringLiteral("missing-colorspace"),
                           project.color_manager()->GetReferenceColorSpace());
  } catch (const OCIO::Exception&) { caught = true; }
  OLIVE_ASSERT(caught);
  OLIVE_TEST_END;
}
OLIVE_ADD_TEST(MergeEmptyAndSingleLayer)
{
  MergeNode merge;
  const VideoParams params(16, 16, PixelFormat::F32, 4);
  auto texture = std::make_shared<Texture>(params);
  NodeGlobals globals(params, AudioParams(), TimeRange(0, 1), LoopMode::kLoopModeOff);
  NodeValueTable table;
  merge.Value({}, globals, &table);
  OLIVE_ASSERT(!table.Get(NodeValue::kTexture).toTexture());
  NodeValueRow row;
  row.insert(MergeNode::kBaseIn, NodeValue(NodeValue::kTexture, texture));
  merge.Value(row, globals, &table);
  OLIVE_ASSERT(table.Get(NodeValue::kTexture).toTexture() == texture);
  OLIVE_TEST_END;
}
}
