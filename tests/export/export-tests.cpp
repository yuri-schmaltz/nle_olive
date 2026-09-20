#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include "codec/ffmpeg/ffmpegencoder.h"
#include "testutil.h"
namespace olive {
OLIVE_ADD_TEST(PCMExportPreservesFinalPartialFrame)
{
  QTemporaryDir directory;
  OLIVE_ASSERT(directory.isValid());
  const AudioParams input_params(48000, AV_CH_LAYOUT_STEREO, SampleFormat::F32P);
  const AudioParams output_params(48000, AV_CH_LAYOUT_STEREO, SampleFormat::F32);
  const int count = 48123; // deliberately not a codec frame multiple
  SampleBuffer audio(input_params, size_t(count));
  for (int i = 0; i < count; i++) {
    audio.data(0)[i] = float(std::sin(i * 0.03) * 0.25);
    audio.data(1)[i] = float(std::cos(i * 0.07) * 0.5);
  }
  EncodingParams params;
  params.SetFilename(directory.filePath("output.wav"));
  params.set_format(ExportFormat::kFormatWAV);
  params.EnableAudio(output_params, ExportCodec::kCodecPCM);
  FFmpegEncoder encoder(params);
  OLIVE_ASSERT(encoder.Open());
  OLIVE_ASSERT(encoder.WriteAudio(audio));
  encoder.Close();
  OLIVE_ASSERT(encoder.GetError().isEmpty());

  // Decode with a separate executable, independently of Olive's decoder.
  QProcess decode;
  decode.start(QStringLiteral(OLIVE_TEST_FFMPEG), {"-v", "error", "-i", params.filename(),
               "-f", "f32le", "-acodec", "pcm_f32le", "-"});
  OLIVE_ASSERT(decode.waitForFinished(30000));
  OLIVE_ASSERT(decode.exitStatus() == QProcess::NormalExit && decode.exitCode() == 0);
  const QByteArray raw = decode.readAllStandardOutput();
  OLIVE_ASSERT_EQUAL(raw.size(), count * 2 * int(sizeof(float)));
  for (int i = 0; i < count; i++) {
    for (int c = 0; c < 2; c++) {
      float value;
      memcpy(&value, raw.constData() + (i * 2 + c) * sizeof(float), sizeof(float));
      OLIVE_ASSERT(std::fabs(value - audio.data(c)[i]) < 1e-6f);
    }
  }
  OLIVE_TEST_END;
}
OLIVE_ADD_TEST(ExportInvalidDestinationFails)
{
  QTemporaryDir directory;
  EncodingParams params;
  params.SetFilename(directory.filePath("missing/output.wav"));
  params.set_format(ExportFormat::kFormatWAV);
  params.EnableAudio(AudioParams(48000, AV_CH_LAYOUT_STEREO, SampleFormat::F32), ExportCodec::kCodecPCM);
  FFmpegEncoder encoder(params);
  OLIVE_ASSERT(!encoder.Open());
  OLIVE_ASSERT(!encoder.GetError().isEmpty());
  encoder.Close();
  OLIVE_TEST_END;
}
}
