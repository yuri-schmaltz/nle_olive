# Engineering Design & Implementation Blueprint: Timeline Auto-Split Integration & Automated Unit Tests (Milestone M3)

**Author**: Explorer 3 (`teamwork_preview_explorer_m3_3`)  
**Date**: September 20, 2026  
**Target Codebase**: Olive Video Editor (`/home/yuri/Documentos/olive`)  
**Scope**: Timeline UI Integration, Time Space Conversion, `BlockSplitPreservingLinksCommand` Mechanics, and Comprehensive Automated Unit Test Suites (`tests/task/scenecut-tests.cpp`, `tests/timeline/scenecut-split-tests.cpp`)  
**Standard**: Native C++17, Pure Qt6, DAG-preserving, Zero Memory Leaks (ASan compliant)

---

## 1. Executive Summary & Architectural Overview

Milestone **M3** completes Olive's Scene Cut Detection and Auto-Split capability (Requirement **R2**). While Explorer 1 designed the high-throughput, allocation-free `SceneCutDetector` engine and Explorer 2 architected the headless `SceneCutTask` decoding pipeline, Explorer 3's mission spans the user-facing and editorial execution layer:
1. **Timeline UI & Action Integration**: Wiring "Auto-Split Scenes..." into Olive's menu hierarchy (`MenuShared`), clip context menus (`TimelineWidget`), and providing the modal dialog (`SceneCutDialog`) for threshold and sensitivity parameter acquisition.
2. **Mathematical Time Conversion**: Formulating and verifying sample-accurate mapping from media frame presentation timestamps ($T_{media}$) into sequence timeline timestamps ($T_{seq}$), taking full account of clip speed multipliers, reverse playback flags, timebase quantization, and boundary filtering.
3. **Command Execution & Critical Subsystem Fix**: Executing multi-point splits on the main GUI thread via `BlockSplitPreservingLinksCommand`. During this investigation, an **inherent limitation/bug in the existing `BlockSplitPreservingLinksCommand::prepare()` was discovered**: it only ever worked for single cut points; when multiple cut timestamps were passed, subsequent cuts failed because the command did not update its tracking pointer to the newly generated tail block. We provide the mathematical proof and exact drop-in fix.
4. **Automated Unit Testing Suites**:
   - `tests/task/scenecut-tests.cpp`: Unit tests validating synthetic frame analysis (black/white, chroma shift, flash rejection, cancellation, stride padding, EOF flush).
   - `tests/timeline/scenecut-split-tests.cpp`: Unit tests validating multi-point cut execution, video-audio dual-track link preservation, out-of-bounds cut filtering, time conversion with speed/reverse, and full undo/redo state restoration.
   - Complete CMake integration via `olive_add_test` in `tests/CMakeLists.txt`.

---

## 2. Technical Investigation 1: Timeline UI Integration

### 2.1 UI Action Locations

To adhere to Olive's existing UI architecture (as seen in `Speed/Duration` and `Split at Playhead`):
1. **Main Edit Menu (`MainMenu` / `MenuShared`)**:
   - In `app/widget/menu/menushared.h`: Declare `QAction* edit_detect_scenes_item_;`
   - In `app/widget/menu/menushared.cpp`:
     - In constructor: `edit_detect_scenes_item_ = Menu::CreateItem(this, "detectscenecuts", this, &MenuShared::DetectSceneCutsTriggered, tr("Ctrl+Alt+K"));`
     - In `AddItemsForEditMenu(Menu *m, bool for_clips)`: Add `m->addAction(edit_detect_scenes_item_);` when `for_clips == true`.
     - In `AddItemsForClipEditMenu(Menu *m)`: Add `m->addAction(edit_detect_scenes_item_);`.
     - In `Retranslate()`: `edit_detect_scenes_item_->setText(tr("Auto-Split &Scenes..."));`
2. **Timeline Clip Context Menu (`TimelineWidget::ShowContextMenu()`)**:
   - In `app/widget/timelinewidget/timelinewidget.cpp:1245`:
     Because `MenuShared::instance()->AddItemsForEditMenu(&menu, true);` is already invoked when `!selected.isEmpty()`, `edit_detect_scenes_item_` automatically appears in the clip context menu!
   - To make it even more accessible, we also place a direct action in the clip-specific section (around line 1305):
     ```cpp
     QAction* scenecut_action = menu.addAction(tr("Auto-Split Scenes..."));
     connect(scenecut_action, &QAction::triggered, this, &TimelineWidget::ShowSceneCutDialogForSelectedClips);
     ```
3. **Focus Dispatcher (`MenuShared::DetectSceneCutsTriggered`)**:
   ```cpp
   void MenuShared::DetectSceneCutsTriggered()
   {
     TimelinePanel* timeline = PanelManager::instance()->MostRecentlyFocused<TimelinePanel>();
     if (timeline != nullptr) {
       timeline->ShowSceneCutDialogForSelectedClips();
     }
   }
   ```
4. **`TimelinePanel` Forwarder (`app/panel/timeline/timeline.h`)**:
   ```cpp
   void ShowSceneCutDialogForSelectedClips()
   {
     timeline_widget()->ShowSceneCutDialogForSelectedClips();
   }
   ```

