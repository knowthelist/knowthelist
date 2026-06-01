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
    }

    data.peakRms = peakRms;
    data.averageRms = data.frameRms.isEmpty() ? 0.0 : sumRms / static_cast<double>(data.frameRms.size());

    const float envelopePeak = qMax(peakRms, 1e-6f);
    data.envelope.reserve(data.frameRms.size());
    for (float rms : data.frameRms)
        data.envelope.append(qBound(0.0f, rms / envelopePeak, 1.0f));

      qDebug() << "Silence threshold (fixed RMS):" << kSilenceRmsThreshold
                << "firstActiveFrame:" << firstActiveFrame
                << "lastActiveFrame:" << lastActiveFrame;

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
    bool tempoWindowStarted = false;
    int tempoScanDurationSeconds = 24;
    QTimer* tempoTimeout;
    bool finishQueued = false;
    bool shuttingDown = false;
    bool inProgress = false;
    TrackAnalyzer::modeType analysisMode = TrackAnalyzer::STANDARD;
    QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    QList<qint64> spectralFluxTimes;
    QVector<float> envelope;
    QTime analysisStartPosition = QTime(0, 0);
    QTime analysisEndPosition = QTime(0, 0);
    QTime analysisBeatActivityEndPosition = QTime(0, 0);
    double analysisGainDb = TrackAnalyzer::GAIN_INVALID;
    QUrl currentUrl;
};

TrackAnalyzer::TrackAnalyzer(QWidget* parent)
    : QWidget(parent)
    , p(new TrackAnalyzer_Private)
    , audioBackend(std::make_unique<JuceAudioBackend>())
{
    qDebug() << Q_FUNC_INFO << "Creating TrackAnalyzer";

    p->tempoTimeout = new QTimer(this);
    p->tempoTimeout->setSingleShot(true);
    connect(p->tempoTimeout, &QTimer::timeout, this, [this]() {
        if (p->analysisMode == TEMPO)
            need_finish();
    });

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

    p->tempoTimeout->stop();
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
    locker.unlock();
    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void TrackAnalyzer::asyncOpen(QUrl url)
{
    if (!audioBackend) {
        if (p->analysisMode == TEMPO)
            emit finishTempo();
        else if (p->analysisMode == ENVELOPE)
            emit finishEnvelope();
        else
            emit finishGain();
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
        if (p->analysisMode == TEMPO)
            emit finishTempo();
        else if (p->analysisMode == ENVELOPE)
            emit finishEnvelope();
        else
            emit finishGain();
        return;
    }

    QTime duration = audioBackend->getDuration();
    TrackAnalysisData analysis = scanAudioFileCached(url);
    if (analysis.durationMs > 0.0)
        duration = QTime(0, 0).addMSecs(static_cast<int>(qRound(analysis.durationMs)));

    {
        QMutexLocker locker(&p->mutex);
        p->spectralFlux = analysis.spectralFlux;
        p->spectralFluxLow = analysis.spectralFluxLow;
        p->spectralFluxTimes = analysis.spectralFluxTimes;
        p->envelope = analysis.envelope;
        p->analysisStartPosition = analysis.startPosition;
        p->analysisEndPosition = analysis.endPosition;
        p->analysisBeatActivityEndPosition = analysis.beatActivityEndPosition;
        p->analysisGainDb = analysis.gainDb;
        m_GainDB = analysis.gainDb;
        m_StartPosition = analysis.startPosition;
        m_EndPosition = analysis.endPosition;
        m_BeatActivityEndPosition = analysis.beatActivityEndPosition;
        m_MaxPosition = duration;
        m_envelope = analysis.envelope;
    }

    qDebug() << Q_FUNC_INFO << "File loaded: duration=" << duration
             << "gainDb=" << analysis.gainDb
             << "start=" << analysis.startPosition
             << "end=" << analysis.endPosition
             << "beatActivityEnd=" << analysis.beatActivityEndPosition
             << "frames=" << analysis.spectralFlux.size();

    emit finishGain();
}

void TrackAnalyzer::start()
{
    qDebug() << Q_FUNC_INFO << "Starting analysis, mode=" << p->analysisMode;

    switch (p->analysisMode) {
    case STANDARD:
        // Gain analysis happens during load
        break;
    case TEMPO:
        p->tempoWindowStarted = true;
        p->tempoTimeout->start(p->tempoScanDurationSeconds * 1000);
        detectTempo();
        need_finish();
        break;
    case ENVELOPE:
        need_finish();  // data is already in p->envelope from asyncOpen(); just trigger finalizeAnalysis
        break;
    }
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
    return m_StartPosition;
}

QTime TrackAnalyzer::endPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_EndPosition;
}

