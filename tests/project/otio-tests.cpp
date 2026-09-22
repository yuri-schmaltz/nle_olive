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

#ifdef USE_OTIO

#include <QFile>
#include <QTemporaryDir>
#include <opentimelineio/timeline.h>
#include <opentimelineio/transition.h>

#include "config/config.h"
#include "node/block/clip/clip.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "node/output/track/track.h"
#include "render/diskmanager.h"
#include "task/project/loadotio/loadotio.h"
#include "task/project/saveotio/saveotio.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineundogeneral.h"
#include "testutil.h"

namespace olive {
namespace {

struct Environment {
  Environment() {
    ColorManager::SetUpDefaultConfig();
    DiskManager::CreateInstance();
    NodeFactory::Initialize();
    ProjectSerializer::Initialize();
  }
  ~Environment() {
    ProjectSerializer::Destroy();
    NodeFactory::Destroy();
    DiskManager::DestroyInstance();
  }
};

} // namespace

OLIVE_ADD_TEST(OTIO_SequenceRoundTrip_TransitionsAndMarkers)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QString otio_file = dir.filePath(QStringLiteral("otio_roundtrip.otio"));

  Project project;
  project.Initialize();

  Sequence* sequence = new Sequence();
  sequence->setParent(&project);
  sequence->SetLabel(QStringLiteral("OTIO Timeline"));

  VideoParams vp;
  vp.set_frame_rate(rational(24, 1));
  sequence->SetVideoParams(vp);

  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();

  ClipBlock* clip1 = new ClipBlock();
  clip1->setParent(&project);
  clip1->set_in(rational(0, 1));
  clip1->set_length_and_media_out(rational(3, 1));
  vtrack->AppendBlock(clip1);

  CrossDissolveTransition* trans = new CrossDissolveTransition();
  trans->setParent(&project);
  trans->set_offsets_and_length(rational(1, 2), rational(1, 2));
  vtrack->AppendBlock(trans);
  Node::ConnectEdge(clip1, NodeInput(trans, TransitionBlock::kOutBlockInput));

  ClipBlock* clip2 = new ClipBlock();
  clip2->setParent(&project);
  clip2->set_in(rational(3, 1));
  clip2->set_length_and_media_out(rational(3, 1));
  vtrack->AppendBlock(clip2);
  Node::ConnectEdge(clip2, NodeInput(trans, TransitionBlock::kInBlockInput));

  // Add Marker
  new TimelineMarker(1, TimeRange(rational(1, 1), rational(1, 1)),
                     QStringLiteral("Cue Point"), sequence->GetMarkers());
  OLIVE_ASSERT_EQUAL(sequence->GetMarkers()->size(), static_cast<size_t>(1));

  // Save to OTIO
  SaveOTIOTask save_task(&project, otio_file);
  OLIVE_ASSERT(save_task.Start());

  // Direct OTIO validation: Ensure transition was not overwritten with blank dummy
  OTIO::ErrorStatus es;
  auto otio_root = OTIO::SerializableObject::from_json_file(otio_file.toStdString(), &es);
  OLIVE_ASSERT(es.outcome == OTIO::ErrorStatus::Outcome::OK);
  auto otio_timeline = dynamic_cast<OTIO::Timeline*>(otio_root);
  OLIVE_ASSERT(otio_timeline != nullptr);

  auto otio_tracks = otio_timeline->video_tracks();
  OLIVE_ASSERT_EQUAL(otio_tracks.size(), static_cast<size_t>(1));
  auto otio_track = otio_tracks.front();

  // Find transition child and assert non-zero offsets
  bool found_transition = false;
  for (auto child : otio_track->children()) {
    if (auto t = dynamic_cast<OTIO::Transition*>(child.value)) {
      found_transition = true;
      OLIVE_ASSERT(t->in_offset().value() > 0);
      OLIVE_ASSERT(t->out_offset().value() > 0);
    }
  }
  OLIVE_ASSERT(found_transition);
  otio_root->possibly_delete();

  // Load back through LoadOTIOTask
  LoadOTIOTask load_task(otio_file);
  OLIVE_ASSERT(load_task.Start());

  Project* loaded_project = load_task.GetLoadedProject();
  OLIVE_ASSERT(loaded_project != nullptr);

  Sequence* loaded_seq = loaded_project->root()->ListChildrenOfType<Sequence>().first();
  Track* loaded_track = loaded_seq->track_list(Track::kVideo)->GetTrackAt(0);

  // Assert transition exists and properties match
  OLIVE_ASSERT_EQUAL(loaded_track->Blocks().size(), 3);
  TransitionBlock* loaded_trans = dynamic_cast<TransitionBlock*>(loaded_track->Blocks().at(1));
  OLIVE_ASSERT(loaded_trans != nullptr);
  OLIVE_ASSERT_EQUAL(loaded_trans->in_offset(), rational(1, 2));
  OLIVE_ASSERT_EQUAL(loaded_trans->out_offset(), rational(1, 2));

  // Assert marker exists
  OLIVE_ASSERT_EQUAL(loaded_seq->GetMarkers()->size(), static_cast<size_t>(1));
  TimelineMarker* loaded_marker = loaded_seq->GetMarkers()->front();
  OLIVE_ASSERT_EQUAL(loaded_marker->name(), QStringLiteral("Cue Point"));

  delete loaded_project;
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(OTIO_SpeedAndReversePreservation)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QString otio_file = dir.filePath(QStringLiteral("otio_speed.otio"));

  Project project;
  project.Initialize();

  Sequence* sequence = new Sequence();
  sequence->setParent(&project);
  sequence->SetLabel(QStringLiteral("Speed Timeline"));

  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();

  ClipBlock* clip = new ClipBlock();
  clip->setParent(&project);
  clip->set_in(rational(0, 1));
  clip->set_length_and_media_out(rational(4, 1));
  clip->SetStandardValue(ClipBlock::kSpeedInput, 2.0);
  clip->set_reverse(true);
  vtrack->AppendBlock(clip);

  SaveOTIOTask save_task(&project, otio_file);
  OLIVE_ASSERT(save_task.Start());

  LoadOTIOTask load_task(otio_file);
  OLIVE_ASSERT(load_task.Start());

  Project* loaded_project = load_task.GetLoadedProject();
  OLIVE_ASSERT(loaded_project != nullptr);

  Sequence* loaded_seq = loaded_project->root()->ListChildrenOfType<Sequence>().first();
  ClipBlock* loaded_clip = dynamic_cast<ClipBlock*>(loaded_seq->track_list(Track::kVideo)->GetTrackAt(0)->Blocks().first());
  OLIVE_ASSERT(loaded_clip != nullptr);
  OLIVE_ASSERT_EQUAL(loaded_clip->speed(), 2.0);
  OLIVE_ASSERT_EQUAL(loaded_clip->reverse(), true);

  delete loaded_project;
  OLIVE_TEST_END;
}

} // namespace olive

#endif // USE_OTIO
