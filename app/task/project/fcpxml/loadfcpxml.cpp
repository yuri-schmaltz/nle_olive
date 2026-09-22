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

#include "loadfcpxml.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <olive/core/util/timecodefunctions.h>
#include "node/audio/volume/volume.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/nodeundo.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineundogeneral.h"

namespace olive {

LoadFCPXMLTask::LoadFCPXMLTask(const QString& filename) :
  ProjectLoadBaseTask(filename),
  sequence_footage_(nullptr),
  sequence_(nullptr)
{
  SetTitle(tr("Loading Final Cut Pro 7 XML"));
}

bool LoadFCPXMLTask::Run()
{
  QFile file(GetFilename());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    SetError(tr("Failed to open file \"%1\" for reading.").arg(GetFilename()));
    return false;
  }

  // Allocate new project
  project_ = new Project();
  project_->Initialize();
  project_->set_filename(GetFilename());
  project_->set_modified(true);

  clip_id_map_.clear();
  pending_links_.clear();
  imported_footage_.clear();
  sequence_footage_ = nullptr;
  sequence_ = nullptr;

  QXmlStreamReader reader(&file);

  bool success = ParseXMeml(&reader);

  if (reader.hasError() || !success) {
    if (reader.hasError() && GetError().isEmpty()) {
      SetError(tr("XML Parsing Error: %1 (line %2, column %3)")
               .arg(reader.errorString())
               .arg(reader.lineNumber())
               .arg(reader.columnNumber()));
    }
    delete project_;
    project_ = nullptr;
    return false;
  }

  // Re-establish audio/video block links
  for (auto it = pending_links_.cbegin(); it != pending_links_.cend(); ++it) {
    ClipBlock* source_clip = it.key();
    const QString& target_id = it.value();
    ClipBlock* target_clip = clip_id_map_.value(target_id, nullptr);
    if (target_clip && target_clip != source_clip && !Node::AreLinked(source_clip, target_clip)) {
      Node::Link(source_clip, target_clip);
    }
  }

  // Safe thread transfer: move all allocated QObjects to the application thread.
  // Use QCoreApplication::instance() instead of qApp to avoid UBSan downcast errors
  // in test contexts where only QCoreApplication (not QApplication) is available.
  if (QCoreApplication::instance()) {
    project_->moveToThread(QCoreApplication::instance()->thread());
  }
  return true;
}

bool LoadFCPXMLTask::ParseXMeml(QXmlStreamReader* reader)
{
  if (!reader->readNextStartElement()) {
    SetError(tr("File is empty or contains no XML elements."));
    return false;
  }

  if (reader->name() != QStringLiteral("xmeml")) {
    SetError(tr("Root element is not <xmeml>."));
    return false;
  }

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("sequence")) {
      if (!ParseSequence(reader)) {
        return false;
      }
    } else if (reader->name() == QStringLiteral("project")) {
      // Support nested <project><children><sequence> structure
      while (reader->readNextStartElement()) {
        if (reader->name() == QStringLiteral("children")) {
          while (reader->readNextStartElement()) {
            if (reader->name() == QStringLiteral("sequence")) {
              if (!ParseSequence(reader)) {
                return false;
              }
            } else {
              reader->skipCurrentElement();
            }
          }
        } else {
          reader->skipCurrentElement();
        }
      }
    } else {
      reader->skipCurrentElement();
    }
  }

  if (!sequence_) {
    SetError(tr("No <sequence> element found in the Final Cut Pro 7 XML."));
    return false;
  }

  return true;
}

bool LoadFCPXMLTask::ParseSequence(QXmlStreamReader* reader)
{
  sequence_ = new Sequence();
  sequence_->setParent(project_);
  FolderAddChild(project_->root(), sequence_).redo_now();

  sequence_footage_ = new Folder();
  sequence_footage_->SetLabel(tr("Footage"));
  sequence_footage_->setParent(project_);
  FolderAddChild(project_->root(), sequence_footage_).redo_now();

  rational sequence_rate = rational(24, 1);
  rational sequence_timebase = rational(1, 24);

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("name")) {
      sequence_->SetLabel(reader->readElementText());
    } else if (reader->name() == QStringLiteral("rate")) {
      if (ParseRate(reader, sequence_rate)) {
        sequence_timebase = sequence_rate.flipped();
        VideoParams vp = sequence_->GetVideoParams();
        vp.set_frame_rate(sequence_rate);
        sequence_->SetVideoParams(vp);
      } else {
        return false;
      }
    } else if (reader->name() == QStringLiteral("media")) {
      if (!ParseMedia(reader, sequence_, sequence_rate, sequence_timebase)) {
        return false;
      }
    } else if (reader->name() == QStringLiteral("marker")) {
      ParseMarker(reader, sequence_->GetMarkers(), sequence_timebase);
    } else {
      reader->skipCurrentElement();
    }
  }

  return true;
}

