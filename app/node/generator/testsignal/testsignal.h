#ifndef OLIVE_TESTSIGNAL_H
#define OLIVE_TESTSIGNAL_H
#include "node/node.h"
namespace olive {
class ToneGenerator : public Node {
  Q_OBJECT
public:
  ToneGenerator();
  NODE_DEFAULT_FUNCTIONS(ToneGenerator)
  QString Name() const override { return tr("Tone"); }
  QString id() const override { return QStringLiteral("org.olivevideoeditor.Olive.tone"); }
  QVector<CategoryID> Category() const override { return {kCategoryGenerator}; }
  QString Description() const override { return tr("Generate a sine wave with continuous phase."); }
  void Retranslate() override;
  void Value(const NodeValueRow&, const NodeGlobals&, NodeValueTable*) const override;
  static const QString kFrequency;
  static const QString kAmplitude;
};
class BarsGenerator : public Node {
  Q_OBJECT
public:
  BarsGenerator() = default;
  NODE_DEFAULT_FUNCTIONS(BarsGenerator)
  QString Name() const override { return tr("Color Bars"); }
  QString id() const override { return QStringLiteral("org.olivevideoeditor.Olive.colorbars"); }
  QVector<CategoryID> Category() const override { return {kCategoryGenerator}; }
  QString Description() const override { return tr("Generate eight full-level RGB color bars."); }
  void Value(const NodeValueRow&, const NodeGlobals&, NodeValueTable*) const override;
  ShaderCode GetShaderCode(const ShaderRequest&) const override;
};
}
#endif
