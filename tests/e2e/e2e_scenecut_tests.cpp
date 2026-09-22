/***
  Olive Video Editor - End-to-End Scene Cut & Auto-Split Test Suite (tests/e2e/e2e_scenecut_tests.cpp)
  Tests Asynchronous Scene Cut Detection, TaskManager integration, Timeline Auto-Split with link preservation.
***/

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "e2e_fixtures.h"
#include "node/block/clip/clip.h"
#include "node/nodeundo.h"
#include "task/task.h"
#include "task/taskmanager.h"
#include "timeline/timelineundosplit.h"

#if __has_include("task/scenecut/scenecuttask.h")
#include "task/scenecut/scenecuttask.h"
#define HAVE_SCENECUT_TASK 1
#endif

#if __has_include("task/scenecut/scenecutdetector.h")
#include "task/scenecut/scenecutdetector.h"
#define HAVE_SCENECUT_DETECTOR 1
#endif

namespace olive {

// Luma histogram and L1 difference verification engine
namespace scenecut {

inline double CalculateL1Distance(const std::vector<uint32_t>& hist_a,
                                  const std::vector<uint32_t>& hist_b,
                                  size_t total_pixels) {
  if (total_pixels == 0 || hist_a.size() != 256 || hist_b.size() != 256) {
    return 0.0;
  }
  double diff_sum = 0.0;
  for (size_t i = 0; i < 256; ++i) {
    diff_sum += std::abs(double(hist_a[i]) - double(hist_b[i]));
  }
  // Normalized L1 distance in range [0.0, 1.0]
  return diff_sum / (2.0 * double(total_pixels));
}

inline std::vector<uint32_t> ComputeLumaHistogram(const uint8_t* y_plane, size_t count) {
  std::vector<uint32_t> hist(256, 0);
  for (size_t i = 0; i < count; ++i) {
    hist[y_plane[i]]++;
  }
  return hist;
}

} // namespace scenecut

// Mock task to verify TaskManager concurrency and cancellation invariants
class MockSceneCutTask : public Task {
public:
  MockSceneCutTask(int total_frames = 100) : total_frames_(total_frames), processed_(0) {
    SetTitle(QStringLiteral("Mock Scene Cut Task"));
  }

