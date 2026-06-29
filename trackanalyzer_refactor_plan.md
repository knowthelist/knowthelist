# TrackAnalyzer Refactoring Plan

## Unified Naming Convention & Entity Reference

All variables must clearly indicate what entity they refer to. Use the prefix `m_track` for anything about the loaded audio file; use `m_` only when it's unambiguous within class context (like gain/bpm state). The analyzer works on **two temporal scopes** — full track length vs. audible content boundary — this distinction must be explicit in every name:

```
Full track (audioBackend duration):  [---intro silence---|======audio======|---outro silence---]
                                     ↑                                   ↓
                              m_trackEffectiveStart        m_trackEffectiveEnd
                                                                    (may be earlier if fadeout ends before last sample)
                                                        ^^^^^^^^^^^^^^^^ content region
                                                         beat grid starts on first hit

m_beatPosition         = phase offset within detected period (0..period). Time of first beat RELATIVE to content start.
m_beatStopPosition   = when rhythmic flux drops below threshold + 3s silent window. Music has stopped, possibly during fadeout.
```

---

## Variable Naming Table

| Current name | Proposed name | Meaning | Why this name |
|---|---|---|---|
| `m_StartPosition` | **`m_trackEffectiveStart`** | First frame RMS > silence threshold (excludes intro) | "track" scopes to the loaded file; "effective" = actual content boundary, not physical start |
| `m_EndPosition` | **`m_trackEffectiveEnd`** | Last frame before trailing silence (excludes fadeout tail) | Mirrors start — both mark where the audible track actually lives |
| `m_bpm` (p->) / `m_ExactBpm` (m_) | KEEP as-is | BPM state | No renaming needed — gain and bpm are scalar state, not temporal scope |
| `m_BeatActivityEndPosition` | **`m_beatStopPosition`** | Beat flux drops below threshold + 3s silent window | When music has stopped per beat detection logic (differs from fadeout tail) |
| `m_ExactBpm` / p->bpm | KEEP as-is | BPM integer and fractional result | No renaming needed — scalar state, not temporal |
| `m_MaxPosition` | **`m_trackDuration`** | Full track length from audioBackend (all silence included) | "track" scopes to loaded file; "duration" is clearer than ambiguous "max position". This IS the total entity duration. |
| `m_FirstSignificantEnergyPosition` | **`m_beatStartPosition`** | First high-energy frame snapped to beat grid = first detected beat in content zone | It IS the physical start of the beat grid within the track — "first energy" was misleading; it's literally where the beat grid anchors |

### What every variable refers to:

```
Scope entity = m_track*       → loaded audio file (duration, start/end boundaries)
Scope entity = m_beat*       → detected beat grid within that track (phase, period, position)
Scope entity = gain/bpm state → analyzer scalar results (no scope prefix needed)
```

---

## 1. Dead Code Identification

### Fields to DELETE from TrackAnalyzer_Private (never read after set)

| Field | Set where | Status |
|---|---|---|
| `analysisStartPosition` | `asyncOpen()` → **never read again** | DELETE — dead duplicate of m_trackEffectiveStart |
| `analysisEndPosition` | `asyncOpen()` → **never read again** | DELETE — dead duplicate of m_trackEffectiveEnd |
| `analysisBeatActivityEndPosition` | `asyncOpen()` → **never read again** | DELETE — dead, mapped to m_beatStopPosition via analysis data |
| `envelope` (qVector) | `asyncOpen()` → **never read** | DELETE — m_envelope is the single live copy via amplitudeEnvelope() |
| `analysisGainDb` | `TrackAnalyzer_Private` struct field | DELETE — m_GainDB IS the live one. This "analysis" prefixed field just adds noise. |

### Fields that STAY (workspace) and why

| Field | Used where | Reason to keep |
|---|---|---|
| `spectralFlux` / `spectralFluxLow` / `spectralFluxTimes` | `detectTempo()` copies to locals | Intermediate onset detection workspace, never exported |
| `frameRms` | `detectTempo()`, `beatActivityEndPosition()` fallback | Per-frame RMS for threshold logic |
| `averageRms` | Same as above — beat detection threshold calculations | Overall RMS level for threshold comparisons |

---

## 2. p-> vs m_ Rule (no mixing)

