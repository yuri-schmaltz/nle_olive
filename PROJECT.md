# Project: Olive Video Editor Modernization & Parity Suite

> Estado de integração: o inventário abaixo descreve requisitos e contratos do
> projeto, não uma lista de recursos concluídos. Mixer/VU e backend de IA ainda
> estão pendentes. OTIO depende de biblioteca opcional; packaging tem receitas e
> testes estáticos, sem validação universal dos pacotes. Consulte TEST_INFRA.md
> para a cobertura executável atual; os antigos 130 casos não comprovam todos
> esses requisitos.

## Architecture
Olive Video Editor is a non-linear video editor written in native C++17 and Qt6, featuring a directed acyclic graph (DAG) node-based rendering architecture.
- **Node & Audio Pipeline**: Audio streams flow through planar 32-bit floating point buffers (`olive::core::SampleBuffer`) within audio nodes (`Node::Value()`). Tracks aggregate clips and apply volume/pan transforms before summing into sequence outputs.
- **Task & Concurrency System**: `TaskManager` handles asynchronous operations through a thread pool executing `olive::Task` instances. Worker threads run background tasks (such as frame decoding and scene cut analysis) without contending with the GPU `RenderManager` or audio playback engine.
- **Interchange Subsystem**: Projects and sequences serialize to/from XML. Native FCP7 XML (`xmeml`) and OpenTimelineIO (`.otio`) provide timeline interchange within the implemented format subset with third-party NLEs (Premiere, Kdenlive).
- **Packaging & CI/CD**: Reproducible AppImage generation and Flatpak recipes package Olive and its dependencies (FFmpeg, OCIO, OIIO, Qt6) into isolated, portable Linux bundles.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Parametric Equalizer Node | Multi-band biquad IIR filter node (Robert Bristow-Johnson) in audio node library | M1 | R1 (survey_audio.md) |
| 2 | Track Audio Controls | Native volume (`kVolumeInput`), pan (`kPanInput`), and solo (`kSoloInput`) on `Track` | M1 | R1 (survey_audio.md) |
| 3 | Track Audio Mixer Panel | Dockable Qt6 `AudioMixerPanel` with faders, pan dials, mute/solo buttons, and VU meters | M2 | R1 (survey_audio.md) |
| 4 | Thread-Safe VU Metering | Real-time lock-free level calculation (`std::atomic<float>`) and ballistics decay timer | M2 | R1 (survey_audio.md) |
| 5 | Asynchronous Scene Cut Analysis | CPU FFmpeg frame decoding and YUV histogram/$L_1$ difference analysis in `SceneCutTask` | M3 | R2 (survey_scenecut.md) |
| 6 | Timeline Auto-Split | GUI action triggering `BlockSplitPreservingLinksCommand` with undo/redo | M3 | R2 (survey_scenecut.md) |
| 7 | Final Cut Pro 7 XML Interchange | Native `LoadFCPXMLTask` & `SaveFCPXMLTask` (sequences, tracks, clips, in/out, links) | M4 | R3 (survey_interchange_packaging.md) |
| 8 | OpenTimelineIO Hardening | Fix transition clobber/leak in `saveotio.cpp:180`, add markers and speed serialization | M4 | R3 (survey_interchange_packaging.md) |
| 9 | Main Menu Export Wiring | Add FCP7 XML and OTIO export actions to `MainMenu::file_export_menu_` | M4 | R3 (survey_interchange_packaging.md) |
| 10 | Linux AppImage Packaging | Reproducible bundling script and modernized `AppRun` with Qt6 plugins & library paths | M5 | R4 (survey_interchange_packaging.md) |
| 11 | Linux Flatpak Packaging | Complete Flatpak manifest (`org.olivevideoeditor.Olive.json`) on KDE 6.8+ runtime | M5 | R4 (survey_interchange_packaging.md) |
| 12 | Gauntlet ASan & CTest Gate | 100% pass on `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` with 0 leaks/asserts | M6 | Acceptance Criteria |

