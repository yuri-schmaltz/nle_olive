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

#include "scenecuttask.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cmath>
#include <QDebug>

#include <olive/core/util/timecodefunctions.h>

#include "common/qtutils.h"
#include "node/project/footage/footage.h"

namespace olive {

namespace {

// RAII Session Guard ensuring complete deallocation of all FFmpeg structures
struct FFmpegSession {
  AVFormatContext* fmt_ctx{nullptr};
  AVCodecContext* codec_ctx{nullptr};
  AVPacket* pkt{nullptr};
  AVFrame* frame{nullptr};
  AVDictionary* opts{nullptr};

  ~FFmpegSession() {
    if (opts) {
      av_dict_free(&opts);
      opts = nullptr;
    }
    if (pkt) {
      av_packet_free(&pkt);
      pkt = nullptr;
    }
    if (frame) {
      av_frame_free(&frame);
      frame = nullptr;
    }
    if (codec_ctx) {
      avcodec_free_context(&codec_ctx);
      codec_ctx = nullptr;
    }
    if (fmt_ctx) {
      avformat_close_input(&fmt_ctx);
      fmt_ctx = nullptr;
    }
  }
};

} // anonymous namespace

SceneCutTask::SceneCutTask(ClipBlock* clip, const SceneCutConfig& config)
  : clip_(clip),
    config_(config)
{
  qRegisterMetaType<QVector<olive::core::rational>>("QVector<olive::core::rational>");
  qRegisterMetaType<olive::core::rational>("olive::core::rational");

  if (clip) {
    TimeRange mr = clip->media_range();
    media_in_ = std::min(mr.in(), mr.out());
    media_out_ = std::max(mr.in(), mr.out());

    auto list = Node::FindInputNodesConnectedToInput<Footage>(
        NodeInput(clip, ClipBlock::kBufferIn));
    if (!list.isEmpty() && list.first()) {
      Footage* footage = list.first();
      filename_ = footage->active_media_filename();
      if (filename_.isEmpty()) {
        filename_ = footage->filename();
      }
      VideoParams vp = footage->GetVideoParams();
      stream_index_ = vp.stream_index();
      frame_rate_ = vp.frame_rate();
      if (frame_rate_.isNull() || frame_rate_ <= core::rational(0)) {
        frame_rate_ = core::rational(30, 1);
      }
    }

    SetTitle(tr("Detecting scene cuts for \"%1\"").arg(clip->Name()));
  } else {
    SetTitle(tr("Detecting scene cuts"));
  }
}

SceneCutTask::SceneCutTask(const QString& filename,
                           int stream_index,
                           const core::rational& media_in,
                           const core::rational& media_out,
                           const core::rational& frame_rate,
                           const SceneCutConfig& config,
                           ClipBlock* clip)
  : clip_(clip),
    filename_(filename),
    stream_index_(stream_index),
    media_in_(media_in),
    media_out_(media_out),
    frame_rate_(frame_rate),
    config_(config)
{
  qRegisterMetaType<QVector<olive::core::rational>>("QVector<olive::core::rational>");
  qRegisterMetaType<olive::core::rational>("olive::core::rational");

  if (frame_rate_.isNull() || frame_rate_ <= core::rational(0)) {
    frame_rate_ = core::rational(30, 1);
  }

  SetTitle(tr("Detecting scene cuts for \"%1\"").arg(filename));
}

QString SceneCutTask::FFmpegError(int error_code)
{
  char err[1024];
  av_strerror(error_code, err, sizeof(err));
  return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(err), QString::number(error_code));
}

