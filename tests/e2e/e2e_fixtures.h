/***
  Olive Video Editor - End-to-End Test Fixtures (tests/e2e/e2e_fixtures.h)
  Provides in-memory planar audio synthesis, synthetic video generator, and XML/JSON project fixtures.
***/

#ifndef OLIVE_E2E_FIXTURES_H
#define OLIVE_E2E_FIXTURES_H

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <libavutil/channel_layout.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundosplit.h"
#include "testutil.h"

namespace olive {
namespace e2e {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// 1. Audio Media Fixtures & DSP Verification Utilities
// ============================================================================

inline AudioParams MakeAudioParams(int sample_rate = 48000, int channels = 2) {
  uint64_t layout = (channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
  return AudioParams(sample_rate, layout, core::SampleFormat::F32P);
}

/**
 * @brief Synthesize in-memory pure sine wave into a planar SampleBuffer.
 */
inline core::SampleBuffer GenerateSineBuffer(int sample_rate, int channels, size_t sample_count,
                                             float frequency_hz, float amplitude = 1.0f) {
  AudioParams params = MakeAudioParams(sample_rate, channels);
  core::SampleBuffer buffer(params, sample_count);
  buffer.allocate();
  buffer.silence();

  const double w = 2.0 * M_PI * double(frequency_hz) / double(sample_rate);
  for (int c = 0; c < channels; ++c) {
    float* channel_data = buffer.data(c);
    for (size_t i = 0; i < sample_count; ++i) {
      channel_data[i] = amplitude * float(std::sin(w * double(i)));
    }
  }
  return buffer;
}

/**
 * @brief Synthesize two mixed frequencies for filter crossover and shelf tests.
 */
inline core::SampleBuffer GenerateDualToneBuffer(int sample_rate, int channels, size_t sample_count,
                                                 float f1, float a1, float f2, float a2) {
  AudioParams params = MakeAudioParams(sample_rate, channels);
  core::SampleBuffer buffer(params, sample_count);
  buffer.allocate();
  buffer.silence();

  const double w1 = 2.0 * M_PI * double(f1) / double(sample_rate);
  const double w2 = 2.0 * M_PI * double(f2) / double(sample_rate);
  for (int c = 0; c < channels; ++c) {
    float* channel_data = buffer.data(c);
    for (size_t i = 0; i < sample_count; ++i) {
      channel_data[i] = a1 * float(std::sin(w1 * double(i))) + a2 * float(std::sin(w2 * double(i)));
    }
  }
  return buffer;
}

/**
 * @brief Synthesize deterministic uniform white noise buffer.
 */
inline core::SampleBuffer GenerateWhiteNoiseBuffer(int sample_rate, int channels, size_t sample_count,
                                                   float amplitude = 1.0f, unsigned int seed = 42) {
  AudioParams params = MakeAudioParams(sample_rate, channels);
  core::SampleBuffer buffer(params, sample_count);
  buffer.allocate();
  buffer.silence();

  std::srand(seed);
  for (int c = 0; c < channels; ++c) {
    float* channel_data = buffer.data(c);
    for (size_t i = 0; i < sample_count; ++i) {
      float r = float(std::rand()) / float(RAND_MAX); // [0, 1]
      channel_data[i] = amplitude * (2.0f * r - 1.0f); // [-amp, +amp]
    }
  }
  return buffer;
}

/**
 * @brief Synthesize unit impulse delta[n] buffer.
 */
inline core::SampleBuffer GenerateImpulseBuffer(int sample_rate, int channels, size_t sample_count,
                                                size_t impulse_index = 0, float amplitude = 1.0f) {
  AudioParams params = MakeAudioParams(sample_rate, channels);
  core::SampleBuffer buffer(params, sample_count);
  buffer.allocate();
  buffer.silence();

  if (impulse_index < sample_count) {
    for (int c = 0; c < channels; ++c) {
      buffer.data(c)[impulse_index] = amplitude;
    }
  }
  return buffer;
}

/**
 * @brief Synthesize constant DC offset buffer.
 */
inline core::SampleBuffer GenerateDCOffsetBuffer(int sample_rate, int channels, size_t sample_count,
                                                 float dc_value = 1.0f) {
  AudioParams params = MakeAudioParams(sample_rate, channels);
  core::SampleBuffer buffer(params, sample_count);
  buffer.allocate();
  buffer.silence();

  for (int c = 0; c < channels; ++c) {
    float* channel_data = buffer.data(c);
    for (size_t i = 0; i < sample_count; ++i) {
      channel_data[i] = dc_value;
    }
  }
  return buffer;
}

/**
 * @brief Compute maximum absolute peak in channel.
 */
inline float ComputePeak(const core::SampleBuffer& buf, int channel = 0) {
  if (channel < 0 || channel >= buf.channel_count() || buf.sample_count() == 0) {
    return 0.0f;
  }
  const float* data = buf.data(channel);
  float peak = 0.0f;
  for (size_t i = 0; i < buf.sample_count(); ++i) {
    float abs_val = std::abs(data[i]);
    if (abs_val > peak) {
      peak = abs_val;
    }
  }
  return peak;
}

/**
 * @brief Compute Root Mean Square (RMS) energy in channel.
 */
inline float ComputeRMS(const core::SampleBuffer& buf, int channel = 0) {
  if (channel < 0 || channel >= buf.channel_count() || buf.sample_count() == 0) {
    return 0.0f;
  }
  const float* data = buf.data(channel);
  double sum_sq = 0.0;
  for (size_t i = 0; i < buf.sample_count(); ++i) {
    double val = double(data[i]);
    sum_sq += val * val;
  }
  return float(std::sqrt(sum_sq / double(buf.sample_count())));
}

/**
 * @brief Goertzel algorithm to compute single-bin magnitude at target_freq.
 */
inline float ComputeGoertzelMagnitude(const core::SampleBuffer& buf, int channel,
                                      float target_freq, float sample_rate) {
  if (channel < 0 || channel >= buf.channel_count()) return 0.0f;
  const size_t N = buf.sample_count();
  if (N == 0) return 0.0f;
  const float* data = buf.data(channel);

  float k = std::round((float(N) * target_freq) / sample_rate);
  float omega = (2.0f * float(M_PI) * k) / float(N);
  float cosine = std::cos(omega);
  float sine = std::sin(omega);
  float coeff = 2.0f * cosine;

  float q0 = 0.0f;
  float q1 = 0.0f;
  float q2 = 0.0f;

  for (size_t i = 0; i < N; ++i) {
    q0 = coeff * q1 - q2 + data[i];
    q2 = q1;
    q1 = q0;
  }

  float real = q1 - q2 * cosine;
  float imag = q2 * sine;
  return std::sqrt(real * real + imag * imag) / (float(N) * 0.5f);
}

// ============================================================================
// 2. Synthetic Video Generation via FFmpeg lavfi
// ============================================================================

/**
 * @brief Generate a deterministic synthetic test video with hard scene cuts.
 * Returns true if generated successfully or false if ffmpeg is unavailable.
 */
inline bool GenerateSyntheticVideo(const QString& output_path,
                                   const QVector<QPair<QString, int>>& color_segments) {
  QString ffmpeg_bin = QStringLiteral("ffmpeg");
#ifdef OLIVE_TEST_FFMPEG
  ffmpeg_bin = QStringLiteral(OLIVE_TEST_FFMPEG);
#endif

  // Construct FFmpeg filter graph to generate colored segments concatenated together
  // Example: color=c=red:s=320x240:d=3:r=25 [v0]; color=c=blue:s=320x240:d=4:r=25 [v1]; [v0][v1]concat=n=2:v=1:a=0[out]
  QString filter_complex;
  QString concat_inputs;
  for (int i = 0; i < color_segments.size(); ++i) {
    const QString& color = color_segments.at(i).first;
    int duration = color_segments.at(i).second;
    filter_complex += QString("color=c=%1:s=320x240:d=%2:r=25 [v%3]; ").arg(color).arg(duration).arg(i);
    concat_inputs += QString("[v%1]").arg(i);
  }
  filter_complex += QString("%1concat=n=%2:v=1:a=0 [out]").arg(concat_inputs).arg(color_segments.size());

  QStringList args;
  args << "-y" << "-f" << "lavfi" << "-i" << "anullsrc=r=48000:cl=stereo"
       << "-filter_complex" << filter_complex
       << "-map" << "[out]" << "-map" << "0:a"
       << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p" << "-preset" << "ultrafast"
       << "-c:a" << "aac" << "-shortest"
       << output_path;

  QProcess proc;
  proc.start(ffmpeg_bin, args);
  if (!proc.waitForStarted(3000)) {
    return false;
  }
  if (!proc.waitForFinished(15000)) {
    proc.kill();
    return false;
  }
  return (proc.exitCode() == 0 && QFile::exists(output_path));
}

// ============================================================================
// 3. In-Memory Project & Timeline Helpers
// ============================================================================

struct TestTimelineContext {
  std::unique_ptr<Project> project;
  Sequence* sequence;
  Track* video_track;
  Track* audio_track;
};

inline TestTimelineContext SetupStandardTimeline() {
  ColorManager::SetUpDefaultConfig();
  TestTimelineContext ctx;
  ctx.project = std::make_unique<Project>();
  ctx.sequence = new Sequence();
  ctx.sequence->setParent(ctx.project.get());

  ctx.video_track = TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kVideo));
  ctx.audio_track = TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kAudio));

  return ctx;
}

