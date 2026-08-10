#include "transitionplanner.h"

#include "track.h"

#include <QtGlobal>

namespace {
bool isDanceGenre(const QString& genre)
{
    const QString value = genre.toLower();
    return value.contains("dance") || value.contains("house") || value.contains("techno")
        || value.contains("trance") || value.contains("edm") || value.contains("disco")
        || value.contains("electronic");
}

bool isVocalGenre(const QString& genre)
{
    const QString value = genre.toLower();
    return value.contains("rock") || value.contains("pop") || value.contains("indie")
        || value.contains("folk") || value.contains("country") || value.contains("soul");
}
}

TransitionPreferences TransitionPreferences::fromSettings(const QSettings& settings)
{
    TransitionPreferences preferences;
    preferences.beatBlendEnabled = settings.value("Transition/BeatBlendEnabled", true).toBool();
    preferences.bassSwapEnabled = settings.value("Transition/BassSwapEnabled", true).toBool();
    preferences.vocalHandoffEnabled = settings.value("Transition/VocalHandoffEnabled", true).toBool();
    preferences.equalPowerEnabled = settings.value("Transition/EqualPowerEnabled", true).toBool();
    preferences.hardCutEnabled = settings.value("Transition/HardCutEnabled", true).toBool();
    preferences.minimumDurationSeconds = qBound(
        1, settings.value("Transition/MinimumDurationSeconds", 1).toInt(), 30);
    preferences.maximumDurationSeconds = qBound(
        preferences.minimumDurationSeconds,
        settings.value("Transition/MaximumDurationSeconds", 30).toInt(), 30);
    preferences.maximumTempoCorrectionPercent = qBound(
        0, settings.value("Transition/MaximumTempoCorrectionPercent", 12).toInt(), 50);
    preferences.style = settings.value("Transition/Style", "balanced").toString();
    return preferences;
}

TransitionPlan TransitionPlanner::choose(Track* outgoing, Track* incoming,
                                          int outgoingBpm, int incomingBpm,
                                          int defaultDurationSeconds,
                                          const TransitionPreferences& preferences)
{
    TransitionPlan plan;
    plan.durationSeconds = qBound(preferences.minimumDurationSeconds,
                                  defaultDurationSeconds,
                                  preferences.maximumDurationSeconds);

    auto setMode = [&plan, &preferences](TransitionMode mode, int duration, double confidence,
                                          const QString& rationale) {
        const bool enabled = (mode == TransitionMode::BeatBlend && preferences.beatBlendEnabled)
            || (mode == TransitionMode::BassSwap && preferences.bassSwapEnabled)
            || (mode == TransitionMode::VocalHandoff && preferences.vocalHandoffEnabled)
            || (mode == TransitionMode::EqualPower && preferences.equalPowerEnabled)
            || (mode == TransitionMode::HardCut && preferences.hardCutEnabled);
        if (!enabled)
            return false;
        plan.mode = mode;
        plan.cueMode = mode == TransitionMode::BeatBlend || mode == TransitionMode::BassSwap
            ? TransitionCueMode::SkipSilenceOccurrence
            : mode == TransitionMode::HardCut
                ? TransitionCueMode::BeatOccurrence
                : TransitionCueMode::SkipSilence;
        plan.durationSeconds = qBound(preferences.minimumDurationSeconds,
                                      duration, preferences.maximumDurationSeconds);
        plan.confidence = confidence;
        plan.rationale = rationale;
        return true;
    };

    if (!outgoing || !incoming || outgoingBpm <= 0 || incomingBpm <= 0) {
        if (!setMode(TransitionMode::EqualPower, defaultDurationSeconds, 0.0,
                     QStringLiteral("Missing pair metadata; using a safe equal-power fade."))) {
            plan.mode = TransitionMode::EqualPower;
            plan.rationale = QStringLiteral("No enabled transition was available; using equal-power fallback.");
        }
        return plan;
    }

    const int bpmDistance = qAbs(outgoingBpm - incomingBpm);
    const int slowerBpm = qMax(1, qMin(outgoingBpm, incomingBpm));
    const int tempoCorrectionPercent = qRound(100.0 * bpmDistance / slowerBpm);
    const bool dancePair = isDanceGenre(outgoing->genre()) && isDanceGenre(incoming->genre());
    const bool vocalPair = isVocalGenre(outgoing->genre()) || isVocalGenre(incoming->genre());

    const bool preferDance = preferences.style == "dance";
    const bool preferVocal = preferences.style == "vocal";

    if (bpmDistance >= 45 && tempoCorrectionPercent > preferences.maximumTempoCorrectionPercent) {
        if (setMode(TransitionMode::HardCut, 1, 0.82,
                    QStringLiteral("Large BPM gap; a long blend would require excessive tempo correction.")))
            return plan;
    }

    // A matching tempo is the strongest reliable signal that a phrase-aligned
    // blend will work, even when genre metadata is missing or inconsistent.
    if (bpmDistance <= 3
        && tempoCorrectionPercent <= preferences.maximumTempoCorrectionPercent
        && setMode(TransitionMode::BeatBlend, qBound(8, defaultDurationSeconds, 24), 0.9,
                   QStringLiteral("Matching BPM; use beat and phrase alignment by default."))) {
        plan.matchTempo = true;
        return plan;
    }

    if ((dancePair || preferDance) && tempoCorrectionPercent <= preferences.maximumTempoCorrectionPercent
        && setMode(TransitionMode::BeatBlend, qBound(8, defaultDurationSeconds, 24), 0.78,
                   QStringLiteral("Tempo-compatible dance profile; blend on beats and phrases."))) {
        plan.matchTempo = true;
        return plan;
    }

    if (dancePair && setMode(TransitionMode::BassSwap, qBound(6, defaultDurationSeconds, 18), 0.68,
                             QStringLiteral("Dance pair; hand off the low end during the blend."))) {
        plan.matchTempo = tempoCorrectionPercent <= preferences.maximumTempoCorrectionPercent;
        return plan;
    }

    if ((vocalPair || preferVocal)
        && setMode(TransitionMode::VocalHandoff, qBound(4, defaultDurationSeconds, 16), 0.62,
                   QStringLiteral("Vocal-oriented material; make the incoming lead dominant."))) {
        return plan;
    }

    if (setMode(TransitionMode::EqualPower, qBound(4, defaultDurationSeconds, 16), 0.45,
                QStringLiteral("No strong style match; use a neutral equal-power crossfade."))) {
        return plan;
    }

    if (preferences.hardCutEnabled)
        setMode(TransitionMode::HardCut, 1, 0.2,
                QStringLiteral("All preferred blend modes are disabled; using the enabled hard cut."));
    return plan;
}
