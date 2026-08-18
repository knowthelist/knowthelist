/*
    Copyright (C) 2011 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "trackanalyzer.h"
#include "juce_audio_backend.h"

#include <QWidget>
#include <QFileInfo>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QTimer>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>
#include <QVector>
#include <QMetaObject>
#include <cmath>
#include <limits>
#include <atomic>

// Analysis parameters
constexpr bool kLogDebug = false;
constexpr int kAnalysisFrameRate = 120;
constexpr int kTempoMinBpm = 70;
constexpr int kTempoMaxBpm = 200;
constexpr float kSilenceRmsThreshold = 0.1f;
constexpr float kBeatFluxThresholdFraction = 0.05f;
constexpr float kTrailingSilenceRmsThreshold = 0.001778f; // -55 dBFS
constexpr float kTrailingSilencePeakFraction = 0.02f;
constexpr double kTrailingRmsWindowSeconds = 0.3;
constexpr double kTrailingSilenceHoldSeconds = 1.5;

struct TempoCacheEntry {
    int bpm = 0;
    int beatPhaseMs = 0;
    int beatStartMs = 0;
    int barAnchorMs = 0;
    double barPhaseConfidence = 0.0;
    double exactBpm = 0.0;
};

struct TrackAnalysisData {
    double sampleRate = 44100.0;
    double durationMs = 0.0;
    double averageRms = 0.0;
    double peakRms = 0.0;
    QList<float> frameRms;
    QList<float> frameLowRms;
    QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    QList<qint64> spectralFluxTimes;
    QVector<float> envelope;
    QTime startPosition = QTime(0, 0);
    QTime endPosition = QTime(0, 0);
    QTime beatActivityEndPosition = QTime(0, 0);
    double gainDb = 0.0;
    double lowEndConfidence = 0.0;
};

static QMutex g_scanCacheMutex;
static QHash<QString, TrackAnalysisData> g_scanCache;
static QSet<QString> g_scanInProgress;
static QWaitCondition g_scanComplete;

static QMutex g_tempoCacheMutex;
static QHash<QString, TempoCacheEntry> g_tempoCache;
static std::atomic<std::uint64_t> g_cacheEpoch {1};

static QThreadPool* analyzerThreadPool()
{
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.setThreadPriority(QThread::LowestPriority);
    return &pool;
}

static float lowPassStep(float input, float& lowState, float alpha)
{
    lowState += alpha * (input - lowState);
    return lowState;
}

struct KickGridFit {
    double bpm = 0.0;
    double phaseMs = 0.0;
    int matchedBeats = 0;
    double score = 0.0;
};

struct BarPhaseFit {
    bool accepted = false;
    double anchorMs = 0.0;
    double confidence = 0.0;
    double score = 0.0;
    double runnerUpScore = 0.0;
    int bars = 0;
    double downbeatMean = 0.0;
    double otherBeatMean = 0.0;
};

static double refinedOnsetTimeMs(int onsetFrame, const QList<float>& env, const QList<qint64>& times);

static KickGridFit fitKickGrid(const QVector<int>& onsets,
                               const QList<float>& envelope,
                               const QList<qint64>& times,
                               double seedBpm)
{
    KickGridFit best;
    if (seedBpm <= 0.0 || onsets.size() < 32 || times.isEmpty())
        return best;

    QVector<int> phaseCandidates = onsets;
    std::sort(phaseCandidates.begin(), phaseCandidates.end(), [&](int lhs, int rhs) {
        const float left = lhs >= 0 && lhs < envelope.size() ? envelope.at(lhs) : 0.0f;
        const float right = rhs >= 0 && rhs < envelope.size() ? envelope.at(rhs) : 0.0f;
        return left > right;
    });
    if (phaseCandidates.size() > 64)
        phaseCandidates.resize(64);

    const double minBpm = qMax(static_cast<double>(kTempoMinBpm), seedBpm - 0.5);
    const double maxBpm = qMin(static_cast<double>(kTempoMaxBpm), seedBpm + 0.5);
    for (double candidateBpm = minBpm; candidateBpm <= maxBpm + 1.0e-9; candidateBpm += 0.01) {
        const double beatMs = 60000.0 / candidateBpm;
        const double toleranceMs = qMin(70.0, beatMs * 0.16);
        const double sigmaMs = qMax(8.0, beatMs * 0.08);

        QVector<double> phases;
        phases.reserve(phaseCandidates.size());
        for (const int onset : phaseCandidates) {
            if (onset < 0 || onset >= times.size())
                continue;
            double phase = std::fmod(refinedOnsetTimeMs(onset, envelope, times), beatMs);
            if (phase < 0.0)
                phase += beatMs;
            bool duplicate = false;
            for (const double existing : phases) {
                if (qAbs(existing - phase) < 2.0
                    || qAbs(existing - phase + beatMs) < 2.0
                    || qAbs(existing - phase - beatMs) < 2.0) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
                phases.append(phase);
        }

        for (const double phaseMs : phases) {
            QHash<int, QPair<float, double>> strongestByBeat;
            for (const int onset : onsets) {
                if (onset < 0 || onset >= times.size())
                    continue;
                const double timeMs = refinedOnsetTimeMs(onset, envelope, times);
                const int beatIndex = qRound((timeMs - phaseMs) / beatMs);
                const double predictedMs = phaseMs + beatIndex * beatMs;
                if (qAbs(timeMs - predictedMs) > toleranceMs)
                    continue;

                const float strength = onset < envelope.size() ? envelope.at(onset) : 0.0f;
                const auto existing = strongestByBeat.constFind(beatIndex);
                if (existing == strongestByBeat.constEnd() || strength > existing.value().first)
                    strongestByBeat.insert(beatIndex, qMakePair(strength, timeMs));
            }

            if (strongestByBeat.size() < 32)
                continue;

            double score = 0.0;
            double sumI = 0.0;
            double sumT = 0.0;
            double sumIT = 0.0;
            double sumI2 = 0.0;
            for (auto it = strongestByBeat.constBegin(); it != strongestByBeat.constEnd(); ++it) {
                const double beatIndex = it.key();
                const double timeMs = it.value().second;
                const double distanceMs = timeMs - (phaseMs + beatIndex * beatMs);
                score += it.value().first
                        * std::exp(-(distanceMs * distanceMs) / (2.0 * sigmaMs * sigmaMs));
                sumI += beatIndex;
                sumT += timeMs;
                sumIT += beatIndex * timeMs;
                sumI2 += beatIndex * beatIndex;
            }

            const double count = static_cast<double>(strongestByBeat.size());
            const double denom = count * sumI2 - sumI * sumI;
            if (qAbs(denom) <= 1.0e-9)
                continue;

            const double fittedPeriodMs = (count * sumIT - sumI * sumT) / denom;
            if (fittedPeriodMs <= 0.0)
                continue;

            const double fittedBpm = 60000.0 / fittedPeriodMs;
            if (qAbs(fittedBpm - seedBpm) > 0.5)
                continue;
            const double interceptMs = (sumT - fittedPeriodMs * sumI) / count;
            double fittedPhaseMs = std::fmod(interceptMs, fittedPeriodMs);
            if (fittedPhaseMs < 0.0)
                fittedPhaseMs += fittedPeriodMs;

            const double normalizedScore = score / std::sqrt(count);
            if (normalizedScore > best.score) {
                best.bpm = fittedBpm;
                best.phaseMs = fittedPhaseMs;
                best.matchedBeats = strongestByBeat.size();
                best.score = normalizedScore;
            }
        }
    }

    return best;
}

static BarPhaseFit estimateFourFourBarPhase(const QVector<int>& onsets,
                                            const QList<float>& envelope,
                                            const QList<qint64>& times,
                                            double bpm,
                                            double beatPhaseMs,
                                            double firstBeatMs,
                                            int stableStartFrame,
                                            int stableEndFrame)
{
    BarPhaseFit best;
    if (bpm <= 0.0 || onsets.isEmpty() || times.size() < 2)
        return best;

    const double beatMs = 60000.0 / bpm;
    if (beatMs <= 1.0)
        return best;

    const int startFrame = qBound(0, stableStartFrame, times.size() - 1);
    const int endFrame = qBound(startFrame + 1, stableEndFrame, times.size());
    const double startMs = times.at(startFrame) / 1000000.0;
    const double endMs = times.at(endFrame - 1) / 1000000.0;
    if (endMs <= startMs)
        return best;

    const int firstGridIndex = qRound((firstBeatMs - beatPhaseMs) / beatMs);
    const int firstIndex = qCeil((startMs - beatPhaseMs) / beatMs);
    const int lastIndex = qFloor((endMs - beatPhaseMs) / beatMs);
    if (lastIndex - firstIndex < 31)
        return best;

    const double toleranceMs = qMin(70.0, beatMs * 0.18);
    QHash<int, double> beatStrengths;
    beatStrengths.reserve(lastIndex - firstIndex + 1);

    for (int beatIndex = firstIndex; beatIndex <= lastIndex; ++beatIndex) {
        const double targetMs = beatPhaseMs + beatIndex * beatMs;
        double strongest = 0.0;
        for (const int onset : onsets) {
            if (onset < 0 || onset >= times.size() || onset >= envelope.size())
                continue;
            const double onsetMs = refinedOnsetTimeMs(onset, envelope, times);
            if (qAbs(onsetMs - targetMs) <= toleranceMs)
                strongest = qMax(strongest, static_cast<double>(envelope.at(onset)));
        }
        beatStrengths.insert(beatIndex, strongest);
    }

    double totalEnergy = 0.0;
    int nonZeroBeats = 0;
    for (auto it = beatStrengths.constBegin(); it != beatStrengths.constEnd(); ++it) {
        totalEnergy += it.value();
        if (it.value() > 0.02)
            ++nonZeroBeats;
    }
    const double beatCount = static_cast<double>(beatStrengths.size());
    const double coverage = beatCount > 0.0 ? nonZeroBeats / beatCount : 0.0;
    const double averageEnergy = beatCount > 0.0 ? totalEnergy / beatCount : 0.0;
    // Low-band onset extraction can legitimately retain only the kick/downbeat
    // in sparse arrangements, so require some repeated support without
    // demanding a detected onset on every beat.
    if (coverage < 0.20 || averageEnergy < 0.02)
        return best;

    for (int offset = 0; offset < 4; ++offset) {
        double downbeatSum = 0.0;
        double otherSum = 0.0;
        int downbeatCount = 0;
        int otherCount = 0;
        int bars = 0;
        int downbeatWins = 0;

        for (int barStart = firstIndex; barStart + 3 <= lastIndex; ++barStart) {
            if ((barStart - firstGridIndex - offset) % 4 != 0)
                continue;

            double barDownbeat = 0.0;
            double barOthers = 0.0;
            bool complete = true;
            for (int beatInBar = 0; beatInBar < 4; ++beatInBar) {
                const auto value = beatStrengths.constFind(barStart + beatInBar);
                if (value == beatStrengths.constEnd()) {
                    complete = false;
                    break;
                }
                if (beatInBar == 0) {
                    barDownbeat = value.value();
                } else {
                    barOthers += value.value();
                }
            }
            if (!complete)
                continue;

            barOthers /= 3.0;
            downbeatSum += barDownbeat;
            otherSum += barOthers;
            ++downbeatCount;
            ++bars;
            otherCount += 1;
            if (barDownbeat > barOthers + 0.02)
                ++downbeatWins;
        }

        if (bars < 8 || downbeatCount <= 0 || otherCount <= 0)
            continue;

        const double downbeatMean = downbeatSum / downbeatCount;
        const double otherMean = otherSum / otherCount;
        const double contrast = qBound(0.0,
            (downbeatMean - otherMean) / qMax(0.05, downbeatMean + otherMean), 1.0);
        const double consistency = static_cast<double>(downbeatWins) / bars;
        const double score = 0.65 * contrast + 0.35 * consistency;

        if (score > best.score) {
            best.runnerUpScore = best.score;
            best.score = score;
            best.bars = bars;
            best.downbeatMean = downbeatMean;
            best.otherBeatMean = otherMean;
            const int candidateIndex = firstGridIndex + offset;
            best.anchorMs = qMax(0.0, beatPhaseMs + candidateIndex * beatMs);
        } else if (score > best.runnerUpScore) {
            best.runnerUpScore = score;
        }
    }

    const double separation = qBound(0.0,
        (best.score - best.runnerUpScore) / qMax(0.05, best.score), 1.0);
    const double contrast = qBound(0.0,
        (best.downbeatMean - best.otherBeatMean)
            / qMax(0.05, best.downbeatMean + best.otherBeatMean), 1.0);
    const double consistency = best.bars > 0
        ? qBound(0.0, (best.score - 0.65 * contrast) / 0.35, 1.0) : 0.0;
    best.confidence = qBound(0.0,
        0.45 * contrast + 0.30 * consistency + 0.15 * coverage + 0.10 * separation, 1.0);
    // Sparse arrangements often do not produce a separate onset on every
    // downbeat.  A clearly stronger and well-separated downbeat candidate is
    // still useful for beat-one synchronization, even when the strict
    // per-bar consistency test is inconclusive.
    const bool strongSparseDownbeat = best.bars >= 8
        && contrast >= 0.35
        && separation >= 0.10;
    best.accepted = best.bars >= 8
        && contrast >= 0.16
        && (consistency >= 0.55 || strongSparseDownbeat)
        && separation >= 0.08
        && (best.confidence >= 0.58 || strongSparseDownbeat);
    if (strongSparseDownbeat)
        best.confidence = qMax(best.confidence, 0.60);
    return best;
}

/*
  Description:
    Scans the given audio file and extracts analysis data such as RMS, spectral flux, and envelope.
    This is a CPU-intensive operation and should be run in a background thread to avoid blocking the UI.
*/
static TrackAnalysisData scanAudioFile(const QUrl& url)
{
    TrackAnalysisData data;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    juce::File audioFile(url.toLocalFile().toStdString());
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(audioFile));
    if (!reader)
        return data;

    data.sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    const int channels = qMax(1, static_cast<int>(reader->numChannels));
    const int frameSize = qMax(1, static_cast<int>(std::round(data.sampleRate / static_cast<double>(kAnalysisFrameRate))));
    const qint64 totalSamples = reader->lengthInSamples;
    data.durationMs = totalSamples > 0 ? (1000.0 * static_cast<double>(totalSamples) / data.sampleRate) : 0.0;

    juce::AudioBuffer<float> buffer(channels, frameSize);
    buffer.clear();

    // Keep the phase detector focused on kick and bass transients. At 44.1 kHz
    // alpha 0.12 reaches far into the midrange, where piano and percussion
    // attacks can pull the grid away from the rhythmic pulse.
    const float lowAlpha = 0.02f;
    float lowState = 0.0f;
    float prevRms = 0.0f;
    float prevLowRms = 0.0f;
    float peakRms = 0.0f;
    double sumRms = 0.0;
    int firstActiveFrame = -1;
    int lastActiveFrame = -1;
    bool readFailed = false;
    qint64 firstFailedSample = -1;

    for (qint64 samplePos = 0; samplePos < totalSamples; samplePos += frameSize) {
        const int numSamples = static_cast<int>(qMin<qint64>(frameSize, totalSamples - samplePos));
        buffer.clear();
        if (!reader->read(&buffer, 0, numSamples, samplePos, true, true)) {
            if (!readFailed) {
                qWarning() << "TrackAnalyzer: audio reader failed at sample"
                           << samplePos << "of" << totalSamples
                           << "for" << url;
                firstFailedSample = samplePos;
            }
            readFailed = true;
        }

        double sumSq = 0.0;
        double lowSumSq = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            float mono = 0.0f;
            double channelSumSq = 0.0;
            for (int ch = 0; ch < channels; ++ch) {
                const float sample = buffer.getSample(ch, i);
                mono += sample;
                channelSumSq += static_cast<double>(sample) * static_cast<double>(sample);
            }
            mono /= static_cast<float>(channels);

            const float low = lowPassStep(mono, lowState, lowAlpha);
            sumSq += channelSumSq / static_cast<double>(channels);
            lowSumSq += static_cast<double>(low) * static_cast<double>(low);
        }

        const float frameRms = static_cast<float>(std::sqrt(sumSq / qMax(1, numSamples)));
        const float frameLowRms = static_cast<float>(std::sqrt(lowSumSq / qMax(1, numSamples)));
        const float flux = qMax(0.0f, frameRms - prevRms);
        const float lowFlux = qMax(0.0f, frameLowRms - prevLowRms);
        const qint64 timestampNs = static_cast<qint64>((1000000000.0 * static_cast<double>(samplePos)) / data.sampleRate);

        data.frameRms.append(frameRms);
        data.frameLowRms.append(frameLowRms);
        data.spectralFlux.append(flux);
        data.spectralFluxLow.append(lowFlux * (1.0f + qMin(1.5f, frameLowRms * 3.0f)));
        data.spectralFluxTimes.append(timestampNs);

        prevRms = frameRms;
        prevLowRms = frameLowRms;
        peakRms = qMax(peakRms, frameRms);
        sumRms += frameRms;

        if (frameRms >= kSilenceRmsThreshold) {
            if (firstActiveFrame < 0)
                firstActiveFrame = static_cast<int>(data.frameRms.size()) - 1;
        }

        if ((data.frameRms.size() & 31) == 0) {
            QThread::yieldCurrentThread();
            QThread::msleep(1);
        }
    }

    data.peakRms = peakRms;
    data.averageRms = data.frameRms.isEmpty() ? 0.0 : sumRms / static_cast<double>(data.frameRms.size());

    // Estimate low-end presence from the existing per-frame low-pass RMS.
    // The confidence combines low-frequency energy share with persistence,
    // avoiding a high score from an isolated kick or a quiet intro.
    double lowRatioSum = 0.0;
    int activeFrames = 0;
    int lowActiveFrames = 0;
    float lowPeakRms = 0.0f;
    for (int i = 0; i < data.frameRms.size(); ++i) {
        lowPeakRms = qMax(lowPeakRms, data.frameLowRms.at(i));
        if (data.frameRms.at(i) < kSilenceRmsThreshold)
            continue;
        const double full = data.frameRms.at(i);
        const double low = data.frameLowRms.at(i);
        lowRatioSum += qBound(0.0, low / qMax(full, 1.0e-6), 1.0);
        ++activeFrames;
    }
    if (activeFrames > 0) {
        const double meanLowRatio = lowRatioSum / activeFrames;
        const double lowActivityThreshold = qMax(0.02f, lowPeakRms * 0.12f);
        for (int i = 0; i < data.frameLowRms.size(); ++i) {
            if (data.frameRms.at(i) >= kSilenceRmsThreshold
                && data.frameLowRms.at(i) >= lowActivityThreshold) {
                ++lowActiveFrames;
            }
        }
        const double persistence = static_cast<double>(lowActiveFrames) / activeFrames;
        const double energyScore = qBound(0.0, (meanLowRatio - 0.18) / 0.32, 1.0);
        const double persistenceScore = qBound(0.0, (persistence - 0.35) / 0.45, 1.0);
        data.lowEndConfidence = qBound(0.0,
            0.70 * energyScore + 0.30 * persistenceScore, 1.0);
    }

    // Detect only sustained trailing silence. A short sliding RMS window avoids
    // treating individual quiet frames as the end, while the hold period keeps
    // pauses and reverb tails inside the audible part of the track.
    const int trailingWindowFrames = qMax(
        1, qRound(kAnalysisFrameRate * kTrailingRmsWindowSeconds));
    const int trailingHoldFrames = qMax(
        1, qRound(kAnalysisFrameRate * kTrailingSilenceHoldSeconds));
    const float trailingSilenceThreshold = qMax(
        kTrailingSilenceRmsThreshold, peakRms * kTrailingSilencePeakFraction);
    QList<float> trailingRms;
    trailingRms.reserve(data.frameRms.size());
    double rollingSquareSum = 0.0;
    for (int i = 0; i < data.frameRms.size(); ++i) {
        const double frameSquare = static_cast<double>(data.frameRms.at(i))
            * static_cast<double>(data.frameRms.at(i));
        rollingSquareSum += frameSquare;
        if (i >= trailingWindowFrames) {
            const double removedSquare = static_cast<double>(data.frameRms.at(i - trailingWindowFrames))
                * static_cast<double>(data.frameRms.at(i - trailingWindowFrames));
            rollingSquareSum -= removedSquare;
        }
        const int sampleCount = qMin(i + 1, trailingWindowFrames);
        trailingRms.append(static_cast<float>(
            std::sqrt(qMax(0.0, rollingSquareSum / static_cast<double>(sampleCount)))));
    }

    int trailingSilentFrames = 0;
    if (!readFailed) {
        for (int i = trailingRms.size() - 1; i >= 0; --i) {
            if (trailingRms.at(i) >= trailingSilenceThreshold)
                break;
            ++trailingSilentFrames;
        }
    }
    if (readFailed) {
        // A failed decoder read produces cleared frames, which must not be
        // mistaken for genuine trailing silence.
        lastActiveFrame = data.frameRms.size() - 1;
    } else if (trailingSilentFrames >= trailingHoldFrames) {
        const int firstSilentWindow = trailingRms.size() - trailingSilentFrames;
        lastActiveFrame = qMax(0, firstSilentWindow - trailingWindowFrames + 1);
    } else {
        lastActiveFrame = data.frameRms.size() - 1;
    }
    const int diagnosticStartFrame = qMax(0, data.frameRms.size() - kAnalysisFrameRate * 10);
    float diagnosticTailPeak = 0.0f;
    for (int i = diagnosticStartFrame; i < data.frameRms.size(); ++i)
        diagnosticTailPeak = qMax(diagnosticTailPeak, data.frameRms.at(i));
    qDebug() << "Trailing RMS analysis:" << url.fileName()
             << "threshold=" << trailingSilenceThreshold
             << "tailPeak=" << diagnosticTailPeak
             << "readFailed=" << readFailed
             << "firstFailedSample=" << firstFailedSample
             << "lastActiveMs=" << (lastActiveFrame * 1000 / kAnalysisFrameRate)
             << "durationMs=" << qRound(data.durationMs);

    const float envelopePeak = qMax(peakRms, 1e-6f);
    data.envelope.reserve(data.frameRms.size());
    for (float rms : data.frameRms)
        data.envelope.append(qBound(0.0f, rms / envelopePeak, 1.0f));

    if (firstActiveFrame < 0) {
        data.startPosition = QTime(0, 0);
        data.endPosition = QTime(0, 0).addMSecs(static_cast<int>(qRound(data.durationMs)));
    } else {
        const double frameMs = 1000.0 * static_cast<double>(frameSize) / data.sampleRate;
        data.startPosition = QTime(0, 0).addMSecs(qRound(firstActiveFrame * frameMs));
        data.endPosition = QTime(0, 0).addMSecs(qMin(
            static_cast<int>(qRound(data.durationMs)), qRound((lastActiveFrame + 1) * frameMs)));
    }

    const float lowPeak = std::max_element(data.spectralFluxLow.constBegin(), data.spectralFluxLow.constEnd()) != data.spectralFluxLow.constEnd()
        ? *std::max_element(data.spectralFluxLow.constBegin(), data.spectralFluxLow.constEnd())
        : 0.0f;
    if (lowPeak > 0.0f && !data.spectralFluxLow.isEmpty()) {
        const float beatThreshold = lowPeak * kBeatFluxThresholdFraction;
        const int minSilentFrames = static_cast<int>(kAnalysisFrameRate * 3.0f);
        int lastBeatFrame = -1;
        for (int i = data.spectralFluxLow.size() - 1; i >= 0; --i) {
            if (data.spectralFluxLow.at(i) >= beatThreshold) {
                lastBeatFrame = i;
                break;
            }
        }

        if (lastBeatFrame >= 0 && (data.spectralFluxLow.size() - lastBeatFrame) >= minSilentFrames && lastBeatFrame < data.spectralFluxTimes.size()) {
            const qint64 tsNs = data.spectralFluxTimes.at(lastBeatFrame);
            data.beatActivityEndPosition = QTime(0, 0).addMSecs(static_cast<int>(tsNs / 1000000LL));
        } else {
            data.beatActivityEndPosition = data.endPosition;
        }
    } else {
        data.beatActivityEndPosition = data.endPosition;
    }

    if (data.averageRms > 0.0)
        data.gainDb = qBound(-18.0, 20.0 * std::log10(0.18 / qMax(data.averageRms, 1e-6)), 12.0);
    else
        data.gainDb = 0.0;

    return data;
}

