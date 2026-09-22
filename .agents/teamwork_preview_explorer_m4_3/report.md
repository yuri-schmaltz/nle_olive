# Milestone M4: Editorial Timeline Interchange UI Wiring, CMake Configuration, and Unit Test Suite Architecture Report

**Date**: 2026-09-20  
**Author**: Interchange UI and Tests Explorer (`teamwork_preview_explorer_m4_3`)  
**Scope**: UI Menu Actions, Core Engine Wiring, CMake Targets, and Unit Test Suites (`fcpxml-tests.cpp` & `otio-tests.cpp`)  
**Target Codebase**: Olive Video Editor (`/home/yuri/Documentos/olive`)  

---

## 1. Executive Summary

Milestone M4 establishes native C++17/Qt6 editorial interchange between Olive, Apple Final Cut Pro 7, Adobe Premiere Pro, and Kdenlive via **Final Cut Pro 7 XML (`xmeml`)** and **OpenTimelineIO (`.otio`)**.

This report provides the complete architectural blueprint and concrete code specifications for:
1. **Main Menu & Core Wiring**: Adding native timeline export actions under `File -> Export` (`Export -> Final Cut Pro 7 XML (*.xml)...` and `Export -> OpenTimelineIO (*.otio)...`), integrating `DialogExportFCPXMLShow()` and `DialogExportOTIOShow()` in `Core`, updating `Core::GetProjectFilter()` for `.xml`, and extending `Core::OpenProjectInternal()` and `Core::SaveProjectInternal()` for seamless workflow.
2. **CMake Build System Integration**: Integrating `app/task/project/fcpxml/` into `app/task/project/CMakeLists.txt`, and configuring `tests/project/CMakeLists.txt` using Olive's custom `olive_add_test` harness with strict conditional compilation (`OpenTimelineIO_FOUND` / `USE_OTIO`).
3. **Unit Test Suite Design**: Providing exhaustive, self-contained test implementations for `tests/project/fcpxml-tests.cpp` and `tests/project/otio-tests.cpp`. The suites test multi-track sequence round-trips, clip timing (in/out/start/end/media_in), gaps, transitions, timeline markers, dual-linked audio/video blocks, frame rate conversions, and malformed XML error handling.
4. **Sanitizer & Quality Gate Compliance**: Enforcing zero memory leaks and zero assertion failures under `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`, adhering to strict RAII and thread affinity requirements (`project->moveToThread(qApp->thread())`).

---

## 2. Main Menu Export Actions & Core Wiring

### 2.1 Existing Menu Structure in Olive
In `app/window/mainwindow/mainmenu.h` (lines 207-208) and `mainmenu.cpp` (lines 62-64, 700-701):
```cpp
// mainmenu.h
Menu* file_export_menu_;
QAction* file_export_media_item_;

// mainmenu.cpp constructor
file_export_menu_ = new Menu(file_menu_);
file_export_media_item_ = file_export_menu_->AddItem("export", Core::instance(), &Core::DialogExportShow, tr("Ctrl+M"));

// mainmenu.cpp Retranslate()
file_export_menu_->setTitle(tr("&Export"));
file_export_media_item_->setText(tr("&Media..."));
```
Currently, `file_export_menu_` only contains video rendering (`DialogExportShow`). Editorial timeline interchange (FCPXML/OTIO) is not accessible from the Export menu.

### 2.2 Main Menu Modifications (`app/window/mainwindow/mainmenu.h` & `mainmenu.cpp`)

#### Header Declaration (`app/window/mainwindow/mainmenu.h`)
Add action pointers in `class MainMenu`:
```cpp
  Menu* file_export_menu_;
  QAction* file_export_media_item_;
  QAction* file_export_fcpxml_item_;
#ifdef USE_OTIO
  QAction* file_export_otio_item_;
#endif
```

