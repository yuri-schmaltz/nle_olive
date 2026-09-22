// Exercise Olive's importer/exporter, not an independent XML implementation.
#include "e2e_fixtures.h"
#include "e2e_environment.h"
#include "task/project/fcpxml/loadfcpxml.h"
#include "task/project/fcpxml/savefcpxml.h"

namespace olive {
OLIVE_ADD_TEST(ImportedTimelineSplitExportReimport)
{
  e2e::Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  const QString source = dir.filePath("source.xml");
  QFile file(source);
  OLIVE_ASSERT(file.open(QIODevice::WriteOnly));
  const auto bytes = e2e::GetMinimalValidFCP7XML().toUtf8();
  OLIVE_ASSERT_EQUAL(file.write(bytes), bytes.size());
  file.close();
  LoadFCPXMLTask import(source);
  OLIVE_ASSERT(import.Start());
  std::unique_ptr<Project> project(import.GetLoadedProject());
  OLIVE_ASSERT(project != nullptr);
  const auto sequences = project->root()->ListChildrenOfType<Sequence>();
  OLIVE_ASSERT_EQUAL(sequences.size(), 1);
  auto *sequence = sequences.first();
  auto *video = sequence->track_list(Track::kVideo)->GetTrackAt(0);
  auto *audio = sequence->track_list(Track::kAudio)->GetTrackAt(0);
  OLIVE_ASSERT(video != nullptr && audio != nullptr);
  OLIVE_ASSERT_EQUAL(video->Blocks().size(), 2);
  OLIVE_ASSERT_EQUAL(audio->Blocks().size(), 1);
  OLIVE_ASSERT(Node::AreLinked(video->Blocks().first(), audio->Blocks().first()));

  BlockSplitPreservingLinksCommand split({video->Blocks().first(), audio->Blocks().first()}, {rational(2)});
  split.redo_now();
  OLIVE_ASSERT_EQUAL(video->Blocks().size(), 3);
  OLIVE_ASSERT_EQUAL(audio->Blocks().size(), 2);
  sequence->SetLabel(QString::fromUtf8("Edição & revisão <final>"));
  const QString destination = dir.filePath("edited.xml");
  SaveFCPXMLTask save(sequence, destination);
  OLIVE_ASSERT(save.Start());
  LoadFCPXMLTask reload(destination);
  OLIVE_ASSERT(reload.Start());
  std::unique_ptr<Project> loaded(reload.GetLoadedProject());
  OLIVE_ASSERT(loaded != nullptr);
  const auto loaded_sequences = loaded->root()->ListChildrenOfType<Sequence>();
  OLIVE_ASSERT_EQUAL(loaded_sequences.size(), 1);
  auto *result = loaded_sequences.first();
  OLIVE_ASSERT_EQUAL(result->GetLabel(), sequence->GetLabel());
  OLIVE_ASSERT_EQUAL(result->GetVideoParams().frame_rate(), rational(24));
  for (Track::Type type : {Track::kVideo, Track::kAudio}) {
    auto *before = sequence->track_list(type)->GetTrackAt(0);
    auto *after = result->track_list(type)->GetTrackAt(0);
    OLIVE_ASSERT(after != nullptr);
    OLIVE_ASSERT_EQUAL(after->Blocks().size(), before->Blocks().size());
    for (int i = 0; i < before->Blocks().size(); ++i) {
      OLIVE_ASSERT_EQUAL(after->Blocks().at(i)->in(), before->Blocks().at(i)->in());
      OLIVE_ASSERT_EQUAL(after->Blocks().at(i)->length(), before->Blocks().at(i)->length());
      auto *before_clip = dynamic_cast<ClipBlock*>(before->Blocks().at(i));
      auto *after_clip = dynamic_cast<ClipBlock*>(after->Blocks().at(i));
      OLIVE_ASSERT(before_clip != nullptr && after_clip != nullptr);
      OLIVE_ASSERT_EQUAL(after_clip->media_in(), before_clip->media_in());
    }
  }
  auto *loaded_video = result->track_list(Track::kVideo)->GetTrackAt(0);
  auto *loaded_audio = result->track_list(Track::kAudio)->GetTrackAt(0);
  for (int i = 0; i < 2; ++i) {
    OLIVE_ASSERT(Node::AreLinked(loaded_video->Blocks().at(i), loaded_audio->Blocks().at(i)));
  }
  OLIVE_ASSERT_EQUAL(result->GetMarkers()->size(), sequence->GetMarkers()->size());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(FCPXMLExportFailureReported)
{
  e2e::Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  auto ctx = e2e::SetupStandardTimeline();
  SaveFCPXMLTask task(ctx.sequence, dir.filePath("missing/output.xml"));
  OLIVE_ASSERT(!task.Start());
  OLIVE_ASSERT(!task.GetError().isEmpty());
  OLIVE_TEST_END;
}
}
