// Author: SeungJae Lee
// AnalysisStatusPanel: compact status card summarising pass selection and weld alignment offsets.

#include "AnalysisStatusPanel.h"

#include "WeldAlignmentWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kStatusSectionSpacingPx = 3;
constexpr double kMaxAlignmentOffsetMm = 1.0;
}

AnalysisStatusPanel::AnalysisStatusPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("analysisStatusPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(168);

    m_panelLayout = new QVBoxLayout(this);
    m_panelLayout->setContentsMargins(0, 0, 0, 0);
    m_panelLayout->setSpacing(0);

    createSection([&](QWidget *section, QVBoxLayout *sectionLayout) {
        auto *passRow = new QHBoxLayout();
        passRow->setContentsMargins(0, 0, 0, 0);
        passRow->setSpacing(8);

        m_passLabel = createLabel(section);
        m_passLabel->setStyleSheet(QStringLiteral("background: transparent;color:rgba(255,255,255,0.75);font-size:12px;font-weight:600;"));
        passRow->addWidget(m_passLabel, 0, Qt::AlignVCenter);

        m_passIndicatorLabel = createLabel(section);
        m_passIndicatorLabel->setText(QStringLiteral("--"));
        m_passIndicatorLabel->setStyleSheet(QStringLiteral("background: transparent;color:#FFFFFF;font-size:14px;font-weight:700;"));
        passRow->addWidget(m_passIndicatorLabel, 0, Qt::AlignVCenter);

        passRow->addStretch();
        sectionLayout->addLayout(passRow);
    });

    createSection([&](QWidget *section, QVBoxLayout *sectionLayout) {
        auto *alignmentRow = new QHBoxLayout();
        alignmentRow->setContentsMargins(0, 0, 0, 0);
        alignmentRow->setSpacing(8);

        auto *alignmentLeftLabel = createLabel(section);
        alignmentLeftLabel->setText(QStringLiteral("L"));
        alignmentLeftLabel->setStyleSheet(QStringLiteral("background: transparent;color:rgba(255,255,255,0.65);font-size:11px;font-weight:600;"));
        alignmentRow->addWidget(alignmentLeftLabel, 0, Qt::AlignBottom);

        m_alignmentWidget = new WeldAlignmentWidget(section);
        alignmentRow->addWidget(m_alignmentWidget, 1);

        auto *alignmentRightLabel = createLabel(section);
        alignmentRightLabel->setText(QStringLiteral("R"));
        alignmentRightLabel->setStyleSheet(QStringLiteral("background: transparent;color:rgba(255,255,255,0.65);font-size:11px;font-weight:600;"));
        alignmentRow->addWidget(alignmentRightLabel, 0, Qt::AlignBottom);

        sectionLayout->addLayout(alignmentRow);

        m_alignmentValueLabel = createLabel(section);
        m_alignmentValueLabel->setText(QStringLiteral("--"));
        m_alignmentValueLabel->setAlignment(Qt::AlignCenter);
        m_alignmentValueLabel->setStyleSheet(QStringLiteral("background: transparent;color:#00FFB7;font-size:12px;font-weight:600;"));
        sectionLayout->addWidget(m_alignmentValueLabel);
    });

    setPassInfo(QString());
    setAlignmentOffset(std::nullopt, std::nullopt);
}

QWidget *AnalysisStatusPanel::addCustomSection(const std::function<void(QWidget *, QVBoxLayout *)> &builder)
{
    return createSection(builder);
}

void AnalysisStatusPanel::setPassInfo(const QString &passLabel)
{
    m_passValue = passLabel.trimmed();

    if (m_passLabel)
        m_passLabel->setText(tr("Pass"));

    if (m_passIndicatorLabel)
    {
        if (!m_passValue.isEmpty())
        {
            QString displayText = m_passValue;
            if (displayText.compare(QStringLiteral("Root"), Qt::CaseInsensitive) == 0)
                displayText = tr("Root");
            else if (displayText.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0)
                displayText = tr("Second");
            m_passIndicatorLabel->setText(displayText);
        }
        else
            m_passIndicatorLabel->setText(QStringLiteral("--"));
    }
}

void AnalysisStatusPanel::setAlignmentOffset(const std::optional<double> &offsetMm,
                                             const std::optional<double> &warningThresholdMm)
{
    m_alignmentOffset = offsetMm;
    m_alignmentWarningThreshold = warningThresholdMm;
    updateAlignmentDisplay();
}

void AnalysisStatusPanel::clear()
{
    setPassInfo(QString());
    setAlignmentOffset(std::nullopt, std::nullopt);
}

QLabel *AnalysisStatusPanel::createLabel(QWidget *parent) const
{
    auto *label = new QLabel(parent);
    label->setAttribute(Qt::WA_StyledBackground, true);
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    return label;
}

QWidget *AnalysisStatusPanel::createSection(const std::function<void(QWidget *, QVBoxLayout *)> &builder)
{
    if (!m_panelLayout)
        return nullptr;

    auto *container = new QWidget(this);
    container->setAttribute(Qt::WA_StyledBackground, true);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    if (!m_firstSection)
        containerLayout->addSpacing(kStatusSectionSpacingPx);

    auto *section = new QWidget(container);
    section->setObjectName(QStringLiteral("analysisSection"));
    section->setAttribute(Qt::WA_StyledBackground, true);
    section->setStyleSheet(QStringLiteral("background: rgba(211, 237, 248, 15);"));

    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 10, 12, 10);
    sectionLayout->setSpacing(6);

    builder(section, sectionLayout);

    containerLayout->addWidget(section);
    m_panelLayout->addWidget(container);

    m_firstSection = false;
    return section;
}

void AnalysisStatusPanel::updateAlignmentDisplay()
{
    if (!m_alignmentWidget || !m_alignmentValueLabel)
        return;

    const double offsetValue = m_alignmentOffset.value_or(0.0);
    const double normalized = kMaxAlignmentOffsetMm > 0.0
        ? std::clamp(offsetValue / kMaxAlignmentOffsetMm, -1.0, 1.0)
        : 0.0;
    m_alignmentWidget->setOffset(normalized);

    const double magnitude = std::abs(offsetValue);
    const bool warn = m_alignmentWarningThreshold.has_value()
        && m_alignmentWarningThreshold.value() > 0.0
        && magnitude > m_alignmentWarningThreshold.value();
    m_alignmentWidget->setWarningActive(warn);

    QString displayText;
    if (!m_alignmentOffset.has_value())
    {
        displayText = QStringLiteral("--");
    }
    else
    {
        QString valueText;
        if (offsetValue > 0.0)
            valueText = QStringLiteral("+%1").arg(QString::number(offsetValue, 'f', 2));
        else if (offsetValue < 0.0)
            valueText = QString::number(offsetValue, 'f', 2);
        else
            valueText = QStringLiteral("0.00");

        if (offsetValue > 0.0)
            displayText = QStringLiteral("%1 mm %2").arg(valueText, QStringLiteral("→"));
        else if (offsetValue < 0.0)
            displayText = QStringLiteral("← %1 mm").arg(valueText);
        else
            displayText = QStringLiteral("%1 mm").arg(valueText);
    }

    const QString color = (!m_alignmentOffset.has_value())
        ? QStringLiteral("#9EA3AA")
        : (warn ? QStringLiteral("#E5484D") : QStringLiteral("#00FFB7"));
    m_alignmentValueLabel->setText(displayText);
    m_alignmentValueLabel->setStyleSheet(QStringLiteral("background: transparent;color:%1;font-size:12px;font-weight:600;").arg(color));
}
