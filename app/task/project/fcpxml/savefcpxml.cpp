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

#include "savefcpxml.h"

#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <olive/core/util/timecodefunctions.h>
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/transition.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "timeline/timelinemarker.h"

namespace olive {

SaveFCPXMLTask::SaveFCPXMLTask(Sequence* sequence, const QString& filename) :
  sequence_(sequence),
  project_(sequence ? sequence->project() : nullptr),
  filename_(filename)
{
  SetTitle(tr("Exporting Final Cut Pro 7 XML"));
}

SaveFCPXMLTask::SaveFCPXMLTask(Project* project, const QString& filename) :
  sequence_(nullptr),
  project_(project),
  filename_(filename)
{
  if (project_) {
    QVector<Sequence*> seqs = project_->root()->ListChildrenOfType<Sequence>();
    if (!seqs.isEmpty()) {
      sequence_ = seqs.first();
    }
  }
  SetTitle(tr("Exporting Final Cut Pro 7 XML"));
}

bool SaveFCPXMLTask::Run()
{
  if (!sequence_) {
    SetError(tr("No active sequence available to export."));
    return false;
  }

  QFile file(filename_);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    SetError(tr("Failed to open file \"%1\" for writing.").arg(filename_));
    return false;
  }

  // Pre-pass: Index all clips to generate clipitem IDs and coordinates for dual <link> tags
  clip_id_map_.clear();
  clip_coord_map_.clear();
  clip_mediatype_map_.clear();

  int clip_counter = 1;

  // Index Video Tracks
  TrackList* vtracks = sequence_->track_list(Track::kVideo);
  if (vtracks) {
    for (int t = 0; t < vtracks->GetTrackCount(); ++t) {
      Track* track = vtracks->GetTrackAt(t);
      int clip_index = 1;
      for (Block* b : track->Blocks()) {
        if (ClipBlock* clip = dynamic_cast<ClipBlock*>(b)) {
          clip_id_map_.insert(clip, QStringLiteral("clipitem-%1").arg(clip_counter++));
          clip_coord_map_.insert(clip, qMakePair(t + 1, clip_index++));
          clip_mediatype_map_.insert(clip, QStringLiteral("video"));
        }
      }
    }
  }

  // Index Audio Tracks
  TrackList* atracks = sequence_->track_list(Track::kAudio);
  if (atracks) {
    for (int t = 0; t < atracks->GetTrackCount(); ++t) {
      Track* track = atracks->GetTrackAt(t);
      int clip_index = 1;
      for (Block* b : track->Blocks()) {
        if (ClipBlock* clip = dynamic_cast<ClipBlock*>(b)) {
          clip_id_map_.insert(clip, QStringLiteral("clipitem-%1").arg(clip_counter++));
          clip_coord_map_.insert(clip, qMakePair(t + 1, clip_index++));
          clip_mediatype_map_.insert(clip, QStringLiteral("audio"));
        }
      }
    }
  }

  QXmlStreamWriter writer(&file);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  writer.writeStartDocument();
  writer.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));

  writer.writeStartElement(QStringLiteral("xmeml"));
  writer.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));

  bool success = WriteSequence(&writer, sequence_);

  writer.writeEndElement(); // xmeml
  writer.writeEndDocument();

  file.close();
  return success;
}