static QString analysisCacheKey(const QUrl& url)
{
    const QFileInfo info(url.toLocalFile());
    const QString path = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    const qint64 size = info.exists() ? info.size() : -1;
    const qint64 modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    return QStringLiteral("%1|%2|%3").arg(path).arg(size).arg(modified);
}

static TrackAnalysisData scanAudioFileCached(const QUrl& url)
{
    const QString key = analysisCacheKey(url);
    QMutexLocker locker(&g_scanCacheMutex);

    // Wait if another thread is already scanning this file
    while (true) {
        const auto it = g_scanCache.constFind(key);
        if (it != g_scanCache.constEnd())
            return it.value();
        if (!g_scanInProgress.contains(key)) {
            g_scanInProgress.insert(key);
            break;
        }
        g_scanComplete.wait(&g_scanCacheMutex);
    }

    locker.unlock();
    TrackAnalysisData data = scanAudioFile(url);
    locker.relock();

    g_scanCache.insert(key, data);
    g_scanInProgress.remove(key);
    g_scanComplete.wakeAll();
    return data;
}

static QList<float> buildOnsetEnvelope(const QList<float>& input, int thresholdWindow, float thresholdMultiplier)
{
    QList<float> threshold;
    QList<float> pruned;
    QList<float> smoothed;

    threshold.reserve(input.size());
    pruned.reserve(input.size());
    smoothed.reserve(input.size());

    for (int i = 0; i < input.size(); ++i) {
        const int start = qMax(0, i - thresholdWindow);
        const int end = qMin(input.size() - 1, i + thresholdWindow);
        float mean = 0.0f;
        for (int j = start; j <= end; ++j)
            mean += input.at(j);
        mean /= qMax(1, end - start + 1);
        threshold.append(mean * thresholdMultiplier);
    }

    for (int i = 0; i < input.size(); ++i) {
        const float value = input.at(i) - threshold.at(i);
        pruned.append(value > 0.0f ? value : 0.0f);
    }

    for (int i = 0; i < pruned.size(); ++i) {
        const float prev = (i > 0) ? pruned.at(i - 1) : pruned.at(i);
        const float curr = pruned.at(i);
        const float next = (i + 1 < pruned.size()) ? pruned.at(i + 1) : pruned.at(i);
        smoothed.append((prev + 2.0f * curr + next) * 0.25f);
    }

    return smoothed;
}