---

### 2.2 Parameter Acquisition Dialog (`SceneCutDialog`)

Olive uses clean, native Qt dialogs (e.g. `SpeedDurationDialog` in `app/dialog/speedduration/`). We design `SceneCutDialog` following the exact same conventions:
- **Location**: `app/dialog/scenecut/scenecutdialog.h` and `scenecutdialog.cpp`
- **Controls**:
  1. **Threshold / Sensitivity Slider (`FloatSlider`)**: Range $0.10$ to $0.90$, default $0.35$. Formatted as percentage ($10\%$ to $90\%$).
  2. **Minimum Scene Duration (`QSpinBox`)**: Range $3$ to $120$ frames, default $12$ frames (approx. 0.5s at 24fps).
  3. **Subsampling Stride (`QComboBox`)**: Options: `Auto (Recommended)`, `1x (Full Quality / Slow)`, `2x`, `4x (Fast)`, `8x (Ultra Fast)`.
  4. **Flash / Strobe Suppression (`QCheckBox`)**: Default checked (`true`). Suppresses 1-frame camera flashes and lightning spikes.
  5. **Split Linked Audio (`QCheckBox`)**: Default checked (`true`). Ensures linked audio tracks are razor-split synchronously with video cuts.
  6. **Button Box (`QDialogButtonBox`)**: "Start Analysis" (Ok) and "Cancel".

#### Header Blueprint: `app/dialog/scenecut/scenecutdialog.h`
```cpp
#ifndef SCENECUTDIALOG_H
#define SCENECUTDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QSpinBox>
#include <QVector>

#include "node/block/clip/clip.h"
#include "task/scenecut/scenecutdetector.h"
#include "widget/slider/floatslider.h"

namespace olive {

class SceneCutDialog : public QDialog {
  Q_OBJECT
public:
  explicit SceneCutDialog(const QVector<ClipBlock*>& clips, const rational& timebase, QWidget* parent = nullptr);

  SceneCutConfig GetConfig() const;
  bool SplitLinkedAudio() const;

public slots:
  virtual void accept() override;

private:
  QVector<ClipBlock*> clips_;
  rational timebase_;

  FloatSlider* threshold_slider_{nullptr};
  QSpinBox* min_frames_spin_{nullptr};
  QComboBox* stride_combo_{nullptr};
  QCheckBox* flash_suppression_box_{nullptr};
  QCheckBox* split_audio_box_{nullptr};
};

} // namespace olive

#endif // SCENECUTDIALOG_H
```

#### Implementation Blueprint: `app/dialog/scenecut/scenecutdialog.cpp`
```cpp
#include "scenecutdialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace olive {

#define super QDialog

SceneCutDialog::SceneCutDialog(const QVector<ClipBlock*>& clips, const rational& timebase, QWidget* parent)
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
  threshold_slider_->SetRange(0.05, 0.95);
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
```

---

## 3. Technical Investigation 2: Time Space Conversion & Boundary Filtering

A video clip on Olive's timeline exists across three distinct mathematical coordinate spaces:
1. **Media Space** ($T_{media}$): The presentation timestamp within the source container file $[0, \text{Footage Length}]$. The headless FFmpeg decoder identifies cuts at media timestamps:
   $$T_{media} = \text{frame\_pts} \times \text{stream\_time\_base}$$
2. **Clip-Local Sequence Space** ($T_{clip}$): The relative timeline offset within the clip block $[0, \text{clip->length()}]$.
3. **Sequence Timeline Space** ($T_{seq}$): The absolute timeline sequence timestamp $[\text{clip->in()}, \text{clip->out()}]$.

### 3.1 Mathematical Mapping Formulas

The conversion from media time $T_{media}$ to timeline sequence time $T_{seq}$ must account for:
- Source media in-point offset: $\text{media\_in} = \text{clip->media\_in()}$
- Playback speed factor: $S = \text{clip->speed()}$
- Direction of playback: $R = \text{clip->reverse()} \in \{\text{false}, \text{true}\}$
- Clip start time on timeline: $\text{clip\_in} = \text{clip->in()}$

From `app/node/block/clip/clip.cpp:201-224`, `ClipBlock::MediaToSequenceTime(T_media)` computes:
$$T_{clip} = \frac{T_{media} - \text{media\_in}}{S}$$
If reverse playback is enabled ($R == \text{true}$):
$$T_{clip} = \text{clip->length()} - T_{clip} = \text{clip->length()} - \frac{T_{media} - \text{media\_in}}{S}$$

Then, the absolute timeline sequence time is:
$$T_{seq} = \text{clip\_in} + T_{clip} = \text{clip->in()} + \text{clip->MediaToSequenceTime}(T_{media})$$

### 3.2 Sequence Timebase Quantization & Boundary Filtering

In non-linear video editing, cuts must fall on exact sequence frame boundaries:
$$T_{snapped} = \text{Timecode::snap\_time\_to\_timebase}(T_{seq}, \text{sequence->timebase()})$$

