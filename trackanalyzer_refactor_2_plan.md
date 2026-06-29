# TrackAnalyzer External Naming Refactor Plan #2

## Goal

Rename `TrackAnalyzer::firstSignificantEnergyPosition()` → `beatStartPosition()` to align all external naming with the unified domain convention (`m_beatStartPosition` internal field). This eliminates the "energy" vs "beat" semantic mismatch throughout the codebase.

---

## Scope: 14 lines across 3 files

### Declarations (2 locations)
| File | Line | Before | After |
|---|---|---|---|
| `src/trackanalyzer.h` | 46 | `QTime firstSignificantEnergyPosition();` | `QTime beatStartPosition();` |
| `src/trackanalyzer.cpp` | ~563 | `QTime TrackAnalyzer::firstSignificantEnergyPosition()` | `QTime TrackAnalyzer::beatStartPosition()` |

### Callers — `playercuemanager.cpp` (3 locations)
| File | Line | Before | After |
|---|---|---|---|
| `src/playercuemanager.cpp` | 73 | `m_owner.trackanalyzer->firstSignificantEnergyPosition()` | `m_owner.trackanalyzer->beatStartPosition()` |
| `src/playercuemanager.cpp` | 82 | `m_owner.trackanalyzer->firstSignificantEnergyPosition()` | `m_owner.trackanalyzer->beatStartPosition()` |
| `src/playercuemanager.cpp` | 102 | `QTime firstEnergy = m_owner.trackanalyzer->firstSignificantEnergyPosition();` | `QTime firstBeat = m_owner.trackanalyzer->beatStartPosition();` (and local var rename to `m_firstBeatValid`) |

### Callers — `playerwidget.cpp` (2 call expressions, each appearing twice)
| File | Lines | Before | After |
|---|---|---|---|
| `src/playerwidget.cpp` | 274-275 | `trackanalyzer->firstSignificantEnergyPosition()` | `trackanalyzer->beatStartPosition()` |
| `src/playerwidget.cpp` | 1157-1159 | `trackanalyzer->firstSignificantEnergyPosition()` | `trackanalyzer->beatStartPosition()` |

---

## Implementation Steps

```
Step 1: Update header declaration        → src/trackanalyzer.h line 46
Step 2: Update implementation definition → src/trackanalyzer.cpp ~line 563
Step 3: Update all callers               → playercuemanager.cpp (3), playerwidget.cpp (2 expressions)
Step 4: Verify no remaining references   → grep for old name must find 0 matches
Step 5: Build and fix any issues         → project compile
```

## Verification Checklist

```bash
# After refactor completes, confirm:
grep -rn "firstSignificantEnergyPosition" src/ # → 0 results
grep -rn "beatStartPosition" src/              # → declarations + all callers present
```

---

*Generated from unified naming convention (m_beatStartPosition internal field).*
