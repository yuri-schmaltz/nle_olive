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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <libavutil/channel_layout.h>
#include <libavutil/error.h>

#include "audio/audioprocessor.h"
#include "testutil.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/output/track/track.h"

namespace olive {

namespace {

// Check that AudioProcessor itself preserves state across input chunks.

const int kSampleRate = 48000;
const int kBlockSize = 4096;
const int kBlockCount = 2; // two blocks means one boundary between them
const double kTempo = 2.0;

AudioParams TestParams()
{
  return AudioParams(kSampleRate,
                     uint64_t(AV_CH_LAYOUT_STEREO),
                     core::SampleFormat::F32P);
}

// Fill `sb` with an identical 440 Hz tone on every channel.
void FillSine(SampleBuffer *sb)
{
  sb->silence();

  const size_t n = sb->sample_count();
  const int channels = sb->audio_params().channel_count();
  const double w = 2.0 * std::acos(-1.0) * 440.0 / double(kSampleRate);

  for (size_t i = 0; i < n; i++) {
    const float s = float(std::sin(w * double(i)));
    for (int c = 0; c < channels; c++) {
      sb->data(c)[i] = s;
    }
  }
}

void AppendChannel0(const AudioProcessor::Buffer &raw, std::vector<float> *out)
{
  if (raw.empty()) {
    return;
  }
  const QByteArray &plane = raw.front();
  const float *samples = reinterpret_cast<const float*>(plane.constData());
  const size_t count = size_t(plane.size()) / sizeof(float);
  out->insert(out->end(), samples, samples + count);
}

// Feed `in` through `proc`, appending channel-0 output samples to `out`.
int ConvertAppend(AudioProcessor *proc, SampleBuffer *in, std::vector<float> *out)
{
  AudioProcessor::Buffer raw;
  int r = proc->Convert(in->to_raw_ptrs().data(), static_cast<int>(in->sample_count()), &raw);
  if (r < 0) {
    return r;
  }
  AppendChannel0(raw, out);
  return 0;
}

// Flush `proc` once and append all drained channel-0 samples to `out`.
int FlushAppend(AudioProcessor *proc, std::vector<float> *out)
{
  proc->Flush();

  AudioProcessor::Buffer raw;
  int r = proc->Convert(nullptr, 0, &raw);
  if (r < 0) {
    return r;
  }
  AppendChannel0(raw, out);
  return 0;
}

}

OLIVE_ADD_TEST(TempoStreamContinuity)
{
  const AudioParams params = TestParams();
  const int channels = params.channel_count();
  OLIVE_ASSERT(channels == 2);

  // Two consecutive tone blocks.
  SampleBuffer block_a(params, size_t(kBlockSize));
  SampleBuffer block_b(params, size_t(kBlockSize));
  FillSine(&block_a);
  FillSine(&block_b);

  // Path 1 (whole): one processor, concatenated input, single flush at end.
  std::vector<float> out_whole;
  {
    AudioProcessor proc;
    OLIVE_ASSERT(proc.Open(params, params, kTempo));

    SampleBuffer whole(params, size_t(kBlockSize * kBlockCount));
    for (int c = 0; c < channels; c++) {
      memcpy(whole.data(c), block_a.data(c), size_t(kBlockSize) * sizeof(float));
      memcpy(whole.data(c) + kBlockSize, block_b.data(c), size_t(kBlockSize) * sizeof(float));
    }

    OLIVE_ASSERT(ConvertAppend(&proc, &whole, &out_whole) == 0);
    OLIVE_ASSERT(FlushAppend(&proc, &out_whole) == 0);
  }

  // Path 2 (chunked): one processor, one Convert per block, single flush at
  // the end. This tests the filter independently of track rendering.
  std::vector<float> out_chunked;
  {
    AudioProcessor proc;
    OLIVE_ASSERT(proc.Open(params, params, kTempo));

    OLIVE_ASSERT(ConvertAppend(&proc, &block_a, &out_chunked) == 0);
    OLIVE_ASSERT(ConvertAppend(&proc, &block_b, &out_chunked) == 0);
    OLIVE_ASSERT(FlushAppend(&proc, &out_chunked) == 0);
  }

  // Block boundaries must not disturb the stream: identical sizes and samples.
  OLIVE_ASSERT(!out_whole.empty());
  OLIVE_ASSERT_EQUAL(out_whole.size(), out_chunked.size());
  for (size_t i = 0; i < out_whole.size(); i++) {
    OLIVE_ASSERT(std::fabs(out_whole[i] - out_chunked[i]) < 1e-6f);
  }

  OLIVE_TEST_END;
}

// Compare the actual Track::Value output with a fully drained filter. Include
// a following normal-speed clip and a gap to detect tail leakage and overwrite.
int CheckTrackTempo(int input_samples, double tempo, bool reverse)
{
  const AudioParams params = TestParams();
  ClipBlock clip;
  GapBlock gap;
  ClipBlock normal;
  Track track;
  track.set_type(Track::kAudio);
  const int duration = 8192;
  clip.set_length_and_media_out(rational(duration, kSampleRate));
  clip.SetStandardValue(ClipBlock::kSpeedInput, tempo);
  clip.set_maintain_audio_pitch(true);
  clip.set_reverse(reverse);
  gap.set_length_and_media_out(rational(128, kSampleRate));
  normal.set_length_and_media_out(rational(256, kSampleRate));
  // Allocate array slots in the opposite order to the timeline.
  track.AppendBlock(&normal);
  track.PrependBlock(&gap);
  track.PrependBlock(&clip);

  SampleBuffer input(params, size_t(input_samples));
  FillSine(&input);
  SampleBuffer normal_input(params, size_t(256));
  for (int c = 0; c < 2; c++) {
    for (int i = 0; i < 256; i++) normal_input.data(c)[i] = 0.25f;
  }

  AudioProcessor reference;
  OLIVE_ASSERT(reference.Open(params, params, tempo));
  std::vector<float> expected;
  OLIVE_ASSERT(ConvertAppend(&reference, &input, &expected) == 0);
  OLIVE_ASSERT(FlushAppend(&reference, &expected) == 0);
  if (reverse) std::reverse(expected.begin(), expected.end());

  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));
  blocks.emplace(track.GetArrayIndexFromBlock(&normal), NodeValue(NodeValue::kSamples, normal_input));
  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params,
                      TimeRange(0, rational(duration + 128 + 256, kSampleRate)),
                      LoopMode::kLoopModeOff);
  NodeValueTable table;
  track.Value(row, globals, &table);
  const SampleBuffer result = table.Get(NodeValue::kSamples).toSamples();
  OLIVE_ASSERT_EQUAL(result.sample_count(), size_t(duration + 128 + 256));
  for (int c = 0; c < 2; c++) {
    for (int i = 0; i < duration; i++) {
      const float wanted = size_t(i) < expected.size() ? expected[i] : 0.0f;
      OLIVE_ASSERT(std::fabs(result.data(c)[i] - wanted) < 1e-6f);
    }
    for (int i = duration; i < duration + 128; i++) {
      OLIVE_ASSERT(result.data(c)[i] == 0.0f);
    }
    for (int i = duration + 128; i < duration + 384; i++) {
      OLIVE_ASSERT(result.data(c)[i] == 0.25f);
    }
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(TrackTempoDrainsAndBoundsOutput)
{
  return CheckTrackTempo(8192, 2.0, false);
}

OLIVE_ADD_TEST(TrackTempoShortInput)
{
  return CheckTrackTempo(128, 2.0, false);
}

OLIVE_ADD_TEST(TrackTempoReverse)
{
  return CheckTrackTempo(8192, 2.0, true);
}

OLIVE_ADD_TEST(TrackTempoSlow)
{
  return CheckTrackTempo(4096, 0.5, false);
}

}
