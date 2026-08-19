#include "playerbpmwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QImage>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>
#include <cmath>

namespace {
float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

QString formatTempoValue(double tempo)
{
    if (qAbs(tempo - qRound(tempo)) < 0.05)
        return QString::number(qRound(tempo));
    return QString::number(tempo, 'f', 1);
}

static QVector<float> buildVisibleEnvelope(const QVector<float>& timelineEnvelope,
                                           const QVector<quint8>& timelineKnown,
                                           double sampleIntervalMs,
                                           int bandWidth,
                                           int windowMs,
                                           int leftMs,
                                           int trackLenMs)
{
    const double intervalMs = qMax(0.001, sampleIntervalMs);
    const int targetSamples = qMax(2, bandWidth);
    QVector<float> envelope;
    envelope.resize(targetSamples);

    const float* envelopeData = timelineEnvelope.constData();
    const quint8* knownData = timelineKnown.constData();
    const int timelineCount = timelineEnvelope.size();

    float lastValidEnvelope = 0.0f;
    for (int k = timelineCount - 1; k >= 0; --k) {
        if (knownData[k]) {
            lastValidEnvelope = envelopeData[k];
            break;
        }
    }

    const int rightMs = leftMs + windowMs;
    const int visibleWindowMs = rightMs - leftMs;
    const double stepMs = static_cast<double>(visibleWindowMs) / static_cast<double>(targetSamples);

    for (int i = 0; i < targetSamples; ++i) {
        const double tStartMs = leftMs + static_cast<double>(i) * stepMs;
        const double tEndMs   = tStartMs + stepMs;

        if (tEndMs < 0.0) {
            envelope[i] = 0.0f;
            continue;
        }

        const int rawFirst = static_cast<int>(tStartMs / intervalMs);
        const int rawLast  = static_cast<int>(tEndMs   / intervalMs);
        const int idxFirst = rawFirst < 0 ? 0 : rawFirst;
        const int idxLast  = rawLast >= timelineCount ? timelineCount - 1 : rawLast;

        if (rawFirst >= timelineCount) {
            envelope[i] = lastValidEnvelope;
            continue;
        }

        float maxVal = 0.0f;
        for (int j = idxFirst; j <= idxLast; ++j) {
            if (knownData[j]) {
                const float v = envelopeData[j];
                if (v > maxVal) maxVal = v;
            }
        }

        if (maxVal <= 0.0f && rawLast >= timelineCount)
            maxVal = lastValidEnvelope;

        envelope[i] = maxVal;
    }

    return envelope;
}

static void buildEnvelopePaths(const QVector<float>& envelope,
                               const QRect& band,
                               int centerY,
                               double halfH,
                               QPainterPath& fillPath,
                               QPainterPath& topPath,
                               QPainterPath& bottomPath)
{
    const int count = envelope.size();
    if (count < 2) {
        fillPath = QPainterPath();
        topPath = QPainterPath();
        bottomPath = QPainterPath();
        return;
    }

    const double bandLeft = band.left();
    const double bandW = band.width();
    const double binWidth = bandW / static_cast<double>(count);
    const float* envData = envelope.constData();

    fillPath = QPainterPath();
    fillPath.moveTo(bandLeft, centerY);
    for (int i = 0; i < count; ++i) {
        const double x = bandLeft + (static_cast<double>(i) + 0.5) * binWidth;
        fillPath.lineTo(x, centerY - envData[i] * halfH);
    }
    fillPath.lineTo(bandLeft + bandW, centerY);
    for (int i = count - 1; i >= 0; --i) {
        const double x = bandLeft + (static_cast<double>(i) + 0.5) * binWidth;
        fillPath.lineTo(x, centerY + envData[i] * halfH);
    }
    fillPath.closeSubpath();

    topPath = QPainterPath();
    bottomPath = QPainterPath();
    for (int i = 0; i < count; ++i) {
        const double x = bandLeft + (static_cast<double>(i) + 0.5) * binWidth;
        const double yt = centerY - envData[i] * halfH;
        const double yb = centerY + envData[i] * halfH;
        if (i == 0) {
            topPath.moveTo(x, yt);
            bottomPath.moveTo(x, yb);
        } else {
            topPath.lineTo(x, yt);
            bottomPath.lineTo(x, yb);
        }
    }
}
} // namespace

