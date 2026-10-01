# Mixxx 2.4 Video Backport Progress

Date: 2026-09-19

## Source trees

- Video fork: `C:\Users\HP\Downloads\mixxxxx-video`
- 2.4.2 working tree: `C:\Users\HP\Downloads\mixxx-2.4.2\mixxx-2.4.2`
- Original video archive: `C:\Users\HP\Downloads\mixxxxx-video.zip`
- 2.4.2 source archive: `C:\Users\HP\Downloads\mixxx-2.4.2-source.zip`

## Completed

Added the first minimal video slice to the 2.4.2 tree:

- `src/video/videodecoder.h/.cpp`
  - Qt 5 `QThread` decoder wrapper
  - FFmpeg video stream discovery and frame conversion
  - Emits `QImage` frames
  - Companion file formats: MP4, MKV, MOV, WebM through the widget
- `src/video/videowidget.h/.cpp`
  - Legacy QWidget
  - Aspect-ratio-preserving QPainter rendering
  - Loads a same-basename companion video when a track loads
- `src/skin/legacy/legacyskinparser.h/.cpp`
  - Registers and parses `<VideoWidget>`
  - Connects it to the deck's `newTrackLoaded` signal
- `CMakeLists.txt`
  - Registers the two new video source files

## Validation

- VS Code error check: no errors reported in the edited files.
- Full build/configure: not run. This Windows environment does not have CMake, Qt, a C++ compiler, or FFmpeg development libraries.

## Important limitations

This is only the core v1 slice. The following fork features are not yet ported:

- VideoMixer/crossfader compositing
- Video VFX controls
- Fullscreen projector output
- Fallback visuals and video pool
- NDI
- OSC video controls
- Phase indicator and export features
- MixxxxxVideo skin wiring

The current source also needs a real Qt 5 + FFmpeg build on the target Mac. FFmpeg API compatibility should be checked against the installed FFmpeg version.

## Next steps on the Mac

1. Open the 2.4.2 working tree.
2. Install the Mixxx 2.4 build dependencies, Qt 5, FFmpeg development libraries, CMake, and Ninja.
3. Configure with Qt 5, FFmpeg enabled, and the desired Intel macOS deployment target.
4. Build and fix any FFmpeg API or CMake option differences.
5. Copy `res/skins/MixxxxxVideo` from the video fork or add `<VideoWidget>` to a legacy skin.
6. Test a track with a same-basename `.mp4` companion file.
7. Add VideoMixer and fullscreen output only after the basic widget works.
