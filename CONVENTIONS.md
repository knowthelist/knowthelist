# Local Engine AI Coding Constraints

## 1. Token Safety & Output Caps
- NEVER rewrite a whole file or large sections of code.
- You must break all requested tasks into micro-steps (maximum 15-20 lines of changes per turn).
- If a refactoring task requires massive modifications, explain your plan first, modify ONE function, then ask the user: "Should I proceed to the next step?".
- Ensure any unified diff patch you send contains less than 200 total output tokens.

## 2. PlayerWidget Architecture (MANDATORY — DO NOT VIOLATE)

These rules define how `PlayerWidget` and `PlayerCueManager` must interact. Every PR and agent task must respect them.

### 2.1 Layer separation

| Layer | Responsibility | Files |
|-------|---------------|-------|
| **UI / orchestration** | Transport (play/pause/stop), button connects, analysis callbacks, UI state updates (`setInfo`, `bpmWidget->setState`), envelope scrubbing | `src/playerwidget.h/.cpp`, `playerwidget.ui` |
| **Pure calculation** | All cue-point math, fade-point computation, timestamp logic. **No Qt, no widgets, no UI access.** | `src/playercuemanager.h/.cpp` |

### 2.2 Golden rules

1. **No duplicate calculations.** If the same logic exists in both classes, move it to `PlayerCueManager` and delete the PlayerWidget copy. This is a violation every time.
2. **Calculate → delegate.** All cue-point calculation routes through `PlayerCueManager`. `PlayerWidget::calculateCuePosition()` is a thin dispatcher, not an implementation.
3. **No inline fade-point logic in PlayerWidget.** The pattern of computing `min(beatActivityEnd(), trackEnd())` inline must never appear outside `PlayerCueManager::computeFadePoint()`. Any class that needs a fade point calls `m_cueManager->computeFadePoint()`.
4. **No mixing calculation with UI.** Never compute timestamps and then immediately touch widgets in the same function if that function belongs to `PlayerCueManager`. Calculation functions return values; the caller (PlayerWidget) applies UI side effects.

### 2.3 Source of truth map

```
calculateCuePosition(mode)       → PlayerCueManager (single owner)
determineSilentEndCuePosition()  → PlayerCueManager (single owner) — DO NOT duplicate in PlayerWidget
computeFadePoint()               → PlayerCueManager (single owner)
computeRemainCueTime()           → PlayerCueManager (single owner)
applyAutoCueAfterAnalysis()      → PlayerCueManager computes, PlayerWidget applies UI
```

### 2.4 Anti-pattern to flag immediately

| ❌ Violation | ✅ Correct pattern |
|---|---|
| Inline `trackanalyzer->beatActivityEndPosition()` + min logic in PlayerWidget | Call `m_cueManager->computeFadePoint()` |
| PlayerWidget re-implements `determineSilentEndCuePosition()` | Delete it; delegate through `m_cueManager->calculateCuePosition()` |
| CueManager accessing `ui->` or `bpmWidget->` directly | Return values; let PlayerWidget touch widgets |