static QVector<int> pickOnsets(const QList<float>& env, int minDistanceFrames)
{
    QVector<int> onsets;
    onsets.reserve(env.size() / 4);

    for (int i = 1; i < env.size() - 1; ++i) {
        if (env.at(i) <= 0.0f || env.at(i) < env.at(i - 1) || env.at(i) <= env.at(i + 1))
            continue;

        if (onsets.isEmpty()) {
            onsets.append(i);
            continue;
        }

        const int last = onsets.last();
        if (i - last < minDistanceFrames) {
            if (env.at(i) > env.at(last))
                onsets.last() = i;
        } else {
            onsets.append(i);
        }
    }

    return onsets;
}

static double lagCorrelation(const QList<float>& env, double lagFrames)
{
    const int lag = qRound(lagFrames);
    if (lag <= 0 || lag >= env.size())
        return 0.0;

    double corr = 0.0;
    double energy = 0.0;
    for (int i = 0; i + lag < env.size(); ++i) {
        const double a = env.at(i);
        const double b = env.at(i + lag);
        corr += a * b;
        energy += a * a + b * b;
    }

    return energy > 0.0 ? (2.0 * corr) / energy : 0.0;
}

static double strongestLagCorrelation(const QList<float>& env, double lagFrames, int radius)
{
    double best = 0.0;
    for (int delta = -radius; delta <= radius; ++delta)
        best = qMax(best, lagCorrelation(env, lagFrames + delta));
    return best;
}

