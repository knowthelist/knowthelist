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
    bool ensureTempoCacheTable();
    void storeCachedTempo(const QUrl& url, int bpm, double exactBpm, const QTime& beatPosition,
                          const QTime& cueStartPosition = QTime());
    struct CachedTempo {
        bool valid;
        int bpm;
        int beatOffsetMs;
        int cueStartMs;
        double exactBpm;

        CachedTempo() : valid(false), bpm(0), beatOffsetMs(0), cueStartMs(0), exactBpm(0.0) {}
    };
    CachedTempo loadCachedTempo(const QUrl& url);

    // Envelope caching methods
    void storeCachedEnvelope(const QUrl& url, const QVector<float>& samples, int durationMs);
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