bool SceneCutTask::Run()
{
  if (filename_.isEmpty()) {
    SetError(tr("No source media file specified."));
    return false;
  }

  if (media_out_ <= media_in_) {
    SetError(tr("Invalid media analysis range: media_out <= media_in."));
    return false;
  }

  detected_cuts_.clear();
  emit ProgressChanged(0.0);

  // Initialize RAII session on the stack
  FFmpegSession session;

  // 1. Open media file
  int ret = avformat_open_input(&session.fmt_ctx, filename_.toUtf8().constData(), nullptr, nullptr);
  if (ret != 0) {
    SetError(tr("Failed to open file \"%1\": %2").arg(filename_, FFmpegError(ret)));
    return false;
  }

  // 2. Discover stream metadata
  ret = avformat_find_stream_info(session.fmt_ctx, nullptr);
  if (ret < 0) {
    SetError(tr("Failed to find stream info for \"%1\": %2").arg(filename_, FFmpegError(ret)));
    return false;
  }

  // 3. Select video stream
  int stream_idx = stream_index_;
  if (stream_idx < 0 || stream_idx >= static_cast<int>(session.fmt_ctx->nb_streams) ||
      session.fmt_ctx->streams[stream_idx]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
    stream_idx = av_find_best_stream(session.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  }
  if (stream_idx < 0) {
    SetError(tr("No video stream found in \"%1\".").arg(filename_));
    return false;
  }
  AVStream* stream = session.fmt_ctx->streams[stream_idx];

  // 4. Locate video decoder
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    SetError(tr("Failed to find decoder for codec ID %1.").arg(stream->codecpar->codec_id));
    return false;
  }

  // 5. Allocate codec context
  session.codec_ctx = avcodec_alloc_context3(codec);
  if (!session.codec_ctx) {
    SetError(tr("Failed to allocate AVCodecContext."));
    return false;
  }

  // 6. Copy codec parameters from stream
  ret = avcodec_parameters_to_context(session.codec_ctx, stream->codecpar);
  if (ret < 0) {
    SetError(tr("Failed to copy codec parameters: %1").arg(FFmpegError(ret)));
    return false;
  }

  // 7. Enable automatic multi-threaded CPU decoding
  ret = av_dict_set(&session.opts, "threads", "auto", 0);
  if (ret < 0) {
    qWarning() << "Failed to set decoder threads option:" << FFmpegError(ret);
  }

  // 8. Open codec
  ret = avcodec_open2(session.codec_ctx, codec, &session.opts);
  if (ret < 0) {
    SetError(tr("Failed to open decoder: %1").arg(FFmpegError(ret)));
    return false;
  }

  // 9. Allocate packet and frame
  session.pkt = av_packet_alloc();
  session.frame = av_frame_alloc();
  if (!session.pkt || !session.frame) {
    SetError(tr("Failed to allocate packet or frame buffers."));
    return false;
  }

  // Compute stream start offset
  int64_t stream_start_ts = 0;
  if (session.fmt_ctx->start_time != AV_NOPTS_VALUE) {
    stream_start_ts = av_rescale_q(session.fmt_ctx->start_time, {1, AV_TIME_BASE}, stream->time_base);
  } else if (stream->start_time != AV_NOPTS_VALUE) {
    stream_start_ts = stream->start_time;
  }

  // Calculate target timestamps in stream timebase
  int64_t in_ts = Timecode::time_to_timestamp(media_in_, stream->time_base) + stream_start_ts;

  // 10. Perform fast keyframe seek to or before media_in_
  if (media_in_ > core::rational(0)) {
    int seek_ret = av_seek_frame(session.fmt_ctx, stream_idx, in_ts, AVSEEK_FLAG_BACKWARD);
    if (seek_ret >= 0) {
      avcodec_flush_buffers(session.codec_ctx);
    } else {
      qWarning() << "Seek failed, falling back to sequential decode:" << FFmpegError(seek_ret);
    }
  }

  // Initialize scene cut detector engine
  SceneCutDetector detector(config_);
  detector.Reset();

  int64_t frame_index = 0;
  double last_progress = 0.0;
  core::rational total_duration = media_out_ - media_in_;
  bool reached_end_of_range = false;
  bool eof_packet_sent = false;

  // 11. Main packet read and decoding loop
  while (!IsCancelled() && !reached_end_of_range) {
    ret = av_read_frame(session.fmt_ctx, session.pkt);

    if (ret == AVERROR_EOF) {
      if (!eof_packet_sent) {
        avcodec_send_packet(session.codec_ctx, nullptr);
        eof_packet_sent = true;
      }
    } else if (ret < 0) {
      // End of container or read error
      break;
    } else {
      if (session.pkt->stream_index == stream_idx) {
        ret = avcodec_send_packet(session.codec_ctx, session.pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
          qWarning() << "avcodec_send_packet error:" << FFmpegError(ret);
        }
      }
      av_packet_unref(session.pkt);
    }

    // Drain decoded frames from codec
    while (!IsCancelled()) {
      ret = avcodec_receive_frame(session.codec_ctx, session.frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      } else if (ret < 0) {
        qWarning() << "avcodec_receive_frame error:" << FFmpegError(ret);
        break;
      }

      // Calculate frame presentation timestamp
      int64_t pts = (session.frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        ? session.frame->best_effort_timestamp
                        : session.frame->pts;

      core::rational frame_time;
      if (pts != AV_NOPTS_VALUE) {
        int64_t rel_pts = pts - stream_start_ts;
        frame_time = Timecode::timestamp_to_time(rel_pts, stream->time_base);
      } else {
        frame_time = media_in_ + (core::rational(frame_index) / frame_rate_);
      }

      // Pre-roll check: warm up detector if frame is before media_in_
      if (frame_time < media_in_) {
        detector.ProcessFrame(session.frame, frame_index, frame_time);
        av_frame_unref(session.frame);
        frame_index++;
        continue;
      }

      // End-of-range check: stop when media_out_ is reached
      if (frame_time >= media_out_) {
        reached_end_of_range = true;
        av_frame_unref(session.frame);
        break;
      }

      // Ingest frame into detector
      SceneCutResult cut_res = detector.ProcessFrame(session.frame, frame_index, frame_time);
      if (cut_res.cut_detected) {
        if (cut_res.cut_timestamp >= media_in_ && cut_res.cut_timestamp < media_out_) {
          detected_cuts_.append(cut_res.cut_timestamp);
        }
      }

      // Progress reporting throttled to 1% or 10 frames
      if (total_duration > core::rational(0)) {
        double current_progress = std::clamp(
            (frame_time - media_in_).toDouble() / total_duration.toDouble(),
            0.0, 1.0);
        if (current_progress - last_progress >= 0.01 || frame_index % 10 == 0) {
          emit ProgressChanged(current_progress);
          last_progress = current_progress;
        }
      }

      av_frame_unref(session.frame);
      frame_index++;
    }

    if (eof_packet_sent && ret == AVERROR_EOF) {
      break;
    }
  }

  // 12. Check cancellation
  if (IsCancelled()) {
    SetError(tr("Scene cut detection was cancelled."));
    return false;
  }

  // 13. Flush lookahead buffer at end-of-stream
  SceneCutResult flush_res = detector.Flush();
  if (flush_res.cut_detected) {
    if (flush_res.cut_timestamp >= media_in_ && flush_res.cut_timestamp < media_out_) {
      detected_cuts_.append(flush_res.cut_timestamp);
    }
  }

  // Sort cuts chronologically and remove potential duplicates
  std::sort(detected_cuts_.begin(), detected_cuts_.end());
  detected_cuts_.erase(std::unique(detected_cuts_.begin(), detected_cuts_.end()), detected_cuts_.end());

  emit ProgressChanged(1.0);

  // Emit queued signal to GUI thread
  emit SceneCutsDetected(detected_cuts_);

  return true;
}

} // namespace olive