**Boundary Invariant**:
In Olive's `BlockSplitCommand::redo()`, line 42 asserts:
$$\text{Q\_ASSERT}(point\_ > block\_->in() \ \&\& \ point\_ < block\_->out());$$
Thus:
1. Any cut where $T_{snapped} \le \text{clip->in()}$ is **strictly invalid** (cannot cut before or at start).
2. Any cut where $T_{snapped} \ge \text{clip->out()}$ is **strictly invalid** (cannot cut at or after end).
3. If multiple detected cuts quantize to the same sequence frame, duplicate timestamps must be eliminated.
4. Timestamps must be sorted in strictly increasing chronological order:
   $$t_0 < t_1 < \dots < t_{M-1}$$
   *(Note: when $R == \text{true}$, forward media timestamps yield descending sequence timestamps; sorting automatically restores proper chronological sequence order).*

#### Time Conversion Implementation Blueprint
```cpp
QList<rational> ConvertAndFilterMediaCutsToSequence(ClipBlock* clip,
                                                   const QVector<rational>& media_cuts,
                                                   const rational& timebase)
{
  QList<rational> seq_cuts;
  if (!clip || media_cuts.isEmpty()) return seq_cuts;

  const rational clip_in = clip->in();
  const rational clip_out = clip->out();

  for (const rational& m_time : media_cuts) {
    // 1. Convert media time to clip-local sequence time
    rational rel_time = clip->MediaToSequenceTime(m_time);
    if (rel_time.isNaN()) continue;

    // 2. Map to absolute timeline sequence time
    rational seq_time = clip_in + rel_time;

    // 3. Snap to sequence timebase
    rational snapped = Timecode::snap_time_to_timebase(seq_time, timebase);

    // 4. Boundary guard: strictly within (clip_in, clip_out)
    if (snapped > clip_in && snapped < clip_out) {
      if (!seq_cuts.contains(snapped)) {
        seq_cuts.append(snapped);
      }
    }
  }

  // 5. Sort ascending
  std::sort(seq_cuts.begin(), seq_cuts.end());
  return seq_cuts;
}
```

---

## 4. Technical Investigation 3: Command Execution & Critical Bug Fix

### 4.1 Dispatching from Background Task to GUI Thread

The background task emits `SceneCutsDetected(media_cuts)` when decoding completes.
Because Olive requires that **all DAG node graph modifications and UndoStack pushes occur strictly on the main GUI thread**:
```cpp
void TimelineWidget::ShowSceneCutDialogForSelectedClips()
{
  QVector<ClipBlock*> clips;
  for (Block* b : selected_blocks_) {
    if (auto c = dynamic_cast<ClipBlock*>(b)) {
      clips.append(c);
    }
  }
  if (clips.isEmpty()) return;

  SceneCutDialog dialog(clips, timebase(), this);
  if (dialog.exec() != QDialog::Accepted) return;

  SceneCutConfig config = dialog.GetConfig();
  bool split_linked_audio = dialog.SplitLinkedAudio();

  for (ClipBlock* clip : clips) {
    Footage* footage = GetFootageFromClip(clip);
    if (!footage) continue;

    QString filename = footage->filename();
    int stream_idx = footage->GetVideoParams().stream_index();
    rational media_in = clip->media_in();
    rational media_out = clip->media_in() + clip->length() * clip->speed();

    SceneCutTask* task = new SceneCutTask(filename, stream_idx, media_in, media_out, config);

    // QPointer prevents crashing if the user deletes the clip or closes project while task runs
    QPointer<ClipBlock> safe_clip(clip);
    QPointer<TimelineWidget> safe_widget(this);

    connect(task, &SceneCutTask::SceneCutsDetected, this,
            [safe_clip, safe_widget, split_linked_audio](const QVector<rational>& cuts) {
      if (!safe_clip || !safe_widget || !safe_clip->track() || !safe_clip->project()) {
        return; // Clip was removed or timeline closed; discard safely
      }
      safe_widget->ApplySceneCutsToClip(safe_clip.data(), cuts, split_linked_audio);
    }, Qt::QueuedConnection);

    TaskManager::instance()->AddTask(task);
  }
}
```

---

### 4.2 The Multi-Point Split Bug in `BlockSplitPreservingLinksCommand`

Olive has a dedicated command class for multi-point splitting: `BlockSplitPreservingLinksCommand` (`app/timeline/timelineundosplit.h`).
However, prior to this investigation, `BlockSplitPreservingLinksCommand` was **only ever used with single timestamps** `{point_}` or `{playhead_time}` (in `timelineundogeneral.cpp:326`, `multicamwidget.cpp:136`, `timelinewidget.cpp:454`, and `tool/razor.cpp:96`).

