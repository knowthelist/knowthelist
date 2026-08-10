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
#include <QPixmap>
#include <QRect>
#include <QFutureWatcher>

class PlayerBpmWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlayerBpmWidget(QWidget* parent = nullptr);

    struct RebuildResult {
        QVector<float> envelope;
        QPainterPath envFillPath;
        QPainterPath topEdgePath;
        QPainterPath bottomEdgePath;
        QRect band;
    };

    void setState(int bpm, const QTime& position, const QTime& beatReference, bool running, bool analyzed = true);
    void setState(int bpm, double exactBpm, const QTime& position, const QTime& beatReference, bool running, bool analyzed = true);
    void setExactBpm(double exactBpm);
    void setTempoInfo(double tempoRate, bool syncAdjusting, qint64 syncCompleted = 0);
    void setCuePosition(const QTime& position);
    void appendEnvelopeSample(float value);
    void appendEnvelopeSampleAt(int positionMs, float value);
    void clearEnvelope();
    void setPreloadedEnvelope(const QVector<float>& samples, int sourceIntervalMs = 0,
                              int sourceDurationMs = 0);
    void setWindowMilliseconds(int windowMs);
    void setTrackLength(const QTime& length);
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
    QTime m_cuePosition;
    QTime m_beatReference;
    QTime m_trackLength;
    bool m_running;
    bool m_analyzed;
    double m_tempoRate;
    bool m_syncAdjusting;
    qint64 m_syncCompleted;  // Timestamp of last sync completion (0 if none)
    QVector<float> m_timelineEnvelope;
    QVector<quint8> m_timelineKnown;
    QVector<float> m_envelope;
    int m_windowMs;
    int m_liveSampleIntervalMs;
    double m_exactBpm;
    double m_trueSampleIntervalMs;  // Calculated from track length / sample count (not rounded)

    QFutureWatcher<RebuildResult> m_rebuildWatcher;
    QRect    m_requestedBand;
    int      m_requestedCenterY;
    double   m_requestedHalfH;
    bool     m_rebuildRequested;

    // CPU optimisation: throttled repaints + cached envelope geometry
    QTimer   m_updateTimer;
    bool     m_envelopeDirty;
    bool     m_envelopePreloaded;
    QRect    m_cachedBand;
    QPainterPath m_envFillPath;
    QPainterPath m_topEdgePath;
    QPainterPath m_bottomEdgePath;
    QPixmap m_waveformLayer;
    int m_waveformLayerHeight;
    int m_waveformLayerSampleCount;
    bool m_waveformLayerDirty;
    bool m_scrubbing;
    double m_scrubStartNorm;
    int m_scrubStartX;

    void rebuildEnvelopePaths(const QRect& band, int centerY, double halfH);
    void invalidateWaveformLayer();
    void rebuildWaveformLayer(int bandHeight);
    void rebuildVisibleEnvelope(int bandWidth);
    int visibleWindowLeftMs() const;
    QTime visualBeatReference() const;
    QRect phaseBandRect() const;
    double normalizedX(const QPoint& pos) const;
    double phase() const;
    void requestEnvelopeRebuild(const QRect& band, int centerY, double halfH);
    void onRebuildFinished();
};

#endif // PLAYERBPMWIDGET_H
