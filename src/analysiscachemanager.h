#ifndef ANALYSISCACHEMANAGER_H
#define ANALYSISCACHEMANAGER_H

#include <QObject>
#include <QUrl>
#include <QVector>
#include <QString>
#include <QTime>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QByteArray>
#include <QDataStream>

class AnalysisCacheManager : public QObject
{
    Q_OBJECT
public:
    explicit AnalysisCacheManager(QObject *parent = nullptr);

    // Tempo caching methods
    bool ensureTempoCacheTable() const;
    
    void storeCachedTempo(const QUrl& url, int bpm, double exactBpm,
                          int startPositionMs, int endPositionMs,
                          int beatStartPosMs, int beatPhasePosMs, int beatEndPosMs,
                          int barAnchorPosMs, double barPhaseConfidence);
    
    struct CachedTempo {
        bool valid;
        int bpm;
        double exactBpm;

        // Track boundaries (ms from T=0)
        int startPositionMs;     // first frame > silence threshold
        int endPositionMs;       // last audible frame, including quiet outros

        // Beat-grid phase (ms from T=0)
        int beatStartPositionMs;  // first detected beat in content zone
        int beatPhasePositionMs;  // phase offset within beat period for beatline rendering
        int beatEndPositionMs;    // beat flux drops below + 3s silent window
        int barAnchorPositionMs;  // absolute 4/4 downbeat anchor, or first beat fallback
        double barPhaseConfidence; // confidence in the 4/4 downbeat estimate

        // File-based invalidation key (path|size|mtime)
        QString analysisCacheKey;

        CachedTempo() : valid(false), bpm(0), exactBpm(0.0),
                        startPositionMs(0), endPositionMs(0),
                        beatStartPositionMs(0), beatPhasePositionMs(0), beatEndPositionMs(0),
                        barAnchorPositionMs(0), barPhaseConfidence(0.0) {}
    };
    
    CachedTempo loadCachedTempo(const QUrl& url);

    // Check cache validity: DB row exists AND file key matches (no stale cache on file replacement).
    bool hasValidCache(const QUrl& url, const QString& currentKey) const;

    // Remove all persisted analysis for one file so it can be analyzed again.
    bool removeCachedAnalysis(const QUrl& url);

    // Envelope caching methods
    void storeCachedEnvelope(const QUrl& url, const QVector<float>& samples, int durationMs);

    // Helper to set analysis_cache_key after storeCachedTempo() (key is passed separately)
    void setCachedKey(const QUrl& url, const QString& key);
    struct CachedEnvelope {
        bool valid;
        QVector<float> samples;
        int durationMs;

        CachedEnvelope() : valid(false), durationMs(0) {}
    };
    CachedEnvelope loadCachedEnvelope(const QUrl& url);

private:
    // Utility functions moved into private methods or kept as static helpers if appropriate
    static QByteArray encodeEnvelopeSamples(const QVector<float>& samples);
    static QVector<float> decodeEnvelopeSamples(const QByteArray& compressed);
    static bool addColumnIfMissing(QSqlDatabase& db, const QString& columnName, const QString& alterSql);

private:
    // Internal state or connections if needed later
};

#endif // ANALYSISCACHEMANAGER_H