QTime TrackAnalyzer::beatPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_BeatPosition;
}

QTime TrackAnalyzer::beatActivityEndPosition()
{
    QMutexLocker locker(&p->mutex);
    return m_BeatActivityEndPosition;
}

int TrackAnalyzer::bpm()
{
    QMutexLocker locker(&p->mutex);
    return p->bpm;
}

QVector<float> TrackAnalyzer::amplitudeEnvelope() const
{
    return m_envelope;
}

void TrackAnalyzer::setMode(modeType mode)
{
    QMutexLocker locker(&p->mutex);
    p->analysisMode = mode;
    qDebug() << Q_FUNC_INFO << "Analysis mode set to:" << mode;
}

void TrackAnalyzer::setPosition(QTime position)
{
    if (audioBackend) {
        audioBackend->seek(position);
    }
}

int TrackAnalyzer::tempoScanDurationSeconds() const
{
    return p->tempoScanDurationSeconds;
}

void TrackAnalyzer::setTempoScanDurationSeconds(int seconds)
{
    p->tempoScanDurationSeconds = seconds;
    qDebug() << Q_FUNC_INFO << "Tempo scan duration set to:" << seconds << "seconds";
}

QTime TrackAnalyzer::length()
{
    QMutexLocker locker(&p->mutex);
    return m_MaxPosition;
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
    modeType mode;
    {
        QMutexLocker locker(&p->mutex);
        mode = p->analysisMode;
        p->finishQueued = false;
        p->tempoTimeout->stop();
        if (mode == TEMPO)
            p->tempoWindowStarted = false;
        p->inProgress = false;
        m_finished = true;
    }

    qDebug() << Q_FUNC_INFO << "Analysis mode=" << mode;

    // Emit outside the analyzer mutex: slots may query analyzer state and
    // would deadlock if finalizeAnalysis keeps the lock while emitting.
    switch (mode) {
    case STANDARD:
        emit finishGain();
        break;
    case TEMPO:
        emit finishTempo();
        break;
    case ENVELOPE:
        emit finishEnvelope();
        break;
    }
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
    QUrl currentUrl;
    {
        QMutexLocker locker(&p->mutex);
        // Skip if BPM already detected for this session
        if (p->bpmDetected)
            return;
        spectralFlux = p->spectralFlux;
        spectralFluxLow = p->spectralFluxLow;
        spectralFluxTimes = p->spectralFluxTimes;
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
            p->bpmDetected = true;
            return;
        }
    }

    if (spectralFlux.isEmpty()) {
        QMutexLocker locker(&p->mutex);
        p->bpm = 0;
        m_BeatPosition = m_StartPosition;
        return;
    }

    if (spectralFluxLow.size() != spectralFlux.size())
        spectralFluxLow = spectralFlux;

    const QList<float> fullEnv = buildOnsetEnvelope(spectralFlux, 12, 1.35f);
    const QList<float> lowEnv = buildOnsetEnvelope(spectralFluxLow, 14, 1.20f);

    const int minDistance = qMax(1, qRound((kAnalysisFrameRate * 60.0) / 240.0));
    const QVector<int> onsetsFull = pickOnsets(fullEnv, minDistance);
    const QVector<int> onsetsLow = pickOnsets(lowEnv, minDistance + 2);

    QVector<double> score(kMaxBpm + 1, 0.0);

    auto voteTempo = [&](const QVector<int>& onsets, const QList<float>& env, double weight, bool lowBandSource) {
        for (int i = 0; i < onsets.size(); ++i) {
            const int base = onsets.at(i);
            const float baseWeight = qMax(0.01f, env.at(base));
            const int upper = qMin(onsets.size(), i + 18);
            for (int j = i + 1; j < upper; ++j) {
                const int delta = onsets.at(j) - base;
                if (delta <= 0)
                    continue;

                double bpm = (static_cast<double>(kAnalysisFrameRate) * 60.0) / static_cast<double>(delta);
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
        const double lag = (static_cast<double>(kAnalysisFrameRate) * 60.0) / bpmBin;
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
    const int autoCorrBpm = qRound(AutoCorrelation(combinedEnv, combinedEnv.count(), kMinBpm, kMaxBpm, kAnalysisFrameRate));
    const int autoCorrLowBpm = qRound(AutoCorrelation(lowEnv, lowEnv.count(), kMinBpm, kMaxBpm, kAnalysisFrameRate));

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

    {
        QMutexLocker locker(&p->mutex);
        p->bpm = qBound(0, finalBpm, kMaxBpm);
        p->bpmDetected = true;
    }

    int beatAnchorIdx = -1;
    const QList<float>& anchorEnv = lowEnv.isEmpty() ? fullEnv : lowEnv;
    float strongestAnchor = 0.0f;
    for (float value : anchorEnv)
        strongestAnchor = qMax(strongestAnchor, value);

    if (strongestAnchor > 0.0f) {
        const float earlyThreshold = strongestAnchor * 0.60f;
        const int earlyLimit = qMin(anchorEnv.size(), static_cast<int>(kAnalysisFrameRate * 20));
        for (int i = 1; i < earlyLimit - 1; ++i) {
            if (anchorEnv.at(i) >= earlyThreshold
                && anchorEnv.at(i) >= anchorEnv.at(i - 1)
                && anchorEnv.at(i) > anchorEnv.at(i + 1)) {
                beatAnchorIdx = i;
                break;
            }
        }
    }

    if (beatAnchorIdx < 0) {
        for (int i = 0; i < anchorEnv.size(); ++i) {
            if (anchorEnv.at(i) > strongestAnchor * 0.95f) {
                beatAnchorIdx = i;
                break;
            }
        }
    }

    if (beatAnchorIdx >= 0) {
        if (p->bpm > 0 && beatAnchorIdx < spectralFluxTimes.size() && spectralFluxTimes.at(beatAnchorIdx) > 0) {
            const qint64 anchorMs = spectralFluxTimes.at(beatAnchorIdx) / 1000000LL;
            const qint64 beatMs = qMax<qint64>(1LL, 60000LL / static_cast<qint64>(p->bpm));
            const qint64 phaseMs = anchorMs % beatMs;
            m_BeatPosition = QTime(0, 0).addMSecs(static_cast<int>(phaseMs));
        } else {
            const qint64 offsetMs = static_cast<qint64>((1000.0 * beatAnchorIdx) / kAnalysisFrameRate);
            m_BeatPosition = m_StartPosition.addMSecs(static_cast<int>(offsetMs));
        }
    } else {
        m_BeatPosition = m_StartPosition;
    }

    {
        QMutexLocker cacheLocker(&tempoCacheMutex);
        tempoCache.insert(cacheKey, qMakePair(qBound(0, finalBpm, kMaxBpm), QTime(0, 0).msecsTo(m_BeatPosition)));
    }

    qDebug() << Q_FUNC_INFO << "Estimated BPM:" << p->bpm << "frames:" << spectralFlux.size()
             << "onsetsFull:" << onsetsFull.size() << "onsetsLow:" << onsetsLow.size();
}

float TrackAnalyzer::AutoCorrelation(QList<float> buffer, int frames, int minBpm, int maxBpm, int sampleRate)
{
    float maxCorr = 0.0f;
    int maxLag = 0;
    const int maxOffset = sampleRate * 60 / minBpm;
    const int minOffset = sampleRate * 60 / maxBpm;
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
        return sampleRate * 60.0f / maxLag;

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
    // Analysis load thread finished
    start();
}
