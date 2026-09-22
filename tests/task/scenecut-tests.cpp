#include "testutil.h"

#include <vector>
#include <cstdint>

#include "task/scenecut/scenecutdetector.h"

namespace olive {

static void FillSyntheticYUV420P(std::vector<uint8_t>& y_plane,
                                 std::vector<uint8_t>& u_plane,
                                 std::vector<uint8_t>& v_plane,
                                 int width, int height,
                                 uint8_t y_val, uint8_t u_val, uint8_t v_val)
{
  y_plane.assign(width * height, y_val);
  u_plane.assign((width / 2) * (height / 2), u_val);
  v_plane.assign((width / 2) * (height / 2), v_val);
}

OLIVE_ADD_TEST(SceneCutDetector_IdenticalFrames)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> y, u, v;
  FillSyntheticYUV420P(y, u, v, W, H, 16, 128, 128); // TV black

  const uint8_t* data[4] = { y.data(), u.data(), v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i < 25; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_HardCutBlackToWhite)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> white_y, white_u, white_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(white_y, white_u, white_v, W, H, 235, 128, 128);

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* white_data[4] = { white_y.data(), white_u.data(), white_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Frames 0-4: Black
  for (int i = 0; i <= 4; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  // Frame 5: White (Candidate, queued in lookahead)
  SceneCutResult res5 = detector.ProcessPlanar(5, white_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res5.cut_detected);

  // Frame 6: White continues -> Genuine cut confirmed at frame 5!
  SceneCutResult res6 = detector.ProcessPlanar(6, white_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(res6.cut_detected);
  OLIVE_ASSERT_EQUAL(res6.cut_frame_index, 5);
  OLIVE_ASSERT(res6.score > 0.65);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_ChromaShiftDisambiguation)
{
  SceneCutConfig config;
  config.base_threshold = 0.20;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = false;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> red_y, red_u, red_v;
  std::vector<uint8_t> blue_y, blue_u, blue_v;
  FillSyntheticYUV420P(red_y, red_u, red_v, W, H, 128, 90, 240);
  FillSyntheticYUV420P(blue_y, blue_u, blue_v, W, H, 128, 240, 90);

  const uint8_t* red_data[4] = { red_y.data(), red_u.data(), red_v.data(), nullptr };
  const uint8_t* blue_data[4] = { blue_y.data(), blue_u.data(), blue_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i < 5; ++i) {
    detector.ProcessPlanar(i, red_data, linesize, W, H, PixelFormatType::YUV420P);
  }

  SceneCutResult cut = detector.ProcessPlanar(5, blue_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(cut.cut_detected);
  OLIVE_ASSERT_EQUAL(cut.cut_frame_index, 5);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_StrobeFlashRejection)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> flash_y, flash_u, flash_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(flash_y, flash_u, flash_v, W, H, 255, 128, 128); // 1-frame strobe flash

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* flash_data[4] = { flash_y.data(), flash_u.data(), flash_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Frames 0-4: Black
  for (int i = 0; i <= 4; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  // Frame 5: Flash (1 frame)
  SceneCutResult res5 = detector.ProcessPlanar(5, flash_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res5.cut_detected);

  // Frame 6: Returns to Black -> Flash MUST be suppressed!
  SceneCutResult res6 = detector.ProcessPlanar(6, black_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res6.cut_detected);

  // Frames 7-10: Black continues -> Still no cuts!
  for (int i = 7; i <= 10; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_FlashFollowedByGenuineCut)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> flash_y, flash_u, flash_v;
  std::vector<uint8_t> gray_y, gray_u, gray_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(flash_y, flash_u, flash_v, W, H, 255, 128, 128);
  FillSyntheticYUV420P(gray_y, gray_u, gray_v, W, H, 180, 128, 128);

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* flash_data[4] = { flash_y.data(), flash_u.data(), flash_v.data(), nullptr };
  const uint8_t* gray_data[4] = { gray_y.data(), gray_u.data(), gray_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  // Frames 0-4: Black
  for (int i = 0; i <= 4; ++i) {
    detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
  }

  // Frame 5: Flash
  detector.ProcessPlanar(5, flash_data, linesize, W, H, PixelFormatType::YUV420P);

  // Frames 6-11: Back to Black (Flash suppressed)
  for (int i = 6; i <= 11; ++i) {
    SceneCutResult res = detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
    OLIVE_ASSERT(!res.cut_detected);
  }

  // Frame 12: Genuine cut to Gray
  detector.ProcessPlanar(12, gray_data, linesize, W, H, PixelFormatType::YUV420P);

  // Frame 13: Gray continues -> Cut confirmed at frame 12!
  SceneCutResult res13 = detector.ProcessPlanar(13, gray_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(res13.cut_detected);
  OLIVE_ASSERT_EQUAL(res13.cut_frame_index, 12);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_MinSceneDurationSuppression)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 10;
  config.enable_flash_suppression = false;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> s1_y, s1_u, s1_v;
  std::vector<uint8_t> s2_y, s2_u, s2_v;
  std::vector<uint8_t> s3_y, s3_u, s3_v;
  FillSyntheticYUV420P(s1_y, s1_u, s1_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(s2_y, s2_u, s2_v, W, H, 128, 128, 128);
  FillSyntheticYUV420P(s3_y, s3_u, s3_v, W, H, 235, 128, 128);

  const uint8_t* d1[4] = { s1_y.data(), s1_u.data(), s1_v.data(), nullptr };
  const uint8_t* d2[4] = { s2_y.data(), s2_u.data(), s2_v.data(), nullptr };
  const uint8_t* d3[4] = { s3_y.data(), s3_u.data(), s3_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i < 10; ++i) detector.ProcessPlanar(i, d1, linesize, W, H, PixelFormatType::YUV420P);

  // Frame 10: Cut 1 to s2
  SceneCutResult cut1 = detector.ProcessPlanar(10, d2, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(cut1.cut_detected);
  OLIVE_ASSERT_EQUAL(cut1.cut_frame_index, 10);

  // Frame 14: Cut 2 to s3 occurs only 4 frames later (< 10 min_scene_frames)
  for (int i = 11; i <= 13; ++i) detector.ProcessPlanar(i, d2, linesize, W, H, PixelFormatType::YUV420P);
  SceneCutResult cut2 = detector.ProcessPlanar(14, d3, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!cut2.cut_detected);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_MultiFormatNV12WithStridePadding)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = false;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  const int stride = W + 64;

  std::vector<uint8_t> padded_y1(stride * H, 16);
  std::vector<uint8_t> padded_uv1(stride * (H / 2), 128);

  std::vector<uint8_t> padded_y2(stride * H, 235);
  std::vector<uint8_t> padded_uv2(stride * (H / 2), 128);

  const uint8_t* data1[4] = { padded_y1.data(), padded_uv1.data(), nullptr, nullptr };
  const uint8_t* data2[4] = { padded_y2.data(), padded_uv2.data(), nullptr, nullptr };
  const int linesize[4] = { stride, stride, 0, 0 };

  for (int i = 0; i < 5; ++i) {
    detector.ProcessPlanar(i, data1, linesize, W, H, PixelFormatType::NV12);
  }

  SceneCutResult cut = detector.ProcessPlanar(5, data2, linesize, W, H, PixelFormatType::NV12);
  OLIVE_ASSERT(cut.cut_detected);
  OLIVE_ASSERT_EQUAL(cut.cut_frame_index, 5);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SceneCutDetector_TailFlushAtEOF)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.enable_flash_suppression = true;
  SceneCutDetector detector(config);

  const int W = 64, H = 64;
  std::vector<uint8_t> black_y, black_u, black_v;
  std::vector<uint8_t> white_y, white_u, white_v;
  FillSyntheticYUV420P(black_y, black_u, black_v, W, H, 16, 128, 128);
  FillSyntheticYUV420P(white_y, white_u, white_v, W, H, 235, 128, 128);

  const uint8_t* black_data[4] = { black_y.data(), black_u.data(), black_v.data(), nullptr };
  const uint8_t* white_data[4] = { white_y.data(), white_u.data(), white_v.data(), nullptr };
  const int linesize[4] = { W, W / 2, W / 2, 0 };

  for (int i = 0; i <= 4; ++i) {
    detector.ProcessPlanar(i, black_data, linesize, W, H, PixelFormatType::YUV420P);
  }

  // Frame 5 is the final frame before EOF
  SceneCutResult res5 = detector.ProcessPlanar(5, white_data, linesize, W, H, PixelFormatType::YUV420P);
  OLIVE_ASSERT(!res5.cut_detected);

  // Flush at EOF confirms the tail candidate
  SceneCutResult flushed = detector.Flush();
  OLIVE_ASSERT(flushed.cut_detected);
  OLIVE_ASSERT_EQUAL(flushed.cut_frame_index, 5);

  OLIVE_TEST_END;
}

} // namespace olive
