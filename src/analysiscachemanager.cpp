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

bool AnalysisCacheManager::ensureTempoCacheTable() const
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    // Schema v12: position-based tempo cache (replaces beat_offset_ms / cueStartMs)
    const QString baseTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS analysis_cache ("
        "url VARCHAR(120) PRIMARY KEY,"
        "bpm INTEGER,"
        "beat_offset_ms INTEGER DEFAULT 0,"         // deprecated, kept for migration compat
        "cue_start_ms INTEGER DEFAULT 0,"           // deprecated, kept for migration compat
        "start_position_ms INTEGER DEFAULT 0,"
        "end_position_ms INTEGER DEFAULT 0,"
        "beat_start_position_ms INTEGER DEFAULT 0,"
        "beat_end_position_ms INTEGER DEFAULT 0,"
        "changedate INTEGER,"
        "analysis_version INTEGER DEFAULT 12,"
        "envelope_version INTEGER DEFAULT 0,"
        "exact_bpm REAL DEFAULT 0.0,"
        "envelope_data BLOB,"
        "analysis_cache_key VARCHAR(360) DEFAULT ''"   // file key for stale-cache detection
        ")");
    if (!q.exec(baseTable)) {
        return false;
    }

    // Ensure new v12 columns exist (for existing DBs migrated from older schemas)
    QList<QPair<QString, QString>> colsToAdd = {
        {"start_position_ms",       "ALTER TABLE analysis_cache ADD COLUMN start_position_ms INTEGER DEFAULT 0"},
        {"end_position_ms",         "ALTER TABLE analysis_cache ADD COLUMN end_position_ms INTEGER DEFAULT 0"},
        {"beat_start_position_ms",  "ALTER TABLE analysis_cache ADD COLUMN beat_start_position_ms INTEGER DEFAULT 0"},
        {"beat_end_position_ms",    "ALTER TABLE analysis_cache ADD COLUMN beat_end_position_ms INTEGER DEFAULT 0"},
        {"analysis_cache_key",      "ALTER TABLE analysis_cache ADD COLUMN analysis_cache_key VARCHAR(360) DEFAULT ''"},
    };
    for (const auto &pair : colsToAdd) {
        if (!addColumnIfMissing(db, pair.first, pair.second))
            return false;
    }

    return true;
}

AnalysisCacheManager::CachedTempo AnalysisCacheManager::loadCachedTempo(const QUrl& url)
{
    CachedTempo cached;
    if (!ensureTempoCacheTable())
        return cached;

    QSqlQuery q(QSqlDatabase::database());
    // v12 SELECT with new positional fields, plus analysis_cache_key for hasValidCache
    q.prepare("SELECT bpm, exact_bpm, start_position_ms, end_position_ms, "
              "beat_start_position_ms, beat_end_position_ms, analysis_version, analysis_cache_key "
              "FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());

    // If the new columns exist but query fails (very old schema), fall back to legacy SELECT.
    if (!q.exec()) {
        q.prepare("SELECT bpm, beat_offset_ms, cue_start_ms, exact_bpm, 0, analysis_version "
                  "FROM analysis_cache WHERE url = :url");
        q.bindValue(":url", url.toLocalFile());
        if (!q.exec() || !q.next())
            return cached;

        const int storedBpm = q.value(0).toInt();
        if (storedBpm <= 0)
            return cached;

        cached.valid = true;
        cached.bpm = storedBpm;
        cached.exactBpm = q.value(3).toDouble() > 0.0 ? q.value(3).toDouble() : static_cast<double>(storedBpm);
        // Old offset: beat_offset_ms is meaningless without the original analysis context — keep it empty
        cached.startPositionMs   = 0;
        cached.endPositionMs     = 0;
        cached.beatStartPositionMs = q.value(1).toInt();    // beat_offset_ms (best-effort)
        cached.beatEndPositionMs = 0;

        return cached;
    }

    if (!q.next())
        return cached;

    const int storedBpm   = q.value(0).toInt();
    const int version     = q.value(6).toInt();

    if (storedBpm <= 0)
        return cached;

    // Schema v12+: new position-based fields
    if (version >= 12) {
        cached.valid         = true;
        cached.bpm           = storedBpm;
        cached.exactBpm      = q.value(1).toDouble() > 0.0 ? q.value(1).toDouble() : static_cast<double>(storedBpm);
        cached.startPositionMs     = q.value(2).toInt();
        cached.endPositionMs       = q.value(3).toInt();
        cached.beatStartPositionMs = q.value(4).toInt();
        cached.beatEndPositionMs   = q.value(5).toInt();
        cached.analysisCacheKey  = q.value(7).toString();
    }
    // Legacy v11: partial reconstruction from old offset fields
    else {
        cached.valid         = true;
        cached.bpm           = storedBpm;
        cached.exactBpm      = (q.value(1).toDouble() > 0.0) ? q.value(1).toDouble() : static_cast<double>(storedBpm);
        cached.startPositionMs     = q.value(5).toInt();   // cueStartMs from legacy row
        cached.endPositionMs       = 0;                      // was never stored in v11
        cached.beatStartPositionMs = q.value(1).toInt();    // beat_offset_ms (approximation)
        cached.beatEndPositionMs   = 0;
    }

    return cached;
}

bool AnalysisCacheManager::hasValidCache(const QUrl& url, const QString& currentKey) const
{
    if (!ensureTempoCacheTable())
        return false;
    if (currentKey.isEmpty())
        return false;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("SELECT analysis_cache_key, analysis_version FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());

    if (!q.exec() || !q.next())
        return false;

    const QString storedKey = q.value(0).toString();
    const int version       = q.value(1).toInt();

    // Must be v12+ schema AND key must match exactly
    return version >= 12 && !storedKey.isEmpty() && (storedKey == currentKey);
}

void AnalysisCacheManager::storeCachedTempo(const QUrl& url, int bpm, double exactBpm,
                                            int startPositionMs, int endPositionMs,
                                            int beatStartPosMs, int beatEndPosMs)
{
    if (bpm <= 0)
        return;
    if (!ensureTempoCacheTable())
        return;

    // We store the analysis_cache_key later via a separate column write.
    // Note: the key parameter is passed separately via setCachedKey().
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(
        "INSERT OR REPLACE INTO analysis_cache ("
        "url, bpm, exact_bpm, start_position_ms, end_position_ms, "
         "beat_start_position_ms, beat_end_position_ms, changedate, "
         "analysis_version, envelope_version) "
        "VALUES (:url, :bpm, :exact_bpm, :start_pos, :end_pos, "
         ":beat_start, :beat_end, strftime('%s','now'), 12, "
         "COALESCE((SELECT envelope_version FROM analysis_cache WHERE url = :url), 0))"
    );
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":bpm", bpm);
    q.bindValue(":exact_bpm", exactBpm);
    q.bindValue(":start_pos", startPositionMs);
    q.bindValue(":end_pos", endPositionMs);
    q.bindValue(":beat_start", beatStartPosMs);
    q.bindValue(":beat_end", beatEndPosMs);
    q.exec();
}

void AnalysisCacheManager::setCachedKey(const QUrl& url, const QString& key)
{
    if (key.isEmpty())
        return;
    QSqlQuery q(QSqlDatabase::database());
    q.prepare("UPDATE analysis_cache SET analysis_cache_key = :key WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":key", key);
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