PlayerBpmWidget::PlayerBpmWidget(QWidget* parent)
    : QWidget(parent)
    , m_bpm(0)
    , m_position(QTime(0, 0))
    , m_beatReference(QTime())
    , m_running(false)
    , m_analyzed(false)
    , m_tempoRate(1.0)
    , m_syncAdjusting(false)
    , m_windowMs(6000)
    , m_liveSampleIntervalMs(25)  // Live-only envelope binning; preloaded waveform uses m_trueSampleIntervalMs.
    , m_exactBpm(0.0)
    , m_manualBpmAdjustment(0.0)
    , m_manualPhaseAdjustmentMs(0)
    , m_trueSampleIntervalMs(8.0)  // Default to 120fps analyzer rate (1000/120 ≈ 8.333ms)
    , m_rebuildRequested(false)
    , m_envelopeDirty(false)
    , m_envelopePreloaded(false)
    , m_waveformLayerHeight(0)
    , m_waveformLayerSampleCount(0)
    , m_waveformLayerDirty(false)
    , m_scrubbing(false)
    , m_scrubStartNorm(0.0)
    , m_scrubStartX(0)
{
    setAutoFillBackground(false);

    m_decreaseBpmButton = new QToolButton(this);
    m_increaseBpmButton = new QToolButton(this);
    m_decreasePhaseButton = new QToolButton(this);
    m_increasePhaseButton = new QToolButton(this);
    m_decreaseBpmButton->setText(QStringLiteral("-"));
    m_increaseBpmButton->setText(QStringLiteral("+"));
    m_decreasePhaseButton->setText(QStringLiteral("P-"));
    m_increasePhaseButton->setText(QStringLiteral("P+"));
    m_decreaseBpmButton->setToolTip(QStringLiteral("Decrease displayed BPM by 0.01"));
    m_increaseBpmButton->setToolTip(QStringLiteral("Increase displayed BPM by 0.01"));
    m_decreasePhaseButton->setToolTip(QStringLiteral("Move beat phase earlier by 10 ms"));
    m_increasePhaseButton->setToolTip(QStringLiteral("Move beat phase later by 10 ms"));
    m_decreaseBpmButton->setAutoRaise(true);
    m_increaseBpmButton->setAutoRaise(true);
    m_decreaseBpmButton->setFixedSize(22, 20);
    m_increaseBpmButton->setFixedSize(22, 20);
    m_decreasePhaseButton->setFixedSize(26, 20);
    m_increasePhaseButton->setFixedSize(26, 20);
    m_decreaseBpmButton->hide();
    m_increaseBpmButton->hide();
    m_decreasePhaseButton->hide();
    m_increasePhaseButton->hide();
    connect(m_decreaseBpmButton, &QToolButton::clicked, this, &PlayerBpmWidget::decreaseManualBpm);
    connect(m_increaseBpmButton, &QToolButton::clicked, this, &PlayerBpmWidget::increaseManualBpm);
    connect(m_decreasePhaseButton, &QToolButton::clicked, this, &PlayerBpmWidget::decreaseManualPhase);
    connect(m_increasePhaseButton, &QToolButton::clicked, this, &PlayerBpmWidget::increaseManualPhase);

    // Throttle envelope repaints to ~30 FPS maximum
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(33);
    connect(&m_updateTimer, &QTimer::timeout, this, &PlayerBpmWidget::onUpdateTimer);
    connect(&m_rebuildWatcher, &QFutureWatcher<RebuildResult>::finished, this, &PlayerBpmWidget::onRebuildFinished);
}

void PlayerBpmWidget::invalidateWaveformLayer()
{
    m_waveformLayerDirty = true;
    m_waveformLayer = QPixmap();
    m_waveformLayerHeight = 0;
    m_waveformLayerSampleCount = 0;
}

