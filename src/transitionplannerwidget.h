#ifndef TRANSITIONPLANNERWIDGET_H
#define TRANSITIONPLANNERWIDGET_H

#include "transitionplanner.h"
#include "qled.h"

#include <QToolButton>
#include <QWidget>
#include <optional>

class TransitionPlannerWidget : public QWidget {
    Q_OBJECT

public:
    explicit TransitionPlannerWidget(QWidget* parent = nullptr);

    void setPlannedMode(TransitionMode mode);
    void clearOverride();
    bool hasOverride() const { return m_overrideMode.has_value(); }
    TransitionMode effectiveMode() const;
    bool isEnabled(TransitionMode mode) const;

signals:
    void modeOverrideRequested(TransitionMode mode);
    void enabledModesChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static constexpr int modeCount = 5;
    QToolButton* m_buttons[modeCount]{};
    QLed* m_enabledLeds[modeCount]{};
    bool m_enabled[modeCount]{};
    TransitionMode m_plannedMode{TransitionMode::EqualPower};
    std::optional<TransitionMode> m_overrideMode;

    static int indexForMode(TransitionMode mode);
    static TransitionMode modeForIndex(int index);
    static QString labelForMode(TransitionMode mode);
    static QIcon iconForMode(TransitionMode mode);
    void updateButtonStyles();
    void saveEnabledModes() const;
    void toggleEnabled(TransitionMode mode);
};

#endif
