/*
    Copyright (C) 2026 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
*/

#include "playerbpmwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
float clamp01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

QString formatTempoValue(double tempo)
{
    if (qAbs(tempo - qRound(tempo)) < 0.05)
        return QString::number(qRound(tempo));
    return QString::number(tempo, 'f', 1);
}
}

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
{
    setAutoFillBackground(false);
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
}

void PlayerBpmWidget::clearEnvelope()
{
    m_envelope.clear();
    update();
}

void PlayerBpmWidget::setWindowMilliseconds(int windowMs)
{
    m_windowMs = qBound(2000, windowMs, 20000);
}

double PlayerBpmWidget::phase() const
{
    if (m_bpm <= 0)
        return 0.0;

    const double beatMs = 60000.0 / static_cast<double>(m_bpm);
    if (beatMs <= 0.0)
        return 0.0;

    const qint64 posMs = QTime(0, 0).msecsTo(m_position);
    const qint64 beatRefMs = m_beatReference.isValid() ? QTime(0, 0).msecsTo(m_beatReference) : 0;

    double p = fmod(static_cast<double>(posMs - beatRefMs), beatMs) / beatMs;
    if (p < 0.0)
        p += 1.0;

    return qBound(0.0, p, 1.0);
}

void PlayerBpmWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(17, 24, 35));

    const QRect outer = rect().adjusted(1, 1, -1, -1);
    painter.setPen(QColor(51, 65, 89));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outer, 6, 6);

    QString bpmText;
    if (!m_analysed)
        bpmText = QString("Analysing BPM...");
    else if (m_bpm > 0) {
        bpmText = QString::number(m_bpm) + " BPM";
        const double adjustedTempo = static_cast<double>(m_bpm) * m_tempoRate;
        if (qAbs(adjustedTempo - static_cast<double>(m_bpm)) >= 0.05)
            bpmText += " (" + formatTempoValue(adjustedTempo) + ")";
    }
    else
        bpmText = QString("No BPM detected");
    painter.setPen(QColor(214, 226, 243));
    const QRect textRect = outer.adjusted(8, 2, -32, -2);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, bpmText);

    if (m_syncAdjusting) {
        const int blinkSlice = (QTime::currentTime().msecsSinceStartOfDay() / 300) % 2;
        painter.setPen(QPen(QColor(255, 190, 64), 1.0));
        painter.setBrush(blinkSlice == 0 ? QColor(255, 190, 64) : QColor(66, 78, 94));
        const QRect syncDot(outer.right() - 20, outer.top() + 6, 10, 10);
        painter.drawEllipse(syncDot);
    }

    const int bandTop = outer.top() + 18;
    const QRect phaseBand(outer.left() + 6, bandTop, outer.width() - 12, qMax(22, outer.bottom() - bandTop - 5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(36, 48, 68));
    painter.drawRoundedRect(phaseBand, 4, 4);

    if (m_bpm > 0 && m_windowMs > 0) {
        const double beatMs = 60000.0 / static_cast<double>(m_bpm);
        const qint64 posMs = QTime(0, 0).msecsTo(m_position);
        const qint64 beatRefMs = m_beatReference.isValid() ? QTime(0, 0).msecsTo(m_beatReference) : 0;
        const qint64 leftMs = posMs - m_windowMs;

        painter.setPen(QPen(QColor(95, 120, 154, 170), 1));
        int beatIndex = static_cast<int>(qFloor((leftMs - beatRefMs) / beatMs));
        for (int i = 0; i < 128; ++i) {
            const qint64 beatTime = beatRefMs + static_cast<qint64>((beatIndex + i) * beatMs);
            if (beatTime > posMs)
                break;
            if (beatTime < leftMs)
                continue;
            const double norm = static_cast<double>(beatTime - leftMs) / static_cast<double>(m_windowMs);
            const int x = phaseBand.left() + qRound(norm * phaseBand.width());
            painter.drawLine(x, phaseBand.top(), x, phaseBand.bottom());
        }
    }

    if (!m_envelope.isEmpty()) {
        QPainterPath envPath;
        const int count = m_envelope.size();
        for (int i = 0; i < count; ++i) {
            const double nx = (count > 1) ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.0;
            const int x = phaseBand.left() + qRound(nx * phaseBand.width());
            const int y = phaseBand.bottom() - qRound(m_envelope.at(i) * phaseBand.height());
            if (i == 0)
                envPath.moveTo(x, y);
            else
                envPath.lineTo(x, y);
        }

        painter.setPen(QPen(QColor(255, 160, 80), 2.0));
        painter.drawPath(envPath);
    }

    if (m_bpm > 0) {
        const int x = phaseBand.left() + qRound(phase() * (phaseBand.width() - 1));
        painter.setBrush(m_running ? QColor(28, 214, 116) : QColor(117, 142, 171));
        painter.drawRect(x - 2, phaseBand.top(), 4, phaseBand.height());
    }
}
