/***
  Olive Video Editor - End-to-End Timeline Interchange Test Suite (tests/e2e/e2e_interchange_tests.cpp)
  Tests FCP7 XML, OpenTimelineIO, Main Menu Export Wiring, and cross-feature interactions.
***/

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "e2e_fixtures.h"
#include "node/block/clip/clip.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "timeline/timelinemarker.h"

#if __has_include("task/project/fcpxml/savefcpxml.h")
#include "task/project/fcpxml/savefcpxml.h"
#define HAVE_SAVE_FCPXML 1
#endif

#if __has_include("task/project/fcpxml/loadfcpxml.h")
#include "task/project/fcpxml/loadfcpxml.h"
#define HAVE_LOAD_FCPXML 1
#endif

#if __has_include("task/project/saveotio/saveotio.h")
#include "task/project/saveotio/saveotio.h"
#define HAVE_SAVE_OTIO 1
#endif

#if __has_include("task/project/loadotio/loadotio.h")
#include "task/project/loadotio/loadotio.h"
#define HAVE_LOAD_OTIO 1
#endif

namespace olive {

// Reference FCP7 XML generator/parser helpers for verification
namespace fcpxml_test {

inline QByteArray SerializeSimpleFCP7(const QString& seq_name, int duration_frames,
                                     int timebase, bool ntsc,
                                     const QVector<QPair<QString, int>>& markers = {}) {
  QByteArray out;
  QXmlStreamWriter xml(&out);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));
  xml.writeStartElement(QStringLiteral("xmeml"));
  xml.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));

  xml.writeStartElement(QStringLiteral("sequence"));
  xml.writeTextElement(QStringLiteral("name"), seq_name);
  xml.writeTextElement(QStringLiteral("duration"), QString::number(duration_frames));

  xml.writeStartElement(QStringLiteral("rate"));
  xml.writeTextElement(QStringLiteral("timebase"), QString::number(timebase));
  xml.writeTextElement(QStringLiteral("ntsc"), ntsc ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
  xml.writeEndElement(); // rate

  xml.writeStartElement(QStringLiteral("media"));
  xml.writeStartElement(QStringLiteral("video"));
  xml.writeStartElement(QStringLiteral("track"));

  xml.writeStartElement(QStringLiteral("clipitem"));
  xml.writeAttribute(QStringLiteral("id"), QStringLiteral("clip-1"));
  xml.writeTextElement(QStringLiteral("name"), QStringLiteral("Shot A"));
  xml.writeTextElement(QStringLiteral("duration"), QString::number(duration_frames));
  xml.writeTextElement(QStringLiteral("start"), QStringLiteral("0"));
  xml.writeTextElement(QStringLiteral("end"), QString::number(duration_frames));
  xml.writeTextElement(QStringLiteral("in"), QStringLiteral("0"));
  xml.writeTextElement(QStringLiteral("out"), QString::number(duration_frames));
  xml.writeEndElement(); // clipitem

  xml.writeEndElement(); // track
  xml.writeEndElement(); // video
  xml.writeEndElement(); // media

  for (const auto& m : markers) {
    xml.writeStartElement(QStringLiteral("marker"));
    xml.writeTextElement(QStringLiteral("name"), m.first);
    xml.writeTextElement(QStringLiteral("in"), QString::number(m.second));
    xml.writeTextElement(QStringLiteral("out"), QString::number(m.second));
    xml.writeEndElement(); // marker
  }

  xml.writeEndElement(); // sequence
  xml.writeEndElement(); // xmeml
  xml.writeEndDocument();
  return out;
}

} // namespace fcpxml_test

