#include "playerbpmwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QtMath>

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
} // namespace

PlayerBpmWidget::PlayerBpmWidget(QWidget* parent)
    : QWidget(parent)
    , m_bpm(0)
    , m_position(QTime(0, 0))
    , m_beatReference(QTime())
    , m_running(false)
    , m_analysed(false)
    , m_tempoRate(1.0)
    , m_syncAdjusting(false)
    , m_windowMs(6000)
    , m_sampleIntervalMs(50)
    , m_envelopeDirty(false)
    , m_envelopePreloaded(false)
    , m_scrubbing(false)
    , m_scrubStartNorm(0.0)
    , m_scrubStartX(0)
{
    setAutoFillBackground(false);

    // Throttle envelope repaints to ~30 FPS maximum
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(33);
    connect(&m_updateTimer, &QTimer::timeout, this, &PlayerBpmWidget::onUpdateTimer);
}

void PlayerBpmWidget::onUpdateTimer()
{
    update();
}

void PlayerBpmWidget::setState(int bpm, const QTime& position, const QTime& beatReference, bool running, bool analysed)
{
    m_bpm = bpm;
    m_position = position;
    m_beatReference = beatReference;
    m_running = running;
    m_analysed = analysed;
    // The envelope is position-anchored: moving playback position must move the visible waveform window.
    m_envelopeDirty = true;
    update();
}

void PlayerBpmWidget::setTempoInfo(double tempoRate, bool syncAdjusting)
{
    m_tempoRate = tempoRate;
    m_syncAdjusting = syncAdjusting;
    update();
}

void PlayerBpmWidget::appendEnvelopeSample(float value)
{
    appendEnvelopeSampleAt(QTime(0, 0).msecsTo(m_position), value);
}

void PlayerBpmWidget::appendEnvelopeSampleAt(int positionMs, float value)
{
    if (positionMs < 0)
        return;

    const int sampleIndex = positionMs / qMax(1, m_sampleIntervalMs);
    if (sampleIndex < 0)
        return;

    if (m_timelineEnvelope.size() <= sampleIndex)
        m_timelineEnvelope.resize(sampleIndex + 1);
    if (m_timelineKnown.size() <= sampleIndex)
        m_timelineKnown.resize(sampleIndex + 1);

    m_timelineEnvelope[sampleIndex] = clamp01(value);
    m_timelineKnown[sampleIndex] = 1;

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
    m_envelopeDirty = false;
    m_envelopePreloaded = false;
    update();
}

void PlayerBpmWidget::setPreloadedEnvelope(const QVector<float>& samples)
{
    if (samples.isEmpty())
        return;
    m_timelineEnvelope.clear();
    m_timelineKnown.clear();
    m_timelineEnvelope.reserve(samples.size());
    m_timelineKnown.reserve(samples.size());
    for (float v : samples)
        m_timelineEnvelope.append(qBound(0.0f, v, 1.0f));
    for (int i = 0; i < samples.size(); ++i)
        m_timelineKnown.append(1);
    m_envelopeDirty = true;
    m_envelopePreloaded = true;
    if (!m_updateTimer.isActive())
        m_updateTimer.start();
}

void PlayerBpmWidget::setWindowMilliseconds(int windowMs)
{
    m_windowMs = qBound(2000, windowMs, 20000);
    m_envelopeDirty = true;
}

