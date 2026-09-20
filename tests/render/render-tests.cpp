#include "render/renderjobtracker.h"
#include "testutil.h"
namespace olive {
OLIVE_ADD_TEST(StaleRenderCannotOverwriteNewerEdit)
{
  RenderJobTracker tracker;
  JobTime first;
  JobTime second;
  OLIVE_ASSERT(second >= first);
  tracker.insert(TimeRange(0, 10), first);
  tracker.insert(TimeRange(3, 7), second);
  OLIVE_ASSERT(tracker.isCurrent(1, first));
  OLIVE_ASSERT(!tracker.isCurrent(5, first));
  OLIVE_ASSERT(tracker.isCurrent(5, second));
  OLIVE_ASSERT(!tracker.isCurrent(12, second));
  const auto ranges = tracker.getCurrentSubRanges(TimeRange(0, 10), first);
  OLIVE_ASSERT_EQUAL(ranges.size(), size_t(2));
  OLIVE_ASSERT(ranges.contains(TimeRange(0, 3)));
  OLIVE_ASSERT(ranges.contains(TimeRange(7, 10)));
  tracker.clear();
  OLIVE_ASSERT(!tracker.isCurrent(1, second));
  OLIVE_TEST_END;
}
}
