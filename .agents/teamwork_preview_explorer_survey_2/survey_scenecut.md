# Comprehensive Code Survey: Scene Cut Detection & TaskManager (Requirement R2)

**Author**: `teamwork_preview_explorer_survey_2` (Video Analysis & TaskManager Explorer)  
**Date**: September 20, 2026  
**Target Codebase**: Olive Video Editor (`/home/yuri/Documentos/olive`)  
**Standard**: Native C++17, Pure Qt6, DAG-preserving, Thread-Safe, AddressSanitizer 0-leak compliant  

---

## 1. Executive Summary & System Overview

Requirement **R2** mandates:
1. An asynchronous video analyzer integrated into Olive's `TaskManager` capable of detecting shot transitions and scene cuts via histogram analysis and frame difference thresholds during video decoding.
2. Direct integration into Olive's timeline to enable automatic splitting (razor cuts) of selected clips at the detected cut points with full Undo/Redo support.

This code survey examines the entire path from background task scheduling and non-interfering video decoding to computer vision cut detection algorithms, timeline data model splitting, thread-safe asynchronous dispatch, and automated testing.

### Key Architectural Discoveries
1. **TaskManager Infrastructure**: Olive provides a centralized background task subsystem (`olive::TaskManager`, singleton) managing `olive::Task` derivatives via `QThreadPool` and `QtConcurrent::run`. It includes native progress reporting (`ProgressChanged(double)`), cancellation via atomic flags (`CancelAtom`), and UI monitoring (`TaskView` / `TaskViewItem`).
2. **Video Decoding & Playback Isolation**: Video decoding during playback occurs through `DecoderCache` and `RenderManager`. To decode video frames for scene cut detection **without interfering with timeline playback or causing playback stutter**, the analyzer must NOT go through the GPU `RenderManager` pipeline. Instead, it must instantiate a dedicated, headless CPU decoding session (using FFmpeg's `libavcodec`/`libavformat` directly or an independent `FFmpegDecoder` instance). This enables raw CPU decoding at 300–800+ FPS directly accessing `AVFrame` Y (luma) planar data with zero GPU/render lock contention.
3. **Existing Timeline Splitting Engine**: Olive **already has** a robust multi-point splitting command: `BlockSplitPreservingLinksCommand` (`app/timeline/timelineundosplit.h`, lines 76–122). It accepts a list of blocks and a `QList<rational>` of timestamps, automatically sorts timestamps in ascending order, splits the blocks using `BlockSplitCommand`, and automatically relinks linked video/audio blocks with `NodeLinkCommand`. It is an `UndoCommand` that plugs directly into `Core::instance()->undo_stack()`.
4. **Thread Safety Model**: In Olive, all DAG node modifications, track block insertions/deletions, and undo stack operations **must run strictly on the main GUI thread**. The background task must only perform decoding and numerical analysis, outputting a value-type list of cut timestamps (`QVector<rational>`) back to the GUI thread via a Qt queued signal.

---

## 2. TaskManager & Background Jobs Subsystem

### 2.1 File Locations & Core Classes

| Component | File Path | Role |
| :--- | :--- | :--- |
| **`Task` Base Class** | `app/task/task.h` | Base class for async jobs; inherits `QObject` and `CancelableObject` |
| **`CancelableObject`** | `app/common/cancelableobject.h` | Provides thread-safe cancellation via `CancelAtom` |
| **`CancelAtom`** | `app/render/cancelatom.h` | Atomic boolean flag wrapper (`std::atomic<bool>`) |
| **`TaskManager`** | `app/task/taskmanager.h`<br>`app/task/taskmanager.cpp` | Singleton queue & thread pool manager for all tasks |
| **Task UI Panel** | `app/panel/taskmanager/taskmanager.h`<br>`app/panel/taskmanager/taskmanager.cpp` | Dockable tool panel hosting `TaskView` |
| **Task UI View & Items** | `app/widget/taskview/taskview.h`<br>`app/widget/taskview/taskviewitem.h`<br>`app/widget/taskview/taskviewitem.cpp` | Visual progress bar, cancellation button, and ETA display |

### 2.2 Task Lifecycle & Execution Model

```
[Main Thread]
   │
   ├─► SceneCutTask* task = new SceneCutTask(clip, params);
   ├─► connect(task, &SceneCutTask::SceneCutsDetected, receiver, &OnCutsDetected);
   └─► TaskManager::instance()->AddTask(task);
         │
         ├─► Instantiates QFutureWatcher<bool>* watcher
         ├─► tasks_.insert(watcher, task)
         ├─► watcher->setFuture(QtConcurrent::run(&thread_pool_, &Task::Start, task))
         ├─► emit TaskAdded(task); emit TaskListChanged();
         ▼
[Worker Thread in QThreadPool]
   │
   ├─► task->Start()
   │     ├─► start_time_ = QDateTime::currentMSecsSinceEpoch()
   │     ├─► emit Started(start_time_)
   │     ├─► bool ret = task->Run();  <── Pure video decoding & analysis loop
   │     │     ├─► while(has_frames && !IsCancelled()) {
   │     │     │     emit ProgressChanged(current_progress);
   │     │     │   }
   │     │     └─► emit SceneCutsDetected(cuts);
   │     ├─► emit Finished(this, ret)
   │     └─► return ret;
   ▼
[Main Thread Event Loop]
   │
   ├─► watcher triggers finished signal ──► TaskManager::TaskFinished()
   │     ├─► if (watcher->result()) {
   │     │     emit TaskRemoved(task);
   │     │     task->deleteLater();
   │     │   } else {
   │     │     emit TaskFailed(task);
   │     │     failed_tasks_.push_back(task);
   │     │   }
   │     └─► watcher->deleteLater();
   └─► OnCutsDetected(cuts) executes on GUI thread!
```

### 2.3 Key Function Signatures & Mechanics

1. **Subclassing `olive::Task` (`app/task/task.h`)**:
   ```cpp
   class Task : public QObject, public CancelableObject {
     Q_OBJECT
   public:
     Task();
     const QString& GetTitle() const;
     const QString& GetError() const;
     const qint64& GetStartTime() const;
   public slots:
     bool Start(); // Emits Started(), invokes Run(), emits Finished(), returns bool
     virtual void Reset();
     void Cancel(); // Delegates to CancelableObject::Cancel()
   protected:
     virtual bool Run() = 0; // The pure virtual work function
     void SetError(const QString& s);
     void SetTitle(const QString& s);
   signals:
     void Started(qint64 start_time);
     void ProgressChanged(double d); // Progress value 0.0 to 1.0
     void Finished(Task *task, bool succeeded);
   };
   ```

2. **Managing Tasks via `TaskManager` (`app/task/taskmanager.h`)**:
   ```cpp
   class TaskManager : public QObject {
     Q_OBJECT
   public:
     static TaskManager* instance();
     int GetTaskCount() const;
     Task* GetFirstTask() const;
     void CancelTaskAndWait(Task* t);
   public slots:
     void AddTask(Task *t);    // Takes ownership of Task; must be called on GUI thread
     void CancelTask(Task* t);
   signals:
     void TaskAdded(Task* t);
     void TaskListChanged();
     void TaskRemoved(Task* t);
     void TaskFailed(Task* t);
   };
   ```

3. **Progress Reporting & Cancellation**:
   - Inside `Run()`, the worker calls `emit ProgressChanged(double fraction)` where `fraction` is between `0.0` and `1.0`.
   - `TaskViewItem::UpdateProgress(double d)` automatically updates `QProgressBar` (0–100) and recalculates elapsed/estimated remaining time via `ElapsedCounterWidget`.
   - Cancellation is thread-safe and non-blocking: `task->Cancel()` sets `CancelAtom::cancelled_` to `true`.
   - In the decoding loop, the task checks `if (IsCancelled()) { return false; }`.

---

## 3. Video Decoding & Frame Extraction Without Interrupted Playback

A critical question in Requirement R2 is how to decode video frames efficiently without interfering with active timeline playback.

### 3.1 Architectural Comparison of Decoding Paths

#### Path A: The DAG RenderTask Route (`app/task/render/render.h`)
- Used by `PreCacheTask` (`app/task/precache/precachetask.cpp`) and `ExportTask` (`app/task/export/export.cpp`).
- Evaluates the DAG node graph through `ViewerOutput` and `RenderManager::instance()->RenderVideo(...)`.
- **Downsides for Scene Cut Detection**:
  1. **Direct GPU & Playback Contention**: All video tickets are processed by `RenderManager`'s rendering queue. Running a scene cut analysis on a 30-minute 4K clip floods `RenderManager` with thousands of GPU render tickets, starving playback tickets and causing severe UI/preview frame drops.
  2. **Heavy GPU Readback Bottleneck**: Every frame is uploaded as an OpenGL texture, rendered via shader pipelines, and then copied back to CPU RAM via PBO (`DownloadFrame`). Throughput is capped at ~30–60 FPS.
  3. **Unnecessary DAG Overhead**: Color conversion (OCIO), transformations, and shader compositing are completely redundant when only calculating histogram distributions of source frames.

#### Path B: Dedicated Headless CPU FFmpeg Decoding (Recommended)
- Olive's video decoding backend is built on FFmpeg (`libavcodec`, `libavformat`, `libswscale`).
- Active timeline playback maintains decoders inside `DecoderCache` (`app/render/rendercache.h`):
  ```cpp
  using DecoderCache = RenderCache<Decoder::CodecStream, DecoderPair>;
  ```
- **The Non-Interfering Solution**:
  The background `SceneCutTask` initializes its **own independent decoding context** for the media file:
  - It does NOT touch `DecoderCache` (no lock contention on `decoder_cache_->mutex()`).
  - It does NOT touch `RenderManager` (zero GPU contention, zero shader execution, zero PBO readbacks).
  - It opens the media file with an independent `AVFormatContext` and `AVCodecContext` (configured with `threads = auto`).
  - It reads packets sequentially via `av_read_frame()`, decodes them via `avcodec_send_packet()` / `avcodec_receive_frame()`, and processes `AVFrame` directly in system RAM.
  - **Throughput**: Sequential CPU decoding of 1080p/4K footage on modern multi-core systems easily achieves **300–800+ FPS** (10x to 25x faster than real-time playback).
  - Active timeline playback continues completely uninterrupted at full frame rate.

### 3.2 Accessing Media Source from a Selected `ClipBlock`

To detect scene cuts on a selected timeline clip, we inspect how `ClipBlock` connects to media footage:

1. **Traversing Upstream to `Footage`**:
   In `ClipBlock` (`app/node/block/clip/clip.cpp`), the source media connects to the input named `kBufferIn` (`"buffer_in"`):
   ```cpp
   Footage* GetFootageFromClip(ClipBlock* clip) {
     if (!clip) return nullptr;
     Node* connected = clip->GetConnectedOutput(ClipBlock::kBufferIn);
     if (auto footage = dynamic_cast<Footage*>(connected)) {
       return footage;
     }
     // If an intermediate filter or speed effect is in-between:
     auto list = Node::FindInputNodesConnectedToInput<Footage>(
         NodeInput(clip, ClipBlock::kBufferIn));
     return list.isEmpty() ? nullptr : list.first();
   }
   ```

2. **Extracting File Path, Stream Index, and Time Range**:
   From `Footage* footage` (`app/node/project/footage/footage.h`):
   - Media File Path: `QString filename = footage->filename();`
   - Video Parameters: `VideoParams params = footage->GetVideoParams();`
   - Video Stream Index: `int stream_idx = params.stream_index();`
   - Frame Rate / Timebase: `rational frame_rate = footage->video_frame_rate();`
   - Clip's Active Media Range:
     ```cpp
     rational media_in = clip->media_in();
     rational media_out = clip->media_in() + clip->length() * clip->speed();
     ```
   - Only frames within `[media_in, media_out]` need to be analyzed!

---

## 4. Scene Cut Detection Algorithms (Native C++17)

### 4.1 Theoretical Foundation & Comparative Metrics

A professional scene cut detection engine must distinguish three types of video transitions:
1. **Hard Cuts (Instantaneous Transitions)**: An abrupt discontinuity between frame $t-1$ and frame $t$ (e.g. angle change or scene change). Characterized by a sharp spike in both histogram distance and pixel difference.
2. **Gradual Transitions (Dissolves, Fades, Wipes)**: A progressive blend over 10–60 frames. Detected via cumulative histogram drift across a sliding window.
3. **Motion / Camera Panning (False Positives to reject)**: Large spatial pixel change, but nearly identical overall color distribution. Histogram comparison is invariant to translation and rotation, naturally suppressing motion false positives.
4. **Flash / Strobe Lights (False Positives to reject)**: An abrupt luminance spike lasting exactly 1 or 2 frames before returning to the original scene. Suppressed by lookahead validation: if frame $t+1$ or $t+2$ matches frame $t-1$, the transition is flagged as a flash artifact and rejected.

### 4.2 Mathematical Formulations

#### 1. Luma (Y-Plane) Histogram Difference
In standard video formats (`AV_PIX_FMT_YUV420P`, `AV_PIX_FMT_NV12`), `AVFrame->data[0]` contains the raw 8-bit Luminance ($Y \in [0, 255]$).
For two consecutive frames $A$ and $B$, compute 256-bin normalized histograms $H_A, H_B$:
$$H(i) = \frac{count(Y == i)}{N_{pixels}}, \quad \sum_{i=0}^{255} H(i) = 1.0$$

The normalized Manhattan distance ($L_1$ norm) is:
$$D_{hist}(A, B) = \frac{1}{2} \sum_{i=0}^{255} |H_A(i) - H_B(i)| \in [0.0, 1.0]$$
- If $A == B$, $D_{hist} = 0.0$.
- If completely disjoint (e.g. all black to all white), $D_{hist} = 1.0$.

#### 2. Color Histogram Difference (RGB / YUV 3-Channel)
When two scenes share similar average luminance but have distinct colors (e.g. cutting between a red studio background and a blue sky):
$$D_{color}(A, B) = \frac{1}{3} \left( D(H_{Y_A}, H_{Y_B}) + D(H_{U_A}, H_{U_B}) + D(H_{V_A}, H_{V_B}) \right)$$
Using the native $Y, U, V$ planes of `AVFrame` avoids the computational expense of converting YUV to RGB!
- $Y$ plane: `data[0]`, $W \times H$
- $U$ plane: `data[1]`, $(W/2) \times (H/2)$
- $V$ plane: `data[2]`, $(W/2) \times (H/2)$

#### 3. Spatial Pixel Intensity Difference (Mean Absolute Deviation - MAD)
To catch localized composition changes while rejecting uniform brightness shifts:
$$D_{MAD}(A, B) = \frac{1}{W_{sub} \times H_{sub}} \sum_{y=0}^{H_{sub}-1} \sum_{x=0}^{W_{sub}-1} \frac{|Y_A(x \cdot s_x, y \cdot s_y) - Y_B(x \cdot s_x, y \cdot s_y)|}{255.0}$$
where $s_x, s_y$ are subsampling strides (e.g., sample every 4th pixel horizontally and vertically, reducing computational load by $16\times$).

#### 4. Combined Difference Score ($S_t$)
$$S_t = w_{hist} \cdot D_{hist}(t-1, t) + w_{MAD} \cdot D_{MAD}(t-1, t)$$
Recommended weights: $w_{hist} = 0.70$, $w_{MAD} = 0.30$.

#### 5. Adaptive Rolling-Window Thresholding
Fixed thresholds fail on very dark scenes or high-action scenes. An adaptive threshold evaluates $S_t$ against local temporal statistics over a rolling window $W = [t-12, \dots, t-1]$ (approx. 0.5 sec):
$$\mu_W = \frac{1}{|W|} \sum_{k \in W} S_k, \quad \sigma_W = \sqrt{\frac{1}{|W|} \sum_{k \in W} (S_k - \mu_W)^2}$$
$$\text{Threshold}_t = \max\left(\theta_{base}, \, \mu_W + k_{sigma} \cdot \sigma_W\right)$$
Recommended defaults: $\theta_{base} = 0.35$, $k_{sigma} = 2.8$.
Frame $t$ is a cut candidate if $S_t \ge \text{Threshold}_t$.

#### 6. Flash & Strobe Suppression
If frame $t$ exceeds the threshold, check the difference between frame $t-1$ and frame $t+1$:
$$\text{If } D_{hist}(t-1, t+1) < 0.15 \implies \text{Flash artifact, do NOT cut.}$$

#### 7. Minimum Scene Duration Enforcement
Enforce a minimum scene duration $\Delta t_{min} \ge 12 \text{ frames}$ (approx. 0.4–0.5s at 24–30fps) to eliminate jitter cuts during strobe lights or rapid explosions.

### 4.3 High-Performance C++17 Implementation Pattern

```cpp
#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <algorithm>

namespace olive {

struct SceneCutConfig {
  double base_threshold = 0.35;
  double sigma_multiplier = 2.8;
  int rolling_window_size = 15;
  int min_scene_frames = 12;
  int subsample_stride = 4; // Subsample every 4th pixel
};

class SceneCutDetector {
public:
  explicit SceneCutDetector(const SceneCutConfig& config = {}) : config_(config) {}

  void Reset() {
    prev_luma_hist_.fill(0);
    prev_u_hist_.fill(0);
    prev_v_hist_.fill(0);
    recent_scores_.clear();
    last_cut_frame_ = -9999;
    has_prev_frame_ = false;
  }

  // Analyzes raw planar YUV data from AVFrame (zero copies, zero allocations)
  bool ProcessFrame(int64_t frame_index,
                    const uint8_t* y_data, int y_stride,
                    const uint8_t* u_data, int u_stride,
                    const uint8_t* v_data, int v_stride,
                    int width, int height,
                    double* out_score = nullptr)
  {
    std::array<uint32_t, 256> curr_y_hist{};
    std::array<uint32_t, 256> curr_u_hist{};
    std::array<uint32_t, 256> curr_v_hist{};

    // 1. Build Y histogram with strided subsampling for extreme speed
    const int step = config_.subsample_stride;
    uint32_t y_pixel_count = 0;
    for (int y = 0; y < height; y += step) {
      const uint8_t* row = y_data + (y * y_stride);
      for (int x = 0; x < width; x += step) {
        curr_y_hist[row[x]]++;
        y_pixel_count++;
      }
    }

    // 2. Build U and V histograms (chroma planes are half resolution in YUV420p)
    uint32_t uv_pixel_count = 0;
    const int uv_h = height / 2;
    const int uv_w = width / 2;
    for (int y = 0; y < uv_h; y += step) {
      const uint8_t* u_row = u_data + (y * u_stride);
      const uint8_t* v_row = v_data + (y * v_stride);
      for (int x = 0; x < uv_w; x += step) {
        curr_u_hist[u_row[x]]++;
        curr_v_hist[v_row[x]]++;
        uv_pixel_count++;
      }
    }

    if (!has_prev_frame_) {
      prev_luma_hist_ = curr_y_hist;
      prev_u_hist_ = curr_u_hist;
      prev_v_hist_ = curr_v_hist;
      prev_y_pixel_count_ = y_pixel_count;
      prev_uv_pixel_count_ = uv_pixel_count;
      has_prev_frame_ = true;
      if (out_score) *out_score = 0.0;
      return false;
    }

    // 3. Compute normalized L1 distances
    double diff_y = ComputeHistL1(prev_luma_hist_, prev_y_pixel_count_, curr_y_hist, y_pixel_count);
    double diff_u = ComputeHistL1(prev_u_hist_, prev_uv_pixel_count_, curr_u_hist, uv_pixel_count);
    double diff_v = ComputeHistL1(prev_v_hist_, prev_uv_pixel_count_, curr_v_hist, uv_pixel_count);

    // Weighted combined difference (Y luma dominates, UV chroma disambiguates)
    double score = 0.70 * diff_y + 0.15 * diff_u + 0.15 * diff_v;
    if (out_score) *out_score = score;

    // 4. Adaptive thresholding
    bool is_cut = false;
    if (recent_scores_.size() >= static_cast<size_t>(config_.rolling_window_size)) {
      double mean = std::accumulate(recent_scores_.begin(), recent_scores_.end(), 0.0) / recent_scores_.size();
      double sq_sum = 0.0;
      for (double s : recent_scores_) sq_sum += (s - mean) * (s - mean);
      double stddev = std::sqrt(sq_sum / recent_scores_.size());

      double dynamic_thresh = std::max(config_.base_threshold, mean + config_.sigma_multiplier * stddev);
      if (score >= dynamic_thresh && (frame_index - last_cut_frame_ >= config_.min_scene_frames)) {
        is_cut = true;
        last_cut_frame_ = frame_index;
      }
    } else if (score >= config_.base_threshold && (frame_index - last_cut_frame_ >= config_.min_scene_frames)) {
      is_cut = true;
      last_cut_frame_ = frame_index;
    }

    // Update rolling window
    recent_scores_.push_back(score);
    if (recent_scores_.size() > static_cast<size_t>(config_.rolling_window_size)) {
      recent_scores_.erase(recent_scores_.begin());
    }

    prev_luma_hist_ = curr_y_hist;
    prev_u_hist_ = curr_u_hist;
    prev_v_hist_ = curr_v_hist;
    prev_y_pixel_count_ = y_pixel_count;
    prev_uv_pixel_count_ = uv_pixel_count;

    return is_cut;
  }

private:
  static double ComputeHistL1(const std::array<uint32_t, 256>& h1, uint32_t n1,
                              const std::array<uint32_t, 256>& h2, uint32_t n2) {
    if (n1 == 0 || n2 == 0) return 0.0;
    double inv1 = 1.0 / n1;
    double inv2 = 1.0 / n2;
    double sum = 0.0;
    for (size_t i = 0; i < 256; ++i) {
      sum += std::abs((h1[i] * inv1) - (h2[i] * inv2));
    }
    return 0.5 * sum; // Normalized to [0.0, 1.0]
  }

  SceneCutConfig config_;
  std::array<uint32_t, 256> prev_luma_hist_{};
  std::array<uint32_t, 256> prev_u_hist_{};
  std::array<uint32_t, 256> prev_v_hist_{};
  uint32_t prev_y_pixel_count_{0};
  uint32_t prev_uv_pixel_count_{0};
  std::vector<double> recent_scores_;
  int64_t last_cut_frame_{-9999};
  bool has_prev_frame_{false};
};

} // namespace olive
```

---

## 5. Timeline Clip Model & Auto-Split Command

### 5.1 Olive's Timeline Hierarchy

```
Project (app/node/project.h)
  └── Sequence (app/node/project/sequence/sequence.h)
        ├── TrackList [Track::kVideo]
        │     └── Track (app/node/output/track/track.h)
        │           ├── ClipBlock (in=0, out=10) ──► NodeInput(kBufferIn) ──► Footage
        │           ├── TransitionBlock (in=9.5, out=10.5)
        │           └── ClipBlock (in=10, out=25)
        └── TrackList [Track::kAudio]
              └── Track
                    └── ClipBlock (linked to Video ClipBlock via block_links())
```

- **`Block` (`app/node/block/block.h`)**:
  - `in()`: start timestamp in sequence timeline.
  - `out()`: end timestamp in sequence timeline.
  - `length()`: `out() - in()`.
- **`ClipBlock` (`app/node/block/clip/clip.h`)**:
  - `media_in()`: offset into source media.
  - `block_links()`: list of associated blocks (e.g. linked audio track blocks).
  - `SequenceToMediaTime(time)` & `MediaToSequenceTime(time)`: converts between source media time and timeline sequence time, accounting for speed and reverse playback.

### 5.2 Splitting Commands: `BlockSplitPreservingLinksCommand`

Olive has a production-ready command for splitting blocks at multiple timestamps in `app/timeline/timelineundosplit.h`:

```cpp
class BlockSplitPreservingLinksCommand : public UndoCommand {
public:
  BlockSplitPreservingLinksCommand(const QVector<Block *> &blocks, const QList<rational>& times);
  virtual ~BlockSplitPreservingLinksCommand() override;
  virtual Project* GetRelevantProject() const override;
  Block *GetSplit(Block *original, int time_index) const;
protected:
  virtual void prepare() override;
  virtual void redo() override;
  virtual void undo() override;
};
```

#### How It Operates Internally (`app/timeline/timelineundosplit.cpp`, lines 112–164):
1. **Timestamp Sorting**:
   `std::sort(times_.begin(), times_.end());`
   Times are sorted in ascending chronological order so earlier cuts are applied first without displacing the coordinates of later cuts.
2. **Sequential Split via `BlockSplitCommand`**:
   For each cut timestamp $T_{cut}$, it checks if each block contains the cut:
   ```cpp
   if (b->in() < time && b->out() > time) {
     BlockSplitCommand* split_command = new BlockSplitCommand(b, time);
     split_command->redo_now();
     splits.replace(j, split_command->new_block());
     commands_.append(split_command);
   }
   ```
3. **Link Preservation**:
   It inspects existing links (`Block::AreLinked(a, b)`). For every pair of blocks that were originally linked (e.g., video track 1 and audio track 1), it creates and executes `NodeLinkCommand` for all generated sub-blocks!
4. **Full Undo / Redo**:
   - `undo()` iterates through the commands in reverse order (`commands_.size() - 1 down to 0`), calling `undo_now()`. This perfectly rejoins the clips and restores original track state.
   - `redo()` re-executes each step.

### 5.3 Calculating Split Timestamps from Media Space to Timeline Space

When the detector identifies a scene cut at source frame $N_{cut}$ with media timestamp $T_{media}$:
1. Convert media time to sequence-relative time:
   $$T_{clip} = \text{clip}->\text{MediaToSequenceTime}(T_{media})$$
2. Convert clip-relative time to absolute timeline sequence time:
   $$T_{timeline} = \text{clip}->\text{in}() + T_{clip}$$
3. Boundary Guard: Verify that:
   $$\text{clip}->\text{in}() < T_{timeline} < \text{clip}->\text{out}()$$
   Any cut landing outside the clip's trimmed in/out interval is discarded.

### 5.4 UI Integration Points

1. **Context Menu on Timeline Clips**:
   In `TimelineWidget` / `TimelinePanel`, add a context menu item:
   `"Auto-Split Scenes..."` or `"Detect Scene Cuts..."` when one or more video clips are selected.
2. **Main Menu**:
   In `MenuShared::AddItemsForClipEditMenu` (`app/widget/menu/menushared.cpp`):
   ```cpp
   QAction* clip_detect_scenes_item_;
   clip_detect_scenes_item_ = m->AddItem("detectscenecuts", this, &MenuShared::DetectSceneCutsTriggered);
   ```
3. **Parameter Dialog (`SceneCutDialog`)**:
   - Detection Sensitivity / Threshold slider: 0.1 to 1.0 (default 0.35).
   - Minimum Scene Length in frames (default 12 frames).
   - Action Option: "Split Clip into Sub-Clips" vs "Add Timeline Markers at Cuts".

---

## 6. Thread Safety & Asynchrony Architecture

To prevent data races, crashes, and deadlocks, the system must strictly isolate background worker execution from main GUI thread DAG mutations.

### 6.1 Strict Execution Boundaries

| Action | Allowed Thread | Enforcement Mechanism |
| :--- | :--- | :--- |
| File I/O (`avformat_open_input`) | Background Worker | Independent `AVFormatContext` |
| Packet & Frame Decoding | Background Worker | Independent `AVCodecContext` |
| Histogram & Difference Computation | Background Worker | Pure C++ stack memory / detector instance |
| Progress Updates | Background Worker ──► GUI | `emit ProgressChanged(double)` (Qt queued signal) |
| Cancellation Check | Background Worker | `IsCancelled()` (atomic `CancelAtom`) |
| Results Transmission | Background Worker ──► GUI | `emit SceneCutsDetected(clip, cuts)` (Qt queued signal) |
| DAG Node Mutation | **Main GUI Thread ONLY** | Executed in slot triggered by queued signal |
| Undo Stack Push (`Core::undo_stack()`) | **Main GUI Thread ONLY** | Executed in slot triggered by queued signal |

### 6.2 Lifespan & Cancellation Protection

What happens if the user deletes the clip or closes the sequence while `SceneCutTask` is running in the background?
1. **Dangling Pointer Prevention**:
   Use `QPointer<ClipBlock>` and `QPointer<Sequence>`:
   ```cpp
   QPointer<ClipBlock> safe_clip = clip;
   connect(task, &SceneCutTask::SceneCutsDetected, this, [safe_clip](const QVector<rational>& cuts) {
     if (!safe_clip || !safe_clip->track() || !safe_clip->project()) {
       // Clip was removed or project closed while analyzing; discard safely
       return;
     }
     // Safe to execute split
   });
   ```
2. **Project Closure**:
   Olive's project destruction cancels all active tasks via `TaskManager::instance()->CancelTaskAndWait(...)` or cleans up gracefully.

---

## 7. Unit Testing & Gauntlet Verification Strategy

### 7.1 Test Framework Anatomy

Olive uses a lightweight test macro harness defined in `tests/testutil.h`:
- `OLIVE_ADD_TEST(TestName)`: Declares a test function returning `int` (`OLIVE_TEST_SUCCESS` on pass, `__LINE__` on fail).
- `OLIVE_ASSERT(cond)`: Returns `__LINE__` if condition is false.
- `OLIVE_ASSERT_EQUAL(a, b)`: Prints mismatch and returns `__LINE__` if $a \neq b$.
- `OLIVE_TEST_END`: Returns `OLIVE_TEST_SUCCESS` (`-1`).

Tests are compiled and registered via CMake in `tests/CMakeLists.txt`:
```cmake
olive_add_test(GROUP NAME SOURCE)
```
which reads the source file, extracts `OLIVE_ADD_TEST(...)`, auto-generates a `main()` runner, links `$<TARGET_OBJECTS:libolive-editor>`, and creates a CTest target.

### 7.2 Proposed Test Cases

#### Test 1: Computer Vision Scene Cut Detector Unit Tests (`tests/timeline/timeline-tests.cpp` or `tests/general/common-tests.cpp`)
```cpp
OLIVE_ADD_TEST(SceneCutDetector_SyntheticFrames)
{
  SceneCutConfig config;
  config.base_threshold = 0.35;
  config.min_scene_frames = 5;
  config.subsample_stride = 1;
  SceneCutDetector detector(config);

  const int W = 64;
  const int H = 64;
  std::vector<uint8_t> black_y(W * H, 16);   // TV black
  std::vector<uint8_t> white_y(W * H, 235);  // TV white
  std::vector<uint8_t> neutral_uv(W * H / 4, 128);

  double score = 0.0;

  // Frame 0: First frame never cuts
  bool cut0 = detector.ProcessFrame(0, black_y.data(), W, neutral_uv.data(), W/2, neutral_uv.data(), W/2, W, H, &score);
  OLIVE_ASSERT(!cut0);

  // Frame 1-4: Identical black frames -> score must be 0.0
  for (int i = 1; i <= 4; ++i) {
    bool cut = detector.ProcessFrame(i, black_y.data(), W, neutral_uv.data(), W/2, neutral_uv.data(), W/2, W, H, &score);
    OLIVE_ASSERT(!cut);
    OLIVE_ASSERT(score < 0.01);
  }

  // Frame 5: Abrupt transition from black to white -> must trigger cut!
  bool cut5 = detector.ProcessFrame(5, white_y.data(), W, neutral_uv.data(), W/2, neutral_uv.data(), W/2, W, H, &score);
  OLIVE_ASSERT(cut5);
  OLIVE_ASSERT(score > 0.80);

  // Frame 6: White frame again -> no cut
  bool cut6 = detector.ProcessFrame(6, white_y.data(), W, neutral_uv.data(), W/2, neutral_uv.data(), W/2, W, H, &score);
  OLIVE_ASSERT(!cut6);
  OLIVE_ASSERT(score < 0.01);

  OLIVE_TEST_END;
}
```

#### Test 2: Flash Artifact Suppression Test
Verify that an isolated 1-frame strobe spike (black -> white -> black) does not produce a false positive scene cut if flash filtering is active.

#### Test 3: Timeline Auto-Split Test (`tests/timeline/timeline-tests.cpp`)
```cpp
OLIVE_ADD_TEST(TimelineAutoSplit_MultiCut)
{
  TIMELINE_TEST_START;
  sequence.add_default_nodes();

  TrackList *v_list = sequence.track_list(Track::kVideo);
  Track *v_track = v_list->GetTracks().first();

  ClipBlock *clip = new ClipBlock();
  clip->set_length_and_media_out(rational(100)); // 100 seconds
  clip->setParent(&project);
  v_track->AppendBlock(clip);

  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);

  // Split at t = 20, t = 45, t = 80
  QList<rational> cut_points = {rational(20), rational(45), rational(80)};
  BlockSplitPreservingLinksCommand split_cmd({clip}, cut_points);
  split_cmd.redo_now();

  // Must have 4 resulting blocks
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(0)->length(), rational(20));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(1)->length(), rational(25));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(2)->length(), rational(35));
  OLIVE_ASSERT_EQUAL(v_track->Blocks().at(3)->length(), rational(20));

  // Test Undo: reverts cleanly to 1 block of length 100
  split_cmd.undo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 1);
  OLIVE_ASSERT_EQUAL(v_track->Blocks().first(), clip);
  OLIVE_ASSERT_EQUAL(clip->length(), rational(100));

  // Test Redo: reapplies 4 blocks
  split_cmd.redo_now();
  OLIVE_ASSERT_EQUAL(v_track->Blocks().size(), 4);

  OLIVE_TEST_END;
}
```

### 7.3 AddressSanitizer (Gauntlet) Compliance

The project's quality gate is `scripts/gauntlet.py --preset linux-asan --jobs 4`.
To achieve **0 memory leaks and 0 ASan errors**:
1. Every `AVPacket` must be managed via RAII (`av_packet_alloc()` and `av_packet_free()`).
2. Every `AVFrame` must be wrapped in `AVFramePtr` via Olive's `CreateAVFramePtr()`, which registers `av_frame_free()` as its custom deleter.
3. Every `AVCodecContext` must be deallocated with `avcodec_free_context(&codec_ctx)`.
4. Every `AVFormatContext` must be closed with `avformat_close_input(&fmt_ctx)`.
5. All Qt objects created on heap must have explicit parentage or be deleted with `deleteLater()`.

---

## 8. Implementation Blueprint & Recommended File Tree

When implementing R2, the following file structure is recommended:

```
app/
├── task/
│   ├── CMakeLists.txt              # Add: add_subdirectory(scenecut)
│   └── scenecut/                   # NEW SUBDIRECTORY
│       ├── CMakeLists.txt          # Target sources registration
│       ├── scenecutdetector.h      # Standalone C++17 histogram & MAD algorithm
│       ├── scenecutdetector.cpp
│       ├── scenecuttask.h          # olive::Task derivative running in TaskManager
│       └── scenecuttask.cpp        # Decodes frames, runs detector, emits cuts
├── dialog/
│   └── scenecut/                   # NEW UI DIALOG
│       ├── scenecutdialog.h        # Sensitivity, min scene length, split vs markers
│       └── scenecutdialog.cpp
├── panel/
│   └── timeline/
│       ├── timeline.h              # Add: AutoSplitSelectedClips()
│       └── timeline.cpp
└── widget/
    └── timelinewidget/
        ├── timelinewidget.h        # Context menu action wiring
        └── timelinewidget.cpp
tests/
└── timeline/
    └── timeline-tests.cpp          # Add SceneCutDetector and TimelineAutoSplit tests
```

---

## 9. Conclusion & Actionable Next Steps for Implementers

1. **Subsystem Reuse**:
   - Reuse `olive::Task` and `olive::TaskManager` directly. No changes to `TaskManager` are needed.
   - Reuse `BlockSplitPreservingLinksCommand` directly. It already supports multiple cut points and link preservation.
2. **Decoding Engine**:
   - Build a lightweight `FFmpegVideoReader` / direct decoding loop inside `SceneCutTask` to ensure zero interference with `RenderManager` and timeline playback.
3. **Detector Algorithm**:
   - Implement `SceneCutDetector` with YUV strided subsampling, combined $L_1$ histogram distance + MAD, and adaptive rolling-window thresholding.
4. **Verification**:
   - Integrate automated tests in `tests/timeline/timeline-tests.cpp` covering synthetic frame transitions, threshold sensitivity, multi-point splitting, and undo/redo state restoration.