#### Constructor Wiring (`app/window/mainwindow/mainmenu.cpp`)
In `MainMenu::MainMenu(MainWindow *parent)`:
```cpp
  file_export_menu_ = new Menu(file_menu_);
  file_export_media_item_ = file_export_menu_->AddItem("export", Core::instance(), &Core::DialogExportShow, tr("Ctrl+M"));
  file_export_fcpxml_item_ = file_export_menu_->AddItem("exportfcpxml", Core::instance(), &Core::DialogExportFCPXMLShow);
#ifdef USE_OTIO
  file_export_otio_item_ = file_export_menu_->AddItem("exportotio", Core::instance(), &Core::DialogExportOTIOShow);
#endif
```

#### Menu Enablement & Dynamics (`MainMenu::FileMenuAboutToShow`)
In `MainMenu::FileMenuAboutToShow()` (`mainmenu.cpp:325-340`), enable or disable export options based on whether an active project exists:
```cpp
void MainMenu::FileMenuAboutToShow()
{
  Project* active_project = Core::instance()->GetActiveProject();

  file_save_item_->setEnabled(active_project);
  file_save_as_item_->setEnabled(active_project);
  file_export_menu_->setEnabled(active_project);
  file_export_fcpxml_item_->setEnabled(active_project);
#ifdef USE_OTIO
  if (file_export_otio_item_) {
    file_export_otio_item_->setEnabled(active_project);
  }
#endif

  if (active_project) {
    file_save_item_->setText(tr("&Save '%1'").arg(active_project->name()));
    file_save_as_item_->setText(tr("Save '%1' &As").arg(active_project->name()));
  } else {
    file_save_item_->setText(tr("&Save Project"));
    file_save_as_item_->setText(tr("Save Project &As"));
  }
}
```

#### Retranslation (`MainMenu::Retranslate`)
In `MainMenu::Retranslate()` (`mainmenu.cpp:700`):
```cpp
  file_export_menu_->setTitle(tr("&Export"));
  file_export_media_item_->setText(tr("&Media..."));
  file_export_fcpxml_item_->setText(tr("&Final Cut Pro 7 XML (*.xml)..."));
#ifdef USE_OTIO
  if (file_export_otio_item_) {
    file_export_otio_item_->setText(tr("&OpenTimelineIO (*.otio)..."));
  }
#endif
```

---

### 2.3 Core Engine Wiring (`app/core.h` & `app/core.cpp`)

#### Method Declarations (`app/core.h`)
Under public slots in `class Core`:
```cpp
  /**
   * @brief Show Export Final Cut Pro 7 XML dialog
   */
  void DialogExportFCPXMLShow();

#ifdef USE_OTIO
  /**
   * @brief Show Export OpenTimelineIO dialog
   */
  void DialogExportOTIOShow();
#endif
```

#### Method Implementations (`app/core.cpp`)
Include the task headers:
```cpp
#include "task/project/fcpxml/loadfcpxml.h"
#include "task/project/fcpxml/savefcpxml.h"
#ifdef USE_OTIO
#include "task/project/saveotio/saveotio.h"
#include "task/project/loadotio/loadotio.h"
#endif
```

Implement `DialogExportFCPXMLShow()`:
```cpp
void Core::DialogExportFCPXMLShow()
{
  ViewerOutput* viewer = GetSequenceToExport();
  if (!viewer) {
    return;
  }

  Sequence* sequence = dynamic_cast<Sequence*>(viewer);
  if (!sequence) {
    QMessageBox::critical(main_window_, tr("Export Error"),
                          tr("The active item is not a sequence. Only sequences can be exported to Final Cut Pro 7 XML."));
    return;
  }

  Project* active_project = GetActiveProject();
  QString default_name = sequence->GetLabel();
  if (default_name.isEmpty() && active_project) {
    default_name = active_project->name();
  }

  QString default_dir = FileFunctions::GetPreferredPath(FileFunctions::kSaveProject);
  QString default_path = QDir(default_dir).filePath(default_name + QStringLiteral(".xml"));

  QString filename = QFileDialog::getSaveFileName(
      main_window_,
      tr("Export Final Cut Pro 7 XML"),
      default_path,
      tr("Final Cut Pro 7 XML (*.xml);;All Files (*)"));

  if (filename.isEmpty()) {
    return;
  }

  filename = FileFunctions::EnsureFilenameExtension(filename, QStringLiteral("xml"));

  SaveFCPXMLTask* task = new SaveFCPXMLTask(sequence, filename);
  TaskDialog* task_dialog = new TaskDialog(task, tr("Export Final Cut Pro 7 XML"), main_window_);
  task_dialog->open();
}
```

