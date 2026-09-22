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

#include "scenecutdialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace olive {

#define super QDialog

SceneCutDialog::SceneCutDialog(const QVector<ClipBlock*>& clips, const core::rational& timebase, QWidget* parent)
  : super(parent),
    clips_(clips),
    timebase_(timebase)
{
  setWindowTitle(tr("Scene Cut Detection & Auto-Split"));
  setMinimumWidth(380);

  QVBoxLayout* main_layout = new QVBoxLayout(this);

  // Group Box: Detection Parameters
  QGroupBox* param_group = new QGroupBox(tr("Detection Parameters"), this);
  QGridLayout* param_layout = new QGridLayout(param_group);
  int row = 0;

  // 1. Sensitivity / Threshold
  param_layout->addWidget(new QLabel(tr("Threshold / Sensitivity:"), this), row, 0);
  threshold_slider_ = new FloatSlider(this);
  threshold_slider_->SetMinimum(0.05);
  threshold_slider_->SetMaximum(0.95);
  threshold_slider_->SetValue(0.35);
  threshold_slider_->SetDisplayType(FloatSlider::kPercentage);
  param_layout->addWidget(threshold_slider_, row, 1);
  row++;

  // 2. Minimum Scene Duration
  param_layout->addWidget(new QLabel(tr("Min Scene Length (Frames):"), this), row, 0);
  min_frames_spin_ = new QSpinBox(this);
  min_frames_spin_->setRange(3, 120);
  min_frames_spin_->setValue(12);
  param_layout->addWidget(min_frames_spin_, row, 1);
  row++;

  // 3. Subsampling Stride
  param_layout->addWidget(new QLabel(tr("Analysis Quality:"), this), row, 0);
  stride_combo_ = new QComboBox(this);
  stride_combo_->addItem(tr("Auto (Adaptive)"), 0);
  stride_combo_->addItem(tr("High (2x Subsampling)"), 2);
  stride_combo_->addItem(tr("Standard (4x Subsampling)"), 4);
  stride_combo_->addItem(tr("Fast (8x Subsampling)"), 8);
  stride_combo_->setCurrentIndex(0);
  param_layout->addWidget(stride_combo_, row, 1);
  row++;

  // 4. Flash Suppression
  flash_suppression_box_ = new QCheckBox(tr("Suppress Camera Flash & Strobes"), this);
  flash_suppression_box_->setChecked(true);
  param_layout->addWidget(flash_suppression_box_, row, 0, 1, 2);
  row++;

  // 5. Split Linked Audio
  split_audio_box_ = new QCheckBox(tr("Split Linked Audio Tracks Synchronously"), this);
  split_audio_box_->setChecked(true);
  param_layout->addWidget(split_audio_box_, row, 0, 1, 2);

  main_layout->addWidget(param_group);

  // Button Box
  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Detect & Split"));
  connect(buttons, &QDialogButtonBox::accepted, this, &SceneCutDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &SceneCutDialog::reject);
  main_layout->addWidget(buttons);
}

SceneCutConfig SceneCutDialog::GetConfig() const
{
  SceneCutConfig cfg;
  cfg.base_threshold = threshold_slider_->GetValue();
  cfg.min_scene_frames = min_frames_spin_->value();
  cfg.subsample_stride = stride_combo_->currentData().toInt();
  cfg.enable_flash_suppression = flash_suppression_box_->isChecked();
  return cfg;
}

bool SceneCutDialog::SplitLinkedAudio() const
{
  return split_audio_box_->isChecked();
}

void SceneCutDialog::accept()
{
  super::accept();
}

} // namespace olive
