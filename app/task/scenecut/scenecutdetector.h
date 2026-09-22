/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2026 Olive Team

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

#ifndef SCENECUTDETECTOR_H
#define SCENECUTDETECTOR_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavutil/pixfmt.h>
struct AVFrame;
}

#include <olive/core/util/rational.h>

namespace olive {

enum class PixelFormatType {
  YUV420P,
  YUV422P,
  YUV444P,
  NV12,
  NV21,
  UNKNOWN
};

struct SceneCutConfig {
  double base_threshold = 0.35;         // Minimum adaptive threshold floor [0.0, 1.0]
  double sigma_multiplier = 2.8;        // Multiplier k for adaptive threshold: mean + k * sigma
  int rolling_window_size = 15;         // Number of non-cut frames in rolling statistics window
  int min_scene_frames = 12;            // Minimum duration between cuts in frames
  int subsample_stride = 4;             // Subsample stride (0 = auto-detect from resolution)
  double flash_threshold = 0.18;        // Lookahead distance below which a spike is deemed flash
  bool enable_flash_suppression = true; // Lookahead strobe/flash filtering
  double weight_y = 0.70;               // Weight for luminance channel
  double weight_u = 0.15;               // Weight for U chrominance channel
  double weight_v = 0.15;               // Weight for V chrominance channel
  bool enable_mad = false;              // Enable spatial Mean Absolute Deviation
  double weight_mad = 0.30;             // Weight of MAD (1.0 - weight_mad for histogram)
};

struct SceneCutResult {
  bool cut_detected{false};             // True if a scene cut was confirmed at this frame
  int64_t cut_frame_index{-1};          // Frame index of confirmed cut
  int64_t cut_pts{0};                   // PTS of confirmed cut
  core::rational cut_timestamp{};       // Media timestamp of confirmed cut
  double score{0.0};                    // Inter-frame difference score of confirmed cut
  double threshold{0.0};                // Dynamic threshold that was crossed
};

class SceneCutDetector {
public:
  explicit SceneCutDetector(const SceneCutConfig& config = SceneCutConfig());
  ~SceneCutDetector() = default;

  // Resets detector state for a new stream or after a seek
  void Reset();

  // Ingests an FFmpeg AVFrame (zero heap allocations in inner loop)
  SceneCutResult ProcessFrame(const AVFrame* frame,
                              int64_t frame_index,
                              const core::rational& timestamp = core::rational());

  // Ingests raw planar data (ideal for unit tests and non-FFmpeg pipelines)
  SceneCutResult ProcessPlanar(int64_t frame_index,
                               const uint8_t* const data[4],
                               const int linesize[4],
                               int width,
                               int height,
                               PixelFormatType format,
                               int64_t pts = 0,
                               const core::rational& timestamp = core::rational());

  // Flushes the lookahead pipeline at end-of-stream
  SceneCutResult Flush();

  const SceneCutConfig& config() const { return config_; }
  void set_config(const SceneCutConfig& c) { config_ = c; }

  static PixelFormatType DetectPixelFormat(int ffmpeg_pix_fmt);

private:
  struct FrameHistograms {
    std::array<uint32_t, 256> y{};
    std::array<uint32_t, 256> u{};
    std::array<uint32_t, 256> v{};
    uint32_t y_count{0};
    uint32_t uv_count{0};

    void Reset() {
      y.fill(0);
      u.fill(0);
      v.fill(0);
      y_count = 0;
      uv_count = 0;
    }
  };

  struct FrameSlot {
    int64_t frame_index{-1};
    int64_t pts{0};
    core::rational timestamp{};
    FrameHistograms hists{};
    double score{0.0};
    double threshold{0.0};
    bool is_candidate{false};
    bool valid{false};
  };

  int EffectiveStride(int width, int height) const;

  void BuildHistograms(const uint8_t* const data[4],
                       const int linesize[4],
                       int width,
                       int height,
                       PixelFormatType format,
                       int stride,
                       FrameHistograms& out_hist) const;

  double ComputeHistDistance(const FrameHistograms& h1, const FrameHistograms& h2) const;

  static double ComputePlaneL1(const std::array<uint32_t, 256>& a, uint32_t count_a,
                               const std::array<uint32_t, 256>& b, uint32_t count_b);

  void UpdateRollingStats(double score);
  double CurrentDynamicThreshold() const;

  SceneCutConfig config_;

  // Preallocated 4-slot ring buffer for lookahead pipeline
  static constexpr size_t kRingCapacity = 4;
  std::array<FrameSlot, kRingCapacity> ring_{};
  size_t ring_head_{0};
  size_t ring_count_{0};

  // Rolling statistics ring buffer (O(1) updates, 0 allocations)
  static constexpr size_t kMaxRollingWindow = 64;
  std::array<double, kMaxRollingWindow> rolling_scores_{};
  size_t rolling_head_{0};
  size_t rolling_count_{0};
  double rolling_sum_{0.0};
  double rolling_sq_sum_{0.0};

  int64_t last_cut_frame_{-9999};
  int64_t total_frames_processed_{0};
};

} // namespace olive

#endif // SCENECUTDETECTOR_H