static double refinedOnsetTimeMs(int onsetFrame, const QList<float>& env, const QList<qint64>& times)
{
    if (onsetFrame < 0 || onsetFrame >= times.size())
        return 0.0;

    const double baseMs = static_cast<double>(times.at(onsetFrame)) / 1.0e6;
    if (onsetFrame == 0 || onsetFrame + 1 >= env.size() || onsetFrame + 1 >= times.size())
        return baseMs;

    const double left = env.at(onsetFrame - 1);
    const double center = env.at(onsetFrame);
    const double right = env.at(onsetFrame + 1);
    const double denominator = left - 2.0 * center + right;
    if (qAbs(denominator) < 1.0e-9)
        return baseMs;

    // A parabola through the three neighboring samples estimates the peak between
    // frame boundaries, avoiding a systematic 8 ms timing quantization error.
    const double offset = qBound(-0.5, 0.5 * (left - right) / denominator, 0.5);
    const double frameMs = static_cast<double>(times.at(onsetFrame + 1) - times.at(onsetFrame)) / 1.0e6;
    return baseMs + offset * frameMs;
}

struct TrackAnalyzer_Private {
    QFutureWatcher<void> watcher;
    QMutex mutex;
    int bpm = 0;
    int preferredBpm = 0;
    bool bpmDetected = false;
    bool finishQueued = false;
    bool shuttingDown = false;
    bool inProgress = false;

    // Intermediate workspace for detectTempo() only — never exported
    QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    QList<qint64> spectralFluxTimes;

    double averageRms = 0.0;
    QList<float> frameRms;
    QUrl currentUrl;
};

TrackAnalyzer::TrackAnalyzer(QWidget* parent)
    : QWidget(parent)
    , p(new TrackAnalyzer_Private)
    , audioBackend(std::make_unique<JuceAudioBackend>())
    , m_analysisCache(std::make_shared<AnalysisCacheManager>(this))
{
    qDebug() << Q_FUNC_INFO << "Creating TrackAnalyzer";

    if (audioBackend) {
        audioBackend->initialize();
    }

}

TrackAnalyzer::~TrackAnalyzer()
{
    {
        QMutexLocker locker(&p->mutex);
        p->shuttingDown = true;
    }

    p->watcher.waitForFinished();  // ensure asyncOpen() thread has exited before freeing p
    cleanup();
    delete p;
    p = nullptr;
}

void TrackAnalyzer::setCachedBpm(int bpm)
{
    QMutexLocker locker(&p->mutex);
    p->preferredBpm = qMax(0, bpm);
}

void TrackAnalyzer::clearRuntimeCaches()
{
    g_cacheEpoch.fetch_add(1, std::memory_order_relaxed);
    {
        QMutexLocker locker(&g_tempoCacheMutex);
        g_tempoCache.clear();
    }
    {
        QMutexLocker locker(&g_scanCacheMutex);
        g_scanCache.clear();
    }
}

bool TrackAnalyzer::prepare()
{
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "TrackAnalyzer prepared";
    return true;
}

void TrackAnalyzer::open(QUrl url)
{
    const QString owner = parent() ? parent()->objectName() : QString();
    qDebug() << Q_FUNC_INFO << "owner=" << owner << "url=" << url;
    QMutexLocker locker(&p->mutex);
    if (p->inProgress && p->currentUrl == url)
        return;
    p->inProgress = true;
    p->currentUrl = url;
    p->finishQueued = false;
    p->bpmDetected = false;
    m_envelope.clear();
    m_ExactBpm = 0.0;
    m_lowEndConfidence = 0.0;
    locker.unlock();
    QFuture<void> future = QtConcurrent::run(analyzerThreadPool(),
                                             [this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void TrackAnalyzer::asyncOpen(QUrl url)
{
    QThread::currentThread()->setObjectName("TrackAnalyzerOpen");
    QThread::currentThread()->setPriority(QThread::LowestPriority);
    m_cacheEpochAtOpen = g_cacheEpoch.load(std::memory_order_relaxed);

    if (!audioBackend) {
        need_finish();
        return;
    }

    m_finished = false;

    // Reset BPM detection flag for new file
    {
        QMutexLocker locker(&p->mutex);
        p->bpmDetected = false;
    }

    // Load file with JUCE backend for analysis
    audioBackend->load(url);

    if (!audioBackend->isLoaded()) {
        qWarning() << "Failed to load file for analysis:" << url;
        need_finish();
        return;
    }

    QTime duration = audioBackend->getDuration();

    // ── Step D: Check SQLite cache before running full analysis ──
    const QString cachedKey = analysisCacheKey(url);
    {
        // Store cache key for later use in finalizeAnalysis
        m_lastCacheKey = cachedKey;

        AnalysisCacheManager::CachedTempo cached = loadCachedTempo(url);
        if (cached.valid && cached.bpm > 0 && hasValidCache(url, cachedKey)) {
            qDebug() << "[db cache hit] skipping analysis for:" << url.toLocalFile();

            // Apply cached state — direct ms→QTime conversion
            {
                QMutexLocker locker(&p->mutex);
                p->bpm       = cached.bpm;
                m_ExactBpm   = cached.exactBpm;
                m_trackDuration = duration;
                m_finished   = true;
                p->inProgress = false;
            }

            m_trackEffectiveStart  = QTime(0, 0).addMSecs(cached.startPositionMs);
            m_trackEffectiveEnd    = QTime(0, 0).addMSecs(cached.endPositionMs);
            m_beatStartPosition    = QTime(0, 0).addMSecs(cached.beatStartPositionMs);
            m_beatStopPosition     = QTime(0, 0).addMSecs(cached.beatEndPositionMs);
            m_barAnchorPosition    = QTime(0, 0).addMSecs(
                cached.barAnchorPositionMs > 0 ? cached.barAnchorPositionMs
                                               : cached.beatStartPositionMs);
            m_barPhaseConfidence   = qBound(0.0, cached.barPhaseConfidence, 1.0);
            m_lowEndConfidence     = qBound(0.0, cached.lowEndConfidence, 1.0);
            if (m_trackEffectiveStart.isValid() && m_trackEffectiveStart > QTime(0, 0)
                    && (!m_beatStartPosition.isValid() || m_beatStartPosition <= QTime(0, 0)
                        || m_beatStartPosition < m_trackEffectiveStart)) {
                // Legacy rows may contain beat phase offset (~0-1 beat) in place of
                // absolute cue/beat-start position. Never allow that to override
                // the effective track start.
                m_beatStartPosition = m_trackEffectiveStart;
            }

            // Restore beat-grid phase for visual beat lines.
            m_BeatPosition = QTime(0, 0).addMSecs(qMax(0, cached.beatPhasePositionMs));
            if (m_BeatPosition <= QTime(0, 0)) {
                // Backward compatibility for older cache rows without persisted phase.
                const double bpmForPhase = cached.exactBpm > 0.0
                        ? cached.exactBpm
                        : static_cast<double>(cached.bpm);
                if (bpmForPhase > 0.0 && cached.beatStartPositionMs > 0) {
                    const double beatMs = 60000.0 / bpmForPhase;
                    if (beatMs > 1.0e-6) {
                        double phaseMs = std::fmod(static_cast<double>(cached.beatStartPositionMs), beatMs);
                        if (phaseMs < 0.0)
                            phaseMs += beatMs;
                        m_BeatPosition = QTime(0, 0).addMSecs(qMax(0, qRound(phaseMs)));
                    }
                }
            }

            qDebug() << "[tempo cache]"
                     << "bpm=" << cached.bpm
                     << "exactBpm=" << cached.exactBpm
                     << "beatPhase=" << m_BeatPosition
                     << "beatStart=" << m_beatStartPosition
                     << "barAnchor=" << m_barAnchorPosition
                     << "barConfidence=" << m_barPhaseConfidence;

            // A tempo cache hit is only complete when the envelope cache also
            // matches the current envelope format. Otherwise continue into
            // the full scan so the waveform cannot retain stale track data.
            AnalysisCacheManager::CachedEnvelope envCached = loadCachedEnvelope(url);
            if (envCached.valid && !envCached.samples.isEmpty()) {
                m_envelope = envCached.samples;
                need_finish();
                return;
            }
        }
    }

    TrackAnalysisData analysis = scanAudioFileCached(url);
    if (kLogDebug) {
        qDebug() << Q_FUNC_INFO << "duration from JUCE:" << duration << "duration from analysis:" << QTime(0, 0).addMSecs(static_cast<int>(qRound(analysis.durationMs)));
    }
    if (analysis.durationMs > 0.0)
        duration = QTime(0, 0).addMSecs(static_cast<int>(qRound(analysis.durationMs)));

    {
        QMutexLocker locker(&p->mutex);

        // ── Output state → m_ fields (ONE source of truth) ──
        m_trackEffectiveStart   = analysis.startPosition;
        m_trackEffectiveEnd     = analysis.endPosition;
        m_beatStopPosition      = analysis.beatActivityEndPosition;
        m_beatStartPosition     = QTime(0, 0);              // set later in detectTempo()
        m_BeatPosition          = QTime(0, 0);              // set later in detectTempo()
        m_barAnchorPosition     = QTime(0, 0);              // set later in detectTempo()
        m_barPhaseConfidence    = 0.0;
        m_trackDuration         = duration;                 // full duration from audioBackend/analysis

        m_GainDB              = analysis.gainDb;
        m_lowEndConfidence   = analysis.lowEndConfidence;
        m_envelope            = analysis.envelope;           // live array for amplitudeEnvelope() callers

        // ── Intermediate workspace → p- fields (detectTempo only) ──
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
    qDebug() << "TrackAnalyzer low-end confidence:" << m_lowEndConfidence;

    // Keep autocorrelation and cache preparation off the GUI thread. The
    // queued finalizeAnalysis() call below is the only GUI-thread handoff.
    detectTempo();
    need_finish();
}

bool TrackAnalyzer::close()
{
    cleanup();
    return true;
}

double TrackAnalyzer::gainDB()
{
    QMutexLocker locker(&p->mutex);
    return m_GainDB;
}

double TrackAnalyzer::gainFactor()
{
    double gainDb = gainDB();
    if (gainDb == GAIN_INVALID)
        return 1.0;
    return std::pow(10.0, gainDb / 20.0);
}

double TrackAnalyzer::lowEndConfidence()
{
    QMutexLocker locker(&p->mutex);
    return m_lowEndConfidence;
}

QTime TrackAnalyzer::startPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_trackEffectiveStart;
}

QTime TrackAnalyzer::endPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_trackEffectiveEnd;
}

QTime TrackAnalyzer::beatPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_BeatPosition;
}

QTime TrackAnalyzer::beatEndPosition()
{
    QMutexLocker locker(&p->mutex);

    // Return the pre-computed beat stop position from finalizeAnalysis().
    // Computation was done once during asyncOpen — no live recalculation.
    return m_beatStopPosition;
}

QTime TrackAnalyzer::beatStartPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_beatStartPosition;
}