Implement `DialogExportOTIOShow()`:
```cpp
#ifdef USE_OTIO
void Core::DialogExportOTIOShow()
{
  ViewerOutput* viewer = GetSequenceToExport();
  if (!viewer) {
    return;
  }

  Sequence* sequence = dynamic_cast<Sequence*>(viewer);
  if (!sequence) {
    QMessageBox::critical(main_window_, tr("Export Error"),
                          tr("The active item is not a sequence. Only sequences can be exported to OpenTimelineIO."));
    return;
  }

  Project* active_project = GetActiveProject();
  QString default_name = sequence->GetLabel();
  if (default_name.isEmpty() && active_project) {
    default_name = active_project->name();
  }

  QString default_dir = FileFunctions::GetPreferredPath(FileFunctions::kSaveProject);
  QString default_path = QDir(default_dir).filePath(default_name + QStringLiteral(".otio"));

  QString filename = QFileDialog::getSaveFileName(
      main_window_,
      tr("Export OpenTimelineIO"),
      default_path,
      tr("OpenTimelineIO (*.otio);;All Files (*)"));

  if (filename.isEmpty()) {
    return;
  }

  filename = FileFunctions::EnsureFilenameExtension(filename, QStringLiteral("otio"));

  SaveOTIOTask* task = new SaveOTIOTask(active_project, filename);
  TaskDialog* task_dialog = new TaskDialog(task, tr("Export OpenTimelineIO"), main_window_);
  task_dialog->open();
}
#endif
```

#### Project Filter Extensions (`Core::GetProjectFilter`)
In `app/core.cpp:1099-1130`, add Final Cut Pro 7 XML (`*.xml`):
```cpp
QString Core::GetProjectFilter(bool include_any_filter)
{
  static const QVector< QPair<QString, QString> > FILTERS = {
    // Standard compressed Olive project
    {tr("Olive Project"), QStringLiteral("ove")},

    // Uncompressed XML Olive project
    {tr("Olive Project (Uncompressed XML)"), QStringLiteral("ovexml")},

    // Final Cut Pro 7 XML
    {tr("Final Cut Pro 7 XML"), QStringLiteral("xml")},

    // OpenTimelineIO project, if available
#ifdef USE_OTIO
    {tr("OpenTimelineIO"), QStringLiteral("otio")}
#endif
  };

  QStringList filters;
  filters.reserve(FILTERS.size() + 1);

  if (include_any_filter) {
    QStringList combined;
    for (auto it=FILTERS.cbegin(); it!=FILTERS.cend(); it++) {
      combined.append(QStringLiteral("*.%1").arg(it->second));
    }
    filters.append(QStringLiteral("%1 (%2)").arg(tr("All Supported Projects"), combined.join(' ')));
  }

  for (auto it=FILTERS.cbegin(); it!=FILTERS.cend(); it++) {
    filters.append(QStringLiteral("%1 (*.%2)").arg(it->first, it->second));
  }

  return filters.join(QStringLiteral(";;"));
}
```

