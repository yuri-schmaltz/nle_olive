/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2026 Olive Team

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

#ifndef SCENECUTTASK_H
#define SCENECUTTASK_H

#include <QPointer>
#include <QString>
#include <QVector>

#include <olive/core/util/rational.h>

#include "node/block/clip/clip.h"
#include "task/scenecut/scenecutdetector.h"
#include "task/task.h"

namespace olive {

/**
 * @brief Background task that decodes video frames via dedicated CPU FFmpeg decoding
 * and detects shot transitions/cuts using SceneCutDetector.
 *
 * Runs inside TaskManager's thread pool without touching DecoderCache or RenderManager,
 * ensuring zero stutter during active timeline playback.
 */
class SceneCutTask : public Task
{
  Q_OBJECT
public:
  /**
   * @brief Construct SceneCutTask for a timeline ClipBlock.
   *
   * Must be called on the GUI thread. Safely extracts media parameters from clip
   * before worker thread execution.
   */
  explicit SceneCutTask(ClipBlock* clip, const SceneCutConfig& config = SceneCutConfig());

  /**
   * @brief Construct SceneCutTask directly from media parameters.
   *
   * Ideal for automated unit tests, headless CLI workflows, and non-timeline analysis.
   */
  SceneCutTask(const QString& filename,
               int stream_index,
               const core::rational& media_in,
               const core::rational& media_out,
               const core::rational& frame_rate,
               const SceneCutConfig& config = SceneCutConfig(),
               ClipBlock* clip = nullptr);

  virtual ~SceneCutTask() override = default;

  /**
   * @brief Target clip associated with this detection task
   */
  ClipBlock* clip() const { return clip_; }

  /**
   * @brief Active media path being analyzed
   */
  const QString& filename() const { return filename_; }

  /**
   * @brief Video stream index
   */
  int stream_index() const { return stream_index_; }

  /**
   * @brief Start of media analysis window
   */
  const core::rational& media_in() const { return media_in_; }

  /**
   * @brief End of media analysis window
   */
  const core::rational& media_out() const { return media_out_; }

  /**
   * @brief Retrieve detected cut timestamps synchronously upon completion
   */
  const QVector<core::rational>& detected_cuts() const { return detected_cuts_; }

signals:
  /**
   * @brief Emitted when scene cut analysis successfully finishes.
   *
   * Queued to the GUI thread. Passes detected media timestamps in ascending order.
   */
  void SceneCutsDetected(const QVector<olive::core::rational>& cut_times);

protected:
  /**
   * @brief Work function executed on background thread in TaskManager's QThreadPool.
   */
  virtual bool Run() override;

private:
  static QString FFmpegError(int error_code);

  QPointer<ClipBlock> clip_;
  QString filename_;
  int stream_index_{0};
  core::rational media_in_{0};
  core::rational media_out_{0};
  core::rational frame_rate_{30, 1};
  SceneCutConfig config_;

  QVector<core::rational> detected_cuts_;
};

} // namespace olive

#endif // SCENECUTTASK_H