QTime TrackAnalyzer::barAnchorPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_barAnchorPosition;
}

double TrackAnalyzer::barPhaseConfidence()
{
    QMutexLocker locker(&p->mutex);
    return m_barPhaseConfidence;
}

int TrackAnalyzer::bpm()
{
    QMutexLocker locker(&p->mutex);
    return p->bpm;
}

double TrackAnalyzer::exactBpm()
{
    return m_ExactBpm > 0.0 ? m_ExactBpm : static_cast<double>(p->bpm);
}

QVector<float> TrackAnalyzer::amplitudeEnvelope() const
{
    return m_envelope;
}

void TrackAnalyzer::setPosition(QTime position)
{
    if (audioBackend) {
        audioBackend->seek(position);
    }
}

QTime TrackAnalyzer::length()
{
    QMutexLocker locker(&p->mutex);
    return m_trackDuration;
}

void TrackAnalyzer::need_finish()
{
    qDebug() << Q_FUNC_INFO;
    QMutexLocker locker(&p->mutex);
    if (p->finishQueued)
        return;
    p->finishQueued = true;
    QMetaObject::invokeMethod(this, &TrackAnalyzer::finalizeAnalysis, Qt::QueuedConnection);
}

void TrackAnalyzer::finalizeAnalysis()
{
    {
        QMutexLocker locker(&p->mutex);
        p->finishQueued = false;
        p->inProgress = false;
        m_finished = true;
    }

    // Persist analyzed results to SQLite (one-write, uses current analyzer state).
    // Skip persisting if a cache reset happened while this analysis was in flight.
    const std::uint64_t epochNow = g_cacheEpoch.load(std::memory_order_relaxed);
    if (p->currentUrl.isValid() && m_analysisCache && m_cacheEpochAtOpen == epochNow) {
        storeCachedTempo();
        const int durMs = qBound(0, m_trackDuration.msecsSinceStartOfDay(), INT_MAX);
        storeCachedEnvelope(durMs);
    }

    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Unified analysis complete.";

    // Emit outside the analyzer mutex: slots may query analyzer state and
    // would deadlock if finalizeAnalysis keeps the lock while emitting.
    emit finishGain();
    emit finishTempo();
    emit finishEnvelope();
}

