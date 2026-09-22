// Integration tests of production audio nodes. Mixer UI/VU coverage is pending.
#include "e2e_fixtures.h"
#include "node/audio/equalizer/equalizer.h"

namespace olive {
namespace {
SampleBuffer ApplyEqualizer(EqualizerNode &eq, const SampleBuffer &input, bool enabled = true)
{
  NodeValueRow row;
  row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
  row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, enabled));
  NodeGlobals globals(VideoParams(), input.audio_params(),
                      TimeRange(0, rational(input.sample_count(), input.audio_params().sample_rate())),
                      LoopMode::kLoopModeOff);
  NodeValueTable table;
  eq.Value(row, globals, &table);
  return table.Get(NodeValue::kSamples).toSamples();
}

SampleBuffer ApplyTrack(Track &track, ClipBlock &clip, const SampleBuffer &input)
{
  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));
  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), input.audio_params(),
                      TimeRange(clip.in(), clip.out()), LoopMode::kLoopModeOff);
  NodeValueTable table;
  track.Value(row, globals, &table);
  return table.Get(NodeValue::kSamples).toSamples();
}
}

OLIVE_ADD_TEST(EqualizerIntoTrackGainAndPan)
{
  const auto input = e2e::GenerateSineBuffer(48000, 2, 4800, 1000.0f, 0.25f);
  EqualizerNode eq;
  for (int b = 1; b < EqualizerNode::kBandCount; ++b) eq.SetBandEnabled(b, false);
  eq.SetBandType(0, kFilterPeaking);
  eq.SetBandFrequency(0, 1000.0);
  eq.SetBandGain(0, 6.0);
  eq.SetBandQ(0, 1.0);
  const auto filtered = ApplyEqualizer(eq, input);
  OLIVE_ASSERT_EQUAL(filtered.sample_count(), input.sample_count());

  ClipBlock clip;
  clip.set_length_and_media_out(rational(1, 10));
  Track track;
  track.set_type(Track::kAudio);
  track.AppendBlock(&clip);
  track.SetStandardValue(Track::kVolumeInput, 0.5);
  track.SetStandardValue(Track::kPanInput, -1.0);
  const auto output = ApplyTrack(track, clip, filtered);
  OLIVE_ASSERT_EQUAL(output.sample_count(), input.sample_count());
  const float magnitude = e2e::ComputeGoertzelMagnitude(output, 0, 1000, 48000);
  OLIVE_ASSERT(magnitude > 0.23f && magnitude < 0.26f);
  OLIVE_ASSERT_EQUAL(e2e::ComputePeak(output, 1), 0.0f);
  // Neither node may modify its caller's source buffer.
  OLIVE_ASSERT(std::abs(e2e::ComputePeak(input, 0) - 0.25f) < 1e-6f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(EqualizerBypassPreservesSamples)
{
  const auto input = e2e::GenerateWhiteNoiseBuffer(48000, 2, 1024, 0.8f);
  EqualizerNode eq;
  eq.SetBandGain(2, 24.0);
  const auto output = ApplyEqualizer(eq, input, false);
  OLIVE_ASSERT_EQUAL(output.sample_count(), input.sample_count());
  for (int c = 0; c < 2; ++c) {
    OLIVE_ASSERT(std::memcmp(input.data(c), output.data(c), input.sample_count() * sizeof(float)) == 0);
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SplitAudioKeepsTrackControlsAndRenderedGain)
{
  auto ctx = e2e::SetupStandardTimeline();
  auto *clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(1, 5));
  ctx.audio_track->AppendBlock(clip);
  ctx.audio_track->SetStandardValue(Track::kVolumeInput, 0.25);
  ctx.audio_track->SetStandardValue(Track::kPanInput, 1.0);
  BlockSplitPreservingLinksCommand split({clip}, {rational(1, 10)});
  split.redo_now();
  OLIVE_ASSERT_EQUAL(ctx.audio_track->Blocks().size(), 2);
  for (Block *block : ctx.audio_track->Blocks()) {
    auto *part = dynamic_cast<ClipBlock*>(block);
    OLIVE_ASSERT(part != nullptr);
    const auto input = e2e::GenerateSineBuffer(48000, 2, 4800, 1000, 1.0f);
    const auto output = ApplyTrack(*ctx.audio_track, *part, input);
    OLIVE_ASSERT_EQUAL(output.sample_count(), input.sample_count());
    OLIVE_ASSERT_EQUAL(e2e::ComputePeak(output, 0), 0.0f);
    OLIVE_ASSERT(std::abs(e2e::ComputePeak(output, 1) - 0.25f) < 1e-6f);
  }
  split.undo_now();
  OLIVE_ASSERT_EQUAL(ctx.audio_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(ctx.audio_track->GetStandardValue(Track::kVolumeInput).toDouble(), 0.25);
  OLIVE_TEST_END;
}
}
