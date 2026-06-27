#include "analysiscachemanager.h"
#include <QIODevice>
#include <QSqlDatabase>
#include <QtGlobal>

AnalysisCacheManager::AnalysisCacheManager(QObject *parent) : QObject(parent) {}

// --- Utility Functions ---

QByteArray AnalysisCacheManager::encodeEnvelopeSamples(const QVector<float>& samples)
{
    QByteArray payload;
    payload.reserve(samples.size() * static_cast<int>(sizeof(float)) + 16);
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_12);
    out << samples;
    return qCompress(payload, 6);
}

QVector<float> AnalysisCacheManager::decodeEnvelopeSamples(const QByteArray& compressed)
{
    QVector<float> samples;
    if (compressed.isEmpty())
        return samples;

    QByteArray payload = qUncompress(compressed);
    if (payload.isEmpty())
        return samples;

    QDataStream in(&payload, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_5_12);
    in >> samples;
    return samples;
}

bool AnalysisCacheManager::addColumnIfMissing(QSqlDatabase& db, const QString& columnName, const QString& alterSql)
{
    QSqlQuery q(db);
    if (!q.exec("PRAGMA table_info(analysis_cache)"))
        return false;

    while (q.next()) {
        if (q.value(1).toString() == columnName)
            return true;
    }

    QSqlQuery alterQuery(db);
    return alterQuery.exec(alterSql);
}

// --- Cache Management Logic ---

bool AnalysisCacheManager::ensureTempoCacheTable()
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS analysis_cache ("
                "url VARCHAR(120) PRIMARY KEY,"
                "bpm INTEGER,"
                "beat_offset_ms INTEGER,"
                "changedate INTEGER );")) {
        return false;
    }

    if (!addColumnIfMissing(db, "analysis_version", "ALTER TABLE analysis_cache ADD COLUMN analysis_version INTEGER DEFAULT 0"))
        return false;

    if (!addColumnIfMissing(db, "envelope_version", "ALTER TABLE analysis_cache ADD COLUMN envelope_version INTEGER DEFAULT 0"))
        return false;

    if (!addColumnIfMissing(db, "exact_bpm", "ALTER TABLE analysis_cache ADD COLUMN exact_bpm REAL DEFAULT 0.0"))
        return false;

    if (!addColumnIfMissing(db, "cue_start_ms", "ALTER TABLE analysis_cache ADD COLUMN cue_start_ms INTEGER DEFAULT 0"))
        return false;

    if (!addColumnIfMissing(db, "envelope_data", "ALTER TABLE analysis_cache ADD COLUMN envelope_data BLOB"))
        return false;

    if (!addColumnIfMissing(db, "envelope_duration_ms", "ALTER TABLE analysis_cache ADD COLUMN envelope_duration_ms INTEGER DEFAULT 0"))
        return false;

    return true;
}

AnalysisCacheManager::CachedTempo AnalysisCacheManager::loadCachedTempo(const QUrl& url)
{
    CachedTempo cached;
    if (!ensureTempoCacheTable())
        return cached;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("SELECT bpm, beat_offset_ms, exact_bpm, cue_start_ms, analysis_version FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    if (!q.exec()) {
        // Older schema without exact_bpm
        q.prepare("SELECT bpm, beat_offset_ms, analysis_version FROM analysis_cache WHERE url = :url");
        q.bindValue(":url", url.toLocalFile());
        if (!q.exec())
            return cached;

        if (q.next()) {
            const int analysisVersion = q.value(2).toInt();
            const int cachedBpm = q.value(0).toInt();
            if (analysisVersion == 11 || (analysisVersion <= 0 && cachedBpm > 0)) { // Using kTempoCacheVersion=11 here for consistency
                cached.valid = true;
                cached.bpm = cachedBpm;
                cached.beatOffsetMs = q.value(1).toInt();
                cached.cueStartMs = 0;
                cached.exactBpm = static_cast<double>(cachedBpm);
            }
        }
        return cached;
    }

    if (q.next()) {
        const int cachedBpm = q.value(0).toInt();
        const int beatOffsetMs = q.value(1).toInt();
        const double cachedExactBpm = q.value(2).toDouble();
        const int cueStartMs = q.value(3).toInt();
        const int analysisVersion = q.value(4).toInt();
        if (analysisVersion == 11 || (analysisVersion <= 0 && cachedBpm > 0)) {
            cached.valid = true;
            cached.bpm = cachedBpm;
            cached.beatOffsetMs = beatOffsetMs;
            cached.cueStartMs = cueStartMs;
            cached.exactBpm = (cachedExactBpm > 0.0) ? cachedExactBpm : static_cast<double>(cachedBpm);
        }
    }

    return cached;
}