void TrackAnalyzer::detectTempo()
{
    static const int kMinBpm = kTempoMinBpm;
    static const int kMaxBpm = kTempoMaxBpm;

    QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    QList<qint64> spectralFluxTimes;
    QUrl currentUrl;
    int preferredBpm = 0;
    {
        QMutexLocker locker(&p->mutex);
        // Skip if BPM already detected for this session
        if (p->bpmDetected)
            return;
        spectralFlux = p->spectralFlux;
        spectralFluxLow = p->spectralFluxLow;
        spectralFluxTimes = p->spectralFluxTimes;
        currentUrl = p->currentUrl;
        preferredBpm = p->preferredBpm;
    }

    if (preferredBpm > 0) {
        QMutexLocker locker(&p->mutex);
        p->bpm = preferredBpm;
        m_ExactBpm = static_cast<double>(preferredBpm);
        m_BeatPosition = m_trackEffectiveStart;
        m_beatStartPosition = m_trackEffectiveStart;
        m_barAnchorPosition = m_beatStartPosition;
        m_barPhaseConfidence = 0.0;
        p->bpmDetected = true;
        qDebug() << Q_FUNC_INFO << "Using supplied cached BPM:" << preferredBpm
                 << "beatPhase=" << m_BeatPosition
                 << "barAnchor=" << m_barAnchorPosition
                 << "barConfidence=" << m_barPhaseConfidence;
        return;
    }

    const QString cacheKey = analysisCacheKey(currentUrl);
    {
        QMutexLocker cacheLocker(&g_tempoCacheMutex);
        const auto it = g_tempoCache.constFind(cacheKey);
        if (it != g_tempoCache.constEnd()) {
            const TempoCacheEntry entry = it.value();
            QMutexLocker locker(&p->mutex);
            p->bpm = entry.bpm;
            m_ExactBpm = (entry.exactBpm > 0.0) ? entry.exactBpm : static_cast<double>(entry.bpm);
            m_BeatPosition = QTime(0, 0).addMSecs(qMax(0, entry.beatPhaseMs));
            m_beatStartPosition = QTime(0, 0).addMSecs(qMax(0, entry.beatStartMs));
            m_barAnchorPosition = QTime(0, 0).addMSecs(qMax(0, entry.barAnchorMs));
            m_barPhaseConfidence = qBound(0.0, entry.barPhaseConfidence, 1.0);
            if (m_barAnchorPosition <= QTime(0, 0))
                m_barAnchorPosition = m_beatStartPosition;
            if (m_trackEffectiveStart.isValid() && m_trackEffectiveStart > QTime(0, 0)
                    && (!m_beatStartPosition.isValid() || m_beatStartPosition <= QTime(0, 0)
                        || m_beatStartPosition < m_trackEffectiveStart)) {
                m_beatStartPosition = m_trackEffectiveStart;
            }
            qDebug() << "[runtime tempo cache hit] bpm=" << p->bpm
                     << "beatPhase=" << m_BeatPosition
                     << "beatStartPosition=" << m_beatStartPosition
                     << "barAnchor=" << m_barAnchorPosition
                     << "barConfidence=" << m_barPhaseConfidence;
            p->bpmDetected = true;
            return;
        }
    }

    if (spectralFlux.isEmpty()) {
        QMutexLocker locker(&p->mutex);
        p->bpm = 0;
        m_BeatPosition = m_trackEffectiveStart;
        m_beatStartPosition = m_trackEffectiveStart;
        m_barAnchorPosition = m_beatStartPosition;
        m_barPhaseConfidence = 0.0;
        return;
    }

    // Compute actual analysis fps from nanosecond timestamps.
    // For 44100 Hz: frameSize = round(44100/120) = 368 → actual fps = 119.837 (not 120).
    // Using the wrong 120 fps introduces a +0.137% BPM error → ~1 beat of drift per 5 min.
    double actualFps = static_cast<double>(kAnalysisFrameRate);
    if (spectralFluxTimes.size() >= 2) {
        const qint64 totalNs = spectralFluxTimes.last() - spectralFluxTimes.first();
        if (totalNs > 0)
            actualFps = 1.0e9 * (spectralFluxTimes.size() - 1) / static_cast<double>(totalNs);
    }

    if (spectralFluxLow.size() != spectralFlux.size())
        spectralFluxLow = spectralFlux;

    const QList<float> fullEnv = buildOnsetEnvelope(spectralFlux, 12, 1.35f);
    const QList<float> lowEnv = buildOnsetEnvelope(spectralFluxLow, 14, 1.20f);

    const int minDistance = qMax(1, qRound((actualFps * 60.0) / 240.0));
    const QVector<int> onsetsFull = pickOnsets(fullEnv, minDistance);
    const QVector<int> onsetsLow = pickOnsets(lowEnv, minDistance + 2);

    // Tempo evidence must come from a stable rhythmic section. Intro synths,
    // breakdowns, and outro effects can contain strong but intentionally
    // off-grid transients. Keep the complete envelope for display, but exclude
    // the first 45 seconds and final 20 seconds from BPM estimation when the
    // track is long enough to leave a substantial middle section.
    const int stableStartFrame = (spectralFlux.size() > static_cast<int>(actualFps * 120.0))
        ? qRound(actualFps * 45.0) : 0;
    const int stableEndFrame = (spectralFlux.size() > static_cast<int>(actualFps * 120.0))
        ? spectralFlux.size() - qRound(actualFps * 20.0) : spectralFlux.size();

    auto stableOnsetsOnly = [&](const QVector<int>& onsets) {
        QVector<int> stable;
        stable.reserve(onsets.size());
        for (const int onset : onsets) {
            if (onset >= stableStartFrame && onset < stableEndFrame)
                stable.append(onset);
        }
        return stable;
    };

    const QVector<int> tempoOnsetsFull = stableOnsetsOnly(onsetsFull);
    const QVector<int> tempoOnsetsLow = stableOnsetsOnly(onsetsLow);
    const QVector<int>& selectedTempoOnsets = tempoOnsetsLow.isEmpty() ? tempoOnsetsFull : tempoOnsetsLow;
    const QList<float>& selectedTempoEnv = tempoOnsetsLow.isEmpty() ? fullEnv : lowEnv;

    QVector<double> score(kMaxBpm + 1, 0.0);
    QVector<double> exactBpmSum(kMaxBpm + 1, 0.0);
    QVector<double> exactBpmWeight(kMaxBpm + 1, 0.0);

    auto voteTempo = [&](const QVector<int>& onsets, const QList<float>& env, double weight, bool lowBandSource) {
        for (int i = 0; i < onsets.size(); ++i) {
            const int base = onsets.at(i);
            const float baseWeight = qMax(0.01f, env.at(base));
            const int upper = qMin(onsets.size(), i + 18);
            for (int j = i + 1; j < upper; ++j) {
                const int delta = onsets.at(j) - base;
                if (delta <= 0)
                    continue;

                double bpm = (actualFps * 60.0) / static_cast<double>(delta);
                while (bpm < static_cast<double>(kMinBpm))
                    bpm *= 2.0;
                while (bpm > static_cast<double>(kMaxBpm))
                    bpm *= 0.5;

                if (bpm >= static_cast<double>(kMinBpm) && bpm <= static_cast<double>(kMaxBpm)) {
                    const int bpmBin = qBound(kMinBpm, qRound(bpm), kMaxBpm);
                    const float pairWeight = qMax(0.01f, env.at(onsets.at(j)));
                    const double pairDistancePenalty = 1.0 / (1.0 + 0.08 * (j - i - 1));
                    const double contribution = weight * static_cast<double>(baseWeight * pairWeight) * pairDistancePenalty;
                    score[bpmBin] += contribution;
                    exactBpmSum[bpmBin] += contribution * bpm;
                    exactBpmWeight[bpmBin] += contribution;

                    if (lowBandSource && bpmBin >= 72 && bpmBin <= 90) {
                        const int doubledBin = bpmBin * 2;
                        if (doubledBin >= 140 && doubledBin <= 180)
                            score[doubledBin] += contribution * 0.65;
                    }
                }
            }
        }
    };

    voteTempo(tempoOnsetsFull, fullEnv, 1.0, false);
    voteTempo(tempoOnsetsLow, lowEnv, 2.25, true);

    QVector<double> smoothedScore = score;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        const double prev = score[qMax(kMinBpm, bpmBin - 1)];
        const double curr = score[bpmBin];
        const double next = score[qMin(kMaxBpm, bpmBin + 1)];
        smoothedScore[bpmBin] = 0.2 * prev + 1.0 * curr + 0.2 * next;
    }

    int bestBpm = 0;
    double bestScore = 0.0;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        if (smoothedScore[bpmBin] > bestScore) {
            bestScore = smoothedScore[bpmBin];
            bestBpm = bpmBin;
        }
    }

    auto supportFor = [&](int bpmBin) {
        if (bpmBin < kMinBpm || bpmBin > kMaxBpm)
            return 0.0;

        double support = 0.0;
        for (int d = -2; d <= 2; ++d) {
            const int idx = qBound(kMinBpm, bpmBin + d, kMaxBpm);
            support += smoothedScore[idx] * ((d == 0) ? 1.0 : 0.7);
        }

        if (bpmBin * 2 <= kMaxBpm && bpmBin >= 100)
            support += 0.30 * smoothedScore[bpmBin * 2];
        if (bpmBin / 2 >= kMinBpm)
            support += 0.20 * smoothedScore[bpmBin / 2];

        return support;
    };

    auto strongestNear = [&](int center, int radius) {
        int bestBpmBin = center;
        double bestSupport = 0.0;
        const int start = qMax(kMinBpm, center - radius);
        const int end = qMin(kMaxBpm, center + radius);
        for (int candidate = start; candidate <= end; ++candidate) {
            const double candidateSupport = supportFor(candidate);
            if (candidateSupport > bestSupport) {
                bestSupport = candidateSupport;
                bestBpmBin = candidate;
            }
        }
        return QPair<int, double>(bestBpmBin, bestSupport);
    };

    QList<float> combinedEnv;
    combinedEnv.reserve(fullEnv.size());
    for (int i = 0; i < fullEnv.size(); ++i)
        combinedEnv.append(0.6f * fullEnv.at(i) + 0.4f * lowEnv.at(i));

    const int stableEnvStart = qBound(0, stableStartFrame, combinedEnv.size());
    const int stableEnvEnd = qBound(stableEnvStart, stableEndFrame, combinedEnv.size());
    const QList<float> stableCombinedEnv = combinedEnv.mid(stableEnvStart, stableEnvEnd - stableEnvStart);
    const QList<float> stableLowEnv = lowEnv.mid(stableEnvStart, stableEnvEnd - stableEnvStart);
    const QList<float>& tempoCombinedEnv = stableCombinedEnv.isEmpty() ? combinedEnv : stableCombinedEnv;
    const QList<float>& tempoLowEnv = stableLowEnv.isEmpty() ? lowEnv : stableLowEnv;

    QVector<double> lagStrength(kMaxBpm + 1, 0.0);
    double maxLagStrength = 0.0;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        const double lag = (actualFps * 60.0) / bpmBin;
        const double combinedBase = strongestLagCorrelation(tempoCombinedEnv, lag, 1);
        const double lowBase = strongestLagCorrelation(tempoLowEnv, lag, 1);
        const double combinedDouble = strongestLagCorrelation(tempoCombinedEnv, lag * 2.0, 2);
        const double lowDouble = strongestLagCorrelation(tempoLowEnv, lag * 2.0, 2);
        const double combinedHalf = strongestLagCorrelation(tempoCombinedEnv, lag * 0.5, 1);
        const double lowHalf = strongestLagCorrelation(tempoLowEnv, lag * 0.5, 1);

        double strength = 0.55 * combinedBase + 1.00 * lowBase;
        if (bpmBin >= 118)
            strength += 0.45 * combinedDouble + 0.85 * lowDouble;
        else
            strength += 0.12 * combinedHalf + 0.18 * lowHalf;

        lagStrength[bpmBin] = strength;
        maxLagStrength = qMax(maxLagStrength, strength);
    }

    if (maxLagStrength > 0.0) {
        for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin)
            lagStrength[bpmBin] /= maxLagStrength;
    }

    const double bestSupportOverall = qMax(0.0001, supportFor(bestBpm));
    const int autoCorrBpm = qRound(AutoCorrelation(tempoCombinedEnv, tempoCombinedEnv.count(), kMinBpm, kMaxBpm, actualFps));
    const int autoCorrLowBpm = qRound(AutoCorrelation(tempoLowEnv, tempoLowEnv.count(), kMinBpm, kMaxBpm, actualFps));

    int autoCorrConsensus = autoCorrBpm;
    if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm) {
        if (autoCorrConsensus < kMinBpm || autoCorrConsensus > kMaxBpm)
            autoCorrConsensus = autoCorrLowBpm;
        else if (qAbs(autoCorrLowBpm - autoCorrConsensus) <= 4)
            autoCorrConsensus = qRound(0.5 * autoCorrConsensus + 0.5 * autoCorrLowBpm);
        else if (qAbs(autoCorrLowBpm * 2 - autoCorrConsensus) <= 4)
            autoCorrConsensus = autoCorrLowBpm * 2;
        else if (qAbs(autoCorrLowBpm - autoCorrConsensus * 2) <= 4)
            autoCorrConsensus = qRound(autoCorrLowBpm * 0.5);
    }

    QVector<int> candidates;
    auto addCandidate = [&](int bpm) {
        if (bpm < kMinBpm || bpm > kMaxBpm)
            return;
        if (!candidates.contains(bpm))
            candidates.append(bpm);
    };

    auto addTopPeaks = [&](const QVector<double>& values, int count, int minSpacing) {
        QVector<bool> used(values.size(), false);
        for (int pick = 0; pick < count; ++pick) {
            int peak = -1;
            double peakValue = 0.0;
            for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
                if (used[bpmBin])
                    continue;
                if (values[bpmBin] > peakValue) {
                    peakValue = values[bpmBin];
                    peak = bpmBin;
                }
            }
            if (peak < 0 || peakValue <= 0.0)
                break;

            addCandidate(peak);
            const int start = qMax(kMinBpm, peak - minSpacing);
            const int end = qMin(kMaxBpm, peak + minSpacing);
            for (int bpmBin = start; bpmBin <= end; ++bpmBin)
                used[bpmBin] = true;
        }
    };

    addCandidate(bestBpm);
    addCandidate(qRound(bestBpm * 0.5));
    addCandidate(bestBpm * 2);
    addCandidate(qRound(bestBpm * 1.5));
    addCandidate(qRound(bestBpm / 1.5));
    addCandidate(autoCorrConsensus);
    addCandidate(autoCorrLowBpm);
    addCandidate(autoCorrConsensus * 2);
    addCandidate(autoCorrLowBpm * 2);
    addTopPeaks(smoothedScore, 6, 3);

    auto candidateStrength = [&](int bpm) {
        double strength = supportFor(bpm);

        const int half = qRound(bpm * 0.5);
        const int doub = bpm * 2;
        const int threeHalf = qRound(bpm * 1.5);
        const int twoThird = qRound(bpm / 1.5);

        if (half >= kMinBpm)
            strength += 0.10 * supportFor(half);
        if (doub <= kMaxBpm && bpm >= 100)
            strength += 0.28 * supportFor(doub);
        if (threeHalf <= kMaxBpm)
            strength += 0.24 * supportFor(threeHalf);
        if (twoThird >= kMinBpm)
            strength += 0.12 * supportFor(twoThird);

        if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm) {
            if (qAbs(bpm - autoCorrConsensus) <= 4)
                strength *= 1.12;
            else if (qAbs(bpm * 2 - autoCorrConsensus) <= 4 || qAbs(autoCorrConsensus * 2 - bpm) <= 4)
                strength *= 1.06;
            else if (qAbs(qRound(bpm * 1.5) - autoCorrConsensus) <= 4 || qAbs(qRound(autoCorrConsensus * 1.5) - bpm) <= 4)
                strength *= 1.05;
        }

        const double lagBoost = lagStrength[bpm];
        const double supportGate = qMin(1.0, supportFor(bpm) / bestSupportOverall);
        strength += supportFor(bpm) * ((bpm >= 118) ? 0.45 : 0.18) * lagBoost * supportGate;

        return strength;
    };

    int finalBpm = 0;
    double finalStrength = 0.0;
    for (int candidate : candidates) {
        const double strength = candidateStrength(candidate);
        if (strength > finalStrength) {
            finalStrength = strength;
            finalBpm = candidate;
        }
    }

    if (finalBpm == 0)
        finalBpm = (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm) ? autoCorrConsensus : bestBpm;

    auto promoteHalfTimeToDouble = [&](int minRange, int maxRange) {
        if (finalBpm < minRange || finalBpm > maxRange)
            return;

        const int doubledBpm = finalBpm * 2;
        if (doubledBpm < kMinBpm || doubledBpm > kMaxBpm)
            return;

        const double baseSupport = supportFor(finalBpm);
        QPair<int, double> doubledCandidate = strongestNear(doubledBpm, 6);
        double doubledSupport = doubledCandidate.second;
        const int tripletBpm = qRound(finalBpm * 1.5);
        const double tripletSupport = (tripletBpm >= kMinBpm && tripletBpm <= kMaxBpm) ? supportFor(tripletBpm) : 0.0;

        if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm && qAbs(doubledCandidate.first - autoCorrConsensus) <= 5)
            doubledSupport *= 1.10;
        if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm && qAbs(doubledCandidate.first - autoCorrLowBpm) <= 5)
            doubledSupport *= 1.08;

        if (doubledSupport >= baseSupport * 0.34 && doubledSupport >= tripletSupport * 0.92)
            finalBpm = doubledCandidate.first;
    };

    promoteHalfTimeToDouble(68, 95);
    promoteHalfTimeToDouble(70, 82);

    double finalExactBpm = static_cast<double>(finalBpm);
    if (finalBpm >= kMinBpm && finalBpm <= kMaxBpm && exactBpmWeight[finalBpm] > 0.0)
        finalExactBpm = exactBpmSum[finalBpm] / exactBpmWeight[finalBpm];

    // Refine exact BPM using least-squares linear regression over all detected onsets.
    // This is the standard approach used by aubio, librosa, and Essentia:
    //   Model: t_beat[k] = t0 + beatNumber[k] * T    (T = beat period in seconds)
    //   Solve for T using OLS over all N onsets -> precision ~T^2 / (track_duration * sqrt(N))
    // For 128 BPM over 5 min with ~500 onsets: precision < 0.001 BPM (vs 0.26 BPM from frame voting).
    if (finalBpm > 0 && !spectralFluxTimes.isEmpty()) {
        const QVector<int>& onsets = selectedTempoOnsets;
        const int N = onsets.size();
        if (N >= 8) {
            const double estimatedPeriodS = 60.0 / static_cast<double>(finalBpm);
            const double t0ms = refinedOnsetTimeMs(onsets.first(), selectedTempoEnv, spectralFluxTimes);

            // Assign beat number to each onset by rounding to nearest beat.
            QVector<double> beatNum(N), tSec(N);
            for (int k = 0; k < N; ++k) {
                const int frame = onsets.at(k);
                const double onsetMs = refinedOnsetTimeMs(frame, selectedTempoEnv, spectralFluxTimes);
                const double tS = (frame < spectralFluxTimes.size())
                    ? (onsetMs - t0ms) * 1.0e-3
                        : static_cast<double>(frame) / actualFps;
                tSec[k]    = tS;
                beatNum[k] = qRound(tS / estimatedPeriodS);
            }

            // OLS: minimize sum((tSec[k] - t0 - beatNum[k]*T)^2) w.r.t. T and t0.
            double sumI = 0.0, sumT = 0.0, sumIT = 0.0, sumI2 = 0.0;
            for (int k = 0; k < N; ++k) {
                const double i = beatNum.at(k);
                const double t = tSec.at(k);
                sumI  += i;
                sumT  += t;
                sumIT += i * t;
                sumI2 += i * i;
            }
            const double denom = static_cast<double>(N) * sumI2 - sumI * sumI;
            if (qAbs(denom) > 1e-12) {
                const double T = (static_cast<double>(N) * sumIT - sumI * sumT) / denom;
                if (T > 0.0) {
                    const double refinedBpm = 60.0 / T;
                    // Accept only if within 0.5 BPM of the integer estimate.
                    if (qAbs(refinedBpm - static_cast<double>(finalBpm)) < 0.5)
                        finalExactBpm = refinedBpm;
                }
            }
        }
    }

    {
        QMutexLocker locker(&p->mutex);
        p->bpm = qBound(0, finalBpm, kMaxBpm);
        p->bpmDetected = true;
    }
    m_ExactBpm = finalExactBpm;

    const QList<float>& anchorEnv = lowEnv.isEmpty() ? fullEnv : lowEnv;
    const QVector<int>& phaseOnsets = !onsetsLow.isEmpty() ? onsetsLow : onsetsFull;
    int phaseMs = 0;
    const KickGridFit kickFit = fitKickGrid(phaseOnsets, anchorEnv, spectralFluxTimes, m_ExactBpm);
    const bool kickFitApplied = kickFit.matchedBeats >= 100
            && kickFit.bpm > 0.0
            && qAbs(kickFit.bpm - m_ExactBpm) <= 0.5;
    if (kickFitApplied) {
        m_ExactBpm = kickFit.bpm;
        phaseMs = qRound(kickFit.phaseMs);
    }

    if (!kickFitApplied && p->bpm > 0 && m_ExactBpm > 0.0 && !phaseOnsets.isEmpty() && !spectralFluxTimes.isEmpty()) {
        const double beatMs = 60000.0 / m_ExactBpm;
        if (beatMs > 1.0) {
            // Fit beat phase globally over all onsets instead of anchoring on a single
            // early spike. This is much more stable and aligns beat grid to kick peaks.
            const int periodMs = qMax(1, qRound(beatMs));
            const int coarseStepMs = qMax(1, periodMs / 240);
            const double sigmaMs = qMax(6.0, beatMs * 0.10);
            const double invTwoSigma2 = 1.0 / (2.0 * sigmaMs * sigmaMs);

            auto phaseScore = [&](int candidatePhaseMs) {
                double score = 0.0;
                for (int onsetIdx : phaseOnsets) {
                    if (onsetIdx < 0 || onsetIdx >= spectralFluxTimes.size())
                        continue;
                    const double tMs = refinedOnsetTimeMs(onsetIdx, anchorEnv, spectralFluxTimes);
                    double wrap = std::fmod(tMs - static_cast<double>(candidatePhaseMs), beatMs);
                    if (wrap < 0.0)
                        wrap += beatMs;
                    const double dist = qMin(wrap, beatMs - wrap);
                    const double weight = (onsetIdx >= 0 && onsetIdx < anchorEnv.size())
                            ? qMax(0.01, static_cast<double>(anchorEnv.at(onsetIdx)))
                            : 0.01;
                    score += weight * std::exp(-(dist * dist) * invTwoSigma2);
                }
                return score;
            };

            double bestScore = -1.0;
            int bestPhase = 0;
            for (int cand = 0; cand < periodMs; cand += coarseStepMs) {
                const double score = phaseScore(cand);
                if (score > bestScore) {
                    bestScore = score;
                    bestPhase = cand;
                }
            }

            // 1 ms local refinement around the best coarse phase.
            int refined = bestPhase;
            double refinedScore = bestScore;
            for (int d = -coarseStepMs; d <= coarseStepMs; ++d) {
                int cand = bestPhase + d;
                while (cand < 0)
                    cand += periodMs;
                while (cand >= periodMs)
                    cand -= periodMs;
                const double score = phaseScore(cand);
                if (score > refinedScore) {
                    refinedScore = score;
                    refined = cand;
                }
            }

            phaseMs = refined;
        }
    } else {
        // Fallback: preserve previous behaviour if onset set is unavailable.
        int phaseAnchorIdx = -1;
        float strongestAnchor = 0.0f;
        for (float value : anchorEnv)
            strongestAnchor = qMax(strongestAnchor, value);

        for (int i = 0; i < anchorEnv.size(); ++i) {
            if (anchorEnv.at(i) > strongestAnchor * 0.95f) {
                phaseAnchorIdx = i;
                break;
            }
        }

        if (phaseAnchorIdx >= 0 && p->bpm > 0 && phaseAnchorIdx < spectralFluxTimes.size() && spectralFluxTimes.at(phaseAnchorIdx) > 0) {
            const qint64 anchorMs = spectralFluxTimes.at(phaseAnchorIdx) / 1000000LL;
            const double beatMs = 60000.0 / qMax(1e-6, m_ExactBpm);
            phaseMs = static_cast<int>(std::fmod(static_cast<double>(anchorMs), beatMs));
            if (phaseMs < 0)
                phaseMs += qMax(1, qRound(beatMs));
        }
    }

    m_BeatPosition = QTime(0, 0).addMSecs(qMax(0, phaseMs));

    // A beat cue must begin in a sustained rhythmic section.  The first loud
    // frame is not sufficient: intros commonly contain isolated notes or
    // percussion before the kick pattern starts.  Measure support on the
    // detected beat grid and require most beats in a short rolling window.
    {
        const int firstEnergyMs = m_trackEffectiveStart.msecsSinceStartOfDay();
        const int lastEnergyMs = m_trackEffectiveEnd.msecsSinceStartOfDay();

        if (p->bpm > 0 && m_ExactBpm > 0.0 && !phaseOnsets.isEmpty()) {
            const double beatMs = 60000.0 / m_ExactBpm;
            const float onsetPeak = anchorEnv.isEmpty()
                ? 0.0f
                : *std::max_element(anchorEnv.constBegin(), anchorEnv.constEnd());
            const double supportThreshold = qMax(0.02, static_cast<double>(onsetPeak) * 0.08);
            const double toleranceMs = qMin(70.0, beatMs * 0.16);
            const int windowBeats = 8;

            auto gridSupport = [&](int beatIndex) {
                const double targetMs = static_cast<double>(phaseMs) + beatIndex * beatMs;
                double strongest = 0.0;
                for (const int onset : phaseOnsets) {
                    if (onset < 0 || onset >= spectralFluxTimes.size()
                        || onset >= anchorEnv.size())
                        continue;
                    const double onsetMs = refinedOnsetTimeMs(onset, anchorEnv, spectralFluxTimes);
                    if (qAbs(onsetMs - targetMs) <= toleranceMs)
                        strongest = qMax(strongest, static_cast<double>(anchorEnv.at(onset)));
                }
                return strongest;
            };

            const int firstGridIndex = qCeil(
                (static_cast<double>(firstEnergyMs) - phaseMs) / beatMs);
            const int lastGridIndex = qFloor(
                (static_cast<double>(lastEnergyMs) - phaseMs) / beatMs);
            int sustainedStartIndex = -1;
            for (int beatIndex = firstGridIndex; beatIndex <= lastGridIndex - windowBeats + 1; ++beatIndex) {
                int supported = 0;
                for (int offset = 0; offset < windowBeats; ++offset) {
                    if (gridSupport(beatIndex + offset) >= supportThreshold)
                        ++supported;
                }
                if (supported >= 6) {
                    // The long window validates that a section is rhythmic,
                    // but its first beat may still be an isolated pickup.
                    // Start at the first locally supported 4-beat run inside
                    // that window so the cue lands on an audible beat.
                    for (int localIndex = beatIndex; localIndex <= beatIndex + 4; ++localIndex) {
                        int localSupported = 0;
                        for (int offset = 0; offset < 4; ++offset) {
                            if (gridSupport(localIndex + offset) >= supportThreshold)
                                ++localSupported;
                        }
                        if (localSupported >= 3) {
                            sustainedStartIndex = localIndex;
                            break;
                        }
                    }
                    if (sustainedStartIndex < 0)
                        sustainedStartIndex = beatIndex;
                    break;
                }
            }

            if (sustainedStartIndex >= 0) {
                m_beatStartPosition = QTime(0, 0).addMSecs(qMax(
                    0, qRound(phaseMs + sustainedStartIndex * beatMs)));
            } else {
                m_beatStartPosition = m_trackEffectiveStart;
            }

            int lastSupportedIndex = -1;
            for (int beatIndex = firstGridIndex; beatIndex <= lastGridIndex; ++beatIndex) {
                if (gridSupport(beatIndex) >= supportThreshold)
                    lastSupportedIndex = beatIndex;
            }
            if (lastSupportedIndex >= 0) {
                const int detectedBeatEnd = qRound(phaseMs + lastSupportedIndex * beatMs);
                const int existingBeatEnd = m_beatStopPosition.msecsSinceStartOfDay();
                if (existingBeatEnd <= 0 || detectedBeatEnd < existingBeatEnd)
                    m_beatStopPosition = QTime(0, 0).addMSecs(qMax(0, detectedBeatEnd));
            }

            qDebug() << Q_FUNC_INFO << "Rhythmic cue boundaries:"
                     << "beatStart=" << m_beatStartPosition
                     << "beatStop=" << m_beatStopPosition
                     << "supportThreshold=" << supportThreshold
                     << "sustainedStartIndex=" << sustainedStartIndex
                     << "lastSupportedIndex=" << lastSupportedIndex;
        } else {
            m_beatStartPosition = m_trackEffectiveStart;
            qDebug() << Q_FUNC_INFO << "[m_beatStartPosition]:"
                     << m_beatStartPosition << "No usable beat grid";
        }
    }

    const BarPhaseFit barFit = estimateFourFourBarPhase(
        phaseOnsets, anchorEnv, spectralFluxTimes, m_ExactBpm,
        static_cast<double>(phaseMs),
        static_cast<double>(QTime(0, 0).msecsTo(m_beatStartPosition)),
        stableStartFrame, stableEndFrame);
    if (barFit.accepted) {
        m_barAnchorPosition = QTime(0, 0).addMSecs(qMax(0, qRound(barFit.anchorMs)));
        m_barPhaseConfidence = barFit.confidence;
    } else {
        // A weak or ambiguous 4/4 accent pattern must not move the grid.
        // The first detected beat is the conservative, behavior-preserving fallback.
        m_barAnchorPosition = m_beatStartPosition;
        m_barPhaseConfidence = 0.0;
    }

    {
        QMutexLocker cacheLocker(&g_tempoCacheMutex);
        TempoCacheEntry entry;
        entry.bpm = qBound(0, finalBpm, kMaxBpm);
        entry.beatPhaseMs = QTime(0, 0).msecsTo(m_BeatPosition);
        entry.beatStartMs = QTime(0, 0).msecsTo(m_beatStartPosition);
        entry.barAnchorMs = QTime(0, 0).msecsTo(m_barAnchorPosition);
        entry.barPhaseConfidence = m_barPhaseConfidence;
        entry.exactBpm = m_ExactBpm;
        g_tempoCache.insert(cacheKey, entry);
    }

    qDebug() << Q_FUNC_INFO << "Estimated BPM:" << p->bpm << "exactBpm:" << m_ExactBpm
             << "beatPhase:" << m_BeatPosition
             << "beatStart:" << m_beatStartPosition
             << "barAnchor:" << m_barAnchorPosition
             << "barConfidence:" << m_barPhaseConfidence
             << "barFitAccepted:" << barFit.accepted
             << "barFitBars:" << barFit.bars
             << "barFitScore:" << barFit.score
             << "barFitRunnerUp:" << barFit.runnerUpScore
             << "kickFitBpm:" << kickFit.bpm
             << "kickFitPhaseMs:" << kickFit.phaseMs
             << "kickFitMatchedBeats:" << kickFit.matchedBeats
             << "kickFitApplied:" << kickFitApplied
             << "frames:" << spectralFlux.size()
             << "onsetsFull:" << onsetsFull.size() << "onsetsLow:" << onsetsLow.size();
}

