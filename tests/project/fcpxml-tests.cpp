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

#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "config/config.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"
#include "task/project/fcpxml/loadfcpxml.h"
#include "task/project/fcpxml/savefcpxml.h"
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

bool WriteFileBytes(const QString &filename, const QByteArray &bytes) {
  QFile file(filename);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

OLIVE_ADD_TEST(FCPXML_SequenceRoundTrip_ClipsGapsMarkersLinks)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QString xml_file = dir.filePath(QStringLiteral("sequence_roundtrip.xml"));

  Project project;
  project.Initialize();

  Sequence* sequence = new Sequence();
  sequence->setParent(&project);
  sequence->SetLabel(QStringLiteral("Editorial Timeline 1"));

  // Set sequence parameters: 24 fps 1920x1080
  VideoParams vparams;
  vparams.set_frame_rate(rational(24, 1));
  vparams.set_width(1920);
  vparams.set_height(1080);
  sequence->SetVideoParams(vparams);

  // Setup tracks: 1 Video, 1 Audio
  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();
  OLIVE_ASSERT(vtrack != nullptr);

  TimelineAddTrackCommand add_atrack(sequence->track_list(Track::kAudio));
  add_atrack.redo_now();
  Track* atrack = add_atrack.track();
  OLIVE_ASSERT(atrack != nullptr);

  // Video Clip 1: [0s to 5s]
  ClipBlock* vclip1 = new ClipBlock();
  vclip1->setParent(&project);
  vclip1->SetLabel(QStringLiteral("Shot 01 Video"));
  vclip1->set_in(rational(0, 1));
  vclip1->set_media_in(rational(0, 1));
  vclip1->set_length_and_media_out(rational(5, 1));
  vtrack->AppendBlock(vclip1);

  // Video Gap: [5s to 7s]
  GapBlock* vgap = new GapBlock();
  vgap->setParent(&project);
  vgap->set_in(rational(5, 1));
  vgap->set_length_and_media_out(rational(2, 1));
  vtrack->AppendBlock(vgap);

  // Video Clip 2: [7s to 11s], media_in = 1s
  ClipBlock* vclip2 = new ClipBlock();
  vclip2->setParent(&project);
  vclip2->SetLabel(QStringLiteral("Shot 02 Video"));
  vclip2->set_in(rational(7, 1));
  vclip2->set_media_in(rational(1, 1));
  vclip2->set_length_and_media_out(rational(4, 1));
  vtrack->AppendBlock(vclip2);

  // Audio Clip 1: [0s to 5s]
  ClipBlock* aclip1 = new ClipBlock();
  aclip1->setParent(&project);
  aclip1->SetLabel(QStringLiteral("Shot 01 Audio"));
  aclip1->set_in(rational(0, 1));
  aclip1->set_media_in(rational(0, 1));
  aclip1->set_length_and_media_out(rational(5, 1));
  atrack->AppendBlock(aclip1);

  // Establish bidirectional link between vclip1 and aclip1
  OLIVE_ASSERT(Node::Link(vclip1, aclip1));
  OLIVE_ASSERT(Node::AreLinked(vclip1, aclip1));
  OLIVE_ASSERT_EQUAL(vclip1->block_links().size(), 1);
  OLIVE_ASSERT(vclip1->block_links().contains(aclip1));

  // Add Sequence Markers
  MarkerAddCommand mcmd1(sequence->GetMarkers(), TimeRange(rational(2, 1), rational(2, 1)), QStringLiteral("Scene Start"), 1);
  mcmd1.redo_now();
  MarkerAddCommand mcmd2(sequence->GetMarkers(), TimeRange(rational(8, 1), rational(10, 1)), QStringLiteral("Action Beat"), 2);
  mcmd2.redo_now();
  OLIVE_ASSERT_EQUAL(sequence->GetMarkers()->size(), static_cast<size_t>(2));

  // Save to FCP7 XML
  SaveFCPXMLTask save_task(sequence, xml_file);
  OLIVE_ASSERT(save_task.Start());

  QFile check_xml(xml_file);
  OLIVE_ASSERT(check_xml.exists());
  OLIVE_ASSERT(check_xml.size() > 0);

  // Load from FCP7 XML
  LoadFCPXMLTask load_task(xml_file);
  OLIVE_ASSERT(load_task.Start());

  Project* loaded_project = load_task.GetLoadedProject();
  OLIVE_ASSERT(loaded_project != nullptr);

  QVector<Sequence*> loaded_seqs = loaded_project->root()->ListChildrenOfType<Sequence>();
  OLIVE_ASSERT_EQUAL(loaded_seqs.size(), 1);
  Sequence* loaded_seq = loaded_seqs.first();

  // Validate Sequence Parameters
  OLIVE_ASSERT_EQUAL(loaded_seq->GetVideoParams().frame_rate(), rational(24, 1));
  OLIVE_ASSERT_EQUAL(loaded_seq->GetVideoParams().width(), 1920);
  OLIVE_ASSERT_EQUAL(loaded_seq->GetVideoParams().height(), 1080);

  // Validate Tracks
  TrackList* loaded_vtracks = loaded_seq->track_list(Track::kVideo);
  TrackList* loaded_atracks = loaded_seq->track_list(Track::kAudio);
  OLIVE_ASSERT(loaded_vtracks != nullptr);
  OLIVE_ASSERT(loaded_atracks != nullptr);
  OLIVE_ASSERT_EQUAL(loaded_vtracks->GetTrackCount(), 1);
  OLIVE_ASSERT_EQUAL(loaded_atracks->GetTrackCount(), 1);

  Track* loaded_vtrack = loaded_vtracks->GetTrackAt(0);
  Track* loaded_atrack = loaded_atracks->GetTrackAt(0);

  // Validate Video Blocks
  OLIVE_ASSERT_EQUAL(loaded_vtrack->Blocks().size(), 3);
  ClipBlock* lvclip1 = dynamic_cast<ClipBlock*>(loaded_vtrack->Blocks().at(0));
  GapBlock* lvgap = dynamic_cast<GapBlock*>(loaded_vtrack->Blocks().at(1));
  ClipBlock* lvclip2 = dynamic_cast<ClipBlock*>(loaded_vtrack->Blocks().at(2));

  OLIVE_ASSERT(lvclip1 != nullptr);
  OLIVE_ASSERT(lvgap != nullptr);
  OLIVE_ASSERT(lvclip2 != nullptr);

  OLIVE_ASSERT_EQUAL(lvclip1->in(), rational(0, 1));
  OLIVE_ASSERT_EQUAL(lvclip1->length(), rational(5, 1));
  OLIVE_ASSERT_EQUAL(lvclip1->media_in(), rational(0, 1));

  OLIVE_ASSERT_EQUAL(lvgap->in(), rational(5, 1));
  OLIVE_ASSERT_EQUAL(lvgap->length(), rational(2, 1));

  OLIVE_ASSERT_EQUAL(lvclip2->in(), rational(7, 1));
  OLIVE_ASSERT_EQUAL(lvclip2->length(), rational(4, 1));
  OLIVE_ASSERT_EQUAL(lvclip2->media_in(), rational(1, 1));

  // Validate Audio Blocks & Link
  OLIVE_ASSERT_EQUAL(loaded_atrack->Blocks().size(), 1);
  ClipBlock* laclip1 = dynamic_cast<ClipBlock*>(loaded_atrack->Blocks().at(0));
  OLIVE_ASSERT(laclip1 != nullptr);
  OLIVE_ASSERT_EQUAL(laclip1->in(), rational(0, 1));
  OLIVE_ASSERT_EQUAL(laclip1->length(), rational(5, 1));

  // Assert Link Preservation
  OLIVE_ASSERT(Node::AreLinked(lvclip1, laclip1));
  OLIVE_ASSERT(lvclip1->block_links().contains(laclip1));

  // Validate Markers
  OLIVE_ASSERT_EQUAL(loaded_seq->GetMarkers()->size(), static_cast<size_t>(2));
  TimelineMarker* lm1 = loaded_seq->GetMarkers()->GetMarkerAtTime(rational(2, 1));
  OLIVE_ASSERT(lm1 != nullptr);
  OLIVE_ASSERT_EQUAL(lm1->name(), QStringLiteral("Scene Start"));

  TimelineMarker* lm2 = loaded_seq->GetMarkers()->GetMarkerAtTime(rational(8, 1));
  OLIVE_ASSERT(lm2 != nullptr);
  OLIVE_ASSERT_EQUAL(lm2->name(), QStringLiteral("Action Beat"));
  OLIVE_ASSERT_EQUAL(lm2->time().length(), rational(2, 1));

  delete loaded_project;
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(FCPXML_TransitionRoundTrip)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QString xml_file = dir.filePath(QStringLiteral("transition_test.xml"));

  Project project;
  project.Initialize();

  Sequence* sequence = new Sequence();
  sequence->setParent(&project);
  sequence->SetLabel(QStringLiteral("Transition Sequence"));

  // Use integer 24fps so that 0.5s = exactly 12 frames (no NTSC precision issues).
  VideoParams vparams;
  vparams.set_frame_rate(rational(24, 1));
  vparams.set_width(1920);
  vparams.set_height(1080);
  sequence->SetVideoParams(vparams);

  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();

  // Two abutting clips: [0s to 4s] and [4s to 8s]
  ClipBlock* clip1 = new ClipBlock();
  clip1->setParent(&project);
  clip1->set_in(rational(0, 1));
  clip1->set_length_and_media_out(rational(4, 1));
  vtrack->AppendBlock(clip1);

  // Transition centered on cut: 1s duration (0.5s in, 0.5s out)
  CrossDissolveTransition* trans = new CrossDissolveTransition();
  trans->setParent(&project);
  trans->set_offsets_and_length(rational(1, 2), rational(1, 2));
  vtrack->AppendBlock(trans);

  Node::ConnectEdge(clip1, NodeInput(trans, TransitionBlock::kOutBlockInput));

  ClipBlock* clip2 = new ClipBlock();
  clip2->setParent(&project);
  clip2->set_in(rational(4, 1));
  clip2->set_length_and_media_out(rational(4, 1));
  vtrack->AppendBlock(clip2);

  Node::ConnectEdge(clip2, NodeInput(trans, TransitionBlock::kInBlockInput));

  // Save and reload
  SaveFCPXMLTask save_task(sequence, xml_file);
  OLIVE_ASSERT(save_task.Start());

  LoadFCPXMLTask load_task(xml_file);
  OLIVE_ASSERT(load_task.Start());

  Project* loaded_project = load_task.GetLoadedProject();
  OLIVE_ASSERT(loaded_project != nullptr);

  Sequence* loaded_seq = loaded_project->root()->ListChildrenOfType<Sequence>().first();
  Track* loaded_vtrack = loaded_seq->track_list(Track::kVideo)->GetTrackAt(0);

  // Transition must be preserved between the two clips
  OLIVE_ASSERT_EQUAL(loaded_vtrack->Blocks().size(), 3);
  TransitionBlock* loaded_trans = dynamic_cast<TransitionBlock*>(loaded_vtrack->Blocks().at(1));
  OLIVE_ASSERT(loaded_trans != nullptr);
  OLIVE_ASSERT_EQUAL(loaded_trans->in_offset(), rational(1, 2));
  OLIVE_ASSERT_EQUAL(loaded_trans->out_offset(), rational(1, 2));

  delete loaded_project;
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(FCPXML_FrameRateMapping)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const struct {
    rational rate;
    int expected_timebase;
    bool expected_ntsc;
  } kTestRates[] = {
    {rational(24000, 1001), 24, true},
    {rational(24, 1),       24, false},
    {rational(25, 1),       25, false},
    {rational(30000, 1001), 30, true},
    {rational(30, 1),       30, false},
    {rational(50, 1),       50, false},
    {rational(60000, 1001), 60, true},
    {rational(60, 1),       60, false},
  };

  for (size_t i = 0; i < sizeof(kTestRates)/sizeof(kTestRates[0]); ++i) {
    const QString xml_file = dir.filePath(QStringLiteral("rate_test_%1.xml").arg(i));

    Project project;
    project.Initialize();

    Sequence* seq = new Sequence();
    seq->setParent(&project);
    seq->SetLabel(QStringLiteral("Rate Test"));
    VideoParams vp;
    vp.set_frame_rate(kTestRates[i].rate);
    seq->SetVideoParams(vp);

    SaveFCPXMLTask save_task(seq, xml_file);
    OLIVE_ASSERT(save_task.Start());

    LoadFCPXMLTask load_task(xml_file);
    OLIVE_ASSERT(load_task.Start());

    Project* loaded_project = load_task.GetLoadedProject();
    OLIVE_ASSERT(loaded_project != nullptr);

    Sequence* loaded_seq = loaded_project->root()->ListChildrenOfType<Sequence>().first();
    OLIVE_ASSERT_EQUAL(loaded_seq->GetVideoParams().frame_rate(), kTestRates[i].rate);

    delete loaded_project;
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(FCPXML_MalformedXMLErrorHandling)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QVector<QByteArray> malformed_samples = {
    "",                                                         // Empty file
    "This is random junk text, not XML",                        // Non-XML
    "<otherroot><sequence/></otherroot>",                       // Missing <xmeml>
    "<xmeml version=\"5\"></xmeml>",                           // Missing <sequence>
    "<xmeml version=\"5\"><sequence><name>Incomplete...",       // Truncated XML
    "<xmeml version=\"5\"><sequence><rate><timebase>invalid</timebase></rate></sequence></xmeml>" // Bad data
  };

  for (int i = 0; i < malformed_samples.size(); ++i) {
    const QString bad_file = dir.filePath(QStringLiteral("malformed_%1.xml").arg(i));
    OLIVE_ASSERT(WriteFileBytes(bad_file, malformed_samples.at(i)));

    LoadFCPXMLTask load_task(bad_file);
    bool success = load_task.Start();
    // Must gracefully fail without crashing or leaking memory
    OLIVE_ASSERT(!success);
    OLIVE_ASSERT(!load_task.GetError().isEmpty());
    delete load_task.GetLoadedProject();
  }

  OLIVE_TEST_END;
}

} // namespace olive