```
NO MIXING — clear separation:

m_ fields = OUTPUT STATE (one source of truth)
    - Everything returned via public API or consumed by other classes (PlayerWidget, CueManager)
    - Set once in asyncOpen() or detectTempo(), read by callers
    - NO parallel copies into p-> from the same data source

p-> fields = WORKSPACE (scratch data)
    - Populated by asyncOpen() during load, consumed exclusively by detectTempo() / beatActivityEndPosition() logic
    - Transient intermediate state — never part of analyzer's output contract
    - Never returned from public getters
```

---

## 3. Header Changes (trackanalyzer.h)

### m_ member renames

| Before | After | Type | Comment |
|---|---|---|---|
| `m_StartPosition` | `m_trackEffectiveStart` (QTime) | effective content boundary — first frame above silence threshold |
| `m_EndPosition` | `m_trackEffectiveEnd` (QTime) | effective content boundary — last frame before trailing silence |
| `m_BeatActivityEndPosition` | `m_beatStopPosition` (QTime) | beat flux drops → rhythm stopped (may differ from fadeout tail) |
| `m_FirstSignificantEnergyPosition` | `m_beatStartPosition` (QTime) | first detected beat in content zone — energy threshold snapped to beat grid |
| `m_MaxPosition` | `m_trackDuration` (QTime) | full track length incl. ALL silence from audioBackend |

### Dead field removals from TrackAnalyzer_Private

```diff
  struct TrackAnalyzer_Private {
      QFutureWatcher<void> watcher;
      QMutex mutex;
      int bpm = 0;
      bool bpmDetected = false;
      bool tempoWindowStarted = false;
      int tempoScanDurationSeconds = 24;
      QTimer* tempoTimeout;
      bool finishQueued = false;
      bool shuttingDown = false;
      bool inProgress = false;
      modeType analysisMode = TrackAnalyzer::STANDARD;

-     QList<float> spectralFlux;
-     QList<float> spectralFluxLow;
-     QList<qint64> spectralFluxTimes;
-     QVector<float> envelope;         // dead — m_envelope is the live copy
+     // ── Intermediate workspace for detectTempo() ---
      QList<float> spectralFlux;
      QList<float> spectralFluxLow;
      QList<qint64> spectralFluxTimes;
-     QTime analysisStartPosition = QTime(0, 0);        // dead — delete
-     QTime analysisEndPosition = QTime(0, 0);           // dead — delete
-     QTime analysisBeatActivityEndPosition = QTime(0, 0); // dead — delete
+     QList<float> frameRms;
+     double averageRms = 0.0;

      double analysisGainDb = TrackAnalyzer::GAIN_INVALID; // dead — m_GainDB is the live one → delete this field
...
-     double averageRms = 0.0;
-     QList<float> frameRms;
      QUrl currentUrl;
  };
```

### Getter method signature updates

| Method | Returns | Becomes |
|---|---|---|
| `startPosition()` | `m_StartPosition` | `return m_trackEffectiveStart` |
| `endPosition()` | `m_EndPosition` | `return m_trackEffectiveEnd` |
| `beatActivityEndPosition()` | `m_BeatActivityEndPosition` + fallback logic | Uses `m_beatStopPosition` semantics; the complex RMS threshold fallback inside this method should be reviewed — is it still needed? |
| `beatPosition()` | `m_BeatPosition` | Unchanged (already correct) |
| `firstSignificantEnergyPosition()` | `m_FirstSignificantEnergyPosition` | `return m_beatStartPosition` |
| `length()` | `m_MaxPosition` | `return m_trackDuration` |

---

## 4. asyncOpen() Consolidation (trackanalyzer.cpp)

### Current state — broken across parallel writes and two logs:

```cpp
// Dead copy #1 — never read again
p->analysisStartPosition = analysis.startPosition;    // DELETE
p->analysisEndPosition = analysis.endPosition;         // DELETE
p->analysisBeatActivityEndPosition = analysis.beatActivityEndPosition;  // DELETE
p->envelope = analysis.envelope;                        // DELETE

// Log #1 — too early, before all data available
qDebug() << Q_FUNC_INFO << "duration from JUCE:" ...;  // DELETE

// Live copy (correct scope)
m_StartPosition = analysis.startPosition;              // RENAME → m_trackEffectiveStart
m_EndPosition = analysis.endPosition;                  // RENAME → m_trackEffectiveEnd
m_BeatActivityEndPosition = analysis.beatActivityEndPosition;  // RENAME → m_beatStopPosition

// More dead writes
p->envelope = analysis.envelope;                        // DELETE (m_envelope already has it)
```

### Desired state — single block, clear separation:

