#ifndef TRANSITIONPLANNER_H
#define TRANSITIONPLANNER_H

#include <QString>
#include <QSettings>

class Track;

enum class TransitionMode {
    EqualPower,
    BeatBlend,
    BassSwap,
    VocalHandoff,
    HardCut
};

enum class TransitionCueMode {
    SkipSilence,
    BeatOccurrence,
    SkipSilenceOccurrence
};

struct TransitionPlan {
    TransitionMode mode{TransitionMode::EqualPower};
    TransitionCueMode cueMode{TransitionCueMode::SkipSilence};
    int durationSeconds{12};
    bool matchTempo{false};
    double confidence{0.0};
    QString rationale;
};

struct TransitionPreferences {
    bool beatBlendEnabled{true};
    bool bassSwapEnabled{true};
    bool vocalHandoffEnabled{true};
    bool equalPowerEnabled{true};
    bool hardCutEnabled{true};
    int minimumDurationSeconds{1};
    int maximumDurationSeconds{30};
    int maximumTempoCorrectionPercent{12};
    QString style{QStringLiteral("balanced")};

    static TransitionPreferences fromSettings(const QSettings& settings);
};

class TransitionPlanner {
public:
    static TransitionPlan choose(Track* outgoing, Track* incoming,
                                 int outgoingBpm, int incomingBpm,
                                 int defaultDurationSeconds,
                                 const TransitionPreferences& preferences);
};

#endif