#### The Flaw in `prepare()` (`app/timeline/timelineundosplit.cpp:112-140`):
```cpp
void BlockSplitPreservingLinksCommand::prepare()
{
  std::sort(times_.begin(), times_.end());
  splits_.resize(times_.size());

  for (int i=0; i<times_.size(); i++) {
    const rational& time = times_.at(i);
    QVector<Block*> splits(blocks_.size());

    for (int j=0; j<blocks_.size(); j++) {
      Block* b = blocks_.at(j); // <--- FATAL FLAW: 'b' ALWAYS fetches from blocks_[j]!

      if (b->in() < time && b->out() > time) {
        BlockSplitCommand* split_command = new BlockSplitCommand(b, time);
        split_command->redo_now();
        splits.replace(j, split_command->new_block());
        commands_.append(split_command);
      } else {
        splits.replace(j, nullptr);
      }
    }
    splits_.replace(i, splits);
  }
  // Relinking logic...
}
```

#### Step-by-Step Failure Trace:
1. Assume `clip` spans $[0, 100]$. Cut timestamps: $T = \{20, 45, 80\}$.
2. **Iteration $i = 0$ ($time = 20$)**:
   - $b = \text{blocks\_}[0]$ (the initial clip).
   - $b->\text{in}() < 20 \ \&\& \ b->\text{out}() > 20$ ($0 < 20 < 100$) $\implies$ **TRUE**.
   - `BlockSplitCommand(clip, 20)` executes `redo_now()`.
   - `clip` length is shortened to $20$ ($[0, 20]$).
   - `split_command->new_block()` is created with length $80$ ($[20, 100]$).
3. **Iteration $i = 1$ ($time = 45$)**:
   - $b = \text{blocks\_}[0]$ $\implies$ **`b` is STILL the original `clip`**!
   - But `clip` was already truncated to $[0, 20]$!
   - Condition: $b->\text{in}() < 45 \ \&\& \ b->\text{out}() > 45$ $\implies 0 < 45 \ \&\& \ 20 > 45$ is **FALSE**!
   - Result: `splits.replace(0, nullptr)`. **Cut at 45 is completely skipped!**
4. **Iteration $i = 2$ ($time = 80$)**:
   - Condition: $0 < 80 \ \&\& \ 20 > 80$ is **FALSE**!
   - Result: `splits.replace(0, nullptr)`. **Cut at 80 is completely skipped!**
5. **Final Result**: Only the first cut ($t = 20$) is made. All subsequent cuts are lost!

#### The Drop-In Fix for `app/timeline/timelineundosplit.cpp`:
To correctly apply sequential splits in ascending order, the command must track the **current tail block** (`current_blocks[j]`) for each chain:
```cpp
void BlockSplitPreservingLinksCommand::prepare()
{
  std::sort(times_.begin(), times_.end());

  splits_.resize(times_.size());

  // Track the active tail block for each track as splits are applied
  QVector<Block*> current_blocks = blocks_;

  for (int i=0; i<times_.size(); i++) {
    const rational& time = times_.at(i);
    QVector<Block*> splits(current_blocks.size());

    for (int j=0; j<current_blocks.size(); j++) {
      Block* b = current_blocks.at(j);

      if (b && b->in() < time && b->out() > time) {
        BlockSplitCommand* split_command = new BlockSplitCommand(b, time);
        split_command->redo_now();
        splits.replace(j, split_command->new_block());
        commands_.append(split_command);

        // Next cut time in ascending order will fall into this newly created tail block
        current_blocks[j] = split_command->new_block();
      } else {
        splits.replace(j, nullptr);
      }
    }

    splits_.replace(i, splits);
  }

  // Relink all split segments across tracks
  for (int i=0; i<blocks_.size(); i++) {
    Block* a = blocks_.at(i);

    for (int j=i+1; j<blocks_.size(); j++) {
      Block* b = blocks_.at(j);

      if (Block::AreLinked(a, b)) {
        foreach (const QVector<Block*>& split_list, splits_) {
          if (split_list.at(i) && split_list.at(j)) {
            NodeLinkCommand* blc = new NodeLinkCommand(split_list.at(i), split_list.at(j), true);
            blc->redo_now();
            commands_.append(blc);
          }
        }
      }
    }
  }
}
```

### 4.3 Link Preservation & Reversibility Proof
- **Link Preservation**: For every split slice $k$, `split_list.at(i)` (video block) and `split_list.at(j)` (audio block) are explicitly linked via `NodeLinkCommand`.
- **Undo Operation**: `commands_` are undone in reverse order ($commands.size() - 1$ down to $0$). All `NodeLinkCommand` instances are undone first, followed by each `BlockSplitCommand` merging tail blocks back into the head blocks, flawlessly restoring the original uninterrupted blocks and original links.
- **Redo Operation**: `commands_` are redone in ascending order ($0$ to $size - 1$), cleanly recreating all sub-blocks and re-establishing all pairwise links.

---

## 5. Technical Investigation 4: Automated Test Suite 1 — `tests/task/scenecut-tests.cpp`

### 5.1 Test Specification & Rationale
`tests/task/scenecut-tests.cpp` provides fast, memory-isolated testing of the computer vision engine without depending on external media files or GPU contexts.