bool LoadFCPXMLTask::ParseRate(QXmlStreamReader* reader, rational& rate)
{
  int tb = 0;
  bool ntsc = false;
  bool has_tb = false;

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("timebase")) {
      bool ok = false;
      tb = reader->readElementText().toInt(&ok);
      if (!ok) {
        SetError(tr("Invalid timebase integer in <rate>."));
        return false;
      }
      has_tb = true;
    } else if (reader->name() == QStringLiteral("ntsc")) {
      QString text = reader->readElementText().trimmed().toUpper();
      ntsc = (text == QStringLiteral("TRUE") || text == QStringLiteral("1"));
    } else {
      reader->skipCurrentElement();
    }
  }

  if (!has_tb || tb <= 0) {
    SetError(tr("Missing or invalid timebase in <rate>."));
    return false;
  }

  if (ntsc) {
    rate = rational(tb * 1000, 1001);
  } else {
    rate = rational(tb, 1);
  }
  return true;
}

bool LoadFCPXMLTask::ParseMedia(QXmlStreamReader* reader, Sequence* sequence,
                               const rational& rate, const rational& timebase)
{
  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("video")) {
      if (!ParseVideo(reader, sequence, rate, timebase)) return false;
    } else if (reader->name() == QStringLiteral("audio")) {
      if (!ParseAudio(reader, sequence, rate, timebase)) return false;
    } else {
      reader->skipCurrentElement();
    }
  }
  return true;
}

bool LoadFCPXMLTask::ParseVideo(QXmlStreamReader* reader, Sequence* sequence,
                               const rational& rate, const rational& timebase)
{
  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("format")) {
      while (reader->readNextStartElement()) {
        if (reader->name() == QStringLiteral("samplecharacteristics")) {
          while (reader->readNextStartElement()) {
            if (reader->name() == QStringLiteral("width")) {
              int w = reader->readElementText().toInt();
              VideoParams vp = sequence->GetVideoParams();
              vp.set_width(w);
              sequence->SetVideoParams(vp);
            } else if (reader->name() == QStringLiteral("height")) {
              int h = reader->readElementText().toInt();
              VideoParams vp = sequence->GetVideoParams();
              vp.set_height(h);
              sequence->SetVideoParams(vp);
            } else if (reader->name() == QStringLiteral("rate")) {
              rational r;
              if (ParseRate(reader, r)) {
                VideoParams vp = sequence->GetVideoParams();
                vp.set_frame_rate(r);
                sequence->SetVideoParams(vp);
              } else {
                return false;
              }
            } else {
              reader->skipCurrentElement();
            }
          }
        } else {
          reader->skipCurrentElement();
        }
      }
    } else if (reader->name() == QStringLiteral("track")) {
      TimelineAddTrackCommand cmd(sequence->track_list(Track::kVideo));
      cmd.redo_now();
      Track* track = cmd.track();
      if (!ParseTrack(reader, track, Track::kVideo, rate, timebase)) {
        return false;
      }
    } else {
      reader->skipCurrentElement();
    }
  }
  return true;
}

bool LoadFCPXMLTask::ParseAudio(QXmlStreamReader* reader, Sequence* sequence,
                               const rational& rate, const rational& timebase)
{
  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("track")) {
      TimelineAddTrackCommand cmd(sequence->track_list(Track::kAudio));
      cmd.redo_now();
      Track* track = cmd.track();
      if (!ParseTrack(reader, track, Track::kAudio, rate, timebase)) {
        return false;
      }
    } else {
      reader->skipCurrentElement();
    }
  }
  return true;
}

bool LoadFCPXMLTask::ParseTrack(QXmlStreamReader* reader, Track* track, Track::Type type,
                               const rational& rate, const rational& timebase)
{
  int64_t current_timeline_frame = 0;
  Block* previous_block = nullptr;
  bool previous_was_transition = false;

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("clipitem")) {
      if (!ParseClipItem(reader, track, type, current_timeline_frame, rate, timebase,
                         previous_block, previous_was_transition)) {
        return false;
      }
    } else if (reader->name() == QStringLiteral("transitionitem")) {
      if (!ParseTransitionItem(reader, track, rate, timebase,
                               previous_block, previous_was_transition)) {
        return false;
      }
    } else {
      reader->skipCurrentElement();
    }
  }
  return true;
}

