// Real FFmpeg scene analysis and production split/undo commands.
#include "e2e_fixtures.h"
#include "node/nodeundo.h"
#include "task/scenecut/scenecuttask.h"

namespace olive {
OLIVE_ADD_TEST(SceneCutTaskDecodesKnownCutsAndEmitsResults)
{
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  const QString media = dir.filePath("cuts.mp4");
  OLIVE_ASSERT(e2e::GenerateSyntheticVideo(media, {{"black", 1}, {"white", 1}, {"black", 1}}));
  SceneCutTask task(media, 0, rational(0), rational(3), rational(25));
  QVector<rational> emitted;
  int signal_count = 0;
  QObject::connect(&task, &SceneCutTask::SceneCutsDetected, [&](const QVector<rational> &cuts) {
    emitted = cuts;
    ++signal_count;
  });
  OLIVE_ASSERT(task.Start());
  OLIVE_ASSERT_EQUAL(signal_count, 1);
  OLIVE_ASSERT_EQUAL(task.detected_cuts().size(), 2);
  OLIVE_ASSERT_EQUAL(task.detected_cuts().at(0), rational(1));
  OLIVE_ASSERT_EQUAL(task.detected_cuts().at(1), rational(2));
  OLIVE_ASSERT(emitted == task.detected_cuts());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutTaskStaticVideoAndCancellation)
{
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  const QString media = dir.filePath("static.mp4");
  OLIVE_ASSERT(e2e::GenerateSyntheticVideo(media, {{"black", 2}}));
  SceneCutTask task(media, 0, rational(0), rational(2), rational(25));
  OLIVE_ASSERT(task.Start());
  OLIVE_ASSERT(task.detected_cuts().isEmpty());
  SceneCutTask cancelled(media, 0, rational(0), rational(2), rational(25));
  int signal_count = 0;
  QObject::connect(&cancelled, &SceneCutTask::SceneCutsDetected, [&](const QVector<rational>&) { ++signal_count; });
  cancelled.Cancel();
  OLIVE_ASSERT(!cancelled.Start());
  OLIVE_ASSERT_EQUAL(signal_count, 0);
  OLIVE_ASSERT(cancelled.detected_cuts().isEmpty());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutTaskRejectsCorruptMedia)
{
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());
  const QString media = dir.filePath("corrupt.mp4");
  QFile file(media);
  OLIVE_ASSERT(file.open(QIODevice::WriteOnly));
  OLIVE_ASSERT(file.write("not a video") > 0);
  file.close();
  SceneCutTask task(media, 0, rational(0), rational(2), rational(25));
  OLIVE_ASSERT(!task.Start());
  OLIVE_ASSERT(!task.GetError().isEmpty());
  OLIVE_ASSERT(task.detected_cuts().isEmpty());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_06_01_TimelineAutoSplitMultiPoint)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  OLIVE_ASSERT(track->Blocks().size() == 1);

  // Split at t = 3 and t = 7
  QVector<Block*> blocks_to_split = {clip};
  QList<rational> split_times = {rational(3, 1), rational(7, 1)};

  BlockSplitPreservingLinksCommand split_cmd(blocks_to_split, split_times);
  split_cmd.redo_now();

  OLIVE_ASSERT(track->Blocks().size() == 3);
  OLIVE_ASSERT(track->Blocks().at(0)->length() == rational(3, 1));
  OLIVE_ASSERT(track->Blocks().at(1)->length() == rational(4, 1));
  OLIVE_ASSERT(track->Blocks().at(2)->length() == rational(3, 1));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_06_02_TimelineAutoSplitLinkedAV)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* v_track = ctx.video_track;
  Track* a_track = ctx.audio_track;

  ClipBlock* clip_v = new ClipBlock();
  clip_v->setParent(ctx.project.get());
  clip_v->set_length_and_media_out(rational(10, 1));
  v_track->AppendBlock(clip_v);

  ClipBlock* clip_a = new ClipBlock();
  clip_a->setParent(ctx.project.get());
  clip_a->set_length_and_media_out(rational(10, 1));
  a_track->AppendBlock(clip_a);

  NodeLinkCommand link_cmd(clip_v, clip_a, true);
  link_cmd.redo_now();
  OLIVE_ASSERT(Block::AreLinked(clip_v, clip_a));

  // Auto split both at t = 4
  QVector<Block*> blocks = {clip_v, clip_a};
  QList<rational> times = {rational(4, 1)};

  BlockSplitPreservingLinksCommand split_cmd(blocks, times);
  split_cmd.redo_now();

  OLIVE_ASSERT(v_track->Blocks().size() == 2);
  OLIVE_ASSERT(a_track->Blocks().size() == 2);

  // New blocks must remain linked
  Block* new_v = v_track->Blocks().at(1);
  Block* new_a = a_track->Blocks().at(1);
  OLIVE_ASSERT(Block::AreLinked(new_v, new_a));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_06_03_TimelineAutoSplitUndoFidelity)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(5, 1)});
  split_cmd.redo_now();
  OLIVE_ASSERT(track->Blocks().size() == 2);

  split_cmd.undo_now();
  // Reverts to single unbroken block
  OLIVE_ASSERT(track->Blocks().size() == 1);
  OLIVE_ASSERT(track->Blocks().first() == clip);
  OLIVE_ASSERT(track->Blocks().first()->length() == rational(10, 1));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_06_04_TimelineAutoSplitRedoFidelity)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(5, 1)});
  split_cmd.redo_now();
  split_cmd.undo_now();
  split_cmd.redo_now();

  OLIVE_ASSERT(track->Blocks().size() == 2);
  OLIVE_ASSERT(track->Blocks().at(0)->length() == rational(5, 1));
  OLIVE_ASSERT(track->Blocks().at(1)->length() == rational(5, 1));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_06_05_TimelineAutoSplitMediaInOutConsistency)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_media_in(rational(5, 1));
  clip->set_length_and_media_out(rational(10, 1)); // media range [5, 15]
  track->AppendBlock(clip);

  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(3, 1)});
  split_cmd.redo_now();

  Block* b0 = track->Blocks().at(0);
  Block* b1 = track->Blocks().at(1);
  ClipBlock* cb0 = static_cast<ClipBlock*>(b0);
  ClipBlock* cb1 = static_cast<ClipBlock*>(b1);
  OLIVE_ASSERT(cb0->media_in() == rational(5, 1));
  OLIVE_ASSERT(cb0->media_in() + cb0->length() == rational(8, 1));
  OLIVE_ASSERT(cb1->media_in() == rational(8, 1));
  OLIVE_ASSERT(cb1->media_in() + cb1->length() == rational(15, 1));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_06_01_TimelineAutoSplitExactInPoint)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  // Split at t = 0 (exact in point)
  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(0, 1)});
  split_cmd.redo_now();

  // Invariant: BlockSplitCommand rejects points not strictly inside (in < time < out)
  OLIVE_ASSERT(track->Blocks().size() == 1);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_06_02_TimelineAutoSplitExactOutPoint)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  // Split at t = 10 (exact out point)
  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(10, 1)});
  split_cmd.redo_now();

  OLIVE_ASSERT(track->Blocks().size() == 1);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_06_03_TimelineAutoSplitOutsideRange)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  // Split at t = -5 and t = 15
  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(-5, 1), rational(15, 1)});
  split_cmd.redo_now();

  OLIVE_ASSERT(track->Blocks().size() == 1);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(TimelineSplitWithNormalizedCutPoints)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  // Unsorted and duplicate times: {7, 2, 2, 5}
  QList<rational> raw_times = {rational(7, 1), rational(2, 1), rational(2, 1), rational(5, 1)};
  // Deduplicate and sort
  std::sort(raw_times.begin(), raw_times.end());
  auto last = std::unique(raw_times.begin(), raw_times.end());
  raw_times.erase(last, raw_times.end());

  BlockSplitPreservingLinksCommand split_cmd({clip}, raw_times);
  split_cmd.redo_now();

  // Resulting splits at t=2, t=5, t=7 produces 4 blocks
  OLIVE_ASSERT(track->Blocks().size() == 4);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_06_05_TimelineAutoSplitFractionalRational)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* track = ctx.video_track;

  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  track->AppendBlock(clip);

  // Fractional rational timestamp (e.g. 1001/30000 s)
  rational cut_point(1001, 300); // 3.3366... s
  BlockSplitPreservingLinksCommand split_cmd({clip}, {cut_point});
  split_cmd.redo_now();

  OLIVE_ASSERT(track->Blocks().size() == 2);
  OLIVE_ASSERT(track->Blocks().at(0)->length() == cut_point);
  OLIVE_TEST_END;
}


} // namespace olive
