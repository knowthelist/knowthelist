# PlayerWidget Refactoring Plan - STATUS UPDATE

**Last updated:** 2026-06-25

## Current State Analysis

### Phase 1: Transport Controller
| Item | Status | Notes |
|------|--------|-------|
| `playerwidget_transportcontroller.h` | ✅ Created | Header exists |
| `playerwidget_transportcontroller.cpp` | ✅ Created (146 lines) | Implementation done |
| Added to build (src.pro) | ❌ **NOT ADDED** | Missing from src.pro — will not compile! |
| PlayerWidget delegates to it? | ❌ No | Still monolithic in PlayerWidget |

### Phase 2: Cue Manager
| Item | Status | Notes |
|------|--------|-------|
| `playerwidget_cue.h` | ✅ Created | Header exists with `PlayerCueHelpers` namespace |
| `playerwidget_cue.cpp` | ⚠️ Placeholder only (20 lines) | Comment says "implementations remain inline in playerwidget.cpp" — **Logic NOT moved out** |
| PlayerWidget uses it? | ❌ No | Cue logic remains directly in PlayerWidget. Free helper friends (`pComputeCuePts`, etc.) defined in playerwidget.cpp instead of using `PlayerCueHelpers` namespace |

### Phase 3: Performance Controls
| Item | Status | Notes |
|------|--------|-------|
| `playerwidget_performancecontrols.h` | ✅ Created | Header exists |
| `playerwidget_performancecontrols.cpp` | ✅ Created (329 lines) | Full implementation — beat jump, monitor route, pitch slider, BPM sync, etc. |
| PlayerWidget delegates to it? | ⚠️ Partial | Forward-declared but sub-object not instantiated |

### Phase 4: Track Loader
| Item | Status | Notes |
|------|--------|-------|
| `playerwidget_trackloader.h` | ✅ Created | Header exists |
| `playerwidget_trackloader.cpp` | ✅ Created (152 lines) | Full implementation — loadTrack, applyCuePoints, setPositionMarkers helpers |
| PlayerWidget delegates to it? | ❌ No | Still monolithic in PlayerWidget. TrackLoader never instantiated |

### Phase 5: Integration (delegate PlayerWidget methods to sub-objects)
| Item | Status | Notes |
|------|--------|-------|
| Transport delegation | ❌ NOT DONE | `play()`/`pause()`/`stop()` still in PlayerWidget directly |
| Cue computation delegation | ❌ NOT DONE | Cue logic moved but stays as inline playerwidget.cpp helpers — not using `PlayerCueHelpers` |
| Performance controls delegation | ❌ NOT DONE | Logic still in PlayerWidget |
| Track loader delegation | ❌ NOT DONE | Load/apply logic still in PlayerWidget |

## What Was Done (✅ Complete)

1. **All 4 header files created** — TransportController, CueManager, PerformanceControls, TrackLoader interfaces designed
2. **3 of 4 .cpp implementations done**:
   - `playerwidget_transportcontroller.cpp` (146 lines) ✅
   - `playerwidget_performancecontrols.cpp` (329 lines) ✅
   - `playerwidget_trackloader.cpp` (152 lines) ✅
3. **Cue logic simplified** in PlayerWidget — unified `computeCuePoints(CueMode)` replaces old dual-path, inline `computeFadePoint()` / `computeRemainCueTime()` extracted as internal helpers
4. **PlayerWidget header refactored** — added forward-decls, CueMode enum, CuePoints struct, friend helper functions, accessors for sub-objects
5. **`playerwidget_cue.cpp` exists but marked as placeholder** — cue logic intentionally kept inline in PlayerWidget rather than moved to separate file

## What Remains (❌ Not Done)

### Blocker: Build Integration
1. **Add `playerwidget_transportcontroller.cpp` to src.pro** — it exists but is NOT in the build, so it will never compile/ship
2. Optionally add remaining sub-component .cpp files if they get added later

### Major: PlayerWidget Integration (Phase 5)
3. **Instantiate sub-object members** — PlayerWidget needs `m_transport`, `m_performanceControls`, `m_trackLoader` member variables and init them in constructor
4. **Delegate transport operations** — `play()`, `pause()`, `stop()` → forward to TransportController
5. **Delegate cue computation** — Replace inline helpers with calls to CueManager/CueHelpers namespace  
6. **Delegate performance control UI** — Move button/slider creation to PerformanceControls
7. **Delegate track loading** — Replace `loadTrack()` body in PlayerWidget with TrackLoader call

### Minor: Cleanup
8. Remove `playerwidget_cue.cpp` placeholder (or fill it) — currently a no-op that confuses the plan intent
9. Verify build works after all integration
10. Functional testing of play/pause/stop, cue, beat jump, monitor route, pitch slider, track loading

## Files to Create / Update

### Must Add to Build
- Update `src/src.pro` — add `playerwidget_transportcontroller.cpp`

### Remaining Work
- PlayerWidget integration (Phase 5) — the sub-object classes are written but **not wired up**. This is the bulk of remaining work.