bool LoadFCPXMLTask::ParseClipItem(QXmlStreamReader* reader, Track* track, Track::Type type,
                                   int64_t& current_frame, const rational& rate,
                                   const rational& timebase, Block*& previous_block,
                                   bool& previous_was_transition)
{
  Q_UNUSED(rate);

  QString clip_id = reader->attributes().value(QStringLiteral("id")).toString();
  QString name;
  int64_t start = 0;
  int64_t end = 0;
  int64_t in = 0;
  int64_t out = 0;
  QString file_id, file_name, file_pathurl;
  rational file_duration = 0;
  QStringList clip_links;

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("name")) {
      name = reader->readElementText();
    } else if (reader->name() == QStringLiteral("start")) {
      start = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("end")) {
      end = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("in")) {
      in = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("out")) {
      out = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("file")) {
      ParseFile(reader, file_id, file_name, file_pathurl, file_duration, timebase);
    } else if (reader->name() == QStringLiteral("link")) {
      while (reader->readNextStartElement()) {
        if (reader->name() == QStringLiteral("linkclipref")) {
          clip_links.append(reader->readElementText());
        } else {
          reader->skipCurrentElement();
        }
      }
    } else {
      reader->skipCurrentElement();
    }
  }

  // If there is a gap between current track time and clip start, create a GapBlock
  if (!previous_was_transition && start > current_frame) {
    int64_t gap_frames = start - current_frame;
    GapBlock* gap = new GapBlock();
    gap->setParent(project_);
    gap->set_length_and_media_out(Timecode::timestamp_to_time(gap_frames, timebase));
    track->AppendBlock(gap);
    gap->SetNodePositionInContext(gap, QPointF(0, 0));
    current_frame = start;
    previous_block = gap;
  }

  // Create ClipBlock
  ClipBlock* clip = new ClipBlock();
  clip->setParent(project_);
  clip->SetLabel(name);

  int64_t length_frames = end - start;
  clip->set_media_in(Timecode::timestamp_to_time(in, timebase));
  clip->set_length_and_media_out(Timecode::timestamp_to_time(length_frames, timebase));
  track->AppendBlock(clip);
  clip->SetNodePositionInContext(clip, QPointF(0, 0));

  // Connect to incoming transition if prior block was a transition
  if (previous_was_transition && previous_block) {
    Node::ConnectEdge(clip, NodeInput(previous_block, TransitionBlock::kInBlockInput));
    previous_was_transition = false;
  }

  // Attach Footage and processing nodes if a media file is referenced
  QString resolved_path = ResolvePathUrl(file_pathurl);
  if (!resolved_path.isEmpty()) {
    Footage* footage = imported_footage_.value(resolved_path, nullptr);
    if (!footage) {
      footage = new Footage(resolved_path);
      footage->setParent(project_);
      footage->SetLabel(QFileInfo(resolved_path).fileName());
      imported_footage_.insert(resolved_path, footage);
      FolderAddChild add(sequence_footage_, footage);
      add.redo_now();
    }

    clip->SetNodePositionInContext(footage, QPointF(-2, 0));

    if (type == Track::kVideo) {
      TransformDistortNode* transform = new TransformDistortNode();
      transform->setParent(project_);
      Node::ConnectEdge(footage, NodeInput(transform, TransformDistortNode::kTextureInput));
      Node::ConnectEdge(transform, NodeInput(clip, ClipBlock::kBufferIn));
      clip->SetNodePositionInContext(transform, QPointF(-1, 0));
    } else {
      VolumeNode* volume_node = new VolumeNode();
      volume_node->setParent(project_);
      Node::ConnectEdge(footage, NodeInput(volume_node, VolumeNode::kSamplesInput));
      Node::ConnectEdge(volume_node, NodeInput(clip, ClipBlock::kBufferIn));
      clip->SetNodePositionInContext(volume_node, QPointF(-1, 0));
    }
  }

  if (!clip_id.isEmpty()) {
    clip_id_map_.insert(clip_id, clip);
  }
  for (const QString& link_ref : clip_links) {
    pending_links_.insert(clip, link_ref);
  }

  current_frame = end;
  previous_block = clip;
  return true;
}