// ============================================================================
// 4. In-Memory XML & OTIO Templates
// ============================================================================

inline QString GetMinimalValidFCP7XML() {
  return QStringLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE xmeml>\n"
    "<xmeml version=\"5\">\n"
    "  <sequence id=\"sequence-1\">\n"
    "    <name>E2E Test Sequence</name>\n"
    "    <duration>240</duration>\n"
    "    <rate>\n"
    "      <timebase>24</timebase>\n"
    "      <ntsc>FALSE</ntsc>\n"
    "    </rate>\n"
    "    <media>\n"
    "      <video>\n"
    "        <format>\n"
    "          <samplecharacteristics>\n"
    "            <width>1920</width>\n"
    "            <height>1080</height>\n"
    "          </samplecharacteristics>\n"
    "        </format>\n"
    "        <track>\n"
    "          <clipitem id=\"clip-v1\">\n"
    "            <name>Shot 1</name>\n"
    "            <duration>120</duration>\n"
    "            <rate><timebase>24</timebase><ntsc>FALSE</ntsc></rate>\n"
    "            <start>0</start>\n"
    "            <end>120</end>\n"
    "            <in>0</in>\n"
    "            <out>120</out>\n"
    "            <link>\n"
    "              <linkclipref>clip-v1</linkclipref>\n"
    "              <mediatype>video</mediatype>\n"
    "              <trackindex>1</trackindex>\n"
    "              <clipindex>1</clipindex>\n"
    "            </link>\n"
    "            <link>\n"
    "              <linkclipref>clip-a1</linkclipref>\n"
    "              <mediatype>audio</mediatype>\n"
    "              <trackindex>1</trackindex>\n"
    "              <clipindex>1</clipindex>\n"
    "            </link>\n"
    "          </clipitem>\n"
    "          <clipitem id=\"clip-v2\">\n"
    "            <name>Shot 2</name>\n"
    "            <duration>120</duration>\n"
    "            <rate><timebase>24</timebase><ntsc>FALSE</ntsc></rate>\n"
    "            <start>120</start>\n"
    "            <end>240</end>\n"
    "            <in>0</in>\n"
    "            <out>120</out>\n"
    "          </clipitem>\n"
    "        </track>\n"
    "      </video>\n"
    "      <audio>\n"
    "        <track>\n"
    "          <clipitem id=\"clip-a1\">\n"
    "            <name>Audio 1</name>\n"
    "            <duration>120</duration>\n"
    "            <rate><timebase>24</timebase><ntsc>FALSE</ntsc></rate>\n"
    "            <start>0</start>\n"
    "            <end>120</end>\n"
    "            <in>0</in>\n"
    "            <out>120</out>\n"
    "            <link>\n"
    "              <linkclipref>clip-v1</linkclipref>\n"
    "              <mediatype>video</mediatype>\n"
    "              <trackindex>1</trackindex>\n"
    "              <clipindex>1</clipindex>\n"
    "            </link>\n"
    "            <link>\n"
    "              <linkclipref>clip-a1</linkclipref>\n"
    "              <mediatype>audio</mediatype>\n"
    "              <trackindex>1</trackindex>\n"
    "              <clipindex>1</clipindex>\n"
    "            </link>\n"
    "          </clipitem>\n"
    "        </track>\n"
    "      </audio>\n"
    "    </media>\n"
    "    <marker>\n"
    "      <name>Intro Marker</name>\n"
    "      <comment>Scene start</comment>\n"
    "      <in>48</in>\n"
    "      <out>48</out>\n"
    "    </marker>\n"
    "  </sequence>\n"
    "</xmeml>\n"
  );
}