### 5.2 Complete Source Code: `tests/task/scenecut-tests.cpp`
```cpp
#include "testutil.h"

#include <vector>
#include <cstdint>

#include "task/scenecut/scenecutdetector.h"

namespace olive {

// Helper: Generates uniform planar YUV420P frame
static void FillSyntheticYUV420P(std::vector<uint8_t>& y_plane,
                                 std::vector<uint8_t>& u_plane,
                                 std::vector<uint8_t>& v_plane,
                                 int width, int height,
                                 uint8_t y_val, uint8_t u_val, uint8_t v_val)
{
  y_plane.assign(width * height, y_val);
  u_plane.assign((width / 2) * (height / 2), u_val);
  v_plane.assign((width / 2) * (height / 2), v_val);
}

OLIVE_ADD_TEST(SceneCutDetector_IdenticalFrames)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> y, u, v;
  FillSyntheticYUV420P(y, u, v, W, H, 16, 128, 128); // TV black

  const uint8_t* data[4] = { y.data(), u.data(), v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Feed 25 identical frames; detector should produce zero cuts
  for (int i = 0; i < 25; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_HardCutBlackToWhite)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> white_y, white_u, white_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(white_y, white_u, white_v, W, H, 235, 128, 128);

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* white_data[4] = { white_y.data(), white_u.data(), white_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Frames 0-4: Black
  for (int i = 0; i <= 4; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  // Frame 5: White (Candidate, queued in lookahead)
  SceneCutResult res5 = detector.ProcessPlanar(5, white_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res5.cut_detected);

  // Frame 6: White continues -> Genuine cut confirmed at frame 5!
  SceneCutResult res6 = detector.ProcessPlanar(6, white_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(res6.cut_detected);
  OLIVE_ASSERT_EQUAL(res6.cut_frame_index, 5);
  OLIVE_ASSERT(res6.score > 0.80);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_ChromaShiftDisambiguation)
{
  SceneCutConfig config;
  config.base_threshold = 0.20;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = false;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> red_y, red_u, red_v;
  std::vector<uint8_t> blue_y, blue_u, blue_v;
  // Identical luma Y = 128, but contrasting chroma
  FillSyntheticYUV420P(red_y, red_u, red_v, W, H, 128, 90, 240);
  FillSyntheticYUV420P(blue_y, blue_u, blue_v, W, H, 128, 240, 90);

  const uint8_t* red_data[4] = { red_y.data(), red_u.data(), red_v.data(), nullptr };
  const uint8_t* blue_data[4] = { blue_y.data(), blue_u.data(), blue_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i < 5; ++i) {
    detector.ProcessPlanar(i, red_data, linesize, W, H, PixelFormatType::YUV420P);
  }

  SceneCutResult cut = detector.ProcessPlanar(5, blue_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(cut.cut_detected);
  OLIVE_ASSERT_EQUAL(cut.cut_frame_index, 5);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_StrobeFlashRejection)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> flash_y, flash_u, flash_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(flash_y, flash_u, flash_v, W, H, 255, 128, 128); // 1-frame strobe flash

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* flash_data[4] = { flash_y.data(), flash_u.data(), flash_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Frames 0-4: Black
  for (int i = 0; i <= 4; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  // Frame 5: Flash (1 frame)
  SceneCutResult res5 = detector.ProcessPlanar(5, flash_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res5.cut_detected);

  // Frame 6: Returns to Black -> Flash MUST be suppressed!
  SceneCutResult res6 = detector.ProcessPlanar(6, black_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res6.cut_detected);

  // Frames 7-10: Black continues -> Still no cuts!
  for (int i = 7; i <= 10; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_FlashFollowedByGenuineCut)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> flash_y, flash_u, flash_v;
  std::vector<uint8_t> gray_y, gray_u, gray_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(flash_y, flash_u, flash_v, W, H, 255, 128, 128);
  FillSyntheticYUV420P(gray_y, gray_u, gray_v, W, H, 180, 128, 128);

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* flash_data[4] = { flash_y.data(), flash_u.data(), flash_v.data(), nullptr };
  const uint8_t* gray_data[4] = { gray_y.data(), gray_u.data(), gray_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Frames 0-4: Black
  for (int i = 0; i <= 4; ++i) {
    detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
  }

  // Frame 5: Flash
  detector.ProcessPlanar(5, flash_data, linesize, W, H, PixelFormatType::YUV420P);

  // Frames 6-11: Back to Black (Flash suppressed)
  for (int i = 6; i <= 11; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  // Frame 12: Genuine cut to Gray
  detector.ProcessPlanar(12, gray_data, linesize, W, H, PixelFormatType::YUV420P);

  // Frame 13: Gray continues -> Cut confirmed at frame 12!
  SceneCutResult res13 = detector.ProcessPlanar(13, gray_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(res13.cut_detected);
  OLIVE_ASSERT_EQUAL(res13.cut_frame_index, 12);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_MinSceneDurationSuppression)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 10; // Require at least 10 frames between cuts
  config.enable_flash_suppression = false;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> s1_y, s1_u, s1_v;
  std::vector<uint8_t> s2_y, s2_u, s2_v;
  std::vector<uint8_t> s3_y, s3_u, s3_v;
  FillSyntheticYUV420P(s1_y, s1_u, s1_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(s2_y, s2_u, s2_v, W, H, 128, 128, 128);
  FillSyntheticYUV420P(s3_y, s3_u, s3_v, W, H, 235, 128, 128);

  const uint8_t* d1[4] = { s1_y.data(), s1_u.data(), s1_v.data(), nullptr };
  const uint8_t* d2[4] = { s2_y.data(), s2_u.data(), s2_v.data(), nullptr };
  const uint8_t* d3[4] = { s3_y.data(), s3_u.data(), s3_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i < 10; ++i) detector.ProcessPlanar(i, d1, linesize, W, H, PixelFormatType::YUV420P);

  // Frame 10: Cut 1 to s2
  SceneCutResult cut1 = detector.ProcessPlanar(10, d2, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(cut1.cut_detected);
  OLIVE_ASSERT_EQUAL(cut1.cut_frame_index, 10);

  // Frame 14: Cut 2 to s3 occurs only 4 frames later (< 10 min_scene_frames)
  for (int i = 11; i <= 13; ++i) detector.ProcessPlanar(i, d2, linesize, W, H, PixelFormatType::YUV420P);
  SceneCutResult cut2 = detector.ProcessPlanar(14, d3, linesize, W, H, PixelFormatType::YUV420P);
  // Must be suppressed due to min_scene_frames!
  OLIVE_ASSERT(!cut2.cut_detected);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_MultiFormatNV12WithStridePadding)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = false;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  const int stride = W + 64; // 64 bytes of alignment padding per row

  std::vector<uint8_t> padded_y1(stride * H, 16);
  std::vector<uint8_t> padded_uv1(stride * (H / 2), 128); // NV12 interleaved

  std::vector<uint8_t> padded_y2(stride * H, 235);
  std::vector<uint8_t> padded_uv2(stride * (H / 2), 128);

  const uint8_t* data1[4] = { padded_y1.data(), padded_uv1.data(), nullptr, nullptr };
  const uint8_t* data2[4] = { padded_y2.data(), padded_uv2.data(), nullptr, nullptr };
  const int linesize[4] = { stride, stride, 0, 0 };

  for (int i = 0; i < 5; ++i) {
    detector.ProcessPlanar(i, data1, linesize, W, H, PixelFormatType::NV12);
  }

  SceneCutResult cut = detector.ProcessPlanar(5, data2, linesize, W, H, PixelFormatType::NV12);
  OLIVE_ASSERT(cut.cut_detected);
  OLIVE_ASSERT_EQUAL(cut.cut_frame_index, 5);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_TailFlushAtEOF)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> white_y, white_u, white_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(white_y, white_u, white_v, W, H, 235, 128, 128);

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* white_data[4] = { white_y.data(), white_u.data(), white_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i <= 4; ++i) {
    detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
  }

  // Frame 5 is the final frame before EOF
  SceneCutResult res5 = detector.ProcessPlanar(5, white_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res5.cut_detected); // Held in lookahead

  // Flush at EOF confirms the tail candidate
  SceneCutResult flushed = detector.Flush();
  OLIVE_ASSERT(flushed.cut_detected);
  OLIVE_ASSERT_EQUAL(flushed.cut_frame_index, 5);

  OLIVE_TEST_END;
}

} // namespace olive
```

