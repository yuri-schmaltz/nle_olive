// Headless editorial workflow through production import, edit, save and export APIs.
#include "e2e_fixtures.h"
#include "e2e_environment.h"
#include <QImage>
#include "codec/oiio/oiiodecoder.h"
#include "node/nodeundo.h"
#include "task/project/import/import.h"
#include "task/project/fcpxml/loadfcpxml.h"
#include "task/project/fcpxml/savefcpxml.h"
#include "task/scenecut/scenecuttask.h"

namespace olive {
OLIVE_ADD_TEST(StillImageImportUsesOIIO)
{
  e2e::Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  const QString filename = dir.filePath("image.PNG");
  QImage image(16, 8, QImage::Format_RGBA8888);
  image.fill(Qt::red);
  OLIVE_ASSERT(image.save(filename, "PNG"));
  OIIODecoder decoder;
  const auto description = decoder.Probe(filename, nullptr);
  OLIVE_ASSERT(description.IsValid());
  OLIVE_ASSERT_EQUAL(description.GetVideoStreams().size(), 1);
  OLIVE_ASSERT_EQUAL(description.GetVideoStreams().first().width(), 16);
  OLIVE_ASSERT_EQUAL(description.GetVideoStreams().first().height(), 8);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(MediaImportDetectSplitSaveReopenExport)
{
  e2e::Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  const QString media = dir.filePath("shots.mp4");
  OLIVE_ASSERT(e2e::GenerateSyntheticVideo(media, {{"black", 1}, {"white", 1}, {"black", 1}}));
  Project project;
  project.Initialize();
  ProjectImportTask import(project.root(), {media});
  OLIVE_ASSERT(import.Start());
  std::unique_ptr<MultiUndoCommand> imported(import.GetCommand());
  OLIVE_ASSERT(!import.HasInvalidFiles());
  OLIVE_ASSERT_EQUAL(import.GetImportedFootage().size(), 1);
  OLIVE_ASSERT(imported != nullptr);
  imported->redo_now();
  Footage *footage = import.GetImportedFootage().first();
  OLIVE_ASSERT(footage->IsValid());
  OLIVE_ASSERT_EQUAL(footage->decoder(), QStringLiteral("ffmpeg"));

  auto *sequence = new Sequence();
  sequence->setParent(&project);
  VideoParams vp(320, 240, PixelFormat::F32, 4);
  vp.set_frame_rate(rational(25));
  sequence->SetVideoParams(vp);
  sequence->SetLabel(QString::fromUtf8("Montagem de teste"));
  auto *track = TimelineAddTrackCommand::RunImmediately(sequence->track_list(Track::kVideo));
  auto *clip = new ClipBlock();
  clip->setParent(&project);
  clip->set_length_and_media_out(rational(3));
  Node::ConnectEdge(footage, NodeInput(clip, ClipBlock::kBufferIn));
  track->AppendBlock(clip);

  SceneCutTask detect(clip);
  OLIVE_ASSERT(detect.Start());
  OLIVE_ASSERT_EQUAL(detect.detected_cuts().size(), 2);
  OLIVE_ASSERT_EQUAL(detect.detected_cuts().at(0), rational(1));
  OLIVE_ASSERT_EQUAL(detect.detected_cuts().at(1), rational(2));
  BlockSplitPreservingLinksCommand split({clip}, detect.detected_cuts().toList());
  split.redo_now();
  OLIVE_ASSERT_EQUAL(track->Blocks().size(), 3);
  split.undo_now();
  OLIVE_ASSERT_EQUAL(track->Blocks().size(), 1);
  split.redo_now();
  OLIVE_ASSERT_EQUAL(track->Blocks().size(), 3);

  const QString saved = dir.filePath(QString::fromUtf8("edição.ove"));
  ProjectSerializer::SaveData data(ProjectSerializer::kProject, &project, saved);
  OLIVE_ASSERT(ProjectSerializer::Save(data, true).code() == ProjectSerializer::kSuccess);
  Project reopened;
  OLIVE_ASSERT(ProjectSerializer::Load(&reopened, saved, ProjectSerializer::kProject).code() == ProjectSerializer::kSuccess);
  Sequence *restored = nullptr;
  for (Node *node : reopened.nodes()) {
    if (auto *candidate = dynamic_cast<Sequence*>(node)) restored = candidate;
  }
  OLIVE_ASSERT(restored != nullptr);
  OLIVE_ASSERT_EQUAL(restored->GetLabel(), sequence->GetLabel());
  auto *restored_track = restored->track_list(Track::kVideo)->GetTrackAt(0);
  OLIVE_ASSERT(restored_track != nullptr);
  OLIVE_ASSERT_EQUAL(restored_track->Blocks().size(), 3);
  for (int i = 0; i < 3; ++i) {
    auto *block = dynamic_cast<ClipBlock*>(restored_track->Blocks().at(i));
    OLIVE_ASSERT(block != nullptr);
    OLIVE_ASSERT_EQUAL(block->in(), rational(i));
    OLIVE_ASSERT_EQUAL(block->length(), rational(1));
    OLIVE_ASSERT_EQUAL(block->media_in(), rational(i));
    const auto sources = Node::FindInputNodesConnectedToInput<Footage>(NodeInput(block, ClipBlock::kBufferIn));
    OLIVE_ASSERT_EQUAL(sources.size(), 1);
    OLIVE_ASSERT_EQUAL(sources.first()->filename(), media);
  }
  const QString xml = dir.filePath("export.xml");
  SaveFCPXMLTask export_task(restored, xml);
  OLIVE_ASSERT(export_task.Start());
  LoadFCPXMLTask load_export(xml);
  OLIVE_ASSERT(load_export.Start());
  std::unique_ptr<Project> exported(load_export.GetLoadedProject());
  OLIVE_ASSERT(exported != nullptr);
  const auto exported_sequences = exported->root()->ListChildrenOfType<Sequence>();
  OLIVE_ASSERT_EQUAL(exported_sequences.size(), 1);
  auto *exported_track = exported_sequences.first()->track_list(Track::kVideo)->GetTrackAt(0);
  OLIVE_ASSERT(exported_track != nullptr);
  OLIVE_ASSERT_EQUAL(exported_track->Blocks().size(), 3);
  for (int i = 0; i < 3; ++i) {
    OLIVE_ASSERT_EQUAL(exported_track->Blocks().at(i)->in(), rational(i));
    OLIVE_ASSERT_EQUAL(exported_track->Blocks().at(i)->length(), rational(1));
    auto *block = dynamic_cast<ClipBlock*>(exported_track->Blocks().at(i));
    OLIVE_ASSERT(block != nullptr);
    OLIVE_ASSERT_EQUAL(block->media_in(), rational(i));
  }
  OLIVE_TEST_END;
}
}