```cpp
QTime duration = audioBackend->getDuration();

// ── Load file + scan ──
if (!audioBackend->isLoaded()) {
    qWarning() << "Failed to load file for analysis:" << url;
    need_finish();
    return;
}

TrackAnalysisData analysis = scanAudioFileCached(url);

QTime analyzedDuration = QTime(0, 0).addMSecs(static_cast<int>(qRound(analysis.durationMs)));
if (analysis.durationMs > 0.0)
    duration = analyzedDuration;

// ── Write FINAL results → m_ fields (ONE source of truth) ──
{
    QMutexLocker locker(&p->mutex);

    m_trackEffectiveStart   = analysis.startPosition;
    m_trackEffectiveEnd     = analysis.endPosition;
    m_beatStopPosition      = analysis.beatActivityEndPosition;
    m_beatStartPosition     = QTime(0, 0);              // set later in detectTempo()
    m_trackDuration         = duration;                 // full duration from audioBackend/analysis

    m_envelope            = analysis.envelope;           // live array for amplitudeEnvelope() callers
    m_GainDB              = analysis.gainDb;

    // ── Intermediate workspace → p-> fields (detectTempo only) ──
    p->spectralFlux     = analysis.spectralFlux;
    p->spectralFluxLow  = analysis.spectralFluxLow;
    p->spectralFluxTimes = analysis.spectralFluxTimes;
    p->frameRms         = analysis.frameRms;
    p->averageRms       = analysis.averageRms;
}

// ── ONE log per operation, AFTER all data is available ──
qDebug() << "asyncOpen:" << url.fileName()
         << "effectiveStart=" << m_trackEffectiveStart
         << "effectiveEnd=" << m_trackEffectiveEnd
         << "beatStop=" << m_beatStopPosition
         << "trackDuration=" << duration << "gainDb=" << analysis.gainDb;
```

---

## 5. Other .cpp Changes

### detectTempo() updates

- `m_beatStartPosition` is computed in detectTempo() (currently in the section starting at line ~1125) — this IS where the first energy threshold snaps to beat grid. Rename all references.
- `m_BeatPosition` (= phase offset within period) is already set correctly near line 1094. No rename needed here (it's a distinct concept from `m_beatStartPosition`).
- Keep the final qDebug at line ~1220 — useful, unique output.

### Getter function updates

| Method | Line approx | Change |
|---|---|---|
| `startPosition()` | ~529 | Return `m_trackEffectiveStart` |
| `endPosition()` | ~534 | Return `m_trackEffectiveEnd` |
| `beatActivityEndPosition()` | ~539 | Update all internal refs to `m_beatStopPosition`; complex fallback logic may need review |
| `firstSignificantEnergyPosition()` | ~568 | Rename method to `beatStartPosition()` or keep name and return `m_beatStartPosition` |
| `length()` | ~612 | Return `m_trackDuration` |

### qDebug cleanup — delete, no exceptions:

| Location | Action |
|---|---|
| `scanAudioFile()` line ~208: `qDebug() << "Silence threshold (fixed RMS):"` | DELETE — useless noise, not actionable in any caller |
| `asyncOpen()` early debug about JUCE duration | DELETE — replaced by the consolidated log after all data is available |
| `need_finish()` bare `qDebug() << Q_FUNC_INFO;` | DELETE or guard with `kLogDebug` (carries zero context data, very frequent) |

---

## 6. Final State Data Map

```
┌───────────────────────────────── OUTPUT STATE (m_) ──────────────────────────────────────┐
│ m_trackEffectiveStart   → first frame > silence threshold (excludes intro silence)       │
│ m_trackEffectiveEnd     → last frame before trailing silence (excludes fadeout tail)      │
│ m_beatStopPosition      → beat flux drops below + 3s silent window (music has stopped)   │
│ m_beatStartPosition     → first detected beat in content zone (energy threshold on beat) │
│ m_trackDuration         → full track length incl. ALL silence from audioBackend           │
│ m_GainDB                → RMS gain compensation dB                                        │
│ m_envelope              → normalized per-frame envelope array                             │
└───────────────────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────── WORKSPACE (p->, consumed by detectTempo / beatActivityEndPosition) ───────────┐
│ spectralFlux / spectralFluxLow / spectralFluxTimes → onset detection features                        │
│ frameRms                                            → per-frame RMS for threshold comparison              │
│ averageRms                                          → overall RMS level for threshold calculations         │
└───────────────────────────────────────────────────────────────────────────────────────────────────────┘

Rules: m_ data set once, read by callers. p-> data written by asyncOpen, read only by internals. No parallel copies.
```