float TrackAnalyzer::AutoCorrelation(QList<float> buffer, int frames, int minBpm, int maxBpm, double sampleRate)
{
    float maxCorr = 0.0f;
    int maxLag = 0;
    const int maxOffset = static_cast<int>(sampleRate * 60.0 / minBpm);
    const int minOffset = static_cast<int>(sampleRate * 60.0 / maxBpm);
    if (frames > buffer.count())
        frames = buffer.count();

    for (int lag = minOffset; lag < maxOffset; ++lag) {
        float corr = 0.0f;
        for (int i = 0; i < frames - lag; ++i)
            corr += (buffer.at(i + lag) * buffer.at(i));

        if (corr > maxCorr) {
            maxCorr = corr;
            maxLag = lag;
        }
    }

    if (maxLag > 0)
        return static_cast<float>(sampleRate * 60.0 / maxLag);

    return 0.0f;
}

void TrackAnalyzer::cleanup()
{
    if (audioBackend) {
        audioBackend->stop();
    }
}

// ── Cache lifecycle (Steps C/E): owned by TrackAnalyzer ──

AnalysisCacheManager::CachedTempo TrackAnalyzer::loadCachedTempo(const QUrl& url)
{
    if (!m_analysisCache)
        return {};
    return m_analysisCache->loadCachedTempo(url);
}

