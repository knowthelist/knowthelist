#include "analysiscachemanager.h"
#include <QFileInfo>
#include <QIODevice>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QtGlobal>

AnalysisCacheManager::AnalysisCacheManager(QObject *parent) : QObject(parent) {}

// --- Utility Functions ---

// Per-thread unique connection name so each thread has its own QSqlDatabase instance.
// The main thread's "CollectionDB" is just one of many connections in the pool; all point
// to the same SQLite file on disk, which WAL mode can handle concurrently.
static QString getDbFilePath()
{
    const QString pathName = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    QDir dir(pathName);
    if (!dir.exists()) {
        dir.mkpath(pathName);
    }
    return QFileInfo(dir.absolutePath(), "collection.db").absoluteFilePath();
}

static QSqlDatabase collectionDb()
{
    // Each thread gets its own uniquely-named connection.
    const QString connName = QStringLiteral("CollectionDB_%1")
                                .arg(qulonglong(QThread::currentThreadId()), 0, 16);

    if (!QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(getDbFilePath());

        if (!db.open()) {
            qCritical() << "Failed to open database for thread"
                        << QThread::currentThreadId();
        } else {
            QSqlQuery pragma("PRAGMA journal_mode=WAL", db);
            (void)pragma.next();
        }
    }

    return QSqlDatabase::database(connName);
}

static constexpr int kEnvelopeCacheVersion = 2;
static constexpr int kTempoAnalysisVersion = 25;

