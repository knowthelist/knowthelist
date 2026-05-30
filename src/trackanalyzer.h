/*
    Copyright (C) 2011 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef TRACKANALYSER_H
#define TRACKANALYSER_H

#include <QtCore>
#include <QWidget>
#include <memory>

class JuceAudioBackend;

class TrackAnalyzer : public QWidget
{
    Q_OBJECT
public:
    TrackAnalyzer(QWidget* parent = nullptr);
    ~TrackAnalyzer();

    enum modeType { STANDARD, TEMPO, ENVELOPE };

    bool prepare();
    void open(QUrl url);
    void start();
    bool close();

    double gainDB();
    double gainFactor();
    QTime startPosition();
    QTime endPosition();
    QTime beatPosition();
    QTime beatActivityEndPosition();
    int bpm();
    QVector<float> amplitudeEnvelope() const;
    bool finished() { return m_finished; }
    void setMode(modeType mode);
    void setPosition(QTime position);
    int tempoScanDurationSeconds() const;
    void setTempoScanDurationSeconds(int seconds);

    QTime length();
    static const int GAIN_INVALID = -99;

    void need_finish();

Q_SIGNALS:
    void finishGain();
    void finishTempo();
    void finishEnvelope();

private slots:
    void loadThreadFinished();
    void finalizeAnalysis();

private:
    struct TrackAnalyzer_Private* p;
    std::unique_ptr<JuceAudioBackend> audioBackend;

    double m_GainDB = GAIN_INVALID;
    QTime m_StartPosition = QTime(0, 0);
    QTime m_EndPosition = QTime(0, 0);
    QTime m_BeatPosition = QTime(0, 0);
    QTime m_BeatActivityEndPosition = QTime(0, 0);
    QTime m_MaxPosition = QTime(0, 0);
    bool m_finished = false;
    QVector<float> m_envelope;

    void detectTempo();
    float AutoCorrelation(QList<float> buffer, int frames, int minBpm, int maxBpm, int sampleRate);

    void cleanup();
    void asyncOpen(QUrl url);
};

#endif // TRACKANALYSER_H
