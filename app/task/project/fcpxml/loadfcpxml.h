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

#ifndef OLIVE_LOADFCPXMLTASK_H
#define OLIVE_LOADFCPXMLTASK_H

#include <QHash>
#include <QMultiHash>
#include <QString>
#include <QXmlStreamReader>

#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "task/project/load/loadbasetask.h"

namespace olive {

class ClipBlock;
class Footage;
class Folder;
class TimelineMarkerList;

/**
 * @brief Background task to load and parse Final Cut Pro 7 XML (xmeml) into an Olive Project.
 */
class LoadFCPXMLTask : public ProjectLoadBaseTask
{
  Q_OBJECT
public:
  LoadFCPXMLTask(const QString& filename);

protected:
  virtual bool Run() override;

private:
  bool ParseXMeml(QXmlStreamReader* reader);
  bool ParseSequence(QXmlStreamReader* reader);
  bool ParseRate(QXmlStreamReader* reader, rational& rate);
  bool ParseMedia(QXmlStreamReader* reader, Sequence* sequence, const rational& rate, const rational& timebase);
  bool ParseVideo(QXmlStreamReader* reader, Sequence* sequence, const rational& rate, const rational& timebase);
  bool ParseAudio(QXmlStreamReader* reader, Sequence* sequence, const rational& rate, const rational& timebase);
  bool ParseTrack(QXmlStreamReader* reader, Track* track, Track::Type type,
                  const rational& rate, const rational& timebase);
  bool ParseClipItem(QXmlStreamReader* reader, Track* track, Track::Type type,
                     int64_t& current_frame, const rational& rate, const rational& timebase,
                     Block*& previous_block, bool& previous_was_transition);
  bool ParseTransitionItem(QXmlStreamReader* reader, Track* track,
                           const rational& rate, const rational& timebase,
                           Block*& previous_block, bool& previous_was_transition);
  bool ParseMarker(QXmlStreamReader* reader, TimelineMarkerList* markers, const rational& timebase);
  bool ParseFile(QXmlStreamReader* reader, QString& file_id, QString& name,
                 QString& pathurl, rational& duration, const rational& timebase);

  QString ResolvePathUrl(const QString& pathurl) const;

  // Track parsed clips and links
  QHash<QString, ClipBlock*> clip_id_map_;
  QMultiHash<ClipBlock*, QString> pending_links_;
  QHash<QString, Footage*> imported_footage_;
  Folder* sequence_footage_;
  Sequence* sequence_;
};

} // namespace olive

#endif // OLIVE_LOADFCPXMLTASK_H