void PlayerBpmWidget::rebuildWaveformLayer(int bandHeight)
{
    const int sampleCount = m_timelineEnvelope.size();
    const int height = qMax(8, bandHeight);
    if (sampleCount <= 0) {
        invalidateWaveformLayer();
        return;
    }

    QImage img(sampleCount, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter layerPainter(&img);
    layerPainter.setRenderHint(QPainter::Antialiasing, false);
    const int centerY = height / 2;
    const double halfH = qMax(4.0, (height / 2.0) - 2.0);

    const float* envData = m_timelineEnvelope.constData();
    const quint8* knownData = m_timelineKnown.constData();
    const QColor bodyColor(112, 152, 200, 170);
    const QColor edgeColor(190, 216, 246, 150);
    layerPainter.setPen(QPen(bodyColor, 1));

    for (int x = 0; x < sampleCount; ++x) {
        const float amp = knownData[x] ? envData[x] : 0.0f;
        const int yTop = qRound(static_cast<double>(centerY) - static_cast<double>(amp) * halfH);
        const int yBottom = qRound(static_cast<double>(centerY) + static_cast<double>(amp) * halfH);
        layerPainter.drawLine(x, yTop, x, yBottom);

        if (amp > 0.01f) {
            layerPainter.setPen(QPen(edgeColor, 1));
            layerPainter.drawPoint(x, yTop);
            layerPainter.drawPoint(x, yBottom);
            layerPainter.setPen(QPen(bodyColor, 1));
        }
    }

    layerPainter.end();

    m_waveformLayer = QPixmap::fromImage(img);
    m_waveformLayerHeight = height;
    m_waveformLayerSampleCount = sampleCount;
    m_waveformLayerDirty = false;
}

void PlayerBpmWidget::onUpdateTimer()
{
    update();
}

void PlayerBpmWidget::setState(int bpm, const QTime& position, const QTime& beatReference, bool running, bool analyzed)
{
    const bool preserveExactBpm = analyzed
            && bpm > 0
            && m_bpm == bpm
            && m_exactBpm > 0.0;
    const double exactBpm = preserveExactBpm
            ? m_exactBpm - m_manualBpmAdjustment
            : static_cast<double>(bpm);
    setState(bpm, exactBpm, position, beatReference, running, analyzed);
}

void PlayerBpmWidget::setState(int bpm, double exactBpm, const QTime& position, const QTime& beatReference, bool running, bool analyzed)
{
    const int previousLeftMs = visibleWindowLeftMs();
    if (!analyzed && bpm <= 0)
        clearManualBpmAdjustment();
    m_bpm = bpm;
    const double baseExactBpm = exactBpm > 0.0 ? exactBpm : static_cast<double>(bpm);
    m_exactBpm = baseExactBpm > 0.0 ? baseExactBpm + m_manualBpmAdjustment : 0.0;
    m_position = position;
    m_beatReference = beatReference;
    m_running = running;
    m_analyzed = analyzed;
    const int currentLeftMs = visibleWindowLeftMs();
    // Rebuild the expensive envelope geometry only when the visible window advances.
    if (currentLeftMs != previousLeftMs)
        m_envelopeDirty = true;
    updateManualBpmButtons();
    update();
}

void PlayerBpmWidget::setExactBpm(double exactBpm)
{
    if (exactBpm > 0.0) {
        m_exactBpm = exactBpm + m_manualBpmAdjustment;
        m_envelopeDirty = true;
        update();
    }
}

void PlayerBpmWidget::clearManualBpmAdjustment()
{
    m_manualBpmAdjustment = 0.0;
    m_manualPhaseAdjustmentMs = 0;
    updateManualBpmButtons();
}

QTime PlayerBpmWidget::adjustedBeatReference() const
{
    if (!m_beatReference.isValid() || m_exactBpm <= 0.0)
        return m_beatReference;

    const double beatMs = 60000.0 / m_exactBpm;
    double phaseMs = std::fmod(
        static_cast<double>(QTime(0, 0).msecsTo(m_beatReference))
            + static_cast<double>(m_manualPhaseAdjustmentMs),
        beatMs);
    if (phaseMs < 0.0)
        phaseMs += beatMs;
    return QTime(0, 0).addMSecs(qRound(phaseMs));
}

void PlayerBpmWidget::logManualGridState() const
{
    if (m_exactBpm <= 0.0)
        return;

    const QTime phase = adjustedBeatReference();
    const double beatMs = 60000.0 / m_exactBpm;
    const qint64 positionMs = QTime(0, 0).msecsTo(m_position);
    const qint64 referenceMs = phase.isValid() ? QTime(0, 0).msecsTo(phase) : 0;
    double residualMs = std::fmod(static_cast<double>(positionMs - referenceMs), beatMs);
    if (residualMs > beatMs * 0.5)
        residualMs -= beatMs;
    if (residualMs < -beatMs * 0.5)
        residualMs += beatMs;

    qDebug() << Q_FUNC_INFO
             << "manualExactBpm=" << m_exactBpm
             << "periodMs=" << beatMs
             << "basePhase=" << m_beatReference
             << "manualPhaseAdjustmentMs=" << m_manualPhaseAdjustmentMs
             << "effectivePhase=" << phase
             << "position=" << m_position
             << "gridResidualMs=" << residualMs;
}

void PlayerBpmWidget::decreaseManualBpm()
{
    if (m_exactBpm <= 0.0)
        return;
    m_manualBpmAdjustment -= 0.01;
    m_exactBpm -= 0.01;
    logManualGridState();
    updateManualBpmButtons();
    update();
}

void PlayerBpmWidget::increaseManualBpm()
{
    if (m_exactBpm <= 0.0)
        return;
    m_manualBpmAdjustment += 0.01;
    m_exactBpm += 0.01;
    logManualGridState();
    updateManualBpmButtons();
    update();
}

void PlayerBpmWidget::decreaseManualPhase()
{
    if (!m_beatReference.isValid())
        return;
    m_manualPhaseAdjustmentMs -= 10;
    logManualGridState();
    update();
}

void PlayerBpmWidget::increaseManualPhase()
{
    if (!m_beatReference.isValid())
        return;
    m_manualPhaseAdjustmentMs += 10;
    logManualGridState();
    update();
}

void PlayerBpmWidget::updateManualBpmButtons()
{
    if (!m_decreaseBpmButton || !m_increaseBpmButton
            || !m_decreasePhaseButton || !m_increasePhaseButton)
        return;
    m_decreaseBpmButton->setEnabled(m_exactBpm > 0.0);
    m_increaseBpmButton->setEnabled(m_exactBpm > 0.0);
    m_decreasePhaseButton->setEnabled(m_beatReference.isValid());
    m_increasePhaseButton->setEnabled(m_beatReference.isValid());
}

void PlayerBpmWidget::setCuePosition(const QTime& position)
{
    m_cuePosition = position;
    update();
}

void PlayerBpmWidget::setTempoInfo(double tempoRate, bool syncAdjusting, qint64 syncCompleted)
{
    m_tempoRate = tempoRate;
    m_syncAdjusting = syncAdjusting;
    m_syncCompleted = syncCompleted;
    update();
}

void PlayerBpmWidget::requestEnvelopeRebuild(const QRect& band, int centerY, double halfH)
{
    if (m_rebuildWatcher.isRunning()) {
        m_rebuildRequested = true;
        m_requestedBand = band;
        m_requestedCenterY = centerY;
        m_requestedHalfH = halfH;
        return;
    }

    const QVector<float> timelineEnvelope = m_timelineEnvelope;
    const QVector<quint8> timelineKnown = m_timelineKnown;
    const double sampleIntervalMs = m_envelopePreloaded
            ? qMax(0.001, m_trueSampleIntervalMs)
            : static_cast<double>(qMax(1, m_liveSampleIntervalMs));
    const int leftMs = visibleWindowLeftMs();
    const int trackLenMs = QTime(0, 0).msecsTo(m_trackLength);

    const int windowMs = m_windowMs;
    QFuture<RebuildResult> future = QtConcurrent::run([timelineEnvelope,
                                                       timelineKnown,
                                                       sampleIntervalMs,
                                                       band,
                                                       centerY,
                                                       halfH,
                                                       leftMs,
                                                       windowMs,
                                                       trackLenMs]() {
        RebuildResult result;
        result.band = band;
        result.envelope = buildVisibleEnvelope(timelineEnvelope, timelineKnown, sampleIntervalMs, band.width(), windowMs, leftMs, trackLenMs);
        buildEnvelopePaths(result.envelope, band, centerY, halfH, result.envFillPath, result.topEdgePath, result.bottomEdgePath);
        return result;
    });

    m_rebuildWatcher.setFuture(future);
}

void PlayerBpmWidget::onRebuildFinished()
{
    const RebuildResult result = m_rebuildWatcher.result();
    m_envelope = result.envelope;
    m_envFillPath = result.envFillPath;
    m_topEdgePath = result.topEdgePath;
    m_bottomEdgePath = result.bottomEdgePath;
    m_cachedBand = result.band;
    m_envelopeDirty = false;
    update();

    if (m_rebuildRequested) {
        m_rebuildRequested = false;
        requestEnvelopeRebuild(m_requestedBand, m_requestedCenterY, m_requestedHalfH);
    }
}

void PlayerBpmWidget::appendEnvelopeSample(float value)
{
    appendEnvelopeSampleAt(QTime(0, 0).msecsTo(m_position), value);
}

void PlayerBpmWidget::appendEnvelopeSampleAt(int positionMs, float value)
{
    if (positionMs < 0)
        return;

    const int sampleIndex = positionMs / qMax(1, m_liveSampleIntervalMs);
    if (sampleIndex < 0)
        return;

    if (m_timelineEnvelope.size() <= sampleIndex)
        m_timelineEnvelope.resize(sampleIndex + 1);
    if (m_timelineKnown.size() <= sampleIndex)
        m_timelineKnown.resize(sampleIndex + 1);

    m_timelineEnvelope[sampleIndex] = clamp01(value);
    m_timelineKnown[sampleIndex] = 1;
    m_waveformLayerDirty = true;

    // Mark path cache stale; trigger a repaint via timer (caps to ~30 FPS)
    m_envelopeDirty = true;
    if (!m_updateTimer.isActive())
        m_updateTimer.start();
}

void PlayerBpmWidget::clearEnvelope()
{
    m_timelineEnvelope.clear();
    m_timelineKnown.clear();
    m_envelope.clear();
    m_envFillPath  = QPainterPath();
    m_topEdgePath  = QPainterPath();
    m_bottomEdgePath = QPainterPath();
    m_cachedBand   = QRect();
    m_envelopeDirty = false;
    m_envelopePreloaded = false;
    // Reset track length so the next preloaded waveform cannot inherit stale
    // timing from a previously loaded track.
    m_trackLength = QTime(0, 0);
    m_trueSampleIntervalMs = 8.0;
    invalidateWaveformLayer();
    update();
}

void PlayerBpmWidget::setPreloadedEnvelope(const QVector<float>& samples, int sourceIntervalMs,
                                            int sourceDurationMs)
{
    if (samples.isEmpty())
        return;

    m_timelineEnvelope.clear();
    m_timelineKnown.clear();

    const qint64 trackMs = QTime(0, 0).msecsTo(m_trackLength);
    const qint64 fallbackMs = (sourceIntervalMs > 0)
            ? static_cast<qint64>(samples.size() - 1) * static_cast<qint64>(sourceIntervalMs)
            : static_cast<qint64>(qRound(static_cast<double>(samples.size() - 1) * 1000.0 / 120.0));

    // Guard against stale track length (e.g. previous song) when cached envelope
    // arrives before the new player length is known.
    qint64 totalMs = sourceDurationMs > 0 ? sourceDurationMs : fallbackMs;
    if (sourceDurationMs <= 0 && trackMs > 0) {
        const double ratio = fallbackMs > 0 ? static_cast<double>(trackMs) / static_cast<double>(fallbackMs) : 1.0;
        if (ratio > 0.9 && ratio < 1.1)
            totalMs = trackMs;
    }

    const double frameDurationMs = (samples.size() > 1)
            ? (static_cast<double>(totalMs) / static_cast<double>(samples.size() - 1))
            : static_cast<double>(qMax(1, sourceIntervalMs));
    m_trueSampleIntervalMs = frameDurationMs;  // Store exact interval for beat-grid alignment

    m_timelineEnvelope.resize(samples.size());
    m_timelineKnown.resize(samples.size());
    for (int i = 0; i < samples.size(); ++i) {
        m_timelineEnvelope[i] = qBound(0.0f, samples.at(i), 1.0f);
        m_timelineKnown[i] = 1;
    }

    m_envelopeDirty = true;
    m_envelopePreloaded = true;
    m_waveformLayerDirty = true;
    if (m_updateTimer.isActive())
        m_updateTimer.stop();
    update();
}

void PlayerBpmWidget::setWindowMilliseconds(int windowMs)
{
    m_windowMs = qBound(2000, windowMs, 20000);
    m_envelopeDirty = true;
}

void PlayerBpmWidget::setTrackLength(const QTime& length)
{
    if (m_trackLength != length) {
        m_trackLength = length;
        m_envelopeDirty = true;
    }
}

int PlayerBpmWidget::visibleWindowLeftMs() const
{
    const int posMs = QTime(0, 0).msecsTo(m_position);
    const int desiredLeftMs = posMs - qRound(0.20 * m_windowMs);
    // Keep the playhead fixed at 20% while allowing a negative left edge near
    // track start. This makes 0ms align to the playhead at startup and leaves
    // a silent pre-roll area to the left.
    return desiredLeftMs;
}

QTime PlayerBpmWidget::visualBeatReference() const
{
    if (!m_beatReference.isValid() || m_bpm <= 0)
        return m_beatReference;

    return adjustedBeatReference();
}

void PlayerBpmWidget::rebuildVisibleEnvelope(int bandWidth)
{
    const double intervalMs = m_envelopePreloaded
            ? qMax(0.001, m_trueSampleIntervalMs)
            : static_cast<double>(qMax(1, m_liveSampleIntervalMs));
    // Use exactly bandWidth samples so each pixel represents one time bin.
    // This ensures 1:1 pixel mapping and prevents horizontal squeezing.
    const int targetSamples = qMax(2, bandWidth);
    m_envelope.resize(targetSamples);

    const int leftMs = visibleWindowLeftMs();
    const int trackLenMs = QTime(0, 0).msecsTo(m_trackLength);
    // Always keep the visible window width constant so it can continue scrolling
    // beyond the track end if playback has reached the final section.
    const int rightMs = leftMs + m_windowMs;
    const int visibleWindowMs = rightMs - leftMs;
    // Divide by targetSamples (not targetSamples-1) so each bin has width windowMs/count.
    const double stepMs = static_cast<double>(visibleWindowMs) / static_cast<double>(targetSamples);

    // Cache envelope and timeline pointers for faster access
    const float* envelopeData = m_timelineEnvelope.constData();
    const quint8* knownData = m_timelineKnown.constData();
    const int timelineCount = m_timelineEnvelope.size();

    // Find the last valid envelope value for extending during simulation
    float lastValidEnvelope = 0.0f;
    for (int k = timelineCount - 1; k >= 0; --k) {
        if (knownData[k]) {
            lastValidEnvelope = envelopeData[k];
            break;
        }
    }

    for (int i = 0; i < targetSamples; ++i) {
        const double tStartMs = leftMs + static_cast<double>(i) * stepMs;
        const double tEndMs   = tStartMs + stepMs;

        if (tEndMs < 0.0) { m_envelope[i] = 0.0f; continue; }

        // Max-pool over the time range this display slot covers.
        // Linear interpolation (the previous approach) causes all peak amplitudes to
        // vary continuously as the fractional alignment shifts with every position
        // update — this is the source of the "jitter". With max-pooling the value
        // only changes when the playhead moves far enough to pull a different
        // timeline sample into the slot's range, giving a clean 1-slot discrete hop.
        const int rawFirst = static_cast<int>(tStartMs / intervalMs);
        const int rawLast  = static_cast<int>(tEndMs   / intervalMs);
        const int idxFirst = rawFirst < 0 ? 0 : rawFirst;
        const int idxLast  = rawLast >= timelineCount ? timelineCount - 1 : rawLast;

        // If we're past the end of known timeline data, extend the last valid envelope value.
        if (rawFirst >= timelineCount) {
            m_envelope[i] = lastValidEnvelope;
            continue;
        }

        float maxVal = 0.0f;
        for (int j = idxFirst; j <= idxLast; ++j) {
            if (knownData[j]) {
                const float v = envelopeData[j];
                if (v > maxVal) maxVal = v;
            }
        }

        // If the time window extends beyond the known timeline and there is no valid
        // data inside the current slot, carry the last value forward instead of freezing.
        if (maxVal <= 0.0f && rawLast >= timelineCount)
            maxVal = lastValidEnvelope;

        m_envelope[i] = maxVal;
    }
}

QRect PlayerBpmWidget::phaseBandRect() const
{
    const QRect outer = rect().adjusted(1, 1, -1, -1);
    const int bandTop = outer.top() + 20;
    return QRect(outer.left() + 2, bandTop, outer.width() - 4, qMax(24, outer.bottom() - bandTop - 6));
}

double PlayerBpmWidget::normalizedX(const QPoint& pos) const
{
    const QRect band = phaseBandRect();
    if (band.width() <= 1)
        return 0.0;
    const int clampedX = qBound(band.left(), pos.x(), band.right());
    return qBound(0.0, static_cast<double>(clampedX - band.left()) / static_cast<double>(band.width()), 1.0);
}

void PlayerBpmWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (!phaseBandRect().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_scrubbing = true;
    m_scrubStartNorm = normalizedX(event->pos());
    m_scrubStartX = event->pos().x();
    emit envelopeScrubStarted();
    event->accept();
}

void PlayerBpmWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubbing) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QRect band = phaseBandRect();
    if (qAbs(event->pos().x() - m_scrubStartX) < qMax(1, band.width() / 200)) {
        event->accept();
        return;
    }

    const double currentNorm = normalizedX(event->pos());
    const double deltaNorm = currentNorm - m_scrubStartNorm;
    emit envelopeScrubPositionChanged(deltaNorm, false);
    event->accept();
}

void PlayerBpmWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_scrubbing || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_scrubbing = false;
    const double currentNorm = normalizedX(event->pos());
    const double deltaNorm = currentNorm - m_scrubStartNorm;
    emit envelopeScrubPositionChanged(deltaNorm, true);
    event->accept();
}

void PlayerBpmWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const int right = width() - 4;
    m_increaseBpmButton->move(right - m_increaseBpmButton->width(), 2);
    m_decreaseBpmButton->move(right - m_increaseBpmButton->width() - m_decreaseBpmButton->width() - 2, 2);
    m_increasePhaseButton->move(right - m_increaseBpmButton->width()
                                - m_decreaseBpmButton->width()
                                - m_increasePhaseButton->width()
                                - 2, 2);
    m_decreasePhaseButton->move(right - m_increaseBpmButton->width()
                                - m_decreaseBpmButton->width()
                                - m_increasePhaseButton->width()
                                - m_decreasePhaseButton->width()
                                - 4, 2);
    m_envelopeDirty = true; // geometry changed – rebuild paths
    m_waveformLayerDirty = true;
}

// ---------------------------------------------------------------------------
// Rebuild the three envelope paths whenever data or geometry changes
// ---------------------------------------------------------------------------
void PlayerBpmWidget::rebuildEnvelopePaths(const QRect& band, int centerY, double halfH)
{
    const int count = m_envelope.size();
    if (count < 2) {
        m_envFillPath = QPainterPath();
        m_topEdgePath = QPainterPath();
        m_bottomEdgePath = QPainterPath();
        return;
    }

    const double bandLeft = band.left();
    const double bandW    = band.width();
    const double binWidth  = bandW / static_cast<double>(count);

    // Cache envelope data pointer for faster access
    const float* envData = m_envelope.constData();

    // Fill polygon: each value represents a time bin, so draw it at the bin center
    m_envFillPath = QPainterPath();
    m_envFillPath.moveTo(bandLeft, centerY);
    for (int i = 0; i < count; ++i) {
        const double x = bandLeft + (static_cast<double>(i) + 0.5) * binWidth;
        m_envFillPath.lineTo(x, centerY - envData[i] * halfH);
    }
    m_envFillPath.lineTo(bandLeft + bandW, centerY);
    for (int i = count - 1; i >= 0; --i) {
        const double x = bandLeft + (static_cast<double>(i) + 0.5) * binWidth;
        m_envFillPath.lineTo(x, centerY + envData[i] * halfH);
    }
    m_envFillPath.closeSubpath();

    // Outline edges
    m_topEdgePath    = QPainterPath();
    m_bottomEdgePath = QPainterPath();
    for (int i = 0; i < count; ++i) {
        const double x  = bandLeft + (static_cast<double>(i) + 0.5) * binWidth;
        const double yt = centerY - envData[i] * halfH;
        const double yb = centerY + envData[i] * halfH;
        if (i == 0) { m_topEdgePath.moveTo(x, yt); m_bottomEdgePath.moveTo(x, yb); }
        else        { m_topEdgePath.lineTo(x, yt); m_bottomEdgePath.lineTo(x, yb); }
    }
}

