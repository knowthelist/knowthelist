#include "transitionplannerwidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QSettings>
#include <QVBoxLayout>

namespace {
constexpr const char* enabledKeys[] = {
    "Transition/EqualPowerEnabled",
    "Transition/BeatBlendEnabled",
    "Transition/BassSwapEnabled",
    "Transition/VocalHandoffEnabled",
    "Transition/HardCutEnabled"
};
const QColor kWaveformBlue("#7098c8");
}

TransitionPlannerWidget::TransitionPlannerWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("transitionPlanner"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    QSettings settings;
    for (int i = 0; i < modeCount; ++i) {
        const TransitionMode mode = modeForIndex(i);
        m_enabled[i] = settings.value(enabledKeys[i], true).toBool();
        auto* tile = new QWidget(this);
        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(0, 0, 0, 0);
        tileLayout->setSpacing(1);
        m_enabledLeds[i] = new QLed(tile);
        m_enabledLeds[i]->setObjectName(QStringLiteral("transition_enabled_%1").arg(labelForMode(mode)));
        m_enabledLeds[i]->setFixedSize(30, 7);
        m_enabledLeds[i]->setLook(QLed::Flat);
        m_enabledLeds[i]->setShape(QLed::Rectangular);
        m_enabledLeds[i]->setColor(kWaveformBlue);
        m_enabledLeds[i]->installEventFilter(this);
        tileLayout->addWidget(m_enabledLeds[i], 0, Qt::AlignHCenter);
        m_buttons[i] = new QToolButton(tile);
        m_buttons[i]->setObjectName(QStringLiteral("transition_%1").arg(labelForMode(mode)));
        m_buttons[i]->setCheckable(false);
        m_buttons[i]->setIcon(iconForMode(mode));
        m_buttons[i]->setIconSize(QSize(24, 24));
        m_buttons[i]->setFixedSize(30, 30);
        tileLayout->addWidget(m_buttons[i]);
        layout->addWidget(tile);
        connect(m_buttons[i], &QToolButton::clicked, this, [this, mode]() {
            if (!isEnabled(mode))
                toggleEnabled(mode);
            m_overrideMode = mode;
            emit modeOverrideRequested(mode);
            updateButtonStyles();
        });
    }

    if (!isEnabled(TransitionMode::EqualPower)
        && !isEnabled(TransitionMode::BeatBlend)
        && !isEnabled(TransitionMode::BassSwap)
        && !isEnabled(TransitionMode::VocalHandoff)
        && !isEnabled(TransitionMode::HardCut)) {
        m_enabled[indexForMode(TransitionMode::EqualPower)] = true;
        saveEnabledModes();
    }
    updateButtonStyles();
}

int TransitionPlannerWidget::indexForMode(TransitionMode mode)
{
    switch (mode) {
    case TransitionMode::EqualPower: return 0;
    case TransitionMode::BeatBlend: return 1;
    case TransitionMode::BassSwap: return 2;
    case TransitionMode::VocalHandoff: return 3;
    case TransitionMode::HardCut: return 4;
    }
    return 0;
}

TransitionMode TransitionPlannerWidget::modeForIndex(int index)
{
    return static_cast<TransitionMode>(index);
}

QString TransitionPlannerWidget::labelForMode(TransitionMode mode)
{
    switch (mode) {
    case TransitionMode::EqualPower: return QStringLiteral("equal-power");
    case TransitionMode::BeatBlend: return QStringLiteral("beat-blend");
    case TransitionMode::BassSwap: return QStringLiteral("bass-swap");
    case TransitionMode::VocalHandoff: return QStringLiteral("vocal-handoff");
    case TransitionMode::HardCut: return QStringLiteral("hard-cut");
    }
    return QStringLiteral("transition");
}

QIcon TransitionPlannerWidget::iconForMode(TransitionMode mode)
{
    switch (mode) {
    case TransitionMode::EqualPower: return QIcon(QStringLiteral(":/transition_equalpower.svg"));
    case TransitionMode::BeatBlend: return QIcon(QStringLiteral(":/transition_beatblend.svg"));
    case TransitionMode::BassSwap: return QIcon(QStringLiteral(":/transition_bassswap.svg"));
    case TransitionMode::VocalHandoff: return QIcon(QStringLiteral(":/transition_vocalhandoff.svg"));
    case TransitionMode::HardCut: return QIcon(QStringLiteral(":/transition_hardcut.svg"));
    }
    return {};
}

bool TransitionPlannerWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            for (int i = 0; i < modeCount; ++i) {
                if (watched == m_enabledLeds[i]) {
                    toggleEnabled(modeForIndex(i));
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TransitionPlannerWidget::toggleEnabled(TransitionMode mode)
{
    const int index = indexForMode(mode);
    if (m_enabled[index]) {
        int enabledCount = 0;
        for (bool enabled : m_enabled)
            enabledCount += enabled ? 1 : 0;
        if (enabledCount == 1)
            return;
    }
    m_enabled[index] = !m_enabled[index];
    if (!m_enabled[index] && m_overrideMode == mode)
        m_overrideMode.reset();
    saveEnabledModes();
    updateButtonStyles();
    emit enabledModesChanged();
}

void TransitionPlannerWidget::saveEnabledModes() const
{
    QSettings settings;
    for (int i = 0; i < modeCount; ++i)
        settings.setValue(enabledKeys[i], m_enabled[i]);
}

void TransitionPlannerWidget::setPlannedMode(TransitionMode mode)
{
    m_plannedMode = mode;
    updateButtonStyles();
}

void TransitionPlannerWidget::clearOverride()
{
    m_overrideMode.reset();
    updateButtonStyles();
}

TransitionMode TransitionPlannerWidget::effectiveMode() const
{
    return m_overrideMode.value_or(m_plannedMode);
}

bool TransitionPlannerWidget::isEnabled(TransitionMode mode) const
{
    return m_enabled[indexForMode(mode)];
}

void TransitionPlannerWidget::updateButtonStyles()
{
    for (int i = 0; i < modeCount; ++i) {
        const TransitionMode mode = modeForIndex(i);
        const bool active = effectiveMode() == mode;
        const bool enabled = m_enabled[i];
        const QString border = active ? QStringLiteral("1px solid #a0c3e8")
                                      : enabled ? QStringLiteral("1px solid #7098c8")
                                                : QStringLiteral("1px solid #222");
        const QString background = active ? QStringLiteral("#527aa0")
                                          : enabled ? QStringLiteral("#30485f")
                                                    : QStringLiteral("#252525");
        m_buttons[i]->setStyleSheet(QStringLiteral(
            "QToolButton { border: %1; border-radius: 7px; "
            "background: %2; padding: 2px; }")
            .arg(border, background));
        m_buttons[i]->setIcon(iconForMode(mode));
        m_enabledLeds[i]->setState(enabled ? QLed::On : QLed::Off);
        m_enabledLeds[i]->setColor(kWaveformBlue);
        m_buttons[i]->setEnabled(true);
    }
}