void TrackAnalyzer::storeCachedTempo()
{
    if (p->bpm <= 0 || !m_analysisCache || p->currentUrl.isEmpty())
        return;

    const int startPosMs   = m_trackEffectiveStart.msecsSinceStartOfDay();
    const int endPosMs     = m_trackEffectiveEnd.msecsSinceStartOfDay();
    const int beatStartMs  = m_beatStartPosition.msecsSinceStartOfDay();
    const int beatPhaseMs  = m_BeatPosition.msecsSinceStartOfDay();
    const int beatEndMs    = m_beatStopPosition.msecsSinceStartOfDay();
    const int barAnchorMs  = m_barAnchorPosition.msecsSinceStartOfDay();

    m_analysisCache->storeCachedTempo(
        p->currentUrl,
        p->bpm,
        m_ExactBpm,
        startPosMs,
        endPosMs,
        beatStartMs,
        beatPhaseMs,
        beatEndMs,
        barAnchorMs,
        m_barPhaseConfidence,
        m_lowEndConfidence
    );

    if (!m_lastCacheKey.isEmpty())
        m_analysisCache->setCachedKey(p->currentUrl, m_lastCacheKey);
}

void TrackAnalyzer::storeCachedEnvelope(int durationMs)
{
    if (m_envelope.isEmpty() || !m_analysisCache || p->currentUrl.isEmpty())
        return;
    m_analysisCache->storeCachedEnvelope(p->currentUrl, m_envelope, qBound(0, durationMs, INT_MAX));
}

bool TrackAnalyzer::hasValidCache(const QUrl& url, const QString& cacheKey) const
{
    if (!m_analysisCache)
        return false;
    return m_analysisCache->hasValidCache(url, cacheKey);
}

AnalysisCacheManager::CachedEnvelope TrackAnalyzer::loadCachedEnvelope(const QUrl& url)
{
    if (!m_analysisCache)
        return {};
    return m_analysisCache->loadCachedEnvelope(url);
}
