#ifndef CUSTOMDIAL_H
#define CUSTOMDIAL_H

#include <QtCore>
#include <QDial>

class CustomDial : public QDial
{
    Q_OBJECT

public:

    CustomDial(QWidget * parent = nullptr);

private:

    virtual void paintEvent(QPaintEvent*) override;

};

#endif // CUSTOMDIAL_H
