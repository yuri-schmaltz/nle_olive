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

#include "scenecutdetector.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

namespace olive {

SceneCutDetector::SceneCutDetector(const SceneCutConfig& config)
  : config_(config)
{
  Reset();
}

void SceneCutDetector::Reset()
{
  for (auto& slot : ring_) {
    slot.valid = false;
    slot.is_candidate = false;
    slot.hists.Reset();
    slot.frame_index = -1;
  }
  ring_head_ = 0;
  ring_count_ = 0;

  rolling_scores_.fill(0.0);
  rolling_head_ = 0;
  rolling_count_ = 0;
  rolling_sum_ = 0.0;
  rolling_sq_sum_ = 0.0;

  last_cut_frame_ = -9999;
  total_frames_processed_ = 0;
}

PixelFormatType SceneCutDetector::DetectPixelFormat(int ffmpeg_pix_fmt)
{
  switch (ffmpeg_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      return PixelFormatType::YUV420P;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
      return PixelFormatType::YUV422P;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
      return PixelFormatType::YUV444P;
    case AV_PIX_FMT_NV12:
      return PixelFormatType::NV12;
    case AV_PIX_FMT_NV21:
      return PixelFormatType::NV21;
    default:
      return PixelFormatType::UNKNOWN;
  }
}

int SceneCutDetector::EffectiveStride(int width, int height) const
{
  if (config_.subsample_stride > 0) {
    return config_.subsample_stride;
  }
  int max_dim = std::max(width, height);
  if (max_dim >= 3840) return 8;
  if (max_dim >= 1920) return 4;
  if (max_dim >= 1280) return 2;
  return 1;
}

SceneCutResult SceneCutDetector::ProcessFrame(const AVFrame* frame,
                                             int64_t frame_index,
                                             const core::rational& timestamp)
{
  if (!frame) return SceneCutResult();

  PixelFormatType fmt = DetectPixelFormat(frame->format);
  if (fmt == PixelFormatType::UNKNOWN) {
    fmt = PixelFormatType::YUV420P;
  }

  return ProcessPlanar(frame_index,
                       frame->data,
                       frame->linesize,
                       frame->width,
                       frame->height,
                       fmt,
                       frame->pts,
                       timestamp);
}

SceneCutResult SceneCutDetector::ProcessPlanar(int64_t frame_index,
                                              const uint8_t* const data[4],
                                              const int linesize[4],
                                              int width,
                                              int height,
                                              PixelFormatType format,
                                              int64_t pts,
                                              const core::rational& timestamp)
{
  SceneCutResult confirmed_cut;

  if (width <= 0 || height <= 0 || !data[0]) {
    return confirmed_cut;
  }

  // 1. Prepare slot in circular ring buffer
  size_t slot_idx = (ring_head_ + ring_count_) % kRingCapacity;
  if (ring_count_ == kRingCapacity) {
    ring_head_ = (ring_head_ + 1) % kRingCapacity;
  } else {
    ring_count_++;
  }

  FrameSlot& curr_slot = ring_[slot_idx];
  curr_slot.frame_index = frame_index;
  curr_slot.pts = pts;
  curr_slot.timestamp = timestamp;
  curr_slot.is_candidate = false;
  curr_slot.valid = true;
  curr_slot.score = 0.0;
  curr_slot.threshold = 0.0;

  // 2. Build histograms using strided subsampling
  const int stride = EffectiveStride(width, height);
  BuildHistograms(data, linesize, width, height, format, stride, curr_slot.hists);
  total_frames_processed_++;

  // 3. If first frame, initialize baseline and return
  if (ring_count_ < 2) {
    return confirmed_cut;
  }

  // Identify previous slot (t-1)
  size_t prev_idx = (slot_idx + kRingCapacity - 1) % kRingCapacity;
  FrameSlot& prev_slot = ring_[prev_idx];

  // 4. Compute inter-frame score between frame t-1 and frame t
  double score = ComputeHistDistance(prev_slot.hists, curr_slot.hists);
  double dynamic_thresh = CurrentDynamicThreshold();
  curr_slot.score = score;
  curr_slot.threshold = dynamic_thresh;

  // 5. Lookahead Flash Suppression Pipeline
  if (config_.enable_flash_suppression) {
    bool prev_was_flash = false;

    // Check if previous frame (t-1) was a candidate waiting for lookahead validation
    if (prev_slot.is_candidate && ring_count_ >= 3) {
      size_t pre_prev_idx = (slot_idx + kRingCapacity - 2) % kRingCapacity;
      FrameSlot& pre_prev_slot = ring_[pre_prev_idx];

      // Compare Frame t-2 with Frame t
      double lookahead_dist = ComputeHistDistance(pre_prev_slot.hists, curr_slot.hists);

      if (lookahead_dist < config_.flash_threshold) {
        // Frame t-1 was a transient flash! Suppress it!
        prev_slot.is_candidate = false;
        prev_was_flash = true;
      } else {
        // Confirmed genuine cut at frame t-1!
        confirmed_cut.cut_detected = true;
        confirmed_cut.cut_frame_index = prev_slot.frame_index;
        confirmed_cut.cut_pts = prev_slot.pts;
        confirmed_cut.cut_timestamp = prev_slot.timestamp;
        confirmed_cut.score = prev_slot.score;
        confirmed_cut.threshold = prev_slot.threshold;

        last_cut_frame_ = prev_slot.frame_index;
        prev_slot.is_candidate = false;
      }
    }

    if (prev_was_flash) {
      // Frame t-1 was a flash that returned to scene at frame t.
      curr_slot.is_candidate = false;
      size_t pre_prev_idx = (slot_idx + kRingCapacity - 2) % kRingCapacity;
      double return_score = ComputeHistDistance(ring_[pre_prev_idx].hists, curr_slot.hists);
      curr_slot.score = return_score;
      UpdateRollingStats(return_score);
    } else {
      bool is_cut_candidate = (score >= dynamic_thresh) &&
                              (frame_index - last_cut_frame_ >= config_.min_scene_frames);

      if (is_cut_candidate) {
        curr_slot.is_candidate = true;
      } else {
        curr_slot.is_candidate = false;
        UpdateRollingStats(score);
      }
    }
  } else {
    // Instantaneous mode
    if (score >= dynamic_thresh && (frame_index - last_cut_frame_ >= config_.min_scene_frames)) {
      confirmed_cut.cut_detected = true;
      confirmed_cut.cut_frame_index = curr_slot.frame_index;
      confirmed_cut.cut_pts = curr_slot.pts;
      confirmed_cut.cut_timestamp = curr_slot.timestamp;
      confirmed_cut.score = score;
      confirmed_cut.threshold = dynamic_thresh;
      last_cut_frame_ = frame_index;
    } else {
      UpdateRollingStats(score);
    }
  }

  return confirmed_cut;
}

SceneCutResult SceneCutDetector::Flush()
{
  SceneCutResult tail_cut;

  if (config_.enable_flash_suppression && ring_count_ >= 1) {
    size_t last_idx = (ring_head_ + ring_count_ - 1) % kRingCapacity;
    FrameSlot& last_slot = ring_[last_idx];

    if (last_slot.is_candidate) {
      tail_cut.cut_detected = true;
      tail_cut.cut_frame_index = last_slot.frame_index;
      tail_cut.cut_pts = last_slot.pts;
      tail_cut.cut_timestamp = last_slot.timestamp;
      tail_cut.score = last_slot.score;
      tail_cut.threshold = last_slot.threshold;

      last_cut_frame_ = last_slot.frame_index;
      last_slot.is_candidate = false;
    }
  }

  Reset();
  return tail_cut;
}

void SceneCutDetector::BuildHistograms(const uint8_t* const data[4],
                                      const int linesize[4],
                                      int width,
                                      int height,
                                      PixelFormatType format,
                                      int stride,
                                      FrameHistograms& out_hist) const
{
  out_hist.Reset();

  const int step = std::max(1, stride);

  // 1. Process Y (luma) plane
  const uint8_t* y_plane = data[0];
  const int y_stride = linesize[0];
  uint32_t y_count = 0;

  for (int y = 0; y < height; y += step) {
    const uint8_t* row = y_plane + (y * y_stride);
    for (int x = 0; x < width; x += step) {
      out_hist.y[row[x]]++;
      y_count++;
    }
  }
  out_hist.y_count = y_count;

  // 2. Process U and V planes based on pixel format
  uint32_t uv_count = 0;

  if (format == PixelFormatType::NV12 || format == PixelFormatType::NV21) {
    if (data[1]) {
      const uint8_t* uv_plane = data[1];
      const int uv_stride = linesize[1];
      const int uv_h = height / 2;
      const int uv_w = width / 2;
      const bool is_nv12 = (format == PixelFormatType::NV12);

      for (int y = 0; y < uv_h; y += step) {
        const uint8_t* row = uv_plane + (y * uv_stride);
        for (int x = 0; x < uv_w; x += step) {
          uint8_t u_val = is_nv12 ? row[2 * x] : row[2 * x + 1];
          uint8_t v_val = is_nv12 ? row[2 * x + 1] : row[2 * x];
          out_hist.u[u_val]++;
          out_hist.v[v_val]++;
          uv_count++;
        }
      }
    }
  } else {
    const uint8_t* u_plane = data[1];
    const uint8_t* v_plane = data[2];
    const int u_stride = linesize[1];
    const int v_stride = linesize[2];

    int uv_w = width / 2;
    int uv_h = height / 2;

    if (format == PixelFormatType::YUV422P) {
      uv_h = height;
    } else if (format == PixelFormatType::YUV444P) {
      uv_w = width;
      uv_h = height;
    }

    if (u_plane && v_plane) {
      for (int y = 0; y < uv_h; y += step) {
        const uint8_t* u_row = u_plane + (y * u_stride);
        const uint8_t* v_row = v_plane + (y * v_stride);
        for (int x = 0; x < uv_w; x += step) {
          out_hist.u[u_row[x]]++;
          out_hist.v[v_row[x]]++;
          uv_count++;
        }
      }
    }
  }

  out_hist.uv_count = uv_count;
}

double SceneCutDetector::ComputeHistDistance(const FrameHistograms& h1, const FrameHistograms& h2) const
{
  double diff_y = ComputePlaneL1(h1.y, h1.y_count, h2.y, h2.y_count);

  if (h1.uv_count == 0 || h2.uv_count == 0) {
    return diff_y;
  }

  double diff_u = ComputePlaneL1(h1.u, h1.uv_count, h2.u, h2.uv_count);
  double diff_v = ComputePlaneL1(h1.v, h1.uv_count, h2.v, h2.uv_count);

  return config_.weight_y * diff_y +
         config_.weight_u * diff_u +
         config_.weight_v * diff_v;
}

double SceneCutDetector::ComputePlaneL1(const std::array<uint32_t, 256>& a, uint32_t count_a,
                                       const std::array<uint32_t, 256>& b, uint32_t count_b)
{
  if (count_a == 0 || count_b == 0) return 0.0;

  const double inv_a = 1.0 / count_a;
  const double inv_b = 1.0 / count_b;
  double sum = 0.0;

  for (size_t i = 0; i < 256; ++i) {
    sum += std::abs((a[i] * inv_a) - (b[i] * inv_b));
  }

  return 0.5 * sum;
}

void SceneCutDetector::UpdateRollingStats(double score)
{
  const size_t win_size = static_cast<size_t>(std::clamp(config_.rolling_window_size, 3, 64));

  if (rolling_count_ == win_size) {
    double old_val = rolling_scores_[rolling_head_];
    rolling_sum_ -= old_val;
    rolling_sq_sum_ -= old_val * old_val;
  } else {
    rolling_count_++;
  }

  rolling_scores_[rolling_head_] = score;
  rolling_sum_ += score;
  rolling_sq_sum_ += score * score;
  rolling_head_ = (rolling_head_ + 1) % win_size;
}

double SceneCutDetector::CurrentDynamicThreshold() const
{
  const size_t win_size = static_cast<size_t>(std::clamp(config_.rolling_window_size, 3, 64));

  if (rolling_count_ < std::min<size_t>(5, win_size)) {
    return config_.base_threshold;
  }

  double mean = rolling_sum_ / rolling_count_;
  double variance = std::max(0.0, (rolling_sq_sum_ / rolling_count_) - (mean * mean));
  double stddev = std::sqrt(variance);

  double dynamic = mean + config_.sigma_multiplier * stddev;
  return std::max(config_.base_threshold, dynamic);
}

} // namespace olive
