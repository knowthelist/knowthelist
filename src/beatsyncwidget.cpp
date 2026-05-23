/*
    Copyright (C) 2026 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
*/

#include "beatsyncwidget.h"

#include <QPainter>
#include <QtMath>

namespace {
QRect laneRect(const QRect& area, int lane)
{
    const int laneHeight = area.height() / 2;
    const int y = area.top() + lane * laneHeight;
    return QRect(area.left(), y, area.width(), laneHeight - 4);
}

void drawLane(QPainter& painter, const QRect& lane, const QString& label, const BeatSyncWidget::DeckState& state, const QColor& color)
{
    painter.setPen(QColor(52, 58, 70));
    painter.setBrush(QColor(19, 24, 33));
    painter.drawRoundedRect(lane, 6, 6);

    painter.setPen(QColor(180, 190, 210));
    painter.drawText(lane.adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignTop, label);

    QString bpmText = state.bpm > 0 ? QString::number(state.bpm) + " BPM" : QString("-- BPM");
    painter.drawText(lane.adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignTop, bpmText);

    const QRect phaseBand = lane.adjusted(10, 24, -10, -8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(45, 56, 77));
    painter.drawRoundedRect(phaseBand, 4, 4);

    if (state.bpm > 0) {
        const double p = BeatSyncWidget::phase(state);
        const int x = phaseBand.left() + qRound(p * (phaseBand.width() - 1));
        painter.setBrush(color);
        painter.drawRect(x - 2, phaseBand.top(), 4, phaseBand.height());

        painter.setBrush(QColor(255, 255, 255, 45));
        painter.drawRect(phaseBand.left(), phaseBand.top(), 2, phaseBand.height());
    }
}
} // namespace

BeatSyncWidget::BeatSyncWidget(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
}

void BeatSyncWidget::setDeck1(const DeckState& state)
{
    m_deck1 = state;
    update();
}

void BeatSyncWidget::setDeck2(const DeckState& state)
{
    m_deck2 = state;
    update();
}

double BeatSyncWidget::phase(const DeckState& state)
{
    if (state.bpm <= 0)
        return 0.0;

    const double beatMs = 60000.0 / static_cast<double>(state.bpm);
    if (beatMs <= 0.0)
        return 0.0;

    const qint64 posMs = QTime(0, 0).msecsTo(state.position);
    const qint64 beatRefMs = state.beatReference.isValid() ? QTime(0, 0).msecsTo(state.beatReference) : 0;

    double p = fmod(static_cast<double>(posMs - beatRefMs), beatMs) / beatMs;
    if (p < 0.0)
        p += 1.0;
    return qBound(0.0, p, 1.0);
}

void BeatSyncWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(12, 17, 24));

    const QRect area = rect().adjusted(4, 4, -4, -4);
    const QRect upper = laneRect(area, 0);
    const QRect lower = laneRect(area, 1);

    drawLane(painter, upper, QString("Deck A"), m_deck1, QColor(0, 188, 212));
    drawLane(painter, lower, QString("Deck B"), m_deck2, QColor(255, 193, 7));

    QString syncText("SYNC: waiting for BPM");
    if (m_deck1.bpm > 0 && m_deck2.bpm > 0) {
        const double p1 = phase(m_deck1);
        const double p2 = phase(m_deck2);
        const double phaseDelta = qAbs(p1 - p2);
        const double wrapped = qMin(phaseDelta, 1.0 - phaseDelta);
        syncText = QString("SYNC delta: %1%2")
                       .arg(QString::number(wrapped * 100.0, 'f', 1))
                       .arg(" % beat");
    }

    painter.setPen(QColor(185, 200, 220));
    painter.drawText(rect().adjusted(8, 0, -8, -4), Qt::AlignBottom | Qt::AlignHCenter, syncText);
}