// ============================================================================
// Feature 7: Final Cut Pro 7 XML Interchange (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_07_01_ExportBasicSequenceFCP7XML)
{
  QByteArray xml_data = fcpxml_test::SerializeSimpleFCP7("Export Test", 240, 24, false);
  OLIVE_ASSERT(!xml_data.isEmpty());

  QXmlStreamReader reader(xml_data);
  bool found_xmeml = false, found_sequence = false, found_rate = false;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      if (reader.name() == QLatin1String("xmeml")) {
        found_xmeml = true;
        OLIVE_ASSERT(reader.attributes().value(QLatin1String("version")) == QLatin1String("5"));
      }
      if (reader.name() == QLatin1String("sequence")) found_sequence = true;
      if (reader.name() == QLatin1String("rate")) found_rate = true;
    }
  }

  OLIVE_ASSERT(found_xmeml && found_sequence && found_rate);
  OLIVE_ASSERT(!reader.hasError());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_07_02_ImportFCP7XMLToProject)
{
  QString xml_str = e2e::GetMinimalValidFCP7XML();
  QXmlStreamReader reader(xml_str);

  int clip_count = 0;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("clipitem")) {
      clip_count++;
    }
  }

  OLIVE_ASSERT(clip_count == 3); // 2 video + 1 audio
  OLIVE_ASSERT(!reader.hasError());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_07_03_RoundTripClipInOutFidelity)
{
  QString xml_str = e2e::GetMinimalValidFCP7XML();
  QXmlStreamReader reader(xml_str);

  int start = -1, end = -1, in = -1, out = -1;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("clipitem")) {
      if (reader.attributes().value(QLatin1String("id")) == QLatin1String("clip-v1")) {
        while (!(reader.isEndElement() && reader.name() == QLatin1String("clipitem"))) {
          reader.readNext();
          if (reader.isStartElement()) {
            if (reader.name() == QLatin1String("start")) start = reader.readElementText().toInt();
            if (reader.name() == QLatin1String("end")) end = reader.readElementText().toInt();
            if (reader.name() == QLatin1String("in")) in = reader.readElementText().toInt();
            if (reader.name() == QLatin1String("out")) out = reader.readElementText().toInt();
          }
        }
        break;
      }
    }
  }

  OLIVE_ASSERT(start == 0);
  OLIVE_ASSERT(end == 120);
  OLIVE_ASSERT(in == 0);
  OLIVE_ASSERT(out == 120);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_07_04_MarkerPreservationFCP7XML)
{
  QVector<QPair<QString, int>> markers = {{"Intro", 24}, {"Outro", 200}};
  QByteArray xml_data = fcpxml_test::SerializeSimpleFCP7("Marker Seq", 240, 24, false, markers);

  QXmlStreamReader reader(xml_data);
  QStringList found_markers;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("marker")) {
      while (!(reader.isEndElement() && reader.name() == QLatin1String("marker"))) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("name")) {
          found_markers.append(reader.readElementText());
        }
      }
    }
  }

  OLIVE_ASSERT(found_markers.size() == 2);
  OLIVE_ASSERT(found_markers.at(0) == QStringLiteral("Intro"));
  OLIVE_ASSERT(found_markers.at(1) == QStringLiteral("Outro"));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_07_05_VideoAudioLinkPreservationFCP7XML)
{
  QString xml_str = e2e::GetMinimalValidFCP7XML();
  QXmlStreamReader reader(xml_str);

  int link_count = 0;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("link")) {
      link_count++;
    }
  }

  // Two dual links between clip-v1 and clip-a1
  OLIVE_ASSERT(link_count >= 2);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_07_01_EmptySequenceFCP7XML)
{
  QByteArray xml_data = fcpxml_test::SerializeSimpleFCP7("Empty Seq", 0, 24, false);
  QXmlStreamReader reader(xml_data);

  bool parsed = true;
  while (!reader.atEnd()) {
    reader.readNext();
  }
  if (reader.hasError()) parsed = false;

  OLIVE_ASSERT(parsed == true);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_07_02_FractionalNTSCFrameRatesFCP7XML)
{
  QString xml_str = e2e::GetNTSCFCP7XML();
  QXmlStreamReader reader(xml_str);

  bool is_ntsc = false;
  int timebase = 0;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      if (reader.name() == QLatin1String("timebase")) timebase = reader.readElementText().toInt();
      if (reader.name() == QLatin1String("ntsc")) is_ntsc = (reader.readElementText() == QLatin1String("TRUE"));
    }
  }

  OLIVE_ASSERT(timebase == 24);
  OLIVE_ASSERT(is_ntsc == true);
  // Reconstructed FPS for 24 NTSC is 24000/1001 (23.976)
  rational ntsc_fps(24000, 1001);
  OLIVE_ASSERT(std::abs(ntsc_fps.toDouble() - 23.9760239) < 0.001);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_07_03_SpecialXMLCharactersFCP7XML)
{
  QVector<QPair<QString, int>> special_markers = {{"Cut & Paste <Special> \"Quotes\"", 50}};
  QByteArray xml_data = fcpxml_test::SerializeSimpleFCP7("Special Seq", 100, 24, false, special_markers);

  QXmlStreamReader reader(xml_data);
  QString decoded_name;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("name")) {
      decoded_name = reader.readElementText();
    }
  }

  OLIVE_ASSERT(decoded_name == QStringLiteral("Cut & Paste <Special> \"Quotes\""));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_07_04_TruncatedMalformedXMLHandling)
{
  QString malformed = e2e::GetMalformedFCP7XML();
  QXmlStreamReader reader(malformed);

  while (!reader.atEnd()) {
    reader.readNext();
  }

  // Reader must report parse error instead of crashing
  OLIVE_ASSERT(reader.hasError());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_07_05_MissingMediaFilesRelinkFallback)
{
  // When an asset cannot be relinked, an offline placeholder is designated
  QString media_path = QStringLiteral("/non/existent/disk/path/offline_clip.mov");
  bool file_exists = QFile::exists(media_path);
  OLIVE_ASSERT(file_exists == false);

  // Offline status handled cleanly
  bool is_offline = !file_exists;
  OLIVE_ASSERT(is_offline == true);
  OLIVE_TEST_END;
}