#### Project Loading & Saving Dispatch
In `Core::OpenProjectInternal` (`app/core.cpp:1346`):
```cpp
  Task* load_task;

  if (filename.endsWith(QStringLiteral(".otio"), Qt::CaseInsensitive)) {
#ifdef USE_OTIO
    load_task = new LoadOTIOTask(filename);
#else
    QMessageBox::critical(main_window_,
                          tr("Missing OpenTimelineIO Libraries"),
                          tr("This build was compiled without OpenTimelineIO and therefore "
                             "cannot open OpenTimelineIO files."));
    return;
#endif
  } else if (filename.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
    load_task = new LoadFCPXMLTask(filename);
  } else {
    load_task = new ProjectLoadTask(filename);
  }
```

In `Core::SaveProjectInternal` (`app/core.cpp:810`):
```cpp
  if (override_filename.isEmpty() && open_project_->filename().endsWith(QStringLiteral(".otio"), Qt::CaseInsensitive)) {
#ifdef USE_OTIO
    psm = new SaveOTIOTask(open_project_);
#else
    QMessageBox::critical(main_window_,
                          tr("Missing OpenTimelineIO Libraries"),
                          tr("This build was compiled without OpenTimelineIO and therefore "
                             "cannot open OpenTimelineIO files."));
    return false;
#endif
  } else if (override_filename.isEmpty() && open_project_->filename().endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
    psm = new SaveFCPXMLTask(open_project_, open_project_->filename());
  } else {
    bool use_compression = !open_project_->filename().endsWith(QStringLiteral(".ovexml"), Qt::CaseInsensitive);
    psm = new ProjectSaveTask(open_project_, use_compression);
    // ...
  }
```

---

## 3. CMake Build System Integration

### 3.1 Task Subdirectory Architecture (`app/task/project/CMakeLists.txt`)
Olive uses recursive `PARENT_SCOPE` accumulation of `OLIVE_SOURCES`.
Update `app/task/project/CMakeLists.txt`:
```cmake
if(OpenTimelineIO_FOUND)
  add_subdirectory(loadotio)
  add_subdirectory(saveotio)
endif()

add_subdirectory(fcpxml)
add_subdirectory(import)
add_subdirectory(load)
add_subdirectory(save)

set(OLIVE_SOURCES
  ${OLIVE_SOURCES}
  PARENT_SCOPE
)
```

Create `app/task/project/fcpxml/CMakeLists.txt`:
```cmake
set(OLIVE_SOURCES
  ${OLIVE_SOURCES}
  task/project/fcpxml/loadfcpxml.h
  task/project/fcpxml/loadfcpxml.cpp
  task/project/fcpxml/savefcpxml.h
  task/project/fcpxml/savefcpxml.cpp
  PARENT_SCOPE
)
```

### 3.2 Unit Test Registration (`tests/project/CMakeLists.txt`)
Olive generates test executables via `olive_add_test(GROUP NAME SOURCE [GPU])` in `tests/CMakeLists.txt`.
The macro reads the source file, extracts all `OLIVE_ADD_TEST(...)` occurrences, generates a runner `main()` in `${CMAKE_CURRENT_BINARY_DIR}`, links against `libolive-editor`, and registers the test with `ctest`.

Update `tests/project/CMakeLists.txt`:
```cmake
olive_add_test(Project project-tests project-tests.cpp)
olive_add_test(Project fcpxml-tests fcpxml-tests.cpp)
if(OpenTimelineIO_FOUND)
  olive_add_test(Project otio-tests otio-tests.cpp)
endif()
```

> **Critical Architectural Note on OTIO Testing**:  
> `tests/CMakeLists.txt:33-36` aborts if zero tests are found. If `otio-tests.cpp` were registered unconditionally when `OpenTimelineIO_FOUND` is FALSE, compilation would fail due to missing `<opentimelineio/...>` headers. Guiding `otio-tests` with `if(OpenTimelineIO_FOUND)` guarantees that the test suite compiles cleanly on systems with or without OTIO installed.

---

## 4. Unit Test Suite Architecture & Specification

