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

#ifndef SCENECUTDIALOG_H
#define SCENECUTDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QSpinBox>
#include <QVector>

#include <olive/core/util/rational.h>

#include "node/block/clip/clip.h"
#include "task/scenecut/scenecutdetector.h"
#include "widget/slider/floatslider.h"

namespace olive {

class SceneCutDialog : public QDialog {
  Q_OBJECT
public:
  explicit SceneCutDialog(const QVector<ClipBlock*>& clips, const core::rational& timebase, QWidget* parent = nullptr);

  SceneCutConfig GetConfig() const;
  bool SplitLinkedAudio() const;

public slots:
  virtual void accept() override;

private:
  QVector<ClipBlock*> clips_;
  core::rational timebase_;

  FloatSlider* threshold_slider_{nullptr};
  QSpinBox* min_frames_spin_{nullptr};
  QComboBox* stride_combo_{nullptr};
  QCheckBox* flash_suppression_box_{nullptr};
  QCheckBox* split_audio_box_{nullptr};
};

} // namespace olive

#endif // SCENECUTDIALOG_H
