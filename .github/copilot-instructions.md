# Copilot instructions for knowthelist

## Build, run, and validation

The current build of the application is CMake-based and is the preferred workflow. It requires Qt 6, TagLib, JUCE, and the platform audio dependencies documented in `README.md`.

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build --parallel 1

# Run on Linux
./build/knowthelist

# Run on macOS
open build/knowthelist.app
```

The CMake configuration fetches JUCE with `FetchContent` when it is not installed locally. The dependency cache is outside the build directory by default; use `-DKNOWTHELIST_FETCHCONTENT_DIR=/path/to/cache` to override it. The project intentionally limits build parallelism because JUCE has had parallel-compilation issues.

There is also a qmake path:

```bash
qmake6 knowthelist.pro
make -j"$(sysctl -n hw.ncpu)"   # macOS
```

Use CMake for current development: `CMakeLists.txt` contains the current JUCE audio backend, monitor backend, analysis cache, and other newer sources, while `src/src.pro` is an older/secondary project definition. On Debian packaging, `debian/rules` invokes the CMake build in Release mode.

No project-owned automated test suite or configured linter was found. There is consequently no single-test command; validate source changes with the smallest relevant CMake rebuild and manual application behavior when the change affects audio or UI.

## Architecture

- `src/main.cpp` bootstraps Qt and JUCE, installs the filtered Qt message handler, loads bundled fonts and translations, initializes the named `CollectionDB` SQLite connection, verifies/repairs the database, and starts the `Knowthelist` main window.
- `Knowthelist` (`src/knowthelist.*`) is the top-level controller/window. It composes the two deck widgets, playlists, collection browser, monitor player, DJ session, playlist browser, settings dialog, meters, and crossfader/auto-fade/beat-sync timers. Most application-wide signal wiring and deck coordination belongs here.
- Each deck is a `PlayerWidget` containing a `Player` and `TrackAnalyzer`. `Player` is the Qt-facing asynchronous playback API; `JuceAudioBackend` implements playback, tempo changes, EQ/gain, output metering, and optional monitor routing using JUCE. `PlayerWidget` owns controls, cue points, waveform/envelope display, BPM/beat state, and sync behavior.
- `CollectionDB` stores the music collection and playlist/statistics data in SQLite under Qt's `AppDataLocation`. `CollectionUpdater` scans configured folders asynchronously and updates the database; `CollectionWidget`/`CollectionTree` expose searching, filtering, and selection to the main window.
- `Track` is the metadata/value object backed by TagLib. `Playlist` and `PlaylistItem` own the visible queue and XML playlist persistence. `Dj`, `Filter`, and `DjSession` select tracks and maintain Auto-DJ state, then communicate playlist changes through Qt signals.
- `TrackAnalyzer` performs gain, BPM, beat-phase, cue-point, and amplitude-envelope analysis asynchronously. `AnalysisCacheManager` persists tempo/envelope results in the same SQLite database and uses cache keys/epochs to avoid stale writes after cache resets.
- `.ui` files, `.qrc` resource files, translation `.ts` files, and bundled fonts/images are source assets. CMake's AUTOUIC/AUTOMOC/AUTORCC generate the corresponding `ui_*.h`, `moc_*`, and `qrc_*` files; generated files must not be edited manually.

## Repository-specific conventions

- Use Qt's parent ownership model for `QObject`/widget lifetimes and communicate between asynchronous components with signals/slots. File scanning, audio loading, track analysis, and DJ searches use `QtConcurrent`; do not update widgets directly from worker code.
- Preserve the separation between UI/controller classes and backend services: deck UI behavior belongs in `PlayerWidget`, playback/device behavior in `Player`/`JuceAudioBackend`, and collection persistence/scanning in `CollectionDB`/`CollectionUpdater`.
- Keep audio-callback code real-time safe. `JuceAudioBackend` uses atomics, JUCE buffers, and a lock-free monitor FIFO; avoid allocations, blocking locks, Qt UI calls, or database work in the JUCE device callback.
- Treat `QSettings` as the source for user preferences and deck/UI state, and preserve the existing organization/application identity configured in `main.cpp`. Collection data and analysis cache belong in the application SQLite database, not ad hoc files in the repository.
- Maintain the existing signal/slot naming convention, including Qt Designer auto-connect handlers such as `on_<objectName>_<signal>()`. Changes to widget object names or `.ui` files can silently break those connections.
- Use `QTime`/millisecond conversions consistently for playback positions, cue points, beat positions, and durations. Tempo synchronization also accounts for output latency and the separate monitor route, so changes to timing logic should be reviewed across `Player`, `PlayerWidget`, and `Knowthelist`.
- Keep translations in `locale/*.ts` and resources in the corresponding `.qrc` files. Do not replace bundled font/resource loading with system-only paths.
- Project source files use C++17 and the existing Qt style. Preserve the LGPL headers already present in source files when modifying them.