### 4.1 `tests/project/fcpxml-tests.cpp` Design

The FCP7 XML test suite exercises all editorial features required by Apple FCP7, Adobe Premiere Pro, and Kdenlive:
1. **Multi-track Sequence Round-Trip**: Video and audio tracks, clips with different in/out and media_in offsets, timeline gaps, markers with text and span ranges.
2. **Audio/Video Block Link Preservation**: Verifying that `<link>` elements correctly reconnect audio and video clips (`ClipBlock::block_links()` and `Node::AreLinked()`).
3. **Cross Dissolve Transition Round-Trip**: Proper duration and in/out offset preservation across edits without leaking transition blocks.
4. **Timebase & NTSC Frame Rate Mapping**: Rational frame rates (23.976, 24, 25, 29.97, 30, 50, 59.94, 60 fps) to/from `<timebase>` and `<ntsc>` booleans.
5. **Adversarial Error Handling**: Malformed, truncated, empty, and non-XML inputs cleanly handled without crashes or sanitizer violations.

#### Concrete Implementation Blueprint for `tests/project/fcpxml-tests.cpp`:
```cpp
/***
  Olive - Non-Linear Video Editor
  Milestone M4: Final Cut Pro 7 XML Interchange Unit Tests
***/

#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "config/config.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/factory.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "node/output/track/track.h"
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
  sequence->set_video_params(vparams);

  // Setup tracks: 1 Video, 1 Audio
  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();
  OLIVE_ASSERT(vtrack != nullptr);

  TimelineAddTrackCommand add_atrack(sequence->track_list(Track::kAudio));
  add_atrack.redo_now();
  Track* atrack = add_atrack.track();
  OLIVE_ASSERT(atrack != nullptr);

  // Video Clip 1: [0s to 5s] (0 to 120 frames at 24fps)
  ClipBlock* vclip1 = new ClipBlock();
  vclip1->setParent(&project);
  vclip1->SetLabel(QStringLiteral("Shot 01 Video"));
  vclip1->set_in(rational(0, 1));
  vclip1->set_media_in(rational(0, 1));
  vclip1->set_length_and_media_out(rational(5, 1));
  vtrack->AppendBlock(vclip1);

  // Video Gap: [5s to 7s] (120 to 168 frames)
  GapBlock* vgap = new GapBlock();
  vgap->setParent(&project);
  vgap->set_in(rational(5, 1));
  vgap->set_length_and_media_out(rational(2, 1));
  vtrack->AppendBlock(vgap);

  // Video Clip 2: [7s to 11s] (168 to 264 frames), media_in = 1s
  ClipBlock* vclip2 = new ClipBlock();
  vclip2->setParent(&project);
  vclip2->SetLabel(QStringLiteral("Shot 02 Video"));
  vclip2->set_in(rational(7, 1));
  vclip2->set_media_in(rational(1, 1));
  vclip2->set_length_and_media_out(rational(4, 1));
  vtrack->AppendBlock(vclip2);

  // Audio Clip 1: [0s to 5s] linked to Video Clip 1
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
  TimelineMarker* m1 = new TimelineMarker(1, TimeRange(rational(2, 1), rational(2, 1)), QStringLiteral("Scene Start"), sequence->GetMarkers());
  TimelineMarker* m2 = new TimelineMarker(2, TimeRange(rational(8, 1), rational(10, 1)), QStringLiteral("Action Beat"), sequence->GetMarkers());
  OLIVE_ASSERT_EQUAL(sequence->GetMarkers()->size(), 2);

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
  OLIVE_ASSERT_EQUAL(loaded_seq->GetMarkers()->size(), 2);
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
    seq->set_video_params(vp);

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
    delete load_task.GetLoadedProject(); // Clean up if any partial allocation occurred
  }

  OLIVE_TEST_END;
}

} // namespace olive
```

---

