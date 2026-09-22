#include "testutil.h"

#include <QList>
#include <QVector>

#include "core.h"
#include "node/block/gap/gap.h"
#include "node/block/clip/clip.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundosplit.h"
#include "undo/undostack.h"

namespace olive {

#define TIMELINE_TEST_START \
  ColorManager::SetUpDefaultConfig(); \
  Project project; \
  Sequence sequence; \
  sequence.setParent(&project)

OLIVE_ADD_TEST(MultiPointCutExecution_SingleVideoTrack)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  TrackList* v_list = sequence.track_list(Track::kVideo);
  Track* v_track = v_list->GetTracks().first();

  ClipBlock* clip = new ClipBlock();
  clip->set_length_and_media_out(rational(100)); // Length = 100 sec
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(clip->in(), rational(0));
  OLIVE_ASSERT_EQUAL(clip->out(), rational(100));

  // Execute 3 cuts: t = 20, t = 45, t = 80
  QList<rational> cut_times = { rational(20), rational(45), rational(80) };
  BlockSplitPreservingLinksCommand split_cmd({ clip }, cut_times);
  split_cmd.redo_now();

  // Verify 4 resulting blocks with exact expected coordinates
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->in(), rational(0));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->out(), rational(20));

  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->in(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->out(), rational(45));

  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->in(), rational(45));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(35));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->out(), rational(80));

  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->in(), rational(80));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->length(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->out(), rational(100));

  // Test Undo: reverts cleanly to 1 block of length 100
  split_cmd.undo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().first(), clip);
  OLIVE_ASSERT_EQUAL(clip->length(), rational(100));

  // Test Redo: reapplies 4 blocks cleanly
  split_cmd.redo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(MultiPointCutExecution_VideoAndAudioLinked)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  Track* v_track = sequence.track_list(Track::kVideo)->GetTracks().first();
  Track* a_track = sequence.track_list(Track::kAudio)->GetTracks().first();

  ClipBlock* v_clip = new ClipBlock();
  v_clip->set_length_and_media_out(rational(60));
  v_clip->setParent(&project);
  v_track->AppendBlock(v_clip);

  ClipBlock* a_clip = new ClipBlock();
  a_clip->set_length_and_media_out(rational(60));
  a_clip->setParent(&project);
  a_track->AppendBlock(a_clip);

  // Explicitly link video and audio clip
  Node::Link(v_clip, a_clip);
  OLIVE_ASSERT(Node::AreLinked(v_clip, a_clip));

  // Cut at t = 15 and t = 40
  QList<rational> cut_times = { rational(15), rational(40) };
  QVector<Block*> blocks_to_split = { v_clip, a_clip };

  BlockSplitPreservingLinksCommand split_cmd(blocks_to_split, cut_times);
  split_cmd.redo_now();

  // Both tracks must have 3 blocks
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 3);
  OLIVE_ASSERT_EQUAL(a_track->Blocks().size(), 3);

  // Check segment lengths
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(15));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(20));

  OLIVE_ASSERT_EQUAL(a_track->Blocks().at(0)->length(), rational(15));
  OLIVE_ASSERT_EQUAL(a_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(a_track->Blocks().at(2)->length(), rational(20));

  // Verify link preservation across each split slice
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(0), a_track->Blocks().at(0)));
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(1), a_track->Blocks().at(1)));
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(2), a_track->Blocks().at(2)));

  // Test Undo: restores single linked pair
  split_cmd.undo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(a_track->Blocks().size(), 1);
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().first(), a_track->Blocks().first()));

  // Test Redo: restores 3 linked pairs
  split_cmd.redo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 3);
  OLIVE_ASSERT_EQUAL(a_track->Blocks().size(), 3);
  OLIVE_ASSERT(Node::AreLinked(v_track->Blocks().at(1), a_track->Blocks().at(1)));

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(MultiPointCut_BoundaryAndOutOfRangeCuts)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  Track* v_track = sequence.track_list(Track::kVideo)->GetTracks().first();

  GapBlock* gap = new GapBlock();
  gap->set_length_and_media_out(rational(10));
  gap->setParent(&project);
  v_track->AppendBlock(gap);

  ClipBlock* clip = new ClipBlock();
  clip->set_length_and_media_out(rational(40));
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  OLIVE_ASSERT_EQUAL(clip->in(), rational(10));
  OLIVE_ASSERT_EQUAL(clip->out(), rational(50));

  // Cut points: 5 (before in), 10 (at in), 30 (inside), 50 (at out), 70 (after out)
  QList<rational> raw_cuts = { rational(5), rational(10), rational(30), rational(50), rational(70) };

  // Filter cuts according to boundary rule
  QList<rational> valid_cuts;
  for (const rational& t : raw_cuts) {
    if (t > clip->in() && t < clip->out()) {
      valid_cuts.append(t);
    }
  }

  // Exactly one cut is valid (t = 30)
  OLIVE_ASSERT_EQUAL(valid_cuts.size(), 1);
  OLIVE_ASSERT_EQUAL(valid_cuts.first(), rational(30));

  BlockSplitPreservingLinksCommand split_cmd({ clip }, valid_cuts);
  split_cmd.redo_now();

  // 1 GapBlock + 2 ClipBlocks = 3 blocks
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 3);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->in(), rational(10));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->out(), rational(30));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->in(), rational(30));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->out(), rational(50));

  OLIVE_TEST_END;
}

