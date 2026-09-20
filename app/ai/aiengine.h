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

#ifndef AIENGINE_H
#define AIENGINE_H

#include <memory>
#include <QString>
#include <QVector>
#include <QObject>

#include <olive/core/util/rational.h>
#include "task/task.h"

using namespace olive::core;

namespace olive {

/**
 * @brief Structured subtitle segment produced by AI transcription / speech-to-text models
 */
struct AISubtitleSegment {
  rational start_time;
  rational end_time;
  QString text;
  float confidence = 1.0f;
};

/**
 * @brief Abstract native C++ interface for AI model inference (ONNX, NCNN, GGML/Whisper)
 */
class AIEngine
{
public:
  enum Backend {
    kBackendCPU,
    kBackendCUDA,
    kBackendVulkan,
    kBackendOpenVINO
  };

  enum ModelType {
    kModelSpeechToText,
    kModelSceneDetection,
    kModelAudioDenoise,
    kModelUpscale
  };

  AIEngine() : backend_(kBackendCPU), loaded_(false) {}
  virtual ~AIEngine() = default;

  virtual bool LoadModel(const QString &model_path, Backend backend = kBackendCPU) = 0;
  virtual void UnloadModel() = 0;
  virtual bool IsLoaded() const { return loaded_; }

  Backend GetBackend() const { return backend_; }
  void SetBackend(Backend b) { backend_ = b; }

  virtual ModelType GetType() const = 0;

protected:
  Backend backend_;
  bool loaded_;
  QString model_path_;
};

/**
 * @brief Speech-to-Text inference engine contract for subtitle generation
 */
class AISpeechToTextEngine : public AIEngine
{
public:
  virtual ModelType GetType() const override { return kModelSpeechToText; }

  virtual QVector<AISubtitleSegment> Transcribe(const float *audio_pcm,
                                                size_t sample_count,
                                                int sample_rate,
                                                CancelableObject *cancelable = nullptr) = 0;
};

/**
 * @brief Background Task for executing speech recognition and populating subtitles
 */
class AutoSubtitleTask : public Task
{
  Q_OBJECT
public:
  AutoSubtitleTask(std::shared_ptr<AISpeechToTextEngine> engine,
                   const QVector<float> &audio_samples,
                   int sample_rate,
                   const QString &title = QStringLiteral("Auto-generate Subtitles")) :
    engine_(engine),
    samples_(audio_samples),
    sample_rate_(sample_rate)
  {
    SetTitle(title);
  }

  const QVector<AISubtitleSegment>& GetResults() const
  {
    return segments_;
  }

protected:
  virtual bool Run() override
  {
    if (!engine_ || !engine_->IsLoaded()) {
      SetError(tr("AI Speech-to-text model is not initialized or loaded."));
      return false;
    }

    if (samples_.isEmpty() || sample_rate_ <= 0) {
      SetError(tr("Invalid audio input data."));
      return false;
    }

    emit ProgressChanged(0.1);
    segments_ = engine_->Transcribe(samples_.constData(), samples_.size(), sample_rate_, this);

    if (IsCancelled()) {
      return false;
    }

    emit ProgressChanged(1.0);
    return true;
  }

private:
  std::shared_ptr<AISpeechToTextEngine> engine_;
  QVector<float> samples_;
  int sample_rate_;
  QVector<AISubtitleSegment> segments_;
};

} // namespace olive

#endif // AIENGINE_H