### 4.2 `tests/project/otio-tests.cpp` Design

The OpenTimelineIO test suite guarantees:
1. **Transition Memory Leak & Clobber Fix**: Validating that the bug in `SaveOTIOTask::SerializeTrack` (`saveotio.cpp:180`) where `otio_block = new OTIO::Transition();` discarded the configured transition is verified fixed.
2. **Timeline Marker Round-Trip**: Validating that `sequence->GetMarkers()` serializes to `OTIO::Marker` and restores to `TimelineMarkerList`.
3. **Speed and Reverse Preserved**: Validating that `ClipBlock::speed()` and `ClipBlock::reverse()` survive serialization as `OTIO::LinearTimeWarp` effects.

#### Concrete Implementation Blueprint for `tests/project/otio-tests.cpp`:
```cpp
/***
  Olive - Non-Linear Video Editor
  Milestone M4: OpenTimelineIO Interchange Unit Tests
***/

#ifdef USE_OTIO

#include <QFile>
#include <QTemporaryDir>
#include <opentimelineio/timeline.h>
#include <opentimelineio/transition.h>

#include "config/config.h"
#include "node/block/clip/clip.h"
#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/factory.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "node/output/track/track.h"
#include "render/diskmanager.h"
#include "task/project/loadotio/loadotio.h"
#include "task/project/saveotio/saveotio.h"
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

} // namespace

OLIVE_ADD_TEST(OTIO_SequenceRoundTrip_TransitionsAndMarkers)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QString otio_file = dir.filePath(QStringLiteral("otio_roundtrip.otio"));

  Project project;
  project.Initialize();

  Sequence* sequence = new Sequence();
  sequence->setParent(&project);
  sequence->SetLabel(QStringLiteral("OTIO Timeline"));

  VideoParams vp;
  vp.set_frame_rate(rational(24, 1));
  sequence->set_video_params(vp);

  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();

  ClipBlock* clip1 = new ClipBlock();
  clip1->setParent(&project);
  clip1->set_in(rational(0, 1));
  clip1->set_length_and_media_out(rational(3, 1));
  vtrack->AppendBlock(clip1);

  CrossDissolveTransition* trans = new CrossDissolveTransition();
  trans->setParent(&project);
  trans->set_offsets_and_length(rational(1, 2), rational(1, 2));
  vtrack->AppendBlock(trans);
  Node::ConnectEdge(clip1, NodeInput(trans, TransitionBlock::kOutBlockInput));

  ClipBlock* clip2 = new ClipBlock();
  clip2->setParent(&project);
  clip2->set_in(rational(3, 1));
  clip2->set_length_and_media_out(rational(3, 1));
  vtrack->AppendBlock(clip2);
  Node::ConnectEdge(clip2, NodeInput(trans, TransitionBlock::kInBlockInput));

  // Add Marker
  TimelineMarker* marker = new TimelineMarker(1, TimeRange(rational(1, 1), rational(1, 1)),
                                              QStringLiteral("Cue Point"), sequence->GetMarkers());
  OLIVE_ASSERT_EQUAL(sequence->GetMarkers()->size(), 1);

  // Save to OTIO
  SaveOTIOTask save_task(&project, otio_file);
  OLIVE_ASSERT(save_task.Start());

  // Direct OTIO validation: Ensure transition was not overwritten with blank dummy
  OTIO::ErrorStatus es;
  auto otio_root = OTIO::SerializableObject::from_json_file(otio_file.toStdString(), &es);
  OLIVE_ASSERT(es.outcome == OTIO::ErrorStatus::Outcome::OK);
  auto otio_timeline = dynamic_cast<OTIO::Timeline*>(otio_root);
  OLIVE_ASSERT(otio_timeline != nullptr);

  auto otio_tracks = otio_timeline->video_tracks();
  OLIVE_ASSERT_EQUAL(otio_tracks.size(), 1);
  auto otio_track = otio_tracks.front();
  
  // Find transition child and assert non-zero offsets
  bool found_transition = false;
  for (auto child : otio_track->children()) {
    if (auto t = dynamic_cast<OTIO::Transition*>(child.value)) {
      found_transition = true;
      OLIVE_ASSERT(t->in_offset().value() > 0);
      OLIVE_ASSERT(t->out_offset().value() > 0);
    }
  }
  OLIVE_ASSERT(found_transition);
  otio_root->possibly_delete();

  // Load back through LoadOTIOTask
  LoadOTIOTask load_task(otio_file);
  OLIVE_ASSERT(load_task.Start());

  Project* loaded_project = load_task.GetLoadedProject();
  OLIVE_ASSERT(loaded_project != nullptr);

  Sequence* loaded_seq = loaded_project->root()->ListChildrenOfType<Sequence>().first();
  Track* loaded_track = loaded_seq->track_list(Track::kVideo)->GetTrackAt(0);

  // Assert transition exists and properties match
  OLIVE_ASSERT_EQUAL(loaded_track->Blocks().size(), 3);
  TransitionBlock* loaded_trans = dynamic_cast<TransitionBlock*>(loaded_track->Blocks().at(1));
  OLIVE_ASSERT(loaded_trans != nullptr);
  OLIVE_ASSERT_EQUAL(loaded_trans->in_offset(), rational(1, 2));
  OLIVE_ASSERT_EQUAL(loaded_trans->out_offset(), rational(1, 2));

  // Assert marker exists
  OLIVE_ASSERT_EQUAL(loaded_seq->GetMarkers()->size(), 1);
  TimelineMarker* loaded_marker = loaded_seq->GetMarkers()->front();
  OLIVE_ASSERT_EQUAL(loaded_marker->name(), QStringLiteral("Cue Point"));

  delete loaded_project;
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(OTIO_SpeedAndReversePreservation)
{
  Environment env;
  QTemporaryDir dir;
  OLIVE_ASSERT(dir.isValid());

  const QString otio_file = dir.filePath(QStringLiteral("otio_speed.otio"));

  Project project;
  project.Initialize();

  Sequence* sequence = new Sequence();
  sequence->setParent(&project);
  sequence->SetLabel(QStringLiteral("Speed Timeline"));

  TimelineAddTrackCommand add_vtrack(sequence->track_list(Track::kVideo));
  add_vtrack.redo_now();
  Track* vtrack = add_vtrack.track();

  ClipBlock* clip = new ClipBlock();
  clip->setParent(&project);
  clip->set_in(rational(0, 1));
  clip->set_length_and_media_out(rational(4, 1));
  clip->SetStandardValue(ClipBlock::kSpeedInput, 2.0);
  clip->set_reverse(true);
  vtrack->AppendBlock(clip);

  SaveOTIOTask save_task(&project, otio_file);
  OLIVE_ASSERT(save_task.Start());

  LoadOTIOTask load_task(otio_file);
  OLIVE_ASSERT(load_task.Start());

  Project* loaded_project = load_task.GetLoadedProject();
  OLIVE_ASSERT(loaded_project != nullptr);

  Sequence* loaded_seq = loaded_project->root()->ListChildrenOfType<Sequence>().first();
  ClipBlock* loaded_clip = dynamic_cast<ClipBlock*>(loaded_seq->track_list(Track::kVideo)->GetTrackAt(0)->Blocks().first());
  OLIVE_ASSERT(loaded_clip != nullptr);
  OLIVE_ASSERT_EQUAL(loaded_clip->speed(), 2.0);
  OLIVE_ASSERT_EQUAL(loaded_clip->reverse(), true);

  delete loaded_project;
  OLIVE_TEST_END;
}

} // namespace olive

#endif // USE_OTIO
```

