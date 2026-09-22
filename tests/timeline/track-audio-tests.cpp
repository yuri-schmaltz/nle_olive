#include <cmath>
#include <vector>

#include <libavutil/channel_layout.h>

#include "testutil.h"
#include <olive/core/core.h>
#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundogeneral.h"

namespace olive {

namespace {

const int kSampleRate = 48000;
const int kBlockSize = 1024;

AudioParams TestStereoParams() {
  return AudioParams(kSampleRate, uint64_t(AV_CH_LAYOUT_STEREO), core::SampleFormat::F32P);
}

void FillTone(SampleBuffer *sb, float val_ch0 = 0.8f, float val_ch1 = 0.8f) {
  const size_t n = sb->sample_count();
  for (size_t i = 0; i < n; i++) {
    sb->data(0)[i] = val_ch0;
    sb->data(1)[i] = val_ch1;
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Track Volume Scaling (0.0, 0.5, 1.0, 2.0)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(TrackAudioVolumeScaling)
{
  const AudioParams params = TestStereoParams();
  ClipBlock clip;
  clip.set_length_and_media_out(rational(kBlockSize, kSampleRate));

  SampleBuffer input(params, size_t(kBlockSize));
  FillTone(&input, 0.8f, 0.8f);

  Track track;
  track.set_type(Track::kAudio);
  track.AppendBlock(&clip);

  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));
  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kBlockSize, kSampleRate)), LoopMode::kLoopModeOff);

  // Case 1: Unity volume (1.0)
  {
    track.SetStandardValue(Track::kVolumeInput, 1.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(res.sample_count(), size_t(kBlockSize));
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT(std::fabs(res.data(c)[i] - 0.8f) < 1e-6f);
      }
    }
  }

  // Case 2: Half volume (0.5 = -6 dB)
  {
    track.SetStandardValue(Track::kVolumeInput, 0.5f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT(std::fabs(res.data(c)[i] - 0.4f) < 1e-6f);
      }
    }
  }

  // Case 3: Double volume (2.0 = +6 dB)
  {
    track.SetStandardValue(Track::kVolumeInput, 2.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT(std::fabs(res.data(c)[i] - 1.6f) < 1e-6f);
      }
    }
  }

  // Case 4: Mute / Silence (0.0)
  {
    track.SetStandardValue(Track::kVolumeInput, 0.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT_EQUAL(res.data(c)[i], 0.0f);
      }
    }
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 2: Track Stereo Pan (-1.0, -0.5, 0.0, 0.5, +1.0)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(TrackAudioStereoPanning)
{
  const AudioParams params = TestStereoParams();
  ClipBlock clip;
  clip.set_length_and_media_out(rational(kBlockSize, kSampleRate));

  SampleBuffer input(params, size_t(kBlockSize));
  FillTone(&input, 1.0f, 1.0f);

  Track track;
  track.set_type(Track::kAudio);
  track.AppendBlock(&clip);

  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));
  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kBlockSize, kSampleRate)), LoopMode::kLoopModeOff);

  // Pan Center (0.0) -> Both Left and Right are 1.0
  {
    track.SetStandardValue(Track::kVolumeInput, 1.0f);
    track.SetStandardValue(Track::kPanInput, 0.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 1.0f) < 1e-6f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 1.0f) < 1e-6f);
    }
  }

  // Pan Full Left (-1.0) -> Left is 1.0, Right is completely silenced (0.0)
  {
    track.SetStandardValue(Track::kPanInput, -1.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 1.0f) < 1e-6f);
      OLIVE_ASSERT_EQUAL(res.data(1)[i], 0.0f);
    }
  }

  // Pan Full Right (+1.0) -> Left is completely silenced (0.0), Right is 1.0
  {
    track.SetStandardValue(Track::kPanInput, 1.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT_EQUAL(res.data(0)[i], 0.0f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 1.0f) < 1e-6f);
    }
  }

  // Pan Half Right (0.5) -> Left scaled by (1.0 - 0.5) = 0.5, Right unchanged (1.0)
  {
    track.SetStandardValue(Track::kPanInput, 0.5f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 0.5f) < 1e-6f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 1.0f) < 1e-6f);
    }
  }

  // Pan Half Left (-0.5) -> Left unchanged (1.0), Right scaled by (1.0 - 0.5) = 0.5
  {
    track.SetStandardValue(Track::kPanInput, -0.5f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 1.0f) < 1e-6f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 0.5f) < 1e-6f);
    }
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 3: Track Solo Logic Verification Across Multiple Tracks
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(TrackAudioSoloLogic)
{
  ColorManager::SetUpDefaultConfig();
  const AudioParams params = TestStereoParams();
  Project project;
  Sequence sequence;
  sequence.setParent(&project);

  Track *track_a = TimelineAddTrackCommand::RunImmediately(sequence.track_list(Track::kAudio));
  Track *track_b = TimelineAddTrackCommand::RunImmediately(sequence.track_list(Track::kAudio), true);

  OLIVE_ASSERT(track_a != nullptr);
  OLIVE_ASSERT(track_b != nullptr);

  ClipBlock* clip_a = new ClipBlock();
  ClipBlock* clip_b = new ClipBlock();
  clip_a->setParent(&project);
  clip_b->setParent(&project);
  clip_a->set_length_and_media_out(rational(kBlockSize, kSampleRate));
  clip_b->set_length_and_media_out(rational(kBlockSize, kSampleRate));
  track_a->AppendBlock(clip_a);
  track_b->AppendBlock(clip_b);

  const TimeRange range(0, rational(kBlockSize, kSampleRate));

  // Scenario 1: Neither track soloed -> Both active
  {
    track_a->SetStandardValue(Track::kSoloInput, false);
    track_b->SetStandardValue(Track::kSoloInput, false);

    OLIVE_ASSERT_EQUAL(track_a->IsEffectivelyMuted(), false);
    OLIVE_ASSERT_EQUAL(track_b->IsEffectivelyMuted(), false);
    OLIVE_ASSERT_EQUAL(sequence.track_list(Track::kAudio)->HasSoloTrack(), false);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(!act_a.elements().empty());
    OLIVE_ASSERT(!act_b.elements().empty());
  }

  // Scenario 2: Track A soloed -> Track A active, Track B silenced (kNoElements)
  {
    track_a->SetStandardValue(Track::kSoloInput, true);
    track_b->SetStandardValue(Track::kSoloInput, false);

    OLIVE_ASSERT_EQUAL(track_a->IsEffectivelyMuted(), false);
    OLIVE_ASSERT_EQUAL(track_b->IsEffectivelyMuted(), true);
    OLIVE_ASSERT_EQUAL(sequence.track_list(Track::kAudio)->HasSoloTrack(), true);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(!act_a.elements().empty());
    OLIVE_ASSERT(act_b.elements().empty());
  }

  // Scenario 3: Both tracks soloed -> Both active
  {
    track_a->SetStandardValue(Track::kSoloInput, true);
    track_b->SetStandardValue(Track::kSoloInput, true);

    OLIVE_ASSERT_EQUAL(track_a->IsEffectivelyMuted(), false);
    OLIVE_ASSERT_EQUAL(track_b->IsEffectivelyMuted(), false);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(!act_a.elements().empty());
    OLIVE_ASSERT(!act_b.elements().empty());
  }

  // Scenario 4: Track B soloed -> Track B active, Track A silenced
  {
    track_a->SetStandardValue(Track::kSoloInput, false);
    track_b->SetStandardValue(Track::kSoloInput, true);

    OLIVE_ASSERT_EQUAL(track_a->IsEffectivelyMuted(), true);
    OLIVE_ASSERT_EQUAL(track_b->IsEffectivelyMuted(), false);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(act_a.elements().empty());
    OLIVE_ASSERT(!act_b.elements().empty());
  }

  // Scenario 5: Track A soloed, but also muted -> Mute overrides solo (both silenced)
  {
    track_a->SetStandardValue(Track::kSoloInput, true);
    track_a->SetMuted(true);
    track_b->SetStandardValue(Track::kSoloInput, false);
    track_b->SetMuted(false);

    OLIVE_ASSERT_EQUAL(track_a->IsEffectivelyMuted(), true);
    OLIVE_ASSERT_EQUAL(track_b->IsEffectivelyMuted(), true);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(act_a.elements().empty()); // A is muted
    OLIVE_ASSERT(act_b.elements().empty()); // B is silenced because A is soloed
  }

  // Scenario 6: Effectively muted Track::ProcessAudioTrack returns silence
  {
    track_a->SetMuted(false);
    track_a->SetSolo(true);
    track_b->SetSolo(false); // track_b is effectively muted

    SampleBuffer input_b(params, size_t(kBlockSize));
    FillTone(&input_b, 1.0f, 1.0f);
    NodeValueArray blocks_b;
    blocks_b.emplace(track_b->GetArrayIndexFromBlock(clip_b), NodeValue(NodeValue::kSamples, input_b));
    NodeValueRow row_b;
    row_b.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks_b, track_b, true));
    NodeGlobals globals_b(VideoParams(), params, range, LoopMode::kLoopModeOff);

    NodeValueTable table_b;
    track_b->Value(row_b, globals_b, &table_b);
    SampleBuffer res_b = table_b.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(res_b.sample_count(), size_t(kBlockSize));
    for (size_t i = 0; i < size_t(kBlockSize); i++) {
      OLIVE_ASSERT_EQUAL(res_b.data(0)[i], 0.0f);
      OLIVE_ASSERT_EQUAL(res_b.data(1)[i], 0.0f);
    }
  }

  OLIVE_TEST_END;
}

} // namespace olive