void PlayerBpmWidget::rebuildVisibleEnvelope()
{
    const int intervalMs = qMax(1, m_sampleIntervalMs);
    const int minVisibleSamples = 40;
    const int targetSamples = qMax(minVisibleSamples, m_windowMs / intervalMs);
    m_envelope.resize(targetSamples);

    const int posMs = QTime(0, 0).msecsTo(m_position);
    // Playhead is at 20% from the left edge.
    const int leftMs = posMs - qRound(0.20 * m_windowMs);
    const double stepMs = (targetSamples > 1)
        ? static_cast<double>(m_windowMs) / static_cast<double>(targetSamples - 1)
        : 0.0;

    for (int i = 0; i < targetSamples; ++i) {
        const int sampleTimeMs = leftMs + qRound(static_cast<double>(i) * stepMs);
        if (sampleTimeMs < 0) {
            m_envelope[i] = 0.0f;
            continue;
        }

        const int timelineIndex = sampleTimeMs / intervalMs;
        if (timelineIndex >= 0 && timelineIndex < m_timelineEnvelope.size()
            && timelineIndex < m_timelineKnown.size() && m_timelineKnown.at(timelineIndex)) {
            m_envelope[i] = m_timelineEnvelope.at(timelineIndex);
        } else {
            m_envelope[i] = 0.0f;
        }
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
    m_envelopeDirty = true; // geometry changed – rebuild paths
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

    const double invCount = 1.0 / (count - 1);
    const double bandLeft = band.left();
    const double bandW    = band.width();

    // Fill polygon: older samples at left, newest samples at right (right-to-left motion)
    m_envFillPath = QPainterPath();
    m_envFillPath.moveTo(bandLeft, centerY);
    for (int i = 0; i < count; ++i) {
        const double x = bandLeft + i * invCount * bandW;
        m_envFillPath.lineTo(x, centerY - m_envelope.at(i) * halfH);
    }
    m_envFillPath.lineTo(bandLeft + bandW, centerY);
    for (int i = count - 1; i >= 0; --i) {
        const double x = bandLeft + i * invCount * bandW;
        m_envFillPath.lineTo(x, centerY + m_envelope.at(i) * halfH);
    }
    m_envFillPath.closeSubpath();

    // Outline edges
    m_topEdgePath    = QPainterPath();
    m_bottomEdgePath = QPainterPath();
    for (int i = 0; i < count; ++i) {
        const double x  = bandLeft + i * invCount * bandW;
        const double yt = centerY - m_envelope.at(i) * halfH;
        const double yb = centerY + m_envelope.at(i) * halfH;
        if (i == 0) { m_topEdgePath.moveTo(x, yt); m_bottomEdgePath.moveTo(x, yb); }
        else        { m_topEdgePath.lineTo(x, yt); m_bottomEdgePath.lineTo(x, yb); }
    }
}

double PlayerBpmWidget::phase() const
{
    if (m_bpm <= 0) return 0.0;
    const double beatMs = 60000.0 / static_cast<double>(m_bpm);
    if (beatMs <= 0.0) return 0.0;

    const qint64 posMs    = QTime(0, 0).msecsTo(m_position);
    const qint64 beatRefMs = m_beatReference.isValid() ? QTime(0, 0).msecsTo(m_beatReference) : 0;

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
    if (!m_analysed)
        bpmText = "Analysing BPM...";
    else if (m_bpm > 0) {
        bpmText = QString::number(m_bpm) + " BPM";
        const double adjusted = static_cast<double>(m_bpm) * m_tempoRate;
        if (qAbs(adjusted - static_cast<double>(m_bpm)) >= 0.05)
            bpmText += " (" + formatTempoValue(adjusted) + ")";
    } else {
        bpmText = "No BPM detected";
    }
    painter.setPen(QColor(222, 231, 242));
    painter.drawText(outer.adjusted(8, 2, -38, -2), Qt::AlignLeft | Qt::AlignTop, bpmText);
    painter.setRenderHint(QPainter::Antialiasing, false); // off again

    // ── Sync dot ─────────────────────────────────────────────────────────────
    if (m_syncAdjusting) {
        const int blink = (QTime::currentTime().msecsSinceStartOfDay() / 300) % 2;
        painter.setPen(QPen(QColor(255, 190, 64), 1.0));
        painter.setBrush(blink == 0 ? QColor(255, 190, 64) : QColor(66, 78, 94));
        painter.drawEllipse(QRect(outer.right() - 20, outer.top() + 6, 10, 10));
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
        const double beatMs    = 60000.0 / static_cast<double>(m_bpm);
        const qint64 posMs     = QTime(0, 0).msecsTo(m_position);
        const qint64 beatRefMs = m_beatReference.isValid() ? QTime(0, 0).msecsTo(m_beatReference) : 0;
        // Playhead is at 20% from the left edge.
        const qint64 leftMs    = posMs - static_cast<qint64>(qRound(0.20 * m_windowMs));
        const qint64 rightMs   = leftMs + m_windowMs;

        // Collect all beat lines into one vector, then draw in one call
        QVector<QLineF> beatLines;
        beatLines.reserve(32);
        int beatIndex = static_cast<int>(qFloor(static_cast<double>(leftMs - beatRefMs) / beatMs));
        for (int i = 0; i < 128; ++i) {
            const qint64 beatTime = beatRefMs + static_cast<qint64>((beatIndex + i) * beatMs);
            if (beatTime > rightMs)  break;
            if (beatTime < leftMs) continue;
            const double norm = static_cast<double>(beatTime - leftMs) / static_cast<double>(m_windowMs);
            const int x = phaseBand.left() + qRound(norm * phaseBand.width());
            beatLines.append(QLineF(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1));
        }
        painter.setPen(QPen(QColor(102, 128, 161, 140), 1));
        painter.drawLines(beatLines);
    }

    // ── Envelope waveform (cached paths) ──────────────────────────────────────
    if (!m_timelineEnvelope.isEmpty()) {
        if (m_envelopeDirty)
            rebuildVisibleEnvelope();

        const double halfH = qMax(4.0, (phaseBand.height() / 2.0) - 2.0);

        // Rebuild only when data or geometry changed
        if (m_envelopeDirty || m_cachedBand != phaseBand) {
            rebuildEnvelopePaths(phaseBand, centerY, halfH);
            m_cachedBand    = phaseBand;
            m_envelopeDirty = false;
        }

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

    // ── Beat cursor (flash exactly when a beat tick crosses the reference) ───
    if (m_bpm > 0 && m_windowMs > 0) {
        const double beatMs = 60000.0 / static_cast<double>(m_bpm);
        const qint64 posMs = QTime(0, 0).msecsTo(m_position);
        const qint64 beatRefMs = m_beatReference.isValid() ? QTime(0, 0).msecsTo(m_beatReference) : 0;
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
        const bool flashOn = m_running && !beatAtMarker;

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

