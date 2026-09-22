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

#include "timeline.h"

#include "panel/panelmanager.h"
#include "panel/project/footagemanagementpanel.h"

#include "dialog/scenecut/scenecutdialog.h"
#include "task/scenecut/scenecuttask.h"
#include "task/taskmanager.h"
#include "timeline/timelineundosplit.h"
#include <olive/core/util/timecodefunctions.h>

namespace olive {

TimelinePanel::TimelinePanel(const QString &name) :
  TimeBasedPanel(name)
{
  TimelineWidget* tw = new TimelineWidget(this);
  SetTimeBasedWidget(tw);

  Retranslate();

  connect(tw, &TimelineWidget::BlockSelectionChanged, this, &TimelinePanel::BlockSelectionChanged);
  connect(tw, &TimelineWidget::RequestCaptureStart, this, &TimelinePanel::RequestCaptureStart);
  connect(tw, &TimelineWidget::RevealViewerInProject, this, &TimelinePanel::RevealViewerInProject);
  connect(tw, &TimelineWidget::RevealViewerInFootageViewer, this, &TimelinePanel::RevealViewerInFootageViewer);
}

void TimelinePanel::SplitAtPlayhead()
{
  timeline_widget()->SplitAtPlayhead();
}

void TimelinePanel::LoadData(const Info &info)
{
  timeline_widget()->RestoreSplitterState(QByteArray::fromBase64(info.at("splitter").toUtf8()));
}

PanelWidget::Info TimelinePanel::SaveData() const
{
  Info i;

  i["splitter"] = timeline_widget()->SaveSplitterState().toBase64();

  return i;
}

void TimelinePanel::SelectAll()
{
  timeline_widget()->SelectAll();
}

void TimelinePanel::DeselectAll()
{
  timeline_widget()->DeselectAll();
}

void TimelinePanel::RippleToIn()
{
  timeline_widget()->RippleToIn();
}

void TimelinePanel::RippleToOut()
{
  timeline_widget()->RippleToOut();
}

void TimelinePanel::EditToIn()
{
  timeline_widget()->EditToIn();
}

void TimelinePanel::EditToOut()
{
  timeline_widget()->EditToOut();
}

void TimelinePanel::DeleteSelected()
{
  timeline_widget()->DeleteSelected(false);
}

void TimelinePanel::RippleDelete()
{
  timeline_widget()->DeleteSelected(true);
}

void TimelinePanel::IncreaseTrackHeight()
{
  timeline_widget()->IncreaseTrackHeight();
}

void TimelinePanel::DecreaseTrackHeight()
{
  timeline_widget()->DecreaseTrackHeight();
}

void TimelinePanel::ToggleLinks()
{
  timeline_widget()->ToggleLinksOnSelected();
}

void TimelinePanel::PasteInsert()
{
  timeline_widget()->PasteInsert();
}

void TimelinePanel::DeleteInToOut()
{
  timeline_widget()->DeleteInToOut(false);
}

void TimelinePanel::RippleDeleteInToOut()
{
  timeline_widget()->DeleteInToOut(true);
}

void TimelinePanel::ToggleSelectedEnabled()
{
  timeline_widget()->ToggleSelectedEnabled();
}

void TimelinePanel::SetColorLabel(int index)
{
  timeline_widget()->SetColorLabel(index);
}

void TimelinePanel::NudgeLeft()
{
  timeline_widget()->NudgeLeft();
}

void TimelinePanel::NudgeRight()
{
  timeline_widget()->NudgeRight();
}

void TimelinePanel::MoveInToPlayhead()
{
  timeline_widget()->MoveInToPlayhead();
}

void TimelinePanel::MoveOutToPlayhead()
{
  timeline_widget()->MoveOutToPlayhead();
}

void TimelinePanel::RenameSelected()
{
  timeline_widget()->RenameSelectedBlocks();
}

void TimelinePanel::InsertFootageAtPlayhead(const QVector<ViewerOutput *> &footage)
{
  timeline_widget()->InsertFootageAtPlayhead(footage);
}

void TimelinePanel::OverwriteFootageAtPlayhead(const QVector<ViewerOutput *> &footage)
{
  timeline_widget()->OverwriteFootageAtPlayhead(footage);
}

namespace {

rational MapClipMediaToSequenceTime(const ClipBlock* clip, const rational& media_time)
{
  if (!clip || media_time == RATIONAL_MIN || media_time == RATIONAL_MAX) {
    return media_time;
  }

  rational sequence_time = media_time - clip->media_in();

  double speed_val = clip->speed();
  if (qIsNull(speed_val)) {
    return rational::NaN;
  } else if (!qFuzzyCompare(speed_val, 1.0)) {
    sequence_time = rational::fromDouble(sequence_time.toDouble() / speed_val);
  }

  if (clip->reverse()) {
    sequence_time = clip->length() - sequence_time;
  }

  return sequence_time;
}

} // anonymous namespace

void TimelinePanel::ShowSceneCutDialogForSelectedClips()
{
  QVector<ClipBlock*> clips;
  for (Block* b : GetSelectedBlocks()) {
    if (auto c = dynamic_cast<ClipBlock*>(b)) {
      clips.append(c);
    }
  }
  if (clips.isEmpty()) {
    return;
  }

  rational tb = timeline_widget() ? timeline_widget()->timebase() : rational(30, 1);
  if (tb.isNull() || tb <= rational(0)) {
    tb = rational(30, 1);
  }

  SceneCutDialog dialog(clips, tb, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  SceneCutConfig config = dialog.GetConfig();
  bool split_audio = dialog.SplitLinkedAudio();

  for (ClipBlock* clip : clips) {
    SceneCutTask* task = new SceneCutTask(clip, config);
    QPointer<ClipBlock> safe_clip(clip);
    QPointer<TimelinePanel> safe_panel(this);

    connect(task, &SceneCutTask::SceneCutsDetected, this,
            [safe_clip, safe_panel, split_audio](const QVector<rational>& cuts) {
      if (!safe_clip || !safe_panel || !safe_clip->track() || !safe_clip->project()) {
        return;
      }
      safe_panel->ApplySceneCutsToClip(safe_clip.data(), cuts, split_audio);
    }, Qt::QueuedConnection);

    TaskManager::instance()->AddTask(task);
  }
}

void TimelinePanel::ApplySceneCutsToClip(ClipBlock* clip, const QVector<rational>& media_cuts, bool split_linked_audio)
{
  if (!clip || media_cuts.isEmpty()) return;

  rational tb = timeline_widget() ? timeline_widget()->timebase() : rational(30, 1);
  if (tb.isNull() || tb <= rational(0)) {
    tb = rational(30, 1);
  }
  const rational clip_in = clip->in();
  const rational clip_out = clip->out();

  QList<rational> seq_cuts;
  for (const rational& m_time : media_cuts) {
    rational rel_time = MapClipMediaToSequenceTime(clip, m_time);
    if (rel_time.isNaN()) continue;

    rational seq_time = clip_in + rel_time;
    rational snapped = Timecode::snap_time_to_timebase(seq_time, tb);

    if (snapped > clip_in && snapped < clip_out) {
      if (!seq_cuts.contains(snapped)) {
        seq_cuts.append(snapped);
      }
    }
  }

  if (seq_cuts.isEmpty()) return;

  std::sort(seq_cuts.begin(), seq_cuts.end());

  QVector<Block*> blocks_to_split = { clip };
  if (split_linked_audio) {
    for (Node* linked_node : clip->links()) {
      if (Block* linked_block = dynamic_cast<Block*>(linked_node)) {
        if (!blocks_to_split.contains(linked_block)) {
          blocks_to_split.append(linked_block);
        }
      }
    }
  }

  Core::instance()->undo_stack()->push(
      new BlockSplitPreservingLinksCommand(blocks_to_split, seq_cuts),
      tr("Auto-Split Scenes"));
}

void TimelinePanel::Retranslate()
{
  TimeBasedPanel::Retranslate();

  SetTitle(tr("Timeline"));
}

}
