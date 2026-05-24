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
#include <QVector>
#include <QWidget>

class PlayerBpmWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlayerBpmWidget(QWidget* parent = nullptr);

    void setState(int bpm, const QTime& position, const QTime& beatReference, bool running, bool analysed = true);
    void setTempoInfo(double tempoRate, bool syncAdjusting);
    void appendEnvelopeSample(float value);
    void clearEnvelope();
    void setWindowMilliseconds(int windowMs);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_bpm;
    QTime m_position;
    QTime m_beatReference;
    bool m_running;
    bool m_analysed;
    double m_tempoRate;
    bool m_syncAdjusting;
    QVector<float> m_envelope;
    int m_windowMs;
    int m_sampleIntervalMs;

    double phase() const;
};

#endif // PLAYERBPMWIDGET_H