void AnalysisCacheManager::storeCachedTempo(const QUrl& url, int bpm, double exactBpm,
                                            const QTime& beatPosition, const QTime& cueStartPosition)
{
    if (bpm <= 0)
        return;
    if (!ensureTempoCacheTable())
        return;

    const int beatOffsetMs = QTime(0, 0).msecsTo(beatPosition);
    const int cueStartMs = cueStartPosition.isValid() && cueStartPosition > QTime(0, 0)
                               ? QTime(0, 0).msecsTo(cueStartPosition)
                               : 0;
    QSqlQuery q(QSqlDatabase::database());
    q.prepare("INSERT OR REPLACE INTO analysis_cache (url, bpm, beat_offset_ms, exact_bpm, cue_start_ms, changedate, analysis_version, envelope_version, envelope_data) "
              "VALUES (:url, :bpm, :beat_offset_ms, :exact_bpm, :cue_start_ms, strftime('%s','now'), 11, "
              "COALESCE((SELECT envelope_version FROM analysis_cache WHERE url = :url), 0), "
              "(SELECT envelope_data FROM analysis_cache WHERE url = :url))");
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":bpm", bpm);
    q.bindValue(":beat_offset_ms", beatOffsetMs);
    q.bindValue(":exact_bpm", exactBpm);
    q.bindValue(":cue_start_ms", cueStartMs);
    q.exec();
}

AnalysisCacheManager::CachedEnvelope AnalysisCacheManager::loadCachedEnvelope(const QUrl& url)
{
    CachedEnvelope cached;
    if (!ensureTempoCacheTable())
        return cached;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("SELECT envelope_version, envelope_data, envelope_duration_ms FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    if (!q.exec())
        return cached;

    if (q.next() && q.value(0).toInt() == 2) { // Using hardcoded version for simplicity here
        const QVector<float> decoded = decodeEnvelopeSamples(q.value(1).toByteArray());
        if (!decoded.isEmpty()) {
            cached.valid = true;
            cached.samples = decoded;
            cached.durationMs = qMax(0, q.value(2).toInt());
        }
    }

    return cached;
}

void AnalysisCacheManager::storeCachedEnvelope(const QUrl& url, const QVector<float>& samples, int durationMs)
{
    if (samples.isEmpty())
        return;
    if (!ensureTempoCacheTable())
        return;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("INSERT OR REPLACE INTO analysis_cache (url, bpm, beat_offset_ms, changedate, analysis_version, envelope_version, envelope_data, envelope_duration_ms) "
              "VALUES (:url, "
              "COALESCE((SELECT bpm FROM analysis_cache WHERE url = :url), 0), "
              "COALESCE((SELECT beat_offset_ms FROM analysis_cache WHERE url = :url), 0), "
              "strftime('%s','now'), "
              "COALESCE((SELECT analysis_version FROM analysis_cache WHERE url = :url), 0), "
              ":envelope_version, :envelope_data, :envelope_duration_ms)");
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":envelope_version", 2); // Using hardcoded version for simplicity here
    q.bindValue(":envelope_data", encodeEnvelopeSamples(samples));
    q.bindValue(":envelope_duration_ms", qMax(0, durationMs));
    q.exec();
}