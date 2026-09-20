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

#ifndef COLORWHEELSNODE_H
#define COLORWHEELSNODE_H

#include "node/node.h"

namespace olive {

class ColorWheelsNode : public Node
{
  Q_OBJECT
public:
  ColorWheelsNode();

  NODE_DEFAULT_FUNCTIONS(ColorWheelsNode)

  virtual QString Name() const override { return tr("Color Wheels (Lift/Gamma/Gain)"); }
  virtual QString id() const override { return QStringLiteral("org.olivevideoeditor.Olive.colorwheels"); }
  virtual QVector<CategoryID> Category() const override { return {kCategoryColor}; }
  virtual QString Description() const override { return tr("Three-way color grading with lift, gamma, gain, and offset."); }

  virtual void Retranslate() override;

  virtual ShaderCode GetShaderCode(const ShaderRequest &request) const override;
  virtual void Value(const NodeValueRow& value, const NodeGlobals &globals, NodeValueTable *table) const override;

  static const QString kTextureInput;
  static const QString kLiftInput;
  static const QString kGammaInput;
  static const QString kGainInput;
  static const QString kOffsetInput;

private:
  void SetVec3InputColors(const QString &input);

};

} // olive

#endif // COLORWHEELSNODE_H