static AnalysisCacheManager::CachedTempo buildLegacyCachedTempo(const QSqlQuery& query)
{
    AnalysisCacheManager::CachedTempo cached;

    const int storedBpm = query.value(0).toInt();
    if (storedBpm <= 0)
        return cached;

    const int legacyBeatOffsetMs = qMax(0, query.value(1).toInt());
    const int legacyCueStartMs = qMax(0, query.value(2).toInt());

    cached.valid = true;
    cached.bpm = storedBpm;
    cached.exactBpm = query.value(3).toDouble() > 0.0
            ? query.value(3).toDouble()
            : static_cast<double>(storedBpm);
    cached.startPositionMs = legacyCueStartMs;
    cached.endPositionMs = 0; // not available in legacy schema
    // cue_start_ms is the best absolute approximation for legacy rows.
    // beat_offset_ms is only phase and must not override cue_start_ms.
    cached.beatStartPositionMs = (legacyCueStartMs > 0) ? legacyCueStartMs : legacyBeatOffsetMs;
    cached.beatPhasePositionMs = legacyBeatOffsetMs;
    cached.beatEndPositionMs = 0;
    cached.barAnchorPositionMs = cached.beatStartPositionMs;
    cached.barPhaseConfidence = 0.0;

    return cached;
}

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
    if (!db.isOpen())
        return false;

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
    QSqlDatabase db = collectionDb();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    // Position-based tempo cache with beat and conservative 4/4 bar anchors.
    const QString baseTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS analysis_cache ("
        "url VARCHAR(120) PRIMARY KEY,"
        "bpm INTEGER,"
        "beat_offset_ms INTEGER DEFAULT 0,"         // deprecated, kept for migration compat
        "cue_start_ms INTEGER DEFAULT 0,"           // deprecated, kept for migration compat
        "start_position_ms INTEGER DEFAULT 0,"
        "end_position_ms INTEGER DEFAULT 0,"
        "beat_start_position_ms INTEGER DEFAULT 0,"
        "beat_phase_position_ms INTEGER DEFAULT 0,"
        "beat_end_position_ms INTEGER DEFAULT 0,"
        "bar_anchor_position_ms INTEGER DEFAULT 0,"
        "bar_phase_confidence REAL DEFAULT 0.0,"
        "changedate INTEGER,"
        "analysis_version INTEGER DEFAULT 25,"
        "envelope_version INTEGER DEFAULT 0,"
        "exact_bpm REAL DEFAULT 0.0,"
        "envelope_data BLOB,"
        "envelope_duration_ms INTEGER DEFAULT 0,"
        "analysis_cache_key VARCHAR(360) DEFAULT ''"   // file key for stale-cache detection
        ")");
    if (!q.exec(baseTable)) {
        return false;
    }

    // Ensure current positional columns exist for databases migrated from older schemas.
    QList<QPair<QString, QString>> colsToAdd = {
        {"start_position_ms",       "ALTER TABLE analysis_cache ADD COLUMN start_position_ms INTEGER DEFAULT 0"},
        {"end_position_ms",         "ALTER TABLE analysis_cache ADD COLUMN end_position_ms INTEGER DEFAULT 0"},
        {"beat_start_position_ms",  "ALTER TABLE analysis_cache ADD COLUMN beat_start_position_ms INTEGER DEFAULT 0"},
        {"beat_phase_position_ms",  "ALTER TABLE analysis_cache ADD COLUMN beat_phase_position_ms INTEGER DEFAULT 0"},
        {"beat_end_position_ms",    "ALTER TABLE analysis_cache ADD COLUMN beat_end_position_ms INTEGER DEFAULT 0"},
        {"bar_anchor_position_ms",  "ALTER TABLE analysis_cache ADD COLUMN bar_anchor_position_ms INTEGER DEFAULT 0"},
        {"bar_phase_confidence",    "ALTER TABLE analysis_cache ADD COLUMN bar_phase_confidence REAL DEFAULT 0.0"},
        {"envelope_duration_ms",    "ALTER TABLE analysis_cache ADD COLUMN envelope_duration_ms INTEGER DEFAULT 0"},
        {"exact_bpm",               "ALTER TABLE analysis_cache ADD COLUMN exact_bpm REAL DEFAULT 0.0"},
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

    QSqlQuery q(collectionDb());
    // Current positional fields, plus the file key used by hasValidCache().
    q.prepare("SELECT bpm, exact_bpm, start_position_ms, end_position_ms, "
              "beat_start_position_ms, beat_phase_position_ms, beat_offset_ms, beat_end_position_ms, "
              "bar_anchor_position_ms, bar_phase_confidence, analysis_version, analysis_cache_key "
              "FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());

    // If the new columns exist but query fails (very old schema), fall back to legacy SELECT.
    if (!q.exec()) {
        q.prepare("SELECT bpm, beat_offset_ms, cue_start_ms, exact_bpm, 0, analysis_version "
                  "FROM analysis_cache WHERE url = :url");
        q.bindValue(":url", url.toLocalFile());
        if (!q.exec() || !q.next())
            return cached;
        return buildLegacyCachedTempo(q);
    }

    if (!q.next())
        return cached;

    const int storedBpm   = q.value(0).toInt();
    const int version     = q.value(10).toInt();

    if (storedBpm <= 0)
        return cached;

    // Current position-based fields.
    if (version >= kTempoAnalysisVersion) {
        cached.valid         = true;
        cached.bpm           = storedBpm;
        cached.exactBpm      = q.value(1).toDouble() > 0.0 ? q.value(1).toDouble() : static_cast<double>(storedBpm);
        cached.startPositionMs     = q.value(2).toInt();
        cached.endPositionMs       = q.value(3).toInt();
        cached.beatStartPositionMs = q.value(4).toInt();
        cached.beatPhasePositionMs = qMax(0, q.value(5).toInt());
        const int legacyPhaseMs = qMax(0, q.value(6).toInt());
        if (cached.beatPhasePositionMs <= 0)
            cached.beatPhasePositionMs = legacyPhaseMs;
        cached.beatEndPositionMs   = q.value(7).toInt();
        cached.barAnchorPositionMs = qMax(0, q.value(8).toInt());
        cached.barPhaseConfidence = qBound(0.0, q.value(9).toDouble(), 1.0);
        cached.analysisCacheKey  = q.value(11).toString();
        if (cached.barAnchorPositionMs <= 0)
            cached.barAnchorPositionMs = cached.beatStartPositionMs;
        if (cached.startPositionMs > 0 && (cached.beatStartPositionMs <= 0 || cached.beatStartPositionMs < cached.startPositionMs))
            cached.beatStartPositionMs = cached.startPositionMs;
    }
    // Legacy v11: partial reconstruction from old offset fields
    else {
        QSqlQuery legacyQ(collectionDb());
        legacyQ.prepare("SELECT bpm, beat_offset_ms, cue_start_ms, exact_bpm, analysis_version "
                        "FROM analysis_cache WHERE url = :url");
        legacyQ.bindValue(":url", url.toLocalFile());
        if (!legacyQ.exec() || !legacyQ.next())
            return cached;
        cached = buildLegacyCachedTempo(legacyQ);
    }

    return cached;
}

bool AnalysisCacheManager::hasValidCache(const QUrl& url, const QString& currentKey) const
{
    if (!ensureTempoCacheTable())
        return false;
    if (currentKey.isEmpty())
        return false;

    QSqlQuery q(collectionDb());
    q.prepare("SELECT analysis_cache_key, analysis_version FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());

    if (!q.exec() || !q.next())
        return false;

    const QString storedKey = q.value(0).toString();
    const int version       = q.value(1).toInt();

    // Must be current analysis version AND key must match exactly.
    return version >= kTempoAnalysisVersion && !storedKey.isEmpty() && (storedKey == currentKey);
}

bool AnalysisCacheManager::removeCachedAnalysis(const QUrl& url)
{
    if (!ensureTempoCacheTable())
        return false;

    QSqlQuery q(collectionDb());
    q.prepare("DELETE FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    return q.exec();
}

void AnalysisCacheManager::storeCachedTempo(const QUrl& url, int bpm, double exactBpm,
                                            int startPositionMs, int endPositionMs,
                                            int beatStartPosMs, int beatPhasePosMs, int beatEndPosMs,
                                            int barAnchorPosMs, double barPhaseConfidence)
{
    if (bpm <= 0)
        return;
    if (!ensureTempoCacheTable())
        return;

    // We store the analysis_cache_key later via a separate column write.
    // Note: the key parameter is passed separately via setCachedKey().
    QSqlQuery q(collectionDb());
    q.prepare(
        "INSERT OR REPLACE INTO analysis_cache ("
        "url, bpm, exact_bpm, start_position_ms, end_position_ms, "
         "beat_start_position_ms, beat_phase_position_ms, beat_offset_ms, beat_end_position_ms, "
         "bar_anchor_position_ms, bar_phase_confidence, changedate, "
         "analysis_version, envelope_version) "
        "VALUES (:url, :bpm, :exact_bpm, :start_pos, :end_pos, "
         ":beat_start, :beat_phase, :beat_phase, :beat_end, :bar_anchor, :bar_confidence, "
         "strftime('%s','now'), 25, "
         "COALESCE((SELECT envelope_version FROM analysis_cache WHERE url = :url), 0))"
    );
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":bpm", bpm);
    q.bindValue(":exact_bpm", exactBpm);
    q.bindValue(":start_pos", startPositionMs);
    q.bindValue(":end_pos", endPositionMs);
    q.bindValue(":beat_start", beatStartPosMs);
    q.bindValue(":beat_phase", beatPhasePosMs);
    q.bindValue(":beat_end", beatEndPosMs);
    q.bindValue(":bar_anchor", qMax(0, barAnchorPosMs));
    q.bindValue(":bar_confidence", qBound(0.0, barPhaseConfidence, 1.0));
    q.exec();
}

void AnalysisCacheManager::setCachedKey(const QUrl& url, const QString& key)
{
    if (key.isEmpty())
        return;
    QSqlQuery q(collectionDb());
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

    QSqlQuery q(collectionDb());
    q.prepare("SELECT envelope_version, envelope_data, envelope_duration_ms FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    if (!q.exec())
        return cached;

    if (q.next() && q.value(0).toInt() == kEnvelopeCacheVersion) {
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

    QSqlQuery q(collectionDb());
    q.prepare(
        "INSERT INTO analysis_cache (url, changedate, envelope_version, envelope_data, envelope_duration_ms) "
        "VALUES (:url, strftime('%s','now'), :envelope_version, :envelope_data, :envelope_duration_ms) "
        "ON CONFLICT(url) DO UPDATE SET "
        "changedate = excluded.changedate, "
        "envelope_version = excluded.envelope_version, "
        "envelope_data = excluded.envelope_data, "
        "envelope_duration_ms = excluded.envelope_duration_ms"
    );
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":envelope_version", kEnvelopeCacheVersion);
    q.bindValue(":envelope_data", encodeEnvelopeSamples(samples));
    q.bindValue(":envelope_duration_ms", qMax(0, durationMs));
    q.exec();
}
