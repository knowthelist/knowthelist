/*
    Copyright (C) 2026 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
*/

#ifndef PLAYERBPMWIDGET_H
#define PLAYERBPMWIDGET_H

#include <QTime>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QPainterPath>
#include <QRect>

class PlayerBpmWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlayerBpmWidget(QWidget* parent = nullptr);

    void setState(int bpm, const QTime& position, const QTime& beatReference, bool running, bool analysed = true);
    void setTempoInfo(double tempoRate, bool syncAdjusting);
    void appendEnvelopeSample(float value);
    void appendEnvelopeSampleAt(int positionMs, float value);
    void clearEnvelope();
    void setPreloadedEnvelope(const QVector<float>& samples);
    void setWindowMilliseconds(int windowMs);
    int windowMilliseconds() const { return m_windowMs; }
    bool isEnvelopePreloaded() const { return m_envelopePreloaded; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onUpdateTimer();

Q_SIGNALS:
    void envelopeScrubStarted();
    void envelopeScrubPositionChanged(double normalizedPosition, bool finished);

private:
    int m_bpm;
    QTime m_position;
    QTime m_beatReference;
    bool m_running;
    bool m_analysed;
    double m_tempoRate;
    bool m_syncAdjusting;
    QVector<float> m_timelineEnvelope;
    QVector<quint8> m_timelineKnown;
    QVector<float> m_envelope;
    int m_windowMs;
    int m_sampleIntervalMs;

    // CPU optimisation: throttled repaints + cached envelope geometry
    QTimer   m_updateTimer;
    bool     m_envelopeDirty;
    bool     m_envelopePreloaded;
    QRect    m_cachedBand;
    QPainterPath m_envFillPath;
    QPainterPath m_topEdgePath;
    QPainterPath m_bottomEdgePath;
    bool m_scrubbing;
    double m_scrubStartNorm;
    int m_scrubStartX;

    void rebuildEnvelopePaths(const QRect& band, int centerY, double halfH);
    void rebuildVisibleEnvelope(int bandWidth);
    QRect phaseBandRect() const;
    double normalizedX(const QPoint& pos) const;
    double phase() const;
};

#endif // PLAYERBPMWIDGET_H
