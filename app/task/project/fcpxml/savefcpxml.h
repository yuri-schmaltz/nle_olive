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

#ifndef OLIVE_SAVEFCPXMLTASK_H
#define OLIVE_SAVEFCPXMLTASK_H

#include <QHash>
#include <QPair>
#include <QString>
#include <QXmlStreamWriter>

#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "task/task.h"

namespace olive {

class ClipBlock;
class TransitionBlock;
class Footage;
class TimelineMarkerList;

/**
 * @brief Background task to serialize an Olive Sequence or Project into Final Cut Pro 7 XML (xmeml v5).
 */
class SaveFCPXMLTask : public Task
{
  Q_OBJECT
public:
  SaveFCPXMLTask(Sequence* sequence, const QString& filename);
  SaveFCPXMLTask(Project* project, const QString& filename);

protected:
  virtual bool Run() override;

private:
  bool WriteSequence(QXmlStreamWriter* writer, Sequence* sequence);
  void WriteRate(QXmlStreamWriter* writer, const rational& rate);
  void WriteTimecode(QXmlStreamWriter* writer, const rational& rate);
  void WriteVideoTrack(QXmlStreamWriter* writer, Track* track, int track_index,
                       const rational& rate, const rational& timebase);
  void WriteAudioTrack(QXmlStreamWriter* writer, Track* track, int track_index,
                       const rational& rate, const rational& timebase);
  void WriteClipItem(QXmlStreamWriter* writer, ClipBlock* clip, Track::Type type,
                     int track_index, int clip_index, const rational& rate,
                     const rational& timebase);
  void WriteTransitionItem(QXmlStreamWriter* writer, TransitionBlock* trans,
                           const rational& rate, const rational& timebase);
  void WriteFileElement(QXmlStreamWriter* writer, ClipBlock* clip,
                        const rational& rate, const rational& timebase);
  void WriteLinks(QXmlStreamWriter* writer, ClipBlock* clip);
  void WriteMarkers(QXmlStreamWriter* writer, TimelineMarkerList* markers,
                    const rational& timebase);

  Sequence* sequence_;
  Project* project_;
  QString filename_;

  // Pre-calculated mappings for dual <link> tags:
  QHash<ClipBlock*, QString> clip_id_map_;
  QHash<ClipBlock*, QPair<int, int>> clip_coord_map_;
  QHash<ClipBlock*, QString> clip_mediatype_map_;
};

} // namespace olive

#endif // OLIVE_SAVEFCPXMLTASK_H