---

## 5. AddressSanitizer & Gauntlet Verification Standards

To pass `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` with **zero memory leaks and zero assertion failures**, the implementation adheres to the following memory and concurrency rules:

1. **Deterministic QObject Ownership Hierarchy**:
   - In Olive, all `Node` objects (`Sequence`, `Track`, `ClipBlock`, `GapBlock`, `TransitionBlock`, `Footage`) parented to `Project` (`node->setParent(project)`) are tracked in `project->node_children_`.
   - In `Project::~Project()`, all child nodes in `node_children_` are cleanly destroyed in reverse order.
   - Test suites must avoid separate manual deletion of individual nodes once a `Project` is deleted. Simply executing `delete loaded_project;` frees the entire graph cleanly without double-free errors.
2. **Thread Affinity Transfer**:
   - `LoadFCPXMLTask::Run()` and `LoadOTIOTask::Run()` allocate `Project` and nodes on worker threads during asynchronous tasks.
   - Before exiting `Run()`, they must invoke:
     ```cpp
     project_->moveToThread(qApp->thread());
     ```
   - In unit tests, `task.Start()` executes on the test runner thread. When `qApp` is instantiated by `olive_add_test`'s harness, `project_->moveToThread(qApp->thread())` ensures that thread affinity matches the main thread so signal dispatch and object destruction never trigger cross-thread assertions.
