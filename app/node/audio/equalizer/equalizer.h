#ifndef EQUALIZERNODE_H
#define EQUALIZERNODE_H

#include "biquad.h"
#include "node/node.h"

namespace olive {

class EqualizerNode : public Node
{
  Q_OBJECT
public:
  EqualizerNode();

  NODE_DEFAULT_FUNCTIONS(EqualizerNode)

  virtual QString Name() const override;
  virtual QString id() const override;
  virtual QVector<CategoryID> Category() const override;
  virtual QString Description() const override;

  virtual void Value(const NodeValueRow &value, const NodeGlobals &globals, NodeValueTable *table) const override;

  virtual void ProcessSamples(const NodeValueRow &values, const SampleBuffer &input, SampleBuffer &output, int index) const override;

  virtual void Retranslate() override;

  static constexpr int kBandCount = 6;

  static QString BandEnabledInput(int band);
  static QString BandTypeInput(int band);
  static QString BandFreqInput(int band);
  static QString BandGainInput(int band);
  static QString BandQInput(int band);

  static const QString kSamplesInput;

  void SetBandEnabled(int band, bool enabled);
  bool GetBandEnabled(int band) const;

  void SetBandType(int band, BiquadFilterType type);
  BiquadFilterType GetBandType(int band) const;

  void SetBandFrequency(int band, double freq);
  double GetBandFrequency(int band) const;

  void SetBandGain(int band, double gain);
  double GetBandGain(int band) const;

  void SetBandQ(int band, double q);
  double GetBandQ(int band) const;

private:
  void ProcessBuffer(SampleBuffer &buffer, const NodeValueRow &values) const;

  static const BiquadFilterType kDefaultTypes[kBandCount];
  static const double kDefaultFrequencies[kBandCount];
  static const double kDefaultQs[kBandCount];
};

} // namespace olive

#endif // EQUALIZERNODE_H