---

## 6. Technical Investigation 5: Automated Test Suite 2 — `tests/timeline/scenecut-split-tests.cpp`

### 6.1 Test Specification & Rationale
`tests/timeline/scenecut-split-tests.cpp` validates the editorial integration:
1. Multi-point splitting of a single video block into exact sub-blocks.
2. Dual-track video + audio multi-point splitting with pairwise `NodeLinkCommand` link preservation.
3. Strict out-of-boundary cut rejection ($T \le in$ and $T \ge out$).
4. Mathematical time conversion parity under speed multiplier and reverse playback.
5. Invariance to unordered cut point lists (verifying internal ascending sort).
6. Comprehensive Undo and Redo state restoration.

### 6.2 Complete Source Code: `tests/timeline/scenecut-split-tests.cpp`
```cpp
#include "testutil.h"

#include <QList>
#include <QVector>

#include "core.h"
#include "node/block/clip/clip.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundosplit.h"
#include "undo/undostack.h"

namespace olive {

#define TIMELINE_TEST_START \
  ColorManager::SetUpDefaultConfig(); \
  Project project; \
  Sequence sequence; \
  sequence.setParent(&project)

OLIVE_ADD_TEST(MultiPointCutExecution_SingleVideoTrack)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  TrackList* v_list = sequence.track_list(Track::kVideo);
  Track* v_track = v_list->GetTracks().first();

  ClipBlock* clip = new ClipBlock();
  clip->set_length_and_media_out(rational(100)); // Length = 100 sec
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(clip->in(), rational(0));
  OLIVE_ASSERT_EQUAL(clip->out(), rational(100));

  // Execute 3 cuts: t = 20, t = 45, t = 80
  QList<rational> cut_times = { rational(20), rational(45), rational(80) };
  BlockSplitPreservingLinksCommand split_cmd({ clip }, cut_times);
  split_cmd.redo_now();

  // Verify 4 resulting blocks with exact expected coordinates
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->in(), rational(0));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->out(), rational(20));

  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->in(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->out(), rational(45));

  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->in(), rational(45));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(35));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->out(), rational(80));

  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->in(), rational(80));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->length(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->out(), rational(100));

  // Test Undo: reverts cleanly to 1 block of length 100
  split_cmd.undo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().first(), clip);
  OLIVE_ASSERT_EQUAL(clip->length(), rational(100));

  // Test Redo: reapplies 4 blocks cleanly
  split_cmd.redo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(MultiPointCutExecution_VideoAndAudioLinked)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  Track* v_track = sequence.track_list(Track::kVideo)->GetTracks().first();
  Track* a_track = sequence.track_list(Track::kAudio)->GetTracks().first();

  ClipBlock* v_clip = new ClipBlock();
  v_clip->set_length_and_media_out(rational(60));
  v_clip->setParent(&project);
  v_track->AppendBlock(v_clip);

  ClipBlock* a_clip = new ClipBlock();
  a_clip->set_length_and_media_out(rational(60));
  a_clip->setParent(&project);
  a_track->AppendBlock(a_clip);

  // Explicitly link video and audio clip
  Node::Link(v_clip, a_clip);
  OLIVE_ASSERT(Node::AreLinked(v_clip, a_clip));

  // Cut at t = 15 and t = 40
  QList<rational> cut_times = { rational(15), rational(40) };
  QVector<Block*> blocks_to_split = { v_clip, a_clip };

  BlockSplitPreservingLinksCommand split_cmd(blocks_to_split, cut_times);
  split_cmd.redo_now();

  // Both tracks must have 3 blocks
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 3);
  OLIVE_ASSERT_EQUAL(a_track->Blocks().size(), 3);

  // Check segment lengths
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(15));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(20));

  OLIVE_ASSERT_EQUAL(a_track->Blocks().at(0)->length(), rational(15));
  OLIVE_ASSERT_EQUAL(a_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(a_track->Blocks().at(2)->length(), rational(20));

  // CRITICAL: Verify link preservation across each split slice
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(0), a_track->Blocks().at(0)));
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(1), a_track->Blocks().at(1)));
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(2), a_track->Blocks().at(2)));

  // Test Undo: restores single linked pair
  split_cmd.undo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(a_track->Blocks().size(), 1);
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().first(), a_track->Blocks().first()));

  // Test Redo: restores 3 linked pairs
  split_cmd.redo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 3);
  OLIVE_ASSERT_EQUAL(a_track->Blocks().size(), 3);
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(1), a_track->Blocks().at(1)));

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(MultiPointCut_BoundaryAndOutOfRangeCuts)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  Track* v_track = sequence.track_list(Track::kVideo)->GetTracks().first();

  ClipBlock* clip = new ClipBlock();
  clip->set_length_and_media_out(rational(40)); // [0, 40]
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  // Move clip to in = 10, out = 50
  clip->set_in_and_out(rational(10), rational(50));
  OLIVE_ASSERT_EQUAL(clip->in(), rational(10));
  OLIVE_ASSERT_EQUAL(clip->out(), rational(50));

  // Cut points: 5 (before in), 10 (at in), 30 (inside), 50 (at out), 70 (after out)
  QList<rational> raw_cuts = { rational(5), rational(10), rational(30), rational(50), rational(70) };

  // Filter cuts according to boundary rule
  QList<rational> valid_cuts;
  for (const rational& t : raw_cuts) {
    if (t > clip->in() && t < clip->out()) {
      valid_cuts.append(t);
    }
  }

  // Exactly one cut is valid (t = 30)
  OLIVE_ASSERT_EQUAL(valid_cuts.size(), 1);
  OLIVE_ASSERT_EQUAL(valid_cuts.first(), rational(30));

  BlockSplitPreservingLinksCommand split_cmd({ clip }, valid_cuts);
  split_cmd.redo_now();

  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 2);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->in(), rational(10));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->out(), rational(30));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->in(), rational(30));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->out(), rational(50));

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(TimeConversion_MediaToSequence_SpeedAndReverse)
{
  Project project;
  ClipBlock clip;
  clip.setParent(&project);

  clip.set_in_and_out(rational(100), rational(150)); // Timeline [100, 150], length = 50
  clip.set_media_in(rational(20));                   // Media offset = 20

  // 1. Standard Playback (speed = 1.0, reverse = false)
  // Media time = 35 -> rel_seq = 35 - 20 = 15 -> abs_seq = 100 + 15 = 115
  {
    rational media_t = rational(35);
    rational rel_seq = clip.MediaToSequenceTime(media_t);
    rational abs_seq = clip.in() + rel_seq;
    OLIVE_ASSERT_EQUAL(rel_seq, rational(15));
    OLIVE_ASSERT_EQUAL(abs_seq, rational(115));
  }

  // 2. 2x Speed (speed = 2.0, reverse = false)
  // Media time = 40 -> rel_seq = (40 - 20) / 2.0 = 10 -> abs_seq = 100 + 10 = 110
  {
    clip.SetStandardValue(ClipBlock::kSpeedInput, 2.0);
    rational media_t = rational(40);
    rational rel_seq = clip.MediaToSequenceTime(media_t);
    rational abs_seq = clip.in() + rel_seq;
    OLIVE_ASSERT_EQUAL(rel_seq, rational(10));
    OLIVE_ASSERT_EQUAL(abs_seq, rational(110));
  }

  // 3. Reverse Playback (speed = 1.0, reverse = true)
  // Media time = 30 -> unreversed_rel = 30 - 20 = 10 -> reversed_rel = length(50) - 10 = 40
  // abs_seq = 100 + 40 = 140
  {
    clip.SetStandardValue(ClipBlock::kSpeedInput, 1.0);
    clip.SetStandardValue(ClipBlock::kReverseInput, true);
    rational media_t = rational(30);
    rational rel_seq = clip.MediaToSequenceTime(media_t);
    rational abs_seq = clip.in() + rel_seq;
    OLIVE_ASSERT_EQUAL(rel_seq, rational(40));
    OLIVE_ASSERT_EQUAL(abs_seq, rational(140));
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(UnsortedCutsRobustness)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  Track* v_track = sequence.track_list(Track::kVideo)->GetTracks().first();

  ClipBlock* clip = new ClipBlock();
  clip->set_length_and_media_out(rational(100));
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  // Provide cuts in reverse/jumbled order: 75, 25, 50
  QList<rational> jumbled_cuts = { rational(75), rational(25), rational(50) };
  BlockSplitPreservingLinksCommand split_cmd({ clip }, jumbled_cuts);
  split_cmd.redo_now();

  // Must automatically sort and partition correctly into [0, 25], [25, 50], [50, 75], [75, 100]
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->length(), rational(25));

  OLIVE_TEST_END;
}

} // namespace olive
```