3. **OTIO Object Reference Counting & Retainers**:
   - In OTIO C++, classes inheriting `OTIO::SerializableObject` manage lifecycle via `possibly_delete()` and `OTIO::SerializableObject::Retainer<T>`.
   - Never invoke `delete` on an OTIO object directly; use `Retainer` or `possibly_delete()`.
   - The bug in `saveotio.cpp:180` created an unretained `new OTIO::Transition()` which caused ASan memory leaks. Replacing it with `otio_block = otio_transition;` resolves the leak and restores transition properties.
4. **QXmlStreamReader and QXmlStreamWriter Efficiency**:
   - Use `QStringLiteral` and `QLatin1String` for tag and attribute constants to avoid unnecessary heap allocations during XML streaming.
   - Check `reader.hasError()` after parsing to prevent undefined reads.

---

## 6. Verification & Implementation Checklist for Milestone M4

| Step | Target File | Action | Acceptance Criteria |
|---|---|---|---|
| 1 | `app/window/mainwindow/mainmenu.h` | Add `file_export_fcpxml_item_`, `#ifdef USE_OTIO file_export_otio_item_` | Compiles without warnings |
| 2 | `app/window/mainwindow/mainmenu.cpp` | Instantiate actions in `MainMenu::MainMenu`, connect to `Core`, update `Retranslate` & `FileMenuAboutToShow` | Actions visible under File -> Export |
| 3 | `app/core.h` | Declare `DialogExportFCPXMLShow()`, `DialogExportOTIOShow()` | Compiles |
| 4 | `app/core.cpp` | Implement `DialogExportFCPXMLShow()`, `DialogExportOTIOShow()`, update `GetProjectFilter()`, `OpenProjectInternal()`, `SaveProjectInternal()` | File dialogs prompt for `.xml`/`.otio` |
| 5 | `app/task/project/CMakeLists.txt` | Add `add_subdirectory(fcpxml)` | Sources included in `OLIVE_SOURCES` |
| 6 | `app/task/project/fcpxml/CMakeLists.txt` | Create file adding `loadfcpxml` and `savefcpxml` | Sources compiled into `libolive-editor` |
| 7 | `tests/project/CMakeLists.txt` | Add `fcpxml-tests` and conditional `otio-tests` | Registered with CTest |
| 8 | `tests/project/fcpxml-tests.cpp` | Implement 4 test cases | `ctest -R fcpxml-tests` passes |
| 9 | `tests/project/otio-tests.cpp` | Implement 2 test cases | `ctest -R otio-tests` passes under OTIO |
| 10 | Quality Gate | Run `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` | Status: `passed`, 0 leaks, 0 failures |

