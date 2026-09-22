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

#include "saveotio.h"

#ifdef USE_OTIO

#include <cmath>
#include <opentimelineio/clip.h>
#include <opentimelineio/externalReference.h>
#include <opentimelineio/gap.h>
#include <opentimelineio/linearTimeWarp.h>
#include <opentimelineio/marker.h>
#include <opentimelineio/serializableCollection.h>
#include <opentimelineio/serializableObject.h>
#include <opentimelineio/transition.h>

#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/transition.h"
#include "node/project/footage/footage.h"
#include "timeline/timelinemarker.h"
#include "ui/colorcoding.h"

namespace olive {

namespace {

std::string OliveColorToOTIOMarkerColor(int color)
{
  switch (color) {
  case ColorCoding::kRed:
  case ColorCoding::kMaroon:
    return OTIO::Marker::Color::red;
  case ColorCoding::kOrange:
  case ColorCoding::kBrown:
    return OTIO::Marker::Color::orange;
  case ColorCoding::kYellow:
    return OTIO::Marker::Color::yellow;
  case ColorCoding::kOlive:
  case ColorCoding::kLime:
  case ColorCoding::kGreen:
    return OTIO::Marker::Color::green;
  case ColorCoding::kCyan:
  case ColorCoding::kTeal:
    return OTIO::Marker::Color::cyan;
  case ColorCoding::kBlue:
  case ColorCoding::kNavy:
    return OTIO::Marker::Color::blue;
  case ColorCoding::kPink:
    return OTIO::Marker::Color::pink;
  case ColorCoding::kPurple:
    return OTIO::Marker::Color::purple;
  case ColorCoding::kSilver:
  case ColorCoding::kGray:
    return OTIO::Marker::Color::white;
  default:
    return OTIO::Marker::Color::green;
  }
}

} // namespace

SaveOTIOTask::SaveOTIOTask(Project *project, const QString &filename) :
  project_(project),
  filename_(filename)
{
  SetTitle(tr("Exporting project to OpenTimelineIO"));
}

bool SaveOTIOTask::Run()
{
  QVector<Sequence*> sequences = project_->root()->ListChildrenOfType<Sequence>();

  if (sequences.isEmpty()) {
    SetError(tr("Project contains no sequences to export."));
    return false;
  }

  std::vector<OTIO::SerializableObject*> serialized;

  foreach (Sequence* seq, sequences) {
    auto otio_timeline = SerializeTimeline(seq);

    if (otio_timeline) {
      // Append to list
      serialized.push_back(otio_timeline);
    } else {
      // Delete all existing timelines
      foreach (auto s, serialized) {
        s->possibly_delete();
      }

      // Error out of function
      SetError(tr("Failed to serialize sequence \"%1\"").arg(seq->GetLabel()));

      return false;
    }
  }

  OTIO::ErrorStatus es;
  QString out_file = filename_.isEmpty() ? project_->filename() : filename_;
  if (out_file.isEmpty()) {
    SetError(tr("No export filename specified."));
    return false;
  }

  if (serialized.size() == 1) {
    // Serialize timeline on its own
    auto t = serialized.front();
    t->to_json_file(out_file.toStdString(), &es);
    t->possibly_delete();
  } else {
    // Serialize all into a SerializableCollection
    auto collection = new OTIO::SerializableCollection("Sequences", serialized);
    collection->to_json_file(out_file.toStdString(), &es);
    collection->possibly_delete();

    // Delete all existing timelines
    foreach (auto s, serialized) {
      s->possibly_delete();
    }
  }

  return (es.outcome == OTIO::ErrorStatus::Outcome::OK);
}

OTIO::Timeline *SaveOTIOTask::SerializeTimeline(Sequence *sequence)
{
  auto otio_timeline = new OTIO::Timeline(sequence->GetLabel().toStdString());

  double rate = sequence->GetVideoParams().frame_rate().toDouble();
  if (qIsNaN(rate) || rate <= 0) {
    otio_timeline->possibly_delete();
    return nullptr;
  }

  if (!SerializeTrackList(sequence->track_list(Track::kVideo), otio_timeline, rate)
      || !SerializeTrackList(sequence->track_list(Track::kAudio), otio_timeline, rate)) {
    otio_timeline->possibly_delete();
    return nullptr;
  }

  SerializeMarkers(sequence, otio_timeline, rate);

  return otio_timeline;
}

void SaveOTIOTask::SerializeMarkers(Sequence *sequence, OTIO::Timeline *otio_timeline, double sequence_rate)
{
  if (!sequence->GetMarkers()) {
    return;
  }

  for (auto it = sequence->GetMarkers()->cbegin(); it != sequence->GetMarkers()->cend(); ++it) {
    TimelineMarker* marker = *it;
    if (!marker) continue;

    OTIO::TimeRange marked_range(
        marker->time().in().toRationalTime(sequence_rate),
        marker->time().length().toRationalTime(sequence_rate)
    );

    std::string color = OliveColorToOTIOMarkerColor(marker->color());
    auto otio_marker = new OTIO::Marker(marker->name().toStdString(), marked_range, color);
    otio_timeline->markers().push_back(otio_marker);
  }
}

OTIO::Clip *SaveOTIOTask::SerializeClip(ClipBlock *block, const std::string &track_kind, double sequence_rate)
{
  auto otio_clip = new OTIO::Clip(block->GetLabel().toStdString());

  otio_clip->set_source_range(OTIO::TimeRange(block->in().toRationalTime(sequence_rate),
                                              block->length().toRationalTime(sequence_rate)));

  QVector<Footage*> media_nodes = block->FindInputNodes<Footage>();
  if (!media_nodes.isEmpty()) {
    OTIO::TimeRange available_range;
    if (track_kind == "Video") {
      double source_frame_rate = block->connected_viewer() ?
          block->connected_viewer()->GetVideoParams().frame_rate().toDouble() : sequence_rate;
      if (qIsNaN(source_frame_rate) || source_frame_rate <= 0) {
        source_frame_rate = sequence_rate;
      }
      available_range = OTIO::TimeRange(OTIO::RationalTime(0, source_frame_rate),
                                        OTIO::RationalTime(media_nodes.first()->GetVideoParams().duration(),
                                                           source_frame_rate));
    } else if (track_kind == "Audio") {
      double sample_rate = media_nodes.first()->GetAudioParams().sample_rate();
      if (sample_rate <= 0) {
        sample_rate = 48000;
      }
      available_range = OTIO::TimeRange(OTIO::RationalTime(0, sample_rate),
                                        OTIO::RationalTime(media_nodes.first()->GetAudioParams().duration(),
                                                           sample_rate));
    }
    auto media_ref = new OTIO::ExternalReference(media_nodes.first()->filename().toStdString(), available_range);
    otio_clip->set_media_reference(media_ref);
  }

  double speed = block->speed();
  bool reverse = block->reverse();
  if (speed != 1.0 || reverse) {
    double time_scalar = reverse ? -speed : speed;
    auto time_warp = new OTIO::LinearTimeWarp(std::string(), "LinearTimeWarp", time_scalar);
    otio_clip->effects().push_back(time_warp);
  }

  return otio_clip;
}

OTIO::Track *SaveOTIOTask::SerializeTrack(Track *track, double sequence_rate, rational max_track_length)
{
  auto otio_track = new OTIO::Track();

  OTIO::ErrorStatus es;

  switch (track->type()) {
  case Track::kVideo:
    otio_track->set_kind("Video");
    break;
  case Track::kAudio:
    otio_track->set_kind("Audio");
    break;
  default:
    qWarning() << "Don't know OTIO track kind for native type" << track->type();
    goto fail;
  }

  foreach (Block* block, track->Blocks()) {
    OTIO::Composable* otio_block = nullptr;

    if (dynamic_cast<ClipBlock*>(block)) {
      otio_block = SerializeClip(static_cast<ClipBlock*>(block), otio_track->kind(), sequence_rate);
    } else if (dynamic_cast<GapBlock*>(block)) {
      otio_block = new OTIO::Gap(OTIO::TimeRange(block->in().toRationalTime(sequence_rate),
                                                 block->length().toRationalTime(sequence_rate)),
                                 block->GetLabel().toStdString());
    } else if (dynamic_cast<TransitionBlock*>(block)) {
      auto otio_transition = new OTIO::Transition(block->GetLabel().toStdString());

      TransitionBlock* our_transition = static_cast<TransitionBlock*>(block);

      otio_transition->set_in_offset(our_transition->in_offset().toRationalTime(sequence_rate));
      otio_transition->set_out_offset(our_transition->out_offset().toRationalTime(sequence_rate));

      otio_block = otio_transition;
    }

    if (!otio_block) {
      goto fail;
    }

    otio_track->append_child(otio_block, &es);

    if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
      goto fail;
    }
  }

