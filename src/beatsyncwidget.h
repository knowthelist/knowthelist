/*
    Copyright (C) 2026 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
*/

#ifndef BEATSYNCWIDGET_H
#define BEATSYNCWIDGET_H

#include <QTime>
#include <QWidget>

class BeatSyncWidget : public QWidget {
    Q_OBJECT
public:
    struct DeckState {
        int bpm;
        bool running;
        QTime position;
        QTime beatReference;

        DeckState()
            : bpm(0)
            , running(false)
            , position(QTime(0, 0))
            , beatReference(QTime())
        {
        }
    };

    explicit BeatSyncWidget(QWidget* parent = nullptr);

    void setDeck1(const DeckState& state);
    void setDeck2(const DeckState& state);
    static double phase(const DeckState& state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    DeckState m_deck1;
    DeckState m_deck2;
};

#endif // BEATSYNCWIDGET_H