bool LoadFCPXMLTask::ParseTransitionItem(QXmlStreamReader* reader, Track* track,
                                         const rational& rate, const rational& timebase,
                                         Block*& previous_block, bool& previous_was_transition)
{
  Q_UNUSED(rate);

  int64_t start = 0;
  int64_t end = 0;
  QString alignment = QStringLiteral("center");
  QString name;

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("start")) {
      start = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("end")) {
      end = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("alignment")) {
      alignment = reader->readElementText().trimmed().toLower();
    } else if (reader->name() == QStringLiteral("name")) {
      name = reader->readElementText();
    } else {
      reader->skipCurrentElement();
    }
  }

  int64_t dur_frames = end - start;
  int64_t in_offset_frames = 0;
  int64_t out_offset_frames = 0;

  if (alignment == QStringLiteral("start-on-edit")) {
    in_offset_frames = 0;
    out_offset_frames = dur_frames;
  } else if (alignment == QStringLiteral("end-on-edit")) {
    in_offset_frames = dur_frames;
    out_offset_frames = 0;
  } else {
    // "center"
    in_offset_frames = dur_frames / 2;
    out_offset_frames = dur_frames - in_offset_frames;
  }

  CrossDissolveTransition* trans = new CrossDissolveTransition();
  trans->setParent(project_);
  trans->set_offsets_and_length(Timecode::timestamp_to_time(in_offset_frames, timebase),
                                Timecode::timestamp_to_time(out_offset_frames, timebase));
  track->AppendBlock(trans);
  trans->SetNodePositionInContext(trans, QPointF(0, 0));

  if (previous_block) {
    Node::ConnectEdge(previous_block, NodeInput(trans, TransitionBlock::kOutBlockInput));
  }

  previous_was_transition = true;
  previous_block = trans;
  return true;
}

bool LoadFCPXMLTask::ParseMarker(QXmlStreamReader* reader, TimelineMarkerList* markers,
                                 const rational& timebase)
{
  QString name;
  QString comment;
  int64_t in = 0;
  int64_t out = -1;

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("name")) {
      name = reader->readElementText();
    } else if (reader->name() == QStringLiteral("comment")) {
      comment = reader->readElementText();
    } else if (reader->name() == QStringLiteral("in")) {
      in = reader->readElementText().toLongLong();
    } else if (reader->name() == QStringLiteral("out")) {
      out = reader->readElementText().toLongLong();
    } else {
      reader->skipCurrentElement();
    }
  }

  rational in_time = Timecode::timestamp_to_time(in, timebase);
  rational out_time = in_time;
  if (out > in) {
    out_time = Timecode::timestamp_to_time(out, timebase);
  }

  // Two-phase construction: create with nullptr parent first, then call setParent.
  // If we pass `markers` directly to the constructor, QObject::QObject(parent) triggers
  // TimelineMarkerList::childEvent before the TimelineMarker vtable is fully established.
  // At that point dynamic_cast<TimelineMarker*> returns nullptr, so InsertIntoList is
  // never called and the marker silently disappears.
  auto* marker = new TimelineMarker(1, TimeRange(in_time, out_time), name, nullptr);
  marker->setParent(markers);
  return true;
}

bool LoadFCPXMLTask::ParseFile(QXmlStreamReader* reader, QString& file_id, QString& name,
                              QString& pathurl, rational& duration, const rational& timebase)
{
  file_id = reader->attributes().value(QStringLiteral("id")).toString();

  while (reader->readNextStartElement()) {
    if (reader->name() == QStringLiteral("name")) {
      name = reader->readElementText();
    } else if (reader->name() == QStringLiteral("pathurl")) {
      pathurl = reader->readElementText();
    } else if (reader->name() == QStringLiteral("duration")) {
      int64_t dur = reader->readElementText().toLongLong();
      duration = Timecode::timestamp_to_time(dur, timebase);
    } else {
      reader->skipCurrentElement();
    }
  }
  return true;
}

QString LoadFCPXMLTask::ResolvePathUrl(const QString& pathurl) const
{
  if (pathurl.isEmpty()) {
    return QString();
  }

  QUrl url(pathurl);
  QString local_file = url.toLocalFile();

  if (local_file.isEmpty()) {
    QString p = pathurl;
    if (p.startsWith(QStringLiteral("file://localhost/"), Qt::CaseInsensitive)) {
      p = p.mid(16);
    } else if (p.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
      p = p.mid(7);
    } else if (p.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
      p = p.mid(7);
    }
    local_file = QUrl::fromPercentEncoding(p.toUtf8());
  }

  return local_file;
}

} // namespace olive
