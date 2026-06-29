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
#include <QtConcurrent/QtConcurrent>
#include <QVector>
#include <QMetaObject>
#include <cmath>
#include <limits>

// Analysis parameters
constexpr int AUDIOFREQ = 32000;
constexpr int SCAN_DURATION = 60;
constexpr int GAIN_ANALYSIS_CHUNK = 4096;
constexpr bool kLogDebug = false;
constexpr int kAnalysisFrameRate = 120;
constexpr int kTempoMinBpm = 70;
constexpr int kTempoMaxBpm = 200;
constexpr float kSilenceRmsThreshold = 0.1f;

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
};

static float computeRMS(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sumSquares += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(numSamples)));
}

static float lowPassStep(float input, float& lowState, float alpha)
{
    lowState += alpha * (input - lowState);
    return lowState;
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

    const float lowAlpha = 0.12f;
    float lowState = 0.0f;
    float prevRms = 0.0f;
    float prevLowRms = 0.0f;
    float peakRms = 0.0f;
    double sumRms = 0.0;
    int firstActiveFrame = -1;
    int lastActiveFrame = -1;

    for (qint64 samplePos = 0; samplePos < totalSamples; samplePos += frameSize) {
        const int numSamples = static_cast<int>(qMin<qint64>(frameSize, totalSamples - samplePos));
        buffer.clear();
        reader->read(&buffer, 0, numSamples, samplePos, true, true);

        double sumSq = 0.0;
        double lowSumSq = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            float mono = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                mono += buffer.getSample(ch, i);
            mono /= static_cast<float>(channels);

            const float low = lowPassStep(mono, lowState, lowAlpha);
            sumSq += static_cast<double>(mono) * static_cast<double>(mono);
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
            lastActiveFrame = static_cast<int>(data.frameRms.size()) - 1;
        }

        if ((data.frameRms.size() & 127) == 0)
            QThread::yieldCurrentThread();
    }

    data.peakRms = peakRms;
    data.averageRms = data.frameRms.isEmpty() ? 0.0 : sumRms / static_cast<double>(data.frameRms.size());

    const float envelopePeak = qMax(peakRms, 1e-6f);
    data.envelope.reserve(data.frameRms.size());
    for (float rms : data.frameRms)
        data.envelope.append(qBound(0.0f, rms / envelopePeak, 1.0f));

    if (firstActiveFrame < 0) {
        data.startPosition = QTime(0, 0);
        data.endPosition = QTime(0, 0).addMSecs(static_cast<int>(qRound(data.durationMs)));
    } else {
        const int frameMs = qMax(1, qRound(1000.0 / kAnalysisFrameRate));
        data.startPosition = QTime(0, 0).addMSecs(firstActiveFrame * frameMs);
        data.endPosition = QTime(0, 0).addMSecs(qMin(static_cast<int>(qRound(data.durationMs)), (lastActiveFrame + 1) * frameMs));
    }

    const float lowPeak = std::max_element(data.spectralFluxLow.constBegin(), data.spectralFluxLow.constEnd()) != data.spectralFluxLow.constEnd()
        ? *std::max_element(data.spectralFluxLow.constBegin(), data.spectralFluxLow.constEnd())
        : 0.0f;
    if (lowPeak > 0.0f && !data.spectralFluxLow.isEmpty()) {
        const float beatThreshold = lowPeak * 0.10f;
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
    static QMutex cacheMutex;
    static QHash<QString, TrackAnalysisData> cache;
    static QSet<QString> inProgress;
    static QWaitCondition scanComplete;

    const QString key = analysisCacheKey(url);
    QMutexLocker locker(&cacheMutex);

    // Wait if another thread is already scanning this file
    while (true) {
        const auto it = cache.constFind(key);
        if (it != cache.constEnd())
            return it.value();
        if (!inProgress.contains(key)) {
            inProgress.insert(key);
            break;
        }
        scanComplete.wait(&cacheMutex);
    }

    locker.unlock();
    TrackAnalysisData data = scanAudioFile(url);
    locker.relock();

    cache.insert(key, data);
    inProgress.remove(key);
    scanComplete.wakeAll();
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

struct TrackAnalyzer_Private {
    QFutureWatcher<void> watcher;
    QMutex mutex;
    int bpm = 0;
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
{
    qDebug() << Q_FUNC_INFO << "Creating TrackAnalyzer";

    if (audioBackend) {
        audioBackend->initialize();
    }

    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));
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

bool TrackAnalyzer::prepare()
{
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "TrackAnalyzer prepared";
    return true;
}

void TrackAnalyzer::open(QUrl url)
{
    qDebug() << Q_FUNC_INFO << "url=" << url;
    QMutexLocker locker(&p->mutex);
    if (p->inProgress && p->currentUrl == url)
        return;
    p->inProgress = true;
    p->currentUrl = url;
    p->finishQueued = false;
    p->bpmDetected = false;
    m_ExactBpm = 0.0;
    locker.unlock();
    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void TrackAnalyzer::asyncOpen(QUrl url)
{
    QThread::currentThread()->setObjectName("TrackAnalyzerOpen");
    QThread::currentThread()->setPriority(QThread::LowestPriority);

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
    TrackAnalysisData analysis = scanAudioFileCached(url);
    qDebug() << Q_FUNC_INFO << "duration from JUCE:" << duration << "duration from analysis:" << QTime(0, 0).addMSecs(static_cast<int>(qRound(analysis.durationMs)));
    if (analysis.durationMs > 0.0)
        duration = QTime(0, 0).addMSecs(static_cast<int>(qRound(analysis.durationMs)));

    {
        QMutexLocker locker(&p->mutex);

        // ── Output state → m_ fields (ONE source of truth) ──
        m_trackEffectiveStart   = analysis.startPosition;
        m_trackEffectiveEnd     = analysis.endPosition;
        m_beatStopPosition      = analysis.beatActivityEndPosition;
        m_beatStartPosition     = QTime(0, 0);              // set later in detectTempo()
        m_trackDuration         = duration;                 // full duration from audioBackend/analysis

        m_GainDB              = analysis.gainDb;
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
}

void TrackAnalyzer::start()
{
    qDebug() << Q_FUNC_INFO << "Starting unified analysis";
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

QTime TrackAnalyzer::beatActivityEndPosition()
{
    QMutexLocker locker(&p->mutex);

    // Find the first frame where RMS crosses a "significant" threshold
    // (above silence and above average by some margin). This is more reliable
    // than beat detection for finding the true start of musical content.
    if (p->frameRms.isEmpty()) {
        return m_trackEffectiveStart; // fallback to audio start
    }

    const double significantThreshold = qMax(p->averageRms * 0.5, kSilenceRmsThreshold * 3);
    const int frameMs = qMax(1, qRound(1000.0 / kAnalysisFrameRate));

    for (int i = 0; i < p->frameRms.size(); ++i) {
        if (p->frameRms.at(i) >= significantThreshold) {
            return QTime(0, 0).addMSecs(i * frameMs);
        }
    }

    // No significant energy found - fallback to beat position or start position
    if (p->bpmDetected) {
        return m_beatStartPosition;
    }
    return m_trackEffectiveStart;
}

QTime TrackAnalyzer::beatStartPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_beatStartPosition;
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
    bool finishedWasQueued = false;

    {
        QMutexLocker locker(&p->mutex);
        p->finishQueued = false;
        p->inProgress = false;
        m_finished = true;
        finishedWasQueued = true;
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
    static QMutex tempoCacheMutex;
    static QHash<QString, QPair<int, int> > tempoCache;

    QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    QList<qint64> spectralFluxTimes;
    QList<float> frameRms;
    double averageRms = 0.0;
    QUrl currentUrl;
    {
        QMutexLocker locker(&p->mutex);
        // Skip if BPM already detected for this session
        if (p->bpmDetected)
            return;
        spectralFlux = p->spectralFlux;
        spectralFluxLow = p->spectralFluxLow;
        spectralFluxTimes = p->spectralFluxTimes;
        frameRms = p->frameRms;
        averageRms = p->averageRms;
        currentUrl = p->currentUrl;
    }

    const QString cacheKey = analysisCacheKey(currentUrl);
    {
        QMutexLocker cacheLocker(&tempoCacheMutex);
        const auto it = tempoCache.constFind(cacheKey);
        if (it != tempoCache.constEnd()) {
            QMutexLocker locker(&p->mutex);
            p->bpm = it.value().first;
            m_BeatPosition = QTime(0, 0).addMSecs(it.value().second);
            m_beatStartPosition = m_BeatPosition; // first significant energy ≈ beat start
            p->bpmDetected = true;
            return;
        }
    }

    if (spectralFlux.isEmpty()) {
        QMutexLocker locker(&p->mutex);
        p->bpm = 0;
        m_BeatPosition = m_trackEffectiveStart;
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

    voteTempo(onsetsFull, fullEnv, 1.0, false);
    voteTempo(onsetsLow, lowEnv, 2.25, true);

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

    QVector<double> lagStrength(kMaxBpm + 1, 0.0);
    double maxLagStrength = 0.0;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        const double lag = (actualFps * 60.0) / bpmBin;
        const double combinedBase = strongestLagCorrelation(combinedEnv, lag, 1);
        const double lowBase = strongestLagCorrelation(lowEnv, lag, 1);
        const double combinedDouble = strongestLagCorrelation(combinedEnv, lag * 2.0, 2);
        const double lowDouble = strongestLagCorrelation(lowEnv, lag * 2.0, 2);
        const double combinedHalf = strongestLagCorrelation(combinedEnv, lag * 0.5, 1);
        const double lowHalf = strongestLagCorrelation(lowEnv, lag * 0.5, 1);

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
    const int autoCorrBpm = qRound(AutoCorrelation(combinedEnv, combinedEnv.count(), kMinBpm, kMaxBpm, actualFps));
    const int autoCorrLowBpm = qRound(AutoCorrelation(lowEnv, lowEnv.count(), kMinBpm, kMaxBpm, actualFps));

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

    if (finalBpm >= 68 && finalBpm <= 95) {
        const int doubledBpm = finalBpm * 2;
        const bool hasDoubled = (doubledBpm >= kMinBpm && doubledBpm <= kMaxBpm);
        QPair<int, double> doubledCandidate = hasDoubled ? strongestNear(doubledBpm, 6) : QPair<int, double>(0, 0.0);
        double doubledSupport = doubledCandidate.second;
        const int tripletBpm = qRound(finalBpm * 1.5);
        const double tripletSupport = (tripletBpm >= kMinBpm && tripletBpm <= kMaxBpm) ? supportFor(tripletBpm) : 0.0;

        if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm && qAbs(doubledCandidate.first - autoCorrConsensus) <= 5)
            doubledSupport *= 1.10;
        if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm && qAbs(doubledCandidate.first - autoCorrLowBpm) <= 5)
            doubledSupport *= 1.08;

        if (doubledSupport >= supportFor(finalBpm) * 0.34 && doubledSupport >= tripletSupport * 0.92)
            finalBpm = doubledCandidate.first;
    }

    if (finalBpm >= 70 && finalBpm <= 82) {
        const int doubled = finalBpm * 2;
        if (doubled <= kMaxBpm) {
            const double baseSupport = supportFor(finalBpm);
            QPair<int, double> doubledCandidate = strongestNear(doubled, 6);
            double doubledSupport = doubledCandidate.second;
            const int triplet = qRound(finalBpm * 1.5);
            const double tripletSupport = (triplet >= kMinBpm && triplet <= kMaxBpm) ? supportFor(triplet) : 0.0;

            if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm && qAbs(doubledCandidate.first - autoCorrConsensus) <= 5)
                doubledSupport *= 1.10;
            if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm && qAbs(doubledCandidate.first - autoCorrLowBpm) <= 5)
                doubledSupport *= 1.08;

            if (doubledSupport >= baseSupport * 0.34 && doubledSupport >= tripletSupport * 0.92)
                finalBpm = doubledCandidate.first;
        }
    }

    double finalExactBpm = static_cast<double>(finalBpm);
    if (finalBpm >= kMinBpm && finalBpm <= kMaxBpm && exactBpmWeight[finalBpm] > 0.0)
        finalExactBpm = exactBpmSum[finalBpm] / exactBpmWeight[finalBpm];

    // Refine exact BPM using least-squares linear regression over all detected onsets.
    // This is the standard approach used by aubio, librosa, and Essentia:
    //   Model: t_beat[k] = t0 + beatNumber[k] * T    (T = beat period in seconds)
    //   Solve for T using OLS over all N onsets -> precision ~T^2 / (track_duration * sqrt(N))
    // For 128 BPM over 5 min with ~500 onsets: precision < 0.001 BPM (vs 0.26 BPM from frame voting).
    if (finalBpm > 0 && !spectralFluxTimes.isEmpty()) {
        const QVector<int>& onsets = onsetsLow.isEmpty() ? onsetsFull : onsetsLow;
        const int N = onsets.size();
        if (N >= 8) {
            const double estimatedPeriodS = 60.0 / static_cast<double>(finalBpm);
            const qint64 t0ns = spectralFluxTimes.at(onsets.first());

            // Assign beat number to each onset by rounding to nearest beat.
            QVector<double> beatNum(N), tSec(N);
            for (int k = 0; k < N; ++k) {
                const int frame = onsets.at(k);
                const double tS = (frame < spectralFluxTimes.size())
                        ? (spectralFluxTimes.at(frame) - t0ns) * 1.0e-9
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

    if (p->bpm > 0 && m_ExactBpm > 0.0 && !phaseOnsets.isEmpty() && !spectralFluxTimes.isEmpty()) {
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
                    const qint64 tMs = spectralFluxTimes.at(onsetIdx) / 1000000LL;
                    double wrap = std::fmod(static_cast<double>(tMs - candidatePhaseMs), beatMs);
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
        int beatAnchorIdx = -1;
        float strongestAnchor = 0.0f;
        for (float value : anchorEnv)
            strongestAnchor = qMax(strongestAnchor, value);

        for (int i = 0; i < anchorEnv.size(); ++i) {
            if (anchorEnv.at(i) > strongestAnchor * 0.95f) {
                beatAnchorIdx = i;
                break;
            }
        }

        if (beatAnchorIdx >= 0 && p->bpm > 0 && beatAnchorIdx < spectralFluxTimes.size() && spectralFluxTimes.at(beatAnchorIdx) > 0) {
            const qint64 anchorMs = spectralFluxTimes.at(beatAnchorIdx) / 1000000LL;
            const double beatMs = 60000.0 / qMax(1e-6, m_ExactBpm);
            phaseMs = static_cast<int>(std::fmod(static_cast<double>(anchorMs), beatMs));
            if (phaseMs < 0)
                phaseMs += qMax(1, qRound(beatMs));
        }
    }

    m_BeatPosition = QTime(0, 0).addMSecs(qMax(0, phaseMs));

    // Compute first significant energy position, snapped to the beat grid.
    // Find the first frame where RMS crosses a significant threshold, then snap
    // to the nearest beat using the detected BPM and phase.
    {
        const double significantThreshold = qMax(averageRms * 0.5, kSilenceRmsThreshold * 3);
        const int frameMs = qMax(1, qRound(1000.0 / kAnalysisFrameRate));
        int firstSignificantFrame = -1;

        for (int i = 0; i < frameRms.size(); ++i) {
            if (frameRms.at(i) >= significantThreshold) {
                firstSignificantFrame = i;
                break;
            }
        }

        if (firstSignificantFrame >= 0 && p->bpm > 0 && m_ExactBpm > 0.0) {
            const double beatMs = 60000.0 / m_ExactBpm;
            const int energyMs = firstSignificantFrame * frameMs;
            // Snap to nearest beat: round((energyMs - phaseMs) / beatMs) * beatMs + phaseMs
            const double beatIndex = std::round((static_cast<double>(energyMs) - static_cast<double>(phaseMs)) / beatMs);
            const int snappedMs = static_cast<int>(qRound(beatIndex * beatMs + phaseMs));
            m_beatStartPosition = QTime(0, 0).addMSecs(qMax(0, snappedMs));
            qDebug() << Q_FUNC_INFO << "[m_beatStartPosition]:" << m_beatStartPosition << "Snapped to nearest beat from first significant energy position";
        } else if (firstSignificantFrame >= 0) {
            // No BPM detected - use the raw position
            m_beatStartPosition = QTime(0, 0).addMSecs(firstSignificantFrame * frameMs);
            qDebug() << Q_FUNC_INFO << "[m_beatStartPosition]:" << m_beatStartPosition << "No BPM detected, using first significant energy position";
        } else {
            // No significant energy found - fallback to beat position
            m_beatStartPosition = m_BeatPosition;
            qDebug() << Q_FUNC_INFO << "[m_beatStartPosition]:" << m_beatStartPosition << "No significant energy found, falling back to beat position";
        }
    }

    {
        QMutexLocker cacheLocker(&tempoCacheMutex);
        tempoCache.insert(cacheKey, qMakePair(qBound(0, finalBpm, kMaxBpm), QTime(0, 0).msecsTo(m_BeatPosition)));
    }

    qDebug() << Q_FUNC_INFO << "Estimated BPM:" << p->bpm << "exactBpm:" << m_ExactBpm
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

void TrackAnalyzer::loadThreadFinished()
{
    bool inProgress;
    {
        QMutexLocker locker(&p->mutex);
        inProgress = p->inProgress;
    }

    // If asyncOpen() failed early and already called need_finish(), finalizeAnalysis()
    // may have already run and set inProgress=false. Guard against double-finalization.
    if (!inProgress)
        return;

    start();
}