---

## 7. CMake Build System Wiring

### 7.1 `app/dialog/CMakeLists.txt`
Add `add_subdirectory(scenecut)`:
```cmake
add_subdirectory(sequence)
add_subdirectory(speedduration)
add_subdirectory(scenecut)
add_subdirectory(task)
```

### 7.2 `app/dialog/scenecut/CMakeLists.txt` (New File)
```cmake
set(OLIVE_SOURCES
  ${OLIVE_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/scenecutdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scenecutdialog.h
  PARENT_SCOPE
)
```

### 7.3 `tests/CMakeLists.txt`
Register the new `task` test group:
```cmake
add_subdirectory(compositing)
add_subdirectory(general)
add_subdirectory(timeline)
add_subdirectory(task)
add_subdirectory(project)
add_subdirectory(render)
add_subdirectory(export)
```

### 7.4 `tests/task/CMakeLists.txt` (New File)
```cmake
olive_add_test(Task scenecut-tests scenecut-tests.cpp)
```

### 7.5 `tests/timeline/CMakeLists.txt`
Add `scenecut-split-tests`:
```cmake
olive_add_test(Timeline timeline-tests timeline-tests.cpp)
olive_add_test(Timeline TempoStream tempo-tests.cpp)
olive_add_test(Timeline scenecut-split-tests scenecut-split-tests.cpp)
```