bool SaveFCPXMLTask::WriteSequence(QXmlStreamWriter* writer, Sequence* sequence)
{
  writer->writeStartElement(QStringLiteral("sequence"));
  writer->writeAttribute(QStringLiteral("id"), QStringLiteral("sequence-1"));

  // Name
  QString seq_name = sequence->GetLabel();
  if (seq_name.isEmpty()) {
    seq_name = tr("Sequence 1");
  }
  writer->writeTextElement(QStringLiteral("name"), seq_name);

  // Rate & Timebase
  rational fps = sequence->GetVideoParams().frame_rate();
  if (fps <= 0) {
    fps = rational(24, 1);
  }
  rational timebase = fps.flipped();

  // Duration in frames
  int64_t dur_frames = Timecode::time_to_timestamp(sequence->GetLength(), timebase, Timecode::kRound);
  writer->writeTextElement(QStringLiteral("duration"), QString::number(dur_frames));

  // Sequence Rate
  WriteRate(writer, fps);

  // Timecode
  WriteTimecode(writer, fps);

  // Media (Video and Audio Tracks)
  writer->writeStartElement(QStringLiteral("media"));

  // Video Section
  writer->writeStartElement(QStringLiteral("video"));
  writer->writeStartElement(QStringLiteral("format"));
  writer->writeStartElement(QStringLiteral("samplecharacteristics"));
  int w = sequence->GetVideoParams().width();
  int h = sequence->GetVideoParams().height();
  if (w <= 0) w = 1920;
  if (h <= 0) h = 1080;
  writer->writeTextElement(QStringLiteral("width"), QString::number(w));
  writer->writeTextElement(QStringLiteral("height"), QString::number(h));
  writer->writeTextElement(QStringLiteral("pixelaspectratio"), QStringLiteral("square"));
  WriteRate(writer, fps);
  writer->writeEndElement(); // samplecharacteristics
  writer->writeEndElement(); // format

  TrackList* vtracks = sequence->track_list(Track::kVideo);
  if (vtracks) {
    for (int i = 0; i < vtracks->GetTrackCount(); ++i) {
      WriteVideoTrack(writer, vtracks->GetTrackAt(i), i + 1, fps, timebase);
    }
  }
  writer->writeEndElement(); // video

  // Audio Section
  writer->writeStartElement(QStringLiteral("audio"));
  writer->writeTextElement(QStringLiteral("numOutputChannels"), QStringLiteral("2"));
  writer->writeStartElement(QStringLiteral("format"));
  writer->writeStartElement(QStringLiteral("samplecharacteristics"));
  int sample_rate = sequence->GetAudioParams().sample_rate();
  if (sample_rate <= 0) sample_rate = 48000;
  writer->writeTextElement(QStringLiteral("samplerate"), QString::number(sample_rate));
  writer->writeTextElement(QStringLiteral("depth"), QStringLiteral("16"));
  writer->writeEndElement(); // samplecharacteristics
  writer->writeEndElement(); // format

  TrackList* atracks = sequence->track_list(Track::kAudio);
  if (atracks) {
    for (int i = 0; i < atracks->GetTrackCount(); ++i) {
      WriteAudioTrack(writer, atracks->GetTrackAt(i), i + 1, fps, timebase);
    }
  }
  writer->writeEndElement(); // audio

  writer->writeEndElement(); // media

  // Sequence Markers
  WriteMarkers(writer, sequence->GetMarkers(), timebase);

  writer->writeEndElement(); // sequence
  return true;
}