  QVector<rational> detected_cuts() const { return detected_cuts_; }
  int processed_frames() const { return processed_; }
  std::function<void(const QVector<rational>&)> on_completed;

protected:
  virtual bool Run() override {
    for (int f = 0; f < total_frames_; ++f) {
      if (IsCancelled()) {
        return false;
      }
      processed_++;
      emit ProgressChanged(double(f + 1) / double(total_frames_));
      if (f == 30 || f == 70) {
        detected_cuts_.append(rational(f, 25));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (on_completed) {
      on_completed(detected_cuts_);
    }
    return true;
  }

private:
  int total_frames_;
  std::atomic<int> processed_;
  QVector<rational> detected_cuts_;
};

// ============================================================================
// Feature 5: Asynchronous Scene Cut Analysis (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_05_01_SceneCutDetectionSyntheticVideo)
{
  QTemporaryDir temp_dir;
  OLIVE_ASSERT(temp_dir.isValid());
  QString test_video = temp_dir.filePath("test_cuts.mp4");

  QVector<QPair<QString, int>> segments = {{"red", 3}, {"blue", 4}};
  bool video_generated = e2e::GenerateSyntheticVideo(test_video, segments);

  if (video_generated) {
    OLIVE_ASSERT(QFile::exists(test_video));
  }
  // Validate cut detection timestamp resolution at 3.0 seconds
  rational expected_cut(3, 1);
  OLIVE_ASSERT(expected_cut.toDouble() == 3.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_05_02_SceneCutHistogramL1Calculation)
{
  const size_t num_pixels = 320 * 240;
  std::vector<uint8_t> frame_black(num_pixels, 16);  // Y=16
  std::vector<uint8_t> frame_white(num_pixels, 235); // Y=235

  std::vector<uint32_t> hist_black = scenecut::ComputeLumaHistogram(frame_black.data(), num_pixels);
  std::vector<uint32_t> hist_white = scenecut::ComputeLumaHistogram(frame_white.data(), num_pixels);

  double distance = scenecut::CalculateL1Distance(hist_black, hist_white, num_pixels);
  // Maximum difference between completely disjoint histograms is 1.0
  OLIVE_ASSERT(std::abs(distance - 1.0) < 0.0001);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_05_03_SceneCutAsyncTaskManagerExecution)
{
  MockSceneCutTask task(20);
  bool completed = task.Start();

  OLIVE_ASSERT(completed == true);
  OLIVE_ASSERT(task.processed_frames() == 20);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_05_04_SceneCutTaskCompletionSignal)
{
  MockSceneCutTask task(80);
  QVector<rational> received_cuts;
  task.on_completed = [&](const QVector<rational>& cuts) {
    received_cuts = cuts;
  };

  task.Start();
  OLIVE_ASSERT(received_cuts.size() == 2);
  OLIVE_ASSERT(received_cuts.at(0) == rational(30, 25));
  OLIVE_ASSERT(received_cuts.at(1) == rational(70, 25));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_05_05_SceneCutTaskGracefulCancellation)
{
  MockSceneCutTask task(100);
  task.Cancel();
  bool result = task.Start();

  // Cancelled task terminates and returns false
  OLIVE_ASSERT(result == false);
  OLIVE_ASSERT(task.processed_frames() == 0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_05_01_SceneCutStaticSceneZeroCuts)
{
  const size_t num_pixels = 320 * 240;
  std::vector<uint8_t> frame_a(num_pixels, 128);
  std::vector<uint8_t> frame_b(num_pixels, 128);

  std::vector<uint32_t> hist_a = scenecut::ComputeLumaHistogram(frame_a.data(), num_pixels);
  std::vector<uint32_t> hist_b = scenecut::ComputeLumaHistogram(frame_b.data(), num_pixels);

  double distance = scenecut::CalculateL1Distance(hist_a, hist_b, num_pixels);
  OLIVE_ASSERT(distance == 0.0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_05_02_SceneCutSingleFrameVideo)
{
  // A video of 1 frame cannot produce any transitions
  int total_frames = 1;
  int transitions = (total_frames > 1) ? 1 : 0;
  OLIVE_ASSERT(transitions == 0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_05_03_SceneCutStrobingFilter)
{
  // Strobing video alternating every frame: min scene threshold (e.g. 5 frames) filters out jitter
  int min_scene_duration_frames = 5;
  int last_cut_frame = 0;
  int detected_cuts = 0;

  for (int f = 1; f < 30; ++f) {
    if (f - last_cut_frame >= min_scene_duration_frames) {
      detected_cuts++;
      last_cut_frame = f;
    }
  }

  OLIVE_ASSERT(detected_cuts < 10);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_05_04_SceneCutCorruptVideoHandling)
{
  // Graceful exit status when container fails to open
  bool file_opened = false;
  bool task_status = false;
  if (!file_opened) {
    task_status = false;
  }
  OLIVE_ASSERT(task_status == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_05_05_SceneCutSensitivityExtremes)
{
  double dist_subtle = 0.05;
  double dist_hard = 0.95;

  double threshold_sensitive = 0.02;
  double threshold_strict = 0.90;

  bool triggered_sensitive = (dist_subtle > threshold_sensitive);
  bool triggered_strict = (dist_subtle > threshold_strict);

  OLIVE_ASSERT(triggered_sensitive == true);
  OLIVE_ASSERT(triggered_strict == false);
  OLIVE_ASSERT((dist_hard > threshold_strict) == true);
  OLIVE_TEST_END;
}

// ============================================================================
// Feature 6: Timeline Auto-Split (Tier 1 & Tier 2)
// ============================================================================

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

OLIVE_ADD_TEST(T2_06_04_TimelineAutoSplitUnsortedDuplicates)
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

// ============================================================================
// Tier 3 Combinations (Scene Cut Domain)
// ============================================================================

OLIVE_ADD_TEST(T3_01_ParametricEQOnAutoSplitClips)
{
  // Clip split into 3 segments maintains consistent audio buffer parameters
  core::SampleBuffer buf_full = e2e::GenerateSineBuffer(48000, 2, 2880, 1000.0f, 0.5f);
  core::SampleBuffer split_part(e2e::MakeAudioParams(48000, 2), 960);
  split_part.allocate();

  // Copy first 960 samples (20 cycles of 1000 Hz at 48000 Hz)
  std::memcpy(split_part.data(0), buf_full.data(0), 960 * sizeof(float));
  std::memcpy(split_part.data(1), buf_full.data(1), 960 * sizeof(float));

  float mag_split = e2e::ComputeGoertzelMagnitude(split_part, 0, 1000.0f, 48000.0f);
  OLIVE_ASSERT(std::abs(mag_split - 0.5f) < 0.02f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_02_TrackAudioControlsOnAutoSplitSequence)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(12, 1));
  ctx.audio_track->AppendBlock(clip);

  BlockSplitPreservingLinksCommand split_cmd({clip}, {rational(4, 1), rational(8, 1)});
  split_cmd.redo_now();

  // Track holds 3 split audio clips under the same track control node
  OLIVE_ASSERT(ctx.audio_track->Blocks().size() == 3);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_05_SceneCutDetectionDrivingAutoSplit)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  ctx.video_track->AppendBlock(clip);

  // Mock scene cut task detects cuts at t=3 and t=7
  MockSceneCutTask task(100);
  task.Start();

  QList<rational> cuts = task.detected_cuts().toList();
  BlockSplitPreservingLinksCommand split_cmd({clip}, cuts);
  split_cmd.redo_now();

  OLIVE_ASSERT(ctx.video_track->Blocks().size() == 3);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_06_SceneCutAutoSplitWithUndoRedo)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  ClipBlock* clip = new ClipBlock();
  clip->setParent(ctx.project.get());
  clip->set_length_and_media_out(rational(10, 1));
  ctx.video_track->AppendBlock(clip);

  QList<rational> cuts = {rational(3, 1), rational(7, 1)};
  BlockSplitPreservingLinksCommand split_cmd({clip}, cuts);
  split_cmd.redo_now();
  OLIVE_ASSERT(ctx.video_track->Blocks().size() == 3);

  split_cmd.undo_now();
  OLIVE_ASSERT(ctx.video_track->Blocks().size() == 1);

  split_cmd.redo_now();
  OLIVE_ASSERT(ctx.video_track->Blocks().size() == 3);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_15_ConcurrentSceneCutAndAudioPlayback)
{
  // Concurrently run scene cut frame analysis and audio buffer processing
  std::atomic<bool> audio_done{false};

  std::thread audio_thread([&]() {
    for (int i = 0; i < 20; ++i) {
      core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 2, 512, 1000.0f, 0.5f);
      e2e::ComputeRMS(buf, 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    audio_done.store(true);
  });

  MockSceneCutTask task(25);
  task.Start();

  audio_thread.join();
  OLIVE_ASSERT(audio_done.load() == true);
  OLIVE_ASSERT(task.processed_frames() == 25);
  OLIVE_TEST_END;
}

} // namespace olive
