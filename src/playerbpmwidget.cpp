#include "playerbpmwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
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
    m_envelope.append(clamp01(value));

    const int maxSamples = qMax(40, m_windowMs / qMax(1, m_sampleIntervalMs));
    while (m_envelope.size() > maxSamples)
        m_envelope.removeFirst();

    // Mark path cache stale; trigger a repaint via timer (caps to ~30 FPS)
    m_envelopeDirty = true;
    if (!m_updateTimer.isActive())
        m_updateTimer.start();
}

void PlayerBpmWidget::clearEnvelope()
{
    m_envelope.clear();
    m_envFillPath  = QPainterPath();
    m_topEdgePath  = QPainterPath();
    m_bottomEdgePath = QPainterPath();
    m_envelopeDirty = false;
    update();
}

void PlayerBpmWidget::setWindowMilliseconds(int windowMs)
{
    m_windowMs = qBound(2000, windowMs, 20000);
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

    // Fill polygon: top edge forward, bottom edge reversed
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
    const int bandTop = outer.top() + 20;
    const QRect phaseBand(outer.left() + 2, bandTop, outer.width() - 4, qMax(24, outer.bottom() - bandTop - 6));
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
        const qint64 leftMs    = posMs - m_windowMs;

        // Collect all beat lines into one vector, then draw in one call
        QVector<QLineF> beatLines;
        beatLines.reserve(32);
        int beatIndex = static_cast<int>(qFloor(static_cast<double>(leftMs - beatRefMs) / beatMs));
        for (int i = 0; i < 128; ++i) {
            const qint64 beatTime = beatRefMs + static_cast<qint64>((beatIndex + i) * beatMs);
            if (beatTime > posMs)  break;
            if (beatTime < leftMs) continue;
            const double norm = static_cast<double>(beatTime - leftMs) / static_cast<double>(m_windowMs);
            const int x = phaseBand.left() + qRound(norm * phaseBand.width());
            beatLines.append(QLineF(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1));
        }
        painter.setPen(QPen(QColor(102, 128, 161, 140), 1));
        painter.drawLines(beatLines);
    }

    // ── Envelope waveform (cached paths) ──────────────────────────────────────
    if (!m_envelope.isEmpty()) {
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

    // ── Playhead ──────────────────────────────────────────────────────────────
    if (m_bpm > 0) {
        const int x = phaseBand.left() + qRound(phase() * (phaseBand.width() - 1));
        painter.setPen(QPen(m_running ? QColor(28, 214, 116) : QColor(117, 142, 171), 1.0));
        painter.setBrush(m_running ? QColor(28, 214, 116, 110) : QColor(117, 142, 171, 110));
        painter.drawRect(QRect(x - 2, phaseBand.top() + 1, 4, phaseBand.height() - 2));
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
        painter.drawLine(x, phaseBand.top() + 1, x, phaseBand.bottom() - 1);
    }
}