static rational MapClipMediaToSequenceTime(const ClipBlock* clip, const rational& media_time)
{
  if (!clip || media_time == RATIONAL_MIN || media_time == RATIONAL_MAX) {
    return media_time;
  }

  rational sequence_time = media_time - clip->media_in();

  double speed_val = clip->speed();
  if (qIsNull(speed_val)) {
    return rational::NaN;
  } else if (!qFuzzyCompare(speed_val, 1.0)) {
    sequence_time = rational::fromDouble(sequence_time.toDouble() / speed_val);
  }

  if (clip->reverse()) {
    sequence_time = clip->length() - sequence_time;
  }

  return sequence_time;
}

OLIVE_ADD_TEST(TimeConversion_MediaToSequence_SpeedAndReverse)
{
  Project project;
  ClipBlock clip;
  clip.setParent(&project);

  clip.set_in(rational(100));
  clip.set_length_and_media_out(rational(50));
  clip.set_out(rational(150)); // Timeline [100, 150], length = 50
  clip.set_media_in(rational(20));                   // Media offset = 20

  // 1. Standard Playback (speed = 1.0, reverse = false)
  // Media time = 35 -> rel_seq = 35 - 20 = 15 -> abs_seq = 100 + 15 = 115
  {
    rational media_t = rational(35);
    rational rel_seq = MapClipMediaToSequenceTime(&clip, media_t);
    rational abs_seq = clip.in() + rel_seq;
    OLIVE_ASSERT_EQUAL(rel_seq, rational(15));
    OLIVE_ASSERT_EQUAL(abs_seq, rational(115));
  }

  // 2. 2x Speed (speed = 2.0, reverse = false)
  // Media time = 40 -> rel_seq = (40 - 20) / 2.0 = 10 -> abs_seq = 100 + 10 = 110
  {
    clip.SetStandardValue(ClipBlock::kSpeedInput, 2.0);
    rational media_t = rational(40);
    rational rel_seq = MapClipMediaToSequenceTime(&clip, media_t);
    rational abs_seq = clip.in() + rel_seq;
    OLIVE_ASSERT_EQUAL(rel_seq, rational(10));
    OLIVE_ASSERT_EQUAL(abs_seq, rational(110));
  }

  // 3. Reverse Playback (speed = 1.0, reverse = true)
  // Media time = 30 -> unreversed_rel = 30 - 20 = 10 -> reversed_rel = length(50) - 10 = 40
  // abs_seq = 100 + 40 = 140
  {
    clip.SetStandardValue(ClipBlock::kSpeedInput, 1.0);
    clip.SetStandardValue(ClipBlock::kReverseInput, true);
    rational media_t = rational(30);
    rational rel_seq = MapClipMediaToSequenceTime(&clip, media_t);
    rational abs_seq = clip.in() + rel_seq;
    OLIVE_ASSERT_EQUAL(rel_seq, rational(40));
    OLIVE_ASSERT_EQUAL(abs_seq, rational(140));
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(UnsortedCutsRobustness)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  Track* v_track = sequence.track_list(Track::kVideo)->GetTracks().first();

  ClipBlock* clip = new ClipBlock();
  clip->set_length_and_media_out(rational(100));
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  // Provide cuts in reverse/jumbled order: 75, 25, 50
  QList<rational> jumbled_cuts = { rational(75), rational(25), rational(50) };
  BlockSplitPreservingLinksCommand split_cmd({ clip }, jumbled_cuts);
  split_cmd.redo_now();

  // Must automatically sort and partition correctly into [0, 25], [25, 50], [50, 75], [75, 100]
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->length(), rational(25));

  OLIVE_TEST_END;
}

} // namespace olive
