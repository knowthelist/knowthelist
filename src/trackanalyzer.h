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
#ifndef TRACKANALYSER_H
#define TRACKANALYSER_H

#include "analysiscachemanager.h"
#include <QtCore>
#include <QWidget>
#include <memory>
#include <cstdint>

class JuceAudioBackend;

class TrackAnalyzer : public QWidget
{
    Q_OBJECT
public:
    TrackAnalyzer(QWidget* parent = nullptr);
    ~TrackAnalyzer();
    static void clearRuntimeCaches();

    bool prepare();
    void open(QUrl url);
    void setCachedBpm(int bpm);
    bool close();

    double gainDB();
    double gainFactor();
    QTime startPosition();
    QTime endPosition();
    QTime beatPosition();
    QTime beatEndPosition();
    QTime beatStartPosition();
    int bpm();
    double exactBpm();
    QVector<float> amplitudeEnvelope() const;
    bool finished() { return m_finished; }
    void setPosition(QTime position);

    QTime length();
    static const int GAIN_INVALID = -99;

    void need_finish();

Q_SIGNALS:
    void finishGain();
    void finishTempo();
    void finishEnvelope();

private slots:
    void finalizeAnalysis();

private:
    struct TrackAnalyzer_Private* p;
    std::unique_ptr<JuceAudioBackend> audioBackend;

    // Tempo / envelope caching backed by AnalysisCacheManager (SQLite)
    // Sole owner of tempo/envelope caching lifecycle (read + write).
    std::shared_ptr<AnalysisCacheManager> m_analysisCache;

    // Computed in asyncOpen() and persisted in finalizeAnalysis().
    QString m_lastCacheKey;                 // file-hash key for cache row invalidation
    std::uint64_t m_cacheEpochAtOpen = 0;   // guards against stale writes after cache reset

    double m_GainDB = GAIN_INVALID;
    double m_ExactBpm = 0.0;
    // track-level temporal boundaries (output state — one source of truth)
    QTime m_trackEffectiveStart = QTime(0, 0);      // first frame > silence threshold (excludes intro)
    QTime m_trackEffectiveEnd = QTime(0, 0);         // last frame before trailing silence (excludes fadeout tail)
    QTime m_beatStopPosition = QTime(0, 0);           // beat flux drops below + 3s silent window
    // beat-grid phase (output state)
    QTime m_BeatPosition = QTime(0, 0);               // phase offset within detected period
    QTime m_beatStartPosition = QTime(0, 0);          // first detected beat in content zone
    QTime m_trackDuration = QTime(0, 0);            // full track length incl. ALL silence from audioBackend
    // scalar analyzer results
    bool m_finished = false;
    QVector<float> m_envelope;

    void detectTempo();

    AnalysisCacheManager::CachedTempo loadCachedTempo(const QUrl& url);
    bool hasValidCache(const QUrl& url, const QString& cacheKey) const;
    void     storeCachedTempo();            // writes from current analyzer state — no params
    void     storeCachedEnvelope(int durationMs);
    AnalysisCacheManager::CachedEnvelope loadCachedEnvelope(const QUrl& url);

    float AutoCorrelation(QList<float> buffer, int frames, int minBpm, int maxBpm, double sampleRate);

    void cleanup();
    void asyncOpen(QUrl url);
};



#endif // TRACKANALYSER_H