double PlayerBpmWidget::phase() const
{
    if (m_bpm <= 0) return 0.0;
    // Use base BPM — beats are fixed positions in the audio file.
    const double baseBpm = (m_exactBpm > 0.0) ? m_exactBpm : static_cast<double>(m_bpm);
    const double beatMs = 60000.0 / baseBpm;
    if (beatMs <= 0.0) return 0.0;

    const qint64 posMs    = QTime(0, 0).msecsTo(m_position);
    const QTime visualReference = adjustedBeatReference();
    const qint64 beatRefMs = visualReference.isValid() ? QTime(0, 0).msecsTo(visualReference) : 0;

    double p = fmod(static_cast<double>(posMs - beatRefMs), beatMs) / beatMs;
    if (p < 0.0) p += 1.0;
    return qBound(0.0, p, 1.0);
}

void PlayerBpmWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    // Antialiasing only for text; all geometry drawn without it (big CPU saving)
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // ── Background ──────────────────────────────────────────────────────────
    painter.fillRect(rect(), QColor(14, 19, 28));

    const QRect outer = rect().adjusted(1, 1, -1, -1);
    QLinearGradient frameGrad(outer.topLeft(), outer.bottomLeft());
    frameGrad.setColorAt(0.0, QColor(28, 36, 49));
    frameGrad.setColorAt(0.55, QColor(20, 28, 40));
    frameGrad.setColorAt(1.0, QColor(16, 22, 32));
    painter.setPen(QPen(QColor(58, 74, 96), 1.0));
    painter.setBrush(frameGrad);
    painter.drawRect(outer); // drawRect is faster than drawRoundedRect

    // ── BPM label ────────────────────────────────────────────────────────────
    painter.setRenderHint(QPainter::Antialiasing, true);  // on for text
    QString bpmText;
    if (m_bpm > 0) {
        bpmText = (m_manualBpmAdjustment != 0.0)
                ? QString::number(m_exactBpm, 'f', 3) + " BPM"
                : QString::number(m_bpm) + " BPM";
        const double adjusted = static_cast<double>(m_bpm) * m_tempoRate;
        if (qAbs(adjusted - static_cast<double>(m_bpm)) >= 0.05)
            bpmText += " (" + formatTempoValue(adjusted) + ")";
    } else if (m_analyzed) {
        bpmText = "No BPM detected";
    }
    painter.setPen(QColor(222, 231, 242));
    painter.drawText(outer.adjusted(8, 2, -38, -2), Qt::AlignLeft | Qt::AlignTop, bpmText);
    painter.setRenderHint(QPainter::Antialiasing, false); // off again

    // ── Sync dot ─────────────────────────────────────────────────────────────
    if (m_syncAdjusting || m_syncCompleted != 0) {
        const qint64 now = QTime::currentTime().msecsSinceStartOfDay();
        const int blink = (now / 300) % 2;
        
        // If sync completed recently, show solid color for 1 second
        if (m_syncCompleted != 0 && (now - m_syncCompleted) < 1000) {
            painter.setPen(QPen(QColor(255, 190, 64), 1.0));
            painter.setBrush(QColor(255, 190, 64));
            painter.drawEllipse(QRect(outer.right() - 20, outer.top() + 6, 10, 10));
        } else if (m_syncAdjusting) {
            // While adjusting, blink every 300ms
            painter.setPen(QPen(QColor(255, 190, 64), 1.0));
            painter.setBrush(blink == 0 ? QColor(255, 190, 64) : QColor(66, 78, 94));
            painter.drawEllipse(QRect(outer.right() - 20, outer.top() + 6, 10, 10));
        }
    }

    // ── Phase band ────────────────────────────────────────────────────────────
    const QRect phaseBand = phaseBandRect();
    QLinearGradient bandGrad(phaseBand.topLeft(), phaseBand.bottomLeft());
    bandGrad.setColorAt(0.0, QColor(39, 53, 73));
    bandGrad.setColorAt(1.0, QColor(23, 31, 45));
    painter.setPen(Qt::NoPen);
    painter.setBrush(bandGrad);
    painter.drawRect(phaseBand);

    const int centerY = phaseBand.center().y();
    painter.setPen(QPen(QColor(89, 104, 129), 1));
    painter.drawLine(phaseBand.left(), centerY, phaseBand.right(), centerY);

    // ── Beat grid ─────────────────────────────────────────────────────────────
    if (m_bpm > 0 && m_windowMs > 0) {
        // Use base BPM (without tempo adjustment) — beats are fixed positions in the audio file.
        // Tempo only changes how fast the playhead scrolls across them, not where they are.
        const double baseBpm = (m_exactBpm > 0.0) ? m_exactBpm : static_cast<double>(m_bpm);
        const double beatMs = 60000.0 / baseBpm;
        const QTime visualBeatRef = visualBeatReference();
        const qint64 beatRefMs = visualBeatRef.isValid() ? QTime(0, 0).msecsTo(visualBeatRef) : 0;
        const qint64 leftMs = visibleWindowLeftMs();
        const qint64 rightMs = leftMs + m_windowMs;
        const int visibleWindowMs = qMax(1, m_windowMs);

        // Collect all beat lines into one vector, then draw in one call
        QVector<QLineF> beatLines;
        QVector<QLineF> barLines;
        beatLines.reserve(32);
        barLines.reserve(8);
        const double firstBeatIndex = qFloor(static_cast<double>(leftMs - beatRefMs) / beatMs);
        for (int i = 0; i < 128; ++i) {
            const double beatTime = static_cast<double>(beatRefMs) + (firstBeatIndex + static_cast<double>(i)) * beatMs;
            if (beatTime > static_cast<double>(rightMs))  break;
            if (beatTime < static_cast<double>(leftMs)) continue;
            
            const double norm = (beatTime - static_cast<double>(leftMs)) / static_cast<double>(visibleWindowMs);
            const int x = phaseBand.left() + qRound(norm * phaseBand.width());
            const QLineF line(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1);
            const qint64 beatIndex = static_cast<qint64>(qRound(firstBeatIndex)) + i;
            if ((beatIndex % 4 + 4) % 4 == 0)
                barLines.append(line);
            else
                beatLines.append(line);
        }
        painter.setPen(QPen(QColor(102, 128, 161, 140), 1));
        painter.drawLines(beatLines);
        painter.setPen(QPen(QColor(102, 128, 161, 240), 2));
        painter.drawLines(barLines);
    }

    // ── Envelope waveform (cached paths) ──────────────────────────────────────
    if (!m_timelineEnvelope.isEmpty()) {
        const double halfH = qMax(4.0, (phaseBand.height() / 2.0) - 2.0);
        if (m_waveformLayerDirty
            || m_waveformLayer.isNull()
            || m_waveformLayerHeight != phaseBand.height()
            || m_waveformLayerSampleCount != m_timelineEnvelope.size()) {
            rebuildWaveformLayer(phaseBand.height());
        }

        if (!m_waveformLayer.isNull()) {
            const double sampleIntervalF = m_envelopePreloaded
                    ? qMax(0.001, m_trueSampleIntervalMs)
                    : static_cast<double>(qMax(1, m_liveSampleIntervalMs));
            const double leftMsF  = static_cast<double>(visibleWindowLeftMs());
            const double rightMsF = leftMsF + static_cast<double>(m_windowMs);
            // Keep exact time-domain mapping so beat-grid and waveform stay phase-locked.
            const double srcLeftF  = leftMsF  / sampleIntervalF;
            const double srcRightF = rightMsF / sampleIntervalF;
            const double totalSrcW = srcRightF - srcLeftF;
            const double maxW      = static_cast<double>(m_waveformLayer.width());
            const double clampedL  = qMax(0.0, srcLeftF);
            const double clampedR  = qMin(srcRightF, maxW);
            const double validSrcW = qMax(0.0, clampedR - clampedL);

            if (validSrcW > 0.0 && clampedL < maxW && totalSrcW > 0.0) {
                const double dstOffsetW = ((clampedL - srcLeftF) / totalSrcW) * phaseBand.width();
                const double dstValidW = (validSrcW / totalSrcW) * phaseBand.width();
                const QRectF dstRect(phaseBand.left() + dstOffsetW, phaseBand.top(), dstValidW, phaseBand.height());
                const QRectF srcRect(clampedL, 0, validSrcW, m_waveformLayer.height());
                painter.drawPixmap(dstRect, m_waveformLayer, srcRect);
            }
        } else {
            if (m_envelopeDirty && !m_rebuildWatcher.isRunning())
                requestEnvelopeRebuild(phaseBand, centerY, halfH);

            if (!m_envFillPath.isEmpty()) {
                QLinearGradient envGrad(0, centerY - halfH, 0, centerY + halfH);
                envGrad.setColorAt(0.00, QColor(160, 195, 232, 220));
                envGrad.setColorAt(0.35, QColor(112, 152, 200, 200));
                envGrad.setColorAt(0.50, QColor( 75, 110, 165, 170));
                envGrad.setColorAt(0.65, QColor(112, 152, 200, 200));
                envGrad.setColorAt(1.00, QColor(160, 195, 232, 220));

                painter.setPen(Qt::NoPen);
                painter.setBrush(envGrad);
                painter.drawPath(m_envFillPath);

                painter.setPen(QPen(QColor(195, 218, 248, 160), 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(m_topEdgePath);
                painter.drawPath(m_bottomEdgePath);
            }
        }
    }

    // Keep the planned cue visible while the playhead continues through the track.
    if (m_cuePosition.isValid() && m_windowMs > 0) {
        const qint64 cueMs = QTime(0, 0).msecsTo(m_cuePosition);
        const qint64 leftMs = visibleWindowLeftMs();
        const qint64 rightMs = leftMs + m_windowMs;
        if (cueMs >= leftMs && cueMs <= rightMs) {
            const double norm = static_cast<double>(cueMs - leftMs)
                / static_cast<double>(m_windowMs);
            const int x = phaseBand.left() + qRound(norm * phaseBand.width());
            painter.setPen(QPen(QColor(255, 190, 64, 220), 2));
            painter.drawLine(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1);
        }
    }

    // ── Beat cursor (flash exactly when a beat tick crosses the reference) ───
    if (m_bpm > 0 && m_windowMs > 0) {
        // Use base BPM — beats are fixed positions in the audio file.
        const double baseBpm = (m_exactBpm > 0.0) ? m_exactBpm : static_cast<double>(m_bpm);
        const double beatMs = 60000.0 / baseBpm;
        const qint64 posMs = QTime(0, 0).msecsTo(m_position);
        const QTime visualBeatRef = visualBeatReference();
        const qint64 beatRefMs = visualBeatRef.isValid() ? QTime(0, 0).msecsTo(visualBeatRef) : 0;
        // Playhead is at 20% from the left edge — beat cursor is pinned there.
        static constexpr double kPlayheadNorm = 0.20;
        const double anchorTimeMs = static_cast<double>(posMs);
        const double anchorNorm = kPlayheadNorm;
        const int x = phaseBand.left() + qRound(anchorNorm * phaseBand.width());

        // Find nearest beat to the anchor time.
        const double beatIndexF = (anchorTimeMs - static_cast<double>(beatRefMs)) / beatMs;
        const qint64 nearestBeatIdx = static_cast<qint64>(qRound(beatIndexF));
        const double nearestBeatTime = static_cast<double>(beatRefMs) + static_cast<double>(nearestBeatIdx) * beatMs;
        const double deltaMs = anchorTimeMs - nearestBeatTime;

        // ON every time a beat crosses the reference marker.
        // Window is wide enough to survive UI/frame jitter, but still beat-tight.
        const double flashWindowMs = qMin(120.0, beatMs * 0.28);
        const bool beatAtMarker = (qAbs(deltaMs) <= flashWindowMs);
        const bool flashOn = m_running && beatAtMarker;

        if (!m_running) {
            painter.setPen(QPen(QColor(132, 195, 255, 190), 1));
            painter.drawLine(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1);
            return;
        }

        if (flashOn) {
            painter.setPen(QPen(QColor(28, 214, 116), 1.0));
            painter.setBrush(QColor(28, 214, 116, 140));
            painter.drawRect(QRect(x - 2, phaseBand.top() + 1, 4, phaseBand.height() - 2));
            painter.setPen(QPen(QColor(210, 255, 226, 170), 1));
            painter.drawLine(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1);
        } else {
            // Keep a faint anchor so the left beat reference stays readable.
            painter.setPen(QPen(QColor(117, 142, 171, 90), 1));
            painter.drawLine(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1);
        }
    }
}