## Milestones
| # | Name | Scope | Dependencies | Status | Sub-Orch Conv ID |
|---|------|-------|-------------|--------|------------------|
| E2E | E2E Testing Track | Requirement-driven opaque-box test suite (Tiers 1-4) & TEST_READY.md | None | IN_PROGRESS | 84402d74-b7ee-4aee-b377-b220077c8306 |
| M1 | Audio Engine & Parametric EQ Node | Parametric EQ biquad filter node, Track volume/pan/solo inputs, unit tests | None | IN_PROGRESS | e57a6951-109c-4b82-af00-11dc1bf661c2 |
| M2 | Track Audio Mixer UI & VU Metering | AudioMixerPanel dock widget, track strips, master fader, thread-safe VU meters | M1 | PENDING | not yet spawned |
| M3 | Async Scene Cut Detection & Auto-Split | SceneCutDetector, SceneCutTask in TaskManager, Timeline auto-split command, tests | None | IN_PROGRESS | 9582691c-390f-49e1-8b46-cb743a86ad5d |
| M4 | Editorial Timeline Interchange (FCPXML/OTIO) | FCP7 XML importer/exporter, OTIO bugfixes & markers, main menu actions, tests | None | IN_PROGRESS | 1f1c3fe7-601f-4066-a5db-4f3593b3394d |
| M5 | Linux Packaging Automation | Reproducible AppImage packaging script & AppRun, Flatpak manifest | None | IN_PROGRESS | d8291db2-3b3d-41ad-a12d-27615886bd25 |
| M6 | Final Verification & Adversarial Hardening | E2E test suite pass (Tiers 1-4), Tier 5 adversarial tests, Gauntlet ASan verification | M1, M2, M3, M4, M5, E2E | PENDING | not yet spawned |

## Interface Contracts
### EqualizerNode ↔ Track Audio Pipeline
- `EqualizerNode` inherits `Node`, implements `Value()` and `SampleJob` generation.
- Operates on `olive::core::SampleBuffer` in 32-bit floating point planar format.
- Inputs: `kSamplesInput` ("samples_in"), `kBandsInput` ("bands_in"), `kEnabledInput` ("enabled_in").
- Frequency bands support: Low Shelf, Peaking Bell (multiple), High Shelf, Low Pass, High Pass.

### Track Audio Controls ↔ AudioMixerPanel
- `Track` provides inputs:
  - `kVolumeInput` ("volume_in", float, default 1.0, logarithmic/dB display)
  - `kPanInput` ("pan_in", float, default 0.0, range -1.0 to 1.0)
  - `kSoloInput` ("solo_in", bool, default false)
- `AudioMixerPanel` observes sequence track changes and connects slider/dial signals to `Track` property modifications via `UndoStack`.
- Level metering: Master levels read from atomic registers updated by audio output callback; per-track levels query `track->waveform_cache()` at playback position.

### SceneCutTask ↔ Timeline Auto-Split
- `SceneCutTask` inherits `olive::Task`, runs on `TaskManager` thread pool.
- Decodes video frames via dedicated CPU `AVFormatContext` / `AVCodecContext` without touching `RenderManager` or `DecoderCache`.
- Emits signal `SceneCutCompleted(QVector<rational> cut_times)` queued to GUI thread.
- Timeline controller converts media times to sequence times:
  $T_{seq} = \text{clip->in()} + \text{clip->MediaToSequenceTime}(T_{media})$
  and pushes `BlockSplitPreservingLinksCommand` to `Core::instance()->undo_stack()`.

### FCP7 XML & OTIO ↔ Core Project Model
- `LoadFCPXMLTask` & `SaveFCPXMLTask` inherit `olive::Task`.
- Uses Qt `QXmlStreamReader` and `QXmlStreamWriter` with zero external dependencies.
- Dual `<link>` elements maintain video and audio block association.
- Frame rates map between rational FPS and integer `<timebase>` + `<ntsc>` flag.
- Safe thread transfer: `project->moveToThread(qApp->thread())` upon completion.

## Code Layout
- Audio Nodes: `app/node/audio/equalizer/` (`equalizer.h`, `equalizer.cpp`)
- Track Engine: `app/node/output/track/` (`track.h`, `track.cpp`)
- Audio Mixer UI: `app/panel/audiomixer/` (`audiomixerpanel.h`, `audiomixerpanel.cpp`), `app/widget/audiomixer/`
- Scene Cut Task: `app/task/scenecut/` (`scenecuttask.h`, `scenecuttask.cpp`, `scenecutdetector.h`, `scenecutdetector.cpp`)
- Scene Cut UI: `app/widget/timelinewidget/` or `app/dialog/scenecut/` (`scenecutdialog.h`, `scenecutdialog.cpp`)
- FCPXML Interchange: `app/task/project/fcpxml/` (`loadfcpxml.h`, `.cpp`, `savefcpxml.h`, `.cpp`)
- OTIO Fixes: `app/task/project/loadotio/`, `app/task/project/saveotio/`
- Packaging: `packaging/linux/build_appimage.sh`, `app/packaging/linux/AppRun`, `packaging/flatpak/org.olivevideoeditor.Olive.json`
- Tests: `tests/node/`, `tests/timeline/`, `tests/task/`, `tests/project/`