---

## 8. Actionable Implementation Checklist for the Worker

| Task | File | Action Required |
| :--- | :--- | :--- |
| **Fix Splitting Bug** | `app/timeline/timelineundosplit.cpp` | Update `prepare()` in `BlockSplitPreservingLinksCommand` to maintain `current_blocks[j]` as documented in §4.2. |
| **SceneCutDialog** | `app/dialog/scenecut/scenecutdialog.h`<br>`app/dialog/scenecut/scenecutdialog.cpp`<br>`app/dialog/scenecut/CMakeLists.txt` | Create dialog class with slider, spinbox, quality combo, flash check, audio link check. |
| **Menu Action** | `app/widget/menu/menushared.h`<br>`app/widget/menu/menushared.cpp` | Add `edit_detect_scenes_item_` and slot `DetectSceneCutsTriggered()`. |
| **Timeline Integration** | `app/widget/timelinewidget/timelinewidget.h`<br>`app/widget/timelinewidget/timelinewidget.cpp`<br>`app/panel/timeline/timeline.h` | Add `ShowSceneCutDialogForSelectedClips()` and context menu action. Connect queued signal to GUI thread split. |
| **Unit Test 1** | `tests/task/scenecut-tests.cpp`<br>`tests/task/CMakeLists.txt` | Implement 8 synthetic tests (identical, hard cut, chroma, strobe rejection, min scene, NV12, tail flush). |
| **Unit Test 2** | `tests/timeline/scenecut-split-tests.cpp`<br>`tests/timeline/CMakeLists.txt` | Implement 5 timeline tests (multi-cut, audio link preservation, boundary filter, speed/reverse conversion, jumbled order). |
| **CMake Registration** | `tests/CMakeLists.txt`<br>`app/dialog/CMakeLists.txt` | Add subdirectories `task` and `scenecut`. |
| **Gauntlet Verification** | `scripts/gauntlet.py` | Run `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`. |