  // All OTIO tracks must have the same duration so we add a Gap to fill the remaining time
  if (otio_track->duration(&es).to_seconds() < max_track_length.toDouble()) {
    double time_left = max_track_length.toDouble() - otio_track->duration(&es).to_seconds();

    OTIO::Gap* gap = new OTIO::Gap(OTIO::TimeRange(otio_track->duration(&es),
                                   OTIO::RationalTime(time_left, 1.0)));
    otio_track->append_child(gap, &es);

    if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
      goto fail;
    }
  }

  return otio_track;

fail:
  otio_track->possibly_delete();

  return nullptr;
}

bool SaveOTIOTask::SerializeTrackList(TrackList *list, OTIO::Timeline* otio_timeline, double sequence_rate)
{
  OTIO::ErrorStatus es;

  rational max_track_length = RATIONAL_MIN;

  foreach (Track* track, list->GetTracks()) {
    if (track->track_length() > max_track_length) {
      max_track_length = track->track_length();
    }
  }

  foreach (Track* track, list->GetTracks()) {
    auto otio_track = SerializeTrack(track, sequence_rate, max_track_length);

    if (!otio_track) {
      return false;
    }

    otio_timeline->tracks()->append_child(otio_track, &es);

    if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
      otio_track->possibly_delete();
      return false;
    }
  }

  return true;
}

} // namespace olive

#endif // USE_OTIO