inline QString GetNTSCFCP7XML() {
  return QStringLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE xmeml>\n"
    "<xmeml version=\"5\">\n"
    "  <sequence id=\"sequence-ntsc\">\n"
    "    <name>NTSC 23.976 Sequence</name>\n"
    "    <duration>240</duration>\n"
    "    <rate>\n"
    "      <timebase>24</timebase>\n"
    "      <ntsc>TRUE</ntsc>\n"
    "    </rate>\n"
    "    <media>\n"
    "      <video><track></track></video>\n"
    "      <audio><track></track></audio>\n"
    "    </media>\n"
    "  </sequence>\n"
    "</xmeml>\n"
  );
}

inline QString GetMalformedFCP7XML() {
  return QStringLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<xmeml version=\"5\">\n"
    "  <sequence id=\"incomplete\">\n"
    "    <name>Broken</name>\n"
    "    <duration>100\n"
  );
}

inline QString GetMinimalValidOTIO() {
  return QStringLiteral(
    "{\n"
    "  \"OTIO_SCHEMA\": \"Timeline.1\",\n"
    "  \"name\": \"E2E OTIO Timeline\",\n"
    "  \"global_start_time\": null,\n"
    "  \"tracks\": {\n"
    "    \"OTIO_SCHEMA\": \"Stack.1\",\n"
    "    \"name\": \"tracks\",\n"
    "    \"children\": [\n"
    "      {\n"
    "        \"OTIO_SCHEMA\": \"Track.1\",\n"
    "        \"name\": \"Video 1\",\n"
    "        \"kind\": \"Video\",\n"
    "        \"children\": [\n"
    "          {\n"
    "            \"OTIO_SCHEMA\": \"Clip.1\",\n"
    "            \"name\": \"Clip 1\",\n"
    "            \"source_range\": {\n"
    "              \"OTIO_SCHEMA\": \"TimeRange.1\",\n"
    "              \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 24.0, \"value\": 0.0},\n"
    "              \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 24.0, \"value\": 48.0}\n"
    "            }\n"
    "          }\n"
    "        ]\n"
    "      }\n"
    "    ]\n"
    "  }\n"
    "}\n"
  );
}

} // namespace e2e
} // namespace olive

#endif // OLIVE_E2E_FIXTURES_H
