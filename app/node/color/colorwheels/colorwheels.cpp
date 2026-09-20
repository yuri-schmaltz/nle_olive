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

#include "colorwheels.h"

#include "render/job/shaderjob.h"

namespace olive {

const QString ColorWheelsNode::kTextureInput = QStringLiteral("tex_in");
const QString ColorWheelsNode::kLiftInput = QStringLiteral("lift_in");
const QString ColorWheelsNode::kGammaInput = QStringLiteral("gamma_in");
const QString ColorWheelsNode::kGainInput = QStringLiteral("gain_in");
const QString ColorWheelsNode::kOffsetInput = QStringLiteral("offset_in");

#define super Node

ColorWheelsNode::ColorWheelsNode()
{
  AddInput(kTextureInput, NodeValue::kTexture, InputFlags(kInputFlagNotKeyframable));

  // Lift: default (0, 0, 0)
  AddInput(kLiftInput, NodeValue::kVec3, QVector3D(0.0f, 0.0f, 0.0f));
  SetInputProperty(kLiftInput, QStringLiteral("base"), 0.01);
  SetVec3InputColors(kLiftInput);

  // Gamma: default (1, 1, 1)
  AddInput(kGammaInput, NodeValue::kVec3, QVector3D(1.0f, 1.0f, 1.0f));
  SetInputProperty(kGammaInput, QStringLiteral("min"), QVector3D(0.001f, 0.001f, 0.001f));
  SetInputProperty(kGammaInput, QStringLiteral("base"), 0.01);
  SetVec3InputColors(kGammaInput);

  // Gain: default (1, 1, 1)
  AddInput(kGainInput, NodeValue::kVec3, QVector3D(1.0f, 1.0f, 1.0f));
  SetInputProperty(kGainInput, QStringLiteral("min"), QVector3D(0.0f, 0.0f, 0.0f));
  SetInputProperty(kGainInput, QStringLiteral("base"), 0.01);
  SetVec3InputColors(kGainInput);

  // Offset: default (0, 0, 0)
  AddInput(kOffsetInput, NodeValue::kVec3, QVector3D(0.0f, 0.0f, 0.0f));
  SetInputProperty(kOffsetInput, QStringLiteral("base"), 0.01);
  SetVec3InputColors(kOffsetInput);

  SetFlag(kVideoEffect);
  SetEffectInput(kTextureInput);
}

void ColorWheelsNode::Retranslate()
{
  super::Retranslate();

  SetInputName(kTextureInput, tr("Texture"));
  SetInputName(kLiftInput, tr("Lift (Shadows)"));
  SetInputName(kGammaInput, tr("Gamma (Midtones)"));
  SetInputName(kGainInput, tr("Gain (Highlights)"));
  SetInputName(kOffsetInput, tr("Offset"));
}

ShaderCode ColorWheelsNode::GetShaderCode(const ShaderRequest &request) const
{
  Q_UNUSED(request)
  return ShaderCode(FileFunctions::ReadFileAsString(":/shaders/colorwheels.frag"));
}

void ColorWheelsNode::Value(const NodeValueRow &value, const NodeGlobals &globals, NodeValueTable *table) const
{
  if (TexturePtr tex = value[kTextureInput].toTexture()) {
    ShaderJob job(value);
    table->Push(NodeValue::kTexture, tex->toJob(job), this);
  }
}

void ColorWheelsNode::SetVec3InputColors(const QString &input)
{
  SetInputProperty(input, QStringLiteral("color0"), QColor(255, 64, 64).name());
  SetInputProperty(input, QStringLiteral("color1"), QColor(64, 255, 64).name());
  SetInputProperty(input, QStringLiteral("color2"), QColor(64, 64, 255).name());
}

} // olive