void SaveFCPXMLTask::WriteRate(QXmlStreamWriter* writer, const rational& rate)
{
  int tb;
  bool ntsc;
  if (rate.denominator() == 1001 || Timecode::timebase_is_drop_frame(rate.flipped())) {
    ntsc = true;
    tb = qRound(rate.toDouble() * (1001.0 / 1000.0));
  } else {
    ntsc = false;
    tb = qRound(rate.toDouble());
  }

  writer->writeStartElement(QStringLiteral("rate"));
  writer->writeTextElement(QStringLiteral("timebase"), QString::number(tb));
  writer->writeTextElement(QStringLiteral("ntsc"), ntsc ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
  writer->writeEndElement(); // rate
}

void SaveFCPXMLTask::WriteTimecode(QXmlStreamWriter* writer, const rational& rate)
{
  writer->writeStartElement(QStringLiteral("timecode"));
  WriteRate(writer, rate);
  writer->writeTextElement(QStringLiteral("string"), QStringLiteral("00:00:00:00"));
  writer->writeTextElement(QStringLiteral("frame"), QStringLiteral("0"));
  writer->writeTextElement(QStringLiteral("displayformat"), QStringLiteral("NDF"));
  writer->writeEndElement(); // timecode
}

void SaveFCPXMLTask::WriteVideoTrack(QXmlStreamWriter* writer, Track* track, int track_index,
                                     const rational& rate, const rational& timebase)
{
  writer->writeStartElement(QStringLiteral("track"));
  int clip_index = 1;

  for (Block* b : track->Blocks()) {
    if (ClipBlock* clip = dynamic_cast<ClipBlock*>(b)) {
      WriteClipItem(writer, clip, Track::kVideo, track_index, clip_index++, rate, timebase);
    } else if (TransitionBlock* trans = dynamic_cast<TransitionBlock*>(b)) {
      WriteTransitionItem(writer, trans, rate, timebase);
    }
    // GapBlocks are intentionally skipped: FCP7 XML represents gaps implicitly
  }

  writer->writeEndElement(); // track
}

void SaveFCPXMLTask::WriteAudioTrack(QXmlStreamWriter* writer, Track* track, int track_index,
                                     const rational& rate, const rational& timebase)
{
  writer->writeStartElement(QStringLiteral("track"));
  int clip_index = 1;

  for (Block* b : track->Blocks()) {
    if (ClipBlock* clip = dynamic_cast<ClipBlock*>(b)) {
      WriteClipItem(writer, clip, Track::kAudio, track_index, clip_index++, rate, timebase);
    } else if (TransitionBlock* trans = dynamic_cast<TransitionBlock*>(b)) {
      WriteTransitionItem(writer, trans, rate, timebase);
    }
  }

  writer->writeEndElement(); // track
}

void SaveFCPXMLTask::WriteClipItem(QXmlStreamWriter* writer, ClipBlock* clip, Track::Type type,
                                   int track_index, int clip_index, const rational& rate,
                                   const rational& timebase)
{
  Q_UNUSED(type);
  Q_UNUSED(track_index);
  Q_UNUSED(clip_index);

  QString clip_id = clip_id_map_.value(clip);
  writer->writeStartElement(QStringLiteral("clipitem"));
  if (!clip_id.isEmpty()) {
    writer->writeAttribute(QStringLiteral("id"), clip_id);
  }

  writer->writeTextElement(QStringLiteral("name"), clip->GetLabel());

  int64_t start_frame = Timecode::time_to_timestamp(clip->in(), timebase, Timecode::kRound);
  int64_t end_frame = Timecode::time_to_timestamp(clip->out(), timebase, Timecode::kRound);
  int64_t in_frame = Timecode::time_to_timestamp(clip->media_in(), timebase, Timecode::kRound);
  int64_t dur_frame = end_frame - start_frame;
  int64_t out_frame = in_frame + dur_frame;

  writer->writeTextElement(QStringLiteral("duration"), QString::number(dur_frame));
  WriteRate(writer, rate);
  writer->writeTextElement(QStringLiteral("start"), QString::number(start_frame));
  writer->writeTextElement(QStringLiteral("end"), QString::number(end_frame));
  writer->writeTextElement(QStringLiteral("in"), QString::number(in_frame));
  writer->writeTextElement(QStringLiteral("out"), QString::number(out_frame));

  // File reference
  WriteFileElement(writer, clip, rate, timebase);

  // Links
  WriteLinks(writer, clip);

  writer->writeEndElement(); // clipitem
}

void SaveFCPXMLTask::WriteTransitionItem(QXmlStreamWriter* writer, TransitionBlock* trans,
                                         const rational& rate, const rational& timebase)
{
  writer->writeStartElement(QStringLiteral("transitionitem"));

  rational in_off = trans->in_offset();
  rational out_off = trans->out_offset();
  rational cut_time = trans->in();
  if (trans->connected_out_block()) {
    cut_time = trans->connected_out_block()->out();
  }

  int64_t start_frame = Timecode::time_to_timestamp(cut_time - in_off, timebase, Timecode::kRound);
  int64_t end_frame = Timecode::time_to_timestamp(cut_time + out_off, timebase, Timecode::kRound);

  writer->writeTextElement(QStringLiteral("start"), QString::number(start_frame));
  writer->writeTextElement(QStringLiteral("end"), QString::number(end_frame));

  QString alignment = QStringLiteral("center");
  if (in_off == 0) {
    alignment = QStringLiteral("start-on-edit");
  } else if (out_off == 0) {
    alignment = QStringLiteral("end-on-edit");
  }
  writer->writeTextElement(QStringLiteral("alignment"), alignment);

  WriteRate(writer, rate);
  writer->writeTextElement(QStringLiteral("name"), QStringLiteral("Cross Dissolve"));

  writer->writeStartElement(QStringLiteral("effect"));
  writer->writeTextElement(QStringLiteral("name"), QStringLiteral("Cross Dissolve"));
  writer->writeTextElement(QStringLiteral("effectid"), QStringLiteral("Cross Dissolve"));
  writer->writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
  writer->writeTextElement(QStringLiteral("mediatype"), QStringLiteral("video"));
  writer->writeEndElement(); // effect

  writer->writeEndElement(); // transitionitem
}

void SaveFCPXMLTask::WriteFileElement(QXmlStreamWriter* writer, ClipBlock* clip,
                                      const rational& rate, const rational& timebase)
{
  QVector<Footage*> footage_nodes = clip->FindInputNodes<Footage>();
  Footage* footage = footage_nodes.isEmpty() ? nullptr : footage_nodes.first();

  writer->writeStartElement(QStringLiteral("file"));
  QString file_id = QStringLiteral("file-%1").arg(clip_id_map_.value(clip));
  writer->writeAttribute(QStringLiteral("id"), file_id);

  QString filename = footage ? footage->filename() : clip->GetLabel();
  QFileInfo info(filename);
  writer->writeTextElement(QStringLiteral("name"), info.fileName());

  if (footage && !footage->filename().isEmpty()) {
    writer->writeTextElement(QStringLiteral("pathurl"), QUrl::fromLocalFile(footage->filename()).toString());
  }

  WriteRate(writer, rate);

  int64_t dur = Timecode::time_to_timestamp(clip->length(), timebase, Timecode::kRound);
  if (footage && footage->GetLength() > 0) {
    dur = Timecode::time_to_timestamp(footage->GetLength(), timebase, Timecode::kRound);
  }
  writer->writeTextElement(QStringLiteral("duration"), QString::number(dur));

  writer->writeEndElement(); // file
}

void SaveFCPXMLTask::WriteLinks(QXmlStreamWriter* writer, ClipBlock* clip)
{
  const QVector<Block*>& links = clip->block_links();
  if (links.isEmpty()) {
    return;
  }

  // Create unified set of all mutually linked clips: self + partners
  QVector<ClipBlock*> all_links;
  all_links.append(clip);
  for (Block* b : links) {
    if (ClipBlock* cb = dynamic_cast<ClipBlock*>(b)) {
      if (!all_links.contains(cb)) {
        all_links.append(cb);
      }
    }
  }

  // Sort: video first, then audio; then by coordinates
  std::sort(all_links.begin(), all_links.end(), [this](ClipBlock* a, ClipBlock* b) {
    QString type_a = clip_mediatype_map_.value(a);
    QString type_b = clip_mediatype_map_.value(b);
    if (type_a != type_b) {
      return type_a == QStringLiteral("video");
    }
    return clip_coord_map_.value(a) < clip_coord_map_.value(b);
  });

  for (ClipBlock* linked_clip : all_links) {
    writer->writeStartElement(QStringLiteral("link"));
    writer->writeTextElement(QStringLiteral("linkclipref"), clip_id_map_.value(linked_clip));
    QString mediatype = clip_mediatype_map_.value(linked_clip);
    writer->writeTextElement(QStringLiteral("mediatype"), mediatype);
    writer->writeTextElement(QStringLiteral("trackindex"), QString::number(clip_coord_map_.value(linked_clip).first));
    writer->writeTextElement(QStringLiteral("clipindex"), QString::number(clip_coord_map_.value(linked_clip).second));
    if (mediatype == QStringLiteral("audio")) {
      writer->writeTextElement(QStringLiteral("groupindex"), QStringLiteral("1"));
    }
    writer->writeEndElement(); // link
  }
}

void SaveFCPXMLTask::WriteMarkers(QXmlStreamWriter* writer, TimelineMarkerList* markers,
                                  const rational& timebase)
{
  if (!markers) return;

  for (TimelineMarker* m : *markers) {
    writer->writeStartElement(QStringLiteral("marker"));
    writer->writeTextElement(QStringLiteral("name"), m->name());
    writer->writeTextElement(QStringLiteral("comment"), QString());

    int64_t in_frame = Timecode::time_to_timestamp(m->time().in(), timebase, Timecode::kRound);
    int64_t out_frame = -1;
    if (m->time().length() > 0) {
      out_frame = Timecode::time_to_timestamp(m->time().out(), timebase, Timecode::kRound);
    }
    writer->writeTextElement(QStringLiteral("in"), QString::number(in_frame));
    writer->writeTextElement(QStringLiteral("out"), QString::number(out_frame));
    writer->writeEndElement(); // marker
  }
}

} // namespace olive