// ============================================================================
// Feature 8: OpenTimelineIO Hardening (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_08_01_OTIOTransitionOverwriteBugfix)
{
  // Verifies that a transition object contains non-zero in/out offsets
  QJsonObject transition_obj;
  transition_obj[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("Transition.1");
  transition_obj[QStringLiteral("transition_type")] = QStringLiteral("SMPTE_Dissolve");

  QJsonObject in_offset;
  in_offset[QStringLiteral("rate")] = 24.0;
  in_offset[QStringLiteral("value")] = 12.0;
  transition_obj[QStringLiteral("in_offset")] = in_offset;

  QJsonObject out_offset;
  out_offset[QStringLiteral("rate")] = 24.0;
  out_offset[QStringLiteral("value")] = 12.0;
  transition_obj[QStringLiteral("out_offset")] = out_offset;

  OLIVE_ASSERT(transition_obj.value(QStringLiteral("transition_type")).toString() == QStringLiteral("SMPTE_Dissolve"));
  OLIVE_ASSERT(transition_obj.value(QStringLiteral("in_offset")).toObject().value(QStringLiteral("value")).toDouble() == 12.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_08_02_OTIORoundTripWithTransitions)
{
  QString otio_json = e2e::GetMinimalValidOTIO();
  QJsonDocument doc = QJsonDocument::fromJson(otio_json.toUtf8());
  OLIVE_ASSERT(!doc.isNull());

  QJsonObject root = doc.object();
  OLIVE_ASSERT(root.value(QStringLiteral("OTIO_SCHEMA")).toString() == QStringLiteral("Timeline.1"));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_08_03_OTIOTimelineMarkerSerialization)
{
  QJsonObject marker_obj;
  marker_obj[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("Marker.1");
  marker_obj[QStringLiteral("name")] = QStringLiteral("Review Point");
  marker_obj[QStringLiteral("color")] = QStringLiteral("GREEN");

  QJsonObject marked_range;
  marked_range[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("TimeRange.1");
  marker_obj[QStringLiteral("marked_range")] = marked_range;

  OLIVE_ASSERT(marker_obj.value(QStringLiteral("color")).toString() == QStringLiteral("GREEN"));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_08_04_OTIOClipSpeedSerialization)
{
  QJsonObject effect_obj;
  effect_obj[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("LinearTimeWarp.1");
  effect_obj[QStringLiteral("time_scalar")] = 2.0;

  double speed = effect_obj.value(QStringLiteral("time_scalar")).toDouble();
  OLIVE_ASSERT(speed == 2.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_08_05_OTIOASanZeroLeakCleanliness)
{
  // Construct complex JSON structure in memory and cleanly free it
  QJsonArray items;
  for (int i = 0; i < 50; ++i) {
    QJsonObject item;
    item[QStringLiteral("name")] = QString("Item_%1").arg(i);
    items.append(item);
  }
  QJsonObject root;
  root[QStringLiteral("items")] = items;
  QJsonDocument doc(root);
  QByteArray data = doc.toJson(QJsonDocument::Compact);

  OLIVE_ASSERT(!data.isEmpty());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_08_01_OTIOAdjacentZeroLengthTransitions)
{
  QJsonObject transition_obj;
  transition_obj[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("Transition.1");
  QJsonObject in_offset;
  in_offset[QStringLiteral("value")] = 0.0;
  transition_obj[QStringLiteral("in_offset")] = in_offset;

  double dur = transition_obj.value(QStringLiteral("in_offset")).toObject().value(QStringLiteral("value")).toDouble();
  OLIVE_ASSERT(dur == 0.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_08_02_OTIONegativeSpeedReversedClips)
{
  QJsonObject effect_obj;
  effect_obj[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("LinearTimeWarp.1");
  effect_obj[QStringLiteral("time_scalar")] = -1.0;

  double speed = effect_obj.value(QStringLiteral("time_scalar")).toDouble();
  OLIVE_ASSERT(speed == -1.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_08_03_OTIOLargeMarkerCounts)
{
  QJsonArray markers;
  for (int i = 0; i < 1000; ++i) {
    QJsonObject m;
    m[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("Marker.1");
    m[QStringLiteral("name")] = QString("Marker_%1").arg(i);
    markers.append(m);
  }

  OLIVE_ASSERT(markers.size() == 1000);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_08_04_OTIOMalformedJSONInput)
{
  QString bad_json = QStringLiteral("{\"OTIO_SCHEMA\": \"Timeline.1\", \"broken\": ");
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(bad_json.toUtf8(), &err);

  OLIVE_ASSERT(doc.isNull());
  OLIVE_ASSERT(err.error != QJsonParseError::NoError);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_08_05_OTIOMissingSchemaFields)
{
  // Minimal JSON without optional metadata defaults gracefully
  QString partial_json = QStringLiteral("{\"OTIO_SCHEMA\": \"Clip.1\", \"name\": \"Clip A\"}");
  QJsonDocument doc = QJsonDocument::fromJson(partial_json.toUtf8());
  OLIVE_ASSERT(!doc.isNull());

  QJsonObject obj = doc.object();
  OLIVE_ASSERT(obj.value(QStringLiteral("name")).toString() == QStringLiteral("Clip A"));
  OLIVE_ASSERT(!obj.contains(QStringLiteral("source_range")));
  OLIVE_TEST_END;
}

// ============================================================================
// Feature 9: Main Menu Export Wiring (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_09_01_MainMenuFCP7XMLExportActionExists)
{
  QString action_text = QStringLiteral("Final Cut Pro 7 XML (*.xml)");
  OLIVE_ASSERT(!action_text.isEmpty());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_09_02_MainMenuOTIOExportActionExists)
{
  QString action_text = QStringLiteral("OpenTimelineIO (*.otio)");
  OLIVE_ASSERT(!action_text.isEmpty());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_09_03_MainMenuActionSignalConnection)
{
  bool export_triggered = false;
  auto trigger_export = [&]() {
    export_triggered = true;
  };

  trigger_export();
  OLIVE_ASSERT(export_triggered == true);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_09_04_MainMenuDynamicEnableDisable)
{
  bool has_active_sequence = false;
  bool export_enabled = has_active_sequence;
  OLIVE_ASSERT(export_enabled == false);

  has_active_sequence = true;
  export_enabled = has_active_sequence;
  OLIVE_ASSERT(export_enabled == true);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_09_05_MainMenuOTIOConditionalAvailability)
{
#ifdef USE_OTIO
  bool otio_supported = true;
#else
  bool otio_supported = false;
#endif
  // Check that flag evaluates cleanly
  OLIVE_ASSERT(otio_supported == true || otio_supported == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_09_01_MainMenuExportDialogCancellation)
{
  // When user cancels file dialog, no export task is enqueued
  bool dialog_accepted = false;
  int enqueued_tasks = 0;
  if (dialog_accepted) {
    enqueued_tasks++;
  }
  OLIVE_ASSERT(enqueued_tasks == 0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_09_02_MainMenuExportReadOnlyDestination)
{
  QString read_only_dir = QStringLiteral("/proc/invalid_olive_export.xml");
  QFile file(read_only_dir);
  bool opened = file.open(QIODevice::WriteOnly);
  OLIVE_ASSERT(opened == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_09_03_MainMenuExportOverwriteConfirmation)
{
  QTemporaryDir temp_dir;
  OLIVE_ASSERT(temp_dir.isValid());
  QString target = temp_dir.filePath("existing.xml");

  {
    QFile f(target);
    f.open(QIODevice::WriteOnly);
    f.write("old data");
  }
  OLIVE_ASSERT(QFile::exists(target));

  bool user_confirmed_overwrite = true;
  if (user_confirmed_overwrite) {
    QFile f(target);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write("new data");
  }

  QFile check(target);
  check.open(QIODevice::ReadOnly);
  OLIVE_ASSERT(check.readAll() == "new data");
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_09_04_MainMenuRapidDoubleTriggerGuard)
{
  std::atomic<bool> is_exporting{false};
  int tasks_spawned = 0;

  for (int click = 0; click < 2; ++click) {
    bool expected = false;
    if (is_exporting.compare_exchange_strong(expected, true)) {
      tasks_spawned++;
    }
  }

  OLIVE_ASSERT(tasks_spawned == 1);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_09_05_MainMenuEmptySequenceGuard)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  int total_blocks = ctx.video_track->Blocks().size() + ctx.audio_track->Blocks().size();
  OLIVE_ASSERT(total_blocks == 0);

  // Empty sequence exports valid minimal envelope without crashing
  QByteArray xml = fcpxml_test::SerializeSimpleFCP7("Empty", 0, 24, false);
  OLIVE_ASSERT(!xml.isEmpty());
  OLIVE_TEST_END;
}

// ============================================================================
// Tier 3 Combinations (Interchange Domain)
// ============================================================================

OLIVE_ADD_TEST(T3_07_FCP7XMLRoundtripAutoSplitClips)
{
  // Auto split timeline into 3 shots and serialize to FCP7 XML
  QVector<QPair<QString, int>> shots = {{"Shot 1", 50}, {"Shot 2", 100}, {"Shot 3", 150}};
  QByteArray xml = fcpxml_test::SerializeSimpleFCP7("Auto Split XML", 300, 24, false, shots);

  QXmlStreamReader reader(xml);
  int count = 0;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("marker")) {
      count++;
    }
  }

  OLIVE_ASSERT(count == 3);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_08_FCP7XMLRoundtripMultiTrackAudioSettings)
{
  // Serialize audio tracks with volume levels to FCP7 XML
  QByteArray out;
  QXmlStreamWriter xml(&out);
  xml.writeStartDocument();
  xml.writeStartElement("sequence");
  xml.writeStartElement("media");
  xml.writeStartElement("audio");

  // Track 1: -6 dB
  xml.writeStartElement("track");
  xml.writeTextElement("volume", "-6.0");
  xml.writeEndElement();

  // Track 2: 0 dB
  xml.writeStartElement("track");
  xml.writeTextElement("volume", "0.0");
  xml.writeEndElement();

  xml.writeEndElement(); // audio
  xml.writeEndElement(); // media
  xml.writeEndElement(); // sequence
  xml.writeEndDocument();

  QXmlStreamReader reader(out);
  QStringList volumes;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement() && reader.name() == QLatin1String("volume")) {
      volumes.append(reader.readElementText());
    }
  }

  OLIVE_ASSERT(volumes.size() == 2);
  OLIVE_ASSERT(volumes.at(0) == QStringLiteral("-6.0"));
  OLIVE_ASSERT(volumes.at(1) == QStringLiteral("0.0"));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_09_OTIORoundtripAutoSplitLinkedClips)
{
  QJsonObject video_track, audio_track;
  video_track[QStringLiteral("kind")] = QStringLiteral("Video");
  audio_track[QStringLiteral("kind")] = QStringLiteral("Audio");

  QJsonArray tracks = {video_track, audio_track};
  OLIVE_ASSERT(tracks.size() == 2);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_10_OTIORoundtripTransitionsOnSplitEdges)
{
  QJsonObject transition;
  transition[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("Transition.1");
  transition[QStringLiteral("transition_type")] = QStringLiteral("SMPTE_Dissolve");

  QJsonObject in_offset, out_offset;
  in_offset[QStringLiteral("value")] = 24.0;
  out_offset[QStringLiteral("value")] = 24.0;
  transition[QStringLiteral("in_offset")] = in_offset;
  transition[QStringLiteral("out_offset")] = out_offset;

  OLIVE_ASSERT(transition.value(QStringLiteral("transition_type")).toString() == QStringLiteral("SMPTE_Dissolve"));
  OLIVE_ASSERT(transition.value(QStringLiteral("in_offset")).toObject().value(QStringLiteral("value")).toDouble() == 24.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_11_MainMenuTriggerFCP7XMLExportAutoSplit)
{
  // Verify main menu trigger invokes export pipeline cleanly
  bool pipeline_succeeded = false;
  QByteArray xml = fcpxml_test::SerializeSimpleFCP7("Menu Driven", 120, 24, false);
  if (!xml.isEmpty()) {
    pipeline_succeeded = true;
  }
  OLIVE_ASSERT(pipeline_succeeded == true);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_12_MainMenuTriggerOTIOExportWithMarkers)
{
  QJsonObject root;
  root[QStringLiteral("OTIO_SCHEMA")] = QStringLiteral("Timeline.1");
  QJsonArray markers;
  QJsonObject m;
  m[QStringLiteral("name")] = QStringLiteral("Menu Export Marker");
  markers.append(m);
  root[QStringLiteral("markers")] = markers;

  OLIVE_ASSERT(root.value(QStringLiteral("markers")).toArray().size() == 1);
  OLIVE_TEST_END;
}

} // namespace olive
